Owner: Integration owner
Owns: Work queue, task packets, locks, agent roles, review/fix/integration flow, stop rules, and workflow metrics.
Does not own: Product scope, architecture, milestone requirements, or permission to publish private/canonical content.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: An approved META packet changes an orchestration rule, schema, metric, or role boundary.
Validation: Validate each ready packet against this contract, verify non-overlapping locks, and audit completed packet evidence before owner integration.

# Agent workflow

Reasoning doctrine (orientation order, claim standards, fail-closed defaults,
anti-patterns): [AGENT_REASONING_MANUAL.md](AGENT_REASONING_MANUAL.md).

## Authority

The integration owner alone may promote work to `ready`, assign locks or worktrees, expand scope, accept findings, publish packs or selection, integrate changes, mark packets done, or approve workflow/skill changes.

Workers run the smallest assigned focused check. They do not run final gates, mutate shared publication, or use stash, reset, rebase, pull, merge, or destructive checkout operations.

## Queue

Owner-controlled state lives under `.private/orchestration/` as `queue.json`, `locks.json`, and `metrics.json`.

```text
draft -> ready -> claimed -> implementing -> review -> fixing -> integrate -> done
any state -> blocked | rejected | superseded
```

Priorities are `P0` containment/destruction/canonical corruption, `P1` active-milestone or severe-oracle blocker, `P2` visible scoreboard progress, `P3` cleanup that directly unblocks higher priority, and `META` repeated workflow failure.

## Ready packet contract

Each packet lives at `.private/scratch/jobs/<task-id>/task.yaml` and provides:

```yaml
id: bounded-task-id
state: ready
objective: one observable outcome
scoreboard_effect: exact milestone row or blocker advanced
source_of_truth:
  - named retail evidence, contract, file, or oracle ID
oracle: expected externally observable behavior
base_commit: full commit identity
lane: importer | runtime | simulation | networking | oracle | tooling | docs
risk: normal | high
allowed_paths: [exact files or narrow directories]
forbidden_paths: [canonical publication and unrelated hotspots]
private_job_root: .private/scratch/jobs/bounded-task-id
dependencies: []
non_goals: []
acceptance: [smallest focused command]
expected_marker: exact useful result
reviewers: 1
review_focus: [parity, regression]
stop_if: [overlapping lock, ambiguous oracle, new subsystem, repeated failure]
deliverables: [bounded diff or commit, focused result, unresolved risks]
workflow_version: loop-v1
```

A packet is ready only when it has one observable change, a named source, an oracle or acceptance result, narrow paths, explicit non-goals, and one focused check. Objectives containing open-ended “improve,” “polish,” “finish,” or broad refactoring language remain draft. Missing oracle evidence produces a read-only discovery packet first.

## Locks and concurrency

- The main worktree belongs to the integration owner.
- Start with one implementer; allow no more than two implementation worktrees only after ten successful packets.
- Path-prefix overlap is a conflict. Stop rather than edit across an active or user-owned change.
- Large importer and generated-runtime files are single-writer.
- Canonical packs, selection, generated completion profiles, pinned tools, shared Python environment, and oracle approval are owner-only.
- Each worker uses its assigned private job root and commits only allowed paths.
- Reviewers are read-only and inspect the bounded diff, task contract, and evidence without relying on implementer reasoning.

## Review and integration

Default to one adversarial reviewer. Require two for importer/runtime boundaries, simulation/save/replay/networking, containment/publication, performance/lifecycle, retail audiovisual parity, and destructive cleanup.

Every finding includes severity, evidence, reproduction, and the minimum correction:

- `P0`: containment, destructive behavior, security, or canonical corruption.
- `P1`: the packet objective or parity contract is wrong.
- `P2`: real non-blocking issue; queue separately.
- `P3`: style or preference; normally discard.

P0/P1 findings must close within two review/fix rounds. The owner integrates only after the affected focused checks pass together. Integration is a checkpoint, not release proof.

## Stop and escalate

Stop immediately for possible retail escape, unauthorized canonical publication, lock overlap, changed base identity, weakened tests, stub/silent fallback/invented parity, cross-subsystem scope growth, missing or contradictory oracle evidence, a repeated failure after one correction, two failed P0/P1 fix rounds, diagnosis requiring only a broad gate, or a seemingly necessary destructive Git operation.

Escalate with:

```text
Blocker:
Evidence:
Attempted:
Options:
Recommendation:
Authority or lock requested:
```

## Production loops

Discovery turns an unresolved oracle into evidence and a bounded packet. Census classifies a source/root/reference. Conversion produces deterministic artifacts and provenance. Parity closes or classifies an oracle difference. Implementation produces a focused passing diff. Review produces reproducible findings or an explicit no-defect result. Fix closes accepted P0/P1 findings. Integration passes the union of affected checks. Porting proves language-independent behavior before deleting the old authority. Networking proves identical command/replay outcomes and recovery. Performance retains only measured improvements. Retrospective proposes loop repair without self-modification.

## Metrics

Track milestone and coverage rows closed, oracle approvals and severe findings, first-pass acceptance, fix rounds, actionable/escaped defects, blocked age, gate time and wasted reruns, conversion cold/warm/resume time, deterministic divergences, containment/lock incidents, and repeated workflow signatures.

Initial targets are zero containment/lock/test-weakening incidents, complete assigned packets, two reviewers for every high-risk packet, at least 90% closure within two review/fix rounds, zero final gates run by workers, and a named milestone row or blocker for every packet. Collect ten packets before adopting cycle-time targets. Do not reward lines, commits, or assertion totals.

