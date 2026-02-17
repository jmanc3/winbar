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

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/keys.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/utils/dict.h>

// Global stuff
bool audio_running = false;
bool allow_audio_thread_creation = true;

static App *app;

enum AudioBackend {
    UNSET,
    PULSEAUDIO,
    PIPEWIRE,
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

// PipeWire stuff
struct PipeWireBoundNode {
    uint32_t id = SPA_ID_INVALID;
    bool is_master = false;
    pw_node *node = nullptr;
    spa_hook listener = {};
    AudioClient *client = nullptr;
};

static pw_thread_loop *pw_thread_loop_instance = nullptr;
static pw_context *pw_context_instance = nullptr;
static pw_core *pw_core_instance = nullptr;
static pw_registry *pw_registry_instance = nullptr;
static pw_metadata *pw_metadata_instance = nullptr;
static spa_hook pw_core_listener = {};
static spa_hook pw_registry_listener = {};
static spa_hook pw_metadata_listener = {};
static std::vector<PipeWireBoundNode *> pw_bound_nodes;
static int pw_sync_seq = 1;
static bool pw_initial_sync_done = false;

void sort();

static void delete_all_audio_clients() {
    for (auto c: audio_clients)
        delete c;
    audio_clients.clear();
}

static bool pipewire_class_is_master(const char *media_class) {
    return media_class && strcmp(media_class, "Audio/Sink") == 0;
}

static bool pipewire_class_is_stream(const char *media_class) {
    if (!media_class)
        return false;
    return strcmp(media_class, "Stream/Output/Audio") == 0 ||
           strcmp(media_class, "Audio/Stream") == 0 ||
           strcmp(media_class, "Audio/Source") == 0;
}

static bool is_default_sink_client(const AudioClient *client) {
    if (!client || !client->is_master)
        return false;

    if (backend == AudioBackend::PIPEWIRE) {
        if (default_sink_name.empty())
            return false;
        if (!client->pw_node_name.empty() && client->pw_node_name == default_sink_name)
            return true;
        return client->title == default_sink_name;
    }

    return client->title == default_sink_name;
}

static std::string parse_pipewire_default_name(const char *value) {
    if (!value)
        return "";

    std::string s = value;
    auto name_pos = s.find("\"name\"");
    if (name_pos == std::string::npos)
        return "";
    auto colon = s.find(':', name_pos);
    if (colon == std::string::npos)
        return "";
    auto quote_start = s.find('"', colon);
    if (quote_start == std::string::npos)
        return "";
    quote_start++;
    auto quote_end = s.find('"', quote_start);
    if (quote_end == std::string::npos || quote_end <= quote_start)
        return "";
    return s.substr(quote_start, quote_end - quote_start);
}

static AudioClient *find_or_create_pipewire_client(uint32_t id, bool is_master) {
    for (auto *c: audio_clients) {
        if (c->pw_node_id == id && c->is_master == is_master)
            return c;
    }

    auto *client = new AudioClient();
    client->index = static_cast<int>(id);
    client->pw_node_id = id;
    client->is_master = is_master;
    if (is_master) {
        client->icon_name = "audio-card";
    }
    audio_clients.push_back(client);
    return client;
}

static PipeWireBoundNode *find_pipewire_bound_node(uint32_t id) {
    for (auto *bound: pw_bound_nodes) {
        if (bound->id == id)
            return bound;
    }
    return nullptr;
}

static void update_pipewire_client_volumes(AudioClient *client, const float *volumes, uint32_t n_values) {
    if (!client)
        return;

    client->pw_channel_volumes.clear();
    if (volumes && n_values > 0) {
        client->pw_channel_volumes.assign(volumes, volumes + n_values);
    } else {
        client->pw_channel_volumes.push_back(1.0f);
    }

    client->volume.channels = static_cast<uint8_t>(std::min<size_t>(client->pw_channel_volumes.size(), PA_CHANNELS_MAX));
    if (client->volume.channels == 0)
        client->volume.channels = 1;

    for (uint8_t i = 0; i < client->volume.channels; ++i) {
        float linear = client->pw_channel_volumes[i];
        if (linear < 0.0f)
            linear = 0.0f;
        if (linear > 1.0f)
            linear = 1.0f;
        client->volume.values[i] = static_cast<pa_volume_t>(std::round(linear * 65535.0f));
    }
}

static void cleanup_pipewire_state() {
    if (pw_thread_loop_instance)
        pw_thread_loop_lock(pw_thread_loop_instance);

    for (auto *bound: pw_bound_nodes) {
        spa_hook_remove(&bound->listener);
        if (bound->node) {
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(bound->node));
            bound->node = nullptr;
        }
        delete bound;
    }
    pw_bound_nodes.clear();

