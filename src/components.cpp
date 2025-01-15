//
// Created by jmanc3 on 6/14/20.
//

#include "components.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include "utility.h"
#include "config.h"

#include <atomic>
#include <utility>
#include <pango/pangocairo.h>
#include <cmath>

static int scroll_amount = 120;
static double scroll_anim_time = 120;
static double repeat_time = scroll_anim_time * .9;
static easingFunction easing_function = 0;

static std::atomic<bool> dragging = false;

static std::atomic<bool> mouse_down_arrow_held = false;

void scrollpane_scrolled(AppClient *client,
                         cairo_t *cr,
                         Container *container,
                         int scroll_x,
                         int scroll_y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);
    
    if (reply->mods & XKB_KEY_Shift_L || reply->mods & XKB_KEY_Control_L) {
        container->scroll_h_real += scroll_x * scroll_amount + scroll_y * scroll_amount;
    } else {
        container->scroll_h_real += scroll_x * scroll_amount;
        container->scroll_v_real += scroll_y * scroll_amount;
    }
    
    container->scroll_h_visual = container->scroll_h_real;
    container->scroll_v_visual = container->scroll_v_real;
    ::layout(client, cr, container, container->real_bounds);
}

void fine_scrollpane_scrolled(AppClient *client,
                              cairo_t *cr,
                              Container *container,
                              int scroll_x,
                              int scroll_y,
                              bool came_from_touchpad) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);
    
    if (reply->mods & XKB_KEY_Shift_L || reply->mods & XKB_KEY_Control_L) {
        container->scroll_h_real += scroll_x + scroll_y;
    } else {
        container->scroll_h_real += scroll_x;
        container->scroll_v_real += scroll_y;
    }
    
    if (container->type == newscroll && !came_from_touchpad) {
        auto *scroll = (ScrollContainer *) container;
        
        long current_time = get_current_time_in_ms();
        double ms_between_scroll = current_time - scroll->previous_time_scrolled;
        scroll->previous_time_scrolled = current_time;
        if (ms_between_scroll > 300) {
            scroll->previous_delta_diff = -1;
            ms_between_scroll = 300;
        }
        
        double anim_time = 140;
        if (scroll->previous_delta_diff != -1) {
            if (scroll->previous_delta_diff < ms_between_scroll) { // we slowed
                anim_time = 200;
            } else if (scroll->previous_delta_diff >= ms_between_scroll) { // we sped up
                anim_time = 100;
            }
        }
        scroll->previous_delta_diff = ms_between_scroll;
       
        client_create_animation(client->app, client, &container->scroll_v_visual, container->lifetime, 0,
                                anim_time,
                                getEasingFunction(EaseOutCubic),
                                container->scroll_v_real, true);
        client_create_animation(client->app, client, &container->scroll_h_visual, container->lifetime, 0,
                                anim_time,
                                getEasingFunction(EaseInOutSine),
                                container->scroll_h_real, true);
    } else {
        container->scroll_h_visual = container->scroll_h_real;
        container->scroll_v_visual = container->scroll_v_real;
        ::layout(client, cr, container, container->real_bounds);
    }
}

static void
paint_scroll_bg(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto scroll_container = ((ScrollContainer *) container->parent->parent);
    set_rect(cr, container->real_bounds);
    ArgbColor color = config->color_apps_scrollbar_gutter;
    color.a = scroll_container->scrollbar_openess;
    set_argb(cr, color);
    cairo_fill(cr);
}

static void
paint_right_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool minimal = ((ScrollContainer *) container->parent->parent)->settings.paint_minimal;
    auto scroll_container = ((ScrollContainer *) container->parent->parent);
    
    if (!minimal)
        paint_scroll_bg(client, cr, container);
    
    Container *scrollpane = container->parent->parent;
    
    auto right_bounds = right_thumb_bounds(scrollpane, container->real_bounds);
    
    right_bounds.x += right_bounds.w;
    right_bounds.w = std::max(right_bounds.w * scroll_container->scrollbar_openess, 2.0);
    if (minimal)
        right_bounds.w = 2.0;
    right_bounds.x -= right_bounds.w;
    right_bounds.x -= 2 * (1 - scroll_container->scrollbar_openess);
    
    set_rect(cr, right_bounds);
    
    if (container->state.mouse_pressing) {
        ArgbColor color = config->color_apps_scrollbar_pressed_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        ArgbColor color = config->color_apps_scrollbar_hovered_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else if (right_bounds.w == 2.0) {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        lighten(&color, 10);
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    }
    
    cairo_fill(cr);
}

static void
paint_bottom_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool minimal = ((ScrollContainer *) container->parent->parent)->settings.paint_minimal;
    auto scroll_container = ((ScrollContainer *) container->parent->parent);
    
    if (!minimal)
        paint_scroll_bg(client, cr, container);
    
    Container *scrollpane = container->parent->parent;
    
    auto bottom_bounds = bottom_thumb_bounds(scrollpane, container->real_bounds);
    
    bottom_bounds.y += bottom_bounds.h;
    bottom_bounds.h = std::max(bottom_bounds.h * scroll_container->scrollbar_openess, 2.0);
    bottom_bounds.y -= bottom_bounds.h;
    bottom_bounds.y -= 2 * (1 - scroll_container->scrollbar_openess);
    
    set_rect(cr, bottom_bounds);
    
    if (container->state.mouse_pressing) {
        ArgbColor color = config->color_apps_scrollbar_pressed_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else if (bounds_contains(bottom_bounds, client->mouse_current_x, client->mouse_current_y)) {
        ArgbColor color = config->color_apps_scrollbar_hovered_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else if (bottom_bounds.w == 2.0) {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        lighten(&color, 10);
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    } else {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        color.a = scroll_container->scrollbar_visible;
        set_argb(cr, color);
    }
    
    cairo_fill(cr);
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool minimal = ((ScrollContainer *) container->parent->parent)->settings.paint_minimal;
    if (minimal)
        return;
    auto scroll_container = ((ScrollContainer *) container->parent->parent);
    
    auto *data = (ButtonData *) container->user_data;
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_pressed_button;
            color.a = scroll_container->scrollbar_openess;
            set_argb(cr, color);
            cairo_fill(cr);
        } else {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_hovered_button;
            color.a = scroll_container->scrollbar_openess;
            set_argb(cr, color);
            cairo_fill(cr);
        }
    } else {
        set_rect(cr, container->real_bounds);
        ArgbColor color = config->color_apps_scrollbar_default_button;
        color.a = scroll_container->scrollbar_openess;
        set_argb(cr, color);
        cairo_fill(cr);
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 6 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            auto c = ArgbColor(config->color_apps_scrollbar_pressed_button_icon);
            c.a = scroll_container->scrollbar_openess;
            set_argb(cr, c);
        } else {
            auto c = ArgbColor(config->color_apps_scrollbar_hovered_button_icon);
            c.a = scroll_container->scrollbar_openess;
            set_argb(cr, c);
        }
    } else {
        auto c = ArgbColor(config->color_apps_scrollbar_default_button_icon);
        c.a = scroll_container->scrollbar_openess;
        set_argb(cr, c);
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, data->text.data(), strlen("\uE83F"));
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_show(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing) {
            set_argb(cr, ArgbColor(0, 0, 0, .3));
        } else {
            set_argb(cr, ArgbColor(0, 0, 0, .1));
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    if (container->parent->active)
        set_argb(cr, ArgbColor(1, 0, 1, 1));
    else
        set_argb(cr, ArgbColor(0, 1, 1, 1));
    
    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, 1);
    cairo_fill(cr);
    
    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, 1, container->real_bounds.h);
    cairo_fill(cr);
    
    cairo_rectangle(cr,
                    container->real_bounds.x + container->real_bounds.w - 1,
                    container->real_bounds.y,
                    1,
                    container->real_bounds.h);
    cairo_fill(cr);
    
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h - 1,
                    container->real_bounds.w,
                    1);
    cairo_fill(cr);
}

struct MouseDownInfo {
    Container *container = nullptr;
    int horizontal_change = 0;
    int vertical_change = 0;
};

static void
mouse_down_thread(App *app, AppClient *client, Timeout *, void *data) {
    auto *mouse_info = (MouseDownInfo *) data;
    
    if (mouse_down_arrow_held.load() && valid_client(app, client) && app->running) {
        if (bounds_contains(mouse_info->container->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            Container *target = nullptr;
            bool clamp = false;
            if (auto *s = dynamic_cast<ScrollContainer *>(mouse_info->container->parent->parent)) {
                target = s;
                clamp = true;
            } else {
                target = mouse_info->container->parent->parent->children[2];
            }
        
            target->scroll_h_real += mouse_info->horizontal_change * 1.5;
            target->scroll_v_real += mouse_info->vertical_change * 1.5;
            if (clamp) clamp_scroll((ScrollContainer *) target);
        
            // ::layout(target, target->real_bounds, false);
//            client_layout(client->app, client);
        
            if (mouse_info->horizontal_change != 0)
                client_create_animation(client->app,
                                        client,
                                        &target->scroll_h_visual, target->lifetime, 0,
                                        scroll_anim_time,
                                        easing_function,
                                        target->scroll_h_real,
                                        true);
            if (mouse_info->vertical_change != 0)
                client_create_animation(client->app,
                                        client,
                                        &target->scroll_v_visual, target->lifetime, 0,
                                        scroll_anim_time,
                                        easing_function,
                                        target->scroll_v_real,
                                        true);
            
            app_timeout_create(app, client, repeat_time, mouse_down_thread, mouse_info, const_cast<char *>(__PRETTY_FUNCTION__));
        } else {
            app_timeout_create(app, client, repeat_time, mouse_down_thread, mouse_info, const_cast<char *>(__PRETTY_FUNCTION__));
        }
    } else {
        delete mouse_info;
    }
}

static void
mouse_down_arrow_up(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *target = nullptr;
    bool clamp = false;
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent->parent)) {
        target = s;
        clamp = true;
    } else {
        target = container->parent->parent->children[2];
    }
    target->scroll_v_real += scroll_amount;
    if (clamp) clamp_scroll((ScrollContainer *) target);
    
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);
    
    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);
    
    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    auto *data = new MouseDownInfo;
    data->container = container;
    data->vertical_change = scroll_amount;
    app_timeout_create(client->app, client, scroll_anim_time * 1.56, mouse_down_thread, data, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
mouse_down_arrow_bottom(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *target = nullptr;
    bool clamp = false;
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent->parent)) {
        target = s;
        clamp = true;
    } else {
        target = container->parent->parent->children[2];
    }
    target->scroll_v_real -= scroll_amount;
    if (clamp) clamp_scroll((ScrollContainer *) target);
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);
    
    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);
    
    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    auto *data = new MouseDownInfo;
    data->container = container;
    data->vertical_change = -scroll_amount;
    app_timeout_create(client->app, client, scroll_anim_time * 1.56, mouse_down_thread, data, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
mouse_down_arrow_left(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *target = nullptr;
    bool clamp = false;
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent->parent)) {
        target = s;
        clamp = true;
    } else {
        target = container->parent->parent->children[2];
    }
    target->scroll_h_real += scroll_amount;
    if (clamp) clamp_scroll((ScrollContainer *) target);
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);
    
    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);
    
    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    auto *data = new MouseDownInfo;
    data->container = container;
    data->horizontal_change = scroll_amount;
    app_timeout_create(client->app, client, scroll_anim_time * 1.56, mouse_down_thread, data, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
mouse_down_arrow_right(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *target = nullptr;
    bool clamp = false;
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent->parent)) {
        target = s;
        clamp = true;
    } else {
        target = container->parent->parent->children[2];
    }
    target->scroll_h_real -= scroll_amount;
    if (clamp) clamp_scroll((ScrollContainer *) target);
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);
    
    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual, target->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);
    
    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    auto *data = new MouseDownInfo;
    data->container = container;
    data->horizontal_change = -scroll_amount;
    app_timeout_create(client->app, client, scroll_anim_time * 1.56, mouse_down_thread, data, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
right_thumb_scrolled(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(container)) {
        fine_scrollpane_scrolled(client, cr, container, 0, scroll_y * 120, false);
    } else {
        Container *target = container->parent->children[2];
        scrollpane_scrolled(client, cr, target, 0, scroll_y);
    }
}

