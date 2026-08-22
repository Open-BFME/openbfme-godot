# Lane RADAR — "the radar blends into the terrain"

Date: 2026-08-22
Branch: `worktree-agent-afd6f1f515f7d6413` (worktree of `C:\Users\Jonathan\Desktop\open-bfme`)
Worktree branch base: `47c2aa8d`. Main head at lane start was `887d712b`; the two
differ only in `orchestration/` docs (`git diff --stat 47c2aa8d 887d712b` touches
briefs and queue.md only), so every BEFORE number below is the same code either way.
Fix commit: `688fd3dc`
Content the numbers came from: the `workspace/content-packs` selection as of this run —
active `rotwk-men-vslice/b361ec5f...`, palantir atlas from
`bfme2-men-vslice/7de517bf146582f10741750b50d63f9955c42d1fe2aa13200757fc6fb29f217a`,
map art from `rotwk-playable-maps-private/0127db1693ab16f02c76878b69e486f0ae2f4f072b37dad8538c196783391508`.
No pack was cooked, published or selected; `selection.json` and `VERSION` untouched.

All evidence under `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\radar-owner\`.

---

## 1. What the brief assumed, and what retail actually does

The brief expected retail's radar to be "a terrain bitmap (map texture colours with
visible roads/cliffs/water)". **It is not, and I did not fix it to be one.** Evidence,
taken from the original game files rather than from prior reports:

- Every map's `<map>_art.tga` is a **single flat ink colour with the whole drawing in
  the alpha channel**. `map mp fords of isen ii_art.tga` (128x128 RGBA) has RGB extrema
  R(74..91) G(44..59) B(1..16) — one brown — and alpha 0..255 carrying a filled river
  band at alpha~128 with white outlines at 255. Dumped to `fords-art.png` /
  `fords-alpha.png`.
- Across all 102 `*_art.tga` under
  `workspace/retail-work/editions/rotwk/cache/effective-assets/maps/`, the **largest RGB
  channel span of any file is 43**, and 14 are perfectly flat. There is no terrain
  photograph anywhere in that data (`peek2.py` output).
- The retail reference dish
  (`workspace/retail-work/oracle/captures/bfme2-fords-men-reference-youtube-z6ZI6wY_LYE-500s.png`,
  cropped to `retail-500s-dish.png`) is tan parchment with sepia ink, a thick gold view
  box and house-colour blips. `retail-500s-radar.png` shows the same at frame scale.
- The only radar colour rows retail authors are `roads.ini:15,34 RadarColor = R:192 G:192 B:192`
  and `water.ini:74 RadarWaterColor = R:140 G:140 B:255`; there is no per-cell terrain
  colour table. `mappedimages/handcreated/handcreatedmappedimages.ini:1680-1687` authors
  `RadarViewBoxEdge`, and `animation2d.ini:313-334` the radar alerts.

So the Q64 claim that the radar is parchment plus authored ink is **correct**. What was
wrong is *how* that composition was drawn — and it was wrong in a way that produces
exactly the owner's symptom.

## 2. The measured defect

`RetailMinimap._paper_square()` drew the authored palantir parchment sheet **1:1 across
the bezel opening** — sheet radius equal to bezel radius. That sheet
(`assets/ui/palantir/atlases/apt-palantir-1-d9888d52cd89.png`, region `Rect2i(4,4,214,214)`)
carries its own vignette *and* the frame's drop shadow: normalised on its 107px radius it
runs 1.00 -> 0.45 by 0.72R and down to **0.09 at its edge** (`ATLAS-norm`, `radial.py`).
Laid 1:1, that black rim sits *inside* the ring, so the outer third of our dish — map
ink, blips, everything — was drawn into near-black.

Mean luminance per radial bin, normalised to each profile's own peak, 20 bins, outer
quarter:

| r/R | 0.775 | 0.825 | 0.875 | 0.925 | 0.975 |
|---|---|---|---|---|---|
| **BEFORE** (`887d712b`, 362px render) | 0.33 | 0.22 | 0.15 | 0.11 | 0.09 |
| **RETAIL** (Fords 500s dish, centre (131,598), r=74) | 0.68 | 0.60 | 0.53 | 0.48 | 0.43 |
| **AFTER** (this lane) | 0.73 | 0.66 | 0.59 | 0.49 | 0.37 |

The owner's own 1920x1080 screenshot measures the same way: dish centre (227,897),
radius 142, local high-frequency detail ratio **0.114** against the retail dish's
**0.137** (`local_contrast.py`).

Secondary, same symptom: the procedural camera view-box fallback drew a **flat 1.6px
polyline at any dish size** — 0.010 of the bezel radius at the shipping 362px control.
Retail's gold band is 3.5px against a 74px opening (column profile at x=152, rows
602-605, peak `(252,215,91)`) — **0.047R**, four to five times thicker.

## 3. The fix

`game/src/retail_slice/retail_minimap.gd`

- New `RETAIL_PARCHMENT_DISC_SCALE := 1.29`. `_paper_square()` now draws the authored
  sheet at `bezel_radius() * 1.29` and lets the ring crop the vignette. **The constant is
  a fit, not a taste call**: least squares of the sheet's own radial profile against the
  retail capture's, over all 20 bins, minimises at s=1.29 with an RMS residual of 0.035
  of peak (`fit.py`; the printed fit table is in the log dir).
- New `RETAIL_VIEW_BOX_EDGE_RADIUS_RATIO := 0.047` and `VIEW_BOX_FALLBACK_GOLD :=
  Color8(252,215,91)` at alpha 0.9, plus a public `view_box_line_width()`.
  `_draw_footprint_outline` uses them. Both numbers are read off the retail capture
  (cited above). **The 0.9 alpha is inherited, not measured** — retail's band is a soft
  glow that a flat polyline cannot carry; it stays until an interface-art pack publishes
  the authored `RadarViewBoxEdge` crop.

Nothing else changed: no terrain synthesis, no shroud change, no blip change, no
geometry/placement change, no importer or pack work.

## 4. Tests

`game/tests/minimap_parchment_capture_runner.gd`
- New `RETAIL_RADIAL_LUMINANCE` (the retail dish's 20-bin profile),
  `_radial_luminance_profile()` and check `<px>_rim_stays_legible_like_retails_dish`.
  One-sided floor over the outer quarter, tolerance 0.10 — ink and shroud can only remove
  light, so a floor is the honest contract.
- `_parchment_disc_mean()` now samples the sheet at the new scale, so
  `<px>_disc_reads_as_retail_sepia` still compares against the source bitmap rather than
  a remembered number.

`game/tests/radar_look_runner.gd`
- New `view_box_fallback_width_scales_with_the_dish` and
  `view_box_fallback_uses_the_measured_retail_gold`.

**Failing first, both:**

| Runner | Before fix | After fix |
|---|---|---|
| `minimap_parchment_capture_runner` (Amon Sul art) | **17 passed / 2 failed** — `362_rim_stays_legible_like_retails_dish`: `r/R=0.78 got=0.33 floor=0.58, 0.82 got=0.22 floor=0.50, 0.88 got=0.15 floor=0.43, 0.93 got=0.11 floor=0.38, 0.97 got=0.09 floor=0.33` (same for 724px). Log `red-capture-amonsul.txt` | **19 passed / 0 failed** — `after-capture-amonsul.txt`, `final-capture-amonsul.txt` |
| `radar_look_runner` | Refused to load: `Parse Error: Cannot find member "RETAIL_VIEW_BOX_EDGE_RADIUS_RATIO"` / `"VIEW_BOX_FALLBACK_GOLD"` (`red-radar-look.txt`). Honest caveat: this is a parse-level red, not a clean assertion red — the seam did not exist yet. | **18 passed / 0 failed** — `green-radar-look.txt`, `final-radar-look.txt` |

## 5. Named gates, before -> after

| Gate | Brief's stated baseline | Measured this lane | Verdict |
|---|---|---|---|
| `radar_look_runner` | 16 / 0 | **18 / 0** (2 new checks added by this lane) | green, grown |
| `retail_four_unit_hud_runner` | 124 / 0 | **124 / 0** (`gate-hud.txt`) | unchanged |
| `castle_map_live_boot_runner` (`rotwk.map.wor-erebor`) | 8 / 0 | **1 passed / 1 failed** — `castle_slice_ready: Slice map 'rotwk.map.wor-erebor' is unavailable: it is neither in the registered content nor in the bfme2-five-maps-106-private pack catalog` | **RED, and PRE-EXISTING** |

The castle gate red is **not** this lane's. Proof, not assertion: the identical command
run against the untouched main checkout at `887d712b` produces the byte-identical failure
(`gate-castle-boot-baseline-main.txt` versus `gate-castle-boot.txt`). Cause: the currently
selected maps pack `rotwk-playable-maps-private/0127db1693...` ships **10 maps**
(adorn-river, amon-sul-fortress, anfalas, argonath, brown-lands, evendim, fall-back-4p,
fall-back-8p, fords-of-isen-ii, grey-mountains) and `wor-erebor` is not among them. The
Q64 report's 8/0 came from maps pack `abc27325...`, a different selection. This is a
selection-state gap for whoever owns the maps pack, not a radar change.

## 6. Eyes-required evidence

`workspace/logs/radar-owner/radar-before-after-retail.png` — three 362px dishes side by
side: BEFORE `887d712b`, AFTER this lane, and the retail Fords dish resampled to the same
diameter. Sources: `before/radar-362px.png`, `after2/radar-362px.png`, `dish-retail.png`.
The black rim is gone, the ink reads out to the metal, and the view box is a thick bright
gold band as in retail.

Also kept: `owner-dish.png` and `owner-hud.png` (the owner's screenshot cropped),
`retail-500s-dish.png`, `parchment-region.png`, `fords-alpha.png`, `art-sheet.png`
(alpha of all 10 selected map arts).

## 7. Honest residue — what I did NOT fix

1. **`minimap_parchment_capture_runner` is 17/2 on Fords of Isen II art, before *and*
   after this lane.** The two failures are `362/724_centre_is_retails_lit_parchment`
   (`centre=(148.9,126.3,84.0)` versus expected `(179.0,160.3,118.2)`). Cause: Fords'
   river ink runs straight through the sample window, and the check's
   `INK_LUMINANCE < 0.45` exclusion does not catch the half-alpha river fill, which lands
   at about 0.43. That is a bug in the check's ink exclusion, not in the composition —
   the same runner is 19/0 on Amon Sul art. Before-fix log `before-capture-fords.txt`,
   after-fix `after2-capture-fords.txt`; the two centre values differ by 0.1/255. I left
   it alone rather than widen a threshold to make my own lane look clean.
2. **Blip sizes are unchanged and I could not prove a defect either way.** Retail's Fords
   dish shows unit marks about 2px and structure marks about 5-6px against a 74px opening
   (0.014R and 0.037R). Ours are 2.3px and 2.0px at a 162px opening (0.014R and 0.012R) —
   the unit marks match retail's ratio, the structure marks look about 3x small. I did
   not change them: one compressed 720p frame cannot tell me whether those large blue
   squares are ordinary structures or fortresses, and the Q64 runner pins 2.3/2.0. This
   needs a second retail capture at a known composition before anyone touches it.
3. **The authored `RadarViewBoxEdge` crop is still absent from the selected interface-art
   packs**, so what shipped is the *procedural* band at retail's measured width and
   colour, not the retail bitmap. Named gap carried over from Q64; needs an interface-art
   cook.
4. **`RadarPriority` admission and `RadarInfoAlert` pings** remain the Q64 content and
   event gaps. Untouched.
5. **I did not re-photograph the live HUD in a real match.** The after-evidence is the
   `RetailMinimap` control rendered at its shipping 362px through the real published
   atlas and map art, not a full skirmish frame. The owner's screenshot is the only
   live-match image in this report and it is the BEFORE.
6. **The retail oracle is a YouTube capture, not a lossless frame.** Its absolute
   luminances carry video compression; that is why every threshold here is a
   peak-normalised ratio with a stated tolerance rather than an absolute RGB.
