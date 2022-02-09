
#include "systray.h"
#include "application.h"
#include "main.h"
#include "config.h"
#include "taskbar.h"

#include <utility.h>
#include <defer.h>

static std::vector<xcb_window_t> systray_icon_windows;
static bool layout_invalid = true;

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2

#define ICON_SIZE_WITH_PAD 40
#define ICON_SIZE 22

/**
 * The reason we put unmap the systray after a certain amount of time instead of just instantly,
 * is because right-click menu's created from systray icon's need a moment to determine where they're going to position
 * themselves. If we didn't delay, the menu's would be created at 0, 0 (the top-left corner of the screen).
 */
void systray_unmap_timeout(App *app, AppClient *, Timeout *, void *) {
    auto systray = client_by_name(app, "systray");
    if (systray == nullptr)
        return;

    xcb_unmap_window(app->connection, systray->window);
}

static void systray_close() {
    auto systray = client_by_name(app, "systray");
    if (systray == nullptr)
        return;

    xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
    app->grab_window = -1;
    xcb_flush(app->connection);
    xcb_aux_sync(app->connection);

    app_timeout_create(app, nullptr, 100, systray_unmap_timeout, nullptr, false);
}

/**
 * This checks if the click was outside the systray (and if so, it closes it).
 * It also does the stuff needed so that menu's created by the taskbar open and close rather than just
 * open---(it's sorta complicated)---when the systray button is repeatably pressed for instance.
 */
