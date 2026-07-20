# Evaluation Artifact Architecture

## Purpose

Evaluation artifacts are runtime data for the pattern evaluator. They are
committed only after a specific promotion decision and validation summary.

The current committed artifact is:

```text
data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/
```

It is the experimental default, not a production strength claim.

## Directory Structure

```text
data/eval/
|-- README.md
|-- default-artifact.json
`-- artifacts/
    `-- <artifact-id>/
        |-- weights.bin
        |-- manifest.json
        |-- provenance.json
        |-- README.md
        `-- NOTICE.md
```

Only the final runtime payload is committed. Raw transcripts, normalized
corpora, selected TSVs, teacher labels, move-teacher TSVs, cache files, local
reports, trainer reports, datasets, and logs remain local-only.

## Default Pointer

`data/eval/default-artifact.json` is the central default pointer.

Schema:

```json
{
  "schema_version": 1,
  "default_artifact_id": "pattern-v2-egaroucid-lv17-full-value-v1",
  "status": "experimental-default",
  "artifact_manifest": "artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json",
  "artifact_provenance": "artifacts/pattern-v2-egaroucid-lv17-full-value-v1/provenance.json",
  "reason": "...",
  "override": {
    "custom_artifact": "--eval-artifact <manifest-path>",
    "static_eval": "--eval-mode static"
  },
  "non_claims": []
}
```

`artifact_manifest` is resolved relative to `data/eval/`. It must stay inside
that directory and must not be an absolute path.

## Runtime Manifest

`manifest.json` is the loader contract. It records:

* `format`
* `format_version`
* `bit_order`
* `score_unit`
* `score_scale`
* `phase_count`
* `trained_phases`, when the artifact reports reviewed learning coverage
* `fallback_additive_through_phase`, when learned weights are residuals over
  the built-in fallback through an inclusive phase boundary
* `pattern_set_id`
* `weights_file`
* `weights_checksum`
* pattern table shape and phase metadata

`trained_phases` is an array of unique runtime phase ids in `0..12`. It is the
source of truth for learning coverage and can represent non-contiguous phase
sets. A missing field is legacy/unreported coverage, not a claim of all-phase
training; the runtime loader remains backward compatible with such artifacts.
Committed reviewed artifacts must report the field in both manifest and
provenance, with matching values.

`score_scale` is a positive unsigned 16-bit fixed-point denominator. Runtime
sums the integer bias and pattern weights, divides once by this scale with
nearest rounding, and then applies the evaluator score bound. Scale `1`
preserves legacy integer artifacts; larger values retain sub-disc learned
updates without changing the binary format version.

When `fallback_additive_through_phase` is present, a covered phase at or below
that boundary evaluates as the deterministic fallback plus the learned pattern
score. Covered phases above it remain learned replacements, and uncovered
phases remain fallback-only. This field is evaluator policy and therefore must
be included in arena identity and promotion review.

The manifest also records phase diagnostics derived from final quantized binary
weights: nonzero pattern-weight count, nonzero phase-bias presence, and maximum
absolute pattern weight. These diagnostics are sanity data only and never infer
or replace `trained_phases`.

`weights_file` is resolved relative to the artifact directory. The runtime
resolver rejects absolute paths and parent traversal.

The runtime checksum is the existing pattern-artifact CRC32 over `weights.bin`
excluding its trailing checksum field. The binary loader then validates the
embedded checksum and schema-compatible pattern layout.

## Provenance

`provenance.json` records publication metadata that should not be required by
the hot runtime loader:

* artifact id, version, status, pattern set, and trainer mode
* teacher kind and label derivation
* selected source checksum
* source attribution
* redistribution flags
* weight SHA-256
* runtime checksum
* reviewed `trained_phases`
* reproducible trainer/export config and local report checksums
* independently generated promotion-opening identity and source-overlap audit
* direct validation summary against the artifact being replaced
* non-claims

The current WHTOR artifact attributes the FFO database snapshot, records the
full-corpus played-move training and rejected saturation follow-ups, and states
that it is not an official WHTOR or FFO artifact. Its frozen late-game lineage
retains the previous Egaroucid attribution. Promotion uses an independent
board-core-generated opening suite whose board and transcript-prefix overlap
with WHTOR is audited, then directly compares the final candidate with the
previous default. Bootstrap comparisons remain learning evidence only. The
previous progressive-search artifact remains committed as the rollback target.

## Resolution Order

When no explicit evaluation option is supplied, the engine CLI:

1. reads `data/eval/default-artifact.json`
2. resolves `artifact_manifest` relative to `data/eval/`
3. reads the runtime manifest
4. resolves `weights_file` relative to the manifest directory
5. validates manifest fields, runtime checksum, binary checksum, pattern set,
   phase count, score unit, score scale, and pattern table layout
6. constructs `PhaseAwareEvaluator`, which uses `PatternEvaluator` for covered
   phases, optionally adds it as a residual over the fallback through the
   declared boundary, and uses the fallback alone for uncovered phases

Failure is loud. A missing or corrupt default artifact never silently falls
back to static evaluation.

Override paths:

* `--eval-artifact <manifest-path>` loads a custom artifact.
* `--eval-mode static` forces the legacy static evaluator.

An artifact with `trained_phases` uses phase-aware routing for both default and
explicit manifest paths. A legacy artifact without that field remains
all-phase learned at runtime for backward compatibility; this is not an
inference that its training coverage was all-phase.

Legacy smoke tooling may still pass `--eval pattern --pattern-weights PATH`
with `--pattern-set`, but committed defaults should use manifest-based loading.

## Commit Policy

The CTest target `vibe_othello_eval_artifact_commit_policy` runs
`tools/pattern/artifacts/check_eval_artifact_commit_policy.py`.

It validates:

* every artifact directory has exactly the required five files
* `weights.bin` exists and is non-empty
* `manifest.json` references `weights.bin`
* provenance redistribution flags reject raw data and teacher-label
  redistribution
* `not_official_egaroucid_artifact` is true
* metadata does not contain local absolute paths
* committed `weights.bin` SHA-256 matches provenance
* manifest and provenance runtime checksums match the binary payload
* manifest and provenance `trained_phases` are present, valid, and equal
* phase diagnostics and aggregate nonzero pattern-weight count match the final
  binary payload
* tracked archive, TSV, log, and generated evaluation intermediate names are
  not committed outside checked-in smoke fixture/sample locations

## Promotion

To promote a future artifact:

1. create a new artifact id and directory under `data/eval/artifacts/`
2. copy only the final runtime `weights.bin`
3. write a loader-compatible `manifest.json` with relative `weights_file` and
   reviewed `trained_phases`, fixed-point scale, and any residual-routing policy
4. write `provenance.json` with source attribution, redistribution flags,
   reproducible trainer/export settings, report checksums, independent opening
   identity and overlap audit, direct comparison with the current default,
   checksum, and non-claims
5. update `data/eval/default-artifact.json`
6. update `data/eval/README.md`, this document, and progress docs
7. run the commit policy checker and loader smoke tests

Do not overwrite an existing artifact directory with semantically different
weights.

## Rollback

Rollback is a pointer change:

1. restore `default-artifact.json` to the previous `artifact_manifest`
2. keep both artifact directories committed unless repository policy explicitly
   removes the reverted candidate
3. document why the default changed
4. run the same loader and commit-policy tests
