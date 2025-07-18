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
#include "icons.h"
#include <sstream>
#include <iomanip>

#include "picomath.hpp"
using namespace picomath;

#include <algorithm>
#include <fstream>
#include <pango/pangocairo.h>
#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

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
static gl_surface *gsurf16 = nullptr;
static gl_surface *gsurf32 = nullptr;
static gl_surface *gsurf64 = nullptr;

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

static void paint_left_bg(AppClient *client, cairo_t *cr, Container *container);

static void paint_right_bg(AppClient *client, cairo_t *cr, Container *container);

static void paint_right_fg(AppClient *client, cairo_t *cr, Container *container);

static void paint_content(AppClient *client, cairo_t *cr, Container *container);

static void paint_hbox(AppClient *client, cairo_t *cr, Container *container);

static void
paint_top(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_search_tab_bar_background), container->real_bounds);
}

static void
paint_top_splitter(AppClient *client, cairo_t *cr, Container *container) {
    ArgbColor color = correct_opaqueness(client, config->color_search_tab_bar_background);
    darken(&color, 7);
    set_argb(cr, color);
    draw_colored_rect(client, color, container->real_bounds);
}

static void
paint_left_bg(AppClient *client, cairo_t *cr, Container *container) {
    draw_operator(client, CAIRO_OPERATOR_SOURCE);
    draw_colored_rect(client, correct_opaqueness(client, config->color_search_content_left_background), container->real_bounds);
    draw_operator(client, CAIRO_OPERATOR_OVER);
}

static void
paint_item_background(AppClient *client, cairo_t *cr, Container *container, int other_index) {
    auto *data = (SearchItemData *) container->parent->user_data;
    if (data->item_number == active_item) {
        if (on_menu_items) {
            draw_colored_rect(client, config->color_search_content_left_button_hovered, container->real_bounds);
        } else {
            draw_colored_rect(client, config->color_search_content_left_button_active, container->real_bounds);
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
    
    auto color =  config->color_search_content_left_button_default;
    if (something->state.mouse_pressing || something->state.mouse_hovering) {
        if (something->state.mouse_pressing) {
            if (use_other_index) {
                color = config->color_search_content_left_set_active_button_pressed;
            } else {
                color = config->color_search_content_left_button_pressed;
            }
        } else {
            if (use_other_index) {
                color = config->color_search_content_left_set_active_button_hovered;
            } else {
                color = config->color_search_content_left_button_hovered;
            }
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
    
    if (other_index == 0 && (something->state.mouse_pressing || something->state.mouse_hovering)) {
        draw_colored_rect(client, config->color_search_content_left_button_splitter, Bounds(container->real_bounds.x, container->real_bounds.y, 1, container->real_bounds.h));
        
    }
}

static void
paint_right_item(AppClient *client, cairo_t *cr, Container *container) {
    paint_item_background(client, cr, container, 0);
        
    auto color = config->color_search_content_left_set_active_button_icon_default;
    if (container->state.mouse_pressing)
        color = config->color_search_content_left_set_active_button_icon_pressed;
    draw_text(client, 10 * config->dpi, config->icons, EXPAND(color), "\uE974", container->real_bounds);
}

static void
paint_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    auto *data = (SearchItemData *) container->parent->user_data;
    
    draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), data->sortable->name, container->real_bounds, 5, 40 * config->dpi);
    
    if (active_tab == "Scripts") {
        if (script_16) {
            if (!gsurf16)
                gsurf16 = new gl_surface;
            draw_gl_texture(client, gsurf16, script_16, container->real_bounds.x + (12 * config->dpi), container->real_bounds.y + container->real_bounds.h / 2 - (8 * config->dpi));
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_16__) {
            draw_gl_texture(client, l_data->g16,
                                    l_data->icon_16__,
                                    container->real_bounds.x + 12 * config->dpi,
                                    container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi);
        } else {
            draw_gl_texture(client, global->u16,
                                    global->unknown_icon_16,
                                    container->real_bounds.x + 12 * config->dpi,
                                    container->real_bounds.y + container->real_bounds.h / 2 - 8 * config->dpi);
        }
    }
}

static void
paint_top_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    
    auto *data = (SearchItemData *) container->parent->user_data;
   
    draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), data->sortable->name, container->real_bounds, 5, 56 * config->dpi, (int) (13 * config->dpi));
    
    auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(config->color_search_content_text_secondary), active_tab);
    f->draw_text_end((int) (container->real_bounds.x + 56 * config->dpi),
                     (int) (container->real_bounds.y + container->real_bounds.h - 13 * config->dpi - h));
    if (active_tab == "Scripts") {
        if (script_32) {
            if (!gsurf32)
                gsurf32 = new gl_surface;
            draw_gl_texture(client, gsurf32, script_32, container->real_bounds.x + 12 * config->dpi,
                                container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_32__) {
            draw_gl_texture(client, l_data->g32,
                                    l_data->icon_32__,
                                    container->real_bounds.x + 12 * config->dpi,
                                    container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        } else {
            draw_gl_texture(client, global->u32,
                                    global->unknown_icon_32,
                                    container->real_bounds.x + 12 * config->dpi,
                                    container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        }
    }
}

static void
paint_generic_item(AppClient *client, cairo_t *cr, Container *container, std::string subtitle_text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_item_background(client, cr, container, 1);
    
    auto *data = (SearchItemData *) container->parent->user_data;
    
    int text_off = 56 * config->dpi;
    draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary),
              data->sortable->name, container->real_bounds, 5, text_off, (int) (13 * config->dpi));
    
    {
        auto [f, w, h] = draw_text_begin(client, 9 * config->dpi, config->font,
                                         EXPAND(config->color_search_content_text_secondary), subtitle_text);
        f->draw_text_end((int) (container->real_bounds.x + text_off),
                         (int) (container->real_bounds.y + container->real_bounds.h - 13 * config->dpi - h));
    }
    
    if (!starts_with(subtitle_text, "=")) {
        cairo_set_source_surface(cr,
                                 script_32,
                                 container->real_bounds.x + 12 * config->dpi,
                                 container->real_bounds.y + container->real_bounds.h / 2 - 16 * config->dpi);
        cairo_paint(cr);
    }
}

