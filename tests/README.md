# webos-connman-adapter tests

Three layers, smallest first.

## Unit tests (`tests/*.c`)

Built with the adapter (`enable_testing()`, registered with CTest in
`tests/CMakeLists.txt`) and runnable on the target. They cover the helpers
that need no bus, such as SSID conversion, BSS info, `utils`,
`json_utils` and `wifi_hwaddr`.

## Hardening (`tests/hardening/`)

Host-side checks. The webOS libraries are not on a build host, so they come
from a LuneOS build tree: pass `-b` or set `LUNEOS_BUILDDIR` to the
directory that has `tmp/sysroots-components`.

    tests/hardening/host-check.sh   -b BUILDDIR [gcc clang asan-gcc asan-clang tsan valgrind]
    tests/hardening/static-check.sh -b BUILDDIR [-r BASE] [-a] [tool...]
    tests/hardening/cross-check.sh  -b BUILDDIR [-r BASE] [-a]

- **host-check.sh** builds every test listed in `tests/CMakeLists.txt` with
  the host's gcc and clang and runs it plain, under ASan+UBSan (both
  compilers), TSan and valgrind. Tests that need pbnjson link the build
  tree's x86-64 libraries; the rest use the host's glib.
- **static-check.sh** runs gcc `-fanalyzer` (-O0 and -O2), the clang
  analyzer with the alpha checkers, clang-tidy, cppcheck (exhaustive),
  flawfinder and sparse over the C files changed since BASE (default: the
  merge-base with `origin/webOS-ports/webOS-OSE`), on the working tree and
  on BASE, and prints only what the working tree adds. `-a` checks every
  source. Mark a line a reviewer has cleared with flawfinder's
  `/* Flawfinder: ignore */`, or cppcheck's
  `// cppcheck-suppress <id>`, with the reason next to it.
- **cross-check.sh** compiles the changed files and the tests with the
  image's GCC and the host's Clang for armv7, aarch64 and x86-64, with the
  recipe's flags (`-std=gnu17` included), and prints only the warnings the
  working tree adds over BASE. `-a` builds everything and prints every
  warning.

Each script exits non-zero on a failure or a new finding, so they can gate
a pull request. Work files go to `~/.cache/webos-connman-adapter-tests`
(`-w` to change).

## On the device (`tests/stress/`)

`luna-stress.sh [iterations]` hammers the luna API with valid queries,
subscription churn and hostile payloads while watching the daemon for
restarts, RSS growth and fd leaks; `adb-stress.sh` runs it from a host.
