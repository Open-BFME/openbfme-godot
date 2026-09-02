# Manual queues

One JSON file per unit under `<queue>/<unit-id>.json`. Adding a unit is
adding a file. Finishing one sets `"status": "done"` (`work.py done`).

```json
{
  "title": "one line, imperative",
  "rank": 10,
  "detail": "what, where, and the reference to use",
  "oracle": "the exact command that proves it",
  "status": "open"
}
```

Lower `rank` is served first. Derived queues (`core-modules`, `red`,
`maps`, `assets`, `screens`, `bugs`) have no directory here; they compute
their units from the corpus and the tree.
