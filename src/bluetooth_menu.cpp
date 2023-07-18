//
// Created by jmanc3 on 1/25/23.
//

#include "bluetooth_menu.h"
#include "simple_dbus.h"
#include "main.h"
#include "config.h"
#include "icons.h"
#include "components.h"

#include <pango/pangocairo.h>
#include <iostream>
#include <unordered_map>
#include <cmath>

struct DataOfLabelButton : UserData {
    std::string text;
    bool disabled = false;
};

struct BlueOption : DataOfLabelButton {
    cairo_surface_t *icon = nullptr;
    std::string mac_address;
    bool being_worked_on = false;
    
    ~BlueOption() override {
        cairo_surface_destroy(icon);
    }
};

struct OptionButton : DataOfLabelButton {
    BlueOption *option = nullptr;
};

struct BarMessageData : DataOfLabelButton {
    bool error = true;
};

struct InputBarData : UserData {
    BluetoothRequest *br = nullptr;
    
    InputBarData(BluetoothRequest *br) {
        this->br = br;
    }
    
    ~InputBarData() override {
        if (br) {
            // If cancellable br, cancel it
            if (br->type != "DisplayPasskey" && br->type != "Cancelled" && br->message) {
                DBusMessage *reply = dbus_message_new_error(br->message, "org.bluez.Error.Canceled", "Canceled");
                dbus_connection_send(br->connection, reply, NULL);
                dbus_message_unref(reply);
            }
            delete br;
        }
    }
};

struct BlueData : DataOfLabelButton {
    // For dots animation
    long start = get_current_time_in_ms();
    bool animation_running = false;
    bool showing_paired_devices = true;
    
    // if this is true, buttons that do actions shouldn't do anything when clicked
    bool blocking_command_running = false;
    
    void set_running(AppClient *client, bool state) {
        if (state) {
            if (!animation_running) {
                client_register_animation(client->app, client);
            }
            start = get_current_time_in_ms();
            animation_running = true;
        } else {
            if (animation_running) {
                client_unregister_animation(client->app, client);
            }
            animation_running = false;
        }
    }
};

static float option_height = 62;
int button_height = 34;

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container);

static void
fill_devices(AppClient *client);

static void
fill_root(AppClient *client, Container *root);

static double map(double x, double in_min, double in_max, double out_min, double out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
    
    auto data = (BlueData *) container->user_data;
    if (data) {
        if (data->animation_running) {
            // data->running
            auto current_time = get_current_time_in_ms();
            auto elapsed_time = current_time - data->start;
            long animation_length = 2000; // in milliseconds (1000 is 1 second)
            double scalar = ((double) (elapsed_time % animation_length)) / ((double) animation_length);
            
            double r = 4 * config->dpi;
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
                    
                    double scroll_amount = 0;
                    if (auto c = (ScrollContainer *) container_by_name("top", client->root)) {
                        if (!c->content->children.empty()) {
                            scroll_amount = c->scroll_v_real;
                        }
                    }
    
                    cairo_save(cr);
                    set_argb(cr, config->color_search_accent);
                    cairo_translate(cr, ((container->real_bounds.w + r * 2) * fixed) - r, r + (r / 2) + scroll_amount);
                    cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
                    cairo_fill(cr);
                    cairo_restore(cr);
                }
            }
        }
    }
}

static void
paint_error_bar(AppClient *, cairo_t *cr, Container *container) {
    // clip
    cairo_save(cr);
    set_rect(cr, container->real_bounds);
    cairo_clip(cr);
    
    auto data = (BarMessageData *) container->user_data;
    set_rect(cr, container->real_bounds);
    if (data->error) {
        set_argb(cr, ArgbColor(.88, .2, .3, 1));
    } else {
        set_argb(cr, ArgbColor(.3, .47, .19, 1));
    }
    cairo_fill(cr);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout,
                           (container->real_bounds.w - ((10 * 2) * config->dpi)) * PANGO_SCALE); // disable wrapping
    
    pango_layout_set_text(layout, data->text.c_str(), data->text.size());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 10 * config->dpi,
                  container->real_bounds.y + container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
paint_input_bar(AppClient *, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, ArgbColor(.3, .47, .19, 1));
    cairo_fill(cr);
}

