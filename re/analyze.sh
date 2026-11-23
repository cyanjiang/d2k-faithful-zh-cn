#!/usr/bin/env bash
# Headless Ghidra analysis of a Dune 2000 executable + basic exports.
#   re/analyze.sh <label> <path/to/DUNE2000.DAT>
# Project:  re/ghidra-project/d2k.gpr, program under folder /<label>
#           (gitignored; open it in the Ghidra GUI)
# Exports:  re/exports/<label>/*.tsv (gitignored: derived from the original binary)
set -euo pipefail
GHIDRA="${GHIDRA_HOME:-$HOME/tools/ghidra_12.1.4_PUBLIC}"
HERE="$(cd "$(dirname "$0")" && pwd)"
label="$1"; exe="$2"
mkdir -p "$HERE/ghidra-project" "$HERE/exports/$label"
"$GHIDRA/support/analyzeHeadless.bat" "$HERE/ghidra-project" "d2k/$label" \
    -import "$exe" -overwrite \
    -scriptPath "$HERE/ghidra_scripts" \
    -postScript ExportBasics.java "$HERE/exports/$label" \
    -log "$HERE/exports/$label/analysis.log"
