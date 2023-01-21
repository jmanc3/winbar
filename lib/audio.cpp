//
// Created by jmanc3 on 9/30/21.
//

#include <cmath>
#include "audio.h"
#include "application.h"
#include "volume_mapping.h"
#include "utility.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

AudioBackendData *audio_backend_data = new AudioBackendData;
std::vector<Audio_Client *> audio_clients;

double Audio_Client::get_volume() {
    if (this->type == Audio_Backend::PULSEAUDIO) {
        return ((double) this->pulseaudio_volume.values[0]) / ((double) 65535);
    } else if (this->type == Audio_Backend::ALSA) {
        return this->alsa_volume;
    }
    return 0;
}

void Audio_Client::set_volume(double value) {
    if (this->type == Audio_Backend::PULSEAUDIO) {
        pa_cvolume copy = this->pulseaudio_volume;
        for (int i = 0; i < this->pulseaudio_volume.channels; i++)
            copy.values[i] = std::round(65536 * value);
        
        pa_threaded_mainloop_lock(audio_backend_data->mainloop);
        pa_operation *pa_op;
        if (this->is_master) {
            pa_op = pa_context_set_sink_volume_by_index(audio_backend_data->context, this->pulseaudio_index, &copy,
                                                        NULL, NULL);
        } else {
            pa_op = pa_context_set_sink_input_volume(audio_backend_data->context, this->pulseaudio_index, &copy,
                                                     NULL, NULL);
        }
        assert(pa_op);
        pa_operation_unref(pa_op);
        pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    } else if (this->type == Audio_Backend::ALSA) {
        this->alsa_volume = value;
        // will determine the bounds? or how the number is round? something like that. it doesn't matter too much from what I can tell
        auto dir = this->get_volume() == value ? 0 : this->get_volume() > value ? -1 : 1;
        set_normalized_playback_volume(audio_backend_data->master_volume, value, dir);
    }
}

bool Audio_Client::is_muted() {
    if (this->type == Audio_Backend::PULSEAUDIO) {
        return this->pulseaudio_mute_state;
    } else if (this->type == Audio_Backend::ALSA) {
        return this->alsa_mute_state;
    }
    return false;
}

static int alsa_state_change_callback(snd_mixer_elem_t *elem, unsigned int mask);

void Audio_Client::set_mute(bool state) {
    if (this->type == Audio_Backend::PULSEAUDIO) {
        pa_threaded_mainloop_lock(audio_backend_data->mainloop);
        pa_operation *pa_op;
        if (this->is_master) {
            pa_op = pa_context_set_sink_mute_by_index(audio_backend_data->context, this->pulseaudio_index, (int) state,
                                                      NULL, NULL);
        } else {
            pa_op = pa_context_set_sink_input_mute(audio_backend_data->context, this->pulseaudio_index, (int) state,
                                                   NULL, NULL);
        }
        assert(pa_op);
        pa_operation_unref(pa_op);
        pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    } else if (this->type == Audio_Backend::ALSA) {
        if (state) { // mute
            snd_mixer_selem_set_playback_switch_all(audio_backend_data->master_volume, 0);
            if (audio_backend_data->headphone_volume)
                snd_mixer_selem_set_playback_switch_all(audio_backend_data->headphone_volume, 0);
            if (audio_backend_data->speaker_volume)
                snd_mixer_selem_set_playback_switch_all(audio_backend_data->speaker_volume, 0);
        } else {
            snd_mixer_selem_set_playback_switch_all(audio_backend_data->master_volume, 1);
            if (audio_backend_data->headphone_volume)
                snd_mixer_selem_set_playback_switch_all(audio_backend_data->headphone_volume, 1);
            if (audio_backend_data->speaker_volume)
                snd_mixer_selem_set_playback_switch_all(audio_backend_data->speaker_volume, 1);
        }
        alsa_state_change_callback(audio_backend_data->master_volume, 0);
    }
}

Audio_Client::Audio_Client(Audio_Backend backend) {
    type = backend;
}

void alsa_event_pumping_required_callback(App *app, int fd) {
    snd_mixer_handle_events(audio_backend_data->alsa_handle);
}

static int alsa_state_change_callback(snd_mixer_elem_t *elem, unsigned int mask) {
    if (!audio_backend_data->callback) return 0;
    if (audio_backend_data->master_volume != elem) return 0;
    if (audio_backend_data->shutting_down) return 0;
    
    double volume = get_normalized_playback_volume(audio_backend_data->master_volume, SND_MIXER_SCHN_FRONT_LEFT);
    int mute;
    snd_mixer_selem_get_playback_switch(elem, snd_mixer_selem_channel_id_t::SND_MIXER_SCHN_FRONT_LEFT, &mute);
    
    if (!audio_clients.empty()) {
        audio_clients[0]->alsa_volume = volume;
        audio_clients[0]->alsa_mute_state = !((bool) mute);
    }
    audio_backend_data->callback();
    return 0;
}

