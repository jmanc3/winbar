//
// Created by jmanc3 on 5/31/21.
//

#ifndef WINBAR_NOTIFICATIONS_H
#define WINBAR_NOTIFICATIONS_H

#include <vector>
#include <string>
#include <dbus/dbus.h>
#include <pango/pango-font.h>

struct NotificationAction {
    std::string id;
    std::string label;
};

enum NotificationReasonClosed {
    EXPIRED = 1,
    DISMISSED_BY_USER = 2,
    CLOSED_BY_CLOSE_NOTIFICATION_CALL = 3,
    UNDEFINED_OR_RESERVED_REASON = 4,
};

struct NotificationInfo {
    dbus_uint32_t id = 0;
    std::string time_started;
    std::string icon_path;
    std::string calling_dbus_client;
    bool sent_to_action_center = false;
    bool removed_from_action_center = false;
    
    std::string app_name;
    std::string app_icon;
    std::string summary;
    std::string body;
    dbus_int32_t expire_timeout_in_milliseconds = -1; // 0 means never, -1 means the server (us) decides
    
    std::vector<NotificationAction> actions;
};

struct Container;

struct App;

struct AppClient;

extern std::vector<NotificationInfo *> notifications;

extern std::vector<AppClient *> displaying_notifications;

void show_notification(App *app, NotificationInfo *ni);

void close_notification(int id);

std::string strip_html(const std::string &text);

int determine_height_of_text(App *app, std::string text, PangoWeight weight, int size, int width);


#endif //WINBAR_NOTIFICATIONS_H
