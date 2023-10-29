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
    std::string date_style = "windows 11 detailed";
    PangoAlignment date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
    int date_size = 9;
    bool battery_expands_on_hover = true;
    bool volume_expands_on_hover = true;
};

extern WinbarSettings *winbar_settings;

void open_settings_menu(SettingsPage page);

void read_settings_file();

void save_settings_file();

#endif //WINBAR_SETTINGS_MENU_H
