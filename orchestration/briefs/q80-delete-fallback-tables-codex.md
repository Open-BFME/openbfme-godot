# Q80 — delete the invented fallback rule tables (missing pack data fails loudly)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10 binding;
rule 7: NO find-replace sweeps). Claim row Q80 in orchestration/queue.md
(owner=codex-q80). Exclusive tree access; no branches/worktrees; git add by
explicit path; BANNED git add -A / reset / restore / clean / stash / amend.
Long output → workspace/logs/. Godot exe per docs (owner machine:
C:\Users\Jonathan\Downloads\godot47\). Do NOT rebuild/publish/select any pack.

## Why (owner-ratified 2026-08-25, audit-verified)
`game/src/retail_slice/retail_slice_sim.gd:2472-2513` loads faction manifests
with `manifest.get(key, HARDCODED_CONST)`: when a pack manifest lacks a field,
the sim silently substitutes hand-typed constants (`UNIT_PRODUCTION_RULES`
:181, `STRUCTURE_BUILD_RULES` :89, `STRUCTURE_MAX_HEALTH`, `UNIT_DAMAGE_TYPES`,
`DEFAULT_STRUCTURE_ARMOR`, `DEFAULT_SPAWN_ROSTER`, plus `AI_PRODUCTION_PLAN`
:2473 and `STRUCTURE_KINDS` :2474). The least authoritative copy of the rules
wins, quietly. This is the mechanism behind the project's recorded
"stale/incomplete pack produced confidently wrong numbers that passed review"
incident class. Owner ruling: invented data must never shadow cooked data;
a missing field is a refusal, not a default.

## Deliverable
1. In `configure(...)` (the :2440-2520 region): for every manifest key that
   currently falls back to a hardcoded table, absence → set
   `configuration_error = "Faction manifest is missing required field '<key>'
   (pack must carry it; invented defaults were removed)"` and return false.
   No behavior change when the field IS present.
2. Delete the now-dead constant tables from the sim file. EXCEPTION: if a
   constant is still read by another live code path (grep each name across
   game/), do NOT delete it there — instead list every surviving reader in
   your report as named residue (candidate follow-up rows). Do not chase them
   in this lane.
3. Legacy fixtures/runners that configure the sim WITHOUT a full manifest will
   now refuse. Triage each: (a) if the fixture represents a shipped pack,
   thread the real field through the fixture; (b) if it is a synthetic unit
   test that never claimed retail parity, give it an explicit minimal manifest
   (constants moved INTO the test, clearly labeled synthetic). Never re-add a
   silent default in the sim.
4. `DEFAULT_STRUCTURE_ARMOR` is load-bearing in the Q11/Q15 story (frozen
   fixture has compiled armor only for `fortress`). If your change flips those
   gates' color, record before/after in the report and cite Q11 — do not "fix"
   Q11 here.

## Tests — failing-first
- New/extended runner (or extend an existing config-refusal test): a manifest
  missing `unit_production_rules` (and separately `structure_build_rules`) is
  REFUSED with the named error; the same manifest with the field present
  configures identically to before (assert a sampled rule value comes from the
  manifest, not a constant).
- Prove no live fallback was load-bearing on shipped content: boot the retail
  slice on the SELECTED packs (both fords + one castle map) — if any refusal
  fires, that is a REAL finding (a shipped pack was riding an invented
  default); name the field + pack digest in the report and stop for owner
  review rather than papering over it.

## Definition of Done (verbatim outputs in report)
1. New tests green (failing-first shown). Slice boot on selected packs green
   (or the named-finding stop above). `retail_state_pin_runner`: hash
   UNCHANGED (values already came from packs) — if it moves, that is evidence
   a fallback was live in shipped content; treat as the named-finding stop.
2. Focused gates that touch sim config green at their current baselines;
   pathing/projectile pins untouched.
3. git status clean; commits `fix(sim):` / `test(sim):` explicit paths.
4. Report orchestration/reports/q80-fallback-tables.md: fields converted,
   constants deleted vs surviving readers (named residue), fixture triage
   table, pin evidence. Update queue row Q80; unblock Q87 in its row if DoD 1
   passed clean.
