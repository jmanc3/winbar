
#include "container.h"
#include "application.h"
#include "../src/components.h"
#include "../src/config.h"

#include <cassert>
#include <cmath>
#include <iostream>

#include <glm/gtc/type_ptr.hpp>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <hb-ft.h>
#include <freetype/ftlcdfil.h>
#include <freetype/ftsynth.h>

#include FT_GLYPH_H  // This header provides functions like FT_GlyphSlot_Embolden.
#include <codecvt>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include "stb_image.h"

#define STB_RECT_PACK_IMPLEMENTATION

#include "stb_rect_pack.h"

// Sum of non filler child height and spacing
double
reserved_height(Container *box) {
    double space = 0;
    for (auto child: box->children) {
        if (!child->exists)
            continue;
        if (child->wanted_bounds.h == FILL_SPACE) {
            space += child->wanted_pad.y + child->wanted_pad.h;
        } else if (child->wanted_bounds.h == USE_CHILD_SIZE) {
            double child_height = 0;
            for (auto grandchild: child->children) {
                if (!grandchild->exists)
                    continue;
                if (grandchild->wanted_bounds.h > child_height)
                    child_height = grandchild->wanted_bounds.h;
            }
            space += child_height;
        } else {
            space += child->wanted_bounds.h;
        }
        space += box->spacing;
    }
    space -= box->spacing;// Remove spacing after last child
    return space;
}

// The reserved height plus the relevant padding
double
true_height(Container *box) {
    return reserved_height(box) + box->wanted_pad.y + box->wanted_pad.h - box->real_bounds.h;
}

double
actual_true_height(Container *box) {
    // set space to the distance between the lowest y of child and the hightest y+h of child
    double lowest_y = 0;
    double highest_y = 0;
    bool lowest_y_set = false;
    bool highest_y_set = false;
    for (auto child: box->children) {
        if (!child->exists)
            continue;
        if (!lowest_y_set || child->real_bounds.y < lowest_y) {
            lowest_y = child->real_bounds.y;
            lowest_y_set = true;
        }
        if (!highest_y_set || child->real_bounds.y + child->real_bounds.h > highest_y) {
            highest_y = child->real_bounds.y + std::max((child->real_bounds.h - 1), 0.0);
            highest_y_set = true;
        }
    }
    return (highest_y - lowest_y) + box->wanted_pad.y + box->wanted_pad.h;
}

double
actual_true_width(Container *box) {
    // set space to the distance between the lowest x of child and the hightest x+w of child
    double lowest_x = 0;
    double highest_x = 0;
    bool lowest_x_set = false;
    bool highest_x_set = false;
    for (auto child: box->children) {
        if (!child->exists)
            continue;
        if (!lowest_x_set || child->real_bounds.x < lowest_x) {
            lowest_x = child->real_bounds.x;
            lowest_x_set = true;
        }
        if (!highest_x_set || child->real_bounds.x + child->real_bounds.w > highest_x) {
            highest_x = child->real_bounds.x + std::max((child->real_bounds.w - 1), 0.0);
            highest_x_set = true;
        }
    }
    return (highest_x - lowest_x) + box->wanted_pad.x + box->wanted_pad.w;
}

// returns the height filler children should be
double
single_filler_height(Container *container) {
    double reserved_h = reserved_height(container);
    double available_h = container->children_bounds.h - reserved_h;
    double single_fill_size = 0;
    if (available_h > 0) {
        double filler_children_count = 0;
        for (auto child: container->children) {
            if (child->wanted_bounds.h == FILL_SPACE) {
                filler_children_count++;
            }
        }
        if (filler_children_count > 0) {
            single_fill_size = available_h / filler_children_count;
        }
    }
    return single_fill_size;
}

static void
modify_all(Container *container, double x_change, double y_change) {
    for (auto child: container->children) {
        modify_all(child, x_change, y_change);
    }
    
    container->real_bounds.x += x_change;
    container->real_bounds.y += y_change;
}

void layout_vbox(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds) {
    double fill_h = single_filler_height(container);
    
    double offset = 0;
    for (auto child: container->children) {
        if (child && child->exists) {
            double target_w = child->wanted_pad.x + child->wanted_pad.w;
            double target_h = child->wanted_pad.y + child->wanted_pad.h;
    
            if (child->before_layout)
                child->before_layout(client, child, bounds, &target_w, &target_h);
    
            if (child->wanted_bounds.w == FILL_SPACE) {
                target_w = container->children_bounds.w;
            } else if (child->wanted_bounds.w == USE_CHILD_SIZE) {
                target_w += reserved_width(child);
            } else {
                target_w += child->wanted_bounds.w;
            }
            if (child->wanted_bounds.h == FILL_SPACE) {
                target_h += fill_h;
            } else if (child->wanted_bounds.h == USE_CHILD_SIZE) {
                target_h += reserved_height(child);
            } else {
                target_h += child->wanted_bounds.h;
            }
            if (child->wanted_bounds.w == DYNAMIC || child->wanted_bounds.h == DYNAMIC) {
                child->when_layout(client, child, bounds, &target_w, &target_h);
            }
            
            // Keep within horizontal bounds
            if (container->scroll_h_real > 0)
                container->scroll_h_real = 0;
            if (container->scroll_h_real != 0) {
                double overhang = target_w - container->real_bounds.w;
                if (-container->scroll_h_real > overhang) {
                    container->scroll_h_real = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_h_real = 0;
                }
            }
            
            // Keep within vertical bounds
            if (container->scroll_v_real > 0)
                container->scroll_v_real = 0;
            if (container->scroll_v_real != 0) {
                double overhang = target_h - container->real_bounds.h;
                if (-container->scroll_v_real > overhang) {
                    container->scroll_v_real = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_v_real = 0;
                }
            }
            
            // Keep within horizontal bounds
            if (container->scroll_h_visual > 0)
                container->scroll_h_visual = 0;
            if (container->scroll_h_visual != 0) {
                double overhang = target_w - container->real_bounds.w;
                if (-container->scroll_h_visual > overhang) {
                    container->scroll_h_visual = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_h_visual = 0;
                }
            }
            
            // Keep within vertical bounds
            if (container->scroll_v_visual > 0)
                container->scroll_v_visual = 0;
            if (container->scroll_v_visual != 0) {
                double overhang = target_h - container->real_bounds.h;
                if (-container->scroll_v_visual > overhang) {
                    container->scroll_v_visual = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_v_visual = 0;
                }
            }
            
            layout(client, cr, child,
                   Bounds(container->children_bounds.x + container->scroll_h_visual,
                          container->children_bounds.y + offset + container->scroll_v_visual,
                          target_w,
                          target_h));
            
            offset += child->real_bounds.h + container->spacing;
        }
    }
    
    if (container->wanted_bounds.w == USE_CHILD_SIZE) {
        container->real_bounds.w = reserved_width(container);
    }
    if (container->wanted_bounds.h == USE_CHILD_SIZE) {
        container->real_bounds.h = reserved_height(container);
    }
    
    if (container->alignment & ALIGN_CENTER) {
        // Get height, divide by two, subtract that by parent y - h / 2
        double full_height = offset;
        double align_offset = bounds.h / 2 - full_height / 2;
        
        modify_all(container, 0, align_offset);
    }
}

// Sum of non filler child widths and spacing
double
reserved_width(Container *box) {
    double space = 0;
    for (auto child: box->children) {
        if (child) {
            if (!child->exists)
                continue;
            if (child->wanted_bounds.w == FILL_SPACE) {
                space += child->wanted_pad.x + child->wanted_pad.w;
            } else if (child->wanted_bounds.w == USE_CHILD_SIZE) {
                space += reserved_width(child);
            } else {
                space += child->wanted_bounds.w;
            }
            space += box->spacing;
        }
    }
    space -= box->spacing;// Remove spacing after last child
    return space;
}

// The reserved width plus the relevant padding
double
true_width(Container *box) {
    return reserved_width(box) + box->wanted_pad.x + box->wanted_pad.w - box->real_bounds.w;
}

// returns the width filler children should be
double
single_filler_width(Container *container, const Bounds bounds) {
    double reserved_w = reserved_width(container);
    double available_w = container->children_bounds.w - reserved_w;
    double single_fill_size = 0;
    if (available_w > 0) {
        double filler_children_count = 0;
        for (auto child: container->children) {
            if (child) {
                if (child->wanted_bounds.w == FILL_SPACE) {
                    filler_children_count++;
                }
            }
        }
        if (filler_children_count > 0) {
            single_fill_size = available_w / filler_children_count;
        }
    }
    return single_fill_size;
}

