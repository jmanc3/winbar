//
// Created by jmanc3 on 5/30/21.
//

#ifndef WINBAR_DBUS_H
#define WINBAR_DBUS_H

#include <vector>
#include <string>

extern std::vector<std::string> dbus_services; // up to date list of the names of running services

struct App;

void start_watching_dbus_services(App *app);

void stop_watching_dbus_services(App *app);

void dbus_kde_show_desktop_grid(App *app);

void dbus_gnome_show_overview(App *app);

#endif //WINBAR_DBUS_H
