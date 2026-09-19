#!/bin/bash
# Build the STM32F7-DISCO kernel and package it as a U-Boot image for TFTP.
#
# Paths derive from this script's location; override the toolchain, host make,
# python and output image via environment variables if your setup differs:
#   TC=/path/to/arm-uclinuxeabi/bin  PYTHON=python3  UIMAGE=/srv/tftp/networking.uImage  ./build_all.sh
set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
KDIR="$TOP/linux-stm32f7-master"
MKIMAGE="$TOP/tools/mkimage.py"

# Sourcery CodeBench Lite 2010.09 (arm-uclinuxeabi), gcc 4.5.1. Override with TC=.
TC="${TC:-/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin}"
PYTHON="${PYTHON:-/c/Python314/python.exe}"
# MSYS2 make/coreutils (the default Git-Bash PATH has no make). Override MSYS_BIN=.
MSYS_BIN="${MSYS_BIN:-/c/msys64/usr/bin}"
# U-Boot netboot image destination (your TFTP root).
UIMAGE="${UIMAGE:-/c/tftp/networking.uImage}"

export PATH="$MSYS_BIN:/usr/bin:/bin:$TC:$PATH"

MAKEARGS=(ARCH=arm CROSS_COMPILE=$TC/arm-uclinuxeabi- HOSTCFLAGS="-std=gnu89 -O2")

USERCFLAGS="-Os -mcpu=cortex-m3 -mthumb -std=gnu99 -Wall"
USERLDFLAGS="-Wl,-elf2flt=-s -Wl,-elf2flt=16384"

echo "=== Building userspace tools ==="
for tool in f7fetch uart_bridge fbtest_rgb565; do
    src=$TOP/userspace/tools/$tool.c
    [ -f "$src" ] || continue
    arm-uclinuxeabi-gcc $USERCFLAGS -o "$TOP/userspace/tools/$tool" "$src" $USERLDFLAGS
    cp "$TOP/userspace/tools/$tool" "$KDIR/initdir/bin/$tool"
done
# BusyBox variants come from userspace/build_busybox.sh
for bb in busybox bbx; do
    [ -f "$TOP/userspace/$bb" ] && cp "$TOP/userspace/$bb" "$KDIR/initdir/bin/$bb"
done

cd "$KDIR"

echo "=== Updating config ==="
yes "" | make "${MAKEARGS[@]}" oldconfig >/dev/null

echo "=== Building kernel Image ==="
make -j4 "${MAKEARGS[@]}" Image

echo "=== Packaging uImage ==="
mkdir -p "$(dirname "$UIMAGE")"
"$PYTHON" "$MKIMAGE" arch/arm/boot/Image "$UIMAGE" \
    0xc0008000 0xc0008001 "Linux-2.6.33-cortexm-1.14.2"

ls -l "$UIMAGE"
