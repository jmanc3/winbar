//
// Created by jmanc3 on 5/12/21.
//

#include "pinned_icon_editor.h"
#include "application.h"
#include "main.h"
#include "components.h"
#include "config.h"

void paint_background(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_pinned_icon_editor_background);
    cairo_fill(cr);
}

void paint_ex(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, config->color_search_accent);
    cairo_fill(cr);
}

void fill_root(AppClient *client) {
    Container *root = client->root;
    root->wanted_pad = Bounds(10, 10, 10, 10);
    root->spacing = 10;
    root->type = vbox;
    root->when_paint = paint_background;

    TextAreaSettings textarea_settings = TextAreaSettings();
    textarea_settings.single_line = true;
    textarea_settings.bottom_show_amount = 2;
    textarea_settings.right_show_amount = 2;

    Container *icon_name_hbox = root->child(layout_type::hbox, FILL_SPACE, 64);
    icon_name_hbox->spacing = 10;
    {
        Container *icon = icon_name_hbox->child(64, 64);
        icon->when_paint = paint_ex;

        Container *icon_name_label_and_field_vbox = icon_name_hbox->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
        {
            Container *icon_name_label = icon_name_label_and_field_vbox->child(FILL_SPACE, 13);
            icon_name_label->when_paint = paint_ex;

            icon_name_label_and_field_vbox->child(FILL_SPACE, FILL_SPACE);

            Container *icon_name_field = make_textarea(app, client, icon_name_label_and_field_vbox, textarea_settings);
//            icon_name_field->when_paint = paint_ex;
            icon_name_field->wanted_bounds.h = 32;
        }
    }

    Container *launch_command_label_and_field = root->child(layout_type::vbox, FILL_SPACE, 64);
    {
        Container *launch_command_label = launch_command_label_and_field->child(FILL_SPACE, 13);
        launch_command_label->when_paint = paint_ex;

        launch_command_label_and_field->child(FILL_SPACE, FILL_SPACE);

        Container *launch_command_field = make_textarea(app, client, launch_command_label_and_field, textarea_settings);
//        launch_command_field->when_paint = paint_ex;
        launch_command_field->wanted_bounds.h = 32;
    }

    Container *wm_class_label_and_field = root->child(layout_type::vbox, FILL_SPACE, FILL_SPACE);
    {
        Container *wm_class_label = wm_class_label_and_field->child(FILL_SPACE, 13);
        wm_class_label->when_paint = paint_ex;

        wm_class_label->child(FILL_SPACE, FILL_SPACE);

        Container *wm_class_field = make_textarea(app, client, wm_class_label_and_field, textarea_settings);
        wm_class_field->when_paint = paint_ex;
        wm_class_field->wanted_bounds.h = 32;
    }
}

void start_pinned_icon_editor(Container *icon_container) {
    Settings settings;
    settings.w = 300;
    settings.h = 300;
    if (auto client = client_new(app, settings, "pinned_icon_editor")) {
        fill_root(client);
        std::string title = "Pinned Icon Editor";
        xcb_ewmh_set_wm_name(&app->ewmh, client->window, title.length(), title.c_str());
        client_show(app, client);
    }
}