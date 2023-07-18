//#include "main.h"
//
// Created by jmanc3 on 5/28/21.
//

#include "simple_dbus.h"
#include "application.h"
#include "main.h"
#include "notifications.h"
#include "taskbar.h"
#include "bluetooth_menu.h"

#include <dbus/dbus.h>
#include <defer.h>
#include <cstring>
#include <iomanip>
#include <iostream>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif


DBusConnection *dbus_connection_session = nullptr;
DBusConnection *dbus_connection_system = nullptr;

std::vector<std::string> running_dbus_services;
std::vector<std::string> status_notifier_items;
std::vector<BluetoothInterface *> bluetooth_interfaces;

bool registered_object_path = false;
bool registered_bluetooth_agent = false;
bool registered_with_bluez = false;

/*************************************************
 *
 * Watch running services.
 * Watch for the ownership of "names" changing.
 * Watch for losing or acquiring of "names".
 *
 *************************************************/

static double max_kde_brightness = 0;
static bool gnome_brightness_running = false;

void StatusNotifierItemRegistered(const std::string &service);

void StatusNotifierItemUnregistered(const std::string &service);

void StatusNotifierHostRegistered();

void remove_service(const std::string &service) {
    for (int i = 0; i < running_dbus_services.size(); i++) {
        if (running_dbus_services[i] == service) {
            if (running_dbus_services[i] == "local.org_kde_powerdevil")
                max_kde_brightness = 0;
            if (running_dbus_services[i] == "org.gnome.SettingsDaemon.Power")
                gnome_brightness_running = false;
            if (running_dbus_services[i] == "org.bluez")
                bluetooth_service_ended();
            if (running_dbus_services[i] == "org.freedesktop.UPower")
                upower_service_ended();
            
            for (int x = 0; x < status_notifier_items.size(); x++) {
                const std::string &sni = status_notifier_items[x];
                if (sni == service) {
                    StatusNotifierItemUnregistered(service);
                    
                    status_notifier_items.erase(status_notifier_items.begin() + x);
                }
            }
            
            running_dbus_services.erase(running_dbus_services.begin() + i);
            return;
        }
    }
}

DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *userdata);

void create_status_notifier_watcher();

static bool dbus_kde_max_brightness();

void parse_and_add_or_update_interface(DBusMessageIter iter2);

