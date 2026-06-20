# Corpus Data Policy

This directory owns dataset manifests for pattern learning.

Raw third-party corpora are not committed here. Keep downloaded game records,
source archives, derived intermediate datasets, and source-specific scratch
files in local-only paths outside the repository/worktree by default.

## Local-only Measurement Directories

Use generic local environment variables for day-to-day corpus and measurement
work:

```sh
export VIBE_OTHELLO_LOCAL="${VIBE_OTHELLO_LOCAL:-$HOME/vibe-othello-local}"
export VIBE_OTHELLO_CORPORA="${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}"
export VIBE_OTHELLO_SEQUENCE_CACHE="${VIBE_OTHELLO_SEQUENCE_CACHE:-$VIBE_OTHELLO_LOCAL/sequence-cache}"
export VIBE_OTHELLO_MEASUREMENTS="${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}"

mkdir -p "$VIBE_OTHELLO_CORPORA"
mkdir -p "$VIBE_OTHELLO_SEQUENCE_CACHE"
mkdir -p "$VIBE_OTHELLO_MEASUREMENTS"
```

`VIBE_OTHELLO_LOCAL` is the optional root for all local-only data.
`VIBE_OTHELLO_CORPORA` is for local corpus inputs. `VIBE_OTHELLO_SEQUENCE_CACHE`
is the shared sequence replay cache and should be shared across worktrees.
`VIBE_OTHELLO_MEASUREMENTS` is the measurement suite output root.

Use generic paths only in committed docs and examples, such as
`$HOME/vibe-othello-local`, `$VIBE_OTHELLO_LOCAL`, `<repo-root>`,
`<sequence-input>`, and `<sequence-manifest>`. Do not commit personal paths,
developer-specific directories, `.env` files with real values, generated
measurement outputs, external corpus payloads, TSVs, learned weights,
artifacts, reports, or logs.

Local measurement suites for external corpora should also write under ignored
local paths, normally under `$VIBE_OTHELLO_MEASUREMENTS/<suite-name>`. Suite
reports, analyzer summaries, normalized TSVs, sequence replay caches, pattern
datasets, trainer reports, learned weights, exported artifacts, and logs from
those runs remain local-only and must not be committed.

For sequence-derived normalized schema v2 measurements, use
`--measurement-split-policy connected-board-game` when validation/test leakage
hygiene matters. It keeps rows sharing a semantic game or identical
side-to-move-relative board in the same local measurement split. This may change
split counts and is only a measurement split policy, not a strength,
publication, label-quality, or artifact-release claim.

For long local measurements, keep a short operator note or progress journal in
the same ignored tree, for example
`$VIBE_OTHELLO_MEASUREMENTS/perf-notes/<run-id>.md`. Include the active suite
output directory, shared sequence cache, stderr log, suite report, and resume
command so another workspace can pick up the run without rediscovering local
state. Keep any note with real local paths out of git.

## Dataset Manifests

Every dataset used for pattern learning must have a manifest before importer,
feature extraction, training, or artifact export uses it.

The current manifest schema is:

* `data/corpora/dataset-manifest.schema.json`

Tiny checked-in examples may live under `data/corpora/samples/` when they do
not include raw external corpus content. Repository-local synthetic records in
that directory are allowed for importer and replay smoke tests.

Minimum manifest fields are:

* `dataset_id`
* `source_name`
* `source_url`
* `retrieved_at`
* `license_or_terms`
* `redistribution_allowed`
* `commercial_use_allowed`
* `derived_weights_allowed`
* `required_attribution`
* `local_path`
* `sha256`
* `notes`

Use `unknown` for permission fields only when the answer has not been reviewed.
Any dataset with unknown license terms or unknown permissions is local-only and
must not be used for published raw data, derived datasets, learned weights, or
release artifacts.

## Provenance Rules

Manifests must record enough provenance to answer:

* where the data came from
* when it was retrieved or generated
* what license or terms apply
* whether redistribution is allowed
* whether commercial use is allowed
* whether learned weights may be derived and published
* what attribution is required
* where the local uncommitted copy lives
* which exact payload checksum was used

Do not commit personal absolute paths in `local_path`. Prefer a relative
repository path for checked-in synthetic samples, or `not-applicable` for
manifest-only synthetic samples.

## GPL Boundary

Do not copy GPL engine code, GPL evaluation weights, GPL-derived tables, or
line-by-line translations of GPL implementation details into this repository.

Public papers, high-level descriptions, independently written code, black-box
comparison, and locally generated self-play are acceptable inputs when their
own terms permit the intended use.

## Validation

The manifest smoke validator checks the checked-in sample without external
network access:

```sh
python3 tools/data-policy/validate_dataset_manifest.py \
  --schema data/corpora/dataset-manifest.schema.json \
  data/corpora/samples/tiny-local-synthetic.manifest.json
```

CTest also runs this check when testing is enabled.

The checked-in tiny synthetic records are replayed by the data-import smoke
tool through board-core move application. They are intended only to verify that
known-good rows are accepted and malformed, illegal, or bad-pass rows are
rejected before any external corpus is considered.
