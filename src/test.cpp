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
    set_argb(cr, ArgbColor(1, 0, 0, 1));
    set_rect(cr, container->real_bounds);
    cairo_fill(cr);

    {
        ArgbColor color = ArgbColor(0, 1, 0, 1);
        set_argb(cr, color);
        cairo_rectangle(cr,
                        0,
                        0,
                        40,
                        40);
        cairo_fill(cr);

        double ph;
        double ps;
        double pl;
        rgb2hsluv(color.r, color.g, color.b, &ph, &ps, &pl);
        printf("pl: %f\n", pl);
        pl -= 10;
        hsluv2rgb(ph, ps, pl, &color.r, &color.g, &color.b);

        set_argb(cr, color);
        cairo_rectangle(cr,
                        40,
                        0,
                        40,
                        40);
        cairo_fill(cr);
    }

    {
        ArgbColor color = ArgbColor(1, 1, 1, 1);
        set_argb(cr, color);
        cairo_rectangle(cr,
                        0,
                        40,
                        40,
                        40);
        cairo_fill(cr);

        double ph;
        double ps;
        double pl;
        rgb2hsluv(color.r, color.g, color.b, &ph, &ps, &pl);
        printf("pl: %f\n", pl);
        pl -= 10;
        hsluv2rgb(ph, ps, pl, &color.r, &color.g, &color.b);

        set_argb(cr, color);
        cairo_rectangle(cr,
                        40,
                        40,
                        40,
                        40);
        cairo_fill(cr);
    }


    {
        ArgbColor color = ArgbColor(0, 0, 0, 1);
        set_argb(cr, color);
        cairo_rectangle(cr,
                        0,
                        80,
                        40,
                        40);
        cairo_fill(cr);

        double ph;
        double ps;
        double pl;
        rgb2hsluv(color.r, color.g, color.b, &ph, &ps, &pl);
        printf("pl: %f\n", pl);
        pl += 10;
        hsluv2rgb(ph, ps, pl, &color.r, &color.g, &color.b);

        set_argb(cr, color);
        cairo_rectangle(cr,
                        40,
                        80,
                        40,
                        40);
        cairo_fill(cr);
    }
}

void start_test_window() {
    Settings settings;
    settings.w = 1200;
    AppClient *client = client_new(app, settings, "test");
    client->root->when_paint = paint_root;
    client_show(app, client);
}