
#include "app_menu.h"
#include "application.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

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
#include "functional"
#include "simple_dbus.h"
#include "settings_menu.h"
#include <pango/pango-layout.h>
#include <filesystem>
#include <fstream>
#include "utility.h"
#include "drawer.h"
#include <xcb/xcb_cursor.h>
#include <X11/cursorfont.h>

#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"



std::vector<Launcher *> launchers;

class ItemData : public HoverableButton {
public:
    Launcher *launcher = nullptr;
};

struct ItemDataWithClickOffset : public UserData {
    ItemData *item_data = nullptr;
    int x_off = 0;
    int y_off = 0;
    cairo_surface_t *surface = nullptr;
    gl_surface *gsurf = nullptr;
    
    ItemDataWithClickOffset(ItemData *data) {
        this->item_data = data;
        if (surface)
            cairo_surface_destroy(surface);
    }
};

struct RightClickMenuData : public UserData {
    Timeout *tooltip_timeout = nullptr;
    bool inside = false;
    std::string path;
};

struct TooltipMenuData : public UserData {
    std::string path;
};

struct LiveTileItemType : UserData {
    int type = 0; // 0 = live tile, 1 = label/textfield
};

struct LiveTileButtonData : ButtonData {
    Launcher *launcher = nullptr;
    Container *tile = nullptr;
    std::weak_ptr<bool> lifetime;
};

struct LiveTileData : LiveTileItemType {
    Launcher *launcher = nullptr;
    cairo_surface_t *__surface = nullptr;
    gl_surface *gsurf = nullptr;
    Container *tile = nullptr;
//    int w = 2;
//    int h = 2;
    std::weak_ptr<bool> lifetime;
    
    LiveTileData(Launcher *launcher) {
        this->launcher = launcher;
    }
    
    ~LiveTileData() {
        if (__surface)
            cairo_surface_destroy(__surface);
    }
};

// when we open the power sub menu, the left sliding menu needs to be locked
static bool left_locked = false;

static void
paint_live_tile(AppClient *client, cairo_t *cr, Container *container);

static void
when_live_tile_clicked(AppClient *client, cairo_t *cr, Container *container);

void
get_global_coordinates(xcb_connection_t *connection, xcb_window_t window, int relative_x, int relative_y, int *global_x,
                       int *global_y);
void
fill_live_tiles(ScrollContainer *live_scroll);

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    draw_colored_rect(client, correct_opaqueness(client, config->color_apps_background), container->real_bounds);
}

static void
paint_tooltip(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TooltipMenuData *) client->user_data;
    paint_root(client, cr, container);
    
    auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_apps_text), data->path);
    int text_x = (int) (6 * config->dpi);
    int text_y = (int) (container->real_bounds.y + container->real_bounds.h * .5 - h * .5);
    f->draw_text_end(text_x, text_y);
}

static void
paint_power_menu(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    ArgbColor color = correct_opaqueness(client, lighten(config->color_apps_background, 8));
    if (is_light_theme(config->color_apps_background))
        color = correct_opaqueness(client, darken(config->color_apps_background, 8));
    draw_colored_rect(client, color, container->real_bounds);
    
    bool is_light_theme = false;
    {
        double h; // hue
        double s; // saturation
        double p; // perceived brightness
        ArgbColor real = config->color_apps_background;
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
        is_light_theme = p > 50; // if the perceived perceived brightness is greater than that we are a light theme
    }
    
    color = ArgbColor(0.0, 0.0, 0.0, 0.9);
    if (is_light_theme)
        color = ArgbColor(0.0, 0.0, 0.0, 0.34);
    draw_margins_rect(client, color, container->real_bounds, 1, 0);
}

static void
paint_left(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    draw_operator(client, CAIRO_OPERATOR_SOURCE);
    double openess = (container->real_bounds.w - (48 * config->dpi)) / (256 * config->dpi);
    
    auto color = correct_opaqueness(client, config->color_apps_background);
    if (std::abs(container->real_bounds.w - (48 * config->dpi)) > 1) {
        color = correct_opaqueness(client, config->color_apps_background);
        lighten(&color, 2 * openess);
        draw_colored_rect(client, color, container->real_bounds);
    }
   
    draw_operator(client, CAIRO_OPERATOR_OVER);
    
    easingFunction ease = getEasingFunction(easing_functions::EaseInCubic);
    if (std::abs(container->real_bounds.w - (48 * config->dpi)) > 1) {
        int steps = 14 * config->dpi;
        for (int i = 0; i < steps; i++) {
            double scalar = ((double) (i)) / steps;
            scalar = (1 - scalar) * openess;
            scalar = ease(scalar);
            scalar /= (4 * config->dpi);
            draw_colored_rect(client, ArgbColor(0, 0, 0, scalar * 3),
                              Bounds((int) (container->real_bounds.x + container->real_bounds.w + i),
                                     (int) (container->real_bounds.y), 1, (int) (container->real_bounds.h)));
        }
    }

    draw_clip_begin(client, container->real_bounds);
    for (auto c: container->children)
        if (c->when_paint)
            c->when_paint(client, client->cr, c);
    draw_clip_end(client);
}

static void
paint_button_background(AppClient *client, cairo_t *cr, Container *container) {
    container->real_bounds.x += 1;
    container->real_bounds.w -= 2;
    
    auto data = (ButtonData *) container->user_data;
    {
        auto default_color = config->color_taskbar_button_default;
        auto hovered_color = config->color_taskbar_button_hovered;
        auto pressed_color = config->color_taskbar_button_pressed;
        
        auto e = getEasingFunction(easing_functions::EaseOutQuad);
        double time = 0;
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                if (data->previous_state != 2) {
                    time = 40;
                    data->previous_state = 2;
                    client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e,
                                            pressed_color.r);
                    client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e,
                                            pressed_color.g);
                    client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e,
                                            pressed_color.b);
                    client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e,
                                            pressed_color.a);
                }
            } else if (data->previous_state != 1) {
                time = 70;
                data->previous_state = 1;
                client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, hovered_color.r);
                client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, hovered_color.g);
                client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, hovered_color.b);
                client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, hovered_color.a);
            }
        } else if (data->previous_state != 0) {
            time = 100;
            data->previous_state = 0;
            e = getEasingFunction(easing_functions::EaseInCirc);
            client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, default_color.r);
            client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, default_color.g);
            client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, default_color.b);
            client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, default_color.a);
        }
        
        draw_colored_rect(client, data->color, container->real_bounds);
    }
    
    container->real_bounds.x -= 1;
    container->real_bounds.w += 2;
}

static void
paint_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_button_background(client, cr, container);
    auto data = (ButtonData *) container->user_data;
    if (data) {
        std::string text;
        
        bool right = false;
        // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
        if (data->text == "START") {
            text = "\uE700";
        } else if (data->text == "Documents") {
            text = "\uE7C3";
        } else if (data->text == "Downloads") {
            text = "\uE896";
        } else if (data->text == "Music") {
            text = "\uEC4F";
        } else if (data->text == "Pictures") {
            text = "\uEB9F";
        } else if (data->text == "Videos") {
            text = "\uE714";
        } else if (data->text == "Network") {
            text = "\uEC27";
        } else if (data->text == "Personal Folder") {
            text = "\uEC25";
        } else if (data->text == "File Explorer") {
            text = "\uEC50";
        } else if (data->text == "Settings") {
            text = "\uE713";
        } else if (data->text == "Power") {
            text = "\uE7E8";
        } else if (data->text == "Off") {
            text = "\uE947";
        } else if (data->text == "Sign Out") {
            text = "\uE77B";
        } else if (data->text == "Shut Down") {
            text = "\uE7E8";
        } else if (data->text == "Restart") {
            text = "\uE777";
        } else if (data->text == "Reload") {
            text = "\uE117";
        } else if (data->text == "Open file location") {
            text = "\uED43";
        } else if (data->text == "Pin to Start") {
            text = "\uE718";
        } else if (data->text == "Unpin from Start") {
            text = "\uE77A";
        } else if (data->text == "Small") {
            text = "\uE743";
        } else if (data->text == "Medium") {
            text = "\uE744";
        } else if (data->text == "Wide") {
            text = "\uE745";
        } else if (data->text == "Large") {
            text = "\uE747";
        } else if (data->text == "Resize") {
            right = true;
        }
        
        if (right) {
            auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->icons, EXPAND(config->color_apps_icons), "\uE974");
            f->draw_text(
                    (int) (container->real_bounds.x + container->real_bounds.w - container->real_bounds.h / 2 - h / 2),
                    (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2));
            f->end();
        } else {
            auto [f, w, h] = draw_text_begin(client, 12 * config->dpi, config->icons, EXPAND(config->color_apps_icons), text);
            f->draw_text((int) (container->real_bounds.x + container->real_bounds.h / 2 - w / 2),
                         (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2));
            f->end();
        }
    }
    
    auto taskbar_entity = client_by_name(app, "taskbar");
    auto taskbar_root = taskbar_entity->root;
    auto super = container_by_name("super", taskbar_root);
    
    if (container->parent->wanted_bounds.w != super->real_bounds.w) {
        bool should_bold = false;
        if (data->text == "START")
            should_bold = true;
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_apps_icons), data->text, should_bold);
        f->draw_text((int) (container->real_bounds.x + super->real_bounds.w),
                     (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2));
        f->end();
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
        if (auto scroll_pane = (ScrollContainer *) container_by_name("scroll_pane", client->root)) {
            int offset = -scroll_pane->scroll_v_real + c->real_bounds.y;
            offset -= 5 * config->dpi;
            scroll_pane->scroll_v_real = -offset;
            scroll_pane->scroll_v_visual = scroll_pane->scroll_v_real;
            ::layout(client, cr, scroll_pane, scroll_pane->real_bounds);
        
            clicked_title(client, cr, container);
        }
    }
}

static void
right_clicked_application(AppClient *client, cairo_t *cr, Container *container);