static void
bottom_thumb_scrolled(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(container)) {
        fine_scrollpane_scrolled(client, cr, container, scroll_x * 120, 0, false);
    } else {
        Container *target = container->parent->children[2];
        scrollpane_scrolled(client, cr, target, scroll_x, 0);
    }
}

static void
fine_right_thumb_scrolled(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y,
                          bool came_from_touchpad) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent)) {
        fine_scrollpane_scrolled(client, cr, container->parent, scroll_x, scroll_y, came_from_touchpad);
    }
}

static void
fine_bottom_thumb_scrolled(AppClient *client, cairo_t *cr, Container *container, int scroll_x, int scroll_y,
                           bool came_from_touchpad) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(container->parent)) {
        fine_scrollpane_scrolled(client, cr, container->parent, scroll_x, scroll_y, came_from_touchpad);
    }
}

static void
mouse_arrow_up(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    mouse_down_arrow_held = false;
}

Bounds
right_thumb_bounds(Container *scrollpane, Bounds thumb_area) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(scrollpane)) {
        double true_height = actual_true_height(s->content);
        if (s->bottom && s->bottom->exists && !s->settings.bottom_inline_track)
            true_height += s->bottom->real_bounds.h;
        
        double start_scalar = -s->scroll_v_visual / true_height;
        double thumb_height = thumb_area.h * (s->real_bounds.h / true_height);
        double start_off = thumb_area.y + thumb_area.h * start_scalar;
        double avail = thumb_area.h - (start_off - thumb_area.y);
        
        return {thumb_area.x, start_off, thumb_area.w, std::min(thumb_height, avail)};
    }
    
    auto content_area = scrollpane->children[2];
    double total_height = content_area->children[0]->real_bounds.h;
    
    double view_height = content_area->real_bounds.h;
    
    double view_scalar = view_height / total_height;
    double thumb_height = view_scalar * thumb_area.h;
    
    // 0 as min pos and 1 as max position
    double max_scroll = total_height - view_height;
    if (max_scroll < 0)
        max_scroll = 0;
    
    double scroll_scalar = (-content_area->scroll_v_visual) / max_scroll;
    double scroll_offset = (thumb_area.h - thumb_height) * scroll_scalar;
    if (max_scroll == 0) {
        scroll_offset = 0;
    }
    if (thumb_height > thumb_area.h) {
        thumb_height = thumb_area.h;
    }
    return Bounds(thumb_area.x, thumb_area.y + scroll_offset, thumb_area.w, thumb_height);
}

Bounds
bottom_thumb_bounds(Container *scrollpane, Bounds thumb_area) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *s = dynamic_cast<ScrollContainer *>(scrollpane)) {
        double true_width = actual_true_width(s->content);
        if (s->right && s->right->exists && !s->settings.right_inline_track)
            true_width += s->right->real_bounds.w;
        
        double start_scalar = -s->scroll_h_visual / true_width;
        double thumb_width = thumb_area.w * (s->real_bounds.w / true_width);
        double start_off = thumb_area.x + thumb_area.w * start_scalar;
        double avail = thumb_area.w - (start_off - thumb_area.x);
        
        return {thumb_area.x + thumb_area.w * start_scalar, thumb_area.y, std::min(thumb_width, avail), thumb_area.h};
    }
    
    auto content_area = scrollpane->children[2];
    double total_width = content_area->children[0]->real_bounds.w;
    
    double view_width = content_area->real_bounds.w;
    
    if (total_width == 0) {
        total_width = view_width;
    }
    double view_scalar = view_width / total_width;
    double thumb_width = view_scalar * thumb_area.w;
    
    // 0 as min pos and 1 as max position
    double max_scroll = total_width - view_width;
    if (max_scroll < 0)
        max_scroll = 0;
    
    double scroll_scalar = (-content_area->scroll_h_visual) / max_scroll;
    double scroll_offset = (thumb_area.w - thumb_width) * scroll_scalar;
    if (max_scroll == 0) {
        scroll_offset = 0;
    }
    
    if (thumb_width > thumb_area.w) {
        thumb_width = thumb_area.w;
    }
    return Bounds(thumb_area.x + scroll_offset, thumb_area.y, thumb_width, thumb_area.h);
}

static void
clicked_right_thumb(AppClient *client, cairo_t *cr, Container *thumb_container, bool animate) {
    if (auto *s = dynamic_cast<ScrollContainer *>(thumb_container->parent->parent)) {
        auto *content = s->content;
        
        double thumb_height = right_thumb_bounds(s, thumb_container->real_bounds).h;
        double mouse_y = client->mouse_current_y;
        if (mouse_y < thumb_container->real_bounds.y) {
            mouse_y = thumb_container->real_bounds.y;
        } else if (mouse_y > thumb_container->real_bounds.y + thumb_container->real_bounds.h) {
            mouse_y = thumb_container->real_bounds.y + thumb_container->real_bounds.h;
        }
        
        mouse_y -= thumb_container->real_bounds.y;
        mouse_y -= thumb_height / 2;
        double scalar = mouse_y / thumb_container->real_bounds.h;
        
        // why the fuck do I have to add the real...h to true height to actually get
        // the true height
        double true_height = actual_true_height(s->content);
        if (s->bottom && s->bottom->exists && !s->settings.bottom_inline_track)
            true_height += s->bottom->real_bounds.h;
        
        double y = true_height * scalar;
        
        s->scroll_v_real = -y;
        s->scroll_v_visual = -y;
        ::layout(client, cr, s, s->real_bounds);
        
        return;
    }
    
    auto *scrollpane = thumb_container->parent->parent;
    auto *content = scrollpane->children[2];
    
    double thumb_height =
            right_thumb_bounds(scrollpane, thumb_container->real_bounds).h;
    double mouse_y = client->mouse_current_y;
    if (mouse_y < thumb_container->real_bounds.y) {
        mouse_y = thumb_container->real_bounds.y;
    } else if (mouse_y > thumb_container->real_bounds.y + thumb_container->real_bounds.h) {
        mouse_y = thumb_container->real_bounds.y + thumb_container->real_bounds.h;
    }
    
    mouse_y -= thumb_container->real_bounds.y;
    mouse_y -= thumb_height / 2;
    double scalar = mouse_y / thumb_container->real_bounds.h;
    
    // why the fuck do I have to add the real...h to true height to actually get
    // the true height
    double content_height = true_height(content) + content->real_bounds.h;
    double y = content_height * scalar;
    
    content->scroll_v_real = -y;
    if (!animate)
        content->scroll_v_visual = -y;
    // ::layout(content_area, content_area->real_bounds, false);
    client_layout(client->app, client);
    
    if (animate) {
        client_create_animation(client->app,
                                client,
                                &content->scroll_h_visual, content->lifetime, 0,
                                scroll_anim_time * 2,
                                easing_function,
                                content->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content->scroll_v_visual, content->lifetime, 0,
                                scroll_anim_time * 2,
                                easing_function,
                                content->scroll_v_real,
                                true);
    } else {
        client_create_animation(client->app,
                                client,
                                &content->scroll_h_visual, content->lifetime, 0,
                                0,
                                easing_function,
                                content->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content->scroll_v_visual, content->lifetime, 0,
                                0,
                                easing_function,
                                content->scroll_v_real,
                                true);
    }
}

static void
clicked_bottom_thumb(AppClient *client, cairo_t *cr, Container *thumb_container, bool animate) {
    if (auto *s = dynamic_cast<ScrollContainer *>(thumb_container->parent->parent)) {
        auto *content = s->content;
        
        double thumb_width =
                bottom_thumb_bounds(thumb_container->parent->parent, thumb_container->real_bounds).w;
        double mouse_x = client->mouse_current_x;
        if (mouse_x < thumb_container->real_bounds.x) {
            mouse_x = thumb_container->real_bounds.x;
        } else if (mouse_x > thumb_container->real_bounds.x + thumb_container->real_bounds.w) {
            mouse_x = thumb_container->real_bounds.x + thumb_container->real_bounds.w;
        }
        
        mouse_x -= thumb_container->real_bounds.x;
        mouse_x -= thumb_width / 2;
        double scalar = mouse_x / thumb_container->real_bounds.w;
        
        double true_width = actual_true_width(s->content);
        if (s->right && s->right->exists && !s->settings.right_inline_track)
            true_width += s->right->real_bounds.w;
        
        double x = true_width * scalar;
        
        s->scroll_h_real = -x;
        s->scroll_h_visual = -x;
        ::layout(client, cr, s, s->real_bounds);
        
        return;
    }
    
    auto *scrollpane = thumb_container->parent->parent;
    auto *content = scrollpane->children[2];
    
    double thumb_width =
            bottom_thumb_bounds(thumb_container->parent->parent, thumb_container->real_bounds).w;
    double mouse_x = client->mouse_current_x;
    if (mouse_x < thumb_container->real_bounds.x) {
        mouse_x = thumb_container->real_bounds.x;
    } else if (mouse_x > thumb_container->real_bounds.x + thumb_container->real_bounds.w) {
        mouse_x = thumb_container->real_bounds.x + thumb_container->real_bounds.w;
    }
    
    mouse_x -= thumb_container->real_bounds.x;
    mouse_x -= thumb_width / 2;
    double scalar = mouse_x / thumb_container->real_bounds.w;
    
    // why the fuck do I have to add the real...w to true width to actually get
    // the true width
    double content_width = true_width(content) + content->real_bounds.w;
    double x = content_width * scalar;
    
    Container *content_area = thumb_container->parent->parent->children[2];
    content_area->scroll_h_real = -x;
    if (!animate)
        content_area->scroll_h_visual = -x;
    // ::layout(content_area, content_area->real_bounds, false);
    client_layout(client->app, client);
    
    if (animate) {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual, content_area->lifetime, 0,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual, content_area->lifetime, 0,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    } else {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual, content_area->lifetime, 0,
                                0,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual, content_area->lifetime, 0,
                                0,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    }
}

