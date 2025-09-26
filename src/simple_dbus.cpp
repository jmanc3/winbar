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
#include "root.h"
#include "audio.h"
#include "settings_menu.h"
#include "wifi_backend.h"
#include "dbus_helper.h"

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
std::vector<std::string> status_notifier_hosts;
std::vector<BluetoothInterface *> bluetooth_interfaces;
bool bluetooth_running = false;

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

bool network_manager_running = false;
bool binded_network_manager = false;

void network_manager_service_started();

void network_manager_service_ended();

void nm_timeout_reload();

void StatusNotifierItemRegistered(const std::string &service);

void StatusNotifierItemUnregistered(const std::string &service);

void StatusNotifierHostRegistered();


Msg sys_method_call(std::string bus, std::string path, std::string iface, std::string method) {
    return method_call(dbus_connection_system, bus, path, iface, method);
}

Msg sess_method_call(std::string bus, std::string path, std::string iface, std::string method) {
    return method_call(dbus_connection_session, bus, path, iface, method);
}

void remove_service(const std::string &service) {
    for (int x = 0; x < status_notifier_items.size(); x++) {
        if (status_notifier_items[x] == service) {
            StatusNotifierItemUnregistered(service);
            status_notifier_items.erase(status_notifier_items.begin() + x);
        }
    }
    for (int i = 0; i < status_notifier_hosts.size(); i++) {
        if (status_notifier_hosts[i] == service) {
            status_notifier_hosts.erase(status_notifier_hosts.begin() + i);
        }
    }
    for (int i = 0; i < running_dbus_services.size(); i++) {
        if (running_dbus_services[i] == service) {
            if (running_dbus_services[i] == "local.org_kde_powerdevil")
                max_kde_brightness = 0;
            if (running_dbus_services[i] == "org.gnome.SettingsDaemon.Power")
                gnome_brightness_running = false;
            if (running_dbus_services[i] == "org.freedesktop.NetworkManager") {
                network_manager_running = false;
                network_manager_service_ended();
            }
            if (running_dbus_services[i] == "org.bluez") {
                bluetooth_service_ended();
                bluetooth_running = false;
            }
            if (running_dbus_services[i] == "org.freedesktop.UPower")
                upower_service_ended();
            
            running_dbus_services.erase(running_dbus_services.begin() + i);
            return;
        }
    }
}

DBusHandlerResult handle_message_cb(DBusConnection *connection, DBusMessage *message, void *userdata);

void create_status_notifier_watcher();

void create_status_notifier_host();

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
            if (std::string(name) == "org.freedesktop.NetworkManager") {
                network_manager_running = true;
                network_manager_service_started();
            }
            if (std::string(name) == "org.bluez") {
                bluetooth_service_started();
                bluetooth_running = true;
            }
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
        if (std::string(name) == "org.freedesktop.NetworkManager") {
            network_manager_running = true;
            network_manager_service_started();
        }
        if (std::string(name) == "org.bluez") {
            bluetooth_service_started();
            bluetooth_running = true;
        }
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
        // bluetooth or network manager has this signal
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        std::string mm;
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_OBJECT_PATH) {
            char *object_path;
            dbus_message_iter_get_basic(&args, &object_path);
            mm = object_path;
        }
        
        if (mm.find("/org/freedesktop/NetworkManager") != std::string::npos) {
            auto any = parse_iter_message(message);
            if (auto str = std::any_cast<std::string>(&any)) {
                nm_timeout_reload();
            }
        } else if (mm.find("/org/bluez") != std::string::npos) {
            DBusMessageIter args;
            dbus_message_iter_init(message, &args);
            parse_and_add_or_update_interface(args);
            if (on_any_bluetooth_property_changed) {
                on_any_bluetooth_property_changed();
            }
        }
    
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved")) {
        DBusMessageIter args;
        dbus_message_iter_init(message, &args);
        std::string mm;
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_OBJECT_PATH) {
            char *object_path;
            dbus_message_iter_get_basic(&args, &object_path);
            mm = object_path;
        }
        if (mm.find("/org/freedesktop/NetworkManager") != std::string::npos) {
            auto any = parse_iter_message(message);
            if (auto str = std::any_cast<std::string>(&any)) {
                nm_timeout_reload();
            }
        } else if (mm.find("/org/bluez") != std::string::npos) {
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
        }
    
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        auto str = dbus_message_get_path(message);
        if (std::string(str) == "/org/freedesktop/UPower/devices/DisplayDevice") {
            battery_display_device_state_changed();
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        
        if (std::string(str).find("/org/bluez/") == std::string::npos)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        
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
        if (std::string(name) == "org.freedesktop.NetworkManager") {
            network_manager_running = true;
            network_manager_service_started();
        }
        if (std::string(name) == "org.bluez") {
            bluetooth_service_started();
            bluetooth_running = true;
        }
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
    if (dbus_connection == dbus_connection_system) {
        dbus_bus_add_match(dbus_connection_system,
                           ("type='signal',"
                            "sender='org.freedesktop.UPower',"
                            "interface='org.freedesktop.DBus.Properties',"
                            "member='PropertiesChanged',"
                            "path='" + std::string("/org/freedesktop/UPower/devices/DisplayDevice") + "'").c_str(),
                           &error);
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

void highlight_windows(const std::vector<std::string> &windows) {
    // Submit an array of strings
    if (!dbus_connection_session) return;
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.KWin.HighlightWindow",
                                                         "/org/kde/KWin/HighlightWindow",
                                                         "org.kde.KWin.HighlightWindow",
                                                         "highlightWindows");
    defer(dbus_message_unref(dbus_msg));
    DBusMessageIter iter;
    dbus_message_iter_init_append(dbus_msg, &iter);
    
    DBusMessageIter array_iter;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array_iter);
    for (auto &item: windows)
        dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &item);
    dbus_message_iter_close_container(&iter, &array_iter);
    
    dbus_connection_send(dbus_connection_session, dbus_msg, NULL);
}

