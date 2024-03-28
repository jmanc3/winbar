//
// Created by jmanc3 on 5/28/21.
//

#ifndef WINBAR_SIMPLE_DBUS_H
#define WINBAR_SIMPLE_DBUS_H

#include "notifications.h"

#include <string>
#include <utility>
#include <vector>
#include <mutex>

struct DBusConnection;

extern DBusConnection *dbus_connection_session;
extern DBusConnection *dbus_connection_system;

extern std::vector<std::string> running_dbus_services;
struct BluetoothInterface;
extern std::vector<BluetoothInterface *> bluetooth_interfaces;
extern bool bluetooth_running;

void dbus_start(DBusBusType dbusType);

void dbus_end();

bool dbus_gnome_show_overview();

bool dbus_kde_show_desktop();

bool dbus_kde_show_desktop_grid();

void notification_closed_signal(App *app, NotificationInfo *ni, NotificationReasonClosed reason);

void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action);

double dbus_get_kde_max_brightness();

double dbus_get_kde_current_brightness();

void highlight_windows(const std::vector<std::string> &windows);

void highlight_window(std::string window_id_as_string);

/// Number from 0 to 1
bool dbus_kde_set_brightness(double percentage);

bool dbus_kde_running();

bool dbus_gnome_running();

double dbus_get_gnome_brightness();

/// Number from 0 to 100
bool dbus_set_gnome_brightness(double percentage);

void dbus_computer_logoff();

void dbus_computer_shut_down();

void dbus_computer_restart();

void dbus_open_in_folder(std::string path);

void register_agent_if_needed();

void unregister_agent_if_needed();

void upower_service_started();

void upower_service_ended();

enum struct BluetoothInterfaceType {
    Error = 0,
    Device = 1,
    Adapter = 2,
};

struct BluetoothInterface {
    std::string object_path;
    std::string mac_address;
    std::string name;
    std::string alias;
    std::string upower_path;
    BluetoothInterfaceType type = BluetoothInterfaceType::Error;
};

struct BluetoothCallbackInfo {
    BluetoothCallbackInfo(BluetoothInterface *blue_interface, std::string command,
                          void (*function)(BluetoothCallbackInfo *));
    
    std::string mac_address;
    std::string command;
    std::string message;
    void (*function)(BluetoothCallbackInfo *) = nullptr;
    bool succeeded = false;
};

struct Device : BluetoothInterface {
    std::string icon;
    std::string adapter;
    bool paired = false;
    bool connected = false;
    bool bonded = false;
    bool trusted = false;
    std::string percentage;
    
    Device(std::string string) {
        object_path = std::move(string);
        type = BluetoothInterfaceType::Device;
    }
    
    void connect(void (*function)(BluetoothCallbackInfo *));
    
    void disconnect(void (*function)(BluetoothCallbackInfo *));
    
    void trust(void (*function)(BluetoothCallbackInfo *));
    
    void untrust(void (*function)(BluetoothCallbackInfo *));
    
    void pair(void (*function)(BluetoothCallbackInfo *));
    
    void cancel_pair(void (*function)(BluetoothCallbackInfo *));
    
    void unpair(void (*function)(BluetoothCallbackInfo *));
};

struct Adapter : BluetoothInterface {
    bool powered = false;
    
    Adapter(std::string string) {
        object_path = std::move(string);
        type = BluetoothInterfaceType::Adapter;
    }
    
    void scan_on(void (*function)(BluetoothCallbackInfo *));
    
    void scan_off(void (*function)(BluetoothCallbackInfo *));
    
    void power_on(void (*function)(BluetoothCallbackInfo *));
    
    void power_off(void (*function)(BluetoothCallbackInfo *));
};

bool become_default_bluetooth_agent();

void update_devices();

extern void (*on_any_bluetooth_property_changed)();


#endif //WINBAR_SIMPLE_DBUS_H
