# open-bfme — RotWK 2.01 → Godot port

## Convert retail assets into content packs {#importer}
tech: importer/openbfme_importer (186 py modules, ~209k lines); playable_unit_compiler.py, module_contracts.py
files: [importer/**, contracts/**]
- [x] Cook all 7 RotWK factions + 74-map pack from pure 2.01 oracle {#importer-factions}
  by: prior lanes (queue Q14/Q44/Q70; maps digest 459a4dec)
- [x] Deterministic publish/selection with digests + gates {#importer-publish}
  by: prior lanes (tools/gate-retail.ps1, check_pack_addresses.py PASS packs=200)
- [ ] New-module rule: packs carry raw authored fields, runtime owns all interpretation — behavior changes never need a recook {#importer-facts-only}
  from: roadmap (queue Q82; owner ratified 2026-08-25)
- [ ] Collapse 6 content sources to 2 — explicit selection or explicit env, everything else refuses loudly; delete the durable-mirror two-phase machinery {#importer-two-sources}
  from: roadmap (queue Q86)
- [x] Prune packs to one good set — 11 bundles / 29.96 GB freed, all proofs green, standing policy + hardened scanner shipped {#importer-prune}
  by: sol (lane sol-q88) + claude (round-2 fixes)
  from: roadmap (queue Q88; two verifier rounds — round 1 stopped a wrong 30 GB deletion, round 2 disclosed 2 contained python-pinned losses)

## Run the game simulation {#sim}
tech: game/src/retail_slice/retail_slice_sim.gd (conductor, shrinking) + retail_sim_*.gd subsystem modules (14 so far); state stays on the sim, modules hold the logic
files: [game/src/retail_slice/**]
needs: importer
- [x] Skirmish sim playable on all factions/maps, lockstep MP, castle sieges {#sim-skirmish}
  by: shipped v0.2.12 (queue Q71-Q76; dist/v0.2.9+)
- [x] Delete the hand-typed fallback rule tables — missing pack field fails loudly by name {#sim-single-truth}
  by: claude (took over after 3 failed sol rounds; setup refuses-and-seeds-nothing, 4 runtime fallbacks deleted, 45 fixtures given explicit synthetic manifests, 3 pins consciously re-minted with zero-behavior-change proof; shipped 42c20c13)
  from: roadmap (queue Q80; owner ratified)
- [~] Split the 33k-line sim into named class files, one subsystem at a time, state-pin equality proving each move {#sim-split}
  by: claude (14 drawers done: projectiles, economy, experience, ai, combat, persistence, production, movement, abilities, upgrades, powers, bases, module-contracts, transport/garrison; sim 31,351 → ~18,200 lines, every move pin-proven; drawer 14 also surfaced+fixed 3 latent typed-array boundary bugs from drawer 13)
  from: roadmap (queue Q81; owner: highest cleanability/moddability impact; owner bar: tiny per module)
- [ ] Module framework + traffic-ranked burn-down of the 99 missing retail behavior modules {#sim-modules}
  from: roadmap (queue Q82; oracles: ZH GPL source, decomp symbols.csv, INI clean-sheet for the 9 BFME2-only)
- [~] Port retail's skirmish AI — personalities, build lists, attack priorities; difficulty = behavior, not cheats {#sim-ai}
  by: codex (lane codex-q83, Phase 1: compile the authored AI data)
  from: roadmap (queue Q83; owner: #1 UX lever)
- [ ] Grow map-script vocabulary opportunistically (135/609 actions handled) {#sim-scripts}
  from: roadmap (queue Q85)
- [ ] Retire the hand-authored synthetic pack game/data/base (after fallback tables die) {#sim-retire-synthetic}
  from: roadmap (queue Q87; blocked on Q80)

## Draw menus, HUD, and shell {#shell}
tech: game/src/ui (~29k lines), retail_hud.gd (6.2k), retail_hud_apt_runtime.gd (APT-authored layout)
files: [game/src/ui/**, game/src/retail_slice/retail_hud*.gd]
needs: sim
- [x] Retail-derived HUD/radar/radial + skirmish setup + Create-a-Hero loop {#shell-hud}
  by: prior lanes (queue Q66/Q67/Q72/Q73, HUD runner 147/0)
- [ ] Save/load browser — sim already serializes; wire the shell surface {#shell-saveload}
  from: roadmap (queue Q84; main_menu.gd:191 hard-disabled today)
- [ ] UI parity: render EA's authored APT layouts everywhere, hand-measured constants are bugs; screenshot-vs-reference/ gates per screen; recover ~760 HUD text labels {#shell-ui-parity}
  from: roadmap (queue Q90; oracle = owner's retail capture library in reference/)

## Cook, gate, and ship builds {#pipeline-tools}
tech: tools/ (40 scripts), gate-retail.ps1, publish-durable-pack.ps1, dist/v0.2.12
files: [tools/**, dist/**, orchestration/**, .github/**]
links: importer
- [x] Byte-reproducible builds, durable-pack verify, determinism pins {#pipeline-gates}
  by: prior lanes (queue Q57/Q58; repeat cook 9s)
- [~] Trust week: CI python pin fixed (3.12.10→3.12.13, was killing the importer suite at collection for weeks); remaining: CI skip ceiling, pack-digest first line of every report {#pipeline-trust}
  by: claude
  tech: queue Q78; ci.yml:95
- [ ] Orphan-runner triage: 170/393 runners invoked by nothing — wire into a gate or delete; correct AGENTS.md "~35" {#pipeline-orphans}
  from: roadmap (queue Q79)

## Later game modes {#modes}
tech: game/src/wotr (~28k lines, launches with presentation); campaigns not started
files: [game/src/wotr/**]
needs: sim, shell
- [ ] Campaigns (good/evil/Angmar) — explicitly OUT of playable-release scope; gated on script vocabulary {#modes-campaign}
  from: roadmap (DIRECTION.md ladder step 12)
- [ ] War of the Ring full parity audit {#modes-wotr}
  from: roadmap (parity-ledger.md)

## Deterministic C# sim (parked) {#csharp-sim}
tech: engine/OpenBfme.Sim (5.9k lines, Fixed64) — frozen, not deleted; GDScript lockstep ships with cross-OS hash-identical CI proof
files: [engine/**]
links: sim
- [ ] Mark engine/ frozen (README + no new lanes); dual-run trace runner stays as the doorway back {#csharp-park}
  from: roadmap (queue Q89; owner chose park over delete 2026-08-25)

## Windows launcher and updater {#launcher}
tech: launcher/OpenBFME.Launcher (C#/WPF, 8.9k lines); provisioner + release feed
files: [launcher/**]
links: pipeline-tools
- [x] v0.2 release path: Build-PlayableBundle -Release -Zip {#launcher-release}
  by: prior lanes (launcher v0.2 release audit)

## Audit: where are the cracks {#audit}
tech: 4 parallel read-only auditors + owner-facing PM report; 2026-08-25 session
files: [PLAN.md]
links: importer, sim, shell, pipeline-tools
- [x] Architecture audit — god file, dual engines, four rule copies, fallback-wins verified in code {#audit-arch}
  by: claude
- [x] Fidelity-vs-retail audit — 99/178 sim-class retail modules absent, campaigns/cinematics zero, AI is a stand-in {#audit-fidelity}
  by: claude
- [x] Test-honesty audit — CI importer suite dead for weeks (verified), 170/393 runners wired to nothing {#audit-tests}
  by: claude
- [x] Asset-pipeline fragility audit — 6 silent content sources, recook-coupled iteration {#audit-pipeline}
  by: claude
- [x] Plain-language PM report with ranked cracks + decision list {#audit-report}
  by: claude
- [x] Inventory Open-BFME-1 decompile — usable code is EA's official ZH GPL source; BFME-unique parts are stubs/asm; 9 gap modules are BFME2-only {#audit-decomp}
  by: claude
- [x] Owner ratified the six-crack program; filed as queue Q78-Q90 {#audit-ratified}
  by: claude

## decisions
- 2026-08-25 Q83 RAW-FIELDS RULE (owner ratified): `ai/skirmish.json` preserves authored values verbatim with source provenance. The importer must not derive or pre-bake runtime decisions; Phase 2 interprets ArmyDefinition phases, CombatChain target priorities, and DifficultyTuning in the lockstep sim. `BrutalDifficultyCheats` is the only authored cheat.
- 2026-08-25 OWNER RATIFICATION (the six-crack program): fix all four Crack-1 items (traffic ranking, module framework, per-module oracles, lane discipline); delete fallback tables; strangler-fig the sim into class files; PARK the C# engine (not delete — owner corrected after learning GDScript lockstep already ships hash-identical cross-OS); retire synthetic pack after fallback deletion; trust week WITHOUT a nightly self-hosted runner; ONE good pack set + prune (disk pressure); AI port, save/load, script vocabulary all greenlit; facts-not-decisions pack rule + 6→2 content sources; UI gates use the owner's retail capture library in reference/. Filed as queue Q78-Q90.
- 2026-08-25: release criterion ratified in spirit — a stranger plays three skirmishes as three factions vs the AI and can't name what's missing. Campaigns/cinematics explicitly out of playable-release scope.
- 2026-08-25: owner verified as admin of Open-BFME/Open-BFME-1 (gh: Ancalgonn, org+repo admin); copying/translating from it is in-house and owner-approved. STILL OPEN: license posture for Zero-Hour-GPL-derived translations in this public-domain repo — (A) relicense runtime GPL-3 vs (B) ZH read-to-understand only. Becomes load-bearing at the first Q82/Q83 lane that ports ZH code.
- 2026-08-25 backfill: statuses verified against code/queue evidence, not README prose. parity-ledger.md and orchestration/queue.md are the live truth sources this map compresses.
- Component count held at 8: tests (~284k lines) are owned inside each component, not a component themselves.
