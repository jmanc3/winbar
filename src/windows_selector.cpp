
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

static double option_width = 217;
static double option_height = 144;
static double close_width = 32;
static double close_height = 32;
static Container *data_container = nullptr;
static LaunchableButton *data = nullptr;
static AppClient *client = nullptr;

static void
fill_root(Container *root);

static void
window_option_clicked(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }

    xcb_window_t to_focus = data->windows[index];

    xcb_ewmh_request_change_active_window(&app->ewmh,
                                          app->screen_number,
                                          to_focus,
                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                          XCB_CURRENT_TIME,
                                          XCB_NONE);

    client_close_threaded(app, client_entity);
}

static void
clicked_body(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent);
}

static void
clicked_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_clicked(client_entity, cr, container->parent->parent);
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

    xcb_window_t to_close = data->windows[index];

    unsigned long windows_count = data->windows.size();
    xcb_ewmh_request_close_window(&app->ewmh,
                                  app->screen_number,
                                  to_close,
                                  XCB_CURRENT_TIME,
                                  XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL);

    if (windows_count > 1) {
        std::thread t([windows_count]() -> void {
            long start_time = get_current_time_in_ms();
            while (get_current_time_in_ms() - start_time < 2000) {
                if (data->windows.size() != windows_count) {
                    double width = option_width * data->windows.size();
                    double x =
                            data_container->real_bounds.x - width / 2 + data_container->real_bounds.w / 2;

                    xcb_ewmh_request_moveresize_window(
                            &app->ewmh,
                            app->screen_number,
                            client->window,
                            static_cast<xcb_gravity_t>(0),
                            XCB_EWMH_CLIENT_SOURCE_TYPE_NORMAL,
                            static_cast<xcb_ewmh_moveresize_window_opt_flags_t>(
                                    XCB_EWMH_MOVERESIZE_WINDOW_X | XCB_EWMH_MOVERESIZE_WINDOW_WIDTH),
                            x,
                            0,
                            width,
                            0);
                    xcb_flush(app->connection);

                    for (auto child : client->root->children) {
                        delete child;
                    }
                    client->root->children.clear();
                    fill_root(client->root);
                    client_paint(app, client);

                    break;
                }
                usleep(50);
            }
        });
        t.detach();
    } else {
        client_close_threaded(app, client);
    }
}

static void
clicked_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    window_option_closed(client_entity, cr, container->parent->parent);
}

static void
paint_hovered(AppClient *client_entity, cairo_t *cr, Container *container) {
    return;
}

static void
paint_option_background(AppClient *client_entity, cairo_t *cr, Container *container) {
    bool hovered = false;
    bool pressed = false;
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

    ArgbColor copy = config->main_bg;
    copy.a = config->taskbar_transparency;
    set_argb(cr, copy);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    if (hovered || pressed) {
        if (pressed) {
            set_argb(cr, config->button_pressed);
        } else {
            set_argb(cr, config->button_hovered);
        }
    } else {
        set_argb(cr, config->button_default);
    }
    cairo_rectangle(cr,
                    container->real_bounds.x + 1,
                    container->real_bounds.y + 1,
                    container->real_bounds.w - 2,
                    container->real_bounds.h - 2);
    cairo_fill(cr);
}

static void
paint_close(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto *data = static_cast<IconButton *>(client->root->user_data);

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        ArgbColor color;
        color.a = 1;
        if (container->state.mouse_pressing) {
            color.r = .910;
            color.g = .067;
            color.b = .137;
        } else {
            color.r = .776;
            color.g = .102;
            color.b = .157;
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
        dye_surface(data->surface, ArgbColor(1, 1, 1, 1));
        double offset = (double) (32 - 16) / 2;
        cairo_set_source_surface(
                cr, data->surface, container->real_bounds.x + offset, container->real_bounds.y + offset);
        cairo_paint(cr);
    }
}

static void
paint_titlebar(AppClient *client_entity, cairo_t *cr, Container *container) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); i++) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    int width;
    int height;
    pango_layout_set_text(layout, data->class_name.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, config->calendar_font_default);
    cairo_move_to(cr,
                  container->real_bounds.x + 10,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_body(AppClient *client_entity, cairo_t *cr, Container *container) {
    double pad = 10;
    cairo_rectangle(cr,
                    container->real_bounds.x + pad,
                    container->real_bounds.y,
                    container->real_bounds.w - pad * 2,
                    container->real_bounds.h - pad);
    ArgbColor color;
    color.a = 1;
    color.r = .1;
    color.g = .1;
    color.b = .1;

    set_argb(cr, color);
    cairo_fill(cr);
}

static void
fill_root(Container *root) {
    IconButton *image = new IconButton;
    image->surface = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(image->surface, as_resource_path("taskbar-close.png"), nullptr);
    root->user_data = image;

    if (data == nullptr)
        return;

    for (int i = 0; i < data->windows.size(); i++) {
        Container *option_container = new Container();
        option_container->type = vbox;
        option_container->parent = root;
        option_container->wanted_bounds.w = FILL_SPACE;
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
        option_container->children.push_back(option_body);
    }
}

static bool first_expose = true;

static bool
windows_selector_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_ungrab_pointer(app->connection, XCB_CURRENT_TIME);
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
        }
    }

    return true;
}

void start_windows_selector(Container *container) {
    first_expose = true;
    data_container = container;
    data = static_cast<LaunchableButton *>(container->user_data);

    double width = option_width * data->windows.size();
    Settings settings;
    settings.x = container->real_bounds.x - width / 2 + data_container->real_bounds.w / 2;
    settings.y = app->bounds.h - option_height - config->taskbar_height;
    settings.w = width;
    settings.h = option_height;
    settings.force_position = true;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.popup = true;

    client = client_new(app, settings, "windows_selector");

    client_add_handler(app, client, windows_selector_event_handler);

    fill_root(client->root);

    client_show(app, client);
}