static void
paint_message(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (DataOfLabelButton *) container->user_data;
    
    paint_root(client, cr, client->root);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int side_pad = 30 * config->dpi;
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), -1);
    pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
    pango_layout_set_width(layout, (container->real_bounds.w - (side_pad * 2)) * PANGO_SCALE);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_temp_message(AppClient *client, cairo_t *cr, Container *container) {
    auto *scroll = (ScrollContainer *) container;
    if (scroll->content->children.empty())
        paint_message(client, cr, container);
}

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container, std::string text, bool disabled) {
    auto blue_disabled = ((BlueData *) client->root->user_data)->blocking_command_running;
    
    if (!disabled && !blue_disabled) {
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
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(text);
    pango_layout_set_text(layout, message.data(), message.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    set_argb(cr, config->color_volume_text);
    if (disabled || blue_disabled) {
        ArgbColor color = config->color_volume_text;
        color.a = .41;
        set_argb(cr, color);
    }
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (DataOfLabelButton *) container->user_data;
    paint_centered_label(client, cr, container, data->text, data->disabled);
}

static void
paint_option_button(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (OptionButton *) container->user_data;
    
    cairo_save(cr);
    set_rect(cr, container->real_bounds);
    cairo_clip(cr);
    cairo_push_group(cr);
    paint_centered_label(client, cr, container, data->text, data->disabled);
    
    double max_height = button_height * config->dpi;
    double alpha = container->real_bounds.h / max_height;
    auto p = cairo_pop_group(cr);
    cairo_set_source(cr, p);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

void bar_message(AppClient *client, const std::string &text, bool error) {
    if (auto c = container_by_name("error_bar", client->root)) {
        if (auto *data = (BarMessageData *) c->user_data) {
            data->text = text;
            data->error = error;
            
            PangoLayout *layout = get_cached_pango_font(client->cr, config->font, 10 * config->dpi,
                                                        PangoWeight::PANGO_WEIGHT_NORMAL);
            pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD_CHAR);
            pango_layout_set_width(layout, (c->parent->real_bounds.w - ((10 * 2) * config->dpi)) *
                                           PANGO_SCALE); // disable wrapping
            
            pango_layout_set_text(layout, text.c_str(), text.length());
            PangoRectangle ink;
            PangoRectangle logical;
            pango_layout_get_extents(layout, &ink, &logical);
            
            // text height
            double text_height = logical.height / PANGO_SCALE;
            text_height += (8 * 2) * config->dpi; // top and bottom padding
            
            client_create_animation(app, client, &c->wanted_bounds.h, 0, 100,
                                    nullptr,
                                    text_height, true);
            // error_bar message pair failed
            app_timeout_create(app, client, 9000,
                               [](App *, AppClient *client, Timeout *,
                                  void *user_data) {
                                   auto *c = (Container *) user_data;
                                   client_create_animation(app, client,
                                                           &c->wanted_bounds.h,
                                                           30 * config->dpi, 100,
                                                           nullptr,
                                                           0, true);
                               }, c, const_cast<char *>(__PRETTY_FUNCTION__));
        }
    }
}

static void
paint_option(AppClient *, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
//    bool keep_open = false;
//    if (container->children.size() >= 2)
//        keep_open = ((DataOfLabelButton *) container->children[1]->user_data)->text.find("...") != std::string::npos;
    if (container->state.mouse_hovering ||
        (container->real_bounds.h - (option_height * config->dpi) > 1)) {
        set_argb(cr, config->color_search_accent);
    } else {
        set_argb(cr, config->color_wifi_default_button);
    }
    cairo_fill(cr);
}

static void
option_entered(AppClient *client, cairo_t *, Container *container) {
//    container->wanted_bounds.h = actual_true_height(container);
    client_create_animation(app, client, &container->wanted_bounds.h, 0, 250, getEasingFunction(EaseOutElastic),
                            (option_height + (button_height * (container->children.size() - 1))) * config->dpi, true);
    client_layout(app, client);
}

static void
option_leaves(AppClient *client, cairo_t *, Container *container) {
    bool keep_open = false;
    for (auto *child: container->children) {
        auto *data = (DataOfLabelButton *) child->user_data;
        if (data->text.find("...") != std::string::npos || data->text.find("Cancel") != std::string::npos) {
            keep_open = true;
        }
    }
    
    if (!keep_open) {
        client_create_animation(app, client, &container->wanted_bounds.h, 0, 120, getEasingFunction(EaseInCirc),
                                container->children[0]->real_bounds.h, true);
        client_layout(app, client);
    }
    
    if (container->children.size() >= 3) {
        auto *data = (OptionButton *) container->children[2]->user_data;
        if (data->text.find("Are you sure?") != std::string::npos) {
            data->text = "Forget";
        }
    }
}

static void
paint_option_name(AppClient *, cairo_t *cr, Container *container) {
    auto *data = (OptionButton *) container->user_data;
    
    std::string subtitle = "Paired";
    Device *device = nullptr;
    for (auto *interface: bluetooth_interfaces) {
        if (interface->mac_address == data->option->mac_address) {
            device = (Device *) interface;
            if (!((Device *) interface)->paired) {
                subtitle = "Unpaired";
                break;
            }
            if (((Device *) interface)->connected) {
                subtitle = "Connected";
                
                std::string &percentage = ((Device *) interface)->percentage;
                if (!percentage.empty()) {
                    // If percentage has '.' in it remove everything that comes after it
                    if (percentage.find('.') != std::string::npos) {
                        percentage = percentage.substr(0, percentage.find('.'));
                    }
                    subtitle += ", Battery " + ((Device *) interface)->percentage + "%";
                }
                break;
            }
        }
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string title(data->option->text);
    bool show_mac = false;
    if (device) {
        if (device->paired) {
            if (container->parent->state.mouse_hovering ||
                (container->parent->real_bounds.h - (option_height * config->dpi) > 1)) {
                show_mac = true;
            }
            title = device->name;
        } else {
            if (!device->alias.empty()) {
                title = device->alias;
            } else {
                title = device->name;
            }
            subtitle = device->mac_address;
        }
    }
    pango_layout_set_text(layout, title.data(), title.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + (10 + 24 + 20) * config->dpi,
                  container->real_bounds.y + (option_height * config->dpi) / 2 - ((logical.height / PANGO_SCALE) / 2) -
                  (option_height / 4));
    pango_cairo_show_layout(cr, layout);
    
    ArgbColor watered_down = config->color_volume_text;
    watered_down.a = 0.5;
    set_argb(cr, watered_down);
    
    if (show_mac) {
        std::string mac = " (" + device->mac_address + ")";
        pango_layout_set_text(layout, mac.c_str(), mac.size());
        pango_layout_get_extents(layout, &ink, &logical);
        set_argb(cr, watered_down);
        cairo_move_to(cr,
                      container->real_bounds.x + (10 + 24 + 20) * config->dpi + (logical.width / PANGO_SCALE),
                      container->real_bounds.y + (option_height * config->dpi) / 2 -
                      ((logical.height / PANGO_SCALE) / 2) -
                      (option_height / 4));
        pango_cairo_show_layout(cr, layout);
    }
    
    layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, subtitle.data(), subtitle.length());
    pango_layout_get_extents(layout, &ink, &logical);
    cairo_move_to(cr,
                  container->real_bounds.x + (10 + 24 + 20) * config->dpi,
                  container->real_bounds.y + (option_height * config->dpi) / 2 - ((logical.height / PANGO_SCALE) / 2) +
                  (option_height / 4));
    pango_cairo_show_layout(cr, layout);
    
    if (data->option->icon) {
        cairo_set_source_surface(cr, data->option->icon, container->real_bounds.x + 10 * config->dpi,
                                 container->real_bounds.y + (option_height * config->dpi) / 2 -
                                 ((24 * config->dpi) / 2));
        cairo_paint(cr);
    }
}

static void
on_disconnect_response(BluetoothCallbackInfo *cci) {
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        blue_data->set_running(client, false);
        
        Container *con = nullptr;
        OptionButton *data = nullptr;
        if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
            for (auto c: scroll->content->children) {
                if (c->name == cci->mac_address) {
                    if (c->children.size() >= 2) {
                        con = c;
                        data = (OptionButton *) c->children[1]->user_data;
                        break;
                    }
                }
            }
        }
        
        if (cci->succeeded) {
            bar_message(client, "Device successfully disconnected", false);
            if (data) data->text = "Connect";
        } else {
            if (data) data->text = "Disconnect";
            bar_message(client, "Device disconnect failed\n" + cci->message, true);
        }
        
        if (con && !con->state.mouse_hovering)
            option_leaves(client, client->cr, con);
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
on_connect_response(BluetoothCallbackInfo *cci) {
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        blue_data->set_running(client, false);
        
        Container *con = nullptr;
        OptionButton *data = nullptr;
        if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
            for (auto c: scroll->content->children) {
                if (c->name == cci->mac_address) {
                    if (c->children.size() >= 2) {
                        con = c;
                        data = (OptionButton *) c->children[1]->user_data;
                        break;
                    }
                }
            }
        }
        
        if (cci->succeeded) {
            bar_message(client, "Device successfully connected", false);
            if (data) data->text = "Disconnect";
        } else {
            if (data) data->text = "Connect";
            bar_message(client, "Device connect failed\n" + cci->message, true);
        }
        
        if (con && !con->state.mouse_hovering)
            option_leaves(client, client->cr, con);
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
connect_clicked(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionButton *) container->user_data;
    auto *blue_option = data->option;
    auto *blue_data = (BlueData *) client->root->user_data;
    
    Device *device = nullptr;
    {
        for (auto &bluetooth_interface: bluetooth_interfaces) {
            if (bluetooth_interface->mac_address == blue_option->mac_address) {
                device = (Device *) bluetooth_interface;
                break;
            }
        }
    }
    
    if (data->text == "Connect" && device) {
        data->text = "Connecting...";
        blue_data->set_running(client, true);
        device->connect(on_connect_response);
    } else if (data->text == "Disconnect" && device) {
        data->text = "Disconnecting...";
        blue_data->set_running(client, true);
        device->disconnect(on_disconnect_response);
    }
}

static void
on_forget_response(BluetoothCallbackInfo *cci) {
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        blue_data->set_running(client, false);
        
        if (cci->succeeded) {
            bar_message(client, "Device successfully unpaired", false);
            
            if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
                for (int i = 0; i < scroll->content->children.size(); ++i) {
                    auto *c = scroll->content->children[i];
                    if (c->name == cci->mac_address) {
                        delete c;
                        scroll->content->children.erase(scroll->content->children.begin() + i);
                        break;
                    }
                }
            }
        } else {
            bar_message(client, "Device unpair failed\n" + cci->message, true);
            if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
                for (auto c: scroll->content->children) {
                    if (c->name == cci->mac_address) {
                        if (c->children.size() >= 2) {
                            c = c->children[2];
                            auto *data = (OptionButton *) c->user_data;
                            data->text = "Forget";
                            
                            if (!c->parent->state.mouse_hovering)
                                option_leaves(client, client->cr, c->parent);
                        }
                    }
                }
            }
        }
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
forget_clicked(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionButton *) container->user_data;
    auto *blue_option = data->option;
    auto *blue_data = (BlueData *) client->root->user_data;
    
    if (data->text == "Forget") {
        data->text = "Are you sure?";
    } else if (data->text == "Are you sure?") {
        data->text = "Forgetting...";
        Device *device = nullptr;
        {
            for (auto &bluetooth_interface: bluetooth_interfaces) {
                if (bluetooth_interface->mac_address == blue_option->mac_address) {
                    device = (Device *) bluetooth_interface;
                    break;
                }
            }
        }
        if (device) {
            blue_data->set_running(client, true);
            device->unpair(on_forget_response);
        }
    }
}

