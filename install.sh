#!/usr/bin/env bash

#exit when any of the following commands fails
set -e

#make and enter the build directory if it doesn't exist already
mkdir -p build
cd build

#let cmake find dependencies on system
cmake -DCMAKE_BUILD_TYPE=Release ../

#actually compile
make

# install
if [[ -f /usr/bin/dpkg ]]; then                 # debian based distro
  sudo checkinstall --default --install=yes
else
  sudo make -j 4 install
fi
