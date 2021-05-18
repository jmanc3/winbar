
#include "taskbar.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "app_menu.h"
#include "battery_menu.h"
#include "components.h"
#include "config.h"
#include "date_menu.h"
#include "icons.h"
#include "main.h"
#include "pinned_icon_right_click.h"
#include "root.h"
#include "search_menu.h"
#include "systray.h"
#include "utility.h"
#include "volume_menu.h"
#include "wifi_menu.h"
#include "windows_selector.h"
#include "hsluv.h"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <cassert>
#include <pango/pangocairo.h>
#include <xcb/xproto.h>

class WorkspaceButton : public HoverableButton {
public:
    cairo_surface_t *surface = nullptr;
    cairo_surface_t *surface_hover = nullptr;

    ~WorkspaceButton() { cairo_surface_destroy(surface); }
};

static Container *active_container = nullptr;
static xcb_window_t active_window = 0;
static xcb_window_t backup_active_window = 0;

static std::string time_text("N/A");

static xcb_window_t popup_window_open = -1;

static int max_resize_attempts = 10;
static int resize_attempts = 0;

static void
paint_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_button(AppClient *client, cairo_t *cr, Container *container);

static void
paint_icon_background(AppClient *client, cairo_t *cr, Container *container);

static void
paint_date(AppClient *client, cairo_t *cr, Container *container);

static void
paint_search(AppClient *client, cairo_t *cr, Container *container);

static void
fill_root(Container *root);

static void
late_classes_update(App *app, AppClient *client, void *data);

static void
paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_taskbar_background));
    cairo_fill(cr);
}

static void
paint_hoverable_button_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    HoverableButton *data = (HoverableButton *) container->user_data;

    auto default_color = config->color_taskbar_button_default;
    auto hovered_color = config->color_taskbar_button_hovered;
    auto pressed_color = config->color_taskbar_button_pressed;

    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            if (data->previous_state != 2) {
                data->previous_state = 2;
                client_create_animation(app, client, &data->color.r, 10, nullptr, pressed_color.r);
                client_create_animation(app, client, &data->color.g, 10, nullptr, pressed_color.g);
                client_create_animation(app, client, &data->color.b, 10, nullptr, pressed_color.b);
                client_create_animation(app, client, &data->color.a, 10, nullptr, pressed_color.a);
            }
        } else if (data->previous_state != 1) {
            data->previous_state = 1;
            client_create_animation(app, client, &data->color.r, 20, nullptr, hovered_color.r);
            client_create_animation(app, client, &data->color.g, 20, nullptr, hovered_color.g);
            client_create_animation(app, client, &data->color.b, 20, nullptr, hovered_color.b);
            client_create_animation(app, client, &data->color.a, 20, nullptr, hovered_color.a);
        }
    } else if (data->previous_state != 0) {
        data->previous_state = 0;
        client_create_animation(app, client, &data->color.r, 40, nullptr, default_color.r);
        client_create_animation(app, client, &data->color.g, 40, nullptr, default_color.g);
        client_create_animation(app, client, &data->color.b, 40, nullptr, default_color.b);
        client_create_animation(app, client, &data->color.a, 40, nullptr, default_color.a);
    }

    set_argb(cr, data->color);

    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y,
                    container->real_bounds.w,
                    container->real_bounds.h);
    cairo_fill(cr);
}

static void
paint_super(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    IconButton *data = (IconButton *) container->user_data;

    paint_hoverable_button_background(client, cr, container);

    if (data->surface) {
        // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
        if (container->state.mouse_pressing) {
            dye_surface(data->surface, config->color_taskbar_windows_button_pressed_icon);
        } else if (container->state.mouse_hovering) {
            dye_surface(data->surface, config->color_taskbar_windows_button_hovered_icon);
        } else {
            dye_surface(data->surface, config->color_taskbar_windows_button_default_icon);
        }

        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 8),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
        cairo_paint(cr);
    }
}

static void
paint_volume(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client, cr, container);
    container->real_bounds = start;

    auto surfaces = (volume_surfaces *) container->user_data;

    if (surfaces->mute == nullptr || surfaces->high == nullptr || surfaces->low == nullptr ||
        surfaces->medium == nullptr)
        return;

    int val = 100;
    bool mute_state = false;
    if (!audio_outputs.empty()) {
        double scalar = ((double) audio_outputs[0]->volume.values[0]) / ((double) 65535);
        val = (int) (scalar * 100);
        mute_state = audio_outputs[0]->mute_state;
    }

    cairo_surface_t *surface = nullptr;
    if (mute_state) {
        surface = surfaces->mute;
    } else if (val == 0) {
        surface = surfaces->none;
    } else if (val < 33) {
        surface = surfaces->low;
    } else if (val < 66) {
        surface = surfaces->medium;
    } else {
        surface = surfaces->high;
    }

    dye_surface(surface, config->color_taskbar_button_icons);

    cairo_set_source_surface(cr,
                             surface,
                             (int) (container->real_bounds.x + container->real_bounds.w / 2 -
                                    cairo_image_surface_get_width(surface) / 2),
                             (int) (container->real_bounds.y + container->real_bounds.h / 2 -
                                    cairo_image_surface_get_height(surface) / 2));
    cairo_paint(cr);
}

