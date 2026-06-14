#!/bin/sh
set -eu

script_dir="$(CDPATH= cd "$(dirname "$0")" && pwd -P)"
repo_root="$(CDPATH= cd "$script_dir/../../../.." && pwd -P)"

bench="${1:-./build-bench/engine/benchmarks/vibe_othello_search_bench}"
output="${2:-engine/testdata/search/golden/discdiff_depth_1_4.jsonl}"
tmp="${TMPDIR:-/tmp}/vibe_othello_search_golden.$$"
trap 'rm -f "$tmp"' EXIT

cd "$repo_root"
mkdir -p "$(dirname "$output")"
"$bench" \
  --mode iterative \
  --depth 1..4 \
  --tt both \
  --corpus engine/testdata/search/positions.tsv \
  --jsonl >"$tmp"
"$script_dir/check_golden.py" --write-normalized "$output" "$tmp"
