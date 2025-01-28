
#include "wifi_menu.h"
#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "utility.h"
#include "wifi_backend.h"
#include "settings_menu.h"
#include "components.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <pango/pangocairo.h>
#include <cmath>

struct RootScanAnimationData : public UserData {
    long start = get_current_time_in_ms();
    bool running = false;
};

struct WifiOptionData : public UserData {
    ScanResult info;
    bool clicked = false;
};

static double WIFI_OPTION_HEIGHT = 65;

static int calculateSignalLevel(int rssi, int numLevels) {
    int MIN_RSSI = -100;
    int MAX_RSSI = -55;
    if (rssi <= MIN_RSSI) {
        return 0;
    } else if (rssi >= MAX_RSSI) {
        return numLevels - 1;
    } else {
        float inputRange = (MAX_RSSI - MIN_RSSI);
        float outputRange = (numLevels - 1);
        return (int) ((float) (rssi - MIN_RSSI) * outputRange / inputRange);
    }
}

InterfaceLink *get_active_link() {
    if (auto client = client_by_name(app, "wifi_menu")) {
        if (auto container = container_by_name("wifi_combobox", client->root)) {
            auto data = (GenericComboBox *) container->user_data;
            std::string interface = data->determine_selected(client, client->cr, container);
            for (auto l: wifi_data->links) {
                if (l->interface == interface) {
                    return l;
                }
            }
        }
    }
    return nullptr;
}

static void
paint_option(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    auto data = (WifiOptionData *) container->user_data;
    if (data->clicked) {
        set_argb(cr, config->color_search_accent);
    } else if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_wifi_pressed_button);
        } else {
            set_argb(cr, config->color_wifi_hovered_button);
        }
    } else {
        set_argb(cr, config->color_wifi_default_button);
    }
    cairo_fill(cr);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(data->info.network_name);
    pango_layout_set_text(layout, message.data(), message.length());
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 48 * config->dpi,
                  container->real_bounds.y + 10 * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    ArgbColor subtitle_color = config->color_volume_text;
    subtitle_color.a = .6;
    set_argb(cr, subtitle_color);
    
    if (data->info.saved_network) {
        if (data->info.auth != AUTH_NONE_OPEN) {
            pango_layout_set_text(layout, "Connected, secured", -1);
        } else {
            pango_layout_set_text(layout, "Saved", -1);
        }
    } else if (data->info.auth != AUTH_NONE_OPEN) {
        pango_layout_set_text(layout, "Secured", -1);
    } else {
        pango_layout_set_text(layout, "Open", -1);
    }
    cairo_move_to(cr,
                  container->real_bounds.x + 48 * config->dpi,
                  container->real_bounds.y + 29 * config->dpi);
    pango_cairo_show_layout(cr, layout);
    
    layout = get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 24 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // TODO: signal strength into account
    int strength = 0;
    try {
        strength = std::atoi(data->info.connection_quality.c_str());
    } catch (std::exception &e) {
    
    }
    int level = calculateSignalLevel(strength, 4);
    std::string strength_icon;
    if (level == 3) {
        strength_icon = "\uE701";
    } else if (level == 2) {
        strength_icon = "\uE874";
    } else if (level == 1) {
        strength_icon = "\uE873";
    } else {
        strength_icon = "\uE872";
    }
    
    pango_layout_set_text(layout, strength_icon.data(), strlen("\uE83F"));
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    set_argb(cr, config->color_taskbar_windows_button_default_icon);
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + ((48 * config->dpi) / 2) - width / 2),
                  (int) (container->real_bounds.y + ((48 * config->dpi) / 2) - width / 2));
    pango_cairo_show_layout(cr, layout);
    
    bool locked = true;
    if (locked) {
        layout = get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 20 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        pango_layout_set_text(layout, "\uE889", strlen("\uE889"));
        
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + ((48 * config->dpi) / 2) - width * .4),
                      (int) (container->real_bounds.y + ((48 * config->dpi) / 2) - width * .4));
        pango_cairo_show_layout(cr, layout);
    }
}

