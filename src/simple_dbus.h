//
// Created by jmanc3 on 5/28/21.
//

#ifndef WINBAR_SIMPLE_DBUS_H
#define WINBAR_SIMPLE_DBUS_H

#include "notifications.h"

#include <string>
#include <vector>

struct DBusConnection;

extern DBusConnection *dbus_connection;

extern std::vector<std::string> running_dbus_services;

void dbus_start();

void dbus_end();

bool dbus_gnome_show_overview();

bool dbus_kde_show_desktop();

bool dbus_kde_show_desktop_grid();

void notification_closed_signal(App *app, NotificationInfo *ni, NotificationReasonClosed reason);

void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action);

double dbus_get_kde_max_brightness();

double dbus_get_kde_current_brightness();

/// Number from 0 to 1
bool dbus_kde_set_brightness(double percentage);

bool dbus_kde_running();

bool dbus_gnome_running();

double dbus_get_gnome_brightness();

/// Number from 0 to 100
bool dbus_set_gnome_brightness(double percentage);

void dbus_computer_shut_down();

void dbus_computer_restart();

#endif //WINBAR_SIMPLE_DBUS_H
