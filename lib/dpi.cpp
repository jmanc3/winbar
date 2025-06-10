
#include "dpi.h"

#include "utility.h"
#include <xcb/randr.h>
#include <cassert>
#include <cmath>
#include <xcb/xcb_event.h>
#include <vector>

std::vector<ScreenInformation *> screens;

const xcb_query_extension_reply_t *randr_query = nullptr;

static int get_dpi_scale(int height_of_screen_in_pixels, int height_of_screen_in_millimeters) {
    double dpi = height_of_screen_in_pixels * 25.4 / height_of_screen_in_millimeters;
    return MAX(round(dpi / 96), 1);
}

static void
check_if_client_dpi_should_change_or_if_it_was_moved_to_another_screen(App *app, AppClient *client,
                                                                       bool came_from_movement) {
    // FIGURE OUT WHICH SCREEN THE CLIENT BELONGS TO
    ScreenInformation *screen_client_overlaps_most = nullptr;
    double greatest_overlap_percentage = -1;
    for (auto screen: screens) {
        auto overlap_amount = calculate_overlap_percentage(client->bounds->x, client->bounds->y,
                                                           client->bounds->w, client->bounds->h,
                                                           screen->x, screen->y,
                                                           screen->width_in_pixels,
                                                           screen->height_in_pixels);
        if (overlap_amount > greatest_overlap_percentage) {
            greatest_overlap_percentage = overlap_amount;
            screen_client_overlaps_most = screen;
        }
    }
    if (screen_client_overlaps_most) {
        // DPI CHANGED
        if (client->screen_information == nullptr ||
            client->screen_information->dpi_scale != screen_client_overlaps_most->dpi_scale) {
            if (client->automatically_resize_on_dpi_change) {
                float change = screen_client_overlaps_most->dpi_scale / client->dpi();
                client_set_size(app, client,
                                client->bounds->w * change,
                                client->bounds->h * change);
            }
            if (client->screen_information != nullptr) {
                delete client->screen_information;
                client->screen_information = nullptr;
            }
            client->screen_information = new ScreenInformation(*screen_client_overlaps_most);
            
            if (client->on_dpi_change) {
                client->on_dpi_change(app, client);
            }
        }
        // SCREEN CHANGED
        if (client->screen_information != screen_client_overlaps_most) {
            if (client->screen_information != nullptr) {
                delete client->screen_information;
                client->screen_information = nullptr;
            }
            client->screen_information = new ScreenInformation(*screen_client_overlaps_most);
            if (client->on_screen_changed) {
                client->on_screen_changed(app, client);
            }
        }
        if (client->on_screen_size_changed) {
            if (client->screen_information->width_in_pixels != screen_client_overlaps_most->width_in_pixels ||
                client->screen_information->height_in_pixels != screen_client_overlaps_most->height_in_pixels) {
                if (client->screen_information != nullptr) {
                    delete client->screen_information;
                    client->screen_information = nullptr;
                }
                client->screen_information = new ScreenInformation(*screen_client_overlaps_most);
                client->on_screen_size_changed(app, client);
            }
        }
        if (client->on_any_screen_change && !came_from_movement) {
            client->on_any_screen_change(app, client);
        }
    }
}

static bool listen_to_randr_and_client_configured_events(App *app, xcb_generic_event_t *event, xcb_window_t) {
    if (event->response_type == randr_query->first_event + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
        update_information_of_all_screens(app);
        for (auto c: app->clients) {
            check_if_client_dpi_should_change_or_if_it_was_moved_to_another_screen(app, c, false);
        }
    }
    if (event->response_type == randr_query->first_event + XCB_RANDR_NOTIFY) {
        update_information_of_all_screens(app);
        for (auto c: app->clients) {
            check_if_client_dpi_should_change_or_if_it_was_moved_to_another_screen(app, c, false);
        }
    }
    if (auto window = get_window(event)) {
        if (auto client = client_by_window(app, window)) {
            switch (XCB_EVENT_RESPONSE_TYPE(event)) {
                case XCB_CONFIGURE_NOTIFY: {
                    auto *e = (xcb_configure_notify_event_t *) event;
                    if (e->width == client->bounds->w && e->height == client->bounds->h) {
                        // PASS CONFIGURE EVENT TO THE CLIENT SO IT CAN UPDATE IT'S INTERNAL DATA
                        handle_xcb_event(app, client->window, event, false);
                        
                        check_if_client_dpi_should_change_or_if_it_was_moved_to_another_screen(app, client, true);
                        return true;
                    }
                    return false;
                }
            }
        }
    }
    
    return false; // Returning false here means this event handler does not consume the event
}

