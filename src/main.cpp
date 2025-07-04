

#include <pango/pangocairo.h>
#include <cmath>
#include <sys/wait.h>
#include <csignal>

#include "main.h"
#include "app_menu.h"
#include "application.h"
#include "audio.h"
#include "root.h"
#include "systray.h"
#include "taskbar.h"
#include "windows_selector.h"
#include "config.h"
#include "globals.h"
#include "notifications.h"
#include "wifi_backend.h"
#include "simple_dbus.h"
#include "icons.h"
#include "dpi.h"
#include "volume_menu.h"
#include "settings_menu.h"

App *app;

bool restart = false;
bool force_close = false;

void load_in_fonts();

bool copy_resources_from_system_to_user();

int main_actual(int argc, char *argv[]);

static long last_crash_time = 0;
int first = 0;
static int crash_under_20_count = 0;

int main(int argc, char* argv[]) {
    main_actual(argc, argv);
    return 0;
    
    /*
#ifndef defined(NDEBUG) || defined(TRACY_ENABLE)
    main_actual(argc, argv);
    return 0;
#endif
*/
    bool keep_going = true;
    while (keep_going && !force_close) {
        first = 0;
        pid_t pid = fork();
        
        if (pid == -1) {
            std::cerr << "[Parent] Failed to fork process.\n";
            return 1;
        }
        
        if (pid == 0) {
            // CHILD process
            try {
                main_actual(argc, argv);
                return 0; // Normal exit
            } catch (const std::exception &e) {
                std::cerr << "[Child] Caught exception: " << e.what() << "\n";
                return 1; // Abnormal exit
            } catch (...) {
                std::cerr << "[Child] Caught unknown exception.\n";
                return 1;
            }
        } else {
            // PARENT process
            int status;
            waitpid(pid, &status, 0);
            
            if (WIFEXITED(status)) {
                int exitStatus = WEXITSTATUS(status);
                std::cout << "[Parent] Child exited with code " << exitStatus << "\n";
                if (exitStatus == 0) {
                    std::cout << "[Parent] Child exited normally. Not restarting.\n";
                    break;
                }
            } else if (WIFSIGNALED(status)) {
                int signal = WTERMSIG(status);
                std::cout << "[Parent] Child terminated by signal: " << signal << "\n";
            }
            
            std::cout << "[Parent] Restarting child after failure...\n";
            auto current = get_current_time_in_ms();
            if (current - last_crash_time < 40000) {
                crash_under_20_count++;
                if (crash_under_20_count >= 3) {
                    keep_going = false;
                    std::cout << "[Parent] Quitting (crashed too many times)...\n";
                }
            } else {
                crash_under_20_count = 0;
            }
            last_crash_time = current;
            sleep(1); // Optional: wait before restarting
        }
    }
    
    return 0;
}

int main_actual(int argc, char *argv[]) {
    restart = false;
    force_close = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--create-cache") == 0) {
            printf("Creating icon cache...\n");
            generate_cache();
            return 0;
        }
    }
    
    // Open connection to app
    app = app_new();
    
    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }
    
    global = new globals;
    
    if (!copy_resources_from_system_to_user()) {
        printf("Exiting. Couldn't copy winbar resources from system into $HOME/.config\n");
        return -1;
    }
    
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
    
    read_settings_file();
    if (!winbar_settings->auto_dpi) {
        config->dpi = ((float) winbar_settings->scale_amount) / 100.0f;
    }
 
    config->taskbar_height = winbar_settings->taskbar_height * config->dpi;
    if (!winbar_settings->user_font.empty()) {
        config->font = winbar_settings->user_font;
    }
    
    load_in_fonts();
    
    set_icons_path_and_possibly_update(app);
    
    // Add listeners and grabs on the root window
    root_start(app);

    // Start the pulseaudio connection
    audio_state_change_callback(updates);
    
    // We need to register as the systray
    register_as_systray();
    
    load_scripts(true);// The scripts are reloaded every time the search_menu window closes
    
    // Open our windows
    AppClient *taskbar = create_taskbar(app);
    
    // We only want to load the desktop files once at the start of the program
    //std::thread(load_desktop_files).detach();
    load_all_desktop_files();
    load_historic_scripts();
    load_historic_apps();
    load_live_tiles();
    
    if (!winbar_settings->always_hide) {
      client_show(app, taskbar);
    }

    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, taskbar->window, XCB_CURRENT_TIME);
    
    if (first++ == 0) {
        dbus_start(DBUS_BUS_SESSION);
        dbus_start(DBUS_BUS_SYSTEM);
    }
    
    wifi_start(app);
    
    app_timeout_create(app, taskbar, 1000 * 10, [](App *, AppClient *, Timeout *t, void *) {
        wifi_start(app);
        for (auto l: wifi_data->links) {
            wifi_scan(l);
        }
    }, nullptr, "Initial timeout to cache wifi networks after 10 seconds");
    
    // Start our listening loop until the end of the program
    app_main(app);
    
    free_slept();
    
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

static int acceptable_config_version = 9;

std::string first_message;
std::string second_message;
std::string third_message;

void paint_wrong_version(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_volume_background), container->real_bounds); 
    
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

#include <fontconfig/fontconfig.h>
#include <filesystem>

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

bool copy_resources_from_system_to_user() {
    const char *home = getenv("HOME");
    std::string itemPath(home);
    itemPath += "/.config/";
    if (mkdir(itemPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemPath.c_str());
            return false;
        }
    }
    itemPath += "/winbar/";
    if (mkdir(itemPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemPath.c_str());
            return false;
        }
    }
    const auto copyOptions = std::filesystem::copy_options::overwrite_existing
                             | std::filesystem::copy_options::recursive;
    try {
        std::filesystem::copy("/usr/share/winbar/fonts", itemPath + "fonts", copyOptions);
    } catch (...) {
        return false;
    }
    try {
        std::filesystem::copy("/usr/share/winbar/resources", itemPath + "resources", copyOptions);
    } catch (...) {
        return false;
    }
    try {
        std::filesystem::copy("/usr/share/winbar/plugins", itemPath + "plugins", copyOptions);
    } catch (...) {
        return false;
    }
    try {
        std::filesystem::copy("/usr/share/winbar/tofix.csv", itemPath + "tofix.csv", copyOptions);
    } catch (...) {
        return false;
    }
    try {
        std::filesystem::copy("/usr/share/winbar/winbar.cfg", itemPath + "winbar.cfg", copyOptions);
    } catch (...) {
    
    }
    try {
        std::filesystem::copy("/etc/winbar.cfg", itemPath + "winbar.cfg", copyOptions);
    } catch (...) {
    
    }
    try {
        if (!std::filesystem::exists(itemPath + "items_custom.ini"))
            std::filesystem::copy("/usr/share/winbar/items_custom.ini", itemPath + "items_custom.ini", copyOptions);
    } catch (...) {
        return false;
    }
    
    
    return true;
}
