
#include "application.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "utility.h"
#include "dpi.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <unistd.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_cursor.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/timerfd.h>
#include <cassert>
#include <cmath>

#define explicit dont_use_cxx_explicit

#include <xcb/xcb_keysyms.h>
#include <xcb/xkb.h>
#include <xcb/xcb_aux.h>

#undef explicit

int
update_keymap(struct ClientKeyboard *kbd) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    struct xkb_keymap *new_keymap;
    struct xkb_state *new_state;

    new_keymap = xkb_x11_keymap_new_from_device(
            kbd->ctx, kbd->conn, kbd->device_id, static_cast<xkb_keymap_compile_flags>(0));
    if (!new_keymap)
        goto err_out;

    new_state = xkb_state_new(new_keymap);
    if (!new_state)
        goto err_keymap;

    if (kbd->keymap)
        printf("Keymap updated!\n");

    xkb_state_unref(kbd->state);
    xkb_keymap_unref(kbd->keymap);
    kbd->keymap = new_keymap;
    kbd->state = new_state;
    return 0;

    err_keymap:
    xkb_keymap_unref(new_keymap);
    err_out:
    return -1;
}

static int
select_xkb_events_for_device(xcb_connection_t *conn, int32_t device_id) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    enum {
        required_events = (XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY |
                           XCB_XKB_EVENT_TYPE_STATE_NOTIFY),

        required_nkn_details = (XCB_XKB_NKN_DETAIL_KEYCODES),

        required_map_parts =
        (XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP |
         XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS |
         XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP),

        required_state_details =
        (XCB_XKB_STATE_PART_MODIFIER_BASE | XCB_XKB_STATE_PART_MODIFIER_LATCH |
         XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_BASE |
         XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK),
    };

    static const xcb_xkb_select_events_details_t details = {
            .affectNewKeyboard = required_nkn_details,
            .newKeyboardDetails = required_nkn_details,
            .affectState = required_state_details,
            .stateDetails = required_state_details,
    };

    xcb_void_cookie_t cookie = xcb_xkb_select_events_aux_checked(conn,
                                                                 device_id,
                                                                 required_events,    /* affectWhich */
                                                                 0,                  /* clear */
                                                                 0,                  /* selectAll */
                                                                 required_map_parts, /* affectMap */
                                                                 required_map_parts, /* map */
                                                                 &details);          /* details */

    xcb_generic_error_t *error = xcb_request_check(conn, cookie);
    if (error) {
        free(error);
        return -1;
    }

    return 0;
}

static int
init_keyboard(ClientKeyboard *kbd,
              xcb_connection_t *conn,
              uint8_t first_xkb_event,
              int32_t device_id,
              struct xkb_context *ctx) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    int ret;

    kbd->conn = conn;
    kbd->first_xkb_event = first_xkb_event;
    kbd->ctx = ctx;
    kbd->keymap = NULL;
    kbd->state = NULL;
    kbd->device_id = device_id;

    ret = update_keymap(kbd);
    if (ret)
        goto err_out;

    ret = select_xkb_events_for_device(conn, device_id);
    if (ret)
        goto err_state;

    return 0;

    err_state:
    xkb_state_unref(kbd->state);
    xkb_keymap_unref(kbd->keymap);
    err_out:
    return -1;
}

void init_xkb(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    int ret;
    uint8_t first_xkb_event;
    int32_t core_kbd_device_id;
    struct xkb_context *ctx;

    setlocale(LC_ALL, "");

    ret = xkb_x11_setup_xkb_extension(app->connection,
                                      XKB_X11_MIN_MAJOR_XKB_VERSION,
                                      XKB_X11_MIN_MINOR_XKB_VERSION,
                                      XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
                                      NULL,
                                      NULL,
                                      &first_xkb_event,
                                      NULL);
    if (!ret) {
        fprintf(stderr, "Couldn't setup XKB extension\n");
        return;
    }

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        ret = -1;
        fprintf(stderr, "Couldn't create xkb context\n");
        return;
    }

    core_kbd_device_id = xkb_x11_get_core_keyboard_device_id(app->connection);
    if (core_kbd_device_id == -1) {
        ret = -1;
        fprintf(stderr, "Couldn't find core keyboard device\n");
        return;
    }

    client->keyboard = new ClientKeyboard();
    ret =
            init_keyboard(client->keyboard, app->connection, first_xkb_event, core_kbd_device_id, ctx);
    if (ret) {
        fprintf(stderr, "Couldn't initialize core keyboard device\n");
        return;
    }
}

static void
deinit_keyboard(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xkb_state_unref(client->keyboard->state);
    xkb_keymap_unref(client->keyboard->keymap);
    xkb_context_unref(client->keyboard->ctx);
    delete client->keyboard;
}

void process_xkb_event(xcb_generic_event_t *generic_event, ClientKeyboard *keyboard) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    union xkb_event {
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    } *event = (union xkb_event *) generic_event;

    if (event->any.deviceID != keyboard->device_id)
        return;

    switch (event->any.xkbType) {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY:
            if (event->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                update_keymap(keyboard);
            break;

        case XCB_XKB_MAP_NOTIFY:
            update_keymap(keyboard);
            break;

        case XCB_XKB_STATE_NOTIFY:
            xkb_state_update_mask(keyboard->state,
                                  event->state_notify.baseMods,
                                  event->state_notify.latchedMods,
                                  event->state_notify.lockedMods,
                                  event->state_notify.baseGroup,
                                  event->state_notify.latchedGroup,
                                  event->state_notify.lockedGroup);
            break;
    }
}

bool poll_descriptor(App *app, int file_descriptor, int events, void function(App *, int fd)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    assert(app != nullptr);

    PolledDescriptor polled = {file_descriptor, function};
    app->descriptors_being_polled.push_back(polled);

    epoll_event event = {};
    event.events = events;
    event.data.fd = file_descriptor;

    if (epoll_ctl(app->epoll_fd, EPOLL_CTL_ADD, file_descriptor, &event) != 0) {
        printf("Failed to add file descriptor: %d\n", file_descriptor);
        close(app->epoll_fd);
        return false;
    }

    return true;
}

static xcb_visualtype_t *
get_alpha_visualtype(xcb_screen_t *s) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(s);

    // iterate over the available visualtypes and return the first one with 32bit
    // depth
    for (; di.rem; xcb_depth_next(&di)) {
        if (di.data->depth == 32) {
            return xcb_depth_visuals_iterator(di.data).data;
        }
    }
    // we didn't find any visualtypes with 32bit depth
    return NULL;
}

static xcb_visualtype_t *
get_visualtype(xcb_screen_t *s) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_depth_iterator_t di = xcb_screen_allowed_depths_iterator(s);

    // iterate over the available visualtypes and return the first one with 32bit
    // depth
    for (; di.rem; xcb_depth_next(&di)) {
        if (di.data->depth == 24) {
            return xcb_depth_visuals_iterator(di.data).data;
        }
    }
    // we didn't find any visualtypes with 32bit depth
    return NULL;
}

