---
name: openbfme-retrospective
description: Review completed or failed OpenBFME task packets, gate results, regressions, and adversarial findings using a five-step engineering retrospective. Use after an integrated packet, failed gate, confirmed review defect, repeated user correction, material performance regression, or unexpectedly costly task to identify evidence-backed lessons and propose narrowly scoped guidance, skill, test, hook, architecture, or audit changes without modifying persistent workflow state.
---

# OpenBFME Retrospective

Produce a proposal-only retrospective from task-local evidence. Do not claim permanent learning or mutate repository guidance.

## Establish the evidence boundary

Read only the artifacts supplied or named for the completed packet:

- original task packet and requirement source;
- final diff;
- focused and integration results;
- performance measurements;
- adversarial-review findings;
- fixes applied after review.

State missing artifacts and reduce confidence accordingly. Do not reconstruct a favorable story from implementation commentary. Stop and request the missing oracle when a parity conclusion depends on retail evidence that is unavailable or contradictory.

Read [references/known-failures.md](references/known-failures.md) only when checking whether a failure signature may have occurred before. Treat that file as a diagnostic index, not proof; cite the underlying incident artifacts before calling a mistake repeated.

For the capability and skill-gap check only, read-only inspection of the available
skill catalog, the applicable skill entrypoints, and applicable `AGENTS.md` files is
allowed. Do not treat those materials as evidence that the packet outcome passed.

## Run the five-step review in order

1. **Question requirements.** Identify each requirement's source, whether it was necessary, and whether the supplied evidence supports the claimed outcome.
2. **Delete.** Identify unnecessary code, tests, process, documentation, abstraction, automation, or context. State what should not be repeated.
3. **Simplify.** Describe the smallest implementation and workflow that would have produced the same verified result.
4. **Accelerate.** Identify avoidable context reads, slow or duplicated gates, repeated investigation, shared-path conflicts, unclear task boundaries, and tool misuse.
5. **Automate last.** Propose automation only for a stable repeated process or a mechanically detectable high-severity invariant.

Separate root causes into code defect, task-specification defect, workflow defect, missing evidence, agent-context defect, and tooling defect. Use `none evidenced` rather than forcing a cause into every category.

## Perform the capability and skill-gap check

Check all of the following before proposing persistence:

- Determine whether an applicable `AGENTS.md` rule was ignored, missing, or unclear.
- Determine whether an existing skill already covers the workflow.
- Prefer updating an existing skill over creating another skill.
- Require evidence of the same mistake at least twice, except for severe determinism, legal, security, containment, or data-loss risk.
- Test whether the proposed rule would have prevented the failure without blocking a common valid task.
- Prefer changes that shrink context and narrow decisions.
- Prefer mechanical enforcement for mechanically detectable invariants.
- Identify stale guidance that the proposal can replace, shorten, or remove.

Route each confirmed lesson to exactly one primary surface:

| Lesson | Primary surface |
|---|---|
| One-off task fact or first ordinary incident | Next packet or private task report only |
| Repeated durable repository rule | Closest applicable `AGENTS.md` |
| Repeated multi-step workflow | Existing skill, or a new skill only when none fits |
| Mechanically detectable invariant | Focused test or hook |
| Architecture or parity fact | Canonical contract or architecture document |
| Current milestone state or volatile gate/identity result | `STATUS.md` only |
| Stable product target or scope-ladder change | `DIRECTION.md` only |
| Stable recurring audit proven manually | Scheduled task |
| External-system dependency | MCP workflow |
| User preference or cross-project history | Memory only with explicit user authorization |

Treat a hook or scheduled task as premature until the manual correction has succeeded repeatedly. Require five successful manual uses before proposing a scheduled audit. Give every proposal a removal condition.

## Preserve owner control

Do not edit files, memories, skills, hooks, configuration, queues, locks, contracts, or automations. Do not promote a lesson, create a `META` packet, or approve your own proposal. Do not claim that Codex has learned permanently or modified itself.

Return exact proposed text for integration-owner review. Persistent changes require a separate bounded task, its own path lock, and independent review.

## Return exactly these sections

1. Outcome and evidence.
2. Unnecessary work.
3. Root causes: code, specification, workflow, evidence, context, and tooling.
4. Confirmed lessons.
5. Repeated-incident evidence.
6. Existing capability or skill to reuse.
7. Proposed persistence surface.
8. Exact proposed text.
9. Guidance to remove or shorten.
10. Narrower prompt for the next similar task.

For sections without evidence, write `None evidenced.` Distinguish observed facts from inference and recommendation.
