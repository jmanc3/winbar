
#include "wifi_menu.h"
#include "application.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "utility.h"

#include <fstream>
#include <iostream>
#include <pango/pangocairo.h>

class cached_bounds : public UserData {
public:
    bool cached = false;
    Bounds bounds;
    Bounds cached_real_bounds;
};

void wifi_state(bool *up, bool *wired) {
    std::string status = "down";
    std::ifstream status_file("/sys/class/net/" + std::string(config->interface) + "/operstate");
    if (status_file.is_open()) {
        std::string line;
        if (getline(status_file, line)) {
            status = line;
        }
        status_file.close();
    }

    *up = status == "up";

    // Wireless interfaces are prefixed with wlp
    *wired = std::string::npos == config->interface.find("wlp");
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_wifi_background));
    cairo_fill(cr);
}

static void
paint_wifi(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (wifi_surfaces *) container->user_data;

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_wifi_pressed_button);
        } else {
            set_argb(cr, config->color_wifi_hovered_button);
        }
    } else {
        set_argb(cr, config->color_wifi_default_button);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    bool up = false;
    bool wired = false;
    wifi_state(&up, &wired);

    cairo_surface_t *surface = nullptr;
    if (up) {
        if (wired) {
            surface = data->wired_up;
        } else {
            surface = data->wireless_up;
        }
    } else {
        if (wired) {
            surface = data->wired_down;
        } else {
            surface = data->wireless_down;
        }
    }

    if (surface) {
        dye_surface(surface, config->color_wifi_icons);
        cairo_set_source_surface(
                cr,
                surface,
                (int) (container->real_bounds.x + 9),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 24 / 2));
        cairo_paint(cr);
    }

    set_argb(cr, config->color_wifi_text_title);

    std::string text = "Network";
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, text.c_str(), text.length());

    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);
    int text_x = (int) (container->real_bounds.x + 9 + 24 + 9);
    int text_y =
            (int) (container->real_bounds.y + container->real_bounds.h / 2 - ((height * 2) / 2));
    cairo_move_to(cr, text_x, text_y);
    pango_cairo_show_layout(cr, layout);

    set_argb(cr, config->color_wifi_text_title_info);

    if (up)
        text = "Connected - Interface: " + config->interface;
    else
        text = "Disconnected - Interface: " + config->interface;
    pango_layout_set_text(layout, text.c_str(), text.length());
    cairo_move_to(cr, text_x, text_y + height);

    pango_cairo_show_layout(cr, layout);
}

static void
clicked_wifi(AppClient *client, cairo_t *cr, Container *container) {}

static void
paint_info(AppClient *client, cairo_t *cr, Container *container) {
    std::string top_text = "Network & Internet Settings";
    PangoLayout *top_layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(top_layout, top_text.c_str(), top_text.length());
    int top_width, top_height;
    pango_layout_get_pixel_size(top_layout, &top_width, &top_height);

    std::string bottom_text = "Change settings, such as making a connection metered";
    PangoLayout *bottom_layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(bottom_layout, bottom_text.c_str(), bottom_text.length());
    int bottom_width, bottom_height;
    pango_layout_get_pixel_size(bottom_layout, &bottom_width, &bottom_height);

    int distance_from_bottom = 7;

    set_argb(cr, config->color_wifi_text_settings_title_info);

    int text_x = 13;
    int text_y =
            container->real_bounds.y + container->real_bounds.h - distance_from_bottom - bottom_height;
    cairo_move_to(cr, text_x, text_y);
    pango_cairo_show_layout(cr, bottom_layout);

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            set_argb(cr, config->color_wifi_text_settings_pressed_title);
        else
            set_argb(cr, config->color_wifi_text_settings_hovered_title);
    } else {
        set_argb(cr, config->color_wifi_text_settings_default_title);
    }

    cairo_move_to(cr, text_x, text_y - bottom_height - 5);
    pango_cairo_show_layout(cr, top_layout);
}

static void
clicked_info(AppClient *client, cairo_t *cr, Container *container) {
    printf("Run provided wifi info command\n");
}