static void
on_pair_response(BluetoothCallbackInfo *cci) {
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        
        if (cci->succeeded) {
            bar_message(client, "Device successfully paired", false);
            
            blue_data->set_running(client, false);
            blue_data->showing_paired_devices = true;
            if (auto *connection_toggle_button = container_by_name("connection_toggle_button", client->root))
                ((DataOfLabelButton *) connection_toggle_button->user_data)->disabled = false;
            if (auto *scan_button = container_by_name("scan_button", client->root))
                ((DataOfLabelButton *) scan_button->user_data)->text = "Add Device";
            fill_devices(client);
            for (auto &bluetooth_interface: bluetooth_interfaces) {
                if (bluetooth_interface->type == BluetoothInterfaceType::Adapter) {
                    ((Adapter *) bluetooth_interface)->scan_off(nullptr);
                }
            }
        } else {
            bar_message(client, "Device pair failed\n" + cci->message, true);
            
            if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
                for (auto c: scroll->content->children) {
                    if (c->name == cci->mac_address) {
                        for (auto *child: c->children) {
                            auto *data = (OptionButton *) child->user_data;
                            if (data->text == "Cancel") {
                                data->text = "Pair";
                            }
                        }
                    }
                }
            }
        }
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
on_pair_cancel_response(BluetoothCallbackInfo *cci) {
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        
        if (cci->succeeded) {
            bar_message(client, "Paired successfully cancelled", false);
        } else {
            bar_message(client, "Cancel failed\n" + cci->message, true);
        }
        
        fill_devices(client);
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
pair_clicked(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionButton *) container->user_data;
    auto *blue_option = data->option;
    auto *blue_data = (BlueData *) client->root->user_data;
    
    Device *device = nullptr;
    {
        for (auto &bluetooth_interface: bluetooth_interfaces) {
            if (bluetooth_interface->mac_address == blue_option->mac_address) {
                device = (Device *) bluetooth_interface;
                break;
            }
        }
    }
    
    blue_data->set_running(client, true);
    if (data->text == "Pair" && device) {
        data->text = "Cancel";
        device->pair(on_pair_response);
    } else if (data->text == "Cancel" && device) {
        device->cancel_pair(on_pair_cancel_response);
    }
}

