#!/bin/sh
set -eu

bench="${1:-./build-bench/engine/benchmarks/vibe_othello_search_bench}"
output="${2:-}"
corpus="engine/testdata/search/positions.tsv"
depth="5"
mode="iterative"
tt_mode="both"
build_type="Release"
raw="engine/benchmarks/results/search-iterative-discdiff-depth${depth}-raw.jsonl"

mkdir -p engine/benchmarks/results

measured_commit="$(git rev-parse HEAD)"
measured_revision="$(git rev-parse --short HEAD)"
measured_at="$(date +%Y-%m-%d)"
uname_s="$(uname -s)"
uname_r="$(uname -r)"
uname_m="$(uname -m)"
os="${uname_s} ${uname_r} ${uname_m}"
compiler="$(c++ --version | head -n 1)"

if [ "$uname_s" = "Darwin" ] && [ "$uname_m" = "arm64" ]; then
  machine_token="apple-silicon-macos-arm64"
else
  machine_token="$(printf '%s' "$os" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/^-//;s/-$//')"
fi
machine="$machine_token"

case "$compiler" in
  "Apple clang version "*)
    compiler_major="$(printf '%s' "$compiler" | sed -n 's/^Apple clang version \([0-9][0-9]*\).*/\1/p')"
    compiler_token="apple-clang-${compiler_major:-unknown}"
    ;;
  *)
    compiler_token="$(printf '%s' "$compiler" | tr '[:upper:]' '[:lower:]' | tr -cs 'a-z0-9' '-' | sed 's/^-//;s/-$//')"
    ;;
esac

if [ -z "$output" ]; then
  output="engine/benchmarks/baselines/search/${measured_at}-${measured_revision}-${machine_token}-${compiler_token}-release.json"
fi

command="${bench} --mode ${mode} --depth ${depth} --tt ${tt_mode} --corpus ${corpus} --jsonl"

"$bench" \
  --mode "$mode" \
  --depth "$depth" \
  --tt "$tt_mode" \
  --corpus "$corpus" \
  --jsonl >"$raw"

tools/search/check_baseline.py \
  --write-aggregate "$output" \
  --measured-commit "$measured_commit" \
  --measured-revision "$measured_revision" \
  --measured-at "$measured_at" \
  --machine "$machine" \
  --os "$os" \
  --compiler "$compiler" \
  --build-type "$build_type" \
  --command "$command" \
  --corpus "$corpus" \
  --mode "$mode" \
  --depth "$depth" \
  --tt-mode "$tt_mode" \
  "$raw"

tools/search/check_baseline.py "$output"