static void
paint_debug(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, ArgbColor(1, 0, 0, 1));
        } else {
            set_argb(cr, ArgbColor(1, 0, 1, 1));
        }
    } else {
        set_argb(cr, ArgbColor(1, 1, 0, 1));
    }
    cairo_fill(cr);
}

static void
paint_clicked(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_search_accent);
    cairo_fill(cr);
}

struct DataOfLabelButton : UserData {
    ScanResult info;
    std::string text;
};

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_wifi_pressed_button);
        } else {
            set_argb(cr, config->color_wifi_hovered_button);
        }
    } else {
        set_argb(cr, config->color_wifi_default_button);
    }
    cairo_fill(cr);
    
    auto data = (DataOfLabelButton *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(data->text);
    pango_layout_set_text(layout, message.data(), message.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void delete_container(App *app, AppClient *client, Timeout *timeout, void *user_data) {
    auto forget_button_container = (Container *) user_data;
    auto wifi_option_container = forget_button_container->parent;
    auto options_container = wifi_option_container->parent;
    
    for (int i = 0; i < options_container->children.size(); i++) {
        if (options_container->children[i] == wifi_option_container) {
            options_container->children.erase(options_container->children.begin() + i);
            delete wifi_option_container;
            break;
        }
    }
    
    client_layout(app, client);
    client_paint(app, client);
}

static void
clicked_forget(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (DataOfLabelButton *) container->user_data;
//    wifi_forget_network(data->info);
    
    app_timeout_create(app, client, 0, delete_container, container, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
clicked_connect(AppClient *client, cairo_t *cr, Container *container) {

}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (WifiOptionData *) container->user_data;
    if (data->clicked) {
        container->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi;
    } else {
        container->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi * 2;
    }
    data->clicked = !data->clicked;
    if (auto content = container_by_name("content", client->root)) {
        for (auto c: content->children) {
            if (c != container) {
                ((WifiOptionData *) c->user_data)->clicked = false;
                c->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi;
            }
        }
    }
    client_layout(app, client);
}

void scan_results(std::vector<ScanResult> &results) {
    if (auto client = client_by_name(app, "wifi_menu")) {
        auto content = container_by_name("content", client->root);
        
        // Update all data
        for (auto c: content->children) {
            auto *data = (WifiOptionData *) c->user_data;
            for (auto &r: results) {
                if (data->info.saved_network && data->info.network_name == r.network_name ||
                    data->info.mac == r.mac && data->info.interface == r.interface) {
                    data->info = r;
                    break;
                }
            }
        }
        
        // Remove non found containers
        for (int i = content->children.size() - 1; i >= 0; i--) {
            auto c = content->children[i];
            auto data = (WifiOptionData *) c->user_data;
            bool found = false;
            for (auto &r: results) {
                if (r.mac == data->info.mac && r.interface == data->info.interface) {
                    found = true;
                }
            }
            if (!found) {
                content->parent->children.erase(content->parent->children.begin() + i);
            }
        }
        
        // Add new containers if not already in there
        for (auto &r: results) {
            bool found = false;
            for (auto c: content->children) {
                auto data = (WifiOptionData *) c->user_data;
                if (r.mac == data->info.mac && r.interface == data->info.interface) {
                    found = true;
                }
            }
            if (!found) {
                auto c = content->child(FILL_SPACE, WIFI_OPTION_HEIGHT * config->dpi);
                c->name = r.network_name;
                auto wifi_option_data = new WifiOptionData;
                c->when_paint = paint_option;
                c->when_clicked = option_clicked;
                wifi_option_data->info = r;
                c->user_data = wifi_option_data;
            }
        }
        
        if (results.empty()) {
            content->wanted_bounds.h = 80 * config->dpi;
        }
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

void state_changed_callback() {
    if (auto l = get_active_link()) {
        if (auto client = client_by_name(app, "wifi_menu")) {
            auto content = container_by_name("content", client->root);
            // TODO: content->children is empty which is impossible
            
            bool any_different_interface = true;
            // Update all data
            for (auto c: content->children) {
                auto *data = (WifiOptionData *) c->user_data;
                if (data->info.interface != l->interface) {
                    any_different_interface = true;
                }
            }
            if (any_different_interface) {
                for (auto c: content->children) {
                    delete c;
                }
                content->children.clear();
            }
        }
        
        scan_results(l->results);
    }
    if (auto client = client_by_name(app, "wifi_menu")) {
//        client_unregister_animation(app, client);
        auto data = (RootScanAnimationData *) client->root->user_data;
        data->running = false;
    }
}

void wifi_state(AppClient *client, bool *up, bool *wired) {
    // throttle the state check to once every 5 seconds
    static std::chrono::time_point<std::chrono::system_clock> last_check;
    static bool last_up = false;
    static bool last_wired = false;
    if (std::chrono::system_clock::now() - last_check < std::chrono::seconds(5)) {
        *up = last_up;
        *wired = last_wired;
        return;
    }
    std::string status = "down";
    const std::string &default_interface = get_default_wifi_interface(client);
    std::ifstream status_file("/sys/class/net/" + default_interface + "/operstate");
    if (status_file.is_open()) {
        std::string line;
        if (getline(status_file, line)) {
            status = line;
        }
        status_file.close();
    }
    
    *up = status == "up";
    
    // Wireless interfaces are prefixed with wlp
    *wired = std::string::npos == default_interface.find("wlp");
    
    last_up = *up;
    last_wired = *wired;
}

static double map(double x, double in_min, double in_max, double out_min, double out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static std::string root_message;

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_wifi_background));
    cairo_fill(cr);
    
    auto data = (RootScanAnimationData *) container->user_data;
    
    if (data->running) {
        auto current_time = get_current_time_in_ms();
        auto elapsed_time = current_time - data->start;
        if (elapsed_time > 3800)
            data->running = false;
        long animation_length = 2000; // in milliseconds (1000 is 1 second)
        double scalar = ((double) (elapsed_time % animation_length)) / ((double) animation_length);
        
        double r = 4;
        int dots_count = 5;
        double dot_delay = .1;
        
        for (int i = 0; i < dots_count; i++) {
            double low = dot_delay * i;
            double high = dot_delay * i + dot_delay * dots_count;
            
            if (scalar >= low && scalar <= high) {
                double fixed = map(scalar, low, high, 0, 1);
                
                if (fixed < .5) {
                    fixed = getEasingFunction(easing_functions::EaseOutQuad)(fixed * 2) / 2;
                } else {
                    fixed = 1 - getEasingFunction(easing_functions::EaseOutQuad)((fixed * 2)) / 2;
                }
                
                cairo_save(cr);
                set_argb(cr, config->color_search_accent);
                cairo_translate(cr, ((container->real_bounds.w + r * 2) * fixed) - r, r + (r / 2));
                cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_restore(cr);
            }
        }
    }
    
    if (!root_message.empty()) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
        
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, (container->real_bounds.w - 40) * PANGO_SCALE);
        pango_layout_set_text(layout, root_message.data(), root_message.length());
        
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);
        
        set_argb(cr, config->color_wifi_text_title);
        cairo_move_to(cr,
                      container->real_bounds.x + ((container->real_bounds.w - 40) / 2) -
                      (ink.width / PANGO_SCALE / 2) + 20,
                      container->real_bounds.y + ((container->real_bounds.h) / 2) -
                      (ink.height / PANGO_SCALE / 2));
        pango_cairo_show_layout(cr, layout);
        
        pango_layout_set_width(layout, -1);
    }
}

