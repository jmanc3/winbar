#include "systray.h"
#include "application.h"
#include "config.h"
#include "drawer.h"
#include "icons.h"
#include "main.h"
#include "simple_dbus.h"
#include "taskbar.h"
#include <dbus/dbus.h>
#include <xcb/xcb.h>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility.h>
#include <vector>
#include <xcb/xcb_event.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0

static AppClient *systray = nullptr;
static AppClient *display = nullptr;
static AppClient *sni_menu_client = nullptr;
static uint32_t icon_size = 22;
static uint32_t container_size = 40;
static bool layout_invalid = true;
static int current_grid_w = 1;

static void
layout_systray();

static void
display_close();

class Systray_Icon {
public:
    xcb_window_t window;
    bool reparented = false;
    int times = 0;
};

std::vector<Systray_Icon *> systray_icons;

/*************************************************
 *
 * StatusNotifierItem (SNI) & com.canonical.dbusmenu
 *
 *************************************************/

struct TrayIconPixels {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> argb; // premultiplied, native endian (cairo ARGB32 layout)
};

struct TrayMenuItem {
    int id = 0;
    std::string label;
    bool separator = false;
    bool enabled = true;
    bool visible = true;
    std::string toggle_type; // "", "checkmark", or "radio"
    int toggle_state = -1;   // 0 off, 1 on, -1 unknown
    std::vector<TrayMenuItem> children;
};

struct SniItem {
    std::string key;
    std::string service; // bus name the item was registered with
    std::string path;
    std::string owner;   // unique bus name, which signals come from
    std::string interface = "org.kde.StatusNotifierItem";

    std::string title;
    std::string status;
    std::string icon_name;
    std::string attention_icon_name;
    std::string theme_path;
    std::string menu_path;
    TrayIconPixels pixmap;
    TrayIconPixels attention_pixmap;
    bool item_is_menu = false;
    bool loaded = false;
    bool fetching = false;
    bool fetch_again = false;
    unsigned long icon_revision = 0;
};

struct SniIconCache {
    unsigned long revision = 0;
    uint32_t size = 0;
    bool attempted = false;
    cairo_surface_t *surface = nullptr;
    gl_surface *gsurf = nullptr;

    void clear() {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
        if (gsurf) {
            delete gsurf;
            gsurf = nullptr;
        }
        revision = 0;
        size = 0;
        attempted = false;
    }

    ~SniIconCache() {
        clear();
    }
};

static const char *WATCHER_PATH = "/StatusNotifierWatcher";
static const char *KDE_WATCHER = "org.kde.StatusNotifierWatcher";
static const char *FDO_WATCHER = "org.freedesktop.StatusNotifierWatcher";
static const char *KDE_ITEM = "org.kde.StatusNotifierItem";
static const char *FDO_ITEM = "org.freedesktop.StatusNotifierItem";
static const char *DBUSMENU = "com.canonical.dbusmenu";

static std::vector<std::unique_ptr<SniItem>> sni_items;
static std::map<std::string, SniIconCache> sni_icon_cache;
static std::vector<DBusPendingCall *> sni_pending_calls;
static bool sni_running = false;
static unsigned long sni_generation = 0;
static unsigned long sni_icon_revision_counter = 0;
static bool owns_watcher = false;
static bool sni_filter_added = false;
static bool watcher_path_registered = false;
static std::string sni_host_name;
static std::string external_watcher;

struct SniPendingData {
    unsigned long generation = 0;
    std::function<void(DBusMessage *)> callback;
};

static std::vector<SniItem *> visible_sni_items() {
    std::vector<SniItem *> out;
    for (auto &item : sni_items) {
        if (item->loaded && item->status != "Passive") {
            out.push_back(item.get());
        }
    }
    return out;
}

static void notify_sni_changed() {
    layout_invalid = true;
    if (display) {
        layout_systray();
        request_refresh(app, display);
    }
}

static void sni_pending_notify(DBusPendingCall *pending, void *user_data) {
    auto *data = (SniPendingData *) user_data;
    sni_pending_calls.erase(std::remove(sni_pending_calls.begin(), sni_pending_calls.end(), pending),
                            sni_pending_calls.end());
    DBusMessage *reply = dbus_pending_call_steal_reply(pending);
    if (data->generation == sni_generation && sni_running && data->callback) {
        data->callback(reply);
    }
    if (reply)
        dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
}

static void free_sni_pending_data(void *user_data) {
    delete (SniPendingData *) user_data;
}

static void sni_call_async(DBusMessage *message, std::function<void(DBusMessage *)> callback, int timeout_ms = 3000) {
    if (!message)
        return;
    if (!dbus_connection_session) {
        dbus_message_unref(message);
        return;
    }
    DBusPendingCall *pending = nullptr;
    if (!dbus_connection_send_with_reply(dbus_connection_session, message, &pending, timeout_ms) || !pending) {
        dbus_message_unref(message);
        return;
    }
    dbus_message_unref(message);
    auto *data = new SniPendingData;
    data->generation = sni_generation;
    data->callback = std::move(callback);
    if (!dbus_pending_call_set_notify(pending, sni_pending_notify, data, free_sni_pending_data)) {
        delete data;
        dbus_pending_call_cancel(pending);
        dbus_pending_call_unref(pending);
        return;
    }
    sni_pending_calls.push_back(pending);
    dbus_connection_flush(dbus_connection_session);
}

static void sni_send_no_reply(DBusMessage *message) {
    if (!message)
        return;
    if (dbus_connection_session) {
        dbus_message_set_no_reply(message, TRUE);
        dbus_connection_send(dbus_connection_session, message, nullptr);
        dbus_connection_flush(dbus_connection_session);
    }
    dbus_message_unref(message);
}

static bool sni_is_error(DBusMessage *reply) {
    return !reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR;
}

static std::string sni_iter_string(DBusMessageIter *it) {
    int type = dbus_message_iter_get_arg_type(it);
    if (type == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(it, &inner);
        return sni_iter_string(&inner);
    }
    if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH) {
        const char *value = nullptr;
        dbus_message_iter_get_basic(it, &value);
        return value ? value : "";
    }
    return "";
}

static bool sni_iter_bool(DBusMessageIter *it, bool fallback) {
    int type = dbus_message_iter_get_arg_type(it);
    if (type == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(it, &inner);
        return sni_iter_bool(&inner, fallback);
    }
    if (type == DBUS_TYPE_BOOLEAN) {
        dbus_bool_t value = FALSE;
        dbus_message_iter_get_basic(it, &value);
        return value;
    }
    return fallback;
}

static int sni_iter_int(DBusMessageIter *it, int fallback) {
    int type = dbus_message_iter_get_arg_type(it);
    if (type == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(it, &inner);
        return sni_iter_int(&inner, fallback);
    }
    if (type == DBUS_TYPE_INT32 || type == DBUS_TYPE_UINT32) {
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(it, &value);
        return value;
    }
    return fallback;
}

