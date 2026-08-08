# Product direction

Stable product target and system ladder. Volatile evidence: `STATUS.md`.
Machine-readable policy: `contracts/rotwk-201-product-scope.json`.

## North star

A modern, moddable RTS engine in Godot that reproduces **Rise of the Witch-king
(RotWK) 2.01** through measured comparison with the original - skirmish and its
complete retail shell/HUD first, then Create-a-Hero, campaigns, and War of the
Ring.

Content is converted locally from a user-owned RotWK install (BFME2 base as the
importer catalog layers it). Retail and converted packs stay under `.private`.
This public tree is **code + fixtures only**.

**BFME2 1.06 alone is not the parity baseline.** Use `--game bfme2` only for
comparison. RotWK is a superset; targeting it once avoids a second full parity
program.

## Development model: systems-first iterative

Vertical-slice freeze (one map / one faction "complete before anything else") is
**not** the active strategy.

Work advances by **major systems**, in short iterations, against **RotWK data**:

1. Pick one primary system (or two tightly coupled systems).
2. Implement or harden it with fail-closed conversion/runtime behavior.
3. Attach a focused automated check.
4. Prove it on **more than one** map and/or faction when the system claims generality.
5. Record evidence in `STATUS.md`; move to the next system.

Progress metrics: reusable factories (cook, closure, bind, pack, sim, script/AI),
coverage/gap burn-down, and "RotWK pack still boots" smoke - **not** "Fords M2
freeze percentage."

Lightweight play spikes (any RotWK map / factions, short session) every few
systems are encouraged so systems stay grounded. They do **not** reimpose a
single-map product freeze.

Hand-authored profile/bindings work that once defined the Men/Fords vertical
slice is historical technique, not the long-term content model. Prefer census ->
closure -> convert -> gap report over multi-day one-map JSON recipes.

## System ladder

Order of **system completion**, not "only this map may exist in the tree."
Later systems may already have partial code; partial presence does not mean the
system is accepted.

1. **RotWK content pipeline** - index, extract, convert, audit, publish, mount;
   default edition RotWK; deterministic packs and provenance.
2. **Map cook IR** - official maps to engine map facts (height, passability,
   water, objects, starts, setup, triggers, scripts packaging path); classify or
   decode remaining chunk families needed for skirmish.
3. **Asset closure and conversion** - object/faction graphs to W3D, textures,
   audio, particles; batch convert with explicit converter-gap accounting.
4. **Object binding** - retail type -> model or logical class; unresolved types
   are a burn-down list, never silent fake art in parity mode.
5. **Navigation and buildability** - generated from cooked maps + footprints;
   multi-map, not single-map special cases.
6. **Simulation core** - production, economy, combat, death, command points,
   and related rules driven from pack data for RotWK factions (including Angmar).
7. **Script and AI surface** - map/AI library packaging plus runtime opcode and
   world coverage growth with measured gaps.
8. **Module / behavior execution** - reduce opaque-deferred module surface for
   behaviors that skirmish actually needs.
9. **Skirmish shell** - faction/map selection, handicap and retail game rules,
   profile-dependent setup, save/replay/observer flows, launch, and end match on
   RotWK content. No retail-reachable setup control may be disabled.
10. **Presentation and HUD/audio** - terrain materials, water, fog, props, retail
    UI/audio paths iterated as systems (not one-map art freezes).
11. **Create-a-Hero authoring and custom maps** - later product-authoring
    completeness. Retail-reachable custom-hero integration in skirmish remains
    part of step 9 even while the authoring screen is deferred.
12. **Campaigns** - Good/Evil (and Angmar campaign content as shipped by RotWK)
    maps, mission scripts, objectives, cinematics.
13. **War of the Ring** - strategic layer, territories, armies, strategic->"tactical
    handoff, SP and MP as designed.
14. **Multiplayer lockstep** - self-hosted local/listen/dedicated, up to eight
    players, after skirmish systems are stable enough.
