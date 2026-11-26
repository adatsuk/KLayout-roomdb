#!/usr/bin/env bash
# Install build dependencies on RHEL 8 / Rocky Linux 8 / AlmaLinux 8 (CI and local containers).
set -euo pipefail

dnf install -y dnf-plugins-core
dnf config-manager --set-enabled powertools 2>/dev/null \
  || dnf config-manager --set-enabled crb 2>/dev/null \
  || true
dnf install -y epel-release 2>/dev/null || true

dnf install -y \
  gcc gcc-c++ make git which curl patchelf \
  autoconf automake libtool \
  python3 python3-devel \
  qt5-qtbase-devel qt5-qtsvg-devel qt5-qttools-devel \
  qt5-qtmultimedia-devel qt5-qtxmlpatterns-devel \
  zlib-devel libpng-devel libcurl-devel expat-devel \
  libX11-devel libxcb-devel mesa-libGL-devel

# qmake from Qt5
if command -v qmake-qt5 >/dev/null 2>&1; then
  ln -sf "$(command -v qmake-qt5)" /usr/local/bin/qmake 2>/dev/null || true
fi

qmake -v || qmake-qt5 -v