void xcb_poll_wakeup(App *app, int fd);

void timeout_poll_wakeup(App *app, int fd) {
    bool keep_running = false;
    for (int timeout_index = 0; timeout_index < app->timeouts.size(); timeout_index++) {
        Timeout *timeout = app->timeouts[timeout_index];
        if (timeout->file_descriptor == fd) {
            timeout->function(app, timeout->client, timeout, timeout->user_data);
            if ((keep_running = timeout->keep_running))
                break;

            app->timeouts.erase(app->timeouts.begin() + timeout_index);
            close(timeout->file_descriptor);
            delete timeout;
            break;
        }
    }

    int BUFFER_SIZE = 400;
    char buffer[BUFFER_SIZE];
    read(fd, buffer, BUFFER_SIZE);

    if (keep_running)
        return;

    for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
        if (app->descriptors_being_polled[i].file_descriptor == fd) {
            app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
            return;
        }
    }
}

App::App() {
}

App *app_new() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    //    Display *pDisplay = XOpenDisplay(nullptr);

    int screen_number;
    xcb_connection_t *connection = xcb_connect(nullptr, &screen_number);
    if (xcb_connection_has_error(connection)) {
        return nullptr;
    }

    auto *app = new App;
    app->connection = connection;
    app->screen_number = screen_number;
    app->screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
    app->epoll_fd = epoll_create(40);
    app->argb_visualtype = get_alpha_visualtype(app->screen);
    app->root_visualtype = get_visualtype(app->screen);
    app->bounds.x = 0;
    app->bounds.y = 0;
    app->bounds.w = app->screen->width_in_pixels;
    app->bounds.h = app->screen->height_in_pixels;

    xcb_intern_atom_cookie_t *c = xcb_ewmh_init_atoms(app->connection, &app->ewmh);
    if (!xcb_ewmh_init_atoms_replies(&app->ewmh, c, NULL))
        return nullptr;

    xcb_flush(app->connection);

    poll_descriptor(app, xcb_get_file_descriptor(app->connection), EPOLLIN, xcb_poll_wakeup);

    auto atom_cookie = xcb_intern_atom(app->connection, 1, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(app->connection, atom_cookie, NULL);
    app->protocols_atom = reply->atom;
    free(reply);

    atom_cookie =
            xcb_intern_atom(app->connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
    reply = xcb_intern_atom_reply(app->connection, atom_cookie, NULL);
    app->delete_window_atom = reply->atom;
    free(reply);

    atom_cookie = xcb_intern_atom(app->connection, 0, strlen("_MOTIF_WM_HINTS"), "_MOTIF_WM_HINTS");
    reply = xcb_intern_atom_reply(app->connection, atom_cookie, NULL);
    app->MOTIF_WM_HINTS = reply->atom;
    free(reply);

    dpi_setup(app);

    return app;
}

void set_cursor(App *app, xcb_screen_t *screen, AppClient *client, const std::string &name, uint8_t backup) {
    if (client->cursor != -1) {
        xcb_free_cursor(app->connection, client->cursor);
    }
    if (client->ctx == nullptr && xcb_cursor_context_new(app->connection, screen, &client->ctx) < 0) {
        xcb_font_t font = xcb_generate_id(app->connection);
        xcb_open_font(app->connection, font, strlen("cursor"), "cursor");
        client->cursor = xcb_generate_id(app->connection);
        xcb_create_glyph_cursor(app->connection, client->cursor, font, font, backup,
                                (backup + 1), 0, 0,
                                0, 0xffff, 0xffff, 0xffff);

        const uint32_t values[] = {client->cursor};
        xcb_change_window_attributes(app->connection, client->window, XCB_CW_CURSOR,
                                     values);
        if (font != XCB_NONE)
            xcb_close_font(app->connection, font);
    } else {
        client->cursor = xcb_cursor_load_cursor(client->ctx, name.c_str());

        const uint32_t values[] = {client->cursor};
        xcb_change_window_attributes(app->connection, client->window, XCB_CW_CURSOR,
                                     values);
    }
}

xcb_screen_t *get_screen_from_screen_info(ScreenInformation *screen_info, App *app) {
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(app->connection));

    for (int count = 0; iter.rem; ++count, xcb_screen_next(&iter)) {
        xcb_screen_t *screen = iter.data;
        if (screen->root == screen_info->root_window) {
            return screen;
        }
    }
    return nullptr;
}