bool try_establishing_connection_with_alsa(App *app) {
    if (snd_mixer_open(&audio_backend_data->alsa_handle, 0) != 0)
        return false;
    
    const char *card = "default"; // can be replaced with hw:0
    if (snd_mixer_attach(audio_backend_data->alsa_handle, card) != 0) {
        snd_mixer_close(audio_backend_data->alsa_handle);
        return false;
    }
    if (snd_mixer_selem_register(audio_backend_data->alsa_handle, NULL, NULL) != 0) {
        snd_mixer_close(audio_backend_data->alsa_handle);
        return false;
    }
    if (snd_mixer_load(audio_backend_data->alsa_handle) != 0) {
        snd_mixer_close(audio_backend_data->alsa_handle);
        return false;
    }
    
    snd_mixer_selem_id_malloc(&audio_backend_data->master_sid);
    snd_mixer_selem_id_set_index(audio_backend_data->master_sid, 0);
    
    const char *selem_name = "Master";
    snd_mixer_selem_id_set_name(audio_backend_data->master_sid, selem_name);
    if ((audio_backend_data->master_volume = snd_mixer_find_selem(audio_backend_data->alsa_handle,
                                                                  audio_backend_data->master_sid)) == nullptr)
        return false;
    
    // The reason we can't just use Master is because when you mute the master, all the children get muted, but when you
    // un-mute, they don't get un-muted.
    { // Headphone (optional)
        snd_mixer_selem_id_malloc(&audio_backend_data->headphone_sid);
        snd_mixer_selem_id_set_index(audio_backend_data->headphone_sid, 0);
        
        const char *name = "Headphone";
        snd_mixer_selem_id_set_name(audio_backend_data->headphone_sid, name);
        if ((audio_backend_data->headphone_volume = snd_mixer_find_selem(audio_backend_data->alsa_handle,
                                                                         audio_backend_data->headphone_sid)) == nullptr)
            snd_mixer_selem_id_free(audio_backend_data->headphone_sid);
    }
    { // Speaker Front (optional)
        snd_mixer_selem_id_malloc(&audio_backend_data->speaker_sid);
        snd_mixer_selem_id_set_index(audio_backend_data->speaker_sid, 0);
        
        const char *name = "Speaker Front";
        snd_mixer_selem_id_set_name(audio_backend_data->speaker_sid, name);
        if ((audio_backend_data->speaker_volume = snd_mixer_find_selem(audio_backend_data->alsa_handle,
                                                                       audio_backend_data->speaker_sid)) == nullptr)
            snd_mixer_selem_id_free(audio_backend_data->speaker_sid);
    }
    
    // Watch for events and pump them when required
    int i, nfds = snd_mixer_poll_descriptors_count(audio_backend_data->alsa_handle);
    struct pollfd pfds[nfds];
    if (snd_mixer_poll_descriptors(audio_backend_data->alsa_handle, pfds, nfds) < 0)
        return false;
    for (i = 0; i < nfds; i++)
        poll_descriptor(app, pfds[i].fd, pfds[i].events, alsa_event_pumping_required_callback);
    
    snd_mixer_elem_set_callback(audio_backend_data->master_volume, alsa_state_change_callback);
    
    auto c = new Audio_Client(Audio_Backend::ALSA);
    audio_clients.push_back(c);
    c->is_master = true;
    c->title = "Master";
    c->alsa_index = snd_mixer_selem_get_index(audio_backend_data->master_volume);
    c->default_sink = "Master";
    audio_backend_data->default_sink_name = "Master";
    
    alsa_state_change_callback(audio_backend_data->master_volume, 0);
    
    return true;
}

static bool try_establishing_connection_with_pulseaudio(App *app);

// The only reason we pass [App] here is so that we can poll file descriptors fired from audio servers.
// Don't get discouraged if you are trying to use this file for your project.
// Only like six lines in this file use [App]. If you have an epoll at the heart of your project, it should be simple plugging that in here instead.
void audio_start(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (audio_backend_data->audio_backend != Audio_Backend::NONE)
        return;
    
    if (try_establishing_connection_with_pulseaudio(app)) {
        audio_backend_data->audio_backend = Audio_Backend::PULSEAUDIO;
    } else if (try_establishing_connection_with_alsa(app)) {
        audio_backend_data->audio_backend = Audio_Backend::ALSA;
    }
}

void audio_state_change_callback(void (*callback)()) {
    audio_backend_data->callback = callback;
}

void audio_stop() {
    for (auto c: audio_clients)
        delete c;
    audio_clients.clear();
    
    delete audio_backend_data;
    audio_backend_data = new AudioBackendData;
}

