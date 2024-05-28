//
// Created by jmanc3 on 9/30/21.
//

#include "audio.h"
#include "application.h"
#include "volume_mapping.h"
#include "utility.h"
#include "defer.h"
#include "icons.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif


#include "../src/settings_menu.h"

#include <pulse/mainloop.h>
#include <pulse/mainloop-api.h>
#include <iostream>
#include <cstring>
#include <cmath>
#include <condition_variable>

// Global stuff
bool audio_running = false;
bool allow_audio_thread_creation = true;

static App *app;

enum AudioBackend {
    UNSET,
    PULSEAUDIO,
    ALSA
};

// General stuff
static AudioBackend backend = AudioBackend::UNSET;
snd_mixer_t *alsa_handle = nullptr;
snd_mixer_elem_t *master_volume = nullptr;
snd_mixer_selem_id_t *master_sid = nullptr;
snd_mixer_elem_t *headphone_volume = nullptr;
snd_mixer_selem_id_t *headphone_sid = nullptr;
snd_mixer_elem_t *speaker_volume = nullptr;
snd_mixer_selem_id_t *speaker_sid = nullptr;

// Pulseaudio stuff
static bool ready = false;
static pa_mainloop *mainloop;
static pa_mainloop_api *mainloop_api;
static pa_context *context;
static std::atomic<bool> audio_thread_busy = false;
static std::mutex read_mutex;
static std::mutex signal_mutex;
static std::mutex free_to_continue_mutex;
static std::condition_variable needs_signal_condition;
static std::condition_variable free_to_continue_condition;
static std::atomic<bool> needs_block = false;
static bool signal_met = false;
static bool free_to_continue = true;
static std::string default_sink_name;
static std::thread::id audio_thread_id;

static void (*audio_change_callback)() = nullptr;

std::vector<AudioClient *> audio_clients;

void sort();

static void on_stream_suspend(pa_stream *s, void *data) {
    auto *client = (AudioClient *) data;
    
    if (pa_stream_is_suspended(s)) {
        if (client->peak != 0) {
            client->peak = 0;
            client->last_time_peak_changed = get_current_time_in_ms();
        }
        sort();
        if (audio_change_callback)
            audio_change_callback();
    }
}

static void on_stream_update(pa_stream *stream, size_t length, void *data) {
    auto *client = (AudioClient *) data;
    
    const void *buffer = nullptr;
    if (pa_stream_peek(stream, &buffer, &length) < 0) {
        fprintf(stderr, "pa_stream_peek() fail: %s", pa_strerror(pa_context_errno(pa_stream_get_context(stream))));
        return;
    }
    if (!buffer) {
        /* NULL data means either a hole or empty buffer. Only drop the stream when there is a hole (length > 0) */
        if (length)
            pa_stream_drop(stream);
        return;
    }
    
    double v = ((const float *) buffer)[length / sizeof(float) - 1];
    pa_stream_drop(stream);
    if (client->peak != std::min(std::max(v, 0.0), 1.0)) {
        client->last_time_peak_changed = get_current_time_in_ms();
    }
    client->peak = std::min(std::max(v, 0.0), 1.0);
    sort();
    if (audio_change_callback)
        audio_change_callback();
}

void sort() {
    auto time = get_current_time_in_ms();
    std::sort(audio_clients.begin(), audio_clients.end(), [time](AudioClient *a, AudioClient *b) {
        // order: master who is default sink, masters with older indexes first, sink inputs with lower index first
        if (a->is_master && b->is_master) {
            if (a->title == default_sink_name) {
                return true;
            } else if (b->title == default_sink_name) {
                return false;
            } else {
                return a->index < b->index;
            }
        } else if (a->is_master && !b->is_master) {
            return true;
        } else if (b->is_master && !a->is_master) {
            return false;
        } else {
            if ((a->peak == 0 && b->peak == 0) || (a->peak != 0 && b->peak != 0)) {
                if (a->peak != 0) {
                    if ((time - a->last_time_peak_changed < 1300) && (time - b->last_time_peak_changed > 1300)) {
                        return true;
                    } else if ((time - b->last_time_peak_changed < 1300) && (time - a->last_time_peak_changed > 1300)) {
                        return false;
                    }
                }
                return a->index < b->index;
            } else {
                return a->peak != 0;
            }
        }
    });
}

