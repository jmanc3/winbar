#ifndef UTILITY_HEADER
#define UTILITY_HEADER

#include "application.h"
#include <cairo-xcb.h>
#include <container.h>
#include <pango/pango-layout.h>
#include <regex>
#include <utility>

static bool parse_hex(std::string hex, double *a, double *r, double *g, double *b) {
    while (hex[0] == '#') { // remove leading pound sign
        hex.erase(0, 1);
    }
    std::regex pattern("([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})([0-9a-fA-F]{2})");

    std::smatch match;
    if (std::regex_match(hex, match, pattern)) {
        double t_a = std::stoul(match[1].str(), nullptr, 16);
        double t_r = std::stoul(match[2].str(), nullptr, 16);
        double t_g = std::stoul(match[3].str(), nullptr, 16);
        double t_b = std::stoul(match[4].str(), nullptr, 16);

        *a = t_a / 255;
        *r = t_r / 255;
        *g = t_g / 255;
        *b = t_b / 255;
        return true;
    }

    return false;
}

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

    ArgbColor(std::string hex) {
        parse_hex(hex, &this->a, &this->r, &this->g, &this->b);
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

long get_current_time_in_seconds();

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

// amount: 0 to 100
void
darken(ArgbColor *color, double amount);

// amount: 0 to 100
void
lighten(ArgbColor *color, double amount);

// amount: 0 to 100
ArgbColor
darken(ArgbColor color, double amount);

uint32_t
argb_to_color(ArgbColor color);

// amount: 0 to 100
ArgbColor
lighten(ArgbColor color, double amount);

void paint_surface_with_data(cairo_surface_t *surface, uint32_t *icon_data);

cairo_surface_t *
accelerated_surface(App *app, AppClient *client_entity, int w, int h);

cairo_surface_t *
accelerated_surface_rgb(App *app, AppClient *client_entity, int w, int h);

bool
paint_surface_with_image(cairo_surface_t *surface, std::string path, int target_size, void (*upon_completion)(bool));

bool paint_png_to_surface(cairo_surface_t *surface, std::string path, int target_size);

bool paint_svg_to_surface(cairo_surface_t *surface, std::string path, int target_size);

std::string
as_resource_path(std::string path);

void load_icon_full_path(App *app,
                         AppClient *client_entity,
                         cairo_surface_t **surface,
                         std::string path,
                         int target_size);

bool screen_has_transparency(App *app);

ArgbColor correct_opaqueness(AppClient *client, ArgbColor color);

void get_average_color(cairo_surface_t *surface, ArgbColor *result);

bool overlaps(double ax, double ay, double aw, double ah,
              double bx, double by, double bw, double bh);

double calculate_overlap_percentage(double ax, double ay, double aw, double ah,
                                    double bx, double by, double bw, double bh);

void
paint_margins_rect(AppClient *client, cairo_t *cr, Bounds b, double width, double pad);

bool is_light_theme(const ArgbColor &color);


#endif