AppClient *
client_new(App *app, Settings settings, const std::string &name) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr) {
        printf("App * passed to client_new was nullptr so couldn't make the client\n");
        return nullptr;
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;

    ScreenInformation *primary_screen_info = nullptr;
    for (auto s: screens) {
        if (s->is_primary) primary_screen_info = s;
    }
    if (primary_screen_info == nullptr) {
        if (screens.empty()) {
            assert(primary_screen_info != nullptr);
        } else {
            primary_screen_info = screens[0];
        }
    }

    /* Create a window */
    xcb_window_t window = xcb_generate_id(app->connection);

    uint8_t depth = settings.window_transparent ? 32 : 24;
    xcb_visualtype_t *visual = xcb_aux_find_visual_by_attrs(
            screen,
            -1,
            depth
    );
    xcb_colormap_t colormap = xcb_generate_id(app->connection);
    xcb_create_colormap(
            app->connection,
            XCB_COLORMAP_ALLOC_NONE,
            colormap, primary_screen_info->root_window,
            visual->visual_id
    );

    const uint32_t values[] = {
            // XCB_CW_BACK_PIXEL
            settings.background,
            // XCB_CW_BORDER_PIXEL
            settings.background,
            // XCB_CW_OVERRIDE_REDIRECT
            settings.popup,
            // XCB_CW_EVENT_MASK
            XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_ENTER_WINDOW |
            XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
            XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE,
            // XCB_CW_COLORMAP
            colormap
    };

    xcb_create_window(app->connection,
                      depth,
                      window,
                      primary_screen_info->root_window,
                      settings.x, settings.y, settings.w, settings.h,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      visual->visual_id,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL
                      | XCB_CW_OVERRIDE_REDIRECT
                      | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                      values);

    // This is so that we don't flicker when resizing the window
    if (settings.window_transparent) {
        const uint32_t s[] = {XCB_BACK_PIXMAP_NONE};
        xcb_change_window_attributes(app->connection, window, XCB_CW_BACK_PIXMAP, s);
    }

    cairo_surface_t *client_cr_surface = cairo_xcb_surface_create(app->connection,
                                                                  window,
                                                                  visual,
                                                                  settings.w, settings.h);
    cairo_t *cr = cairo_create(client_cr_surface);
    cairo_surface_destroy(client_cr_surface);

    if (settings.sticky) {
        long every_desktop = 0xFFFFFFFF;
        xcb_atom_t atom = get_cached_atom(app, "_NET_WM_STATE_SKIP_PAGER");
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_APPEND,
                            window,
                            get_cached_atom(app, "_NET_WM_DESKTOP"),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            &every_desktop);
        atom = get_cached_atom(app, "_NET_WM_STATE");
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_APPEND,
                            window,
                            get_cached_atom(app, "_NET_WM_STATE_ABOVE"),
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &atom);
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_APPEND,
                            window,
                            get_cached_atom(app, "_NET_WM_STATE_STICKY"),
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &atom);
    }

    // This is so we don't show up on our own taskbar
    if (settings.skip_taskbar) {
        xcb_atom_t atom = get_cached_atom(app, "_NET_WM_STATE_SKIP_TASKBAR");
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_APPEND,
                            window,
                            get_cached_atom(app, "_NET_WM_STATE"),
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &atom);

        atom = get_cached_atom(app, "_NET_WM_STATE_SKIP_PAGER");
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_APPEND,
                            window,
                            get_cached_atom(app, "_NET_WM_STATE"),
                            XCB_ATOM_ATOM,
                            32,
                            1,
                            &atom);
    }

    if (settings.no_input_focus) {
        xcb_icccm_wm_hints_t hints;
        xcb_icccm_wm_hints_set_input(&hints, XCB_INPUT_FOCUS_NONE);
        xcb_icccm_set_wm_hints(app->connection, window, &hints);
    } else {
        xcb_icccm_wm_hints_t hints;
        xcb_icccm_wm_hints_set_input(&hints, XCB_INPUT_FOCUS_FOLLOW_KEYBOARD);
        xcb_icccm_set_wm_hints(app->connection, window, &hints);
    }

    if (settings.dock) {
        xcb_atom_t atom = get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DOCK");
        xcb_ewmh_set_wm_window_type(&app->ewmh, window, 1, &atom);
    } else {
        xcb_atom_t atom = get_cached_atom(app, "_NET_WM_WINDOW_TYPE_NORMAL");
        xcb_ewmh_set_wm_window_type(&app->ewmh, window, 1, &atom);
    }

    if (settings.keep_above) {
        xcb_atom_t atoms_state[2] = {get_cached_atom(app, "_NET_WM_STATE_ABOVE"),
                                     get_cached_atom(app, "_NET_WM_STATE_STAYS_ON_TOP")};
        xcb_ewmh_set_wm_state(&app->ewmh, window, 2, atoms_state);
    }

    // No decorations
    if (!settings.decorations) {
        long motif_wm_hints[5] = {2, 0, 0, 0, 0};
        xcb_change_property_checked(app->connection,
                                    XCB_PROP_MODE_REPLACE,
                                    window,
                                    app->MOTIF_WM_HINTS,
                                    app->MOTIF_WM_HINTS,
                                    32,
                                    5,
                                    (unsigned char *) motif_wm_hints);
    }

    if (settings.force_position) {
        xcb_size_hints_t sizeHints;
        sizeHints.flags = XCB_ICCCM_SIZE_HINT_US_POSITION;
        sizeHints.x = settings.x;
        sizeHints.y = settings.y;
        xcb_icccm_set_wm_normal_hints(app->connection, window, &sizeHints);
    }

    // This is so we get delete requests
    xcb_change_property_checked(app->connection,
                                XCB_PROP_MODE_REPLACE,
                                window,
                                app->protocols_atom,
                                XCB_ATOM_ATOM,
                                32,
                                1,
                                &app->delete_window_atom);

    long zero = 0;
    xcb_change_property_checked(app->connection,
                                XCB_PROP_MODE_REPLACE,
                                window,
                                get_cached_atom(app, "_KDE_NET_WM_BLUR_BEHIND_REGION"),
                                XCB_ATOM_CARDINAL,
                                32,
                                1,
                                &zero);

    // strut (a.k.a what part we want to reserve for ourselves)
    if (settings.reserve_side) {
        xcb_void_cookie_t strut = xcb_ewmh_set_wm_strut(&app->ewmh,
                                                        window,
                                                        settings.reserve_left,
                                                        settings.reserve_right,
                                                        settings.reserve_top,
                                                        settings.reserve_bottom);
    }

    // Set the WM_CLASS
    auto set_wm_class_cookie = xcb_icccm_set_wm_class_checked(app->connection, window,
                                                              name.length(), name.c_str());
    xcb_generic_error_t *error = xcb_request_check(app->connection, set_wm_class_cookie);
    if (error) {
        printf("Couldn't set the WM_CLASS property for client: %s\n", name.c_str());
    }

    AppClient *client = new AppClient();
    init_client(client);

    client->app = app;
    client->name = name;

    client->window = window;
    client->popup = settings.popup;

    client->root->wanted_bounds.w = FILL_SPACE;
    client->root->wanted_bounds.h = FILL_SPACE;

    client->bounds->x = settings.x;
    client->bounds->y = settings.y;
    client->bounds->w = settings.w;
    client->bounds->h = settings.h;
    client->colormap = colormap;
    client->cr = cr;

    uint8_t XC_left_ptr = 68; // from: https://tronche.com/gui/x/xlib/appendix/b/
    set_cursor(app, screen, client, "left_ptr", XC_left_ptr);

    client->window_supports_transparency = settings.window_transparent;

    app->device = cairo_device_reference(cairo_surface_get_device(cairo_get_target(client->cr)));

    init_xkb(app, client);

    app->clients.push_back(client);

    return client;
}

void init_client(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    client->window = 0;
    client->bounds = new Bounds();
    client->root = new Container();
    // These are -100 because we need a number that isn't in the windows bounds 0..width 0..height.
    client->mouse_initial_x = -100;
    client->mouse_initial_y = -100;
    client->mouse_current_x = -100;
    client->mouse_current_y = -100;
    client->window_supports_transparency = false;
    client->cr = nullptr;
    client->colormap = 0;
}

void destroy_client(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    delete client->bounds;
    delete client->root;
    cairo_destroy(client->cr);
    xcb_free_colormap(app->connection, client->colormap);
    xcb_cursor_context_free(client->ctx);
    deinit_keyboard(app, client);
}

AppClient *
client_by_name(App *app, const std::string &target_name) {
    for (AppClient *possible_client : app->clients)
        if (target_name == possible_client->name)
            return possible_client;

    return nullptr;
}

AppClient *
client_by_window(App *app, xcb_window_t target_window) {
    for (AppClient *possible_client : app->clients)
        if (target_window == possible_client->window)
            return possible_client;

    return nullptr;
}

bool valid_client(App *app, AppClient *target_client) {
    if (target_client == nullptr)
        return false;

    for (AppClient *client : app->clients)
        if (target_client == client)
            return true;

    return false;
}

