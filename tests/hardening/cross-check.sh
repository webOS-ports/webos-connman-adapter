#!/bin/bash
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Compile the adapter's changed sources and the unit tests with GCC and
# Clang for armv7, aarch64 and x86-64: six builds, warnings printed with
# the build they came from.
#
# Usage: cross-check.sh [-b LUNEOS_BUILDDIR] [-w WORKDIR] [-r BASE] [-a]
#
#   -r   the ref to compare with (default: the merge-base of HEAD and
#        origin/webOS-ports/webOS-OSE); files changed since then are built
#   -a   every source file and every test, and every warning printed
#
# Without -a each changed file is built at BASE too, and only warnings the
# working tree adds are printed (compared without line numbers); the ones
# BASE already had are counted. The flags are the recipe's, -std=gnu17
# included (webos-connman-adapter.bb sets it), and the image's options.
#
# GCC is the image's cross compiler from tmp/sysroots-components; Clang is
# the host's, pointed at the same sysroots, which are put together in
# WORKDIR from those components. Files are compiled to assembly only:
# this is about warnings, not about linking a binary. armv7 uses a 64-bit
# time_t, as the image does.
#
# Exit status: 0 when no build printed a warning or an error.

set -u
export LC_ALL=C
. "$(dirname "$0")/common.sh"

BASE=
ALL=
while getopts "b:w:r:ah" opt; do
	case $opt in
	b) LUNEOS_BUILDDIR=$OPTARG ;;
	w) WORK=$OPTARG ;;
	r) BASE=$OPTARG ;;
	a) ALL=1 ;;
	*) sed -n '6,/^$/s/^# \{0,1\}//p' "$0"; exit 2 ;;
	esac
done

need_builddir
COMP=$LUNEOS_BUILDDIR/tmp/sysroots-components
generate_headers

