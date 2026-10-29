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

The skirmish AI block now requires version 7: predefined siege-unit requests, BFME2 interest-zone threat, spatial registrations/groups,
ArmyBreaker timers, defensive target sizes and team recruitment origins affect
future decisions. Older layouts are refused explicitly. SimpleExpansion keeps
upstream prototype index 8; SimpleDefense uses 9. Capture history and expansion
requests/counters retain their upstream representations. AIStatCollector version 3
and TerrainTrees version 2 retain complete spatial and tree-selection state.
Native tree snapshots preserve cached groups, whereas retail rebuilds them; this
existing format difference is reported by S-4252.

## Remaining gaps

This port does not close native siege/wall construction, remaining targetless
families, team reassignment/formation limitations,
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

## BFME2 roaming defense follow-up

Ported AIRoamingDefenseTactic from Open-BFME-2: eligibility RVA `0x5AAC35`,
cleanup `0x5AAD26`, run `0x5AAB9C`, update `0x5AB085`, and destination selection
`0x5AAE3F`. The native vtable's team-count slot resolves to the zero-return body
at VA `0x4D43D0`. Easy alone schedules a check five seconds later. The tactic
recruits no teams and sends the first idle default-team army registration not
leaving a factory. Tactic membership is also excluded because the port represents
native team assignment in its tactic records (S-418).

Owned economy registrations strictly within 900 units of the AI center in 3D
are selected with source-line 164's random draw. If economy registrations exist
but none qualify, no order is issued. Only an empty economy registry falls back
to the first DOZER_FACTORY: an integer angle 0..6 radians, source line 186,
places an attack-move destination 400 units from it. Cleanup clears both named
counters without drawing a cooldown. The roaming flag reuses the disjoint
per-tactic +0x58 field already saved and hashed; no snapshot layout changes.

Three source regression cases cover all four difficulty gates, exact delay,
zero-team readiness/run/end, factory-exit filtering, first eligible unit,
strict economy boundary, no-fallback behavior, first fortress and absent fortress,
and random call sites/bounds. Wider targetless registry/order and team/formation
limitations remain open; the trig facade retains S-081. This closes a tactic
body, not the overall retail replay parity gate.

Validation: the native Release build passed; 88 selected source/power/expansion/
save cases passed 15,190 assertions. The four-difficulty repeated 1,500-frame
matrix, seven-faction construction/economy/attack case and offensive tactics
passed four cases / 15,328 assertions. The focused roaming tests passed three
cases / 156 assertions. The installed commit gate and whitespace checks passed.
The gameplay fixture mounts the existing combined BFME2/RotWK assets, so these
runs establish native regressions and determinism, not a pure BFME2 retail trace
match. BFME2 executable facts ran against BFME2_INSTALL; RotWK-only binary facts
were skipped without RW_GAME_DAT. No graphical or cross-play gate was run.

## BFME2 tech-building opportunity follow-up

Ported `AITargetHeuristicTechBuilding.cpp` RVA `0x572EFF` using the existing
historical capture registry. Uncaptured flags require the closest LINKED_TO_FLAG
object within 150 units in 3D, and both site and linked object must be non-allied.
The nearest qualifying site within an inclusive 1,000 units in 2D wins, preserving
registration order on ties. After Rush, a non-economy site is rejected while
TacticalAI's interest-zone vector (+0x20, represented by attackedAt) is nonempty.
This vector is not the running tactic list. Selection consumes no random draw;
the target uses DefaultTargetThreatRadius, manager +0x884 in BFME2.

Three synthetic source cases pass 302 assertions across phases, danger states,
economy kinds, allied nearest-linked objects, absent linked objects, exact range
boundaries and stable ties. The partition fixture has an explicit map region,
so getClosestObject uses its normal range rather than its no-map zero range.
Historical registration and snapshot regressions remain in the manager tests.