static void
paint_workspace(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    WorkspaceButton *data = (WorkspaceButton *) container->user_data;

    paint_hoverable_button_background(client, cr, container);

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (data->surface_hover) {
            dye_surface(data->surface_hover, config->color_taskbar_button_icons);
            // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
            cairo_set_source_surface(
                    cr,
                    data->surface_hover,
                    (int) (container->real_bounds.x + container->real_bounds.w / 2 - 8),
                    (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
            cairo_paint(cr);
        }
    } else {
        if (data->surface) {
            dye_surface(data->surface, config->color_taskbar_button_icons);
            // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
            cairo_set_source_surface(
                    cr,
                    data->surface,
                    (int) (container->real_bounds.x + container->real_bounds.w / 2 - 8),
                    (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
            cairo_paint(cr);
        }
    }
}

static void
paint_double_bar(cairo_t *cr,
                 Container *container,
                 ArgbColor bar_l_c,
                 ArgbColor bar_m_c,
                 ArgbColor bar_r_c) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    LaunchableButton *data = (LaunchableButton *) container->user_data;

    double bar_amount = std::max(data->hover_amount, data->active_amount);
    if (data->type != selector_type::CLOSED) {
        bar_amount = 1;
    }
    double bar_inset = 4 * (1 - bar_amount);
    double bar_right = 4 + (4 * (1 - bar_amount));

    Bounds bar_rect = Bounds(container->real_bounds.x + bar_inset,
                             container->real_bounds.y + container->real_bounds.h - 2,
                             container->real_bounds.w - bar_inset * 2,
                             2);

    set_argb(cr, bar_r_c);
    set_rect(cr, bar_rect);
    cairo_fill(cr);

    bar_rect.w -= (bar_right - 1);

    set_argb(cr, bar_m_c);
    set_rect(cr, bar_rect);
    cairo_fill(cr);

    bar_rect.w -= 1;

    set_argb(cr, bar_l_c);
    set_rect(cr, bar_rect);
    cairo_fill(cr);
}

static void
paint_double_bg_with_opacity(cairo_t *cr, Bounds bounds, ArgbColor bg_l_c, ArgbColor bg_m_c, ArgbColor bg_r_c,
                             double opacity) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    set_rect(cr, bounds);
    ArgbColor r = bg_r_c;
    set_argb(cr, r);
    cairo_fill(cr);

    bounds.w -= 3;

    set_rect(cr, bounds);
    ArgbColor m = bg_m_c;
    set_argb(cr, m);
    cairo_fill(cr);

    bounds.w -= 1;

    set_rect(cr, bounds);
    ArgbColor l = bg_l_c;
    set_argb(cr, l);
    cairo_fill(cr);
}

static void
paint_double_bg(cairo_t *cr, Bounds bounds, ArgbColor bg_l_c, ArgbColor bg_m_c, ArgbColor bg_r_c) {
    paint_double_bg_with_opacity(cr, bounds, bg_l_c, bg_m_c, bg_r_c, 1);
}

static void
paint_icon_surface(AppClient *client, cairo_t *cr, Container *container) {
    LaunchableButton *data = (LaunchableButton *) container->user_data;

    if (data->surface) {
        // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 12),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 12));
        cairo_paint(cr);
    }
}

static void
paint_icon_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    // This is the real underlying color
    ArgbColor real = config->color_taskbar_application_icons_background;
    // is_light_theme determines if generated secondary colors should go up or down in brightness
    bool is_light_theme = false;
    {
        double h; // hue
        double s; // saturation
        double p; // perceived brightness
        rgb2hsluv(real.r, real.g, real.b, &h, &s, &p);
        is_light_theme = p > 50; // if the perceived perceived brightness is greater than that we are a light theme
    }

    double r_c = real.r;
    double g_c = real.g;
    double b_c = real.b;

    int windows_count = data->windows_data_list.size();
    bool active = active_container == container || (data->type == selector_type::OPEN_CLICKED);
    bool pressed = container->state.mouse_pressing;
    bool hovered = container->state.mouse_hovering || (data->type != selector_type::CLOSED);
    bool dragging = container->state.mouse_dragging;
    double active_amount = data->active_amount;
    if (data->type == selector_type::OPEN_CLICKED) active_amount = 1;

    int highlight_height = 2;

    double bar_amount = std::max(data->hover_amount, active_amount);
    if (data->type != selector_type::CLOSED) {
        bar_amount = 1;
    }
    double highlight_inset = 4 * (1 - bar_amount);

    double bg_openess = highlight_inset;
    double right_size = 0;

    ArgbColor c = config->color_taskbar_application_icons_accent;
    ArgbColor bottom_bar_left = c;
    ArgbColor bottom_bar_middle = c;
    darken(&bottom_bar_middle, 20);
    ArgbColor bottom_bar_right = c;
    darken(&bottom_bar_right, 15);

    // The pinned icon is composed of three sections;
    // The background pane, the foreground pane, and the accent bar.
    //

    // The following colors are used on the accent bar
    ArgbColor color_accent_bar_left = config->color_taskbar_application_icons_accent;
    ArgbColor color_accent_bar_middle = darken(config->color_taskbar_application_icons_accent, 20);
    ArgbColor color_accent_bar_right = darken(config->color_taskbar_application_icons_accent, 15);

    ArgbColor original_color_taskbar_application_icons_background = config->color_taskbar_application_icons_background;

    if (screen_has_transparency(app)) {
        config->color_taskbar_application_icons_background.a = config->color_taskbar_background.a;
    }

    // The following colors are used for the background pane
    ArgbColor color_background_pane_hovered_left = darken(config->color_taskbar_application_icons_background, 15);
    ArgbColor color_background_pane_hovered_middle = darken(config->color_taskbar_application_icons_background, 22);
    ArgbColor color_background_pane_hovered_right = darken(config->color_taskbar_application_icons_background, 17);

    ArgbColor color_background_pane_pressed_left = darken(config->color_taskbar_application_icons_background, 20);
    ArgbColor color_background_pane_pressed_middle = darken(config->color_taskbar_application_icons_background, 27);
    ArgbColor color_background_pane_pressed_right = darken(config->color_taskbar_application_icons_background, 22);

    // The following colors are used for the foreground pane
    ArgbColor color_foreground_pane_default_left = darken(config->color_taskbar_application_icons_background, 10);
    ArgbColor color_foreground_pane_default_middle = darken(config->color_taskbar_application_icons_background, 17);
    ArgbColor color_foreground_pane_default_right = darken(config->color_taskbar_application_icons_background, 12);

    ArgbColor color_foreground_pane_hovered_left = darken(config->color_taskbar_application_icons_background, 0);
    ArgbColor color_foreground_pane_hovered_middle = darken(config->color_taskbar_application_icons_background, 7);
    ArgbColor color_foreground_pane_hovered_right = darken(config->color_taskbar_application_icons_background, 2);

    ArgbColor color_foreground_pane_pressed_left = darken(config->color_taskbar_application_icons_background, 0 + 2);
    ArgbColor color_foreground_pane_pressed_middle = darken(config->color_taskbar_application_icons_background, 7 + 2);
    ArgbColor color_foreground_pane_pressed_right = darken(config->color_taskbar_application_icons_background, 2 + 2);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    { // Background pane
        if (windows_count > 1) {
            if (pressed || hovered) {
                if (pressed) {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_middle,
                                    color_background_pane_pressed_right);
                } else {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_middle,
                                    color_background_pane_hovered_right);
                }
            }
        } else {
            if (pressed || hovered) {
                if (pressed) {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_left,
                                    color_background_pane_pressed_left);
                } else {
                    paint_double_bg(cr,
                                    container->real_bounds,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_left,
                                    color_background_pane_hovered_left);
                }
            }
        }
    }

    { // Foreground pane
        // make the bounds
        double back_x = container->real_bounds.x + highlight_inset;
        double back_w = container->real_bounds.w - highlight_inset * 2;

        if (active_amount) {
            double height = container->real_bounds.h * active_amount;
            Bounds bounds = Bounds(back_x, container->real_bounds.y + container->real_bounds.h - height, back_w,
                                   height);

            if (windows_count > 1) {
                if (pressed || hovered) {
                    if (pressed) {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_middle,
                                        color_foreground_pane_pressed_right);
                    } else {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_middle,
                                        color_foreground_pane_hovered_right);
                    }
                } else {
                    paint_double_bg(cr,
                                    bounds,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_middle,
                                    color_foreground_pane_default_right);
                }
            } else {
                if (pressed || hovered) {
                    if (pressed) {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_left,
                                        color_foreground_pane_pressed_left);
                    } else {
                        paint_double_bg(cr,
                                        bounds,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_left,
                                        color_foreground_pane_hovered_left);
                    }
                } else {
                    paint_double_bg(cr,
                                    bounds,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_left,
                                    color_foreground_pane_default_left);
                }
            }
        }
    }

    { // Accent bar
        if (windows_count > 0) {
            paint_double_bar(cr,
                             container,
                             color_accent_bar_left,
                             color_accent_bar_left,
                             color_accent_bar_left);

            // paint the right side
            if (windows_count > 1) {
                paint_double_bar(cr,
                                 container,
                                 color_accent_bar_left,
                                 color_accent_bar_middle,
                                 color_accent_bar_right);
            }
        }
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    config->color_taskbar_application_icons_background = original_color_taskbar_application_icons_background;
}

// TODO order is not correct
static void
paint_all_icons(AppClient *client_entity, cairo_t *cr, Container *container) {
    std::vector<int> render_order;
    for (int i = 0; i < container->children.size(); i++) {
        render_order.push_back(i);
    }
    std::sort(render_order.begin(), render_order.end(), [container](int a, int b) -> bool {
        return container->children[a]->z_index < container->children[b]->z_index;
    });

    for (auto index : render_order) {
        paint_icon_background(client_entity, cr, container->children[index]);
    }
    for (auto index : render_order) {
        paint_icon_surface(client_entity, cr, container->children[index]);
    }
}

static void
pinned_icon_mouse_enters(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    if (client_by_name(app, "right_click_menu") == nullptr) {
        possibly_open(app, client, container, data);
    }

    client_create_animation(app, client, &data->hover_amount, 70, 0, 1);
}

static void
pinned_icon_mouse_leaves(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;
    if (auto c = client_by_name(app, "windows_selector")) {
        possibly_close(app, c, container, data);
    } else if (data->close_timeout_fd != -1){
        app_timeout_stop(app, client, data->close_timeout_fd);
    }
    client_create_animation(app, client, &data->hover_amount, 70, 0, 0);
}

std::string
return_current_time_and_date() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%I:%M %p");
    if (config->date_single_line) {
        ss << ' ';
    } else {
        ss << '\n';
    }

    std::stringstream month;
    month << std::put_time(std::localtime(&in_time_t), "%m");

    std::string real_month;
    if (month.str().at(0) == '0') {
        real_month = month.str().erase(0, 1);
    } else {
        real_month = month.str();
    }

    std::stringstream day;
    day << std::put_time(std::localtime(&in_time_t), "%d");
    auto s = day.str();

    std::string real_day;
    if (day.str().at(0) == '0') {
        real_day = day.str().erase(0, 1);
    } else {
        real_day = day.str();
    }

    std::stringstream year;
    year << std::put_time(std::localtime(&in_time_t), "%Y");

    ss << real_month << "/" << real_day << "/" << year.str();

    return ss.str();
}

void update_time(App *app, AppClient *client, void *data) {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Time Thread");
#endif
    std::string date = return_current_time_and_date();
    if (date[0] == '0')
        date.erase(0, 1);
    if (time_text != date) {
        time_text = date;
        client_layout(app, client);
        request_refresh(app, client);
    }

    app_timeout_create(app, client, 1000, update_time, nullptr);
}

void active_window_changed(xcb_window_t new_active_window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    active_window = new_active_window;

    AppClient *entity = client_by_name(app, "taskbar");
    if (!entity)
        return;
    auto *root = entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    for (Container *icon : icons->children) {
        LaunchableButton *data = (LaunchableButton *) icon->user_data;
        if (active_container) {
            LaunchableButton *old_data = (LaunchableButton *) active_container->user_data;
            client_create_animation(app, entity, &old_data->active_amount, 45, 0, 0);
        }

        for (auto window_data : data->windows_data_list) {
            auto window = window_data->id;
            if (window == new_active_window) {
                window_data->take_screenshot();
                active_container = icon;
                client_create_animation(app, entity, &data->active_amount, 45, 0, 1);
                request_refresh(app, entity);
                return;
            }
        }
    }
}

static void
finished_icon_animation() {
    AppClient *client = client_by_name(app, "taskbar");
    Container *icons = container_by_name("icons", client->root);
    for (Container *child : icons->children) {
        auto *data = static_cast<LaunchableButton *>(child->user_data);
        if (data->animating) {
            if (data->target == child->real_bounds.x) {
                data->animating = false;
            }
        }
    }
    handle_mouse_motion(app, client, client->mouse_current_x, client->mouse_current_y);
}

