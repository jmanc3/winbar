//
// Created by jmanc3 on 5/31/21.
//

#include "notifications.h"
#include "application.h"
#include "defer.h"
#include "config.h"
#include "taskbar.h"
#include "utility.h"
#include "icons.h"

#include <dbus/dbus.h>
#include <cstring>
#include <pango/pangocairo.h>

std::vector<NotificationInfo *> notifications;

static std::vector<AppClient *> displaying_notifications;

static const int notification_width = 365;

struct NotificationWrapper : public IconButton {
    NotificationInfo *ni = nullptr;
};

struct NotificationActionWrapper : public UserData {
    NotificationAction action;
};

struct LabelData : UserData {
    std::string text;
    PangoWeight weight;
    int size;
};

enum NotificationReasonClosed {
    EXPIRED = 1,
    DISMISSED_BY_USER = 2,
    CLOSED_BY_CLOSE_NOTIFICATION_CALL = 3,
    UNDEFINED_OR_RESERVED_REASON = 4,
};

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
    for (auto &i : array) {
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &i))
            success = false;
    }

    if (success)
        success = dbus_connection_send(connection, reply, NULL);
    return success;
}

static void notification_closed_signal(App *app, NotificationInfo *ni, NotificationReasonClosed reason);

static void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action);

static void show_notification(App *app, NotificationInfo *ni);

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
        if (::dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &result, DBUS_TYPE_INVALID)) {
            for (auto c : displaying_notifications) {
                auto data = (NotificationWrapper *) c->root->user_data;
                if (data->ni->id == result) {
                    notification_closed_signal(c->app, data->ni,
                                               NotificationReasonClosed::CLOSED_BY_CLOSE_NOTIFICATION_CALL);
                    client_close_threaded(c->app, c);
                }
            }
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
        notification_info->time_started = get_current_time_in_ms();
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

void start_notification_interceptor(App *app) {
    if (app->dbus_connection) {
        // TODO: instead of checking what the result of the request_name was
        //  have a filter and check for name_aquired and name_lost. that way we can allow
        //  ourselves to be put in queue and if the user closes another Notification manager
        //  we will become the manager
        DBusError error = DBUS_ERROR_INIT;
        int result = ::dbus_bus_request_name(app->dbus_connection, "org.freedesktop.Notifications",
                                             DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);

        if (dbus_error_is_set(&error)) {
            fprintf(stderr, "Ran into error when trying to become the sessions notification manager (%s)\n",
                    error.message);
            dbus_error_free(&error);
            return;
        }
        static const DBusObjectPathVTable vtable = {
                .message_function = handle_message_cb,
        };
        if (result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            ::dbus_connection_register_object_path(app->dbus_connection, "/org/freedesktop/Notifications", &vtable,
                                                   app);
        }
    }
}

void stop_notification_interceptor(App *app) {
    for (auto n : notifications) {
        delete n;
    }
    notifications.clear();
    notifications.shrink_to_fit();
    if (app->dbus_connection) {
        ::dbus_connection_unregister_object_path(app->dbus_connection, "/org/freedesktop/Notifications");
        ::dbus_bus_release_name(app->dbus_connection, "org.freedesktop.Notifications", nullptr);
    }
}

static void close_notification_timeout(App *app, AppClient *client, void *data) {
    auto root_data = (NotificationWrapper *) client->root->user_data;
    notification_closed_signal(app, root_data->ni, NotificationReasonClosed::EXPIRED);
    client_close_threaded(app, client);
}

static void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;

    set_argb(cr, config->color_volume_background);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    if (data->surface) {
        if (container->state.mouse_hovering || container->state.mouse_pressing) {
            if (container->state.mouse_pressing) {
                dye_surface(data->surface, ArgbColor(.7, .7, .7, 1));
            } else {
                dye_surface(data->surface, ArgbColor(.9, .9, .9, 1));
            }
        } else {
            dye_surface(data->surface, ArgbColor(.4, .4, .4, 1));
        }

        // TODO: dye this surface the variable in config that we need to create
        cairo_set_source_surface(cr, data->surface,
                                 (int) (container->real_bounds.x + container->real_bounds.w - 16 - 12),
                                 (int) (container->real_bounds.y + 12));
        cairo_paint(cr);
    }
}

