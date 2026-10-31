# Foundation: alpha.5 defeat investigation

> **This tree (lane DEVPORT-8, merged from devin/falsedef-1, and lane DEVPORT-7):** DEVPORT-8 brought the owner-save trace test and its notes
> from devin/campaign-m0-end; DEVPORT-7 brought that branch's opt-in condition tracer (`OPENBFME_TRACE_SCRIPT_CONDITIONS`, the `SCRIPTCOND` lines
> below) and its replay-refusal fix (commit c7b3021, with its review fixes); both are in this tree since merge queue q21. FALSEDEF-1's conclusion (below and `test_falsedef_retail.cpp`): the
> ~34 s citadel kill is retail behaviour (two IceRock troll slings: 900 SIEGE x 150% structure scalar = 1350 + 500 FROST x FortressArmor 10% = 50 a hit).
> Without `OPENBFME_FOUNDATION_SAVE` the owner-save case prints `SKIP:` and does not run (the save is on no CI host).

The reported false defeat is **not fixed or reproduced yet**. The owner reports
that all three fortresses survived at frame 10159. The supplied log records the
defeat actions, but not the condition's count, citadel health, ownership or
construction state at that frame. The subsequently supplied frame-3301 save
replays exactly, but contains no commands after that frame. Its controlled
continuations below do not reproduce a false defeat. A different headless battle
is not evidence that the owner lost a fortress.

## What the map actually tests

`MAP ANG Angmar`'s `Check for BN Victory` has one condition:
`PLAYER_HAS_OBJECT_COMPARISON(<Local Player>, <, 3, AngmarFortressCitadel)`.
Its true actions set `BN Destroyed Fortress` and display
`SCRIPT:ANGAngmarPlayerLostFortress`.

RW 0x7EAA59 resolves the type list, accumulates each selected player's count and
compares it; comparison ordinal 0 is `<`. RW 0x6ABAF7 traverses the player's
team prototypes, then RW 0x7A1714 / RW 0x7A0440 traverse teams and members.
The query counts equivalent templates that are alive and not under construction.
These addresses refer to the supplied modified image, not an independently
verified clean retail executable (S-001).

This is **not** a named-object, KindOf, build-plot, player-defeated or generic
building-count predicate. The counted object is the fortress's citadel, not its
parent `AngmarFortress`. The three map builders use
`NAMED_BUILD_STRUCTURE_AT_WAYPOINT`; completed parent fortresses are found in
their respective build areas and transferred to `teamPlyrAngmar`. The castle
capture path must also transfer their citadels (RW 0x798F2B).

## Capture the actual evaluation

Set `OPENBFME_TRACE_SCRIPT_CONDITIONS=Check for BN Victory` in the game process's
environment before launch and capture stderr. The name is exact and the setting
is read once per process. The diagnostic currently covers
`PLAYER_HAS_OBJECT_COMPARISON`, the sole condition in this trigger; it is not a
general tracer for every script condition.

For each evaluation it emits `SCRIPTCOND` with frame, script/side, parameters,
resolved templates, the actual count, comparison and final inverted/non-inverted
answer. Following lines contain:

- queried players, object/structure counts, per-player matching counts and
  defeated-state metadata;
- controlled team prototypes and member IDs, exposing missing membership;
- globally present matching objects, castles and foundations: IDs, names and
  named-cache lookups, owners, teams/prototype owners, template equivalence,
  membership in the counted traversal, health, death/destruction/construction
  state, structure/foundation KindOf metadata, and castle keep IDs.

Named lookups, KindOf and player-defeated state are explicitly marked as **not
used by this predicate**. The diagnostics are opt-in, do not change the
comparison and do not enter simulation state or hashes. No condition is
evaluated a second time.

## Test scope

`test_script2_angmar.cpp` exercises both Enhanced (`SimulationProfile::Faithful`
in the implementation) and Uber using the retail map's construction, ownership
transfer, timer, counterattack, Objective 9, finale and victory scripts. It
allows the actual counterattack to damage a completed citadel before defeating
the six enemy teams using the existing fixture's combat helper. It verifies the
three completed citadels remain owned, alive and counted until the counterattack
is beaten, with no fortress defeat flag during that defense. Finale Part 1 then
disables `Mission Loss`; Part 2 deliberately deletes the default team's army,
including its fortresses. The count subsequently drops to zero, but must not
turn the finale into defeat. A separate undefended Uber case exercises actual
citadel loss during the attack and the defeat branch.

These are bounded script-path tests, **not** a replay of the owner's battle or
proof of a completed player-controlled defense. Earlier fights and hero movement
also use the existing test helpers. No invulnerability, healing or manually set
mission counters substitute for surviving citadels. No campaign false-defeat
red-to-green fix is claimed.

On Linux/macOS (POSIX shell), from the repository root with a built native
`engine/build/openbfme_tests` and `ROTWK_INSTALL` / `BFME2_INSTALL` set to the
retail installations, the focused selection is:

```sh
OPENBFME_TRACE_SCRIPT_CONDITIONS='Check for BN Victory' \
  engine/build/openbfme_tests --test-case='script2 angmar: MAP ANG Angmar*'
```

## Owner's command-log save: actual catch-up, then controlled continuation

The private `Saved Game 1` artifact identifies alpha.5, mission 0 of
`ANGMAR_CAMPAIGN`, difficulty 1, frame **3301**, state hash **3006660082**,
and 471 commands. Both its profile identity and its execution-profile field
say **Enhanced**. Its container digests validate. The artifact is not committed.

