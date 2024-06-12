#!/usr/bin/env bash

#exit when any of the following commands fails
set -e

#make and enter the build directory if it doesn't exist already
mkdir -p newbuild
cd newbuild

#let cmake find dependencies on system
# NEEDS TO BE SUDO SO IT CAN INSTALL RESOURCES
sudo cmake -DCMAKE_BUILD_TYPE=Release ../

#actually compile
make -j 16

echo "Trying to install to /usr/local/bin/winbar"

sudo mkdir -p /usr/local/bin
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

