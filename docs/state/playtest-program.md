> Promoted from `workspace/playtest-program.md` on 2026-08-17 (stage-2a triage); absolute machine paths relativized.

# Playtest Program — charter (owner-ratified 2026-08-08)

## Milestone definition (wave 1)
RotWK **pure retail 2.01** baseline. **Single-player skirmish only**: all 7 factions
(incl. Angmar), all official skirmish maps. Launcher works. It is FUN.
Out of scope for wave 1: multiplayer, campaigns, WOTR, Create-a-Hero, retail-import
wizard on tester machines.

## Owner rulings (do not re-litigate)
- 2026-08-08: threat pair (UNIT/TEAM_THREAT_LEVEL) UNBLOCKED on the slice combat-weight
  formula for the playtest era — rescinds the prior "retail-sourced oracles" re-block.
  Divergence documented in wp09/wp21; FOLLOW-UP filed: derive retail threat formula and
  reconcile (parking lot, post-playtest).
- Baseline: pure retail 2.01 (`cache/effective-assets`) — ratified twice (2026-08-04, 2026-08-08).
  Patched 2.02 tree stays quarantined for the future mod-import feature.
- Distribution wave 1: pre-built pack shared PRIVATELY (NAS/direct) to a small circle of
  trusted friends who own the game. No public distribution of converted assets. Wave 2
  (public) requires the launcher import wizard building from each tester's own retail files.
- Every playtest build carries its pack hashes; bug reports without a pack hash are ghosts.

## Lanes
1. **Baseline residue** — fix the 11 red importer tests (root-cause in flight), close the
   SILENT-SHORT-PUBLISH gate, burn down named gaps from the 08-04 rebase
   (~26 non-summon spellbook powers, 34 pinned slice failures, strings lane).
2. **Script-world grind** — wire the AI-blocking world calls, ranked by traffic
   (packet queue being generated). Test-first: every packet ships a failing test first.
3. **Play-smoke harness** — nightly bot: every map × faction rotation, headless,
   watchdogged, repro artifact on stall/crash/invariant break. Its morning report mints
   the day's packets for lanes 2 and 4.
4. **Converter-gap burn-down** — ranked by in-skirmish visibility (untrainable units
   first, cosmetic last).
5. **Launcher (wave-1 scope)** — point at local pack, verify digests, staleness warning,
   launch with correct env, one-click support bundle. NOT the import wizard.
6. **Release pipeline** — dry-run to audience-of-one, fix, weekly cuts. THIRD_PARTY
   attestation before anything leaves this machine.

## Daily cadence
- Morning: harness report (lane 3) triaged into packets; agents dispatched per lane.
- Every packet: strict DoD — failing test first, green gate after, named baseline delta,
  which pack hash it ran against. No packet closes on an agent's own claim (verifier pass).
- Owner plays the current build regularly; taste findings become lane-4/fun packets.
- Weekly: release cut (lane 6) once the dry-run succeeds.

## Day-one work queue (consolidated 2026-08-08 from three audits)

### Lane 1 — baseline residue
- B1 (M): rebuild catalog/rotwk.json as TWO-LAYER pure retail (<retail-install>\BFME2 base +
  <retail-install>\RotWK expansion, official 2.01, no community patch). The 08-05 regen dropped the
  base layer the real engine mounts — that one mistake causes 9 of the 11 red tests.
  Verify layer-prefixed archive names.
- B2 (L): re-extract effective-assets + manifest from the finalized catalog (fixes the
  strategic-APT identity test; may trigger pack re-cook cost).
- B3 (S): regenerate game/data/retail_module_census.json LAST, after B1+B2.

### Lane 2 — script handler packets (world interface is 319/319 DONE; this is wiring)
- DONE 2026-08-08, ALL SIX PACKETS LANDED + verified ACCEPT: P1 wp19 (a812dc8),
  P2 wp20 (cc42ef5), P3 wp21 threat under owner ruling (f9d4ec3), P4 wp22 sciences
  (235131d), P5 wp23 misc verbs (b1577b4), P6 wp24 fog partial (97a251b).
  R5+R6 bundle (0cf65bf), CI fixes (b032c99). B1 catalog rebuild verified ACCEPT
  (full suite 2628/0); B2/B3 STRUCK — moot, their failures were catalog-shape.
- Tickets from verification: wp16_ai_units.gd:266 non-bool ctx["result"] trips bool()
  at script_dispatch.gd:317 (noise + type hazard); wp19 runner leaks 13 ObjectDB
  instances at exit (cosmetic — add _release_facets like wp20).
- Follow-up ticket from P1 verify: pre-existing runtime error — wp16_ai_units.gd:266
  stores non-bool query.value in ctx["result"], trips bool() at script_dispatch.gd:317
  via CREATE_OBJECT; lane still 43/0 but the error print is noise + latent type hazard.
