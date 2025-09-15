
#include "pinned_icon_right_click.h"
#include "INIReader.h"
#include "config.h"
#include "icons.h"
#include "main.h"
#include "taskbar.h"
#include "pinned_icon_editor.h"
#include "settings_menu.h"

#include <xcb/xcb.h>
#include <sys/stat.h>

class CustomItem {
public:
    std::string class_name;
    std::string name;
    std::string icon;
    std::string command;
    std::string category;
};

enum option_data_type {
    NONE = 0,
    CLOSE = 1,
    PIN = 2,
    UNPIN = 3,
    OPEN = 4,
    CUSTOM = 5,
    EDIT = 6,
    ENDTASK = 7,
};

class OptionData : public UserData {
public:
    std::string text;
    ArgbColor text_color;
    gl_surface *gsurf;
    cairo_surface_t *surface = nullptr;
    int text_offset = 0;
    
    option_data_type option_type = NONE;
    void *user_data;
    
    ~OptionData() {
        if (!surface) {
            cairo_surface_destroy(surface);
        }
    }
};

static std::vector<CustomItem *> custom_items;
static Container *pinned_icon_container = nullptr;
static LaunchableButton *pinned_icon_data = nullptr;
static std::string pinned_icon_class;
static AppClient *client_entity = nullptr;
static int pad = 8;

static void
load_custom_items() {
    std::string items_path(getenv("HOME"));
    items_path += "/.config/winbar/items_custom.ini";
    
    INIReader items_parsed(items_path);
    if (items_parsed.ParseError() != 0) {
        return;
    }
    for (auto c: custom_items) {
        delete c;
    }
    custom_items.clear();
    custom_items.shrink_to_fit();
    
    for (auto section: items_parsed.Sections()) {
        auto class_name = items_parsed.Get(section, "class_name", "");
        if (class_name.empty())
            continue;
        auto name = items_parsed.Get(section, "name", "");
        if (name.empty())
            continue;
        auto icon = items_parsed.Get(section, "icon", "");
        if (icon.empty())
            continue;
        auto command = items_parsed.Get(section, "command", "");
        if (command.empty())
            continue;
        auto category = items_parsed.Get(section, "category", "");
        if (category.empty())
            continue;
        CustomItem *custom_item = new CustomItem();
        custom_item->class_name = class_name;
        custom_item->name = name;
        custom_item->icon = icon;
        custom_item->command = command;
        custom_item->category = category;
        custom_items.push_back(custom_item);
    }
}

static void
paint_icon(AppClient *client, cairo_t *cr, Container *container, bool dye) {
    auto *data = (OptionData *) container->user_data;
    
    std::string text;
    std::string path;
    if (data->text == "Edit") {
        text = "\uE713";
    } else if (data->text == "Pin to taskbar") {
        text = "\uE718";
        path = "open.png";
    } else if (data->text == "Unpin from taskbar") {
        text = "\uE77A";
    }else if (data->text == "End task" || data->text == "End tasks") {
      text = "\uF140";
    } else if (data->text == "Close window" || data->text == "Close all windows") {
        text = "\uE10A";
    } else if (data->surface != nullptr) {
        // TODO: gsurf here maybe (likely)?
        int height = cairo_image_surface_get_height(data->surface);
        
        if (dye)
            dye_surface(data->surface, config->color_pin_menu_icons);
        
        draw_gl_texture(client, data->gsurf, data->surface, (int) (container->real_bounds.x + 10 * config->dpi),
                        (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        return;
    }
    if (!winbar_settings->icons_from_font) {
        // Function needs to be revised to support this kind of positioning
        // load_and_paint(app, client, path, 16 * config->dpi, container->real_bounds, 5, 12 * config->dpi);
        return;
    }
    draw_text(client, 10 * config->dpi, config->icons, EXPAND(config->color_pin_menu_icons), text, container->real_bounds, 5, 12 * config->dpi);
}

static void
paint_text(AppClient *client, cairo_t *cr, Container *container, ArgbColor color) {
    auto *data = (OptionData *) container->user_data;
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(color), data->text, container->real_bounds, 5, data->text_offset);
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_pin_menu_background), container->real_bounds);
}

