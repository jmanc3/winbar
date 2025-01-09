/* date = June 21st 2020 4:16 pm */

#ifndef APP_MENU_H
#define APP_MENU_H

#include "search_menu.h"

#include <cairo.h>
#include <string>
#include <vector>

class PinInfo {
public:
    int page = 0;
    int w = 2;
    int h = 2;
    int x = -1; // -1 means it needs to be positioned
    int y = -1; // -1 means it needs to be positioned
};

class Launcher : public Sortable {
public:
    std::string full_path;
    std::string icon;
    std::string exec;
    std::string wmclass;
    
    cairo_surface_t *icon_16 = nullptr;
    cairo_surface_t *icon_32 = nullptr;
    cairo_surface_t *icon_24 = nullptr;
    cairo_surface_t *icon_48 = nullptr;
    cairo_surface_t *icon_64 = nullptr;
    
    time_t time_modified = 0;
    int priority = 0;
    
    int app_menu_priority = 0;
    PinInfo info;
    
    ~Launcher() {
        if (icon_16)
            cairo_surface_destroy(icon_16);
        if (icon_32)
            cairo_surface_destroy(icon_32);
        if (icon_24)
            cairo_surface_destroy(icon_24);
        if (icon_48)
            cairo_surface_destroy(icon_48);
        if (icon_64)
            cairo_surface_destroy(icon_64);
    }
    
    void set_pinned(bool pinned) {
        info.x = -1;
        info.y = -1;
        this->is_pinned = pinned;
    }
    
    bool get_pinned() {
        return this->is_pinned;
    }
    
private:
    bool is_pinned = false;
};

extern std::vector<Launcher *> launchers;

void start_app_menu();

void load_all_desktop_files();

void save_live_tiles();

void load_live_tiles();

#endif// APP_MENU_H