static void
right_scrollbar_mouse_down(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, true);
}

static void
right_scrollbar_drag_start(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
right_scrollbar_drag(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
right_scrollbar_drag_end(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_mouse_down(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, true);
}

static void
bottom_scrollbar_drag_start(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_drag(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_drag_end(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

static void
paint_content(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_save(cr);
    cairo_push_group(cr);
    
    for (auto *c: container->children) {
        if (overlaps(c->real_bounds, c->parent->parent->real_bounds)) {
            if (c->when_paint) {
                c->when_paint(client, cr, c);
            }
        }
    }
    
    cairo_pop_group_to_source(cr);
    
    cairo_rectangle(cr,
                    container->parent->real_bounds.x,
                    container->parent->real_bounds.y,
                    container->parent->real_bounds.w,
                    container->parent->real_bounds.h);
    cairo_clip(cr);
    cairo_paint(cr);
    cairo_restore(cr);
}

Container *
make_scrollpane(Container *parent, ScrollPaneSettings settings) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto scrollable_pane = parent->child(FILL_SPACE, FILL_SPACE);
    scrollable_pane->type = ::scrollpane;
    if (settings.bottom_inline_track)
        scrollable_pane->type |= ::scrollpane_inline_b;
    if (settings.right_inline_track)
        scrollable_pane->type |= ::scrollpane_inline_r;
    
    scrollable_pane->type |= (1 << (settings.bottom_show_amount + 8));
    scrollable_pane->type |= (1 << (settings.right_show_amount + 5));
    
    auto right_vbox = scrollable_pane->child(settings.right_width, FILL_SPACE);
    right_vbox->type = ::vbox;
    right_vbox->when_scrolled = right_thumb_scrolled;
    
    auto right_top_arrow = right_vbox->child(FILL_SPACE, settings.right_arrow_height);
    right_top_arrow->when_paint = paint_show;
    right_top_arrow->when_mouse_down = mouse_down_arrow_up;
    right_top_arrow->when_mouse_up = mouse_arrow_up;
    right_top_arrow->when_clicked = mouse_arrow_up;
    right_top_arrow->when_drag_end = mouse_arrow_up;
    auto right_thumb = right_vbox->child(FILL_SPACE, FILL_SPACE);
    right_thumb->when_paint = paint_show;
    right_thumb->when_drag_start = right_scrollbar_drag_start;
    right_thumb->when_drag = right_scrollbar_drag;
    right_thumb->when_drag_end = right_scrollbar_drag_end;
    right_thumb->when_mouse_down = right_scrollbar_mouse_down;
    
    auto right_bottom_arrow = right_vbox->child(FILL_SPACE, settings.right_arrow_height);
    right_bottom_arrow->when_paint = paint_show;
    right_bottom_arrow->when_mouse_down = mouse_down_arrow_bottom;
    right_bottom_arrow->when_mouse_up = mouse_arrow_up;
    right_bottom_arrow->when_clicked = mouse_arrow_up;
    right_bottom_arrow->when_drag_end = mouse_arrow_up;
    right_vbox->z_index += 1;
    
    auto bottom_hbox = scrollable_pane->child(FILL_SPACE, settings.bottom_height);
    bottom_hbox->type = ::hbox;
    auto bottom_left_arrow = bottom_hbox->child(settings.bottom_arrow_width, FILL_SPACE);
    bottom_left_arrow->when_paint = paint_show;
    bottom_left_arrow->when_mouse_down = mouse_down_arrow_left;
    bottom_left_arrow->when_mouse_up = mouse_arrow_up;
    bottom_left_arrow->when_clicked = mouse_arrow_up;
    bottom_left_arrow->when_drag_end = mouse_arrow_up;
    auto bottom_thumb = bottom_hbox->child(FILL_SPACE, FILL_SPACE);
    bottom_thumb->when_paint = paint_show;
    bottom_thumb->when_drag_start = bottom_scrollbar_drag_start;
    bottom_thumb->when_drag = bottom_scrollbar_drag;
    bottom_thumb->when_drag_end = bottom_scrollbar_drag_end;
    bottom_thumb->when_mouse_down = bottom_scrollbar_mouse_down;
    
    auto bottom_right_arrow = bottom_hbox->child(settings.bottom_arrow_width, FILL_SPACE);
    bottom_right_arrow->when_paint = paint_show;
    bottom_right_arrow->when_mouse_down = mouse_down_arrow_right;
    bottom_right_arrow->when_mouse_up = mouse_arrow_up;
    bottom_right_arrow->when_clicked = mouse_arrow_up;
    bottom_right_arrow->when_drag_end = mouse_arrow_up;
    bottom_hbox->z_index += 1;
    
    auto content_container = scrollable_pane->child(FILL_SPACE, FILL_SPACE);
    //    content_container->when_paint = paint_show;
    content_container->when_scrolled = scrollpane_scrolled;
    content_container->receive_events_even_if_obstructed = true;
    
    if (settings.make_content) {
        // scroll_pane->children[2]->chilren[0]
        auto content = content_container->child(FILL_SPACE, FILL_SPACE);
        content->spacing = 0;
        content->when_paint = paint_content;
        content->clip_children = false; // We have to do custom clipping so don't waste calls on this
        content->automatically_paint_children = false;
        content->name = "content";
    }
    
    return content_container;
}

void scroll_hover_timeout(App *, AppClient *client, Timeout *,
                          void *userdata) {
    auto *container = (Container *) userdata;
    if (container_by_container(container, client->root)) {
        auto *scroll = (ScrollContainer *) container;
        scroll->openess_delay_timeout = nullptr;
        if (!bounds_contains(scroll->right->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            client_create_animation(client->app, client, &scroll->scrollbar_openess, scroll->lifetime,
                                    0, 100, nullptr, 0);
        }
    }
}

ScrollContainer *make_newscrollpane_as_child(Container *parent, const ScrollPaneSettings &settings) {
    auto scrollpane = new ScrollContainer(settings);
    // setup as child of root
    if (parent)
        scrollpane->parent = parent;
//    scrollpane->when_scrolled = scrollpane_scrolled;
    scrollpane->when_fine_scrolled = fine_scrollpane_scrolled;
    scrollpane->receive_events_even_if_obstructed = true;
    if (settings.start_at_end) {
        scrollpane->scroll_v_real = -1000000;
        scrollpane->scroll_v_visual = -1000000;
    }
    scrollpane->scrollbar_openess = 0;
    scrollpane->scrollbar_visible = 0;
    if (parent)
        parent->children.push_back(scrollpane);
    scrollpane->when_mouse_leaves_container = [](AppClient *client, cairo_t *, Container *container) {
        auto scroll = (ScrollContainer *) container;
        client_create_animation(client->app, client, &scroll->scrollbar_visible, scroll->lifetime, 0, 100, nullptr, 0);
        client_create_animation(client->app, client, &scroll->scrollbar_openess, scroll->lifetime, 0, 100, nullptr, 0);
    };
    scrollpane->when_mouse_enters_container = [](AppClient *client, cairo_t *, Container *container) {
        auto scroll = (ScrollContainer *) container;
        client_create_animation(client->app, client, &scroll->scrollbar_visible, scroll->lifetime, 0, 100, nullptr, 1);
    };
    
    auto content = new Container(::vbox, FILL_SPACE, FILL_SPACE);
    // setup as content of scrollpane
    content->parent = scrollpane;
    scrollpane->content = content;
    
    auto right_vbox = new Container(settings.right_width, FILL_SPACE);
    scrollpane->right = right_vbox;
    right_vbox->parent = scrollpane;
    right_vbox->type = ::vbox;
    right_vbox->when_fine_scrolled = fine_right_thumb_scrolled;
    right_vbox->when_mouse_leaves_container = [](AppClient *client, cairo_t *, Container *container) {
        auto scroll = (ScrollContainer *) container->parent;
        int delay = 0;
        if (bounds_contains(scroll->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            delay = 3000;
        }
        if (delay == 0) {
            client_create_animation(client->app, client, &scroll->scrollbar_openess, scroll->lifetime,
                                    delay, 100, nullptr, 0);
        } else {
            if (!scroll->openess_delay_timeout) {
                scroll->openess_delay_timeout =
                        app_timeout_create(client->app, client, 3000, scroll_hover_timeout,
                                           container->parent, "scrollpane_3000ms_timeout");
            } else {
                scroll->openess_delay_timeout = app_timeout_replace(client->app, client, scroll->openess_delay_timeout,
                                                                    3000, scroll_hover_timeout, container->parent);
            }
        }
    };
    right_vbox->receive_events_even_if_obstructed = true;
    right_vbox->when_mouse_enters_container = [](AppClient *client, cairo_t *, Container *container) {
        auto scroll = (ScrollContainer *) container->parent;
        client_create_animation(client->app, client, &scroll->scrollbar_openess, scroll->lifetime, 0, 100, nullptr, 1);
    };
    
    auto right_top_arrow = right_vbox->child(FILL_SPACE, settings.right_arrow_height);
    right_top_arrow->user_data = new ButtonData;
    ((ButtonData *) right_top_arrow->user_data)->text = "\uE971";
    right_top_arrow->when_paint = paint_arrow;
    right_top_arrow->when_mouse_down = mouse_down_arrow_up;
    right_top_arrow->when_mouse_up = mouse_arrow_up;
    right_top_arrow->when_clicked = mouse_arrow_up;
    right_top_arrow->when_drag_end = mouse_arrow_up;
    auto right_thumb = right_vbox->child(FILL_SPACE, FILL_SPACE);
    right_thumb->when_paint = paint_right_thumb;
    right_thumb->when_drag_start = right_scrollbar_drag_start;
    right_thumb->when_drag = right_scrollbar_drag;
    right_thumb->when_drag_end = right_scrollbar_drag_end;
    right_thumb->when_mouse_down = right_scrollbar_mouse_down;
    
    auto right_bottom_arrow = right_vbox->child(FILL_SPACE, settings.right_arrow_height);
    right_bottom_arrow->user_data = new ButtonData;
    ((ButtonData *) right_bottom_arrow->user_data)->text = "\uE972";
    right_bottom_arrow->when_paint = paint_arrow;
    right_bottom_arrow->when_mouse_down = mouse_down_arrow_bottom;
    right_bottom_arrow->when_mouse_up = mouse_arrow_up;
    right_bottom_arrow->when_clicked = mouse_arrow_up;
    right_bottom_arrow->when_drag_end = mouse_arrow_up;
    right_vbox->z_index += 1;
    
    auto bottom_hbox = new Container(FILL_SPACE, settings.bottom_height);
    scrollpane->bottom = bottom_hbox;
    bottom_hbox->parent = scrollpane;
    bottom_hbox->type = ::hbox;
    bottom_hbox->when_fine_scrolled = fine_bottom_thumb_scrolled;
    auto bottom_left_arrow = bottom_hbox->child(settings.bottom_arrow_width, FILL_SPACE);
    bottom_left_arrow->user_data = new ButtonData;
    ((ButtonData *) bottom_left_arrow->user_data)->text = "\uE973";
    bottom_left_arrow->when_paint = paint_arrow;
    bottom_left_arrow->when_mouse_down = mouse_down_arrow_left;
    bottom_left_arrow->when_mouse_up = mouse_arrow_up;
    bottom_left_arrow->when_clicked = mouse_arrow_up;
    bottom_left_arrow->when_drag_end = mouse_arrow_up;
    auto bottom_thumb = bottom_hbox->child(FILL_SPACE, FILL_SPACE);
    bottom_thumb->when_paint = paint_bottom_thumb;
    bottom_thumb->when_drag_start = bottom_scrollbar_drag_start;
    bottom_thumb->when_drag = bottom_scrollbar_drag;
    bottom_thumb->when_drag_end = bottom_scrollbar_drag_end;
    bottom_thumb->when_mouse_down = bottom_scrollbar_mouse_down;
    
    auto bottom_right_arrow = bottom_hbox->child(settings.bottom_arrow_width, FILL_SPACE);
    bottom_right_arrow->user_data = new ButtonData;
    ((ButtonData *) bottom_right_arrow->user_data)->text = "\uE974";
    bottom_right_arrow->when_paint = paint_arrow;
    bottom_right_arrow->when_mouse_down = mouse_down_arrow_right;
    bottom_right_arrow->when_mouse_up = mouse_arrow_up;
    bottom_right_arrow->when_clicked = mouse_arrow_up;
    bottom_right_arrow->when_drag_end = mouse_arrow_up;
    bottom_hbox->z_index += 1;
    
    return scrollpane;
}

void combobox_key_event(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                        char string[64],
                        uint16_t mods, xkb_key_direction direction) {
    if (!self->active)
        return;
    if (is_string) {
        // append to text
    } else {
        // handle backspace
        
    }
    printf("is_string: %b\n", is_string);
    printf("keysym: %d\n", keysym);
    printf("string: %s\n", string);
    printf("mods: %d\n", mods);
    printf("direction: %d\n", direction);
}

void combobox_popup_pressed(AppClient *client, cairo_t *cr, Container *self) {
    client_close_threaded(client->app, client);
}

struct ComboboxPopupData : UserData {
    Container *parent;
};

void combobox_popup_key_event(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                              char string[64],
                              uint16_t mods, xkb_key_direction direction) {
    auto c = (ComboboxPopupData *) self->user_data;
    if (c->parent->when_key_event)
        c->parent->when_key_event(client, cr, c->parent, is_string, keysym, string, mods, direction);
}

void combobox_pressed(AppClient *client, cairo_t *cr, Container *self) {
    Settings settings;
    PopupSettings popup_settings;
    popup_settings.name = client->name + "_combobox_popup";
    popup_settings.ignore_scroll = true;
    popup_settings.takes_input_focus = true;
    popup_settings.transparent_mouse_grab = false;
    auto menu = client->create_popup(popup_settings, settings);
    menu->root->when_mouse_down = combobox_popup_pressed;
    menu->root->when_key_event = combobox_popup_key_event;
    menu->root->user_data = new ComboboxPopupData;
    ((ComboboxPopupData *) menu->root->user_data)->parent = self;
//    menu->root->user_data = self;
    client_show(client->app, menu);
}

Container *make_combobox(Container *parent, const std::vector<std::string> &items) {
    auto *combobox = parent->child(::vbox, FILL_SPACE, FILL_SPACE);
    
    // instead of re-using textarea, just make a new simple area meant just for combobox
    combobox->when_key_event = combobox_key_event;
    combobox->when_paint = paint_show;
    combobox->when_mouse_down = combobox_pressed;
    
    return combobox;
}

static void
update_preffered_x(AppClient *client, Container *textarea) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TextAreaData *) textarea->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, textarea->real_bounds.w * PANGO_SCALE);
    }
    
    PangoRectangle strong_pos;
    PangoRectangle weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &strong_pos, &weak_pos);
    
    data->state->preferred_x = strong_pos.x;
}

static void
put_cursor_on_screen(AppClient *client, Container *textarea) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TextAreaData *) textarea->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, textarea->real_bounds.w * PANGO_SCALE);
    }
    
    PangoRectangle strong_pos;
    PangoRectangle weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &strong_pos, &weak_pos);
    
    Container *content_area = textarea->parent;
    
    Bounds view_bounds = Bounds(-content_area->scroll_h_real,
                                -content_area->scroll_v_real,
                                content_area->real_bounds.w,
                                content_area->real_bounds.h);
    
    int x_pos = strong_pos.x / PANGO_SCALE;
    if (x_pos < view_bounds.x) {
        content_area->scroll_h_real = -(x_pos - scroll_amount);
    } else if (x_pos > view_bounds.x + view_bounds.w) {
        content_area->scroll_h_real = -(x_pos - content_area->real_bounds.w + scroll_amount);
    }
    int y_pos = strong_pos.y / PANGO_SCALE;
    if (y_pos < view_bounds.y) {
        content_area->scroll_v_real = -(y_pos - scroll_amount);
    } else if (y_pos + strong_pos.height / PANGO_SCALE > (view_bounds.y + view_bounds.h)) {
        content_area->scroll_v_real =
                -(y_pos + strong_pos.height / PANGO_SCALE - content_area->real_bounds.h);
    }
    
    // ::layout(content_area, content_area->real_bounds, false);
    client_layout(client->app, client);
    
    client_create_animation(client->app,
                            client,
                            &content_area->scroll_h_visual, content_area->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            content_area->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &content_area->scroll_v_visual, content_area->lifetime, 0,
                            scroll_anim_time,
                            easing_function,
                            content_area->scroll_v_real,
                            true);
}

