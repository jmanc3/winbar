
#include "app_menu.h"
#include "application.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "INIReader.h"
#include "application.h"
#include "components.h"
#include "config.h"
#include "icons.h"
#include "main.h"
#include "search_menu.h"
#include "taskbar.h"
#include "globals.h"

#include <cmath>
#include <pango/pangocairo.h>
#include <vector>
#include <optional>
#include <xcb/xcb_aux.h>
#include <hsluv.h>
#include <sys/stat.h>

std::vector<Launcher *> launchers;

class ItemData : public HoverableButton {
public:
    Launcher *launcher = nullptr;
};

class ButtonData : public IconButton {
public:
    std::string text;
};

// the scrollbar should only open if the mouse is in the scrollbar
static double scrollbar_openess = 0;
// the scrollbar should only be visible if the mouse is in the container
static double scrollbar_visible = 0;

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_apps_background));
    cairo_fill(cr);
}

static void
paint_left(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    double openess = (container->real_bounds.w - 48) / 256;

    if (container->real_bounds.w == 48) {
        set_argb(cr, correct_opaqueness(client, config->color_apps_background));
    } else {
        ArgbColor color = correct_opaqueness(client, config->color_apps_background);
        lighten(&color, 2 * openess);
        set_argb(cr, color);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    easingFunction ease = getEasingFunction(easing_functions::EaseInCubic);
    if (container->real_bounds.w != 48) {
        int steps = 14;
        for (int i = 0; i < steps; i++) {
            double scalar = ((double) (i)) / steps;
            scalar = (1 - scalar) * openess;
            scalar = ease(scalar);
            scalar /= 4;
            cairo_rectangle(cr,
                            (int) (container->real_bounds.x + container->real_bounds.w + i),
                            (int) (container->real_bounds.y),
                            1,
                            (int) (container->real_bounds.h));
            set_argb(cr, ArgbColor(0, 0, 0, scalar));
            cairo_fill(cr);
        }
    }
}

static void
paint_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container->real_bounds.x += 1;
    container->real_bounds.w -= 2;

    ButtonData *data = (ButtonData *) container->user_data;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_apps_pressed_item);
        } else {
            set_argb(cr, config->color_apps_hovered_item);
        }
        int border = 0;

        cairo_rectangle(cr,
                        container->real_bounds.x + border,
                        container->real_bounds.y + border,
                        container->real_bounds.w - border * 2,
                        container->real_bounds.h - border * 2);
        cairo_fill(cr);
    }

    container->real_bounds.x -= 1;
    container->real_bounds.w += 2;

    if (data && data->surface) {
        int width = cairo_image_surface_get_width(data->surface);
        int height = cairo_image_surface_get_height(data->surface);

        dye_surface(data->surface, config->color_apps_icons);

        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + container->real_bounds.h / 2 - height / 2),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        cairo_paint(cr);
    }

    auto taskbar_entity = client_by_name(app, "taskbar");
    auto taskbar_root = taskbar_entity->root;

    if (container->parent->wanted_bounds.w != taskbar_root->children[0]->real_bounds.w) {
        cairo_push_group(cr);

        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
        if (data->text == "START") {
            layout = get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_BOLD);
        }

        set_argb(cr, config->color_apps_text);
        pango_layout_set_text(layout, data->text.c_str(), data->text.length());

        int width, height;
        // valgrind thinks this leaks
        pango_layout_get_pixel_size(layout, &width, &height);

        int text_x = (int) (container->real_bounds.x + taskbar_root->children[0]->real_bounds.w);
        int text_y = (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
        cairo_move_to(cr, text_x, text_y);

        pango_cairo_show_layout(cr, layout);

        // TODO: for some reason valgrind is picking this up as a huge source of leaks
        cairo_pop_group_to_source(cr);

        cairo_rectangle(cr,
                        container->real_bounds.x,
                        container->real_bounds.y,
                        container->real_bounds.w,
                        container->real_bounds.h);
        cairo_clip(cr);
        cairo_paint(cr);
        cairo_reset_clip(cr);
    }
}

static void
clicked_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *right_content = container_by_name("right_content", client->root);
    if (right_content->children[0]->name == "app_list_container") {
        transition_same_container(client, cr, right_content,
                                  Transition::ANIM_DEFAULT_TO_SQUASHED | Transition::ANIM_FADE_OUT,
                                  Transition::ANIM_EXPANDED_TO_DEFAULT | Transition::ANIM_FADE_IN);
    } else {
        transition_same_container(client, cr, right_content,
                                  Transition::ANIM_DEFAULT_TO_EXPANDED | Transition::ANIM_FADE_OUT,
                                  Transition::ANIM_SQUASHED_TO_DEFAULT | Transition::ANIM_FADE_IN);
    }
}