void client_add_handler(App *app,
                        AppClient *client_entity,
                        bool (*event_handler)(App *app, xcb_generic_event_t *)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Handler *handler = new Handler;
    handler->target_window = client_entity->window;
    handler->event_handler = event_handler;
    app->handlers.push_back(handler);
}

void client_show(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;

    client_layout(app, client);

    xcb_map_window(app->connection, client->window);
    xcb_flush(app->connection);

    client_paint(app, client, true);
}

void client_hide(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;

    xcb_unmap_window(app->connection, client->window);
    xcb_flush(app->connection);
}

int desktops_current(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    uint32_t current_desktop;
    const xcb_get_property_cookie_t &cookie =
            xcb_ewmh_get_current_desktop(&app->ewmh, app->screen_number);
    xcb_ewmh_get_current_desktop_reply(&app->ewmh, cookie, &current_desktop, nullptr);
    return current_desktop;
}

int desktops_count(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    uint32_t number_of_desktops;
    const xcb_get_property_cookie_t &cookie =
            xcb_ewmh_get_number_of_desktops_unchecked(&app->ewmh, app->screen_number);
    xcb_ewmh_get_number_of_desktops_reply(&app->ewmh, cookie, &number_of_desktops, nullptr);
    return number_of_desktops;
}

void desktops_change(App *app, long desktop_index) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_ewmh_request_change_current_desktop(
            &app->ewmh, app->screen_number, desktop_index, XCB_CURRENT_TIME);
    xcb_flush(app->connection);
}

void request_refresh(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;

    auto *event = new xcb_expose_event_t;

    event->response_type = XCB_EXPOSE;
    event->window = client->window;

    xcb_send_event(app->connection, true, event->window, XCB_EVENT_MASK_EXPOSURE, (char *) event);
    xcb_flush(app->connection);

    delete event;
}

void client_animation_paint(App *app, AppClient *client, Timeout *, void *user_data);

void client_register_animation(App *app, AppClient *client) {
    assert(app != nullptr && app->running);
    assert(client != nullptr);
    if (client->animations_running == 0) {
        float fps = client->fps;
        if (fps != 0)
            fps = 1000 / fps;
        app_timeout_create(app, client, fps, client_animation_paint, nullptr);
    }
    client->animations_running++;
}

void client_unregister_animation(App *app, AppClient *client) {
    assert(app != nullptr && app->running);
    assert(client != nullptr);
    client->animations_running--;
    // TODO: why can this even occur in the first place
    assert(client->animations_running >= 0);
}

void client_close(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;

    if (client->when_closed) {
        client->when_closed(client);
    }

    if (client->popup) {
        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
        xcb_flush(app->connection);
        app->grab_window = -1;
    }

    // TODO: this will crash if there is more than one handler per window I think
    for (int i = 0; i < app->handlers.size(); i++) {
        if (app->handlers[i]->target_window == client->window) {
            delete (app->handlers[i]);
            app->handlers.erase(app->handlers.begin() + i);
        }
    }

    app->timeouts.erase(std::remove_if(app->timeouts.begin(),
                                       app->timeouts.end(),
                                       [client](Timeout *timeout) {
                                           auto *timeout_client = (AppClient *) timeout->client;
                                           if (timeout_client == client) {
                                               close(timeout->file_descriptor);
                                               delete timeout;
                                           }
                                           return timeout_client == client;
                                       }), app->timeouts.end());

    client->animations.clear();
    client->animations.shrink_to_fit();

    xcb_unmap_window(app->connection, client->window);
    xcb_destroy_window(app->connection, client->window);
    xcb_flush(app->connection);

    for (int i = 0; i < app->clients.size(); i++) {
        if (app->clients[i] == client) {
            app->clients.erase(app->clients.begin() + i);
        }
    }

    destroy_client(app, client);

    if (app->clients.empty()) {
        app->running = false;
    }

    delete client;
}

void client_close_threaded(App *app, AppClient *client) {
    if (client) {
        client->marked_to_close = true;
    }
}

void client_replace_root(App *app, AppClient *client, Container *new_root) {
    if (valid_client(app, client)) {
        delete client->root;
        client->root = new_root;
    }
}

void client_layout(App *app, AppClient *client) {
    if (valid_client(app, client)) {
        Bounds copy = *client->bounds;
        copy.x = 0;
        copy.y = 0;
        layout(client, client->cr, client->root, copy);
    }
}

void paint_container(App *app, AppClient *client, Container *container) {
    if (container == nullptr || !container->exists) {
        return;
    }

    if (valid_client(app, client)) {
        if (container->when_paint && client->cr) {
            container->when_paint(client, client->cr, container);
        }

        if (!container->automatically_paint_children) {
            return;
        }

        // TODO: check if any childrens z_index is non zero first as an optimization
        // so we don't have to sort if we don't need it Only sort render_order
        // indexes by z_index rather than the actual children list themselves
        std::vector<int> render_order;
        for (int i = 0; i < container->children.size(); i++) {
            render_order.push_back(i);
        }
        std::sort(render_order.begin(), render_order.end(), [container](int a, int b) -> bool {
            return container->children[a]->z_index < container->children[b]->z_index;
        });

        if (container->clip_children) {
            for (auto index : render_order) {
                if (overlaps(container->children[index]->real_bounds, container->real_bounds)) {
                    if (container->clip) {
                        cairo_save(client->cr);
                        set_rect(client->cr, container->real_bounds);
                        cairo_clip(client->cr);
                    }
                    paint_container(app, client, container->children[index]);
                    if (container->clip) {
                        cairo_restore(client->cr);
                    }
                }
            }
        } else {
            for (auto index : render_order) {
                if (container->clip) {
                    cairo_save(client->cr);
                    set_rect(client->cr, container->real_bounds);
                    cairo_clip(client->cr);
                }
                paint_container(app, client, container->children[index]);
                if (container->clip) {
                    cairo_restore(client->cr);
                }
            }
        }
    }
}

// TODO: double buffering not really working
void client_paint(App *app, AppClient *client, bool force_repaint) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (valid_client(app, client)) {
        if (client->cr && client->root) {
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("paint");
#endif
                cairo_save(client->cr);
                cairo_push_group(client->cr);

                paint_container(app, client, client->root);

                cairo_pop_group_to_source(client->cr);
                cairo_set_operator(client->cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(client->cr);
                cairo_restore(client->cr);
            }

            {
#ifdef TRACY_ENABLE
                ZoneScopedN("flush");
#endif
                // TODO: Crucial!!!
                xcb_flush(app->connection);
            }
        }
    }
}

void client_paint(App *app, AppClient *client) {
    client_paint(app, client, false);
}

