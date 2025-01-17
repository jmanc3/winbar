//
// Created by jmanc3 on 3/8/20.
//
#include "volume_menu.h"

#include "audio.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "components.h"
#include "icons.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include <application.h>
#include <iostream>
#include <math.h>
#include <pango/pangocairo.h>
#include <utility.h>
#include <condition_variable>

static AppClient *client_entity;
static std::string connected_message;

// TODO: every frame we should resize and remake containers based on data in AudioClients and
// audio_outputs since it can change
//  behind our hands and lead to a crash because we index into the list instead of doing something
//  smarter like have the actual index of the thing
// TODO: fix muting the output leading to a pause
void total_update();

class option_data : UserData {
public:
    int unique_client_id = -100;
    double volume = 0;
    bool muted = false;
    double peak = 0;
    std::string title;
    std::string icon_name;
    int sort_index = 0; // the higher means it comes first (based on total size of client list)
    
    long last_update = get_current_time_in_ms();
    double rolling_avg = 0;
    double rolling_avg_delayed = 0;
    cairo_surface_t *icon = nullptr;
    long last_time_peak_set = 0;
    
    double position = 0;
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
    
    ~option_data() {
        if (icon) {
            cairo_surface_destroy(icon);
            icon = nullptr;
        }
    }
};

void rounded_rect(cairo_t *cr, double corner_radius, double x, double y, double width, double height) {
    double radius = corner_radius;
    double degrees = M_PI / 180.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

static void
fill_root(AppClient *client, Container *root);

static void
paint_root(AppClient *client_entity, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client_entity, config->color_volume_background));
    cairo_fill(cr);
    
    if (!audio_running) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        int width;
        int height;
        pango_layout_set_text(layout, connected_message.c_str(), -1);
        pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        set_argb(cr, config->color_volume_text);
        cairo_move_to(cr,
                      container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                      container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
        pango_cairo_show_layout(cr, layout);
    }
}

