/* date = June 21st 2020 4:16 pm */

#ifndef APP_MENU_H
#define APP_MENU_H

#include "search_menu.h"
#include "taskbar.h"

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
    
    // TODO: gl_surfaces are only valid for their respective clients, not across client
    cairo_surface_t *icon_16__ = nullptr;
    gl_surface *g16 = new gl_surface;
    
    cairo_surface_t *icon_32__ = nullptr;
    gl_surface *g32 = new gl_surface;
    
    cairo_surface_t *icon_24__ = nullptr;
    gl_surface *g24 = new gl_surface;
    
    cairo_surface_t *icon_48__ = nullptr;
    gl_surface *g48 = new gl_surface;
    
    cairo_surface_t *icon_64__ = nullptr;
    gl_surface *g64 = new gl_surface;

    time_t time_modified = 0;
    int priority = 0;
    
    int app_menu_priority = 0;
    PinInfo info;
    
    ~Launcher() {
        if (icon_16__)
            cairo_surface_destroy(icon_16__);
        if (icon_32__)
            cairo_surface_destroy(icon_32__);
        if (icon_24__)
            cairo_surface_destroy(icon_24__);
        if (icon_48__)
            cairo_surface_destroy(icon_48__);
        if (icon_64__)
            cairo_surface_destroy(icon_64__);
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

void start_app_menu(bool autoclose = false);

void load_all_desktop_files();

void save_live_tiles();

void load_live_tiles();

#endif// APP_MENU_H
