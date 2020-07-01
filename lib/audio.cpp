//
// Created by jmanc3 on 3/9/20.
//

#include "audio.h"
#include <iostream>
#include <string.h>
#include <thread>

std::vector<AudioClient *> audio_clients;
std::vector<AudioOutput *> audio_outputs;

bool audio_connected = false;
static pa_threaded_mainloop *mainloop;
static pa_mainloop_api *api;
static pa_context *context;

void (*audio_callback_function)() = nullptr;

void on_output_list_response(pa_context *c, const pa_sink_info *l, int eol, void *userdata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(mainloop, 0);
        return;
    }
    if (l == nullptr)
        return;
    for (auto audio_client : audio_outputs) {
        if (audio_client->index == l->index) {
            audio_client->output_name = l->name;
            audio_client->output_description = l->description;
            audio_client->volume = l->volume;
            audio_client->index = l->index;
            audio_client->mute_state = l->mute;
            return;
        }
    }

    auto audio_client = new AudioOutput();
    audio_client->output_name = l->name;
    audio_client->output_description = l->description;
    audio_client->volume = l->volume;
    audio_client->index = l->index;
    audio_client->mute_state = l->mute;

    audio_outputs.push_back(audio_client);

    pa_threaded_mainloop_signal(mainloop, 0);
}

void on_sink_input_info_list_response(pa_context *c,
                                      const pa_sink_input_info *l,
                                      int eol,
                                      void *userdata) {
    if (eol != 0) {
        pa_threaded_mainloop_signal(mainloop, 0);
        return;
    }
    if (l == nullptr)
        return;
    for (auto audio_client : audio_clients) {
        if (audio_client->index == l->index) {
            audio_client->client_name = l->name;
            audio_client->volume = l->volume;
            audio_client->index = l->index;
            audio_client->mute_state = l->mute;
            return;
        }
    }

    auto audio_client = new AudioClient();
    audio_client->client_name = l->name;
    audio_client->volume = l->volume;
    audio_client->index = l->index;
    audio_client->mute_state = l->mute;

    if (pa_context_get_server_protocol_version(c) >= 13) {
    }

    if (l->proplist) {
        size_t nbytes;
        const void *data = nullptr;
        pa_proplist_get(l->proplist, PA_PROP_APPLICATION_NAME, &data, &nbytes);
        if (data) {
            auto result = strndup(reinterpret_cast<const char *>(data), nbytes);
            audio_client->application_name = result;
        }
    }

    audio_clients.push_back(audio_client);
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata);

void audio_subscribe_to_changes() {
    if (!audio_connected)
        return;

    pa_threaded_mainloop_lock(mainloop);
    pa_context_set_subscribe_callback(context, subscribe_cb, nullptr);

    pa_operation *pa_op = pa_context_subscribe(
            context,
            (pa_subscription_mask_t) (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                                      PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                      PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT | PA_SUBSCRIPTION_MASK_CLIENT |
                                      PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_CARD),
            nullptr,
            nullptr);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void context_state_callback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY: {
            std::thread t([]() -> void { audio_subscribe_to_changes(); });
            t.detach();
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
        default:
            return;
    }
}

void audio_start() {
    mainloop = pa_threaded_mainloop_new();
    api = pa_threaded_mainloop_get_api(mainloop);
    pa_threaded_mainloop_start(mainloop);

    pa_proplist *prop_list = pa_proplist_new();
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_NAME, ("Winbar Volume Control"));
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ID, "winbar.volume");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(prop_list, PA_PROP_APPLICATION_VERSION, "1");

    context = pa_context_new_with_proplist(api, "Winbar Volume Control", prop_list);

    pa_proplist_free(prop_list);

    pa_context_connect(context, NULL, PA_CONTEXT_NOFAIL, NULL);
    pa_context_set_state_callback(context, context_state_callback, NULL);

    while (pa_context_get_state(context) != PA_CONTEXT_READY &&
           pa_context_get_state(context) != PA_CONTEXT_FAILED &&
           pa_context_get_state(context) != PA_CONTEXT_TERMINATED) {
    }

    audio_connected = pa_context_get_state(context) == PA_CONTEXT_READY;
}

void audio_stop() {
    audio_connected = false;
    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
}

void audio_all_clients() {
    if (!audio_connected)
        return;
    audio_clients.clear();

    pa_threaded_mainloop_lock(mainloop);
    pa_operation *pa_op =
            pa_context_get_sink_input_info_list(context, on_sink_input_info_list_response, NULL);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void audio_set_client_volume(unsigned int client_index, pa_cvolume volume) {
    if (!audio_connected)
        return;
    pa_threaded_mainloop_lock(mainloop);
    pa_operation *pa_op =
            pa_context_set_sink_input_volume(context, client_index, &volume, NULL, NULL);
    assert(pa_op);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void audio_set_client_mute(unsigned int client_index, bool state) {
    if (!audio_connected)
        return;
    pa_threaded_mainloop_lock(mainloop);
    pa_operation *pa_op =
            pa_context_set_sink_input_mute(context, client_index, (int) state, NULL, NULL);
    assert(pa_op);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void audio_all_outputs() {
    if (!audio_connected)
        return;
    audio_outputs.clear();

    pa_threaded_mainloop_lock(mainloop);
    pa_operation *pa_op = pa_context_get_sink_info_list(context, on_output_list_response, NULL);
    assert(pa_op);
    while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
        pa_threaded_mainloop_wait(mainloop);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void audio_set_active_output(std::string output_name) {
    if (!audio_connected)
        return;
}

void audio_set_output_volume(unsigned int client_index, pa_cvolume volume) {
    if (!audio_connected)
        return;
    pa_threaded_mainloop_lock(mainloop);
    pa_operation *pa_op =
            pa_context_set_sink_volume_by_index(context, client_index, &volume, NULL, NULL);
    assert(pa_op);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void audio_set_output_mute(unsigned int output_index, bool state) {
    if (!audio_connected)
        return;

    pa_threaded_mainloop_lock(mainloop);

    pa_operation *pa_op =
            pa_context_set_sink_mute_by_index(context, output_index, (int) state, NULL, NULL);
    assert(pa_op);
    pa_operation_unref(pa_op);
    pa_threaded_mainloop_unlock(mainloop);
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    // This is called from a different thread so we need to be on the real main
    // thread
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK: {
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            } else {
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_SOURCE: {
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            } else {
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_SINK_INPUT: {
            std::thread t([]() -> void {
                pa_threaded_mainloop_lock(mainloop);

                pa_operation *pa_op = pa_context_get_sink_input_info_list(
                        context, on_sink_input_info_list_response, NULL);
                assert(pa_op);
                while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
                    pa_threaded_mainloop_wait(mainloop);
                pa_operation_unref(pa_op);

                pa_op = pa_context_get_sink_info_list(context, on_output_list_response, NULL);
                assert(pa_op);
                while (pa_operation_get_state(pa_op) == PA_OPERATION_RUNNING)
                    pa_threaded_mainloop_wait(mainloop);
                pa_operation_unref(pa_op);

                pa_threaded_mainloop_unlock(mainloop);
            });
            t.detach();

            break;
        }
        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT: {
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            } else {
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_CLIENT: {
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            } else {
            }
            break;
        }
        case PA_SUBSCRIPTION_EVENT_SERVER: {
            break;
        }
        case PA_SUBSCRIPTION_EVENT_CARD: {
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
            } else {
            }
            break;
        }
    }
    if (audio_callback_function) {
        audio_callback_function();
    }
}

void audio_set_callback(void (*callback)()) {
    audio_callback_function = callback;
}
