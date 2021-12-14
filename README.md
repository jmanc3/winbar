# Winbar

A familiar X11 panel/dock to ease new linux users transition
![Screenshots of the taskbar and some menus](screenshots/1.png)

## Packages required for building

* Void Linux

```bash
sudo xbps-install -S git gcc cmake make pkg-config pango-devel cairo-devel librsvg-devel libxcb-devel xcb-util-devel pulseaudio-devel xcb-util-wm-devel libxkbcommon-devel libxkbcommon-x11 libconfig++-devel xcb-util-keysyms-devel xcb-util-image-devel xcb-util-cursor-devel dbus-devel alsa-lib-devel papirus-icon-theme lxappearance unzip
```

* Arch Linux

```bash
sudo pacman -S git gcc cmake make pkg-config pango cairo librsvg libxcb xcb-util pulseaudio xcb-util-wm libxkbcommon libxkbcommon-x11 libconfig xcb-util-keysyms xcb-util-image xcb-util-cursor dbus alsa-lib papirus-icon-theme lxappearance unzip
```

* Ubuntu

```bash
sudo apt install git g++ make cmake checkinstall pkg-config libpango1.0-dev libcairo2-dev librsvg2-dev libxcb1-dev libxcb-util-dev libpulse-dev libxkbcommon-dev libxkbcommon-x11-dev libconfig++-dev libxcb-keysyms1-dev libxcb-image0-dev papirus-icon-theme lxappearance unzip libxcb-randr0-dev libxcb-record0-dev libxcb-ewmh-dev libxcb-icccm4-dev libx11-xcb-dev libxcb-cursor-dev libdbus-1-dev libasound2-dev
```

* Fedora

```bash
sudo yum install git cmake g++ cairo-devel pango-devel librsvg2-devel xcb-util-devel pulseaudio-libs-devel xcb-util-wm-devel libxkbcommon-x11-devel libconfig-devel xcb-util-cursor-devel dbus-devel xcb-util-keysyms-devel alsa-lib-devel
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

* Finally once you've done everything above, run the install script as root.

```bash
sudo ./install.sh
``` 

If compilation fails, it should tell you what headers are missing and you can look up what you need to install for your
distribution to get that library.

## Recommendations

* It's recommended you set Papirus as your systems icon theme (you can use something like lxappearance to do that)

* The default font set in ~/.config/winbar/winbar.cfg is "Segoe UI" so either have that on your system or set it to something else

* There are four themes available default, light, dark, and dark-clear. Set your preferred in ~/.config/winbar/winbar.cfg

* Winbar is mainly tested against KDE, Openbox, and Gnome (in that order) but it should work with other EWMH compliant window managers.

* Change the value of the interface variable in the config file so winbar can display correct network status information. (~/.config/winbar/winbar.cfg)
