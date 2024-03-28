
#include "windows_selector.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "simple_dbus.h"

#include <pango/pangocairo.h>
#include <xcb/xcb_image.h>
#include <icons.h>
#include <cmath>

int option_width = 217 * 1.2;
int option_min_width = 100 * 1.2;
int option_height = 144 * 1.2;
int option_pad = 8;
static double close_width = 32;
static double close_height = 32;
static Timeout *drag_and_drop_timeout = nullptr;
bool drag_and_dropping = false;

static void
fill_root(Container *root);

static void when_closed(AppClient *client);

static void on_open_timeout(App *app, AppClient *client, Timeout *, void *user_data) {
    auto container = (Container *) user_data;
    if (auto c = client_by_name(app, "taskbar")) {
        if (container_by_container(container, c->root)) {
            auto data = (LaunchableButton *) container->user_data;
            data->possibly_open_timeout = nullptr;
            start_windows_selector(container, selector_type::OPEN_HOVERED);
        }
    }
}

static void on_close_timeout(App *app, AppClient *client, Timeout *, void *user_data) {
    if (auto c = client_by_name(app, "windows_selector")) {
        auto pii = (PinnedIconInfo *) c->root->user_data;
        pii->data->possibly_stop_timeout = nullptr;
        pii->data->type = ::CLOSED;
        client_close_threaded(app, c);
    }
}

void possibly_open(App *app, Container *container, LaunchableButton *data) {
    if (data->windows_data_list.empty()) {
        return;
    }
    if (client_by_name(app, "right_click_menu")) {
        return;
    }
    selector_type we_are = data->type;
    
    auto they = client_by_name(app, "windows_selector");
    bool they_are_us = false;
    selector_type they_are = selector_type::CLOSED;
    if (they) {
        auto pii = (PinnedIconInfo *) they->root->user_data;
        if (pii->data_container == container) {
            they_are_us = true;
        }
        they_are = pii->data->type;
    }
    
    // cancel any possibly_stop
    if (data->possibly_stop_timeout != nullptr) {
        app_timeout_stop(app, nullptr, data->possibly_stop_timeout);
        data->possibly_stop_timeout = nullptr;
    }
    
    if (they && they_are_us) { // we are already open, we don't need to reopen
        return;
    }
    
    if (we_are == selector_type::CLOSED) {
        if (they) {
            if (they_are != selector_type::OPEN_CLICKED) {
                // instantly close them
                client_close(app, they);
                // instantly open us
                on_open_timeout(app, nullptr, nullptr, container);
            }
        } else {
            // start timeout
            if (data->possibly_open_timeout == nullptr) {
                data->possibly_open_timeout = app_timeout_create(app, nullptr, 130, on_open_timeout, container,
                                                                 const_cast<char *>(__PRETTY_FUNCTION__));
            }
        }
    }
}

void possibly_close(App *app, Container *container, LaunchableButton *data) {
    if (data->windows_data_list.empty()) {
        return;
    }
    if (client_by_name(app, "right_click_menu")) {
        return;
    }
    selector_type we_are = data->type;
    
    auto they = client_by_name(app, "windows_selector");
    bool they_are_us = false;
    selector_type they_are = selector_type::CLOSED;
    if (they) {
        auto pii = (PinnedIconInfo *) they->root->user_data;
        if (pii->data_container == container) {
            they_are_us = true;
        }
        they_are = pii->data->type;
    }
    
    // cancel any possibly_open
    if (data->possibly_open_timeout != nullptr) {
        app_timeout_stop(app, nullptr, data->possibly_open_timeout);
        data->possibly_open_timeout = nullptr;
    }
    
    if (we_are == selector_type::CLOSED) { // if we are already closed, we don't need to re-close
        return;
    }
    
    if (we_are == selector_type::OPEN_HOVERED) {
        if (data->possibly_stop_timeout == nullptr) {
            data->possibly_stop_timeout = app_timeout_create(app, nullptr, 220, on_close_timeout, container,
                                                             const_cast<char *>(__PRETTY_FUNCTION__));
        }
    }
}

