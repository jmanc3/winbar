
#include "container.h"
#include "application.h"
#include "../src/components.h"

#include <cassert>
#include <cmath>
#include <iostream>

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
            highest_y = child->real_bounds.y + child->real_bounds.h;
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
            highest_x = child->real_bounds.x + child->real_bounds.w;
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
                child->when_layout(client, container, bounds, &target_w, &target_h);
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
single_filler_width(Container *container) {
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
    double fill_w = single_filler_width(container);
    
    double offset = 0;
    for (auto child: container->children) {
        if (child && child->exists) {
            double target_w = child->wanted_pad.x + child->wanted_pad.w;
            double target_h = child->wanted_pad.y + child->wanted_pad.h;
            
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
                child->when_layout(client, container, bounds, &target_w, &target_h);
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
    
    double true_width = actual_true_width(scrollpane->content);
    // add to true_width to account for right if it exists and not inline
    if (scrollpane->right && scrollpane->right->exists && !scrollpane->settings.right_inline_track)
        true_width += scrollpane->right->real_bounds.w;
    scrollpane->scroll_h_real = -std::max(0.0,
                                          std::min(-scrollpane->scroll_h_real, true_width - scrollpane->real_bounds.w));
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
    scroll->scroll_v_visual = scroll->scroll_v_real;
    scroll->scroll_h_visual = scroll->scroll_h_real;
    
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
    
    for (auto child: root->children) {
        auto possible = container_by_name(name, child);
        if (possible)
            return possible;
    }
    
    return nullptr;
}

Container *
container_by_container(Container *target, Container *root) {
    if (root == target) {
        return root;;
    }
    
    for (auto child: root->children) {
        auto possible = container_by_container(target, child);
        if (possible)
            return possible;
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
}

Container::~Container() {
    for (auto child: children) {
        if (child->type == layout_type::newscroll) {
            delete (ScrollContainer *) child;
        } else {
            delete child;
        }
    }
    auto data = static_cast<UserData *>(user_data);
    delete data;
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
    
    epoll_ctl(app->epoll_fd, EPOLL_CTL_DEL, outpipe[0], NULL);
    close(inpipe[1]);
    close(outpipe[0]);
    if (::kill(pid, SIGTERM) == -1) {
        std::cerr << "Error killing child process\n";
    }
    
    if (this->timeout_fd != -1) {
        epoll_ctl(app->epoll_fd, EPOLL_CTL_DEL, this->timeout_fd, NULL);
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
