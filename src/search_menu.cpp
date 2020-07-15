//
// Created by jmanc3 on 6/25/20.
//

#include "search_menu.h"

#ifdef TRACY_ENABLE

#include "../tracy/Tracy.hpp"

#endif

#include "app_menu.h"
#include "application.h"
#include "components.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"

#include <algorithm>
#include <fstream>
#include <pango/pangocairo.h>

class Script : public Sortable {
public:
    std::string path;
};

class SearchItemData : public UserData {
public:
    Sortable *sortable = nullptr;
    void *user_data = nullptr;
    int item_number = 0;
};

class TitleData : public UserData {
public:
    std::string text;
};

class HistoricalNameUsed {
public:
    std::string text;
};

std::vector<HistoricalNameUsed *> history_scripts;
std::vector<HistoricalNameUsed *> history_apps;

std::vector<Script *> scripts;

static std::string active_tab = "Scripts";
static int active_item = 0;
static int scroll_amount = 0;

static cairo_surface_t *script_16 = nullptr;
static cairo_surface_t *script_32 = nullptr;
static cairo_surface_t *script_64 = nullptr;
static cairo_surface_t *arrow_right_surface = nullptr;
static cairo_surface_t *open_surface = nullptr;

class TabData : public UserData {
public:
    std::string name;

    cairo_surface_t *surface = nullptr;

