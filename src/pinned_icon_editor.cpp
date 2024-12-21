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
#include "app_menu.h"
#include <sys/stat.h>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

static Container *pinned_icon_container = nullptr;
static LaunchableButton *pinned_icon_data = nullptr;
static bool creating_not_editing = false;
static bool saved = false;
static TextAreaData *icon_field_data = nullptr;
static TextAreaData *launch_field_data = nullptr;
static TextAreaData *wm_field_data = nullptr;
static TextAreaData *name_field_data = nullptr;

static int active_option = 0;
static bool active_option_preferred = false;
static int max_options = 0;

struct NumberedLabel : UserData {
    int number = 0;
    Label *label = nullptr;
    
    ~NumberedLabel() {
        delete label;
    }
};


static void paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_pinned_icon_editor_background);
    cairo_fill(cr);
}


static void paint_icon_list_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_pinned_icon_editor_background);
    cairo_fill(cr);
    
    Bounds bounds = container->real_bounds;
    set_argb(cr, config->color_pinned_icon_editor_field_default_border);
    paint_margins_rect(client, cr, bounds, 1, 0);
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
    pango_layout_get_pixel_size_safe(layout, &width, &height);
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
    PangoWeight weight = PangoWeight::PANGO_WEIGHT_BOLD;
    if (label->weight == PangoWeight::PANGO_WEIGHT_BOLD)
        weight = PangoWeight::PANGO_WEIGHT_NORMAL;
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 11 * config->dpi, weight);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
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
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_pinned_icon_editor_button_text_default);
    cairo_move_to(cr, container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void update_icon(AppClient *client);

static bool has_desktop_file();

static void paint_icon_option(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor color = config->color_pinned_icon_editor_background;
    auto label = (NumberedLabel *) container->user_data;
    if (container->state.mouse_hovering || container->state.mouse_pressing ||
        (label->number == active_option && active_option_preferred)) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            set_argb(cr, darken(color, 18));
            cairo_fill(cr);
        } else {
            if (label->number != active_option) {
                active_option = label->number;
                AppClient *editor = client_by_name(app, "pinned_icon_editor");
                update_icon(editor);
                request_refresh(app, editor);
            }
            active_option = label->number;
            active_option_preferred = true;
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
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, label->label->text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
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
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
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
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    button->user_data = new Label(std::move(text));
    button->when_paint = paint_button;
    button->wanted_bounds.w = width + 64 * config->dpi;
    
    return button;
}

static std::string trim(std::string str) {
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

static void clicked_create_destroy_desktop_file(AppClient *client, cairo_t *cr, Container *container) {
    const char *home = getenv("HOME");
    std::string desktop_path(home);
    desktop_path += "/.local/share/applications/" + wm_field_data->state->text + ".desktop";
    auto label = (Label *) container->user_data;
    
    if (has_desktop_file()) {
        try {
            if (std::filesystem::exists(desktop_path)) {
                std::filesystem::remove(desktop_path);
            }
        } catch (const std::exception &e) {
            std::cerr << "Error deleting file: " << e.what() << "\n";
        }
        label->text = "Make Desktop File";
    } else {
        label->text = "Remove Desktop File";
        // add it
        try {
            // Convert the file path to a filesystem path object
            std::filesystem::path path(desktop_path);
            
            // Extract the parent directory path
            std::filesystem::path parentDir = path.parent_path();
            
            // Create directories if they don't exist
            if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
                std::filesystem::create_directories(parentDir);
            }
            
            // Open the file and write content
            std::ofstream outFile(desktop_path);
            if (!outFile) {
                throw std::ios_base::failure("Failed to open file for writing.");
            }
            
            outFile << "[Desktop Entry]\n";
            outFile << "Icon=" << icon_field_data->state->text << "\n";
            outFile << "Exec=" << launch_field_data->state->text << "\n";
            outFile << "StartupWMClass=" << wm_field_data->state->text << "\n";
            if (trim(name_field_data->state->text).empty()) {
                outFile << "Name=" << wm_field_data->state->text << "\n";
            } else {
                outFile << "Name=" << name_field_data->state->text << "\n";
            }
            outFile.close();
        } catch (const std::exception &e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    
    load_all_desktop_files();
}

static void clicked_save_and_quit(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    pinned_icon_data->command_launched_by = launch_field_data->state->text;
    pinned_icon_data->class_name = wm_field_data->state->text;
    pinned_icon_data->icon_name = icon_field_data->state->text;
    if (creating_not_editing) {
        if (auto *taskbar = client_by_name(app, "taskbar")) {
            saved = true;
            std::vector<IconTarget> targets;
            targets.emplace_back(pinned_icon_data->icon_name);
            search_icons(targets);
            pick_best(targets, 24 * config->dpi);
            bool found = false;
            for (auto &target: targets) {
                std::string path = target.best_full_path;
                if (!path.empty()) {
                    auto *data = (LaunchableButton *) pinned_icon_data;
                    load_icon_full_path(app, taskbar, &data->surface, path, 24 * config->dpi);
                    found = true;
                    break;
                }
            }
            if (!found) {
                pinned_icon_data->surface = accelerated_surface(app, client, 24 * config->dpi, 24 * config->dpi);
                paint_surface_with_image(pinned_icon_data->surface, as_resource_path("unknown-24.svg"),
                                         24 * config->dpi, nullptr);
            }
        }
    }
    client_close_threaded(client->app, client);
    update_pinned_items_file(false);
    
    update_pinned_items_icon();
}

static void update_icon(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::string active_text = icon_field_data->state->text;
    if (auto popup = client_by_name(app, "icon_list_popup")) {
        if (auto s = container_by_name("icon_list_scroll", popup->root)) {
            ScrollContainer *scroll_pane = (ScrollContainer *) s;
            
            for (auto item: scroll_pane->content->children) {
                auto nl = (NumberedLabel *) item->user_data;
                if (nl->number == active_option && active_option_preferred) {
                    active_text = nl->label->text;
                }
            }
        }
    }
    
    if (auto icon = container_by_name("icon", client->root)) {
        auto icon_data = (IconButton *) icon->user_data;
        
        std::vector<IconTarget> targets;
        
        targets.emplace_back(IconTarget(active_text));
        search_icons(targets);
        pick_best(targets, 64 * config->dpi);
        std::string icon_path = targets[0].best_full_path;
        if (!icon_path.empty()) {
            if (icon_data->surface) {
                cairo_surface_destroy(icon_data->surface);
                icon_data->surface = nullptr;
            }
            load_icon_full_path(app, client, &icon_data->surface, icon_path, 64 * config->dpi);
        } else {
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
switch_active(App *, AppClient *client, Timeout *, void *data) {
    auto *n = (std::string *) data;
    set_active(client, container_by_name((*n), client->root)->parent, true);
    delete n;
}

static void
show_icon_options() {
    auto c = client_by_name(app, "pinned_icon_editor");
    
    if (client_by_name(app, "icon_list_popup") == nullptr) {
        PopupSettings popup_settings;
        popup_settings.close_on_focus_out = true;
//        popup_settings.ignore_scroll = true;
        Container *field = container_by_name("icon_name_field", c->root);
        
        double pad = 0 * config->dpi;
        
        Settings settings;
        settings.skip_taskbar = true;
        settings.decorations = false;
        settings.override_redirect = true;
        settings.force_position = true;
        settings.x = c->bounds->x + field->parent->real_bounds.x + pad;
        settings.y = c->bounds->y + field->parent->real_bounds.y + field->parent->real_bounds.h;
        
        settings.w = field->parent->real_bounds.w - pad * 2;
//        settings.w = field->real_bounds.w;
        settings.h = 200;
        
        auto popup = c->create_popup(popup_settings, settings);
        popup->name = "icon_list_popup";
        popup->root->when_paint = paint_icon_list_background;
        popup->root->receive_events_even_if_obstructed = true;
        popup->when_closed = [](AppClient *) {
            AppClient *client = client_by_name(app, "pinned_icon_editor");
            if (client) {
                update_icon(client);
                request_refresh(app, client);
            }
            active_option = 0;
            max_options = 0;
            active_option_preferred = false;
        };
        popup->root->when_mouse_leaves_container = [](AppClient *, cairo_t *, Container *) {
            active_option_preferred = false;
        };
        
        ScrollPaneSettings scroll_settings(config->dpi);
        scroll_settings.right_width = 6 * config->dpi;
        scroll_settings.paint_minimal = true;
        ScrollContainer *s = make_newscrollpane_as_child(popup->root, scroll_settings);
        s->content->wanted_pad = Bounds(4 * config->dpi, 4 * config->dpi, 4 * config->dpi, 4 * config->dpi);
        s->content->spacing = 4 * config->dpi;
        s->content->clip = true;
        s->name = "icon_list_scroll";
        client_show(app, popup);
    }
    auto popup = client_by_name(app, "icon_list_popup");
    if (auto s = container_by_name("icon_list_scroll", popup->root)) {
        ScrollContainer *scroll = (ScrollContainer *) s;
        for (auto item: scroll->content->children)
            delete item;
        scroll->content->children.clear();
        
        auto *field = container_by_name("icon_name_field", c->root);
        std::string &target = ((TextAreaData *) field->user_data)->state->text;
        if (!target.empty()) {
            std::vector<std::string_view> names;
            get_options(names, target, 200);
            std::vector<IconTarget> targets;
            for (auto view: names) {
                targets.emplace_back(std::string(view));
            }
            search_icons(targets);
            
            if (!names.empty()) {
                std::vector<std::string> no_dups;
                for (int i = 0; i < targets.size(); i++) {
                    auto t = targets[i];
                    for (const auto &item: t.candidates) {
                        if (std::find(no_dups.begin(), no_dups.end(), item.filename) == no_dups.end()) {
                            no_dups.push_back(item.filename); // Add value if not found
                        }
                    }
                }
                for (int i = 0; i < targets.size(); i++) {
                    auto t = targets[i];
                    for (const auto &item: t.candidates) {
                        std::string fullname = ":" + item.theme + ":" + item.filename;
                        if (std::find(no_dups.begin(), no_dups.end(), fullname) == no_dups.end()) {
                            no_dups.push_back(fullname); // Add value if not found
                        }
                    }
                }
                
                for (int i = 0; i < no_dups.size(); ++i) {
                    Container *icon_option = scroll->content->child(FILL_SPACE, 27 * config->dpi);
                    auto nl = new NumberedLabel();
                    nl->number = i;
                    nl->label = new Label(no_dups[i]);
                    icon_option->user_data = nl;
                    icon_option->when_paint = paint_icon_option;
                    icon_option->when_clicked = [](AppClient *client, cairo_t *, Container *container) {
                        auto label = (NumberedLabel *) container->user_data;
                        auto c = client_by_name(app, "pinned_icon_editor");
                        auto *field = container_by_name("icon_name_field", c->root);
                        ((TextAreaData *) field->user_data)->state->text = label->label->text;
                        
                        client_close_threaded(app, client);
                    };
                }
                
                max_options = no_dups.size();
                if (active_option > max_options - 1) {
                    active_option = max_options - 1;
                }
                if (active_option < 0)
                    active_option = 0;
            }
        }
        
        client_layout(app, popup);
        request_refresh(app, popup);
    }
}

static void
scroll_for_active_option() {
    if (auto popup = client_by_name(app, "icon_list_popup")) {
        if (auto s = container_by_name("icon_list_scroll", popup->root)) {
            ScrollContainer *scroll_pane = (ScrollContainer *) s;
            
            for (auto item: scroll_pane->content->children) {
                auto nl = (NumberedLabel *) item->user_data;
                if (nl->number == active_option) {
                    int offset = -scroll_pane->scroll_v_real + item->real_bounds.y;
                    offset -= 3 * config->dpi;
                    scroll_pane->scroll_v_real = -offset;
                    scroll_pane->scroll_v_visual = scroll_pane->scroll_v_real;
                    ::layout(popup, popup->cr, scroll_pane, scroll_pane->real_bounds);
                    break;
                }
            }
        }
    }
}

static void
tab_key_event(AppClient *client,
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
    if (container->name == "icon_name_field") {
        if (keysym == XKB_KEY_Escape) {
            if (auto c = client_by_name(app, "icon_list_popup")) {
                active_option_preferred = false;
                client_close_threaded(app, c);
            }
            return;
        } else if (keysym == XKB_KEY_Up) {
            active_option--;
            if (active_option < 0)
                active_option = 0;
            scroll_for_active_option();
            update_icon(client);
            active_option_preferred = true;
        } else if (keysym == XKB_KEY_Down) {
            if (active_option == 0 && !active_option_preferred)
                active_option--;
            active_option++;
            if (active_option > max_options - 1)
                active_option = max_options - 1;
            scroll_for_active_option();
            update_icon(client);
            active_option_preferred = true;
        } else if (keysym == XKB_KEY_Return || keysym == XKB_KEY_Tab) {
            auto c = client_by_name(app, "pinned_icon_editor");
            auto *field = container_by_name("icon_name_field", c->root);
            if (auto popup = client_by_name(app, "icon_list_popup")) {
                if (auto s = container_by_name("icon_list_scroll", popup->root)) {
                    ScrollContainer *scroll_pane = (ScrollContainer *) s;
                    for (auto item: scroll_pane->content->children) {
                        auto nl = (NumberedLabel *) item->user_data;
                        if (nl->number == active_option) {
                            ((TextAreaData *) field->user_data)->state->text = nl->label->text;
                            ((TextAreaData *) field->user_data)->state->cursor = nl->label->text.size();
                            break;
                        }
                    }
                }
                client_close_threaded(app, popup);
            }
            update_icon(client);
        }
    }
    
    if (container->parent->active) {
        if (keysym == XKB_KEY_Tab) {
            if (container->name == "icon_name_field") {
                app_timeout_create(app, client, 0, switch_active, new std::string("launch_command_field"),
                                   "icon_name_key_event");
            } else if (container->name == "launch_command_field") {
                app_timeout_create(app, client, 0, switch_active, new std::string("wm_class_field"),
                                   "icon_name_key_event");
            } else {
                app_timeout_create(app, client, 0, switch_active, new std::string("icon_name_field"),
                                   "icon_name_key_event");
            }
        } else {
            textarea_handle_keypress(client, container, is_string, keysym, string, mods, XKB_KEY_DOWN);
            if (container->name == "icon_name_field") {
                update_icon(client);
                show_icon_options();
            }
        }
    }
}

static void fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *root = client->root;
    root->wanted_pad = Bounds(10 * config->dpi, 10 * config->dpi, 10 * config->dpi, 10 * config->dpi);
    root->spacing = 10 * config->dpi;
    root->type = vbox;
    root->when_paint = paint_background;
    
    TextAreaSettings textarea_settings = TextAreaSettings(config->dpi);
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
        } else {
        }
        
        icon->user_data = icon_data;
        icon->when_paint = paint_icon;
        
        centered->child(FILL_SPACE, FILL_SPACE);
    }
    
    Container *icon_name_hbox = root->child(layout_type::hbox, FILL_SPACE, 64 * config->dpi);
    icon_name_hbox->spacing = 10 * config->dpi;
    {
        Container *icon_name_label_and_field_vbox = icon_name_hbox->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
        {
            Container *icon_name_label_and_alternatives_hbox = icon_name_label_and_field_vbox->child(::hbox, FILL_SPACE,
                                                                                                     13 * config->dpi);
            PangoLayout *layout = get_cached_pango_font(client->cr, config->font, 11 * config->dpi, PANGO_WEIGHT_BOLD);
            
            int width;
            int height;
            pango_layout_set_text(layout, "Icon ", -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            Container *icon_name_label = icon_name_label_and_alternatives_hbox->child(width, FILL_SPACE);
            icon_name_label->name = "icon_name_label";
            icon_name_label->user_data = new Label("Icon");
            icon_name_label->when_paint = paint_label;
            
            icon_name_label_and_field_vbox->child(FILL_SPACE, FILL_SPACE);
            
            Container *icon_name_field = make_textarea(app, client, icon_name_label_and_field_vbox, textarea_settings);
            icon_name_field->name = "icon_name_field";
            icon_name_field->parent->when_paint = paint_textarea_border;
            icon_name_field->when_key_event = tab_key_event;
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
        launch_command_field->name = "launch_command_field";
        launch_command_field->when_key_event = tab_key_event;
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
        wm_class_field->name = "wm_class_field";
        wm_class_field->when_key_event = tab_key_event;
        wm_class_field->parent->when_paint = paint_textarea_border;
        auto wm_class_field_data = (TextAreaData *) wm_class_field->user_data;
        wm_class_field_data->state->text = pinned_icon_data->class_name;
        wm_field_data = wm_class_field_data;
//        wm_class_field->when_paint = paint_ex;i
        wm_class_field->wanted_bounds.h = 32 * config->dpi;
    }
    
    root->child(FILL_SPACE, FILL_SPACE);
    
    
    Container *name_label_and_field = root->child(layout_type::vbox, FILL_SPACE, 64 * config->dpi);
    {
        Container *name_label = name_label_and_field->child(FILL_SPACE, 13 * config->dpi);
        name_label->user_data = new Label("Desktop file name");
        name_label->when_paint = paint_label;
        
        name_label_and_field->child(FILL_SPACE, FILL_SPACE);
        
        Container *name_field = make_textarea(app, client, name_label_and_field, textarea_settings);
        name_field->name = "name_field";
        name_field->when_key_event = tab_key_event;
        name_field->parent->when_paint = paint_textarea_border;
        auto nf_data = (TextAreaData *) name_field->user_data;
        nf_data->state->text = "";
        nf_data->state->prompt = wm_field_data->state->text;
        nf_data->color_prompt = ArgbColor(0, 0, 0, .3);
        if (has_desktop_file()) {
            const char *home = getenv("HOME");
            std::string desktop_path(home);
            desktop_path += "/.local/share/applications/" + wm_field_data->state->text + ".desktop";
            
            std::ifstream in(desktop_path);
            std::string line;
            const char *target = "Name=";
            while (std::getline(in, line)) {
                if (line.find(target) != std::string::npos) {
                    nf_data->state->text = line.substr(strlen(target));
                }
            }
        }
        name_field_data = nf_data;
        name_field->wanted_bounds.h = 32 * config->dpi;
    }
    
    
    Container *button_hbox = root->child(::hbox, FILL_SPACE, 30 * config->dpi);
    button_hbox->spacing = 10 * config->dpi;
    {
        std::string desktop_file_text = "Make Desktop File";
        if (has_desktop_file()) {
            desktop_file_text = "Remove Desktop File";
        }
        Container *make_desktop = make_button(client, button_hbox, desktop_file_text);
        make_desktop->when_clicked = clicked_create_destroy_desktop_file;
        
        // Spacer
        button_hbox->child(FILL_SPACE, 1);
        
        Container *save_button = make_button(client, button_hbox, "Save & Quit");
        save_button->when_clicked = clicked_save_and_quit;
        
        Container *restore_button = make_button(client, button_hbox, "Restore");
        restore_button->when_paint = paint_restore;
        restore_button->when_clicked = clicked_restore;
        
        Container *cancel_button = make_button(client, button_hbox, "Close");
        cancel_button->when_clicked = clicked_cancel;
    }
}

static bool has_desktop_file() {
    // Check if the desktop folder there is a 'class_name'.desktop file
    if (wm_field_data) {
        const char *home = getenv("HOME");
        std::string desktop_path(home);
        desktop_path += "/.local/share/applications/" + wm_field_data->state->text + ".desktop";
        
        struct stat cache_stat{};
        if (stat(desktop_path.c_str(), &cache_stat) == 0) { // exists
            return true;
        }
    }
    
    return false;
}

static void
closed_pinned_icon(AppClient *client) {
    if (creating_not_editing) {
        if (saved) {
            if (auto *taskbar = client_by_name(app, "taskbar")) {
                if (auto *icons = container_by_name("icons", taskbar->root)) {
                    icons->children.push_back(pinned_icon_container);
                }
                client_layout(app, taskbar);
                client_paint(app, taskbar);
            }
        } else {
            delete pinned_icon_container;
        }
    }
    if (auto p = client_by_name(app, "icon_list_popup")) {
        client_close_threaded(app, p);
    }
    icon_field_data = nullptr;
    launch_field_data = nullptr;
    wm_field_data = nullptr;
    name_field_data = nullptr;
}

void start_pinned_icon_editor(Container *icon_container, bool creating) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    saved = false;
    creating_not_editing = creating;
    pinned_icon_container = icon_container;
    pinned_icon_data = (LaunchableButton *) icon_container->user_data;
    if (!pinned_icon_container || !pinned_icon_data) {
        pinned_icon_container = nullptr;
        pinned_icon_data = nullptr;
        return;
    }
    
    Settings settings;
    settings.w = 750 * config->dpi;
    settings.h = 580 * config->dpi;
    settings.skip_taskbar = false;
    settings.keep_above = true;
    
    if (auto client = client_new(app, settings, "pinned_icon_editor")) {
        xcb_ewmh_set_wm_icon_name(&app->ewmh, client->window, strlen("settings"), "settings");
        fill_root(client);
        std::string title = "Pinned Icon Editor";
        xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
        client_show(app, client);
        client->when_closed = closed_pinned_icon;
        xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
    }
}