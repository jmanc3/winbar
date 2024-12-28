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
    bool battery_label_always_on = false;
    bool volume_expands_on_hover = true;
    bool volume_label_always_on = false;
    bool show_agenda = true;
    bool thumbnails = true;
    bool battery_notifications = false;
    bool pinned_icon_shortcut = false;
    bool allow_live_tiles = true;
    std::string custom_desktops_directory;
    bool custom_desktops_directory_exclusive = false;
    bool ignore_only_show_in = true;
    bool meter_animations = true;
    bool labels = false;
    bool super_icon_default = true;
    bool label_uniform_size = false;
    bool minimize_maximize_animation = true;
    std::string shutdown_command;
    std::string restart_command;
    std::string pinned_icon_style = "win10";
};

extern WinbarSettings *winbar_settings;

void open_settings_menu(SettingsPage page);

void read_settings_file();

void save_settings_file();

void merge_order_with_taskbar();

#endif //WINBAR_SETTINGS_MENU_H