static void
update_bounds(AppClient *client, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TextAreaData *) container->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(layout, &text_ink, &text_logical);
    
    int width = text_logical.width / PANGO_SCALE;
    int height = text_logical.height / PANGO_SCALE;
    
    if (data->wrap) {
        if (container->real_bounds.h != height) {
            container->wanted_bounds.h = height;
            // ::layout(container->parent->parent, container->parent->parent->real_bounds, false);
            client_layout(client->app, client);
    
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_h_visual, container->parent->parent->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_h_real,
                                    true);
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_v_visual, container->parent->parent->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_v_real,
                                    true);
            
            request_refresh(client->app, client);
        }
    } else {
        if (container->real_bounds.h != height || container->wanted_bounds.w != width) {
            container->wanted_bounds.w = width;
            container->wanted_bounds.h = height;
            client_layout(client->app, client);
            // ::layout(container->parent->parent, container->parent->parent->real_bounds, false);
    
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_h_visual, container->parent->parent->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_h_real,
                                    true);
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_v_visual, container->parent->parent->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_v_real,
                                    true);
            
            request_refresh(client->app, client);
        }
    }
}

static void
paint_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TextAreaData *) container->user_data;
    if (data->state->first_bounds_update) {
        update_bounds(client, container);
        data->state->first_bounds_update = false;
    }
    
    cairo_save(cr);
    
    // DEBUG
    // paint_show(client, cr, container);
    
    // TEXT
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_width(layout, -1);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_LEFT);
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    set_rect(cr, container->parent->real_bounds);
    cairo_clip(cr);
    
    PangoRectangle cursor_strong_pos;
    PangoRectangle cursor_weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &cursor_strong_pos, &cursor_weak_pos);
    
    // SELECTION BACKGROUND
    if (data->state->selection_x != -1) {
        set_argb(cr, ArgbColor(.2, .5, .8, 1));
        PangoRectangle selection_strong_pos;
        PangoRectangle selection_weak_pos;
        pango_layout_get_cursor_pos(
                layout, data->state->selection_x, &selection_strong_pos, &selection_weak_pos);
        
        bool cursor_first = false;
        if (cursor_strong_pos.y == selection_strong_pos.y) {
            if (cursor_strong_pos.x < selection_strong_pos.x) {
                cursor_first = true;
            }
        } else if (cursor_strong_pos.y < selection_strong_pos.y) {
            cursor_first = true;
        }
        
        double w = std::max(container->real_bounds.w, container->parent->real_bounds.w);
        
        double minx = std::min(selection_strong_pos.x, cursor_strong_pos.x) / PANGO_SCALE;
        double miny = std::min(selection_strong_pos.y, cursor_strong_pos.y) / PANGO_SCALE;
        double maxx = std::max(selection_strong_pos.x, cursor_strong_pos.x) / PANGO_SCALE;
        double maxy = std::max(selection_strong_pos.y, cursor_strong_pos.y) / PANGO_SCALE;
        double h = selection_strong_pos.height / PANGO_SCALE;
        
        if (maxy == miny) {// Same line
            cairo_rectangle(
                    cr, container->real_bounds.x + minx, container->real_bounds.y + miny, maxx - minx, h);
            cairo_fill(cr);
        } else {
            if ((maxy - miny) > h) {// More than one line off difference
                cairo_rectangle(cr,
                                container->real_bounds.x,
                                container->real_bounds.y + miny + h,
                                w,
                                maxy - miny - h);
            }
            // If the y's aren't on the same line then we always draw the two rects
            // for when there's a one line diff
            
            if (cursor_first) {
                // Top line
                cairo_rectangle(cr,
                                container->real_bounds.x + cursor_strong_pos.x / PANGO_SCALE,
                                container->real_bounds.y + cursor_strong_pos.y / PANGO_SCALE,
                                w,
                                h);
                
                // Bottom line
                int bottom_width = selection_strong_pos.x / PANGO_SCALE;
                cairo_rectangle(cr,
                                container->real_bounds.x,
                                container->real_bounds.y + selection_strong_pos.y / PANGO_SCALE,
                                bottom_width,
                                h);
            } else {
                // Top line
                cairo_rectangle(cr,
                                container->real_bounds.x + selection_strong_pos.x / PANGO_SCALE,
                                container->real_bounds.y + selection_strong_pos.y / PANGO_SCALE,
                                w,
                                h);
                
                // Bottom line
                int bottom_width = cursor_strong_pos.x / PANGO_SCALE;
                cairo_rectangle(cr,
                                container->real_bounds.x,
                                container->real_bounds.y + cursor_strong_pos.y / PANGO_SCALE,
                                bottom_width,
                                h);
            }
            cairo_fill(cr);
        }
    }// END Selection background
    
    // SHOW TEXT LAYOUT
    set_argb(cr, data->color);
    
    cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_LEFT);
    
    if (container->parent->active == false && data->state->text.empty()) {
        cairo_save(cr);
        
        PangoLayout *prompt_layout = get_cached_pango_font(
                client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        pango_layout_set_width(prompt_layout, -1);
        pango_layout_set_text(
                prompt_layout, data->state->prompt.c_str(), data->state->prompt.length());
        pango_layout_set_alignment(prompt_layout, PangoAlignment::PANGO_ALIGN_LEFT);
        if (data->wrap) {
            pango_layout_set_wrap(prompt_layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_width(prompt_layout, container->real_bounds.w * PANGO_SCALE);
        }
        
        set_rect(cr, container->parent->real_bounds);
        cairo_clip(cr);
        
        set_argb(cr, data->color_prompt);
        
        cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y);
        pango_cairo_show_layout(cr, prompt_layout);
        cairo_restore(cr);
    }
    
    // PAINT CURSOR
    if (container->parent->active && data->state->cursor_on) {
        set_argb(cr, data->color_cursor);
        int offset = cursor_strong_pos.x != 0 ? -1 : 0;
        cairo_rectangle(cr,
                        cursor_strong_pos.x / PANGO_SCALE + container->real_bounds.x + offset,
                        cursor_strong_pos.y / PANGO_SCALE + container->real_bounds.y,
                        data->cursor_width,
                        cursor_strong_pos.height / PANGO_SCALE);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void
move_cursor(TextAreaData *data, int byte_index, bool increase_selection) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (increase_selection) {
        if (data->state->selection_x == -1) {
            data->state->selection_x = data->state->cursor;
        }
    } else {
        data->state->selection_x = -1;
    }
    data->state->cursor = byte_index;
}

static void
clicked_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    
    if ((get_current_time_in_ms() - data->state->last_time_mouse_press) < 220) {
        data->state->cursor = data->state->text.size();
        data->state->selection_x = 0;
        put_cursor_on_screen(client, container);
        return;
    }
    data->state->last_time_mouse_press = get_current_time_in_ms();
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, data->color);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    int index;
    int trailing;
    int x = client->mouse_current_x - container->real_bounds.x;
    int y = client->mouse_current_y - container->real_bounds.y;
    bool inside =
            pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing);
    
    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);
    
    bool shift = reply->mods & XKB_KEY_Shift_L;
    
    move_cursor(data, index + trailing, shift);
    update_preffered_x(client, container);
}