static void
paint_volume_icon(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    double scalar = data->volume;
    bool is_muted = data->muted;
    
    int val = (int) std::round(scalar * 100);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 20 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, "\uEBC5", strlen("\uE83F"));
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    if (!is_muted) {
        ArgbColor volume_bars_color = ArgbColor(.4, .4, .4, 1);
        set_argb(cr, volume_bars_color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (is_muted) {
        pango_layout_set_text(layout, "\uE74F", strlen("\uE83F"));
    } else if (val == 0) {
        pango_layout_set_text(layout, "\uE992", strlen("\uE83F"));
    } else if (val < 33) {
        pango_layout_set_text(layout, "\uE993", strlen("\uE83F"));
    } else if (val < 66) {
        pango_layout_set_text(layout, "\uE994", strlen("\uE83F"));
    } else {
        pango_layout_set_text(layout, "\uE995", strlen("\uE83F"));
    }
    
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_volume_amount(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 17 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    double scalar = data->volume;
    
    std::string text = std::to_string((int) (std::round(scalar * 100)));
    
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_label(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->user_data);
    std::string text = data->title;
    
    int pad = 13 * config->dpi;
    double icon_width = 34 * config->dpi;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    
    int text_space = container->real_bounds.w - (container->real_bounds.x + (pad * 2) + icon_width);
    int off = 0;
    if (width > text_space) {
        off = -(width - text_space);
        bool animating = false;
        for (auto a: client_entity->animations)
            if (a.value == &data->position)
                animating = true;
        if (!animating) {
            if (data->position > .5) {
                client_create_animation(app, client_entity, &data->position, data->lifetime, 1000, 1000 + (text.size() * 30),
                                        getEasingFunction(EaseInOutSine), 0);
            } else {
                client_create_animation(app, client_entity, &data->position, data->lifetime, 2400, 1000 + (text.size() * 30),
                                        getEasingFunction(EaseInOutSine), 1);
            }
        }
    }
    off = off * data->position;
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr, off + container->real_bounds.x + pad + icon_width, container->real_bounds.y + 12);
    if (data->position < .94)
        pango_layout_set_width(layout, (text_space - off) * PANGO_SCALE);
    pango_cairo_show_layout(cr, layout);
    if (data->position < .94)
        pango_layout_set_width(layout, -1);
    pango_layout_set_attributes(layout, nullptr);
    auto start_operator = cairo_get_operator(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_rect(cr,
             Bounds(container->real_bounds.x, container->real_bounds.y, pad + icon_width, container->real_bounds.h));
    set_argb(cr, config->color_volume_background);
    cairo_fill(cr);
    cairo_set_operator(cr, start_operator);
    
    if (data->icon) {
        cairo_set_source_surface(cr, data->icon,
                                 container->real_bounds.x + 8 * config->dpi,
                                 container->real_bounds.y + 8 * config->dpi);
        cairo_paint(cr);
    }
}

static void
paint_option(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    
    double marker_height = 24 * config->dpi;
    double marker_width = 8 * config->dpi;
    double volume = data->volume;
    long current = get_current_time_in_ms();
    long delta = current - data->last_time_peak_set;
    double scalar = std::min(1.0, delta / 250.0);
    double peak = data->peak * (1 - scalar);
    double marker_position = volume * container->real_bounds.w;
    
    double line_height = 2 * config->dpi;
    set_argb(cr, config->color_volume_slider_background);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    container->real_bounds.w,
                    line_height);
    cairo_fill(cr);
    set_argb(cr, config->color_volume_slider_foreground);
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2,
                    marker_position,
                    line_height);
    cairo_fill(cr);
    
    if (peak > data->rolling_avg) {
        data->rolling_avg = peak;
        client_create_animation(app, client_entity, &data->rolling_avg, data->lifetime, 0, 250, nullptr, 0);
    }
    
    if (peak > data->rolling_avg_delayed) {
        data->rolling_avg_delayed = peak;
        client_create_animation(app, client_entity, &data->rolling_avg_delayed, data->lifetime, 130, 1000, getEasingFunction(EaseInExpo), 0);
    }
    
    if (is_light_theme(config->color_volume_background)) {
        set_argb(cr, darken(config->color_volume_background, 13));
    } else {
        set_argb(cr, lighten(config->color_volume_background, 20));
    }
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2 + line_height,
                    marker_position * data->rolling_avg_delayed,
                    line_height);
    cairo_fill(cr);
    
    if (is_light_theme(config->color_volume_background)) {
        set_argb(cr, darken(config->color_volume_background, 30));
    } else {
        set_argb(cr, lighten(config->color_volume_background, 50));
    }
    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h / 2 - line_height / 2 + line_height,
                    marker_position * data->rolling_avg,
                    line_height);
    cairo_fill(cr);
    
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        set_argb(cr, config->color_volume_slider_active);
    } else {
        set_argb(cr, config->color_volume_slider_foreground);
    }
    
    rounded_rect(cr,
                 4 * config->dpi,
                 container->real_bounds.x + marker_position - marker_width / 2,
                 container->real_bounds.y + container->real_bounds.h / 2 - marker_height / 2,
                 marker_width,
                 marker_height);
    cairo_fill(cr);
}

static void
toggle_mute(AppClient *client_entity, cairo_t *cr, Container *container) {
#ifdef  TRACY_ENABLE
    ZoneScoped;
#endif
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    data->muted = !data->muted;
    audio([&data]() {
        for (auto c: audio_clients) {
            if (c->index == data->unique_client_id) {
                c->set_mute(!c->is_muted());
                break;
            }
        }
    });
    update_taskbar_volume_icon();
}

static void
scroll(AppClient *client_entity, cairo_t *cr, Container *container, int scroll_x, int scroll_y,
       bool came_from_touchpad) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    audio([&data, &client_entity, &cr, &container, scroll_x, scroll_y, came_from_touchpad]() {
        for (auto c: audio_clients) {
            if (c->index == data->unique_client_id) {
                adjust_volume_based_on_fine_scroll(c, client_entity, cr, container,
                                                   scroll_x, scroll_y, came_from_touchpad);
            }
        }
    });
}

