
#include "windows_selector.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"

#include <pango/pangocairo.h>
#include <xcb/xcb_image.h>

int option_width = 217 * 1.2;
int option_min_width = 100 * 1.2;
int option_height = 144 * 1.2;
int option_pad = 8;
static double close_width = 32;
static double close_height = 32;

static void
fill_root(Container *root);

static void when_closed(AppClient *client);

static void on_open_timeout(App *app, AppClient *client, void *user_data) {
    auto container = (Container *) user_data;
    if (auto c = client_by_name(app, "taskbar")) {
        if (container_by_container(container, c->root)) {
            auto data = (LaunchableButton *) container->user_data;
            data->possibly_open_timeout_fd = -1;
            start_windows_selector(container, selector_type::OPEN_HOVERED);
        }
    }
}

static void on_close_timeout(App *app, AppClient *client, void *user_data) {
    if (auto c = client_by_name(app, "windows_selector")) {
        auto pii = (PinnedIconInfo *) c->root->user_data;
        pii->data->possibly_stop_timeout_fd = -1;
        pii->data->type = ::CLOSED;
        client_close_threaded(app, c);
    }
}

void possibly_open(App *app, Container *container, LaunchableButton *data) {
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
    if (data->possibly_stop_timeout_fd != -1) {
        app_timeout_stop(app, nullptr, data->possibly_stop_timeout_fd);
        data->possibly_stop_timeout_fd = -1;
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
                on_open_timeout(app, nullptr, container);
            }
        } else {
            // start timeout
            if (data->possibly_open_timeout_fd == -1) {
                data->possibly_open_timeout_fd = app_timeout_create(app, nullptr, 300, on_open_timeout, container);
            }
        }
    }
}

void possibly_close(App *app, Container *container, LaunchableButton *data) {
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
    if (data->possibly_open_timeout_fd != -1) {
        app_timeout_stop(app, nullptr, data->possibly_open_timeout_fd);
        data->possibly_open_timeout_fd = -1;
    }

    if (we_are == selector_type::CLOSED) { // if we are already closed, we don't need to re-close
        return;
    }

    if (we_are == selector_type::OPEN_HOVERED) {
        if (data->possibly_stop_timeout_fd == -1) {
            data->possibly_stop_timeout_fd = app_timeout_create(app, nullptr, 300, on_close_timeout, container);
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

static int get_width(LaunchableButton *data) {
    double total_width = 0;

    for (auto w : data->windows_data_list) {
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


    if (windows_count > 1) {
        container->parent->children.erase(container->parent->children.begin() + index);

        int width = get_width(pii->data);
        delete container;

        double x = pii->data_container->real_bounds.x - width / 2 + pii->data_container->real_bounds.w / 2;
        if (x < 0) {
            x = 0;
        }
        double y = app->bounds.h - option_height - config->taskbar_height;
        double h = option_height;

        handle_configure_notify(app, client_entity, x, y, width, h);
        client_set_position_and_size(app, client_entity, x, y, width, h);
    } else {
        client_close_threaded(app, client_entity);
        app->grab_window = -1;
    }
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
    bool hovered = false;
    bool pressed = false;
    paint_option_hovered_or_pressed(container, hovered, pressed);

    if (hovered || pressed) {
        if (pressed) {
            set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_pressed_background));
        } else {
            set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_hovered_background));
        }
    } else {
        set_argb(cr, correct_opaqueness(client_entity, config->color_windows_selector_default_background));
    }
    cairo_rectangle(cr,
                    container->real_bounds.x - 1,
                    container->real_bounds.y,
                    container->real_bounds.w + 1,
                    container->real_bounds.h);
    cairo_fill(cr);
}

