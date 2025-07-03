//
// Created by jmanc3 on 10/18/23.
//

#include <pango/pangocairo.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <utility>
#include <any>
#include <unordered_set>
#include "settings_menu.h"
#include "main.h"
#include "config.h"
#include "components.h"
#include "windows_selector.h"
#include "utility.h"
#include "simple_dbus.h"
#include "search_menu.h"
#include "drawer.h"
#include "dpi.h"
#include "icons.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

WinbarSettings *winbar_settings = new WinbarSettings;

void merge_order_with_taskbar();

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, ArgbColor(.941, .957, .976, 1), container->real_bounds); 
    client_layout(app, client);
//    draw_colored_rect(client, ArgbColor(.953, .953, .953, 1), container->real_bounds); 
//    draw_colored_rect(client, correct_opaqueness(client, config->color_pinned_icon_editor_background), container->real_bounds); 
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto label = (Label *) container->user_data;
    int size = label->size;
    if (size == -1)
        size = 9;
    
    auto color = label->color;
    if (label->color.a == 0) {
        color = config->color_pinned_icon_editor_field_default_text;
    }
    
    draw_text(client, size * config->dpi, config->font, EXPAND(color), label->text, container->real_bounds, 5, 0);
}

static void paint_draggable(AppClient *client, cairo_t *cr, Container *container) {
    float dot_size = 3 * config->dpi;
    float height = dot_size * 5; // three real and 2 spaces between
    float start_x = container->real_bounds.x + container->real_bounds.w / 2 - dot_size / 2 - dot_size;
    float start_y = container->real_bounds.y + container->real_bounds.h / 2 - height / 2;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
                draw_colored_rect(client, config->color_pinned_icon_editor_field_default_border, Bounds(start_x + (dot_size * x) + (dot_size * x),
                                start_y + (dot_size * y) + (dot_size * y),
                                dot_size,
                                dot_size)); 
        }
    }
    cairo_fill(cr);
}

static void clicked_on_off(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (Checkbox *) container->user_data;
    data->on = !data->on;
    
    for (auto &c: winbar_settings->taskbar_order) {
        if (c.name == data->name) {
            c.on = data->on;
            break;
        }
    }
    merge_order_with_taskbar();
}

static void paint_on_off(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (Checkbox *) container->user_data;
    
    ArgbColor color;
    color = config->color_pinned_icon_editor_field_default_border;
    if (container->state.mouse_hovering)
        color = config->color_pinned_icon_editor_field_hovered_border;
    if (container->state.mouse_pressing)
        color = config->color_pinned_icon_editor_field_pressed_border;
    if (data->on)
        color = config->color_pinned_icon_editor_field_pressed_border;
    
    float stroke = 0.0f;
    if (!data->on) {
        stroke = std::round(1 * config->dpi);
    }
    
    float size = 14 * config->dpi;
    draw_round_rect(client, ArgbColor(.984, .988, .992, 1), 
                        Bounds(container->real_bounds.x + container->real_bounds.w / 2 - size / 2,
                               container->real_bounds.y + container->real_bounds.h / 2 - size / 2,
                               size,
                               size), 5 * config->dpi, 0);
    
    rounded_rect(client, 2 * config->dpi,
                 container->real_bounds.x + container->real_bounds.w / 2 - size / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - size / 2,
                 size, size, color, stroke);
    
    if (!data->on)
        return;
    
    auto [c_f, c_w, c_h] = draw_text_begin(client, 14 * config->dpi, config->icons, 1, 1, 1, 1, "\uF13E");
    double kern_y = -1.0 * config->dpi;
    c_f->draw_text_end(container->real_bounds.x + container->real_bounds.w * .5 - size * .5,
                       container->real_bounds.y + container->real_bounds.h / 2 - c_h / 2 + kern_y);
}

static void paint_remove(AppClient *client, cairo_t *cr, Container *container) {
    draw_text(client, 10 * config->dpi, config->icons, .8, .3, .1, 1, "\uE107", container->real_bounds);
}

static void clicked_remove_reorderable(AppClient *client, cairo_t *cr, Container *container) {
    auto containers = container->parent->parent->children;
    auto it = std::find(containers.begin(), containers.end(), container->parent);
    
    if (it != containers.end()) {
        size_t current_index = std::distance(containers.begin(), it);
        
        container->parent->parent->children.erase(container->parent->parent->children.begin() + current_index);
        client_layout(app, client);
    }
}

struct Drag : UserData {
    int initial_mouse_click_before_drag_offset_y = 0;
};

static void dragged_list_start(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (Drag *) container->user_data;
    data->initial_mouse_click_before_drag_offset_y = container->real_bounds.y - client->mouse_initial_y;
    for (auto c: container->parent->children)
        c->z_index = 0;
    container->z_index = 20;
}

static void paint_combox_item(AppClient *client, cairo_t *cr, Container *container) {
    auto *label = (Label *) container->user_data;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        auto color = darken(config->color_pinned_icon_editor_background, 7);
        if (container->state.mouse_pressing) {
            color = darken(config->color_pinned_icon_editor_background, 15);
        }
        draw_colored_rect(client, color, container->real_bounds);
    }
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_pinned_icon_editor_field_default_text),label->text, container->real_bounds, 5, container->wanted_pad.x + 12 * config->dpi);
}

void move_container_to_index(std::vector<Container *> &containers, Container *container, int wants_i) {
    auto it = std::find(containers.begin(), containers.end(), container);
    
    if (it != containers.end()) {
        size_t current_index = std::distance(containers.begin(), it);
        
        if (current_index != static_cast<size_t>(wants_i)) {
            if (current_index < wants_i) {
                // Move forward in the vector.
                for (size_t i = current_index; i < wants_i; ++i) {
                    containers[i] = containers[i + 1];
                }
            } else {
                // Move backward in the vector.
                for (size_t i = current_index; i > static_cast<size_t>(wants_i); --i) {
                    containers[i] = containers[i - 1];
                }
            }
            
            // Finally, place 'container' at the new index 'wants_i'.
            containers[wants_i] = container;
        }
    }
}

static void dragged_list_item(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (Drag *) container->user_data;
    
    // parent layout
    layout(client, cr, container->parent, container->parent->real_bounds);
    double max_y = container->parent->children[container->parent->children.size() - 1]->real_bounds.y;
    
    float drag_position_y = client->mouse_current_y + data->initial_mouse_click_before_drag_offset_y;
    bool wants_different = false;
    int wants_i;
    for (int i = 0; i < container->parent->children.size(); i++) {
        auto c = container->parent->children[i];
        if (c == container) // don't replace self
            continue;
        float half_height = c->real_bounds.h / 2;
        float c_middle = c->real_bounds.y + half_height;
        if (std::abs(c_middle - (drag_position_y + half_height)) < half_height) {
            wants_different = true;
            wants_i = i;
            break;
        }
    }
    if (wants_different) {
        move_container_to_index(container->parent->children, container, wants_i);
        layout(client, cr, container->parent, container->parent->real_bounds);
    }
    
    // put drag item where it is
    container->real_bounds.y = drag_position_y;
    
    double min_y = container->parent->real_bounds.y;
    if (container->real_bounds.y < min_y)
        container->real_bounds.y = min_y;
    if (container->real_bounds.y > max_y)
        container->real_bounds.y = max_y;
    
    // internal layout
    layout(client, cr, container, container->real_bounds);
}

static void dragged_list_item_end(AppClient *client, cairo_t *cr, Container *container) {
    layout(client, cr, container->parent, container->parent->real_bounds);
    
    Container *reorder_list = container_by_name("reorder_list", client->root);
    std::sort(winbar_settings->taskbar_order.begin(), winbar_settings->taskbar_order.end(),
              [reorder_list](const TaskbarItem &first, const TaskbarItem &second) {
                  int first_index = 1000;
                  int second_index = 1000;
                  for (int i = 0; i < reorder_list->children.size(); ++i) {
                    auto *label = (Label *) reorder_list->children[i]->children[2]->user_data;
                    if (label->text == first.name) {
                        first_index = i;
                        break;
                    }
                  }
                  for (int i = 0; i < reorder_list->children.size(); ++i) {
                      auto *label = (Label *) reorder_list->children[i]->children[2]->user_data;
                      if (label->text == second.name) {
                          second_index = i;
                          break;
                      }
                  }
                  return first_index < second_index;
              });
    for (int i = 0; i < winbar_settings->taskbar_order.size(); i++)
        winbar_settings->taskbar_order[i].target_index = i;
    merge_order_with_taskbar();
}

bool invalidate_item_pierce(Container *container, int mouse_x, int mouse_y) {
    // If "combo" or on_off, container is hovered, parent shouldn't be 'pierced'
    if (auto c = container_by_name("combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("icon_combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("size_field", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("date_combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("style_combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("battery_expands_combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (auto c = container_by_name("volume_expands_combo", container))
        if (bounds_contains(c->real_bounds, mouse_x, mouse_y))
            return false;
    if (bounds_contains(container->children[1]->real_bounds, mouse_x, mouse_y))
        return false;
    return bounds_contains(container->real_bounds, mouse_x, mouse_y);
}

void when_size_field_key_event(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                               char string[64],
                               uint16_t mods, xkb_key_direction direction) {
    key_event_textfield(client, cr, self, is_string, keysym, string, mods, direction);
    auto *field_data = (FieldData *) self->user_data;
    try {
        int size = std::atoi(field_data->text.c_str());
        if (size < 4) {
            size = 4;
        } else if (size > 60 * config->dpi) {
            size = 60 * config->dpi;
        }
        winbar_settings->date_size = size;
    } catch (...) {
    
    }
}

static void add_item(Container *reorder_list, std::string n, bool on_off_state) {
    auto r = reorder_list->child(::hbox, FILL_SPACE, 32 * config->dpi);
    r->when_paint = paint_reordable_item;
    r->receive_events_even_if_obstructed_by_one = true;
//    r->clip = true;
    r->when_drag_start = dragged_list_start;
    r->when_drag = dragged_list_item;
    r->when_drag_end = dragged_list_item_end;
    r->when_drag_end_is_click = false;
    r->user_data = new Drag;
    r->handles_pierced = invalidate_item_pierce;
    
    auto drag = r->child(r->wanted_bounds.h * .8, FILL_SPACE);
    drag->wanted_pad.x = r->wanted_bounds.h * .43;
    drag->when_paint = paint_draggable;
    
    if (n == "Space") {
        auto remove = r->child(r->wanted_bounds.h * .8, FILL_SPACE);
        remove->wanted_pad.x = r->wanted_bounds.h * .43;
        remove->when_clicked = clicked_remove_reorderable;
        remove->when_paint = paint_remove;
    } else {
        auto on_off = r->child(r->wanted_bounds.h * .8, FILL_SPACE);
        on_off->wanted_pad.x = r->wanted_bounds.h * .43;
        on_off->when_clicked = clicked_on_off;
        on_off->when_paint = paint_on_off;
        auto check = new Checkbox;
        check->name = n;
        check->on = on_off_state;
        on_off->user_data = check;
    }
    
    auto taskbar = client_by_name(app, "taskbar");
    PangoLayout *layout = get_cached_pango_font(taskbar->cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, n.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    auto label = r->child(::hbox, width + r->wanted_bounds.h * .8, FILL_SPACE);
    label->wanted_pad.x = r->wanted_bounds.h * .4;
    label->when_paint = paint_label;
    auto data = new Label(n);
    label->user_data = data;
    
    if (n == "Search Field") {
        auto combo_data = new GenericComboBox("combo", "Behaviour: ");
        combo_data->options.emplace_back("Default");
        combo_data->options.emplace_back("Fully Hidden");
        combo_data->options.emplace_back("Fully Disabled");
        combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
            if (winbar_settings->search_behaviour == "Default") {
                return "Default";
            } else if (winbar_settings->search_behaviour == "Fully Hidden") {
                return "Fully Hidden";
            }
            return "Fully Disabled";
        };
        combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
            if (((Label *) (self->user_data))->text == "Default") {
                winbar_settings->search_behaviour = "Default";
            } else if (((Label *) (self->user_data))->text == "Fully Hidden") {
                winbar_settings->search_behaviour = "Fully Hidden";
            } else if (((Label *) (self->user_data))->text == "Fully Disabled") {
                winbar_settings->search_behaviour = "Fully Disabled";
            }
            client_close_threaded(app, client);
            merge_order_with_taskbar();
        };
        
        std::string longest_text = "Behaviour: Fully Disabled";
        pango_layout_set_text(layout, longest_text.c_str(), -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        auto combobox = r->child(width * 1.5, FILL_SPACE);
        combobox->name = combo_data->name;
        combobox->when_clicked = clicked_expand_generic_combobox;
        combobox->when_paint = paint_generic_combobox;
        combobox->user_data = combo_data;
    } else if (n == "Pinned Icons") {
        {
            auto combo_data = new GenericComboBox("combo", "Alignment: ");
            combo_data->options.emplace_back("Left");
            combo_data->options.emplace_back("Right");
            combo_data->options.emplace_back("Screen Center");
            combo_data->options.emplace_back("Container Center");
            combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
                if (winbar_settings->icons_alignment == container_alignment::ALIGN_LEFT) {
                    return "Left";
                } else if (winbar_settings->icons_alignment == container_alignment::ALIGN_RIGHT) {
                    return "Right";
                } else if (winbar_settings->icons_alignment == container_alignment::ALIGN_GLOBAL_CENTER_HORIZONTALLY) {
                    return "Screen Center";
                }
                return "Container Center";
            };
            combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
                if (((Label *) (self->user_data))->text == "Left") {
                    winbar_settings->icons_alignment = container_alignment::ALIGN_LEFT;
                } else if (((Label *) (self->user_data))->text == "Right") {
                    winbar_settings->icons_alignment = container_alignment::ALIGN_RIGHT;
                } else if (((Label *) (self->user_data))->text == "Screen Center") {
                    winbar_settings->icons_alignment = container_alignment::ALIGN_GLOBAL_CENTER_HORIZONTALLY;
                } else if (((Label *) (self->user_data))->text == "Container Center") {
                    winbar_settings->icons_alignment = container_alignment::ALIGN_CENTER_HORIZONTALLY;
                }
                client_close_threaded(app, client);
                merge_order_with_taskbar();
            };
            
            std::string longest_text = "Alignment: Container Center";
            pango_layout_set_text(layout, longest_text.c_str(), -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            
            auto combobox = r->child(width * 1.5, FILL_SPACE);
            combobox->name = combo_data->name;
            combobox->when_clicked = clicked_expand_generic_combobox;
            combobox->when_paint = paint_generic_combobox;
            combobox->user_data = combo_data;
        }
        r->child(14 * config->dpi, FILL_SPACE);
        {
            auto combo_data = new GenericComboBox("icon_combo", "Style: ");
            combo_data->options.emplace_back("Windows 10");
            combo_data->options.emplace_back("Windows 7");
            combo_data->options.emplace_back("Windows 7 Flat");
            combo_data->options.emplace_back("Windows 11");
            combo_data->options.emplace_back("Mac OS");
            combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
                if (winbar_settings->pinned_icon_style == "win7") {
                    return "Windows 7";
                } if (winbar_settings->pinned_icon_style == "win7flat") {
                    return "Windows 7 Flat";
                } else if (winbar_settings->pinned_icon_style == "win11") {
                    return "Windows 11";
                } else if (winbar_settings->pinned_icon_style == "macos") {
                    return "Mac OS";
                }
                return "Windows 10";
            };
            combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
                winbar_settings->click_icon_tab_next_window = false;
                if (((Label *) (self->user_data))->text == "Windows 10") {
                    winbar_settings->pinned_icon_style = "win10";
                } else if (((Label *) (self->user_data))->text == "Windows 7") {
                    winbar_settings->pinned_icon_style = "win7";
                } else if (((Label *) (self->user_data))->text == "Windows 7 Flat") {
                    winbar_settings->pinned_icon_style = "win7flat";
                } else if (((Label *) (self->user_data))->text == "Windows 11") {
                    winbar_settings->pinned_icon_style = "win11";
                } else if (((Label *) (self->user_data))->text == "Mac OS") {
                    winbar_settings->pinned_icon_style = "macos";
                    winbar_settings->click_icon_tab_next_window = true;
                }
                client_close_threaded(app, client);
                merge_order_with_taskbar();
            };
            
            std::string longest_text = "Style: Windows 7 Flat";
            pango_layout_set_text(layout, longest_text.c_str(), -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            
            auto combobox = r->child(width * 1.5, FILL_SPACE);
            combobox->name = combo_data->name;
            combobox->when_clicked = clicked_expand_generic_combobox;
            combobox->when_paint = paint_generic_combobox;
            combobox->user_data = combo_data;
        }
    } else if (n == "Date") {
        {
            auto combo_data = new GenericComboBox("date_combo", "Alignment: ");
            combo_data->options.emplace_back("Left");
            combo_data->options.emplace_back("Right");
            combo_data->options.emplace_back("Center");
            combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
                if (winbar_settings->date_alignment == PangoAlignment::PANGO_ALIGN_RIGHT) {
                    return "Right";
                } else if (winbar_settings->date_alignment == PangoAlignment::PANGO_ALIGN_CENTER) {
                    return "Center";
                }
                return "Left";
            };
            combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
                if (((Label *) (self->user_data))->text == "Left") {
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_LEFT;
                } else if (((Label *) (self->user_data))->text == "Right") {
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_RIGHT;
                } else if (((Label *) (self->user_data))->text == "Center") {
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
                }
                client_close_threaded(app, client);
                if (auto c = client_by_name(app, "taskbar")) {
                    client_layout(app, c);
                    request_refresh(app, c);
                }
            };
            
            std::string longest_text = "Alignment: Center";
            pango_layout_set_text(layout, longest_text.c_str(), -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            
            auto combobox = r->child(width * 1.5, FILL_SPACE);
            combobox->name = combo_data->name;
            combobox->when_clicked = clicked_expand_generic_combobox;
            combobox->when_paint = paint_generic_combobox;
            combobox->user_data = combo_data;
        }
        r->child(14 * config->dpi, FILL_SPACE);
        {
            auto combo_data = new GenericComboBox("style_combo", "Style: ");
            combo_data->options.emplace_back("Windows 10");
            combo_data->options.emplace_back("Windows 11");
            combo_data->options.emplace_back("Windows 11 Detailed");
            combo_data->options.emplace_back("Windows 11 Minimal");
            combo_data->options.emplace_back("Windows Vista");
            combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
                if (winbar_settings->date_style == "windows 10") {
                    return "Windows 10";
                } else if (winbar_settings->date_style == "windows 11") {
                    return "Windows 11";
                } else if (winbar_settings->date_style == "windows 11 detailed") {
                    return "Windows 11 Detailed";
                } else if (winbar_settings->date_style == "windows 11 minimal") {
                    return "Windows 11 Minimal";
                } else if (winbar_settings->date_style == "windows vista") {
                    return "Windows Vista";
                }
                return "Windows 10";
            };
            combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
                if (((Label *) (self->user_data))->text == "Windows 10") {
                    winbar_settings->date_style = "windows 10";
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
                } else if (((Label *) (self->user_data))->text == "Windows 11") {
                    winbar_settings->date_style = "windows 11";
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_RIGHT;
                } else if (((Label *) (self->user_data))->text == "Windows 11 Detailed") {
                    winbar_settings->date_style = "windows 11 detailed";
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_RIGHT;
                } else if (((Label *) (self->user_data))->text == "Windows 11 Minimal") {
                    winbar_settings->date_style = "windows 11 minimal";
                    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_RIGHT;
                } else if (((Label *) (self->user_data))->text == "Windows Vista") {
                    winbar_settings->date_style = "windows vista";
                }
                client_close_threaded(app, client);
                update_time(app, client, nullptr, nullptr);
                if (auto c = client_by_name(app, "taskbar")) {
                    client_layout(app, c);
                    request_refresh(app, c);
                }
            };
            
            std::string longest_text = "Style: Windows 11 Detailed";
            pango_layout_set_text(layout, longest_text.c_str(), -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            
            auto combobox = r->child(width * 1.5, FILL_SPACE);
            combobox->name = combo_data->name;
            combobox->when_clicked = clicked_expand_generic_combobox;
            combobox->when_paint = paint_generic_combobox;
            combobox->user_data = combo_data;
        }
        
        pango_layout_set_text(layout, "   Font Size: ", -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        label = r->child(::hbox, width, FILL_SPACE);
        label->wanted_pad.x = r->wanted_bounds.h * .4;
        label->when_paint = paint_label;
        data = new Label("   Font Size: ");
        label->user_data = data;
        
        {
            auto parent = r->child(38 * config->dpi, FILL_SPACE);
//            parent->clip = true;
            parent->wanted_pad.y = 3 * config->dpi;
            parent->wanted_pad.h = 2 * config->dpi;
            FieldSettings field_settings;
            field_settings.only_numbers = true;
            field_settings.font_size = std::round(9 * config->dpi);
            field_settings.max_size = 3;
            auto field = make_textfield(parent, field_settings, FILL_SPACE, FILL_SPACE);
            field->when_key_event = when_size_field_key_event;
            auto *data = (FieldData *) field->user_data;
            data->text = std::to_string(winbar_settings->date_size);
            field->name = "size_field";
        }
    } else if (n == "Volume") {
        auto combo_data = new GenericComboBox("volume_expands_combo", "Label: ");
        combo_data->options.emplace_back("Always");
        combo_data->options.emplace_back("On Hover");
        combo_data->options.emplace_back("Never");
        combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
            if (winbar_settings->volume_label_always_on) {
                return "Always";
            } else {
                return winbar_settings->volume_expands_on_hover ? "On Hover" : "Never";
            }
        };
        combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
            std::string text = ((Label *) (self->user_data))->text;
            if (text == "Always") {
                winbar_settings->volume_label_always_on = true;
            } else {
                winbar_settings->volume_label_always_on = false;
                winbar_settings->volume_expands_on_hover = text == "On Hover";
            }
            client_close_threaded(app, client);
            request_refresh(app, client_by_name(app, "taskbar"));
        };
        
        std::string longest_text = "Label: On Hover";
        pango_layout_set_text(layout, longest_text.c_str(), -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        auto combobox = r->child(width * 1.5, FILL_SPACE);
        combobox->name = combo_data->name;
        combobox->when_clicked = clicked_expand_generic_combobox;
        combobox->when_paint = paint_generic_combobox;
        combobox->user_data = combo_data;
    } else if (n == "Battery") {
        auto combo_data = new GenericComboBox("battery_expands_combo", "Label: ");
        combo_data->options.emplace_back("Always");
        combo_data->options.emplace_back("On Hover");
        combo_data->options.emplace_back("Never");
        combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
            if (winbar_settings->battery_label_always_on) {
                return "Always";
            } else {
                return winbar_settings->battery_expands_on_hover ? "On Hover" : "Never";
            }
        };
        combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
            std::string text = ((Label *) (self->user_data))->text;
            if (text == "Always") {
                winbar_settings->battery_label_always_on = true;
            } else {
                winbar_settings->battery_label_always_on = false;
                winbar_settings->battery_expands_on_hover = text == "On Hover";
            }
            client_close_threaded(app, client);
            request_refresh(app, client_by_name(app, "taskbar"));
        };
        
        std::string longest_text = "Label: On Hover";
        pango_layout_set_text(layout, longest_text.c_str(), -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        auto combobox = r->child(width * 1.5, FILL_SPACE);
        combobox->name = combo_data->name;
        combobox->when_clicked = clicked_expand_generic_combobox;
        combobox->when_paint = paint_generic_combobox;
        combobox->user_data = combo_data;
    }
}