void layout_hbox(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds) {
    double fill_w = single_filler_width(container, bounds);
    
    double offset = 0;
    for (auto child: container->children) {
        if (child && child->exists) {
            double target_w = child->wanted_pad.x + child->wanted_pad.w;
            double target_h = child->wanted_pad.y + child->wanted_pad.h;
    
            if (child->before_layout)
                child->before_layout(client, child, bounds, &target_w, &target_h);
    
            if (child->wanted_bounds.w == FILL_SPACE) {
                target_w += fill_w;
            } else if (child->wanted_bounds.w == USE_CHILD_SIZE) {
                target_w += reserved_width(child);
            } else {
                target_w += child->wanted_bounds.w;
            }
            if (child->wanted_bounds.h == FILL_SPACE) {
                target_h = container->children_bounds.h;
            } else if (child->wanted_bounds.h == USE_CHILD_SIZE) {
                target_h += reserved_height(child);
            } else {
                target_h += child->wanted_bounds.h;
            }
            if (child->wanted_bounds.w == DYNAMIC || child->wanted_bounds.h == DYNAMIC) {
                child->when_layout(client, child, bounds, &target_w, &target_h);
            }
            
            // Keep within horizontal bounds
            if (container->scroll_h_real > 0)
                container->scroll_h_real = 0;
            if (container->scroll_h_real != 0) {
                double overhang = target_w - container->real_bounds.w;
                if (-container->scroll_h_real > overhang) {
                    container->scroll_h_real = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_h_real = 0;
                }
            }
            
            // Keep within vertical bounds
            if (container->scroll_v_real > 0)
                container->scroll_v_real = 0;
            if (container->scroll_v_real != 0) {
                double overhang = target_h - container->real_bounds.h;
                if (-container->scroll_v_real > overhang) {
                    container->scroll_v_real = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_v_real = 0;
                }
            }
            
            // Keep within horizontal bounds
            if (container->scroll_h_visual > 0)
                container->scroll_h_visual = 0;
            if (container->scroll_h_visual != 0) {
                double overhang = target_w - container->real_bounds.w;
                if (-container->scroll_h_visual > overhang) {
                    container->scroll_h_visual = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_h_visual = 0;
                }
            }
            
            // Keep within vertical bounds
            if (container->scroll_v_visual > 0)
                container->scroll_v_visual = 0;
            if (container->scroll_v_visual != 0) {
                double overhang = target_h - container->real_bounds.h;
                if (-container->scroll_v_visual > overhang) {
                    container->scroll_v_visual = -overhang;
                }
                if (overhang < 0) {
                    container->scroll_v_visual = 0;
                }
            }
            
            layout(client, cr, child,
                   Bounds(container->children_bounds.x + offset + container->scroll_h_visual,
                          container->children_bounds.y + container->scroll_v_visual,
                          target_w,
                          target_h));
            
            offset += child->real_bounds.w + container->spacing;
        }
    }
    
    if (container->wanted_bounds.w == USE_CHILD_SIZE) {
        container->real_bounds.w = reserved_width(container);
    }
    if (container->wanted_bounds.h == USE_CHILD_SIZE) {
        container->real_bounds.h = reserved_height(container);
    }
    
    if (container->alignment & ALIGN_CENTER) {
        for (auto c: container->children) {
            if (c->wanted_bounds.h != FILL_SPACE) {
                // Get height, divide by two, subtract that by parent y - h / 2
                double full_height = c->real_bounds.h;
                double align_offset = bounds.h / 2 - full_height / 2;
                modify_all(c, 0, align_offset);
            }
        }
    }
    if (container->alignment & ALIGN_RIGHT) {
        if (!container->children.empty()) {
            Container *first = container->children[0];
            Container *last = container->children[container->children.size() - 1];
            double total_children_w = (last->real_bounds.x + last->real_bounds.w) - first->real_bounds.x;
            
            for (auto c: container->children) {
                modify_all(c, (container->real_bounds.w - total_children_w), 0);
            }
        }
    }
    if (container->alignment & ALIGN_CENTER_HORIZONTALLY) {
        if (!container->children.empty()) {
            Bounds real_bounds = container->real_bounds;
            Container *first = container->children[0];
            Container *last = container->children[container->children.size() - 1];
            double total_children_w = (last->real_bounds.x + last->real_bounds.w) - first->real_bounds.x;
            
            for (auto c: container->children) {
                modify_all(c, (container->real_bounds.w - total_children_w) * .5, 0);
            }
            // guarantee first is greater than real_bounds.x
            if (first->real_bounds.x < real_bounds.x) {
                auto diff = real_bounds.x - first->real_bounds.x;
                for (auto c: container->children) {
                    modify_all(c, diff, 0);
                }
            }
        }
    }
    if (container->alignment & ALIGN_GLOBAL_CENTER_HORIZONTALLY) {
        if (!container->children.empty()) {
            Container *root = container;
            while (true) {
                if (root->parent == nullptr)
                    break;
                root = root->parent;
            }
            
            Bounds real_bounds = container->real_bounds;
            Container *first = container->children[0];
            Container *last = container->children[container->children.size() - 1];
            double total_children_w = (last->real_bounds.x + last->real_bounds.w) - first->real_bounds.x;
            double target_x = root->real_bounds.w / 2 - total_children_w / 2;
            double initial_x = first->real_bounds.x;
            for (auto c: container->children) {
                modify_all(c, target_x - initial_x, 0);
            }
            // guarantee first is greater than real_bounds.x
            if (first->real_bounds.x < real_bounds.x) {
                auto diff = real_bounds.x - first->real_bounds.x;
                for (auto c: container->children) {
                    modify_all(c, diff, 0);
                }
            }
            // guarantee last is less than real_bounds.x
            if (last->real_bounds.x + last->real_bounds.w > real_bounds.x + real_bounds.w) {
                auto diff = (real_bounds.x + real_bounds.w) - (last->real_bounds.x + last->real_bounds.w);
                for (auto c: container->children) {
                    modify_all(c, diff, 0);
                }
            }
            // guarantee first is greater than real_bounds.x
            if (first->real_bounds.x < real_bounds.x) {
                auto diff = real_bounds.x - first->real_bounds.x;
                for (auto c: container->children) {
                    modify_all(c, diff, 0);
                }
            }
        }
    }
}

void layout_stack(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds) {
    for (auto child: container->children) {
        layout(client, cr, child, bounds);
    }
}

// Expected container children:
// [required] right_box
// [required] bottom_box
// [required] content_area
void layout_scrollpane(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds) {
    assert(container->children.size() == 3 && !container->children[2]->children.empty());
    
    auto *r_bar = container->children[0];
    auto *b_bar = container->children[1];
    auto *content_area = container->children[2];
    auto *content = container->children[2]->children[0];
    
    int rv = content_area->scroll_v_real;
    int rh = content_area->scroll_h_real;
    content_area->scroll_v_real = 0;
    content_area->scroll_h_real = 0;
    int vv = content_area->scroll_v_visual;
    int vh = content_area->scroll_h_visual;
    content_area->scroll_v_visual = 0;
    content_area->scroll_h_visual = 0;
    layout(client, cr, content_area, Bounds(bounds.x, bounds.y, bounds.w, bounds.h));
    content_area->scroll_v_real = rv;
    content_area->scroll_h_real = rh;
    content_area->scroll_v_visual = vv;
    content_area->scroll_h_visual = vh;
    
    int options = container->type;
    
    double r_w = r_bar->wanted_bounds.w;
    double b_h = b_bar->wanted_bounds.h;
    double target_w = content->wanted_bounds.w;
    double target_h = content->wanted_bounds.h;
    
    if (options & scrollpane_r_always) {
    
    } else if (options & scrollpane_r_never) {
        r_w = 0;
    } else if (options & scrollpane_r_sometimes) {// sometimes
        if (options & scrollpane_inline_r) {
            if (target_h <= container->real_bounds.h) {
                r_w = 0;
            }
        } else {
            int future_b_h = b_h;
            
            if (options & scrollpane_b_always) {
            
            } else if (options & scrollpane_b_never) {
                future_b_h = 0;
            } else {// sometimes
                if (options & scrollpane_inline_b) {
                    if (target_w <= container->real_bounds.w) {
                        future_b_h = 0;
                    }
                } else {
                    if (target_w + r_w <= container->real_bounds.w && b_h != 0) {
                        future_b_h = 0;
                    }
                }
            }
            
            if (target_h + future_b_h <= container->real_bounds.h) {
                r_w = 0;
            }
        }
    }
    if (options & scrollpane_b_always) {
    
    } else if (options & scrollpane_b_never) {
        b_h = 0;
    } else if (options & scrollpane_b_sometimes) {// sometimes
        if (options & scrollpane_inline_b) {
            if (target_w <= container->real_bounds.w) {
                b_h = 0;
            }
        } else {
            if (target_w + r_w <= container->real_bounds.w && b_h != 0) {
                b_h = 0;
            }
        }
    }
    r_bar->exists = (r_w != 0);
    b_bar->exists = (b_h != 0);
    
    if (!(options & ::scrollpane_inline_r) && !(options & ::scrollpane_inline_b)) {
        layout(client, cr, content_area, Bounds(bounds.x, bounds.y, bounds.w - r_w, bounds.h - b_h));
    } else if (!(options & ::scrollpane_inline_r)) {
        layout(client, cr, content_area, Bounds(bounds.x, bounds.y, bounds.w - r_w, bounds.h));
    } else if (!(options & ::scrollpane_inline_b)) {
        layout(client, cr, content_area, Bounds(bounds.x, bounds.y, bounds.w, bounds.h - b_h));
    } else {
        layout(client, cr, content_area, Bounds(bounds.x, bounds.y, bounds.w, bounds.h));
    }
    
    if (r_bar->exists) {
        layout(client, cr, r_bar, Bounds(bounds.x + bounds.w - r_w, bounds.y, r_w, bounds.h - b_h));
    }
    if (b_bar->exists)
        layout(client, cr, b_bar, Bounds(bounds.x, bounds.y + bounds.h - b_h, bounds.w - r_w, b_h));
}

void clamp_scroll(ScrollContainer *scrollpane) {
    double true_height = actual_true_height(scrollpane->content);
    // add to true_height to account for bottom if it exists and not inline
    if (scrollpane->bottom && scrollpane->bottom->exists && !scrollpane->settings.bottom_inline_track)
        true_height += scrollpane->bottom->real_bounds.h;
    scrollpane->scroll_v_real = -std::max(0.0, std::min(-scrollpane->scroll_v_real,
                                                        true_height - scrollpane->real_bounds.h));
    scrollpane->scroll_v_visual = -std::max(0.0, std::min(-scrollpane->scroll_v_visual,
                                                          true_height - scrollpane->real_bounds.h));
    
    double true_width = actual_true_width(scrollpane->content);
    // add to true_width to account for right if it exists and not inline
    if (scrollpane->right && scrollpane->right->exists && !scrollpane->settings.right_inline_track)
        true_width += scrollpane->right->real_bounds.w;
    scrollpane->scroll_h_real = -std::max(0.0,
                                          std::min(-scrollpane->scroll_h_real, true_width - scrollpane->real_bounds.w));
    scrollpane->scroll_h_visual = -std::max(0.0,
                                            std::min(-scrollpane->scroll_h_visual,
                                                     true_width - scrollpane->real_bounds.w));
}

