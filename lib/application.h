#ifndef APPLICATION_HEADER
#define APPLICATION_HEADER

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
    
    bool slide = false;
    // From: https://github.com/droidian/kwin/blob/15736367f6f533b075094e8ab384ee3b948f4cb9/effects/slidingpopups/slidingpopups.cpp
    // [1] Offset amount of pixels which are left empty at the beginning of the animation, and which return at the end
    // [2] Where the window comes from: 0 = Left, 1 = Top, 2 = Right, 3 = Bottom
    // [3] Animation-in duration in milliseconds
    // [4] Animation-out duration in milliseconds
    // [5] Start fading window after duration in milliseconds
    int slide_data[5] = {-1, 3, 160, 100, 80};
    
    bool skip_taskbar = true;
    bool no_input_focus = false;
    bool dock = false;
    bool tooltip = false;
    bool sticky = false;
    bool window_transparent = true;
    bool override_redirect = false;
    bool keep_above = false;
    bool on_close_is_unmap = false;
    
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
    
    void (*function)(App *, AppClient *, Timeout *, void *user_data);
    
    AppClient *client = nullptr;
    
    void *user_data = nullptr;
    
    bool keep_running = false;
    
    bool kill = false;
    std::string text;
};

struct PolledDescriptor {
    int file_descriptor;
    
    std::string text;
    
    void (*function)(App *, int fd, void *user_data);
    
    void *user_data = nullptr;
    
    ~PolledDescriptor() {
//        if (user_data) {
//            free(user_data);
//        }
    }
};

struct DBusConnection;

struct App {
    xcb_ewmh_connection_t ewmh;
    
    std::mutex running_mutex;
    bool running = true;
    
    Bounds bounds;// these are the bounds of the entire screen
    
    std::vector<AppClient *> clients;
    
    std::mutex thread_mutex;
    
    std::vector<Handler *> handlers;
    
    xcb_window_t grab_window;
    
    xcb_connection_t *connection = nullptr;
    
    xcb_key_symbols_t *key_symbols = nullptr;
    
    xcb_visualtype_t *argb_visualtype = nullptr;
    
    xcb_visualtype_t *root_visualtype = nullptr;
    
    int screen_number = 0;
    
    xcb_screen_t *screen = nullptr;

//    int epoll_fd = -1;
    std::vector<PolledDescriptor> descriptors_being_polled;
    
    std::vector<Timeout *> timeouts;
    
    int loop = 0;
    
    // TODO: move atoms into their own things
    xcb_atom_t protocols_atom = 0;
    
    xcb_atom_t delete_window_atom = 0;
    
    xcb_atom_t MOTIF_WM_HINTS = 0;
    
    cairo_device_t *device = nullptr;

    // Running on wayland
    bool wayland = false;
    
    App();
};

struct Handler {
    xcb_window_t target_window = 0;
    
    // returning false from this callback means don't consume the event
    bool (*event_handler)(App *app, xcb_generic_event_t *, xcb_window_t target_window) = nullptr;
};

void init_client(AppClient *client);

void destroy_client(AppClient *client);

bool valid_client(App *app, AppClient *target_client);

void handle_xcb_event(App *app, std::vector<xcb_window_t> *windows);

void handle_xcb_event(App *app, xcb_window_t window_number, xcb_generic_event_t *event, bool change_event_source);

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

void client_create_animation(App *app, AppClient *client_entity, double *value, std::shared_ptr<bool> lifetime, double delay, double length,
                             easingFunction easing, double target);

void client_create_animation(App *app,
                             AppClient *client_entity,
                             double *value,
                             std::shared_ptr<bool> lifetime,
                             double delay,
                             double length,
                             easingFunction easing,
                             double target,
                             void (*finished)(AppClient *client));

void
client_create_animation(App *app, AppClient *client, double *value, std::shared_ptr<bool> lifetime, double delay, double length, easingFunction easing,
                        double target, bool relayout);

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

void set_active(AppClient *client, Container *c, bool state);

void init_xkb(App *app, AppClient *client);

void process_xkb_event(xcb_generic_event_t *generic_event, ClientKeyboard *keyboard);

Timeout *app_timeout_replace(App *app, AppClient *client, Timeout *timeout, float timeout_ms,
                             void (*timeout_function)(App *, AppClient *, Timeout *, void *),
                             void *user_data);

Timeout *
app_timeout_create(App *app, AppClient *client, float timeout_ms,
                   void (*timeout_function)(App *, AppClient *, Timeout *, void *), void *user_data,
                   char *text);

bool app_timeout_stop(App *app,
                      AppClient *client,
                      Timeout *timeout);

void app_create_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event,
                                                            xcb_window_t target_window));

void app_remove_custom_event_handler(App *app, xcb_window_t window,
                                     bool (*custom_handler)(App *app, xcb_generic_event_t *event,
                                                            xcb_window_t target_window));

bool client_set_position(App *app, AppClient *client, int x, int y);

bool client_set_size(App *app, AppClient *client, int w, int h);

bool client_set_position_and_size(App *app, AppClient *client, int x, int y, int w, int h);

void handle_configure_notify(App *app, AppClient *client, double x, double y, double w, double h);

void
send_key_actual(App *app, AppClient *client, Container *container, bool is_string, xkb_keysym_t keysym, char string[64],
                uint16_t mods, xkb_key_direction direction);

int
update_keymap(struct ClientKeyboard *kbd);

void paint_container(App *app, AppClient *client, Container *container);

bool poll_descriptor(App *app, int file_descriptor, int events, void (*function)(App *, int, void *), void *user_data,
                     char* text);

Subprocess *
command_with_client(AppClient *client, const std::string &c, int timeout_in_ms, void (*function)(Subprocess *),
                    void *user_data);

#endif