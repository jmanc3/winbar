/* date = June 21st 2020 4:16 pm */

#ifndef APP_MENU_H
#define APP_MENU_H

#include "search_menu.h"

#include <cairo.h>
#include <string>
#include <vector>

class Launcher : public Sortable {
public:
    std::string icon;
    std::string exec;
    std::string wmclass;

    cairo_surface_t *icon_16 = nullptr;
    cairo_surface_t *icon_32 = nullptr;
    cairo_surface_t *icon_24 = nullptr;
    cairo_surface_t *icon_64 = nullptr;

    ~Launcher() {
        if (icon_16)
            cairo_surface_destroy(icon_16);
        if (icon_32)
            cairo_surface_destroy(icon_32);
        if (icon_24)
            cairo_surface_destroy(icon_24);
        if (icon_64)
            cairo_surface_destroy(icon_64);
    }
};

extern std::vector<Launcher *> launchers;

void start_app_menu();

void load_all_desktop_files();

#endif// APP_MENU_H
