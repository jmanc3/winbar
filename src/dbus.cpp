//
// Created by jmanc3 on 5/30/21.
//

#include "dbus.h"
#include "application.h"

#include <dbus/dbus.h>
#include <cstring>
#include <iostream>

std::vector<std::string> dbus_services; // up to date list of the names of running services

void add_service(const std::string &service) {
    dbus_services.emplace_back(service);
}

void remove_service(const std::string &service) {
    for (int i = 0; i < dbus_services.size(); i++) {
        if (dbus_services[i] == service) {
            dbus_services.erase(dbus_services.begin() + i);
            dbus_services.shrink_to_fit();
            return;
        }
    }
}

static DBusHandlerResult signal_handler(DBusConnection *connection,
                                        DBusMessage *message, void *_usr_data) {

    if (dbus_message_is_signal(message, "org.freedesktop.DBus",
                               "NameOwnerChanged")) {
        const char *name;
        const char *old_owner;
        const char *new_owner;

        if (!dbus_message_get_args(message, NULL,
                                   DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_STRING, &old_owner,
                                   DBUS_TYPE_STRING, &new_owner,
                                   DBUS_TYPE_INVALID)) {
            fprintf(stderr, "Error getting OwnerChanged args");
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (strcmp(old_owner, "") == 0) {
            add_service(name);
        } else if (strcmp(new_owner, "") == 0) {
            remove_service(name);
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void dbus_list_services(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto app = (App *) data;
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);

    DBusError error;
    ::dbus_error_init(&error);
    int len;
    char **args;
    if (dbus_message_get_args(dbus_reply, &error, DBUS_TYPE_ARRAY,
                              DBUS_TYPE_STRING, &args, &len, DBUS_TYPE_INVALID)) {
        for (char *name = *args; name; name = *++args) {
            add_service(name);
        }

        // TODO: crash if winbar started, dunst stareted and closed, and then winbar "settings" restarted
        dbus_bus_add_match(app->dbus_connection,
                           "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
                           &error);
        dbus_connection_add_filter(app->dbus_connection, signal_handler, NULL, free);
    }
    ::dbus_message_unref(dbus_reply);
}

void start_watching_dbus_services(App *app) {
    if (app->dbus_connection) {
        {
            DBusMessage *dbus_msg = nullptr;
            DBusPendingCall *pending;

            dbus_msg = ::dbus_message_new_method_call("org.freedesktop.DBus", "/", "org.freedesktop.DBus",
                                                      "ListNames");
            dbus_bool_t r = ::dbus_connection_send_with_reply(app->dbus_connection, dbus_msg, &pending,
                                                              DBUS_TIMEOUT_USE_DEFAULT);
            dbus_bool_t x = ::dbus_pending_call_set_notify(pending, dbus_list_services, app, nullptr);
            ::dbus_message_unref(dbus_msg);
            ::dbus_pending_call_unref(pending);
        }
    }
}

void stop_watching_dbus_services(App *app) {
    dbus_services.clear();
    dbus_services.shrink_to_fit();
}

static void dbus_kde_show_desktop_grid_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time

    }
    ::dbus_message_unref(dbus_reply);
}

void dbus_kde_show_desktop_grid(App *app) {
    if (!app->dbus_connection) return;

    DBusMessage *dbus_msg = nullptr;
    DBusPendingCall *pending;

    dbus_msg = ::dbus_message_new_method_call("org.kde.kglobalaccel", "/component/kwin",
                                              "org.kde.kglobalaccel.Component",
                                              "invokeShortcut");
    const char *arg = "ShowDesktopGrid";
    dbus_bool_t a = ::dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    dbus_bool_t r = ::dbus_connection_send_with_reply(app->dbus_connection, dbus_msg, &pending,
                                                      DBUS_TIMEOUT_USE_DEFAULT);
    dbus_bool_t x = ::dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_grid_response, nullptr, nullptr);
    ::dbus_message_unref(dbus_msg);
    ::dbus_pending_call_unref(pending);
}

void dbus_kde_show_desktop(App *app) {
    if (!app->dbus_connection) return;

    DBusMessage *dbus_msg = nullptr;
    DBusPendingCall *pending;

    dbus_msg = ::dbus_message_new_method_call("org.kde.kglobalaccel", "/component/kwin",
                                              "org.kde.kglobalaccel.Component",
                                              "invokeShortcut");
    const char *arg = "Show Desktop";
    dbus_bool_t a = ::dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    dbus_bool_t r = ::dbus_connection_send_with_reply(app->dbus_connection, dbus_msg, &pending,
                                                      DBUS_TIMEOUT_USE_DEFAULT);
    dbus_bool_t x = ::dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_grid_response, nullptr, nullptr);
    ::dbus_message_unref(dbus_msg);
    ::dbus_pending_call_unref(pending);
}

static void dbus_gnome_show_overview_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time
        printf("here\n");
    }
    ::dbus_message_unref(dbus_reply);
}

void dbus_gnome_show_overview(App *app) {
    if (!app->dbus_connection) return;

    DBusMessage *dbus_msg = nullptr;
    DBusPendingCall *pending;

    dbus_msg = ::dbus_message_new_method_call("org.gnome.Shell", "/org/gnome/Shell",
                                              "org.gnome.Shell",
                                              "Eval");
    const char *arg = "Main.overview.show();";
    dbus_bool_t a = ::dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    dbus_bool_t r = ::dbus_connection_send_with_reply(app->dbus_connection, dbus_msg, &pending,
                                                      DBUS_TIMEOUT_USE_DEFAULT);
    dbus_bool_t x = ::dbus_pending_call_set_notify(pending, dbus_gnome_show_overview_response, nullptr, nullptr);
    ::dbus_message_unref(dbus_msg);
    ::dbus_pending_call_unref(pending);
}