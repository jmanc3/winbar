#ifndef CONTAINER_HEADER
#define CONTAINER_HEADER

#include <X11/keysym.h>
#include <cairo.h>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <xcb/xcb_event.h>
#include <xcb/xproto.h>
#include <GL/glew.h>
#include <GL/glx.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>
#include <glm/glm.hpp>
#include <iostream>
#include <freetype/freetype.h>

#include "stb_rect_pack.h"

#define explicit dont_use_cxx_explicit

#include <xcb/xcb_keysyms.h>
#include <xcb/xkb.h>
#include <xcb/xcb_cursor.h>
#include <pango/pango-font.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pango/pango.h>
#include "easing.h"

#undef explicit

static int FILL_SPACE = -1;
static int USE_CHILD_SIZE = -2;
static int DYNAMIC = -3;

struct Bounds {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    
    Bounds();
    
    Bounds(double x, double y, double w, double h);
    
    Bounds(const Bounds &b);
    
    bool non_zero();
    
    void shrink(double amount);
    
    void grow(double amount);
};

enum layout_type {
    hbox = 1 << 0,
    
    vbox = 1 << 1,
    
    stack = 1 << 2,
    
    scrollpane = 1 << 3,
    scrollpane_inline_r = 1 << 4,// optional flag for scrollpane to inline the right thumb
    scrollpane_inline_b = 1 << 4,// optional flag for scrollpane to inline the bottom thumb
    scrollpane_r_always = 1 << 5,
    scrollpane_r_sometimes = 1 << 6,
    scrollpane_r_never = 1 << 7,
    scrollpane_b_always = 1 << 8,
    scrollpane_b_sometimes = 1 << 9,
    scrollpane_b_never = 1 << 10,
    
    transition = 1 << 11,
    
    newscroll = 1 << 12,
    
    editable_label = 1 << 13,
};

enum container_alignment {
    ALIGN_NONE = 0,
    ALIGN_GLOBAL_CENTER_HORIZONTALLY = 1 << 0,
    ALIGN_CENTER_HORIZONTALLY = 1 << 1,
    ALIGN_CENTER = 1 << 2,
    ALIGN_LEFT = 1 << 3,
    ALIGN_BOTTOM = 1 << 4,
    ALIGN_RIGHT = 1 << 5,
    ALIGN_TOP = 1 << 6,
};

struct UserData {
    virtual ~UserData() {};
    
    void destroy() {
        delete this;
    }
};

struct MouseState {
    // This variables is used so that we minimize the amount of containers we are
    // dealing with when having to figure out what container gets what event
    bool concerned = false;
    
    // If the mouse is ____STRICTLY____ inside this container this VERY EXACT
    // moment
    bool mouse_hovering = false;
    
    // TODO: this data structure for pressing wont be able to correctly handle the
    // case where one mouse button is pressed on the container then another and
    // then a release
    
    // If this container was pressed by the mouse and its currently being held
    // down irregardless if the mouse is still inside this container
    bool mouse_pressing = false;
    
    // If after a mouse_down there was motion irregardless of current mouse
    // position
    bool mouse_dragging = false;
    
    // This variable will only be valid if mouse_pressing is true
    // it will be set to the events e->detail so test it against
    // XCB_BUTTON_INDEX_[0-9] left XCB_BUTTON_INDEX_1 = 1,
    //
    // middle
    // XCB_BUTTON_INDEX_2 = 2,
    //
    // right
    // XCB_BUTTON_INDEX_3 = 3,
    int mouse_button_pressed = 0;
    
    void reset() {
        this->concerned = false;
        this->mouse_hovering = false;
        this->mouse_pressing = false;
        this->mouse_dragging = false;
        this->mouse_button_pressed = 0;
    }
};

struct Container;

struct ClientKeyboard {
    xcb_connection_t *conn = nullptr;
    uint8_t first_xkb_event;
    struct xkb_context *ctx = nullptr;
    
    struct xkb_keymap *keymap = nullptr;
    struct xkb_state *state = nullptr;
    int32_t device_id;
    int balance = 0; // always needs to be above 0
};

struct App;

struct ScreenInformation {
    ScreenInformation() {
    
    }
    
