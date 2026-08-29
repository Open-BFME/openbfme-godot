# <work-item ID> - implementation handoff

- Implementor: `<name>`
- Base commit: `<sha>`
- Commit: `<sha>`
- Target baseline: `rotwk-202-v9.7.7-en`
- Work-item revision/digest: `<identity>`

## Result

State `implemented; awaiting independent verification`, or state the blocker.
Do not self-accept the item.

## Changed paths

List every changed path and why it belongs to the assigned scope.

## Evidence

| Level | Result | Source/command | Sanitized artifact digest |
|---|---|---|---|
| SOURCE | `PASS/FAIL/SKIP` |  |  |
| CONVERT | `PASS/FAIL/SKIP` |  |  |
| LOAD | `PASS/FAIL/SKIP` |  |  |
| BEHAVIOR | `PASS/FAIL/SKIP` |  |  |
| VISUAL | `PASS/FAIL/SKIP` |  |  |
| AUDIO | `PASS/FAIL/SKIP` |  |  |

Record only evidence required by the item. `SKIP` is not success. Raw output,
captures, retail bytes, and machine paths stay below
`workspace\logs\<work-item-id>\`.

## Focused check

- Command: `<exact command>`
- Exit/status: `<PASS/FAIL/SKIP>`
- Acceptance marker: `<exact observed marker>`
- Unexpected diagnostics: `<none or exact names>`

## Decisions and unresolved work

List source-backed interpretation decisions, unsupported semantics, and scope
that remains open. The integration owner decides whether any follow-up becomes
a separate work item.