#include <algorithm>

void possibly_update_default_sink() {
    for (const auto &item: audio_clients) {
        if (item->title == audio_backend_data->default_sink_name) {
            item->default_sink = true;
        } else {
            item->default_sink = false;
        }
    }
}

void on_stream(pa_stream *stream, size_t length, void *data) {
#if TRACY_ENABLE
    ZoneScoped;
#endif
    
    Audio_Client *client = (Audio_Client *) data;
    
    // guard with client->pulseaudio_mutex
    std::lock_guard<std::mutex> lock(client->pulseaudio_mutex);
    
    // print out the peak sample
    const void *buffer = nullptr;
    if (pa_stream_peek(stream, &buffer, &length) < 0) {
        fprintf(stderr, "pa_stream_peek() failed: %s", pa_strerror(pa_context_errno(pa_stream_get_context(stream))));
    }
    if (length >= 0 || buffer) {
        pa_stream_drop(stream);
    }
    long current_time = get_current_time_in_ms();
    long last_update = current_time - client->last_update;
    if (last_update > 100) {
        last_update = 100;
    }
    client->time_between_last_sample = last_update;
    client->last_update = current_time;
    memcpy(client->buffer, buffer, length);
    client->length = length;
}

void on_output_list_response(pa_context *c, const pa_sink_info *l, int eol, void *userdata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(audio_backend_data->mainloop, 0);
        return;
    }
    if (l == nullptr)
        return;
    for (auto audio_client: audio_clients) {
        if (audio_client->pulseaudio_index == l->index) {
            audio_client->title = l->name;
            audio_client->subtitle = l->description;
            audio_client->pulseaudio_volume = l->volume;
            audio_client->pulseaudio_mute_state = l->mute;
            audio_client->rate = l->sample_spec.rate;
            possibly_update_default_sink();
            return;
        }
    }
    
    auto audio_client = new Audio_Client(Audio_Backend::PULSEAUDIO);
    audio_client->title = l->name;
    audio_client->subtitle = l->description;
    audio_client->pulseaudio_volume = l->volume;
    audio_client->pulseaudio_index = l->index;
    audio_client->pulseaudio_mute_state = l->mute;
    audio_client->monitor_source_name = l->monitor_source_name;
    audio_client->is_master = true;
    audio_client->rate = l->sample_spec.rate;
    
    audio_clients.push_back(audio_client);
    possibly_update_default_sink();
    
    pa_threaded_mainloop_signal(audio_backend_data->mainloop, 0);
}

void on_sink_input_info_list_response(pa_context *c,
                                      const pa_sink_input_info *l,
                                      int eol,
                                      void *userdata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(audio_backend_data->mainloop, 0);
        return;
    }
    if (l == nullptr)
        return;
    for (auto audio_client: audio_clients) {
        if (audio_client->pulseaudio_index == l->index && !audio_client->is_master) {
            audio_client->title = l->name;
            audio_client->pulseaudio_volume = l->volume;
            audio_client->pulseaudio_mute_state = l->mute;
            audio_client->rate = l->sample_spec.rate;
            possibly_update_default_sink();
            return;
        }
    }

    auto audio_client = new Audio_Client(Audio_Backend::PULSEAUDIO);
    audio_client->title = l->name;
    audio_client->pulseaudio_volume = l->volume;
    audio_client->pulseaudio_index = l->index;
    audio_client->pulseaudio_mute_state = l->mute;
    audio_client->rate = l->sample_spec.rate;
    
    if (l->proplist) {
        size_t nbytes;
        const void *data = nullptr;
        pa_proplist_get(l->proplist, PA_PROP_APPLICATION_NAME, &data, &nbytes);
        if (data) {
            audio_client->subtitle = std::string((const char *) data, nbytes);
        }
    }
    
    audio_clients.push_back(audio_client);
    possibly_update_default_sink();
}

void on_server_info(pa_context *c, const pa_server_info *i, void *userdata) {
    audio_backend_data->default_sink_name = i->default_sink_name;
    possibly_update_default_sink();
    // I can't remember why you have to do this signal, but I'm assuming it's some multi-threading thing
    // that test also we're done with the data
    pa_threaded_mainloop_signal(audio_backend_data->mainloop, 0);
}

void change_in_audio(App *, AppClient *, Timeout *, void *) {
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_operation *pa_op = pa_context_get_sink_input_info_list(
            audio_backend_data->context, on_sink_input_info_list_response, NULL);
    if (!pa_op) return;
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    pa_op = nullptr;
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_op = pa_context_get_sink_info_list(audio_backend_data->context, on_output_list_response, NULL);
    if (!pa_op) return;
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    pa_op = nullptr;
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_op = pa_context_get_server_info(audio_backend_data->context, on_server_info, NULL);
    if (!pa_op) return;
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    if (audio_backend_data->callback)
        audio_backend_data->callback();
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    app_timeout_create(((App *) userdata), nullptr, 0, change_in_audio, nullptr);
}