// a(iiay): picks the largest icon and converts from network byte order ARGB to premultiplied ARGB32.
static TrayIconPixels sni_iter_pixmaps(DBusMessageIter *it) {
    TrayIconPixels best;
    if (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(it, &inner);
        return sni_iter_pixmaps(&inner);
    }
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_ARRAY)
        return best;
    DBusMessageIter array;
    dbus_message_iter_recurse(it, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
        DBusMessageIter fields;
        dbus_message_iter_recurse(&array, &fields);
        int w = sni_iter_int(&fields, 0);
        dbus_message_iter_next(&fields);
        int h = sni_iter_int(&fields, 0);
        dbus_message_iter_next(&fields);
        if (w > 0 && h > 0 && w <= 1024 && h <= 1024 && w * h > best.w * best.h &&
            dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
            DBusMessageIter bytes_iter;
            dbus_message_iter_recurse(&fields, &bytes_iter);
            const unsigned char *bytes = nullptr;
            int length = 0;
            dbus_message_iter_get_fixed_array(&bytes_iter, &bytes, &length);
            if (bytes && length >= w * h * 4) {
                best.w = w;
                best.h = h;
                best.argb.resize((size_t) w * h);
                for (int i = 0; i < w * h; i++) {
                    uint32_t a = bytes[i * 4 + 0];
                    uint32_t r = bytes[i * 4 + 1];
                    uint32_t g = bytes[i * 4 + 2];
                    uint32_t b = bytes[i * 4 + 3];
                    r = (r * a + 127) / 255;
                    g = (g * a + 127) / 255;
                    b = (b * a + 127) / 255;
                    best.argb[i] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
        dbus_message_iter_next(&array);
    }
    return best;
}

static SniItem *find_sni(const std::string &key) {
    for (auto &item : sni_items)
        if (item->key == key)
            return item.get();
    return nullptr;
}

static void emit_watcher_signal(const char *member, const std::string *argument) {
    if (!owns_watcher || !dbus_connection_session)
        return;
    for (auto interface : {KDE_WATCHER, FDO_WATCHER}) {
        auto *signal = dbus_message_new_signal(WATCHER_PATH, interface, member);
        if (!signal)
            continue;
        if (argument) {
            const char *value = argument->c_str();
            dbus_message_append_args(signal, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID);
        }
        dbus_connection_send(dbus_connection_session, signal, nullptr);
        dbus_message_unref(signal);
    }
    dbus_connection_flush(dbus_connection_session);
}

static void fetch_sni_properties(SniItem *item);

static void parse_sni_properties(SniItem *item, DBusMessage *reply) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;
    DBusMessageIter dict;
    dbus_message_iter_recurse(&iter, &dict);
    item->title.clear();
    item->menu_path.clear();
    item->pixmap = {};
    item->attention_pixmap = {};
    item->icon_name.clear();
    item->attention_icon_name.clear();
    item->theme_path.clear();
    item->item_is_menu = false;
    std::string id, tooltip;
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);
        std::string name = sni_iter_string(&entry);
        dbus_message_iter_next(&entry);
        if (name == "Id")
            id = sni_iter_string(&entry);
        else if (name == "Title")
            item->title = sni_iter_string(&entry);
        else if (name == "Status")
            item->status = sni_iter_string(&entry);
        else if (name == "IconName")
            item->icon_name = sni_iter_string(&entry);
        else if (name == "AttentionIconName")
            item->attention_icon_name = sni_iter_string(&entry);
        else if (name == "IconThemePath")
            item->theme_path = sni_iter_string(&entry);
        else if (name == "Menu")
            item->menu_path = sni_iter_string(&entry);
        else if (name == "ItemIsMenu")
            item->item_is_menu = sni_iter_bool(&entry, false);
        else if (name == "IconPixmap")
            item->pixmap = sni_iter_pixmaps(&entry);
        else if (name == "AttentionIconPixmap")
            item->attention_pixmap = sni_iter_pixmaps(&entry);
        else if (name == "ToolTip") {
            // (sa(iiay)ss): icon, pixmaps, title, description
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                DBusMessageIter variant;
                dbus_message_iter_recurse(&entry, &variant);
                if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT) {
                    DBusMessageIter fields;
                    dbus_message_iter_recurse(&variant, &fields);
                    dbus_message_iter_next(&fields);
                    dbus_message_iter_next(&fields);
                    tooltip = sni_iter_string(&fields);
                }
            }
        }
        dbus_message_iter_next(&dict);
    }
    if (item->menu_path == "/")
        item->menu_path.clear();
    if (item->title.empty())
        item->title = !tooltip.empty() ? tooltip : id;
    if (item->status.empty())
        item->status = "Active";
    item->icon_revision = ++sni_icon_revision_counter;
}

static void fetch_sni_properties(SniItem *item) {
    if (item->fetching) {
        item->fetch_again = true;
        return;
    }
    item->fetching = true;
    auto *message = dbus_message_new_method_call(item->service.c_str(), item->path.c_str(),
                                                 "org.freedesktop.DBus.Properties", "GetAll");
    if (!message) {
        item->fetching = false;
        return;
    }
    const char *interface = item->interface.c_str();
    dbus_message_append_args(message, DBUS_TYPE_STRING, &interface, DBUS_TYPE_INVALID);
    std::string key = item->key;
    sni_call_async(message, [key](DBusMessage *reply) {
        auto *item = find_sni(key);
        if (!item)
            return;
        item->fetching = false;
        if (sni_is_error(reply)) {
            if (!item->loaded && item->interface == KDE_ITEM) {
                item->interface = FDO_ITEM;
                fetch_sni_properties(item);
            }
            return;
        }
        parse_sni_properties(item, reply);
        item->loaded = true;
        notify_sni_changed();
        if (item->fetch_again) {
            item->fetch_again = false;
            fetch_sni_properties(item);
        }
    });
}

static void resolve_sni_owner(SniItem *item) {
    if (!item->service.empty() && item->service[0] == ':') {
        item->owner = item->service;
        return;
    }
    auto *message = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                 "org.freedesktop.DBus", "GetNameOwner");
    if (!message)
        return;
    const char *name = item->service.c_str();
    dbus_message_append_args(message, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
    std::string key = item->key;
    sni_call_async(message, [key](DBusMessage *reply) {
        auto *item = find_sni(key);
        if (!item || sni_is_error(reply))
            return;
        const char *owner = nullptr;
        if (dbus_message_get_args(reply, nullptr, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID) && owner)
            item->owner = owner;
    });
}

static void add_sni_item(std::string registered, const char *sender) {
    std::string service, path;
    if (!registered.empty() && registered[0] == '/') {
        service = sender ? sender : "";
        path = registered;
    } else {
        auto slash = registered.find('/');
        service = slash == std::string::npos ? registered : registered.substr(0, slash);
        path = slash == std::string::npos ? "/StatusNotifierItem" : registered.substr(slash);
    }
    if (service.empty())
        return;
    std::string key = service + path;
    if (find_sni(key))
        return;
    auto item = std::make_unique<SniItem>();
    item->key = key;
    item->service = service;
    item->path = path;
    auto *raw = item.get();
    sni_items.push_back(std::move(item));
    resolve_sni_owner(raw);
    fetch_sni_properties(raw);
    std::string announced = (!registered.empty() && registered[0] == '/') ? key : registered;
    emit_watcher_signal("StatusNotifierItemRegistered", &announced);
}

