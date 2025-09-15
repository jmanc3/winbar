//
// Created by jmanc3 on 2/1/20.
//

#include "root.h"
#include "app_menu.h"
#include "application.h"
#include "components.h"
#include "main.h"
#include "taskbar.h"
#include "utility.h"
#include "config.h"
#include "drawer.h"

#include <mutex>
#include <cmath>

void update_stacking_order() {
    xcb_get_property_cookie_t cookie =
            xcb_get_property(app->connection,
                             0,
                             app->screen->root,
                             get_cached_atom(app, "_NET_CLIENT_LIST_STACKING"),
                             XCB_ATOM_WINDOW,
                             0,
                             -1);
    
    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, NULL);
    
    long windows_count = xcb_get_property_value_length(reply) / sizeof(xcb_window_t);
    auto *windows = (xcb_window_t *) xcb_get_property_value(reply);
    
    stacking_order_changed(windows, windows_count);
    free(reply);
}

void update_active_window() {
    xcb_get_property_cookie_t cookie = xcb_get_property(app->connection,
                                                        0,
                                                        app->screen->root,
                                                        get_cached_atom(app, "_NET_ACTIVE_WINDOW"),
                                                        XCB_ATOM_WINDOW,
                                                        0,
                                                        -1);
    
    xcb_get_property_reply_t *reply = xcb_get_property_reply(app->connection, cookie, NULL);
    if (!reply)
        return;
    
    long windows_count = xcb_get_property_value_length(reply) / sizeof(xcb_window_t);
    auto *windows = (xcb_window_t *) xcb_get_property_value(reply);
    
    active_window_changed(windows[0]);
    
    free(reply);
}

static bool
root_event_handler(App *app, xcb_generic_event_t *event, xcb_window_t) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_PROPERTY_NOTIFY: {
            auto *e = (xcb_property_notify_event_t *) event;
//            const xcb_get_atom_name_cookie_t &cookie = xcb_get_atom_name(app->connection, e->atom);
//            xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(app->connection, cookie, NULL);
//            char *name = xcb_get_atom_name_name(reply);
//            printf("ATOM: %s\n", name);
            
            if (e->atom == get_cached_atom(app, "_NET_CLIENT_LIST_STACKING")) {
                update_stacking_order();
            }
            if (e->atom == get_cached_atom(app, "_NET_CURRENT_DESKTOP")) {
            //if (e->atom == get_cached_atom(app, "_NET_WM_DESKTOP")) {
                on_desktop_change();
            }
            update_active_window();
            break;
        }
        case XCB_BUTTON_PRESS: {
            break;
        }
        case XCB_BUTTON_RELEASE: {
            break;
        }
        case XCB_MOTION_NOTIFY: {
            break;
        }
        case XCB_CONFIGURE_NOTIFY: {
            auto *e = (xcb_configure_notify_event_t *) event;
            app->bounds.w = e->width;
            app->bounds.h = e->height;
            break;
        }
    }
    
    return false;
}

void correct_position_based_on_extent(AppClient *client, std::string extent) {
    auto taskbar = client_by_name(app, "taskbar");
    if (!taskbar) return;
    auto cookie = xcb_get_property(app->connection, 0, client->window,
                                   get_cached_atom(app, extent),
                                   XCB_ATOM_CARDINAL, 0, 4);
    auto reply = xcb_get_property_reply(app->connection, cookie, nullptr);
    auto pad = 12 * config->dpi;
    
    if (reply) {
        int length = xcb_get_property_value_length(reply);
        if (length != 0) {
            auto frame_extents = static_cast<uint32_t *>(xcb_get_property_value(
                    reply));
            auto left = (int) frame_extents[0];
            auto top = (int) frame_extents[2];
            auto bottom = (int) frame_extents[3];
            client_set_position(app, client, taskbar->bounds->x + pad - left,
                                taskbar->bounds->y - client->bounds->h - pad - (top + bottom));
        }
        free(reply);
    }
}

bool on_message(App *, xcb_generic_event_t *event, xcb_window_t window) {
    if (auto run_client = client_by_name(app, "winbar_run")) {
        if (run_client->window == window) {
            switch (XCB_EVENT_RESPONSE_TYPE(event)) {
                case XCB_PROPERTY_NOTIFY: {
                    auto e = (xcb_property_notify_event_t *) event;
                    if (e->atom == get_cached_atom(app, "_GTK_FRAME_EXTENTS")) {
                        correct_position_based_on_extent(run_client, "_GTK_FRAME_EXTENTS");
                    } else if (e->atom == get_cached_atom(app, "_NET_FRAME_EXTENTS")) {
                        correct_position_based_on_extent(run_client, "_NET_FRAME_EXTENTS");
                    }
                    break;
                }
            }
        }
    }
    return false;
}