Container *
hovered_container(App *app, Container *root, int x, int y) {
    if (root == nullptr)
        return nullptr;

    for (Container *child : root->children) {
        if (child) {
            if (auto real = hovered_container(app, child, x, y)) {
                return real;
            }
        }
    }

    bool also_in_parent =
            root->parent == nullptr ? true : bounds_contains(root->parent->real_bounds, x, y);
    if (root->interactable && bounds_contains(root->real_bounds, x, y) && also_in_parent &&
        root->exists) {
        return root;
    }

    return nullptr;
}

static xcb_generic_event_t *event;

void fill_list_with_concerned(std::vector<Container *> &containers, Container *parent) {
    for (auto child : parent->children) {
        fill_list_with_concerned(containers, child);
    }

    if (parent->state.concerned) {
        containers.push_back(parent);
    }
}

std::vector<Container *>
concerned_containers(App *app, AppClient *client) {
    std::vector<Container *> containers;

    fill_list_with_concerned(containers, client->root);

    return containers;
}

void fill_list_with_pierced(std::vector<Container *> &containers, Container *parent, int x, int y) {
    for (auto child : parent->children) {
        if (child->interactable) {
            fill_list_with_pierced(containers, child, x, y);
        }
    }

    if (parent->exists) {
        if (parent->handles_pierced) {
            if (parent->handles_pierced(parent, x, y)) {
                containers.push_back(parent);
            }
        } else if (bounds_contains(parent->real_bounds, x, y)) {
            containers.push_back(parent);
        }
    }
}

// Should return the list of containers directly underneath the x and y with
// deepest children first in the list
std::vector<Container *>
pierced_containers(App *app, AppClient *client, int x, int y) {
    std::vector<Container *> containers;

    fill_list_with_pierced(containers, client->root, x, y);

    return containers;
}

bool is_pierced(Container *c, std::vector<Container *> &pierced) {
    for (auto container : pierced) {
        if (container == c) {
            return true;
        }
    }
    return false;
}

void handle_mouse_motion(App *app, AppClient *client, int x, int y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!valid_client(app, client)) {
        return;
    }
    client->mouse_current_x = x;
    client->mouse_current_y = y;
    std::vector<Container *> pierced = pierced_containers(app, client, x, y);
    std::vector<Container *> concerned = concerned_containers(app, client);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // mouse_motion can be the catalyst for sending out
    // when_mouse_enters_container, when_mouse_motion,
    // when_mouse_leaves_container, when_drag_start, and when_drag

    // If container in concerned but not in pierced means
    // when_mouse_leaves_container needs to be called and mouse_hovering needs to
    // be set to false and if the container is not doing anything else like being
    // dragged or pressed then concerned can be set to false
    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];

        bool in_pierced = false;
        for (int j = 0; j < pierced.size(); j++) {
            auto p = pierced[j];
            if (p == c) {
                in_pierced = true;
                break;
            }
        }

        if (c->state.mouse_pressing || c->state.mouse_dragging) {
            if (c->state.mouse_dragging) {
                // handle when_drag
                if (c->when_drag) {
                    c->when_drag(client, client->cr, c);
                }
            } else if (c->state.mouse_pressing) {
                // handle when_drag_start
                c->state.mouse_dragging = true;
                if (c->when_drag_start) {
                    c->when_drag_start(client, client->cr, c);
                }
            }
        } else if (in_pierced) {
            // handle when_mouse_motion
            if (c->when_mouse_motion) {
                c->when_mouse_motion(client, client->cr, c);
            }
        } else {
            // handle when_mouse_leaves_container
            c->state.mouse_hovering = false;
            if (c->when_mouse_leaves_container) {
                c->when_mouse_leaves_container(client, client->cr, c);
            }
            c->state.reset();
        }
    }

    bool a_concerned_container_mouse_pressed = false;
    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];

        if (c->state.mouse_pressing || c->state.mouse_dragging)
            a_concerned_container_mouse_pressed = true;
    }

    for (int i = 0; i < pierced.size(); i++) {
        auto p = pierced[i];

        if (a_concerned_container_mouse_pressed && !p->state.concerned)
            continue;

        if (i != 0 && (!p->receive_events_even_if_obstructed_by_one &&
                       !p->receive_events_even_if_obstructed)) {
            continue;
        }
        if (i != 0) {
            if (p->receive_events_even_if_obstructed) {

            } else if (p->receive_events_even_if_obstructed_by_one && p != pierced[0]->parent) {
                continue;
            }
        }

        if (p->state.concerned)
            continue;

        // handle when_mouse_enters_container
        p->state.concerned = true;
        p->state.mouse_hovering = true;
        if (p->when_mouse_enters_container) {
            p->when_mouse_enters_container(client, client->cr, p);
        }
    }
}

void handle_mouse_motion(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_motion_notify_event_t *) (event);
    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return;

    handle_mouse_motion(app, client, e->event_x, e->event_y);
}

void set_active(AppClient *client, const std::vector<Container *> &active_containers, Container *c, bool state) {
    for (auto child : c->children) {
        set_active(client, active_containers, child, state);
    }

    bool will_be_activated = false;
    for (auto active_container : active_containers) {
        if (active_container == c) {
            will_be_activated = true;
        }
    }
    if (will_be_activated) {
        if (!c->active) {
            c->active = true;
            if (c->when_active_status_changed) {
                c->when_active_status_changed(client, client->cr, c);
            }
        }
    } else {
        if (c->active) {
            c->active = false;
            if (c->when_active_status_changed) {
                c->when_active_status_changed(client, client->cr, c);
            }
        }
    }
}

void handle_mouse_button_press(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_button_press_event_t *) (event);
    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return;

    client->mouse_initial_x = e->event_x;
    client->mouse_initial_y = e->event_y;
    client->mouse_current_x = e->event_x;
    client->mouse_current_y = e->event_y;
    std::vector<Container *> pierced = pierced_containers(app, client, e->event_x, e->event_y);
    std::vector<Container *> concerned = concerned_containers(app, client);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // handle_mouse_button can be the catalyst for sending out when_mouse_down and
    // when_scrolled

    std::vector<Container *> mouse_downed;

    for (int i = 0; i < pierced.size(); i++) {
        auto p = pierced[i];

        // pierced[0] will always be the top or "real" container we are actually
        // clicking on therefore everyone else in the list needs to set
        // receive_events_even_if_obstructed to true to receive events
        if (i != 0 && (!p->receive_events_even_if_obstructed_by_one &&
                       !p->receive_events_even_if_obstructed)) {
            continue;
        }
        if (i != 0) {
            if (p->receive_events_even_if_obstructed) {

            } else if (p->receive_events_even_if_obstructed_by_one && p != pierced[0]->parent) {
                continue;
            }
        }

        p->state.concerned = true;// Make sure this container is concerned

        // Check if its a scroll event and call when_scrolled if so
        if (e->detail >= 4 && e->detail <= 7) {
            if (p->when_scrolled) {
                if (e->detail == 4 || e->detail == 5) {
                    p->when_scrolled(client, client->cr, p, 0, e->detail == 4 ? 1 : -1);
                } else {
                    p->when_scrolled(client, client->cr, p, e->detail == 6 ? 1 : -1, 0);
                }
            }
            handle_mouse_motion(app, client, e->event_x, e->event_y);
            continue;
        }

        if (e->detail != XCB_BUTTON_INDEX_1 && e->detail != XCB_BUTTON_INDEX_2 &&
            e->detail != XCB_BUTTON_INDEX_3) {
            continue;
        }

        // Update state and call when_mouse_down
        p->state.mouse_hovering =
                true;// If this container is pressed then clearly we are hovered as well
        p->state.mouse_pressing = true;
        p->state.mouse_button_pressed = e->detail;

        if (p->when_mouse_down) {
            mouse_downed.push_back(p);
            p->when_mouse_down(client, client->cr, p);
        }
    }
    set_active(client, mouse_downed, client->root, false);
}

