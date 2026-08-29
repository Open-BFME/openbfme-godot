# Claude contributor note

Read and follow [AGENTS.md](AGENTS.md). It overrides every older agent or lane
instruction in Git history.

`PLAN.md` is the integration owner's component map, not a worker task list.
Workers must not claim or mark plan rows. Work begins only from one assigned
row in `orchestration/work-items.json`, in the sibling worktree and owned paths
recorded by that row. The integration owner alone updates `PLAN.md`, contracts,
the work ledger, selected packs, and completion claims.

Use Windows-native commands, keep retail bytes and raw evidence under ignored
`workspace/`, and hand off one focused commit with the evidence required by
`docs/VERIFICATION.md`.
