//
// Created by jmanc3 on 7/7/20.
//

#include "test.h"
#include "main.h"
#include <application.h>
#include <utility.h>
#include "hsluv.h"

static void
paint_root(AppClient *client, cairo_t *cr, Container *container) {
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    set_argb(cr, ArgbColor(1, 0, 0, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    set_argb(cr, ArgbColor(0, .7, 0, 1));
    cairo_rectangle(cr, 10, 10, 100, 100);
    cairo_fill(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_LIGHTEN);
    set_argb(cr, ArgbColor(0, 1, 0, .5));
    cairo_rectangle(cr, 10 + 50, 10 + 50, 100, 100);
    cairo_fill(cr);
}

void start_test_window() {
    Settings settings;
    settings.w = 1200;
    AppClient *client = client_new(app, settings, "test");
    client->root->when_paint = paint_root;
    client_show(app, client);
}