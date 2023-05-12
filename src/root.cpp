//
// Created by jmanc3 on 2/1/20.
//

#include "root.h"
#include "app_menu.h"
#include "application.h"
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
root_event_handler(App *app, xcb_generic_event_t *event, xcb_window_t) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_PROPERTY_NOTIFY: {
            auto *e = (xcb_property_notify_event_t *) event;
//            const xcb_get_atom_name_cookie_t &cookie = xcb_get_atom_name(app->connection, e->atom);
//            xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, NULL);
//            char *name = xcb_get_atom_name_name(reply);
//            printf("ATOM: %s\n", name);
            
            if (e->atom == get_cached_atom(app, "_NET_CLIENT_LIST_STACKING")) {
                update_stacking_order();
            }
            if (e->atom == get_cached_atom(app, "_NET_WM_DESKTOP")) {
//                printf("here\n");
            }
            update_active_window();
            break;
        }
        case XCB_BUTTON_PRESS: {
            break;
        }
        case XCB_BUTTON_RELEASE: {
            break;
        }
        case XCB_MOTION_NOTIFY: {
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            auto *e = (xcb_configure_notify_event_t *) event;
            app->bounds.w = e->width;
            app->bounds.h = e->height;
            break;
        }
    }
    
    return false;
}

void meta_pressed(unsigned int num) {
    if (num == 0) {
        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
        
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
        
        if (auto client = client_by_name(app, "app_menu")) {
            client_close(app, client);
            set_textarea_inactive();
        } else {
            start_app_menu();
        }
    } else {
        taskbar_launch_index(num - 10);
    }
}

void root_start(App *app) {
    auto *handler = new Handler;
    handler->event_handler = root_event_handler;
    handler->target_window = app->screen->root;
    app->handlers.push_back(handler);
    
    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, app->screen->root, XCB_CW_EVENT_MASK, values);
    
    xcb_flush(app->connection);
}
