# UI PARITY lane report — worktree kimi-ui-parity (branch worktree-kimi-ui-parity)

Date: 2026-08-19. Brief: `UI-PARITY-BRIEF.md` (7 items). Content:
`OPENBFME_CONTENT=C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs`
(active pack `rotwk-men-vslice/f177d1bd…`, supplemental `bfme2-men-vslice/7de517bf…`).
Retail INI oracle: `workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini`.
Godot: `C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe`, sequential only.

The owner judges by eye, so every item below ends at a PNG in `ui-frames/` shot from the
REAL slice on the REAL Men pack (`game/tests/ui_parity_capture_runner.gd`, new this lane —
a camera, it asserts nothing). Before/after pairs are kept for the diff.

## Per item: reference → change → evidence

### 1. Radar/minimap gaps

- **Reference:** `reference/in game ui.jpg` (REF-52) and every [hud] capture: at match start
  the radar is a COMPLETE parchment disc with the full map ink; the key/evenstar/flag orbs
  sit on the metal frame arc above it. Ours (`reference/owner-playtest-2026-08-18/ours-fortress-radial-heroes-green-arc-radar-gaps.png`,
  `ours-hud-v024-marked-offsets.png`): the disc is opaque black except the explored wedge.
- **Change:** `game/src/retail_slice/retail_minimap.gd` — removed the shroud-darkening pass
  from `_draw` (and the now-dead `_draw_shroud`). The black was the fog of war's radar layer
  (`alpha = 255 - visibility`, `retail_fog_of_war.gd:869`) painted over the parchment; retail
  never blacks out the map. The shroud still gates which BLIPS render (units need clear
  ground, structures explored) — only the terrain darkening is gone.
- **Evidence:** `ui-frames/01-hud-idle.png` (after) vs `ui-frames/before/01-hud-idle.png`.
  Runner pin: `retail_radial_layout_runner` `radar_draws_no_shroud_darkening` +
  `radar_disc_geometry_closes_the_full_circle`; `retail_four_unit_hud_runner`
  `radar_draws_no_shroud_darkening`.

### 2. Structure world radial size/radius

- **Reference:** RETAIL-barracks-radial-icons.jpg / REF-25/33/35: ~48-stage-unit buttons
  (`InGameRadialMenuStage.apt` bttnFrame) in a tight ring hugging the building.
- **Change:** none needed in code — the constants were already the authored/measured ones
  (`retail_hud_apt_runtime.gd:214` RADIAL_BUTTON_STAGE_SIZE = 48;
  `retail_hud.gd:193` WORLD_RADIAL_STAGE_RADIUS = 39, the owner's capture measurement).
  The oversized floating ring in the v0.2.4 screenshot was the pre-paging hero roster;
  the paging lane fixed that before this lane. This lane PINS the geometry so it cannot
  drift back.
- **Evidence:** `ui-frames/02-fortress-radial.png` — four small buttons hugging the fortress.
  Runner pins: `retail_radial_layout_runner` `world_radial_button_size_is_the_authored_48_stage_units`,
  `world_radial_radius_is_the_retail_capture_measurement`; `retail_four_unit_hud_runner`
  `world_radial_button_size_is_authored`.

### 3. Green arc around the selected fortress

- **Reference:** REF-25/33/35 — a selected structure shows its health bar and nothing else;
  retail draws no arc. Ours: bright green torus (`retail_structure.gd` `SelectionRing`,
  albedo `67e48b`) circling the footprint.
- **Change:** `game/src/retail_slice/retail_structure.gd` — deleted the `SelectionRing`
  torus node outright (creation in `_build_markers`, the three visibility sites, the
  geometry sync). Selection now reads through the health bar alone, as retail.
- **Evidence:** `ui-frames/02-fortress-radial.png` — no arc, green health bar above the keep.
  Runner pin: `structure_radial_command_set_runner` `selected_structure_has_no_selection_arc_node`.

### 4. Hover tooltips everywhere

- **Reference:** RETAIL-barracks-radial-hover-tooltip.jpg / REF-25/32/33/35: parchment-style
  box — gold title, Cost with treasure icon, Command Points, Shortcut, description.
- **Change:** `game/src/retail_slice/retail_hud.gd` `_sync_world_radial` — the in-world ring
  buttons now mirror their palantir twin's tooltip metadata (`tooltip_group`,
  `tooltip_unit_id`, fallback label/desc, cost, refund, command points, `retail_label`) and
  register the hover path (`_register_button_tooltip`). The retail panel
  (`retail_tooltip.gd`) and every other surface (palantir sockets, hero bar, unit actions,
  spellbook powers, orbs, side build bar) already had it.