static void
clicked_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (container->state.mouse_button_pressed == 3) {
        right_clicked_application(client, cr, container);
    } else {
        auto *data = (ItemData *) container->user_data;
        set_textarea_inactive();
        launch_command(data->launcher->exec);
        client_close_threaded(app, client);
        xcb_flush(app->connection);
        app->grab_window = -1;
    }
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
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        auto color = config->color_apps_pressed_item;
        if (container->state.mouse_pressing) {
            color = config->color_apps_pressed_item;
        } else {
            color = config->color_apps_hovered_item;
        }
        draw_colored_rect(client, color, container->real_bounds);
    }
    
    draw_clip_begin(client, container->real_bounds);
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_apps_text), data->launcher->name,
              container->real_bounds, 5, 44 * config->dpi);
    draw_clip_end(client);
    
    if (data->launcher->icon_24__) {
        int width = cairo_image_surface_get_width(data->launcher->icon_24__);
        int height = cairo_image_surface_get_height(data->launcher->icon_24__);
        
        draw_gl_texture(client, data->launcher->g24,
                        data->launcher->icon_24__,
                                 (int) (container->real_bounds.x + 8 * config->dpi),
                                 (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    } else {
        int width = cairo_image_surface_get_width(global->unknown_icon_24);
        int height = cairo_image_surface_get_height(global->unknown_icon_24);
        
        draw_gl_texture(client, global->u24,
                        global->unknown_icon_24,
                        (int) (container->real_bounds.x + 8 * config->dpi),
                        (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
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
        ArgbColor color = config->color_apps_hovered_item;
        if (container->state.mouse_pressing)
            set_argb(cr, config->color_apps_pressed_item);
        draw_colored_rect(client, color, container->real_bounds);
    }
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_apps_text), data->text,
              container->real_bounds, 5, 3 * config->dpi);
    
    // TODO: I think maybe this paints new updates icon?
//    if (data->surface) {
//        draw_gl_texture(client, data->gsurf, data->surface, container->real_bounds.x, container->real_bounds.y);
//    }
/*
    if (data->surface) {
        cairo_set_source_surface(cr, data->surface, container->real_bounds.x, container->real_bounds.y);
        cairo_paint(cr);
    }
 */
}

static Timeout *left_open_fd = nullptr;

static void
left_open_timeout(App *app, AppClient *client, Timeout *, void *data) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (left_locked)
        return;
    auto *container = (Container *) data;
    if (app && app->running && valid_client(app, client) &&
        (container->state.mouse_hovering || container->state.mouse_pressing)) {
        client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, 100, nullptr,
                                (int) (256 * config->dpi), true);
    }
    left_open_fd = nullptr;
}

static void
left_open(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (left_locked)
        return;
    if (left_open_fd == nullptr) {
        left_open_fd = app_timeout_create(client->app, client, 160, left_open_timeout, container, const_cast<char *>(__PRETTY_FUNCTION__));
    } else {
        app_timeout_replace(client->app, client, left_open_fd, 160, left_open_timeout, container);
    }
}

static void
left_close(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (left_locked || !container)
        return;
    client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, 70, nullptr, (int) (48 * config->dpi), true);
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

static int
app_menu_width() {
    bool any_pinned = false;
    for (auto l: launchers) {
        if (l->get_pinned()) {
            any_pinned = true;
            break;
        }
    }
    if (!winbar_settings->allow_live_tiles)
        any_pinned = false;
    if (any_pinned) {
		int total_w = 320 * config->dpi + (342 * config->dpi) + (342 * config->dpi * winbar_settings->extra_live_tile_pages);
		bool will_save = false;
		while (total_w >= app->screen->width_in_pixels && winbar_settings->extra_live_tile_pages >= 1) {
			total_w -= 342 * config->dpi;
			winbar_settings->extra_live_tile_pages -= 1;
			will_save = true;
		}
        return total_w;
    } else {
        return 320 * config->dpi;
    }
}

static void
clicked_start_button(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Toggle left menu openess
    if (auto *client = client_by_name(app, "app_menu")) {
        if (auto *container = container_by_name("left_buttons", client->root)) {
            if (container->real_bounds.w <= 64 * config->dpi) {
                client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, 120, nullptr, (int) (256 * config->dpi), true);
            } else if (container->real_bounds.w > 86 * config->dpi) {
                client_create_animation(app, client, &container->wanted_bounds.w, container->lifetime, 0, 120, nullptr, (int) (48 * config->dpi), true);
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
sub_menu_closed(AppClient *client) {
    left_locked = false;
    if (auto client = client_by_name(app, "app_menu")) {
        left_close(client, client->cr, container_by_name("left_buttons", client->root));
    }
    if (auto c = client_by_name(app, "tooltip_popup"))
        client_close_threaded(app, c);
    if (auto c = client_by_name(app, "right_click_resize_popup"))
        client_close_threaded(app, c);
}

static void
clicked_open_settings(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    open_settings_menu(SettingsPage::Taskbar);
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
        if (winbar_settings->search_behaviour == "Fully Disabled")
            return;
        
        app_menu_client->animations.clear();
        app_menu_client->animations_running = 0;
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
    } else if (keysym == XKB_KEY_Up) {
        auto scroll_pane = container_by_name("scroll_pane", client->root);
        fine_scrollpane_scrolled(client, client->cr, scroll_pane, 0, 100 * config->dpi, false);
    } else if (keysym == XKB_KEY_Down) {
        auto scroll_pane = container_by_name("scroll_pane", client->root);
        fine_scrollpane_scrolled(client, client->cr, scroll_pane, 0, -100 * config->dpi, false);
    }
}

static void
clicked_off(AppClient *client, cairo_t *cr, Container *container) {
    client->app->running = false;
}

static void
clicked_shut_down(AppClient *client, cairo_t *cr, Container *container) {
    dbus_computer_shut_down();
}

static void
clicked_logoff(AppClient *client, cairo_t *cr, Container *container) {
    dbus_computer_logoff();
}

static void
clicked_restart(AppClient *client, cairo_t *cr, Container *container) {
    dbus_computer_restart();
}

static void
clicked_reload(AppClient *client, cairo_t *cr, Container *container) {
    client->app->running = false;
    restart = true;
}

static void
clicked_open_in_folder(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (ButtonData *) container->user_data;
    dbus_open_in_folder(data->full_path);
    if (auto *c = client_by_name(app, "app_menu"))
        client_close_threaded(app, c);
    client_close_threaded(app, client);
}

static void
recurse_delete(Container *c, std::string full_path) {
    for (auto item: c->children) {
        recurse_delete(item, full_path);
    }
    for (int i = 0; i < c->children.size(); ++i) {
        auto co = c->children[i];
        if (co->user_data) {
            auto d = (LiveTileData *) co->user_data;
            if (d->launcher) {
                if (full_path == d->launcher->full_path) {
                    c->children.erase(c->children.begin() + i);
                    d->launcher = nullptr;
                    delete co;
                    break;
                }
            }
        }
    }
}

static void
possibly_resize_after_pin_unpin() {
    uint32_t value_mask =
            XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    
    int w = app_menu_width();
    int h = winbar_settings->start_menu_height * config->dpi;
    int x = app->bounds.x;
    int y = app->bounds.h - h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *super = container_by_name("super", taskbar->root);
        auto field_search = container_by_name("field_search", taskbar->root);
        if (super->exists) {
            x = taskbar->bounds->x + super->real_bounds.x;
        } else if (field_search->exists) {
            x = taskbar->bounds->x + field_search->real_bounds.x;
        } else {
            x = taskbar->bounds->x;
        }
        // Make sure doesn't go off-screen right side
        if (x + w > taskbar->screen_information->width_in_pixels) {
            x = taskbar->screen_information->width_in_pixels - w;
        }
        y = taskbar->bounds->y - h;
    }
    uint32_t value_list_resize[] = {
            (uint32_t) (x),
            (uint32_t) (y),
            (uint32_t) (w),
            (uint32_t) (h),
    };
    AppClient *app_menu = client_by_name(app, "app_menu");
    xcb_configure_window(app->connection, app_menu->window, value_mask, value_list_resize);
}

static void
projected_live_tile_position_based_on_drag(int left_edge_x, int top_edge_y,
                                           const int w, const int h,
                                           int *result_x, int *result_y, Launcher *launcher, int scroll_v, bool pin = false) {
    auto client = (AppClient *) client_by_name(app, "app_menu");
    if (!client) return;
    auto live_scroll = container_by_name("live_scroll", client->root);
    
    int extra_pages = winbar_settings->extra_live_tile_pages;
    int pages = 1 + extra_pages;
    int page_size = live_scroll->real_bounds.w / pages;
    int pad_size = (page_size - (100 * config->dpi) * 3) / 2;
    int x = live_scroll->real_bounds.x + pad_size;
    if (left_edge_x < x)
        left_edge_x = x;
    
    int page =
            std::floor(((double) (left_edge_x + pad_size) / (double) live_scroll->real_bounds.w) * (double) pages) - 1;
    int max_overlap = 0;
    for (int i = 0; i < pages; i++) {
        int page_start_x = live_scroll->real_bounds.x + i * page_size;
        int page_end_x = page_start_x + page_size;
        int tile_w = w * (50 * config->dpi);
        int tile_x = left_edge_x;
        int tile_end_x = tile_x + tile_w;
        
        int overlap = std::max(0, std::min(tile_end_x, page_end_x) - std::max(tile_x, page_start_x));
        
        if (overlap > max_overlap) {
            max_overlap = overlap;
            page = i;
        }
    }
    
    float title_pad = 40 * config->dpi;
    float pad = 2 * config->dpi;
    
    float start_x = x + pad;
    float start_y = live_scroll->real_bounds.y + pad + title_pad;
    
    if (page >= 1)
        left_edge_x -= page * page_size;
    
    float offset_x = std::max(0.0f, left_edge_x - start_x);
    float offset_y = std::max(0.0f, top_edge_y - start_y);
    int tile_x = std::round(offset_x / (50 * config->dpi));
    tile_x = std::min(6 - w, tile_x);
    int tile_y = std::round((offset_y - scroll_v) / (50 * config->dpi));
//    tile_y = std::min((int) (live_scroll->real_bounds.h / (50 * config->dpi)) - h, tile_y);
    
    if (bounds_contains(live_scroll->real_bounds, client->mouse_current_x, client->mouse_current_y)) {
        launcher->info.x = tile_x;
        launcher->info.y = tile_y;
        if (page >= pages)
            page = pages - 1;
        launcher->info.page = page;
        if (result_x != nullptr) {
            *result_x = tile_x;
            *result_y = tile_y;
        }
        if (pin) {
            launcher->set_pinned(true);
        }
    } else if (pin) {
        launcher->set_pinned(false);
    }
}

static void
clicked_add_to_live_tiles(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (ButtonData *) container->user_data;
    for (auto item: launchers) {
        if (item->full_path == data->full_path && item->get_pinned()) {
            item->set_pinned(false);
            
            auto live_scroll = (ScrollContainer *) container_by_name("live_scroll",
                                                                     client_by_name(app, "app_menu")->root);
            
            recurse_delete(live_scroll->content, item->full_path);
            
            possibly_resize_after_pin_unpin();
            
            client_layout(app, client);
            request_refresh(app, client);
        } else if (item->full_path == data->full_path && !item->get_pinned()) {
            item->set_pinned(true);
            item->info.w = 2;
            item->info.h = 2;

            possibly_resize_after_pin_unpin();
            
            auto live_scroll = (ScrollContainer *) container_by_name("live_scroll",
                                                                     client_by_name(app, "app_menu")->root);
//            auto live_tile_width = (660 - 320) * config->dpi;
            
            if (live_scroll) {
                auto last_line = live_scroll->content;
//                last_line->alignment = ALIGN_CENTER_HORIZONTALLY;
                last_line->spacing = 4 * config->dpi;
                
                auto live_tile = last_line->child(100 * config->dpi, 100 * config->dpi);
                live_tile->when_paint = paint_live_tile;
                live_tile->when_clicked = when_live_tile_clicked;
                live_tile->when_drag = [](AppClient *client, cairo_t *, Container *c) {
                    if (auto drag = client_by_name(app, "drag_window")) {
                        int global_x = 0;
                        int global_y = 0;
                        get_global_coordinates(app->connection, client->window, client->mouse_current_x,
                                               client->mouse_current_y, &global_x, &global_y);
                        uint32_t value_list_resize[] = {
                                (uint32_t) (global_x + (c->real_bounds.x - client->mouse_initial_x)),
                                (uint32_t) (global_y + (c->real_bounds.y - client->mouse_initial_y)),
                        };
                        xcb_configure_window(app->connection, drag->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                             value_list_resize);
                    }
                    auto scroll = (ScrollContainer *) c;
                    auto data = (LiveTileData *) c->user_data;
                    int result_x, result_y;
                    int left_edge_x = client->mouse_current_x - (client->mouse_initial_x - c->real_bounds.x);
                    int top_edge_y = client->mouse_current_y - (client->mouse_initial_y - c->real_bounds.y);
                    projected_live_tile_position_based_on_drag(left_edge_x, top_edge_y, data->launcher->info.w,
                                                               data->launcher->info.h, &result_x, &result_y,
                                                               data->launcher, scroll->scroll_v_visual, true);
                    data->launcher->info.x = result_x;
                    data->launcher->info.y = result_y;
                };
                live_tile->when_drag_start = [](AppClient *client, cairo_t *, Container *c) {
                    c->when_paint = nullptr;
                    request_refresh(app, client);
                    Settings settings;
                    settings.force_position = true;
                    settings.skip_taskbar = true;
                    settings.decorations = false;
                    settings.override_redirect = true;
                    settings.w = 100 * config->dpi;
                    settings.h = 100 * config->dpi;
                    auto data = (LiveTileData *) c->user_data;
                    if (data->lifetime.lock()) {
                        float pad = 2 * config->dpi;
                        settings.w = data->launcher->info.w * (50 * config->dpi) - pad * 2;
                        settings.h = data->launcher->info.h * (50 * config->dpi) - pad * 2;
                    }
                    auto drag = client_new(app, settings, "drag_window");
                    client_show(app, drag);
                    drag->root->user_data = c->user_data;
                    drag->root->when_paint = paint_live_tile;
                };
                live_tile->when_drag_end = [](AppClient *, cairo_t *, Container *c) {
                    c->when_paint = paint_live_tile;
                    if (auto drag = client_by_name(app, "drag_window")) {
                        drag->root->user_data = nullptr; // So that we don't double delete
                        client_close_threaded(app, client_by_name(app, "drag_window"));
                    }
                    if (auto c = client_by_name(app, "app_menu")) {
                        if (auto live_scroll = container_by_name("live_scroll", c->root)) {
                            fill_live_tiles((ScrollContainer *) live_scroll);
                        }
                    }
                };
                live_tile->when_drag_end_is_click = false;
                live_tile->minimum_x_distance_to_move_before_drag_begins = 4 * config->dpi;
                live_tile->minimum_y_distance_to_move_before_drag_begins = 4 * config->dpi;
                LiveTileData *live_drag_data = new LiveTileData(item);
                live_tile->user_data = live_drag_data;
                live_drag_data->lifetime = live_tile->lifetime;
                live_drag_data->tile = live_tile;
            }
        }
    }
    client_close_threaded(app, client);
}

static void
mouse_leaves_open_file_location(AppClient *right_click_client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (RightClickMenuData *) right_click_client->user_data;
    data->inside = false;
    if (auto c = client_by_name(app, "tooltip_popup")) {
        client_close_threaded(app, c);
    }
}

static void
paint_resize_button(AppClient *client, cairo_t *cr, Container *container) {
    paint_button_background(client, cr, container);
    auto data = (LiveTileButtonData *) container->user_data;
    
    bool has_checkmark = false;
    if (data->lifetime.lock()) {
        auto *live = (LiveTileData *) data->tile->user_data;
        if (live->launcher->info.w == 1 && live->launcher->info.h) {
            if (data->text == "Small") {
                has_checkmark = true;
            }
        } else if (live->launcher->info.w == 4 && live->launcher->info.h == 4) {
            if (data->text == "Large") {
                has_checkmark = true;
            }
        } else if (live->launcher->info.w == 2 && live->launcher->info.h == 2) {
            if (data->text == "Medium") {
                has_checkmark = true;
            }
        } else if (live->launcher->info.w == 4 && live->launcher->info.h == 2) {
            if (data->text == "Wide") {
                has_checkmark = true;
            }
        }
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (has_checkmark) {
        draw_text(client, 12 * config->dpi, config->icons, EXPAND(config->color_apps_icons), "\uE73E", container->real_bounds);
    }
    
    std::string icon;
    if (data->text == "Small") {
        icon = "\uE743";
    } else if (data->text == "Medium") {
        icon = "\uE744";
    } else if (data->text == "Wide") {
        icon = "\uE745";
    } else if (data->text == "Large") {
        icon = "\uE747";
    }
    
    {
        auto [f, w, h] = draw_text_begin(client, 12 * config->dpi, config->icons, EXPAND(config->color_apps_icons), data->text);
        int text_x = (int) (container->real_bounds.x + ((container->real_bounds.h / 2 - h / 2) * 2 + h));
        int text_y = (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2);
        f->draw_text_end(text_x, text_y);
    }
    
    {
        auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_apps_text), data->text);
        int text_x = (int) (container->real_bounds.x + ((container->real_bounds.h * .5 - h * .5) * 3) + h * .5);
        int text_y = (int) (container->real_bounds.y + container->real_bounds.h * .5 - h * .5);
        f->draw_text_end(text_x, text_y);
    }
}