static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                systray_close();

                if (auto taskbar_client = client_by_name(app, "taskbar")) {
                    if (auto systray_button = container_by_name("systray", taskbar_client->root)) {
                        if (systray_button->state.mouse_hovering) {
                            auto data = (IconButton *) systray_button->user_data;
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

unsigned long
create_rgba(int r, int g, int b, int a) {
    return ((a & 0xff) << 24) + ((r & 0xff) << 16) + ((g & 0xff) << 8) + (b & 0xff);
}

static void layout_systray();

static bool
systray_event_handler(App *app, xcb_generic_event_t *event) {
    auto systray = client_by_name(app, "systray");
    if (systray == nullptr)
        return true;

    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CLIENT_MESSAGE: {
            auto *client_message = (xcb_client_message_event_t *) event;

            if (client_message->data.data32[0] == app->delete_window_atom) {
                for (auto icon_window: systray_icon_windows) { // rescind ownership of icon windows
                    xcb_reparent_window(app->connection, icon_window, app->screen->root, 100, 100);
                }
            } else if (client_message->type == get_cached_atom(app, "_NET_SYSTEM_TRAY_OPCODE")) {
                if (client_message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK) {
                    auto window_that_wants_to_dock = client_message->data.data32[2];

                    for (auto icon_window: systray_icon_windows) // Avoid duplicates
                        if (icon_window == window_that_wants_to_dock)
                            break;

                    // Set which events we want to receive from window_that_wants_to_dock
                    const uint32_t cw_values[] = {
                            XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                            XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW};
                    xcb_change_window_attributes(app->connection, window_that_wants_to_dock,
                                                 XCB_CW_EVENT_MASK, cw_values);
                    xcb_change_save_set(app->connection, XCB_SET_MODE_INSERT, window_that_wants_to_dock);

                    ArgbColor bg_color = correct_opaqueness(systray, config->color_systray_background);
                    int r = (int) (bg_color.r * 255);
                    int g = (int) (bg_color.g * 255);
                    int b = (int) (bg_color.b * 255);
                    int a = (int) (bg_color.a * 255);
                    uint32_t rgb = create_rgba(r, g, b, a);
                    uint32_t value_list[] = {rgb};
                    xcb_change_window_attributes(app->connection, window_that_wants_to_dock,
                                                 XCB_CW_BACK_PIXEL, value_list);

                    systray_icon_windows.emplace_back(window_that_wants_to_dock);

                    xcb_reparent_window(app->connection, window_that_wants_to_dock,
                                        systray->window, 10, 10);
                    xcb_unmap_window(app->connection, window_that_wants_to_dock);

                    layout_invalid = true;
                }
            }
            break;
        }
        case XCB_MAP_NOTIFY: {
            register_popup(systray->window); // Grab the mouse pointer, so we know when to close the systray window
            layout_invalid = true;
            break;
        }
        case XCB_FOCUS_OUT: {
            systray_close(); // If another window gains focus, close the systray
            break;
        }
        case XCB_BUTTON_PRESS: {
            grab_event_handler(systray, event); // Checks if the button was outside the systray
            break;
        }
    }

    return false; // Let other's handle event
}

/**
 * Since the icon windows paint themselves, and the background color of the systray is set during its creation,
 * we aren't actually painting anything. What were actually doing is checking if the layout is invalid, in which case
 * we do a layout, and make sure the windows are mapped.
 */
static void
paint_systray(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (layout_invalid) {
        for (auto icon_window: systray_icon_windows)
            xcb_map_window(app->connection, icon_window);

        layout_systray();
    }

    ArgbColor bg_color = correct_opaqueness(client, config->color_systray_background);
    set_rect(cr, container->real_bounds);
    set_argb(cr, bg_color);
    cairo_fill(cr);
}

static bool
icons_event_handler(App *app, xcb_generic_event_t *event) {
    // Since this function looks at every single xcb event generated
    // we first need to filter out windows that are not clients (icons) we are handling
    xcb_window_t event_window = get_window(event);

    bool window_is_systray_icon = false;
    for (auto icon_window: systray_icon_windows)
        if (icon_window == event_window)
            window_is_systray_icon = true;

    if (!window_is_systray_icon)
        return false; // Let someone else handle the event

    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_DESTROY_NOTIFY: {
            for (int i = 0; i < systray_icon_windows.size(); i++)
                if (systray_icon_windows[i] == event_window)
                    systray_icon_windows.erase(systray_icon_windows.begin() + i);

            if (systray_icon_windows.empty())
                systray_close();
            break;
        }
    }
    layout_invalid = true;

    return false; // Let no one else handle event
}

void register_as_systray() {
    Settings settings;
    settings.window_transparent = false;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.w = ICON_SIZE_WITH_PAD;
    settings.h = ICON_SIZE_WITH_PAD;
    settings.x = 0;
    settings.y = 0;
    settings.no_input_focus = false;
    settings.popup = true;
    settings.background = argb_to_color(config->color_systray_background);

    auto systray = client_new(app, settings, "systray");
    systray->keeps_app_running = false;
    systray->grab_event_handler = grab_event_handler;
    systray->root->when_paint = paint_systray;

    std::string selection = "_NET_SYSTEM_TRAY_S";
    selection.append(std::to_string(app->screen_number));
    auto tray_atom = get_cached_atom(app, selection.c_str());
    xcb_set_selection_owner(app->connection, systray->window, tray_atom, XCB_CURRENT_TIME);
    auto selection_owner_cookie = xcb_get_selection_owner(app->connection, tray_atom);
    auto *selection_owner_reply =
            xcb_get_selection_owner_reply(app->connection, selection_owner_cookie, NULL);
    defer(free(selection_owner_reply));
    if (selection_owner_reply->owner !=
        systray->window) { // If our systray window didn't become the owner of the selection
        client_close_threaded(app, systray);
        return;
    }

    systray_icon_windows.clear();
    layout_invalid = true;

    app_create_custom_event_handler(app, systray->window, systray_event_handler);
    app_create_custom_event_handler(app, INT_MAX, icons_event_handler);

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
    if (auto systray = client_by_name(app, "systray")) {
        xcb_map_window(app->connection, systray->window);
        for (auto icon_window: systray_icon_windows)
            xcb_map_window(app->connection, icon_window);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
        layout_systray();
        layout_invalid = true;
    }
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
    int w = closest_square_root_above(systray_icon_windows.size());
    if (w == 0) { // even if the systray has no icons we want to show a 1x1
        w = 1;
    } else if (w > 4) { // after we reach a width of 4 icons we just want to grow upwards
        w = 4;
    }

    // This part puts the icon in the correct location at the correct size
    for (int i = 0; i < systray_icon_windows.size(); i++) {
        if (x == w) {
            x = 0;
            y++;
        }

        xcb_window_t icon_window = systray_icon_windows[i];

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                              XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {
                (uint32_t) (x * ICON_SIZE_WITH_PAD + ICON_SIZE_WITH_PAD / 2 - ICON_SIZE / 2),
                (uint32_t) (y * ICON_SIZE_WITH_PAD + ICON_SIZE_WITH_PAD / 2 - ICON_SIZE / 2),
                ICON_SIZE,
                ICON_SIZE};
        xcb_configure_window(app->connection, icon_window, value_mask, value_list_resize);

        x++;
    }

    // If the display window (which holds our icon windows) is open, we move and resize it to the correct spot
    if (auto systray = client_by_name(app, "systray")) {
        layout_invalid = false;

        auto window_width = (uint32_t) ICON_SIZE_WITH_PAD * w;
        auto window_height = (uint32_t) ICON_SIZE_WITH_PAD * ++y;

        auto window_x = (uint32_t) 0;
        auto window_y = (uint32_t) 0;

        if (auto taskbar = client_by_name(app, "taskbar")) {
            window_x = taskbar->bounds->x;
            window_y = taskbar->bounds->y - window_height;
            if (auto systray_button = container_by_name("systray", taskbar->root)) {
                window_x += systray_button->real_bounds.x + systray_button->real_bounds.w / 2 - window_width / 2;
            }
        }

        uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                              XCB_CONFIG_WINDOW_HEIGHT;
        uint32_t value_list_resize[] = {window_x, window_y, window_width, window_height};
        xcb_configure_window(app->connection, systray->window, value_mask, value_list_resize);
        systray->bounds->x = window_x;
        systray->bounds->y = window_y;
        systray->bounds->w = window_width;
        systray->bounds->h = window_height;
    }
}
