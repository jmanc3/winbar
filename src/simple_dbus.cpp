//
// Created by jmanc3 on 5/28/21.
//

#include "simple_dbus.h"
#include "application.h"
#include "main.h"
#include "notifications.h"
#include "taskbar.h"

#include <dbus/dbus.h>
#include <defer.h>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif


DBusConnection *dbus_connection = nullptr;

std::vector<std::string> running_dbus_services;

bool registered_object_path = false;


/*************************************************
 *
 * Watch running services.
 * Watch for the ownership of "names" changing.
 * Watch for losing or acquiring of "names".
 *
 *************************************************/

static double max_kde_brightness = 0;

void remove_service(const std::string &service) {
    for (int i = 0; i < running_dbus_services.size(); i++) {
        if (running_dbus_services[i] == service) {
            if (running_dbus_services[i] == "local.org_kde_powerdevil")
                max_kde_brightness = 0;
            running_dbus_services.erase(running_dbus_services.begin() + i);
            return;
        }
    }
}

DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *userdata);

static bool dbus_kde_max_brightness();

static DBusHandlerResult signal_handler(DBusConnection *connection,
                                        DBusMessage *message, void *user_data) {
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
            running_dbus_services.emplace_back(name);
            if (std::string(name) == "local.org_kde_powerdevil")
                dbus_kde_max_brightness();
        } else if (strcmp(new_owner, "") == 0) {
            remove_service(name);
        }
        // TODO: do I have to free "name" and others???

        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus",
                                      "NameAcquired")) {
        const char *name;
        if (!dbus_message_get_args(message, NULL,
                                   DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_INVALID)) {
            fprintf(stderr, "Error getting NameAcquired args");
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (std::string(name) == "org.freedesktop.Notifications") {
            static const DBusObjectPathVTable vtable = {
                    .message_function = handle_message_cb,
            };
            if (!dbus_connection_register_object_path(dbus_connection, "/org/freedesktop/Notifications", &vtable,
                                                      app)) {
                fprintf(stdout, "%s\n",
                        "Error registering object path /org/freedesktop/Notifications on NameAcquired signal");
            } else {
                registered_object_path = true;
            }
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus",
                                      "NameLost")) {
        const char *name;
        if (!dbus_message_get_args(message, NULL,
                                   DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_INVALID)) {
            fprintf(stderr, "Error getting NameLost args");
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (!dbus_connection_unregister_object_path(dbus_connection, "/org/freedesktop/Notifications")) {
            fprintf(stdout, "%s\n", "Error unregistering object path /org/freedesktop/Notifications after losing name");
        } else {
            registered_object_path = false;
        }

        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void dbus_reply_to_list_names_request(DBusPendingCall *call, void *data) {
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    if (!dbus_reply) return;
    defer(dbus_message_unref(dbus_reply));

    DBusError error = DBUS_ERROR_INIT;
    defer(dbus_error_free(&error));

    int len;
    char **args;
    if (!dbus_message_get_args(dbus_reply, &error,
                               DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &args, &len,
                               DBUS_TYPE_INVALID)) {
        fprintf(stderr, "Couldn't parse arguments of reply to list names request due to: %s\n%s\n",
                error.name, error.message);
        return;
    }

    // Add all the names to the running services
    //
    for (char *name = *args; name; name = *++args) {
        running_dbus_services.emplace_back(name);
        if (std::string(name) == "local.org_kde_powerdevil")
            dbus_kde_max_brightness();
    }

    // Register some signals that we are interested in hearing about "NameOwnerChanged", "NameAcquired", "NameLost"
    //
    if (!dbus_connection) return;

    dbus_bus_add_match(dbus_connection,
                       "type='signal',"
                       "sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',"
                       "member='NameOwnerChanged'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal NameOwnerChanged because: %s\n%s\n",
                error.name, error.message);
    }
    dbus_bus_add_match(dbus_connection,
                       "type='signal',"
                       "sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',"
                       "member='NameAcquired'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal NameAcquired because: %s\n%s\n",
                error.name, error.message);
    }
    dbus_bus_add_match(dbus_connection,
                       "type='signal',"
                       "sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',"
                       "member='NameLost'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal NameLost because: %s\n%s\n",
                error.name, error.message);
    }
    if (!dbus_connection_add_filter(dbus_connection, signal_handler, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory to add connection filter\n");
        return;
    }
}

static void request_name_of_every_service_running() {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.freedesktop.DBus",
                                                         "/",
                                                         "org.freedesktop.DBus",
                                                         "ListNames");
    defer(dbus_message_unref(dbus_msg));

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }

    if (!dbus_pending_call_set_notify(pending, dbus_reply_to_list_names_request, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }
}


