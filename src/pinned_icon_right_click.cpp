
#include "pinned_icon_right_click.h"
#include "INIReader.h"
#include "config.h"
#include "icons.h"
#include "main.h"
#include "taskbar.h"

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
    custom_items.clear();

    for (auto section : items_parsed.Sections()) {
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

    if (data->surface != nullptr) {
        int width = cairo_image_surface_get_width(data->surface);
        int height = cairo_image_surface_get_height(data->surface);

        if (dye) {
            dye_surface(data->surface, config->color_pin_menu_icons);
        }

        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + 12),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
        cairo_paint(cr);
    }
}

static void
paint_text(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (OptionData *) container->user_data;

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

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
    set_argb(cr, config->color_pin_menu_background);
    set_rect(cr, container->real_bounds);
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
            for (auto window : pinned_icon_data->windows) {
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
            cairo_surface_write_to_png(pinned_icon_data->surface, itemPath.c_str());

            update_pinned_items_file();
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
    }

    client_close(app, client);
}

class DelayedSurfacePainting {
public:
    cairo_surface_t **surface;
    std::string path;
    int size = 0;
};

static Container *
make_root(std::vector<DelayedSurfacePainting *> *delayed) {
    Container *root = new Container();
    root->type = vbox;
    root->when_paint = paint_root;

    std::vector<CustomItem *> custom_items_for_us;
    for (auto *item : custom_items) {
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
                title = root->child(FILL_SPACE, 30);
            } else {
                title = root->child(FILL_SPACE, 30);
            }
            title->when_paint = paint_title;
            auto *data = new OptionData();
            data->text_offset = 12;
            data->text = item->category;
            title->user_data = data;
            active_category = item->category;
        }

        auto *option = root->child(FILL_SPACE, 30);
        option->when_paint = paint_custom_option;
        auto *data = new OptionData();
        option->when_clicked = option_clicked;
        data->option_type = option_data_type::CUSTOM;
        data->user_data = item;
        data->text_offset = 40;
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
    auto *open = root->child(FILL_SPACE, 30);
    std::string path = find_icon(pinned_icon_data->icon_name, 16);

    auto *d = new DelayedSurfacePainting();
    d->surface = &data->surface;
    d->size = 16;

    if (!path.empty()) {
        d->path = path;
    } else {
        d->path = as_resource_path("unknown-16.svg");
    }
    delayed->push_back(d);

    open->when_paint = paint_open;
    open->when_clicked = option_clicked;

    data->text = pinned_icon_data->class_name;
    data->text_offset = 40;
    open->user_data = data;

    auto *pinned = root->child(FILL_SPACE, 30);
    pinned->when_paint = paint_option;
    data = new OptionData();
    pinned->when_clicked = option_clicked;

    d = new DelayedSurfacePainting();
    d->surface = &data->surface;
    d->size = 16;

    if (pinned_icon_data->pinned) {
        data->text = "Unpin from taskbar";
        d->path = as_resource_path("taskbar-unpin.png");
        data->option_type = option_data_type::UNPIN;
    } else {
        data->text = "Pin to taskbar";
        d->path = as_resource_path("taskbar-pin.png");
        data->option_type = option_data_type::PIN;
    }
    delayed->push_back(d);

    data->text_offset = 40;
    pinned->user_data = data;

    if (!pinned_icon_data->windows.empty()) {
        auto *close = root->child(FILL_SPACE, 30);
        close->when_paint = paint_option;
        close->when_clicked = option_clicked;
        data = new OptionData();
        data->option_type = option_data_type::CLOSE;
        if (pinned_icon_data->windows.size() == 1) {
            data->text = "Close window";
        } else {
            data->text = "Close all windows";
        }
        auto *d = new DelayedSurfacePainting();
        d->surface = &data->surface;
        d->path = as_resource_path("taskbar-close.png");
        d->size = 16;
        delayed->push_back(d);
        data->text_offset = 40;
        close->user_data = data;
    }

    auto *end_pad = root->child(FILL_SPACE, pad);

    double height = true_height(root);

    root->wanted_bounds.w = 256;
    root->wanted_bounds.h = height;
    return root;
}

static bool first_expose = false;

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
    }
}

static bool
icon_menu_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
            }
        }
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }
            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
    }

    return true;
}

void start_pinned_icon_right_click(Container *container) {
    pinned_icon_container = container;
    pinned_icon_data = (LaunchableButton *) container->user_data;

    load_custom_items();
    Settings settings;

    std::vector<DelayedSurfacePainting *> delayed;

    Container *root = make_root(&delayed);
    settings.w = root->wanted_bounds.w;
    settings.h = root->wanted_bounds.h;
    settings.x = container->real_bounds.x + container->real_bounds.w / 2 - settings.w / 2;
    settings.y = app->bounds.h - config->taskbar_height - settings.h;
    settings.decorations = false;
    settings.force_position = true;
    settings.skip_taskbar = true;
    settings.popup = true;

    AppClient *client = client_new(app, settings, "right_click_menu");
    client->grab_event_handler = grab_event_handler;
    delete client->root;
    client->root = root;
    client_entity = client;

    for (auto *d : delayed) {
        *d->surface = accelerated_surface(app, client, d->size, d->size);
        paint_surface_with_image(*d->surface, d->path, nullptr);
        delete d;
    }

    client_add_handler(app, client, icon_menu_event_handler);

    client_show(app, client);
}
