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

## WLD Vs Exact-Score Comparison

Use this local comparison to decide whether direct WLD search is ready to drive a
later root-triggered orchestration change. Keep the raw JSONL in local scratch
space and report the summary table in the pull request description.

```sh
python3 engine/benchmarks/scripts/endgame/run_high_empty_probe.py \
  --bench ./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --position-id fourteen_empty_simple \
  --position-id sixteen_empty_simple \
  --position-id eighteen_empty_simple \
  --position-id twenty_empty_simple \
  --mode both \
  --root-mode best \
  --parity on \
  --tt on \
  --repeat 1 \
  --timeout-sec 180 \
  --output-dir /tmp/vibe-endgame-wld-vs-exact
```

Interpret completed pairs by checking deterministic agreement first: exact-score
rows with positive, zero, or negative `score` must match WLD rows with `win`,
`draw`, or `loss` respectively. Compare `nodes_p50` and `endgame_nodes_p50`
before elapsed time, because local timing is machine- and load-sensitive. If WLD
substantially reduces nodes across the selected positions, use a follow-up PR to
evaluate root-triggered WLD orchestration. If it does not, improve WLD ordering,
WLD TT behavior, or WLD small-empty specialization before adding orchestration.

## Root-Triggered WLD Threshold Probe

Local runner check on 2026-06-15 JST measured the root-triggered
`search_iterative` WLD path with `--root-mode best`, `--parity on`, `--tt on`,
`--repeat 1`, and a 180-second per-row timeout. Raw JSONL output stayed in
local scratch space under `/tmp/vibe-endgame-wld-threshold-pr`.

Direct WLD agreed with the exact-score sign for all measured positions:

| position | empties | exact score | direct WLD | direct WLD elapsed_ms | direct WLD nodes |
| --- | ---: | ---: | --- | ---: | ---: |
| `fourteen_empty_simple` | 14 | -26 | loss | 28.609 | 42713 |
| `sixteen_empty_simple` | 16 | -11 | loss | 179.430 | 324395 |
| `eighteen_empty_simple` | 18 | -8 | loss | 801.072 | 1450690 |
| `twenty_empty_simple` | 20 | 4 | win | 23299.300 | 42434203 |

Threshold gating behaved as expected: rows below the root empty count reported
`not_triggered` with zero endgame nodes and one evaluator call; rows at or above
the root empty count triggered WLD and completed exactly.

| position | empties | completed thresholds | not-triggered thresholds | score | best move | elapsed_ms range | nodes | tt_hit_rate | tt_cutoff_rate |
| --- | ---: | --- | --- | ---: | --- | ---: | ---: | ---: | ---: |
| `fourteen_empty_simple` | 14 | 14, 16, 18, 20, 22, 24 | none | -1 | c1 | 23.538-29.466 | 42713 | 0.0217966 | 0.0217498 |
| `sixteen_empty_simple` | 16 | 16, 18, 20, 22, 24 | 14 | -1 | c1 | 175.030-180.874 | 324395 | 0.00603277 | 0.00570292 |
| `eighteen_empty_simple` | 18 | 18, 20, 22, 24 | 14, 16 | -1 | c1 | 806.429-835.618 | 1450690 | 0.00236026 | 0.00217896 |
| `twenty_empty_simple` | 20 | 20, 22, 24 | 14, 16, 18 | 1 | c1 | 23180.300-23211.900 | 42434203 | 0.00013171 | 0.000115143 |

All completed root-triggered WLD rows returned WLD scores only (`-1` or `1`),
reported `exact=true`, `stopped=false`, `completed_depth == empties`, and
`eval_calls == 0`.

Recommendation: keep the production default disabled until a broader corpus and
WASM/native split are measured, but document `endgame_wld_empties = 20` as a
reasonable native manual/experimental threshold for best-only WLD outcome
queries on the current checked-in high-empty probes. The remaining risks are the
small generated corpus, machine-specific timing, WLD outcome-only scoring, no
WASM tuning, and no top-N Multi-PV reporting yet.

## Broader Corpus Follow-Up

The checked-in endgame corpus currently has no additional high-empty rows beyond
`fourteen_empty_simple`, `sixteen_empty_simple`, `eighteen_empty_simple`, and
`twenty_empty_simple`. A broader real-game or randomized high-empty corpus should
be a follow-up PR. As a small same-PR check, the existing lower-empty corpus rows
were also measured without adding new fixture data.

Local runner check on 2026-06-15 JST used the full checked-in endgame corpus,
`--root-mode best`, `--parity on`, `--tt on`, `--repeat 1`, and a 180-second
per-row timeout. Raw JSONL output stayed in local scratch space under
`/tmp/vibe-endgame-wld-broader-direct-pr` and
`/tmp/vibe-endgame-wld-threshold-broader-pr`.

Direct WLD matched the exact-score sign for all 14 checked-in rows. The
additional lower-empty rows were:

