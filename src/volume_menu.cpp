//
// Created by jmanc3 on 3/8/20.
//
#include "volume_menu.h"

#include "audio.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "components.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include <application.h>
#include <iostream>
#include <math.h>
#include <pango/pangocairo.h>
#include <utility.h>

static AppClient *client_entity;
static std::string connected_message = "";

enum option_type {
    client,
    output
};

static double opacity_diff = .5;

static double opacity_thresh = 200;

// TODO: every frame we should resize and remake containers based on data in audio_clients and
// audio_outputs since it can change
//  behind our hands and lead to a crash because we index into the list instead of doing something
//  smarter like have the actual index of the thing
// TODO: fix muting the output leading to a pause

class option_data : UserData {
public:
    int unique_client_id = -100;
    long last_update = get_current_time_in_ms();
    double rolling_avg = 0;
    
    Audio_Client *client() const {
        for (auto c: audio_clients)
            if (c->unique_id() == unique_client_id)
                return c;
        return nullptr;
    };
    
    ~option_data() {}
    
    float previous_end_percent = 0;
};

class ButtonData : public IconButton {
public:
    std::string text;
};

static void
rounded_rect(cairo_t *cr, double corner_radius, double x, double y, double width, double height) {
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
fill_root(Container *root);

static void
paint_root(AppClient *client_entity, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client_entity, config->color_volume_background));
    cairo_fill(cr);
    
    if (audio_backend_data->audio_backend == Audio_Backend::NONE) {
        PangoLayout *layout =
                get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        int width;
        int height;
        pango_layout_set_text(layout, connected_message.c_str(), -1);
        pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
        pango_layout_set_width(layout, container->real_bounds.w * PANGO_SCALE);
        pango_layout_get_pixel_size(layout, &width, &height);
        
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
    Audio_Client *client = data->client();
    if (!client) return;
    
    double scalar = client->get_volume();
    int val = (int) (scalar * 100);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets", 20 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, "\uEBC5", strlen("\uE83F"));
    pango_layout_get_pixel_size(layout, &width, &height);
    
    if (!client->is_muted()) {
        ArgbColor volume_bars_color = ArgbColor(.4, .4, .4, 1);
        set_argb(cr, volume_bars_color);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    }
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    if (client->is_muted()) {
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
    
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_taskbar_button_icons);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_volume_amount(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio_Client *client = data->client();
    if (!client) return;

//    printf("is mater: %d, volume: %f, muted: %d\n", client->is_master, client->get_volume(), client->is_muted());
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 17 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    double scalar = client->get_volume();
    
    std::string text = std::to_string((int) (std::round(scalar * 100)));
    
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_label(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->user_data);
    Audio_Client *client = data->client();
    if (!client) return;
    
    std::string text = client->title;
    if (!client->subtitle.empty())
        text = client->subtitle;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr, container->real_bounds.x + 13, container->real_bounds.y + 12);
    
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_attributes(layout, nullptr);
}

static void
paint_option(AppClient *client_entity, cairo_t *cr, Container *container) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio_Client *client = data->client();
    if (!client) return;
    
    double marker_height = 24 * config->dpi;
    double marker_width = 8 * config->dpi;
    double marker_position = client->get_volume() * container->real_bounds.w;
    
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
    
    std::lock_guard<std::mutex> lock(client->pulseaudio_mutex);
    
    long current_time = get_current_time_in_ms();
    long time_since_last_update = current_time - client->last_update;
    float endPercent = time_since_last_update / (client->time_between_last_sample + 1);
    if (endPercent > 1)
        endPercent = 1;
    float startPercent = endPercent - 0.2;
    if (startPercent < 0)
        startPercent = 0;
    if (endPercent < data->previous_end_percent) {
        startPercent = 0;
    }
    data->previous_end_percent = endPercent;
    
    int startIndex = client->length * startPercent;
    int endIndex = client->length * endPercent;
    
    double sum = 0;
    for (int i = startIndex; i < endIndex; i++) {
        sum += client->buffer[i] + 128;
    }
    float i1 = endIndex - startIndex;
    client->peak = (sum / i1) / 255.0f;
    
    if (data->rolling_avg < client->peak) {
        data->rolling_avg = client->peak;
        client_create_animation(app, client_entity, &data->rolling_avg, 250, nullptr, 0);
    }
    
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
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio_Client *client = data->client();
    if (!client) return;
    
    client->set_mute(!client->is_muted());
    update_taskbar_volume_icon();
}

static void
scroll(AppClient *client_entity, cairo_t *cr, Container *container, int scroll_x, int scroll_y) {
    auto data = static_cast<option_data *>(container->parent->parent->user_data);
    Audio_Client *client = data->client();
    if (!client) return;
    
    double new_volume = client->get_volume() + (.05 * scroll_y) + (.05 * -scroll_x);
    if (new_volume < 0) {
        new_volume = 0;
    } else if (new_volume > 1) {
        new_volume = 1;
    }
    
    if (new_volume != client->get_volume()) {
        if (client->is_muted())
            client->set_mute(false);
        
        client->set_volume(new_volume);
        if (client->is_master_volume())
            update_taskbar_volume_icon();
    }
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
    Audio_Client *client = data->client();
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
    
    if (new_volume != client->get_volume()) {
        if (client->is_muted())
            client->set_mute(false);
        
        client->set_volume(new_volume);
        if (client->is_master_volume())
            update_taskbar_volume_icon();
    }
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
    if (auto client = client_by_name(app, "volume")) {
        client_close(app, client);
        open_volume_menu();
    }
}

