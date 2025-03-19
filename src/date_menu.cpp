
#include "date_menu.h"
#include "application.h"
#include "components.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"
#include "settings_menu.h"
#include "drawer.h"

#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <pango/pangocairo.h>
#include <sstream>//std::stringstream
#include <stdio.h>
#include <sys/stat.h>
#include <iomanip>

class weekday_title : public UserData {
public:
    std::string text;
};

class date_title : public UserData {
public:
    std::string text;
    
    int month = 0;
    int day = 0;
    int year = 0;
};

static int view_month = 0;
static int view_year = 0;

static int current_month = 0;
static int current_year = 0;
static int current_day = 0;

static int agenda_month = 0;
static int agenda_year = 0;
static int agenda_day = 0;

static int agenda_showing = true;

class UniqueTextState : public UserData {
public:
    TextState *state = new TextState;
    int day;
    int year;
    int month;
    bool show_every_year = false;
};

static std::vector<UniqueTextState *> unique_day_text_state;

// Takes month in the form 0 to 11
static int
GetDaysInMonth(int year, int month) {
    int The_months[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Days In Each Month;
    bool A_Leap_Year = ((year % 400 == 0) || (year % 4 == 0 && year % 100 != 0));
    
    if (A_Leap_Year)
        The_months[1] = 29;
    
    return The_months[month];
}

// d starts at 1, m is 0-11, y is what you'd expect
int day(int d, int m, int y) {
    /*The following, invented by Mike Keith and published in Journal of Recreational Mathematics,
     Vol. 22, No. 4, 1990, p. 280,
     is conjectured to be the shortest expression of a day-of-the-week algorithm: */
    // Sunday = 0, Monday = 1, Tuesday = 2, etc.
    
    return (d += m < 3 ? y-- : y - 2, 23 * m / 9 + d + 4 + y / 4 - y / 100 + y / 400) % 7;
}

static void
update_days(int month, int year) {
    view_month = month;
    view_year = year;
    // TODO we get the wrong month if we don't do this
    
    int previous_month_day_count;
    if (month == 0) {
        previous_month_day_count = GetDaysInMonth(year - 1, 11);
    } else {
        previous_month_day_count = GetDaysInMonth(year, month - 1);
    }
    int current_month_day_count = GetDaysInMonth(year, month);
    int weekday = day(1, month + 1, year);// 0 for sunday, 6 for sat
    int start_number = previous_month_day_count - weekday;
    
    int i = 0;
    if (auto *client = client_by_name(app, "date_menu")) {
        if (auto *dates_container = container_by_name("dates_container", client->root)) {
            for (auto *row_container: dates_container->children) {
                for (auto *date: row_container->children) {
                    auto *data = (date_title *) date->user_data;
                    
                    if (start_number < previous_month_day_count) {
                        data->text = std::to_string(start_number++);
                        if (month == 0) {
                            data->month = 11;
                            data->year = year - 1;
                        } else {
                            data->month = month - 1;
                            data->year = year;
                        }
                        data->day = std::stoi(data->text);
                    } else {
                        if (i < current_month_day_count) {
                            data->text = std::to_string(++i);
                            data->month = month;
                            data->year = year;
                            data->day = std::stoi(data->text);
                        } else {
                            ++i;
                            data->text = std::to_string(i - current_month_day_count);
                            if (month == 11) {
                                data->month = 0;
                                data->year = year + 1;
                            } else {
                                data->month = month + 1;
                                data->year = year;
                            }
                            data->day = std::stoi(data->text);
                        }
                    }
                }
            }
        }
    }
}

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    draw_colored_rect(client, correct_opaqueness(client, config->color_date_background), container->real_bounds); 
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
    time_t now = time(0);
    tm *ltm = localtime(&now);
    
    const char *months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"};
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%I:%M:%S");
    std::string top_text = ss.str();
    if (top_text.at(0) == '0') {
        top_text = top_text.erase(0, 1);
    }
        
    std::stringstream sl;
    sl << std::put_time(std::localtime(&now), "%p");
    std::string top_right_text = sl.str();
    
    const char *date_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};
    int day_index = day(ltm->tm_mday, ltm->tm_mon + 1, 1900 + ltm->tm_year);
    std::string bottom_text =
            std::string(date_names[day_index]) + " " + std::string(months[ltm->tm_mon]) + " " +
            std::to_string(ltm->tm_mday) + ", " + std::to_string(1900 + ltm->tm_year);
    
    auto top_color = config->color_date_text_title;
    auto [top_f, top_logical_w, top_logical_h] = draw_text_begin(client, 34 * config->dpi, config->font, EXPAND(top_color), top_text);
    int top_x_pos = container->real_bounds.x + (24 * config->dpi); //- (top_ink.x / PANGO_SCALE);
    int top_y_pos = container->real_bounds.y + (10 * config->dpi); //- (top_ink.y / PANGO_SCALE);
    top_f->draw_text_end(top_x_pos, top_y_pos);
 
    auto top_right_color = config->color_date_text_title_period;
    auto [top_right_f, top_right_logical_w, top_right_logical_h] = draw_text_begin(client, 14 * config->dpi, config->font, EXPAND(top_right_color), top_right_text);
    int top_right_x_pos = container->real_bounds.x + (24 * config->dpi) + top_logical_w + 10 * config->dpi; //- (top_right_ink.x / PANGO_SCALE);
    // TODO: last '-' should actually be the ascent or text ink
    int top_right_y_pos = container->real_bounds.y + (10 * config->dpi) + top_logical_h - top_right_logical_h - (5 * config->dpi); //- (top_right_ink.y / PANGO_SCALE);
    top_right_f->draw_text_end(top_right_x_pos, top_right_y_pos);
 
    auto bottom_color = config->color_date_text_title_info;
    auto [bottom_f, bottom_logical_w, bottom_logical_h] = draw_text_begin(client, 11 * config->dpi, config->font, EXPAND(bottom_color), bottom_text);
    int bottom_x_pos = container->real_bounds.x + (25 * config->dpi); //- (bottom_ink.x / PANGO_SCALE);
    int bottom_y_pos = container->real_bounds.y + (10 * config->dpi) + top_logical_h; //- (bottom_ink.y / PANGO_SCALE);
    bottom_f->draw_text_end(bottom_x_pos, bottom_y_pos);
}

