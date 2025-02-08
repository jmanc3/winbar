//
// Created by jmanc3 on 3/3/23.
//

#include <pango/pangocairo.h>
#include <cmath>

#ifdef TRACY_ENABLE

#include <tracy/Tracy.hpp>

#endif

#include "chatgpt.h"
#include "application.h"
#include "drawer.h"
#include "main.h"
#include "config.h"
#include "taskbar.h"
#include "dpi.h"
#include "components.h"
#include "defer.h"

static std::string api_key;
static double scroll_anim_time = 100;

struct RootData : UserData {
    bool hovering = false;
};

bool pango_layout_xy_to_index_custom(PangoLayout *layout, int x, int y, int *index, int *trailing);

struct TabData : public UserData {
    int index = 0;
    bool selected = false;
};

static void
paint_tab(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (TabData *) container->user_data;
    if (data->selected) {
        draw_colored_rect(client, ArgbColor(.267, .275, .329, 1), container->real_bounds);
    } else {
        ArgbColor color(.204, .208, .255, 1);
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                darken(&color, 10);
            } else {
                darken(&color, 5);
            }
        }
        draw_colored_rect(client, color, container->real_bounds);
    }
    
    auto layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    std::string text = "Revision " + std::to_string(data->index + 1);
    
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
clicked_tab(AppClient *client, cairo_t *cr, Container *container) {
    for (auto tab: container->parent->children) {
        auto data = (TabData *) tab->user_data;
        data->selected = false;
    }
    auto data = (TabData *) container->user_data;
    data->selected = true;
}

static void
paint_add_new_button(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, ArgbColor(.125, .129, .137, 1), container->real_bounds); 
    
    double pad = 6 * config->dpi;
    rounded_rect(client, 4 * config->dpi, std::round(container->real_bounds.x + pad),
                 std::round(container->real_bounds.y + pad),
                 std::round(container->real_bounds.w - (pad * 2)), std::round(container->real_bounds.h - (pad * 2)),
                 ArgbColor(1, 1, 1, .3), 2.0f);
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        ArgbColor color = ArgbColor(.125, .129, .137, 1);
        if (container->state.mouse_pressing) {
            color = lighten(color, 6);
        } else {
            color = lighten(color, 12);
        }
        rounded_rect(client, 4 * config->dpi, container->real_bounds.x + pad, container->real_bounds.y + pad,
                     container->real_bounds.w - (pad * 2), container->real_bounds.h - (pad * 2), color);
        set_argb(cr, color);
        cairo_fill(cr);
    } else {
        ArgbColor color = ArgbColor(.125, .129, .137, 1);
        rounded_rect(client, 4 * config->dpi, container->real_bounds.x + pad, container->real_bounds.y + pad,
                     container->real_bounds.w - (pad * 2), container->real_bounds.h - (pad * 2), color);
        set_argb(cr, color);
        cairo_fill(cr);
    }
    
    
    auto layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text = "New chat";
    
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + width * .34),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

struct ConversationData : public UserData {
    int index = 0;
//    bool selected = false;
};

static void
paint_conversation_button(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (ConversationData *) container->user_data;
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        double pad = 6 * config->dpi;
        ArgbColor color = ArgbColor(.125, .129, .137, 1);
//        if (data->selected) {
//            set_argb(cr, lighten(color, 14));
//        } else
        if (container->state.mouse_pressing) {
            color = lighten(color, 10);
        } else {
            color = lighten(color, 12);
        }
        rounded_rect(client, 4 * config->dpi, container->real_bounds.x + pad, container->real_bounds.y + pad,
                     container->real_bounds.w - (pad * 2), container->real_bounds.h - (pad * 2), color);
        set_argb(cr, color);
        cairo_fill(cr);
    }
    
    auto layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text = "Chat " + std::to_string(data->index + 1);
    
    pango_layout_set_text(layout, text.c_str(), text.length());
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + width * .34),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}


static void
clicked_conversation_button(AppClient *client, cairo_t *cr, Container *container) {

}

static void
clicked_add_new_button(AppClient *client, cairo_t *cr, Container *container) {
    if (auto c = container_by_name("left_content", container->parent)) {
        for (auto child: c->children) {
            auto data = (ConversationData *) child->user_data;
        }
    }
}