15. **Modern product layer** - accessibility, HD packs, mod management, safe mode,
    diagnostics, rollback updates without changing the parity profile.
16. **One-button convert-and-play** - thin UX over green systems (last, not first).

The active **iteration objective** (which system is in flight) lives in
[docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md). Volatile evidence lives in
[STATUS.md](STATUS.md).

## Meaning of parity

"1:1 parity profile" means every included capability is discovered from the effective
**RotWK 2.01** source corpus and has the required source, conversion, runtime,
simulation, presentation, oracle, and reliability evidence for the claim being
made (skirmish-complete vs full-game-complete).

INI presence and converted-asset counts are not parity. Unknown, ambiguous,
unsupported, substituted, or unclassified requirements fail closed. The retail
profile does not permit "tasteful" simulation, rules, UI, audio, or visual
improvements as parity; those belong to separately named post-parity profiles.

Angmar and other RotWK-only content are in the skirmish denominator when
claiming RotWK skirmish completeness. BFME2-only evidence cannot substitute.

### Map claim boundary

Keep two map claims mechanically separate:

1. **Retail skirmish parity** covers the 22 maps exposed by RotWK 2.01 as
   skirmish maps and requires retail gameplay, presentation, visual, audio, and
   reliability evidence.
2. **Expanded 72-map skirmish** adds the 50 official War of the Ring battle
   maps through an explicit OpenBFME adaptation contract. Those maps must be
   labelled adaptations in the shell and reports; they cannot count as 1:1
   retail skirmish behavior because retail did not expose them in that mode.

Full campaigns, the strategic War of the Ring layer, and Create-a-Hero remain
separate later claims. Their partial implementation cannot raise the retail
skirmish-complete result.

## Permanent product constraints

- Eight players maximum.
- Godot owns presentation, input, UI, audio, and desktop integration.
- Pure C# owns deterministic authoritative simulation (target architecture).
- The current GDScript simulation is the executable parity/reference
  implementation during the showcase wave. Do not begin a broad C# rewrite
  until canonical command/state traces and per-tick digest replay form an
  executable boundary. A later C# authority must match those traces before it
  replaces any accepted behavior.
- Production simulation targets 30 Hz; presentation remains render-rate independent.
- Multiplayer is server-refereed deterministic lockstep and self-hostable.
- No Steam, ranked-service, or mandatory-account dependency.
- Gameplay and presentation mods are versioned and hashed separately.
- Private parity never silently uses synthetic or generic replacement art.
- Campaigns and War of the Ring are **in product scope**. They sit later on the
  system ladder because they depend on skirmish conversion, factions, heroes,
  and scripting systems.

## Active objective

The active systems-iteration objective is
[docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).
Current evidence and blockers live only in [STATUS.md](STATUS.md).
Machine-readable policy: [contracts/rotwk-201-product-scope.json](contracts/rotwk-201-product-scope.json).

Historical Men/Fords M2 tooling (`run_m2_acceptance.bat`, oracle capture IDs)
may remain in the tree for regression and migration; it is **not** the product
strategy and must not block RotWK systems work.

## Non-goals for the current phase

- Treating BFME2 1.06 freeze as a prerequisite to RotWK work.
- Requiring vertical-slice "complete and freeze" before major systems.
- Dual full-parity programs (finish BFME2, then re-do RotWK).
- New synthetic Stage 1-10 product work as a substitute for retail systems.
- Declaring skirmish or full-game completion without identity-bound evidence.
- One-button convert-and-play UX before systems 1-9 are real enough to support it.
- Public-release automation beyond containment checks until release policy says so.

## Agent and contributor rule of thumb

Do not reject work as out of scope merely because it is not Men/Fords or not
BFME2. Reject work that invents retail behavior, weakens fail-closed gates,
writes retail payloads outside `.private`, or expands synthetic product surface
without integration-owner authority.