static void
paint_body(AppClient *client, cairo_t *cr, Container *container) {
    auto b = Bounds(container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, 1);
    draw_colored_rect(client, config->color_date_seperator, b);
    
    if (agenda_showing) {
        draw_colored_rect(client, config->color_date_seperator, Bounds(container->real_bounds.x, container->real_bounds.y + container->real_bounds.h - 1, container->real_bounds.w, 1));
    }
}

static void
paint_centered_text(AppClient *client, cairo_t *cr, Container *container, std::string text) {
    /*
    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 9 * config->dpi, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(text_layout, text.c_str(), text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);
    
    cairo_move_to(cr,
                  container->real_bounds.x - (text_ink.x / PANGO_SCALE) +
                  container->real_bounds.w / 2 - (text_ink.width / PANGO_SCALE) / 2,
                  container->real_bounds.y - (text_ink.y / PANGO_SCALE) +
                  container->real_bounds.h / 2 - (text_ink.height / PANGO_SCALE) / 2);
    pango_cairo_show_layout(cr, text_layout);
    */
    draw_text(client, 9 * config->dpi, config->font, EXPAND(config->color_date_text_week_day), text, container->real_bounds);
}

static void
paint_weekday_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (weekday_title *) container->user_data;
    paint_centered_text(client, cr, container, data->text);
}

static void
clicked_date(AppClient *client, cairo_t *cr, Container *container);