void audio_subscribe_to_changes(App *app) {
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_context_set_subscribe_callback(audio_backend_data->context, subscribe_cb, app);
    
    pa_operation *pa_op = pa_context_subscribe(
            audio_backend_data->context,
            (pa_subscription_mask_t) (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                      PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                      PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT | PA_SUBSCRIPTION_MASK_CLIENT |
                                      PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_CARD),
            nullptr,
            nullptr);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
}

void context_state_callback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
        
        case PA_CONTEXT_READY: {
            std::thread t([userdata]() -> void { audio_subscribe_to_changes((App *) userdata); });
            t.detach();
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
        default:
            return;
    }
}

static bool try_establishing_connection_with_pulseaudio(App *app) {
    if (!(audio_backend_data->mainloop = pa_threaded_mainloop_new())) {
        return false;
    }
    
    if (!(audio_backend_data->api = pa_threaded_mainloop_get_api(audio_backend_data->mainloop))) {
        return false;
    }
    
    pa_proplist *prop_list = pa_proplist_new();
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_NAME, ("Winbar Volume Control"));
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ID, "winbar.volume");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_VERSION, "1");
    
    if (!(audio_backend_data->context = pa_context_new_with_proplist(audio_backend_data->api, "Winbar Volume Control",
                                                                     prop_list))) {
        return false;
    }
    
    pa_proplist_free(prop_list);
    
    pa_context_set_state_callback(audio_backend_data->context, context_state_callback, app);
    
    char *server = nullptr;
    if (pa_context_connect(audio_backend_data->context, server, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        int error = pa_context_errno(audio_backend_data->context);
        return false;
    }
    
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    
    if (pa_threaded_mainloop_start(audio_backend_data->mainloop) < 0) {
        return false;
    }
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    while (true) {
        pa_context_state_t state;
        
        state = pa_context_get_state(audio_backend_data->context);
        
        if (state == PA_CONTEXT_READY)
            break;
        
        if (!PA_CONTEXT_IS_GOOD(state)) {
            int error = pa_context_errno(audio_backend_data->context);
            return false;
        }
        
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    }
    
    return true;
}

void audio_update_list_of_clients() {
    for (auto c: audio_clients)
        delete c;
    audio_clients.clear();
    audio_clients.shrink_to_fit();
    
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_operation *pa_op = pa_context_get_sink_info_list(audio_backend_data->context, on_output_list_response, NULL);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_op = pa_context_get_sink_input_info_list(audio_backend_data->context, on_sink_input_info_list_response, NULL);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    pa_op = nullptr;
    pa_threaded_mainloop_lock(audio_backend_data->mainloop);
    pa_op = pa_context_get_server_info(audio_backend_data->context, on_server_info, NULL);
    if (!pa_op) return;
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(audio_backend_data->mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(audio_backend_data->mainloop);
    
    // swap so that first client is the default sink
    for (int i = 1; i < audio_clients.size(); ++i) {
        Audio_Client *client = audio_clients[i];
        if (client->default_sink) {
            // swap with location at zero
            std::iter_swap(audio_clients.begin(), audio_clients.begin() + i);
        }
    }
}

void hook_up_stream() {
    for (const auto &audio_client: audio_clients) {
        if (!audio_client->stream) {
            pa_sample_spec spec;
            spec.channels = 1;
            spec.format = PA_SAMPLE_U8;
            spec.rate = audio_client->rate;
            
            audio_client->stream = pa_stream_new(audio_backend_data->context, audio_client->title.c_str(), &spec, NULL);
            pa_stream_set_read_callback(audio_client->stream, on_stream, audio_client);
            
            if (audio_client->monitor_source_name.empty() && audio_client->pulseaudio_index != PA_INVALID_INDEX) {
                pa_stream_set_monitor_stream(audio_client->stream, audio_client->pulseaudio_index);
                pa_stream_connect_record(audio_client->stream, NULL, NULL, PA_STREAM_NOFLAGS);
            } else {
                pa_stream_connect_record(audio_client->stream, audio_client->monitor_source_name.c_str(), NULL,
                                         PA_STREAM_NOFLAGS);
            }
        }
    }
}

void unhook_stream() {
    for (const auto &audio_client: audio_clients) {
        std::lock_guard<std::mutex> lock(audio_client->pulseaudio_mutex);
        if (audio_client->stream) {
            pa_stream_disconnect(audio_client->stream);
            pa_stream_unref(audio_client->stream);
            audio_client->stream = nullptr;
        }
    }
}