static void
paint_no_result_item(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    paint_generic_item(client, cr, container, "Run command anyways");
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (TitleData *) container->user_data;
    
    auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), data->text);
    f->draw_text_end((int) (container->real_bounds.x + 13 * config->dpi),
                     (int) (container->real_bounds.y + container->real_bounds.h / 2 - h / 2));
}

static void
paint_right_active_title(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (SearchItemData *) container->parent->user_data;
   
    auto [f, w, h] = draw_text_begin(client, 13 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), data->sortable->name);
    int x = (int) (container->real_bounds.x + container->real_bounds.w / 2 - w / 2);
    if (x < container->real_bounds.x)
        x = container->real_bounds.x;
    f->draw_text_end(x, (int) (container->real_bounds.y + 106 * config->dpi - h / 2));
    
    auto ff = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(config->color_search_content_text_secondary), active_tab);
    ff.f->draw_text_end((int) (container->real_bounds.x + container->real_bounds.w / 2 - ff.w / 2),
                        (int) (container->real_bounds.y + 106 * config->dpi + ff.h - (ff.h / 3)));
    
    if (active_tab == "Scripts") {
        if (script_64) {
            if (!gsurf64)
                gsurf64 = new gl_surface;
            draw_gl_texture(client, gsurf64, script_64, container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi, container->real_bounds.y + 21 * config->dpi);
        }
    } else if (active_tab == "Apps") {
        auto *l_data = (Launcher *) data->user_data;
        if (l_data->icon_64__) {
            draw_gl_texture(client, l_data->g64,
                                     l_data->icon_64__,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                     container->real_bounds.y + 21 * config->dpi);
        } else {
            draw_gl_texture(client, global->u64,
                                     global->unknown_icon_64,
                                     container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi,
                                     container->real_bounds.y + 21 * config->dpi);
        }
    }
}

