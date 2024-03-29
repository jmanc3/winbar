//
// Created by jmanc3 on 10/1/21.
//

#ifndef WINBAR_VOLUME_MAPPING_H
#define WINBAR_VOLUME_MAPPING_H


#include <alsa/asoundlib.h>

double get_normalized_playback_volume(snd_mixer_elem_t *elem,
                                      snd_mixer_selem_channel_id_t channel);

double get_normalized_capture_volume(snd_mixer_elem_t *elem,
                                     snd_mixer_selem_channel_id_t channel);

int set_normalized_playback_volume(snd_mixer_elem_t *elem,
                                   double volume,
                                   int dir);

int set_normalized_capture_volume(snd_mixer_elem_t *elem,
                                  double volume,
                                  int dir);


#endif //WINBAR_VOLUME_MAPPING_H
