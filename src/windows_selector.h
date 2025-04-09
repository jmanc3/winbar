#ifndef WINDOWS_SELECTOR_H
#define WINDOWS_SELECTOR_H

#include "container.h"
#include "taskbar.h"
#include <vector>

extern int option_width;
extern int option_height;
extern bool drag_and_dropping;
extern bool have_drag;

struct SleptWindows {
    int pid;
    std::string window_str;
    // surface texture data
};

extern std::vector<SleptWindows *> slept;

class PinnedIconInfo : public IconButton {
public:
    Container *data_container = nullptr;
    LaunchableButton *data = nullptr;
    cairo_surface_t *icon_surface = nullptr;
    
    ~PinnedIconInfo();
};

void possibly_open(App *app, Container *container, LaunchableButton *data);

void possibly_close(App *app, Container *container, LaunchableButton *data);

void start_windows_selector(Container *container, selector_type selector_state);

#endif// WINDOWS_SELECTOR_H