static void
paint_events(AppClient *client, cairo_t *cr, Container *container) {
    if (!agenda_showing || !container->exists) {
        return;
    }
    
    const char *date_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};
    
    int day_index = 0;
    day_index = day(agenda_day, agenda_month + 1, agenda_year);
    std::string text = std::string(date_names[day_index]) + " " + std::to_string(agenda_day);
    
    auto [f, w, h] = draw_text_begin(client, 13 * config->dpi, config->font, EXPAND(config->color_date_weekday_monthday), text);
    f->draw_text_end(container->real_bounds.x + 24 * config->dpi, 
                     container->real_bounds.y + 21 * config->dpi);
    
        
    std::string shown_amount_str = "Shown every year";
    bool found = false;
    for (auto ud: unique_day_text_state) {
        bool year_okay = (ud->year == agenda_year) || ud->show_every_year;
        if (ud->day == agenda_day && ud->month == agenda_month && year_okay) { // active ud
            found = true;
            if (!ud->show_every_year) {
                shown_amount_str = "Only for specific day";
            }
            if (ud->state->text.empty()) {
                return; // early return
            }
        }
    }
    if (!found) {
        return; // early return;
    }
        
    auto color = config->color_date_text_default_button;
    auto [ff, ww, hh] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(color), shown_amount_str);
    auto text_bounds = Bounds(container->real_bounds.x + 24 * config->dpi + w + 8 * config->dpi, 
                              container->real_bounds.y + 21 * config->dpi, ww, h);
    bool hovering = bounds_contains(text_bounds, client->mouse_current_x, client->mouse_current_y);
    static int previous_state_static = 0;
    int previous_state = previous_state_static;
    if (hovering || (hovering && container->state.mouse_pressing)) {
        if (container->state.mouse_pressing) {
            color = config->color_date_text_pressed_button;
            previous_state_static = 2;
        } else {
            color = config->color_date_text_hovered_button;
            previous_state_static = 1;
        }
    }
    // Bug where if you show every year, but then go to a different year and you un-show every year, that one gets bugged the next time you show every year
    if (previous_state == 2 && previous_state_static == 1) { // clicked toggle
        request_refresh(app, client);
        // also remove agenda if not ud is active
        for (auto ud: unique_day_text_state) {
            bool year_okay = (ud->year == agenda_year) || ud->show_every_year;
            if (ud->day == agenda_day && ud->month == agenda_month && year_okay) { // active ud
                ud->show_every_year = !ud->show_every_year;
            }
        }
        
        // click on date container so that agenda box is 'fixed'
        if (auto dates = container_by_name("dates_container", client->root)) {
            for (int y = 0; y < 6; y++) {
                auto row = dates->children[y];
                for (int x = 0; x < 7; x++) {
                    auto date = row->children[x];
                    auto *data = (date_title *) date->user_data;
                    
                    if (agenda_day == data->day && agenda_month == data->month && agenda_year == data->year) { 
                        clicked_date(client, client->cr, date);
                    }
                }
            }
        }
    }
    
    ff->set_color(EXPAND(color));
   
    ff->draw_text_end(container->real_bounds.x + 24 * config->dpi + w + 8 * config->dpi, container->real_bounds.y + 21 * config->dpi + (h - hh) / 2);
}

static void
paint_agenda(AppClient *client, cairo_t *cr, Container *container) {
    std::string text = "Show agenda";
    if (agenda_showing)
        text = "Hide agenda";
    
    auto color = config->color_date_text_default_button;
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            color = config->color_date_text_pressed_button;
        else
            color = config->color_date_text_hovered_button;
    }
    auto [f, w, h] = draw_text_begin(client, 10 * config->dpi, config->font, EXPAND(color), text);
    int pos_x = container->real_bounds.x + container->real_bounds.w - w - 24;
    int pos_y = container->real_bounds.y + container->real_bounds.h / 2 - h / 2;
    f->draw_text_end(pos_x, pos_y);
 
    text = "\uE971";
    if (agenda_showing) {
        text = "\uE972";
    }
    auto ff = draw_text_begin(client, 6 * config->dpi, config->icons, EXPAND(color), text);
    ff.f->draw_text_end((int) ((pos_x + w + 5 * config->dpi)), 
                        (int) ((pos_y + ff.h / 3 + 4 * config->dpi)));
}