Order: P1 spawn (CREATE_NAMED_ON_TEAM_AT_WAYPOINT, ~576 slots) → P2 game-mode +
proximity conditions (gates every SkirmishGollum init; P1 without P2 spawns nothing) →
P3 threat queries (stale blocked-list; slice threat exists — verify SAGE comparison
semantics; requires one flagged edit to wp09 blocked list) → P4 sciences → P5 misc
verbs → P6 fog unblock (1 tier-1 use, last). Registry contract: each packet ships
exactly two NEW files (handler wpNN + runner), failing-first against the stub,
conditions write ctx.result only, refusal ≠ false.
NEEDS-SIM parking lot: S1 wall upgrades (13 sites), S2 tactical-marker basebuilding,
S3 transport/garrison. OWNER-GATED: victory-screen trio, client-random counter.
- After P1+P2: run retail_map_script_runner.gd over the full cooked corpus and dump
  SageScriptGapLog — that becomes the FIRST true runtime gap census (none exists today).

### Lane 2b — runtime census results (2026-08-08, .private/scratch/runtime-script-gap-census-20260808.md)
- HEADLINE C1 SCOPED (report: .private/scratch/c1-map-scripts-scoping-20260808.md):
  the importer composite lane already cooks map scripts; goal-official-72 was hand-
  assembled WITHOUT them (rotwk_map_cook_corpus.py was a red herring — throwaway
  diagnostic). C1a in flight: in-place pack repair (close_goal_prop_bindings
  precedent) for the 22 qualifying mp maps + durable-mirror refresh guard.
  OPEN SCOPE ITEM for owner: 50 wotr-battle maps are category-gated out of script
  qualification (21 carry authored scripts nothing ships); full re-cook admits 74
  maps (opus35 hazard) — deferred. AI-library closure expansion (C1c) is M/L,
  hardcoded in 5 layers, needs admission rules — deferred, sequence after C2'.
- C2 RECLASSIFIED (diagnosis gate caught a wrong premise): NO BASE_FLAG objects exist
  in any of the 128 retail rotwk maps (byte-verified) — never wire/invent them. Real
  fix = named-object-namespace completeness: install declares the map's namedObjects
  list authoritative; adapter answers retail-FALSE for names absent from the map
  (GPL ScriptEngine sourced, :5256-5278), keeps REFUSING for sim-unmodeled-but-map-
  present names and when no world document exists. C2' implementation in flight.
- C3: maps author PlayerHuman; production binds only Civilian/Neutral/Creeps/Player_N.
- C4: CALL_SUBROUTINE "not found" detail is wrong — all 3,312 are disabled-subroutine
  refusals (retail semantics unsourced); needs diagnostic split + semantics ruling.
- AI library closure: composite emitter ships only ai_initialize+ai_mp_inherit_
  management; the 12 AI libraries carrying wp19/20/21/22 traffic (ai_spell_execution:
  569 scripts) never ship. Proposed packet, needs scoping.
