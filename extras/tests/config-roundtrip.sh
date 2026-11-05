#!/bin/sh
# Round-trip test for the configuration editor's file writer.
#
# Usage: extras/tests/config-roundtrip.sh [path/to/es40_cfg_gtk] [config.cfg ...]
#
# Each configuration is loaded by the editor and written back without any
# change; the result must be byte-identical to the input, which proves that
# comments, spacing and settings the editor does not model survive a save.
# Without files, the shipped src/es40.cfg is tested.
set -u
here=$(cd "$(dirname "$0")/../.." && pwd)
editor=${1:-}
if [ -n "$editor" ] && [ -x "$editor" ]; then shift; else editor=$(command -v es40_cfg_gtk || true); fi
if [ -z "$editor" ]; then echo "es40_cfg_gtk not found; pass its path as the first argument" >&2; exit 2; fi
[ $# -gt 0 ] || set -- "$here/src/es40.cfg"
failed=0
for file in "$@"; do
    if ! out=$("$editor" --render "$file" 2>&1); then
        echo "FAIL  $file: $out"; failed=1; continue
    fi
    if printf '%s' "$out" | cmp -s - "$file" || [ "$out" = "$(cat "$file")" ]; then
        echo "ok    $file"
    else
        echo "FAIL  $file: the editor would change it"
        printf '%s\n' "$out" | diff "$file" - | head -20
        failed=1
    fi
done
exit $failed