void layout_newscrollpane_content(AppClient *client, cairo_t *cr, ScrollContainer *scroll, const Bounds &bounds,
                                  bool right_scroll_bar_needed, bool bottom_scroll_bar_needed) {
    double w = scroll->content->wanted_bounds.w;
    double h = scroll->content->wanted_bounds.h;
    ScrollPaneSettings settings = scroll->settings;
    
    if (w == FILL_SPACE) {
        w = bounds.w - (settings.right_inline_track ? 0 : right_scroll_bar_needed ? settings.right_width : 0);
        if (settings.right_show_amount == 0) {
            w = bounds.w - settings.right_width;
        } else if (settings.right_show_amount == 2) {
            w = bounds.w;
        }
    } else {
        w = true_width(scroll->content);
    }
    if (h == FILL_SPACE) {
        h = bounds.h - (settings.bottom_inline_track ? 0 : bottom_scroll_bar_needed ? settings.bottom_height : 0);
        if (settings.bottom_show_amount == 0) {
            h = bounds.h - settings.bottom_height;
        } else if (settings.bottom_show_amount == 2) {
            h = bounds.h;
        }
    } else {
        h = true_height(scroll->content);
    }
    
    layout(client, cr, scroll->content,
           Bounds(bounds.x + scroll->scroll_h_visual, bounds.y + scroll->scroll_v_visual, w, h));
    
    scroll->content->real_bounds.h = actual_true_height(scroll->content);
    scroll->content->real_bounds.w = actual_true_width(scroll->content);
}

void layout_newscrollpane(AppClient *client, cairo_t *cr, ScrollContainer *scroll, const Bounds &bounds) {
    ScrollPaneSettings settings = scroll->settings;
    
    // layout the content as if the scroll bars were needed, and then if the size exceeds the bounds, layout again but with only the needed scroll bars
    layout_newscrollpane_content(client, cr, scroll, bounds, true, true);
    
    bool right_scroll_bar_needed = scroll->content->real_bounds.h > scroll->real_bounds.h;
    bool bottom_scroll_bar_needed = scroll->content->real_bounds.w > scroll->real_bounds.w;
    
    bool create_right_scrollbar = right_scroll_bar_needed;
    if (settings.right_show_amount == 2) {
        create_right_scrollbar = false;
    } else if (settings.right_show_amount == 0) {
        create_right_scrollbar = true;
    }
    bool create_bottom_scrollbar = bottom_scroll_bar_needed;
    if (settings.bottom_show_amount == 2) {
        create_bottom_scrollbar = false;
    } else if (settings.bottom_show_amount == 0) {
        create_bottom_scrollbar = true;
    }
    
    clamp_scroll(scroll);
    
    layout_newscrollpane_content(client, cr, scroll, bounds, right_scroll_bar_needed, bottom_scroll_bar_needed);
    
    if (create_right_scrollbar) {
        layout(client, cr, scroll->right,
               Bounds(bounds.x + bounds.w - settings.right_width, bounds.y, settings.right_width,
                      bounds.h));
    } else {
        scroll->right->exists = false;
    }
    if (create_bottom_scrollbar) {
        layout(client, cr, scroll->bottom,
               Bounds(bounds.x, bounds.y + bounds.h - settings.bottom_height, bounds.w - settings.right_width,
                      settings.bottom_height));
    } else {
        scroll->bottom->exists = false;
    }
}

void layout(AppClient *client, cairo_t *cr, Container *container, const Bounds &bounds) {
    container->real_bounds.x = bounds.x;
    container->real_bounds.y = bounds.y;
    
    bool fill_w = container->wanted_bounds.w == FILL_SPACE;
    bool fill_h = container->wanted_bounds.h == FILL_SPACE;
    container->real_bounds.w = (fill_w) ? bounds.w : container->wanted_bounds.w;
    container->real_bounds.h = (fill_h) ? bounds.h : container->wanted_bounds.h;
    
    container->children_bounds.x = container->real_bounds.x + container->wanted_pad.x;
    container->children_bounds.y = container->real_bounds.y + container->wanted_pad.y;
    container->children_bounds.w =
            container->real_bounds.w - container->wanted_pad.x - container->wanted_pad.w;
    container->children_bounds.h =
            container->real_bounds.h - container->wanted_pad.y - container->wanted_pad.h;
    
    if (container->type & layout_type::newscroll) {
        auto s = (ScrollContainer *) container;
        if (s->content->children.empty()) {
            s->content->exists = false;
            s->right->exists = false;
            s->bottom->exists = false;
            return;
        }
        s->content->exists = true;
        s->right->exists = true;
        s->bottom->exists = true;
    } else if (container->children.empty()) {
        return;
    }
    if (!container->should_layout_children)
        return;
    
    if (container->type & layout_type::hbox) {
        layout_hbox(client, cr, container, container->children_bounds);
    } else if (container->type & layout_type::vbox) {
        layout_vbox(client, cr, container, container->children_bounds);
    } else if (container->type & layout_type::stack) {
        layout_stack(client, cr, container, container->children_bounds);
    } else if (container->type & layout_type::scrollpane) {
        layout_scrollpane(client, cr, container, container->children_bounds);
    } else if (container->type & layout_type::transition) {
        for (int i = 0; i < container->children.size(); i++) {
            auto child = container->children[i];
            if (i == 0) {
                child->exists = true;
                layout(client, cr, child, bounds);
            } else {
                child->exists = false;
            }
        }
    } else if (container->type & layout_type::newscroll) {
        layout_newscrollpane(client, cr, (ScrollContainer *) container, container->children_bounds);
    } else if (container->type & layout_type::editable_label) {
    
    }
    
    // TODO: this only covers the first layer and not all of them
    // for sharp pixel boundaries
    for (auto child: container->children) {
        if (child) {
            child->real_bounds.x = round(child->real_bounds.x);
            child->real_bounds.y = round(child->real_bounds.y);
            child->real_bounds.w = round(child->real_bounds.w);
            child->real_bounds.h = round(child->real_bounds.h);
            
            child->children_bounds.x = round(child->children_bounds.x);
            child->children_bounds.y = round(child->children_bounds.y);
            child->children_bounds.w = round(child->children_bounds.w);
            child->children_bounds.h = round(child->children_bounds.h);
        }
    }
    
    if (container->distribute_overflow_to_children) {
        if (!container->children.empty()) {
            double overflow = 0;
            int i = 0;
            do {
                auto last = container->children[container->children.size() - 1];
                auto right = (last->real_bounds.x + last->real_bounds.w) - container->real_bounds.x;
                overflow = right - container->real_bounds.w;
                overflow--;
                container->children[i]->real_bounds.w -= 1;
                for (int l = i + 1; l < container->children.size(); l++)
                    container->children[l]->real_bounds.x -= 1;
                i++;
                if (i == container->children.size())
                    i = 0;
            } while (overflow >  0);
        }
    }

//    if (generate_event) {
//        if (container->when_layout) {
//            container->when_layout(container);
//        }
//    }
}

Container *
container_by_name(std::string name, Container *root) {
    if (!root) {
        return nullptr;
    }
    
    if (root->name == name) {
        return root;;
    }
    
    if (root->type == layout_type::newscroll) {
        auto scroll = (ScrollContainer *) root;
        auto possible = container_by_name(name, scroll->content);
        if (possible)
            return possible;
        possible = container_by_name(name, scroll->right);
        if (possible)
            return possible;
        possible = container_by_name(name, scroll->bottom);
        if (possible)
            return possible;
        for (auto child: scroll->children) {
            possible = container_by_name(name, child);
            if (possible)
                return possible;
        }
    } else {
        for (auto child: root->children) {
            auto possible = container_by_name(name, child);
            if (possible)
                return possible;
        }
    }
    
    
    return nullptr;
}

Container *
container_by_container(Container *target, Container *root) {
    if (root == target) {
        return root;;
    }
    
    if (root->type == layout_type::newscroll) {
        auto scroll = (ScrollContainer *) root;
        auto possible = container_by_container(target, scroll->content);
        if (possible)
            return possible;
        possible = container_by_container(target, scroll->right);
        if (possible)
            return possible;
        possible = container_by_container(target, scroll->bottom);
        if (possible)
            return possible;
        for (auto child: scroll->children) {
            possible = container_by_container(target, child);
            if (possible)
                return possible;
        }
    } else {
        for (auto child: root->children) {
            auto possible = container_by_container(target, child);
            if (possible)
                return possible;
        }
    }
    
    return nullptr;
}

bool overlaps(Bounds a, Bounds b) {
    if (a.x > (b.x + b.w) || b.x > (a.x + a.w))
        return false;
    
    return !(a.y > (b.y + b.h) || b.y > (a.y + a.h));
}

bool bounds_contains(const Bounds &bounds, int x, int y) {
    int bounds_x = std::round(bounds.x);
    int bounds_y = std::round(bounds.y);
    int bounds_w = std::round(bounds.w);
    int bounds_h = std::round(bounds.h);
    
    return x >= bounds_x && x <= bounds_x + bounds_w && y >= bounds_y && y <= bounds_y + bounds_h;
}

Bounds::Bounds(double x, double y, double w, double h) {
    this->x = x;
    this->y = y;
    this->w = w;
    this->h = h;
}

Bounds::Bounds(const Bounds &b) {
    x = b.x;
    y = b.y;
    w = b.w;
    h = b.h;
}

Bounds::Bounds() {
    x = 0;
    y = 0;
    w = 0;
    h = 0;
}

bool Bounds::non_zero() {
    return (x != 0) || (y != 0) || (w == 0) || (h == 0);
}

void Bounds::shrink(double amount) {
    this->x += amount;
    this->y += amount;
    this->w -= amount * 2;
    this->h -= amount * 2;
}

void Bounds::grow(double amount) {
    this->x -= amount;
    this->y -= amount;
    this->w += amount * 2;
    this->h += amount * 2;
}