| position | category | empties | exact score | direct WLD | best move | direct WLD nodes |
| --- | --- | ---: | ---: | --- | --- | ---: |
| `zero_empty_terminal` | zero_empty | 0 | 16 | win | none | 1 |
| `one_empty_forced_move` | one_empty | 1 | 64 | win | h8 | 2 |
| `one_empty_forced_pass` | forced_pass | 1 | 58 | win | pass | 3 |
| `two_empty_simple` | generated | 2 | 2 | win | h7 | 3 |
| `three_empty_simple` | generated | 3 | -2 | loss | f3 | 4 |
| `four_empty_simple` | generated | 4 | 18 | win | f3 | 21 |
| `six_empty_simple` | generated | 6 | 2 | win | f3 | 86 |
| `eight_empty_simple` | generated | 8 | 2 | win | g7 | 624 |
| `ten_empty_simple` | generated | 10 | 0 | draw | a7 | 1747 |
| `twelve_empty_simple` | generated | 12 | 0 | draw | a7 | 6201 |

Root-triggered WLD was then measured with thresholds 16, 18, and 20 across all
14 rows. All completed root-triggered WLD rows matched direct WLD on score,
WLD result, best move, exact/stopped flags, and completed depth. Threshold
gating also behaved correctly: the 18-empty row reported `not_triggered` at
threshold 16, and the 20-empty row reported `not_triggered` at thresholds 16 and
18. All other rows triggered and completed. There were no timeouts or failed
rows.

This follow-up does not change the recommendation. The default should remain
disabled, and `endgame_wld_empties = 20` remains only a native
manual/experimental threshold for best-only WLD outcome queries. The evidence is
still limited because the broader checked-in rows are small deterministic
positions rather than a broad real-game high-empty corpus.

## Parity Region Shootout

Local shootout on 2026-06-15 JST after the opponent-mobility endgame ordering
hint landed on `origin/main` at `fa17bec`. The goal was to test whether the
parity/region hint could still reduce exact endgame nodes after the mobility
hint. These exploratory results used a Release benchmark build, `repeat 1`,
`--root-mode best`, `--parity on`, and `--tt on`. Raw JSONL output stayed in
local scratch space under `/tmp/vibe-endgame-parity-shootout`.

| candidate | position | status | elapsed_ms_p50 | nodes_p50 | nps_p50 | note |
| --- | --- | --- | ---: | ---: | ---: | --- |
| baseline, odd region bonus 10k | `eighteen_empty_simple` | completed | 1853.810 | 4491953 | 2423092.442 | current main after mobility hint |
| odd region bonus 5k | `eighteen_empty_simple` | completed | n/a | 4491953 | n/a | same node count as baseline |
| odd region bonus 15k | `eighteen_empty_simple` | completed | n/a | 4491953 | n/a | same node count as baseline |
| odd region bonus 18k | `eighteen_empty_simple` | completed | 1785.781 | 4487504 | 2512908.358 | improves baseline, worse than 20k |
| odd region bonus 19k | `eighteen_empty_simple` | completed | 1806.374 | 4500987 | 2491724.802 | worse than baseline |
| odd region bonus 20k | `eighteen_empty_simple` | completed | 1787.369 | 4470360 | 2501083.996 | best 18-empty candidate |
| odd region bonus 21k | `eighteen_empty_simple` | completed | 1836.177 | 4600056 | 2505235.947 | worse than baseline |
| odd region bonus 22k | `eighteen_empty_simple` | completed | 1847.135 | 4627474 | 2505216.998 | worse than baseline |
| odd region bonus 25k | `eighteen_empty_simple` | completed | n/a | 4627474 | n/a | worse than baseline |
| odd region bonus 50k | `eighteen_empty_simple` | completed | 1849.867 | 4637292 | n/a | worse than baseline |
| odd 20k, even region penalty -5k | `eighteen_empty_simple` | completed | n/a | 4627474 | n/a | worse than plain 20k |
| odd 20k, smaller odd region tiebreak | `eighteen_empty_simple` | completed | n/a | 4905821 | n/a | worse than plain 20k |
| odd 20k, larger odd region tiebreak | `eighteen_empty_simple` | completed | n/a | 8683723 | n/a | much worse |
| 8-neighbor regions, odd 20k | `eighteen_empty_simple` | completed | 2099.788 | 5274797 | n/a | worse than 4-neighbor regions |
| baseline, odd region bonus 10k | `twenty_empty_simple` | completed | 40027.835 | 100224252 | 2503863.918 | current main after mobility hint |
| odd region bonus 20k | `twenty_empty_simple` | completed | 39695.366 | 99527583 | 2507284.663 | reduced 696669 nodes from baseline |

The best candidate in this focused pass was keeping the existing 4-neighbor
region definition and raising the odd-region ordering bonus from 10k to 20k.
The larger structural variants were negative on the 18-empty probe, so they
should not be promoted without a broader position set showing a compensating
win elsewhere.
