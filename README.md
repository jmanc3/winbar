## Before building
Make sure pkg-config can find these libs on your system

Requirements | Reason
------------ | -------------
git | to clone this repository
cmake | to find required dependencies on your system
make | to compile generated make files from cmake
gcc | a c++ compiler
pkg-config | to find packages
cairo | to paint rectangles
pango | to load fonts and lay them out
librsvg-2.0 | to load svg images
xcb | to open windows with the Xorg server
pangocairo | to paint pango text layouts using cairo
xcb-randr | to gain brightness control
xcb-aux | to really flush the Xorg server
xcb-record | to bind the windows key
pulseaudio-devel | to control pulseaudio
xcb-ewmh | to make it easier to handle extended windows manager hints requests
xcb-icccm | to make it easier to handle icccm requests
xcb-xkb | to handle translating key presses to actual text
xkbcommon-x11 | to handle translating key presses to actual text
libconfig++ | to parse config files
xcb-keysyms | to translate keysyms
xcb-image | to manipulate images

On voidlinux you can run the following to get set up for compiling
```bash
sudo xbps-install -S git gcc cmake make pango-devel cairo-devel librsvg-devel libxcb-devel xcb-util-devel pulseaudio-devel xbc-util-wm-devel libxkbcommon-devel libxkbcommon-x11 libconfig++-devel xcb-util-keysyms-devel xcb-util-image-devel
```

## Installation
Make sure you have all the requirements laid out above. If compilation fails, it should tell you what you're missing
```bash
git clone https://github.com/jmanc3/winbar
cd winbar
./install.sh
```