    ~TabData() { cairo_surface_destroy(surface); }
};

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(0, 0, 0, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static bool first_expose = true;

static void
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            if (!valid_client(app, client)) {
                break;
            }
            Bounds search_bar = app->bounds;
            if (auto *taskbar = client_by_name(app, "taskbar")) {
                if (auto *container = container_by_name("field_search", taskbar->root)) {
                    search_bar = container->real_bounds;
                    search_bar.x += taskbar->bounds->x;
                    search_bar.y += taskbar->bounds->y;
                }
            }

            if (!bounds_contains(*client->bounds, e->root_x, e->root_y) &&
                !bounds_contains(search_bar, e->root_x, e->root_y)) {
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
search_menu_event_handler(App *app, xcb_generic_event_t *event) {

    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            break;
        }
        case XCB_KEY_PRESS: {
            auto *e = (xcb_key_press_event_t *) (event);

            on_key_press_search_bar(event);

            break;
        }
        case XCB_FOCUS_OUT: {
            auto *e = (xcb_focus_out_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (valid_client(app, client)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();
            }
            break;
        }
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);
            auto *client = client_by_window(app, e->event);
            if (!valid_client(app, client)) {
                break;
            }
            Bounds search_bar = app->bounds;
            if (auto *taskbar = client_by_name(app, "taskbar")) {
                if (auto *container = container_by_name("field_search", taskbar->root)) {
                    search_bar = container->real_bounds;
                    search_bar.x += taskbar->bounds->x;
                    search_bar.y += taskbar->bounds->y;
                }
            }

            if (!bounds_contains(*client->bounds, e->root_x, e->root_y) &&
                !bounds_contains(search_bar, e->root_x, e->root_y)) {
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

static void
paint_top(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(.122, .122, .122, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_splitter(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(.11, .11, .11, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_left_bg(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(.941, .941, .941, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_right_thumb(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    Container *scrollpane = container->parent->parent;

    auto right_bounds = right_thumb_bounds(scrollpane, container->real_bounds);

    right_bounds.x += right_bounds.w;
    right_bounds.w = std::max(right_bounds.w * 1, 2.0);
    right_bounds.x -= right_bounds.w;
    right_bounds.x -= 2 * (1 - 1);

    set_rect(cr, right_bounds);

    if (container->state.mouse_pressing) {
        set_argb(cr, ArgbColor(.682, .682, .682, 1));
    } else if (bounds_contains(right_bounds, client->mouse_current_x, client->mouse_current_y)) {
        set_argb(cr, ArgbColor(.525, .525, .525, 1));
    } else if (right_bounds.w == 2.0) {
        set_argb(cr, ArgbColor(.482, .482, .482, 1));
    } else {
        set_argb(cr, ArgbColor(.365, .365, .365, 1));
    }

    cairo_fill(cr);
}

static void
paint_show(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing) {
            set_argb(cr, ArgbColor(0, 0, 0, .3));
        } else {
            set_argb(cr, ArgbColor(0, 0, 0, .1));
        }
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
    }

    if (container->parent->active)
        set_argb(cr, ArgbColor(1, 0, 1, 1));
    else
        set_argb(cr, ArgbColor(0, 1, 1, 1));

    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, 1);
    cairo_fill(cr);

    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, 1, container->real_bounds.h);
    cairo_fill(cr);

    cairo_rectangle(cr,
                    container->real_bounds.x + container->real_bounds.w - 1,
                    container->real_bounds.y,
                    1,
                    container->real_bounds.h);
    cairo_fill(cr);

    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h - 1,
                    container->real_bounds.w,
                    1);
    cairo_fill(cr);
}

static inline int
determine_priority(Sortable *item,
                   const std::string &text,
                   const std::string &lowercase_text,
                   std::vector<HistoricalNameUsed *> *history) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (text.empty())
        return 11;
    // Sort priority (this won't try to do anything smart when searching for multiple words)
    //
    // 0: name found somewhere in history TODO: need to implement this
    // 1: Perfect match is highest
    // 2: Start of string, correct capitalization, closest in length
    // 3: Start of string, any     capitalization, closest in length
    // 4: Any position in string, correct capitalization, closest in length
    // 5: Any position in string, any     capitalization, closets in length
    //
    // For normal text

    unsigned long normal_find = item->name.find(text);
    unsigned long lowercase_find = item->lowercase_name.find(text);

    int prio = 11;
    if (normal_find == 0 && item->name.length() == text.length()) {
        prio = -1;// absolute perfect matches come before everything even historical
    } else if (normal_find == 0) {
        prio = 2;
    } else if ((lowercase_find) == 0) {
        prio = 3;
    } else if (normal_find != std::string::npos) {
        prio = 4;
    } else if (lowercase_find != std::string::npos) {
        prio = 5;
    }

    // For lowercase_text
    if (prio == 11) {
        normal_find = item->name.find(lowercase_text);
        if (normal_find == 0 && item->name.length() == lowercase_text.length()) {// perfect match
            prio = 6;
        } else if (normal_find == 0) {
            prio = 7;
        } else if ((lowercase_find = item->lowercase_name.find(lowercase_text)) == 0) {
            prio = 8;
        } else if (normal_find != std::string::npos) {
            prio = 9;
        } else if (lowercase_find != std::string::npos) {
            prio = 10;
        }
    }

    // Find it in history and attach a ranking
    if (prio != -1) {    // if it wasn't a perfect match
        if (prio != 11) {// but it was a match
            for (int i = 0; i < history->size(); i++) {
                HistoricalNameUsed *h = (*history)[i];
                auto lowercase_historic_find = h->text.find(item->lowercase_name);
                if (lowercase_historic_find != std::string::npos) {
                    item->historical_ranking = i;
                    prio = 0;
                    return prio;
                }
            }
        }
    }

    return prio;
}

static inline int
determine_priority_location(const Sortable &item,
                            const std::string &text,
                            const std::string &lowercase_text,
                            int *location) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    // Sort priority (this won't try to do anything smart when searching for multiple words)
    //
    // 0: name found somewhere in history TODO: need to implement this
    // 1: Perfect match is highest
    // 2: Start of string, correct capitalization, closest in length
    // 3: Start of string, any     capitalization, closest in length
    // 4: Any position in string, correct capitalization, closest in length
    // 5: Any position in string, any     capitalization, closets in length
    //
    // For normal text
    unsigned long normal_find = item.name.find(text);
    unsigned long lowercase_find;
    if (normal_find == 0 && item.name.length() == text.length()) {// perfect match
        *location = normal_find;
        return 1;
    } else if (normal_find == 0) {
        *location = normal_find;
        return 2;
    } else if ((lowercase_find = item.lowercase_name.find(text)) == 0) {
        *location = lowercase_find;
        return 3;
    } else if (normal_find != std::string::npos) {
        *location = normal_find;
        return 4;
    } else if (lowercase_find != std::string::npos) {
        *location = lowercase_find;
        return 5;
    }

    // For lowercase_text
    normal_find = item.name.find(lowercase_text);
    if (normal_find == 0 && item.name.length() == lowercase_text.length()) {// perfect match
        *location = normal_find;
        return 6;
    } else if (normal_find == 0) {
        *location = normal_find;
        return 7;
    } else if ((lowercase_find = item.lowercase_name.find(lowercase_text)) == 0) {
        *location = lowercase_find;
        return 8;
    } else if (normal_find != std::string::npos) {
        *location = normal_find;
        return 9;
    } else if (lowercase_find != std::string::npos) {
        *location = lowercase_find;
        return 10;
    }

    *location = -1;
    return 11;
}

static void
paint_item_background(AppClient *client, cairo_t *cr, Container *container, int other_index) {
    auto *data = (SearchItemData *) container->parent->user_data;
    if (data->item_number == active_item) {
        set_argb(cr, ArgbColor(.659, .800, .914, 1));
        set_rect(cr, container->real_bounds);
        cairo_fill(cr);
        return;
    }
    bool use_other_index = false;
    Container *something = nullptr;
    if (!container->state.mouse_pressing && !container->state.mouse_hovering) {
        if (!container->parent->children.empty()) {
            Container *other = container->parent->children[other_index];
            if (other) {
                if (other && other->state.mouse_pressing || other->state.mouse_hovering) {
                    use_other_index = true;
                }
            }
        }
    }
    something = use_other_index ? container->parent->children[other_index] : container;

    if (something->state.mouse_pressing || something->state.mouse_hovering) {
        if (something->state.mouse_pressing) {
            if (use_other_index) {
                set_argb(cr, ArgbColor(.753, .753, .753, 1));
            } else {
                set_argb(cr, ArgbColor(.678, .678, .678, 1));
            }
        } else {
            if (use_other_index) {
                set_argb(cr, ArgbColor(.847, .847, .847, 1));
            } else {
                set_argb(cr, ArgbColor(.761, .761, .761, 1));
            }
        }

    } else {
        set_argb(cr, ArgbColor(.941, .941, .941, 1));
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    if (other_index == 0 && (something->state.mouse_pressing || something->state.mouse_hovering)) {
        set_argb(cr, ArgbColor(1, 1, 1, 1));
        cairo_rectangle(
                cr, container->real_bounds.x, container->real_bounds.y, 1, container->real_bounds.h);
        cairo_fill(cr);
    }
}

static void
paint_right_item(AppClient *client, cairo_t *cr, Container *container) {
    paint_item_background(client, cr, container, 0);

    if (arrow_right_surface) {
        if (container->state.mouse_pressing) {
            dye_surface(arrow_right_surface, ArgbColor(1, 1, 1, 1));
        } else {
            dye_surface(arrow_right_surface, ArgbColor(.376, .376, .376, 1));
        }

        cairo_set_source_surface(cr,
                                 arrow_right_surface,
                                 container->real_bounds.x + container->real_bounds.w / 2 - 16 / 2,
                                 container->real_bounds.y + container->real_bounds.h / 2 - 16 / 2);
        cairo_paint(cr);
    }
}

static void
paint_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);

    int location = -1;
    int length = -1;
    if (auto *taskbar = client_by_name(client->app, "taskbar")) {
        if (auto *textarea = container_by_name("main_text_area", taskbar->root)) {
            auto *textarea_data = (TextAreaData *) textarea->user_data;
            std::string text(textarea_data->state->text);
            std::string lowercase_text(text);
            std::transform(
                    lowercase_text.begin(), lowercase_text.end(), lowercase_text.begin(), ::tolower);
            length = text.length();
            determine_priority_location(*data->sortable, text, lowercase_text, &location);
        }
    }

    if (location != -1) {
        pango_layout_set_attributes(layout, nullptr);
        std::string text(data->sortable->name);
        text.insert(location + length, "</b>");
        text.insert(location, "<b>");

        PangoAttrList *attrs;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);

        pango_layout_set_attributes(layout, attrs);
    }

    set_argb(cr, ArgbColor(0, 0, 0, 1));
    pango_layout_set_text(layout, data->sortable->name.data(), data->sortable->name.size());

    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);

    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 40),
                  (int) (container->real_bounds.y + (container->real_bounds.h / 2) -
                         ((logical.height / PANGO_SCALE) / 2)));
    pango_cairo_show_layout(cr, layout);

    pango_layout_set_attributes(layout, nullptr);

    if (active_tab == "Scripts") {
        if (script_16) {
            cairo_set_source_surface(cr,
                                     script_16,
                                     container->real_bounds.x + 12,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 8);
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        cairo_set_source_surface(cr,
                                 l_data->icon_16,
                                 container->real_bounds.x + 12,
                                 container->real_bounds.y + container->real_bounds.h / 2 - 8);
        cairo_paint(cr);
    }
}

