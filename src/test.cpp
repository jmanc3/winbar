//
// Created by jmanc3 on 7/7/20.
//

#include "test.h"
#include "main.h"
#include <application.h>
#include <utility.h>
#include "hsluv.h"
#include "simple_dbus.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include <dbus/dbus.h>
#include <iostream>

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    
    set_argb(cr, ArgbColor(1, 0, 0, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    
    set_argb(cr, ArgbColor(0, .7, 0, 1));
    cairo_rectangle(cr, 10, 10, 100, 100);
    cairo_fill(cr);
    
    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR);
    
    // draw square
    set_argb(cr, ArgbColor(0, 1, 0, 1));
    cairo_rectangle(cr, 10 + 50, 10 + 50, 100, 100);
    cairo_fill(cr);
    
    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

static void function(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    const char *dbus_result = nullptr;
    
    if (!::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_STRING, &dbus_result, DBUS_TYPE_INVALID)) {
        ::dbus_message_unref(dbus_reply);
    } else {
        std::cout << "Connected to D-Bus as \"" << ::dbus_bus_get_unique_name(dbus_connection) << "\"."
                  << std::endl;
        std::cout << "Introspection Result:" << std::endl << std::endl;
        std::cout << dbus_result << std::endl;
        ::dbus_message_unref(dbus_reply);
    }
}

void start_test_window() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Settings settings;
    settings.w = 1200;
    AppClient *client = client_new(app, settings, "test");
    client->root->when_paint = paint_root;
    client_show(app, client);
    
    if (dbus_connection) {
        DBusMessage *dbus_msg = nullptr;
        DBusPendingCall *pending;
        
        dbus_msg = ::dbus_message_new_method_call("org.freedesktop.DBus", "/", "org.freedesktop.DBus.Introspectable",
                                                  "Introspect");
        dbus_bool_t r = ::dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                                          DBUS_TIMEOUT_USE_DEFAULT);
        dbus_bool_t x = ::dbus_pending_call_set_notify(pending, function, nullptr, nullptr);
        ::dbus_message_unref(dbus_msg);
        ::dbus_pending_call_unref(pending);
    }
}