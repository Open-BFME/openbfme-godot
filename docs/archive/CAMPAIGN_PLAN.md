# Campaign lane plan

**Owner:** Jonathan
**Status:** active, started 2026-07-25
**Scope:** single-player campaign missions (BFME2 16 + RotWK Angmar 10) and every
retail map. War of the Ring is explicitly **out** of this lane — it is a separate
strategic/living-world project (19 `livingworld*.ini` files, ~531 KB, none of it
imported) and shares almost nothing with this work.

## Where we start

- Script engine: **done**. Parses, binds world data, executes deterministically.
  Three real missions run 600 ticks with correct objective/briefing order.
  `retail_map_script_runner` 41/0.
- Script coverage: **19.1%** of campaign scripts (1,056 of 5,536) can execute
  every opcode they use. One unsupported condition fails a whole script closed,
  so this script-level figure is the honest one; slot percentages flatter it.
  It has since gone 19.1% -> 35.5% -> 45.9% -> **62.7%** (3,468 of 5,535).
  Of the 24,371 opcode slots the campaign corpus authors, 15,501 are
  gameplay-semantic, 4,171 are `recorded` presentation, and 4,699 are still
  unsupported. The recorded slots are reported apart from the semantic ones and
  are never counted as gameplay coverage.
- Maps: only `fords-of-isen-ii` is cooked. Phase A has since closed the
  planning side — every shipped map in both editions plans, and prop binding is
  automatic — but no maps pack has been cooked yet.
- The blocker is no longer the script VM. It is the simulation.

## Phase A — every map converts

A1. **Done.** Map discovery covers all retail categories, not just `map mp`.
    `classify_map_directory` sorts the shipped `maps/<directory>` names into
    `skirmish`, `wotr-battle`, `campaign`, `cinematic`, `tutorial`, `shell` and
    `system`; `--map-set` selects one category, a named group (`playable`,
    `single-player`) or `all`. `mapcache.ini` already registered every family
    (BFME2 72 rows, RotWK 2.01 122 rows) — only the `map mp` prefix filter hid
    them. `discover_catalog_only_map_targets` additionally picks up the six
    BFME2 map payloads a layered RotWK install ships but whose registry rows
    `_patch201maps.big` replaced. Every catalog row now carries its `category`,
    and `ContentDB.list_catalog_maps` only offers `LOBBY_MAP_CATEGORIES`, so a
    campaign map can never appear in the skirmish list.

    Two blockers were removed to reach 100%: non-lobby maps are parsed and
    cooked with the existing `scenario` SAGE map profile (which is what
    `enforceLobbyStartRules = false` is for — campaign, cinematic, tutorial and
    shell maps ship zero `Player_N_Start` waypoints), and the terrain symbol
    grammar admits `&` for the single retail symbol that uses it,
    `SandLargeType3Rocky&Grassy`.

    Result: **BFME2 67/67 shipped maps and RotWK 128/128 plan cleanly.** The
    only rejections are five BFME2 registry rows whose payload the install does
    not ship.

A2. **Done.** Automatic prop binding lives in `map_prop_bindings.py`, a generic
    replacement keyed off the map itself rather than off Fords. It partitions a
    map's recorded non-road placement types into SAGE logical namespaces
    (`*Waypoints/Waypoint`, `*GenericAIObjects/GenericAIObject` — the only two
    in either corpus), visual targets, and unsafe names, then runs the existing
    static -> hierarchical -> animated planners over the visual targets and
    injects the merged result as `options.objectBindings`. A fully resolved
    Object that authors no geometry at all is declared logical from that
    evidence instead of by hand.

    It reproduces the hand-composed Fords profile exactly: the same 26 logical
    rows and every one of its model bindings, with nothing extra. Across the
    eight BFME2 skirmish maps it binds 301 of 469 types and resolves 92.7% of
    11,444 placements, where the lane previously bound zero on every map but
    Fords.

A3. **Done.** The `stray End` at `object/cinematic/cinematicobjects.ini:1695`
    was not an unbalanced file: line 1695 closes the `System` sub-block of an
    `FXParticleSystem`. `parse_sage_document` advanced one line at a time
    through non-Object top-level families, so their nested `End` tokens leaked
    into the top level. It now skips each foreign family's whole balanced
    region. Two smaller grammar gaps went with it: RotWK's assignment-shaped
    `ThreatBreakdown` block (31 shipped object files) and the `FCurve`-valued
    curve blocks in `object/system/system.ini`. BFME2 object-bearing INI parse
    failures went 42 -> 0; RotWK's remaining ones are all `maps/**/map.ini`,
    which the closure deliberately never reads.

A4. Cook a maps pack and prove the menu lists them with real player counts.
    Only a pack cook can prove this; the profiles are generated and resolve.

## Phase B — the simulation features campaign scripts need

Ordered by how many script slots each unblocks. Each is an engine feature, not
an opcode.