static DBusHandlerResult signal_handler(DBusConnection *dbus_connection,
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
            if (std::string(name) == "org.gnome.SettingsDaemon.Power")
                gnome_brightness_running = true;
            if (std::string(name) == "org.bluez")
                bluetooth_service_started();
            if (std::string(name) == "org.freedesktop.UPower")
                upower_service_started();
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
    
        if (std::string(name) == "local.org_kde_powerdevil")
            dbus_kde_max_brightness();
        if (std::string(name) == "org.gnome.SettingsDaemon.Power")
            gnome_brightness_running = true;
        if (std::string(name) == "org.bluez")
            bluetooth_service_started();
        if (std::string(name) == "org.freedesktop.UPower")
            upower_service_started();
    
        if (std::string(name) == "org.freedesktop.Notifications") {
            static const DBusObjectPathVTable vtable = {
                    .message_function = handle_message_cb,
            };
            if (!dbus_connection_register_object_path(dbus_connection, "/org/freedesktop/Notifications", &vtable,
                                                      nullptr)) {
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
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        parse_and_add_or_update_interface(args);
    
        if (on_any_bluetooth_property_changed) {
            on_any_bluetooth_property_changed();
        }
    
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_OBJECT_PATH) {
            char *object_path;
            dbus_message_iter_get_basic(&args, &object_path);
            
            for (int i = 0; i < bluetooth_interfaces.size(); i++) {
                if (bluetooth_interfaces[i]->object_path == object_path) {
                    DBusError error;
                    dbus_error_init(&error);
                    dbus_bus_remove_match(dbus_connection,
                                          ("type='signal',"
                                           "sender='org.bluez',"
                                           "interface='org.freedesktop.DBus.Properties',"
                                           "member='PropertiesChanged',"
                                           "path='" + std::string(object_path) + "'").c_str(),
                                          &error);
                    if (dbus_error_is_set(&error)) {
                        fprintf(stderr, "Error removing match for %s: %s\n", object_path, error.message);
                    }
                    
                    if (bluetooth_interfaces[i]->type == BluetoothInterfaceType::Device) {
                        delete (Device *) bluetooth_interfaces[i];
                    } else if (bluetooth_interfaces[i]->type == BluetoothInterfaceType::Adapter) {
                        delete (Adapter *) bluetooth_interfaces[i];
                    }
                    bluetooth_interfaces.erase(bluetooth_interfaces.begin() + i);
                    break;
                }
            }
        }
    
        if (on_any_bluetooth_property_changed) {
            on_any_bluetooth_property_changed();
        }
    
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        for (auto *interface: bluetooth_interfaces) {
            if (dbus_message_has_path(message, interface->object_path.c_str())) {
                DBusMessageIter args;
                dbus_message_iter_init(message, &args);
                
                if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
                    dbus_message_iter_next(&args);
                    
                    while (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
                        DBusMessageIter array;
                        dbus_message_iter_recurse(&args, &array);
                        
                        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
                            DBusMessageIter dict;
                            dbus_message_iter_recurse(&array, &dict);
                            
                            char *key = nullptr;
                            if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRING)
                                dbus_message_iter_get_basic(&dict, &key);
                            dbus_message_iter_next(&dict);
                            
                            if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_VARIANT) {
                                DBusMessageIter variant;
                                dbus_message_iter_recurse(&dict, &variant);
                                
                                if (strcmp(key, "Address") == 0) {
                                    char *iter7s;
                                    dbus_message_iter_get_basic(&variant, &iter7s);
                                    interface->mac_address = iter7s;
                                } else if (strcmp(key, "Name") == 0) {
                                    char *iter7s;
                                    dbus_message_iter_get_basic(&variant, &iter7s);
                                    interface->name = iter7s;
                                } else if (strcmp(key, "Alias") == 0) {
                                    char *iter7s;
                                    dbus_message_iter_get_basic(&variant, &iter7s);
                                    interface->alias = iter7s;
                                } else if (interface->type == BluetoothInterfaceType::Adapter) {
                                    if (strcmp(key, "Powered") == 0) {
                                        bool iter7b;
                                        dbus_message_iter_get_basic(&variant, &iter7b);
                                        ((Adapter *) interface)->powered = iter7b;
                                    }
                                } else if (interface->type == BluetoothInterfaceType::Device) {
                                    if (strcmp(key, "Icon") == 0) {
                                        char *iter7s;
                                        dbus_message_iter_get_basic(&variant, &iter7s);
                                        ((Device *) interface)->icon = iter7s;
                                    } else if (strcmp(key, "Adapter") == 0) {
                                        char *iter7s;
                                        dbus_message_iter_get_basic(&variant, &iter7s);
                                        ((Device *) interface)->adapter = iter7s;
                                    } else if (strcmp(key, "Paired") == 0) {
                                        bool iter7b;
                                        dbus_message_iter_get_basic(&variant, &iter7b);
                                        ((Device *) interface)->paired = iter7b;
                                    } else if (strcmp(key, "Connected") == 0) {
                                        bool iter7b;
                                        dbus_message_iter_get_basic(&variant, &iter7b);
                                        ((Device *) interface)->connected = iter7b;
                                    } else if (strcmp(key, "Bonded") == 0) {
                                        bool iter7b;
                                        dbus_message_iter_get_basic(&variant, &iter7b);
                                        ((Device *) interface)->bonded = iter7b;
                                    } else if (strcmp(key, "Trusted") == 0) {
                                        bool iter7b;
                                        dbus_message_iter_get_basic(&variant, &iter7b);
                                        ((Device *) interface)->trusted = iter7b;
                                    }
                                }
                            }
                            
                            dbus_message_iter_next(&array);
                        }
                        dbus_message_iter_next(&args);
                    }
                }
    
                if (on_any_bluetooth_property_changed) {
                    on_any_bluetooth_property_changed();
                }
    
                return DBUS_HANDLER_RESULT_HANDLED;
            }
        }
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void dbus_reply_to_list_names_request(DBusPendingCall *call, void *data) {
    auto dbus_connection = (DBusConnection *) data;
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
        if (std::string(name) == "org.gnome.SettingsDaemon.Power")
            gnome_brightness_running = true;
        if (std::string(name) == "org.bluez")
            bluetooth_service_started();
        if (std::string(name) == "org.freedesktop.UPower")
            upower_service_started();
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

static void request_name_of_every_service_running(DBusConnection *dbus_connection) {
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
    
    if (!dbus_pending_call_set_notify(pending, dbus_reply_to_list_names_request, dbus_connection, nullptr)) {
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


static void dbus_kde_show_desktop_grid_response(DBusPendingCall *call, void *) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    if (::dbus_message_get_type(dbus_reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        // The call didn't succeed so, do what we were going to do anyways, as long as it hasn't been too much time
    }
}

static void dbus_kde_show_desktop_response(DBusPendingCall *call, void *) {
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
    if (!dbus_connection_session) return false;
    
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
    
    if (!dbus_connection_send_with_reply(dbus_connection_session, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_grid_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

bool dbus_kde_show_desktop() {
    if (!dbus_connection_session) return false;
    
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
    
    if (!dbus_connection_send_with_reply(dbus_connection_session, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_show_desktop_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

static void dbus_gnome_show_overview_response(DBusPendingCall *call, void *) {
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
    if (!dbus_connection_session) return false;
    
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
    
    if (!dbus_connection_send_with_reply(dbus_connection_session, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_gnome_show_overview_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

static void dbus_kde_max_brightness_response(DBusPendingCall *call, void *) {
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
    if (!dbus_connection_session) return false;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("local.org_kde_powerdevil",
                                                         "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                                                         "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                                                         "brightnessMax");
    defer(dbus_message_unref(dbus_msg));
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    
    if (!dbus_connection_send_with_reply(dbus_connection_session, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_max_brightness_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

// TODO time-gate this so it only happens every 3 seconds
double dbus_get_kde_max_brightness() {
    if (max_kde_brightness == 0) {
        for (auto name: running_dbus_services) {
            if (std::string(name) == "local.org_kde_powerdevil")
                dbus_kde_max_brightness();
        }
    }
    return max_kde_brightness;
}

double dbus_get_kde_current_brightness() {
    if (!dbus_connection_session) return 0;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("local.org_kde_powerdevil",
                                                         "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                                                         "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                                                         "brightness");
    defer(dbus_message_unref(dbus_msg));
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 200, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return dbus_result;
    }
    return 0;
}

static void dbus_kde_set_brightness_response(DBusPendingCall *call, void *) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
}

bool dbus_kde_set_brightness(double percentage) {
    if (!dbus_connection_session) return false;
    
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
    
    if (!dbus_connection_send_with_reply(dbus_connection_session, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    if (!dbus_pending_call_set_notify(pending, dbus_kde_set_brightness_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return false;
    }
    return true;
}

bool dbus_kde_running() {
    for (int i = 0; i < running_dbus_services.size(); i++) {
        if (running_dbus_services[i] == "local.org_kde_powerdevil") {
            return true;
        }
    }
    
    return false;
}

bool dbus_gnome_running() {
    return gnome_brightness_running;
}

double dbus_get_gnome_brightness() {
    if (!dbus_connection_session) return 0;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.gnome.SettingsDaemon.Power",
                                                         "/org/gnome/SettingsDaemon/Power",
                                                         "org.freedesktop.DBus.Properties",
                                                         "Get");
    defer(dbus_message_unref(dbus_msg));
    
    const char *interface = "org.gnome.SettingsDaemon.Power.Screen";
    const char *property = "Brightness";
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property,
                                  DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_get_gnome_brightness\" couldn't append an arguments to the DBus message.");
        return 0;
    }
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 200, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        DBusMessageIter iter;
        DBusMessageIter sub;
        int dbus_result;
        
        dbus_message_iter_init(dbus_reply, &iter);
        if (DBUS_TYPE_VARIANT != dbus_message_iter_get_arg_type(&iter)) {
            fprintf(stderr, "Reply from \"dbus_get_gnome_brightness\" wasn't of type Variant.\n");
            return 0;
        }
        dbus_message_iter_recurse(&iter, &sub);
        if (DBUS_TYPE_INT32 != dbus_message_iter_get_arg_type(&sub)) {
            fprintf(stderr, "Reply from \"dbus_get_gnome_brightness\" wasn't of type INT32.\n");
            return 0;
        }
        
        dbus_message_iter_get_basic(&sub, &dbus_result);
        return dbus_result;
    }
    return 0;
}

bool dbus_set_gnome_brightness(double p) {
    if (!dbus_connection_session) return false;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.gnome.SettingsDaemon.Power",
                                                         "/org/gnome/SettingsDaemon/Power",
                                                         "org.freedesktop.DBus.Properties",
                                                         "Set");
    defer(dbus_message_unref(dbus_msg));
    
    const char *interface = "org.gnome.SettingsDaemon.Power.Screen";
    const char *property = "Brightness";
    const int percentage = (int) p;
    
    DBusMessageIter iter, subIter;
    dbus_message_iter_init_append(dbus_msg, &iter);
    
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &interface)) {
        fprintf(stderr, "dbus_set_gnome_brightness: Couldn't append interface string\n");
        return false;
    }
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &property)) {
        fprintf(stderr, "dbus_set_gnome_brightness: Couldn't append property string\n");
        return false;
    }
    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, DBUS_TYPE_INT32_AS_STRING, &subIter)) {
        fprintf(stderr, "dbus_set_gnome_brightness: Couldn't open variant container\n");
        return false;
    }
    if (!dbus_message_iter_append_basic(&subIter, DBUS_TYPE_INT32, &percentage)) {
        fprintf(stderr, "dbus_set_gnome_brightness: Couldn't append int32 to variant container\n");
        return false;
    }
    if (!dbus_message_iter_close_container(&iter, &subIter)) {
        fprintf(stderr, "dbus_set_gnome_brightness: Couldn't close variant container\n");
        return false;
    }
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 200, nullptr);
    if (dbus_reply) {
        dbus_message_unref(dbus_reply);
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
DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *) {
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        defer(dbus_message_unref(reply));
        
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_xml, DBUS_TYPE_INVALID);
        dbus_connection_send(connection, reply, NULL);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "GetCapabilities")) {
        std::vector<std::string> strings = {"actions", "body", "body-hyperlinks", "body-markup", "body-images",
                                            "icon-static", "inline-reply", "x-kde-urls", "persistence",
                                            "x-kde-origin-name", "x-kde-display-appname", "inhibitions"};
    
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
        
        auto notification_info = new NotificationInfo;
        int actions_count = dbus_message_iter_get_element_count(&args);
//        printf("App Name: %s\n", app_name);
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
                    
//                    printf("Action: %s, %s\n", key, value);
                    
                    NotificationAction notification_action;
                    notification_action.id = key;
                    if (strlen(value) == 0) {
                        notification_action.label = value;                  
                    } else {
                        notification_action.label = "Default Action";
                    }
                    notification_info->actions.push_back(notification_action);
                }
            }
        }
        dbus_message_iter_next(&args);  // actions of type ARRAY
    
        while (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr;
            dbus_message_iter_recurse(&args, &arr);
        
            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter dict;
                dbus_message_iter_recurse(&arr, &dict);
            
                const char *hint_name = nullptr;
                if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRING) {
                    dbus_message_iter_get_basic(&dict, &hint_name);
                    dbus_message_iter_next(&dict);
                }
                if (hint_name) {
                    if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter var;
                        dbus_message_iter_recurse(&dict, &var);
//                        printf("hint_name: %s, type: %d, %c\n", hint_name, dbus_message_iter_get_arg_type(&var), dbus_message_iter_get_arg_type(&var));
                        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                            const char *hint_value = nullptr;
                            dbus_message_iter_get_basic(&var, &hint_value);
        
                            if (strcmp("x-kde-appname", hint_name) == 0) {
                                notification_info->x_kde_appname = hint_value;
                            } else if (strcmp("x-kde-origin-name", hint_name) == 0) {
                                notification_info->x_kde_origin_name = hint_value;
                            } else if (strcmp("x-kde-display-appname", hint_name) == 0) {
                                notification_info->x_kde_display_appname = hint_value;
                            } else if (strcmp("desktop-entry", hint_name) == 0) {
                                notification_info->desktop_entry = hint_value;
                            } else if (strcmp("x-kde-eventId", hint_name) == 0) {
                                notification_info->x_kde_eventId = hint_value;
                            } else if (strcmp("x-kde-reply-placeholder-text", hint_name) == 0) {
                                notification_info->x_kde_reply_placeholder_text = hint_value;
                            } else if (strcmp("x-kde-reply-submit-button-text", hint_name) == 0) {
                                notification_info->x_kde_reply_submit_button_text = hint_value;
                            } else if (strcmp("x-kde-reply-submit-button-icon-name", hint_name) == 0) {
                                notification_info->x_kde_reply_submit_button_icon_name = hint_value;
                            }
                        } else if (strcmp("x-kde-urls", hint_name) == 0) {
                            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
                                DBusMessageIter arrs;
                                dbus_message_iter_recurse(&var, &arrs);
            
                                while (dbus_message_iter_get_arg_type(&arrs) == DBUS_TYPE_STRING) {
                                    const char *url = nullptr;
                                    dbus_message_iter_get_basic(&arrs, &url);
                                    dbus_message_iter_next(&arrs);
                
                                    notification_info->x_kde_urls.emplace_back(url);
                                }
                            }
                        }
                    }
                }
                dbus_message_iter_next(&arr);  // hints of type DICT
            }
        
            dbus_message_iter_next(&args);  // hints of type DICT
        }
    
        if (strcmp(summary, "Widget Removed") == 0) {
            NotificationAction notification_action;
            notification_action.id = std::to_string(1);
            notification_action.label = "Undo";
            notification_info->actions.push_back(notification_action);
        }
    
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
        show_notification(notification_info);
    
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
    if (!dbus_connection_session || !ni)
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
    
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
    
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
                client_create_animation(app, c, &data->slide_anim, 0, 140, nullptr, 1);
                request_refresh(app, c);
            }
        }
    }
}

