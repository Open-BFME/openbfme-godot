# Fortress presentation review report

- Review base: `f497adfc591d4a27d40e1a9e088cc95bebe2e5c9`
- Capture-selected pack: `bfme2-men-vslice/ce02105e952ce91faa2b2cab429e2be01200c939e6553ef8be5b2deb8e591383`
- Headless content mount: `rotwk-men-vslice/d9a12509ba2591245c50a7109a90ef4a348f82ac9e08b4290e0aaac070238a2e`

## Review fixes

### B1 — selected-state health bar

The bar now anchors to the transformed intact model's measured visual-AABB top
(`_visual_top_y + 0.12`), not a fortress/non-fortress constant. It is a
screen-facing `Sprite3D` with `fixed_size=true`; its logical footprint width is
kept separately from its fixed 64-pixel presentation.

Identical 1920x1080 selected-fortress capture recipe:

| capture | private path | measured longest green run | location |
|---|---|---:|---|
| before (`f497adf`) | `.private/scratch/fortress-selected-bar-before-f497adf.png` | 384 px | `y=40`, `x=594..977` (screen ceiling) |
| after | `.private/scratch/fortress-selected-bar-after-review-fix-calibrated.png` | 63 px | `y=212`, `x=770..832` (immediately above top tower) |

Both PNGs were emitted successfully by `retail_render_capture_runner.gd` from
the capture-selected pack above. The first attempted fixed-size calibration was
rejected after visual inspection and is not acceptance evidence.

### B2 — positive footprint and roof bounds

The red-first runner on `f497adf` failed with four live rows at
`width=3.600`, `footprint=3.782`, `source=intact-body-bounds`. Its roof row also
failed every damageable castle structure. Production now recomputes
`_health_bar_width = pick_radius * 2.0` whenever intact bounds can remeasure the
pick radius, with no contradictory minimum floor.

The strengthened gate positively requires:

- bar logical width equals the measured footprint diameter within 0.02;
- the bar anchor is above the measured visual roof by at most 0.20 world units;
- the rendered bar is both fixed-size and billboarded.

Final: `FORTRESS_PLOT_PRESENTATION_RESULT passed=22 failed=0`.

### B3 — authored socket arc and ownership

The red-first gate reproduced the reviewer's coordinates exactly:
`Radial_train_ (555,803)` and `@Button@140 (473.6895,842.157)` intersected the
visible `Train_men-porter (510,784)` socket.

Radial entries now own the palantir socket surface while visible. Up to six
entries use `RETAIL_COMMAND_SLOT_SOURCE` exactly. Longer authored ranges extend
that same open arc with a count-scaled radius and an axis-aligned rectangle
separation bound; they never use the regressed fixed `r=104` full circle.
Populated socket buttons are hidden once on the ownership transition, and the
per-frame production refresh honors that state instead of re-showing and
re-hiding them.

The main, upgrades, and heroes pages each assert:

- every radial button remains inside the command panel;
- zero intersection with any visible authored socket button;
- zero radial-button self-intersection;
- unchanged socket visibility and transition count across repeated frame
  refreshes.

Final Men result: `fortress_command_surface passed=66 failed=0`.

### B4 — retail empty palantir fact

Retail's **unselected** palantir is exactly a black disc with empty sockets in
the oracle capture. That empty-disc appearance is retail-correct. The
owner-visible defect was the **populated selected-state** layout, where two
command surfaces occupied the same sockets; this change targets that state.

### B5 — reproducible Mordor icon proof

`fortress_command_surface_runner.gd` now checks the nine retail ButtonImage ids
used by Mordor's two page selectors, six fortress improvements, and back button.
Every row must originate from the interface-art index and resolve to an existing
PNG. The row runs before faction expectation filtering, so
`OPENBFME_SLICE_FACTION=mordor` now prints:

`PASS mordor_all_nine_fortress_image_ids_resolve_via_interface_art`

The rest of the Mordor fortress behavior matrix still truthfully reports
`SKIPPED (no transcribed retail expectation)`; the icon claim itself is no
longer dependent on an ad-hoc probe. Mordor result: `passed=18 failed=0`.

## Regression evidence

| gate | result |
|---|---|
| fortress plot presentation | 22 passed, 0 failed |
| fortress command surface (Men; all three pages) | 66 passed, 0 failed |
| fortress command surface (Mordor icon row before expected skip) | 18 passed, 0 failed |
| retail launch validation | 35 passed, 0 failed |
| retail state pin, 3000 ticks | `a436bb5989a026ee0be6674ac514c1035784dbe6fc92281ddfbb78cc79e0a05a` unchanged |
| retail slice, branch | 373 passed, 32 known failures |
| retail slice, main | 373 passed, 32 known failures |
| ordered failure names | byte-identical; SHA-256 `dedce0de54f6ff67128a0324e763b1be2a1acda887e771c9a82c01961134d0f4` |

No pack bytes or selection state were changed.
