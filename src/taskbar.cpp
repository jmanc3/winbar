
#include "taskbar.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include "app_menu.h"
#include "battery_menu.h"
#include "components.h"
#include "config.h"
#include "date_menu.h"
#include "icons.h"
#include "main.h"
#include "pinned_icon_right_click.h"
#include "root.h"
#include "search_menu.h"
#include "systray.h"
#include "volume_menu.h"
#include "wifi_menu.h"
#include "windows_selector.h"
#include "hsluv.h"
#include "globals.h"
#include "action_center_menu.h"
#include "notifications.h"
#include "simple_dbus.h"
#include "audio.h"
#include "defer.h"
#include "bluetooth_menu.h"
#include "plugins_menu.h"
#include "chatgpt.h"
#include "settings_menu.h"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <pango/pangocairo.h>
#include <xcb/xproto.h>
#include <dpi.h>
#include <sys/inotify.h>
#include "utility.h"
#include "pinned_icon_editor.h"

#define scale_ratio (config->taskbar_height / (35.0 * config->dpi))

static Container *active_container = nullptr;
static xcb_window_t active_window = 0;
static xcb_window_t backup_active_window = 0;
static bool someone_is_fullscreen_and_covering = false;
static int times_painted = 0;

static std::string time_text("N/A");

static xcb_window_t popup_window_open = -1;

static int max_resize_attempts = 10;
static int resize_attempts = 0;
static int pixel_spacing = 1;

static void
paint_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_icon_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_date(AppClient *client, cairo_t *cr, Container *container);

static void
paint_search(AppClient *client, cairo_t *cr, Container *container);

static void
fill_root(Container *root);

static void
late_classes_update(App *app, AppClient *client, Timeout *, void *data);

static void
validate_layout(AppClient *client, PangoLayout *layout) {

}

// Trim leading and trailing whitespace from a string in-place
std::string trim(std::string str) {
    std::string copy = str;
    // Remove leading whitespace
    copy.erase(copy.begin(), std::find_if(copy.begin(), copy.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    
    // Remove trailing whitespace
    copy.erase(std::find_if(copy.rbegin(), copy.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), copy.end());
    
    return copy;
}

std::string trimnewlines(std::string str) {
    std::string copy = str;
    // Remove leading whitespace
    copy.erase(copy.begin(), std::find_if(copy.begin(), copy.end(), [](int ch) {
        return !std::isspace(ch) && ch != '\r' && ch != '\n';
    }));
    
    // Remove trailing whitespace
    copy.erase(std::find_if(copy.rbegin(), copy.rend(), [](int ch) {
        return !std::isspace(ch) && ch != '\r' && ch != '\n';
    }).base(), copy.end());
    
    return copy;
}

static int
icon_width(AppClient *client) {
    int max_container_w = (int) (client->bounds->h + 4 * config->dpi);
    bool max_container_even = max_container_w % 2 == 0;
    
    int tentative_icon_size = (int) (24.0 * config->dpi * (scale_ratio));
    bool tentative_even = tentative_icon_size % 2 == 0;
    
    // So that we are pixel perfectly centered
    if ((max_container_even && !tentative_even) || (!max_container_even && tentative_even)) {
        tentative_icon_size++;
    }
    
    return tentative_icon_size;
}

static void
reserve(AppClient *client, int amount) {
    xcb_ewmh_wm_strut_partial_t wm_strut = {};
    wm_strut.bottom = amount;
    wm_strut.bottom_start_x = client->bounds->x;
    wm_strut.bottom_end_x = client->bounds->w;
    xcb_ewmh_set_wm_strut_partial(&client->app->ewmh,
                                  client->window,
                                  wm_strut);
    
    xcb_ewmh_set_wm_strut(&client->app->ewmh,
                          client->window,
                          0,
                          0,
                          0,
                          amount);

}

static void
paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (times_painted == 0) {
        times_painted++;
        std::thread t([client]() -> void {
            reserve(client, client->bounds->h);
        });
        t.detach();
    }
    
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_taskbar_background));
    cairo_fill(cr);
    
    if (winbar_settings->pinned_icon_style == "win11") {
        set_rect(cr, Bounds(container->real_bounds.x, container->real_bounds.y, container->real_bounds.w,
                            1 * std::floor(config->dpi)));
        set_argb(cr, correct_opaqueness(client, config->color_taskbar_search_bar_default_border));
        cairo_fill(cr);
    } else if (winbar_settings->pinned_icon_style == "win7") {
        auto h = 1 * std::floor(config->dpi);
        set_rect(cr, Bounds(container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, h));
        set_argb(cr, ArgbColor(0.0, 0.0, 0.0, 0.7));
        cairo_fill(cr);
        set_rect(cr, Bounds(container->real_bounds.x, container->real_bounds.y + h, container->real_bounds.w, h));
        set_argb(cr, ArgbColor(1.0, 1.0, 1.0, 0.425));
        cairo_fill(cr);
    }
}

static void
paint_right_click_popup_background(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_taskbar_background));
    cairo_fill(cr);
    bool is_light_theme = false;
    {
        double h; // hue
        double s; // saturation
        double p; // perceived brightness
        ArgbColor real = config->color_taskbar_application_icons_background;
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
        is_light_theme = p > 50; // if the perceived perceived brightness is greater than that we are a light theme
    }
    set_rect(cr, container->real_bounds);
    if (is_light_theme) {
        set_argb(cr, correct_opaqueness(client, darken(config->color_taskbar_background, 30)));
    } else {
        set_argb(cr, correct_opaqueness(client, lighten(config->color_taskbar_background, 30)));
    }
    cairo_set_line_width(cr, std::round(1 * config->dpi));
    cairo_stroke(cr);
}

static void
paint_hoverable_button_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (HoverableButton *) container->user_data;
    
    auto default_color = config->color_taskbar_button_default;
    auto hovered_color = config->color_taskbar_button_hovered;
    auto pressed_color = config->color_taskbar_button_pressed;
    
    bool hovered = false;
    if (client->previous_x != -1 && client->mouse_current_x > 0) {
        Bounds mouse_bounds(0, 0, 1, 1);
        if (client->previous_x < client->motion_event_x) {
            mouse_bounds.x = client->previous_x;
            mouse_bounds.w = client->motion_event_x - client->previous_x;
        } else {
            mouse_bounds.x = client->motion_event_x;
            mouse_bounds.w = client->previous_x - client->motion_event_x;
        }
        if (client->previous_y < client->motion_event_y) {
            mouse_bounds.y = client->previous_y;
            mouse_bounds.h = client->motion_event_y - client->previous_y;
        } else {
            mouse_bounds.y = client->motion_event_y;
            mouse_bounds.h = client->previous_y - client->motion_event_y;
        }
        hovered = overlaps(container->real_bounds, mouse_bounds);
    }
    
    auto e = getEasingFunction(easing_functions::EaseOutQuad);
    double time = 0;
    if (container->state.mouse_pressing || container->state.mouse_hovering || hovered) {
        if (container->state.mouse_pressing) {
            if (data->previous_state != 2) {
                data->previous_state = 2;
                client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, pressed_color.r);
                client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, pressed_color.g);
                client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, pressed_color.b);
                client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, pressed_color.a);
            }
        } else if (data->previous_state != 1) {
            data->previous_state = 1;
            client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, hovered_color.r);
            client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, hovered_color.g);
            client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, hovered_color.b);
            client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, hovered_color.a);
        }
    } else if (data->previous_state != 0) {
        time = 100;
        data->previous_state = 0;
        e = getEasingFunction(easing_functions::EaseInCirc);
        client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, default_color.r);
        client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, default_color.g);
        client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, default_color.b);
        client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, default_color.a);
    }
    
    set_argb(cr, data->color);
    
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y,
                    container->real_bounds.w,
                    container->real_bounds.h);
    cairo_fill(cr);
}

struct SuperButton : IconButton {
    bool was_success = false;
    bool already_attempted = false;
    ArgbColor current_dye = ArgbColor(0, 0, 0, 0);
};

static void
paint_super(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    SuperButton *data = (SuperButton *) container->user_data;
    
    paint_hoverable_button_background(client, cr, container);
    
    if (winbar_settings->super_icon_default) {
        float icon_size = icon_width(client);
        if (!data->already_attempted) {
            data->already_attempted = true;
            data->surface = accelerated_surface(app, client, icon_size, icon_size);
            data->was_success = paint_surface_with_image(data->surface, as_resource_path("applications.png"), icon_size,nullptr);
        }
        if (data->surface) {
            if (container->state.mouse_pressing) {
                if (data->current_dye != config->color_taskbar_windows_button_pressed_icon) {
                    data->current_dye = config->color_taskbar_windows_button_pressed_icon;
                    dye_surface(data->surface, data->current_dye);
                }
            } else if (container->state.mouse_hovering) {
                if (data->current_dye != config->color_taskbar_windows_button_hovered_icon) {
                    data->current_dye = config->color_taskbar_windows_button_hovered_icon;
                    dye_surface(data->surface, data->current_dye);
                }
            } else {
                if (data->current_dye != config->color_taskbar_windows_button_default_icon) {
                    data->current_dye = config->color_taskbar_windows_button_default_icon;
                    dye_surface(data->surface, data->current_dye);
                }
            }
            
            cairo_set_source_surface(cr, data->surface,container->real_bounds.x + container->real_bounds.w / 2  - icon_size / 2,
                          container->real_bounds.y + container->real_bounds.h / 2  - icon_size / 2);
            cairo_paint(cr);
        }
        
        if (data->was_success)
            return;
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    pango_layout_set_text(layout, "\uE782", strlen("\uE83F"));
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (container->state.mouse_pressing) {
        set_argb(cr, config->color_taskbar_windows_button_pressed_icon);
    } else if (container->state.mouse_hovering) {
        set_argb(cr, config->color_taskbar_windows_button_hovered_icon);
    } else {
        set_argb(cr, config->color_taskbar_windows_button_default_icon);
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_volume(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client, cr, container);
    container->real_bounds = start;
    auto *data = (VolumeButton *) container->user_data;
    
    int val = std::round(data->volume * 100);
    bool mute_state = data->muted;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);

    int volume_icon_width;
    int height;
    pango_layout_set_text(layout, "\uEBC5", strlen("\uE83F"));
    pango_layout_get_pixel_size_safe(layout, &volume_icon_width, &height);

    if (!mute_state) {
        ArgbColor volume_bars_color = ArgbColor(.4, .4, .4, 1);
        set_argb(cr, volume_bars_color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + (container->real_bounds.w - 12 * config->dpi) -
                             volume_icon_width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (mute_state) {
        pango_layout_set_text(layout, "\uE74F", strlen("\uE83F"));
    } else if (val == 0) {
        pango_layout_set_text(layout, "\uE992", strlen("\uE83F"));
    } else if (val < 33) {
        pango_layout_set_text(layout, "\uE993", strlen("\uE83F"));
    } else if (val < 66) {
        pango_layout_set_text(layout, "\uE994", strlen("\uE83F"));
    } else {
        pango_layout_set_text(layout, "\uE995", strlen("\uE83F"));
    }

    pango_layout_get_pixel_size_safe(layout, &volume_icon_width, &height);

    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + (container->real_bounds.w - 12 * config->dpi) -
                         volume_icon_width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
   
    if (((container->state.mouse_hovering || container->state.mouse_pressing ||
          container->state.mouse_dragging) && winbar_settings->volume_expands_on_hover) ||
        winbar_settings->volume_label_always_on) {
        std::string text = std::to_string(val) + "%";
        // Draw percentage when hovered
        PangoLayout *percentage =
                get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        validate_layout(client, layout);
        pango_layout_set_text(percentage, text.c_str(), text.size());

        int width;
        bool resize = false;
        pango_layout_get_pixel_size_safe(percentage, &width, &height);
        if (data->previous_volume_width != width) {
            data->already_expanded = false;
            data->previous_volume_width = width;
            resize = true;
        }

        float speed = 110.0f;
        if (!data->already_expanded) {
            if (resize) {
                speed = 40.0f;
            } else {
                data->start_time = get_current_time_in_ms();
            }
            
            data->already_expanded = true;
            client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, speed, nullptr,
                                    (config->dpi * 24) + (width + 7 * config->dpi), true);
        }

        ArgbColor color = config->color_taskbar_button_icons;
        color.a = (get_current_time_in_ms() - data->start_time) / speed;
        if (color.a > 1)
            color.a = 1;
        set_argb(cr, color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + 5 * config->dpi),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, percentage);
    } else {
        if (data->already_expanded) {
            data->already_expanded = false;
            client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, 100.0f, nullptr, 24 * config->dpi, true);
        }
    }
}

static void
paint_workspace(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    set_argb(cr, config->color_taskbar_button_icons);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        pango_layout_set_text(layout, "\uEB91", strlen("\uE83F"));
    } else {
        pango_layout_set_text(layout, "\uE7C4", strlen("\uE83F"));
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_chatgpt(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (IconButton *) container->user_data;
    paint_hoverable_button_background(client, cr, container);
    
    if (data->surface) {
        double w = cairo_image_surface_get_width(data->surface);
        double h = cairo_image_surface_get_height(data->surface);
        cairo_set_source_surface(cr, data->surface,
                                 container->real_bounds.x + container->real_bounds.w / 2 - w / 2,
                                 container->real_bounds.y + container->real_bounds.h / 2 - h / 2);
        cairo_paint(cr);
    }
}


static void
clicked_chatgpt(AppClient *client, cairo_t *cr, Container *container) {
    start_chatgpt_menu();
}

static void
paint_double_bar(cairo_t *cr, Container *container, ArgbColor bar_l_c, ArgbColor bar_m_c, ArgbColor bar_r_c,
                 int windows_count) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = (LaunchableButton *) container->user_data;
    
    double bar_amount = std::max(data->hover_amount, data->active_amount);
    if (data->type != selector_type::CLOSED) {
        bar_amount = 1;
    }
    if (data->wants_attention_amount != 0) {
        bar_amount = 1;
    }
    
    double squish_factor = std::round(4 * config->dpi);
    double bar_inset = squish_factor * (1 - bar_amount);
    
    Bounds bounds = container->real_bounds;
    float height = std::round(config->dpi * 2) + 1;
    // setting the appropriate height
    bounds.y = bounds.y + bounds.h - height;
    bounds.h = height;
    
    // squishing the width
    bounds.x += bar_inset;
    bounds.w -= bar_inset * 2;
    
    set_rect(cr, bounds);
    ArgbColor r = bar_r_c;
    set_argb(cr, r);
    cairo_fill(cr);
    
    bounds.w -= std::round(3 * config->dpi);
    
    set_rect(cr, bounds);
    ArgbColor m = bar_m_c;
    set_argb(cr, m);
    cairo_fill(cr);
    
    bounds.w -= std::round(1 * config->dpi);
    
    set_rect(cr, bounds);
    ArgbColor l = bar_l_c;
    set_argb(cr, l);
    cairo_fill(cr);
}

static void
paint_double_bg_with_opacity(cairo_t *cr, Bounds bounds, ArgbColor bg_l_c, ArgbColor bg_m_c, ArgbColor bg_r_c,
                             double opacity, int windows_count) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    set_rect(cr, bounds);
    ArgbColor r = bg_r_c;
    set_argb(cr, r);
    cairo_fill(cr);
    
    bounds.w -= std::round(3 * config->dpi);
    
    set_rect(cr, bounds);
    ArgbColor m = bg_m_c;
    set_argb(cr, m);
    cairo_fill(cr);
    
    bounds.w -= std::round(1 * config->dpi);
    
    set_rect(cr, bounds);
    ArgbColor l = bg_l_c;
    set_argb(cr, l);
    cairo_fill(cr);
}

static void
paint_double_bg(cairo_t *cr, Bounds bounds, ArgbColor bg_l_c, ArgbColor bg_m_c, ArgbColor bg_r_c, int windows_count) {
    paint_double_bg_with_opacity(cr, bounds, bg_l_c, bg_m_c, bg_r_c, 1, windows_count);
}

