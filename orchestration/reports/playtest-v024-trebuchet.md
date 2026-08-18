# Playtest report — v0.2.4 alpha, ordinary-weapon projectiles + warhead splash (Q13)

**Date:** 2026-08-18 · **Lane:** playtest · **Commit under test:** `3238dee` (main, clean tree at start)
**Content:** `OPENBFME_CONTENT=<repo>\workspace\content-packs`, selection `4e3e7024…`, active Men pack
`rotwk-men-vslice/a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0`
(confirmed in every log by the `[ModLoader] content source=external active=…a0fde4ac…` line — no stale-pack fallback).
**Godot:** `C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe` (v4.7.stable.official.5b4e0cb0f)

The owner asked "can't you playtest this?". Short answer: yes — the feature works, in the sim
and on screen, on the real shipped Men pack. Numbers below, and four screenshots.

---

## Part A — headless sim playtest (the proof)

**Runner:** `game/tests/playtest_trebuchet_splash_runner.gd` (new; the only source file this lane wrote)
**Log:** `workspace/logs/playtest-v024/trebuchet-headless.txt`
**Command:**
```
OPENBFME_CONTENT=<repo>\workspace\content-packs
<godot> --headless --path game --script res://tests/playtest_trebuchet_splash_runner.gd
```
**Result: `PLAYTEST_TREBUCHET_RESULT passed=18 failed=0`, exit 0.** Re-run after the final edit; still 18/0.

### The compiled combat block actually shipped

