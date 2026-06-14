# Evaluation Artifact Policy

This directory owns metadata and local placement policy for pattern-evaluation
artifacts consumed by the runtime evaluation module.

Weights are data, not code. A production artifact should consist of:

* a readable artifact manifest, such as `pattern-v1.manifest.json`
* a binary weights payload, such as `pattern-v1.weights.bin`

The manifest may be committed when it contains no restricted data and no
personal local paths. Binary weights and large generated artifacts should live
in releases, Git LFS, or local download caches, not normal git history.

## Artifact Manifests

Artifact manifests should record:

* artifact id and format version
* engine compatibility
* bit order, score unit, score scale, phase count, and pattern set id
* binary weights filename and SHA-256
* training run id or training manifest reference
* dataset manifest ids used to produce the artifact
* source data policy status
* attribution and redistribution notes

An artifact derived from any dataset with unknown license terms or unknown
permissions is local-only and must not be published unless a later review clears
the source terms.

## Binary Weights

Binary weights may be placed locally under this directory while developing, but
large artifacts are ignored by default and should not be committed.

Recommended local layout:

```text
data/eval/
|-- README.md
|-- pattern-v1.manifest.json
`-- local/
    `-- pattern-v1.weights.bin
```

The runtime loader validates binary compatibility and checksums. Artifact
exporters must not rely on file size alone to infer schema or pattern layout.

## Source Boundaries

Do not place raw training corpora in `data/eval/`.

Do not copy GPL engine code, GPL weights, GPL-derived tables, or release
artifacts whose source manifests do not allow the intended redistribution.

This directory currently contains policy only. No learned weights or external
corpus-derived artifacts are committed.