static int get_label_width(AppClient *client, Container *container) {
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    std::string title;
    if (!data->windows_data_list.empty())
        title = data->windows_data_list[0]->title;
    if (winbar_settings->labels && !data->windows_data_list.empty() && !trim(title).empty()) {
        PangoLayout *text_layout =
                get_cached_pango_font(client->cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        auto pad = 8.0f * config->dpi;
        pango_layout_set_text(text_layout, title.c_str(), -1);
        pango_layout_set_wrap(text_layout, (PangoWrapMode) -1);
        pango_cairo_update_layout(client->cr, text_layout);
        
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(text_layout, &ink, &logical);
        
        auto label_width = logical.width / PANGO_SCALE;
        if (label_width > client->bounds->w / 12)
            label_width = client->bounds->w / 12;
        if (winbar_settings->label_uniform_size)
            label_width = client->bounds->w / 12;
        return client->bounds->h + 4 * config->dpi + pad * 2 + label_width;
    } else if (winbar_settings->pinned_icon_style == "win7") {
        return 60 * config->dpi;
    } else {
        return client->bounds->h + 4 * config->dpi;
    }
}

static void update_wanted_width(AppClient *client, Container *container) {
    request_refresh(app, client);
}

static void
paint_icon_label(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    if (!data->windows_data_list.empty() && winbar_settings->labels) {
        PangoLayout *text_layout =
                get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        auto pad = 8.0f * config->dpi;
        double xpos = 0;
        double w = 0;
        if (data->surface) {
            w = cairo_image_surface_get_width(data->surface);
            pad = container->real_bounds.h - w;
        }
        pango_layout_set_text(text_layout, data->windows_data_list[0]->title.c_str(), -1);
        pango_layout_set_width(text_layout, (container->real_bounds.w - 55 * config->dpi) * PANGO_SCALE);
        pango_layout_set_alignment(text_layout, PangoAlignment::PANGO_ALIGN_LEFT);
        defer(pango_layout_set_width(text_layout, -1));
        pango_layout_set_ellipsize(text_layout, PangoEllipsizeMode::PANGO_ELLIPSIZE_END);
        pango_cairo_update_layout(cr, text_layout);
        
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(text_layout, &ink, &logical);
        
        set_argb(cr, config->color_taskbar_date_time_text);
        cairo_move_to(cr, container->real_bounds.x + 14 * config->dpi + w,
                      container->real_bounds.y + container->real_bounds.h / 2 - (logical.height / PANGO_SCALE) / 2);
        
        pango_cairo_show_layout(cr, text_layout);
        pango_layout_set_width(text_layout, -1);
    }
}

double bounce_slam_animation(double input) {
    if (input < 0.0) {
        input = 0.0;
    } else if (input > 1.0) {
        input = 1.0;
    }
    
    // Define segment boundaries
    const double firstSegment = 0.25;  // 30%
    const double secondSegment = 0.7; // 30% + 50% = 80%
    const double lastSegment = 1.0;   // 100%
    
    if (input < firstSegment) {
        auto first = input / firstSegment;
        auto ease = getEasingFunction(easing_functions::EaseInSine);
        return ease(first); // linearly increase from 0 to 1
    } else if (input < secondSegment) {
        return 1.0; // stay at 1
    } else {
        auto ease = getEasingFunction(easing_functions::EaseOutQuart);
        return ease(1.0 - (input - secondSegment) / (lastSegment - secondSegment)); // linearly decrease from 1 to 0
    }
}

static void
paint_icon_surface(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    if (data->surface) {
        cairo_save(cr);
        double scale_afterwards = .81;
        double surface_width = cairo_image_surface_get_width(data->surface);
        double w = surface_width;
        
        auto scale_amount = 1 - (data->animation_zoom_amount * (1 - scale_afterwards));
        if (data->animation_zoom_locked) {
            auto current = get_current_time_in_ms();
            auto elapsed = current - data->animation_zoom_locked_time;
            double total_time = 500;
            elapsed %= (long) total_time;
            elapsed -= total_time / 2;
            double scalar = ((double) elapsed) / (total_time / 2);
            if (scalar < 0)
                scalar = -scalar;
            auto grow_factor = .22 * scalar;
            
            auto new_amount = 1 - (data->animation_zoom_locked * (1 - .61)) + grow_factor;
            
            scale_amount = (new_amount + scale_amount) / 2;
        }
        double current_w = w * scale_amount;

        double xpos = container->real_bounds.x + container->real_bounds.w / 2 - surface_width / 2;
        double ypos = container->real_bounds.y + container->real_bounds.h / 2 -surface_width / 2;
        if (winbar_settings->labels && !data->windows_data_list.empty()) {
            auto title = data->windows_data_list[0]->title;
            if (!trim(title).empty()) {
                xpos = container->real_bounds.x + 8 * config->dpi;
            }
        }
        
        xpos += (w - current_w) / 2;
        ypos += (w - current_w) / 2;
    
        cairo_scale(cr, scale_amount, scale_amount);
        double xpostrans = (xpos * (1 - scale_amount) / scale_amount);
        double ypostrans = (ypos * (1 - scale_amount) / scale_amount);
        cairo_translate(cr, xpostrans, ypostrans);
        // Assumes the size of the icon to be 24x24 and tries to draw it centered
        if (data->animation_bounce_amount == 1 || data->windows_data_list.empty())
            data->animation_bounce_amount = 0;
        auto amount = data->animation_bounce_amount * 1.9;
        double bounce_amount = bounce_slam_animation(amount);
        if (data->animation_bounce_direction)
            bounce_amount = -bounce_amount;
        if (!winbar_settings->minimize_maximize_animation)
            bounce_amount = 0;
        
        double off = (((config->taskbar_height - w) - (11 * config->dpi)) / 2) * (bounce_amount);
        double draw_x = xpos - 1;
        cairo_set_source_surface(cr, data->surface, std::round(draw_x), ypos + off);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

static void animate_color_change(App *app, AppClient *client, ArgbColor *current_color, ArgbColor target_color) {
    if (current_color->a == 0) {
        // If the target_color is going to be lerping from invisible, then we should just immediately set chroma and just let the opacity animate
        current_color->r = target_color.r;
        current_color->g = target_color.g;
        current_color->b = target_color.b;
    } else if (target_color.a != 0) { // If the target color is invisible, then we shouldn't change our rgb values
        client_create_animation(app, client, &current_color->r, current_color->lifetime, 0, 16.67 * 5.8, nullptr, target_color.r);
        client_create_animation(app, client, &current_color->g, current_color->lifetime, 0, 16.67 * 5.8, nullptr, target_color.g);
        client_create_animation(app, client, &current_color->b, current_color->lifetime, 0, 16.67 * 5.8, nullptr, target_color.b);
    }
    client_create_animation(app, client, &current_color->a, current_color->lifetime, 0, 16.67 * 5.8, nullptr, target_color.a);
}

static void paint_pinnned_icon_border(cairo_t *cr, Bounds bounds, double radius, double width, ArgbColor color) {
    cairo_push_group(cr);
    set_argb(cr, ArgbColor(1, 1, 1, 1));
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    bounds.shrink(width);
    set_argb(cr, ArgbColor(0, 0, 0, 0));
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    bounds.grow(width);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_pattern_t *outline_mask = cairo_pop_group(cr);
    defer(cairo_pattern_destroy(outline_mask));
    
    cairo_push_group(cr);
    set_argb(cr, color);
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    cairo_pattern_t *outline_no_gradient = cairo_pop_group(cr);
    defer(cairo_pattern_destroy(outline_no_gradient));
    cairo_set_source(cr, outline_no_gradient);
    cairo_mask(cr, outline_mask);
}

static void paint_pinnned_icon_gradient(cairo_t *cr, Bounds bounds, double radius, double width, ArgbColor color) {
    cairo_push_group(cr);
    set_argb(cr, ArgbColor(1, 1, 1, 1));
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    bounds.shrink(width);
    set_argb(cr, ArgbColor(0, 0, 0, 0));
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    bounds.grow(width);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_pattern_t *outline_mask = cairo_pop_group(cr);
    defer(cairo_pattern_destroy(outline_mask));
    
    cairo_push_group(cr);
    {
        cairo_push_group(cr);
        set_argb(cr, color);
        rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
        cairo_fill(cr);
        cairo_pattern_t *outline_cast_pattern = cairo_pop_group(cr);
        defer(cairo_pattern_destroy(outline_cast_pattern));
        
        // Paint the outline with the outline mask
        cairo_set_source(cr, outline_cast_pattern);
        cairo_mask(cr, outline_mask);
    }
    cairo_pattern_t *outline_pattern = cairo_pop_group(cr);
    defer(cairo_pattern_destroy(outline_pattern));
    
    cairo_push_group(cr);
    cairo_pattern_t *pat3 = cairo_pattern_create_linear(bounds.x, bounds.y, bounds.x,
                                                        bounds.y + radius * 1.5);
    defer(cairo_pattern_destroy(pat3));
    cairo_pattern_add_color_stop_rgba(pat3, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(pat3, 1, 0, 0, 0, 0);
    cairo_rectangle(cr, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_set_source(cr, pat3);
    cairo_fill(cr);
    cairo_pattern_t *mask_pattern = cairo_pop_group(cr);
    defer(cairo_pattern_destroy(mask_pattern));
    
    cairo_set_source(cr, outline_pattern);
    cairo_mask(cr, mask_pattern);
}

static void paint_pinnned_icon_pane(cairo_t *cr, Bounds bounds, double radius, double width, ArgbColor color) {
    bounds.shrink(width);
    
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    set_argb(cr, color);
    rounded_rect(cr, radius, bounds.x, bounds.y, bounds.w, bounds.h);
    cairo_fill(cr);
    cairo_restore(cr);
    
    bounds.grow(width);
}

static void
paint_icon_background_win11(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (LaunchableButton *) container->user_data;
    Bounds bounds = container->real_bounds;
    
    double corner_radius = 0;
    double outline_width = 1 * std::floor(config->dpi);
    
    double p; // perceived brightness
    {
        double h; // hue
        double s; // saturation
        ArgbColor real = config->color_taskbar_background;
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
    }
    
    int windows_count = data->windows_data_list.size();
    bool active = active_container == container;
    bool pressed = container->state.mouse_pressing;
    bool hovered = container->state.mouse_hovering || (data->type != selector_type::CLOSED);
    
    bool dragging = container->state.mouse_dragging;
    ArgbColor accent = config->color_taskbar_application_icons_accent;
    ArgbColor accent_dark = p < 50 ? ArgbColor(.604, .604, .604, 1) : ArgbColor(.524, .524, .524, 1);
    ArgbColor background = ArgbColor(1, 1, 1, 0);
    
    ArgbColor border_color = background;
    ArgbColor gradient_color = background;
    ArgbColor pane_color = background;
    
    int current_color_option = 1;
    
    // Maximum boost should be times 5 since the maximum alpha value is .2
    double boost = 4 * (p / 100);
    
    if (active) {
        if (pressed) {
            if (!dragging) {
                current_color_option = 6;
                border_color.a = .08 + (.08 * boost);
                gradient_color.a = .08 + (.08 * boost);
                pane_color.a = .08 + (.08 * boost);
            }
        } else if (hovered) {
            current_color_option = 5;
            border_color.a = .13 + (.13 * boost);
            gradient_color.a = .2 + (.2 * boost);
            pane_color.a = .15 + (.15 * boost);
        } else {
            current_color_option = 4;
            border_color.a = .13 + (.13 * boost);
            gradient_color.a = .13 + (.13 * boost);
            pane_color.a = .1 + (.1 * boost);
        }
    } else if (pressed) {
        if (!dragging) {
            current_color_option = 3;
            border_color.a = .1 + (.1 * boost);
            gradient_color.a = .1 + (.1 * boost);
            pane_color.a = .1 + (.1 * boost);
        }
    } else if (hovered) {
        current_color_option = 2;
        border_color.a = .1 + (.1 * boost);
        gradient_color.a = .14 + (.14 * boost);
        pane_color.a = .1 + (.1 * boost);
    }
    
    bool just_finished = data->wants_attention_just_finished;
    if (current_color_option != 1) {
        if (current_color_option != data->color_option || just_finished) {
            data->wants_attention_just_finished = false;
            
            int previous_color_option = data->color_option;
            data->color_option = current_color_option;
            
            if (current_color_option == 2 ||
                current_color_option == 1) { // In certain cases we want the border and gradient to be instant
                data->actual_border_color = border_color;
                data->actual_gradient_color = gradient_color;
            } else {
                animate_color_change(app, client, &data->actual_border_color, border_color);
                animate_color_change(app, client, &data->actual_gradient_color, gradient_color);
            }
            
            animate_color_change(app, client, &data->actual_pane_color, pane_color);
        }
    }
    
    if (data->wants_attention_amount != 0 && !just_finished) {
        double blinks = 10.5;
        double scalar = fmod(data->wants_attention_amount, (1.0 / blinks)); // get N blinks
        scalar *= blinks;
        if (scalar > .5)
            scalar = 1 - scalar;
        scalar *= 2;
        if (data->wants_attention_amount == 1)
            scalar = 1;
        
        accent = lerp_argb(scalar, accent_dark, config->color_taskbar_attention_accent);
        accent_dark = accent;
        
        pane_color = config->color_taskbar_attention_background;
        pane_color.a = 0;
        data->actual_gradient_color.a = 0;
        data->actual_pane_color = lerp_argb(scalar, pane_color, config->color_taskbar_attention_background);
        data->actual_border_color = data->actual_pane_color;
    }
    
    if (current_color_option != 1) {
        cairo_push_group(cr);
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        if (data->actual_border_color.a != 0)
            paint_pinnned_icon_border(cr, bounds, corner_radius, outline_width, data->actual_border_color);
        if (data->actual_gradient_color.a != 0)
            paint_pinnned_icon_gradient(cr, bounds, corner_radius, outline_width, data->actual_gradient_color);
        if (data->actual_pane_color.a != 0)
            paint_pinnned_icon_pane(cr, bounds, corner_radius, outline_width, data->actual_pane_color);
        cairo_restore(cr);
        cairo_pop_group_to_source(cr);
        cairo_paint(cr);
    }
    
    if (windows_count >= 1) {
        double w = 6;
        if (data->active_amount != 0) {
            w += getEasingFunction(EaseOutQuad)(data->active_amount) * 10;
        }
        w *= config->dpi;
        
        if (active) {
            set_argb(cr, accent);
        } else {
            set_argb(cr, accent_dark);
        }
        double h = 3 * config->dpi;
        rounded_rect(cr, h / 2, bounds.x + bounds.w / 2 - w / 2, bounds.y + bounds.h - h + .5 - 1, w, h);
        cairo_fill(cr);
    }
}

static void
swap_color(ArgbColor *first, ArgbColor *second) {
    ArgbColor temp = *first;
    *first = *second;
    *second = temp;
}

static void draw_win7_pane(cairo_t *cr, const Bounds &real_bounds, bool active, bool pressed, bool hovered,
                           ArgbColor &color_background_pane_hovered_left, ArgbColor &color_background_pane_pressed_left,
                           ArgbColor &color_foreground_pane_default_left, ArgbColor &color_foreground_pane_hovered_left,
                           ArgbColor &color_foreground_pane_pressed_left, float alpha, bool clip, Bounds clip_bounds);

void drawRoundedRect(cairo_t *cr, double x, double y, double width, double height,
                     double radius, double stroke_width);

static void
paint_icon_background_win7(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    auto *data = (LaunchableButton *) container->user_data;
    // This is the real underlying color
    // is_light_theme determines if generated secondary colors should go up or down in brightness
    bool is_light_theme = false;
    {
        double h; // hue
        double s; // saturation
        double p; // perceived brightness
        ArgbColor real = config->color_taskbar_application_icons_background;
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
        is_light_theme = p > 50; // if the perceived perceived brightness is greater than that we are a light theme
    }
    
    int windows_count = data->windows_data_list.size();
    bool active = active_container == container || (data->type == selector_type::OPEN_CLICKED);
    bool pressed = container->state.mouse_pressing;
    bool hovered = container->state.mouse_hovering || (data->type != selector_type::CLOSED);
    if (client->previous_x != -1 && client->mouse_current_x > 0) {
        Bounds mouse_bounds(0, 0, 1, 1);
        if (client->previous_x < client->motion_event_x) {
            mouse_bounds.x = client->previous_x;
            mouse_bounds.w = client->motion_event_x - client->previous_x;
        } else {
            mouse_bounds.x = client->mouse_current_x;
            mouse_bounds.w = client->previous_x - client->motion_event_x;
        }
        if (client->previous_y < client->motion_event_y) {
            mouse_bounds.y = client->previous_y;
            mouse_bounds.h = client->motion_event_y - client->previous_y;
        } else {
            mouse_bounds.y = client->mouse_current_y;
            mouse_bounds.h = client->previous_y - client->motion_event_y;
        }
        hovered = overlaps(container->real_bounds, mouse_bounds);
    }
//    hovered = true;
    bool dragging = container->state.mouse_dragging;
    double active_amount = data->active_amount;
    if (data->type == selector_type::OPEN_CLICKED) active_amount = 1;
    
    int highlight_height = 2;
    
    double bar_amount = std::max(data->hover_amount, active_amount);
    if (data->type != selector_type::CLOSED)
        bar_amount = 1;
    if (data->wants_attention_amount != 0)
        bar_amount = 1;
    double highlight_inset = 4 * (1 - bar_amount);
    
    double bg_openess = highlight_inset;
    double right_size = 0;
    
    ArgbColor original_color_taskbar_application_icons_background = config->color_taskbar_application_icons_background;
    // The pinned icon is composed of three sections;
    // The background pane, the foreground pane, and the accent bar.
    //
    ArgbColor accent = config->color_taskbar_application_icons_accent;
    ArgbColor background = config->color_taskbar_application_icons_background;
    background = ArgbColor(1, 1, 1, 1);
    background.a = 0.11 + (data->active_amount * .05);
    
    if (data->wants_attention_amount != 0) {
        double blinks = 10.5;
        double scalar = fmod(data->wants_attention_amount, (1.0 / blinks)); // get N blinks
        scalar *= blinks;
        if (scalar > .5)
            scalar = 1 - scalar;
        scalar *= 2;
        if (data->wants_attention_amount == 1)
            scalar = 1;
        accent = lerp_argb(scalar, accent, config->color_taskbar_attention_accent);
        background = lerp_argb(scalar, background, config->color_taskbar_attention_background);
        active_amount = 1;
    }
    
    // The following colors are used on the accent bar
    ArgbColor color_accent_bar_left = accent;
    ArgbColor color_accent_bar_middle = darken(accent, 20);
    ArgbColor color_accent_bar_right = darken(accent, 15);
    
    // The following colors are used for the background pane
    ArgbColor color_background_pane_hovered_left = darken(background, 15);
    ArgbColor color_background_pane_hovered_middle = darken(background, 22);
    ArgbColor color_background_pane_hovered_right = darken(background, 17);
    
    ArgbColor color_background_pane_pressed_left = darken(background, 20);
    ArgbColor color_background_pane_pressed_middle = darken(background, 27);
    ArgbColor color_background_pane_pressed_right = darken(background, 22);
    
    // The following colors are used for the foreground pane
    ArgbColor color_foreground_pane_default_left = darken(background, 10);
    ArgbColor color_foreground_pane_hovered_left = darken(background, 0);
    ArgbColor color_foreground_pane_pressed_left = darken(background, 0 + 2);
    if (is_light_theme) {
        swap_color(&color_foreground_pane_pressed_left, &color_foreground_pane_default_left);
        swap_color(&color_foreground_pane_hovered_left, &color_foreground_pane_hovered_left);
    }
    
    ArgbColor color_foreground_pane_default_middle = darken(color_foreground_pane_default_left, 7);
    ArgbColor color_foreground_pane_default_right = darken(color_foreground_pane_default_left, 2);
    
    ArgbColor color_foreground_pane_hovered_middle = darken(color_foreground_pane_hovered_left, 7);
    ArgbColor color_foreground_pane_hovered_right = darken(color_foreground_pane_hovered_left, 2);
    
    ArgbColor color_foreground_pane_pressed_middle = darken(color_foreground_pane_pressed_left, 7);
    ArgbColor color_foreground_pane_pressed_right = darken(color_foreground_pane_pressed_left, 2);
    
    Bounds backup = container->real_bounds;
    float pad = 0;
//    container->real_bounds.y -= std::floor(config->dpi * 1);
//    container->real_bounds.h += std::floor(config->dpi * 1);
    
    bool show_hover_spotlight = false;
    if (windows_count >= 3) {
        show_hover_spotlight = true;
        auto diff = std::round(4 * config->dpi);
        Bounds b = container->real_bounds;
        b.x += b.w - diff;
        b.w = diff;
        draw_win7_pane(cr, container->real_bounds, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 0.33, true, b);
        
        Bounds drawB = container->real_bounds;
        drawB.w -= diff * 1;
        b.x -= diff;
        draw_win7_pane(cr, drawB, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 0.66, true, b);
        
        b = container->real_bounds;
        b.w -= diff * 2;
        draw_win7_pane(cr, b, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 1.0, false, b);
    } else if (windows_count == 2) {
        show_hover_spotlight = true;
        auto diff = std::round(5 * config->dpi);
        Bounds b = container->real_bounds;
        b.x += b.w - diff;
        b.w = diff;
        draw_win7_pane(cr, container->real_bounds, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 0.66, true, b);
        
        b = container->real_bounds;
        b.w -= diff;
        draw_win7_pane(cr, b, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 1.0, false, b);
    } else if (windows_count == 1) {
        show_hover_spotlight = true;
        draw_win7_pane(cr, container->real_bounds, active, pressed, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 1.0, false, Bounds());
    } else if (data->attempting_to_launch_first_window &&
               ((get_current_time_in_ms() - data->attempting_to_launch_first_window_time) < 10000)) {
        show_hover_spotlight = true;
        draw_win7_pane(cr, container->real_bounds, true, false, hovered, color_background_pane_hovered_left,
                       color_background_pane_pressed_left,
                       color_foreground_pane_default_left, color_foreground_pane_hovered_left,
                       color_foreground_pane_pressed_left, 0.42, false, Bounds());
    } else if (data->hover_amount != 0 && !container->state.mouse_dragging) {
        // Create a radial gradient pattern
        int x = container->real_bounds.x + container->real_bounds.w / 2;
        float r = (container->real_bounds.h * .4) * data->hover_amount;
        int y = container->real_bounds.y + container->real_bounds.h - r * .24;
        cairo_pattern_t* radial = cairo_pattern_create_radial(x, y, 0, x, y, r);
        
        // Add color stops: white (fully opaque) at the center and transparent at the edge
        cairo_pattern_add_color_stop_rgba(radial, 0.0, 1.0, 1.0, 1.0, 0.1 * data->hover_amount); // White
        cairo_pattern_add_color_stop_rgba(radial, 1.0, 1.0, 1.0, 1.0, 0.0); // Transparent
        
        // Set the pattern as the source
        cairo_set_source(cr, radial);
        
        // Draw the circle
        cairo_arc(cr, x, y, r, 0, 2 * M_PI);
        cairo_fill(cr);
        
        // Destroy the pattern to free memory
        cairo_pattern_destroy(radial);
    }
    
    float hover_amount = data->hover_amount;
    if (data->type != CLOSED) {
        hover_amount = 1;
    }
    if (show_hover_spotlight && (data->hover_amount != 0 || (data->type != CLOSED)) && !data->windows_data_list.empty()) {
//        int x = client->mouse_current_x;
        int x = container->real_bounds.x + container->real_bounds.w / 2;
        float r = container->real_bounds.w * hover_amount * 1.5;
        int y = container->real_bounds.y + container->real_bounds.h / 2;
        x = client->mouse_current_x;
        y = client->mouse_current_y;
       
        cairo_pattern_t* radial = cairo_pattern_create_radial(x, y, 0, x, y, r);
        
        if (!data->average_color_set) {
            get_average_color(data->surface, &data->average_color);
            data->average_color_set = true;
        }

//        float a = 1;
        float a = hover_amount;
        // Add color stops: white (fully opaque) at the center and transparent at the edge
        if (data->average_color_set) {
            cairo_pattern_add_color_stop_rgba(radial, 0.00, data->average_color.r, data->average_color.g,
                                              data->average_color.b, 0.7 * a); // Transparent
            cairo_pattern_add_color_stop_rgba(radial, 0.7, data->average_color.r, data->average_color.g,
                                              data->average_color.b, 0.55 * a); // Transparent
            cairo_pattern_add_color_stop_rgba(radial, 1.0, data->average_color.r, data->average_color.g,
                                              data->average_color.b, 0.0); // Transparent
        } else {
            cairo_pattern_add_color_stop_rgba(radial, 0.0, 1.0, 1.0, 1.0, 0.2 * a); // White
            cairo_pattern_add_color_stop_rgba(radial, 1.0, 1.0, 1.0, 1.0, 0.0); // Transparent
        }
        
        // Set the pattern as the source
        cairo_set_source(cr, radial);
        
        float radius = 2 * config->dpi;
        float line_w = std::floor(1 * config->dpi);
        drawRoundedRect(cr, container->real_bounds.x + line_w, container->real_bounds.y + line_w,
                        container->real_bounds.w - line_w * 2, container->real_bounds.h - line_w * 2, radius, line_w);
        cairo_clip(cr);
        
        // Draw the circle
        cairo_arc(cr, x, y, r * hover_amount, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_reset_clip(cr);
        
        // Destroy the pattern to free memory
        cairo_pattern_destroy(radial);
    }
    
    float line_w = std::floor(1 * config->dpi);
    
    if (data->windows_data_list.size() != data->last_frame_window_count) {
        if (data->windows_data_list.empty()) {
            // removed
            client_create_animation(app, client, &data->window_opened_scalar,
                                    data->lifetime, 0, 100, nullptr, 0);
        } else if (data->window_opened_scalar != 1.0) {
            // added
            client_create_animation(app, client, &data->window_opened_scalar,
                                    data->lifetime, 0, 200, getEasingFunction(easing_functions::EaseInQuad), 1);
        }
    }
    data->last_frame_window_count = data->windows_data_list.size();
    
    if (windows_count >= 1) {
        int x = container->real_bounds.x + line_w * 2;
        float r = container->real_bounds.w;
        int y = container->real_bounds.y + line_w * 2;
        x += container->real_bounds.w * .1;
        y -= container->real_bounds.w * .5;
        cairo_pattern_t *radial = cairo_pattern_create_radial(x, y, 0, x, y, r);
        x = container->real_bounds.x + line_w * 2;
        y = container->real_bounds.y + line_w * 2;
        
        // Add color stops: white (fully opaque) at the center and transparent at the edge
        cairo_pattern_add_color_stop_rgba(radial, 0.0, 1.0, 1.0, 1.0, 0.35 * data->window_opened_scalar); // White
        cairo_pattern_add_color_stop_rgba(radial, 1.0, 1.0, 1.0, 1.0, 0.0); // Transparent
        
        // Set the pattern as the source
        cairo_set_source(cr, radial);
        
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x + container->real_bounds.w, y);
        cairo_line_to(cr, x + container->real_bounds.w, y + container->real_bounds.h * .2);
        
        cairo_curve_to(cr,
                       x + container->real_bounds.w,
                       y + container->real_bounds.h * .2, // First control point (slightly above the line)
                       x + container->real_bounds.w * .15,
                       y + container->real_bounds.h * .15, // Second control point (slightly above the line)
                       x + container->real_bounds.w * .1, y + container->real_bounds.h * .8); // End point
        
        cairo_line_to(cr, x, y + container->real_bounds.h * .8);

//        cairo_line_to(cr, x, y + container->real_bounds.h);
        cairo_line_to(cr, x, y);

//        set_rect(cr, container->real_bounds);
        cairo_clip(cr);
        
        // Draw the circle
        cairo_arc(cr, x, y, r, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_pattern_destroy(radial);
        
        x = container->real_bounds.x + line_w * 2;
        r = container->real_bounds.h * .6;
        y = container->real_bounds.y + line_w * 2;
        radial = cairo_pattern_create_radial(x, y, 0, x, y, r);
        
        // Add color stops: white (fully opaque) at the center and transparent at the edge
        cairo_pattern_add_color_stop_rgba(radial, 0.0, 1.0, 1.0, 1.0, 0.2 * data->window_opened_scalar); // White
        cairo_pattern_add_color_stop_rgba(radial, 1.0, 1.0, 1.0, 1.0, 0.0); // Transparent
        
        // Set the pattern as the source
        cairo_set_source(cr, radial);
        
        set_rect(cr, container->real_bounds);
        cairo_clip(cr);
        
        // Draw the circle
        cairo_arc(cr, x, y, r, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_reset_clip(cr);
        
        // Destroy the pattern to free memory
        cairo_pattern_destroy(radial);
    }
    
    config->color_taskbar_application_icons_background = original_color_taskbar_application_icons_background;
    container->real_bounds = backup;
}

void drawRoundedRect(cairo_t *cr, double x, double y, double width, double height,
                     double radius, double stroke_width) {
    // Ensure the stroke width does not exceed the bounds
    double half_stroke = stroke_width / 2.0;
    double adjusted_radius = std::fmin(radius, std::fmin(width, height) / 2.0);
    double inner_width = width - stroke_width;
    double inner_height = height - stroke_width - 1;
    
    if (inner_width <= 0 || inner_height <= 0) {
        // Cannot draw if the stroke width exceeds or equals the bounds
        return;
    }
    
    // Adjusted bounds to ensure the stroke remains inside
    double adjusted_x = x + half_stroke;
    double adjusted_y = y + half_stroke;
    
    // Begin path for rounded rectangle
    cairo_new_path(cr);
    
    // Move to the start of the top-right corner
    cairo_move_to(cr, adjusted_x + adjusted_radius, adjusted_y);
    
    // Top side
    cairo_line_to(cr, adjusted_x + inner_width - adjusted_radius, adjusted_y);
    
    // Top-right corner
    cairo_arc(cr, adjusted_x + inner_width - adjusted_radius, adjusted_y + adjusted_radius,
              adjusted_radius, -M_PI / 2, 0);
    
    // Right side
    cairo_line_to(cr, adjusted_x + inner_width, adjusted_y + inner_height - adjusted_radius);
    
    // Bottom-right corner
    cairo_arc(cr, adjusted_x + inner_width - adjusted_radius, adjusted_y + inner_height - adjusted_radius,
              adjusted_radius, 0, M_PI / 2);
    
    // Bottom side
    cairo_line_to(cr, adjusted_x + adjusted_radius, adjusted_y + inner_height);
    
    // Bottom-left corner
    cairo_arc(cr, adjusted_x + adjusted_radius, adjusted_y + inner_height - adjusted_radius,
              adjusted_radius, M_PI / 2, M_PI);
    
    // Left side
    cairo_line_to(cr, adjusted_x, adjusted_y + adjusted_radius);
    
    // Top-left corner
    cairo_arc(cr, adjusted_x + adjusted_radius, adjusted_y + adjusted_radius,
              adjusted_radius, M_PI, 3 * M_PI / 2);
    
    // Close the path
    cairo_close_path(cr);
    
    // Set stroke width and stroke
    cairo_set_line_width(cr, stroke_width);
}

static void draw_win7_pane(cairo_t *cr, const Bounds &real_bounds, bool active, bool pressed, bool hovered,
                           ArgbColor &color_background_pane_hovered_left, ArgbColor &color_background_pane_pressed_left,
                           ArgbColor &color_foreground_pane_default_left, ArgbColor &color_foreground_pane_hovered_left,
                           ArgbColor &color_foreground_pane_pressed_left, float alpha, bool clip, Bounds clip_bounds) {
    ArgbColor copy;
    float line_w = std::floor(1 * config->dpi);
    float pos_x = std::round(real_bounds.x);
    float offset = .5;
    if (((int) line_w) % 2 == 0) {
        offset = 0;
    }
    float radius = 2 * config->dpi;
    if (clip) {
        set_rect(cr, clip_bounds);
        cairo_clip(cr);
    }
    drawRoundedRect(cr, pos_x + line_w, real_bounds.y + line_w, real_bounds.w - line_w * 2, real_bounds.h - line_w * 2,
                    radius, line_w);
    cairo_clip(cr);
    
    if (hovered || pressed || active) {
        set_rect(cr, real_bounds);
        if (pressed || active) {
            copy = color_background_pane_pressed_left;
            copy.a *= alpha;
            set_argb(cr, copy);
        } else {
            copy = color_background_pane_hovered_left;
            copy.a *= alpha;
            set_argb(cr, copy);
        }
        cairo_fill(cr);
    }
    
    set_rect(cr, real_bounds);
    if (hovered || pressed || active) {
        if (pressed || active) {
            copy = color_foreground_pane_pressed_left;
            copy.a *= alpha;
            set_argb(cr, copy);
        } else {
            copy = color_foreground_pane_hovered_left;
            copy.a *= alpha;
            set_argb(cr, copy);
        }
    } else {
        copy = color_foreground_pane_default_left;
        copy.a *= alpha;
        set_argb(cr, copy);
    }
    cairo_fill(cr);
    
    cairo_reset_clip(cr);
    
    if (clip) {
        set_rect(cr, clip_bounds);
        cairo_clip(cr);
    }
    
    set_argb(cr, ArgbColor(1, 1, 1, .35 * alpha));
    drawRoundedRect(cr, pos_x + line_w, real_bounds.y + line_w, real_bounds.w - line_w * 2, real_bounds.h - line_w * 2,
                    radius * .4, line_w);
    cairo_stroke(cr);
    
    set_argb(cr, ArgbColor(0, 0, 0, .8 * alpha));
    drawRoundedRect(cr, pos_x, real_bounds.y, real_bounds.w, real_bounds.h, radius, line_w);
    cairo_stroke(cr);
    if (clip) {
        cairo_reset_clip(cr);
    }
}

static void
paint_icon_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (winbar_settings->pinned_icon_style == "win11") {
        paint_icon_background_win11(client, cr, container);
        return;
    } else if (winbar_settings->pinned_icon_style == "win7") {
        paint_icon_background_win7(client, cr, container);
        return;
    }
    
    auto *data = (LaunchableButton *) container->user_data;
    // This is the real underlying color
    // is_light_theme determines if generated secondary colors should go up or down in brightness
    bool is_light_theme = false;
    {
        double h; // hue
        double s; // saturation
        double p; // perceived brightness
        ArgbColor real = config->color_taskbar_application_icons_background;
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
        is_light_theme = p > 50; // if the perceived perceived brightness is greater than that we are a light theme
    }
    
    int windows_count = data->windows_data_list.size();
    bool active = active_container == container || (data->type == selector_type::OPEN_CLICKED);
    bool pressed = container->state.mouse_pressing;
    bool hovered = container->state.mouse_hovering || (data->type != selector_type::CLOSED);
    if (client->previous_x != -1 && client->mouse_current_x > 0) {
        Bounds mouse_bounds(0, 0, 1, 1);
        if (client->previous_x < client->motion_event_x) {
            mouse_bounds.x = client->previous_x;
            mouse_bounds.w = client->motion_event_x - client->previous_x;
        } else {
            mouse_bounds.x = client->mouse_current_x;
            mouse_bounds.w = client->previous_x - client->motion_event_x;
        }
        if (client->previous_y < client->motion_event_y) {
            mouse_bounds.y = client->previous_y;
            mouse_bounds.h = client->motion_event_y - client->previous_y;
        } else {
            mouse_bounds.y = client->mouse_current_y;
            mouse_bounds.h = client->previous_y - client->motion_event_y;
        }
        hovered = overlaps(container->real_bounds, mouse_bounds);
    }
//    hovered = true;
    bool dragging = container->state.mouse_dragging;
    double active_amount = data->active_amount;
    if (data->type == selector_type::OPEN_CLICKED) active_amount = 1;
    
    int highlight_height = 2;
    
    double bar_amount = std::max(data->hover_amount, active_amount);
    if (data->type != selector_type::CLOSED)
        bar_amount = 1;
    if (data->wants_attention_amount != 0)
        bar_amount = 1;
    double highlight_inset = 4 * (1 - bar_amount);
    
    double bg_openess = highlight_inset;
    double right_size = 0;
    
    ArgbColor original_color_taskbar_application_icons_background = config->color_taskbar_application_icons_background;
    // The pinned icon is composed of three sections;
    // The background pane, the foreground pane, and the accent bar.
    //
    ArgbColor accent = config->color_taskbar_application_icons_accent;
    ArgbColor background = config->color_taskbar_application_icons_background;
    
    if (data->wants_attention_amount != 0) {
        double blinks = 10.5;
        double scalar = fmod(data->wants_attention_amount, (1.0 / blinks)); // get N blinks
        scalar *= blinks;
        if (scalar > .5)
            scalar = 1 - scalar;
        scalar *= 2;
        if (data->wants_attention_amount == 1)
            scalar = 1;
        accent = lerp_argb(scalar, accent, config->color_taskbar_attention_accent);
        background = lerp_argb(scalar, background, config->color_taskbar_attention_background);
        active_amount = 1;
    }
    
    // The following colors are used on the accent bar
    ArgbColor color_accent_bar_left = accent;
    ArgbColor color_accent_bar_middle = darken(accent, 20);
    ArgbColor color_accent_bar_right = darken(accent, 15);

    // The following colors are used for the background pane
    ArgbColor color_background_pane_hovered_left = darken(background, 15);
    ArgbColor color_background_pane_hovered_middle = darken(background, 22);
    ArgbColor color_background_pane_hovered_right = darken(background, 17);
    
    ArgbColor color_background_pane_pressed_left = darken(background, 20);
    ArgbColor color_background_pane_pressed_middle = darken(background, 27);
    ArgbColor color_background_pane_pressed_right = darken(background, 22);
    
    // The following colors are used for the foreground pane
    ArgbColor color_foreground_pane_default_left = darken(background, 10);
    ArgbColor color_foreground_pane_hovered_left = darken(background, 0);
    ArgbColor color_foreground_pane_pressed_left = darken(background, 0 + 2);
    if (is_light_theme) {
        swap_color(&color_foreground_pane_pressed_left, &color_foreground_pane_default_left);
        swap_color(&color_foreground_pane_hovered_left, &color_foreground_pane_hovered_left);
    }
    
    ArgbColor color_foreground_pane_default_middle = darken(color_foreground_pane_default_left, 7);
    ArgbColor color_foreground_pane_default_right = darken(color_foreground_pane_default_left, 2);
    
    ArgbColor color_foreground_pane_hovered_middle = darken(color_foreground_pane_hovered_left, 7);
    ArgbColor color_foreground_pane_hovered_right = darken(color_foreground_pane_hovered_left, 2);
    
    ArgbColor color_foreground_pane_pressed_middle = darken(color_foreground_pane_pressed_left, 7);
    ArgbColor color_foreground_pane_pressed_right = darken(color_foreground_pane_pressed_left, 2);
    
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    
    { // Background pane
        if (windows_count > 1) {
            if (pressed || hovered) {
                if (pressed) {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_middle,
                                    color_background_pane_pressed_right, windows_count);
                } else {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_middle,
                                    color_background_pane_hovered_right, windows_count);
                }
            }
        } else {
            if (pressed || hovered) {
                if (pressed) {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_left, windows_count);
                } else {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_left, windows_count);
                }
            }
        }
    }
    
    { // Foreground pane
        // make the bounds
        double back_x = container->real_bounds.x + highlight_inset;
        double back_w = container->real_bounds.w - highlight_inset * 2;
        
        if (active_amount) {
            double height = container->real_bounds.h * active_amount;
            Bounds bounds = Bounds(back_x, container->real_bounds.y + container->real_bounds.h - height, back_w,
                                   height);
            
            if (windows_count > 1) {
                if (pressed || hovered) {
                    if (pressed) {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_middle,
                                        color_foreground_pane_pressed_right, windows_count);
                    } else {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_middle,
                                        color_foreground_pane_hovered_right, windows_count);
                    }
                } else {
                    paint_double_bg(cr,
                                    bounds,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_middle,
                                    color_foreground_pane_default_right, windows_count);
                }
            } else {
                if (pressed || hovered) {
                    if (pressed) {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_left, windows_count);
                    } else {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_left, windows_count);
                    }
                } else {
                    paint_double_bg(cr,
                                    bounds,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_left, windows_count);
                }
            }
        }
    }
    
    { // Accent bar
        if (windows_count > 0) {
            paint_double_bar(cr,
                             container,
                             color_accent_bar_left,
                             color_accent_bar_left,
                             color_accent_bar_left, windows_count);
            
            // paint the right side
            if (windows_count > 1) {
                paint_double_bar(cr,
                                 container,
                                 color_accent_bar_left,
                                 color_accent_bar_middle,
                                 color_accent_bar_right, windows_count);
            }
        }
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    
    config->color_taskbar_application_icons_background = original_color_taskbar_application_icons_background;
}

