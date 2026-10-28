# AI source overhaul and BFME2 parity work

This branch ports the source-based AI work from the personal fork onto upstream
`87579746eb`. It carries no enhanced counter-production, retreat micro, siege
planner, capture planner, accelerated recruitment, or reinforcement-wave policy.
The current upstream expansion and capture-history implementations are retained.

## Source evidence and difficulty

Current direction (owner, 28 October 2026): target BFME2 1.06 AI behavior in
Open-BFME-2 `e190ef5fe4`, rather than RotWK-specific differences. Earlier ports
on this branch were researched against RotWK and still require a BFME2 audit
where they differ. The BFME2 follow-up below records which paths have been
corrected. Native determinism tests alone do not establish retail replay parity.

| Port | Source evidence | Regression coverage |
| --- | --- | --- |
| Brutal unit/building and hero production reductions | Common/Thing/ThingTemplateBuildCost.cpp; target RW 0x73C344..0x73C5C8, hero wrappers RW 0x780AD3/0x780B72 | All four difficulty IDs, registered-AI gate, human/unregistered exclusion, upper-only clamp, exact cost/time/hero rounding |
| Builder stage order and reserve shortfalls | AIBuilderUpdate.cpp; Common/AIBuildable_doBuild.cpp 0x55ADE2 | Exact cash/reserve/queue results with partial cash, enough cash, and insufficient combined funds |
| Spatial structure grouping | Common/Rva00283114GroupMerge.cpp, Rva00282B26.cpp, Rva004DFA43Sphere.cpp, Rva004DFCE6SphereMerge.cpp | Strict adjacency, bridge merging, registration/removal order, human structures, OffensiveBuildings exclusion, snapshot consistency |
| Structure Breaker, Pref Walls, Basekill, Citadel | AISpellBookStructureBaseKill.cpp and corresponding decision bodies; target RW 0x9E7A98/0x9E7942/0x9E6EF4/0x9E6CCB | Ordered group selection, fortress preference, exact Citadel draws and position |
| Army Breaker and Tree Killer | AISpellBookArmyBreaker.cpp, Common/AISpellBookTreeKiller_shouldActivate.cpp | Independent per-player timers, target-derived threat thresholds, global tree selection and snapshots |
| Defensive heuristic and SimpleDefense | AITargetHeuristicBaseDefense.cpp; Common/Rva005AA735Tactic.cpp | All four difficulties, 30/101 small-group gate, 1/3/half-army maxima, stable 3D recruitment, snapshot restoration |

Medium is internal NORMAL (1); Easy is 0, Hard 2, Brutal 3. Economy, special-power,
and offensive probability gates remain data-driven, including Easy's Rush refusal.
Hard and Brutal retaining some identical probability rows is intentional. Brutal's
production advantage is applied separately. Defensive tactics bypass the offensive
difficulty gate as the source does. No damage boost is invented.

## Native snapshots

The skirmish AI block now requires version 5: spatial registrations/groups,
ArmyBreaker timers, defensive target sizes and team recruitment origins affect
future decisions. Older layouts are refused explicitly. SimpleExpansion keeps
upstream prototype index 8; SimpleDefense uses 9. Capture history and expansion
requests/counters retain their upstream representations. AIStatCollector version 3
and TerrainTrees version 2 retain complete spatial and tree-selection state.
Native tree snapshots preserve cached groups, whereas retail rebuilds them; this
existing format difference is reported by S-4252.

## Remaining gaps

This port does not close native siege/wall construction, remaining targetless
families, tech-building opportunity scoring, team reassignment/formation limitations,
or full per-decision behavioral parity. BFME2 has 41 AI decision names; all now
have implementation paths. Four entries from the wider RotWK registry still
refuse with S-1421: SOUL_FREEZE, ATTACK_HEAL_AOE, DEBUFFECONOMYBUILDING and
DEBUFFPRODUCTIONBUILDING. Those four are outside the BFME2 baseline. Existing
source stops remain visible at runtime. The engine still accepts the wider
registry; strict BFME2 parser acceptance is not established by the 41-name test.

## Earlier overhaul validation