static void
clicked_grid(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Set the correct scroll offset
    auto data = (ButtonData *) container->user_data;
    if (auto c = container_by_name(data->text, client->root)) {
        if (auto scroll_pane = container_by_name("scroll_pane", client->root)) {
            int offset = get_offset(c, scroll_pane) + scroll_pane->children_bounds.y;
            scroll_pane->scroll_v_real = -offset;
            scroll_pane->scroll_v_visual = scroll_pane->scroll_v_real;
            ::layout(client, cr, scroll_pane->parent, scroll_pane->real_bounds);

            clicked_title(client, cr, container);
        }
    }
}

static void
clicked_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    auto *data = (ItemData *) container->user_data;
    set_textarea_inactive();
    launch_command(data->launcher->exec);
    client_close_threaded(app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
paint_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!overlaps(container->real_bounds, container->parent->parent->real_bounds)) {
        return;
    }

    auto *data = (ItemData *) container->user_data;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_apps_pressed_item);
        } else {
            set_argb(cr, config->color_apps_hovered_item);
        }
        int border = 0;

        cairo_rectangle(cr,
                        container->real_bounds.x + border,
                        container->real_bounds.y + border,
                        container->real_bounds.w - border * 2,
                        container->real_bounds.h - border * 2);
        cairo_fill(cr);
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text = data->launcher->name;
    pango_layout_set_text(layout, text.c_str(), text.size());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_apps_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 44,
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);

    if (data->launcher->icon_24) {
        cairo_set_source_surface(cr,
                                 data->launcher->icon_24,
                                 (int) (container->real_bounds.x + 4 + 4),
                                 (int) (container->real_bounds.y + 2 + 4));
        cairo_paint(cr);
    } else {
        cairo_set_source_surface(cr,
                                 global->unknown_icon_24,
                                 (int) (container->real_bounds.x + 4 + 4),
                                 (int) (container->real_bounds.y + 2 + 4));
        cairo_paint(cr);
    }
}

static void
paint_item_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!overlaps(container->real_bounds, container->parent->parent->real_bounds)) {
        return;
    }

    auto *data = (ButtonData *) container->user_data;

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_apps_pressed_item);
        } else {
            set_argb(cr, config->color_apps_hovered_item);
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text(data->text);
    pango_layout_set_text(layout, text.c_str(), text.size());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, config->color_apps_text);
    cairo_move_to(cr,
                  container->real_bounds.x + 3,
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);

    if (data->surface) {
        cairo_set_source_surface(cr, data->surface, container->real_bounds.x, container->real_bounds.y);
        cairo_paint(cr);
    }
}

static void
paint_scroll_bg(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    set_rect(cr, container->real_bounds);
    ArgbColor color = config->color_apps_scrollbar_gutter;
    color.a = scrollbar_openess;
    set_argb(cr, color);
    cairo_fill(cr);
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (IconButton *) container->user_data;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_pressed_button;
            color.a = scrollbar_openess;
            set_argb(cr, color);
            cairo_fill(cr);
        } else {
            set_rect(cr, container->real_bounds);
            ArgbColor color = config->color_apps_scrollbar_hovered_button;
            color.a = scrollbar_openess;
            set_argb(cr, color);
            cairo_fill(cr);
        }
    } else {
        set_rect(cr, container->real_bounds);
        ArgbColor color = config->color_apps_scrollbar_default_button;
        color.a = scrollbar_openess;
        set_argb(cr, color);
        cairo_fill(cr);
    }

    if (data->surface) {
        // TODO: cache the dye so we only do it once
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                dye_surface(data->surface, config->color_apps_scrollbar_pressed_button_icon);
            } else {
                dye_surface(data->surface, config->color_apps_scrollbar_hovered_button_icon);
            }
        } else {
            dye_surface(data->surface, config->color_apps_scrollbar_default_button_icon);
        }

        cairo_set_source_surface(
                cr, data->surface, container->real_bounds.x, container->real_bounds.y);
        cairo_paint_with_alpha(cr, scrollbar_openess);
    }
}