static void
paint_content_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_content_background(client, cr, container);
    
    auto *scroll = (ScrollContainer *) container;
    if (!scroll->content->children.empty()) {
        return;
    }
    
    auto *data = (RootData *) client->root->user_data;
    
    auto layout = get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    static std::string text = "How to use chatgpt:"
                              "\n\t\u2022 Visit <span foreground=\"#5577ff\">https://platform.openai.com/account/api-keys</span>"
                              "\n\t\u2022 Login"
                              "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                              "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                              "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
    
    double pad = 30 * config->dpi;
    pango_layout_set_width(layout, (container->real_bounds.w - pad * 2) * PANGO_SCALE);
    
    int start = 31;
    int end = 74;
    
    // markup text so that the link is blue
    pango_layout_set_markup(layout, text.data(), text.size());
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h * .33 - height / 2));
    
    int mouse_x =
            client->mouse_current_x - ((int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2));
    int mouse_y = client->mouse_current_y -
                  ((int) (container->real_bounds.y + container->real_bounds.h * .33 - height / 2));
    
    // get the index of the character at the mouse position
    int index;
    pango_layout_xy_to_index_custom(layout, mouse_x * PANGO_SCALE, mouse_y * PANGO_SCALE, &index, nullptr);
    
    if (index >= start && index <= end) {
        data->hovering = true;
        
        static std::string text = "How to use chatgpt:"
                                  "\n\t\u2022 Visit <span foreground=\"#5577ff\" underline=\"single\">https://platform.openai.com/account/api-keys</span>"
                                  "\n\t\u2022 Login"
                                  "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                                  "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                                  "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
        
        if (container->state.mouse_pressing) {
            static std::string text = "How to use chatgpt:"
                                      "\n\t\u2022 Visit <span foreground=\"#225599\" underline=\"single\">https://platform.openai.com/account/api-keys</span>"
                                      "\n\t\u2022 Login"
                                      "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                                      "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                                      "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
            pango_layout_set_markup(layout, text.data(), text.size());
        } else {
            pango_layout_set_markup(layout, text.data(), text.size());
        }
    } else {
        data->hovering = false;
    }
    
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_width(layout, -1);
}

static void
paint_label(AppClient *client, cairo_t *cr, Container *container) {
    auto label = dynamic_cast<EditableSelectableLabel *>(container);
    
    PangoLayout *layout =
            get_cached_pango_font(client->cr, label->font, label->size, label->weight);
    
    pango_layout_set_text(layout, label->text.data(), label->text.size());
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    
    if (label->selection_start != label->selection_end) {
        PangoRectangle cursor_strong_pos;
        PangoRectangle cursor_weak_pos;
        pango_layout_get_cursor_pos(layout, label->selection_start, &cursor_strong_pos, &cursor_weak_pos);
        
        // TODO: for the center rect, only draw a rect clipped by the parent bounds
        
        // SELECTION BACKGROUND
        set_argb(cr, ArgbColor(.2, .5, .8, 1));
        PangoRectangle selection_strong_pos;
        PangoRectangle selection_weak_pos;
        pango_layout_get_cursor_pos(
                layout, label->selection_end, &selection_strong_pos, &selection_weak_pos);
        
        bool cursor_first = false;
        if (cursor_strong_pos.y == selection_strong_pos.y) {
            if (cursor_strong_pos.x < selection_strong_pos.x) {
                cursor_first = true;
            }
        } else if (cursor_strong_pos.y < selection_strong_pos.y) {
            cursor_first = true;
        }
        
        double w = std::max(container->real_bounds.w, container->parent->real_bounds.w);
        
        int minx = std::min(selection_strong_pos.x, cursor_strong_pos.x) / PANGO_SCALE;
        int miny = std::min(selection_strong_pos.y, cursor_strong_pos.y) / PANGO_SCALE;
        int maxx = std::max(selection_strong_pos.x, cursor_strong_pos.x) / PANGO_SCALE;
        int maxy = std::max(selection_strong_pos.y, cursor_strong_pos.y) / PANGO_SCALE;
        int h = cursor_strong_pos.height / PANGO_SCALE;
        
        if (maxy == miny) {// Same line
            cairo_rectangle(
                    cr, container->real_bounds.x + minx, container->real_bounds.y + miny, maxx - minx, h);
            cairo_fill(cr);
        } else {
            if ((maxy - miny) > h) {// More than one line off difference
                Bounds b(container->real_bounds.x, container->real_bounds.y + miny + h,
                         w,
                         maxy - miny - h);
                set_rect(cr, b);
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
    }
    
    set_argb(cr, config
            ->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x),
                  (int) (container->real_bounds.y));
    pango_cairo_show_layout(cr, layout
    );
    
    pango_layout_set_width(layout,
                           -1);
}

static void
paint_textfield_label(AppClient *client, cairo_t *cr, Container *container) {
    Bounds b = container->real_bounds;
    double pad = -10 * config->dpi;
    b.x += pad;
    b.y += pad;
    b.w -= pad * 2;
    b.h -= pad * 2;
    draw_colored_rect(client, ArgbColor(.267, .275, .329, 1), b);
    auto color = ArgbColor(1, 1, 1, .3);
    rounded_rect(client, 4 * config->dpi, std::round(b.x),
                 std::round(b.y),
                 std::round(b.w), std::round(b.h), color, 2.0f);
    set_argb(cr, color);
    cairo_stroke(cr);
    paint_label(client, cr, container);
}

static void
paint_content_background(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, ArgbColor(.267, .275, .329, 1), container->real_bounds);
}