static void
paint_right_active_title_for_no_results(AppClient *client, cairo_t *cr, Container *container) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *data = (SearchItemData *) container->parent->user_data;
    
    auto [f, w, h] = draw_text_begin(client, 13 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), data->sortable->name);
    int x = (int) (container->real_bounds.x + container->real_bounds.w / 2 - w / 2);
    if (x < container->real_bounds.x)
        x = container->real_bounds.x;
    f->draw_text_end(x, (int) (container->real_bounds.y + 106 * config->dpi - h / 2));
    
    std::string subtitle_text = "Run command anyways";
    auto ff = draw_text_begin(client, 9 * config->dpi, config->font, EXPAND(config->color_search_content_text_secondary), subtitle_text);
    ff.f->draw_text_end((int) (container->real_bounds.x + container->real_bounds.w / 2 - ff.w / 2),
                        (int) (container->real_bounds.y + 106 * config->dpi + ff.h - (ff.h / 3)));
    
    if (script_64) {
        if (!gsurf64)
            gsurf64 = new gl_surface;
        draw_gl_texture(client, gsurf64, script_64, container->real_bounds.x + container->real_bounds.w / 2 - 32 * config->dpi, container->real_bounds.y + 21 * config->dpi);
    }
}

static void
paint_spacer(AppClient *client, cairo_t *cr, Container *container) {
    Bounds b = container->real_bounds;
    b.x += 6 * config->dpi;
    b.w -= (6 * 2) * config->dpi;
    draw_colored_rect(client, config->color_search_content_right_splitter, container->real_bounds);
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
    
    auto color = config->color_search_content_right_button_default;
    if (container->state.mouse_pressing || container->state.mouse_hovering ||
        (on_menu_items && index == active_menu_item)) {
        if (on_menu_items && index == active_menu_item) {
            color = config->color_search_content_left_button_active;
        } else if (container->state.mouse_pressing) {
            color = config->color_search_content_right_button_pressed;
        } else {
            color = config->color_search_content_right_button_hovered;
        }
    }
    draw_colored_rect(client, color, container->real_bounds);
    
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_search_content_text_primary), text, container->real_bounds, 5, 52 * config->dpi);
    
    draw_text(client, 12 * config->dpi, config->icons, EXPAND(config->color_search_accent), icon, container->real_bounds, 5, 23 * config->dpi, container->real_bounds.h / 2 - ((16 / 2) * config->dpi));
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
            if (item->get_pinned()) {
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

// cleans up trailing zeros.
std::string clean_double(double val) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << val;
  std::string result = oss.str();

  result.erase(result.find_last_not_of('0') + 1);
  if (result.back() == '.') {
    result.pop_back();
  }
  return result;
}

// evalutes inputted equation 
std::string evaluate_math(std::string input) {
  PicoMath pm;

  auto result = pm.evalExpression(input.c_str());
  if (result.isOk()) {
      double r = result.getResult();
      return clean_double(r);
  }else{
    // error.
    return "";
  }
}

static void
paint_calc_result(AppClient *client, cairo_t *cr, Container *container) {
  auto data = (Label*) container->user_data;
  draw_colored_rect(client, ArgbColor(config->color_search_content_left_background), container->real_bounds); 
  draw_text(client, 10 * config->dpi, config->font, EXPAND(ArgbColor(config->color_search_content_text_primary)), data->text, container->real_bounds);

}
bool contains_operator(const std::string& input) {
    if (input.find("pow") != std::string::npos) {
        return true;
    }
    for (char ch : input) {
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^') {
            return true;
        }
    }
    return false;
}

