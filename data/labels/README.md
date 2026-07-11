# Teacher Label Data

## Purpose

`data/labels/` is for teacher label schema notes, tiny safe fixtures, and
directory policy documentation.

Generated teacher labels are local-only by default. This directory should not
contain local solve output, campaign output, training output, cache payloads, or
machine-specific paths.

## What May Be Committed

Commit OK:

* this README
* schema and policy docs
* tiny repo-owned synthetic fixtures used by tests
* manifest-like metadata files only when they do not contain raw external
  payloads or local machine paths

Checked-in files should stay limited to schema/policy docs and tiny
repo-owned fixtures.

## What Must Stay Local

Commit NG:

* generated exact teacher label TSVs
* generated search teacher label TSVs
* generated move-teacher TSVs
* generated child-normalized TSVs
* teacher-overlaid normalized TSVs
* exact solve caches
* move-teacher caches
* materialized cache outputs
* local campaign outputs
* local matrix outputs
* local growth-cycle outputs
* local reports
* trainer reports
* sweep reports
* logs
* temporary datasets
* source archives
* extracted source files
* local machine paths

Generated teacher, move-teacher, and child-normalized files are not Elo
results, self-play results, production strength claims, or runtime artifacts.

## Teacher Label Schema

Teacher label TSVs are keyed by `board_id`. A teacher row must declare:

* `board_id`
* `label_kind`
* `label_unit`
* `label_perspective`
* `label_score_side_to_move`
* `teacher_source`
* enough solve/search metadata for local review, such as depth and node counts

Typical solve/search metadata fields are `teacher_depth` and `teacher_nodes`.

`label_unit` is `disc`, and `label_perspective` is `side_to_move` for the
teacher labels covered by this policy.

Duplicate identical teacher rows may be tolerated by tools. Conflicting
teacher rows for the same key must be rejected.

Missing-label handling belongs in tool or workflow docs, not in committed data.

## Move-Teacher Label Contract

Move-teacher labels connect root legal moves to child board labels.

Child exact labels are from the child side-to-move perspective. The root move
score is derived as the negative child score.

Child-normalized rows use:

```text
label_kind = teacher_exact_move_child_final_disc_diff
label_unit = disc
label_perspective = side_to_move
```

Move-teacher and child-normalized TSVs are generated local outputs and must
stay out of the repository unless a future tiny repo-owned fixture is added for
tests.

Search move-teacher TSV schema v2 preserves the existing root/child, score,
rank, tie, and margin fields and adds `teacher_kind`, `teacher_artifact_id`,
`teacher_artifact_checksum`, and `teacher_search_config_id`. This provenance is
mandatory for search-generated labels. Search child-normalized rows use
`teacher_search_final_disc_diff`; exact child rows retain
`teacher_exact_move_child_final_disc_diff`.

Child `board_id` values use the canonical `board-v1` SHA-256 identity derived
only from `board_a1_to_h8`. Search materialization de-duplicates equal child
boards only within one split and rejects a canonical child board that crosses
splits. Search scores written to normalized schema v2 must remain in `[-64,64]`;
out-of-range values are rejected rather than clamped.

## Directory Rules

Keep this directory small and reviewable.

Do not add generated label payloads, cache payloads, local reports, local
workflow outputs, source payloads, or absolute local paths.

Do not use files in this directory to make strength, release, or runtime
artifact claims. This directory only defines the label data boundary and the
minimum schema contract.

## Where Details Live

Use these docs for the broader workflow and historical context:

* Pattern learning architecture and data flow:
  `../../docs/architecture/pattern-learning.md`
* Pattern learning current status: `../../docs/progress/pattern-learning.md`
* Historical experiment logs: `../../docs/experiments/README.md`
* Corpus source policy: `../corpora/README.md`
* Evaluation artifact directory policy: `../eval/README.md`
* Artifact layout and commit policy:
  `../../docs/architecture/evaluation-artifacts.md`
