

#include "main.h"
#include "app_menu.h"
#include "application.h"
#include "audio.h"
#include "bind_meta.h"
#include "root.h"
#include "systray.h"
#include "taskbar.h"
#include "config.h"

#include "wpa_ctrl.h"

App *app;

static int wpa_ctrl_command(struct wpa_ctrl *ctrl, char *cmd, int print) {
    char buf[4096];
    size_t len;
    int ret;

    len = sizeof(buf) - 1;
    ret = wpa_ctrl_request(ctrl, cmd, strlen(cmd), buf, &len,
                           NULL);
    if (ret == -2) {
        printf("'%s' command timed out.\n", cmd);
        return -2;
    } else if (ret < 0) {
        printf("'%s' command failed.\n", cmd);
        return -1;
    }
    if (print) {
        buf[len] = '\0';
        printf("%s", buf);
        if (true && len > 0 && buf[len - 1] != '\n')
            printf("\n");
    }
    return 0;
}

static inline bool snprintf_error(int ret, size_t buf_len) {
    return ret < 0 || ret >= buf_len;
}

static int write_cmd(char *buf, size_t buflen, const char *cmd, int argc,
                     char *argv[]) {
    int i, res;
    char *pos, *end;

    pos = buf;
    end = buf + buflen;

    res = snprintf(pos, end - pos, "%s", cmd);
    if (snprintf_error(end - pos, res))
        goto fail;
    pos += res;

    for (i = 0; i < argc; i++) {
        res = snprintf(pos, end - pos, " %s", argv[i]);
        if (snprintf_error(end - pos, res))
            goto fail;
        pos += res;
    }

    buf[buflen - 1] = '\0';
    return 0;

    fail:
    printf("Too long command\n");
    return -1;
}


static int wpa_cli_cmd(struct wpa_ctrl *ctrl, const char *cmd, int min_args,
                       int argc, char *argv[]) {
    char buf[4096];
    if (argc < min_args) {
        printf("Invalid %s command - at least %d argument%s "
               "required.\n", cmd, min_args,
               min_args > 1 ? "s are" : " is");
        return -1;
    }
    if (write_cmd(buf, sizeof(buf), cmd, argc, argv) < 0)
        return -1;
    return wpa_ctrl_command(ctrl, buf, 1);
}

int main() {
    // Open connection to app
    app = app_new();

    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }/*
    wpa_ctrl *ctrl = wpa_ctrl_open("/var/run/wpa_supplicant/wlp7s0");
    if (ctrl) {
        wpa_ctrl_command(ctrl, "LIST_NETWORKS", 1);
        wpa_ctrl_command(ctrl, "ADD_NETWORK", 1);
        wpa_ctrl_command(ctrl, "SET_NETWORK 2 ssid \"CRAZY\"", 1);
        wpa_ctrl_command(ctrl, "SET_NETWORK 2 ssid \"CRAZY\"", 1);
        wpa_ctrl_command(ctrl, "LIST_NETWORKS", 1);

        wpa_ctrl_close(ctrl);
    }
*/

    // Load the config
    config_load();

    // Add listeners and grabs on the root window
    root_start(app);

    // Start the pulseaudio connection
    audio_start();

    // We need to register as the systray
    start_systray();

    // Open our windows
    AppClient *taskbar = create_taskbar(app);

    // We only want to load the desktop files once at the start of the program
    //std::thread(load_desktop_files).detach();
    load_all_desktop_files();
    load_scripts();// The scripts are reloaded every time the search_menu window closes
    load_historic_scripts();
    load_historic_apps();

    client_show(app, taskbar);
    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, taskbar->window, XCB_CURRENT_TIME);

    on_meta_key_pressed = meta_pressed;

    // Start our listening loop until the end of the program
    app_main(app);

    // Clean up
    app_clean(app);

    audio_stop();

    return 0;
}