void fill_calc_root(AppClient *client, Container *bottom, std::string text_input) {
    Container *hbox = bottom->child(::hbox, FILL_SPACE, FILL_SPACE);
    
    Container *left = hbox->child(::vbox, 344 * config->dpi, FILL_SPACE);
    left->when_paint = paint_left_bg;
    Container *right = hbox->child(::vbox, FILL_SPACE, FILL_SPACE);
    right->when_paint = paint_right_bg;
    right->wanted_pad = Bounds(12 * config->dpi, 12 * config->dpi, 12 * config->dpi, 0);
    
    Container *right_fg = right->child(::vbox, FILL_SPACE, FILL_SPACE);
    right_fg->when_paint = paint_right_fg;
    right_fg->name = "right_fg";
    
    // Setup the title on the left side
    Container *title = left->child(::hbox, FILL_SPACE, 32 * config->dpi);
    title->when_paint = paint_title;
    auto *title_data = new TitleData;
    title_data->text = "Math result";
    title->user_data = title_data;
    
    {
        Container *hbox = left->child(::hbox, FILL_SPACE, 64 * config->dpi);
        hbox->when_paint = paint_hbox;
        auto *data = new SearchItemData;
        auto *sortable_data = new Script;
        sortable_data->name = evaluate_math(text_input);
        data->sortable = sortable_data;
        data->user_data = sortable_data;
        data->delete_user_data_as_script = true; // aka delete 'new Script'
        hbox->user_data = data;
        
        // Setup main left container
        Container *main_item_on_left = hbox->child(FILL_SPACE, FILL_SPACE);
        auto left_ic = new IconButton();
        icon(client, &left_ic->surface__, "accessories-calculator", 24 * config->dpi);
        set_data<IconButton>(client, main_item_on_left, left_ic);
        main_item_on_left->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            paint_generic_item(client, cr, c, "=" + data->text);
            
            if (auto ic = get<IconButton>(client, c)) {
                if (ic->gsurf && ic->surface__) {
                    draw_gl_texture(client, ic->gsurf,
                                    ic->surface__,
                                    c->real_bounds.x + 12 * config->dpi,
                                    c->real_bounds.y + c->real_bounds.h / 2 - 16 * config->dpi);
                }
            }
        };
        main_item_on_left->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            clipboard_set(app, evaluate_math(data->text));
            client_close_threaded(app, client);
        };
        main_item_on_left->user_data = new Label(text_input);
        
        // Setup main right container
        Container *right_active_title = right_fg->child(FILL_SPACE, 176 * config->dpi);
        right_active_title->user_data = new Label(text_input);
        right_active_title->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            clipboard_set(app, evaluate_math(data->text));
            client_close_threaded(app, client);
        };
        auto ic = new IconButton();
        icon(client, &ic->surface__, "accessories-calculator", 64 * config->dpi);
        set_data<IconButton>(client, right_active_title, ic);
        
        right_active_title->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            // Icon
            if (auto ic = get<IconButton>(client, c)) {
                if (ic->gsurf && ic->surface__) {
                    draw_gl_texture(client, ic->gsurf,
                                    ic->surface__,
                                    c->real_bounds.x + c->real_bounds.w / 2 - 32 * config->dpi,
                                    c->real_bounds.y + 21 * config->dpi);
                }
            }
            
            // Result
            auto [f, w, h] = draw_text_begin(client, 17 * config->dpi, config->font,
                                             EXPAND(config->color_search_content_text_primary),
                                             evaluate_math(data->text));
            int x = (int) (c->real_bounds.x + c->real_bounds.w / 2 - w / 2);
            if (x < c->real_bounds.x)
                x = c->real_bounds.x;
            f->draw_text_end(x, (int) (c->real_bounds.y + 106 * config->dpi - h / 2));
            
            // Equation subtitle
            std::string subtitle_text = data->text;
            auto ff = draw_text_begin(client, 9 * config->dpi, config->font,
                                      EXPAND(config->color_search_content_text_secondary), "=" + subtitle_text);
            ff.f->draw_text_end((int) (c->real_bounds.x + c->real_bounds.w / 2 - ff.w / 2),
                                (int) (c->real_bounds.y + 116 * config->dpi + ff.h - (ff.h / 3)));
        };
        
        auto *spacer = right_fg->child(FILL_SPACE, 2 * config->dpi);
        spacer->when_paint = paint_spacer;
        
        right_fg->child(FILL_SPACE, 12 * config->dpi);
        
        Container *copy = right_fg->child(FILL_SPACE, 32 * config->dpi);
        copy->when_paint = [](AppClient *client, cairo_t *cr, Container *c) {
            paint_sub_option(client, cr, c, "Copy", "\uE8C8");
        };
        copy->when_clicked = [](AppClient *client, cairo_t *cr, Container *c) {
            auto data = (Label *) c->user_data;
            clipboard_set(app, evaluate_math(data->text));
            client_close_threaded(app, client);
        };
        copy->user_data = new Label(text_input);
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
                        
                        if (!evaluate_math(data->state->text).empty() && contains_operator(data->state->text)) {
                            fill_calc_root(search_menu_client, bottom, data->state->text);
                        } else if (active_tab == "Scripts") {
                            sort_and_add<Script *>(&scripts, bottom, data->state->text, global->history_scripts);
                        } else if (active_tab == "Apps") {
                            // We create a copy because app_menu relies on the order
                            std::vector<Launcher *> launchers_copy;
                            for (auto l: launchers) {
                                launchers_copy.push_back(l);
                            }
                            sort_and_add<Launcher *>(&launchers_copy, bottom, data->state->text,
                                                     global->history_apps);
                        }
                    }
                    client_layout(app, search_menu_client);
                    request_refresh(app, search_menu_client);
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
            app_timeout_stop(app, client, data->state->cursor_blink);
            delete data->state;
            data->state = new TextState;
            blink_on(app, client, container);
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
            item->set_pinned(!item->get_pinned());
            if (item->get_pinned())
                item->info.w = 2;
                item->info.h = 2;
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
    draw_colored_rect(client, correct_opaqueness(client, config->color_search_content_right_background), container->real_bounds);
}