template<int W, int H>
static void
make_resize_option_container(std::string label, Container *menu, LiveTileButtonData *data) {
    auto option_container = menu->child(FILL_SPACE, FILL_SPACE);
    option_container->when_clicked = [](AppClient *, cairo_t *, Container *c) {
        auto *data = (LiveTileButtonData *) c->user_data;
        if (data->lifetime.lock()) {
            auto *live = (LiveTileData *) data->tile->user_data;
            live->launcher->info.w = W; // Use template parameter W
            live->launcher->info.h = H; // Use template parameter H
            request_refresh(app, client_by_name(app, "app_menu"));
            if (auto client = client_by_name(app, "live_tile_right_click_menu"))
                client_close_threaded(app, client);
            if (auto client = client_by_name(app, "right_click_resize_popup"))
                client_close_threaded(app, client);
        }
    };
    option_container->when_paint = paint_resize_button;
    auto *option_data = new LiveTileButtonData;
    option_data->full_path = data->full_path;
    if (data->tile) {
        option_data->lifetime = data->tile->lifetime;
        option_data->tile = data->tile;
    }
    option_data->text = label;
    option_container->user_data = option_data;
}

void live_tiles_resize_popup(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (LiveTileButtonData *) container->user_data;
    
    int options_count = 4;
    int pad = 6 * config->dpi;
    Settings settings;
    settings.force_position = true;
    settings.w = 256 * config->dpi;
    settings.h = ((36 * options_count) * config->dpi) + (config->dpi * (options_count - 1)) + (pad * 2);
    settings.x = client->bounds->x + client->bounds->w - 4 * config->dpi;
    settings.y = client->bounds->y + container->real_bounds.y;
    if (app->screen) {
        if (settings.y + settings.h > app->screen->height_in_pixels) {
            settings.y = client->mouse_current_y + client->bounds->y - settings.h;
        }
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto live_tile_fly_out = client_by_name(app, "live_tile_right_click_menu")) {
        PopupSettings popup_settings;
        popup_settings.close_on_focus_out = false;
        popup_settings.takes_input_focus = true;
        
        auto popup = live_tile_fly_out->create_popup(popup_settings, settings);
        popup->name = "right_click_resize_popup";
        popup->when_closed = [](AppClient *resize_menu) {
            // check if mouse is currently hovering over the other popup
            
            if (auto c = client_by_name(app, "live_tile_right_click_menu")) {
                int g_x = 0;
                int g_y = 0;
                get_global_coordinates(app->connection, resize_menu->window, resize_menu->mouse_current_x,
                                       resize_menu->mouse_current_y, &g_x, &g_y);
                if (!bounds_contains(*c->bounds, g_x, g_y)) {
                    client_close_threaded(app, c);
                }
            }
        };
        
        popup->root->when_paint = paint_power_menu;
        popup->root->type = vbox;
        popup->root->spacing = 1;
        popup->root->wanted_pad.y = pad;
        popup->root->wanted_pad.h = pad;
        popup->user_data = new RightClickMenuData;
        
        make_resize_option_container<1, 1>("Small", popup->root, data);
        make_resize_option_container<2, 2>("Medium", popup->root, data);
        make_resize_option_container<4, 2>("Wide", popup->root, data);
        make_resize_option_container<4, 4>("Large", popup->root, data);
        
        client_show(app, popup);
        
        xcb_set_input_focus(app->connection, XCB_NONE, popup->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}

void something_timeout(App *app, AppClient *client, Timeout *timeout, void *user_data) {
    auto *data = (RightClickMenuData *) client->user_data;
    data->tooltip_timeout = nullptr;
    if (!data->inside || client_by_name(app, "right_click_resize_popup") != nullptr) {
        return;
    }
    
    PangoLayout *layout = get_cached_pango_font(client->cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(layout, data->path.c_str(), data->path.length());
    
    int width, height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    Settings settings;
    settings.force_position = true;
    settings.w = width + 6 * 2 * config->dpi;
    settings.h = height + 6 * 2 * config->dpi;
    settings.x = client->mouse_current_x + client->bounds->x;
    settings.y = client->bounds->y - settings.h;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.no_input_focus = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    PopupSettings popup_settings;
    popup_settings.takes_input_focus = false;
    popup_settings.close_on_focus_out = false;
    popup_settings.wants_grab = false;
    
    auto popup = client->create_popup(popup_settings, settings);
    auto *tooltip_data = new TooltipMenuData;
    tooltip_data->path = data->path;
    popup->user_data = tooltip_data;
    popup->name = "tooltip_popup";
    popup->root->when_paint = paint_tooltip;
    client_show(app, popup);
    request_refresh(app, popup);
}

static void
mouse_enters_open_file_location(AppClient *right_click_client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (RightClickMenuData *) right_click_client->user_data;
    data->inside = true;
    auto *l_data = (ButtonData *) container->user_data;
    data->path = l_data->full_path;
    if (auto c = client_by_name(app, "tooltip_popup")) {
        return;
    }
    
    if (data->tooltip_timeout == nullptr) {
        data->tooltip_timeout = app_timeout_create(app, right_click_client, 100, something_timeout, nullptr,
                                                   const_cast<char *>(__PRETTY_FUNCTION__));
    }
}

static void
make_option_file_location_container(Container *root, std::string full_path) {
    auto l = root->child(FILL_SPACE, FILL_SPACE);
    l->when_clicked = clicked_open_in_folder;
    l->when_mouse_enters_container = mouse_enters_open_file_location;
    l->when_mouse_leaves_container = mouse_leaves_open_file_location;
    l->when_paint = paint_button;
    auto *l_data = new ButtonData;
    l_data->full_path = full_path;
    l_data->text = "Open file location";
    l->user_data = l_data;
}

static void
make_option_pin_unpin_container(Container *root, bool pinned, std::string full_path) {
    auto l = root->child(FILL_SPACE, FILL_SPACE);
    l->when_clicked = clicked_add_to_live_tiles;
    l->when_paint = paint_button;
    auto *l_data = new ButtonData;
    l_data->full_path = full_path;
    if (pinned) {
        l_data->text = "Unpin from Start";
    } else {
        l_data->text = "Pin to Start";
    }
    l->user_data = l_data;
}
static void
right_clicked_application(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (ItemData *) container->user_data;

    int options_count = 1;
    if (winbar_settings->allow_live_tiles)
        options_count++;
    int pad = 6 * config->dpi;
    Settings settings;
    settings.force_position = true;
    settings.w = 256 * config->dpi;
    settings.h = ((36 * options_count) * config->dpi) + (config->dpi * (options_count - 1)) + (pad * 2);
    // TODO: get mouse position
    settings.x = client->mouse_current_x + client->bounds->x;
    settings.y = client->mouse_current_y + client->bounds->y;
    if (app->screen) {
        if (settings.y + settings.h > app->screen->height_in_pixels) {
            settings.y = client->mouse_current_y + client->bounds->y - settings.h;
        }
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto app_menu = client_by_name(app, "app_menu")) {
        PopupSettings popup_settings;
        popup_settings.close_on_focus_out = false;
        popup_settings.takes_input_focus = true;
        
        auto popup = app_menu->create_popup(popup_settings, settings);
        popup->name = "right_click_popup";
        
        popup->root->when_paint = paint_power_menu;
        popup->root->type = vbox;
        popup->root->spacing = 1;
        popup->root->wanted_pad.y = pad;
        popup->root->wanted_pad.h = pad;
        popup->user_data = new RightClickMenuData;
        
        if (winbar_settings->allow_live_tiles) {
            make_option_pin_unpin_container(popup->root, data->launcher ? data->launcher->get_pinned() : false,
                                            data->launcher ? data->launcher->full_path : "");
        }
        
        make_option_file_location_container(popup->root, data->launcher ? data->launcher->full_path : "");
        
        popup->when_closed = sub_menu_closed;
        
        client_show(app, popup);
        
        xcb_set_input_focus(app->connection, XCB_NONE, popup->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}

static void
right_clicked_live_tile(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (LiveTileData *) container->user_data;
    
    int options_count = 3;
    int pad = 6 * config->dpi;
    Settings settings;
    settings.force_position = true;
    settings.w = 256 * config->dpi;
    settings.h = ((36 * options_count) * config->dpi) + (config->dpi * (options_count - 1)) + (pad * 2);
    settings.x = client->mouse_current_x + client->bounds->x;
    settings.y = client->mouse_current_y + client->bounds->y;
    if (app->screen) {
        if (settings.y + settings.h > app->screen->height_in_pixels) {
            settings.y = client->mouse_current_y + client->bounds->y - settings.h;
        }
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto app_menu = client_by_name(app, "app_menu")) {
        PopupSettings popup_settings;
        popup_settings.close_on_focus_out = false;
        popup_settings.takes_input_focus = true;
        
        auto popup = app_menu->create_popup(popup_settings, settings);
        popup->name = "live_tile_right_click_menu";
        
        popup->root->when_paint = paint_power_menu;
        popup->root->type = vbox;
        popup->root->spacing = 1;
        popup->root->wanted_pad.y = pad;
        popup->root->wanted_pad.h = pad;
        popup->user_data = new RightClickMenuData;
        
        make_option_pin_unpin_container(popup->root, data->launcher ? data->launcher->get_pinned() : false,
                                        data->launcher ? data->launcher->full_path : "");
        
        {
            auto l = popup->root->child(FILL_SPACE, FILL_SPACE);
            l->when_clicked = live_tiles_resize_popup;
            l->when_paint = paint_button;
            auto *l_data = new LiveTileButtonData;
            l_data->launcher = data->launcher;
            if (data->tile) {
                l_data->lifetime = data->tile->lifetime;
                l_data->tile = data->tile;
            }
            if (data->launcher)
                l_data->full_path = data->launcher->full_path;
            l_data->text = "Resize";
            l->user_data = l_data;
        }
        
        make_option_file_location_container(popup->root, data->launcher ? data->launcher->full_path : "");
        
        client_show(app, popup);
        
        xcb_set_input_focus(app->connection, XCB_NONE, popup->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}

bool covering_each_other(Bounds a, Bounds b) {
    // Check if one rectangle is completely to the left or right of the other
    if (a.x >= (b.x + b.w) || b.x >= (a.x + a.w))
        return false;
    
    // Check if one rectangle is completely above or below the other
    return !(a.y >= (b.y + b.h) || b.y >= (a.y + a.h));
}

static void
next_pin_location(int w, int h, int *x, int *y) {
    // Dumber algo, start at 0, 0, and confirm it doesn't overlap with any other rects
    for (int y_index = 0; y_index < 50; ++y_index) {
        for (int x_index = 0; x_index <= 6 - w; ++x_index) {
            bool problem = false;
            for (const auto &item: launchers) {
                if (item->get_pinned() && item->info.x != -1) {
                    const Bounds &pretend = Bounds(x_index, y_index, w, h);
                    const Bounds &actual = Bounds(item->info.x, item->info.y, item->info.w, item->info.h);
                    if (covering_each_other(pretend, actual)) {
                        problem = true;
                    }
                }
            }
            if (!problem) {
                *x = x_index;
                *y = y_index;
                return;
            }
        }
    }
}

// client_layout layout live tile
void paint_live_tile_bg(AppClient *client, cairo_t *cr, Container *container) {
    auto v = container->scroll_v_visual;
    ::layout(client, cr, container, container->real_bounds);
    container->scroll_v_visual = v;
    auto s = (ScrollContainer *) container;
    
    std::string text("Pinned Apps");
    
    int extra_pages = winbar_settings->extra_live_tile_pages;
    int pages = 1 + extra_pages;
    
    // Assumes only one page exists
    // int x = container->real_bounds.x + (container->real_bounds.w - 100 * config->dpi * 3) / 2;
    int page_size = container->real_bounds.w / pages;
    
    int page = 0;
    int x = container->real_bounds.x + ((page_size - 100 * config->dpi * 3) / 2) + page * page_size;
    
    draw_clip_begin(client, container->parent->real_bounds);
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_apps_text), text, container->real_bounds, 5,
              x - container->real_bounds.x + container->scroll_h_visual,
              (int) (12 * config->dpi + container->scroll_v_visual));
    
    for (auto pin: launchers) {
        if (pin->get_pinned() && pin->info.x == -1) {
            int target_x = 0;
            int target_y = 0;
            pin->info.w = 2;
            pin->info.h = 2;
            next_pin_location(pin->info.w, pin->info.h, &target_x, &target_y);
            pin->info.x = target_x;
            pin->info.y = target_y;
        }
    }
    
    float title_pad = 40 * config->dpi;
    for (auto item: s->content->children) {
        auto info = (LiveTileItemType *) item->user_data;
        if (info->type == 0 && item->when_paint) { // what is type specifiying here?
            auto live = (LiveTileData *) item->user_data;
            
            page = live->launcher->info.page;
            if (page >= pages) {
                item->exists = false;
            } else {
                item->exists = true;
            }
            int x = container->real_bounds.x + ((page_size - 100 * config->dpi * 3) / 2) + page * page_size;
            
            float pad = 2 * config->dpi;
            float start_x = x + pad + container->scroll_h_visual;
            float start_y = container->real_bounds.y + pad + title_pad + container->scroll_v_visual;
            item->real_bounds.x = start_x + live->launcher->info.x * (50 * config->dpi);
            item->real_bounds.y = start_y + live->launcher->info.y * (50 * config->dpi);
            item->real_bounds.w = live->launcher->info.w * (50 * config->dpi) - pad * 2;
            item->real_bounds.h = live->launcher->info.h * (50 * config->dpi) - pad * 2;
            
            paint_live_tile(client, cr, item);
        }
    }
    draw_clip_end(client);
}

void
paint_live_tile_data(AppClient *client, cairo_t *cr, Container *container, cairo_surface_t *surface, gl_surface *gsurf,
                     Launcher *launcher) {
    auto bg = lighten(config->color_apps_background, 10);
    bg.a = .4;
    draw_colored_rect(client, bg, container->real_bounds);
    
    if (container->state.mouse_pressing && !container->state.mouse_dragging) {
        draw_margins_rect(client, lighten(bg, 65), container->real_bounds, 2 * config->dpi, 0);
    } else if (container->state.mouse_hovering && !container->state.mouse_dragging) {
        draw_margins_rect(client, lighten(bg, 40), container->real_bounds, 2 * config->dpi, 0);
    }
    
    int size = 36 * config->dpi;
    if (surface && gsurf) {
        draw_gl_texture(client, gsurf, surface,
                        (int) (container->real_bounds.x + container->real_bounds.w / 2 - size / 2),
                        (int) (container->real_bounds.y + container->real_bounds.h / 2 - size / 2));
    }
    int text_margin = 3 * config->dpi;
    
    draw_clip_begin(client, Bounds(container->real_bounds.x, container->real_bounds.y,
                                   container->real_bounds.w - text_margin * 1.3, container->real_bounds.h));
    auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(config->color_apps_text), launcher->name, false);
    f->draw_text_end((int) (container->real_bounds.x + text_margin * 1.3),
                 (int) (container->real_bounds.y + container->real_bounds.h - h - text_margin));
    draw_clip_end(client);
}

void paint_live_tile(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (LiveTileData *) container->user_data;
    int size = 36 * config->dpi;
    if (data->__surface == nullptr) {
        data->__surface = accelerated_surface(app, client, size, size);
        delete data->gsurf;
        data->gsurf = new gl_surface;

        if (!data->launcher->icon.empty() && data->launcher->icon[0] == '/') {
            paint_surface_with_image(data->__surface, data->launcher->icon, size, nullptr);
        } else {
            std::vector<IconTarget> targets;
            // TODO: if icon is path, use path
            targets.emplace_back(data->launcher->icon);
            search_icons(targets);
            pick_best(targets, size);
            for (const auto &item: targets[0].candidates) {
                paint_surface_with_image(data->__surface, item.full_path(), size, nullptr);
                break;
            }
        }
    }
    
    paint_live_tile_data(client, cr, container, data->__surface, data->gsurf, data->launcher);
}

void when_live_tile_clicked(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        right_clicked_live_tile(client, cr, container);
        return;
    }
    // TOOD: we should take a ref to not use after free like we do in other places with shared point
    auto data = (LiveTileData *) container->user_data;
    if (data->launcher) {
        launch_command(data->launcher->exec);
        app_timeout_create(app, client, 100, [](App *, AppClient *client, Timeout *, void *) {
            client_close_threaded(app, client);
        }, nullptr, "Delayed closer for some sort of 'pop' animation");
    }
}

void
get_global_coordinates(xcb_connection_t *connection, xcb_window_t window, int relative_x, int relative_y, int *global_x,
                       int *global_y) {
    xcb_translate_coordinates_cookie_t translate_cookie;
    xcb_translate_coordinates_reply_t *translate_reply;
    
    // Translate the window's (0, 0) to the root window's coordinates
    translate_cookie = xcb_translate_coordinates(connection, window,
                                                 xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root, 0, 0);
    translate_reply = xcb_translate_coordinates_reply(connection, translate_cookie, NULL);
    
    if (translate_reply) {
        *global_x = translate_reply->dst_x + relative_x;
        *global_y = translate_reply->dst_y + relative_y;
        free(translate_reply);
    } else {
        fprintf(stderr, "Failed to translate coordinates.\n");
    }
}

static void
clicked_open_power_menu(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // TODO make button open *and* close menu.
    left_locked = true;
    
    Settings settings;
    settings.force_position = true;
    settings.w = 256 * config->dpi;
    settings.h = ((48 * 4) * config->dpi) + (config->dpi * 3);
    settings.x = client->bounds->x;
    settings.y = app->bounds.h - settings.h - config->taskbar_height - (48 * 1) * config->dpi;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.y = taskbar->bounds->y - settings.h - (48 * 1) * config->dpi;
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
    
    if (auto app_menu = client_by_name(app, "app_menu")) {
        PopupSettings popup_settings;
        popup_settings.takes_input_focus = true;
        
        auto popup = app_menu->create_popup(popup_settings, settings);
        popup->name = "power_popup";
        
        popup->root->when_paint = paint_power_menu;
        popup->root->type = vbox;
        popup->root->spacing = 1;
        popup->root->wanted_pad.y = 8 * config->dpi;
        popup->root->wanted_pad.h = 8 * config->dpi;
    
        auto l = popup->root->child(FILL_SPACE, FILL_SPACE);
        l->when_clicked = clicked_logoff;
        l->when_paint = paint_button;
        auto *l_data = new ButtonData;
        l_data->text = "Sign Out";
        l->user_data = l_data;
    
        auto b = popup->root->child(FILL_SPACE, FILL_SPACE);
        b->when_clicked = clicked_shut_down;
        b->when_paint = paint_button;
        auto *b_data = new ButtonData;
        b_data->text = "Shut Down";
        b->user_data = b_data;
        
        auto c = popup->root->child(FILL_SPACE, FILL_SPACE);
        c->when_clicked = clicked_restart;
        c->when_paint = paint_button;
        auto *c_data = new ButtonData;
        c_data->text = "Restart";
        c->user_data = c_data;
        
        auto d = popup->root->child(FILL_SPACE, FILL_SPACE);
        d->when_clicked = clicked_reload;
        d->when_paint = paint_button;
        auto *d_data = new ButtonData;
        d_data->text = "Reload";
        d->user_data = d_data;
        
        auto a = popup->root->child(FILL_SPACE, FILL_SPACE);
        a->when_clicked = clicked_off;
        a->when_paint = paint_button;
        auto *a_data = new ButtonData;
        a_data->text = "Off";
        a->user_data = a_data;
        
        popup->when_closed = sub_menu_closed;
        
        client_show(app, popup);
        
        xcb_set_input_focus(app->connection, XCB_NONE, popup->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}


static void
paint_grid_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = (ButtonData *) container->user_data;
    {
        auto default_color = config->color_taskbar_button_default;
        auto hovered_color = config->color_taskbar_button_hovered;
        auto pressed_color = config->color_taskbar_button_pressed;
        
        auto e = getEasingFunction(easing_functions::EaseOutQuad);
        double time = 0;
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                if (data->previous_state != 2) {
                    time = 40;
                    data->previous_state = 2;
                    client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e,
                                            pressed_color.r);
                    client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e,
                                            pressed_color.g);
                    client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e,
                                            pressed_color.b);
                    client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e,
                                            pressed_color.a);
                }
            } else if (data->previous_state != 1) {
                time = 70;
                data->previous_state = 1;
                client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, hovered_color.r);
                client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, hovered_color.g);
                client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, hovered_color.b);
                client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, hovered_color.a);
            }
        } else if (data->previous_state != 0) {
            time = 100;
            data->previous_state = 0;
            e = getEasingFunction(easing_functions::EaseInCirc);
            client_create_animation(app, client, &data->color.r, data->color.lifetime, 0, time, e, default_color.r);
            client_create_animation(app, client, &data->color.g, data->color.lifetime, 0, time, e, default_color.g);
            client_create_animation(app, client, &data->color.b, data->color.lifetime, 0, time, e, default_color.b);
            client_create_animation(app, client, &data->color.a, data->color.lifetime, 0, time, e, default_color.a);
        }
        
        draw_margins_rect(client, data->color, container->real_bounds, 2, 0);
    }
    
    auto color = config->color_apps_text_inactive;
    if (container->interactable)
        color = config->color_apps_text;
    std::string text = data->text;
    if (text == "Recently added")
        text = "\uE823";
    color = ArgbColor(1, 1, 1, 1);
    draw_text(client, 14 * config->dpi, config->font, EXPAND(color), text, container->real_bounds);
}

