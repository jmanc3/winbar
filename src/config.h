#ifndef CONFIG_HEADER
#define CONFIG_HEADER

#include "utility.h"
#include <string>
#include <vector>

struct Config {
    int taskbar_height = 40;
    int icon_spacing = 1;
    int starting_tab_index = 0;

    std::string resource_path = "~/.config/winbar/resources";
    std::string font = "segoe ui";
    std::string volume_command;
    std::string wifi_command;
    std::string date_command;
    std::string battery_command;
    std::string systray_command;
    std::string file_manager = "thunar";

    std::string interface = "wlp7s0";

    std::vector<std::string> order;

    bool date_single_line = false;

    ArgbColor main_bg = {0, 0, 0, 1};
    ArgbColor main_accent = {.2, .5, .8, 1};

    ArgbColor icons_colors = {1, 1, 1, 1};

    ArgbColor button_default = {1, 1, 1, 0};
    ArgbColor button_hovered = {1, 1, 1, .15};
    ArgbColor button_pressed = {1, 1, 1, .2};

    double taskbar_transparency = .85;
    ArgbColor icon_default = {1, 1, 1, 0};          // nothing
    ArgbColor icon_background_back = {1, 1, 1, .1}; //
    ArgbColor icon_background_front = {1, 1, 1, .15};
    ArgbColor icon_pressed = {1, 1, 1, .2};
    double icon_bar_height = 2;

    ArgbColor textfield_default = {.9, .9, .9, 1};
    ArgbColor textfield_hovered = {1, 1, 1, 1};
    ArgbColor textfield_pressed = {1, 1, 1, 1};

    ArgbColor textfield_default_font = {.15, .15, .15, 1};
    ArgbColor textfield_hovered_font = {.15, .15, .15, 1};
    ArgbColor textfield_pressed_font = {0, 0, 0, 1};

    ArgbColor textfield_default_icon = {0, 0, 0, 1};
    ArgbColor textfield_hovered_icon = {0, 0, 0, 1};
    ArgbColor textfield_pressed_icon = {0, 0, 0, 1};

    double sound_menu_transparency = .8;
    ArgbColor sound_bg = {1, 1, 1, .15};
    ArgbColor sound_font = {1, 1, 1, 1};
    ArgbColor sound_default_icon = {.9, .9, .9, 1};
    ArgbColor sound_hovered_icon = {.95, .95, .95, 1};
    ArgbColor sound_pressed_icon = {1, 1, 1, 1};
    ArgbColor sound_line_background_default = button_hovered;
    ArgbColor sound_line_background_active = main_accent;
    ArgbColor sound_line_marker_default = main_accent;
    ArgbColor sound_line_marker_hovered = {1, 1, 1, 1};
    ArgbColor sound_line_marker_pressed = {1, 1, 1, 1};

    ArgbColor search_menu_color_bg = {.165, .165, .165, 1};

    ArgbColor search_menu_color_bg_query_left = {.165, .165, .165, 1};
    ArgbColor search_menu_color_bg_query_right_bg = {.165, .165, .165, 1};
    ArgbColor search_menu_color_bg_query_right_active = {.165, .165, .165, 1};

    ArgbColor search_menu_color_header = {.122, .122, .122, 1};

    ArgbColor search_menu_color_header_font_default = {.9, .9, .9, 1};
    ArgbColor search_menu_color_header_font_hovered = {.95, .95, .95, 1};
    ArgbColor search_menu_color_header_font_pressed = {1, 1, 1, 1};

    ArgbColor textfield_border_default = {.72, .72, .72, 1};
    ArgbColor textfield_border_highlight = main_accent;

    ArgbColor show_desktop_stripe = {.4, .4, .4, 1};

    ArgbColor calendar_font_default = {1, 1, 1, 1};
};

extern Config *config;

void
config_load();

#endif