bool handle_mouse_button_release(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_button_release_event_t *) (event);

    if (e->detail != XCB_BUTTON_INDEX_1 && e->detail != XCB_BUTTON_INDEX_2 &&
        e->detail != XCB_BUTTON_INDEX_3) {
        return true;
    }

    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return false;

    client->mouse_current_x = e->event_x;
    client->mouse_current_y = e->event_y;
    std::vector<Container *> concerned = concerned_containers(app, client);
    std::vector<Container *> pierced = pierced_containers(app, client, e->event_x, e->event_y);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // handle_mouse_button_release can be the catalyst for sending out
    // when_mouse_up, when_drag_end, and_when_clicked

    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];
        bool p = is_pierced(c, pierced);

        if (c->when_mouse_leaves_container && !p) {
            c->when_mouse_leaves_container(client, client->cr, c);
        }

        if (c->when_drag_end) {
            if (c->state.mouse_dragging) {
                c->when_drag_end(client, client->cr, c);
            }
        }

        if (c->when_clicked) {
            if (c->when_drag_end_is_click && c->state.mouse_dragging && p) {
                c->when_clicked(client, client->cr, c);
            } else if (!c->state.mouse_dragging) {
                c->when_clicked(client, client->cr, c);
            }
        }

        c->state.mouse_pressing = false;
        c->state.mouse_dragging = false;
        c->state.mouse_hovering = p;
        c->state.concerned = p;
    }

    handle_mouse_motion(app, client, e->event_x, e->event_y);

    return false;
}

void handle_mouse_enter_notify(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_enter_notify_event_t *) (event);
    if (e->mode != XCB_NOTIFY_MODE_NORMAL)// clicks generate leave and enter
        // notifies when you're grabbing wtf xlib
        return;

    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return;

    handle_mouse_motion(app, client, e->event_x, e->event_y);
}

void handle_mouse_leave_notify(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_leave_notify_event_t *) (event);
    if (e->mode != XCB_NOTIFY_MODE_NORMAL)// clicks generate leave and enter
        // notifies when you're grabbing wtf xlib
        return;

    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return;

    client->mouse_current_x = -1;
    client->mouse_current_y = -1;
    std::vector<Container *> concerned = concerned_containers(app, client);

    // pierced   are ALL the containers under the mouse
    // concerned are all the containers which have concerned state on
    // handle_mouse_leave_notify can be the catalyst for sending out
    // when_mouse_leaves_container
    for (int i = 0; i < concerned.size(); i++) {
        auto c = concerned[i];

        if (!c->state.mouse_pressing) {
            c->state.reset();
            if (c->when_mouse_leaves_container) {
                c->when_mouse_leaves_container(client, client->cr, c);
            }
        }
    }
}

void handle_configure_notify(App *app, AppClient *client, double x, double y, double w, double h) {
    client->bounds->w = w;
    client->bounds->h = h;
    client->bounds->x = x;
    client->bounds->y = y;

    cairo_xcb_surface_set_size(cairo_get_target(client->cr), client->bounds->w, client->bounds->h);

    client_layout(app, client);
}

void handle_configure_notify(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_configure_notify_event_t *) event;
    auto client = client_by_window(app, e->window);
    if (!valid_client(app, client))
        return;

    handle_configure_notify(app, client, e->x, e->y, e->width, e->height);
}

static bool is_control(char *buf) {
    return (buf[0] >= 0 && buf[0] < 0x20) || buf[0] == 0x7f;
}

void
send_key_actual(App *app, AppClient *client, Container *container, bool is_string, xkb_keysym_t keysym, char string[64],
                uint16_t mods, xkb_key_direction direction) {
    for (auto c : container->children) {
        send_key_actual(app, client, c, is_string, keysym, string, mods, direction);
    }
    if (container->when_key_event) {
        container->when_key_event(client, client->cr, container, is_string, keysym, string, mods, direction);
    }
}

static void
send_key(App *app, AppClient *client, Container *container) {
    xkb_state *state = client->keyboard->state;
    xkb_key_direction direction = event->response_type == XCB_KEY_PRESS ? XKB_KEY_DOWN : XKB_KEY_UP;
    if (direction == XKB_KEY_UP) {
        if (client->keyboard->balance == 0) {
            return;
        } else {
            client->keyboard->balance--;
        }
    } else {
        client->keyboard->balance++;
    }

    auto *e = (xcb_key_press_event_t *) event;
    xkb_keycode_t keycode = e->detail;

    xkb_keysym_t keysym;
    char key_character[64];

    // Record the change
    xkb_state_update_key(state, keycode, direction);

    // Get information
    keysym = xkb_state_key_get_one_sym(state, keycode);

    // Get the current character string
    xkb_state_key_get_utf8(state, keycode, key_character, sizeof(key_character));

    if (is_control(key_character)) {
        send_key_actual(app, client, container, false, keysym, key_character, e->state, direction);
    } else {
        send_key_actual(app, client, container, true, keysym, key_character, e->state, direction);
    }
}

void handle_xcb_event(App *app, xcb_window_t window_number, xcb_generic_event_t *event) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    int event_type = XCB_EVENT_RESPONSE_TYPE(event);

#ifdef TRACY_ENABLE
    const char *string = xcb_event_get_label(event_type);
    std::string info = "Window: " + std::to_string(window_number) + " EventType: ";
    if (string) {
        info += string;
    } else {
        info += "null";
    }
    ZoneText(info.c_str(), info.size());
