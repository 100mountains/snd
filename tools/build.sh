#!/usr/bin/env bash
# One-shot local build + self-test. Run this by hand whenever you want to
# check SND still works -- it is NOT wired to any CI, git hook, or automated
# trigger, and it never will be. Run it as often or as rarely as you like;
# nothing runs unless you type this command.
#
#   tools/build.sh            configure + build + run --selftest
#   tools/build.sh --no-test  configure + build only
#
# Cross-platform note: this script itself is bash, so it runs as-is on macOS
# and Linux. On Windows, run the same three cmake commands below from
# PowerShell (there's nothing bash-specific about the build itself, only
# about this convenience wrapper).

set -euo pipefail
cd "$(dirname "$0")/.."

RUN_TEST=1
for a in "$@"; do
    case "$a" in
        --no-test) RUN_TEST=0 ;;
        *) echo "unknown flag: $a"; exit 2 ;;
    esac
done

echo "== configure =="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

echo "== build =="
cmake --build build

if [ "$RUN_TEST" = 1 ]; then
    echo "== selftest =="
    ./build/snd-example --selftest
    echo "== done: build OK, selftest passed =="
else
    echo "== done: build OK (selftest skipped) =="
fi
