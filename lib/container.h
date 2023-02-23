#ifndef CONTAINER_HEADER
#define CONTAINER_HEADER

#include <X11/keysym.h>
#include <cairo.h>
#include <string>
#include <utility>
#include <vector>
#include <xcb/xcb_event.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#define explicit dont_use_cxx_explicit

#include <xcb/xcb_keysyms.h>
#include <xcb/xkb.h>
#include <xcb/xcb_cursor.h>
#include "easing.h"

#undef explicit

static int FILL_SPACE = -1;
static int USE_CHILD_SIZE = -2;
static int DYNAMIC = -3;

struct Bounds {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    
    Bounds();
    
    Bounds(double x, double y, double w, double h);
    
    Bounds(const Bounds &b);
    
    bool non_zero();
    
    void shrink(double amount);
    
    void grow(double amount);
};

enum layout_type {
    hbox = 1 << 0,
    
    vbox = 1 << 1,
    
    stack = 1 << 2,
    
    scrollpane = 1 << 3,
    scrollpane_inline_r = 1 << 4,// optional flag for scrollpane to inline the right thumb
    scrollpane_inline_b = 1 << 4,// optional flag for scrollpane to inline the bottom thumb
    scrollpane_r_always = 1 << 5,
    scrollpane_r_sometimes = 1 << 6,
    scrollpane_r_never = 1 << 7,
    scrollpane_b_always = 1 << 8,
    scrollpane_b_sometimes = 1 << 9,
    scrollpane_b_never = 1 << 10,
    
    transition = 1 << 11,
    
    newscroll = 1 << 12,
};

enum container_alignment {
    ALIGN_NONE = 0,
    ALIGN_CENTER = 1 << 0,
    ALIGN_LEFT = 2 << 0,
    ALIGN_BOTTOM = 2 << 0,
    ALIGN_RIGHT = 2 << 0,
    ALIGN_TOP = 2 << 0,
};

struct UserData {
    virtual ~UserData() {};
};

struct MouseState {
    // This variables is used so that we minimize the amount of containers we are
    // dealing with when having to figure out what container gets what event
    bool concerned = false;
    
    // If the mouse is ____STRICTLY____ inside this container this VERY EXACT
    // moment
    bool mouse_hovering = false;
    
    // TODO: this data structure for pressing wont be able to correctly handle the
    // case where one mouse button is pressed on the container then another and
    // then a release
    
    // If this container was pressed by the mouse and its currently being held
    // down irregardless if the mouse is still inside this container
    bool mouse_pressing = false;
    
    // If after a mouse_down there was motion irregardless of current mouse
    // position
    bool mouse_dragging = false;
    
    // This variable will only be valid if mouse_pressing is true
    // it will be set to the events e->detail so test it against
    // XCB_BUTTON_INDEX_[0-9] left XCB_BUTTON_INDEX_1 = 1,
    //
    // middle
    // XCB_BUTTON_INDEX_2 = 2,
    //
    // right
    // XCB_BUTTON_INDEX_3 = 3,
    int mouse_button_pressed = 0;
    
    void reset() {
        this->concerned = false;
        this->mouse_hovering = false;
        this->mouse_pressing = false;
        this->mouse_dragging = false;
        this->mouse_button_pressed = 0;
    }
};

struct Container;

struct ClientKeyboard {
    xcb_connection_t *conn = nullptr;
    uint8_t first_xkb_event;
    struct xkb_context *ctx = nullptr;
    
    struct xkb_keymap *keymap = nullptr;
    struct xkb_state *state = nullptr;
    int32_t device_id;
    int balance = 0; // always needs to be above 0
};

struct App;

struct ScreenInformation {
    ScreenInformation() {
    
    }
    
    bool is_primary;
    int x, y;
    int width_in_pixels;
    int width_in_millimeters;
    int height_in_pixels;
    int height_in_millimeters;
    int rotation;
    uint8_t status;
    float dpi_scale = 1; // can be fractional
    xcb_window_t root_window;
    
    ScreenInformation(const ScreenInformation &p1) {
        is_primary = p1.is_primary;
        x = p1.x;
        y = p1.y;
        width_in_pixels = p1.width_in_pixels;
        width_in_millimeters = p1.width_in_millimeters;
        height_in_pixels = p1.height_in_pixels;
        height_in_millimeters = p1.height_in_millimeters;
        rotation = p1.rotation;
        status = p1.status;
        dpi_scale = p1.dpi_scale;
        root_window = p1.root_window;
    }
};

struct PopupSettings {
    bool is_popup = false;
    bool transparent_mouse_grab = true;
    bool takes_input_focus = false;
    bool ignore_scroll = false;
    bool close_on_focus_out = true;
    int close_delay_in_ms = 0;
    std::string name;
};

struct AppClient;

struct Settings;

struct Timeout;

