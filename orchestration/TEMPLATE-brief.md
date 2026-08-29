# <work-item ID> - <one bounded outcome>

Read [AGENTS.md](../AGENTS.md) first. This brief expands one item already
assigned by the integration owner in `work-items.json`; it never claims or
changes ledger state.

## Binding identity

- Base commit: `<sha>`
- Sibling worktree: `..\open-bfme-lanes\<work-item-id>`
- Branch: `<branch>`
- Target baseline: `rotwk-202-v9.7.7-en`
- Owned paths: `<exact paths from the work item>`
- Forbidden paths: `contracts/**`, `PLAN.md`, `work-items.json`, selections,
  published packs, and every path not assigned above

## Gap and source

Describe the player-visible gap and cite the exact effective 2.02 source,
provenance row, or original-game observation. Name unresolved interpretation
questions; do not invent an answer.

## Deliverable

State the smallest implementation outcome, its shipping consumer, and what is
explicitly outside this lane.

## Focused verification

- Command: `<Windows-native command copied from the work item>`
- Timeout: `<seconds>`
- Acceptance marker: `<exact marker>`
- Forbidden diagnostics: `<exact classes>`
- Required evidence levels: `<SOURCE/CONVERT/LOAD/BEHAVIOR/VISUAL/AUDIO>`
- Private artifacts: `workspace\logs\<work-item-id>\`

Run no broader suite unless the integration owner amends the work item.

## Handoff

Stage explicit assigned paths and make one focused commit. Supply a sanitized
report from `TEMPLATE-report.md`; do not update the ledger, publish/select a
pack, or call the item complete.
