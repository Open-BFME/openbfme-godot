# N-team / multi-faction scoping survey

Read-only survey (2026-07-23) enumerating every 2-team / single-faction
assumption in the retail slice, to sequence multi-faction + up-to-8-player
support. Assessment: **deep rework at the core, bounded at the edges** — ~8-10
genuinely structural sites, the rest mechanical per-team loops. The
single-`faction_manifest`-for-both-teams design is the root of the
same-faction-only blocker (STATUS.md).

## Root cause (item #1)
The sim consumes ONE `_rules["faction_manifest"]` applied to both teams;
`_unit_production_rules`, `_structure_build_rules`, `_spawn_roster`,
`_structure_max_health`, `_structure_armor`, `_ai_production_plan` are single
global tables. `retail_vertical_slice.gd::_resolve_faction_manifest` (~1280)
resolves only from `retail_player_faction`; `_initialize_content_and_match`
(~266) rejects `enemy_faction != player_faction`.

## Sequencing
1. **Per-team faction manifests** — team->manifest map; make the six
   faction-scoped tables team-indexed; resolve a manifest per team. Tallest pole.
2. **Team registry** — explicit `teams` roster (team -> {faction,color,start,
   controller}) from the menu; drive dict init and snapshot arrays from it; add
   an alliance/hostility predicate to replace `enemy = other team` flips.
3. Then (independent, all depend on 1-2): victory = last-team/alliance-standing;
   one AI controller per AI team; per-team spawn geometry from map Player_N_Start;
   menu N-row model bounded by map.playerCount (already parsed); HUD outcome
   keyed off local_team not team 0 (fixes a latent MP-join victory inversion).
4. Deferrable/separable: N-player lockstep (currently 1v1 host0/join1, 1 peer,
   pairwise hash) — orthogonal to single-player N-team-vs-AI.

## Favorable existing machinery
State accessed via `*_for_team(team)` dict helpers; save/restore round-trips
whole dicts; `_step_economy` already iterates per-team; HUD renders one local
team's scalars; `local_team` already exists; maps carry >2 starts + playerCount;
menu already reads playerStarts/playerIndex; game_state.gd has a 3-side
precedent (Side.WILD, enemy_of()).

## Structural sites (need redesign, not loop conversion)
Single manifest (#1); cross-faction rejects (sim ~266, menu ~462); victory
(sim ~7305-7336); AI bound to ENEMY_TEAM (~6987-7250); enemy=other-team flips
(~6127-6641); two-corner L/R spawn geometry (~1610-1686); lockstep 1v1 (whole
file); menu 2 fixed rows (skirmish_setup ~177-199); map consumer capped at 2
starts + 2-point transform (map_data ~630-664).

Full line-level findings table archived in the session report; regenerate with
the read-only survey if needed.