static void
remove_all_content(AppClient *client) {
    if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
        for (auto &option: scroll->content->children)
            delete option;
        scroll->content->children.clear();
    }
    client_layout(app, client);
    client_paint(app, client);
}

static void
fill_devices(AppClient *client) {
    auto *blue_data = (BlueData *) client->root->user_data;
    
    if (auto scroll = (ScrollContainer *) container_by_name("top", client->root)) {
        ((DataOfLabelButton *) scroll->user_data)->text = "No paired devices";
        
        std::vector<Device *> wanted_devices;
        for (const auto &interface: bluetooth_interfaces) {
            if (interface->type == BluetoothInterfaceType::Adapter) {
                auto *adapter = (Adapter *) interface;
                if (!adapter->powered) {
                    remove_all_content(client);
                    ((DataOfLabelButton *) scroll->user_data)->text = "Bluetooth is disabled";
                    return;
                } else {
                    continue;
                }
            }
            auto *device = (Device *) interface;
            if (device->paired && blue_data->showing_paired_devices) {
                wanted_devices.push_back(device);
            } else if (!device->paired && !blue_data->showing_paired_devices) {
                wanted_devices.push_back(device);
            }
        }
        
        // Remove and delete containers not found wanted_devices
        scroll->content->children.erase(
                std::remove_if(scroll->content->children.begin(), scroll->content->children.end(),
                               [&wanted_devices](Container *c) {
                                   for (auto &wanted_device: wanted_devices)
                                       if (wanted_device->mac_address == c->name)
                                           return false;
                                   delete c;
                                   return true;
                               }), scroll->content->children.end());
        
        // for wanted_devices not found in scroll->content->children, create a container for it
        for (auto &wanted_device: wanted_devices) {
            if (std::find_if(scroll->content->children.begin(), scroll->content->children.end(),
                             [&wanted_device](Container *c) {
                                 return c->name == wanted_device->mac_address;
                             }) == scroll->content->children.end()) {
                auto parent = scroll->content->child(::vbox, FILL_SPACE, option_height * config->dpi);
                parent->name = wanted_device->mac_address;
                auto blue_option = new BlueOption();
                blue_option->mac_address = wanted_device->mac_address;
                parent->user_data = blue_option;
                if (!wanted_device->alias.empty()) {
                    blue_option->text = wanted_device->alias + " (" + wanted_device->mac_address + ")";
                } else if (!wanted_device->name.empty()) {
                    blue_option->text = wanted_device->name + " (" + wanted_device->mac_address + ")";
                } else {
                    blue_option->text = wanted_device->mac_address;
                }
                parent->when_paint = paint_option;

//                parent->when_paint = paint_option_name;
                parent->receive_events_even_if_obstructed_by_one = true;
                parent->when_mouse_enters_container = option_entered;
                parent->when_mouse_leaves_container = option_leaves;
//                parent->clip = true;
                
                int size = 24 * config->dpi;
                if (!wanted_device->icon.empty() && has_options(wanted_device->icon)) {
                    std::string path;
                    std::vector<IconTarget> targets;
                    targets.emplace_back(wanted_device->icon);
                    search_icons(targets);
                    pick_best(targets, size);
                    path = targets[0].best_full_path;
                    if (!path.empty()) {
                        load_icon_full_path(app, client, &blue_option->icon, path, size);
                    } else {
                        goto load_default;
                    }
                } else {
                    // load bluetooth.svg from local_resources
                    load_default:
                    load_icon_full_path(app, client, &blue_option->icon, as_resource_path("bluetooth.svg"), size);
                }
                
                auto top = parent->child(::vbox, FILL_SPACE, option_height * config->dpi);
                auto *top_data = new OptionButton();
                top_data->option = blue_option;
                top->user_data = top_data;
                top->when_paint = paint_option_name;
                
                if (wanted_device->paired) {
                    auto connect = parent->child(::vbox, FILL_SPACE, FILL_SPACE);
                    auto *connect_data = new OptionButton();
                    if (wanted_device->connected) {
                        connect_data->text = "Disconnect";
                    } else {
                        connect_data->text = "Connect";
                    }
                    connect_data->option = blue_option;
                    connect->user_data = connect_data;
                    connect->when_paint = paint_option_button;
                    connect->when_clicked = connect_clicked;
    
                    auto forget = parent->child(::vbox, FILL_SPACE, FILL_SPACE);
                    auto *forget_data = new OptionButton();
                    forget_data->text = "Forget";
                    forget_data->option = blue_option;
                    forget->user_data = forget_data;
                    forget->when_paint = paint_option_button;
                    forget->when_clicked = forget_clicked;
                } else {
                    auto pair = parent->child(::vbox, FILL_SPACE, FILL_SPACE);
                    auto *pair_data = new OptionButton();
                    pair_data->text = "Pair";
                    pair_data->option = blue_option;
                    pair->user_data = pair_data;
                    pair->when_paint = paint_option_button;
                    pair->when_clicked = pair_clicked;
                }
            }
        }
    }
    client_layout(app, client);
    client_paint(app, client);
}

