
#include "utility.h"
#include "hsluv.h"
#include <stdio.h>
#include <X11/Xlib.h>

#ifdef TRACY_ENABLE

#include "../tracy/public/tracy/Tracy.hpp"

#endif

#include <chrono>
#include <algorithm>
#include <cmath>
#include <librsvg/rsvg.h>
#include <pango/pangocairo.h>
#include <zconf.h>
#include <cassert>
#include <sys/wait.h>
#include <xcb/xcb_aux.h>
#include <iostream>
#include <sys/poll.h>

void dye_surface(cairo_surface_t *surface, ArgbColor argb_color) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (surface == nullptr)
        return;
    cairo_surface_flush(surface);
    
    unsigned char *data = cairo_image_surface_get_data(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    for (int y = 0; y < height; y++) {
        auto row = (uint32_t *) data;
        data += stride;
        
        for (int x = 0; x < width; x++) {
            unsigned int color = *row;
            
            unsigned int alpha = ((color >> 24) & 0xFF);
            
            unsigned int red = std::floor(argb_color.r * 255);
            unsigned int green = std::floor(argb_color.g * 255);
            unsigned int blue = std::floor(argb_color.b * 255);
            
            // pre multiplied alpha
            // a r g b
            // unsigned int set_argb = (0x44 << 24) | (0x44 << 16) | (0x00 << 8) |
            // 0x44; https://microsoft.github.io/Win2D/html/PremultipliedAlpha.htm
            // https://www.cairographics.org/manual/cairo-Image-Surfaces.html#cairo-format-t
            unsigned int set_color = (alpha << 24) | ((red * alpha / 255) << 16) |
                                     (green * alpha / 255 << 8) | blue * alpha / 255;
            
            *row = set_color;
            row++;
        }
    }
}

void dye_opacity(cairo_surface_t *surface, double amount, int thresh_hold) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (surface == nullptr)
        return;
    cairo_surface_flush(surface);
    
    unsigned char *data = cairo_image_surface_get_data(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    for (int y = 0; y < height; y++) {
        auto row = (uint32_t *) data;
        data += stride;
        
        for (int x = 0; x < width; x++) {
            unsigned int color = *row;
            
            unsigned int alpha = ((color >> 24) & 0xFF);
            if (alpha != 0) {
                if (alpha > thresh_hold) {
                    alpha += (amount * 255);
                    if (alpha > 255) {
                        alpha = 255;
                    }
                    if (alpha < 0) {
                        alpha = 0;
                    }
                }
            }
            unsigned int red = ((color >> 16) & 0xFF);
            unsigned int green = ((color >> 8) & 0xFF);
            unsigned int blue = ((color >> 0) & 0xFF);
            
            // pre multiplied alpha
            // a r g b
            // unsigned int set_argb = (0x44 << 24) | (0x44 << 16) | (0x00 << 8) |
            // 0x44; https://microsoft.github.io/Win2D/html/PremultipliedAlpha.htm
            // https://www.cairographics.org/manual/cairo-Image-Surfaces.html#cairo-format-t
            unsigned int set_color = (alpha << 24) | ((red * alpha / 255) << 16) |
                                     (green * alpha / 255 << 8) | blue * alpha / 255;
            
            *row = set_color;
            row++;
        }
    }
}

void get_average_color(cairo_surface_t *surface, ArgbColor *result) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    result->a = 1;
    result->r = 1;
    result->g = 1;
    result->b = 1;
    if (surface == nullptr)
        return;
    cairo_surface_flush(surface);
    
    unsigned char *data = cairo_image_surface_get_data(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    
    int total_pixels = width * height;
    double a = 0;
    double r = 0;
    double g = 0;
    double b = 0;
    
    for (int y = 0; y < height; y++) {
        auto row = (uint32_t *) data;
        data += stride;
        
        for (int x = 0; x < width; x++) {
            unsigned int color = *row;
            
            unsigned int alpha = ((color >> 24) & 0xFF);
            unsigned int red = ((color >> 16) & 0xFF);
            unsigned int green = ((color >> 8) & 0xFF);
            unsigned int blue = ((color >> 0) & 0xFF);
            if (alpha == 0) {
                total_pixels--;
            } else {
                a += (((double) alpha) / 255);
                r += (((double) red) / 255);
                g += (((double) green) / 255);
                b += (((double) blue) / 255);
            }
            
            row++;
        }
    }
    result->a = a / (total_pixels);
    result->r = r / (total_pixels);
    result->g = g / (total_pixels);
    result->b = b / (total_pixels);
}

