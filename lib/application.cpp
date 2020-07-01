
#include "application.h"

#ifdef TRACY_ENABLE
#include "../tracy/Tracy.hpp"
#endif

#include "utility.h"

#include <X11/keysym.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <unistd.h>
#include <xcb/xcb_event.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#define explicit dont_use_cxx_explicit

#include <xcb/xcb_keysyms.h>
#include <xcb/xkb.h>

#undef explicit

static int
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

    new_state = xkb_x11_state_new_from_device(new_keymap, kbd->conn, kbd->device_id);
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
deinit_keyboard(App *app, AppClient *client, ClientKeyboard *keyboard) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xkb_state_unref(keyboard->state);
    xkb_keymap_unref(keyboard->keymap);
    client->keyboard = nullptr;
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

bool poll_descriptor(App *app, int real_file_descriptor, int target_file_descriptor) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    epoll_event event = {};
    event.events = EPOLLIN;
    event.data.fd = target_file_descriptor;

    if (epoll_ctl(app->epoll_fd, EPOLL_CTL_ADD, real_file_descriptor, &event)) {
        printf("failed to add file descriptor\n");
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
    app->xcb_fd = xcb_get_file_descriptor(app->connection);
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

    // poll_descriptor(app, app->xcb_fd, app->xcb_fd + 1);

    if (pipe(app->refresh_pipes) == -1) {
        std::cout << "Couldn't create file descriptor pipes for use to refresh clients"
                  << std::endl;
        exit(EXIT_FAILURE);
    }
    poll_descriptor(app, app->refresh_pipes[0], app->refresh_pipes[0]);

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

    return app;
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

    uint8_t depth = 24;
    xcb_visualtype_t *visual_type = app->root_visualtype;
    xcb_visualid_t visual_id = visual_type->visual_id;
    if (settings.window_transparent) {
        visual_type = app->argb_visualtype;
        depth = 32;
        visual_id = visual_type->visual_id;
    }

    xcb_window_t colormap = xcb_generate_id(app->connection);
    xcb_create_colormap(
            app->connection, XCB_COLORMAP_ALLOC_NONE, colormap, app->screen->root, visual_id);

    xcb_window_t window = xcb_generate_id(app->connection);

    if (settings.popup) {
        const uint32_t vals[] = {settings.background,
                                 settings.background,
                                 true,
                                 XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
                                 XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
                                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                 XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_ENTER_WINDOW |
                                 XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                 XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
                                 XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                                 colormap};

        xcb_create_window(app->connection,
                          depth,
                          window,
                          app->screen->root,
                          settings.x,
                          settings.y,
                          settings.w,
                          settings.h,
                          XCB_COPY_FROM_PARENT,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          visual_id,
                          XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
                          XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
                          vals);
    } else {
        const uint32_t vals[] = {settings.background,
                                 settings.background,
                                 XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
                                 XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
                                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                 XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_ENTER_WINDOW |
                                 XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                 XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
                                 XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                                 colormap};

        xcb_create_window(app->connection,
                          depth,
                          window,
                          app->screen->root,
                          settings.x,
                          settings.y,
                          settings.w,
                          settings.h,
                          XCB_COPY_FROM_PARENT,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          visual_id,
                          XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK |
                          XCB_CW_COLORMAP,
                          vals);
    }
    xcb_free_colormap(app->connection, colormap);

    // This is so that we don't flicker when resizing the window
    const uint32_t s[] = {XCB_BACK_PIXMAP_NONE};
    xcb_change_window_attributes(app->connection, window, XCB_CW_BACK_PIXMAP, s);

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

    // strut (a.k.a what part we want to reserve for ourselves)
    if (settings.reserve_side) {
        xcb_void_cookie_t strut = xcb_ewmh_set_wm_strut(&app->ewmh,
                                                        window,
                                                        settings.reserve_left,
                                                        settings.reserve_right,
                                                        settings.reserve_top,
                                                        settings.reserve_bottom);
    }

    AppClient *client = new AppClient();
    init_client(client);

    memcpy(client->name, name.c_str(), 255);
    client->app = app;
    client->name[256] = '\0';

    client->window = window;
    client->popup = settings.popup;

    client->root->wanted_bounds.w = FILL_SPACE;
    client->root->wanted_bounds.h = FILL_SPACE;

    client->bounds->x = settings.x;
    client->bounds->y = settings.y;
    client->bounds->w = settings.w;
    client->bounds->h = settings.h;

    client->window_supports_transparency = settings.window_transparent;
    cairo_surface_t *client_cr_surface =
            cairo_xcb_surface_create(app->connection, window, visual_type, settings.w, settings.h);
    client->cr = cairo_create(client_cr_surface);
    cairo_surface_destroy(client_cr_surface);
    client->back_pixmap = xcb_generate_id(app->connection);
    xcb_create_pixmap(
            app->connection, depth, client->back_pixmap, app->screen->root, settings.w, settings.h);
    cairo_surface_t *client_back_cr_surface = cairo_xcb_surface_create(
            app->connection, client->back_pixmap, visual_type, settings.w, settings.h);
    client->back_cr = cairo_create(client_back_cr_surface);
    cairo_surface_destroy(client_back_cr_surface);

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
    client->mouse_initial_x = 0;
    client->mouse_initial_y = 0;
    client->mouse_current_x = 0;
    client->mouse_current_y = 0;
    client->animation_count = 0;
    client->window_supports_transparency = false;
    client->cr = nullptr;
    client->back_pixmap = 0;
    client->back_cr = 0;
}

