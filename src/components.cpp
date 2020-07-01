//
// Created by jmanc3 on 6/14/20.
//

#include "components.h"
#ifdef TRACY_ENABLE
#include "../tracy/Tracy.hpp"
#endif
#include "utility.h"

#include <atomic>
#include <pango/pangocairo.h>
#include <tinyclipboard.h>

static int scroll_amount = 30;
static double scroll_anim_time = 30;
static easingFunction easing_function = 0;

static std::atomic<bool> dragging = false;

static std::atomic<bool> mouse_down_arrow_held = false;

void
scrollpane_scrolled(AppClient* client,
                    cairo_t* cr,
                    Container* container,
                    int scroll_x,
                    int scroll_y)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    /*
        auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
        auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);

        if (reply->mods & XKB_KEY_Shift_L || reply->mods & XKB_KEY_Control_L) {
            container->scroll_h_real += scroll_x * scroll_amount + scroll_y * scroll_amount;
        } else {
    */
    //}
    container->scroll_v_real += scroll_y * scroll_amount;

    container->scroll_h_real += scroll_x * scroll_amount;

    container->scroll_h_visual = container->scroll_h_real;
    container->scroll_v_visual = container->scroll_v_real;
    ::layout(container, container->real_bounds, false);
}

static void
paint_show(AppClient* client, cairo_t* cr, Container* container)
{
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

static void
mouse_down_thread(AppClient* client,
                  cairo_t* cr,
                  Container* container,
                  int horizontal_change,
                  int vertical_change)
{
    App* app = client->app;

    usleep(1000 * 400); // first skip should be slower than the rest

    std::unique_lock guard(client->app->clients_mutex);
    while (mouse_down_arrow_held.load() && valid_client(app, client) && app->running) {
        if (bounds_contains(
              container->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            Container* target = container->parent->parent->children[2];
            target->scroll_h_real += horizontal_change * 1.5;
            target->scroll_v_real += vertical_change * 1.5;
            // ::layout(target, target->real_bounds, false);
            client_layout(client->app, client);

            if (horizontal_change != 0)
                client_create_animation(client->app,
                                        client,
                                        &target->scroll_h_visual,
                                        scroll_anim_time,
                                        easing_function,
                                        target->scroll_h_real,
                                        true);
            if (vertical_change != 0)
                ;
            client_create_animation(client->app,
                                    client,
                                    &target->scroll_v_visual,
                                    scroll_anim_time,
                                    easing_function,
                                    target->scroll_v_real,
                                    true);
            guard.unlock();
            usleep(1000 * (scroll_anim_time * 3));
            guard.lock();
        } else {
            guard.unlock();
            usleep(1000 * (scroll_anim_time));
            guard.lock();
        }
    }
}

static void
mouse_down_arrow_up(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container* target = container->parent->parent->children[2];
    target->scroll_v_real += scroll_amount;
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);

    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);

    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    std::thread t(mouse_down_thread, client, cr, container, 0, scroll_amount);
    t.detach();
}

static void
mouse_down_arrow_bottom(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container* target = container->parent->parent->children[2];
    target->scroll_v_real -= scroll_amount;
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);

    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);

    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    std::thread t(mouse_down_thread, client, cr, container, 0, -scroll_amount);
    t.detach();
}

static void
mouse_down_arrow_left(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container* target = container->parent->parent->children[2];
    target->scroll_h_real += scroll_amount;
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);

    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);

    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    std::thread t(mouse_down_thread, client, cr, container, scroll_amount, 0);
    t.detach();
}

static void
mouse_down_arrow_right(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container* target = container->parent->parent->children[2];
    target->scroll_h_real -= scroll_amount;
    // ::layout(target, target->real_bounds, false);
    client_layout(client->app, client);

    client_create_animation(client->app,
                            client,
                            &target->scroll_h_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &target->scroll_v_visual,
                            scroll_anim_time,
                            easing_function,
                            target->scroll_v_real,
                            true);

    if (mouse_down_arrow_held.load())
        return;
    mouse_down_arrow_held.store(true);
    std::thread t(mouse_down_thread, client, cr, container, -scroll_amount, 0);
    t.detach();
}

static void
mouse_arrow_up(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    mouse_down_arrow_held = false;
}