static void clicked_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;
    // TODO I think this should invoke "default" action
    notification_closed_signal(client->app, data->ni, NotificationReasonClosed::DISMISSED_BY_USER);
    client_close_threaded(client->app, client);
}

static int determine_height_of_text(App *app, std::string text, PangoWeight weight, int size, int width) {
    if (text.empty())
        return 0;

    int height = size;

    if (auto c = client_by_name(app, "taskbar")) {
        PangoLayout *layout =
                get_cached_pango_font(c->cr, config->font, size, weight);

        pango_layout_set_width(layout, width * PANGO_SCALE);
        pango_layout_set_text(layout, text.c_str(), text.length());

        PangoRectangle text_ink;
        PangoRectangle text_logical;
        pango_layout_get_extents(layout, &text_ink, &text_logical);

        height = text_logical.height / PANGO_SCALE;

        pango_layout_set_width(layout, -1);
    }

    return height;
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LabelData *) container->user_data;

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, data->size, data->weight);

    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, data->text.c_str(), data->text.length());
    pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);

    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  container->real_bounds.x,
                  container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_width(layout, -1);
}

static void paint_notify(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, darken(config->color_volume_background, 4));
    cairo_fill(cr);

    std::string text = "Interaction Required";

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, 10, PANGO_WEIGHT_BOLD);

    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, text.c_str(), text.length());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 -
                  ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void paint_icon(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;
    if (data->surface) {
        cairo_set_source_surface(cr, data->surface,
                                 container->real_bounds.x + 16,
                                 container->real_bounds.y + 25);
        cairo_paint(cr);
    }
}

static void paint_action(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationActionWrapper *) container->user_data;
    auto action = data->action;

    auto color = ArgbColor(.294, .294, .294, 1);
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, darken(color, 3));
        } else {
            set_argb(cr, lighten(color, 3));
        }
    } else {
        set_argb(cr, color);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    std::string text = action.label;

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, 11, PANGO_WEIGHT_NORMAL);

    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, text.c_str(), text.length());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 -
                  ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}


static void clicked_action(AppClient *client, cairo_t *cr, Container *container) {
    auto client_wrapper = (NotificationWrapper *) client->root->user_data;
    auto action_wrapper = (NotificationActionWrapper *) container->user_data;
    notification_action_invoked_signal(client->app, client_wrapper->ni, action_wrapper->action);

    app_timeout_create(client->app, client, 300, close_notification_timeout, nullptr);
//    notification_closed_signal(client->app, client_wrapper->ni, NotificationReasonClosed::DISMISSED_BY_USER);
//    client_close_threaded(client->app, client);
}

static void client_closed(AppClient *client) {
    for (int i = 0; i < displaying_notifications.size(); i++) {
        if (displaying_notifications[i] == client) {
            displaying_notifications.erase(displaying_notifications.begin() + i);

            i--;
            for (int x = i; x >= 0; x--) {
                auto x_client = displaying_notifications[x];
                client_set_position(client->app, x_client, x_client->bounds->x,
                                    x_client->bounds->y + client->bounds->h + 12);
            }
            return;
        }
    }
}

static Container *create_notification_container(App *app, NotificationInfo *notification_info);

