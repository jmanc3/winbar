
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
    auto data = (WifiOptionData *) container->children[0]->user_data;
    auto  color = config->color_wifi_default_button;
    if (data->clicked) {
        color = config->color_search_accent;
    } else if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color = config->color_wifi_pressed_button;
        } else {
            color = config->color_wifi_hovered_button;
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
    
    draw_text(client, 10 * config->dpi, config->font, EXPAND(config->color_volume_text), data->info.network_name, container->real_bounds, 5, 48 * config->dpi, 10 * config->dpi);
    
    ArgbColor subtitle_color = config->color_volume_text;
    subtitle_color.a = .6;
    auto text = "Open";
    if (data->info.saved_network) {
        if (data->info.auth != AUTH_NONE_OPEN) {
            text = "Connected";
        } else {
            text = "Saved";
        }
    } else if (data->info.auth != AUTH_NONE_OPEN) {
        text = "Secured";
    }
    draw_text(client, 9 * config->dpi, config->font, EXPAND(subtitle_color), data->info.network_name, container->real_bounds, 5, 48 * config->dpi, 29 * config->dpi);
    
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
        
    {
        auto [f, w, h] = draw_text_begin(client, 24 * config->dpi, config->icons, EXPAND(config->color_taskbar_windows_button_default_icon), strength_icon);
        f->draw_text_end((int) (container->real_bounds.x + ((48 * config->dpi) / 2) - w / 2),
                         (int) (container->real_bounds.y + ((48 * config->dpi) / 2) - w / 2));
    }
    
    bool locked = true;
    if (locked) {
            auto [f, w, h] = draw_text_begin(client, 20 * config->dpi, config->icons, EXPAND(config->color_taskbar_windows_button_default_icon), "\uE889");
        f->draw_text_end((int) (container->real_bounds.x + ((48 * config->dpi) / 2) - w * .4),
                         (int) (container->real_bounds.y + ((48 * config->dpi) / 2) - w * .4));
    }
}

static void
paint_debug(AppClient *client, cairo_t *cr, Container *container) {
    auto color = ArgbColor(1, 1, 0, 1);
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color = ArgbColor(1, 0, 0, 1);
        } else {
            color = ArgbColor(1, 0, 1, 1);
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
}

static void
paint_clicked(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, config->color_search_accent, container->real_bounds);
}

struct DataOfLabelButton : UserData {
    ScanResult info;
    std::string text;
};

static void
recheck_wifi_status(AppClient *client);

static void
clicked_forget(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (WifiOptionData *) container->parent->parent->children[0]->user_data;
    wifi_forget_network(data->info);
    recheck_wifi_status(client);
}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container);

static void
paint_label(AppClient *client, cairo_t *cr, Container *container, std::string text);

static bool show = false;

static void
paint_textfield(AppClient *client, cairo_t *cr, Container *container) {
    // paint blue border
    auto *data = (FieldData *) container->user_data;
    
    draw_colored_rect(client, ArgbColor(1, 1, 1, 1), container->real_bounds);
    
    // clip text
    PangoLayout *layout = get_cached_pango_font(cr, config->font, data->settings.font_size,
                                                PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_width(layout, -1); // disable wrapping
    
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    std::string text_set;
    ArgbColor color = config->color_pinned_icon_editor_field_default_text;
    if (!data->text.empty()) {
        std::string t;
        int size = data->text.size();
        for (int i = 0; i < size; i++)
            t.append("•");
        pango_layout_set_text(layout, t.c_str(), t.size());
        if (show) {
            text_set = data->text;
        } else {
            text_set = t;
        }
    } else {
        auto watered_down = ArgbColor(config->color_pinned_icon_editor_field_default_text);
        watered_down.a = 0.6;
        color = watered_down;
        set_argb(cr, watered_down);
        pango_layout_set_text(layout, data->settings.when_empty_text.c_str(), data->settings.when_empty_text.size());
        text_set = data->settings.when_empty_text;
    }
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    auto text_off_x = 10 * config->dpi;
    auto text_off_y = container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2);
    
    auto b = Bounds(container->real_bounds);
    b.x += 10 * config->dpi;
    b.w -= 10 * config->dpi;
    draw_clip_begin(client, container->real_bounds);
    auto [f, w, h] = draw_text_begin(client, data->settings.font_size, config->font, EXPAND(color), text_set);
    auto overflow = w - (container->real_bounds.w - 45 * config->dpi);
    if (overflow < 0)
        overflow = 0;
    f->draw_text_end(container->real_bounds.x + 10 * config->dpi - overflow, MIDY(container) - h / 2);
    draw_clip_end(client);
    
    if (container->active) {
        PangoRectangle cursor_strong_pos;
        PangoRectangle cursor_weak_pos;
        if (data->text.empty()) {
            pango_layout_get_cursor_pos(layout, 0, &cursor_strong_pos, &cursor_weak_pos);
        } else {
            pango_layout_get_cursor_pos(layout, text_set.size(), &cursor_strong_pos, &cursor_weak_pos);
        }
        int offset = cursor_strong_pos.x != 0 ? -1 : 0;
        draw_colored_rect(client, ArgbColor(0, 0, 0, 1), Bounds(cursor_strong_pos.x / PANGO_SCALE + container->real_bounds.x + offset + text_off_x - overflow,
                                                                cursor_strong_pos.y / PANGO_SCALE + container->real_bounds.y + text_off_y,
                                                                1 * config->dpi,
                                                                cursor_strong_pos.height / PANGO_SCALE));
    }
    
    //E7B3
    auto show_color = ArgbColor(0, 0, 0, .9);
    if (show)
        show_color = config->color_search_accent;
    auto ff =  draw_text_begin(client, data->settings.font_size, config->icons, EXPAND(show_color), "\uF78D");
    ff.f->draw_text_end(container->real_bounds.x + container->real_bounds.w - (45 * config->dpi / 2) - (ff.w * .45),
                        MIDY(container) - ff.h / 2);
    
    color = config->color_pinned_icon_editor_field_default_border;
    if (container->active) {
        color = config->color_pinned_icon_editor_field_pressed_border;
    } else if (container->state.mouse_hovering) {
        color = config->color_pinned_icon_editor_field_hovered_border;
    }
    draw_margins_rect(client, color, container->real_bounds, 2, 0);
}

