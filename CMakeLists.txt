cmake_minimum_required(VERSION 3.14)
set(project_name winbar)
project(${project_name})

set(CMAKE_CXX_STANDARD 17) ## or 14 !!
set(CMAKE_CXX_EXTENSIONS OFF) ## on g++ this ensures: -std=c++11 and not -std=gnu++11

find_package(Threads)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

file(GLOB HEADERS src/*.h)
file(GLOB SOURCES src/*.cpp)

include_directories(lib)
file(GLOB LIB lib/*.cpp lib/*.h)

include_directories(wpa_ctrl)
file(GLOB WPA_CTRL wpa_ctrl/*.c wpa_ctrl/*.h)

option(PROFILE "Enable tracy profiling instrumentation" False)

if (PROFILE)
    # compile with profiling enabled
    # run git clone https://github.com/wolfpld/tracy.git if you want to do this
    add_definitions(-DTRACY_ENABLE)
    add_executable(${project_name} ${HEADERS} ${SOURCES} ${LIB} ${WPA_CTRL} ../tracy/TracyClient.cpp)

    set(DL_LIB dl)
    set(PTHREAD_LIB pthread)
    target_link_libraries(${project_name} PUBLIC ${PTHREAD_LIB} ${DL_LIB})
else ()
    # otherwise just compile
    add_executable(${project_name} ${HEADERS} ${SOURCES} ${LIB} ${WPA_CTRL})
endif ()

find_package(PkgConfig)

if (NOT PkgConfig_FOUND)
    message(FATAL_ERROR "You need to have pkg-config installed. On voidlinux, sudo xbps-install pkg-config")
endif ()

function(try_to_add_dependency lib_name)
    if (${lib_name}_FOUND)
        target_link_libraries(${project_name} PUBLIC ${${lib_name}_LIBRARIES})
        target_include_directories(${project_name} PUBLIC ${${lib_name}_INCLUDE_DIRS})
        target_compile_options(${project_name} PUBLIC ${${lib_name}_CFLAGS_OTHER})
    else ()
        message(FATAL_ERROR "Could not find: ${lib_name}.\
                             Make sure your system has it installed.")
    endif ()
endfunction(try_to_add_dependency)

#[[ just keeping this here in case we ever need to use them
set(LIBS pangocairo;pango;gobject-2.0;glib-2.0;cairo; # Pango libraries (mainly for font rendering and cairo for image rendering)
        x11;xcomposite;xrender;xext;xdamage; # xlib headers so we can communicate with xserver
        librsvg-2.0;libconfig++;
        xcb;xcb-util;xcb-icccm;x11-xcb;xcb-ewmh;xcb-aux;libpulse;
        xkbcommon-x11;xcb-xkb;xcb-keysyms;xcb-randr;xcb-image;xcb-errors;xcb-record) # svg library and config file parser library
]]

set(
        LIBS
        cairo # for painting rectangles and stuff
        pango # for loading fonts and laying them out
        librsvg-2.0 # to load svg icons
        xcb # to open windows with the Xorg server
        pangocairo # to paint pango text layouts using cairo
        xcb-randr # to gain brightness control
        xcb-aux # to force the Xorg server to really finish with all the events before xcb_sync_aux called
        xcb-record # to open the app_menu when the windows key is pressed
        libpulse # to be able to modify audio volume and mute state
        xcb-ewmh # to make it easier to handle extended windows manager hints requests
        xcb-icccm # to make it easier to handle icccm request
        xcb-xkb # to handle translating key presses to actual text
        xkbcommon-x11 # to handle translating key presses to actual text
        libconfig++ # to parse config files
        xcb-cursor # for setting the cursor
        dbus-1
)

foreach (LIB IN LISTS LIBS)
    pkg_check_modules(D_${LIB} ${LIB})
    try_to_add_dependency(D_${LIB} ${LIB})
endforeach ()

# install ${project_name} executable to /usr/local/bin/${project_name}
#
install(TARGETS ${project_name}
        DESTINATION /usr/local/bin/)
