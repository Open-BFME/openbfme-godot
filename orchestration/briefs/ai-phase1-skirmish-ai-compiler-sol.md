# Skirmish AI — Phase 1: compile aidata.ini + skirmishaidata.ini into a typed pack document (Sol)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10 binding;
rule 7: NO find-replace sweeps). Claim row Q18 in orchestration/queue.md
(owner=sol-ai-p1). Exclusive tree access; no branches/worktrees; git add by
explicit path; BANNED git add -A / reset / restore / clean / stash / amend.
Long output → workspace/logs/. Pinned interpreter
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe;
BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2.
Run pytest sequentially (no -n). Do NOT rebuild/publish/select any pack.

## Why
The sim's skirmish AI is invented (retail_slice_sim.gd `AI_DIFFICULTY_PROFILES`
~:246, `AI_PRODUCTION_PLAN` ~:227) and nothing in importer/ parses retail's AI
data. Retail's skirmish opponent is INI-driven: `data/ini/default/aidata.ini`
(globals) + `data/ini/default/skirmishaidata.ini` (per-faction ArmyDefinition,
CombatChainDefinition, DifficultyTuning, AIBase, AIDozerAssignment). This phase
compiles that data, with provenance, into a pack document. No sim change.

## Oracle facts (verified 2026-08-17; anchors are in workspace/retail-extract/data/ini/)
- `aidata.ini` (top level) is a 3-line stub → real data in `default/aidata.ini`
  (11,530 B): AIData globals :13-96 (RebuildDelayTimeSeconds=30 :57,
  Wealthy=7000/Poor=2000, TeamSeconds, MaxRecruitRadius, SupplyCenterSafeRadius
  =240 :56, AttackPriorityDistanceModifier=80 :31, RotateSkirmishBases,
  build-phase priority modifiers :107-110), SideInfo×5 :113-142
  (ResourceGatherers Easy/Normal/Hard), ONE `SkirmishBuildList Gondor` :145-202
  (all AutomaticallyBuild=No), `AttackPriority` blocks :212-246 with retail
  comment :205-210 "no longer in use … THIS IS OUTDATED, DO NOT USE THIS".
- `default/skirmishaidata.ini` (BFME2 52,994 B / RotWK twin 77,989 B under
  workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/default/):
  `AIBase`×46 (Side, Map="AI BASE - MOTW - Horse Rush Base", GameMapToUseOn=
  "<ANY>" or a map name; 4 generic strategy variants per side + per-map
  overrides), `AIDozerAssignment`×6 :485-511 (NOTE retail bug: Dwarves entry is
  duplicate-named MordorDefaultDozer :510 — resolve by Side, not name),
  `SkirmishAIData TheSkirmishAIData` :522-668 (CombatChainDefinition×12 with
  TargetTypes / TargetPriorityModifiers matrices, -1.0 = never acquire; formula
  in comment :526-531; DefaultTargetThreatRadius=300; BrutalDifficultyCheats
  15%/15%; DifficultyTuning Easy/Normal; Disable* toggles),
  `ArmyDefinition`×6 BFME2 / ×8 RotWK (MenOfTheWestArmy :670-745 etc.): phase
  params (MustUseCommandPointPercentage_Phase1/2/3, StructureRebuildPriority
  Modifier, FortressRebuildPriority, PhaseDuration_Rush/MidGame,
  EconomyBuilderMinFarmsOwned, upgrade priorities, team-builder depths,
  TacticalAITargets/MaxTeamsPerTarget), AIEconomyAssigment (sic — retail
  misspelling, preserve token), AIWallNodeAssignment, ArmyMemberDefinition×N
  (Unit + PercentageOfArmyPhase1/2/3), HeroBuildOrder, OffensiveBuildings.
- There are NO TeamTemplate blocks (Generals concept) — do not invent them.
- `.bse` base layouts are already decoded by importer/openbfme_importer/
  sage_map.py:2132/2220 (consumers castle_behavior.py:165,
  retail_building_lifecycle.py:944) — Phase 1 only records the AIBase→template
  binding, it does not embed layouts.