static void
icons_align(AppClient *client_entity, Container *icon_container, bool all_except_dragged);

static int
calc_largest(Container *icons) {
    int largest = 0;
    for (auto c: icons->children)
        if (c->real_bounds.w > largest)
            largest = c->real_bounds.w;
    return largest;
}

static int
size_icons(AppClient *client, cairo_t *cr, Container *icons) {
    int total_width = 0;
    for (auto c: icons->children) {
        auto w = get_label_width(client, c);
        c->real_bounds.w = w;
        c->real_bounds.h = client->bounds->h;
        c->real_bounds.y = 0;
    }
    
    for (auto c: icons->children) {
        total_width += c->real_bounds.w;
    }
    
    // For pixel spacing between pinned icons
    int count = icons->children.size();
    if (count != 0)
        count--;
    total_width += count * pixel_spacing;
    
    if (total_width > icons->real_bounds.w) {
        auto overflow = total_width - icons->real_bounds.w;
        
        for (int i = 0; i < overflow; i++) {
            int largest = calc_largest(icons);
            
            for (auto c: icons->children) {
                if ((int) c->real_bounds.w == largest) {
                    c->real_bounds.w -= 1;
                    break;
                }
            }
        }
        total_width = icons->real_bounds.w;
    }
    
    return total_width;
}

static void
swap_icon(Container *icons, Container *dragging, Container *other, bool before) {
    // TODO: it's not a swap it's an insert after, or before
    for (int i = 0; i < icons->children.size(); i++) {
        if (icons->children[i] == dragging) {
            icons->children.erase(icons->children.begin() + i);
            break;
        }
    }
    for (int i = 0; i < icons->children.size(); i++) {
        if (icons->children[i] == other) {
            if (before) {
                icons->children.insert(icons->children.begin() + i, dragging);
            } else {
                icons->children.insert(icons->children.begin() + i + 1, dragging);
            }
            break;
        }
    }
}

int
would_be_x(Container *icons, Container *target, int pos_x) {
    for (int i = 0; i < icons->children.size(); i++) {
        if (icons->children[i] == target) {
            return pos_x;
        }
        pos_x += icons->children[i]->real_bounds.w;
    }
    return pos_x;
}

static void
position_icons(AppClient *client, cairo_t *cr, Container *icons) {
    auto total_width = size_icons(client, cr, icons);
    
    auto align = winbar_settings->icons_alignment;
    int off = icons->real_bounds.x;
    if (align == container_alignment::ALIGN_RIGHT) {
        off += icons->real_bounds.w - total_width;
    } else if (align == container_alignment::ALIGN_GLOBAL_CENTER_HORIZONTALLY) {
        auto mid_point = client->bounds->w / 2;
        auto left_x = mid_point - (total_width / 2);
        auto right_x = left_x + total_width;
        auto min = icons->real_bounds.x;
        auto max = icons->real_bounds.x + icons->real_bounds.w;
        if (right_x > max) {
            left_x -= right_x - max;
            right_x = left_x + total_width;
        }
        if (left_x < min) {
            left_x += min - left_x;
            right_x = left_x + total_width;
        }
        off = left_x;
    } else if (align == container_alignment::ALIGN_CENTER_HORIZONTALLY) {
        off += (icons->real_bounds.w - total_width) / 2;
    }
    
    // Set the 'x' for the pinned_icons, or more specifically the "natural_position_x"
    for (auto c: icons->children) {
        auto *data = (LaunchableButton *) c->user_data;
        if (data->natural_position_x == INT_MAX) {
            data->old_natural_position_x = off;
        } else {
            data->old_natural_position_x = data->natural_position_x;
        }
        data->natural_position_x = off;
        off += c->real_bounds.w + pixel_spacing;
    }
    
    Container *dragging = nullptr;
    int drag_index = 0;
    
    // Position dragged icon based on current mouse position, and prevent it from leaving icons container
    for (auto c: icons->children) {
        auto *data = (LaunchableButton *) c->user_data;
        if (c->state.mouse_dragging) {
            dragging = c;
            auto x = client->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
            c->real_bounds.x = x;
            if (c->real_bounds.x < icons->real_bounds.x) {
                c->real_bounds.x = icons->real_bounds.x;
            }
            if (c->real_bounds.x + c->real_bounds.w > icons->real_bounds.x + icons->real_bounds.w) {
                c->real_bounds.x = icons->real_bounds.x + icons->real_bounds.w - c->real_bounds.w;
            }
            break;
        }
        drag_index++;
    }
    
    // Calculate 'slot' the icon is closest to and swap into it
    if (dragging) {
        int distance = 100000;
        int index = 0;
        int w_b = 0;
        auto natural_x = ((LaunchableButton *) icons->children[0]->user_data)->natural_position_x;
        icons->children.erase(icons->children.begin() + drag_index);
        for (int i = 0; i < icons->children.size() + 1; i++) {
            icons->children.insert(icons->children.begin() + i, dragging);
            auto would_be = would_be_x(icons, dragging, natural_x);
            auto dist = std::abs(dragging->real_bounds.x - would_be);
            if (dist < distance) {
                distance = dist;
                index = i;
                w_b = would_be;
            }
            icons->children.erase(icons->children.begin() + i);
        }
        icons->children.insert(icons->children.begin() + index, dragging);
        auto *data = (LaunchableButton *) dragging->user_data;
        data->old_natural_position_x = dragging->real_bounds.x;
        data->natural_position_x = dragging->real_bounds.x;
    }
    
    // Queue spring animations
    static auto start_time = get_current_time_in_ms();
    for (auto c: icons->children) {
        if (c->state.mouse_dragging) continue;
        auto *data = (LaunchableButton *) c->user_data;
        bool needs_animation = c->real_bounds.x != data->natural_position_x;
        
        if (get_current_time_in_ms() - start_time < 1000) {
            needs_animation = false;
            c->real_bounds.x = data->natural_position_x;
        }
        
        if (needs_animation) {
            if (data->animating) {
                if (data->old_natural_position_x != data->natural_position_x) {
                    data->spring.target = data->natural_position_x;
                }
            } else {
                data->animating = true;
                data->spring = SpringAnimation(data->old_natural_position_x, data->natural_position_x);
            }
        }
        
        if (data->animating) {
            data->spring.update((float) client->delta / 1000.0f);
            c->real_bounds.x = data->spring.position;
            float abs_vel = std::abs(data->spring.velocity);
            if (abs_vel < .05) {
                data->animating = false;
                c->real_bounds.x = data->spring.target;
            }
        }
    }
    
    // Start animating, if not already
    static bool running = false;
    for (auto c: icons->children) {
        auto *data = (LaunchableButton *) c->user_data;
        if (data->animating && !running) {
            running = true;
            client_register_animation(app, client);
            return;
        }
    }
    if (running) {
        client_unregister_animation(app, client);
        running = false;
    }
}

// TODO order is not correct
static void
paint_all_icons(AppClient *client_entity, cairo_t *cr, Container *container) {
    pixel_spacing = 1;
    if (winbar_settings->pinned_icon_style == "win7") {
        pixel_spacing = std::floor(2 * config->dpi);
    }
    position_icons(client_entity, cr, container);
    
    for (auto con: container->children) {
        double time = 250;
        auto *data = (LaunchableButton *) con->user_data;
        auto delta = (double) (app->current - data->creation_time);
        if (delta < time) {
            request_refresh(app, client_entity);
            cairo_push_group(cr);
        }
        if (con->z_index == 0) {
            paint_icon_background(client_entity, cr, con);
            paint_icon_surface(client_entity, cr, con);
            paint_icon_label(client_entity, cr, con);
        }
        if (delta < time) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, delta / time);
        }
    }
    for (auto con: container->children) {
        double time = 250;
        auto *data = (LaunchableButton *) con->user_data;
        auto delta = (double) (app->current - data->creation_time);
        if (delta < time) {
            cairo_push_group(cr);
        }
        if (con->z_index != 0) {
            paint_icon_background(client_entity, cr, con);
            paint_icon_surface(client_entity, cr, con);
            paint_icon_label(client_entity, cr, con);
        }
        if (delta < time) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, delta / time);
        }
    }
}

static void
on_tooltip_open(App *app, AppClient *client, Timeout *timeout, void *data) {
#define PAD (4 * config->dpi)
    if (auto c = client_by_name(app, "tooltip_taskbar")) {
        return;
    }
    Container *container = nullptr;
    if (auto c = client_by_name(app, "taskbar")) {
        if (auto icons = container_by_name("icons", c->root)) {
            for (Container *con: icons->children) {
                auto *d = (LaunchableButton *) con->user_data;
                if (con->state.mouse_hovering && !d->attempting_to_launch_first_window && d->windows_data_list.empty() &&
                    !con->state.mouse_dragging) {
                    container = con;
                }
            }
        }
    }
    if (container == nullptr || container != (Container *) data)
        return;
    auto d = (LaunchableButton *) container->user_data;
    std::string text = d->class_name;
    if (text.empty()) {
        return;
    }
    d->possibly_open_tooltip_timeout = nullptr;
    Settings settings;
    settings.decorations = false;
    settings.force_position = true;
    settings.keep_above = true;
    settings.override_redirect = true;
    PangoLayout *layout =
            get_cached_pango_font(client->cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(layout, text.data(), text.size());
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    settings.h = height + PAD * 2;
    settings.w = width + PAD * 2;
    settings.x = client->bounds->x + container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2;
    settings.y = client->bounds->y - settings.h - 3 * config->dpi - PAD;
    
    auto c = client_new(app, settings, "tooltip_taskbar");
    c->root->user_data = new Label(text);
    
    app_timeout_create(app, c, 100, [](App *app, AppClient *tooltip, Timeout *t, void *) {
        t->keep_running = false;
        if (auto c = client_by_name(app, "taskbar")) {
            if (auto icons = container_by_name("icons", c->root)) {
                for (Container *container: icons->children) {
                    auto data = (LaunchableButton *) container->user_data;
                    
                    if (container->state.mouse_hovering && data->windows_data_list.empty()) {
                        t->keep_running = true;
                        auto label = (Label *) tooltip->root->user_data;
                        if (label->text != data->class_name) {
                            label->text = data->class_name;
                            PangoLayout *layout =
                                    get_cached_pango_font(tooltip->cr, config->font, 12 * config->dpi,
                                                          PangoWeight::PANGO_WEIGHT_NORMAL);
                            pango_layout_set_text(layout, label->text.data(), label->text.size());
                            int width;
                            int height;
                            pango_layout_get_pixel_size_safe(layout, &width, &height);
                            uint32_t value_mask =
                                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                    XCB_CONFIG_WINDOW_HEIGHT;
                            uint32_t value_list_resize[] = {
                                    (uint32_t) (c->bounds->x + container->real_bounds.x + container->real_bounds.w / 2 -
                                                (width + PAD * 2) / 2),
                                    (uint32_t) (c->bounds->y - (height + PAD * 2) - 3 * config->dpi - PAD),
                                    (uint32_t) (width + PAD * 2),
                                    (uint32_t) (height + PAD * 2),
                            };
                            xcb_configure_window(app->connection, tooltip->window, value_mask, value_list_resize);
                            request_refresh(app, tooltip);
                        }
                    }
                }
            }
        }
        
        if (!t->keep_running) {
            client_close_threaded(app, tooltip);
        }
    }, nullptr, "tooltip_open");
    c->root->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        set_rect(cr, container->real_bounds);
        auto c = config->color_taskbar_background;
        c.a = 1.0;
        set_argb(cr, c);
        cairo_fill(cr);
        
        auto label = (Label *) container->user_data;
        
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        pango_layout_set_text(layout, label->text.data(), label->text.size());
        int width;
        int height;
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        set_argb(cr, config->color_taskbar_button_icons);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    };
    client_show(app, c);
}

static void
possibly_open_tooltip(AppClient *client, Container *container, LaunchableButton *data) {
    if (auto c = client_by_name(app, "tooltip_taskbar")) {
        return;
    }
    if (client_by_name(app, "right_click_menu")) {
        return;
    }
    bool recently_touchpad = get_current_time_in_ms() - app->last_touchpad_time < 400;
    data->possibly_open_tooltip_timeout = app_timeout_create(app, client,
                                                             800 + (winbar_settings->labels ? 120 : 0) + (recently_touchpad ? 450 : 0),
                                                             on_tooltip_open, container,
                                                             const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
pinned_icon_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    if (data->windows_data_list.empty()) {
        possibly_open_tooltip(client, container, data);
    }
    possibly_open(app, container, data);
    if (winbar_settings->pinned_icon_style == "win7") {
        client_create_animation(app, client, &data->hover_amount, data->lifetime, 0, 100, 0, 1);
    } else {
        client_create_animation(app, client, &data->hover_amount, data->lifetime, 0, 70, 0, 1);
    }
}

static void
pinned_icon_mouse_leaves(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    possibly_close(app, container, data);
    if (winbar_settings->pinned_icon_style == "win7") {
        auto delay = 100 - (100 * data->hover_amount);
        client_create_animation(app, client, &data->hover_amount, data->lifetime, delay, 50, 0, 0);
    } else {
        client_create_animation(app, client, &data->hover_amount, data->lifetime, 0, 70, 0, 0);
    }
}

std::string
return_current_time_and_date() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    if (winbar_settings->date_style == "windows 11 minimal") {
        ss << std::put_time(std::localtime(&in_time_t), "%I:%M");
    } else if (winbar_settings->date_style == "windows 11 detailed") {
        ss << std::put_time(std::localtime(&in_time_t), "%I:%M %p — %A");
    } else {
        ss << std::put_time(std::localtime(&in_time_t), "%I:%M %p");
    }
    if (winbar_settings->date_style == "windows vista") {
        return ss.str();
    }
    if (config->date_single_line) {
        ss << ' ';
    } else {
        ss << '\n';
    }
    
    std::stringstream month;
    month << std::put_time(std::localtime(&in_time_t), "%m");
    
    std::string real_month;
    if (month.str().at(0) == '0') {
        real_month = month.str().erase(0, 1);
    } else {
        real_month = month.str();
    }
    
    if (winbar_settings->date_style == "windows 11 detailed") {
        std::stringstream month_name;
        month_name << std::put_time(std::localtime(&in_time_t), "%B");
        real_month = month_name.str() + " " + real_month;
    }
    
    std::stringstream day;
    day << std::put_time(std::localtime(&in_time_t), "%d");
    auto s = day.str();
    
    std::string real_day;
    if (day.str().at(0) == '0') {
        real_day = day.str().erase(0, 1);
    } else {
        real_day = day.str();
    }
    
    std::stringstream year;
    if (winbar_settings->date_style == "windows 11 minimal") {
        year << std::put_time(std::localtime(&in_time_t), "%y");
        ss << real_day << "/" << year.str();
    } else {
        year << std::put_time(std::localtime(&in_time_t), "%Y");
        ss << real_month << "/" << real_day << "/" << year.str();
    }
    
    return ss.str();
}

