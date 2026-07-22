# Evaluation Artifact Data

## Purpose

`data/eval/` contains committed runtime evaluation artifacts and the default
artifact pointer used by the engine.

Reviewed learned runtime artifacts live under
`data/eval/artifacts/<artifact-id>/`. The default artifact pointer is
`data/eval/default-artifact.json`.

## What May Be Committed

`data/eval/default-artifact.json` may be committed.

Committed artifact payloads are limited to final runtime files:

* `weights.bin`
* `manifest.json`
* `provenance.json`
* `README.md`
* `NOTICE.md`

Artifact-specific details belong in each artifact directory's `README.md` and
`provenance.json`.

Reviewed artifacts must declare `trained_phases` in both manifest and
provenance. This coverage metadata is distinct from phase-by-phase nonzero
weight diagnostics and is not inferred from them.

## What Must Stay Local

Do not commit generated or source data under `data/eval/`, including:

* raw external corpora
* normalized TSVs
* selected TSVs
* teacher labels
* move-teacher TSVs
* child-normalized TSVs
* cache materialization outputs
* local training reports
* trainer reports
* sweep reports
* logs
* temporary datasets
* candidate exports before artifact review
* local machine paths
* source archives
* extracted source files

## Directory Rules

Keep this directory limited to the default pointer, this policy README, and
reviewed artifact directories.

Committed artifacts must contain final runtime payloads only. Keep intermediate
training, labeling, corpus, cache, report, and experiment outputs outside this
directory or in local-only storage.

Do not place machine-specific absolute paths in committed files.

## Web Runtime Copies

The Web build can copy the current default artifact into
`apps/web/public/eval/` for local browser runs, Web CI, and GitHub Pages builds.
Those copied files are ignored runtime assets. This directory remains the source
of truth for committed evaluation artifacts and the default artifact pointer.

## Where Details Live

Artifact layout, default pointer, manifest, provenance, promotion, rollback,
commit policy: `../../docs/architecture/evaluation-artifacts.md`

Runtime evaluation architecture and current implementation:
`../../docs/architecture/evaluation.md`

Pattern learning pipeline, trainer/exporter responsibilities, and current route:
`../../docs/architecture/pattern-learning.md`.

Corpus source policy: `../corpora/README.md`

Teacher label policy: `../labels/README.md`