Container *
Container::child(int wanted_width, int wanted_height) {
    Container *child_container = new Container(wanted_width, wanted_height);
    child_container->parent = this;
    this->children.push_back(child_container);
    return child_container;
}

Container *
Container::child(int type, int wanted_width, int wanted_height) {
    Container *child_container = new Container(wanted_width, wanted_height);
    child_container->type = type;
    child_container->parent = this;
    this->children.push_back(child_container);
    return child_container;
}

Container::Container(layout_type type, double wanted_width, double wanted_height) {
    this->type = type;
    wanted_bounds.w = wanted_width;
    wanted_bounds.h = wanted_height;
}

Container::Container(double wanted_width, double wanted_height) {
    wanted_bounds.w = wanted_width;
    wanted_bounds.h = wanted_height;
}

Container::Container(const Container &c) {
    parent = c.parent;
    name = c.name;
    
    for (auto child: c.children) {
        children.push_back(new Container(*child));
    }
    
    type = c.type;
    z_index = c.z_index;
    spacing = c.spacing;
    
    wanted_bounds = c.wanted_bounds;
    wanted_pad = c.wanted_pad;
    real_bounds = c.real_bounds;
    children_bounds = c.children_bounds;
    interactable = c.interactable;
    alignment = c.alignment;
    
    should_layout_children = c.should_layout_children;
    clip_children = c.clip_children;
    
    when_paint = c.when_paint;
    when_layout = c.when_layout;
    when_mouse_enters_container = c.when_mouse_enters_container;
    when_mouse_leaves_container = c.when_mouse_leaves_container;
    when_scrolled = c.when_scrolled;
    when_drag_start = c.when_drag_start;
    when_drag = c.when_drag;
    when_drag_end = c.when_drag_end;
    when_mouse_down = c.when_mouse_down;
    when_mouse_up = c.when_mouse_up;
    when_clicked = c.when_clicked;
    distribute_overflow_to_children = c.distribute_overflow_to_children;
}

Container::~Container() {
    for (auto child: children) {
        if (child->type == layout_type::newscroll) {
            delete (ScrollContainer *) child;
        } else {
            delete child;
        }
    }
//    auto data = static_cast<UserData *>(user_data);
    auto data = (UserData *) user_data;
    if (data != nullptr) {
        data->destroy();
    }
//    ((UserData *) user_data)->destroy();
}

Container::Container() {
    parent = nullptr;
    type = layout_type::hbox;
    z_index = 0;
    spacing = 0;
    should_layout_children = true;
    user_data = nullptr;
}

ScrollContainer *Container::scrollchild(const ScrollPaneSettings &scroll_pane_settings) {
    return make_newscrollpane_as_child(this, scroll_pane_settings);
}

AppClient *AppClient::create_popup(PopupSettings popup_settings, Settings client_settings) {
    // Close other top level popups
    if (this->popup_info.is_popup && this->wants_popup_events)
        this->wants_popup_events = false;
    
    AppClient *popup_client = nullptr;
    if (popup_settings.name.empty()) {
        popup_client = client_new(app, client_settings, this->name + "_popup");
    } else {
        popup_client = client_new(app, client_settings, popup_settings.name);
    }
    if (popup_client) {
        popup_client->popup_info = popup_settings;
    
        this->child_popup = popup_client;
        // TODO: is this the correct order (Why is teh comment on the function so useless!)
        xcb_icccm_set_wm_transient_for(app->connection, this->window, popup_client->window);
        popup_client->wants_popup_events = true;
        popup_client->popup_info.is_popup = true;
    }
    xcb_flush(app->connection);
    return popup_client;
}

Subprocess *AppClient::command(const std::string &command, void (*function)(Subprocess *)) {
    return command_with_client(this, command, 1000, function, nullptr);
}

Subprocess *AppClient::command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *)) {
    return command_with_client(this, command, timeout_in_ms, function, nullptr);
}

Subprocess *AppClient::command(const std::string &command, void (*function)(Subprocess *), void *user_data) {
    return command_with_client(this, command, 1000, function, user_data);
}

Subprocess *
AppClient::command(const std::string &command, int timeout_in_ms, void (*function)(Subprocess *), void *user_data) {
    return command_with_client(this, command, timeout_in_ms, function, user_data);
}

AppClient::~AppClient() {
    this->app->previously_closed_client = name;
    this->app->previously_closed_client_time = get_current_time_in_ms();
}

ScrollPaneSettings::ScrollPaneSettings(float scale) {
    this->right_width = this->right_width * scale;
    this->bottom_height = this->bottom_height * scale;
    this->right_arrow_height = this->right_arrow_height * scale;
    this->bottom_arrow_width = this->bottom_arrow_width * scale;
}

void Subprocess::kill(bool warn) {
    if (warn) {
        this->status = CommandStatus::ERROR;
        if (this->function)
            this->function(this);
    }
    for (int i = 0; i < app->descriptors_being_polled.size(); i++)
        if (app->descriptors_being_polled[i].file_descriptor == outpipe[0])
            app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
    if (this->timeout_fd != -1)
        for (int i = 0; i < app->descriptors_being_polled.size(); i++)
            if (app->descriptors_being_polled[i].file_descriptor == this->timeout_fd)
                app->descriptors_being_polled.erase(app->descriptors_being_polled.begin() + i);
    for (int i = 0; i < this->client->commands.size(); i++) {
        if (this->client->commands[i] == this) {
            this->client->commands.erase(this->client->commands.begin() + i);
            break;
        }
    }
    
    close(inpipe[1]);
    close(outpipe[0]);
    if (::kill(pid, SIGTERM) == -1) {
        std::cerr << "Error killing child process\n";
    }
    
    if (this->timeout_fd != -1) {
        close(this->timeout_fd);
    }
    
    delete this;
}

void Subprocess::write(const std::string &message) {
    std::string message_with_newline = message;
    if (message_with_newline[message_with_newline.size() - 1] != '\n')
        message_with_newline += '\n';
    
    if (::write(inpipe[1], message_with_newline.c_str(), message_with_newline.size()) == -1) {
        std::cerr << "Error writing to pipe\n";
    }
}

Subprocess::Subprocess(App *app, const std::string &command) {
    this->app = app;
    this->command = command;
    if (pipe(inpipe) == -1 || pipe(outpipe) == -1) {
        std::cerr << "Error creating pipes\n";
    }
    
    pid = fork();
    if (pid == -1) {
        std::cerr << "Error forking child process\n";
    } else if (pid == 0) {
        close(inpipe[1]);
        close(outpipe[0]);
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]);
        close(outpipe[1]);
        execl(command.c_str(), "", nullptr);
        std::cerr << "Error executing command\n";
    }
    
    close(inpipe[0]);
    close(outpipe[1]);
}

void AppClient::draw_start() {
    //printf("draw_start: %s\n", name.c_str());
    if (!is_context_current) {
        for (auto *item: app->clients)
            item->is_context_current = false;
        // TODO: this might be failing because we are doing it too fast when in opengl mode, because for some reason we never sleep?
        glXMakeContextCurrent(app->display, gl_drawable, gl_drawable, context);
        is_context_current = true;
    }
    if (just_changed_size) {
        glXMakeContextCurrent(app->display, gl_drawable, gl_drawable, context);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glViewport(0, 0, bounds->w, bounds->h);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glOrtho(0, bounds->w, bounds->h, 0, 1, -1); // Origin in lower-left corner
    }
}

void AppClient::draw_end(bool reset_input) {
    //printf("draw_end: %s\n", name.c_str());
    glXSwapBuffers(app->display, gl_drawable);
    just_changed_size = false;
    previous_redraw_time = get_current_time_in_ms();
}

void AppClient::gl_clear() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

RoundedRect::RoundedRect() {
    vertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

out vec4 fragColor;
out vec4 pos;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
    pos = gl_Position;
}
    )";
    // Fragment shader source
    fragmentShaderSource =
            R"(
#version 330 core

in vec4 fragColor; // Interpolated color from vertex shader
in vec4 pos;

out vec4 FragColor;
uniform float radius;
uniform float softness;
uniform vec4 rect;
uniform float pad;
uniform float panel;

float roundedBoxSDF(vec2 CenterPosition, vec2 Size, float Radius) {
    return length(max(abs(CenterPosition)-Size+Radius,0.0))-Radius;
}

float al(float pad) {
    // The pixel space scale of the rectangle.
    vec2 size = vec2(rect.z - pad * 2, rect.w - pad * 2);
    vec4 fragCoord = gl_FragCoord;
    
    // the pixel space location of the rectangle.
    vec2 location = vec2(rect.x + pad, rect.y + pad);

    // Calculate distance to edge.
    float distance = roundedBoxSDF(fragCoord.xy - location - (size/2.0f), size / 2.0f, radius);
    
    // Smooth the result (free antialiasing).
    float smoothedAlpha =  1.0f-smoothstep(0.0f, softness*2.0f,distance);
 
    return smoothedAlpha;
}

void main()
{
    float smoothedAlpha = al(0);
    float inter = 1 - al(pad);
    vec4 q = fragColor;
    float whole = fragColor.a * smoothedAlpha;
    float frame = fragColor.a * smoothedAlpha * inter;
    q.a = mix(whole, frame, 1-panel);
    
    FragColor = q;
}

)";
    
    // Compile shaders, link program, and create vertex buffers here
    // ... (This part depends on your setup)
    
    // Compile shaders and create shader program
    GLuint vertexShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    // Get uniform location for projection matrix
    projectionUniform = glGetUniformLocation(shaderProgram, "projection");
    radiusUniform = glGetUniformLocation(shaderProgram, "radius");
    softnessUniform = glGetUniformLocation(shaderProgram, "softness");
    rectUniform = glGetUniformLocation(shaderProgram, "rect");
    padUniform = glGetUniformLocation(shaderProgram, "pad");
    panelUniform = glGetUniformLocation(shaderProgram, "panel");
    
    // Set up vertex data and configure vertex attributes
    float quadVertices[] = {
            // Position            // Color
            -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, // Bottom-left vertex with red color
            1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, // Bottom-right vertex with green color
            1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, // Top-right vertex with blue color
            -1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f  // Top-left vertex with yellow color
    };
    
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *) 0);
    glEnableVertexAttribArray(0);
    
    // Color attribute
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *) (2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // Unbind VBO and VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void RoundedRect::update_projection(const glm::mat4 &projection) {
    this->projection = projection;
}

