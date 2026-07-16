# OpenBFME agent guardrails

## End goal (active)

**This week:** playable Men-versus-Men Fords of Isen II slice with **1:1 graphical/audiovisual fidelity** for that roster/map, using converted retail packs under `.private` only. See `DIRECTION.md`.

Longer horizon: extract and convert the complete useful BFME2 1.06 retail asset
closure into `.private`, then expand the same pipeline to full skirmish (all
factions and multiplayer maps). Public release is a later code-only scrub; it
does not block private conversion. Do not skip the Fords Men visual+play gate.

## Forbidden unless the integration owner explicitly unblocks

- New or expanded synthetic proof stages 1–10 product work
- Claiming vertical slice complete without `DIRECTION.md` checklist
- Silent generic/procedural art in private parity mode when retail conversion is required
- Writing retail payloads outside `.private`
- Open-ended refactors, architecture debates, or “polish” that does not move the scoreboard
- Editing shared Codex user config from workers

## Orchestration

- One integration owner keeps the plan, path locks, and final gates.
- Delegate only bounded tasks with:
  - allowed paths
  - forbidden paths
  - expected output
  - acceptance command
- Concurrent workers must use non-overlapping path locks.
- Do not run MCP smoke tests against pinned importer tool trees during an importer build. Use `.private\scratch` copies.
- Preserve user changes; stop on overlapping edits.

## Sources of truth

MCP is optional observation only. It cannot bypass CLI, provenance, containment, or retail gates.

Before final integration handoff of retail-slice work, the integration owner
runs:

```bat
run_retail_pipeline_tests.bat
```

Workers must not run this broad gate for bounded oracle/converter handoffs.
The current script still targets the handwritten base profile and can publish
the wrong selection after its long importer suite. Workers run only their
assigned focused acceptance commands; the integration owner runs the broad
gate after its profile target/assertions are updated and the strict completion
pack is ready.

Use the smallest focused test first. Never weaken an assertion merely to pass.

## Private retail workspace

Retail and converted retail live only under:

- `.private\retail-work`
- `.private\content-packs`

`.private` stays gitignored. Never commit, log, or export retail payloads. Public/legal-safe fixtures remain repository-authored only.
