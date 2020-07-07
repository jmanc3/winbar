//
// Created by jmanc3 on 6/14/20.
//

#ifndef SCROLL_COMPONENTS_H
#define SCROLL_COMPONENTS_H

#include <application.h>
#include <stack>
#include <utility.h>

class ScrollPaneSettings : public UserData {
public:
    int right_width = 15;
    int right_arrow_height = 15;

    int bottom_height = 15;
    int bottom_arrow_width = 15;

    bool right_inline_track = false;
    bool bottom_inline_track = false;

    // 0 is always show, 1 is when needed, 2 is never show
    int right_show_amount = 1;
    int bottom_show_amount = 1;

    // paint functions
};

Container *
make_scrollpane(Container *parent, ScrollPaneSettings settings);

Bounds
right_thumb_bounds(Container *scrollpane, Bounds thumb_area);

Bounds
bottom_thumb_bounds(Container *scrollpane, Bounds thumb_area);

void scrollpane_scrolled(AppClient *client,
                         cairo_t *cr,
                         Container *container,
                         int scroll_x,
                         int scroll_y);

enum UndoType {
    INSERT,
    DELETE,
    REPLACE,
    CURSOR,
};

class UndoAction {
public:
    UndoType type;

    std::string inserted_text;
    std::string replaced_text;

    int cursor_start = -1;
    int cursor_end = -1;

    int selection_start = -1;
    int selection_end = -1;
};

class TextState : public UserData {
public:
    std::string text;
    std::string prompt;

    int cursor = 0;
    bool cursor_on = true;
    long last_time_key_press = 0;

    int preferred_x = 0;

    int selection_x = -1;// when -1 means there is no selection
    std::vector<UndoAction *> redo_stack;
    std::vector<UndoAction *> undo_stack;

    bool first_bounds_update = true;

    ~TextState() {
        for (auto *a : redo_stack) {
            delete a;
        }
        for (auto *a : undo_stack) {
            delete a;
        }
    }
};

class TextAreaData : public UserData {
public:
    // The state is the replaceable
    TextState *state = new TextState;

    // The following variables are the style
    std::string font = "Arial";
    int font_size = 15;

    bool single_line = false;
    bool wrap = false;

    ArgbColor color = ArgbColor(1, 1, 0, 1);
    ArgbColor color_cursor = ArgbColor(0, 1, 1, 1);
    double cursor_width = 1;
    ArgbColor color_prompt = ArgbColor(0, .5, .5, 1);

    int text_alignment = -1;
    int prompt_alignment = -1;

    ~TextAreaData() {
    }
};

class TextAreaSettings : public ScrollPaneSettings {
public:
    std::string font = "Arial";
    int font_size = 15;
    bool single_line = false;
    bool wrap = false;// When wrap is enabled the text area will set its width to
    // FILL_SPACE and therefore the horizontal scrollbar should
    // never appear
    std::string prompt;
    int prompt_alignment = -1;
    int text_alignment = -1;
    ArgbColor color_prompt = ArgbColor(0, .5, .5, 1);

    ArgbColor color = ArgbColor(1, 1, 0, 1);
    ArgbColor color_cursor = ArgbColor(0, 1, 1, 1);
    double cursor_width = 1;
};

Container *
make_textarea(Container *parent, TextAreaSettings settings);

void textarea_handle_keypress(App *app, xcb_generic_event_t *event, Container *textarea);

void
blink(AppClient *client, Container *textarea);

#endif// SCROLL_COMPONENTS_H