void RoundedRect::draw_rect(float x, float y, float w, float h, float r, float pad, float panel) {
    glUseProgram(shaderProgram);
    glUniform4f(rectUniform, x, y, w, h);
    
    glUniformMatrix4fv(projectionUniform, 1, GL_FALSE, glm::value_ptr(projection));
    
    glUniform1f(radiusUniform, r);
//    glUniform1f(softnessUniform, r == 0 ? 0.0f : 1.0f);
    glUniform1f(softnessUniform, r == 0 ? 0.0f : 0.5f);
    glUniform1f(padUniform, pad);
    glUniform1f(panelUniform, panel);
    
    glBindVertexArray(VAO);
    float vertices[] = {
            x, y + h, color_bottom_left.r, color_bottom_left.g, color_bottom_left.b, color_bottom_left.a,
            x + w, y + h, color_bottom_right.r, color_bottom_right.g, color_bottom_right.b, color_bottom_right.a,
            x + w, y, color_top_right.r, color_top_right.g, color_top_right.b, color_top_right.a,
            x, y, color_top_left.r, color_top_left.g, color_top_left.b, color_top_left.a,
    };
    
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    glBindVertexArray(0);
    glUseProgram(0);
}

void RoundedRect::set_color(float r, float g, float b) {
    set_color(r, g, b, 1.0f);
}

void RoundedRect::set_color(float r, float g, float b, float a) {
    auto color = glm::vec4(r, g, b, a);
    set_color(color, color, color, color);
}

void RoundedRect::set_color(glm::vec4 top_left_rgba, glm::vec4 top_right_rgba, glm::vec4 bottom_right_rgba,
                            glm::vec4 bottom_left_rgba) {
    color_top_left = top_left_rgba;
    color_top_right = top_right_rgba;
    color_bottom_right = bottom_right_rgba;
    color_bottom_left = bottom_left_rgba;
}

ShapeRenderer::ShapeRenderer() {
    // Vertex shader: passes along position, color, and UV coordinates.
    vertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

out vec4 fragColor;
out vec2 fragUV;

uniform mat4 projection;

void main()
{
    vec4 pos = vec4(inPosition, 0.0, 1.0);
    gl_Position = projection * pos;
    fragColor = inColor;
    fragUV = inUV;
}
    )";
    
    /*
       Fragment shader: Uses a rounded rectangle signed distance function (SDF).
       It supports two modes:
         1) Filled shape (strokeWidth == 0): pixels inside the shape are drawn.
         2) Stroke mode (strokeWidth > 0): only a band around the SDF=0 boundary is drawn.
       
       The SDF is computed by first centering the fragment coordinate and then
       comparing with half the rectangle size (minus the rounding radius).
    */
    fragmentShaderSource = R"(
#version 330 core

in vec4 fragColor;
in vec2 fragUV;

uniform float radius;       // Corner radius (in rectangle space)
uniform vec2 rectSize;      // The rectangle's width and height
uniform float strokeWidth;  // Stroke width: 0 = filled, >0 = stroked outline

out vec4 FragColor;

void main()
{
    // Compute the fragment's coordinate in rectangle space.
    // p will be in [0, rectSize].
    vec2 p = fragUV * rectSize;

    // Compute the center of the rectangle.
    vec2 center = rectSize * 0.5;

    // The half-size of the rectangle.
    vec2 halfSize = rectSize * 0.5;

    // Compute the "corner" for the SDF:
    // This is the amount by which the half-size is inset by the radius.
    vec2 inner = halfSize - vec2(radius);

    // Shift p so that the rectangle is centered at (0,0)
    vec2 pos = p - center;

    // Compute the distance to the edge of the box with rounded corners.
    // For a rounded rectangle SDF, we first take the absolute value.
    vec2 d = abs(pos) - inner;
    // d.x or d.y may be negative if inside the inner box.
    float outsideDistance = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - radius;

    // The signed distance from the fragment to the rounded rect boundary.
    float sd = outsideDistance;

    // Anti-aliasing factor based on the derivative.
    float aa = fwidth(sd);

    float alpha = 0.0;
    if(strokeWidth > 0.0)
    {
        // Stroke mode: we want a band where sd is between -strokeWidth/2 and +strokeWidth/2.
        float halfStroke = strokeWidth * 0.5;
        // smoothstep creates an anti-aliased band at both inner and outer boundaries.
        float innerEdge = smoothstep(-halfStroke - aa, -halfStroke, sd);
        float outerEdge = smoothstep(halfStroke, halfStroke + aa, sd);
        alpha = innerEdge - outerEdge;
    }
    else
    {
        // Filled mode: fill if inside the shape (sd < 0) with anti-aliasing.
        alpha = 1.0 - smoothstep(0.0, aa, sd);
    }

    // Multiply the incoming color's alpha by the computed alpha.
    FragColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
    )";
    
    initialize();
}

void ShapeRenderer::update_projection(const glm::mat4 &projection) {
    this->projection = projection;
}

//
// draw_rect() now takes two extra optional parameters:
//  - radius: if > 0, rounds the corners.
//  - strokeWidth: if > 0, draws only a stroked outline of that width; if 0, fills the shape.
//
void ShapeRenderer::draw_rect(float x, float y, float w, float h, float radius, float strokeWidth) {
    glUseProgram(shaderProgram);
    glDisable(GL_CULL_FACE);
    
    // Set the projection uniform.
    glUniformMatrix4fv(projectionUniform, 1, GL_FALSE, glm::value_ptr(projection));
    
    // Set the uniforms for rounded corners and stroke.
    glUniform1f(radiusUniform, radius);
    glUniform2f(rectSizeUniform, w, h);
    glUniform1f(strokeWidthUniform, strokeWidth);
    
    glBindVertexArray(VAO);
    
    // Supply 4 vertices with 8 floats each:
    // Layout per vertex: position (2), color (4), uv (2)
    // (x,y) is the bottom-left corner.
    // Vertex order: bottom-left, bottom-right, top-right, top-left.
    float vertices[] = {
            // Position       // Color (RGBA)                                 // UV
            x,    y,        color_bottom_left.r, color_bottom_left.g, color_bottom_left.b, color_bottom_left.a,   0.0f, 0.0f,
            x+w,  y,        color_bottom_right.r, color_bottom_right.g, color_bottom_right.b, color_bottom_right.a,  1.0f, 0.0f,
            x+w,  y+h,      color_top_right.r, color_top_right.g, color_top_right.b, color_top_right.a,        1.0f, 1.0f,
            x,    y+h,      color_top_left.r, color_top_left.g, color_top_left.b, color_top_left.a,           0.0f, 1.0f,
    };
    
    // Update VBO data.
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Draw the rectangle as a triangle fan.
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    glBindVertexArray(0);
    glUseProgram(0);
}

void ShapeRenderer::set_color(float r, float g, float b) {
    set_color(r, g, b, 1.0f);
}

void ShapeRenderer::set_color(float r, float g, float b, float a) {
    auto color = glm::vec4(r, g, b, a);
    set_color(color, color, color, color);
}

void ShapeRenderer::set_color(glm::vec4 top_left_rgba, glm::vec4 top_right_rgba, glm::vec4 bottom_right_rgba,
                              glm::vec4 bottom_left_rgba) {
    color_top_left = top_left_rgba;
    color_top_right = top_right_rgba;
    color_bottom_right = bottom_right_rgba;
    color_bottom_left = bottom_left_rgba;
}

//
// The initialize() function compiles the shaders, links the program, and sets up the VAO/VBO.
// We now allocate enough space for 4 vertices, each with 8 floats.
//
void ShapeRenderer::initialize() {
    // Compile shaders and create shader program.
    GLuint vertexShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    // (Error checking omitted for brevity)
    
    // Get uniform locations.
    projectionUniform = glGetUniformLocation(shaderProgram, "projection");
    radiusUniform = glGetUniformLocation(shaderProgram, "radius");
    rectSizeUniform = glGetUniformLocation(shaderProgram, "rectSize");
    strokeWidthUniform = glGetUniformLocation(shaderProgram, "strokeWidth");
    
    // Set up vertex data and configure vertex attributes.
    // Reserve space for 4 vertices * 8 floats per vertex.
    float quadVertices[4 * 8] = { 0.0f };
    
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_DYNAMIC_DRAW);
    
    // Position attribute: location 0, 2 floats.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Color attribute: location 1, 4 floats (starting after 2 floats).
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    // UV attribute: location 2, 2 floats (starting after 2+4 floats).
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    // Unbind.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