void update_time(App *app, AppClient *client, Timeout *timeout, void *) {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Time Thread");
#endif
    if (timeout)
        timeout->keep_running = true;
    
    std::string date = return_current_time_and_date();
    if (date[0] == '0')
        date.erase(0, 1);
    if (time_text != date) {
        time_text = date;
        client_layout(app, client);
        request_refresh(app, client);
    }
}

Container *get_pinned_icon_representing_window(xcb_window_t window) {
    if (auto c = client_by_name(app, "taskbar")) {
        if (auto icons = container_by_name("icons", c->root)) {
            for (Container *container: icons->children) {
                auto data = (LaunchableButton *) container->user_data;
                
                for (auto windows_data: data->windows_data_list) {
                    if (windows_data->id == window) {
                        return container;
                    }
                }
            }
        }
    }
    return nullptr;
}

void active_window_changed(xcb_window_t new_active_window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (new_active_window == active_window)
        return;
    active_window = new_active_window;
    auto cookie = xcb_get_property(app->connection, 0, new_active_window, get_cached_atom(app, "_NET_WM_STATE"),
                                   XCB_ATOM_ATOM, 0, BUFSIZ);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, nullptr);
    if (reply) {
        if (reply->type == XCB_ATOM_ATOM) {
            xcb_atom_t *state_atoms = (xcb_atom_t *) xcb_get_property_value(reply);
            
            bool found_fullscreen = false;
            auto fullscreen = get_cached_atom(app, "_NET_WM_STATE_FULLSCREEN");
            for (unsigned int a = 0; a < reply->length; a++)
                if (state_atoms[a] == fullscreen)
                    found_fullscreen = true;
            someone_is_fullscreen_and_covering = found_fullscreen;
            if (someone_is_fullscreen_and_covering)
                client_close_threaded(app, client_by_name(app, "windows_selector"));
        }
        if (reply)
            free(reply);
    }
    auto *new_active_container = get_pinned_icon_representing_window(new_active_window);
    if (new_active_container == active_container)
        return;
    
    if (auto c = client_by_name(app, "taskbar")) {
        if (active_container) {
            auto data = (LaunchableButton *) active_container->user_data;
            if (data)
                client_create_animation(app, c, &data->active_amount, data->lifetime, 0, 80, nullptr, 0);
        }
        
        active_container = new_active_container;
        if (new_active_container) {
            auto data = (LaunchableButton *) new_active_container->user_data;
            client_create_animation(app, c, &data->active_amount, data->lifetime, 0, 120, nullptr, 1);
            
            for (auto w_d: data->windows_data_list) {
                if (w_d->id == new_active_window) {
                    if (w_d->mapped) {
                        w_d->take_screenshot();
                    }
                }
            }
        }
        request_refresh(app, c);
    }
}

static void
finished_icon_animation() {
    AppClient *client = client_by_name(app, "taskbar");
    Container *icons = container_by_name("icons", client->root);
    for (Container *child: icons->children) {
        auto *data = static_cast<LaunchableButton *>(child->user_data);
        if (data->animating) {
            if (data->target == child->real_bounds.x) {
                data->animating = false;
            }
        }
    }
    handle_mouse_motion(app, client, client->mouse_current_x, client->mouse_current_y);
}

static void
icons_align(AppClient *client_entity, Container *icon_container, bool all_except_dragged) {
    request_refresh(app, client_entity);
}

static void
pinned_icon_drag_start(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto c: app->clients) {
        if (c->name == "windows_selector") {
            client_close_threaded(app, c);
        }
    }
    backup_active_window = active_window;
    active_window_changed(-1);
    container->parent->should_layout_children = false;
    auto *data = static_cast<LaunchableButton *>(container->user_data);
    client_create_animation(app, client_entity, &data->animation_zoom_amount, data->lifetime, 0, 55 * data->animation_zoom_amount,
                            nullptr,
                            0);
    data->initial_mouse_click_before_drag_offset_x =
            container->real_bounds.x - client_entity->mouse_initial_x;
    container->z_index = 1;
    possibly_close(app, container, data);
    if (auto c = client_by_name(app, "tooltip_taskbar")) {
        client_close_threaded(app, c);
    }
    
    icons_align(client_entity, container->parent, true);
}

static void
pinned_icon_drag(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
}

static void
pinned_icon_drag_end(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client_entity, &data->animation_zoom_amount, data->lifetime, 0, 85 * data->animation_zoom_amount,
                            nullptr,
                            0);
    
    container->parent->should_layout_children = true;
    active_window_changed(backup_active_window);
    icons_align(client_entity, container->parent, false);
    container->z_index = 0;
}

uint32_t
get_wm_state(xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_get_property_reply_t *reply;
    xcb_get_property_cookie_t cookie;
    uint32_t *statep;
    uint32_t state = 0;
    
    cookie = xcb_get_property(app->connection,
                              false,
                              window,
                              get_cached_atom(app, "WM_STATE"),
                              get_cached_atom(app, "WM_STATE"),
                              0,
                              sizeof(int32_t));
    
    reply = xcb_get_property_reply(app->connection, cookie, NULL);
    if (NULL == reply) {
        fprintf(stderr, "mcwm: Couldn't get properties for win %d\n", window);
        return -1;
    }
    
    /* Length is 0 if we didn't find it. */
    if (0 == xcb_get_property_value_length(reply)) {
        goto bad;
    }
    
    statep = static_cast<uint32_t *>(xcb_get_property_value(reply));
    state = *statep;
    
    bad:
    free(reply);
    return state;
}

static void
minimize_window(xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = window;
    event.type = get_cached_atom(app, "WM_CHANGE_STATE");
    event.data.data32[0] = XCB_ICCCM_WM_STATE_ICONIC;// IconicState
    event.data.data32[1] = 0;
    event.data.data32[2] = 0;
    event.data.data32[3] = 0;
    event.data.data32[4] = 0;
    
    xcb_send_event(app->connection,
                   0,
                   app->screen->root,
                   XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   reinterpret_cast<char *>(&event));
    xcb_flush(app->connection);
}

static void
update_minimize_icon_positions() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    auto *bounds = client_entity->bounds;
    
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        
        if (!data)
            continue;
        
        for (auto window_data: data->windows_data_list) {
            auto window = window_data->id;
            double value[] = {bounds->x + icon->real_bounds.x,
                              bounds->y + icon->real_bounds.y,
                              icon->real_bounds.w,
                              icon->real_bounds.h};
            
            xcb_ewmh_set_wm_icon_geometry(
                    &app->ewmh, window, value[0], value[1], value[2], value[3]);
        }
    }
}

static bool volume_open_because_of_scroll = false;
static bool battery_open_because_of_scroll = false;

static void
mouse_leaves_volume(AppClient *client_entity,
                    cairo_t *cr,
                    Container *container) {
    if (volume_open_because_of_scroll && client_by_name(app, "volume")) {
        client_close_threaded(app, client_by_name(app, "volume"));
    }
    volume_open_because_of_scroll = false;
}

static void
mouse_leaves_battery(AppClient *client_entity,
                     cairo_t *cr,
                     Container *container) {
    if (battery_open_because_of_scroll && client_by_name(app, "battery_menu")) {
        client_close_threaded(app, client_by_name(app, "battery_menu"));
    }
    battery_open_because_of_scroll = false;
}

static void
scrolled_volume(AppClient *client_entity,
                cairo_t *cr,
                Container *container,
                int horizontal_scroll,
                int vertical_scroll, bool came_from_touchpad) {
    if (someone_is_fullscreen_and_covering)
        return;
    if (!audio_running)
        return;
    if (audio_clients.empty())
        return;
    
    if (client_by_name(app, "volume") == nullptr) {
        open_volume_menu();
        volume_open_because_of_scroll = true;
    }
    
    audio([&client_entity, &cr, &container, horizontal_scroll, vertical_scroll, came_from_touchpad]() {
        for (auto c: audio_clients) {
            if (c->is_master_volume()) {
                adjust_volume_based_on_fine_scroll(c, client_entity, cr, container, horizontal_scroll, vertical_scroll,
                                                   came_from_touchpad);
            }
        }
    });
}

static void
scrolled_battery(AppClient *client,
                 cairo_t *cr,
                 Container *container,
                 int horizontal_scroll,
                 int vertical_scroll, bool came_from_touchpad) {
    if (someone_is_fullscreen_and_covering)
        return;
    if (client_by_name(app, "battery_menu") == nullptr) {
        start_battery_menu();
        battery_open_because_of_scroll = true;
    }
    adjust_brightness_based_on_fine_scroll(client, cr, container, horizontal_scroll, vertical_scroll,
                                           came_from_touchpad);
}

struct ZoomData {
    std::weak_ptr<bool> lifetime;
    LaunchableButton *launchable = nullptr;
};

static void start_zoom_animation(AppClient *client, Container *pinned_icon) {
    LaunchableButton *data = (LaunchableButton *) pinned_icon->user_data;
    
    app_timeout_create(app, client, 10000, [](App *app, AppClient *client, Timeout *timeout, void *user_data){
        client_unregister_animation(app, client);
        auto data = (ZoomData *) user_data;
        if (data->lifetime.lock()) {
            data->launchable->animation_zoom_locked = 0;
            data->launchable->animation_zoom_locked_time = get_current_time_in_ms();
            data->launchable->attempting_to_launch_first_window = false;
        }
        delete data;
    }, new ZoomData({pinned_icon->lifetime, data}), "zoom_lock");
    
    data->animation_zoom_locked = 1;
    client_register_animation(app, client);
}

static void
pinned_icon_mouse_clicked(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client, &data->animation_zoom_amount, data->lifetime, 0, 85 * data->animation_zoom_amount, nullptr, 0);
    if (auto c = client_by_name(app, "tooltip_taskbar")) {
        client_close_threaded(app, c);
    }
    
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_1) {
        if (data->windows_data_list.empty() && !data->animation_zoom_locked) {
            if (data->windows_data_list.empty()) {
                data->attempting_to_launch_first_window = true;
                data->attempting_to_launch_first_window_time = get_current_time_in_ms();
            }
            start_zoom_animation(client, container);
            launch_command(data->command_launched_by);
            app_timeout_stop(client->app, client, data->possibly_open_timeout);
            data->possibly_open_timeout = nullptr;
            for (auto c: app->clients) {
                if (c->name == "windows_selector") {
                    client_close(app, c);
                }
            }
        } else if (data->windows_data_list.size() > 1) {
            if (winbar_settings->click_icon_tab_next_window) {
                for (auto c: app->clients) {
                    if (c->name == "windows_selector") {
                        client_close(app, c);
                    }
                }
                bool contains_active = false;
                for (int i = 0; i < data->windows_data_list.size(); ++i) {
                    if (data->windows_data_list[i]->id == active_window) {
                        contains_active = true;
                        xcb_window_t window;
                        if (i == data->windows_data_list.size() - 1) {
                            window = data->windows_data_list[0]->id;
                        } else {
                            window = data->windows_data_list[i + 1]->id;
                        }
                        std::thread t([window]() -> void {
                            xcb_ewmh_request_change_active_window(&app->ewmh,
                                                                  app->screen_number,
                                                                  window,
                                                                  XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                                  XCB_CURRENT_TIME,
                                                                  XCB_NONE);
                            xcb_flush(app->connection);
                        });
                        t.detach();
                        break;
                    }
                }
                if (!contains_active) {
                    xcb_window_t window = data->windows_data_list[0]->id;
                    std::thread t([window]() -> void {
                        xcb_ewmh_request_change_active_window(&app->ewmh,
                                                              app->screen_number,
                                                              window,
                                                              XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                              XCB_CURRENT_TIME,
                                                              XCB_NONE);
                        xcb_flush(app->connection);
                    });
                    t.detach();
                }
                possibly_close(app, container, data);
                possibly_open(app, container, data);
            } else {
                for (auto c: app->clients) {
                    if (c->name == "windows_selector") {
                        auto pii = (PinnedIconInfo *) c->root->user_data;
                        if (pii->data->type == ::OPEN_HOVERED && data == pii->data) {
                            pii->data->type = ::OPEN_CLICKED;
                            return;
                        }
                    }
                }
                start_windows_selector(container, selector_type::OPEN_CLICKED);
            }
        } else if (!data->animation_zoom_locked) {
            // TODO: choose window if there are more then one
            app_timeout_stop(client->app, client, data->possibly_open_timeout);
            data->possibly_open_timeout = nullptr;
            
            xcb_window_t window = data->windows_data_list[0]->id;
            for (auto c: app->clients) {
                if (c->name == "windows_selector") {
                    client_close(app, c);
                }
            }
            uint32_t state = get_wm_state(window);
            
            if (state == XCB_ICCCM_WM_STATE_NORMAL) {
                bool is_active_window = false;
                if (active_container) {
                    auto *button_data = (LaunchableButton *) active_container->user_data;
                    if (button_data) {
                        for (auto window_data: button_data->windows_data_list) {
                            if (window_data->id == window)
                                is_active_window = true;
                        }
                    }
                }
                if (is_active_window) {
                    std::thread t([window]() -> void {
                        update_minimize_icon_positions();
                        minimize_window(window);
                    });
                    t.detach();
                    data->animation_bounce_amount = 0;
                    data->animation_bounce_direction = 0;
                    client_create_animation(app, client, &data->animation_bounce_amount, data->lifetime, 0,
                                            651.2, nullptr, 1);
                } else {
                    std::thread t([window]() -> void {
                        xcb_ewmh_request_change_active_window(&app->ewmh,
                                                              app->screen_number,
                                                              window,
                                                              XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                              XCB_CURRENT_TIME,
                                                              XCB_NONE);
                        xcb_flush(app->connection);
                    });
                    t.detach();
                }
            } else {
                std::thread t([window]() -> void {
                    xcb_ewmh_request_change_active_window(&app->ewmh,
                                                          app->screen_number,
                                                          window,
                                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                          XCB_CURRENT_TIME,
                                                          XCB_NONE);
                });
                t.detach();
                data->animation_bounce_amount = 0;
                data->animation_bounce_direction = 1;
                client_create_animation(app, client, &data->animation_bounce_amount, data->lifetime, 0,
                                        651.2, nullptr, 1);
            }
        }
    } else if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        app_timeout_stop(client->app, client, data->possibly_open_timeout);
        data->possibly_open_timeout = nullptr;
        for (auto c: app->clients) {
            if (c->name == "windows_selector") {
                client_close(app, c);
            }
        }
        start_pinned_icon_right_click(container);
    } else if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_2) {
        if (data->windows_data_list.empty()) {
            data->attempting_to_launch_first_window = true;
            data->attempting_to_launch_first_window_time = get_current_time_in_ms();
        }
        launch_command(data->command_launched_by);
        app_timeout_stop(client->app, client, data->possibly_open_timeout);
        data->possibly_open_timeout = nullptr;
        for (auto c: app->clients) {
            if (c->name == "windows_selector") {
                client_close(app, c);
            }
        }
    }
}

static void
pinned_icon_mouse_down(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client, &data->animation_zoom_amount, data->lifetime, 0,
                            100 * (1 - data->animation_zoom_amount),
                            getEasingFunction(easing_functions::EaseOutQuad),
                            1);
}

static void
pinned_icon_mouse_up(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client, &data->animation_zoom_amount, data->lifetime, 0, 85 * data->animation_zoom_amount, nullptr, 0);
}

static void
paint_minimize(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    Bounds bounds = container->real_bounds;
    bounds.w = std::round(1 * config->dpi);
    set_rect(cr, bounds);
    set_argb(cr, config->color_taskbar_minimize_line);
    cairo_fill(cr);
    bounds.x += container->real_bounds.w - bounds.w;
    set_rect(cr, bounds);
    set_argb(cr, config->color_taskbar_minimize_line);
    cairo_fill(cr);
}

static void
paint_action_center(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto backup_bounds = container->real_bounds;
    container->real_bounds.w = container->real_bounds.w - (8 * config->dpi);
    paint_hoverable_button_background(client, cr, container);
    container->real_bounds = backup_bounds;
    
    auto data = (ActionCenterButtonData *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE91C", strlen("\uE83F"));
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + (12 * config->dpi)),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi)));
    pango_cairo_show_layout(cr, layout);
    
    if (data->slide_anim != 1) {
        cairo_push_group(cr);
        pango_layout_set_text(layout, "\uE7E7", strlen("\uE83F"));
        set_argb(cr, config->color_taskbar_button_icons);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + (12 * config->dpi)),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi)));
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_attributes(layout, nullptr);
        cairo_pattern_t *mask = cairo_pop_group(cr);
    
        cairo_push_group(cr);
        pango_layout_set_text(layout, "\uE7E7", strlen("\uE83F"));
        set_argb(cr, config->color_taskbar_button_icons);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + (12 * config->dpi) +
                             (1 - data->slide_anim) * (16 * config->dpi)),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi)));
        pango_cairo_show_layout(cr, layout);
        cairo_pattern_t *actual = cairo_pop_group(cr);
    
        cairo_set_source(cr, actual);
        cairo_mask(cr, mask);
        
        cairo_pattern_destroy(actual);
        cairo_pattern_destroy(mask);
    } else if (data->some_unseen) {
        // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
        pango_layout_set_text(layout, "\uE7E7", strlen("\uE83F"));
    
        set_argb(cr, config->color_taskbar_button_icons);
    
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + (12 * config->dpi) +
                             (1 - data->slide_anim) * (16 * config->dpi)),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi)));
        pango_cairo_show_layout(cr, layout);
    }
    
    if (data->slide_anim != 0 && data->some_unseen) {
        cairo_push_group(cr);
        // @Important (crazyness): https://stackoverflow.com/questions/66820155/how-to-prevent-an-extra-line-to-be-drawn-between-text-and-shape
        cairo_new_path(cr);
        cairo_set_line_width(cr, 1);
        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, .8);
    
        cairo_arc(cr, container->real_bounds.x + (12 * config->dpi) + (16 * config->dpi),
                  container->real_bounds.y + container->real_bounds.h / 2 + (8 * config->dpi),
                  (17.0 / 2.0) * config->dpi, 0, 2 * M_PI);
        cairo_stroke_preserve(cr);
    
        cairo_set_source_rgba(cr, .2, .2, .2, 0.7);
        cairo_fill(cr);
        
        std::string count_text;
        int count = 0;
        for (auto n: notifications) {
            if (n->sent_to_action_center) {
                count++;
            }
        }
        count_text = std::to_string(count);
    
        PangoLayout *text_layout =
                get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        validate_layout(client, layout);
    
        pango_layout_set_text(text_layout, count_text.c_str(), 2);
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(text_layout, &ink, &logical);
        
        set_argb(cr, config->color_taskbar_date_time_text);
        cairo_move_to(cr,
                      container->real_bounds.x - (ink.x / PANGO_SCALE) + (12 * config->dpi) + (16 * config->dpi) -
                      (std::ceil(ink.width / PANGO_SCALE / 2)) - 1,
                      container->real_bounds.y - (ink.y / PANGO_SCALE) +
                      (container->real_bounds.h / 2 + (8 * config->dpi)) -
                      (std::ceil(ink.height / PANGO_SCALE / 2)) - 1);
        pango_cairo_show_layout(cr, text_layout);
        cairo_pop_group_to_source(cr);
    
        double time = data->slide_anim * 2;
        if (time > 1)
            time = 1;
        cairo_paint_with_alpha(cr, time);
    }
}

static void
paint_systray(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    auto *data = (IconButton *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE971", strlen("\uE83F"));
    
    set_argb(cr, config->color_taskbar_button_icons);
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - (10 * config->dpi) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_bluetooth(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    auto *data = (IconButton *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE702", strlen("\uE702"));
    
    set_argb(cr, config->color_taskbar_button_icons);
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_date(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, winbar_settings->date_size * config->dpi,
                                  PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    PangoAlignment initial_alignment = pango_layout_get_alignment(layout);
    pango_layout_set_alignment(layout, winbar_settings->date_alignment);
    
    int width;
    int height;
    pango_layout_set_text(layout, time_text.c_str(), time_text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    int pad = 16;
    if (container->wanted_bounds.w != width + pad) {
        container->wanted_bounds.w = width + pad;
        client_layout(app, client);
        client_paint(app, client);
        return;
    }
    
    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_alignment(layout, initial_alignment);
}

static void
paint_right_click_popup_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = (HoverableButton *) container->user_data;
    paint_hoverable_button_background(client, cr, container);
    
    if (!data->icon.empty()) {
        // Paint search icon
        PangoLayout *layout =
                get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        validate_layout(client, layout);
        
        // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
        pango_layout_set_text(layout, data->icon.c_str(), data->icon.size());
        
        int width;
        int height;
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        set_argb(cr, config->color_taskbar_button_icons);
        
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + 15 * config->dpi),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    PangoAlignment initial_alignment = pango_layout_get_alignment(layout);
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_CENTER);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), data->text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 43 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_alignment(layout, initial_alignment);
}

static void
open_right_click_menu(AppClient *client, cairo_t *cr, Container *container);

static void
clicked_root(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
}

static void
clicked_date(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        if (config->date_command.empty()) {
            start_date_menu();
        } else {
            launch_command(config->date_command);
        }
    }
}

static void
clicked_wifi(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        if (config->wifi_command.empty()) {
            start_wifi_menu();
        } else {
            launch_command(config->wifi_command);
        }
    }
}

static void
clicked_systray(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        if (config->systray_command.empty()) {
            open_systray();
        } else {
            launch_command(config->systray_command);
        }
    }
}

static void
clicked_right_click_popup(AppClient *client, cairo_t *cr, Container *container) {
    client_close_threaded(app, client);
    
    open_settings_menu(SettingsPage::Taskbar);
}

void add_window(App *app, xcb_window_t window);

