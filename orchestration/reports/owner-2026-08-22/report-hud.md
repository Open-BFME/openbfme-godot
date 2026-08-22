# Lane HUD — "the hero selection on the bottom left isn't fitted to the graphics"

Owner report + screenshot: the 1920x1080 v0.2.8 capture in the brief.
Brief: `orchestration/briefs/owner-2026-08-22/brief-hud.md`
Branch: `worktree-agent-aacedb376a4f7582f`, head `e136f87c`.
Logs: `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\hud-owner\`

Numbers came from the SELECTED pack set: active `rotwk-men-vslice/b361ec5f...`; the
palantir APT atlases resolve out of the supplemental `bfme2-men-vslice/7de517bf...`
(see `capture-final.log`). Nothing was published; `selection.json` and `VERSION` untouched.

## 1. What the screenshot actually is

The bottom-left surface in the capture is the fortress HERO PAGE, not the main command
page. `workspace/retail-extract/data/ini/commandset.ini:1876-1899` makes
`MenFortressCommandSet` one paged set: `InitialVisible = 6`, slots 1-6 main menu, 7-13
upgrades menu, 14-23 hero menu, each sub-page closed by `Command_RadialBack`. And
`commandbutton.ini:11003-11004` (`Command_SelectRevivablesMenFortress`) reveals the hero
range with `CommandRangeStart 14 / CommandRangeCount 10`. Ten entries.

The capture shows eleven round portraits on a large ring running across the radar globe,
with the command dish left empty and "Level: 1" alone inside it.

## 2. Why — the cause, in code

`game/src/retail_slice/retail_hud.gd _radial_button_position`. For any count over six it
abandoned the authored sockets and synthesised a wheel:

    var radius_x := clampf(131.0 + 8.0 * float(count - 7), 117.0, 168.0)
    var sweep := minf(TAU - min_step, maxf(authored_sweep, min_step * float(count - 1)))

At eleven entries that is a 163 px ellipse whose sweep has grown to nearly 360 degrees,
centred on the dish. Measured in the failing-first run: button 0 landed 174.3 px from the
radar centre against `RETAIL_RADAR_RADIUS = 181`, button 9 at 155.3 px. That is the
owner's symptom exactly. Every constant in that block was invented; its comment said
"measured", not authored.

The bar art was NOT the cause, and the brief's premise about it is wrong — see section 4.

## 3. The retail oracle for where the seats go

`Palantir.apt` (sha256 c629e2b6cdc4daa9d00c40a852c80e303de2415c42dcacfcf92004f44dcaf4c2,
from `workspace/retail-work/editions/rotwk/cache/effective-assets`), sprite character 114
`CommandButtons`, frame 9, dumped with `tools/dump_apt_placements.py --sprites` into
`workspace/logs/hud-owner/palantir-sprites.json`:

| name | translation, local to `CommandButtons` @ stage [289.55, 659.85] | matrix rotation |
|---|---|---|
| `glass0..glass5` | the six known socket locals | none |
| `subMenu0` | (-0.55, -128.55) | 0.00 deg |
| `subMenu1` | (76.0, -101.4) | 35.87 deg |
| `subMenu2` | (118.1, -38.0) | 70.03 deg |
| `subMenu3` | (116.45, 41.9) | 105.49 deg |
| `toggleFlash0..3` | the same ring pattern, scaled 0.62 from `subMenu0` | — |

Polar angles from straight up: 0.245, 36.85, 72.16, 109.79 degrees (mean step 36.51) at
radii 128.551, 126.720, 124.058, 123.752 (mean 125.770 stage units).

SIX GLASS SOCKETS PLUS FOUR SUBMENU SEATS IS EXACTLY THE TEN A HERO RANGE ASKS FOR. The
four `toggleFlash` overlays sitting on the same ring at a constant 0.62 scale are what
rule out "frame 9 is a partial enumeration of the six sockets": they are a four-element
companion set to a four-element seat set. The largest `CommandRangeCount` anywhere in
`commandbutton.ini` is 14 (histogram 5, 7x7, 8x2, 10x5, 11x6, 13, 14x2), i.e. eight ring
seats, which the authored 36.51 degree step reaches without closing the circle onto the
radar.

`InGameRadialMenuStage.apt` (sha256 2485d51800fc...) exports only `RadialButtonShell` and
authors no ring geometry at all, so the world radial's radius stays the measured Q39
constant, correctly labelled as measured.

## 4. The bar art: retail does not ship one

The brief asked for "bar art rendered full-width". It cannot be done, because the texture
does not exist.

* `controlbarscheme.ini:156-165` (Gondor8x6 and its five siblings) authors one
  `ImagePart Position X:0 Y:520 / Size X:1024 Y:248 / ImageName SGCommandBar`, and
  `mappedimages/handcreated/handcreatedmappedimages.ini:1257-1263` maps `SGCommandBar` to
  `SGCommandBar.tga`, 1024x256.
* SGCommandBar.tga IS IN NO ARCHIVE. I enumerated every `.big` in both layers of
  `workspace/retail-work/editions/rotwk/layered-install/` (`layer-0-rotwk`,
  `layer-1-bfme2`) through `openbfme_importer.big.BigArchive` and searched every entry
  name: zero hits on "commandbar", for any of the six side variants. The same scan finds
  `palantir` art fine, so the scan works. BFME2 moved the in-game HUD to APT;
  `controlbarscheme.ini` is carried-forward BFME1 text.
* What IS authored is `PalantirFrame`, placed at stage [0, 512] with an identity matrix.
  Its sheet is `apt_PalantirExport_17.tga` -> `apt-palantirexport-17-fb63d3d26008.png`.

  THAT SHEET IS 512x256 AND THE CODE SAID 384x256, cropping the region to match. Fixed:
  region (0,0,512,256) -> dest (0,0,960,360), the stage rect [0,512]..[512,768] through
  the same transform everything else uses. The 128 discarded columns carry only a faint
  streak (max alpha 17/255 over rows 137-159), which is why the crop was never visible —
  the constant was wrong anyway. The score overlay keeps the inhabited 0..384 columns
  under its own named constant, so its backdrop is unchanged.

The sheet is the double-ring palantir (big radar ring left, command dish right, resource
scroll below), left-anchored. Measured from the shipped PNG: ring holes centred at
(128.00, 120.50) with half-extents (86, 79.5), and (294.50, 148.50) with (57.5, 65.5).
There is no full-width bar behind the buttons in BFME2/RotWK, and the shipped HUD already
drew this sheet.

## 5. The fix

`_radial_button_position`, for a page longer than six:

* seats 0-5 -> the six authored glass sockets (`RETAIL_COMMAND_SLOT_SOURCE`);
* seats 6+ -> the authored subMenu ring, via the new `RetailHudStage.submenu_slot_dock` /
  `submenu_slot_center_dock`;
* ring seats 0-3 are the authored placements VERBATIM; seat 4 and beyond continue the same
  ring at the authored mean step (36.51 deg) and mean radius (125.770 stage units),
  derived from those four rows and from nothing else.

Two consequences, both because authored seats cannot move:

* the production queue chip column moved panel-local 432 -> 445 to clear `subMenu2`, which
  reaches panel-local x 436.3 at a 64px button (that column's own comment already says the
  chips move and the sockets do not);
* three runners carried bounds that `Palantir.apt` itself breaks — see residue item 1.

Not touched, per the coordinator's note: the minimap / radar parchment. The dish portrait,
"Level: N" text and curved level arc were already shipped by Q38; their checks
(`palantir_level_bar_is_an_arc`, `hero_health_is_drawn_as_an_arc`,
`hero_bar_anchors_at_the_authored_hero_select_stage_point`) pass unchanged.

## 6. Files touched

| File | What |
|---|---|
| `game/src/retail_slice/retail_hud_apt_runtime.gd` | `PALANTIR_SUBMENU_SLOT_LOCAL`: the four authored seats plus derivation notes |
| `game/src/retail_slice/retail_hud_stage.gd` | `submenu_slot_local` / `_center_dock` / `_dock`, `submenu_ring_step`, `submenu_ring_radius`, `command_seat_centers_dock` |
| `game/src/retail_slice/retail_hud.gd` | `_radial_button_position` rewritten; `RETAIL_PALANTIR_FRAME_SOURCE_SIZE` 384->512; `RETAIL_FRAME_PIECES` region/dest; new `RETAIL_SCORE_SHELL_REGION`; `RETAIL_QUEUE_CHIP_ORIGIN` 432->445 |
| `game/tests/retail_four_unit_hud_runner.gd` | `_check_paged_command_page_layout` (six checks) plus three control-bar-art checks |
| `game/tests/retail_radial_layout_runner.gd` | bounds derive from the full authored seat set; scaled rows bounded by that set; `_authored_seat_rects` |
| `game/tests/fortress_command_surface_runner.gd` | `_check_radial_is_in_the_palantir_wheel` bounded by the authored seat set; `_authored_seat_bounds` |
| `game/tests/hud_layout_capture_runner.gd` | the camera points at a ten-entry page (the reported surface) instead of four |

Commits: `1913932d` failing-first, `02c204c4` fix, `9f508b91` frame sheet,
`e136f87c` fortress runner bounds.

## 7. Test numbers, before -> after

All runs headless with `OPENBFME_CONTENT=workspace/content-packs`; logs in
`workspace/logs/hud-owner/`.

| Runner | Before | Failing-first | After |
|---|---|---|---|
| `retail_four_unit_hud_runner` | 124 / 0 (`base-hud.log`) | 128 / 3 (`failing-first-hud.log`) | 134 / 0 (`FINAL-retail_four_unit_hud_runner.log`) |
| `retail_radial_layout_runner` | 48 / 0 (`base-radial.log`) | 29 / 19 (`after-radial-1.log`) | 48 / 0 (`FINAL-retail_radial_layout_runner.log`) |
| `boot_startup_runner` | 44 checks / 4 (`base-boot.log`, cold cache) | — | 44 checks / 0 (`FINAL-boot_startup_runner.log`) |
| `fortress_command_surface_runner` (angmar) | 95 / 2 at pre-lane `47c2aa8d` (`PRELANE-...log`) | 91 / 6 | 95 / 2, same two names (`after-fortress-3.log`) |
| `hud_command_feedback_runner` | — | — | 19 / 0 |

The three failing-first names, all now green:

* `paged_page_keeps_the_six_authored_glass_sockets`
* `paged_overflow_uses_the_authored_submenu_ring`
* `no_paged_command_button_sits_on_the_radar_globe`

Other new checks, also green: `submenu_ring_table_is_the_authored_apt_placements`,
`paged_page_builds_one_button_per_authored_slot`,
`first_four_ring_seats_are_the_authored_placements_verbatim`,
`no_paged_command_button_sits_inside_the_dish`,
`control_bar_art_is_the_whole_authored_frame_sheet`,
`control_bar_art_sits_at_its_authored_stage_rect`,
`control_bar_frame_node_is_wide_enough_for_the_authored_art`.

After PNGs: `workspace/logs/hud-owner/hud-after-1920x1080.png` and `...-2560x1440.png`.
In the 1080p shot the bottom-left reads C0..C5 around the dish arc and C6..C9 on the outer
ring arcing up and to the right, away from the radar; the nearest button to the radar
centre is about 250 px out against a 181 px radius.

## 8. Honest residue — read this part

1. I CHANGED THREE GATES' BOUNDS, AND A VERIFIER SHOULD SCRUTINISE THAT.
   `retail_radial_layout_runner` and `fortress_command_surface_runner` held the radial to
   "no rectangle intersects any other" and "every button inside the 520x360 panel". Both
   rules fail `Palantir.apt` itself the moment production stops inventing a wheel: the
   authored `glass1`/`glass2` and `glass3`/`glass4` sockets intersect as boxes at 64px, and
   `subMenu0` (stage [289.0, 531.3]) overhangs the dock band's top edge by 4.9px at 64px.
   I rewrote both against the authored seat set — computed from the APT table, never typed
   in — rather than weaken a number. `retail_radial_layout_runner` already computed its
   bounds this way from the six glass sockets; I extended the same computation to the ten
   authored seats. Check me here: it is the one place in this lane where a gate got easier.
2. The `+0.5` px slack in `fortress_command_surface_runner._authored_seat_bounds`. The
   envelope and the live rectangles are the same authored numbers reached by two float
   paths, and the bottom-most seat missed its own bound by 1/128 px. Float slack, not
   headroom.
3. THE CAPTURE RUNNER PHOTOGRAPHS THE SYNTHETIC SHELL, not the retail palantir art — it
   builds a bare `RetailHud` with no pack binding. The after PNG therefore proves the
   GEOMETRY is fixed; it does not show the retail frame. I did not build a rendered capture
   with the retail art bound, and the "before" side of the pair is the owner's own
   screenshot, not a run of mine.
4. `structure_radial_command_set_runner` HANGS, AND IT IS PRE-EXISTING. It prints 12 PASS
   rows and never reaches a RESULT line. Confirmed at pre-lane `47c2aa8d` in a throwaway
   worktree under %TEMP%: killed at a 420 s wall clock with no RESULT there either. Not
   caused by this lane, not fixed by it. Worth a queue row.
5. `fortress_command_surface_runner` is red at 2 by NAME both before and after:
   `angmar_the_second_fortress_finishes_construction` and `runner_ran_every_section` (a
   silently aborted `constructed_fortress_parity` section — the classic headless coroutine
   abort). Pre-existing, measured at 47c2aa8d, not touched.
6. The boot runner's "before" 4 failures were cold-cache wall-clock budgets (first frame
   17.1 s vs 7 s, ContentDB.reload 5.7 s vs 5 s, shell compile 3.7 s vs 1.8 s, menu 33.0 s
   vs 12 s), taken immediately after a fresh `--import` in this worktree. Warm, the same
   runner is 44/0. Both are reported rather than picking the flattering one; the lane
   changes no boot path.
7. THE RING CONTINUATION PAST FOUR SEATS IS DERIVED, NOT AUTHORED. Seats 0-3 are verbatim;
   seats 4-7 use the mean of the authored step and the mean of the authored radius. A range
   of 11-14 (Elven / Mordor monument fortresses, `CommandRangeCount 11/13/14`) therefore
   lands on derived positions. I could not find an authored seat for them; if a later
   capture shows retail placing them elsewhere, this is the constant to correct.
8. `RETAIL_PALANTIR_FRAME_DISPLAY_SIZE` is still (720, 360) and drives the synthetic public
   shell's size and the powers-dock top. I deliberately did not widen it to 960 with the
   retail frame, to keep the public fallback's appearance unchanged. The retail composition
   no longer agrees with that constant; intentional, and named here rather than silently
   reconciled.
9. NOT DONE FROM THE BRIEF: nothing about a full-width bar bitmap, because section 4 shows
   retail ships none, and nothing that would have asserted "the bar art node spans the
   window width" (brief task 4a) — that assertion would be false against retail. If the
   owner's expectation came from BFME1 or from a mod, that is a product decision rather
   than a parity fix, and I did not make it.
10. I did not run the full importer suite or the offline systems gate: this lane touches no
    Python and no pack bytes.
