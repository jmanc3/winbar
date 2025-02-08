//
// Created by jmanc3 on 3/29/20.
//

#include "battery_menu.h"
#include "config.h"
#include "main.h"
#include "simple_dbus.h"

#include <application.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <math.h>
#include <xbacklight.h>

double marker_position_scalar = 1;

static AppClient *battery_entity = nullptr;

static int brightness_fake = 100;

void set_brightness(double new_brightness) {
    int amount = (int) std::round(new_brightness * 100);
    if (amount <= 0)
        amount = 1;
    if (dbus_kde_running()) {
        dbus_kde_set_brightness(((double) amount) / 100.0);
    } else if (dbus_gnome_running()) {
        dbus_set_gnome_brightness(amount);
    } else {
        backlight_set_brightness(amount);
    }

//    get_brightness_and_update_visually(app, client_by_name(app, "battery_menu"), nullptr, nullptr);
}

void set_brightness_visual(double new_brightness) {
    marker_position_scalar = new_brightness;
    brightness_fake = (int) std::round(new_brightness * 100);
    client_paint(app, client_by_name(app, "battery_menu"));
}

static void
paint_battery_bar(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = static_cast<BatteryInfo *>(container->user_data);
    assert(data);
    
    std::string line;
    std::ifstream status("/sys/class/power_supply/BAT0/status");
    if (status.is_open()) {
        if (getline(status, line)) {
            data->status = line;
        }
        status.close();
    } else {
        data->status = "Missing BAT0";
    }
    
    std::ifstream capacity("/sys/class/power_supply/BAT0/capacity");
    if (capacity.is_open()) {
        if (getline(capacity, line)) {
            data->capacity = line;
        }
        capacity.close();
    } else {
        data->capacity = "0";
    }
    
    float i = std::stoi(data->capacity);
    if (i == 5)
        i = 4;
    int rounded = std::round(i / 10) * 10;
    int capacity_index = std::floor(((double) (rounded)) / 10.0);
    
    if (capacity_index > 10)
        capacity_index = 10;
    if (capacity_index < 0)
        capacity_index = 0;
    
    data->capacity_index = capacity_index;
    
    std::string regular[] = {"\uEBA0", "\uEBA1", "\uEBA2", "\uEBA3", "\uEBA4", "\uEBA5", "\uEBA6", "\uEBA7", "\uEBA8",
                             "\uEBA9", "\uEBAA"};
    
    std::string charging[] = {"\uEBAB", "\uEBAC", "\uEBAD", "\uEBAE", "\uEBAF", "\uEBB0", "\uEBB1", "\uEBB2", "\uEBB3",
                              "\uEBB4", "\uEBB5"};
    
    auto text = regular[data->capacity_index];
    if (data->status == "Full") {
        text = regular[10];
    } else if (data->status == "Charging") {
        text = charging[data->capacity_index];
    }
        
    auto [f, w, h] = draw_text_begin(client, 32 * config->dpi, config->icons, EXPAND(config->color_taskbar_button_icons), text);
    f->draw_text_end((int) (container->real_bounds.x + 12 * config->dpi), (int) (MIDY(container) - h / 2));
    
    text = data->capacity + "%";
    auto ff = draw_text_begin(client, 34 * config->dpi, config->font, EXPAND(config->color_battery_text), text);
    int text_x = (int) (container->real_bounds.x + 90 * config->dpi);
    int text_y = (int) (MIDY(container) - ff.h / 2) - 3;
    ff.f->draw_text_end(text_x, text_y);
        
    text = "Status: " + data->status;
    ff = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_battery_text), text);
    text_x = (int) (container->real_bounds.x + 90 * config->dpi + ff.w + 10 * config->dpi);
    text_y = (int) (MIDY(container) - ff.h / 2);
    ff.f->draw_text_end(text_x, text_y);
}

static void
paint_title(AppClient *client_entity, cairo_t *cr, Container *container) {
    draw_text(client_entity, 10 * config->dpi, config->font, EXPAND(config->color_battery_text), "Screen Brightness", container->real_bounds, 5, 13, 0);
}

static void
paint_brightness_amount(AppClient *client_entity, cairo_t *cr, Container *container) {
    std::string text = std::to_string((int) (brightness_fake));
    if (((int) brightness_fake) == -1)
        text = "100";
    draw_text(client_entity, 17 * config->dpi, config->font, EXPAND(config->color_battery_text), text, container->real_bounds);
}