void highlight_window(std::string window_id_as_string) {
    std::vector<std::string> ids = {window_id_as_string};
    highlight_windows(ids);
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
        std::string summary_str = std::string(summary);
        if (summary_str == "winbaractivate" || summary_str == "winbarrun" || summary_str == "winbaraudio") {
            DBusMessage *reply = dbus_message_new_method_return(message);
            defer(dbus_message_unref(reply));
            if (summary_str == "winbaractivate") {
                meta_pressed(0);
            } else if (summary_str == "winbarrun") {
                start_run_window();
            } else {
                if (!audio_running)
                    audio_start(app);
            }
            dbus_message_iter_init_append(reply, &args);
            const dbus_uint32_t current_id = -1;
            if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &current_id) ||
                !dbus_connection_send(connection, reply, NULL)) {
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        
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
                    
                    NotificationAction notification_action;
                    notification_action.id = key;
                    std::string v(value);
                    bool only_whitespace = std::all_of(v.begin(), v.end(), isspace);
                    if (!only_whitespace) {
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
                client_create_animation(app, c, &data->slide_anim, data->lifetime, 0, 140, nullptr, 1);
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
    
            // create_status_notifier_watcher();
            // create_status_notifier_host();
        }
        
        dbus_poll_wakeup(nullptr, 0, dbus_connection);
    }
}

