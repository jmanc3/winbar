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
#include <pango/pangocairo.h>
#include <xbacklight.h>

double marker_position_scalar = .5;

static AppClient *battery_entity = nullptr;

static void
paint_battery_bar(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto *data = static_cast<data_battery_surfaces *>(container->user_data);
    assert(data);
    assert(!data->normal_surfaces.empty());
    assert(!data->charging_surfaces.empty());
    
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
    
    int capacity_index = std::floor(((double) (std::stoi(data->capacity))) / 10.0);
    
    if (capacity_index > 9)
        capacity_index = 9;
    if (capacity_index < 0)
        capacity_index = 0;
    
    cairo_surface_t *surface;
    if (data->status == "Charging") {
        surface = data->charging_surfaces[capacity_index];
    } else {
        surface = data->normal_surfaces[capacity_index];
    }
    int image_height = cairo_image_surface_get_height(surface);
    dye_surface(surface, config->color_battery_icons);
    cairo_set_source_surface(
            cr,
            surface,
            (int) (container->real_bounds.x + 12 * config->dpi),
            (int) (container->real_bounds.y + container->real_bounds.h / 2 - image_height / 2));
    cairo_paint(cr);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 34 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    set_argb(cr, config->color_battery_text);
    std::string text = data->capacity + "%";
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int charge_width, charge_height;
    pango_layout_get_pixel_size(layout, &charge_width, &charge_height);
    
    int text_x = (int) (container->real_bounds.x + 72 * config->dpi);
    int text_y =
            (int) (container->real_bounds.y + container->real_bounds.h / 2 - charge_height / 2) - 3;
    cairo_move_to(cr, text_x, text_y);
    
    pango_cairo_show_layout(cr, layout);
    
    layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    ArgbColor copy = config->color_battery_text;
    set_argb(cr, copy);
    text = "Status: " + data->status;
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int status_height;
    int status_width;
    pango_layout_get_pixel_size(layout, &status_width, &status_height);
    
    text_x = (int) (container->real_bounds.x + 72 * config->dpi + charge_width + 14);
    text_y = (int) (container->real_bounds.y + container->real_bounds.h / 2 - status_height / 2);
    cairo_move_to(cr, text_x, text_y);
    
    pango_cairo_show_layout(cr, layout);
}

static void
paint_title(AppClient *client_entity, cairo_t *cr, Container *container) {
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    set_argb(cr, config->color_battery_text);
    std::string text = "Screen Brightness";
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int text_width, text_height;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);
    
    int text_x = (int) (container->real_bounds.x + 13);
    int text_y = (int) (container->real_bounds.y);
    cairo_move_to(cr, text_x, text_y);
    
    pango_cairo_show_layout(cr, layout);
}

static void
rounded_rect(cairo_t *cr, double corner_radius, double x, double y, double width, double height) {
    double radius = corner_radius;
    double degrees = M_PI / 180.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

static void
paint_brightness_amount(AppClient *client_entity, cairo_t *cr, Container *container) {
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 17 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string text = std::to_string((int) (marker_position_scalar * 100));
    
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_battery_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
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
    
    int amount = (int) (marker_position_scalar * 100);
    if (amount <= 0)
        amount = 1;
    if (real) {
        if (dbus_gnome_running()) {
            dbus_set_gnome_brightness(amount);
        } else if (dbus_get_kde_max_brightness() != 0) {
            dbus_kde_set_brightness(((double) amount) / 100.0);
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
    double marker_height = 24;
    double marker_width = 8;
    
    double line_height = 2;
    set_argb(cr, config->color_battery_slider_background);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    container->real_bounds.w,
                    line_height);
    cairo_fill(cr);
    
    set_argb(cr, config->color_battery_slider_foreground);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    (marker_position_scalar * container->real_bounds.w),
                    line_height);
    cairo_fill(cr);
    
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        set_argb(cr, config->color_battery_slider_active);
    } else {
        set_argb(cr, config->color_battery_slider_foreground);
    }
    
    rounded_rect(cr,
                 4,
                 container->real_bounds.x + (marker_position_scalar * container->real_bounds.w) -
                 marker_width / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - marker_height / 2,
                 marker_width,
                 marker_height);
    cairo_fill(cr);
}