/*************************************************
 *
 * Using DBus for what winbar wants to do
 *
 *************************************************/


static void dbus_kde_show_desktop_grid_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time
    }
}

static void dbus_kde_show_desktop_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time
    }
}

bool dbus_kde_show_desktop_grid() {
    if (!dbus_connection) return false;

    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.kglobalaccel", "/component/kwin",
                                                         "org.kde.kglobalaccel.Component",
                                                         "invokeShortcut");
    defer(dbus_message_unref(dbus_msg));
    const char *arg = "ShowDesktopGrid";
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_gnome_show_overview\" couldn't append an argument to the DBus message.");
        return false;
    }

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_grid_response, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

bool dbus_kde_show_desktop() {
    if (!dbus_connection) return false;

    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.kglobalaccel", "/component/kwin",
                                                         "org.kde.kglobalaccel.Component",
                                                         "invokeShortcut");
    defer(dbus_message_unref(dbus_msg));
    const char *arg = "Show Desktop";
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_gnome_show_overview\" couldn't append an argument to the DBus message.");
        return false;
    }

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_response, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

static void dbus_gnome_show_overview_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time
    }
}

bool dbus_gnome_show_overview() {
    if (!dbus_connection) return false;

    DBusMessage *dbus_msg = dbus_message_new_method_call("org.gnome.Shell", "/org/gnome/Shell",
                                                         "org.gnome.Shell",
                                                         "Eval");
    defer(dbus_message_unref(dbus_msg));
    const char *arg = "Main.overview.show();";
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_gnome_show_overview\" couldn't append an argument to the DBus message.");
        return false;
    }

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_gnome_show_overview_response, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

static void dbus_kde_max_brightness_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));

    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
        return;

    int dbus_result = 0;
    if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID)) {
        max_kde_brightness = dbus_result;
    }
}

static bool dbus_kde_max_brightness() {
    if (!dbus_connection) return false;

    DBusMessage *dbus_msg = dbus_message_new_method_call("local.org_kde_powerdevil",
                                                         "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                                                         "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                                                         "brightnessMax");
    defer(dbus_message_unref(dbus_msg));

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_max_brightness_response, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

double dbus_get_kde_max_brightness() {
    return max_kde_brightness;
}

double dbus_get_kde_current_brightness() {
    if (!dbus_connection) return 0;

    DBusMessage *dbus_msg = dbus_message_new_method_call("local.org_kde_powerdevil",
                                                         "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                                                         "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                                                         "brightness");
    defer(dbus_message_unref(dbus_msg));

    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection, dbus_msg, 200, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));

        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return dbus_result;
    }
    return 0;
}

static void dbus_kde_set_brightness_response(DBusPendingCall *call, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
}