static void
icons_align(AppClient *client_entity, Container *icon_container, bool all_except_dragged) {
    update_pinned_items_file();
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *icon_container_copy = new Container(*icon_container);
    icon_container_copy->should_layout_children = true;
    layout(client_entity, client_entity->cr, icon_container_copy, icon_container_copy->real_bounds);

    for (int i = 0; i < icon_container->children.size(); ++i) {
        auto *real_icon = icon_container->children[i];
        auto *real_data = static_cast<LaunchableButton *>(real_icon->user_data);
        auto *laid_icon = icon_container_copy->children[i];

        // the real icon is already in the correct position
        if (real_icon->real_bounds.x == laid_icon->real_bounds.x) {
            continue;
        }

        // we don't want to align the real_icon if its the one the user is dragging
        if (all_except_dragged && real_icon->state.mouse_dragging) {
            continue;
        }

        if (real_data->animating) {
            if (real_data->target != laid_icon->real_bounds.x) {
                client_create_animation(app,
                                        client_entity,
                                        &real_icon->real_bounds.x,
                                        100,
                                        nullptr,
                                        laid_icon->real_bounds.x,
                                        finished_icon_animation);
            }
        } else {
            real_data->animating = true;
            client_create_animation(app,
                                    client_entity,
                                    &real_icon->real_bounds.x,
                                    100,
                                    nullptr,
                                    laid_icon->real_bounds.x,
                                    finished_icon_animation);
        }
    }
}

static void
pinned_icon_drag_start(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto c : app->clients) {
        if (c->name == "windows_selector") {
            client_close_threaded(app, c);
        }
    }
    backup_active_window = active_window;
    active_window_changed(-1);
    container->parent->should_layout_children = false;
    auto *data = static_cast<LaunchableButton *>(container->user_data);
    data->initial_mouse_click_before_drag_offset_x =
            container->real_bounds.x - client_entity->mouse_initial_x;
    container->z_index = 1;

    icons_align(client_entity, container->parent, true);
}

static void
pinned_icon_drag(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = static_cast<LaunchableButton *>(container->user_data);
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x = std::max(container->parent->real_bounds.x, container->real_bounds.x);
    container->real_bounds.x =
            std::min(container->parent->real_bounds.x + container->parent->real_bounds.w -
                     container->real_bounds.w,
                     container->real_bounds.x);

    auto *copy_parent = new Container(*container->parent);
    copy_parent->should_layout_children = true;
    layout(client_entity, cr, copy_parent, copy_parent->real_bounds);

    std::vector<int> centers;
    for (auto child : copy_parent->children) {
        centers.push_back(child->real_bounds.x + child->real_bounds.w / 2);
    }
    delete copy_parent;

    int our_center = container->real_bounds.x + container->real_bounds.w / 2;

    int new_index = 0;
    int center_diff = 100000;
    for (int i = 0; i < centers.size(); i++) {
        int diff = std::abs(centers[i] - our_center);
        if (diff < center_diff) {
            new_index = i;
            center_diff = diff;
        }
    }

    for (int x = 0; x < container->parent->children.size(); x++) {
        if (container->parent->children.at(x) == container) {
            if (x == new_index) {
                return;
            }
            container->parent->children.erase(container->parent->children.begin() + x);
            break;
        }
    }
    container->parent->children.insert(container->parent->children.begin() + new_index, container);

    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x =
            client_entity->mouse_current_x + data->initial_mouse_click_before_drag_offset_x;
    container->real_bounds.x = std::max(container->parent->real_bounds.x, container->real_bounds.x);
    container->real_bounds.x =
            std::min(container->parent->real_bounds.x + container->parent->real_bounds.w -
                     container->real_bounds.w,
                     container->real_bounds.x);

    icons_align(client_entity, container->parent, true);
}

static void
pinned_icon_drag_end(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    container->parent->should_layout_children = true;
    active_window_changed(backup_active_window);
    icons_align(client_entity, container->parent, false);
    container->z_index = 0;
}

uint32_t
get_wm_state(xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_get_property_reply_t *reply;
    xcb_get_property_cookie_t cookie;
    uint32_t *statep;
    uint32_t state = 0;

    cookie = xcb_get_property(app->connection,
                              false,
                              window,
                              get_cached_atom(app, "WM_STATE"),
                              get_cached_atom(app, "WM_STATE"),
                              0,
                              sizeof(int32_t));

    reply = xcb_get_property_reply(app->connection, cookie, NULL);
    if (NULL == reply) {
        fprintf(stderr, "mcwm: Couldn't get properties for win %d\n", window);
        return -1;
    }

    /* Length is 0 if we didn't find it. */
    if (0 == xcb_get_property_value_length(reply)) {
        goto bad;
    }

    statep = static_cast<uint32_t *>(xcb_get_property_value(reply));
    state = *statep;

    bad:
    free(reply);
    return state;
}

static void
minimize_window(xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = window;
    event.type = get_cached_atom(app, "WM_CHANGE_STATE");
    event.data.data32[0] = XCB_ICCCM_WM_STATE_ICONIC;// IconicState
    event.data.data32[1] = 0;
    event.data.data32[2] = 0;
    event.data.data32[3] = 0;
    event.data.data32[4] = 0;

    xcb_send_event(app->connection,
                   0,
                   app->screen->root,
                   XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   reinterpret_cast<char *>(&event));
    xcb_flush(app->connection);
}

static void
update_minimize_icon_positions() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;
    auto *bounds = client_entity->bounds;

    for (auto icon : icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);

        if (!data)
            continue;

        for (auto window_data : data->windows_data_list) {
            auto window = window_data->id;
            double value[] = {bounds->x + icon->real_bounds.x,
                              bounds->y + icon->real_bounds.y,
                              icon->real_bounds.w,
                              icon->real_bounds.h};

            xcb_ewmh_set_wm_icon_geometry(
                    &app->ewmh, window, value[0], value[1], value[2], value[3]);
        }
    }
}

static void
scrolled_volume(AppClient *client_entity,
                cairo_t *cr,
                Container *container,
                int horizontal_scroll,
                int vertical_scroll) {
    if (!audio_connected) {
        return;
    }

    audio_all_outputs();

    if (audio_outputs.empty()) {
        return;
    }

    if (client_by_name(app, "volume") == nullptr) {
        open_volume_menu();
    }

    Audio *client = audio_outputs[0];

    pa_cvolume copy = client->volume;
    double val = client->volume.values[0];
    val += 655.35 * vertical_scroll;
    val -= 655.35 * horizontal_scroll;// we subtract to correct the direction

    if (val < 0) {
        val = 0;
    } else if (val > 65535) {
        val = 65535;
    }
    client->volume.values[0] = (int) val;

    if (client->volume.values[0] < 0) {
        client->volume.values[0] = 0;
    } else if (client->volume.values[0] > 65535) {
        client->volume.values[0] = 65535;
    }
    if (copy.values[0] != client->volume.values[0]) {
        for (int i = 0; i < client->volume.channels; i++) {
            copy.values[i] = client->volume.values[0];
        }
        if (client->mute_state) {
            client->mute_state = !client->mute_state;
            audio_set_output_mute(client->index, client->mute_state);
        }
        audio_set_output_volume(client->index, copy);
        update_taskbar_volume_icon();
    }
}

static void
pinned_icon_mouse_clicked(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    LaunchableButton *data = (LaunchableButton *) container->user_data;

    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_1) {
        if (data->windows_data_list.empty()) {
            launch_command(data->command_launched_by);
            app_timeout_stop(client->app, client, data->open_timeout_fd);
            for (auto c : app->clients) {
                if (c->name == "windows_selector") {
                    client_close(app, c);
                }
            }
        } else if (data->windows_data_list.size() > 1) {
            for (auto c : app->clients) {
                if (c->name == "windows_selector") {
                    auto pii = (PinnedIconInfo *) c->root->user_data;
                    if (pii->data->type == ::OPEN_HOVERED && data == pii->data) {
                        register_popup(c->window);
                        pii->data->type = ::OPEN_CLICKED;
                        return;
                    }
                }
            }
            start_windows_selector(container, selector_type::OPEN_CLICKED);
        } else {
            // TODO: choose window if there are more then one
            app_timeout_stop(client->app, client, data->open_timeout_fd);

            xcb_window_t window = data->windows_data_list[0]->id;
            for (auto c : app->clients) {
                if (c->name == "windows_selector") {
                    client_close(app, c);
                }
            }
            uint32_t state = get_wm_state(window);

            if (state == XCB_ICCCM_WM_STATE_NORMAL) {
                bool is_active_window = false;
                if (active_container) {
                    LaunchableButton *button_data = (LaunchableButton *) active_container->user_data;
                    if (button_data) {
                        for (auto window_data : button_data->windows_data_list) {
                            auto active_window = window_data->id;
                            if (active_window == window)
                                is_active_window = true;
                        }
                    }
                }
                if (is_active_window) {
                    update_minimize_icon_positions();
                    minimize_window(window);
                } else {
                    xcb_ewmh_request_change_active_window(&app->ewmh,
                                                          app->screen_number,
                                                          window,
                                                          XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                          XCB_CURRENT_TIME,
                                                          XCB_NONE);
                }
            } else {
                xcb_ewmh_request_change_active_window(&app->ewmh,
                                                      app->screen_number,
                                                      window,
                                                      XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                      XCB_CURRENT_TIME,
                                                      XCB_NONE);
            }
        }
    } else if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
        app_timeout_stop(client->app, client, data->open_timeout_fd);
        for (auto c : app->clients) {
            if (c->name == "windows_selector") {
                client_close(app, c);
            }
        }
        start_pinned_icon_right_click(container);
    }
}

static void
paint_minimize(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);

    Bounds bounds = container->real_bounds;
    bounds.w = 1;
    set_rect(cr, bounds);
    set_argb(cr, config->color_taskbar_minimize_line);
    cairo_fill(cr);
}

static void
paint_systray(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);

    IconButton *data = (IconButton *) container->user_data;
    if (data->surface) {
        // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
        dye_surface(data->surface, config->color_taskbar_button_icons);
        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 8),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
        cairo_paint(cr);
    }
}

