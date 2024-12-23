//
// Created by jmanc3 on 10/18/23.
//

#include <pango/pangocairo.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <utility>
#include <any>
#include "settings_menu.h"
#include "main.h"
#include "config.h"
#include "components.h"
#include "utility.h"
#include "simple_dbus.h"
#include "search_menu.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

WinbarSettings *winbar_settings = new WinbarSettings;

void merge_order_with_taskbar();

static void paint_reordable_item(AppClient *client, cairo_t *cr, Container *container);

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_pinned_icon_editor_background));
    cairo_fill(cr);
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto label = (Label *) container->user_data;
    int size = label->size;
    if (size == -1)
        size = 9 * config->dpi;
    PangoLayout *layout = get_cached_pango_font(cr, config->font, size, label->weight);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    if (label->color.a == 0) {
        set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    } else {
        set_argb(cr, label->color);
    }
    cairo_move_to(cr, container->real_bounds.x + container->wanted_pad.x,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void paint_draggable(AppClient *client, cairo_t *cr, Container *container) {
    float dot_size = 3 * config->dpi;
    float height = dot_size * 5; // three real and 2 spaces between
    float start_x = container->real_bounds.x + container->real_bounds.w / 2 - dot_size / 2 - dot_size;
    float start_y = container->real_bounds.y + container->real_bounds.h / 2 - height / 2;
    set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            set_rect(cr, Bounds(start_x + (dot_size * x) + (dot_size * x),
                                start_y + (dot_size * y) + (dot_size * y),
                                dot_size,
                                dot_size));
        }
    }
    cairo_fill(cr);
}

struct Checkbox : UserData {
    bool on = true;
    std::string container_name;
    std::string name;
};

struct GenericComboBox : UserData {
    std::string name; // of creation container
    std::string prompt;
    std::vector<std::string> options;
    
    std::string (*determine_selected)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    void (*when_clicked)(AppClient *, cairo_t *, Container *);
    
    explicit GenericComboBox(std::string name) : name(std::move(name)) {}
    
    GenericComboBox(std::string name, std::string prompt) : name(std::move(name)), prompt(std::move(prompt)) {}
};

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
    
    float size = 14 * config->dpi;
    rounded_rect(cr, 2 * config->dpi,
                 container->real_bounds.x + container->real_bounds.w / 2 - size / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - size / 2,
                 size, size);
    set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    if (container->state.mouse_hovering)
        set_argb(cr, config->color_pinned_icon_editor_field_hovered_border);
    if (container->state.mouse_pressing)
        set_argb(cr, config->color_pinned_icon_editor_field_pressed_border);
    
    if (data->on)
        set_argb(cr, config->color_pinned_icon_editor_field_pressed_border);
    
    if (data->on) {
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, std::round(1 * config->dpi));
        cairo_stroke(cr);
    }
    
    if (!data->on)
        return;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 14 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uF13E", strlen("\uE83F"));
    
    set_argb(cr, ArgbColor(1, 1, 1, 1));
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2 - 1 * config->dpi));
    pango_cairo_show_layout(cr, layout);
}

