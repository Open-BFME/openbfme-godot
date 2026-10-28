# Source AI overhaul on upstream main

This branch ports the source-based AI work from the personal fork onto upstream
`87579746eb`. It carries no enhanced counter-production, retreat micro, siege
planner, capture planner, accelerated recruitment, or reinforcement-wave policy.
The current upstream expansion and capture-history implementations are retained.

## Source evidence and difficulty

The donor is Open-BFME-2 `e190ef5fe4` (BFME2 1.06). Established RotWK differences
retain priority under PLAN.md. Existing S-001 target-binary provenance and numeric
parity caveats remain; successful engine tests do not establish retail replay parity.

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
or all power decisions. Five of 53 power decisions still refuse with S-1421:
SOUL_FREEZE, ATTACK_HEAL_AOE, spellbook HEAL, DEBUFFECONOMYBUILDING and
DEBUFFPRODUCTIONBUILDING. Existing source stops remain visible at runtime.

## Validation

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