void start_run_window() {
    if (auto winbar_run = client_by_name(app, "winbar_run")) {
        if (auto textarea = container_by_name("textarea", winbar_run->root)) {
            auto *data = (TextAreaData *) textarea->user_data;
            data->state->text = "";
            data->state->cursor = 0;
            data->state->selection_x = 0;
            data->state->redo_stack.clear();
            data->state->undo_stack.clear();
        }
        return;
    }
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PangoLayout *layout =
                get_cached_pango_font(taskbar->cr, config->font, 10 * config->dpi, PANGO_WEIGHT_NORMAL);
        int width;
        int height;
        pango_layout_set_text(layout, "Open:", -1);
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        auto pad = 9 * config->dpi;
        static auto search_pad = 4 * config->dpi;
        
        auto options = Settings();
        options.force_position = true;
        options.decorations = true;
        options.w = 400 * config->dpi;
        options.h = height + pad * 2 + search_pad * 2;
        options.x = taskbar->bounds->x + pad;
        options.y = taskbar->bounds->y - options.h - pad;
        
        PopupSettings popup_settings;
        popup_settings.name = "winbar_run";
        popup_settings.ignore_scroll = true;
        popup_settings.takes_input_focus = true;
        auto run_client = taskbar->create_popup(popup_settings, options);
        auto root = run_client->root;
        static auto fg = config->color_apps_background;
        static auto bg = config->color_apps_text;
        
        root->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
            draw_colored_rect(client, correct_opaqueness(client, bg), container->real_bounds);
        };
        std::string title = "Run";
        xcb_ewmh_set_wm_name(&app->ewmh, run_client->window, title.length(), title.c_str());
        root->type = layout_type::hbox;
        root->wanted_pad = Bounds(pad, pad, pad, pad);
        root->spacing = pad * 2;
        root->alignment = ALIGN_CENTER;
        
        auto label = root->child(width, FILL_SPACE);
        label->when_paint = [](AppClient *, cairo_t *cr, Container *container) {
            PangoLayout *layout =
                    get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
            int width;
            int height;
            pango_layout_set_text(layout, "Open:", -1);
            pango_layout_get_pixel_size_safe(layout, &width, &height);
            
            set_argb(cr, fg);
            cairo_move_to(cr,
                          container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                          container->real_bounds.y + container->real_bounds.h / 2 - height / 2 - (search_pad / 2));
            pango_cairo_show_layout(cr, layout);
        };
        
        auto text_box = root->child(FILL_SPACE, height + (search_pad * 2));
        auto area_options = TextAreaSettings(config->dpi);
        area_options.color = fg;
        area_options.color_cursor = fg;
        area_options.bottom_show_amount = ScrollShow::SNever;
        area_options.bottom_show_amount = ScrollShow::SNever;
        area_options.font_size__ = 10 * config->dpi;
        area_options.single_line = true;
        area_options.pad = Bounds(search_pad, search_pad, search_pad, search_pad);
        auto textarea = make_textarea(app, run_client, text_box, area_options);
        textarea->parent->when_paint = [](AppClient *client, cairo_t *cr, Container *container) {
            draw_margins_rect(client, config->color_search_accent, container->real_bounds, std::round(1 * config->dpi), 0);
        };
        set_active(run_client, textarea->parent, true);
        
        textarea->name = "textarea";
        textarea->when_key_event = [](AppClient *client, cairo_t *cr, Container *self, bool is_string,
                                      xkb_keysym_t keysym, char string[64], uint16_t mods,
                                      xkb_key_direction direction) {
            if (keysym == XK_Escape)
                client_close_threaded(app, client);
            if (keysym == XK_Return) {
                client_close_threaded(app, client);
                
                auto *data = (TextAreaData *) self->user_data;
                launch_command(data->state->text);
            }
            if (!(mods & XCB_MOD_MASK_4)) {
                textarea_handle_keypress(client, self, is_string, keysym, string, mods, direction);
            }
        };
        app_create_custom_event_handler(app, run_client->window, on_message);
        client_show(app, run_client);
    }
    
}

void meta_pressed(unsigned int num) {
    if (num == 0) {
        xcb_ungrab_button(app->connection, XCB_BUTTON_INDEX_ANY, app->screen->root, XCB_MOD_MASK_ANY);
        
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
        
        if (auto client = client_by_name(app, "app_menu")) {
            client_close(app, client);
            set_textarea_inactive();
        } else {
            start_app_menu();
        }
    } else {
        taskbar_launch_index(num - 10);
    }
}

void root_start(App *app) {
    auto *handler = new Handler;
    handler->event_handler = root_event_handler;
    handler->target_window = app->screen->root;
    app->handlers.push_back(handler);
    
    const uint32_t values[] = {XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_change_window_attributes(app->connection, app->screen->root, XCB_CW_EVENT_MASK, values);
    
    xcb_flush(app->connection);
}