static void
paint_date(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
    PangoAlignment initial_alignment = pango_layout_get_alignment(layout);
    pango_layout_set_alignment(layout, PangoAlignment::PANGO_ALIGN_CENTER);

    int width;
    int height;
    pango_layout_set_text(layout, time_text.c_str(), time_text.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    int pad = 16;
    if (container->wanted_bounds.w != width + pad) {
        container->wanted_bounds.w = width + pad;
        client_layout(app, client);
        request_refresh(app, client);
        return;
    }

    set_argb(cr, config->color_taskbar_date_time_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_alignment(layout, initial_alignment);
}

static void
clicked_date(AppClient *client, cairo_t *cr, Container *container) {
    if (config->date_command.empty()) {
        start_date_menu();
    } else {
        launch_command(config->date_command);
    }
}

static void
clicked_wifi(AppClient *client, cairo_t *cr, Container *container) {
    if (config->wifi_command.empty()) {
        start_wifi_menu();
    } else {
        launch_command(config->wifi_command);
    }
}

static void
clicked_systray(AppClient *client, cairo_t *cr, Container *container) {
    if (config->systray_command.empty()) {
        open_systray();
    } else {
        launch_command(config->systray_command);
    }
}

static void
clicked_battery(AppClient *client, cairo_t *cr, Container *container) {
    if (config->battery_command.empty()) {
        start_battery_menu();
    } else {
        launch_command(config->battery_command);
    }
}

static void
clicked_volume(AppClient *client, cairo_t *cr, Container *container) {
    if (config->volume_command.empty()) {
        open_volume_menu();
    } else {
        launch_command(config->volume_command);
    }
}

static std::vector<xcb_window_t> minimize_button_windows_order;
static bool minimize_button_hide = true;

static void
clicked_minimize(AppClient *client, cairo_t *cr, Container *container) {
    if (minimize_button_hide) {
        // Here we hide the windows
        xcb_query_tree_cookie_t cookie;
        xcb_query_tree_reply_t *reply;

        cookie = xcb_query_tree(client->app->connection, client->app->screen->root);
        if ((reply = xcb_query_tree_reply(client->app->connection, cookie, NULL))) {
            xcb_window_t *children = xcb_query_tree_children(reply);
            for (int i = 0; i < xcb_query_tree_children_length(reply); i++) {
                xcb_window_t window = children[i];
                auto *c = client_by_window(client->app, window);
                if (c == nullptr) {
                    minimize_button_windows_order.push_back(window);
                    minimize_window(window);
                }
            }

            free(reply);
        }
    } else {
        // Here we show them based on the order
        for (auto window : minimize_button_windows_order) {
            auto *c = client_by_window(client->app, window);
            if (c == nullptr) {
                xcb_ewmh_request_change_active_window(&app->ewmh,
                                                      app->screen_number,
                                                      window,
                                                      XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                      XCB_CURRENT_TIME,
                                                      XCB_NONE);
            }
        }

        minimize_button_windows_order.clear();
    }
    minimize_button_hide = !minimize_button_hide;
}

static void
clicked_search(AppClient *client, cairo_t *cr, Container *container) {
    set_textarea_active();
    if (client_by_name(app, "search_menu") == nullptr) {
        start_search_menu();
    }
}

static void
paint_search(AppClient *client, cairo_t *cr, Container *container) {
    IconButton *data = (IconButton *) container->user_data;

#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    int border_size = 1;

    bool active = false;
    bool text_empty = false;
    if (auto *con = container_by_name("main_text_area", container)) {
        auto *text_data = (TextAreaData *) con->user_data;
        text_empty = text_data->state->text.empty();
        active = con->parent->active;
    }

    if (active)
        border_size = 2;
    if (container->state.mouse_hovering)
        border_size = 2;

    // Paint border
//    set_rect(cr, container->real_bounds);
    if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
        if (active || container->state.mouse_pressing) {
            set_argb(cr, config->color_taskbar_search_bar_pressed_border);
        } else {
            set_argb(cr, config->color_taskbar_search_bar_hovered_border);
        }
    } else {
        set_argb(cr, config->color_taskbar_search_bar_default_border);
    }

    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, border_size);
    cairo_fill(cr);

    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, border_size, container->real_bounds.h);
    cairo_fill(cr);

    cairo_rectangle(cr,
                    container->real_bounds.x + container->real_bounds.w - border_size,
                    container->real_bounds.y,
                    border_size,
                    container->real_bounds.h);
    cairo_fill(cr);

    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h - border_size,
                    container->real_bounds.w,
                    border_size);
    cairo_fill(cr);

    // Paint background
    if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
        if (active || container->state.mouse_pressing) {
            set_argb(cr, config->color_taskbar_search_bar_pressed_background);
        } else {
            set_argb(cr, config->color_taskbar_search_bar_hovered_background);
        }
    } else {
        set_argb(cr, config->color_taskbar_search_bar_default_background);
    }
    cairo_rectangle(cr,
                    container->real_bounds.x + border_size,
                    container->real_bounds.y + border_size,
                    container->real_bounds.w - border_size * 2,
                    container->real_bounds.h - border_size * 2);
    cairo_fill(cr);

    // Paint search icon
    if (data->surface) {
        // Assumes the size of the window_surface to be 16x16 and tries to draw it centered
        if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
            if (active || container->state.mouse_pressing) {
                dye_surface(data->surface, config->color_taskbar_search_bar_pressed_icon);
            } else {
                dye_surface(data->surface, config->color_taskbar_search_bar_hovered_icon);
            }
        } else {
            dye_surface(data->surface, config->color_taskbar_search_bar_default_icon);
        }

        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + 12),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
        cairo_paint(cr);
    }

    if (text_empty) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 12, PangoWeight::PANGO_WEIGHT_NORMAL);
        std::string text("Type here to search");
        pango_layout_set_text(layout, text.c_str(), text.size());

        if (active || container->state.mouse_pressing || container->state.mouse_hovering) {
            if (active || container->state.mouse_pressing) {
                set_argb(cr, config->color_taskbar_search_bar_pressed_text);
            } else {
                set_argb(cr, config->color_taskbar_search_bar_hovered_text);
            }
        } else {
            set_argb(cr, config->color_taskbar_search_bar_default_text);
        }

        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);

        cairo_move_to(cr,
                      container->real_bounds.x + 12 + 16 + 12,
                      container->real_bounds.y + container->real_bounds.h / 2 -
                      ((logical.height / PANGO_SCALE) / 2));
        pango_cairo_show_layout(cr, layout);
    }
}

void paint_battery(AppClient *client_entity, cairo_t *cr, Container *container) {
    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client_entity, cr, container);
    container->real_bounds = start;

    auto *data = static_cast<data_battery_surfaces *>(container->user_data);
    assert(data);
    assert(!data->normal_surfaces.empty());
    assert(!data->charging_surfaces.empty());

    std::string line;
    std::ifstream status("/sys/class/power_supply/BAT0/status");
    if (status.is_open()) {
        data->status = "Missing BAT0";
        if (getline(status, line)) {
            data->status = line;
        }
        status.close();
    } else {
        data->status = "Missing BAT0";
    }

    std::ifstream capacity("/sys/class/power_supply/BAT0/capacity");
    if (capacity.is_open()) {
        data->capacity = "0";
        if (getline(capacity, line)) {
            data->capacity = line;
        }
        capacity.close();
    } else {
        data->capacity = "0";
    }

    int capacity_index = std::floor(((double) (std::stoi(data->capacity))) / 10.0);

    if (capacity_index > 9)
        capacity_index = 9;
    if (capacity_index < 0)
        capacity_index = 0;

    cairo_surface_t *surface;
    if (data->status == "Charging") {
        surface = data->charging_surfaces[capacity_index];
    } else {
        surface = data->normal_surfaces[capacity_index];
    }
    if (surface) {
        dye_surface(surface, config->color_taskbar_button_icons);
        cairo_set_source_surface(
                cr,
                surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 16 / 2),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 16 / 2));
        cairo_paint(cr);
    }
}

static void
make_battery_button(Container *parent, AppClient *client_entity) {
    auto *c = new Container();
    c->parent = parent;
    c->type = hbox;
    c->wanted_bounds.w = 24;
    c->wanted_bounds.h = FILL_SPACE;
    c->when_paint = paint_battery;
    c->when_clicked = clicked_battery;

    auto *data = new data_battery_surfaces;
    for (int i = 0; i <= 9; i++) {
        auto *normal_surface = accelerated_surface(app, client_entity, 16, 16);
        paint_surface_with_image(
                normal_surface,
                as_resource_path("battery/16/normal/" + std::to_string(i) + ".png"), 16,
                nullptr);
        data->normal_surfaces.push_back(normal_surface);

        auto *charging_surface = accelerated_surface(app, client_entity, 16, 16);
        paint_surface_with_image(
                charging_surface,
                as_resource_path("battery/16/charging/" + std::to_string(i) + ".png"), 16,
                nullptr);
        data->charging_surfaces.push_back(charging_surface);
    }
    c->user_data = data;

    std::string line;
    std::ifstream capacity("/sys/class/power_supply/BAT0/type");
    if (capacity.is_open()) {
        if (getline(capacity, line)) {
            if (line != "UPS") {
                parent->children.push_back(c);
            }
        }
        capacity.close();
    } else {
        delete c;
    }
};