static void
drag_timeout(App *app, AppClient *client, Timeout *, void *data) {
    auto *container = (Container *) data;
    
    Container *content_area = container->parent;
    
    blink_on(client->app, client, container);
    
    if (dragging.load()) {
        auto *data = (TextAreaData *) container->user_data;
        
        PangoLayout *layout = get_cached_pango_font(
                client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
        if (data->wrap) {
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
        }
        
        int index;
        int trailing;
        int lx = client->mouse_current_x - container->real_bounds.x;
        int ly = client->mouse_current_y - container->real_bounds.y;
        bool inside = pango_layout_xy_to_index(
                layout, lx * PANGO_SCALE, ly * PANGO_SCALE, &index, &trailing);
        
        Bounds bounds = container->parent->real_bounds;
        int x = client->mouse_current_x;
        int y = client->mouse_current_y;
        
        bool modified_x = false;
        bool modified_y = false;
        
        if (x < bounds.x) {// off the left side
            modified_x = true;
            double multiplier =
                    std::min((double) scroll_amount * 3, bounds.x - x) / scroll_amount;
            content_area->scroll_h_real += scroll_amount * multiplier;
        }
        if (x > bounds.x + bounds.w) {// off the right side
            modified_x = true;
            double multiplier =
                    std::min((double) scroll_amount * 3, x - (bounds.x + bounds.w)) / scroll_amount;
            content_area->scroll_h_real -= scroll_amount * multiplier;
        }
        if (y < bounds.y) {// off the top
            modified_y = true;
            double multiplier =
                    std::min((double) scroll_amount * 3, bounds.y - y) / scroll_amount;
            content_area->scroll_v_real += scroll_amount * multiplier;
        }
        if (y > bounds.y + bounds.h) {// off the bottom
            modified_y = true;
            double multiplier =
                    std::min((double) scroll_amount * 3, y - (bounds.y + bounds.h)) / scroll_amount;
            content_area->scroll_v_real -= scroll_amount * multiplier;
        }
        
        // ::layout(content_area, content_area->real_bounds, false);
        client_layout(client->app, client);
        
        if (modified_x)
            client_create_animation(client->app,
                                    client,
                                    &content_area->scroll_h_visual, content_area->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    content_area->scroll_h_real,
                                    true);
        if (modified_y)
            client_create_animation(client->app,
                                    client,
                                    &content_area->scroll_v_visual, content_area->lifetime, 0,
                                    scroll_anim_time,
                                    easing_function,
                                    content_area->scroll_v_real,
                                    true);
        
        app_timeout_create(client->app, client, scroll_anim_time, drag_timeout, container, const_cast<char *>(__PRETTY_FUNCTION__));
    } else {
        request_refresh(app, client);
    }
}

static void
drag_start_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, data->color);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    int index;
    int trailing;
    int x = client->mouse_current_x - container->real_bounds.x;
    int y = client->mouse_current_y - container->real_bounds.y;
    bool inside =
            pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing);
    
    dragging = true;
    app_timeout_create(client->app, client, 0, drag_timeout, container, const_cast<char *>(__PRETTY_FUNCTION__));
    blink_on(client->app, client, container);
    
    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);
    
    bool shift = reply->mods & XKB_KEY_Shift_L;
    
    move_cursor(data, index + trailing, shift);
}

static void
mouse_down_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, data->color);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    int index;
    int trailing;
    int x = client->mouse_initial_x - container->real_bounds.x;
    int y = client->mouse_initial_y - container->real_bounds.y;
    bool inside =
            pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing);
    
    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);
    
    bool shift = reply->mods & XKB_KEY_Shift_L;
    
    move_cursor(data, index + trailing, shift);
}

static void
drag_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, data->color);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    int index;
    int trailing;
    int x = client->mouse_current_x - container->real_bounds.x;
    int y = client->mouse_current_y - container->real_bounds.y;
    bool inside =
            pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing);
    
    move_cursor(data, index + trailing, true);
}

static void
drag_end_textarea(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    set_argb(cr, data->color);
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    
    int index;
    int trailing;
    int x = client->mouse_current_x - container->real_bounds.x;
    int y = client->mouse_current_y - container->real_bounds.y;
    bool inside =
            pango_layout_xy_to_index(layout, x * PANGO_SCALE, y * PANGO_SCALE, &index, &trailing);
    
    move_cursor(data, index + trailing, true);
    
    dragging = false;
}

static void
textarea_key_release(AppClient *client,
                     cairo_t *cr,
                     Container *container,
                     bool is_string, xkb_keysym_t keysym, char string[64],
                     uint16_t mods,
                     xkb_key_direction direction);

static void
textarea_active_status_changed(AppClient *client, cairo_t *cr, Container *container) {
    container = container->children[0];
    auto *data = (TextAreaData *) container->user_data;
    if (!container->active) {
        data->state->selection_x = data->state->cursor;
    }
}

Container *
make_textarea(App *app, AppClient *client, Container *parent, TextAreaSettings settings) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    Container *content_area = make_scrollpane(parent, settings);
    content_area->wanted_pad = settings.pad;
    
    int width = 0;
    if (settings.wrap)
        width = FILL_SPACE;
    Container *textarea = content_area->child(::vbox, width, settings.font_size);
    textarea->wanted_pad = settings.pad;
    
    textarea->when_paint = paint_textarea;
    textarea->when_key_event = textarea_key_release;
    content_area->when_drag_end_is_click = false;
    content_area->when_drag_start = drag_start_textarea;
    content_area->when_drag = drag_textarea;
    content_area->when_drag_end = drag_end_textarea;
    content_area->when_clicked = clicked_textarea;
    content_area->when_mouse_down = mouse_down_textarea;
    content_area->when_active_status_changed = textarea_active_status_changed;
    
    auto data = new TextAreaData();
    data->cursor_width = settings.cursor_width;
    data->color_cursor = settings.color_cursor;
    data->font_size = settings.font_size;
    data->font = settings.font;
    data->color = settings.color;
    data->single_line = settings.single_line;
    data->wrap = settings.wrap;
    data->state->prompt = settings.prompt;
    data->color_prompt = settings.color_prompt;
    data->text_alignment = settings.text_alignment;
    data->prompt_alignment = settings.prompt_alignment;
    textarea->user_data = data;
    
    scroll_amount = data->font_size * 3;
    
    blink_loop(app, client, nullptr, textarea);
    
    return textarea;
}