Bounds
right_thumb_bounds(Container* scrollpane, Bounds thumb_area)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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
bottom_thumb_bounds(Container* scrollpane, Bounds thumb_area)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
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
clicked_right_thumb(AppClient* client, cairo_t* cr, Container* thumb_container, bool animate)
{
    auto* scrollpane = thumb_container->parent->parent;
    auto* content = scrollpane->children[2];

    double thumb_height =
      right_thumb_bounds(thumb_container->parent->parent, thumb_container->real_bounds).h;
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

    Container* content_area = thumb_container->parent->parent->children[2];
    content_area->scroll_v_real = -y;
    if (!animate)
        content_area->scroll_v_visual = -y;
    // ::layout(content_area, content_area->real_bounds, false);
    client_layout(client->app, client);

    if (animate) {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    } else {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual,
                                0,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual,
                                0,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    }
}

static void
clicked_bottom_thumb(AppClient* client, cairo_t* cr, Container* thumb_container, bool animate)
{
    auto* scrollpane = thumb_container->parent->parent;
    auto* content = scrollpane->children[2];

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

    Container* content_area = thumb_container->parent->parent->children[2];
    content_area->scroll_h_real = -x;
    if (!animate)
        content_area->scroll_h_visual = -x;
    // ::layout(content_area, content_area->real_bounds, false);
    client_layout(client->app, client);

    if (animate) {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual,
                                scroll_anim_time * 2,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    } else {
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_h_visual,
                                0,
                                easing_function,
                                content_area->scroll_h_real,
                                true);
        client_create_animation(client->app,
                                client,
                                &content_area->scroll_v_visual,
                                0,
                                easing_function,
                                content_area->scroll_v_real,
                                true);
    }
}

static void
right_scrollbar_mouse_down(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, true);
}

static void
right_scrollbar_drag_start(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
right_scrollbar_drag(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
right_scrollbar_drag_end(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_right_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_mouse_down(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, true);
}

static void
bottom_scrollbar_drag_start(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_drag(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

static void
bottom_scrollbar_drag_end(AppClient* client_entity, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    clicked_bottom_thumb(client_entity, cr, container, false);
}

Container*
make_scrollpane(Container* parent, ScrollPaneSettings settings)
{
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

    return content_container;
}

static void
update_preffered_x(AppClient* client, Container* textarea)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* data = (TextAreaData*)textarea->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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
put_cursor_on_screen(AppClient* client, Container* textarea)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* data = (TextAreaData*)textarea->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, textarea->real_bounds.w * PANGO_SCALE);
    }

    PangoRectangle strong_pos;
    PangoRectangle weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &strong_pos, &weak_pos);

    Container* content_area = textarea->parent;

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
                            &content_area->scroll_h_visual,
                            scroll_anim_time,
                            easing_function,
                            content_area->scroll_h_real,
                            true);
    client_create_animation(client->app,
                            client,
                            &content_area->scroll_v_visual,
                            scroll_anim_time,
                            easing_function,
                            content_area->scroll_v_real,
                            true);
}

static void
update_bounds(AppClient* client, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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
                                    &container->parent->parent->scroll_h_visual,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_h_real,
                                    true);
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_v_visual,
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
                                    &container->parent->parent->scroll_h_visual,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_h_real,
                                    true);
            client_create_animation(client->app,
                                    client,
                                    &container->parent->parent->scroll_v_visual,
                                    scroll_anim_time,
                                    easing_function,
                                    container->parent->parent->scroll_v_real,
                                    true);

            request_refresh(client->app, client);
        }
    }
}

static void
paint_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* data = (TextAreaData*)container->user_data;
    if (data->state->first_bounds_update) {
        update_bounds(client, container);
        data->state->first_bounds_update = false;
    }

    cairo_save(cr);

    // DEBUG
    // paint_show(client, cr, container);

    // TEXT
    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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

        if (maxy == miny) { // Same line
            cairo_rectangle(
              cr, container->real_bounds.x + minx, container->real_bounds.y + miny, maxx - minx, h);
            cairo_fill(cr);
        } else {
            if ((maxy - miny) > h) { // More than one line off difference
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
    } // END Selection background

    // SHOW TEXT LAYOUT
    set_argb(cr, data->color);

    cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y);
    pango_cairo_show_layout(cr, layout);

    if (container->parent->active == false && data->state->text.empty()) {
        cairo_save(cr);

        PangoLayout* prompt_layout = get_cached_pango_font(
          client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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

        cairo_move_to(cr, container->real_bounds.x + 100, container->real_bounds.y);
        pango_cairo_show_layout(cr, prompt_layout);
        cairo_restore(cr);
    }

    // PAINT CURSOR
    if (container->parent->active) {
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

    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_LEFT);
}