struct ClientAnimation {
    double start_value{};
    double *value = nullptr;
    double length{};
    double target{};
    easingFunction easing = nullptr;
    long start_time{};
    bool relayout = false;
    
    bool done = false;
    
    void (*finished)(AppClient *client) = nullptr;
    
    double delay;
};

enum struct CommandStatus {
    NONE,
    UPDATE, // For commands that don't finish right away
    ERROR, // When command fails
    TIMEOUT, // When command times out
    FINISHED, // When command finishes
};

struct Subprocess {
    int inpipe[2]{};
    int outpipe[2]{};
    int timeout_fd = -1;
    pid_t pid;
    App *app = nullptr;
    
    std::string command;
    std::string output;
    std::string recent;
    CommandStatus status = CommandStatus::NONE; // UPDATE, FINISHED, ERROR, TIMEOUT
    void *user_data = nullptr;
    
    void (*function)(Subprocess *){};
    
    AppClient *client{};
    
    Subprocess(App *app, const std::string &command);
    
    void write(const std::string &message);
    
    void kill(bool warn);
};

struct AppClient {
    App *app = nullptr;
    
    // Information about the screen the client is on like DPI
    ScreenInformation *screen_information = nullptr;
    
    xcb_window_t window;
    
    std::string name;
    
    Bounds *bounds;
    
    Container *root;
    
    bool auto_delete_root = true;
    
    int mouse_initial_x = -1;
    int mouse_initial_y = -1;
    int mouse_current_x = -1;
    int mouse_current_y = -1;
    
    long last_repaint_time;
    
    // Variables to limit how often we handle motion notify events
    float motion_events_per_second = 120;
    int motion_event_x = 0;
    int motion_event_y = 0;
    Timeout *motion_event_timeout = nullptr;
    
    std::vector<ClientAnimation> animations;
    int animations_running = 0;
    float fps = 144;
    
    bool automatically_resize_on_dpi_change = false;
    
    // called after dpi_scale_factor and screen_information have been updated
    void (*on_dpi_change)(App *, AppClient *) = nullptr;
    
    // called when client moved to a different screen
    void (*on_screen_changed)(App *, AppClient *) = nullptr;
    
    // called when client moved to a different screen
    void (*on_screen_size_changed)(App *, AppClient *) = nullptr;
    
    void (*on_any_screen_change)(App *, AppClient *) = nullptr;
    
    float dpi() const {
        if (screen_information == nullptr) {
            return 1;
        }
        return screen_information->dpi_scale;
    }
    
    bool window_supports_transparency;
    cairo_t *cr = nullptr;
    xcb_colormap_t colormap;
    xcb_cursor_context_t *ctx;
    xcb_cursor_t cursor = -1;
    
    bool marked_to_close = false;
    
    ClientKeyboard *keyboard;
    
    void (*when_closed)(AppClient *client) = nullptr;
    
    bool keeps_app_running = true;
    
    PopupSettings popup_info;
    
    AppClient *child_popup = nullptr;
    
    bool wants_popup_events = false;
    
    AppClient *create_popup(PopupSettings popup_settings, Settings client_settings);
    
    std::vector<Subprocess *> commands;
    
    Subprocess *command(const std::string &command, void (*function)(Subprocess *));
    
    Subprocess *command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *));
    
    Subprocess *command(const std::string &command, void (*function)(Subprocess *), void *user_data);
    
    Subprocess *command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *), void *user_data);
};

struct ScrollContainer;
struct ScrollPaneSettings;

struct Container {
    // The parent of this container which must be set by the user whenever a
    // relationship is added
    Container *parent;
    
    // A user settable name that can be used for retrival
    std::string name;
    
    // List of this containers children;
    std::vector<Container *> children;
    
    // The way children are laid out
    int type = vbox;
    
    // A higher z_index will mean it will be rendered above everything else
    int z_index = 0;
    
    // Spacing between children when laying them out
    int spacing = 0;
    
    // Where you are placed inside the parent
    int alignment = 0;
    
    // These numbers are usually going to be negative
    // The underlying real scrolling offset along an axis
    double scroll_v_real = 0;
    double scroll_h_real = 0;
    
    double scroll_v_visual = 0;
    double scroll_h_visual = 0;
    
    // State of the mouse used by application.cpp to determine when to call this
    // containers when_* functions
    MouseState state;
    
    // User settable target bounds
    Bounds wanted_bounds;
    
    // User settable target padding for children of this container
    Bounds wanted_pad;
    
    // real_bounds is generated after calling layout on the root container and is
    // the bounds of this container
    Bounds real_bounds;
    
    // children_bounds is generated after calling layout on the root container and
    // is the bounds of the children since remember you can set a wanted_pad
    // amount
    Bounds children_bounds;
    
    // This variable can be set by layout parent to determine if it should be
    // rendered
    bool exists = true;
    
    // Variable meaning if we should layout the children whenever layout is called
    // on this container
    bool should_layout_children = true;
    