static void
paint_month_year_label(AppClient *client, cairo_t *cr, Container *container) {
    const char *months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"};
    
    std::string text = months[view_month];
    text += " " + std::to_string(view_year);
    draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_date_text_month_year), text, container->real_bounds, 5, 0, 0);
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (ButtonData *) container->user_data;
    
    auto color = config->color_date_default_arrow;
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            color = config->color_date_pressed_arrow;
        else
            color = config->color_date_hovered_arrow;
    }
    
    draw_text(client, 12 * config->dpi, config->icons, EXPAND(color), data->text, container->real_bounds);
}

static void
paint_textarea_parent(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        if (data->state->text.empty() && !container->active) {
            std::string text("Write the days events here");
                        
            draw_text(client, 11 * config->dpi, config->font, EXPAND(config->color_date_text_prompt), text, container->real_bounds);
        } else {
            draw_margins_rect(client, config->color_date_seperator, container->real_bounds, 1, -2);
        }
    }
}

static void
paint_date_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (date_title *) container->user_data;
    
    for (auto *ud: unique_day_text_state) {
        bool year_okay = (ud->year == data->year) || ud->show_every_year;
        if (ud->day == data->day && ud->month == data->month && year_okay && !ud->state->text.empty()) { 
            draw_margins_rect(client, config->color_date_cal_border, container->real_bounds, 1, -1);
        }
    }
    
    bool is_today =
            data->month == current_month && data->year == current_year && data->day == current_day;
    bool is_day_chosen =
            data->month == agenda_month && data->year == agenda_year && data->day == agenda_day;
    if (is_today) {
        draw_colored_rect(client, config->color_date_cal_background, container->real_bounds);
    }
    
    if (is_day_chosen) {
        draw_margins_rect(client, config->color_date_cal_background, container->real_bounds, 2, 0);
    }
    
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        ArgbColor color;
        if (is_today || is_day_chosen) {
            ArgbColor b = config->color_date_cal_background;
            lighten(&b, 5);
            if (container->state.mouse_pressing) {
                lighten(&b, 5);
                color = b;
            } else {
                color = config->color_date_cal_background;
            }
        } else {
            ArgbColor b = config->color_date_cal_border;
            lighten(&b, 5);
            if (container->state.mouse_pressing) {
                lighten(&b, 5);
                color = b;
            } else {
                color = config->color_date_cal_border;
            }
        }
        
        draw_margins_rect(client, color, container->real_bounds, 2, 0);
    }
    
    if (((container->state.mouse_hovering || container->state.mouse_pressing) || is_day_chosen) &&
        is_today) {
        draw_margins_rect(client, config->color_date_cal_foreground, container->real_bounds, 2, 2);
    }
    
    auto color = config->color_date_text_not_current_month;
    if (data->month == view_month)
        color = config->color_date_text_current_month;
    draw_text(client, 11 * config->dpi, config->font, EXPAND(color), data->text, container->real_bounds);
}

static void
clicked_up_arrow(AppClient *client, cairo_t *cr, Container *container) {
    view_month--;
    
    if (view_month < 0) {
        view_month = 11;
        view_year--;
    }
    
    update_days(view_month, view_year);
}

static void
clicked_down_arrow(AppClient *client, cairo_t *cr, Container *container) {
    view_month++;
    
    if (view_month >= 12) {
        view_month = 0;
        view_year++;
    }
    
    update_days(view_month, view_year);
}