void dbus_end() {
    status_notifier_hosts.clear();
    status_notifier_hosts.shrink_to_fit();
    status_notifier_items.clear();
    status_notifier_items.shrink_to_fit();
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
        
        if (dbus_connection == dbus_connection_session) {
            dbus_bus_release_name(dbus_connection, "org.kde.StatusNotifierWatcher", &error);
            if (dbus_error_is_set(&error)) {
                fprintf(stderr, "Error releasing name: %s\n%s\n", error.name, error.message);
            }
            
            int pid = 23184910;
            auto host = std::string("org.kde.StatusNotifierHost-" + std::to_string(pid));
            dbus_bus_release_name(dbus_connection, host.c_str(), &error);
            if (dbus_error_is_set(&error)) {
                fprintf(stderr, "Error releasing name: %s\n%s\n", error.name, error.message);
            }
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

bool kde_shutdown_check_and_call(std::string func) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.Shutdown",
                                                         "/Shutdown",
                                                         "org.freedesktop.DBus.Introspectable",
                                                         "Introspect");
    defer(dbus_message_unref(dbus_msg));
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 4000,
                                                                        nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        const char *output;
        if (dbus_message_get_args(dbus_reply, NULL, DBUS_TYPE_STRING, &output, DBUS_TYPE_INVALID)) {
            std::string o(output);
            std::string key = "method name=\"";
            key += func;
            key += "\"";
            if (o.find(key) != std::string::npos) {
                DBusMessage *dbus_msg_logout = dbus_message_new_method_call("org.kde.Shutdown",
                                                                            "/Shutdown",
                                                                            "org.kde.Shutdown",
                                                                            func.c_str());
                defer(dbus_message_unref(dbus_msg_logout));
                DBusMessage *dbus_reply_logout = dbus_connection_send_with_reply_and_block(dbus_connection_session,
                                                                                           dbus_msg_logout, 4000,
                                                                                           nullptr);
                if (dbus_reply_logout) {
                    defer(dbus_message_unref(dbus_reply_logout));
                    
                    int dbus_result = 0;
                    if (::dbus_message_get_args(dbus_reply_logout, nullptr, DBUS_TYPE_INT32, &dbus_result,
                                                DBUS_TYPE_INVALID)) {
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

bool new_dbus_computer_logoff() {
    return kde_shutdown_check_and_call("logout");
}

bool gnome_logoff(std::string func) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.gnome.SessionManager",
                                                         "/org/gnome/SessionManager",
                                                         "org.freedesktop.DBus.Introspectable",
                                                         "Introspect");
    defer(dbus_message_unref(dbus_msg));
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 4000,
                                                                        nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        const char *output;
        if (dbus_message_get_args(dbus_reply, NULL, DBUS_TYPE_STRING, &output, DBUS_TYPE_INVALID)) {
            std::string o(output);
            std::string key = "method name=\"";
            key += func;
            key += "\"";
            if (o.find(key) != std::string::npos) {
                DBusMessage *dbus_msg_logout = dbus_message_new_method_call("org.gnome.SessionManager",
                                                                            "/org/gnome/SessionManager",
                                                                            "org.gnome.SessionManager",
                                                                            func.c_str());
                defer(dbus_message_unref(dbus_msg_logout));
                
                if (func == "Logout") {
                    // https://github.com/GNOME/gnome-settings-daemon/blob/master/gnome-settings-daemon/org.gnome.SessionManager.xml
                    const unsigned int no_prompt = 1;
                    if (!dbus_message_append_args(dbus_msg_logout, DBUS_TYPE_UINT32, &no_prompt, DBUS_TYPE_INVALID)) {
                        fprintf(stderr, "%s\n", "In \"gnome_logoff\" couldn't append an argument to the DBus message.");
                        return false;
                    }
                }
                
                
                DBusMessage *dbus_reply_logout = dbus_connection_send_with_reply_and_block(dbus_connection_session,
                                                                                           dbus_msg_logout, 4000,
                                                                                           nullptr);
                if (dbus_reply_logout) {
                    defer(dbus_message_unref(dbus_reply_logout));
                    
                    int dbus_result = 0;
                    if (::dbus_message_get_args(dbus_reply_logout, nullptr, DBUS_TYPE_INT32, &dbus_result,
                                                DBUS_TYPE_INVALID)) {
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

void dbus_computer_logoff() {
    if (!dbus_connection_session) return;
    
    if (new_dbus_computer_logoff()) return;
    
    if (gnome_logoff("Logout")) return;
    
    const char *desktopSession = std::getenv("DESKTOP_SESSION");
    if (desktopSession && std::strcmp(desktopSession, "openbox") == 0) {
        system("openbox --exit");
        return;
    }
    
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
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 1000,
                                                                        nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return;
    }
}

bool new_dbus_computer_shut_down() {
    return kde_shutdown_check_and_call("logoutAndShutdown");
}

bool kms_shutdown(int confirm, int shutdown_type, int shutdown_mode) {
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.kde.ksmserver",
                                                         "/KSMServer",
                                                         "org.kde.KSMServerInterface",
                                                         "logout");
    defer(dbus_message_unref(dbus_msg));
    
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &confirm, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return false;
    }
    
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_type, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return false;
    }
    
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_INT32, &shutdown_mode, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_computer_logout\" couldn't append an argument to the DBus message.");
        return false;
    }
    
    DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_session, dbus_msg, 1000, nullptr);
    if (dbus_reply) {
        defer(dbus_message_unref(dbus_reply));
        
        int dbus_result = 0;
        if (::dbus_message_get_args(dbus_reply, nullptr, DBUS_TYPE_INT32, &dbus_result, DBUS_TYPE_INVALID))
            return dbus_result;
    }
    return false;
}

// All from: https://www.reddit.com/r/kde/comments/70hnzg/command_to_properly_shutdownreboot_kde_machine/
void dbus_computer_shut_down() {
    if (!dbus_connection_session) {
        if (!winbar_settings->shutdown_command.empty())
            launch_command(winbar_settings->shutdown_command);
        return;
    }
    
    if (new_dbus_computer_shut_down()) return;
    
    if (gnome_logoff("Shutdown")) return;
    
    if (kms_shutdown(0, 2, 2)) return;
    
    if (!winbar_settings->shutdown_command.empty()) {
        launch_command(winbar_settings->shutdown_command);
    }
}

bool new_dbus_computer_restart() {
    return kde_shutdown_check_and_call("logoutAndReboot");
}

// All from: https://www.reddit.com/r/kde/comments/70hnzg/command_to_properly_shutdownreboot_kde_machine/
void dbus_computer_restart() {
    if (!dbus_connection_session) {
        if (!winbar_settings->restart_command.empty())
            launch_command(winbar_settings->restart_command);
        return;
    }
    
    if (new_dbus_computer_restart()) return;
    
    if (gnome_logoff("Reboot")) return;
    
    if (kms_shutdown(0, 1, 2)) return;
    
    if (!winbar_settings->restart_command.empty()) {
        launch_command(winbar_settings->restart_command);
    }
}

void dbus_open_in_folder(std::string path) {
    if (path.empty()) return;
    if (!dbus_connection_session) return;

    DBusMessage *dbus_msg = dbus_message_new_method_call("org.freedesktop.FileManager1",
                                                         "/org/freedesktop/FileManager1",
                                                         "org.freedesktop.FileManager1",
                                                         "ShowItems");
    defer(dbus_message_unref(dbus_msg));

    DBusMessageIter iter;
    dbus_message_iter_init_append(dbus_msg, &iter);

    DBusMessageIter array_iter;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array_iter);

    dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &path);

    dbus_message_iter_close_container(&iter, &array_iter);

    const char *id = "";
    if (!dbus_message_append_args(dbus_msg, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "%s\n", "In \"dbus_open_in_folder\" couldn't append an argument to the DBus message.");
        return;
    }

    dbus_connection_send(dbus_connection_session, dbus_msg, NULL);
}

void network_manager_service_ended() {
    if (wifi_data) {
        for (auto l: wifi_data->links)
            delete l;
        wifi_data->links.clear();
        if (wifi_data->when_state_changed) {
            wifi_data->when_state_changed();
        }
    }
    if (dbus_connection_system == nullptr) return;
    binded_network_manager = false;
}

void nm_timeout_reload() {
    static long start = app->current;
    start = app->current;
    for (auto t: app->timeouts)
        if (t->text == "network_manager_timeout")
            return;
    
    app_timeout_create(app, client_by_name(app, "taskbar"), 20, [](App *, AppClient *, Timeout *t, void *) {
        t->keep_running = true;
        if (app->current - start > 140) {
            t->keep_running = false;
            network_manager_service_get_all_devices();
        }
    }, nullptr, "network_manager_timeout");
}

