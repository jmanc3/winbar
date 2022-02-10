#include "systray.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"
#include "../tracy/common/TracySystem.hpp"

#endif

#include <iostream>
#include <utility.h>
#include <xcb/xcb_event.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0

static AppClient *systray = nullptr;
static AppClient *display = nullptr;
static uint32_t icon_size = 22;
static uint32_t container_size = 40;
static bool layout_invalid = true;

static void
layout_systray();

static void
display_close();

class Systray_Icon {
public:
    xcb_window_t window;
};

std::vector<Systray_Icon *> systray_icons;

unsigned long
create_rgba(int r, int g, int b, int a) {
    return ((a & 0xff) << 24) + ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

static void
paint_display(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor bg_color = correct_opaqueness(client_entity, config->color_systray_background);

    for (int i = 0; i < systray_icons.size(); i++) {
        Systray_Icon *icon = systray_icons[i];
        int r = (int) (bg_color.r * 255);
        int g = (int) (bg_color.g * 255);
        int b = (int) (bg_color.b * 255);
        int a = (int) (bg_color.a * 255);
        uint32_t rgb = create_rgba(r, g, b, a);
        uint32_t value_list[] = {rgb};
        xcb_change_window_attributes(app->connection, icon->window, XCB_CW_BACK_PIXEL, value_list);
    }

    if (layout_invalid) {
        layout_systray();
    }

    for (auto icon: systray_icons) {
        xcb_map_window(app->connection, icon->window);
    }

    xcb_map_window(app->connection, client_entity->window);
    xcb_map_subwindows(app->connection, client_entity->window);

    xcb_flush(app->connection);

    // paint the background

    set_rect(cr, container->real_bounds);
    set_argb(cr, bg_color);
    cairo_fill(cr);
}

static bool
systray_event_handler(App *app, xcb_generic_event_t *event) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CLIENT_MESSAGE: {
            auto *client_message = (xcb_client_message_event_t *) event;

            if (client_message->type == get_cached_atom(app, "_NET_SYSTEM_TRAY_OPCODE")) {
                if (client_message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK) {
                    auto window_to_be_docked = client_message->data.data32[2];

                    for (auto icon: systray_icons)
                        if (icon->window == window_to_be_docked)
                            break;

                    const uint32_t cw_values[] = {
                            XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                            XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW};
                    xcb_change_window_attributes(app->connection, window_to_be_docked,
                                                 XCB_CW_EVENT_MASK, cw_values);
                    xcb_change_save_set(app->connection, XCB_SET_MODE_INSERT, window_to_be_docked);

                    auto icon = new Systray_Icon;
                    icon->window = window_to_be_docked;
                    systray_icons.push_back(icon);

                    if (display) {
                        xcb_reparent_window(app->connection, icon->window, display->window, -512, -512);
                    } else if (systray) {
                        xcb_reparent_window(app->connection, icon->window, systray->window, 0, 0);
                    }
                    xcb_unmap_window(app->connection, icon->window);

                    layout_invalid = true;
                }
            }
        }
            break;
    }

    return false;
}

// The XEmbed Protocol says that when you re-parent a window into your window
// You should basically act in the way that a windows manager acts a.k.a
// Selecting the SubstrucreRedirectMask and intercepting events on it like mapping/unmapping
// configuring and so on. So that's what we do in this icon_event_handler
static bool
icon_event_handler(App *app, xcb_generic_event_t *generic_event) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Since this function looks at every single xcb event generated
    // we first need to filter out windows that are not clients (icons) we are handling
    xcb_window_t event_window = get_window(generic_event);

    bool window_is_systray_icon = false;
    for (auto icon: systray_icons)
        if (icon->window == event_window)
            window_is_systray_icon = true;
    if (!window_is_systray_icon)
        return false;// Let someone else handle the event

    switch (XCB_EVENT_RESPONSE_TYPE(generic_event)) {
        case XCB_DESTROY_NOTIFY: {
            for (int i = 0; i < systray_icons.size(); i++) {
                auto icon = systray_icons[i];
                if (icon->window == event_window) {
                    systray_icons.erase(systray_icons.begin() + i);
                    delete icon;
                }
            }
            layout_invalid = true;

            if (systray_icons.empty()) {
                display_close();
            }
            break;
        }
    }
    layout_invalid = true;

    return true;
}

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*display->bounds, e->root_x, e->root_y)) {
                app->grab_window = -1;
                set_textarea_inactive();
                display_close();

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("systray", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (IconButton *) co->user_data;
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
display_event_handler(App *app, xcb_generic_event_t *event) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CLIENT_MESSAGE: {
            auto *e = (xcb_client_message_event_t *) event;
            auto client = client_by_window(app, e->window);
            if (!valid_client(app, client))
                break;

            if (e->data.data32[0] == app->delete_window_atom) {
                display_close();
            }
            break;
        }
        case XCB_MAP_NOTIFY: {
            register_popup(display->window);
            break;
        }
        case XCB_FOCUS_OUT: {
            display_close();
            break;
        }
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!valid_client(app, display))
                break;
            if (!bounds_contains(*display->bounds, e->root_x, e->root_y)) {
                display_close();
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
    }
    return false;
}

