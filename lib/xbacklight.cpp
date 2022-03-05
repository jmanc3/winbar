//
// Created by jmanc3 on 4/6/20.
//

#include "xbacklight.h"

/*
 * Copyright © 2007 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>

#include <string.h>
#include <unistd.h>

static xcb_atom_t backlight, backlight_new, backlight_legacy;

static long
backlight_get(xcb_connection_t *conn, xcb_randr_output_t output) {
    xcb_generic_error_t *error;
    xcb_randr_get_output_property_reply_t *prop_reply = NULL;
    xcb_randr_get_output_property_cookie_t prop_cookie;
    long value;
    
    backlight = backlight_new;
    if (backlight != XCB_ATOM_NONE) {
        prop_cookie =
                xcb_randr_get_output_property(conn, output, backlight, XCB_ATOM_NONE, 0, 4, 0, 0);
        prop_reply = xcb_randr_get_output_property_reply(conn, prop_cookie, &error);
        if (error != NULL || prop_reply == NULL) {
            backlight = backlight_legacy;
            if (backlight != XCB_ATOM_NONE) {
                prop_cookie =
                        xcb_randr_get_output_property(conn, output, backlight, XCB_ATOM_NONE, 0, 4, 0, 0);
                prop_reply = xcb_randr_get_output_property_reply(conn, prop_cookie, &error);
                if (error != NULL || prop_reply == NULL) {
                    return -1;
                }
            }
        }
    }
    
    if (prop_reply == NULL || prop_reply->type != XCB_ATOM_INTEGER || prop_reply->num_items != 1 ||
        prop_reply->format != 32) {
        value = -1;
    } else {
        value = *((int32_t *) xcb_randr_get_output_property_data(prop_reply));
    }
    
    free(prop_reply);
    return value;
}

static void
backlight_set(xcb_connection_t *conn, xcb_randr_output_t output, long value) {
    xcb_randr_change_output_property(conn,
                                     output,
                                     backlight,
                                     XCB_ATOM_INTEGER,
                                     32,
                                     XCB_PROP_MODE_REPLACE,
                                     1,
                                     (unsigned char *) &value);
}

// Value between 0 and 100
int backlight_set_brightness(int amount) {
    char *dpy_name = NULL;
    int value = amount;
    int i;
    int total_time = 200; /* ms */
    int steps = 20;
    
    xcb_connection_t *conn;
    xcb_generic_error_t *error;
    
    xcb_randr_query_version_cookie_t ver_cookie;
    xcb_randr_query_version_reply_t *ver_reply;
    
    xcb_intern_atom_cookie_t backlight_cookie[2];
    xcb_intern_atom_reply_t *backlight_reply;
    
    xcb_screen_iterator_t iter;
    
    conn = xcb_connect(dpy_name, NULL);
    ver_cookie = xcb_randr_query_version(conn, 1, 2);
    ver_reply = xcb_randr_query_version_reply(conn, ver_cookie, &error);
    if (error != NULL || ver_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "RANDR Query Version returned error %d\n", ec);
        return -1;
    }
    if (ver_reply->major_version != 1 || ver_reply->minor_version < 2) {
        fprintf(stderr,
                "RandR version %d.%d too old\n",
                ver_reply->major_version,
                ver_reply->minor_version);
        return -1;
    }
    free(ver_reply);
    
    backlight_cookie[0] = xcb_intern_atom(conn, 1, strlen("Backlight"), "Backlight");
    backlight_cookie[1] = xcb_intern_atom(conn, 1, strlen("BACKLIGHT"), "BACKLIGHT");
    
    backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[0], &error);
    if (error != NULL || backlight_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "Intern Atom returned error %d\n", ec);
        return -1;
    }
    
    backlight_new = backlight_reply->atom;
    free(backlight_reply);
    
    backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[1], &error);
    if (error != NULL || backlight_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "Intern Atom returned error %d\n", ec);
        return -1;
    }
    
    backlight_legacy = backlight_reply->atom;
    free(backlight_reply);
    
    if (backlight_new == XCB_NONE && backlight_legacy == XCB_NONE) {
        fprintf(stderr, "No outputs have backlight property\n");
        return -1;
    }
    
    iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
    while (iter.rem) {
        xcb_screen_t *screen = iter.data;
        xcb_window_t root = screen->root;
        xcb_randr_output_t *outputs;
        
        xcb_randr_get_screen_resources_cookie_t resources_cookie;
        xcb_randr_get_screen_resources_reply_t *resources_reply;
        
        resources_cookie = xcb_randr_get_screen_resources(conn, root);
        resources_reply = xcb_randr_get_screen_resources_reply(conn, resources_cookie, &error);
        if (error != NULL || resources_reply == NULL) {
            int ec = error ? error->error_code : -1;
            fprintf(stderr, "RANDR Get Screen Resources returned error %d\n", ec);
            continue;
        }
        
        outputs = xcb_randr_get_screen_resources_outputs(resources_reply);
        for (int o = 0; o < resources_reply->num_outputs; o++) {
            xcb_randr_output_t output = outputs[o];
            double cur, new_something, step;
            double min, max;
            double set;
            
            cur = backlight_get(conn, output);
            if (cur != -1) {
                xcb_randr_query_output_property_cookie_t prop_cookie;
                xcb_randr_query_output_property_reply_t *prop_reply;
                
                prop_cookie = xcb_randr_query_output_property(conn, output, backlight);
                prop_reply = xcb_randr_query_output_property_reply(conn, prop_cookie, &error);
                
                if (error != NULL || prop_reply == NULL)
                    continue;
                
                if (prop_reply->range &&
                    xcb_randr_query_output_property_valid_values_length(prop_reply) == 2) {
                    int32_t *values = xcb_randr_query_output_property_valid_values(prop_reply);
                    min = values[0];
                    max = values[1];
                    
                    set = value * (max - min) / 100;
                    new_something = min + set;
                    if (new_something > max)
                        new_something = max;
                    if (new_something < min)
                        new_something = min;
                    step = (new_something - cur) / steps;
                    for (i = 0; i < steps && step != 0; i++) {
                        if (i == steps - 1)
                            cur = new_something;
                        else
                            cur += step;
                        backlight_set(conn, output, (long) cur);
                        xcb_flush(conn);
                        usleep(total_time * 1000 / steps);
                    }
                }
                free(prop_reply);
            }
        }
        
        free(resources_reply);
        xcb_screen_next(&iter);
    }
    xcb_aux_sync(conn);
    
    return 0;
}

