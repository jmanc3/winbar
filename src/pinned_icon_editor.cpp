//
// Created by jmanc3 on 5/12/21.
//

#include <pango/pangocairo.h>
#include "pinned_icon_editor.h"
#include "application.h"
#include "main.h"
#include "components.h"
#include "config.h"
#include "taskbar.h"
#include "icons.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

static Container *pinned_icon_container = nullptr;
static LaunchableButton *pinned_icon_data = nullptr;
static TextAreaData *icon_field_data = nullptr;
static TextAreaData *launch_field_data = nullptr;
static TextAreaData *wm_field_data = nullptr;

class Label : public UserData {
public:
    std::string text;
    
    explicit Label(std::string text) {
        this->text = text;
    }
};

static Label *icon_search_state = nullptr;

static void paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_pinned_icon_editor_background);
    cairo_fill(cr);
}

static void paint_icon(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    IconButton *icon_data = (IconButton *) container->user_data;
    if (icon_data->surface) {
        cairo_set_source_surface(cr, icon_data->surface, container->real_bounds.x, container->real_bounds.y);
        cairo_paint(cr);
    }
}

static void paint_state_label(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto label = (Label *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    if (width < container->real_bounds.w) {
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
    }
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    
    
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y + container->real_bounds.h);
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
    pango_layout_set_width(layout, -1);
}

static void paint_label(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto label = (Label *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_pinned_icon_editor_field_default_text);
    cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y + container->real_bounds.h);
    pango_cairo_show_layout(cr, layout);
}

