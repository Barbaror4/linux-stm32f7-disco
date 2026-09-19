#!/bin/bash
# Build the two BusyBox variants for the STM32F7-DISCO:
#   busybox  - small core (init, hush, coreutils, mount, switch_root, net
#              basics); every process needs a contiguous RAM block on NOMMU,
#              so this one is kept below a 256 KB (order-6) allocation.
#   bbx      - everything else (vi, tar, gzip, fdisk, mkfs.*, telnetd, ...)
#              reached through shell functions defined in /etc/profile.
#
# Usage: build_busybox.sh [core|ext|all]   (default all)
set -e

TC=/c/Users/cfylmz/Downloads/arm-toolchain/arm-2010.09/bin
TOP=/c/Users/cfylmz/Downloads/linux-stm32f7-master/userspace
SRC=$TOP/busybox-1.24.2
export PATH="/c/msys64/usr/bin:/usr/bin:/bin:$TC:$PATH"

build_variant() {
	local name=$1 frag=$2 out=$3

	cd "$SRC"
	# The applet set differs completely between variants, so build in-tree
	# from a clean state each time.
	make distclean >/dev/null 2>&1 || true
	make allnoconfig >/dev/null 2>&1
	# kconfig keeps the first definition of a symbol, so the fragment
	# goes in front of the allnoconfig baseline.
	grep -oE "^CONFIG_[A-Z0-9_]+" "$frag" | sort -u > .fragsyms
	grep -vE "^(# )?($(paste -sd'|' .fragsyms))( |=)" .config > .config.base
	cat "$frag" .config.base > .config
	rm -f .fragsyms .config.base
	yes "" | make oldconfig >/dev/null 2>&1

	echo "=== building busybox variant '$name' ==="
	make -j4 SKIP_STRIP=y HOSTCFLAGS="-std=gnu89 -O2" 2>&1 		| grep -E " error|Error " || true
	[ -f busybox ] || { echo "build of $name failed"; exit 1; }
	cp busybox "$out"
	cp .config "$TOP/busybox-$name.config"
	arm-uclinuxeabi-flthdr "$out" | grep -E "Data Start|Data End|BSS End|Stack"
	ls -l "$out"
}

what=${1:-all}
[ "$what" = core ] || [ "$what" = all ] && \
	build_variant core "$TOP/busybox-core.frag" "$TOP/busybox"
[ "$what" = ext ] || [ "$what" = all ] && \
	build_variant ext "$TOP/busybox-ext.frag" "$TOP/bbx"
exit 0
