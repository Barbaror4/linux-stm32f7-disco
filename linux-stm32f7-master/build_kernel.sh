#!/bin/bash
set -e
export PATH="/usr/bin:/bin:/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin"
cd /c/Users/cfylmz/Downloads/linux-stm32f7-master/linux-stm32f7-master

export ARCH=arm
export CROSS_COMPILE=/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin/arm-uclinuxeabi-

echo "=== Updating Config ==="
yes "" | make oldconfig

echo "=== Building Kernel Image ==="
make -j4 HOSTCFLAGS="-std=gnu89 -O2" Image

echo "=== Packaging uImage ==="
/c/Python314/python.exe /c/Users/cfylmz/.gemini/antigravity/brain/316cb13d-2b91-420b-bf38-134dc17053b0/scratch/mkimage.py \
    arch/arm/boot/Image \
    /c/tftp/networking.uImage \
    0xc0008000 0xc0008001 "Linux-2.6.33-cortexm-1.14.2"

cp /c/tftp/networking.uImage /c/Users/cfylmz/Downloads/networking.uImage

echo "=== Build & Package Complete! ==="
ls -l /c/tftp/networking.uImage
