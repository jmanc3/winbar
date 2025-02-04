//
// Created by jmanc3 on 2/16/23.
//

#include "plugins_menu.h"
#include "taskbar.h"
#include "config.h"
#include "main.h"
#include "icons.h"
#include "components.h"
#include "drawer.h"
#include "dpi.h"

#include <filesystem>
#include <pango/pangocairo.h>
#include <iostream>
#include <utility>
#include <cassert>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

enum struct PluginContainerType {
    LABEL,
    BUTTON,
    FIELD,
    COMBOBOX,
    FILLER,
};

struct PluginData : IconButton {
    Container *root = new Container(::vbox, FILL_SPACE, FILL_SPACE);
    Subprocess *cc = nullptr;
    std::string icon;
    std::string icon_text;
    std::string path;
    ArgbColor icon_text_color = config->color_taskbar_button_icons;
    bool tried_to_load = false;
    
    ~PluginData() {
        if (root)
            delete root;
    }
};

struct PluginContainerData : UserData {
    PluginContainerType type = PluginContainerType::LABEL;
    
    std::string text; // for buttons, labels, and fields
    std::string prompt; // for field
    std::vector<std::string> items; // for combobox, item 0 is the selected item
    bool disabled = false;
};

struct ComboBoxChildData : PluginContainerData {
    Container *combobox = nullptr;
    unsigned long i = std::string::npos;
};

struct ComboBoxPopup : UserData {
    std::string filter_text;
    long last_keypress_time = get_current_time_in_ms();
    Bounds start_bounds;
};

void paint_root(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (PluginData *) container->user_data;
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
}


static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container, std::string text, bool disabled);

void paint_combo_box_popup(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (ComboBoxPopup *) container->user_data;
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_search_accent));
    cairo_fill(cr);
    
    
    if (auto *scroll = (ScrollContainer *) container_by_name("scroll", container)) {
        for (auto *child: scroll->content->children)
            if (child->exists)
                return;
    }
    
    paint_centered_label(client, cr, container, "No results for: \"" + data->filter_text + "\"", false);
}

void add_plugin(const std::string &path);

static void
clicked_plugin(AppClient *, cairo_t *cr, Container *container) {
    auto *data = (PluginData *) container->user_data;
    if (!data->invalid_button_down) {
        if (container->state.mouse_button_pressed == XCB_BUTTON_INDEX_3) {
//            add_plugin(data->path);
            return;
        }
        
        Settings settings;
        settings.decorations = false;
        settings.skip_taskbar = true;
        settings.sticky = true;
        settings.w = 376 * config->dpi;
        settings.h = true_height(data->root) + data->root->real_bounds.h;
        settings.x = app->bounds.w - settings.w;
        settings.y = app->bounds.h - settings.h - config->taskbar_height;
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
            popup_settings.name = "plugin_menu";
            popup_settings.ignore_scroll = true;
            popup_settings.takes_input_focus = true;
            auto client = taskbar->create_popup(popup_settings, settings);
            
            delete client->root;
            client->root = data->root;
            client->root->user_data = data;
            client->root->when_paint = paint_root;
            client->auto_delete_root = false;
            
            data->cc->write("window_shown");
            client_show(app, client);
            client->when_closed = [](AppClient *client) {
                auto *data = (PluginData *) client->root->user_data;
                data->cc->write("window_closed");
            };
        }
    }
}

