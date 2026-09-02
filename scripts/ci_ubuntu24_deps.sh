#!/usr/bin/env bash
# Install build dependencies on Ubuntu 24.04 (CI and local).
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
  build-essential \
  git \
  curl \
  patchelf \
  autoconf \
  automake \
  libtool \
  pkg-config \
  qtbase5-dev \
  qt5-qmake \
  qttools5-dev \
  libqt5svg5-dev \
  qtmultimedia5-dev \
  libqt5xmlpatterns5-dev \
  zlib1g-dev \
  libpng-dev \
  libcurl4-openssl-dev \
  libexpat1-dev \
  libx11-dev \
  libxcb1-dev \
  libgl1-mesa-dev

if command -v qmake-qt5 >/dev/null 2>&1; then
  ln -sf "$(command -v qmake-qt5)" /usr/local/bin/qmake 2>/dev/null || true
fi

qmake -v || qmake-qt5 -v
