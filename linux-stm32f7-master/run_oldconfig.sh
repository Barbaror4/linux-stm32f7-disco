#!/bin/bash
set -e
export PATH="/usr/bin:/bin:/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin"
echo "Make is: $(which make)"
echo "GCC is: $(which gcc)"
echo "ARM GCC is: $(which arm-uclinuxeabi-gcc)"

cd /c/Users/cfylmz/Downloads/linux-stm32f7-master/linux-stm32f7-master
export ARCH=arm
export CROSS_COMPILE=/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin/arm-uclinuxeabi-

make HOSTCFLAGS="-std=gnu89 -O2" oldconfig </dev/null
