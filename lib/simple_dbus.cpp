//
// Created by jmanc3 on 5/28/21.
//

#include "simple_dbus.h"
#include "application.h"

#include <dbus/dbus.h>
#include <cassert>

static bool poll_descriptor(App *app, int file_descriptor) {
    assert(app != nullptr);
    epoll_event event = {};
    event.events = EPOLLIN | EPOLLPRI | EPOLLHUP | EPOLLERR;
    event.data.fd = file_descriptor;

    if (epoll_ctl(app->epoll_fd, EPOLL_CTL_ADD, file_descriptor, &event) != 0) {
        printf("Failed to add file descriptor: %d\n", file_descriptor);
        close(app->epoll_fd);
        return false;
    }

    return true;
}

void dbus_start_connection(App *app) {
    DBusError error;
    dbus_error_init(&error);
    app->dbus_connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        app->dbus_connection = nullptr;
        return;
    }
    dbus_error_free(&error);

    if (app->dbus_connection) {
        int file_descriptor = -1;
        if (!dbus_connection_get_unix_fd(app->dbus_connection, &file_descriptor)) {
            app->dbus_connection = nullptr;
            return;
        }

        app->dbus_fd = file_descriptor;
        poll_descriptor(app, app->dbus_fd);
    }
}

void dbus_stop_connection(App *app) {
    if (app->dbus_connection) {
        ::dbus_connection_unref(app->dbus_connection);
        app->dbus_connection = nullptr;
    }
}

void dbus_process_event(App *app) {
    if (app->dbus_connection) {
        DBusDispatchStatus status;
        do {
            dbus_connection_read_write_dispatch(app->dbus_connection, 0);
            status = dbus_connection_get_dispatch_status(app->dbus_connection);
        } while (status == DBUS_DISPATCH_DATA_REMAINS);
    }
}


