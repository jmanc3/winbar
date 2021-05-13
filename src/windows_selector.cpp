
#include "windows_selector.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"

#include <pango/pangocairo.h>
#include <xcb/xcb_image.h>

int option_width = 217 * 1.2;
int option_height = 144 * 1.2;
static double close_width = 32;
static double close_height = 32;
static Container *data_container = nullptr;
static LaunchableButton *data = nullptr;
static AppClient *client = nullptr;

static void
fill_root(Container *root);

static void
window_option_clicked(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }

    xcb_window_t to_focus = data->windows_data_list[index]->id;

    xcb_ewmh_request_change_active_window(&app->ewmh,
                                          app->screen_number,
                                          to_focus,
                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                          XCB_CURRENT_TIME,
                                          XCB_NONE);

    client_close_threaded(app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
clicked_body(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent);
}

static void
clicked_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent->parent);
}

static void
window_option_closed(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }

    xcb_window_t to_close = data->windows_data_list[index]->id;

    unsigned long windows_count = data->windows_data_list.size();
    xcb_ewmh_request_close_window(&app->ewmh,
                                  app->screen_number,
                                  to_close,
                                  XCB_CURRENT_TIME,
                                  XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);


    if (windows_count > 1) {
        container->parent->children.erase(container->parent->children.begin() + index);
        int width = option_width * container->parent->children.size();
        delete container;

        double x = data_container->real_bounds.x - width / 2 + data_container->real_bounds.w / 2;
        if (x < 0) {
            x = 0;
        }
        double y = app->bounds.h - option_height - config->taskbar_height;
        double h = option_height;

        handle_configure_notify(app, client_entity, x, y, width, h);
        client_set_position_and_size(app, client_entity, x, y, width, h);
    } else {
        client_close_threaded(app, client_entity);
        app->grab_window = -1;
    }
}

static void
clicked_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_closed(client_entity, cr, container->parent->parent);
}

static void
paint_option_background(AppClient *client_entity, cairo_t *cr, Container *container) {
    bool hovered = false;
    bool pressed = false;
    Container *titlebar = container->children[0]->children[0];
    Container *close = container->children[0]->children[1];
    Container *body = container->children[1];
    if (titlebar->state.mouse_hovering || body->state.mouse_hovering ||
        close->state.mouse_hovering) {
        hovered = true;
    }
    if (titlebar->state.mouse_pressing || body->state.mouse_pressing ||
        body->state.mouse_pressing) {
        pressed = true;
    }

    if (hovered || pressed) {
        if (pressed) {
            set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_pressed_background));
        } else {
            set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_hovered_background));
        }
    } else {
        set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_default_background));
    }
    cairo_rectangle(cr,
                    container->real_bounds.x - 1,
                    container->real_bounds.y,
                    container->real_bounds.w + 1,
                    container->real_bounds.h);
    cairo_fill(cr);
}

static void
paint_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto *data = static_cast<IconButton *>(client->root->user_data);

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        ArgbColor color;
        if (container->state.mouse_pressing) {
            color = config->color_windows_selector_close_icon_pressed_background;
        } else {
            color = config->color_windows_selector_close_icon_hovered_background;
        }
        set_argb(cr, color);
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }

    bool active = container->state.mouse_pressing || container->state.mouse_hovering ||// US
                  container->parent->children[0]->state.mouse_pressing ||
                  container->parent->children[0]->state.mouse_hovering ||// TITLE
                  container->parent->parent->children[1]->state.mouse_pressing ||
                  container->parent->parent->children[1]->state.mouse_hovering;// BODY

    if (data->surface && active) {
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                dye_surface(data->surface, config->color_windows_selector_close_icon_pressed);
            } else {
                dye_surface(data->surface, config->color_windows_selector_close_icon_hovered);
            }
        } else {
            dye_surface(data->surface, config->color_windows_selector_close_icon);
        }
        double offset = (double) (32 - 16) / 2;
        cairo_set_source_surface(
                cr, data->surface, container->real_bounds.x + offset, container->real_bounds.y + offset);
        cairo_paint(cr);
    }
}