static void
paint_right_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    paint_scroll_bg(client, cr, container);

    Container *scrollpane = container->parent->parent;

    auto right_bounds = right_thumb_bounds(scrollpane, container->real_bounds);

    right_bounds.x += right_bounds.w;
    right_bounds.w = std::max(right_bounds.w * scrollbar_openess, 2.0);
    right_bounds.x -= right_bounds.w;
    right_bounds.x -= 2 * (1 - scrollbar_openess);

    set_rect(cr, right_bounds);

    if (container->state.mouse_pressing) {
        ArgbColor color = config->color_apps_scrollbar_pressed_thumb;
        color.a = scrollbar_visible;
        set_argb(cr, color);
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        ArgbColor color = config->color_apps_scrollbar_hovered_thumb;
        color.a = scrollbar_visible;
        set_argb(cr, color);
    } else if (right_bounds.w == 2.0) {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        lighten(&color, 10);
        color.a = scrollbar_visible;
        set_argb(cr, color);
    } else {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        color.a = scrollbar_visible;
        set_argb(cr, color);
    }

    cairo_fill(cr);
}

static void
when_scrollbar_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 1);
}

static void
when_scrollbar_container_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    client_create_animation(client->app, client, &scrollbar_visible, 100, 0, 1);
}

static void
when_scrollbar_mouse_leaves(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 0);
    client_create_animation(client->app, client, &scrollbar_visible, 100, 0, 0);
}

static Timeout *scrollbar_leave_fd = nullptr;

static void
scrollbar_leaves_timeout(App *app, AppClient *client, Timeout *, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (valid_client(app, client)) {
        auto *container = (Container *) data;
        if (scrollbar_openess != 0 && scrollbar_openess == 1 &&
            !bounds_contains(
                    container->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
            client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 0);
        }
    } else {
        scrollbar_openess = 0;
    }
    scrollbar_leave_fd = nullptr;
}

static void
when_scrollbar_mouse_leaves_slow(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (scrollbar_leave_fd == nullptr) {
        scrollbar_leave_fd = app_timeout_create(app, client, 3000, scrollbar_leaves_timeout, container);
    } else {
        app_timeout_replace(app, client, scrollbar_leave_fd, 3000, scrollbar_leaves_timeout, container);
    }
}

static Timeout *left_open_fd = nullptr;

static void
left_open_timeout(App *app, AppClient *client, Timeout *, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *container = (Container *) data;
    if (app && app->running && valid_client(app, client) &&
        (container->state.mouse_hovering || container->state.mouse_pressing)) {
        client_create_animation(
                app, client, &container->wanted_bounds.w, 100, nullptr, 256, true);
    }
    left_open_fd = nullptr;
}

static void
left_open(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (left_open_fd == nullptr) {
        left_open_fd = app_timeout_create(client->app, client, 160, left_open_timeout, container);
    } else {
        app_timeout_replace(client->app, client, left_open_fd, 160, left_open_timeout, container);
    }
}

static void
left_close(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    client_create_animation(app, client, &container->wanted_bounds.w, 70, nullptr, 48, true);
}

static bool
right_content_handles_pierced(Container *container, int x, int y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *client = client_by_name(app, "app_menu")) {
        if (auto *container = container_by_name("left_buttons", client->root)) {
            if (bounds_contains(container->real_bounds, x, y)) {
                return false;
            }
        }
    }
    return bounds_contains(container->real_bounds, x, y);
}

void scrolled_content_area(AppClient *client,
                           cairo_t *cr,
                           Container *container,
                           int scroll_x,
                           int scroll_y) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (auto *client = client_by_name(app, "app_menu")) {
        if (auto *container = container_by_name("left_buttons", client->root)) {
            if (bounds_contains(
                    container->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
                return;
            }
        }
    }

    scrollpane_scrolled(client, cr, container, scroll_x, scroll_y);
}

static void
clicked_start_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Toggle left menu openess
    if (auto *client = client_by_name(app, "app_menu")) {
        if (auto *container = container_by_name("left_buttons", client->root)) {
            if (container->real_bounds.w == 48) {
                client_create_animation(
                        app, client, &container->wanted_bounds.w, 120, nullptr, 256, true);
            } else if (container->real_bounds.w == 256) {
                client_create_animation(
                        app, client, &container->wanted_bounds.w, 120, nullptr, 48, true);
            }
        }
    }
}

bool DirectoryExists(const char *pzPath) {
    if (pzPath == NULL)
        return false;

    DIR *pDir;
    bool bExists = false;

    pDir = opendir(pzPath);

    if (pDir != NULL) {
        bExists = true;
        (void) closedir(pDir);
    }

    return bExists;
}

