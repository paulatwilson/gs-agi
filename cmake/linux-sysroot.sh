#!/bin/sh
# Fetch the Linux headers and stub-link libraries SDL2 needs to build its
# X11 and ALSA backends when cross-compiling (cmake/zig-linux.cmake).
# SDL dlopen()s the libraries at run time; the build only needs them to
# detect sonames.
# Usage: cmake/linux-sysroot.sh [x86_64|aarch64]   (default: x86_64)
# Output: build-linux-sysroot/<arch>/
set -e
arch=${1:-x86_64}
case $arch in
x86_64)  deb=amd64 ;;
aarch64) deb=arm64 ;;
*) echo "unknown arch: $arch" >&2; exit 1 ;;
esac
mirror=https://deb.debian.org/debian
suite=bookworm
pkgs="libx11-dev libx11-6 x11proto-dev libxext-dev libxext6 libxrandr-dev libxrandr2
 libxrender-dev libxrender1 libxcursor-dev libxcursor1 libxfixes-dev libxfixes3
 libxi-dev libxi6 libxss-dev libxss1 libxcb1-dev libxcb1 libxau-dev libxau6
 libxdmcp-dev libxdmcp6 libasound2-dev libasound2 xtrans-dev"

root=$(cd "$(dirname "$0")/.." && pwd)/build-linux-sysroot
out=$root/$arch
mkdir -p "$out" "$root/debs-$deb"
[ -f "$root/Packages-$deb" ] ||
	curl -fsSL "$mirror/dists/$suite/main/binary-$deb/Packages.xz" | xz -dc > "$root/Packages-$deb"
for p in $pkgs; do
	f=$(awk -v p="$p" '$1=="Package:"{m=($2==p)} m&&$1=="Filename:"{print $2; exit}' "$root/Packages-$deb")
	[ -n "$f" ] || { echo "not in index: $p" >&2; exit 1; }
	b=$(basename "$f")
	[ -f "$root/debs-$deb/$b" ] || curl -fsSL -o "$root/debs-$deb/$b" "$mirror/$f"
	tmp=$(mktemp -d)
	(cd "$tmp" && ar x "$root/debs-$deb/$b" && tar -xf data.tar.* -C "$out")
	rm -rf "$tmp"
done
echo "sysroot: $out"
