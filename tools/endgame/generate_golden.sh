#!/bin/sh
set -eu

bench="${1:-./build-bench/engine/benchmarks/vibe_othello_endgame_bench}"
output="${2:-engine/testdata/endgame/golden/exact_score.jsonl}"
tmp="${TMPDIR:-/tmp}/vibe_othello_endgame_golden.$$"
trap 'rm -f "$tmp"' EXIT

mkdir -p "$(dirname "$output")"
"$bench" \
  --jsonl \
  --repeat 1 \
  --max-empties 12 \
  --corpus engine/testdata/endgame/positions.tsv >"$tmp"
tools/endgame/check_golden.py --write-normalized "$output" "$tmp"