int backlight_get_brightness() {
    char *dpy_name = NULL;
    
    xcb_connection_t *conn;
    xcb_generic_error_t *error;
    
    xcb_randr_query_version_cookie_t ver_cookie;
    xcb_randr_query_version_reply_t *ver_reply;
    
    xcb_intern_atom_cookie_t backlight_cookie[2];
    xcb_intern_atom_reply_t *backlight_reply;
    
    xcb_screen_iterator_t iter;
    
    conn = xcb_connect(dpy_name, NULL);
    ver_cookie = xcb_randr_query_version(conn, 1, 2);
    ver_reply = xcb_randr_query_version_reply(conn, ver_cookie, &error);
    if (error != NULL || ver_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "RANDR Query Version returned error %d\n", ec);
        return -1;
    }
    if (ver_reply->major_version != 1 || ver_reply->minor_version < 2) {
        fprintf(stderr,
                "RandR version %d.%d too old\n",
                ver_reply->major_version,
                ver_reply->minor_version);
        return -1;
    }
    free(ver_reply);
    
    backlight_cookie[0] = xcb_intern_atom(conn, 1, strlen("Backlight"), "Backlight");
    backlight_cookie[1] = xcb_intern_atom(conn, 1, strlen("BACKLIGHT"), "BACKLIGHT");
    
    backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[0], &error);
    if (error != NULL || backlight_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "Intern Atom returned error %d\n", ec);
        return -1;
    }
    
    backlight_new = backlight_reply->atom;
    free(backlight_reply);
    
    backlight_reply = xcb_intern_atom_reply(conn, backlight_cookie[1], &error);
    if (error != NULL || backlight_reply == NULL) {
        int ec = error ? error->error_code : -1;
        fprintf(stderr, "Intern Atom returned error %d\n", ec);
        return -1;
    }
    
    backlight_legacy = backlight_reply->atom;
    free(backlight_reply);
    
    if (backlight_new == XCB_NONE && backlight_legacy == XCB_NONE) {
        fprintf(stderr, "No outputs have backlight property\n");
        return -1;
    }
    
    iter = xcb_setup_roots_iterator(xcb_get_setup(conn));
    while (iter.rem) {
        xcb_screen_t *screen = iter.data;
        xcb_window_t root = screen->root;
        xcb_randr_output_t *outputs;
        
        xcb_randr_get_screen_resources_cookie_t resources_cookie;
        xcb_randr_get_screen_resources_reply_t *resources_reply;
        
        resources_cookie = xcb_randr_get_screen_resources(conn, root);
        resources_reply = xcb_randr_get_screen_resources_reply(conn, resources_cookie, &error);
        if (error != NULL || resources_reply == NULL) {
            int ec = error ? error->error_code : -1;
            fprintf(stderr, "RANDR Get Screen Resources returned error %d\n", ec);
            continue;
        }
        
        outputs = xcb_randr_get_screen_resources_outputs(resources_reply);
        for (int o = 0; o < resources_reply->num_outputs; o++) {
            xcb_randr_output_t output = outputs[o];
            double cur;
            double min, max;
            
            cur = backlight_get(conn, output);
            if (cur != -1) {
                xcb_randr_query_output_property_cookie_t prop_cookie;
                xcb_randr_query_output_property_reply_t *prop_reply;
                
                prop_cookie = xcb_randr_query_output_property(conn, output, backlight);
                prop_reply = xcb_randr_query_output_property_reply(conn, prop_cookie, &error);
                
                if (error != NULL || prop_reply == NULL)
                    continue;
                
                if (prop_reply->range &&
                    xcb_randr_query_output_property_valid_values_length(prop_reply) == 2) {
                    int32_t *values = xcb_randr_query_output_property_valid_values(prop_reply);
                    min = values[0];
                    max = values[1];
                    
                    free(prop_reply);
                    free(resources_reply);
                    xcb_aux_sync(conn);
                    return (cur - min) * 100 / (max - min);
                }
                free(prop_reply);
            }
        }
        
        free(resources_reply);
        xcb_screen_next(&iter);
    }
    xcb_aux_sync(conn);
    
    return 0;
}