static void
paint_custom_option(AppClient *client, cairo_t *cr, Container *container) {
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        auto color = config->color_pin_menu_hovered_item;
        if (container->state.mouse_pressing) {
            color =  config->color_pin_menu_pressed_item;
        }
        draw_colored_rect(client, color, container->real_bounds);
    }
    
    paint_text(client, cr, container, config->color_pin_menu_text);
}

static void
paint_option(AppClient *client, cairo_t *cr, Container *container) {
    paint_custom_option(client, cr, container);
    paint_icon(client, cr, container, true);
}

static void
paint_open(AppClient *client, cairo_t *cr, Container *container) {
    paint_custom_option(client, cr, container);
    paint_text(client, cr, container, config->color_pin_menu_text);
    paint_icon(client, cr, container, false);
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionData *) container->user_data;
    
    paint_text(client, cr, container, config->color_pin_menu_text);
}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container) {
    // If the pinned icon with the initial starting class return
    bool found = false;
    AppClient *taskbar = nullptr;
    Container *icons = nullptr;
    if (auto c = client_by_name(app, "taskbar")) {
        if (auto ico = container_by_name("icons", c->root)) {
            taskbar = c;
            icons = ico;
            for (auto icon: icons->children) {
                auto data = static_cast<LaunchableButton *>(icon->user_data);
                if (data->class_name == pinned_icon_class) {
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        client_close_threaded(app, client);
        return;
    }
    auto *data = (OptionData *) container->user_data;
    
    switch (data->option_type) {
        case CLOSE: {
            for (auto window_list: pinned_icon_data->windows_data_list) {
                auto window = window_list->id;
                xcb_ewmh_client_source_type_t source;
                xcb_ewmh_request_close_window(
                        &app->ewmh, app->screen_number, window, XCB_TIME_CURRENT_TIME, source);
            }
            break;
        }
        case PIN: {
            for (auto icon : icons->children) {
                auto data = (LaunchableButton *) icon->user_data;
                if (data->class_name == pinned_icon_data->class_name) {
                    data->pinned = true;
                }
            }
            // Remove icon cache if exists
            const char *home = getenv("HOME");
            std::string itemPath(home);
            itemPath += "/.config/";
            
            if (mkdir(itemPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
                if (errno != EEXIST) {
                    printf("Couldn't mkdir %s\n", itemPath.c_str());
                    return;
                }
            }
            
            itemPath += "/winbar/";
            
            if (mkdir(itemPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
                if (errno != EEXIST) {
                    printf("Couldn't mkdir %s\n", itemPath.c_str());
                    return;
                }
            }
            
            itemPath += "/cached_icons/";
            
            if (mkdir(itemPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
                if (errno != EEXIST) {
                    printf("Couldn't mkdir %s\n", itemPath.c_str());
                    return;
                }
            }
            
            itemPath += pinned_icon_data->class_name + ".png";
            if (pinned_icon_data->surface__) {
                cairo_surface_write_to_png(pinned_icon_data->surface__, itemPath.c_str());
            }
            
            update_pinned_items_file(false);
            
            if (config->open_pinned_icon_editor == "ALWAYS") {
                start_pinned_icon_editor(pinned_icon_container, false);
            } else if (config->open_pinned_icon_editor != "NEVER") { // WHEN ANY FIELD EMPTY
                if (pinned_icon_data->class_name.empty() || pinned_icon_data->icon_name.empty() ||
                    pinned_icon_data->command_launched_by.empty()) {
                    start_pinned_icon_editor(pinned_icon_container, false);
                }
            }
            break;
        }
        case UNPIN: {
            for (auto icon: icons->children) {
                auto icon_data = (LaunchableButton *) icon->user_data;
                if (icon_data->class_name == pinned_icon_data->class_name)
                    icon_data->pinned = false;
            }
            const char *home = getenv("HOME");
            std::string itemPath(home);
            itemPath += "/.config/winbar/cached_icon/" + pinned_icon_data->class_name + ".png";
            rmdir(itemPath.c_str());
            remove_non_pinned_icons();
            break;
        }
        case OPEN: {
            launch_command(pinned_icon_data->command_launched_by);
            break;
        }
        case CUSTOM: {
            auto *custom_data = (CustomItem *) data->user_data;
            launch_command(custom_data->command);
            break;
        }
        case EDIT: {
            start_pinned_icon_editor(pinned_icon_container, false);
            break;
        }
        case ENDTASK: {
            for (auto window_list : pinned_icon_data->windows_data_list) {
                auto window = window_list->id;

                /* xcb_ewmh_connection_t has a public field `connection`
                   that is exactly the xcb_connection_t* you need.   */
                xcb_kill_client(app->ewmh.connection, window);
            }
            xcb_flush(app->connection);   // make sure the requests are sent
            break;
        }
    }
    
    client_close_threaded(app, client);
}

class DelayedSurfacePainting {
public:
    cairo_t *cr = nullptr;
    cairo_surface_t **surface = nullptr;
    
    cairo_surface_t *original_surface = nullptr;
    std::string path;
    int size = 0;
};

static Container *
make_root(std::vector<DelayedSurfacePainting *> *delayed) {
    Container *root = new Container();
    root->type = vbox;
    root->when_paint = paint_root;
    
    std::vector<CustomItem *> custom_items_for_us;
    for (auto *item: custom_items) {
        if (item->class_name == pinned_icon_data->class_name) {
            custom_items_for_us.push_back(item);
        }
    }
    
    std::string active_category;
    for (int i = 0; i < custom_items_for_us.size(); i++) {
        if (i == 0) {
            // Start Pad
            auto *start_pad = root->child(FILL_SPACE, pad);
        }
        
        // Item
        CustomItem *item = custom_items_for_us[i];
        if (item->category != active_category) {
            Container *title = nullptr;
            if (active_category.empty()) {
                title = root->child(FILL_SPACE, 30 * config->dpi);
            } else {
                title = root->child(FILL_SPACE, 30 * config->dpi);
            }
            title->when_paint = paint_title;
            auto *data = new OptionData();
            data->text_offset = 12 * config->dpi;
            data->text = item->category;
            title->user_data = data;
            active_category = item->category;
        }
        
        auto *option = root->child(FILL_SPACE, 30 * config->dpi);
        option->when_paint = paint_custom_option;
        auto *data = new OptionData();
        option->when_clicked = option_clicked;
        data->option_type = option_data_type::CUSTOM;
        data->user_data = item;
        data->text_offset = 40 * config->dpi;
        data->text = item->name;
        option->user_data = data;
        
        if (i == custom_items_for_us.size() - 1) {
            // End pad
            auto *end_pad = root->child(FILL_SPACE, pad);
        }
    }
    
    auto *start_pad = root->child(FILL_SPACE, pad);
    
    bool not_ours = true;
    for (auto w: pinned_icon_data->windows_data_list) {
        for (const auto &item: app->clients) {
            if (w->id == item->window) {
                not_ours = false;
            }
        }
    }
    if (not_ours) {
        auto *data = new OptionData();
        data->option_type = option_data_type::OPEN;
        auto *open = root->child(FILL_SPACE, 30 * config->dpi);
        pinned_icon_data->icon_name = c3ic_fix_wm_class(pinned_icon_data->icon_name);
        
        auto *d = new DelayedSurfacePainting();
        d->surface = &data->surface;
        data->gsurf = new gl_surface;
        
        d->size = 16 * config->dpi;
        std::string icon_path;
        
        std::vector<IconTarget> targets;
        targets.emplace_back(IconTarget(pinned_icon_data->icon_name));
        targets.emplace_back(IconTarget(c3ic_fix_wm_class(pinned_icon_data->class_name)));
        search_icons(targets);
        pick_best(targets, 16 * config->dpi);
        icon_path = targets[0].best_full_path;
        
        if (icon_path.empty()) {
            icon_path = targets[1].best_full_path;
        }
        if (!icon_path.empty()) {
            d->path = icon_path;
        }
        d->original_surface = pinned_icon_data->surface__;
        delayed->push_back(d);
        
        open->when_paint = paint_open;
        open->when_clicked = option_clicked;
        
        data->text = pinned_icon_data->class_name;
        data->text_offset = 40 * config->dpi;
        open->user_data = data;
    }
    
    if (pinned_icon_data->pinned) {
        auto *data = new OptionData();
        auto *edit = root->child(FILL_SPACE, 30 * config->dpi);
        edit->when_paint = paint_option;
        edit->when_clicked = option_clicked;
        
        data->text = "Edit";
        data->option_type = option_data_type::EDIT;
        
        data->text_offset = 40 * config->dpi;
        edit->user_data = data;
    }
    
    
    if (not_ours) {
        auto *data = new OptionData();
        auto *pinned = root->child(FILL_SPACE, 30 * config->dpi);
        pinned->when_paint = paint_option;
        pinned->when_clicked = option_clicked;
        
        if (pinned_icon_data->pinned) {
            data->text = "Unpin from taskbar";
            data->option_type = option_data_type::UNPIN;
        } else {
            data->text = "Pin to taskbar";
            data->option_type = option_data_type::PIN;
        }
        
        data->text_offset = 40 * config->dpi;
        pinned->user_data = data;
    }
    
    if (!pinned_icon_data->windows_data_list.empty()) {
        // ENDTASK 
        auto *endTaskData = new OptionData();
        auto *endTask = root->child(FILL_SPACE, 30 * config->dpi);
        endTask->when_paint = paint_option;
        endTask->when_clicked = option_clicked;
        endTaskData->option_type = option_data_type::ENDTASK;
        endTaskData->text_offset = 40 * config->dpi;
        endTask->user_data = endTaskData;
        
        // CLOSE 
        auto *closeData = new OptionData();
        auto *close = root->child(FILL_SPACE, 30 * config->dpi);
        close->when_paint = paint_option;
        close->when_clicked = option_clicked;
        closeData->option_type = option_data_type::CLOSE;
        closeData->text_offset = 40 * config->dpi;
        close->user_data = closeData;

        if (pinned_icon_data->windows_data_list.size() == 1) {
            endTaskData->text = "End task";
            closeData->text = "Close window";
        } else {
            endTaskData->text = "End tasks";
            closeData->text = "Close all windows";
        }
    }
    
    auto *end_pad = root->child(FILL_SPACE, pad);
    
    double height = true_height(root);
    
    root->wanted_bounds.w = 256 * config->dpi;
    root->wanted_bounds.h = height;
    return root;
}

static void when_pinned_icon_right_click_menu_closed(AppClient *client) {
    pinned_icon_data->type = selector_type::CLOSED;
}

void start_pinned_icon_right_click(Container *container) {
    pinned_icon_container = container;
    pinned_icon_data = (LaunchableButton *) container->user_data;
    pinned_icon_class = pinned_icon_data->class_name;
    pinned_icon_data->type = selector_type::OPEN_CLICKED;
    
    load_custom_items();
    Settings settings;
    
    std::vector<DelayedSurfacePainting *> delayed;
    
    Container *root = make_root(&delayed);
    settings.w = root->wanted_bounds.w;
    settings.h = root->wanted_bounds.h;
    settings.x = container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2;
    settings.y = app->bounds.h - config->taskbar_height - settings.h;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        settings.x = taskbar->bounds->x + (container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2);
        if (settings.x > taskbar->bounds->x + taskbar->bounds->w - settings.w)
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        if (settings.x < taskbar->bounds->x)
            settings.x = taskbar->bounds->x;
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.decorations = false;
    settings.force_position = true;
    settings.skip_taskbar = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[1] = 3;
    settings.slide_data[2] = 100;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 50;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "right_click_menu";
        auto client = taskbar->create_popup(popup_settings, settings);
        
        client->when_closed = when_pinned_icon_right_click_menu_closed;
        delete client->root;
        client->root = root;
        client_entity = client;
        
        // TODO: why do we delay this again?
        for (auto *d: delayed) {
            if (d->path.empty()) {
                if (pinned_icon_data->surface__) {
                    *d->surface = accelerated_surface(app, client, d->size, d->size);
                    cairo_t *cr = cairo_create(*d->surface);
                    
                    double starting_w = cairo_image_surface_get_width(pinned_icon_data->surface__);
                    double target_w = 16 * config->dpi;
                    double sx = target_w / starting_w;
                    
                    cairo_scale(cr, sx, sx);
                    draw_gl_texture(client, pinned_icon_data->gsurf, pinned_icon_data->surface__, 0, 0, target_w, target_w);
                    cairo_destroy(cr);
                }
            } else {
                *d->surface = accelerated_surface(app, client, d->size, d->size);
                paint_surface_with_image(*d->surface, d->path, d->size, nullptr);
            }
            delete d;
        }
        delayed.clear();
        delayed.shrink_to_fit();
        client_show(app, client);
    }
}
