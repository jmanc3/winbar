//
// Created by jmanc3 on 3/31/20.
//

#ifndef APP_SYSTRAY_H
#define APP_SYSTRAY_H

#include <vector>
#include <xcb/xproto.h>

void register_as_systray();

void open_systray();

void systray_dbus_start();

void systray_dbus_stop();

#endif// APP_SYSTRAY_H
