//
// Created by jmanc3 on 3/8/20.
//
#include "volume_menu.h"

#include "audio.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "components.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include <application.h>
#include <iostream>
#include <math.h>
#include <pango/pangocairo.h>
#include <utility.h>

static AppClient *client_entity;
static std::string connected_message = "";

enum option_type {
    client,
    output
};

static double opacity_diff = .5;

static double opacity_thresh = 200;

// TODO: every frame we should resize and remake containers based on data in audio_clients and
// audio_outputs since it can change
//  behind our hands and lead to a crash because we index into the list instead of doing something
//  smarter like have the actual index of the thing
// TODO: fix muting the output leading to a pause

class option_data : UserData {
public:
    int type = option_type::client;
    int index = 0;
    long last_update = get_current_time_in_ms();

    ~option_data() {}
};

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
fill_root(Container *root);

static void
paint_root(AppClient *client_entity, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client_entity, config->color_volume_background));
    cairo_fill(cr);

    if (!audio_connected) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);

        int width;
        int height;
        pango_layout_set_text(layout, connected_message.c_str(), -1);
        pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
        pango_layout_get_pixel_size(layout, &width, &height);

        set_argb(cr, config->color_volume_text);
        cairo_move_to(cr,
                      container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                      container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
        pango_cairo_show_layout(cr, layout);
    }
}

static void
paint_volume_icon(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    auto surfaces = static_cast<volume_surfaces *>(container->user_data);
    Audio *client = nullptr;
    if (data->type == option_type::client) {
        client = audio_clients[data->index];
    } else {
        client = audio_outputs[data->index];
    }

    if (surfaces->mute == nullptr || surfaces->high == nullptr || surfaces->low == nullptr ||
        surfaces->medium == nullptr)
        return;

    double scalar = ((double) client->volume.values[0]) / ((double) 65535);
    int val = (int) (scalar * 100);

    cairo_surface_t *surface = nullptr;
    bool mute_state;
    if (data->type == option_type::client) {
        mute_state = audio_clients[data->index]->mute_state;
    } else {
        mute_state = audio_outputs[data->index]->mute_state;
    }
    if (mute_state) {
        surface = surfaces->mute;
    } else if (val == 0) {
        surface = surfaces->none;
    } else if (val < 33) {
        surface = surfaces->low;
    } else if (val < 66) {
        surface = surfaces->medium;
    } else {
        surface = surfaces->high;
    }

    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        if (container->state.mouse_pressing) {
            dye_surface(surface, config->color_volume_pressed_icon);
        } else {
            dye_surface(surface, config->color_volume_hovered_icon);
        }
    } else {
        dye_surface(surface, config->color_volume_default_icon);
    }

    cairo_set_source_surface(cr,
                             surface,
                             (int) (container->real_bounds.x + container->real_bounds.w / 2 -
                                    cairo_image_surface_get_width(surface) / 2),
                             (int) (container->real_bounds.y + container->real_bounds.h / 2 -
                                    cairo_image_surface_get_height(surface) / 2));
    cairo_paint(cr);
}

static void
paint_volume_amount(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio *client = nullptr;
    if (data->type == option_type::client) {
        client = audio_clients[data->index];
    } else {
        client = audio_outputs[data->index];
    }
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 17, PangoWeight::PANGO_WEIGHT_NORMAL);

    double scalar = ((double) client->volume.values[0]) / ((double) 65535);

    std::string text = std::to_string((int) (scalar * 100));

    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_label(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->user_data);

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);

    std::string text;

    if (data->type == option_type::client) {
        auto client = audio_clients[data->index];
        text = client->application_name + ": " + client->client_name;
    } else {
        auto output = audio_outputs[data->index];
        text = output->output_description;
    }

    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr, container->real_bounds.x + 13, container->real_bounds.y + 12);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_option(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);

    double marker_height = 24;
    double marker_width = 8;
    double marker_position = 0;
    if (data->type == option_type::client) {
        auto client = audio_clients[data->index];
        marker_position =
                container->real_bounds.w * ((double) client->volume.values[0]) / ((double) 65535);
    } else {
        auto output = audio_outputs[data->index];
        marker_position =
                container->real_bounds.w * ((double) output->volume.values[0]) / ((double) 65535);
    }

    double line_height = 2;
    set_argb(cr, config->color_volume_slider_background);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    container->real_bounds.w,
                    line_height);
    cairo_fill(cr);
    set_argb(cr, config->color_volume_slider_foreground);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    marker_position,
                    line_height);
    cairo_fill(cr);

    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        set_argb(cr, config->color_volume_slider_active);
    } else {
        set_argb(cr, config->color_volume_slider_foreground);
    }

    rounded_rect(cr,
                 4,
                 container->real_bounds.x + marker_position - marker_width / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - marker_height / 2,
                 marker_width,
                 marker_height);
    cairo_fill(cr);
}

