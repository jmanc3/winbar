//
// Created by jmanc3 on 3/3/23.
//

#include <pango/pangocairo.h>
#include "chatgpt.h"
#include "application.h"
#include "main.h"
#include "config.h"
#include "taskbar.h"
#include "dpi.h"

static std::string api_key;

struct RootData : UserData {
    bool hovering = false;
};

static void
paint_background(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (RootData *) container->user_data;
    
    set_rect(cr, container->real_bounds);
    ArgbColor color = config->color_volume_background;
    color.a = 1;
    set_argb(cr, color);
    cairo_fill(cr);
    
    if (api_key.empty()) {
        auto layout = get_cached_pango_font(cr, config->font, 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        static std::string text = "How to use chatgpt:"
                                  "\n\t\u2022 Visit <span foreground=\"#5577ff\">https://platform.openai.com/account/api-keys</span>"
                                  "\n\t\u2022 Login"
                                  "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                                  "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                                  "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
        
        double pad = 30 * config->dpi;
        pango_layout_set_width(layout, (container->real_bounds.w - pad * 2) * PANGO_SCALE);
        
        int start = 31;
        int end = 74;
        
        // markup text so that the link is blue
        pango_layout_set_markup(layout, text.data(), text.size());
        
        int width;
        int height;
        pango_layout_get_pixel_size(layout, &width, &height);
        
        set_argb(cr, config->color_volume_text);
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h * .33 - height / 2));
        
        int mouse_x =
                client->mouse_current_x - ((int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2));
        int mouse_y = client->mouse_current_y -
                      ((int) (container->real_bounds.y + container->real_bounds.h * .33 - height / 2));
        
        // get the index of the character at the mouse position
        int index;
        pango_layout_xy_to_index(layout, mouse_x * PANGO_SCALE, mouse_y * PANGO_SCALE, &index, nullptr);
        
        if (index >= start && index <= end) {
            data->hovering = true;
            
            static std::string text = "How to use chatgpt:"
                                      "\n\t\u2022 Visit <span foreground=\"#5577ff\" underline=\"single\">https://platform.openai.com/account/api-keys</span>"
                                      "\n\t\u2022 Login"
                                      "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                                      "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                                      "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
            
            if (container->state.mouse_pressing) {
                static std::string text = "How to use chatgpt:"
                                          "\n\t\u2022 Visit <span foreground=\"#225599\" underline=\"single\">https://platform.openai.com/account/api-keys</span>"
                                          "\n\t\u2022 Login"
                                          "\n\t\u2022 Click <span foreground=\"#338844\">\"Create new secret key\"</span>"
                                          "\n\t\u2022 Copy the API key to the file <span foreground=\"#338844\">~/.config/winbar/chatgpt.txt</span> (create it if doesn't exist)"
                                          "\n\n(Paid version (1$ for 500000 tokens) gives instant results, the free version is the same as the website)";
                pango_layout_set_markup(layout, text.data(), text.size());
            } else {
                pango_layout_set_markup(layout, text.data(), text.size());
            }
        } else {
            data->hovering = false;
        }
        
        pango_cairo_show_layout(cr, layout);
        pango_layout_set_width(layout, -1);
    }
}

static void clicked_no_api(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (RootData *) container->user_data;
    if (data->hovering) {
        system("xdg-open https://platform.openai.com/account/api-keys");
    }
}

static void fill_or_replace_as_if_no_key(AppClient *client) {
    client->root->when_paint = paint_background;
    client->root->when_clicked = clicked_no_api;
    if (client->root->user_data == nullptr) {
        client->root->user_data = new RootData();
    }
}

static void fill_or_replace_as_if_key(AppClient *client) {
    client->root->when_paint = paint_background;
    if (client->root->user_data == nullptr) {
        client->root->user_data = new RootData();
    }
}

static void load_api_key(AppClient *client) {
    char *home = getenv("HOME");
    std::string path = std::string(home) + "/.config/winbar/chatgpt.txt";
    
    // read file at path to std::string
    auto file = fopen(path.data(), "r");
    if (!file) {
        api_key = "";
        fill_or_replace_as_if_no_key(client);
        return;
    }
    
    char buffer[1024 * 4];
    size_t len = fread(buffer, 1, 1024 * 4, file);
    fclose(file);
    
    std::string key = std::string(buffer, len);
    
    // trim all whitespace
    key.erase(std::remove_if(key.begin(), key.end(), isspace), key.end());
    
    api_key = key;
    
    if (api_key.empty()) {
        fill_or_replace_as_if_no_key(client);
    } else {
        fill_or_replace_as_if_key(client);
    }
}

void start_chatgpt_menu() {
    if (auto *client = client_by_name(app, "chatgpt_menu")) {
        xcb_window_t active_window = get_active_window();
        
        if (active_window == client->window && client->mapped) {
            client_hide(app, client);
            return;
        }
        
        load_api_key(client);
        
        client_show(app, client);
        xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, client->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_window_t window = client->window;
        std::thread t([window]() -> void {
            xcb_ewmh_request_change_active_window(&app->ewmh,
                                                  app->screen_number,
                                                  window,
                                                  XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
                                                  XCB_CURRENT_TIME,
                                                  XCB_NONE);
            xcb_flush(app->connection);
        });
        t.detach();
        return;
    }
    
    double w = 800;
    double h = 600;
    for (auto s: screens) {
        if (s->is_primary) {
            w = s->width_in_pixels * .7;
            h = s->height_in_pixels * .7;
            break;
        }
    }
    
    Settings settings;
    settings.w = w;
    settings.h = h;
    settings.skip_taskbar = true;
    settings.on_close_is_unmap = true;
    auto *client = client_new(app, settings, "chatgpt_menu");
    std::string title = "ChatGPT";
    // set title of window
    xcb_change_property(app->connection,
                        XCB_PROP_MODE_REPLACE,
                        client->window,
                        get_cached_atom(app, "_NET_WM_NAME"),
                        get_cached_atom(app, "UTF8_STRING"),
                        8,
                        title.size(),
                        title.c_str());
    load_api_key(client);
    client_show(app, client);
}