# Contributing

Read [AGENTS.md](AGENTS.md) before doing anything. OpenBFME targets exact
RotWK Patch 2.02 v9.7.7 parity; the governing scope and baseline are
[product scope](contracts/rotwk-202-v9.7.7-product-scope.json) and
[retail baseline](contracts/rotwk-202-v9.7.7-baseline.json).

## Start one item

The integration owner assigns one bounded row from
[orchestration/work-items.json](orchestration/work-items.json), including owned
paths, required evidence, and a focused check. Work in the short-lived sibling
worktree created for that item. Do not work on `main`, claim from the historical
`queue.md`, edit canonical state/contracts/selection, or broaden the task.

Use Windows-native commands. Keep retail material and all raw logs under the
ignored `workspace/` tree. Never commit retail bytes, captures, machine paths,
or raw command output.

## Build the evidence packet

Reproduce the gap first, implement the smallest fix, and prove only the levels
actually reached: **SOURCE -> CONVERT -> LOAD -> BEHAVIOR / VISUAL / AUDIO**.
Lower evidence never proves a higher level. Follow
[docs/VERIFICATION.md](docs/VERIFICATION.md) and report the exact revision,
baseline, profile, selected bundle, command, result, and private artifact
digest.

Stage only assigned paths and produce one focused commit. The integration owner
hands that commit to an independent verifier. The item is not complete until
the verifier accepts it and the integration owner merges it, runs the required
integration gate, updates canonical state, and retires the worktree.

Product direction is in [DIRECTION.md](DIRECTION.md); the system map is in
[PLAN.md](PLAN.md). Neither is a worker-owned task ledger.
