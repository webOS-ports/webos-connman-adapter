#!/bin/bash
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Static analysis of the adapter, reporting only what a branch adds.
#
# Usage: static-check.sh [-b LUNEOS_BUILDDIR] [-w WORKDIR] [-r BASE] [-a]
#                        [tool...]
#
#   tools: fanalyzer clang-analyze clang-tidy cppcheck flawfinder sparse
#          (default: all that are installed)
#   -r     the ref to compare with (default: the merge-base of HEAD and
#          origin/webOS-ports/webOS-OSE)
#   -a     every source file, not only the ones changed since BASE
#
# Each tool runs on the working tree and on BASE (exported with git
# archive). Findings are compared without line numbers; the ones only the
# working tree has are printed as NEW with their location, the rest are
# counted as pre-existing. Full output stays in WORKDIR/static/.
#
# luna-service2, pbnjson, PmLogLib, luna-prefs and wca-support headers come
# from the LuneOS build tree's sysroot components; connman-interface.h and
# friends are generated with the host's gdbus-codegen.
#
# Exit status: 0 when nothing new was found.

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
shift $((OPTIND - 1))
TOOLS=${*:-fanalyzer clang-analyze clang-tidy cppcheck flawfinder sparse}

need_builddir
C=$(components_of corei7-64 x86_64) || die "no x86-64 sysroot components"
OUT=$WORK/static
mkdir -p "$OUT"
generate_headers

cd "$ADAPTER" || exit 2
[ -n "$BASE" ] || BASE=$(git merge-base HEAD origin/webOS-ports/webOS-OSE) \
	|| die "cannot find the merge-base with origin/webOS-ports/webOS-OSE; pass -r"
P=$WORK/base
rm -rf "$P" && mkdir -p "$P"
git archive "$BASE" src | tar -x -C "$P" || die "cannot export $BASE"

if [ -n "$ALL" ]; then
	FILES=$(adapter_sources)
else
	FILES=$( (git diff --name-only "$BASE" -- 'src/*.c'; git ls-files --others --exclude-standard -- 'src/*.c') | sort -u)
fi

INC=()
for d in $(webos_dirs "$C"); do
	[ -d "$d/usr/include" ] && INC+=("-I$d/usr/include")
done
read -ra PKG <<<"$(pkg-config --cflags glib-2.0 gio-unix-2.0 2>/dev/null)"
DEFS=(-D_GNU_SOURCE -DENABLE_SINGLE_PROFILE)

# set_flags TREE
set_flags() {
	FL=("-I$1/src" "-I$WORK/gen" "${INC[@]}" "${PKG[@]}" "${DEFS[@]}")
}

run_tool() {
	local tool=$1 t=$2 f=$3 o
	set_flags "$t"
	case $tool in
	fanalyzer)
		for o in -O0 -O2; do
			gcc "${FL[@]}" -fanalyzer $o -c "$t/$f" -o /dev/null 2>&1 \
				| grep -E "warning:.*\[-Wanalyzer" | sed "s/^/$o /"
		done ;;
	clang-analyze)
		clang "${FL[@]}" --analyze -Xanalyzer -analyzer-output=text \
			-Xanalyzer -analyzer-checker=alpha.unix,alpha.security,alpha.core \
			-w -o /dev/null "$t/$f" 2>&1 | grep -E "warning:" ;;
	clang-tidy)
		clang-tidy -quiet "-checks=-*,bugprone-*,cert-*,clang-analyzer-*,-bugprone-easily-swappable-parameters,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp,-bugprone-assignment-in-if-condition,-cert-err33-c,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling" \
			"$t/$f" -- "${FL[@]}" 2>/dev/null | grep -E "^/.*warning:" ;;
	cppcheck)
		cppcheck -q --enable=all --check-level=exhaustive --inconclusive \
			--suppress=missingIncludeSystem --suppress=missingInclude \
			--suppress=unusedFunction --suppress=staticFunction \
			--suppress=checkersReport -D__GNUC__ \
			$(printf '%s\n' "${FL[@]}" | grep -E '^-I') \
			--template='{file}:{line}: {severity}: {message} [{id}]' \
			"$t/$f" 2>&1 | grep -E ": (error|warning|style|performance|portability):" ;;
	flawfinder)
		flawfinder -Q -D -m 1 -S "$t/$f" 2>/dev/null | grep -E "^\S+:[0-9]+:" ;;
	sparse)
		sparse -Wno-decl -Wno-non-pointer-null "${FL[@]}" "$t/$f" 2>&1 \
			| grep -E "(warning|error):" | grep -vE "^/usr/|sysroots-components" ;;
	esac
}

norm() { sed -E "s|$ADAPTER/||g; s|$P/||g; s/:[0-9]+(:[0-9]+)?:/:/g; s/\[[0-9]+\]//g" | sort -u; }

have() {
	case $1 in
	fanalyzer) command -v gcc ;; clang-analyze) command -v clang ;;
	*) command -v "$1" ;;
	esac >/dev/null
}

skipped=
CHECK=
for f in $FILES; do
	[ -f "$ADAPTER/$f" ] || continue
	set_flags "$ADAPTER"
	if ! gcc "${FL[@]}" -fsyntax-only -w "$ADAPTER/$f" 2>"$OUT/compile.err"; then
		skipped="$skipped $f($(grep -m1 -o 'fatal error: [^ ]*' "$OUT/compile.err" | cut -d' ' -f3))"
		continue
	fi
	CHECK="$CHECK $f"
done
if [ -z "$CHECK" ]; then
	[ -n "$skipped" ] && die "no file could be compiled:$skipped (see $OUT/compile.err)"
	echo "no C sources changed since $BASE"; exit 0
fi

total_new=0
for tool in $TOOLS; do
	if ! have "$tool"; then
		echo "== $tool: not installed, skipped"
		continue
	fi
	: >"$OUT/$tool.head"; : >"$OUT/$tool.base"
	for f in $CHECK; do
		run_tool "$tool" "$ADAPTER" "$f" >>"$OUT/$tool.head"
		[ -f "$P/$f" ] && run_tool "$tool" "$P" "$f" >>"$OUT/$tool.base"
	done
	norm <"$OUT/$tool.head" >"$OUT/$tool.head.n"
	norm <"$OUT/$tool.base" >"$OUT/$tool.base.n"
	comm -23 "$OUT/$tool.head.n" "$OUT/$tool.base.n" >"$OUT/$tool.new.n"
	: >"$OUT/$tool.new"
	while IFS= read -r line; do
		while IFS= read -r full; do
			[ "$(echo "$full" | norm)" = "$line" ] && echo "$full" >>"$OUT/$tool.new"
		done <"$OUT/$tool.head"
	done <"$OUT/$tool.new.n"
	n=$(sort -u "$OUT/$tool.new" | wc -l)
	old=$(comm -12 "$OUT/$tool.head.n" "$OUT/$tool.base.n" | wc -l)
	echo "== $tool: $n new, $old pre-existing"
	sort -u "$OUT/$tool.new" | sed "s|$ADAPTER/||; s/^/   NEW /"
	total_new=$((total_new + n))
done

[ -n "$skipped" ] && echo "skipped (does not compile on this host):$skipped"
echo "base: $BASE"
echo "files checked:$CHECK"
echo "full output: $OUT"

[ "$total_new" -eq 0 ]
