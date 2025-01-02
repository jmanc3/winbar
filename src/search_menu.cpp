//
// Created by jmanc3 on 6/25/20.
//

#include "search_menu.h"

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include "app_menu.h"
#include "application.h"
#include "components.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "globals.h"
#include "defer.h"
#include "simple_dbus.h"
#include "settings_menu.h"

#include <algorithm>
#include <fstream>
#include <pango/pangocairo.h>

class Script : public Sortable {
public:
    std::string path;
    
    bool path_is_full_command = false;
};

class SearchItemData : public UserData {
public:
    Sortable *sortable = nullptr;
    void *user_data = nullptr;
    int item_number = 0;
    bool delete_user_data_as_script = false;
    
    ~SearchItemData() {
        if (delete_user_data_as_script) {
            delete (Script *) user_data;
        }
    }
};

class TitleData : public UserData {
public:
    std::string text;
};

std::vector<Script *> scripts;

std::string active_tab = "Apps";
static int active_item = 0;
static int max_items = 0;
static int scroll_amount = 0;

static int on_menu_items = 0;
static int active_menu_item = 0;

static cairo_surface_t *script_16 = nullptr;
static cairo_surface_t *script_32 = nullptr;
static cairo_surface_t *script_64 = nullptr;

class TabData : public UserData {
public:
    std::string name;
};

template<class T>
void sort_and_add(std::vector<T> *sortables,
                  Container *bottom,
                  std::string text,
                  const std::vector<HistoricalNameUsed *> &history);

void update_options();

static void
paint_top(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, correct_opaqueness(client, config->color_search_tab_bar_background));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_top_splitter(AppClient *client, cairo_t *cr, Container *container) {
    ArgbColor color = correct_opaqueness(client, config->color_search_tab_bar_background);
    darken(&color, 7);
    set_argb(cr, color);
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_left_bg(AppClient *client, cairo_t *cr, Container *container) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_argb(cr, correct_opaqueness(client, config->color_search_content_left_background));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

static inline int
determine_priority(Sortable *item,
                   const std::string &text,
                   const std::string &lowercase_text,
                   const std::vector<HistoricalNameUsed *> &history) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (text.empty())
        return 11;
    // Sort priority (this won't try to do anything smart when searching for multiple words at the same time: "Firefox Steam")
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
            for (int i = 0; i < history.size(); i++) {
                HistoricalNameUsed *h = (history)[i];
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
        if (on_menu_items) {
            set_argb(cr, config->color_search_content_left_button_hovered);
            set_rect(cr, container->real_bounds);
            cairo_fill(cr);
        } else {
            set_argb(cr, config->color_search_content_left_button_active);
            set_rect(cr, container->real_bounds);
            cairo_fill(cr);
        }
        return;
    }
    if (other_index >= container->parent->children.size())
        return;
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
                set_argb(cr, config->color_search_content_left_set_active_button_pressed);
            } else {
                set_argb(cr, config->color_search_content_left_button_pressed);
            }
        } else {
            if (use_other_index) {
                set_argb(cr, config->color_search_content_left_set_active_button_hovered);
            } else {
                set_argb(cr, config->color_search_content_left_button_hovered);
            }
        }
    } else {
        set_argb(cr, config->color_search_content_left_button_default);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    if (other_index == 0 && (something->state.mouse_pressing || something->state.mouse_hovering)) {
        set_argb(cr, config->color_search_content_left_button_splitter);
        cairo_rectangle(
                cr, container->real_bounds.x, container->real_bounds.y, 1, container->real_bounds.h);
        cairo_fill(cr);
    }
}

static void
paint_right_item(AppClient *client, cairo_t *cr, Container *container) {
    paint_item_background(client, cr, container, 0);
    
    PangoLayout *icon_layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 10 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(icon_layout, "\uE974", strlen("\uE83F"));
    
    if (container->state.mouse_pressing) {
        set_argb(cr, config->color_search_content_left_set_active_button_icon_pressed);
    } else {
        set_argb(cr, config->color_search_content_left_set_active_button_icon_default);
    }
    
    int width;
    int height;
    pango_layout_get_pixel_size_safe(icon_layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, icon_layout);
}