bool dbus_kde_set_brightness(double percentage) {
    if (!dbus_connection) return false;

    DBusMessage *dbus_msg = dbus_message_new_method_call("local.org_kde_powerdevil",
                                                         "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                                                         "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                                                         "setBrightness");
    defer(dbus_message_unref(dbus_msg));

    const int brightness = max_kde_brightness * percentage;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &brightness, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_kde_set_brightness\" couldn't append an argument to the DBus message.");
        return false;
    }

    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));

    if (!dbus_connection_send_with_reply(dbus_connection, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_set_brightness_response, app, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}


/*************************************************
 *
 * Notifications
 *
 *************************************************/


static const char *introspection_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<node name=\"/org/freedesktop/Notifications\">"
        "    <interface name=\"org.freedesktop.Notifications\">"

        "        <method name=\"GetCapabilities\">"
        "            <arg direction=\"out\" name=\"capabilities\"    type=\"as\"/>"
        "        </method>"

        "        <method name=\"Notify\">"
        "            <arg direction=\"in\"  name=\"app_name\"        type=\"s\"/>"
        "            <arg direction=\"in\"  name=\"replaces_id\"     type=\"u\"/>"
        "            <arg direction=\"in\"  name=\"app_icon\"        type=\"s\"/>"
        "            <arg direction=\"in\"  name=\"summary\"         type=\"s\"/>"
        "            <arg direction=\"in\"  name=\"body\"            type=\"s\"/>"
        "            <arg direction=\"in\"  name=\"actions\"         type=\"as\"/>"
        "            <arg direction=\"in\"  name=\"hints\"           type=\"a{sv}\"/>"
        "            <arg direction=\"in\"  name=\"expire_timeout\"  type=\"i\"/>"
        "            <arg direction=\"out\" name=\"id\"              type=\"u\"/>"
        "        </method>"

        "        <method name=\"CloseNotification\">"
        "            <arg direction=\"in\"  name=\"id\"              type=\"u\"/>"
        "        </method>"

        "        <method name=\"GetServerInformation\">"
        "            <arg direction=\"out\" name=\"name\"            type=\"s\"/>"
        "            <arg direction=\"out\" name=\"vendor\"          type=\"s\"/>"
        "            <arg direction=\"out\" name=\"version\"         type=\"s\"/>"
        "            <arg direction=\"out\" name=\"spec_version\"    type=\"s\"/>"
        "        </method>"

        "        <signal name=\"NotificationClosed\">"
        "            <arg name=\"id\"         type=\"u\"/>"
        "            <arg name=\"reason\"     type=\"u\"/>"
        "        </signal>"

        "        <signal name=\"ActionInvoked\">"
        "            <arg name=\"id\"         type=\"u\"/>"
        "            <arg name=\"action_key\" type=\"s\"/>"
        "        </signal>"

        "    </interface>"

        "    <interface name=\"org.freedesktop.DBus.Introspectable\">"

        "        <method name=\"Introspect\">"
        "            <arg direction=\"out\" name=\"xml_data\"    type=\"s\"/>"
        "        </method>"

        "    </interface>"
        "</node>";

static bool
dbus_array_reply(DBusConnection *connection, DBusMessage *msg, std::vector<std::string> array) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    defer(dbus_message_unref(reply));

    bool success = true;
    DBusMessageIter args;
    dbus_message_iter_init_append(reply, &args);
    for (auto &i: array) {
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &i))
            success = false;
    }

    if (success)
        success = dbus_connection_send(connection, reply, NULL);
    return success;
}

