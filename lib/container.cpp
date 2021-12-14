
#include "container.h"
#include "application.h"

#include <cassert>
#include <cmath>
#include <iostream>

// Sum of non filler child height and spacing
double
reserved_height(Container *box) {
    double space = 0;
    for (auto child: box->children) {
        if (child->wanted_bounds.h == FILL_SPACE) {
            space += child->wanted_pad.y + child->wanted_pad.h;
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

    if (container->children.empty())
        return;
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
        delete child;
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