void dpi_setup(App *app) {
    // PASS US ALL EVENTS
    app_create_custom_event_handler(app, INT_MAX, listen_to_randr_and_client_configured_events);
    
    randr_query = xcb_get_extension_data(app->connection, &xcb_randr_id);
    if (!randr_query->present) {
        perror("XRandr was not present on Xorg server.\n");
        assert(false);
    }
    
    update_information_of_all_screens(app);
    for (auto c: app->clients) {
        check_if_client_dpi_should_change_or_if_it_was_moved_to_another_screen(app, c, false);
    }
    assert(!screens.empty());
    
    // Create a client that won't be shown and selects to have RandR events sent to it
    AppClient *client = client_new(app, Settings(), "hidden_client_to_be_notified_of_randr_events");
    client->keeps_app_running = false;
    assert(client != nullptr);
    
    auto xrandr_mask = XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE | XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE;
    xcb_randr_select_input(app->connection, client->window, xrandr_mask);
}

// TODO we should probably us "monitors" instead even if they don't have a concept of rotation
void update_information_of_all_screens(App *app) {
    //
    // update the screens array
    //
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(app->connection));
    xcb_generic_error_t *err = NULL;
    
    int randr_active = randr_query->present;
    int has_randr_primary = 0;
    
    int count = 0, i, j;
    
    /* Collect information first, then show. This should make the async
     * requests such as those for RANDR faster.
     */
    xcb_screen_t *screen_data = static_cast<xcb_screen_t *>(malloc(iter.rem * sizeof(*screen_data)));
    
    uint32_t rr_major = 0, rr_minor = 0;
    
    xcb_randr_query_version_cookie_t rr_ver_cookie;
    xcb_randr_query_version_reply_t *rr_ver_rep = NULL;
    if (randr_active)
        rr_ver_cookie = xcb_randr_query_version(app->connection, 1, 5);
    
    xcb_randr_get_screen_resources_cookie_t *rr_cookie = static_cast<xcb_randr_get_screen_resources_cookie_t *>(randr_active
                                                                                                                ?
                                                                                                                malloc(iter.rem *
                                                                                                                       sizeof(*rr_cookie))
                                                                                                                : NULL);
    xcb_randr_get_screen_resources_reply_t **rr_res = static_cast<xcb_randr_get_screen_resources_reply_t **>(randr_active
                                                                                                             ?
                                                                                                             calloc(iter.rem,
                                                                                                                    sizeof(*rr_res))
                                                                                                             : NULL);
    
    xcb_randr_get_output_primary_cookie_t *rr_primary_cookie = static_cast<xcb_randr_get_output_primary_cookie_t *>(randr_active
                                                                                                                    ?
                                                                                                                    malloc(iter.rem *
                                                                                                                           sizeof(*rr_primary_cookie))
                                                                                                                    : NULL);
    xcb_randr_get_output_primary_reply_t **rr_primary_reply = static_cast<xcb_randr_get_output_primary_reply_t **>(randr_active
                                                                                                                   ?
                                                                                                                   calloc(iter.rem,
                                                                                                                          sizeof(*rr_primary_reply))
                                                                                                                   : NULL);
    
    xcb_randr_crtc_t **rr_crtc = static_cast<xcb_randr_crtc_t **>(randr_active ?
                                                                  calloc(iter.rem, sizeof(*rr_crtc)) : NULL);
    xcb_randr_output_t **rr_output = static_cast<xcb_randr_output_t **>(randr_active ?
                                                                        calloc(iter.rem, sizeof(*rr_output)) : NULL);
    xcb_randr_get_crtc_info_reply_t ***rr_crtc_info = static_cast<xcb_randr_get_crtc_info_reply_t ***>(randr_active ?
                                                                                                       calloc(iter.rem,
                                                                                                              sizeof(*rr_crtc_info))
                                                                                                                    : NULL);
    xcb_randr_get_output_info_reply_t ***rr_out = static_cast<xcb_randr_get_output_info_reply_t ***>(randr_active ?
                                                                                                     calloc(iter.rem,
                                                                                                            sizeof(*rr_out))
                                                                                                                  : NULL);
    /* Monitors require RANDR 1.5 */
    if (randr_active) {
        rr_ver_rep = xcb_randr_query_version_reply(app->connection, rr_ver_cookie, &err);
        if (err) {
            fprintf(stderr, "error querying RANDR version -- %d\n", err->error_code);
            free(err);
            randr_active = 0;
        } else {
            rr_major = rr_ver_rep->major_version;
            rr_minor = rr_ver_rep->minor_version;
            if (rr_major > 1 || rr_minor >= 3)
                has_randr_primary = 1;
        }
    }
    
    if (!screen_data) {
        fputs("could not allocate memory for screen data\n", stderr);
        goto cleanup;
    }
    if (randr_active && !(rr_cookie && rr_res && rr_crtc && rr_crtc_info && rr_out)) {
        fputs("could not allocate memory for RANDR data\n", stderr);
        goto cleanup;
    }
    if (has_randr_primary && !(rr_primary_cookie && rr_primary_reply)) {
        fputs("could not allocate memory for RANDR primary output\n", stderr);
        goto cleanup;
    }
    /** Query **/
    
    /* Collect core info and query RANDR */
    for (count = 0; iter.rem; ++count, xcb_screen_next(&iter)) {
        screen_data[count] = *iter.data;
        if (randr_active)
            rr_cookie[count] = xcb_randr_get_screen_resources(app->connection,
                                                              iter.data->root);
        if (has_randr_primary)
            rr_primary_cookie[count] = xcb_randr_get_output_primary(app->connection,
                                                                    iter.data->root);
    }
    
    /** Get the actual data **/
    /* RANDR */
    if (randr_active)
        for (i = 0; i < count; ++i) {
            int num_crtcs = 0;
            int num_outputs = 0;
            
            xcb_randr_get_crtc_info_cookie_t *crtc_cookie = NULL;
            xcb_randr_get_output_info_cookie_t *output_cookie = NULL;
            
            rr_res[i] = xcb_randr_get_screen_resources_reply(app->connection,
                                                             rr_cookie[i], &err);
            if (err) {
                fprintf(stderr, "error getting resources for screen %d -- %d\n", i,
                        err->error_code);
                free(err);
                err = NULL;
                randr_active = 0;
            }
            
            if (!randr_active)
                break;
            
            if (has_randr_primary) {
                rr_primary_reply[i] = xcb_randr_get_output_primary_reply(app->connection,
                                                                         rr_primary_cookie[i], &err);
                if (err) {
                    fprintf(stderr, "error getting primary output for screen %d -- %d\n", i,
                            err->error_code);
                    free(err);
                    err = NULL;
                    randr_active = 0;
                }
            }
            
            if (!randr_active)
                break;
            
            
            num_crtcs = xcb_randr_get_screen_resources_crtcs_length(rr_res[i]);
            num_outputs = xcb_randr_get_screen_resources_outputs_length(rr_res[i]);
            
            /* Get the first crtc and output. We store the CRTC to match it to the output
             * later on. NOTE that this is not for us to free. */
            rr_crtc[i] = xcb_randr_get_screen_resources_crtcs(rr_res[i]);
            rr_output[i] = xcb_randr_get_screen_resources_outputs(rr_res[i]);
            
            /* Cookies for the requests */
            crtc_cookie = static_cast<xcb_randr_get_crtc_info_cookie_t *>(calloc(num_crtcs,
                                                                                 sizeof(xcb_randr_get_crtc_info_cookie_t)));
            output_cookie = static_cast<xcb_randr_get_output_info_cookie_t *>(calloc(num_outputs,
                                                                                     sizeof(xcb_randr_get_output_info_cookie_t)));
            
            if (!crtc_cookie || !output_cookie) {
                fputs("could not allocate memory for RANDR request cookies\n", stderr);
                break;
            }
            
            /* CRTC requests */
            for (j = 0; j < num_crtcs; ++j)
                crtc_cookie[j] = xcb_randr_get_crtc_info(app->connection, rr_crtc[i][j], 0);
            
            /* Output requests */
            for (j = 0; j < num_outputs; ++j)
                output_cookie[j] = xcb_randr_get_output_info(app->connection, rr_output[i][j], 0);
            
            /* Room for the replies */
            rr_crtc_info[i] = static_cast<xcb_randr_get_crtc_info_reply_t **>(calloc(num_crtcs,
                                                                                     sizeof(xcb_randr_get_crtc_info_reply_t *)));
            rr_out[i] = static_cast<xcb_randr_get_output_info_reply_t **>(calloc(num_outputs,
                                                                                 sizeof(xcb_randr_get_output_info_reply_t *)));
            
            if (!rr_crtc_info[i] || !rr_out[i]) {
                fputs("could not allocate memory for RANDR data\n", stderr);
                break;
            }
            
            /* Actually get the replies. */
            for (j = 0; j < num_crtcs; ++j) {
                rr_crtc_info[i][j] = xcb_randr_get_crtc_info_reply(app->connection, crtc_cookie[j], &err);
                if (err) {
                    fprintf(stderr, "error getting info for CRTC %d on screen %d -- %d\n", j, i,
                            err->error_code);
                    free(err);
                    err = NULL;
                    continue;
                }
            }
            
            for (j = 0; j < num_outputs; ++j) {
                rr_out[i][j] = xcb_randr_get_output_info_reply(app->connection, output_cookie[j], &err);
                if (err) {
                    fprintf(stderr, "error getting info for output %d on screen %d -- %d\n", j, i,
                            err->error_code);
                    free(err);
                    err = NULL;
                    continue;
                }
            }
            
            free(output_cookie);
            free(crtc_cookie);
        }
    
    for (auto screen: screens)
        delete screen;
    screens.clear();
    screens.shrink_to_fit();
    for (auto c: app->clients)
        c->screen_information = nullptr;
    
    // Kinda strange that we go through (count: screen count) and then 'sub' outputs.
    /* Show it */
    for (i = 0; i < count; ++i) {
        xcb_randr_output_t primary = -1;
        if (has_randr_primary)
            primary = rr_primary_reply[i]->output;
        
        /* XRANDR information */
        if (randr_active && rr_res[i]) {
            const xcb_randr_get_screen_resources_reply_t *rr = rr_res[i];
            for (int o = 0; o < rr->num_outputs; ++o) {
                const xcb_randr_get_output_info_reply_t *rro = rr_out[i][o];
                
                if (rro->crtc) {
                    int c = 0;
                    while (c < rr->num_crtcs) {
                        if (rr_crtc[i][c] == rro->crtc)
                            break;
                        ++c;
                    }
                    if (c < rr->num_crtcs) {
                        const xcb_randr_get_crtc_info_reply_t *rrc = rr_crtc_info[i][c];
                        uint16_t w = rrc->width;
                        uint16_t h = rrc->height;
                        
                        uint16_t rot = (rrc->rotation & 0x0f);
                        int rotated = ((rot == XCB_RANDR_ROTATION_ROTATE_90) || (rot == XCB_RANDR_ROTATION_ROTATE_270));
                        
                        uint32_t mmw = rotated ? rro->mm_height : rro->mm_width;
                        uint32_t mmh = rotated ? rro->mm_width : rro->mm_height;
                        
                        xcb_randr_output_t output = rr_output[i][o];
                        
                        // EDID is a unique property set on every monitor which might be worth using but I think for now it's fine
                        ScreenInformation *screen_information = new ScreenInformation;
                        screen_information->x = rrc->x;
                        screen_information->y = rrc->y;
                        screen_information->width_in_millimeters = mmw;
                        screen_information->height_in_millimeters = mmh;
                        screen_information->width_in_pixels = w;
                        screen_information->height_in_pixels = h;
                        screen_information->rotation = rot;
                        screen_information->is_primary = primary == output;
                        screen_information->status = rrc->status;
                        screen_information->root_window = screen_data[i].root;
                        screen_information->dpi_scale = get_dpi_scale(screen_information->height_in_pixels,
                                                                      screen_information->height_in_millimeters);
                        screens.push_back(screen_information);
                    }
                }
            }
        }
    }
    
    cleanup:
    free(screen_data);
    free(rr_cookie);
    if (randr_active)
        for (i = 0; i < count; ++i) {
            const xcb_randr_get_screen_resources_reply_t *rr = rr_res[i];
            for (int o = 0; o < rr->num_outputs; ++o)
                free(rr_out[i][o]);
            for (int c = 0; c < rr->num_crtcs; ++c)
                free(rr_crtc_info[i][c]);
            free(rr_out[i]);
            free(rr_crtc_info[i]);
            free(rr_res[i]);
        }
    free(rr_out);
    free(rr_crtc_info);
    free(rr_output);
    free(rr_crtc);
    free(rr_res);
    free(rr_ver_rep);
    free(rr_primary_cookie);
    if (has_randr_primary)
        for (i = 0; i < count; ++i) {
            free(rr_primary_reply[i]);
        }
    free(rr_primary_reply);
}