ArgbColor
lerp_argb(double scalar, ArgbColor start_color, ArgbColor target_color) {
    ArgbColor color;
    color.r = ((target_color.r - start_color.r) * scalar) + start_color.r;
    color.g = ((target_color.g - start_color.g) * scalar) + start_color.g;
    color.b = ((target_color.b - start_color.b) * scalar) + start_color.b;
    color.a = ((target_color.a - start_color.a) * scalar) + start_color.a;
    return color;
}

long get_current_time_in_ms() {
    using namespace std::chrono;
    milliseconds currentTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return currentTime.count();
}

long get_current_time_in_seconds() {
    using namespace std::chrono;
    milliseconds currentTime = duration_cast<seconds>(system_clock::now().time_since_epoch());
    return currentTime.count();
}

void set_argb(cairo_t *cr, ArgbColor color) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
}

void set_rect(cairo_t *cr, Bounds bounds) {
    cairo_rectangle(cr, bounds.x, bounds.y, bounds.w, bounds.h);
}

std::vector<CachedFont *> cached_fonts;

PangoLayout *
get_cached_pango_font(cairo_t *cr, std::string name, int pixel_height, PangoWeight weight) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
	for (int i = cached_fonts.size() - 1; i >= 0; i--) {
    	auto font = cached_fonts[i];
        if (font->name == name && font->size == pixel_height && font->weight == weight && font->cr == cr) {
            pango_layout_set_attributes(font->layout, nullptr);
            font->used_count++;
            if (font->used_count < 512) {
//            printf("returned: %p\n", font->layout);
            	return font->layout;
            } else {
				delete font;
				cached_fonts.erase(cached_fonts.begin() + i);
            }
        }
    }
    
    auto *font = new CachedFont;
    assert(font);
    font->name = name;
    font->size = pixel_height;
    font->weight = weight;
    font->cr = cr;
    font->used_count = 0;
    
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_size(desc, pixel_height * PANGO_SCALE);
    pango_font_description_set_family_static(desc, name.c_str());
    pango_font_description_set_weight(desc, weight);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_attributes(layout, nullptr);
    
    assert(layout);
    
    font->layout = layout;
//    printf("new: %p\n", font->layout);
    
    cached_fonts.push_back(font);
    
    assert(font->layout);
    
    return font->layout;
}

void cleanup_cached_fonts() {
    for (auto font: cached_fonts) {
        delete font;
    }
    cached_fonts.clear();
    cached_fonts.shrink_to_fit();
}

void remove_cached_fonts(cairo_t *cr) {
    for (int i = cached_fonts.size() - 1; i >= 0; --i) {
        if (cached_fonts[i]->cr == cr) {
//            printf("removed: %p\n", cached_fonts[i]->layout);
            delete cached_fonts[i];
            cached_fonts.erase(cached_fonts.begin() + i);
        }
    }
}

