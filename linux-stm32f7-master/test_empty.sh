#!/bin/bash
set -e
export PATH="/usr/bin:/bin:/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin"
cd /c/Users/cfylmz/Downloads/linux-stm32f7-master/linux-stm32f7-master
export ARCH=arm
export CROSS_COMPILE=/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin/arm-uclinuxeabi-

make V=1 scripts/mod/empty.o