struct WifiToggle : UserData {
    long last_time_checked = get_current_time_in_ms() - 1000;
    bool wifi_is_enabled = true;
    int checked_count = 0;
};

static long last_check = 0;

static void
recheck_wifi_status(AppClient *client) {
    app_timeout_create(app, client, 250, [](App *, AppClient *client, Timeout *timeout, void *) {
        timeout->keep_running = false;
        if (auto wifi_toggle = container_by_name("wifi_toggle", client->root)) {
            timeout->keep_running = true;
            auto data = (WifiToggle *) wifi_toggle->user_data;
            data->last_time_checked = app->current;
            data->checked_count++;
            bool before = data->wifi_is_enabled;
            data->wifi_is_enabled = wifi_global_status(get_active_link());
            if (data->checked_count > 20) {
                timeout->keep_running = false;
                data->checked_count = 0;
            }
            if (before != data->wifi_is_enabled) {
                last_check = 0;
                request_refresh(app, client_by_name(app, "taskbar"));
                request_refresh(app, client);
            }
        }
    }, nullptr, "Re-check wifi status after adding network");
    
}

static void
clicked_connect(AppClient *client, cairo_t *cr, Container *container) {
    auto first_child = container->parent->parent->children[0];
    auto data = (WifiOptionData *) first_child->user_data;
    auto not_enc = data->info.auth == AUTH_NONE_OPEN || data->info.auth == AUTH_NONE_WEP;
    if (not_enc) {
        wifi_connect_network(data->info, "");
        recheck_wifi_status(client);
        return;
    }
    auto parent = first_child->parent;
    auto pad = ((WIFI_OPTION_HEIGHT - 36) * config->dpi) * .5;
    auto button_height = 36 * config->dpi;
    
    parent->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi + button_height  * 2 + pad * 2;
    // Delete everything below the first element
    for (int i = parent->children.size() - 1; i >= 1; i--) {
        delete parent->children[i];
        parent->children.erase(parent->children.begin() + i);
    }
    
    auto button_container = parent->child(::vbox, FILL_SPACE, FILL_SPACE);
    button_container->wanted_pad = Bounds(pad, 0, pad, 0);
    auto textarea_parent = button_container->child(FILL_SPACE, button_height);

    FieldSettings settings;
    settings.font_size = 11 * config->dpi;
    settings.when_empty_text = "Enter the network security key";
    auto textarea = make_textfield(textarea_parent, settings, FILL_SPACE, FILL_SPACE);
    textarea->when_paint = paint_textfield;
    textarea->when_clicked = [](AppClient *client, cairo_t *, Container *container) {
        float w = 45 * config->dpi;
        if (bounds_contains(Bounds(container->real_bounds.x + container->real_bounds.w - w, container->real_bounds.y, w, container->real_bounds.h), client->mouse_current_x, client->mouse_current_y)) {
            show = !show;
        }
    };
    set_active(client, textarea, true);
    // pad
    button_container->child(FILL_SPACE, FILL_SPACE);
    auto hbox_cont = button_container->child(::hbox, FILL_SPACE, button_height);
    hbox_cont->spacing = pad;
    auto next = hbox_cont->child(FILL_SPACE, FILL_SPACE);
    next->name = "next";
    next->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        auto color = config->color_wifi_hovered_button;
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                color = config->color_wifi_hovered_button;
            } else {
                color = config->color_wifi_pressed_button;
            }
        }
        auto field_container = container->parent->parent->children[0]->children[0];
        auto *data = (FieldData *) field_container->user_data;
        if (!data->text.empty()) {
            draw_colored_rect(client, color, container->real_bounds);
        }
        auto text_color = config->color_volume_text;
        if (data->text.empty())
            text_color.a = .7;
        draw_text(client, config->dpi * 10, config->font, EXPAND(text_color), "Next", container->real_bounds);
    };
    next->when_clicked = [](AppClient *client, cairo_t *, Container *container) {
        auto field_container = container->parent->parent->children[0]->children[0];
        auto *field_data = (FieldData *) field_container->user_data;
        auto first_child = container->parent->parent->parent->children[0];
        auto *first_data = (WifiOptionData *) first_child->user_data;
        if (!field_data->text.empty()) {
            // actually do the next
            wifi_connect_network(first_data->info, field_data->text);
            recheck_wifi_status(client);
        }
    };
    auto cancel = hbox_cont->child(FILL_SPACE, FILL_SPACE);
    cancel->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        paint_label(client, cr, container, "Cancel");
    };
    cancel->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
        auto first_child = container->parent->parent->parent->children[0];
        auto data = (WifiOptionData *) first_child->user_data;
        data->clicked = false; // So that it's like a reset
        option_clicked(client, cr, first_child);
    };
    
    // pad
    button_container->child(FILL_SPACE, FILL_SPACE);
    
    
    client_layout(app, client);
    request_refresh(app, client);
}