static void
paint_top_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);

    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);

    int location = -1;
    int length = -1;
    if (auto *taskbar = client_by_name(client->app, "taskbar")) {
        if (auto *textarea = container_by_name("main_text_area", taskbar->root)) {
            auto *textarea_data = (TextAreaData *) textarea->user_data;
            std::string text(textarea_data->state->text);
            std::string lowercase_text(text);
            std::transform(
                    lowercase_text.begin(), lowercase_text.end(), lowercase_text.begin(), ::tolower);
            length = text.length();
            determine_priority_location(*data->sortable, text, lowercase_text, &location);
        }
    }

    if (location != -1) {
        pango_layout_set_attributes(layout, nullptr);
        std::string text(data->sortable->name);
        text.insert(location + length, "</b>");
        text.insert(location, "<b>");

        PangoAttrList *attrs;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);

        pango_layout_set_attributes(layout, attrs);
    }

    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(0, 0, 0, 1));
    cairo_move_to(cr, (int) (container->real_bounds.x + 56), (int) (container->real_bounds.y + 10));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);

    layout = get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, active_tab.c_str(), active_tab.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(.341, .341, .341, 1));
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 56),
                  (int) (container->real_bounds.y + container->real_bounds.h - 10 - height));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);

    if (active_tab == "Scripts") {
        if (script_32) {
            cairo_set_source_surface(cr,
                                     script_32,
                                     container->real_bounds.x + 12,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 16);
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        cairo_set_source_surface(cr,
                                 l_data->icon_32,
                                 container->real_bounds.x + 12,
                                 container->real_bounds.y + container->real_bounds.h / 2 - 16);
        cairo_paint(cr);
    }
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TitleData *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);

    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), data->text.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(0, 0, 0, 1));
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 13),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
}

static void
paint_right_active_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 13, PangoWeight::PANGO_WEIGHT_NORMAL);

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        std::string text(data->sortable->name);
        text.insert(text.length(), "</u>");
        text.insert(0, "<u>");

        PangoAttrList *attrs;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);

        pango_layout_set_attributes(layout, attrs);
    }

    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(0, 0, 0, 1));
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + 106 - height / 2));
    pango_cairo_show_layout(cr, layout);

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        pango_layout_set_attributes(layout, nullptr);
    }

    layout = get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    pango_layout_set_text(layout, active_tab.data(), active_tab.length());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(.404, .404, .404, 1));
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + 106 + height - (height / 3)));
    pango_cairo_show_layout(cr, layout);

    if (active_tab == "Scripts") {
        if (script_32) {
            cairo_set_source_surface(cr,
                                     script_64,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32,
                                     container->real_bounds.y + 21);
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        cairo_set_source_surface(cr,
                                 l_data->icon_64,
                                 container->real_bounds.x + container->real_bounds.w / 2 - 32,
                                 container->real_bounds.y + 21);
        cairo_paint(cr);
    }
}