ImmediateTexture::ImmediateTexture(const char *filename, int w, int h, bool keep_aspect_ratio) {
    if (strstr(filename, ".png")) {
        // TODO: resize after stb loads based of w and h set
        int width, height, nrChannels;
        unsigned char *data = stbi_load(filename, &width, &height, &nrChannels, 4);
        assert(data);
        
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        stbi_image_free(data);
        this->width = width;
        this->height = height;
    } else if (strstr(filename, ".svg")) {
        GdkPixbuf *pixbuf;
        if (w <= 0 || h <= 0) {
            auto handle = rsvg_handle_new_from_file(filename, nullptr);
            assert(handle);
            pixbuf = rsvg_handle_get_pixbuf(handle);
        } else {
            pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, w, h, keep_aspect_ratio, nullptr);
        }
        assert(pixbuf);
        
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);
        unsigned char *pixels = gdk_pixbuf_get_pixels(pixbuf);
        
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        g_object_unref(pixbuf);
        if (w <= 0 || h <= 0) {
            this->width = width;
            this->height = height;
        } else {
            this->width = w;
            this->height = h;
        }
    } else {
        return;
    }
    
    // Unbind the texture
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ImmediateTexture::create(unsigned char *pixels, int w, int h, bool keep_aspect_ratio, int gl_order) {
    this->width = w;
    this->height = h;
    
    // TODO: resize after stb loads based of w and h set
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    // Use trilinear filtering (mipmaps + linear filtering)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);

//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, gl_order, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    // Unbind the texture
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ImmediateTexture::bind() const {
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void ImmediateTexture::draw(float x, float y, float w, float h) const {
    glEnable(GL_TEXTURE_2D);
    bind();
    
    x = std::round(x);
    y = std::round(y);
    
    /* Draw quad with texture */
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x, y);
    glTexCoord2f(1.0f, 0.0f);
    if (w == 0)
        glVertex2f(x + (float) width, y);
    else
        glVertex2f(x + w, y);
    glTexCoord2f(1.0f, 1.0f);
    if (w == 0 || h == 0)
        glVertex2f(x + (float) width, y + (float) height);
    else
        glVertex2f(x + w, y + h);
    glTexCoord2f(0.0f, 1.0f);
    if (h == 0)
        glVertex2f(x, y + (float) height);
    else
        glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

ImmediateTexture::~ImmediateTexture() {
    glDeleteTextures(1, &textureID);
}

ImageData ImmediateTexture::get(const char *filename, int w, int h, bool keep_aspect_ratio) {
    auto i = ImageData();
    
    if (strstr(filename, ".png")) {
        // TODO: resize after stb loads based of w and h set
        int nrChannels;
        unsigned char *data = stbi_load(filename, &i.w, &i.h, &nrChannels, 4);
        assert(data);
        i.data = data;
//        stbi_image_free(data);
    } else if (strstr(filename, ".svg")) {
        GdkPixbuf *pixbuf;
        if (w <= 0 || h <= 0) {
            auto handle = rsvg_handle_new_from_file(filename, nullptr);
            assert(handle);
            pixbuf = rsvg_handle_get_pixbuf(handle);
        } else {
            pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, w, h, keep_aspect_ratio, nullptr);
        }
        assert(pixbuf);
        
        i.w = gdk_pixbuf_get_width(pixbuf);
        i.h = gdk_pixbuf_get_height(pixbuf);
        i.data = gdk_pixbuf_get_pixels(pixbuf);
        i.pixbuf = pixbuf;
    }
    
    return i;
}

ImmediateTexture::ImmediateTexture(InitialIcon *initial_icon) {
    create(initial_icon->data, initial_icon->width, initial_icon->height);
}


FreeFont::~FreeFont() {
    glDeleteProgram(shader_program);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &texture_id);
    
    // Release FreeType resources
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    
    hb_buffer_destroy(hb_buffer);
    hb_font_destroy(hb_font);
    
    // Clean up other dynamically allocated memory
    delete[] nodes; // Assuming nodes is allocated using new[]
}

int FreeFont::force_ucs2_charmap(FT_Face ftf) {
    for (int i = 0; i < ftf->num_charmaps; i++) {
        if (((ftf->charmaps[i]->platform_id == 0)
             && (ftf->charmaps[i]->encoding_id == 3))
            || ((ftf->charmaps[i]->platform_id == 3)
                && (ftf->charmaps[i]->encoding_id == 1))) {
            return FT_Set_Charmap(ftf, ftf->charmaps[i]);
        }
    }
    return -1;
}
FreeFont::FreeFont(int size, std::string font_name, bool bold, bool italic) {
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Could not init freetype library\n");
        return;
    }
    this->bold = bold;
    this->italic = italic;
    
    // Build the pattern with requested style:
    FcPattern *pat = FcNameParse((const FcChar8 *) font_name.c_str());
    // If italic is requested, ask for an italic face; otherwise, use roman.
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    // If bold is requested, ask for a bold weight; otherwise, normal.
    FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_NORMAL);
    
    FcConfig *fcConfig = FcConfigGetCurrent();
    FcFontSet *fs = FcFontList(fcConfig, pat, nullptr);
    
    bool found = false;
    if (fs) {
        for (int i = 0; i < fs->nfont; ++i) {
            FcPattern *font = fs->fonts[i];
            FcChar8 *fontName;
            if (FcPatternGetString(font, FC_FILE, 0, &fontName) == FcResultMatch) {
                found = true;
                if (FT_New_Face(ft, (char *) fontName, 0, &face)) {
                    fprintf(stderr, "Could not open font %s\n", (char *) fontName);
                    return;
                } else {
                    break;
                }
            }
        }
        FcFontSetDestroy(fs);
    }
    
    // If a face with the desired style was not found, fall back to the regular face
    if (!found) {
        // Clean up the previous pattern
        FcPatternDestroy(pat);
        // Rebuild the pattern without style modifications:
        pat = FcNameParse((const FcChar8 *) font_name.c_str());
        FcPatternAddInteger(pat, FC_SLANT, FC_SLANT_ROMAN);
        FcPatternAddInteger(pat, FC_WEIGHT, FC_WEIGHT_NORMAL);
        fs = FcFontList(fcConfig, pat, nullptr);
        
        if (fs) {
            for (int i = 0; i < fs->nfont; ++i) {
                FcPattern *font = fs->fonts[i];
                FcChar8 *fontName;
                if (FcPatternGetString(font, FC_FILE, 0, &fontName) == FcResultMatch) {
                    found = true;
                    if (FT_New_Face(ft, (char *) fontName, 0, &face)) {
                        fprintf(stderr, "Could not open fallback font %s\n", (char *) fontName);
                        return;
                    } else {
                        break;
                    }
                }
            }
            FcFontSetDestroy(fs);
        }
        if (!found) {
            fprintf(stderr, "Could not find fallback font for %s\n", font_name.c_str());
            return;
        }
        // Since the requested style wasn’t available as a separate file,
        // mark the need for synthetic styling.
        this->needs_synth = true;
    }
    
    FcPatternDestroy(pat);
    
    // Force a UCS2 charmap (unchanged)
    force_ucs2_charmap(face);
    FT_Set_Char_Size(face, 0, std::round((float) size * 64.0f * (100.0f / 76.0f)), 72, 72);
    
    FT_Library_SetLcdFilter(ft, FT_LCD_FILTER_DEFAULT);
    hb_buffer = hb_buffer_create();
    hb_font = hb_ft_font_create(face, NULL);
    features.push_back(HBFeature::KerningOn);
    
    int num_nodes = (atlas_w * atlas_h) / size / 2;
    nodes = new stbrp_node[num_nodes];
    stbrp_init_target(&ctx, atlas_w, atlas_h, nodes, num_nodes);
    
    std::string vertexShaderCode =
            R"(
    #version 330 core

    attribute vec4 coord;
    varying vec2 texpos;
    uniform mat4 projection;

    void main() {
        gl_Position = projection * vec4(coord.x, coord.y, 0, 1);
        texpos = coord.zw;
    }
)";
    std::string fragmentShaderCode =
            R"(
    #version 330 core

    varying vec2 texpos;
    uniform sampler2D tex;
    uniform vec4 color;

    void main(void) {
      // Get the current color at the fragment position
      vec4 start = texture2D(tex, texpos);
      //start = pow(start, vec4(1.0 / 1.45)); // gamma white
      start = pow(start, vec4(1.0 / 1.8)); // gamma black
      //start = pow(start, vec4(1.0 / 2.2)); // gamma black
      gl_FragColor = vec4((start.r * color.r),
                          (start.g * color.g),
                          (start.b * color.b), start.a * color.a);
    }
)";
    // Compile shaders
    GLuint vertexShader = compileShader(vertexShaderCode, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentShaderCode, GL_FRAGMENT_SHADER);
    
    // Link shaders
    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertexShader);
    glAttachShader(shader_program, fragmentShader);
    glLinkProgram(shader_program);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // Get uniform location
    projection_uniform = glGetUniformLocation(shader_program, "projection");
    attribute_coord = glGetAttribLocation(shader_program, "coord");
    uniform_tex = glGetUniformLocation(shader_program, "tex");
    uniform_color = glGetUniformLocation(shader_program, "color");
    
    glGenBuffers(1, &VBO);
    
    /* Create a texture that will be used to hold all ASCII glyphs */
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(uniform_tex, 0);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    
    /* We require 1 byte alignment when uploading texture data. WhhhY? */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    /* Clamping to edges is important to prevent artifacts when scaling */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    /* Linear filtering usually looks best for text */
    glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
    glEnable(GL_BLEND);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void FreeFont::update_projection(const glm::mat4 &projection) {
    this->projection = projection;
}