    if (pw_metadata_instance) {
        spa_hook_remove(&pw_metadata_listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(pw_metadata_instance));
        pw_metadata_instance = nullptr;
    }
    if (pw_registry_instance) {
        spa_hook_remove(&pw_registry_listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(pw_registry_instance));
        pw_registry_instance = nullptr;
    }
    if (pw_core_instance) {
        spa_hook_remove(&pw_core_listener);
        pw_core_disconnect(pw_core_instance);
        pw_core_instance = nullptr;
    }
    if (pw_context_instance) {
        pw_context_destroy(pw_context_instance);
        pw_context_instance = nullptr;
    }
    if (pw_thread_loop_instance)
        pw_thread_loop_unlock(pw_thread_loop_instance);
    if (pw_thread_loop_instance) {
        pw_thread_loop_stop(pw_thread_loop_instance);
        pw_thread_loop_destroy(pw_thread_loop_instance);
        pw_thread_loop_instance = nullptr;
    }

    pw_initial_sync_done = false;
    pw_sync_seq = 1;
}

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
            if (is_default_sink_client(a)) {
                return true;
            } else if (is_default_sink_client(b)) {
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

static void pipewire_update_client_identity(AudioClient *client, const spa_dict *props) {
    if (!client || !props)
        return;

    const char *node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char *node_description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char *media_name = spa_dict_lookup(props, PW_KEY_MEDIA_NAME);
    const char *app_name = spa_dict_lookup(props, PW_KEY_APP_NAME);
    const char *icon_name = spa_dict_lookup(props, PW_KEY_APP_ICON_NAME);

    if (node_name && *node_name)
        client->pw_node_name = node_name;

    if (client->is_master) {
        if (node_name && *node_name)
            client->title = node_name;
        if (node_description && *node_description)
            client->subtitle = node_description;
        if (client->title.empty())
            client->title = "Master";
        client->icon_name = "audio-card";
    } else {
        if (media_name && *media_name) {
            client->title = media_name;
        } else if (node_name && *node_name) {
            client->title = node_name;
        }
        if (app_name && *app_name)
            client->subtitle = app_name;

        if (icon_name && has_options(icon_name)) {
            client->icon_name = icon_name;
        } else if (!client->subtitle.empty() && has_options(client->subtitle)) {
            client->icon_name = client->subtitle;
        } else if (!client->title.empty() && has_options(client->title)) {
            client->icon_name = client->title;
        } else if (!icon_name) {
            std::string lowered = !client->subtitle.empty() ? client->subtitle : client->title;
            for (auto &ch: lowered)
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            if (has_options(lowered))
                client->icon_name = lowered;
        }
    }
}

static void pipewire_handle_node_param(AudioClient *client, uint32_t id, const spa_pod *param) {
    if (!client || !param || id != SPA_PARAM_Props)
        return;

    bool mute = client->mute;
    float single_volume = client->get_volume();
    float channel_volumes[SPA_AUDIO_MAX_CHANNELS] = {};
    uint32_t channel_value_size = 0;
    uint32_t channel_value_type = SPA_ID_INVALID;
    uint32_t n_channel_volumes = 0;
    void *channel_values = nullptr;

    int parsed = spa_pod_parse_object(param,
                                      SPA_TYPE_OBJECT_Props, nullptr,
                                      SPA_PROP_mute, SPA_POD_OPT_Bool(&mute),
                                      SPA_PROP_channelVolumes, SPA_POD_OPT_Array(&channel_value_size,
                                                                                  &channel_value_type,
                                                                                  &n_channel_volumes,
                                                                                  &channel_values),
                                      SPA_PROP_volume, SPA_POD_OPT_Float(&single_volume));
    if (parsed < 0)
        return;

    client->mute = mute;
    if (channel_values && channel_value_size == sizeof(float) && channel_value_type == SPA_TYPE_Float &&
        n_channel_volumes > 0) {
        uint32_t clamped = std::min(n_channel_volumes, static_cast<uint32_t>(SPA_AUDIO_MAX_CHANNELS));
        memcpy(channel_volumes, channel_values, clamped * sizeof(float));
        n_channel_volumes = clamped;
        update_pipewire_client_volumes(client, channel_volumes, n_channel_volumes);
    } else {
        update_pipewire_client_volumes(client, &single_volume, 1);
    }
}

static void pipewire_node_info_callback(void *data, const pw_node_info *info) {
    auto *bound = static_cast<PipeWireBoundNode *>(data);
    if (!bound || !info)
        return;

    auto *client = bound->client;
    if (!client)
        return;

    if (info->props) {
        pipewire_update_client_identity(client, info->props);
        const char *node_name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
        if (client->is_master && node_name && *node_name && default_sink_name.empty()) {
            default_sink_name = node_name;
        }
    }

    client->index = static_cast<int>(bound->id);
    client->pw_node_id = bound->id;
    client->pw_node_ref = bound->node;
    client->is_master = bound->is_master;

    uint32_t ids[] = {SPA_PARAM_Props};
    pw_node_subscribe_params(bound->node, ids, 1);
    pw_node_enum_params(bound->node, 0, SPA_PARAM_Props, 0, UINT32_MAX, nullptr);

    sort();
    if (audio_change_callback)
        audio_change_callback();
}

static void pipewire_node_param_callback(void *data, int, uint32_t id, uint32_t, uint32_t, const spa_pod *param) {
    auto *bound = static_cast<PipeWireBoundNode *>(data);
    if (!bound || !bound->client)
        return;
    pipewire_handle_node_param(bound->client, id, param);
    sort();
    if (audio_change_callback)
        audio_change_callback();
}

static const pw_node_events pipewire_node_events = {
        PW_VERSION_NODE_EVENTS,
        pipewire_node_info_callback,
        pipewire_node_param_callback,
};

static int pipewire_metadata_property_callback(void *, uint32_t, const char *key, const char *, const char *value) {
    if (!key)
        return 0;
    if (strcmp(key, "default.audio.sink") != 0 && strcmp(key, "default.configured.audio.sink") != 0)
        return 0;

    std::string new_default = parse_pipewire_default_name(value);
    if (!new_default.empty()) {
        default_sink_name = new_default;
        sort();
        if (audio_change_callback)
            audio_change_callback();
    }
    return 0;
}

static const pw_metadata_events pipewire_metadata_events = {
        PW_VERSION_METADATA_EVENTS,
        pipewire_metadata_property_callback,
};

static void pipewire_registry_global_callback(void *, uint32_t id, uint32_t, const char *type, uint32_t version,
                                              const spa_dict *props) {
    if (!type)
        return;

    if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        if (pw_metadata_instance)
            return;

        pw_metadata_instance = static_cast<pw_metadata *>(pw_registry_bind(
                pw_registry_instance, id, PW_TYPE_INTERFACE_Metadata,
                std::min(version, static_cast<uint32_t>(PW_VERSION_METADATA)), 0));
        if (!pw_metadata_instance)
            return;
        pw_metadata_add_listener(pw_metadata_instance, &pw_metadata_listener, &pipewire_metadata_events, nullptr);
        return;
    }

    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props)
        return;

    const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const bool is_master = pipewire_class_is_master(media_class);
    const bool is_stream = pipewire_class_is_stream(media_class);
    if (!is_master && !is_stream)
        return;

    if (find_pipewire_bound_node(id))
        return;

    auto *bound = new PipeWireBoundNode();
    bound->id = id;
    bound->is_master = is_master;
    bound->node = static_cast<pw_node *>(pw_registry_bind(
            pw_registry_instance, id, PW_TYPE_INTERFACE_Node,
            std::min(version, static_cast<uint32_t>(PW_VERSION_NODE)), 0));
    if (!bound->node) {
        delete bound;
        return;
    }

    bound->client = find_or_create_pipewire_client(id, is_master);
    bound->client->pw_node_ref = bound->node;
    bound->client->pw_node_id = id;
    bound->client->is_master = is_master;
    pipewire_update_client_identity(bound->client, props);
    pw_node_add_listener(bound->node, &bound->listener, &pipewire_node_events, bound);

    pw_bound_nodes.push_back(bound);
    sort();
    if (audio_change_callback)
        audio_change_callback();
}