static void
add_item_clicked(AppClient *popup, cairo_t *, Container *) {
    auto *taskbar = client_by_name(app, "taskbar");
    auto *icons = container_by_name("icons", taskbar->root);
    
    Container *a = new Container(taskbar->bounds->h + 4 * config->dpi, FILL_SPACE);
    a->parent = icons;
    if (icons->alignment == container_alignment::ALIGN_RIGHT) {
        icons->children.erase(icons->children.begin() + (icons->children.size() - 1));
        icons->children.insert(icons->children.begin(), a);
    }
    a->when_drag_end_is_click = false;
    a->minimum_x_distance_to_move_before_drag_begins = 15;
    a->minimum_y_distance_to_move_before_drag_begins = 15 * config->dpi;
    a->when_mouse_enters_container = pinned_icon_mouse_enters;
    a->when_mouse_leaves_container = pinned_icon_mouse_leaves;
    a->when_clicked = pinned_icon_mouse_clicked;
    a->when_mouse_down = pinned_icon_mouse_down;
    a->when_mouse_up = pinned_icon_mouse_up;
    a->when_drag_end = pinned_icon_drag_end;
    a->when_drag_start = pinned_icon_drag_start;
    a->when_drag = pinned_icon_drag;
    auto *data = new LaunchableButton();
    a->user_data = data;
    data->pinned = true;
    
    client_layout(app, taskbar);
    client_paint(app, taskbar);
    start_pinned_icon_editor(a, true);
    if (popup) {
        client_close_threaded(app, popup);
    }
}

static void
open_right_click_menu(AppClient *client, cairo_t *cr, Container *container) {
    int options_count = 2;
    int pad = 6 * config->dpi;
    Settings settings;
    settings.force_position = true;
    settings.w = 296 * config->dpi;
    settings.h = ((34 * options_count) * config->dpi) + (config->dpi * (options_count - 1)) + (pad * 2);
    // TODO: get mouse position
    settings.x = client->mouse_current_x + client->bounds->x;
    settings.y = client->mouse_current_y + client->bounds->y - settings.h;
    if ((settings.x + settings.w) > client->screen_information->width_in_pixels) {
        settings.x = client->mouse_current_x + client->bounds->x - settings.w;
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    PopupSettings popup_settings;
    popup_settings.close_on_focus_out = true;
    popup_settings.takes_input_focus = true;
    
    auto popup = client->create_popup(popup_settings, settings);
    popup->name = "right_click_popup";
    
    popup->root->when_paint = paint_right_click_popup_background;
    popup->root->type = vbox;
    popup->root->spacing = 1;
    popup->root->wanted_pad.y = pad;
    popup->root->wanted_pad.h = pad;
    popup->root->wanted_bounds.w = FILL_SPACE;
    
    auto l = popup->root->child(FILL_SPACE, FILL_SPACE);
    l->when_paint = paint_right_click_popup_item;
    l->when_clicked = add_item_clicked;
    auto *data = new HoverableButton();
//    data->icon = "\uECCD";
//    data->icon = "\uF56E";
    data->text = "Add Item";
    l->user_data = data;
    
    l = popup->root->child(FILL_SPACE, FILL_SPACE);
    l->when_paint = paint_right_click_popup_item;
    l->when_clicked = clicked_right_click_popup;
    data = new HoverableButton();
    data->icon = "\uE713";
    data->text = "Taskbar Settings";
    l->user_data = data;
    
    client_show(app, popup);
}

static void
clicked_icons_background(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed != XCB_BUTTON_INDEX_3)
        return;
    if (auto c = client_by_name(app, "right_click_popup"))
        return;
    for (auto c: container->children)
        if (bounds_contains(c->real_bounds, client->mouse_current_x, client->mouse_current_y))
            return;
    open_right_click_menu(client, cr, container);
}

static void
clicked_bluetooth(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        open_bluetooth_menu();
    }
}

static void
clicked_battery(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (BatteryInfo *) container->user_data;
    if (!data->invalid_button_down) {
        if (config->battery_command.empty()) {
            start_battery_menu();
        } else {
            launch_command(config->battery_command);
        }
    }
}

static void
clicked_volume(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        if (config->volume_command.empty()) {
            open_volume_menu();
        } else {
            launch_command(config->volume_command);
        }
    }
}

static std::vector<xcb_window_t> minimize_button_windows_order;
static bool minimize_button_hide = true;

static void
clicked_minimize(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    // First we check if the window manager supports the _NET_SHOWING_DESKTOP atom
    // If so, we get the value of _NET_SHOWING_DESKTOP, flip it, and then send that to the root window as a client message
    auto request_cookie = xcb_ewmh_get_supported(&app->ewmh, app->screen_number);
    xcb_ewmh_get_atoms_reply_t atoms_reply_data;
    if (xcb_ewmh_get_supported_reply(&app->ewmh, request_cookie, &atoms_reply_data, nullptr)) {
        defer(xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data));
        bool state = false;
        for (int i = 0; i < atoms_reply_data.atoms_len; i++) {
            if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_SHOWING_DESKTOP")) {
                request_cookie = xcb_ewmh_get_showing_desktop(&app->ewmh, app->screen_number);
                unsigned int state;
                xcb_ewmh_get_showing_desktop_reply(&app->ewmh, request_cookie, &state, nullptr);
                
                state = state == 1 ? 0 : 1;
                
                xcb_client_message_event_t event;
                event.response_type = XCB_CLIENT_MESSAGE;
                event.format = 32;
                event.sequence = 0;
                event.window = app->screen->root;
                event.type = get_cached_atom(app, "_NET_SHOWING_DESKTOP");
                event.data.data32[0] = state;
                event.data.data32[1] = 0;
                event.data.data32[2] = 0;
                event.data.data32[3] = 0;
                event.data.data32[4] = 0;
                
                xcb_send_event(app->connection,
                               1,
                               app->screen->root,
                               XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                               reinterpret_cast<char *>(&event));
                xcb_flush(app->connection);
                
                return;
            }
        }
    }
    
    if (dbus_connection_session) {
        for (const auto &s: running_dbus_services) {
            if (s == "org.kde.kglobalaccel") {
                // On KDE try to show the desktop
                if (dbus_kde_show_desktop()) {
                    return;
                }
            }
        }
    }
    
    // If the window manager doesn't support the _NET_SHOWING_DESKTOP atom, then (as a last resort) we try to minimize manually
    if (minimize_button_hide) {
        // Here we hide the windows
        xcb_query_tree_cookie_t cookie;
        xcb_query_tree_reply_t *reply;
        
        cookie = xcb_query_tree(client->app->connection, client->app->screen->root);
        if ((reply = xcb_query_tree_reply(client->app->connection, cookie, NULL))) {
            xcb_window_t *children = xcb_query_tree_children(reply);
            for (int i = 0; i < xcb_query_tree_children_length(reply); i++) {
                xcb_window_t window = children[i];
                auto *c = client_by_window(client->app, window);
                if (c == nullptr) {
                    minimize_button_windows_order.push_back(window);
                    minimize_window(window);
                }
            }
            
            free(reply);
        }
    } else {
        // Here we show them based on the order
        for (auto window: minimize_button_windows_order) {
            auto *c = client_by_window(client->app, window);
            if (c == nullptr) {
                xcb_ewmh_request_change_active_window(&app->ewmh,
                                                      app->screen_number,
                                                      window,
                                                      XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                      XCB_CURRENT_TIME,
                                                      XCB_NONE);
            }
        }
        
        minimize_button_windows_order.clear();
        minimize_button_windows_order.shrink_to_fit();
    }
    minimize_button_hide = !minimize_button_hide;
}

static void
clicked_action_center(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto *data = (ActionCenterButtonData *) container->user_data;
    if (!data->invalid_button_down) {
        start_action_center(client->app);
    }
}

static void
clicked_search(AppClient *client, cairo_t *cr, Container *container) {
    set_textarea_active();
    if (client_by_name(app, "search_menu") == nullptr) {
        start_search_menu();
    }
}

static void
paint_search(AppClient *client, cairo_t *cr, Container *container) {
    IconButton *data = (IconButton *) container->user_data;

#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    int border_size = 1;
    
    bool active = false;
    bool text_empty = false;
    if (auto *con = container_by_name("main_text_area", container)) {
        auto *text_data = (TextAreaData *) con->user_data;
        text_empty = text_data->state->text.empty();
        active = con->parent->active;
    }
    
    if (active)
        border_size = 2;
    if (container->state.mouse_hovering)
        border_size = 2;
    
    // Paint border
//    set_rect(cr, container->real_bounds);
    if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
        if (active || container->state.mouse_pressing) {
            set_argb(cr, config->color_taskbar_search_bar_pressed_border);
        } else {
            set_argb(cr, config->color_taskbar_search_bar_hovered_border);
        }
    } else {
        set_argb(cr, config->color_taskbar_search_bar_default_border);
    }
    
    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, border_size);
    cairo_fill(cr);
    
    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, border_size, container->real_bounds.h);
    cairo_fill(cr);
    
    cairo_rectangle(cr,
                    container->real_bounds.x + container->real_bounds.w - border_size,
                    container->real_bounds.y,
                    border_size,
                    container->real_bounds.h);
    cairo_fill(cr);
    
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h - border_size,
                    container->real_bounds.w,
                    border_size);
    cairo_fill(cr);
    
    // Paint background
    if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
        if (active || container->state.mouse_pressing) {
            set_argb(cr, config->color_taskbar_search_bar_pressed_background);
        } else {
            set_argb(cr, config->color_taskbar_search_bar_hovered_background);
        }
    } else {
        set_argb(cr, config->color_taskbar_search_bar_default_background);
    }
    cairo_rectangle(cr,
                    container->real_bounds.x + border_size,
                    container->real_bounds.y + border_size,
                    container->real_bounds.w - border_size * 2,
                    container->real_bounds.h - border_size * 2);
    cairo_fill(cr);
    
    // Paint search icon
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);

    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE721", strlen("\uE83F"));

    if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
        if (active || container->state.mouse_pressing) {
            set_argb(cr, config->color_taskbar_search_bar_pressed_icon);
        } else {
            set_argb(cr, config->color_taskbar_search_bar_hovered_icon);
        }
    } else {
        set_argb(cr, config->color_taskbar_search_bar_default_icon);
    }

    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);

    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 12 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi));
    pango_cairo_show_layout(cr, layout);
    
    if (text_empty) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        validate_layout(client, layout);
        std::string text("Type here to search");
        pango_layout_set_text(layout, text.c_str(), text.size());
        
        if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
            if (active || container->state.mouse_pressing) {
                set_argb(cr, config->color_taskbar_search_bar_pressed_text);
            } else {
                set_argb(cr, config->color_taskbar_search_bar_hovered_text);
            }
        } else {
            set_argb(cr, config->color_taskbar_search_bar_default_text);
        }
        
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);
        
        cairo_move_to(cr,
                      container->real_bounds.x + (12 + 16 + 12) * config->dpi,
                      container->real_bounds.y + container->real_bounds.h / 2 -
                      ((logical.height / PANGO_SCALE) / 2));
        pango_cairo_show_layout(cr, layout);
    }
}

void update_battery_animation_timeout(App *app, AppClient *client, Timeout *timeout, void *userdata) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    timeout->keep_running = true;
    
    auto *data = static_cast<BatteryInfo *>(userdata);
    if (data->status != "Charging")
        return;
    
    data->animating_capacity_index++;
    if (data->animating_capacity_index > 10)
        data->animating_capacity_index = data->capacity_index;
    
    request_refresh(app, client);
}

void update_battery_status_timeout(App *app, AppClient *client, Timeout *timeout, void *userdata) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Improvement: could use a conditional variable and thread since this takes about 70ms
    if (timeout) {
        timeout->keep_running = true;
    }
    
    auto data = (BatteryInfo *) userdata;
    long current_time = get_current_time_in_ms();
    
    int previous_animating_capacity = 0;
    bool was_charging_on_previous_status_update = data->status == "Charging";
    if (was_charging_on_previous_status_update) {
        previous_animating_capacity = data->animating_capacity_index;
    }
    
    std::string line;
    std::ifstream status("/sys/class/power_supply/BAT0/status");
    if (status.is_open()) {
        data->status = "Missing BAT0";
        if (getline(status, line)) {
            data->status = line;
        }
        status.close();
    } else {
        data->status = "Missing BAT0";
    }
    
    std::ifstream capacity("/sys/class/power_supply/BAT0/capacity");
    if (capacity.is_open()) {
        data->capacity = "0";
        if (getline(capacity, line)) {
            data->capacity = line;
        }
        capacity.close();
    } else {
        data->capacity = "0";
    }
    
    float i = std::stoi(data->capacity);
    if (i == 5)
        i = 4;
    int rounded = std::round(i / 10) * 10;
    int capacity_index = std::floor(((double) (rounded)) / 10.0);
    
    if (capacity_index > 10)
        capacity_index = 10;
    if (capacity_index < 0)
        capacity_index = 0;
    
    data->capacity_index = capacity_index;
    if (was_charging_on_previous_status_update) {
        data->animating_capacity_index = previous_animating_capacity;
    } else {
        data->animating_capacity_index = data->capacity_index;
    }
    
    request_refresh(app, client);
}

void paint_battery(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client_entity, cr, container);
    container->real_bounds = start;
    
    auto *data = static_cast<BatteryInfo *>(container->user_data);
    assert(data);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client_entity, layout);
    
    std::string regular[] = {"\uE678", "\uE679", "\uE67A", "\uE67B", "\uE67C", "\uE67D", "\uE67E", "\uE67F", "\uE680",
                             "\uE681", "\uE682"};
    
    std::string charging[] = {"\uE683", "\uE684", "\uE685", "\uE686", "\uE687", "\uE688", "\uE689", "\uE68A", "\uE68B",
                              "\uE68C", "\uE68D"};

    if (winbar_settings->battery_notifications) {
        if (std::stoi(data->capacity) <= 5) {
            set_argb(cr, config->color_taskbar_attention_background);
            cairo_rectangle(cr,
                            container->real_bounds.x,
                            container->real_bounds.y,
                            container->real_bounds.w,
                            container->real_bounds.h);
            cairo_fill(cr);
        }
        
        static bool full_warning = false;
        if (std::stoi(data->capacity) >= 80 && !full_warning) {
            full_warning = true;
            launch_command("notify-send \"Battery Full\" \"Unplug charger\" -a \"Winbar\" -i battery-full-charged");
        } else if (std::stoi(data->capacity) < 75) {
            full_warning = false;
        }
        static bool low_warning = false;
        if (std::stoi(data->capacity) <= 25 && !low_warning) {
            low_warning = true;
            launch_command("notify-send \"Battery Low\" \"Plug-in charger\" -a \"Winbar\" -i battery-empty");
        } else if (std::stoi(data->capacity) > 30) {
            low_warning = false;
        }
    }

    if (data->status == "Full") {
        pango_layout_set_text(layout, regular[10].c_str(), strlen("\uE83F"));
    } else if (data->status == "Charging") {
//        surface = data->charging_surfaces[data->animating_capacity_index];
        pango_layout_set_text(layout, charging[data->animating_capacity_index].c_str(), strlen("\uE83F"));
    } else {
//        surface = data->normal_surfaces[data->capacity_index];
        pango_layout_set_text(layout, regular[data->capacity_index].c_str(), strlen("\uE83F"));
    }
    int battery_icon_width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &battery_icon_width, &height);

    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + (container->real_bounds.w - 12 * config->dpi) -
                         battery_icon_width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    if (((container->state.mouse_hovering || container->state.mouse_pressing ||
          container->state.mouse_dragging) && winbar_settings->battery_expands_on_hover) ||
        winbar_settings->battery_label_always_on) {
        // Draw percentage when hovered
        PangoLayout *percentage =
                get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        validate_layout(client_entity, layout);
        std::string status;
        if (data->status == "Charging") {
            status = "+";
        } else if (data->status != "Full") {
            status = "-";
        } else {
            status = "=";
        }
        std::string text = status + data->capacity + "%";
        pango_layout_set_text(percentage, text.c_str(), text.size());

        int width;
        pango_layout_get_pixel_size_safe(percentage, &width, &height);

        bool resize = false;
        pango_layout_get_pixel_size_safe(percentage, &width, &height);
        if (data->previous_volume_width != width) {
            data->already_expanded = false;
            data->previous_volume_width = width;
            resize = true;
        }


        float speed = 110.0f;
        if (!data->already_expanded) {
            if (resize) {
                speed = 40.0f;
            } else {
                data->start_time = get_current_time_in_ms();
            }
            
            data->already_expanded = true;
            client_create_animation(app, client_entity, &container->wanted_bounds.w, container->lifetime, 0, speed, nullptr,
                                    (config->dpi * 24) + (width + 7 * config->dpi), true);
        }

        ArgbColor color = config->color_taskbar_button_icons;
        color.a = (get_current_time_in_ms() - data->start_time) / speed;
        if (color.a > 1)
            color.a = 1;
        set_argb(cr, color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + 4 * config->dpi),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, percentage);
    } else {
        if (data->already_expanded) {
            data->already_expanded = false;
            client_create_animation(app, client_entity, &container->wanted_bounds.w, container->lifetime, 0, 100.0f, nullptr,
                                    24 * config->dpi, true);
        }
    }
}

static void invalidate_icon_button_press_if_window_open(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (IconButton *) container->user_data;
    
    if (data->invalidate_button_press_if_client_with_this_name_is_open == app->previously_closed_client && !app->previously_closed_client.empty()) {
        auto v = get_current_time_in_ms() - app->previously_closed_client_time;
        if (v < 150) {
            data->invalid_button_down = true;
            return;
        }
    }
    data->invalid_button_down = false;
}

static void
make_battery_button(Container *parent, AppClient *client_entity) {
    auto *c = new Container();
    c->parent = parent;
    c->type = hbox;
    c->wanted_bounds.w = 24 * config->dpi;
    c->wanted_bounds.h = FILL_SPACE;
    c->when_paint = paint_battery;
    c->when_clicked = clicked_battery;
    c->name = "battery";
    
    auto *data = new BatteryInfo;
    data->invalidate_button_press_if_client_with_this_name_is_open = "battery_menu";
    c->when_mouse_down = invalidate_icon_button_press_if_window_open;
    c->when_mouse_leaves_container = mouse_leaves_battery;
    c->when_fine_scrolled = scrolled_battery;
    c->user_data = data;
    
    std::string line;
    std::ifstream capacity("/sys/class/power_supply/BAT0/type");
    if (capacity.is_open()) {
        if (getline(capacity, line)) {
            if (line != "UPS") {
                parent->children.push_back(c);
                app_timeout_create(app, client_entity, 30000, update_battery_status_timeout, data,
                                   "update_battery_status_timeout");
                update_battery_status_timeout(app, client_entity, nullptr, data);
                
                app_timeout_create(app, client_entity, 700, update_battery_animation_timeout, data,
                                   "update_battery_animation_timeout");
            } else {
                delete c;
            }
        }
        capacity.close();
    } else {
        delete c;
    }
};

static void
scrolled_workspace(AppClient *client_entity,
                   cairo_t *cr,
                   Container *container,
                   int horizontal_scroll,
                   int vertical_scroll) {
    int current = desktops_current(app);
    current -= vertical_scroll;
    current -= horizontal_scroll;// we subtract to correct the direction
    
    int count = desktops_count(app);
    if (current < 0)
        current = count - 1;
    if (current >= count)
        current = 0;
    desktops_change(app, current);
}

void gnome_stuck_mouse_state_fix(App *app, AppClient *client, Timeout *, void *) {
    if (valid_client(app, client)) {
        client->motion_event_x = -1;
        client->motion_event_y = -1;
        handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
    }
}

static void
clicked_workspace(AppClient *client_entity, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client_entity, cr, container);
        return;
    }
    if (dbus_connection_session) {
        for (const auto &s: running_dbus_services) {
            if (s == "org.kde.kglobalaccel") {
                // On KDE try to show the desktop grid
                if (dbus_kde_show_desktop_grid()) {
                    return;
                }
            } else if (s == "org.gnome.Shell") {
                // On Gnome try to show the overview screen
                if (dbus_gnome_show_overview()) {
                    app_timeout_create(app, client_entity, 100, gnome_stuck_mouse_state_fix, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
                    return;
                }
            }
        }
    }
    
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_1) {// left
        scrolled_workspace(client_entity, cr, container, 0, -1);
    } else {// right
        scrolled_workspace(client_entity, cr, container, 0, 1);
    }
}

static void
clicked_super(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        open_right_click_menu(client, cr, container);
        return;
    }
    auto data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        start_app_menu();
    }
}