static void
clicked_open_folder_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::string home = getenv("HOME");
    auto *data = (ButtonData *) container->user_data;
    set_textarea_inactive();
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;

    std::vector<std::string> commands = {"xdg-open", "thunar", "dolphin", "Thunar", "dolphin"};
    for (const auto &c: commands) {
        if (script_exists(c)) {
            std::string path = home + "/" + data->text;
            std::string small(data->text);
            if (small.size() > 0)
                small[0] = std::tolower(small[0]);
            std::string min_path = home;
            min_path.append("/").append(small);
            if (DirectoryExists(path.c_str())) {
                launch_command(std::string(c + " " + path));
            } else if (DirectoryExists(min_path.c_str())) {
                launch_command(std::string(c + " " + min_path));
            }
            break;
        }
    }
}

static void
clicked_open_file_manager(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::string home = getenv("HOME");
    std::vector<std::string> commands = {"xdg-open", "thunar", "dolphin", "Thunar", "Dolphin"};
    for (const auto &c: commands) {
        if (script_exists(c)) {
            launch_command(std::string(c + " " + home));
            break;
        }
    }
    set_textarea_inactive();
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
clicked_open_settings(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::lock_guard m(app->running_mutex);
    client->app->running = false;
    restart = true;
}

static void
when_key_event(AppClient *client,
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
    auto *app_menu_client = client_by_name(app, "app_menu");
    auto *taskbar_client = client_by_name(app, "taskbar");
    if (!app_menu_client || !taskbar_client) {
        return;
    }

    if (is_string) {
        client_close(app, app_menu_client);
        app->grab_window = -1;
        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->grab_window, XCB_MOD_MASK_ANY);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);

        set_textarea_active();
        start_search_menu();
        if (auto *c = client_by_name(app, "search_menu")) {
            send_key_actual(app, c, c->root, is_string, keysym, string, mods, direction);
        }
        set_textarea_active();
    } else if (keysym == XKB_KEY_Escape) {
        client_close(app, app_menu_client);
        xcb_flush(app->connection);
        app->grab_window = -1;
        xcb_aux_sync(app->connection);
        set_textarea_inactive();
    }
}

static void
clicked_open_power_menu(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::lock_guard m(app->running_mutex);
    client->app->running = false;
}


static void
paint_grid_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_apps_pressed_item);
        } else {
            set_argb(cr, config->color_apps_hovered_item);
        }
        int pad = 2;
        paint_margins_rect(client, cr, container->real_bounds, pad, 0);
    }

    auto *data = (ButtonData *) container->user_data;

    if (data->surface) {
        if (container->interactable) {
            dye_surface(data->surface, config->color_apps_text);
        } else {
            dye_surface(data->surface, config->color_apps_text_inactive);
        }
        cairo_set_source_surface(cr, data->surface,
                                 (int) (container->real_bounds.x + container->real_bounds.w / 2 - 10),
                                 (int) (container->real_bounds.y + container->real_bounds.h / 2 - 10));
        cairo_paint(cr);
    } else {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 14, PangoWeight::PANGO_WEIGHT_NORMAL);
        std::string text(data->text);
        pango_layout_set_text(layout, text.c_str(), text.size());

        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);

        if (container->interactable) {
            set_argb(cr, config->color_apps_text);
        } else {
            set_argb(cr, config->color_apps_text_inactive);
        }
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 -
                             ((logical.width / PANGO_SCALE) / 2)),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 -
                             ((logical.height / PANGO_SCALE) / 2)));
        pango_cairo_show_layout(cr, layout);
    }
}