static void
toggle_mute(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio *client = nullptr;
    if (data->type == option_type::client) {
        client = audio_clients[data->index];
    } else {
        client = audio_outputs[data->index];
    }

    client->mute_state = !client->mute_state;
    if (data->type == option_type::client) {
        audio_set_client_mute(client->index, client->mute_state);
    } else {
        audio_set_output_mute(client->index, client->mute_state);
    }
    update_taskbar_volume_icon();
}

static void
scroll(AppClient *client_entity, cairo_t *cr, Container *container, int scroll_x, int scroll_y) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio *client = nullptr;
    if (data->type == option_type::client) {
        client = audio_clients[data->index];
    } else {
        client = audio_outputs[data->index];
    }
    pa_cvolume copy = client->volume;
    double val = client->volume.values[0];
    val += (655.35 * scroll_y) + (655.35 * -scroll_x);
    if (val < 0) {
        val = 0;
    } else if (val > 65535) {
        val = 65535;
    }
    client->volume.values[0] = (int) val;

    if (client->volume.values[0] < 0) {
        client->volume.values[0] = 0;
    } else if (client->volume.values[0] > 65535) {
        client->volume.values[0] = 65535;
    }
    if (copy.values[0] != client->volume.values[0]) {
        for (int i = 0; i < client->volume.channels; i++) {
            copy.values[i] = client->volume.values[0];
        }
        if (client->mute_state) {
            client->mute_state = !client->mute_state;
            if (data->type == option_type::client) {
                audio_set_client_mute(client->index, client->mute_state);
            } else {
                audio_set_output_mute(client->index, client->mute_state);
            }
        }
        if (data->type == option_type::client) {
            audio_set_client_volume(client->index, copy);
        } else {
            audio_set_output_volume(client->index, copy);
            update_taskbar_volume_icon();
        }
    }
}

static void
drag(AppClient *client_entity, cairo_t *cr, Container *container, bool force) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    if (!force) {
        long delta = get_current_time_in_ms() - data->last_update;
        if (delta < 10) {
            return;
        }
    }
    data->last_update = get_current_time_in_ms();
    Audio *client = nullptr;
    if (data->type == option_type::client) {
        client = audio_clients[data->index];
    } else {
        client = audio_outputs[data->index];
    }
    pa_cvolume copy = client->volume;

    // mouse_current_x and y are relative to the top left point of the window
    int limited_x = client_entity->mouse_current_x;

    if (limited_x < container->real_bounds.x) {
        limited_x = container->real_bounds.x;
    } else if (limited_x > container->real_bounds.x + container->real_bounds.w) {
        limited_x = container->real_bounds.x + container->real_bounds.w;
    }

    limited_x -= container->real_bounds.x;
    double scalar = limited_x / container->real_bounds.w;
    client->volume.values[0] = ((double) 65535) * scalar;

    if (client->volume.values[0] < 0) {
        client->volume.values[0] = 0;
    } else if (client->volume.values[0] > 65535) {
        client->volume.values[0] = 65535;
    }
    if (copy.values[0] != client->volume.values[0]) {
        for (int i = 0; i < client->volume.channels; i++) {
            copy.values[i] = client->volume.values[0];
        }
        if (client->mute_state) {
            client->mute_state = !client->mute_state;
            if (data->type == option_type::client) {
                audio_set_client_mute(client->index, client->mute_state);
            } else {
                audio_set_output_mute(client->index, client->mute_state);
            }
        }
        if (data->type == option_type::client) {
            audio_set_client_volume(client->index, copy);
        } else {
            audio_set_output_volume(client->index, copy);
            update_taskbar_volume_icon();
        }
    }
}

