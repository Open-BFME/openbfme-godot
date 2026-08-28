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
- [x] Collapse 6 content sources to 2 — explicit selection or explicit env, everything else refuses loudly; durable-mirror machinery deleted {#importer-two-sources}
  by: claude (ambient mounting retired with named reporting; broken workspace fails closed; durable = installs only; mirror detection deleted from the importer; runner 14/0, tests 52/0; 60a26c9c)
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
- [x] Split the 33k-line sim into named class files, one subsystem at a time, state-pin equality proving each move {#sim-split}
  by: claude (22 drawers: sim 31,351 → ~7,000-line conductor + 31 modules, all pushed to origin/main; every move pin-proven; carve surfaced+fixed 4 latent bugs of its own making and attributed 8 pre-existing reds to Q94-Q98; function census: 0 lost of 1,128, 5 documented duplicates)
  from: roadmap (queue Q81; owner: highest cleanability/moddability impact)
- [x] Split the two oversized modules into sub-families + give all 36 modules a real base class {#sim-split-followup}
  by: claude (powers → 3 files, contracts → 3 files, largest module now 2.5k; retail_sim_subsystem.gd base owns weakref/sim-getter/emit_event; deterministic tick stays the event dispatcher for lockstep; 12/12 battery, pins exact; 1c77d105)
  from: agent
- [ ] Module framework + traffic-ranked burn-down of the 99 missing retail behavior modules {#sim-modules}
  from: roadmap (queue Q82; oracles: ZH GPL source, decomp symbols.csv, INI clean-sheet for the 9 BFME2-only)
- [x] Port retail's skirmish AI — authored army composition drives production by default {#sim-ai}
  by: claude (codex compiled the data; claude shipped the pack, loader, interpreter, and consumption: retail ArmyDefinition composition layers over the proven baseline, never weaker, m2 3/3 both ways; 3442dbeb. Q83c holds the rest: difficulty odds, hero build order, full replacement)
  from: roadmap (queue Q83/Q83b; owner: #1 UX lever)
- [x] Stop drawing retail's hidden placeholder meshes — the flat gray tower tops are gone {#sim-w3d-hidden}
  by: claude (owner bug 2026-08-26 "trebuchets not on the walls": the gray octagon caps were W3D HIDDEN pad/ramp meshes (P1/R1/R2) the converter preserved in GLB extras but the runtime rendered anyway; AssetFactory now honors the flag at every GLB load site, w3d_hidden_mesh_runner 23/0. The trebuchet itself is a pack gap, NOT a runtime bug: retail mounts MenTrebuchetFortress (model GUFSgTreb_SKN, trebuchet.ini:854 ChildObject) on the tower deck and no pack has ever converted that model — pinned by retail_expansion_turret_mount_runner as EXPECTED_MISSING_TURRET_VISUALS; needs an importer recook of GUFSgTreb_SKN from w3d.big into the men pack)
  from: agent
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
- [x] Save/load browser — pause-menu save + shell LOAD GAME with byte-identical restore {#shell-saveload}
  by: claude (user://saves JSON with verbatim launch keys + full snapshot; named refusals for MP/cross-pack; round-trip runner 8/0 proves identical restore + deterministic advance; 6bebea1e)
  from: roadmap (queue Q84; retail-parity browser chrome rides the Q90 lane)
- [x] Fortress fire burns again — retail's authored additive W3D materials survive the GLB import {#shell-w3d-additive}
  by: claude
  tech: owner playtest "the fortress also doesnt have the smoke particles coming form it". Two consults (sol + grok) agreed retail authors NO idle ParticleSysBone on MenFortress/MenFortressCitadel — every live row is boiling-oil, construction dust or door dust, and the shipped pack carries all of them. The brazier is MODEL geometry: GBFortress.FLAMES + FIREGLOW, authored src=One/dest=One with the depth mask off. glTF has no additive mode, so Godot imported them as lit alpha-blend and they drew black. game/src/view/w3d_shader_materials.gd now reads the preserved extras.shader at GLB load; runner 20/0, fails 2/2 with the pass disabled.
- [x] Hero portraits are round again, at retail's own size {#shell-hero-cell}
  by: claude
  tech: owner 2026-08-27 "this ui for heros is broken". InGameHeroSelect authors each cell as a CIRCLE (highlight art square about local [28,28], every Hero1..Hero16 PlaceObject matrix [1,0,0,1], 70-unit pitch - InGameHeroSelect.apt offsets 27240..28200). We fed that circle the dock's PER-AXIS stage scale and got a 111x83 ellipse. Retail's own 2560x1440 capture (reference/game.dat_6rULTVkae1.jpg) puts the cell-0 highlight at ~110px = 59 units at the HEIGHT scale, so the cell is 83x83 at 1920x1080 and the portrait centre stays on the dock anchor (800.5, 1023.8) it already matched. New RetailHudStage.hero_cell_scale / hero_scale_size / hero_cell_anchor; four_unit 157->160/0.
- [x] Hero abilities sit in their glass sockets, like every other command {#shell-hero-ability-socket}
  by: claude
  tech: the square blue tile from the same owner screenshot. Hero ability buttons were the ONE command family that still called `_style_button` unconditionally, and they are built before the pack binds so there was no atlas to cut a socket from; the seat was always right (authored socket 1 = 597.7, 806.1). New single-owner `_apply_authored_command_socket_style` now serves hero abilities, doc upgrades and battalion upgrades, and the bind restyles + RESEATS the ability buttons - they were also 141px wide in a 64px socket because Godot clamped size up to their fallback-label text and the bind that clears the text cannot shrink it. NEW TOOL game/tests/hero_command_capture_runner.gd (a microscope: per-button seat, stylebox class, texture, and every Control under a seat's centre) found it. four_unit 160->164/0, was 0/12 socketed before the fix.
- [x] The money bar shows retail's keg, not an invented glyph {#shell-resource-icon}
  by: claude
  tech: parity round 15. `Palantir.apt` places a `ResourceIcon` child (char 127) in `ResourceBar` whose ~Enabled shape is an UNTEXTURED 18x20 quad; the engine fills it from `ResourceBarIcons.tga`. No Object or CommandButton names `Resource_Icon`, so the interface-art lane's object-driven scope dropped it and the HUD drew a Unicode diamond. Added to ENGINE_UI_IMAGE_REFERENCES beside the radar view box; men pack recooked (digest 45a46264) and selection retargeted. A first pass preferred `ResourceBar_<faction>` - RENDERING THEM PROVED THAT WRONG: those five are faction badges (white tree, horse, red eye, white hand, star). One id, no guess.
- [x] Hero selection wears the authored halo; every command seat has one owner {#shell-authored-chrome}
  by: claude
  tech: two independent audits (sol 5.5/10, grok 5/10) on 2026-08-28. Fixed from their lists: the drawn 3px arc replaced by the authored `hero1-selectedhighlight-heroselect` (i28) soft halo, arc kept as the named pre-split fallback; radial commands routed through `_apply_authored_command_socket_style` (they were a fourth copy of the crop logic, which REFUTED the single-owner claim); the no-atlas fallback is now an empty box, never Godot's default panel. Gates: four_unit 167/0, apt_runtime 99/0, radial 48/0, side_command 49/0, minimap 32/0, interface_art 19/19.
  STILL OPEN from the audits, in their priority order: the upper-left rope (`PalantirBack` char 50 flattens artless, zero draws - an imported character the InitialSetup flatten skips); command cups use RETAIL_EMPTY_SOCKET_REGION 56x53 stretched to 64x64 where the authored `glass0..5` is 36.1x36.0 with image 46; orb sizes/z-order vs authored PalantirButtons depth 120; `PlayerFactionIcon` and `ResourceMultiplier` authored and undrawn; hero health arc is a documented stand-in for the authored HealthBar curve; the builder-cycle seat's `*2.2` is owner-requested UI with no authored source; resource typography (authored fontHeight 14 vs our 18/20).
- [~] UI parity: render EA's authored APT layouts everywhere, hand-measured constants are bugs; screenshot-vs-reference/ gates per screen; recover ~760 HUD text labels {#shell-ui-parity}
  by: grok
  from: roadmap (queue Q90; oracle = owner's retail capture library in reference/)
  tech: round 1 shipped 1d232d27 — labels claim was stale (739/764 already recovered by recooks; 25 residuals are importer coverage gaps); per-screen structural gate live (67 pass / 7 honest reds); constants audit ranks ~890 literals into a 5-lane burn-down (workspace/review/q90-hand-measured-constants.md). Owner 2026-08-26: palantir/radar is sloppy vs retail — icons sit outside the dish, idle side-build strip must stay hidden, rope/feather ornament is missing; Fable started stage-piece APT conversion + seat registration, spend-limited mid-lane.

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
- 2026-08-28 WHOLE-PORT SCORE, MEASURED BY TWO INDEPENDENT AUDITS THAT AGREE: grok **3.5/10** whole-product and **5/10** skirmish-only; sol **3.8/10** as a 1:1 reproduction of BFME2+RotWK as complete retail products, and ~5.5/10 scored on the skirmish slice alone - the gap is entire missing MODES, not the depth of what exists. The complete map is `docs/ONE_TO_ONE_GAP_MAP.md`. Biggest single absence: the good and evil CAMPAIGNS (17 authored mission maps sit in the oracle, zero converted), then the shell map behind the main menu, then dock fidelity, then the dropped W3D emissive colour, then the ~8 uncooked UI screen movies.
- 2026-08-28 PARITY SCORE, MEASURED NOT CLAIMED: two independent adversarial audits scored the palantir dock 5.5/10 (sol) and 5/10 (grok) against retail. Both named the same top three: the fabricated currency glyph, the missing upper-left rope, and procedural chrome standing in for authored art. Their lists are the burn-down order for #shell-ui-parity; a claim of parity is only credible when both re-score it.
- 2026-08-27 W3D ADDITIVE/EMISSIVE (two-consult verdict): a W3D material with src=One/dest=One IS additive and must not depth-write. It is NOT unconditionally unlit — `pri_gradient = 1` means WW3D modulates the texel by the lighting gradient first — so the runtime derives unlit from the authored vertex-material ambient instead (FIREGLOW's "LightGrow" authors ambient 0/diffuse 0/emissive 161,15,0; FLAMES authors ambient white and stays lit). NAMED GAP, not fixed here: the pinned Blender plugin writes the W3D emissive into `principled.emission_color` (material_import.py:110) but Blender 4.x defaults emission strength to 0, so the glTF exporter drops `emissiveFactor` — EVERY retail self-lit surface in every pack has lost its colour, which is why the fortress halo reads white instead of fire-orange. Fixing it is a converter change plus a full recook and is its own lane.
- 2026-08-26 HUD oracle (owner playtest + retail captures): palantir/radar compositing uses converted Palantir.apt / palantirexport.big pixels at the movie's own placements. Idle has empty sockets and no side-build strip; the strip appears only for a builder selection. Command icons stay in the six glass sockets (overflow uses the authored subMenu seats after the corner-registration correction). Rope/ornament art is an imported character the static InitialSetup flatten skipped — stagePieces is the named pass, and needs a HUD recook to reach the selected pack.
- 2026-08-25 Q83 RAW-FIELDS RULE (owner ratified): `ai/skirmish.json` preserves authored values verbatim with source provenance. The importer must not derive or pre-bake runtime decisions; Phase 2 interprets ArmyDefinition phases, CombatChain target priorities, and DifficultyTuning in the lockstep sim. `BrutalDifficultyCheats` is the only authored cheat.
- 2026-08-25 OWNER RATIFICATION (the six-crack program): fix all four Crack-1 items (traffic ranking, module framework, per-module oracles, lane discipline); delete fallback tables; strangler-fig the sim into class files; PARK the C# engine (not delete — owner corrected after learning GDScript lockstep already ships hash-identical cross-OS); retire synthetic pack after fallback deletion; trust week WITHOUT a nightly self-hosted runner; ONE good pack set + prune (disk pressure); AI port, save/load, script vocabulary all greenlit; facts-not-decisions pack rule + 6→2 content sources; UI gates use the owner's retail capture library in reference/. Filed as queue Q78-Q90.
- 2026-08-25: release criterion ratified in spirit — a stranger plays three skirmishes as three factions vs the AI and can't name what's missing. Campaigns/cinematics explicitly out of playable-release scope.
- 2026-08-25: owner verified as admin of Open-BFME/Open-BFME-1 (gh: Ancalgonn, org+repo admin); copying/translating from it is in-house and owner-approved. STILL OPEN: license posture for Zero-Hour-GPL-derived translations in this public-domain repo — (A) relicense runtime GPL-3 vs (B) ZH read-to-understand only. Becomes load-bearing at the first Q82/Q83 lane that ports ZH code.
- 2026-08-25 backfill: statuses verified against code/queue evidence, not README prose. parity-ledger.md and orchestration/queue.md are the live truth sources this map compresses.
- Component count held at 8: tests (~284k lines) are owned inside each component, not a component themselves.