static void remove_sni_items_of(const std::string &name) {
    bool changed = false;
    for (int i = (int) sni_items.size() - 1; i >= 0; i--) {
        auto &item = sni_items[i];
        if (item->service == name || item->owner == name) {
            std::string key = item->key;
            sni_icon_cache.erase(key);
            sni_items.erase(sni_items.begin() + i);
            emit_watcher_signal("StatusNotifierItemUnregistered", &key);
            changed = true;
        }
    }
    if (changed) {
        notify_sni_changed();
        if (systray_icons.empty() && visible_sni_items().empty() && display) {
            display_close();
        }
    }
}

static DBusHandlerResult watcher_message(DBusConnection *connection, DBusMessage *message, void *) {
    static const char *introspection =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node>\n"
            " <interface name=\"org.kde.StatusNotifierWatcher\">\n"
            "  <method name=\"RegisterStatusNotifierItem\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>\n"
            "  <method name=\"RegisterStatusNotifierHost\"><arg name=\"service\" type=\"s\" direction=\"in\"/></method>\n"
            "  <property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>\n"
            "  <property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>\n"
            "  <property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>\n"
            "  <signal name=\"StatusNotifierItemRegistered\"><arg type=\"s\"/></signal>\n"
            "  <signal name=\"StatusNotifierItemUnregistered\"><arg type=\"s\"/></signal>\n"
            "  <signal name=\"StatusNotifierHostRegistered\"/>\n"
            "  <signal name=\"StatusNotifierHostUnregistered\"/>\n"
            " </interface>\n"
            " <interface name=\"org.freedesktop.DBus.Properties\">\n"
            "  <method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>\n"
            "  <method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>\n"
            " </interface>\n"
            " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
            "  <method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method>\n"
            " </interface>\n"
            "</node>\n";

    auto reply_empty = [&]() {
        auto *reply = dbus_message_new_method_return(message);
        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
    };

    auto append_property = [](DBusMessageIter *iter, const std::string &name) {
        if (name == "RegisteredStatusNotifierItems") {
            DBusMessageIter variant, array;
            dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant);
            dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &array);
            for (auto &item : sni_items) {
                const char *value = item->key.c_str();
                dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &value);
            }
            dbus_message_iter_close_container(&variant, &array);
            dbus_message_iter_close_container(iter, &variant);
        } else if (name == "IsStatusNotifierHostRegistered") {
            DBusMessageIter variant;
            dbus_bool_t value = TRUE;
            dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
            dbus_message_iter_close_container(iter, &variant);
        } else {
            DBusMessageIter variant;
            dbus_int32_t value = 0;
            dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &variant);
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
            dbus_message_iter_close_container(iter, &variant);
        }
    };

    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        auto *reply = dbus_message_new_method_return(message);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection, DBUS_TYPE_INVALID);
        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get")) {
        const char *interface = nullptr, *property = nullptr;
        if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &interface,
                                   DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID) || !property)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        std::string name = property;
        if (name != "RegisteredStatusNotifierItems" &&
            name != "IsStatusNotifierHostRegistered" &&
            name != "ProtocolVersion") {
            auto *error = dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
            dbus_connection_send(connection, error, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(error);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        auto *reply = dbus_message_new_method_return(message);
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply, &iter);
        append_property(&iter, name);
        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll")) {
        auto *reply = dbus_message_new_method_return(message);
        DBusMessageIter iter, dict;
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
        for (auto name : {"RegisteredStatusNotifierItems", "IsStatusNotifierHostRegistered", "ProtocolVersion"}) {
            DBusMessageIter entry;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
            append_property(&entry, name);
            dbus_message_iter_close_container(&dict, &entry);
        }
        dbus_message_iter_close_container(&iter, &dict);
        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    for (auto interface : {KDE_WATCHER, FDO_WATCHER}) {
        if (dbus_message_is_method_call(message, interface, "RegisterStatusNotifierItem")) {
            const char *service = nullptr;
            if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID) && service)
                add_sni_item(service, dbus_message_get_sender(message));
            reply_empty();
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_is_method_call(message, interface, "RegisterStatusNotifierHost")) {
            reply_empty();
            emit_watcher_signal("StatusNotifierHostRegistered", nullptr);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void restart_sni_timeout(App *, AppClient *, Timeout *, void *) {
    if (sni_running) {
        systray_dbus_stop();
        systray_dbus_start();
    }
}

static DBusHandlerResult sni_signal_filter(DBusConnection *, DBusMessage *message, void *) {
    if (!sni_running || dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_signal(message, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
        if (dbus_message_get_args(message, nullptr,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_STRING, &old_owner,
                                  DBUS_TYPE_STRING, &new_owner,
                                  DBUS_TYPE_INVALID) && name && new_owner && !*new_owner) {
            remove_sni_items_of(name);
            if (old_owner && *old_owner)
                remove_sni_items_of(old_owner);
            // The external watcher went away: schedule takeover.
            if (!external_watcher.empty() && external_watcher == name && app) {
                app_timeout_create(app, nullptr, 50, restart_sni_timeout, nullptr, "sni watcher takeover");
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    const char *interface = dbus_message_get_interface(message);
    const char *member = dbus_message_get_member(message);
    if (!interface || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    std::string iface = interface;

    // External watcher announcing items (host mode).
    if (!owns_watcher && (iface == KDE_WATCHER || iface == FDO_WATCHER)) {
        const char *service = nullptr;
        if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID) && service) {
            if (std::string(member) == "StatusNotifierItemRegistered") {
                add_sni_item(service, nullptr);
            } else if (std::string(member) == "StatusNotifierItemUnregistered") {
                std::string key = service;
                if (key.find('/') == std::string::npos)
                    key += "/StatusNotifierItem";
                sni_icon_cache.erase(key);
                sni_items.erase(std::remove_if(sni_items.begin(), sni_items.end(),
                                               [&](const std::unique_ptr<SniItem> &item) {
                                                   return item->key == key;
                                               }),
                                sni_items.end());
                notify_sni_changed();
                if (systray_icons.empty() && visible_sni_items().empty() && display) {
                    display_close();
                }
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (iface == KDE_ITEM || iface == FDO_ITEM) {
        const char *sender = dbus_message_get_sender(message);
        const char *path = dbus_message_get_path(message);
        if (!sender || !path)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        for (auto &item : sni_items) {
            if ((item->owner == sender || item->service == sender) && item->path == path) {
                if (std::string(member) == "NewStatus") {
                    const char *status = nullptr;
                    if (dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &status, DBUS_TYPE_INVALID) && status) {
                        item->status = status;
                        notify_sni_changed();
                    }
                } else {
                    fetch_sni_properties(item.get());
                }
            }
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void start_host_of_external_watcher() {
    external_watcher = KDE_WATCHER;
    auto *message = dbus_message_new_method_call(KDE_WATCHER, WATCHER_PATH, KDE_WATCHER, "RegisterStatusNotifierHost");
    if (message) {
        const char *name = sni_host_name.c_str();
        dbus_message_append_args(message, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
        sni_call_async(message, nullptr);
    }
    message = dbus_message_new_method_call(KDE_WATCHER, WATCHER_PATH, "org.freedesktop.DBus.Properties", "Get");
    if (!message)
        return;
    const char *interface = KDE_WATCHER;
    const char *property = "RegisteredStatusNotifierItems";
    dbus_message_append_args(message, DBUS_TYPE_STRING, &interface, DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
    sni_call_async(message, [](DBusMessage *reply) {
        if (sni_is_error(reply))
            return;
        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
            return;
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY)
            return;
        DBusMessageIter array;
        dbus_message_iter_recurse(&variant, &array);
        while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
            add_sni_item(sni_iter_string(&array), nullptr);
            dbus_message_iter_next(&array);
        }
    });
}

void systray_dbus_start() {
    if (sni_running || !dbus_connection_session)
        return;
    sni_running = true;
    auto *connection = dbus_connection_session;

    if (!sni_filter_added)
        sni_filter_added = dbus_connection_add_filter(connection, sni_signal_filter, nullptr, nullptr);

    for (auto rule : {
                 "type='signal',interface='org.kde.StatusNotifierItem'",
                 "type='signal',interface='org.freedesktop.StatusNotifierItem'",
                 "type='signal',interface='org.kde.StatusNotifierWatcher'",
                 "type='signal',interface='org.freedesktop.StatusNotifierWatcher'",
                 "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'"}) {
        dbus_bus_add_match(connection, rule, nullptr);
    }

    sni_host_name = "org.kde.StatusNotifierHost-" + std::to_string(getpid());
    dbus_bus_request_name(connection, sni_host_name.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, nullptr);

    static const DBusObjectPathVTable vtable = {.message_function = watcher_message};
    if (!watcher_path_registered)
        watcher_path_registered = dbus_connection_register_object_path(connection, WATCHER_PATH, &vtable, nullptr);

    DBusError error = DBUS_ERROR_INIT;
    int result = dbus_bus_request_name(connection, KDE_WATCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    dbus_error_free(&error);
    owns_watcher = (result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
                    result == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER);
    if (owns_watcher) {
        external_watcher.clear();
        dbus_bus_request_name(connection, FDO_WATCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, nullptr);
        emit_watcher_signal("StatusNotifierHostRegistered", nullptr);
    } else {
        start_host_of_external_watcher();
    }
    dbus_connection_flush(connection);
}

void systray_dbus_stop() {
    if (!sni_running)
        return;
    sni_running = false;
    sni_generation++;

    for (auto *pending : sni_pending_calls) {
        dbus_pending_call_cancel(pending);
        dbus_pending_call_unref(pending);
    }
    sni_pending_calls.clear();
    sni_icon_cache.clear();
    sni_items.clear();

    if (dbus_connection_session) {
        auto *connection = dbus_connection_session;
        if (owns_watcher) {
            emit_watcher_signal("StatusNotifierHostUnregistered", nullptr);
            dbus_bus_release_name(connection, KDE_WATCHER, nullptr);
            dbus_bus_release_name(connection, FDO_WATCHER, nullptr);
        }
        owns_watcher = false;
        external_watcher.clear();
        if (!sni_host_name.empty()) {
            dbus_bus_release_name(connection, sni_host_name.c_str(), nullptr);
            sni_host_name.clear();
        }
        if (watcher_path_registered) {
            dbus_connection_unregister_object_path(connection, WATCHER_PATH);
            watcher_path_registered = false;
        }
        if (sni_filter_added) {
            dbus_connection_remove_filter(connection, sni_signal_filter, nullptr);
            sni_filter_added = false;
        }
    }
}

/*************************************************
 *
 * com.canonical.dbusmenu & SNI interaction
 *
 *************************************************/

static std::string strip_mnemonic(const std::string &label) {
    std::string out;
    for (size_t i = 0; i < label.size(); i++) {
        if (label[i] == '_') {
            if (i + 1 < label.size() && label[i + 1] == '_') {
                out += '_';
                i++;
            }
            continue;
        }
        out += label[i];
    }
    return out;
}

static TrayMenuItem parse_menu_node(DBusMessageIter *structure) {
    TrayMenuItem item;
    DBusMessageIter fields;
    dbus_message_iter_recurse(structure, &fields);
    item.id = sni_iter_int(&fields, 0);
    dbus_message_iter_next(&fields);
    if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
        DBusMessageIter props;
        dbus_message_iter_recurse(&fields, &props);
        while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&props, &entry);
            std::string name = sni_iter_string(&entry);
            dbus_message_iter_next(&entry);
            if (name == "label")
                item.label = strip_mnemonic(sni_iter_string(&entry));
            else if (name == "type")
                item.separator = sni_iter_string(&entry) == "separator";
            else if (name == "enabled")
                item.enabled = sni_iter_bool(&entry, true);
            else if (name == "visible")
                item.visible = sni_iter_bool(&entry, true);
            else if (name == "toggle-type")
                item.toggle_type = sni_iter_string(&entry);
            else if (name == "toggle-state")
                item.toggle_state = sni_iter_int(&entry, -1);
            dbus_message_iter_next(&props);
        }
    }
    dbus_message_iter_next(&fields);
    if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_ARRAY) {
        DBusMessageIter children;
        dbus_message_iter_recurse(&fields, &children);
        while (dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_VARIANT) {
            DBusMessageIter variant;
            dbus_message_iter_recurse(&children, &variant);
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT)
                item.children.push_back(parse_menu_node(&variant));
            dbus_message_iter_next(&children);
        }
    }
    return item;
}

static void sni_click(const std::string &key, int button, int x, int y) {
    auto *item = find_sni(key);
    if (!item)
        return;
    const char *method = button == 1 ? "Activate" : button == 2 ? "SecondaryActivate" : "ContextMenu";
    auto *message = dbus_message_new_method_call(item->service.c_str(), item->path.c_str(),
                                                 item->interface.c_str(), method);
    if (!message)
        return;
    dbus_int32_t dx = x, dy = y;
    dbus_message_append_args(message, DBUS_TYPE_INT32, &dx, DBUS_TYPE_INT32, &dy, DBUS_TYPE_INVALID);
    sni_call_async(message, nullptr);
}

static void sni_scroll(const std::string &key, int delta, bool horizontal) {
    auto *item = find_sni(key);
    if (!item)
        return;
    auto *message = dbus_message_new_method_call(item->service.c_str(), item->path.c_str(),
                                                 item->interface.c_str(), "Scroll");
    if (!message)
        return;
    dbus_int32_t amount = delta;
    const char *orientation = horizontal ? "horizontal" : "vertical";
    dbus_message_append_args(message, DBUS_TYPE_INT32, &amount, DBUS_TYPE_STRING, &orientation, DBUS_TYPE_INVALID);
    sni_send_no_reply(message);
}

static void sni_fetch_menu(const std::string &key, std::function<void(std::vector<TrayMenuItem>)> on_ready) {
    auto *item = find_sni(key);
    if (!item || item->menu_path.empty()) {
        if (on_ready)
            on_ready({});
        return;
    }
    auto *about = dbus_message_new_method_call(item->service.c_str(), item->menu_path.c_str(), DBUSMENU, "AboutToShow");
    if (about) {
        dbus_int32_t root_id = 0;
        dbus_message_append_args(about, DBUS_TYPE_INT32, &root_id, DBUS_TYPE_INVALID);
        sni_call_async(about, nullptr);
    }
    auto *message = dbus_message_new_method_call(item->service.c_str(), item->menu_path.c_str(), DBUSMENU, "GetLayout");
    if (!message) {
        if (on_ready)
            on_ready({});
        return;
    }
    dbus_int32_t parent = 0, depth = -1;
    DBusMessageIter iter, names;
    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &parent);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &depth);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_close_container(&iter, &names);
    sni_call_async(message, [on_ready](DBusMessage *reply) {
        std::vector<TrayMenuItem> items;
        DBusMessageIter iter;
        if (!sni_is_error(reply) && dbus_message_iter_init(reply, &iter)) {
            dbus_message_iter_next(&iter); // revision
            if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRUCT)
                items = parse_menu_node(&iter).children;
        }
        if (on_ready)
            on_ready(std::move(items));
    });
}

static void sni_menu_clicked(const std::string &key, int id) {
    auto *item = find_sni(key);
    if (!item || item->menu_path.empty())
        return;
    auto *message = dbus_message_new_method_call(item->service.c_str(), item->menu_path.c_str(), DBUSMENU, "Event");
    if (!message)
        return;
    dbus_int32_t item_id = id;
    const char *event = "clicked";
    dbus_uint32_t timestamp = 0;
    DBusMessageIter iter, variant;
    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &item_id);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &event);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_int32_t data = 0;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &data);
    dbus_message_iter_close_container(&iter, &variant);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &timestamp);
    sni_call_async(message, nullptr);
}