static void
paint_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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
        
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        
        if (layout && attrs) {
            pango_layout_set_attributes(layout, attrs);
        }
    }
    
    set_argb(cr, config->color_search_content_text_primary);
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + (40 * config->dpi)),
                  (int) (container->real_bounds.y + (container->real_bounds.h / 2) -
                         ((logical.height / PANGO_SCALE) / 2)));
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_attributes(layout, nullptr);
    
    if (active_tab == "Scripts") {
        if (script_16) {
            cairo_set_source_surface(cr,
                                     script_16,
                                     container->real_bounds.x + (12 * config->dpi),
                                     container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi));
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_16) {
            cairo_set_source_surface(cr,
                                     l_data->icon_16,
                                     container->real_bounds.x + 12 * config->dpi,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi);
        } else {
            cairo_set_source_surface(cr,
                                     global->unknown_icon_16,
                                     container->real_bounds.x + 12 * config->dpi,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi);
        }
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
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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
        
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        
        if (layout && attrs) {
            pango_layout_set_attributes(layout, attrs);
        }
    }
    
    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    cairo_move_to(cr, (int) (container->real_bounds.x + 56 * config->dpi),
                  (int) (container->real_bounds.y + 10 * config->dpi));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, active_tab.c_str(), active_tab.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_secondary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 56 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h - 10 * config->dpi - height));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);
    
    if (active_tab == "Scripts") {
        if (script_32) {
            cairo_set_source_surface(cr,
                                     script_32,
                                     container->real_bounds.x + 12 * config->dpi,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_32) {
            cairo_set_source_surface(cr,
                                     l_data->icon_32,
                                     container->real_bounds.x + 12 * config->dpi,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        } else {
            cairo_set_source_surface(cr,
                                     global->unknown_icon_32,
                                     container->real_bounds.x + 12 * config->dpi,
                                     container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        }
        cairo_paint(cr);
    }
}

static void
paint_no_result_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    
    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 11 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
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
        
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        
        if (layout && attrs) {
            pango_layout_set_attributes(layout, attrs);
        }
    }
    
    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    cairo_move_to(cr, (int) (container->real_bounds.x + 56 * config->dpi),
                  (int) (container->real_bounds.y + 10 * config->dpi));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string subtitle_text = "Run command anyways";
    pango_layout_set_text(layout, subtitle_text.c_str(), subtitle_text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_secondary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 56 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h - 10 * config->dpi - height));
    pango_cairo_show_layout(cr, layout);
    pango_layout_set_attributes(layout, nullptr);
    
    cairo_set_source_surface(cr,
                             script_32,
                             container->real_bounds.x + 12 * config->dpi,
                             container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
    cairo_paint(cr);
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TitleData *) container->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->text.c_str(), data->text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 13 * config->dpi),
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
            get_cached_pango_font(cr, config->font, 13 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        std::string text(data->sortable->name);
        text.insert(text.length(), "</u>");
        text.insert(0, "<u>");
        
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        
        if (layout && attrs) {
            pango_layout_set_attributes(layout, attrs);
        }
    }
    
    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    int x = (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2);
    if (x < container->real_bounds.x)
        x = container->real_bounds.x;
    cairo_move_to(cr,
                  x,
                  (int) (container->real_bounds.y + 106 * config->dpi - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        pango_layout_set_attributes(layout, nullptr);
    }
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    pango_layout_set_text(layout, active_tab.data(), active_tab.length());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_secondary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + 106 * config->dpi + height - (height / 3)));
    pango_cairo_show_layout(cr, layout);
    
    if (active_tab == "Scripts") {
        if (script_32) {
            cairo_set_source_surface(cr,
                                     script_64,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                     container->real_bounds.y + 21 * config->dpi);
            cairo_paint(cr);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_64) {
            cairo_set_source_surface(cr,
                                     l_data->icon_64,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                     container->real_bounds.y + 21 * config->dpi);
        } else {
            cairo_set_source_surface(cr,
                                     global->unknown_icon_64,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                     container->real_bounds.y + 21 * config->dpi);
        }
        cairo_paint(cr);
    }
}

static void
paint_right_active_title_for_no_results(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (SearchItemData *) container->parent->user_data;
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 13 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        std::string text(data->sortable->name);
        text.insert(text.length(), "</u>");
        text.insert(0, "<u>");
        
        PangoAttrList *attrs = nullptr;
        pango_parse_markup(text.data(), text.length(), 0, &attrs, NULL, NULL, NULL);
        
        if (layout && attrs) {
            pango_layout_set_attributes(layout, attrs);
        }
    }
    
    int width;
    int height;
    pango_layout_set_text(layout, data->sortable->name.c_str(), data->sortable->name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    int x = (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2);
    if (x < container->real_bounds.x)
        x = container->real_bounds.x;
    cairo_move_to(cr,
                  x,
                  (int) (container->real_bounds.y + 106 * config->dpi - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        pango_layout_set_attributes(layout, nullptr);
    }
    
    layout = get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string subtitle_text = "Run command anyways";
    pango_layout_set_text(layout, subtitle_text.data(), subtitle_text.length());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_secondary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  (int) (container->real_bounds.y + 106 * config->dpi + height - (height / 3)));
    pango_cairo_show_layout(cr, layout);
    
    if (script_32) {
        cairo_set_source_surface(cr,
                                 script_64,
                                 container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                 container->real_bounds.y + 21 * config->dpi);
        cairo_paint(cr);
    }
    
    
}

static void
paint_spacer(AppClient *client, cairo_t *cr, Container *container) {
    Bounds b = container->real_bounds;
    b.x += 6 * config->dpi;
    b.w -= (6 * 2) * config->dpi;
    set_rect(cr, b);
    set_argb(cr, config->color_search_content_right_splitter);
    cairo_fill(cr);
}