static void
paint_spacer(AppClient *client, cairo_t *cr, Container *container) {
    Bounds b = container->real_bounds;
    b.x += 6;
    b.w -= 6;
    set_rect(cr, b);
    set_argb(cr, ArgbColor(.949, .949, .949, 1));
    cairo_fill(cr);
}

static void
paint_open(AppClient *client, cairo_t *cr, Container *container) {
    if (container->state.mouse_pressing || container->state.mouse_hovering) {
        if (container->state.mouse_pressing) {
            set_argb(cr, ArgbColor(.678, .678, .678, 1));
        } else {
            set_argb(cr, ArgbColor(.761, .761, .761, 1));
        }
    } else {
        set_argb(cr, ArgbColor(1, 1, 1, 1));
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);

    std::string text("Open");
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(0, 0, 0, 1));
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 52),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);

    if (open_surface) {
        dye_surface(open_surface, ArgbColor(.2, .5, .8, 1));
        cairo_set_source_surface(cr,
                                 open_surface,
                                 container->real_bounds.x + 23,
                                 container->real_bounds.y + container->real_bounds.h / 2 - 16 / 2);
        cairo_paint(cr);
    }
}

static void
launch_item(AppClient *client, Container *item) {
    SearchItemData *data = (SearchItemData *) item->user_data;
    if (active_tab == "Scripts") {
        Script *script = (Script *) data->user_data;

        for (int i = 0; i < history_scripts.size(); i++) {
            auto *historic_script = history_scripts[i];
            if (historic_script->text == script->lowercase_name) {
                delete historic_script;
                history_scripts.erase(history_scripts.begin() + i);
                break;
            }
        }
        auto *historic_script = new HistoricalNameUsed;
        historic_script->text = script->lowercase_name;
        history_scripts.insert(history_scripts.begin(), historic_script);
        if (history_scripts.size() > 100) {
            delete history_scripts[history_scripts.size() - 1];
            history_scripts.erase(history_scripts.end());
        }

        launch_command(script->path + "/" + script->name);
    } else if (active_tab == "Apps") {
        Launcher *launcher = (Launcher *) data->user_data;

        for (int i = 0; i < history_apps.size(); i++) {
            auto *historic_app = history_apps[i];
            if (historic_app->text == launcher->lowercase_name) {
                delete historic_app;
                history_apps.erase(history_apps.begin() + i);
                break;
            }
        }
        auto *historic_app = new HistoricalNameUsed;
        historic_app->text = launcher->lowercase_name;
        history_apps.insert(history_apps.begin(), historic_app);
        if (history_apps.size() > 100) {
            delete history_apps[history_apps.size() - 1];
            history_apps.erase(history_apps.end());
        }

        launch_command(launcher->exec);
    }
    if (auto *client = client_by_name(app, "taskbar")) {
        if (auto *container = container_by_name("main_text_area", client->root)) {
            container->parent->active = false;
            auto *data = (TextAreaData *) container->user_data;
            delete data->state;
            data->state = new TextState;
        }
    }
    client_close_threaded(app, client);
    xcb_flush(app->connection);
    app->grab_window = -1;
}

static void
launch_active_item() {
    if (AppClient *client = client_by_name(app, "search_menu")) {
        if (Container *container = container_by_name("content", client->root)) {
            for (int i = 0; i < container->children.size(); i++) {
                if (i == 0 || i == 2) {
                    continue;
                }
                Container *child = container->children[i];
                SearchItemData *data = (SearchItemData *) child->user_data;
                if (data->item_number == active_item) {
                    launch_item(client, child);
                    return;
                }
            }
        }
    }
}

static void
clicked_open(AppClient *client, cairo_t *cr, Container *container) {
    launch_active_item();
}

static void
clicked_item(AppClient *client, cairo_t *cr, Container *container) {
    launch_item(client, container->parent);
}

static void
clicked_right_active_title(AppClient *client, cairo_t *cr, Container *container) {
    launch_active_item();
}

