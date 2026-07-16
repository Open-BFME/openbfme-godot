Owner: Integration owner
Owns: Work queue, task packets, locks, agent roles, review/fix/integration flow, stop rules, and workflow metrics.
Does not own: Product scope, architecture, milestone requirements, or permission to publish private/canonical content.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: An approved META packet changes an orchestration rule, schema, metric, or role boundary.
Validation: Validate each ready packet against this contract, verify non-overlapping locks, and audit completed packet evidence before owner integration.

# Agent workflow

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