static void
drag(AppClient *client_entity, cairo_t *cr, Container *container, bool force) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    if (!force) {
        long delta = get_current_time_in_ms() - data->last_update;
        if (delta < 10) {
            return;
        }
    }
    data->last_update = get_current_time_in_ms();
    audio([&data, &client_entity, &container]() {
        AudioClient *client = nullptr;
        for (auto c: audio_clients) {
            if (c->index == data->unique_client_id) {
                client = c;
                break;
            }
        }
        if (!client) return;
        
        // mouse_current_x and y are relative to the top left point of the window
        int limited_x = client_entity->mouse_current_x;
        
        if (limited_x < container->real_bounds.x) {
            limited_x = container->real_bounds.x;
        } else if (limited_x > container->real_bounds.x + container->real_bounds.w) {
            limited_x = container->real_bounds.x + container->real_bounds.w;
        }
        
        limited_x -= container->real_bounds.x;
        double new_volume = limited_x / container->real_bounds.w;
        if (new_volume < 0) {
            new_volume = 0;
        } else if (new_volume > 1) {
            new_volume = 1;
        }
        if (((int) std::round(new_volume * 100)) != ((int) std::round(client->cached_volume * 100))) {
            if (client->is_muted())
                client->set_mute(false);
            
            client->set_volume(new_volume);
            if (client->is_master_volume())
                update_taskbar_volume_icon();
        }
        client->cached_volume = new_volume;
    });
}

static void
drag_force(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, true);
}

static void
drag_whenever(AppClient *client_entity, cairo_t *cr, Container *container) {
    drag(client_entity, cr, container, false);
}

void restart_volume_timeout(App *, AppClient *, Timeout *, void *) {
    open_volume_menu();
}

static void
retry_audio_connection(AppClient *client_entity, cairo_t *cr, Container *container) {
    client_close_threaded(app, client_entity);
    if (!audio_running)
        audio_start(app);
    app_timeout_create(app, nullptr, 1000, restart_volume_timeout, nullptr,
                       const_cast<char *>(__PRETTY_FUNCTION__));
}

std::string to_lower_and_remove_non_printable(const std::string &input) {
    std::string result;
    for (char c: input) {
        if (std::isprint(c)) {
            result.push_back(std::tolower(c));
        }
    }
    return result;
}

// TODO: this has to be set on scrolled by the respective parties not on pierced handling
static long last_time_scrollpane_locked = 0;
static long last_time_volume_locked = 0;

void fill_root(AppClient *client, Container *root) {
    root->when_paint = paint_root;
    root->type = vbox;
    root->name = "vbox_container";
    
    struct Pair {
        std::string icon_name;
        int uid = PA_INVALID_INDEX;
        double volume = 0;
        bool muted = 0;
        double peak = peak;
        std::string text;
    };
    std::vector<Pair> pairs;
    
    audio_read([&pairs]() {
        for (auto c: audio_clients) {
            Pair pair;
            pair.text = "";
            std::string str1 = to_lower_and_remove_non_printable(c->subtitle);
            std::string str2 = to_lower_and_remove_non_printable(c->title);
            if (str1 != str2) {
                for (auto ch: c->subtitle)
                    if (isprint(ch))
                        pair.text += ch;
                pair.text += " - ";
                for (auto ch: c->title)
                    if (isprint(ch))
                        pair.text += ch;
            } else {
                pair.text = c->title;
            }
            if (c->title == "Master") pair.text = "Master";
            pair.icon_name = c->icon_name;
            pair.uid = c->index;
            pair.volume = c->get_volume();
            c->cached_volume = pair.volume;
            pair.muted = c->mute;
            pair.peak = c->peak;
            pairs.push_back(pair);
        }
    });
    
    for (const auto &audio_client_info: pairs) {
        auto vbox_container = new Container();
        vbox_container->type = vbox;
        vbox_container->wanted_bounds.h = 96 * config->dpi;
        vbox_container->wanted_bounds.w = FILL_SPACE;
        auto data = new option_data();
        if (!audio_client_info.icon_name.empty()) {
            std::vector<IconTarget> targets;
            targets.emplace_back(audio_client_info.icon_name);
            search_icons(targets);
            double size = 24;
            pick_best(targets, size * config->dpi);
            std::string icon_path = targets[0].best_full_path;
            if (!icon_path.empty()) {
                if (data->icon) {
                    cairo_surface_destroy(data->icon);
                    data->icon = nullptr;
                }
                load_icon_full_path(app, client, &data->icon, icon_path, size * config->dpi);
            }
        }
        data->unique_client_id = audio_client_info.uid;
        data->title = audio_client_info.text;
        data->peak = audio_client_info.peak;
        data->volume = audio_client_info.volume;
        data->muted = audio_client_info.muted;
        data->icon_name = audio_client_info.icon_name;
        vbox_container->user_data = data;
        
        auto label = new Container();
        label->wanted_bounds.h = 34 * config->dpi;
        label->wanted_bounds.w = FILL_SPACE;
        label->when_paint = paint_label;
        
        auto hbox_volume = new Container();
        hbox_volume->type = hbox;
        hbox_volume->wanted_bounds.h = FILL_SPACE;
        hbox_volume->wanted_bounds.w = FILL_SPACE;
        
        auto volume_icon = new Container();
        volume_icon->wanted_bounds.h = FILL_SPACE;
        volume_icon->wanted_bounds.w = 55 * config->dpi;
        volume_icon->when_paint = paint_volume_icon;
        volume_icon->when_clicked = toggle_mute;
        volume_icon->parent = hbox_volume;
        hbox_volume->children.push_back(volume_icon);
        auto volume_data = new ButtonData;
        volume_icon->user_data = volume_data;
        
        auto volume_bar = new Container();
        volume_bar->wanted_bounds.h = FILL_SPACE;
        volume_bar->wanted_bounds.w = FILL_SPACE;
        volume_bar->when_paint = paint_option;
        volume_bar->z_index = 1;
        volume_bar->when_drag_start = drag_force;
        volume_bar->when_drag = drag_whenever;
        volume_bar->when_drag_end = drag_force;
        volume_bar->when_mouse_down = drag_force;
        volume_bar->when_mouse_up = drag_force;
        volume_bar->draggable = true;
        volume_bar->parent = hbox_volume;
        volume_bar->when_fine_scrolled = [](AppClient *client, cairo_t *cr, Container *self, int scroll_x,
                                            int scroll_y, bool came_from_touchpad) -> void {
            auto current = get_current_time_in_ms();
            if (current - last_time_scrollpane_locked < 200)
                return;
            last_time_volume_locked = get_current_time_in_ms();
            scroll(client, cr, self, scroll_x, scroll_y, came_from_touchpad);
        };
        hbox_volume->children.push_back(volume_bar);
        
        auto volume_amount = new Container();
        volume_amount->wanted_bounds.h = FILL_SPACE;
        volume_amount->wanted_bounds.w = 65 * config->dpi;
        volume_amount->when_paint = paint_volume_amount;
        volume_amount->parent = hbox_volume;
        hbox_volume->children.push_back(volume_amount);
        
        label->parent = vbox_container;
        vbox_container->children.push_back(label);
        
        hbox_volume->parent = vbox_container;
        vbox_container->children.push_back(hbox_volume);
        
        vbox_container->parent = root;
        root->children.push_back(vbox_container);
    }
}

