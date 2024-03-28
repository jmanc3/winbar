# Winbar

A familiar X11 panel/dock to ease new linux users transition

https://user-images.githubusercontent.com/25911177/154002452-446eb086-9453-480a-bb70-4703b500946b.mp4

## Packages required for building

* Void Linux

```bash
sudo xbps-install -S git gcc cmake make pkg-config pango-devel cairo-devel librsvg-devel libxcb-devel xcb-util-devel pulseaudio-devel xcb-util-wm-devel libxkbcommon-devel libxkbcommon-x11 libconfig++-devel xcb-util-keysyms-devel xcb-util-image-devel xcb-util-cursor-devel dbus-devel fontconfig-devel alsa-lib-devel papirus-icon-theme lxappearance unzip
```

* Arch Linux

```bash
sudo pacman -S git gcc cmake make pkg-config pango cairo librsvg libxcb xcb-util pulseaudio xcb-util-wm libxkbcommon libxkbcommon-x11 libconfig xcb-util-keysyms xcb-util-image xcb-util-cursor dbus fontconfig alsa-lib papirus-icon-theme lxappearance unzip
```

* Ubuntu

```bash
sudo apt install git g++ make cmake checkinstall pkg-config libpango1.0-dev libcairo2-dev librsvg2-dev libxcb1-dev libxcb-util-dev libpulse-dev libxkbcommon-dev libxkbcommon-x11-dev libconfig++-dev libxcb-keysyms1-dev libxcb-image0-dev papirus-icon-theme lxappearance unzip libxcb-randr0-dev libxcb-record0-dev libxcb-ewmh-dev libxcb-icccm4-dev libx11-xcb-dev libxcb-cursor-dev libdbus-1-dev libfontconfig1-dev libasound2-dev libxcb-xinput-dev libxcb-xinput0
```

* Debian

```bash
sudo apt install git g++ make cmake checkinstall pkg-config libpango1.0-dev libcairo2-dev librsvg2-dev libxcb1-dev libxcb-util-dev libpulse-dev libxkbcommon-dev libxkbcommon-x11-dev libconfig++-dev libxcb-keysyms1-dev libxcb-image0-dev papirus-icon-theme lxappearance unzip libxcb-randr0-dev libxcb-record0-dev libxcb-ewmh-dev libxcb-icccm4-dev libx11-xcb-dev libxcb-cursor-dev libdbus-1-dev libfontconfig1-dev libasound2-dev libcurl4 libcurl4-openssl-dev libxcb-xinput-dev libxcb-xinput0
```

* Fedora

```bash
sudo yum install git cmake g++ cairo-devel pango-devel librsvg2-devel xcb-util-devel pulseaudio-libs-devel xcb-util-wm-devel libxkbcommon-x11-devel libconfig-devel xcb-util-cursor-devel dbus-devel fontconfig-devel xcb-util-keysyms-devel alsa-lib-devel
```

## Installation

* Download the source and enter the folder

```bash
git clone https://github.com/jmanc3/winbar
cd winbar
```

* Put the resources and config where they are needed or you'll have missing icons

```bash
unzip winbar.zip -d ~/.config
```

* Finally once you've done everything above, run the install script.

```bash
./install.sh
``` 

If compilation fails, it should tell you what headers are missing and you can look up what you need to install for your
distribution to get that library.

## Changes you might want to make

* The configuration file is located at $HOME/.config/winbar/winbar.cfg

* You can change the DPI through the "dpi" variable in the configuration file.

## Recommended Setup

Winbar is just a taskbar. You'll need to run it on a desktop environment or windows manager. The recommended desktop environment is KDE Plasma (kwin) because it allows winbar access to features it wouldn't otherwise have: Slide animations when menu's open, window preview on hovering a thumbnail, working 'Sign out'/'Shutdown'/'Restart' buttons, and (if you activate 'blur' in your KDE settings) a nice blur which is close to 'acrylic.'

To autostart winbar with your computer, do the following:

```bash
mkdir -p ~/.config/autostart/
```

And then, inside that folder, write the following, and save it as 'winbar.desktop': 

```bash
[Desktop Entry]
Exec=winbar
Icon=
Name=winbar
Path=
Terminal=False
Type=Application
```

Winbar should now auto start with your computer. You'll want to remove the default KDE panel by right clicking your desktop wallpaper and entering edit mode.