/*************************************************
 *
 * SNI dbusmenu Popup Window
 *
 *************************************************/

struct SniMenuData : UserData {
    std::string key;
    std::vector<TrayMenuItem> items;
    std::vector<int> path; // open submenu indices
};

static constexpr int sni_menu_row_h = 30;
static constexpr int sni_menu_sep_h = 9;
static constexpr int sni_menu_w = 256;
static constexpr int sni_menu_pad = 6;

static const std::vector<TrayMenuItem> &sni_menu_level(SniMenuData *data) {
    const std::vector<TrayMenuItem> *level = &data->items;
    for (int index : data->path) {
        if (index < 0 || index >= (int) level->size())
            break;
        level = &(*level)[index].children;
    }
    return *level;
}

static int sni_menu_content_height(const std::vector<TrayMenuItem> &items, bool back_row) {
    int h = back_row ? (sni_menu_row_h + sni_menu_sep_h) : 0;
    int deepest = 0;
    for (auto &item : items) {
        if (!item.visible)
            continue;
        h += item.separator ? sni_menu_sep_h : sni_menu_row_h;
        if (!item.children.empty())
            deepest = std::max(deepest, sni_menu_content_height(item.children, true));
    }
    return std::max(h, deepest);
}

// -1 = Back row, -2 = separator, >= 0 = index into current level
static std::vector<std::pair<int, Bounds>> sni_menu_rows(SniMenuData *data, const Bounds &area, double dpi) {
    std::vector<std::pair<int, Bounds>> rows;
    double y = area.y;
    if (!data->path.empty()) {
        rows.push_back({-1, Bounds(area.x, y, area.w, sni_menu_row_h * dpi)});
        y += sni_menu_row_h * dpi;
        rows.push_back({-2, Bounds(area.x, y, area.w, sni_menu_sep_h * dpi)});
        y += sni_menu_sep_h * dpi;
    }
    const auto &level = sni_menu_level(data);
    for (int i = 0; i < (int) level.size(); i++) {
        if (!level[i].visible)
            continue;
        double h = (level[i].separator ? sni_menu_sep_h : sni_menu_row_h) * dpi;
        rows.push_back({level[i].separator ? -2 : i, Bounds(area.x, y, area.w, h)});
        y += h;
    }
    return rows;
}

