#ifndef APPLICATION_HEADER
#define APPLICATION_HEADER

#include "application.h"
#include "container.h"
#include "easing.h"

#include <X11/X.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <cairo-xcb.h>
#include <cairo.h>
#include <inttypes.h>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/epoll.h>
#include <thread>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <zconf.h>

struct Settings {
    int16_t x = 0;
    int16_t y = 0;
    uint16_t w = 800;
    uint16_t h = 600;

    uint32_t background = 0x00000000;

    bool force_position = false;
    bool decorations = true;

    bool reserve_side = false;
    uint32_t reserve_left = 0;
    uint32_t reserve_right = 0;
    uint32_t reserve_top = 0;
    uint32_t reserve_bottom = 0;

    bool skip_taskbar = false;
    bool no_input_focus = false;
    bool dock = false;
    bool sticky = false;
    bool window_transparent = true;
    bool popup = false;

    Settings() { reserve_side = false; }
};

struct client_container_info {
    // initial refers to the click
    int mouse_initial_x = 0;

    int mouse_initial_y = 0;

    int mouse_current_x = 0;

    int mouse_current_y = 0;
};

struct client_animations {
    int count = 0;
};

struct Handler;

struct Timeout {
    int file_descriptor;

    void (*function)(App *, AppClient *, void *user_data);

    AppClient *client = nullptr;

    void *user_data = nullptr;
};


struct App {
    xcb_ewmh_connection_t ewmh;

    bool running = true;

    Bounds bounds;// these are the bounds of the entire screen

    // TODO: each client_entity should have its own mutex rather than locking all
    // clients
    std::mutex clients_mutex;

    std::vector<AppClient *> clients;

    std::vector<Handler *> handlers;

    xcb_window_t grab_window;

    xcb_connection_t *connection = nullptr;

    xcb_visualtype_t *argb_visualtype = nullptr;

    xcb_visualtype_t *root_visualtype = nullptr;

    int screen_number = 0;

    xcb_screen_t *screen = nullptr;

    int epoll_fd = 0;

    int xcb_fd = 0;

    std::vector<Timeout *> timeouts;

    // TODO: move atoms into their own things
    xcb_atom_t protocols_atom = 0;

    xcb_atom_t delete_window_atom = 0;

    xcb_atom_t MOTIF_WM_HINTS = 0;

    App();
};

struct Handler {
    xcb_window_t target_window = 0;

    // returning false from this callback means don't consume the event
    bool (*event_handler)(App *app, xcb_generic_event_t *) = nullptr;
};

void init_client(AppClient *client);

void destroy_client(AppClient *client);

bool valid_client(App *app, AppClient *target_client);

/**
 * If you're calling any of the functions below after calling app_main (from
 * another thread for instance) make sure to get a lock on the clients_mutex to
 * stay thread safe like so:
 *
 *     std::lock_guard g(app->clients_mutex);
 * or:
 *
 *     std::unique_lock g(app->clients_mutex);
 *
 * Also, you are free to call these functions from callbacks set on containers
 * by you except from when_paint callbacks. You're asking for trouble if you try
 * to do anything but paint from there.
 *
 */

void handle_xcb_event(App *app, std::vector<xcb_window_t> *windows);

void handle_xcb_event(App *app, xcb_window_t window_number, xcb_generic_event_t *event);

AppClient *
client_new(App *app, Settings settings, const std::string &name);

AppClient *
client_by_name(App *app, const std::string &target_name);

AppClient *
client_by_window(App *app, xcb_window_t target_window);

void client_show(App *app, AppClient *client_entity);

void client_hide(App *app, AppClient *client_entity);

void request_refresh(App *app, AppClient *client_entity);

void client_register_animation(App *app, AppClient *client_entity);

void client_create_animation(App *app,
                             AppClient *client_entity,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target);

void client_create_animation(App *app,
                             AppClient *client_entity,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)());

void client_create_animation(App *app,
                             AppClient *client,
                             double *value,
                             double length,
                             easingFunction easing,
                             double target,
                             bool relayout);

void client_unregister_animation(App *app, AppClient *client_entity);

void client_close(App *app, AppClient *client_entity);

void client_close_threaded(App *app, AppClient *client_entity);

void client_paint(App *app, AppClient *client_entity);

void client_paint(App *app, AppClient *client, bool force_repaint);

void client_replace_root(App *app, AppClient *client_entity, Container *new_root);

void client_layout(App *app, AppClient *client_entity);

void handle_mouse_motion(App *app, AppClient *client, int x, int y);

int desktops_current(App *app);

int desktops_count(App *app);

void desktops_change(App *app, long desktop_index);

App *app_new();

void app_main(App *app);

void app_clean(App *app);

void set_active(Container *c, bool state);

void init_xkb(App *app, AppClient *client);

void process_xkb_event(xcb_generic_event_t *generic_event, ClientKeyboard *keyboard);

bool app_timeout_replace(App *app, AppClient *client, int timeout_file_descriptor, float timeout_ms,
                         void (*timeout_function)(App *, AppClient *, void *),
                         void *user_data);

int
app_timeout_create(App *app, AppClient *client, float timeout_ms, void (*timeout_function)(App *, AppClient *, void *),
                   void *user_data);

void app_create_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event));

void app_remove_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event));

bool client_set_position(App *app, AppClient *client, int x, int y);

bool client_set_size(App *app, AppClient *client, int w, int h);

bool client_set_position_and_size(App *app, AppClient *client, int x, int y, int w, int h);

void handle_configure_notify(App *app, AppClient *client, double x, double y, double w, double h);

void
send_key_actual(App *app, AppClient *client, Container *container, bool is_string, xkb_keysym_t keysym, char string[64],
                uint16_t mods, xkb_key_direction direction);

int
update_keymap(struct ClientKeyboard *kbd);

#endif