static void
drag_force(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, true);
}

static void
drag_whenever(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, false);
}

void fill_root(Container *root) {
    root->when_paint = paint_root;
    root->type = vbox;

    if (!audio_connected)
        return;

    for (int i = 0; i < audio_outputs.size(); i++) {
        auto vbox_container = new Container();
        vbox_container->type = vbox;
        vbox_container->wanted_bounds.h = 96;
        vbox_container->wanted_bounds.w = FILL_SPACE;
        auto data = new option_data();
        data->type = option_type::output;
        data->index = i;
        vbox_container->user_data = data;

        auto label = new Container();
        label->wanted_bounds.h = 34;
        label->wanted_bounds.w = FILL_SPACE;
        label->when_paint = paint_label;

        auto hbox_volume = new Container();
        hbox_volume->type = hbox;
        hbox_volume->wanted_bounds.h = FILL_SPACE;
        hbox_volume->wanted_bounds.w = FILL_SPACE;

        auto volume_icon = new Container();
        volume_icon->wanted_bounds.h = FILL_SPACE;
        volume_icon->wanted_bounds.w = 55;
        volume_icon->when_paint = paint_volume_icon;
        volume_icon->when_clicked = toggle_mute;
        volume_icon->when_scrolled = scroll;
        volume_icon->parent = hbox_volume;
        hbox_volume->children.push_back(volume_icon);
        auto surfaces = new volume_surfaces();
        surfaces->none = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->none, as_resource_path("audio/none24.png"), 24, nullptr);
        surfaces->low = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->low, as_resource_path("audio/low24.png"), 24, nullptr);
        surfaces->medium = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->medium, as_resource_path("audio/medium24.png"), 24, nullptr);
        surfaces->high = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->high, as_resource_path("audio/high24.png"), 24, nullptr);
        surfaces->mute = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->mute, as_resource_path("audio/mute24.png"), 24, nullptr);
        volume_icon->user_data = surfaces;
        dye_opacity(surfaces->none, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->low, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->medium, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->high, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->mute, opacity_diff, opacity_thresh);

        auto volume_bar = new Container();
        volume_bar->wanted_bounds.h = FILL_SPACE;
        volume_bar->wanted_bounds.w = FILL_SPACE;
        volume_bar->when_paint = paint_option;
        volume_bar->when_drag_start = drag_force;
        volume_bar->when_drag = drag_whenever;
        volume_bar->when_drag_end = drag_force;
        volume_bar->when_mouse_down = drag_force;
        volume_bar->when_mouse_up = drag_force;
        volume_bar->draggable = true;
        volume_bar->parent = hbox_volume;
        volume_bar->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_bar);

        auto volume_amount = new Container();
        volume_amount->wanted_bounds.h = FILL_SPACE;
        volume_amount->wanted_bounds.w = 65;
        volume_amount->when_paint = paint_volume_amount;
        volume_amount->parent = hbox_volume;
        volume_amount->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_amount);

        label->parent = vbox_container;
        vbox_container->children.push_back(label);

        hbox_volume->parent = vbox_container;
        vbox_container->children.push_back(hbox_volume);

        vbox_container->parent = root;
        root->children.push_back(vbox_container);
    }

    for (int i = 0; i < audio_clients.size(); i++) {
        auto vbox_container = new Container();
        vbox_container->type = vbox;
        vbox_container->wanted_bounds.h = 96;
        vbox_container->wanted_bounds.w = FILL_SPACE;
        auto data = new option_data();
        data->type = option_type::client;
        data->index = i;
        vbox_container->user_data = data;

        auto label = new Container();
        label->wanted_bounds.h = 34;
        label->wanted_bounds.w = FILL_SPACE;
        label->when_paint = paint_label;

        auto hbox_volume = new Container();
        hbox_volume->type = hbox;
        hbox_volume->wanted_bounds.h = FILL_SPACE;
        hbox_volume->wanted_bounds.w = FILL_SPACE;

        auto volume_icon = new Container();
        volume_icon->wanted_bounds.h = FILL_SPACE;
        volume_icon->wanted_bounds.w = 55;
        volume_icon->when_paint = paint_volume_icon;
        volume_icon->when_clicked = toggle_mute;
        volume_icon->when_scrolled = scroll;
        volume_icon->parent = hbox_volume;
        hbox_volume->children.push_back(volume_icon);
        auto surfaces = new volume_surfaces();
        surfaces->none = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->none, as_resource_path("audio/none24.png"), 24, nullptr);
        surfaces->low = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->low, as_resource_path("audio/low24.png"), 24, nullptr);
        surfaces->medium = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->medium, as_resource_path("audio/medium24.png"), 24, nullptr);
        surfaces->high = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->high, as_resource_path("audio/high24.png"), 24, nullptr);
        surfaces->mute = accelerated_surface(app, client_entity, 24, 24);
        paint_surface_with_image(surfaces->mute, as_resource_path("audio/mute24.png"), 24, nullptr);
        volume_icon->user_data = surfaces;
        dye_opacity(surfaces->none, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->low, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->medium, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->high, opacity_diff, opacity_thresh);
        dye_opacity(surfaces->mute, opacity_diff, opacity_thresh);

        auto volume_bar = new Container();
        volume_bar->wanted_bounds.h = FILL_SPACE;
        volume_bar->wanted_bounds.w = FILL_SPACE;
        volume_bar->when_paint = paint_option;
        volume_bar->when_drag_start = drag_force;
        volume_bar->when_drag = drag_whenever;
        volume_bar->when_drag_end = drag_force;
        volume_bar->when_mouse_down = drag_force;
        volume_bar->when_mouse_up = drag_force;
        volume_bar->draggable = true;
        volume_bar->parent = hbox_volume;
        volume_bar->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_bar);

        auto volume_amount = new Container();
        volume_amount->wanted_bounds.h = FILL_SPACE;
        volume_amount->wanted_bounds.w = 65;
        volume_amount->when_paint = paint_volume_amount;
        volume_amount->parent = hbox_volume;
        volume_amount->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_amount);

        label->parent = vbox_container;
        vbox_container->children.push_back(label);

        hbox_volume->parent = vbox_container;
        vbox_container->children.push_back(hbox_volume);

        vbox_container->parent = root;
        root->children.push_back(vbox_container);
    }
}