static void
paint_bottom_right_background(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, ArgbColor(.204, .208, .255, 1), container->real_bounds);
}

static void
paint_left_background(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, ArgbColor(.125, .129, .137, 1), container->real_bounds);
}

static void clicked_no_api(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (RootData *) container->user_data;
    if (data->hovering) {
        system("xdg-open https://platform.openai.com/account/api-keys");
    }
}

void on_label_layout(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                     double *target_h) {
    auto label = dynamic_cast<EditableSelectableLabel *>(self);
    if (!label) return;
    
    if (label->previous_width != -1)
        if (bounds.w == label->previous_width)
            return;
    label->previous_width = bounds.w;
    
    PangoLayout *layout =
            get_cached_pango_font(client->cr, label->font, label->size, label->weight);
    
    pango_layout_set_text(layout, label->text.data(), label->text.size());
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_width(layout, bounds.w * PANGO_SCALE);
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    pango_layout_set_width(layout, -1);
    
    label->wanted_bounds.h = height + 40 * config->dpi;
}

static void content_before_layout(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                                  double *target_h) {
    auto scroll = (ScrollContainer *) self;
    for (auto s: screens) {
        if (s->is_primary) {
            auto max_width = s->width_in_pixels * .5;
            
            scroll->content->wanted_pad = Bounds(20 * config->dpi, 20 * config->dpi, 20 * config->dpi,
                                                 20 * config->dpi);
            
            double available_space = bounds.w + (40 * config->dpi);
            if (available_space > max_width) {
                double extra = (available_space - max_width) / 2;
                scroll->content->wanted_pad.x += extra;
                scroll->content->wanted_pad.w += extra;
            }
        }
    }
}

static void text_field_before_layout(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                                     double *target_h) {
    for (auto s: screens) {
        if (s->is_primary) {
            auto max_width = s->width_in_pixels * .5;
            
//            self->wanted_pad = Bounds(20 * config->dpi, 0, 20 * config->dpi, 0);
            
            double available_space = bounds.w + (40 * config->dpi);
            if (available_space > max_width) {
                double extra = (available_space - max_width) / 2;
//                self->wanted_pad.x += extra;
//                self->wanted_pad.w += extra;
            }
        }
    }
    
    static int five_line_h = -1;
    
    if (auto c = container_by_name("textfield", self)) {
        Bounds b = bounds;
        auto scroll = (ScrollContainer *) c->parent->parent;
        b.w = b.w - (self->wanted_pad.x + self->wanted_pad.w);
        auto label = (EditableSelectableLabel *) c;
        label->previous_width = -1;
        
        on_label_layout(client, label, b, target_w, target_h);
        
        self->wanted_bounds.h = label->wanted_bounds.h;
        
        if (five_line_h == -1) {
            PangoLayout *layout =
                    get_cached_pango_font(client->cr, label->font, label->size, label->weight);
            pango_layout_set_text(layout, "\n\n\n\n\n", 5);
            int width;
            int height;
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            five_line_h = height;
        }
        
        if (five_line_h != -1 && self->wanted_bounds.h < five_line_h) {
            self->wanted_bounds.h = five_line_h;
        }
    }
}

static void before_left_layout(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                               double *target_h) {
    if (client->bounds->w < 800 * config->dpi) {
        self->wanted_bounds.w = 0;
        self->automatically_paint_children = false;
    } else {
        self->wanted_bounds.w = 260 * config->dpi;
        self->automatically_paint_children = true;
    }
}

bool pango_layout_xy_to_index_custom(PangoLayout *layout, int x, int y, int *index, int *trailing) {
//    g_return_val_if_fail(layout != NULL, false);
//    g_return_val_if_fail(index != NULL, false);
//    g_return_val_if_fail(trailing != NULL, false);
    if (layout == NULL || index == nullptr || trailing == nullptr) {
        return false;
    }
    
    // Convert the input coordinate (x, y) to Pango units
//    x *= PANGO_SCALE;
//    y *= PANGO_SCALE;
    
    // Get the layout extents
    PangoRectangle ink_rect, logical_rect;
    pango_layout_get_extents(layout, &ink_rect, &logical_rect);
    
    if (y < logical_rect.y) {
        *index = 0;
        *trailing = 0;
        return true;
    } else if (y >= logical_rect.y + logical_rect.height) {
        pango_layout_xy_to_index(layout, logical_rect.width + 100, logical_rect.height + 100, index, trailing);
        return true;
    }
    
    // Iterate over the layout's lines
    PangoLayoutIter *iter = pango_layout_get_iter(layout);
    defer(pango_layout_iter_free(iter));
    
    PangoLayoutLine *line;
    int y_current = 0;
    do {
        line = pango_layout_iter_get_line(iter);
        if (line != NULL) {
            // Get the line extents
            PangoRectangle line_ink_rect, line_logical_rect;
            pango_layout_line_get_extents(line, &line_ink_rect, &line_logical_rect);
            
            // Check if the input coordinate is within the line bounds
            if (y >= y_current && y < y_current + line_logical_rect.height) {
                // Convert the input coordinate to line-relative coordinates
                int line_x = x;
                int line_y = y + line_logical_rect.y;
                
                // Get the index of the character at the specified position
                int char_index;
                int char_trailing = 0;
                pango_layout_line_x_to_index(line, line_x, &char_index, &char_trailing);
                
                // TODO: turn to layout index not line index
                *index = char_index;
                *trailing = char_trailing;
                
                return true;
            }
            
            y_current += line_logical_rect.height;
        }
    } while (pango_layout_iter_next_line(iter));
    
    *index = -1;
    *trailing = 0;
    return false;
}

