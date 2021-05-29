//
// Created by jmanc3 on 5/28/21.
//

#ifndef WINBAR_SIMPLE_DBUS_H
#define WINBAR_SIMPLE_DBUS_H


class App;

void dbus_start_connection(App *app);

void dbus_stop_connection(App *app);

void dbus_process_event(App *app);

#endif //WINBAR_SIMPLE_DBUS_H
