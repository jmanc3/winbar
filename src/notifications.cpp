//
// Created by jmanc3 on 5/31/21.
//

#include "notifications.h"
#include "application.h"
#include "config.h"
#include "taskbar.h"
#include "utility.h"
#include "icons.h"
#include "simple_dbus.h"

#include <pango/pangocairo.h>

std::vector<NotificationInfo *> notifications;

std::vector<AppClient *> displaying_notifications;

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

static void close_notification_timeout(App *app, AppClient *client, Timeout *, void *data) {
    auto root_data = (NotificationWrapper *) client->root->user_data;
    notification_closed_signal(app, root_data->ni, NotificationReasonClosed::EXPIRED);
    client_close_threaded(app, client);
}

static void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;

    set_argb(cr, config->color_notification_content_background);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void clicked_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;
    // TODO I think this should invoke "default" action
    notification_closed_signal(client->app, data->ni, NotificationReasonClosed::DISMISSED_BY_USER);
    client_close_threaded(client->app, client);
}

std::string strip_html(const std::string &text) {
    std::string t = text;
    while (t.find("<") != std::string::npos) {
        auto startpos = t.find("<");
        auto endpos = t.find(">") + 1;

        if (endpos != std::string::npos) {
            t.erase(startpos, endpos - startpos);
        }
    }
    return t;
}

static int determine_height_of_text(App *app, std::string text, PangoWeight weight, int size, int width) {
    if (text.empty())
        return 0;

    int height = size;

    if (auto c = client_by_name(app, "taskbar")) {
        PangoLayout *layout =
                get_cached_pango_font(c->cr, config->font, size, weight);
        auto initial_wrap = pango_layout_get_wrap(layout);

        pango_layout_set_attributes(layout, nullptr);
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        if (attrs) {
            pango_layout_set_attributes(layout, attrs);
        }

        pango_layout_set_width(layout, width * PANGO_SCALE);
        const std::string &stripped = strip_html(text);
        pango_layout_set_text(layout, stripped.data(), stripped.length());
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

        PangoRectangle text_ink;
        PangoRectangle text_logical;
        pango_layout_get_extents(layout, &text_ink, &text_logical);

        height = text_logical.height / PANGO_SCALE;

        pango_layout_set_width(layout, -1);
        pango_layout_set_wrap(layout, initial_wrap);
    }

    return height;
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LabelData *) container->user_data;

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, data->size, data->weight);

    pango_layout_set_attributes(layout, nullptr);
    PangoAttrList *attrs = nullptr;
    pango_parse_markup(data->text.data(), data->text.length(), 0, &attrs, NULL, NULL, NULL);
    if (attrs) {
        pango_layout_set_attributes(layout, attrs);
    }

    const std::string &stripped = strip_html(data->text);

    pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    pango_layout_set_text(layout, stripped.data(), stripped.length());
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    set_argb(cr, config->color_notification_content_text);
    cairo_move_to(cr,
                  container->real_bounds.x,
                  container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_width(layout, -1);
}

static void paint_notify(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_notification_title_background);
    cairo_fill(cr);

    std::string text = "Interaction Required";

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, 10, PANGO_WEIGHT_BOLD);

    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, text.c_str(), text.length());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_notification_title_text);
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

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_notification_button_pressed);
        } else {
            set_argb(cr, config->color_notification_button_hovered);
        }
    } else {
        set_argb(cr, config->color_notification_button_default);
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

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_notification_button_text_pressed);
        } else {
            set_argb(cr, config->color_notification_button_text_hovered);
        }
    } else {
        set_argb(cr, config->color_notification_button_text_default);
    }
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

static bool send_to_action_pierced_handler(Container *container, int mouse_x, int mouse_y) {
    return bounds_contains(Bounds(container->real_bounds.x + 16, container->real_bounds.y + 16, 16, 15), mouse_x,
                           mouse_y);
}

static void clicked_send_to_action_center(AppClient *client, cairo_t *cr, Container *container) {
    auto client_wrapper = (NotificationWrapper *) client->root->user_data;
    auto icon_button = (IconButton *) container->user_data;
}