static void
paint_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    int width;
    int height;
    pango_layout_set_text(layout, data->class_name.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, config->color_windows_selector_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 10,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

class BodyData : public UserData {
public:
    WindowsData *windows_data = nullptr;
};

static void
paint_body(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = ((BodyData *) container->user_data)->windows_data;

    double pad = 10;
    double target_width = container->real_bounds.w - pad * 2;
    double target_height = container->real_bounds.h - pad;
    double scale_w = target_width / data->width;
    double scale_h = target_height / data->height;
    if (scale_w < scale_h) {
        scale_h = scale_w;
    } else {
        scale_w = scale_h;
    }

    long currrent_time = get_current_time_in_ms();
    if ((currrent_time - data->last_rescale_timestamp) > 1000) {
        if (screen_has_transparency(app)) {
            data->take_screenshot();
        }
        data->rescale(scale_w, scale_h);
    }
    if (data->scaled_thumbnail_surface) {
        double width = data->width * scale_w;
        double height = data->height * scale_h;

        double dest_x = container->real_bounds.x + pad + target_width / 2 - width / 2;
        double dest_y = container->real_bounds.y;

        cairo_save(cr);
        cairo_set_source_surface(cr, data->scaled_thumbnail_surface,
                                 dest_x, dest_y);
        cairo_rectangle(cr, dest_x, dest_y, width, height);
        cairo_clip(cr);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

static void
fill_root(Container *root) {
    IconButton *image = new IconButton;
    image->surface = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(image->surface, as_resource_path("taskbar-close.png"), 16, nullptr);
    root->user_data = image;


    if (screen_has_transparency(app)) {
        for (auto window_data : data->windows_data_list) {
            window_data->last_rescale_timestamp = -1;
        }
    }

    if (data == nullptr)
        return;

    for (int i = 0; i < data->windows_data_list.size(); i++) {
        Container *option_container = new Container();
        option_container->type = vbox;
        option_container->parent = root;
        option_container->wanted_bounds.w = FILL_SPACE;
        option_container->wanted_bounds.h = FILL_SPACE;
        option_container->when_paint = paint_option_background;
        root->children.push_back(option_container);

        Container *option_top_hbox = new Container();
        option_top_hbox->parent = option_container;
        option_top_hbox->wanted_bounds.w = FILL_SPACE;
        option_top_hbox->wanted_bounds.h = close_height;
        option_top_hbox->type = hbox;
        option_container->children.push_back(option_top_hbox);

        Container *option_titlebar = new Container();
        option_titlebar->parent = option_top_hbox;
        option_titlebar->wanted_bounds.w = FILL_SPACE;
        option_titlebar->wanted_bounds.h = FILL_SPACE;
        option_titlebar->when_paint = paint_titlebar;
        option_titlebar->when_clicked = clicked_titlebar;
        option_top_hbox->children.push_back(option_titlebar);

        Container *option_close_button = new Container();
        option_close_button->parent = option_top_hbox;
        option_close_button->wanted_bounds.w = close_width;
        option_close_button->wanted_bounds.h = FILL_SPACE;
        option_close_button->when_paint = paint_close;
        option_close_button->when_clicked = clicked_close;
        option_top_hbox->children.push_back(option_close_button);

        // Body
        Container *option_body = new Container();
        option_body->parent = option_container;
        option_body->wanted_bounds.w = FILL_SPACE;
        option_body->wanted_bounds.h = FILL_SPACE;
        option_body->when_paint = paint_body;
        option_body->when_clicked = clicked_body;
        auto body_data = new BodyData;
        body_data->windows_data = data->windows_data_list[i];
        option_body->user_data = body_data;
        option_container->children.push_back(option_body);
    }
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
windows_selector_event_handler(App *app, xcb_generic_event_t *event) {
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

void when_closed(AppClient *client) {
    data->window_selector_open = window_selector_state::CLOSED;
    if (auto c = client_by_name(app, "taskbar")) {
        if (!(data_container->state.mouse_hovering || data_container->state.mouse_pressing)) {
            if (data->hover_amount == 1) {
                client_create_animation(app, c, &data->hover_amount, 70, 0, 0);
            }
        }
    }
}

void start_windows_selector(Container *container, window_selector_state selector_state) {
    first_expose = true;
    data_container = container;
    data = static_cast<LaunchableButton *>(container->user_data);
    data->window_selector_open = selector_state;

    int width = option_width * data->windows_data_list.size();
    Settings settings;
    settings.x = container->real_bounds.x - width / 2 + data_container->real_bounds.w / 2;
    if (settings.x < 0) {
        settings.x = 0;
    }
    settings.y = app->bounds.h - option_height - config->taskbar_height;
    settings.w = width;
    settings.h = option_height;
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.popup = true;

    client = client_new(app, settings, "windows_selector");

    client->fps = 2;
    client->grab_event_handler = grab_event_handler;
    client->when_closed = when_closed;
    client_register_animation(app, client);

    app_create_custom_event_handler(app, client->window, windows_selector_event_handler);

    fill_root(client->root);

    client_show(app, client);
}