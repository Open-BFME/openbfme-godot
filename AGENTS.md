# OpenBFME agent guardrails

Owner: Integration owner
Owns: Enforceable repository-wide agent constraints and authority boundaries.
Does not own: Product scope, current status, architecture, or task-specific acceptance.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: A repeated, approved repository-wide failure needs a durable rule, or a stale rule can be deleted.
Validation: Compare this file with `docs/AGENT_WORKFLOW.md` and validate the applicable task packet before work begins.

## Active objective

Close the remaining cross-faction skirmish parity gaps against RotWK 2.01 in
`docs/MILESTONE_CURRENT.md`. `DIRECTION.md` owns product scope; `STATUS.md` owns
current evidence. Do not substitute obsolete proof-stage documents.

## Required workflow

Follow `docs/AGENT_WORKFLOW.md`.

- One integration owner controls queue state, path locks, publication, final
  gates, and persistent workflow changes.
- Every implementation task must name its source/oracle, exact allowed and
  forbidden paths, non-goals, base commit, smallest acceptance command, and
  reviewer count.
- Use a unique `.private/scratch/jobs/<task-id>` root.
- Begin with one implementer. Do not exceed two implementation worktrees.
- Preserve user changes. Stop on overlapping paths or changed base identity.
- Workers do not publish canonical packs or run broad/final gates.
- Reviewers are read-only and do not expand scope.

## Forbidden without integration-owner authority

- Writing retail payloads outside `.private`.
- Editing `.private/content-packs/selection.json` or canonical build/profile
  state from a worker task.
- New or expanded synthetic Stage 1–10 product work.
- Silent generic/procedural fallback in private parity mode.
- Weakening or deleting an assertion to make a gate pass.
- Stubs, invented parity behavior, destructive Git commands, open-ended
  refactors, or unmeasured optimization.
- Editing shared Codex user configuration, memories, skills, hooks, queue, or
  workflow instructions from an implementation or retrospective task.

## Verification

- Run the smallest focused check first.
- Any warning, error, leak, orphan, escaped retail path, or unbounded fallback
  on a required path is a failure.
- `run_m2_acceptance.bat -IntegrationOwnerPublish` is the only final M2 entry
  point and is run only by the integration owner after identity-bound human
  oracle approval.

## Private workspace

- Retail work: `.private/retail-work`
- Converted packs: `.private/content-packs`
- Job roots: `.private/scratch/jobs`
- Queue/locks/metrics: `.private/orchestration`

Never commit or export retail payloads. Public fixtures must be repository-authored.