struct SelectionInfo : public UserData {
    double relative_initial_x = 0;
    double relative_initial_y = 0;
    double relative_end_x = 0;
    double relative_end_y = 0;
    
    long last_click_time_ms = 0;
    int click_type = 0; // 0 means first click, 1 means second click, 2 means third click
};

static void selection_drag(AppClient *client, cairo_t *cr, Container *self);

static void
drag_timeout(App *app, AppClient *client, Timeout *timeout, void *data) {
    auto scroll = (ScrollContainer *) data;
    auto info = (SelectionInfo *) scroll->user_data;
    timeout->keep_running = client->left_mouse_down;
    
    if (client->mouse_current_y < scroll->real_bounds.y) {
        double diff = scroll->real_bounds.y - client->mouse_current_y;
        if (diff < 1)
            diff = 1;
        double scalar = diff / 33 * config->dpi;
        if (scalar > 7)
            scalar = 7;
        
        scroll->scroll_v_real += (15 * config->dpi) * scalar;
        client_create_animation(app, client,
                                &scroll->scroll_v_visual, scroll->lifetime, 0,
                                scroll_anim_time, 0, scroll->scroll_v_real, true);
//        scroll->scroll_v_visual = scroll->scroll_v_real;
        selection_drag(client, client->cr, scroll);
//        ::layout(client, client->cr, scroll, scroll->real_bounds);
//        client_paint(app, client);
    } else if (client->mouse_current_y > scroll->real_bounds.y + scroll->real_bounds.h) {
        auto diff = client->mouse_current_y - (scroll->real_bounds.y + scroll->real_bounds.h);
        if (diff < 1)
            diff = 1;
        double scalar = diff / 33 * config->dpi;
        if (scalar > 7)
            scalar = 7;
        
        scroll->scroll_v_real -= (15 * config->dpi) * scalar;
        client_create_animation(app, client,
                                &scroll->scroll_v_visual, scroll->lifetime, 0,
                                scroll_anim_time, 0, scroll->scroll_v_real, true);
//        scroll->scroll_v_visual = scroll->scroll_v_real;
        selection_drag(client, client->cr, scroll);
//        ::layout(client, client->cr, scroll, scroll->real_bounds);
//        client_paint(app, client);
    }
}

static void remove_all_selections(ScrollContainer *scroll) {
    for (auto c: scroll->content->children) {
        auto s = (EditableSelectableLabel *) c;
        s->selection_start = 0;
        s->selection_end = 0;
    }
}

