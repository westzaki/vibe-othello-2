# Corpus Data Policy

This directory owns dataset manifests for pattern learning.

Raw third-party corpora are not committed here. Keep downloaded game records,
source archives, derived intermediate datasets, and source-specific scratch
files in local-only paths such as `data/corpora/local/`, `data/corpora/raw/`,
or an external cache.

Local measurement suites for external corpora should also write under ignored
local paths, for example `data/corpora/local/measurements/<suite-id>/`.
Suite reports, analyzer summaries, normalized TSVs, sequence replay caches,
pattern datasets, trainer reports, learned weights, exported artifacts, and
logs from those runs remain local-only and must not be committed.

For long local measurements, keep a short operator note or progress journal in
the same ignored tree, for example
`data/corpora/local/measurements/perf-notes/<run-id>.md`. Prefer absolute paths
inside the note for the active suite output directory, shared sequence cache,
stderr log, suite report, and resume command so another Codex workspace can
pick up the run without rediscovering local state.

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