static void
paint_label(AppClient *client, cairo_t *cr, Container *container, std::string text) {
    auto color = config->color_wifi_hovered_button;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color = config->color_wifi_hovered_button;
        } else {
            color = config->color_wifi_pressed_button;
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
    
    draw_text(client, config->dpi * 10, config->font, EXPAND(config->color_volume_text), text, container->real_bounds);
}

static void
paint_connect(AppClient *client, cairo_t *cr, Container *container) {
    paint_label(client, cr, container, "Connect");
}

static void
paint_disconnect(AppClient *client, cairo_t *cr, Container *container) {
    paint_label(client, cr, container, "Disconnect");
}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container) {
    show = false;
    auto data = (WifiOptionData *) container->user_data;
    auto button_height = 36 * config->dpi;
    auto pad = ((WIFI_OPTION_HEIGHT - 36) * config->dpi) * .5;
    if (data->clicked) {
        container->parent->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi;
    } else {
        container->parent->wanted_bounds.h = WIFI_OPTION_HEIGHT * config->dpi + button_height + pad;
    }
    data->clicked = !data->clicked;
    
    bool saved = data->info.saved_network;
    
    // Delete everything below the first element
    for (int i = container->parent->children.size() - 1; i >= 1; i--) {
        delete container->parent->children[i];
        container->parent->children.erase(container->parent->children.begin() + i);
    }
    
    if (data->clicked) {
        auto parent = container->parent;
        if (saved) { // Allow disconnect
            auto button_container = parent->child(::vbox, FILL_SPACE, button_height);
            button_container->wanted_pad = Bounds(pad, 0, pad, 0);
            auto button = button_container->child(FILL_SPACE, FILL_SPACE);
            button->when_paint = paint_disconnect;
            button->when_clicked = clicked_forget;
            parent->child(FILL_SPACE, FILL_SPACE);
        } else { // Allow connect
            auto button_container = parent->child(::vbox, FILL_SPACE, button_height);
            button_container->wanted_pad = Bounds(pad, 0, pad, 0);
            auto button = button_container->child(FILL_SPACE, FILL_SPACE);
            button->when_paint = paint_connect;
            button->when_clicked = clicked_connect;
            parent->child(FILL_SPACE, FILL_SPACE);
        }
    }
    
    client_layout(app, client);
}