static void
paint_content(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    cairo_save(cr);
    cairo_push_group(cr);

    for (auto *c : container->children) {
        if (overlaps(c->real_bounds, c->parent->parent->real_bounds)) {
            if (c->when_paint) {
                c->when_paint(client, cr, c);
            }
        }
    }

    cairo_pop_group_to_source(cr);

    cairo_rectangle(cr,
                    container->parent->real_bounds.x,
                    container->parent->real_bounds.y,
                    container->parent->real_bounds.w,
                    container->parent->real_bounds.h);
    cairo_clip(cr);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void
paint_hbox(AppClient *client, cairo_t *cr, Container *container) {
    for (auto *c : container->children) {
        if (c->when_paint) {
            c->when_paint(client, cr, c);
        }
    }
}

static void
paint_right_bg(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(.961, .961, .961, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_right_fg(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(1, 1, 1, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_bottom(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, ArgbColor(.165, .165, .165, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 20, PangoWeight::PANGO_WEIGHT_NORMAL);

    std::string min = active_tab;
    min[0] = std::tolower(min[0]);

    std::string text = "Start typing to search for " + min;

    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    set_argb(cr, ArgbColor(.663, .663, .663, 1));
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h - 256 - height / 2);
    pango_cairo_show_layout(cr, layout);

    if (auto *tab_group = container_by_name("tab_group", client->root)) {
        for (auto *tab : tab_group->children) {
            auto *tab_data = (TabData *) tab->user_data;
            if (tab_data->name == active_tab && tab_data->surface) {
                cairo_set_source_surface(cr,
                                         tab_data->surface,
                                         container->real_bounds.x + container->real_bounds.w / 2 -
                                         128 / 2,
                                         container->real_bounds.y + 156);
                cairo_paint(cr);
            }
        }
    }
}

static void
paint_tab(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (TabData *) container->user_data;
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 10, PangoWeight::PANGO_WEIGHT_BOLD);

    int width;
    int height;
    pango_layout_set_text(layout, data->name.c_str(), data->name.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    if (data->name == active_tab) {
        if (container->state.mouse_pressing) {
            set_argb(cr, ArgbColor(.9, .9, .9, 1));
        } else {
            set_argb(cr, ArgbColor(1, 1, 1, 1));
        }
    } else if (container->state.mouse_pressing) {
        set_argb(cr, ArgbColor(.65, .65, .65, 1));
    } else if (container->state.mouse_hovering) {
        set_argb(cr, ArgbColor(.85, .85, .85, 1));
    } else {
        set_argb(cr, ArgbColor(.75, .75, .75, 1));
    }
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);

    if (data->name == active_tab) {
        int height = 4;
        cairo_rectangle(cr,
                        container->real_bounds.x,
                        container->real_bounds.y + container->real_bounds.h - height,
                        container->real_bounds.w,
                        height);
        set_argb(cr, ArgbColor(.2, .5, .8, 1));
        cairo_fill(cr);
    }
}

static void
clicked_right_item(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *content = container_by_name("content", client->root)) {
        for (int i = 0; i < content->children.size(); i++) {
            auto *child = content->children[i];
            auto *data = (SearchItemData *) (child->user_data);
            if (data && data->item_number == active_item) {
                Container *right_item = child->child(49, FILL_SPACE);
                right_item->when_paint = paint_right_item;
                right_item->when_clicked = clicked_right_item;
            }
        }
    }
    auto *data = (SearchItemData *) container->parent->user_data;
    active_item = data->item_number;

    if (auto *content = container_by_name("content", client->root)) {
        for (int i = 0; i < content->children.size(); i++) {
            auto *child = content->children[i];
            auto *data = (SearchItemData *) (child->user_data);
            if (data && data->item_number == active_item) {
                delete child->children[1];
                child->children.erase(child->children.begin() + 1);

                if (auto *right_fg = container_by_name("right_fg", client->root)) {
                    auto *right_fg_data = new SearchItemData;
                    right_fg_data->user_data = data->user_data;
                    right_fg_data->item_number = data->item_number;
                    right_fg_data->sortable = data->sortable;
                    right_fg->user_data = right_fg_data;
                }
            }
        }
    }

    client_layout(app, client);
    request_refresh(app, client);
}

static void
clicked_tab(AppClient *client, cairo_t *cr, Container *container) {
    // This has to happen in another thread because on_key_press modifies the containers
    // and this function is called while iterating through them.
    std::thread t([client, container]() -> void {
        std::lock_guard<std::mutex>(client->app->clients_mutex);
        auto *data = (TabData *) container->user_data;
        active_tab = data->name;
        on_key_press_search_bar(nullptr);
    });
    t.detach();
}

static void
add_tab(AppClient *client, Container *tab_bar, std::string tab_name) {
    PangoLayout *layout =
            get_cached_pango_font(client->back_cr, config->font, 10, PangoWeight::PANGO_WEIGHT_BOLD);

    int width;
    int height;
    pango_layout_set_text(layout, tab_name.c_str(), tab_name.size());
    pango_layout_get_pixel_size(layout, &width, &height);

    auto *tab = tab_bar->child(width + 12 * 2, FILL_SPACE);
    auto *data = new TabData();
    data->name = tab_name;
    tab->user_data = data;
    tab->when_paint = paint_tab;
    tab->when_clicked = clicked_tab;

    data->surface = accelerated_surface(client->app, client, 128, 128);
    paint_surface_with_image(data->surface, as_resource_path(tab_name + ".png"), nullptr);
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->when_paint = paint_root;
    root->type = ::vbox;

    auto *top = root->child(FILL_SPACE, 51);
    top->type = ::hbox;
    top->when_paint = paint_top;
    top->spacing = 2;
    top->wanted_pad.x = 12;
    top->wanted_pad.w = 12;
    top->name = "tab_group";

    add_tab(client, top, "Apps");
    add_tab(client, top, "Scripts");
    {
        auto *tab = top->children[config->starting_tab_index];
        auto *tab_data = (TabData *) tab->user_data;
        active_tab = tab_data->name;
    }

    auto *splitter = root->child(FILL_SPACE, 1);
    splitter->when_paint = paint_splitter;

    auto *bottom = root->child(FILL_SPACE, FILL_SPACE);
    bottom->name = "bottom";
    bottom->when_paint = paint_bottom;

    script_16 = accelerated_surface(client->app, client, 16, 16);
    paint_surface_with_image(script_16, as_resource_path("script-16.svg"), nullptr);
    script_32 = accelerated_surface(client->app, client, 32, 32);
    paint_surface_with_image(script_32, as_resource_path("script-32.svg"), nullptr);
    script_64 = accelerated_surface(client->app, client, 64, 64);
    paint_surface_with_image(script_64, as_resource_path("script-64.svg"), nullptr);
    arrow_right_surface = accelerated_surface(client->app, client, 16, 16);
    paint_surface_with_image(arrow_right_surface, as_resource_path("arrow-right.png"), nullptr);
    open_surface = accelerated_surface(client->app, client, 16, 16);
    paint_surface_with_image(open_surface, as_resource_path("open.png"), nullptr);
}

static inline bool
compare_priority(Sortable *first, Sortable *second) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (first->priority != second->priority) {
        return first->priority < second->priority;
    }
    if (first->priority == 0) {
        return first->historical_ranking < second->historical_ranking;
    }
    return first->name.length() < second->name.length();
}

template<class T>
void sort_and_add(std::vector<T> *sortables,
                  Container *bottom,
                  std::string text,
                  std::vector<HistoricalNameUsed *> *history) {
    std::vector<T> sorted;

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("sorting_options");
#endif
        std::string lowercase_text(text);
        std::transform(
                lowercase_text.begin(), lowercase_text.end(), lowercase_text.begin(), ::tolower);

        for (auto *s : *sortables) {
            s->priority = determine_priority(s, text, lowercase_text, history);
            if (s->priority != 11) {
                sorted.push_back(s);
            }
        }

        std::sort(sorted.begin(), sorted.end(), compare_priority);
    }

    {
#ifdef TRACY_ENABLE
        ZoneScopedN("create_containers_for_sorted_items");
#endif
        Container *hbox = bottom->child(::hbox, FILL_SPACE, FILL_SPACE);
        Container *left = hbox->child(::vbox, 344, FILL_SPACE);
        left->when_paint = paint_left_bg;
        Container *right = hbox->child(::vbox, FILL_SPACE, FILL_SPACE);
        right->when_paint = paint_right_bg;
        right->wanted_pad = Bounds(12, 12, 12, 0);

        if (sorted.empty()) {
            return;
        }

        Container *right_fg = right->child(::vbox, FILL_SPACE, FILL_SPACE);
        right_fg->when_paint = paint_right_fg;
        right_fg->name = "right_fg";

        ScrollPaneSettings settings;
        settings.right_inline_track = true;
        settings.right_show_amount = 2;
        Container *content_area = make_scrollpane(left, settings);
        content_area->name = "content_area";
        content_area->scroll_v_real = scroll_amount;
        content_area->scroll_v_visual = scroll_amount;
        Container *top_arrow = content_area->parent->children[0]->children[0];
        Container *right_thumb = content_area->parent->children[0]->children[1];
        Container *bottom_arrow = content_area->parent->children[0]->children[2];
        right_thumb->when_paint = paint_right_thumb;

        Container *content = content_area->child(::vbox, FILL_SPACE, 0);
        content->spacing = 0;
        content->when_paint = paint_content;
        content->clip_children =
                false;// We have to do custom clipping so don't waste calls on this
        content->automatically_paint_children = false;
        content->name = "content";
        if (active_item < 0) {
            active_item = 0;
        }
        if (active_item > sorted.size() - 1) {
            active_item = sorted.size() - 1;
        }

        for (int i = 0; i < sorted.size(); i++) {
            if (i > 200) {
                break;
            }

            if (i == 0) {
                Container *item = content->child(::hbox, FILL_SPACE, 32);
                item->when_paint = paint_title;
                auto *data = new TitleData;
                data->text = "Best match";
                item->user_data = data;
            } else if (i == 1) {
                Container *item = content->child(::hbox, FILL_SPACE, 32);
                item->when_paint = paint_title;
                auto *data = new TitleData;
                data->text = "Other results";
                item->user_data = data;
            }

            bool top_item = i == 0;

            Container *hbox = content->child(::hbox, FILL_SPACE, top_item ? 64 : 36);
            hbox->when_paint = paint_hbox;
            Container *item = hbox->child(FILL_SPACE, FILL_SPACE);
            if (i == active_item) {
                auto *right_data = new SearchItemData;
                right_data->sortable = sorted[i];
                right_data->user_data = sorted[i];
                right_data->item_number = i;
                right_fg->user_data = right_data;

                Container *right_active_title = right_fg->child(FILL_SPACE, 176);
                right_active_title->when_paint = paint_right_active_title;
                right_active_title->when_clicked = clicked_right_active_title;

                auto *spacer = right_fg->child(FILL_SPACE, 2);
                spacer->when_paint = paint_spacer;

                right_fg->child(FILL_SPACE, 12);

                Container *open = right_fg->child(FILL_SPACE, 32);
                open->when_paint = paint_open;
                open->when_clicked = clicked_open;

                right_fg->child(FILL_SPACE, 12);
            } else {
                Container *right_item = hbox->child(49, FILL_SPACE);
                right_item->when_paint = paint_right_item;
                right_item->when_clicked = clicked_right_item;
            }

            item->when_paint = top_item ? paint_top_item : paint_item;
            item->when_clicked = clicked_item;
            auto *data = new SearchItemData;
            data->sortable = sorted[i];
            data->user_data = sorted[i];
            data->item_number = i;
            hbox->user_data = data;
        }

        content->wanted_bounds.h = true_height(content_area) + true_height(content);
    }
}

