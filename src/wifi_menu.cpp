
#include "wifi_menu.h"
#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "utility.h"
#include "wifi_backend.h"
#include "components.h"

#include <fstream>
#include <iostream>
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
            get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(data->info.network_name);
    pango_layout_set_text(layout, message.data(), message.length());
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 48,
                  container->real_bounds.y + 10);
    pango_cairo_show_layout(cr, layout);
    
    message = data->info.flags + ":" + data->info.mac;
    pango_layout_set_text(layout, message.data(), message.length());
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 48,
                  container->real_bounds.y + 29);
    pango_cairo_show_layout(cr, layout);
    
    auto root_data = (RootScanAnimationData *) client->root->user_data;
/*    dye_surface(root_data->wifi_surface, config->color_taskbar_button_icons);
    cairo_set_source_surface(
            cr,
            root_data->wifi_surface,
            (int) (container->real_bounds.x + 48 / 2 - 24 / 2),
            (int) (container->real_bounds.y + WIFI_OPTION_HEIGHT / 2 - 24 / 2));
    cairo_paint(cr);*/
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
    
    app_timeout_create(app, client, 0, delete_container, container);
}

static void
clicked_connect(AppClient *client, cairo_t *cr, Container *container) {

}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container) {
    // delete all currently opened (clicked) containers to start with
    for (int i = 0; i < container->parent->children.size(); i++) {
        int r_i = container->parent->children.size() - i - 1;
        auto c = container->parent->children[r_i];
        auto data = (WifiOptionData *) c->user_data;
        
        if (data && data->clicked) {
            if (c == container) {
                return;
            }
            data->clicked = false;
            auto cd = container->parent->children[r_i + 1];
            container->parent->children.erase(container->parent->children.begin() + r_i + 1);
            delete cd;
        }
    }
    
    auto data = (WifiOptionData *) container->user_data;
    
    if (!data->clicked) {
        int container_index = 0;
        for (int i = 0; i < container->parent->children.size(); i++) {
            if (container->parent->children[i] == container)
                container_index = i;
        }
        
        int option_height = 34;
        int padding = 8;
        Container *new_container;
        if (data->info.saved_network) {
            new_container = new Container(layout_type::hbox, FILL_SPACE, option_height + padding * 2);
            new_container->parent = container->parent;
            new_container->spacing = padding;
            new_container->wanted_pad = Bounds(padding, padding, padding, padding);
            new_container->when_paint = paint_clicked;
            
            Container *forget_button = new_container->child(FILL_SPACE, FILL_SPACE);
            auto forget_data = new DataOfLabelButton;
            forget_data->text = "Forget";
            forget_data->info = data->info;
            forget_button->user_data = forget_data;
            forget_button->when_paint = paint_centered_label;
            forget_button->when_clicked = clicked_forget;
            
            Container *connect_disconnect_button = new_container->child(FILL_SPACE, FILL_SPACE);
            auto connect_data = new DataOfLabelButton;
            connect_data->text = "Connect";
            connect_data->info = data->info;
            connect_disconnect_button->user_data = connect_data;
            connect_disconnect_button->when_paint = paint_centered_label;
            connect_disconnect_button->when_clicked = clicked_connect;
        } else {
            new_container = new Container(layout_type::vbox, FILL_SPACE, option_height * 2 + padding * 3);
            new_container->parent = container->parent;
            new_container->spacing = padding;
            new_container->wanted_pad = Bounds(padding, padding, padding, padding);
            new_container->when_paint = paint_clicked;
            
            Container *security_dropbox = new_container->child(FILL_SPACE, FILL_SPACE);
            security_dropbox->when_paint = paint_debug;
            Container *password_field = new_container->child(FILL_SPACE, FILL_SPACE);
            password_field->when_paint = paint_debug;
        }
        container->parent->children.insert(container->parent->children.begin() + container_index + 1, new_container);
    }
    
    data->clicked = !data->clicked;
    if (auto c = container_by_name("content", client->root)) {
        //        c->wanted_bounds.h = true_height(c->parent) + true_height(c);
    }
    
    client_layout(app, client);
    client_paint(app, client);
}

void scan_results(std::vector<ScanResult> &results) {
    if (auto client = client_by_name(app, "wifi_menu")) {
        auto root = client->root;
        
        for (auto c: root->children)
            delete c;
        root->children.clear();
        
        ScrollPaneSettings settings;
        Container *scrollpane = make_scrollpane(root, settings);
        Container *content = scrollpane->child(::vbox, FILL_SPACE, FILL_SPACE);
        content->name = "content";
        content->wanted_pad.y = 12;
        content->wanted_pad.h = 12;
        
        for (const auto &r: results) {
            auto c = content->child(FILL_SPACE, WIFI_OPTION_HEIGHT);
            c->name = r.network_name;
            auto wifi_option_data = new WifiOptionData;
            c->when_paint = paint_option;
            c->when_clicked = option_clicked;
            wifi_option_data->info = r;
            c->user_data = wifi_option_data;
        }
        
        if (!results.empty()) {
            content->wanted_bounds.h = true_height(scrollpane) + true_height(content);
        } else {
            content->wanted_bounds.h = 80;
        }
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

void cached_scan_results(std::vector<ScanResult> &results) {
    scan_results(results);
}

void uncached_scan_results(std::vector<ScanResult> &results) {
    scan_results(results);
    if (auto client = client_by_name(app, "wifi_menu")) {
        client_unregister_animation(app, client);
        auto data = (RootScanAnimationData *) client->root->user_data;
        data->running = false;
    }
}

void wifi_state(bool *up, bool *wired) {
    std::string status = "down";
    std::ifstream status_file("/sys/class/net/" + get_default_wifi_interface() + "/operstate");
    if (status_file.is_open()) {
        std::string line;
        if (getline(status_file, line)) {
            status = line;
        }
        status_file.close();
    }
    
    *up = status == "up";
    
    // Wireless interfaces are prefixed with wlp
    *wired = std::string::npos == get_default_wifi_interface().find("wlp");
}

double map(double x, double in_min, double in_max, double out_min, double out_max) {
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
                get_cached_pango_font(cr, config->font, 12, PangoWeight::PANGO_WEIGHT_BOLD);
        
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

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->type = ::vbox;
    root->when_paint = paint_root;
    auto root_animation_data = new RootScanAnimationData;
    root_animation_data->start = get_current_time_in_ms();
    root_animation_data->running = true;
    root->user_data = root_animation_data;
}

void start_wifi_menu() {
    Settings settings;
    settings.h = 641;
    settings.w = 360;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.sticky = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "wifi_menu";
        auto client = taskbar->create_popup(popup_settings, settings);
        fill_root(client);
        
        if (wifi_running()) {
            root_message = "";
            client_register_animation(app, client);
            wifi_networks_and_cached_scan(cached_scan_results);
            wifi_scan(uncached_scan_results);
        } else {
            root_message = "WIFI menu is not fully implemented yet";
            auto data = (RootScanAnimationData *) client->root->user_data;
            data->running = false;
        }
        
        client_show(app, client);
    }
}