static void clicked_add_spacer(AppClient *client, cairo_t *cr, Container *container) {
    add_item(container_by_name("reorder_list", client->root), "Space", true);
    client_layout(app, client);
}

static void paint_centered_text(AppClient *client, cairo_t *cr, Container *container) {
    auto *label = (Label *) container->user_data;
    paint_reordable_item(client, cr, container);
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_pinned_icon_editor_field_default_text),label->text, container->real_bounds);
}

void merge_order_with_taskbar() {
    for (const auto &item: winbar_settings->taskbar_order) {
        if (item.name == "Bluetooth") {
            winbar_settings->bluetooth_enabled = item.on;
        }
    }
    std::sort(winbar_settings->taskbar_order.begin(), winbar_settings->taskbar_order.end(),
              [](const TaskbarItem &first, const TaskbarItem &second) {
                  return first.target_index < second.target_index;
              });
    
    auto taskbar = client_by_name(app, "taskbar");
    if (!taskbar)
        return;

#define ADD(button_name, container_name) if (s.name == button_name) { \
       if (auto container = container_by_name(container_name, taskbar->root)) { \
       container->exists = s.on; \
       containers.push_back(container); \
       continue; } \
    }
    
    std::vector<Container *> containers;
    for (const auto &s: winbar_settings->taskbar_order) {
        ADD("Super", "super")
        ADD("Search Field", "field_search")
        ADD("Workspace", "workspace")
        ADD("Pinned Icons", "icons")
        if (s.name == "Systray") {
            // Add all containers whose name ends with .plugin
            for (auto c: taskbar->root->children)
                if (c->name.find(".plugin") != std::string::npos)
                    containers.push_back(c);
        }
        ADD("Systray", "systray")
        ADD("Bluetooth", "bluetooth")
        ADD("Wifi", "wifi")
        ADD("Battery", "battery")
        ADD("Volume", "volume")
        ADD("Date", "date")
        ADD("Notifications", "action")
        ADD("Show Desktop", "minimize")
    }
    for (int i = 0; i < containers.size(); i++) {
        auto already = containers[i];
        if (already->name == "systray") {
            for (auto c: taskbar->root->children) {
                if (c->name.find("frozen") != std::string::npos) {
                    c->exists = !slept.empty();
                    containers.insert(containers.begin() + i, c);
                    goto out;
                }
            }
        }
    }
    out:
    
    taskbar->root->children.clear();
    for (auto c: containers) {
        taskbar->root->children.push_back(c);
    }
    // No matter what, bluetooth does not exist until dbus says it does.
    container_by_name("bluetooth", taskbar->root)->exists = false;
    container_by_name("icons", taskbar->root)->alignment = winbar_settings->icons_alignment;
    
    client_layout(app, taskbar);
    request_refresh(app, taskbar);
}

static void clicked_reset(AppClient *client, cairo_t *, Container *) {
    auto reorder_list = container_by_name("reorder_list", client->root);
    for (auto c: reorder_list->children)
        delete c;
    reorder_list->children.clear();
    std::vector<std::string> names = {"Super", "Search Field", "Workspace", "Pinned Icons",
                                      "Systray", "Bluetooth", "Battery", "Wifi",
                                      "Volume", "Date", "Notifications", "Show Desktop"};
    for (auto n: names) {
        add_item(reorder_list, n, true);
    }
    winbar_settings->taskbar_order.clear();
    for (int i = 0; i < names.size(); ++i) {
        TaskbarItem item;
        item.name = names[i];
        item.on = true;
        item.target_index = i;
        winbar_settings->taskbar_order.push_back(item);
    }
    winbar_settings->search_behaviour = "Default";
    winbar_settings->icons_alignment = container_alignment::ALIGN_LEFT;
    winbar_settings->pinned_icon_style = "win10";
    winbar_settings->bluetooth_enabled = true;
    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
    winbar_settings->date_style = "windows 11 detailed";
    winbar_settings->date_size = 9;
    winbar_settings->start_menu_height = 641;
    winbar_settings->extra_live_tile_pages = 0;
    if (auto *c = client_by_name(app, "settings_menu")) {
        if (auto *con = container_by_name("size_field", c->root)) {
            auto *field_data = (FieldData *) con->user_data;
            field_data->text = std::to_string(winbar_settings->date_size);
        }
    }
    update_time(app, client, nullptr, nullptr);
    client_layout(app, client);
    merge_order_with_taskbar();
    save_settings_file();
}

struct SelectedLabel : public Label {
    bool selected = false;
    int index = 0;
    
    explicit SelectedLabel(const std::string &text) : Label(text) {}
};

void title(std::string text, Container *container) {
    container->wanted_pad = Bounds(24 * config->dpi, 62 * config->dpi, 24 * config->dpi, 24 * config->dpi);
    {
        auto title = container->child(FILL_SPACE, 20 * config->dpi);
        auto data = new Label(text);
        data->size = 20;
        title->when_paint = paint_label;
        title->user_data = data;
    }
    container->child(FILL_SPACE, 32 * config->dpi);
}

struct OriginalBool : public UserData {
    bool *boolean = nullptr;
    void (*on_clicked)() = nullptr;
    
    explicit OriginalBool(bool *boolean) : boolean(boolean) {}
};

void bool_checkbox_indent(const std::string &text, int indent, bool *boolean, Container *container, AppClient *client, void (*on_clicked)() = nullptr) {
    float font_size = std::round(10);
    float pad = std::round(6 * config->dpi);
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, font_size * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.length());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    float size = 14 * config->dpi; // of checkbox that is
    auto full_label_container = container->child(layout_type::hbox,
                                                 size + pad + width + indent, 32 * config->dpi);
    full_label_container->alignment = ALIGN_CENTER;
    full_label_container->wanted_pad.x = indent;
    full_label_container->spacing = pad;
    auto orig_bool = new OriginalBool(boolean);
    full_label_container->user_data = orig_bool;
    orig_bool->on_clicked = on_clicked;
    
    auto check = full_label_container->child(size, size);
    auto data = new Checkbox;
    check->user_data = data;
    data->on = *boolean;
    check->when_paint = paint_on_off;
    
    auto label = full_label_container->child(FILL_SPACE, FILL_SPACE);
    auto label_data = new Label(text);
    label_data->size = font_size;
    label->when_paint = paint_label;
    label->user_data = label_data;
    
    full_label_container->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
        auto ours = (OriginalBool *) container->user_data;
        auto data = (Checkbox *) container->children[0]->user_data;
        data->on = !data->on;
        if (ours->boolean != nullptr)
            *ours->boolean = data->on;
        if (ours->boolean == &winbar_settings->super_icon_default) {
            request_refresh(app, client_by_name(app, "taskbar"));
        }
        if (ours->boolean == &winbar_settings->label_uniform_size) {
            request_refresh(app, client_by_name(app, "taskbar"));
        }
        if (ours->boolean == &winbar_settings->labels) {
            label_change(client_by_name(app, "taskbar"));
        }
        save_settings_file();
        if (ours->on_clicked)
            ours->on_clicked();
    };
    full_label_container->receive_events_even_if_obstructed_by_one = true;
}

void bool_checkbox(const std::string &text, bool *boolean, Container *container, AppClient *client) {
    bool_checkbox_indent(text, 0, boolean, container, client);
}

struct SliderInfo : UserData {
    int start = 0;
    int end = 100;
    int incr = 1;
    bool is_perc = true;
    float value = 0;
    int *target = nullptr;
    bool *disabled_by = nullptr;
    
    void (*callback)(SliderInfo *) = nullptr;
};