static void
paint_wifi(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client, cr, container);
    container->real_bounds = start;
    
    bool up = false;
    bool wired = false;
    wifi_state(client, &up, &wired);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    validate_layout(client, layout);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (up) {
        if (wired) {
            pango_layout_set_text(layout, "\uE839", strlen("\uE83F"));
        } else {
            pango_layout_set_text(layout, "\uEC3F", strlen("\uE83F"));
        }
    } else {
        if (wired) {
            pango_layout_set_text(layout, "\uF384", strlen("\uE83F"));
        } else {
            pango_layout_set_text(layout, "\uEB5E", strlen("\uE83F"));
        }
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
fill_root(App *app, AppClient *client, Container *root) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    root->when_paint = paint_background;
    root->when_clicked = clicked_root;
    root->type = hbox;
    root->spacing = 0;
    
    Container *button_super = root->child(48 * config->dpi, FILL_SPACE);
    Container *field_search = root->child(344 * config->dpi, FILL_SPACE);
//    Container *button_chatgpt = root->child(48 * config->dpi, FILL_SPACE);
    Container *button_workspace = root->child(48 * config->dpi, FILL_SPACE);
    Container *container_icons = root->child(FILL_SPACE, FILL_SPACE);
    Container *button_systray = root->child(24 * config->dpi, FILL_SPACE);
    
    make_plugins(app, client, root);
    
    Container *button_bluetooth = root->child(24 * config->dpi, FILL_SPACE);
    make_battery_button(root, client);
    Container *button_wifi = root->child(24 * config->dpi, FILL_SPACE);
    Container *button_volume = root->child(24 * config->dpi, FILL_SPACE);
    Container *button_date = root->child(80 * config->dpi, FILL_SPACE);
    Container *button_action_center = root->child(48 * config->dpi, FILL_SPACE);
    Container *button_minimize = root->child(5 * config->dpi, FILL_SPACE);
    
    button_super->when_paint = paint_super;
    auto button_super_data = new SuperButton;
    button_super_data->invalidate_button_press_if_client_with_this_name_is_open = "app_menu";
    button_super->user_data = button_super_data;
    button_super->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_super->name = "super";
    button_super->when_clicked = clicked_super;
    button_super->when_mouse_motion = [](AppClient *client, cairo_t *cr, Container *container) {
        if (winbar_settings->open_start_menu_on_bottom_left_hover) {
            if (bounds_contains(
                    Bounds(container->real_bounds.x, container->real_bounds.y + container->real_bounds.h - 2, 2, 2),
                    client->mouse_current_x, client->mouse_current_y)) {
                if (!client_by_name(client->app, "app_menu")) {
                    start_app_menu();
                }
            }
        }
    };
    
    field_search->when_paint = paint_search;
    field_search->when_mouse_down = clicked_search;
    field_search->receive_events_even_if_obstructed = true;
    field_search->user_data = new IconButton;
    field_search->name = "field_search";
    
    TextAreaSettings settings(config->dpi);
    settings.font_size = 12 * config->dpi;
    settings.font = config->font;
    settings.color = ArgbColor(0, 0, 0, 1);
    settings.color_cursor = ArgbColor(0, 0, 0, 1);
    settings.single_line = true;
    settings.wrap = false;
    settings.right_show_amount = 2;
    settings.bottom_show_amount = 2;
    field_search->wanted_pad.x = (12 + 16 + 12) * config->dpi;
    field_search->wanted_pad.w = 8 * config->dpi;
    auto *con = field_search->child(FILL_SPACE, FILL_SPACE);
    Container *textarea = make_textarea(app, client, con, settings);
    textarea->name = "main_text_area";
    textarea->parent->alignment = ALIGN_CENTER | ALIGN_LEFT;
    
    button_workspace->when_paint = paint_workspace;
    button_workspace->user_data = new IconButton;
    button_workspace->when_scrolled = scrolled_workspace;
    button_workspace->when_clicked = clicked_workspace;
    button_workspace->name = "workspace";
    
//    button_chatgpt->when_paint = paint_chatgpt;
//    auto button_chatgpt_data = new IconButton;
//    button_chatgpt_data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
//    paint_surface_with_image(button_chatgpt_data->surface, as_resource_path("chatgpt.svg"),
//                             24 * config->dpi, nullptr);
//    button_chatgpt->user_data = button_chatgpt_data;
//    button_chatgpt->when_clicked = clicked_chatgpt;
//    button_chatgpt->name = "chatgpt";
    
    container_icons->spacing = 1;
    container_icons->type = hbox;
    container_icons->name = "icons";
    container_icons->when_paint = paint_all_icons;
    container_icons->when_clicked = clicked_icons_background;
    container_icons->should_layout_children = false;
    container_icons->distribute_overflow_to_children = true;
    
    button_systray->when_paint = paint_systray;
    auto button_systray_data = new IconButton;
    button_systray_data->invalidate_button_press_if_client_with_this_name_is_open = "display";
    button_systray->user_data = button_systray_data;
    button_systray->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_systray->when_clicked = clicked_systray;
    button_systray->name = "systray";
    
    button_bluetooth->exists = false;
    button_bluetooth->when_paint = paint_bluetooth;
    auto button_bluetooth_data = new IconButton;
    button_bluetooth_data->invalidate_button_press_if_client_with_this_name_is_open = "bluetooth_menu";
    button_bluetooth->user_data = button_bluetooth_data;
    button_bluetooth->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_bluetooth->when_clicked = clicked_bluetooth;
    button_bluetooth->name = "bluetooth";
    
    button_wifi->when_paint = paint_wifi;
    button_wifi->when_clicked = clicked_wifi;
    button_wifi->name = "wifi";
    auto wifi_data = new IconButton;
    wifi_data->invalidate_button_press_if_client_with_this_name_is_open = "wifi_menu";
    button_wifi->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_wifi->user_data = wifi_data;
    
    button_volume->when_paint = paint_volume;
    button_volume->when_clicked = clicked_volume;
    button_volume->when_fine_scrolled = scrolled_volume;
    button_volume->when_mouse_leaves_container = mouse_leaves_volume;
    button_volume->name = "volume";
    auto volume_data = new VolumeButton;
    volume_data->invalidate_button_press_if_client_with_this_name_is_open = "volume";
    button_volume->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_volume->user_data = volume_data;
    
    button_date->when_paint = paint_date;
    button_date->when_clicked = clicked_date;
    auto button_date_data = new IconButton;
    button_date->user_data = button_date_data;
    button_date_data->invalidate_button_press_if_client_with_this_name_is_open = "date_menu";
    button_date->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_date->name = "date";
    
    app_timeout_create(app, client, 60000, update_time, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
    app_timeout_create(app, client, 10000, late_classes_update, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
    
    button_action_center->when_paint = paint_action_center;
    auto action_center_data = new ActionCenterButtonData;
    button_action_center->user_data = action_center_data;
    
    action_center_data->invalidate_button_press_if_client_with_this_name_is_open = "action_center";
    button_action_center->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_action_center->when_clicked = clicked_action_center;
    button_action_center->name = "action";
    
    button_minimize->when_paint = paint_minimize;
    button_minimize->user_data = new IconButton;
    button_minimize->when_clicked = clicked_minimize;
    button_minimize->when_fine_scrolled = scrolled_volume;
    button_minimize->when_mouse_leaves_container = mouse_leaves_volume;
    button_minimize->name = "minimize";
}

static void
load_pinned_icons();

static int inotify_fd = -1;
static int inotify_status_fd = -1;
static int inotify_capacity_fd = -1;
static Timeout *pinned_timeout = nullptr;

static void
when_taskbar_closed(AppClient *client) {
    if (inotify_fd != -1) close(inotify_fd);
    inotify_fd = -1;
    inotify_status_fd = -1;
    inotify_capacity_fd = -1;
    update_pinned_items_file(true);
    pinned_timeout = nullptr;
}

static void
taskbar_on_screen_size_change(App *app, AppClient *client) {
    ScreenInformation *primary_screen = client->screen_information;
    for (auto s: screens)
        if (s->is_primary) primary_screen = s;
    client->screen_information = primary_screen;
    if (!primary_screen) return;
    
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
    
    xcb_ewmh_wm_strut_partial_t wm_strut = {};
    
    auto height = screen->height_in_pixels - primary_screen->height_in_pixels + client->bounds->h;
    wm_strut.bottom = height;
    wm_strut.bottom_start_x = primary_screen->x;
    wm_strut.bottom_end_x = primary_screen->x + client->bounds->w;
    xcb_ewmh_set_wm_strut_partial(&app->ewmh,
                                  client->window,
                                  wm_strut);
    
    client_set_position_and_size(app, client,
                                 primary_screen->x,
                                 primary_screen->y + primary_screen->height_in_pixels -
                                 config->taskbar_height,
                                 primary_screen->width_in_pixels,
                                 config->taskbar_height);
    handle_configure_notify(app, client, primary_screen->x,
                            primary_screen->y + primary_screen->height_in_pixels -
                            config->taskbar_height, primary_screen->width_in_pixels,
                            config->taskbar_height);
    xcb_flush(app->connection);
/*    for (auto *c: app->clients) {
        if (c->popup) {
            client_close_threaded(app, c);
            if (c->window == app->grab_window) {
                app->grab_window = -1;
                xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->grab_window, XCB_MOD_MASK_ANY);
                xcb_flush(app->connection);
            }
        }
    }*/
}

static void
update_window_title_name(xcb_window_t window) {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon: icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    for (auto windows_data: data->windows_data_list) {
                        if (windows_data->id == window) {
                            const xcb_get_property_cookie_t &propertyCookie = xcb_ewmh_get_wm_name(
                                    &app->ewmh, window);
                            xcb_ewmh_get_utf8_strings_reply_t data;
                            uint8_t success = xcb_ewmh_get_wm_name_reply(&app->ewmh, propertyCookie, &data,
                                                                         nullptr);
                            if (success) {
                                windows_data->title = strndup(data.strings, data.strings_len);
                                xcb_ewmh_get_utf8_strings_reply_wipe(&data);
                                return;
                            }
                            
                            const xcb_get_property_cookie_t &cookie = xcb_icccm_get_wm_name(app->connection,
                                                                                            window);
                            xcb_icccm_get_text_property_reply_t reply;
                            success = xcb_icccm_get_wm_name_reply(app->connection, cookie, &reply,
                                                                  nullptr);
                            if (success) {
                                windows_data->title = std::string(reply.name, reply.name_len);
                                xcb_icccm_get_text_property_reply_wipe(&reply);
                                return;
                            }
                            
                            return;
                        }
                    }
                }
            }
        }
        request_refresh(app, client);
    }
}

void remove_window(App *app, xcb_window_t window);

void clear_thumbnails() {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon: icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    for (auto windows_data: data->windows_data_list) {
                        // update the size of the surface
                        if (windows_data->window_surface) {
                            cairo_xcb_surface_set_size(windows_data->window_surface,
                                                       windows_data->width, windows_data->height);
                            
                            cairo_surface_destroy(windows_data->raw_thumbnail_surface);
                            cairo_destroy(windows_data->raw_thumbnail_cr);
                            cairo_surface_destroy(windows_data->scaled_thumbnail_surface);
                            cairo_destroy(windows_data->scaled_thumbnail_cr);
                            
                            windows_data->raw_thumbnail_surface = accelerated_surface(app,
                                                                                      client_by_name(
                                                                                              app,
                                                                                              "taskbar"),
                                                                                      windows_data->width,
                                                                                      windows_data->height);
                            windows_data->raw_thumbnail_cr = cairo_create(
                                    windows_data->raw_thumbnail_surface);
                            windows_data->scaled_thumbnail_surface = accelerated_surface(app,
                                                                                         client_by_name(
                                                                                                 app,
                                                                                                 "taskbar"),
                                                                                         option_width,
                                                                                         option_height);
                            windows_data->scaled_thumbnail_cr = cairo_create(
                                    windows_data->scaled_thumbnail_surface);
                        }
                    }
                }
            }
        }
    }
}

std::string decodePercentEncoding(const std::string& uri) {
    std::string result;
    for (std::size_t i = 0; i < uri.length(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.length()) {
            std::istringstream hex(uri.substr(i + 1, 2));
            int value;
            hex >> std::hex >> value;
            result += static_cast<char>(value);
            i += 2;
        } else {
            result += uri[i];
        }
    }
    return result;
}

std::string uriToFilePath(const std::string& uri) {
    const std::string prefix = "file://";
    if (uri.substr(0, prefix.size()) == prefix) {
        return decodePercentEncoding(uri.substr(prefix.size()));
    }
    return uri;  // return as-is if no "file://" prefix
}