void
insert_action(AppClient *client, Container *textarea, TextAreaData *data, std::string text) {
    // Try to merge with the previous
    bool merged = false;
    
    if (!data->state->undo_stack.empty()) {
        UndoAction *previous_action = data->state->undo_stack.back();
        
        if (previous_action->type == UndoType::INSERT) {
            if (previous_action->cursor_end == data->state->cursor) {
                char last_char = previous_action->inserted_text.back();
                
                bool text_is_split_token = text == " " || text == "\n" || text == "\r";
                
                if (text_is_split_token && last_char == text.back() && text.size() == 1) {
                    previous_action->inserted_text.append(text);
                    previous_action->cursor_end += text.size();
                    merged = true;
                } else {
                    if (!text_is_split_token) {
                        previous_action->inserted_text.append(text);
                        previous_action->cursor_end += text.size();
                        merged = true;
                    }
                }
            } else {
                auto undo_action = new UndoAction;
                undo_action->type = UndoType::CURSOR;
                undo_action->cursor_start = previous_action->cursor_end;
                undo_action->cursor_end = data->state->cursor;
                data->state->undo_stack.push_back(undo_action);
            }
        }
    }
    
    if (!merged) {
        auto undo_action = new UndoAction;
        undo_action->type = UndoType::INSERT;
        
        undo_action->inserted_text = text;
        undo_action->cursor_start = data->state->cursor;
        undo_action->cursor_end = data->state->cursor + text.size();
        data->state->undo_stack.push_back(undo_action);
    }
    
    data->state->text.insert(data->state->cursor, text);
    move_cursor(data, data->state->cursor + text.size(), false);
    update_preffered_x(client, textarea);
    update_bounds(client, textarea);
    put_cursor_on_screen(client, textarea);
    
    data->state->redo_stack.clear();
    data->state->redo_stack.shrink_to_fit();
}

static void
delete_action(AppClient *client, Container *textarea, TextAreaData *data, int amount) {
    auto undo_action = new UndoAction;
    undo_action->type = UndoType::DELETE;
    
    if (amount > 0) {
        undo_action->replaced_text = data->state->text.substr(data->state->cursor, amount);
        undo_action->cursor_start = data->state->cursor;
        undo_action->cursor_end = data->state->cursor;
        
        data->state->text.erase(data->state->cursor, amount);
    } else {
        undo_action->replaced_text =
                data->state->text.substr(data->state->cursor + amount, -amount);
        undo_action->cursor_start = data->state->cursor;
        undo_action->cursor_end = data->state->cursor + amount;
        
        data->state->text.erase(data->state->cursor + amount, -amount);
    }
    data->state->undo_stack.push_back(undo_action);
    
    move_cursor(data, undo_action->cursor_end, false);
    update_preffered_x(client, textarea);
    update_bounds(client, textarea);
    put_cursor_on_screen(client, textarea);
}

static void
replace_action(AppClient *client, Container *textarea, TextAreaData *data, std::string text) {
    auto undo_action = new UndoAction;
    undo_action->type = UndoType::REPLACE;
    
    int min_pos = std::min(data->state->cursor, data->state->selection_x);
    int max_pos = std::max(data->state->cursor, data->state->selection_x);
    
    undo_action->inserted_text = text;
    undo_action->replaced_text = data->state->text.substr(min_pos, max_pos - min_pos);
    
    undo_action->cursor_start = data->state->cursor;
    undo_action->cursor_end = min_pos + text.size();
    undo_action->selection_start = data->state->selection_x;
    undo_action->selection_end = -1;
    data->state->undo_stack.push_back(undo_action);
    
    data->state->text.erase(min_pos, max_pos - min_pos);
    data->state->text.insert(min_pos, text);
    
    move_cursor(data, undo_action->cursor_end, false);
    update_preffered_x(client, textarea);
    update_bounds(client, textarea);
    put_cursor_on_screen(client, textarea);
}

char tokens_list[] = {'[', ']', '|', '(', ')', '{', '}', ';', '.', '!', '@',
                      '#', '$', '%', '^', '&', '*', '-', '=', '+', ':', '\'',
                      '\'', '<', '>', '?', '|', '\\', '/', ',', '`', '~', '\t'};

enum motion {
    left = 0,
    right = 1,
};

enum group {
    none = 0,
    space = 1,
    newline = 2,
    token = 3,
    normal = 4,
};

class Seeker {
public:
    TextState *state;
    
    int start_pos;
    int current_pos;
    
    group starting_group_to_left;
    group starting_group_to_right;
    
    bool same_token_type_to_left_and_right;// we are always inside a group but this means that to
    // either side is the same group type
    bool different_token_type_to_atleast_one_side;// at the end of a group
    
    group group_at(int pos) {
        if (pos < 0 || pos >= state->text.size()) {
            return group::none;
        }
        char c = state->text.at(pos);
        if (c == ' ')
            return group::space;
        if (c == '\n')
            return group::newline;
        for (auto token: tokens_list)
            if (token == c)
                return group::token;
        return group::normal;
    }
    
    group group_to(motion direction) {
        int off = 0;
        if (direction == motion::right)
            off = 1;
        else if (direction == motion::left)
            off = -1;
        return group_at(current_pos + off);
    }
    
    group seek_until_different_token(motion direction, int off) {
        // right now we are going to ignore the first character completely
        if (direction == motion::left) {
            while ((current_pos - off) >= 0) {
                group new_group = group_at(current_pos - off);
                if (new_group != starting_group_to_right) {
                    return new_group;
                }
                current_pos -= 1;
            }
        } else if (direction == motion::right) {
            while ((current_pos + off) < state->text.size()) {
                group new_group = group_at(current_pos + off);
                if (new_group != starting_group_to_right) {
                    return new_group;
                }
                current_pos += 1;
            }
        }
        return group::none;
    }
    
    group seek_until_right_before_different_token(motion direction) {
        return seek_until_different_token(direction, 1);
    }
    
    group seek_and_cover_different_token(motion direction) {
        return seek_until_different_token(direction, 0);
    }
    
    bool seek_until_specific_token(motion direction, group specific_token) {
        group active_group;
        while ((active_group = group_to(direction)) != group::none) {
            if (active_group == specific_token) {
                return true;
            }
            current_pos += direction == motion::left ? -1 : 1;
        }
        return false;
    }
    
    Seeker(TextState *state) {
        this->state = state;
        
        start_pos = state->cursor;
        current_pos = start_pos;
        
        starting_group_to_left = group_at(start_pos - 1);
        starting_group_to_right = group_at(start_pos);
        
        same_token_type_to_left_and_right = starting_group_to_right == starting_group_to_left;
        different_token_type_to_atleast_one_side = !same_token_type_to_left_and_right;
    }
};

static void
go_to_edge(Seeker &seeker, motion motion_direction) {
    if (motion_direction == motion::left) {
        seeker.seek_until_right_before_different_token(motion_direction);
    } else if (motion_direction == motion::right) {
        seeker.seek_and_cover_different_token(motion_direction);
    }
}

static int
seek_token(TextState *state, motion motion_direction) {
    Seeker seeker(state);
    
    if (seeker.same_token_type_to_left_and_right) {
        go_to_edge(seeker, motion_direction);
        if (seeker.starting_group_to_right == group::space) {
            seeker.starting_group_to_right = seeker.starting_group_to_left =
                    seeker.group_to(motion_direction);
            go_to_edge(seeker, motion_direction);
        }
    } else if (seeker.different_token_type_to_atleast_one_side) {
        if (motion_direction == motion::left) {
            seeker.starting_group_to_right = seeker.starting_group_to_left =
                    seeker.group_to(motion_direction);
            go_to_edge(seeker, motion_direction);
            if (seeker.starting_group_to_right == group::space) {
                seeker.starting_group_to_right = seeker.starting_group_to_left =
                        seeker.group_to(motion_direction);
                go_to_edge(seeker, motion_direction);
            }
        } else if (motion_direction == motion::right) {
            go_to_edge(seeker, motion_direction);
            if (seeker.starting_group_to_right == group::space) {
                seeker.starting_group_to_right = seeker.starting_group_to_left =
                        seeker.group_to(motion_direction);
                go_to_edge(seeker, motion_direction);
            }
        }
    }
    
    return seeker.current_pos;
}

static void
move_vertically_lines(AppClient *client,
                      TextAreaData *data,
                      Container *textarea,
                      bool shift,
                      int multiplier) {
    PangoLayout *layout = get_cached_pango_font(
            client->cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, textarea->real_bounds.w * PANGO_SCALE);
    }
    
    PangoRectangle strong_pos;
    PangoRectangle weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &strong_pos, &weak_pos);
    
    PangoLayoutLine *line = pango_layout_get_line(layout, 0);
    PangoRectangle ink_rect;
    PangoRectangle logical_rect;
    pango_layout_line_get_extents(line, &ink_rect, &logical_rect);
    
    int index;
    int trailing;
    int x = data->state->preferred_x;
    int y = strong_pos.y + (logical_rect.height * multiplier);
    bool inside = pango_layout_xy_to_index(layout, x, y, &index, &trailing);
    move_cursor(data, index + trailing, shift);
    put_cursor_on_screen(client, textarea);
}

