//
// Created by jmanc3 on 6/2/21.
//

#include <pango/pangocairo.h>
#include <icons.h>
#include "action_center_menu.h"
#include "application.h"
#include "config.h"
#include "taskbar.h"
#include "main.h"
#include "notifications.h"
#include "components.h"
#include "simple_dbus.h"

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

static void paint_prompt(AppClient *client, cairo_t *cr, Container *container) {
    if (auto c = container_by_name("content", client->root)) {
        if (!c->children.empty()) {
            return;
        }
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_BOLD);
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_LEFT);

    std::string text = "No new notifications";

    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, config->color_action_center_no_new_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;

    set_argb(cr, config->color_action_center_background);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void paint_notification_root(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationWrapper *) container->user_data;

    set_argb(cr, config->color_action_center_notification_content_background);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
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

    set_argb(cr, config->color_action_center_notification_content_text);
    cairo_move_to(cr,
                  container->real_bounds.x,
                  container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_width(layout, -1);
}

static void paint_label_button(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LabelData *) container->user_data;

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, data->text.c_str(), data->text.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_action_center_history_text);
    cairo_move_to(cr,
                  container->real_bounds.x,
                  container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);
}

Container *create_notification_container(App *app, NotificationInfo *notification_info, int width);

static void paint_notify(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_action_center_notification_title_background);
    cairo_fill(cr);

    auto wrapper = (NotificationWrapper *) container->parent->user_data;
    std::string text = "Received at " + wrapper->ni->time_started;

    PangoLayout *layout = get_cached_pango_font(
            client->cr, config->font, 10, PANGO_WEIGHT_BOLD);

    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout, text.c_str(), text.length());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_action_center_notification_title_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 -
                  ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void paint_action(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationActionWrapper *) container->user_data;
    auto action = data->action;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_action_center_notification_button_pressed);
        } else {
            set_argb(cr, config->color_action_center_notification_button_hovered);
        }
    } else {
        set_argb(cr, config->color_action_center_notification_button_default);
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
            set_argb(cr, config->color_action_center_notification_button_text_pressed);
        } else {
            set_argb(cr, config->color_action_center_notification_button_text_hovered);
        }
    } else {
        set_argb(cr, config->color_action_center_notification_button_text_default);
    }
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

