//
// Created by jmanc3 on 6/2/21.
//

#include <pango/pangocairo.h>
#include <pango/pango-layout.h>
#include <icons.h>
#include <dpi.h>
#include <fstream>
#include "action_center_menu.h"
#include "application.h"
#include "config.h"
#include "taskbar.h"
#include "main.h"
#include "utility.h"
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
    
    std::string text = "No new notifications";
    draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_action_center_no_new_text), text, container->real_bounds);
}

static void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, config->color_action_center_background, container->real_bounds);
}

static void paint_notification_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, config->color_action_center_notification_content_background, container->real_bounds);
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LabelData *) container->user_data;
    
    const std::string &stripped = strip_html(data->text);
    
    auto f = draw_get_font(client, data->size * config->dpi, config->font, data->weight == PANGO_WEIGHT_NORMAL ? false : true);
    auto s = f->wrapped_text(stripped, container->real_bounds.w);
    
    f->begin();
    f->set_text(stripped);
    f->set_color(EXPAND(config->color_notification_content_text));
    f->draw_text(5, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w);
    f->end();
}

static void paint_label_button(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LabelData *) container->user_data;
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_action_center_history_text), data->text, container->real_bounds, 5, 0, 0);
}

Container *create_notification_container(App *app, NotificationInfo *notification_info, int width);

static void paint_notify(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, config->color_action_center_notification_title_background, container->real_bounds);
    
    auto wrapper = (NotificationWrapper *) container->parent->user_data;
    std::string text = "Received at " + wrapper->ni->time_started;
    
    draw_text(client, 10 * config->dpi, config->font, EXPAND(config->color_action_center_notification_title_text), text, container->real_bounds);
}

static void paint_action(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (NotificationActionWrapper *) container->user_data;
    auto action = data->action;
    
    auto color = config->color_action_center_notification_button_default;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color =  config->color_action_center_notification_button_pressed;
        } else {
            color =  config->color_action_center_notification_button_hovered;
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
    
    std::string text = action.label;
    color = config->color_action_center_notification_button_text_default;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            color = config->color_action_center_notification_button_text_pressed;
        } else {
            color = config->color_action_center_notification_button_text_hovered;
        }
    }
    draw_text(client, 11 * config->dpi, config->font, EXPAND(color), text, container->real_bounds);
}

static void paint_icon(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;
    if (data->surface__) {
        draw_gl_texture(client, data->gsurf, data->surface__,
                                 container->real_bounds.x + 16 * config->dpi,
                                 container->real_bounds.y + 25 * config->dpi);
    }
}

bool is_blacklisted(std::string title, std::string body, std::string subtitle) {
    // Read notifications_blacklist.txt
    std::string notification_blacklist_path(getenv("HOME"));
    notification_blacklist_path += "/.config/winbar/notifications_blacklist.txt";
    
    std::ifstream file(notification_blacklist_path);
    if (!file)
        return false;
    
    enum PARSE_STATE {
        NONE,
        TITLE,
        BODY,
        SUBTITLE
    };
    
    struct Match {
        std::string text;
        bool partial = false;
    };
    
    std::string line;
    int state = PARSE_STATE::NONE;
    std::vector<Match> titles;
    std::vector<Match> bodies;
    std::vector<Match> subtitles;
    bool partial = false;
    std::string active_text;
    while (std::getline(file, line)) { // Read file line by line
        if (!line.empty()) {
            if (line[0] == '#') // Skip comments
                continue;
            if (starts_with(line, ":version ")) // Skip version tag
                continue;
            if (starts_with(line, ":title_start")) {
                active_text = "";
                state = PARSE_STATE::TITLE;
                continue;
            }
            if (starts_with(line, ":body_start")) {
                active_text = "";
                state = PARSE_STATE::BODY;
                continue;
            }
            if (starts_with(line, ":subtitle_start")) {
                active_text = "";
                state = PARSE_STATE::SUBTITLE;
                continue;
            }
            if (starts_with(line, ":partial")) {
                partial = true;
                continue;
            }
            if (starts_with(line, ":title_end") || starts_with(line, ":body_end") ||
                starts_with(line, ":subtitle_end")) {
                if (state == PARSE_STATE::TITLE) {
                    titles.push_back({active_text, partial});
                } else if (state == PARSE_STATE::BODY) {
                    bodies.push_back({active_text, partial});
                } else if (state == PARSE_STATE::SUBTITLE) {
                    subtitles.push_back({active_text, partial});
                }
                partial = false;
                state = PARSE_STATE::NONE;
                continue;
            }
            
            active_text += line;
        }
    }
    file.close(); // Close the file

#define is_inside(target, match) \
    for (auto t: match) { \
        if (t.partial) { \
            if (target.find(t.text) != std::string::npos) { \
                return true; \
            } \
        } else { \
            if (target == t.text) { \
                return true; \
            } \
        } \
    }
    
    is_inside(title, titles);
    is_inside(body, bodies);
    is_inside(subtitle, subtitles);
    
    return false;
}