void destroy_client(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    delete client->bounds;
    delete client->root;
    cairo_destroy(client->cr);
    cairo_destroy(client->back_cr);
    xcb_free_pixmap(app->connection, client->back_pixmap);
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
    if (app == nullptr || !valid_client(app, client))
        return;

    auto *event = new xcb_expose_event_t;

    event->response_type = XCB_EXPOSE;
    event->window = client->window;

    xcb_send_event(app->connection, true, event->window, XCB_EVENT_MASK_EXPOSURE, (char *) event);
    xcb_flush(app->connection);

    delete event;
}

int refresh_rate = 12;

void client_register_animation(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;
    std::string buf = "a";
    write(app->refresh_pipes[1], buf.c_str(), buf.size());
    client->animation_count += 1;
}

void client_unregister_animation(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || !valid_client(app, client))
        return;
    client->animation_count -= 1;
    if (client->animation_count < 0) {
        client->animation_count = 0;
    }
    char buf[100];
    int length = read(app->refresh_pipes[0], buf, 1);
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

    // TODO: this will crash if there is more than one handler per window I think
    for (int i = 0; i < app->handlers.size(); i++) {
        if (app->handlers[i]->target_window == client->window) {
            delete (app->handlers[i]);
            app->handlers.erase(app->handlers.begin() + i);
        }
    }

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
        layout(client->root, copy);
    }
}

void paint_container(App *app, AppClient *client, Container *container) {
    if (container == nullptr || !container->exists) {
        return;
    }

    if (valid_client(app, client)) {
        if (container->when_paint && client->back_cr) {
            container->when_paint(client, client->back_cr, container);
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
                    paint_container(app, client, container->children[index]);
                }
            }
        } else {
            for (auto index : render_order) {
                paint_container(app, client, container->children[index]);
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
        if (client->back_cr && client->root) {
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("clear");
#endif
                cairo_set_operator(client->back_cr, CAIRO_OPERATOR_CLEAR);
                cairo_paint(client->back_cr);
                cairo_set_operator(client->back_cr, CAIRO_OPERATOR_OVER);
            }
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("paint");
#endif
                paint_container(app, client, client->root);
            }
            {
#ifdef TRACY_ENABLE
                ZoneScopedN("swap");
#endif
                cairo_set_operator(client->cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_surface(client->cr, cairo_get_target(client->back_cr), 0, 0);
                cairo_paint(client->cr);
                cairo_surface_flush(cairo_get_target(client->back_cr));
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
        fill_list_with_pierced(containers, child, x, y);
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
                    c->when_drag(client, client->back_cr, c);
                }
            } else if (c->state.mouse_pressing) {
                // handle when_drag_start
                c->state.mouse_dragging = true;
                if (c->when_drag_start) {
                    c->when_drag_start(client, client->back_cr, c);
                }
            }
        } else if (in_pierced) {
            // handle when_mouse_motion
            if (c->when_mouse_motion) {
                c->when_mouse_motion(client, client->back_cr, c);
            }
        } else {
            // handle when_mouse_leaves_container
            c->state.mouse_hovering = false;
            if (c->when_mouse_leaves_container) {
                c->when_mouse_leaves_container(client, client->back_cr, c);
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
            p->when_mouse_enters_container(client, client->back_cr, p);
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

void set_active(Container *c, bool state) {
    for (auto child : c->children) {
        set_active(child, state);
    }

    c->active = state;
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
                    p->when_scrolled(client, client->back_cr, p, 0, e->detail == 4 ? 1 : -1);
                } else {
                    p->when_scrolled(client, client->back_cr, p, e->detail == 6 ? 1 : -1, 0);
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
            p->when_mouse_down(client, client->back_cr, p);
        }
    }
    // set_active(client->root, false);
    for (auto child : mouse_downed) {
        child->active = true;
    }
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
            c->when_mouse_leaves_container(client, client->back_cr, c);
        }

        if (c->when_drag_end) {
            if (c->state.mouse_dragging) {
                c->when_drag_end(client, client->back_cr, c);
            }
        }

        if (c->when_clicked) {
            if (c->when_drag_end_is_click && c->state.mouse_dragging && p) {
                c->when_clicked(client, client->back_cr, c);
            } else if (!c->state.mouse_dragging) {
                c->when_clicked(client, client->back_cr, c);
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
        // notifies wtf xlib
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
        // notifies wtf xlib
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
                c->when_mouse_leaves_container(client, client->back_cr, c);
            }
        }
    }
}