    bool is_primary;
    int x, y;
    int width_in_pixels;
    int width_in_millimeters;
    int height_in_pixels;
    int height_in_millimeters;
    int rotation;
    uint8_t status;
    float dpi_scale = 1; // can be fractional
    xcb_window_t root_window;
    
    ScreenInformation(const ScreenInformation &p1) {
        is_primary = p1.is_primary;
        x = p1.x;
        y = p1.y;
        width_in_pixels = p1.width_in_pixels;
        width_in_millimeters = p1.width_in_millimeters;
        height_in_pixels = p1.height_in_pixels;
        height_in_millimeters = p1.height_in_millimeters;
        rotation = p1.rotation;
        status = p1.status;
        dpi_scale = p1.dpi_scale;
        root_window = p1.root_window;
    }
};

struct PopupSettings {
    bool is_popup = false;
    bool wants_grab = true;
    bool transparent_mouse_grab = true;
    bool takes_input_focus = false;
    bool ignore_scroll = false;
    bool close_on_focus_out = true;
    int close_delay_in_ms = 0;
    std::string name;
};

struct AppClient;

struct Settings;

struct Timeout;

struct ClientAnimation {
    double start_value{};
    double *value = nullptr;
    std::weak_ptr<bool> lifetime;
    double length{};
    double target{};
    easingFunction easing = nullptr;
    long start_time{};
    bool relayout = false;
    
    bool done = false;
    
    void (*finished)(AppClient *client) = nullptr;
    
    double delay;
};

enum struct CommandStatus {
    NONE,
    UPDATE, // For commands that don't finish right away
    ERROR, // When command fails
    TIMEOUT, // When command times out
    FINISHED, // When command finishes
};

struct Subprocess {
    int inpipe[2]{};
    int outpipe[2]{};
    int timeout_fd = -1;
    pid_t pid;
    App *app = nullptr;
    
    std::string command;
    std::string output;
    std::string recent;
    CommandStatus status = CommandStatus::NONE; // UPDATE, FINISHED, ERROR, TIMEOUT
    void *user_data = nullptr;
    
    void (*function)(Subprocess *){};
    
    AppClient *client{};
    
    Subprocess(App *app, const std::string &command);
    
    void write(const std::string &message);
    
    void kill(bool warn);
};


struct RoundedRect {
    RoundedRect();
    
    void update_projection(const glm::mat4 &projection);
    
    void draw_rect(float x, float y, float w, float h, float r, float pad, float panel);
    
    // API to set color using RGB values (0 to 1)
    void set_color(float r, float g, float b);
    
    // API to set color using RGBA values (0 to 1)
    void set_color(float r, float g, float b, float a);
    
    // API to set color using four corner RGBA values (0 to 1)
    void set_color(glm::vec4 top_left_rgba, glm::vec4 top_right_rgba, glm::vec4 bottom_right_rgba,
                   glm::vec4 bottom_left_rgba);
    
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    GLuint shaderProgram;
    GLuint projectionUniform;
    glm::mat4 projection;
    GLuint radiusUniform;
    GLuint softnessUniform;
    GLuint rectUniform;
    GLuint padUniform;
    GLuint panelUniform;
    GLuint VAO, VBO;
    
    glm::vec4 color_top_left = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_top_right = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_bottom_right = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_bottom_left = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
};

static GLuint compileShader(const std::string &shaderCode, GLenum shaderType) {
    GLuint shader = glCreateShader(shaderType);
    const char *code = shaderCode.c_str();
    glShaderSource(shader, 1, &code, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "Shader compilation error:\n" << infoLog << std::endl;
    }
    
    return shader;
}

class ShapeRenderer {
public:
    float rot = 0;
    
    ShapeRenderer();
    
    void update_projection(const glm::mat4 &projection);
    
    // draw_rect takes optional parameters:
    //  - radius: if > 0, the rect corners are rounded.
    //  - strokeWidth: if 0, the rect is filled; if > 0, only a stroked outline is drawn with that width.
    void draw_rect(float x, float y, float w, float h, float radius = 0.0f, float strokeWidth = 0.0f);
    
    // API to set color using RGB values (0 to 1)
    void set_color(float r, float g, float b);
    
    // API to set color using RGBA values (0 to 1)
    void set_color(float r, float g, float b, float a);
    
