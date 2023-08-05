
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
#include "utility.h"
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
#include "xbacklight.h"

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
#include <functional>

#define WIN7 false

static Container *active_container = nullptr;
static xcb_window_t active_window = 0;
static xcb_window_t backup_active_window = 0;

static std::string time_text("N/A");

static xcb_window_t popup_window_open = -1;

static int max_resize_attempts = 10;
static int resize_attempts = 0;

static void
paint_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_button(AppClient *client, cairo_t *cr, Container *container);

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
paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_taskbar_background));
    cairo_fill(cr);
    
    if (WIN7) {
        set_rect(cr, Bounds(container->real_bounds.x, container->real_bounds.y, container->real_bounds.w,
                            1 * std::floor(config->dpi)));
        set_argb(cr, correct_opaqueness(client, config->color_taskbar_search_bar_default_border));
        cairo_fill(cr);
    }
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
    
    auto e = getEasingFunction(easing_functions::EaseOutQuad);
    double time = 0;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            if (data->previous_state != 2) {
                data->previous_state = 2;
                client_create_animation(app, client, &data->color.r, 0, time, e, pressed_color.r);
                client_create_animation(app, client, &data->color.g, 0, time, e, pressed_color.g);
                client_create_animation(app, client, &data->color.b, 0, time, e, pressed_color.b);
                client_create_animation(app, client, &data->color.a, 0, time, e, pressed_color.a);
            }
        } else if (data->previous_state != 1) {
            data->previous_state = 1;
            client_create_animation(app, client, &data->color.r, 0, time, e, hovered_color.r);
            client_create_animation(app, client, &data->color.g, 0, time, e, hovered_color.g);
            client_create_animation(app, client, &data->color.b, 0, time, e, hovered_color.b);
            client_create_animation(app, client, &data->color.a, 0, time, e, hovered_color.a);
        }
    } else if (data->previous_state != 0) {
        time = 100;
        data->previous_state = 0;
        e = getEasingFunction(easing_functions::EaseInCirc);
        client_create_animation(app, client, &data->color.r, 0, time, e, default_color.r);
        client_create_animation(app, client, &data->color.g, 0, time, e, default_color.g);
        client_create_animation(app, client, &data->color.b, 0, time, e, default_color.b);
        client_create_animation(app, client, &data->color.a, 0, time, e, default_color.a);
    }
    
    set_argb(cr, data->color);
    
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y,
                    container->real_bounds.w,
                    container->real_bounds.h);
    cairo_fill(cr);
}