static Bounds sni_menu_area(Container *c, double dpi) {
    auto b = c->real_bounds;
    double p = sni_menu_pad * dpi;
    return Bounds(b.x + p, b.y + p, b.w - p * 2, b.h - p * 2);
}

static void paint_sni_menu(AppClient *client, cairo_t *, Container *container) {
    double dpi = config->dpi;
    ArgbColor bg = correct_opaqueness(client, config->color_pin_menu_background);
    draw_colored_rect(client, bg, container->real_bounds);

    auto *data = (SniMenuData *) container->user_data;
    if (!data)
        return;
    const auto &level = sni_menu_level(data);
    auto rows = sni_menu_rows(data, sni_menu_area(container, dpi), dpi);

    for (auto &[index, rb] : rows) {
        if (index == -2) {
            ArgbColor sep = config->color_pin_menu_text;
            sep.a *= 0.18;
            draw_colored_rect(client, sep,
                              Bounds(rb.x + 8 * dpi, std::round(rb.y + rb.h * 0.5),
                                     rb.w - 16 * dpi, std::max(1.0, std::floor(dpi))));
            continue;
        }
        bool enabled = (index == -1) || level[index].enabled;
        bool hovered = container->state.mouse_hovering &&
                       bounds_contains(rb, client->mouse_current_x, client->mouse_current_y);
        if (hovered && enabled) {
            ArgbColor hl = container->state.mouse_pressing
                                   ? config->color_pin_menu_pressed_item
                                   : config->color_pin_menu_hovered_item;
            draw_colored_rect(client, hl, rb);
        }

        ArgbColor ink = config->color_pin_menu_text;
        if (!enabled)
            ink.a *= 0.4;

        std::string label = (index == -1) ? "‹  Back" : level[index].label;
        draw_clip_begin(client, Bounds(rb.x, rb.y, rb.w - 22 * dpi, rb.h));
        draw_text(client, 9 * dpi, config->font, EXPAND(ink), label, rb, 5, (int) (28 * dpi));
        draw_clip_end(client);

        if (index >= 0) {
            const auto &item = level[index];
            if (item.toggle_state == 1 && !item.toggle_type.empty()) {
                std::string mark = (item.toggle_type == "radio") ? "●" : "✓";
                draw_text(client, 9 * dpi, config->font, EXPAND(ink), mark,
                          Bounds(rb.x, rb.y, 28 * dpi, rb.h), 5, -1, -1);
            }
            if (!item.children.empty()) {
                draw_text(client, 11 * dpi, config->font, EXPAND(ink), "›",
                          Bounds(rb.x + rb.w - 22 * dpi, rb.y, 22 * dpi, rb.h), 5, -1, -1);
            }
        }
    }
}

static void sni_menu_motion(AppClient *client, cairo_t *, Container *) {
    request_refresh(app, client);
}

static void sni_menu_click(AppClient *client, cairo_t *, Container *container) {
    auto *data = (SniMenuData *) container->user_data;
    if (!data)
        return;
    double dpi = config->dpi;
    const auto &level = sni_menu_level(data);
    for (auto &[index, rb] : sni_menu_rows(data, sni_menu_area(container, dpi), dpi)) {
        if (!bounds_contains(rb, client->mouse_current_x, client->mouse_current_y))
            continue;
        if (index == -1) {
            data->path.pop_back();
            request_refresh(app, client);
        } else if (index >= 0 && level[index].enabled) {
            if (!level[index].children.empty()) {
                data->path.push_back(index);
                request_refresh(app, client);
            } else {
                int id = level[index].id;
                std::string key = data->key;
                client_close_threaded(app, client);
                sni_menu_client = nullptr;
                sni_menu_clicked(key, id);
            }
        }
        return;
    }
}

