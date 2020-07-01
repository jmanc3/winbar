/* date = June 21st 2020 4:16 pm */

#ifndef APP_MENU_H
#define APP_MENU_H

#include "search_menu.h"

#include <cairo.h>
#include <string>
#include <vector>

class Launcher : public Sortable
{
  public:
    std::string icon;
    std::string exec;

    cairo_surface_t* icon_16 = nullptr;
    cairo_surface_t* icon_32 = nullptr;
    cairo_surface_t* icon_24 = nullptr;
    cairo_surface_t* icon_64 = nullptr;
};

extern std::vector<Launcher*> launchers;

void
start_app_menu();

void
load_desktop_files();

#endif // APP_MENU_H