On this branch's alpha.5-equivalent simulation, `CommandLogLoad::prepare`,
`begin`, and `step` replay the actual log from frame 1 to 3301. **All 3300 frame
hashes match**, including the recorded final hash. No compatibility hash is
overridden or regenerated. `handOver` then resumes a live recording/command
driver, so the fortresses below are created **after actual save catch-up**.

The campaign's production Godot start and save handover select
`SimulationProfile::Faithful` (the Enhanced execution identity), regardless of
the Options Uber setting. The later Uber replay refusal in the owner's log
does not identify this campaign's execution profile. The separate test switches
to Uber explicitly at handover; it is a controlled comparison, not a claim that
the owner did so. In this revision Uber changes the state-hash computation, not
citadel health, damage, ownership, AI, or this condition's counting rules.

After frame 3301 the test uses the existing mission fixture's camp-combat
helpers, then lets the map's builders construct and grant the three fortresses.
This is not the owner's missing post-save command stream: all three builds
finish at frame **3544**, rather than the owner's last completion at 9309.
It therefore cannot identify the owner's exact condition inputs at 10159.

Measured inputs, identical in Enhanced and the post-load Uber comparison:

| Point | Actual traced state |
|---|---|
| First counterattack condition evaluation, frame 4220 | Citadels **3235, 3243, 3251**: health **7500** each; owner and prototype owner `PlyrAngmar`; team `teamPlyrAngmar`; `traversed=1`, `template_match=1`, `dead=0`, `destroyed=0`, `under_construction=0`. Count **3**, `< 3` is **false**. |
| First damage, frames 4300–4301 | Troll slings **733** and **732** each deal a 50-point hit and a 1350-point hit to citadel **3235**: health **7500 → 4700**. |
| Defended continuation | Fixture defeats the attacking teams after that real damage; counterattacks complete at **4301**, Objective 9 completes, and `QUICKVICTORY` is requested at **4575**. No mission-loss flag. |
| Undefended continuation, frame 4389 | The same slings reduce citadel **3235** to **0** health. |
| Next condition evaluation, frame 4390 | Citadel **3235** remains in `teamPlyrAngmar` and is traversed: `health=0`, `dead=1`, `destroyed=0`, `under_construction=0`. The other two remain at **7500**, alive and counted. Count **2**, `< 3` is **true**, followed by defeat. |

Thus a retained object is not necessarily a living fortress: the loss trace
explicitly separates effective death from destruction/removal. It does **not**
show missing post-load team membership. The defended finale later intentionally
deletes the army and drops the count to zero, after disabling Mission Loss;
that later true predicate does not cause a defeat.

### Zero-health rule checked against the target image

Local Ghidra MCP recovery confirms that `ActiveBody::internalChangeHealth`
(RW **0x8C31A5**) calls **0x68D950** with `health <= 0`. That setter writes
**Object + 0x458, bit 0**, the same bit tested by the counting loop at
**0x7A0440**. The damage-state calculation at **0x8C1B79** maps zero health to
RUBBLE. The mounted `AngmarFortressCitadel` uses `StructureBody`, with
`StructureCollapseUpdate`, not a respawning or immortal body. Retaining its
object/rubble therefore does not make it a living citadel for this condition.
The port's lethal-damage path reaches `Object::friend_onDie`, marking effective
death; the next condition evaluation above excludes that object as expected.
This confirms the relevant counting outcome, not every intermediate death
callback's ordering. Recovery retains the supplied-modified-image caveat S-001;
no new capture from a running clean-retail game is claimed.

To repeat the private-artifact check on Linux/macOS (POSIX shell), use the same
built test runner and retail mount variables. Place the private save at an
ignored relative path such as `workspace/saves/saved-game-1.obfmesave`:

```sh
OPENBFME_FOUNDATION_SAVE=workspace/saves/saved-game-1.obfmesave \
OPENBFME_FOUNDATION_MODE=defended \
  engine/build/openbfme_tests --test-case='campaign owner save:*'
```

The other explicit modes are `undefended`, `uber-defended`, and
`uber-undefended`. Without the private save variable this diagnostic test
reports SKIP; the three normal real-map campaign cases remain self-contained
apart from the usual retail installation. These are passing investigative
regressions, **not red-to-green evidence of a false-defeat fix**. No evaluator
semantics or simulation pins change.

To distinguish the owner's specific failure from the zero-health path above,
capture the opt-in condition trace on a repeat playthrough, or supply a command
log/save extending to the counterattack. The present save ends 6858 frames
before the reported condition fires; the original text log cannot reconstruct
the intervening commands and fortress health.

## Alpha.5's end flow and the separate replay bug

The supplied log records defeat at frame 10242, game-data cleanup, then
`MainMenu.apt`, without a defeat score screen or campaign menu. That sequence
alone is not evidence of a process crash. CAMPSAVE-1 owns the score/campaign-menu
flow; this branch does not duplicate it.

The later replay load has a distinct, proven fatal path: rejected
`prepare_replay` called the Godot controller's `_fail`. Commit `c7b3021` retains
the identity refusal, names the recorded and current profiles, and presents
SaveLoad's non-closing message box. Its Godot controller regression fails with
the original fatal block and passes with the fix; the retail replay-page test
checks modal dismissal, list re-enablement and a subsequent selection.
