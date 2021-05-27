
#include "date_menu.h"
#include "application.h"
#include "components.h"
#include "config.h"
#include "main.h"
#include "taskbar.h"

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
            for (auto *row_container : dates_container->children) {
                for (auto *date : row_container->children) {
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
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_date_background));
    cairo_fill(cr);
}

static void
paint_title(AppClient *client, cairo_t *cr, Container *container) {
    time_t now = time(0);
    tm *ltm = localtime(&now);

    const char *months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"};

    int hour = ltm->tm_hour;
    if (hour > 12) {
        hour -= 12;
    }
    std::string sec_start = std::to_string(ltm->tm_sec);
    if (ltm->tm_sec < 10) {
        sec_start = std::string(1, '0').append(sec_start);
    }

    std::string min_start = std::to_string(ltm->tm_min);
    if (ltm->tm_min < 10) {
        min_start = std::string(1, '0').append(min_start);
    }

    std::string top_text = std::to_string(hour) + ":" + min_start + ":" + sec_start;
    PangoLayout *top_layout =
            get_cached_pango_font(client->cr, config->font, 34, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(top_layout, top_text.c_str(), top_text.length());
    PangoRectangle top_ink;
    PangoRectangle top_logical;
    pango_layout_get_extents(top_layout, &top_ink, &top_logical);

    ltm->tm_hour = 12;
    std::string top_right_text = "AM";
    if (ltm->tm_hour > 11) {
        top_right_text = "PM";
    }
    PangoLayout *top_right_layout =
            get_cached_pango_font(client->cr, config->font, 14, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(top_right_layout, top_right_text.c_str(), top_right_text.length());
    PangoRectangle top_right_ink;
    PangoRectangle top_right_logical;
    pango_layout_get_extents(top_right_layout, &top_right_ink, &top_right_logical);

    const char *date_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};
    int day_index = day(ltm->tm_mday, ltm->tm_mon + 1, 1900 + ltm->tm_year);

    std::string bottom_text =
            std::string(date_names[day_index]) + " " + std::string(months[ltm->tm_mon]) + " " +
            std::to_string(ltm->tm_mday) + ", " + std::to_string(1900 + ltm->tm_year);
    PangoLayout *bottom_layout =
            get_cached_pango_font(client->cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(bottom_layout, bottom_text.c_str(), bottom_text.length());
    PangoRectangle bottom_ink;
    PangoRectangle bottom_logical;
    pango_layout_get_extents(bottom_layout, &bottom_ink, &bottom_logical);

    int top_x = container->real_bounds.x + 24 - (top_ink.x / PANGO_SCALE);
    int top_y = container->real_bounds.y + 27 - (top_ink.y / PANGO_SCALE);
    set_argb(cr, config->color_date_text_title);
    cairo_move_to(cr, top_x, top_y);
    pango_cairo_show_layout(cr, top_layout);

    int top_right_x = container->real_bounds.x + 24 - (top_right_ink.x / PANGO_SCALE) +
                      (top_ink.width / PANGO_SCALE) + 10;
    int top_right_y = container->real_bounds.y + 27 - (top_right_ink.y / PANGO_SCALE) +
                      (top_ink.height / PANGO_SCALE) - (top_right_ink.height / PANGO_SCALE);
    set_argb(cr, config->color_date_text_title_period);
    cairo_move_to(cr, top_right_x, top_right_y);
    pango_cairo_show_layout(cr, top_right_layout);

    int bottom_x = container->real_bounds.x + 25 - (bottom_ink.x / PANGO_SCALE);
    int bottom_y = container->real_bounds.y + 27 + (top_ink.height / PANGO_SCALE) + 16;
    set_argb(cr, config->color_date_text_title_info);
    cairo_move_to(cr, bottom_x, bottom_y);
    pango_cairo_show_layout(cr, bottom_layout);
}

static void
paint_body(AppClient *client, cairo_t *cr, Container *container) {
    cairo_rectangle(
            cr, container->real_bounds.x, container->real_bounds.y, container->real_bounds.w, 1);
    set_argb(cr, config->color_date_seperator);
    cairo_fill(cr);

    cairo_rectangle(cr,
                    container->real_bounds.x,
                    container->real_bounds.y + container->real_bounds.h - 1,
                    container->real_bounds.w,
                    1);
    set_argb(cr, config->color_date_seperator);
    cairo_fill(cr);
}

static void
paint_centered_text(AppClient *client, cairo_t *cr, Container *container, std::string text) {
    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 9, PangoWeight::PANGO_WEIGHT_NORMAL);
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
}

static void
paint_weekday_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (weekday_title *) container->user_data;
    set_argb(cr, config->color_date_text_week_day);
    paint_centered_text(client, cr, container, data->text);
}