#endif

    switch (event_type) {
        case XCB_EXPOSE: {
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            handle_configure_notify(app);
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            auto *e = (xcb_property_notify_event_t *) event;
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            auto *e = (xcb_client_message_event_t *) event;
            auto client = client_by_window(app, e->window);
            if (!valid_client(app, client))
                return;

            if (e->data.data32[0] == app->delete_window_atom) {
                client_close_threaded(app, client);
            }
            return;
            break;
        }
        case XCB_MOTION_NOTIFY: {
            handle_mouse_motion(app);
            break;
        }
        case XCB_BUTTON_PRESS: {
            handle_mouse_button_press(app);
            break;
        }
        case XCB_BUTTON_RELEASE: {
            bool was_scroll_event = handle_mouse_button_release(app);
            if (was_scroll_event) {
                return;
            }
            break;
        }
        case XCB_LEAVE_NOTIFY: {
            auto *e = (xcb_leave_notify_event_t *) event;
            if (e->mode != 0) {
                return;
            }

            handle_mouse_leave_notify(app);
            break;
        }
        case XCB_ENTER_NOTIFY: {
            auto *e = (xcb_enter_notify_event_t *) event;
            if (e->mode != 0) {
                return;
            }

            handle_mouse_enter_notify(app);
            break;
        }
        case XCB_KEY_PRESS: {
            auto client = client_by_window(app, window_number);
            if (!valid_client(app, client))
                return;

            send_key(app, client, client->root);
            break;
        }

        case XCB_FOCUS_OUT: {
            break;
        }
        case XCB_KEY_RELEASE: {
            auto client = client_by_window(app, window_number);
            if (!valid_client(app, client))
                return;

            send_key(app, client, client->root);
            break;
        }
        default: {
            for (auto *client : app->clients) {
                if (client->keyboard) {
                    if (event->response_type == client->keyboard->first_xkb_event) {
                        process_xkb_event(event, client->keyboard);
                    }
                }
            }

            break;
        }
    }

    client_paint(app, client_by_window(app, window_number), true);
}

void handle_xcb_event(App *app) {
    assert(app != nullptr && app->running);
    // TODO: this is literally only here because the meta watcher is another thread and needs to be able to sync back up
    //  we should try to get rid of this mutex
    std::lock_guard lock(app->clients_mutex);

    while ((event = xcb_poll_for_event(app->connection)) != nullptr) {
        if (auto window = get_window(event)) {
            bool event_consumed_by_custom_handler = false;
            for (auto handler : app->handlers) {
                // If the handler's target window is INT_MAX that means it wants to see every event
                if (handler->target_window == INT_MAX) {
                    if (handler->event_handler(app, event)) {
                        // TODO: is this supposed to be called twice? I doubt it
//                        handler->event_handler(app, event);
                        event_consumed_by_custom_handler = true;
                    }
                } else if (handler->target_window ==
                           window) { // If the target window and the window of this event matches, then send the handler the
                    if (handler->event_handler(app, event)) {
                        event_consumed_by_custom_handler = true;
                    }
                }
            }
            if (event_consumed_by_custom_handler) {

            } else {
                if (auto client = client_by_window(app, window)) {
                    handle_xcb_event(app, client->window, event);
                } else {
                    // An event from a window for which is not a client
                }
            }
        } else {
            for (auto handler : app->handlers) {
                // If the handler's target window is INT_MAX that means it wants to see every event
                if (handler->target_window == INT_MAX) {
                    if (handler->event_handler(app, event)) {
                        printf("here\n");
                    }
                }
            }
        }

        free(event);
    }
}

void xcb_poll_wakeup(App *app, int fd) {
    xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
    xcb_flush(app->connection);
    handle_xcb_event(app);
    xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
    xcb_flush(app->connection);
}

#include <ctime>

// call this function to start a nanosecond-resolution timer
struct timespec
timer_start() {
    struct timespec start_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
    return start_time;
}

// call this function to end a timer, returning nanoseconds elapsed as a long
long timer_end(struct timespec start_time) {
    struct timespec end_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);
    long diffInNanos =
            (end_time.tv_sec - start_time.tv_sec) * (long) 1e9 + (end_time.tv_nsec - start_time.tv_nsec);
    return diffInNanos;
}

void app_main(App *app) {
    if (app == nullptr) {
        printf("App * passed to app_main was nullptr so couldn't run\n");
        assert(app != nullptr);
        return;
    }

    int MAX_POLLING_EVENTS_AT_THE_SAME_TIME = 40;
    epoll_event events[MAX_POLLING_EVENTS_AT_THE_SAME_TIME];

    app->running = true;
    while (app->running) {
        int event_count = epoll_wait(app->epoll_fd, events, MAX_POLLING_EVENTS_AT_THE_SAME_TIME, -1);

        for (int event_index = 0; event_index < event_count; event_index++) {
            for (auto polled : app->descriptors_being_polled) {
                if (events[event_index].data.fd == polled.file_descriptor) {
                    if (polled.function) {
                        polled.function(app, polled.file_descriptor);
                    }
                }
            }
        }

        // TODO: we can't delete while we iterate.
        for (AppClient *client : app->clients) {
            if (client->marked_to_close) {
                client_close(app, client);
                break;
            }
        }
    }
}

void app_clean(App *app) {
    for (AppClient *client : app->clients) {
        client_close(app, client);
    }

    for (auto c : app->clients) {
        delete c;
    }
    app->clients.clear();
    app->clients.shrink_to_fit();

    for (auto handler : app->handlers) {
        delete handler;
    }

    cleanup_cached_fonts();
    cleanup_cached_atoms();

    for (auto t : app->timeouts) {
        close(t->file_descriptor);
        delete t;
    }
    app->timeouts.clear();
    app->timeouts.shrink_to_fit();

    close(app->epoll_fd);

    if (app->device) {
        cairo_device_finish(app->device);
        cairo_device_destroy(app->device);
    }

    xcb_disconnect(app->connection);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)(AppClient *client),
                             bool relayout) {
    for (auto &animation : client->animations) {
        if (animation.value == value) {
            animation.length = length;
            animation.easing = easing;
            animation.target = target;
            animation.start_time = get_current_time_in_ms();
            animation.start_value = *value;
            animation.finished = finished;
            animation.relayout = relayout;
            return;
        }
    }

    ClientAnimation animation;
    animation.value = value;
    animation.length = length;
    animation.easing = easing;
    animation.target = target;
    animation.start_time = get_current_time_in_ms();
    animation.start_value = *value;
    animation.finished = finished;
    animation.relayout = relayout;
    client->animations.push_back(animation);

    client_register_animation(app, client);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target) {
    client_create_animation(app, client, value, length, easing, target, nullptr, false);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)(AppClient *client)) {
    client_create_animation(app, client, value, length, easing, target, finished, false);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             bool relayout) {
    client_create_animation(app, client, value, length, easing, target, nullptr, relayout);
}

bool app_timeout_stop(App *app,
                      AppClient *client,
                      int timeout_file_descriptor) {
    assert(app != nullptr && app->running);
    for (int timeout_index = 0; timeout_index < app->timeouts.size(); timeout_index++) {
        Timeout *timeout = app->timeouts[timeout_index];
        if (timeout->file_descriptor == timeout_file_descriptor) {
            app->timeouts.erase(app->timeouts.begin() + timeout_index);
            close(timeout->file_descriptor);
            delete timeout;
            return true;
        }
    }
    return false;
}

