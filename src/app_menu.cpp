
#include "app_menu.h"

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

#include <cmath>
#include <pango/pangocairo.h>
#include <vector>
#include <xcb/xcb_aux.h>

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
    set_argb(cr, config->sound_bg);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_left(AppClient *client, cairo_t *cr, Container *container) {
    ArgbColor color = config->sound_bg;
    color.r = color.a;
    color.g = color.a;
    color.b = color.a;
    color.a = 1;

    double openess = (container->real_bounds.w - 48) / 256;

    if (container->real_bounds.w == 48)
        set_argb(cr, color);
    else {
        color.r = color.g = color.b += (.1 * openess);
        color.a = 1 - (.03 * openess);
        set_argb(cr, color);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
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
            set_argb(cr, config->button_pressed);
        } else {
            set_argb(cr, config->button_hovered);
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

        dye_surface(data->surface, config->icons_colors);

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
        cairo_save(cr);
        cairo_push_group(cr);

        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
        if (data->text == "START") {
            layout = get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_BOLD);
        }

        set_argb(cr, config->calendar_font_default);
        pango_layout_set_text(layout, data->text.c_str(), data->text.length());

        int width, height;
        // valgrind thinks this leaks
        pango_layout_get_pixel_size(layout, &width, &height);

        int text_x = (int) (container->real_bounds.x + taskbar_root->children[0]->real_bounds.w);
        int text_y = (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
        cairo_move_to(cr, text_x, text_y);

        pango_cairo_show_layout(cr, layout);

        cairo_pop_group_to_source(cr);

        cairo_rectangle(cr,
                        container->real_bounds.x,
                        container->real_bounds.y,
                        container->real_bounds.w,
                        container->real_bounds.h);
        cairo_clip(cr);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

static void
paint_right(AppClient *client, cairo_t *cr, Container *container) {
    return;
    set_argb(cr, ArgbColor(1, 0, 0, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
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
            set_argb(cr, config->button_pressed);
        } else {
            set_argb(cr, config->button_hovered);
        }
        int border = 0;

        cairo_rectangle(cr,
                        container->real_bounds.x + border,
                        container->real_bounds.y + border,
                        container->real_bounds.w - border * 2,
                        container->real_bounds.h - border * 2);
        cairo_fill(cr);
    }

    set_argb(cr, config->main_accent);
    cairo_rectangle(cr, container->real_bounds.x + 4, container->real_bounds.y + 2, 32, 32);
    cairo_fill(cr);

    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe UI", 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text(data->launcher->name);
    pango_layout_set_text(layout, text.c_str(), text.size());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, ArgbColor(1, 1, 1, 1));
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
            set_argb(cr, config->button_pressed);
        } else {
            set_argb(cr, config->button_hovered);
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }

    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe UI", 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text(data->text);
    pango_layout_set_text(layout, text.c_str(), text.size());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    set_argb(cr, ArgbColor(1, 1, 1, 1));
    cairo_move_to(cr,
                  container->real_bounds.x + 3,
                  container->real_bounds.y + container->real_bounds.h / 2 -
                  ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_scroll_bg(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    set_rect(cr, container->real_bounds);
    set_argb(cr, ArgbColor(.208, .208, .208, scrollbar_openess));
    cairo_fill(cr);
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            set_argb(cr, ArgbColor(.682, .682, .682, scrollbar_openess));
            cairo_fill(cr);
        } else {
            set_rect(cr, container->real_bounds);
            set_argb(cr, ArgbColor(.286, .286, .286, scrollbar_openess));
            cairo_fill(cr);
        }
    } else {
        paint_scroll_bg(client, cr, container);
    }

    if (data->surface) {
        // TODO: cache the dye so we only do it once
        if (container->state.mouse_pressing) {
            dye_surface(data->surface, ArgbColor(.333, .333, .333, 1));
        } else {
            dye_surface(data->surface, ArgbColor(.82, .82, .82, 1));
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
        set_argb(cr, ArgbColor(.682, .682, .682, scrollbar_visible));
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        set_argb(cr, ArgbColor(.525, .525, .525, scrollbar_visible));
    } else if (right_bounds.w == 2.0) {
        set_argb(cr, ArgbColor(.482, .482, .482, scrollbar_visible));
    } else {
        set_argb(cr, ArgbColor(.365, .365, .365, scrollbar_visible));
    }

    cairo_fill(cr);
}

static void
paint_bottom_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    Container *scrollpane = container->parent->parent;

    set_rect(cr, bottom_thumb_bounds(scrollpane, container->real_bounds));
    set_argb(cr, ArgbColor(0, 0, 1, 1));
    cairo_fill(cr);
}