// https://developer.gnome.org/notification-spec/
DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *userdata) {
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        defer(dbus_message_unref(reply));

        dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_xml, DBUS_TYPE_INVALID);
        dbus_connection_send(connection, reply, NULL);

        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "GetCapabilities")) {
        std::vector<std::string> strings = {"actions", "body"};

        if (dbus_array_reply(connection, message, strings))
            return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "GetServerInformation")) {
        std::vector<std::string> strings = {"winbar", "winbar", "0.1", "1.2"};

        if (dbus_array_reply(connection, message, strings))
            return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "CloseNotification")) {
        int result = 0;
        DBusError error = DBUS_ERROR_INIT;
        if (dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &result, DBUS_TYPE_INVALID)) {
            close_notification(result);
            return DBUS_HANDLER_RESULT_HANDLED;
        } else if (dbus_error_is_set(&error)) {
            fprintf(stderr, "CloseNotification called but couldn't parse arg. Error message: (%s)\n",
                    error.message);
            dbus_error_free(&error);
        }
    } else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "Notify")) {
        static int id = 1; // id can't be zero due to specification (notice [static])

        DBusMessageIter args;
        const char *app_name = nullptr;
        dbus_uint32_t replaces_id = -1;
        const char *app_icon = nullptr;
        const char *summary = nullptr;
        const char *body = nullptr;
        dbus_int32_t expire_timeout_in_milliseconds = -1; // 0 means never unless user interacts, -1 means the server (us) decides
        auto notification_info = new NotificationInfo;

        dbus_message_iter_init(message, &args);
        dbus_message_iter_get_basic(&args, &app_name);
        dbus_message_iter_next(&args);
        dbus_message_iter_get_basic(&args, &replaces_id);
        dbus_message_iter_next(&args);
        dbus_message_iter_get_basic(&args, &app_icon);
        dbus_message_iter_next(&args);
        dbus_message_iter_get_basic(&args, &summary);
        dbus_message_iter_next(&args);
        dbus_message_iter_get_basic(&args, &body);
        dbus_message_iter_next(&args);
        int actions_count = dbus_message_iter_get_element_count(&args);
        if (actions_count > 0) {
            DBusMessageIter el;
            dbus_message_iter_recurse(&args, &el);
            while (dbus_message_iter_get_arg_type(&el) != DBUS_TYPE_INVALID) {
                const char *key = nullptr;
                dbus_message_iter_get_basic(&el, &key);
                dbus_message_iter_next(&el);

                if (dbus_message_iter_get_arg_type(&el) != DBUS_TYPE_INVALID) {
                    const char *value = nullptr;
                    dbus_message_iter_get_basic(&el, &value);
                    dbus_message_iter_next(&el);

                    NotificationAction notification_action;
                    notification_action.id = key;
                    notification_action.label = value;
                    notification_info->actions.push_back(notification_action);
                }
            }
        }
        dbus_message_iter_next(&args);  // actions of type ARRAY
        dbus_message_iter_next(&args);  // hints of type DICT
        dbus_message_iter_get_basic(&args, &expire_timeout_in_milliseconds);

        // TODO: handle replacing
        notification_info->id = id++;
        notification_info->app_name = app_name;
        notification_info->app_icon = app_icon;
        notification_info->summary = summary;
        notification_info->body = body;
        notification_info->expire_timeout_in_milliseconds = expire_timeout_in_milliseconds;
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%I:%M %p");
        notification_info->time_started = ss.str();
        notification_info->calling_dbus_client = dbus_message_get_sender(message);
        notifications.push_back(notification_info);

        show_notification((App *) userdata, notification_info);

        DBusMessage *reply = dbus_message_new_method_return(message);
        defer(dbus_message_unref(reply));

        dbus_message_iter_init_append(reply, &args);
        const dbus_uint32_t current_id = notification_info->id;
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &current_id) ||
            !dbus_connection_send(connection, reply, NULL)) {
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void notification_closed_signal(App *app, NotificationInfo *ni, NotificationReasonClosed reason) {
    if (!dbus_connection || !ni)
        return;

    DBusMessage *dmsg = dbus_message_new_signal("/org/freedesktop/Notifications",
                                                "org.freedesktop.Notifications",
                                                "NotificationClosed");
    defer(dbus_message_unref(dmsg));

    DBusMessageIter args;
    dbus_message_iter_init_append(dmsg, &args);
    int id = ni->id;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &id);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &reason);

    dbus_message_set_destination(dmsg, NULL);

    dbus_connection_send(dbus_connection, dmsg, NULL);

    bool action_center_icon_needs_change = true;
    for (auto n: notifications) {
        if (n->sent_to_action_center) {
            action_center_icon_needs_change = false;
        }
    }
    ni->sent_to_action_center = true;
    if (action_center_icon_needs_change) {
        if (auto c = client_by_name(app, "taskbar")) {
            if (auto co = container_by_name("action", c->root)) {
                auto data = (ActionCenterButtonData *) co->user_data;
                data->some_unseen = true;
                client_create_animation(app, c, &data->slide_anim, 140, nullptr, 1);
                request_refresh(app, c);
            }
        }
    }
}