static bool
window_event_handler(App *app, xcb_generic_event_t *event, xcb_window_t window) {
    for (auto c: app->clients)
        if (c->window == window)
            return false;
    // This will listen to configure notify events and check if it's about a
    // window we need a thumbnail of and update its size if so.
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CONFIGURE_NOTIFY: {
            auto *e = (xcb_configure_notify_event_t *) event;
            if (auto client = client_by_name(app, "taskbar")) {
                if (client->root) {
                    if (auto icons = container_by_name("icons", client->root)) {
                        for (auto icon: icons->children) {
                            auto *data = static_cast<LaunchableButton *>(icon->user_data);
                            for (auto windows_data: data->windows_data_list) {
                                if (windows_data->id == e->window) {
                                    // update the size of the surface
                                    if (windows_data->window_surface && (e->width != windows_data->width ||
                                                                         e->height != windows_data->height)) {
                                        windows_data->width = e->width;
                                        windows_data->height = e->height;
                                        cairo_xcb_surface_set_size(windows_data->window_surface,
                                                                   windows_data->width, windows_data->height);
                                        
                                        cairo_surface_destroy(windows_data->raw_thumbnail_surface);
                                        cairo_destroy(windows_data->raw_thumbnail_cr);
                                        cairo_surface_destroy(windows_data->scaled_thumbnail_surface);
                                        cairo_destroy(windows_data->scaled_thumbnail_cr);
                                        
                                        windows_data->raw_thumbnail_surface = accelerated_surface(app,
                                                                                                  client_by_name(
                                                                                                          app,
                                                                                                          "taskbar"),
                                                                                                  windows_data->width,
                                                                                                  windows_data->height);
                                        windows_data->raw_thumbnail_cr = cairo_create(
                                                windows_data->raw_thumbnail_surface);
                                        windows_data->scaled_thumbnail_surface = accelerated_surface(app,
                                                                                                     client_by_name(
                                                                                                             app,
                                                                                                             "taskbar"),
                                                                                                     option_width,
                                                                                                     option_height);
                                        windows_data->scaled_thumbnail_cr = cairo_create(
                                                windows_data->scaled_thumbnail_surface);
                                    }
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            auto e = (xcb_property_notify_event_t *) event;
//            const xcb_get_atom_name_cookie_t &cookie = xcb_get_atom_name(app->connection, e->atom);
//            xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, nullptr);
//            char *string = xcb_get_atom_name_name(reply);
//            printf("%s\n", string);
            if (e->atom == get_cached_atom(app, "WM_NAME") ||
                e->atom == get_cached_atom(app, "_NET_WM_NAME")) {
                update_window_title_name(e->window);
            } else if (e->atom == get_cached_atom(app, "_NET_WM_NAME") ||
                       e->atom == get_cached_atom(app, "_NET_WM_NAME")) {
                update_window_title_name(e->window);
            } else if (e->atom == get_cached_atom(app, "WM_CLASS")) {
                late_classes_update(app, client_by_name(app, "taskbar"), nullptr, nullptr);
            } else if (e->atom == get_cached_atom(app, "_NET_WM_CLASS")) {
                late_classes_update(app, client_by_name(app, "taskbar"), nullptr, nullptr);
            } else if (e->atom == get_cached_atom(app, "_GTK_FRAME_EXTENTS")) {
                if (auto client = client_by_name(app, "taskbar")) {
                    if (client->root) {
                        if (auto icons = container_by_name("icons", client->root)) {
                            for (auto icon: icons->children) {
                                auto *data = static_cast<LaunchableButton *>(icon->user_data);
                                for (auto windows_data: data->windows_data_list) {
                                    if (windows_data->id == e->window) {
                                        auto cookie = xcb_get_property(app->connection, 0, e->window,
                                                                       get_cached_atom(app, "_GTK_FRAME_EXTENTS"),
                                                                       XCB_ATOM_CARDINAL, 0, 4);
                                        auto reply = xcb_get_property_reply(app->connection, cookie, nullptr);
                                        
                                        if (reply) {
                                            int length = xcb_get_property_value_length(reply);
                                            if (length != 0) {
                                                auto gtkFrameExtents = static_cast<uint32_t *>(xcb_get_property_value(
                                                        reply));
                                                windows_data->gtk_left_margin = (int) gtkFrameExtents[0];
                                                windows_data->gtk_right_margin = (int) gtkFrameExtents[1];
                                                windows_data->gtk_top_margin = (int) gtkFrameExtents[2];
                                                windows_data->gtk_bottom_margin = (int) gtkFrameExtents[3];
                                            }
                                            free(reply);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } else if (e->atom == get_cached_atom(app, "_NET_WM_STATE")) {
                xcb_generic_error_t *err = nullptr;
                auto cookie = xcb_get_property(app->connection, 0, e->window, get_cached_atom(app, "_NET_WM_STATE"),
                                               XCB_ATOM_ATOM, 0,
                                               BUFSIZ);
                xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, &err);
                if (reply) {
                    if (reply->type == XCB_ATOM_ATOM) {
                        auto *state_atoms = (xcb_atom_t *) xcb_get_property_value(reply);
                        bool attention = false;
                        bool found_fullscreen = false;
                        for (unsigned int a = 0; a < reply->length; a++) {
                            if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_DEMANDS_ATTENTION")) {
                                attention = true;
                                if (auto client = client_by_name(app, "taskbar")) {
                                    if (client->root) {
                                        if (auto icons = container_by_name("icons", client->root)) {
                                            for (auto icon: icons->children) {
                                                auto *data = static_cast<LaunchableButton *>(icon->user_data);
                                                for (auto windows_data: data->windows_data_list) {
                                                    if (windows_data->id == e->window) {
                                                        client_create_animation(app, client,
                                                                                &data->wants_attention_amount, data->lifetime, 0, 10000,
                                                                                0,
                                                                                1);
                                                        windows_data->wants_attention = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                free(reply);
                                reply = nullptr;
                                break;
                            } else if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_FULLSCREEN")) {
                                found_fullscreen = true;
                            }
                        }
                        if (active_window == e->window) {
                            someone_is_fullscreen_and_covering = found_fullscreen;
                            if (someone_is_fullscreen_and_covering)
                                client_close_threaded(app, client_by_name(app, "windows_selector"));
                        }
                        if (!attention) {
                            if (auto client = client_by_name(app, "taskbar")) {
                                if (client->root) {
                                    if (auto icons = container_by_name("icons", client->root)) {
                                        for (auto icon: icons->children) {
                                            auto *data = static_cast<LaunchableButton *>(icon->user_data);
                                            for (auto windows_data: data->windows_data_list) {
                                                if (windows_data->id == e->window) {
                                                    client_create_animation(app, client,
                                                                            &data->wants_attention_amount, data->lifetime, 0, 0,
                                                                            nullptr, 0);
                                                    windows_data->wants_attention = false;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (reply)
                        free(reply);
                } else if (e->atom == get_cached_atom(app, "_NET_WM_DESKTOP")) {
                    // TODO: error check
                    auto r = xcb_get_property(app->connection, False, e->window,
                                              get_cached_atom(app, "_NET_WM_DESKTOP"),
                                              XCB_ATOM_CARDINAL, 0, 32);
                    auto re = xcb_get_property_reply(app->connection, r, nullptr);
                    if (re) {
                        auto current_desktop = reinterpret_cast<uint32_t *>(xcb_get_property_value(re))[0];
                        printf("window: %d, desktop %d\n", e->window, current_desktop);
                        free(re);
                    }
                }
                if (err) {
                    free(err);
                    err = nullptr;
                }
            }
            break;
        }
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) event;
            if (auto client = client_by_name(app, "taskbar")) {
                if (client->root) {
                    if (auto icons = container_by_name("icons", client->root)) {
                        for (auto icon: icons->children) {
                            auto *data = static_cast<LaunchableButton *>(icon->user_data);
                            for (auto windows_data: data->windows_data_list) {
                                if (windows_data->id == e->window) {
                                    windows_data->mapped = true;
                                    data->animation_bounce_amount = 0;
                                    data->animation_bounce_direction = 1;
                                    client_create_animation(app, client, &data->animation_bounce_amount, data->lifetime, 0,
                                                            651.2, nullptr, 1);
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case XCB_UNMAP_NOTIFY: {
            auto *e = (xcb_unmap_notify_event_t *) event;
            if (auto client = client_by_name(app, "taskbar")) {
                if (client->root) {
                    if (auto icons = container_by_name("icons", client->root)) {
                        for (auto icon: icons->children) {
                            auto *data = static_cast<LaunchableButton *>(icon->user_data);
                            for (auto windows_data: data->windows_data_list) {
                                if (windows_data->id == e->window) {
                                    windows_data->mapped = false;
                                    data->animation_bounce_amount = 0;
                                    data->animation_bounce_direction = 0;
                                    client_create_animation(app, client, &data->animation_bounce_amount, data->lifetime, 0,
                                                            651.2, nullptr, 1);
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case XCB_SELECTION_NOTIFY: {
            auto *e = (xcb_selection_notify_event_t *) event;
            
            if (e->property == XCB_ATOM_NONE) {
                fprintf(stderr, "kind=error:message=couldn't convert selection");
                break;
            }
            
            xcb_get_property_cookie_t prop_cookie;
            xcb_get_property_reply_t *prop_reply;
            char *data = NULL;
            
            /* Request the property */
            prop_cookie = xcb_get_property(
                    app->connection,
                    0,                                 // Delete = False
                    e->requestor, // The window that is requesting the selection
                    e->property,  // The property that holds the data
                    e->target,    // Type of the property
                    0,                                 // Offset in longs
                    UINT32_MAX                         // Length in 32-bit words
            );
            prop_reply = xcb_get_property_reply(app->connection, prop_cookie, NULL);
            
            int len = 0;
            if (prop_reply && xcb_get_property_value_length(prop_reply) > 0) {
                data = (char *) xcb_get_property_value(prop_reply);
                len = xcb_get_property_value_length(prop_reply);
            }
            std::string d(data, len);
            std::istringstream stream(d);
            std::string line;
            std::string files;
            int files_count = 0;
            while (std::getline(stream, line)) {  // Read line by line
                if (!line.empty()) {
                    files += " \"";
                    files += uriToFilePath(trimnewlines(line));
                    files += "\"";
                    files_count++;
                }
            }
            bool result = prop_reply != nullptr;
            
            /* Free the property reply */
            free(prop_reply);
            
            auto client = client_by_name(app, "taskbar");
            
            xcb_client_message_event_t status_event = {};
            status_event.response_type = XCB_CLIENT_MESSAGE;
            status_event.format = 32;
            status_event.window = client->drag_and_drop_source;
            status_event.type = get_cached_atom(app, "XdndFinished");
            status_event.data.data32[0] = client->window; // drag and drop target (us)
            status_event.data.data32[3] = 0; // drag and drop target (us)
            status_event.data.data32[2] = 0; // drag and drop target (us)
            status_event.data.data32[1] = result;
            status_event.data.data32[2] = get_cached_atom(app, "XdndActionCopy");
            
            xcb_send_event(app->connection, false, client->drag_and_drop_source, XCB_EVENT_MASK_NO_EVENT,
                           reinterpret_cast<const char *> (&status_event));
            xcb_flush(app->connection);
            
            bool dropped_on_icon = false;
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon: icons->children) {
                    if (icon->state.mouse_hovering && !icon->state.mouse_dragging) {
                        auto *data = static_cast<LaunchableButton *>(icon->user_data);
                        if (!data->command_launched_by.empty()) {
                            dropped_on_icon = true;
                            launch_command(data->command_launched_by + files);
                            start_zoom_animation(client, icon);
                        }
                        break;
                    }
                }
            }
            if (!dropped_on_icon && files_count == 1) {
                // Add a new item, assuming the file *path* will be the 'luancher' command
                add_item_clicked(nullptr, nullptr, nullptr);
                if (auto c = client_by_name(app, "pinned_icon_editor")) {
                    if (auto con = container_by_name("launch_command_field", c->root)) {
                        auto launch_command_field_data = (TextAreaData *) con->user_data;
                        launch_command_field_data->state->text = trim(files);
                    }
                }
            }
            
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            auto *e = (xcb_client_message_event_t *) event;
        
            // Drag and drop stuff from: https://www.acc.umu.se/~vatten/XDND.html
            if (e->type == get_cached_atom(app, "XdndEnter")) {
                if (auto client = client_by_name(app, "taskbar")) {
                    client->drag_and_drop_source = e->data.data32[0];
                    client->drag_and_drop_version = e->data.data32[1] >> 24;
                    
                    unsigned long count = 0;
                    Atom *formats = nullptr;
                    Atom real_formats[6];
                    xcb_get_property_reply_t *reply;
                    Bool list = e->data.data32[1] & 1;
                    int format = None;
                    if (client->drag_and_drop_version > 5)
                        break;
                    
                    if (list) {
                        xcb_atom_t actualType;
                        int32_t actualFormat;
                        uint32_t bytesAfter;
                        xcb_get_property_cookie_t cookie;
                        cookie = xcb_get_property(app->connection,
                                                  0,                    // Delete = False
                                                  client->drag_and_drop_source,
                                                  get_cached_atom(app, "XdndTypeList"),
                                                  XCB_ATOM_ATOM,        // Property type (ATOM = 4 bytes per item)
                                                  0,
                                                  UINT32_MAX);          // Equivalent to LONG_MAX
                        reply = xcb_get_property_reply(app->connection, cookie, NULL);
                        if (reply) {
                            actualType = reply->type;
                            actualFormat = reply->format;
                            bytesAfter = reply->bytes_after;
                            count = xcb_get_property_value_length(reply) / sizeof(xcb_atom_t);
                            formats = static_cast<Atom *>(xcb_get_property_value(reply));
                        }
                    } else {
                        count = 0;
                        
                        if (e->data.data32[2] != None)
                            real_formats[count++] = e->data.data32[2];
                        if (e->data.data32[3] != None)
                            real_formats[count++] = e->data.data32[3];
                        if (e->data.data32[4] != None)
                            real_formats[count++] = e->data.data32[4];
                        formats = real_formats;
                    }
                    
                    auto XtextUriList = get_cached_atom(app, "XtextUriList");
                    auto XtextPlain = get_cached_atom(app, "XtextPlain");
                    unsigned long i = 0;
                    client->drag_and_drop_formats.clear();
                    for (i = 0; i < count; i++) {
                        xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(app->connection, formats[i]);
                        xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, nullptr);
                        if (reply) {
                            // Extract the name from the reply
                            int name_len = xcb_get_atom_name_name_length(reply);
                            const char *name = xcb_get_atom_name_name(reply);
                            std::string form(name, name_len);
                            client->drag_and_drop_formats.push_back(form);
                            free(reply);
                        }
                    }
                    if (list && reply) {
                        free(reply);
                    }
                    have_drag = true;
                }
            } else if (e->type == get_cached_atom(app, "XdndPosition")) {
                if (auto client = client_by_window(app, e->window)) {
                    if (client->name == "windows_selector") {
                        drag_and_dropping = true;
                    }
                    uint32_t x = e->data.data32[2] >> 16;
                    uint32_t y = e->data.data32[2] & 0xffff;
                
                    auto cookie = xcb_translate_coordinates(app->connection, app->screen->root, client->window, x, y);
                    auto reply = xcb_translate_coordinates_reply(app->connection, cookie, nullptr);
                    if (reply) {
                        x = reply->dst_x;
                        y = reply->dst_y;
                        free(reply);
                    }
                
                    client->motion_event_x = (int) x;
                    client->motion_event_y = (int) y;
                    handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
                    client_paint(app, client, true);
                
                    xcb_window_t drag_and_drop_source = e->data.data32[0];
                
                    xcb_client_message_event_t status_event = {};
                    status_event.response_type = XCB_CLIENT_MESSAGE;
                    status_event.format = 32;
                    status_event.window = drag_and_drop_source;
                    status_event.type = get_cached_atom(app, "XdndStatus");
                    status_event.data.data32[0] = client->window; // drag and drop target (us)
                    status_event.data.data32[2] = 0; // drag and drop target (us)
                    status_event.data.data32[3] = 0; // drag and drop target (us)
                    status_event.data.data32[1] = 1;
                    if (client->drag_and_drop_version >= 2)
                        status_event.data.data32[4] = get_cached_atom(app, "XdndActionCopy");
                
                    auto xcb = app->connection;
                
                    xcb_send_event(xcb, false, drag_and_drop_source, XCB_EVENT_MASK_NO_EVENT,
                                   reinterpret_cast<const char *> (&status_event));
                    xcb_flush(app->connection);
                }
            } else if (e->type == get_cached_atom(app, "XdndLeave")) {
                if (auto client = client_by_window(app, e->window)) {
                    if (client->name == "windows_selector") {
                        drag_and_dropping = false;
                    }
                    client->motion_event_x = (int) -1;
                    client->motion_event_y = (int) -1;
                    handle_mouse_motion(app, client, client->motion_event_x, client->motion_event_y);
                    client_paint(app, client, true);
                }
                have_drag = false;
            } else if (e->type == get_cached_atom(app, "XdndDrop")) {
                Time time = CurrentTime;
                if (auto client = client_by_name(app, "taskbar")) {
                    if (client->drag_and_drop_version >= 1)
                        time = e->data.data32[2];
                    std::string form;
                    for (const auto &format: client->drag_and_drop_formats) {
                        if (starts_with(format, "text/plain")) {
                            form = "text/plain";
                            break;
                        }
                    }
                    for (const auto &format: client->drag_and_drop_formats) {
                        if (starts_with(format, "text/uri-list")) {
                            form = "text/uri-list";
                            break;
                        }
                    }
                    if (!form.empty()) {
                        xcb_convert_selection(app->connection,
                                              client_by_name(app, "taskbar")->window,
                                              get_cached_atom(app, "XdndSelection"),
                                              get_cached_atom(app, form),
                                              get_cached_atom(app, "XdndSelection"),
                                              time
                        );
                        xcb_flush(app->connection);
                    }
                }
                have_drag = false;
            }
            break;
        }
    }
    return false;
}

void screenshot_active_window(App *app, AppClient *client, Timeout *, void *user_data) {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon: icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    for (auto windows_data: data->windows_data_list) {
                        if (windows_data->id == active_window) {
                            windows_data->take_screenshot();
                            return;
                        }
                    }
                }
            }
        }
    }
}

void inotify_event_wakeup(App *app, int fd) {
    /* Some systems cannot read integer variables if they are not
              properly aligned. On other systems, incorrect alignment may
              decrease performance. Hence, the buffer used for reading from
              the inotify file descriptor should have the same alignment as
              struct inotify_event. */
    
    char buf[4096]
            __attribute__ ((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    ssize_t len;
    
    /* Loop while events can be read from inotify file descriptor. */
    
    bool update = false;
    
    for (;;) {
        
        /* Read some events. */
        
        len = read(fd, buf, sizeof(buf));
        if (len == -1 && errno != EAGAIN) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        
        /* If the nonblocking read() found no events to read, then
           it returns -1 with errno set to EAGAIN. In that case,
           we exit the loop. */
        
        if (len <= 0)
            break;
        
        /* Loop over all events in the buffer. */
        
        for (char *ptr = buf; ptr < buf + len;
             ptr += sizeof(struct inotify_event) + event->len) {
            
            event = (const struct inotify_event *) ptr;
            
            /* Print event type. */
            
            if (event->mask & IN_OPEN)
                printf("IN_OPEN: ");
            if (event->mask & IN_CLOSE_NOWRITE)
                printf("IN_CLOSE_NOWRITE: ");
            if (event->mask & IN_CLOSE_WRITE)
                printf("IN_CLOSE_WRITE: ");
            if (event->mask & IN_MODIFY)
                printf("IN_CLOSE_WRITE: ");
            
            /* Print the name of the watched directory. */
            
            if (inotify_status_fd == event->wd) {
                update = true;
            } else if (inotify_capacity_fd == event->wd) {
                update = true;
            }
            /* Print the name of the file. */
            
            if (event->len)
                printf("%s", event->name);
            
            /* Print type of filesystem object. */
            
            if (event->mask & IN_ISDIR)
                printf(" [directory]\n");
            else
                printf(" [file]\n");
        }
    }

//    update_battery(app);
}

AppClient *
create_taskbar(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Set window startup settings
    Settings settings;
    settings.window_transparent = true;
    settings.decorations = false;
    settings.dock = true;
    settings.skip_taskbar = true;
//    settings.reserve_side = true;
//    settings.reserve_bottom = config->taskbar_height;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    ScreenInformation *primary_screen_info = nullptr;
    for (auto s: screens) {
        if (s->is_primary) primary_screen_info = s;
    }
    if (primary_screen_info == nullptr) {
        if (screens.empty()) {
            xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
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
    
    settings.x = primary_screen_info->x;
    settings.y = primary_screen_info->y + primary_screen_info->height_in_pixels - config->taskbar_height;
    settings.w = primary_screen_info->width_in_pixels;
    settings.h = config->taskbar_height;
    settings.sticky = true;
    settings.force_position = true;
    
    // Create the window
    
    AppClient *taskbar = client_new(app, settings, "taskbar");
    
    taskbar->creation_time = get_current_time_in_ms();
    times_painted = 0;
    taskbar->screen_information = primary_screen_info;
    
    taskbar->when_closed = when_taskbar_closed;
    taskbar->on_any_screen_change = taskbar_on_screen_size_change;
    
    global->unknown_icon_16 = accelerated_surface(app, taskbar, 16 * config->dpi, 16 * config->dpi);
    global->unknown_icon_24 = accelerated_surface(app, taskbar, 24 * config->dpi, 24 * config->dpi);
    global->unknown_icon_32 = accelerated_surface(app, taskbar, 32 * config->dpi, 32 * config->dpi);
    global->unknown_icon_64 = accelerated_surface(app, taskbar, 64 * config->dpi, 64 * config->dpi);
    paint_surface_with_image(
            global->unknown_icon_16, as_resource_path("unknown-16.svg"), 16 * config->dpi, nullptr);
    paint_surface_with_image(
            global->unknown_icon_24, as_resource_path("unknown-24.svg"), 24 * config->dpi, nullptr);
    paint_surface_with_image(
            global->unknown_icon_32, as_resource_path("unknown-32.svg"), 32 * config->dpi, nullptr);
    paint_surface_with_image(
            global->unknown_icon_64, as_resource_path("unknown-64.svg"), 64 * config->dpi, nullptr);
    
    app_create_custom_event_handler(app, INT_MAX, window_event_handler);
    app_timeout_create(app, taskbar, 500, screenshot_active_window, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
    
    // Lay it out
    fill_root(app, taskbar, taskbar->root);
    merge_order_with_taskbar();
    
    update_time(app, taskbar, nullptr, nullptr);
    update_active_window();
    
    load_pinned_icons();
    
    update_taskbar_volume_icon();
    
    uint32_t version = 5;
    xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, taskbar->window, get_cached_atom(app, "XdndAware"),
                        XCB_ATOM_ATOM, 32, 1, &version);
    
    /*
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd != -1) {
        inotify_status_fd = inotify_add_watch(inotify_fd, "/sys/class/power_supply/BAT0/status", IN_ALL_EVENTS);
        inotify_capacity_fd = inotify_add_watch(inotify_fd, "/sys/class/power_supply/BAT0/capacity", IN_ALL_EVENTS);
        poll_descriptor(app, inotify_fd, POLLIN, inotify_event_wakeup);
    }
    */
    return taskbar;
}

std::string
class_name(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_generic_error_t *error = NULL;
    xcb_get_property_cookie_t c = xcb_icccm_get_wm_class_unchecked(app->connection, window);
    xcb_get_property_reply_t *r = xcb_get_property_reply(app->connection, c, &error);
    
    if (error) {
        std::free(error);
    } else if (r) {
        xcb_icccm_get_wm_class_reply_t wm_class;
        if (xcb_icccm_get_wm_class_from_reply(&wm_class, r)) {
            std::string name;
            
            if (wm_class.class_name) {
                name = std::string(wm_class.class_name);
                if (name.empty()) {
                    name = std::string(wm_class.instance_name);
                }
            } else if (wm_class.instance_name) {
                name = std::string(wm_class.instance_name);
            } else {
            }
            xcb_icccm_get_wm_class_reply_wipe(&wm_class);
            
            std::for_each(name.begin(), name.end(), [](char &c) { c = std::tolower(c); });
            
            return name;
        } else {
            std::free(r);
        }
    }
    return "";
}

std::string get_reply_string(xcb_ewmh_get_utf8_strings_reply_t *reply) {
    std::string str;
    if (reply) {
        str = std::string(reply->strings, reply->strings_len);
        xcb_ewmh_get_utf8_strings_reply_wipe(reply);
    }
    return str;
}

std::string get_icon_name(xcb_window_t win) {
    xcb_ewmh_get_utf8_strings_reply_t utf8_reply{};
    if (xcb_ewmh_get_wm_icon_name_reply(&app->ewmh, xcb_ewmh_get_wm_icon_name(&app->ewmh, win), &utf8_reply, nullptr)) {
        return get_reply_string(&utf8_reply);
    }
    return "";
}

static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

std::string find_icon_string_from_window_properties(xcb_window_t window);

void add_window(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Exit the function if the window type is not something a dock should display
    xcb_get_property_cookie_t cookie = xcb_ewmh_get_wm_window_type_unchecked(&app->ewmh, window);
    xcb_ewmh_get_atoms_reply_t atoms_reply_data;
    if (xcb_ewmh_get_wm_window_type_reply(&app->ewmh, cookie, &atoms_reply_data, nullptr)) {
        for (unsigned short i = 0; i < atoms_reply_data.atoms_len; i++) {
            if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DESKTOP")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_POPUP_MENU")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_TOOLTIP")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_COMBO")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DND")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DOCK")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_NOTIFICATION")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            }
        }
        xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
    }
    
    bool is_ours = false;
    bool skip_taskbar = true;
    for (auto c: app->clients) {
        if (c->window == window) {
            is_ours = true;
            skip_taskbar = c->skip_taskbar;
        }
    }
    // on gnome, the Extension app ends up adding the taskbar to the taskbar. I have no idea how it's doing that
    // but the fix for now is just going to be to ignore every client that is ours. Eventually when we make a settings
    // app, we will have to add an exception for that window.
    if (is_ours && skip_taskbar)
        return;
    
    auto cookie_get_wm_desktop = xcb_ewmh_get_wm_desktop(&app->ewmh, window);
    uint32_t desktop = 0;
    xcb_ewmh_get_wm_desktop_from_reply(&desktop, NULL);
    
    std::vector<xcb_window_t> old_windows;
    AppClient *client = client_by_name(app, "taskbar");
    if (!client)
        return;
    auto *root = client->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    xcb_get_property_cookie_t prop_cookie = xcb_ewmh_get_wm_pid(&app->ewmh, window);
    uint32_t pid = -1;
    xcb_ewmh_get_wm_pid_reply(&app->ewmh, prop_cookie, &pid, NULL);
    std::string command_launched_by_line;
    if (pid != -1) {
        std::ifstream cmdline("/proc/" + std::to_string(pid) + "/cmdline");
        std::getline((cmdline), command_launched_by_line);
        
        size_t index = 0;
        while (true) {
            /* Locate the substring to replace. */
            index = command_launched_by_line.find('\000', index);
            if (index == std::string::npos)
                break;
            
            /* Make the replacement. */
            command_launched_by_line.replace(index, 1, " ");
            
            /* Advance index forward so the next iteration doesn't pick it up as well. */
            index += 1;
        }
    }
    rtrim(command_launched_by_line);
    
    std::string window_class_name = class_name(app, window);
    if (window_class_name.empty()) {
        window_class_name = command_launched_by_line;
        if (window_class_name.empty())
            window_class_name = std::to_string(window);
    } else {
        window_class_name = c3ic_fix_wm_class(window_class_name);
    }
    
    auto pinned = false;
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        if (data->class_name == window_class_name) {
            if (winbar_settings->labels) {
                if (data->windows_data_list.empty()) {
                    goto out;
                }
                pinned = data->pinned;
                continue;
            }
            out:
            if (!is_ours) {
                const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
                xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
                xcb_flush(app->connection);
            }
            
            data->attempting_to_launch_first_window = false;
            data->windows_data_list.push_back(new WindowsData(app, window));
            data->animation_zoom_locked = 0;
            data->animation_zoom_locked_time = get_current_time_in_ms();
            update_window_title_name(window);
            update_minimize_icon_positions();
            request_refresh(app, client);
            return;
        }
    }
    
    xcb_generic_error_t *err = nullptr;
    cookie = xcb_get_property(app->connection, 0, window, get_cached_atom(app, "_NET_WM_STATE"), XCB_ATOM_ATOM, 0,
                              BUFSIZ);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, &err);
    if (reply) {
        if (reply->type == XCB_ATOM_ATOM) {
            xcb_atom_t *state_atoms = (xcb_atom_t *) xcb_get_property_value(reply);
            for (unsigned int a = 0; a < reply->length; a++) {
                // TODO: on first launch xterm has this true????
                if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_SKIP_TASKBAR")) {
                    free(reply);
                    return;
                } else if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_SKIP_PAGER")) {
                    free(reply);
                    return;
                } else if (state_atoms[a] ==
                           get_cached_atom(app, "_NET_WM_STATE_DEMANDS_ATTENTION")) {
                }
            }
        }
        free(reply);
    }
    if (err) {
        free(err);
        err = nullptr;
    }
    
    if (!is_ours) {
        const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
        xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
        xcb_flush(app->connection);
    }
    Container *other_with_same_class = nullptr;
    bool has_launch_command = false;
    for (auto c: icons->children) {
        auto *d = (LaunchableButton *) c->user_data;
        if (d->class_name == window_class_name) {
            has_launch_command = d->has_launchable_info;
            other_with_same_class = c;
            break;
        }
    }
    
    Container *a = icons->child(client->bounds->h + 4 * config->dpi, FILL_SPACE);
    if (icons->alignment == container_alignment::ALIGN_RIGHT) {
        icons->children.erase(icons->children.begin() + (icons->children.size() - 1));
        icons->children.insert(icons->children.begin(), a);
    }
    a->when_drag_end_is_click = false;
    a->minimum_x_distance_to_move_before_drag_begins = 15;
    a->minimum_y_distance_to_move_before_drag_begins = 15 * config->dpi;
    a->when_mouse_enters_container = pinned_icon_mouse_enters;
    a->when_mouse_leaves_container = pinned_icon_mouse_leaves;
    a->when_clicked = pinned_icon_mouse_clicked;
    a->when_mouse_down = pinned_icon_mouse_down;
    a->when_mouse_up = pinned_icon_mouse_up;
    a->when_drag_end = pinned_icon_drag_end;
    a->when_drag_start = pinned_icon_drag_start;
    a->when_drag = pinned_icon_drag;
    LaunchableButton *data = new LaunchableButton();
    data->windows_data_list.push_back(new WindowsData(app, window));
    data->attempting_to_launch_first_window = false;
    data->class_name = window_class_name;
    data->icon_name = window_class_name;
    data->pinned = pinned;
    a->user_data = data;
    if (winbar_settings->labels) {
        for (auto c: icons->children) {
            if (c == a) continue;
            auto *d = (LaunchableButton *) c->user_data;
            if (d->class_name == window_class_name) {
                swap_icon(icons, a, c, false);
            }
        }
    }
    
    bool has_wm_based_launch_command = false;
    std::string wm_based_launcher;
    for (const auto &item: launchers) {
        if (!item->wmclass.empty() && item->wmclass == window_class_name && !item->exec.empty()) {
            has_wm_based_launch_command = true;
            wm_based_launcher = item->exec;
        }
    }
    
    if (other_with_same_class && has_launch_command) {
        data->has_launchable_info = true;
        data->command_launched_by = ((LaunchableButton *) other_with_same_class->user_data)->command_launched_by;
    } else if (has_wm_based_launch_command) {
        data->has_launchable_info = true;
        data->command_launched_by = wm_based_launcher;
    } else if (pid != -1) {
        data->has_launchable_info = true;
        data->command_launched_by = command_launched_by_line;
    }
    
    std::string path;
    std::string icon_name;
    
    if (winbar_settings->labels && other_with_same_class) {
        std::string icon = ((LaunchableButton *) other_with_same_class->user_data)->icon_name;
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(icon));
        search_icons(targets);
        pick_best(targets, icon_width(client));
        if (!targets.empty()) {
            path = targets[0].best_full_path;
            data->icon_name = icon;
        }
    }
    
    if (path.empty()) {
        for (const auto &item: launchers) {
            if (!item->wmclass.empty() && item->wmclass == window_class_name && !item->icon.empty()) {
                std::vector<IconTarget> targets;
                targets.emplace_back(IconTarget(item->icon));
                search_icons(targets);
                pick_best(targets, icon_width(client));
                path = targets[0].best_full_path;
                data->icon_name = item->icon;
            }
        }
    }
    
    if (path.empty()) {
        std::string icon = find_icon_string_from_window_properties(window);
        if (!icon.empty()) {
            std::vector<IconTarget> targets;
            targets.emplace_back(IconTarget(icon));
            search_icons(targets);
            pick_best(targets, icon_width(client));
            path = targets[0].best_full_path;
            data->icon_name = icon;
        }
    }
    
    if (path.empty()) {
        auto get_wm_icon_name_cookie = xcb_icccm_get_wm_icon_name(app->connection, window);
        xcb_icccm_get_text_property_reply_t prop;
        uint8_t success = xcb_icccm_get_wm_icon_name_reply(app->connection, get_wm_icon_name_cookie, &prop, nullptr);
        if (success) {
            icon_name = std::string(prop.name, prop.name_len);
            xcb_icccm_get_text_property_reply_wipe(&prop);
        } else {
            icon_name = get_icon_name(window);
        }
        if (!icon_name.empty()) {
            std::vector<IconTarget> targets;
            targets.emplace_back(IconTarget(icon_name));
            search_icons(targets);
            pick_best(targets, icon_width(client));
            path = targets[0].best_full_path;
            data->icon_name = icon_name;
        }
    }
    if (path.empty()) {
        xcb_generic_error_t *error = NULL;
        xcb_get_property_cookie_t c = xcb_icccm_get_text_property_unchecked(app->connection, window,
                                                                            get_cached_atom(app,
                                                                                            "_GTK_APPLICATION_ID"));
        xcb_icccm_get_text_property_reply_t props;
        if (xcb_icccm_get_text_property_reply(app->connection, c, &props, nullptr)) {
            data->icon_name = std::string(props.name, props.name_len);
            std::vector<IconTarget> targets;
            targets.emplace_back(IconTarget(data->icon_name));
            search_icons(targets);
            pick_best(targets, icon_width(client));
            path = targets[0].best_full_path;
            xcb_icccm_get_text_property_reply_wipe(&props);
        }
    }
    
    if (path.empty()) {
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(window_class_name));
        search_icons(targets);
        pick_best(targets, icon_width(client));
        path = targets[0].best_full_path;
        data->icon_name = window_class_name;
    }
    
    if (!path.empty()) {
        load_icon_full_path(app, client, &data->surface, path, icon_width(client));
    } else {
        xcb_generic_error_t *error;
        xcb_get_property_cookie_t c = xcb_ewmh_get_wm_icon(&app->ewmh, window);
        
        xcb_ewmh_get_wm_icon_reply_t wm_icon;
        memset(&wm_icon, 0, sizeof(xcb_ewmh_get_wm_icon_reply_t));
        xcb_ewmh_get_wm_icon_reply(&app->ewmh, c, &wm_icon, &error);
        
        if (error) {
            std::free(error);
            data->surface = accelerated_surface(app, client, icon_width(client), icon_width(client));
            paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                     icon_width(client), nullptr);
        } else {
            if (0 < xcb_ewmh_get_wm_icon_length(&wm_icon)) {
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t *icon_data = NULL;
                
                xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&wm_icon);
                for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
                    if (iter.width > width) {
                        width = iter.width;
                        height = iter.height;
                        icon_data = iter.data;
                        if (width >= 24) {
                            break;
                        }
                    }
                }
                auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
                auto surface = cairo_image_surface_create_for_data(
                        (unsigned char *) icon_data, CAIRO_FORMAT_ARGB32, width, height, stride);
                
                cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
                cairo_pattern_set_filter(pattern, CAIRO_FILTER_BEST);
                
                data->surface = accelerated_surface(app, client, icon_width(client), icon_width(client));
                cairo_t *cr = cairo_create(data->surface);
                
                cairo_save(cr);
                double taskbar_icon_size = icon_width(client);
                cairo_scale(cr, taskbar_icon_size / (width), taskbar_icon_size / (width));
                cairo_set_source(cr, pattern);
                cairo_paint(cr);
                cairo_restore(cr);
                
                cairo_destroy(cr);
                xcb_ewmh_get_wm_icon_reply_wipe(&wm_icon);
            } else {
                data->surface = accelerated_surface(app, client, icon_width(client), icon_width(client));
                paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                         icon_width(client), nullptr);
            }
        }
    }
    
    update_window_title_name(window);
    update_minimize_icon_positions();
    update_pinned_items_file(false);
 }

void remove_window(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    std::vector<xcb_window_t> old_windows;
    AppClient *entity = client_by_name(app, "taskbar");
    if (!entity)
        return;
    auto *root = entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    for (int j = 0; j < icons->children.size(); j++) {
        Container *container = icons->children[j];
        LaunchableButton *data = (LaunchableButton *) container->user_data;
        for (int i = 0; i < data->windows_data_list.size(); i++) {
            if (data->windows_data_list[i]->id == window) {
                if (auto windows_selector_client = client_by_name(app, "windows_selector")) {
                    if (auto windows_selector_container = container_by_name(std::to_string(window),
                                                                            windows_selector_client->root)) {
                        auto sub_width = windows_selector_container->real_bounds.w;
                        
                        auto parent = windows_selector_container->parent;
                        for (int i = 0; i < parent->children.size(); i++) {
                            if (parent->children[i] == windows_selector_container) {
                                parent->children.erase(parent->children.begin() + i);
                                break;
                            }
                        }
                        
                        delete windows_selector_container;
                        
                        if (parent->children.empty()) {
                            client_close(app, windows_selector_client);
                            app->grab_window = -1;
                        } else {
                            if (!winbar_settings->thumbnails) {
                                auto x = windows_selector_client->bounds->x;
                                auto y = windows_selector_client->bounds->y;
                                auto w = windows_selector_client->bounds->w;
                                auto h = windows_selector_client->bounds->h;
                                h = option_height * parent->children.size();
                                y += option_height;
                                handle_configure_notify(app, windows_selector_client, x, y, w, h);
                                client_set_position_and_size(app, windows_selector_client, x, y, w, h);
                            } else {
                                int width = windows_selector_client->root->real_bounds.w - sub_width;
                                
                                double x = container->real_bounds.x - width / 2 + container->real_bounds.w / 2;
                                if (x < 0) {
                                    x = 0;
                                }
                                double y = app->bounds.h - option_height - config->taskbar_height;
                                double h = option_height;
                                
                                handle_configure_notify(app, windows_selector_client, x, y, width, h);
                                client_set_position_and_size(app, windows_selector_client, x, y, width, h);
                            }
                         }
                    }
                }
                data->animation_bounce_amount = 0;
                data->animation_bounce_direction = 0;
                client_create_animation(app, entity, &data->animation_bounce_amount, data->lifetime, 0, 0, nullptr, 0);
                
                delete data->windows_data_list[i];
                data->windows_data_list.erase(data->windows_data_list.begin() + i);
                
                if (data->windows_data_list.empty()) {
                    int count = 0;
                    for (auto icon: icons->children) {
                        auto icon_data = (LaunchableButton *) icon->user_data;
                        if (icon_data->class_name == data->class_name)
                            count++;
                    }
                    if (active_container == container) {
                        client_create_animation(app, entity, &data->active_amount, data->lifetime, 0, 80, nullptr, 0);
                        active_container = nullptr;
                    }
                    if (!data->pinned || count > 1) {
                        icons->children.erase(icons->children.begin() + j);
                        delete container;
                    }
                }
                break;
            }
        }
    }
    
    // TODO: mark handler as remove at end of loop
    //    for (int i = 0; i < app->handlers.size(); i++) {
    //        if (app->handlers[i]->target_window == window) {
    //            delete (app->handlers[i]);
    //            app->handlers.erase(app->handlers.begin() + i);
    //        }
    //    }
    
    request_refresh(app, entity);
}

void stacking_order_changed(xcb_window_t *all_windows, int windows_count) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    std::vector<xcb_window_t> new_windows;
    for (int i = 0; i < windows_count; i++) {
        if (all_windows[i] != 0) {
            new_windows.push_back(all_windows[i]);
        }
    }
    
    std::vector<xcb_window_t> old_windows;
    AppClient *entity = client_by_name(app, "taskbar");
    if (!entity)
        return;
    auto *root = entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        for (auto window_data: data->windows_data_list) {
            old_windows.emplace_back(window_data->id);
        }
    }
    
    for (auto new_window: new_windows) {
        bool found = false;
        for (auto old_window: old_windows)
            if (old_window == new_window)
                found = true;
        if (!found) {
            add_window(app, new_window);
        }
    }
    
    for (int i = 0; i < old_windows.size(); i++) {
        xcb_window_t old_window = old_windows[i];
        bool found = false;
        for (auto new_window: new_windows)
            if (old_window == new_window)
                found = true;
        if (!found) {
            remove_window(app, old_window);
        }
    }
}

void remove_non_pinned_icons() {
    AppClient *client_entity = client_by_name(app, "taskbar");
    if (!valid_client(app, client_entity))
        return;
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    icons->children.erase(std::remove_if(icons->children.begin(),
                                         icons->children.end(),
                                         [](Container *icon) {
                                             auto *data =
                                                     static_cast<LaunchableButton *>(icon->user_data);
                                             bool del = data->windows_data_list.empty() && !data->pinned;
                                             if (del) {
                                                 delete data;
                                             }
        
                                             return del;
                                         }),
                          icons->children.end());
    
    update_pinned_items_file(false);
    icons_align(client_entity, icons, false);
}

#include <INIReader.h>
#include <sys/stat.h>
#include <xcb/xcb_aux.h>

void update_pinned_items_timeout(App *app, AppClient *client, Timeout *timeout, void *user_data) {
    pinned_timeout = nullptr;
    const char *home = getenv("HOME");
    std::string itemsPath(home);
    itemsPath += "/.config/";
    
    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }
    
    itemsPath += "/winbar/";
    
    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }
    
    itemsPath += "items.ini";
    std::string itemsPathTemp = itemsPath;
    itemsPathTemp += ".temp";
    
    std::ofstream itemsFile(itemsPathTemp);
    
    if (!itemsFile.is_open())
        return;
    
    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    update_minimize_icon_positions();
    
    std::map<std::string, bool> seen_before;
    int i = 0;
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        
        if (!data)
            continue;
        if (!data->pinned)
            continue;
        if (seen_before.find(data->class_name) != seen_before.end())
            continue;
        seen_before[data->class_name] = true;
        
        itemsFile << "[PinnedIcon" << i++ << "]" << std::endl;
        
        itemsFile << "#The class_name is a property that windows set on themselves so that they "
                     "can be stacked with windows of the same kind as them. If when you click this "
                     "pinned icon button, it launches a window that creates an icon button that "
                     "doesn't stack with this one then the this wm_class is wrong and you're going "
                     "to have to fix it by running xprop in your console and clicking the window "
                     "that opened to find the real WM_CLASS that should be set."
                  << std::endl;
        itemsFile << "class_name=" << data->class_name << std::endl;
        
        itemsFile << "#If you want to change the icon, modify this."
                  << std::endl;
        itemsFile << "user_icon_name=" << data->user_icon_name << std::endl;
        
        itemsFile << "#If you want to change the icon use \"user_icon_name\" instead since this one can be overriden."
                  << std::endl;
        itemsFile << "icon_name=" << data->icon_name << std::endl;
        
        itemsFile << "#The command that is run when the icon is clicked" << std::endl;
        itemsFile << "command=" << data->command_launched_by << std::endl
                  << std::endl;
        itemsFile << std::endl;
    }
    
    itemsFile.close();
    
    rename(itemsPathTemp.data(), itemsPath.data());
}

/**
 * Save the pinned items to a file on disk so we can load them on the next session
 */
void update_pinned_items_file(bool force_update) {
    if (auto client = client_by_name(app, "taskbar")) {
        if (force_update) {
            update_pinned_items_timeout(app, client, nullptr, nullptr);
        } else if (pinned_timeout == nullptr) {
            pinned_timeout = app_timeout_create(app, client, 1000, update_pinned_items_timeout, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
        } else {
            app_timeout_replace(app, client, pinned_timeout, 1000, update_pinned_items_timeout, nullptr);
        }
    }
}

static void
load_pinned_icons() {
    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    const char *home = getenv("HOME");
    std::string itemsPath(home);
    itemsPath += "/.config/";
    
    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }
    
    itemsPath += "/winbar/";
    
    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }
    
    itemsPath += "items.ini";
    
    // READ INI FILE
    INIReader itemFile(itemsPath);
    if (itemFile.ParseError() != 0)
        return;
    
    std::vector<IconTarget> targets;
    int i = 0;
    for (const std::string &section_title: itemFile.Sections()) {
        auto user_icon_name = itemFile.Get(section_title, "user_icon_name", "");
        auto icon_name = itemFile.Get(section_title, "icon_name", "");
        auto class_name = itemFile.Get(section_title, "class_name", "");
        
        if (!user_icon_name.empty()) {
            int *mem_i = new int;
            *mem_i = i;
            targets.emplace_back(IconTarget(user_icon_name, mem_i));
        }
        if (!icon_name.empty()) {
            int *mem_i = new int;
            *mem_i = i;
            targets.emplace_back(IconTarget(icon_name, mem_i));
        }
        if (!class_name.empty()) {
            int *mem_i = new int;
            *mem_i = i;
            targets.emplace_back(IconTarget(class_name, mem_i));
        }
        i++;
    }
    search_icons(targets);
    pick_best(targets, icon_width(client_entity));
    
    i = 0;
    for (const std::string &section_title: itemFile.Sections()) {
        auto *child = new Container();
        child->parent = icons;
        child->wanted_bounds.h = FILL_SPACE;
        child->wanted_bounds.w = client_entity->bounds->h + 4 * config->dpi;
        
        child->when_drag_end_is_click = false;
        child->minimum_x_distance_to_move_before_drag_begins = 15;
        child->minimum_y_distance_to_move_before_drag_begins = 15 * config->dpi;
        child->when_mouse_enters_container = pinned_icon_mouse_enters;
        child->when_mouse_leaves_container = pinned_icon_mouse_leaves;
        child->when_clicked = pinned_icon_mouse_clicked;
        child->when_mouse_down = pinned_icon_mouse_down;
        child->when_mouse_up = pinned_icon_mouse_up;
        child->when_drag_end = pinned_icon_drag_end;
        child->when_drag_start = pinned_icon_drag_start;
        child->when_drag = pinned_icon_drag;
        
        auto *data = new LaunchableButton;
        data->class_name = itemFile.Get(section_title, "class_name", "");
        data->icon_name = itemFile.Get(section_title, "icon_name", "");
        data->user_icon_name = itemFile.Get(section_title, "user_icon_name", "");
        data->pinned = true;
        auto command = itemFile.Get(section_title, "command", "NONE");
        if (command != "NONE") {
            data->has_launchable_info = true;
            data->command_launched_by = command;
        }
        
        std::string path;
        for (int x = 0; x < targets.size(); x++) {
            if (*((int *) targets[x].user_data) == i) {
                path = targets[x].best_full_path;
                break;
            }
        }
        
        if (!path.empty()) {
            load_icon_full_path(app, client_entity, &data->surface, path, icon_width(client_entity));
        } else {
            data->surface = accelerated_surface(app, client_entity, icon_width(client_entity), icon_width(client_entity));
            char *string = getenv("HOME");
            std::string home(string);
            home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
            bool b = paint_surface_with_image(data->surface, home, icon_width(client_entity), nullptr);
            if (!b) {
                paint_surface_with_image(
                        data->surface, as_resource_path("unknown-24.svg"), icon_width(client_entity), nullptr);
            }
        }
        
        child->user_data = data;
        
        icons->children.push_back(child);
        update_wanted_width(client_entity, child);
        
        i++;
    }
    
    for (int x = 0; x < targets.size(); x++)
        delete ((int *) targets[x].user_data);
}

static void
late_classes_update(App *app, AppClient *client, Timeout *timeout, void *data) {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Late WM_CLASS Thread");
#endif
    if (timeout)
        timeout->keep_running = true;
    if (!client || !app)
        return;
    auto *root = client->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    
    for (auto icon: icons->children) {
        auto data = static_cast<LaunchableButton *>(icon->user_data);
        
        // since when windows don't have real classes on them we set their names to their class.
        // they should only be stacked by one
        if (data->windows_data_list.size() == 1) {
            xcb_window_t id = data->windows_data_list[0]->id;
            auto name = class_name(app, id);
            if (name.empty()) {
                name = data->command_launched_by;
                if (name.empty()) name = std::to_string(id);
            } else {
                name = c3ic_fix_wm_class(name);
            }
            
            if (name != data->class_name) {
                remove_window(app, id);
                add_window(app, id);
                break;
            }
        }
    }
}

void update_taskbar_volume_icon() {
    request_refresh(app, client_by_name(app, "taskbar"));
}

void set_textarea_active() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("field_search", client->root))
            container->exists = true;
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            container->parent->active = true;
            container->parent->active = true;
        }
        client_layout(app, client);
        request_refresh(client->app, client);
    }
    xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
    xcb_flush(app->connection);
    xcb_aux_sync(app->connection);
}

void set_textarea_inactive() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("field_search", client->root))
            for (auto a: winbar_settings->taskbar_order)
                if (a.name == "Search Field")
                    container->exists = a.on;
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            delete text_data->state;
            text_data->state = new TextState;
            container->parent->active = false;
        }
        client_layout(app, client);
        request_refresh(client->app, client);
    }
}

void update_pinned_items_icon() {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon: icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    if (data->surface) {
                        cairo_surface_destroy(data->surface);
                        data->surface = nullptr;
                    }
                    
                    std::string path;
                    std::vector<IconTarget> targets;
                    targets.emplace_back(IconTarget(data->icon_name));
                    targets.emplace_back(IconTarget(data->class_name));
                    search_icons(targets);
                    pick_best(targets, icon_width(client));
                    path = targets[0].best_full_path;
                    if (!data->icon_name.empty()) {
                        path = targets[0].best_full_path;
                    }
                    if (path.empty()) {
                        path = targets[1].best_full_path;
                    }
                    if (!path.empty()) {
                        load_icon_full_path(app, client, &data->surface, path, icon_width(client));
                    } else {
                        data->surface = accelerated_surface(app, client, icon_width(client), icon_width(client));
                        char *string = getenv("HOME");
                        std::string home(string);
                        home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
                        bool b = paint_surface_with_image(data->surface, home, icon_width(client), nullptr);
                        if (!b) {
                            paint_surface_with_image(
                                    data->surface, as_resource_path("unknown-24.svg"), icon_width(client), nullptr);
                        }
                    }
                }
            }
        }
    }
}

WindowsData::WindowsData(App *app, xcb_window_t window) {
    option_width = 217 * 1.2 * config->dpi;
    option_height = 144 * 1.2 * config->dpi;
    
    id = window;
    
    auto cookie = xcb_get_property(app->connection, 0, id, get_cached_atom(app, "_GTK_FRAME_EXTENTS"),
                                   XCB_ATOM_CARDINAL, 0, 4);
    auto reply = xcb_get_property_reply(app->connection, cookie, nullptr);
    
    if (reply) {
        int length = xcb_get_property_value_length(reply);
        if (length != 0) {
            auto gtkFrameExtents = static_cast<uint32_t *>(xcb_get_property_value(reply));
            gtk_left_margin = (int) gtkFrameExtents[0];
            gtk_right_margin = (int) gtkFrameExtents[1];
            gtk_top_margin = (int) gtkFrameExtents[2];
            gtk_bottom_margin = (int) gtkFrameExtents[3];
        }
        free(reply);
    }
    
    const xcb_get_window_attributes_cookie_t &attributesCookie = xcb_get_window_attributes(
            app->connection, window);
    xcb_get_window_attributes_reply_t *attributes = xcb_get_window_attributes_reply(app->connection,
                                                                                    attributesCookie,
                                                                                    nullptr);
    if (attributes) {
        mapped = attributes->map_state == XCB_MAP_STATE_VIEWABLE;
        // TODO: screen should be found using the window somehow I think
        xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
        xcb_visualtype_t *visual = xcb_aux_find_visual_by_id(screen, attributes->visual);
        
        xcb_get_geometry_cookie_t geomCookie = xcb_get_geometry(app->connection,
                                                                window);  // window is a xcb_drawable_t
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(app->connection, geomCookie, NULL);
        
        if (geom) {
            window_surface = cairo_xcb_surface_create(app->connection,
                                                      window,
                                                      visual,
                                                      (width = geom->width),
                                                      (height = geom->height));
            
            raw_thumbnail_surface = accelerated_surface(app, client_by_name(app, "taskbar"),
                                                        width, height);
            raw_thumbnail_cr = cairo_create(raw_thumbnail_surface);
            scaled_thumbnail_surface = accelerated_surface(app, client_by_name(app, "taskbar"),
                                                           option_width,
                                                           option_height);
            scaled_thumbnail_cr = cairo_create(scaled_thumbnail_surface);
            take_screenshot();
            free(geom);
        }
        free(attributes);
    }
}

WindowsData::~WindowsData() {
    if (window_surface) {
        cairo_surface_destroy(window_surface);
        cairo_surface_destroy(raw_thumbnail_surface);
        cairo_destroy(raw_thumbnail_cr);
        cairo_surface_destroy(scaled_thumbnail_surface);
        cairo_destroy(scaled_thumbnail_cr);
    }
}

void WindowsData::take_screenshot() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!winbar_settings->thumbnails || !mapped || !window_surface || !raw_thumbnail_cr)
        return;
    for (auto c: app->clients)
        if (this->id == c->window)
            if (c->skip_taskbar)
                return;
    
    if (gtk_left_margin == 0 && gtk_right_margin == 0 && gtk_top_margin == 0 && gtk_bottom_margin == 0) {
        cairo_set_source_surface(raw_thumbnail_cr, window_surface, 0, 0);
        cairo_paint(raw_thumbnail_cr);
    } else {
        cairo_save(raw_thumbnail_cr);
        cairo_set_source_surface(raw_thumbnail_cr, window_surface, 0, 0);
        cairo_rectangle(raw_thumbnail_cr, gtk_left_margin, gtk_top_margin,
                        width - (gtk_right_margin + gtk_left_margin), height - (gtk_bottom_margin + gtk_top_margin));
        cairo_clip(raw_thumbnail_cr);
        cairo_paint(raw_thumbnail_cr);
        cairo_restore(raw_thumbnail_cr);
    }
    
    cairo_surface_flush(window_surface);
    cairo_surface_mark_dirty(window_surface);
}