Container *create_notification_container(App *app, NotificationInfo *notification_info, int width) {
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
    
    if (is_blacklisted(title_text, body_text, subtitle_text))
        return nullptr;
    
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
    
    std::vector<IconTarget> targets;
    targets.emplace_back(IconTarget(notification_info->app_icon));
    targets.emplace_back(IconTarget(subtitle_text));
    search_icons(targets);
    int icon_size = 48 * config->dpi;
    pick_best(targets, icon_size, IconContext::Statuses);
    notification_info->icon_path = targets[0].best_full_path;
    if (notification_info->icon_path.empty()) {
        if (!subtitle_text.empty()) {
            notification_info->icon_path = targets[1].best_full_path;
        }
    }
    
    bool has_icon = !notification_info->icon_path.empty();
    
    int max_text_width = width;
    double c_pad = 16 * config->dpi;
    double a_pad = 16 * config->dpi;
    double a_offset_if_no_icon = 55 * config->dpi;
    double icon_space_horizontally = (a_pad * 2) + icon_size;
    max_text_width -= (c_pad *
                       3); // subtract the part taken up by [c] (16 pad on both sides and icon is 16 px at DPI
    if (has_icon) {
        max_text_width -= icon_space_horizontally; // subtract the part taken up [a] (16 pad on both sides and icon is 48 px at DPI 1
    } else {
        max_text_width -= a_offset_if_no_icon; // subtract the padding used instead of [a] (55 is the pad off the left side if we found no icon)
    }
    
    double top_and_bottom_pad = 20 * config->dpi;
    int container_height = top_and_bottom_pad *
                           2; // variable that keeps track of how tall the notification has to be (starts off with just the top and bottom padding)
    
    // determine_height_of_text returns 0 for the height if string is empty
    int title_height = determine_height_of_text(app, title_text, PangoWeight::PANGO_WEIGHT_BOLD, 11 * config->dpi,
                                                max_text_width);
    container_height += title_height;
    
    int body_height = determine_height_of_text(app, body_text, PangoWeight::PANGO_WEIGHT_NORMAL, 11 * config->dpi,
                                               max_text_width);
    container_height += body_height;
    
    int subtitle_height = determine_height_of_text(app, subtitle_text, PangoWeight::PANGO_WEIGHT_NORMAL,
                                                   9 * config->dpi,
                                                   max_text_width);
    container_height += subtitle_height;
    
    if (!title_text.empty() && !body_text.empty()) // if we have title and body, add some padding between them
        container_height += 1 * config->dpi;
    if (!subtitle_text.empty()) // if we have a subtitle, add some padding for it
        container_height += 3 * config->dpi;
    
    int vertical_space_icon_requires = top_and_bottom_pad * 2 + icon_size;
    if (has_icon && vertical_space_icon_requires > container_height) {
        container_height = vertical_space_icon_requires;
    }
    
    container_height += 40 * config->dpi; // Reserving the space for: "Received at #"
    
    int icon_content_close_height = container_height;
    
    if (!notification_info->actions.empty()) {
        container_height += 16 * config->dpi; // for the bottom pad of any actions
        
        int action_height = 32 * config->dpi;
        int action_pad = 4 * config->dpi;
        container_height += notification_info->actions.size() * action_height;
        container_height += (notification_info->actions.size() - 1) * action_pad; // pad between each action option
    }
    
    container->real_bounds.w = width;
    container->real_bounds.h = container_height;
    
    // "Received at #"
    auto notify_user_container = container->child(FILL_SPACE, 40 * config->dpi);
    notify_user_container->when_paint = paint_notify;
    
    auto icon_content_close_hbox = container->child(layout_type::hbox, FILL_SPACE, FILL_SPACE);
    
    if (has_icon) {
        auto icon = icon_content_close_hbox->child(icon_space_horizontally, FILL_SPACE);
        icon->name = "icon";
        auto icon_data = new IconButton;
        icon->user_data = icon_data;
        icon->when_paint = paint_icon;
    } else {
        icon_content_close_hbox->child(a_offset_if_no_icon, FILL_SPACE);
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
        content->child(FILL_SPACE, 1 * config->dpi);
        auto body_label = content->child(FILL_SPACE, body_height);
        body_label->when_paint = paint_label;
        auto body_label_data = new LabelData;
        body_label->user_data = body_label_data;
        body_label_data->size = 11;
        body_label_data->weight = PangoWeight::PANGO_WEIGHT_NORMAL;
        body_label_data->text = body_text;
    }
    if (!subtitle_text.empty()) {
        content->child(FILL_SPACE, 3 * config->dpi);
        auto subtitle_label = content->child(FILL_SPACE, subtitle_height);
        subtitle_label->when_paint = paint_label;
        auto subtitle_label_data = new LabelData;
        subtitle_label->user_data = subtitle_label_data;
        subtitle_label_data->size = 9;
        subtitle_label_data->weight = PangoWeight::PANGO_WEIGHT_NORMAL;
        subtitle_label_data->text = subtitle_text;
    }
    content->child(FILL_SPACE, FILL_SPACE);
    
    auto send_to_action_center = icon_content_close_hbox->child(c_pad * 3, FILL_SPACE);
    
    if (!notification_info->actions.empty()) {
        auto actions_container = container->child(layout_type::vbox, FILL_SPACE,
                                                  container_height - icon_content_close_height);
        actions_container->spacing = 4 * config->dpi;
        actions_container->wanted_pad = Bounds(16 * config->dpi, 0, 16 * config->dpi, 16 * config->dpi);
        actions_container->name = "actions_container";
        actions_container->interactable = false;
        
        for (auto action: notification_info->actions) {
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
    root->wanted_pad = Bounds(16 * config->dpi, 0, 16 * config->dpi, 0);
    root->type = layout_type::vbox;
    root->spacing = 0;
    root->interactable = false;
    
    Container *top_hbox = root->child(::hbox, FILL_SPACE, 44 * config->dpi);
    {
        std::string text = "Notification history";
        
        PangoLayout *layout = get_cached_pango_font(
                client->cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
        
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
    ScrollPaneSettings settings(config->dpi);
    settings.right_show_amount = 2;
    auto scroll_pane = make_newscrollpane_as_child(r, settings);
    scroll_pane->when_paint = paint_prompt;
//    scroll_pane->clip = true;
    scroll_pane->name = "scroll_pane";
    auto content = scroll_pane->content;
    content->name = "content";
    content->spacing = 8 * config->dpi;
    content->wanted_pad.h = 16 * config->dpi;
    
    for (int i = notifications.size(); i--;) {
        NotificationInfo *n = notifications[i];
        if (n->removed_from_action_center)
            continue;
        auto notification_container = create_notification_container(app, n, 364 * config->dpi);
        if (!notification_container)
            continue;
        notification_container->parent = content;
        notification_container->wanted_bounds.h = notification_container->real_bounds.h;
        content->children.push_back(notification_container);
        
        if (auto icon_container = container_by_name("icon", notification_container)) {
            auto icon_data = (IconButton *) icon_container->user_data;
            load_icon_full_path(app, client, &icon_data->surface__, n->icon_path, 48 * config->dpi);
        }
    }
}

void start_action_center(App *app) {
    for (auto n: notifications) {
        n->sent_to_action_center = false;
    }
    for (auto c: displaying_notifications) {
        auto wrapper = (NotificationWrapper *) c->root->user_data;
        notification_closed_signal(c->app, wrapper->ni, NotificationReasonClosed::UNDEFINED_OR_RESERVED_REASON);
        client_close_threaded(app, c);
    }
    
    Settings settings;
    settings.w = 396 * config->dpi;
    settings.h = app->bounds.h - config->taskbar_height;
    settings.x = app->bounds.x + app->bounds.w - settings.w;
    settings.y = 0;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 2;
    settings.slide_data[2] = 120;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        // TODO screen_info can be null if window is not half in any screen, we should preserve most recent screen
        auto *container = container_by_name("action", taskbar->root);
        if (container->real_bounds.x > taskbar->bounds->w / 2) {
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        } else {
            settings.x = 0;
            settings.slide_data[1] = 0;
        }
        ScreenInformation *primary_screen = nullptr;
        for (auto s: screens)
            if (s->is_primary) primary_screen = s;
        if (primary_screen != nullptr) {
            settings.y = primary_screen->y;
            settings.h = primary_screen->height_in_pixels - config->taskbar_height;
        }
    }
    settings.override_redirect = true;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;

    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "action_center";
        auto client = taskbar->create_popup(popup_settings, settings);
        
        fill_root(client, client->root);
        client_show(app, client);
        
        if (auto c = client_by_name(app, "taskbar")) {
            if (auto co = container_by_name("action", c->root)) {
                auto data = (ActionCenterButtonData *) co->user_data;
                data->some_unseen = false;
                client_create_animation(app, c, &data->slide_anim, data->lifetime, 0, 220, nullptr, 0);
                request_refresh(app, c);
            }
        }
    }
}