static void
paint_root(AppClient *client_entity, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client_entity, config->color_battery_background));
    cairo_fill(cr);
}

static void
paint_brightness_icon(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;
    
    if (data->surface == nullptr)
        return;
    
    dye_surface(data->surface, config->color_battery_icons);
    
    cairo_set_source_surface(cr,
                             data->surface,
                             (int) (container->real_bounds.x + container->real_bounds.w / 2 -
                                    cairo_image_surface_get_width(data->surface) / 2),
                             (int) (container->real_bounds.y + container->real_bounds.h / 2 -
                                    cairo_image_surface_get_height(data->surface) / 2));
    cairo_paint(cr);
}

static Container *
make_battery_bar(Container *root) {
    auto battery_bar = new Container();
    battery_bar->parent = root;
    battery_bar->wanted_bounds.w = FILL_SPACE;
    battery_bar->wanted_bounds.h = 77 * config->dpi;
    battery_bar->when_paint = paint_battery_bar;
    auto *data = new data_battery_surfaces;
    for (int i = 0; i <= 9; i++) {
        auto *normal_surface = accelerated_surface(app, battery_entity, 40 * config->dpi, 40 * config->dpi);
        paint_surface_with_image(
                normal_surface,
                as_resource_path("battery/40/normal/" + std::to_string(i) + ".png"), 40 * config->dpi,
                nullptr);
        data->normal_surfaces.push_back(normal_surface);
        
        auto *charging_surface = accelerated_surface(app, battery_entity, 40 * config->dpi, 40 * config->dpi);
        paint_surface_with_image(
                charging_surface,
                as_resource_path("battery/40/charging/" + std::to_string(i) + ".png"), 40 * config->dpi,
                nullptr);
        data->charging_surfaces.push_back(charging_surface);
    }
    battery_bar->user_data = data;
    
    return battery_bar;
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
    auto *brightness_icon_data = new IconButton();
    brightness_icon_data->surface = accelerated_surface(app, battery_entity, 24 * config->dpi, 24 * config->dpi);
    paint_surface_with_image(
            brightness_icon_data->surface, as_resource_path("brightness.png"), 24 * config->dpi, nullptr);
    brightness_icon->user_data = brightness_icon_data;
    
    auto slider = new Container();
    slider->parent = hbox;
    hbox->children.push_back(slider);
    slider->when_paint = paint_slider;
    slider->when_mouse_down = drag_real;
    slider->when_drag_end = drag_real;
    slider->when_drag = drag_not_real;
    slider->when_drag_start = drag_not_real;
    slider->wanted_bounds.w = FILL_SPACE;
    slider->wanted_bounds.h = FILL_SPACE;
    
    auto brightness_amount = hbox->child(65 * config->dpi, FILL_SPACE);
    brightness_amount->when_paint = paint_brightness_amount;
    
    return vbox;
}

static void
fill_root(Container *root) {
    root->when_paint = paint_root;
    root->type = vbox;
    
    root->children.push_back(make_battery_bar(root));
    root->children.push_back(make_brightness_slider(root));
}

void start_battery_menu() {
    if (valid_client(app, battery_entity)) {
        client_close(app, battery_entity);
    }
    
    Settings settings;
    settings.w = 360 * config->dpi;
    settings.h = 164 * config->dpi;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "battery_menu";
        battery_entity = taskbar->create_popup(popup_settings, settings);
        fill_root(battery_entity->root);
        
        int brightness = backlight_get_brightness();
        if (dbus_gnome_running()) {
            brightness = dbus_get_gnome_brightness();
        } else if (dbus_get_kde_max_brightness() != 0)
            brightness = (dbus_get_kde_current_brightness() / dbus_get_kde_max_brightness()) * 100;
        if (brightness == -1) {
            marker_position_scalar = 1;
        } else {
            marker_position_scalar = (brightness) / 100.0;
        }
        
        client_show(app, battery_entity);
    }
}