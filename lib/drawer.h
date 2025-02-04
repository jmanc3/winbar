//
// Created by jmanc3 on 2/3/25.
//

#ifndef WINBAR_DRAWER_H
#define WINBAR_DRAWER_H

#include "container.h"
#include "utility.h"
#include "../src/taskbar.h"

void draw_colored_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds);

void draw_margins_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, double width, double pad);

struct gl_surface;

void draw_gl_texture(AppClient *client, gl_surface *gl_surf, cairo_surface_t *surf, int x, int y, int w = 0, int h = 0);

FontReference *draw_get_font(AppClient *client, int size, std::string font);

void draw_text(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, Bounds bounds, int alignment = 5, int x_off = -1, int y_off = -1);

#endif //WINBAR_DRAWER_H