static void
paint_right_fg(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_search_content_right_foreground), container->real_bounds);
}

static void
paint_bottom(AppClient *client, cairo_t *cr, Container *container) {
    draw_operator(client, CAIRO_OPERATOR_SOURCE);
    draw_colored_rect(client, correct_opaqueness(client, config->color_search_empty_tab_content_background), container->real_bounds);
    draw_operator(client, CAIRO_OPERATOR_OVER);
    
    if (!container->children.empty()) {
        return;
    }
    
    std::string min = active_tab;
    min[0] = std::tolower(min[0]);
    std::string text = "Start typing to search for " + min;
    
    auto [f, w, h] = draw_text_begin(client, 20 * config->dpi, config->font, EXPAND(config->color_search_empty_tab_content_text), text);
    f->draw_text_end(container->real_bounds.x + container->real_bounds.w / 2 - w / 2,
                     container->real_bounds.y + container->real_bounds.h - 256 * config->dpi - h / 2);
    
    if (auto *tab_group = container_by_name("tab_group", client->root)) {
        for (auto *tab: tab_group->children) {
            auto *tab_data = (TabData *) tab->user_data;
            if (tab_data->name == active_tab) {
                std::string text = "\uE62F"; // Scripts
                if (tab_data->name == "Apps")
                    text = "\uF8A5";
                auto ff = draw_text_begin(client, 100 * config->dpi, config->icons, EXPAND(config->color_search_empty_tab_content_icon), text);
                ff.f->draw_text_end((int) (container->real_bounds.x + container->real_bounds.w / 2 - ff.w / 2),
                                    (int) (container->real_bounds.y + container->real_bounds.h / 2 - ff.h / 2 - 100 * config->dpi));
            }
        }
    }
}