static void on_sink(pa_context *, const pa_sink_info *l, int eol, void *) {
    if (eol != 0 || l == nullptr)
        return;
    defer(if (audio_change_callback) audio_change_callback());
    
    auto data = (pa_sink_info *) l;
    
    AudioClient *client = nullptr;
    bool create = true;
    for (auto c: audio_clients) {
        if (c->is_master && c->index == data->index) {
            client = c;
            create = false;
            break;
        }
    }
    if (client == nullptr)
        client = new AudioClient;
    client->title = data->name;
    client->subtitle = data->description;
    client->index = data->index;
    client->icon_name = "audio-card";
    client->is_master = true;
    client->monitor_source_name = l->monitor_source_name;
    client->volume = data->volume;
    client->mute = data->mute;
    if (create) {
        audio_clients.push_back(client);
        sort();
    }
}

static void on_sink_input(pa_context *, const pa_sink_input_info *l, int eol, void *) {
    if (eol != 0 || l == nullptr)
        return;
    defer(if (audio_change_callback) audio_change_callback());
    
    auto data = (pa_sink_input_info *) l;
    
    AudioClient *client = nullptr;
    bool create = true;
    for (auto c: audio_clients) {
        if (!c->is_master && c->index == data->index) {
            client = c;
            create = false;
            break;
        }
    }
    if (client == nullptr)
        client = new AudioClient;
    client->title = data->name;
    client->index = data->index;
    
    if (l->proplist) {
        size_t nbytes;
        const void *data = nullptr;
        pa_proplist_get(l->proplist, PA_PROP_APPLICATION_NAME, &data, &nbytes);
        if (data) {
            client->subtitle = std::string((const char *) data, nbytes);
            data = nullptr;
        }
        
        if (client->icon_name.empty()) {
            pa_proplist_get(l->proplist, PA_PROP_APPLICATION_ICON_NAME, &data, &nbytes);
            if (data) {
                std::string possible_icon_name = std::string((const char *) data, nbytes);
                data = nullptr;
                if (has_options(possible_icon_name))
                    client->icon_name = possible_icon_name;
                if (client->icon_name.empty()) {
                    for (char &i: possible_icon_name)
                        i = tolower(i);
                    if (has_options(possible_icon_name))
                        client->icon_name = possible_icon_name;
                    
                    if (client->icon_name.empty()) {
                        possible_icon_name = client->title;
                        if (!client->subtitle.empty())
                            possible_icon_name = client->subtitle;
                        if (has_options(possible_icon_name))
                            client->icon_name = possible_icon_name;
                        if (client->icon_name.empty()) {
                            for (char &i: possible_icon_name)
                                i = tolower(i);
                            if (has_options(possible_icon_name))
                                client->icon_name = possible_icon_name;
                        }
                    }
                }
            } else {
                std::string possible_icon_name = client->title;
                if (!client->subtitle.empty())
                    possible_icon_name = client->subtitle;
                if (has_options(possible_icon_name))
                    client->icon_name = possible_icon_name;
                if (client->icon_name.empty()) {
                    for (char &i: possible_icon_name)
                        i = tolower(i);
                    if (has_options(possible_icon_name))
                        client->icon_name = possible_icon_name;
                }
            }
        }
    }
    client->volume = data->volume;
    client->mute = data->mute;
    if (create) {
        audio_clients.push_back(client);
        sort();
    }
}

static void on_server(pa_context *, const pa_server_info *i, void *) {
    if (i == nullptr)
        return;
    defer(if (audio_change_callback) audio_change_callback());
    default_sink_name = i->default_sink_name;
    sort();
}