void WindowsData::rescale(double scale_w, double scale_h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!winbar_settings->thumbnails || !window_surface || !raw_thumbnail_cr)
        return;
    last_rescale_timestamp = get_current_time_in_ms();
    
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(raw_thumbnail_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);
    
    cairo_save(scaled_thumbnail_cr);
    cairo_scale(scaled_thumbnail_cr, scale_w, scale_h);
    cairo_set_source(scaled_thumbnail_cr, pattern);
    cairo_paint(scaled_thumbnail_cr);
    cairo_restore(scaled_thumbnail_cr);
}

void taskbar_launch_index(int index) {
    if (auto c = client_by_name(app, "taskbar")) {
        if (auto icons = container_by_name("icons", c->root)) {
            for (int i = 0; i < icons->children.size(); i++) {
                if (i == index) {
                    auto container = icons->children[i];
                    auto data = (LaunchableButton *) container->user_data;
                    if (data) {
                        if (data->windows_data_list.empty()) {
                            if (!data->command_launched_by.empty()) {
                                start_zoom_animation(c, container);
                                launch_command(data->command_launched_by);
                            }
                        } else {
                            xcb_ewmh_request_change_active_window(&app->ewmh,
                                                                  app->screen_number,
                                                                  data->windows_data_list[0]->id,
                                                                  XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                                  XCB_CURRENT_TIME,
                                                                  XCB_NONE);
                            xcb_flush(app->connection);
                        }
                    }
                    break;
                }
            }
        }
    }
}

xcb_window_t get_active_window() {
    return active_window;
}

std::string find_icon_string_from_window_properties(xcb_window_t window) {
    std::string wm_name;
    std::string net_wm_name;
    std::string icon_name;
    std::string wm_class;
    std::string gtk_application_id;
    std::string kde_application_id;
    xcb_icccm_get_text_property_reply_t reply;
    xcb_ewmh_get_utf8_strings_reply_t data;
    
    const xcb_get_property_cookie_t &name_cookie = xcb_icccm_get_wm_icon_name(app->connection, window);
    const xcb_get_property_cookie_t &net_name_cookie = xcb_ewmh_get_wm_name(&app->ewmh, window);
    const xcb_get_property_cookie_t &icon_cookie = xcb_icccm_get_wm_icon_name(app->connection, window);
    const xcb_get_property_cookie_t &wm_class_cookie = xcb_icccm_get_wm_class(app->connection, window);
    xcb_get_property_cookie_t gtk_coookie = xcb_icccm_get_text_property_unchecked(app->connection, window,
                                                                                  get_cached_atom(app,
                                                                                                  "_GTK_APPLICATION_ID"));
    xcb_get_property_cookie_t kde_cookie = xcb_icccm_get_text_property_unchecked(app->connection, window,
                                                                                 get_cached_atom(app,
                                                                                                 "_KDE_NET_WM_DESKTOP_FILE"));
    
    // _GTK_APPLICATION_ID
    if (xcb_icccm_get_text_property_reply(app->connection, gtk_coookie, &reply, nullptr)) {
        gtk_application_id = std::string(reply.name, reply.name_len);
        xcb_icccm_get_text_property_reply_wipe(&reply);
    }
    
    // _KDE_NET_WM_DESKTOP_FILE
    if (xcb_icccm_get_text_property_reply(app->connection, kde_cookie, &reply, nullptr)) {
        kde_application_id = std::string(reply.name, reply.name_len);
        xcb_icccm_get_text_property_reply_wipe(&reply);
    }
    
    uint8_t success = xcb_icccm_get_wm_name_reply(app->connection, net_name_cookie, &reply, nullptr);
    if (success) {
        net_wm_name = std::string(reply.name, reply.name_len);
        xcb_icccm_get_text_property_reply_wipe(&reply);
    }
    
    // try to use properties to find matching desktop file, and if the icon specified, has options, or is a '/' return that
    success = xcb_ewmh_get_wm_name_reply(&app->ewmh, name_cookie, &data, nullptr);
    if (success) {
        wm_name = strndup(data.strings, data.strings_len);
        xcb_ewmh_get_utf8_strings_reply_wipe(&data);
    }
    
    success = xcb_icccm_get_text_property_reply(app->connection, wm_class_cookie, &reply, nullptr);
    if (success) {
        wm_class = strndup(reply.name, reply.name_len);
        xcb_icccm_get_text_property_reply_wipe(&reply);
    }
    
    
    // then check each property individually to see if it has options
    
    // then see if we have a set icon string in text file, and if it has options, return that
    
    // then check if we have a raw icon saved with the wm_class name, and return that path if so
    for (auto &l: launchers) {
        if (!net_wm_name.empty() && !l->name.empty() && l->name == net_wm_name) {
            printf("%s\n\n", l->icon.c_str());
            return l->icon;
        }
    }

//    printf("WM_NAME: %s, NET_WM_NAME: %s, ICON_NAME: %s, WM_CLASS: %s, GTK_APPLICATION_ID: %s, KDE_APPLICATION_ID: %s\n",
//           wm_name.c_str(), net_wm_name.c_str(), icon_name.c_str(), wm_class.c_str(), gtk_application_id.c_str(),
//           kde_application_id.c_str());
    
    return "";
}

TextAreaSettings::TextAreaSettings(float scale) : ScrollPaneSettings(scale) {}

void battery_display_device_state_changed() {
    auto client = client_by_name(app, "taskbar");
    auto battery = container_by_name("battery", client->root);
    if (battery && battery->user_data) {
        update_battery_status_timeout(app, client, nullptr, battery->user_data);
    }
}

void label_change(AppClient *taskbar) {
    if (!taskbar) return;
    
    request_refresh(app, taskbar);
    
    std::vector<xcb_window_t> windows;
    auto *icons = container_by_name("icons", taskbar->root);
    if (!icons) return;
    
    for (auto icon: icons->children) {
        auto data = (LaunchableButton *) icon->user_data;
        for (const auto &item: data->windows_data_list) {
            windows.push_back(item->id);
        }
    }
    
    for (auto w: windows) {
        remove_window(app, w);
    }
    
    for (auto w: windows) {
        add_window(app, w);
    }
    
    icons_align(taskbar, icons, false);
}
