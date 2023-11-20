
#include "application.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include "utility.h"
#include "dpi.h"
#include "defer.h"
#include "../src/config.h"
#include "../src/root.h"
#include "audio.h"

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
#include <xcb/xinput.h>
#include <xcb/xcb.h>
#include <poll.h>

#define explicit dont_use_cxx_explicit

#include <xcb/xkb.h>
#include <xcb/xcb_aux.h>
#include <sys/wait.h>

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

void timeout_stop_and_remove_timeout(App *app, Timeout *timeout) {
    for (int timeout_index = 0; timeout_index < app->timeouts.size(); timeout_index++) {
        Timeout *t = app->timeouts[timeout_index];
        if (t == timeout) {
            if (t->client) {
                // printf("Timeout Removed: client = %s, fd = %d\n", t->client->name.data(), t->file_descriptor);
            } else {
                // printf("Timeout Removed: noclient, fd = %d\n", t->file_descriptor);
            }
            app->timeouts.erase(app->timeouts.begin() + timeout_index);
            // remove from polled_descriptors
            for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
                if (app->descriptors_being_polled[i].file_descriptor == timeout->file_descriptor) {
                    app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
                    break;
                }
            }
            close(timeout->file_descriptor);
            delete timeout;
            return;
        }
    }
}

void timeout_add(App *app, Timeout *t) {
    if (t->client) {
        // printf("Timeout added: client = %s, fd = %d\n", t->client->name.data(), t->file_descriptor);
    } else {
        // printf("Timeout added: noclient, fd = %d\n", t->file_descriptor);
    }
    
    app->timeouts.push_back(t);
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

bool poll_descriptor(App *app, int file_descriptor, int events, void (*function)(App *, int, void *), void *user_data,
                     char *text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!app || !app->running) return false;
    
    PolledDescriptor polled = {file_descriptor, std::string(text), function, user_data};
    app->descriptors_being_polled.push_back(polled);
    
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
    return nullptr;
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
    return nullptr;
}

void xcb_poll_wakeup(App *app, int fd, void *);