    // API to set color using four corner RGBA values (0 to 1)
    void set_color(glm::vec4 top_left_rgba, glm::vec4 top_right_rgba, glm::vec4 bottom_right_rgba,
                   glm::vec4 bottom_left_rgba);

private:
    void initialize();

private:
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    GLuint shaderProgram;
    GLuint projectionUniform;
    GLuint radiusUniform;
    GLuint rectSizeUniform;
    GLuint strokeWidthUniform;  // New uniform for stroke width
    
    glm::mat4 projection;
    glm::mat4 model;
    GLuint VAO, VBO;
    
    // Colors for the four vertices.
    glm::vec4 color_top_left     = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_top_right    = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_bottom_right = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 color_bottom_left  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
};

struct InitialIcon {
    std::string class_name;
    unsigned char *data = nullptr;
    GdkPixbuf *pixbuf = nullptr;
    int width = 0;
    int height = 0;
    
    void destroy();
};

struct ImageData {
    unsigned char *data = nullptr;
    int w = 0;
    int h = 0;
    void *pixbuf = nullptr;
};

class ImmediateTexture {
public:
    explicit ImmediateTexture(const char *filename, int w = 0, int h = 0, bool keep_aspect_ratio = true);
    
    ImmediateTexture(InitialIcon *initial_icon);
    
    ImmediateTexture() {};
    
    ImageData get(const char *filename, int w = 0, int h = 0, bool keep_aspect_ratio = true);
    
    void create(unsigned char *pixels, int w, int h, bool keep_aspect_ratio = true, int gl_order = GL_RGBA);
    
    void bind() const;
    
    float width = 0;
    float height = 0;
    unsigned int textureID;
    
    void draw(float x, float y, float w = 0, float h = 0) const;
    
    ~ImmediateTexture();
};


namespace HBFeature {
    const hb_tag_t KernTag = HB_TAG('k', 'e', 'r', 'n'); // kerning operations
    const hb_tag_t LigaTag = HB_TAG('l', 'i', 'g', 'a'); // standard ligature substitution
    const hb_tag_t CligTag = HB_TAG('c', 'l', 'i', 'g'); // contextual ligature substitution
    
    static hb_feature_t LigatureOff = {LigaTag, 0, 0, std::numeric_limits<unsigned int>::max()};
    static hb_feature_t LigatureOn = {LigaTag, 1, 0, std::numeric_limits<unsigned int>::max()};
    static hb_feature_t KerningOff = {KernTag, 0, 0, std::numeric_limits<unsigned int>::max()};
    static hb_feature_t KerningOn = {KernTag, 1, 0, std::numeric_limits<unsigned int>::max()};
    static hb_feature_t CligOff = {CligTag, 0, 0, std::numeric_limits<unsigned int>::max()};
    static hb_feature_t CligOn = {CligTag, 1, 0, std::numeric_limits<unsigned int>::max()};
}

struct FreeFont {
    GLuint shader_program;
    GLuint VBO;
    GLuint texture_id;
    
    GLuint projection_uniform;
    GLint attribute_coord;
    GLint uniform_tex;
    GLint uniform_color;
    glm::mat4 projection;
    glm::vec4 color = glm::vec4(1, 1, 1, 1);
    
    FT_Library ft;
    FT_Face face;
    float atlas_w = 1024;
    float atlas_h = 1024;
    stbrp_context ctx;
    stbrp_node *nodes;
    
    hb_font_t *hb_font;
    hb_buffer_t *hb_buffer;
    std::vector<hb_feature_t> features;
    
    
    struct GlyphInfo {
        // The location on the atlas of the character.
        stbrp_rect location;
        char32_t codepoint = 0;
        // How much to advance the pen after laying down this character
        // Character width and height
        float bitmap_w = 0;
        float bitmap_h = 0;
        float bearing_x = 0;
        float bearing_y = 0;
        
        FT_Glyph_Metrics metrics;
        
        // Where to pull from the atlas
        float u1 = 0;
        float v1 = 0;
        float u2 = 0;
        float v2 = 0;
    };
    std::vector<GlyphInfo> loaded_glyphs;
    
    // Info for alignment
    std::u32string current_text;
    std::string current_text_raw;
    float full_text_w = 0;
    float full_text_h = 0;
    long largest_horiz_bearing_y = 0;
    std::vector<float> line_widths;
    
    bool bold = false;
    bool italic = false;
    bool needs_synth = false; // Meaning we didn't find a matching font with bold italic metrics and need freetype to do it
    
