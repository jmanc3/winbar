
#include "config.h"

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
        std::regex pattern("([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})");

        std::smatch match;
        if (std::regex_match(temp, match, pattern)) {
            float a = std::stoul(match[1].str(), nullptr, 16);
            float r = std::stoul(match[2].str(), nullptr, 16);
            float g = std::stoul(match[3].str(), nullptr, 16);
            float b = std::stoul(match[4].str(), nullptr, 16);

            target_color->r = r / 255;
            target_color->g = g / 255;
            target_color->b = b / 255;
            target_color->a = a / 255;
        } else {
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
    config->order.clear();
    config->order.push_back("windows");
    config->order.push_back("textfield");
    config->order.push_back("workspace");
    config->order.push_back("icons-fill");
    config->order.push_back("action-bar");

    bool success = false;

    libconfig::Config cfg;
    success = config_parse(cfg);
    if (!success)
        return;

    success = cfg.lookupValue("taskbar_height", config->taskbar_height);

    success = cfg.lookupValue("resource_path", config->resource_path);
    success = cfg.lookupValue("volume_command", config->volume_command);
    success = cfg.lookupValue("wifi_command", config->wifi_command);
    success = cfg.lookupValue("date_command", config->date_command);
    success = cfg.lookupValue("battery_command", config->battery_command);
    success = cfg.lookupValue("systray_command", config->systray_command);
    success = cfg.lookupValue("icon_spacing", config->icon_spacing);
    success = cfg.lookupValue("font", config->font);

    success = cfg.lookupValue("date_single_line", config->date_single_line);

    try {
        libconfig::Setting &order = cfg.lookup("order");
        config->order.clear();

        for (int i = 0; order.getLength(); i++) {
            config->order.push_back(order[i]);
        }
    } catch (const libconfig::SettingNotFoundException &nfex) {
    }

    std::string active_theme;
    success = cfg.lookupValue("active_theme", active_theme);

    if (success) {
        const libconfig::Setting &root = cfg.getRoot();

        try {
            const libconfig::Setting &themes = root["themes"];

            for (int i = 0; themes.getLength(); i++) {
                const libconfig::Setting &theme = themes[i];

                std::string name;

                success = theme.lookupValue("name", name);

                if (success) {
                    if (name == active_theme) {
                        load_hex(theme, "main_bg", &config->main_bg);
                        load_hex(theme, "main_accent", &config->main_accent);
                        load_hex(theme, "icons_color", &config->icons_colors);

                        load_hex(theme, "button_default", &config->button_default);
                        load_hex(theme, "button_hovered", &config->button_hovered);
                        load_hex(theme, "button_pressed", &config->button_pressed);

                        load_hex(theme, "icon_default", &config->icon_default);
                        load_hex(theme, "icon_background_back", &config->icon_background_back);
                        load_hex(theme, "icon_background_front", &config->icon_background_front);
                        load_hex(theme, "icon_pressed", &config->icon_pressed);

                        load_hex(theme, "textfield_default", &config->textfield_default);
                        load_hex(theme, "textfield_hovered", &config->textfield_hovered);
                        load_hex(theme, "textfield_pressed", &config->textfield_pressed);

                        load_hex(theme, "textfield_default_font", &config->textfield_default_font);
                        load_hex(theme, "textfield_hovered_font", &config->textfield_hovered_font);
                        load_hex(theme, "textfield_pressed_font", &config->textfield_pressed_font);

                        load_hex(theme, "textfield_default_icon", &config->textfield_default_icon);
                        load_hex(theme, "textfield_hovered_icon", &config->textfield_hovered_icon);
                        load_hex(theme, "textfield_pressed_icon", &config->textfield_pressed_icon);

                        load_hex(theme, "sound_default_icon", &config->sound_default_icon);
                        load_hex(theme, "sound_hovered_icon", &config->sound_hovered_icon);
                        load_hex(theme, "sound_pressed_icon", &config->sound_pressed_icon);

                        load_hex(theme, "sound_bg", &config->sound_bg);
                        load_hex(theme, "sound_font", &config->sound_font);

                        load_hex(theme,
                                 "sound_line_background_default",
                                 &config->sound_line_background_default);
                        load_hex(theme,
                                 "sound_line_background_active",
                                 &config->sound_line_background_active);
                        load_hex(
                                theme, "sound_line_marker_default", &config->sound_line_marker_default);
                        load_hex(
                                theme, "sound_line_marker_hovered", &config->sound_line_marker_hovered);
                        load_hex(
                                theme, "sound_line_marker_pressed", &config->sound_line_marker_pressed);

                        load_hex(
                                theme, "textfield_border_default", &config->textfield_border_default);
                        load_hex(
                                theme, "textfield_border_highlight", &config->textfield_border_highlight);

                        load_hex(theme, "show_desktop_stripe", &config->show_desktop_stripe);

                        load_hex(theme, "calendar_font_default", &config->calendar_font_default);

                        success = theme.lookupValue("sound_menu_transparency",
                                                    config->sound_menu_transparency);
                        if (!success) {
                            std::cout
                                    << "sound_menu_transparency not set in active_theme: " << active_theme
                                    << std::endl;
                        }
                        success =
                                theme.lookupValue("taskbar_transparency", config->taskbar_transparency);
                        if (!success) {
                            std::cout
                                    << "taskbar_transparency not set in active_theme: " << active_theme
                                    << std::endl;
                        }
                        success = theme.lookupValue("icon_bar_height", config->icon_bar_height);
                        if (!success) {
                            std::cout << "icon_bar_height not set in active_theme: " << active_theme
                                      << std::endl;
                        }
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
        std::cout << "Parsing error:  " << config_file << std::endl;
        return false;
    }

    return true;
}