void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action) {
    if (!dbus_connection_session || !ni)
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
    
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
}

/*************************************************
 *
 * Main DBus pluming
 *
 *************************************************/


void dbus_poll_wakeup(App *, int, void *user_data) {
    auto dbus_connection = (DBusConnection *) user_data;
    DBusDispatchStatus status;
    do {
        dbus_connection_read_write_dispatch(dbus_connection, 0);
        status = dbus_connection_get_dispatch_status(dbus_connection);
    } while (status == DBUS_DISPATCH_DATA_REMAINS);
}

void dbus_start(DBusBusType dbusType) {
    // Open DBus connection
    //
    DBusError error = DBUS_ERROR_INIT;
    defer(dbus_error_free(&error));
    
    auto *dbus_connection = dbus_bus_get(dbusType, &error);
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
    
    if (dbusType == DBUS_BUS_SESSION) {
        dbus_connection_session = dbus_connection;
    } else {
        dbus_connection_system = dbus_connection;
    }
    
    if (poll_descriptor(app, file_descriptor, EPOLLIN | EPOLLPRI | EPOLLHUP | EPOLLERR, dbus_poll_wakeup,
                        dbus_connection, "dbus")) {
        // Get the names of all the services running
        //
        request_name_of_every_service_running(dbus_connection);
        
        if (dbusType == DBUS_BUS_SESSION) {
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
    
            create_status_notifier_watcher();
        }
        
        dbus_poll_wakeup(nullptr, 0, dbus_connection);
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
    
    for (int i = 0; i < 2; ++i) {
        DBusConnection *dbus_connection = i == 0 ? dbus_connection_session : dbus_connection_system;
        if (!dbus_connection)
            continue;
        
        DBusError error = DBUS_ERROR_INIT;
        defer(dbus_error_free(&error));
        
        if (registered_object_path && dbus_connection == dbus_connection_session) {
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
    }
    dbus_connection_session = nullptr;
    dbus_connection_system = nullptr;
}

void dbus_computer_logoff() {
    if (!dbus_connection_session) return;
    
    bool found = false;
    for (const auto &item: running_dbus_services)
        if (item == "org.kde.ksmserver")
            found = true;
    if (!found)
        return;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.ksmserver",
                                                         "/KSMServer",
                                                         "org.kde.KSMServerInterface",
                                                         "logout");
    defer(dbus_message_unref(dbus_msg));
    
    const int confirm = 0;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &confirm, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_type = 0; // Logout
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_type, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_mode = 2;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_mode, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 1000,
                                                                        nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return;
    }
}

// All from: https://www.reddit.com/r/kde/comments/70hnzg/command_to_properly_shutdownreboot_kde_machine/
void dbus_computer_shut_down() {
    if (!dbus_connection_session) return;
    
    bool found = false;
    for (const auto &item: running_dbus_services)
        if (item == "org.kde.ksmserver")
            found = true;
    if (!found)
        return;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.ksmserver",
                                                         "/KSMServer",
                                                         "org.kde.KSMServerInterface",
                                                         "logout");
    defer(dbus_message_unref(dbus_msg));
    
    const int confirm = 0;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &confirm, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_type = 2;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_type, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_mode = 2;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_mode, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 1000, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return;
    }
}

// All from: https://www.reddit.com/r/kde/comments/70hnzg/command_to_properly_shutdownreboot_kde_machine/
void dbus_computer_restart() {
    if (!dbus_connection_session) return;
    
    bool found = false;
    for (const auto &item: running_dbus_services)
        if (item == "org.kde.ksmserver")
            found = true;
    if (!found)
        return;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.ksmserver",
                                                         "/KSMServer",
                                                         "org.kde.KSMServerInterface",
                                                         "logout");
    defer(dbus_message_unref(dbus_msg));
    
    const int confirm = 0;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &confirm, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_type = 1;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_type, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    const int shutdown_mode = 2;
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_mode, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return;
    }
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 1000, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return;
    }
}