B1. **Named-object registry** — `NAMED_DESTROYED` 462, `NAMED_SET_ATTITUDE` 303,
    `MOVE_NAMED_UNIT_TO` 182, `NAMED_DELETE` 142, `NAMED_DISCOVERED` 108,
    `NAMED_INSIDE_AREA` 82, `NAMED_FOLLOW_WAYPOINTS_EXACT` 145.
B2. **Team system** with attitudes and hunt — `TEAM_HUNT` 377, `TEAM_HAS_UNITS`
    315, `TEAM_SET_ATTITUDE` 293, `TEAM_UPGRADE` 185, `TEAM_MERGE_INTO_TEAM` 180,
    `TEAM_DESTROYED` 157, `MOVE_TEAM_TO` 137.
    B1 and B2 are coupled: scripts move named units between teams constantly.
B3. **Trigger-area spatial predicates** — `SKIRMISH_PLAYER_HAS_UNITS_IN_AREA` 307,
    `PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA(_COMPLETELY_BUILT)` 240,
    `TEAM_INSIDE_AREA_PARTIALLY` 154. Polygon queries against the areas the
    converter already emits.
B4. **Spawn and order actions** — `CREATE_NAMED_ON_TEAM_AT_WAYPOINT` 429,
    `CREATE_REINFORCEMENT_TEAM` 341, `SET_ATTACK_PRIORITY_THING` 231.
B5. **Per-player state** — `PLAYER_SCIENCE_AVAILABILITY` 299,
    `ALLOW_DISALLOW_ONE_BUILDING` 162, player relations, command points. Still
    open; measured at +114 scripts (+2.0 points) once done.
B6. **Audio completion tracking** — **done.** `HAS_FINISHED_AUDIO` 229.
    Retail's condition is not paired with the action that played the sound: on
    its first evaluation it files the named audio event against
    `now + authored length` and answers false, and on a later evaluation past
    that tick it answers true and retires the entry so the next wait re-arms.
    That is what the runtime implements, from an injectable length table
    (`set_audio_event_durations`) and the tick index — never from playback
    state, so the presentation bucket stays a pure sink. An event with no
    authored length reads as zero-length and therefore completes on its first
    evaluation, exactly as retail treats an event the loaded content does not
    carry; every one of those is counted in
    `bounds_hit["audio_event_length_unknown"]`. **No shipped pack carries
    campaign audio lengths yet**, so today every wait completes immediately and
    missions run unpaced-but-not-stalled. Closing that is a pack-data job
    (audio event durations in `data/audio_events.json`), not a runtime one.
B7. **Camera and cinematics** — the camera opcodes are all in the `recorded`
    bucket now (a bounded ordered event each, zero simulation state); what is
    still missing is a presentation layer that consumes those events. Required
    for the 22 RotWK `cin` maps and every mission intro.
B8. **Shroud and discovery** — **done for the permanent family.** Per-player
    shroud is the union of named permanent reveals
    (`MAP_REVEAL_PERMANENTLY_AT_WAYPOINT` / `_IN_TRIGGER`, each filed under the
    handle its `MAP_UNDO_REVEAL_PERMANENTLY_*` sibling removes, plus the
    whole-map `MAP_REVEAL_ALL_PERM`) and the sight of the player's living
    simulation entities, read from the compiled `vision_range`.
    `NAMED_DISCOVERED` and `TEAM_DISCOVERED` answer over it. The
    non-permanent reveal family (`MAP_REVEAL_AT_WAYPOINT`, `MAP_REVEAL_ALL`,
    `MAP_SHROUD_*`, 26 slots) needs an explored-but-not-visible layer that
    decays, which this world has no representation for, and is deliberately
    left unimplemented.

## Phase C — campaign shell

C1. Mission list and selection, mission load path.
C2. Objectives panel driven by the recorded objective events the script runtime
    already emits.
C3. Mid-mission save/load of campaign state.
C4. Campaign-only units, heroes and objects that appear in no faction roster.

## Rules that hold throughout

- Evidence-driven. Everything from the retail corpus; fail closed and record
  unresolved items rather than substituting.
- Deterministic. Script execution is tick-deterministic, bounded, and must never
  draw from engine RNG — the project runs deterministic lockstep.
- Presentation opcodes stay in the `recorded` bucket and change zero simulation
  state, so they can never be miscounted as gameplay coverage. That claim is
  proved, not asserted: `retail_map_script_runner` drives **every** declared
  recorded opcode in one script and asserts the simulation hash still equals a
  scriptless control's.
- Opcodes left unsupported on purpose, because declaring them would be a false
  claim rather than a missing branch: `TEAM_UPGRADE` (185) and
  `SET_ATTACK_PRIORITY_THING` (231) — storing the value changes no unit state;
  `DESELECT` (27) — selection is simulation state, not presentation; the
  `*_FORCE_EMOTION` family (135) — BFME emotions drive fear and terror
  behaviour; `UNIT_SET_MODELCONDITION*` (21) — model conditions can gate weapon
  and armor states; `FREEZE_TIME` / `UNFREEZE_TIME` / `CAMERA_MOD_FREEZE_TIME`
  (12) — they stop the logic clock; and the non-permanent reveal family (26),
  which needs a shroud layer that decays.