static bool first_expose = true;

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            // Ignore scroll button presses
            if (e->detail >= 4 && e->detail <= 7) {
                break;
            }
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("volume", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (IconButton *) co->user_data;
                            data->invalid_button_down = true;
                            data->timestamp = get_current_time_in_ms();
                        }
                    }
                }
            }
            break;
        }
    }
}

static bool
volume_menu_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
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
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
        }
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
    }

    return false;
}

void close_volume_menu() {}

void updates() {
    if (valid_client(app, client_entity)) {
        request_refresh(app, client_entity);
    }
}


static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_pressed_button;
            color.a = 1;
            set_argb(cr, color);
            cairo_fill(cr);
        } else {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_hovered_button;
            color.a = 1;
            set_argb(cr, color);
            cairo_fill(cr);
        }
    } else {
        set_rect(cr, container->real_bounds);
        ArgbColor color = config->color_apps_scrollbar_default_button;
        color.a = 1;
        set_argb(cr, color);
        cairo_fill(cr);
    }

    if (data->surface) {
        // TODO: cache the dye so we only do it once
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                dye_surface(data->surface, config->color_apps_scrollbar_pressed_button_icon);
            } else {
                dye_surface(data->surface, config->color_apps_scrollbar_hovered_button_icon);
            }
        } else {
            dye_surface(data->surface, config->color_apps_scrollbar_default_button_icon);
        }

        cairo_set_source_surface(
                cr, data->surface, container->real_bounds.x, container->real_bounds.y);
        cairo_paint_with_alpha(cr, 1);
    }
}

static void
paint_scroll_bg(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    set_rect(cr, container->real_bounds);
    ArgbColor color = config->color_apps_scrollbar_gutter;
    color.a = 1;
    set_argb(cr, color);
    cairo_fill(cr);
}

