
#include "main.h"
#include "app_menu.h"
#include "application.h"
#include "audio.h"
#include "bind_meta.h"
#include "root.h"
#include "systray.h"
#include "taskbar.h"
#include "config.h"

App *app;

int main() {
    // setenv("DISPLAY", ":1", true);

    // Open connection to app
    app = app_new();

    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }

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
    load_desktop_files();
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