static void
drag(AppClient *client_entity, cairo_t *cr, Container *container, bool real) {
    // mouse_current_x and y are relative to the top left point of the window
    int limited_x = client_entity->mouse_current_x;
    
    if (limited_x < container->real_bounds.x) {
        limited_x = container->real_bounds.x;
    } else if (limited_x > container->real_bounds.x + container->real_bounds.w) {
        limited_x = container->real_bounds.x + container->real_bounds.w;
    }
    
    limited_x -= container->real_bounds.x;
    marker_position_scalar = limited_x / container->real_bounds.w;
    
    int amount = (int) std::round(marker_position_scalar * 100);
    if (amount <= 0)
        amount = 1;
    if (real) {
        brightness_fake = amount;
        if (dbus_kde_running()) {
            dbus_kde_set_brightness(((double) amount) / 100.0);
        } else if (dbus_gnome_running()) {
            dbus_set_gnome_brightness(amount);
        } else {
            backlight_set_brightness(amount);
        }
    }
}

static void
drag_real(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, true);
}

static void
drag_not_real(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, false);
}

static void
paint_slider(AppClient *client_entity, cairo_t *cr, Container *container) {
    double marker_height = 24 * config->dpi;
    double marker_width = 8 * config->dpi;
    
    double line_height = 2 * config->dpi;
    draw_colored_rect(client_entity, config->color_battery_slider_background, Bounds(container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    container->real_bounds.w,
                    line_height));
    
    draw_colored_rect(client_entity, config->color_battery_slider_foreground, Bounds(container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    (marker_position_scalar * container->real_bounds.w),
                    line_height));
    
    auto color = config->color_battery_slider_foreground;
    if ((container->state.mouse_pressing || container->state.mouse_hovering))
        color = config->color_battery_slider_active;
    rounded_rect(client_entity,
                 4 * config->dpi,
                 container->real_bounds.x + (marker_position_scalar * container->real_bounds.w) -
                 marker_width / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - marker_height / 2,
                 marker_width,
                 marker_height, color);
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_battery_background), container->real_bounds);
}

static void
paint_brightness_icon(AppClient *client_entity, cairo_t *cr, Container *container) {
    draw_text(client_entity, 24 * config->dpi, config->icons, EXPAND(config->color_battery_icons), "\uEC8A", container->real_bounds);
}

static Container *
make_battery_bar(Container *root) {
    auto battery_bar = new Container();
    battery_bar->parent = root;
    battery_bar->wanted_bounds.w = FILL_SPACE;
    battery_bar->wanted_bounds.h = 77 * config->dpi;
    battery_bar->when_paint = paint_battery_bar;
    auto *data = new BatteryInfo;
    battery_bar->user_data = data;
    
    return battery_bar;
}

static void
scroll(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y, bool came_from_touchpad) {
    double current_brightness = marker_position_scalar; // scalar: 0-1
    
    double new_brightness = current_brightness;
    
    static double cached_current = current_brightness;
    
    if (came_from_touchpad) {
        new_brightness = ((cached_current * 2400) + scroll_y) / 2400;
        cached_current = new_brightness;
        cached_current = cached_current < 0 ? 0 : cached_current > 1 ? 1 : cached_current;
    } else {
        if (scroll_y > 0) {
            new_brightness += .05;
        } else if (scroll_y < 0) {
            new_brightness -= .05;
        }
    }
    
    new_brightness = new_brightness < 0 ? 0 : new_brightness > 1 ? 1 : new_brightness;
    if (new_brightness != current_brightness) {
        set_brightness_visual(new_brightness);
        
        // Set the brightness but only every 500ms
        static int file_descriptor = -1000000;
        
        auto *new_brigtness = new double;
        *new_brigtness = new_brightness;
        
        if (file_descriptor == -1000000) {
            Timeout *timeout = app_timeout_create(app, client, 200,
                                                  [](App *, AppClient *, Timeout *, void *user_data) {
                                                      file_descriptor = -1000000;
                                                      auto *brightness = (double *) user_data;
                                                      set_brightness_visual(*brightness);
                                                      set_brightness(*brightness);
                                                      delete brightness;
                                                  }, new_brigtness, const_cast<char *>(__PRETTY_FUNCTION__));
            file_descriptor = timeout->file_descriptor;
        } else {            // check if file_descriptor exists
            for (auto timeout: app->timeouts) {
                if (timeout->file_descriptor == file_descriptor) {
                    delete (double *) timeout->user_data;
                    
                    timeout->user_data = new_brigtness;
                    return;
                }
            }
            
            Timeout *timeout = app_timeout_create(app, client, 200,
                                                  [](App *, AppClient *, Timeout *, void *user_data) {
                                                      file_descriptor = -1000000;
                                                      auto *brightness = (double *) user_data;
                                                      set_brightness_visual(*brightness);
                                                      set_brightness(*brightness);
                                                      delete brightness;
                                                  }, new_brigtness, const_cast<char *>(__PRETTY_FUNCTION__));
            file_descriptor = timeout->file_descriptor;
        }
    }
}

