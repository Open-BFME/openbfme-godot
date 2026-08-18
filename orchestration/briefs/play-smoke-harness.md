# Play-smoke harness — nightly headless "does a full match work" bot (Sol)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10). Claim
row Q22 in orchestration/queue.md (owner=sol-playsmoke). Standard lane rules
(explicit-path git, no sweeps, logs under workspace/logs/, sequential Godot).
No pack builds/selection changes.

## Why
There is no automated answer to "does a full skirmish actually run to a
result on every map with every faction?" The gates assert subsystem checks;
nothing runs matches. This is why the structure-armor fixture rot went
unnoticed for days and why AI/locomotion work has no end-to-end oracle.
Playtest lane 3 (docs/state/playtest-program.md) called for this and it was
never stood up.

## Deliverable
1. `game/tests/play_smoke_runner.gd` — headless SceneTree runner that, for a
   given (map, faction A, faction B, difficulty, seed, tick budget):
   boots the selected packs, starts a skirmish via the same path the menu uses
   (find `menu_skirmish_runner.gd` and retail_vertical_slice.gd's start path;
   reuse, don't fork), runs the sim to the tick budget or victory, and asserts
   LIVENESS, not just no-crash: (a) both sides issued ≥1 build and ≥1 train
   command (through the command log once AI Phase 2 lands; until then via the
   AI state), (b) ≥1 `combat.hit` event occurred, (c) at least one structure
   changed ownership or was destroyed OR the match ended, (d) tick rate never
   dropped below a floor (e.g. 10 Hz sim advanced ≥ N ticks per wall second),
   (e) no `SCRIPT ERROR` / `ERROR:` lines outside an allowlist file
   (`game/tests/play_smoke_allowlist.txt` — seeded with the known Q11 armor
   messages; the file is a ledger, each entry with a queue row reference).
   Emits `PLAY_SMOKE_RESULT map=<> a=<> b=<> ticks=<> passed=<> failed=<>` and
   on failure writes a repro bundle under workspace/logs/play-smoke/<stamp>/:
   the runner args, seed, last 200 sim events, state_hash at fail tick,
   diag_log tail. A watchdog (reuse game/tests/runner_watchdog.gd) kills a
   stalled match and marks it STALL (distinct from FAIL).
2. `tools/play-smoke.ps1` — matrix driver: reads the selected map list
   (whatever the skirmish menu enumerates — find the source) × the 7 factions
   (A vs B pairs: at minimum each faction vs Men, plus mirror), runs
   sequentially with per-match timeout, writes a matrix summary
   `workspace/logs/play-smoke/<stamp>/summary.md` (grid of PASS/FAIL/STALL +
   duration) and exits non-zero on any FAIL/STALL. Flags: `-Quick` (3 maps × 2
   pairs, for the gate), `-Full` (everything, nightly), `-Seed`, `-TickBudget`.
3. Wire `-Quick` into a gate that already runs regularly (propose which; do not
   add it to CI if it exceeds ~10 min — measure and report), and document the
   `-Full` nightly invocation in AGENTS.md "Run the right lane".

## Tests — failing-first
- A deliberately broken fixture (e.g. AI disabled) must FAIL the liveness
  assertions — prove the runner cannot report green while the AI stood still.
- A synthetic stall (sleep in a step) must be reported STALL with a repro bundle.
- The allowlist mechanism: an unlisted ERROR line fails; a listed one passes.

## Definition of Done
1. `-Quick` matrix on the current selection: summary.md pasted; every cell
   PASS or an honest FAIL/STALL with its repro bundle path (do NOT tune
   budgets to make reds green — record them and open queue rows).
2. `-Full` matrix run once (log it; expected hours) — pasted summary; each
   FAIL/STALL becomes a queue row with the bundle path.
3. Existing gates unchanged: retail_spellbook_runner 218/0,
   retail_member_combat_runner 115/0, lockstep determinism 5/0; state pin
   unchanged; hygiene PASS; pack addresses PASS; git status clean.
4. Report orchestration/reports/play-smoke-harness.md.

## HOTFIX v0.2.4.1 note (2026-08-18)

`game/tests/slice_start_roster_presentation_runner.gd` is the menu-path
skirmish / Fords roster presentation gate (the hole v0.2.4 walked through).
Wire it into `-Quick` when that list exists; it does not yet. Added to the
repo-root `tools/gate-m2-focused.ps1` instead.