static void
paint_message(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (PluginContainerData *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), -1);
    pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD);
    pango_layout_set_width(layout, (container->real_bounds.w) * PANGO_SCALE);
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container, std::string text, bool disabled) {
    if (!disabled) {
        set_rect(cr, container->real_bounds);
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                set_argb(cr, config->color_wifi_pressed_button);
            } else {
                set_argb(cr, config->color_wifi_hovered_button);
            }
        } else {
            set_argb(cr, config->color_wifi_default_button);
        }
        cairo_fill(cr);
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(text);
    pango_layout_set_text(layout, message.data(), message.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    set_argb(cr, config->color_volume_text);
    if (disabled) {
        ArgbColor color = config->color_volume_text;
        color.a = .41;
        set_argb(cr, color);
    }
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_centered_label(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (PluginContainerData *) container->user_data;
    paint_centered_label(client, cr, container, data->text, data->disabled);
}

static void
paint_combobox_item(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (ComboBoxChildData *) container->user_data;
    if (container->parent->children[container->parent->children.size() - 1] == container) {
        set_rect(cr, container->real_bounds);
        set_argb(cr, config->color_wifi_hovered_button);
        cairo_fill(cr);
    } else {
        set_rect(cr, container->real_bounds);
        if (container->state.mouse_pressing || container->state.mouse_hovering) {
            if (container->state.mouse_pressing) {
                set_argb(cr, config->color_wifi_pressed_button);
            } else {
                set_argb(cr, config->color_wifi_hovered_button);
            }
        } else {
            set_argb(cr, config->color_wifi_default_button);
        }
        cairo_fill(cr);
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string message(data->text);
    pango_layout_set_text(layout, message.data(), message.length());
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - ((logical.width / PANGO_SCALE) / 2),
                  container->real_bounds.y + container->real_bounds.h / 2 - ((logical.height / PANGO_SCALE) / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_combobox(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (PluginContainerData *) container->user_data;
    if (!data->items.empty()) {
        paint_centered_label(client, cr, container, data->items[0], data->disabled);
    }
    
    draw_margins_rect(client, config->color_wifi_hovered_button, container->real_bounds, 2 * config->dpi, 0);
}

struct Tokenizer {
    std::string text;
    int index = 0;
    bool error = false;
    std::string plugin_name;
    
    explicit Tokenizer(std::string text) {
        this->text = std::move(text);
    }
};

enum struct TokenType {
    START,
    ERROR,
    EQUALS,
    COMMA,
    STRING,
    IDENTIFIER,
    END,
};

struct Token {
    std::string text;
    TokenType type = TokenType::START;
};

void plugin_error(Tokenizer &tokenizer, const char *buffer);

void eat_all_whitespace(Tokenizer &tokenizer) {
    while (tokenizer.index < tokenizer.text.length() && isspace(tokenizer.text[tokenizer.index])) {
        tokenizer.index++;
    }
}

Token get_token(Tokenizer &tokenizer) {
    eat_all_whitespace(tokenizer);
    
    Token token;
    if (tokenizer.index >= tokenizer.text.length()) {
        token.type = TokenType::END;
        return token;
    } else if (tokenizer.text[tokenizer.index] == '=') {
        token.type = TokenType::EQUALS;
        token.text = "=";
        tokenizer.index++;
    } else if (tokenizer.text[tokenizer.index] == ',') {
        token.type = TokenType::COMMA;
        token.text = ",";
        tokenizer.index++;
    } else if (tokenizer.text[tokenizer.index] == '"') {
        token.type = TokenType::STRING;
        tokenizer.index++;
        
        while (tokenizer.index < tokenizer.text.length() && tokenizer.text[tokenizer.index] != '"') {
            token.text += tokenizer.text[tokenizer.index];
            tokenizer.index++;
        }
        if (tokenizer.index >= tokenizer.text.length()) {
            token.type = TokenType::ERROR;
            
            char buffer[1024];
            snprintf(buffer, 1024,
                     "This was a problem because we reached the end of the message before finding a "
                     "matching '\"' for the string: \"%s",
                     token.text.c_str());
            
            plugin_error(tokenizer, buffer);
            return token;
        }
        tokenizer.index++;
    } else {
        token.type = TokenType::IDENTIFIER;
        while (tokenizer.index < tokenizer.text.length() && !isspace(tokenizer.text[tokenizer.index])) {
            if (tokenizer.text[tokenizer.index] == '=' || tokenizer.text[tokenizer.index] == ',' ||
                tokenizer.text[tokenizer.index] == '"') {
                break;
            }
            token.text += tokenizer.text[tokenizer.index];
            tokenizer.index++;
        }
    }
    
    return token;
}

void plugin_error(Tokenizer &tokenizer, const char *buffer) {
    tokenizer.error = true;
    fprintf(stderr, "Error parsing message from plugin '%s.plugin':\n\tMessage was: %s\n%s",
            tokenizer.plugin_name.c_str(),
            tokenizer.text.c_str(), buffer);
}

static void
key_event_combo_box(AppClient *client,
                    cairo_t *cr,
                    Container *container,
                    bool is_string, xkb_keysym_t keysym, char string[64],
                    uint16_t mods,
                    xkb_key_direction direction) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (direction == XKB_KEY_UP)
        return;
    
    auto *data = (ComboBoxPopup *) container->user_data;
    auto current_time = get_current_time_in_ms();
    auto elapsed_time = current_time - data->last_keypress_time;
    
    if (keysym == XKB_KEY_Escape && data->filter_text.empty()) {
        client_close_threaded(app, client);
        return;
    }
    
    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_Tab) {
        client_close_threaded(app, client);
        
        if (auto *scroll = (ScrollContainer *) container_by_name("scroll", container)) {
            if (!scroll->content->children.empty()) {
                auto *container = scroll->content->children[scroll->content->children.size() - 1];
                
                auto *data = (ComboBoxChildData *) container->user_data;
                if (auto plugin_menu = client_by_name(app, "plugin_menu")) {
                    if (auto c = container_by_container(data->combobox, plugin_menu->root)) {
                        auto *plugin_container_data = (PluginContainerData *) c->user_data;
                        
                        for (auto it = plugin_container_data->items.begin();
                             it != plugin_container_data->items.end(); it++) {
                            if (*it == data->text) {
                                plugin_container_data->items.erase(it);
                                plugin_container_data->items.insert(plugin_container_data->items.begin(), data->text);
                                
                                if (!c->name.empty()) {
                                    auto *plugin_data = (PluginData *) plugin_menu->root->user_data;
                                    std::string message("name=\"" + c->name + "\", made_selection=");
                                    plugin_data->cc->write(message);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        return;
    }
    
    if (elapsed_time > 1000 ||
        (!is_string && (keysym == XKB_KEY_BackSpace || keysym == XKB_KEY_Delete || keysym == XKB_KEY_Escape))) {
        data->filter_text = "";
        if (auto *scroll = (ScrollContainer *) container_by_name("scroll", container)) {
            for (auto *child: scroll->content->children) {
                child->exists = true;
            }
        }
    }
    data->last_keypress_time = get_current_time_in_ms();
    
    if (is_string) {
        data->filter_text += string;
    }
    
    // make exist
    if (auto *scroll = (ScrollContainer *) container_by_name("scroll", container)) {
        scroll->scroll_v_real = -1000000;
        scroll->scroll_v_visual = -1000000;
        
        for (auto *child: scroll->content->children) {
            auto *child_data = (ComboBoxChildData *) child->user_data;
            
            // to lowercase child_data->text into temp string
            std::string lowercase_text = child_data->text;
            std::transform(lowercase_text.begin(), lowercase_text.end(), lowercase_text.begin(), ::tolower);
            
            // instead of find use starts with
            child_data->i = lowercase_text.find(data->filter_text);
            if (child_data->i == std::string::npos) {
                child->exists = false;
            } else {
                child->exists = true;
            }
        }
    
        if (keysym != XKB_KEY_BackSpace && keysym != XKB_KEY_Delete && keysym != XKB_KEY_Escape) {
            // sort scroll->content->children by i
            std::sort(scroll->content->children.begin(), scroll->content->children.end(),
                      [](Container *a, Container *b) {
                          auto *a_data = (ComboBoxChildData *) a->user_data;
                          auto *b_data = (ComboBoxChildData *) b->user_data;
                          return a_data->i > b_data->i;
                      });
        
        }
    
        uint32_t target_height = 0;
        for (auto *child: scroll->content->children)
            if (child->exists)
                target_height += 35 * config->dpi;
    
        if (target_height == 0)
            target_height = 35 * config->dpi;
    
        if (target_height > data->start_bounds.h)
            target_height = data->start_bounds.h;
    
        uint32_t value_mask =
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    
        uint32_t value_list_resize[] = {
                (uint32_t) (data->start_bounds.x),
                (uint32_t) (data->start_bounds.y + (data->start_bounds.h - target_height)),
                (uint32_t) (data->start_bounds.w),
                (uint32_t) (target_height),
        };
        xcb_configure_window(app->connection, client->window, value_mask, value_list_resize);
    }
    client_layout(client->app, client);
    client_paint(client->app, client);
}

void clicked_combo_box(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (PluginContainerData *) container->user_data;
    
    double height = 300 * config->dpi;
    double option_height = 35 * config->dpi;
    
    for (auto *screen: screens) {
        if (screen->is_primary) {
            height = screen->height_in_pixels;
            height -= (screen->height_in_pixels - client->bounds->y);
            height -= option_height * 6;
        }
    }
    
    if (option_height * data->items.size() < height) {
        height = option_height * data->items.size();
    }
    
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.sticky = true;
    settings.force_position = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    settings.h = height;
    settings.w = container->real_bounds.w;
    settings.x = client->bounds->x + container->real_bounds.x;
    settings.y = client->bounds->y + container->real_bounds.y;
    settings.y -= settings.h;
    
    PopupSettings popup_settings;
    popup_settings.ignore_scroll = true;
    popup_settings.takes_input_focus = true;
    popup_settings.name = "combo_box_popup";
    auto popup = client->create_popup(popup_settings, settings);
    popup->root->when_paint = paint_combo_box_popup;
    popup->root->when_key_event = key_event_combo_box;
    popup->root->receive_events_even_if_obstructed = true;
    popup->root->user_data = new ComboBoxPopup;
    ((ComboBoxPopup *) popup->root->user_data)->start_bounds = *popup->bounds;
    
    ScrollPaneSettings scroll_settings(config->dpi);
    scroll_settings.right_inline_track = true;
    scroll_settings.start_at_end = true;
    auto scroll = make_newscrollpane_as_child(popup->root, scroll_settings);
    scroll->name = "scroll";
    for (const auto &item: data->items) {
        auto child = scroll->content->child(::vbox, FILL_SPACE, option_height);
        child->when_paint = paint_combobox_item;
        auto *child_data = new ComboBoxChildData;
        child_data->combobox = container;
        child_data->text = item;
        child->user_data = child_data;
        child->when_clicked = [](AppClient *client, cairo_t *cr, Container *container) {
            auto *data = (ComboBoxChildData *) container->user_data;
            client_close_threaded(app, client);
            if (auto plugin_menu = client_by_name(app, "plugin_menu")) {
                if (auto c = container_by_container(data->combobox, plugin_menu->root)) {
                    auto *plugin_container_data = (PluginContainerData *) c->user_data;
                    
                    for (auto it = plugin_container_data->items.begin();
                         it != plugin_container_data->items.end(); it++) {
                        if (*it == data->text) {
                            plugin_container_data->items.erase(it);
                            plugin_container_data->items.insert(plugin_container_data->items.begin(), data->text);
                            
                            if (!c->name.empty()) {
                                auto *plugin_data = (PluginData *) plugin_menu->root->user_data;
                                std::string message("name=\"" + c->name + "\", made_selection=");
                                plugin_data->cc->write(message);
                            }
                            break;
                        }
                    }
                }
            }
        };
    }
    
    client_show(client->app, popup);
}

static void clicked_button(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (PluginData *) client->root->user_data;
    
    if (!container->name.empty()) {
        std::string message("name=\"" + container->name + "\", when_clicked=");
        data->cc->write(message);
    }
}

void on_plugin_sent_text(Subprocess *cc) {
    auto *taskbar_plugin_button = (Container *) cc->user_data;
    auto *plugin_data = (PluginData *) taskbar_plugin_button->user_data;
    
    std::stringstream iss(cc->recent);
    std::string line;
    Container *container = nullptr;
    while (getline(iss, line, '\n')) {
        Tokenizer tokenizer(line);
        if (line.find("print") == 0) {
            printf("plugin sent:\n%s\n", line.c_str());
            continue;
        }
        tokenizer.plugin_name = taskbar_plugin_button->name;
        
        for (;;) {
            Token token = get_token(tokenizer);
            
            if (token.type == TokenType::END || token.type == TokenType::ERROR)
                break;
            
            if (token.type == TokenType::IDENTIFIER) {
                if (token.text == "set_icon") {
                    token = get_token(tokenizer);
                    if (token.type == TokenType::STRING) {
                        std::string icon_name = token.text;
                        taskbar_plugin_button->exists = true;
                        plugin_data->icon = icon_name;
                        plugin_data->tried_to_load = false;
                        if (plugin_data->surface__)
                            cairo_surface_destroy(plugin_data->surface__);
                        plugin_data->surface__ = nullptr;
                        plugin_data->gsurf->valid = false;
                        if (auto *client = client_by_name(app, "taskbar")) {
                            client_layout(app, client);
                            client_paint(app, client);
                        }
                    } else {
                        plugin_error(tokenizer,
                                     "This is because \"set_icon\" was not followed by a string as was expected.");
                    }
                } else if (token.text == "set_text_as_icon") {
                    token = get_token(tokenizer);
                    if (token.type == TokenType::STRING) {
                        std::string icon_name = token.text;
                        taskbar_plugin_button->exists = true;
                        plugin_data->icon_text = icon_name;
                        if (auto *client = client_by_name(app, "taskbar")) {
                            client_layout(app, client);
                            client_paint(app, client);
                        }
                    } else {
                        plugin_error(tokenizer,
                                     "This is because \"set_icon_text\" was not followed by a single character string as was expected.");
                    }
                } else if (token.text == "set_text_icon_color") {
                    token = get_token(tokenizer);
                    if (token.type == TokenType::STRING) {
                        std::string icon_text_color = token.text;
                        taskbar_plugin_button->exists = true;
                        plugin_data->icon_text_color = ArgbColor(icon_text_color);
                        if (auto *client = client_by_name(app, "taskbar")) {
                            client_layout(app, client);
                            client_paint(app, client);
                        }
                    } else {
                        plugin_error(tokenizer,
                                     "This is because \"set_text_icon_color\" was not followed by a single character string as was expected.");
                    }
                } else if (token.text == "ui_start") {
                    container = new Container(::vbox, FILL_SPACE, FILL_SPACE);
                    container->parent = plugin_data->root;
                    container->wanted_pad = Bounds(8 * config->dpi, 8 * config->dpi, 8 * config->dpi,
                                                   8 * config->dpi);
                    
                    while (getline(iss, line, '\n')) {
                        if (line == "ui_end") {
                            delete plugin_data->root;
                            plugin_data->root = container;
                            break;
                        }
                        Tokenizer ui_tokenizer(line);
                        ui_tokenizer.plugin_name = taskbar_plugin_button->name;
                        
                        for (;;) {
                            token = get_token(ui_tokenizer);
                            
                            if (token.type == TokenType::END || token.type == TokenType::ERROR)
                                break;
                            
                            if (token.type == TokenType::IDENTIFIER) {
                                if (token.text == "type") {
                                    if ((token = get_token(ui_tokenizer)).type == TokenType::EQUALS) {
                                        if ((token = get_token(ui_tokenizer)).type == TokenType::STRING) {
                                            Container *child;
                                            auto container_data = new PluginContainerData;
                                            if (token.text == "button") {
                                                child = container->child(::vbox, FILL_SPACE, 35 * config->dpi);
                                                child->when_paint = paint_centered_label;
                                                child->when_clicked = clicked_button;
                                                container_data->type = PluginContainerType::BUTTON;
                                            } else if (token.text == "label") {
                                                child = container->child(::vbox, FILL_SPACE, (10 + 18) * config->dpi);
                                                child->when_paint = paint_message;
                                                container_data->type = PluginContainerType::LABEL;
                                            } else if (token.text == "combobox") {
                                                child = container->child(::vbox, FILL_SPACE, 35 * config->dpi);
                                                child->when_paint = paint_combobox;
                                                child->when_clicked = clicked_combo_box;
                                                container_data->type = PluginContainerType::COMBOBOX;
                                            } else if (token.text == "field") {
                                                child = container->child(::vbox, FILL_SPACE, 35 * config->dpi);
                                                container_data->type = PluginContainerType::FIELD;
                                            } else if (token.text == "filler") {
                                                child = container->child(::vbox, FILL_SPACE, 35 * config->dpi);
                                                container_data->type = PluginContainerType::FILLER;
                                            } else {
                                                // unknown type
                                                child = container->child(::vbox, FILL_SPACE, 35 * config->dpi);
                                            }
                                            child->user_data = container_data;
                                            
                                            // Parse name or property assignment in any order
                                            for (;;) {
                                                token = get_token(ui_tokenizer);
                                                
                                                if (token.type == TokenType::END || token.type == TokenType::ERROR)
                                                    break;
                                                
                                                if (token.type == TokenType::IDENTIFIER) {
                                                    std::string ident = token.text;
                                                    if ((token = get_token(ui_tokenizer)).type == TokenType::EQUALS) {
                                                        if ((token = get_token(ui_tokenizer)).type ==
                                                            TokenType::STRING) {
                                                            std::string value = token.text;
                                                            if (ident == "name") {
                                                                child->name = value;
                                                                // TODO: set child on_hovered on_clicked
                                                            } else if (ident == "text") {
                                                                container_data->text = value;
                                                            } else if (ident == "prompt") {
                                                                container_data->prompt = value;
                                                            } else if (ident == "font_size") {
                                                                assert(false);
                                                            } else if (ident == "align") {
                                                                assert(false);
                                                            } else if (ident == "height") {
                                                                try {
                                                                    child->wanted_bounds.h =
                                                                            std::stod(value) * config->dpi;
                                                                } catch (std::exception &e) {
                                                                
                                                                }
                                                            } else if (ident == "width") {
                                                                try {
                                                                    child->wanted_bounds.w =
                                                                            std::stod(value) * config->dpi;
                                                                } catch (std::exception &e) {
                                                                
                                                                }
                                                            } else if (ident == "items") {
                                                                container_data->items.clear();
                                                                std::stringstream isss(value);
                                                                std::string item;
                                                                while (getline(isss, item, ':')) {
                                                                    // trim all start whitespace
                                                                    while (item.length() > 0 && isspace(item[0]))
                                                                        item = item.substr(1);
                                                                    container_data->items.push_back(item);
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else if (token.text == "layout_horizontal") {
                                    container = container->child(::hbox, FILL_SPACE, USE_CHILD_SIZE);
                                } else if (token.text == "layout_pop") {
                                    if (container->parent)
                                        container = container->parent;
                                }
                            }
                        }
                    }
                } else if (token.text == "name") {
                    // Either assigns to a property or requests it, or some function
                    if ((token = get_token(tokenizer)).type == TokenType::EQUALS) {
                        if ((token = get_token(tokenizer)).type == TokenType::STRING) {
                            std::string name_of_container = token.text;
                            if ((token = get_token(tokenizer)).type == TokenType::COMMA) {
                                if ((token = get_token(tokenizer)).type == TokenType::IDENTIFIER) {
                                    std::string property_name = token.text;
                                    
                                    if ((token = get_token(tokenizer)).type == TokenType::EQUALS) {
                                        token = get_token(tokenizer);
                                        if (token.type == TokenType::STRING || token.type == TokenType::END) {
                                            if (token.type == TokenType::STRING) {
                                                // makes a child
                                                std::string property_value = token.text;
                                                
                                                if (auto *c = container_by_name(name_of_container, plugin_data->root)) {
                                                    auto *data = (PluginContainerData *) c->user_data;
                                                    
                                                    if (property_name == "text") {
                                                        data->text = property_value;
                                                    } else if (property_name == "prompt") {
                                                        data->prompt = property_value;
                                                    } else if (property_name == "items") {
                                                        data->items.clear();
                                                        std::stringstream isss(property_value);
                                                        std::string item;
                                                        while (getline(isss, item, ':')) {
                                                            // trim all start whitespace
                                                            while (item.length() > 0 && isspace(item[0]))
                                                                item = item.substr(1);
                                                            data->items.push_back(item);
                                                        }
                                                    }
                                                    
                                                    if (auto *client = client_by_name(app, "plugin_menu")) {
                                                        client_layout(app, client);
                                                        client_paint(app, client);
                                                    }
                                                }
                                            } else {
                                                if (auto *c = container_by_name(name_of_container, plugin_data->root)) {
                                                    auto *data = (PluginContainerData *) c->user_data;
                                                    
                                                    if (property_name == "text") {
                                                        cc->write(data->text);
                                                    } else if (property_name == "prompt") {
                                                        cc->write(data->prompt);
                                                    } else if (property_name == "when_clicked") {
                                                        if (c->when_clicked) {
                                                            if (auto *client = client_by_name(app, "plugin_menu")) {
                                                                c->when_clicked(client, client->cr, c);
                                                            }
                                                        }
                                                    } else if (property_name == "items") {
                                                        // make a comma-separated list of items, remove last comma
                                                        std::string items;
                                                        for (auto &item: data->items) {
                                                            items += item + ":";
                                                        }
                                                        if (items.length() > 0)
                                                            items = items.substr(0, items.length() - 1);
                                                        cc->write(items);
                                                    }
                                                }
                                            }
                                        } else {
                                            plugin_error(tokenizer,
                                                         "This is because we expected a string after the '=' after the property name (if you wanted to assign the property), "
                                                         "or nothing at all (if you wanted to request the property), "
                                                         "but instead found something else.");
                                        }
                                    } else {
                                        plugin_error(tokenizer,
                                                     "This is because we expected an '=' after the property name, but instead found something else.");
                                    }
                                } else {
                                    plugin_error(tokenizer,
                                                 "This is because we expected an identifier naming a property after the ',' after the name of the container, but instead found something else.");
                                }
                            } else {
                                plugin_error(tokenizer,
                                             "This is because we expected a ',' after the string, but instead found something else.");
                            }
                        } else {
                            plugin_error(tokenizer,
                                         "This is because we expected a string after 'name=', but instead found something else.");
                        }
                    } else {
                        plugin_error(tokenizer,
                                     "This is because we expected an '=' after 'name', but instead found something else.");
                    }
                } else {
                
                }
            }
        }
    }
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
    
    auto e = getEasingFunction(easing_functions::EaseOutQuad);
    double time = 100;
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            if (data->previous_state != 2) {
                data->previous_state = 2;
                client_create_animation(client->app, client, &data->color.r, data->color.lifetime, 0, time, e,
                                        pressed_color.r);
                client_create_animation(client->app, client, &data->color.g, data->color.lifetime, 0, time, e,
                                        pressed_color.g);
                client_create_animation(client->app, client, &data->color.b, data->color.lifetime, 0, time, e,
                                        pressed_color.b);
                client_create_animation(client->app, client, &data->color.a, data->color.lifetime, 0, time, e,
                                        pressed_color.a);
            }
        } else if (data->previous_state != 1) {
            data->previous_state = 1;
            client_create_animation(client->app, client, &data->color.r, data->color.lifetime, 0, time, e,
                                    hovered_color.r);
            client_create_animation(client->app, client, &data->color.g, data->color.lifetime, 0, time, e,
                                    hovered_color.g);
            client_create_animation(client->app, client, &data->color.b, data->color.lifetime, 0, time, e,
                                    hovered_color.b);
            client_create_animation(client->app, client, &data->color.a, data->color.lifetime, 0, time, e,
                                    hovered_color.a);
        }
    } else if (data->previous_state != 0) {
        data->previous_state = 0;
        e = getEasingFunction(easing_functions::EaseInQuad);
        client_create_animation(client->app, client, &data->color.r, data->color.lifetime, 0, time, e, default_color.r);
        client_create_animation(client->app, client, &data->color.g, data->color.lifetime, 0, time, e, default_color.g);
        client_create_animation(client->app, client, &data->color.b, data->color.lifetime, 0, time, e, default_color.b);
        client_create_animation(client->app, client, &data->color.a, data->color.lifetime, 0, time, e, default_color.a);
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
paint_plugin(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_hoverable_button_background(client, cr, container);
    
    double icon_size = 15 * config->dpi;
    
    auto *data = (PluginData *) container->user_data;
    
    if (!data->icon_text.empty()) {
        PangoLayout *layout =
                get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
        
        // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
        pango_layout_set_text(layout, data->icon_text.c_str(), data->icon_text.size());
        
        set_argb(cr, data->icon_text_color);
        
        int width;
        int height;
        pango_layout_get_pixel_size_safe(layout, &width, &height);
        
        cairo_move_to(cr,
                      (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                      (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        pango_cairo_show_layout(cr, layout);
    } else if (!data->icon.empty()) {
        if (data->surface__ == nullptr && !data->tried_to_load) {
            data->tried_to_load = true;
            
            std::vector<IconTarget> targets;
            targets.emplace_back(data->icon);
            search_icons(targets);
            pick_best(targets, icon_size);
            std::string icon_path = targets[0].best_full_path;
            if (data->icon[0] == '/')
                icon_path = data->icon;
            if (!icon_path.empty()) {
                load_icon_full_path(app, client, &data->surface__, icon_path, icon_size);
            }
        }
        
        if (data->surface__ != nullptr) {
            draw_gl_texture(client, data->gsurf, data->surface__,
                                     (int) (container->real_bounds.x + container->real_bounds.w / 2 - icon_size / 2),
                                     (int) (container->real_bounds.y + container->real_bounds.h / 2 - icon_size / 2));
        }
    }
}

static void invalidate_icon_button_press_if_window_open(AppClient *client, cairo_t *cr, Container *container) {
    auto data = (IconButton *) container->user_data;
    
    if (get_current_time_in_ms() - data->timestamp > 100) {
        if (auto c = client_by_name(client->app, data->invalidate_button_press_if_client_with_this_name_is_open)) {
            data->invalid_button_down = true;
        } else {
            data->invalid_button_down = false;
        }
        data->timestamp = get_current_time_in_ms();
    }
}

void add_plugin(const std::string &path) {
    if (auto *client = client_by_name(app, "taskbar")) {
        Container *button_plugin = nullptr;
        // remove .plugin from path
//        std::string name_without_extension = path.substr(0, path.length() - 7);
        std::string name_without_extension = path;
        if (auto *button = container_by_name(name_without_extension, client->root)) {
            auto *plugin_data = (PluginData *) button->user_data;
            plugin_data->cc->kill(false);
            
            for (int i = 0; i < button->parent->children.size(); i++) {
                if (button->parent->children[i] == button) {
                    button_plugin = new Container(::vbox, 24 * config->dpi, FILL_SPACE);
                    button_plugin->parent = button->parent;
                    button->parent->children.erase(button->parent->children.begin() + i);
                    button->parent->children.insert(button->parent->children.begin() + i, button_plugin);
                    
                    delete button;
                    
                    break;
                }
            }
            
            client_layout(app, client);
            client_paint(app, client);
        } else {
            button_plugin = client->root->child(24 * config->dpi, FILL_SPACE);
        }
        if (!button_plugin)
            return;
        
        auto cc = client->command(path, 0, on_plugin_sent_text, button_plugin);
        
        button_plugin->exists = false; // only show when plugin does set_icon
        button_plugin->when_paint = paint_plugin;
        auto button_plugin_data = new PluginData;
        button_plugin_data->icon_text_color = config->color_taskbar_button_icons;
        button_plugin_data->cc = cc;
        button_plugin_data->path = path;
        button_plugin_data->invalidate_button_press_if_client_with_this_name_is_open = "plugin_menu";
        button_plugin->user_data = button_plugin_data;
        button_plugin->when_mouse_down = invalidate_icon_button_press_if_window_open;
        button_plugin->name = name_without_extension;
        button_plugin->when_clicked = clicked_plugin;
        
        cc->write("program_start");
    }
}

void make_plugins(App *app, AppClient *client, Container *root) {
    char *string = getenv("HOME");
    std::string plugins_path(string);
    plugins_path += "/.config/winbar/plugins/";
    
    // use c++ filesystem to recurse through the plugins directory and find executables with extension .plugin
    try {
        for (const auto &entry: std::filesystem::recursive_directory_iterator(plugins_path)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path();
                if (path.size() > 7 && path.substr(path.size() - 7) == ".plugin") {
                    // check if file is executable
                    if (access(path.c_str(), X_OK) != -1) {
                        add_plugin(path);
                    }
                }
            }
        }
    } catch (...) {
    
    }
}