static void
paint_sub_option(AppClient *client, cairo_t *cr, Container *container, std::string text, std::string icon) {
    int index = 0;
    for (int i = 0; i < container->parent->children.size(); ++i) {
        if (container->parent->children[i] == container) {
            index = i;
            break;
        }
    }
    index -= 3;
    
    if (container->state.mouse_pressing || container->state.mouse_hovering ||
        (on_menu_items && index == active_menu_item)) {
        if (on_menu_items && index == active_menu_item) {
            set_argb(cr, config->color_search_content_left_button_active);
        } else if (container->state.mouse_pressing) {
            set_argb(cr, config->color_search_content_right_button_pressed);
        } else {
            set_argb(cr, config->color_search_content_right_button_hovered);
        }
    } else {
        set_argb(cr, config->color_search_content_right_button_default);
    }
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);

    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_content_text_primary);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 52 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2));
    pango_cairo_show_layout(cr, layout);
    
    
    PangoLayout *icon_layout =
            get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 12 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
    pango_layout_set_text(icon_layout, icon.c_str(), strlen("\uE83F"));

    set_argb(cr, config->color_search_accent);
    
    pango_layout_get_pixel_size_safe(icon_layout, &width, &height);
    
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + 23 * config->dpi),
                  (int) (container->real_bounds.y + container->real_bounds.h / 2 -
                         ((16 / 2) * config->dpi)));
    pango_cairo_show_layout(cr, icon_layout);
}

static void
paint_open(AppClient *client, cairo_t *cr, Container *container) {
    paint_sub_option(client, cr, container, "Open", "\uE8A7");
}

static void
paint_run(AppClient *client, cairo_t *cr, Container *container) {
    paint_sub_option(client, cr, container, "Run", "\uE8A7");
}

static void
paint_open_in_folder(AppClient *client, cairo_t *cr, Container *container) {
    paint_sub_option(client, cr, container, "Open file location", "\uED43");
}

static void
paint_live_tile_button(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (SearchItemData *) container->parent->user_data;
    std::string text = "Pin to Start";
    for (const auto &item: launchers) {
        if (item->full_path == data->sortable->full_path) {
            if (item->is_pinned) {
                text = "Unpin from Start";
                break;
            }
        }
    }
    if (text == "Pin to Start") {
        paint_sub_option(client, cr, container, text, "\uE718");
    } else {
        paint_sub_option(client, cr, container, text, "\uE77A");
    }
}

void update_options() {
    if (auto taskbar_client = client_by_name(app, "taskbar")) {
        if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
            if (auto *search_menu_client = client_by_name(app, "search_menu")) {
                auto *data = (TextAreaData *) textarea->user_data;
                
                auto *bottom = container_by_name("bottom", search_menu_client->root);
                if (bottom) {
                    for (auto *c: bottom->children)
                        delete c;
                    bottom->children.clear();
                    bottom->children.shrink_to_fit();
                    if (!data->state->text.empty()) {
                        active_item = 0;
                        scroll_amount = 0;
                        on_menu_items = false;
                        active_menu_item = 0;
                        
                        if (active_tab == "Scripts") {
                            sort_and_add<Script *>(&scripts, bottom, data->state->text, global->history_scripts);
                        } else if (active_tab == "Apps") {
                            // We create a copy because app_menu relies on the order
                            std::vector<Launcher *> launchers_copy;
                            for (auto *l: launchers) {
                                launchers_copy.push_back(l);
                            }
                            sort_and_add<Launcher *>(&launchers_copy, bottom, data->state->text,
                                                     global->history_apps);
                        }
                    }
                    client_layout(app, search_menu_client);
                    client_paint(app, search_menu_client);
                }
            }
        }
    }
}