static int
closest_square_root_above(int target) {
    int i = 0;
    while (true && i < 100) {
        if (i * i >= target)
            return i;
        i++;
    }
    return 0;
}

static void
layout_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // If this looks funky, it's because systray icons are laid out wierdly
    int x = 0;
    int y = 0;
    int w = closest_square_root_above(systray_icons.size());
    if (w == 0) {// even if the systray has no icons we want to show a 1x1
        w = 1;
    } else if (w > 4) {// after we reach a width of 4 icons we just want to grow upwards
        w = 4;
    }

    // This part puts the icon in the correct location at the correct size
    for (int i = 0; i < systray_icons.size(); i++) {
        if (x == w) {
            x = 0;
            y++;
        }

        Systray_Icon *icon = systray_icons[i];

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                              XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {
                (uint32_t) (x * container_size + container_size / 2 - icon_size / 2),
                (uint32_t) (y * container_size + container_size / 2 - icon_size / 2),
                icon_size,
                icon_size};
        xcb_configure_window(app->connection, icon->window, value_mask, value_list_resize);

        x++;
    }

    // If the display window (which holds our icon windows) is open, we move and resize it to the
    // correct spot
    if (display) {
        layout_invalid = false;

        auto window_width = (uint32_t) container_size * w;
        auto window_height = (uint32_t) container_size * ++y;

        auto window_x = (uint32_t) 0;
        auto window_y = (uint32_t) (app->bounds.h - config->taskbar_height - window_height);

        if (auto taskbar = client_by_name(app, "taskbar")) {
            window_x = taskbar->bounds->x;
            window_y = taskbar->bounds->y - window_height;
            if (auto container = container_by_name("systray", taskbar->root)) {
                window_x += container->real_bounds.x + container->real_bounds.w / 2 -
                            window_width / 2;
            }
        }

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                              XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {window_x, window_y, window_width, window_height};
        xcb_configure_window(app->connection, display->window, value_mask, value_list_resize);
    }
}

static void
on_display_closed(AppClient *) {
    if (systray)
        for (auto icon: systray_icons)
            xcb_reparent_window(app->connection, icon->window, systray->window, 0, 0);
}

static void
on_systray_closed(AppClient *) {
    if (systray)
        for (auto icon: systray_icons) {
            xcb_reparent_window(app->connection, icon->window, app->screen->root, 0, 0);
        }
}

void register_as_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Settings settings;
    settings.window_transparent = false;
    settings.background = argb_to_color(config->color_systray_background);
    systray = client_new(app, settings, "systray");
    systray->keeps_app_running = false;
    systray->when_closed = on_systray_closed;

    std::string selection = "_NET_SYSTEM_TRAY_S";
    selection.append(std::to_string(app->screen_number));
    auto tray_atom = get_cached_atom(app, selection.c_str());
    xcb_set_selection_owner(app->connection, systray->window, tray_atom, XCB_CURRENT_TIME);
    auto selection_owner_cookie = xcb_get_selection_owner(app->connection, tray_atom);
    auto *selection_owner_reply =
            xcb_get_selection_owner_reply(app->connection, selection_owner_cookie, NULL);
    if (selection_owner_reply->owner != systray->window) {
        client_close_threaded(app, systray);
        systray = nullptr;
        free(selection_owner_reply);
        return;
    }
    free(selection_owner_reply);

    layout_invalid = true;
    for (auto a: systray_icons)
        delete a;
    systray_icons.clear();
    systray_icons.shrink_to_fit();

    app_create_custom_event_handler(app, systray->window, systray_event_handler);
    app_create_custom_event_handler(app, INT_MAX, icon_event_handler);

    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = app->screen->root;
    ev.format = 32;
    ev.type = get_cached_atom(app, "MANAGER");
    ev.data.data32[0] = 0;
    ev.data.data32[1] = tray_atom;
    ev.data.data32[2] = systray->window;
    ev.data.data32[3] = ev.data.data32[4] = 0;

    xcb_send_event_checked(app->connection, false, app->screen->root, 0xFFFFFF, (char *) &ev);
    xcb_flush(app->connection);
}

void open_systray() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!systray) {
        printf("Can't open systray display because we didn't manage to register as it earlier\n");
        return;
    }

    Settings settings;
    // Very important that the window is not 32 bit depth because you can't embed non transparent windows into transparent ones
    settings.window_transparent = false;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.w = 1;
    settings.h = 1;
    settings.x = -settings.w * 2;
    settings.y = -settings.h * 2;
    settings.no_input_focus = false;
    settings.popup = true;
    settings.background = argb_to_color(config->color_systray_background);

    display = client_new(app, settings, "display");
    display->grab_event_handler = grab_event_handler;
    display->root->when_paint = paint_display;
    display->when_closed = on_display_closed;

    app_create_custom_event_handler(app, display->window, display_event_handler);

    for (auto icon: systray_icons)
        xcb_reparent_window(app->connection, icon->window, display->window, -512, -512);

    client_show(app, display);

    layout_systray();

    layout_invalid = true;
}

void display_close_timeout(App *app, AppClient *, Timeout *, void *) {
    client_close_threaded(app, display);
    display = nullptr;
}

static void
display_close() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    app->grab_window = 0;
    xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
    app_timeout_create(app, nullptr, 100, display_close_timeout, nullptr, false);
}
