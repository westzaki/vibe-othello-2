# Experiment Log Archive

This directory contains historical experiment notes. These docs are not part of
the default reading path for normal development; read them only when a task needs
the specific experiment context.

For current pattern learning design and workflow, start with
`../architecture/pattern-learning.md`, `../architecture/evaluation-artifacts.md`,
`../progress/pattern-learning.md`, `../../data/corpora/README.md`,
`../../data/labels/README.md`, and `../../data/eval/README.md`.

| Doc | Scope | Current status | Superseded by / Notes |
| --- | --- | --- | --- |
| `pattern-learning-early-diagnostics.md` | Consolidated early pattern-learning diagnostics from sequence import through v0c/v0d, exact-root, bottleneck, and v2-vs-v1 arena checks. | Historical summary. | Explains why the old observed-label MAE and exact-root routes were insufficient before move-teacher validation. |
| `pattern-move-teacher-decision-leverage.md` | Move-teacher decision-leverage campaign and bounded arena check. | Historical experiment log. | Superseded by later scale and growth-cycle validation notes. |
| `pattern-move-teacher-decision-leverage-scale.md` | Scale matrix for move-teacher decision-leverage runs. | Historical experiment log. | Later growth-cycle notes summarize the follow-up path. |
| `pattern-move-teacher-cache.md` | Move-teacher exact label cache behavior, CLI, and local validation notes. | Historical workflow note. | Use current data policy docs before adding or materializing labels. |
| `pattern-learning-validation-history.md` | Consolidated validation history for the initial growth-cycle, 50k, repeated 50k, connected 100k, and broader arena path. | Historical summary. | Use current pattern learning progress and evaluation artifact docs for present implementation status and artifact policy. |
