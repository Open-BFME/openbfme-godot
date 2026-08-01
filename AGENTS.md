# OpenBFME agent guardrails

Owner: Integration owner
Owns: Enforceable repository-wide agent constraints and authority boundaries.
Does not own: Product scope, current status, architecture, or task-specific acceptance.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: A repeated, approved repository-wide failure needs a durable rule, or a stale rule can be deleted.
Validation: Compare this file with `docs/AGENT_WORKFLOW.md` and validate the applicable task packet before work begins.

## Product target and active objective

The product target is the full **RotWK 2.01** game: all playable factions
(including Angmar and RotWK-only units), official skirmish/multiplayer maps,
Create-a-Hero, both campaigns (as shipped), War of the Ring, skirmish shell,
and multiplayer. BFME2 remains an optional comparison install only.

`DIRECTION.md` owns product scope, the **systems-first iterative** development
model, and the system ladder. Do not treat historical Men/Fords vertical-slice
freeze as the active strategy. Do not reject major-system work as out of scope
because it is not a single-map slice.

The active **systems-iteration objective** is named in
`docs/MILESTONE_CURRENT.md` (not a BFME2-only M2 freeze). That objective is the
current system(s) under iteration, not the end of the product.

`DIRECTION.md` owns product scope; `STATUS.md` owns current evidence. Do not
substitute obsolete proof-stage documents for either.

Content baseline for importer and packs is **RotWK** (see importer CLI default
`--game rotwk`). Prefer RotWK evidence for parity claims.

## Required workflow

Follow `docs/AGENT_WORKFLOW.md` for packets, locks, review, and integration.
Follow `docs/AGENT_REASONING_MANUAL.md` for how to orient, classify work, bound
scope, fail closed, verify, and stop/escalate.

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
- Historical `run_m2_acceptance.bat -IntegrationOwnerPublish` remains
  integration-owner-only tooling for the legacy Men/Fords oracle path; it is
  not the definition of product completion under the systems-first model.
- System iterations use the focused acceptance command named in the task packet
  and the objective in `docs/MILESTONE_CURRENT.md`.
- **Grok coding double-check:** when a Grok agent writes or modifies code, it
  must run **Sol medium** via Codex before claiming done — pin
  `model="gpt-5.6-sol"` and `model_reasoning_effort="medium"`. Prefer
  `codex.cmd review --uncommitted` (then optional `codex.cmd exec -s read-only`
  for deeper correctness). Fix or escalate Sol P0/P1 findings. Sol is not a
  substitute for focused runners or owner final gates. Full procedure:
  `docs/AGENT_REASONING_MANUAL.md` §10a. Do not skip silently if Codex is down;
  report blocked. Do not edit shared Codex config from implementation tasks.

## Private workspace

- Retail work: `.private/retail-work`
- Converted packs: `.private/content-packs`
- Job roots: `.private/scratch/jobs`
- Queue/locks/metrics: `.private/orchestration`

Never commit or export retail payloads. Public fixtures must be repository-authored.