void timeout_poll_wakeup(App *app, int fd, void *) {
    std::lock_guard lock(app->thread_mutex);
    
    bool keep_running = false;
    for (int timeout_index = 0; timeout_index < app->timeouts.size(); timeout_index++) {
        Timeout *timeout = app->timeouts[timeout_index];
        if (timeout->file_descriptor == fd) {
            if (timeout->kill) {
                keep_running = false;
                timeout_stop_and_remove_timeout(app, timeout);
                break;
            }
            
            if (timeout->function) {
                timeout->function(app, timeout->client, timeout, timeout->user_data);
            }
            if ((keep_running = timeout->keep_running))
                break;
            
            timeout_stop_and_remove_timeout(app, timeout);
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

const xcb_query_extension_reply_t *input_query = nullptr;

std::map<xcb_input_device_id_t, int> devices_type;

std::vector<Container *>
pierced_containers(App *app, AppClient *client, int x, int y);

std::vector<Container *>
concerned_containers(App *app, AppClient *client);

void set_active(AppClient *client, const std::vector<Container *> &active_containers, Container *c, bool state);

static void deliver_fine_scroll_event(App *app, int horizontal, int vertical, bool came_from_touchpad) {
    xcb_query_pointer_cookie_t pointer_cookie = xcb_query_pointer(app->connection, app->screen->root);
    xcb_query_pointer_reply_t *pointer_reply = xcb_query_pointer_reply(app->connection, pointer_cookie, nullptr);
    
    if (!pointer_reply)
        return;
    
    for (auto *client: app->clients) {
        if (client->root) {
//            if (client->root->children.empty())
//                continue;
        } else {
            continue;
        }
        
        if (bounds_contains(*client->bounds, pointer_reply->root_x, pointer_reply->root_y)) {
            double x = pointer_reply->root_x - client->bounds->x;
            double y = pointer_reply->root_y - client->bounds->y;
            
            client->mouse_initial_x = x;
            client->mouse_initial_y = y;
            client->mouse_current_x = x;
            client->mouse_current_y = y;
            std::vector<Container *> pierced = pierced_containers(app, client, x, y);
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
                
                if (p->when_fine_scrolled) {
                    p->when_fine_scrolled(client, client->cr, p, -horizontal, -vertical, came_from_touchpad);
                }
                
                handle_mouse_motion(app, client, x, y);
            }
            set_active(client, mouse_downed, client->root, false);
            
            request_refresh(app, client);
        }
    }
}

static bool listen_for_raw_input_events(App *app, xcb_generic_event_t *event, xcb_window_t) {
    if (!app->key_symbols)
        app->key_symbols = xcb_key_symbols_alloc(app->connection);
    
    static int keys_down_count = 0;
    static bool clean = true;
    static bool num = true;
    
    if (event->response_type == XCB_GE_GENERIC) {
        auto *generic_event = (xcb_ge_generic_event_t *) event;
        if (generic_event->event_type == XCB_INPUT_KEY_PRESS) {
            auto *press = (xcb_input_key_press_event_t *) event;

            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(app->key_symbols, press->detail, 0);
            
            if (clean && (press->detail >= 10 && press->detail <= 19)) {
                num = true;
                if (config->pinned_icon_shortcut) {
                    meta_pressed(press->detail);
                }
            }
            num = false;
            clean = keysym == XK_Super_L || keysym == XK_Super_R;
        } else if (generic_event->event_type == XCB_INPUT_KEY_RELEASE) {
            auto *release = (xcb_input_key_release_event_t *) event;

            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(app->key_symbols, release->detail, 0);
            
            bool is_meta = keysym == XK_Super_L || keysym == XK_Super_R;
            if (is_meta && clean && !num) {
                meta_pressed(0);
                clean = false;
            }
        } else if (generic_event->event_type == XCB_INPUT_RAW_MOTION) {
            auto *rmt_event = (xcb_input_raw_motion_event_t *) event;
            int axis_len = xcb_input_raw_button_press_axisvalues_length(rmt_event);
            if (axis_len) {
                // from: https://github.com/MatthewZelriche/NamelessWindow/blob/3168145bdd168a37be783acfd1bfd0ba4cbcb1e2/src/X11/X11GenericMouse.cpp#L31
                // > Ignore other raw motions, based on the valuator indices they contain. This is the best way
                // > I can think of for disregarding raw motion events and only doing scroll. Indices 0 and 1 appear to be
                // > real mouse motion events, while 2 and 3 appear to be horz/vertical valuators for scroll.
                
                auto mask = xcb_input_raw_button_press_valuator_mask(rmt_event);
                bool is_scroll_event = (mask[0] & (1 << 2)) || (mask[0] & (1 << 3));
                bool is_mouse_event = (mask[0] & (1 << 0)) || (mask[0] & (1 << 1));
                if (is_mouse_event) {
                
                } else if (is_scroll_event) {
                    auto raw_values = xcb_input_raw_button_press_axisvalues(
                            (xcb_input_raw_button_press_event_t *) event);
                    
                    // Get the horizontal and vertical scroll values
                    int horizontal = raw_values[0].integral;
                    int vertical = raw_values[1].integral;
                    
                    // check devices_type map for device type
                    int &cached_device_pointer_type = devices_type[rmt_event->deviceid];
                    if (cached_device_pointer_type != 0) {
                        if (cached_device_pointer_type == 1) {
                            deliver_fine_scroll_event(app, vertical, horizontal, false);
                            return true;
                        } else if (cached_device_pointer_type == 2) {
                            deliver_fine_scroll_event(app, horizontal, vertical, true);
                            return true;
                        }
                    } else {
                        // We want to know if the event came from a mouse or a touchpad
                        xcb_input_list_input_devices_cookie_t devices_cookie = xcb_input_list_input_devices(
                                app->connection);
                        xcb_input_list_input_devices_reply_t *devices_reply = xcb_input_list_input_devices_reply(
                                app->connection, devices_cookie, NULL);
                        if (devices_reply != nullptr) {
                            defer(free(devices_reply));
                            
                            int len = xcb_input_list_input_devices_devices_length(devices_reply);
                            xcb_input_device_info_t *devices = xcb_input_list_input_devices_devices(devices_reply);
                            // iterate through devices using len
                            
                            for (int i = 0; i < len; i++) {
                                xcb_input_device_info_t *device = &devices[i];
                                if (device->device_id == rmt_event->deviceid) {
                                    if (device->device_type != 0) {
                                        // get name of atom device_type
                                        auto cookie = xcb_get_atom_name(app->connection, device->device_type);
                                        auto reply = xcb_get_atom_name_reply(app->connection, cookie, NULL);
                                        if (reply == nullptr)
                                            continue;
                                        
                                        defer(free(reply));
                                        std::string name(xcb_get_atom_name_name(reply),
                                                         xcb_get_atom_name_name_length(reply));
                                        if (name == "MOUSE") {
                                            devices_type[rmt_event->deviceid] = 1;
                                            deliver_fine_scroll_event(app, vertical, horizontal, false);
                                            return true;
                                        } else if (name == "TOUCHPAD") {
                                            devices_type[rmt_event->deviceid] = 2;
                                            deliver_fine_scroll_event(app, horizontal, vertical, true);
                                            return true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                }
            }
        }
    }
    
    return false; // Returning false here means this event handler does not consume the event
}

void get_raw_motion_and_scroll_events(App *app) {
    // check if xinput is present on server
    input_query = xcb_get_extension_data(app->connection, &xcb_input_id);
    if (!input_query->present) {
        perror("XInput was not present on Xorg server.\n");
        return;
    }
    
    // PASS US ALL EVENTS
    app_create_custom_event_handler(app, INT_MAX, listen_for_raw_input_events);
    
    // select for raw motion events on the root window
    struct {
        xcb_input_event_mask_t iem;
        xcb_input_xi_event_mask_t xiem;
    } se_mask;
    se_mask.iem.deviceid = XCB_INPUT_DEVICE_ALL;
    se_mask.iem.mask_len = 1;
    se_mask.xiem = (xcb_input_xi_event_mask_t) (XCB_INPUT_XI_EVENT_MASK_KEY_PRESS |
                                                XCB_INPUT_XI_EVENT_MASK_KEY_RELEASE |
                                                XCB_INPUT_XI_EVENT_MASK_RAW_MOTION);
    xcb_input_xi_select_events(app->connection, app->screen->root, 1, &se_mask.iem);
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
    app->running_mutex.lock();
    app->connection = connection;
    app->screen_number = screen_number;
    app->screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
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
    
    poll_descriptor(app, xcb_get_file_descriptor(app->connection), POLLIN, xcb_poll_wakeup, nullptr, "XCB");
    
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
    
    get_raw_motion_and_scroll_events(app);
    
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
            auto psi = new ScreenInformation;
            psi->root_window = screen->root;
            psi->width_in_pixels = screen->width_in_pixels;
            psi->width_in_millimeters = screen->width_in_millimeters;
            psi->height_in_pixels = screen->height_in_pixels;
            psi->height_in_millimeters = screen->height_in_millimeters;
            psi->is_primary = true;
            psi->dpi_scale = 1;
            psi->x = 0;
            psi->y = 0;
            screens.push_back(psi);
            primary_screen_info = screens[0];
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
            settings.override_redirect,
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
        xcb_ewmh_wm_strut_partial_t wm_strut = {};
        
        auto height = screen->height_in_pixels - primary_screen_info->height_in_pixels + settings.reserve_bottom;
        wm_strut.bottom = height;
        wm_strut.bottom_start_x = primary_screen_info->x;
        wm_strut.bottom_end_x = primary_screen_info->x + settings.w;
        xcb_ewmh_set_wm_strut_partial(&app->ewmh,
                                      window,
                                      wm_strut);
        
        xcb_ewmh_set_wm_strut(&app->ewmh,
                              window,
                              settings.reserve_left,
                              settings.reserve_right,
                              settings.reserve_top,
                              settings.reserve_bottom);
    }
    
    if (settings.slide) {
        xcb_atom_t atom = get_cached_atom(app, "_KDE_SLIDE");
        xcb_change_property(app->connection,
                            XCB_PROP_MODE_REPLACE,
                            window,
                            atom,
                            atom,
                            32,
                            5,
                            &settings.slide_data);
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
//    client->override_redirect = settings.override_redirect;
    
    client->root->wanted_bounds.w = FILL_SPACE;
    client->root->wanted_bounds.h = FILL_SPACE;
    
    client->bounds->x = settings.x;
    client->bounds->y = settings.y;
    client->bounds->w = settings.w;
    client->bounds->h = settings.h;
    client->colormap = colormap;
    client->on_close_is_unmap = settings.on_close_is_unmap;
    client->cr = cr;
    client->skip_taskbar = settings.skip_taskbar;
    
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
    if (client->auto_delete_root)
        delete client->root;
    cairo_destroy(client->cr);
    xcb_free_colormap(app->connection, client->colormap);
    xcb_cursor_context_free(client->ctx);
    deinit_keyboard(app, client);
}

AppClient *
client_by_name(App *app, const std::string &target_name) {
    for (AppClient *possible_client: app->clients)
        if (target_name == possible_client->name)
            return possible_client;
    
    return nullptr;
}

AppClient *
client_by_window(App *app, xcb_window_t target_window) {
    for (AppClient *possible_client: app->clients)
        if (target_window == possible_client->window)
            return possible_client;
    
    return nullptr;
}

bool valid_client(App *app, AppClient *target_client) {
    if (target_client == nullptr)
        return false;
    
    for (AppClient *client: app->clients)
        if (target_client == client)
            return true;
    
    return false;
}

void client_add_handler(App *app,
                        AppClient *client_entity,
                        bool (*event_handler)(App *app, xcb_generic_event_t *, xcb_window_t)) {
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

void paint_client_timeout(App *app, AppClient *client, Timeout *, void *) {
    client->refresh_already_queued = false;
    client_paint(app, client);
}

void request_refresh(App *app, AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (app == nullptr || client == nullptr || client->refresh_already_queued)
        return;
    float fps = client->fps;
    if (fps != 0)
        fps = 1000 / fps;
    client->refresh_already_queued = true;
    app_timeout_create(app, client, fps, paint_client_timeout, nullptr, "request_refresh(app, client)");
}

void client_animation_paint(App *app, AppClient *client, Timeout *, void *user_data);

void client_register_animation(App *app, AppClient *client) {
    if (app == nullptr || !app->running)
        return;
    if (client->animations_running == 0) {
        float fps = client->fps;
        if (fps != 0)
            fps = 1000 / fps;
        app_timeout_create(app, client, fps, client_animation_paint, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
    }
    client->animations_running++;
}

void client_unregister_animation(App *app, AppClient *client) {
    if (app == nullptr || !app->running)
        return;
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
    
    if (client->name == "taskbar" && app->running) {
        client->marked_to_close = false;
        return;
    }
    
    for (int i = client->commands.size() - 1; i >= 0; i--)
        client->commands[i]->kill(false);
    client->commands.clear();
    
    if (client->when_closed) {
        client->when_closed(client);
    }
    
    remove_cached_fonts(client->cr);
    
    if (client->user_data) {
        auto data = static_cast<UserData *>(client->user_data);
        delete data;
    }
    
    int w = 0;
    if (client->popup_info.is_popup) {
        AppClient *parent_client = nullptr;
        for (auto c: app->clients)
            if (c->child_popup == client)
                parent_client = c;
        if (parent_client) {
            if (parent_client->popup_info.is_popup) {
                parent_client->wants_popup_events = true;
                if (parent_client->popup_info.takes_input_focus) {
                    w = parent_client->window;
                    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, parent_client->window,
                                        XCB_CURRENT_TIME);
                    xcb_ewmh_request_change_active_window(&app->ewmh,
                                                          app->screen_number,
                                                          parent_client->window,
                                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                          XCB_CURRENT_TIME,
                                                          XCB_NONE);
                    xcb_flush(app->connection);
                }
            } else {
                xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
                xcb_flush(app->connection);
                xcb_aux_sync(app->connection);
            }
        } else {
            xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
            xcb_flush(app->connection);
            xcb_aux_sync(app->connection);
        }
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
                                           bool remove = timeout_client == client;
        
                                           if (remove) {
                                               close(timeout->file_descriptor);
                                               for (int i = 0; i < client->app->descriptors_being_polled.size(); i++) {
                                                   if (client->app->descriptors_being_polled[i].file_descriptor ==
                                                       timeout->file_descriptor) {
                                                       client->app->descriptors_being_polled.erase(
                                                               client->app->descriptors_being_polled.begin() + i);
                                                       break;
                                                   }
                                               }
                                               delete timeout;
                                           }
                                           if (remove != (timeout_client == client)) {
                                               // printf("----> Previously would've resulted in an error\n");
                                           }
                                           return remove;
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
    
    app->running = false;
    for (auto c: app->clients)
        app->running = c->keeps_app_running;
    
    if (w != 0) {
        xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, w,
                            XCB_CURRENT_TIME);
        xcb_ewmh_request_change_active_window(&app->ewmh,
                                              app->screen_number,
                                              w,
                                              XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                              XCB_CURRENT_TIME,
                                              XCB_NONE);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
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
    
        if (container->type == ::newscroll) {
            auto s = (ScrollContainer *) container;
            std::vector<int> render_order;
            for (int i = 0; i < s->content->children.size(); i++) {
                render_order.push_back(i);
            }
            std::sort(render_order.begin(), render_order.end(), [s](int a, int b) -> bool {
                return s->content->children[a]->z_index < s->content->children[b]->z_index;
            });
            
            for (auto index: render_order) {
                cairo_save(client->cr);
                set_rect(client->cr, container->real_bounds);
                cairo_clip(client->cr);
                
                if (overlaps(s->content->children[index]->real_bounds, s->real_bounds)) {
                    paint_container(app, client, s->content->children[index]);
                }
                cairo_restore(client->cr);
            }
            
            if (s->right && s->right->exists)
                paint_container(app, client, s->right);
            if (s->bottom && s->bottom->exists)
                paint_container(app, client, s->bottom);
        } else {
            // What we already do
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
                for (auto index: render_order) {
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
                for (auto index: render_order) {
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
}

static xcb_generic_event_t *event;
void handle_xcb_event(App *app);
void handle_event(App *app);

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

void fill_list_with_concerned(std::vector<Container *> &containers, Container *parent) {
    if (parent->type == ::newscroll) {
        auto s = (ScrollContainer *) parent;
        for (auto child: s->content->children)
            fill_list_with_concerned(containers, child);
        if (s->right)
            fill_list_with_concerned(containers, s->right);
        if (s->bottom)
            fill_list_with_concerned(containers, s->bottom);
    } else {
        for (auto child: parent->children)
            fill_list_with_concerned(containers, child);
    }
    
    if (parent->state.concerned && parent->exists) {
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
    if (!parent->exists)
        return;
    if (parent->type == ::newscroll) {
        auto s = (ScrollContainer *) parent;
        for (auto child: s->content->children) {
            if (child->interactable) {
                // parent->real_bounds w and h need to be subtracted by right and bottom if they exist
                auto real_bounds_copy = parent->real_bounds;
                if (s->right && s->right->exists)
                    real_bounds_copy.w -= s->right->real_bounds.w;
                if (s->bottom && s->bottom->exists)
                    real_bounds_copy.h -= s->bottom->real_bounds.h;
                if (!bounds_contains(real_bounds_copy, x, y))
                    continue;
                fill_list_with_pierced(containers, child, x, y);
            }
        }
        if (s->right && s->right->exists)
            fill_list_with_pierced(containers, s->right, x, y);
        if (s->bottom && s->bottom->exists)
            fill_list_with_pierced(containers, s->bottom, x, y);
    } else {
        for (auto child: parent->children) {
            if (child->interactable) {
                // check if parent is scrollpane and if so, check if the child is in bounds before calling
                if (parent->type >= ::scrollpane && parent->type <= ::scrollpane_b_never)
                    if (!bounds_contains(parent->real_bounds, x, y))
                        continue;
                fill_list_with_pierced(containers, child, x, y);
            }
        }
    }
    
    if (parent->exists) {
        if (parent->handles_pierced) {
            if (parent->handles_pierced(parent, x, y))
                containers.push_back(parent);
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
    for (auto container: pierced) {
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
                auto move_distance = abs(client->mouse_initial_x - client->mouse_current_x);
                if (move_distance >= c->minimum_x_distance_to_move_before_drag_begins) {
                    // handle when_drag_start
                    c->state.mouse_dragging = true;
                    if (c->when_drag_start) {
                        c->when_drag_start(client, client->cr, c);
                    }
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

void mouse_motion_timeout(App *app, AppClient *client, Timeout *timeout, void *user_data) {
    if (valid_client(app, client)) {
        client->motion_event_timeout = nullptr;
        handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
        request_refresh(app, client);
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
    
    client->motion_event_x = e->event_x;
    client->motion_event_y = e->event_y;
    
    // TODO: the reason animations hang in the taskbar is because motion_event_timeout is at some point not reset to -1
    
    if (client->motion_events_per_second == 0) {
        handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
        request_refresh(app, client);
    } else if (client->motion_event_timeout == nullptr) {
        float fps = client->motion_events_per_second;
        if (fps != 0)
            fps = 1000 / fps;
        client->motion_event_timeout = app_timeout_create(app, client, fps, mouse_motion_timeout, nullptr,
                                                          const_cast<char *>(__PRETTY_FUNCTION__));
        
        handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
        request_refresh(app, client);
    }
}

void set_active(AppClient *client, const std::vector<Container *> &active_containers, Container *c, bool state) {
    if (c->type == ::newscroll) {
        auto s = (ScrollContainer *) c;
        for (auto child: s->content->children) {
            set_active(client, active_containers, child, state);
        }
        
        bool will_be_activated = false;
        for (auto active_container: active_containers) {
            if (active_container == s->content) {
                will_be_activated = true;
            }
        }
        if (will_be_activated) {
            if (!s->content->active) {
                s->content->active = true;
                if (s->content->when_active_status_changed) {
                    s->content->when_active_status_changed(client, client->cr, s->content);
                }
            }
        } else {
            if (s->content->active) {
                s->content->active = false;
                if (s->content->when_active_status_changed) {
                    s->content->when_active_status_changed(client, client->cr, s->content);
                }
            }
        }
    } else {
        for (auto child: c->children) {
            set_active(client, active_containers, child, state);
        }
        
        bool will_be_activated = false;
        for (auto active_container: active_containers) {
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
}

void handle_mouse_button_press(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *e = (xcb_button_press_event_t *) (event);
    auto client = client_by_window(app, e->event);
    if (!valid_client(app, client))
        return;
    
    if (e->detail == XCB_BUTTON_INDEX_1)
        client->left_mouse_down = true;
    
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
        
        if (p->when_mouse_down || p->when_key_event) {
            mouse_downed.push_back(p);
            if (p->when_mouse_down) {
                p->when_mouse_down(client, client->cr, p);
            }
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
    
    if (e->detail == XCB_BUTTON_INDEX_1)
        client->left_mouse_down = false;
    
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
    
    client->mouse_current_x = e->event_x;
    client->mouse_current_y = e->event_y;
    client->motion_event_x = e->event_x;
    client->motion_event_y = e->event_y;
    std::vector<Container *> concerned = concerned_containers(app, client);
    
    handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
    
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
    if (container->type == ::newscroll) {
        auto *s = (ScrollContainer *) container;
        for (auto c: s->content->children) {
            send_key_actual(app, client, c, is_string, keysym, string, mods, direction);
        }
        if (s->content->when_key_event) {
            s->content->when_key_event(client, client->cr, s->content, is_string, keysym, string, mods, direction);
        }
    } else {
        for (auto c: container->children) {
            send_key_actual(app, client, c, is_string, keysym, string, mods, direction);
        }
        if (container->when_key_event) {
            container->when_key_event(client, client->cr, container, is_string, keysym, string, mods, direction);
        }
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

//static void
//send_key_to_wants_keys(App *app, AppClient *client, Container *container) {
//    for (auto c: container->children) {
//        send_key_to_wants_keys(app, client, c);
//    }
//
//    if (container->when_key_event) {
//       send_key(app, client, container);
//    }
//}

void handle_xcb_event(App *app, xcb_window_t window_number, xcb_generic_event_t *event, bool change_event_source) {
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
                if (client->on_close_is_unmap) {
                    xcb_unmap_window(app->connection, client->window);
                    xcb_flush(app->connection);
                } else {
                    client_close_threaded(app, client);
                }
            }
            return;
            break;
        }
        case XCB_MOTION_NOTIFY: {
            auto *e = (xcb_motion_notify_event_t *) event;
            if (auto client = client_by_window(app, window_number)) {
                if (change_event_source) {
                    e->event = window_number;
                    e->event_x -= client->bounds->x;
                    e->event_y -= client->bounds->y;
                }
                
                if (client->popup_info.is_popup) {
                    if (client->popup_info.transparent_mouse_grab) {
                        xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
                        xcb_flush(app->connection);
                    }
                }
            }
            
            handle_mouse_motion(app);
            return;
        }
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) event;
            if (auto client = client_by_window(app, window_number)) {
                if (change_event_source) {
                    if (!bounds_contains(*client->bounds, e->event_x, e->event_y)) {
                        if (client->popup_info.ignore_scroll) {
                            if (!(e->detail >= 4 && e->detail <= 7)) {
                                client_close_threaded(app, client);
                            }
                        } else {
                            client_close_threaded(app, client);
                        }
                    }
                    if (client->popup_info.is_popup) {
                        if (client->popup_info.transparent_mouse_grab) {
                            xcb_allow_events(app->connection, XCB_ALLOW_REPLAY_POINTER, XCB_CURRENT_TIME);
                            xcb_flush(app->connection);
                        }
                    }
                    e->event = window_number;
                    e->event_x -= client->bounds->x;
                    e->event_y -= client->bounds->y;
                }
            }
            handle_mouse_button_press(app);
            break;
        }
        case XCB_BUTTON_RELEASE: {
            auto *e = (xcb_button_release_event_t *) event;
            if (auto client = client_by_window(app, window_number)) {
                if (change_event_source) {
                    e->event = window_number;
                    e->event_x -= client->bounds->x;
                    e->event_y -= client->bounds->y;
                }
            }
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
            if (auto client = client_by_window(app, window_number)) {
                if (change_event_source) {
                    e->event = window_number;
                    e->event_x -= client->bounds->x;
                    e->event_y -= client->bounds->y;
                }
            }
            handle_mouse_leave_notify(app);
            break;
        }
        case XCB_ENTER_NOTIFY: {
            auto *e = (xcb_enter_notify_event_t *) event;
            if (e->mode != 0) {
                return;
            }
            if (auto client = client_by_window(app, window_number)) {
                if (change_event_source) {
                    e->event = window_number;
                    e->event_x -= client->bounds->x;
                    e->event_y -= client->bounds->y;
                }
            }
            
            handle_mouse_enter_notify(app);
            break;
        }
        case XCB_KEY_PRESS: {
            auto client = client_by_window(app, window_number);
            if (!valid_client(app, client))
                return;
            auto *e = (xcb_key_press_event_t *) event;
            if (change_event_source) {
                e->event = window_number;
                e->event_x -= client->bounds->x;
                e->event_y -= client->bounds->y;
            }
    
            send_key(app, client, client->root);
            break;
        }
    
        case XCB_UNMAP_NOTIFY: {
            if (auto client = client_by_window(app, window_number)) {
                client->mapped = false;
            }
            break;
        }
    
        case XCB_MAP_NOTIFY: {
            if (auto client = client_by_window(app, window_number)) {
                client->mapped = true;

                if (client->popup_info.is_popup && client->popup_info.wants_grab) {
                    // TODO why don't we grab the pointer instead?
                    xcb_void_cookie_t grab_cookie =
                            xcb_grab_button_checked(app->connection,
                                                    client->popup_info.transparent_mouse_grab,
                                                    app->screen->root,
                                                    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                                    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
                                                    client->popup_info.transparent_mouse_grab ? XCB_GRAB_MODE_SYNC
                                                                                              : XCB_GRAB_MODE_ASYNC,
                                                    XCB_GRAB_MODE_ASYNC,
                                                    XCB_NONE,
                                                    XCB_NONE,
                                                    XCB_BUTTON_INDEX_ANY,
                                                    XCB_MOD_MASK_ANY);
                    xcb_generic_error_t *error = xcb_request_check(app->connection, grab_cookie);
                    if (error != NULL) {
                        printf("Could not grab button on root: %d, for window: %d, error_code: %d\n", app->screen->root,
                               client->window,
                               error->error_code);
                        client_close_threaded(app, client);
                        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
                    } else {
                        if (client->popup_info.takes_input_focus)
                            xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window,
                                                XCB_CURRENT_TIME);
                    }
                }
                xcb_flush(app->connection);
            }
            break;
        }
        
        case XCB_FOCUS_IN: {
            if (auto client = client_by_window(app, window_number)) {
                update_keymap(client->keyboard);
            }
            break;
        }
        
        case XCB_FOCUS_OUT: {
            if (auto client = client_by_window(app, window_number)) {
                if (client->child_popup) {
                    if (!client->child_popup->popup_info.is_popup)
                        client_close_threaded(app, client);
                } else if (client->popup_info.is_popup) {
                    if (client->popup_info.close_on_focus_out)
                        client_close_threaded(app, client);
        
                    AppClient *parent_client = nullptr;
                    for (auto c: app->clients)
                        if (c->child_popup == client)
                            parent_client = c;
                    if (parent_client && parent_client->popup_info.is_popup &&
                        parent_client->popup_info.close_on_focus_out) {
                        client_close_threaded(app, parent_client);
                    }
                }
            }
            break;
        }
        case XCB_KEY_RELEASE: {
            auto client = client_by_window(app, window_number);
            if (!valid_client(app, client))
                return;
            auto *e = (xcb_key_release_event_t *) event;
            e->event = window_number;
            
            send_key(app, client, client->root);
            break;
        }
        default: {
            for (auto *client: app->clients) {
                if (client->keyboard) {
                    if (event->response_type == client->keyboard->first_xkb_event) {
                        process_xkb_event(event, client->keyboard);
                    }
                }
            }
    
            break;
        }
    }
    
    if (auto client = client_by_window(app, window_number)) {
        request_refresh(app, client);
    }
}

void handle_event(App *app) {
    if (auto window = get_window(event)) {
        bool event_consumed_by_custom_handler = false;
        for (auto handler: app->handlers) {
            // If the handler's target window is INT_MAX that means it wants to see every event
            if (handler->target_window == INT_MAX) {
                if (handler->event_handler(app, event, handler->target_window)) {
                    // TODO: is this supposed to be called twice? I doubt it
//                        handler->event_handler(app, event);
                    event_consumed_by_custom_handler = true;
                }
            } else if (handler->target_window ==
                       window) { // If the target window and the window of this event matches, then send the handler the
                if (handler->event_handler(app, event, handler->target_window)) {
                    event_consumed_by_custom_handler = true;
                }
            }
        }
        if (event_consumed_by_custom_handler) {
        
        } else {
            if (auto client = client_by_window(app, window)) {
                handle_xcb_event(app, client->window, event, false);
            } else if (window == app->screen->root) {
                for (auto c: app->clients) {
                    if (c->wants_popup_events) {
                        handle_xcb_event(app, c->window, event, true);
                    }
                }
                // An event from a window for which is not a client
            }
        }
    } else {
        for (auto handler: app->handlers) {
            // If the handler's target window is INT_MAX that means it wants to see every event
            if (handler->target_window == INT_MAX) {
                if (handler->event_handler(app, event, handler->target_window)) {
                
                }
            }
        }
    }
    
    free(event);
}

void handle_xcb_event(App *app) {
    if (app == nullptr)
        return;
    if (!app->running)
        return;
    
    std::lock_guard lock(app->thread_mutex);
    
    while ((event = xcb_poll_for_event(app->connection))) {
        handle_event(app);
    }
}

void xcb_poll_wakeup(App *app, int fd, void *) {
    handle_xcb_event(app);
}

void app_main(App *app) {
    if (app == nullptr) {
        printf("App * passed to app_main was nullptr so couldn't run\n");
        assert(app != nullptr);
        return;
    }
    
    // Audio thread
    audio_start(app);
    
    int MAX_POLLING_EVENTS_AT_THE_SAME_TIME = 1000;
    pollfd fds[MAX_POLLING_EVENTS_AT_THE_SAME_TIME];
    
    app->running = true;
    app->running_mutex.unlock();
    while (app->running) {
        // set all fds to 0
        memset(fds, 0, sizeof(fds));
        for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
            fds[i].fd = app->descriptors_being_polled[i].file_descriptor;
            fds[i].events = POLLIN | POLLPRI;
        }
        
        int num_ready = poll(fds, app->descriptors_being_polled.size(), -1);
        if (num_ready < 0) {
            perror("poll");
            exit(1);
        }
        
        std::lock_guard m(app->running_mutex);
        app->loop++;
        
        for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
            if (fds[i].revents & POLLPRI) {
                bool found = false;
                for (const auto &polled: app->descriptors_being_polled) {
                    if (fds[i].fd == polled.file_descriptor) {
                        found = true;
                        if (polled.function) {
                            polled.function(app, polled.file_descriptor, polled.user_data);
                        }
                    }
                }
                if (!found) {
                    for (int x = 0; x < app->descriptors_being_polled.size(); x++) {
                        if (app->descriptors_being_polled[x].file_descriptor == fds[x].fd) {
                            app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + x);
                            break;
                        }
                    }
                    close(fds[i].fd);
                }
            }
        }
        
        for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
            if (fds[i].revents & POLLIN) {
                bool found = false;
                for (const auto &polled: app->descriptors_being_polled) {
                    if (fds[i].fd == polled.file_descriptor) {
                        found = true;
                        if (polled.function) {
                            polled.function(app, polled.file_descriptor, polled.user_data);
                        }
                    }
                }
                if (!found) {
                    for (int x = 0; x < app->descriptors_being_polled.size(); x++) {
                        if (app->descriptors_being_polled[x].file_descriptor == fds[x].fd) {
                            app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + x);
                            break;
                        }
                    }
                    close(fds[i].fd);
                }
            }
        }
        
        // TODO: we can't delete while we iterate.
        for (AppClient *client: app->clients) {
            if (client->marked_to_close) {
                client_close(app, client);
                break;
            }
        }
    }
    
    for (AppClient *client: app->clients) {
        client_close(app, client);
    }
    
    audio_stop();
    audio_join();
}

void app_clean(App *app) {
    for (AppClient *client: app->clients) {
        client_close(app, client);
    }
    
    if (app->key_symbols) {
        xcb_key_symbols_free(app->key_symbols);
    }
    
    input_query = nullptr;
    devices_type.clear();
    
    for (auto c: app->clients) {
        delete c;
    }
    app->clients.clear();
    app->clients.shrink_to_fit();
    
    for (auto handler: app->handlers) {
        delete handler;
    }
    
    cleanup_cached_fonts();
    cleanup_cached_atoms();
    
    for (auto t: app->timeouts) {
        for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
            if (app->descriptors_being_polled[i].file_descriptor == t->file_descriptor) {
                app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
                break;
            }
        }
        close(t->file_descriptor);
        delete t;
    }
    app->timeouts.clear();
    app->timeouts.shrink_to_fit();
    
    app->descriptors_being_polled.clear();
    app->descriptors_being_polled.shrink_to_fit();
    
    if (app->device) {
        cairo_device_finish(app->device);
        cairo_device_destroy(app->device);
    }
    
    xcb_disconnect(app->connection);
}

void
client_create_animation(App *app, AppClient *client, double *value, double delay, double length, easingFunction easing,
                        double target, void (*finished)(AppClient *), bool relayout) {
    for (auto &animation: client->animations) {
        if (animation.value == value) {
            animation.delay = delay;
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
    animation.delay = delay;
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

void
client_create_animation(App *app, AppClient *client, double *value, double delay, double length, easingFunction easing,
                        double target) {
    client_create_animation(app, client, value, delay, length, easing, target, nullptr, false);
}

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double delay,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)(AppClient *client)) {
    client_create_animation(app, client, value, delay, length, easing, target, finished, false);
}

void
client_create_animation(App *app, AppClient *client, double *value, double delay, double length, easingFunction easing,
                        double target, bool relayout) {
    client_create_animation(app, client, value, delay, length, easing, target, nullptr, relayout);
}

bool app_timeout_stop(App *app,
                      AppClient *client,
                      Timeout *timeout) {
    if (timeout == nullptr)
        return false;
    if (app == nullptr || !app->running) return false;
    timeout->kill = true;
    return true;
}

Timeout *app_timeout_replace(App *app,
                             AppClient *client,
                             Timeout *timeout, float timeout_ms,
                             void (*timeout_function)(App *, AppClient *, Timeout *, void *),
                             void *user_data) {
    if (timeout == nullptr) {
        return nullptr;
    }
    if (app == nullptr || !app->running || !timeout_function) return nullptr;
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
    
    if (timerfd_settime(timeout->file_descriptor, 0, &time, nullptr) != 0) {
        return nullptr;
    }
    
    timeout->function = timeout_function;
    timeout->client = client;
    timeout->user_data = user_data;
    timeout->keep_running = false;
    timeout->kill = false;
    
    return timeout;
}

Timeout *
app_timeout_create(App *app, AppClient *client, float timeout_ms,
                   void (*timeout_function)(App *, AppClient *, Timeout *, void *), void *user_data,
                   char *text) {
    if (app == nullptr || !app->running || !timeout_function) return nullptr;
    int timeout_file_descriptor = timerfd_create(CLOCK_REALTIME, 0);
    if (timeout_file_descriptor == -1) { // error with timerfd_create
        return nullptr;
    }
    
    bool success = poll_descriptor(app, timeout_file_descriptor, EPOLLIN, timeout_poll_wakeup, nullptr, "Timeout");
    if (!success) { // error with poll_descriptor
        for (int i = 0; i < app->descriptors_being_polled.size(); i++) {
            if (app->descriptors_being_polled[i].file_descriptor == timeout_file_descriptor) {
                app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
                break;
            }
        }
        close(timeout_file_descriptor);
        return nullptr;
    }
    auto timeout = new Timeout;
    timeout->function = timeout_function;
    timeout->client = client;
    timeout->file_descriptor = timeout_file_descriptor;
    timeout->user_data = user_data;
    timeout->keep_running = false;
    timeout->text = std::string(text);
    timeout->kill = false;
    timeout_add(app, timeout);
    
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
        delete timeout;
        return nullptr;
    }
    
    return timeout;
}

void app_create_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event,
                                                            xcb_window_t target_window)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *custom_event_handler = new Handler;
    custom_event_handler->event_handler = custom_handler;
    custom_event_handler->target_window = window;
    app->handlers.push_back(custom_event_handler);
}

void app_remove_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event,
                                                            xcb_window_t target_window)) {
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
    if (!app || !app->running || !client) return false;
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    uint32_t values[] = {
            (uint32_t) x,
            (uint32_t) y,
    };
    
    auto cookie_configure_window = xcb_configure_window_checked(app->connection, client->window, mask, values);
    return xcb_request_check(app->connection, cookie_configure_window);
}

bool client_set_size(App *app, AppClient *client, int w, int h) {
    if (!app || !app->running || !client) return false;
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
    while ((event = xcb_poll_for_event(app->connection)))
        handle_event(app);
#ifdef TRACY_ENABLE
    FrameMarkStart("Animation Paint");
#endif
    if (!app || !app->running) return;
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("update animating values");
#endif
        long now = get_current_time_in_ms();
        
        bool wants_to_relayout = false;
        
        for (auto &animation: client->animations) {
            long elapsed_time = now - (animation.start_time + animation.delay);
            if (elapsed_time < 0)
                elapsed_time = 0;
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
        request_refresh(app, client);
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

void command_wakeup(App *app, int fd, void *userdata) {
    auto cc = (Subprocess *) userdata;
    std::size_t buffer_size = 131072; // (128 kB).
    char output[buffer_size];
    
    ssize_t read_size = read(fd, output, buffer_size);
    if (read_size == -1) {
        cc->status = CommandStatus::ERROR;
        goto finish;
    } else if (read_size == 0) {
        cc->status = CommandStatus::FINISHED;
        goto finish;
    } else {
        cc->status = CommandStatus::UPDATE;
        cc->output += std::string(output, read_size);
        cc->recent = std::string(output, read_size);
        if (cc->function)
            cc->function(cc);
        return;
    }
    
    finish:
    if (cc->function)
        cc->function(cc);
    cc->kill(false);
}

void command_timeout(App *app, int fd, void *userdata) {
    auto cc = (Subprocess *) userdata;
    cc->status = CommandStatus::TIMEOUT;
    if (cc->function)
        cc->function(cc);
    cc->kill(false);
}

Subprocess *
command_with_client(AppClient *client, const std::string &c, int timeout_in_ms, void (*function)(Subprocess *),
                    void *user_data) {
    int timerfd = 0;
    if (timeout_in_ms != 0) {
        struct itimerspec its;
        timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd == -1) {
            printf("Error creating timer for command: %s\n", c.c_str());
            return nullptr;
        }
        memset(&its, 0, sizeof(its));
        its.it_value.tv_sec = timeout_in_ms / 1000;
        its.it_value.tv_nsec = (timeout_in_ms % 1000) * 1000000;
        if (timerfd_settime(timerfd, 0, &its, NULL) == -1) {
            printf("Error setting timer for command: %s\n", c.c_str());
            return nullptr;
        }
    }
    
    auto cc = new Subprocess(client->app, c);
    if (timeout_in_ms != 0)
        cc->timeout_fd = timerfd;
    cc->function = function;
    cc->client = client;
    cc->user_data = user_data;
    client->commands.push_back(cc);
    
    poll_descriptor(client->app, cc->outpipe[0], EPOLLIN, command_wakeup, cc, "Command with client: " );
    if (timeout_in_ms != 0) {
        poll_descriptor(client->app, cc->timeout_fd, EPOLLIN, command_timeout, cc, "Timeout with command with client: ");
    }
    return cc;
}

void set_active(AppClient *client, Container *c, bool state) {
    std::vector<Container *> containers;
    containers.push_back(c);
    set_active(client, containers, client->root, state);
}