static void show_notification(App *app, NotificationInfo *ni) {
    auto notification_container = create_notification_container(app, ni);

    Settings settings;
    settings.force_position = true;
    settings.decorations = false;
    settings.w = notification_width;
    settings.h = notification_container->real_bounds.h;
    settings.x = app->bounds.w - settings.w - 12;
    settings.y = app->bounds.h - config->taskbar_height - settings.h - 12;
    settings.sticky = true;
    settings.skip_taskbar = true;
    settings.keep_above = true;

    auto client = client_new(app, settings, "winbar_notification_" + std::to_string(ni->id));
    xcb_atom_t atom = get_cached_atom(app, "_NET_WM_WINDOW_TYPE_NOTIFICATION");
    xcb_ewmh_set_wm_window_type(&app->ewmh, client->window, 1, &atom);
    delete client->root;
    client->root = notification_container;

    if (auto icon_container = container_by_name("icon", client->root)) {
        auto icon_data = (IconButton *) icon_container->user_data;
        load_icon_full_path(app, client, &icon_data->surface, ni->icon_path, 48);
    }

    client->when_closed = client_closed;

    for (auto c : displaying_notifications) {
        client_set_position(app, c, c->bounds->x, c->bounds->y - notification_container->real_bounds.h - 12);
    }

    client_show(app, client);
    displaying_notifications.push_back(client);


    if (ni->expire_timeout_in_milliseconds <= 0) {
        int text_length = ni->summary.length() + ni->body.length();
        int timeout = 60000 * text_length / 6 / 200;
        timeout += 2000;
        if (ni->expire_timeout_in_milliseconds == 0 && !ni->actions.empty()) {
            timeout += 5000;
        }
        if (timeout < 5000) {
            timeout = 5000;
        }

        app_timeout_create(app, client, timeout, close_notification_timeout, nullptr);
    } else {
        app_timeout_create(app, client, ni->expire_timeout_in_milliseconds, close_notification_timeout, nullptr);
    }
}

static bool root_pierced_handler(Container *container, int mouse_x, int mouse_y) {
    if (auto c = container_by_name("actions_container", container)) {
        for (auto child : c->children) {
            if (bounds_contains(child->real_bounds, mouse_x, mouse_y)) {
                return false;
            }
        }
    }
    return bounds_contains(container->real_bounds, mouse_x, mouse_y);
}