void
slider(const std::string &text, int indent, int *target, Container *container, AppClient *client, int start, int end,
       int incr, bool is_perc, bool *disabled_by, void (*callback)(SliderInfo *) = nullptr) {
    float font_size = std::round(10);
    float pad = std::round(12 * config->dpi);
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, font_size * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.length());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    float slider_width = 200 * config->dpi;
    float size = 14 * config->dpi; // of checkbox that is
    auto full_label_container = container->child(layout_type::hbox, FILL_SPACE, 32 * config->dpi);
    //full_label_container->alignment = ALIGN_CENTER;
    full_label_container->wanted_pad.x = indent;
    full_label_container->spacing = pad;

    auto label = full_label_container->child(width, FILL_SPACE);
    auto label_data = new Label(text);
    label_data->size = font_size;
    label->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        auto *data = (Label *) container->user_data;
        ArgbColor text_color = config->color_pinned_icon_editor_field_default_text;
        auto c = container->parent->children[1];
        auto slider_info = (SliderInfo *) c->user_data;
        if (slider_info->disabled_by && *slider_info->disabled_by) {
            text_color.a = .4f;
        }
        data->color = text_color;
        paint_label(client, cr, container);
    };

    label->user_data = label_data;
    
    auto slider_container = full_label_container->child(slider_width, FILL_SPACE);
    auto slider_info = new SliderInfo;
    slider_info->callback = callback;
    slider_info->is_perc = is_perc;
    slider_container->user_data = slider_info;
    slider_info->target = target;
    // TODO: the value is being set wrong here
    slider_info->value = (*target) - 1.0f;
    slider_info->start = start;
    slider_info->end = end;
    slider_info->incr = incr;
    slider_info->disabled_by = disabled_by;
    
    slider_container->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        auto slider_info = (SliderInfo *) container->user_data;
        //draw_colored_rect(client, ArgbColor(.4, .4, .4, .8), container->real_bounds);
        //draw_round_rect(client, color, Bounds(x, y, width, height), corner_radius, stroke_w);
        auto foreground = config->color_volume_slider_foreground;
        if (slider_info->disabled_by && *slider_info->disabled_by) {
            foreground = config->color_volume_slider_background;
        }
        
        // draw slider background line
        double line_height = 2 * config->dpi;
        draw_colored_rect(client, config->color_volume_slider_background, Bounds(container->real_bounds.x,
                container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                container->real_bounds.w, line_height));
        // value can be between .5 to 3
        float marker_position =
                ((((float) slider_info->value) - slider_info->start) / (slider_info->end - slider_info->start)) *
                container->real_bounds.w;
        draw_colored_rect(client, foreground, Bounds(container->real_bounds.x,
                container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                marker_position, line_height));
        
        double marker_height = 24 * config->dpi;
        double marker_width = 8 * config->dpi;
        ArgbColor color = foreground;
        rounded_rect(client,
             4 * config->dpi,
             container->real_bounds.x + marker_position - marker_width / 2,
             container->real_bounds.y + container->real_bounds.h / 2 - marker_height / 2,
             marker_width,
             marker_height, color);
    };
    slider_container->when_drag = [](AppClient *client, cairo_t *cr, Container *container) {
        auto slider_info = (SliderInfo *) container->user_data;
        if (slider_info->disabled_by && *slider_info->disabled_by)
            return; // early out
 
        // TODO: check mouse position in reference to container real_bounds.x and real_bounds.w and then store that in the label, which can then be applied later
        
        int x_offset_within_slider = client->mouse_current_x;
        if (x_offset_within_slider < container->real_bounds.x) {
            x_offset_within_slider = container->real_bounds.x;
        } else if (x_offset_within_slider > container->real_bounds.x + container->real_bounds.w) {
            x_offset_within_slider = container->real_bounds.x + container->real_bounds.w;
        }
        x_offset_within_slider -= container->real_bounds.x; // because we're trying to extract the scalar, we need to remove the left indent offset
        float value = x_offset_within_slider / container->real_bounds.w;
        if (value < 0) {
            value = 0;
        } else if (value > 1) {
            value = 1;
        }
        float notch_count = slider_info->incr;
        float notch = std::round(value * notch_count);
        value = notch / notch_count;
        
        slider_info->value = value * (slider_info->end - slider_info->start) + slider_info->start;
        
        bool found_me = false;
        for (auto c: container->parent->children) {
            if (found_me) {
                auto data = (Label *) c->user_data;
                data->text = std::to_string((int) std::round(slider_info->value)) + (slider_info->is_perc ? "%" : "");
                break;
            }
            if (c == container) {
                found_me = true;
            }
        }
        if (slider_info->callback)
            slider_info->callback(slider_info);
    };
    slider_container->when_mouse_down = slider_container->when_drag;
        
    int perc_width;
    int perc_height;
    pango_layout_set_text(layout, "100%", -1);
    pango_layout_get_pixel_size_safe(layout, &perc_width, &perc_height);
    
    auto scale_label = full_label_container->child(perc_width, FILL_SPACE);
    auto s = std::to_string((int) std::round(*target)) + (slider_info->is_perc ? "%" : "");
    auto scale_data = new Label(s);
    scale_data->size = font_size;
    scale_label->when_paint = label->when_paint;
    scale_label->user_data = scale_data;
    
    int apply_width;
    int apply_height;
    pango_layout_set_text(layout, "Apply", 5);
    pango_layout_get_pixel_size_safe(layout, &apply_width, &apply_height);

    auto apply_label = full_label_container->child(apply_width * 1.8f, FILL_SPACE);
    auto apply_data = new Label("Apply");
    apply_data->size = font_size;
    apply_label->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
        auto label = (Label *) container->user_data;
        int size = label->size;
        ArgbColor color = config->color_pinned_icon_editor_field_default_text;
        
        auto c = container->parent->children[1];
        auto slider_info = (SliderInfo *) c->user_data;
        if (slider_info->disabled_by && *slider_info->disabled_by) {
            color.a = .4f;
        }

        draw_text(client, size * config->dpi, config->font, EXPAND(color), label->text, container->real_bounds, 5);
        
        if (slider_info->disabled_by && *slider_info->disabled_by)
            return; // early out
            
        if (container->state.mouse_hovering || container->state.mouse_pressing) {
            color = ArgbColor(.4, .4, .4, .5);
            if (container->state.mouse_pressing) {
                color = config->color_volume_slider_foreground;
            }
            float pad = 3 * config->dpi;
            Bounds smaller = Bounds(container->real_bounds.x + pad, container->real_bounds.y + pad,
                                    container->real_bounds.w - pad * 2, container->real_bounds.h - pad * 2);
            draw_round_rect(client, color, smaller, 4 * config->dpi, 1.25 * config->dpi);
        }
    };
    apply_label->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
        auto c = container->parent->children[1];
        auto slider_info = (SliderInfo *) c->user_data;
        if (slider_info->disabled_by && *slider_info->disabled_by)
            return; // early out
        *slider_info->target = slider_info->value;
        save_settings_file();
        client->app->running = false;
        restart = true;
    };
    apply_label->user_data = apply_data;
}

static void paint_textarea_border(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor color;
    if (container->state.mouse_hovering || container->state.mouse_pressing || container->active) {
        if (container->state.mouse_pressing || container->active) {
            if (container->state.mouse_pressing && container->active) {
                color = lighten(config->color_pinned_icon_editor_field_pressed_border, 7);
            } else {
                color = config->color_pinned_icon_editor_field_pressed_border;
            }
        } else {
            color = config->color_pinned_icon_editor_field_hovered_border;
        }
    } else {
        color = config->color_pinned_icon_editor_field_default_border;
    }
    float size = 14 * config->dpi;
    //draw_colored_rect(client, ArgbColor(.984, .988, .992, 1), container->real_bounds);
    //draw_margins_rect(client, color, container->real_bounds, 2, 0);
    
    draw_round_rect(client, ArgbColor(.984, .988, .992, 1), container->real_bounds, 5 * config->dpi, 0);
    draw_round_rect(client, color, container->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
}

struct PathHolder : public UserData {
    explicit PathHolder(std::string *path) : path(path) {}
    
    std::string *path = nullptr;
};

void string_textfield(const std::string &text, std::string *path, Container *container, AppClient *client, std::string prompt = "") {
    int font_size = std::round(10);
    auto hbox = container->child(::hbox, FILL_SPACE, 26 * config->dpi);
    hbox->user_data = new PathHolder(path);
    hbox->alignment = ALIGN_CENTER;
    
    auto label = hbox->child(FILL_SPACE, FILL_SPACE);
    auto label_data = new Label(text);
    label_data->size = font_size;
    label->when_paint = paint_label;
    label->user_data = label_data;
    
    // TODO: because this happens before the context is created, it can't be replaced by our new system yet
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, font_size * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    label->wanted_bounds.w = width;
    
    TextAreaSettings settings(config->dpi);
    settings.single_line = true;
    settings.bottom_show_amount = 2;
    settings.right_show_amount = 2;
    settings.prompt = std::move(prompt);
    settings.color_prompt = ArgbColor(.1, .1, .1, .5);
    settings.font_size__ = 11 * config->dpi;
    settings.color = config->color_pinned_icon_editor_field_default_text;
    settings.color_cursor = config->color_pinned_icon_editor_cursor;
    settings.pad = Bounds(4 * config->dpi, 5 * config->dpi, 8 * config->dpi, 2 * config->dpi);
    Container *textarea = make_textarea(app, client, hbox, settings);
    auto *data = (TextAreaData *) textarea->user_data;
    data->state->text = *path;
    textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *container, bool is_string, xkb_keysym_t keysym,
                                  char string[64],
                                  uint16_t mods, xkb_key_direction direction) {
        if (direction == XKB_KEY_UP) {
            return;
        }
        if (container->parent->active || container->active) {
            textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
            auto *data = (TextAreaData *) container->user_data;
            auto *holder = (PathHolder *) container->parent->parent->parent->user_data;
            *holder->path = data->state->text;
        }
    };
    textarea->parent->alignment = ALIGN_CENTER;
    textarea->parent->when_paint = paint_textarea_border;
}

struct SettingBoolData : UserData {
    std::string icon;
    std::string title;
    std::string description;
    std::string *target_str = nullptr;
    bool *target = nullptr;
    bool *existance_target = nullptr;
    bool squares_up = false;
    long open_start = 0;
    long close_start = 0;
    double open_amount = 0;
    double slide_amount = 0;
    double slide_open = 0;
    bool dragging = false;
    float position_scalar_read_only = 0;
    double drag_start_x = 0;
    void (*on_change)() = nullptr;
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
};

struct SettingComboBoxData : UserData {
    std::string icon;
    std::string title;
    std::string description;
    std::string *target_str = nullptr;
    bool *existance_target = nullptr;
    bool squares_up = false;
    long open_start = 0;
    long close_start = 0;
    double open_amount = 0;
    double slide_amount = 0;
    double slide_open = 0;
    bool dragging = false;
    float position_scalar_read_only = 0;
    double drag_start_x = 0;
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
    
    std::string *target = nullptr;
    std::vector<std::string> options;
    std::string selected_option;
    
    void (*on_change)(std::string option) = nullptr;
};

static void
setting_bool(Container *container, std::string icon, std::string title, std::string description, bool *target, bool squares_up = false,
             void (*on_change)() = nullptr) {
    auto full_label_container = container->child(layout_type::hbox, FILL_SPACE, 69 * config->dpi);
    auto data = new SettingBoolData;
    data->icon = icon;
    data->title = title;
    data->description = description;
    data->target = target;
    data->on_change = on_change;
    data->squares_up = squares_up;
    full_label_container->user_data = data;
    full_label_container->minimum_x_distance_to_move_before_drag_begins = 4 * config->dpi;
    full_label_container->minimum_y_distance_to_move_before_drag_begins = 4 * config->dpi;
    full_label_container->when_drag_start = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (SettingBoolData *) c->user_data;
        auto check_bounds = Bounds(c->real_bounds.x + c->real_bounds.w - 65 * config->dpi,
                                       c->real_bounds.y + c->real_bounds.h / 2 - 10 * config->dpi,
                                       40 * config->dpi, 20 * config->dpi);
        if (bounds_contains(check_bounds, client->mouse_current_x, client->mouse_current_y)) {
            data->dragging = true;
            data->drag_start_x = client->mouse_initial_x;
        }
    };
    full_label_container->when_drag_end_is_click = false;
    full_label_container->when_drag_end = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (SettingBoolData *) c->user_data;
        data->dragging = false;
        *data->target = data->position_scalar_read_only > .5;
        if (data->on_change)
            data->on_change();
        data->slide_amount = data->position_scalar_read_only;
    };
    
    full_label_container->when_clicked = [](AppClient *client, cairo_t *cr, Container *c)
    {
        auto data = (SettingBoolData *) c->user_data;
        if (data->target != nullptr) {
            *data->target = !(*data->target);
            if (data->on_change)
                data->on_change();
        }
        save_settings_file();
        request_refresh(app, client_by_name(app, "taskbar"));
   };
    
    full_label_container->when_paint = [](AppClient *client, cairo_t *cr, Container *c)
    {
        Bounds backup = c->real_bounds;
        c->real_bounds.x += .5;
        c->real_bounds.y += .5;
        c->real_bounds.w -= 1;
        c->real_bounds.h -= 1;

        auto data = (SettingBoolData *) c->user_data;
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h - 6 * config->dpi));
            }
        }
        
        // bg
        ArgbColor bg_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            bg_color = ArgbColor(.965, .965, .965, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        } else {
            //draw_round_rect(client, ArgbColor(.984, .984, .984, 1), c->real_bounds, 5 * config->dpi, 0);
            bg_color = ArgbColor(.984, .988, .992, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        }
        
        // border
        ArgbColor border_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            border_color = ArgbColor(.818, .818, .818, .75);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            int height = 3 * config->dpi;
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - height, c->real_bounds.w, height));
            if (!c->state.mouse_pressing) {
                border_color = ArgbColor(.75, .75, .75, 1);
                draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            }
            draw_clip_end(client);
        } else {
            border_color = ArgbColor(.818, .818, .818, 1);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
       }
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_end(client);
                
                draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - 6 * config->dpi, c->real_bounds.w, 6 * config->dpi));
                draw_round_rect(client, bg_color, c->real_bounds, 0, 0);
                draw_round_rect(client, border_color, c->real_bounds, 0, std::floor(1.0 * config->dpi));
                draw_clip_end(client);
            }
        }

        // icon
        if (!data->icon.empty()) {
            draw_text(client, 16 * config->dpi, config->icons, EXPAND(config->color_pinned_icon_editor_field_default_text), data->icon, c->real_bounds, -5, 19 * config->dpi);
        }
        
        static int size_title = 11;
        static int size_description = 9;
        int kern_y_off = 3 * config->dpi;
       
        auto color = config->color_pinned_icon_editor_field_default_text;
        // d = description
        auto [d_f, d_w, d_h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->description);
        d_f->end();
        
        draw_clip_begin(client, c->real_bounds);
        color.a = .92;
        auto [f, w, h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->title);
        float total_height = h + d_h;
        f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - total_height / 2 + kern_y_off);
        
        color.a = .7;
        // d_r = description real (in terms of drawing the text)
        auto [d_r_f, d_r_w, d_r_h] = draw_text_begin(client, size_description * config->dpi, config->font, EXPAND(color), data->description);
        d_r_f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - total_height / 2 + h + kern_y_off);
        draw_clip_end(client);
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_begin(client, c->real_bounds);
                // TODO:??????
                draw_clip_end(client);
            }
        }
        
        // On off text
        std::string on_off = "Off";
        if (data->target != nullptr && *data->target) {
            on_off = "On";
        }
        color.a = .9;
        draw_text(client, 11 * config->dpi, config->font, EXPAND(color), on_off, c->real_bounds, -5, c->real_bounds.w - 94 * config->dpi);
        
        // Check
        auto check_bounds = Bounds(c->real_bounds.x + c->real_bounds.w - 65 * config->dpi,
                                   c->real_bounds.y + c->real_bounds.h / 2 - 10 * config->dpi,
                                   40 * config->dpi, 20 * config->dpi);
        float circ_size_small = 12;
        float circ_size_big = 14;
        float circ_anim_time = 70;
        
        bool sliding = already_began(client, &data->slide_amount, -1);
        if (bounds_contains(check_bounds, client->mouse_current_x, client->mouse_current_y) || c->state.mouse_pressing || sliding) {
            if (!already_began(client, &data->slide_open, 1))
                client_create_animation(app, client, &data->slide_open, data->lifetime, 0, circ_anim_time, nullptr, 1);
        } else {
            if (!already_began(client, &data->slide_open, 0))
                client_create_animation(app, client, &data->slide_open, data->lifetime, 0, circ_anim_time, nullptr, 0);
        }
        float circ_extra_width = 5.5 * data->slide_open;
        

        if (bounds_contains(c->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            if (!already_began(client, &data->open_amount, 1))
                client_create_animation(app, client, &data->open_amount, data->lifetime, 0, circ_anim_time, nullptr, 1);
        } else {
            if (!already_began(client, &data->open_amount, 0))
                client_create_animation(app, client, &data->open_amount, data->lifetime, 0, circ_anim_time, nullptr, 0);
        }
        float circ_size = circ_size_small + (circ_size_big - circ_size_small) * data->open_amount;
        float left = check_bounds.x + 4 * config->dpi - ((circ_size - circ_size_small) * .5) * config->dpi;
        float right = check_bounds.x + check_bounds.w - circ_size * config->dpi + ((circ_size - circ_size_small) * .5) * config->dpi - 4 * config->dpi - circ_extra_width * data->open_amount;
        float position = (right - left) * data->slide_amount  + left;
        if (data->dragging) {
            auto diff = client->mouse_current_x - data->drag_start_x;
            position += diff;
            if (position < left)
                position = left;
            if (position > right)
                position = right;
            data->position_scalar_read_only = ((position - left) / (right - left));
        }
        if (on_off == "On") {
            if (!already_began(client, &data->slide_amount, 1))
                client_create_animation(app, client, &data->slide_amount, data->lifetime, 0, circ_anim_time * .5, nullptr, 1);

            auto accent = config->color_search_accent;
            draw_round_rect(client, accent, check_bounds, 10 * config->dpi, 0);
            
            check_bounds.x = position;
            check_bounds.y = check_bounds.y + check_bounds.h / 2 - (circ_size * .5) * config->dpi;
            check_bounds.w = circ_size * config->dpi + circ_extra_width * data->open_amount;
            check_bounds.h = circ_size * config->dpi;
            draw_round_rect(client, ArgbColor(1, 1, 1, 1), check_bounds, (circ_size * .5) * config->dpi);
        } else {
            if (!already_began(client, &data->slide_amount, 0))
                client_create_animation(app, client, &data->slide_amount, data->lifetime, 0, circ_anim_time * .5, nullptr, 0);

            draw_round_rect(client, ArgbColor(.537, .537, .537, 1), check_bounds, 10 * config->dpi, std::floor(1.0 * config->dpi));
            
            check_bounds.x = position;
            check_bounds.y = check_bounds.y + check_bounds.h / 2 - (circ_size * .5) * config->dpi;
            check_bounds.w = circ_size * config->dpi + circ_extra_width * data->open_amount;
            check_bounds.h = circ_size * config->dpi;
            draw_round_rect(client, ArgbColor(.365, .365, .365, 1), check_bounds, (circ_size * .5) * config->dpi);
        }
        c->real_bounds = backup;
        //draw_round_rect(client, ArgbColor(.965, .965, .965, 1), c->real_bounds, 5 * config->dpi, 0);
    };
}