static void on_sni_menu_closed(AppClient *) {
    sni_menu_client = nullptr;
}

static void open_sni_menu(std::string key, std::vector<TrayMenuItem> items, int anchor_x) {
    auto *taskbar = client_by_name(app, "taskbar");
    if (!taskbar)
        return;
    if (sni_menu_client) {
        client_close_threaded(app, sni_menu_client);
        sni_menu_client = nullptr;
    }
    if (display) {
        client_close_threaded(app, display);
        display = nullptr;
    }

    double dpi = config->dpi;
    int content_h = sni_menu_content_height(items, false);
    if (content_h <= 0)
        return;

    Settings settings;
    settings.w = (uint16_t) std::round(sni_menu_w * dpi);
    settings.h = (uint16_t) std::min<int>((int) std::round(640 * dpi),
                                          (int) std::round((content_h + sni_menu_pad * 2) * dpi));
    settings.x = (int16_t) (anchor_x - settings.w / 2);
    settings.y = (int16_t) (taskbar->bounds->y - settings.h);
    if (settings.x + settings.w > taskbar->bounds->x + taskbar->bounds->w)
        settings.x = (int16_t) (taskbar->bounds->x + taskbar->bounds->w - settings.w);
    if (settings.x < taskbar->bounds->x)
        settings.x = (int16_t) taskbar->bounds->x;
    if (settings.y < 0)
        settings.y = 0;

    settings.decorations = false;
    settings.force_position = true;
    settings.skip_taskbar = true;
    settings.override_redirect = !app->wayland;
    settings.window_transparent = true;
    settings.background = argb_to_color(config->color_pin_menu_background);

    PopupSettings popup_settings;
    popup_settings.name = "sni_menu";
    sni_menu_client = taskbar->create_popup(popup_settings, settings);
    if (!sni_menu_client)
        return;

    auto *data = new SniMenuData;
    data->key = std::move(key);
    data->items = std::move(items);
    sni_menu_client->root->user_data = data;
    sni_menu_client->root->when_paint = paint_sni_menu;
    sni_menu_client->root->when_mouse_motion = sni_menu_motion;
    sni_menu_client->root->when_mouse_leaves_container = sni_menu_motion;
    sni_menu_client->root->when_clicked = sni_menu_click;
    sni_menu_client->when_closed = on_sni_menu_closed;
    client_show(app, sni_menu_client);
}

/*************************************************
 *
 * SNI Icon Resolution & Surface Cache
 *
 *************************************************/

static std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

static std::string resolve_sni_icon_file(const std::string &name, const std::string &theme_path, int size) {
    namespace fs = std::filesystem;
    if (name.empty())
        return "";
    std::error_code ec;
    if (name[0] == '/')
        return fs::exists(name, ec) ? name : "";

    if (!theme_path.empty()) {
        for (auto ext : {".svg", ".png", ".xpm"}) {
            auto path = theme_path + "/" + name + ext;
            if (fs::exists(path, ec))
                return path;
        }
        std::string best;
        fs::recursive_directory_iterator it(theme_path, fs::directory_options::skip_permission_denied, ec), end;
        for (int visited = 0; !ec && it != end && visited < 2000; it.increment(ec), visited++) {
            if (!it->is_regular_file(ec))
                continue;
            auto path = it->path();
            if (path.stem() != name)
                continue;
            auto ext = path.extension().string();
            if (ext != ".png" && ext != ".svg" && ext != ".xpm")
                continue;
            best = path.string();
            if (ext == ".svg")
                break;
        }
        if (!best.empty())
            return best;
    }

    std::vector<IconTarget> targets;
    targets.emplace_back(name);
    std::string lower = to_lower_copy(name);
    if (lower != name)
        targets.emplace_back(lower);
    search_icons(targets);
    pick_best(targets, size);
    for (auto &t : targets) {
        if (!t.best_full_path.empty())
            return t.best_full_path;
        if (!t.candidates.empty())
            return t.candidates[0].full_path();
    }
    return "";
}

static SniIconCache &get_sni_icon_cache(AppClient *client, const SniItem *item, uint32_t size) {
    bool needs_attention = (item->status == "NeedsAttention");
    bool use_attention = needs_attention && (!item->attention_icon_name.empty() || !item->attention_pixmap.argb.empty());
    unsigned long rev = item->icon_revision + (use_attention ? 1000000000ul : 0);

    auto &cache = sni_icon_cache[item->key];
    if (cache.attempted && cache.revision == rev && cache.size == size)
        return cache;

    cache.clear();
    cache.revision = rev;
    cache.size = size;
    cache.attempted = true;

    const std::string &icon_name = use_attention ? item->attention_icon_name : item->icon_name;
    const TrayIconPixels &pixels = use_attention ? item->attention_pixmap : item->pixmap;

    std::string path = resolve_sni_icon_file(icon_name, item->theme_path, (int) size);
    if (!path.empty()) {
        load_icon_full_path(app, client, &cache.surface, path, (int) size);
    }
    if (!cache.surface && pixels.w > 0 && pixels.h > 0 && !pixels.argb.empty()) {
        cairo_surface_t *raw = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixels.w, pixels.h);
        cairo_surface_flush(raw);
        int stride = cairo_image_surface_get_stride(raw);
        unsigned char *dst = cairo_image_surface_get_data(raw);
        for (int y = 0; y < pixels.h; y++) {
            memcpy(dst + (size_t) y * stride,
                   pixels.argb.data() + (size_t) y * pixels.w,
                   (size_t) pixels.w * 4);
        }
        cairo_surface_mark_dirty(raw);

        cache.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int) size, (int) size);
        cairo_t *scale_cr = cairo_create(cache.surface);
        double scale = (double) size / std::max(pixels.w, pixels.h);
        cairo_translate(scale_cr, (size - pixels.w * scale) * 0.5, (size - pixels.h * scale) * 0.5);
        cairo_scale(scale_cr, scale, scale);
        cairo_set_source_surface(scale_cr, raw, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(scale_cr), CAIRO_FILTER_GOOD);
        cairo_paint(scale_cr);
        cairo_destroy(scale_cr);
        cairo_surface_destroy(raw);
    }
    if (cache.surface) {
        cache.gsurf = new gl_surface();
    }
    return cache;
}

static int sni_index_at(AppClient *client) {
    if (!client || current_grid_w <= 0 || container_size == 0)
        return -1;
    if (client->mouse_current_x < 0 || client->mouse_current_y < 0)
        return -1;
    int col = client->mouse_current_x / (int) container_size;
    int row = client->mouse_current_y / (int) container_size;
    if (col < 0 || col >= current_grid_w || row < 0)
        return -1;
    int cell = row * current_grid_w + col;
    int sni_idx = cell - (int) systray_icons.size();
    auto sni = visible_sni_items();
    if (sni_idx < 0 || sni_idx >= (int) sni.size())
        return -1;
    return sni_idx;
}