void
adjust_brightness_based_on_fine_scroll(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y,
                                       bool came_from_touchpad) {
    scroll(client, cr, container, scroll_x, scroll_y, came_from_touchpad);
}

static Container *
make_brightness_slider(Container *root) {
    auto vbox = new Container();
    vbox->type = ::vbox;
    vbox->parent = root;
    vbox->wanted_bounds.w = FILL_SPACE;
    vbox->wanted_bounds.h = FILL_SPACE;
    
    auto title = new Container();
    title->parent = vbox;
    vbox->children.push_back(title);
    title->when_paint = paint_title;
    title->wanted_bounds.w = FILL_SPACE;
    title->wanted_bounds.h = 23 * config->dpi;
    
    auto hbox = vbox->child(FILL_SPACE, FILL_SPACE);
    hbox->type = ::hbox;
    
    auto brightness_icon = hbox->child(55 * config->dpi, FILL_SPACE);
    brightness_icon->when_paint = paint_brightness_icon;
    brightness_icon->when_fine_scrolled = scroll;
    
    auto slider = new Container();
    slider->parent = hbox;
    hbox->children.push_back(slider);
    slider->when_paint = paint_slider;
    slider->when_fine_scrolled = scroll;
    slider->when_mouse_down = drag_real;
    slider->when_drag_end = drag_real;
    slider->when_drag = drag_not_real;
    slider->when_drag_start = drag_not_real;
    slider->wanted_bounds.w = FILL_SPACE;
    slider->wanted_bounds.h = FILL_SPACE;
    
    auto brightness_amount = hbox->child(65 * config->dpi, FILL_SPACE);
    brightness_amount->when_paint = paint_brightness_amount;
    brightness_amount->when_fine_scrolled = scroll;
    
    return vbox;
}

static void
fill_root(Container *root) {
    root->when_paint = paint_root;
    root->type = vbox;
    
    root->children.push_back(make_battery_bar(root));
    root->children.push_back(make_brightness_slider(root));
}

static void get_brightness_and_update_visually(App *app, AppClient *client, Timeout *, void *) {
    int brightness;
    
    if (dbus_kde_running()) {
        brightness = (int) std::round((dbus_get_kde_current_brightness() / dbus_get_kde_max_brightness()) * 100);
    } else if (dbus_gnome_running()) {
        brightness = (int) std::round(dbus_get_gnome_brightness());
    } else {
        brightness = backlight_get_brightness();
    }
    
    if (brightness == -1) {
        marker_position_scalar = 1;
        brightness_fake = brightness;
    } else {
        marker_position_scalar = (brightness) / 100.0;
        brightness_fake = brightness;
    }
    if (client) {
        client_paint(app, client, true);
    }
}

void start_battery_menu() {
    static int loop = 0;
    if (loop == 0)
        get_brightness_and_update_visually(nullptr, nullptr, nullptr, nullptr);
    loop++;
    if (valid_client(app, battery_entity)) {
        client_close(app, battery_entity);
    }
    
    Settings settings;
    settings.w = 360 * config->dpi;
    settings.h = 164 * config->dpi;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.slide_data[1] = 3;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *container = container_by_name("battery", taskbar->root);
        if (container->real_bounds.x > taskbar->bounds->w / 2) {
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        } else {
            settings.x = 0;
            settings.slide_data[1] = 0;
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "battery_menu";
        popup_settings.ignore_scroll = true;
        battery_entity = taskbar->create_popup(popup_settings, settings);
        fill_root(battery_entity->root);
        
        app_timeout_create(app, battery_entity, 0, get_brightness_and_update_visually, nullptr,
                           const_cast<char *>(__PRETTY_FUNCTION__));
        
        client_show(app, battery_entity);
    }
}
