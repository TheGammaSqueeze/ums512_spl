#!/usr/bin/env bash
# Install everything needed to build the SPL on a Debian or Ubuntu host.
#
#   - aarch64 bare metal cross compiler (gcc-aarch64-linux-gnu)
#   - make, bison, flex, python3
#   - 32 bit runtime libs so the vendored tools/imgheaderinsert (i386 ELF) runs
#
# The build was verified with gcc-aarch64-linux-gnu 11.4 on Ubuntu 22.04.
set -euo pipefail

SUDO=""
if [ "$(id -u)" != "0" ]; then SUDO="sudo"; fi

$SUDO dpkg --add-architecture i386 || true
$SUDO apt-get update
$SUDO apt-get install -y \
	gcc-aarch64-linux-gnu \
	make bison flex python3 \
	libc6:i386 libstdc++6:i386 zlib1g:i386

echo
echo "Toolchain ready:"
aarch64-linux-gnu-gcc --version | head -1
