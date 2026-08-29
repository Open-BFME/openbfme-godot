# Orchestration

This directory coordinates bounded contributions to the exact RotWK Patch
2.02 v9.7.7 Godot port. [AGENTS.md](../AGENTS.md) is the governing contract.

## Authority

| Path | Role |
|---|---|
| [work-items.json](work-items.json) | Only live work ledger; integration-owner write access only. |
| `briefs/` | Optional sanitized implementation context linked by a work item. |
| `reports/` | Optional sanitized handoff and independent-verification summaries. |
| Git history for `queue.md` | Historical ledger; never claim work or infer current status from it. |

Scope comes from the
[product contract](../contracts/rotwk-202-v9.7.7-product-scope.json) and
[baseline contract](../contracts/rotwk-202-v9.7.7-baseline.json). Product
direction is [DIRECTION.md](../DIRECTION.md), the system map is
[PLAN.md](../PLAN.md), and evidence acceptance is defined by
[docs/VERIFICATION.md](../docs/VERIFICATION.md).

## Lane lifecycle

1. **Assign.** The integration owner chooses one bounded work item and records
   its owner, owned paths, source requirement, evidence levels, focused check,
   and dependencies in `work-items.json`.
2. **Isolate.** The owner creates one short-lived branch and sibling worktree
   such as `..\open-bfme-lanes\<work-id>`. One item, lane, worker, and focused
   commit travel together.
3. **Reproduce.** The worker proves the named gap against exact Patch 2.02
   v9.7.7 evidence before changing implementation.
4. **Implement.** The worker changes only owned paths, runs only the focused
   Windows-native checks needed for the item, and keeps retail bytes and logs
   below ignored `workspace\` paths.
5. **Handoff.** The worker supplies one focused commit and a sanitized packet
   naming SOURCE, CONVERT, LOAD, and whichever of BEHAVIOR, VISUAL, and AUDIO
   the acceptance contract requires. Missing or lower-level evidence stays
   explicitly unproven.
6. **Verify.** A different agent, in a clean context, reviews the original
   evidence and diff and reruns the declared check against the same identities.
   Implementor self-verification cannot accept an item.
7. **Integrate.** The integration owner alone merges, runs affected/final
   gates, updates canonical state/contracts/selection, records the verdict in
   `work-items.json`, and removes the branch and worktree.

Tracked briefs and reports contain portable facts and digests only. Raw output,
captures, retail-derived material, and machine-absolute paths remain private
under `workspace\logs\<work-id>\` or the work item's private oracle directory.
A red check, stale identity, fallback, or disputed oracle keeps the item open.