static void
setting_bool_attachment(Container *scroll_root, std::string icon, std::string title, std::string desc, bool *target, bool *existance_target, bool squared) {
    // Conditional existance
    //auto dd = scroll_root->child(FILL_SPACE, 30 * config->dpi + 20 * config->dpi);
    auto data = new SettingBoolData;
    data->icon = icon;
    data->title = title;
    data->description = desc;
    data->target = target;
    data->existance_target = existance_target;
    data->squares_up = squared;
    
    auto dd = scroll_root->child(FILL_SPACE, 0);
    dd->user_data = data;
    
    dd->pre_layout = [](AppClient *client, Container *dd, const Bounds &bounds) {
        auto data = (SettingBoolData *) dd->user_data;
        if (data->existance_target == nullptr) {
            dd->wanted_bounds.h = (69.0 * .70f) * config->dpi;
            dd->exists = true;
            return;
        }
        dd->exists = data->slide_amount != 0;
        
        float anim_time = 55;
        if (*data->existance_target) {
            if (!already_began(client, &data->slide_amount, 1))
                client_create_animation(app, client, &data->slide_amount, data->lifetime, 0, anim_time, nullptr, 1);
        } else {
            if (!already_began(client, &data->slide_amount, 0))
                client_create_animation(app, client, &data->slide_amount, data->lifetime, 0, anim_time, nullptr, 0);
        }
        
        dd->wanted_bounds.h = ((69.0 * .70f) * config->dpi) * data->slide_amount;
    };
    
    dd->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (SettingBoolData *) c->user_data;
        *data->target = !*data->target;
        save_settings_file();
        request_refresh(app, client_by_name(app, "taskbar"));
    };
    
    dd->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        Bounds backup = c->real_bounds;
        c->real_bounds.x += .5;
        c->real_bounds.y += .5;
        c->real_bounds.w -= 1;
        c->real_bounds.h -= 1;
        auto data = (SettingBoolData *) c->user_data;
        auto bg_color = ArgbColor(.984, .988, .992, 1);
        draw_colored_rect(client, bg_color, c->real_bounds);
        
        auto border_color = ArgbColor(.818, .818, .818, 1);
        auto extended = Bounds(c->real_bounds.x, c->real_bounds.y - std::floor(1.0 * config->dpi), c->real_bounds.w, c->real_bounds.h + std::floor(1.0 * config->dpi) * 2);
        if (data->squares_up) {
            draw_round_rect(client, border_color, extended, 0, std::floor(1.0 * config->dpi));
        } else {
            // top part
            Bounds b = c->real_bounds;
            int halves = std::round(10 * config->dpi);
            draw_clip_begin(client, Bounds(b.x, b.y, b.w, b.h - halves));
            draw_round_rect(client, border_color, extended, 0, std::floor(1.0 * config->dpi));
            draw_clip_end(client);
            
            // bottom part
            draw_clip_begin(client, Bounds(b.x, b.y + b.h - halves, b.w, halves + 10));
            draw_round_rect(client, border_color, extended, 5 * config->dpi, std::floor(1.0 * config->dpi));
            draw_clip_end(client);
        }
        
        static int size_title = 10;
        static int size_description = 9;
        int kern_y_off = 3 * config->dpi;
        kern_y_off = 0;
 
        auto color = config->color_pinned_icon_editor_field_default_text;
        // d = description
        auto [d_f, d_w, d_h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->description);
        d_f->end();
        
        draw_clip_begin(client, c->real_bounds);
        color.a = .92;
        auto [f, w, h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->title);
        float total_height = h + d_h;
        f->draw_text_end(c->real_bounds.x + 87 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - h / 2 + kern_y_off);
        
        color = config->color_pinned_icon_editor_field_default_border;
        if (c->state.mouse_hovering)
            color = config->color_pinned_icon_editor_field_hovered_border;
        if (c->state.mouse_pressing)
            color = config->color_pinned_icon_editor_field_pressed_border;
        if (*data->target)
            color = config->color_pinned_icon_editor_field_pressed_border;
        
        float stroke = 0.0f;
        if (!*data->target) {
            stroke = std::round(1 * config->dpi);
        }
        
        float size = 18 * config->dpi;
        draw_round_rect(client, ArgbColor(.984, .988, .992, 1),
                        Bounds(c->real_bounds.x + 60 * config->dpi,
                               c->real_bounds.y + c->real_bounds.h / 2 - size / 2,
                               size,
                               size), 5 * config->dpi, 0);
        
        rounded_rect(client, 2 * config->dpi,
                     c->real_bounds.x + 60 * config->dpi,
                     c->real_bounds.y + c->real_bounds.h / 2 - size / 2,
                     size, size, color, stroke);
        
        if (*data->target) {
            auto [c_f, c_w, c_h] = draw_text_begin(client, 16 * config->dpi, config->icons, 1, 1, 1, 1, "\uF13E");
            double kern_x = .5 * config->dpi;
            double kern_y = -1.5 * config->dpi;
            c_f->draw_text_end(c->real_bounds.x + 60 * config->dpi + size / 2 - c_w / 2 + kern_x,
                               c->real_bounds.y + c->real_bounds.h / 2 - c_h / 2 + kern_y);
        }
        draw_clip_end(client);
        
        c->real_bounds = backup;
    };
}

void
setting_field(AppClient *client, Container *container, std::string icon, std::string title, std::string description, std::string *target_str, bool squared = false) {
//    setting_bool(container, icon, title, description, nullptr, false);
    auto full = container->child(layout_type::vbox, FILL_SPACE, (69 + 20) * config->dpi);
    auto data = new SettingBoolData;
    data->icon = icon;
    data->title = title;
    data->description = description;
    data->target_str = target_str;
    data->squares_up = squared;
    full->user_data = data;
    
    full->when_paint = [](AppClient *client, cairo_t *cr, Container *c)
    {
        Bounds backup = c->real_bounds;
        c->real_bounds.x += .5;
        c->real_bounds.y += .5;
        c->real_bounds.w -= 1;
        c->real_bounds.h -= 1;

        auto data = (SettingBoolData *) c->user_data;
        
        if (data->squares_up) {
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h - 6 * config->dpi));
        }
        
        // bg
        ArgbColor bg_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            bg_color = ArgbColor(.965, .965, .965, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        } else {
            //draw_round_rect(client, ArgbColor(.984, .984, .984, 1), c->real_bounds, 5 * config->dpi, 0);
            bg_color = ArgbColor(.984, .988, .992, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        }
        
        // border
        ArgbColor border_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            border_color = ArgbColor(.818, .818, .818, .75);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            int height = 3 * config->dpi;
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - height, c->real_bounds.w, height));
            if (!c->state.mouse_pressing) {
                border_color = ArgbColor(.75, .75, .75, 1);
                draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            }
            draw_clip_end(client);
        } else {
            border_color = ArgbColor(.818, .818, .818, 1);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
        }
        
        if (data->squares_up) {
            draw_clip_end(client);
            
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - 6 * config->dpi, c->real_bounds.w, 6 * config->dpi));
            draw_round_rect(client, bg_color, c->real_bounds, 0, 0);
            draw_round_rect(client, border_color, c->real_bounds, 0, std::floor(1.0 * config->dpi));
            draw_clip_end(client);
        }

        // icon
        if (!data->icon.empty()) {
            draw_text(client, 16 * config->dpi, config->icons, EXPAND(config->color_pinned_icon_editor_field_default_text), data->icon, c->real_bounds, -5, 19 * config->dpi);
        }
        
        static int size_title = 11;
        static int size_description = 9;
        int kern_y_off = 3 * config->dpi;
       
        auto color = config->color_pinned_icon_editor_field_default_text;
        // d = description
        auto [d_f, d_w, d_h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->description);
        d_f->end();
        
        draw_clip_begin(client, c->real_bounds);
        color.a = .92;
        auto [f, w, h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->title);
        float total_height = h + d_h;
        f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - (26 * config->dpi) / 2 - h - 1 * config->dpi);
        
        color.a = .7;
        // d_r = description real (in terms of drawing the text)
        auto [d_r_f, d_r_w, d_r_h] = draw_text_begin(client, size_description * config->dpi, config->font, EXPAND(color), data->description);
        d_r_f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h - d_r_h - 6 * config->dpi - 1 * config->dpi);
        draw_clip_end(client);
        
        c->real_bounds = backup;
        //draw_round_rect(client, ArgbColor(.965, .965, .965, 1), c->real_bounds, 5 * config->dpi, 0);
    };
    
    
    TextAreaSettings settings(config->dpi);
    settings.single_line = true;
    settings.bottom_show_amount = 2;
    settings.right_show_amount = 2;
    //settings.prompt = std::move(prompt);
    settings.color_prompt = ArgbColor(.1, .1, .1, .5);
    settings.font_size__ = 10 * config->dpi;
    settings.color = config->color_pinned_icon_editor_field_default_text;
    settings.color_cursor = config->color_pinned_icon_editor_cursor;
    settings.pad = Bounds(4 * config->dpi, 5 * config->dpi, 8 * config->dpi, 2 * config->dpi);
    full->child(FILL_SPACE, full->wanted_bounds.h / 2 - (26 * config->dpi) / 2 + 3 * config->dpi);
    auto field_parent = full->child(FILL_SPACE, 26 * config->dpi);
    field_parent->wanted_pad = Bounds(60 * config->dpi, 0, 25 * config->dpi, 0);
    
    Container *textarea = make_textarea(app, client, field_parent, settings);
    textarea->parent->skip_delete = true;
    textarea->parent->user_data = data;
    textarea->parent->when_paint = paint_textarea_border;
    textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *container, bool is_string, xkb_keysym_t keysym, char string[64], uint16_t mods, xkb_key_direction direction) {
        if (direction == XKB_KEY_UP) {
            return;
        }
        if (container->parent->active || container->active) {
            textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
            auto *data = (TextAreaData *) container->user_data;
            auto *holder = (SettingBoolData *) container->parent->user_data;
            *holder->target_str = data->state->text;
        }
    };

    auto tt = (TextAreaData *) textarea->user_data;
    tt->state->text = *target_str;
}

void text_input(std::string icon, std::string title, std::string default_value, std::string description, int indent, std::string *target_str, Container *container, AppClient *client, bool squared = false) {
    
    //    setting_bool(container, icon, title, description, nullptr, false);
    auto full = container->child(layout_type::vbox, FILL_SPACE, (69 + 20) * config->dpi);
    auto data = new SettingBoolData;
    data->icon = icon;
    data->title = title;
    data->description = description;
    data->target_str = target_str;
    data->squares_up = squared;
    full->user_data = data;
    
    full->when_paint = [](AppClient *client, cairo_t *cr, Container *c)
    {
        Bounds backup = c->real_bounds;
        c->real_bounds.x += .5;
        c->real_bounds.y += .5;
        c->real_bounds.w -= 1;
        c->real_bounds.h -= 1;
        
        auto data = (SettingBoolData *) c->user_data;
        
        if (data->squares_up) {
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w, c->real_bounds.h - 6 * config->dpi));
        }
        
        // bg
        ArgbColor bg_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            bg_color = ArgbColor(.965, .965, .965, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        } else {
            //draw_round_rect(client, ArgbColor(.984, .984, .984, 1), c->real_bounds, 5 * config->dpi, 0);
            bg_color = ArgbColor(.984, .988, .992, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        }
        
        // border
        ArgbColor border_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            border_color = ArgbColor(.818, .818, .818, .75);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            int height = 3 * config->dpi;
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - height, c->real_bounds.w, height));
            if (!c->state.mouse_pressing) {
                border_color = ArgbColor(.75, .75, .75, 1);
                draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            }
            draw_clip_end(client);
        } else {
            border_color = ArgbColor(.818, .818, .818, 1);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
        }
        
        if (data->squares_up) {
            draw_clip_end(client);
            
            draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - 6 * config->dpi, c->real_bounds.w, 6 * config->dpi));
            draw_round_rect(client, bg_color, c->real_bounds, 0, 0);
            draw_round_rect(client, border_color, c->real_bounds, 0, std::floor(1.0 * config->dpi));
            draw_clip_end(client);
        }
        
        // icon
        if (!data->icon.empty()) {
            draw_text(client, 16 * config->dpi, config->icons, EXPAND(config->color_pinned_icon_editor_field_default_text), data->icon, c->real_bounds, -5, 19 * config->dpi);
        }
        
        static int size_title = 11;
        static int size_description = 9;
        int kern_y_off = 3 * config->dpi;
        
        auto color = config->color_pinned_icon_editor_field_default_text;
        // d = description
        auto [d_f, d_w, d_h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->description);
        d_f->end();
        
        draw_clip_begin(client, c->real_bounds);
        color.a = .92;
        auto [f, w, h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->title);
        float total_height = h + d_h;
        f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h / 2 - (26 * config->dpi) / 2 - h - 1 * config->dpi);
        
        color.a = .7;
        // d_r = description real (in terms of drawing the text)
        auto [d_r_f, d_r_w, d_r_h] = draw_text_begin(client, size_description * config->dpi, config->font, EXPAND(color), data->description);
        d_r_f->draw_text_end(c->real_bounds.x + 60 * config->dpi, c->real_bounds.y + c->real_bounds.h - d_r_h - 6 * config->dpi - 1 * config->dpi);
        draw_clip_end(client);
        
        c->real_bounds = backup;
        //draw_round_rect(client, ArgbColor(.965, .965, .965, 1), c->real_bounds, 5 * config->dpi, 0);
    };
    
    
    TextAreaSettings settings(config->dpi);
    settings.single_line = true;
    settings.bottom_show_amount = 2;
    settings.right_show_amount = 2;
    //settings.prompt = std::move(prompt);
    settings.color_prompt = ArgbColor(.1, .1, .1, .5);
    settings.font_size__ = 10 * config->dpi;
    settings.color = config->color_pinned_icon_editor_field_default_text;
    settings.color_cursor = config->color_pinned_icon_editor_cursor;
    settings.prompt = default_value;
    ArgbColor prompt_color = config->color_pinned_icon_editor_field_default_text;
    prompt_color.a *= .4;
    settings.color_prompt = prompt_color;
    settings.pad = Bounds(4 * config->dpi, 5 * config->dpi, 8 * config->dpi, 2 * config->dpi);
    full->child(FILL_SPACE, full->wanted_bounds.h / 2 - (26 * config->dpi) / 2 + 3 * config->dpi);
    auto together_parent = full->child(::hbox, FILL_SPACE, 26 * config->dpi);
    together_parent->spacing = 8 * config->dpi;
    together_parent->wanted_pad = Bounds(60 * config->dpi, 0, 25 * config->dpi, 0);
    
    auto field_parent = together_parent->child(FILL_SPACE, FILL_SPACE);
    auto reset_button = together_parent->child(50 * config->dpi, FILL_SPACE);
    reset_button->user_data = new Label("Reset");
    reset_button->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (Label *) c->user_data;
        
        draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_pinned_icon_editor_field_default_text), data->text, c->real_bounds);
        
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            ArgbColor color = ArgbColor(.4, .4, .4, .5);
            if (c->state.mouse_pressing) {
                color = config->color_volume_slider_foreground;
            }
            draw_round_rect(client, color, c->real_bounds, 4 * config->dpi, 1.25 * config->dpi);
        }
    };
    reset_button->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        if (auto textarea = container_by_name("text_input_area", c->parent)) {
            auto *data = (TextAreaData *) textarea->user_data;
            data->state->text = "";
            data->state->cursor = 0;
            data->state->selection_x = -1;
            
            auto *holder = (SettingBoolData *) textarea->parent->user_data;
            *holder->target_str = data->state->text;
            
            save_settings_file();
            restart = true;
            app->running = false;
        }
    };
    auto apply_button = together_parent->child(50 * config->dpi, FILL_SPACE);
    apply_button->user_data = new Label("Apply");
    apply_button->when_paint = reset_button->when_paint;
    apply_button->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
        if (auto textarea = container_by_name("text_input_area", c->parent)) {
            auto *data = (TextAreaData *) textarea->user_data;
            auto *holder = (SettingBoolData *) textarea->parent->user_data;
            *holder->target_str = data->state->text;
            
            save_settings_file();
            restart = true;
            app->running = false;
        }
    };
    
    Container *textarea = make_textarea(app, client, field_parent, settings);
    textarea->parent->skip_delete = true;
    textarea->parent->user_data = data;
    textarea->parent->when_paint = paint_textarea_border;
    textarea->name = "text_input_area";
    textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *container, bool is_string, xkb_keysym_t keysym, char string[64], uint16_t mods, xkb_key_direction direction) {
        if (direction == XKB_KEY_UP) {
            return;
        }
        if (container->parent->active || container->active) {
            textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
            auto *data = (TextAreaData *) container->user_data;
            auto *holder = (SettingBoolData *) container->parent->user_data;
            *holder->target_str = data->state->text;
        }
    };
    auto tt = (TextAreaData *) textarea->user_data;
    tt->state->text = *target_str;
}


