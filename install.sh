#!/usr/bin/env bash

# fail out on any error
set -e

#make and enter the build directory if it doesn't exist already
mkdir -p newbuild
cd newbuild

#let cmake find dependencies on system
# NEEDS TO BE SUDO SO IT CAN INSTALL RESOURCES
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ../

#actually compile
make -j 16

echo "Trying to install to /usr/bin/winbar"

sudo make -j 16 install

# Create cache if it doesn't exist
if [[ ! -f ~/.cache/winbar_icon_cache/icon.cache ]]; then
  ./winbar --create-cache
fi

cd ../
unzip -o winbar.zip
sudo mkdir -p /usr/share/winbar
sudo cp -R winbar/fonts /usr/share/winbar
sudo cp -R winbar/resources /usr/share/winbar
sudo cp -R winbar/plugins /usr/share/winbar
sudo cp winbar/tofix.csv /usr/share/winbar
sudo cp winbar/items_custom.ini /usr/share/winbar
sudo cp winbar/winbar.cfg /etc
sudo rm -r winbar

# (old install location which should be deleted if still exists)
if [[ -f /usr/local/bin/winbar ]]; then
  sudo rm /usr/local/bin/winbar
fi