/*************************************************
 *
 * Talking with org.bluez
 *
 *************************************************/

// The agent is made on dbus, and then attached to the org.bluez service. This us removing it from org.bluez not dbus.
void unregister_agent_with_bluez() {
    if (dbus_connection_system == nullptr) return;
    registered_with_bluez = false;
    
    const char *agent_path = "/winbar/bluetooth";
    DBusError error;
    dbus_error_init(&error);
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         "/org/bluez",
                                                         "org.bluez.AgentManager1",
                                                         "UnregisterAgent");
    defer(dbus_message_unref(dbus_msg));
    dbus_message_append_args(dbus_msg, DBUS_TYPE_OBJECT_PATH, &agent_path, DBUS_TYPE_INVALID);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, dbus_msg, 500, &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "Failed to unregister agent: " << error.message << std::endl;
        dbus_error_free(&error);
        return;
    }
    defer(dbus_message_unref(reply));
}

bool become_default_bluetooth_agent() {
    if (dbus_connection_system == nullptr) return false;
    if (!registered_with_bluez) return false;
    
    const char *agent_path = "/winbar/bluetooth";
    DBusError error;
    dbus_error_init(&error);
    
    // become default agent by callilng RequestDefaultAgent
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         "/org/bluez",
                                                         "org.bluez.AgentManager1",
                                                         "RequestDefaultAgent");
    defer(dbus_message_unref(dbus_msg));
    
    dbus_message_append_args(dbus_msg, DBUS_TYPE_OBJECT_PATH, &agent_path, DBUS_TYPE_INVALID);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, dbus_msg, 500, &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "Failed to become default agent: " << error.message << std::endl;
        dbus_error_free(&error);
        return false;
    }
    defer(dbus_message_unref(reply));
    return true;
}

#include <sys/select.h>
#include <unistd.h>
#include <cerrno>
#include <atomic>

ssize_t read_with_timeout(int fd, void *buf, size_t count, int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
    if (ret == -1) {
        // select failed
        return -1;
    } else if (ret == 0) {
        // timeout occurred
        errno = ETIMEDOUT;
        return -1;
    } else {
        // pipe is readable, perform the read
        return read(fd, buf, count);
    }
}

DBusHandlerResult bluetooth_agent_message(DBusConnection *conn, DBusMessage *message, void *user_data) {
    if (dbus_message_is_method_call(message, "org.bluez.Agent1", "Release")) {
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "RequestPinCode")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        
        auto br = new BluetoothRequest(conn, message, "RequestPinCode");
        br->object_path = device_object_path;
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "DisplayPinCode")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        
        dbus_message_iter_next(&args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *pin_code;
        dbus_message_iter_get_basic(&args, &pin_code);
        
        auto br = new BluetoothRequest(conn, message, "DisplayPinCode");
        br->object_path = device_object_path;
        br->pin = pin_code;
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "RequestPasskey")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        
        auto br = new BluetoothRequest(conn, message, "RequestPasskey");
        br->object_path = device_object_path;
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "DisplayPasskey")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        dbus_message_iter_next(&args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        uint32_t passkey = 0;
        dbus_message_iter_get_basic(&args, &passkey);
        dbus_message_iter_next(&args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT16)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        uint16_t entered = 0;
        dbus_message_iter_get_basic(&args, &entered);
        
        auto br = new BluetoothRequest(conn, message, "DisplayPasskey");
        br->object_path = device_object_path;
        br->pin = std::to_string(passkey);
        br->pin.insert(0, 6 - br->pin.length(), '0');
        
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "RequestConfirmation")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        dbus_message_iter_next(&args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        uint32_t passkey = 0;
        dbus_message_iter_get_basic(&args, &passkey);
        
        auto br = new BluetoothRequest(conn, message, "RequestConfirmation");
        br->object_path = device_object_path;
        br->pin = std::to_string(passkey);
        br->pin.insert(0, 6 - br->pin.length(), '0');
        
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "RequestAuthorization")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
        char const *device_object_path;
        dbus_message_iter_get_basic(&args, &device_object_path);
        
        auto br = new BluetoothRequest(conn, message, "RequestAuthorization");
        br->object_path = device_object_path;
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "AuthorizeService")) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        dbus_connection_send(conn, reply, nullptr);
        dbus_connection_flush(conn);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.bluez.Agent1", "Cancel")) {
        auto br = new BluetoothRequest(conn, message, "Cancelled");
        
        bluetooth_wants_response_from_user(br);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void register_agent_if_needed() {
    if (dbus_connection_system == nullptr) return;
    if (registered_bluetooth_agent) return;
    
    static const DBusObjectPathVTable agent_vtable = {
            .message_function = &bluetooth_agent_message,
    };
    
    if (!dbus_connection_register_object_path(dbus_connection_system, "/winbar/bluetooth", &agent_vtable, nullptr)) {
        fprintf(stdout, "%s\n",
                "Error registering object path /winbar/bluetooth on NameAcquired signal");
        return;
    } else {
        registered_bluetooth_agent = true;
    }
    
    const char *agent_path = "/winbar/bluetooth";
    DBusError error;
    dbus_error_init(&error);
    
    {
        DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                             "/org/bluez",
                                                             "org.bluez.AgentManager1",
                                                             "RegisterAgent");
        defer(dbus_message_unref(dbus_msg));
        
        const char *capability = "KeyboardDisplay";
        dbus_message_append_args(dbus_msg, DBUS_TYPE_OBJECT_PATH, &agent_path, DBUS_TYPE_STRING, &capability,
                                 DBUS_TYPE_INVALID);
        
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, dbus_msg, 500, &error);
        if (dbus_error_is_set(&error)) {
            std::cerr << "Failed to register agent: " << error.message << std::endl;
            dbus_error_free(&error);
            return;
        }
        defer(dbus_message_unref(reply));
    }
    registered_with_bluez = true;
    
    become_default_bluetooth_agent();
    
    // add dbus_bus_add_match InterfaceAdded, InterfaceRemoved
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',"
                       "sender='org.bluez',"
                       "interface='org.freedesktop.DBus.ObjectManager',"
                       "member='InterfacesAdded'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal InterfacesAdded because: %s\n%s\n",
                error.name, error.message);
    }
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',"
                       "sender='org.bluez',"
                       "interface='org.freedesktop.DBus.ObjectManager',"
                       "member='InterfacesRemoved'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal InterfacesRemoved because: %s\n%s\n",
                error.name, error.message);
    }
    
    update_devices();
}

void unregister_agent_if_needed() {
    if (dbus_connection_system == nullptr) return;
    if (registered_bluetooth_agent) {
        dbus_connection_unregister_object_path(dbus_connection_system, "/winbar/bluetooth");
        registered_bluetooth_agent = false;
        if (registered_with_bluez) {
            // UnregisterAgent
            unregister_agent_with_bluez();
            
            // remove interfaces
            for (auto &interface: bluetooth_interfaces) {
                DBusError error;
                dbus_error_init(&error);
                dbus_bus_remove_match(dbus_connection_system,
                                      ("type='signal',"
                                       "sender='org.bluez',"
                                       "interface='org.freedesktop.DBus.Properties',"
                                       "member='PropertiesChanged',"
                                       "path='" + std::string(interface->object_path) + "'").c_str(),
                                      &error);
                delete interface;
            }
            bluetooth_interfaces.clear();
        }
    }
}