static void
scrolled_workspace(AppClient *client_entity,
                   cairo_t *cr,
                   Container *container,
                   int horizontal_scroll,
                   int vertical_scroll) {
    int current = desktops_current(app);
    current -= vertical_scroll;
    current -= horizontal_scroll;// we subtract to correct the direction

    int count = desktops_count(app);
    if (current < 0)
        current = count - 1;
    if (current >= count)
        current = 0;
    desktops_change(app, current);
}

static void
clicked_workspace(AppClient *client_entity, cairo_t *cr, Container *container) {
    if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_1) {// left
        scrolled_workspace(client_entity, cr, container, 0, -1);
    } else {// right
        scrolled_workspace(client_entity, cr, container, 0, 1);
    }
}

static void
clicked_super(AppClient *client, cairo_t *cr, Container *container) {
    start_app_menu();
}

static void
paint_wifi(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (wifi_surfaces *) container->user_data;

    Bounds start = container->real_bounds;
    container->real_bounds.x += 1;
    container->real_bounds.y += 1;
    container->real_bounds.w -= 2;
    container->real_bounds.h -= 2;
    paint_hoverable_button_background(client, cr, container);
    container->real_bounds = start;

    bool up = false;
    bool wired = false;
    wifi_state(&up, &wired);

    cairo_surface_t *surface = nullptr;
    if (up) {
        if (wired) {
            surface = data->wired_up;
        } else {
            surface = data->wireless_up;
        }
    } else {
        if (wired) {
            surface = data->wired_down;
        } else {
            surface = data->wireless_down;
        }
    }

    if (surface) {
        dye_surface(surface, config->color_taskbar_button_icons);
        cairo_set_source_surface(
                cr,
                surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 16 / 2),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 16 / 2));
        cairo_paint(cr);
    }
}

static void
fill_root(App *app, AppClient *client, Container *root) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    root->when_paint = paint_background;
    root->type = hbox;
    root->spacing = 0;

    double fill_amount = 10;
    Container *button_super = root->child(48, FILL_SPACE);
    Container *field_search = root->child(344, FILL_SPACE);
    Container *button_workspace = root->child(48, FILL_SPACE);
    Container *container_icons = root->child(FILL_SPACE, FILL_SPACE);
    Container *button_systray = root->child(24, FILL_SPACE);
    make_battery_button(root, client);
    Container *button_wifi = root->child(24, FILL_SPACE);
    Container *button_volume = root->child(24, FILL_SPACE);
    Container *button_date = root->child(80, FILL_SPACE);
    Container *button_minimize = root->child(5, FILL_SPACE);

    button_super->when_paint = paint_super;
    button_super->user_data = new IconButton;
    button_super->when_clicked = clicked_super;
    load_icon_full_path(app,
                        client,
                        &((IconButton *) button_super->user_data)->surface,
                        as_resource_path("windows.png"), 24);

    field_search->when_paint = paint_search;
    field_search->when_mouse_down = clicked_search;
    field_search->receive_events_even_if_obstructed = true;
    field_search->user_data = new IconButton;
    field_search->name = "field_search";
    load_icon_full_path(app,
                        client,
                        &((IconButton *) field_search->user_data)->surface,
                        as_resource_path("search.png"), 24);

    TextAreaSettings settings;
    settings.font_size = 12;
    settings.font = config->font;
    settings.color = ArgbColor(0, 0, 0, 1);
    settings.color_cursor = ArgbColor(0, 0, 0, 1);
    settings.single_line = true;
    settings.wrap = false;
    settings.right_show_amount = 2;
    settings.bottom_show_amount = 2;
    field_search->wanted_pad.x = 12 + 16 + 12;
    field_search->wanted_pad.w = 8;
    auto *con = field_search->child(FILL_SPACE, FILL_SPACE);
    Container *textarea = make_textarea(app, client, con, settings);
    textarea->name = "main_text_area";
    textarea->parent->alignment = ALIGN_CENTER | ALIGN_LEFT;

    button_workspace->when_paint = paint_workspace;
    button_workspace->user_data = new WorkspaceButton;
    button_workspace->when_scrolled = scrolled_workspace;
    button_workspace->when_clicked = clicked_workspace;
    load_icon_full_path(app,
                        client,
                        &((WorkspaceButton *) button_workspace->user_data)->surface,
                        as_resource_path("taskview.png"), 24);
    load_icon_full_path(app,
                        client,
                        &((WorkspaceButton *) button_workspace->user_data)->surface_hover,
                        as_resource_path("taskview-hovered.png"), 24);

    container_icons->spacing = 1;
    container_icons->type = hbox;
    container_icons->name = "icons";
    container_icons->when_paint = paint_all_icons;

    button_systray->when_paint = paint_systray;
    button_systray->user_data = new IconButton;
    button_systray->when_clicked = clicked_systray;
    button_systray->name = "systray";
    load_icon_full_path(app,
                        client,
                        &((IconButton *) button_systray->user_data)->surface,
                        as_resource_path("arrow.png"), 24);

    button_wifi->when_paint = paint_wifi;
    button_wifi->when_clicked = clicked_wifi;
    auto wifi_data = new wifi_surfaces;
    wifi_data->wired_up = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(
            wifi_data->wired_up, as_resource_path("wifi/16/wired_up.png"), 16, nullptr);
    wifi_data->wired_down = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(
            wifi_data->wired_down, as_resource_path("wifi/16/wired_down.png"), 16, nullptr);
    wifi_data->wireless_down = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(
            wifi_data->wireless_down, as_resource_path("wifi/16/wireless_down.png"), 16, nullptr);
    wifi_data->wireless_up = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(
            wifi_data->wireless_up, as_resource_path("wifi/16/wireless_up.png"), 16, nullptr);
    button_wifi->user_data = wifi_data;

    button_volume->when_paint = paint_volume;
    button_volume->when_clicked = clicked_volume;
    button_volume->when_scrolled = scrolled_volume;
    auto surfaces = new volume_surfaces;
    surfaces->none = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(surfaces->none, as_resource_path("audio/none16.png"), 16, nullptr);
    surfaces->low = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(surfaces->low, as_resource_path("audio/low16.png"), 16, nullptr);
    surfaces->medium = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(surfaces->medium, as_resource_path("audio/medium16.png"), 16, nullptr);
    surfaces->high = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(surfaces->high, as_resource_path("audio/high16.png"), 16, nullptr);
    surfaces->mute = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(surfaces->mute, as_resource_path("audio/mute16.png"), 16, nullptr);
    button_volume->user_data = surfaces;

    double opacity_diff = .5;
    double opacity_thresh = 200;
    dye_opacity(surfaces->none, opacity_diff, opacity_thresh);
    dye_opacity(surfaces->low, opacity_diff, opacity_thresh);
    dye_opacity(surfaces->medium, opacity_diff, opacity_thresh);
    dye_opacity(surfaces->high, opacity_diff, opacity_thresh);
    dye_opacity(surfaces->mute, opacity_diff, opacity_thresh);

    button_date->when_paint = paint_date;
    button_date->when_clicked = clicked_date;
    button_date->user_data = new IconButton;

    app_timeout_create(app, client, 0, update_time, nullptr);
    app_timeout_create(app, client, 0, late_classes_update, nullptr);

    button_minimize->when_paint = paint_minimize;
    button_minimize->user_data = new IconButton;
    button_minimize->when_clicked = clicked_minimize;
}

static void
load_pinned_icons();

static void
when_taskbar_closed(AppClient *client) {
    while (app->clients.size() != 1) {
        for (int i = 0; i < app->clients.size(); i++) {
            if (app->clients[i] != client) {
                client_close(app, app->clients[i]);
            }
        }
    }
}

static bool
taskbar_event_handler(App *app, xcb_generic_event_t *event) {
    return false;
}

static void
taskbar_on_screen_size_change(App *app, AppClient *client) {
    client_set_position_and_size(app, client,
                                 client->screen_information->x,
                                 client->screen_information->y + client->screen_information->height_in_pixels -
                                 config->taskbar_height,
                                 client->screen_information->width_in_pixels,
                                 config->taskbar_height);
    handle_configure_notify(app, client, client->screen_information->x,
                            client->screen_information->y + client->screen_information->height_in_pixels -
                            config->taskbar_height, client->screen_information->width_in_pixels,
                            config->taskbar_height);
    for (auto *c : app->clients) {
        if (c->popup) {
            client_close_threaded(app, c);
            if (c->window == app->grab_window) {
                app->grab_window = -1;
                xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->grab_window, XCB_MOD_MASK_ANY);
                xcb_flush(app->connection);
            }
        }
    }
}

static void
update_window_title_name(xcb_window_t window) {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon : icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    for (auto windows_data : data->windows_data_list) {
                        if (windows_data->id == window) {
                            const xcb_get_property_cookie_t &propertyCookie = xcb_ewmh_get_wm_name(
                                    &app->ewmh, window);
                            xcb_ewmh_get_utf8_strings_reply_t data;
                            uint8_t success = xcb_ewmh_get_wm_name_reply(&app->ewmh, propertyCookie, &data,
                                                                         nullptr);
                            if (success) {
                                windows_data->title = strndup(data.strings, data.strings_len);
                                xcb_ewmh_get_utf8_strings_reply_wipe(&data);
                                return;
                            }

                            const xcb_get_property_cookie_t &cookie = xcb_icccm_get_wm_name(app->connection,
                                                                                            window);
                            xcb_icccm_get_text_property_reply_t reply;
                            success = xcb_icccm_get_wm_name_reply(app->connection, cookie, &reply,
                                                                  nullptr);
                            if (success) {
                                windows_data->title = std::string(reply.name, reply.name_len);
                                xcb_icccm_get_text_property_reply_wipe(&reply);
                                return;
                            }

                            return;
                        }
                    }
                }
            }
        }
    }
}