Container *create_notification_container(App *app, NotificationInfo *notification_info, int width) {
    auto container = new Container(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    auto data = new NotificationWrapper;
    container->name = "container_with_ni";
    data->ni = notification_info;
    container->user_data = data;
    container->when_paint = paint_notification_root;

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

    container_height += 40; // received at

    int icon_content_close_height = container_height;

    if (!notification_info->actions.empty()) {
        container_height += 16; // for the bottom pad

        container_height += notification_info->actions.size() * 32;
        container_height += notification_info->actions.size() - 1 * 4; // pad between each option
    }

    container->real_bounds.w = width;
    container->real_bounds.h = container_height;

    auto notify_user_container = container->child(FILL_SPACE, 40);
    notify_user_container->when_paint = paint_notify;

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

    if (!notification_info->actions.empty()) {
        auto actions_container = container->child(layout_type::vbox, FILL_SPACE,
                                                  container_height - icon_content_close_height);
        actions_container->spacing = 4;
        actions_container->wanted_pad = Bounds(16, 0, 16, 16);
        actions_container->name = "actions_container";
        actions_container->interactable = false;

        for (auto action : notification_info->actions) {
            auto action_container = actions_container->child(FILL_SPACE, FILL_SPACE);
            auto data = new NotificationActionWrapper;
            data->action = action;
            action_container->user_data = data;
            action_container->when_paint = paint_action;
        }
    }

    return container;
}

static void fill_root(AppClient *client, Container *root) {
    root->when_paint = paint_root;
    root->wanted_pad = Bounds(16, 0, 16, 0);
    root->type = layout_type::vbox;
    root->spacing = 0;
    root->interactable = false;

    Container *top_hbox = root->child(::hbox, FILL_SPACE, 44);
    {
        std::string text = "Notification history";

        PangoLayout *layout = get_cached_pango_font(
                client->cr, config->font, 9, PANGO_WEIGHT_NORMAL);

        pango_layout_set_text(layout, text.c_str(), text.length());

        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);

        top_hbox->wanted_pad.y = (top_hbox->wanted_bounds.h - logical.height / PANGO_SCALE) / 2;
        top_hbox->wanted_pad.h = (top_hbox->wanted_bounds.h - logical.height / PANGO_SCALE) / 2;

        top_hbox->child(FILL_SPACE, FILL_SPACE);

        auto label = top_hbox->child(logical.width / PANGO_SCALE, logical.height / PANGO_SCALE);
        auto label_data = new LabelData;
        label_data->text = text;
        label->user_data = label_data;
        label->when_paint = paint_label_button;
    }

    auto r = root->child(FILL_SPACE, FILL_SPACE);
    ScrollPaneSettings settings;
    settings.right_show_amount = 2;
    auto scroll_pane = make_scrollpane(r, settings);
    scroll_pane->when_paint = paint_prompt;
    scroll_pane->clip = true;
    scroll_pane->name = "scroll_pane";
    auto content = scroll_pane->child(::vbox, FILL_SPACE, 0);
    content->name = "content";
    content->spacing = 8;

    for (int i = notifications.size(); i--;) {
        NotificationInfo *n = notifications[i];
        if (n->removed_from_action_center)
            continue;
        auto notification_container = create_notification_container(app, n, 364);
        notification_container->parent = content;
        notification_container->wanted_bounds.h = notification_container->real_bounds.h;
        content->children.push_back(notification_container);

        if (auto icon_container = container_by_name("icon", notification_container)) {
            auto icon_data = (IconButton *) icon_container->user_data;
            load_icon_full_path(app, client, &icon_data->surface, n->icon_path, 48);
        }
        if (auto icon_container = container_by_name("send_to_action_center", notification_container)) {
            auto icon_data = (IconButton *) icon_container->user_data;
            load_icon_full_path(app, client, &icon_data->surface, as_resource_path("close-12.png"), 12);
        }
    }

    content->wanted_bounds.h = true_height(content) + true_height(content->parent);
}

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);

            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("action", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (ActionCenterButtonData *) co->user_data;
                            data->invalid_button_down = true;
                            data->timestamp = get_current_time_in_ms();
                        }
                    }
                }
            }
            break;
        }
    }
}

static bool
event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            xcb_set_input_focus(app->connection, XCB_NONE, e->window, XCB_CURRENT_TIME);
            xcb_flush(app->connection);
            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
        }
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
            break;
        }
    }

    return false;
}

void start_action_center(App *app) {
    for (auto n : notifications) {
        n->sent_to_action_center = false;
    }
    for (auto c : displaying_notifications) {
        auto wrapper = (NotificationWrapper *) c->root->user_data;
        notification_closed_signal(c->app, wrapper->ni, NotificationReasonClosed::UNDEFINED_OR_RESERVED_REASON);
        client_close_threaded(app, c);
    }

    Settings settings;
    settings.w = 396;
    settings.h = app->bounds.h - config->taskbar_height;
    settings.x = app->bounds.x + app->bounds.w - settings.w;
    settings.y = 0;
    settings.popup = true;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;

    auto client = client_new(app, settings, "action_center");
    fill_root(client, client->root);
    client->grab_event_handler = grab_event_handler;
    app_create_custom_event_handler(app, client->window, event_handler);

    client_show(app, client);

    if (auto c = client_by_name(app, "taskbar")) {
        if (auto co = container_by_name("action", c->root)) {
            auto data = (ActionCenterButtonData *) co->user_data;
            data->some_unseen = false;
            client_create_animation(app, c, &data->slide_anim, 220, nullptr, 0);
            request_refresh(app, c);
        }
    }
}