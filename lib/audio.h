//
// Created by jmanc3 on 9/30/21.
//

#ifndef WINBAR_AUDIO_H
#define WINBAR_AUDIO_H

#include "application.h"

#include <string>
#include <vector>
#include <pulse/volume.h>
#include <alsa/asoundlib.h>
#include <pulse/pulseaudio.h>

// The only reason we need [App] is so that we can poll file descriptors fired from audio servers
void audio_start(App *app);

// For updating UI when other programs change audio state that are not us.
void audio_state_change_callback(void (*callback)());

void audio_stop();

enum class Audio_Backend {
    NONE,
    PULSEAUDIO,
    ALSA
};

// Clients will be things that create sound.
// All doubles will be between 0-1.
class Audio_Client {
public:
    explicit Audio_Client(Audio_Backend backend);

    std::string title;
    std::string subtitle;

    double get_volume();

    void set_volume(double value); // Shouldn't be expected to un-mute

    bool is_muted();

    void set_mute(bool state);

    bool is_master_volume() const { return is_master; }

    int unique_id() const {
        if (type == Audio_Backend::PULSEAUDIO)
            return pulseaudio_index + (pulseaudio_index * (is_master * 100));
        return alsa_index + (pulseaudio_index * (is_master * 100));
    }

    /// Type
    Audio_Backend type = Audio_Backend::PULSEAUDIO;
    bool is_master = false;

    /// Data for use by PulseAudio
    int pulseaudio_index;
    int pulseaudio_mute_state;
    pa_cvolume pulseaudio_volume;

    /// Data for use by Alsa
    double alsa_volume = 0;
    bool alsa_mute_state = false;
    int alsa_index = -1;
};

extern std::vector<Audio_Client *> audio_clients;

class AudioBackendData {
public:
    Audio_Backend audio_backend = Audio_Backend::NONE;

    void (*callback)();

    snd_mixer_t *alsa_handle = nullptr;
    snd_mixer_elem_t *master_volume = nullptr;
    snd_mixer_selem_id_t *master_sid = nullptr;
    snd_mixer_elem_t *headphone_volume = nullptr;
    snd_mixer_selem_id_t *headphone_sid = nullptr;
    snd_mixer_elem_t *speaker_volume = nullptr;
    snd_mixer_selem_id_t *speaker_sid = nullptr;

    pa_threaded_mainloop *mainloop = nullptr;
    pa_mainloop_api *api = nullptr;
    pa_context *context = nullptr;

    bool shutting_down = false;

    ~AudioBackendData() {
        shutting_down = true;
        if (audio_backend == Audio_Backend::ALSA) {
            if (alsa_handle) snd_mixer_close(alsa_handle);
            if (master_sid) snd_mixer_selem_id_free(master_sid);
            if (headphone_sid) snd_mixer_selem_id_free(headphone_sid);
            if (speaker_sid) snd_mixer_selem_id_free(speaker_sid);
        } else if (audio_backend == Audio_Backend::PULSEAUDIO) {
            if (context) {
                pa_context_disconnect(context);
                pa_context_unref(context);
            }
            if (mainloop) {
                pa_threaded_mainloop_stop(mainloop);
                pa_threaded_mainloop_free(mainloop);
            }
        }
    }
};

extern AudioBackendData *audio_backend_data;

void audio_update_list_of_clients();

#endif //WINBAR_AUDIO_H
