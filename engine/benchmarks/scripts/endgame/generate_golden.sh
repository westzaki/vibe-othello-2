#!/bin/sh
set -eu

script_dir="$(CDPATH= cd "$(dirname "$0")" && pwd -P)"
repo_root="$(CDPATH= cd "$script_dir/../../../.." && pwd -P)"

bench="${1:-./build-bench/engine/benchmarks/vibe_othello_endgame_bench}"
output="${2:-engine/testdata/endgame/golden/exact_score.jsonl}"
tmp="${TMPDIR:-/tmp}/vibe_othello_endgame_golden.$$"
trap 'rm -f "$tmp"' EXIT

cd "$repo_root"
mkdir -p "$(dirname "$output")"
"$bench" \
  --jsonl \
  --repeat 1 \
  --max-empties 12 \
  --corpus engine/testdata/endgame/positions.tsv >"$tmp"
"$script_dir/check_golden.py" --write-normalized "$output" "$tmp"