    ~FreeFont();
    
    // Do not know why we are doing this!
    int force_ucs2_charmap(FT_Face ftf);
    
    // Modify the constructor signature to allow style flags:
    FreeFont(int size, std::string font_name, bool bold = false, bool italic = false);
    
    void update_projection(const glm::mat4 &projection);
    
    void bind_needed_glyphs(const std::u32string &text);
    
    void generate_info_needed_for_alignment();
    
    void begin();
    
    void end();
    
    void set_text(std::string text);
    
    void draw_text(PangoAlignment align, float x, float y, float wrap = 0);
    
    void draw_text(float x, float y, float wrap = 0);
    
    // API to set color using RGB values (0 to 1)
    void set_color(float r, float g, float b);
    
    // API to set color using RGBA values (0 to 1)
    void set_color(float r, float g, float b, float a);
    
    std::string wrapped_text(std::string text, float wrap);
};

class OffscreenFrameBuffer {
public:
    GLuint fbo;
    GLuint texColorBuffer;
    GLuint rbo;
    GLuint shaderProgram;
    GLuint shaderProgramBlur;
    GLuint quadVAO, quadVBO;
    int width, height;
    
    OffscreenFrameBuffer(int width, int height);
    
    ~OffscreenFrameBuffer();
    
    void push();
    
    void pop(bool blur = false);
    
    void resize(int w, int h);

private:
    // Placeholder for actual shader source code
    const char *vertexShaderSource = R"glsl(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoords;
        
        out vec2 TexCoords;

        void main() {
            TexCoords = aTexCoords;
            gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
        }
    )glsl";
    
    const char *fragmentShaderSource = R"glsl(
        #version 330 core
        out vec4 FragColor;

        in vec2 TexCoords;
        uniform sampler2D screenTexture;

        void main() {
            vec4 srcColor = texture(screenTexture, TexCoords);
            FragColor = vec4(srcColor.rgb * srcColor.a, srcColor.a);
        }
    )glsl";
    
    const char *fragmentShaderSourceBlur = R"glsl(
        #version 330 core
        out vec4 FragColor;

        in vec2 TexCoords;
        uniform sampler2D screenTexture;

        const float FROST_INTENSITY = 0.1;
        
        vec3 tex(vec2 uv) {
            return pow(texture(screenTexture, uv).rgb, vec3(2.2));
        }
        
        vec2 hash22(vec2 uv) {
            vec3 p3 = fract(vec3(uv.xyx) * vec3(.1031, .1030, .0973));
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.xx + p3.yz) * p3.zy);
        }

        void main() {
            vec2 st = TexCoords;
            vec2 uv = vec2(TexCoords.x / 5, TexCoords.y / 5);
            float t = 0.0;
            
            float d = length(uv);
            
            vec2 noise = (hash22(st * 1000.) * 2. - 1.) * FROST_INTENSITY;
            vec2 tex_offset = vec2(t / 4., sin(t / 8.));
            vec3 color = tex(st + noise + tex_offset);
            
            vec3 srgb = pow(color, vec3(1. / 2.2));
            vec4 srcColor = mix(vec4(srgb, 1.0), texture(screenTexture, uv), 0.87);
            FragColor = vec4(srcColor.rgb * srcColor.a, srcColor.a);
        }
    )glsl";
//
//    const char *fragmentShaderSourceBlur = R"glsl(
//        #version 330 core
//        out vec4 FragColor;
//
//        in vec2 TexCoords;
//        uniform sampler2D screenTexture;
//
//        #define FLIP_IMAGE
//
//        float rand(vec2 uv) {
//            float a = dot(uv, vec2(92., 80.));
//            float b = dot(uv, vec2(41., 62.));
//
//            float x = sin(a) + cos(b) * 51.;
//            return fract(x);
//        }
//
//        void main() {
//            vec2 uv = TexCoords;
//            vec2 rnd = vec2(rand(uv), rand(uv));
//
//            uv += rnd * .05;
//            FragColor = texture(screenTexture, uv);
//        }
//    )glsl";
    
    void destroy();
    
    void create(int w, int h);
};

#include <atomic>
#include <functional>
#include <memory>

struct Sizes {
    float w;
    float h;
};