Validation: the four data-mounted difficulty/faction/tactic cases passed 11,768
assertions. The broad selection passed 90 of 91 cases; its sole failure was the
Athelas engine timing pin, 22 -> 25. A control disabling only the new selector
restored exactly 22. The pin is updated with this cause; it is not a retail
frame oracle. The final targeted rerun covers the corrected pin.

## Shared path-to-structure query

Added the BFME2 `PathfinderQuickStructure.cpp` no-locomotor-override form, its
`Pathfinder::rva002F4AC1` boundary helper and `Rva002E7261Info::cellCallback`.
It rejects missing AI and incompatible surface masks, checks geometry shape 0's
unrotated offset when the origin is unobstructed, and probes +X, -X, +Y, -Y out
of the actual obstacle footprint. The first differing obstacle ID supplies the
zone; effective-zone equivalence and the existing portal query decide access.
The ray length is exactly `2 - trunc(radius * -0.1f)`. Existing layer, zone and
portal facades retain their documented limitations. This adds the shared query;
siege bodies and farm-raiding callers are separate remaining work.

Two synthetic cases pass 12 assertions for AI/surface refusal, clear and isolated
targets, and all four cardinal exits. The combined focused run, including tech
selection, the corrected Athelas pin and existing pathfinding regressions,
passed nine cases / 359 assertions. Native Release build passed.

## BFME2 farm-raiding follow-up

Replaced the RotWK nearest-farm stand-in with `AIFarmKillSquad.cpp` and
`Rva005ACE15Finish.cpp`: constructor RVA `0x5ACF38`, cleanup `0x5ACD51`,
production `0x5AD243`, farms `0x5AD404`, and update `0x5AD5DD`.

- The constructor draws d100 at line 53 and admits rolls <20 (19/100).
- Native named counters are AIFarmKillSquad_IsRunning / AIFarmKillSquad_FrameNextRun.
  Cleanup draws 150..600 frames at line 337, rather than the RotWK 50..300.
- One team has priority 30, HERO/ARCHER/MACHINE forbidden, minimum one in Rush
  and two later, and a maximum drawn through three at line 371.
- With an enemy fortress, reachable tracked buildings are ranked by perpendicular
  distance from the infinite 3D line through the team center and first fortress.
  Farms must score strictly above 300; production has no cutoff and excludes the
  fortress. Equal scores retain the first registration. There is no weighted
  nearest-farm substitute. With no fortress, selectors draw directly from tracked
  registration order (farms line 304, structures line 250).
- Both selectors run in order. When both succeed, the 0..2 draw at line 135 chooses
  farms only on zero. BFME2 retains a resolving target until retargeting; the
  earlier RotWK gate-specific drop behavior is not applied.

Four exact synthetic cases passed 130 assertions. The targetless integration
fixture first verifies the native constructor branch, then explicitly enables
its early gate to test recruitment independently of that random roll; its
source draw sites and eight remaining unported applicability checks are pinned.

The broad selection passed 96 of 98 cases; only derived expansion and Athelas
engine timing assertions changed. An isolated control restoring the prior farm
behavior restored all six expansion pins (1821/1821/1824/1825/2593/2593) and
Athelas 25. With BFME2 farm behavior enabled, the expansion sequence is
1519/1519/1565/1566/2369/2369 and Athelas 24. Corrected pins and the four source
cases passed six cases / 191 assertions. The all-difficulty repeated-frame,
seven-faction attack and offensive-tactic selection passed four cases / 7,007
assertions. Assertions inside per-frame loops vary with tactic lifetimes; the
fixed scenario checks still require each difficulty to train, survive and repeat
all 1,500 frame hashes exactly. These are native checks using combined archives,
not BFME2 retail traces. No native snapshot layout changed.

## BFME2 interest zones and team handover

Ported the handover comparison from `AITeamBuilder_doesTeamMeetThreat.cpp`
RVA `0x59A1C1` and `Rva002C589BMax.cpp` RVA `0x2C5AE6`: sum raw template
ThreatLevel for CAN_ATTACK/HERO members, in native member order, and compare it
with the larger of target threat and interest-zone threat. SUPPORT alone adds
nothing. The RotWK counter-category weighting is no longer used for handover.

