
#include "pinned_icon_right_click.h"
#include "INIReader.h"
#include "config.h"
#include "icons.h"
#include "main.h"
#include "taskbar.h"
#include "pinned_icon_editor.h"

#include <pango/pangocairo.h>
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
};

class OptionData : public UserData {
public:
    std::string text;
    ArgbColor text_color;
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
    
    PangoLayout *layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    set_argb(cr, config->color_pin_menu_icons);
    
    int width;
    int height;
    if (data->text == "Edit") {
        pango_layout_set_text(layout, "\uE713", strlen("\uE83F"));
    } else if (data->text == "Pin to taskbar") {
        pango_layout_set_text(layout, "\uE718", strlen("\uE83F"));
    } else if (data->text == "Unpin from taskbar") {
        pango_layout_set_text(layout, "\uE77A", strlen("\uE83F"));
    } else if (data->text == "Close window" || data->text == "Close all windows") {
        pango_layout_set_text(layout, "\uE10A", strlen("\uE83F"));
    } else if (data->surface != nullptr) {
        height = cairo_image_surface_get_height(data->surface);
        
        if (dye)
            dye_surface(data->surface, config->color_pin_menu_icons);
        
        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + 10 * config->dpi),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        cairo_paint(cr);
        return;
    }
    
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_save(cr);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + (12 * config->dpi)),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
paint_text(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionData *) container->user_data;
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    
    cairo_move_to(cr,
                  container->real_bounds.x + data->text_offset,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_pin_menu_background));
    cairo_fill(cr);
}

static void
paint_custom_option(AppClient *client, cairo_t *cr, Container *container) {
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_pin_menu_pressed_item);
        } else {
            set_argb(cr, config->color_pin_menu_hovered_item);
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    set_argb(cr, config->color_pin_menu_text);
    paint_text(client, cr, container);
}

static void
paint_option(AppClient *client, cairo_t *cr, Container *container) {
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_pin_menu_pressed_item);
        } else {
            set_argb(cr, config->color_pin_menu_hovered_item);
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    set_argb(cr, config->color_pin_menu_text);
    paint_text(client, cr, container);
    paint_icon(client, cr, container, true);
}

static void
paint_open(AppClient *client, cairo_t *cr, Container *container) {
    if ((container->state.mouse_pressing || container->state.mouse_hovering)) {
        if (container->state.mouse_pressing) {
            set_argb(cr, config->color_pin_menu_pressed_item);
        } else {
            set_argb(cr, config->color_pin_menu_hovered_item);
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }
    
    set_argb(cr, config->color_pin_menu_text);
    paint_text(client, cr, container);
    paint_icon(client, cr, container, false);
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionData *) container->user_data;
    
    set_argb(cr, config->color_pin_menu_text);
    paint_text(client, cr, container);
}

static void
option_clicked(AppClient *client, cairo_t *cr, Container *container) {
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
            pinned_icon_data->pinned = true;
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
            if (pinned_icon_data->surface) {
                cairo_surface_write_to_png(pinned_icon_data->surface, itemPath.c_str());
            }
            
            update_pinned_items_file(false);
            
            if (config->open_pinned_icon_editor == "ALWAYS") {
                start_pinned_icon_editor(pinned_icon_container);
            } else if (config->open_pinned_icon_editor != "NEVER") { // WHEN ANY FIELD EMPTY
                if (pinned_icon_data->class_name.empty() || pinned_icon_data->icon_name.empty() ||
                    pinned_icon_data->command_launched_by.empty()) {
                    start_pinned_icon_editor(pinned_icon_container);
                }
            }
            break;
        }
        case UNPIN: {
            pinned_icon_data->pinned = false;
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
            start_pinned_icon_editor(pinned_icon_container);
            break;
        }
    }
    
    client_close(app, client);
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
    
    auto *data = new OptionData();
    data->option_type = option_data_type::OPEN;
    auto *open = root->child(FILL_SPACE, 30 * config->dpi);
    pinned_icon_data->icon_name = c3ic_fix_wm_class(pinned_icon_data->icon_name);
    
    auto *d = new DelayedSurfacePainting();
    d->surface = &data->surface;
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
    d->original_surface = pinned_icon_data->surface;
    delayed->push_back(d);
    
    open->when_paint = paint_open;
    open->when_clicked = option_clicked;
    
    data->text = pinned_icon_data->class_name;
    data->text_offset = 40 * config->dpi;
    open->user_data = data;
    
    if (pinned_icon_data->pinned) {
        auto *edit = root->child(FILL_SPACE, 30 * config->dpi);
        edit->when_paint = paint_option;
        data = new OptionData();
        edit->when_clicked = option_clicked;
        
        data->text = "Edit";
        data->option_type = option_data_type::EDIT;
        
        data->text_offset = 40 * config->dpi;
        edit->user_data = data;
    }
    
    auto *pinned = root->child(FILL_SPACE, 30 * config->dpi);
    pinned->when_paint = paint_option;
    data = new OptionData();
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
    
    if (!pinned_icon_data->windows_data_list.empty()) {
        auto *close = root->child(FILL_SPACE, 30 * config->dpi);
        close->when_paint = paint_option;
        close->when_clicked = option_clicked;
        data = new OptionData();
        data->option_type = option_data_type::CLOSE;
        if (pinned_icon_data->windows_data_list.size() == 1) {
            data->text = "Close window";
        } else {
            data->text = "Close all windows";
        }
        data->text_offset = 40 * config->dpi;
        close->user_data = data;
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
    if (auto c = client_by_name(app, "pinned_icon_editor")) {
        client_close_threaded(app, c);
    }
    pinned_icon_container = container;
    pinned_icon_data = (LaunchableButton *) container->user_data;
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
        
        for (auto *d: delayed) {
            if (d->path.empty()) {
                if (pinned_icon_data->surface) {
                    *d->surface = accelerated_surface(app, client, d->size, d->size);
                    cairo_t *cr = cairo_create(*d->surface);
                    
                    double starting_w = cairo_image_surface_get_width(pinned_icon_data->surface);
                    double target_w = 16 * config->dpi;
                    double sx = target_w / starting_w;
                    
                    cairo_scale(cr, sx, sx);
                    cairo_set_source_surface(cr, pinned_icon_data->surface, 0, 0);
                    cairo_paint(cr);
                    
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
