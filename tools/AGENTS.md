# Tooling and gate agent rules

This subtree contains integration-sensitive gates, publication tools, oracle
helpers, and wrappers.

- `run_m2_acceptance.bat -IntegrationOwnerPublish` is the only final M2 entry
  point. Only the integration owner runs or changes final publication behavior.
- Workers may change a focused gate only when the packet names its semantic
  contract and smallest source test.
- Do not duplicate volatile profile hashes, pack hashes, runner counts, or test
  totals across scripts and docs. Read one machine-readable contract.
- Never weaken a diagnostic pattern, containment assertion, provenance check,
  oracle identity, soak threshold, or negative fixture merely to pass.
- Private-heavy canonical builds and selected-pack mutation are serialized and
  integration-owner-only.