static void
window_option_clicked(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }
    
    auto pii = (PinnedIconInfo *) client_entity->root->user_data;
    
    xcb_window_t to_focus = pii->data->windows_data_list[index]->id;
    
    xcb_ewmh_request_change_active_window(&app->ewmh,
                                          app->screen_number,
                                          to_focus,
                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                          XCB_CURRENT_TIME,
                                          XCB_NONE);
    
    client_close_threaded(app, client_entity);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
clicked_body(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent);
}

static void
clicked_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent->parent);
}

static void option_hover_clicked(App *, AppClient *client, Timeout *, void *user_data) {
    clicked_body(client, client->cr, (Container *) user_data);
    drag_and_drop_timeout = nullptr;
}

static void
option_entered(AppClient *client, cairo_t *, Container *container) {
    if (drag_and_dropping) {
        drag_and_drop_timeout = app_timeout_create(app, client, 600, option_hover_clicked, container, const_cast<char *>(__PRETTY_FUNCTION__));
    }
}

static void
option_exited(AppClient *client, cairo_t *, Container *) {
    if (drag_and_drop_timeout != nullptr) {
        app_timeout_stop(app, client, drag_and_drop_timeout);
    }
}

static int get_width(LaunchableButton *data) {
    double total_width = 0;
    
    for (auto w: data->windows_data_list) {
        if (w->marked_to_close)
            continue;
        
        double pad = option_pad;
        double target_width = option_width - pad * 2;
        double target_height = option_height - pad;
        double scale_w = target_width / w->width;
        double scale_h = target_height / w->height;
        if (scale_w < scale_h) {
            scale_h = scale_w;
        } else {
            scale_w = scale_h;
        }
        double width = w->width * scale_w;
        if (width < option_min_width) {
            width = option_min_width;
        }
        
        total_width += width;
    }
    
    return total_width;
}

static void
window_option_closed(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }
    
    auto pii = (PinnedIconInfo *) client_entity->root->user_data;
    
    xcb_window_t to_close = pii->data->windows_data_list[index]->id;
    pii->data->windows_data_list[index]->marked_to_close = true;
    
    unsigned long windows_count = pii->data->windows_data_list.size();
    xcb_ewmh_request_close_window(&app->ewmh,
                                  app->screen_number,
                                  to_close,
                                  XCB_CURRENT_TIME,
                                  XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);
}

static void
clicked_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_closed(client_entity, cr, container->parent->parent);
}

static void paint_option_hovered_or_pressed(Container *container, bool &hovered, bool &pressed) {
    Container *titlebar = container->children[0]->children[0];
    Container *close = container->children[0]->children[1];
    Container *body = container->children[1];
    if (titlebar->state.mouse_hovering || body->state.mouse_hovering ||
        close->state.mouse_hovering) {
        hovered = true;
    }
    if (titlebar->state.mouse_pressing || body->state.mouse_pressing ||
        body->state.mouse_pressing) {
        pressed = true;
    }
}

static void
paint_option_background(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }
    auto pii = (PinnedIconInfo *) client_entity->root->user_data;
    
    bool hovered = false;
    bool pressed = false;
    paint_option_hovered_or_pressed(container, hovered, pressed);
    
    ArgbColor background = config->color_windows_selector_default_background;
    if (hovered || pressed) {
        if (pressed) {
            background = correct_opaqueness(client_entity, config->color_windows_selector_pressed_background);
        } else {
            background = correct_opaqueness(client_entity, config->color_windows_selector_hovered_background);
        }
    } else {
        background = correct_opaqueness(client_entity, config->color_windows_selector_default_background);
    }
    
    if (pii->data->wants_attention_amount != 0 && pii->data->windows_data_list[index]->wants_attention) {
        double blinks = 10.5;
        double scalar = fmod(pii->data->wants_attention_amount, (1.0 / blinks)); // get N blinks
        scalar *= blinks;
        if (scalar > .5)
            scalar = 1 - scalar;
        scalar *= 2;
        if (pii->data->wants_attention_amount == 1)
            scalar = 1;
        background = lerp_argb(scalar, background, config->color_windows_selector_attention_background);
    }
    
    set_argb(cr, background);
    cairo_rectangle(cr,
                    container->real_bounds.x - 1,
                    container->real_bounds.y,
                    container->real_bounds.w + 1,
                    container->real_bounds.h);
    cairo_fill(cr);
}

