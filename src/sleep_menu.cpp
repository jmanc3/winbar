
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
        //std::cout << "Signal " << signal << " sent to process " << pid << std::endl;
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
        if (container)
            settings.x = taskbar->bounds->x + container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2;
        
        settings.y = taskbar->bounds->y - settings.h;
        
        // Keep on screen
        if (settings.x < taskbar->bounds->x)
            settings.x = taskbar->bounds->x;
        if (settings.x + settings.w > taskbar->bounds->x + taskbar->bounds->w)
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
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
                
                auto color = config->color_volume_background;
                color.a = 1;
                draw_colored_rect(client, color, c->real_bounds);
                
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
                
                double close_height = 32 * config->dpi;
                double width = sleeper->frozen->width;
                
                draw_gl_texture(client, sleeper->gsurf, sleeper->frozen->surface, c->real_bounds.x + c->real_bounds.w / 2 - width / 2, close_height + c->real_bounds.y);
                
                //cairo_restore(cr);
                auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, 1, 1, 1, 1, sleeper->frozen->title);
                auto bounds = c->real_bounds;
                bounds.w -= 8 * config->dpi;
                draw_clip_begin(client, bounds);
                f->draw_text_end(c->real_bounds.x + 8 * config->dpi, c->real_bounds.y + close_height / 2 - h / 2);
                draw_clip_end(client);
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