//
// Created by jmanc3 on 6/2/21.
//

#include "action_center_menu.h"
#include "application.h"
#include "config.h"
#include "taskbar.h"
#include "main.h"

static void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_volume_background);
    cairo_fill(cr);
}

static void fill_root(AppClient *client, Container *root) {
    root->when_paint = paint_root;
}

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);

            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("action", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (ActionCenterButtonData *) co->user_data;
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
event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            xcb_set_input_focus(app->connection, XCB_NONE, e->window, XCB_CURRENT_TIME);
            xcb_flush(app->connection);
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
            }
            break;
        }
    }

    return false;
}

void start_action_center(App *app) {
    Settings settings;
    settings.w = 396;
    settings.h = app->bounds.h - config->taskbar_height;
    settings.x = app->bounds.x + app->bounds.w - settings.w;
    settings.y = 0;
    settings.popup = true;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;

    auto client = client_new(app, settings, "action_center");
    fill_root(client, client->root);
    client->grab_event_handler = grab_event_handler;
    app_create_custom_event_handler(app, client->window, event_handler);

    client_show(app, client);
}