/*************************************************
 *
 * Display Popup Painting & Input (XEMBED + SNI)
 *
 *************************************************/

unsigned long
create_rgba(int r, int g, int b, int a) {
    return ((a & 0xff) << 24) + ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

static void
paint_display(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor bg_color = correct_opaqueness(client, config->color_systray_background);

    for (size_t i = 0; i < systray_icons.size(); i++) {
        Systray_Icon *icon = systray_icons[i];
        int r = (int) (bg_color.r * 255);
        int g = (int) (bg_color.g * 255);
        int b = (int) (bg_color.b * 255);
        int a = (int) (bg_color.a * 255);
        uint32_t rgb = create_rgba(r, g, b, a);
        uint32_t value_list[] = {rgb};
        xcb_change_window_attributes(app->connection, icon->window, XCB_CW_BACK_PIXEL, value_list);
    }

    if (layout_invalid) {
        layout_systray();
    }

    for (auto icon : systray_icons) {
        xcb_map_window(app->connection, icon->window);
    }

    xcb_map_window(app->connection, client->window);
    xcb_map_subwindows(app->connection, client->window);

    xcb_flush(app->connection);

    // Paint the background
    draw_colored_rect(client, bg_color, container->real_bounds);

    // Paint SNI icons after XEMBED icons in the same grid
    auto sni = visible_sni_items();
    int hovered_sni = container->state.mouse_hovering ? sni_index_at(client) : -1;
    int base_index = (int) systray_icons.size();
    for (int i = 0; i < (int) sni.size(); i++) {
        SniItem *item = sni[i];
        int cell = base_index + i;
        int col = cell % current_grid_w;
        int row = cell / current_grid_w;
        Bounds cell_bounds(col * (double) container_size, row * (double) container_size,
                           (double) container_size, (double) container_size);

        if (i == hovered_sni) {
            ArgbColor hl = container->state.mouse_pressing
                                   ? config->color_pin_menu_pressed_item
                                   : config->color_pin_menu_hovered_item;
            draw_colored_rect(client, hl, cell_bounds);
        }
        if (item->status == "NeedsAttention") {
            ArgbColor att = config->color_taskbar_attention_background;
            att.a = 0.28;
            draw_colored_rect(client, att, cell_bounds);
        }

        int ix = (int) (cell_bounds.x + container_size / 2 - icon_size / 2);
        int iy = (int) (cell_bounds.y + container_size / 2 - icon_size / 2);

        auto &cache = get_sni_icon_cache(client, item, icon_size);
        if (cache.surface && cache.gsurf) {
            draw_gl_texture(client, cache.gsurf, cache.surface, ix, iy, (int) icon_size, (int) icon_size);
        } else {
            ArgbColor badge = config->color_pin_menu_text;
            badge.a = 0.2;
            draw_round_rect(client, badge, Bounds(ix, iy, (double) icon_size, (double) icon_size),
                            (float) icon_size * 0.5f);
            std::string letter = item->title.empty() ? "?" : item->title.substr(0, 1);
            draw_text(client, (int) (10 * config->dpi), config->font, EXPAND(config->color_pin_menu_text),
                      letter, Bounds(ix, iy, (double) icon_size, (double) icon_size), 5, -1, -1);
        }
    }
}

static void display_mouse_motion(AppClient *client, cairo_t *, Container *) {
    request_refresh(app, client);
}

static void display_clicked(AppClient *client, cairo_t *, Container *container) {
    int idx = sni_index_at(client);
    if (idx < 0)
        return;
    auto sni = visible_sni_items();
    if (idx >= (int) sni.size())
        return;
    SniItem *item = sni[idx];

    int button = 1;
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3)
        button = 3;
    else if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_2)
        button = 2;

    int root_x = (int) (client->bounds->x + client->mouse_current_x);
    int root_y = (int) (client->bounds->y + client->mouse_current_y);
    std::string key = item->key;
    bool show_menu = !item->menu_path.empty() && (button == 3 || (button == 1 && item->item_is_menu));

    client_close_threaded(app, client);
    display = nullptr;

    if (show_menu) {
        sni_fetch_menu(key, [key, root_x, root_y](std::vector<TrayMenuItem> menu) {
            if (menu.empty() || sni_menu_content_height(menu, false) <= 0) {
                sni_click(key, 3, root_x, root_y);
                return;
            }
            open_sni_menu(key, std::move(menu), root_x);
        });
    } else {
        sni_click(key, button, root_x, root_y);
    }
}

static void display_scrolled(AppClient *client, cairo_t *, Container *, int scroll_x, int scroll_y, bool) {
    int idx = sni_index_at(client);
    if (idx < 0)
        return;
    auto sni = visible_sni_items();
    if (idx >= (int) sni.size())
        return;
    bool horizontal = std::abs(scroll_x) > std::abs(scroll_y);
    int amount = horizontal ? scroll_x : scroll_y;
    if (amount == 0)
        return;
    int delta = amount > 0 ? 1 : -1;
    sni_scroll(sni[idx]->key, delta, horizontal);
}

/*************************************************
 *
 * Legacy XEMBED System Tray
 *
 *************************************************/

static bool
systray_event_handler(App *app, xcb_generic_event_t *event, xcb_window_t) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CLIENT_MESSAGE: {
            auto *client_message = (xcb_client_message_event_t *) event;

            if (client_message->type == get_cached_atom(app, "_NET_SYSTEM_TRAY_OPCODE")) {
                if (client_message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK) {
                    auto window_to_be_docked = client_message->data.data32[2];

                    for (auto icon : systray_icons)
                        if (icon->window == window_to_be_docked)
                            return false;

                    const uint32_t cw_values[] = {
                            XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                            XCB_EVENT_MASK_PROPERTY_CHANGE};
                    xcb_change_window_attributes(app->connection, window_to_be_docked,
                                                 XCB_CW_EVENT_MASK, cw_values);
                    xcb_change_save_set(app->connection, XCB_SET_MODE_INSERT, window_to_be_docked);

                    auto icon = new Systray_Icon;
                    icon->window = window_to_be_docked;
                    systray_icons.push_back(icon);

                    xcb_unmap_window(app->connection, icon->window);

                    layout_invalid = true;
                }
            }
        }
            break;
    }

    return false;
}

// The XEmbed Protocol says that when you re-parent a window into your window
// You should basically act in the way that a windows manager acts a.k.a
// Selecting the SubstrucreRedirectMask and intercepting events on it like mapping/unmapping
// configuring and so on. So that's what we do in this icon_event_handler
static bool
icon_event_handler(App *app, xcb_generic_event_t *generic_event, xcb_window_t) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Since this function looks at every single xcb event generated
    // we first need to filter out windows that are not clients (icons) we are handling
    xcb_window_t event_window = get_window(generic_event);

    bool window_is_systray_icon = false;
    for (auto icon : systray_icons)
        if (icon->window == event_window)
            window_is_systray_icon = true;
    if (!window_is_systray_icon)
        return false; // Let someone else handle the event

    switch (XCB_EVENT_RESPONSE_TYPE(generic_event)) {
        case XCB_DESTROY_NOTIFY: {
            for (int i = 0; i < (int) systray_icons.size(); i++) {
                auto icon = systray_icons[i];
                if (icon->window == event_window) {
                    systray_icons.erase(systray_icons.begin() + i);
                    delete icon;
                    break;
                }
            }
            layout_invalid = true;

            if (systray_icons.empty() && visible_sni_items().empty()) {
                display_close();
            }
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            break;
        }
        case XCB_MAP_NOTIFY: {
            break;
        }
    }
    layout_invalid = true;

    return true;
}