static void
retry_audio_connection(AppClient *client_entity, cairo_t *cr, Container *container) {
    connected_message = "Retrying...";
    client_paint(app, client_entity);
    audio_start(app);
    if (audio_backend_data->audio_backend == Audio_Backend::NONE) {
        connected_message = "Still failed. (You can try again, or maybe you have something set up wrong)";
    } else {
        connected_message = "Success";
        app_timeout_create(app, nullptr, 0, restart_volume_timeout, nullptr);
    }
}

void fill_root(Container *root) {
    root->when_paint = paint_root;
    root->type = vbox;
    
    if (audio_backend_data->audio_backend == Audio_Backend::NONE)
        root->when_clicked = retry_audio_connection;
    
    for (int i = 0; i < audio_clients.size(); i++) {
        auto vbox_container = new Container();
        vbox_container->type = vbox;
        vbox_container->wanted_bounds.h = 96 * config->dpi;
        vbox_container->wanted_bounds.w = FILL_SPACE;
        auto data = new option_data();
        auto uid = audio_clients[i]->unique_id();
        data->unique_client_id = uid;
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
        volume_icon->when_scrolled = scroll;
        volume_icon->parent = hbox_volume;
        hbox_volume->children.push_back(volume_icon);
        auto volume_data = new ButtonData;
        volume_icon->user_data = volume_data;
        
        auto volume_bar = new Container();
        volume_bar->wanted_bounds.h = FILL_SPACE;
        volume_bar->wanted_bounds.w = FILL_SPACE;
        volume_bar->when_paint = paint_option;
        volume_bar->when_drag_start = drag_force;
        volume_bar->when_drag = drag_whenever;
        volume_bar->when_drag_end = drag_force;
        volume_bar->when_mouse_down = drag_force;
        volume_bar->when_mouse_up = drag_force;
        volume_bar->draggable = true;
        volume_bar->parent = hbox_volume;
        volume_bar->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_bar);
        
        auto volume_amount = new Container();
        volume_amount->wanted_bounds.h = FILL_SPACE;
        volume_amount->wanted_bounds.w = 65 * config->dpi;
        volume_amount->when_paint = paint_volume_amount;
        volume_amount->parent = hbox_volume;
        volume_amount->when_scrolled = scroll;
        hbox_volume->children.push_back(volume_amount);
        
        label->parent = vbox_container;
        vbox_container->children.push_back(label);
        
        hbox_volume->parent = vbox_container;
        vbox_container->children.push_back(hbox_volume);
        
        vbox_container->parent = root;
        root->children.push_back(vbox_container);
    }
}

void updates() {
    if (valid_client(app, client_entity)) {
        request_refresh(app, client_entity);
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
            get_cached_pango_font(cr, "Segoe MDL2 Assets", 6 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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
    pango_layout_get_pixel_size(layout, &width, &height);
    
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
    unhook_stream();
}

void open_volume_menu() {
    audio_start(app);
    if (audio_backend_data->audio_backend == Audio_Backend::NONE) {
        connected_message = "Failed to establish connection with an audio server [PulseAudio, Alsa]. (Click anywhere on this."
                            "window to retry)";
    } else if (audio_backend_data->audio_backend == Audio_Backend::PULSEAUDIO ||
               audio_backend_data->audio_backend == Audio_Backend::ALSA) {
        audio_update_list_of_clients();
        hook_up_stream();
        
        if (audio_clients.empty()) {
            connected_message = "Successfully established connection to PulseAudio but found no "
                                "clients or devices running";
        }
    }
    if (audio_backend_data->audio_backend != Audio_Backend::NONE) {
        audio_state_change_callback(updates);
    }
    
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.w = 360 * config->dpi;
    if (audio_backend_data->audio_backend == Audio_Backend::NONE) {
        settings.h = 80 * config->dpi;
    } else {
        unsigned long count = audio_clients.size();
        double maximum_visiually_pleasing_volume_menu_items_count = app->bounds.h * .70 / (config->dpi * 96);
        
        if (count < maximum_visiually_pleasing_volume_menu_items_count) {
            settings.h = count * (config->dpi * 96);
        } else {
            settings.w += 12;
            settings.h = maximum_visiually_pleasing_volume_menu_items_count * (config->dpi * 96);
        }
    }
    
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "volume";
        popup_settings.ignore_scroll = true;
        client_entity = taskbar->create_popup(popup_settings, settings);
        
        Container *root = client_entity->root;
        ScrollPaneSettings s;
        s.right_width = 12 * config->dpi;
        s.right_arrow_height = 12 * config->dpi;
        Container *scrollpane = make_scrollpane(root, s);
        scrollpane->when_scrolled = nullptr;
        Container *content = scrollpane->child(::vbox, FILL_SPACE, FILL_SPACE);
        
        Container *right_thumb_container = scrollpane->parent->children[0]->children[1];
        right_thumb_container->parent->receive_events_even_if_obstructed_by_one = true;
        right_thumb_container->when_paint = paint_right_thumb;
        
        Container *top_arrow = scrollpane->parent->children[0]->children[0];
        top_arrow->when_paint = paint_arrow;
        auto *top_data = new ButtonData;
        top_data->text = "\uE971";
        top_arrow->user_data = top_data;
        Container *bottom_arrow = scrollpane->parent->children[0]->children[2];
        bottom_arrow->when_paint = paint_arrow;
        auto *bottom_data = new ButtonData;
        bottom_data->text = "\uE972";
        bottom_arrow->user_data = bottom_data;
    
        fill_root(content);
        if (audio_backend_data->audio_backend != Audio_Backend::NONE) {
            content->wanted_bounds.h = true_height(scrollpane) + true_height(content);
        } else {
            content->wanted_bounds.h = 80;
        }
    
        client_show(app, client_entity);
        client_entity->fps = 120;
        client_entity->when_closed = closed_volume;
        client_register_animation(app, client_entity);
    }
}

void update_volume_menu() {
    updates();
}
