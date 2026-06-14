# 20-Empty Endgame Experiment Notes

This is a local exploratory measurement note for expensive exact endgame
positions. It is not a benchmark baseline.

Do not commit raw high-empty JSONL output or machine-specific benchmark
baselines from exploratory runs. Keep generated JSONL files in local scratch
space and put detailed run context in the pull request description.

## Focused Matrix

Start high-empty investigation with one selected position, `--repeat 1`, and a
per-position timeout through the high-empty probe runner. For the checked-in
20-empty probe, compare these rows before expanding the matrix:

* `--root-mode best --parity on --tt on`
* `--root-mode best --parity off --tt on`
* `--root-mode best --parity on --tt off`
* `--root-mode all --parity on --tt on`

Keep the checked-in corpus explicit:

```sh
python3 engine/benchmarks/scripts/endgame/run_high_empty_probe.py \
  --bench ./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --position-id twenty_empty_simple \
  --root-mode best \
  --parity on \
  --tt on \
  --repeat 1 \
  --timeout-sec 180 \
  --output-dir /tmp/vibe-endgame-matrix-20/best-parity-on-tt-on
```

## Local Result

Local runner check on `twenty_empty_simple`, `repeat 1`, timeout 180 seconds,
Release benchmark build, checked-in endgame corpus:

* Measured on: 2026-06-15 JST
* Measured branch: `codex/endgame-20-empty-investigation`
* Measured commit: `98dbad8`
* Rebased on: `origin/main` at `aa3edaf`
* Runner: `engine/benchmarks/scripts/endgame/run_high_empty_probe.py`
* Local raw output: `/tmp/vibe-endgame-matrix-20-runner`

| root | parity | TT | status | elapsed_ms_p50 | nodes_p50 | endgame_nodes_p50 | nps_p50 | tt_hit_rate | tt_cutoff_rate | root_moves_searched | score | best_move | exact | stopped |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| best | on | on | completed | 52395.474 | 137141585 | 137141585 | 2617431.880 | 0.0000455 | 0.0000351 | 10 | 4 | c1 | true | false |
| best | off | on | completed | 64152.933 | 170878772 | 170878772 | 2663615.889 | 0.0000465 | 0.0000335 | 10 | 4 | c1 | true | false |
| best | on | off | completed | 51613.457 | 218046623 | 218046623 | 4224607.963 | 0 | 0 | 10 | 4 | c1 | true | false |
| all | on | on | completed | 179428.954 | 469552012 | 469552012 | 2616924.422 | 0.0000157 | 0.0000114 | 10 | 4 | c1 | true | false |

## Interpretation

Compare deterministic fields first: `score`, `best_move`, `exact`, `stopped`,
`pv`, and root-move metadata. Then compare `nodes`, `endgame_nodes`,
`root_moves_searched`, TT counters, elapsed time, and NPS.

For this local run, parity ordering reduced best-mode nodes by about 19.7% with
TT enabled. TT reduced best-mode nodes by about 37.1% with parity enabled, but
the hit and cutoff rates were very low and elapsed time was close to TT-off
because per-node throughput dropped. All-root reporting completed just under the
180-second timeout and visited about 3.4x the best-mode baseline nodes.

Prefer node-count interpretation over elapsed time for tree-shape changes. If
NPS is broadly stable while nodes grow, prioritize search-tree reduction before
micro-optimizing per-node cost.

Use `--root-mode best` for practical high-empty measurement unless all-root
candidate reporting is the explicit question.