void remove_window(App *app, xcb_window_t window);

void add_window(App *app, xcb_window_t window);

static bool
window_event_handler(App *app, xcb_generic_event_t *event) {
    // This will listen to configure notify events and check if it's about a
    // window we need a thumbnail of and update its size if so.
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_CONFIGURE_NOTIFY: {
            auto *e = (xcb_configure_notify_event_t *) event;
            if (auto client = client_by_name(app, "taskbar")) {
                if (client->root) {
                    if (auto icons = container_by_name("icons", client->root)) {
                        for (auto icon : icons->children) {
                            auto *data = static_cast<LaunchableButton *>(icon->user_data);
                            for (auto windows_data : data->windows_data_list) {
                                if (windows_data->id == e->window) {
                                    // update the size of the surface
                                    if (windows_data->window_surface && (e->width != windows_data->width ||
                                                                         e->height != windows_data->height)) {
                                        windows_data->width = e->width;
                                        windows_data->height = e->height;
                                        cairo_xcb_surface_set_size(windows_data->window_surface,
                                                                   windows_data->width, windows_data->height);

                                        cairo_surface_destroy(windows_data->raw_thumbnail_surface);
                                        cairo_destroy(windows_data->raw_thumbnail_cr);
                                        cairo_surface_destroy(windows_data->scaled_thumbnail_surface);
                                        cairo_destroy(windows_data->scaled_thumbnail_cr);

                                        windows_data->raw_thumbnail_surface = accelerated_surface_rgb(app,
                                                                                                      client_by_name(
                                                                                                              app,
                                                                                                              "taskbar"),
                                                                                                      windows_data->width,
                                                                                                      windows_data->height);
                                        windows_data->raw_thumbnail_cr = cairo_create(
                                                windows_data->raw_thumbnail_surface);
                                        windows_data->scaled_thumbnail_surface = accelerated_surface_rgb(app,
                                                                                                         client_by_name(
                                                                                                                 app,
                                                                                                                 "taskbar"),
                                                                                                         option_width,
                                                                                                         option_height);
                                        windows_data->scaled_thumbnail_cr = cairo_create(
                                                windows_data->scaled_thumbnail_surface);
                                    }
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
            break;
        }
        case XCB_PROPERTY_NOTIFY: {
            auto e = (xcb_property_notify_event_t *) event;
//            const xcb_get_atom_name_cookie_t &cookie = xcb_get_atom_name(app->connection, e->atom);
//            xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, nullptr);
//            char *string = xcb_get_atom_name_name(reply);
//            printf("%s\n", string);
            if (e->atom == get_cached_atom(app, "WM_NAME") ||
                e->atom == get_cached_atom(app, "_NET_WM_NAME")) {
                update_window_title_name(e->window);
            } else if (e->atom == get_cached_atom(app, "WM_CLASS")) {
            }
            break;
        }
    }
    return false;
}

void screenshot_active_window(App *app, AppClient *client, void *user_data) {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon : icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    for (auto windows_data : data->windows_data_list) {
                        if (windows_data->id == active_window) {
                            windows_data->take_screenshot();
                            return;
                        }
                    }
                }
            }
        }
    }
}

AppClient *
create_taskbar(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    app = app;

    // Set window startup settings
    Settings settings;
    settings.window_transparent = true;
    settings.decorations = false;
    settings.dock = true;
    settings.skip_taskbar = true;
    settings.reserve_side = true;
    settings.reserve_bottom = config->taskbar_height;
    settings.x = 0;
    settings.y = app->bounds.h - config->taskbar_height;
    settings.w = app->bounds.w;
    settings.h = config->taskbar_height;
    settings.sticky = true;
    settings.force_position = true;

    // Create the window

    AppClient *taskbar = client_new(app, settings, "taskbar");
    taskbar->when_closed = when_taskbar_closed;
    taskbar->on_any_screen_change = taskbar_on_screen_size_change;

    app_create_custom_event_handler(app, taskbar->window, taskbar_event_handler);
    app_create_custom_event_handler(app, INT_MAX, window_event_handler);
    app_timeout_create(app, taskbar, 500, screenshot_active_window, nullptr);

    // Lay it out
    fill_root(app, taskbar, taskbar->root);
    update_active_window();

    load_pinned_icons();

    if (audio_connected) {
        audio_all_clients();
        audio_all_outputs();
    }

    return taskbar;
}

std::string
class_name(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    xcb_generic_error_t *error = NULL;
    xcb_get_property_cookie_t c = xcb_icccm_get_wm_class_unchecked(app->connection, window);
    xcb_get_property_reply_t *r = xcb_get_property_reply(app->connection, c, &error);

    if (error) {
        std::free(error);
    } else if (r) {
        xcb_icccm_get_wm_class_reply_t wm_class;
        if (xcb_icccm_get_wm_class_from_reply(&wm_class, r)) {
            std::string name;

            if (wm_class.class_name) {
                name = std::string(wm_class.class_name);
                if (name.empty()) {
                    name = std::string(wm_class.instance_name);
                }
            } else if (wm_class.instance_name) {
                name = std::string(wm_class.instance_name);
            } else {
            }
            xcb_icccm_get_wm_class_reply_wipe(&wm_class);

            std::for_each(name.begin(), name.end(), [](char &c) { c = std::tolower(c); });

            return name;
        } else {
            std::free(r);
        }
    }
    return std::to_string(window);
}

std::string get_reply_string(xcb_ewmh_get_utf8_strings_reply_t *reply) {
    std::string str;
    if (reply) {
        str = std::string(reply->strings, reply->strings_len);
        xcb_ewmh_get_utf8_strings_reply_wipe(reply);
    }
    return str;
}

std::string get_icon_name(xcb_window_t win) {
    xcb_ewmh_get_utf8_strings_reply_t utf8_reply{};
    if (xcb_ewmh_get_wm_icon_name_reply(&app->ewmh, xcb_ewmh_get_wm_icon_name(&app->ewmh, win), &utf8_reply, nullptr)) {
        return get_reply_string(&utf8_reply);
    }
    return "";
}

void add_window(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Exit the function if the window type is not something a dock should display
    xcb_get_property_cookie_t cookie = xcb_ewmh_get_wm_window_type_unchecked(&app->ewmh, window);
    xcb_ewmh_get_atoms_reply_t atoms_reply_data;
    if (xcb_ewmh_get_wm_window_type_reply(&app->ewmh, cookie, &atoms_reply_data, nullptr)) {
        for (unsigned short i = 0; i < atoms_reply_data.atoms_len; i++) {
            if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DESKTOP")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_POPUP_MENU")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_TOOLTIP")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_COMBO")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            } else if (atoms_reply_data.atoms[i] == get_cached_atom(app, "_NET_WM_WINDOW_TYPE_DND")) {
                xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
                return;
            }
        }
        xcb_ewmh_get_atoms_reply_wipe(&atoms_reply_data);
    }

    // on gnome, the Extension app ends up adding the taskbar to the taskbar. I have no idea how it's doing that
    // but the fix for now is just going to be to ignore every client that is ours. Eventually when we make a settings
    // app, we will have to add an exception for that window.
    for (auto c : app->clients) {
        if (c->window == window) {
            return;
        }
    }

    auto cookie_get_wm_desktop = xcb_ewmh_get_wm_desktop(&app->ewmh, window);
    uint32_t desktop = 0;
    xcb_ewmh_get_wm_desktop_from_reply(&desktop, NULL);

    std::vector <xcb_window_t> old_windows;
    AppClient *client = client_by_name(app, "taskbar");
    if (!client)
        return;
    auto *root = client->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    std::string window_class_name = class_name(app, window);
    window_class_name = c3ic_fix_wm_class(window_class_name);

    for (auto icon : icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        if (data->class_name == window_class_name) {
            const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
            xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
            xcb_flush(app->connection);

            data->windows_data_list.push_back(new WindowsData(app, window));
            update_window_title_name(window);
            update_minimize_icon_positions();
            request_refresh(app, client);
            return;
        }
    }

    xcb_generic_error_t *err = nullptr;
    cookie = xcb_get_property(app->connection, 0, window, get_cached_atom(app, "_NET_WM_STATE"), XCB_ATOM_ATOM, 0,
                              BUFSIZ);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, &err);
    if (reply) {
        if (reply->type == XCB_ATOM_ATOM) {
            xcb_atom_t *state_atoms = (xcb_atom_t *) xcb_get_property_value(reply);
            for (unsigned int a = 0; a < sizeof(xcb_atom_t); a++) {
                // TODO: on first launch xterm has this true????
                if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_SKIP_TASKBAR")) {
                    free(reply);
                    return;
                } else if (state_atoms[a] == get_cached_atom(app, "_NET_WM_STATE_SKIP_PAGER")) {
                    free(reply);
                    return;
                } else if (state_atoms[a] ==
                           get_cached_atom(app, "_NET_WM_STATE_DEMANDS_ATTENTION")) {
                }
            }
        }
        free(reply);
    }
    if (err) {
        free(err);
        err = nullptr;
    }

    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, window, XCB_CW_EVENT_MASK, values);
    xcb_flush(app->connection);

    Container *a = icons->child(48, FILL_SPACE);
    a->when_drag_end_is_click = false;
    a->when_mouse_enters_container = pinned_icon_mouse_enters;
    a->when_mouse_leaves_container = pinned_icon_mouse_leaves;
    a->when_clicked = pinned_icon_mouse_clicked;
    a->when_drag_end = pinned_icon_drag_end;
    a->when_drag_start = pinned_icon_drag_start;
    a->when_drag = pinned_icon_drag;
    LaunchableButton *data = new LaunchableButton();
    data->windows_data_list.push_back(new WindowsData(app, window));
    data->class_name = window_class_name;
    data->icon_name = window_class_name;
    a->user_data = data;
    update_window_title_name(window);

    xcb_get_property_cookie_t prop_cookie = xcb_ewmh_get_wm_pid(&app->ewmh, window);
    uint32_t pid = -1;
    xcb_ewmh_get_wm_pid_reply(&app->ewmh, prop_cookie, &pid, NULL);
    if (pid != -1) {
        std::string line;

        std::ifstream cmdline("/proc/" + std::to_string(pid) + "/cmdline");
        std::getline((cmdline), line);

        size_t index = 0;
        while (true) {
            /* Locate the substring to replace. */
            index = line.find('\000', index);
            if (index == std::string::npos)
                break;

            /* Make the replacement. */
            line.replace(index, 1, " ");

            /* Advance index forward so the next iteration doesn't pick it up as well. */
            index += 1;
        }

        data->has_launchable_info = true;
        data->command_launched_by = line;
    }

    std::string path;
    std::string icon_name;

    auto get_wm_icon_name_cookie = xcb_icccm_get_wm_icon_name(app->connection, window);
    xcb_icccm_get_text_property_reply_t prop;
    uint8_t success = xcb_icccm_get_wm_icon_name_reply(app->connection, get_wm_icon_name_cookie, &prop, nullptr);
    if (success) {
        icon_name = prop.name;
        xcb_icccm_get_text_property_reply_wipe(&prop);
    } else {
        icon_name = get_icon_name(window);
    }
    if (!icon_name.empty()) {
        path = find_icon(icon_name, 24);
        data->icon_name = icon_name;
    }
    if (path.empty()) {
        xcb_generic_error_t *error = NULL;
        xcb_get_property_cookie_t c = xcb_icccm_get_text_property_unchecked(app->connection, window,
                                                                            get_cached_atom(app,
                                                                                            "_GTK_APPLICATION_ID"));
        xcb_icccm_get_text_property_reply_t props;
        if (xcb_icccm_get_text_property_reply(app->connection, c, &props, nullptr)) {
            props.name[props.name_len] = '\0';
            data->icon_name = props.name;
            path = find_icon(props.name, 24);
            xcb_icccm_get_text_property_reply_wipe(&props);
        }
    }
    if (path.empty()) {
        path = find_icon(window_class_name, 24);
        data->icon_name = window_class_name;
    }

    if (!path.empty()) {
        load_icon_full_path(app, client, &data->surface, path, 24);
    } else {
        xcb_generic_error_t *error;
        xcb_get_property_cookie_t c = xcb_ewmh_get_wm_icon(&app->ewmh, window);

        xcb_ewmh_get_wm_icon_reply_t wm_icon;
        memset(&wm_icon, 0, sizeof(xcb_ewmh_get_wm_icon_reply_t));
        xcb_ewmh_get_wm_icon_reply(&app->ewmh, c, &wm_icon, &error);

        if (error) {
            std::free(error);
            data->surface = accelerated_surface(app, client, 24, 24);
            paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                     24, nullptr);
        } else {
            if (0 < xcb_ewmh_get_wm_icon_length(&wm_icon)) {
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t *icon_data = NULL;

                xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&wm_icon);
                for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
                    if (iter.width > width) {
                        width = iter.width;
                        height = iter.height;
                        icon_data = iter.data;
                        if (width == 24) {
                            break;
                        }
                    }
                }
                auto stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
                auto surface = cairo_image_surface_create_for_data(
                        (unsigned char *) icon_data, CAIRO_FORMAT_ARGB32, width, height, stride);

                cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
                cairo_pattern_set_filter(pattern, CAIRO_FILTER_BEST);

                data->surface = accelerated_surface(app, client, 24, 24);
                cairo_t *cr = cairo_create(data->surface);

                cairo_save(cr);
                double taskbar_icon_size = 24;
                cairo_scale(cr, taskbar_icon_size / (width), taskbar_icon_size / (width));
                cairo_set_source(cr, pattern);
                cairo_paint(cr);
                cairo_restore(cr);

                cairo_destroy(cr);
                xcb_ewmh_get_wm_icon_reply_wipe(&wm_icon);
            } else {
                data->surface = accelerated_surface(app, client, 24, 24);
                paint_surface_with_image(data->surface, as_resource_path("unknown-24.svg"),
                                         24, nullptr);
            }
        }
    }

    update_minimize_icon_positions();
    update_pinned_items_file();

    client_layout(app, client);
    request_refresh(app, client);
}

