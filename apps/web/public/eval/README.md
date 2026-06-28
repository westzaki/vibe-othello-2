# Web Evaluation Artifact Assets

This directory receives copied runtime evaluation artifacts for local browser
runs, Web CI, and GitHub Pages builds.

`data/eval/` remains the source of truth for committed evaluation artifacts and
the default artifact pointer. Files copied here are app runtime assets only and
are intentionally ignored by git.

The browser Worker fetches the default pointer from:

```text
eval/default-artifact.json
```

The copy must preserve the relative layout from `data/eval/` so the default
pointer can resolve the artifact manifest, and the manifest can resolve
`weights.bin` from its own directory.
