# Benchmark Tool Helpers

`baseline_common.py` contains shared helpers for benchmark baseline scripts:
JSON loading/writing, common envelope checks, low-level type checks, and small
metadata helpers.

Keep benchmark-specific result schemas in the benchmark-specific tools. Search,
endgame, and board-core baselines measure different things, so their `results[]`
validation should not be forced into one shared schema.
