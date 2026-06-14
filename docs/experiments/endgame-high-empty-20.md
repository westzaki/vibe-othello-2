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