void
textarea_handle_keypress(AppClient *client, Container *textarea, bool is_string, xkb_keysym_t keysym, char string[64],
                         uint16_t mods, xkb_key_direction direction) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (direction == XKB_KEY_UP) {
        return;
    }
    auto *data = (TextAreaData *) textarea->user_data;
    data->state->last_time_key_press = get_current_time_in_ms();
    data->state->cursor_on = true;
    
    blink_on(client->app, client, textarea);
    
    bool shift = false;
    bool control = false;
    if (mods & XCB_MOD_MASK_SHIFT) {
        shift = true;
    }
    if (mods & XCB_MOD_MASK_CONTROL) {
        control = true;
    }
    
    if (is_string) {
        if (data->state->selection_x != -1) {
            replace_action(client, textarea, data, string);
        } else {
            insert_action(client, textarea, data, string);
        }
    } else {
        if (keysym == XKB_KEY_BackSpace) {
            if (data->state->selection_x != -1) {
                replace_action(client, textarea, data, "");
            } else {
                if (control) {
                    int jump_target = seek_token(data->state, motion::left);
                    long absolute_distance = std::abs(data->state->cursor - jump_target);
                    if (absolute_distance != 0) {
                        delete_action(client, textarea, data, -absolute_distance);
                    }
                } else {
                    if (!data->state->text.empty() && data->state->cursor > 0) {
                        delete_action(client, textarea, data, -1);
                    }
                }
            }
        } else if (keysym == XKB_KEY_Delete) {
            if (data->state->selection_x != -1) {
                replace_action(client, textarea, data, "");
            } else {
                if (control) {
                    int jump_target = seek_token(data->state, motion::right);
                    long absolute_distance = std::abs(data->state->cursor - jump_target);
                    if (absolute_distance != 0) {
                        delete_action(client, textarea, data, absolute_distance);
                    }
                } else {
                    if (data->state->cursor < data->state->text.size()) {
                        delete_action(client, textarea, data, 1);
                    }
                }
            }
        } else if (keysym == XKB_KEY_Escape) {
            move_cursor(data, data->state->cursor, false);
        } else if (keysym == XKB_KEY_Return) {
            if (!data->single_line) {
                if (data->state->selection_x != -1) {
                    replace_action(client, textarea, data, "\n");
                } else {
                    insert_action(client, textarea, data, "\n");
                }
            }
        } else if (keysym == XKB_KEY_Tab) {
            if (data->state->selection_x != -1) {
                replace_action(client, textarea, data, "\t");
            } else {
                insert_action(client, textarea, data, "\t");
            }
        } else if (keysym == XKB_KEY_a) {
            if (control) {
                data->state->cursor = data->state->text.size();
                data->state->selection_x = 0;
                put_cursor_on_screen(client, textarea);
                
            }
        } else if (keysym == XKB_KEY_z) {
            if (control) {
                // undo
                if (!data->state->undo_stack.empty()) {
                    UndoAction *action = data->state->undo_stack.back();
                    data->state->undo_stack.pop_back();
                    data->state->redo_stack.push_back(action);
                    
                    if (action->type == UndoType::INSERT) {
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        std::string text = action->inserted_text;
                        
                        data->state->text.erase(cursor_start, text.size());
                        
                        data->state->cursor = cursor_start;
                        data->state->selection_x = -1;
                    } else if (action->type == UndoType::DELETE) {
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        
                        int min = std::min(cursor_start, cursor_end);
                        int max = std::max(cursor_start, cursor_end);
                        
                        std::string text = action->replaced_text;
                        
                        data->state->text.insert(min, text);
                        
                        data->state->cursor = cursor_start;
                        data->state->selection_x = -1;
                    } else if (action->type == UndoType::REPLACE) {
                        // undo a replace
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        int selection_start = action->selection_start;
                        int selection_end = action->selection_end;
                        std::string replaced = action->replaced_text;
                        std::string inserted = action->inserted_text;
                        
                        data->state->text.erase(cursor_end - inserted.size(),
                                                inserted.size());
                        data->state->text.insert(cursor_end - inserted.size(), replaced);
                        
                        data->state->cursor = cursor_start;
                        data->state->selection_x = selection_start;
                    } else if (action->type == UndoType::CURSOR) {
                        data->state->cursor = action->cursor_start;
                    }
                }
                update_preffered_x(client, textarea);
                update_bounds(client, textarea);
                put_cursor_on_screen(client, textarea);
            }
        } else if (keysym == XKB_KEY_Z) {
            if (control) {
                // redo
                if (!data->state->redo_stack.empty()) {
                    UndoAction *action = data->state->redo_stack.back();
                    data->state->redo_stack.pop_back();
                    data->state->undo_stack.push_back(action);
                    
                    if (action->type == UndoType::INSERT) {
                        // do an insert
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        std::string text = action->inserted_text;
                        
                        data->state->text.insert(cursor_start, text);
                        
                        data->state->cursor = cursor_end;
                        data->state->selection_x = -1;
                    } else if (action->type == UndoType::DELETE) {
                        // do a delete
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        
                        int min = std::min(cursor_start, cursor_end);
                        int max = std::max(cursor_start, cursor_end);
                        
                        std::string text = action->replaced_text;
                        
                        data->state->text.erase(min, text.size());
                        
                        data->state->cursor = cursor_end;
                        data->state->selection_x = -1;
                    } else if (action->type == UndoType::REPLACE) {
                        // do a replace
                        int cursor_start = action->cursor_start;
                        int cursor_end = action->cursor_end;
                        int selection_start = action->selection_start;
                        int selection_end = action->selection_end;
                        std::string replaced = action->replaced_text;
                        std::string inserted = action->inserted_text;
                        
                        int min = std::min(cursor_start, selection_start);
                        int max = std::max(cursor_start, selection_start);
                        data->state->text.erase(min, max - min);
                        data->state->text.insert(min, inserted);
                        
                        data->state->cursor = cursor_end;
                        data->state->selection_x = -1;
                    } else if (action->type == UndoType::CURSOR) {
                        data->state->cursor = action->cursor_end;
                    }
                }
                update_preffered_x(client, textarea);
                update_bounds(client, textarea);
                put_cursor_on_screen(client, textarea);
            }
        } else if (keysym == XKB_KEY_c) {
            if (control) {
            
            }
        } else if (keysym == XKB_KEY_v) {
            std::string text = clipboard();
            if (data->single_line) {
                text.erase(std::remove_if(text.begin(), text.end(), [](char c) {
                    return c == '\n' || c == '\r';
                }), text.end());
            }
            if (data->state->selection_x != -1) {
                replace_action(client, textarea, data, text);
            } else {
                insert_action(client, textarea, data, text);
            }
        } else if (keysym == XKB_KEY_Home) {
            Seeker seeker(data->state);
            
            seeker.seek_until_specific_token(motion::left, group::newline);
            
            move_cursor(data, seeker.current_pos, shift);
            update_preffered_x(client, textarea);
            put_cursor_on_screen(client, textarea);
        } else if (keysym == XKB_KEY_End) {
            Seeker seeker(data->state);
            
            seeker.current_pos -= 1;
            seeker.seek_until_specific_token(motion::right, group::newline);
            
            move_cursor(data, seeker.current_pos + 1, shift);
            update_preffered_x(client, textarea);
            put_cursor_on_screen(client, textarea);
        } else if (keysym == XKB_KEY_Page_Up) {
            move_vertically_lines(client, data, textarea, shift, -10);
        } else if (keysym == XKB_KEY_Page_Down) {
            move_vertically_lines(client, data, textarea, shift, 10);
        } else if (keysym == XKB_KEY_Left) {
            if (control) {
                int jump_target = seek_token(data->state, motion::left);
                move_cursor(data, jump_target, shift);
                update_preffered_x(client, textarea);
                put_cursor_on_screen(client, textarea);
            } else {
                int cursor_target = data->state->cursor;
                cursor_target -= 1;
                if (cursor_target < 0) {
                    cursor_target = 0;
                }
                move_cursor(data, cursor_target, shift);
                update_preffered_x(client, textarea);
                put_cursor_on_screen(client, textarea);
            }
        } else if (keysym == XKB_KEY_Right) {
            if (control) {
                int jump_target = seek_token(data->state, motion::right);
                move_cursor(data, jump_target, shift);
                update_preffered_x(client, textarea);
                put_cursor_on_screen(client, textarea);
            } else {
                int cursor_target = data->state->cursor + 1;
                if (cursor_target > data->state->text.size()) {
                    cursor_target = data->state->text.size();
                }
                move_cursor(data, cursor_target, shift);
                update_preffered_x(client, textarea);
                put_cursor_on_screen(client, textarea);
            }
        } else if (keysym == XKB_KEY_Up) {
            move_vertically_lines(client, data, textarea, shift, -1);
        } else if (keysym == XKB_KEY_Down) {
            move_vertically_lines(client, data, textarea, shift, 1);
        }
    }
}
static void
textarea_key_release(AppClient *client,
                     cairo_t *cr,
                     Container *container,
                     bool is_string, xkb_keysym_t keysym, char string[64],
                     uint16_t mods,
                     xkb_key_direction direction) {
    if (direction == XKB_KEY_UP) {
        return;
    }
    if (container->parent->active || container->active) {
        textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
    }
}

void key_event_textfield(AppClient *client, cairo_t *cr, Container *container, bool is_string, xkb_keysym_t keysym,
                         char *string, uint16_t mods, xkb_key_direction direction) {
    if (!container->active)
        return;
    auto *data = (FieldData *) container->user_data;
    if (direction == XKB_KEY_UP)
        return;
    if (is_string) {
        if (data->settings.only_numbers) {
            if (string[0] >= '0' && string[0] <= '9') {
                data->text += string[0];
            }
        } else {
            data->text += string;
        }
        if (data->settings.max_size != -1) {
            data->text = (data->text.length() > data->settings.max_size) ? data->text.substr(0, data->settings.max_size)
                                                                         : data->text;
        }
    } else {
        if (keysym == XKB_KEY_BackSpace) {
            if (!data->text.empty()) {
                data->text.pop_back();
            }
        }
    }
}

#define CURSOR_BLINK_ON_TIME 530
#define CURSOR_BLINK_OFF_TIME 430

void
blink_on(App *app, AppClient *client, void *textarea) {
    auto *container = (Container *) textarea;
    auto *data = (TextAreaData *) container->user_data;
    if (data->state->timeout_alive.lock()) {
        app_timeout_replace(app, client, data->state->cursor_blink,
                            CURSOR_BLINK_ON_TIME, blink_loop, textarea);
    } else {
        data->state->cursor_blink = app_timeout_create(app, client, CURSOR_BLINK_ON_TIME, blink_loop, textarea,
                                                       const_cast<char *>(__PRETTY_FUNCTION__));
        data->state->timeout_alive = data->state->cursor_blink->lifetime;
    }
    data->state->cursor_on = true;
    request_refresh(app, client);
}

void
blink_loop(App *app, AppClient *client, Timeout *, void *textarea) {
    auto *container = (Container *) textarea;
    if (!container->active) return;
    auto *data = (TextAreaData *) container->user_data;
    
    float cursor_blink_time = data->state->cursor_on ? CURSOR_BLINK_OFF_TIME : CURSOR_BLINK_ON_TIME;
    
    data->state->cursor_blink = app_timeout_create(app, client, cursor_blink_time, blink_loop, textarea, const_cast<char *>(__PRETTY_FUNCTION__));
    
    data->state->cursor_on = !data->state->cursor_on;
    request_refresh(app, client);
}

class TransitionData : public UserData {
public:
    Container *first = nullptr;
    cairo_surface_t *original_surface = nullptr;
    cairo_t *original_cr = nullptr;
    
