/* date = February 20th 2021 11:33 pm */

#ifndef DPI_H
#define DPI_H

#include "application.h"
#include <vector>

extern std::vector<ScreenInformation *> screens;

void dpi_setup(App *app);

#endif //DPI_H