#define get_window_from_casted_event__explicit_member(X, Y, W) \
case X: {                                                  \
auto *e = (Y##_event_t *) (event);                     \
return e->W;                                           \
}
#define get_window_from_casted_event(X, Y) \
get_window_from_casted_event__explicit_member(X, Y, event)

xcb_window_t
get_window(xcb_generic_event_t *event) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!event) {
        return 0;
    }
    switch (event->response_type & ~0x80) {
        get_window_from_casted_event(XCB_KEY_PRESS, xcb_key_press);
        get_window_from_casted_event(XCB_KEY_RELEASE, xcb_key_release);
        get_window_from_casted_event(XCB_BUTTON_PRESS, xcb_button_press);
        get_window_from_casted_event(XCB_BUTTON_RELEASE, xcb_button_release);
        get_window_from_casted_event(XCB_MOTION_NOTIFY, xcb_motion_notify);
        get_window_from_casted_event(XCB_ENTER_NOTIFY, xcb_enter_notify);
        get_window_from_casted_event(XCB_LEAVE_NOTIFY, xcb_leave_notify);
        get_window_from_casted_event(XCB_FOCUS_IN, xcb_focus_in);
        get_window_from_casted_event(XCB_FOCUS_OUT, xcb_focus_out);
        get_window_from_casted_event__explicit_member(XCB_EXPOSE, xcb_expose, window);
        get_window_from_casted_event__explicit_member(
                XCB_VISIBILITY_NOTIFY, xcb_visibility_notify, window);
        get_window_from_casted_event__explicit_member(XCB_CREATE_NOTIFY, xcb_create_notify, window);
        get_window_from_casted_event(XCB_DESTROY_NOTIFY, xcb_destroy_notify);
        get_window_from_casted_event(XCB_UNMAP_NOTIFY, xcb_unmap_notify);
        get_window_from_casted_event(XCB_MAP_NOTIFY, xcb_map_notify);
        get_window_from_casted_event(XCB_REPARENT_NOTIFY, xcb_reparent_notify);
        get_window_from_casted_event(XCB_CONFIGURE_NOTIFY, xcb_configure_notify);
        get_window_from_casted_event__explicit_member(
                XCB_CONFIGURE_REQUEST, xcb_configure_request, window);
        get_window_from_casted_event(XCB_GRAVITY_NOTIFY, xcb_gravity_notify);
        get_window_from_casted_event__explicit_member(
                XCB_RESIZE_REQUEST, xcb_resize_request, window);
        get_window_from_casted_event(XCB_CIRCULATE_NOTIFY, xcb_circulate_notify);
        get_window_from_casted_event(XCB_CIRCULATE_REQUEST, xcb_circulate_request);
        get_window_from_casted_event__explicit_member(
                XCB_PROPERTY_NOTIFY, xcb_property_notify, window);
        get_window_from_casted_event__explicit_member(
                XCB_SELECTION_CLEAR, xcb_selection_clear, owner);
        get_window_from_casted_event__explicit_member(
                XCB_COLORMAP_NOTIFY, xcb_colormap_notify, window);
        get_window_from_casted_event__explicit_member(
                XCB_CLIENT_MESSAGE, xcb_client_message, window);
    }
    return 0;
}

static xcb_atom_t
intern_atom(xcb_connection_t *conn, const char *atom) {
    xcb_atom_t result = XCB_NONE;
    const xcb_intern_atom_cookie_t &cookie = xcb_intern_atom(conn, 0, strlen(atom), atom);
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, cookie, NULL);
    if (r)
        result = r->atom;
    free(r);
    return result;
}

struct CachedAtom {
    std::string name;
    xcb_atom_t atom;
    
    ~CachedAtom() {}
};

std::vector<CachedAtom *> cached_atoms;

xcb_atom_t
get_cached_atom(App *app, std::string name) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    for (auto cached_atom: cached_atoms) {
        if (cached_atom->name == name) {
            return cached_atom->atom;
        }
    }
    auto *cached_atom = new CachedAtom;
    cached_atom->name = name;
    cached_atom->atom = intern_atom(app->connection, name.c_str());
    cached_atoms.push_back(cached_atom);
    return cached_atom->atom;
}

void cleanup_cached_atoms() {
    for (auto cached_atom: cached_atoms) {
        delete cached_atom;
    }
    cached_atoms.clear();
    cached_atoms.shrink_to_fit();
}