static void
paint_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        ArgbColor color;
        if (container->state.mouse_pressing) {
            color = config->color_windows_selector_close_icon_pressed_background;
        } else {
            color = config->color_windows_selector_close_icon_hovered_background;
        }
        set_argb(cr, color);
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    bool active = container->state.mouse_pressing || container->state.mouse_hovering ||// US
                  container->parent->children[0]->state.mouse_pressing ||
                  container->parent->children[0]->state.mouse_hovering ||// TITLE
                  container->parent->parent->children[1]->state.mouse_pressing ||
                  container->parent->parent->children[1]->state.mouse_hovering;// BODY
    
    
    if (active) {
        PangoLayout *layout =
                get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                set_argb(cr, config->color_windows_selector_close_icon_pressed);
            } else {
                set_argb(cr, config->color_windows_selector_close_icon_hovered);
            }
        } else {
            set_argb(cr, config->color_windows_selector_close_icon);
        }
        
        int width;
        int height;
        pango_layout_set_text(layout, "\uE10A", strlen("\uE83F"));
        
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        cairo_save(cr);
        cairo_move_to(cr,
                      container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                      container->real_bounds.y + container->real_bounds.h / 2 - width / 2);
        pango_cairo_show_layout(cr, layout);
        cairo_restore(cr);
    }
}

class BodyData : public UserData {
public:
    WindowsData *windows_data = nullptr;
};