static DBusHandlerResult network_manager_device_signal_filter(DBusConnection *dbus_connection,
                                                              DBusMessage *message, void *user_data) {
    if (dbus_message_is_signal(message, "org.freedesktop.DBus.Properties", "PropertiesChanged")) {
        if (std::string(dbus_message_get_path(message)) == "/org/freedesktop/NetworkManager") {
            auto any = parse_iter_message(message);
            if (auto dict = std::any_cast<DbusDict>(&any)) {
                for (const auto &item: dict->map) {
                    if (auto str = std::any_cast<std::string>(&item.first)) {
                        if (*str == "ActiveConnections" || *str == "State") {
                            nm_timeout_reload();
                        }
                    }
                }
            }
            
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    } else if (dbus_message_is_signal(message, "org.freedesktop.NetworkManager", "DeviceAdded")) {
        nm_timeout_reload();
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (dbus_message_is_signal(message, "org.freedesktop.NetworkManager", "DeviceRemoved")) {
        nm_timeout_reload();
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

DBusMessage *get_property(DBusConnection *connection, std::string bus_name, std::string path, std::string iface,
                          std::string property_name) {
    DBusMessage *get_serial_msg = dbus_message_new_method_call(bus_name.c_str(),
                                                               path.c_str(),
                                                               "org.freedesktop.DBus.Properties",
                                                               "Get");
    defer(dbus_message_unref(get_serial_msg));
    
    // Append String
    DBusMessageIter iter3;
    dbus_message_iter_init_append(get_serial_msg, &iter3);
    const char *interface_name = iface.c_str();
    dbus_message_iter_append_basic(&iter3, DBUS_TYPE_STRING, &interface_name);
    const char *property = property_name.c_str();
    dbus_message_iter_append_basic(&iter3, DBUS_TYPE_STRING, &property);
    
    DBusError error;
    dbus_error_init(&error);
    
    // Send the message
    DBusMessage *iface_reply = dbus_connection_send_with_reply_and_block(connection,
                                                                         get_serial_msg, 50, &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "failed get: %s, error: %s\n", property_name.c_str(), error.message);
        return nullptr;
    }
    return iface_reply;
}

DBusMessage *get_property(std::string bus_name, std::string path, std::string iface, std::string property_name) {
    return get_property(dbus_connection_system, bus_name, path, iface, property_name);
}

std::string get_str_property(std::string bus_name, std::string path, std::string iface, std::string property_name) {
    Msg reply = {get_property(bus_name, path, iface, property_name)};
    if (!reply.msg)
        return "";
    std::any val = parse_message(reply.msg);
    if (auto str = std::any_cast<std::string>(&val))
        return *str;
    return "";
}

std::string get_arr_b_property(std::string bus_name, std::string path, std::string iface, std::string property_name) {
    Msg reply = {get_property(bus_name, path, iface, property_name)};
    if (!reply.msg)
        return "";
    std::ostringstream oss;
    std::any val = parse_message(reply.msg);
    if (auto arr = std::any_cast<DbusArray>(&val))
        for (const auto &element: arr->elements)
            if (auto byte = std::any_cast<uint8_t>(element))
                oss << (char) (byte);
    return oss.str();
}

dbus_int64_t get_boottime_millis() {
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        perror("clock_gettime");
        return 0;
    }
    return (uint64_t(ts.tv_sec) * 1000) + (ts.tv_nsec / 1000000);
}

bool nm_scan_too_soon(std::string device_path) {
    dbus_int64_t time = get_num_property<dbus_int64_t>("org.freedesktop.NetworkManager", device_path.c_str(),
                                                       "org.freedesktop.NetworkManager.Device.Wireless", "LastScan");
    if (time == -1) // -1 means never scanned (or error)
        return false;
    
    dbus_int64_t delta_ms = get_boottime_millis() - time;
    return delta_ms < 60000;
}

void network_manager_request_scan(std::string device_path) {
    if (nm_scan_too_soon(device_path))
        return;
    
    DBusMessage *scan = dbus_message_new_method_call("org.freedesktop.NetworkManager",
                                                     device_path.c_str(),
                                                     "org.freedesktop.NetworkManager.Device.Wireless",
                                                     "RequestScan");
    defer(dbus_message_unref(scan));
    
    DBusMessageIter args;
    dbus_message_iter_init_append(scan, &args);
    DBusMessageIter dict_iter;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict_iter);
    
    dbus_message_iter_close_container(&args, &dict_iter);
    
    // Send the message
    DBusMessage *iface_reply = dbus_connection_send_with_reply_and_block(dbus_connection_system,
                                                                         scan, 50, NULL);
    if (iface_reply)
        dbus_message_unref(iface_reply);
}

enum WIFISecurityMode : uint8_t {
    WIFI_SECURITY_NONE          /* @text: NONE */,
    WIFI_SECURITY_WPA_PSK       /* @text: WPA-PSK */,
    WIFI_SECURITY_SAE           /* @text: SAE */,
    WIFI_SECURITY_EAP           /* @text: EAP */,
};

/*! Error code: A recoverable, unexpected error occurred,
 * as defined by one of the following values */
typedef enum _WiFiErrorCode_t {
    WIFI_SSID_CHANGED,              /**< The SSID of the network changed */
    WIFI_CONNECTION_LOST,           /**< The connection to the network was lost */
    WIFI_CONNECTION_FAILED,         /**< The connection failed for an unknown reason */
    WIFI_CONNECTION_INTERRUPTED,    /**< The connection was interrupted */
    WIFI_INVALID_CREDENTIALS,       /**< The connection failed due to invalid credentials */
    WIFI_NO_SSID,                   /**< The SSID does not exist */
    WIFI_UNKNOWN,                   /**< Any other error */
    WIFI_AUTH_FAILED                /**< The connection failed due to auth failure */
} WiFiErrorCode_t;

// Enum 80211ApFlags
enum NM80211ApFlags {
    NM_802_11_AP_FLAGS_NONE = 0,
    NM_802_11_AP_FLAGS_PRIVACY = 1,
    NM_802_11_AP_FLAGS_WPS = 2,
    NM_802_11_AP_FLAGS_WPS_PBC = 4,
    NM_802_11_AP_FLAGS_WPS_PIN = 8,
};

enum NM80211ApSecurityFlags {
    NM_802_11_AP_SEC_NONE = 0,
    NM_802_11_AP_SEC_PAIR_WEP40 = 1,
    NM_802_11_AP_SEC_PAIR_WEP104 = 2,
    NM_802_11_AP_SEC_PAIR_TKIP = 4,
    NM_802_11_AP_SEC_PAIR_CCMP = 8,
    NM_802_11_AP_SEC_GROUP_WEP40 = 16,
    NM_802_11_AP_SEC_GROUP_WEP104 = 32,
    NM_802_11_AP_SEC_GROUP_TKIP = 64,
    NM_802_11_AP_SEC_GROUP_CCMP = 128,
    NM_802_11_AP_SEC_KEY_MGMT_PSK = 256,
    NM_802_11_AP_SEC_KEY_MGMT_802_1X = 512,
    NM_802_11_AP_SEC_KEY_MGMT_SAE = 1024,
    NM_802_11_AP_SEC_KEY_MGMT_OWE = 2048,
    NM_802_11_AP_SEC_KEY_MGMT_OWE_TM = 4096,
    NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192 = 8192
};

// Function to convert percentage (0-100) to dBm string
const char *convertPercentageToSignalStrengtStr(int percentage) {
    
    if (percentage <= 0 || percentage > 100) {
        return "";
    }
    
    /*
     * -30 dBm to -50 dBm: Excellent signal strength.
     * -50 dBm to -60 dBm: Very good signal strength.
     * -60 dBm to -70 dBm: Good signal strength; acceptable for basic internet browsing.
     * -70 dBm to -80 dBm: Weak signal; performance may degrade, slower speeds, and possible dropouts.
     * -80 dBm to -90 dBm: Very poor signal; likely unusable or highly unreliable.
     *  Below -90 dBm: Disconnected or too weak to establish a stable connection.
     */
    
    // dBm range: -30 dBm (strong) to -90 dBm (weak)
    const int max_dBm = -30;
    const int min_dBm = -90;
    int dBm_value = max_dBm + ((min_dBm - max_dBm) * (100 - percentage)) / 100;
    static char result[8] = {0};
    snprintf(result, sizeof(result), "%d", dBm_value);
    return result;
}

std::string getSecurityModeString(guint32 flag, guint32 wpaFlags, guint32 rsnFlags) {
    std::string securityStr = "[AP type: ";
    if (flag == NM_802_11_AP_FLAGS_NONE)
        securityStr += "NONE ";
    else {
        if ((flag & NM_802_11_AP_FLAGS_PRIVACY) != 0)
            securityStr += "PRIVACY ";
        if ((flag & NM_802_11_AP_FLAGS_WPS) != 0)
            securityStr += "WPS ";
        if ((flag & NM_802_11_AP_FLAGS_WPS_PBC) != 0)
            securityStr += "WPS_PBC ";
        if ((flag & NM_802_11_AP_FLAGS_WPS_PIN) != 0)
            securityStr += "WPS_PIN ";
    }
    securityStr += "] ";
    
    if (!(flag & NM_802_11_AP_FLAGS_PRIVACY) && (wpaFlags != NM_802_11_AP_SEC_NONE) &&
        (rsnFlags != NM_802_11_AP_SEC_NONE))
        securityStr += ("Encrypted: ");
    
    if ((flag & NM_802_11_AP_FLAGS_PRIVACY) && (wpaFlags == NM_802_11_AP_SEC_NONE)
        && (rsnFlags == NM_802_11_AP_SEC_NONE))
        securityStr += ("WEP ");
    if (wpaFlags != NM_802_11_AP_SEC_NONE)
        securityStr += ("WPA ");
    if ((rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_PSK)
        || (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)) {
        securityStr += ("WPA2 ");
    }
    if (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_SAE) {
        securityStr += ("WPA3 ");
    }
    if ((rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_OWE)
        || (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_OWE_TM)) {
        securityStr += ("OWE ");
    }
    if ((wpaFlags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
        || (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)) {
        securityStr += ("802.1X ");
    }
    
    if (securityStr.empty()) {
        securityStr = "None";
        return securityStr;
    }
    
    uint32_t flags[2] = {wpaFlags, rsnFlags};
    securityStr += "[WPA: ";
    
    for (int i = 0; i < 2; ++i) {
        if (flags[i] & NM_802_11_AP_SEC_PAIR_WEP40)
            securityStr += "pair_wep40 ";
        if (flags[i] & NM_802_11_AP_SEC_PAIR_WEP104)
            securityStr += "pair_wep104 ";
        if (flags[i] & NM_802_11_AP_SEC_PAIR_TKIP)
            securityStr += "pair_tkip ";
        if (flags[i] & NM_802_11_AP_SEC_PAIR_CCMP)
            securityStr += "pair_ccmp ";
        if (flags[i] & NM_802_11_AP_SEC_GROUP_WEP40)
            securityStr += "group_wep40 ";
        if (flags[i] & NM_802_11_AP_SEC_GROUP_WEP104)
            securityStr += "group_wep104 ";
        if (flags[i] & NM_802_11_AP_SEC_GROUP_TKIP)
            securityStr += "group_tkip ";
        if (flags[i] & NM_802_11_AP_SEC_GROUP_CCMP)
            securityStr += "group_ccmp ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_PSK)
            securityStr += "psk ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_802_1X)
            securityStr += "802.1X ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_SAE)
            securityStr += "sae ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_OWE)
            securityStr += "owe ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_OWE_TM)
            securityStr += "owe_transition_mode ";
        if (flags[i] & NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192)
            securityStr += "wpa-eap-suite-b-192 ";
        
        if (i == 0) {
            securityStr += "] [RSN: ";
        }
    }
    securityStr += "] ";
    return securityStr;
}


uint8_t wifiSecurityModeFromAp(const std::string &ssid, guint32 flags, guint32 wpaFlags, guint32 rsnFlags) {
    uint8_t security = WIFISecurityMode::WIFI_SECURITY_NONE;
    //printf("ap [%s] security str %s", ssid.c_str(), getSecurityModeString(flags, wpaFlags, rsnFlags).c_str());
    
    if ((flags != NM_802_11_AP_FLAGS_PRIVACY) && (wpaFlags == NM_802_11_AP_SEC_NONE) &&
        (rsnFlags == NM_802_11_AP_SEC_NONE)) // Open network
        security = WIFISecurityMode::WIFI_SECURITY_NONE;
    else if ((rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_PSK) &&
             (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_SAE)) // WPA2/WPA3 Transition
        security = WIFISecurityMode::WIFI_SECURITY_WPA_PSK;
    else if (rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_SAE)  // Pure WPA3 (SAE only): WPA3-Personal
        security = WIFISecurityMode::WIFI_SECURITY_SAE;
    else if (wpaFlags & NM_802_11_AP_SEC_KEY_MGMT_802_1X ||
             rsnFlags & NM_802_11_AP_SEC_KEY_MGMT_802_1X) // WPA2/WPA3 Enterprise: EAP present in either WPA or RSN
        security = WIFISecurityMode::WIFI_SECURITY_EAP;
    else
        security = WIFISecurityMode::WIFI_SECURITY_WPA_PSK; // WPA2-PSK
    
    return security;
}

void add_access_point(std::string device_path, std::string ap_path) {
    InterfaceLink *link = nullptr;
    for (auto l: wifi_data->links) {
        if (l->device_object_path == device_path) {
            link = l;
            break;
        }
    }
    if (!link)
        return;
    
    ScanResult result;
    //std::string interface;
    result.interface = link->interface;
    result.access_point = ap_path;
    
    
    std::string nm_bus = "org.freedesktop.NetworkManager";
    std::string nm_iface = "org.freedesktop.NetworkManager.AccessPoint";
    //std::string mac;
    result.mac = get_str_property(nm_bus, ap_path, nm_iface, "HwAddress");
    
    //std::string network_name;
    result.network_name = get_arr_b_property(nm_bus, ap_path, nm_iface, "Ssid");
    
    result.nm_flags = get_num_property<dbus_uint32_t>(nm_bus, ap_path, nm_iface, "Flags");
    result.nm_rsnFlags = get_num_property<dbus_uint32_t>(nm_bus, ap_path, nm_iface, "RsnFlags");
    result.nm_wpaFlags = get_num_property<dbus_uint32_t>(nm_bus, ap_path, nm_iface, "WpaFlags");
    
    uint8_t security = wifiSecurityModeFromAp(result.network_name, result.nm_flags, result.nm_wpaFlags,
                                              result.nm_rsnFlags);
    if (security != WIFI_SECURITY_NONE) {
        result.encr = 1;
        result.auth = AUTH_WPA2_PSK;
    }
    result.frequency = std::to_string(get_num_property<dbus_uint32_t>(nm_bus, ap_path, nm_iface, "Frequency"));
    result.connection_quality = convertPercentageToSignalStrengtStr(
            get_num_property<char>(nm_bus, ap_path, nm_iface, "Strength"));
    
    result.is_scan_result = true;
    
    link->results.push_back(result);
}

void network_manager_service_get_all_access_points_of_device(std::string device_path) {
    DBusError error;
    dbus_error_init(&error);
    
    auto aps_message = get_property("org.freedesktop.NetworkManager", device_path,
                                    "org.freedesktop.NetworkManager.Device.Wireless", "AccessPoints");
    if (!aps_message)
        return;
    defer(dbus_message_unref(aps_message));
    
    DBusMessageIter msg_iter;
    if (!dbus_message_iter_init(aps_message, &msg_iter)) {
        fprintf(stderr, "Message has no arguments!\n");
        return;
    }
    
    if (dbus_message_iter_get_arg_type(&msg_iter) != DBUS_TYPE_VARIANT) {
        fprintf(stderr, "Unexpected argument type: %c\n", dbus_message_iter_get_arg_type(&msg_iter));
        return;
    }
    
    DBusMessageIter variant_iter;
    dbus_message_iter_recurse(&msg_iter, &variant_iter);
    
    if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY) {
        fprintf(stderr, "Variant does not contain array, type: %c\n", dbus_message_iter_get_arg_type(&variant_iter));
        return;
    }
    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&variant_iter, &array_iter);
    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_OBJECT_PATH) {
        const char *ap_path = nullptr;
        dbus_message_iter_get_basic(&array_iter, &ap_path);
        add_access_point(device_path, ap_path);
        
        dbus_message_iter_next(&array_iter);
    }
}

