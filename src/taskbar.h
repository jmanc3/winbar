/* date = May 23rd 2020 5:18 pm */

#ifndef TASKBAR_H
#define TASKBAR_H

#include "application.h"
#include "audio.h"

class HoverableButton : public UserData {
public:
    bool hovered = false;
    double hover_amount = 0;
};

class IconButton : public HoverableButton {
public:
    cairo_surface_t *surface = nullptr;

    ~IconButton() { cairo_surface_destroy(surface); }
};

class LaunchableButton : public IconButton {
public:
    std::vector<xcb_window_t> windows;
    bool pinned = false;
    std::string class_name;
    std::string icon_name;
    bool has_launchable_info = false;
    std::string command_launched_by;
    int initial_mouse_click_before_drag_offset_x = 0;

    double active_amount = 0;
    double hover_amount = 0;

    // For icon lerping to correct position
    bool animating = false;
    double target = 0;
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

uint32_t
get_wm_state(xcb_window_t window);

void update_taskbar_volume_icon();

void set_textarea_active();

void set_textarea_inactive();

void register_popup(xcb_window_t window);

#endif// TASKBAR_H