static void
setting_subheading(Container *container, std::string title) {
    auto heading = container->child(layout_type::hbox, FILL_SPACE, 69 * config->dpi);
    auto data = new Label(title);
    heading->user_data = data;
    heading->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (Label *) c->user_data;
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_pinned_icon_editor_field_default_text), data->text, true);
        f->draw_text_end(c->real_bounds.x, c->real_bounds.y + 40 * config->dpi);
    };
}

static void
setting_subheading_no_indent(Container *container, std::string title) {
    auto heading = container->child(layout_type::hbox, FILL_SPACE, 29 * config->dpi);
    auto data = new Label(title);
    heading->user_data = data;
    heading->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        auto data = (Label *) c->user_data;
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_pinned_icon_editor_field_default_text), data->text, true);
        f->draw_text_end(c->real_bounds.x, c->real_bounds.y);
    };
}

static void
fill_plugins_root(AppClient *client, Container *plugins_root) {

}

static void
setting_combobox(AppClient *client, Container *container, std::string icon, std::string title, std::string description,
                 std::vector<std::string> options, std::string *target, bool squares_up = false,
                 void (*on_change)(std::string option) = nullptr) {
    auto full_label_container = container->child(layout_type::hbox, FILL_SPACE, 69 * config->dpi);
    auto data = new SettingComboBoxData;
    data->icon = icon;
    data->title = title;
    data->description = description;
    data->target = target;
    data->on_change = on_change;
    data->squares_up = squares_up;
    full_label_container->user_data = data;
    full_label_container->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
        Bounds backup = c->real_bounds;
        c->real_bounds.x += .5;
        c->real_bounds.y += .5;
        c->real_bounds.w -= 1;
        c->real_bounds.h -= 1;
        
        auto data = (SettingBoolData *) c->user_data;
        
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y, c->real_bounds.w,
                                               c->real_bounds.h - 6 * config->dpi));
            }
        }
        
        // bg
        ArgbColor bg_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            bg_color = ArgbColor(.965, .965, .965, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        } else {
            //draw_round_rect(client, ArgbColor(.984, .984, .984, 1), c->real_bounds, 5 * config->dpi, 0);
            bg_color = ArgbColor(.984, .988, .992, 1);
            draw_round_rect(client, bg_color, c->real_bounds, 5 * config->dpi, 0);
        }
        
        // border
        ArgbColor border_color;
        if (c->state.mouse_hovering || c->state.mouse_pressing) {
            border_color = ArgbColor(.818, .818, .818, .75);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            int height = 3 * config->dpi;
            draw_clip_begin(client,
                            Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - height, c->real_bounds.w,
                                   height));
            if (!c->state.mouse_pressing) {
                border_color = ArgbColor(.75, .75, .75, 1);
                draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
            }
            draw_clip_end(client);
        } else {
            border_color = ArgbColor(.818, .818, .818, 1);
            draw_round_rect(client, border_color, c->real_bounds, 5 * config->dpi, std::floor(1.0 * config->dpi));
        }
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_end(client);
                
                draw_clip_begin(client, Bounds(c->real_bounds.x, c->real_bounds.y + c->real_bounds.h - 6 * config->dpi,
                                               c->real_bounds.w, 6 * config->dpi));
                draw_round_rect(client, bg_color, c->real_bounds, 0, 0);
                draw_round_rect(client, border_color, c->real_bounds, 0, std::floor(1.0 * config->dpi));
                draw_clip_end(client);
            }
        }
        
        if (!data->icon.empty()) {
            draw_text(client, 16 * config->dpi, config->icons,
                      EXPAND(config->color_pinned_icon_editor_field_default_text), data->icon, c->real_bounds, -5,
                      19 * config->dpi);
        }
        
        
        static int size_title = 11;
        static int size_description = 9;
        int kern_y_off = 3 * config->dpi;
        
        auto color = config->color_pinned_icon_editor_field_default_text;
        // d = description
        auto [d_f, d_w, d_h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color),
                                               data->description);
        d_f->end();
        
        draw_clip_begin(client, c->real_bounds);
        color.a = .92;
        auto [f, w, h] = draw_text_begin(client, size_title * config->dpi, config->font, EXPAND(color), data->title);
        float total_height = h + d_h;
        f->draw_text_end(c->real_bounds.x + 60 * config->dpi,
                         c->real_bounds.y + c->real_bounds.h / 2 - total_height / 2 + kern_y_off);
        
        color.a = .7;
        // d_r = description real (in terms of drawing the text)
        auto [d_r_f, d_r_w, d_r_h] = draw_text_begin(client, size_description * config->dpi, config->font,
                                                     EXPAND(color), data->description);
        d_r_f->draw_text_end(c->real_bounds.x + 60 * config->dpi,
                             c->real_bounds.y + c->real_bounds.h / 2 - total_height / 2 + h + kern_y_off);
        draw_clip_end(client);
        
        if (data->squares_up) {
            if (data->target != nullptr && *data->target) {
                draw_clip_begin(client, c->real_bounds);
                // TODO:??????
                draw_clip_end(client);
            }
        }
        
        c->real_bounds = backup;
    };
    
    full_label_container->alignment = ALIGN_RIGHT;
    auto combobox_parent = full_label_container->child(200 * config->dpi, FILL_SPACE);
    full_label_container->wanted_pad = Bounds(0, 15 * config->dpi, 20 * config->dpi, 15 * config->dpi);
    {
        auto combo_data = new GenericComboBox("color_mode_combobox", "");
        combo_data->options = options;
        
        combo_data->determine_selected = [](AppClient *client, cairo_t *cr, Container *self) -> std::string {
            if (auto s = (SettingComboBoxData *) self->parent->parent->user_data) {
                return *s->target;
            }
            return "";
        };
        combo_data->when_clicked = [](AppClient *client, cairo_t *cr, Container *self) -> void {
            std::string option = ((Label *) (self->user_data))->text;
            client_close_threaded(app, client);
            if (client->root->user_data) {
                if (auto data = (GenericComboBox *) client->root->user_data) {
                    if (data->lifetime.lock()) {
                        if (auto s = (SettingComboBoxData *) data->creator_container->parent->parent->user_data) {
                            if (s->on_change) {
                                s->on_change(option);
                            }
                        }
                    }
                }
            }
        };
        
        auto combo_box = combobox_parent->child(FILL_SPACE, FILL_SPACE);
        combo_box->name = combo_data->name;
        combo_box->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
            clicked_expand_generic_combobox_base(client, cr, container, 22 * config->dpi, true, true);
        };
        
        combo_box->when_paint = paint_generic_combobox;
        combo_box->user_data = combo_data;
    }
};

static void
fill_themes_root(AppClient *client, Container *themes_root) {
    setting_combobox(client, themes_root,
                     "\uE790",
                     "Choose your color",
                     "Adjust how apps and Windows surfaces appear on your screen",
                     {"Dark (Clear)", "Light", "Default"},
                     &winbar_settings->color_mode, true, [](std::string option) {
                winbar_settings->color_mode = option;
                save_settings_file();
                app_timeout_create(app, client_by_name(app, "taskbar"), 100, [](App *app, AppClient *tooltip, Timeout *t, void *userdata) {
                    app->running = false;
                    restart = true;                               
                }, nullptr, "killlater");
            });
    themes_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(themes_root, "\uF4A5", "Transparency", "Windows and surfaces appear translucent",
                 &winbar_settings->transparency);
    themes_root->child(FILL_SPACE, 4.5 * config->dpi);
}

static void
fill_icons_root(AppClient *client, Container *icons_root) {
    auto root = icons_root; // will probably be changed to a scroll container
    { // Button with 300 width, Force re-cache icons
        auto x = root->child(::hbox, 150 * config->dpi, 36 * config->dpi);
        x->when_paint = paint_centered_text;
        x->name = "recache";
        x->user_data = new Label("Force re-cache icons");
        x->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            if (data->text == "Re-caching...") // already caching so return early
                return;
            data->text = "Re-caching...";
            std::thread t([]() {
                std::lock_guard m(icon_cache_mutex);
                generate_cache();
                if (auto client = client_by_name(app, "settings_menu")) {
                    if (auto c = container_by_name("recache", client->root)) {
                        auto data = (Label *) c->user_data;
                        data->text = "Force re-cache icons";
                    }
                }
            });
            t.detach();
        };
    }
    
    root->child(FILL_SPACE, 20 * config->dpi);
    
    std::vector<std::string> icon_names = {"firefox", "spotify", "file-manager", "konsole", "system-settings",
                                           "dolphin"};
    
    std::vector<IconTarget> targets;
    for (auto icon: icon_names)
        targets.emplace_back(IconTarget(icon));
    search_icons(targets);
    pick_best(targets, 64 * config->dpi);
    
    std::unordered_set<std::string> themes;
    for (auto &target: targets)
        for (auto &cand: target.candidates)
            themes.insert(cand.theme); // deduplicate
    
    // TODO: get scrolling working as there will be too many
    for (auto theme: themes) {
        auto x = root->child(::hbox, 150 * config->dpi, 36 * config->dpi);
        x->when_paint = paint_centered_text;
        x->user_data = new Label(theme);
        //root->child(FILL_SPACE, 8 * config->dpi);
    }
    
}


static void
fill_root(AppClient *client, Container *root) {
    auto real_root = root;
    real_root->type = ::hbox;
    real_root->when_paint = paint_root;
    
    auto tabs = root->child(300 * config->dpi, FILL_SPACE);
    tabs->wanted_pad = Bounds(16 * config->dpi, 140 * config->dpi, 0, 0);
    
    auto root_stack = root->child(layout_type::stack, FILL_SPACE, FILL_SPACE);
    root_stack->name = "root_stack";
    
    auto taskbar_order_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    title("Taskbar Order", taskbar_order_root);
    
    auto winbar_behaviour_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    winbar_behaviour_root->exists = false;
    winbar_behaviour_root->name = "wbr";
    
    ScrollPaneSettings ss(config->dpi);
    auto scroll_container = make_newscrollpane_as_child(winbar_behaviour_root, ss);
    tabs->skip_delete = true;
    tabs->receive_events_even_if_obstructed = true;
    tabs->when_fine_scrolled = [](AppClient *client,
                                  cairo_t *cr,
                                  Container *container,
                                  int scroll_x,
                                  int scroll_y,
                                  bool came_from_touchpad) {
        auto c = (Container *) container->user_data;
        if (c->when_fine_scrolled) {
            if (auto wb = container_by_name("wbr", client->root)) {
                if (wb->exists)
                    c->when_fine_scrolled(client, cr, c, scroll_x, scroll_y, came_from_touchpad);
            }
        }
    };
    tabs->user_data = scroll_container;
    auto scroll_root = scroll_container->content;
    title("Winbar Behaviour", scroll_root);
    
    setting_subheading_no_indent(scroll_root, "Taskbar");

    setting_bool(scroll_root, "\uE8A1", "Thumbnails", "Show application preview when hovering a window icon", &winbar_settings->thumbnails);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);

    setting_bool(scroll_root, "\uEDA7", "Icon shortcuts", "Lanch or activate programs on Taskbar with Super+$NUMBER", &winbar_settings->pinned_icon_shortcut);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uEF90", "Animate icon positions", "Should smoothly swap position of pinned icons when re-ordering them", &winbar_settings->animate_icon_positions);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uE8EE", "Window cycling", "Clicking an icon with multiple windows open cycles to the next one", &winbar_settings->click_icon_tab_next_window);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uF605", "Battery warnings", "Receive alerts to plug-in and unplug charger", &winbar_settings->battery_notifications);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uECA5", "Live tiles", "Show in Start menu", &winbar_settings->allow_live_tiles);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
   
    setting_bool(scroll_root, "\uF466", "Labels", "Show labels on Taskbar pins", &winbar_settings->labels, true, []() {
       on_desktop_change();
    });
    setting_bool_attachment(scroll_root, "", "Make every label have the same width", "", &winbar_settings->label_uniform_size, &winbar_settings->labels, false);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);

    setting_bool(scroll_root, "\uE8CB", "Animate minimize/maximize action", "Taskbar pins will bounce when an application is minimized/maximized", &winbar_settings->minimize_maximize_animation);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "", "Auto hide", "Only show taskbar when mouse is nearby", &winbar_settings->always_hide, false, []() {
        if (winbar_settings->always_hide) {
            client_hide(app, client_by_name(app, "taskbar"));
        } else {
            client_show(app, client_by_name(app, "taskbar"));
        }
    });
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    //
    setting_bool(scroll_root, "", "Show windows from all desktops", "Display every window from every desktop", &winbar_settings->show_windows_from_all_desktops, false, []() {
        on_desktop_change();
    });
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uE107", "Close windows with drag", "When dragging a window, add edges which will close the window if drag ended on them", &winbar_settings->on_drag_show_trash);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_subheading(scroll_root, "Start menu");
   
    setting_bool(scroll_root, "\uE7B3", "Show all applications", "Ignore the 'Only Show In' instruction found in .desktop files", &winbar_settings->ignore_only_show_in);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uE782", "Default Start menu icon", "Switches to the default winbar icon", &winbar_settings->super_icon_default);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    
    setting_bool(scroll_root, "\uEDE3", "Start menu corner activate", "Open the Start menu when the mouse moves into the bottom left corner", &winbar_settings->open_start_menu_on_bottom_left_hover, true);
    setting_bool_attachment(scroll_root, "", "Autoclose menu if mouse leaves area", "", &winbar_settings->autoclose_start_menu_if_hover_opened, &winbar_settings->open_start_menu_on_bottom_left_hover, false);
    
    scroll_root->child(FILL_SPACE, 6.5 * config->dpi);
    
    setting_field(client, scroll_root, "\uE74C", "Custom desktop files folder", "Adds the specified path to the directories to search for .desktop files",  &winbar_settings->custom_desktops_directory, true);
    
    setting_bool_attachment(scroll_root, "", "Make directory the exclusive source for desktop files", "", &winbar_settings->custom_desktops_directory_exclusive, nullptr, false);

    //bool_checkbox_indent("Make directory the exclusive source for desktop files", 16 * config->dpi,
    //                     &winbar_settings->custom_desktops_directory_exclusive, scroll_root, client);
    
    setting_subheading(scroll_root, "General");
    
    setting_bool(scroll_root, "\uE767", "Animated volume playback", "Shows the meter of audio clients (not recommended as can crash)", &winbar_settings->meter_animations);
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);

    setting_bool(scroll_root, "\uEDFB", "Use OpenGL", "Switch off cairo backend for experimental OpenGL backend", &winbar_settings->use_opengl, false, []() {
        restart = true;
        app->running = false;
    });
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);

    setting_field(client, scroll_root, "\uE7E8", "Shutdown command", "Command to be executed when attempting to shutdown",  &winbar_settings->shutdown_command);
//    string_textfield("Shutdown command: ", &winbar_settings->shutdown_command, scroll_root, client, "pkexec shutdown -P now");
    scroll_root->child(FILL_SPACE, 4.5 * config->dpi);
    setting_field(client, scroll_root, "\uE777", "Restart command", "Command to be executed when attempting to restart",  &winbar_settings->restart_command);