static void pipewire_registry_global_remove_callback(void *, uint32_t id) {
    for (size_t i = 0; i < pw_bound_nodes.size(); ++i) {
        auto *bound = pw_bound_nodes[i];
        if (bound->id != id)
            continue;

        for (size_t c = 0; c < audio_clients.size(); ++c) {
            if (audio_clients[c] == bound->client) {
                delete audio_clients[c];
                audio_clients.erase(audio_clients.begin() + static_cast<long>(c));
                break;
            }
        }

        spa_hook_remove(&bound->listener);
        if (bound->node)
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(bound->node));
        delete bound;
        pw_bound_nodes.erase(pw_bound_nodes.begin() + static_cast<long>(i));
        sort();
        if (audio_change_callback)
            audio_change_callback();
        return;
    }
}

static const pw_registry_events pipewire_registry_events = {
        PW_VERSION_REGISTRY_EVENTS,
        pipewire_registry_global_callback,
        pipewire_registry_global_remove_callback,
};

static void pipewire_core_done_callback(void *, uint32_t id, int seq) {
    if (id == PW_ID_CORE && seq == pw_sync_seq) {
        pw_initial_sync_done = true;
        pw_thread_loop_signal(pw_thread_loop_instance, false);
    }
}

static void pipewire_core_error_callback(void *, uint32_t, int, int, const char *) {
    ready = false;
    audio_running = false;
    pw_thread_loop_signal(pw_thread_loop_instance, false);
}