static void
paint_tab(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (TabData *) container->user_data;
    
    auto color = config->color_search_tab_bar_default_text;
    if (data->name == active_tab) {
        color = config->color_search_tab_bar_active_text;
    } else if (container->state.mouse_pressing) {
        color = config->color_search_tab_bar_pressed_text;
    } else if (container->state.mouse_hovering) {
        color = config->color_search_tab_bar_hovered_text;
    }
    
    auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(color), data->name, true);
    f->draw_text_end(MIDX(container) - w / 2, MIDY(container) - h / 2);
    
    if (data->name == active_tab) {
        int height = 4 * config->dpi;
        draw_colored_rect(client, config->color_search_accent, Bounds(container->real_bounds.x, container->real_bounds.y + container->real_bounds.h - height, container->real_bounds.w, height));
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
    for (int i = 0; i < sortables->size(); i++) {
        Sortable *s = (*sortables)[i];
        s->priority = 0;
        s->match_level = 100;
        int out = 0;
        if (fts::fuzzy_match(text.c_str(), s->name.c_str(), out)) {
            s->match_level = 0;
            goto success;
        }
        if (!s->generic_name.empty()) {
            if (fts::fuzzy_match(text.c_str(), s->generic_name.c_str(), out)) {
                s->match_level = 0;
                goto success;
            }
        }
        for (auto k: s->keywords) {
            if (fts::fuzzy_match(text.c_str(), k.c_str(), out))
                goto success;
        }
        for (auto k: s->categories) {
            if (fts::fuzzy_match(text.c_str(), k.c_str(), out))
                goto success;
        }
        continue;
        success:
        s->priority = out;
        sorted.push_back((*sortables)[i]);
    }
    
    for (int i = 0; i < sorted.size(); i++) {
        Sortable *s = (sorted)[i];
        for (int i = 0; i < history.size(); i++) {
            HistoricalNameUsed *h = (history)[i];
            if (h->text == s->lowercase_name) {
                s->historical_ranking = i;
            }
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](Sortable *a, Sortable *b) {
                         if (a->match_level == b->match_level) {
                             if (a->historical_ranking != -1 || b->historical_ranking != -1) {
                                 if (a->historical_ranking != -1 && b->historical_ranking == -1) {
                                     return true;
                                 } else if (a->historical_ranking == -1 && b->historical_ranking != -1) {
                                     return false;
                                 } else {
                                     // TODO: have priority be able to outstrip ranking if its a MUCH better match
                                     //  'steam' is a problem because 'system settings' beats it
                                     return a->historical_ranking < b->historical_ranking;
                                 }
                             }
                             
                             return a->priority > b->priority;
                         } else {
                             return a->match_level < b->match_level;
                         }
                     });
    
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
            if (on_menu_items == false) {
                if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
                    textarea_handle_keypress(client, textarea, is_string, keysym, string, mods, direction);
                    request_refresh(app, taskbar_client);
                    update_options();
                }
                return;
            }
            on_menu_items = false;
            active_menu_item = 0;
            return;
        } else if (keysym == XKB_KEY_Right) {
            bool inside_text = false;
            if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
                auto data = (TextAreaData*) textarea->user_data;
                inside_text = data->state->text.size() > data->state->cursor;

                if (on_menu_items == false && inside_text) {
                    textarea_handle_keypress(client, textarea, is_string, keysym, string, mods, direction);
                    request_refresh(app, taskbar_client);
                    update_options();
                    return;
                }
            }
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
            if (auto *textarea = container_by_name("main_text_area", taskbar_client->root)) {
                auto data = (TextAreaData *) textarea->user_data;
                if (!evaluate_math(data->state->text).empty()) {
                    clipboard_set(app, evaluate_math(data->state->text));
                    client_close(app, search_menu_client);
                    set_textarea_inactive();
                    return;
                }
            }
            
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
    delete gsurf16;
    delete gsurf32;
    delete gsurf64;
    gsurf16 = nullptr;
    gsurf32 = nullptr;
    gsurf64 = nullptr;
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
    if (app->wayland)
        settings.override_redirect = false;
    
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

void actual_load(bool lock) {
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
    
    if (lock) {
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
    } else {
        for (auto sc: scripts) {
            delete sc;
        }
        scripts.clear();
        scripts.shrink_to_fit();
        
        for (auto sc: temp_scripts) {
            scripts.push_back(sc);
        }
        
        update_options();
    }
}

void load_scripts(bool do_now) {
    static std::atomic<bool> already_working = false;
    if (already_working)
        return;
    if (do_now) {
        actual_load(false);
        return;
    }
    already_working = true;

    std::thread t([]() {
        defer(already_working = false);
        actual_load(true);
    });
    t.detach();
}

bool script_exists(const std::string &name) {
    for (auto s: scripts)
        if (s->name == name)
            return true;
    return false;
}


