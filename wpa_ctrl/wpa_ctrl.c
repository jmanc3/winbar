/*
 * wpa_supplicant/hostapd control interface library
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

/*
 * wpa_supplicant/hostapd - Default include files
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This header file is included into all C files so that commonly used header
 * files can be selected with OS specific ifdef blocks in one place instead of
 * having to have OS/C library specific selection in many files.
 */
#include "wpa_ctrl.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/**
 * struct wpa_ctrl - Internal structure for control interface library
 *
 * This structure is used by the wpa_supplicant/hostapd control interface
 * library to store internal data. Programs using the library should not touch
 * this data directly. They can only use the pointer to the data structure as
 * an identifier for the control interface connection and use this as one of
 * the arguments for most of the control interface library functions.
 */
struct wpa_ctrl {
    int s;
    struct sockaddr_un local;
    struct sockaddr_un dest;
};

size_t
strlcpy(char *__restrict dst, const char *__restrict src, size_t siz) {
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';        /* NUL-terminate dst */
        while (*s++);
    }

    return (s - src - 1);    /* count does not include NUL */
}

struct wpa_ctrl *wpa_ctrl_open(const char *ctrl_path) {
    struct wpa_ctrl *ctrl;
    static int counter = 0;
    int ret;
    size_t res;
    int tries = 0;

    ctrl = malloc(sizeof(*ctrl));
    if (ctrl == NULL)
        return NULL;
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->s = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (ctrl->s < 0) {
        free(ctrl);
        return NULL;
    }

    ctrl->local.sun_family = AF_UNIX;
    counter++;
    try_again:
    ret = snprintf(ctrl->local.sun_path, sizeof(ctrl->local.sun_path),
                   "/tmp/wpa_ctrl_%d-%d", getpid(), counter);
    if (ret < 0 || (size_t) ret >= sizeof(ctrl->local.sun_path)) {
        close(ctrl->s);
        free(ctrl);
        return NULL;
    }
    tries++;
    if (bind(ctrl->s, (struct sockaddr *) &ctrl->local,
             sizeof(ctrl->local)) < 0) {
        if (errno == EADDRINUSE && tries < 2) {
            /*
             * getpid() returns unique identifier for this instance
             * of wpa_ctrl, so the existing socket file must have
             * been left by unclean termination of an earlier run.
             * Remove the file and try again.
             */
            unlink(ctrl->local.sun_path);
            goto try_again;
        }
        close(ctrl->s);
        free(ctrl);
        return NULL;
    }

    ctrl->dest.sun_family = AF_UNIX;
    res = strlcpy(ctrl->dest.sun_path, ctrl_path,
                  sizeof(ctrl->dest.sun_path));
    if (res >= sizeof(ctrl->dest.sun_path)) {
        close(ctrl->s);
        free(ctrl);
        return NULL;
    }
    if (connect(ctrl->s, (struct sockaddr *) &ctrl->dest,
                sizeof(ctrl->dest)) < 0) {
        close(ctrl->s);
        unlink(ctrl->local.sun_path);
        free(ctrl);
        return NULL;
    }

    return ctrl;
}


void wpa_ctrl_close(struct wpa_ctrl *ctrl) {
    unlink(ctrl->local.sun_path);
    close(ctrl->s);
    free(ctrl);
}

int wpa_ctrl_request(struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len,
                     char *reply, size_t *reply_len,
                     void (*msg_cb)(char *msg, size_t len)) {
    struct timeval tv;
    int res;
    fd_set rfds;
    const char *_cmd;
    char *cmd_buf = NULL;
    size_t _cmd_len;

    {
        _cmd = cmd;
        _cmd_len = cmd_len;
    }

    if (send(ctrl->s, _cmd, _cmd_len, 0) < 0) {
        free(cmd_buf);
        return -1;
    }
    free(cmd_buf);

    for (;;) {
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        FD_ZERO(&rfds);
        FD_SET(ctrl->s, &rfds);
        res = select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
        if (FD_ISSET(ctrl->s, &rfds)) {
            res = recv(ctrl->s, reply, *reply_len, 0);
            if (res < 0)
                return res;
            if (res > 0 && reply[0] == '<') {
                /* This is an unsolicited message from
                 * wpa_supplicant, not the reply to the
                 * request. Use msg_cb to report this to the
                 * caller. */
                if (msg_cb) {
                    /* Make sure the message is nul
                     * terminated. */
                    if ((size_t) res == *reply_len)
                        res = (*reply_len) - 1;
                    reply[res] = '\0';
                    msg_cb(reply, res);
                }
                continue;
            }
            *reply_len = res;
            break;
        } else {
            return -2;
        }
    }
    return 0;
}

static int wpa_ctrl_attach_helper(struct wpa_ctrl *ctrl, int attach) {
    char buf[10];
    int ret;
    size_t len = 10;

    ret = wpa_ctrl_request(ctrl, attach ? "ATTACH" : "DETACH", 6,
                           buf, &len, NULL);
    if (ret < 0)
        return ret;
    if (len == 3 && memcmp(buf, "OK\n", 3) == 0)
        return 0;
    return -1;
}


int wpa_ctrl_attach(struct wpa_ctrl *ctrl) {
    return wpa_ctrl_attach_helper(ctrl, 1);
}


int wpa_ctrl_detach(struct wpa_ctrl *ctrl) {
    return wpa_ctrl_attach_helper(ctrl, 0);
}


int wpa_ctrl_recv(struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
    int res;

    res = recv(ctrl->s, reply, *reply_len, 0);
    if (res < 0)
        return res;
    *reply_len = res;
    return 0;
}


int wpa_ctrl_pending(struct wpa_ctrl *ctrl) {
    struct timeval tv;
    fd_set rfds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(ctrl->s, &rfds);
    select(ctrl->s + 1, &rfds, NULL, NULL, &tv);
    return FD_ISSET(ctrl->s, &rfds);
}


int wpa_ctrl_get_fd(struct wpa_ctrl *ctrl) {
    return ctrl->s;
}