static void
next_tab() {
    // This has to happen in another thread because on_key_press modifies the containers
    // and this function is called while iterating through them.
    std::thread t([]() -> void {
        std::lock_guard<std::mutex> m(app->clients_mutex);

        if (auto *client = client_by_name(app, "search_menu")) {
            if (auto *tab_group = container_by_name("tab_group", client->root)) {
                for (int i = 0; i < tab_group->children.size(); i++) {
                    auto *tab = tab_group->children[i];
                    auto *tab_data = (TabData *) tab->user_data;

                    if (tab_data->name == active_tab) {
                        Container *should_be_active = nullptr;
                        if ((i + 1) == tab_group->children.size()) {
                            should_be_active = tab_group->children[0];
                        } else {
                            should_be_active = tab_group->children[i + 1];
                        }
                        if (should_be_active) {
                            auto *should_be_active_data = (TabData *) should_be_active->user_data;
                            active_tab = should_be_active_data->name;
                        }
                        break;
                    }
                }
            }
        }

        active_item = 0;
        on_key_press_search_bar(nullptr);
    });
    t.detach();
}

void on_key_press_search_bar(xcb_generic_event_t *event) {
    auto *search_menu_client = client_by_name(app, "search_menu");
    auto *taskbar_client = client_by_name(app, "taskbar");

    if (!search_menu_client || !taskbar_client) {
        return;
    }

    if (event) {
        switch (event->response_type) {
            case XCB_KEY_RELEASE:
            case XCB_KEY_PRESS: {
                auto *e = (xcb_key_press_event_t *) event;

                xkb_keycode_t keycode = e->detail;
                const xkb_keysym_t *keysyms;
                int num_keysyms =
                        xkb_state_key_get_syms(taskbar_client->keyboard->state, keycode, &keysyms);

                if (num_keysyms > 0) {
                    if (keysyms[0] == XKB_KEY_Up) {
                        active_item--;
                        break;
                    } else if (keysyms[0] == XKB_KEY_Down) {
                        active_item++;
                        break;
                    } else if (keysyms[0] == XKB_KEY_Escape) {
                        client_close_threaded(app, search_menu_client);
                        xcb_flush(app->connection);
                        app->grab_window = -1;
                        set_textarea_inactive();
                        break;
                    } else if (keysyms[0] == XKB_KEY_Tab) {
                        // next tab
                        next_tab();
                        client_layout(app, search_menu_client);
                        request_refresh(app, search_menu_client);
                        client_layout(app, taskbar_client);
                        request_refresh(app, taskbar_client);
                        return;
                    } else if (keysyms[0] == XKB_KEY_Return) {
                        // launch active item
                        launch_active_item();

                        break;
                    }
                }

                active_item = 0;
                scroll_amount = 0;
            }
                break;
        }
    }

    if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
        if (event != nullptr)
            textarea_handle_keypress(taskbar_client->app, event, textarea);

        auto *data = (TextAreaData *) textarea->user_data;

        auto *bottom = container_by_name("bottom", search_menu_client->root);
        if (bottom) {
            for (auto *c : bottom->children)
                delete c;
            bottom->children.clear();
            if (!data->state->text.empty()) {
                if (active_tab == "Scripts") {
                    sort_and_add<Script *>(&scripts, bottom, data->state->text, &history_scripts);
                } else if (active_tab == "Apps") {
                    // We create a copy because app_menu relies on the order
                    std::vector<Launcher *> launchers_copy;
                    for (auto *l : launchers) {
                        launchers_copy.push_back(l);
                    }
                    sort_and_add<Launcher *>(&launchers_copy, bottom, data->state->text, &history_apps);
                }
            }
        }
    }
    client_layout(app, search_menu_client);
    request_refresh(app, search_menu_client);
    client_layout(app, taskbar_client);
    request_refresh(app, taskbar_client);
}