static void
paint_right_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_scroll_bg(client, cr, container);

    Container *scrollpane = container->parent->parent;

    auto right_bounds = right_thumb_bounds(scrollpane, container->real_bounds);

    right_bounds.x += right_bounds.w;
    right_bounds.w = std::max(right_bounds.w * 1, 2.0);
    right_bounds.x -= right_bounds.w;
    right_bounds.x -= 2 * (1 - 1);

    set_rect(cr, right_bounds);

    if (container->state.mouse_pressing) {
        ArgbColor color = config->color_apps_scrollbar_pressed_thumb;
        color.a = 1;
        set_argb(cr, color);
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        ArgbColor color = config->color_apps_scrollbar_hovered_thumb;
        color.a = 1;
        set_argb(cr, color);
    } else if (right_bounds.w == 2.0) {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        lighten(&color, 10);
        color.a = 1;
        set_argb(cr, color);
    } else {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        color.a = 1;
        set_argb(cr, color);
    }

    cairo_fill(cr);
}


void right_thumb_scrolled(AppClient *client,
                          cairo_t *cr,
                          Container *container,
                          int scroll_x,
                          int scroll_y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    /*
        auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
        auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);

        if (reply->mods & XKB_KEY_Shift_L || reply->mods & XKB_KEY_Control_L) {
            container->scroll_h_real += scroll_x * scroll_amount + scroll_y * scroll_amount;
        } else {
    */
    //}

}

void open_volume_menu() {
    if (audio_connected) {
        audio_all_clients();
        audio_all_outputs();

        if (audio_clients.empty() && audio_outputs.empty()) {
            // TODO: we need a variable to keep track of this state
            // audio_connected = false;
            connected_message = "Successfully established connection to PulseAudio but found no "
                                "clients or devices running";
        }

        audio_set_callback(updates);
    } else {
        connected_message = "Failed to establish connection to PulseAudio";
    }

    first_expose = true;

    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.w = 360;
    if (audio_connected) {
        unsigned long count = audio_clients.size() + audio_outputs.size();
        double d = app->bounds.h * .70 / 96;
        if (count < d) {
            settings.h = count * 96;
        } else {
            settings.w += 12;
            settings.h = d * 96;
        }
    } else {
        settings.h = 80;
    }
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.force_position = true;
    settings.popup = true;

    client_entity = client_new(app, settings, "volume");
    client_entity->grab_event_handler = grab_event_handler;

    app_create_custom_event_handler(app, client_entity->window, volume_menu_event_handler);

    Container *root = client_entity->root;
    ScrollPaneSettings s;
    s.right_width = 12;
    s.right_arrow_height = 12;
    Container *scrollpane = make_scrollpane(root, s);
    scrollpane->when_scrolled = nullptr;
    Container *content = scrollpane->child(::vbox, FILL_SPACE, FILL_SPACE);

    Container *right_thumb_container = scrollpane->parent->children[0]->children[1];
    right_thumb_container->parent->receive_events_even_if_obstructed_by_one = true;
    right_thumb_container->when_paint = paint_right_thumb;
    right_thumb_container->when_scrolled = right_thumb_scrolled;

    Container *top_arrow = scrollpane->parent->children[0]->children[0];
    top_arrow->when_paint = paint_arrow;
    auto *top_data = new IconButton;
    top_data->surface = accelerated_surface(app, client_entity, 12, 12);
    paint_surface_with_image(top_data->surface, as_resource_path("arrow-up-12.png"), 12, nullptr);

    top_arrow->user_data = top_data;
    Container *bottom_arrow = scrollpane->parent->children[0]->children[2];
    bottom_arrow->when_paint = paint_arrow;
    auto *bottom_data = new IconButton;
    bottom_data->surface = accelerated_surface(app, client_entity, 12, 12);
    paint_surface_with_image(bottom_data->surface, as_resource_path("arrow-down-12.png"), 12, nullptr);

    bottom_arrow->user_data = bottom_data;

    fill_root(content);
    if (audio_connected) {
        content->wanted_bounds.h = true_height(scrollpane) + true_height(content);
    } else {
        content->wanted_bounds.h = 80;
    }

    client_show(app, client_entity);
}