static void
fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    Container *root = client->root;
    root->when_paint = paint_root;
    root->when_key_event = when_key_event;

    Container *stack = root->child(::stack, FILL_SPACE, FILL_SPACE);

    int width = 48;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        width = taskbar->root->children[0]->real_bounds.w;
    }
    Container *left_buttons = stack->child(::vbox, width, FILL_SPACE);
    left_buttons->wanted_pad.y = 5;
    left_buttons->spacing = 1;
    left_buttons->name = "left_buttons";
    left_buttons->z_index++;
    left_buttons->receive_events_even_if_obstructed_by_one = true;
    left_buttons->when_paint = paint_left;
    left_buttons->when_mouse_enters_container = left_open;
    left_buttons->when_mouse_leaves_container = left_close;

    std::vector<std::string> list;
    list.push_back("START");
    list.push_back("Documents");
    list.push_back("Downloads");
    list.push_back("Music");
    list.push_back("Pictures");
    list.push_back("Videos");
    list.push_back("Network");
    list.push_back("Personal Folder");
    list.push_back("File Explorer");
    list.push_back("Settings");
    list.push_back("Power");
    for (int i = 0; i < list.size(); i++) {
        std::string item = list[i];

        auto button = new Container();
        button->parent = left_buttons;
        left_buttons->children.push_back(button);
        button->wanted_bounds.w = FILL_SPACE;
        button->wanted_bounds.h = width;
        button->when_paint = paint_button;

        if (i == 0) {
            button->when_clicked = clicked_start_button;
        } else if (i >= 1 && i <= 7) {
            button->when_clicked = clicked_open_folder_button;
        } else if (i == 8) {
            button->when_clicked = clicked_open_file_manager;
        } else if (i == 9) {
            button->when_clicked = clicked_open_settings;
        } else if (i == 10) {
            button->when_clicked = clicked_open_power_menu;
        }

        // button->when_mouse_enters_container = mouse_enters_fading_button;
        // button->when_mouse_leaves_container = leave_fade;
        auto *data = new ButtonData;
        data->text = item;
        data->surface = accelerated_surface(app, client, 16, 16);
        paint_surface_with_image(
                data->surface, as_resource_path("starticons/" + item + ".png"), 16, nullptr);
        button->user_data = data;
    }
    auto start_filler = new Container();
    start_filler->wanted_bounds.w = FILL_SPACE;
    start_filler->wanted_bounds.h = FILL_SPACE;
    start_filler->parent = left_buttons;
    left_buttons->children.insert(left_buttons->children.begin() + 1, start_filler);
    // left_buttons->children[0]->when_clicked = left_buttons_toggle;

    Container *right_hbox = stack->child(::hbox, FILL_SPACE, FILL_SPACE);
    right_hbox->wanted_pad.w = 1;
    right_hbox->child(width, FILL_SPACE);

    auto *right_content = right_hbox->child(::transition, FILL_SPACE, FILL_SPACE);
    right_content->receive_events_even_if_obstructed = true;
    right_content->name = "right_content";
    right_content->when_mouse_leaves_container = when_scrollbar_mouse_leaves;
    right_content->when_mouse_enters_container = when_scrollbar_container_mouse_enters;
    right_content->handles_pierced = right_content_handles_pierced;

    auto *app_list_container = right_content->child(FILL_SPACE, FILL_SPACE);
    app_list_container->name = "app_list_container";

    auto *grid_container = right_content->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    grid_container->name = "grid_container";

    auto *grid = grid_container->child(layout_type::hbox, FILL_SPACE, FILL_SPACE);
    grid->child(FILL_SPACE, FILL_SPACE);
    grid->alignment = ALIGN_CENTER;

    int pad = 4;
    auto *g = grid->child(layout_type::vbox, 48 * 4 + (pad * 3), 48 * 8 + (pad * 7));
    g->spacing = pad;

    ScrollPaneSettings settings;
    settings.right_width = 12;
    settings.right_arrow_height = 12;
    settings.right_inline_track = true;
    Container *content_area = make_scrollpane(app_list_container, settings);
    content_area->when_scrolled = scrolled_content_area;
    content_area->name = "scroll_pane";
    Container *right_thumb_container = content_area->parent->children[0]->children[1];
    right_thumb_container->parent->receive_events_even_if_obstructed_by_one = true;
    right_thumb_container->parent->when_mouse_enters_container = when_scrollbar_mouse_enters;
    right_thumb_container->parent->when_mouse_leaves_container = when_scrollbar_mouse_leaves_slow;

    right_thumb_container->when_paint = paint_right_thumb;
    Container *top_arrow = content_area->parent->children[0]->children[0];
    top_arrow->when_paint = paint_arrow;
    auto *top_data = new IconButton;
    top_data->surface = accelerated_surface(app, client, 12, 12);
    paint_surface_with_image(top_data->surface, as_resource_path("arrow-up-12.png"), 12, nullptr);

    top_arrow->user_data = top_data;
    Container *bottom_arrow = content_area->parent->children[0]->children[2];
    bottom_arrow->when_paint = paint_arrow;
    auto *bottom_data = new IconButton;
    bottom_data->surface = accelerated_surface(app, client, 12, 12);
    paint_surface_with_image(bottom_data->surface, as_resource_path("arrow-down-12.png"), 12, nullptr);

    bottom_arrow->user_data = bottom_data;

    content_area->wanted_pad = Bounds(13, 8, settings.right_width + 1, 54);

    Container *content = content_area->child(FILL_SPACE, 0);
    content->spacing = 2;

    char previous_char = '\0';
    int previous_priority = 0;
    for (int i = 0; i < launchers.size(); i++) {
        Launcher *l = launchers[i];
        // Insert title
        if (l->app_menu_priority == 4) {
            char new_char = std::tolower(l->name.at(0));
            if (previous_char != new_char) {
                previous_char = new_char;

                auto *data = new ButtonData();
                data->text = std::toupper(new_char);
                auto *title = new Container();
                title->parent = content;
                title->name = data->text;
                content->children.push_back(title);

                title->wanted_bounds.w = FILL_SPACE;
                title->wanted_bounds.h = 34;
                title->when_paint = paint_item_title;
                title->when_clicked = clicked_title;

                title->user_data = data;
            }
        } else if (previous_priority != l->app_menu_priority) {
            previous_priority = l->app_menu_priority;

            auto *title = new Container();
            title->parent = content;
            content->children.push_back(title);

            title->wanted_bounds.w = FILL_SPACE;
            title->wanted_bounds.h = 34;
            title->when_paint = paint_item_title;
            title->when_clicked = clicked_title;

            auto *data = new ButtonData();
            if (l->app_menu_priority == 1) {
                title->name = "Recently added";
                data->text = "Recently added";
            } else if (l->app_menu_priority == 2) {
                title->name = "&";
                data->text = "&";
            } else if (l->app_menu_priority == 3) {
                title->name = "#";
                data->text = "#";
            }
            title->user_data = data;
        }

        auto *child = content->child(FILL_SPACE, 36);
        auto *data = new ItemData;
        data->launcher = l;
        child->user_data = data;
        child->when_paint = paint_item;
        child->when_clicked = clicked_item;
    }

    int count = 0;
    for (int y = 0; y < 8; y++) {
        auto hbox = g->child(layout_type::hbox, (48 * 8) + (pad * 7), 48);
        hbox->spacing = pad;
        for (int x = 0; x < 4; x++) {
            count++;
            if (count > 8 * 4 - 3) {
                break;
            }
            auto c = hbox->child(48, 48);
            c->when_paint = paint_grid_item;
            c->when_clicked = clicked_grid;
            auto data = new ButtonData;
            c->user_data = data;
            if (count == 1) { // Recent
                if (!container_by_name("Recently added", root)) {
                    c->interactable = false;
                }
                data->surface = accelerated_surface(app, client, 20, 20);
                paint_png_to_surface(data->surface, as_resource_path("recent.png"), 20);
                data->text = "Recently added";
            } else if (count == 2) { // &
                if (!container_by_name("&", root)) {
                    c->interactable = false;
                }
                data->text = "&";
            } else if (count == 3) { // Numbers
                if (!container_by_name("#", root)) {
                    c->interactable = false;
                }
                data->text = "#";
            } else { // ASCII
                data->text = (char) (61 + count);
                if (!container_by_name(data->text, root)) {
                    c->interactable = false;
                }
            }

        }
    }
    grid->child(FILL_SPACE, FILL_SPACE);

    content->wanted_bounds.h = true_height(content_area) + true_height(content);
}