// Callback function to handle volume events
static void change_callback(pa_context *c, pa_subscription_event_type_t event_type, uint32_t index, void *userdata) {
    pa_subscription_event_type_t event_type_masked;
    pa_operation *o;
    
    event_type_masked = static_cast<pa_subscription_event_type_t>(event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
    
    switch (event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK: {
            if (event_type_masked == PA_SUBSCRIPTION_EVENT_REMOVE) {
                for (int i = 0; i < audio_clients.size(); i++) {
                    if (audio_clients[i]->index == index && audio_clients[i]->is_master) {
                        delete audio_clients[i];
                        audio_clients.erase(audio_clients.begin() + i);
                        sort();
                        break;
                    }
                }
            } else {
                if (!(o = pa_context_get_sink_info_by_index(c, index, on_sink, userdata))) {
                    fprintf(stderr, "ERROR: pa_context_get_sink_info_by_index() failed\n");
                    break;
                }
                pa_operation_unref(o);
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT: {
            if (event_type_masked == PA_SUBSCRIPTION_EVENT_REMOVE) {
                for (int i = 0; i < audio_clients.size(); i++) {
                    if (audio_clients[i]->index == index && !audio_clients[i]->is_master) {
                        delete audio_clients[i];
                        audio_clients.erase(audio_clients.begin() + i);
                        sort();
                        break;
                    }
                }
            } else {
                if (!(o = pa_context_get_sink_input_info(c, index, on_sink_input, userdata))) {
                    fprintf(stderr, "ERROR: pa_context_get_sink_input_info() failed\n");
                    break;
                }
                pa_operation_unref(o);
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_SERVER: {
            if (!(o = pa_context_get_server_info(c, on_server, userdata))) {
                fprintf(stderr, "ERROR: pa_context_get_server_info() failed\n");
                break;
            }
            pa_operation_unref(o);
            break;
        }
    }
}

static void iterate_pulseaudio_mainloop() {
    if (pa_mainloop_prepare(mainloop, -1) < 0) {
        audio_running = false;
        return;
    }
    if (pa_mainloop_poll(mainloop) < 0) {
        audio_running = false;
        return;
    }
    
    std::unique_lock<std::mutex> read_lock(read_mutex);
    
    audio_thread_busy = true;
    defer(audio_thread_busy = false);
    if (pa_mainloop_dispatch(mainloop) < 0) {
        audio_running = false;
        return;
    }
    if (needs_block) {
        needs_block = false;
        
        std::unique_lock<std::mutex> free_to_continue_lock(free_to_continue_mutex);
        {
            std::unique_lock<std::mutex> lock(signal_mutex);
            signal_met = true;
            needs_signal_condition.notify_all();
        }
        free_to_continue_condition.wait_for(free_to_continue_lock, std::chrono::milliseconds(70),
                                            []() { return free_to_continue; });
    }
}

static bool try_establishing_connection_with_pulseaudio() {
    mainloop = pa_mainloop_new();
    if (!mainloop) return false;
    mainloop_api = pa_mainloop_get_api(mainloop);
    if (!mainloop_api) return false;
    
    pa_proplist *prop_list = pa_proplist_new();
    if (!prop_list) return false;
    defer(pa_proplist_free(prop_list));
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_NAME, ("Winbar Volume Control"));
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ID, "winbar.volume");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_VERSION, "1");
    context = pa_context_new_with_proplist(mainloop_api, "Winbar Volume Control", prop_list);
    if (!context) {
        pa_mainloop_free(mainloop);
        return false;
    }
    
    if (pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
        return false;
    }
    
    pa_context_set_state_callback(context, [](pa_context *c, void *) {
        pa_context_state_t state = pa_context_get_state(c);
        if (state == PA_CONTEXT_READY) {
            pa_context_set_subscribe_callback(c, change_callback, nullptr);
            auto mask = (pa_subscription_mask_t) (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                                  PA_SUBSCRIPTION_MASK_SERVER);
            pa_operation *pa_op = pa_context_subscribe(c, mask, nullptr, nullptr);
            pa_operation_unref(pa_op);
            
            pa_op = pa_context_get_server_info(c, on_server, nullptr);
            pa_operation_unref(pa_op);
            pa_op = pa_context_get_sink_info_list(c, on_sink, nullptr);
            pa_operation_unref(pa_op);
            pa_op = pa_context_get_sink_input_info_list(c, on_sink_input, nullptr);
            pa_operation_unref(pa_op);
            ready = true;
        }
        if (!PA_CONTEXT_IS_GOOD(state)) {
            audio_running = false;
            ready = false;
        }
    }, nullptr);
    
    audio_running = true;
    while (audio_running && !ready) {
        iterate_pulseaudio_mainloop();
    }
    return ready;
}

void alsa_event_pumping_required_callback(App *, int, void *) {
    snd_mixer_handle_events(alsa_handle);
}

static int alsa_state_change_callback(snd_mixer_elem_t *elem, unsigned int) {
    if (!audio_change_callback) return 0;
    if (master_volume != elem) return 0;
    if (!audio_running) return 0;
    
    double volume = get_normalized_playback_volume(master_volume, SND_MIXER_SCHN_FRONT_LEFT);
    int mute;
    snd_mixer_selem_get_playback_switch(elem, snd_mixer_selem_channel_id_t::SND_MIXER_SCHN_FRONT_LEFT, &mute);
    
    if (!audio_clients.empty()) {
        audio_clients[0]->alsa_volume = volume;
        audio_clients[0]->mute = !((bool) mute);
    }
    audio_change_callback();
    return 0;
}

bool try_establishing_connection_with_alsa() {
    if (snd_mixer_open(&alsa_handle, 0) != 0)
        return false;
    
    const char *card = "default"; // can be replaced with hw:0
    if (snd_mixer_attach(alsa_handle, card) != 0) {
        snd_mixer_close(alsa_handle);
        return false;
    }
    if (snd_mixer_selem_register(alsa_handle, NULL, NULL) != 0) {
        snd_mixer_close(alsa_handle);
        return false;
    }
    if (snd_mixer_load(alsa_handle) != 0) {
        snd_mixer_close(alsa_handle);
        return false;
    }
    
    snd_mixer_selem_id_malloc(&master_sid);
    snd_mixer_selem_id_set_index(master_sid, 0);
    
    const char *selem_name = "Master";
    snd_mixer_selem_id_set_name(master_sid, selem_name);
    if ((master_volume = snd_mixer_find_selem(alsa_handle, master_sid)) == nullptr) {
        snd_mixer_selem_id_free(master_sid);
        master_sid = nullptr;
        return false;
    }
    
    // The reason we can't just use Master is because when you mute the master, all the children get muted, but when you
    // un-mute, they don't get un-muted.
    { // Headphone (optional)
        snd_mixer_selem_id_malloc(&headphone_sid);
        snd_mixer_selem_id_set_index(headphone_sid, 0);
        
        const char *name = "Headphone";
        snd_mixer_selem_id_set_name(headphone_sid, name);
        if ((headphone_volume = snd_mixer_find_selem(alsa_handle, headphone_sid)) == nullptr) {
            headphone_sid = nullptr;
            snd_mixer_selem_id_free(headphone_sid);
        }
    }
    { // Speaker Front (optional)
        snd_mixer_selem_id_malloc(&speaker_sid);
        snd_mixer_selem_id_set_index(speaker_sid, 0);
        
        const char *name = "Speaker Front";
        snd_mixer_selem_id_set_name(speaker_sid, name);
        if ((speaker_volume = snd_mixer_find_selem(alsa_handle, speaker_sid)) == nullptr) {
            speaker_sid = nullptr;
            snd_mixer_selem_id_free(speaker_sid);
        }
    }
    
    // Watch for events and pump them when required
    int i, nfds = snd_mixer_poll_descriptors_count(alsa_handle);
    struct pollfd pfds[nfds];
    if (snd_mixer_poll_descriptors(alsa_handle, pfds, nfds) < 0)
        return false;
    {
        std::lock_guard<std::mutex> lock(app->running_mutex);
        for (i = 0; i < nfds; i++) {
            poll_descriptor(app, pfds[i].fd, pfds[i].events, alsa_event_pumping_required_callback, nullptr,
                            "try_establishing_connection_with_alsa");
        }
    }
    
    snd_mixer_elem_set_callback(master_volume, alsa_state_change_callback);
    
    auto c = new AudioClient();
    audio_clients.push_back(c);
    c->is_master = true;
    c->title = "Master";
    c->icon_name = "audio-card";
    c->index = snd_mixer_selem_get_index(master_volume);
    default_sink_name = "Master";
    
    alsa_state_change_callback(master_volume, 0);
    
    long min, max;
    snd_mixer_selem_get_playback_volume_range(master_volume, &min, &max);
    long current_volume;
    snd_mixer_selem_get_playback_volume(master_volume, SND_MIXER_SCHN_FRONT_LEFT, &current_volume);
    double volume_percentage = ((double) (current_volume - min)) / (double) (max - min);
    c->cached_volume = volume_percentage;
    c->alsa_volume = volume_percentage;
    
    return true;
}

static std::vector<std::thread> threads;

void audio_sort() {
    sort();
}

void audio_start(App *app_ref) {
    app = app_ref;
    
    auto t = std::thread([]() -> void {
        if (backend != AudioBackend::UNSET)
            return;
        if (try_establishing_connection_with_pulseaudio()) {
            backend = AudioBackend::PULSEAUDIO;
            audio_thread_id = std::this_thread::get_id();
            while (audio_running) {
                iterate_pulseaudio_mainloop();
            }
            pa_context_disconnect(context);
            pa_context_unref(context);
            pa_mainloop_free(mainloop);
            context = nullptr;
            mainloop = nullptr;
            for (auto c: audio_clients)
                delete c;
            audio_clients.clear();
        } else if (try_establishing_connection_with_alsa()) {
            backend = AudioBackend::ALSA;
            audio_running = true;
            ready = true;
            audio_thread_id = std::this_thread::get_id();
        }
    });
    threads.push_back(std::move(t));
}

void audio_stop() {
    if (audio_change_callback)
        audio_change_callback();
    ready = false;
    audio_running = false;
    if (backend == AudioBackend::PULSEAUDIO) {
        pa_mainloop_wakeup(mainloop);
    } else if (backend == AudioBackend::ALSA) {
        if (alsa_handle) snd_mixer_close(alsa_handle);
        if (master_sid) snd_mixer_selem_id_free(master_sid);
        if (headphone_sid) snd_mixer_selem_id_free(headphone_sid);
        if (speaker_sid) snd_mixer_selem_id_free(speaker_sid);
        alsa_handle = nullptr;
        master_sid = nullptr;
        headphone_sid = nullptr;
        speaker_sid = nullptr;
        for (auto c: audio_clients)
            delete c;
        audio_clients.clear();
    }
    backend = AudioBackend::UNSET;
}

void audio_join() {
    for (int i = 0; i < threads.size(); ++i)
        threads[i].join();
    threads.clear();
}

void audio(const std::function<void()> &callback) {
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (!ready || !audio_running) return;
    
    if (backend == AudioBackend::ALSA) {
        callback();
        return;
    } else if (backend != AudioBackend::PULSEAUDIO) {
        return;
    }
    
    // We handle the case where this function is called from the audio thread which would obviously lock us up.
    if (std::this_thread::get_id() == audio_thread_id) {
        // re-call the function passed in but on another thread
        std::thread temp([callback]() {
            audio(callback);
        });
        temp.detach();
        return;
    }
    
    while (audio_thread_busy && audio_running); // If audio thread is doing something, wait for it to finish
    
    if (callback == nullptr) {
        pa_mainloop_wakeup(mainloop);
        return;
    }
    
    needs_block = true;
    signal_met = false;
    free_to_continue = false;
    // First we grab a lock on signal_mutex because after calling 'audio_wakeup', audio thread will try to lock signal_mutex
    // It won't be able to because we have the lock. That gives us time to set up the conditional which will unlock the lock,
    // and will wait for the audio thread to notify us that it has woken up and dispatched, and we are free to use the pa_mainloop.
    {
        std::unique_lock<std::mutex> lock(signal_mutex);
        pa_mainloop_wakeup(mainloop);
        needs_signal_condition.wait_for(lock, std::chrono::milliseconds(70), []() { return signal_met; });
        callback();
    }
    
    // At this point the audio thread is waiting for us to notify it that it is free to continue, so we try to gain a lock
    // Set the variable to true, and notify the conditional variable in the audio thread
    {
        std::unique_lock<std::mutex> free_to_continue_lock(free_to_continue_mutex);
        free_to_continue = true;
        free_to_continue_condition.notify_all();
    }
}

void audio_read(const std::function<void()> &callback) {
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (!ready || !audio_running || !callback) return;
    
    if (backend == AudioBackend::ALSA) {
        callback();
        return;
    } else if (backend != AudioBackend::PULSEAUDIO) {
        return;
    }
    
    while (audio_thread_busy && audio_running); // If audio thread is doing something, wait for it to finish
    
    std::unique_lock<std::mutex> read_lock(read_mutex);
    
    callback();
}

static bool opened = false;

void meter_watching_start() {
    if (!winbar_settings->meter_animations)
        return;
    opened = true;
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (!ready || !audio_running || backend != AudioBackend::PULSEAUDIO) return;
    for (const auto &audio_client: audio_clients) {
        if (!audio_client->stream) {
            pa_sample_spec spec;
            spec.channels = 1;
            spec.format = PA_SAMPLE_FLOAT32;
            spec.rate = 120;
            
            pa_buffer_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.fragsize = sizeof(float);
            attr.maxlength = (uint32_t) -1;
            
            audio_client->stream = pa_stream_new(context, (audio_client->title + " Peak").c_str(), &spec, NULL);
            
            auto flags = (pa_stream_flags_t) (10752); // exact number set by pulseaudio
            pa_stream_set_read_callback(audio_client->stream, on_stream_update, audio_client);
            pa_stream_set_suspended_callback(audio_client->stream, on_stream_suspend, audio_client);
            
            if (audio_client->monitor_source_name.empty() && audio_client->index != PA_INVALID_INDEX) {
                pa_stream_set_monitor_stream(audio_client->stream, audio_client->index);
                pa_stream_connect_record(audio_client->stream, NULL, &attr, flags);
            } else {
                pa_stream_connect_record(audio_client->stream, audio_client->monitor_source_name.c_str(), &attr, flags);
            }
        }
    }
}

void meter_watching_stop() {
    if (!winbar_settings->meter_animations)
        return;
    if (!opened)
        return;
    opened = false;
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (!ready || !audio_running || backend != AudioBackend::PULSEAUDIO) return;
    for (const auto &audio_client: audio_clients) {
        if (audio_client->stream) {
            pa_stream_disconnect(audio_client->stream);
            pa_stream_unref(audio_client->stream);
            audio_client->stream = nullptr;
        }
    }
}

void AudioClient::set_mute(bool mute_on) {
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (backend == AudioBackend::PULSEAUDIO) {
        pa_operation *pa_op;
        if (this->is_master) {
            pa_op = pa_context_set_sink_mute_by_index(context, this->index, (int) mute_on, NULL, NULL);
        } else {
            pa_op = pa_context_set_sink_input_mute(context, this->index, (int) mute_on, NULL, NULL);
        }
        pa_operation_unref(pa_op);
    } else if (backend == AudioBackend::ALSA) {
        if (mute_on) { // mute
            snd_mixer_selem_set_playback_switch_all(master_volume, 0);
            if (headphone_volume)
                snd_mixer_selem_set_playback_switch_all(headphone_volume, 0);
            if (speaker_volume)
                snd_mixer_selem_set_playback_switch_all(speaker_volume, 0);
        } else {
            snd_mixer_selem_set_playback_switch_all(master_volume, 1);
            if (headphone_volume)
                snd_mixer_selem_set_playback_switch_all(headphone_volume, 1);
            if (speaker_volume)
                snd_mixer_selem_set_playback_switch_all(speaker_volume, 1);
        }
        alsa_state_change_callback(master_volume, 0);
    }
}

void AudioClient::set_volume(double value) {
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    if (value > 1)
        value = 1;
    if (value < 0)
        value = 0;
    if (backend == AudioBackend::PULSEAUDIO) {
        pa_cvolume copy = this->volume;
        for (int i = 0; i < this->volume.channels; i++)
            copy.values[i] = std::round(65536 * value);
        
        pa_operation *pa_op;
        if (this->is_master) {
            pa_op = pa_context_set_sink_volume_by_index(context, index, &copy, NULL, NULL);
        } else {
            pa_op = pa_context_set_sink_input_volume(context, index, &copy, NULL, NULL);
        }
        pa_operation_unref(pa_op);
    } else if (backend == AudioBackend::ALSA) {
        this->alsa_volume = value;
        // will determine the bounds? or how the number is round? something like that. it doesn't matter too much from what I can tell
        auto dir = this->get_volume() == value ? 0 : this->get_volume() > value ? -1 : 1;
        set_normalized_playback_volume(master_volume, value, dir);
        alsa_state_change_callback(master_volume, 0);
    }
}

bool AudioClient::is_muted() {
    return mute;
}

double AudioClient::get_volume() {
    if (backend == AudioBackend::PULSEAUDIO) {
        return ((double) volume.values[0]) / ((double) 65535);
    } else if (backend == AudioBackend::ALSA) {
        return alsa_volume;
    }
}

bool AudioClient::is_master_volume() {
    return title == default_sink_name;
}

void audio_state_change_callback(void (*callback)()) {
    audio_change_callback = callback;
}