static bool thread_created = false;
static std::thread to_update;
static std::mutex to_update_mutex;
static std::condition_variable condition;
static bool actually_needs_to_wake = false;

void total_update() {
    if (auto client = client_by_name(app, "volume")) {
        if (auto vbox = container_by_name("vbox_container", client->root)) {
            for (auto child: vbox->children) {
                if (auto *data = (option_data *) child->user_data) {
                    for (auto c: audio_clients) {
                        if (c->index == data->unique_client_id) {
                            data->volume = c->get_volume();
                            c->cached_volume = data->volume;
                            data->peak = c->peak;
                            data->last_time_peak_set = c->last_time_peak_changed;
                            data->title = "";
                            std::string str1 = to_lower_and_remove_non_printable(c->subtitle);
                            std::string str2 = to_lower_and_remove_non_printable(c->title);
                            if (str1 != str2) {
                                for (auto ch: c->subtitle)
                                    if (isprint(ch))
                                        data->title += ch;
                                data->title += " - ";
                                for (auto ch: c->title)
                                    if (isprint(ch))
                                        data->title += ch;
                            } else {
                                data->title = c->title;
                            }
                            if (c->title == "Master") data->title = "Master";
                            
                            data->muted = c->is_muted();
                            break;
                        }
                    }
                }
            }
        }
        request_refresh(app, client);
    }
    
    if (auto client = client_by_name(app, "taskbar")) {
        if (auto con = container_by_name("volume", client->root)) {
            auto *data = (VolumeButton *) con->user_data;
            for (auto c: audio_clients) {
                if (c->is_master_volume()) {
                    data->muted = c->is_muted();
                    data->volume = c->get_volume();
                    break;
                }
            }
        }
    }
    update_taskbar_volume_icon();
}