static void
app_menu_closed(AppClient *client) {
//    set_textarea_inactive();
}

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("super", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (IconButton *) co->user_data;
                            data->invalid_button_down = true;
                            data->timestamp = get_current_time_in_ms();
                        }
                    }
                }
            }
            break;
        }
    }
}

static bool
app_menu_event_handler(App *app, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
        }
    }

    return false;
}

static void
paint_desktop_files() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    std::lock_guard m(app->running_mutex); // No one is allowed to stop Winbar until this function finishes

    std::vector<IconTarget> targets;
    for (auto *launcher: launchers) {
        launcher->icon = c3ic_fix_desktop_file_icon(launcher->name, launcher->wmclass, launcher->icon, launcher->icon);
        if (!launcher->icon.empty()) {
            targets.emplace_back(IconTarget(launcher->icon, launcher));
        }
    }

    search_icons(targets);
    pick_best(targets, 32);

    for (const auto &t: targets) {
        if (t.user_data) {
            auto launcher = (Launcher *) t.user_data;
            launcher->icon_16 = accelerated_surface(app, client_by_name(app, "taskbar"), 16, 16);
            launcher->icon_24 = accelerated_surface(app, client_by_name(app, "taskbar"), 24, 24);
            launcher->icon_32 = accelerated_surface(app, client_by_name(app, "taskbar"), 32, 32);
            launcher->icon_64 = accelerated_surface(app, client_by_name(app, "taskbar"), 64, 64);

            std::string path16;
            std::string path24;
            std::string path32;
            std::string path64;

            if (!launcher->icon.empty()) {
                if (launcher->icon[0] == '/') {
                    path16 = launcher->icon;
                    path24 = launcher->icon;
                    path32 = launcher->icon;
                    path64 = launcher->icon;
                } else {
                    for (const auto &icon: t.indexes_of_results) {
                        if (!path16.empty() && !path24.empty() && !path32.empty() && !path64.empty())
                            break;
                        if ((icon.size == 16) && path16.empty()) {
                            path16 = icon.pre_path + "/" + icon.name;
                        } else if (icon.size == 24 && path24.empty()) {
                            path24 = icon.pre_path + "/" + icon.name;
                        } else if (icon.size == 32 && path32.empty()) {
                            path32 = icon.pre_path + "/" + icon.name;
                        } else if (icon.size == 64 && path64.empty()) {
                            path64 = icon.pre_path + "/" + icon.name;
                        }
                    }
                    for (const auto &icon: t.indexes_of_results) {
                        if (icon.extension == 2) {
                            if (path16.empty())
                                path16 = icon.pre_path + "/" + icon.name;
                            if (path24.empty())
                                path24 = icon.pre_path + "/" + icon.name;
                            if (path32.empty())
                                path32 = icon.pre_path + "/" + icon.name;
                            if (path64.empty())
                                path64 = icon.pre_path + "/" + icon.name;
                            break;
                        }
                    }
                    for (const auto &icon: t.indexes_of_results) {
                        if (!path16.empty() && !path24.empty() && !path32.empty() && !path64.empty())
                            break;
                        if (path16.empty())
                            path16 = icon.pre_path + "/" + icon.name;
                        if (path24.empty())
                            path24 = icon.pre_path + "/" + icon.name;
                        if (path32.empty())
                            path32 = icon.pre_path + "/" + icon.name;
                        if (path64.empty())
                            path64 = icon.pre_path + "/" + icon.name;
                    }
                }
            }

            if (!path16.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_16, path16, 16, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_16, as_resource_path("unknown-16.svg"), 16, nullptr);
            }

            if (!path24.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_24, path24, 24, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_24, as_resource_path("unknown-24.svg"), 24, nullptr);
            }

            if (!path32.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_32, path32, 32, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_32, as_resource_path("unknown-32.svg"), 32, nullptr);
            }

            if (!path32.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_64, path64, 64, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_64, as_resource_path("unknown-64.svg"), 64, nullptr);
            }
        }
    }
}

