//
// Created by jmanc3 on 2/3/25.
//

#include "drawer.h"

void draw_colored_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds) {
    if (client->gl_window_created && client->should_use_gl && client->ctx) {
        client->ctx->shape.set_color(color.r, color.g, color.b, color.a);
        client->ctx->shape.draw_rect(bounds.x, bounds.y, bounds.w, bounds.h);
    } else {
        set_rect(client->cr, bounds);
        set_argb(client->cr, color);
        cairo_fill(client->cr);
    }
}

void draw_margins_rect(AppClient *client, const ArgbColor &color, const Bounds &bounds, double width, double pad) {
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
    DrawContext *pContext = client->ctx;
    FontManager *pManager = pContext->font_manager;
    auto f = pManager->get(client, size, font);
    return f;
}

void draw_text(AppClient *client, int size, std::string font, float r, float g, float b, float a, std::string text, Bounds bounds, int alignment, int x_off, int y_off) {
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
    
    
    