static const pw_core_events pipewire_core_events = {
        PW_VERSION_CORE_EVENTS,
        nullptr,
        pipewire_core_done_callback,
        nullptr,
        pipewire_core_error_callback,
};

static bool try_establishing_connection_with_pipewire() {
    pw_init(nullptr, nullptr);

    pw_thread_loop_instance = pw_thread_loop_new("winbar-audio-pipewire", nullptr);
    if (!pw_thread_loop_instance)
        return false;

    if (pw_thread_loop_start(pw_thread_loop_instance) != 0) {
        pw_thread_loop_destroy(pw_thread_loop_instance);
        pw_thread_loop_instance = nullptr;
        return false;
    }

    pw_thread_loop_lock(pw_thread_loop_instance);

    pw_context_instance = pw_context_new(pw_thread_loop_get_loop(pw_thread_loop_instance), nullptr, 0);
    if (!pw_context_instance) {
        pw_thread_loop_unlock(pw_thread_loop_instance);
        cleanup_pipewire_state();
        return false;
    }

    pw_core_instance = pw_context_connect(pw_context_instance, nullptr, 0);
    if (!pw_core_instance) {
        pw_thread_loop_unlock(pw_thread_loop_instance);
        cleanup_pipewire_state();
        return false;
    }

    pw_core_add_listener(pw_core_instance, &pw_core_listener, &pipewire_core_events, nullptr);

    pw_registry_instance = pw_core_get_registry(pw_core_instance, PW_VERSION_REGISTRY, 0);
    if (!pw_registry_instance) {
        pw_thread_loop_unlock(pw_thread_loop_instance);
        cleanup_pipewire_state();
        return false;
    }

    pw_registry_add_listener(pw_registry_instance, &pw_registry_listener, &pipewire_registry_events, nullptr);

    pw_initial_sync_done = false;
    pw_sync_seq = pw_core_sync(pw_core_instance, PW_ID_CORE, pw_sync_seq);

    audio_running = true;
    int wait_rounds = 0;
    while (audio_running && !pw_initial_sync_done && wait_rounds < 8) {
        pw_thread_loop_timed_wait(pw_thread_loop_instance, 1);
        wait_rounds++;
    }

    ready = audio_running && pw_initial_sync_done;
    pw_thread_loop_unlock(pw_thread_loop_instance);
    if (!ready)
        cleanup_pipewire_state();
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
        if (winbar_settings->prefer_pipewire_audio_backend) {
            if (try_establishing_connection_with_pipewire()) {
                backend = AudioBackend::PIPEWIRE;
                audio_thread_id = std::this_thread::get_id();
                while (audio_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                cleanup_pipewire_state();
                delete_all_audio_clients();
            } else if (try_establishing_connection_with_pulseaudio()) {
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
                delete_all_audio_clients();
            } else if (try_establishing_connection_with_alsa()) {
                backend = AudioBackend::ALSA;
                audio_running = true;
                ready = true;
                audio_thread_id = std::this_thread::get_id();
            }
        } else {
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
                delete_all_audio_clients();
            } else if (try_establishing_connection_with_pipewire()) {
                backend = AudioBackend::PIPEWIRE;
                audio_thread_id = std::this_thread::get_id();
                while (audio_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                cleanup_pipewire_state();
                delete_all_audio_clients();
            } else if (try_establishing_connection_with_alsa()) {
                backend = AudioBackend::ALSA;
                audio_running = true;
                ready = true;
                audio_thread_id = std::this_thread::get_id();
            }
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (pw_thread_loop_instance)
            pw_thread_loop_signal(pw_thread_loop_instance, false);
    } else if (backend == AudioBackend::ALSA) {
        if (alsa_handle) snd_mixer_close(alsa_handle);
        if (master_sid) snd_mixer_selem_id_free(master_sid);
        if (headphone_sid) snd_mixer_selem_id_free(headphone_sid);
        if (speaker_sid) snd_mixer_selem_id_free(speaker_sid);
        alsa_handle = nullptr;
        master_sid = nullptr;
        headphone_sid = nullptr;
        speaker_sid = nullptr;
        delete_all_audio_clients();
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (callback == nullptr)
            return;
        if (!pw_thread_loop_instance)
            return;
        pw_thread_loop_lock(pw_thread_loop_instance);
        callback();
        pw_thread_loop_unlock(pw_thread_loop_instance);
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (!pw_thread_loop_instance)
            return;
        pw_thread_loop_lock(pw_thread_loop_instance);
        callback();
        pw_thread_loop_unlock(pw_thread_loop_instance);
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (!pw_node_ref)
            return;
        pw_thread_loop_lock(pw_thread_loop_instance);
        this->mute = mute_on;

        float channel_volumes[SPA_AUDIO_MAX_CHANNELS];
        uint32_t n_channels = static_cast<uint32_t>(std::min<size_t>(pw_channel_volumes.size(), SPA_AUDIO_MAX_CHANNELS));
        if (n_channels == 0) {
            n_channels = 1;
            channel_volumes[0] = static_cast<float>(this->get_volume());
        } else {
            for (uint32_t i = 0; i < n_channels; ++i)
                channel_volumes[i] = pw_channel_volumes[i];
        }

        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *pod = reinterpret_cast<const spa_pod *>(spa_pod_builder_add_object(
                &b,
                SPA_TYPE_OBJECT_Props,
                SPA_PARAM_Props,
                SPA_PROP_mute, SPA_POD_Bool(mute_on),
                SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, n_channels, channel_volumes)
        ));
        pw_node_set_param(pw_node_ref, SPA_PARAM_Props, 0, pod);
        pw_thread_loop_unlock(pw_thread_loop_instance);
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (!pw_node_ref)
            return;
        pw_thread_loop_lock(pw_thread_loop_instance);

        float new_volume = static_cast<float>(value);
        float channel_volumes[SPA_AUDIO_MAX_CHANNELS];
        uint32_t n_channels = static_cast<uint32_t>(std::min<size_t>(pw_channel_volumes.size(), SPA_AUDIO_MAX_CHANNELS));
        if (n_channels == 0) {
            n_channels = 1;
        }
        for (uint32_t i = 0; i < n_channels; ++i)
            channel_volumes[i] = new_volume;

        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *pod = reinterpret_cast<const spa_pod *>(spa_pod_builder_add_object(
                &b,
                SPA_TYPE_OBJECT_Props,
                SPA_PARAM_Props,
                SPA_PROP_mute, SPA_POD_Bool(mute),
                SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, n_channels, channel_volumes)
        ));

        pw_node_set_param(pw_node_ref, SPA_PARAM_Props, 0, pod);
        update_pipewire_client_volumes(this, channel_volumes, n_channels);
        pw_thread_loop_unlock(pw_thread_loop_instance);
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
    } else if (backend == AudioBackend::PIPEWIRE) {
        if (!pw_channel_volumes.empty())
            return pw_channel_volumes[0];
        return ((double) volume.values[0]) / ((double) 65535);
    } else if (backend == AudioBackend::ALSA) {
        return alsa_volume;
    }
    return 0;
}

bool AudioClient::is_master_volume() {
    return is_default_sink_client(this);
}

void audio_state_change_callback(void (*callback)()) {
    audio_change_callback = callback;
}
