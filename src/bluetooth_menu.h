//
// Created by jmanc3 on 1/25/23.
//

#ifndef WINBAR_BLUETOOTH_MENU_H
#define WINBAR_BLUETOOTH_MENU_H


#include <dbus/dbus.h>
#include <cairo.h>
#include <string>
#include "container.h"

void open_bluetooth_menu();

void bluetooth_service_started();

void bluetooth_service_ended();

struct BluetoothRequest {
    DBusConnection *connection;
    DBusMessage *message;
    std::string type;
    std::string pin;
    std::string object_path;
    
    BluetoothRequest(DBusConnection *connection, DBusMessage *message, const std::string &type) {
        this->connection = connection;
        this->message = message;
        this->type = type;
        if (this->message)
            dbus_message_ref(message);
    }
    
    ~BluetoothRequest() {
        if (this->message)
            dbus_message_unref(message);
    }
};

void bluetooth_wants_response_from_user(BluetoothRequest *);

#endif //WINBAR_BLUETOOTH_MENU_H