void update_upower_battery();

void parse_and_add_or_update_interface(DBusMessageIter iter2) {
    char *object_path;
    if (dbus_message_iter_get_arg_type(&iter2) == DBUS_TYPE_OBJECT_PATH) {
        dbus_message_iter_get_basic(&iter2, &object_path);
    }
    
    dbus_message_iter_next(&iter2);
    
    while (dbus_message_iter_get_arg_type(&iter2) == DBUS_TYPE_ARRAY) {
        DBusMessageIter iter3;
        dbus_message_iter_recurse(&iter2, &iter3);
        
        while (dbus_message_iter_get_arg_type(&iter3) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter iter4;
            dbus_message_iter_recurse(&iter3, &iter4);
            
            char *iter4s;
            bool is_adapter = false;
            bool is_device = false;
            if (dbus_message_iter_get_arg_type(&iter4) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&iter4, &iter4s);
                is_adapter = strcmp(iter4s, "org.bluez.Adapter1") == 0;
                is_device = strcmp(iter4s, "org.bluez.Device1") == 0;
                
                if (!is_device && !is_adapter) {
                    dbus_message_iter_next(&iter3);
                    continue;
                }
            }
            
            // if interface already added, just modify values, otherwise create a new interface
            BluetoothInterface *interface = nullptr;
            for (auto i: bluetooth_interfaces)
                if (i->object_path == object_path)
                    interface = i;
            
            if (interface == nullptr) {
                if (is_adapter) {
                    interface = new Adapter(object_path);
                } else if (is_device) {
                    interface = new Device(object_path);
                }
                
                // Watch the PropertiesChanged signal
                DBusError error;
                dbus_error_init(&error);
                dbus_bus_add_match(dbus_connection_system,
                                   ("type='signal',"
                                    "sender='org.bluez',"
                                    "interface='org.freedesktop.DBus.Properties',"
                                    "member='PropertiesChanged',"
                                    "path='" + std::string(object_path) + "'").c_str(),
                                   &error);
                if (dbus_error_is_set(&error)) {
                    fprintf(stderr, "Couldn't watch signal PropertiesChanged because: %s\n%s\n",
                            error.name, error.message);
                }
    
                bluetooth_interfaces.push_back(interface);
    
                for (const auto &service: running_dbus_services) {
                    if (service == "org.freedesktop.UPower") {
                        update_upower_battery();
                    }
                }
            }
            
            dbus_message_iter_next(&iter4);
            
            while (dbus_message_iter_get_arg_type(&iter4) == DBUS_TYPE_ARRAY) {
                DBusMessageIter iter5;
                dbus_message_iter_recurse(&iter4, &iter5);
                
                while (dbus_message_iter_get_arg_type(&iter5) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter iter6;
                    dbus_message_iter_recurse(&iter5, &iter6);
                    
                    char *iter6s;
                    if (dbus_message_iter_get_arg_type(&iter6) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&iter6, &iter6s);
                    }
                    dbus_message_iter_next(&iter6);
                    
                    if (dbus_message_iter_get_arg_type(&iter6) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter iter7;
                        dbus_message_iter_recurse(&iter6, &iter7);
                        
                        if (strcmp(iter6s, "Address") == 0) {
                            char *iter7s;
                            dbus_message_iter_get_basic(&iter7, &iter7s);
                            interface->mac_address = iter7s;
                        } else if (strcmp(iter6s, "Name") == 0) {
                            char *iter7s;
                            dbus_message_iter_get_basic(&iter7, &iter7s);
                            interface->name = iter7s;
                        } else if (strcmp(iter6s, "Alias") == 0) {
                            char *iter7s;
                            dbus_message_iter_get_basic(&iter7, &iter7s);
                            interface->alias = iter7s;
                        } else if (interface->type == BluetoothInterfaceType::Adapter) {
                            if (strcmp(iter6s, "Powered") == 0) {
                                bool iter7b;
                                dbus_message_iter_get_basic(&iter7, &iter7b);
                                ((Adapter *) interface)->powered = iter7b;
                            }
                        } else if (interface->type == BluetoothInterfaceType::Device) {
                            if (strcmp(iter6s, "Icon") == 0) {
                                char *iter7s;
                                dbus_message_iter_get_basic(&iter7, &iter7s);
                                ((Device *) interface)->icon = iter7s;
                            } else if (strcmp(iter6s, "Adapter") == 0) {
                                char *iter7s;
                                dbus_message_iter_get_basic(&iter7, &iter7s);
                                ((Device *) interface)->adapter = iter7s;
                            } else if (strcmp(iter6s, "Paired") == 0) {
                                bool iter7b;
                                dbus_message_iter_get_basic(&iter7, &iter7b);
                                ((Device *) interface)->paired = iter7b;
                            } else if (strcmp(iter6s, "Connected") == 0) {
                                bool iter7b;
                                dbus_message_iter_get_basic(&iter7, &iter7b);
                                ((Device *) interface)->connected = iter7b;
                            } else if (strcmp(iter6s, "Bonded") == 0) {
                                bool iter7b;
                                dbus_message_iter_get_basic(&iter7, &iter7b);
                                ((Device *) interface)->bonded = iter7b;
                            } else if (strcmp(iter6s, "Trusted") == 0) {
                                bool iter7b;
                                dbus_message_iter_get_basic(&iter7, &iter7b);
                                ((Device *) interface)->trusted = iter7b;
                            }
                        }
                        
                        dbus_message_iter_next(&iter6);
                    } else if (dbus_message_iter_get_arg_type(&iter6) == DBUS_TYPE_ARRAY) {
                        // do nothing for now
                    }
                    dbus_message_iter_next(&iter5);
                }
                dbus_message_iter_next(&iter4);
            }
            dbus_message_iter_next(&iter3);
        }
        dbus_message_iter_next(&iter2);
    }
}

static void on_get_managed_objects_response(DBusPendingCall *call, void *) {
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    
    if (dbus_message_get_type(dbus_reply) == DBUS_MESSAGE_TYPE_ERROR) {
        fprintf(stderr, "Error getting managed objects: %s\n", dbus_message_get_error_name(dbus_reply));
        return;
    } else {
        DBusMessageIter iter0;
        if (!dbus_message_iter_init(dbus_reply, &iter0)) return;
        
        while (dbus_message_iter_get_arg_type(&iter0) == DBUS_TYPE_ARRAY) {
            DBusMessageIter iter1;
            dbus_message_iter_recurse(&iter0, &iter1);
            
            while (dbus_message_iter_get_arg_type(&iter1) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter iter2;
                dbus_message_iter_recurse(&iter1, &iter2);
                
                parse_and_add_or_update_interface(iter2);
                
                dbus_message_iter_next(&iter1);
            }
            
            dbus_message_iter_next(&iter0);
        }
    }
}

