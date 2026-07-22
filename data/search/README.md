# Search Runtime Data

`data/search/profiles/` contains the narrow, reviewed runtime inputs needed to
reproduce a production search calibration profile. It is not a storage area for
raw calibration samples or benchmark output.

A committed production profile must include its reviewed adoption
specification, compact reviewed JSON, aggregate performance-gate evidence, a
human-readable evidence summary, exact runtime identity, and the command that
regenerates the compiled C++ include. Raw JSONL samples, generated TSV,
analyzer reports, Arena reports, position-level timing logs, local paths, and
host names remain under the local measurement root outside the repository.

Production selection must fail closed on evaluator family, artifact ID, weights
checksum, search mode, and exact-handoff policy. A build-time rollback switch is
also required.