static void
launch_item(AppClient *client, Container *item) {
    SearchItemData *data = (SearchItemData *) item->user_data;
    if (active_tab == "Scripts" || data->delete_user_data_as_script) {
        Script *script = (Script *) data->user_data;
        
        for (int i = 0; i < global->history_scripts.size(); i++) {
            auto *historic_script = global->history_scripts[i];
            if (historic_script->text == script->lowercase_name) {
                delete historic_script;
                global->history_scripts.erase(global->history_scripts.begin() + i);
                break;
            }
        }
        auto *historic_script = new HistoricalNameUsed;
        historic_script->text = script->lowercase_name;
        global->history_scripts.insert(global->history_scripts.begin(), historic_script);
        if (global->history_scripts.size() > 100) {
            delete global->history_scripts[global->history_scripts.size() - 1];
            global->history_scripts.erase(global->history_scripts.end() - 1);
        }
        
        if (script->path_is_full_command) {
            launch_command(script->path);
        } else {
            launch_command(script->path + "/" + script->name);
        }
    } else if (active_tab == "Apps") {
        Launcher *launcher = (Launcher *) data->user_data;
        
        for (int i = 0; i < global->history_apps.size(); i++) {
            auto *historic_app = global->history_apps[i];
            if (historic_app->text == launcher->lowercase_name) {
                delete historic_app;
                global->history_apps.erase(global->history_apps.begin() + i);
                break;
            }
        }
        auto *historic_app = new HistoricalNameUsed;
        historic_app->text = launcher->lowercase_name;
        global->history_apps.insert(global->history_apps.begin(), historic_app);
        if (global->history_apps.size() > 100) {
            delete global->history_apps[global->history_apps.size() - 1];
            global->history_apps.erase(global->history_apps.end() - 1);
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
    set_textarea_inactive();
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
clicked_open_in_folder(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (SearchItemData *) container->parent->user_data;
    dbus_open_in_folder(data->sortable->full_path);
}

static void
clicked_live_tiles(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (SearchItemData *) container->parent->user_data;
    for (auto item: launchers) {
        if (item->full_path == data->sortable->full_path) {
            item->is_pinned = !item->is_pinned;
        }
    }
    save_live_tiles();
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
    
    for (auto *c: container->children) {
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
    for (auto *c: container->children) {
        if (c->when_paint) {
            c->when_paint(client, cr, c);
        }
    }
}

static void
paint_right_bg(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, correct_opaqueness(client, config->color_search_content_right_background));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_right_fg(AppClient *client, cairo_t *cr, Container *container) {
    set_argb(cr, correct_opaqueness(client, config->color_search_content_right_foreground));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
}

static void
paint_bottom(AppClient *client, cairo_t *cr, Container *container) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_argb(cr, correct_opaqueness(client, config->color_search_empty_tab_content_background));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    
    if (!container->children.empty()) {
        return;
    }
    
    PangoLayout *layout =
            get_cached_pango_font(cr, config->font, 20 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    
    std::string min = active_tab;
    min[0] = std::tolower(min[0]);
    
    std::string text = "Start typing to search for " + min;
    
    int width;
    int height;
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    set_argb(cr, config->color_search_empty_tab_content_text);
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h - 256 * config->dpi - height / 2);
    pango_cairo_show_layout(cr, layout);
    
    if (auto *tab_group = container_by_name("tab_group", client->root)) {
        for (auto *tab: tab_group->children) {
            auto *tab_data = (TabData *) tab->user_data;
            if (tab_data->name == active_tab) {
    
                PangoLayout *icon_layout =
                        get_cached_pango_font(cr, "Segoe MDL2 Assets Mod", 100 * config->dpi,
                                              PangoWeight::PANGO_WEIGHT_NORMAL);
    
                // from https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
                if (tab_data->name == "Apps") {
                    pango_layout_set_text(icon_layout, "\uF8A5", strlen("\uE83F"));
                } else if (tab_data->name == "Scripts") {
                    pango_layout_set_text(icon_layout, "\uE62F", strlen("\uE83F"));
                }
    
                set_argb(cr, config->color_search_empty_tab_content_icon);
    
                int width;
                int height;
                pango_layout_get_pixel_size_safe(icon_layout, &width, &height);
    
                cairo_move_to(cr,
                              (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                              (int) (container->real_bounds.y + container->real_bounds.h / 2 - height / 2 - 100 * config->dpi));
                pango_cairo_show_layout(cr, icon_layout);
            }
        }
    }
}

static void
paint_tab(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (TabData *) container->user_data;
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    
    int width;
    int height;
    pango_layout_set_text(layout, data->name.c_str(), data->name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    if (data->name == active_tab) {
        set_argb(cr, config->color_search_tab_bar_active_text);
    } else if (container->state.mouse_pressing) {
        set_argb(cr, config->color_search_tab_bar_pressed_text);
    } else if (container->state.mouse_hovering) {
        set_argb(cr, config->color_search_tab_bar_hovered_text);
    } else {
        set_argb(cr, config->color_search_tab_bar_default_text);
    }
    cairo_move_to(cr,
                  container->real_bounds.x + container->real_bounds.w / 2 - width / 2,
                  container->real_bounds.y + container->real_bounds.h / 2 - height / 2);
    pango_cairo_show_layout(cr, layout);
    
    if (data->name == active_tab) {
        int height = 4 * config->dpi;
        cairo_rectangle(cr,
                        container->real_bounds.x,
                        container->real_bounds.y + container->real_bounds.h - height,
                        container->real_bounds.w,
                        height);
        set_argb(cr, config->color_search_accent);
        cairo_fill(cr);
    }
}

static void
clicked_right_item(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *content = container_by_name("content", client->root)) {
        for (int i = 0; i < content->children.size(); i++) {
            if (i == 0 || i == 2) continue;
            auto *child = content->children[i];
            if (child->children.size() < 2) {
                Container *right_item = child->child(49 * config->dpi, FILL_SPACE);
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
clicked_tab_timeout(App *app, AppClient *client, Timeout *, void *user_data) {
    auto *container = (Container *) user_data;
    auto *tab_data = (TabData *) container->user_data;
    active_tab = tab_data->name;
    update_options();
}

static void
clicked_tab(AppClient *client, cairo_t *cr, Container *container) {
    // This has to happen in another thread because on_key_press modifies the containers
    // and this function is called while iterating through them.
    app_timeout_create(app, client, 0, clicked_tab_timeout, container, const_cast<char *>(__PRETTY_FUNCTION__));
}

static void
add_tab(AppClient *client, Container *tab_bar, std::string tab_name) {
    PangoLayout *layout =
            get_cached_pango_font(client->cr, config->font, 10 * config->dpi, PangoWeight::PANGO_WEIGHT_BOLD);
    
    int width;
    int height;
    pango_layout_set_text(layout, tab_name.c_str(), tab_name.size());
    pango_layout_get_pixel_size_safe(layout, &width, &height);
    
    auto *tab = tab_bar->child((width + 12 * 2) * config->dpi, FILL_SPACE);
    auto *data = new TabData();
    data->name = tab_name;
    tab->user_data = data;
    tab->when_paint = paint_tab;
    tab->when_clicked = clicked_tab;
}

static inline bool
compare_priority(Sortable *first, Sortable *second) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (first->priority == -1 && second->priority != -1) {
        return true;
    } else if (second->priority == -1 && first->priority != -1) {
        return false;
    }
    if (first->priority == 0 && second->priority == 0) {
        return first->historical_ranking < second->historical_ranking;
    }
    if (first->priority == 0) {
        return true;
    } else if (second->priority == 0) {
        return false;
    }
    if (first->priority != second->priority) {
        return first->priority < second->priority;
    }
    return first->name.length() < second->name.length();
}

static bool can_pop = false;

template<class T>
void sort_and_add(std::vector<T> *sortables,
                  Container *bottom,
                  std::string text,
                  const std::vector<HistoricalNameUsed *> &history) {
    std::vector<T> sorted;
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("sorting_options");
#endif
        std::string lowercase_text(text);
        std::transform(
                lowercase_text.begin(), lowercase_text.end(), lowercase_text.begin(), ::tolower);
        
        for (int i = 0; i < sortables->size(); i++) {
            Sortable *s = (*sortables)[i];
            s->priority = determine_priority(s, text, lowercase_text, history);
            if (s->priority != 11) {
                sorted.push_back((*sortables)[i]);
            }
        }
        
        std::stable_sort(sorted.begin(), sorted.end(), compare_priority);
    }
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("create_containers_for_sorted_items");
#endif
        Container *hbox = bottom->child(::hbox, FILL_SPACE, FILL_SPACE);
        Container *left = hbox->child(::vbox, 344 * config->dpi, FILL_SPACE);
        left->when_paint = paint_left_bg;
        Container *right = hbox->child(::vbox, FILL_SPACE, FILL_SPACE);
        right->when_paint = paint_right_bg;
        right->wanted_pad = Bounds(12 * config->dpi, 12 * config->dpi, 12 * config->dpi, 0);
        
        Container *right_fg = right->child(::vbox, FILL_SPACE, FILL_SPACE);
        right_fg->when_paint = paint_right_fg;
        right_fg->name = "right_fg";
        
        ScrollPaneSettings settings(config->dpi);
        settings.right_show_amount = 2;
        ScrollContainer *scroll = make_newscrollpane_as_child(left, settings);
        scroll->name = "scroll";
        scroll->scroll_v_real = scroll_amount;
        scroll->scroll_v_visual = scroll_amount;
        
        Container *content = scroll->content;
        content->spacing = 0;
        if (auto client = client_by_name(app, "search_menu")) {
            if (can_pop) {
                can_pop = false;
                content->spacing = 16 * config->dpi;
                client_create_animation(app, client, &content->spacing, content->lifetime, 0, 120, nullptr, 0, true);
            }
        }

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
        max_items = sorted.size() - 1;
        if (max_items < 0)
            max_items = 0;
        
        if (sorted.empty()) {
            Container *title = content->child(::hbox, FILL_SPACE, 32 * config->dpi);
            title->when_paint = paint_title;
            auto *title_data = new TitleData;
            title_data->text = "Zero results";
            title->user_data = title_data;
            
            bool top_item = true;
            Container *hbox = content->child(::hbox, FILL_SPACE, top_item ? 64 * config->dpi : 32 * config->dpi);
            hbox->when_paint = paint_hbox;
            Container *item = hbox->child(FILL_SPACE, FILL_SPACE);
            
            item->when_paint = paint_no_result_item;
            item->when_clicked = clicked_item;
            auto *data = new SearchItemData;
            auto *sortable_data = new Script;
            sortable_data->name = text;
            sortable_data->lowercase_name = text;
            sortable_data->priority = -1;
            sortable_data->historical_ranking = -1;
            sortable_data->path_is_full_command = true;
            sortable_data->path = text;
            
            data->sortable = sortable_data;
            data->user_data = sortable_data;
            data->delete_user_data_as_script = true;
            data->item_number = 0;
            hbox->user_data = data;
            
            auto *right_data = new SearchItemData;
            right_data->sortable = sortable_data;
            right_data->user_data = sortable_data;
            right_data->item_number = 0;
            right_fg->user_data = right_data;
            
            Container *right_active_title = right_fg->child(FILL_SPACE, 176 * config->dpi);
            right_active_title->when_paint = paint_right_active_title_for_no_results;
            right_active_title->when_clicked = clicked_right_active_title;
            
            auto *spacer = right_fg->child(FILL_SPACE, 2 * config->dpi);
            spacer->when_paint = paint_spacer;
            
            right_fg->child(FILL_SPACE, 12 * config->dpi);
            
            Container *open = right_fg->child(FILL_SPACE, 32 * config->dpi);
            open->when_paint = paint_run;
            open->when_clicked = clicked_open;
            
            right_fg->child(FILL_SPACE, 12 * config->dpi);
            return;
        }
        
        for (int i = 0; i < sorted.size(); i++) {
            if (i > 200) {
                break;
            }
            
            if (i == 0) {
                Container *item = content->child(::hbox, FILL_SPACE, 32 * config->dpi);
                item->when_paint = paint_title;
                auto *data = new TitleData;
                data->text = "Best match";
                item->user_data = data;
            } else if (i == 1) {
                Container *item = content->child(::hbox, FILL_SPACE, 32 * config->dpi);
                item->when_paint = paint_title;
                auto *data = new TitleData;
                data->text = "Other results";
                item->user_data = data;
            }
            
            bool top_item = i == 0;
            
            Container *hbox = content->child(::hbox, FILL_SPACE, top_item ? 64 * config->dpi : 36 * config->dpi);
            hbox->when_paint = paint_hbox;
            Container *item = hbox->child(FILL_SPACE, FILL_SPACE);
            if (i == active_item) {
                auto *right_data = new SearchItemData;
                right_data->sortable = sorted[i];
                right_data->sortable->full_path = sorted[i]->full_path;
                right_data->user_data = sorted[i];
                right_data->item_number = i;
                right_fg->user_data = right_data;
                
                Container *right_active_title = right_fg->child(FILL_SPACE, 176 * config->dpi);
                right_active_title->when_paint = paint_right_active_title;
                right_active_title->when_clicked = clicked_right_active_title;
                
                auto *spacer = right_fg->child(FILL_SPACE, 2 * config->dpi);
                spacer->when_paint = paint_spacer;
                
                right_fg->child(FILL_SPACE, 12 * config->dpi);
                
                Container *open = right_fg->child(FILL_SPACE, 32 * config->dpi);
                open->when_paint = paint_open;
                open->when_clicked = clicked_open;

                Container *open_in_folder = right_fg->child(FILL_SPACE, 32 * config->dpi);
                open_in_folder->when_paint = paint_open_in_folder;
                open_in_folder->when_clicked = clicked_open_in_folder;
                
                bool in_apps_tab = active_tab == "Apps";
                if (winbar_settings->allow_live_tiles && in_apps_tab) {
                    Container *add_or_remove_from_live_ties = right_fg->child(FILL_SPACE, 32 * config->dpi);
                    add_or_remove_from_live_ties->when_paint = paint_live_tile_button;
                    add_or_remove_from_live_ties->when_clicked = clicked_live_tiles;
                }
                
                right_fg->child(FILL_SPACE, 12 * config->dpi);
            } else {
                Container *right_item = hbox->child(49 * config->dpi, FILL_SPACE);
                right_item->when_paint = paint_right_item;
                right_item->when_clicked = clicked_right_item;
            }
            
            item->when_paint = top_item ? paint_top_item : paint_item;
            item->when_clicked = clicked_item;
            auto *data = new SearchItemData;
            data->sortable = sorted[i];
            data->sortable->full_path = sorted[i]->full_path;
            data->user_data = sorted[i];
            data->item_number = i;
            hbox->user_data = data;
        }
    }
}

static void
execute_active_menu_item(AppClient *client) {
    if (auto container = container_by_name("right_fg", client->root)) {
        for (int i = 0; i < container->children.size(); ++i) {
            if (i - 3 == active_menu_item) {
                auto c = container->children[i];
                if (c->when_clicked) {
                    c->when_clicked(client, client->cr, c);
                }
                request_refresh(app, client);
                return;
            }
        }
    }
}

static void
when_key_event(AppClient *client,
               cairo_t *cr,
               Container *container,
               bool is_string, xkb_keysym_t keysym, char string[64],
               uint16_t mods,
               xkb_key_direction direction) {
    if (direction == XKB_KEY_UP) {
        return;
    }
    auto *search_menu_client = client_by_name(app, "search_menu");
    auto *taskbar_client = client_by_name(app, "taskbar");
    if (!search_menu_client || !taskbar_client) {
        return;
    }
    
    if (!is_string) {
        if (keysym == XKB_KEY_Left) {
            on_menu_items = false;
            active_menu_item = 0;
            return;
        } else if (keysym == XKB_KEY_Right) {
            on_menu_items = true;
            active_menu_item = 0;
            return;
        } else if (keysym == XKB_KEY_Up) {
            if (on_menu_items) {
                active_menu_item = std::max(0, active_menu_item - 1);
                return;
            }
            active_item--;
            if (active_item < 0)
                active_item = 0;
            // TODO set correct scroll_amount
            if (Container *container = container_by_name("content", search_menu_client->root)) {
                for (int i = 0; i < container->children.size(); i++) {
                    if (i == 0 || i == 2) // Skipping non items
                        continue;
                    Container *child = container->children[i];
                    SearchItemData *data = (SearchItemData *) child->user_data;
                    if (data->item_number == active_item) {
                        if (child->children.size() == 2)
                            child->children[1]->when_clicked(client, cr, child->children[1]);
                        auto scroll_pane = container_by_name("content", search_menu_client->root)->parent;
                        int offset = -scroll_pane->scroll_v_real;
                        if (child->real_bounds.y - child->real_bounds.h < scroll_pane->real_bounds.y)
                            offset -= (scroll_pane->real_bounds.y - child->real_bounds.y) + child->real_bounds.h;
                        if (active_item == 0)
                            offset = 0;
                        scroll_pane->scroll_v_real = -offset;
                        scroll_pane->scroll_v_visual = scroll_pane->scroll_v_real;
                    }
                }
            }
            client_layout(app, search_menu_client);
            request_refresh(app, search_menu_client);
            return;
        } else if (keysym == XKB_KEY_Down) {
            if (on_menu_items) {
                active_menu_item = std::min((int) winbar_settings->allow_live_tiles + (active_tab == "Apps" ? 1 : 0), active_menu_item + 1);
                return;
            }
            active_item++;
            if (max_items == 0)
                active_item = 0;
            if (active_item >= max_items)
                active_item = max_items;
            // TODO set correct scroll_amount
            if (Container *container = container_by_name("content", search_menu_client->root)) {
                for (int i = 0; i < container->children.size(); i++) {
                    if (i == 0 || i == 2) // Skipping non items
                        continue;
                    Container *child = container->children[i];
                    SearchItemData *data = (SearchItemData *) child->user_data;
                    if (data->item_number == active_item) {
                        if (child->children.size() == 2)
                            child->children[1]->when_clicked(client, cr, child->children[1]);
                        auto scroll_pane = container_by_name("content", search_menu_client->root)->parent;
                        int offset = -scroll_pane->scroll_v_real;
                        if (child->real_bounds.y + child->real_bounds.h > scroll_pane->real_bounds.y + scroll_pane->real_bounds.h)
                            offset += child->real_bounds.h;
                        scroll_pane->scroll_v_real = -offset;
                        scroll_pane->scroll_v_visual = scroll_pane->scroll_v_real;
                    }
                }
            }
            
            client_layout(app, search_menu_client);
            request_refresh(app, search_menu_client);
            return;
        } else if (keysym == XKB_KEY_Escape) {
            client_close(app, search_menu_client);
            set_textarea_inactive();
            return;
        } else if (keysym == XKB_KEY_Tab) {
            if (on_menu_items) {
                execute_active_menu_item(search_menu_client);
                return;
            }
            active_tab = active_tab == "Apps" ? "Scripts" : "Apps";
            update_options();
            return;
        } else if (keysym == XKB_KEY_Return) {
            if (on_menu_items) {
                execute_active_menu_item(search_menu_client);
                return;
            }
            // launch active item
            launch_active_item();
            client_layout(app, search_menu_client);
            request_refresh(app, search_menu_client);
            return;
        }
    } else {
        on_menu_items = false;
        active_menu_item = 0;
        active_item = 0;
        scroll_amount = 0;
    }
    
    if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
        textarea_handle_keypress(client, textarea, is_string, keysym, string, mods, direction);
        request_refresh(app, taskbar_client);
        update_options();
    }
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->type = ::vbox;
    root->when_key_event = when_key_event;
    
    auto *top = root->child(FILL_SPACE, 51 * config->dpi);
    top->type = ::hbox;
    top->when_paint = paint_top;
    top->spacing = 2 * config->dpi;
    top->wanted_pad.x = 12 * config->dpi;
    top->wanted_pad.w = 12 * config->dpi;
    top->name = "tab_group";
    
    add_tab(client, top, "Apps");
    add_tab(client, top, "Scripts");
    
    auto *splitter = root->child(FILL_SPACE, 1 * config->dpi);
    splitter->when_paint = paint_top_splitter;
    
    auto *bottom = root->child(FILL_SPACE, FILL_SPACE);
    bottom->name = "bottom";
    bottom->when_paint = paint_bottom;
    
    script_16 = accelerated_surface(client->app, client, 16 * config->dpi, 16 * config->dpi);
    paint_surface_with_image(script_16, as_resource_path("script-16.svg"), 16 * config->dpi, nullptr);
    script_32 = accelerated_surface(client->app, client, 32 * config->dpi, 32 * config->dpi);
    paint_surface_with_image(script_32, as_resource_path("script-32.svg"), 32 * config->dpi, nullptr);
    script_64 = accelerated_surface(client->app, client, 64 * config->dpi, 64 * config->dpi);
    paint_surface_with_image(script_64, as_resource_path("script-64.svg"), 64 * config->dpi, nullptr);
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
            global->history_scripts.push_back(h);
            if (global->history_scripts.size() > 100) {
                break;
            }
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
            global->history_apps.push_back(h);
            if (global->history_apps.size() > 100) {
                break;
            }
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
    for (HistoricalNameUsed *h: global->history_scripts) {
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
    for (HistoricalNameUsed *h: global->history_apps) {
        myfile << h->text + "\n";
    }
    myfile.close();
}

static void
search_menu_when_closed(AppClient *client) {
    if (auto c = container_by_name("tab_group", client->root)) {
        for (auto d: c->children) {
            // TODO: delete here address sanitizer problem
//            delete ((TabData *) (d->user_data));
        }
        c->children.clear();
//        printf("here\n");
    }
    // delete animation
    cairo_surface_destroy(script_16);
    cairo_surface_destroy(script_32);
    cairo_surface_destroy(script_64);
    write_historic_scripts();
    write_historic_apps();
    set_textarea_inactive();
    save_settings_file();
}

void start_search_menu() {
    load_scripts();
    Settings settings;
    settings.decorations = false;
    settings.skip_taskbar = true;
    settings.force_position = true;
    settings.w = 762 * config->dpi;
    settings.h = 641 * config->dpi;
    int width = 48 * config->dpi;
    settings.x = app->bounds.x + width;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto field_search = container_by_name("field_search", taskbar->root);
        auto super = container_by_name("super", taskbar->root);
        if (field_search->exists) {
            settings.x = taskbar->bounds->x + field_search->real_bounds.x;
        } else if (super->exists) {
            settings.x = taskbar->bounds->x + super->real_bounds.x;
        } else {
            settings.x = taskbar->bounds->x;
        }
        // Make sure doesn't go off-screen right side
        if (settings.x + settings.w > taskbar->screen_information->width_in_pixels) {
            settings.x = taskbar->screen_information->width_in_pixels - settings.w;
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.override_redirect = true;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "search_menu";
        popup_settings.takes_input_focus = true;
        auto client = taskbar->create_popup(popup_settings, settings);

        can_pop = true;
        client->when_closed = search_menu_when_closed;
        client->limit_fps = false;
        fill_root(client);
        client_show(app, client);
        set_textarea_active();
        xcb_set_input_focus(app->connection, XCB_NONE, client->window, XCB_CURRENT_TIME);
        xcb_flush(app->connection);
        xcb_aux_sync(app->connection);
    }
}

#include <dirent.h>
#include <sstream>

void load_scripts() {
    static std::atomic<bool> already_working = false;
    if (already_working)
        return;
    already_working = true;

    std::thread t([]() {
        defer(already_working = false);

        static std::vector<Script *> temp_scripts;
        temp_scripts.clear();

        // go through every directory in $PATH environment variable
        // add to our scripts list if the files we check are executable
        std::string paths = std::string(getenv("PATH"));

        std::replace(paths.begin(), paths.end(), ':', ' ');

        std::stringstream ss(paths);
        std::string string_path;
        while (ss >> string_path) {
            if (auto *dir = opendir(string_path.c_str())) {
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
                            for (auto *script: temp_scripts) {
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

                            script->full_path = path;
                            script->full_path += "/" + name;
                            script->path = path;
                            if (!script->path.empty()) {
                                if (script->path[script->path.length() - 1] == '/' ||
                                    script->path[script->path.length() - 1] == '\\') {
                                    script->path.erase(script->path.begin() + (script->path.length() - 1));
                                }
                            }

                            temp_scripts.push_back(script);
                        }
                    }
                }
                closedir(dir);
            }
        }

        if (temp_scripts.empty())
            return;

        std::lock_guard mtx(app->running_mutex);
        for (auto sc: scripts) {
            delete sc;
        }
        scripts.clear();
        scripts.shrink_to_fit();

        for (auto sc: temp_scripts) {
            scripts.push_back(sc);
        }
        
        update_options();
    });
    t.detach();
}

bool script_exists(const std::string &name) {
    for (auto s: scripts)
        if (s->name == name)
            return true;
    return false;
}