static void
clicked_date(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (date_title *) container->user_data;
    agenda_year = data->year;
    agenda_month = data->month;
    agenda_day = data->day;
    
    UniqueTextState *unique_day = nullptr;
    // TODO: let's prefer show every year vs unique for now but in the future we have to combine.
    // because otherwise, when the date is clicked, it'll be empty, because it'll have created a ud that is going to be seen and shown before,
    // the every year text as it's more specific

    for (auto *ud: unique_day_text_state) {
        bool year_okay = ud->show_every_year;
        if (ud->day == agenda_day && year_okay && ud->month == agenda_month)
            unique_day = ud;
    }
    if (!unique_day) {
        for (auto *ud: unique_day_text_state) {
            bool year_okay = ud->year == data->year;
            if (ud->day == agenda_day && year_okay && ud->month == agenda_month)
                unique_day = ud;
        }
    }

    if (unique_day == nullptr) {
        unique_day = new UniqueTextState;
        unique_day->day = agenda_day;
        unique_day->year = agenda_year;
        unique_day->month = agenda_month;
        unique_day_text_state.push_back(unique_day);
    }
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        data->state = unique_day->state;
    }
    
    update_days(view_month, view_year);
}

static const char *
gravity_to_string(xcb_gravity_t gravity) {
    switch (gravity) {
        case XCB_GRAVITY_NORTH_WEST:
            return "NorthWest";
        case XCB_GRAVITY_NORTH:
            return "North";
        case XCB_GRAVITY_NORTH_EAST:
            return "NorthEast";
        case XCB_GRAVITY_WEST:
            return "West";
        case XCB_GRAVITY_CENTER:
            return "Center";
        case XCB_GRAVITY_EAST:
            return "East";
        case XCB_GRAVITY_SOUTH_WEST:
            return "SouthWest";
        case XCB_GRAVITY_SOUTH:
            return "South";
        case XCB_GRAVITY_SOUTH_EAST:
            return "SouthEast";
        case XCB_GRAVITY_STATIC:
            return "Static";
        default:
            fprintf(stderr, "Unknown gravity value %d", (int) gravity);
            exit(1);
    }
}

static void
clicked_agenda(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *client = client_by_name(app, "date_menu")) {
        if (auto *c = container_by_name("events", client->root)) {
            c->exists = !agenda_showing;
            if (agenda_showing) {
                c->wanted_pad = Bounds(0, 0, 0, 0);
            } else {
                c->wanted_pad = Bounds(24 * config->dpi, 58 * config->dpi, 24 * config->dpi, 0);
            }
        }
        
        uint32_t value_mask =
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
        
        uint32_t value_list_resize[4];
        if (agenda_showing) {
            value_list_resize[0] = client->bounds->x;
            value_list_resize[3] = (502 * config->dpi);
            value_list_resize[1] = app->bounds.h - config->taskbar_height - value_list_resize[3];
            value_list_resize[2] = client->bounds->w;
            xcb_configure_window(app->connection, client->window, value_mask, value_list_resize);
            handle_configure_notify(app, client, value_list_resize[0],
                                    value_list_resize[1],
                                    value_list_resize[2],
                                    value_list_resize[3]);
        } else {
            value_list_resize[0] = client->bounds->x;
            value_list_resize[3] = 735 * config->dpi;
            value_list_resize[1] = app->bounds.h - config->taskbar_height - value_list_resize[3];
            value_list_resize[2] = client->bounds->w;
            xcb_configure_window(app->connection, client->window, value_mask, value_list_resize);
            handle_configure_notify(app, client, value_list_resize[0],
                                    value_list_resize[1],
                                    value_list_resize[2],
                                    value_list_resize[3]);
        }
        xcb_flush(app->connection);
        agenda_showing = !agenda_showing;
        winbar_settings->show_agenda = agenda_showing;
    }
}

static void
clicked_clear_text(AppClient *client, cairo_t *cr, Container *container) {
    // TODO: go through agenda and clear that
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        delete data->state;
        data->state = new TextState;
        
        for (auto ud: unique_day_text_state) {
            if (ud->day == agenda_day && ud->month == agenda_month && ud->year == agenda_year) {
                ud->state = data->state;
            }
        }
    }
}