Read out of the configured unit rule for `bfme2.object.gondor-trebuchet` (which the adapter builds
from the pack's `data/playable-units/gondortrebuchet.json`):

```
projectile_object_id = 'GondorTrebuchetRockProjectile'
projectile_speed     = 321.0 (source 321.0)
attack_range         = 500.0   minimum_attack_range = 300.0
damage_type          = 'siege'  radius_damage_affects = 'ENEMIES NEUTRALS ALLIES'
member_damage        = 600
component            = { "value": 600.0, "damage_type": "siege", "radius": 100.0,
                         "damage_taper_off": 50.0, "death_type": "EXPLODED",
                         "damage_fx_type": "BIG_ROCK" }
```

So the projectile fields ARE carried by the shipped pack. Retail oracle (weapon.ini:3110-3165,
`GONDOR_TREBUCHET_*` in gamedata.ini) matches line for line.

**Finding, not a defect:** this weapon has **one** damage nugget, not the inner-nugget +
outer-tapered-disc pair the brief assumed. There is no "inner full-damage disc": it is a single
radius-100 disc with `DamageTaperOff 50`, i.e. 100 % of 600 at the centre falling linearly to 50 %
at 100 units. The staging below was rebuilt around what the pack actually authors.

### The staged fight

Scale 1.0, so every number is a retail source unit. One Men trebuchet (team 0) at 460 units from an
enemy **farm** (team 1, 2000 hp, `ResourceArmor` SIEGE 150 %) at the origin. Three enemy battalions
standing at 8, 75 and 130 units from the farm. Order: attack the farm.

The three splash victims are enemy *trebuchets* on purpose: 1 member × 2000 hp makes the splash
number **readable**. An archer horde cannot show the taper at all — every splash amount here exceeds
a 150 hp archer, so near and far would both simply lose one man. That is itself worth knowing:
**in this sim, radius damage lands on one member of a battalion and does not spill over into the
next**, so against infantry a trebuchet rock kills exactly one man per battalion in the blast.

Two capture-only levers, both named in the runner: the attacker is pinned `indestructible` (three
armed enemy trebuchets counter-batter it to death by ~tick 200 otherwise — real behaviour, wrong
subject), and the three victims are marked `noncombatant` so nothing but the shot under test deals
damage. Neither touches the weapon, the flight, or the warhead.

### What the sim did, tick by tick

| Check | Observed |
|---|---|
| First `combat.projectile_launched` | **tick 15**, token 70001, `GondorTrebuchetRockProjectile` |
| Claimed impact tick | **30** |
| Observed `combat.projectile_impact` | **tick 30** — matches |
| Flight time | **15 ticks = 1.50 s**; `ceil(460 / 321 / 0.1)` = 15. Distance ÷ authored speed, exactly |
| Damage to the structure between launch and impact | **0** — the flight is real, not cosmetic |
| Structure at impact | 2000 → 1100, **delta 900** = 600 × 150 % SIEGE (`ResourceArmor`, armor.ini:2749) |
| Direct target double-hit by its own splash? | **No.** Delta is exactly 900; a double application would be 1800 |
| NEAR battalion, 8.00 units out | 2000 → 1424, **delta 576**; taper oracle `round(600 × (1 − 0.5 × 8/100))` = 576 |
| MID battalion, 75.00 units out | 2000 → 1625, **delta 375**; oracle `round(600 × (1 − 0.5 × 75/100))` = 375 |
| FAR battalion, 130.00 units out (> radius 100) | 2000 → 2000, **delta 0** |
| Run length | **620 ticks**, exit 0, no SCRIPT ERROR anywhere in the log |
| Shot cadence over the run | launches at ticks 15, 162, 305, 452, 599 — **5 launched, 5 impacted**, period 147 ticks = 14.7 s = 1200 ms pre-attack + 5400 ms firing duration + 8000 ms reload, exactly as authored |
| Target fate | farm destroyed on the third rock (2000 → 1100 → 200 → 0) |

The taper expectation is recomputed **inside the runner** from the pack's own numbers and the
victim's position at the impact tick — it is not read back out of the sim. The sim is never its own
oracle here.

**What this proves:** flight time is real and distance-derived; nothing lands early; the warhead
applies once to the direct target and once, tapered by distance, to everyone else inside the radius;
the edge of the disc is hard; armour is applied on top of the taper.

---

## Part B — visual capture (what a human would see)

**Mode:** same runner, `OPENBFME_PLAYTEST_CAPTURE=1`, **windowed** (non-headless) 1600×900, real
`res://scenes/retail_vertical_slice.tscn`, real map, real HUD.
**Log:** `workspace/logs/playtest-v024/trebuchet-capture.txt`
**Frames:**

- `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\playtest-v024\frames\01-before-launch.png` (sim tick 84)
- `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\playtest-v024\frames\02-mid-flight.png` (tick 169)
- `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\playtest-v024\frames\03-impact.png` (tick 175)
- `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\playtest-v024\frames\04-one-second-after.png` (tick 185)

Launch tick 163, impact tick 175 (12 ticks of flight at this map's 0.0265 transform scale, the
trebuchet having closed slightly before firing).

I looked at all four. What they show:

1. **01** — the enemy fortress, the Gondor trebuchet standing off in front of it, an enemy horde to
   the right. Nothing in the air.
2. **02** — a rock, plainly visible, high above the fortress at the top of its arc. This is the
   headline feature rendered: `combat.projectile_launched` spawns a real world object that flies for
   the authored duration.
3. **03** — the rock is gone and the fortress health bar has turned **red**. Impact.
4. **04** — one second later: no projectile, damage bar still red.

Two things I had to do to get a usable frame, both recorded in the runner's comments: stand the
trebuchet at 380 source units instead of 460 (inside its own 400-unit `ShroudClearingRange`, so the
thing it is shooting is actually deshrouded), and hold the attacker `noncombatant` during the first
~3 s so its authored `AutoAcquireEnemiesWhenIdle` did not spend the first shot while the terrain and
GLBs were still streaming in and the screen was black.

**Honest caveat:** the first ~2 s of a slice boot render black. Any capture taken before that is a
picture of nothing. Also: disabling fog of war mid-run makes the whole map render black rather than
revealed — worth knowing before anyone else tries it.

---

## Part C — a short bot match

**Mode:** same runner, `OPENBFME_PLAYTEST_BOTMATCH=1`, headless. Men vs Men, **both** seats driven
by the shipped AI controller at `hard`, on the booted slice's own gameplay rules and map
configuration (the `retail_ai_ladder_runner` construction), 2000 ticks.
**Log:** `workspace/logs/playtest-v024/botmatch-men-vs-men.txt`

- **Wall clock: 2000 ticks in 5.83 s = 343.1 ticks/s** (sim only; slice boot was a further ~12.5 s).
- Both sides built and fought: `construction.started 10`, `construction.completed 8`,
  `production.queued 2`, `production.complete 2`, `production.exit_complete 2`,
  `economy.payout 72`, `structure.destroyed 3`, `battalion.defeated 6`.
  Final: team 0 — 1 battalion, 6 structures, 132 resources, army value 500; team 1 — identical
  (mirrored map, deterministic AI). Winner: none at the cap.
- **Projectiles in real play:**
  `combat.projectile_launched` **570**,
  `combat.projectile_impact` **547**,
  `combat.projectile_cancelled` **23**,
  `combat.structure_projectile_launched` 0.
  570 = the same count as `combat.member_fire` / `combat.member_swing`, i.e. **every ranged shot in
  the match went through the projectile path**. The 23 cancellations are shots whose target died
  mid-flight — exactly the intended semantics, and visible proof the flight window is real.
- **No** `no compiled armor contract` lines at all.
- **SCRIPT ERROR seen, three times per boot** (also present in the AI-ladder run below, so it is a
  boot-path defect, not something this lane introduced):
  ```
  SCRIPT ERROR: Invalid access to property or key 'construction' on a base object of type 'Dictionary'.
     at: _validate_v1_lifecycle_contract (res://src/retail_slice/retail_structure.gd:721)
  ```
  `game/src/retail_slice/retail_structure.gd:721` does `var construction: Dictionary = facts["construction"]`
  behind `if not construction_omitted:` — some shipped structure has `construction_omitted == false`
  and no `construction` facts. Introduced by `34e6cfa fix(importer): preserve retail static construction`
  (2026-08-17), i.e. unrelated to Q13. **Recommend a queue row.**
- Recurring warnings (pre-existing, named loudly by the sim, not errors):
  `[RetailSliceSim] unit 'Wolf' / 'CaveTroll_Slaved' / 'NeutralWarg' has no authored damageType;
  its structure damage uses each kind's DEFAULT armor scalar`.

---

## Extra: existing AI-ladder gate, run and NOT attributed

I also ran the repo's own real-content bot-match gate to see whether the projectile change moved
free play. **`retail_ai_ladder_runner` on HEAD: `RETAIL_AI_LADDER_RESULT passed=13 failed=4`,
204 s** (`workspace/logs/playtest-v024/botmatch-ai-ladder.txt`). Failures **by name**:

- `ladder_medium_beats_easy` (A(easy@0,medium@1)→easy, B(medium@0,easy@1)→easy, high_wins=0 low_wins=2)
- `ladder_hard_beats_medium` (both mirrors → medium, high_wins=0 low_wins=2)
- `footprint_medium_exceeds_easy` (tick 2400: medium=3150, easy=3222)
- `footprint_hard_exceeds_medium` (tick 2400: hard=2878, medium=3150)

Everything else passed, including `ladder_match_twin_run_deterministic`, `ffa_ai{3,4,5}_produced_and_attacked`,
`ffa_runs_to_completion` and `ffa_twin_run_deterministic` — so the failures are deterministic, not flaky.

**I could not attribute these.** I tried: a detached `git worktree` at `9c19da5` (the commit
immediately before `0e9ee3d feat(sim): add authoritative ordinary weapon projectiles`) under
`%TEMP%`, same content packs. It aborted before the ladder ran — a fresh worktree has no
`global_script_class_cache`, so `retail_slice_sim.gd` fails to parse
(`Could not find type "SageScriptExecutor" / "SageScriptEnv" / "RetailSliceScriptWorld"`) and the
slice never instantiates. Building that cache needs a full `--import` pass I did not spend. Worktree
removed.

**Partial attribution, found late.** `orchestration/reports/2026-08-15-orphan-runners-report.md:119`
already lists `retail_ai_ladder_runner` among six runners in bucket **(c) Stale Expectations (No
Longer Applicable)**. That audit is dated **2026-08-15**; the projectile commits (`0e9ee3d`,
`d03e486`, `f000569`) all landed **2026-08-17**. So this gate was already failing *before* Q13, which
is real evidence the four failures are **pre-existing, not a projectile regression**.

It is not proof, and I am not treating it as one: that audit explicitly says its buckets are
"evidence-signature triage", it captured no exit codes, and it names no individual checks — so it
cannot be judged failure-by-name against, which is the only way aggregate counts stop hiding
regressions. **There is still no named baseline for this gate in `docs/state/` or
`orchestration/queue.md`; one should be pinned (13/4 with the four names above is a candidate) or
the gate bisected.**

---

## What I could prove / observe / could not test

**Proved (Part A, external oracles, 18/18):** the shipped Men pack carries the projectile contract;
flight time equals distance ÷ authored speed; no damage lands before impact; splash applies to the
direct target once and to bystanders with the authored linear taper; the radius edge is hard;
armour is applied after the taper; 620 ticks with no script error; retail shot cadence reproduced.

**Observed (Part B):** the rock is a real, visible world object arcing to its target, and the target
takes visible damage on the impact tick. Four PNGs, listed above, all inspected.

**Observed (Part C):** in a real 2000-tick Men-vs-Men bot match, 570 launches / 547 impacts /
23 in-flight cancellations, 343 ticks/s, both sides building and training.

**Could NOT test / did not test:**
- **Splash on infantry as a player would feel it.** The sim's radius damage picks one member per
  battalion; overkill does not spread. Whether that matches retail (which kills several men in a
  blast) is an open question I did not resolve — **this is the single most likely real gap in Q13**
  and deserves its own lane against the retail oracle.
- The upgraded weapon (`Upgrade_GondorFireStones` → `GondorTrebuchetRockFlaming`, two nuggets:
  FLAME 500 r30 taper 0 and SIEGE 900 r50 taper 0) — never fired; the multi-nugget path is untested here.
- `radiusDamageAffects "ENEMIES NEUTRALS ALLIES"` — friendly fire and neutral splash were not staged.
- Structure-mounted trebuchets (`GondorCastleWallTrebuchet`, `MenTrebuchetExpansion`) — the
  `combat.structure_projectile_*` path fired 0 times in the bot match and was not staged.
- Any faction other than Men.
- Whether the four AI-ladder failures are regressions (see above).
- I did not claim a row in `orchestration/queue.md`: this lane is read-only outside its three
  declared paths.

## Files this lane wrote

- `C:\Users\Jonathan\Desktop\open-bfme\game\tests\playtest_trebuchet_splash_runner.gd` (new)
- `C:\Users\Jonathan\Desktop\open-bfme\orchestration\reports\playtest-v024-trebuchet.md` (this file)
- `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\playtest-v024\` (logs + `frames\*.png`)

No pack, selection, pin, contract or sim file was touched.