void sort_containers() {
    // Re-order containers based on up-to-date containers.
    // And delete non-existing clients
    // Has running_mutex lock and audio lock
    if (auto client = client_by_name(app, "volume")) {
        if (auto vbox = container_by_name("vbox_container", client->root)) {
            for (auto child: vbox->children) {
                if (auto *data = (option_data *) child->user_data) {
                    data->sort_index = -1;
                    
                    for (int i = 0; i < audio_clients.size(); ++i) {
                        auto c = audio_clients[i];
                        if (c->index == data->unique_client_id) {
                            data->sort_index = audio_clients.size() - i;
                        }
                    }
                }
            }
            
            std::vector<Container *> temp;
            for (auto c: vbox->children)
                temp.push_back(c);
            vbox->children.clear();
            
            // remove temp container if sort_index stayed -1 as that means it wasn't found in audio_client
            for (int i = temp.size() - 1; i >= 0; --i) {
                auto c = temp[i];
                if (auto *data = (option_data *) c->user_data) {
                    if (data->sort_index == -1) {
                        delete c;
                        temp.erase(temp.begin() + i);
                    }
                }
            }
            
            // sort temp based on sort_index
            std::sort(temp.begin(), temp.end(), [](Container *a, Container *b){
                auto dataA = (option_data *) a->user_data;
                auto dataB = (option_data *) b->user_data;
                return dataA->sort_index > dataB->sort_index;
            });
            
            for (auto c: temp)
                vbox->children.push_back(c);
            client_layout(app, client);
            request_refresh(app, client);
        }
    }
    
    
}