static void
paint_clear_text(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        if (data->state->text.empty()) {
            return;
        }
    }
    
    std::string text = "Clear text";
    auto color = config->color_date_text_default_button;
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            color = config->color_date_text_pressed_button;
        else
            color = config->color_date_text_hovered_button;
    }
    draw_text(client, 10 * config->dpi, config->font, EXPAND(color), text, container->real_bounds, 5, 0);
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->when_paint = paint_root;
    root->type = ::vbox;
    
    Container *title = root->child(FILL_SPACE, 111 * config->dpi);
    title->when_paint = paint_title;
    
    Container *body = root->child(FILL_SPACE, 343 * config->dpi);
    body->type = ::vbox;
    body->when_paint = paint_body;
    
    Container *up_down_hbox = body->child(FILL_SPACE, (18 + 18 + 16) * config->dpi);
    up_down_hbox->type = ::hbox;
    up_down_hbox->wanted_pad.x = 24 * config->dpi;
    up_down_hbox->wanted_pad.w = 28 * config->dpi;
    up_down_hbox->wanted_pad.y = 18 * config->dpi;
    up_down_hbox->wanted_pad.h = 18 * config->dpi;
    
    Container *month_year_label = up_down_hbox->child(66 * config->dpi, FILL_SPACE);
    month_year_label->when_paint = paint_month_year_label;
    
    // Pad
    up_down_hbox->child(FILL_SPACE, FILL_SPACE);
    
    Container *up_arrow = up_down_hbox->child(16 * config->dpi, FILL_SPACE);
    up_arrow->when_paint = paint_arrow;
    up_arrow->when_clicked = clicked_up_arrow;
    auto *up_data = new ButtonData;
    up_arrow->user_data = up_data;
    up_data->text = "\uE971";
    
    // Pad
    up_down_hbox->child(32 * config->dpi, FILL_SPACE);
    
    Container *down_arrow = up_down_hbox->child(16 * config->dpi, FILL_SPACE);
    down_arrow->when_paint = paint_arrow;
    down_arrow->when_clicked = clicked_down_arrow;
    auto *down_data = new ButtonData;
    down_arrow->user_data = down_data;
    down_data->text = "\uE972";
    
    Container *day_titles = body->child(FILL_SPACE, 27 * config->dpi);
    day_titles->type = ::hbox;
    day_titles->wanted_pad.x = 13 * config->dpi;
    day_titles->wanted_pad.w = 13 * config->dpi;
    day_titles->spacing = 2 * config->dpi;
    
    const char *names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; i++) {
        auto day_title = day_titles->child(FILL_SPACE, FILL_SPACE);
        day_title->spacing = 2 * config->dpi;
        day_title->when_paint = paint_weekday_title;
        auto *data = new weekday_title;
        data->text = names[i];
        day_title->user_data = data;
    }
    
    Container *dates = body->child(FILL_SPACE, FILL_SPACE);
    dates->type = ::vbox;
    dates->wanted_pad.x = 13 * config->dpi;
    dates->wanted_pad.w = 13 * config->dpi;
    dates->wanted_pad.y = 6 * config->dpi;
    dates->wanted_pad.h = 6 * config->dpi;
    dates->spacing = 2 * config->dpi;
    dates->name = "dates_container";
    
    int x = 0;
    for (int i = 0; i < 6; i++) {
        Container *temp_hbox = dates->child(FILL_SPACE, 40 * config->dpi);
        temp_hbox->type = ::hbox;
        temp_hbox->spacing = 2 * config->dpi;
        
        for (int i = 0; i < 7; i++) {
            x++;
            Container *date = temp_hbox->child(FILL_SPACE, FILL_SPACE);
            date->when_paint = paint_date_title;
            date->when_clicked = clicked_date;
            date->when_fine_scrolled = [](AppClient *client,
                          cairo_t *cr,
                          Container *container,
                          int scroll_x,
                          int scroll_y, bool came_from_touchpad) {
                if (scroll_y > 0) {
                    clicked_up_arrow(client, cr, container);
                } else if (scroll_y < 0) {
                    clicked_down_arrow(client, cr, container);
                }
            };
            auto *data = new date_title;
            data->text = std::to_string(x);
            date->user_data = data;
        }
    }
    time_t now = time(0);
    tm *ltm = localtime(&now);
    
    current_year = ltm->tm_year + 1900;
    current_month = ltm->tm_mon;
    current_day = ltm->tm_mday;
    agenda_year = ltm->tm_year + 1900;
    agenda_month = ltm->tm_mon;
    agenda_day = ltm->tm_mday;
    update_days(ltm->tm_mon, ltm->tm_year + 1900);
    
    Container *events = root->child(FILL_SPACE, FILL_SPACE);
    events->exists = agenda_showing;
    events->name = "events";
    events->wanted_pad = Bounds(24 * config->dpi, 58 * config->dpi, 24 * config->dpi, 0);
    events->when_paint = paint_events;
    
    TextAreaSettings settings(config->dpi);
    settings.color = config->color_date_text;
    settings.color_cursor = config->color_date_cursor;
    settings.font = config->font;
    settings.font_size__ = 11 * config->dpi;
    settings.wrap = true;
    settings.bottom_show_amount = 2;
    settings.right_show_amount = 2;
    Container *textarea = make_textarea(app, client, events, settings);
    TextAreaData *data = (TextAreaData *) textarea->user_data;
    
    textarea->name = "main_text_area";
    textarea->parent->when_paint = paint_textarea_parent;
    
    
    for (auto ud: unique_day_text_state) {
        if (ud->day == agenda_day && ud->month == agenda_month && ud->year == agenda_year) {
            delete data->state;
            data->state = ud->state;
            break;
        }
    }
    
    Container *agenda_hbox = root->child(::hbox, FILL_SPACE, 48 * config->dpi);
    agenda_hbox->wanted_pad.x = 20 * config->dpi;
    agenda_hbox->wanted_pad.w = 10 * config->dpi;
    Container *clear_text = agenda_hbox->child(FILL_SPACE, FILL_SPACE);
    clear_text->when_paint = paint_clear_text;
    clear_text->when_clicked = clicked_clear_text;
    
    Container *agenda = agenda_hbox->child(FILL_SPACE, FILL_SPACE);
    agenda->type = ::hbox;
    agenda->when_paint = paint_agenda;
    agenda->when_clicked = clicked_agenda;
}

