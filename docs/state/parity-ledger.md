> Promoted from `workspace/scratch/PARITY-LEDGER.md` on 2026-08-17 (stage-2a triage); verbatim.

# RotWK 1:1 Feature-Parity Ledger
Owner goal (2026-08-05): 100% feature parity with BFME2 RotWK 2.01 (pure retail; patch 2.02 becomes an optional import later).
Oracle: `workspace/retail-work/editions/rotwk/cache/effective-assets` ONLY. Layered tree = quarantined fan patch.
Every round updates this ledger. Status: DONE (dual-reviewed + verified) / LIVE (shipped, review pending) / FLIGHT (worker active) / QUEUED (task filed) / GAP (named, no owner yet).

## Content & data
- DONE All 7 factions cooked from pure retail, published, MP-mirrored (round 12-13)
- DONE All 72 official skirmish maps load + validate
- DONE Every builder-constructible building / unit / hero producible (deep_production 730/0)
- DONE Retail values pinned with citations (prices, damage, ranges, speeds)
- GAP Special fortress maps (Minas Tirith class) absent from pool — census FLIGHT (opus31); gates/walls systems GAP
- GAP Map display names + WOR leakage — FLIGHT (opus31)
- GAP map_preview/map_art capability blocker on some maps — FLIGHT (opus31)

## Sim / gameplay systems
- DONE Summon spellbook family (counts/lifetimes/expiry retail-exact)
- DONE Taint/reveal/weather/lair-conversion powers (28 powers castable end-to-end)
- DONE Siege pathing: corridors, eviction, tangential slide, wall-range (rounds 17-21)
- DONE Live-vs-replay signature agreement (first time ever; round 21)
- DONE Fortress plots offer faction building sets; CastleUpgrade grant mechanism
- QUEUED (#21) Cavalry mount model swap (conditionalSets/MODEL_CONDITION consumption)
- QUEUED (#23) Gollum + One Ring + ring heroes (Galadriel/Sauron)
- FLIGHT (opus32) Create-a-Hero (census CORRECTED to pure oracle; playable slice in progress)
- GAP Fortress purchase surface partial (needs next cook to ship castleUpgrades rows for all)
- GAP Blocked powers awaiting subsystems: Flood/Avalanche (FloodUpdate), Citadel (CastleBehavior), Barricade (SpawnBehavior), Undermine (MetaImpact), Watcher (grab-devour), Devastation (trees/Ents), Scavenger (kill bounty), Bombard (placement semantics — one retail capture settles), DragonStrike (StrafeAreaUpdate), EnshroudingMist (invisibility emission)
- GAP Inert economy powers need consumers (production_mult: 5 powers)
- GAP Member-level (soldier) attack range model; retail formation movement flip (#16, built + gated off)

## Presentation / UX
- DONE Selection pick radius from model bounds; menu availability cache; per-faction music
- FLIGHT (opus30) Instant menu (manifest list + validate-on-pick)
- FLIGHT (opus33) Menu/shell music hookup; WotR audio; attack/voice sound census; radar palantir art
- QUEUED (#20) Summon T-poses + hero ability anims (spear throw)
- QUEUED (#22) Red attack cursor on hostile right-click
- GAP ~760 recoverable HUD text labels (strings lane specced, not built)
- GAP Summon fade-out visuals (compiler drops SlowDeathBehavior fade)
- GAP Retail 3-page fortress hero revival submenu (heroes only in flat radial)
- QUEUED (#15) WotR presentation bundles into packs (env-independent)

## Modes
- DONE Skirmish SP+MP on all maps/factions; WotR launches with full presentation via run_game.bat
- GAP Campaigns (good/evil), tutorials — not started
- GAP WotR depth vs retail (audio FLIGHT; full parity audit not done)
- GAP Save/load slots, replays surface, key rebinding, stats (product gaps from July audit)

## Infrastructure guarantees (keep green)
- Gates: retail_slice ACCEPTANCE (named failures only), spellbook locked-set + castable exact, deep_production, pytest 2400+, byte-reproducible builds, durable -Verify, both determinism pins
- Owner debt: 3 signature constants need post-cook re-mint (owner-orchestrated); 86-skip audit (#18)