static void paint_send_to_action_center(AppClient *client, cairo_t *cr, Container *container) {
    auto client_wrapper = (NotificationWrapper *) client->root->user_data;
    auto icon_button = (IconButton *) container->user_data;
    if (icon_button->surface) {
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                dye_surface(icon_button->surface, config->color_notification_button_send_to_action_center_pressed);
            } else {
                dye_surface(icon_button->surface, config->color_notification_button_send_to_action_center_hovered);
            }
        } else {
            if (!bounds_contains(client->root->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
                return;
            }
            if (auto c = container_by_name("actions_container", client->root)) {
                for (auto co: c->children) {
                    if (bounds_contains(co->real_bounds, client->mouse_current_x, client->mouse_current_y))
                        return;
                }
            }
            dye_surface(icon_button->surface, config->color_notification_button_send_to_action_center_default);
        }
        cairo_set_source_surface(cr, icon_button->surface,
                                 container->real_bounds.x + 16 + 4,
                                 container->real_bounds.y + 18);
        cairo_paint(cr);
    }
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

static Container *create_notification_container(App *app, NotificationInfo *notification_info, int width);

void show_notification(App *app, NotificationInfo *ni) {
    auto notification_container = create_notification_container(app, ni, 356);

    Settings settings;
    settings.force_position = true;
    settings.decorations = false;
    settings.w = notification_container->real_bounds.w;
    settings.h = notification_container->real_bounds.h;
    settings.x = app->bounds.w - settings.w - 12;
    settings.y = app->bounds.h - config->taskbar_height - settings.h - 12;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w - 12;
        settings.y = taskbar->bounds->y - settings.h - 12;
    }
    settings.popup = true;
    settings.sticky = true;
    settings.skip_taskbar = true;
    settings.keep_above = true;

    auto client = client_new(app, settings, "winbar_notification_" + std::to_string(ni->id));
    xcb_atom_t atom = get_cached_atom(app, "_NET_WM_WINDOW_TYPE_NOTIFICATION");
    xcb_ewmh_set_wm_window_type(&app->ewmh, client->window, 1, &atom);
    delete client->root;
    client->root = notification_container;

    if (auto icon_container = container_by_name("icon", notification_container)) {
        auto icon_data = (IconButton *) icon_container->user_data;
        load_icon_full_path(app, client, &icon_data->surface, ni->icon_path, 48);
    }
    if (auto icon_container = container_by_name("send_to_action_center", notification_container)) {
        auto icon_data = (IconButton *) icon_container->user_data;
        load_icon_full_path(app, client, &icon_data->surface, as_resource_path("right-arrow.png"), 12);
    }

    client->when_closed = client_closed;

    for (auto c: displaying_notifications) {
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

void close_notification(int id) {
    for (auto c: displaying_notifications) {
        auto data = (NotificationWrapper *) c->root->user_data;
        if (data->ni->id == id) {
            notification_closed_signal(c->app, data->ni,
                                       NotificationReasonClosed::CLOSED_BY_CLOSE_NOTIFICATION_CALL);
            client_close_threaded(c->app, c);
        }
    }
}

static bool root_pierced_handler(Container *container, int mouse_x, int mouse_y) {
    if (auto c = container_by_name("actions_container", container)) {
        for (auto child: c->children) {
            if (bounds_contains(child->real_bounds, mouse_x, mouse_y)) {
                return false;
            }
        }
    }
    return bounds_contains(container->real_bounds, mouse_x, mouse_y);
}

Container *create_notification_container(App *app, NotificationInfo *notification_info, int width) {
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

    int max_text_width = width;
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

    container->real_bounds.w = width;
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

    auto send_to_action_center = icon_content_close_hbox->child(16 * 3, FILL_SPACE);
    send_to_action_center->name = "send_to_action_center";
    auto send_to_action_center_data = new IconButton;
    send_to_action_center->user_data = send_to_action_center_data;
    send_to_action_center->when_clicked = clicked_send_to_action_center;
    send_to_action_center->when_paint = paint_send_to_action_center;
    send_to_action_center->handles_pierced = send_to_action_pierced_handler;

    if (!notification_info->actions.empty()) {
        auto actions_container = container->child(layout_type::vbox, FILL_SPACE,
                                                  container_height - icon_content_close_height);
        actions_container->spacing = 4;
        actions_container->wanted_pad = Bounds(16, 0, 16, 16);
        actions_container->name = "actions_container";

        for (auto action: notification_info->actions) {
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
