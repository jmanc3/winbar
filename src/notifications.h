//
// Created by jmanc3 on 5/31/21.
//

#ifndef WINBAR_NOTIFICATIONS_H
#define WINBAR_NOTIFICATIONS_H

#include <vector>
#include <string>
#include <dbus/dbus.h>

struct NotificationInfo {
    dbus_uint32_t id = 0;
    long time_started = 0;
    std::string icon_path;

    std::string app_name;
    std::string app_icon;
    std::string summary;
    std::string body;
    dbus_int32_t expire_timeout_in_milliseconds = -1; // 0 means never, -1 means the server (us) decides
};

extern std::vector<NotificationInfo *> notifications;

struct App;

void start_notification_interceptor(App *app);

void stop_notification_interceptor(App *app);


#endif //WINBAR_NOTIFICATIONS_H
