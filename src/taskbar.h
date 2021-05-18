/* date = May 23rd 2020 5:18 pm */

#ifndef TASKBAR_H
#define TASKBAR_H

#include <utility.h>
#include <xcb/xcb_aux.h>
#include "application.h"
#include "audio.h"

class HoverableButton : public UserData {
public:
    bool hovered = false;
    double hover_amount = 0;
    ArgbColor color = ArgbColor(0, 0, 0, 0);
    int previous_state = -1;
};

class IconButton : public HoverableButton {
public:
    cairo_surface_t *surface = nullptr;

    ~IconButton() { cairo_surface_destroy(surface); }
};

class WindowsData {
public:

    xcb_window_t id = -1;

    std::string title;

    // This is the surface that is linked to the actual window content
    cairo_surface_t *window_surface = nullptr;
    int width = -1;
    int height = -1;

    // This is where screenshots are stored every so often (if we could guarantee a compositor, we wouldn't need this.
    cairo_surface_t *raw_thumbnail_surface = nullptr;
    cairo_t *raw_thumbnail_cr = nullptr;

    long last_rescale_timestamp = 0;

    // This is where we rescale the screenshot to the correct thumbnail size
    cairo_surface_t *scaled_thumbnail_surface = nullptr;
    cairo_t *scaled_thumbnail_cr = nullptr;

    bool marked_to_close = false;

    WindowsData(App *app, xcb_window_t window);

    void take_screenshot();

    void rescale(double scale_w, double scale_h);

    ~WindowsData();
};

enum selector_type {
    CLOSED,
    OPEN_HOVERED,
    OPEN_CLICKED,
};

class LaunchableButton : public IconButton {
public:
    std::vector<WindowsData *> windows_data_list;

    bool pinned = false;
    std::string class_name;
    std::string icon_name;
    std::string user_icon_name;
    bool has_launchable_info = false;
    std::string command_launched_by;
    int initial_mouse_click_before_drag_offset_x = 0;

    double active_amount = 0;
    double hover_amount = 0;

    selector_type type = selector_type::CLOSED;
    int open_timeout_fd = -1;
    int close_timeout_fd = -1;

    // For icon lerping to correct position
    bool animating = false;
    double target = 0;

    ~LaunchableButton() {

    }
};

class wifi_surfaces : public HoverableButton {
public:
    cairo_surface_t *wireless_up = nullptr;
    cairo_surface_t *wireless_down = nullptr;
    cairo_surface_t *wired_up = nullptr;
    cairo_surface_t *wired_down = nullptr;

    ~wifi_surfaces() {
        if (wireless_up)
            cairo_surface_destroy(wireless_up);
        if (wireless_down)
            cairo_surface_destroy(wireless_down);
        if (wired_up)
            cairo_surface_destroy(wired_up);
        if (wired_down)
            cairo_surface_destroy(wired_down);
    }
};

AppClient *
create_taskbar(App *app);

void stacking_order_changed(xcb_window_t *all_windows, int windows_count);

void active_window_changed(xcb_window_t new_active_window);

void remove_non_pinned_icons();

void update_pinned_items_file();

void update_pinned_items_icon();

uint32_t
get_wm_state(xcb_window_t window);

void update_taskbar_volume_icon();

void set_textarea_active();

void set_textarea_inactive();

void register_popup(xcb_window_t window);

#endif// TASKBAR_H
