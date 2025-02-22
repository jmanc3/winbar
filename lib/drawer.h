//
// Created by jmanc3 on 2/3/25.
//

#ifndef WINBAR_DRAWER_H
#define WINBAR_DRAWER_H

#include "container.h"
#include "utility.h"
#include "../src/taskbar.h"

void draw_colored_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds);

void draw_round_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, float round, float stroke_w = 0.0f);

void draw_margins_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, double width, double pad);

struct gl_surface;

void draw_gl_texture(AppClient *client, gl_surface *gl_surf, cairo_surface_t *surf, int x, int y, int w = 0, int h = 0);

FontReference *draw_get_font(AppClient *client, int size, std::string font, bool bold = false, bool italic = false);

struct FontText {
    FontReference *f;
    float w;
    float h;
};

FontText draw_text_begin(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, bool bold = false, bool italic = false);

void draw_text(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, Bounds bounds, int alignment = 5, int x_off = -1, int y_off = -1);

void draw_clip_begin(AppClient *client, const Bounds &b);

void draw_clip_end(AppClient *client);

void draw_operator(AppClient *client, int op);

void draw_push_temp(AppClient *client);

void draw_pop_temp(AppClient *client);


#endif //WINBAR_DRAWER_H