static Container *create_notification_container(App *app, NotificationInfo *notification_info) {
    auto container = new Container(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    auto data = new NotificationWrapper;
    data->ni = notification_info;
    container->user_data = data;
    container->when_paint = paint_root;
    container->when_clicked = clicked_root;
    container->handles_pierced = root_pierced_handler;
    container->receive_events_even_if_obstructed = true;

    // -------------------
    // | (a) |   b   | c |
    // -------------------
    // |        (d)      |
    // -------------------
    //
    // (a): optional icon
    //  b : (summary), body, (app)
    //  c : close/send to action center button shown only when mouse is in container
    // (d): optional actions. stack vertically

    std::string title_text = notification_info->summary;
    std::string body_text = notification_info->body;
    std::string subtitle_text = notification_info->app_name;

    // The subtitle can be used as the title if no title was sent
    if (title_text.empty() && !subtitle_text.empty()) {
        title_text = subtitle_text;
        subtitle_text.clear();
    }
    // If only a body was sent, set it as the title
    if (title_text.empty() && subtitle_text.empty() && !body_text.empty()) {
        title_text = body_text;
        title_text.clear();
    }

    notification_info->icon_path = find_icon(notification_info->app_icon, 48);
    if (notification_info->icon_path.empty()) {
        if (!subtitle_text.empty()) {
            notification_info->icon_path = find_icon(subtitle_text, 48);
        }
    }

    bool has_icon = !notification_info->icon_path.empty();

    int max_text_width = notification_width;
    max_text_width -= 16 * 3; // subtract the part taken up by [c]
    if (has_icon) {
        max_text_width -= 16 * 2 + 48; // subtract the part taken up [a]
    } else {
        max_text_width -= 55; // subtract the padding used instead of [a]
    }

    int container_height = 20 * 2; // The top and bottom padding
    int title_height = determine_height_of_text(app, title_text, PangoWeight::PANGO_WEIGHT_BOLD, 11, max_text_width);
    container_height += title_height;
    int body_height = determine_height_of_text(app, body_text, PangoWeight::PANGO_WEIGHT_NORMAL, 11, max_text_width);
    container_height += body_height;
    int subtitle_height = determine_height_of_text(app, subtitle_text, PangoWeight::PANGO_WEIGHT_NORMAL, 9,
                                                   max_text_width);
    container_height += subtitle_height;
    if (!title_text.empty() && !body_text.empty())
        container_height += 1;
    if (!subtitle_text.empty())
        container_height += 3;
    if (has_icon)
        container_height = container_height < (20 * 2 + 48) ? 20 * 2 + 48 : container_height;

    if (notification_info->expire_timeout_in_milliseconds == 0) {
        container_height += 40;
    }

    int icon_content_close_height = container_height;

    if (!notification_info->actions.empty()) {
        container_height += 16; // for the bottom pad

        container_height += notification_info->actions.size() * 32;
        container_height += notification_info->actions.size() - 1 * 4; // pad between each option
    }

    container->real_bounds.w = notification_width;
    container->real_bounds.h = container_height;

    if (notification_info->expire_timeout_in_milliseconds == 0) {
        auto notify_user_container = container->child(FILL_SPACE, 40);
        notify_user_container->when_paint = paint_notify;
    }

    auto icon_content_close_hbox = container->child(layout_type::hbox, FILL_SPACE, FILL_SPACE);

    if (has_icon) {
        auto icon = icon_content_close_hbox->child(48 + 16 * 2, FILL_SPACE);
        icon->name = "icon";
        auto icon_data = new IconButton;
        icon->user_data = icon_data;
        icon->when_paint = paint_icon;
    } else {
        icon_content_close_hbox->child(55, FILL_SPACE);
    }

    auto content = icon_content_close_hbox->child(max_text_width, FILL_SPACE);
    content->child(FILL_SPACE, FILL_SPACE);
    if (!title_text.empty()) {
        auto title_label = content->child(FILL_SPACE, title_height);
        title_label->when_paint = paint_label;
        auto title_label_data = new LabelData;
        title_label->user_data = title_label_data;
        title_label_data->size = 11;
        title_label_data->weight = PangoWeight::PANGO_WEIGHT_BOLD;
        title_label_data->text = title_text;
    }
    if (!body_text.empty()) {
        content->child(FILL_SPACE, 1);
        auto body_label = content->child(FILL_SPACE, body_height);
        body_label->when_paint = paint_label;
        auto body_label_data = new LabelData;
        body_label->user_data = body_label_data;
        body_label_data->size = 11;
        body_label_data->weight = PangoWeight::PANGO_WEIGHT_NORMAL;
        body_label_data->text = body_text;
    }
    if (!subtitle_text.empty()) {
        content->child(FILL_SPACE, 3);
        auto subtitle_label = content->child(FILL_SPACE, subtitle_height);
        subtitle_label->when_paint = paint_label;
        auto subtitle_label_data = new LabelData;
        subtitle_label->user_data = subtitle_label_data;
        subtitle_label_data->size = 9;
        subtitle_label_data->weight = PangoWeight::PANGO_WEIGHT_NORMAL;
        subtitle_label_data->text = subtitle_text;
    }
    content->child(FILL_SPACE, FILL_SPACE);

    auto close = icon_content_close_hbox->child(16 * 3, FILL_SPACE);

    if (!notification_info->actions.empty()) {
        auto actions_container = container->child(layout_type::vbox, FILL_SPACE,
                                                  container_height - icon_content_close_height);
        actions_container->spacing = 4;
        actions_container->wanted_pad = Bounds(16, 0, 16, 16);
        actions_container->name = "actions_container";

        for (auto action : notification_info->actions) {
            auto action_container = actions_container->child(FILL_SPACE, FILL_SPACE);
            auto data = new NotificationActionWrapper;
            data->action = action;
            action_container->user_data = data;
            action_container->when_paint = paint_action;
            action_container->when_clicked = clicked_action;
        }
    }

    return container;
}

static void notification_closed_signal(App *app, NotificationInfo *ni, NotificationReasonClosed reason) {
    if (!app->dbus_connection || !ni)
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

    dbus_connection_send(app->dbus_connection, dmsg, NULL);
}

static void notification_action_invoked_signal(App *app, NotificationInfo *ni, NotificationAction action) {
    if (!app->dbus_connection || !ni)
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

    dbus_connection_send(app->dbus_connection, dmsg, NULL);
}