static void selection_mouse_down(AppClient *client, cairo_t *cr, Container *self) {
    // TODO: don't do this if scrollbar is pressed
    
    auto scroll = (ScrollContainer *) self;
    auto info = (SelectionInfo *) scroll->user_data;
    info->relative_initial_x = client->mouse_initial_x + scroll->scroll_h_visual - self->real_bounds.x;
    info->relative_initial_y = client->mouse_initial_y + -scroll->scroll_v_visual - self->real_bounds.y;
    info->relative_end_x = info->relative_initial_x;
    info->relative_end_y = info->relative_initial_y;

//    printf("info->relative_initial_x = %d, info->relative_initial_y = %d\n", info->relative_initial_x, info->relative_initial_y);
    
    remove_all_selections(scroll);
    
    app_timeout_create(client->app, client, scroll_anim_time, drag_timeout, scroll, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
update_selections(EditableSelectableLabel *label, SelectionInfo *info, AppClient *client,
                  const Bounds &selection_rect) {
    if (overlaps(label->real_bounds, selection_rect)) {
        PangoLayout *layout =
                get_cached_pango_font(client->cr, label->font, label->size, label->weight);
        
        pango_layout_set_text(layout, label->text.data(), label->text.size());
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        pango_layout_set_width(layout, label->real_bounds.w * PANGO_SCALE);
        
        // Relative position inside scroll pane
        Bounds layout_normalized_bounds(label->real_bounds.x - label->parent->real_bounds.x,
                                        label->real_bounds.y - label->parent->real_bounds.y,
                                        label->real_bounds.w, label->real_bounds.h);
        
        int mouse_x = info->relative_initial_x - layout_normalized_bounds.x;
        int mouse_y = info->relative_initial_y - layout_normalized_bounds.y;
        int start_index;
        int start_trail;
        pango_layout_xy_to_index_custom(layout, mouse_x * PANGO_SCALE, mouse_y * PANGO_SCALE, &start_index,
                                        &start_trail);
        
        mouse_x = info->relative_end_x - layout_normalized_bounds.x;
        mouse_y = info->relative_end_y - layout_normalized_bounds.y;
        int end_index;
        int end_trail;
        pango_layout_xy_to_index_custom(layout, mouse_x * PANGO_SCALE, mouse_y * PANGO_SCALE, &end_index,
                                        &end_trail);
        
        if (end_index == -1 || start_index == -1) {
            end_index = 0;
            start_index = 0;
        }
        
        label->selection_start = start_index;
        label->selection_end = end_index;
    } else {
        label->selection_start = 0;
        label->selection_end = 0;
    }
}

static void selection_drag(AppClient *client, cairo_t *cr, Container *self) {
    auto scroll = (ScrollContainer *) self;
    auto info = (SelectionInfo *) scroll->user_data;
    
    info->relative_end_x = client->mouse_current_x + scroll->scroll_h_visual - self->real_bounds.x;
    info->relative_end_y = client->mouse_current_y + -scroll->scroll_v_visual - self->real_bounds.y;
//    printf("info->relative_end_x = %d, info->relative_end_y = %d\n", info->relative_end_x, info->relative_end_y);
    
    Bounds selection_rect;
    selection_rect.x = std::min(info->relative_initial_x, info->relative_end_x);
    selection_rect.y = std::min(info->relative_initial_y, info->relative_end_y);
    selection_rect.w = std::abs(info->relative_end_x - info->relative_initial_x);
    selection_rect.h = std::abs(info->relative_end_y - info->relative_initial_y);
    selection_rect.x += scroll->real_bounds.x + scroll->scroll_h_visual;
    selection_rect.y += scroll->real_bounds.y + scroll->scroll_v_visual;

//    printf("selection_rect.x: %f, selection_rect.y: %f, selection_rect.w: %f, selection_rect.h: %f\n", selection_rect.x, selection_rect.y, selection_rect.w, selection_rect.h);
    
    for (auto c: scroll->content->children) {
        auto label = (EditableSelectableLabel *) c;
        update_selections(label, info, client, selection_rect);
    }
}

static void selection_drag_end(AppClient *client, cairo_t *cr, Container *self) {
    selection_drag(client, cr, self);
}

static void selection_click(AppClient *client, cairo_t *cr, Container *self) {
    auto scroll = (ScrollContainer *) self;
    auto info = (SelectionInfo *) scroll->user_data;
    
    long current_time = get_current_time_in_ms();
    long delta_time = current_time - info->last_click_time_ms;
    info->last_click_time_ms = current_time;
    
    
    if (delta_time < 400) {
        info->click_type++;
        if (info->click_type > 3) {
            info->click_type = 1;
        }
    } else {
        info->click_type = 1;
    }
    
    if (info->click_type == 1) {
        remove_all_selections(scroll);
        printf("single click\n");
    } else if (info->click_type == 2) {
        printf("       double click\n");
        // select word at click location
        
        int pos_x = client->mouse_current_x + scroll->scroll_h_visual - self->real_bounds.x;
        int pos_y = client->mouse_current_y + -scroll->scroll_v_visual - self->real_bounds.y;
        
        for (auto c: scroll->content->children) {
            auto label = (EditableSelectableLabel *) c;
            
            // Relative position inside scroll pane
            Bounds layout_normalized_bounds(label->real_bounds.x - label->parent->real_bounds.x,
                                            label->real_bounds.y - label->parent->real_bounds.y,
                                            label->real_bounds.w, label->real_bounds.h);
            
            if (bounds_contains(layout_normalized_bounds, pos_x, pos_y)) {
                PangoLayout *layout =
                        get_cached_pango_font(client->cr, label->font, label->size, label->weight);
                
                pango_layout_set_text(layout, label->text.data(), label->text.size());
                pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
                pango_layout_set_width(layout, label->real_bounds.w * PANGO_SCALE);
                
                int mouse_x = pos_x - layout_normalized_bounds.x;
                int mouse_y = pos_y - layout_normalized_bounds.y;
                int start_index;
                int start_trail;
                pango_layout_xy_to_index_custom(layout, mouse_x * PANGO_SCALE, mouse_y * PANGO_SCALE, &start_index,
                                                &start_trail);
                
                const char &ch = label->text[start_index];
                
                
                printf("text: '%c'\n", ch);
                
                break;
            }
        }
        
        
    } else if (info->click_type == 3) {
        printf("              triple click\n");
        
        // select paragraph at click location
    }
    
}

static void paint_selection(AppClient *client, cairo_t *cr, Container *self) {
    return;
    auto scroll = (ScrollContainer *) self;
    auto info = (SelectionInfo *) scroll->user_data;
    
    Bounds rect;
    rect.x = std::min(info->relative_initial_x, info->relative_end_x);
    rect.y = std::min(info->relative_initial_y, info->relative_end_y);
    rect.w = abs(info->relative_end_x - info->relative_initial_x);
    rect.h = std::abs(info->relative_end_y - info->relative_initial_y);
    
    rect.x += scroll->real_bounds.x + scroll->scroll_h_visual;
    rect.y += scroll->real_bounds.y + scroll->scroll_v_visual;
    
    draw_colored_rect(client, ArgbColor(1, 0, 0, 1), rect);
}

static void textfield_key_event(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                       char string[64],
                       uint16_t mods, xkb_key_direction direction) {
    printf("%s\n", string);
    
}


static void
paint_textarea_parent(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        if (data->state->text.empty() && !container->active) {
            PangoLayout *text_layout = get_cached_pango_font(
                    client->cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
            std::string text("Write the days events here");
            pango_layout_set_text(text_layout, text.c_str(), text.length());
            PangoRectangle text_ink;
            PangoRectangle text_logical;
            pango_layout_get_extents(text_layout, &text_ink, &text_logical);
            
            set_argb(cr, config->color_date_text_prompt);
            cairo_move_to(cr,
                          container->real_bounds.x - (text_ink.x / PANGO_SCALE) +
                          container->real_bounds.w / 2 - (text_ink.width / PANGO_SCALE) / 2,
                          container->real_bounds.y - (text_ink.y / PANGO_SCALE) +
                          container->real_bounds.h / 2 - (text_ink.height / PANGO_SCALE) / 2);
            pango_cairo_show_layout(cr, text_layout);
        } else {
            draw_margins_rect(client, config->color_date_seperator, container->real_bounds, 1, -2);
        }
    }
}

static void fill_root(AppClient *client) {
    if (client->root->user_data == nullptr) {
        client->root->user_data = new RootData();
    }
    
    for (auto child: client->root->children)
        delete child;
    client->root->children.clear();

//    client->root->child(FILL_SPACE, 200 * config->dpi);
//    client->root->wanted_pad = Bounds(100 * config->dpi,
//                                      100 * config->dpi,
//                                      100 * config->dpi,
//                                      100 * config->dpi);
    client->root->type = ::hbox;
    
    auto left = client->root->child(260 * config->dpi, FILL_SPACE);
    left->before_layout = before_left_layout;
    left->when_paint = paint_left_background;
    auto left_top = left->child(FILL_SPACE, 52 * config->dpi);
    left_top->when_paint = paint_add_new_button;
    left_top->when_clicked = clicked_add_new_button;
    auto left_bottom = left->child(FILL_SPACE, FILL_SPACE);
    ScrollPaneSettings converstaion_scroll_settings(config->dpi);
//    converstaion_scroll_settings.right_inline_track = true;
    auto left_scroll = make_newscrollpane_as_child(left_bottom, converstaion_scroll_settings);
    left_scroll->content->name = "left_content";
    for (int i = 100; i >= 0; i--) {
        auto conversation_button = left_scroll->content->child(FILL_SPACE, 48 * config->dpi);
        auto data = new ConversationData;
        data->index = i;
        conversation_button->when_paint = paint_conversation_button;
        conversation_button->user_data = data;
        conversation_button->when_clicked = clicked_conversation_button;
    }
    
    auto right = client->root->child(FILL_SPACE, FILL_SPACE);
    
    
    ScrollPaneSettings tabs_settings(config->dpi);
    tabs_settings.bottom_show_amount = ScrollShow::SNever;
//    tabs_settings.bottom_inline_track = false;
    auto tabs_container = right->child(FILL_SPACE, 38 * config->dpi);
    tabs_container->name = "tabs";
    tabs_container->when_paint = paint_bottom_right_background;
    auto tabs_scroll = make_newscrollpane_as_child(tabs_container, tabs_settings);
    tabs_scroll->content->type = ::hbox;
    
    auto tab = tabs_scroll->content->child(200 * config->dpi, FILL_SPACE);
    auto data = new TabData;
    data->selected = true;
    data->index = 0;
    tab->when_paint = paint_tab;
    tab->user_data = data;
    tab->when_clicked = clicked_tab;
    
    ScrollPaneSettings content_settings(config->dpi);
    auto scroll = make_newscrollpane_as_child(right, content_settings);
    scroll->when_mouse_down = selection_mouse_down;
    scroll->when_drag = selection_drag;
    scroll->when_drag_end = selection_drag_end;
    scroll->when_drag_end_is_click = false;
    scroll->minimum_x_distance_to_move_before_drag_begins = std::round(1 * config->dpi);
    scroll->when_clicked = selection_click;
    scroll->user_data = new SelectionInfo;
    scroll->when_paint = paint_selection;
//    scroll->before_layout = content_before_layout;
    
    Container *content = scroll->content;
    content->spacing = 20 * config->dpi;
    content->wanted_pad = Bounds(20 * config->dpi, 20 * config->dpi, 20 * config->dpi, 20 * config->dpi);
    content->name = "main_content";
    scroll->receive_events_even_if_obstructed = true;
    content->receive_events_even_if_obstructed = true;
    scroll->when_paint = paint_background;

//    content->when_paint = paint_background;

//    for (int i = 0; i < 8; ++i) {
//        auto textfield_label = new EditableSelectableLabel();
//        textfield_label->wanted_bounds.w = FILL_SPACE;
//        textfield_label->wanted_bounds.h = 0;
//        textfield_label->before_layout = on_label_layout;
//        textfield_label->parent = content;
//        textfield_label->type = ::vbox;
//        textfield_label->client = client;
//        content->children.push_back(textfield_label);
//        textfield_label->size = 10 * config->dpi;
//        textfield_label->text = "\n\nLorem ipsum dolor sit amet, consectetur adipiscing elit. Duis convallis eleifend tortor, nec feugiat lacus dapibus at. Curabitur lacinia eleifend tincidunt. Nulla hendrerit nunc risus, quis efficitur nulla cursus eu. Donec consectetur nisi a tincidunt imperdiet. Nullam in iaculis nulla. Phasellus nibh lorem, fermentum id malesuada sed, sagittis in odio. Duis erat leo, pharetra a nulla nec, ornare lobortis est. Ut ut ipsum et nisi commodo aliquet. Sed vehicula dolor tincidunt, facilisis neque et, sagittis elit. Duis in sapien nibh. Integer finibus rutrum pellentesque. Donec tortor ligula, malesuada id facilisis et, elementum ac ligula. Ut posuere eget arcu nec ornare. Donec scelerisque odio ut quam scelerisque, sed venenatis felis aliquet. Nunc rutrum in libero sodales tristique. Etiam ultrices tristique enim, nec vestibulum ligula rutrum in.\n"
//                      "\n"
//                      "Integer laoreet maximus purus eget euismod. Aliquam blandit eros metus, sed egestas erat bibendum id. Donec scelerisque odio libero. Suspendisse id nunc posuere, tempus ex non, aliquam ligula. Aenean at fermentum nisi. Sed elit felis, placerat a accumsan sed, consequat sed mi. Quisque eu finibus leo. Curabitur convallis massa ac purus viverra sollicitudin. Curabitur nec malesuada arcu, eu imperdiet lectus. Quisque ac pretium tellus. Aliquam ut interdum justo. Curabitur pulvinar purus ut eros suscipit semper. Phasellus quam turpis, feugiat luctus viverra sit amet, pellentesque varius quam.\n"
//                      "\n"
//                      "Pellentesque vestibulum quam in ante gravida, a scelerisque nulla vehicula. Vestibulum aliquam sapien lectus, in sagittis felis aliquam sed. Duis eu ante sollicitudin nulla mollis tincidunt eu eu libero. Fusce volutpat pulvinar neque, quis molestie arcu consequat at. Donec consectetur lacus sit amet euismod pharetra. Etiam vestibulum nulla quis molestie finibus. Fusce lacinia tincidunt sem a molestie. Nullam semper orci justo, at elementum diam pellentesque vitae. Fusce a est sit amet erat venenatis ultrices ut at nunc. Praesent finibus sagittis congue. Curabitur vel laoreet justo. Donec quis nibh a risus convallis imperdiet et sit amet mi. Donec feugiat est non justo cursus laoreet. Nullam vel molestie magna. Aliquam ut convallis metus. Duis vehicula vestibulum elementum. ";
//        textfield_label->when_paint = paint_label;
//    }
    
    auto bottom_right = right->child(FILL_SPACE, 70 * config->dpi);
    bottom_right->when_paint = paint_bottom_right_background;
    bottom_right->wanted_pad.x = 20 * config->dpi;
    bottom_right->wanted_pad.w = 20 * config->dpi;
    bottom_right->before_layout = text_field_before_layout;
    
    auto area = bottom_right->child(FILL_SPACE, FILL_SPACE);
    area->when_paint = paint_left_background;
//    area->wanted_pad.y = 20 * config->dpi;
//    area->wanted_pad.h = 20 * config->dpi;
    area->when_paint = paint_bottom_right_background;
    area->child(FILL_SPACE, 10 * config->dpi);
    
    TextAreaSettings textarea_settings(config->dpi);
    textarea_settings.color = config->color_date_text;
    textarea_settings.color_cursor = config->color_date_cursor;
    textarea_settings.font = config->font;
    textarea_settings.font_size__ = 11 * config->dpi;
    textarea_settings.wrap = true;
    auto textarea = make_textarea(app, client, area, textarea_settings);
    textarea->name = "main_text_area";
    textarea->parent->when_paint = paint_textarea_parent;
    
//    ScrollPaneSettings textfield_settings(config->dpi);
////    textfield_settings.right_inline_track = true;
////    textfield_settings.right_show_amount = ScrollShow::SNever;
//    auto textfield_scrollpane = make_newscrollpane_as_child(area, textfield_settings);
//    textfield_scrollpane->name = "textfield_scrollpane";
//    textfield_scrollpane->when_mouse_down = selection_mouse_down;
//    textfield_scrollpane->when_drag = selection_drag;
//    textfield_scrollpane->when_drag_end = selection_drag_end;
//    textfield_scrollpane->when_drag_end_is_click = false;
//    textfield_scrollpane->minimum_x_distance_to_move_before_drag_begins = std::round(1 * config->dpi);
//    textfield_scrollpane->when_clicked = selection_click;
//    textfield_scrollpane->user_data = new SelectionInfo;
//    textfield_scrollpane->when_paint = paint_selection;
////    textfield_scrollpane->content->wanted_pad.x = 10 * config->dpi;
////    textfield_scrollpane->content->wanted_pad.w = 10 * config->dpi;
////    textfield_scrollpane->content->wanted_pad.y = 10 * config->dpi;
//
//    auto textfield_label = new EditableSelectableLabel();
//    textfield_label->name = "textfield";
//    textfield_label->wanted_bounds.w = FILL_SPACE;
//    textfield_label->wanted_bounds.h = 0;
//    textfield_label->editable = true;
////    textfield_label->before_layout = on_label_layout;
//    textfield_label->parent = textfield_scrollpane->content;
//    textfield_label->type = ::vbox;
//    textfield_label->client = client;
//    textfield_scrollpane->content->children.push_back(textfield_label);
//    textfield_label->size = 11 * config->dpi;
//    textfield_label->text = "Hello you this is an expermient\nasdfasdffasdfa\nasdfasdfasdf\nasdfasdfasdf\nasdfasdfasdfasdf df as dfa sdfasdfas dfasdf\n to see if one line things";
//    textfield_label->when_paint = paint_textfield_label;
//    textfield_label->when_key_event = textfield_key_event;
    
    
    area->child(FILL_SPACE, 10 * config->dpi);
}

static void load_api_key(AppClient *client) {
    char *home = getenv("HOME");
    std::string path = std::string(home) + "/.config/winbar/chatgpt.txt";
    
    // read file at path to std::string
    auto file = fopen(path.data(), "r");
    if (!file) {
        api_key = "";
        fill_root(client);
        return;
    }
    
    char buffer[1024 * 4];
    size_t len = fread(buffer, 1, 1024 * 4, file);
    fclose(file);
    
    std::string key = std::string(buffer, len);
    
    // trim all whitespace
    key.erase(std::remove_if(key.begin(), key.end(), isspace), key.end());
    
    api_key = key;
    
    fill_root(client);
}

void start_chatgpt_menu() {
    if (auto *client = client_by_name(app, "chatgpt_menu")) {
        xcb_window_t active_window = get_active_window();
    
        if (active_window == client->window && client->mapped) {
            client_hide(app, client);
            return;
        }
    
        load_api_key(client);
    
        client_layout(app, client);
        client_layout(app, client);
        client_layout(app, client);
        client_show(app, client);
        xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_window_t window = client->window;
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
        return;
    }
    
    double w = 800;
    double h = 600;
    for (auto s: screens) {
        if (s->is_primary) {
            w = s->width_in_pixels * .7;
            h = s->height_in_pixels * .7;
            break;
        }
    }
    
    Settings settings;
    settings.w = w;
    settings.h = h;
    settings.skip_taskbar = true;
    settings.on_close_is_unmap = true;
    auto *client = client_new(app, settings, "chatgpt_menu");
    std::string title = "ChatGPT";
    // set title of window
    xcb_change_property(app->connection,
                        XCB_PROP_MODE_REPLACE,
                        client->window,
                        get_cached_atom(app, "_NET_WM_NAME"),
                        get_cached_atom(app, "UTF8_STRING"),
                        8,
                        title.size(),
                        title.c_str());
    load_api_key(client);
    client_layout(app, client);
    client_layout(app, client);
    client_layout(app, client);
    client_show(app, client);
}
