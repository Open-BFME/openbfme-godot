# Q83 — retail skirmish AI port, Phase 1 (compile the authored AI data)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Claim row Q83 in
orchestration/queue.md (owner=codex-q83).

## Execute the existing detailed brief
The full Phase-1 spec already exists and remains valid:
`orchestration/briefs/ai-phase1-skirmish-ai-compiler-sol.md` — compile
aidata.ini + skirmishaidata.ini into a typed, provenance-carrying pack
document (`openbfme.skirmish-ai`). It was never built (verified 2026-08-25:
no skirmish_ai_compiler.py, no test file). Follow it with these amendments:

1. Row/owner: claim Q83 (the brief's Q18/Q19 rows no longer exist; file the
   Phase-2 row it asks for as Q83b).
2. RAW-FIELDS RULE (owner-ratified 2026-08-25, supersedes any conflict):
   emit authored values verbatim with provenance; NO derived/pre-baked
   decisions in the document. The brief already leans this way — hold the line.
3. Oracle root: use the layered install / pure-2.01 effective-assets paths as
   currently canonical (memory: workspace/retail-work/editions/rotwk/cache/
   effective-assets); re-verify the brief's line anchors against it and note
   drift rather than trusting 2026-08-17 line numbers.
4. Phase-2 seam (report section, no code): today's consumer is the invented
   `AI_DIFFICULTY_PROFILES` (retail_slice_sim.gd ~:247-302, resource_permille
   cheats) and linear `AI_PRODUCTION_PLAN`. Phase 2 replaces these with the
   compiled ArmyDefinition phases + CombatChain target priorities +
   DifficultyTuning, routed through submit_command (lockstep-safe), difficulty
   = authored behavior (BrutalDifficultyCheats is the ONLY authored cheat —
   preserve it as authored, delete our invented ones). Owner calls this the
   #1 UX lever; Phase 2 gets its own brief after your report lands.

## Definition of Done
As in the underlying brief (tests failing-first, full importer suite = 6 Q6
names, no game/ changes, no pins moved), plus: report at
orchestration/reports/q83-ai-phase1.md, queue row Q83 updated with census
numbers (ArmyDefinition 8 RotWK expected), Q83b filed for Phase 2.