void load_historic_scripts() {
    const char *home = getenv("HOME");
    std::string scriptsPath(home);
    scriptsPath += "/.config/winbar/historic/scripts.txt";

    std::ifstream status_file(scriptsPath);
    if (status_file.is_open()) {
        std::string line;
        while (getline(status_file, line)) {
            auto *h = new HistoricalNameUsed;
            h->text = line;
            history_scripts.push_back(h);
        }
    }
    status_file.close();
}

void load_historic_apps() {
    const char *home = getenv("HOME");
    std::string scriptsPath(home);
    scriptsPath += "/.config/winbar/historic/apps.txt";

    std::ifstream status_file(scriptsPath);
    if (status_file.is_open()) {
        std::string line;
        while (getline(status_file, line)) {
            auto *h = new HistoricalNameUsed;
            h->text = line;
            history_apps.push_back(h);
        }
    }
    status_file.close();
}

#include <cerrno>
#include <sys/stat.h> /* mkdir(2) */

static void
write_historic_scripts() {
    const char *home = getenv("HOME");
    std::string scriptsPath(home);
    scriptsPath += "/.config/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }

    scriptsPath += "/winbar/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }
    scriptsPath += "/historic/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }

    scriptsPath += "scripts.txt";

    std::ofstream myfile;
    myfile.open(scriptsPath);
    for (HistoricalNameUsed *h : history_scripts) {
        myfile << h->text + "\n";
    }
    myfile.close();
}

