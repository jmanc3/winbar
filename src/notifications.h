//
// Created by jmanc3 on 5/31/21.
//

#ifndef WINBAR_NOTIFICATIONS_H
#define WINBAR_NOTIFICATIONS_H

#include <vector>
#include <string>
#include <dbus/dbus.h>
#include <pango/pango-font.h>

struct NotificationInfo;

struct NotificationAction {
    std::string id;
    std::string label;
    
    void (*callback)(NotificationInfo *) = nullptr;
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
    bool sent_by_winbar = false;
    
    void (*on_ignore)(NotificationInfo *) = nullptr;
    
    void *user_data = nullptr;
    
    std::string app_name;
    std::string app_icon;
    std::string summary;
    std::string body;
    dbus_int32_t expire_timeout_in_milliseconds = -1; // 0 means never, -1 means the server (us) decides
    
    std::vector<NotificationAction> actions;
    
    std::string x_kde_appname;
    std::string x_kde_origin_name;
    std::string x_kde_display_appname;
    std::string desktop_entry;
    std::string x_kde_eventId;
    std::string x_kde_reply_placeholder_text = "Reply...";
    std::string x_kde_reply_submit_button_text = "Send";
    std::string x_kde_reply_submit_button_icon_name = "document-send";
    std::vector<std::string> x_kde_urls;
};

struct Container;

struct App;

struct AppClient;

extern std::vector<NotificationInfo *> notifications;

extern std::vector<AppClient *> displaying_notifications;

void show_notification(NotificationInfo *ni);

void close_notification(int id);

std::string strip_html(const std::string &text);

int determine_height_of_text(App *app, std::string text, PangoWeight weight, int size, int width);


#endif //WINBAR_NOTIFICATIONS_H