- **Evidence:** `ui-frames/04-tooltip.png` (palantir socket: "Builder / Cost: 500 /
  Shortcut: L / Train a Builder…"), `ui-frames/05-world-radial-tooltip.png` (same box from
  the in-world ring). Runner pins: `structure_radial_command_set_runner`
  `world_radial_buttons_carry_retail_tooltips`, `retail_radial_layout_runner`
  `world_ring_buttons_mirror_the_retail_tooltip_metadata`, `retail_four_unit_hud_runner`
  `world_ring_buttons_carry_retail_tooltips`.

### 5. "Demolish Building" button on structures

- **Reference:** commandbutton.ini:3554 `Command_Sell` (TextLabel CONTROLBAR:SellBuilding,
  ButtonImage BCSell, InPalantir Yes).
- **Finding:** the brief says retail's label is "Sell". The authored data on THIS project
  disagrees: RotWK effective `lotr.str:14228` CONTROLBAR:SellBuilding = **"Demolish Building"**,
  ToolTipSellBuilding = "Demolish" (verified in the oracle cache AND in the mounted pack
  `bfme2-men-vslice/7de517bf…/data/strings.json`). The BCSell icon ships in the active pack
  (`bcsell-ed33ac54.png`). So the wording was never invented — it is the retail RotWK string,
  and the runtime already resolves label + tooltip + icon from the pack
  (`retail_hud.gd` `retail_sell_command`, commandbutton.ini:3554 cited).
  Inventing "Sell" would violate the retail-authored-data rule, so NO wording was changed.
- **Change:** none (verified correct). Existing pin `structure_radial_command_set_runner`
  `farm_sell_tooltip_is_demolish_with_refund` proves title/desc/refund come from the pack.
- **Evidence:** `ui-frames/02-fortress-radial.png` (sell socket bottom of the wheel, BCSell
  icon); runner section 5 output.

### 6. Fortress vs expansion plots

- **Reference:** commandset.ini:4055-4082 `MenFortressCommandSet` = porter, SelectRevivables,
  boiling oil, ivory tower, SelectUpgrades, Command_Sell — NO expansion commands. The
  expansion/side-building commands belong to `MenFortressExpansionPad{Corner,Side}CommandSet`
  (commandset.ini:4091-4101), which the player clicks on the plot. REF-33's expansion radial
  floats over the clicked plot; the plots themselves are flat decal pads flush with the ground.
- **Change:** `game/src/retail_slice/retail_vertical_slice.gd` `_sync_radial_commands` —
  deleted the block that appended every expansion to the FORTRESS's own radial. The fortress
  main page is now exactly porter + sell + the two page selectors. Plots keep their authored
  sets through the existing pad-click branch. The green ring was item 3's torus (pads are
  structures); the plot plates themselves already sit at terrain height
  (`_presentation_height`), verified flat in the capture.
- **Evidence:** `ui-frames/02-fortress-radial.png` (four buttons: porter, upgrades, heroes,
  sell) and `ui-frames/01-hud-idle.png` (plot plates flush, no rings). Runner pins:
  `structure_radial_command_set_runner` `fortress_radial_has_no_expansion_entries`,
  `fortress_main_page_is_exactly_the_authored_set`, `expansion_plots_show_no_ring_when_unclicked`.