void remove_window(App *app, xcb_window_t window) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    std::vector<xcb_window_t> old_windows;
    AppClient *entity = client_by_name(app, "taskbar");
    if (!entity)
        return;
    auto *root = entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    for (int j = 0; j < icons->children.size(); j++) {
        Container *container = icons->children[j];
        LaunchableButton *data = (LaunchableButton *) container->user_data;
        for (int i = 0; i < data->windows_data_list.size(); i++) {
            if (data->windows_data_list[i]->id == window) {
                delete data->windows_data_list[i];
                data->windows_data_list.erase(data->windows_data_list.begin() + i);

                if (data->windows_data_list.empty() && !data->pinned) {
                    if (active_container == container)
                        active_container = nullptr;
                    icons->children.erase(icons->children.begin() + j);
                    delete container;
                }
                break;
            }
        }
    }

    // TODO: mark handler as remove at end of loop
    //    for (int i = 0; i < app->handlers.size(); i++) {
    //        if (app->handlers[i]->target_window == window) {
    //            delete (app->handlers[i]);
    //            app->handlers.erase(app->handlers.begin() + i);
    //        }
    //    }

    update_pinned_items_file();
    icons_align(entity, icons, false);
    request_refresh(app, entity);
}

void stacking_order_changed(xcb_window_t *all_windows, int windows_count) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif

    std::vector<xcb_window_t> new_windows;
    for (int i = 0; i < windows_count; i++) {
        new_windows.push_back(all_windows[i]);
    }

    std::vector<xcb_window_t> old_windows;
    AppClient *entity = client_by_name(app, "taskbar");
    if (!entity)
        return;
    auto *root = entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    for (auto icon : icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);
        for (auto window_data : data->windows_data_list) {
            old_windows.emplace_back(window_data->id);
        }
    }

    for (auto new_window : new_windows) {
        bool found = false;
        for (auto old_window : old_windows)
            if (old_window == new_window)
                found = true;
        if (!found) {
            add_window(app, new_window);
        }
    }

    for (int i = 0; i < old_windows.size(); i++) {
        xcb_window_t old_window = old_windows[i];
        bool found = false;
        for (auto new_window : new_windows)
            if (old_window == new_window)
                found = true;
        if (!found) {
            remove_window(app, old_window);
        }
    }
}

void remove_non_pinned_icons() {
    AppClient *client_entity = client_by_name(app, "taskbar");
    if (!valid_client(app, client_entity))
        return;
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    icons->children.erase(std::remove_if(icons->children.begin(),
                                         icons->children.end(),
                                         [](Container *icon) {
                                             auto *data =
                                                     static_cast<LaunchableButton *>(icon->user_data);
                                             bool del = data->windows_data_list.empty() && !data->pinned;
                                             if (del) {
                                                 delete data;
                                             }

                                             return del;
                                         }),
                          icons->children.end());

    update_pinned_items_file();
    icons_align(client_entity, icons, false);
}

#include <INIReader.h>
#include <sys/stat.h>
#include <xcb/xcb_aux.h>

/**
 * Save the pinned items to a file on disk so we can load them on the next session
 */
void update_pinned_items_file() {
    const char *home = getenv("HOME");
    std::string itemsPath(home);
    itemsPath += "/.config/";

    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }

    itemsPath += "/winbar/";

    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }

    itemsPath += "items.ini";

    std::ofstream itemsFile(itemsPath);

    if (!itemsFile.is_open())
        return;

    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    update_minimize_icon_positions();

    int i = 0;
    for (auto icon : icons->children) {
        auto *data = static_cast<LaunchableButton *>(icon->user_data);

        if (!data)
            continue;
        if (!data->pinned)
            continue;

        itemsFile << "[PinnedIcon" << i++ << "]" << std::endl;

        itemsFile << "#The class_name is a property that windows set on themselves so that they "
                     "can be stacked with windows of the same kind as them. If when you click this "
                     "pinned icon button, it launches a window that creates an icon button that "
                     "doesn't stack with this one then the this wm_class is wrong and you're going "
                     "to have to fix it by running xprop in your console and clicking the window "
                     "that opened to find the real WM_CLASS that should be set."
                  << std::endl;
        itemsFile << "class_name=" << data->class_name << std::endl;

        itemsFile << "#If you want to change the icon, modify this."
                  << std::endl;
        itemsFile << "user_icon_name=" << data->user_icon_name << std::endl;

        itemsFile << "#If you want to change the icon use \"user_icon_name\" instead since this one can be overriden."
                  << std::endl;
        itemsFile << "icon_name=" << data->icon_name << std::endl;

        itemsFile << "#The command that is run when the icon is clicked" << std::endl;
        itemsFile << "command=" << data->command_launched_by << std::endl
                  << std::endl;
        itemsFile << std::endl;
    }

    itemsFile.close();
}

