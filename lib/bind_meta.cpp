#include <X11/Xlibint.h>
#include <cstdio>
#include <cstdlib>
#include <xcb/record.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

/* for this struct, refer to libxnee */
typedef union {
    unsigned char type;
    xEvent event;
    xResourceReq req;
    xGenericReply reply;
    xError error;
    xConnSetupPrefix setup;
} XRecordDatum;

/*
 * FIXME: We need define a private struct for callback function,
 * to store cur_x, cur_y, data_disp, ctrl_disp etc.
 */
static xcb_connection_t *data_disp = NULL;
static xcb_connection_t *ctrl_disp = NULL;

/* stop flag */
static int stop = 0;

size_t
event_callback(xcb_record_enable_context_reply_t *reply, uint8_t *data_);

void watch_meta_key() {
    ctrl_disp = xcb_connect(NULL, NULL);
    data_disp = xcb_connect(NULL, NULL);

    if (xcb_connection_has_error(ctrl_disp) || xcb_connection_has_error(data_disp)) {
        fprintf(stderr, "Error to open local display!\n");
        exit(1);
    }

    const xcb_query_extension_reply_t *query_ext =
            xcb_get_extension_data(ctrl_disp, &xcb_record_id);
    if (!query_ext) {
        fprintf(stderr, "RECORD extension not supported on this X server!\n");
        exit(2);
    }

    xcb_record_query_version_reply_t *version_reply = xcb_record_query_version_reply(
            ctrl_disp,
            xcb_record_query_version(ctrl_disp, XCB_RECORD_MAJOR_VERSION, XCB_RECORD_MINOR_VERSION),
            NULL);
    if (!version_reply) {
        fprintf(stderr, "This should not happen: Can't get RECORD version\n");
        exit(2);
    }

    xcb_record_range_t rr;
    xcb_record_client_spec_t rcs;
    xcb_record_context_t rc = xcb_generate_id(ctrl_disp);

    memset(&rr, 0, sizeof(rr));
    rr.device_events.first = XCB_KEY_PRESS;
    rr.device_events.last = XCB_KEY_RELEASE;
    rcs = XCB_RECORD_CS_ALL_CLIENTS;

    xcb_void_cookie_t create_cookie =
            xcb_record_create_context_checked(ctrl_disp, rc, 0, 1, 1, &rcs, &rr);
    xcb_generic_error_t *error = xcb_request_check(ctrl_disp, create_cookie);
    if (error) {
        fprintf(stderr, "Could not create a record context!\n");
        free(error);
        exit(4);
    }

    /* The above xcb_request_check() makes sure the server already handled the
     * CreateContext request, thus this isn't needed anymore:
     * XSync(ctrl_disp, 0);
     */

    xcb_record_enable_context_cookie_t cookie = xcb_record_enable_context(data_disp, rc);

    while (!stop) {
        auto *reply =
                (xcb_record_enable_context_reply_t *) xcb_wait_for_reply(data_disp, cookie.sequence, NULL);

        if (!reply)
            break;
        if (reply->client_swapped) {
            continue;
        }

        if (reply->category == 0 /* XRecordFromServer */) {
            size_t offset = 0;
            uint8_t *data = xcb_record_enable_context_data(reply);
            while (offset < reply->length << 2) {
                offset += event_callback(reply, &data[offset]);
            }
        }
        free(reply);
    }

    xcb_record_disable_context(ctrl_disp, rc);
    xcb_record_free_context(ctrl_disp, rc);
    xcb_flush(ctrl_disp);

    xcb_disconnect(data_disp);
    xcb_disconnect(ctrl_disp);
}

void (*on_meta_key_pressed)(int num);

static int keys_down_count = 0;
static bool clean = true;
static bool num = true;

size_t
event_callback(xcb_record_enable_context_reply_t *reply, uint8_t *data_) {
    XRecordDatum *data = (XRecordDatum *) data_;

    int event_type = data->type;

    xcb_keycode_t keycode = data->event.u.u.detail;

    switch (event_type) {
        case XCB_KEY_PRESS: {
            if (clean && (keycode >= 10 && keycode <= 19)) {
                num = true;
                on_meta_key_pressed(keycode);
                break;
            }
            num = false;
            clean = keycode == 133;
            break;
        }
        case XCB_KEY_RELEASE: {
            bool is_meta = keycode == 133;
            if (is_meta && clean && !num) {
                if (on_meta_key_pressed)
                    on_meta_key_pressed(0);
                clean = false;
            }
            break;
        }
        default:
            break;
    }

    if (data_[0] == 0)
        /* reply */
        return ((*(uint32_t *) &data_[4]) + 8) << 2;
    /* Error or event TODO: What about XGE events? */
    return 32;
}