static void
write_agenda_to_disk(AppClient *client) {
    const char *home = getenv("HOME");
    std::string calendarPath(home);
    calendarPath += "/.config/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    calendarPath += "/winbar/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    calendarPath += "/calendar/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(calendarPath.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, ".txt") != NULL) {
                remove(std::string(calendarPath + ent->d_name).c_str());
            }
        }
    }
    
    for (auto *ds: unique_day_text_state) {
        if (!ds->state->text.empty()) {
            std::ofstream myfile;
            myfile.open(calendarPath +
                        std::string(std::to_string(ds->day) + "_" + std::to_string(ds->month) +
                                    "_" + std::to_string(ds->year) + ".txt"));
            if (ds->show_every_year) {
                myfile << ":show_every_year\n";
            }
            myfile << ds->state->text;
            myfile.close();
        }
    }
}

static void
read_agenda_from_disk(AppClient *client) {
    for (auto *ds: unique_day_text_state) {
        delete ds;
    }
    unique_day_text_state.clear();
    unique_day_text_state.shrink_to_fit();
    
    const char *home = getenv("HOME");
    std::string calendarPath(home);
    calendarPath += "/.config/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    calendarPath += "/winbar/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    calendarPath += "/calendar/";
    
    if (mkdir(calendarPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
        if (errno != EEXIST) {
            printf("Couldn't mkdir %s\n", calendarPath.c_str());
            return;
        }
    }
    
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(calendarPath.c_str())) != NULL) {
        /* print all the files and directories within directory */
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, ".txt") != NULL) {
                int day = -1;
                int month = -1;
                int year = -1;
                
                {
                    std::string s = ent->d_name;
                    
                    std::string delimiter = "_";
                    
                    size_t pos = 0;
                    std::string token;
                    int group = 0;
                    while ((pos = s.find(delimiter)) != std::string::npos) {
                        token = s.substr(0, pos);
                        if (group == 0) {
                            day = std::stoi(token);
                        } else if (group == 1) {
                            month = std::stoi(token);
                        }
                        group++;
                        s.erase(0, pos + delimiter.length());
                    }
                    
                    delimiter = ".";
                    while ((pos = s.find(delimiter)) != std::string::npos) {
                        token = s.substr(0, pos);
                        if (group == 2) {
                            year = std::stoi(token);
                        }
                        s.erase(0, pos + delimiter.length());
                    }
                }
                
                if (day != -1 && month != -1 && year != -1) {
                    std::ifstream inFile;
                    inFile.open(calendarPath + ent->d_name);
                    std::stringstream strStream;
                    strStream << inFile.rdbuf();
                    
                    bool found = false;
                    for (auto *ds: unique_day_text_state) {
                        if (ds->day == day && ds->month == month && ds->year == year) {
                            found = true;
                            delete ds->state;
                            ds->state = new TextState;
                            ds->state->text = strStream.str();
                        }
                    }
                    if (!found) {
                        auto *ds = new UniqueTextState;
                        ds->day = day;
                        ds->month = month;
                        ds->year = year;
                        std::string file_content = strStream.str();
                        std::string target_phrase = ":show_every_year\n";
                        auto position = file_content.find(target_phrase);
                        if (position != std::string::npos) {
                            ds->show_every_year = true;
                            file_content.erase(position, position + target_phrase.size());
                        }
                        ds->state->text = file_content; // str holds the content of the file
                        unique_day_text_state.push_back(ds);
                    }
                }
            }
        }
        closedir(dir);
    } else {
        printf("Could not open calendar directory %d\n", calendarPath.c_str());
        return;
    }
}