static void
move_cursor(TextAreaData* data, int byte_index, bool increase_selection)
{
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
clicked_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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

    xcb_set_input_focus(client->app->connection, XCB_NONE, client->window, XCB_CURRENT_TIME);
    xcb_flush(client->app->connection);
}

static void
drag_start_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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

    std::thread drag_thread([client, container]() -> void {
        dragging = true;
        App* app = client->app;

        Container* content_area = container->parent;

        std::unique_lock guard(client->app->clients_mutex);
        while (dragging.load() && valid_client(app, client)) {
            if (!container->state.mouse_dragging) {
                dragging = false;
                guard.unlock();
                break;
            }

            auto* data = (TextAreaData*)container->user_data;

            PangoLayout* layout = get_cached_pango_font(
              client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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

            if (x < bounds.x) { // off the left side
                modified_x = true;
                double multiplier =
                  std::min((double)scroll_amount * 3, bounds.x - x) / scroll_amount;
                content_area->scroll_h_real += scroll_amount * multiplier;
            }
            if (x > bounds.x + bounds.w) { // off the right side
                modified_x = true;
                double multiplier =
                  std::min((double)scroll_amount * 3, x - (bounds.x + bounds.w)) / scroll_amount;
                content_area->scroll_h_real -= scroll_amount * multiplier;
            }
            if (y < bounds.y) { // off the top
                modified_y = true;
                double multiplier =
                  std::min((double)scroll_amount * 3, bounds.y - y) / scroll_amount;
                content_area->scroll_v_real += scroll_amount * multiplier;
            }
            if (y > bounds.y + bounds.h) { // off the bottom
                modified_y = true;
                double multiplier =
                  std::min((double)scroll_amount * 3, y - (bounds.y + bounds.h)) / scroll_amount;
                content_area->scroll_v_real -= scroll_amount * multiplier;
            }

            // ::layout(content_area, content_area->real_bounds, false);
            client_layout(client->app, client);

            if (modified_x)
                client_create_animation(client->app,
                                        client,
                                        &content_area->scroll_h_visual,
                                        scroll_anim_time,
                                        easing_function,
                                        content_area->scroll_h_real,
                                        true);
            if (modified_y)
                client_create_animation(client->app,
                                        client,
                                        &content_area->scroll_v_visual,
                                        scroll_anim_time,
                                        easing_function,
                                        content_area->scroll_v_real,
                                        true);

            guard.unlock();
            usleep(1000 * scroll_anim_time);
            guard.lock();
        }

        dragging = false;
        if (valid_client(app, client))
            request_refresh(app, client);
    });
    drag_thread.detach();

    auto cookie = xcb_xkb_get_state(client->app->connection, client->keyboard->device_id);
    auto reply = xcb_xkb_get_state_reply(client->app->connection, cookie, nullptr);

    bool shift = reply->mods & XKB_KEY_Shift_L;

    move_cursor(data, index + trailing, shift);
}

static void
mouse_down_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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
drag_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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
drag_end_textarea(AppClient* client, cairo_t* cr, Container* container)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container = container->children[0];
    auto* data = (TextAreaData*)container->user_data;

    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

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
textarea_key_release(AppClient* client,
                     cairo_t* cr,
                     Container* container,
                     xcb_generic_event_t* event);

Container*
make_textarea(Container* parent, TextAreaSettings settings)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    Container* content_area = make_scrollpane(parent, settings);

    int width = 0;
    if (settings.wrap)
        width = FILL_SPACE;
    Container* textarea = content_area->child(::vbox, width, settings.font_size);

    textarea->when_paint = paint_textarea;
    textarea->when_key_release = textarea_key_release;
    content_area->when_drag_end_is_click = false;
    content_area->when_drag_start = drag_start_textarea;
    content_area->when_drag = drag_textarea;
    content_area->when_drag_end = drag_end_textarea;
    content_area->when_clicked = clicked_textarea;
    content_area->when_mouse_down = mouse_down_textarea;

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

    return textarea;
}