static void
paint_events(AppClient *client, cairo_t *cr, Container *container) {
    if (!agenda_showing || !container->exists) {
        return;
    }
    set_argb(cr, config->color_date_weekday_monthday);
    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 13, PangoWeight::PANGO_WEIGHT_NORMAL);

    const char *date_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};

    int day_index = 0;
    day_index = day(agenda_day, agenda_month + 1, agenda_year);

    std::string text = std::string(date_names[day_index]) + " " + std::to_string(agenda_day);
    pango_layout_set_text(text_layout, text.c_str(), text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);

    cairo_move_to(cr, container->real_bounds.x + 24, container->real_bounds.y + 21);
    pango_cairo_show_layout(cr, text_layout);
}

static void
paint_agenda(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;

    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);
    std::string text;
    if (agenda_showing) {
        text = "Hide agenda";
    } else {
        text = "Show agenda";
    }
    pango_layout_set_text(text_layout, text.c_str(), text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);

    ArgbColor color;
    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            set_argb(cr, (color = config->color_date_text_pressed_button));
        else
            set_argb(cr, (color = config->color_date_text_hovered_button));
    } else {
        set_argb(cr, (color = config->color_date_text_default_button));
    }
    int pos_x =
            container->real_bounds.x + container->real_bounds.w - (text_logical.width / PANGO_SCALE) - 24;
    int pos_y = container->real_bounds.y + container->real_bounds.h / 2 -
                ((text_logical.height / PANGO_SCALE) / 2);
    //    int pos_y = container->real_bounds.y + container->real_bounds.h / 2 -
    //                (text_logical.height / PANGO_SCALE) / 2;
    cairo_move_to(cr, pos_x, pos_y);
    pango_cairo_show_layout(cr, text_layout);

    if (data->surface) {
        dye_surface(data->surface, color);
        cairo_set_source_surface(
                cr, data->surface, pos_x + (text_logical.width / PANGO_SCALE) + 5, pos_y + 4);
        cairo_paint(cr);
    }
}

static void
paint_month_year_label(AppClient *client, cairo_t *cr, Container *container) {
    const char *months[] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"};

    std::string text = months[view_month];
    text += " " + std::to_string(view_year);
    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(text_layout, text.c_str(), text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);

    set_argb(cr, config->color_date_text_month_year);
    cairo_move_to(cr, container->real_bounds.x, container->real_bounds.y);
    pango_cairo_show_layout(cr, text_layout);
}

static void
paint_arrow(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (IconButton *) container->user_data;
    if (data->surface) {
        if (container->state.mouse_hovering || container->state.mouse_pressing) {
            if (container->state.mouse_pressing)
                dye_surface(data->surface, config->color_date_pressed_arrow);
            else
                dye_surface(data->surface, config->color_date_hovered_arrow);
        } else {
            dye_surface(data->surface, config->color_date_default_arrow);
        }

        cairo_set_source_surface(
                cr,
                data->surface,
                (int) (container->real_bounds.x + container->real_bounds.w / 2 - 8),
                (int) (container->real_bounds.y + container->real_bounds.h / 2 - 8));
        cairo_paint(cr);
    }
}

static void
paint_textarea_parent(AppClient *client, cairo_t *cr, Container *container) {
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        if (data->state->text.empty() && !container->active) {
            PangoLayout *text_layout = get_cached_pango_font(
                    client->cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
            std::string text("Write the days events here");
            pango_layout_set_text(text_layout, text.c_str(), text.length());
            PangoRectangle text_ink;
            PangoRectangle text_logical;
            pango_layout_get_extents(text_layout, &text_ink, &text_logical);

            set_argb(cr, config->color_date_text_prompt);
            cairo_move_to(cr,
                          container->real_bounds.x - (text_ink.x / PANGO_SCALE) +
                          container->real_bounds.w / 2 - (text_ink.width / PANGO_SCALE) / 2,
                          container->real_bounds.y - (text_ink.y / PANGO_SCALE) +
                          container->real_bounds.h / 2 - (text_ink.height / PANGO_SCALE) / 2);
            pango_cairo_show_layout(cr, text_layout);
        } else {
            set_argb(cr, config->color_date_seperator);
            paint_margins_rect(client, cr, container->real_bounds, 1, -2);
        }
    }
}