static std::optional<int> ends_with(const char *str, const char *suffix) {
    if (!str || !suffix)
        return {};
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return {};
    bool b = strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
    if (!b) {
        return {};
    }
    return lenstr - lensuffix;
}

void eraseAllSubStr(std::string &mainStr, const std::string &toErase) {
    size_t pos = std::string::npos;
    while ((pos = mainStr.find(toErase)) != std::string::npos) {
        mainStr.erase(pos, toErase.length());
    }
}

void eraseSubStrings(std::string &mainStr, const std::vector<std::string> &strList) {
    std::for_each(strList.begin(), strList.end(), std::bind(eraseAllSubStr, std::ref(mainStr), std::placeholders::_1));
}

void load_desktop_files(std::string directory) {
    auto c = getenv("XDG_CURRENT_DESKTOP");
    std::string paths;
    if (c) paths = std::string(c);
    std::stringstream input(paths);
    std::string parsed;
    std::vector<std::string> current_desktop;
    if (getline(input, parsed, ';')) {
        current_desktop.push_back(parsed);
    }

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(directory.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (!ends_with(ent->d_name, ".desktop")) {
                continue;
            }
            std::string path = directory + ent->d_name;
            struct stat buffer{};
            if (stat(path.c_str(), &buffer) != 0) {
                continue;
            }

            // Parse desktop file
            INIReader desktop_application(path);
            if (desktop_application.ParseError() != 0) {
                continue;
            }

            std::string name = desktop_application.Get("Desktop Entry", "Name", "");
            std::string wmclass = desktop_application.Get("Desktop Entry", "StartupWMClass", "");
            std::string exec = desktop_application.Get("Desktop Entry", "Exec", "");
            std::string icon = desktop_application.Get("Desktop Entry", "Icon", "");
            std::string display = desktop_application.Get("Desktop Entry", "NoDisplay", "");
            std::string not_show_in = desktop_application.Get("Desktop Entry", "NotShowIn", "");
            std::string only_show_in = desktop_application.Get("Desktop Entry", "OnlyShowIn", "");

            if (exec.empty() || display == "True" ||
                display == "true") // If we find no exec entry then there's nothing to run
                continue;

            if (!current_desktop.empty()) {
                if (!only_show_in.empty()) {
                    std::stringstream only_input(only_show_in);
                    bool found = false;
                    if (getline(only_input, parsed, ';')) {
                        for (const auto &s: current_desktop) {
                            if (s == parsed)
                                found = true;
                        }
                    }
                    if (!found)
                        continue;
                } else if (!not_show_in.empty()) {
                    std::stringstream not_input(not_show_in);
                    bool found = false;
                    if (getline(not_input, parsed, ';')) {
                        for (const auto &s: current_desktop) {
                            if (s == parsed)
                                found = true;
                        }
                    }
                    if (found)
                        continue;
                }
            }

            // Remove all field codes
            // https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#exec-variables
            eraseSubStrings(exec, {"%f", "%F", "%u", "%U", "%d", "%D", "%n", "%N", "%i", "%c", "%k", "%v", "%m"});

            if (name.empty())// If no name was set, just give it the exec name
                name = exec;

            auto *launcher = new Launcher();
            launcher->name = name;
            launcher->lowercase_name = launcher->name;
            std::transform(launcher->lowercase_name.begin(),
                           launcher->lowercase_name.end(),
                           launcher->lowercase_name.begin(),
                           ::tolower);
            launcher->exec = exec;
            launcher->wmclass = wmclass;
            launcher->icon = icon;
            launcher->time_modified = buffer.st_mtim.tv_sec;

            launchers.push_back(launcher);
        }
    }
}