`Rva002C5EF0Xfer.cpp`'s constructor samples enemy CAN_ATTACK/HERO raw template
threat once when the interest zone is created. This value is stored rather than
rescanned during recruitment. `TacticalAI_updateInterestZones.cpp` records the
attacking member's position, not its victim's position, and excludes support-only
members. The native four-second lifetime is confirmed by
`Rva007ABC6FFrameRateInits.cpp`'s g_00DFEFCC initializer.

The native AI save layout is version 6, rejecting earlier layouts, because this
recorded scalar affects future handover. It is included in the state hash. The
legacy category records used by other still-RotWK tactical paths remain present;
this change does not claim those paths now match BFME2. Native AI team objects,
script recruitment availability, priority reassignment, and formation remain open.
Two synthetic cases passed 43 assertions for raw values, support exclusion,
exact equality, danger-vs-target maximum, attacking position, hash sensitivity
and snapshot restoration.

Validation: all 101 selected source/power/expansion/manager/targetless/save cases
passed 15,084 assertions, including the existing interest-zone registration tests.
The four repeated-difficulty/faction/offensive-tactic cases passed 6,995 assertions.
The native Release build passed. No timing pin changed in this follow-up.

## BFME2 native generator lists

`AITacticsGenerator.cpp` RVA `0x505E5D` registers exactly SimpleAttack,
FlankAttack, SimpleSiege, SiegeGates, AIBasePenetrationTroopsTactic, once each and
in that order. RVA `0x506020` registers FarmKillSquad, AIRingHeroTactic,
StructureCreep, ReturnTheRing, AIStartWoTRBattleTactic, AIRoamingDefenseTactic.
The branch now generates those lists rather than the wider RotWK ten/eleven
lists. Formation/Pincer/Feint/FlagCaptureSquad and the other extended prototypes
retain internal identifiers and bodies for native snapshot handling, but are not
new BFME2 generator candidates. Source pick sites are 326 offensive, 357 defensive,
388 expansion and 426 targetless; the base offensive minimum draw is line 386.
The capture-history registry remains, including tech-building selection; this
list correction does not establish the complete BFME2 AI capture execution path.

The targetless gameplay fixture now pins a single native FarmKillSquad start,
four unported native applicability checks, and exact source draws. The offensive
matrix requires live Flank waypoint/draw coverage and zero generated RotWK-only
Formation/Pincer/Feint/FlagCaptureSquad tactics. It and the all-difficulty repeat
and faction attack checks passed four cases / 20,580 assertions.

The broad selection passed 100 of 102 cases; only the derived expansion and
Athelas timing assertions changed. Restoring just the prior registration lists
restored all six 1519/1519/1565/1566/2369/2369 pins and Athelas 24 exactly. Native
lists produce 2123/2123/2126/2127/2869/2869 and Athelas 23. These are engine
regressions, not retail timing oracles. No save layout changed beyond version 6.

## Remaining closure gates after this audit

The requested full gap closure is **not complete**. These are concrete remaining
implementation and evidence gates, rather than items inferred closed from a
successful native run:

