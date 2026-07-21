#!/bin/sh
# Run this on a fresh GhostBSD installation to set up orbiter + i3
set -e

echo "==> Installing build dependencies..."
sudo pkg install -y \
  git \
  cmake \
  pkgconf \
  xcb \
  xcb-util \
  xcb-util-icccm \
  xcb-util-keysyms \
  xcb-util-wm \
  cairo \
  pango \
  librsvg2 \
  libxkbcommon \
  libxkbcommon-x11 \
  i3 \
  xterm \
  feh

echo "==> Cloning orbiter..."
cd /tmp
git clone git@github.com:steven66619/orbiter.git
cd orbiter

echo "==> Building orbiter..."
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
sudo cp orbiter /usr/local/bin/

echo "==> Installing i3 config..."
mkdir -p ~/.config/i3
cp ../examples/i3/config ~/.config/i3/config

echo "==> Done!"
echo ""
echo "Log out and select i3 from the session menu."
echo "Press Mod4+d to open orbiter."