void update_devices() {
    // Call GetManagedObjects
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         "/",
                                                         "org.freedesktop.DBus.ObjectManager",
                                                         "GetManagedObjects");
    defer(dbus_message_unref(dbus_msg));
    
    DBusError error;
    dbus_error_init(&error);
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    if (!dbus_connection_send_with_reply(dbus_connection_system, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }
    if (!dbus_pending_call_set_notify(pending, on_get_managed_objects_response, nullptr, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }
}

// remove trailing new lines and whitespace
static std::string
trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

static void general_response(DBusPendingCall *call, void *user_data) {
    // cast user_data to void (*function)(bool, std::string)
    auto callback_info = (BluetoothCallbackInfo *) user_data;
    defer(delete callback_info);
    
    DBusMessage *dbus_reply = dbus_pending_call_steal_reply(call);
    defer(dbus_message_unref(dbus_reply));
    
    if (dbus_message_get_type(dbus_reply) == DBUS_MESSAGE_TYPE_ERROR) {
        fprintf(stderr, "Error setting power: %s\n", dbus_message_get_error_name(dbus_reply));
        if (callback_info->function) {
            callback_info->succeeded = false;
            callback_info->message = dbus_message_get_error_name(dbus_reply);
            DBusMessageIter iter;
            if (dbus_message_iter_init(dbus_reply, &iter)) {
                if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                    char *error_message;
                    dbus_message_iter_get_basic(&iter, &error_message);
                    callback_info->message += ": ";
                    callback_info->message += error_message;
                    callback_info->message = trim(callback_info->message);
                }
            }
            callback_info->function(callback_info);
        }
    } else {
        if (callback_info->function) {
            callback_info->succeeded = true;
            callback_info->function(callback_info);
        }
    }
}

void submit_message(DBusMessage *dbus_msg, BluetoothCallbackInfo *callback_info) {
    DBusError error;
    dbus_error_init(&error);
    
    DBusPendingCall *pending = nullptr;
    defer(dbus_pending_call_unref(pending));
    if (!dbus_connection_send_with_reply(dbus_connection_system, dbus_msg, &pending,
                                         DBUS_TIMEOUT_USE_DEFAULT)) {
        fprintf(stderr, "Not enough memory available to create message for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }
    if (!dbus_pending_call_set_notify(pending, general_response, (void *) callback_info, nullptr)) {
        fprintf(stderr, "Not enough memory available to set notification function for interface: %s\n",
                dbus_message_get_interface(dbus_msg));
        return;
    }
}

void set_bool_property(bool from_device, const std::string &object_path, const std::string &property_name, bool value,
                       BluetoothCallbackInfo *callback_info) {
    // Set the Connected property to true
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.freedesktop.DBus.Properties",
                                                         "Set");
    defer(dbus_message_unref(dbus_msg));
    
    // Append String, String, Variant Bool
    DBusMessageIter iter0;
    dbus_message_iter_init_append(dbus_msg, &iter0);
    
    const char *device_interface = "org.bluez.Device1";
    const char *adapter_interface = "org.bluez.Adapter1";
    if (from_device)
        dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &device_interface);
    else
        dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &adapter_interface);
    
    const char *property = property_name.c_str();
    dbus_message_iter_append_basic(&iter0, DBUS_TYPE_STRING, &property);
    
    DBusMessageIter iter1;
    dbus_message_iter_open_container(&iter0, DBUS_TYPE_VARIANT, "b", &iter1);
    dbus_bool_t val = value;
    dbus_message_iter_append_basic(&iter1, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&iter0, &iter1);
    
    submit_message(dbus_msg, callback_info);
}

void Device::connect(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Device1",
                                                         "Connect");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Connect", function));
}

void Device::disconnect(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Device1",
                                                         "Disconnect");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Disconnect", function));
}

void Device::trust(void (*function)(BluetoothCallbackInfo *)) {
    set_bool_property(true, object_path, "Trusted", true, new BluetoothCallbackInfo(this, "Trust", function));
}

void Device::untrust(void (*function)(BluetoothCallbackInfo *)) {
    set_bool_property(true, object_path, "Trusted", false, new BluetoothCallbackInfo(this, "Untrust", function));
}

void Device::pair(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Device1",
                                                         "Pair");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Pair", function));
}

void Device::cancel_pair(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Device1",
                                                         "CancelPairing");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Cancel Pair", function));
}

void Device::unpair(void (*function)(BluetoothCallbackInfo *)) {
    for (auto &interface: bluetooth_interfaces) {
        if (interface->object_path == this->adapter) {
            // Unpair message
            DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                                 interface->object_path.c_str(),
                                                                 "org.bluez.Adapter1",
                                                                 "RemoveDevice");
            defer(dbus_message_unref(dbus_msg));
            
            // Append String
            DBusMessageIter iter0;
            dbus_message_iter_init_append(dbus_msg, &iter0);
            const char *device = this->object_path.c_str();
            dbus_message_iter_append_basic(&iter0, DBUS_TYPE_OBJECT_PATH, &device);
            
            submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Unpair", function));
            break;
        }
    }
}

void Adapter::scan_on(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Adapter1",
                                                         "StartDiscovery");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Scan On", function));
}

void Adapter::scan_off(void (*function)(BluetoothCallbackInfo *)) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.bluez",
                                                         object_path.c_str(),
                                                         "org.bluez.Adapter1",
                                                         "StopDiscovery");
    defer(dbus_message_unref(dbus_msg));
    submit_message(dbus_msg, new BluetoothCallbackInfo(this, "Scan Off", function));
}

void Adapter::power_on(void (*function)(BluetoothCallbackInfo *)) {
    set_bool_property(false, object_path, "Powered", true, new BluetoothCallbackInfo(this, "Power On", function));
}

void Adapter::power_off(void (*function)(BluetoothCallbackInfo *)) {
    set_bool_property(false, object_path, "Powered", false, new BluetoothCallbackInfo(this, "Power Off", function));
}

void (*on_any_bluetooth_property_changed)() = nullptr;

BluetoothCallbackInfo::BluetoothCallbackInfo(BluetoothInterface *blue_interface, std::string command,
                                             void (*function)(BluetoothCallbackInfo *)) {
    this->mac_address = blue_interface->mac_address;
    this->command = command;
    this->function = function;
}

/*************************************************
 *
 * Talking with org.freedesktop.UPower
 *
 *************************************************/