struct FontReference {
    std::string name;
    int size;
    int weight;
    bool italic;
    
    AppClient *creation_client = nullptr;
    std::weak_ptr<bool> creation_client_alive;
    
    // Will have both pango and FreeType for now
    FreeFont *font = nullptr;
    PangoLayout *layout = nullptr;
        
    void begin();
    Sizes begin(std::string text, float r, float g, float b, float a = 1);
    void set_color(float r, float g, float b, float a = 1);
    void set_text(std::string text);
    std::string wrapped_text(std::string text, int w);
    void draw_text(int x, int y, int param = 5);
    void draw_text(int align, int x, int y, int wrap);
    void draw_text_end(int x, int y, int param = 5);
    void end();
    Sizes sizes();
    
    ~FontReference() {
        if (font)
            delete font;
    }
};

struct FontManager {
    std::vector<FontReference *> fonts;
    
    FontReference *get(AppClient *client, int size, std::string font, bool bold = false, bool italic = false);
};

struct DrawContext {
    ShapeRenderer shape;
    
    FontManager *font_manager = new FontManager();
    
    OffscreenFrameBuffer *buffer = nullptr;
    
    RoundedRect round;
    
    ~DrawContext() {
        delete font_manager;
        delete buffer;
    }
};

struct AppClient {
    App *app = nullptr;
    
    void *user_data = nullptr;
    
    // Information about the screen the client is on like DPI
    ScreenInformation *screen_information = nullptr;
    
    xcb_window_t window;
    bool is_context_current = false;
    GLXContext context;
    bool gl_window_created = true;
    bool should_use_gl = false;
    GLXWindow gl_window;
    GLXDrawable gl_drawable; // Automatically freed when window is destroyed
    DrawContext *ctx;
    
    std::string name;
    
    Bounds *bounds;
    
    Container *root;
    
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
    
    bool auto_delete_root = true;
    bool on_close_is_unmap = false;
    
    int mouse_initial_x = -1;
    int mouse_initial_y = -1;
    int mouse_current_x = -1;
    int mouse_current_y = -1;
    int previous_x = -1;
    int previous_y = -1;
    bool left_mouse_down = false;

    bool inside = false;
    bool mapped = false;
    
    long creation_time;
    long last_repaint_time;
    long delta;
    
    // Variables to limit how often we handle motion notify events
    float motion_events_per_second = 120;
    int motion_event_x = 0;
    int motion_event_y = 0;
    Timeout *motion_event_timeout = nullptr;

    std::atomic<bool> refresh_already_queued = false;

    std::vector<ClientAnimation> animations;
    int animations_running = 0;
    float fps = 144;
    bool limit_fps = true;
    
    bool automatically_resize_on_dpi_change = false;
    
    // called after dpi_scale_factor and screen_information have been updated
    void (*on_dpi_change)(App *, AppClient *) = nullptr;
    
    // called when client moved to a different screen
    void (*on_screen_changed)(App *, AppClient *) = nullptr;
    
    // called when client moved to a different screen
    void (*on_screen_size_changed)(App *, AppClient *) = nullptr;
    
    void (*on_any_screen_change)(App *, AppClient *) = nullptr;
    
    float dpi() const {
        if (screen_information == nullptr) {
            return 1;
        }
        return screen_information->dpi_scale;
    }
    
    bool window_supports_transparency;
    cairo_t *cr = nullptr;
    xcb_colormap_t colormap;
    xcb_cursor_context_t *cursor_ctx;
    xcb_cursor_t cursor = -1;
    uint32_t cursor_type = -1;
    xcb_window_t drag_and_drop_source = -1;
    uint32_t drag_and_drop_version = 0;
    std::vector<std::string> drag_and_drop_formats;
    
    bool marked_to_close = false;
    
    ClientKeyboard *keyboard = nullptr;
    
    void (*when_closed)(AppClient *client) = nullptr;
    
    bool keeps_app_running = true;
    
    PopupSettings popup_info;
    
    AppClient *child_popup = nullptr;
    
    bool wants_popup_events = false;
    
    bool skip_taskbar = true;
    
    AppClient *create_popup(PopupSettings popup_settings, Settings client_settings);
    
    long previous_redraw_time = 0;
    bool just_changed_size = true;
    void draw_start();
    void draw_end(bool reset_input);
    void gl_clear();
    glm::mat4 projection;
    