static void
when_scrollbar_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
    client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 1);
}

static void
when_scrollbar_container_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
    client_create_animation(client->app, client, &scrollbar_visible, 100, 0, 1);
}

static void
when_scrollbar_mouse_leaves(AppClient *client, cairo_t *cr, Container *container) {
    client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 0);
    client_create_animation(client->app, client, &scrollbar_visible, 100, 0, 0);
}

static void
when_scrollbar_mouse_leaves_slow(AppClient *client, cairo_t *cr, Container *container) {
    App *app = client->app;
    std::thread t([app, client, container]() -> void {
        usleep(1000 * 3000);
        std::lock_guard m(app->clients_mutex);
        if (valid_client(app, client)) {
            if (scrollbar_openess != 0 && scrollbar_openess == 1 &&
                !bounds_contains(
                        container->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
                client_create_animation(client->app, client, &scrollbar_openess, 100, 0, 0);
            }
        } else {
            scrollbar_openess = 0;
        }
    });
    t.detach();
}

static void
left_open(AppClient *client, cairo_t *cr, Container *container) {
    std::thread t([client, container]() -> void {
        usleep(1000 * 180);
        if (app && app->running && valid_client(app, client) &&
            (container->state.mouse_hovering || container->state.mouse_pressing)) {
            client_create_animation(
                    app, client, &container->wanted_bounds.w, 120, nullptr, 256, true);
        }
    });
    t.detach();
}

static void
left_close(AppClient *client, cairo_t *cr, Container *container) {
    client_create_animation(app, client, &container->wanted_bounds.w, 120, nullptr, 48, true);
}

static bool
right_content_handles_pierced(Container *container, int x, int y) {
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
    std::string home = getenv("HOME");
    auto *data = (ButtonData *) container->user_data;
    set_textarea_inactive();
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;

    if (!config->file_manager.empty()) {
        std::string path = home + "/" + data->text;
        std::string small(data->text);
        if (small.size() > 0) {
            small[0] = std::tolower(small[0]);
        }
        std::string min_path = home + "/" + small;
        if (DirectoryExists(path.c_str())) {
            launch_command(config->file_manager + " " + path);
        } else if (DirectoryExists(min_path.c_str())) {
            launch_command(config->file_manager + " " + min_path);
        }
    }
}

static void
clicked_open_file_manager(AppClient *client, cairo_t *cr, Container *container) {
    if (!config->file_manager.empty()) {
        launch_command(config->file_manager);
    }
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
clicked_open_settings(AppClient *client, cairo_t *cr, Container *container) {
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
clicked_open_power_menu(AppClient *client, cairo_t *cr, Container *container) {
    client_close_threaded(client->app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
fill_root(AppClient *client) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    Container *root = client->root;
    root->when_paint = paint_root;

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
                data->surface, as_resource_path("starticons/" + item + ".png"), nullptr);
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
    auto *right_content = right_hbox->child(FILL_SPACE, FILL_SPACE);
    right_content->receive_events_even_if_obstructed = true;
    right_content->when_mouse_leaves_container = when_scrollbar_mouse_leaves;
    right_content->when_mouse_enters_container = when_scrollbar_container_mouse_enters;
    right_content->handles_pierced = right_content_handles_pierced;

    ScrollPaneSettings settings;
    settings.right_width = 12;
    settings.right_arrow_height = 12;
    settings.right_inline_track = true;
    Container *content_area = make_scrollpane(right_content, settings);
    content_area->when_scrolled = scrolled_content_area;
    Container *right_thumb_container = content_area->parent->children[0]->children[1];
    right_thumb_container->parent->receive_events_even_if_obstructed_by_one = true;
    right_thumb_container->parent->when_mouse_enters_container = when_scrollbar_mouse_enters;
    right_thumb_container->parent->when_mouse_leaves_container = when_scrollbar_mouse_leaves_slow;

    right_thumb_container->when_paint = paint_right_thumb;
    Container *top_arrow = content_area->parent->children[0]->children[0];
    top_arrow->when_paint = paint_arrow;
    auto *top_data = new IconButton;
    top_data->surface = accelerated_surface(app, client, 12, 12);
    paint_surface_with_image(top_data->surface, as_resource_path("arrow-up-12.png"), nullptr);

    top_arrow->user_data = top_data;
    Container *bottom_arrow = content_area->parent->children[0]->children[2];
    bottom_arrow->when_paint = paint_arrow;
    auto *bottom_data = new IconButton;
    bottom_data->surface = accelerated_surface(app, client, 12, 12);
    paint_surface_with_image(bottom_data->surface, as_resource_path("arrow-down-12.png"), nullptr);

    bottom_arrow->user_data = bottom_data;

    content_area->when_paint = paint_right;
    content_area->wanted_pad = Bounds(13, 8, settings.right_width + 1, 54);

    Container *content = content_area->child(FILL_SPACE, 0);
    content->spacing = 2;

    // TODO: add title for ranges
    char previous_char = '\0';
    for (int i = 0; i < launchers.size(); i++) {
        Launcher *launcher = launchers[i];
        char new_char = std::tolower(launcher->name.at(0));
        if (new_char != previous_char) {
            if (std::isdigit(new_char)) {
                if (!std::isdigit(previous_char)) {
                    // Only one title for names that start with digit
                    auto *title = new Container();
                    title->parent = content;
                    content->children.push_back(title);

                    title->wanted_bounds.w = FILL_SPACE;
                    title->wanted_bounds.h = 34;
                    title->when_paint = paint_item_title;
                    // title->when_mouse_enters_container = mouse_enters_fading_button;
                    // title->when_mouse_leaves_container = leave_fade;

                    auto *data = new ButtonData();
                    data->text += "#";
                    title->user_data = data;
                }
            } else {
                auto *title = new Container();
                title->parent = content;
                content->children.push_back(title);

                title->wanted_bounds.w = FILL_SPACE;
                title->wanted_bounds.h = 34;
                title->when_paint = paint_item_title;
                // title->when_mouse_enters_container = mouse_enters_fading_button;
                // title->when_mouse_leaves_container = leave_fade;

                auto *data = new ButtonData();
                data->text += std::toupper(new_char);
                title->user_data = data;
            }
            previous_char = new_char;
        }

        auto *child = content->child(FILL_SPACE, 36);
        auto *data = new ItemData;
        data->launcher = launcher;
        child->user_data = data;
        child->when_paint = paint_item;
        child->when_clicked = clicked_item;
    }

    content->wanted_bounds.h = true_height(content_area) + true_height(content);
}

static bool first_expose = true;

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
        case XCB_KEY_PRESS: {
            auto *e = (xcb_key_press_event_t *) (event);
            if (e->detail == 133)// super l key
                break;
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }

            xkb_keycode_t keycode = e->detail;
            const xkb_keysym_t *keysyms;
            int num_keysyms = xkb_state_key_get_syms(client->keyboard->state, keycode, &keysyms);

            bool was_escape = false;
            if (num_keysyms > 0) {
                if (keysyms[0] == XKB_KEY_Escape) {
                    was_escape = true;
                }
            }

            // No matter what key was pressed after this app_menu is open, it is closed
            client_close(app, client);
            xcb_flush(app->connection);
            app->grab_window = -1;
            xcb_aux_sync(app->connection);

            // If it was escape than we leave
            if (was_escape) {
                set_textarea_inactive();
            } else {
                start_search_menu();
                if (auto *client = client_by_name(app, "taskbar")) {
                    if (auto *container = container_by_name("main_text_area", client->root)) {
                        e->event = client->window;
                        auto *ev = (xcb_generic_event_t *) e;
                        textarea_handle_keypress(app, ev, container);
                        request_refresh(app, client);
                    }
                }
                on_key_press_search_bar(nullptr);
            }

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

    return true;
}

static void
paint_desktop_files() {
    for (auto *launcher : launchers) {

        launcher->icon_16 = accelerated_surface(app, client_by_name(app, "taskbar"), 16, 16);
        launcher->icon_24 = accelerated_surface(app, client_by_name(app, "taskbar"), 24, 24);
        launcher->icon_32 = accelerated_surface(app, client_by_name(app, "taskbar"), 32, 32);
        launcher->icon_64 = accelerated_surface(app, client_by_name(app, "taskbar"), 64, 64);
        std::string path16 = find_icon(launcher->icon, 16);
        std::string path24 = find_icon(launcher->icon, 24);
        std::string path32 = find_icon(launcher->icon, 32);
        std::string path64 = find_icon(launcher->icon, 64);
        if (!path16.empty() && !launcher->icon.empty()) {
            paint_surface_with_image(launcher->icon_16, path16, nullptr);
        } else {
            paint_surface_with_image(
                    launcher->icon_16, as_resource_path("unknown-16.svg"), nullptr);
        }

        if (!path24.empty() && !launcher->icon.empty()) {
            paint_surface_with_image(launcher->icon_24, path24, nullptr);
        } else {
            paint_surface_with_image(
                    launcher->icon_24, as_resource_path("unknown-24.svg"), nullptr);
        }

        if (!path32.empty() && !launcher->icon.empty()) {
            paint_surface_with_image(launcher->icon_32, path32, nullptr);
        } else {
            paint_surface_with_image(
                    launcher->icon_32, as_resource_path("unknown-32.svg"), nullptr);
        }

        if (!path32.empty() && !launcher->icon.empty()) {
            paint_surface_with_image(launcher->icon_64, path64, nullptr);
        } else {
            paint_surface_with_image(
                    launcher->icon_64, as_resource_path("unknown-64.svg"), nullptr);
        }
    }
}

void load_desktop_files() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    // If the "Icon=" contains "." it's a full path that we should try to load
    // We should look in "~/.icons"  then "/usr/local/share/icons/" then "/usr/share/icons"
    // If we didn't find it then we try to get the name of the .png and use that to look for icons
    // If its not a path, then we do the same search
    for (auto *l : launchers) {
        delete l;
    }
    launchers.clear();

    std::string path = "/usr/share/applications/";

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(path.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            // Parse desktop file
            INIReader desktop_application(path + ent->d_name);
            if (desktop_application.ParseError() != 0) {
                continue;
            }

            std::string name = desktop_application.Get("Desktop Entry", "Name", "");
            std::string exec = desktop_application.Get("Desktop Entry", "Exec", "");
            std::string icon = desktop_application.Get("Desktop Entry", "Icon", "");

            if (exec.empty())// If we find no exec entry then there's nothing to run
                continue;

            // Remove everything after the first space found in the exec line
            int white_space_position;
            if (std::string::npos != (white_space_position = exec.find(" "))) {
                exec.erase(white_space_position);
            }

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
            launcher->icon = icon;

            launchers.push_back(launcher);
        }
    }

    std::sort(launchers.begin(), launchers.end(), [](const auto &lhs, const auto &rhs) {
        std::string first_name = lhs->name;
        std::string second_name = rhs->name;
        std::for_each(first_name.begin(), first_name.end(), [](char &c) { c = std::tolower(c); });
        std::for_each(second_name.begin(), second_name.end(), [](char &c) { c = std::tolower(c); });

        return first_name < second_name;
    });
    std::thread(paint_desktop_files).detach();
}

void start_app_menu() {
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

    first_expose = true;

    Settings settings;
    settings.force_position = true;
    settings.w = 320;
    settings.h = 641;
    settings.x = 0;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.popup = true;

    AppClient *client = client_new(app, settings, "app_menu");
    client->grab_event_handler = grab_event_handler;
    client_add_handler(app, client, app_menu_event_handler);
    fill_root(client);
    client_show(app, client);
    set_textarea_active();
}