static void
on_any_property_changed() {
    // This is to handle the case where (in the background) some property of a device changes.
    // Like if the user disconnects a device somewhere other than our menu
    // bluetooth_interfaces already locked
    
    if (auto *client = client_by_name(app, "bluetooth_menu")) {
        if (auto *scroll = (ScrollContainer *) container_by_name("top", client->root)) {
            fill_devices(client);
            
            // If set disconnect/connect button text
            for (auto &interface: bluetooth_interfaces) {
                if (interface->type == BluetoothInterfaceType::Device) {
                    auto device = (Device *) interface;
                    
                    for (auto option: scroll->content->children) {
                        auto *blue_option = (BlueOption *) option->user_data;
                        if (blue_option->mac_address == interface->mac_address) {
                            for (auto &option_buttons: option->children) {
                                auto *option_button_data = (OptionButton *) option_buttons->user_data;
                                if (option_button_data->text.find("...") != std::string::npos)
                                    continue;
                                if (option_button_data->text == "Connect" ||
                                    option_button_data->text == "Disconnect") {
                                    if (device->connected) {
                                        option_button_data->text = "Disconnect";
                                    } else {
                                        option_button_data->text = "Connect";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        client_layout(app, client);
        client_paint(app, client);
    }
}

static void
power_on_result(BluetoothCallbackInfo *info) {
    if (auto client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        blue_data->blocking_command_running = false;
        blue_data->set_running(client, false);
        
        if (!info->succeeded) {
            bar_message(client, "Error turning on bluetooth\n" + info->message, true);
        } else {
            fill_devices(client);
        }
        
        if (auto *container = (Container *) container_by_name("connection_toggle_button", client->root)) {
            if (info->succeeded) {
                ((DataOfLabelButton *) container->user_data)->text = "Disable Bluetooth";
            } else {
                ((DataOfLabelButton *) container->user_data)->text = "Enable Bluetooth";
            }
        }
        if (auto *container = (Container *) container_by_name("scan_button", client->root)) {
            if (info->succeeded) {
                ((DataOfLabelButton *) container->user_data)->disabled = false;
            }
        }
    }
}

static void
power_off_result(BluetoothCallbackInfo *info) {
    if (auto client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        blue_data->blocking_command_running = false;
        blue_data->set_running(client, false);
        
        if (!info->succeeded) {
            bar_message(client, "Error turning off bluetooth\n" + info->message, true);
        } else {
            if (auto *container = (ScrollContainer *) container_by_name("top", client->root))
                ((DataOfLabelButton *) container->user_data)->text = "Bluetooth is disabled";
    
            remove_all_content(client);
        }
        
        if (auto *container = (Container *) container_by_name("connection_toggle_button", client->root)) {
            if (info->succeeded) {
                ((DataOfLabelButton *) container->user_data)->text = "Enable Bluetooth";
            } else {
                ((DataOfLabelButton *) container->user_data)->text = "Disable Bluetooth";
            }
        }
        if (auto *container = (Container *) container_by_name("scan_button", client->root)) {
            if (info->succeeded) {
                ((DataOfLabelButton *) container->user_data)->disabled = true;
            }
        }
    }
}

static void
scan_on_result(BluetoothCallbackInfo *info) {
    if (auto client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        
        if (!info->succeeded) {
            bar_message(client, "Error starting scan\n" + info->message, true);
            blue_data->set_running(client, false);
            blue_data->showing_paired_devices = true;
            fill_devices(client);
        } else {
            blue_data->showing_paired_devices = false;
            fill_devices(client);
            blue_data->set_running(client, true);
            if (auto *container = (Container *) container_by_name("scan_button", client->root))
                ((DataOfLabelButton *) container->user_data)->text = "Cancel";
        }
    }
}

static void
scan_off_result(BluetoothCallbackInfo *info) {
    if (auto client = client_by_name(app, "bluetooth_menu")) {
        auto *blue_data = (BlueData *) client->root->user_data;
        
        if (!info->succeeded) {
            bar_message(client, "Error stopping scan\n" + info->message, true);
            blue_data->showing_paired_devices = false;
            fill_devices(client);
        } else {
            blue_data->showing_paired_devices = true;
            fill_devices(client);
            blue_data->set_running(client, false);
            if (auto *container = (Container *) container_by_name("scan_button", client->root))
                ((DataOfLabelButton *) container->user_data)->text = "Add Device";
            if (auto *container = (Container *) container_by_name("connection_toggle_button", client->root))
                ((DataOfLabelButton *) container->user_data)->disabled = false;
        }
    }
}

static void
fill_root(AppClient *client) {
    bool powered = false;
    for (const auto &interface: bluetooth_interfaces) {
        if (interface->type == BluetoothInterfaceType::Adapter) {
            if (((Adapter *) interface)->powered) {
                powered = true;
                break;
            }
        }
    }
    
    auto root = client->root;
    for (const auto &item: root->children)
        delete item;
    root->children.clear();
    root->when_paint = paint_root;
    
    int padding = 8 * config->dpi;
    auto top_and_bottom = root->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    
    ScrollPaneSettings scroll_settings(config->dpi);
    auto top = top_and_bottom->scrollchild(scroll_settings);
    top->name = "top";
    top->content->wanted_pad = Bounds(padding * 2, padding * 2, padding * 2, padding * 2);
    top->content->spacing = padding;
    top->user_data = new DataOfLabelButton;
    top->when_paint = paint_temp_message;
    if (powered) {
        ((DataOfLabelButton *) top->user_data)->text = "No devices paired";
    } else {
        ((DataOfLabelButton *) top->user_data)->text = "Bluetooth is disabled";
    }
    
    auto error_bar = top_and_bottom->child(FILL_SPACE, 0);
    error_bar->name = "error_bar";
    error_bar->when_paint = paint_error_bar;
    error_bar->user_data = new BarMessageData;
    
    auto input_bar = top_and_bottom->child(FILL_SPACE, 0);
    input_bar->name = "input_bar";
    input_bar->when_paint = paint_input_bar;
    input_bar->clip = true;
    
    auto bottom = top_and_bottom->child(layout_type::hbox, FILL_SPACE,
                                        (button_height + padding * 2) * config->dpi);
    bottom->spacing = padding;
    bottom->wanted_pad = Bounds(padding, padding, padding, padding);
    
    Container *scan_button = bottom->child(FILL_SPACE, FILL_SPACE);
    scan_button->name = "scan_button";
    auto scan_data = new DataOfLabelButton;
    scan_data->text = "Add Device";
    scan_data->disabled = !powered;
    scan_button->user_data = scan_data;
    scan_button->when_paint = paint_centered_label;
    scan_button->when_clicked = [](AppClient *client, cairo_t *, Container *container) {
        auto *data = (DataOfLabelButton *) container->user_data;
        auto *blue_data = (BlueData *) client->root->user_data;
        if (data->disabled || blue_data->blocking_command_running)
            return;
        blue_data->set_running(client, true);
        
        if (data->text == "Add Device") {
            data->text = "Starting Scan...";
            for (const auto &interface: bluetooth_interfaces)
                if (interface->type == BluetoothInterfaceType::Adapter) {
                    ((Adapter *) interface)->scan_on(scan_on_result);
                    if (auto *blue = (Container *) container_by_name("connection_toggle_button", client->root))
                        ((DataOfLabelButton *) blue->user_data)->disabled = true;
                }
        } else if (data->text == "Cancel") {
            data->text = "Canceling...";
            for (const auto &interface: bluetooth_interfaces)
                if (interface->type == BluetoothInterfaceType::Adapter)
                    ((Adapter *) interface)->scan_off(scan_off_result);
        }
    };

//            bottom->child(FILL_SPACE, FILL_SPACE);
    
    Container *connection_toggle_button = bottom->child(FILL_SPACE, FILL_SPACE);
    connection_toggle_button->name = "connection_toggle_button";
    auto connection_toggle_data = new DataOfLabelButton;
    if (powered) {
        connection_toggle_data->text = "Disable Bluetooth";
    } else {
        connection_toggle_data->text = "Enable Bluetooth";
    }
    connection_toggle_button->user_data = connection_toggle_data;
    connection_toggle_button->when_paint = paint_centered_label;
    connection_toggle_button->when_clicked = [](AppClient *client, cairo_t *, Container *container) {
        auto *data = (DataOfLabelButton *) container->user_data;
        auto *blue_data = (BlueData *) client->root->user_data;
        if (data->disabled || blue_data->blocking_command_running)
            return;
        blue_data->blocking_command_running = true;
        blue_data->set_running(client, true);
        
        if (data->text == "Enable Bluetooth") {
            data->text = "Enabling...";
            for (const auto &interface: bluetooth_interfaces)
                if (interface->type == BluetoothInterfaceType::Adapter)
                    ((Adapter *) interface)->power_on(power_on_result);
        } else if (data->text == "Disable Bluetooth") {
            data->text = "Disabling...";
            for (const auto &interface: bluetooth_interfaces)
                if (interface->type == BluetoothInterfaceType::Adapter)
                    ((Adapter *) interface)->power_off(power_off_result);
        }
    };
    
    if (powered)
        fill_devices(client);
}

void open_bluetooth_menu() {
    on_any_bluetooth_property_changed = on_any_property_changed;
    bool bluetooth_service_running = false;
    for (const auto &running_dbus_service: running_dbus_services) {
        if (running_dbus_service == "org.bluez") {
            bluetooth_service_running = true;
        }
    }
    if (!bluetooth_service_running) // This shouldn't even possible because the icon shouldn't exist in the case that the service isn't running.
        return;
    
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.w = 376 * config->dpi;
    settings.h = 112 * 4 * config->dpi;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
//    settings.decorations = false;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "bluetooth_menu";
        popup_settings.ignore_scroll = true;
        popup_settings.takes_input_focus = true;
        auto client = taskbar->create_popup(popup_settings, settings);
        client->when_closed = [](AppClient *client) {
            for (const auto &interface: bluetooth_interfaces)
                if (interface->type == BluetoothInterfaceType::Adapter)
                    ((Adapter *) interface)->scan_off(scan_off_result);
        };
    
        Container *root = client->root;
        root->type = ::vbox;
        root->user_data = new BlueData;
        root->when_paint = paint_root;
    
        if (become_default_bluetooth_agent()) {
            fill_root(client);
        } else {
            root->when_paint = paint_message;
            ((BlueData *) root->user_data)->text = "Was not able to become the default bluetooth handler";
            ((BlueData *) root->user_data)->set_running(client, false);
        }
    
        client_show(app, client);
    }
}

void bluetooth_service_started() {
    register_agent_if_needed();
    if (auto client = client_by_name(app, "taskbar")) {
        if (auto c = container_by_name("bluetooth", client->root)) {
            c->exists = true;
        }
        client_layout(app, client);
        client_paint(app, client);
    }
}

void bluetooth_service_ended() {
    unregister_agent_if_needed();
    if (auto client = client_by_name(app, "taskbar")) {
        if (auto c = container_by_name("bluetooth", client->root)) {
            c->exists = false;
        }
        client_layout(app, client);
        client_paint(app, client);
    }
}

void reject_connection(NotificationInfo *ni) {
    if (ni->user_data == nullptr)
        return;
    ni->user_data = nullptr;
    
    auto br = (BluetoothRequest *) ni->user_data;
    DBusMessage *reply = dbus_message_new_error(br->message, "org.bluez.Error.Rejected", "Rejected");
    dbus_connection_send(br->connection, reply, NULL);
    dbus_message_unref(reply);
    delete br;
}

void establish_connection(NotificationInfo *ni) {
    if (ni->user_data == nullptr)
        return;
    ni->user_data = nullptr;
    
    auto br = (BluetoothRequest *) ni->user_data;
    DBusMessage *reply = dbus_message_new_method_return(br->message);
    dbus_connection_send(br->connection, reply, NULL);
    dbus_message_unref(reply);
    
    delete br;
}

static void
paint_left_label(AppClient *, cairo_t *cr, Container *container) {
    auto data = (DataOfLabelButton *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), -1);
    pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
    pango_layout_set_width(layout, (container->real_bounds.w) * PANGO_SCALE);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 13 * config->dpi,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2 + 2 * config->dpi);
    pango_cairo_show_layout(cr, layout);
}

static void
response_clicked_cancel(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *input_bar = container_by_name("input_bar", client->root)) {
        if (auto *data = (InputBarData *) input_bar->user_data) {
            if (data->br && data->br->message && data->br->connection) {
                // cancel reply
                DBusMessage *reply = dbus_message_new_error(data->br->message, "org.bluez.Error.Canceled", "Canceled");
                dbus_connection_send(data->br->connection, reply, NULL);
                dbus_message_unref(reply);
                
                delete data->br;
                input_bar->user_data = nullptr;
            }
        }
        
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
    }
}

static void
response_clicked_reject(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *input_bar = container_by_name("input_bar", client->root)) {
        if (auto *data = (InputBarData *) input_bar->user_data) {
            if (data->br && data->br->message && data->br->connection) {
                // reject reply
                DBusMessage *reply = dbus_message_new_error(data->br->message, "org.bluez.Error.Rejected", "Rejected");
                dbus_connection_send(data->br->connection, reply, NULL);
                dbus_message_unref(reply);
                
                delete data->br;
                input_bar->user_data = nullptr;
            }
        }
        
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
    }
}

static void
response_clicked_matches(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *input_bar = container_by_name("input_bar", client->root)) {
        if (auto *data = (InputBarData *) input_bar->user_data) {
            if (data->br && data->br->message && data->br->connection) {
                // empty reply
                DBusMessage *reply = dbus_message_new_method_return(data->br->message);
                dbus_connection_send(data->br->connection, reply, NULL);
                dbus_message_unref(reply);
                
                delete data->br;
                input_bar->user_data = nullptr;
            }
        }
        
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
    }
}

static void
response_clicked_submit_string(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *input_bar = container_by_name("input_bar", client->root)) {
        if (auto *data = (InputBarData *) input_bar->user_data) {
            if (data->br && data->br->message && data->br->connection) {
                if (auto *field = container_by_name("field", client->root)) {
                    // reply with string
                    auto *field_data = (FieldData *) field->user_data;
                    DBusMessage *reply = dbus_message_new_method_return(data->br->message);
                    dbus_message_append_args(reply, DBUS_TYPE_STRING, &field_data->text, DBUS_TYPE_INVALID);
                    dbus_connection_send(data->br->connection, reply, NULL);
                    dbus_message_unref(reply);
                }
                
                
                delete data->br;
                input_bar->user_data = nullptr;
            }
        }
        
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
    }
}

static void
response_clicked_submit_uint32(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *input_bar = container_by_name("input_bar", client->root)) {
        if (auto *data = (InputBarData *) input_bar->user_data) {
            if (data->br && data->br->message && data->br->connection) {
                if (auto *field = container_by_name("field", client->root)) {
                    // reply with string
                    auto *field_data = (FieldData *) field->user_data;
                    try {
                        uint32_t value = std::stoi(field_data->text);
                        DBusMessage *reply = dbus_message_new_method_return(data->br->message);
                        dbus_message_append_args(reply, DBUS_TYPE_UINT32, &value, DBUS_TYPE_INVALID);
                        dbus_connection_send(data->br->connection, reply, NULL);
                        dbus_message_unref(reply);
                    } catch (std::exception &e) {
                        // ignore
                    }
                }
                
                delete data->br;
                input_bar->user_data = nullptr;
            }
        }
        
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
    }
}

