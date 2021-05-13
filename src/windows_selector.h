#ifndef WINDOWS_SELECTOR_H
#define WINDOWS_SELECTOR_H

#include "container.h"
#include "taskbar.h"

extern int option_width;
extern int option_height;

void start_windows_selector(Container *container, window_selector_state selector_state);

#endif// WINDOWS_SELECTOR_H