    std::vector<Subprocess *> commands;
    
    Subprocess *command(const std::string &command, void (*function)(Subprocess *));
    
    Subprocess *command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *));
    
    Subprocess *command(const std::string &command, void (*function)(Subprocess *), void *user_data);
    
    Subprocess *command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *), void *user_data);
    
    ~AppClient();
};

struct ScrollContainer;
struct ScrollPaneSettings;

struct Container {
    // The parent of this container which must be set by the user whenever a
    // relationship is added
    Container *parent;
    
    // A user settable name that can be used for retrival
    std::string name;
    
    // List of this containers children;
    std::vector<Container *> children;
    
    // The way children are laid out
    int type = vbox;
    
    // A higher z_index will mean it will be rendered above everything else
    int z_index = 0;
    
    // Spacing between children when laying them out
    double spacing = 0;
    
    // Where you are placed inside the parent
    int alignment = 0;
    
    // These numbers are usually going to be negative
    // The underlying real scrolling offset along an axis
    double scroll_v_real = 0;
    double scroll_h_real = 0;
    
    std::shared_ptr<bool> lifetime = std::make_shared<bool>();
    double scroll_v_visual = 0;
    double scroll_h_visual = 0;
    
    // State of the mouse used by application.cpp to determine when to call this
    // containers when_* functions
    MouseState state;
    
    // User settable target bounds
    Bounds wanted_bounds;
    
    // User settable target padding for children of this container
    Bounds wanted_pad;
    
    // real_bounds is generated after calling layout on the root container and is
    // the bounds of this container
    Bounds real_bounds;
    
    // children_bounds is generated after calling layout on the root container and
    // is the bounds of the children since remember you can set a wanted_pad
    // amount
    Bounds children_bounds;
    
    // This variable can be set by layout parent to determine if it should be
    // rendered
    bool exists = true;
    
    // Variable meaning if we should layout the children whenever layout is called
    // on this container
    bool should_layout_children = true;
    
    // This doesn't actually do clipping on children to the parent containers
    // bounds when rendering, instead it tells us if we should call the render
    // function of non visible children containers
    bool clip_children = true;
    
    bool clip = false;
    
    bool interactable = true;
    
    bool draggable = true;
    
    // If set to true, after layout of children, will check if there was overflow, if so, will distribute one pixel at a time
    bool distribute_overflow_to_children = false;
    
    // Is set to true when the container is the active last interacted with
    bool active = false;
    
    // If we should call when_clicked if this container was dragged
    bool when_drag_end_is_click = true;
    
    // How many pixels does a container need to be moved before dragging starts
    int minimum_x_distance_to_move_before_drag_begins = 0;
    int minimum_y_distance_to_move_before_drag_begins = 0;
    
    // If the container should receive events through a single container above it
    // (children)
    bool receive_events_even_if_obstructed_by_one = false;
    
    // If the container should receive events through other containers above it
    // (children)
    bool receive_events_even_if_obstructed = false;
    
    // Do children get painted
    bool automatically_paint_children = true;
    
    void *user_data = nullptr;
    
    // Called when client needs to repaint itself
    void (*when_paint)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when the mouse enters the container for the first time
    void (*when_mouse_enters_container)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called every time the mouse moves and its inside the container unless if
    // its being dragged
    void (*when_mouse_motion)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when the mouse is no longer inside the container, or if
    // mouse_down happend, will be called when mouse_up happens
    void (*when_mouse_leaves_container)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once if left_mouse, middle_mouse, or right_mouse is pressed down
    // inside this container
    void (*when_mouse_down)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // TODO: is this really the behaviour we want????
    // Called once when the mouse_down is released regardless if the mouse is
    // actually inside the container it initially mouse_downed on
    void (*when_mouse_up)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when this container was mouse_downed and then mouse_upped regardless
    // of any motion the mouse did in between those two events
    void (*when_clicked)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when the containers status is changed
    void (*when_active_status_changed)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called when this container was scrolled on
    void (*when_scrolled)(AppClient *client,
                          cairo_t *cr,
                          Container *self,
                          int scroll_x,
                          int scroll_y) = nullptr;
    
    // Called when this container was scrolled on
    void (*when_fine_scrolled)(AppClient *client,
                               cairo_t *cr,
                               Container *self,
                               int scroll_x,
                               int scroll_y,
                               bool came_from_touchpad) = nullptr;
    