static void
paint_super(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    IconButton *data = (IconButton *) container->user_data;
    
    paint_hoverable_button_background(client, cr, container);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
    
    int val = 100;
    bool mute_state = false;
    for (auto c: audio_clients) {
        if (c->is_master_volume()) {
            val = round(c->get_volume() * 100);
            mute_state = c->is_muted();
            break;
        }
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, "\uEBC5", strlen("\uE83F"));
    pango_layout_get_pixel_size(layout, &width, &height);
    
    if (!mute_state) {
        ArgbColor volume_bars_color = ArgbColor(.4, .4, .4, 1);
        set_argb(cr, volume_bars_color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
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
    
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_workspace(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    set_argb(cr, config->color_taskbar_button_icons);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        pango_layout_set_text(layout, "\uEB91", strlen("\uE83F"));
    } else {
        pango_layout_set_text(layout, "\uE7C4", strlen("\uE83F"));
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
    float height = std::round(2 * config->dpi);
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

static void
paint_icon_surface(AppClient *client, cairo_t *cr, Container *container) {
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    if (data->surface) {
        cairo_save(cr);
        double scale_afterwards = .81;
        double w = cairo_image_surface_get_width(data->surface);
        auto scale_amount = 1 - (data->animation_zoom_amount * (1 - scale_afterwards));
        double current_w = w * scale_amount;
        double xpos = container->real_bounds.x + container->real_bounds.w / 2 -
                      cairo_image_surface_get_width(data->surface) / 2;
        double ypos = container->real_bounds.y + container->real_bounds.h / 2 -
                      cairo_image_surface_get_width(data->surface) / 2;
        xpos += (w - current_w) / 2;
        ypos += (w - current_w) / 2;
    
        cairo_scale(cr, scale_amount, scale_amount);
        double xpostrans = (xpos * (1 - scale_amount) / scale_amount);
        double ypostrans = (ypos * (1 - scale_amount) / scale_amount);
        cairo_translate(cr, xpostrans, ypostrans);
        // Assumes the size of the icon to be 24x24 and tries to draw it centered
        if (data->animation_bounce_amount == 1 || data->windows_data_list.empty()) {
            data->animation_bounce_amount = 0;
        }
        auto easeBack = getEasingFunction(EaseOutBack);
        auto easeIn = getEasingFunction(EaseInQuad);
        if (data->animation_bounce_direction == 1) {
            easeIn = getEasingFunction(EaseInSine);
        }
        double bounce_amount = easeBack(easeIn(data->animation_bounce_amount));
        if (bounce_amount > .5)
            bounce_amount = 1 - bounce_amount;
        if (data->animation_bounce_direction == 0) {
            bounce_amount = bounce_amount;
        } else if (data->animation_bounce_direction == 1) {
            bounce_amount = -bounce_amount;
        }
        double off = (((config->taskbar_height - w) - (2 * config->dpi)) / 2) * (bounce_amount);
        cairo_set_source_surface(cr, data->surface, xpos, ypos + off);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

static void
rounded_rect(cairo_t *cr, double corner_radius, double x, double y, double width, double height) {
    double radius = corner_radius;
    double degrees = M_PI / 180.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}


static void animate_color_change(App *app, AppClient *client, ArgbColor *current_color, ArgbColor target_color) {
    if (current_color->a == 0) {
        // If the target_color is going to be lerping from invisible, then we should just immediately set chroma and just let the opacity animate
        current_color->r = target_color.r;
        current_color->g = target_color.g;
        current_color->b = target_color.b;
    } else if (target_color.a != 0) { // If the target color is invisible, then we shouldn't change our rgb values
        client_create_animation(app, client, &current_color->r, 0, 16.67 * 5.8, nullptr, target_color.r);
        client_create_animation(app, client, &current_color->g, 0, 16.67 * 5.8, nullptr, target_color.g);
        client_create_animation(app, client, &current_color->b, 0, 16.67 * 5.8, nullptr, target_color.b);
    }
    client_create_animation(app, client, &current_color->a, 0, 16.67 * 5.8, nullptr, target_color.a);
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
paint_icon_background_win7(AppClient *client, cairo_t *cr, Container *container) {
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
paint_icon_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (WIN7) {
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
    
    if (screen_has_transparency(app)) {
        background.a = config->color_taskbar_background.a;
    }
    
    // The following colors are used for the background pane
    ArgbColor color_background_pane_hovered_left = darken(background, 15);
    ArgbColor color_background_pane_hovered_middle = darken(background, 22);
    ArgbColor color_background_pane_hovered_right = darken(background, 17);
    
    ArgbColor color_background_pane_pressed_left = darken(background, 20);
    ArgbColor color_background_pane_pressed_middle = darken(background, 27);
    ArgbColor color_background_pane_pressed_right = darken(background, 22);
    
    // The following colors are used for the foreground pane
    ArgbColor color_foreground_pane_default_left = darken(background, 10);
    ArgbColor color_foreground_pane_default_middle = darken(background, 17);
    ArgbColor color_foreground_pane_default_right = darken(background, 12);
    
    ArgbColor color_foreground_pane_hovered_left = darken(background, 0);
    ArgbColor color_foreground_pane_hovered_middle = darken(background, 7);
    ArgbColor color_foreground_pane_hovered_right = darken(background, 2);
    
    ArgbColor color_foreground_pane_pressed_left = darken(background, 0 + 2);
    ArgbColor color_foreground_pane_pressed_middle = darken(background, 7 + 2);
    ArgbColor color_foreground_pane_pressed_right = darken(background, 2 + 2);
    
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

// TODO order is not correct
static void
paint_all_icons(AppClient *client_entity, cairo_t *cr, Container *container) {
    std::vector<int> render_order;
    for (int i = 0; i < container->children.size(); i++) {
        render_order.push_back(i);
    }
    std::sort(render_order.begin(), render_order.end(), [container](int a, int b) -> bool {
        return container->children[a]->z_index < container->children[b]->z_index;
    });
    
    for (auto index: render_order) {
        paint_icon_background(client_entity, cr, container->children[index]);
    }
    for (auto index: render_order) {
        paint_icon_surface(client_entity, cr, container->children[index]);
    }
}

static void
pinned_icon_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    possibly_open(app, container, data);
    client_create_animation(app, client, &data->hover_amount, 0, 70, 0, 1);
}

static void
pinned_icon_mouse_leaves(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    possibly_close(app, container, data);
    client_create_animation(app, client, &data->hover_amount, 0, 70, 0, 0);
}

std::string
return_current_time_and_date() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%I:%M %p");
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
    year << std::put_time(std::localtime(&in_time_t), "%Y");
    
    ss << real_month << "/" << real_day << "/" << year.str();
    
    return ss.str();
}

void update_time(App *app, AppClient *client, Timeout *timeout, void *data) {
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
    
    auto *new_active_container = get_pinned_icon_representing_window(new_active_window);
    if (new_active_container) {
        if (auto data = (LaunchableButton *) new_active_container->user_data) {
            for (int i = 0; i < data->windows_data_list.size(); i++) {
                if (data->windows_data_list[i]->id == active_window) {
//                    std::swap(data->windows_data_list[0], data->windows_data_list[i]);
                    break;
                }
            }
        }
    }
    if (new_active_container == active_container)
        return;
    
    if (auto c = client_by_name(app, "taskbar")) {
        if (active_container) {
            auto data = (LaunchableButton *) active_container->user_data;
            if (data)
                client_create_animation(app, c, &data->active_amount, 0, 80, nullptr, 0);
        }
        
        active_container = new_active_container;
        if (new_active_container) {
            auto data = (LaunchableButton *) new_active_container->user_data;
            client_create_animation(app, c, &data->active_amount, 0, 120, nullptr, 1);
            
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
    update_pinned_items_file(false);
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *icon_container_copy = new Container(*icon_container);
    icon_container_copy->should_layout_children = true;
    layout(client_entity, client_entity->cr, icon_container_copy, icon_container_copy->real_bounds);
    
    for (int i = 0; i < icon_container->children.size(); ++i) {
        auto *real_icon = icon_container->children[i];
        auto *real_data = static_cast<LaunchableButton *>(real_icon->user_data);
        auto *laid_icon = icon_container_copy->children[i];
        
        // the real icon is already in the correct position
        if (real_icon->real_bounds.x == laid_icon->real_bounds.x) {
            continue;
        }
        
        // we don't want to align the real_icon if its the one the user is dragging
        if (all_except_dragged && real_icon->state.mouse_dragging) {
            continue;
        }
        
        if (real_data->animating) {
            if (real_data->target != laid_icon->real_bounds.x) {
                client_create_animation(app,
                                        client_entity,
                                        &real_icon->real_bounds.x, 0,
                                        100,
                                        nullptr,
                                        laid_icon->real_bounds.x,
                                        finished_icon_animation);
            }
        } else {
            real_data->animating = true;
            client_create_animation(app,
                                    client_entity,
                                    &real_icon->real_bounds.x, 0,
                                    100,
                                    nullptr,
                                    laid_icon->real_bounds.x,
                                    finished_icon_animation);
        }
    }
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
    client_create_animation(app, client_entity, &data->animation_zoom_amount, 0, 55 * data->animation_zoom_amount,
                            nullptr,
                            0);
    data->initial_mouse_click_before_drag_offset_x =
            container->real_bounds.x - client_entity->mouse_initial_x;
    container->z_index = 1;
    possibly_close(app, container, data);
    
    icons_align(client_entity, container->parent, true);
}

static void
pinned_icon_drag(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = static_cast<LaunchableButton *>(container->user_data);
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x = std::max(container->parent->real_bounds.x, container->real_bounds.x);
    container->real_bounds.x =
            std::min(container->parent->real_bounds.x + container->parent->real_bounds.w -
                     container->real_bounds.w,
                     container->real_bounds.x);
    
    possibly_close(app, container, data);
    
    auto *copy_parent = new Container(*container->parent);
    copy_parent->should_layout_children = true;
    layout(client_entity, cr, copy_parent, copy_parent->real_bounds);
    
    std::vector<int> centers;
    for (auto child: copy_parent->children) {
        centers.push_back(child->real_bounds.x + child->real_bounds.w / 2);
    }
    delete copy_parent;
    
    int our_center = container->real_bounds.x + container->real_bounds.w / 2;
    
    int new_index = 0;
    int center_diff = 100000;
    for (int i = 0; i < centers.size(); i++) {
        int diff = std::abs(centers[i] - our_center);
        if (diff < center_diff) {
            new_index = i;
            center_diff = diff;
        }
    }
    
    for (int x = 0; x < container->parent->children.size(); x++) {
        if (container->parent->children.at(x) == container) {
            if (x == new_index) {
                return;
            }
            container->parent->children.erase(container->parent->children.begin() + x);
            break;
        }
    }
    container->parent->children.insert(container->parent->children.begin() + new_index, container);
    
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x = std::max(container->parent->real_bounds.x, container->real_bounds.x);
    container->real_bounds.x =
            std::min(container->parent->real_bounds.x + container->parent->real_bounds.w -
                     container->real_bounds.w,
                     container->real_bounds.x);
    
    icons_align(client_entity, container->parent, true);
}

static void
pinned_icon_drag_end(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client_entity, &data->animation_zoom_amount, 0, 85 * data->animation_zoom_amount,
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
    if (audio_backend_data->audio_backend == Audio_Backend::NONE)
        return;
    if (audio_clients.empty())
        return;
    
    if (client_by_name(app, "volume") == nullptr) {
        open_volume_menu();
        volume_open_because_of_scroll = true;
    }
    
    for (auto c: audio_clients) {
        if (c->is_master_volume()) {
            if (audio_backend_data->audio_backend == Audio_Backend::PULSEAUDIO) {
                if (!c->default_sink) {
                    continue;
                }
            }
            
            adjust_volume_based_on_fine_scroll(c, client_entity, cr, container, horizontal_scroll, vertical_scroll,
                                               came_from_touchpad);
        }
    }
}

static void
scrolled_battery(AppClient *client,
                 cairo_t *cr,
                 Container *container,
                 int horizontal_scroll,
                 int vertical_scroll, bool came_from_touchpad) {
    if (client_by_name(app, "battery_menu") == nullptr) {
        start_battery_menu();
        battery_open_because_of_scroll = true;
    }
    adjust_brightness_based_on_fine_scroll(client, cr, container, horizontal_scroll, vertical_scroll,
                                           came_from_touchpad);
}

static void
pinned_icon_mouse_clicked(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client, &data->animation_zoom_amount, 0, 85 * data->animation_zoom_amount, nullptr, 0);
    
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_1) {
        if (data->windows_data_list.empty()) {
            launch_command(data->command_launched_by);
            app_timeout_stop(client->app, client, data->possibly_open_timeout);
            data->possibly_open_timeout = nullptr;
            for (auto c: app->clients) {
                if (c->name == "windows_selector") {
                    client_close(app, c);
                }
            }
        } else if (data->windows_data_list.size() > 1) {
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
        } else {
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
                    client_create_animation(app, client, &data->animation_bounce_amount, 0,
                                            451.2, nullptr, 1);
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
                client_create_animation(app, client, &data->animation_bounce_amount, 0,
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
    
    client_create_animation(app, client, &data->animation_zoom_amount, 0, 85 * (1 - data->animation_zoom_amount),
                            nullptr,
                            1);
}

static void
pinned_icon_mouse_up(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LaunchableButton *) container->user_data;
    
    client_create_animation(app, client, &data->animation_zoom_amount, 0, 85 * data->animation_zoom_amount, nullptr, 0);
}

static void
paint_minimize(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    Bounds bounds = container->real_bounds;
    bounds.w = 1;
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
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE971", strlen("\uE83F"));
    
    set_argb(cr, config->color_taskbar_button_icons);
    
    int width;
    int height;
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE702", strlen("\uE702"));
    
    set_argb(cr, config->color_taskbar_button_icons);
    
    int width;
    int height;
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
            get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    PangoAlignment initial_alignment = pango_layout_get_alignment(layout);
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_CENTER);
    
    int width;
    int height;
    pango_layout_set_text(layout, time_text.c_str(), time_text.size());
    pango_layout_get_pixel_size(layout, &width, &height);
    
    int pad = 16;
    if (container->wanted_bounds.w != width + pad) {
        container->wanted_bounds.w = width + pad;
        client_layout(app, client);
        request_refresh(app, client);
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
clicked_date(AppClient *client, cairo_t *cr, Container *container) {
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
clicked_bluetooth(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;
    if (!data->invalid_button_down) {
        open_bluetooth_menu();
    }
}

static void
clicked_battery(AppClient *client, cairo_t *cr, Container *container) {
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
    pango_layout_get_pixel_size(layout, &width, &height);

    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 12 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi));
    pango_cairo_show_layout(cr, layout);
    
    if (text_empty) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
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
    
    data->animating_capacity_index++;
    if (data->animating_capacity_index > 10)
        data->animating_capacity_index = data->capacity_index;
    
    request_refresh(app, client);
}

void update_battery_status_timeout(App *app, AppClient *client, Timeout *timeout, void *userdata) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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
    
    std::string regular[] = {"\uE678", "\uE679", "\uE67A", "\uE67B", "\uE67C", "\uE67D", "\uE67E", "\uE67F", "\uE680",
                             "\uE681", "\uE682"};
    
    std::string charging[] = {"\uE683", "\uE684", "\uE685", "\uE686", "\uE687", "\uE688", "\uE689", "\uE68A", "\uE68B",
                              "\uE68C", "\uE68D"};
    
    if (data->status == "Full") {
        pango_layout_set_text(layout, regular[10].c_str(), strlen("\uE83F"));
    } else if (data->status == "Charging") {
//        surface = data->charging_surfaces[data->animating_capacity_index];
        pango_layout_set_text(layout, charging[data->animating_capacity_index].c_str(), strlen("\uE83F"));
    } else {
//        surface = data->normal_surfaces[data->capacity_index];
        pango_layout_set_text(layout, regular[data->capacity_index].c_str(), strlen("\uE83F"));
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void invalidate_icon_button_press_if_window_open(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (IconButton *) container->user_data;
    
    if (get_current_time_in_ms() - data->timestamp > 100) {
        if (auto c = client_by_name(app, data->invalidate_button_press_if_client_with_this_name_is_open)) {
            data->invalid_button_down = true;
        } else {
            data->invalid_button_down = false;
        }
        data->timestamp = get_current_time_in_ms();
    }
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
    data->invalidate_button_press_if_client_with_this_name_is_open = "app_menu";
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
                app_timeout_create(app, client_entity, 7000, update_battery_status_timeout, data,
                                   const_cast<char *>(__PRETTY_FUNCTION__));
                update_battery_status_timeout(app, client_entity, nullptr, data);
                
                app_timeout_create(app, client_entity, 700, update_battery_animation_timeout, data,
                                   const_cast<char *>(__PRETTY_FUNCTION__));
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
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
    auto button_super_data = new IconButton;
    button_super_data->invalidate_button_press_if_client_with_this_name_is_open = "app_menu";
    button_super->user_data = button_super_data;
    button_super->when_mouse_down = invalidate_icon_button_press_if_window_open;
    button_super->name = "super";
    button_super->when_clicked = clicked_super;
    
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
    auto volume_data = new IconButton;
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
    
    app_timeout_create(app, client, 1000, update_time, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
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
    }
}

void remove_window(App *app, xcb_window_t window);

void add_window(App *app, xcb_window_t window);

static bool
window_event_handler(App *app, xcb_generic_event_t *event, xcb_window_t) {
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
                        for (unsigned int a = 0; a < sizeof(xcb_atom_t); a++) {
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
                                                                                &data->wants_attention_amount, 0, 10000,
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
                            }
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
                                                                            &data->wants_attention_amount, 0, 0,
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
                                    client_create_animation(app, client, &data->animation_bounce_amount, 0,
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
                                    client_create_animation(app, client, &data->animation_bounce_amount, 0,
                                                            451.2, nullptr, 1);
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case XCB_CLIENT_MESSAGE: {
            auto *e = (xcb_client_message_event_t *) event;
        
            // Drag and drop stuff from: https://www.acc.umu.se/~vatten/XDND.html
            if (e->type == get_cached_atom(app, "XdndPosition")) {
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
                    int data = 0;
                    data |= (1 << 1);
                    status_event.data.data32[1] = data; // if we are going to accept it
                    status_event.data.data32[2] = ((int) client->bounds->x << 16) | (int) client->bounds->y;
                    status_event.data.data32[3] = ((int) client->bounds->w << 16) | (int) client->bounds->h;
                    status_event.data.data32[3] = XCB_NONE;
                
                    auto xcb = app->connection;
                
                    xcb_send_event(xcb, false, drag_and_drop_source, XCB_EVENT_MASK_NO_EVENT,
                                   reinterpret_cast<const char *> (&status_event));
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
    audio_start(app);
    
    // Set window startup settings
    Settings settings;
    settings.window_transparent = true;
    settings.decorations = false;
    settings.dock = true;
    settings.skip_taskbar = true;
    settings.reserve_side = true;
    settings.reserve_bottom = config->taskbar_height;
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
            assert(primary_screen_info != nullptr);
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
    update_time(app, taskbar, nullptr, nullptr);
    update_active_window();
    
    load_pinned_icons();
    
    if (audio_backend_data->audio_backend == Audio_Backend::PULSEAUDIO) {
        audio_update_list_of_clients();
        update_taskbar_volume_icon();
    }
    
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
    find_icon_string_from_window_properties(window);
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
    
    // on gnome, the Extension app ends up adding the taskbar to the taskbar. I have no idea how it's doing that
    // but the fix for now is just going to be to ignore every client that is ours. Eventually when we make a settings
    // app, we will have to add an exception for that window.
    for (auto c: app->clients) {
        if (c->window == window) {
            return;
        }
    }
    
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
    
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        if (data->class_name == window_class_name) {
            const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
            xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
            xcb_flush(app->connection);
            
            data->windows_data_list.push_back(new WindowsData(app, window));
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
            for (unsigned int a = 0; a < sizeof(xcb_atom_t); a++) {
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
    
    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
    xcb_flush(app->connection);
    
    Container *a = icons->child(50 * config->dpi, FILL_SPACE);
    a->when_drag_end_is_click = false;
    a->minimum_x_distance_to_move_before_drag_begins = 15;
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
    data->class_name = window_class_name;
    data->icon_name = window_class_name;
    a->user_data = data;
    update_window_title_name(window);
    
    if (pid != -1) {
        data->has_launchable_info = true;
        data->command_launched_by = command_launched_by_line;
    }
    
    std::string path;
    std::string icon_name;
    
    auto get_wm_icon_name_cookie = xcb_icccm_get_wm_icon_name(app->connection, window);
    xcb_icccm_get_text_property_reply_t prop;
    uint8_t success = xcb_icccm_get_wm_icon_name_reply(app->connection, get_wm_icon_name_cookie, &prop, nullptr);
    if (success) {
        icon_name = prop.name;
        xcb_icccm_get_text_property_reply_wipe(&prop);
    } else {
        icon_name = get_icon_name(window);
    }
    if (!icon_name.empty()) {
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(icon_name));
        search_icons(targets);
        pick_best(targets, 24 * config->dpi);
        path = targets[0].best_full_path;
        data->icon_name = icon_name;
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
            pick_best(targets, 24 * config->dpi);
            path = targets[0].best_full_path;
            xcb_icccm_get_text_property_reply_wipe(&props);
        }
    }
    if (path.empty()) {
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(window_class_name));
        search_icons(targets);
        pick_best(targets, 24 * config->dpi);
        path = targets[0].best_full_path;
        data->icon_name = window_class_name;
    }
    
    if (!path.empty()) {
        load_icon_full_path(app, client, &data->surface, path, 24 * config->dpi);
    } else {
        xcb_generic_error_t *error;
        xcb_get_property_cookie_t c = xcb_ewmh_get_wm_icon(&app->ewmh, window);
        
        xcb_ewmh_get_wm_icon_reply_t wm_icon;
        memset(&wm_icon, 0, sizeof(xcb_ewmh_get_wm_icon_reply_t));
        xcb_ewmh_get_wm_icon_reply(&app->ewmh, c, &wm_icon, &error);
        
        if (error) {
            std::free(error);
            data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
            paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                     24 * config->dpi, nullptr);
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
                
                data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
                cairo_t *cr = cairo_create(data->surface);
                
                cairo_save(cr);
                double taskbar_icon_size = 24 * config->dpi;
                cairo_scale(cr, taskbar_icon_size / (width), taskbar_icon_size / (width));
                cairo_set_source(cr, pattern);
                cairo_paint(cr);
                cairo_restore(cr);
                
                cairo_destroy(cr);
                xcb_ewmh_get_wm_icon_reply_wipe(&wm_icon);
            } else {
                data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
                paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                         24 * config->dpi, nullptr);
            }
        }
    }
    
    update_minimize_icon_positions();
    update_pinned_items_file(false);
    
    client_layout(app, client);
    request_refresh(app, client);
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
                data->animation_bounce_amount = 0;
                data->animation_bounce_direction = 0;
                client_create_animation(app, entity, &data->animation_bounce_amount, 0, 0, nullptr, 0);
                
                delete data->windows_data_list[i];
                data->windows_data_list.erase(data->windows_data_list.begin() + i);
                
                if (data->windows_data_list.empty() && !data->pinned) {
                    if (active_container == container)
                        active_container = nullptr;
                    icons->children.erase(icons->children.begin() + j);
                    delete container;
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
    
    update_pinned_items_file(false);
    icons_align(entity, icons, false);
    request_refresh(app, entity);
}

void stacking_order_changed(xcb_window_t *all_windows, int windows_count) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    std::vector<xcb_window_t> new_windows;
    for (int i = 0; i < windows_count; i++) {
        new_windows.push_back(all_windows[i]);
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
    
    int i = 0;
    for (auto icon: icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        
        if (!data)
            continue;
        if (!data->pinned)
            continue;
        
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
    pick_best(targets, 24 * config->dpi);
    
    i = 0;
    for (const std::string &section_title: itemFile.Sections()) {
        auto *child = new Container();
        child->parent = icons;
        child->wanted_bounds.h = FILL_SPACE;
        child->wanted_bounds.w = 50 * config->dpi;
        
        child->when_drag_end_is_click = false;
        child->minimum_x_distance_to_move_before_drag_begins = 15;
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
            load_icon_full_path(app, client_entity, &data->surface, path, 24 * config->dpi);
        } else {
            data->surface = accelerated_surface(app, client_entity, 24 * config->dpi, 24 * config->dpi);
            char *string = getenv("HOME");
            std::string home(string);
            home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
            bool b = paint_surface_with_image(data->surface, home, 24 * config->dpi, nullptr);
            if (!b) {
                paint_surface_with_image(
                        data->surface, as_resource_path("unknown-24.svg"), 24 * config->dpi, nullptr);
            }
        }
        
        child->user_data = data;
        
        icons->children.push_back(child);
        
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
    std::thread t([]() {
        std::lock_guard lock(app->running_mutex);
        if (auto *client = client_by_name(app, "taskbar")) {
            client_layout(app, client);
            client_paint(app, client);
        }
    });
    t.detach();
}

void set_textarea_active() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            container->parent->active = true;
            container->parent->active = true;
        }
        request_refresh(client->app, client);
    }
}

void set_textarea_inactive() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            delete text_data->state;
            text_data->state = new TextState;
            container->parent->active = false;
        }
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
                    pick_best(targets, 24 * config->dpi);
                    path = targets[0].best_full_path;
                    if (!data->icon_name.empty()) {
                        path = targets[0].best_full_path;
                    }
                    if (path.empty()) {
                        path = targets[1].best_full_path;
                    }
                    if (!path.empty()) {
                        load_icon_full_path(app, client, &data->surface, path, 24 * config->dpi);
                    } else {
                        data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
                        char *string = getenv("HOME");
                        std::string home(string);
                        home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
                        bool b = paint_surface_with_image(data->surface, home, 24 * config->dpi, nullptr);
                        if (!b) {
                            paint_surface_with_image(
                                    data->surface, as_resource_path("unknown-24.svg"), 24 * config->dpi, nullptr);
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
    if (!mapped)
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
                            if (!data->command_launched_by.empty())
                                launch_command(data->command_launched_by);
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
    
    return "";
}

TextAreaSettings::TextAreaSettings(float scale) : ScrollPaneSettings(scale) {}
