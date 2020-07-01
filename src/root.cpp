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

    // for xcape to work, a client window must have input focus. if the root has focus, it won't
    // generate events therefore when we see that nothing or the root has focus we take it instead
    if (windows_count == 0 || windows[0] == app->screen->root || windows[0] == 0) {
        if (auto *client = client_by_name(app, "taskbar")) {
            // TODO: Alt tab on openbox sets the active to 0 so this will screw us, we should wait some time and check that its still zero after some time
            //xcb_set_input_focus(
            //app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
        }
    }
}

static bool
root_event_handler(App *app, xcb_generic_event_t *event) {
    switch (event->response_type & ~0x80) {
        case XCB_PROPERTY_NOTIFY: {
            auto *e = (xcb_property_notify_event_t *) event;

            if (e->atom == get_cached_atom(app, "_NET_CLIENT_LIST_STACKING")) {
                update_stacking_order();
            } else if (e->atom == get_cached_atom(app, "_NET_ACTIVE_WINDOW")) {
                update_active_window();
            }
            break;
        }
        case XCB_BUTTON_PRESS: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    handler->event_handler(app, event);
                    break;
                }
            }
            break;
        }
        case XCB_BUTTON_RELEASE: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    handler->event_handler(app, event);
                    break;
                }
            }
            break;
        }
        case XCB_MOTION_NOTIFY: {
            for (auto handler : app->handlers) {
                if (handler->target_window == app->grab_window) {
                    handler->event_handler(app, event);
                    break;
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
    bool already_open = client_by_name(app, "app_menu") != nullptr;
    if (!already_open)
        start_app_menu();
}

void root_start(App *app) {
    auto *handler = new Handler;
    handler->event_handler = root_event_handler;
    handler->target_window = app->screen->root;
    app->handlers.push_back(handler);

    const uint32_t values[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, app->screen->root, XCB_CW_EVENT_MASK, values);

    // If not on another thread, will block this one
    std::thread t(watch_meta_key);
    t.detach();

    xcb_flush(app->connection);
}
