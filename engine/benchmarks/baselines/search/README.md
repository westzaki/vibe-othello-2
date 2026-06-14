# Search Benchmark Baselines

Search baselines track aggregate `vibe_othello_search_bench` output for the
checked-in search corpus.

They are comparison data only. Do not use them as CI pass/fail gates, because
timing and NPS are machine-, compiler-, and load-dependent. Deterministic search
golden checks cover stable result fields separately.

The current search aggregate baseline command uses iterative search,
disc-difference evaluation, depth 5, transposition-table mode `both`, and the
checked-in search corpus:

```sh
./build-bench/engine/benchmarks/vibe_othello_search_bench \
  --mode iterative \
  --depth 5 \
  --tt both \
  --corpus engine/testdata/search/positions.tsv \
  --jsonl \
  > engine/benchmarks/results/search-iterative-discdiff-depth5-raw.jsonl
```

This command produces raw JSONL benchmark output: one JSON object per
position/mode/depth result. The checked-in baseline is not that raw JSONL file.
It is an aggregate JSON document that records benchmark metadata and compact
per-position comparison rows.

To regenerate the checked-in baseline:

1. Configure and build `vibe_othello_search_bench` in Release mode.
2. Run `engine/benchmarks/scripts/search/generate_baseline.sh` on an otherwise
   quiet machine.
3. Set `measured_commit` and `measured_revision` to the engine commit that was
   measured, not necessarily the documentation commit that updates the baseline.
4. Review deterministic result fields first: score, best move, PV, and root
   move summaries.
5. Review search statistics: nodes, eval calls, beta cutoffs, alpha updates,
   and TT probe/hit/store/cutoff counters.
6. Treat elapsed timing and NPS as local comparison data only.
7. Run the schema check below before committing.

The checked-in aggregate JSON must include:

- `schema_version`
- `benchmark`, currently `search_iterative_discdiff`
- `measured_commit` and `measured_revision`, which identify the commit that was
  measured
- the command, corpus, build type, compiler, and generic machine description
- `mode`, `depth`, and `tt_mode`
- `results[]` with score, best move, PV, root move summaries, nodes,
  eval/search statistics, TT statistics, enabled benchmark option columns, and
  elapsed timing summary fields

Minimum aggregate shape:

```json
{
  "schema_version": 1,
  "benchmark": "search_iterative_discdiff",
  "measured_commit": "<full measured commit sha>",
  "measured_revision": "<short measured revision>",
  "command": "./build-bench/engine/benchmarks/vibe_othello_search_bench --mode iterative --depth 5 --tt both --corpus engine/testdata/search/positions.tsv --jsonl",
  "corpus": "engine/testdata/search/positions.tsv",
  "mode": "iterative",
  "depth": 5,
  "tt_mode": "both",
  "results": [
    {
      "position_id": "initial",
      "category": "opening",
      "mode": "iterative",
      "variant_id": "eval=disc_difference;tt=both;pvs=off;aspiration=off;history=off;killers=off;iid=off;exact_endgame=0;endgame_tt=off;endgame_parity=on",
      "tt_mode": "both",
      "depth": 5,
      "evaluator": "disc_difference",
      "pvs": "off",
      "aspiration": "off",
      "history": "off",
      "killers": "off",
      "iid": "off",
      "exact_endgame": false,
      "endgame_exact_empties": 0,
      "endgame_tt": "off",
      "endgame_parity": "on",
      "score": 3,
      "best_move": "f5",
      "pv": ["f5"],
      "root_moves": [
        {
          "move": "f5",
          "score": 3,
          "bound": "exact",
          "depth": 5,
          "exact": true,
          "selective": false
        }
      ],
      "nodes": 250,
      "eval_calls": 120,
      "terminal_nodes": 0,
      "pass_nodes": 0,
      "beta_cutoffs": 80,
      "alpha_updates": 40,
      "pvs_researches": 0,
      "aspiration_fail_lows": 0,
      "aspiration_fail_highs": 0,
      "iid_searches": 0,
      "endgame_nodes": 0,
      "tt_probes": 20,
      "tt_hits": 5,
      "tt_stores": 10,
      "tt_cutoffs": 2,
      "elapsed_ns": 100000,
      "nps": 2500000.0
    }
  ]
}
```

Sanity-check the aggregate JSON before committing it:

```sh
engine/benchmarks/scripts/search/check_baseline.py \
  engine/benchmarks/baselines/search/2026-06-14-<short-sha>-<machine>-<compiler>-release.json
```

The schema check is intentionally light. It verifies the aggregate baseline
shape and required comparison fields; it does not compare timing values or act
as a performance gate.

Keep raw one-off benchmark outputs in `engine/benchmarks/results/`.