static void
paint_date_title(AppClient *client, cairo_t *cr, Container *container) {
    auto *data = (date_title *) container->user_data;

    int pad = 2;
    int width = 2;

    for (auto *ud : unique_day_text_state) {
        if (ud->day == data->day && ud->month == data->month && ud->year == data->year &&
            !ud->state->text.empty()) {
            set_argb(cr, config->color_date_cal_border);
            paint_margins_rect(client, cr, container->real_bounds, 1, -1);
            cairo_fill(cr);
        }
    }

    set_argb(cr, config->color_date_cal_foreground); // TODO: is this needed?
    bool is_today =
            data->month == current_month && data->year == current_year && data->day == current_day;
    bool is_day_chosen =
            data->month == agenda_month && data->year == agenda_year && data->day == agenda_day;
    if (is_today) {
        set_rect(cr, container->real_bounds);
        set_argb(cr, config->color_date_cal_background);
        cairo_fill(cr);
    }

    if (is_day_chosen) {
        set_argb(cr, config->color_date_cal_background);
        paint_margins_rect(client, cr, container->real_bounds, 2, 0);
    }

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (is_today || is_day_chosen) {
            ArgbColor b = config->color_date_cal_background;
            lighten(&b, 5);
            if (container->state.mouse_pressing) {
                lighten(&b, 5);
                set_argb(cr, b);
            } else {
                set_argb(cr, config->color_date_cal_background);
            }
        } else {
            ArgbColor b = config->color_date_cal_border;
            lighten(&b, 5);
            if (container->state.mouse_pressing) {
                lighten(&b, 5);
                set_argb(cr, b);
            } else {
                set_argb(cr, config->color_date_cal_border);
            }
        }

        paint_margins_rect(client, cr, container->real_bounds, 2, 0);
    }

    if (((container->state.mouse_hovering || container->state.mouse_pressing) || is_day_chosen) &&
        is_today) {
        set_argb(cr, config->color_date_cal_foreground);
        paint_margins_rect(client, cr, container->real_bounds, 2, 2);
    }

    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 11, PangoWeight::PANGO_WEIGHT_NORMAL);
    pango_layout_set_text(text_layout, data->text.c_str(), data->text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);

    int baseline = pango_layout_get_baseline(text_layout);

    if (data->month == view_month) {
        set_argb(cr, config->color_date_text_current_month);
    } else {
        set_argb(cr, config->color_date_text_not_current_month);
    }
    cairo_move_to(cr,
                  container->real_bounds.x - (text_ink.x / PANGO_SCALE) +
                  container->real_bounds.w / 2 - (text_ink.width / PANGO_SCALE) / 2,
                  container->real_bounds.y - (text_ink.y / PANGO_SCALE) +
                  container->real_bounds.h / 2 - (text_ink.height / PANGO_SCALE) / 2);
    pango_cairo_show_layout(cr, text_layout);
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
    for (auto *ud : unique_day_text_state) {
        if (ud->day == agenda_day && ud->year == agenda_year && ud->month == agenda_month) {
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
                c->wanted_pad = Bounds(24, 58, 24, 0);
            }
        }

        uint32_t value_mask =
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

        uint32_t value_list_resize[4];
        if (agenda_showing) {
            value_list_resize[0] = client->bounds->x;
            value_list_resize[1] = app->bounds.h - config->taskbar_height - 502;
            value_list_resize[2] = client->bounds->w;
            value_list_resize[3] = 502;
            xcb_configure_window(app->connection, client->window, value_mask, value_list_resize);
            handle_configure_notify(app, client, value_list_resize[0],
                                    value_list_resize[1],
                                    value_list_resize[2],
                                    value_list_resize[3]);
        } else {
            value_list_resize[0] = client->bounds->x;
            value_list_resize[1] = app->bounds.h - config->taskbar_height - 735;
            value_list_resize[2] = client->bounds->w;
            value_list_resize[3] = 735;
            xcb_configure_window(app->connection, client->window, value_mask, value_list_resize);
            handle_configure_notify(app, client, value_list_resize[0],
                                    value_list_resize[1],
                                    value_list_resize[2],
                                    value_list_resize[3]);
        }
        xcb_flush(app->connection);
        agenda_showing = !agenda_showing;
    }
}