static void
paint_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }
    
    auto pii = (PinnedIconInfo *) client_entity->root->user_data;
    if (pii->icon_surface != nullptr) {
        cairo_set_source_surface(cr, pii->icon_surface,
                                 container->real_bounds.x + (8 * config->dpi),
                                 container->real_bounds.y + (8 * config->dpi));
        cairo_paint(cr);
    }
    
    auto windows_data = ((BodyData *) container->parent->parent->children[1]->user_data)->windows_data;
    std::string title = windows_data->title;
    if (title.empty()) {
        auto pii = (PinnedIconInfo *) client_entity->root->user_data;
        title = pii->data->class_name;
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    int close_w = close_width;
    bool hovered = false;
    bool pressed = false;
    paint_option_hovered_or_pressed(container->parent->parent, hovered, pressed);
    if (hovered || pressed) {
        close_w = 0;
    }
    int pad = option_pad;
    if (close_w == 0) {
        pad = 5;
    }
    pango_layout_set_width(layout,
                           (((container->real_bounds.w - (pad * 2)) + close_w) - 24 * config->dpi) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
    if (close_w == 0) {
        pad = option_pad;
    }
    
    set_argb(cr, config->color_windows_selector_text);
    cairo_move_to(cr,
                  container->real_bounds.x + ((pad + 24 * config->dpi)),
                  container->real_bounds.y + container->real_bounds.h / 2 - (height / 2));
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_width(layout, -1); // because other people using this cached layout don't expect wrapping
}

static void
paint_body(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = ((BodyData *) container->user_data)->windows_data;
    
    double pad = option_pad;
    double target_width = container->real_bounds.w - pad * 2;
    double target_height = container->real_bounds.h - pad;
    double scale_w = (target_width) / data->width;
    double scale_h = (target_height) / data->height;
    if (scale_w < scale_h) {
        scale_h = scale_w;
    } else {
        scale_w = scale_h;
    }
    
    long currrent_time = get_current_time_in_ms();
    if ((currrent_time - data->last_rescale_timestamp) > 1000) {
        if (screen_has_transparency(app)) {
            data->take_screenshot();
        }
        data->rescale(scale_w, scale_h);
    }
    if (data->scaled_thumbnail_surface) {
        double width = data->width * scale_w;
        double height = data->height * scale_h;
        
        double dest_x = container->real_bounds.x + pad + target_width / 2 - width / 2;
        double dest_y = container->real_bounds.y + target_height / 2 - height / 2;
        
        cairo_save(cr);
        cairo_set_source_surface(cr, data->scaled_thumbnail_surface,
                                 dest_x, dest_y);
        cairo_rectangle(cr, dest_x, dest_y, width, height);
        cairo_clip(cr);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

void when_enter(AppClient *client, cairo_t *cr, Container *self) {
    auto pii = (PinnedIconInfo *) client->root->user_data;
    if (pii->data->type == selector_type::OPEN_HOVERED) {
        possibly_open(app, pii->data_container, pii->data);
    }
}

void when_leave(AppClient *client, cairo_t *cr, Container *self) {
    auto pii = (PinnedIconInfo *) client->root->user_data;
    if (pii->data->type == selector_type::OPEN_HOVERED) {
        possibly_close(app, pii->data_container, pii->data);
    }
}

static void paint_root(AppClient *client, cairo_t *, Container *root) {
    auto pii = (PinnedIconInfo *) root->user_data;
    
    static xcb_window_t currently_previewing = -1;
    xcb_window_t currently_hovered = -1;
    for (int i = 0; i < root->children.size(); i++) {
        auto c = root->children[i];
        bool hovered = false;
        bool pressed = false;
        paint_option_hovered_or_pressed(c, hovered, pressed);
        if (hovered)
            currently_hovered = pii->data->windows_data_list[i]->id;
    }
    if (currently_hovered == -1) {
        if (currently_previewing != -1) {
            currently_previewing = -1;
            highlight_window("");
        }
    } else {
        if (currently_hovered != currently_previewing) {
            currently_previewing = currently_hovered;
            std::vector<std::string> windows;
            windows.push_back(std::to_string(currently_hovered));
            windows.push_back(std::to_string(client->window));
            highlight_windows(windows);
        }
    }
}

static void
fill_root(AppClient *client, Container *root) {
    auto pii = (PinnedIconInfo *) root->user_data;
    root->receive_events_even_if_obstructed = true;
    root->when_mouse_enters_container = when_enter;
    root->when_mouse_leaves_container = when_leave;
    root->when_paint = paint_root;
    
    if (screen_has_transparency(app)) {
        for (auto window_data: pii->data->windows_data_list) {
            window_data->last_rescale_timestamp = -1;
        }
    }
    
    if (pii->data == nullptr)
        return;
    
    for (int i = 0; i < pii->data->windows_data_list.size(); i++) {
        WindowsData *w = pii->data->windows_data_list[i];
        double pad = option_pad;
        double target_width = option_width - pad * 2;
        double target_height = option_height - pad;
        double scale_w = target_width / w->width;
        double scale_h = target_height / w->height;
        if (scale_w < scale_h) {
            scale_h = scale_w;
        } else {
            scale_w = scale_h;
        }
        double width = w->width * scale_w;
        if (width < option_min_width) {
            width = option_min_width;
        }
        
        Container *option_container = new Container();
        option_container->type = vbox;
        option_container->parent = root;
        option_container->wanted_bounds.w = width;
        option_container->wanted_bounds.h = FILL_SPACE;
        option_container->when_paint = paint_option_background;
        option_container->name = std::to_string(w->id);
        root->children.push_back(option_container);
        
        Container *option_top_hbox = new Container();
        option_top_hbox->parent = option_container;
        option_top_hbox->wanted_bounds.w = FILL_SPACE;
        option_top_hbox->wanted_bounds.h = close_height;
        option_top_hbox->type = hbox;
        option_container->children.push_back(option_top_hbox);
        
        Container *option_titlebar = new Container();
        option_titlebar->parent = option_top_hbox;
        option_titlebar->wanted_bounds.w = FILL_SPACE;
        option_titlebar->wanted_bounds.h = FILL_SPACE;
        option_titlebar->when_paint = paint_titlebar;
        option_titlebar->when_clicked = clicked_titlebar;
        option_titlebar->when_mouse_enters_container = option_entered;
        option_titlebar->when_mouse_leaves_container = option_exited;
        option_top_hbox->children.push_back(option_titlebar);
        
        Container *option_close_button = new Container();
        option_close_button->parent = option_top_hbox;
        option_close_button->wanted_bounds.w = close_width;
        option_close_button->wanted_bounds.h = FILL_SPACE;
        option_close_button->when_paint = paint_close;
        option_close_button->when_clicked = clicked_close;
        option_top_hbox->children.push_back(option_close_button);
        
        // Body
        Container *option_body = new Container();
        option_body->parent = option_container;
        option_body->wanted_bounds.w = FILL_SPACE;
        option_body->wanted_bounds.h = FILL_SPACE;
        option_body->when_paint = paint_body;
        option_body->when_clicked = clicked_body;
        option_body->when_mouse_enters_container = option_entered;
        option_body->when_mouse_leaves_container = option_exited;
        auto body_data = new BodyData;
        body_data->windows_data = w;
        option_body->user_data = body_data;
        option_container->children.push_back(option_body);
    }
}

static void when_closed(AppClient *client) {
    auto pii = (PinnedIconInfo *) client->root->user_data;
    pii->data->type = selector_type::CLOSED;
    app_timeout_stop(client->app, client, pii->data->possibly_stop_timeout);
    app_timeout_stop(client->app, client, pii->data->possibly_open_timeout);
    pii->data->possibly_stop_timeout = nullptr;
    pii->data->possibly_open_timeout = nullptr;
    if (auto c = client_by_name(app, "taskbar")) {
        request_refresh(app, c);
    }
    drag_and_dropping = false;
    highlight_window("");
}

void start_windows_selector(Container *container, selector_type selector_state) {
    option_width = 217 * 1.2 * config->dpi;
    option_min_width = 100 * 1.2 * config->dpi;
    option_height = 144 * 1.2 * config->dpi;
    option_pad = 8 * config->dpi;
    close_width = 32 * config->dpi;
    close_height = 32 * config->dpi;
    
    if (auto c = client_by_name(app, "windows_selector")) {
        client_close(app, c);
        xcb_flush(app->connection);
    }
    auto data = (LaunchableButton *) container->user_data;
    
    auto pii = new PinnedIconInfo;
    pii->data_container = container;
    pii->data = data;
    pii->data->type = selector_state;
    pii->data->possibly_open_timeout = nullptr;
    pii->data->possibly_stop_timeout = nullptr;
    
    int width = get_width(pii->data);
    Settings settings;
    settings.w = width;
    settings.h = option_height;
    settings.x = container->real_bounds.x - settings.w / 2 + pii->data_container->real_bounds.w / 2;
    if (settings.x < 0)
        settings.x = 0;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x +
                     (container->real_bounds.x - settings.w / 2 + pii->data_container->real_bounds.w / 2);
        if (settings.x < taskbar->bounds->x)
            settings.x = taskbar->bounds->x;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.override_redirect = true;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "windows_selector";
        popup_settings.takes_input_focus = false;
        auto client = taskbar->create_popup(popup_settings, settings);
    
    
        uint32_t version = 5;
        xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, client->window, get_cached_atom(app, "XdndAware"),
                            XCB_ATOM_ATOM, 32, 1, &version);
    
        client->root->user_data = pii;
        if (pii->data->surface) {
            std::string path;
            std::vector<IconTarget> targets;
            targets.emplace_back(IconTarget(pii->data->icon_name));
            targets.emplace_back(IconTarget(c3ic_fix_wm_class(pii->data->class_name)));
            search_icons(targets);
            pick_best(targets, 16 * config->dpi);
            path = targets[0].best_full_path;
            if (path.empty()) {
                path = targets[1].best_full_path;
            }
            if (path.empty()) {
                pii->icon_surface = accelerated_surface(app, client, 16 * config->dpi, 16 * config->dpi);
                cairo_t *cr = cairo_create(pii->icon_surface);
                
                double starting_w = cairo_image_surface_get_width(pii->data->surface);
                double target_w = 16 * config->dpi;
                double sx = target_w / starting_w;
                
                cairo_scale(cr, sx, sx);
                cairo_set_source_surface(cr, pii->data->surface, 0, 0);
                cairo_paint(cr);
                
                cairo_destroy(cr);
            } else {
                load_icon_full_path(app, client, &pii->icon_surface, path, 16 * config->dpi);
            }
        }
        
        
        client->fps = 30;
        client->when_closed = when_closed;
        client_register_animation(app, client);
        fill_root(client, client->root);
        
        client_show(app, client);
        
        if (auto c = client_by_name(app, "taskbar")) {
            request_refresh(app, c);
        }
    }
}

PinnedIconInfo::~PinnedIconInfo() {
    cairo_surface_destroy(icon_surface);
}