void FreeFont::bind_needed_glyphs(const std::u32string &text) {
    hb_buffer_reset(hb_buffer);
    std::vector<char32_t> line;
    
    // TODO: instead of checking the glyph info, first check the
    for (int current_index = 0; current_index < current_text.size(); current_index++) {
        if (current_text[current_index] == '\n' || current_index == current_text.size() - 1) {
            hb_buffer_add_utf32(hb_buffer, (uint32_t *) line.data(), line.size(), 0, -1);
            hb_buffer_guess_segment_properties(hb_buffer);
            hb_shape(hb_font, hb_buffer, features.empty() ? NULL : &features[0], features.size());
            unsigned int glyph_count;
            hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
            
            for (int i = 0; i < glyph_count; i++) {
                hb_glyph_info_t ginfo = glyph_info[i];
                
                GlyphInfo info = {};
                for (auto pc: loaded_glyphs) {
                    if (pc.codepoint == ginfo.codepoint) {
                        info = pc;
                        break;
                    }
                }
                // Apparently we can get a '0' codepoint glyph with valid? advance info
                if (info.codepoint == 0 && ginfo.codepoint != 0) {
                    FT_Load_Glyph(face, ginfo.codepoint, FT_LOAD_TARGET_LCD);
                    // If synthetic bolding is requested, embolden the glyph before rendering.
                    if (this->needs_synth) {
                        if (this->italic)
                            FT_GlyphSlot_Oblique(face->glyph);
                        if (this->bold)
                            FT_GlyphSlot_Embolden(face->glyph);
                    }
                    FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
                    FT_GlyphSlot g = face->glyph;
                    
                    info.codepoint = ginfo.codepoint;
                    float padding = 1;
                    info.bitmap_w = g->bitmap.width / 3;
                    info.bitmap_h = g->bitmap.rows;
                    info.bearing_x = g->bitmap_left;
                    info.bearing_y = g->bitmap_top;
                    info.location.w = info.bitmap_w + padding * 2;
                    info.location.h = info.bitmap_h + padding * 2; // TODO: This was changed from bitmap_w to bitmap_h, is that correct?
                    stbrp_pack_rects(&ctx, &info.location, 1);
                    assert(info.location.was_packed && "TODO: Handle when character fails to be packed");
                    info.metrics = g->metrics;
                    info.u1 = ((float) info.location.x + padding) / atlas_w;
                    info.u2 = ((float) info.location.x + padding + (float) info.bitmap_w) / atlas_w;
                    info.v1 = ((float) info.location.y + padding) / atlas_h;
                    info.v2 = ((float) info.location.y + padding + (float) info.bitmap_h) / atlas_h;
                    loaded_glyphs.push_back(info);
                    
                    std::vector<uint8_t> pixels3;
                    for (int y = 0; y < face->glyph->bitmap.rows; y++) {
                        for (int x = 0; x < face->glyph->bitmap.pitch / 3; x++) {
                            uint8_t r = face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x * 3];
                            uint8_t g = face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x * 3 + 1];
                            uint8_t b = face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x * 3 + 2];
                            pixels3.push_back(r);
                            pixels3.push_back(g);
                            pixels3.push_back(b);
                            pixels3.push_back((r + g + b) / 3);
                        }
                    }
                    
                    glTexSubImage2D(GL_TEXTURE_2D, 0,
                                    info.location.x + padding,
                                    info.location.y + padding,
                                    face->glyph->bitmap.pitch / 3,
                                    face->glyph->bitmap.rows,
                                    GL_RGBA, GL_UNSIGNED_BYTE,
                                    pixels3.data());
                }
            }
            
            hb_buffer_reset(hb_buffer);
            line.clear();
            continue;
        }
        
        line.push_back(current_text[current_index]);
    }
}

void FreeFont::generate_info_needed_for_alignment() {
    full_text_w = full_text_h = 0;
    line_widths.clear();
    largest_horiz_bearing_y = 0;
    
    hb_buffer_reset(hb_buffer);
    std::vector<char32_t> line;
    int lines = 0;
    //
    //
    // TODO: we should only load hb_buffer once, and then get glyph info using substrings into the buffer
    //  it would be less costly, also the way harfbuzz recommends doing it.
    //
    //
    for (int current_index = 0; current_index <= current_text.size(); current_index++) {
        if (current_text[current_index] == '\n' || current_index == current_text.size()) {
            hb_buffer_add_utf32(hb_buffer, (uint32_t *) line.data(), line.size(), 0, -1);
            hb_buffer_guess_segment_properties(hb_buffer);
            hb_shape(hb_font, hb_buffer, features.empty() ? NULL : &features[0], features.size());
            unsigned int glyph_count;
            hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
            hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);
            
            float pen_x = 0;
            float last_w = 0;
            float first_bearing = 0; // we need to remove the first bearing to get accurate width
            for (int i = 0; i < glyph_count; i++) {
                hb_glyph_info_t ginfo = glyph_info[i];
                hb_glyph_position_t pos = glyph_pos[i];
                GlyphInfo info = {};
                info.metrics.horiBearingY = 0;
                for (auto pc: loaded_glyphs) {
                    if (pc.codepoint == ginfo.codepoint) {
                        info = pc;
                        break;
                    }
                }
                if (i == 0)
                    first_bearing = info.bearing_x;
                if (info.metrics.horiBearingY > largest_horiz_bearing_y)
                    largest_horiz_bearing_y = info.metrics.horiBearingY >> 7;
                if (i != glyph_count - 1) {
                    pen_x += pos.x_advance;
                } else {
                    last_w = std::ceil(info.bitmap_w + first_bearing);
                }
            }
            
            float w_f = pen_x / 64.0 + last_w;
            line_widths.push_back(w_f);
            if (w_f > full_text_w)
                full_text_w = w_f;
            
            hb_buffer_reset(hb_buffer);
            line.clear();
            lines++;
            continue;
        }
        
        line.push_back(current_text[current_index]);
    }
    
    full_text_h = (face->size->metrics.height >> 6) * lines;
}

void FreeFont::begin() {
    glUseProgram(shader_program);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}

void FreeFont::end() {
    glDisableVertexAttribArray(attribute_coord);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FreeFont::set_text(std::string text) {
    // Removes '\r' as they are not needed
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("Erase \r");
#endif
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        current_text_raw = text;
    }
    
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("convert");
#endif
        // Convert to utf32, so we feed the correct code points to freetype
        // Create a wide string using UTF-16 encoding (wchar_t)
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring utf16String = converter.from_bytes(text);
        
        // Create a UTF-32 encoded string (std::u32string)
       current_text = std::u32string(utf16String.begin(), utf16String.end() + 1);
    }
    
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("check empty");
#endif
        if (current_text_raw.empty()) {
            full_text_w = 0;
            full_text_h = 0;
            return;
        }
    }
    
    {
#ifdef TRACY_ENABLE
            ZoneScopedN("bind_needed_glyphs");
#endif
        bind_needed_glyphs(current_text);
    }
    
    {
#ifdef TRACY_ENABLE
        ZoneScopedN("generate_info_needed_for_alignment");
#endif
        generate_info_needed_for_alignment();
    }
}

void FreeFont::draw_text(PangoAlignment align, float x, float y, float wrap) {
    if (current_text_raw.empty())
        return;
    glUniformMatrix4fv(projection_uniform, 1, GL_FALSE, glm::value_ptr(projection));
    
    glUniform4f(uniform_color, color.r, color.g, color.b, color.a);
    
    /* Set up the VBO for our vertex data */
    glEnableVertexAttribArray(attribute_coord);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

//        glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    struct point {
        GLfloat texture_x;
        GLfloat texture_y;
        GLfloat texture_u;
        GLfloat texture_v;
    };
    // Six because it's three points per triangle, and you need two triangles to make a quad (rectangle)
    point vertex_corners[6 * current_text.size()];
    
    int current_corner = 0;
    int current_line = 0;
    
    float pen_x;
    if (align == PANGO_ALIGN_RIGHT) {
        pen_x = std::floor((full_text_w - line_widths[current_line]) * 64.0);
    } else if (align == PANGO_ALIGN_CENTER) {
        pen_x = std::floor(((full_text_w - line_widths[current_line]) / 2) * 64.0);
    } else {
        pen_x = 0;
    }
    float pen_y = 0;
    float line_height = face->size->metrics.height >> 6;
    
    hb_buffer_reset(hb_buffer);
    std::vector<char32_t> line;
    
    for (int current_index = 0; current_index <= current_text.size(); current_index++) {
        if (current_text[current_index] == '\n' || current_index == current_text.size()) {
            hb_buffer_add_utf32(hb_buffer, (uint32_t *) line.data(), line.size(), 0, -1);
            hb_buffer_guess_segment_properties(hb_buffer);
            hb_shape(hb_font, hb_buffer, features.empty() ? NULL : &features[0], features.size());
            unsigned int glyph_count;
            hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
            hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);
            
            int maxAscent = int(face->ascender * (face->size->metrics.y_scale / 65536.0)) >> 6;
            int maxDescent = int(abs(face->descender * (face->size->metrics.y_scale / 65536.0))) >> 6;
            
            for (int i = 0; i < glyph_count; i++) {
                hb_glyph_info_t ginfo = glyph_info[i];
                hb_glyph_position_t pos = glyph_pos[i];
                
                GlyphInfo glyph_info = {};
                for (auto pc: loaded_glyphs) {
                    if (pc.codepoint == ginfo.codepoint) {
                        glyph_info = pc;
                        break;
                    }
                }
                if (glyph_info.codepoint != 0) {
                    float baseline_y = maxAscent - glyph_info.bearing_y;
                    
                    float full_scale_pen_x = pen_x;
                    pen_x = full_scale_pen_x / 64.0;
                    
                    float x_offset = (float) pos.x_offset / 64;
                    float left_x = pen_x + x + x_offset + glyph_info.bearing_x;
                    float right_x = left_x + glyph_info.bitmap_w;
                    
                    float y_offset = (float) pos.y_offset / 64;
                    float bottom_y = std::round(pen_y + y + y_offset + baseline_y);
                    float top_y = std::round(bottom_y + glyph_info.bitmap_h);
                    
                    // create vertex glyph_info
                    //bottom left corner of triangle
                    vertex_corners[current_corner++] = {left_x,
                                                        bottom_y,
                                                        glyph_info.u1,
                                                        glyph_info.v1};
                    
                    //bottom right corner of triangle
                    vertex_corners[current_corner++] = {right_x,
                                                        bottom_y,
                                                        glyph_info.u2,
                                                        glyph_info.v1};
                    
                    //top right corner of triangle
                    vertex_corners[current_corner++] = {right_x,
                                                        top_y,
                                                        glyph_info.u2,
                                                        glyph_info.v2};
                    
                    
                    //top right corner of triangle
                    vertex_corners[current_corner++] = {right_x,
                                                        top_y,
                                                        glyph_info.u2,
                                                        glyph_info.v2};
                    
                    //top left corner of triangle
                    vertex_corners[current_corner++] = {left_x,
                                                        top_y,
                                                        glyph_info.u1,
                                                        glyph_info.v2};
                    
                    //bottom left corner of triangle
                    vertex_corners[current_corner++] = {left_x,
                                                        bottom_y,
                                                        glyph_info.u1,
                                                        glyph_info.v1};
                    pen_x = full_scale_pen_x;
                    pen_x += pos.x_advance;
                } else {
                    // There are cases where we have no codepoint, but DO have an advance we need to attend to
                    pen_x += pos.x_advance;
                }
            }
            
            current_line++;
            if (align == PANGO_ALIGN_RIGHT) {
                if (current_line < line_widths.size()) {
                    pen_x = std::floor((full_text_w - line_widths[current_line]) * 64.0);
                }
            } else if (align == PANGO_ALIGN_CENTER) {
                if (current_line < line_widths.size()) {
                    pen_x = std::floor(((full_text_w - line_widths[current_line]) / 2) * 64.0);
                }
            } else {
                pen_x = 0;
            }
            pen_y += line_height;
            hb_buffer_reset(hb_buffer);
            line.clear();
            continue;
        }
        line.push_back(current_text[current_index]);
    }
    
    glBufferData(GL_ARRAY_BUFFER, sizeof vertex_corners, vertex_corners, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, current_corner);
}

