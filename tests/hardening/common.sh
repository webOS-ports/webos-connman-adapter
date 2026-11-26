# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Sourced by the hardening scripts. Bash.
#
# The webOS libraries (luna-service2, pbnjson, PmLogLib, luna-prefs,
# wca-support) are not on a build host, so their headers and libraries come
# from a LuneOS build tree: LUNEOS_BUILDDIR, or -b, is the directory with
# tmp/sysroots-components in it.

ADAPTER=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
WORK=${WORK:-$HOME/.cache/webos-connman-adapter-tests}

die() { echo "$(basename "$0"): $*" >&2; exit 2; }

# components ARCH...: the sysroots-components dir of the first ARCH there
components_of() {
	local a
	for a in "$@"; do
		[ -d "$LUNEOS_BUILDDIR/tmp/sysroots-components/$a" ] && {
			echo "$LUNEOS_BUILDDIR/tmp/sysroots-components/$a"; return 0; }
	done
	return 1
}

need_builddir() {
	[ -n "${LUNEOS_BUILDDIR:-}" ] || die "set LUNEOS_BUILDDIR or pass -b (a LuneOS build dir with tmp/sysroots-components)"
	[ -d "$LUNEOS_BUILDDIR/tmp/sysroots-components" ] || die "no tmp/sysroots-components in $LUNEOS_BUILDDIR"
}

# webos_dirs COMPONENTS-DIR: the component dirs holding a webOS library,
# for -I and -L. wca-support-api is headers only and staged without them
# for some architectures, so any one that has them will do.
webos_dirs() {
	local c=$1 n d
	for n in luna-service2 libpbnjson pmloglib luna-prefs yajl uriparser gmp; do
		d=$c/$n
		[ -d "$d" ] || d=$(ls -d "$LUNEOS_BUILDDIR"/tmp/sysroots-components/*/"$n" 2>/dev/null | head -n 1)
		[ -n "$d" ] && [ -d "$d" ] && echo "$d"
	done
	# staged empty on some architectures: take one that has the header
	d=$(ls -d "$c/wca-support-api" "$LUNEOS_BUILDDIR"/tmp/sysroots-components/*/wca-support-api 2>/dev/null \
		| while read -r x; do [ -f "$x/usr/include/wca-support.h" ] && { echo "$x"; break; }; done)
	[ -n "$d" ] && echo "$d"
}

# Headers the build generates: connman_common.h from its .in, and the
# gdbus-codegen interfaces. Into $WORK/gen.
generate_headers() {
	local g=$WORK/gen
	mkdir -p "$g"
	sed -e 's|@WIFI_IFACE_NAME@|wlan0|; s|@WIRED_IFACE_NAME@|eth0|' \
	    -e 's|@P2P_IFACE_NAME@|p2p0|; s|@CONNMAN_CONFIG_DIR@|/var/lib/connman|' \
		"$ADAPTER/src/connman_common.h.in" >"$g/connman_common.h"
	if [ ! -f "$g/connman-interface.h" ] || [ "$ADAPTER/files/xml/connman.xml" -nt "$g/connman-interface.h" ]; then
		(cd "$g" && gdbus-codegen --c-namespace ConnmanInterface --generate-c-code \
			connman-interface --interface-prefix net.connman. \
			"$ADAPTER/files/xml/connman.xml" \
		&& gdbus-codegen --c-namespace PacrunnerInterface --generate-c-code \
			pacrunner-interface --interface-prefix org.pacrunner. \
			"$ADAPTER/files/xml/pacrunner.xml") || die "gdbus-codegen failed"
	fi
}

# The adapter's sources, from the top-level CMakeLists.txt
adapter_sources() {
	sed -n '/^set(SOURCE_FILES/,/)/p' "$ADAPTER/CMakeLists.txt" \
		| grep -oE 'src/[A-Za-z0-9_/]+\.c'
}