Native build and exact source regressions pass. The unmodified upstream baseline
passed 19 AI/data/manager cases (452 assertions) with the installed game files.
The four-difficulty gameplay matrix passed all 68 assertions: each difficulty
ran twice for 1,500 frames, matched every frame hash, trained an army and remained
undefeated. Observed farm/attack counts for seed 51027 were Easy 2/1, Medium
5/1, Hard 4/2 and Brutal 5/2; these are observations, not retail goldens.
Easy recorded 1,411 economy refusals; Medium 25; Hard/Brutal zero.
Exact source regressions passed 9 cases / 310 assertions, including malformed
spatial snapshots and transactional rejection of dangling registrations.
The final combined source/power/expansion/save suite passed 74 cases / 13,543
assertions. The seven-faction construction/economy/attack and offensive-tactic
selection passed 6 cases / 15,430 assertions. Together with the difficulty matrix,
these three runs passed 81 selected cases and 29,041 assertions. Binary-oracle
checks were skipped because RW_GAME_DAT was not configured; no binary provenance
or retail parity claim is made. The installed commit gate and whitespace check
passed. Full-suite, multiplayer cross-play and graphical runtime were not tested. The four-difficulty gameplay test runs each scenario twice and
compares every simulation frame; this is determinism coverage, not an external
retail oracle. Asset-free tests pin the source's exact expected decisions and values.

## BFME2 follow-up

- Spellbook Heal now follows `AISpellBookHeal.cpp` RVA 0x5D81A4: first busy
  friendly unit (heroes and structure victims allowed), combat statistics in
  radius 350, friendly mean health strictly below float32 0.8, then the AoE
  location validator. RotWK's order-list implementation is deliberately not used.
- Army Breaker now follows `AISpellBookArmyBreaker.cpp` RVA 0x5D8619: at least
  four enemies and no more than half as many friendly units, then the busy unit's
  victim position, then its separate per-player timer. Rejected count/location
  checks do not consume the cooldown draw. The source draw site is line 0x4E;
  `Rva007B4C70FrameScaleInits.cpp` supplies 15..30 seconds (75..150 logic frames).
  This replaces the earlier RotWK attacked-at threat/range selection.
- Combat statistics follow `AISpellBookBaseGetStatsInCombatRange.cpp` RVA
  0x5EEBF6: CAN_ATTACK or HERO, excluding structures. SUPPORT alone is not counted.
- A read-only BFME2 executable test checks all 41 names at VA 0xDD3068,
  the Heal and Army Breaker vtable decision slots, and the logic clock at
  0xDBA4E4. It passed 85 assertions against the installed BFME2 game.dat.
- The two live decision cases passed 59 assertions. Their fixtures use the
  existing combined-archive test arena; this is not a pure BFME2 startup or
  external frame-trace comparison. Remaining engine numeric, pathfinding,
  targetless-tactic, wall/siege and formation gaps still block full parity.

The BFME2 executable bytes are read only at test time and are never committed.
Full game parity is not claimed. Final source/power/expansion/save regressions
passed 84 selected cases and 15,008 assertions. The four-difficulty, seven-faction
attack, and offensive-tactic gameplay selection passed 4 cases / 15,328 assertions.
The Udun expansion engine pins changed to 1821/1821/1824/1825/2593/2593. An
isolated control with only Heal returning false restored all six old pins
(2425/2425/2428/2429/3169/3169), identifying the new Heal decision/cooldown RNG
consumption as the cause. These pins are engine regressions, not retail timing
oracles. Native build, installed commit gate and whitespace checks passed.

### BFME2 defensive targeting

`AITargetHeuristicBaseDefense.cpp` RVA 0x5737D6 admits CAN_ATTACK or HERO,
not SUPPORT alone. The branch now uses that filter and the native draw sites
0x56 (group selection) / 0x5F (small-group gate), replacing the RotWK labels
0x57 / 0x60. Group ordering, the 30/101 small-group gate and 1/3/half-army
recruitment limits are retained. A regression places a support-only enemy near
an owned base, verifies it is ignored, then introduces an eligible attacker and
checks the exact selected object and RNG record. The focused defense/spatial
selection passed 8 cases / 267 assertions, including all four difficulties.

Final defensive-targeting validation passed 9 focused cases / 280 assertions
and 3 data-mounted difficulty/offensive-tactic cases / 15,263 assertions. The
four-difficulty repeat checks matched every simulation frame. Native build,
installed commit gate and whitespace checks passed.