bool iequals(const std::string &a, const std::string &b) {
    unsigned int sz = a.size();
    if (b.size() != sz)
        return false;
    for (unsigned int i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

void update_object_path(const std::string &object_path) {
    DBusMessage *get_serial_msg = dbus_message_new_method_call("org.freedesktop.UPower",
                                                               object_path.c_str(),
                                                               "org.freedesktop.DBus.Properties",
                                                               "Get");
    defer(dbus_message_unref(get_serial_msg));
    
    // Append String
    DBusMessageIter iter3;
    dbus_message_iter_init_append(get_serial_msg, &iter3);
    const char *interface_name = "org.freedesktop.UPower.Device";
    dbus_message_iter_append_basic(&iter3, DBUS_TYPE_STRING, &interface_name);
    const char *serial_property = "Serial";
    dbus_message_iter_append_basic(&iter3, DBUS_TYPE_STRING, &serial_property);
    
    // Send the message
    DBusMessage *serial_reply = dbus_connection_send_with_reply_and_block(dbus_connection_system,
                                                                          get_serial_msg, 500, NULL);
    defer(dbus_message_unref(serial_reply));
    
    // Get the serial in serial_reply
    DBusMessageIter iter4;
    dbus_message_iter_init(serial_reply, &iter4);
    
    DBusMessageIter iter5;
    dbus_message_iter_recurse(&iter4, &iter5);
    
    const char *serial;
    dbus_message_iter_get_basic(&iter5, &serial);
    
    for (auto interface: bluetooth_interfaces) {
        if (interface->type == BluetoothInterfaceType::Device) {
            auto device = (Device *) interface;
            if (device->mac_address.empty())
                continue;
            if (iequals(device->mac_address, serial)) {
                if (device->upower_path.empty()) {
                    // Watch signal PropertiesChanged
                    dbus_bus_add_match(dbus_connection_system, std::string("type='signal',"
                                                                           "interface='org.freedesktop.DBus.Properties',"
                                                                           "member='PropertiesChanged',"
                                                                           "path='" + std::string(object_path) +
                                                                           "'").c_str(), NULL);
                }
                device->upower_path = object_path;
                
                // Get the battery level
                DBusMessage *something = dbus_message_new_method_call("org.freedesktop.UPower",
                                                                      object_path.c_str(),
                                                                      "org.freedesktop.DBus.Properties",
                                                                      "Get");
                defer(dbus_message_unref(something));
                
                // Append String
                DBusMessageIter iter6;
                dbus_message_iter_init_append(something, &iter6);
                dbus_message_iter_append_basic(&iter6, DBUS_TYPE_STRING, &interface_name);
                const char *percentage_property = "Percentage";
                dbus_message_iter_append_basic(&iter6, DBUS_TYPE_STRING, &percentage_property);
                
                // Send the message
                DBusMessage *reply = dbus_connection_send_with_reply_and_block(dbus_connection_system,
                                                                               something,
                                                                               500, NULL);
                
                // Get the battery level
                DBusMessageIter iter7;
                dbus_message_iter_init(reply, &iter7);
                DBusMessageIter iter8;
                dbus_message_iter_recurse(&iter7, &iter8);
                double battery_level;
                dbus_message_iter_get_basic(&iter8, &battery_level);
                
                device->percentage = std::to_string(battery_level);
            }
        }
    }
}

static DBusHandlerResult upower_device_signal_filter(DBusConnection *dbus_connection,
                                                     DBusMessage *message, void *user_data) {
    // Check if it's added signal or removed signal
    if (dbus_message_is_signal(message, "org.freedesktop.UPower", "DeviceAdded")) {
        // Get the object path
        DBusMessageIter iter;
        dbus_message_iter_init(message, &iter);
        const char *object_path;
        dbus_message_iter_get_basic(&iter, &object_path);
        
        update_object_path(object_path);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.UPower", "DeviceRemoved")) {
        // Get the object path
        DBusMessageIter iter;
        dbus_message_iter_init(message, &iter);
        const char *object_path;
        dbus_message_iter_get_basic(&iter, &object_path);
        
        for (auto interface: bluetooth_interfaces) {
            if (interface->type == BluetoothInterfaceType::Device) {
                auto device = (Device *) interface;
                if (device->upower_path == object_path) {
                    device->upower_path = "";
                    device->percentage = "";
                }
            }
        }
        
        dbus_bus_remove_match(dbus_connection_system, std::string("type='signal',"
                                                                  "interface='org.freedesktop.DBus.Properties',"
                                                                  "member='PropertiesChanged',"
                                                                  "path='" + std::string(object_path) +
                                                                  "'").c_str(), NULL);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        for (auto *interface: bluetooth_interfaces) {
            if (dbus_message_has_path(message, interface->upower_path.c_str())) {
                DBusMessageIter args;
                dbus_message_iter_init(message, &args);
                
                if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
                    dbus_message_iter_next(&args);
                    
                    while (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
                        DBusMessageIter array;
                        dbus_message_iter_recurse(&args, &array);
                        
                        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY) {
                            DBusMessageIter dict;
                            dbus_message_iter_recurse(&array, &dict);
                            
                            char *key = nullptr;
                            if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRING)
                                dbus_message_iter_get_basic(&dict, &key);
                            dbus_message_iter_next(&dict);
                            
                            if (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_VARIANT) {
                                DBusMessageIter variant;
                                dbus_message_iter_recurse(&dict, &variant);
                                
                                if (strcmp(key, "Percentage") == 0) {
                                    if (interface->type == BluetoothInterfaceType::Device) {
                                        auto device = (Device *) interface;
                                        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_DOUBLE) {
                                            double percentage;
                                            dbus_message_iter_get_basic(&variant, &percentage);
                                            device->percentage = std::to_string(percentage);
                                        } else if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                                            const char *percentage;
                                            dbus_message_iter_get_basic(&variant, &percentage);
                                            device->percentage = percentage;
                                        }
                                    }
                                }
                            }
                            dbus_message_iter_next(&array);
                        }
                        dbus_message_iter_next(&args);
                    }
                }
                
                return DBUS_HANDLER_RESULT_HANDLED;
            }
        }
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void upower_service_ended() {
    for (auto *interface: bluetooth_interfaces) {
        if (interface->type == BluetoothInterfaceType::Device) {
            ((Device *) interface)->percentage = "";
        }
    }
    
    // remove deviced added/removed
    dbus_bus_remove_match(dbus_connection_system, "type='signal',"
                                                  "interface='org.freedesktop.UPower',"
                                                  "member='DeviceAdded'", NULL);
    dbus_bus_remove_match(dbus_connection_system, "type='signal',"
                                                  "interface='org.freedesktop.UPower',"
                                                  "member='DeviceRemoved'", NULL);
    
    // remove match
    for (auto *device: bluetooth_interfaces) {
        if (device->upower_path.empty())
            continue;
        dbus_bus_remove_match(dbus_connection_system, std::string("type='signal',"
                                                                  "interface='org.freedesktop.DBus.Properties',"
                                                                  "member='PropertiesChanged',"
                                                                  "path='" + device->upower_path +
                                                                  "'").c_str(), NULL);
    }
}

void update_upower_battery() {
    DBusMessage *get_devices_msg = dbus_message_new_method_call("org.freedesktop.UPower",
                                                                "/org/freedesktop/UPower",
                                                                "org.freedesktop.UPower",
                                                                "EnumerateDevices");
    defer(dbus_message_unref(get_devices_msg));
    
    DBusMessage *devices_reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, get_devices_msg, 500,
                                                                           NULL);
    defer(dbus_message_unref(devices_reply));
    
    DBusMessageIter iter0;
    dbus_message_iter_init(devices_reply, &iter0);
    
    if (dbus_message_iter_get_arg_type(&iter0) == DBUS_TYPE_ARRAY) {
        DBusMessageIter iter1;
        dbus_message_iter_recurse(&iter0, &iter1);
        
        while (dbus_message_iter_get_arg_type(&iter1) == DBUS_TYPE_OBJECT_PATH) {
            const char *object_path;
            dbus_message_iter_get_basic(&iter1, &object_path);
            
            update_object_path(object_path);
            
            dbus_message_iter_next(&iter1);
        }
        
    }
}

void upower_service_started() {
    update_upower_battery();
    
    // Register upower device added signal and device removed signal
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',interface='org.freedesktop.UPower',member='DeviceAdded'",
                       NULL);
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',interface='org.freedesktop.UPower',member='DeviceRemoved'",
                       NULL);
    dbus_connection_add_filter(dbus_connection_system, upower_device_signal_filter, NULL, NULL);
}


/*************************************************
 *
 * Talking with org.kde.StatusNotifierItem since org.freedesktop.StatusNotifierItem is not ACTUALLY USED by anyone
 *
 *************************************************/