void updates() {
//     if thread created
    if (!thread_created) {
        thread_created = true;
        to_update = std::thread([]() {
            try {
                defer(thread_created = false);
                while (audio_running) {
                    {
                        std::unique_lock<std::mutex> lock(to_update_mutex);
                        condition.wait(lock, []() { return actually_needs_to_wake; });
                        actually_needs_to_wake = false;
                    }
                    
                    std::lock_guard<std::mutex> second(app->running_mutex);
                    audio_read([]() {
                        total_update();
                        sort_containers();
                    });
                }
            } catch (...) {
                thread_created = false;
            }
        });
        to_update.detach();
        updates();
    } else {
        // wakeup
        std::unique_lock<std::mutex> lock(to_update_mutex);
        actually_needs_to_wake = true;
        condition.notify_one();
    }
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (ButtonData *) container->user_data;
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_rect(cr, container->real_bounds);
            set_argb(cr, config->color_apps_scrollbar_pressed_button);
            cairo_fill(cr);
        } else {
            set_rect(cr, container->real_bounds);
            set_argb(cr, config->color_apps_scrollbar_hovered_button);
            cairo_fill(cr);
        }
    } else {
        set_rect(cr, container->real_bounds);
        set_argb(cr, config->color_apps_scrollbar_default_button);
        cairo_fill(cr);
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 6 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_apps_scrollbar_pressed_button_icon);
        } else {
            set_argb(cr, config->color_apps_scrollbar_hovered_button_icon);
        }
    } else {
        set_argb(cr, config->color_apps_scrollbar_default_button_icon);
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(layout, data->text.data(), strlen("\uE83F"));
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_scroll_bg(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    set_rect(cr, container->real_bounds);
    ArgbColor color = config->color_apps_scrollbar_gutter;
    color.a = 1;
    set_argb(cr, color);
    cairo_fill(cr);
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
    right_bounds.w = std::max(right_bounds.w * 1, 2.0);
    right_bounds.x -= right_bounds.w;
    right_bounds.x -= 2 * (1 - 1);
    
    set_rect(cr, right_bounds);
    
    if (container->state.mouse_pressing) {
        ArgbColor color = config->color_apps_scrollbar_pressed_thumb;
        color.a = 1;
        set_argb(cr, color);
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        ArgbColor color = config->color_apps_scrollbar_hovered_thumb;
        color.a = 1;
        set_argb(cr, color);
    } else if (right_bounds.w == 2.0) {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        lighten(&color, 10);
        color.a = 1;
        set_argb(cr, color);
    } else {
        ArgbColor color = config->color_apps_scrollbar_default_thumb;
        color.a = 1;
        set_argb(cr, color);
    }
    
    cairo_fill(cr);
}

void closed_volume(AppClient *client) {
    meter_watching_stop();
}

void open_volume_menu() {
    unsigned long count = 0;
    if (!audio_running) {
        connected_message = "Failed to establish connection with an audio server [PulseAudio, Alsa]. (Click anywhere on this."
                            "window to retry)";
    } else {
        update_taskbar_volume_icon();
        
        audio([&count]() {
            if (audio_clients.empty()) {
                connected_message = "Successfully established connection to PulseAudio but found no "
                                    "clients or devices running";
            } else {
                meter_watching_start();
                count = audio_clients.size();
            }
        });
    }
    
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.w = 360 * config->dpi;
    if (!audio_running) {
        settings.h = 80 * config->dpi;
    } else {
        double maximum_visiually_pleasing_volume_menu_items_count = app->bounds.h * .70 / (config->dpi * 96);
        
        if (count < maximum_visiually_pleasing_volume_menu_items_count) {
            settings.h = count * (config->dpi * 96);
        } else {
            settings.w += 12;
            settings.h = maximum_visiually_pleasing_volume_menu_items_count * (config->dpi * 96);
        }
    }
    
    settings.slide_data[1] = 3;
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *container = container_by_name("volume", taskbar->root);
        if (container->real_bounds.x > taskbar->bounds->w / 2) {
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        } else {
            settings.x = 0;
            settings.slide_data[1] = 0;
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "volume";
        popup_settings.ignore_scroll = true;
        client_entity = taskbar->create_popup(popup_settings, settings);
    
        Container *root = client_entity->root;
        if (!audio_running) {
            root->when_clicked = retry_audio_connection;
            root->receive_events_even_if_obstructed = true;
        }
        ScrollPaneSettings s(config->dpi);
        ScrollContainer *scrollpane = make_newscrollpane_as_child(root, s);
        scrollpane->when_fine_scrolled = [](AppClient *client, cairo_t *cr, Container *self, int scroll_x, int scroll_y,
                                            bool came_from_touchpad) -> void {
            auto current = get_current_time_in_ms();
            if (current - last_time_volume_locked < 200)
                return;
            last_time_scrollpane_locked = get_current_time_in_ms();
            fine_scrollpane_scrolled(client, cr, self, scroll_x, scroll_y, came_from_touchpad);
        };
        Container *content = scrollpane->content;
        
        Container *right_thumb_container = scrollpane->right->children[1];
        right_thumb_container->parent->receive_events_even_if_obstructed_by_one = true;
        right_thumb_container->when_paint = paint_right_thumb;
    
        Container *top_arrow = scrollpane->right->children[0];
        top_arrow->when_paint = paint_arrow;
        auto *top_data = new ButtonData;
        top_data->text = "\uE971";
        top_arrow->user_data = top_data;
        Container *bottom_arrow = scrollpane->right->children[2];
        bottom_arrow->when_paint = paint_arrow;
        auto *bottom_data = new ButtonData;
        bottom_data->text = "\uE972";
        bottom_arrow->user_data = bottom_data;
    
        scrollpane->when_paint = paint_root;
        fill_root(client_entity, content);
        if (!audio_running) {
            content->wanted_bounds.h = 80;
        }
    
        client_show(app, client_entity);
        client_entity->fps = 90;
        client_entity->when_closed = closed_volume;
    }
}

void
adjust_volume_based_on_fine_scroll(AudioClient *audio_client, AppClient *client, cairo_t *cr, Container *container,
                                   int horizontal_scroll,
                                   int vertical_scroll, bool came_from_touchpad) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    last_time_volume_locked = get_current_time_in_ms();
    
    double current_volume = audio_client->get_volume();
    if (came_from_touchpad) {
        audio_client->cached_volume += vertical_scroll / 2400.0;
    } else {
        // When changing volume with mouse, round to next, 0, 5, 3, or 7
        int full = ((int) (audio_client->cached_volume * 100));
        int last_digit;
        do {
            if (vertical_scroll > 0) {
                full++;
            } else if (vertical_scroll < 0) {
                full--;
            }
            last_digit = full % 10;
        } while (!(last_digit == 0 || last_digit == 3 || last_digit == 5 || last_digit == 7));
        
        audio_client->cached_volume = ((double) full) / 100.0;
    }
    
    audio_client->cached_volume =
            audio_client->cached_volume < 0 ? 0 : audio_client->cached_volume > 1 ? 1 : audio_client->cached_volume;

    if (current_volume != audio_client->cached_volume) {
        if (audio_client->is_muted())
            audio_client->set_mute(false);
        
        audio_client->set_volume(audio_client->cached_volume);
        if (audio_client->is_master_volume())
            update_taskbar_volume_icon();
    }
}
