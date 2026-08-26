# Q81 — the 33k-line sim carved into six subsystem modules (+ Q87/Q89)

Owner directive 2026-08-25: Crack 2 to 100%, no subagents — implemented and
verified directly by Claude. Proof logs: workspace/logs/q80-takeover/
(accept-*.txt is the final battery; round*-*.txt are the iteration proofs).

## The six extractions (all pin-proven byte-identical)

| # | module | file | scope |
|---|---|---|---|
| 1 | Projectiles | retail_sim_projectiles.gd | member projectile launch/flight/impact, radius+splash damage |
| 2 | Economy | retail_sim_economy.gd | authoritative economy step, AutoDepositUpdate, capture bonus, income upgrade bonus, AI handicap, scavenger bounty |
| 3 | Experience | retail_sim_experience.gd | spawn XP attach, kill/own-guys-die awards, level-up effects, King's Favor grant |
| 4 | AI controller | retail_sim_ai.gd | full skirmish AI as-is (waves, retreat, base building, castle AI candidates/scan) — the dedicated landing pad for Q83 Phase 2 |
| 5 | Combat core | retail_sim_combat.gd | _apply_damage / _apply_member_damage / _apply_structure_damage / bonus nuggets |
| 6 | Persistence | retail_sim_persistence.gd | authoritative state assembly, canonical hashing, snapshot/restore |

Pattern: state stays on the sim (tests read/write it directly; serialization
unchanged); each module holds a WEAK back-reference (a strong ref formed a
RefCounted cycle that leaked freed sims as zombies — caught by the
script_wiring orphan-refusal contracts, now 111/0); the sim keeps one-line
delegates under the original names so call sites, tick order, and every
external caller are untouched. retail_slice_sim.gd: 31,351 → 29,243 lines
(~2,100 lines of logic moved; delegates remain as the seam).

Extractions #5/#6 were SCRIPTED moves: function text cut verbatim,
compiler-guided loop adds `sim.` prefixes (every unresolved identifier is
named by --check-only), inference demotions where Variant boundaries block
`:=`. Two real boundary classes surfaced and fixed:
- typed-array literals do not convert across the subsystem call boundary
  (Array vs Array[int] at _stamp_order_sequence) — typed locals at 5 sites;
- the prefix regex once contaminated STRING literals (hashed state keys
  became "sim.entities" — all three pins moved; 125 strings repaired, pins
  restored). Lesson recorded: prefix loops must exclude string spans.

## Verification (final battery accept-summary.txt)

Pins byte-exact at their ledgered values (state 2723894…, projectile
626df5fb…, pathing a43f07e4…); lockstep determinism 5/0 + network 37/0 +
eight-peer 26/0; member combat 115/0; castle boot 10/0 / gate 47/0 /
wall 32/0 / fixtures 32/0 / skirmish AI 105/0; stage14_15 31/0 (repaired:
watchdog added, legacy battalion-combat assertions re-pinned to the retail
member model incl. authored 20% fortress armor, wave contract at the
roster's real muster size); script_wiring 111/0; state_boundary 152/0;
build_permissions 30/0; control_api 20/0; knockback 13/0; crush_trample
15/0 (fixture now authors MaxTurnWithoutReform=45 like retail);
formation_movement 34/0; 45 fixture files migrated to explicit synthetic
manifests (the displaced-harness class: rules belong in the harness dict,
setup({}, {}) restored).

## Q87 — synthetic pack retired

game/data/base gameplay data DELETED (abilities/buildings/factions/units/
powers/research/maps + damage_matrix.json + globals.json + pack.json; 151
files). assets/ kept (original OpenBFME menu/HUD art + synthesized SFX,
owner-ruled keep; provenance in THIRD_PARTY.md). With pack.json gone the
res:// ambient content source self-retires (Q86 partial). Post-delete:
legacy-demo units 33→1, factions 4→0; castle boot 10/0; state pin exact.

## Q89 — C# engine frozen

engine/README.md marks OpenBfme.Sim parked: GDScript lockstep ships with
cross-OS hash-identical CI proof; dual-run trace runner stays as the
doorway back.

## Named residue

- Q91 stage11_12 arrival trio (pre-existing, git-stash attribution proof)
- Q92 turn_model 34/10 (pre-existing pack-pin drift, 8f40f2af vs selected)
- retail_scripted_state_pin red (Q12), ai_ladder red (Q30) — pre-existing
- archery_range_level2 needs OPENBFME_RANGER_PROFILE (environmental)
- dualrun_trace watchdog abort — frozen C# engine, needs its binary built
- AI special-power cast cluster + foundation/animal AI attachment remain in
  the sim (next extraction candidates); constants tables remain declared in
  the sim with default_manifest()/tests as sole readers