//    string_textfield("Restart command: ", &winbar_settings->restart_command, scroll_root, client, "pkexec reboot");
    scroll_root->child(FILL_SPACE, 60 * config->dpi);
    
    auto other_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    other_root->exists = false;
    title("Other", other_root);
    bool_checkbox_indent("Use automatic scale", 0, &winbar_settings->auto_dpi, other_root, client, []() {
        if (winbar_settings->auto_dpi) {
            // Calculate what the automatic scale factor will be when we do the restart and save that
            for (auto &i: screens) {
                auto *screen = (ScreenInformation *) i;
                if (screen->is_primary) {
                    auto scale_factor = screen->height_in_pixels / 1080.0;
                    scale_factor = std::round(scale_factor * 2) / 2;
                    if (scale_factor < 1)
                        scale_factor = 1;
                    winbar_settings->scale_amount = scale_factor * 100.0f;
                    break;
                }
            }
            
            save_settings_file();
            app->running = false;
            restart = true;
        }
    });
    slider("Global scale:", 16 * config->dpi, &winbar_settings->scale_amount, other_root, client, 100, 200, 10, true,
           &winbar_settings->auto_dpi, nullptr);
    other_root->child(FILL_SPACE, 9.5 * config->dpi);
    text_input("\uE185", "Font", "Segoe UI Variable Mod", "The font used by the interface", 0, &winbar_settings->user_font, other_root, client);
    
    other_root->child(FILL_SPACE, 9.5 * config->dpi);
    slider("Taskbar height:", 16 * config->dpi, &winbar_settings->taskbar_height, other_root, client, 24, 48, 24, false,
           nullptr, [](SliderInfo *s) {
                static float target = 0;
                float max = 48;
                float min = 24;
                float full_scale = max - min;
                target = std::round(s->value);
                winbar_settings->taskbar_height = target;
                target = target * config->dpi;
                config->taskbar_height = target;
                for (auto t: app->timeouts)
                    if (t->text == "taskbar_height_timeout")
                        return;
                
                auto taskbar = client_by_name(app, "taskbar");
                if (!taskbar)
                    return;
                app_timeout_create(app, taskbar, 200, [](App *, AppClient *taskbar, Timeout *, void *) {
                    uint32_t value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                          XCB_CONFIG_WINDOW_HEIGHT;
                    
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
                    
                    uint32_t value_list_resize[] = {
                            (uint32_t) (primary_screen_info->x),
                            (uint32_t) (primary_screen_info->y + primary_screen_info->height_in_pixels - target),
                            (uint32_t) primary_screen_info->width_in_pixels,
                            (uint32_t) target};
                    xcb_configure_window(app->connection, taskbar->window, value_mask, value_list_resize);
                    
                    reserve(taskbar, target);
                    
                    save_settings_file();
                }, nullptr, "taskbar_height_timeout");
            });
    
    auto themes_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    themes_root->exists = false;
    title("Themes", themes_root);
    fill_themes_root(client, themes_root);
    
    auto icons_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    icons_root->exists = false;
    title("Icons", icons_root);
    fill_icons_root(client, icons_root);
    
    auto plugins_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    plugins_root->exists = false;
    title("Plugins", plugins_root);
    fill_plugins_root(client, plugins_root);
    
    for (int i = 0; i < root_stack->children.size(); ++i) {
        auto tab = tabs->child(FILL_SPACE, 36 * config->dpi);
        if (i == 0) {
            tab->user_data = new SelectedLabel("Taskbar Order");
            ((SelectedLabel *) tab->user_data)->selected = true;
        } else if (i == 1) {
            tab->user_data = new SelectedLabel("Winbar Behaviour");
        } else if (i == 2) {
            tab->user_data = new SelectedLabel("Other");
        } else if (i == 3) {
            tab->user_data = new SelectedLabel("Themes");
        } else if (i == 4) {
            tab->user_data = new SelectedLabel("Icons");
        } else if (i == 5) {
            tab->user_data = new SelectedLabel("Plugins");
        }
        ((SelectedLabel *) tab->user_data)->index = i;
        
        tab->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
            for (int j = 0; j < container->parent->children.size(); ++j) {
                auto con = container->parent->children[j];
                auto *data = (SelectedLabel *) con->user_data;
                data->selected = false;
            }
            auto *data = (SelectedLabel *) container->user_data;
            for (auto c: client->root->children[1]->children)
                c->exists = false;
            client->root->children[1]->children[data->index]->exists = true;
            client_layout(app, client);
            data->selected = true;
        };
        
        tab->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
            auto *data = (SelectedLabel *) container->user_data;
            
            if (data->selected) {
                //auto a = darken(config->color_pinned_icon_editor_background, 3);
                auto a = ArgbColor(1, 1, 1, 1);
                rounded_rect(client, container->real_bounds.h * .13, container->real_bounds.x, container->real_bounds.y,
                             container->real_bounds.w, container->real_bounds.h, a);
            }
            
            if (data->selected) {
                auto height = container->real_bounds.h * .55;
                auto width = std::round(4 * config->dpi);
                rounded_rect(client, width * .44,
                             container->real_bounds.x,
                             container->real_bounds.y + container->real_bounds.h / 2 - height / 2,
                             width, height, config->color_search_accent);
            }
            
            auto start = container->real_bounds.x;
            container->real_bounds.x += 20 * config->dpi;
            paint_label(client, cr, container);
            container->real_bounds.x = start;
        };
        
    }
    
    root = taskbar_order_root;
    root->type = ::vbox;
    
    ScrollPaneSettings scroll_settings(config->dpi);
    scroll_settings.right_width = 6 * config->dpi;
    scroll_settings.paint_minimal = true;
    auto scrollpane = make_newscrollpane_as_child(root, scroll_settings);
    scrollpane->content = new Container(::vbox, FILL_SPACE, USE_CHILD_SIZE);
    auto reorder_list = scrollpane->content;
    reorder_list->parent = scrollpane;
    reorder_list->name = "reorder_list";
    reorder_list->wanted_pad.y = std::round(1 * config->dpi);
    reorder_list->wanted_pad.x = std::round(1 * config->dpi);
    reorder_list->wanted_pad.w = std::round(1 * config->dpi);
    reorder_list->wanted_pad.h = std::round(1 * config->dpi);
    reorder_list->spacing = 4 * config->dpi;
    for (auto item: winbar_settings->taskbar_order) {
        if (item.name == "Bluetooth") {
            add_item(reorder_list, item.name, winbar_settings->bluetooth_enabled);
        } else {
            add_item(reorder_list, item.name, item.on);
        }
    }
    
    root->child(FILL_SPACE, 16 * config->dpi); // space after re-orderable list
    
    {
//        auto x = root->child(::hbox, FILL_SPACE, 28 * config->dpi);
//        x->when_paint = paint_centered_text;
//        x->when_clicked = clicked_add_spacer;
//        x->user_data = new Label("Add spacer");
//        root->child(FILL_SPACE, 6 * config->dpi);
    }
    
    {
        auto x = root->child(::hbox, FILL_SPACE, 36 * config->dpi);
        x->when_paint = paint_centered_text;
        x->user_data = new Label("Reset to default");
        x->when_clicked = clicked_reset;
    }
}

// Trim leading and trailing whitespace from a string in-place
void trim(std::string &str) {
    // Remove leading whitespace
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    
    // Remove trailing whitespace
    str.erase(std::find_if(str.rbegin(), str.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), str.end());
}

void when_closed_settings_menu(AppClient *client) {
    save_settings_file();
}

void open_settings_menu(SettingsPage page) {
    Settings settings;
    settings.skip_taskbar = false;
//    settings.keep_above = true;
    settings.w = 1000 * config->dpi;
    settings.h = 700 * config->dpi;
    auto client = client_new(app, settings, "settings_menu");
    client->when_closed = when_closed_settings_menu;
    xcb_ewmh_set_wm_icon_name(&app->ewmh, client->window, strlen("settings"), "settings");
    std::string title = "Taskbar Settings";
    xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
    fill_root(client, client->root);
    
    client_show(app, client);
    xcb_set_input_focus(app->connection, XCB_NONE, client->window, XCB_CURRENT_TIME);
    xcb_flush(app->connection);
    xcb_aux_sync(app->connection);
    active_window_changed(client->window);
}

void save_settings_file() {
    char *home = getenv("HOME");
    std::string path = std::string(home) + "/.config/winbar/settings.conf";
    std::ofstream out_file(path);
    
    // version
    out_file << "version=\"" << winbar_settings->version << "\"";
    out_file << std::endl << std::endl;
    
    // Taskbar order
    out_file << "order=";

#define WRITE(button_name, container_name) \
    if (child->name == container_name) { \
        for (auto item: winbar_settings->taskbar_order) { \
            if (item.name == button_name) { \
                out_file << "\"" << button_name << "\"=" << (item.on ? "on," : "off,");  \
            } \
        } \
        continue; \
    }
    
    if (auto c = client_by_name(app, "taskbar")) {
        for (auto child: c->root->children) {
            WRITE("Search Field", "field_search")
            WRITE("Super", "super")
            WRITE("Workspace", "workspace")
            WRITE("Pinned Icons", "icons")
            WRITE("Systray", "systray")
            WRITE("Bluetooth", "bluetooth")
            WRITE("Wifi", "wifi")
            WRITE("Battery", "battery")
            WRITE("Volume", "volume")
            WRITE("Date", "date")
            WRITE("Notifications", "action")
            WRITE("Show Desktop", "minimize")
        }
    }
    out_file << std::endl << std::endl;
    
    // Icons alignment
    out_file << "icons_alignment=";
    if (winbar_settings->icons_alignment == container_alignment::ALIGN_RIGHT) {
        out_file << "right";
    } else if (winbar_settings->icons_alignment == container_alignment::ALIGN_GLOBAL_CENTER_HORIZONTALLY) {
        out_file << "center";
    } else if (winbar_settings->icons_alignment == container_alignment::ALIGN_CENTER_HORIZONTALLY) {
        out_file << "center local";
    } else {
        out_file << "left";
    }
    out_file << std::endl << std::endl;
    
    // Pinned icon style
    out_file << "pinned_icon_style=\"" << winbar_settings->pinned_icon_style << "\"";
    out_file << std::endl << std::endl;
    
    // Search Behaviour
    out_file << "search_behaviour=\"" << winbar_settings->search_behaviour << "\"";
    out_file << std::endl << std::endl;
    
    // Date alignment
    out_file << "date_alignment=";
    if (winbar_settings->date_alignment == PangoAlignment::PANGO_ALIGN_RIGHT) {
        out_file << "right";
    } else if (winbar_settings->date_alignment == PangoAlignment::PANGO_ALIGN_CENTER) {
        out_file << "center";
    } else {
        out_file << "left";
    }
    out_file << std::endl << std::endl;
    
    // Date style
    out_file << "date_style=\"" << winbar_settings->date_style << "\"";
    out_file << std::endl << std::endl;
    
    // Date style
    out_file << "date_size=\"" << std::to_string(winbar_settings->date_size) << "\"";
    out_file << std::endl << std::endl;

    // Start menu height
    out_file << "start_menu_height=\"" << std::to_string(winbar_settings->start_menu_height) << "\"";
    out_file << std::endl << std::endl;

    // Extra live tile pages
    out_file << "extra_live_tile_pages=\"" << std::to_string(winbar_settings->extra_live_tile_pages) << "\"";
    out_file << std::endl << std::endl;
    
    out_file << "show_agenda=" << (winbar_settings->show_agenda ? "true" : "false");
    out_file << std::endl << std::endl;
    
    // Volume expands
    out_file << "volume_expands_on_hover=" << (winbar_settings->volume_expands_on_hover ? "true" : "false");
    out_file << std::endl << std::endl;
 
    out_file << "volume_label_always_on=" << (winbar_settings->volume_label_always_on ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "battery_notifications=" << (winbar_settings->battery_notifications ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "pinned_icon_shortcut=" << (winbar_settings->pinned_icon_shortcut ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "animate_icon_positions=" << (winbar_settings->animate_icon_positions ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "allow_live_tiles=" << (winbar_settings->allow_live_tiles ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "open_start_menu_on_bottom_left_hover="
             << (winbar_settings->open_start_menu_on_bottom_left_hover ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "autoclose_start_menu_if_hover_opened="
             << (winbar_settings->autoclose_start_menu_if_hover_opened ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "click_icon_tab_next_window=" << (winbar_settings->click_icon_tab_next_window ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "custom_desktops_directory=" << winbar_settings->custom_desktops_directory;
    out_file << std::endl << std::endl;
    
    out_file << "shutdown_command=" << winbar_settings->shutdown_command;
    out_file << std::endl << std::endl;
    
    out_file << "user_font=" << winbar_settings->user_font;
    out_file << std::endl << std::endl;
    
    out_file << "restart_command=" << winbar_settings->restart_command;
    out_file << std::endl << std::endl;
    
    out_file << "color_mode=" << winbar_settings->color_mode;
    out_file << std::endl << std::endl;
    
    out_file << "custom_desktops_directory_exclusive="
             << (winbar_settings->custom_desktops_directory_exclusive ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "ignore_only_show_in=" << (winbar_settings->ignore_only_show_in ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "meter_animations=" << (winbar_settings->meter_animations ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "super_icon_default=" << (winbar_settings->super_icon_default ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "labels=" << (winbar_settings->labels ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "label_uniform_size=" << (winbar_settings->label_uniform_size ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "minimize_maximize_animation=" << (winbar_settings->minimize_maximize_animation ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "transparency=" << (winbar_settings->transparency ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "always_hide=" << (winbar_settings->always_hide ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "show_windows_from_all_desktops=" << (winbar_settings->show_windows_from_all_desktops ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "auto_dpi=" << (winbar_settings->auto_dpi ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "scale_amount=\"" << std::to_string(winbar_settings->scale_amount) << "\"";
    out_file << std::endl << std::endl;
    
    out_file << "taskbar_height=\"" << std::to_string(winbar_settings->taskbar_height) << "\"";
    out_file << std::endl << std::endl;
    
    out_file << "use_opengl=" << (winbar_settings->use_opengl ? "true" : "false");
    out_file << std::endl << std::endl;
    
    out_file << "on_drag_show_trash=" << (winbar_settings->on_drag_show_trash ? "true" : "false");
    out_file << std::endl << std::endl;
    
    // Thumbnails
    out_file << "thumbnails=" << (winbar_settings->thumbnails ? "true" : "false");
    out_file << std::endl << std::endl;
    
    // Battery expands
    out_file << "battery_expands_on_hover=" << (winbar_settings->battery_expands_on_hover ? "true" : "false");
    out_file << std::endl << std::endl;

    out_file << "battery_label_always_on=" << (winbar_settings->battery_label_always_on ? "true" : "false");
    out_file << std::endl << std::endl;
    
    // Search tab
    out_file << "tab=\"" << active_tab << "\"";
    out_file << std::endl << std::endl;
}

void parse_bool(LineParser *parser, std::string key, std::string name, bool *target) {
    if (key != name)
        return;
    parser->until(LineParser::Token::IDENT);
    if (parser->current_token == LineParser::Token::IDENT) {
        std::string text = parser->until(LineParser::Token::END_OF_LINE);
        trim(text);
        if (text == "false") {
            *target = false;
        } else {
            *target = true;
        }
    }
}

void parse_string(LineParser *parser, std::string key, std::string name, std::string *target) {
    if (key != name)
        return;
    parser->until(LineParser::Token::IDENT);
    if (parser->current_token == LineParser::Token::IDENT) {
        std::string text = parser->until(LineParser::Token::END_OF_LINE);
        trim(text);
       *target = text;
    }
}

void read_settings_file() {
    winbar_settings->taskbar_order.clear();
    // Load default taskbar_order
    std::vector<std::string> names = {"Super", "Search Field", "Workspace", "Pinned Icons",
                                      "Systray", "Bluetooth", "Battery", "Wifi",
                                      "Volume", "Date", "Notifications", "Show Desktop"};
    for (int i = 0; i < names.size(); ++i) {
        TaskbarItem item;
        item.name = names[i];
        item.on = true;
        item.target_index = i;
        winbar_settings->taskbar_order.push_back(item);
    }
    std::vector<TaskbarItem> file_order;
    bool found_order = false;
    
    char *home = getenv("HOME");
    std::string path = std::string(home) + "/.config/winbar/settings.conf";
    std::ifstream input_file(path);
    bool was_good = input_file.good();
    std::string found_version = "0";
    if (was_good) {
        std::string line;
        while (std::getline(input_file, line)) {
            LineParser parser(line);
            std::string key = parser.until(LineParser::Token::EQUAL);
            trim(key);
            
            if (key == "order") {
                found_order = true;
#define EAT_EXPECTED(type) if (parser.current_token != type) {goto out;} else{parser.next();};
                std::string first;
                std::string second;
                while (parser.current_token != LineParser::Token::END_OF_LINE) {
                    parser.until(LineParser::Token::QUOTE);
                    EAT_EXPECTED(LineParser::Token::QUOTE)
                    first = parser.until(LineParser::Token::QUOTE);
                    EAT_EXPECTED(LineParser::Token::QUOTE)
                    parser.until(LineParser::Token::EQUAL);
                    EAT_EXPECTED(LineParser::Token::EQUAL)
                    second = parser.until(LineParser::Token::COMMA);
                    trim(second);
                    
                    // Skip adding to file_order if already added unless if it's 'Space'
                    bool should_be_added = true;
                    for (auto item: file_order)
                        if (item.name == first)
                            should_be_added = first == "Space";
                    
                    if (should_be_added) {
                        TaskbarItem item;
                        item.name = first;
                        item.on = second == "on";
                        item.target_index = file_order.size();
                        file_order.push_back(item);
                    }
                }
                out:
                continue;
            } else if (key == "icons_alignment") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (text == "right") {
                        winbar_settings->icons_alignment = container_alignment::ALIGN_RIGHT;
                    } else if (text == "center") {
                        winbar_settings->icons_alignment = container_alignment::ALIGN_GLOBAL_CENTER_HORIZONTALLY;
                    } else if (text == "center local") {
                        winbar_settings->icons_alignment = container_alignment::ALIGN_CENTER_HORIZONTALLY;
                    }
                }
            } else if (key == "pinned_icon_style") {
                parser.until(LineParser::Token::QUOTE);
                if (parser.current_token == LineParser::Token::QUOTE) {
                    parser.next();
                    std::string text = parser.until(LineParser::Token::QUOTE);
                    if (parser.current_token == LineParser::Token::QUOTE) {
                        trim(text);
                        if (!text.empty()) {
                            winbar_settings->pinned_icon_style = text;
                        }
                    }
                }
           } else if (key == "date_alignment") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (text == "left") {
                        winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_LEFT;
                    } else if (text == "right") {
                        winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_RIGHT;
                    }
                }
            } else if (key == "date_style") {
                parser.until(LineParser::Token::QUOTE);
                if (parser.current_token == LineParser::Token::QUOTE) {
                    parser.next();
                    std::string text = parser.until(LineParser::Token::QUOTE);
                    if (parser.current_token == LineParser::Token::QUOTE) {
                        trim(text);
                        if (!text.empty()) {
                            winbar_settings->date_style = text;
                        }
                    }
                }
            } else if (key == "version") {
                parser.until(LineParser::Token::QUOTE);
                if (parser.current_token == LineParser::Token::QUOTE) {
                    parser.next();
                    std::string text = parser.until(LineParser::Token::QUOTE);
                    if (parser.current_token == LineParser::Token::QUOTE) {
                        trim(text);
                        if (!text.empty()) {
                            found_version = text;
                        }
                    }
                }
            } else if (key == "search_behaviour") {
                parser.until(LineParser::Token::QUOTE);
                if (parser.current_token == LineParser::Token::QUOTE) {
                    parser.next();
                    std::string text = parser.until(LineParser::Token::QUOTE);
                    if (parser.current_token == LineParser::Token::QUOTE) {
                        trim(text);
                        if (!text.empty()) {
                            winbar_settings->search_behaviour = text;
                        }
                    }
                }
            } else if (key == "tab") {
                parser.until(LineParser::Token::QUOTE);
                if (parser.current_token == LineParser::Token::QUOTE) {
                    parser.next();
                    std::string text = parser.until(LineParser::Token::QUOTE);
                    if (parser.current_token == LineParser::Token::QUOTE) {
                        trim(text);
                        if (!text.empty() && text == "Apps") {
                            active_tab = "Apps";
                        } else {
                            active_tab = "Scripts";
                        }
                    }
                }
            } else if (key == "date_size") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                   std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            int size = std::atoi(text.c_str());
                            if (size < 4) {
                                size = 4;
                            } else if (size > 60 * config->dpi) {
                                size = 60 * config->dpi;
                            }
                            winbar_settings->date_size = size;
                        } catch (...) {
                        
                        }
                    }
                }
            } else if (key == "start_menu_height") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            int height = std::atoi(text.c_str());
                            winbar_settings->start_menu_height = height;
                        } catch (...) {
                        
                        }
                    }
                }
            } else if (key == "scale_factor") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            float amount = std::atof(text.c_str());
                            winbar_settings->scale_factor = amount;
                        } catch (...) {
                        
                        }
                    }
                }
            } else if (key == "scale_amount") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            float amount = std::atoi(text.c_str());
                            winbar_settings->scale_amount = amount;
                        } catch (...) {
                        
                        }
                    }
                }
            } else if (key == "taskbar_height") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            float amount = std::atoi(text.c_str());
                            winbar_settings->taskbar_height = amount;
                        } catch (...) {
                        
                        }
                    }
                }
            } else if (key == "extra_live_tile_pages") {
                parser.until(LineParser::Token::IDENT);
                if (parser.current_token == LineParser::Token::IDENT) {
                    std::string text = parser.until(LineParser::Token::END_OF_LINE);
                    trim(text);
                    if (!text.empty()) {
                        try {
                            int extra_pages = std::atoi(text.c_str());
                            winbar_settings->extra_live_tile_pages = extra_pages;
                        } catch (...) {
                        
                        }
                    }
                }
            } else {
                if (key.empty())
                    continue;
                parse_bool(&parser, key, "battery_notifications", &winbar_settings->battery_notifications);
                parse_bool(&parser, key, "pinned_icon_shortcut", &winbar_settings->pinned_icon_shortcut);
                parse_bool(&parser, key, "animate_icon_positions", &winbar_settings->animate_icon_positions);
                parse_bool(&parser, key, "allow_live_tiles", &winbar_settings->allow_live_tiles);
                parse_bool(&parser, key, "open_start_menu_on_bottom_left_hover",
                           &winbar_settings->open_start_menu_on_bottom_left_hover);
                parse_bool(&parser, key, "autoclose_start_menu_if_hover_opened",
                           &winbar_settings->autoclose_start_menu_if_hover_opened);
                parse_bool(&parser, key, "click_icon_tab_next_window", &winbar_settings->click_icon_tab_next_window);
                parse_bool(&parser, key, "volume_expands_on_hover", &winbar_settings->volume_expands_on_hover);
                parse_bool(&parser, key, "volume_label_always_on", &winbar_settings->volume_label_always_on);
                parse_bool(&parser, key, "battery_expands_on_hover", &winbar_settings->battery_expands_on_hover);
                parse_bool(&parser, key, "battery_label_always_on", &winbar_settings->battery_label_always_on);
                parse_bool(&parser, key, "show_agenda", &winbar_settings->show_agenda);
                parse_bool(&parser, key, "thumbnails", &winbar_settings->thumbnails);
                parse_bool(&parser, key, "custom_desktops_directory_exclusive",
                           &winbar_settings->custom_desktops_directory_exclusive);
                parse_bool(&parser, key, "ignore_only_show_in", &winbar_settings->ignore_only_show_in);
                parse_bool(&parser, key, "meter_animations", &winbar_settings->meter_animations);
                parse_bool(&parser, key, "super_icon_default", &winbar_settings->super_icon_default);
                parse_bool(&parser, key, "labels", &winbar_settings->labels);
                parse_bool(&parser, key, "label_uniform_size", &winbar_settings->label_uniform_size);
                parse_bool(&parser, key, "minimize_maximize_animation", &winbar_settings->minimize_maximize_animation);
                parse_bool(&parser, key, "transparency", &winbar_settings->transparency);
                parse_bool(&parser, key, "always_hide", &winbar_settings->always_hide);
                parse_bool(&parser, key, "show_windows_from_all_desktops", &winbar_settings->show_windows_from_all_desktops);
                parse_bool(&parser, key, "auto_dpi", &winbar_settings->auto_dpi);
                parse_bool(&parser, key, "use_opengl", &winbar_settings->use_opengl);
                parse_bool(&parser, key, "on_drag_show_trash", &winbar_settings->on_drag_show_trash);
                parse_string(&parser, key, "custom_desktops_directory", &winbar_settings->custom_desktops_directory);
                parse_string(&parser, key, "color_mode", &winbar_settings->color_mode);
                parse_string(&parser, key, "shutdown_command", &winbar_settings->shutdown_command);
                parse_string(&parser, key, "restart_command", &winbar_settings->restart_command);
                parse_string(&parser, key, "user_font", &winbar_settings->user_font);
            }
        }
    }
    
    if (found_version == "0") {
        // Transition deprecated variables to v1
        if (!winbar_settings->auto_dpi) {
            winbar_settings->scale_amount = std::round(winbar_settings->scale_factor * 100.f);
        }
    } else if (found_version == "1") {
    
    }
    
    for (auto &order: winbar_settings->taskbar_order) {
        TaskbarItem *found = nullptr;
        for (auto &file: file_order) {
            if (order.name == file.name) {
                found = &file;
                break;
            }
        }
        if (found) {
            order.on = found->on;
            order.target_index = found->target_index;
        } else {
            order.on = !found_order;
            order.target_index += 1000;
        }
    }
    if (!was_good) { // First launch layout can be set here
        for (auto &t: winbar_settings->taskbar_order) {
            if (t.name == "Search Field") {
                t.on = false;
            }
        }
    }
    load_colors(); 
    merge_order_with_taskbar();
}

