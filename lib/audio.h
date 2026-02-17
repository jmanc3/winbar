//
// Created by jmanc3 on 9/30/21.
//

#ifndef WINBAR_AUDIO_H
#define WINBAR_AUDIO_H

#include "application.h"

#include <pulse/mainloop.h>
#include <thread>
#include <pulse/pulseaudio.h>
#include <vector>
#include <functional>
#include <pipewire/node.h>

extern bool audio_running;
extern bool allow_audio_thread_creation;

struct AudioClient {
    int index = PA_INVALID_INDEX;
    uint32_t pw_node_id = SPA_ID_INVALID;
    pw_node *pw_node_ref = nullptr;
    std::string pw_node_name;
    std::string monitor_source_name;
    std::string title;
    std::string subtitle;
    std::string icon_name;
    bool is_master = false;
    pa_cvolume volume = {0};
    bool mute = 0;
    double peak = 0;
    pa_stream *stream = nullptr; // for showing uv meter
    std::vector<float> pw_channel_volumes;

    long last_time_peak_changed = 0;
    double cached_volume = 1;
    double alsa_volume = 0;
    
    void set_mute(bool mute_on);
    
    void set_volume(double value);
    bool is_muted();
    
    double get_volume();
    
    bool is_master_volume();
};

extern std::vector<AudioClient *> audio_clients;

struct App;

void audio_start(App *app);

void audio_stop();

void audio_sort();

void audio_join();

void audio(const std::function<void()> &callback);

void audio_read(const std::function<void()> &callback);

void meter_watching_start();

void meter_watching_stop();

// For updating UI when other programs change audio state that are not us.
void audio_state_change_callback(void (*callback)());


#endif //WINBAR_AUDIO_H
