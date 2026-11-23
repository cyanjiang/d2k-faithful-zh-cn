#!/usr/bin/env bash
# Like re/query.sh, but retries while another headless run holds the
# Ghidra project lock (parallel agents share one project).
HERE="$(cd "$(dirname "$0")" && pwd)"
label="$1"; name="$2"
for i in $(seq 1 60); do
    out=$("$HERE/query.sh" "$@")
    if ! grep -q "Unable to lock project" "$HERE/exports/$label/q_$name.log" 2>/dev/null; then
        echo "$out"; exit 0
    fi
    sleep 5
done
echo "lock timeout" >&2; exit 1