void launch_command(std::string command) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (command.empty())
        return;
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "winbar: Could not fork\n");
        return;
    }
    
    if (pid == 0) {
        char *dir = getenv("HOME");
        if (dir) {
            int ret = chdir(dir);
            if (ret != 0) {
                fprintf(stderr, "winbar: failed to chdir to %s\n", dir);
            }
        }
        
        execlp("sh", "sh", "-c", command.c_str(), NULL);
        fprintf(stderr, "winbar: Failed to execute %s\n", command.c_str());
        
        _exit(1);
    } else {
        signal(SIGCHLD, SIG_IGN); // https://www.geeksforgeeks.org/zombie-processes-prevention/
    }
}

// amount: 0 to 100
static void
mod_color(ArgbColor *color, double amount) {
    double h; // hue
    double s; // saturation
    double p; // perceived brightness
    rgb2hsluv(color->r, color->g, color->b, &h, &s, &p);
    
    p = p + amount;
    
    if (p < 0)
        p = 0;
    else if (p > 100)
        p = 100;
    hsluv2rgb(h, s, p, &color->r, &color->g, &color->b);
}

void
darken(ArgbColor *color, double amount) {
    mod_color(color, -amount);
}

void
lighten(ArgbColor *color, double amount) {
    mod_color(color, amount);
}

ArgbColor
darken(ArgbColor color, double amount) {
    ArgbColor result = color;
    mod_color(&result, -amount);
    return result;
}

ArgbColor
lighten(ArgbColor color, double amount) {
    ArgbColor result = color;
    mod_color(&result, amount);
    return result;
}

void
load_icon_full_path(App *app, AppClient *client_entity, cairo_surface_t **surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (path.find("svg") != std::string::npos) {
        *surface = accelerated_surface(app, client_entity, target_size, target_size);
        paint_svg_to_surface(*surface, path, target_size);
    } else if (path.find("png") != std::string::npos) {
        *surface = accelerated_surface(app, client_entity, target_size, target_size);
        paint_png_to_surface(*surface, path, target_size);
    }
}

std::string
as_resource_path(std::string path) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    char *string = getenv("HOME");
    std::string home(string);
    home += "/.config/winbar/resources/" + path;
    return home;
}

bool paint_svg_to_surface(cairo_surface_t *surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    GFile *gfile = g_file_new_for_path(path.c_str());
    RsvgHandle *handle = rsvg_handle_new_from_gfile_sync(gfile, RSVG_HANDLE_FLAGS_NONE, NULL, NULL);
    
    // TODO: is this correct?
    if (handle == nullptr)
        return false;
    
    auto *temp_context = cairo_create(surface);
    cairo_save(temp_context);
    cairo_set_operator(temp_context, CAIRO_OPERATOR_CLEAR);
    cairo_paint(temp_context);
    cairo_restore(temp_context);
    
    cairo_save(temp_context);
    const RsvgRectangle viewport{0, 0, (double) target_size, (double) target_size};
    rsvg_handle_render_layer(handle, temp_context, NULL, &viewport, nullptr);
    cairo_restore(temp_context);
    cairo_destroy(temp_context);
    
    g_object_unref(gfile);
    g_object_unref(handle);
    
    return true;
}

bool paint_png_to_surface(cairo_surface_t *surface, std::string path, int target_size) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    auto *png_surface = cairo_image_surface_create_from_png(path.c_str());
    
    if (cairo_surface_status(png_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(png_surface);
        return false;
    }
    
    auto *temp_context = cairo_create(surface);
    int w = cairo_image_surface_get_width(png_surface);
    int h = cairo_image_surface_get_height(png_surface);
    
    cairo_save(temp_context);
    cairo_set_operator(temp_context, CAIRO_OPERATOR_CLEAR);
    cairo_paint(temp_context);
    cairo_restore(temp_context);
    
    cairo_save(temp_context);
    if (target_size != w) {
        double scale = ((double) target_size) / ((double) w);
        cairo_scale(temp_context, scale, scale);
    }
    cairo_set_source_surface(temp_context, png_surface, 0, 0);
    cairo_paint(temp_context);
    cairo_restore(temp_context);
    cairo_destroy(temp_context);
    cairo_surface_destroy(png_surface);
    
    return true;
}

