# Benchmark Baselines

Baseline files are optional reference measurements for comparing local changes.
They are environment-dependent and are not correctness gates.

Use baselines to preserve useful historical measurements when:

- the machine and compiler are stable enough to make comparisons meaningful
- the benchmark command and build type are recorded
- the checksum values match the expected behavior

Do not treat a baseline from one machine as a required target on another machine.

Name baseline files with the measurement date, short revision, generic machine
description, compiler, and build type:

```text
YYYY-MM-DD-<short-sha>-<machine>-<compiler>-<build>.json
```

Keep the common top-level metadata envelope consistent across benchmark
families. Use benchmark-specific `benchmark`, option, and `results[]` fields,
because board-core, search, and endgame measure different things. The `machine`
field should match the stable machine token used in the filename, such as
`apple-silicon-macos-arm64`; use `os` for fuller OS, release, and architecture
details when useful.

Suggested JSON shape:

```json
{
  "schema_version": 1,
  "benchmark": "board_core",
  "commit": "<git-sha>",
  "revision": "<short-sha>",
  "measured_at": "YYYY-MM-DD",
  "machine": "<machine-token-from-filename>",
  "os": "<os-release-and-architecture>",
  "compiler": "<compiler-id-and-version>",
  "build_type": "Release",
  "command": "./build-bench/engine/benchmarks/vibe_othello_board_core_bench",
  "selection_policy": "last run after warmup",
  "results": [
    {
      "name": "legal_moves",
      "operations": 1400000,
      "elapsed_ns": 7617875,
      "ns_per_op": 5.44,
      "checksum": "0x49ac6394e6166160"
    }
  ]
}
```

Store domain-specific baseline notes under subdirectories such as
`baselines/board_core/`.