struct WifiToggle : UserData {
    long last_time_checked = get_current_time_in_ms() - 1000;
    bool wifi_is_enabled = true;
    int checked_count = 0;
};

static void
paint_wifi_toggle(AppClient *, cairo_t *cr, Container *container) {
    auto data = (WifiToggle *) container->user_data;
    if (app->current - data->last_time_checked > 1000 * 20) {
        data->last_time_checked = app->current;
        data->wifi_is_enabled = wifi_global_status(get_active_link());
    }
    bool wifi_is_enabled = data->wifi_is_enabled;
    if (wifi_is_enabled) {
        auto color = config->color_search_accent;
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                set_argb(cr, darken(color, 5));
            } else {
                set_argb(cr, lighten(color, 5));
            }
        } else {
            set_argb(cr, color);
        }
        
    } else if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, darken(config->color_wifi_hovered_button, 5));
        } else {
            auto color = config->color_wifi_hovered_button;
            color.a += .05;
            set_argb(cr, color);
        }
    } else {
        set_argb(cr, config->color_wifi_hovered_button);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    
    auto layout = get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 14 * config->dpi,
                                        PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE701", strlen("\uE83F"));
    set_argb(cr, config->color_taskbar_windows_button_default_icon);
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 5 * config->dpi),
                  (int) (container->real_bounds.y + 7 * config->dpi));
    pango_cairo_show_layout(cr, layout);
    
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, config->color_volume_text);
    
    pango_layout_set_text(layout, "Wi-Fi", -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  container->real_bounds.x + 5 * config->dpi,
                  container->real_bounds.y + container->real_bounds.h - 6 * config->dpi - height);
    pango_cairo_show_layout(cr, layout);
}

