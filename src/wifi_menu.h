/* date = June 11th 2020 10:51 am */

#ifndef WIFI_MENU_H
#define WIFI_MENU_H

#include "application.h"

void wifi_state(AppClient *client, bool *up, bool *wired);

void start_wifi_menu();

#endif// WIFI_MENU_H
