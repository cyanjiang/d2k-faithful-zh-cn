#!/usr/bin/env bash
# Run a Query.java query against an already-analyzed program.
#   re/query.sh <label> <outname> <mode> <args...>
# e.g. re/query.sh 1.06 timer import timeSetEvent
# Output: re/exports/<label>/q_<outname>.txt (gitignored)
set -euo pipefail
GHIDRA="${GHIDRA_HOME:-$HOME/tools/ghidra_12.1.4_PUBLIC}"
HERE="$(cd "$(dirname "$0")" && pwd)"
label="$1"; name="$2"; shift 2
out="$HERE/exports/$label/q_$name.txt"
"$GHIDRA/support/analyzeHeadless.bat" "$HERE/ghidra-project" "d2k/$label" \
    -process DUNE2000.DAT -noanalysis -readOnly \
    -scriptPath "$HERE/ghidra_scripts" \
    -postScript Query.java "$out" "$@" > "$HERE/exports/$label/q_$name.log" 2>&1
echo "$out"
