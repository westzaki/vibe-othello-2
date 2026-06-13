# Progress

Progress documents track current implementation state.

They are not architecture documents.

Use progress documents for:

* current repository status
* completed and pending milestones
* temporary implementation gaps
* benchmark baselines and measurement notes
* rollout notes
* deferred work

Use architecture documents for:

* intended module boundaries
* stable semantics
* invariants
* public API shape
* correctness rules
* long-lived design constraints

Progress documents may point to architecture documents, but they must not
redefine architecture.

When implementation catches up with a design, update the relevant progress
document. When the intended design changes, update the relevant architecture
document.