void handle_configure_notify(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_configure_notify_event_t *) event;
    auto client = client_by_window(app, e->window);
    if (!valid_client(app, client))
        return;

    auto geom_cookie = xcb_get_geometry(app->connection, client->window);
    auto geom_reply = xcb_get_geometry_reply(app->connection, geom_cookie, nullptr);
    auto trans_cookie = xcb_translate_coordinates(
            app->connection, client->window, app->screen->root, geom_reply->x, geom_reply->y);
    auto trans_reply = xcb_translate_coordinates_reply(app->connection, trans_cookie, nullptr);
    client->bounds->x = trans_reply->dst_x;
    client->bounds->y = trans_reply->dst_y;
    free(geom_reply);
    free(trans_reply);
    client->bounds->w = e->width;
    client->bounds->h = e->height;

    uint8_t depth = 24;
    xcb_visualtype_t *visual_type = app->root_visualtype;
    xcb_visualid_t visual_id = visual_type->visual_id;
    if (client->window_supports_transparency) {
        visual_type = app->argb_visualtype;
        depth = 32;
        visual_id = visual_type->visual_id;
    }

    cairo_xcb_surface_set_size(cairo_get_target(client->cr), client->bounds->w, client->bounds->h);

    cairo_destroy(client->back_cr);

    xcb_free_pixmap(app->connection, client->back_pixmap);

    client->back_pixmap = xcb_generate_id(app->connection);
    xcb_create_pixmap(app->connection,
                      depth,
                      client->back_pixmap,
                      app->screen->root,
                      client->bounds->w,
                      client->bounds->h);

    cairo_surface_t *back_cr_surface = cairo_xcb_surface_create(
            app->connection, client->back_pixmap, visual_type, client->bounds->w, client->bounds->h);
    client->back_cr = cairo_create(back_cr_surface);
    cairo_surface_destroy(back_cr_surface);

    client_layout(app, client);
}

