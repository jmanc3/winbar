#!/usr/bin/env bash

#exit when any of the following commands fails
set -e

# Check for non-root permissions.
if [ "$(id -u)" -eq 0 ]; then
    echo "Should be ran as normal user, not root. Want to continue anyways? (y/n)"
    read -n 1 response
    if [ "$response" == "y" ] || [ "$response" == "Y" ]; then
        printf "\nContinuing...\n"
    else
        exit
    fi
fi

#make and enter the build directory if it doesn't exist already
mkdir -p newbuild
cd newbuild

#let cmake find dependencies on system
cmake -DCMAKE_BUILD_TYPE=Release ../

#actually compile
make -j 12

echo "Trying to install to /usr/local/bin/winbar"

# install
if [[ -f /usr/bin/dpkg ]]; then                 # debian based distro
  sudo checkinstall --default --install=yes
else
  sudo make -j 12 install
fi

# Create cache if it doesn't exist
if [[ ! -f ~/.cache/winbar_icon_cache/icon.cache ]]; then
  ./winbar --create-cache
fi

