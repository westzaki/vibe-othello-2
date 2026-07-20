# Corpus Source Data

## Purpose

`data/corpora/` is the source-data policy directory for pattern learning
corpora: source manifests, tiny checked-in fixtures, and policy docs.

Raw third-party corpora are local-only by default. This repository does not
redistribute raw Egaroucid archives, extracted files, transcript dumps,
board-score archives, or WTHOR game database payloads.

## What May Be Committed

Committed files are limited to:

* source manifests under `data/corpora/manifests/` and repo-owned sample
  manifests under `data/corpora/samples/`
* `dataset-manifest.schema.json`
* this policy README and related tiny policy files
* repo-owned tiny fixtures that are safe to redistribute and useful for smoke
  coverage

Small synthetic fixtures may be committed only when repo-owned and safe.
External corpus names may appear only as policy context, not payloads.

## What Must Stay Local

Do not commit generated or external corpus data under `data/corpora/`,
including:

* raw external archives
* extracted external source files
* Egaroucid raw transcripts or board-score archives
* WTHOR `.wtb`, `.jou`, `.trn`, or downloaded archive files
* normalized TSVs
* selected TSVs
* resplit normalized TSVs
* generated pattern datasets
* local import reports
* local training reports
* sweep reports
* trainer reports
* logs
* temporary datasets
* local machine paths
* generated teacher labels or move-teacher outputs

The committed WTHOR manifest identifies the source and local snapshot. Raw
source files and generated training data remain local. The local import report
owns the exact per-file and aggregate checksums.

## Manifest Contract

External or local-only corpus inputs must be identified by a manifest before
they are used by pattern learning data workflows.

Manifests should record:

* source name
* source URL, source description, or durable source reference
* retrieved date when known
* license or terms
* redistribution and other permission flags
* checksum when available
* local-only path placeholder, without personal absolute paths
* review notes for restrictions, attribution, and provenance caveats

The current schema is `dataset-manifest.schema.json`. Raw data and derived
datasets remain local-only. Reviewed runtime artifacts follow `data/eval/`
policy.

## Directory Rules

Keep this directory focused on source manifests, tiny safe fixtures, and policy
docs. Do not place local machine paths, generated outputs, experiment reports,
training outputs, or cache materialization outputs here.

Generated normalized TSVs, selected TSVs, datasets, reports, logs, caches,
teacher labels, and move-teacher outputs belong in ignored local storage.

For an external `VIBE_OTHELLO_LOCAL` root, use this maintenance layout:

```text
vibe-othello-local/
├─ corpora/       # downloaded archives and other read-only source inputs
├─ training/      # normalized TSVs, pattern datasets, weights, and run reports
└─ measurements/  # arena, benchmark, and comparison evidence
```

The Egaroucid board-score importer reads
`corpora/Egaroucid_Train_Data.zip` directly. The sampled runner creates one
immutable run directory under `training/egaroucid-board-score/`. Full streaming
training uses extracted text under
`training/cache/egaroucid-board-score-v2025-02-02/`; it never writes generated
or extracted files into `corpora/`.

Do not commit copied GPL engine code, GPL evaluation weights, GPL-derived
tables, or line-by-line translations of GPL implementation details. Use public
papers, high-level descriptions, black-box comparisons, or independently generated
data only when terms permit.

## Where Details Live

* Pattern learning architecture and data flow: `../../docs/architecture/pattern-learning.md`
* External corpus import workflows: `../../tools/data-import/README.md`
* Pattern learning current status: `../../docs/progress/pattern-learning.md`
* Historical experiment logs: `../../docs/experiments/README.md`
* Teacher label and move-teacher local policy: `../labels/README.md`
* Evaluation artifact directory policy: `../eval/README.md`
