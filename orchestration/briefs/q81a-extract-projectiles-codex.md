# Q81a — strangler-fig extraction #1: projectiles & splash out of the sim god file

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10; rule 7:
NO sweeps). Claim row Q81 in orchestration/queue.md (owner=your lane name,
note "extraction #1: projectiles"). Exclusive access to
game/src/retail_slice/retail_slice_sim.gd — confirm no other lane is IN FLIGHT
on that file in the queue before starting. Explicit-path git; logs →
workspace/logs/q81a-lane/. Godot:
C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe
--headless --path game --script res://tests/<runner>.gd. No pack/selection
changes; no importer changes.

## Why (owner-ratified 2026-08-25)
retail_slice_sim.gd is 33,716 lines holding the entire game. Q81 splits it one
subsystem per lane with zero behavior change, proven by the determinism pins.
Projectiles go first BECAUSE they are the only subsystem with a dedicated pin
(retail_projectile_pin, expected hash e6e053d4… per queue Q63) on top of the
state pin — double instrumentation for the first extraction.

## The rule of the lane
REFACTOR ONLY. Zero behavior change, zero tuning, zero cleanup-while-you're-
in-there. If you see a bug in the projectile code, record it as a queue row —
do not fix it here. Every pin must be BYTE-IDENTICAL after the move; there is
no acceptable drift in a refactor. If any pin moves, revert and diagnose your
extraction — never re-mint.

## Deliverable
1. New file game/src/retail_slice/sim/retail_sim_projectiles.gd (class_name
   RetailSimProjectiles or house naming — check existing class_name
   conventions in game/src/retail_slice/). Move the projectile/splash
   subsystem: _step_projectiles (~:22501), _apply_radius_damage (~:22617),
   projectile spawn/flight/bezier carrier helpers, and their private state
   (the projectile list + any counters). Identify the full closure by
   grepping call/state references — list every moved function + variable in
   the report.
2. The extracted class holds a reference to the sim and calls back through
   the sim's EXISTING public/internal methods for everything outside its own
   state (damage application entry points, entity queries, RNG). Do NOT
   duplicate logic, do NOT widen visibility more than needed, do NOT add an
   abstraction layer — this is a cut-and-delegate move.
3. retail_slice_sim.gd keeps one-line delegating methods with the original
   signatures so every external caller and test is untouched. The sim
   constructs the subsystem in setup(); tick order is IDENTICAL (call site of
   _step_projectiles does not move relative to other systems).
4. Determinism: the subsystem's state must serialize into the authoritative
   state EXACTLY as before (same keys, same order). If projectile state is
   part of save/restore, the restore path fills the subsystem — prove
   save→restore→hash equality if a runner for that exists.
5. If Q82's module framework has merged by the time you start, match its
   services-interface conventions where they fit naturally; if it has not,
   do NOT invent one — plain sim back-reference.

## Tests — proof harness (all must be byte-exact / baseline-exact)
- retail_state_pin_runner → hash=2d13b881c59bf5b3707f630878a4308b8cbe947d525e302028da1f2d9433fdc1 OK
  (or the then-current pinned value if a prior accepted lane consciously
  re-minted — read the runner's authored expectation, do not assume)
- retail projectile pin runner → its authored expected hash unchanged, OK
- lockstep two-peer smoke → baseline green
- castle_map_live_boot_runner → passed>=7 failed=0
- retail slice acceptance name set → 87/87 identical (or current baseline per
  gate-retail; zero name diff)
- The Q13/Q16 projectile/splash focused runners → unchanged pass counts
Save every log under workspace/logs/q81a-lane/.

## Definition of Done (verbatim outputs in report)
1. All proof-harness runs green at baseline, logs saved; line count of
   retail_slice_sim.gd before/after stated (expect a multi-hundred-line drop).
2. Report orchestration/reports/q81a-extract-projectiles.md: moved-symbol
   inventory, delegation table (old signature → new home), pin evidence,
   any bugs SEEN but not fixed (as proposed queue rows).
3. Queue row Q81 updated (extraction #1 done, next candidate named:
   economy/income). git clean; commits refactor(sim): explicit paths.