    // Called once when after mouse_downing a container, there was a mouse_motion
    // event
    void (*when_drag_start)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called everytime when after mouse_downing a container, there where mouse
    // motion events until a mouse_up
    void (*when_drag)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // Called once when after dragging a container the mouse_up happens
    void (*when_drag_end)(AppClient *client, cairo_t *cr, Container *self) = nullptr;
    
    // If this function is set, it'll be called to determine if the container is
    // pierced
    bool (*handles_pierced)(Container *container, int mouse_x, int mouse_y) = nullptr;
    
    void (*before_layout)(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                          double *target_h) = nullptr;
    
    // Gives you the opportunity to set wanted bounds before layout
    void (*pre_layout)(AppClient *client, Container *self, const Bounds &bounds) = nullptr;
    
    
    // When layout is called on this container and generate_event is true on that
    // call
    void (*when_layout)(AppClient *client, Container *self, const Bounds &bounds, double *target_w,
                        double *target_h) = nullptr;
    
    void (*when_key_event)(AppClient *client, cairo_t *cr, Container *self, bool is_string, xkb_keysym_t keysym,
                           char string[64],
                           uint16_t mods, xkb_key_direction direction) = nullptr;
    
    Container *child(int wanted_width, int wanted_height);
    
    Container *child(int type, int wanted_width, int wanted_height);
    
    Container();
    
    Container(layout_type type, double wanted_width, double wanted_height);
    
    Container(double wanted_width, double wanted_height);
    
    Container(const Container &c);
    
    bool skip_delete = false;
    
    virtual ~Container();
    
    ScrollContainer *scrollchild(const ScrollPaneSettings &scroll_pane_settings);
};

enum ScrollShow {
    SAlways,
    SWhenNeeded,
    SNever
};

class ScrollPaneSettings : public UserData {
public:
    ScrollPaneSettings(float scale);
    
    int right_width = 12;
    int right_arrow_height = 12;
    
    int bottom_height = 12;
    int bottom_arrow_width = 12;
    
    bool right_inline_track = false;
    bool bottom_inline_track = false;
    
    // 0 is always show, 1 is when needed, 2 is never show
    int right_show_amount = ScrollShow::SWhenNeeded;
    int bottom_show_amount = ScrollShow::SWhenNeeded;
    
    bool make_content = false;
    
    bool start_at_end = false;
    
    // paint functions
    bool paint_minimal = false;
};

struct ScrollContainer : public Container {
    Container *content = nullptr;
    Container *right = nullptr;
    Container *bottom = nullptr;
    long previous_time_scrolled = 0;
    long previous_delta_diff = -1;
    int scroll_count = 0;
    ScrollPaneSettings settings;
    double scrollbar_openess = 1;
    double scrollbar_visible = 1;
    Timeout *openess_delay_timeout = nullptr;
    
    explicit ScrollContainer(ScrollPaneSettings settings) : settings(std::move(settings)) {
        type = ::newscroll;
        wanted_bounds.w = FILL_SPACE;
        wanted_bounds.h = FILL_SPACE;
    }
    
    ~ScrollContainer() {
        delete content;
        delete right;
        delete bottom;
    }
};

struct EditableSelectableLabel : public Container {
    std::string font = "Segoe MDL2 Assets Mod";
    PangoWeight weight = PANGO_WEIGHT_NORMAL;
    std::string text;
    int size = 12;
    bool editable = false; // draws a cursor, and key presses work
    bool selectable = true;
    int selection_start = 0;
    int selection_end = 0;
    
    double previous_width = -1;
    
    AppClient *client = nullptr; // so we can create the pango text layout on layout
};

Bounds
scroll_bounds(Container *container);

void layout(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds);

Container *
container_by_name(std::string name, Container *root);

Container *
container_by_container(Container *target, Container *root);

bool overlaps(Bounds a, Bounds b);

bool bounds_contains(const Bounds &bounds, int x, int y);

double
reserved_width(Container *box);

double
reserved_height(Container *box);

double
true_height(Container *box);

double
true_width(Container *box);

double
actual_true_height(Container *box);

double
actual_true_width(Container *box);

void clamp_scroll(ScrollContainer *scrollpane);

#endif
