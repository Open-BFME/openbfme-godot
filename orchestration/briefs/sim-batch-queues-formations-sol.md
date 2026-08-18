# Sim batch — Q40 production queue caps + Q41 formations (Sol; one pin move)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10; rule 7 no
sweeps). Claim Q40 + Q41 (owner=sol-sim-qf). Standard rules. Sequential pytest/
Godot; logs workspace/logs/simqf-*.txt. NO pack builds/selection changes.
These two lanes are batched because BOTH move sim state (queue acceptance;
FORMATION attribute modifiers) → the retail_state_pin hash may move ONCE. You
do NOT re-mint: record old→new hash and WHY (which fixture rows are affected)
in the report and on the rows; the owner re-mints per
orchestration/reports/q5-state-pin-drift.md. retail_lockstep_determinism_runner
must stay 5/0 throughout.

## Q40 — production queues (retail oracle, verified 2026-08-18)
- `MaxQueueEntries` is authored on exactly TWO of 423 ProductionUpdate blocks:
  angmarthrallmaster.ini:587 and dwarvenbattlewagon.ini:492 (`= 1 ; only allow
  one queued upgrade at a time`). Everything else: no cap authored.
- One `ProductionUpdate` = one queue per object (barracks.ini:311-313;
  campsandcastles.ini:3502-3504 comment: upgrades ride the same module).
  Builders (porter.ini:226 DozerAIUpdate) are separate objects → own queues.
  gamedata.ini has no queue keyword.
- Ours: `retail_vertical_slice.gd:1611 "maximum_queue": 5` (invented default),
  `retail_slice_sim.gd:13389-13390` `maximum_queue <= 0 → queue-full` (INVERTED:
  absent/0 must mean uncapped), importer `:17337` defaults to 5.
Fix: absent/0 = uncapped; authored `= 1` honoured for the two objects; per-
producer queues stay; upgrades ride the structure queue; verify heroes are not
forced through a capped fortress queue in our model (retail: the fortress has
one ProductionUpdate — fortress.ini:724 — engine-side hero behaviour is not
data-authored; do NOT invent a hero-parallel rule; keep one queue per producer
and document the owner's observation as an open question with the retail
evidence). Presentation: the palantir queue display must render however many
entries exist (find the 5/9-slot assumption in retail_hud.gd and make it
authored/unbounded per the ControlBar queue art — cite what you find).
Tests (failing-first): retail_production_queue_runner: (a) barracks accepts
>5 queued with no cap; (b) ThrallMaster/BattleWagon refuse a 2nd upgrade;
(c) porter builds don't consume a structure's queue; (d) upgrade + unit share
the structure queue. Importer pytest: `maximum_queue_entries` absent → not
emitted / emitted as null, `=1` emitted as 1.

## Q41 — formations (retail oracle)
- Per-horde command buttons: `HORDE_TOGGLE_FORMATION`, `Options =
  TOGGLE_IMAGE_ON_FORMATION OK_FOR_MULTI_SELECT`, TWO-image ButtonImage:
  commandbutton.ini:651-662 Command_TowerGuardPorcupineFormation
  (`UCCommon_PorcupineFormation UCCommon_PorcupineFormationOff`,
  UnitSpecificSound TowerGuardVoiceWallFormation/LineFormation); :196-206
  Command_ToggleFormationGondorFighter (`UCSoldier_ShieldWall/Off`); also
  Rohan (:665), IsengardPikeman (:678), Easterling (:462), Mithlond/Dwarven/
  Angmar/Wild. Only 13 command sets carry a formation button (commandset.ini
  :221,413,1131,1145,1156,1170,1181,1337,1351,2131,2833,3404,3849).
- Effect: attributemodifier.ini:756-764 `ModifierList
  GondorTowerShieldGuardHordePorcupine Category=FORMATION Modifier=
  CRUSHED_DECELERATE 1000% Duration=0` (ARMOR/DAMAGE_ADD/CRUSHABLE_LEVEL
  lines are commented OUT — do not enable them); same shape :766-806 for the
  other factions.
- Ours: generic "formation" action for EVERY unit (retail_hud.gd:309, :348/:354
  fallback labels "Line/Block formation", :1426 set_active_formation for any
  selection, :1451 gate is a UI string fold). retail_formation.gd is the slot
  steering engine, no porcupine semantics.
Fix: importer compiles the formation buttons (both images, options, sounds)
and the FORMATION ModifierLists with provenance; sim applies/removes the
authored modifier on toggle (CRUSHED_DECELERATE via the existing crush path —
find where crush deceleration is applied and consume the modifier; if the
crush model has no deceleration term, add it as the authored semantic, cite
retail); HUD shows the button ONLY for hordes whose command set carries one,
with the on/off image swap; delete the generic all-units formation action and
its invented labels. Tests (failing-first): TowerGuard horde has the button,
GondorFighter horde has ShieldWall with its own icons, GondorArcher horde has
NONE; toggling swaps the image; a cavalry crush into a porcupine horde
decelerates per the modifier; a horde without the button cannot toggle.

## Definition of Done
1. New tests green; FULL importer suite (sequential) → exactly the 6 Q6 names,
   0 errors. Runners: retail_production_queue_runner (new count, re-pinned),
   formation runner(s), retail_member_combat_runner 115/0, retail_spellbook
   218/0, slice_start_roster 22/0, lockstep determinism 5/0, boot 44/0; zero
   SCRIPT ERROR/Invalid access in stderr.
2. State pin: old→new hash + which fixture behaviour moved it (or unchanged);
   NOT re-minted.
3. hygiene PASS; git status clean; commits `feat(importer):`/`feat(sim):`/
   `fix(ui):`/`test(...)`. Report orchestration/reports/sim-batch-queues-
   formations.md. Note: reaching shipped content needs the next recook (list
   under Q14-style follow-up on the rows).
