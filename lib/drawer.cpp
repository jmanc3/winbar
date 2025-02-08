//
// Created by jmanc3 on 2/3/25.
//

#include "drawer.h"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void draw_colored_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (client->gl_window_created && client->should_use_gl && client->ctx) {
        client->ctx->shape.set_color(color.r, color.g, color.b, color.a);
        client->ctx->shape.draw_rect(bounds.x, bounds.y, bounds.w, bounds.h);
    } else {
        set_rect(client->cr, bounds);
        set_argb(client->cr, color);
        cairo_fill(client->cr);
    }
}

void rounded_rect_new(cairo_t *cr, double corner_radius, double x, double y, double width, double height) {
    double radius = corner_radius;
    double degrees = M_PI / 180.0;
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

void draw_round_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, float round, float stroke_w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (client->gl_window_created && client->should_use_gl && client->ctx) {
        client->ctx->shape.set_color(color.r, color.g, color.b, color.a);
        client->ctx->shape.draw_rect(bounds.x, bounds.y, bounds.w, bounds.h, round, stroke_w);
    } else {
        set_argb(client->cr, color);
        rounded_rect_new(client->cr, round, bounds.x, bounds.y, bounds.w, bounds.h);
        if (stroke_w == 0) {
            cairo_fill(client->cr);
        } else {
            auto before = cairo_get_line_width(client->cr);
            cairo_set_line_width(client->cr, stroke_w);
            cairo_stroke(client->cr);
            cairo_set_line_width(client->cr, before);
        }
    }
}

void draw_margins_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, double width, double pad) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    auto b = bounds;
    draw_colored_rect(client, color,
                      Bounds((int) (b.x + pad), (int) (b.y + pad), (int) (b.w - pad * 2 - width), (int) (width)));
    draw_colored_rect(client, color, Bounds((int) (b.x + pad), (int) (b.y + pad + width), (int) (width),
                                            (int) (b.h - pad * 2 - width * 2)));
    draw_colored_rect(client, color,
                      Bounds((int) (b.x + b.w - width - pad), (int) (b.y + pad), (int) (width), (int) (b.h - pad * 2)));
    draw_colored_rect(client, color,
                      Bounds((int) (b.x + pad), (int) (b.y + b.h - width - pad), (int) (b.w - pad * 2 - width),
                             (width)));
}

void draw_gl_texture(AppClient *client, gl_surface *gl_surf, cairo_surface_t *surf,  int x, int y, int w, int h) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (!surf)
        return;
    ClientTexture *tex_target = nullptr;
    for (int i = gl_surf->textures.size() - 1; i >= 0; i--) {
        if (!gl_surf->textures[i]->lifetime.lock()) {
            delete gl_surf->textures[i];
            gl_surf->textures.erase(gl_surf->textures.begin() + i);
        } else {
            if (gl_surf->textures[i]->client == client) {
                tex_target = gl_surf->textures[i];
            }
        }
    }
    
    if (client->gl_window_created && client->should_use_gl && client->ctx) {
        if (!gl_surf->valid || (tex_target == nullptr)) {
            printf("copying from surface... - %d\n", gl_surf->textures.size());
            gl_surf->creation_client = client;
            
            auto tex = new ClientTexture;
            tex->texture = new ImmediateTexture;
            tex->client = client;
            tex->lifetime = client->lifetime;
            tex_target = tex;
            gl_surf->textures.push_back(tex);
           
            auto src_w = cairo_image_surface_get_width(surf);
            auto src_h = cairo_image_surface_get_height(surf);
            auto data = cairo_image_surface_get_data(surf);
            cairo_format_t format = cairo_image_surface_get_format(surf);
            GLenum gl_format;
            switch (format) {
                case CAIRO_FORMAT_ARGB32:
                    gl_format = GL_BGRA;  // Cairo stores ARGB32 as pre-multiplied BGRA
                    break;
                case CAIRO_FORMAT_RGB24:
                    gl_format = GL_BGR;
                    break;
                case CAIRO_FORMAT_A8:
                    gl_format = GL_ALPHA;
                    break;
                case CAIRO_FORMAT_RGB16_565:
                    gl_format = GL_RGB;
                    break;
                default:
                    gl_format = GL_RGBA;  // Fallback
                    break;
            }
            // Whatever opengl context is active at the time the following called is the only glx context in which it will be valid
            tex->texture->create(data, src_w, src_h, true, gl_format);
            gl_surf->valid = true;
        }
        
        tex_target->texture->draw(x, y, w, h);
    } else {
        cairo_set_source_surface(client->cr, surf, x, y);
        cairo_paint(client->cr);
    }
}

FontReference *draw_get_font(AppClient *client, int size, std::string font) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    DrawContext *pContext = client->ctx;
    FontManager *pManager = pContext->font_manager;
    auto f = pManager->get(client, size, font);
    return f;
}

FontText draw_text_begin(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text) {
    auto f = draw_get_font(client, size, font);
    auto [w, h] = f->begin(text, r, g, b, a);
    return {f, w, h};
}

void draw_text(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, Bounds bounds, int alignment, int x_off, int y_off) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    // if x_off -1 align center horiz, y_off vert
    auto f = draw_get_font(client, size, std::move(font));
    if (!f) return;
    
    f->begin();
    f->set_text(std::move(text));
    auto [w, h] = f->sizes();
    f->set_color(r, g, b, a);
    int x = bounds.x;
    if (x_off == -1) {
        x += bounds.w / 2 - w / 2;
    } else {
        x += x_off;
    }
    int y = bounds.y;
    if (y_off == -1) {
        y += bounds.h / 2 - h / 2;
    } else {
        y += y_off;
    }
    f->draw_text(x, y, alignment);
    f->end();
}

void draw_clip_begin(AppClient *client, const Bounds &b) {
    if (client->should_use_gl) {
        glEnable(GL_SCISSOR_TEST);
        // Reason for the y being what it is, is because open gl 0,0 is bottom left, but our drawing is top left 0,0
        glScissor(b.x, client->bounds->h - b.y - b.h, b.w, b.h);
    } else {
        cairo_save(client->cr);
        set_rect(client->cr, b);
        cairo_clip(client->cr);
    }
}

void draw_clip_end(AppClient *client) {
    if (client->should_use_gl) {
        glDisable(GL_SCISSOR_TEST);
//        glScissor(0, 0, client->bounds->w, client->bounds->h);
    } else {
        cairo_reset_clip(client->cr);
        cairo_restore(client->cr);
    }
}
    
void draw_operator(AppClient *client, int op) {
    if (!client->should_use_gl) {
        cairo_set_operator(client->cr, (cairo_operator_t) op);
    } else {
        if (op == (int) CAIRO_OPERATOR_OVER) {
            // Restore default blending for `CAIRO_OPERATOR_OVER`
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (op == (int) CAIRO_OPERATOR_SOURCE) {
            glBlendFunc(GL_ONE, GL_ZERO); // Directly replace destination with source
        }
    }
}
    
    














