//
// Created by jmanc3 on 6/16/25.
//

#ifndef WINBAR_DBUS_HELPER_H
#define WINBAR_DBUS_HELPER_H

#include <vector>
#include <any>
#include <string>
#include <dbus/dbus.h>
#include <cstdint>

struct Msg {
    DBusMessage *msg = nullptr;
    ~Msg() {
        if (msg) {
            dbus_message_unref(msg);
        }
    }
};

struct DBusStrString {
    std::string value;
    explicit DBusStrString(const std::string &v) : value(v) {}
};

struct DBusStrObjectPath {
    std::string value;
    explicit DBusStrObjectPath(const std::string &v) : value(v) {}
};

struct DBusStrSignature {
    std::string value;
    explicit DBusStrSignature(const std::string &v) : value(v) {}
};

struct DBusVariantBool {
    bool value;
    explicit DBusVariantBool(const bool &v) : value(v) {}
};

struct DBusVariantString {
    std::string value;
    explicit DBusVariantString(const std::string &v) : value(v) {}
};

struct DBusVariantByteArray {
    std::vector<uint8_t> value;
    
    // Construct from vector of bytes
    explicit DBusVariantByteArray(const std::vector<uint8_t> &v) : value(v) {}
    
    // Construct from string -> convert to bytes
    explicit DBusVariantByteArray(const std::string &s) : value(s.begin(), s.end()) {}
};

struct DbusArray {
    std::vector<std::any> elements;
};

struct DbusStruct {
    std::vector<std::any> members;
};

struct DbusDict {
    std::vector<std::pair<std::any, std::any>> map;
    std::string sig = "{vv}";
};

// For dbusmessages returned from function calls
std::any parse_message(DBusMessage* msg);

// For DBusMessage Iters
std::any parse_iter(DBusMessageIter *iter);

// For dbusmessages which need one itereration first, before using (like signals)
std::any parse_iter_message(DBusMessage* msg);

bool write_dbus_message(DBusMessage *msg);

Msg method_call(DBusConnection *con, std::string bus, std::string path, std::string iface, std::string method);

DBusMessage* build_message(const char *dest, const char *path, const char *iface, const char *method, const std::any &payload);

DBusMessage* build_message(const char *dest, const char *path, const char *iface, const char *method, const std::vector<std::any> &payloads);

#endif //WINBAR_DBUS_HELPER_H