Container *make_label(std::string text, Container *parent) {
    auto label = parent->child(FILL_SPACE, 26 * config->dpi);
    auto data = new DataOfLabelButton;
    data->text = text;
    label->user_data = data;
    label->when_paint = paint_left_label;
    return label;
}

static void
paint_large_centered_label(AppClient *, cairo_t *cr, Container *container) {
    auto *data = (DataOfLabelButton *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 16 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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

Container *make_display_label(std::string text, Container *parent) {
    auto label = parent->child(FILL_SPACE, 37 * config->dpi);
    auto data = new DataOfLabelButton;
    data->text = std::move(text);
    label->user_data = data;
    label->when_paint = paint_large_centered_label;
    return label;
}

Container *make_button(std::string text, Container *parent) {
    auto button = parent->child(FILL_SPACE, (button_height * config->dpi));
    auto data = new DataOfLabelButton;
    data->text = std::move(text);
    button->user_data = data;
    button->when_paint = paint_centered_label;
    return button;
}

void bluetooth_wants_response_from_user(BluetoothRequest *br) {
    // Put the br_ptr on the working on container root user_data (BlueOption)
    // That way when cancel is pressed, we can reply with an error to the message.
    // It'll also reply with cancel and error when the user_data is deleted (window close)
    // It'll also have to be set to null when the user replies.
    if (br->type == "RequestAuthorization") {
        // When a device plugs in with a cable, it could want to connect with bluetooth.
        // We should send a notification to ourselves with actions that'll handle this.
        // The reply should be an empty reply if want to connect, otherwise, error message.
        auto *ni = new NotificationInfo;
        ni->summary = "Bluetooth";
        std::string mac = br->object_path;
        std::string name = br->object_path;
        for (auto *interface: bluetooth_interfaces) {
            if (interface->object_path == br->object_path) {
                mac = interface->mac_address;
                if (!interface->alias.empty())
                    name = interface->alias;
                else
                    name = interface->name;
                break;
            }
        }
        ni->body = "Establish connection with: " + name + " (" + mac + ")" + "?";
        ni->app_name = "Winbar";
        ni->app_icon = "bluetooth";
        ni->expire_timeout_in_milliseconds = 12000;
        ni->sent_by_winbar = true;
        ni->sent_to_action_center = true;
        ni->user_data = br;
        ni->on_ignore = reject_connection;
        ni->actions.push_back({"deny", "Allow", establish_connection});
        ni->actions.push_back({"accept", "Deny", reject_connection});
        show_notification(ni);
        return;
    }

#define CANCEL_EXIT \
    if (br->type != "DisplayPasskey" && br->type != "Cancelled") { \
        DBusMessage *reply = dbus_message_new_error(br->message, "org.bluez.Error.Canceled", "Canceled"); \
        dbus_connection_send(br->connection, reply, NULL); \
        dbus_message_unref(reply); \
    } \
    delete br; \
    return;
    
    AppClient *client = client_by_name(app, "bluetooth_menu");
    if (!client) {
        CANCEL_EXIT
    }
    auto *input_bar = container_by_name("input_bar", client->root);
    if (!input_bar) {
        CANCEL_EXIT
    }
    
    // If there's already a request, remove children on input_bar and reply with cancel.
    // Or if received a cancel, remove children on input_bar.
    if (input_bar->user_data != nullptr || br->type == "Cancelled") {
        for (auto *child: input_bar->children)
            delete child;
        input_bar->children.clear();
        
        if (input_bar->user_data != nullptr) {
            auto input_bar_data = (InputBarData *) input_bar->user_data;
            auto old_br = input_bar_data->br;
            
            // If cancellable old_br, cancel it
            if (old_br->type != "DisplayPasskey" && old_br->type != "Cancelled") {
                DBusMessage *reply = dbus_message_new_error(br->message, "org.bluez.Error.Canceled", "Canceled");
                dbus_connection_send(old_br->connection, reply, NULL);
                dbus_message_unref(reply);
            }
            
            delete old_br;
            input_bar->user_data = nullptr;
        }
        
        if (br->type == "Cancelled") {
            client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, 0, true);
            delete br;
            return;
        }
    }
    
    input_bar->user_data = new InputBarData(br);
    
    double target_height = 0;
    
    if (br->type == "RequestPinCode") {
        // Wants a string between 1-16 in length with numbers and characters.
        // Can reply with org.bluez.Error.Rejected or org.bluez.Error.Canceled
        // Label should be "Enter PIN: "
        auto label = make_label("Enter PIN:", input_bar);
        target_height += label->wanted_bounds.h;
        // TextField
        FieldSettings settings;
        settings.when_empty_text = "000000";
        auto field_parent = input_bar->child(FILL_SPACE, (button_height + 16) * config->dpi);
        field_parent->wanted_pad = Bounds(13 * config->dpi, 6 * config->dpi, 13 * config->dpi, 11 * config->dpi);
        auto field = make_textfield(field_parent, settings, FILL_SPACE, FILL_SPACE);
        field->name = "field";
        target_height += field_parent->wanted_bounds.h;
        // Button: Submit (delete br_ptr)
        auto submit = make_button("Submit", input_bar);
        submit->when_clicked = response_clicked_submit_string;
        target_height += submit->wanted_bounds.h;
        // Button: Cancel (delete br_ptr)
        auto cancel = make_button("Cancel", input_bar);
        cancel->when_clicked = response_clicked_cancel;
        target_height += submit->wanted_bounds.h;
    } else if (br->type == "DisplayPinCode") {
        // Show pin to user for authentication and then give empty reply when they pressed "Matches".
        // Can reply with org.bluez.Error.Rejected or org.bluez.Error.Canceled
        // Will call "Cancel" when no longer needed to be displayed.
        // Label should be "Does PIN match?: " + br_ptr->pin
        auto label = make_label("Does PIN match?", input_bar);
        target_height += label->wanted_bounds.h;
        // Label: PIN
        auto display_label = make_display_label(br->pin, input_bar);
        target_height += display_label->wanted_bounds.h;
        // Button: Matches (delete br_ptr)
        auto matches = make_button("Matches", input_bar);
        matches->when_clicked = response_clicked_matches;
        target_height += matches->wanted_bounds.h;
        // Button: Reject (delete br_ptr)
        auto reject = make_button("Reject", input_bar);
        reject->when_clicked = response_clicked_reject;
        target_height += reject->wanted_bounds.h;
    } else if (br->type == "RequestPasskey") {
        // Wants a number from to 0-999999 (only numbers) uint32
        // Can reply with org.bluez.Error.Rejected or org.bluez.Error.Canceled
        // Label should be "Enter Passkey: "
        auto label = make_label("Enter Passkey:", input_bar);
        target_height += label->wanted_bounds.h;
        // TextField
        FieldSettings settings;
        settings.when_empty_text = "000000";
        settings.only_numbers = true;
        auto field_parent = input_bar->child(FILL_SPACE, (button_height + 16) * config->dpi);
        field_parent->wanted_pad = Bounds(13 * config->dpi, 6 * config->dpi, 13 * config->dpi, 11 * config->dpi);
        auto field = make_textfield(field_parent, settings, FILL_SPACE, FILL_SPACE);
        field->name = "field";
        target_height += field_parent->wanted_bounds.h;
        // Button: Submit (delete br_ptr)
        auto submit = make_button("Submit", input_bar);
        submit->when_clicked = response_clicked_submit_uint32;
        target_height += submit->wanted_bounds.h;
        // Button: Cancel (delete br_ptr)
        auto cancel = make_button("Cancel", input_bar);
        cancel->when_clicked = response_clicked_cancel;
        target_height += submit->wanted_bounds.h;
    } else if (br->type == "DisplayPasskey") {
        // Show 6 digit (0 padded) pin to user for authentication. Just wants an empty reply. Will call "Cancel" when no longer needed to be displayed.
        // Label should be "Passkey: " + br_ptr->passkey
        auto label = make_label("Passkey:", input_bar);
        target_height += label->wanted_bounds.h;
        // Label: PIN
        auto display_label = make_display_label(br->pin, input_bar);
        target_height += display_label->wanted_bounds.h;
        // Button: Matches (delete br_ptr)
        auto matches = make_button("Matches", input_bar);
        matches->when_clicked = response_clicked_matches;
        target_height += matches->wanted_bounds.h;
    } else if (br->type == "RequestConfirmation") {
        // Show 6 digit (0 padded) pin to user for authentication. Wants an empty reply if user clicks matches
        // Otherwise, can reply with org.bluez.Error.Rejected or org.bluez.Error.Canceled
        // Label should be "Does Passkey match?: " + br_ptr->pin
        auto label = make_label("Does Passkey match?", input_bar);
        target_height += label->wanted_bounds.h;
        // Label: PIN
        auto display_label = make_display_label(br->pin, input_bar);
        target_height += display_label->wanted_bounds.h;
        // Button: Matches (delete br_ptr)
        auto matches = make_button("Matches", input_bar);
        matches->when_clicked = response_clicked_matches;
        target_height += matches->wanted_bounds.h;
        // Button: Reject (delete br_ptr)
        auto reject = make_button("Reject", input_bar);
        reject->when_clicked = response_clicked_reject;
        target_height += reject->wanted_bounds.h;
    }
    
    client_create_animation(app, client, &input_bar->wanted_bounds.h, 0, 100, nullptr, target_height, true);
}
    