static const char *status_notifier_watcher_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<node name=\"/StatusNotifierWatcher\">\n"
        "<interface name=\"org.kde.StatusNotifierWatcher\">\n"
        "\n"
        "    <!-- methods -->\n"
        "    <method name=\"RegisterStatusNotifierItem\">\n"
        "        <arg name=\"service\" type=\"s\" direction=\"in\"/>\n"
        "    </method>\n"
        "\n"
        "    <method name=\"RegisterStatusNotifierHost\">\n"
        "        <arg name=\"service\" type=\"s\" direction=\"in\"/>\n"
        "    </method>\n"
        "\n"
        "\n"
        "    <!-- properties -->\n"
        "\n"
        "    <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\">\n"
        "        <annotation name=\"org.qtproject.QtDBus.QtTypeName.Out0\" value=\"QStringList\"/>\n"
        "    </property>\n"
        "\n"
        "    <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>\n"
        "\n"
        "    <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>\n"
        "\n"
        "\n"
        "    <!-- signals -->\n"
        "\n"
        "    <signal name=\"StatusNotifierItemRegistered\">\n"
        "        <arg type=\"s\"/>\n"
        "    </signal>\n"
        "\n"
        "    <signal name=\"StatusNotifierItemUnregistered\">\n"
        "        <arg type=\"s\"/>\n"
        "    </signal>\n"
        "\n"
        "    <signal name=\"StatusNotifierHostRegistered\">\n"
        "    </signal>\n"
        "\n"
        "    <signal name=\"StatusNotifierHostUnregistered\">\n"
        "    </signal>\n"
        "</interface>\n"
        "</node>";

DBusHandlerResult sni_message(DBusConnection *conn, DBusMessage *message, void *user_data) {
    printf("SNI message: %s, %s, %s\n", dbus_message_get_interface(message), dbus_message_get_member(message),
           dbus_message_get_path(message));
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        defer(dbus_message_unref(reply));
        
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &status_notifier_watcher_xml, DBUS_TYPE_INVALID);
        dbus_connection_send(conn, reply, NULL);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get")) {
        const char *interface_name_char;
        const char *property_name_char;
        dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &interface_name_char, DBUS_TYPE_STRING,
                              &property_name_char, DBUS_TYPE_INVALID);
        std::string property_name = property_name_char;
        printf("Property: %s\n", property_name.c_str());
        
        if (property_name == "ProtocolVersion" || property_name == "IsStatusNotifierHostRegistered" ||
            property_name == "RegisteredStatusNotifierItems") {
            if (property_name == "ProtocolVersion") {
                DBusMessage *reply = dbus_message_new_method_return(message);
                defer(dbus_message_unref(reply));
                
                dbus_int32_t version = 42;
                
                dbus_message_append_args(reply, DBUS_TYPE_UINT32, &version, DBUS_TYPE_INVALID);
                dbus_connection_send(conn, reply, NULL);
            } else if (property_name == "IsStatusNotifierHostRegistered") {
                // Since Winbar is a StatusNotifierHost, this is always true if we are running
                // reply boolean true
                DBusMessage *reply = dbus_message_new_method_return(message);
                defer(dbus_message_unref(reply));
                
                dbus_bool_t is_registered = true;
                dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &is_registered, DBUS_TYPE_INVALID);
                dbus_connection_send(conn, reply, NULL);
            } else if (property_name == "RegisteredStatusNotifierItems") {
                // reply with array of strings
                DBusMessage *reply = dbus_message_new_method_return(message);
                
                DBusMessageIter iter;
                dbus_message_iter_init_append(reply, &iter);
                
                DBusMessageIter array_iter;
                dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array_iter);
                
                for (auto &item: status_notifier_items) {
                    dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &item);
                }
                
                dbus_message_iter_close_container(&iter, &array_iter);
                
                dbus_connection_send(conn, reply, NULL);
                dbus_message_unref(reply);
            }
            
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    } else if (dbus_message_is_method_call(message, "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem")) {
        const char *service_name;
        dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &service_name, DBUS_TYPE_INVALID);
        
        std::string fixed_name(service_name);
        
        if (fixed_name[0] == '/') {
            // Get the name of the service or the bus address of the remote method call.
            const char *sender = dbus_message_get_sender(message);
            if (sender) {
                fixed_name = sender;
            }
        }
        
        // TODO: we need the below code to know how to call the services
//        if (serviceOrPath.startsWith('/')) {
//            service = message().service();
//            path = serviceOrPath;
//        } else {
//            service = serviceOrPath;
//            path = "/StatusNotifierItem";
//        }
        
        status_notifier_items.emplace_back(fixed_name);
        StatusNotifierItemRegistered(fixed_name);
        
        DBusMessage *reply = dbus_message_new_method_return(message);
        defer(dbus_message_unref(reply));
        
        dbus_connection_send(conn, reply, NULL);
        
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_method_call(message, "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierHost")) {
        // We don't care about watching other StatusNotifierHost's come online since Winbar is one.
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void create_status_notifier_watcher() {
    static const DBusObjectPathVTable notifier_watcher_vtable = {
            .message_function = &sni_message,
    };
    
    std::string status_notifier_watcher_service = std::string("/StatusNotifierWatcher");
    
    if (!dbus_connection_register_object_path(dbus_connection_session, status_notifier_watcher_service.c_str(),
                                              &notifier_watcher_vtable,
                                              nullptr)) {
        fprintf(stdout, "%s\n",
                "Error registering object path /StatusNotifierWatcher on NameAcquired signal");
        return;
    }
    
    DBusError error;
    dbus_error_init(&error);
    int result = dbus_bus_request_name(dbus_connection_session, "org.kde.StatusNotifierWatcher",
                                       DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Ran into error when trying to become \"org.kde.StatusNotifierWatcher\" (%s)\n",
                error.message);
        dbus_error_free(&error);
        return;
    }
    
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to become StatusNotifierWatcher\n");
        return;
    }
    
    StatusNotifierHostRegistered();
}

void StatusNotifierItemRegistered(const std::string &service) {
    if (!dbus_connection_session)
        return;
    
    printf("Registering SNI: %s\n", service.c_str());
    
    DBusMessage *dmsg = dbus_message_new_signal("/StatusNotifierWatcher",
                                                "org.kde.StatusNotifierWatcher",
                                                "StatusNotifierItemRegistered");
    defer(dbus_message_unref(dmsg));
    dbus_message_set_destination(dmsg, NULL);
    dbus_message_append_args(dmsg, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
    dbus_connection_flush(dbus_connection_session);
}

void StatusNotifierItemUnregistered(const std::string &service) {
    if (!dbus_connection_session)
        return;
    
    printf("Unregistering SNI: %s\n", service.c_str());
    
    DBusMessage *dmsg = dbus_message_new_signal("/StatusNotifierWatcher",
                                                "org.kde.StatusNotifierWatcher",
                                                "StatusNotifierItemUnregistered");
    defer(dbus_message_unref(dmsg));
    dbus_message_set_destination(dmsg, NULL);
    dbus_message_append_args(dmsg, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
    dbus_connection_flush(dbus_connection_session);
}

void StatusNotifierHostRegistered() {
    if (!dbus_connection_session)
        return;
    
    DBusMessage *dmsg = dbus_message_new_signal("/StatusNotifierWatcher",
                                                "org.kde.StatusNotifierWatcher",
                                                "StatusNotifierHostRegistered");
    defer(dbus_message_unref(dmsg));
    
    dbus_message_set_destination(dmsg, NULL);
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
    dbus_connection_flush(dbus_connection_session);
}
