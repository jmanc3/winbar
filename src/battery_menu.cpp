//
// Created by jmanc3 on 3/29/20.
//

#include "battery_menu.h"
#include "config.h"
#include "main.h"

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
            (int) (container->real_bounds.x + 12),
            (int) (container->real_bounds.y + container->real_bounds.h / 2 - image_height / 2));
    cairo_paint(cr);

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 34, PangoWeight::PANGO_WEIGHT_NORMAL);
    set_argb(cr, config->color_battery_text);
    std::string text = data->capacity + "%";
    pango_layout_set_text(layout, text.c_str(), text.length());

    int charge_width, charge_height;
    pango_layout_get_pixel_size(layout, &charge_width, &charge_height);

    int text_x = (int) (container->real_bounds.x + 72);
    int text_y =
            (int) (container->real_bounds.y + container->real_bounds.h / 2 - charge_height / 2) - 3;
    cairo_move_to(cr, text_x, text_y);

    pango_cairo_show_layout(cr, layout);

    layout = get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
    ArgbColor copy = config->color_battery_text;
    set_argb(cr, copy);
    text = "Status: " + data->status;
    pango_layout_set_text(layout, text.c_str(), text.length());

    int status_height;
    int status_width;
    pango_layout_get_pixel_size(layout, &status_width, &status_height);

    text_x = (int) (container->real_bounds.x + 72 + charge_width + 14);
    text_y = (int) (container->real_bounds.y + container->real_bounds.h / 2 - status_height / 2);
    cairo_move_to(cr, text_x, text_y);

    pango_cairo_show_layout(cr, layout);
}

static void
paint_title(AppClient *client_entity, cairo_t *cr, Container *container) {
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
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
            get_cached_pango_font(cr, config->font, 17, PangoWeight::PANGO_WEIGHT_NORMAL);

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
        backlight_set_brightness(amount);
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
    battery_bar->wanted_bounds.h = 77;
    battery_bar->when_paint = paint_battery_bar;
    auto *data = new data_battery_surfaces;
    for (int i = 0; i <= 9; i++) {
        auto *normal_surface = accelerated_surface(app, battery_entity, 40, 40);
        paint_surface_with_image(
                normal_surface,
                as_resource_path("battery/40/normal/" + std::to_string(i) + ".png"), 40,
                nullptr);
        data->normal_surfaces.push_back(normal_surface);

        auto *charging_surface = accelerated_surface(app, battery_entity, 40, 40);
        paint_surface_with_image(
                charging_surface,
                as_resource_path("battery/40/charging/" + std::to_string(i) + ".png"), 40,
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
    title->wanted_bounds.h = 23;

    auto hbox = vbox->child(FILL_SPACE, FILL_SPACE);
    hbox->type = ::hbox;

    auto brightness_icon = hbox->child(55, FILL_SPACE);
    brightness_icon->when_paint = paint_brightness_icon;
    auto *brightness_icon_data = new IconButton();
    brightness_icon_data->surface = accelerated_surface(app, battery_entity, 24, 24);
    paint_surface_with_image(
            brightness_icon_data->surface, as_resource_path("brightness.png"), 24, nullptr);
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

    auto brightness_amount = hbox->child(65, FILL_SPACE);
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

static bool first_expose = true;

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
    }
}

static bool
battery_menu_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_ungrab_pointer(app->connection, XCB_CURRENT_TIME);
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
        }
    }

    return true;
}

void start_battery_menu() {
    if (valid_client(app, battery_entity)) {
        client_close(app, battery_entity);
    }

    first_expose = true;
    Settings settings;
    settings.w = 361;
    settings.h = 164;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.popup = true;

    battery_entity = client_new(app, settings, "battery_menu");
    battery_entity->grab_event_handler = grab_event_handler;
    battery_entity->popup = true;

    client_add_handler(app, battery_entity, battery_menu_event_handler);

    fill_root(battery_entity->root);

    int brightness = backlight_get_brightness();
    if (brightness == -1) {
        marker_position_scalar = 1;
    } else {
        marker_position_scalar = (brightness) / 100.0;
    }

    client_show(app, battery_entity);
}