void update_device_results_based_on_active_connection_info(std::string active_connection_path) {
    // SpecificOption property tells me what result it applies to
    Msg ap_reply = {get_property("org.freedesktop.NetworkManager", active_connection_path,
                                 "org.freedesktop.NetworkManager.Connection.Active", "SpecificObject")};
    Msg settings_reply = {get_property("org.freedesktop.NetworkManager", active_connection_path,
                                       "org.freedesktop.NetworkManager.Connection.Active", "Connection")};
    if (!ap_reply.msg || !settings_reply.msg)
        return;
    
    auto ap_any = parse_message(ap_reply.msg);
    auto settings_any = parse_message(settings_reply.msg);
    if (auto access_point = std::any_cast<std::string>(&ap_any)) {
        if (auto settings_path = std::any_cast<std::string>(&settings_any)) {
            for (auto l: wifi_data->links) {
                for (auto &item: l->results) {
                    if (item.access_point == *access_point) {
                        item.saved_network = true;
                        if (std::find(item.active_connections.begin(), item.active_connections.end(), *access_point) ==
                            item.active_connections.end()) {
                            item.active_connections.push_back(*access_point);
                        }
                        if (std::find(item.settings_paths.begin(), item.settings_paths.end(), *settings_path) ==
                            item.settings_paths.end()) {
                            item.settings_paths.push_back(*settings_path);
                        }
                    }
                }
            }
        }
    }
}

