
#include <cstdio>
#include "application.h"
#include "main.h"
#include "components.h"
#include "config.h"
#include "drawer.h"
#include "windows_selector.h"
#include "settings_menu.h"

static void send_signal(pid_t pid, int signal) {
    if (kill(pid, signal) == 0) {
        std::cout << "Signal " << signal << " sent to process " << pid << std::endl;
    }
}

struct Sleeper {
    SleptWindows *frozen = nullptr;
    gl_surface *gsurf = nullptr;
};

void start_sleep_menu() {
    option_width = 217 * 1.2 * config->dpi;
    option_height = 144 * 1.2 * config->dpi;
 
    Settings settings;
    settings.w = option_width;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.force_position = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    int max = std::floor((app->screen->height_in_pixels - config->taskbar_height) / option_height);
    int num = slept.size();
    if (num > max)
        num = max;
    settings.h = num * option_height;
    
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *container = container_by_name("frozen", taskbar->root);
        // TOOD: center position
        if (container) {
            settings.x = taskbar->bounds->x + container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2;
        }
        
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.force_position = true;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "sleep_menu";
        popup_settings.ignore_scroll = true;
        auto client = taskbar->create_popup(popup_settings, settings);
 
        auto real_root = client->root;
        ScrollPaneSettings s(config->dpi);
        ScrollContainer *scrollpane = make_newscrollpane_as_child(real_root, s);
 
        auto root = scrollpane->content;
        root->type = ::vbox;
        
        root->when_paint =  [](AppClient *client, cairo_t *, Container *c) {
            auto color = config->color_volume_background;
            color.a = 1;
            draw_colored_rect(client, color, c->real_bounds);
        };
        root->receive_events_even_if_obstructed = true;
        
        for (auto frozen: slept) {
            auto pane = root->child(FILL_SPACE, option_height);
            pane->skip_delete = true;
            auto s = new Sleeper;
            s->frozen = frozen;
            pane->user_data = s;
            pane->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
                auto sleeper = (Sleeper *) c->user_data;
                if (!sleeper)
                    return;
                
                if (c->state.mouse_hovering || c->state.mouse_pressing) {
                    auto color = config->color_apps_pressed_item;
                    if (c->state.mouse_pressing) {
                        color = config->color_apps_pressed_item;
                    } else {
                        color = config->color_apps_hovered_item;
                    }
                    draw_colored_rect(client, color, c->real_bounds);
                }
                
                if (!sleeper->gsurf)
                    sleeper->gsurf = new gl_surface();
                
                double close_width = 32 * config->dpi;
                double close_height = 32 * config->dpi;
                
                double pad = 8 * config->dpi;
                double target_width = option_width - pad * 2;
                double target_height = option_height - pad;
                double scale_w = target_width / sleeper->frozen->width;
                double scale_h = target_height / sleeper->frozen->height;
                if (scale_w < scale_h) {
                    scale_h = scale_w;
                } else {
                    scale_w = scale_h;
                }
                double width = sleeper->frozen->width * scale_w;
                double option_min_width = 100 * 1.2 * config->dpi;
                if (width < option_min_width) {
                    width = option_min_width;
                }
                
                draw_gl_texture(client, sleeper->gsurf, sleeper->frozen->surface, c->real_bounds.w / 2 - (width / 2) + pad, close_height + c->real_bounds.y);
                
                //cairo_restore(cr);
                auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, 1, 1, 1, 1, sleeper->frozen->title);
                f->draw_text_end(c->real_bounds.x + 4 * config->dpi, c->real_bounds.y + close_height / 2 - h / 2);
            };
            pane->when_clicked = [](AppClient *client, cairo_t *, Container *c) {
                auto sleeper = (Sleeper *) c->user_data;
                if (!sleeper)
                    return;
                auto frozen = sleeper->frozen;
                for (int i = 0; i < slept.size(); i++) {
                    if (slept[i] == frozen) {
                        slept.erase(slept.begin() + i);
                        break;
                    }
                }
                
                xcb_map_window(app->connection, frozen->window_id);
                send_signal(frozen->pid, SIGCONT);
                delete frozen;
                c->user_data = nullptr;
                merge_order_with_taskbar();
                client_close_threaded(app, client);
            };
        }
        
        client_show(app, client);
    }
}