# sysroot NAME COMPONENT-ARCH
sysroot() {
	local name=$1 c=$COMP/$2 dir=$WORK/sysroot-$1 n w
	if [ ! -f "$dir/.done" ]; then
		rm -rf "$dir" && mkdir -p "$dir"
		for n in glibc linux-libc-headers glib-2.0 libgcc gcc-runtime \
			 luna-service2 libpbnjson pmloglib luna-prefs json-c openssl wca-support-api; do
			[ -d "$c/$n" ] && cp -a "$c/$n/." "$dir/"
		done
		# headers some architectures stage without: take another's
		for n in wca-support-api/usr/include/wca-support.h luna-prefs/usr/include/lunaprefs.h; do
			[ -f "$dir/usr/include/$(basename "$n")" ] && continue
			w=$(ls "$COMP"/*/$n 2>/dev/null | head -n 1)
			[ -n "$w" ] && cp "$w" "$dir/usr/include/"
		done
		touch "$dir/.done"
	fi
	for n in usr/lib/glib-2.0/include/glibconfig.h usr/include/luna-service2/lunaservice.h \
		 usr/include/pbnjson.h usr/include/PmLogLib.h usr/include/wca-support.h \
		 usr/include/lunaprefs.h usr/include/json-c/json.h; do
		[ -f "$dir/$n" ] || die "sysroot $name has no $n"
	done
	echo "$dir"
}

ARM_SYS=$(sysroot armv7 cortexa8t2hf-neon-halium) || exit 2
A64_SYS=$(sysroot aarch64 aarch64-halium) || exit 2
X86_SYS=$(sysroot x86_64 corei7-64) || exit 2

XC=$COMP/x86_64
GCC_ARM=$XC/gcc-cross-arm/usr/bin/arm-webos-linux-gnueabi/arm-webos-linux-gnueabi-gcc
GCC_A64=$XC/gcc-cross-aarch64/usr/bin/aarch64-webos-linux/aarch64-webos-linux-gcc
GCC_X86=$XC/gcc-cross-x86_64/usr/bin/x86_64-webos-linux/x86_64-webos-linux-gcc
for g in "$GCC_ARM" "$GCC_A64" "$GCC_X86"; do
	[ -x "$g" ] || die "no cross compiler $g"
done
command -v clang >/dev/null || die "no clang on the host"

CFLAGS=("$IMAGE_STD" -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wformat
	-Wformat-security -Werror=format-security -Wall -Wextra
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
	"${IMAGE_DEFS[@]}")
ARM_FLAGS="-march=armv7-a -mthumb -mfpu=neon -mfloat-abi=hard -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"

cd "$ADAPTER" || exit 2
if [ -n "$ALL" ]; then
	FILES=$( (adapter_sources; ls tests/*.c) | sort -u)
else
	[ -n "$BASE" ] || BASE=$(git merge-base HEAD origin/webOS-ports/webOS-OSE) \
		|| die "cannot find the merge-base with origin/webOS-ports/webOS-OSE; pass -r"
	FILES=$( (git diff --name-only "$BASE" -- 'src/*.c' 'tests/*.c'
		  git ls-files --others --exclude-standard -- 'src/*.c' 'tests/*.c') | sort -u)
fi
[ -n "$FILES" ] || { echo "no C sources changed since $BASE"; exit 0; }

P=
if [ -z "$ALL" ]; then
	P=$WORK/cross-base
	rm -rf "$P" && mkdir -p "$P"
	git archive "$BASE" src tests | tar -x -C "$P" || die "cannot export $BASE"
fi

# build TREE FILE: compiler output of the six builds, prefixed by build
build() {
	local t=$1 f=$2 v label cc sys aflags
	for v in "gcc-armv7|$GCC_ARM|$ARM_SYS|$ARM_FLAGS" \
		 "gcc-aarch64|$GCC_A64|$A64_SYS|" \
		 "gcc-x86_64|$GCC_X86|$X86_SYS|" \
		 "clang-armv7|clang --target=armv7a-linux-gnueabihf|$ARM_SYS|$ARM_FLAGS" \
		 "clang-aarch64|clang --target=aarch64-linux-gnu|$A64_SYS|" \
		 "clang-x86_64|clang --target=x86_64-linux-gnu|$X86_SYS|"; do
		IFS='|' read -r label cc sys aflags <<<"$v"
		read -ra CC <<<"$cc"
		read -ra AF <<<"$aflags"
		"${CC[@]}" --sysroot="$sys" "${AF[@]}" "${CFLAGS[@]}" \
			-I"$t/src" -I"$WORK/gen" \
			-I"$sys/usr/include/glib-2.0" -I"$sys/usr/lib/glib-2.0/include" \
			-I"$sys/usr/include/gio-unix-2.0" -I"$sys/usr/include/json-c" \
			-S "$t/$f" -o /dev/null 2>&1 \
			| grep -E "warning:|error:" | sed "s|$t/||; s|^|$label: |"
	done
}

norm() { sed -E 's/:[0-9]+:[0-9]+:/:/' | sort -u; }

warnings=0
old=0
for f in $FILES; do
	[ -f "$f" ] || continue
	head=$(build "$ADAPTER" "$f")
	if [ -n "$P" ] && [ -f "$P/$f" ] && [ -n "$head" ]; then
		base=$(build "$P" "$f" | norm)
		new=$(comm -23 <(echo "$head" | norm) <(echo "$base"))
		old=$((old + $(comm -12 <(echo "$head" | norm) <(echo "$base") | grep -c .)))
		head=$(echo "$head" | while IFS= read -r l; do
			echo "$new" | grep -qxF "$(echo "$l" | norm)" && echo "$l"; done)
	fi
	[ -n "$head" ] || continue
	echo "$head"
	warnings=$((warnings + $(echo "$head" | grep -c .)))
done
[ -n "$P" ] && echo "$old warnings/errors BASE already had are not shown"
echo "$(echo $FILES | wc -w) files, 6 builds each, $warnings ${P:+new }warnings/errors"
[ "$warnings" -eq 0 ]
