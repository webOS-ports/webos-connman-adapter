#!/bin/bash
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Build the unit tests in tests/ natively and run them in several variants:
#
#   gcc, clang            -Wall -Wextra, warnings shown
#   asan-gcc, asan-clang  AddressSanitizer + UndefinedBehaviorSanitizer
#   tsan                  ThreadSanitizer (gcc)
#   valgrind              memcheck, definite leaks are errors
#
# Usage: host-check.sh [-b LUNEOS_BUILDDIR] [-w WORKDIR] [variant...]
#
# The tests and their sources are read from tests/CMakeLists.txt, so a test
# added there is picked up. Tests that link only glib use the host's glib.
# Tests that need pbnjson take its headers and libraries from the LuneOS
# build tree's x86-64 sysroot components (glibc is the same major version
# as current hosts); without a build tree they are reported as skipped.
#
# Exit status: 0 when every variant run passed.

set -u
export LC_ALL=C
. "$(dirname "$0")/common.sh"

while getopts "b:w:h" opt; do
	case $opt in
	b) LUNEOS_BUILDDIR=$OPTARG ;;
	w) WORK=$OPTARG ;;
	*) sed -n '6,/^$/s/^# \{0,1\}//p' "$0"; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
VARIANTS=${*:-gcc clang asan-gcc asan-clang tsan valgrind}

mkdir -p "$WORK" || die "cannot create $WORK"
pkg-config --exists glib-2.0 || die "host is missing glib-2.0 development files"

# name|sources|libs for every add_executable in tests/CMakeLists.txt
tests_list() {
	awk '
	/add_executable\(/ { collecting = 1; buf = "" }
	collecting { buf = buf " " $0; if ($0 ~ /\)/) { collecting = 0; print "E" buf } }
	/target_link_libraries\(/ { print "L " $0 }
	' "$ADAPTER/tests/CMakeLists.txt" | sed 's/[()]/ /g' | awk '
	$1 == "E" { name = $3; srcs = ""; for (i = 4; i <= NF; i++) srcs = srcs " " $i; src[name] = srcs; order[++n] = name }
	$1 == "L" { name = $3; libs = ""; for (i = 4; i <= NF; i++) libs = libs " " $i; lib[name] = libs }
	END { for (i = 1; i <= n; i++) print order[i] "|" src[order[i]] "|" lib[order[i]] }'
}

PB_CFLAGS=()
PB_LIBS=()
if [ -n "${LUNEOS_BUILDDIR:-}" ] && C=$(components_of corei7-64 x86_64); then
	for d in $(webos_dirs "$C"); do
		[ -d "$d/usr/include" ] && PB_CFLAGS+=("-I$d/usr/include")
		[ -d "$d/usr/lib" ] && PB_LIBS+=("-L$d/usr/lib" "-Wl,-rpath,$d/usr/lib")
	done
	PB_LIBS+=(-lpbnjson_c)
fi

read -ra GLIB_CFLAGS <<<"$(pkg-config --cflags glib-2.0)"
read -ra GLIB_LIBS <<<"$(pkg-config --libs glib-2.0)"
WARN=(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers)
SAN=(-O1 -g -fno-omit-frame-pointer)
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export TSAN_OPTIONS=halt_on_error=1

# run_variant NAME CC FLAGS...
run_variant() {
	local v=$1 cc=$2 dir=$WORK/$1 line name srcs libs extra rc=0 n=0
	shift 2
	mkdir -p "$dir"
	while IFS='|' read -r name srcs libs; do
		srcs=$(echo $srcs | sed "s|\${CMAKE_SOURCE_DIR}|$ADAPTER|g; s|\${CMAKE_CURRENT_SOURCE_DIR}|$ADAPTER/tests|g")
		srcs=$(for s in $srcs; do case $s in /*) echo "$s" ;; *) echo "$ADAPTER/tests/$s" ;; esac; done)
		extra=()
		case $libs in
		*PBNJSON*)
			if [ ${#PB_LIBS[@]} -eq 0 ]; then
				echo "  $v: $name skipped (needs pbnjson: pass -b LUNEOS_BUILDDIR)"
				continue
			fi
			extra=("${PB_CFLAGS[@]}" "${PB_LIBS[@]}") ;;
		esac
		if ! $cc "$@" -I"$ADAPTER/src" "${GLIB_CFLAGS[@]}" $srcs "${extra[@]}" \
				"${GLIB_LIBS[@]}" -o "$dir/$name" >"$dir/$name.build.log" 2>&1; then
			echo "  $v: $name does not build, see $dir/$name.build.log"
			rc=1; continue
		fi
		grep -E "warning:" "$dir/$name.build.log" | sed "s|$ADAPTER/||; s|^|  $v: |"
		if [ "$v" = valgrind ]; then
			valgrind -q --error-exitcode=99 --leak-check=full \
				--errors-for-leak-kinds=definite \
				--suppressions="$(dirname "$0")/valgrind.supp" \
				"$dir/$name" >"$dir/$name.log" 2>&1
		else
			"$dir/$name" >"$dir/$name.log" 2>&1
		fi
		if [ $? -eq 0 ]; then
			n=$((n + 1))
		else
			echo "  $v: $name FAILED, see $dir/$name.log"
			grep -hE "not ok|ERROR|runtime error|Sanitizer|definitely lost|Invalid" "$dir/$name.log" \
				| head -n 8 | sed 's/^/    /'
			rc=1
		fi
	done < <(tests_list)
	echo "  $v: $n tests passed"
	return $rc
}

status=0
for v in $VARIANTS; do
	echo "== $v"
	case $v in
	gcc)        run_variant gcc gcc -O2 -g "${WARN[@]}" ;;
	clang)      run_variant clang clang -O2 -g "${WARN[@]}" ;;
	asan-gcc)   run_variant asan-gcc gcc "${SAN[@]}" -fsanitize=address,undefined ;;
	asan-clang) run_variant asan-clang clang "${SAN[@]}" -fsanitize=address,undefined ;;
	tsan)       run_variant tsan gcc "${SAN[@]}" -fsanitize=thread ;;
	valgrind)   command -v valgrind >/dev/null || { echo "  valgrind: not installed"; false; } \
			&& run_variant valgrind gcc -O0 -g ;;
	*)          echo "unknown variant $v"; false ;;
	esac || status=1
done
exit $status