static void paint_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor color = config->color_pinned_icon_editor_button_default;
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            set_argb(cr, darken(color, 18));
            cairo_fill(cr);
        } else {
            set_argb(cr, color);
            set_rect(cr, container->real_bounds);
            cairo_fill(cr);
            
            set_argb(cr, darken(color, 18));
            paint_margins_rect(client, cr, container->real_bounds, 2, 0);
        }
    } else {
        set_rect(cr, container->real_bounds);
        set_argb(cr, color);
        cairo_fill(cr);
    }
    
    auto label = (Label *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_pinned_icon_editor_button_text_default);
    cairo_move_to(cr, container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void paint_restore(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor color = config->color_pinned_icon_editor_button_default;
    
    bool disabled = true;
    if (pinned_icon_data->command_launched_by != launch_field_data->state->text ||
        pinned_icon_data->icon_name != icon_field_data->state->text | \
        pinned_icon_data->class_name != wm_field_data->state->text) {
        disabled = false;
    }
    
    if (disabled) {
        set_rect(cr, container->real_bounds);
        set_argb(cr, color);
        cairo_fill(cr);
    } else {
        if (container->state.mouse_hovering || container->state.mouse_pressing) {
            if (container->state.mouse_pressing) {
                set_rect(cr, container->real_bounds);
                set_argb(cr, darken(color, 18));
                cairo_fill(cr);
            } else {
                set_argb(cr, color);
                set_rect(cr, container->real_bounds);
                cairo_fill(cr);
                
                set_argb(cr, darken(color, 18));
                paint_margins_rect(client, cr, container->real_bounds, 2, 0);
            }
        } else {
            set_rect(cr, container->real_bounds);
            set_argb(cr, color);
            cairo_fill(cr);
        }
    }
    
    
    auto label = (Label *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    if (disabled) {
        set_argb(cr, lighten(config->color_pinned_icon_editor_button_text_default, 45));
    } else {
        set_argb(cr, config->color_pinned_icon_editor_button_text_default);
    }
    cairo_move_to(cr, container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
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

static Container *make_button(AppClient *client, Container *parent, std::string text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *button = parent->child(FILL_SPACE, FILL_SPACE);
    
    PangoLayout *layout =
            get_cached_pango_font(client->cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    button->user_data = new Label(std::move(text));
    button->when_paint = paint_button;
    button->wanted_bounds.w = width + 64 * config->dpi;
    
    return button;
}

static void clicked_save_and_quit(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    pinned_icon_data->command_launched_by = launch_field_data->state->text;
    pinned_icon_data->class_name = wm_field_data->state->text;
    pinned_icon_data->icon_name = icon_field_data->state->text;
    client_close_threaded(client->app, client);
    update_pinned_items_file(false);
    
    update_pinned_items_icon();
}

static void update_icon(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto icon = container_by_name("icon", client->root)) {
        auto icon_data = (IconButton *) icon->user_data;
        
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(icon_field_data->state->text));
        search_icons(targets);
        pick_best(targets, 64 * config->dpi);
        std::string icon_path = targets[0].best_full_path;
        if (!icon_path.empty()) {
            if (icon_data->surface) {
                cairo_surface_destroy(icon_data->surface);
                icon_data->surface = nullptr;
            }
            load_icon_full_path(app, client, &icon_data->surface, icon_path, 64 * config->dpi);
            icon_search_state->text = "Found a match for: '" + icon_field_data->state->text + "'";
        } else {
            icon_search_state->text = "Didn't find a match for: '" + icon_field_data->state->text + "'";
            // TODO: this is kind of annoying, we should instead make the surface have a cairo_t and just clear the surface
            cairo_surface_destroy(icon_data->surface);
            icon_data->surface = nullptr;
        }
    }
}

static void clicked_restore(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool disabled = true;
    if (pinned_icon_data->command_launched_by != launch_field_data->state->text ||
        pinned_icon_data->icon_name != icon_field_data->state->text | \
        pinned_icon_data->class_name != wm_field_data->state->text) {
        disabled = false;
    }
    if (disabled)
        return;
    
    delete launch_field_data->state;
    launch_field_data->state = new TextState;
    launch_field_data->state->text = pinned_icon_data->command_launched_by;
    
    delete wm_field_data->state;
    wm_field_data->state = new TextState;
    wm_field_data->state->text = pinned_icon_data->class_name;
    
    delete icon_field_data->state;
    icon_field_data->state = new TextState;
    icon_field_data->state->text = pinned_icon_data->icon_name;
    
    update_icon(client);
}

static void clicked_cancel(AppClient *client, cairo_t *cr, Container *container) {
    client_close_threaded(client->app, client);
}

static void
icon_name_key_event(AppClient *client,
                    cairo_t *cr,
                    Container *container,
                    bool is_string, xkb_keysym_t keysym, char string[64],
                    uint16_t mods,
                    xkb_key_direction direction) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (direction == XKB_KEY_UP) {
        return;
    }
    if (container->parent->active) {
        textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
        
        update_icon(client);
    }
}

void fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *root = client->root;
    root->wanted_pad = Bounds(10 * config->dpi, 10 * config->dpi, 10 * config->dpi, 10 * config->dpi);
    root->spacing = 10 * config->dpi;
    root->type = vbox;
    root->when_paint = paint_background;
    
    TextAreaSettings textarea_settings = TextAreaSettings();
    textarea_settings.single_line = true;
    textarea_settings.bottom_show_amount = 2;
    textarea_settings.right_show_amount = 2;
    textarea_settings.font_size = 13 * config->dpi;
    textarea_settings.color = config->color_pinned_icon_editor_field_default_text;
    textarea_settings.color_cursor = config->color_pinned_icon_editor_cursor;
    textarea_settings.pad = Bounds(4 * config->dpi, 3 * config->dpi, 8 * config->dpi, 0);
    
    { // Icon
        Container *centered = root->child(::hbox, FILL_SPACE, 64 * config->dpi);
        centered->child(FILL_SPACE, FILL_SPACE);
        
        Container *icon = centered->child(64 * config->dpi, 64 * config->dpi);
        icon->name = "icon";
        auto icon_data = new IconButton;
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(pinned_icon_data->icon_name));
        search_icons(targets);
        pick_best(targets, 64 * config->dpi);
        std::string icon_path = targets[0].best_full_path;
        if (!icon_path.empty()) {
            load_icon_full_path(app, client, &icon_data->surface, icon_path, 64 * config->dpi);
            icon_search_state = new Label("Found a match for: '" + pinned_icon_data->icon_name + "'");
        } else {
            icon_search_state = new Label("Didn't find a match for: '" + pinned_icon_data->icon_name + "'");
        }
        
        icon->user_data = icon_data;
        icon->when_paint = paint_icon;
        
        centered->child(FILL_SPACE, FILL_SPACE);
    }
    
    { // Icon search state label
        Container *icon_search_state_label = root->child(FILL_SPACE, 13 * config->dpi);
        icon_search_state_label->user_data = icon_search_state;
        icon_search_state_label->when_paint = paint_state_label;
    }
    
    Container *icon_name_hbox = root->child(layout_type::hbox, FILL_SPACE, 64 * config->dpi);
    icon_name_hbox->spacing = 10 * config->dpi;
    {
        Container *icon_name_label_and_field_vbox = icon_name_hbox->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
        {
            Container *icon_name_label = icon_name_label_and_field_vbox->child(FILL_SPACE, 13 * config->dpi);
            icon_name_label->user_data = new Label("Icon");
            icon_name_label->when_paint = paint_label;
            
            icon_name_label_and_field_vbox->child(FILL_SPACE, FILL_SPACE);
            
            Container *icon_name_field = make_textarea(app, client, icon_name_label_and_field_vbox, textarea_settings);
            icon_name_field->parent->when_paint = paint_textarea_border;
            icon_name_field->when_key_event = icon_name_key_event;
            auto icon_name_field_data = (TextAreaData *) icon_name_field->user_data;
            icon_name_field_data->state->text = pinned_icon_data->icon_name;
            icon_field_data = icon_name_field_data;
//            icon_name_field->when_paint = paint_ex;
            icon_name_field->wanted_bounds.h = 32 * config->dpi;
        }
    }
    
    Container *launch_command_label_and_field = root->child(layout_type::vbox, FILL_SPACE, 64 * config->dpi);
    {
        Container *launch_command_label = launch_command_label_and_field->child(FILL_SPACE, 13 * config->dpi);
        launch_command_label->user_data = new Label("Command");
        launch_command_label->when_paint = paint_label;
        
        launch_command_label_and_field->child(FILL_SPACE, FILL_SPACE);
        
        Container *launch_command_field = make_textarea(app, client, launch_command_label_and_field, textarea_settings);
        launch_command_field->parent->when_paint = paint_textarea_border;
        auto launch_command_field_data = (TextAreaData *) launch_command_field->user_data;
        launch_command_field_data->state->text = pinned_icon_data->command_launched_by;
        launch_field_data = launch_command_field_data;
//        launch_command_field->when_paint = paint_ex;
        launch_command_field->wanted_bounds.h = 32 * config->dpi;
    }
    
    Container *wm_class_label_and_field = root->child(layout_type::vbox, FILL_SPACE, 64 * config->dpi);
    {
        Container *wm_class_label = wm_class_label_and_field->child(FILL_SPACE, 13 * config->dpi);
        wm_class_label->user_data = new Label("WM_CLASS");
        wm_class_label->when_paint = paint_label;
        
        wm_class_label_and_field->child(FILL_SPACE, FILL_SPACE);
        
        Container *wm_class_field = make_textarea(app, client, wm_class_label_and_field, textarea_settings);
        wm_class_field->parent->when_paint = paint_textarea_border;
        auto wm_class_field_data = (TextAreaData *) wm_class_field->user_data;
        wm_class_field_data->state->text = pinned_icon_data->class_name;
        wm_field_data = wm_class_field_data;
//        wm_class_field->when_paint = paint_ex;i
        wm_class_field->wanted_bounds.h = 32 * config->dpi;
    }
    
    root->child(FILL_SPACE, FILL_SPACE);
    
    Container *button_hbox = root->child(::hbox, FILL_SPACE, 30 * config->dpi);
    button_hbox->spacing = 10 * config->dpi;
    {
        button_hbox->child(FILL_SPACE, FILL_SPACE);
        
        Container *save_button = make_button(client, button_hbox, "Save & Quit");
        save_button->when_clicked = clicked_save_and_quit;
        
        Container *restore_button = make_button(client, button_hbox, "Restore");
        restore_button->when_paint = paint_restore;
        restore_button->when_clicked = clicked_restore;
        
        Container *cancel_button = make_button(client, button_hbox, "Close");
        cancel_button->when_clicked = clicked_cancel;
    }
}

void start_pinned_icon_editor(Container *icon_container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    pinned_icon_container = icon_container;
    pinned_icon_data = (LaunchableButton *) icon_container->user_data;
    if (!pinned_icon_container || !pinned_icon_data) {
        pinned_icon_container = nullptr;
        pinned_icon_data = nullptr;
        return;
    }
    
    Settings settings;
    settings.w = 600 * config->dpi;
    settings.h = 450 * config->dpi;
    if (auto client = client_new(app, settings, "pinned_icon_editor")) {
        fill_root(client);
        std::string title = "Pinned Icon Editor";
        xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
        client_show(app, client);
    }
}