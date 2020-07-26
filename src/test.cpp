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

    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR);

    // draw square
    set_argb(cr, ArgbColor(0, 1, 0, 1));
    cairo_rectangle(cr, 10 + 50, 10 + 50, 100, 100);
    cairo_fill(cr);

    cairo_pop_group_to_source(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

void start_test_window() {
    Settings settings;
    settings.w = 1200;
    AppClient *client = client_new(app, settings, "test");
    client->root->when_paint = paint_root;
    client_show(app, client);
}