static void
insert_action(AppClient* client, Container* textarea, TextAreaData* data, std::string text)
{
    // Try to merge with the previous
    bool merged = false;

    if (!data->state->undo_stack.empty()) {
        UndoAction* previous_action = data->state->undo_stack.back();

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
}

static void
delete_action(AppClient* client, Container* textarea, TextAreaData* data, int amount)
{
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
replace_action(AppClient* client, Container* textarea, TextAreaData* data, std::string text)
{
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

char tokens_list[] = { '[',  ']', '|', '(', ')', '{',  '}', ';', '.', '!', '@',
                       '#',  '$', '%', '^', '&', '*',  '-', '=', '+', ':', '\'',
                       '\'', '<', '>', '?', '|', '\\', '/', ',', '`', '~', '\t' };

enum motion
{
    left = 0,
    right = 1,
};

enum group
{
    none = 0,
    space = 1,
    newline = 2,
    token = 3,
    normal = 4,
};

class Seeker
{
  public:
    TextState* state;

    int start_pos;
    int current_pos;

    group starting_group_to_left;
    group starting_group_to_right;

    bool same_token_type_to_left_and_right; // we are always inside a group but this means that to
    // either side is the same group type
    bool different_token_type_to_atleast_one_side; // at the end of a group

    group group_at(int pos)
    {
        if (pos < 0 || pos >= state->text.size()) {
            return group::none;
        }
        char c = state->text.at(pos);
        if (c == ' ')
            return group::space;
        if (c == '\n')
            return group::newline;
        for (auto token : tokens_list)
            if (token == c)
                return group::token;
        return group::normal;
    }

    group group_to(motion direction)
    {
        int off = 0;
        if (direction == motion::right)
            off = 1;
        else if (direction == motion::left)
            off = -1;
        return group_at(current_pos + off);
    }

    group seek_until_different_token(motion direction, int off)
    {
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

    group seek_until_right_before_different_token(motion direction)
    {
        return seek_until_different_token(direction, 1);
    }

    group seek_and_cover_different_token(motion direction)
    {
        return seek_until_different_token(direction, 0);
    }

    bool seek_until_specific_token(motion direction, group specific_token)
    {
        group active_group;
        while ((active_group = group_to(direction)) != group::none) {
            if (active_group == specific_token) {
                return true;
            }
            current_pos += direction == motion::left ? -1 : 1;
        }
        return false;
    }

    Seeker(TextState* state)
    {
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
go_to_edge(Seeker& seeker, motion motion_direction)
{
    if (motion_direction == motion::left) {
        seeker.seek_until_right_before_different_token(motion_direction);
    } else if (motion_direction == motion::right) {
        seeker.seek_and_cover_different_token(motion_direction);
    }
}

static int
seek_token(TextState* state, motion motion_direction)
{
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
move_vertically_lines(AppClient* client,
                      TextAreaData* data,
                      Container* textarea,
                      bool shift,
                      int multiplier)
{
    PangoLayout* layout = get_cached_pango_font(
      client->back_cr, data->font, data->font_size, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, data->state->text.c_str(), data->state->text.length());
    if (data->wrap) {
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout, textarea->real_bounds.w * PANGO_SCALE);
    }

    PangoRectangle strong_pos;
    PangoRectangle weak_pos;
    pango_layout_get_cursor_pos(layout, data->state->cursor, &strong_pos, &weak_pos);

    PangoLayoutLine* line = pango_layout_get_line(layout, 0);
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
textarea_handle_keypress(App* app, xcb_generic_event_t* event, Container* textarea)
{
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto* data = (TextAreaData*)textarea->user_data;

    if (!textarea->parent->active) {
        return;
    }

    switch (event->response_type) {
        case XCB_KEY_RELEASE:
        case XCB_KEY_PRESS: {
            auto* e = (xcb_key_press_event_t*)event;
            auto client = client_by_window(app, e->event);
            if (!valid_client(app, client))
                break;

            xkb_keycode_t keycode = e->detail;
            const xkb_keysym_t* keysyms;
            int num_keysyms = xkb_state_key_get_syms(client->keyboard->state, keycode, &keysyms);

            bool shift = false;
            bool control = false;
            if (e->state & XCB_MOD_MASK_SHIFT) {
                shift = true;
            }
            if (e->state & XCB_MOD_MASK_CONTROL) {
                control = true;
            }

            if (num_keysyms > 0) {
                if (keysyms[0] == XKB_KEY_BackSpace) {
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
                    break;
                } else if (keysyms[0] == XKB_KEY_Delete) {
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
                    break;
                } else if (keysyms[0] == XKB_KEY_Escape) {
                    move_cursor(data, data->state->cursor, false);
                    break;
                } else if (keysyms[0] == XKB_KEY_Return) {
                    if (!data->single_line) {
                        if (data->state->selection_x != -1) {
                            replace_action(client, textarea, data, "\n");
                        } else {
                            insert_action(client, textarea, data, "\n");
                        }
                    }
                    break;
                } else if (keysyms[0] == XKB_KEY_Tab) {
                    if (data->state->selection_x != -1) {
                        replace_action(client, textarea, data, "\t");
                    } else {
                        insert_action(client, textarea, data, "\t");
                    }
                    break;
                } else if (keysyms[0] == XKB_KEY_a) {
                    if (control) {
                        data->state->cursor = data->state->text.size();
                        data->state->selection_x = 0;
                        put_cursor_on_screen(client, textarea);
                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_z) {
                    if (control) {
                        // undo
                        if (!data->state->undo_stack.empty()) {
                            UndoAction* action = data->state->undo_stack.back();
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
                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_Z) {
                    if (control) {
                        // redo
                        if (!data->state->redo_stack.empty()) {
                            UndoAction* action = data->state->redo_stack.back();
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
                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_c) {
                    if (control) {

                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_v) {
                    if (control) {

                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_Home) {
                    Seeker seeker(data->state);

                    seeker.seek_until_specific_token(motion::left, group::newline);

                    move_cursor(data, seeker.current_pos, shift);
                    update_preffered_x(client, textarea);
                    put_cursor_on_screen(client, textarea);
                    break;
                } else if (keysyms[0] == XKB_KEY_End) {
                    Seeker seeker(data->state);

                    seeker.current_pos -= 1;
                    seeker.seek_until_specific_token(motion::right, group::newline);

                    move_cursor(data, seeker.current_pos + 1, shift);
                    update_preffered_x(client, textarea);
                    put_cursor_on_screen(client, textarea);
                    break;
                } else if (keysyms[0] == XKB_KEY_Page_Up) {
                    move_vertically_lines(client, data, textarea, shift, -10);
                    break;
                } else if (keysyms[0] == XKB_KEY_Page_Down) {
                    move_vertically_lines(client, data, textarea, shift, 10);
                    break;
                } else if (keysyms[0] == XKB_KEY_Left) {
                    if (control) {
                        int jump_target = seek_token(data->state, motion::left);
                        move_cursor(data, jump_target, shift);
                        update_preffered_x(client, textarea);
                        put_cursor_on_screen(client, textarea);
                        break;
                    } else {
                        int cursor_target = data->state->cursor;
                        cursor_target -= 1;
                        if (cursor_target < 0) {
                            cursor_target = 0;
                        }
                        move_cursor(data, cursor_target, shift);
                        update_preffered_x(client, textarea);
                        put_cursor_on_screen(client, textarea);
                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_Right) {
                    if (control) {
                        int jump_target = seek_token(data->state, motion::right);
                        move_cursor(data, jump_target, shift);
                        update_preffered_x(client, textarea);
                        put_cursor_on_screen(client, textarea);
                        break;
                    } else {
                        int cursor_target = data->state->cursor + 1;
                        if (cursor_target > data->state->text.size()) {
                            cursor_target = data->state->text.size();
                        }
                        move_cursor(data, cursor_target, shift);
                        update_preffered_x(client, textarea);
                        put_cursor_on_screen(client, textarea);
                        break;
                    }
                } else if (keysyms[0] == XKB_KEY_Up) {
                    move_vertically_lines(client, data, textarea, shift, -1);
                    break;
                } else if (keysyms[0] == XKB_KEY_Down) {
                    move_vertically_lines(client, data, textarea, shift, 1);
                    break;
                }
            }

            int size = xkb_state_key_get_utf8(client->keyboard->state, keycode, NULL, 0) + 1;
            if (size > 1) {
                char* buffer = new char[size];
                int read = xkb_state_key_get_utf8(client->keyboard->state, keycode, buffer, size);
                if (read > 0) {
                    if (isprint(buffer[0])) {
                        if (data->state->selection_x != -1) {
                            replace_action(client, textarea, data, buffer);
                        } else {
                            insert_action(client, textarea, data, buffer);
                        }
                    }
                }
                delete[] buffer;
            }
            break;
        }
    }
}

static void
textarea_key_release(AppClient* client,
                     cairo_t* cr,
                     Container* container,
                     xcb_generic_event_t* event)
{
    textarea_handle_keypress(client->app, event, container);
}