bool app_timeout_replace(App *app,
                         AppClient *client,
                         int timeout_file_descriptor, float timeout_ms,
                         void (*timeout_function)(App *, AppClient *, Timeout *, void *),
                         void *user_data) {
    assert(app != nullptr && app->running);
    assert(timeout_function != nullptr);
    for (auto timeout : app->timeouts) {
        if (timeout->file_descriptor == timeout_file_descriptor) {
            struct itimerspec time = {0};
            // The division done below converts the timeout_ms into seconds and nanoseconds
            time.it_interval.tv_sec = timeout_ms / 1000;
            time.it_interval.tv_nsec = std::fmod(timeout_ms, 1000) * 1000000;
            time.it_value.tv_sec = timeout_ms / 1000;
            time.it_value.tv_nsec = std::fmod(timeout_ms, 1000) * 1000000;

            // If the caller of this function passed in a zero,
            // they probably expected the function to execute as soon as possible,
            // but based on documentation, the timer will never go off if it_value.tv_sec and it_value.tv_nsec
            // are both set to zero.
            // To have the function behave more like what the caller probably expected,
            // we do what we do below and give the timer the smallest possible increment.
            if (timeout_ms == 0) {
                time.it_value.tv_nsec = 1;
            }

            if (timerfd_settime(timeout_file_descriptor, 0, &time, nullptr) != 0) {
                return false;
            }

            timeout->function = timeout_function;
            timeout->client = client;
            timeout->user_data = user_data;
            timeout->keep_running = false;

            return true;
        }
    }

    return false;
}

int
app_timeout_create(App *app, AppClient *client, float timeout_ms, void (*timeout_function)(App *, AppClient *, Timeout *, void *),
                   void *user_data) {
    assert(app != nullptr);
    if (!app->running)
        return -1;
    assert(timeout_function != nullptr);
    int timeout_file_descriptor = timerfd_create(CLOCK_REALTIME, 0);
    if (timeout_file_descriptor == -1) { // error with timerfd_create
        return -1;
    }

    bool success = poll_descriptor(app, timeout_file_descriptor, EPOLLIN, timeout_poll_wakeup);
    if (!success) { // error with poll_descriptor
        return -1;
    }
    auto timeout = new Timeout;
    timeout->function = timeout_function;
    timeout->client = client;
    timeout->file_descriptor = timeout_file_descriptor;
    timeout->user_data = user_data;
    timeout->keep_running = false;
    app->timeouts.emplace_back(timeout);

    struct itimerspec time = {0};
    // The division done below converts the timeout_ms into seconds and nanoseconds
    time.it_interval.tv_sec = timeout_ms / 1000;
    time.it_interval.tv_nsec = std::fmod(timeout_ms, 1000) * 1000000;
    time.it_value.tv_sec = timeout_ms / 1000;
    time.it_value.tv_nsec = std::fmod(timeout_ms, 1000) * 1000000;

    // If the caller of this function passed in a zero,
    // they probably expected the function to execute as soon as possible,
    // but based on documentation, the timer will never go off if it_value.tv_sec and it_value.tv_nsec
    // are both set to zero.
    // To have the function behave more like what the caller probably expected,
    // we do what we do below and give the timer the smallest possible increment.
    if (timeout_ms == 0) {
        time.it_value.tv_nsec = 1;
    }

    int settime_success = timerfd_settime(timeout_file_descriptor, 0, &time, nullptr);
    if (settime_success != 0) { // error with timerfd_settime
        return -1;
    }

    return timeout_file_descriptor;
}

void app_create_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *custom_event_handler = new Handler;
    custom_event_handler->event_handler = custom_handler;
    custom_event_handler->target_window = window;
    app->handlers.push_back(custom_event_handler);
}

void app_remove_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (int i = 0; i < app->handlers.size(); i++) {
        Handler *custom_event_handler = app->handlers[i];
        if (custom_event_handler->target_window == window && custom_event_handler->event_handler == custom_handler) {
            delete custom_event_handler;
            app->handlers.erase(app->handlers.begin() + i);
            return;
        }
    }
}

bool client_set_position(App *app, AppClient *client, int x, int y) {
    assert(app != nullptr && app->running);
    assert(client != nullptr);
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    uint32_t values[] = {
            (uint32_t) x,
            (uint32_t) y,
    };

    auto cookie_configure_window = xcb_configure_window_checked(app->connection, client->window, mask, values);
    return xcb_request_check(app->connection, cookie_configure_window);
}

bool client_set_size(App *app, AppClient *client, int w, int h) {
    assert(app != nullptr && app->running);
    assert(client != nullptr);
    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    uint32_t values[] = {
            (uint32_t) w,
            (uint32_t) h,
    };
    auto cookie_configure_window = xcb_configure_window_checked(app->connection, client->window, mask, values);
    return xcb_request_check(app->connection, cookie_configure_window);
}

bool client_set_position_and_size(App *app, AppClient *client, int x, int y, int w, int h) {
    bool reposition = client_set_position(app, client, x, y);
    bool resize = client_set_size(app, client, w, h);
    return reposition && resize;
}

void client_animation_paint(App *app, AppClient *client, Timeout *timeout, void *user_data) {
#ifdef TRACY_ENABLE
    FrameMarkStart("Animation Paint");
#endif
    assert(app != nullptr);
    assert(client != nullptr);

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("update animating values");
#endif
        long now = get_current_time_in_ms();

        bool wants_to_relayout = false;

        for (auto &animation : client->animations) {
            long elapsed_time = now - animation.start_time;
            double scalar = (double) elapsed_time / animation.length;
            animation.done = scalar >= 1;

            if (animation.easing != nullptr)
                scalar = animation.easing(scalar);

            double diff = (animation.target - animation.start_value) * scalar;
            *animation.value = animation.start_value + diff;

            if (animation.relayout)
                wants_to_relayout = true;

            if (animation.done) {
                *animation.value = animation.target;
                client_unregister_animation(app, client);
                if (animation.finished) {
                    animation.finished(client);
                }
            }
        }

        if (wants_to_relayout) {
            client_layout(app, client);
            handle_mouse_motion(app, client, client->mouse_current_x, client->mouse_current_y);
        }

        client->animations.erase(std::remove_if(client->animations.begin(),
                                                client->animations.end(),
                                                [](const ClientAnimation &data) {
                                                    return data.done;
                                                }), client->animations.end());
        client->animations.shrink_to_fit();
    }

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("paint");
#endif
        client_paint(app, client, true);
    }

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("request another frame");
#endif
        timeout->keep_running = client->animations_running > 0;
    }

#ifdef TRACY_ENABLE
    FrameMarkEnd("Animation Paint");
#endif
}