    Container *second = nullptr;
    cairo_surface_t *replacement_surface = nullptr;
    cairo_t *replacement_cr = nullptr;
    
    double transition_scalar = 0;
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
    
    int original_anim = 0;
    int replacement_anim = 0;
};

void paint_default(AppClient *client, cairo_t *cr, Container *container,
                   TransitionData *data, cairo_surface_t *surface) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_set_source_surface(cr, surface, container->real_bounds.x, container->real_bounds.y);
    cairo_paint(cr);
}


void paint_transition_scaled(AppClient *client, cairo_t *cr, Container *container,
                             TransitionData *data, cairo_surface_t *surface, double scale_amount) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double translate_x = container->real_bounds.x;
    double translate_y = container->real_bounds.y;
    if (scale_amount < 1) {
        // to x and y we will add
        translate_x += (container->real_bounds.w / 2) * (1 - scale_amount);
        translate_y += (container->real_bounds.h / 2) * (1 - scale_amount);
    } else {
        // to x and y we will subtract
        translate_x -= (container->real_bounds.w / 2) * (scale_amount - 1);
        translate_y -= (container->real_bounds.h / 2) * (scale_amount - 1);
    }
    
    if (scale_amount == 0) {
        // If you try passing cairo_scale "0" it breaks everything and it was VERY hard to debug that
        return;
    }
    
    cairo_save(cr);
    cairo_translate(cr, translate_x, translate_y);
    cairo_scale(cr, scale_amount, scale_amount);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    cairo_paint(cr);
    cairo_restore(cr);
}

void paint_default_to_squashed(AppClient *client, cairo_t *cr, Container *container,
                               TransitionData *data, cairo_surface_t *surface) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double start = 1;
    double target = .5;
    double total_diff = target - start;
    double scale_amount = start + (total_diff * data->transition_scalar);
    
    paint_transition_scaled(client, cr, container, data, surface, scale_amount);
}

void paint_squashed_to_default(AppClient *client, cairo_t *cr, Container *container,
                               TransitionData *data, cairo_surface_t *surface) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double start = .5;
    double target = 1;
    double total_diff = target - start;
    double scale_amount = start + (total_diff * data->transition_scalar);
    
    paint_transition_scaled(client, cr, container, data, surface, scale_amount);
}

void paint_default_to_expanded(AppClient *client, cairo_t *cr, Container *container,
                               TransitionData *data, cairo_surface_t *surface) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double start = 1;
    double target = 1.75;
    double total_diff = target - start;
    double scale_amount = start + (total_diff * data->transition_scalar);
    
    paint_transition_scaled(client, cr, container, data, surface, scale_amount);
}

void paint_expanded_to_default(AppClient *client, cairo_t *cr, Container *container,
                               TransitionData *data, cairo_surface_t *surface) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double start = 1.75;
    double target = 1;
    double total_diff = target - start;
    double scale_amount = start + (total_diff * data->transition_scalar);
    
    paint_transition_scaled(client, cr, container, data, surface, scale_amount);
}

void paint_transition_surface(AppClient *client, cairo_t *cr, Container *container,
                              TransitionData *data, cairo_surface_t *surface, int anim) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (anim & Transition::ANIM_FADE_IN || anim & Transition::ANIM_FADE_OUT) {
        cairo_push_group(cr);
    }
    if (anim & Transition::ANIM_NONE) {
        paint_default(client, cr, container, data, surface);
    } else if (anim & Transition::ANIM_DEFAULT_TO_SQUASHED) {
        paint_default_to_squashed(client, cr, container, data, surface);
    } else if (anim & Transition::ANIM_SQUASHED_TO_DEFAULT) {
        paint_squashed_to_default(client, cr, container, data, surface);
    } else if (anim & Transition::ANIM_DEFAULT_TO_EXPANDED) {
        paint_default_to_expanded(client, cr, container, data, surface);
    } else if (anim & Transition::ANIM_EXPANDED_TO_DEFAULT) {
        paint_expanded_to_default(client, cr, container, data, surface);
    }
    
    if (anim & Transition::ANIM_FADE_IN) {
        auto p = cairo_pop_group(cr);
        
        cairo_set_source(cr, p);
        cairo_paint_with_alpha(cr, data->transition_scalar);
    } else if (anim & Transition::ANIM_FADE_OUT) {
        auto p = cairo_pop_group(cr);
        
        cairo_set_source(cr, p);
        cairo_paint_with_alpha(cr, 1 - data->transition_scalar);
    }
}

static void layout_and_repaint(App *app, AppClient *client, Timeout *, void *user_data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *container = (Container *) user_data;
    if (!container)
        return;
    auto data = (TransitionData *) container->user_data;
    if (!data)
        return;
    container->children.clear();
    container->children.shrink_to_fit();
    container->children.push_back(data->second);
    container->children.push_back(data->first);
    data->first->interactable = true;
    data->second->interactable = true;
    delete data;
    container->automatically_paint_children = true;
    container->user_data = nullptr;
    container->when_paint = nullptr;
    
    client_layout(client->app, client);
    client_paint(client->app, client);
}

void paint_transition(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = (TransitionData *) container->user_data;
    if (!data->original_surface || !data->replacement_surface) {
        return;
    }
    
    paint_transition_surface(client, cr, container, data, data->original_surface,
                             data->original_anim);
    
    paint_transition_surface(client, cr, container, data, data->replacement_surface,
                             data->replacement_anim);
    
    if (data->transition_scalar >= 1) {
        app_timeout_create(client->app, client, 0, layout_and_repaint, container, const_cast<char *>(__PRETTY_FUNCTION__));
    }
}

void transition_same_container(AppClient *client, cairo_t *cr, Container *parent, int original_anim,
                               int replacement_anim) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (parent->user_data != nullptr) {
        return;
    }
    auto data = new TransitionData;
    parent->user_data = data;
    parent->when_paint = paint_transition;
    parent->automatically_paint_children = false;
    
    data->first = parent->children[0];
    data->second = parent->children[1];
    data->first->interactable = false;
    data->second->interactable = false;
    data->original_anim = original_anim;
    data->replacement_anim = replacement_anim;
    
    // layout and paint both first and second containers into their own temp surfaces
    {
        data->original_surface = accelerated_surface(client->app, client, parent->real_bounds.w,
                                                     parent->real_bounds.h);
        data->original_cr = cairo_create(data->original_surface);
        layout(client, cr, data->first,
               Bounds(0,
                      0,
                      parent->real_bounds.w,
                      parent->real_bounds.h));
        auto main_cr = client->cr;
        client->cr = data->original_cr;
        paint_container(client->app, client, data->first);
        client->cr = main_cr;
    }
    {
        data->replacement_surface = accelerated_surface(client->app, client, parent->real_bounds.w,
                                                        parent->real_bounds.h);
        data->replacement_cr = cairo_create(data->replacement_surface);
        data->second->exists = true;
        layout(client, cr, data->second,
               Bounds(0,
                      0,
                      parent->real_bounds.w,
                      parent->real_bounds.h));
        auto main_cr = client->cr;
        client->cr = data->replacement_cr;
        paint_container(client->app, client, data->second);
        data->second->exists = false;
        client->cr = main_cr;
    }
    
    client_create_animation(client->app, client, &data->transition_scalar, data->lifetime, 0, 350,
                            getEasingFunction(::EaseOutQuint), 1, false);
}

int get_offset(Container *target, ScrollContainer *scroll_pane) {
    int offset = scroll_pane->content->wanted_pad.h;
    
    for (int i = 0; i < scroll_pane->content->children.size(); i++) {
        auto possible = scroll_pane->content->children[i];
        
        if (possible == target) {
            return offset;
        }
        
        offset += possible->real_bounds.h;
        offset += scroll_pane->content->spacing;
    }
    
    return offset;
}

static void
paint_textfield(AppClient *client, cairo_t *cr, Container *container) {
    // paint blue border
    auto *data = (FieldData *) container->user_data;

    // clip text
    PangoLayout *layout = get_cached_pango_font(cr, config->font, data->settings.font_size * config->dpi,
                                                PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_width(layout, -1); // disable wrapping
    
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    if (!data->text.empty() || container->active) {
        pango_layout_set_text(layout, data->text.c_str(), data->text.size());
    } else {
        auto watered_down = ArgbColor(config->color_pinned_icon_editor_field_default_text);
        watered_down.a = 0.6;
        set_argb(cr, watered_down);
        pango_layout_set_text(layout, data->settings.when_empty_text.c_str(), data->settings.when_empty_text.size());
    }
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    auto text_off_x = 10 * config->dpi;
    auto text_off_y = container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2);
    
    cairo_move_to(cr,
                  container->real_bounds.x + text_off_x,
                  container->real_bounds.y + text_off_y);
    pango_cairo_show_layout(cr, layout);
    
    if (container->active) {
        PangoRectangle cursor_strong_pos;
        PangoRectangle cursor_weak_pos;
        pango_layout_get_cursor_pos(layout, data->text.size(), &cursor_strong_pos, &cursor_weak_pos);
        
        set_argb(cr, ArgbColor(0, 0, 0, 1));
        int offset = cursor_strong_pos.x != 0 ? -1 : 0;
        cairo_rectangle(cr,
                        cursor_strong_pos.x / PANGO_SCALE + container->real_bounds.x + offset + text_off_x,
                        cursor_strong_pos.y / PANGO_SCALE + container->real_bounds.y + text_off_y,
                        1 * config->dpi,
                        cursor_strong_pos.height / PANGO_SCALE);
        cairo_fill(cr);
    }
    
    if (container->active) {
        set_argb(cr, config->color_pinned_icon_editor_field_pressed_border);
    } else if (container->state.mouse_hovering) {
        set_argb(cr, config->color_pinned_icon_editor_field_hovered_border);
    } else {
        set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    }
    paint_margins_rect(client, cr, container->real_bounds, 2, 0);
}

Container *make_textfield(Container *parent, FieldSettings settings, int w, int h) {
    auto *field = parent->child(w, h);
    auto data = new FieldData;
    data->settings = std::move(settings);
    field->user_data = data;
    field->when_paint = paint_textfield;
    field->when_key_event = key_event_textfield;
    
    return field;
}

PopupItemDraw::PopupItemDraw(const std::string &icon0, const std::string &icon1, const std::string &text,
                             const std::string &iconend) : icon0(icon0), icon1(icon1), text(text), iconend(iconend) {}