static void
date_menu_closed(AppClient *client) {
    write_agenda_to_disk(client);
    save_settings_file();
}

static bool time_update_thread_updated = false;

static void paint_date_menu(App *app, AppClient *client, Timeout *timeout, void *data) {
    if (timeout)
        timeout->keep_running = true;
    if (auto *client = client_by_name(app, "date_menu")) {
        client_paint(app, client);
    }
}

void start_date_menu() {
    agenda_showing = winbar_settings->show_agenda;
    Settings settings;
    settings.w = 360 * config->dpi;
    if (agenda_showing) {
        settings.h = 780 * config->dpi;
    } else {
        settings.h = 502 * config->dpi;
    }
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.slide_data[1] = 3;
    if (auto *taskbar = client_by_name(app, "taskbar")) {
        auto *container = container_by_name("date", taskbar->root);
        if (container->real_bounds.x > taskbar->bounds->w / 2) {
            settings.x = taskbar->bounds->x + taskbar->bounds->w - settings.w;
        } else {
            settings.x = 0;
            settings.slide_data[1] = 0;
        }
        settings.y = taskbar->bounds->y - settings.h;
    }
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.sticky = true;
    settings.override_redirect = true;
    settings.slide = true;
    settings.slide_data[0] = -1;
    settings.slide_data[2] = 160;
    settings.slide_data[3] = 100;
    settings.slide_data[4] = 80;
    
    if (auto taskbar = client_by_name(app, "taskbar")) {
        PopupSettings popup_settings;
        popup_settings.name = "date_menu";
        popup_settings.takes_input_focus = true;
        auto client = taskbar->create_popup(popup_settings, settings);
        client->when_closed = date_menu_closed;
        
        if (!time_update_thread_updated) {
            time_update_thread_updated = true;
            app_timeout_create(app, client, 500, paint_date_menu, nullptr, const_cast<char *>(__PRETTY_FUNCTION__));
        }
        
        read_agenda_from_disk(client);
        
        fill_root(client);
        
        client_show(app, client);
    }
}