    // This doesn't actually do clipping on children to the parent containers
    // bounds when rendering, instead it tells us if we should call the render
    // function of non visible children containers
    bool clip_children = true;
    
    bool clip = false;
    
    bool interactable = true;
    
    bool draggable = true;
    
    // Is set to true when the container is the active last interacted with
    bool active = false;
    
    // If we should call when_clicked if this container was dragged
    bool when_drag_end_is_click = true;
    
    // How many pixels does a container need to be moved before dragging starts
    int minimum_x_distance_to_move_before_drag_begins = 0;
    
    // If the container should receive events through a single container above it
    // (children)
    bool receive_events_even_if_obstructed_by_one = false;
    
    // If the container should receive events through other containers above it
    // (children)
    bool receive_events_even_if_obstructed = false;
    
    // Do children get painted
    bool automatically_paint_children = true;
    
    void *user_data = nullptr;
    
    // Called when client needs to repaint itself
    void (*when_paint)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when the mouse enters the container for the first time
    void (*when_mouse_enters_container)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called every time the mouse moves and its inside the container unless if
    // its being dragged
    void (*when_mouse_motion)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when the mouse is no longer inside the container, or if
    // mouse_down happend, will be called when mouse_up happens
    void (*when_mouse_leaves_container)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once if left_mouse, middle_mouse, or right_mouse is pressed down
    // inside this container
    void (*when_mouse_down)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // TODO: is this really the behaviour we want????
    // Called once when the mouse_down is released regardless if the mouse is
    // actually inside the container it initially mouse_downed on
    void (*when_mouse_up)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when this container was mouse_downed and then mouse_upped regardless
    // of any motion the mouse did in between those two events
    void (*when_clicked)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when the containers status is changed
    void (*when_active_status_changed)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when this container was scrolled on
    void (*when_scrolled)(AppClient *client,
                          cairo_t *cr,
                          Container *self,
                          int scroll_x,
                          int scroll_y) = nullptr;
    
    // Called once when after mouse_downing a container, there was a mouse_motion
    // event
    void (*when_drag_start)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called everytime when after mouse_downing a container, there where mouse
    // motion events until a mouse_up
    void (*when_drag)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when after dragging a container the mouse_up happens
    void (*when_drag_end)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // If this function is set, it'll be called to determine if the container is
    // pierced
    bool (*handles_pierced)(Container *container, int mouse_x, int mouse_y) = nullptr;
    
    // When layout is called on this container and generate_event is true on that
    // call
    void (*when_layout)(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                        double *target_h) = nullptr;
    
    void (*when_key_event)(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                           char string[64],
                           uint16_t mods, xkb_key_direction direction) = nullptr;
    
    Container *child(int wanted_width, int wanted_height);
    
    Container *child(int type, int wanted_width, int wanted_height);
    
    Container();
    
    Container(layout_type type, double wanted_width, double wanted_height);
    
    Container(double wanted_width, double wanted_height);
    
    Container(const Container &c);
    
    virtual ~Container();
    
    ScrollContainer *scrollchild(const ScrollPaneSettings &scroll_pane_settings);
};

enum ScrollShow {
    SAlways,
    SWhenNeeded,
    SNever
};

class ScrollPaneSettings : public UserData {
public:
    ScrollPaneSettings(float scale);
    
    int right_width = 15;
    int right_arrow_height = 15;
    
    int bottom_height = 15;
    int bottom_arrow_width = 15;
    
    bool right_inline_track = false;
    bool bottom_inline_track = false;
    
    // 0 is always show, 1 is when needed, 2 is never show
    int right_show_amount = ScrollShow::SWhenNeeded;
    int bottom_show_amount = ScrollShow::SWhenNeeded;
    
    bool make_content = false;
    
    bool start_at_end = false;
    
    // paint functions
};

struct ScrollContainer : public Container {
    Container *content = nullptr;
    Container *right = nullptr;
    Container *bottom = nullptr;
    ScrollPaneSettings settings;
    
    explicit ScrollContainer(ScrollPaneSettings settings) : settings(std::move(settings)) {
        type = ::newscroll;
        wanted_bounds.w = FILL_SPACE;
        wanted_bounds.h = FILL_SPACE;
    }
    
    ~ScrollContainer() {
        delete content;
        delete right;
        delete bottom;
    }
};

Bounds
scroll_bounds(Container *container);

void layout(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds);

Container *
container_by_name(std::string name, Container *root);

Container *
container_by_container(Container *target, Container *root);

bool overlaps(Bounds a, Bounds b);

bool bounds_contains(const Bounds &bounds, int x, int y);

double
reserved_width(Container *box);

double
reserved_height(Container *box);

double
true_height(Container *box);

double
true_width(Container *box);

double
actual_true_height(Container *box);

double
actual_true_width(Container *box);

void clamp_scroll(ScrollContainer *scrollpane);

#endif