- Static census refreshed: 99.65% dispatchable / 22 blocked (99.24%/47 was stale).
  Static call-site counts ≠ traffic (NAMED_NOT_DESTROYED: 17 sites, #1 by 30x).

### Grok spine session — audit verdict PARTIAL (2026-08-08 late)
- REAL: menu→match→select→move spine reproduces green (one silent-abort flake);
  mutation suite 11/0 (self-test of the validator, not a live check); men
  hostSliceBridge digest verifies; durable in sync; only men republished.
- DEBT-1 RESOLVED (C1b, verified ACCEPT): maps pack re-addressed to a64107f8...,
  swapped in both roots via the FIRST production apply-selection-transaction run
  (worked as designed: staged, verified, all-or-nothing, swaps=2); stale 4c3d1afb
  dirs deleted; durable -Verify now byte-level green; adorn-river installs
  runtimes=3 under the new selection. mp-harlindon still the known 1/22 inert map.
- DEBT-2 (Grok's): bridge pack provenance/manifest.json is a stale byte-copy
  (declares 3,488 files; +2,595 BFME2-derived files undeclared); rotwk-* rename
  also defeats the name-based bfme2-contamination check. Regen manifest, and the
  pure RotWK host cook remains the real fix.
- DEBT-3 (Grok's): 4 tracked game/ files modified off-books (menu backdrops →
  res://data/base/assets/ui/*.png NEW UNTRACKED assets in the synthetic pack we
  planned to unmount; build-id label; options multi-monitor rework) + boot.tscn/
  project.godot. Uncommitted; needs review as its own change before ANY commit
  from game/. my_heroes_screen.gd + retail_slice_sim.gd churn also observed.
- Validator hardening DONE (untracked .private/scratch/agent-loop): spot tier
  default (~10s, manifest-sampled, escalates loudly when manifest absent), deep
  tier flagged (~33s, full bundle_digest recompute), contamination check now
  provenance-aware (bridges REPORTED, unmarked bfme2-content FAILS), self-test
  19/19. Live run caught the men-pack drift (2,597 unmanifested bridge files)
  before DEBT-2 re-addressed it — the check works.
- OWNER RULING 2026-08-09: the 8 AI-generated menu art PNGs committed in 59a04e3
  stay as-is in game/data/base (no relocation/attribution work). Assessed
  high-confidence AI-generated, not retail-derived.
- Small packets from the 59a04e3 review: stage15 version-label oracle update
  (_expected_version_text + blank the boot.tscn literal → 70/0); backdrop pick
  reshuffles every second vs its stable-per-process comment; menu_skirmish_runner
  DEAD GATE since a1d207a (reads undefined FACTIONS_BLOCKED_FROM_PLAY, watchdog
  aborts every run); launcher hygiene: retire edition-agnostic compat overloads.

### Lane 5/6 — release + bundle (wave-1 vehicle = Build-PlayableBundle, NOT launcher)
- R1 (S): generate signing keypair, rotate both public pins (docs/BUILD_AND_RELEASE.md
  ritual), store PEM per docs.
- R2 (S): GitHub environments (release-signing, production-release) + v* tag ruleset
  (without ref_protected the release is a silent build-only no-op) + git tag signing.
- R3 (M): owner decision — stand up openbfme-release-vm runner OR make acceptance
  skippable for the playtest channel (publish is hard-blocked behind it).
- R4 (S): Test-LaunchReleaseReadiness + workflow_dispatch 0.0.0-local dry run; fix the
  pinned-exe-hash / Godot-warning brittleness it surfaces.
- R5 (S): add zip + SHA256SUMS step to Build-PlayableBundle.
- R6 (S): verify-then-play.bat inside the bundle (Test-PlayableBundle -Quick wrapper)
  + "zip logs/ and send" README line. Friend → skirmish <15 min, no phone call.
- Launcher wave-2 backlog (L): offline/local engine source, content-pack digest verify,
  staleness compare, log-export button. NOTE df07112 shipped the bfmeladder retail
  provisioner ENABLED behind a disclosure dialog — owner should consciously ratify that
  posture before any public wave.

## RESOLVED 2026-08-09 01:35 — drift cleared, both roots honest
CaH lane went quiet (0 source writes in 15 min) without cleaning up, so the
workspace men pack was restored from the AUTHENTIC durable copy. The cooked
system.json (256,913 B, from the 00:13 experiment) is preserved at
.private/scratch/cah-drift-preserved-20260809/system.json.cooked-0013 alongside
the published bytes — nothing lost, and it is derived output the compiler can
regenerate anyway (that compiler has since changed, so the cook was likely
stale). Now: PACK_ADDRESS_CHECK PASS packs=18 roots=2; durable -Verify exit 0.
When the CaH compiler work lands, cook to a STAGING dir and republish a new
digest — see the nudge at .private/scratch/NUDGE-cah-lane.md.

## (historical) LIVE HAZARD 2026-08-09 00:13 — published pack mutated in place (CaH lane)
The durable men pack d44afb09... is AUTHENTIC (bytes == name). The WORKSPACE copy
of the same digest drifted to 354c8d01... because ONE file was rewritten 23 min
after publish: data/cah/system.json (80,421 -> 256,913 bytes, 00:13 vs 23:50).
Source: the live Create-a-Hero lane (uncommitted cah_system_compiler.py +
cah_heroes.gd + test_cah_system_compiler.py) cooking straight into a published,
content-addressed pack dir. Packs are immutable by contract — a re-cook must
publish a NEW digest and swap selection, never mutate a published one.
Consequence: workspace-first resolution means the GAME LOADS the drifted copy,
so its pack address is currently a lie; publish-durable-pack -Verify exits 1.
Fix (owner's call, that lane is live): finish the CaH cook, then republish men
as a new digest + apply-selection-transaction swap (C1b/harlindon pattern).
Do NOT hand-revert the workspace file — it is someone's in-flight work.

## Standing rules
- Baselines are owned by files, not memories: importer lane 2617/11 (11 root-cause in
  flight); slice 366/34 vs floor 366; spellbook 295/0; production 730/0; maps 83/0.
- Bare pytest lies (Pillow pin) — sanctioned lanes only.
- Contract edits reseal the policy digest; provenance edits regenerate the manifest.
- Nothing publishes from any tree except effective-assets-derived packs.