static void
load_pinned_icons() {
    AppClient *client_entity = client_by_name(app, "taskbar");
    auto *root = client_entity->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    const char *home = getenv("HOME");
    std::string itemsPath(home);
    itemsPath += "/.config/";

    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }

    itemsPath += "/winbar/";

    if (mkdir(itemsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", itemsPath.c_str());
            return;
        }
    }

    itemsPath += "items.ini";

    // READ INI FILE
    INIReader itemFile(itemsPath);
    if (itemFile.ParseError() != 0)
        return;

    for (const std::string &section_title : itemFile.Sections()) {
        auto *child = new Container();
        child->parent = icons;
        child->wanted_bounds.h = FILL_SPACE;
        child->wanted_bounds.w = 48;

        child->when_drag_end_is_click = false;
        child->when_mouse_enters_container = pinned_icon_mouse_enters;
        child->when_mouse_leaves_container = pinned_icon_mouse_leaves;
        child->when_clicked = pinned_icon_mouse_clicked;
        child->when_drag_end = pinned_icon_drag_end;
        child->when_drag_start = pinned_icon_drag_start;
        child->when_drag = pinned_icon_drag;

        auto *data = new LaunchableButton;
        data->class_name = itemFile.Get(section_title, "class_name", "NONE");
        data->icon_name = itemFile.Get(section_title, "icon_name", "NONE");
        data->user_icon_name = itemFile.Get(section_title, "user_icon_name", "NONE");
        data->pinned = true;
        auto command = itemFile.Get(section_title, "command", "NONE");
        if (command != "NONE") {
            data->has_launchable_info = true;
            data->command_launched_by = command;
        }

        data->icon_name = c3ic_fix_wm_class(data->icon_name);
        std::string path;
        if (data->user_icon_name.empty()) {
            path = find_icon(data->icon_name, 24);
        } else {
            path = find_icon(data->user_icon_name, 24);
        }
        if (!path.empty()) {
            load_icon_full_path(app, client_entity, &data->surface, path, 24);
        } else {
            data->surface = accelerated_surface(app, client_entity, 24, 24);
            char *string = getenv("HOME");
            std::string home(string);
            home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
            bool b = paint_surface_with_image(data->surface, home, 24, nullptr);
            if (!b) {
                paint_surface_with_image(
                        data->surface, as_resource_path("unknown-24.svg"), 24, nullptr);
            }
        }

        child->user_data = data;

        icons->children.push_back(child);
    }
}

static void
late_classes_update(App *app, AppClient *client, void *data) {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Late WM_CLASS Thread");
#endif
    return;
    auto *root = client->root;
    if (!root)
        return;
    auto *icons = container_by_name("icons", root);
    if (!icons)
        return;

    for (auto icon : icons->children) {
        auto data = static_cast<LaunchableButton *>(icon->user_data);

        // since when windows don't have real classes on them we set their names to their class.
        // they should only be stacked by one
        if (data->windows_data_list.size() == 1) {
            auto name = class_name(app, data->windows_data_list[0]->id);

            if (name != data->class_name) {
                remove_window(app, data->windows_data_list[0]->id);
                add_window(app, data->windows_data_list[0]->id);
            }
        }
    }

    app_timeout_create(app, client, 10000, late_classes_update, nullptr);
}

void update_taskbar_volume_icon() {
    if (auto *client = client_by_name(app, "taskbar")) {
        auto *event = new xcb_expose_event_t;

        event->response_type = XCB_EXPOSE;
        event->window = client->window;

        xcb_send_event(app->connection, true, event->window, XCB_EVENT_MASK_EXPOSURE, (char *) event);
        xcb_flush(app->connection);

        delete event;
    }
}

void set_textarea_active() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            container->parent->active = true;
            container->parent->active = true;
        }
        request_refresh(client->app, client);
    }
}

void set_textarea_inactive() {
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            auto *text_data = (TextAreaData *) container->user_data;
            delete text_data->state;
            text_data->state = new TextState;
            container->parent->active = false;
        }
        request_refresh(client->app, client);
    }
}

void register_popup(xcb_window_t window) {
    // Close every other popup
    xcb_aux_sync(app->connection);

    auto *client = client_by_window(app, window);
    if (!valid_client(app, client))
        return;
    popup_window_open = window;

    // TODO why don't we grab the pointer instead?
    xcb_void_cookie_t grab_cookie =
            xcb_grab_button_checked(app->connection,
                                    False,
                                    app->screen->root,
                                    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
                                    XCB_GRAB_MODE_SYNC,
                                    XCB_GRAB_MODE_ASYNC,
                                    XCB_NONE,
                                    XCB_NONE,
                                    XCB_BUTTON_INDEX_ANY,
                                    XCB_MOD_MASK_ANY);
    xcb_generic_error_t *error = xcb_request_check(app->connection, grab_cookie);

    if (error != NULL) {
        printf("Could not grab pointer for the window %d\n", window);
        client_close_threaded(app, client);
        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
    } else {
        if (auto *c = client_by_name(app, "taskbar")) {
            app->grab_window = popup_window_open;
        } else {
            app->grab_window = popup_window_open;
        }


        xcb_set_input_focus(
                app->connection, XCB_INPUT_FOCUS_PARENT, popup_window_open, XCB_CURRENT_TIME);
        for (auto *c : app->clients) {
            if (c->window != window) {
                if (c->popup) {
                    client_close_threaded(app, c);
                }
            }
        }
    }
}

void update_pinned_items_icon() {
    if (auto client = client_by_name(app, "taskbar")) {
        if (client->root) {
            if (auto icons = container_by_name("icons", client->root)) {
                for (auto icon : icons->children) {
                    auto *data = static_cast<LaunchableButton *>(icon->user_data);
                    if (data->surface) {
                        cairo_surface_destroy(data->surface);
                        data->surface = nullptr;
                    }

                    std::string path;
                    if (!data->icon_name.empty()) {
                        path = find_icon(data->icon_name, 24);
                    }
                    if (path.empty()) {
                        path = find_icon(data->class_name, 24);
                    }
                    if (!path.empty()) {
                        load_icon_full_path(app, client, &data->surface, path, 24);
                    } else {
                        data->surface = accelerated_surface(app, client, 24, 24);
                        char *string = getenv("HOME");
                        std::string home(string);
                        home += "/.config/winbar/cached_icons/" + data->class_name + ".png";
                        bool b = paint_surface_with_image(data->surface, home, 24, nullptr);
                        if (!b) {
                            paint_surface_with_image(
                                    data->surface, as_resource_path("unknown-24.svg"), 24, nullptr);
                        }
                    }
                }
            }
        }
    }
}

WindowsData::WindowsData(App *app, xcb_window_t window) {
    id = window;

    const xcb_get_window_attributes_cookie_t &attributesCookie = xcb_get_window_attributes(
            app->connection, window);
    xcb_get_window_attributes_reply_t *attributes = xcb_get_window_attributes_reply(app->connection,
                                                                                    attributesCookie,
                                                                                    nullptr);
    if (attributes) {
        // TODO: screen should be found using the window somehow I think
        xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(app->connection)).data;
        xcb_visualtype_t *visual = xcb_aux_find_visual_by_id(screen, attributes->visual);

        xcb_get_geometry_cookie_t geomCookie = xcb_get_geometry(app->connection,
                                                                window);  // window is a xcb_drawable_t
        xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(app->connection, geomCookie, NULL);

        if (geom) {
            window_surface = cairo_xcb_surface_create(app->connection,
                                                      window,
                                                      visual,
                                                      (width = geom->width),
                                                      (height = geom->height));

            raw_thumbnail_surface = accelerated_surface_rgb(app, client_by_name(app, "taskbar"), width, height);
            raw_thumbnail_cr = cairo_create(raw_thumbnail_surface);
            scaled_thumbnail_surface = accelerated_surface_rgb(app, client_by_name(app, "taskbar"), option_width,
                                                               option_height);
            scaled_thumbnail_cr = cairo_create(scaled_thumbnail_surface);
            take_screenshot();
            free(geom);
        }
        free(attributes);
    }
}

WindowsData::~WindowsData() {
    if (window_surface) {
        cairo_surface_destroy(window_surface);
        cairo_surface_destroy(raw_thumbnail_surface);
        cairo_destroy(raw_thumbnail_cr);
        cairo_surface_destroy(scaled_thumbnail_surface);
        cairo_destroy(scaled_thumbnail_cr);
    }
}

void WindowsData::take_screenshot() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_set_source_surface(raw_thumbnail_cr, window_surface, 0, 0);
    cairo_paint(raw_thumbnail_cr);

    cairo_surface_flush(window_surface);
    cairo_surface_mark_dirty(window_surface);
}

void WindowsData::rescale(double scale_w, double scale_h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    last_rescale_timestamp = get_current_time_in_ms();

    // TODO: make sure the window is mapped.
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(raw_thumbnail_surface);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_GOOD);

    cairo_save(scaled_thumbnail_cr);
    cairo_scale(scaled_thumbnail_cr, scale_w, scale_h);
    cairo_set_source(scaled_thumbnail_cr, pattern);
    cairo_paint(scaled_thumbnail_cr);
    cairo_restore(scaled_thumbnail_cr);
}