static void
write_historic_apps() {
    const char *home = getenv("HOME");
    std::string scriptsPath(home);
    scriptsPath += "/.config/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }

    scriptsPath += "/winbar/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }
    scriptsPath += "/historic/";

    if (mkdir(scriptsPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", scriptsPath.c_str());
            return;
        }
    }

    scriptsPath += "apps.txt";

    std::ofstream myfile;
    myfile.open(scriptsPath);
    for (HistoricalNameUsed *h : history_apps) {
        myfile << h->text + "\n";
    }
    myfile.close();
}

static void
search_menu_when_closed(AppClient *client) {
    cairo_surface_destroy(script_16);
    cairo_surface_destroy(script_32);
    cairo_surface_destroy(script_64);
    cairo_surface_destroy(arrow_right_surface);
    cairo_surface_destroy(open_surface);
    write_historic_scripts();
    write_historic_apps();
    std::thread(load_scripts).detach();
}

void start_search_menu() {
    first_expose = true;
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.force_position = true;
    settings.w = 762;
    settings.h = 641;
    int width = 48;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        width = taskbar->root->children[0]->real_bounds.w;
    }
    settings.x = width;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.popup = true;

    AppClient *client = client_new(app, settings, "search_menu");
    client->grab_event_handler = grab_event_handler;
    client->when_closed = search_menu_when_closed;
    fill_root(client);
    client_add_handler(app, client, search_menu_event_handler);
    client_show(app, client);
    set_textarea_active();
}

#include <dirent.h>
#include <sstream>

static std::mutex script_loaded;

void load_scripts() {
    std::lock_guard m(script_loaded);
    scripts.clear();
    // go through every directory in $PATH environment variable
    // add to our scripts list if the files we check are executable
    std::string paths = std::string(getenv("PATH"));

    std::replace(paths.begin(), paths.end(), ':', ' ');

    std::stringstream ss(paths);
    std::string string_path;
    while (ss >> string_path) {
        DIR *dir = opendir(string_path.c_str());
        struct dirent *dp;
        while ((dp = readdir(dir)) != NULL) {
            struct stat st, ln;

            // This is what determines if its an executable and its from dmenu and its not good. oh
            // well
            static int flag[26];
#define FLAG(x) (flag[(x) - 'a'])

            const char *path = string_path.c_str();
            if ((!stat(path, &st) && (FLAG('a') || dp->d_name[0] != '.')       /* hidden files      */
                 && (!FLAG('b') || S_ISBLK(st.st_mode))                        /* block special     */
                 && (!FLAG('c') || S_ISCHR(st.st_mode))                        /* character special */
                 && (!FLAG('d') || S_ISDIR(st.st_mode))                        /* directory         */
                 && (!FLAG('e') || access(path, F_OK) == 0)                    /* exists            */
                 && (!FLAG('f') || S_ISREG(st.st_mode))                        /* regular file      */
                 && (!FLAG('g') || st.st_mode & S_ISGID)                       /* set-group-id flag */
                 && (!FLAG('h') || (!lstat(path, &ln) && S_ISLNK(ln.st_mode))) /* symbolic link */
                 && (!FLAG('p') || S_ISFIFO(st.st_mode))                       /* named pipe        */
                 && (!FLAG('r') || access(path, R_OK) == 0)                    /* readable          */
                 && (!FLAG('s') || st.st_size > 0)                             /* not empty         */
                 && (!FLAG('u') || st.st_mode & S_ISUID)                       /* set-user-id flag  */
                 && (!FLAG('w') || access(path, W_OK) == 0)                    /* writable          */
                 && (!FLAG('x') || access(path, X_OK) == 0)) != FLAG('v')) {   /* executable        */

                if (!(FLAG('q'))) {
                    bool already_have_this_script = false;
                    std::string name = std::string(dp->d_name);
                    for (auto *script : scripts) {
                        if (script->name == name) {
                            already_have_this_script = true;
                            break;
                        }
                    }
                    if (already_have_this_script)
                        continue;

                    auto *script = new Script();
                    script->name = name;
                    script->lowercase_name = script->name;
                    std::transform(script->lowercase_name.begin(),
                                   script->lowercase_name.end(),
                                   script->lowercase_name.begin(),
                                   ::tolower);

                    script->path = path;
                    if (!script->path.empty()) {
                        if (script->path[script->path.length() - 1] == '/' ||
                            script->path[script->path.length() - 1] == '\\') {
                            script->path.erase(script->path.begin() + (script->path.length() - 1));
                        }
                    }

                    scripts.push_back(script);
                }
            }
        }
        closedir(dir);
    }
}
