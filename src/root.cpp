//
// Created by jmanc3 on 2/1/20.
//

#include "root.h"
#include "app_menu.h"
#include "application.h"
#include "bind_meta.h"
#include "components.h"
#include "main.h"
#include "taskbar.h"
#include "utility.h"
#include "config.h"

#include <mutex>

void update_stacking_order() {
    xcb_get_property_cookie_t cookie =
            xcb_get_property(app->connection,
                             0,
                             app->screen->root,
                             get_cached_atom(app, "_NET_CLIENT_LIST_STACKING"),
                             XCB_ATOM_WINDOW,
                             0,
                             -1);

    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, NULL);

    long windows_count = xcb_get_property_value_length(reply) / sizeof(xcb_window_t);
    auto *windows = (xcb_window_t *) xcb_get_property_value(reply);

    stacking_order_changed(windows, windows_count);
    free(reply);
}

void update_active_window() {
    xcb_get_property_cookie_t cookie = xcb_get_property(app->connection,
                                                        0,
                                                        app->screen->root,
                                                        get_cached_atom(app, "_NET_ACTIVE_WINDOW"),
                                                        XCB_ATOM_WINDOW,
                                                        0,
                                                        -1);

    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, NULL);

    long windows_count = xcb_get_property_value_length(reply) / sizeof(xcb_window_t);
    auto *windows = (xcb_window_t *) xcb_get_property_value(reply);

    active_window_changed(windows[0]);

    free(reply);
}

static bool
root_event_handler(App *app, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_PROPERTY_NOTIFY: {
            auto *e = (xcb_property_notify_event_t *) event;
            const xcb_get_atom_name_cookie_t &cookie = xcb_get_atom_name(app->connection, e->atom);
            xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, NULL);

//            char *name = xcb_get_atom_name_name(reply);
//            printf("ATOM: %s\n", name);

            if (e->atom == get_cached_atom(app, "_NET_CLIENT_LIST_STACKING")) {
                update_stacking_order();
            } else if (e->atom == get_cached_atom(app, "_NET_ACTIVE_WINDOW")) {
                update_active_window();
            } else if (e->atom == get_cached_atom(app, "_NET_WM_DESKTOP")) {
//                printf("here\n");
            }
            break;
        }
        case XCB_BUTTON_PRESS: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    auto *client = client_by_window(app, handler->target_window);
                    if (valid_client(app, client)) {
                        if (client->grab_event_handler) {
                            client->grab_event_handler(client, event);
                        }
                    }
                    break;
                }
            }
            break;
        }
        case XCB_BUTTON_RELEASE: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    auto *client = client_by_window(app, handler->target_window);
                    if (valid_client(app, client)) {
                        if (client->grab_event_handler) {
                            client->grab_event_handler(client, event);
                        }
                    }
                    break;
                }
            }
            break;
        }
        case XCB_MOTION_NOTIFY: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    auto *client = client_by_window(app, handler->target_window);
                    if (valid_client(app, client)) {
                        if (client->grab_event_handler) {
                            client->grab_event_handler(client, event);
                        }
                    }
                    break;
                }
            }
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            auto *e = (xcb_configure_notify_event_t *) event;
            app->bounds.w = e->width;
            app->bounds.h = e->height;
            if (auto taskbar = client_by_name(app, "taskbar")) {
                uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                      XCB_CONFIG_WINDOW_HEIGHT;
                uint32_t window_x = 0;
                uint32_t window_y = app->bounds.h - config->taskbar_height;
                uint32_t window_width = app->bounds.w;
                uint32_t window_height = config->taskbar_height;
                uint32_t value_list_resize[] = {window_x, window_y, window_width, window_height};
                auto configure_check = xcb_configure_window_checked(
                        app->connection, taskbar->window, value_mask, value_list_resize);
                auto configure_error = xcb_request_check(app->connection, configure_check);
                if (configure_error) {
                    printf("error: %d\n", configure_error->error_code);
                }
            }
            break;
        }
    }

    return true;
}

void meta_pressed() {
    // printf("open or close search app lister and set main_text_area active\n");
    std::lock_guard lock(app->clients_mutex);
    AppClient *client = client_by_name(app, "app_menu");
    bool already_open = client != nullptr;
    if (already_open) {
        client_close(app, client);
    } else {
        start_app_menu();
    }
}

void root_start(App *app) {
    auto *handler = new Handler;
    handler->event_handler = root_event_handler;
    handler->target_window = app->screen->root;
    app->handlers.push_back(handler);

    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, app->screen->root, XCB_CW_EVENT_MASK, values);

    // If not on another thread, will block this one
    std::thread t(watch_meta_key);
    t.detach();

    xcb_flush(app->connection);
}
