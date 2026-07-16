# Private Men building lifecycle runtime

The Men structure presenter now consumes authored lifecycle resources instead
of stretching one construction model and hiding it at zero health. It is a
strict private-pack gate: no retail file is added to the repository, and a
missing, escaping, malformed, or unloadable required resource produces a
visible contract failure rather than procedural masonry.

## Required object contract

Each of the five selected bundle objects is a `structure` whose
`presentation.model` is the true intact model. Its
`presentation.buildingLifecycle` must use this v0 shape:

```json
{
  "schema": "openbfme.building-lifecycle-presentation",
  "schemaVersion": 0,
  "maxHealth": 3000,
  "damagedHealth": 1999,
  "reallyDamagedHealth": 999,
  "paths": {
    "construction": "assets/.../construction.glb",
    "intact": "assets/.../intact.glb",
    "damaged": "assets/.../damaged.glb",
    "reallyDamaged": "assets/.../really-damaged.glb",
    "rubble": "assets/.../rubble.glb",
    "bib": "assets/.../bib.glb"
  },
  "bibDuringConstruction": false,
  "unresolved": []
}
```

The numeric example only illustrates field types. The converter owns the exact
evidenced thresholds for each object. Runtime validation requires strictly
ordered thresholds and requires the entity's `maximum_health` to equal the
contract's `maxHealth`.

Fortress additionally requires all three authored door resources:

```json
{
  "components": {
    "door": {
      "construction": {"path": "assets/.../door-construction.glb"},
      "closed": {"path": "assets/.../door-closed.glb"},
      "rubble": {"path": "assets/.../door-rubble.glb"}
    }
  }
}
```

A source-only or blocked record with a null `path` is useful conversion
evidence, but is not a loadable runtime completion. Every body, bib, and
fortress-door path must be a safe pack-relative `.glb`, resolve inside the
selected pack, exist, and pass GLB header preflight. `presentation.model` must
equal `paths.intact`.

An optional `clips` object binds imported animation names without runtime name
guessing. Supported modes are `none`, `manual-progress`, `loop`, `loop-random`,
and `once`. `none` requires an empty names list. A construction entry must be
`manual-progress`; its declared clip is paused and sought from authoritative
`construction_progress`. Declared intact, really-damaged, and rubble clips are
played on phase entry when present in the authored scene.

## Deterministic state selection

The selector uses this precedence:

1. `construction_progress < 1` selects `construction`.
2. `health <= 0` selects retained `rubble`.
3. `health <= reallyDamagedHealth` selects `reallyDamaged`.
4. `health <= damagedHealth` selects `damaged`.
5. All other values select `intact`.

The bib is shown for completed living phases, hidden with rubble, and hidden
during construction unless `bibDuringConstruction` is true. Fortress door
selection is deliberately limited to construction, closed, and rubble; the
runtime does not invent opening or closing states.

All paths are preflighted before the presenter is accepted, while phase scenes
are instantiated lazily. The intact body is loaded once to establish its AABB.
One uniform scale and one vertical offset derived from that AABB are applied to
the common body/bib/door parent. Individual rubble, bib, door, and construction
models remain at identity scale, so their authored relative proportions are
preserved. Team tinting still happens on per-instance duplicated meshes and
materials through the explicit AssetFactory GLB helper; cached source scenes
remain immutable.

Runtime diagnostics are available through `lifecycle_state()` and the
`building_lifecycle_state` node metadata. They include current phase, active
body/bib/door paths, active declared animation, shared transform, retained
rubble state, and the first contract error.

## Health values and focused gate

The deterministic slice now uses the BFME2 base values requested by the
building contract:

- fortress: 7500
- farm: 2000
- barracks: 3000
- archery range: 3000
- stable: 3000

Run the legal-safe lifecycle gate from the repository root:

```powershell
& C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe `
  --headless --path game `
  --script res://tests/retail_structure_lifecycle_runner.gd
```

The runner covers all five health boundaries, construction precedence and
manual scrubbing, retained rubble, bib and fortress-door rules, exact
simulation maxima, shared intact-derived scaling, and malformed/missing/null
fail-closed behavior using repository-authored fixture nodes. It does not claim
that the private lifecycle GLBs have been converted; selected-pack validation
and rendered original-game comparison remain separate gates.

## Source-proven damage runtime closure

Schema version 1 is the authoritative Men damage route. It preserves the same
five exact object IDs and thresholds, adds explicit `collapsing`, `post-rubble`,
and `post-collapse` phases, and consumes only identifiers declared by the
selected pack. The five Men structures derive only these phase entries from
authoritative simulation facts:

1. construction progress below one selects `construction`;
2. the inclusive damaged threshold selects `damaged`;
3. the inclusive really-damaged threshold selects `really-damaged`;
4. zero health enters `collapsing`.

Zero health does not start a guessed timer. Once collapsing begins, ordinary
simulation sync retains collapsing, rubble, or a terminal phase. Advancing to
rubble or to post-rubble requires an explicit authoritative phase change. The
presenter reports the source timing blocker when automatic advancement is
requested.

For all five Men sources, `POST_RUBBLE` and `POST_COLLAPSE` author `Model=None`.
The runtime therefore hides the body and BIB in these terminal phases; it does
not invent a post-rubble GLB. The declared `SmokeBuildingMediumRubble` route is
retained as an identifier request.

Before a manifested selected-pack v1 structure is accepted, its non-null audio
event IDs, entering/collapse FX-list IDs, and particle-system IDs are checked
against the selected pack's audio definitions and Fords particle binding
document. Unknown IDs fail the structure contract. Known unresolved particle
family selections remain explicit route blockers and produce no fallback
emitter.

`RetailVerticalSlice` listens for phase-entry route requests. Exact audio IDs
are passed to `RetailSliceAudio`; missing events are rejected without replacing
them with a generic sound. FX and particle requests are passed to the Fords
battlefield and recorded. They remain non-rendering while the selected particle
contract reports cross-family precedence gaps or while a dynamic emitter
translation is not implemented.

Run the damage-route gate:

```powershell
& C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe `
  --headless --path game `
  --script res://tests/retail_structure_damage_effects_runner.gd
```

The gate covers all five exact threshold transitions, collapse entry, terminal
no-render states, exact declared audio/FX/particle IDs, unknown-ID rejection,
and the absence of invented collapse/post-rubble timing.

## Blocker delta

Closed by this runtime change:

- The presenter can represent all authored Men damage phases, including
  no-render `POST_RUBBLE` and `POST_COLLAPSE`.
- Exact Men thresholds drive construction, damaged, really-damaged, and
  collapse entry without changing the neutral lifecycle's explicit authority.
- Selected-pack lifecycle route identifiers now have a fail-closed registry
  boundary and deterministic dispatch seam.
- Exact declared structure audio can reach the retail audio router.

Still open and intentionally not guessed:

- The generated Men completion profile currently carries the older v0
  lifecycle. The importer/composer must publish source-proven v1 contracts
  before this closure is active in the private pack.
- Exact collapse-completion and post-rubble transition timing are absent from
  the retail data evidence. Runtime requires authoritative phase events.
- `SmokeBuildingMediumRubble`, `RDTMediumExplosion`,
  `RDTMediumExplosionLight`, and related identifiers retain unresolved
  ParticleSystem-versus-FXParticleSystem precedence.
- Dynamic structure FX/particle emitter translation remains disabled. Requests
  are preserved with exact blockers; no synthetic emitter is created.
- Completed-pack conversion, material/render comparison, effect timing, audio
  timing, and original-game visual proof remain required before 1:1 parity.
