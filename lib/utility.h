#ifndef UTILITY_HEADER
#define UTILITY_HEADER

#include "application.h"
#include <cairo-xcb.h>
#include <container.h>
#include <pango/pango-layout.h>

struct ArgbColor {
    double r;
    double g;
    double b;
    double a;

    ArgbColor() { r = g = b = a = 0; }

    ArgbColor(double r, double g, double b, double a) {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }

    void add(double r, double g, double b, double a) {
        this->r += r;
        this->g += r;
        this->b += r;
        this->a += r;
    }

    ArgbColor copy() {
        ArgbColor color;
        color.r = r;
        color.g = g;
        color.b = b;
        color.a = a;
        return color;
    }
};

void dye_surface(cairo_surface_t *surface, ArgbColor argb_color);

void dye_opacity(cairo_surface_t *surface, double amount, int thresh_hold);

long get_current_time_in_ms();

ArgbColor
lerp_argb(double scalar, ArgbColor start_color, ArgbColor target_color);

void set_argb(cairo_t *cr, ArgbColor color);

void set_rect(cairo_t *cr, Bounds bounds);

PangoLayout *
get_cached_pango_font(cairo_t *cr, std::string name, int pixel_height, PangoWeight weight);

void cleanup_cached_fonts();

xcb_window_t
get_window(xcb_generic_event_t *event);

xcb_atom_t
get_cached_atom(App *app, std::string name);

void cleanup_cached_atoms();

void launch_command(std::string command);

ArgbColor
darken(ArgbColor b, double amount);

ArgbColor
lighten(ArgbColor b, double amount);

void paint_surface_with_data(cairo_surface_t *surface, uint32_t *icon_data);

cairo_surface_t *
accelerated_surface(App *app, AppClient *client_entity, int w, int h);

bool paint_surface_with_image(cairo_surface_t *surface, std::string path, void (*upon_completion)(bool));

bool paint_png_to_surface(cairo_surface_t *surface, std::string path);

bool paint_svg_to_surface(cairo_surface_t *surface, std::string path);

std::string
as_resource_path(std::string path);

void load_icon_full_path(App *app,
                         AppClient *client_entity,
                         cairo_surface_t **surface,
                         std::string path);

#endif