static void
paint_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto *data = static_cast<PinnedIconInfo *>(client_entity->root->user_data);

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

    if (data->surface && active) {
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                dye_surface(data->surface, config->color_windows_selector_close_icon_pressed);
            } else {
                dye_surface(data->surface, config->color_windows_selector_close_icon_hovered);
            }
        } else {
            dye_surface(data->surface, config->color_windows_selector_close_icon);
        }
        double offset = (double) (32 - 16) / 2;
        cairo_set_source_surface(
                cr, data->surface, container->real_bounds.x + offset, container->real_bounds.y + offset);
        cairo_paint(cr);
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
        cairo_set_source_surface(cr, pii->icon_surface, container->real_bounds.x + 8, container->real_bounds.y + 8);
        cairo_paint(cr);
    }

    auto windows_data = ((BodyData *) container->parent->parent->children[1]->user_data)->windows_data;
    std::string title = windows_data->title;
    if (title.empty()) {
        auto pii = (PinnedIconInfo *) client_entity->root->user_data;
        title = pii->data->class_name;
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    int width;
    int height;
    pango_layout_set_text(layout, title.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);

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
    pango_layout_set_width(layout, (((container->real_bounds.w - (pad * 2)) + close_w) - 24) * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
    if (close_w == 0) {
        pad = option_pad;
    }

    set_argb(cr, config->color_windows_selector_text);
    cairo_move_to(cr,
                  container->real_bounds.x + pad + 24,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
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
    double scale_w = target_width / data->width;
    double scale_h = target_height / data->height;
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

static void
fill_root(AppClient *client, Container *root) {
    auto pii = (PinnedIconInfo *) root->user_data;
    pii->surface = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(pii->surface, as_resource_path("taskbar-close.png"), 16, nullptr);
    root->receive_events_even_if_obstructed = true;
    root->when_mouse_enters_container = when_enter;
    root->when_mouse_leaves_container = when_leave;

    if (screen_has_transparency(app)) {
        for (auto window_data : pii->data->windows_data_list) {
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
        auto body_data = new BodyData;
        body_data->windows_data = w;
        option_body->user_data = body_data;
        option_container->children.push_back(option_body);
    }
}

static bool first_expose = true;

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
    }
}

static bool
windows_selector_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            if (auto c = client_by_window(app, e->window)) {
                if (c->name == "windows_selector") {
                    auto pii = (PinnedIconInfo *) c->root->user_data;
                    if (pii->data->type == ::OPEN_CLICKED) {
                        register_popup(e->window);
                    }
                }
            }

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
                set_textarea_inactive();
            }
            break;
        }
    }

    return false;
}

static void when_closed(AppClient *client) {
    auto pii = (PinnedIconInfo *) client->root->user_data;
    pii->data->type = selector_type::CLOSED;
    app_timeout_stop(client->app, client, pii->data->possibly_stop_timeout_fd);
    app_timeout_stop(client->app, client, pii->data->possibly_open_timeout_fd);
    pii->data->possibly_stop_timeout_fd = -1;
    pii->data->possibly_open_timeout_fd = -1;
}

void start_windows_selector(Container *container, selector_type selector_state) {
    first_expose = true;
    auto data = (LaunchableButton *) container->user_data;
    auto pii = new PinnedIconInfo;
    pii->data_container = container;
    pii->data = data;
    pii->data->type = selector_state;
    pii->data->possibly_open_timeout_fd = -1;
    pii->data->possibly_stop_timeout_fd = -1;

    int width = get_width(pii->data);
    Settings settings;
    settings.x = container->real_bounds.x - width / 2 + pii->data_container->real_bounds.w / 2;
    if (settings.x < 0) {
        settings.x = 0;
    }
    settings.y = app->bounds.h - option_height - config->taskbar_height;
    settings.w = width;
    settings.h = option_height;
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.popup = true;

    auto client = client_new(app, settings, "windows_selector");
    client->root->user_data = pii;
    if (pii->data->surface) {
        pii->icon_surface = accelerated_surface(app, client, 16, 16);
        cairo_t *cr = cairo_create(pii->icon_surface);

        double starting_w = cairo_image_surface_get_width(pii->data->surface);
        double target_w = 16;
        double sx = target_w / starting_w;

        cairo_scale(cr, sx, sx);
        cairo_set_source_surface(cr, pii->data->surface, 0, 0);
        cairo_paint(cr);

        cairo_destroy(cr);
    }


    client->fps = 2;
    client->grab_event_handler = grab_event_handler;
    client->when_closed = when_closed;
    client_register_animation(app, client);

    app_create_custom_event_handler(app, client->window, windows_selector_event_handler);

    fill_root(client, client->root);

    client_show(app, client);

    if (auto c = client_by_name(app, "taskbar")) {
        request_refresh(app, c);
    }
}

PinnedIconInfo::~PinnedIconInfo() {
    cairo_surface_destroy(icon_surface);
}