static void
clicked_wifi_toggle(AppClient *client, cairo_t *, Container *container) {
    auto data = (WifiToggle *) container->user_data;
    data->last_time_checked = app->current;
    data->wifi_is_enabled = wifi_global_status(get_active_link());
    if (data->wifi_is_enabled) {
        wifi_global_disable(get_active_link());
    } else {
        wifi_global_enable(get_active_link());
    };
    data->wifi_is_enabled = !data->wifi_is_enabled;
    app_timeout_create(app, client, 1000, [](App *, AppClient *, Timeout *timeout, void *) {
        timeout->keep_running = true;
        if (auto client = client_by_name(app, "wifi_menu")) {
            if (auto toggle = container_by_name("wifi_toggle", client->root)) {
                auto data = (WifiToggle *) toggle->user_data;
                data->wifi_is_enabled = wifi_global_status(get_active_link());
                data->checked_count++;
                if (data->checked_count > 4) {
                    data->checked_count = 0;
                    timeout->keep_running = false;
                }
            }
        }
    }, nullptr, "update wifi status every 1000ms after toggle");
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->type = ::vbox;
    root->when_paint = paint_root;
    auto root_animation_data = new RootScanAnimationData;
    root_animation_data->start = get_current_time_in_ms();
    root_animation_data->running = true;
    root->user_data = root_animation_data;
    
    ScrollPaneSettings settings(config->dpi);
    settings.right_inline_track = true;
    auto scrollpane = make_newscrollpane_as_child(root, settings);
    Container *content = scrollpane->content;
    content->name = "content";
    content->wanted_pad.y = 12 * config->dpi;
    content->wanted_pad.h = 12 * config->dpi;
    
    double button_height = 90 * config->dpi + 40 * config->dpi + 38 * config->dpi;
    
    auto bottom_buttons_pane = root->child(::vbox, FILL_SPACE, button_height);
    bottom_buttons_pane->wanted_pad = Bounds(6 * config->dpi, 6 * config->dpi, 6 * config->dpi, 6 * config->dpi);
    bottom_buttons_pane->spacing = 6 * config->dpi;
    
    auto pref_label = bottom_buttons_pane->child(FILL_SPACE, 28 * config->dpi);
    pref_label->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        auto layout = get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        pango_layout_set_text(layout, "Preferred network card", -1);
        set_argb(cr, config->color_action_center_history_text);
        
        int width;
        int height;
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + 5 * config->dpi),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    };
    
    {
        auto combo_data = new GenericComboBox("wifi_combobox", "");
        std::string network_interfaces_dir = "/var/run/wpa_supplicant";
        namespace fs = std::filesystem;
        try {
            for (const auto &entry: fs::directory_iterator(network_interfaces_dir)) {
                if (!entry.is_directory()) {
                    combo_data->options.emplace_back(entry.path().filename().string());
                }
            }
        } catch (std::exception &e) {
            
        }
        
        combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
            // TODO: this needs to reality check against available interfaces
            auto pref = winbar_settings->get_preferred_interface();
            for (auto s: wifi_data->seen_interfaces) {
                if (s == pref)
                    return s;
            }
            return "";
        };
        combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
            std::string interface = ((Label *) (self->user_data))->text;
            wifi_set_active(interface);
            if (auto c = client_by_name(app, "wifi_menu")) {
                if (auto con = container_by_name("content", c->root)) {
                    for (auto ch: con->children) {
                        delete ch;
                    }
                    con->children.clear();
                    client_layout(app, c);
                    request_refresh(app, c);
                }
                if (auto con = container_by_name("wifi_toggle", c->root)) {
                    auto data = (WifiToggle *) con->user_data;
                    if (wifi_running(get_active_link() == nullptr ? "" : get_active_link()->interface)) {
                        data->wifi_is_enabled = wifi_global_status(get_active_link());
                    } else {
                        data->wifi_is_enabled = false;
                    }
                }
            }
            wifi_networks_and_cached_scan(get_active_link());
            client_close_threaded(app, client);
            
            // TODO: fill root based on interface connection
        };
        
        auto combo_box = bottom_buttons_pane->child(FILL_SPACE, 36 * config->dpi);
        combo_box->name = combo_data->name;
        combo_box->when_clicked = clicked_expand_generic_combobox_dark;
        combo_box->when_paint = paint_generic_combobox_dark;
        combo_box->user_data = combo_data;
    }
    
    auto wifi_toggle_button = bottom_buttons_pane->child(button_height * .64, FILL_SPACE);
    wifi_toggle_button->name = "wifi_toggle";
    auto wifi_toggle_data = new WifiToggle;
    if (wifi_running(get_active_link() == nullptr ? "" : get_active_link()->interface)) {
        wifi_toggle_data->wifi_is_enabled = wifi_global_status(get_active_link());
    } else {
        wifi_toggle_data->wifi_is_enabled = false;
    }
    wifi_toggle_button->user_data = wifi_toggle_data;
    wifi_toggle_button->when_paint = paint_wifi_toggle;
    wifi_toggle_button->when_clicked = clicked_wifi_toggle;
}

void start_wifi_menu() {
    wifi_start(app);
    
    Settings settings;
    settings.h = 641 * config->dpi;
    settings.w = 360 * config->dpi;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.slide_data[1] = 3;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *container = container_by_name("wifi", taskbar->root);
        if (container->real_bounds.x > taskbar->bounds->w / 2) {
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        } else {
            settings.x = 0;
            settings.slide_data[1] = 0;
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.sticky = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "wifi_menu";
        auto client = taskbar->create_popup(popup_settings, settings);
        fill_root(client);

//        if (wifi_running()) {
        root_message = "";
//        client_register_animation(app, client);
        wifi_data->when_state_changed = state_changed_callback;
        wifi_networks_and_cached_scan(get_active_link());
        wifi_scan(get_active_link());
//        } else {
//            root_message = "Couldn't establish communication with wpa_supplicant";
////            root_message = "WIFI menu is not fully implemented yet";
//            auto data = (RootScanAnimationData *) client->root->user_data;
//            data->running = false;
//        }
        
        client_show(app, client);
    }
}