void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action) {
    if (!dbus_connection || !ni)
        return;

    DBusMessage *dmsg = dbus_message_new_signal("/org/freedesktop/Notifications",
                                                "org.freedesktop.Notifications",
                                                "ActionInvoked");
    defer(dbus_message_unref(dmsg));

    DBusMessageIter args;
    dbus_message_iter_init_append(dmsg, &args);
    int id = ni->id;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &id);
    const char *text_id = action.id.c_str();
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &text_id);

    dbus_message_set_destination(dmsg, ni->calling_dbus_client.c_str());

    dbus_connection_send(dbus_connection, dmsg, NULL);
}

/*************************************************
 *
 * Main DBus pluming
 *
 *************************************************/


void dbus_poll_wakeup(App *, int) {
    DBusDispatchStatus status;
    do {
        dbus_connection_read_write_dispatch(dbus_connection, 0);
        status = dbus_connection_get_dispatch_status(dbus_connection);
    } while (status == DBUS_DISPATCH_DATA_REMAINS);
}

void dbus_start() {
    // Open DBus connection
    //
    DBusError error = DBUS_ERROR_INIT;
    defer(dbus_error_free(&error));

    dbus_connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "DBus Error: %s\n%s\n", error.name, error.message);
        dbus_connection = nullptr;
        return;
    }

    if (!dbus_connection) return;

    // Weave the DBus file descriptor into our main event loop
    //
    int file_descriptor = -1;
    if (dbus_connection_get_unix_fd(dbus_connection, &file_descriptor) != TRUE) {
        fprintf(stderr, "%s\n", "Couldn't get the file descriptor for the DBus Connection");
        dbus_connection_unref(dbus_connection);
        dbus_connection = nullptr;
        return;
    }

    if (poll_descriptor(app, file_descriptor, EPOLLIN | EPOLLPRI | EPOLLHUP | EPOLLERR, dbus_poll_wakeup)) {
        // Get the names of all the services running
        //
        request_name_of_every_service_running();

        // Try to become the owner of the org.freedesktop.Notification name
        //
        int result = dbus_bus_request_name(dbus_connection, "org.freedesktop.Notifications",
                                           DBUS_NAME_FLAG_REPLACE_EXISTING, &error);

        if (dbus_error_is_set(&error)) {
            fprintf(stderr, "Ran into error when trying to become the sessions notification manager (%s)\n",
                    error.message);
            dbus_error_free(&error);
            return;
        }

        dbus_poll_wakeup(nullptr, 0);
    }
}

void dbus_end() {
    running_dbus_services.clear();
    running_dbus_services.shrink_to_fit();

    for (auto n: notifications) {
        delete n;
    }
    notifications.clear();
    notifications.shrink_to_fit();

    displaying_notifications.clear();
    displaying_notifications.shrink_to_fit();

    if (!dbus_connection) return;

    DBusError error = DBUS_ERROR_INIT;
    defer(dbus_error_free(&error));

    if (registered_object_path) {
        if (!dbus_connection_unregister_object_path(dbus_connection, "/org/freedesktop/Notifications")) {
            fprintf(stderr, "%s", "Error unregistering object path /org/freedesktop/Notifications");
        }

        dbus_bus_release_name(dbus_connection, "org.freedesktop.Notifications", &error);
        if (dbus_error_is_set(&error)) {
            fprintf(stderr, "Error releasing name: %s\n%s\n", error.name, error.message);
        }

        registered_object_path = false;
    }

    dbus_bus_remove_match(dbus_connection,
                          "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
                          &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Error trying to remove NameOwnerChanged rule due to: %s\n%s\n", error.name, error.message);
    }
    dbus_bus_remove_match(dbus_connection,
                          "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameAcquired'",
                          &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Error trying to remove NameAcquired rule due to: %s\n%s\n", error.name, error.message);
    }
    dbus_bus_remove_match(dbus_connection,
                          "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameLost'",
                          &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Error trying to remove NameLost rule due to: %s\n%s\n", error.name, error.message);
    }

    dbus_connection_unref(dbus_connection);
    dbus_connection = nullptr;
}