static int
closest_square_root_above(int target) {
    int i = 0;
    while (i < 100) {
        if (i * i >= target)
            return i;
        i++;
    }
    return 0;
}

static void
layout_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto sni = visible_sni_items();
    int total_count = (int) systray_icons.size() + (int) sni.size();

    int w = closest_square_root_above(total_count);
    if (w == 0) { // even if the systray has no icons we want to show a 1x1
        w = 1;
    } else if (w > 4) { // after we reach a width of 4 icons we just want to grow upwards
        w = 4;
    }
    current_grid_w = w;

    int x = 0;
    int y = 0;
    // Position XEMBED icons first
    for (size_t i = 0; i < systray_icons.size(); i++) {
        if (x == w) {
            x = 0;
            y++;
        }

        Systray_Icon *icon = systray_icons[i];

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                              XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {
                (uint32_t) (x * container_size + container_size / 2 - icon_size / 2),
                (uint32_t) (y * container_size + container_size / 2 - icon_size / 2),
                icon_size,
                icon_size};
        xcb_configure_window(app->connection, icon->window, value_mask, value_list_resize);
        if (display && !systray_icons[i]->reparented && systray_icons[i]->times++) {
            xcb_reparent_window(app->connection, systray_icons[i]->window, display->window,
                                value_list_resize[0], value_list_resize[1]);
            if (systray_icons[i]->times > 10) {
                systray_icons[i]->reparented = true;
            }
        }

        x++;
    }

    int rows = std::max(1, (total_count + w - 1) / w);

    // If the display window (which holds our icons) is open, move and resize it to the correct spot
    if (display) {
        layout_invalid = false;

        auto window_width = (uint32_t) container_size * w;
        auto window_height = (uint32_t) container_size * rows;

        int window_x = 0;
        auto window_y = (uint32_t) (app->bounds.h - config->taskbar_height - window_height);

        if (auto taskbar = client_by_name(app, "taskbar")) {
            window_x = taskbar->bounds->x;
            window_y = taskbar->bounds->y - window_height;
            if (auto container = container_by_name("systray", taskbar->root)) {
                window_x += container->real_bounds.x + container->real_bounds.w / 2 - window_width / 2;
                if (!container->exists)
                    window_x = 0;
                if (window_x + (int) window_width >= taskbar->screen_information->width_in_pixels)
                    window_x = taskbar->screen_information->width_in_pixels - window_width;
            }
        }
        if (window_x <= 0)
            window_x = 0;

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                              XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {(uint32_t) window_x, window_y, window_width, window_height};
        xcb_configure_window(app->connection, display->window, value_mask, value_list_resize);
    }
}

static void
on_display_closed(AppClient *) {
    if (systray)
        for (auto icon : systray_icons) {
            xcb_reparent_window(app->connection, icon->window, systray->window, 0, 0);
            icon->reparented = false;
            icon->times = 0;
        }
    sni_icon_cache.clear();
    display = nullptr;
}

static void
on_systray_closed(AppClient *) {
    if (systray)
        for (auto icon : systray_icons) {
            xcb_reparent_window(app->connection, icon->window, app->screen->root, 0, 0);
            icon->reparented = false;
            icon->times = 0;
        }
}

void register_as_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Settings settings;
    settings.window_transparent = false;
    settings.background = argb_to_color(config->color_systray_background);
    systray = client_new(app, settings, "systray");
    systray->keeps_app_running = false;
    systray->when_closed = on_systray_closed;

    std::string selection = "_NET_SYSTEM_TRAY_S";
    selection.append(std::to_string(app->screen_number));
    auto tray_atom = get_cached_atom(app, selection.c_str());
    xcb_set_selection_owner(app->connection, systray->window, tray_atom, XCB_CURRENT_TIME);
    auto selection_owner_cookie = xcb_get_selection_owner(app->connection, tray_atom);
    auto *selection_owner_reply =
            xcb_get_selection_owner_reply(app->connection, selection_owner_cookie, NULL);
    if (selection_owner_reply->owner != systray->window) {
        client_close_threaded(app, systray);
        systray = nullptr;
        free(selection_owner_reply);
        return;
    }
    free(selection_owner_reply);

    layout_invalid = true;
    for (auto a : systray_icons)
        delete a;
    systray_icons.clear();
    systray_icons.shrink_to_fit();

    app_create_custom_event_handler(app, INT_MAX, icon_event_handler);
    app_create_custom_event_handler(app, systray->window, systray_event_handler);

    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = app->screen->root;
    ev.format = 32;
    ev.type = get_cached_atom(app, "MANAGER");
    ev.data.data32[0] = 0;
    ev.data.data32[1] = tray_atom;
    ev.data.data32[2] = systray->window;
    ev.data.data32[3] = ev.data.data32[4] = 0;

    xcb_send_event_checked(app->connection, false, app->screen->root, 0xFFFFFF, (char *) &ev);
    xcb_flush(app->connection);
}

void open_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    icon_size = 22 * config->dpi;
    container_size = 40 * config->dpi;
    if (!systray && !sni_running) {
        printf("Can't open systray display because neither XEMBED nor SNI is registered\n");
        return;
    }

    Settings settings;
    // Very important that the window is not 32 bit depth because you can't embed non transparent windows into transparent ones
    settings.window_transparent = false;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.w = 1;
    settings.h = 1;
    settings.x = -settings.w * 2;
    settings.y = -settings.h * 2;
    settings.no_input_focus = false;
    settings.override_redirect = true;
    if (app->wayland)
        settings.override_redirect = false;
    settings.background = argb_to_color(config->color_systray_background);

    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "display";
        display = taskbar->create_popup(popup_settings, settings);

        display->root->when_paint = paint_display;
        display->root->when_mouse_motion = display_mouse_motion;
        display->root->when_mouse_leaves_container = display_mouse_motion;
        display->root->when_clicked = display_clicked;
        display->root->when_fine_scrolled = display_scrolled;
        display->when_closed = on_display_closed;
        client_show(app, display);
        static int times = 0;
        times = 0;
        app_timeout_create(taskbar->app, display, 50, [](App *, AppClient *client, Timeout *t, void *) {
            t->keep_running = times++ < 10;
            layout_systray();
            request_refresh(app, client);
        }, nullptr, "systray relayout");

        layout_systray();

        layout_invalid = true;
    }
}

void display_close_timeout(App *app, AppClient *, Timeout *, void *) {
    if (display) {
        client_close_threaded(app, display);
        display = nullptr;
    }
}

static void
display_close() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    app->grab_window = 0;
    xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
    app_timeout_create(app, nullptr, 100, display_close_timeout, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
}