- sage_cst.py:1021-1060 `_skip_foreign_top_level` deliberately SKIPS the AIData
  family — write a standalone End-terminated block reader for these two files;
  do not extend the Object reader.

## Deliverable
New `importer/openbfme_importer/skirmish_ai_compiler.py` + wiring so the faction
/ neutral pipeline emits `ai/skirmish.json` (or the house-convention path —
look at how other typed docs like spellbooks/data/*.json are placed and
registered in the pack manifest; follow that) with:
- `schema: "openbfme.skirmish-ai"`, `schemaVersion`, `game` (bfme2|rotwk),
  every scalar/row carrying `sourceIni` + `line` provenance (house style, see
  playable_unit_compiler.py ~:5459).
- Sections: `globals` (AIData), `sideInfo`, `armies` (ArmyDefinition keyed by
  name with `side` inferred from AIEconomyAssigment/AIDozerAssignment side or
  the ArmyDefinition's own faction token — state your rule), `combatChains`,
  `difficultyTuning`, `brutalDifficultyCheats`, `disables`, `dozerAssignments`,
  `aiBases` (side, mapTemplateName, gameMapToUseOn; case-insensitive filename
  join documented per docs/castle-siege-design.md:1047-1049 — record the
  resolved `.bse` virtual path if it exists in the catalog, else `unresolved`).
- `deprecatedByRetail: true` on SkirmishBuildList and AttackPriority if you
  emit them at all (recommend: emit as `deprecated` section for provenance,
  never as behaviour).
- Unknown/unsupported keys: recorded under `unsupported` with line numbers,
  never silently dropped (rule 5).
- Also emit a `census` block: counts of ArmyDefinition / ArmyMemberDefinition /
  CombatChainDefinition / AIBase per game.

## Tests — failing-first (importer/tests/test_skirmish_ai_compiler.py)
1. MenOfTheWestArmy round-trip: exact ArmyMemberDefinition rows (count them —
   report says 11; verify), phase percentages preserved exactly (do NOT assert
   they sum to 100 unless they do), HeroBuildOrder token list, OffensiveBuildings.
2. All 12 CombatChainDefinition matrices; -1.0 sentinel preserved; TargetTypes
   and modifiers arrays have equal length per chain.
3. AIDozerAssignment duplicate-name collision resolves by Side (6 rows).
4. AIBase: 46 rows; a per-map override and an <ANY> row both parsed; at least
   one resolves to an existing `.bse` catalog path.
5. BFME2 vs RotWK: 6 vs 8 ArmyDefinition (Arnor, Angmar added).
6. Provenance: every emitted scalar has `sourceIni` matching
   `data/ini/default/(aidata|skirmishaidata).ini` and an int `line`.
7. Deprecated sections carry `deprecatedByRetail: true` and the retail comment
   line reference.
8. Full pipeline smoke: building a small fixture pack (however the existing
   tests build fixture packs) includes the document and the manifest lists it.

## Definition of Done (verbatim outputs in report)
1. New tests green; FULL importer suite (sequential) → exactly the 6 Q6
   failures, 0 errors, log workspace/logs/ai-p1-importer-full.txt.
2. `python tools\check_pack_addresses.py` PASS (packs unchanged);
   `tools\gate-hygiene.ps1` PASS; git status clean.
3. No game/ files changed (this phase is importer-only). No pins moved.
4. Report orchestration/reports/ai-phase1-skirmish-ai-compiler.md: schema
   summary, census numbers per game, unsupported-key list, and the exact
   consumer seam for Phase 2 (retail_slice_sim.gd reads
   manifest.ai_production_plan at ~:2402/:2410 and _team_ai_production_plan
   ~:1020/:1056/:1206 — describe how the new document should be surfaced there).
5. Add queue row Q19 for Phase 2 (sim consumes compiled build order + routes AI
   through submit_command; battle-signature pin migration) with the evidence
   path to your report.