| Area | Source/dependency still needed | Closure gate |
| --- | --- | --- |
| Four native targetless families | RingHero, StructureCreep, ReturnTheRing, StartWoTRBattle; StructureCreep has substantial donor bodies, while sibling bodies are split among address-named units and need a complete callgraph audit | Native eligibility, recruitment, orders, cleanup, RNG and save tests for each applicable mode |
| Siege tactics | SiegeGates/SimpleSiege bodies and predefined recruitment are now ported (see follow-up below); general native team ownership and movement facades still have open dependencies | External retail decision/frame comparison and native team/formation parity |
| AI wall planning/construction | AIWallBuilder, AIWall, map wall-library loading and buildable wall request lifecycle | Source plan selection, affordability, hub/gate/segment order, completion/failure and snapshots |
| Native teams and reassignment | AITeamBuilder recruitment predicate, Team/TeamPrototype identity, recruitment availability and priority; current tactic vectors represent membership | Ownership changes and defensive/priority transfers follow native rules without duplicate membership |
| Group formation/movement | Native formation manager, group ownership and remaining AIGroup state transitions | Native route/turn/order fixtures plus deterministic save continuation |
| Remaining BFME2 differences | Legacy RotWK manager/builder, target threat/counter logic and individual power decisions still need per-body BFME2 audits; 41 names alone are insufficient | Each active native body has source-backed decisions, numeric order and RNG tests |
| AI capture execution | Native opportunity-target execution/capture behavior after removal of the RotWK-only capture squad from generation | Neutral and enemy-held sites are captured through native orders without unintended damage |
| Retail evidence | Pure BFME2 startup/data fixtures and external native decision/frame traces; present gameplay fixtures mount combined BFME2/RotWK archives | All six BFME2 factions and four difficulties compared against native evidence; numeric/pathfinding dependencies remain explicit |

Full retail replay or multiplayer parity remains outside what the current native
regressions establish. Open shared-engine numeric, pathfinding and formation
stops remain visible; none were suppressed to call this overhaul complete.

Final generator validation: the corrected timing tests, native prototype list
facts and native targetless gameplay case passed four cases / 762 assertions.
The earlier broad run's other 100 cases passed. Native Release build, installed
commit gate and whitespace checks passed. The work remains local.

## BFME2 siege tactics follow-up

Ported `AITacticOffensiveCheckTarget.cpp`, `AITacticSiege_sideHasIdleSiegeWeapons.cpp`,
`Rva005DC87BXfer.cpp`, `Rva005A9ACDTactic.cpp` and `Rva005A9CB3Tactic.cpp`.
The first mobile CAN_ATTACK registration now performs the actual position or
structure path query; unreachable targets admit siege tactics and refuse normal
attacks. The native empty/final-registration null guard is retained. A live
DOZER_FACTORY target is refused during Rush on every difficulty, as BFME2 does.
Siege availability uses default-team ThreatBreakdown category 7, rather than a
SIEGEENGINE KindOf, health or selection filter.

SiegeGates uses pooled team sizes, forbids ARCHER, attacks the nearest registered
enemy gate or non-walkable wall segment, excludes NOT_AUTOACQUIRABLE/destroyed
objects and preserves first-registration ties. After a real breach it attack-moves
all teams to the target and completes when idle. SimpleSiege requests one unit
per tracked category-7 template for team 0, has a second assault team, attacks the
wall, stages the assault 500 units back, waits for reachability, then advances
and finishes at idle. Its native predefined-recruitment pass consumes matching
requests in army registration order at priority >=20; unfinished requests block
handover. General cross-team priority/defensive reassignment remains S-418.
Team object attacks now use the native player command source.

Predefined requests are saved and hashed; native AI layout version 7 rejects
older layouts. Unknown requested templates and invalid siege stages are refused
transactionally. Existing AIGroup placement/clipping, native team representation
and shared pathfinding limits remain explicit: the stand-back geometry is tested
before that movement facade, not presented as a retail route trace.

The first focused run passed four cases / 175 assertions. The broad source/power/
manager/targetless/save selection passed 106 cases / 17,375 assertions. The
four-difficulty repeated 1,500-frame matrix, seven-faction attack and native
Flank selection passed four cases / 16,063 assertions. No timing pins changed.
Additional focused checks cover malformed predefined requests, mid-stage save
continuation and the Rush guard at all difficulties. AI wall construction itself
is still separate from these attacking tactics.

Final siege checks passed five cases / 209 assertions, including malformed-save
rollback and resumption between breach and assault. Native Release build,
installed commit gate and whitespace checks passed. Commits remain local.