void load_colors() {
    config->color_taskbar_background = ArgbColor("#aa101010");
    config->color_taskbar_button_icons = ArgbColor("#ffffffff");
    config->color_taskbar_button_default = ArgbColor("#00ffffff");
    config->color_taskbar_button_hovered = ArgbColor("#23ffffff");
    config->color_taskbar_button_pressed = ArgbColor("#35ffffff");
    config->color_taskbar_windows_button_default_icon = ArgbColor("#ffffffff");
    config->color_taskbar_windows_button_hovered_icon = ArgbColor("#ff429ce3");
    config->color_taskbar_windows_button_pressed_icon = ArgbColor("#ff0078d7");
    config->color_taskbar_search_bar_default_background = ArgbColor("#fff3f3f3");
    config->color_taskbar_search_bar_hovered_background = ArgbColor("#ffffffff");
    config->color_taskbar_search_bar_pressed_background = ArgbColor("#ffffffff");
    config->color_taskbar_search_bar_default_text = ArgbColor("#ff2b2b2b");
    config->color_taskbar_search_bar_hovered_text = ArgbColor("#ff2d2d2d");
    config->color_taskbar_search_bar_pressed_text = ArgbColor("#ff020202");
    config->color_taskbar_search_bar_default_icon = ArgbColor("#ff020202");
    config->color_taskbar_search_bar_hovered_icon = ArgbColor("#ff020202");
    config->color_taskbar_search_bar_pressed_icon = ArgbColor("#ff020202");
    config->color_taskbar_search_bar_default_border = ArgbColor("#ffb4b4b4");
    config->color_taskbar_search_bar_hovered_border = ArgbColor("#ffb4b4b4");
    config->color_taskbar_search_bar_pressed_border = ArgbColor("#ff0078d7");
    config->color_taskbar_date_time_text = ArgbColor("#ffffffff");
    config->color_taskbar_application_icons_background = ArgbColor("#aa474747");
    config->color_taskbar_application_icons_accent = ArgbColor("#ff76b9ed");
    config->color_taskbar_minimize_line = ArgbColor("#ff222222");
    config->color_taskbar_attention_accent = ArgbColor("#ffffa21d");
    config->color_taskbar_attention_background = ArgbColor("#ffffa21d");
    config->color_systray_background = ArgbColor("#f3282828");
    config->color_battery_background = ArgbColor("#f31f1f1f");
    config->color_battery_text = ArgbColor("#ffffffff");
    config->color_battery_icons = ArgbColor("#ffffffff");
    config->color_battery_slider_background = ArgbColor("#ff797979");
    config->color_battery_slider_foreground = ArgbColor("#ff0178d6");
    config->color_battery_slider_active = ArgbColor("#ffffffff");
    config->color_wifi_background = ArgbColor("#f31f1f1f");
    config->color_wifi_icons = ArgbColor("#ffffffff");
    config->color_wifi_default_button = ArgbColor("#00ffffff");
    config->color_wifi_hovered_button = ArgbColor("#22ffffff");
    config->color_wifi_pressed_button = ArgbColor("#44ffffff");
    config->color_wifi_text_title = ArgbColor("#ffffffff");
    config->color_wifi_text_title_info = ArgbColor("#ffadadad");
    config->color_wifi_text_settings_default_title = ArgbColor("#ffa5d6fd");
    config->color_wifi_text_settings_hovered_title = ArgbColor("#ffa4a4a4");
    config->color_wifi_text_settings_pressed_title = ArgbColor("#ff787878");
    config->color_wifi_text_settings_title_info = ArgbColor("#ffa4a4a4");
    config->color_date_background = ArgbColor("#f31f1f1f");
    config->color_date_seperator = ArgbColor("#ff4b4b4b");
    config->color_date_text = ArgbColor("#ffffffff");
    config->color_date_text_title = ArgbColor("#ffffffff");
    config->color_date_text_title_period = ArgbColor("#ffa5a5a5");
    config->color_date_text_title_info = ArgbColor("#ffa5dafd");
    config->color_date_text_month_year = ArgbColor("#ffdedede");
    config->color_date_text_week_day = ArgbColor("#ffffffff");
    config->color_date_text_current_month = ArgbColor("#ffffffff");
    config->color_date_text_not_current_month = ArgbColor("#ff808080");
    config->color_date_cal_background = ArgbColor("#ff006fd8");
    config->color_date_cal_foreground = ArgbColor("#ff000000");
    config->color_date_cal_border = ArgbColor("#ff797979");
    config->color_date_weekday_monthday = ArgbColor("#ffffffff");
    config->color_date_default_arrow = ArgbColor("#ffdfdfdf");
    config->color_date_hovered_arrow = ArgbColor("#ffefefef");
    config->color_date_pressed_arrow = ArgbColor("#ffffffff");
    config->color_date_text_default_button = ArgbColor("#ffa5d6fd");
    config->color_date_text_hovered_button = ArgbColor("#ffa4a4a4");
    config->color_date_text_pressed_button = ArgbColor("#ff787878");
    config->color_date_cursor = ArgbColor("#ffffffff");
    config->color_date_text_prompt = ArgbColor("#ffcccccc");
    config->color_volume_background = ArgbColor("#f31f1f1f");
    config->color_volume_text = ArgbColor("#ffffffff");
    config->color_volume_default_icon = ArgbColor("#ffd2d2d2");
    config->color_volume_hovered_icon = ArgbColor("#ffe8e8e8");
    config->color_volume_pressed_icon = ArgbColor("#ffffffff");
    config->color_volume_slider_background = ArgbColor("#ff797979");
    config->color_volume_slider_foreground = ArgbColor("#ff0178d6");
    config->color_volume_slider_active = ArgbColor("#ffffffff");
    config->color_apps_background = ArgbColor("#f31f1f1f");
    config->color_apps_text = ArgbColor("#ffffffff");
    config->color_apps_text_inactive = ArgbColor("#ff505050");
    config->color_apps_icons = ArgbColor("#ffffffff");
    config->color_apps_default_item = ArgbColor("#00ffffff");
    config->color_apps_hovered_item = ArgbColor("#22ffffff");
    config->color_apps_pressed_item = ArgbColor("#44ffffff");
    config->color_apps_item_icon_background = ArgbColor("#ff3380cc");
    config->color_apps_scrollbar_gutter = ArgbColor("#ff353535");
    config->color_apps_scrollbar_default_thumb = ArgbColor("#ff5d5d5d");
    config->color_apps_scrollbar_hovered_thumb = ArgbColor("#ff868686");
    config->color_apps_scrollbar_pressed_thumb = ArgbColor("#ffaeaeae");
    config->color_apps_scrollbar_default_button = ArgbColor("#ff353535");
    config->color_apps_scrollbar_hovered_button = ArgbColor("#ff494949");
    config->color_apps_scrollbar_pressed_button = ArgbColor("#ffaeaeae");
    config->color_apps_scrollbar_default_button_icon = ArgbColor("#ffffffff");
    config->color_apps_scrollbar_hovered_button_icon = ArgbColor("#ffffffff");
    config->color_apps_scrollbar_pressed_button_icon = ArgbColor("#ff545454");
    config->color_pin_menu_background = ArgbColor("#f31f1f1f");
    config->color_pin_menu_hovered_item = ArgbColor("#22ffffff");
    config->color_pin_menu_pressed_item = ArgbColor("#44ffffff");
    config->color_pin_menu_text = ArgbColor("#ffffffff");
    config->color_pin_menu_icons = ArgbColor("#ffffffff");
    config->color_windows_selector_default_background = ArgbColor("#f3282828");
    config->color_windows_selector_hovered_background = ArgbColor("#f33d3d3d");
    config->color_windows_selector_pressed_background = ArgbColor("#f3535353");
    config->color_windows_selector_close_icon = ArgbColor("#ffffffff");
    config->color_windows_selector_close_icon_hovered = ArgbColor("#ffffffff");
    config->color_windows_selector_close_icon_pressed = ArgbColor("#ffffffff");
    config->color_windows_selector_text = ArgbColor("#ffffffff");
    config->color_windows_selector_close_icon_hovered_background = ArgbColor("#ffc61a28");
    config->color_windows_selector_close_icon_pressed_background = ArgbColor("#ffe81123");
    config->color_windows_selector_attention_background = ArgbColor("#ffffa21d");
    config->color_search_tab_bar_background = ArgbColor("#f31f1f1f");
    config->color_search_accent = ArgbColor("#ff0078d7");
    config->color_search_tab_bar_default_text = ArgbColor("#ffbfbfbf");
    config->color_search_tab_bar_hovered_text = ArgbColor("#ffd9d9d9");
    config->color_search_tab_bar_pressed_text = ArgbColor("#ffa6a6a6");
    config->color_search_tab_bar_active_text = ArgbColor("#ffffffff");
    config->color_search_empty_tab_content_background = ArgbColor("#f32a2a2a");
    config->color_search_empty_tab_content_icon = ArgbColor("#ff6b6b6b");
    config->color_search_empty_tab_content_text = ArgbColor("#ffaaaaaa");
    config->color_search_content_left_background = ArgbColor("#fff0f0f0");
    config->color_search_content_right_background = ArgbColor("#fff5f5f5");
    config->color_search_content_right_foreground = ArgbColor("#ffffffff");
    config->color_search_content_right_splitter = ArgbColor("#fff2f2f2");
    config->color_search_content_text_primary = ArgbColor("#ff010101");
    config->color_search_content_text_secondary = ArgbColor("#ff606060");
    config->color_search_content_right_button_default = ArgbColor("#00000000");
    config->color_search_content_right_button_hovered = ArgbColor("#26000000");
    config->color_search_content_right_button_pressed = ArgbColor("#51000000");
    config->color_search_content_left_button_splitter = ArgbColor("#ffffffff");
    config->color_search_content_left_button_default = ArgbColor("#00000000");
    config->color_search_content_left_button_hovered = ArgbColor("#24000000");
    config->color_search_content_left_button_pressed = ArgbColor("#48000000");
    config->color_search_content_left_button_active = ArgbColor("#ffa8cce9");
    config->color_search_content_left_set_active_button_default = ArgbColor("#00000000");
    config->color_search_content_left_set_active_button_hovered = ArgbColor("#22000000");
    config->color_search_content_left_set_active_button_pressed = ArgbColor("#19000000");
    config->color_search_content_left_set_active_button_active = ArgbColor("#ff97b8d2");
    config->color_search_content_left_set_active_button_icon_default = ArgbColor("#ff606060");
    config->color_search_content_left_set_active_button_icon_pressed = ArgbColor("#ffffffff");
    config->color_pinned_icon_editor_background = ArgbColor("#ffffffff");
    config->color_pinned_icon_editor_field_default_text = ArgbColor("#ff000000");
    config->color_pinned_icon_editor_field_hovered_text = ArgbColor("#ff2d2d2d");
    config->color_pinned_icon_editor_field_pressed_text = ArgbColor("#ff020202");
    config->color_pinned_icon_editor_field_default_border = ArgbColor("#ffb4b4b4");
    config->color_pinned_icon_editor_field_hovered_border = ArgbColor("#ff646464");
    config->color_pinned_icon_editor_field_pressed_border = ArgbColor("#ff0078d7");
    config->color_pinned_icon_editor_cursor = ArgbColor("#ff000000");
    config->color_pinned_icon_editor_button_default = ArgbColor("#ffcccccc");
    config->color_pinned_icon_editor_button_text_default = ArgbColor("#ff000000");
    config->color_notification_content_background = ArgbColor("#ff1f1f1f");
    config->color_notification_title_background = ArgbColor("#ff191919");
    config->color_notification_content_text = ArgbColor("#ffffffff");
    config->color_notification_title_text = ArgbColor("#ffffffff");
    config->color_notification_button_default = ArgbColor("#ff545454");
    config->color_notification_button_hovered = ArgbColor("#ff616161");
    config->color_notification_button_pressed = ArgbColor("#ff474747");
    config->color_notification_button_text_default = ArgbColor("#ffffffff");
    config->color_notification_button_text_hovered = ArgbColor("#ffffffff");
    config->color_notification_button_text_pressed = ArgbColor("#ffffffff");
    config->color_notification_button_send_to_action_center_default = ArgbColor("#ff9c9c9c");
    config->color_notification_button_send_to_action_center_hovered = ArgbColor("#ffcccccc");
    config->color_notification_button_send_to_action_center_pressed = ArgbColor("#ff888888");
    config->color_action_center_background = ArgbColor("#ff1f1f1f");
    config->color_action_center_history_text = ArgbColor("#ffa5d6fd");
    config->color_action_center_no_new_text = ArgbColor("#ffffffff");
    config->color_action_center_notification_content_background = ArgbColor("#ff282828");
    config->color_action_center_notification_title_background = ArgbColor("#ff1f1f1f");
    config->color_action_center_notification_content_text = ArgbColor("#ffffffff");
    config->color_action_center_notification_title_text = ArgbColor("#ffffffff");
    config->color_action_center_notification_button_default = ArgbColor("#ff545454");
    config->color_action_center_notification_button_hovered = ArgbColor("#ff616161");
    config->color_action_center_notification_button_pressed = ArgbColor("#ff474747");
    config->color_action_center_notification_button_text_default = ArgbColor("#ffffffff");
    config->color_action_center_notification_button_text_hovered = ArgbColor("#ffffffff");
    config->color_action_center_notification_button_text_pressed = ArgbColor("#ffffffff");

    
    if (winbar_settings->color_mode == "Light") {
        config->color_taskbar_background = ArgbColor("#cceeeeee");
        config->color_taskbar_button_icons = ArgbColor("#ff000000");
        config->color_taskbar_button_default = ArgbColor("#00ffffff");
        config->color_taskbar_button_hovered = ArgbColor("#ccffffff");
        config->color_taskbar_button_pressed = ArgbColor("#ddffffff");
        config->color_taskbar_windows_button_default_icon = ArgbColor("#ff000000");
        config->color_taskbar_windows_button_hovered_icon = ArgbColor("#ff429ce3");
        config->color_taskbar_windows_button_pressed_icon = ArgbColor("#ff0078d7");
        config->color_taskbar_search_bar_default_background = ArgbColor("#ffffffff");
        config->color_taskbar_search_bar_hovered_background = ArgbColor("#ffffffff");
        config->color_taskbar_search_bar_pressed_background = ArgbColor("#ffffffff");
        config->color_taskbar_search_bar_default_text = ArgbColor("#ff2b2b2b");
        config->color_taskbar_search_bar_hovered_text = ArgbColor("#ff2d2d2d");
        config->color_taskbar_search_bar_pressed_text = ArgbColor("#ff020202");
        config->color_taskbar_search_bar_default_icon = ArgbColor("#ff020202");
        config->color_taskbar_search_bar_hovered_icon = ArgbColor("#ff020202");
        config->color_taskbar_search_bar_pressed_icon = ArgbColor("#ff020202");
        config->color_taskbar_search_bar_default_border = ArgbColor("#ffb4b4b4");
        config->color_taskbar_search_bar_hovered_border = ArgbColor("#ffb4b4b4");
        config->color_taskbar_search_bar_pressed_border = ArgbColor("#ff0078d7");
        config->color_taskbar_date_time_text = ArgbColor("#ff000000");
        config->color_taskbar_application_icons_background = ArgbColor("#fffafafa");
        config->color_taskbar_application_icons_accent = ArgbColor("#ff0078d7");
        config->color_taskbar_minimize_line = ArgbColor("#ffbebebe");
        config->color_taskbar_attention_accent = ArgbColor("#ffffcd4d");
        config->color_taskbar_attention_background = ArgbColor("#ffffcd4d");
        config->color_systray_background = ArgbColor("#f3e4e4e4");
        config->color_battery_background = ArgbColor("#f3e4e4e4");
        config->color_battery_text = ArgbColor("#ff020202");
        config->color_battery_icons = ArgbColor("#ff000000");
        config->color_battery_slider_background = ArgbColor("#ff8a8a8a");
        config->color_battery_slider_foreground = ArgbColor("#ff0078d7");
        config->color_battery_slider_active = ArgbColor("#ff000000");
        config->color_wifi_background = ArgbColor("#f3e4e4e4");
        config->color_wifi_icons = ArgbColor("#ff000000");
        config->color_wifi_default_button = ArgbColor("#00ffffff");
        config->color_wifi_hovered_button = ArgbColor("#D0ffffff");
        config->color_wifi_pressed_button = ArgbColor("#90ffffff");
        config->color_wifi_text_title = ArgbColor("#ff020202");
        config->color_wifi_text_title_info = ArgbColor("#ff616161");
        config->color_wifi_text_settings_default_title = ArgbColor("#ff024376");
        config->color_wifi_text_settings_hovered_title = ArgbColor("#ff5c5c5c");
        config->color_wifi_text_settings_pressed_title = ArgbColor("#ff8a8a8a");
        config->color_wifi_text_settings_title_info = ArgbColor("#ff5d5d5d");
        config->color_date_background = ArgbColor("#f3e4e4e4");
        config->color_date_seperator = ArgbColor("#ffb7b7b7");
        config->color_date_text = ArgbColor("#ff020202");
        config->color_date_text_title = ArgbColor("#ff020202");
        config->color_date_text_title_period = ArgbColor("#ff5c5c5c");
        config->color_date_text_title_info = ArgbColor("#ff024376");
        config->color_date_text_month_year = ArgbColor("#ff020202");
        config->color_date_text_week_day = ArgbColor("#ff020202");
        config->color_date_text_current_month = ArgbColor("#ff020202");
        config->color_date_text_not_current_month = ArgbColor("#ff8a8a8a");
        config->color_date_cal_background = ArgbColor("#ff006fd8");
        config->color_date_cal_foreground = ArgbColor("#ffffffff");
        config->color_date_cal_border = ArgbColor("#fff4f4f4");
        config->color_date_weekday_monthday = ArgbColor("#ff020202");
        config->color_date_default_arrow = ArgbColor("#ff3a3a3a");
        config->color_date_hovered_arrow = ArgbColor("#ff3a3a3a");
        config->color_date_pressed_arrow = ArgbColor("#ff6f6f6f");
        config->color_date_text_default_button = ArgbColor("#ff024376");
        config->color_date_text_hovered_button = ArgbColor("#ff5c5c5c");
        config->color_date_text_pressed_button = ArgbColor("#ff8a8a8a");
        config->color_date_cursor = ArgbColor("#ff020202");
        config->color_date_text_prompt = ArgbColor("#ff424242");
        config->color_volume_background = ArgbColor("#f3e4e4e4");
        config->color_volume_text = ArgbColor("#ff020202");
        config->color_volume_default_icon = ArgbColor("#ff2e2e2e");
        config->color_volume_hovered_icon = ArgbColor("#ff000000");
        config->color_volume_pressed_icon = ArgbColor("#ff1f1f1f");
        config->color_volume_slider_background = ArgbColor("#ff8a8a8a");
        config->color_volume_slider_foreground = ArgbColor("#ff0078d7");
        config->color_volume_slider_active = ArgbColor("#ff000000");
        config->color_apps_background = ArgbColor("#f3e4e4e4");
        config->color_apps_text = ArgbColor("#ff000000");
        config->color_apps_text_inactive = ArgbColor("#ff505050");
        config->color_apps_icons = ArgbColor("#ff000000");
        config->color_apps_default_item = ArgbColor("#00ffffff");
        config->color_apps_hovered_item = ArgbColor("#D0ffffff");
        config->color_apps_pressed_item = ArgbColor("#90ffffff");
        config->color_apps_item_icon_background = ArgbColor("#ff3380cc");
        config->color_apps_scrollbar_gutter = ArgbColor("#ffcecece");
        config->color_apps_scrollbar_default_thumb = ArgbColor("#ffa5a5a5");
        config->color_apps_scrollbar_hovered_thumb = ArgbColor("#ff7c7c7c");
        config->color_apps_scrollbar_pressed_thumb = ArgbColor("#ff525252");
        config->color_apps_scrollbar_default_button = ArgbColor("#ffcecece");
        config->color_apps_scrollbar_hovered_button = ArgbColor("#ffbababa");
        config->color_apps_scrollbar_pressed_button = ArgbColor("#ff535353");
        config->color_apps_scrollbar_default_button_icon = ArgbColor("#ff646464");
        config->color_apps_scrollbar_hovered_button_icon = ArgbColor("#ff5a5a5a");
        config->color_apps_scrollbar_pressed_button_icon = ArgbColor("#ffd1d1d1");
        config->color_pin_menu_background = ArgbColor("#f3e4e4e4");
        config->color_pin_menu_hovered_item = ArgbColor("#D0ffffff");
        config->color_pin_menu_pressed_item = ArgbColor("#90ffffff");
        config->color_pin_menu_text = ArgbColor("#ff000000");
        config->color_pin_menu_icons = ArgbColor("#ff000000");
        config->color_windows_selector_default_background = ArgbColor("#f3e4e4e4");
        config->color_windows_selector_hovered_background = ArgbColor("#D0ffffff");
        config->color_windows_selector_pressed_background = ArgbColor("#90ffffff");
        config->color_windows_selector_close_icon = ArgbColor("#ff0d0d0d");
        config->color_windows_selector_close_icon_hovered = ArgbColor("#ffffffff");
        config->color_windows_selector_close_icon_pressed = ArgbColor("#ffffffff");
        config->color_windows_selector_text = ArgbColor("#ff000000");
        config->color_windows_selector_close_icon_hovered_background = ArgbColor("#ffc61a28");
        config->color_windows_selector_close_icon_pressed_background = ArgbColor("#ffe81123");
        config->color_windows_selector_attention_background = ArgbColor("#ffFFAB3C");
        config->color_search_tab_bar_background = ArgbColor("#f3e4e4e4");
        config->color_search_accent = ArgbColor("#ff0078d7");
        config->color_search_tab_bar_default_text = ArgbColor("#ff5c5c5c");
        config->color_search_tab_bar_hovered_text = ArgbColor("#ff3c3c3c");
        config->color_search_tab_bar_pressed_text = ArgbColor("#ff4c4c4c");
        config->color_search_tab_bar_active_text = ArgbColor("#ff000000");
        config->color_search_empty_tab_content_background = ArgbColor("#f3ececec");
        config->color_search_empty_tab_content_icon = ArgbColor("#ffbdbdbd");
        config->color_search_empty_tab_content_text = ArgbColor("#ff5f5f5f");
        config->color_search_content_left_background = ArgbColor("#fff0f0f0");
        config->color_search_content_right_background = ArgbColor("#fff5f5f5");
        config->color_search_content_right_foreground = ArgbColor("#ffffffff");
        config->color_search_content_right_splitter = ArgbColor("#fff2f2f2");
        config->color_search_content_text_primary = ArgbColor("#ff010101");
        config->color_search_content_text_secondary = ArgbColor("#ff606060");
        config->color_search_content_right_button_default = ArgbColor("#00000000");
        config->color_search_content_right_button_hovered = ArgbColor("#26000000");
        config->color_search_content_right_button_pressed = ArgbColor("#51000000");
        config->color_search_content_left_button_splitter = ArgbColor("#ffffffff");
        config->color_search_content_left_button_default = ArgbColor("#00000000");
        config->color_search_content_left_button_hovered = ArgbColor("#24000000");
        config->color_search_content_left_button_pressed = ArgbColor("#48000000");
        config->color_search_content_left_button_active = ArgbColor("#ffa8cce9");
        config->color_search_content_left_set_active_button_default = ArgbColor("#00000000");
        config->color_search_content_left_set_active_button_hovered = ArgbColor("#22000000");
        config->color_search_content_left_set_active_button_pressed = ArgbColor("#19000000");
        config->color_search_content_left_set_active_button_active = ArgbColor("#ff97b8d2");
        config->color_search_content_left_set_active_button_icon_default = ArgbColor("#ff606060");
        config->color_search_content_left_set_active_button_icon_pressed = ArgbColor("#ffffffff");
        config->color_pinned_icon_editor_background = ArgbColor("#ffffffff");
        config->color_pinned_icon_editor_field_default_text = ArgbColor("#ff000000");
        config->color_pinned_icon_editor_field_hovered_text = ArgbColor("#ff2d2d2d");
        config->color_pinned_icon_editor_field_pressed_text = ArgbColor("#ff020202");
        config->color_pinned_icon_editor_field_default_border = ArgbColor("#ffb4b4b4");
        config->color_pinned_icon_editor_field_hovered_border = ArgbColor("#ff646464");
        config->color_pinned_icon_editor_field_pressed_border = ArgbColor("#ff0078d7");
        config->color_pinned_icon_editor_cursor = ArgbColor("#ff000000");
        config->color_pinned_icon_editor_button_default = ArgbColor("#ffcccccc");
        config->color_pinned_icon_editor_button_text_default = ArgbColor("#ff000000");
        config->color_notification_content_background = ArgbColor("#ffe4e4e4");
        config->color_notification_title_background = ArgbColor("#ffCBCBCB");
        config->color_notification_content_text = ArgbColor("#ff000000");
        config->color_notification_title_text = ArgbColor("#ff000000");
        config->color_notification_button_default = ArgbColor("#ffffffff");
        config->color_notification_button_hovered = ArgbColor("#fff2f2f2");
        config->color_notification_button_pressed = ArgbColor("#ffbfbfbf");
        config->color_notification_button_text_default = ArgbColor("#ff000000");
        config->color_notification_button_text_hovered = ArgbColor("#ff000000");
        config->color_notification_button_text_pressed = ArgbColor("#ff000000");
        config->color_notification_button_send_to_action_center_default = ArgbColor("#ff3c3c3c");
        config->color_notification_button_send_to_action_center_hovered = ArgbColor("#ff222222");
        config->color_notification_button_send_to_action_center_pressed = ArgbColor("#ff000000");
        config->color_action_center_background = ArgbColor("#ffe4e4e4");
        config->color_action_center_history_text = ArgbColor("#ff002642");
        config->color_action_center_no_new_text = ArgbColor("#ff5b5b5b");
        config->color_action_center_notification_content_background = ArgbColor("#fff4f4f4");
        config->color_action_center_notification_title_background = ArgbColor("#ffe4e4e4");
        config->color_action_center_notification_content_text = ArgbColor("#ff000000");
        config->color_action_center_notification_title_text = ArgbColor("#ff000000");
        config->color_action_center_notification_button_default = ArgbColor("#ffffffff");
        config->color_action_center_notification_button_hovered = ArgbColor("#fff2f2f2");
        config->color_action_center_notification_button_pressed = ArgbColor("#ffbfbfbf");
        config->color_action_center_notification_button_text_default = ArgbColor("#ff000000");
        config->color_action_center_notification_button_text_hovered = ArgbColor("#ff000000");
        config->color_action_center_notification_button_text_pressed = ArgbColor("#ff000000");
    }

}