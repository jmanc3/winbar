//
// Created by jmanc3 on 3/8/20.
//

#ifndef APP_VOLUME_MENU_H
#define APP_VOLUME_MENU_H

#include "taskbar.h"
#include "audio.h"
#include <cairo.h>

void open_volume_menu();

void
adjust_volume_based_on_fine_scroll(AudioClient *audio_client,
                                   AppClient *client,
                                   cairo_t *cr,
                                   Container *container,
                                   int horizontal_scroll,
                                   int vertical_scroll, bool came_from_touchpad);

void updates();


#endif// APP_VOLUME_MENU_H