bool
paint_surface_with_image(cairo_surface_t *surface, std::string path, int target_size, void (*upon_completion)(bool)) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    bool success = false;
    if (path.find(".svg") != std::string::npos) {
        success = paint_svg_to_surface(surface, path, target_size);
    } else if (path.find(".png") != std::string::npos) {
        success = paint_png_to_surface(surface, path, target_size);
    }
    if (upon_completion != nullptr) {
        upon_completion(success);
    }
    return success;
}

cairo_surface_t *
accelerated_surface(App *app, AppClient *client_entity, int w, int h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (client_entity && client_entity->cr == nullptr)
        return nullptr;
    
    cairo_surface_t *fast_surface = cairo_surface_create_similar_image(
            cairo_get_target(client_entity->cr), CAIRO_FORMAT_ARGB32, w, h);
    
    if (cairo_surface_status(fast_surface) != CAIRO_STATUS_SUCCESS)
        return nullptr;
    
    return fast_surface;
}

cairo_surface_t *
accelerated_surface_rgb(App *app, AppClient *client_entity, int w, int h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (client_entity && client_entity->cr == nullptr)
        return nullptr;
    
    cairo_surface_t *fast_surface = cairo_surface_create_similar_image(
            cairo_get_target(client_entity->cr), CAIRO_FORMAT_RGB24, w, h);
    
    if (cairo_surface_status(fast_surface) != CAIRO_STATUS_SUCCESS)
        return nullptr;
    
    return fast_surface;
}

void paint_surface_with_data(cairo_surface_t *surface, uint32_t *icon_data) {
    unsigned char *data = cairo_image_surface_get_data(surface);
}

bool there_is_a_compositor(App *app) {
    const char register_prop[] = "_NET_WM_CM_S";
    xcb_atom_t atom;
    
    char *buf = NULL;
    if (asprintf(&buf, "%s%d", register_prop, app->screen_number) < 0) {
        return false;
    }
    atom = get_cached_atom(app, buf);
    free(buf);
    
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(
            app->connection, xcb_get_selection_owner(app->connection, atom), NULL);
    
    if (reply && reply->owner != XCB_NONE) {
        // Another compositor already running
        free(reply);
        return true;
    }
    free(reply);
    return false;
}

static long last_check = 0;
static bool previous_result = false;

bool screen_has_transparency(App *app) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    long current_time = get_current_time_in_ms();
    if ((current_time - last_check) > 1000) { // Recheck every so often
        last_check = current_time;
        previous_result = there_is_a_compositor(app);
    }
    return previous_result;
}

ArgbColor correct_opaqueness(AppClient *client, ArgbColor color) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double alpha;
    if (screen_has_transparency(client->app)) {
        alpha = color.a;
    } else {
        alpha = 1;
    }
    return ArgbColor(color.r, color.g, color.g, alpha);
}


bool overlaps(double ax, double ay, double aw, double ah,
              double bx, double by, double bw, double bh) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (ax > (bx + bw) || bx > (ax + aw))
        return false;
    return !(ay > (by + bh) || by > (ay + ah));
}

double calculate_overlap_percentage(double ax, double ay, double aw, double ah,
                                    double bx, double by, double bw, double bh) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    double result = 0.0;
    //trivial cases
    if (!overlaps(ax, ay, aw, ah, bx, by, bw, bh)) return 0.0;
    if (ax == bx && ay == by && aw == bw && ah == bh) return 100.0;
    
    //# overlap between A and B
    double SA = aw * ah;
    double SB = bw * bh;
    double SI = MAX(0, MIN(ax + aw, bx + bw) - MAX(ax, bx)) *
                MAX(0, MIN(ay + ah, by + bh) - MAX(ay, by));
    double SU = SA + SB - SI;
    result = SI / SU; //ratio
    result *= 100.0; //percentage
    return result;
}

