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
    std::string lowercase_name;
    int priority = -1;
    int historical_ranking = -1;
};

void
start_search_menu();

void
on_key_press_search_bar(xcb_generic_event_t *event);

void
load_scripts();

void
load_historic_apps();

void
load_historic_scripts();

#endif // APP_SEARCH_MENU_H