void load_all_desktop_files() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    for (auto *l: launchers) {
        delete l;
    }
    launchers.clear();
    launchers.shrink_to_fit();

    load_desktop_files("/usr/share/applications/");
    std::string local_desktop_files = getenv("HOME");
    local_desktop_files += "/.local/share/applications/";
    load_desktop_files(local_desktop_files);
    load_desktop_files("/var/lib/flatpak/exports/share/applications/");
    std::string local_flatpak_files = getenv("HOME");
    local_flatpak_files += "/.local/share/flatpak/exports/share/applications/";
    load_desktop_files(local_flatpak_files);

    time_t now;
    time(&now);

    auto recently_added_threshold = 86400 * 2; // two days  in seconds
    for (auto l: launchers) {
        double diff = difftime(now, l->time_modified);
        if (diff < recently_added_threshold) { // less than two days old
            l->app_menu_priority = 1;
        } else if (!l->name.empty()) {
            if (!isalnum(l->name[0])) { // is symbol
                l->app_menu_priority = 2;
            } else if (isdigit(l->name[0])) { // is number
                l->app_menu_priority = 3;
            } else { // is ascii
                l->app_menu_priority = 4;
            }
        }
    }

    // TODO: sort in order latest, &, #, A...Z
    std::sort(launchers.begin(), launchers.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs->app_menu_priority == rhs->app_menu_priority) {
            if (lhs->app_menu_priority == 1) { // time based
                return lhs->time_modified > rhs->time_modified;
            }

            // alphabetical order
            std::string first_name = lhs->name;
            std::string second_name = rhs->name;
            std::for_each(first_name.begin(), first_name.end(), [](char &c) { c = std::tolower(c); });
            std::for_each(second_name.begin(), second_name.end(), [](char &c) { c = std::tolower(c); });

            return first_name < second_name;
        } else {
            return lhs->app_menu_priority < rhs->app_menu_priority;
        }
    });
    std::thread(paint_desktop_files).detach();
}

void start_app_menu() {
    scrollbar_openess = 0;
    scrollbar_visible = 0;
    if (auto *c = client_by_name(app, "search_menu")) {
        client_close(app, c);
    }
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            delete text_data->state;
            text_data->state = new TextState;
        }
        request_refresh(client->app, client);
    }

    Settings settings;
    settings.force_position = true;
    settings.w = 320;
    settings.h = 641;
    settings.x = 0;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x;
        settings.y = taskbar->bounds->y - settings.h;
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

    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "app_menu";
        auto client = taskbar->create_popup(popup_settings, settings);

        client->grab_event_handler = grab_event_handler;
        client->when_closed = app_menu_closed;
        app_create_custom_event_handler(app, client->window, app_menu_event_handler);
        fill_root(client);
        client_show(app, client);
        set_textarea_active();
        xcb_set_input_focus(app->connection, XCB_NONE, client->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}