void
fill_live_tiles(ScrollContainer *live_scroll) {
    for (int i = live_scroll->content->children.size() - 1; i >= 0; i--) {
        delete live_scroll->content->children[i];
    }
    live_scroll->content->children.clear();
    
    int i_off = 0;
    for (int i = 0; i < launchers.size(); i++) {
        if (!launchers[i]->get_pinned())
            continue;
        if (i_off == 0) {
            live_scroll->content->wanted_pad = Bounds(13 * config->dpi, 34 * config->dpi + 8 * config->dpi,
                                                      13 * config->dpi, 120 * config->dpi);
        }
        
        auto c = live_scroll->content;
        c->spacing = 4 * config->dpi;
        
        auto live_tile = c->child(100 * config->dpi, 100 * config->dpi);
        live_tile->when_paint = paint_live_tile;
        live_tile->when_clicked = when_live_tile_clicked;
        live_tile->when_drag = [](AppClient *client, cairo_t *, Container *c) {
            auto data = (LiveTileData *) c->user_data;
            if (auto drag = client_by_name(app, "drag_window")) {
                int global_x = 0;
                int global_y = 0;
                get_global_coordinates(app->connection, client->window, client->mouse_current_x,
                                       client->mouse_current_y, &global_x, &global_y);
                uint32_t value_list_resize[] = {
                        (uint32_t) (global_x + (c->real_bounds.x - client->mouse_initial_x)),
                        (uint32_t) (global_y + (c->real_bounds.y - client->mouse_initial_y)),
                };
                xcb_configure_window(app->connection, drag->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                     value_list_resize);
                int result_x, result_y;
                int left_edge_x = client->mouse_current_x - (client->mouse_initial_x - c->real_bounds.x);
                int top_edge_y = client->mouse_current_y - (client->mouse_initial_y - c->real_bounds.y);
                auto scroll = (ScrollContainer *) container_by_name("live_scroll", client->root);
                projected_live_tile_position_based_on_drag(left_edge_x, top_edge_y, data->launcher->info.w,
                                                           data->launcher->info.h, &result_x, &result_y,
                                                           data->launcher, scroll->scroll_v_visual, true);
                data->launcher->info.x = result_x;
                data->launcher->info.y = result_y;
            }
        };
        live_tile->when_drag_start = [](AppClient *client, cairo_t *, Container *c) {
            c->when_paint = nullptr;
            request_refresh(app, client);
            Settings settings;
            settings.force_position = true;
            settings.skip_taskbar = true;
            settings.decorations = false;
            settings.override_redirect = true;
            settings.w = 100 * config->dpi;
            settings.h = 100 * config->dpi;
            auto data = (LiveTileData *) c->user_data;
            if (data->lifetime.lock()) {
                float pad = 2 * config->dpi;
                settings.w = data->launcher->info.w * (50 * config->dpi) - pad * 2;
                settings.h = data->launcher->info.h * (50 * config->dpi) - pad * 2;
            }
            auto drag = client_new(app, settings, "drag_window");
            client_show(app, drag);
            drag->root->user_data = c->user_data;
            drag->root->when_paint = paint_live_tile;
        };
        live_tile->when_drag_end = [](AppClient *, cairo_t *, Container *c) {
            c->when_paint = paint_live_tile;
            if (auto drag = client_by_name(app, "drag_window")) {
                drag->root->user_data = nullptr; // So that we don't double delete
                client_close_threaded(app, client_by_name(app, "drag_window"));
            }
            if (auto c = client_by_name(app, "app_menu")) {
                if (auto live_scroll = container_by_name("live_scroll", c->root)) {
                    fill_live_tiles((ScrollContainer *) live_scroll);
                }
            }
        };
        live_tile->when_drag_end_is_click = false;
        live_tile->minimum_y_distance_to_move_before_drag_begins = 4 * config->dpi;
        live_tile->minimum_x_distance_to_move_before_drag_begins = 4 * config->dpi;
        LiveTileData *live_data = new LiveTileData(launchers[i]);
        live_tile->user_data = live_data;
        live_data->lifetime = live_tile->lifetime;
        live_data->tile = live_tile;
        i_off++;
        if (i_off == 3)
            i_off = 0;
    }
}

