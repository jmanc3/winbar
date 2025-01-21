//
// Created by jmanc3 on 6/25/20.
//

#ifndef APP_SEARCH_MENU_H
#define APP_SEARCH_MENU_H

#include <application.h>
#include <string>
#include <xcb/xcb.h>

class Sortable {
public:
    std::string name;
    std::vector<std::string> keywords;
    std::string lowercase_name;
    int priority = -1;
    int historical_ranking = -1;
    std::string full_path;
};

extern std::string active_tab;

void start_search_menu();

void on_key_press_search_bar(xcb_generic_event_t *event);

void load_scripts(bool do_now = false);

void load_historic_apps();

void load_historic_scripts();

bool script_exists(const std::string &name);

#endif// APP_SEARCH_MENU_H