// The bounds of the container wifi_info is bigger than I want for when hovering the text should be
// determined so this will make sure only when the text is hovered is the container hovered
static bool
pierced_wifi_info(Container *container, int mouse_x, int mouse_y) {
    auto *data = (cached_bounds *) container->user_data;

    if (data->cached) {
        if (data->cached_real_bounds.x != container->real_bounds.x ||
            data->cached_real_bounds.y != container->real_bounds.y ||
            data->cached_real_bounds.w != container->real_bounds.w ||
            data->cached_real_bounds.h != container->real_bounds.h) {
            data->cached = false;
        }
    }

    if (!data->cached) {
        if (auto client = client_by_name(app, "wifi_menu")) {
            data->cached_real_bounds = container->real_bounds;
            data->cached = true;
            std::string top_text = "Network & Internet Settings";
            PangoLayout *top_layout = get_cached_pango_font(
                    client->cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
            pango_layout_set_text(top_layout, top_text.c_str(), top_text.length());
            int top_width, top_height;
            pango_layout_get_pixel_size(top_layout, &top_width, &top_height);

            std::string bottom_text = "Change settings, such as making a connection metered";
            PangoLayout *bottom_layout = get_cached_pango_font(
                    client->cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
            pango_layout_set_text(bottom_layout, bottom_text.c_str(), bottom_text.length());
            int bottom_width, bottom_height;
            pango_layout_get_pixel_size(bottom_layout, &bottom_width, &bottom_height);

            int distance_from_bottom = 7;

            int text_x = 13;
            int text_y = container->real_bounds.y + container->real_bounds.h -
                         distance_from_bottom - bottom_height - top_height;

            data->bounds.x = text_x;
            data->bounds.y = text_y;
            data->bounds.w = container->real_bounds.w;
            data->bounds.h = bottom_height + top_height;
        }
    }

    if (data->cached) {
        return bounds_contains(data->bounds, mouse_x, mouse_y);
    }
    return bounds_contains(container->real_bounds, mouse_x, mouse_y);
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->type = ::vbox;
    root->when_paint = paint_root;
    root->wanted_pad.y = 12;
    root->wanted_pad.h = 12;

    Container *button_wifi = root->child(FILL_SPACE, FILL_SPACE);
    button_wifi->when_paint = paint_wifi;
    button_wifi->when_clicked = clicked_wifi;
    auto wifi_data = new wifi_surfaces;
    wifi_data->wired_up = accelerated_surface(app, client, 24, 24);
    paint_surface_with_image(
            wifi_data->wired_up, as_resource_path("wifi/24/wired_up.png"), 24, nullptr);
    wifi_data->wired_down = accelerated_surface(app, client, 24, 24);
    paint_surface_with_image(
            wifi_data->wired_down, as_resource_path("wifi/24/wired_down.png"), 24, nullptr);
    wifi_data->wireless_down = accelerated_surface(app, client, 24, 24);
    paint_surface_with_image(
            wifi_data->wireless_down, as_resource_path("wifi/24/wireless_down.png"), 24, nullptr);
    wifi_data->wireless_up = accelerated_surface(app, client, 24, 24);
    paint_surface_with_image(
            wifi_data->wireless_up, as_resource_path("wifi/24/wireless_up.png"), 24, nullptr);
    button_wifi->user_data = wifi_data;

    auto *info = root->child(FILL_SPACE, 71);
    info->when_paint = paint_info;
    info->when_clicked = clicked_info;
    info->handles_pierced = pierced_wifi_info;
    info->user_data = new cached_bounds;
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
wifi_menu_event_handler(App *app, xcb_generic_event_t *event) {
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

void start_wifi_menu() {
    first_expose = true;

    Settings settings;
    settings.h = 12 * 2 + 55 + 71;// number based on fill_root
    settings.w = 360;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.sticky = true;
    settings.popup = true;

    AppClient *client = client_new(app, settings, "wifi_menu");
    client->grab_event_handler = grab_event_handler;

    app_create_custom_event_handler(app, client->window, wifi_menu_event_handler);

    fill_root(client);
    client_show(app, client);
}
