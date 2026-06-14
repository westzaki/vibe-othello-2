# Endgame Benchmark Baselines

Endgame baselines track the root-triggered exact-score solver.

They are comparison data only. Do not use them as CI pass/fail gates, because
timing and NPS are machine-, compiler-, and load-dependent.

The current baseline command uses the checked-in deterministic corpus:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --repeat 3 \
  --max-empties 12 \
  --corpus engine/testdata/endgame/positions.tsv \
  > engine/benchmarks/results/endgame-exact-score-raw.jsonl
```

This command produces raw JSONL benchmark output: one JSON object per
position/repeat. The checked-in baseline is not that raw JSONL file. It is an
aggregate JSON document that records the benchmark metadata, one selected result
per position, and raw repeat timing summaries for local comparison.

To regenerate the checked-in baseline:

1. Run the raw JSONL command above on an otherwise quiet machine.
2. Create or update the aggregate JSON file under this directory.
3. Set `measured_commit` and `measured_revision` to the engine commit that was
   measured, not necessarily the documentation commit that updates the baseline.
4. Copy one selected repeat per position into `results[]`.
5. Keep per-repeat timing summaries in `raw_runs` when useful for comparison.
6. Run the schema check below before committing.

The checked-in aggregate JSON must include:

- `schema_version`
- `benchmark`
- `measured_commit` and `measured_revision`, which identify the commit that was
  measured
- the command, corpus, build type, compiler, and generic machine description
- the selected repeat policy
- `results[]` with deterministic search statistics such as score, best move,
  nodes, and cutoffs
- raw repeat timing values for local comparison

Minimum aggregate shape:

```json
{
  "schema_version": 1,
  "benchmark": "endgame_exact_score",
  "measured_commit": "<full measured commit sha>",
  "measured_revision": "<short measured revision>",
  "command": "./build-bench/engine/benchmarks/vibe_othello_endgame_bench --jsonl --repeat 3 --max-empties 12 --corpus engine/testdata/endgame/positions.tsv",
  "corpus": "engine/testdata/endgame/positions.tsv",
  "results": [
    {
      "position_id": "twelve_empty_simple",
      "score": 0,
      "best_move": "a7",
      "nodes": 88404,
      "endgame_nodes": 88404
    }
  ]
}
```

Sanity-check the aggregate JSON before committing it:

```sh
tools/endgame/check_baseline.py \
  engine/benchmarks/baselines/endgame/2026-06-14-8f89540-apple-silicon-macos-arm64-apple-clang-17-release.json
```

The schema check is intentionally light. It verifies the aggregate baseline
shape and required comparison fields; it does not compare timing values or act as
a performance gate.

Keep one-off benchmark outputs in `engine/benchmarks/results/`.
