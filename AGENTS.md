# AGENTS.md

## Working Style

Act autonomously and make steady progress.

Before editing, inspect the relevant code, tests, and project docs. Do not ask for clarification unless the task is blocked by a missing product decision, an unsafe/destructive action, or conflicting instructions.

Prefer the smallest coherent change that solves the task. Avoid broad rewrites, speculative features, and unrelated cleanup.

When unsure, choose the simpler, more testable design and state the assumption in the final response.

## Documentation

Start with `README.md`.

Use `docs/README.md` as the documentation index. Do not read all docs by default.

Classify the task area, then read only the smallest relevant set of docs. For cross-boundary changes, read the docs for each affected area and the boundary doc.

When planning, state the task area and selected docs.

Keep the docs index in `docs/README.md`, not here.

If code/tests/docs disagree, code and tests are current behavior; docs are intended design. Mention mismatches before changing behavior.

Update relevant docs when changing public behavior, architecture, layout, or workflow.

## Pull Request Rule

When creating a pull request for this project:

1. use `.github/PULL_REQUEST_TEMPLATE.md`
2. write the PR body in Japanese
3. write only the PR title in English
4. format the PR title as a Conventional Commit, for example `feat: add legal move generation`

For behavior, strength, evaluation, search, or performance PRs, include the
relevant measurement or explain why measurement was not possible. For
tooling-only, documentation-only, or refactor-only PRs, explain what they enable.

## Final Response

Summarize:

* what changed
* what was tested
* anything not tested
* any important assumptions or follow-ups