- **Named gap (pre-existing, sim-side, not this lane):** authored slots 3/4
  (Command_FireWeaponMenFortressBoilingOil, Command_SpecialAbilityIvoryTowerVision) have no
  sim behavior, so those two sockets stay empty. That is a sim closure item, not UI layout.

### 7. Fortress hero list incl. Create-a-Hero

- **Reference:** commandset.ini:4069-4076 hero menu (ring hero, CreateAHeroReviveSlot,
  GenericReviveSlot1-7); REF-35 hero-roster radial.
- **Finding:** the roster path was already authored-complete — the defect class was
  environmental. The fortress hero page offers every hero the roster fields: all seven Men
  faction heroes (Aragorn, Boromir, Faramir, Gandalf, Théoden, Éomer, Éowyn) plus every
  Create-a-Hero profile the local store admits — the final capture shows 7 + 1 CAH + back,
  eight portraits exactly like REF-35; an earlier capture the same hour showed both local
  CAH profiles (9 + back). The page follows the roster as the store changes. Headless
  runners get an empty scratch profile store BY DESIGN (`cah_heroes.gd:65`
  PROFILE_DIR_HEADLESS_SUFFIX), so a headless box can never see the player's heroes —
  which is exactly how "my custom hero is not available" reproduces in test but not in game.
- **Change:** none in game code. New pins hold the page == roster contract:
  `structure_radial_command_set_runner` `fortress_heroes_page_is_authored_complete`
  (page ids == production hero ids, ≥ 7 faction heroes) and
  `fortress_heroes_page_includes_every_offered_create_a_hero`. The seeded-profile proof
  already lives in `fortress_command_surface_runner` section 6.
- **Evidence:** `ui-frames/03-fortress-heroes-page.png` (9 hero portraits + back, hugging
  the keep like REF-35).

## Captures (windowed, real slice, real Men pack)

`game/tests/ui_parity_capture_runner.gd` (new). Frames in `ui-frames/`:

- `01-hud-idle.png` — HUD idle, complete radar disc
- `02-fortress-radial.png` — fortress selected: authored wheel, no green arc, world ring
- `03-fortress-heroes-page.png` — revivables page (7 faction heroes + 1 CAH + back, REF-35 shape)
- `04-tooltip.png` — retail tooltip from a palantir socket
- `05-world-radial-tooltip.png` — retail tooltip from the in-world ring
- `before/` — the same four frames pre-fix, for the diff

## Runner table

| Runner | Result | Notes |
|---|---|---|
| retail_spellbook_runner | 218/0 | baseline held; 0 SCRIPT ERROR |
| slice_start_roster_presentation_runner | 22/0 | baseline held; 0 SCRIPT ERROR |
| boot_startup_runner | exit 0 | 0 SCRIPT ERROR |
| retail_state_pin_runner | OK — hash `0e4bcdbf7e9a8579…` matches the pinned value | presentation-only change confirmed |
| retail_radial_layout_runner | 48/0 | was 41; +7 this lane (radar disc, world-ring geometry + tooltips) |
| retail_four_unit_hud_runner | 121/0 | was 118; +3 this lane; gate-m2-focused.ps1 re-pinned 118→121 with dated comment |
| structure_radial_command_set_runner | 29/3 | +7 this lane, all passing; the 3 failures are pre-existing (below) |
| fortress_plot_presentation_runner | 22/0 | unaffected by the SelectionRing removal (chrome-name list only) |

Pre-existing failures (reproduced at HEAD before my edits, log
`logs/run-structure-radial-BASELINE.txt`: 22/3): `farm_built`,
`porter_palantir_unchanged_stop_and_stance` (["stop"]), and the liveness count that follows
from them. My run of the same runner: the same two failures plus the liveness cascade —
no new red. Not caused by, and not fixed by, this lane.

Zero `SCRIPT ERROR` in every run (grep counts in the runner table logs).

## Commits

TBD — `git log --oneline main..HEAD` at finish.
