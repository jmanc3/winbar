/* date = May 23rd 2020 5:18 pm */

#ifndef TASKBAR_H
#define TASKBAR_H

#include <utility.h>
#include <xcb/xcb_aux.h>
#include "application.h"

class HoverableButton : public UserData {
public:
    bool hovered = false;
    double hover_amount = 0;
    ArgbColor color = ArgbColor(0, 0, 0, 0);
    int previous_state = -1;
    
    std::string text;
    std::string icon;
    
    int color_option = 0;
    ArgbColor actual_border_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_gradient_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_pane_color = ArgbColor(0, 0, 0, 0);
};

class IconButton : public HoverableButton {
public:
    cairo_surface_t *surface = nullptr;
    
    // These three things are so that opening menus with buttons toggles between opening and closing
    std::string invalidate_button_press_if_client_with_this_name_is_open;
    bool invalid_button_down = false;
    long timestamp = 0;
    
    ~IconButton() { cairo_surface_destroy(surface); }
};

class VolumeButton : public IconButton {
public:
    double volume = 1;
    bool muted = false;
};

struct ActionCenterButtonData : public IconButton {
    bool some_unseen = false;
    double slide_anim = 0;
};

class WindowsData {
public:
    
    xcb_window_t id = -1;
    
    std::string title;
    
    // Window is shown on screen
    bool mapped = true;
    
    bool wants_attention = false;
    
    // This is the surface that is linked to the actual window content
    cairo_surface_t *window_surface = nullptr;
    int width = -1;
    int height = -1;
    
    int gtk_left_margin = 0;
    int gtk_right_margin = 0;
    int gtk_top_margin = 0;
    int gtk_bottom_margin = 0;
    
    // This is where screenshots are stored every so often (if we could guarantee a compositor, we wouldn't need this.
    cairo_surface_t *raw_thumbnail_surface = nullptr;
    cairo_t *raw_thumbnail_cr = nullptr;
    
    long last_rescale_timestamp = 0;
    
    // This is where we rescale the screenshot to the correct thumbnail size
    cairo_surface_t *scaled_thumbnail_surface = nullptr;
    cairo_t *scaled_thumbnail_cr = nullptr;
    
    bool marked_to_close = false;
    
    ScreenInformation *on_screen = nullptr;
    int on_desktop = 0;
    
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
    double double_window_amount = 0;
    double wants_attention_amount = 0;
    bool wants_attention_just_finished = false;
    
    int color_option = 0;
    ArgbColor actual_border_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_gradient_color = ArgbColor(0, 0, 0, 0);
    ArgbColor actual_pane_color = ArgbColor(0, 0, 0, 0);
    
    selector_type type = selector_type::CLOSED;
    Timeout *possibly_open_timeout = nullptr;
    Timeout *possibly_stop_timeout = nullptr;
    
    // For icon lerping to correct position
    bool animating = false;
    double target = 0;
    
    double animation_zoom_amount = 0;
    double animation_zoom_locked = 0;
    long animation_zoom_locked_time = 0;
    
    int animation_bounce_direction = 0; // 0 is down, 1 is up
    double animation_bounce_amount = 0;
    
    ~LaunchableButton() {
        for (auto w: windows_data_list)
            delete w;
        windows_data_list.clear();
        windows_data_list.shrink_to_fit();
    }
};

AppClient *
create_taskbar(App *app);

void stacking_order_changed(xcb_window_t *all_windows, int windows_count);

void active_window_changed(xcb_window_t new_active_window);

void remove_non_pinned_icons();

void update_pinned_items_file(bool force_update);

void update_pinned_items_icon();

uint32_t
get_wm_state(xcb_window_t window);

void update_taskbar_volume_icon();

void set_textarea_active();

void set_textarea_inactive();

void register_popup(xcb_window_t window);

void taskbar_launch_index(int index);

xcb_window_t get_active_window();

void update_time(App *app, AppClient *client, Timeout *timeout, void *data);

void clear_thumbnails();

#endif// TASKBAR_H
