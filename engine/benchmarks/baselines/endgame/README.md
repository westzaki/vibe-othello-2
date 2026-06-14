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
  --corpus engine/testdata/endgame/positions.tsv
```

When adding a baseline file, include:

- the command, corpus, build type, compiler, and generic machine description
- the selected repeat policy
- deterministic search statistics such as score, best move, nodes, and cutoffs
- raw repeat timing values for local comparison

Keep one-off benchmark outputs in `engine/benchmarks/results/`.