static void paint_generic_combobox(AppClient *client, cairo_t *cr, Container *container) {
    auto old = container->real_bounds;
    int pad = 3;
    container->real_bounds.y += pad * config->dpi;
    container->real_bounds.h -= (pad * 2) * config->dpi;
    paint_reordable_item(client, cr, container);
    container->real_bounds = old;
    
    auto *data = (GenericComboBox *) container->user_data;
    std::string selected = data->prompt;
    if (data->determine_selected)
        selected += data->determine_selected(client, cr, container);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, selected.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    cairo_move_to(cr, container->real_bounds.x + container->wanted_pad.x + 8 * config->dpi,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
    
    layout = get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE70D", strlen("\uE83F"));
    
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w - width - 8 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void paint_reordable_item(AppClient *client, cairo_t *cr, Container *container) {
    rounded_rect(cr, 4 * config->dpi, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w,
                 container->real_bounds.h);
    set_argb(cr, config->color_pinned_icon_editor_background);
    cairo_fill(cr);
    rounded_rect(cr, 4 * config->dpi, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w,
                 container->real_bounds.h);
    set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    if (container->state.mouse_hovering)
        set_argb(cr, config->color_pinned_icon_editor_field_hovered_border);
    if (container->state.mouse_pressing)
        set_argb(cr, config->color_pinned_icon_editor_field_pressed_border);
    cairo_set_line_width(cr, std::round(1 * config->dpi));
    cairo_stroke(cr);
}

static void paint_remove(AppClient *client, cairo_t *cr, Container *container) {
    // check: F13E
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, "\uE107", strlen("\uE83F"));
    
    set_argb(cr, ArgbColor(.8, .3, .1, 1));
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
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
        if (container->state.mouse_pressing) {
            set_argb(cr, darken(config->color_pinned_icon_editor_background, 15));
        } else if (container->state.mouse_hovering) {
            set_argb(cr, darken(config->color_pinned_icon_editor_background, 7));
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    cairo_move_to(cr, container->real_bounds.x + container->wanted_pad.x + 12 * config->dpi,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void clicked_expand_generic_combobox(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (GenericComboBox *) container->user_data;
    
    float total_text_height = 0;
    
    auto taskbar = client_by_name(app, "taskbar");
    PangoLayout *layout = get_cached_pango_font(taskbar->cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    for (const auto &m: data->options) {
        pango_layout_set_text(layout, m.c_str(), -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        total_text_height += height;
    }
    total_text_height += data->options.size() * (12 * config->dpi); // pad
    
    Settings settings;
    settings.force_position = true;
    settings.w = container->real_bounds.w;
    settings.h = total_text_height;
    settings.x = client->bounds->x + container->real_bounds.x;
    settings.y = client->bounds->y + container->real_bounds.y + container->real_bounds.h - 2 * config->dpi;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.no_input_focus = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    PopupSettings popup_settings;
    popup_settings.takes_input_focus = false;
    popup_settings.close_on_focus_out = true;
    popup_settings.wants_grab = true;
    
    auto popup = client->create_popup(popup_settings, settings);
    popup->root->when_paint = paint_root;
    popup->root->type = ::vbox;
    for (const auto &m: data->options) {
        auto c = popup->root->child(FILL_SPACE, FILL_SPACE);
        c->when_paint = paint_combox_item;
        c->when_clicked = data->when_clicked;
        auto label = new Label(m);
        c->user_data = label;
    }
    client_show(app, popup);
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
    auto r = reorder_list->child(::hbox, FILL_SPACE, 28 * config->dpi);
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
                    client_paint(app, c);
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
                    client_paint(app, c);
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
        
        pango_layout_set_text(layout, "Font Size: ", -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        label = r->child(::hbox, width + r->wanted_bounds.h * .5, FILL_SPACE);
        label->wanted_pad.x = r->wanted_bounds.h * .4;
        label->when_paint = paint_label;
        data = new Label("Font Size: ");
        label->user_data = data;
        
        {
            auto parent = r->child(38 * config->dpi, FILL_SPACE);
//            parent->clip = true;
            parent->wanted_pad.y = 3 * config->dpi;
            parent->wanted_pad.h = 2 * config->dpi;
            FieldSettings field_settings;
            field_settings.only_numbers = true;
            field_settings.font_size = 9;
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
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    cairo_move_to(cr, container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
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
    taskbar->root->children.clear();
    for (auto c: containers) {
        taskbar->root->children.push_back(c);
    }
    // No matter what, bluetooth does not exist until dbus says it does.
    container_by_name("bluetooth", taskbar->root)->exists = false;
    container_by_name("icons", taskbar->root)->alignment = winbar_settings->icons_alignment;
    
    client_layout(app, taskbar);
    client_paint(app, taskbar);
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
    winbar_settings->bluetooth_enabled = true;
    winbar_settings->date_alignment = PangoAlignment::PANGO_ALIGN_CENTER;
    winbar_settings->date_style = "windows 11 detailed";
    winbar_settings->date_size = 9;
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
        data->size = 20 * config->dpi;
        title->when_paint = paint_label;
        title->user_data = data;
    }
    container->child(FILL_SPACE, 32 * config->dpi);
}

struct OriginalBool : public UserData {
    bool *boolean = nullptr;
    
    explicit OriginalBool(bool *boolean) : boolean(boolean) {}
};

void bool_checkbox_indent(const std::string &text, int indent, bool *boolean, Container *container, AppClient *client) {
    float font_size = std::round(10 * config->dpi);
    float pad = std::round(12 * config->dpi);
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, font_size, PANGO_WEIGHT_NORMAL);
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
    full_label_container->user_data = new OriginalBool(boolean);
    
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
    };
    full_label_container->receive_events_even_if_obstructed_by_one = true;
}

void bool_checkbox(const std::string &text, bool *boolean, Container *container, AppClient *client) {
    bool_checkbox_indent(text, 0, boolean, container, client);
}

static void paint_textarea_border(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (container->state.mouse_hovering || container->state.mouse_pressing || container->active) {
        if (container->state.mouse_pressing || container->active) {
            if (container->state.mouse_pressing && container->active) {
                set_argb(cr, lighten(config->color_pinned_icon_editor_field_pressed_border, 7));
            } else {
                set_argb(cr, config->color_pinned_icon_editor_field_pressed_border);
            }
        } else {
            set_argb(cr, config->color_pinned_icon_editor_field_hovered_border);
        }
    } else {
        set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    }
    paint_margins_rect(client, cr, container->real_bounds, 2, 0);
}

struct PathHolder : public UserData {
    explicit PathHolder(std::string *path) : path(path) {}
    
    std::string *path = nullptr;
};

void string_textfield(const std::string &text, std::string *path, Container *container, AppClient *client, std::string prompt = "") {
    int font_size = std::round(10 * config->dpi);
    auto hbox = container->child(::hbox, FILL_SPACE, 26 * config->dpi);
    hbox->user_data = new PathHolder(path);
    hbox->alignment = ALIGN_CENTER;
    
    auto label = hbox->child(FILL_SPACE, FILL_SPACE);
    auto label_data = new Label(text);
    label_data->size = font_size;
    label->when_paint = paint_label;
    label->user_data = label_data;
    
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, font_size, PANGO_WEIGHT_NORMAL);
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
    settings.font_size = 11 * config->dpi;
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

static void
fill_root(AppClient *client, Container *root) {
    auto real_root = root;
    real_root->type = ::hbox;
    real_root->when_paint = paint_root;
    
    auto tabs = root->child(300 * config->dpi, FILL_SPACE);
    tabs->wanted_pad = Bounds(16 * config->dpi, 140 * config->dpi, 0, 0);
    
    auto root_stack = root->child(layout_type::stack, FILL_SPACE, FILL_SPACE);
    
    auto taskbar_order_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    title("Taskbar Order", taskbar_order_root);
    
    auto winbar_behaviour_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    winbar_behaviour_root->exists = false;
    title("Winbar Behaviour", winbar_behaviour_root);
    bool_checkbox("Thumbnails", &winbar_settings->thumbnails, winbar_behaviour_root, client);
    bool_checkbox("Battery warning notifications", &winbar_settings->battery_notifications, winbar_behaviour_root,
                  client);
    bool_checkbox("Pinned icons shortcuts: Meta+[0-9]", &winbar_settings->pinned_icon_shortcut, winbar_behaviour_root,
                  client);
    string_textfield("Custom desktop files location: ", &winbar_settings->custom_desktops_directory,
                     winbar_behaviour_root, client);
    bool_checkbox_indent("Make directory the exclusive source for desktop files", 16 * config->dpi,
                         &winbar_settings->custom_desktops_directory_exclusive, winbar_behaviour_root, client);
    bool_checkbox("Ignore 'Only Show In' instruction in desktop files", &winbar_settings->ignore_only_show_in, winbar_behaviour_root, client);
    bool_checkbox("Meter animations in volume menu", &winbar_settings->meter_animations, winbar_behaviour_root, client);
    bool_checkbox("Use default super icon", &winbar_settings->super_icon_default, winbar_behaviour_root, client);
    bool_checkbox("Labels", &winbar_settings->labels, winbar_behaviour_root, client);
    bool_checkbox_indent("Uniform pinned icon width", 16 * config->dpi,
                         &winbar_settings->label_uniform_size, winbar_behaviour_root, client);
    bool_checkbox("Icon minimize/maximize bounce animation", &winbar_settings->minimize_maximize_animation, winbar_behaviour_root, client);
    string_textfield("Shutdown command: ", &winbar_settings->shutdown_command, winbar_behaviour_root, client, "pkexec shutdown -P now");
    winbar_behaviour_root->child(FILL_SPACE, 6 * config->dpi);
    string_textfield("Restart command: ", &winbar_settings->restart_command, winbar_behaviour_root, client, "pkexec reboot");
    
    auto other_root = root_stack->child(FILL_SPACE, FILL_SPACE);
    other_root->exists = false;
    title("Other", other_root);
    
    for (int i = 0; i < 3; ++i) {
        auto tab = tabs->child(FILL_SPACE, 36 * config->dpi);
        if (i == 0) {
            tab->user_data = new SelectedLabel("Taskbar Order");
            ((SelectedLabel *) tab->user_data)->selected = true;
        } else if (i == 1) {
            tab->user_data = new SelectedLabel("Winbar Behaviour");
        } else if (i == 2) {
            tab->user_data = new SelectedLabel("Other");
        }
        ((SelectedLabel *) tab->user_data)->index = i;
        
        tab->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
            for (int j = 0; j < container->parent->children.size(); ++j) {
                auto con = container->parent->children[j];
                auto *data = (SelectedLabel *) con->user_data;
                data->selected = false;
            }
            auto *data = (SelectedLabel *) container->user_data;
            client->root->children[1]->children[0]->exists = false;
            client->root->children[1]->children[1]->exists = false;
            client->root->children[1]->children[2]->exists = false;
            client->root->children[1]->children[data->index]->exists = true;
            client_layout(app, client);
            data->selected = true;
        };
        
        tab->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
            auto *data = (SelectedLabel *) container->user_data;
            
            if (data->selected) {
                auto a = darken(config->color_pinned_icon_editor_background, 3);
                set_argb(cr, a);
                rounded_rect(cr, container->real_bounds.h * .13, container->real_bounds.x, container->real_bounds.y,
                             container->real_bounds.w, container->real_bounds.h);
                cairo_fill(cr);
            }
            
            if (data->selected) {
                set_argb(cr, config->color_search_accent);
                auto height = container->real_bounds.h * .55;
                auto width = std::round(4 * config->dpi);
                rounded_rect(cr, width * .44,
                             container->real_bounds.x,
                             container->real_bounds.y + container->real_bounds.h / 2 - height / 2,
                             width, height);
                cairo_fill(cr);
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
        auto x = root->child(::hbox, FILL_SPACE, 28 * config->dpi);
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
    
    // Taskbar order
    out_file << "order=";

#define WRITE(button_name, container_name) \
    if (child->name == container_name) {   \
       out_file << "\"" << button_name << "\"=" << (child->exists ? "on," : "off,"); \
       continue; \
    }
    
    if (auto c = client_by_name(app, "taskbar")) {
        for (auto child: c->root->children) {
            WRITE("Super", "super")
            WRITE("Search Field", "field_search")
            WRITE("Workspace", "workspace")
            WRITE("Pinned Icons", "icons")
            WRITE("Systray", "systray")
            if (child->name == "bluetooth") {
                out_file << "\"" << "Bluetooth" << "\"=" << (winbar_settings->bluetooth_enabled ? "on," : "off,");
                continue;
            }
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
    
    out_file << "custom_desktops_directory=" << winbar_settings->custom_desktops_directory;
    out_file << std::endl << std::endl;
    
    out_file << "shutdown_command=" << winbar_settings->shutdown_command;
    out_file << std::endl << std::endl;
    
    out_file << "restart_command=" << winbar_settings->restart_command;
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
    if (input_file.good()) {
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
            }  else if (key == "search_behaviour") {
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
            } else {
                if (key.empty())
                    continue;
                parse_bool(&parser, key, "battery_notifications", &winbar_settings->battery_notifications);
                parse_bool(&parser, key, "pinned_icon_shortcut", &winbar_settings->pinned_icon_shortcut);
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
                parse_string(&parser, key, "custom_desktops_directory", &winbar_settings->custom_desktops_directory);
                parse_string(&parser, key, "shutdown_command", &winbar_settings->shutdown_command);
                parse_string(&parser, key, "restart_command", &winbar_settings->restart_command);
            }
        }
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
    merge_order_with_taskbar();
}
