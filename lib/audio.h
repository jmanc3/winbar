//
// Created by jmanc3 on 3/9/20.
//

#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <pulse/pulseaudio.h>
#include <string>
#include <vector>

struct Audio {
    uint32_t index;
    int mute_state;
    pa_cvolume volume;
};

struct AudioClient : Audio {
    std::string client_name;
    std::string application_name;
};

struct AudioOutput : Audio {
    std::string output_name;
    std::string output_description;
};

// These two vectors will get filled when calling audio_all_clients and
// audio_all_outputs
extern bool audio_connected;
extern std::vector<AudioClient *> audio_clients;
extern std::vector<AudioOutput *> audio_outputs;

void
audio_start();

void
audio_stop();

void
audio_all_clients();

void
audio_set_client_volume(unsigned int client_index, pa_cvolume volume);

void
audio_set_client_mute(unsigned int client_index, bool state);

void
audio_all_outputs();

void
audio_set_active_output(std::string output_name);

void
audio_set_output_mute(unsigned int output_index, bool state);

void
audio_set_output_volume(unsigned int client_index, pa_cvolume volume);

void
audio_set_callback(void (*callback)());

#endif // APP_AUDIO_H
