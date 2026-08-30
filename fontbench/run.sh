#!/usr/bin/env bash
# One command: build the venv if missing, fetch the corpus if missing, run
# every measurement, write results into out/.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -x .venv/bin/python ]; then
  echo "== creating .venv (python 3.12) =="
  uv venv --python 3.12 .venv
  VIRTUAL_ENV="$PWD/.venv" uv pip install -q -r requirements.txt
fi
PY=.venv/bin/python

[ "$(ls fonts 2>/dev/null | wc -l)" -ge 15 ] || ./download.sh

mkdir -p out
echo "== size ladder =="        | tee    out/ladder.txt
$PY measure_fonts.py ladder     | tee -a out/ladder.txt
echo "== equal x-height =="     | tee    out/equal.txt
$PY measure_fonts.py equal      | tee -a out/equal.txt
echo "== AA-off dilation =="    | tee    out/dilation.txt
$PY measure_aa_dilation.py      | tee -a out/dilation.txt
$PY render_panel_proof.py compare
# the two ends of the size-ladder argument in ../FONTS.md
$PY render_panel_proof.py page --font Bitter --size 14 --margin 5              # ships today
$PY render_panel_proof.py page --font LexicaUltralegible --size 10 --margin 20 # proposed

echo
echo "results in $(pwd)/out"