void network_manager_update_active_connections() {
    auto reply = sys_method_call("org.freedesktop.NetworkManager",
                                 "/org/freedesktop",
                                 "org.freedesktop.DBus.ObjectManager",
                                 "GetManagedObjects");
    if (!reply.msg)
        return;
    
    for (auto l: wifi_data->links) {
        for (auto &item: l->results) {
            item.active_connections.clear();
        }
    }
    
    std::any val = parse_message(reply.msg);
    if (auto dict = std::any_cast<DbusDict>(&val)) {
        for (const auto &item: dict->map) {
            if (auto object_path = std::any_cast<std::string>(&item.first)) {
                if (object_path->find("/org/freedesktop/NetworkManager/ActiveConnection/") != std::string::npos) {
                    update_device_results_based_on_active_connection_info(*object_path);
                }
            }
        }
    }
}

void network_manager_service_get_all_devices() {
    DBusError error;
    dbus_error_init(&error);
    
    DBusMessage *dbus_msg = dbus_message_new_method_call("org.freedesktop.NetworkManager",
                                                         "/org/freedesktop/NetworkManager",
                                                         "org.freedesktop.NetworkManager",
                                                         "GetAllDevices");
    defer(dbus_message_unref(dbus_msg));
    DBusMessage *devices_reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, dbus_msg, 16.6 * 5,
                                                                           &error);
    if (dbus_error_is_set(&error)) {
        std::cerr << "Failed to get NetworkManager devices: " << error.message << std::endl;
        dbus_error_free(&error);
        return;
    }
    defer(dbus_message_unref(devices_reply));
    
    DBusMessageIter iter;
    DBusMessageIter array_iter;
    
    if (!dbus_message_iter_init(devices_reply, &iter)) {
        printf("Message has no arguments!\n");
        return;
    }
    
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
        printf("Message argument is not an array!\n");
        return;
    }
    
    dbus_message_iter_recurse(&iter, &array_iter);
    
    for (auto links: wifi_data->links)
        delete links;
    wifi_data->links.clear();
    
    while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
        if (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_OBJECT_PATH) {
            const char *object_path;
            dbus_message_iter_get_basic(&array_iter, &object_path);
            
            DBusMessage *dbus_msg = dbus_message_new_method_call("org.freedesktop.NetworkManager",
                                                                 object_path,
                                                                 "org.freedesktop.DBus.Introspectable",
                                                                 "Introspect");
            dbus_error_init(&error);
            
            defer(dbus_message_unref(dbus_msg));
            DBusMessage *dbus_reply = dbus_connection_send_with_reply_and_block(dbus_connection_system, dbus_msg, 50,
                                                                                &error);
            
            if (dbus_error_is_set(&error)) {
                std::cerr << "Failed to get Device xml: " << error.message << std::endl;
                dbus_error_free(&error);
                return;
            }
            
            if (dbus_reply) {
                defer(dbus_message_unref(dbus_reply));
                
                const char *output;
                if (dbus_message_get_args(dbus_reply, NULL, DBUS_TYPE_STRING, &output, DBUS_TYPE_INVALID)) {
                    std::string xml(output);
                    
                    //if (xml.find("org.freedesktop.NetworkManager.Device.Wireless") != std::string::npos) {
                    std::string interface = get_str_property("org.freedesktop.NetworkManager", object_path, "org.freedesktop.NetworkManager.Device", "Interface");

                    if (!interface.empty()) {
                        auto link = new InterfaceLink;
                        link->interface = interface;
                        link->device_object_path = object_path;
                        wifi_data->links.push_back(link);
                        wifi_data->seen_interfaces.emplace_back(interface);
                        //winbar_settings->set_preferred_interface(interface);

                        network_manager_service_get_all_access_points_of_device(object_path);
                    }
                    //}
                }
            }
        } else {
            printf("Unexpected type in array: %c\n", dbus_message_iter_get_arg_type(&array_iter));
        }
        
        dbus_message_iter_next(&array_iter);
    }
    
    // Determine if links->results are saved config via searching all active_connections info and seeing if they corrolate
    network_manager_update_active_connections();
    if (wifi_data->when_state_changed) {
        wifi_data->when_state_changed();
    }
}