uint32_t argb_to_color(ArgbColor color) {
    unsigned int r = color.r * 255;
    unsigned int g = color.g * 255;
    unsigned int b = color.b * 255;
    unsigned int a = color.a * 255;
    r &= 0xFF;
    g &= 0xFF;
    b &= 0xFF;
    a &= 0xFF;
    return (a << 24) + (r << 16) + (g << 8) + (b);
}

bool is_light_theme(const ArgbColor &color) {
    double h; // hue
    double s; // saturation
    double p; // perceived brightness
    rgb2hsluv(color.r, color.g, color.b, &h, &s, &p);
    
    return p >= 50;
}

std::string
show_utf8_prop(Display *dpy, Window w, Atom p) {
    Atom da, incr, type;
    int di;
    unsigned long size, dul;
    unsigned char *prop_ret = NULL;

    /* Dummy call to get type and size. */
    XGetWindowProperty(dpy, w, p, 0, 0, False, AnyPropertyType,
                       &type, &di, &dul, &size, &prop_ret);
    XFree(prop_ret);

    incr = XInternAtom(dpy, "INCR", False);
    if (type == incr) {
        return "";
    }

    /* Read the data in one go. */
    //printf("Property size: %lu\n", size);

    XGetWindowProperty(dpy, w, p, 0, size, False, AnyPropertyType,
                       &da, &di, &dul, &dul, &prop_ret);
    //printf("%s", prop_ret);
    std::string t((char *) prop_ret, size);
    XFree(prop_ret);

    /* Signal the selection owner that we have successfully read the
     * data. */
    XDeleteProperty(dpy, w, p);
    return t;
}

std::string clipboard() {
    Display *dpy;
    Window owner, target_window, root;
    int screen;
    Atom sel, target_property, utf8;
    XEvent ev;
    XSelectionEvent *sev;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        return "";
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    sel = XInternAtom(dpy, "CLIPBOARD", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    owner = XGetSelectionOwner(dpy, sel);
    if (owner == None) {
        return "";
    }

    /* The selection owner will store the data in a property on this
     * window: */
    target_window = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);

    /* That's the property used by the owner. Note that it's completely
     * arbitrary. */
    target_property = XInternAtom(dpy, "PENGUIN", False);

    /* Request conversion to UTF-8. Not all owners will be able to
     * fulfill that request. */
    XConvertSelection(dpy, sel, utf8, target_property, target_window,
                      CurrentTime);

    for (;;) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case SelectionNotify:
                sev = (XSelectionEvent *) &ev.xselection;
                if (sev->property == None) {
                    printf("Conversion could not be performed.\n");
                    return "";
                } else {
                    return show_utf8_prop(dpy, target_window, target_property);
                }
                break;
        }
    }
}

void pango_layout_get_pixel_size_safe(PangoLayout *layout, int *w, int *h) {
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    *w = logical.width;
    *h = logical.height;
}

bool starts_with(const std::string &str, const std::string &prefix) {
    // Check if str is long enough to contain the prefix
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
}

void SpringAnimation::update(float deltaTime) {
    if (deltaTime < .00001)
        deltaTime = dt;
    if (deltaTime > dt)
        deltaTime = dt;
    // Calculate the force based on Hooke's Law: F = -kx
    float force = -stiffness * (position - target);
    // Calculate the damping force: Fd = -bv
    float dampingForce = -damping * velocity;
    // Sum the forces
    float acceleration = (force + dampingForce) / mass;
    // Integrate to get the velocity and position
    velocity += acceleration * deltaTime;
    position += velocity * deltaTime;
}

void SpringAnimation::setTarget(float newTarget) {
    target = newTarget;
}