void FreeFont::draw_text(float x, float y, float wrap) {
    draw_text(PangoAlignment::PANGO_ALIGN_LEFT, x, y, wrap);
}

void FreeFont::set_color(float r, float g, float b) {
    set_color(r, g, b, 1.0f);
}

void FreeFont::set_color(float r, float g, float b, float a) {
    this->color.r = r;
    this->color.g = g;
    this->color.b = b;
    this->color.a = a;
}

std::string FreeFont::wrapped_text(std::string text, float wrap) {
    std::string buffer;
    
    int base = 0;
    bool first_line = true;
    for (int i = 0; i < text.size(); i++) {
        this->begin();
        this->set_text(text.substr(base, i - base));
        this->end();
        
        if (this->full_text_w >= wrap) {
            defer(first_line = false);
            if (isalpha(text[i]) && first_line) {
                for (int x = i - 1; x >= 0; x--) {
                    if (!isalpha(text[x])) {
                        for (int z = 0; z < (i - x); z++) {
                            buffer.pop_back();
                        }
                        
                        i = x;
                        base = x;
                        buffer.push_back('\n');
                        break;
                    }
                }
                continue;
            }
            buffer.push_back('\n');
            base = i;
        }
        buffer.push_back(text[i]);
    }
    
    return buffer;
}

FontReference *FontManager::get(AppClient *client, int size, std::string font, bool bold, bool italic) {
    for (int i = fonts.size() - 1; i >= 0; i--) { // Get rid of fonts which have no live client
        if (!fonts[i]->creation_client_alive.lock()) {
            delete fonts[i];
            fonts.erase(fonts.begin() + i);
        }
    }
    
    for (auto f: fonts) {
        auto weight_matches = bold ? f->weight == PANGO_WEIGHT_BOLD : f->weight == PANGO_WEIGHT_NORMAL;
        if (client->should_use_gl) {
            if (f->size == size && f->name == font && f->creation_client == client && f->italic == italic && weight_matches) {
                return f;
            }
        } else {
            if (f->size == size && f->name == font && f->italic == italic && weight_matches) {
                return f;
            }
        }
    }
    
    auto ref = new FontReference;
    ref->name = font;
    ref->size = size;
    ref->weight = PANGO_WEIGHT_NORMAL;
    if (bold)
        ref->weight = PANGO_WEIGHT_BOLD;
    ref->italic = italic;
    ref->creation_client = client;
    ref->creation_client_alive = client->lifetime;
    // Free any whose lifetimes are gone
    // This guy needs to account for different clients unlike the cairo version
    if (client->should_use_gl) {
        ref->font = new FreeFont(size, font, bold, italic);
    } else {
        // Create the font ref and add it to the list
//        ref->layout = get_cached_pango_font(client->cr, font, size, PANGO_WEIGHT_NORMAL);
//        ref->layout = get_cached_pango_font(client->cr, font, size * config->dpi, PANGO_WEIGHT_NORMAL);
    }
    fonts.push_back(ref);
    return ref;
}

void FontReference::begin() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (!font) {
        layout = get_cached_pango_font(creation_client->cr, name, size, (PangoWeight) weight, italic);
        return;
    }
    font->begin();
}

void FontReference::set_color(float r, float g, float b, float a) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (layout) {
        cairo_set_source_rgba(creation_client->cr, r, g, b, a);
    } else {
        font->set_color(r, g, b, a);
    }
}

void FontReference::set_text(std::string text) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (layout) {
        pango_layout_set_text(layout, text.data(), text.size());
    } else {
        font->set_text(text);
    }
}

std::string FontReference::wrapped_text(std::string text, int w) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    if (!font) {
        layout = get_cached_pango_font(creation_client->cr, name, size, (PangoWeight) weight, italic);
        
        auto pre_w = pango_layout_get_width(layout);
        auto pre_wrap = pango_layout_get_wrap(layout);
        pango_layout_set_text(layout, text.data(), text.size());
        
        pango_layout_set_width(layout, w * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        
        // Get the number of lines in the layout.
        int line_count = pango_layout_get_line_count(layout);

        std::stringstream ss;
        
        for (int i = 0; i < line_count; ++i) {
            // Get the i-th layout line.
            PangoLayoutLine* line = pango_layout_get_line_readonly(layout, i);
            if (!line)
                continue;
            
            // Get the start index and length of the substring for this line.
            int start_index = line->start_index;
            int line_length = line->length;
            
            // Extract the substring from the original text.
            // (This assumes that the original text has not been modified since being set on the layout.)
            ss << text.substr(start_index, line_length);
            
            // Append a newline if this is not the last line.
            if (i != line_count - 1)
                ss << "\n";
        }
        
        std::string wrapped_text = ss.str();
        
        pango_layout_set_wrap(layout, pre_wrap);
        pango_layout_set_width(layout, pre_w);
        
        return wrapped_text;
    } else {
        return font->wrapped_text(text, w);
    }
}

void FontReference::draw_text(int align, int x, int y, int wrap) {
    if (layout) {
        auto p_wrap = pango_layout_get_wrap(layout);
        auto p_width = pango_layout_get_width(layout);
        auto p_align = pango_layout_get_alignment(layout);
        
        pango_layout_set_width(layout, wrap * PANGO_SCALE);
        pango_layout_set_wrap(layout, PangoWrapMode::PANGO_WRAP_WORD_CHAR);
        
        draw_text(x, y, align);
        
        pango_layout_set_wrap(layout, p_wrap);
        pango_layout_set_width(layout, p_width);
    } else {
        font->draw_text((PangoAlignment) align, x, y, wrap);
    }
}

void FontReference::draw_text(int x, int y, int param) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    bool cares_about_align = param != 5;
    if (layout) {
        PangoAlignment original_align;
        if (cares_about_align) {
           original_align =  pango_layout_get_alignment(layout);
        }
        cairo_move_to(creation_client->cr, x, y);
        if (cares_about_align) {
            pango_layout_set_alignment(layout, (PangoAlignment) param);
        }
        pango_cairo_show_layout(creation_client->cr, layout);
        if (cares_about_align) {
            pango_layout_set_alignment(layout, original_align);
        }
    } else {
        if (cares_about_align) {
            font->draw_text((PangoAlignment) param, x, y);
        } else {
            font->draw_text(x, y);
        }
    }
}

void FontReference::end() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (layout)
        return;
    font->end();
}

Sizes FontReference::sizes() {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    if (layout) {
        PangoRectangle ink;
        PangoRectangle logical;
        pango_layout_get_extents(layout, &ink, &logical);
        return {(float) (logical.width / PANGO_SCALE), (float) (logical.height / PANGO_SCALE)};
    } else {
        return {font->full_text_w, font->full_text_h};
    }
}

Sizes FontReference::begin(std::string text, float r, float g, float b, float a) {
#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    
    begin();
    set_text(text);
    set_color(r, g, b, a);
    return sizes();
}

void FontReference::draw_text_end(int x, int y, int param) {
    draw_text(x, y, param);
    end();
}

OffscreenFrameBuffer::OffscreenFrameBuffer(int width, int height) : width(width), height(height) {
    create(width, height);
}

OffscreenFrameBuffer::~OffscreenFrameBuffer() {
    destroy();
}

void OffscreenFrameBuffer::push() {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height); // Ensure the viewport matches the FBO size
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Set clear color to transparent black (or any color you need)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear color and depth buffers (if depth is used)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OffscreenFrameBuffer::pop(bool blur) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Bind back to the default framebuffer
    if (blur) {
        glUseProgram(shaderProgramBlur);
    } else {
        glUseProgram(shaderProgram);
    }
    glBindVertexArray(quadVAO);
    glBindTexture(GL_TEXTURE_2D, texColorBuffer);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

void OffscreenFrameBuffer::destroy() {
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texColorBuffer);
    glDeleteRenderbuffers(1, &rbo);
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
}

void OffscreenFrameBuffer::create(int w, int h) {
    width = w;
    height = h;
    // Generate and bind the framebuffer
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    // Create a color attachment texture
    glGenTextures(1, &texColorBuffer);
    glBindTexture(GL_TEXTURE_2D, texColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColorBuffer, 0);
    
    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    
    // Load and compile shaders and link program
    GLuint vertexShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    GLuint fragmentShaderBlur = compileShader(fragmentShaderSourceBlur, GL_FRAGMENT_SHADER);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    shaderProgramBlur = glCreateProgram();
    glAttachShader(shaderProgramBlur, vertexShader);
    glAttachShader(shaderProgramBlur, fragmentShaderBlur);
    glLinkProgram(shaderProgramBlur);
    
    // Cleanup shaders (they can be deleted once linked into a program)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteShader(fragmentShaderBlur);
    
    // Setup quad for drawing FBO texture
    float quadVertices[] = {
            // positions   // texCoords
            -1.0f, 1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            
            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) (2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OffscreenFrameBuffer::resize(int w, int h) {
    destroy();
    create(w, h);
}