void scan_results(std::vector<ScanResult> &results) {
    if (auto client = client_by_name(app, "wifi_menu")) {
        auto content = container_by_name("content", client->root);
        
        // Update all data
        for (auto c: content->children) {
            auto *data = (WifiOptionData *) c->children[0]->user_data;
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
            auto data = (WifiOptionData *) c->children[0]->user_data;
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
                auto data = (WifiOptionData *) c->children[0]->user_data;
                if (r.mac == data->info.mac && r.interface == data->info.interface) {
                    found = true;
                }
            }
            if (!found) {
                auto parent_option_vbox = content->child(::vbox, FILL_SPACE, WIFI_OPTION_HEIGHT * config->dpi);
                parent_option_vbox->when_paint = paint_option;
                parent_option_vbox->receive_events_even_if_obstructed_by_one = true;
                
                // Spacing
                auto c = parent_option_vbox->child(FILL_SPACE, WIFI_OPTION_HEIGHT * config->dpi);
                c->name = r.network_name;
                auto wifi_option_data = new WifiOptionData;
                c->when_clicked = option_clicked;
                wifi_option_data->info = r;
                c->user_data = wifi_option_data;

//                auto button = parent_option_vbox->child(::stack,FILL_SPACE, FILL_SPACE);
//                button->exists = false;
//                button->when_paint = paint_debug;
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
                auto *data = (WifiOptionData *) c->children[0]->user_data;
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
    static bool last_up = false;
    static bool last_wired = false;
    if (client->app->current - last_check < 1000) {
        *up = last_up;
        *wired = last_wired;
        return;
    }
    last_check = client->app->current;
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
    draw_colored_rect(client, correct_opaqueness(client, config->color_wifi_background), container->real_bounds);
    
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
        auto f = draw_get_font(client, 12 * config->dpi, config->font, true);
        auto text = f->wrapped_text(root_message, (container->real_bounds.w - 40));
        f->begin();
        f->set_text(text);
        auto [w, h] = f->sizes();
        f->end();
        
        f->begin();
        f->set_text(text);
        f->set_color(EXPAND(config->color_wifi_text_title));
        
        auto x = container->real_bounds.x + ((container->real_bounds.w - 40) / 2) - (w / 2) + 20; 
        auto y = container->real_bounds.y + ((container->real_bounds.h) / 2) - (h / 2);
        f->draw_text(PANGO_ALIGN_LEFT, x, y, (container->real_bounds.w / 2));
        f->end();
        
       
        /*
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
        */
        
        // TODO: wrap 
        // pango_layout_set_width(layout, (container->real_bounds.w - 40) * PANGO_SCALE);
        // draw_text(client, config->dpi * 12, config->font, EXPAND(config->color_wifi_text_title), root_message, container->real_bounds);
    }
}

static void
paint_wifi_toggle(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (WifiToggle *) container->user_data;
    if (app->current - data->last_time_checked > 1000 * 20) {
        data->last_time_checked = app->current;
        data->wifi_is_enabled = wifi_global_status(get_active_link());
    }
    bool wifi_is_enabled = data->wifi_is_enabled;
    ArgbColor color_set;
    if (wifi_is_enabled) {
        auto color = config->color_search_accent;
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                color_set = darken(color, 5);
            } else {
                color_set = lighten(color, 5);
            }
        } else {
            color_set = color;
        }
    } else if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color_set = darken(config->color_wifi_hovered_button, 5);
        } else {
            auto color = config->color_wifi_hovered_button;
            color.a += .05;
            color_set = color;
        }
    } else {
        color_set = config->color_wifi_hovered_button;
    }
    draw_colored_rect(client, color_set, container->real_bounds);
 
    draw_text(client, 14 * config->dpi, config->icons, EXPAND(config->color_taskbar_windows_button_default_icon), "\uE701", container->real_bounds, 5, 5 * config->dpi, 7 * config->dpi);
    
    auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(config->color_volume_text), "Wi-Fi");
    f->draw_text_end(container->real_bounds.x + 5 * config->dpi,
                     container->real_bounds.y + container->real_bounds.h - 6 * config->dpi - h);
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
        auto [f, w, h] = draw_text_begin(client, 11 * config->dpi, config->font, EXPAND(config->color_action_center_history_text), "Preferred network card");
        f->draw_text_end((int) (container->real_bounds.x + 5 * config->dpi),
                         (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2));
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
        popup_settings.takes_input_focus = true;
        show = false;
        auto client = taskbar->create_popup(popup_settings, settings);
        fill_root(client);
        client->when_closed = [](AppClient *client) {
            wifi_save_config(get_active_link());
        };

//        if (wifi_running()) {
//        root_message = "Couldn't establish communication with wpa_supplicant";
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
        xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window,
                            XCB_CURRENT_TIME);
        xcb_ewmh_request_change_active_window(&app->ewmh,
                                              app->screen_number,
                                              client->window,
                                              XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                              XCB_CURRENT_TIME,
                                              XCB_NONE);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}