---

<!-- merged from docs/AGENTS.md -->

## Documentation agent rules

Only the canonical documents linked from `README.md` may claim current status,
architecture, scope, verification, release, or workflow authority.

- Put volatile hashes, counts, benchmarks, and blockers only in `STATUS.md` or
  generated private reports.
- Do not create sprint diaries, duplicate plans, evidence journals, or
  `docs/archive`; Git history is the archive.
- Human documents own policy, decisions, reproduction commands, and unresolved
  questions. Generated manifests/tests own retail membership and exact counts.
- When consolidating a narrow document, migrate each unique conclusion into a
  canonical contract, test, or generated evidence row before deleting it.
- Keep `Owner`, `Owns`, `Does not own`, `Last verified commit`, `Update trigger`,
  and `Validation` headers current.

---

<!-- merged from docs/RETROSPECTIVE.md -->

Owner: Integration owner
Owns: Confirmed workflow lessons, incident thresholds, approved persistence routing, and removal conditions for promoted guidance.
Does not own: Raw task retrospectives, autonomous skill/instruction changes, product status, architecture, or the work queue.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: The owner approves a lesson promotion or removes guidance after its stated condition is met.
Validation: Trace every retained lesson to concrete task/gate/review evidence and verify that its rule still prevents the failure without blocking common valid work.

## Retrospective policy and confirmed lessons

### Confirmed lessons

| Lesson | Evidence-backed consequence | Persistence surface | Remove or revise when |
|---|---|---|---|
| Retail source parsing and asset conversion do not prove parity. | Require original-game/Godot behavioral and audiovisual oracle evidence. | Parity contract and verification gates | A stronger reproducible oracle replaces the current evidence model. |
| Private parity cannot mix selected retail content with synthetic fallback definitions. | Load only the selected strict pack and fail closed on missing requirements. | Runtime contract plus containment/selection tests | Never remove; revise only if an equally strict, auditable profile contract replaces it. |
| Competing gameplay authorities make behavior and tests ambiguous. | Establish one authoritative simulation and delete the migrated authority immediately after equivalence proof. | Architecture and simulator-port packet | The architecture adopts and proves a different single-authority boundary. |
| Translation combined with redesign creates avoidable behavioral drift. | Mechanically port current behavior, compare language-independent traces, then change cadence/design separately. | Simulation-port workflow | The port is complete and the rule no longer applies to active work. |
| Broad worker gates can mutate shared/private selection and waste diagnosis time. | Workers run only focused acceptance; the owner runs corrected integration/final gates. | Agent workflow and gate ownership | The broad gate becomes proven read-only, selection-safe, fast, and owner explicitly changes policy. |
| Shared private paths and overlapping edits cause nondeterministic agent interference. | Assign unique job roots and non-overlapping path-prefix locks; stop on overlap. | Agent workflow | A transactional orchestration system mechanically isolates all writes. |
| Volatile counts and gate results copied across plans become contradictory. | Keep changing state in `STATUS.md` or generated reports and one authority per subject. | Documentation control plane | A generated documentation system enforces one-source projections. |
| Snapshot-heavy primary networking conflicts with the chosen deterministic RTS model. | Synchronize accepted commands; reserve checkpoints for recovery, reconnect, and observation. | Architecture and simulation protocol | Measured evidence requires and proves a different protocol. |
| Simulation cadence and immediate user feedback are separate concerns. | Use a 30 Hz authoritative target with same-frame local acknowledgement and independent rendering. | Architecture and simulation protocol | New measured oracle/latency evidence justifies another cadence. |

### Retrospective output

After an integrated packet, failed gate, real adversarial defect, repeated correction, or material performance regression, review only the task contract/source, final diff, focused and integration results, performance evidence, adversarial findings, and corrections.

The retrospective questions requirements, identifies unnecessary work, finds the smallest verified solution, identifies wasted context or unclear boundaries, and recommends automation only for a stable repeated process. It separates code, specification, workflow, evidence, context, and tooling causes. It returns proposals and exact text; it does not edit files, memory, skills, hooks, configuration, automation, or the queue.

### Promotion rules

- One ordinary incident constrains the next similar packet only.
- The same evidenced mistake twice may become an `AGENTS.md` rule.
- A subtree-specific mistake belongs in the closest nested `AGENTS.md`.
- A repeated multi-step workflow may update an existing skill; create a new skill only when no existing skill fits.
- A mechanically detectable invariant belongs in a test or hook rather than prose.
- An architecture or parity fact belongs in its canonical contract.
- A scheduled audit requires five successful manual uses of the workflow.
- Memory changes require explicit user authorization.
- Severe deterministic, containment, security, legal, or data-loss risk may justify promotion after one incident.
- Every workflow promotion is a separate `META` packet with an owner, evidence, independent review, exact text, and a removal condition.

A proposal is accepted only when its root cause is evidenced, its wording is actionable, a reviewer cannot show a common valid case it blocks, it uses the narrowest correct persistence surface, and it replaces or shortens stale guidance where possible.

### Removal rules

Review promoted guidance when its triggering subsystem is deleted, its enforcement becomes mechanical, its workflow is superseded, or it has not applied across the next relevant milestone. Remove duplicate rules, obsolete task facts, and prose already guaranteed by a test/hook. Keep an architectural invariant only while the corresponding decision remains active.

Raw retrospectives remain in private task reports. This document contains curated, owner-approved lessons only and never claims that an agent learned or modified itself permanently.
