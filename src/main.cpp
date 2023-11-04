

#include <pango/pangocairo.h>
#include <cmath>
#include "main.h"
#include "app_menu.h"
#include "application.h"
#include "audio.h"
#include "root.h"
#include "systray.h"
#include "taskbar.h"
#include "config.h"
#include "globals.h"
#include "notifications.h"
#include "wifi_backend.h"
#include "simple_dbus.h"
#include "icons.h"
#include "dpi.h"
#include "volume_menu.h"

App *app;

bool restart = false;

void check_config_version();

void load_in_fonts();

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--create-cache") == 0) {
            printf("Creating icon cache...\n");
            generate_cache();
            return 0;
        }
    }
    
    global = new globals;
    
    // Open connection to app
    app = app_new();
    
    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }
    
    // Load the config
    config_load();
    
    // Set DPI if auto
    double total_time_waiting_for_primary_screen = 4000;
    int reattempt_time = 200;
    int amount_of_times = total_time_waiting_for_primary_screen / reattempt_time;
    
    dpi_setup(app);
    if (config->dpi_auto) {
        for (int oj = 0; oj < amount_of_times; oj++) {
            for (auto &i: screens) {
                auto *screen = (ScreenInformation *) i;
                if (screen->is_primary) {
                    config->dpi = screen->height_in_pixels / 1080.0;
                    config->dpi = std::round(config->dpi * 2) / 2;
                    if (config->dpi < 1)
                        config->dpi = 1;
                    goto out;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(reattempt_time));
            update_information_of_all_screens(app);
        }
    }
    out:
    
    config->taskbar_height = config->taskbar_height * config->dpi;
    
    check_config_version();
    
    load_in_fonts();
    
    active_tab = config->starting_tab_index == 0 ? "Apps" : "Scripts";
    
    set_icons_path_and_possibly_update(app);
    
    // Add listeners and grabs on the root window
    root_start(app);

    // Start the pulseaudio connection
    audio_state_change_callback(updates);
    
    // We need to register as the systray
    register_as_systray();
    
    load_scripts();// The scripts are reloaded every time the search_menu window closes
    
    // Open our windows
    AppClient *taskbar = create_taskbar(app);
    
    // We only want to load the desktop files once at the start of the program
    //std::thread(load_desktop_files).detach();
    load_all_desktop_files();
    load_historic_scripts();
    load_historic_apps();
    
    client_show(app, taskbar);
    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, taskbar->window, XCB_CURRENT_TIME);
    
    static int first = 0;
    if (first++ == 0) {
        dbus_start(DBUS_BUS_SESSION);
        dbus_start(DBUS_BUS_SYSTEM);
    }
    
    wifi_start(app);
    
    // Start our listening loop until the end of the program
    app_main(app);
    
    unload_icons();
    
    // Clean up
    app_clean(app);
    
    audio_stop();
    
    wifi_stop();
    
    for (auto l: launchers) {
        delete l;
    }
    launchers.clear();
    launchers.shrink_to_fit();
    
    delete global;
    
    if (restart) {
        restart = false;
        main(argc, argv);
    } else {
        dbus_end();
    }
    
    return 0;
}

static int acceptable_config_version = 8;

std::string first_message;
std::string second_message;
std::string third_message;

void paint_wrong_version(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 14 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    int width;
    int height;
    pango_layout_set_text(layout, first_message.data(), first_message.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  10 * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    
    layout = get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    int second_height;
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    pango_layout_set_text(layout, second_message.data(), second_message.size());
    pango_layout_get_pixel_size_safe(layout, &width, &second_height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10 * config->dpi,
                  (10 + height + 10) * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_text(layout, third_message.data(), third_message.size());
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - (20 * config->dpi)) * PANGO_SCALE);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10 * config->dpi,
                  (10 + height + 10 + second_height + 5) * config->dpi);
    pango_cairo_show_layout(cr, layout);
}

void check_config_version() {
    if (config->config_version == 0 ||
        config->config_version > acceptable_config_version ||
        config->config_version < acceptable_config_version ||
        !config->found_config) {
        Settings settings;
        settings.w = 400 * config->dpi;
        settings.h = 200 * config->dpi;
        auto client = client_new(app, settings, "winbar_version_check");
        client->root->when_paint = paint_wrong_version;
    
        first_message = "Couldn't start WinBar";
        char *home = getenv("HOME");
        std::string config_directory(home);
        config_directory += "/.config/winbar/winbar.cfg";
    
        if (!config->found_config) {
            second_message = "We didn't find a Winbar config at: " + config_directory;
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "read the README.md (in particular the section about missing icons), "
                            "and make sure you unzip \"winbar.zip\" into the correct place.";
        } else if (config->config_version == 0) {
            second_message = "We found a config file, but there was no version number in it, which means it is out of date.";
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "download the resources: \"winbar.zip\", "
                            "and unzip them into the correct place (as you've done once already) overriding the old files.";
        } else if (config->config_version > acceptable_config_version) {
            second_message = "The config version is \"";
            second_message += std::to_string(config->config_version);
            second_message += "\", which is too new compared to Winbar's acceptable config version \"";
            second_message += std::to_string(acceptable_config_version);
            second_message += "\".";
            third_message = "To fix this, pull the latest https://github.com/jmanc3/winbar, compile, and install it.";
        } else if (config->config_version < acceptable_config_version) {
            second_message = "The config version is \"";
            second_message += std::to_string(config->config_version);
            second_message += "\", which is too old compared to Winbar's acceptable config version \"";
            second_message += std::to_string(acceptable_config_version);
            second_message += "\".";
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "download the resources: \"winbar.zip\", "
                            "and unzip them into the correct place (as you have already done once) overriding the old files.";
        }
    
        client_layout(app, client);
        request_refresh(app, client);
        client_show(app, client);
        app_main(app);
        app_clean(app);
    }
}

#include <fontconfig/fontconfig.h>

void load_in_fonts() {
    char *home = getenv("HOME");
    std::string font_directory(home);
    font_directory += "/.config/winbar/fonts";
    
    FcInit();
    FcConfig *now = FcConfigGetCurrent();
    const FcChar8 *file = (const FcChar8 *) font_directory.c_str();
    FcBool fontAddStatus = FcConfigAppFontAddDir(now, file);
    FcConfigBuildFonts(now);
}