static void
send_key(App *app, AppClient *client, Container *container) {
    for (auto child : container->children) {
        send_key(app, client, child);
    }

    if (container->when_key_release) {
        container->when_key_release(client, client->back_cr, container, event);
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
    bool should_handle_event = true;
    xcb_window_t window_number = get_window(event);

    for (auto handler : app->handlers) {
        if (handler->target_window == INT_MAX) {
            if (!handler->event_handler(app, event)) {
                should_handle_event = false;
            }
        } else {
            if (window_number == 0) {
                if (!handler->event_handler(app, event)) {
                    should_handle_event = false;
                }
            } else if (handler->target_window == window_number) {
                if (!handler->event_handler(app, event)) {
                    should_handle_event = false;
                }
            }
        }
    }

    if (!should_handle_event) {
        return;
    }

    handle_xcb_event(app, window_number, event);
}

struct animation_data {
    App *app;
    AppClient *client;
    double start_value;
    double *value;
    double length;
    double target;
    easingFunction easing;
    long start_time;
    bool relayout;

    void (*finished)();
};

static std::vector<animation_data *> animations_list;

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

void update_animations(App *app) {
    int start_empty = animations_list.empty();

    std::vector<AppClient *> to_layout;

    for (animation_data *animation : animations_list) {
        std::vector<animation_data *> destroy;

        for (animation_data *data : animations_list) {
            long now = get_current_time_in_ms();
            long elapsed_time = now - data->start_time;
            double scalar = (double) elapsed_time / data->length;

            if (data->easing != nullptr) {
                scalar = data->easing(scalar);
            }

            if (scalar >= 1) {
                scalar = 1;
            }

            double diff = (data->target - data->start_value) * scalar;

            *data->value = data->start_value + diff;

            if (scalar == 1) {
                destroy.push_back(data);
            }
        }

        // TODO: this is slow, since we will layout a client as many animations as it has on it, instead of just doing it once at the end
        if (animation->relayout) {
            to_layout.push_back(animation->client);
        }

        for (animation_data *to_destroy : destroy) {
            *to_destroy->value = to_destroy->target;
            if (to_destroy->finished) {
                to_destroy->finished();
            }
            client_unregister_animation(app, to_destroy->client);

            if (to_destroy->relayout) {
                client_layout(to_destroy->client->app, to_destroy->client);
            }
            client_paint(app, to_destroy->client, true);

            for (int i = 0; i < animations_list.size(); i++) {
                if (animations_list[i] == to_destroy) {
                    animations_list.erase(animations_list.begin() + i);
                    break;
                }
            }
        }
    }

    for (AppClient *client : to_layout) {
        client_layout(app, client);
    }

    for (AppClient *client : app->clients) {
        if (client->animation_count != 0) {
            client_paint(app, client, true);
        }
    }
}

void render_loop(App *app) {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Render Thread");
#endif
    int MAX_POLLING_EVENTS_AT_THE_SAME_TIME = 40;
    epoll_event events[MAX_POLLING_EVENTS_AT_THE_SAME_TIME];

    std::vector<xcb_window_t> windows;
    std::unique_lock guard(app->clients_mutex);
    guard.unlock();
    while (app->running) {
        epoll_wait(app->epoll_fd, events, MAX_POLLING_EVENTS_AT_THE_SAME_TIME, -1);

        guard.lock();
        auto start = timer_start();
        update_animations(app);
        auto end = timer_end(start);
        guard.unlock();

        auto ms = 1000 * refresh_rate;

        usleep(ms - (end / 1000));
    }
}

void app_main(App *app) {
    if (app == nullptr) {
        printf("App * passed to app_main was nullptr so couldn't run\n");
        return;
    }

    xcb_flush(app->connection);

    std::thread render_thread(render_loop, app);
    render_thread.detach();

    while (app->running) {
        xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
        xcb_flush(app->connection);

        event = xcb_wait_for_event(app->connection);
        xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
        xcb_flush(app->connection);

#ifdef TRACY_ENABLE
        FrameMarkStart("Xcb Frame");
#endif

        std::lock_guard guard(app->clients_mutex);

        handle_xcb_event(app);

        free(event);

        for (auto client : app->clients) {
            if (client->marked_to_close) {
                client_close(app, client);
            }
        }
#ifdef TRACY_ENABLE
        FrameMarkEnd("Xcb Frame");
#endif
    }
}

void app_clean(App *app) {
    for (AppClient *client : app->clients) {
        client_close(app, client);
    }

    app->clients.clear();

    for (auto handler : app->handlers) {
        delete handler;
    }

    cleanup_cached_fonts();
    cleanup_cached_atoms();

    xcb_disconnect(app->connection);
}

client_cairo_aspect::~client_cairo_aspect() {
    cairo_destroy(cr);
    cairo_destroy(back_cr);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)(),
                             bool relayout) {
    // TODO: fix this
    *value = target;
    if (relayout) {
        client_layout(app, client);
    }
    client_paint(app, client);
    return;

    bool break_out = false;

    for (animation_data *data : animations_list) {
        if (data->value == value) {
            data->app = app;
            data->client = client;
            data->value = value;
            data->length = length;
            data->easing = easing;
            data->target = target;
            data->start_time = get_current_time_in_ms();
            data->start_value = *value;
            data->finished = finished;
            data->relayout = relayout;

            break_out = true;
        }
    }

    if (break_out)
        return;

    auto data = new animation_data();
    data->app = app;
    data->client = client;
    data->value = value;
    data->length = length;
    data->easing = easing;
    data->target = target;
    data->start_time = get_current_time_in_ms();
    data->start_value = *value;
    data->finished = finished;
    data->relayout = relayout;
    animations_list.push_back(data);

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
                             void (*finished)()) {
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