static void
clicked_clear_text(AppClient *client, cairo_t *cr, Container *container) {
    // TODO: go through agenda and clear that
    if (auto *c = container_by_name("main_text_area", client->root)) {
        auto *data = (TextAreaData *) c->user_data;
        delete data->state;
        data->state = new TextState;

        for (auto ud : unique_day_text_state) {
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

    auto *data = (IconButton *) container->user_data;

    PangoLayout *text_layout =
            get_cached_pango_font(client->cr, config->font, 10, PangoWeight::PANGO_WEIGHT_NORMAL);

    std::string text = "Clear text";
    pango_layout_set_text(text_layout, text.c_str(), text.length());
    PangoRectangle text_ink;
    PangoRectangle text_logical;
    pango_layout_get_extents(text_layout, &text_ink, &text_logical);

    if (container->state.mouse_hovering || container->state.mouse_pressing) {
        if (container->state.mouse_pressing)
            set_argb(cr, config->color_date_text_pressed_button);
        else
            set_argb(cr, config->color_date_text_hovered_button);
    } else {
        set_argb(cr, config->color_date_text_default_button);
    }
    int pos_x =
            container->real_bounds.x + container->real_bounds.w - (text_logical.width / PANGO_SCALE) - 24;
    int pos_y = container->real_bounds.y + container->real_bounds.h / 2 -
                ((text_logical.height / PANGO_SCALE) / 2);
    cairo_move_to(cr, container->real_bounds.x, pos_y);
    pango_cairo_show_layout(cr, text_layout);
}

static void
fill_root(AppClient *client) {
    Container *root = client->root;
    root->when_paint = paint_root;
    root->type = ::vbox;

    Container *title = root->child(FILL_SPACE, 111);
    title->when_paint = paint_title;

    Container *body = root->child(FILL_SPACE, 343);
    body->type = ::vbox;
    body->when_paint = paint_body;

    Container *up_down_hbox = body->child(FILL_SPACE, 18 + 18 + 16);
    up_down_hbox->type = ::hbox;
    up_down_hbox->wanted_pad.x = 24;
    up_down_hbox->wanted_pad.w = 28;
    up_down_hbox->wanted_pad.y = 18;
    up_down_hbox->wanted_pad.h = 18;

    Container *month_year_label = up_down_hbox->child(66, FILL_SPACE);
    month_year_label->when_paint = paint_month_year_label;

    // Pad
    up_down_hbox->child(FILL_SPACE, FILL_SPACE);

    Container *up_arrow = up_down_hbox->child(16, FILL_SPACE);
    up_arrow->when_paint = paint_arrow;
    up_arrow->when_clicked = clicked_up_arrow;
    auto *up_data = new IconButton;
    up_arrow->user_data = up_data;
    up_data->surface = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(up_data->surface, as_resource_path("arrow-up-16.png"), 16, nullptr);

    // Pad
    up_down_hbox->child(32, FILL_SPACE);

    Container *down_arrow = up_down_hbox->child(16, FILL_SPACE);
    down_arrow->when_paint = paint_arrow;
    down_arrow->when_clicked = clicked_down_arrow;
    auto *down_data = new IconButton;
    down_arrow->user_data = down_data;
    down_data->surface = accelerated_surface(app, client, 16, 16);
    paint_surface_with_image(down_data->surface, as_resource_path("arrow-down-16.png"), 16, nullptr);

    Container *day_titles = body->child(FILL_SPACE, 27);
    day_titles->type = ::hbox;
    day_titles->wanted_pad.x = 13;
    day_titles->wanted_pad.w = 13;
    day_titles->spacing = 2;

    const char *names[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; i++) {
        auto day_title = day_titles->child(FILL_SPACE, FILL_SPACE);
        day_title->spacing = 2;
        day_title->when_paint = paint_weekday_title;
        auto *data = new weekday_title;
        data->text = names[i];
        day_title->user_data = data;
    }

    Container *dates = body->child(FILL_SPACE, FILL_SPACE);
    dates->type = ::vbox;
    dates->wanted_pad.x = 13;
    dates->wanted_pad.w = 13;
    dates->wanted_pad.y = 6;
    dates->wanted_pad.h = 6;
    dates->spacing = 2;
    dates->name = "dates_container";

    int x = 0;
    for (int i = 0; i < 6; i++) {
        Container *temp_hbox = dates->child(FILL_SPACE, 40);
        temp_hbox->type = ::hbox;
        temp_hbox->spacing = 2;

        for (int i = 0; i < 7; i++) {
            x++;
            Container *date = temp_hbox->child(FILL_SPACE, FILL_SPACE);
            date->when_paint = paint_date_title;
            date->when_clicked = clicked_date;
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
    events->wanted_pad = Bounds(24, 58, 24, 0);
    events->when_paint = paint_events;

    TextAreaSettings settings;
    settings.color = config->color_date_text;
    settings.color_cursor = config->color_date_cursor;
    settings.font = config->font;
    settings.font_size = 11;
    settings.wrap = true;
    settings.bottom_show_amount = 2;
    settings.right_show_amount = 2;
    Container *textarea = make_textarea(app, client, events, settings);
    TextAreaData *data = (TextAreaData *) textarea->user_data;

    textarea->name = "main_text_area";
    textarea->parent->when_paint = paint_textarea_parent;


    for (auto ud : unique_day_text_state) {
        if (ud->day == agenda_day && ud->month == agenda_month && ud->year == agenda_year) {
            delete data->state;
            data->state = ud->state;
            break;
        }
    }

    Container *agenda_hbox = root->child(::hbox, FILL_SPACE, 48);
    agenda_hbox->wanted_pad.x = 20;
    agenda_hbox->wanted_pad.w = 10;
    Container *clear_text = agenda_hbox->child(FILL_SPACE, FILL_SPACE);
    clear_text->when_paint = paint_clear_text;
    clear_text->when_clicked = clicked_clear_text;

    Container *agenda = agenda_hbox->child(FILL_SPACE, FILL_SPACE);
    agenda->type = ::hbox;
    agenda->when_paint = paint_agenda;
    agenda->when_clicked = clicked_agenda;
    auto *agenda_data = new IconButton;
    load_icon_full_path(app, client, &agenda_data->surface, as_resource_path("arrow-down-12.png"), 12);
    agenda->user_data = agenda_data;
}

static bool first_expose = true;

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

    for (auto *ds : unique_day_text_state) {
        if (!ds->state->text.empty()) {
            std::ofstream myfile;
            myfile.open(calendarPath +
                        std::string(std::to_string(ds->day) + "_" + std::to_string(ds->month) +
                                    "_" + std::to_string(ds->year) + ".txt"));
            myfile << ds->state->text;
            myfile.close();
        }
    }
}

static void
read_agenda_from_disk(AppClient *client) {
    for (auto *ds : unique_day_text_state) {
        delete ds;
    }
    unique_day_text_state.clear();

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
                    for (auto *ds : unique_day_text_state) {
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

                        ds->state->text = strStream.str(); // str holds the content of the file
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
grab_event_handler(AppClient *client, xcb_generic_event_t *event) {
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_BUTTON_PRESS: {
            auto *e = (xcb_button_press_event_t *) (event);

            if (auto *textarea = container_by_name("main_text_area", client->root)) {
                if (!bounds_contains(textarea->real_bounds, e->event_x, e->event_y)) {
                    textarea->parent->active = false;
                }
            }

            if (!bounds_contains(*client->bounds, e->root_x, e->root_y)) {
                client_close_threaded(app, client);
                xcb_flush(app->connection);
                app->grab_window = -1;
                set_textarea_inactive();

                if (auto c = client_by_name(client->app, "taskbar")) {
                    if (auto co = container_by_name("date", c->root)) {
                        if (co->state.mouse_hovering) {
                            auto data = (IconButton *) co->user_data;
                            data->invalid_button_down = true;
                            data->timestamp = get_current_time_in_ms();
                        }
                    }
                }
            }
            break;
        }
    }
}

static bool
date_menu_event_handler(App *app, xcb_generic_event_t *event) {
    // For detecting if we pressed outside the window
    switch (XCB_EVENT_RESPONSE_TYPE(event)) {
        case XCB_MAP_NOTIFY: {
            auto *e = (xcb_map_notify_event_t *) (event);
            register_popup(e->window);
            xcb_set_input_focus(app->connection, XCB_NONE, e->window, XCB_CURRENT_TIME);
            xcb_flush(app->connection);
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

    return false;
}

static void
date_menu_closed(AppClient *client) {
    write_agenda_to_disk(client);
}

static bool time_update_thread_updated = false;

static void paint_date_menu(App *app, AppClient *client, void *data) {
    if (auto *client = client_by_name(app, "date_menu")) {
        client_paint(app, client);
        app_timeout_create(app, client, 500, paint_date_menu, nullptr);
    }
}

void start_date_menu() {
    Settings settings;
    settings.w = 360;
    if (agenda_showing) {
        settings.h = 780;
    } else {
        settings.h = 502;
    }
    settings.x = app->bounds.w - settings.w;
    settings.y = app->bounds.h - settings.h - config->taskbar_height;
    settings.skip_taskbar = true;
    settings.decorations = false;
    settings.force_position = true;
    settings.sticky = true;
    settings.popup = true;

    AppClient *client = client_new(app, settings, "date_menu");
    client->grab_event_handler = grab_event_handler;

    client->when_closed = date_menu_closed;

    app_create_custom_event_handler(app, client->window, date_menu_event_handler);

    if (!time_update_thread_updated) {
        time_update_thread_updated = true;
        app_timeout_create(app, client, 500, paint_date_menu, nullptr);
    }

    read_agenda_from_disk(client);

    fill_root(client);

    client_show(app, client);
}