void network_manager_service_started() {
    if (dbus_connection_system == nullptr) return;
    binded_network_manager = true;
    
    // Watch for device added and removed events
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',interface='org.freedesktop.NetworkManager',member='DeviceAdded'",
                       NULL);
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',interface='org.freedesktop.NetworkManager',member='DeviceRemoved'",
                       NULL);
    DBusError error;
    dbus_error_init(&error);
    dbus_bus_add_match(dbus_connection_system,
                       ("type='signal',"
                        "sender='org.freedesktop.NetworkManager',"
                        "interface='org.freedesktop.DBus.Properties',"
                        "member='PropertiesChanged',"
                        "path='" + std::string("/org/freedesktop/NetworkManager") + "'").c_str(),
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "error: %s\n%s\n",
                error.name, error.message);
    }
    
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',"
                       "sender='org.freedesktop.NetworkManager',"
                       "interface='org.freedesktop.DBus.ObjectManager',"
                       "member='InterfacesAdded'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal InterfacesAdded because: %s\n%s\n",
                error.name, error.message);
    }
    dbus_bus_add_match(dbus_connection_system,
                       "type='signal',"
                       "sender='org.freedesktop.NetworkManager',"
                       "interface='org.freedesktop.DBus.ObjectManager',"
                       "member='InterfacesRemoved'",
                       &error);
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Couldn't watch signal InterfacesRemoved because: %s\n%s\n",
                error.name, error.message);
    }
    
    dbus_connection_add_filter(dbus_connection_system, network_manager_device_signal_filter, NULL, NULL);
    
    network_manager_service_get_all_devices();
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
    const char *interface_name = "org.freedesktop.UPower.Device";
    std::string serial = get_str_property("org.freedesktop.UPower", object_path, interface_name, "Serial");
    
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
    Msg reply = method_call(dbus_connection_system, "org.freedesktop.UPower", "/org/freedesktop/UPower",
                            "org.freedesktop.UPower", "EnumerateDevices");
    if (!reply.msg)
        return;
    auto any = parse_message(reply.msg);
    if (auto *arr = std::any_cast<DbusArray>(&any)) {
        for (auto el: arr->elements) {
            if (auto *str = std::any_cast<std::string>(&el)) {
                update_object_path(*str);
            }
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
                
                dbus_int32_t version = 0;
                
                dbus_message_append_args(reply, DBUS_TYPE_UINT32, &version, DBUS_TYPE_INVALID);
                dbus_connection_send(conn, reply, NULL);
            } else if (property_name == "IsStatusNotifierHostRegistered") {
                // Since Winbar is a StatusNotifierHost, this is always true if we are running
                // reply boolean true
                DBusMessage *reply = dbus_message_new_method_return(message);
                defer(dbus_message_unref(reply));
                
                dbus_bool_t is_registered = !status_notifier_hosts.empty();
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
        const char *service_name;
        dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &service_name, DBUS_TYPE_INVALID);
        
        status_notifier_hosts.emplace_back(service_name);
        
        StatusNotifierHostRegistered();
        
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
}

void RegisterStatusNotifierHost(const std::string &service);

void create_status_notifier_host() {
    int pid = 23184910;
    
    DBusError error;
    dbus_error_init(&error);
    auto host = std::string("org.kde.StatusNotifierHost-" + std::to_string(pid));
    int result = dbus_bus_request_name(dbus_connection_session,
                                       host.c_str(),
                                       DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    
    if (dbus_error_is_set(&error)) {
        fprintf(stderr, "Ran into error when trying to become \"org.kde.StatusNotifierHost\" (%s)\n",
                error.message);
        dbus_error_free(&error);
        return;
    }
    
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        fprintf(stderr, "Failed to become StatusNotifierHost\n");
        return;
    }
    
    RegisterStatusNotifierHost(host);
}

void RegisterStatusNotifierHost(const std::string &service) {
    if (!dbus_connection_session)
        return;
    
    printf("Registering Host: %s\n", service.c_str());
    
    DBusMessage *dmsg = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                     "org.kde.StatusNotifierWatcher",
                                                     "RegisterStatusNotifierHost");
    defer(dbus_message_unref(dmsg));
    dbus_message_append_args(dmsg, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(dbus_connection_session, dmsg, NULL);
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
