
#include "config.h"
#include "utility.h"

#include <iostream>
#include <libconfig.h++>
#include <regex>
#include <sys/stat.h>

Config *config = new Config;

bool config_parse(libconfig::Config &cfg);

void load_hex(const libconfig::Setting &theme, std::string value_name, ArgbColor *target_color) {
    std::string temp;
    bool success = theme.lookupValue(value_name, temp);
    std::string name;
    theme.lookupValue("name", name);

    if (success) {
        success = parse_hex(temp, &target_color->a, &target_color->r, &target_color->g, &target_color->b);

        if (!success) {
            target_color->r = 1;
            target_color->g = 0;
            target_color->b = 1;
            target_color->a = 1;
            std::cout << "Hex string not a valid hex color in active theme: " << name << std::endl;
        }
    } else {
        std::cout << "Could not find hex color: " << value_name << " in active theme: " << name
                  << std::endl;
    }
}

void config_load() {
    bool success;

    libconfig::Config cfg;
    success = config_parse(cfg);
    if (!success)
        return;

    success = cfg.lookupValue("taskbar_height", config->taskbar_height);

    success = cfg.lookupValue("starting_tab_index", config->starting_tab_index);

    success = cfg.lookupValue("font", config->font);

    success = cfg.lookupValue("open_pinned_icon_editor", config->open_pinned_icon_editor);

    success = cfg.lookupValue("volume_command", config->volume_command);
    success = cfg.lookupValue("wifi_command", config->wifi_command);
    success = cfg.lookupValue("date_command", config->date_command);
    success = cfg.lookupValue("battery_command", config->battery_command);
    success = cfg.lookupValue("systray_command", config->systray_command);

    success = cfg.lookupValue("file_manager", config->file_manager);

    success = cfg.lookupValue("date_single_line", config->date_single_line);

    success = cfg.lookupValue("interface", config->interface);

    std::string active_theme_name;
    success = cfg.lookupValue("active_theme_name", active_theme_name);

    if (success) { // Then the user selected a theme
        const libconfig::Setting &root = cfg.getRoot();

        try {
            const libconfig::Setting &themes = root["themes"];

            for (int i = 0; themes.getLength(); i++) {
                const libconfig::Setting &theme = themes[i];

                std::string name;

                success = theme.lookupValue("name", name);

                if (success) {
                    if (name == active_theme_name) {
                        load_hex(theme, "color_taskbar_background", &config->color_taskbar_background);
                        load_hex(theme, "color_taskbar_button_icons", &config->color_taskbar_button_icons);
                        load_hex(theme, "color_taskbar_button_default", &config->color_taskbar_button_default);
                        load_hex(theme, "color_taskbar_button_hovered", &config->color_taskbar_button_hovered);
                        load_hex(theme, "color_taskbar_button_pressed", &config->color_taskbar_button_pressed);
                        load_hex(theme, "color_taskbar_windows_button_default_icon",
                                 &config->color_taskbar_windows_button_default_icon);
                        load_hex(theme, "color_taskbar_windows_button_hovered_icon",
                                 &config->color_taskbar_windows_button_hovered_icon);
                        load_hex(theme, "color_taskbar_windows_button_pressed_icon",
                                 &config->color_taskbar_windows_button_pressed_icon);
                        load_hex(theme, "color_taskbar_search_bar_default_background",
                                 &config->color_taskbar_search_bar_default_background);
                        load_hex(theme, "color_taskbar_search_bar_hovered_background",
                                 &config->color_taskbar_search_bar_hovered_background);
                        load_hex(theme, "color_taskbar_search_bar_pressed_background",
                                 &config->color_taskbar_search_bar_pressed_background);
                        load_hex(theme, "color_taskbar_search_bar_default_text",
                                 &config->color_taskbar_search_bar_default_text);
                        load_hex(theme, "color_taskbar_search_bar_hovered_text",
                                 &config->color_taskbar_search_bar_hovered_text);
                        load_hex(theme, "color_taskbar_search_bar_pressed_text",
                                 &config->color_taskbar_search_bar_pressed_text);
                        load_hex(theme, "color_taskbar_search_bar_default_icon",
                                 &config->color_taskbar_search_bar_default_icon);
                        load_hex(theme, "color_taskbar_search_bar_hovered_icon",
                                 &config->color_taskbar_search_bar_hovered_icon);
                        load_hex(theme, "color_taskbar_search_bar_pressed_icon",
                                 &config->color_taskbar_search_bar_pressed_icon);
                        load_hex(theme, "color_taskbar_search_bar_default_border",
                                 &config->color_taskbar_search_bar_default_border);
                        load_hex(theme, "color_taskbar_search_bar_hovered_border",
                                 &config->color_taskbar_search_bar_hovered_border);
                        load_hex(theme, "color_taskbar_search_bar_pressed_border",
                                 &config->color_taskbar_search_bar_pressed_border);
                        load_hex(theme, "color_taskbar_date_time_text", &config->color_taskbar_date_time_text);
                        load_hex(theme, "color_taskbar_application_icons_background",
                                 &config->color_taskbar_application_icons_background);
                        load_hex(theme, "color_taskbar_application_icons_accent",
                                 &config->color_taskbar_application_icons_accent);
                        load_hex(theme, "color_taskbar_minimize_line",
                                 &config->color_taskbar_minimize_line);

                        load_hex(theme, "color_systray_background", &config->color_systray_background);

                        load_hex(theme, "color_battery_background", &config->color_battery_background);
                        load_hex(theme, "color_battery_text", &config->color_battery_text);
                        load_hex(theme, "color_battery_icons", &config->color_battery_icons);
                        load_hex(theme, "color_battery_slider_background", &config->color_battery_slider_background);
                        load_hex(theme, "color_battery_slider_foreground", &config->color_battery_slider_foreground);
                        load_hex(theme, "color_battery_slider_active", &config->color_battery_slider_active);

                        load_hex(theme, "color_wifi_background", &config->color_wifi_background);
                        load_hex(theme, "color_wifi_icons", &config->color_wifi_icons);
                        load_hex(theme, "color_wifi_default_button", &config->color_wifi_default_button);
                        load_hex(theme, "color_wifi_hovered_button", &config->color_wifi_hovered_button);
                        load_hex(theme, "color_wifi_pressed_button", &config->color_wifi_pressed_button);
                        load_hex(theme, "color_wifi_text_title", &config->color_wifi_text_title);
                        load_hex(theme, "color_wifi_text_title_info", &config->color_wifi_text_title_info);
                        load_hex(theme, "color_wifi_text_settings_default_title",
                                 &config->color_wifi_text_settings_default_title);
                        load_hex(theme, "color_wifi_text_settings_hovered_title",
                                 &config->color_wifi_text_settings_hovered_title);
                        load_hex(theme, "color_wifi_text_settings_pressed_title",
                                 &config->color_wifi_text_settings_pressed_title);
                        load_hex(theme, "color_wifi_text_settings_title_info",
                                 &config->color_wifi_text_settings_title_info);

                        load_hex(theme, "color_date_background", &config->color_date_background);
                        load_hex(theme, "color_date_seperator", &config->color_date_seperator);
                        load_hex(theme, "color_date_text", &config->color_date_text);
                        load_hex(theme, "color_date_text_title", &config->color_date_text_title);
                        load_hex(theme, "color_date_text_title_period", &config->color_date_text_title_period);
                        load_hex(theme, "color_date_text_title_info", &config->color_date_text_title_info);
                        load_hex(theme, "color_date_text_month_year", &config->color_date_text_month_year);
                        load_hex(theme, "color_date_text_week_day", &config->color_date_text_week_day);
                        load_hex(theme, "color_date_text_current_month", &config->color_date_text_current_month);
                        load_hex(theme, "color_date_text_not_current_month",
                                 &config->color_date_text_not_current_month);
                        load_hex(theme, "color_date_cal_background", &config->color_date_cal_background);
                        load_hex(theme, "color_date_cal_foreground", &config->color_date_cal_foreground);
                        load_hex(theme, "color_date_cal_border", &config->color_date_cal_border);
                        load_hex(theme, "color_date_weekday_monthday", &config->color_date_weekday_monthday);
                        load_hex(theme, "color_date_default_arrow", &config->color_date_default_arrow);
                        load_hex(theme, "color_date_hovered_arrow", &config->color_date_hovered_arrow);
                        load_hex(theme, "color_date_pressed_arrow", &config->color_date_pressed_arrow);
                        load_hex(theme, "color_date_text_default_button", &config->color_date_text_default_button);
                        load_hex(theme, "color_date_text_hovered_button", &config->color_date_text_hovered_button);
                        load_hex(theme, "color_date_text_pressed_button", &config->color_date_text_pressed_button);
                        load_hex(theme, "color_date_cursor", &config->color_date_cursor);
                        load_hex(theme, "color_date_text_prompt", &config->color_date_text_prompt);

                        load_hex(theme, "color_volume_background", &config->color_volume_background);
                        load_hex(theme, "color_volume_text", &config->color_volume_text);
                        load_hex(theme, "color_volume_default_icon", &config->color_volume_default_icon);
                        load_hex(theme, "color_volume_hovered_icon", &config->color_volume_hovered_icon);
                        load_hex(theme, "color_volume_pressed_icon", &config->color_volume_pressed_icon);
                        load_hex(theme, "color_volume_slider_background", &config->color_volume_slider_background);
                        load_hex(theme, "color_volume_slider_foreground", &config->color_volume_slider_foreground);
                        load_hex(theme, "color_volume_slider_active", &config->color_volume_slider_active);

                        load_hex(theme, "color_apps_background", &config->color_apps_background);
                        load_hex(theme, "color_apps_text", &config->color_apps_text);
                        load_hex(theme, "color_apps_text_inactive", &config->color_apps_text_inactive);
                        load_hex(theme, "color_apps_icons", &config->color_apps_icons);
                        load_hex(theme, "color_apps_default_item", &config->color_apps_default_item);
                        load_hex(theme, "color_apps_hovered_item", &config->color_apps_hovered_item);
                        load_hex(theme, "color_apps_pressed_item", &config->color_apps_pressed_item);
                        load_hex(theme, "color_apps_item_icon_background", &config->color_apps_item_icon_background);
                        load_hex(theme, "color_apps_scrollbar_gutter", &config->color_apps_scrollbar_gutter);
                        load_hex(theme, "color_apps_scrollbar_default_thumb",
                                 &config->color_apps_scrollbar_default_thumb);
                        load_hex(theme, "color_apps_scrollbar_hovered_thumb",
                                 &config->color_apps_scrollbar_hovered_thumb);
                        load_hex(theme, "color_apps_scrollbar_pressed_thumb",
                                 &config->color_apps_scrollbar_pressed_thumb);
                        load_hex(theme, "color_apps_scrollbar_default_button",
                                 &config->color_apps_scrollbar_default_button);
                        load_hex(theme, "color_apps_scrollbar_hovered_button",
                                 &config->color_apps_scrollbar_hovered_button);
                        load_hex(theme, "color_apps_scrollbar_pressed_button",
                                 &config->color_apps_scrollbar_pressed_button);
                        load_hex(theme, "color_apps_scrollbar_default_button_icon",
                                 &config->color_apps_scrollbar_default_button_icon);
                        load_hex(theme, "color_apps_scrollbar_hovered_button_icon",
                                 &config->color_apps_scrollbar_hovered_button_icon);
                        load_hex(theme, "color_apps_scrollbar_pressed_button_icon",
                                 &config->color_apps_scrollbar_pressed_button_icon);

                        load_hex(theme, "color_pin_menu_background", &config->color_pin_menu_background);
                        load_hex(theme, "color_pin_menu_hovered_item", &config->color_pin_menu_hovered_item);
                        load_hex(theme, "color_pin_menu_pressed_item", &config->color_pin_menu_pressed_item);
                        load_hex(theme, "color_pin_menu_text", &config->color_pin_menu_text);
                        load_hex(theme, "color_pin_menu_icons", &config->color_pin_menu_icons);

                        load_hex(theme, "color_windows_selector_default_background",
                                 &config->color_windows_selector_default_background);
                        load_hex(theme, "color_windows_selector_hovered_background",
                                 &config->color_windows_selector_hovered_background);
                        load_hex(theme, "color_windows_selector_pressed_background",
                                 &config->color_windows_selector_pressed_background);
                        load_hex(theme, "color_windows_selector_close_icon",
                                 &config->color_windows_selector_close_icon);
                        load_hex(theme, "color_windows_selector_close_icon_hovered",
                                 &config->color_windows_selector_close_icon_hovered);
                        load_hex(theme, "color_windows_selector_close_icon_pressed",
                                 &config->color_windows_selector_close_icon_pressed);
                        load_hex(theme, "color_windows_selector_text", &config->color_windows_selector_text);
                        load_hex(theme, "color_windows_selector_close_icon_hovered_background",
                                 &config->color_windows_selector_close_icon_hovered_background);
                        load_hex(theme, "color_windows_selector_close_icon_pressed_background",
                                 &config->color_windows_selector_close_icon_pressed_background);

                        load_hex(theme, "color_search_tab_bar_background", &config->color_search_tab_bar_background);
                        load_hex(theme, "color_search_accent", &config->color_search_accent);
                        load_hex(theme, "color_search_tab_bar_default_text",
                                 &config->color_search_tab_bar_default_text);
                        load_hex(theme, "color_search_tab_bar_hovered_text",
                                 &config->color_search_tab_bar_hovered_text);
                        load_hex(theme, "color_search_tab_bar_pressed_text",
                                 &config->color_search_tab_bar_pressed_text);
                        load_hex(theme, "color_search_tab_bar_active_text", &config->color_search_tab_bar_active_text);
                        load_hex(theme, "color_search_empty_tab_content_background",
                                 &config->color_search_empty_tab_content_background);
                        load_hex(theme, "color_search_empty_tab_content_icon",
                                 &config->color_search_empty_tab_content_icon);
                        load_hex(theme, "color_search_empty_tab_content_text",
                                 &config->color_search_empty_tab_content_text);
                        load_hex(theme, "color_search_content_left_background",
                                 &config->color_search_content_left_background);
                        load_hex(theme, "color_search_content_right_background",
                                 &config->color_search_content_right_background);
                        load_hex(theme, "color_search_content_right_foreground",
                                 &config->color_search_content_right_foreground);
                        load_hex(theme, "color_search_content_right_splitter",
                                 &config->color_search_content_right_splitter);
                        load_hex(theme, "color_search_content_text_primary",
                                 &config->color_search_content_text_primary);
                        load_hex(theme, "color_search_content_text_secondary",
                                 &config->color_search_content_text_secondary);
                        load_hex(theme, "color_search_content_right_button_default",
                                 &config->color_search_content_right_button_default);
                        load_hex(theme, "color_search_content_right_button_hovered",
                                 &config->color_search_content_right_button_hovered);
                        load_hex(theme, "color_search_content_right_button_pressed",
                                 &config->color_search_content_right_button_pressed);
                        load_hex(theme, "color_search_content_left_button_splitter",
                                 &config->color_search_content_left_button_splitter);
                        load_hex(theme, "color_search_content_left_button_default",
                                 &config->color_search_content_left_button_default);
                        load_hex(theme, "color_search_content_left_button_hovered",
                                 &config->color_search_content_left_button_hovered);
                        load_hex(theme, "color_search_content_left_button_pressed",
                                 &config->color_search_content_left_button_pressed);
                        load_hex(theme, "color_search_content_left_button_active",
                                 &config->color_search_content_left_button_active);
                        load_hex(theme, "color_search_content_left_set_active_button_default",
                                 &config->color_search_content_left_set_active_button_default);
                        load_hex(theme, "color_search_content_left_set_active_button_hovered",
                                 &config->color_search_content_left_set_active_button_hovered);
                        load_hex(theme, "color_search_content_left_set_active_button_pressed",
                                 &config->color_search_content_left_set_active_button_pressed);
                        load_hex(theme, "color_search_content_left_set_active_button_active",
                                 &config->color_search_content_left_set_active_button_active);
                        load_hex(theme, "color_search_content_left_set_active_button_icon_default",
                                 &config->color_search_content_left_set_active_button_icon_default);
                        load_hex(theme, "color_search_content_left_set_active_button_icon_pressed",
                                 &config->color_search_content_left_set_active_button_icon_pressed);

                        load_hex(theme, "color_pinned_icon_editor_background",
                                 &config->color_pinned_icon_editor_background);
                        load_hex(theme, "color_pinned_icon_editor_field_default_text",
                                 &config->color_pinned_icon_editor_field_default_text);
                        load_hex(theme, "color_pinned_icon_editor_field_hovered_text",
                                 &config->color_pinned_icon_editor_field_hovered_text);
                        load_hex(theme, "color_pinned_icon_editor_field_pressed_text",
                                 &config->color_pinned_icon_editor_field_pressed_text);
                        load_hex(theme, "color_pinned_icon_editor_field_default_border",
                                 &config->color_pinned_icon_editor_field_default_border);
                        load_hex(theme, "color_pinned_icon_editor_field_hovered_border",
                                 &config->color_pinned_icon_editor_field_hovered_border);
                        load_hex(theme, "color_pinned_icon_editor_field_pressed_border",
                                 &config->color_pinned_icon_editor_field_pressed_border);
                        load_hex(theme, "color_pinned_icon_editor_cursor",
                                 &config->color_pinned_icon_editor_cursor);
                        load_hex(theme, "color_pinned_icon_editor_button_default",
                                 &config->color_pinned_icon_editor_button_default);
                        load_hex(theme, "color_pinned_icon_editor_button_text_default",
                                 &config->color_pinned_icon_editor_button_text_default);
                        break;
                    }
                } else {
                    printf("One of the themes you defined doesn't have a name\n");
                }
            }
        } catch (const libconfig::SettingNotFoundException &nfex) {
        }
    }
}

bool config_parse(libconfig::Config &cfg) {
    char *home = getenv("HOME");

    std::string config_directory(home);
    config_directory += "/.config/winbar";
    mkdir(config_directory.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    std::string config_file(config_directory + "/winbar.cfg");

    try {
        cfg.readFile(config_file.c_str());
    } catch (const libconfig::FileIOException &fioex) {
        std::cout << "IO error:  " << config_file << std::endl;
        return false;
    } catch (const libconfig::ParseException &pex) {

        std::cout << "Parsing error:  " << config_file << " Line: " << pex.getLine() << " Error: " << pex.getError()
                  << std::endl;
        return false;
    }

    return true;
}
