#ifndef UTILITY_HEADER
#define UTILITY_HEADER

#include "application.h"

#include "defer.h"
#include <cairo-xcb.h>
#include <container.h>
#include <pango/pangocairo.h>
#include <pango/pango-layout.h>
#include <regex>
#include <utility>

#define EXPAND(v) v.r, v.g, v.b, v.a
#define MIDX(v) v->real_bounds.x + v->real_bounds.w / 2
#define MIDY(v) v->real_bounds.y + v->real_bounds.h / 2

#define execute_this_function_later(client, container, func_name) { \
    static bool not_called_from_timeout = true; \
    static bool not_created_timeout_yet = true; \
    if (not_called_from_timeout) { \
        if (not_created_timeout_yet) { \
            not_created_timeout_yet = false; \
            app_timeout_create(app, client, 1, [](App *, AppClient *client, Timeout *timeout, void *cont) { \
                if (container_by_container((Container *) cont, client->root)) {  \
                    not_called_from_timeout = false;  \
                    func_name(client, client->cr, (Container *) cont);  \
                    not_called_from_timeout = true; \
                    not_created_timeout_yet = true; \
                }  \
            }, container, "execute_later option_clicked");  \
        } \
        return; \
    } \
}


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
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
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
    
    bool operator==(const ArgbColor &rhs) const {
        return r == rhs.r &&
               g == rhs.g &&
               b == rhs.b &&
               a == rhs.a;
    }
    
    bool operator!=(const ArgbColor &rhs) const {
        return !(rhs == *this);
    }
};

struct SpringAnimation {
    // Parameters for the spring motion
    float position;
    float velocity = 0.0;
    float target;
    float damping;
    float stiffness;
    float mass;
    
    // Create a spring animation with initial position 0, target position 100
    // SpringAnimation spring(0.0f, 100.0f, 0.1f, 10.0f, 1.0f); // Adjusted for bounce
    
    // Simulate the spring animation
    float dt = 0.016f; // Assuming 60 updates per second
    
    // Constructor to initialize the parameters
    SpringAnimation(float pos = 0.0f, float tar = 0.0f, float damp = 29.5f, float stiff = 350.0f, float m = 1.0f)
            : position(pos), velocity(0.0f), target(tar), damping(damp), stiffness(stiff), mass(m) {}
    
    // Method to update the animation state
    void update(float deltaTime);
    
    // Method to set a new target position
    void setTarget(float newTarget);
};

class Label : public UserData {
public:
    std::string text;
    int size = -1;
    PangoWeight weight = PangoWeight::PANGO_WEIGHT_NORMAL;
    
    explicit Label(std::string text) {
        this->text = text;
    }
    
    ArgbColor color = ArgbColor(0, 0, 0, 0);
};

struct CachedFont {
    std::string name;
    int size;
    int used_count;
    bool italic = false;
    PangoWeight weight;
    PangoLayout *layout;
    cairo_t *cr; // Creator
    
    ~CachedFont() { g_object_unref(layout); }
};

struct LineParser {
    enum Token {
        UNSET, WHITESPACE, IDENT, COMMA, EQUAL, QUOTE, END_OF_LINE, NEWLINE
    };
    
    bool simple = false;
    std::string line;
    size_t current_index = -1;
    Token current_token = LineParser::Token::UNSET;
    size_t previous_index = -1;
    Token previous_token = LineParser::Token::UNSET;
    
    explicit LineParser(std::string line) : line(std::move(line)) {
        next();
    }
    
    void next() {
        if (current_token == LineParser::Token::END_OF_LINE)
            return;
        
        previous_index = current_index;
        previous_token = current_token;
        while (previous_token == current_token) {
            current_index++;
            if (current_index >= line.size()) {
                current_token = LineParser::Token::END_OF_LINE;
                current_index = line.size();
                break;
            } else {
                if (simple) {
                    if (line[current_index] == '\n' || line[current_index] == '\r') {
                        current_token = LineParser::Token::NEWLINE;
                    } else if (isspace(line[current_index])) {
                        current_token = LineParser::Token::WHITESPACE;
                    } else {
                        current_token = LineParser::Token::IDENT;
                    }
                } else {
                    if (line[current_index] == '=') {
                        current_token = LineParser::Token::EQUAL;
                        break; // We shouldn't join '+' with previous
                    } else if (line[current_index] == '\n' || line[current_index] == '\r') {
                        current_token = LineParser::Token::NEWLINE;
                    } else if (line[current_index] == ',') {
                        current_token = LineParser::Token::COMMA;
                        break; // We shouldn't join ',' with previous
                    } else if (line[current_index] == '"') {
                        current_token = LineParser::Token::QUOTE;
                        break; // We shouldn't join '"' with previous
                    } else if (isspace(line[current_index])) {
                        current_token = LineParser::Token::WHITESPACE;
                    } else {
                        current_token = LineParser::Token::IDENT;
                    }
                }
            }
        }
    }
    
    void back() {
        current_index = previous_index;
        current_token = previous_token;
    }
    
    Token peek() {
        next();
        defer(back()); // happens after return statement
        return current_token;
    }
    
    std::string text() {
        next();
        defer(back()); // happens after return statement
        if (current_index > previous_index)
            return line.substr(previous_index, current_index - previous_index);
        return "";
    }
    
    std::string until(Token type) {
        if (current_token == type) {
            return text();
        }
        std::string t;
        while (current_token != Token::END_OF_LINE) {
            if (current_token == type)
                break;
            t += text();
            next();
        }
        return t;
    }
};

extern std::vector<CachedFont *> cached_fonts;

void dye_surface(cairo_surface_t *surface, ArgbColor argb_color);

void dye_opacity(cairo_surface_t *surface, double amount, int thresh_hold);

long get_current_time_in_ms();

long get_current_time_in_seconds();

ArgbColor
lerp_argb(double scalar, ArgbColor start_color, ArgbColor target_color);

void set_argb(cairo_t *cr, ArgbColor color);

void set_rect(cairo_t *cr, Bounds bounds);

PangoLayout *
get_cached_pango_font(cairo_t *cr, std::string name, int pixel_height, PangoWeight weight, bool italic = false);

void cleanup_cached_fonts();

void remove_cached_fonts(cairo_t *cr);

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

bool is_light_theme(const ArgbColor &color);

std::string clipboard();

void
rounded_rect(AppClient *client, double corner_radius, double x, double y, double width, double height, ArgbColor color, float stroke_w = 0);

void pango_layout_get_pixel_size_safe(PangoLayout *layout, int *w, int *h);

bool
starts_with(const std::string &str, const std::string &prefix);

float random_float();

bool already_began(AppClient *client, double *value, double target);

#endif
