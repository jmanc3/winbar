//
// Created by jmanc3 on 10/18/23.
//

#ifndef WINBAR_SETTINGS_MENU_H
#define WINBAR_SETTINGS_MENU_H

#include <vector>
#include "container.h"

enum SettingsPage {
    Taskbar
};

struct TaskbarItem {
    std::string name;
    int target_index = 1000;
    bool on = true;
};

struct WinbarSettings {
    std::vector<TaskbarItem> taskbar_order;
    bool bluetooth_enabled = true;
    container_alignment icons_alignment = container_alignment::ALIGN_LEFT;
    std::string search_behaviour = "Default";
    std::string date_style = "windows 11 detailed";
    PangoAlignment date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
    int date_size = 9;
    bool battery_expands_on_hover = true;
    bool volume_expands_on_hover = true;
    bool show_agenda = true;
    bool thumbnails = true;
    bool battery_notifications = false;
    bool pinned_icon_shortcut = false;
    std::string custom_desktops_directory;
    bool custom_desktops_directory_exclusive = false;
    bool ignore_only_show_in = true;
    bool meter_animations = true;
};

extern WinbarSettings *winbar_settings;

void open_settings_menu(SettingsPage page);

void read_settings_file();

void save_settings_file();

void merge_order_with_taskbar();

#endif //WINBAR_SETTINGS_MENU_H