struct PaneDragData : UserData {
    bool dragging = false;
};

static void
root_dragged(AppClient *client, cairo_t *, Container *c) {
    auto data = (PaneDragData *) c->user_data;
    if (!data->dragging)
        return;
    
    int global_x = 0;
    int global_y = 0;
    get_global_coordinates(app->connection, client->window, client->mouse_current_x,
                           client->mouse_current_y, &global_x, &global_y);
    if (client->cursor_type == XC_sb_v_double_arrow) { // vertical
        if (global_y <= 100) {
            global_y = 100;
        }
        if (global_y >= client->app->screen->height_in_pixels * .55) {
            global_y = client->app->screen->height_in_pixels * .55;
        }
        uint32_t value_list_resize[] = {
                (uint32_t) (global_y),
                (uint32_t) (client->app->screen->height_in_pixels - (global_y + config->taskbar_height)),
        };
        winbar_settings->start_menu_height = value_list_resize[1] * (1 / config->dpi);
        xcb_configure_window(app->connection, client->window, XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT,
                             value_list_resize);
    } else {
        int w = 0;
        bool any_pinned = false;
        for (auto l: launchers) {
            if (l->get_pinned()) {
                any_pinned = true;
                break;
            }
        }
        if (!winbar_settings->allow_live_tiles)
            any_pinned = false;
        if (any_pinned) {
            w = 662 * config->dpi;
        } else {
            w = 320 * config->dpi;
        }
        int min_w = w;
        if (global_x - client->bounds->x >= w) {
            w = global_x - client->bounds->x;
        }
        uint32_t value_list_resize[] = {
                (uint32_t) (w),
        };
        xcb_configure_window(app->connection, client->window, XCB_CONFIG_WINDOW_WIDTH, value_list_resize);
        winbar_settings->extra_live_tile_pages = std::round((double) (w - min_w) / (342 * config->dpi));
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
    
    auto root_hbox = root->child(::hbox, FILL_SPACE, FILL_SPACE);
    root_hbox->receive_events_even_if_obstructed = true;
    root_hbox->when_mouse_motion = [](AppClient *client, cairo_t *, Container *c) {
        bool on_right_edge = bounds_contains(Bounds(c->real_bounds.x + c->real_bounds.w - 4 * config->dpi - 2,
                                                    c->real_bounds.y, 10 * config->dpi, c->real_bounds.h),
                                             client->mouse_current_x, client->mouse_current_y);
        bool on_top_edge = bounds_contains(
                Bounds(c->real_bounds.x, c->real_bounds.y - 1, c->real_bounds.w, config->dpi * 6),
                client->mouse_current_x, client->mouse_current_y);
        if (!on_right_edge && !on_top_edge) {
            if (client->cursor_type == XC_left_ptr)
                return;
            client->cursor_type = XC_left_ptr;
            set_cursor(app, app->screen, client, "left_ptr", XC_left_ptr);
        } else {
            if (on_top_edge) {
                if (client->cursor_type == XC_sb_v_double_arrow)
                    return;
                client->cursor_type = XC_sb_v_double_arrow;
                set_cursor(app, app->screen, client, "sb_v_double_arrow", XC_sb_v_double_arrow);
            } else {
                bool any_pinned = false;
                for (auto l: launchers) {
                    if (l->get_pinned()) {
                        any_pinned = true;
                        break;
                    }
                }
                if (client->cursor_type == XC_sb_h_double_arrow || !winbar_settings->allow_live_tiles || !any_pinned)
                    return;
                client->cursor_type = XC_sb_h_double_arrow;
                set_cursor(app, app->screen, client, "sb_h_double_arrow", XC_sb_h_double_arrow);
            }
        }
    };
    root_hbox->when_drag_start = [](AppClient *client, cairo_t *cr, Container *c) {
        bool on_right_edge = bounds_contains(Bounds(c->real_bounds.x + c->real_bounds.w - 4 * config->dpi - 2,
                                                    c->real_bounds.y, 10 * config->dpi, c->real_bounds.h),
                                             client->mouse_current_x, client->mouse_current_y);
        bool on_top_edge = bounds_contains(
                Bounds(c->real_bounds.x, c->real_bounds.y - 1, c->real_bounds.w, config->dpi * 6),
                client->mouse_current_x, client->mouse_current_y);
        bool any_pinned = false;
        for (auto l: launchers) {
            if (l->get_pinned()) {
                any_pinned = true;
                break;
            }
        }
        ((PaneDragData *) c->user_data)->dragging =
                (on_right_edge && winbar_settings->allow_live_tiles && any_pinned) || on_top_edge;
        if (((PaneDragData *) c->user_data)->dragging) {
            if (auto c = client_by_name(app, "app_menu")) {
                if (auto live_scroll = container_by_name("live_scroll", c->root)) {
                    ((ScrollContainer *) live_scroll)->scrollbar_openess = 0;
                    ((ScrollContainer *) live_scroll)->scrollbar_visible = 0;
                }
            }
            root_dragged(client, cr, c);
        }
    };
    root_hbox->when_drag = root_dragged;
    root_hbox->when_drag_end = [](AppClient *client, cairo_t *cr, Container *c) {
        ((PaneDragData *) c->user_data)->dragging = false;
        root_dragged(client, cr, c);
        client->cursor_type = XC_left_ptr;
        set_cursor(app, app->screen, client, "left_ptr", XC_left_ptr);

        possibly_resize_after_pin_unpin();
        request_refresh(app, client);
    };
    root_hbox->user_data = new PaneDragData;
    Container *stack = root_hbox->child(::stack, 320 * config->dpi, FILL_SPACE);
    
    // settings.w = 660 * config->dpi;
    //auto live_tile_width = (660 - 320) * config->dpi;
    auto live_tile_width = FILL_SPACE;
    Container *live_tile_root = root_hbox->child(::hbox, live_tile_width, FILL_SPACE);
    Container *resize_edge = root_hbox->child(::hbox, 2 * config->dpi, FILL_SPACE);
    
    ScrollPaneSettings tile_scroll(config->dpi);
    tile_scroll.bottom_show_amount = ScrollShow::SNever;
    ScrollContainer *live_scroll = make_newscrollpane_as_child(live_tile_root, tile_scroll);
    live_scroll->content->should_layout_children = false;
    live_scroll->content->spacing = 4 * config->dpi;
    live_scroll->name = "live_scroll";
    live_scroll->when_paint = paint_live_tile_bg;

    fill_live_tiles(live_scroll);
    
    int width = 48 * config->dpi;
    Container *left_buttons = stack->child(::vbox, width, FILL_SPACE);
    left_buttons->wanted_pad.y = 5 * config->dpi;
    left_buttons->spacing = 1 * config->dpi;
    left_buttons->name = "left_buttons";
    left_buttons->z_index++;
    left_buttons->receive_events_even_if_obstructed_by_one = true;
    left_buttons->when_paint = paint_left;
    left_buttons->when_mouse_enters_container = left_open;
    left_buttons->when_mouse_leaves_container = left_close;
    left_buttons->automatically_paint_children = false;
    
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
        button->user_data = data;
    }
    auto start_filler = new Container();
    start_filler->wanted_bounds.w = FILL_SPACE;
    start_filler->wanted_bounds.h = FILL_SPACE;
    start_filler->parent = left_buttons;
    left_buttons->children.insert(left_buttons->children.begin() + 1, start_filler);
    // left_buttons->children[0]->when_clicked = left_buttons_toggle;
    
    Container *right_hbox = stack->child(::hbox, FILL_SPACE, FILL_SPACE);
    right_hbox->wanted_pad.w = 1 * config->dpi;
    right_hbox->child(width, FILL_SPACE);
    
    auto *right_content = right_hbox->child(::transition, FILL_SPACE, FILL_SPACE);
    right_content->receive_events_even_if_obstructed = true;
    right_content->name = "right_content";
    right_content->handles_pierced = right_content_handles_pierced;
    
    auto *app_list_container = right_content->child(FILL_SPACE, FILL_SPACE);
    app_list_container->name = "app_list_container";
    
    auto *grid_container = right_content->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    grid_container->name = "grid_container";
    
    auto *grid = grid_container->child(layout_type::hbox, FILL_SPACE, FILL_SPACE);
    grid->child(FILL_SPACE, FILL_SPACE);
    grid->alignment = ALIGN_CENTER;
    
    int pad = 4;
    auto *g = grid->child(layout_type::vbox, (48 * 4 + (pad * 3)) * config->dpi, (48 * 8 + (pad * 7)) * config->dpi);
    g->spacing = pad * config->dpi;
    
    ScrollPaneSettings settings(config->dpi);
    ScrollContainer *scroll = make_newscrollpane_as_child(app_list_container, settings);
    scroll->name = "scroll_pane";
    scroll->content->wanted_pad = Bounds(13 * config->dpi, 8 * config->dpi, (settings.right_width / 2) * config->dpi,
                                         54 * config->dpi);
    
    Container *content = scroll->content;
    content->spacing = 54 * config->dpi;
    client_create_animation(app, client, &content->spacing, content->lifetime, 0, 130, nullptr, 2 * config->dpi, true);

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
                title->wanted_bounds.h = 34 * config->dpi;
                title->when_paint = paint_item_title;
                title->when_clicked = clicked_title;
    
                title->user_data = data;
                title->name = data->text;
            }
        } else if (previous_priority != l->app_menu_priority) {
            previous_priority = l->app_menu_priority;
            
            auto *title = new Container();
            title->parent = content;
            content->children.push_back(title);
            
            title->wanted_bounds.w = FILL_SPACE;
            title->wanted_bounds.h = 34 * config->dpi;
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
            title->name = data->text;
        }
        
        auto *child = content->child(FILL_SPACE, 36 * config->dpi);
        auto *data = new ItemData;
        data->launcher = l;
        child->user_data = data;
        child->when_paint = paint_item;
        child->when_clicked = clicked_item;
        child->name = l->name;
        child->when_drag_end_is_click = false;
        child->minimum_x_distance_to_move_before_drag_begins = 4 * config->dpi;
        child->minimum_y_distance_to_move_before_drag_begins = 4 * config->dpi;

        child->when_drag = [](AppClient *client, cairo_t *cr, Container *c) {
            if (auto drag = client_by_name(app, "drag_window")) {
                auto *data = (ItemDataWithClickOffset *) drag->root->user_data;
                int global_x = 0;
                int global_y = 0;
                get_global_coordinates(app->connection, client->window, client->mouse_current_x,
                                        client->mouse_current_y, &global_x, &global_y);
                uint32_t value_list_resize[] = {
                        (uint32_t) (global_x - data->x_off),
                        (uint32_t) (global_y - data->y_off),
                };
                xcb_configure_window(app->connection, drag->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                     value_list_resize);
            }
        };
        child->when_drag_start = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (ItemData *) c->user_data;
            if (data->launcher->get_pinned()) { // Don't start a drag, if it's already pinned
                return;
            }
            c->when_paint = nullptr;
            request_refresh(app, client);
            Settings settings;
            settings.force_position = true;
            settings.skip_taskbar = true;
            settings.decorations = false;
            settings.override_redirect = true;
            settings.w = 100 * config->dpi;
            settings.h = 100 * config->dpi;
            float pad = 2 * config->dpi;
            data->launcher->info.w = 2;
            data->launcher->info.h = 2;
            settings.w = data->launcher->info.w * (50 * config->dpi) - pad * 2;
            settings.h = data->launcher->info.h * (50 * config->dpi) - pad * 2;
            auto drag = client_new(app, settings, "drag_window");
            client_show(app, drag);
            auto click_off_data = new ItemDataWithClickOffset(data);
            click_off_data->x_off =
                    ((client->mouse_initial_x - c->real_bounds.x) / c->real_bounds.w) * (100 * config->dpi);
            click_off_data->y_off =
                    ((client->mouse_initial_y - c->real_bounds.y) / c->real_bounds.h) * (100 * config->dpi);
            drag->root->user_data = click_off_data;
            drag->root->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                auto *data = (ItemDataWithClickOffset *) c->user_data;
                int size = 36 * config->dpi;
                if (data->surface == nullptr) {
                    data->surface = accelerated_surface(app, client, size, size);
                    delete data->gsurf;
                    data->gsurf = new gl_surface;
                    
                    std::vector<IconTarget> targets;
                    targets.emplace_back(data->item_data->launcher->icon);
                    search_icons(targets);
                    pick_best(targets, size);
                    for (const auto &item: targets[0].candidates) {
                        paint_surface_with_image(data->surface, item.full_path(), size, nullptr);
                        break;
                    }
                }
                paint_live_tile_data(client, cr, c, data->surface, data->gsurf, data->item_data->launcher);
            };
        };

	    child->when_drag_end = [](AppClient *client, cairo_t *cr, Container *c) {
            if (auto drag = client_by_name(app, "drag_window")) {
                auto data = (ItemDataWithClickOffset *) drag->root->user_data;
                
                if (!data->item_data->launcher->get_pinned()) {
                    int result_x, result_y;
                    int left_edge_x = client->mouse_current_x - data->x_off;
                    int top_edge_y = client->mouse_current_y - data->y_off;
                    auto scroll = (ScrollContainer *) container_by_name("live_scroll", client->root);
                    projected_live_tile_position_based_on_drag(left_edge_x, top_edge_y,
                                                               data->item_data->launcher->info.w,
                                                               data->item_data->launcher->info.h, &result_x, &result_y,
                                                               data->item_data->launcher, scroll->scroll_v_visual,true);
                    data->item_data->launcher->info.x = result_x;
                    data->item_data->launcher->info.y = result_y;
                    if (auto c = client_by_name(app, "app_menu")) {
                        if (auto live_scroll = container_by_name("live_scroll", c->root)) {
                            fill_live_tiles((ScrollContainer *) live_scroll);
                        }
                    }
                }
                c->when_paint = paint_item;
                client_close_threaded(app, client_by_name(app, "drag_window"));
            }
        };
    }
    
    int count = 0;
    for (int y = 0; y < 8; y++) {
        auto hbox = g->child(layout_type::hbox, ((48 * 8) + (pad * 7)) * config->dpi, 48 * config->dpi);
        hbox->spacing = pad;
        for (int x = 0; x < 4; x++) {
            count++;
            if (count > 8 * 4 - 3) {
                break;
            }
            auto c = hbox->child(48 * config->dpi, 48 * config->dpi);
            c->when_paint = paint_grid_item;
            c->when_clicked = clicked_grid;
            auto data = new ButtonData;
            c->user_data = data;
            if (count == 1) { // Recent
                if (!container_by_name("Recently added", root)) {
                    c->interactable = false;
                }
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
}

void
load_live_tiles() {
    char *home = getenv("HOME");
    std::string path = std::string(home) + "/.config/winbar/live_tiles.data";
    std::ifstream input_file(path);
    
    if (input_file.good()) {
        std::string line;
        bool first_line = true;
        int i = 0;
        bool load_pre_version_1 = false;
        Launcher *target = nullptr;
        while (std::getline(input_file, line)) {
            if (first_line) {
                if (line != "#version 1") {
                    load_pre_version_1 = true;
                }
                first_line = false;
                continue;
            }
            if (i == 0) {
                for (auto item: launchers) {
                    if (item->full_path == line) {
                        item->set_pinned(true);
                        target = item;
                    }
                }
                if (load_pre_version_1 && target) {
                    target->info.w = 2;
                    target->info.h = 2;
                    i = 0;
                    target = nullptr;
                    continue;
                }
            } else if (i == 1 && target) {
                target->info.page = std::atoi(line.c_str());
            } else if (i == 2 && target) {
                target->info.w = std::atoi(line.c_str());
            } else if (i == 3 && target) {
                target->info.h = std::atoi(line.c_str());
            } else if (i == 4 && target) {
                target->info.x = std::atoi(line.c_str());
            } else if (i == 5 && target) {
                target->info.y = std::atoi(line.c_str());
            }
            
            if (i == 5) {
                i = -1;
                target = nullptr;
            }
            
            i++;
        }
    }
}

void
save_live_tiles() {
    try {
        char *home = getenv("HOME");
        std::string path = std::string(home) + "/.config/winbar/live_tiles.data";
        std::ofstream out_file(path);
        
        out_file << "#version 1" << '\n';
        for (auto item: launchers) {
            if (item->get_pinned()) {
                out_file << item->full_path;
                out_file << '\n';
                out_file << item->info.page;
                out_file << '\n';
                out_file << item->info.w;
                out_file << '\n';
                out_file << item->info.h;
                out_file << '\n';
                out_file << item->info.x;
                out_file << '\n';
                out_file << item->info.y;
                out_file << '\n';
            }
        }
    } catch (...) {
    
    }
}

static void
app_menu_closed(AppClient *client) {
    if (auto c = client_by_name(app, "tooltip_popup"))
        client_close_threaded(app, c);
    if (auto c = client_by_name(app, "power_popup"))
        client_close_threaded(app, c);
    if (auto c = client_by_name(app, "right_click_popup"))
        client_close_threaded(app, c);
    if (auto c = client_by_name(app, "live_tile_right_click_menu"))
        client_close_threaded(app, c);
    if (auto c = client_by_name(app, "right_click_resize_popup"))
        client_close_threaded(app, c);
     if (auto c = client_by_name(app, "drag_window")) {
        c->root->user_data = nullptr;
        client_close_threaded(app, c);
     }
    left_open_fd = nullptr;
    set_textarea_inactive();
    save_settings_file();
    save_live_tiles();
}

static void
paint_desktop_files() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
//    std::lock_guard m(app->running_mutex); // No one is allowed to stop Winbar until this function finishes
    
    std::vector<IconTarget> targets;
    for (auto *launcher: launchers) {
        if (!launcher->icon.empty() && launcher->icon[0] != '/') {
            if (!has_options(launcher->icon))
                launcher->icon = "";
        }
    
        if (launcher->icon.empty() && !launcher->name.empty()) {
            if (has_options(launcher->name))
                launcher->icon = launcher->name;
        }
    
        if (launcher->icon.empty() && !launcher->wmclass.empty()) {
            if (has_options(launcher->wmclass))
                launcher->icon = launcher->wmclass;
        }
    
        if (launcher->icon.empty() && !launcher->exec.empty()) {
            if (has_options(launcher->exec))
                launcher->icon = launcher->exec;
        }
    
        if (!launcher->icon.empty()) {
            targets.emplace_back(IconTarget(launcher->icon, launcher));
        }
    }
    
    search_icons(targets);
    pick_best(targets, 32 * config->dpi);
    
    for (const auto &t: targets) {
        if (t.user_data) {
            auto launcher = (Launcher *) t.user_data;
            launcher->icon_16__ = accelerated_surface(app, client_by_name(app, "taskbar"), 16 * config->dpi,
                                                    16 * config->dpi);
            launcher->icon_24__ = accelerated_surface(app, client_by_name(app, "taskbar"), 24 * config->dpi,
                                                    24 * config->dpi);
            launcher->icon_32__ = accelerated_surface(app, client_by_name(app, "taskbar"), 32 * config->dpi,
                                                    32 * config->dpi);
            launcher->icon_48__ = accelerated_surface(app, client_by_name(app, "taskbar"), 48 * config->dpi,
                                                    48 * config->dpi);
            launcher->icon_64__ = accelerated_surface(app, client_by_name(app, "taskbar"), 64 * config->dpi,
                                                    64 * config->dpi);
            
            std::string path16;
            std::string path24;
            std::string path32;
            std::string path48;
            std::string path64;
            
            if (!launcher->icon.empty()) {
                if (launcher->icon[0] == '/') {
                    path16 = launcher->icon;
                    path24 = launcher->icon;
                    path32 = launcher->icon;
                    path48 = launcher->icon;
                    path64 = launcher->icon;
                } else {
                    if (!t.candidates.empty()) {
                        path16 = t.candidates[0].full_path();
                        path24  = t.candidates[0].full_path();
                        path32 = t.candidates[0].full_path();
                        path48 = t.candidates[0].full_path();
                        path64 = t.candidates[0].full_path();
                    }
//                    for (const auto &icon: t.candidates) {
//                        if (!path16.empty() && !path24.empty() && !path32.empty() && !path48.empty() && !path64.empty())
//                            break;
//                        if ((icon.size == 16) && path16.empty()) {
//                            path16 = icon.full_path();
//                        } else if (icon.size == 24 && path24.empty()) {
//                            path24 = icon.full_path();
//                        } else if (icon.size == 32 && path32.empty()) {
//                            path32 = icon.full_path();
//                        } else if (icon.size == 48 && path48.empty()) {
//                            path48 = icon.full_path();
//                        } else if (icon.size == 64 && path64.empty()) {
//                            path64 = icon.full_path();
//                        }
//                    }
//                    for (const auto &icon: t.candidates) {
//                        if (icon.extension == 2) {
//                            if (path16.empty())
//                                path16 = icon.full_path();
//                            if (path24.empty())
//                                path24 = icon.full_path();
//                            if (path32.empty())
//                                path32 = icon.full_path();
//                            if (path48.empty())
//                                path48 = icon.full_path();
//                            if (path64.empty())
//                                path64 = icon.full_path();
//                            break;
//                        }
//                    }
//                    for (const auto &icon: t.candidates) {
//                        if (!path16.empty() && !path24.empty() && !path32.empty() && !path48.empty() && !path64.empty())
//                            break;
//                        if (path16.empty())
//                            path16 = icon.full_path();
//                        if (path24.empty())
//                            path24 = icon.full_path();
//                        if (path32.empty())
//                            path32 = icon.full_path();
//                        if (path48.empty())
//                            path48 = icon.full_path();
//                        if (path64.empty())
//                            path64 = icon.full_path();
//                    }
                }
            }
            
            if (!path16.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_16__, path16, 16 * config->dpi, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_16__, as_resource_path("unknown-16.svg"), 16 * config->dpi, nullptr);
            }
            
            if (!path24.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_24__, path24, 24 * config->dpi, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_24__, as_resource_path("unknown-24.svg"), 24 * config->dpi, nullptr);
            }
            
            if (!path32.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_32__, path32, 32 * config->dpi, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_32__, as_resource_path("unknown-32.svg"), 32 * config->dpi, nullptr);
            }
            
            if (!path48.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_48__, path48, 48 * config->dpi, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_48__, as_resource_path("unknown-32.svg"), 48 * config->dpi, nullptr);
            }
            
            if (!path64.empty() && !launcher->icon.empty()) {
                paint_surface_with_image(launcher->icon_64__, path64, 64 * config->dpi, nullptr);
            } else {
                paint_surface_with_image(
                        launcher->icon_64__, as_resource_path("unknown-64.svg"), 64 * config->dpi, nullptr);
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
    
    if (!directory.empty())
        if (directory[directory.size() - 1] != '/')
            directory += '/';
    try {
        for (const auto &entry: std::filesystem::recursive_directory_iterator(directory,
                                                                              std::filesystem::directory_options::follow_directory_symlink)) {
            if (!entry.is_regular_file())
                continue;
            
            const std::string &filename = entry.path().filename().string();
            if (!ends_with(filename.c_str(), ".desktop")) {
                continue;
            }
            std::string path = entry.path().string();

            // Parse desktop file
            INIReader desktop_application(path);
            if (desktop_application.ParseError() != 0) {
                continue;
            }
            
            std::string name = desktop_application.Get("Desktop Entry", "Name", "");
            std::string wmclass = desktop_application.Get("Desktop Entry", "StartupWMClass", "");
            std::string exec = desktop_application.Get("Desktop Entry", "Exec", "");
            std::string keywords = desktop_application.Get("Desktop Entry", "Keywords", "");
            std::string categories = desktop_application.Get("Desktop Entry", "Categories", "");
            std::string generic_name = desktop_application.Get("Desktop Entry", "GenericName", "");
            std::string icon = desktop_application.Get("Desktop Entry", "Icon", "");
            std::string no_display_text = desktop_application.Get("Desktop Entry", "NoDisplay", "");
            std::string not_show_in = desktop_application.Get("Desktop Entry", "NotShowIn", "");
            std::string only_show_in = desktop_application.Get("Desktop Entry", "OnlyShowIn", "");
            bool no_display = false;
            std::transform(no_display_text.begin(), no_display_text.end(), no_display_text.begin(), ::tolower);
            
            if (no_display_text == "true") {
                if (app->on_kde) {
                    no_display = !starts_with(filename, "kcm_");
                } else {
                    no_display = true;
                }
            }
            
            if (exec.empty() || no_display || keywords.find("lsp-plugins") !=
                                              std::string::npos) // If we find no exec entry then there's nothing to run
                continue;
            
            if (!current_desktop.empty() && !winbar_settings->ignore_only_show_in) {
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
            launcher->full_path = path;
            launcher->name = name;
            launcher->lowercase_name = launcher->name;
            if (!keywords.empty()) {
                std::stringstream ss(keywords);
                std::string item;
                while (std::getline(ss, item, ';')) {
                    if (!item.empty()) { // Skip empty tokens if any
                        std::transform(item.begin(), item.end(), item.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        launcher->keywords.push_back(item);
                    }
                }
            }
            if (!categories.empty()) {
                std::stringstream ss(categories);
                std::string item;
                while (std::getline(ss, item, ';')) {
                    if (!item.empty()) { // Skip empty tokens if any
                        std::transform(item.begin(), item.end(), item.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        launcher->categories.push_back(item);
                    }
                }
            }
            if (!generic_name.empty()) {
                std::transform(generic_name.begin(), generic_name.end(), generic_name.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                launcher->generic_name = generic_name;
            }
            
            std::transform(launcher->lowercase_name.begin(),
                           launcher->lowercase_name.end(),
                           launcher->lowercase_name.begin(),
                           ::tolower);
            launcher->exec = exec;
            launcher->wmclass = wmclass;
            launcher->launcher_name = name;
            launcher->icon = icon;
            struct stat buffer{};
            if (stat(path.c_str(), &buffer) != 0) {
                delete launcher;
                continue;
            }
            launcher->time_modified = buffer.st_mtim.tv_sec;
            
            launchers.push_back(launcher);
        }
    } catch (const std::filesystem::filesystem_error &e) {
    
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
    
    std::string local_desktop_files = getenv("HOME");
    local_desktop_files += "/.local/share/applications/";
    std::string local_flatpak_files = getenv("HOME");
    local_flatpak_files += "/.local/share/flatpak/exports/share/applications/";
    
    if (!winbar_settings->custom_desktops_directory.empty())
        load_desktop_files(winbar_settings->custom_desktops_directory);
    if (winbar_settings->custom_desktops_directory_exclusive)
        goto skip;
    load_desktop_files("/usr/share/applications/");
    load_desktop_files(local_desktop_files);
    load_desktop_files("/var/lib/flatpak/exports/share/applications/");
    load_desktop_files(local_flatpak_files);
    skip:
    
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

void start_app_menu(bool autoclose) {
    left_locked = false;
    if (auto *c = client_by_name(app, "search_menu")) {
        client_close(app, c);
    }
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            app_timeout_stop(app, client, text_data->state->cursor_blink);
            delete text_data->state;
            text_data->state = new TextState;
            blink_on(app, client, container);
        }
        request_refresh(client->app, client);
    }
    
    Settings settings;
    settings.force_position = true;
	settings.w = app_menu_width();
    settings.h = winbar_settings->start_menu_height * config->dpi;
    settings.x = app->bounds.x;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *super = container_by_name("super", taskbar->root);
        auto field_search = container_by_name("field_search", taskbar->root);
        if (super->exists) {
            settings.x = taskbar->bounds->x + super->real_bounds.x;
        } else if (field_search->exists) {
            settings.x = taskbar->bounds->x + field_search->real_bounds.x;
        } else {
            settings.x = taskbar->bounds->x;
        }
        // Make sure doesn't go off-screen right side
        if (taskbar->screen_information) {
            if (settings.x + settings.w > taskbar->screen_information->width_in_pixels) {
                settings.x = taskbar->screen_information->width_in_pixels - settings.w;
            }
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.override_redirect = true;
    if (app->wayland)
        settings.override_redirect = false;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "app_menu";
        popup_settings.takes_input_focus = true;
        auto client = taskbar->create_popup(popup_settings, settings);
        client->limit_fps = false;
        
        client->when_closed = app_menu_closed;
        if (winbar_settings->open_start_menu_on_bottom_left_hover &&
            (autoclose && winbar_settings->autoclose_start_menu_if_hover_opened)) {
            app_timeout_create(app, client, 100, [](App *app, AppClient *, Timeout *timeout, void *) {
                timeout->keep_running = true;
                auto *app_menu = client_by_name(app, "app_menu");
                auto *taskbar = client_by_name(app, "taskbar");
                if (!taskbar || !app_menu) {
                    client_close_threaded(app, app_menu);
                    timeout->keep_running = false;
                    return;
                }
                bool in_taskbar = bounds_contains(Bounds(0, 0, taskbar->bounds->w, taskbar->bounds->h),
                                                  taskbar->mouse_current_x, taskbar->mouse_current_y);
                bool in_start = bounds_contains(Bounds(0, 0, app_menu->bounds->w, app_menu->bounds->h),
                                                app_menu->mouse_current_x, app_menu->mouse_current_y);
                auto data = (PaneDragData *) app_menu->root->children[0]->user_data;
                
                if (!in_taskbar && !in_start && !data->dragging && !client_by_name(app, "drag_window")) {
                    client_close_threaded(app, app_menu);
                    timeout->keep_running = false;
                    return;
                }
            }, nullptr, "check_if_mouse_has_left_start_menu");
        }
        
        fill_root(client);
        client_show(app, client);
        if (winbar_settings->search_behaviour == "Default")
            set_textarea_active();
        xcb_set_input_focus(app->connection, XCB_NONE, client->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}
