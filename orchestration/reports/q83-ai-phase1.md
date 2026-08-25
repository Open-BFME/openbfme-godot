# Q83 Phase 1 — compile retail skirmish AI

Date: 2026-08-25  
Lane: `codex-q83`  
Scope: importer only; no `game/`, pack, selection, pin, or publish mutation

## Outcome

The faction compose path now compiles the effective retail
`data/ini/default/aidata.ini` and
`data/ini/default/skirmishaidata.ini` into `ai/skirmish.json`. The Men host
pack owns the edition-wide document and registers it as `pack.files.skirmishAi`,
following the existing single-owner convention for global strategic data.

The document is lexical, not behavioral. Authored scalar spellings such as
`90%`, `50.0f`, `1 : 1050`, and `-1.0` survive exactly; token rows are split
only into their authored token sequence. The compiler does not choose units,
score targets, advance phases, or calculate difficulty effects.

## Schema

- `schema`: `openbfme.skirmish-ai`
- `schemaVersion`: `0`
- `game`: `bfme2` or `rotwk`
- Authored facts: `{value, sourceIni, line}`
- Sections: `globals`, `sideInfo`, `armies`, `combatChains`,
  `difficultyTuning`, `brutalDifficultyCheats`, `disables`,
  `skirmishGlobals`, `dozerAssignments`, `aiBases`, `deprecated`,
  `unsupported`, and `census`
- Army side rule: the `ArmyDefinition`'s own authored `Side` field is
  authoritative. `AIEconomyAssigment` (retail spelling preserved) and dozer
  assignments remain separate raw rows; they are not used to invent a side.
- Dozer rows are keyed by authored `Side`, so retail's duplicate
  `MordorDefaultDozer` name remains visible for both Mordor and Dwarves without
  a collision.
- `AIBase.Map` is joined to the `.bse` catalog case-insensitively. The resolved
  virtual path is catalog metadata only; layouts are not embedded and the
  runtime still decides how to use the binding.
- The obsolete `SkirmishBuildList` and `AttackPriority` blocks are retained
  only under `deprecated`, each marked `deprecatedByRetail: true`, with the
  retail deprecation comment at `aidata.ini:207`.
- Structurally unknown statements fail into `unsupported` with source and line.
  Arbitrary assignment keys inside supported retail blocks are themselves a
  supported raw-field shape, so new authored knobs are retained instead of
  silently dropped.

## Live oracle census and anchor drift

| Game | ArmyDefinition | ArmyMemberDefinition | CombatChainDefinition | AIBase | Dozers | Resolved BSE |
|---|---:|---:|---:|---:|---:|---:|
| BFME2 | 6 | 73 | 12 | 60 | 6 | 0/60 in the current `workspace/retail-extract` comparison tree |
| RotWK 2.01 | 8 | 117 | 12 | 104 | 8 | 104/104 in the canonical effective-assets tree |

The 2026-08-17 brief anchors drifted. In the current pure-2.01 RotWK oracle,
dozers are at lines 842–880, `SkirmishAIData` begins at 891, combat chains at
903–975, and armies at 1051–2684. The old 46-base/6-dozer statement is not the
current RotWK file; current BFME2 also has 60 bases, not 46. The live
`MenOfTheWestArmy` has 14 member rows, not 11.

RotWK-only `ArnorArmy` and `AngmarArmy` account for the 6→8 army delta.
RotWK also carries Arnor/Angmar dozers and expanded generic/per-map base
bindings. No `TeamTemplate` blocks were found or invented.

## Unsupported-key census

Both live documents compile with `unsupported: []` for BFME2 and RotWK. This
means every encountered block and assignment is represented by the raw schema;
it does not mean unknown syntax would be ignored—the standalone reader records
such syntax with its exact source line.

## Phase 2 consumer seam — no code in this lane

The shipping authority is `game/src/retail_slice/retail_slice_sim.gd`.
Current live anchors are:

- Invented `AI_PRODUCTION_PLAN`: line 227.
- Invented `AI_DIFFICULTY_PROFILES`: line 246; invented
  `resource_permille` values are at 253/264/275/286/297 and applied by
  `_ai_resource_handicap` at 26438.
- Team plan setup: `_team_ai_production_plan` writes at 1026 and 1062; the
  manifest's `ai_production_plan` is read at 1045; accessor at 1212.
- Pack-manifest validation and fallback: `ai_production_plan` is admitted at
  2459 and falls back to `AI_PRODUCTION_PLAN` at 2473–2481.
- The wave bot consumes plans and directly queues/mutates gameplay at 31527+;
  another plan consumer begins at 31851.
- Lockstep command admission is `submit_command` at 32966.

Phase 2 should surface `pack.files.skirmishAi` / `ai/skirmish.json` alongside
the other mounted manifest documents, then replace the linear plan and invented
difficulty profile with runtime interpretation of:

1. `ArmyDefinition` phase durations, authored phase percentages, economy,
   upgrades, heroes, offensive buildings, and tactical target rows.
2. `CombatChainDefinition` target types and priority modifiers, including
   authored `-1.0` never-acquire sentinels.
3. `DifficultyTuning` authored probabilities/toggles.
4. `BrutalDifficultyCheats` as the **only** authored cheat (15% build cost and
   15% build time reductions). Delete the invented `resource_permille` cheats.

AI construction, production, upgrade, movement, and attack decisions must
become future-tick commands submitted through `submit_command`, not direct
calls to `queue_unit` or direct entity/route mutation. The compiled document is
raw input; all selection, scoring, phase progression, random sampling, and
difficulty interpretation stays in the deterministic lockstep runtime.

## Verification

- Failing-first proof: initial collection failed with
  `ModuleNotFoundError: openbfme_importer.skirmish_ai_compiler`.
- Focused Q83 tests: `8 passed`.
- Q83 plus faction compose regressions: `49 passed`.
- Python compile check: PASS.
- Full sequential importer suite: RED after `1:02:44` with `10 failed, 3972
  passed, 24 skipped, 0 errors, 982 subtests passed`. The six Q6 names are
  unchanged:
  - `test_effective_bfme2_neutral_recipe_blockers_are_closed`
  - `test_official_corpus_is_72_maps_split_22_skirmish_50_wotr`
  - `test_effective_official_skirmish_inheritance_teams_are_attested`
  - `test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure`
  - `test_committed_census_dates_itself_and_is_current`
  - `test_committed_census_decode_corpus_figures_are_dated_fresh`
- Four additional current-tree failures are outside Q83:
  - Module-census freshness x2. The live added-consumer diff is
    `castle_ai_bases`, `corpus_warm_cache`, `faction_census_cache`,
    `faction_coverage_cache`, and `faction_plan_cache`;
    `skirmish_ai_compiler` is not in it.
  - `test_exact_rotwk_static_construction_w3d_contracts`.
  - `test_neutral_structure_evidence_is_wired_at_exact_selection_and_floors`:
    concurrent Q88 changed the live selection while the test still pins the
    former selection SHA.
- `tools/check_pack_addresses.py`: PASS, `packs=200 roots=2`.
- `tools/gate-hygiene.ps1`: RED, `count=24`. All findings are outside Q83:
  unallowlisted shared root files `CLAUDE.md`/`PLAN.md` and 22 old
  machine-absolute paths under `workspace/orchestration/fable-wave/...`.
  The initial launch was blocked by local execution policy; the measured run
  used process-scoped `-ExecutionPolicy Bypass`.

Full-suite log: `workspace/logs/ai-p1-importer-full.txt`.
