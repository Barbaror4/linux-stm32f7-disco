#!/bin/bash
#
# setup-busybox.sh - fetch BusyBox 1.24.2 and apply this project's patches.
#
# The upstream BusyBox tree is not committed to this repo; only our config
# fragments (busybox-*.frag), the two-binary build script (build_busybox.sh)
# and the source patches (patches/) are. Run this once, then build_busybox.sh.
#
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/busybox-1.24.2"
TARBALL=busybox-1.24.2.tar.bz2
URL="https://busybox.net/downloads/$TARBALL"

if [ -d "$SRC" ]; then
	echo "$SRC already exists; remove it to re-extract. Skipping download."
else
	echo ">> downloading $URL"
	curl -fL -o "$HERE/$TARBALL" "$URL"
	echo ">> extracting"
	tar xjf "$HERE/$TARBALL" -C "$HERE"
	rm -f "$HERE/$TARBALL"
fi

echo ">> applying patches"
for p in "$HERE"/patches/busybox-*.patch; do
	# -p1, tolerate already-applied
	if patch -d "$SRC" -p1 -N --dry-run < "$p" >/dev/null 2>&1; then
		patch -d "$SRC" -p1 -N < "$p"
		echo "   applied $(basename "$p")"
	else
		echo "   skipped $(basename "$p") (already applied?)"
	fi
done

echo ">> done. Now run:  ./build_busybox.sh all"
