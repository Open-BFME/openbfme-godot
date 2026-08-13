extends Control

## THE 3D WAR OF THE RING MAP. Retail's own Middle-earth mesh under a camera,
## with the region graph drawn over it at retail's own world coordinates.
##
## HOW REGIONS LAND IN THE RIGHT PLACE - the whole point of this file:
##
## The living-world document's `centerPoint` values and the living map's vertex
## data are the SAME coordinate space. That is measured, not assumed, and the
## measurement rides in the bundle manifest where a test can read it:
##
##   * the 20 terrain tiles tile a 5x4 grid of ~1204 x ~1205 unit cells, each
##     cell occupied exactly once - so the bone transform is applied correctly;
##   * the nine landmark sub-objects (Minas Tirith, Orthanc, Helm's Deep, Erebor,
##     Dol Guldur, Rivendell, Osgiliath, the Black Gate, Cirith Ungol) sit within
##     140 world units of their region's authored centre on a map 6021 units
##     wide - so document space and map space are the same space, at scale 1.
##
## A region is therefore placed at its authored (x, y) with its HEIGHT SAMPLED
## from retail's terrain triangles at that exact point. Sampling shipped geometry
## is derivation; picking a height that looks right would be invention, and when
## no triangle covers the point this says so rather than guessing.
##
## WHAT IT REFUSES TO DRAW:
##
## * A region with no authored `centerPoint` is NOT placed. Retail derives those
##   markers from per-region mesh data that this bundle does not carry, so the
##   screen lists them separately instead of dropping them at a plausible spot.
##   BFME2 authors exactly one such region: Rhun.
## * A sub-object whose texture did not resolve is drawn flat grey, never with a
##   substitute image.
##
## PRESENTATION ONLY. The camera, the hover highlight and the zoom live in this
## Control and reach nothing. No value here is ever hashed, and the only way a
## click becomes a battle is the screen calling `session.commit_attack()`.

signal region_clicked(region_id: String)
## Retail's strategic move gesture is "Left click your Heroes, then right click
## to move them" (SCRIPT:MORIAMOVE in the shipped string table).  These two
## signals keep the view presentation-only while exposing both halves of that
## gesture to the screen.
signal army_clicked(army_id: int, region_id: String)
signal region_commanded(region_id: String)
signal region_hovered(region_id: String)
## A build plot on the map was clicked. `index` is the plot's position in the
## region's own authored `BuildingSpot` list, so the screen can name it exactly.
signal plot_clicked(region_id: String, index: int)
## AN ICON ON THE BUILD RING WAS CLICKED, OR IS UNDER THE POINTER.
##
## THE OWNER'S WORDS: "I cannot click on the buildings or build them with the
## icons as they don't light up and do not allow me to build them." Both halves of
## that were one absence: `radial_slots()` has computed a hit box per entry for
## several rounds and NOTHING EVER TESTED IT AGAINST THE MOUSE, so the ring's
## icons were a picture of a menu. There was no signal to emit either, so the
## screen had nothing to connect even if it had wanted to.
##
## `building_id` is the entry's own `id` as the screen handed it in through
## `set_overlays`, so this view neither knows nor decides what a building is - it
## reports which of the caller's own offered entries the pointer is on. The hover
## signal is emitted with an EMPTY `building_id` when the pointer leaves the ring,
## so a listener can clear whatever it lit.
signal build_entry_clicked(region_id: String, plot_index: int, building_id: String)
signal build_entry_hovered(region_id: String, plot_index: int, building_id: String)
## Emitted after the overlay has actually painted. The counters this view reports
## - banners drawn, labels drawn, labels held back - only exist AFTER the paint,
## so a mode line built during `refresh()` reported the previous frame's numbers
## and said "0 banners" over a map with six banners on it. The screen re-reads
## them here, and only them.
signal overlay_painted

const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const MarkerModelsScript = preload("res://src/wotr/wotr_marker_models.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")
## The HUD's shared type scale and its over-the-map text plate. Read-only: this
## view uses `TYPE_MAP_FLOOR` and `draw_text_plate()` so the lettering it puts on
## Middle-earth is the same lettering the rest of the strategic surface uses.
const HudChromeScript = preload("res://src/wotr/wotr_hud_chrome.gd")
## The presentation treatments for retail's water and cloud surfaces. See the
## headers of both shaders and `PRESENTATION_SURFACES` in the bundle for what
## each one claims and does not claim.
##
## LOADED AT RUNTIME, NOT PRELOADED, AND THAT IS A STARTUP FIX RATHER THAN A
## STYLE PREFERENCE. `preload` resolves at COMPILE time, and GDScript cannot
## compile a script that preloads a `.gdshader` on the background resource-loader
## thread - the shader compiler is main-thread only. So four `preload(...)` lines
## in this file made every scene that reaches this script (`boot.tscn`, through
## the WOTR screen) refuse the threaded load path: the shell's compile blocked the
## main thread and the loading screen's progress bar sat still for ~3 seconds
## while it did. `load()` defers the same four resources to the first
## `_surface_material` call, which happens on the main thread once a bundle is
## bound, and the threaded path works. They are cached in `_shaders` so the four
## loads happen once rather than per surface.
const WATER_SHADER_PATH := "res://src/wotr/shaders/wotr_map_water.gdshader"
const CLOUD_SHADER_PATH := "res://src/wotr/shaders/wotr_map_cloud.gdshader"
const COAST_SHADER_PATH := "res://src/wotr/shaders/wotr_map_coast.gdshader"
const SMOKE_SHADER_PATH := "res://src/wotr/shaders/wotr_map_smoke.gdshader"
const FLARE_SHADER_PATH := "res://src/wotr/shaders/wotr_region_flare.gdshader"

## How far above their authored height the LM_COAST* strips are lifted. Retail
## parks them a fraction of a unit BELOW the WATER plane's top face, and its
## renderer resolves the tie by draw order; here the animated sea would win the
## tie and swallow the foam. Same precedent as TERRITORY_HEIGHT_BIAS: retail's
## own `ArmyLineHeightBias = 3.0` exists for exactly this class of problem, and
## the foam sits below the territory art on purpose.
const COAST_HEIGHT_BIAS := 1.5

const MARKER_RADIUS := 9.0
const PICK_SLOP := 7.0

## The army banner. Retail draws a stack as a standard carrying a portrait; this
## is a portrait plate on a staff in the owner's colour, at retail's own map
## position. The size is a presentation choice and reaches nothing.
const BANNER_WIDTH := 34.0
const BANNER_HEIGHT := 34.0
const BANNER_STAFF := 15.0
## How far apart stacked banners are fanned when several armies share a region.
## Wider than the banner itself, so two stacks in one region are two readable
## portraits rather than one portrait with an edge behind it.
const BANNER_FAN := 38.0
## At most this many banners are drawn per region; the rest are counted in the
## "+N" tail rather than piled into an unreadable heap.
const MAX_BANNERS_PER_REGION := 3
## How far apart stacked 3D markers are fanned, in RETAIL WORLD UNITS. Retail's
## own banner models are ~70 units across and ~128 tall, so 90 puts two standards
## side by side with a gap rather than one inside the other. Presentation, like
## `BANNER_FAN` is for the flat plates, and it reaches nothing.
const MARKER_FAN_WORLD := 90.0

## MARKER LEGIBILITY ACROSS A ~29x CAMERA, and why this multiplier exists at
## all rather than standing every model at retail's own `Scale` and stopping.
##
## Retail's banner is about 128 world units tall on a map 6,021 units wide, and
## at RETAIL'S OWN strategic camera distance that is a readable standard. This
## camera goes from a fifth of one terrain tile to more than the whole board -
## travel retail never had - and at the far end retail's true size projects to
## roughly fifteen pixels: converted, standing, and unreadable.
##
## So the marker is scaled by the camera, on the SAME rule and for the same
## reason the flat overlay already uses in `_view_scale()`: square-rooted so the
## growth is gentle, clamped at both ends, and 1.0 - retail's exact authored size
## with no multiplier at all - for every framing at or below MARKER_TRUE_ZOOM.
##
## THIS IS A PRESENTATION VALUE AND IT IS NOT RETAIL'S. It is stated here and on
## screen rather than folded into the model, it reaches nothing, and the model,
## its meshes, its `ZOffset` and its `OrientAngle` are all still retail's own.
const MARKER_TRUE_ZOOM := 0.25
const MARKER_MAX_MAGNIFICATION := 2.4

## Build-plot markers: retail decals a plot with a faction foundation model
## (`LMGFoundation` and its six siblings). When the marker bundle carries that
## model the DECAL ITSELF is stood on the map at retail's own authored plot
## coordinate; the flat ring below is the STAND-IN kept for a plot whose model
## did not convert, and the screen names the model in that case.
const PLOT_RADIUS := 9.0
const PLOT_PICK_SLOP := 6.0
## How strongly a plot OUTSIDE the province the player is acting on is drawn. It
## is still there - see `plot_regions` for why every plot on the map is now shown
## - but the province under the pointer has to stay the loudest thing in the
## frame or "always visible" becomes "98 identical rings and no focus".
const PLOT_DISTANT_ALPHA := 0.42

## The radial build menu. Retail rings a selected plot with the structures that
## can go on it; the ring radius and icon size are presentation.
const RADIAL_RADIUS := 78.0
const RADIAL_ICON := 46.0

## Label placement. A label is only drawn when its box does not overlap one
## already placed, so a dense corner of the map shows the regions that matter
## rather than an unreadable pile.
const LABEL_FONT_SIZE := 13
const LABEL_PADDING := Vector2(6.0, 3.0)
## THE STRATEGIC FRAMING CARRIES NO FLOATING NAMES. At retail's default zoom
## the only lettering on the map is the engraved TEXT PLANE (ARNOR / ERIADOR /
## RHOVANION / GONDOR / Rhun); the per-region names appear as you close in on
## a territory. Zoom below this and the floating labels come back; above it
## only the regions the player is acting on (selection, target, hover) keep
## theirs. 0.30 is inside the band where the engraved plane has already begun
## to fade (`_text_plane_alpha` fades over 0.16..0.55), so one lettering hands
## over to the other rather than both shouting at once.
## A FRACTION OF THE REACHABLE PULL-BACK, not an absolute zoom, for the reason
## `_text_plane_alpha` states: 0.48 is the old 0.30 over the old fixed ceiling of
## 0.62, so it is the same point in the same travel on every panel shape rather
## than a different point on each.
const LABEL_REVEAL_FRACTION := 0.48

## How far above retail's terrain the territory fills and borders are lifted so
## they do not z-fight the ground they lie on. This is RETAIL'S OWN NUMBER, not a
## tuned one: `livingworld.ini` sets `ArmyLineHeightBias = 3.0` for exactly this
## problem - "this is added to the height of each point so it doesn't conflict
## with the terrain" - and the fills are the same kind of surface.
const TERRITORY_HEIGHT_BIAS := 3.0
## The border is lifted slightly further so it draws over its own fill.
const BORDER_HEIGHT_BIAS := 4.5

## How opaque an owned territory is. Retail shades the fill and lets the terrain
## read through it; a solid fill would bury Middle-earth under flat colour. The
## old values (0.46 / 0.62 / 0.74) did exactly that burying - a selected region
## was three-quarters paint - where the reference captures show Mordor's yellow
## as a TINT the volcano still reads through. Ownership now leans on the BORDER,
## which is how retail reads too: a thick band in the owner's colour around a
## lightly tinted interior.
## Pushed down in round 2 (0.20/0.28/0.34 -> 0.10/0.18/0.26) on the reading that
## the reference interiors are a WASH and the outline carries ownership. ROUND 3
## PUTS THAT BACK UP, because a blind review of the two screens side by side
## found the round-2 reading was half right and the consequence was a legibility
## failure, not a taste one: "Exhibit Two's red lines are nearly invisible
## against the red-brown Eriador highlands they enclose". Look at the reference
## again and the interiors are NOT bare - Rhun's purple, the orange province
## north of Eriador, the red one in the middle of Eriador and the green one in
## Gondor all carry a tint you can name the owner from with the outline covered.
## 0.20 is the strength at which this map's own owned regions can be told apart
## at arm's length at the default framing WITHOUT burying the painted terrain.
## Both ends of that were LOOKED AT, not reasoned about: 0.30 was tried first
## and the capture showed the Dwarven north-west as a sheet of red paint with
## the painted relief only just surviving under it - round 1's defect coming
## back by another door - while 0.20 over a bloomed band reads instantly and
## still lets the Misty Mountains' snow and the Eriador scrub through.
## Ownership now leans on BOTH: a legible tint inside a bloomed band.
const TERRITORY_ALPHA := 0.20
const TERRITORY_ALPHA_HOVER := 0.29
const TERRITORY_ALPHA_SELECTED := 0.38

## ------------------------------------------------------------------------------
## THE MOUSEOVER FLARE - retail's `MouseoverEffectFlareup`, drawn as a flare.
## ------------------------------------------------------------------------------
##
## RETAIL-DERIVED GEOMETRY, PROJECT-AUTHORED TREATMENT, and the two are kept
## apart here on purpose. `livingworldregioneffects.ini` authors
## `MouseoverEffectFlareup` on `LMR_Fill` - that is retail's, and it is why the
## flare is the region's whole footprint rather than a ring or a marker halo. How
## LOUD it is, and that it is an ADDITIVE pass over the ownership wash rather than
## nine more points of alpha on it, is this project's decision.
##
## WHY IT EXISTS. Hover used to be `TERRITORY_ALPHA` -> `TERRITORY_ALPHA_HOVER`,
## nine points of alpha on a 20%-opaque wash. On a painted terrain that is a
## change you can find if you already know where to look and cannot see if you do
## not, and the owner's complaint was that hovering an area did not light it up.
## An additive pass is a different signal from a denser wash: it RAISES the
## province out of the map instead of putting more paint on it, so the relief,
## the roads and the settlements stay readable through the thing that is meant to
## be telling you where the pointer is.
##
## HOW LOUD, DECIDED BY LOOKING. 0.34 was tried and captured at 2560x1440 with
## the pointer in Rhun: the province lit unmistakably and it also went FLAT - a
## sheet of cream with the steppe under it gone, which is the same "buried under
## paint" defect `TERRITORY_ALPHA` was pulled back from in round 3. 0.20 over the
## amber below lights the whole province just as unambiguously and leaves the
## terrain readable through it.
##
## IT FADES, AND ONLY THE FADE COSTS FRAMES. Coming on is instant - a pointer
## that arrives in a province and waits for its highlight feels broken - and going
## off takes `HOVER_FLARE_FADE_SECONDS`, which is what stops a fast sweep across
## Middle-earth strobing. At most two materials are written per frame and only
## while something is actually fading; the rest of the time `_process` does what
## it always did.
const HOVER_FLARE_ALPHA := 0.20
## Just above the ownership fill and below the border, so the flare lifts the
## interior without washing out the band that says whose it is.
const HOVER_FLARE_HEIGHT_BIAS := 3.6
## A province nobody owns has no hue to be flared in, so it flares in the warm
## amber the rest of this screen's gilt chrome is drawn in - NOT in a near-white
## parchment, which was tried first and photographed as a flat cream sheet with
## retail's painted relief buried under it. See `HOVER_FLARE_ALPHA`.
const HOVER_FLARE_NEUTRAL := Color(0.98, 0.81, 0.46)
## How much brighter the hovered region's own ownership band burns. Ownership is
## still the hue - see `_apply_territory_colors` - this only raises it.
const HOVER_BAND_GAIN := 1.28
const HOVER_FLARE_FADE_SECONDS := 0.14

## THE SHAPE OF THE FLARE'S FALLOFF, in fractions of the province's own
## rim-to-deepest-point distance. PROJECT-AUTHORED: retail's
## `MouseoverEffectFlareup` is authored as one effect on one mesh and its data
## says nothing about a gradient, so the gradient is this project's design and is
## labelled as such. What is retail's is the FIELD it falls off against - see
## `wotr_region_geometry.gd:fill_falloff_mesh`, which measures every fill vertex
## against retail's own border ribbon for the same region.
##
## 0.45 IS WHERE THE POOL REACHES FULL STRENGTH, i.e. a little under halfway in
## from the outline, and the flare is exactly ZERO on the outline itself. That
## last part is the whole point: through round 7 the flare was constant right up
## to the polygon edge, so the light ended on a hard step and the province read as
## a coloured sheet cut to the shape of a polygon. The ownership band and its
## bloomed shoulder already own the rim, and the band burns `HOVER_BAND_GAIN`
## harder on the hovered province, so what the player now sees is a hot outline
## with light pooling inside it rather than a filled polygon.
##
## 0.30 IS THE CORE LIFT, added over the pool in the deepest third so a large
## province - Rhun, Harad - does not go visibly flat in the middle the way a bare
## smoothstep does. It is added rather than multiplied so it cannot reach the rim.
const HOVER_FLARE_EDGE_FRACTION := 0.45
const HOVER_FLARE_CORE_LIFT := 0.30
## THE FILL OF A REGION AN ATTACK CURTAIN IS STANDING ON, and this is the number
## that resolves the overlap round 6 was photographed making.
##
## MEASURED, on `.captures/wotr-stream-a/r6-final/`, by comparing 01-opening with
## 03-staged - the same camera, the same window, the only difference being that
## Angmar is staged with its targets offered. Over the map area 28.1% of the
## pixels changed, and the mean change on those pixels was +17.3 red and +26.6
## blue. Both numbers are one defect each:
##
##   * +17.3 RED is nine contiguous Dwarven holdings ALL going from
##     `TERRITORY_ALPHA` to `TERRITORY_ALPHA_HOVER` at once, because every one of
##     them was a valid target and targets flared the fill. That is the "flat
##     multiply-red across all of Eriador/Arnor that flattens the terrain to
##     brown mush" a blind review reported - and it only exists while staging.
##   * +26.6 BLUE is the attacker's `LMR_Edge` curtains standing on those same
##     red regions. A curtain is a WALL about 57 world units tall, which at the
##     strategic framing projects to a band roughly fourteen pixels wide, so at
##     `TARGET_EDGE_ALPHA` it laid fourteen pixels of blue over a red fill that
##     had just been flared. Blue over red is the "muddy indeterminate purple
##     where they cross" - a third colour that names no seat and no state.
##
## THE FIX IS AN EXPLICIT RULE, not a smaller number: a region carries the
## ATTACKER'S mark or its OWNER'S paint, never both at full strength. Retail's
## own effect list is on this side of the argument - `MouseoverEffectFlareup` is
## authored on `LMR_Fill` for MOUSEOVER, and retail authors no attack-target
## effect at all - so flaring the fill for targets was this view's own addition
## and it is the half that goes. A target now reads from its curtain, which is
## retail's own geometry, over ground whose ownership tint is pulled DOWN to here
## so the two hues cannot sum. Half of `TERRITORY_ALPHA`: still nameable as the
## owner's when you look for it, no longer able to make a colour with the
## curtain over it.
const TERRITORY_ALPHA_UNDER_TARGET := 0.10
## Retail's own neutral-region colour, from `livingworldregioneffects.ini`
## (`NeutralRegionColor = R:245 G:245 B:245`), used at a much lower alpha so an
## unclaimed region reads as unclaimed rather than as a seventh player. In the
## reference captures an unclaimed region is essentially bare terrain inside its
## dark border, which is what 0.04 gives.
const NEUTRAL_TERRITORY_ALPHA := 0.04

## RETAIL'S OWN BORDER COLOUR for a region nobody owns:
## `livingworldregioneffects.ini` sets `RegionBorderColor = R:30 G:6 B:6`, which
## the living-world document carries through as
## `regionEffects[].colors.regionBorder`. Stated once, used in two places.
const NEUTRAL_BORDER_COLOR := Color8(30, 6, 6, 235)
## How far a border is pushed towards parchment for "look here".
##
## ROUND 4 TOOK THIS OFF THE OWNED BAND ENTIRELY. It used to apply to every
## region, and that is precisely how ownership and selection became one signal:
## a selected red region was a slightly paler red one, which a blind review read
## as a selection wash over a map with no ownership colour on it at all. An owned
## band is now ALWAYS the owner's hue at full chroma, selection is retail's own
## `LMR_Edge` curtain (round 5 - see `SELECTION_CORE_COLOR`), and these values
## survive only on UNCLAIMED regions under the POINTER, which have no owner hue to
## keep and so still answer "which one am I on" by lifting their own dark
## `RegionBorderColor` towards parchment.
## Presentation values; they reach nothing.
const BORDER_LIGHTEN := 0.0
const BORDER_LIGHTEN_HOVER := 0.14
## Round 5 took SELECTION off this path as well: the selected region wears
## retail's own `LMR_Edge` curtain whether or not anyone owns it, so an unclaimed
## selection no longer needs its border lifted. The value survives because the
## fill alpha table below is still keyed on the same three states and dropping one
## of them would make the two tables disagree about what a state is.
const BORDER_LIGHTEN_SELECTED := 0.28
## The additive under-glow that widens the band. Retail's outlines read
## thicker than the `lmr_border` strip is because they BLOOM. Alpha of the
## additive pass laid over the solid band; a neutral region gets none.
const BORDER_GLOW_ALPHA := 0.5

## HOW FAR OVER 1.0 AN OWNED BAND IS DRIVEN, so the environment's bloom
## threshold (1.25) catches it and nothing else on the map does. This is what
## gives the outline the SOFT SHOULDER the reference has and the round-2 capture
## did not: an over-bright band bleeds a few pixels of its own colour into the
## terrain either side of it instead of ending on a hard one-pixel step.
## Presentation, and it reaches nothing.
const BORDER_HDR_GAIN := 2.1

## THE CEILING THE BAND'S SECOND-BRIGHTEST CHANNEL MAY REACH AFTER THE GAIN, and
## THE BUG IT EXISTS TO FIX. Read this before touching either number.
##
## Round 3 drove every owned band to `v = 1.0` in HSV and then multiplied all
## three channels by `BORDER_HDR_GAIN`. The environment tonemaps LINEAR (see
## `_build_environment`), so anything at or over 1.0 CLIPS to full. Work the
## arithmetic through for the six seat colours the screen actually uses and only
## TWO of them survive it:
##
##   seat 0 #4d7fd6  band (0.264, 0.533, 1.00) x2.1 -> clips to (0.55, 1, 1)
##                   = rgb(141,255,255). A PALE CYAN. Hue 218 became hue 180 and
##                   saturation 0.74 became 0.45.
##   seat 1 #c8483f  band (1.00, 0.264, 0.212) x2.1 -> clips to (1, 0.50, 0.44)
##                   = rgb(255,129,113). Still unmistakably red.
##   seat 2 #5aa552, seat 4 #a763c9, seat 5 #3fb0ad all clip two channels and
##                   render as near-white.
##
## That is not a styling miss, it is ownership colour being DESTROYED by the
## renderer: with six seats on the board the map would show one red, one yellow
## and four white outlines. A blind review of the two-seat capture reported
## exactly the visible half of it - "no faction ownership colour on the map, only
## a red selection wash" - because the red seat was the only one whose band
## survived its own gain. Measured on `.private/capture-r4e/12-retail-aspect.png`
## over the map area, at band strength (saturation >= 0.55, value >= 0.55) the
## red seat covered 24051 pixels and the blue seat 704, a 34:1 split between two
## seats holding NINE REGIONS EACH.
##
## THE FIX IS A CONSTRAINT, NOT A TASTE KNOB. Only the BRIGHTEST channel may
## clip - that one clipping is what makes the band read as a saturated hue at
## full brightness, and it is what bloom feeds on. The second-brightest channel
## must stay under this ceiling, which is set at the value RED ALREADY LANDS ON
## (0.264 x 2.1 = 0.554), so the band a blind review called "better than anything
## in Alpha" comes out of this arithmetic UNCHANGED and every other seat is
## brought up to it. `_band_color` buys the headroom in saturation first (hue and
## value untouched) and `_band_gain` gives back gain only when saturation has run
## out at 1.0.
const BAND_SECOND_CHANNEL_CEILING := 0.55
## The floor `_band_gain` will not go under, because the glow threshold is 1.25:
## a band driven below this stops blooming altogether, which would trade a hue
## defect for the hard-edged one round 3 fixed. A hue whose geometry cannot fit
## the ceiling even at saturation 1.0 (violet and teal, whose seat colours sit
## between two primaries) lands here and keeps its bloom.
const BORDER_HDR_GAIN_MIN := 1.55

## THE FILL'S CHROMA FLOOR. Same defect one layer down, without the clipping: the
## interior wash used the seat colour RAW, and the six seat colours carry
## saturations from 0.50 (green) to 0.71 (yellow) and values from 0.65 to 0.84.
## At `TERRITORY_ALPHA` = 0.20 over painted terrain that spread is the difference
## between a wash you can name the owner from and one that reads as haze. These
## floors are `maxf`, never a rescale: no seat colour is darkened or desaturated,
## the weak ones are brought up to the strong ones. Red (#c8483f, s 0.685,
## v 0.784) moves by less than two hundredths, so the terrain/fill balance round 3
## measured and a blind review praised is not disturbed.
const FILL_SATURATION_FLOOR := 0.70
## RAISED IN ROUND 9 FROM 0.80, AND THE REVIEW THAT ASKED FOR IT NAMED THE SEAT
## THIS MOVES. "Faction red wash over red-brown southern terrain is muddy.
## Slightly cooler or more desaturated red, or drive ownership more through the
## outline and less through the fill."
##
## THE DIAGNOSIS IS VALUE, NOT HUE OR ALPHA. Mud is what a mid-value wash makes
## over a mid-value ground: red #c8483f carries v 0.784 and Eriador's own scrub
## and the southern highlands sit in the same band, so the fill and the terrain
## met at the same brightness and only the saturation separated them. Nothing here
## touches the hue - the hue IS the ownership, see `_fill_color` - and nothing
## touches `TERRITORY_ALPHA`, whose 0.20 was arrived at by looking at captures at
## 0.30 and at 0.10 and is a separate argument.
##
## IT IS A FLOOR, SO IT ONLY LIFTS THE SEATS THAT WERE UNDER IT, and at 0.88 the
## seat it lifts most is exactly the red one the review named (0.784 -> 0.88).
## Blue (0.839), teal (0.690) and green (0.647) come up with it; yellow (0.816)
## and violet (0.788) too. No seat is darkened and none is desaturated.
const FILL_VALUE_FLOOR := 0.88

## SELECTION IS NOT MORE OF THE OWNER'S PAINT, and retail's own data is why.
##
## `regionEffects[0].sections` in the living-world document - converted from
## `livingworldregioneffects.ini` - lists FIVE effects, and they are separated by
## GEOMETRY, not by colour:
##
##   BordersEffect            geometry LMR_Border    (ownership)
##   FilledOwnershipEffect    geometry LMR_Fill      (ownership)
##   MouseoverEffectFlareup   geometry LMR_Fill      (hover flares the FILL)
##   RegionSelectionEffect    geometry LMR_Edge      (selection has its own mesh)
##   HomeRegionHighlight      geometry LMR_Highlight
##
## So retail answers "who owns this" and "which one am I on" with two different
## pieces of art. This view answered both with the same border strip, differing
## only by +0.18 fill alpha and a 0.28 lerp towards white - which is why a blind
## review read every owned red region as a selection wash. It was not wrong to.
##
## ROUND 5 STOPPED SUBSTITUTING AND DREW RETAIL'S OWN MESHES. The converter now
## writes the `edge` and `highlight` layers (`art/w3d/lm/lmr_edge.w3d`,
## `art/w3d/lm/lmr_highlight.w3d`), measured at 52/52 region coverage each, and
## `wotr_region_geometry.selection_mesh()` / `home_highlight_mesh()` hand them
## out. So:
##
##   * SELECTION is `LMR_Edge`, driven to the one achromatic colour retail's
##     region-effects palette carries (`colors.neutralRegion = R:245 G:245 B:245`).
##
##     WHAT THAT MESH ACTUALLY IS, because nothing in this project had ever drawn
##     it and the guess in the brief was wrong: it is NOT a tighter outline
##     ribbon. It is a VERTICAL CURTAIN standing on the region's boundary. Its
##     triangles are measured to enclose essentially no ground area at all -
##     Mordor's `LMR_Edge` projects to 0.0 square units on the ground plane
##     against 838,921 for its fill and 25,559 for its border ribbon - and its
##     bounding box is TALLER than the terrain under it: Angmar's edge spans
##     y -87.6 to +36.3 where its own fill spans -86.8 to -21.3, so the mesh rises
##     about 57 world units clear of the ground it stands on. Retail's selection
##     effect is a wall of light around the chosen province, not a line on it.
##
##     That is why it is drawn TRANSLUCENT and at a modest gain rather than as a
##     hot stroke. The first capture of it stood the curtain at full white and
##     `BORDER_HDR_GAIN`, and a wall of 2.1-white around a province's whole
##     perimeter blooms into a solid white blob that swallows the province: the
##     region is not filled by the mesh, it is filled by the mesh's own glow.
##   * The two marks can never be confused whatever they are coloured, because
##     one lies on the ground and the other stands up off it.
##   * THE OWNERSHIP BAND NOW KEEPS THE OWNER'S HUE EVEN WHEN SELECTED. The white
##     core was the stand-in for the mesh this view did not have; with the mesh
##     bound it would be a second selection mark that also destroys the answer to
##     "whose is it", so it is gone.
##   * THE ATTACK TARGETS get `LMR_Edge` too, one curtain each, in the ATTACKING
##     seat's own hue at reduced chroma (`TARGET_EDGE_SATURATION`). A blind review
##     of round 4 called the target set "a flat uniform wash with no per-region
##     separation, the loudest thing in the frame and the least crafted"; it was
##     right that a fill-alpha bump inside an owner's colour wash is not a mark.
##     A per-region ring in the attacker's colour is, and it keeps the one thing
##     that review said the wash did better than retail - naming the VALID SET at
##     a glance - because every valid target carries the same ring.
##   * HOVER still flares the FILL and leaves the band alone, which is
##     `MouseoverEffectFlareup` on `LMR_Fill`, exactly.
##   * THE HOME REGION is `LMR_Highlight`, and that one IS a ground mesh: a wide
##     inner band covering about a tenth of the region's own area (82,903 square
##     units against Mordor's 838,921), hugging the inside of the border. It is
##     exactly the "soft inner glow that sits under the terrain detail" a blind
##     review described in the reference capture. Drawn for the regions the caller
##     names through `set_home_regions()`. See `HOME_REGION_BINDING_GAP`: the mesh is
##     bound and drawable for all 52 regions, and this view will not GUESS which
##     region is a seat's home - retail's own answer is the seat's `capital`
##     (`StartRegion`, the field `loseIfCapitalLost` tests), which lives in the
##     strategic state and has to be handed in.
const SELECTION_CORE_COLOR := Color8(245, 245, 245)
## How much the owner-hue glow and shoulders are raised under a selection ring, so
## the region the player is acting on still reads as louder than its neighbours.
## Alphas are clamped after it, so this can never take a ring past opaque.
const SELECTION_HALO_GAIN := 1.45
## How far above the ownership band retail's `LMR_Edge` is lifted, so its foot
## does not z-fight the band it stands on. Same family of number as
## `TERRITORY_HEIGHT_BIAS`, and the same reason.
const SELECTION_EDGE_HEIGHT_BIAS := 5.6
## HOW MUCH OF THE CURTAIN'S OPACITY SURVIVES AT THE CLOSEST FRAMING. The
## curtain's height is fixed in RETAIL WORLD UNITS - about 57 units of wall above
## the ground - so its size on screen grows with every step the camera takes in,
## and this camera closes to under a fifth of one terrain tile, travel retail's
## own strategic camera never had. Photographed at the near end with no fade, a
## 0.42-alpha wall around the province the player is standing over covered the
## ENTIRE PANEL in flat blue. Same rule and same reason as the band's bloom fade
## in `_apply_territory_colors`: full at the pull-back the screen opens on, almost
## nothing at the close framing, and stated rather than folded into the mesh.
const EDGE_ALPHA_AT_CLOSEST := 0.12
## How opaque the selection curtain is. It is a WALL seen edge-on across most of
## its perimeter, so an opaque one hides the province behind it at any pitch off
## the vertical; at this alpha the terrain reads straight through the far side.
## Round 6 photographed this at 0.40 OVER the glow threshold (see
## `SELECTION_EDGE_GAIN`) and the pair produced a white sheet tens of pixels wide
## that swallowed the province it was meant to outline. The curtain is already
## fourteen pixels of standing wall at the strategic framing; it does not also
## need to be opaque.
## ROUND 9 TOOK IT FROM 0.30 TO 0.22, and the reason is the other half of
## `SELECTION_RIM_FLOOR`'s. The round-8 review: "the pale wash north of Arnor
## reads as snow before it reads as selection". The breath answers "is it alive";
## it does not answer "is it as bright as a snowfield", and at 0.30 over a core
## that clips to white the curtain was the brightest thing on the board. At 0.22,
## breathing down to 0.136, the province's own terrain and its ownership fill read
## THROUGH the curtain at every phase - which is what makes it an outline of a
## place rather than a sheet laid over one. The curtain is still fourteen pixels
## of standing wall at the strategic framing and is still the only mark of its
## kind on the map, so it does not need to be the loudest by value as well.
const SELECTION_EDGE_ALPHA := 0.22
## And the target curtains, which there are several of at once - and which stand
## on somebody ELSE'S ownership fill, so every hundredth here is a hundredth of a
## second hue laid over a first. Down from 0.42 with
## `TERRITORY_ALPHA_UNDER_TARGET`, which is the other half of the same fix.
const TARGET_EDGE_ALPHA := 0.24
## The home-region highlight sits UNDER the ownership fill - retail's
## `HomeRegionHighlight` is a footprint glow, not an outline, and a glow drawn
## over the fill would wash the terrain the fill is deliberately letting through.
const HOME_HIGHLIGHT_HEIGHT_BIAS := 2.2
## How opaque the home-region footprint is, additive, at full framing.
const HOME_HIGHLIGHT_ALPHA := 0.26

## HOW MUCH CHROMA AN ATTACK-TARGET RING GIVES UP against the selection ring and
## the ownership band it has to be told apart from, as a multiplier on the
## attacking seat's own saturation. Not a hue of its own: a target ring in an
## invented colour would be a seventh player on the map, and the round-4 capture's
## "aliased saturated-red stroke" was exactly that - it read as pure #ff0000
## because it was the RED SEAT'S band at full gain, and nothing on screen said
## which of the red regions were targets.
const TARGET_EDGE_SATURATION := 0.72
## The gain a target ring is driven to. Below `BORDER_HDR_GAIN` on purpose: the
## targets must be findable without being the brightest thing in the frame, which
## is the ordering the round-4 review said was inverted.
const TARGET_EDGE_GAIN := 1.5

## NO CURTAIN BLOOMS ANY MORE, and this pair of ceilings is how that is enforced
## rather than hoped for. Read this before raising either.
##
## The environment's glow threshold is `GLOW_HDR_THRESHOLD`, and everything above
## it is smeared through blur levels 2 and 3 - tens of pixels at these panel
## sizes. That bloom exists for the OWNERSHIP BAND, which is a ~6.5-unit ribbon
## about two pixels wide and genuinely needs a shoulder to be seen at all
## (`BORDER_HDR_GAIN`, and the review that praised the result). A CURTAIN is not
## a two-pixel ribbon: `LMR_Edge` is a wall about 57 world units tall, which at
## the strategic framing already projects to a band roughly fourteen pixels wide.
## Blooming it added twenty more pixels of soft halo on each side of something
## that was never thin - which is precisely the "generic soft outer bloom" a
## blind review named, and in the round-6 capture the selection curtain is a
## white sheet spread across half of Arnor with no readable outline anywhere in
## it.
##
## So the shoulder is retail's own GEOMETRY and nothing else. Both curtains are
## now held under the threshold and neither reaches the glow buffer at all: the
## mark is the wall, whose apparent width already varies with how squarely it
## faces the camera, and that variation is a weighting no isotropic blur can
## produce. The bands keep their bloom untouched.
const GLOW_HDR_THRESHOLD := 1.25
## The brightest channel an attack-target curtain may reach. Under
## `GLOW_HDR_THRESHOLD`, so it cannot bloom, and under the selection curtain's own
## peak, so the ordering round 4's review said was inverted stays right way up.
const TARGET_EDGE_PEAK := 1.00
## And the gain the SELECTION curtain is driven to - the loudest mark on the map,
## because there is only ever one of it. Just over 1.0 so the achromatic core
## CLIPS to full white against the terrain, and under `GLOW_HDR_THRESHOLD` so
## that is all it does. It was 1.34, which is over the threshold, and an
## achromatic curtain drives all THREE channels past it at once - roughly three
## times the bloom energy of a band, which is the sheet in the round-6 capture.
const SELECTION_EDGE_GAIN := 1.18

## THIS VIEW WILL NOT GUESS A SEAT'S HOME REGION. Retail's own answer is the
## seat's capital - `StartRegion` in an ownership set, or the region a freeform
## seat began in, which `wotr_session` already writes to
## `state.players[i].capital` because `loseIfCapitalLost` tests it. That is
## strategic state, this view reaches no state, and inferring "home" from the
## staging list or the largest holding would be inventing a rule retail does not
## author. So the mesh is bound, its coverage is measured, and NOTHING IS DRAWN
## until a caller names the regions through `set_home_regions()`.
const HOME_REGION_BINDING_GAP := "HomeRegionHighlight draws LMR_Highlight (art/w3d/lm/lmr_highlight.w3d); the mesh IS converted and bound here for every region the bundle carries, and this view draws it only for regions a caller names through set_home_regions(). Retail's own home region is the seat's capital (StartRegion / loseIfCapitalLost, held in wotr_state players[].capital) and nothing on this screen hands it in yet, so no home-region highlight is on the map. No region is guessed into being a capital."

## THE INNER SHOULDER, in RETAIL WORLD UNITS, and why it moved INSIDE in round 5.
##
## `lmr_border.w3d`'s ribbon is ~6.5 units across - two pixels at the strategic
## framing - and bloom alone spreads two pixels into a faint fuzz, not into the
## four-to-five-pixel saturated band the reference carries. So retail's OWN strip
## is also drawn displaced from the region's own area-weighted centroid, twice, at
## falling alpha. Nothing is authored here: every vertex is retail's, the centroid
## is the one the converter derived from retail's own fill triangles, and the only
## new number is how far each copy sits from the band.
##
## ROUND 4 PUSHED THESE OUTWARDS AND THAT PRODUCED TWO REPORTED DEFECTS AT ONCE.
## An outward additive ring lands on the NEIGHBOUR'S ground, so:
##
##   * where two regions of the SAME seat meet, both shoulders cross the shared
##     border and fill the seam - nine adjacent Dwarven holdings merged into the
##     single "flat uniform wash ... no per-region separation" a blind review
##     called the least crafted thing in the frame;
##   * where two DIFFERENT seats meet, an additive blue ring laid over a red
##     neighbour's red-brown Eriador ground renders MAGENTA - which is exactly
##     the "stray blue and magenta region borders in the north-central map [with]
##     no legend ... indistinguishable from debug strokes" the same review
##     reported. There was no third colour on the map; there was blue added to
##     red and nothing saying so.
##
## Both are the same sign error, and retail does not have it: in the reference
## capture every highlighted territory carries a soft glow on the INSIDE of its
## outline and clean ground outside it. Negative displacements put the shoulder
## where retail's is - inside the region's own area - so a region's glow can only
## ever land on its own ground, adjacent holdings keep a visible seam, and no
## colour is created that no seat owns.
## THE ALPHAS CAME DOWN WITH THE SIGN, and for a reason the picture made obvious:
## an OUTWARD ring lands on the neighbour's ground, where its own colour is the
## only one present, but an INWARD ring lands on this region's own FILL, which is
## already tinted with the same hue - so the same alpha reads roughly twice as
## strong. At 0.42/0.20 inward, a blue seat's provinces were photographed ringed
## in a solid saturated blue band that read as a river rather than a glow.
const BORDER_SHOULDER_OUTSETS := [-5.0, -11.0]
const BORDER_SHOULDER_ALPHAS := [0.26, 0.12]

## RETAIL'S OWN LENS, MEASURED OFF THE ORACLE RATHER THAN CHOSEN.
##
## Round 6 solved retail's strategic camera out of its own reference capture
## (`game.dat_l1eJcM0zCw.jpg`, 2560x1440) instead of describing it. The method
## uses the one surface whose world rectangle is known exactly: retail's engraved
## TEXT PLANE. Its quad spans x -2784.11..3236.79, y -1372.05..3447.11 - the
## terrain extent to the unit - and carries `lm_text.dds`, so the pixel position
## of each engraved province name inside that 1024x1024 texture converts straight
## to a world coordinate. Five of them are legible in the oracle:
##
##   ARNOR      world (-620, 1682)   oracle px ( 897,  452)
##   ERIADOR    world (-479,  727)   oracle px ( 973,  768)
##   RHOVANION  world ( 932, 1273)   oracle px (1568,  570)
##   RHUN       world (2137, 1320)   oracle px (2118,  565)
##   GONDOR     world ( 373,  -73)   oracle px (1286, 1005)
##
## TWO NUMBERS FALL OUT OF THOSE FIVE POINTS, and both contradicted what this
## file used to say:
##
##  1. THE PITCH. A least-squares affine fit gives 0.4367 px per world unit
##     across and 0.3186 px per world unit up the screen. The ratio 0.7295 is
##     the ground plane's foreshortening, i.e. `sin(pitch)`, so retail's pitch
##     is -46.8 degrees. Sweeping the fit with the pitch PINNED shows a clean
##     minimum there - residual 16.2 px at -47 against 32.4 px at -64 - so this
##     is a measurement, not a preference. The old comment claimed "the province
##     shapes barely foreshorten"; they foreshorten by 27%.
##
##  2. THE FIELD OF VIEW. That affine fit has no keystone term in it at all, and
##     it still lands inside 20 px on a 2560-wide frame. A 45-degree lens filling
##     the same frame at this pitch would spread the far edge of the map ~39%
##     wider than the near edge, which no affine fit could absorb - the residual
##     would be in the hundreds of pixels. Retail's living-world lens is
##     therefore very long: near enough to orthographic that its keystone is
##     under this measurement's own noise floor.
##
## WHY THE LENS MATTERED FAR MORE THAN IT LOOKED. `zoom_ceiling()` refuses any
## pull-back that puts the terrain slab's cut edge in frame, which means the
## camera's ground footprint must sit INSIDE retail's own 6021 x 4819 rectangle.
## A 45-degree lens does not project a rectangle onto the ground, it projects a
## badly flared trapezoid, and the flare is what has to fit: at 16:9 the best
## framing a 45-degree lens can reach under that rule covers 50% of the map's
## area. The same rule at 12 degrees reaches 80%, and at 12 degrees the picture
## is the oracle's picture - Forodwaith to Mordor's lava, the whole western
## ocean, Rhun and the Sea of Rhun, all in frame at once.
##
## SO THE FRAMING DEFECT WAS NEVER THE RULE. Four of the five south-eastern
## items a blind review reported missing were off-frame because of the LENS.
## 12.0 rather than something even longer because the fit distance goes as
## 1/tan(fov/2): 12 degrees already puts the camera ~14,500 units out on a map
## 6,021 across, and the returns past it are a percent of coverage per doubling.
## ROUND 7 FINISHED THAT MEASUREMENT INSTEAD OF APPROXIMATING IT. Round 6's own
## conclusion, quoted above, is that retail's living-world lens is "near enough
## to orthographic that its keystone is under this measurement's own noise
## floor" - and 12 degrees was the longest lens it was willing to spend distance
## on, not the lens the oracle actually showed. THE CAMERA IS NOW ORTHOGRAPHIC,
## which is what that fit said it was.
##
## THE OWNER'S COMPLAINT IS WHAT FORCED THE LAST STEP: "I want to be able to zoom
## all the way out". Under `zoom_ceiling()` the camera's ground footprint must sit
## INSIDE retail's 6021 x 4819 slab, and a PERSPECTIVE frustum lays a TRAPEZOID on
## the ground - so the widest rectangle-shaped picture it can take is limited by
## its own narrow end. Round 6 measured the consequence: 78.1% of the map at 12
## degrees against retail's ~100%, and it recorded that the remaining gap is
## inherent to a frustum rather than a tuning miss.
##
## An orthographic camera lays a RECTANGLE on the ground (a parallelogram once
## yawed), so the same rule gives up nothing to keystone at all: the only losses
## left are the panel's own aspect against the slab's, and the slab's relief. At
## retail's own 16:9 the picture reaches the whole 6021 units of width and 4633 of
## the 4819 units of depth.
##
## AND THE RELIEF COST IS NOT THE ONE THE PROPOSAL FEARED, for the reason round 6
## stated and did not follow through: at 12 degrees the perspective parallax was
## ALREADY under the fit's noise floor. Going from a 12-degree frustum to a
## parallel one removes a keystone that measured 20 px on a 2560-wide frame. The
## mountains' relief on this map is carried by the SHADING - a low warm key at
## -38 degrees with self-shadowing, see `build()` - and shading is untouched by
## the projection. Captured before and after at 2560x1440 and compared by eye,
## the Misty Mountains, the Ephel Duath and Erebor read the same.
##
## `CAMERA_FOV_DEGREES` SURVIVES AS THE MEASUREMENT, not as a live setting. It is
## what the oracle fit resolved to before the last step was taken, and it is the
## number the coverage comparison above is quoted against; nothing reads it to
## place the camera any more.
const CAMERA_FOV_DEGREES := 12.0
const DEFAULT_PITCH_DEGREES := -47.0

## HOW FAR BACK THE ORTHOGRAPHIC CAMERA STANDS, as a multiple of the map's own
## depth along the view direction, plus a fixed clearance in world units.
##
## Under a parallel projection the standoff does NOT affect the picture - it only
## decides what falls outside the near and far planes - so this is a clipping
## number and nothing else. It is derived from retail's own extent on every fit
## rather than fixed, because the depth the map occupies along the view swings by
## more than 2x across the pitch range: at -88 degrees the camera looks down the
## slab's 6021-unit width and at -8 it looks along its whole 4819-unit depth plus
## the relief. `camera.far` is 60000 and the deepest standoff this can produce is
## well inside it.
const CAMERA_STANDOFF_SPAN := 1.5
const CAMERA_STANDOFF_CLEARANCE := 2000.0

## THE OPENING ZOOM. `_zoom` multiplies the ORTHOGRAPHIC SIZE that fits the WHOLE
## MAP, so 1.0 frames all of Middle-earth INSIDE the panel - which puts the slab's
## own rectangular cut edges on screen, the loudest non-retail tell the round-1
## captures had. Retail's map FILLS the screen and bleeds off all four edges; no
## capture of retail's strategic screen shows a map edge except the western sea
## fading into the border cloud.
##
## IT EQUALS THE ZOOM-OUT CLAMP, deliberately: in the reference capture the
## default view already shows everything from ARNOR to GONDOR's coast - the
## opening framing IS retail's maximum pull-back, and from it the player only
## zooms IN. This is now stated as `MAX_ZOOM` itself rather than as a number
## tuned under it, because what actually decides the opening picture is
## `zoom_ceiling()` - the cut-edge rule at the live panel shape - and a constant
## sitting under that ceiling could only ever throw away map the rule was willing
## to give. THE OWNER'S COMPLAINT WAS EXACTLY THAT: "I want to be able to zoom all
## the way out" against an opening that stopped at 0.88 of a ceiling of ~0.80,
## i.e. at a number, not at the guarantee.
## Written as the literal rather than as `MAX_ZOOM` only because this constant is
## declared above it; the two are asserted equal in `wotr_map3d_runner.gd`.
const DEFAULT_ZOOM := 1.0

## ZOOM RANGE, and where the two ends came from. `_zoom` multiplies the
## ORTHOGRAPHIC SIZE that fits the WHOLE MAP, so 1.0 is "all of Middle-earth" by
## construction and the number means the same thing on every panel shape.
##
## MIN 0.035 IS THE SAME CLOSEST FRAMING IT HAS ALWAYS BEEN, restated for the
## projection change rather than re-decided. The floor has always been stated as
## GROUND COVERAGE, not as a distance: about 250 world units of ground across the
## panel, a fifth of one 1,200-unit terrain tile, which is already finer than the
## compiled terrain textures carry detail for. Under a parallel projection the
## arithmetic is direct instead of going through a tangent - the fitted size at
## 16:9 is ~4,400 units of ground up the panel, so 0.035 puts ~154 up it and ~274
## across it, which is that same framing. Nothing can clip at the near plane any
## more either: the standoff is derived from the extent, not from the zoom.
##
## MAX 1.0 is the ABSOLUTE ceiling - the furthest back the player may pull at any
## framing - and it is not the guarantee. THE CONSTANT WAS THE BUG once: round 2
## measured it against one window shape, and the terrain slab's own western cut
## edge is off-frame at that shape and ON-FRAME at a wider one. The capture a
## blind review judged was 1860x800 (2.33:1), and at that aspect the slab edge ran
## down the left quarter of the picture as a hard straight diagonal - measured,
## not guessed: the pixels of that cut ray-trace back to retail x = -2784, which
## is `terrain_extent.x_min` to within a unit.
##
## So the guarantee is COMPUTED rather than tuned: `zoom_ceiling()` solves for
## the furthest pull-back at which NO PART OF THE SLAB'S CUT EDGE PROJECTS INTO
## THE PANEL, at the LIVE aspect, pitch, yaw and pan, and the player's zoom is
## clamped to it. The constant survives as the cap the computed value is taken
## with, so the reachable range can only ever be narrower than the stated one,
## never wider.
##
## 0.88 -> 1.0 IS THE CAP GETTING OUT OF THE GUARANTEE'S WAY FOR THE LAST TIME,
## and it is the second half of the owner's fix. 1.0 is not a tuned number at all:
## it is BY CONSTRUCTION the framing that fits the entire slab, so a cap there
## cannot be "wrong for one window shape" the way 0.62 and 0.88 both were. Every
## panel shape's pull-back is now decided by the cut-edge rule alone. The stated
## range is ~29x, which covers "zoom way in and out like in a regular skirmish
## match" from a fifth of a tile to the whole board.
const MIN_ZOOM := 0.035
const MAX_ZOOM := 1.0
## How many halvings `zoom_ceiling()` spends. Coverage is monotone in the zoom -
## pulling further back can only ever expose more of the slab's edge, never less
## - so a bisection over a bracket of at most `MAX_ZOOM - MIN_ZOOM` resolves to
## better than a thousandth of a zoom step in a FIXED number of tests rather
## than "until it converges". It is the only search left in this camera - the
## framing's own centring bisection went when the projection became parallel and
## the balance point acquired a closed form (see `_fit_distance`).
const ZOOM_CEILING_STEPS := 20
## One wheel notch, as a ratio, so the travel per notch is the same fraction of
## the picture everywhere in the ~29x range rather than a bigger jump at one end.
## 1.16 crosses the whole range in 22 notches. It came DOWN from 1.22 with the
## pointer anchoring in `_zoom_towards`: a coarse notch was tolerable while the
## zoom closed on the panel's centre and nothing could be aimed at anyway, and it
## is not once the notch is aimed - a 22% jump overshoots the province the player
## put the cursor on. Finer than this and crossing the range becomes a chore on a
## notched wheel; a trackpad sends many small events and gets a smooth ramp out of
## the same rule.
const ZOOM_STEP := 1.16

## PITCH RANGE. Retail's strategic camera looks down at a fixed angle; this one
## is adjustable, from nearly overhead to a low oblique that shows the relief of
## the Misty Mountains. Not past -6 degrees: below that the camera is inside the
## terrain's own silhouette and the map folds into a line.
const MIN_PITCH_DEGREES := -88.0
const MAX_PITCH_DEGREES := -8.0
## Degrees per pixel of vertical drag while orbiting.
const PITCH_PER_PIXEL := 0.35
## Radians per pixel of horizontal drag while orbiting.
const YAW_PER_PIXEL := 0.006

## How far outside the map's own footprint the camera target may be dragged. Pan
## used to be unbounded, so one long drag put Middle-earth off the panel with no
## way back but a reset. A quarter of the map's span is enough to put a corner
## region in the middle of the panel and no more.
const PAN_MARGIN_FRACTION := 0.25

## KEYBOARD PAN, AND WHY THE SPEED IS EXPRESSED IN SCREENS RATHER THAN IN UNITS.
##
## The owner asked for this in as many words - "I want to be able to zoom all the
## way out and use WASD to move the camera" - and the reason it is not a world
## units-per-second number is the same reason `_view_scale()` exists: this camera
## crosses a ~29x range. A fixed 900 units/second is a tenth of a screen per
## second when the whole board is framed (unusably slow: crossing Middle-earth
## takes seven seconds of held key) and SIX screens per second at the closest
## framing (unusably fast: one tap throws the map off the panel).
##
## So the rule is "how much of the PICTURE the camera crosses in a second", which
## is constant in the only frame the player actually judges it in. At 0.9 the
## camera crosses the visible height in about 1.1 seconds and the visible width in
## about 2 seconds at 16:9 - fast enough to get from Forochel to Mordor's lava in
## three seconds at the strategic framing, fine enough to nudge one province into
## the middle of the panel when zoomed in. Under a parallel projection the visible
## height in world units is exactly `camera.size`, so this needs no approximation:
## the speed IS `PAN_SCREENS_PER_SECOND * camera.size`.
##
## SHIFT is the usual RTS modifier and it multiplies, so the player who wants to
## cross the board in one gesture can; CTRL divides, for placing the camera.
const PAN_SCREENS_PER_SECOND := 0.9
const PAN_FAST_MULTIPLIER := 2.5
const PAN_SLOW_MULTIPLIER := 0.35

## EDGE SCROLL, which retail has and this asks for cheaply because the keyboard
## pan above already does the work: the edge test only has to produce the same
## -1..1 pair of axes the keys produce, and one call site consumes both.
##
## The band is a FRACTION OF THE PANEL rather than a pixel count, because with the
## strategic screen now filling the whole window the panel is anything from a
## 1280-wide laptop to a 4K display, and 20 px is a third of the band on one and a
## twelfth on the other. 1.4% of the smaller panel axis is ~10 px at 720p and ~30
## px at 2160p, which is the same gesture on both.
##
## OFF WHILE A DRAG IS IN PROGRESS and off when the window is not focused. A
## right-drag pan that also edge-scrolls accelerates away from the pointer, and a
## map that scrolls because the player alt-tabbed with the cursor near an edge is
## a bug rather than a feature.
const EDGE_SCROLL_BAND_FRACTION := 0.014
const EDGE_SCROLL_MULTIPLIER := 0.85

## How many halvings the pan spends finding the wall - see `_pan_ground`, which is
## where the property and the defect are stated. Six resolves the step to better
## than 2% of itself, i.e. under a pixel of a 60-pixel-per-frame drag, in a FIXED
## number of tests rather than "until it converges". Same discipline as
## `ZOOM_CEILING_STEPS`, and it only runs at all when the zoom is pinned to the
## ceiling.
const PAN_WALL_STEPS := 6
## How much zoom a pan may cost before it counts as costing zoom.
##
## SMALL, AND MEASURED RATHER THAN PICKED, because this tolerance is spent ONCE
## PER CALL and a held key calls sixty times a second. At 0.002 the first
## photographed second of held `D` gave up 0.054 of zoom - thirty frames each
## quietly buying two thousandths - which is the diving defect back at a
## thirtieth of the speed. `ZOOM_CEILING_STEPS` resolves the ceiling to about a
## millionth of a zoom step, so a tolerance three orders of magnitude above the
## search's own noise floor is still comfortably clear of it, and one second of
## held key can now cost at most 0.003.
const PAN_ZOOM_TOLERANCE := 0.0001
## Breathing room around the fitted map, so the coastline is not flush with the
## panel edge. 1.0 would be an exact fit.
##
## IT CAME DOWN 1.06 -> 1.03 IN ROUND 8 AND THE REASON IS ARITHMETIC, NOT TASTE.
## The subject of the fit is now the PLAYABLE REGION SET (see `_framing_box`) and
## that box very nearly fills retail's slab on the east: the easternmost region
## fill reaches godot x 3088.0 against the slab's own cut at x 3236.8, 149 units
## of clearance on a 5,195-unit board. At 16:9 a 1.06 margin asks for a 5,507-unit
## picture, and no placement of a 5,507-unit picture both contains the region set
## AND stays inside the 6,021-unit slab - the two feasible intervals for the
## centring miss each other by 7 units, so `zoom_ceiling()` would pull the opening
## framing in and crop the board it had just been re-aimed at. 1.03 asks for
## 5,351 units, the two intervals overlap by 149, and the opening framing holds
## the whole region set with the cut still out of frame. Anything above ~1.035
## reintroduces the conflict; this is the largest breathing room the slab pays
## for.
const FRAMING_MARGIN := 1.03

## ------------------------------------------------------------------------------
## THE BAND OF PANEL THE HUD STANDS ON, AS A FRACTION OF THE PANEL'S HEIGHT.
## PROJECT-AUTHORED. Retail authors nothing of the kind and none is claimed.
## ------------------------------------------------------------------------------
##
## MEASURED, NOT CHOSEN: at the 2560x1440 capture this project's own strategic
## tray stands at y 1035..1426 across x 702..2555 and the palantir dish occupies
## (0, 960) to (960, 1440), so the union covers every column of the bottom 405 px
## of 1440. 405/1440 = 0.281.
##
## IT IS A FRACTION AND NOT A PIXEL COUNT because the HUD is laid out in fractions
## of the panel, so a pixel count measured at one window would be wrong at every
## other one.
##
## THIS VIEW DOES NOT OWN THE HUD, so this is a STATED ASSUMPTION and it is
## overridable: `set_hud_keep_out()` already receives the chrome's own island
## rectangles, and `occluded_bottom()` prefers them whenever they are supplied.
## The assumption is only used when nothing has been handed in.
##
## AND IT IS NOT TAKEN ON TRUST. `_draw_tray_feather` inks this band itself, so
## whatever the HUD does or does not cover, the player never sees raw map or void
## in it. That is what makes the framing bias below safe rather than hopeful.
const HUD_OCCLUDED_BOTTOM_FRACTION := 0.281

## How much of the occluded band the feather has already reached full opacity by,
## as a fraction of the band, measured from the panel's bottom edge. The top 65%
## of the band is the GRADIENT - the map going into shadow under the tray, which
## is the "feather the map into the tray rather than a hard seam" the round-8
## review asked for - and the bottom 35% is solid, which is the part that has to
## be able to cover the slab's rim.
const TRAY_FEATHER_SOLID_FRACTION := 0.35

## The margin, in panel pixels, the southward bias leaves between the northernmost
## region and the top of the panel. `_held_inside`'s interval end is exact
## containment with nothing to spare, and the runner allows 2 px of rounding; this
## is above that and costs 3 px of the 95 the bias has to spend.
const SOUTH_BIAS_TOP_MARGIN_PX := 3.0

## THE HUD COVERS THE BOTTOM THIRD OF THE PANEL, AND THE FRAMING DOES NOT TRY TO
## DODGE IT. Recorded here because it was investigated and rejected on numbers,
## not overlooked.
##
## MEASURED FROM THIS PROJECT'S OWN HUD at the 2560x1440 capture: the details tray
## stands at y 1035..1426 across x 702..2555 and the palantir dish occupies (0,
## 960) to (960, 1440), so the bottom 480 px of 1440 - exactly a third - is chrome,
## and at the 1860x800 window it is nearer 35%.
##
## FITTING THE BOARD INTO THE VISIBLE FIELD INSTEAD COSTS 40% OF IT. The region
## set's screen footprint is 5195 x 2839 world units; into a 2560x960 field that
## is height-limited and needs a 4,332-unit picture, against the 3,010 the panel
## fit needs - a board 30% smaller than this framing and 21% smaller than round
## 7's. "It feels very cramped" is the complaint this round exists to answer, so
## the panel is what gets fitted.
##
## ROUND 8 SAID THE BIAS COULD NOT BE PAID FOR. ROUND 9 FOUND THE PURSE. What
## follows is the old finding, kept verbatim, and then the thing that was wrong
## with it.
##
## > BIASING THE FRAMING UP INSTEAD BUYS AT MOST 41 PX, AND RETAIL'S SLAB TAKES IT.
## > The containment interval for the centring at 16:9 is +-85.7 lift units, i.e.
## > +-41 px; a full re-centring into the visible field would want 237. And the
## > available direction is the wrong one: the slab's own southern cut sits 182
## > units behind Harad while the picture needs 389 units more depth than the
## > region set occupies, which forces the framing to the NORTHERN end of that
## > interval. So the bias would have to be spent showing the cut edge.
##
## THE ART DIRECTOR'S REVIEW OF THAT FRAME: "You hid Mordor. In a Middle-earth
## strategy layer, Mordor is not a peripheral province, it is the antagonist ... if
## the camera can be biased at all, bias it south." It was right, and the reason
## round 8 could not was a rule applied one rectangle too wide.
##
## THE MEASUREMENT, taken again at 2560x1440 on the shipped bundle. Every region
## mesh's own projected corners, not the framing box's, which is what containment
## is actually about:
##
##   top slack     95.4 px  (MountGundabad is the northernmost thing on screen)
##   bottom slack   7.2 px  (Harad)
##   the slab's southern rim, top face, projects to y 1440.0 - the panel's own
##   last row, which is why round 8 read the interval as unusable.
##
## SO THE PICTURE HAS 95 PX OF ROOM ABOVE IT AND NONE BELOW, and the only thing
## stopping it moving up is that the slab's cut would appear in the bottom band.
## THE BOTTOM BAND IS THE TRAY. `slab_cut_edge_is_in_frame` was asked about the
## whole panel when the property that matters is about the part of the panel the
## player can see - and this view now ALSO GUARANTEES the rest, by inking the
## occluded band itself (`_draw_tray_feather`), so the answer does not depend on
## the HUD actually being there.
##
## WHAT THE BIAS BUYS, measured the same way, at 2560x1440:
##
##   Mordor        region centre  y 1057 -> 969   (under the tray -> clear of it)
##   Minas Tirith                 y 1009 -> 921
##   Osgiliath                    y 1047 ->  959
##   Mount Doom                   y  958 ->  870
##   Harad's lowest corner        y 1433 -> 1345  (still occluded, and it is Harad)
##
## THE FITTING TRADE ITSELF IS UNCHANGED and the numbers above it still stand: the
## board is still fitted to the PANEL, not to the visible field, because fitting
## the visible field still costs 30% of the board. What changed is that the slack
## the fit leaves over is now spent southwards instead of being left in the empty
## ice north of Forodwaith.

var bundle: BundleScript = null
## Retail's per-region territory geometry, when a bundle has been converted.
## Null means regions are drawn as markers only, and the screen says so.
var region_geometry: RegionGeometryScript = null
## Why there is no territory geometry, or "" when there is.
var region_geometry_reason := ""
## Why there is no 3D map, or "" when there is one. Non-empty means this view
## draws the reason instead of a map.
var unavailable_reason := ""

## Regions actually SHADED on the map this frame, and the ones the strategic
## layer knows about that no fill mesh covers. Both public so the screen can
## name the second rather than leave a silent hole in Middle-earth.
var shaded_regions: PackedStringArray = PackedStringArray()
## Regions whose mouseover flare could NOT be given a rim falloff, because retail
## ships them a fill mesh and no border ribbon to measure the rim against. Their
## flare is the flat additive wash this screen drew for seven rounds; it is named
## rather than hidden, and it is EMPTY on the shipped bundle (52/52 regions carry
## both layers).
var flat_flare_regions: PackedStringArray = PackedStringArray()
var unshaded_regions: PackedStringArray = PackedStringArray()
## Regions placed from geometry the converter DERIVED (an area-weighted centroid
## of retail's own fill triangles) rather than from an authored `CenterPoint`.
## Reported separately because the two are different claims.
var centroid_placed_regions: PackedStringArray = PackedStringArray()

## The material on retail's TEXT PLANE (the engraved province lettering), kept
## so the camera can fade the lettering with the zoom: it belongs to the
## strategic framing, and pinned at full strength it would sit under a close-up
## like a watermark. Null when no map (or no text plane) is standing.
var _text_plane_material: StandardMaterial3D = null

## `path -> Shader`, filled by `_shader()` the first time a surface asks for one.
## See `WATER_SHADER_PATH` for why these are not `preload`ed.
var _shaders: Dictionary = {}

var _territory_root: Node3D = null
## `region id -> {fill: MeshInstance3D, fill_material: StandardMaterial3D,
## border: MeshInstance3D}`.
var _territory_nodes: Dictionary = {}

## Region rows as `wotr_session.region_rows()` returns them. Read, never written.
var rows: Array[Dictionary] = []
var selected_region := ""
var selected_target := ""
var hover_region := ""
var targets: PackedStringArray = PackedStringArray()
var staging: PackedStringArray = PackedStringArray()
## The regions retail's `HomeRegionHighlight` is drawn for - a seat's CAPITAL,
## nothing else. Empty until a caller names them, and empty is a NAMED GAP rather
## than a default: see `HOME_REGION_BINDING_GAP`.
var home_regions: PackedStringArray = PackedStringArray()
var neighbours_by_region: Dictionary = {}
var owner_colors: Array[Color] = []
var neutral_color := Color("#5a6656")

## Regions placed on the map, and regions that could not be. Both are public so
## the screen can report the second honestly.
var placed_regions: PackedStringArray = PackedStringArray()
var unplaced_regions: PackedStringArray = PackedStringArray()
## Regions whose height could not be sampled from retail terrain. They are still
## placed - their authored x/y is real - but the screen says the height is not.
var unsampled_heights: PackedStringArray = PackedStringArray()

var viewport_container: SubViewportContainer
var viewport: SubViewport
var world_root: Node3D
var camera: Camera3D
var overlay: Control

## RETAIL'S UI SURFACE - the atlas crops behind every icon - or null. Null means
## banners are drawn as plain plates in the owner's colour and the screen says
## which portraits are absent.
var ui = null
## Why there is no UI bundle, or "" when there is one.
var ui_reason := ""

## `region id -> Array[Dictionary]` of the army stacks standing there, already
## resolved by the screen: `{owner, kind, label, portrait_id, portrait_source}`.
## Read, never written.
var armies_by_region: Dictionary = {}
## `region id -> Array[Dictionary]` of standing structures.  Each row carries
## `{plot, building, icon, owner}` and is already resolved by the screen.
var structures_by_region: Dictionary = {}
## `region id -> Array[Vector2]`, retail's own authored `BuildingSpot` points.
var plots_by_region: Dictionary = {}
## `region id -> LivingWorldBuildPlotIcon id`, resolved by the screen from the
## owning seat's own `BuildPlotIconName`. A region no seat owns carries none.
var plot_icons_by_region: Dictionary = {}
## `region id -> String`, retail's English name when the string table converted.
var display_names: Dictionary = {}
## The plot the radial build menu is open on: `{region, index}` or `{}`.
var selected_plot: Dictionary = {}
## The plot the POINTER is over, as `{region, index}`, or `{}`. Separate from
## `selected_plot`, because retail draws two different things for the two states
## and gates them on two different fields.
##
## WHY THIS EXISTS. Retail authors every one of its seven
## `LivingWorldBuildPlotIcon` families with a `HilightedRing` slot carrying the
## model `ArmyAntsLoc` and `HideWhenUnhilighted = Yes`. The slot was converted,
## `_slot_is_showing()` already honours the field, and the ring still never
## appeared - because hover was only ever tracked per REGION and the plot stand
## was handed a flat `false`. So retail's own hover art for a build plot was
## present in the bundle, understood by the code, and unreachable by the pointer.
var hover_plot: Dictionary = {}
## What that menu offers: `[{id, image_id, title, cost, turns}]`, supplied by the
## screen from retail's own `LivingWorldBuilding` records.
var radial_entries: Array[Dictionary] = []
## The build-ring entry id under the pointer, or "". PRESENTATION ONLY, and it is
## the whole of "the icons light up" - see `build_entry_clicked`.
var hover_build_entry := ""

## RETAIL'S OWN 3D MARKER MODELS - the banners, the marching columns and the
## foundation decals - or null. Null means armies keep their flat portrait plates
## and plots their flat rings, and the screen says so.
var markers = null
## Why there are no marker models, or "" when there are.
var markers_reason := ""
## The node holding every marker standing in the world this frame.
var _marker_root: Node3D = null
## `Array[Dictionary]` of what is standing: {key, kind, node, aabb}. Rebuilt
## whenever the overlays change; read by the label placer so a region name is
## never written across a banner.
var _standing_markers: Array[Dictionary] = []
## The marker keys whose BODY slot actually stood, so the overlay knows which
## flat plates and rings it must NOT also draw. A marker is drawn once.
var _standing_keys: Dictionary = {}

## How many army stacks are standing as retail's own 3D banner, and how many fell
## back to a flat plate WITH THE REASON. Both public, because "the markers are 3D"
## and "every marker is 3D" are two different claims.
var army_markers_standing := 0
var army_markers_flat: Dictionary = {}
## How many marker meshes were drawn in the OWNING SEAT'S colour rather than in
## retail's own texture, because retail flat-shades them from its house-colour
## swatch and authored `UseHouseColor = Yes` on the slot. Public, because "the
## field is read" and "the field changed a pixel" are two different claims. See
## `wotr_marker_models.HOUSE_COLOUR_GAP`.
var house_coloured_meshes := 0
## The same for build plots.
var plot_markers_standing := 0
var plot_markers_flat: Dictionary = {}
## The same accounting for structures that actually exist in strategic state.
var structure_markers_standing := 0
var structure_markers_flat: Dictionary = {}

## Army stacks whose portrait did not resolve, `army label -> reason`. Public so
## the screen can name every banner drawn as a bare faction plate.
var banners_without_portrait: Dictionary = {}
## How many banners were drawn, and how many region labels were held back because
## they collided with one already placed. Both reported rather than assumed.
var banners_drawn := 0
## OF THOSE, HOW MANY ARE THE FLAT STAND-IN PLATE and how many are a general's
## medallion hung on retail's own standing model. `banners_drawn` is the total,
## because "how many armies are marked" is what the screen reports; these two say
## WHICH KIND, which is a different question and the one a test needs. A flat
## plate over a model that DID stand is a defect - two things claiming to be one
## army - and it cannot be seen in the total.
var flat_banners_drawn := 0
var medallions_drawn := 0
var labels_drawn := 0
var labels_suppressed := 0
## Labels held back because the camera is at the strategic framing, where
## retail letters the map with the engraved TEXT PLANE alone. Counted apart
## from `labels_suppressed` (collisions) because "there was no room" and "the
## framing does not show names" are two different reasons.
var labels_held_for_framing := 0

var _world_positions: Dictionary = {}
var _screen_positions: Dictionary = {}
## `region id -> Array[Vector2]` in screen space, recomputed every draw.
var _plot_screen_positions: Dictionary = {}
## Screen-space rectangles the banners occupy this frame. Labels are placed
## AROUND them: a region name written across a portrait costs both.
var _banner_boxes: Array[Rect2] = []
## Mesh instances actually put in the world by the last `_rebuild_world()`.
## Reported rather than assumed: "the map loaded" and "the map is on screen" are
## two different claims and only the second one is what the player sees.
var _drawn_count := 0
## Presentation surfaces that REFUSED to stand because a stage texture their
## shader composites did not resolve, `sub-object name -> reason`. Public so
## the screen can name them; nothing is ever substituted for them.
var surfaces_refused: Dictionary = {}
var _camera_target := Vector3.ZERO
## THE ORTHOGRAPHIC SIZE THAT FRAMES THE WHOLE MAP - the world height, in retail
## units, that the panel spans at `_zoom` = 1. Named `_camera_distance` still, and
## reported under that key by `camera_state()`, because it is the same thing it
## always was in every way that matters to a caller: the FIT SCALAR the player's
## zoom multiplies, solved from retail's own extent against the live aspect,
## pitch and yaw, and re-solved on a resize. Under a perspective lens the fit
## scalar happened to be a distance; under a parallel one it is a size. Nothing
## outside `_apply_camera` ever treated it as a length.
var _camera_distance := 1.0
## How far back the camera physically stands. Under a parallel projection this
## changes NOTHING about the picture - see `CAMERA_STANDOFF_SPAN` - so it is kept
## apart from the fit scalar above, and unlike it, it is NOT multiplied by the
## zoom: a standoff that shrank with the zoom would put the map's far half behind
## the near plane at the closest framings.
var _camera_standoff := 1.0
## How far the framing shifts the camera and its look-at point across the screen
## (`x`) and up it (`y`), in world units at zoom 1, so retail's map sits in the
## MIDDLE of the panel. A pitched plane projects its near half larger than its
## far half, so a camera aimed at the map's own centre frames it low - and, once
## the camera is turned, off to one side as well. This is the correction. It is
## solved by `_fit_distance()` from retail's own extent, and it is the FRAMING
## rather than the player's pan: `_pan()` never writes it and `_camera_target`
## never carries it.
var _framing_offset := Vector2.ZERO
## The live value of `zoom_ceiling()`, recomputed whenever the fit, the pan or
## the panel changes. Kept so the zoom-dependent presentation bands (the
## engraved lettering's fade, the floating-label reveal) can be expressed as a
## FRACTION of the reachable pull-back instead of as absolute zooms tuned at one
## window shape - the exact mistake `MAX_ZOOM` used to make.
var _zoom_ceiling := MAX_ZOOM
## THE ZOOM THE PLAYER ASKED FOR, kept SEPARATE from the zoom the camera is
## actually at - and that separation is a bug fix, not bookkeeping.
##
## The ceiling is solved per frame against the LIVE aspect, so it MOVES when the
## window does. Storing only the clamped value threw the request away: a player
## who pulled all the way out on a narrow panel was held at that panel's ceiling
## (0.2564), and when the window widened and the ceiling rose to 0.3131 the zoom
## stayed at 0.2564. The framing the old window imposed silently became the
## framing the player had "chosen", and no resize could ever give back the
## pull-back he had actually asked for. Every request is now remembered here and
## RE-RESOLVED against the ceiling on every change to the fit, the pan, the orbit
## or the panel.
##
## IT DOES NOT YANK A DELIBERATE ZOOM-IN OUTWARD, because the resolve is a clamp
## and not a restore: `_zoom = clamp(_zoom_request, MIN_ZOOM, ceiling)`. A player
## sitting at 0.10 has a request of 0.10, which is under every ceiling this view
## can compute, so it resolves to 0.10 at every panel shape and the resize is
## invisible to him. Only a request the ceiling is CURRENTLY cutting short - one
## strictly above the ceiling, i.e. one the player can see is not being honoured
## - can move, and it can only move towards what he asked for.
##
## The wheel steps from `_zoom` rather than from this, deliberately: a request of
## 0.88 pinned to a ceiling of 0.2564 would otherwise need six notches inward
## before the picture changed at all.
var _zoom_request := DEFAULT_ZOOM
var _zoom := DEFAULT_ZOOM
var _yaw := 0.0
## The live pitch. `DEFAULT_PITCH_DEGREES` is now only the value this opens at
## and returns to on reset; the player owns it after that, and `_fit_distance()`
## fits against THIS rather than the constant so the both-axes fit stays correct
## at any angle.
var _pitch_degrees := DEFAULT_PITCH_DEGREES
var _dragging := false
## True while the drag is orbiting rather than panning.
var _orbiting := false
## A short right click is a strategic order; a right drag remains camera pan.
## Keeping the distance explicitly is what prevents a shaky mouse release from
## marching an army after the player has just moved the camera.
const RIGHT_CLICK_DRAG_THRESHOLD := 7.0
var _right_press_position := Vector2.ZERO
var _right_drag_distance := 0.0

## WHETHER THE HELD-KEY AND SCREEN-EDGE CAMERA DRIVES ARE LIVE. Public and true,
## so a capture runner or a test that wants a camera that stays exactly where it
## was put can turn them off rather than fight a `_process` that keeps moving it.
## Both are presentation and reach nothing.
var keyboard_pan_enabled := true
var edge_scroll_enabled := true
## Default letter/arrow map the camera pan still means. Runtime drive reads
## remappable cam_* InputMap actions (those defaults plus the left stick).
## Q/E stay unbound here — orbit is middle-drag.
const PAN_KEYS := {
	KEY_W: Vector2(0.0, 1.0), KEY_UP: Vector2(0.0, 1.0),
	KEY_S: Vector2(0.0, -1.0), KEY_DOWN: Vector2(0.0, -1.0),
	KEY_A: Vector2(-1.0, 0.0), KEY_LEFT: Vector2(-1.0, 0.0),
	KEY_D: Vector2(1.0, 0.0), KEY_RIGHT: Vector2(1.0, 0.0),
}


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	# THE HELD-KEY PAN IS POLLED, NOT EVENT-DRIVEN, and that is the whole reason
	# this control processes at all. A key that is HELD produces one pressed event
	# and then nothing until it is released, so a camera driven from
	# `_gui_input` would lurch once per keypress instead of gliding; and the
	# strategic screen's own buttons take focus away from this control the moment
	# one is clicked, after which no key event would reach it again at all.
	set_process(true)
	if viewport_container == null:
		build()


func build() -> void:
	viewport_container = SubViewportContainer.new()
	viewport_container.name = "MapViewport"
	# The viewport is sized explicitly to match this control, so the overlay's
	# 2D coordinates and the camera's projected coordinates are the SAME pixels.
	# With `stretch` on, the container would resize the viewport itself and a
	# marker could drift from the ground it is standing on.
	viewport_container.stretch = false
	viewport_container.set_anchors_preset(Control.PRESET_FULL_RECT)
	viewport_container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(viewport_container)

	viewport = SubViewport.new()
	viewport.name = "MapWorld"
	viewport.transparent_bg = false
	viewport.handle_input_locally = false
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	# MULTISAMPLED, AT THE PROJECT'S OWN SETTING. `project.godot` asks for
	# `anti_aliasing/quality/msaa_3d=2` (4x), but that setting reaches the ROOT
	# window's viewport only - a SubViewport carries its own `msaa_3d` and
	# defaults it to DISABLED. So every 3D pixel on this screen was the one
	# unantialiased 3D surface in the game, and it showed exactly where a
	# one-pixel-wide surface lives: retail's territory border strip is a ~6.5
	# world-unit ribbon on a map 6,021 units wide, which is a hairline at the
	# strategic framing, and unantialiased it stair-stepped on every diagonal.
	# Read from the project setting rather than restated, so the two cannot
	# drift apart.
	viewport.msaa_3d = int(ProjectSettings.get_setting(
		"rendering/anti_aliasing/quality/msaa_3d", Viewport.MSAA_4X)) as Viewport.MSAA
	viewport_container.add_child(viewport)

	world_root = Node3D.new()
	world_root.name = "LivingMap"
	viewport.add_child(world_root)

	camera = Camera3D.new()
	camera.name = "MapCamera"
	# RETAIL'S OWN LENS, solved out of the oracle: PARALLEL. See
	# `CAMERA_FOV_DEGREES` for the fit that measured it and for what the 12-degree
	# frustum was an approximation of. `size` is written by `_apply_camera` from
	# the fit and the player's zoom; this is only the initial value.
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 1.0
	camera.near = 1.0
	camera.far = 60000.0
	viewport.add_child(camera)

	# Retail lights the living map warmly from the south-west, low enough that
	# the Misty Mountains and the Ephel Duath throw readable relief. This is one
	# warm key light with shadows and a cool ambient fill - not retail's exact
	# lighting rig, and not claimed to be, but the same CHARACTER: a low warm sun
	# against a cold sky, which is what every reference capture shows.
	var light := DirectionalLight3D.new()
	light.name = "MapSun"
	# RAISED WITH THE AMBIENT DROP BELOW, so the map's MEAN brightness is held
	# where the art direction review liked it and only the SHADOWS move. Round 6
	# took it 1.26 -> 1.74 against the same measurement, for the same reason: see
	# `ambient_light_energy`.
	light.light_energy = 1.74
	light.light_color = Color(1.0, 0.94, 0.82)
	# Pitched -38 so slopes facing away from the sun genuinely shade, yawed so
	# the sun sits WSW of Middle-earth the way the retail captures read.
	light.rotation_degrees = Vector3(-38.0, -52.0, 0.0)
	# Self-shadowing is what makes a mountain range read as standing geometry
	# rather than a printed texture, and it is what carries the relief now that the
	# projection is parallel and no keystone does (see `CAMERA_FOV_DEGREES`). The
	# distance covers the whole-map framing with room to spare: the camera stands
	# `CAMERA_STANDOFF_SPAN` times the map's own depth back from what it looks at,
	# which is ~7,700 world units at the opening pitch.
	light.shadow_enabled = true
	light.directional_shadow_max_distance = 24000.0
	# THE THREE SPLIT PLANES MUST ALL BE DIFFERENT, AND LEAVING THE THIRD ALONE IS
	# WHAT MADE THIS SCREEN UNPLAYABLE. This is the single most expensive line in
	# the War of the Ring lane and it was an omission, not a choice.
	#
	# Godot's parallel-split shadow map slices the camera's depth range at these
	# three fractions and renders the scene once per slice, so the four slice
	# projections are built from consecutive pairs of
	# `[near, split_1, split_2, split_3, far]`. The engine's OWN DEFAULTS are
	# 0.1 / 0.2 / 0.5. This code set `split_1` to 0.2 and `split_2` to 0.5 and said
	# nothing about `split_3` - which therefore stayed at 0.5, i.e. AT `split_2`.
	# The fourth slice was then built between two planes at the same depth, its
	# projection divided by a zero depth range, and every entry in the matrix came
	# out non-finite.
	#
	# What that cost: `Projection::get_endpoints` failed on the NaN planes, and
	# Godot logged six warnings and errors - seventeen lines - EVERY FRAME, FOREVER.
	# Measured through this lane's `tests/wotr_perf_runner.gd` on the real screen,
	# mounted through the real menu:
	#
	#     four splits, split_3 unset (0.5)   147.29 ms/frame     6.8 fps
	#     four splits, split_3 = 0.8           1.16 ms/frame   858   fps
	#
	# The frame time is not the shadow. It is the ENGINE PRINTING, and it scales
	# with how expensive the process's stderr is: ~5 ms/frame writing to a file,
	# ~147 ms/frame writing to a pipe or a console - which is what the owner, who
	# launches through `run_game.bat`, actually had. That is the "super laggy to
	# play" report, in full.
	#
	# 0.8 is chosen as the obvious continuation of 0.2 / 0.5 and only has to satisfy
	# `split_2 < split_3 < 1`; the split planes are then strictly increasing by
	# construction and no pair can ever collapse at any zoom or orbit. Four splits
	# are KEPT rather than reduced because a picture-diff over the same framing put
	# the repaired four-split frame nearest the one a blind review liked (mean
	# absolute difference 0.41/255 against the broken frame, versus 0.68 at two
	# splits, 0.91 at one and 1.04 with the shadow off) at no measurable cost.
	light.directional_shadow_split_1 = 0.2
	light.directional_shadow_split_2 = 0.5
	light.directional_shadow_split_3 = 0.8
	light.shadow_normal_bias = 3.0
	viewport.add_child(light)

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	# The void beyond retail's border cloud: deep night-sea blue rather than
	# black, so the map edge fades into weather instead of ending on a cut.
	environment.background_color = Color(0.020, 0.032, 0.052)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	# Cool fill against the warm key - the two-tone rig every painted map in
	# this franchise uses. Bright enough that shadowed slopes stay readable art.
	environment.ambient_light_color = Color(0.55, 0.58, 0.66)
	# 0.60 -> 0.44, AND THE NUMBER IS A MEASUREMENT, not a preference. A blind
	# review of round 4 called this terrain "flatter, lower in contrast" than the
	# shipped game's, so the two frames were measured over the same rectangle of
	# central Middle-earth (x 1500-2400, y 420-980 of the 2560x1440 captures, which
	# is clear of chrome in both):
	#
	#            mean   std    5th pct   95th pct
	#   ours     87.7   34.2     28.2      142.7
	#   retail   80.1   41.9     13.8      150.2
	#
	# The means are within 8 levels - the exposure was never the problem - and the
	# whole difference is in the FIFTH PERCENTILE: retail's darks reach 14 and this
	# map's bottomed out at 28. A floor exactly there is what a flat ambient fill
	# IS: nothing lit only by the fill can go below `ambient_energy * albedo`, so
	# 0.60 of a 0.55-0.66 fill was a 14-level pedestal under the whole of
	# Middle-earth. Dropping it is what lets the Misty Mountains' shadowed faces and
	# the Ephel Duath actually go dark; the key light above is raised to match so
	# the midtones the same review praised do not move with them.
	environment.ambient_light_energy = 0.14
	# ROUND 6 FINISHED THE JOB, against the same window and the same statistics.
	# Re-measured at 2560x1440 over x 1500-2400, y 420-980 with the framing above
	# (which is what makes the two frames comparable at all - see
	# `CAMERA_FOV_DEGREES`):
	#
	#             mean   std    p5    p25   p50   p95
	#   round 5   79.6   33.2   29    55    78    136
	#   round 6   80.1   41.5   15    48    77    152
	#   retail    80.3   42.2   14    47    79    152
	#
	# All six now land on retail's. The move was 0.44 -> 0.14 on the fill with the
	# key raised 1.26 -> 1.74 to hold the mean, plus the grade contrast below - and
	# the split between those two is not arbitrary. The fill sets the FLOOR:
	# nothing lit only by fill can go below `ambient_energy * albedo`, which is
	# exactly the 29-level pedestal round 5 still had against retail's 14. The
	# grade widens the whole distribution. Tried with the grade alone, the same
	# standard deviation only came at the price of the mean, which fell to 74.
	#
	# NOTHING IN THE BAND APPARATUS MOVED. The HDR values the ownership bands are
	# written at and the 1.25 bloom threshold that catches them are untouched, and
	# the grade is applied after the tonemapper so it cannot reach them.
	# LINEAR tonemap, on purpose. Filmic compressed the painted art's midtones
	# into mud - the whole of Middle-earth read as dusk. The energies above are
	# balanced so flat ground under sun-plus-fill sits at ~1.0, i.e. the texture
	# as the artist painted it, with the sun adding relief either side of that.
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	# A light grade towards the reference captures' saturation: the cool fill
	# above desaturates the painted greens slightly, and this puts them back.
	environment.adjustment_enabled = true
	environment.adjustment_saturation = 1.12
	environment.adjustment_brightness = 1.00
	# THE SECOND HALF OF THE CONTRAST MEASUREMENT above. Deepening the shadows with
	# the ambient gets the 5th percentile down; this is what puts the standard
	# deviation back up across the whole range, which is the other number retail
	# beat this map on (41.9 to 34.2). Applied AFTER the tonemapper, so it cannot
	# touch the HDR values the ownership bands are written at and the bloom
	# threshold that catches them is unmoved - the band apparatus round 4 measured
	# is deliberately outside this grade.
	environment.adjustment_contrast = 1.26
	# BLOOM, AND WHAT IT IS FOR. Retail's ownership outlines read THICKER than
	# their geometry is: `lmr_border.w3d` is a ribbon ~6.5 world units across on
	# a 6,021-unit map - about two pixels at the strategic framing - and in the
	# reference capture the magenta of Rhun and the orange north of Eriador are
	# four or five pixels of saturated colour with a soft shoulder either side.
	# That shoulder is a BLOOM, not extra geometry, and this is the same bloom:
	# the border band is written into the HDR buffer above 1.0 (see
	# `_band_color`), the threshold is set so nothing the painted terrain can
	# produce reaches it, and only the bands glow. The terrain grade the art
	# direction review called "the strongest thing in the exhibit" is therefore
	# untouched by it - which is the whole reason for the high threshold rather
	# than a pretty full-screen glow.
	environment.glow_enabled = true
	environment.glow_intensity = 0.85
	environment.glow_strength = 1.05
	environment.glow_bloom = 0.0
	environment.glow_blend_mode = Environment.GLOW_BLEND_MODE_ADDITIVE
	# 1.25 is above anything the lit terrain reaches: flat ground under sun plus
	# fill is balanced to ~1.0 by the energies above, and the linear tonemap does
	# not lift it. The bands are driven past it on purpose.
	environment.glow_hdr_threshold = GLOW_HDR_THRESHOLD
	environment.glow_hdr_scale = 2.0
	# The wide levels only. A one- or two-pixel band blooming into levels 1-2
	# would be a halo the width of the band itself; the shoulder wanted is three
	# to four pixels, which is what levels 2 and 3 give at these panel sizes.
	environment.set_glow_level(1, 0.0)
	environment.set_glow_level(2, 0.9)
	environment.set_glow_level(3, 0.45)
	# Level 4 is a quarter-resolution blur - a halo tens of pixels wide. It was
	# tried at 0.25 and it did not read as a shoulder, it read as the whole
	# province being lit from underneath. Off.
	environment.set_glow_level(4, 0.0)
	var camera_attributes := CameraAttributesPractical.new()
	camera.environment = environment
	camera.attributes = camera_attributes

	overlay = Control.new()
	overlay.name = "MapOverlay"
	overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# CLIPPED TO THE MAP PANEL. A region whose marker projects outside the
	# viewport is still a legitimate projection - the camera can be panned or
	# zoomed so that half of Middle-earth is off the panel - but the label and the
	# graph edge that go with it must not be painted across the rest of the
	# screen. Without this, panning south wrote Mordor, Ithilien and Belfalas over
	# the seat table and the buttons.
	overlay.clip_contents = true
	overlay.draw.connect(_draw_overlay)
	add_child(overlay)

	resized.connect(_on_resized)
	_on_resized()


## Bind a loaded bundle, or none plus the reason. Both are legitimate states -
## and BOTH are said out loud. This view used to contain no print, warning or
## error of any kind, so a failure to build retail's map produced a screen that
## looked slightly wrong and a log that said nothing at all; the only way to find
## it was to notice. That is the failure this logging exists to make impossible.
func set_bundle(loaded_bundle, reason: String) -> void:
	if viewport_container == null:
		build()
	bundle = loaded_bundle
	unavailable_reason = reason
	_framing_box_cache = {}
	_rebuild_rim_segments()
	_rebuild_world()
	_frame_camera()
	_redraw()
	if not has_map():
		push_warning("[WotrMap3D] no retail map bound; this view will draw its refusal instead. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO 3D MAP. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	print("[WotrMap3D] drawing %d of %d retail sub-objects (%d held back: impassable volumes, the RIVERS underlay, the lava frame-sequence planes, and the vertex-alpha smoke cards). WATER stands under a procedural water shader (the same claim retail's WaterShader.FX makes); BORDERCLOUD and LM_CLOUDLAYER drift under an animated cloud shader; the LM_COAST strips composite retail's two shoreline stages; Mount Doom's PLANE03 cross-fades retail's two frames inside frame 0's own alpha; TEXT PLANE is retail's engraved lettering, faded with the zoom." % [
		_drawn_count, bundle.sub_objects.size(), bundle.sub_objects.size() - _drawn_count])
	if not surfaces_refused.is_empty():
		for key in surfaces_refused.keys():
			push_warning("[WotrMap3D] surface %s NOT DRAWN: %s" % [
				key, surfaces_refused[key]])


## Bind retail's region territory geometry, or none plus the reason. Separate
## from `set_bundle` because the two bundles fail independently: retail's map can
## be present with no territory shapes converted, and that is a legitimate state
## the screen reports rather than hides.
func set_region_geometry(geometry, reason: String) -> void:
	if viewport_container == null:
		build()
	region_geometry = geometry
	region_geometry_reason = reason
	# THE FRAMING IS A FUNCTION OF THIS BUNDLE NOW (see `_framing_box`), so binding
	# it invalidates the box and re-frames. Before round 8 the camera was fitted
	# once, in `set_bundle`, and the region bundle could not move it; it now
	# decides what the camera is aimed at, and a view that kept the slab framing
	# after the regions arrived would open on the wrong picture. This is a LOAD-TIME
	# bind - the screen calls it once, before the player has touched the camera -
	# so re-framing here discards nothing the player chose.
	_framing_box_cache = {}
	_rebuild_territories()
	_recompute_world_positions()
	_apply_territory_colors()
	_frame_camera()
	_redraw()
	if not has_territories():
		push_warning("[WotrMap3D] no region territory geometry; regions are drawn as markers. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO TERRITORY SHADING. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	print("[WotrMap3D] territory shading from retail geometry: %d regions filled, %d bordered, %d selection edges, %d home highlights, %d triangles" % [
		shaded_regions.size(), _bordered_count(), selection_edge_count(),
		home_highlight_count(), region_geometry.total_triangles])
	print("[WotrMap3D]   mouseover flare falloff baked for %d of %d region(s) in %.0f ms%s" % [
		shaded_regions.size() - flat_flare_regions.size(), shaded_regions.size(),
		region_geometry.falloff_build_ms,
		"" if flat_flare_regions.is_empty()
			else "; FLAT (no border ribbon to measure a rim against): "
				+ ", ".join(flat_flare_regions)])
	for line in region_geometry.describe_load():
		print("[WotrMap3D]   %s" % line)
	# WHAT RETAIL'S OWN EFFECT MESHES CANNOT BE DRAWN FOR, in the converter's own
	# words, printed every time the territories stand rather than recorded in a
	# comment nobody runs. Both are EMPTY on the shipped bundle - 52/52 regions
	# carry an `LMR_Edge` and an `LMR_Highlight` - and they are still printed when
	# they are not, because a bundle rebuilt without those layers must not lose the
	# selection ring silently.
	var selection_gap: String = region_geometry.selection_geometry_gap()
	if not selection_gap.is_empty():
		print("[WotrMap3D]   GAP: %s" % selection_gap)
	var highlight_gap: String = region_geometry.home_highlight_gap()
	if not highlight_gap.is_empty():
		print("[WotrMap3D]   GAP: %s" % highlight_gap)
	for line in region_geometry.named_gap_lines():
		print("[WotrMap3D]   converter gap: %s" % line)
	# AND THE ONE THIS VIEW OWNS: the highlight mesh is bound and drawable, and
	# nothing has told this view which region is a seat's home.
	if home_regions.is_empty():
		print("[WotrMap3D]   GAP: %s" % HOME_REGION_BINDING_GAP)


func has_map() -> bool:
	return bundle != null and bundle.loaded


func has_territories() -> bool:
	return region_geometry != null and region_geometry.loaded and not shaded_regions.is_empty()


func _bordered_count() -> int:
	return _slot_count("border")


## How many regions carry retail's own `RegionSelectionEffect` mesh, standing and
## ready to be lit. Counted from the NODES that were actually built, not from the
## bundle's claim, so a mesh that failed to instance cannot be counted as drawn.
func selection_edge_count() -> int:
	return _slot_count("selection_edge_material")


## How many regions carry retail's own `HomeRegionHighlight` mesh. Same
## discipline; see `HOME_REGION_BINDING_GAP` for why a non-zero count here does
## not mean anything is visible.
func home_highlight_count() -> int:
	return _slot_count("home_highlight_material")


func _slot_count(key: String) -> int:
	var count := 0
	for region_id in _territory_nodes.keys():
		if (_territory_nodes[region_id] as Dictionary).has(key):
			count += 1
	return count


## How many retail sub-objects are actually standing in the 3D world right now.
## `has_map()` says the bytes parsed; this says something is on screen.
func drawn_mesh_count() -> int:
	return _drawn_count


## Feed the view the strategic picture. Pure presentation: nothing here is
## written back, and the arrays are the screen's own already-computed ones.
## `home` is OPTIONAL and defaults to empty, so every existing caller keeps the
## behaviour it had; passing it is what makes retail's `HomeRegionHighlight`
## visible. It is the seat capitals (`state.players[].capital`), and this view
## refuses to derive them - `HOME_REGION_BINDING_GAP` says why.
func set_regions(
	region_rows: Array[Dictionary],
	adjacency: Dictionary,
	staged: PackedStringArray,
	attack_targets: PackedStringArray,
	selection: String,
	target: String,
	home: PackedStringArray = PackedStringArray()
) -> void:
	rows = region_rows
	neighbours_by_region = adjacency
	staging = staged
	targets = attack_targets
	selected_region = selection
	selected_target = target
	home_regions = home
	_recompute_world_positions()
	_apply_territory_colors()
	_rebuild_markers()
	_redraw()


## Bind retail's UI surface, or none plus the reason. Separate from the map and
## the territory bundles because it fails independently: Middle-earth can be on
## screen, shaded, with no portrait atlas converted, and that is a state the view
## reports rather than papers over.
func set_ui(loaded_ui, reason: String) -> void:
	ui = loaded_ui
	ui_reason = reason
	_redraw()
	if not has_ui():
		push_warning("[WotrMap3D] no living-world UI bundle; army banners carry no portraits. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO PORTRAITS. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	for line in ui.describe_load():
		print("[WotrMap3D]   ui: %s" % line)


func has_ui() -> bool:
	return ui != null and ui.loaded


## Bind retail's 3D marker models, or none plus the reason. Separate from the
## map, the territory and the UI bundles for the same reason those three are
## separate from each other: they fail independently, and a screen that could
## only say "something is missing" would be no help at all.
func set_markers(loaded_markers, reason: String) -> void:
	if viewport_container == null:
		build()
	markers = loaded_markers
	markers_reason = reason
	_rebuild_markers()
	_redraw()
	if not has_markers():
		push_warning("[WotrMap3D] no marker models; armies are flat plates and plots are flat rings. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO 3D MARKERS. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	for line in markers.describe_load():
		print("[WotrMap3D]   markers: %s" % line)


func has_markers() -> bool:
	return markers != null and markers.loaded


## Feed the view the army stacks, the build plots and the region labels. All
## three are already resolved by the screen; nothing here reads the simulation.
func set_overlays(
	army_rows: Dictionary,
	plot_rows: Dictionary,
	labels: Dictionary,
	plot_selection: Dictionary,
	menu_entries: Array[Dictionary],
	plot_icon_rows: Dictionary = {},
	structure_rows: Dictionary = {}
) -> void:
	armies_by_region = army_rows
	plots_by_region = plot_rows
	plot_icons_by_region = plot_icon_rows
	structures_by_region = structure_rows
	display_names = labels
	selected_plot = plot_selection
	radial_entries = menu_entries
	# A RING THAT CLOSED CANNOT STILL HAVE A SLOT UNDER THE POINTER, and a stale
	# id would light whichever slot of the NEXT ring happened to share it.
	if radial_entries.is_empty() or selected_plot.is_empty():
		hover_build_entry = ""
	_recompute_plot_world_positions()
	_rebuild_markers()
	_redraw()


## Ask for a repaint. THE OVERLAY IS THE THING THAT DRAWS, and it is a CHILD
## Control with its own `draw` signal - so `queue_redraw()` on this node marked
## a node that paints nothing and the markers, rings and labels only ever
## appeared on the very first frame. Every selection, hover and territory change
## after that redrew nothing at all. Every request goes through here now, so the
## two can never drift apart again.
func _redraw() -> void:
	queue_redraw()
	if overlay != null:
		overlay.queue_redraw()


func _on_resized() -> void:
	if viewport == null:
		return
	var view_size := size
	if view_size.x < 1.0 or view_size.y < 1.0:
		view_size = Vector2(1.0, 1.0)
	viewport.size = Vector2i(int(view_size.x), int(view_size.y))
	# The fit depends on the viewport's ASPECT, so a resize has to redo it. Only
	# the distance is recomputed: where the camera is looking and how far the
	# player has zoomed are his, and a resize must not throw them away.
	_fit_distance()
	_clamp_zoom()
	_apply_camera()
	_redraw()


# --- the 3D world -------------------------------------------------------------

func _rebuild_world() -> void:
	if world_root == null:
		return
	_drawn_count = 0
	_text_plane_material = null
	surfaces_refused = {}
	for child in world_root.get_children():
		world_root.remove_child(child)
		child.queue_free()
	if not has_map():
		return
	for entry in bundle.sub_objects:
		# Retail's impassable volumes were never meant to be seen, RIVERS needs
		# terrain alpha holes this lane's tiles do not cut, and the lava planes
		# are frame-sequence sheets whose schedule the bundle does not carry.
		# All are LOADED and reported; they are simply not drawn, which is a
		# presentation choice this file states rather than a gap it hides.
		if bool(entry["collision"]) or bool(entry["ambient"]) or bool(entry["shader_only"]):
			continue
		var material := _surface_material(entry)
		if material == null:
			# A presentation surface whose stage textures did not all resolve.
			# `_surface_material` recorded the reason; nothing stands in for it.
			continue
		var instance := MeshInstance3D.new()
		instance.name = String(entry["name"])
		instance.mesh = entry["mesh"]
		instance.material_override = material
		if String(entry.get("surface", "")) == "coast":
			# See COAST_HEIGHT_BIAS: lifted over the animated sea that would
			# otherwise swallow the foam by a fraction of a unit.
			instance.position = Vector3(0.0, COAST_HEIGHT_BIAS, 0.0)
		world_root.add_child(instance)
		_drawn_count += 1
	# The territories and the markers are rebuilt with the world, because clearing
	# `world_root` above destroyed the nodes that held them.
	_territory_root = null
	_territory_nodes = {}
	_marker_root = null
	_standing_markers = []
	# The reconciliation ledger points at nodes `world_root` has just destroyed.
	_marker_holders = {}
	# The plot heights are sampled from the terrain this just rebuilt.
	_recompute_plot_world_positions()
	_rebuild_territories()
	_rebuild_markers()


## One of the four presentation shaders, loaded on first use and cached. See
## `WATER_SHADER_PATH` for why this exists instead of four `preload`s: a script
## that preloads a shader cannot be compiled off the main thread, which took
## `boot.tscn` off the threaded-load path and froze the loading bar for seconds.
##
## A shader that will not load is a HARD stop for its surface - the caller records
## the refusal and draws nothing there - because a presentation surface with no
## treatment is exactly the flat grey rectangle this lane refuses to ship.
func _shader(path: String) -> Shader:
	if _shaders.has(path):
		return _shaders[path] as Shader
	var loaded: Shader = load(path) as Shader
	if loaded == null:
		push_error("[WotrMap3D] %s did not load as a Shader; the surface it treats draws nothing." % path)
	_shaders[path] = loaded
	return loaded


## The material a sub-object actually stands under. Ordinary geometry keeps the
## bundle's own lit textured material; the four presentation surfaces get the
## treatment `PRESENTATION_SURFACES` names for them. Every branch here draws
## retail's own geometry - the only thing chosen is the shading, and each shader
## states what it claims.
## Returns null - and records why in `surfaces_refused` - when a presentation
## surface's shader needs a stage texture the bundle did not resolve. The
## caller then draws NOTHING there; a one-stage approximation of a two-stage
## material is exactly the defect that kept the coasts held back for a round.
func _surface_material(entry: Dictionary) -> Material:
	var surface := String(entry.get("surface", ""))
	if surface.is_empty():
		return entry["material"]
	# THE SHADER IS RESOLVED FIRST AND ITS ABSENCE IS A REFUSAL, not a fallback.
	# `_shader` loads at runtime now (see `WATER_SHADER_PATH`), so "the file is
	# missing" is a state that can reach here at all - and an untreated WATER
	# plane is a flat blue sheet, an untreated BORDERCLOUD a white ring. Both are
	# worse than the nothing the caller draws when this returns null.
	if surface != "text":
		var wanted := String({
			"coast": COAST_SHADER_PATH, "smoke": SMOKE_SHADER_PATH,
			"water": WATER_SHADER_PATH, "cloud": CLOUD_SHADER_PATH,
		}.get(surface, ""))
		if wanted.is_empty() or _shader(wanted) == null:
			surfaces_refused[String(entry["name"])] = (
				"the %s surface's shader (%s) did not load, so nothing is drawn for it"
				% [surface, wanted if not wanted.is_empty() else "unnamed"])
			return null
	if surface == "coast":
		# Retail's two authored stages, by their declared names. See the coast
		# shader's header for what each stage is and how they compose.
		var stages := entry.get("stage_textures", {}) as Dictionary
		var foam: Texture2D = stages.get("WtrWhitecap_01.tga", null)
		var sky: Texture2D = stages.get("SkyBoxNightClouds.tga", null)
		if foam == null or sky == null:
			surfaces_refused[String(entry["name"])] = (
				"the coast shader composites retail's two stages and stage %s did not resolve"
				% ("1 (WtrWhitecap_01)" if foam == null else "0 (SkyBoxNightClouds)"))
			return null
		var coast := ShaderMaterial.new()
		coast.shader = _shader(COAST_SHADER_PATH)
		coast.set_shader_parameter("whitecap", foam)
		coast.set_shader_parameter("reflection", sky)
		# Over the water surface (priority 0), under the territory art.
		coast.render_priority = 1
		return coast
	if surface == "smoke":
		# Mount Doom's two frames, in the row's own declared order (`LM_Doom01`
		# then `LM_Doom02`). Frame 0's authored DXT5 alpha is the plume's
		# shape; see the smoke shader's header for why only this card can be
		# composed from shipped bytes.
		var stages := entry.get("stage_textures", {}) as Dictionary
		var order: Array = entry.get("stage_order", []) as Array
		if order.size() < 2 or not stages.has(String(order[0])) \
				or not stages.has(String(order[1])):
			surfaces_refused[String(entry["name"])] = (
				"the smoke shader cross-fades retail's two frames and this card's did not both resolve (declared %s)"
				% str(order))
			return null
		var smoke := ShaderMaterial.new()
		smoke.shader = _shader(SMOKE_SHADER_PATH)
		smoke.set_shader_parameter("frame_a", stages[String(order[0])])
		smoke.set_shader_parameter("frame_b", stages[String(order[1])])
		# Warm-grey ash over the mountain's own lava glow; presentation value.
		smoke.set_shader_parameter("tint", Color(0.9, 0.86, 0.84, 0.9))
		return smoke
	if surface == "water":
		var water := ShaderMaterial.new()
		water.shader = _shader(WATER_SHADER_PATH)
		# The plane overhangs the painted seabed to the north and east; past the
		# terrain it fades out rather than ending on a lit cut edge.
		water.set_shader_parameter("fade_rect", _terrain_fade_rect(0.0))
		water.set_shader_parameter("fade_margin", 400.0)
		return water
	if surface == "cloud":
		var cloud := ShaderMaterial.new()
		cloud.shader = _shader(CLOUD_SHADER_PATH)
		var source := entry["material"] as StandardMaterial3D
		cloud.set_shader_parameter("cloud_texture", source.albedo_texture)
		if String(entry["name"]) == "BORDERCLOUD":
			# Retail authors the border cloud as a white mask and its strategic
			# screen shows it as the cold blue-grey weather bank in every
			# reference capture. The tint is that reading, stated here; the
			# texture and the ring geometry are retail's. The bank fades with
			# distance from the map so its own outer rim melts into the void
			# instead of cutting against it.
			cloud.set_shader_parameter("tint", Color(0.30, 0.38, 0.52, 0.97))
			cloud.set_shader_parameter("drift", Vector2(0.0016, 0.0007))
			cloud.set_shader_parameter("fade_rect", _terrain_fade_rect(250.0))
			cloud.set_shader_parameter("fade_margin", 1900.0)
		else:
			# LM_CLOUDLAYER: thin, cool drift over the map itself. BARELY there
			# by design - at 0.26 it greyed all of Middle-earth into winter; at
			# this strength it reads as weather crossing the map, never as fog
			# hiding it. Faded at the terrain's own extent, because the plane
			# overhangs the map and its cut edge read as a sheet of glass.
			cloud.set_shader_parameter("tint", Color(0.85, 0.90, 1.0, 0.12))
			cloud.set_shader_parameter("drift", Vector2(0.0042, 0.0016))
			cloud.set_shader_parameter("fade_rect", _terrain_fade_rect(0.0))
			cloud.set_shader_parameter("fade_margin", 500.0)
		return cloud
	# TEXT PLANE: retail's own engraved province names, exactly the authored
	# static overlay. Depth testing is off because retail parks the plane below
	# the terrain's relief and still shows the lettering over it; the fade with
	# zoom lives in `_apply_camera`.
	var text := StandardMaterial3D.new()
	text.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	text.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	text.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	text.no_depth_test = true
	text.cull_mode = BaseMaterial3D.CULL_DISABLED
	# Above the territory passes (fill 1, border glow 2, border 3): retail's
	# engraved lettering sits over the ownership art, not under it.
	text.render_priority = 4
	text.albedo_texture = (entry["material"] as StandardMaterial3D).albedo_texture
	text.albedo_color = Color(1.0, 1.0, 1.0, _text_plane_alpha())
	_text_plane_material = text
	return text


## How strongly the engraved lettering shows at the live zoom: full at the
## whole-map framing, gone by the time one region fills the panel. The band is a
## presentation choice; it reaches nothing and is stated here.
## Expressed as a FRACTION of the reachable pull-back rather than as two
## absolute zooms. 0.26 and 0.89 are the old 0.16 and 0.55 divided by the old
## fixed ceiling of 0.62 - the same band, at the same place in the same travel -
## but now they land in the same place on a 4:3 panel and on a 2.4:1 one, where
## the absolute pair fired at different framings on each.
func _text_plane_alpha() -> float:
	return clampf(inverse_lerp(0.26, 0.89, _framing_fraction()), 0.0, 1.0) * 0.85


## Retail's terrain rectangle in Godot X/Z, grown by `grow` world units, packed
## the way the water and cloud shaders' `fade_rect` expects it.
func _terrain_fade_rect(grow: float) -> Vector4:
	var extent: Dictionary = bundle.terrain_extent
	var low := BundleScript.world_to_godot(
		float(extent["x_min"]) - grow, float(extent["y_min"]) - grow, 0.0)
	var high := BundleScript.world_to_godot(
		float(extent["x_max"]) + grow, float(extent["y_max"]) + grow, 0.0)
	return Vector4(
		minf(low.x, high.x), minf(low.z, high.z),
		maxf(low.x, high.x), maxf(low.z, high.z))


## Stand retail's per-region fill and border meshes in the world, one node per
## region. THE SHAPES ARE RETAIL'S; only the colour is this project's, and the
## colour is a presentation value that reaches nothing.
##
## A region the bundle has no fill mesh for gets NO NODE. It keeps its marker and
## is named in `unshaded_regions`, because a region silently drawn in a
## neighbour's shape would be worse than one drawn in none.
func _rebuild_territories() -> void:
	if world_root == null:
		return
	if _territory_root != null and is_instance_valid(_territory_root):
		world_root.remove_child(_territory_root)
		_territory_root.queue_free()
	_territory_root = null
	_territory_nodes = {}
	# The fade ledger points at materials that are about to be freed.
	_flare_levels = {}
	shaded_regions = PackedStringArray()
	unshaded_regions = PackedStringArray()
	flat_flare_regions = PackedStringArray()
	var flat_flare_regions_list: Array[String] = []
	if region_geometry == null or not region_geometry.loaded:
		return

	_territory_root = Node3D.new()
	_territory_root.name = "Territories"
	world_root.add_child(_territory_root)

	var region_ids: Array[String] = []
	for key in region_geometry.by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()

	var shaded: Array[String] = []
	for region_id in region_ids:
		var fill_mesh: ArrayMesh = region_geometry.region_mesh(region_id, "fill")
		if fill_mesh == null:
			continue
		var slot: Dictionary = {}

		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		# The fill lies ON the terrain, so it must not write depth or it would
		# occlude the markers and the landmarks standing in it.
		material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.render_priority = 1
		# Starts at the NEUTRAL alpha, not opaque. A view with no session bound
		# yet still builds its territories, and an opaque starting fill painted
		# every region solid green until the first `set_regions` arrived.
		var initial := neutral_color
		initial.a = NEUTRAL_TERRITORY_ALPHA
		material.albedo_color = initial

		var fill := MeshInstance3D.new()
		fill.name = "Fill_%s" % region_id
		fill.mesh = fill_mesh
		fill.material_override = material
		fill.position = Vector3(0.0, TERRITORY_HEIGHT_BIAS, 0.0)
		_territory_root.add_child(fill)
		slot["fill"] = fill
		slot["fill_material"] = material

		# RETAIL'S OWN `HomeRegionHighlight` FOOTPRINT, under the fill. Starts
		# fully transparent and stays that way unless a caller names this region
		# home - see `HOME_REGION_BINDING_GAP` for why this view will not decide
		# that for itself.
		var highlight_mesh: ArrayMesh = region_geometry.home_highlight_mesh(region_id)
		if highlight_mesh != null:
			var highlight_material := StandardMaterial3D.new()
			highlight_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			highlight_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			highlight_material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
			highlight_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			highlight_material.cull_mode = BaseMaterial3D.CULL_DISABLED
			highlight_material.render_priority = 0
			highlight_material.albedo_color = Color(0, 0, 0, 0)
			var highlight := MeshInstance3D.new()
			highlight.name = "HomeHighlight_%s" % region_id
			highlight.mesh = highlight_mesh
			highlight.material_override = highlight_material
			highlight.position = Vector3(0.0, HOME_HIGHLIGHT_HEIGHT_BIAS, 0.0)
			_territory_root.add_child(highlight)
			slot["home_highlight_material"] = highlight_material

		# RETAIL'S `MouseoverEffectFlareup`, ON RETAIL'S OWN `LMR_Fill`. A second
		# instance of the same footprint rather than more alpha on the first,
		# because it is ADDITIVE - see `HOVER_FLARE_ALPHA`. Transparent until the
		# pointer is in this province.
		#
		# ROUND 8 GAVE IT A FALLOFF. Through round 7 this was a flat additive wash
		# at one alpha over the whole polygon, which ends the light on a hard edge
		# and draws the province's raw silhouette as a sheet of colour - and this is
		# the primary interaction of the screen, so a sheet is what the owner sees
		# most of the time. The reason it stayed flat was recorded and was true:
		# retail's fill mesh carries positions and nothing else, so a
		# StandardMaterial3D had no coordinate to fade against. It has one now -
		# `fill_falloff_mesh` bakes distance-from-the-rim into the vertex colour,
		# measured against retail's own border ribbon for the same region - and
		# `wotr_region_flare.gdshader` reads it.
		#
		# A REGION WHOSE FALLOFF COULD NOT BE BAKED KEEPS THE PLAIN FILL, and Godot
		# passes an absent COLOR array through as white, so the shader degrades to
		# exactly the flat wash it replaces rather than to nothing. Those regions
		# are counted in `flare_falloff_regions` and named by the screen.
		var flare_shader := _shader(FLARE_SHADER_PATH)
		var flare_mesh: ArrayMesh = region_geometry.fill_falloff_mesh(region_id)
		if flare_mesh == null:
			flare_mesh = fill_mesh
		var flare_material: Material = null
		if flare_shader != null:
			var shaded_flare := ShaderMaterial.new()
			shaded_flare.shader = flare_shader
			shaded_flare.set_shader_parameter("flare_color", Color(0, 0, 0, 0))
			shaded_flare.set_shader_parameter("edge", HOVER_FLARE_EDGE_FRACTION)
			shaded_flare.set_shader_parameter("core", HOVER_FLARE_CORE_LIFT)
			flare_material = shaded_flare
		else:
			# The shader file did not load, which `_shader` has already recorded.
			# The flat wash is what this screen shipped for seven rounds, so it is a
			# named earlier behaviour rather than an invented fallback.
			var flat_flare := StandardMaterial3D.new()
			flat_flare.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			flat_flare.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			flat_flare.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
			flat_flare.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			flat_flare.cull_mode = BaseMaterial3D.CULL_DISABLED
			flat_flare.albedo_color = Color(0, 0, 0, 0)
			flare_material = flat_flare
		flare_material.render_priority = 1
		var flare := MeshInstance3D.new()
		flare.name = "HoverFlare_%s" % region_id
		flare.mesh = flare_mesh
		flare.material_override = flare_material
		flare.position = Vector3(0.0, HOVER_FLARE_HEIGHT_BIAS, 0.0)
		_territory_root.add_child(flare)
		slot["hover_flare_material"] = flare_material
		slot["hover_flare_has_falloff"] = flare_mesh != fill_mesh

		var border_mesh: ArrayMesh = region_geometry.region_mesh(region_id, "border")
		if border_mesh != null:
			# THE OUTER SHOULDER FIRST, furthest out and faintest. Retail's own
			# ribbon pushed away from the region's derived centroid (see
			# `border_shoulder_mesh`), additive, so the band fades OUT of the
			# terrain rather than ending on a step. A region with no derived
			# centroid gets no shoulder and is drawn with the band alone.
			var shoulder_materials: Array[StandardMaterial3D] = []
			for shoulder_index in range(BORDER_SHOULDER_OUTSETS.size() - 1, -1, -1):
				var outset := float(BORDER_SHOULDER_OUTSETS[shoulder_index])
				var shoulder_mesh: ArrayMesh = region_geometry.border_shoulder_mesh(
					region_id, outset)
				if shoulder_mesh == null:
					continue
				var shoulder_material := StandardMaterial3D.new()
				shoulder_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
				shoulder_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				shoulder_material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
				shoulder_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
				shoulder_material.cull_mode = BaseMaterial3D.CULL_DISABLED
				shoulder_material.render_priority = 2
				shoulder_material.albedo_color = Color(0, 0, 0, 0)
				# The alpha this ring is raised to for an OWNED region, carried
				# on the material so `_apply_territory_colors` needs no second
				# copy of the table.
				shoulder_material.set_meta("shoulder_alpha",
					float(BORDER_SHOULDER_ALPHAS[shoulder_index]))
				var shoulder := MeshInstance3D.new()
				shoulder.name = "BorderShoulder%d_%s" % [shoulder_index, region_id]
				shoulder.mesh = shoulder_mesh
				shoulder.material_override = shoulder_material
				shoulder.position = Vector3(0.0, BORDER_HEIGHT_BIAS - 0.2, 0.0)
				_territory_root.add_child(shoulder)
				shoulder_materials.append(shoulder_material)
			if not shoulder_materials.is_empty():
				slot["border_shoulder_materials"] = shoulder_materials
			# THE GLOW PASS NEXT, under the solid band: the SAME retail border
			# strip, blended additively, which is what makes an owned border read
			# as the thick bright outline of the reference captures without
			# widening retail's geometry. It starts fully transparent -
			# `_apply_territory_colors` raises it for owned regions only.
			var glow_material := StandardMaterial3D.new()
			glow_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			glow_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			glow_material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
			glow_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			glow_material.cull_mode = BaseMaterial3D.CULL_DISABLED
			glow_material.render_priority = 2
			glow_material.albedo_color = Color(0, 0, 0, 0)
			var glow := MeshInstance3D.new()
			glow.name = "BorderGlow_%s" % region_id
			glow.mesh = border_mesh
			glow.material_override = glow_material
			glow.position = Vector3(0.0, BORDER_HEIGHT_BIAS - 0.1, 0.0)
			_territory_root.add_child(glow)
			slot["border_glow_material"] = glow_material

			var border_material := StandardMaterial3D.new()
			border_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			border_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			border_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			border_material.cull_mode = BaseMaterial3D.CULL_DISABLED
			border_material.render_priority = 3
			# RETAIL'S OWN BORDER COLOUR: `livingworldregioneffects.ini` sets
			# `RegionBorderColor = R:30 G:6 B:6`, which the living-world document
			# carries through as `regionEffects[].colors.regionBorder`.
			border_material.albedo_color = Color8(30, 6, 6, 235)
			var border := MeshInstance3D.new()
			border.name = "Border_%s" % region_id
			border.mesh = border_mesh
			border.material_override = border_material
			border.position = Vector3(0.0, BORDER_HEIGHT_BIAS, 0.0)
			_territory_root.add_child(border)
			slot["border"] = border
			slot["border_material"] = border_material

		# RETAIL'S OWN `RegionSelectionEffect` GEOMETRY, last and highest, so the
		# selection ring and the target rings draw over the ownership band they
		# enclose. One instance per region, transparent until this region is the
		# selection, the committed target, or one of the attack targets.
		var edge_mesh: ArrayMesh = region_geometry.selection_mesh(region_id)
		if edge_mesh != null:
			var edge_material := StandardMaterial3D.new()
			edge_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			edge_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			edge_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			edge_material.cull_mode = BaseMaterial3D.CULL_DISABLED
			edge_material.render_priority = 4
			edge_material.albedo_color = Color(0, 0, 0, 0)
			var edge := MeshInstance3D.new()
			edge.name = "SelectionEdge_%s" % region_id
			edge.mesh = edge_mesh
			edge.material_override = edge_material
			edge.position = Vector3(0.0, SELECTION_EDGE_HEIGHT_BIAS, 0.0)
			_territory_root.add_child(edge)
			slot["selection_edge_material"] = edge_material

		_territory_nodes[region_id] = slot
		shaded.append(region_id)
		if not bool(slot.get("hover_flare_has_falloff", false)):
			flat_flare_regions_list.append(region_id)
	shaded.sort()
	shaded_regions = PackedStringArray(shaded)
	flat_flare_regions_list.sort()
	flat_flare_regions = PackedStringArray(flat_flare_regions_list)


## Push ownership onto the territory materials. Pure presentation, run whenever
## the strategic picture changes; nothing here is read back.
func _apply_territory_colors() -> void:
	if _territory_nodes.is_empty():
		return
	var owner_by_region: Dictionary = {}
	for row in rows:
		owner_by_region[String(row["id"])] = int(row["owner"])
	var missing: Array[String] = []
	for row in rows:
		var region_id := String(row["id"])
		if not _territory_nodes.has(region_id):
			missing.append(region_id)
			continue
		var slot := _territory_nodes[region_id] as Dictionary
		var material := slot["fill_material"] as StandardMaterial3D
		var owner := int(row["owner"])
		var owned := owner >= 0 and owner < owner_colors.size()
		# THE FILL CARRIES THE OWNER'S HUE AT A COMMON CHROMA, not the raw seat
		# colour: see `FILL_SATURATION_FLOOR`. A neutral region keeps retail's own
		# `neutralRegion` grey untouched - it has no owner to be legible as.
		var color := _fill_color(_color_of(owner)) if owned else _color_of(owner)
		var alpha := TERRITORY_ALPHA if owned else NEUTRAL_TERRITORY_ALPHA
		var selected := region_id == selected_region or region_id == selected_target
		# `MouseoverEffectFlareup` is authored on `LMR_Fill` FOR MOUSEOVER, so
		# hover flares the fill and leaves the band alone. The band is ownership,
		# and ownership does not change because the cursor moved.
		var flared := region_id == hover_region
		# AN ATTACK TARGET IS THE OTHER WAY ROUND, and see
		# `TERRITORY_ALPHA_UNDER_TARGET` for the capture that forced it: the
		# attacker's curtain is about to stand on this ground, so the OWNER'S paint
		# gets out of its way rather than adding to it. Retail authors no
		# attack-target fill effect at all; flaring one was this view's own
		# addition, and it is what turned nine adjacent holdings into one red sheet
		# the moment a stage began.
		var targeted := Array(targets).has(region_id)
		var lighten := BORDER_LIGHTEN
		if selected:
			alpha = TERRITORY_ALPHA_SELECTED
			lighten = BORDER_LIGHTEN_SELECTED
		elif targeted:
			# Only an OWNED fill is pulled down; unclaimed ground is already at
			# `NEUTRAL_TERRITORY_ALPHA`, which is lower still, and raising it here to
			# "resolve an overlap" would be painting a region nobody owns.
			if owned:
				alpha = TERRITORY_ALPHA_UNDER_TARGET
		elif flared:
			alpha = TERRITORY_ALPHA_HOVER
			lighten = BORDER_LIGHTEN_HOVER
		color.a = alpha
		material.albedo_color = color
		# THE MOUSEOVER FLARE, on retail's own `LMR_Fill`. Driven from the fade
		# ledger rather than from `flared` directly so a province the pointer has
		# just left goes out over `HOVER_FLARE_FADE_SECONDS` instead of snapping.
		if flared:
			_flare_levels[region_id] = 1.0
		_write_hover_flare(region_id, owner, owned)
		# OWNERSHIP LIVES ON THE BORDER, the way the reference captures read:
		# a claimed region wears a bright band in its owner's colour around a
		# light interior tint, and an unclaimed one keeps retail's own dark
		# `RegionBorderColor`. Hover and selection brighten the band further
		# rather than piling more paint on the ground. The additive glow pass
		# under the band is what widens it into the bloomed outline of the
		# reference; a neutral border gets no glow, because a dark red band
		# added to itself would read as a seventh player's colour.
		if slot.has("border_material"):
			var border_material := slot["border_material"] as StandardMaterial3D
			var glow_material := slot.get("border_glow_material", null) as StandardMaterial3D
			if owned:
				# OWNERSHIP IS THE HUE AND NOTHING TOUCHES IT. Round 3 lerped the
				# band towards white for hover and selection, which is how the two
				# became one signal; the white now goes on the CORE ONLY, for the
				# selected region only, and the halo around it stays the owner's.
				var band := _band_color(_color_of(owner))
				band.a = 1.0
				# HOW MUCH BLOOM AND SHOULDER THIS FRAMING GETS. The whole
				# apparatus exists because retail's border ribbon is ~6.5 world
				# units - about two pixels at the strategic framing - and two
				# pixels cannot carry ownership across a room. CLOSE IN it is
				# already forty pixels wide, and the same gain there was
				# photographed as a white-hot rope with a cyan halo, which is a
				# worse defect than the one it was fixing. So bloom and shoulder
				# fade in WITH the pull-back: none at all at the closest framing,
				# full at the ceiling (which is where the screen opens).
				var bloom := _framing_fraction()
				# PER-HUE, not the constant: see `BAND_SECOND_CHANNEL_CEILING`.
				# A hue whose second channel would clip under the full gain gets
				# only as much gain as it can carry with its hue intact.
				var gain := lerpf(1.0, _band_gain(band), bloom)
				# THE HOVERED PROVINCE'S OWN BAND BURNS HARDER. Still the owner's
				# hue and nothing but - see the paragraph above - and it is the
				# outline the flare needs to be contained by, or a lit interior
				# reads as a smear with no edge to it.
				if flared:
					gain *= HOVER_BAND_GAIN
				# OVER 1.0 ON PURPOSE. An unshaded material writes its albedo
				# straight into the HDR buffer, and the environment's bloom
				# threshold sits at 1.25 - so this, and nothing the painted
				# terrain can reach, is what glows. That glow IS the soft
				# shoulder; without it the band ends on a hard pixel step, which
				# is what a blind review called a legibility failure.
				# THE BAND IS ALWAYS THE OWNER'S HUE, INCLUDING WHEN SELECTED.
				# Round 4 drove the selected region's core to retail's achromatic
				# `neutralRegion` white as a STAND-IN for the `LMR_Edge` mesh the
				# bundle did not carry. It carries it now, the selection ring is
				# drawn on retail's own geometry below, and a white band would only
				# take "whose is it" away from the one region the player is looking
				# hardest at. The stand-in is gone and nothing replaced it here.
				border_material.albedo_color = Color(
					band.r * gain, band.g * gain, band.b * gain, 1.0)
				# THE HALO IS RAISED, NOT RECOLOURED, when this region is selected:
				# the ring has to sit inside enough owner colour that "whose"
				# survives "which one". Everything here is still the owner's band.
				var halo_gain := SELECTION_HALO_GAIN if selected else (
					HOVER_BAND_GAIN if flared else 1.0)
				if glow_material != null:
					var halo := band
					halo.a = clampf(
						BORDER_GLOW_ALPHA * lerpf(0.35, 1.0, bloom) * halo_gain,
						0.0, 1.0)
					glow_material.albedo_color = halo
				for shoulder_material in slot.get(
						"border_shoulder_materials", []) as Array:
					var ring := band
					ring.a = clampf(float((shoulder_material as StandardMaterial3D)
						.get_meta("shoulder_alpha", 0.0)) * bloom * halo_gain,
						0.0, 1.0)
					(shoulder_material as StandardMaterial3D).albedo_color = ring
			else:
				# AN UNCLAIMED REGION KEEPS RETAIL'S OWN DARK `RegionBorderColor`
				# WHATEVER THE PLAYER IS DOING TO IT. Round 4 drove a selected
				# neutral's band to white here, for the same stand-in reason the
				# owned branch did; selection is now `LMR_Edge` and it is drawn on
				# neutral and owned ground alike, so the band has no second job.
				# Hover keeps its own answer to "which one am I on" - a neutral has
				# no owner hue to brighten, so its dark border is lifted towards
				# parchment rather than given a fake owner.
				var neutral_band := NEUTRAL_BORDER_COLOR
				if region_id == hover_region:
					neutral_band = NEUTRAL_BORDER_COLOR.lerp(
						Color(0.92, 0.86, 0.66), maxf(lighten, 0.2))
				border_material.albedo_color = neutral_band
				if glow_material != null:
					glow_material.albedo_color = Color(0, 0, 0, 0)
				# An unclaimed region gets no shoulder either: an additive halo
				# in retail's dark `RegionBorderColor` would read as a seventh
				# player's colour, which is the same reason it gets no glow.
				for shoulder_material in slot.get(
						"border_shoulder_materials", []) as Array:
					(shoulder_material as StandardMaterial3D).albedo_color = Color(0, 0, 0, 0)
		# RETAIL'S OWN SELECTION AND HOME-REGION MESHES, which are separate art from
		# the ownership band and are therefore driven separately from it.
		_apply_region_effect_meshes(region_id, slot, owner, owned)
	missing.sort()
	unshaded_regions = PackedStringArray(missing)


## `region id -> 0..1`, how lit that province's mouseover flare currently is. The
## province under the pointer is pinned at 1; every other entry is on its way out
## and is erased when it gets there, so this dictionary is empty whenever nothing
## is fading and `drive_hover_flare` costs one `is_empty()` per frame.
var _flare_levels: Dictionary = {}


## THE COLOUR THE MOUSEOVER FLARE BURNS AT. Public so a test can assert on what
## actually reaches the material rather than on the intent behind it.
##
## An OWNED province flares in its owner's own hue at the fill's common chroma -
## the flare says "the pointer is here", never "this changed hands" - and an
## unclaimed one flares in parchment, because it has no owner hue to be lit in
## and inventing one would put a seventh seat on a six-seat map. It also eases off
## with the pull-in the way the band's bloom does: close in, one province is a
## quarter of the panel and full strength there is a wall of light.
func hover_flare_color(region_id: String, level: float) -> Color:
	var owner := _owner_of_region(region_id)
	return hover_flare_color_for(
		level, owner, owner >= 0 and owner < owner_colors.size())


## The same, with the owner already resolved. `_apply_territory_colors` runs over
## every region on every camera move and already HAS the owner in hand;
## `_owner_of_region` is a linear scan of the region rows, so resolving it again
## per region turned one camera frame into 52 x 52 row comparisons. Measured on
## the frame-budget runner, that alone was most of a millisecond of a panning
## frame.
func hover_flare_color_for(level: float, owner: int, owned: bool) -> Color:
	if level <= 0.0:
		return Color(0.0, 0.0, 0.0, 0.0)
	var tint := _fill_color(_color_of(owner)) if owned else HOVER_FLARE_NEUTRAL
	var strength := HOVER_FLARE_ALPHA * clampf(level, 0.0, 1.0) \
		* lerpf(0.55, 1.0, _framing_fraction())
	return Color(tint.r, tint.g, tint.b, clampf(strength, 0.0, 1.0))


func _write_hover_flare(region_id: String, owner := -2, owned := false) -> void:
	var slot: Dictionary = _territory_nodes.get(region_id, {}) as Dictionary
	var material := slot.get("hover_flare_material", null) as Material
	if material == null:
		return
	var level := float(_flare_levels.get(region_id, 0.0))
	var tint := hover_flare_color(region_id, level) if owner == -2 \
		else hover_flare_color_for(level, owner, owned)
	# ONE COLOUR, TWO KINDS OF MATERIAL. The flare is a ShaderMaterial wherever
	# `wotr_region_flare.gdshader` loaded and a StandardMaterial3D where it did
	# not; the colour that goes in is the same either way, which is what lets
	# `hover_flare_color*` stay the single public statement of what the flare
	# burns at.
	if material is ShaderMaterial:
		(material as ShaderMaterial).set_shader_parameter("flare_color", tint)
	else:
		(material as StandardMaterial3D).albedo_color = tint


## Walk the fade ledger one frame. Split from `_process` so a test can run it
## without a visible tree, the way `drive_camera` is.
func drive_hover_flare(delta: float) -> void:
	if _flare_levels.is_empty():
		return
	var step := maxf(delta, 0.0) / maxf(HOVER_FLARE_FADE_SECONDS, 0.0001)
	var finished: Array[String] = []
	for key in _flare_levels.keys():
		var region_id := String(key)
		if region_id == hover_region:
			# Pinned: coming ON is instant, because a pointer that arrives in a
			# province and waits for its highlight feels broken.
			if float(_flare_levels[key]) < 1.0:
				_flare_levels[key] = 1.0
				_write_hover_flare(region_id)
			continue
		var level := float(_flare_levels[key]) - step
		_flare_levels[key] = maxf(level, 0.0)
		_write_hover_flare(region_id)
		if level <= 0.0:
			finished.append(region_id)
	for region_id in finished:
		_flare_levels.erase(region_id)


## `RegionSelectionEffect` (`LMR_Edge`) and `HomeRegionHighlight`
## (`LMR_Highlight`) for one region. Split out of `_apply_territory_colors`
## because these are DIFFERENT MESHES from the ownership band, not a recolour of
## it, and the file used to hide exactly that fact by doing both in one branch.
##
## THREE STATES ON ONE MESH, and the ordering is the point:
##
##   selection  - the one region the player is acting on. Retail's own achromatic
##                `neutralRegion` white, driven hardest, so it is the loudest mark
##                on the map. There is never more than one.
##   target     - every region the selection may attack. The ATTACKER'S own hue at
##                `TARGET_EDGE_SATURATION` of its chroma and a lower gain, so the
##                valid set reads as one family at a glance (which a blind review
##                said the round-4 wash did better than retail) while each member
##                keeps its own outline (which is what that review said the wash
##                destroyed).
##   nothing    - transparent. A region that is neither gets no ring at all.
##
## The home highlight is independent of all three: it says whose ground this is
## the heart of, not what is being done to it.
func _apply_region_effect_meshes(
	region_id: String, slot: Dictionary, owner: int, owned: bool
) -> void:
	var bloom := _framing_fraction()
	var edge_material := slot.get("selection_edge_material", null) as StandardMaterial3D
	if edge_material != null:
		var ring := Color(0, 0, 0, 0)
		if region_id == selected_region or region_id == selected_target:
			# Driven just over 1.0 so the environment's glow threshold (1.25) catches
			# the curtain and gives it a halo. The gain fades in with the pull-back
			# for the reason the band's does: close in the curtain already fills a
			# quarter of the panel and the full gain there is a white wall.
			var selection_gain := lerpf(1.0, SELECTION_EDGE_GAIN, bloom)
			ring = Color(
				SELECTION_CORE_COLOR.r * selection_gain,
				SELECTION_CORE_COLOR.g * selection_gain,
				SELECTION_CORE_COLOR.b * selection_gain,
				SELECTION_EDGE_ALPHA * lerpf(EDGE_ALPHA_AT_CLOSEST, 1.0, bloom)
					* selection_rim_gain())
		elif Array(targets).has(region_id):
			ring = target_ring_color(bloom)
		edge_material.albedo_color = ring
	var home_material := slot.get("home_highlight_material", null) as StandardMaterial3D
	if home_material != null:
		var footprint := Color(0, 0, 0, 0)
		if home_regions.has(region_id):
			# The owner's own hue at the fill's common chroma, additive and faint:
			# retail's `HomeRegionHighlight` is a footprint glow under the ownership
			# art, not a second outline over it. A home region nobody owns - which
			# the strategic layer can produce the turn a capital changes hands - gets
			# retail's own achromatic `neutralRegion` rather than an invented hue.
			footprint = _fill_color(_color_of(owner)) if owned else SELECTION_CORE_COLOR
			footprint.a = clampf(HOME_HIGHLIGHT_ALPHA * lerpf(0.5, 1.0, bloom), 0.0, 1.0)
		home_material.albedo_color = footprint


## THE COLOUR EVERY ATTACK TARGET IS RINGED IN, and why it is derived rather than
## chosen. Public so a test can assert on the colour that actually reaches the
## materials instead of on the intent behind it.
##
## It is the ATTACKING SEAT'S hue - the seat that owns the selected region, or,
## before a region is selected, the seat that owns the first staging region this
## view was handed. Never a colour of its own: an invented target hue is a seventh
## player on a six-seat map, and the round-4 capture's "aliased saturated-red
## stroke" was read as exactly that when it was only the red seat's own band.
##
## Chroma is cut to `TARGET_EDGE_SATURATION` and the gain held under the band's,
## so the ring is findable without being the brightest thing in the frame. The
## same second-channel ceiling the ownership band lives under applies (see
## `BAND_SECOND_CHANNEL_CEILING`), so a target ring cannot clip its way into a
## different hue either.
func target_ring_color(bloom: float = 1.0) -> Color:
	var attacker := -1
	if not selected_region.is_empty():
		attacker = _owner_of_region(selected_region)
	if attacker < 0 and not staging.is_empty():
		attacker = _owner_of_region(String(staging[0]))
	var base := SELECTION_CORE_COLOR
	if attacker >= 0 and attacker < owner_colors.size():
		var seat := _band_color(_color_of(attacker))
		base = Color.from_hsv(seat.h, clampf(seat.s * TARGET_EDGE_SATURATION, 0.0, 1.0),
			seat.v, 1.0)
	# THE EXACT SECOND-CHANNEL CEILING, not `_band_gain`. `_band_gain` never
	# returns less than `BORDER_HDR_GAIN_MIN`, because a BAND under that stops
	# clearing the glow threshold and loses the shoulder it exists for - a target
	# ring has no such floor to protect, and taking the floor anyway is what drove
	# the blue seat's ring 25.5 degrees towards cyan on the way through the clip.
	# Here the hue is worth more than the bloom, so the gain gives way completely.
	var second := _second_channel(base)
	var allowed := TARGET_EDGE_GAIN if second <= 0.0 		else clampf(BAND_SECOND_CHANNEL_CEILING / second, 1.0, TARGET_EDGE_GAIN)
	var gain := lerpf(1.0, allowed, bloom)
	# AND THEN THE CURTAIN IS HELD OUT OF THE GLOW BUFFER ENTIRELY. See
	# `TARGET_EDGE_PEAK`: the second-channel ceiling above protects the HUE, this
	# protects the SHOULDER - a curtain that crosses `GLOW_HDR_THRESHOLD` gets the
	# isotropic halo the ownership band exists to have and a fourteen-pixel wall
	# does not. Applied to all three channels together, so it takes brightness and
	# never hue.
	var peak := maxf(base.r, maxf(base.g, base.b)) * gain
	if peak > TARGET_EDGE_PEAK:
		gain *= TARGET_EDGE_PEAK / peak
	return Color(base.r * gain, base.g * gain, base.b * gain,
		TARGET_EDGE_ALPHA * lerpf(EDGE_ALPHA_AT_CLOSEST, 1.0, bloom))


## Which seat holds a region, from the rows this view was handed. -1 for
## unclaimed and for a region the rows do not carry.
func _owner_of_region(region_id: String) -> int:
	for row in rows:
		if String(row["id"]) == region_id:
			return int(row["owner"])
	return -1


# --- retail's 3D markers ---------------------------------------------------------

## STAND RETAIL'S OWN MARKER MODELS ON THE MAP.
##
## This is what replaces the flat plates and rings. Every piece of geometry is
## retail's, and so is every number that positions it: the model comes from the
## marker family retail's own data binds to that army or that seat, the meshes
## shown are the ones the slot's `SubObjects` names, the height above the terrain
## is the slot's `ZOffset`, the size is its `Scale` and the facing is its
## `OrientAngle`. Nothing here is chosen to look right.
##
## WHAT IS NOT STOOD UP, and why it is not a gap this hides:
##
## * STRUCTURES are placed only when `structures_by_region` carries an occupied
##   authoritative plot. All 28 `LivingWorldBuildingIcon` families are converted,
##   but an unbuilt structure is never stood merely because its model exists.
## * RALLY FLAGS and the move-order art (`DisplayAtRallyPoint`,
##   `ShowOnlyAfterMoveOrder`). Retail shows them while a march is being ordered;
##   this screen commits an attack through a button, so there is no rally point
##   to stand one on.
##
## A stack or a plot whose model is absent gets NO substitute mesh: it keeps its
## flat plate or ring and is named in `army_markers_flat` / `plot_markers_flat`.
## THE MARKERS THAT ARE CURRENTLY BUILT, `key -> {node, signature, ...}`.
##
## WHY THERE IS A CACHE AT ALL, and it is not a micro-optimisation. `_rebuild_markers`
## used to free the whole marker tree and build every marker again, and it is called
## on every pointer move that changes which province or which plot is under the
## cursor. That was affordable while the only plots on the map were the two or three
## in the region under the pointer. It stopped being affordable the moment retail's
## foundation decals went on EVERY region (see `plot_regions`): 98 authored plots
## plus the army banners is on the order of a hundred and fifty nodes torn down and
## rebuilt per mouse move, which is a stutter on the primary interaction of the
## screen.
##
## SO IT RECONCILES INSTEAD. Each marker carries a SIGNATURE of everything that
## decides what it looks like - the family, where it stands, its magnification, its
## army size, whether it is hovered or selected, and the seat colour it is house-
## coloured in. A marker whose signature is unchanged is left standing untouched; one
## whose signature moved is rebuilt; one that is no longer wanted is freed. A hover
## move therefore touches the two markers that actually changed.
##
## IT IS NOT A CACHE OF DECISIONS, only of nodes. Every visibility rule in
## `_slot_is_showing` is still retail's and is still evaluated on the rebuild path;
## the signature is a statement of what those rules were evaluated AGAINST, so a
## marker can only be reused when the answer provably cannot have changed.
var _marker_holders: Dictionary = {}
## The keys reconciled during the rebuild in progress. Anything in
## `_marker_holders` and not in here at the end of the sweep is stale and freed.
var _marker_keys_seen: Dictionary = {}


func _rebuild_markers() -> void:
	army_markers_standing = 0
	army_markers_flat = {}
	house_coloured_meshes = 0
	plot_markers_standing = 0
	plot_markers_flat = {}
	structure_markers_standing = 0
	structure_markers_flat = {}
	_standing_markers = []
	_standing_keys = {}
	_marker_keys_seen = {}
	if world_root == null:
		return
	if not has_map() or not has_markers():
		_drop_all_markers()
	if not has_map():
		return
	if not has_markers():
		# Every stack and every plot is a flat stand-in, for ONE reason, recorded
		# once rather than repeated per marker.
		var why := markers_reason.split("\n")[0] if not markers_reason.is_empty() else "no marker bundle is bound"
		if not armies_by_region.is_empty():
			army_markers_flat["<all stacks>"] = why
		if not plots_by_region.is_empty():
			plot_markers_flat["<all plots>"] = why
		if not structures_by_region.is_empty():
			structure_markers_flat["<all structures>"] = why
		return

	if _marker_root == null or not is_instance_valid(_marker_root):
		_marker_root = Node3D.new()
		_marker_root.name = "Markers"
		world_root.add_child(_marker_root)

	_stand_army_markers()
	_stand_plot_markers()
	_stand_structure_markers()

	# EVERYTHING THE SWEEP DID NOT CLAIM IS STALE. An army that moved out of a
	# region, a plot whose region stopped being drawn, a marker whose family
	# changed hands - all of them fall out here rather than being left standing.
	var stale: Array[String] = []
	for key in _marker_holders.keys():
		if not _marker_keys_seen.has(key):
			stale.append(String(key))
	for key in stale:
		_free_marker(key)


## Free every standing marker. Used when the map or the marker bundle goes away,
## where reconciling has nothing to reconcile against.
func _drop_all_markers() -> void:
	for key in _marker_holders.keys():
		var node := (_marker_holders[key] as Dictionary).get("node", null) as Node3D
		if node != null and is_instance_valid(node):
			node.queue_free()
	_marker_holders = {}
	if _marker_root != null and is_instance_valid(_marker_root):
		world_root.remove_child(_marker_root)
		_marker_root.queue_free()
	_marker_root = null


func _free_marker(key: String) -> void:
	var holder: Dictionary = _marker_holders.get(key, {}) as Dictionary
	var node := holder.get("node", null) as Node3D
	if node != null and is_instance_valid(node):
		if _marker_root != null and is_instance_valid(_marker_root):
			_marker_root.remove_child(node)
		node.queue_free()
	_marker_holders.erase(key)


## How much bigger than retail's own authored size a marker stands at this
## framing. 1.0 means retail's exact size. Public so the screen can state it and
## a test can assert both ends of it rather than take it on trust.
func marker_magnification() -> float:
	return clampf(sqrt(maxf(_zoom, 0.0001) / MARKER_TRUE_ZOOM),
		1.0, MARKER_MAX_MAGNIFICATION)


func _stand_army_markers() -> void:
	if armies_by_region.is_empty():
		return
	var region_ids: Array[String] = []
	for key in armies_by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()
	for region_id in region_ids:
		if not _world_positions.has(region_id):
			continue
		var anchor: Vector3 = _world_positions[region_id]
		var stacks: Array = armies_by_region[region_id] as Array
		var shown: int = mini(stacks.size(), MAX_BANNERS_PER_REGION)
		# Several stacks in one region are fanned apart along the map's own east
		# axis so two banners read as two banners. The SPACING is presentation and
		# reaches nothing; the anchor it is measured from is retail's coordinate.
		# The fan widens with the markers, or magnified banners would stand inside
		# one another at exactly the framing the magnification exists to serve.
		var fan_world := MARKER_FAN_WORLD * marker_magnification()
		var span := float(shown - 1) * fan_world
		for index in range(shown):
			var stack := stacks[index] as Dictionary
			var family := String(stack.get("icon", ""))
			var label := String(stack.get("label", "?"))
			if family.is_empty():
				army_markers_flat[label] = (
					"retail's living-world data binds this army to no LivingWorldArmyIcon "
					+ "and its seat's template authors no DefaultArmyIconName")
				continue
			if not markers.has_family(family):
				army_markers_flat[label] = (
					"the marker bundle carries no family %s" % family)
				continue
			var offset := Vector3(float(index) * fan_world - span * 0.5, 0.0, 0.0)
			# RETAIL'S OWN `UseHouseColor`, now that half of it is applicable.
			# The stack's own seat colour goes in; `_stand_family` gives it only
			# to the meshes retail flat-shades from its house-colour swatch, and
			# never to the painted cloth. See `HOUSE_COLOUR_GAP`.
			var stood := _stand_family(
				"%s#%d" % [region_id, index], "army", family, anchor + offset,
				String(stack.get("size", "")),
				region_id == hover_region, region_id == selected_region,
				_color_of(int(stack.get("owner", -1))))
			if stood:
				army_markers_standing += 1
			else:
				army_markers_flat[label] = (
					"retail's %s slot of family %s names the model %s, which the bundle did not convert"
					% [String(markers.families.get(family, {}).get("bodySlot", "?")), family,
						markers.slot_model(family, String(markers.families.get(family, {}).get("bodySlot", "")))])


func _stand_plot_markers() -> void:
	var shown := plot_regions()
	if shown.is_empty():
		return
	var open_region := String(selected_plot.get("region", ""))
	var open_index := int(selected_plot.get("index", -1))
	var over_region := String(hover_plot.get("region", ""))
	var over_index := int(hover_plot.get("index", -1))
	for region_id in shown:
		var family := String(plot_icons_by_region.get(region_id, ""))
		var spots: Array = _plot_world_positions.get(region_id, []) as Array
		if family.is_empty():
			plot_markers_flat[region_id] = (
				"no seat owns this region, so retail's data binds its plots to no "
				+ "LivingWorldBuildPlotIcon")
			continue
		if not markers.has_family(family):
			plot_markers_flat[region_id] = "the marker bundle carries no family %s" % family
			continue
		for index in range(spots.size()):
			var at := spots[index] as Vector3
			var stood := _stand_family(
				"%s.plot%d" % [region_id, index], "plot", family, at, "",
				over_region == region_id and over_index == index,
				open_region == region_id and open_index == index)
			if stood:
				plot_markers_standing += 1
			else:
				plot_markers_flat[region_id] = (
					"retail's Decal slot of family %s names the model %s, which the bundle did not convert"
					% [family, markers.slot_model(family, "Decal")])


## Stand every structure that the authoritative strategic state says occupies a
## plot.  The family is the structure's own LivingWorldBuilding.BuildingIcon and
## the anchor is that plot's authored BuildingSpot; no model or position is
## inferred here.
func _stand_structure_markers() -> void:
	if structures_by_region.is_empty():
		return
	var region_ids: Array[String] = []
	for key in structures_by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()
	for region_id in region_ids:
		var spots: Array = _plot_world_positions.get(region_id, []) as Array
		for row_value in structures_by_region[region_id] as Array:
			var row := row_value as Dictionary
			var plot := int(row.get("plot", -1))
			var family := String(row.get("icon", ""))
			var building := String(row.get("building", "?"))
			if plot < 0 or plot >= spots.size():
				structure_markers_flat["%s#%d" % [region_id, plot]] = (
					"%s names plot %d, outside this region's %d authored BuildingSpot rows"
					% [building, plot, spots.size()])
				continue
			if family.is_empty():
				structure_markers_flat["%s#%d" % [region_id, plot]] = (
					"%s carries no LivingWorldBuildingIcon id" % building)
				continue
			if not markers.has_family(family):
				structure_markers_flat["%s#%d" % [region_id, plot]] = (
					"the marker bundle carries no family %s for %s" % [family, building])
				continue
			var stood := _stand_family(
				"%s.structure%d" % [region_id, plot], "building", family,
				spots[plot] as Vector3, "", false, false,
				_color_of(int(row.get("owner", -1))))
			if stood:
				structure_markers_standing += 1
			else:
				var body := String((markers.families.get(family, {}) as Dictionary).get(
					"bodySlot", "Building"))
				structure_markers_flat["%s#%d" % [region_id, plot]] = (
					"retail's %s slot of family %s names model %s, which did not stand"
					% [body, family, markers.slot_model(family, body)])


## Stand every slot of one family that retail's own visibility rules say is
## showing, at `anchor`. Returns true when the family's BODY slot - the thing that
## replaces the flat plate or ring - actually stood.
func _stand_family(
	key: String, kind: String, family_id: String, anchor: Vector3,
	army_size: String, hovered: bool, selected: bool,
	house_color: Color = Color(0.0, 0.0, 0.0, 0.0)
) -> bool:
	_marker_keys_seen[key] = true
	# EVERYTHING THAT DECIDES WHAT THIS MARKER LOOKS LIKE, in one string. See
	# `_marker_holders`: a marker whose signature has not moved cannot have
	# changed, so it is left standing rather than torn down and built again.
	# The anchor is quantised to a thousandth of a world unit, which is four
	# orders of magnitude finer than anything that can be seen and keeps float
	# noise from forcing a rebuild.
	var signature := "%s|%.3f,%.3f,%.3f|%s|%d%d|%.4f|%s" % [
		family_id, anchor.x, anchor.y, anchor.z, army_size,
		1 if hovered else 0, 1 if selected else 0,
		# QUANTISED TO A TWENTIETH. The magnification is a continuous function of
		# the zoom, so an un-quantised signature would rebuild every marker on the
		# map on every frame of a smoothed zoom. A twentieth of retail's own size
		# is well under what can be seen moving, and it turns a continuous rebuild
		# into about thirty of them across the whole zoom range.
		snappedf(marker_magnification(), 0.05),
		house_color.to_html(true)]
	var cached: Dictionary = _marker_holders.get(key, {}) as Dictionary
	if String(cached.get("signature", "")) == signature:
		var standing := cached.get("standing", {}) as Dictionary
		if not standing.is_empty():
			_standing_markers.append(standing)
		house_coloured_meshes += int(cached.get("house_meshes", 0))
		if bool(cached.get("body", false)):
			_standing_keys[key] = true
			return true
		return false
	if not cached.is_empty():
		_free_marker(key)

	var family: Dictionary = markers.families.get(family_id, {}) as Dictionary
	var body_slot := String(family.get("bodySlot", ""))
	var body_stood := false
	var house_meshes_here := 0
	var stood_slots: Array[String] = []
	var holder := Node3D.new()
	holder.name = "%s_%s" % [kind, key.replace("#", "_").replace(".", "_")]
	var bounds := AABB()
	var have_bounds := false
	for slot_value in family.get("slots", []) as Array:
		var slot_row := slot_value as Dictionary
		if not _slot_is_showing(slot_row, army_size, hovered, selected):
			continue
		var pieces: Array[Dictionary] = markers.pieces_of(slot_row)
		if pieces.is_empty():
			continue
		var slot_node := Node3D.new()
		slot_node.name = String(slot_row.get("slot", "slot"))
		# RETAIL'S OWN PLACEMENT, in retail's own order: turn by `OrientAngle`
		# about the map's up axis, scale by `Scale`, lift by `ZOffset`.
		var magnification := marker_magnification()
		slot_node.transform = Transform3D(
			Basis(Vector3.UP, MarkerModelsScript.slot_orient_radians(slot_row))
				.scaled(Vector3.ONE * MarkerModelsScript.slot_scale(slot_row) * magnification),
			anchor + Vector3(
				0.0, MarkerModelsScript.slot_z_offset(slot_row) * magnification, 0.0))
		# RETAIL'S OWN `UseHouseColor = Yes`, applied to exactly the meshes it can
		# be applied to and no others: a mesh whose whole UV footprint lands on
		# retail's flat house-colour swatch is drawn in the seat's colour, and a
		# mesh carrying painted art keeps retail's texture. `HOUSE_COLOUR_GAP`
		# carries the measurement and names what is still not recoloured.
		var wants_house := house_color.a > 0.0 			and String(slot_row.get("useHouseColor", "")).to_lower() == "yes"
		for piece in pieces:
			var instance := MeshInstance3D.new()
			instance.name = String(piece["name"])
			instance.mesh = piece["mesh"]
			instance.material_override = piece["material"]
			if wants_house and MarkerModelsScript.piece_is_flat_swatch(piece):
				instance.material_override = markers.house_coloured_material_of(
					String(slot_row.get("model", "")), piece, house_color)
				house_coloured_meshes += 1
				house_meshes_here += 1
			slot_node.add_child(instance)
			var piece_aabb := (piece["mesh"] as ArrayMesh).get_aabb()
			var world_aabb := slot_node.transform * piece_aabb
			if have_bounds:
				bounds = bounds.merge(world_aabb)
			else:
				bounds = world_aabb
				have_bounds = true
		holder.add_child(slot_node)
		stood_slots.append(String(slot_row.get("slot", "")))
		if String(slot_row.get("slot", "")) == body_slot:
			body_stood = true
	if holder.get_child_count() == 0:
		holder.queue_free()
		# REMEMBERED AS NOTHING, not forgotten. A family that stands no slot at
		# this hover/selection state must not be re-attempted on every pointer
		# move, and the empty record is what says "this one was answered".
		_marker_holders[key] = {
			"node": null, "signature": signature, "standing": {},
			"body": false, "house_meshes": 0,
		}
		return false
	_marker_root.add_child(holder)
	var standing_row: Dictionary = {}
	if have_bounds:
		# THE FAMILY AND THE SLOTS ARE RECORDED, not only the box. "Six markers are
		# standing" and "six markers are standing the family retail's own data
		# names for that army, showing only the slots retail's own visibility
		# fields say are showing" are two different claims, and only the second
		# one is the claim this lane makes.
		standing_row = {
			"key": key, "kind": kind, "aabb": bounds, "body": body_stood,
			"family": family_id, "slots": PackedStringArray(stood_slots),
		}
		_standing_markers.append(standing_row)
	_marker_holders[key] = {
		"node": holder, "signature": signature, "standing": standing_row,
		"body": body_stood, "house_meshes": house_meshes_here,
	}
	if body_stood:
		_standing_keys[key] = true
	return body_stood


## RETAIL'S OWN VISIBILITY RULES for one slot, and nothing else. Every clause is
## a field retail authors; there is no "looks better" clause.
func _slot_is_showing(
	slot_row: Dictionary, army_size: String, hovered: bool, selected: bool
) -> bool:
	# Move-order art. There is no rally point on this screen to stand it on.
	if String(slot_row.get("displayAtRallyPoint", "")).to_lower() == "yes":
		return false
	if String(slot_row.get("showOnlyAfterMoveOrder", "")).to_lower() == "yes":
		return false
	# Construction art. Construction is not simulated, so a scaffold and a
	# producing-unit glow have no state to be in; drawing either would assert one.
	if String(slot_row.get("hideWhenNotUnderConstruction", "")).to_lower() == "yes":
		return false
	if String(slot_row.get("hideWhenNotProducing", "")).to_lower() == "yes":
		return false
	if MarkerModelsScript.slot_hover_only(slot_row) and not hovered:
		return false
	if MarkerModelsScript.slot_selection_only(slot_row) and not selected:
		return false
	var sizes := MarkerModelsScript.slot_army_sizes(slot_row)
	if sizes.is_empty():
		# Retail's "always", not "never".
		return true
	# A slot gated on army size shows only when retail's own `IconSize` for this
	# army says so. An army whose size retail does not author - one reached
	# through the seat's `DefaultArmyIconName` rather than a recruit block - shows
	# the size-independent slots only, rather than being assigned a size here.
	if army_size.is_empty():
		return false
	return Array(sizes).has(army_size.to_upper())


## THE BOX THE CAMERA IS AIMED AND SIZED AT, in Godot space.
##
## ROUND 8 CHANGED WHAT THIS IS, AND IT IS THE ROUND'S LARGEST SINGLE CHANGE.
## Through round 7 the subject of the framing was RETAIL'S TERRAIN SLAB - the
## 6021 x 4819 rectangle of twenty tiles `terrain_extent` describes - because the
## target was parity and retail frames its slab. The owner's verdict on the result
## was "it feels very cramped and awful to manoeuvre, it feels like the FOV or
## resolution is very low", and the measurement agrees with him: a third of that
## rectangle is ground NO REGION IS EVER PLACED ON. The playable region set spans
## godot x -2106.9..3088.0 and z -2537.3..1189.9 (5195 x 3727); the slab's extra
## 826 units of width are retail's painted seabed column in the far west
## (`LM_01`/`LM_06`/`LM_11`/`LM_16`) and its extra 1092 units of depth are the
## empty ice north of Forodwaith and empty sea south of Harad.
##
## SO THE SUBJECT IS THE PLAYABLE BOARD NOW. The camera frames the box that holds
## every region's own fill mesh, not the box the tile grid happens to occupy.
## Measured consequences at retail's own 2560x1440, opening framing:
##
##   * the fitted picture goes from 3381 to 3035 world units of panel height, so
##     every province is drawn 1.11x larger across and 1.24x larger in area;
##   * the framing RE-CENTRES. The slab's centre is 364 units north and 264 units
##     west of the region set's, and at the owner's own wide window that error was
##     not cosmetic: at 1860x800 the round-7 framing put Harad, Umbar, Khand and
##     Near Harad entirely OFF THE PANEL - measured, 117.6 px past the bottom edge
##     for Harad - while spending the top of the frame on empty ocean;
##   * the pan wall stops binding at the opening framing on most panel shapes,
##     because the fitted picture is now strictly inside the slab instead of
##     pressed against its cut edge.
##
## THE FALLBACK IS THE OLD BEHAVIOUR AND IT IS REPORTED, NOT SILENT. The region
## bundle fails independently of the map bundle (`set_region_geometry` says so),
## and with no fill layer there is no playable box to frame. The framing then
## falls back to `terrain_extent` - which is exactly what round 7 shipped, so the
## fallback is a NAMED EARLIER BEHAVIOUR rather than an invented one - and
## `framing_source()` says which of the two is live so a caller can print it.
##
## Returns the eight corners of the box plus its centre. Eight rather than a
## Rect2 because the fit resolves them into the camera's own basis, where a box's
## screen footprint depends on all eight.
## CACHED, because `_clamp_camera_target` asks for it and that runs up to eight
## times in one frame of held-key panning (once per pan-wall bisection step). The
## box is a function of the two bundles and of nothing else, and both are bound
## once - `set_bundle` and `set_region_geometry` drop the cache, and nothing else
## can change the answer.
var _framing_box_cache: Dictionary = {}

func _framing_box() -> Dictionary:
	if not _framing_box_cache.is_empty():
		return _framing_box_cache
	var low := Vector3.ZERO
	var high := Vector3.ZERO
	var source := ""
	var playable := AABB()
	if region_geometry != null and region_geometry.loaded:
		playable = region_geometry.playable_bounds()
	if playable.size.x > 1.0 and playable.size.z > 1.0:
		low = playable.position
		high = playable.end
		source = "playable region set"
	elif has_map():
		var extent: Dictionary = bundle.terrain_extent
		var a := BundleScript.world_to_godot(
			float(extent["x_min"]), float(extent["y_min"]), float(extent["z_min"]))
		var b := BundleScript.world_to_godot(
			float(extent["x_max"]), float(extent["y_max"]), float(extent["z_max"]))
		low = Vector3(minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z))
		high = Vector3(maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z))
		source = "retail's terrain slab (no region fill geometry: %s)" % (
			region_geometry_reason if not region_geometry_reason.is_empty()
			else "no reason was supplied, which is itself a defect")
	else:
		# NOT CACHED: with no map bound at all there is nothing to be right about
		# yet, and caching this would freeze the framing at "none" for the run.
		return {"low": low, "high": high, "centre": Vector3.ZERO, "source": "none"}
	_framing_box_cache = {
		"low": low, "high": high, "centre": (low + high) * 0.5, "source": source,
	}
	return _framing_box_cache


## WHICH BOX THE OPENING FRAMING IS AIMED AT, in a sentence fit to print. Public
## because "the camera frames the playable board" and "the camera frames whatever
## it could find" are different claims and only a caller that can read this can
## tell them apart.
func framing_source() -> String:
	return String(_framing_box()["source"])


func _frame_camera() -> void:
	if not has_map():
		return
	_camera_target = _framing_box()["centre"] as Vector3
	_fit_distance()
	_clamp_zoom()
	_apply_camera()


## The direction from the point the camera looks at to the camera itself, at the
## live yaw and pitch, as a unit vector. One expression, used by the fit and by
## `_apply_camera`, so the distance that was solved for and the place the camera
## is actually put cannot drift apart.
func _camera_offset_direction() -> Vector3:
	var pitch := deg_to_rad(_pitch_degrees)
	return Vector3(sin(_yaw) * cos(pitch), -sin(pitch), cos(_yaw) * cos(pitch))


## The camera's own basis at the live yaw and pitch: `x` across the screen, `y`
## up it, `-z` along the view. Built with `Basis.looking_at` and the same up hint
## `look_at_from_position` is given below, so it is the basis the camera will
## actually have rather than a re-derivation of it.
func _view_basis() -> Basis:
	return Basis.looking_at(-_camera_offset_direction(), Vector3.UP)


## Fit retail's whole map into the viewport it is actually being drawn in.
##
## THE FRAMING a1e7b2e REPLACED fitted the map's LONGER axis into the camera's
## VERTICAL field of view, ignoring both the wide panel and the pitch. That is
## fixed and stays fixed. WHAT THIS COMMIT REPLACES is the fit that followed it:
## it measured the map's BOUNDING BOX under an ORTHOGRAPHIC approximation -
## `depth * sin(pitch) + relief * cos(pitch)`, one number for the whole map - and
## a pitched perspective camera does not draw a box. It draws a TRAPEZOID: the
## south edge is nearer the camera and projects wide, the north edge is further
## and projects narrow, and the two do not straddle the panel's centre line.
##
## Measured on the shipped bundle at the 1860x800 window, panel 1264x496,
## pitch -52, zoom 1: the ground quad projected to y 71.9..568.5. The panel is
## 496 tall. So 72 px of Middle-earth's south coast was BELOW THE PANEL and cut
## off, and a 72 px band along the top was empty - the map was not too big and
## not too small, it was 72 px too low, because the orthographic estimate has no
## way to know that the near half of a pitched plane projects larger than the far
## half. The width was under-used for the same reason: the estimate is symmetric
## and the projection is not.
##
## THIS FITS THE PROJECTED FOOTPRINT. The eight corners of retail's own terrain
## extent are resolved into the camera's own basis and the exact perspective
## inequalities are solved:
##
##   ndc_x = across / ((along + distance) * half_horizontal)
##   ndc_y = (lift - centring) / ((along + distance) * half_vertical)
##
## `distance` is the smallest that keeps every |ndc| within 1 - a closed form on
## each axis, no search - and `centring` is then solved so the footprint's top
## and bottom slacks are equal, which is what puts the map in the middle of the
## panel instead of 72 px below it.
##
## ROUND 7 KEPT THE STRUCTURE AND DELETED THE TRAPEZOID. The camera is parallel
## now (see `CAMERA_FOV_DEGREES`), so the two inequalities above lose their depth
## term entirely:
##
##   ndc_x = (across - centring_x) / (size * 0.5 * aspect)
##   ndc_y = (lift   - centring_y) / (size * 0.5)
##
## The pairwise closed form survives with `along` dropped, which collapses it to
## the obvious thing - the size that fits the footprint is its own SPAN on each
## axis, taken against that axis's half-width - and the centring collapses to the
## span's MIDPOINT, which is exactly what "both slacks equal" means once the two
## slacks are no longer weighted by depth. The bisection in `_solve_centring` is
## therefore gone rather than left running on a problem with a closed form; the
## 72-px-low defect it was written for cannot exist under a parallel projection,
## because the near half of a pitched plane no longer projects larger than the far
## half. `along` is still computed, and it is what sizes `_camera_standoff`.
##
## BOTH PROPERTIES a1e7b2e ESTABLISHED ARE PRESERVED, and both are asserted at a
## non-default zoom and a non-zero yaw: the fit is still against the viewport's
## own aspect and the LIVE pitch, and this function still writes ONLY
## `_camera_distance`, `_camera_standoff` and `_framing_offset`, never the
## player's pan, zoom, yaw or pitch. A resize re-fits; it discards nothing.
func _fit_distance() -> void:
	if camera == null or not has_map():
		return
	# THE FRAMING BOX, WHICH IS THE PLAYABLE REGION SET AND NOT THE SLAB. See
	# `_framing_box` for the measurement and for what the fallback is.
	var box: Dictionary = _framing_box()
	# The fit is measured from the FRAMING BOX'S OWN CENTRE, never from the live
	# camera target. If it were measured from the target, panning would move the
	# footprint, the centring would cancel the move, and the map could not be
	# panned at all.
	var centre := box["centre"] as Vector3
	var basis := _view_basis()
	var right := basis.x
	var up := basis.y
	var forward := -basis.z

	var aspect := 16.0 / 9.0
	if viewport != null and viewport.size.y > 0:
		aspect = float(viewport.size.x) / float(viewport.size.y)
	# Godot's Camera3D keeps the VERTICAL extent, so `size` is the world height the
	# panel spans and the width follows the aspect. Both are expressed here PER
	# UNIT OF `size`, so the fit below solves directly for `size` itself.
	var half_vertical := 0.5
	var half_horizontal := maxf(0.5 * aspect, 0.0001)

	# The eight corners of the framing box, in the camera's basis.
	var across: PackedFloat32Array = PackedFloat32Array()
	var lift: PackedFloat32Array = PackedFloat32Array()
	var along: PackedFloat32Array = PackedFloat32Array()
	var box_low := box["low"] as Vector3
	var box_high := box["high"] as Vector3
	for xi in [box_low.x, box_high.x]:
		for yi in [box_low.y, box_high.y]:
			for zi in [box_low.z, box_high.z]:
				var rel := Vector3(xi, yi, zi) - centre
				across.append(rel.dot(right))
				lift.append(rel.dot(up))
				along.append(rel.dot(forward))
	# AND WHERE THE SLAB LETS THE PICTURE BE PLACED. The slab no longer sizes the
	# frame, but it still bounds WHERE the frame may sit: the cut-edge rule
	# (`slab_cut_edge_is_in_frame`) is unchanged and is still about the slab, so a
	# centring that pushes the frame past the slab's rim is immediately taken back
	# by `zoom_ceiling()` as lost pull-back. Measured on the shipped bundle at
	# 2560x1351: leaving the centring at the box midpoint costs 6% of the picture.
	#
	# THE BOUND IS THE INTERSECTION OF THE TWO HORIZONTAL FACES, NOT THE SPAN OF
	# ALL EIGHT CORNERS, and the difference is not pedantry - the first version of
	# this clamp used all eight and it made the framing WORSE. A pixel shows the
	# slab's cut rim exactly when its ray enters through the slab's TOP face and
	# does not leave through the BOTTOM one (or misses one of them entirely); it
	# looks straight through the box, top to bottom, exactly when it is inside BOTH
	# faces' projections. Using all eight corners takes the union instead, which
	# admits a band along the near edge where the ray crosses the south WALL - and
	# the south wall is precisely the raw straight silhouette this whole rule
	# exists to keep off the screen. Measured: the eight-corner form put the frame
	# 143 lift-units into the south wall and cost 9% of the pull-back.
	#
	# EXACT AT YAW 0, which is the framing every capture and every opening is taken
	# at; at a yawed camera the two faces project to parallelograms and this takes
	# their bounding spans, which is an over-estimate. That is safe by construction
	# because this is a BEST EFFORT and not the guarantee: the guarantee is
	# `slab_cut_edge_is_in_frame`, which clips the twelve rim segments exactly at
	# any yaw, and `zoom_ceiling()` is solved against it.
	var extent: Dictionary = bundle.terrain_extent
	var slab_across := Vector2(-INF, INF)
	var slab_lift := Vector2(-INF, INF)
	for zi in [float(extent["z_min"]), float(extent["z_max"])]:
		var face_across: PackedFloat32Array = PackedFloat32Array()
		var face_lift: PackedFloat32Array = PackedFloat32Array()
		for xi in [float(extent["x_min"]), float(extent["x_max"])]:
			for yi in [float(extent["y_min"]), float(extent["y_max"])]:
				var rel := BundleScript.world_to_godot(xi, yi, zi) - centre
				face_across.append(rel.dot(right))
				face_lift.append(rel.dot(up))
		var span_across := _span_of(face_across)
		var span_lift := _span_of(face_lift)
		slab_across = Vector2(
			maxf(slab_across.x, span_across.x), minf(slab_across.y, span_across.y))
		slab_lift = Vector2(
			maxf(slab_lift.x, span_lift.x), minf(slab_lift.y, span_lift.y))

	# THE SIZE, on both axes at once. With a centring shift `s` on an axis, corner
	# i is on the panel when
	#     -size * half  <=  offset[i] - s  <=  size * half
	# and a single `s` satisfies every corner iff it satisfies every PAIR, which
	# rearranges into a closed form with no search in it:
	#     size >= (offset[i] - offset[j]) / (2 * half).
	# The pairwise form is kept rather than simplified to `span / (2 * half)`
	# because the two are the same statement and this one says WHY: it is the
	# widest separation any two corners of the framing box can have on the axis,
	# which is the only thing that can force the picture to be bigger.
	var needed := 1.0
	for i in across.size():
		for j in across.size():
			needed = maxf(needed, (across[i] - across[j]) / (2.0 * half_horizontal))
			needed = maxf(needed, (lift[i] - lift[j]) / (2.0 * half_vertical))
	_camera_distance = needed * FRAMING_MARGIN

	# THE CENTRING, one axis at a time. Through round 7 this was one line - the
	# midpoint of the footprint's span, which under a parallel projection is
	# exactly the shift that leaves both slacks equal - and the midpoint is still
	# where it starts. One thing now moves it, and only within the interval the
	# midpoint's own containment guarantee allows.
	#
	# `_framing_offset` is measured at zoom 1 and `_apply_camera` scales it by the
	# zoom, so it stays a CONSTANT offset on screen at every framing - the property
	# that stops a zoom-in sliding its subject across the panel. Nothing below
	# changes that; every term is solved once, at zoom 1, here.
	var half_frame_x := _camera_distance * half_horizontal
	var half_frame_y := _camera_distance * half_vertical
	# 1. THE SLAB CLAMP, which is where the picture would LIKE to sit: as far
	#    inside retail's own rim as it can, because every unit of frame hanging
	#    past that rim is pull-back `zoom_ceiling()` takes away again.
	var wanted_x := _pulled_inside(
		_span_midpoint(across), slab_across, half_frame_x)
	var wanted_y := _pulled_inside(
		_span_midpoint(lift), slab_lift, half_frame_y)
	# 2. AND CONTAINMENT WINS. Whatever the clamp above asked for, the framing box
	#    must still be entirely on the panel - that is the property this round
	#    replaced "the whole slab is on the panel" with, and it is asserted in
	#    `wotr_map3d_runner.gd`. The feasible interval is non-empty by construction
	#    because `needed` was solved to make it so, and `FRAMING_MARGIN` widens it.
	#
	#    THE TWO CAN DISAGREE, BY 1.3 LIFT UNITS, AND THIS IS WHERE THEY DO.
	#    Harad's fill reaches godot z 1189.9 and the slab's own southern cut is at
	#    z 1372.1, 182 units behind it; the picture needs 389 units of depth more
	#    than the region set occupies, so the frame's southern edge lands within
	#    1.3 units of the slab's south wall and the wall is a hair inside the
	#    frame. `zoom_ceiling()` then trims the opening zoom to about 0.999 - six
	#    tenths of a pixel on a 1440-tall panel. It is recorded here rather than
	#    tuned away because the only ways to remove it are to crop Harad or to show
	#    the cut, and both are worse than 0.6 px.
	# 3. AND THEN THE PICTURE IS PUSHED SOUTH INTO WHATEVER CONTAINMENT LEFT OVER.
	#    See `HUD_OCCLUDED_BOTTOM_FRACTION` for the measurement and the review that
	#    asked for it. Lowering the centring on the lift axis raises the picture on
	#    screen (`ndc_y = (lift - centring) / half`), which brings the southern
	#    provinces out from under the tray; `_held_inside` then refuses any part of
	#    the move that would push a northern province off the top, so the bias can
	#    only ever spend slack the fit was not using. The request is the whole
	#    occluded band, and containment is expected to grant about a third of it -
	#    asking for exactly what containment can give would be a number that went
	#    stale the moment the HUD or the aspect changed.
	var panel_height := maxf(float(viewport.size.y) if viewport != null else size.y, 1.0)
	var world_per_pixel := _camera_distance / panel_height
	var bias := occluded_bottom() * world_per_pixel
	var floor_y := _span_of(lift).y - half_frame_y \
		+ SOUTH_BIAS_TOP_MARGIN_PX * world_per_pixel
	_framing_offset = Vector2(
		_held_inside(wanted_x, across, half_frame_x),
		maxf(_held_inside(_held_inside(wanted_y, lift, half_frame_y) - bias,
			lift, half_frame_y), floor_y))

	# THE STANDOFF, which decides only what is inside the near and far planes. See
	# `CAMERA_STANDOFF_SPAN`.
	var deepest := 0.0
	var nearest := 0.0
	for index in along.size():
		deepest = maxf(deepest, along[index])
		nearest = minf(nearest, along[index])
	_camera_standoff = (deepest - nearest) * CAMERA_STANDOFF_SPAN \
		+ CAMERA_STANDOFF_CLEARANCE


## The largest and smallest of a set of offsets along one screen axis.
static func _span_of(offsets: PackedFloat32Array) -> Vector2:
	var low := INF
	var high := -INF
	for value in offsets:
		low = minf(low, value)
		high = maxf(high, value)
	if not is_finite(low) or not is_finite(high):
		return Vector2.ZERO
	return Vector2(low, high)


## Move `wanted` as little as possible so that a frame of half-extent `half`
## centred on it CONTAINS every one of `offsets`. Used for the framing box: this
## is the property "no region is ever off the panel", stated as arithmetic.
##
## The feasible interval is `[high - half, low + half]`, which is non-empty
## exactly when the frame is at least as wide as the span - true by construction
## here, since `needed` was solved from the same offsets. When it is empty (a
## caller with a frame too small for its subject) the midpoint is returned, which
## is the least-bad placement rather than an arbitrary end of a backwards range.
static func _held_inside(
	wanted: float, offsets: PackedFloat32Array, half: float
) -> float:
	var span := _span_of(offsets)
	var lower := span.y - half
	var upper := span.x + half
	if lower > upper:
		return (span.x + span.y) * 0.5
	return clampf(wanted, lower, upper)


## Move `wanted` as little as possible so that a frame of half-extent `half`
## centred on it lies INSIDE `span` (low in `x`, high in `y`) - the mirror image
## of `_held_inside`. Used for the slab: the frame must sit inside the slab's rim,
## not the other way round.
##
## WHEN THE FRAME IS WIDER THAN THE SPAN the feasible interval runs backwards and
## no placement works at all - which happens at panel shapes far from the region
## set's own 1.83:1, where framing every region necessarily reaches past retail's
## rim. The interval's MIDPOINT is returned then, which centres the frame in the
## span and so makes the worst overflow as small as it can be. That is not a
## cosmetic choice: `zoom_ceiling()` trims the opening zoom by the worst overflow,
## and measured at 1860x800 - the owner's own wide window - returning `wanted`
## untouched instead cost 8% of the picture on the across axis and 17% on the lift
## axis, because the whole overflow was piled onto one edge.
static func _pulled_inside(wanted: float, span: Vector2, half: float) -> float:
	var lower := span.x + half
	var upper := span.y - half
	if not is_finite(lower) or not is_finite(upper):
		return wanted
	if lower > upper:
		return (lower + upper) * 0.5
	return clampf(wanted, lower, upper)


## The midpoint of a set of offsets along one screen axis.
static func _span_midpoint(offsets: PackedFloat32Array) -> float:
	var low := INF
	var high := -INF
	for value in offsets:
		low = minf(low, value)
		high = maxf(high, value)
	if not is_finite(low) or not is_finite(high):
		return 0.0
	return (low + high) * 0.5


## THE FURTHEST THE PLAYER MAY PULL BACK BEFORE THE SLAB'S CUT EDGE ENTERS THE
## FRAME, at the LIVE aspect, pitch, yaw and pan. Public, because "the zoom is
## clamped" and "the zoom is clamped to the thing the clamp exists for" are two
## different claims and only a test that can read this number can assert the
## second.
##
## WHAT THE RULE IS ABOUT, restated in round 6 because the OLD WORDING WAS THE
## WRONG PROPERTY EVEN THOUGH IT CAUGHT THE RIGHT DEFECT. It used to read "all
## four panel corners land inside retail's own terrain footprint", and that was
## then read back as a demand that no ocean, coast or cloud may ever reach a
## frame corner - which retail's own capture plainly violates, since three of the
## four corners of `game.dat_l1eJcM0zCw.jpg` are open sea under weather.
##
## THE TWO THINGS ARE NOT IN CONFLICT, and the measurement says why. Retail's
## terrain slab is not a coastline-shaped island: it is a 6021 x 4819 rectangle
## of twenty tiles, and its WESTERN COLUMN IS PAINTED SEABED. `LM_01`, `LM_06`,
## `LM_11` and `LM_16` are almost entirely ocean, and every one of the 24 terrain
## vertices standing on the western boundary (x = x_min) sits between -121.0 and
## -98.5, i.e. BELOW the WATER plane's top face at -91.24. So the ocean at
## retail's frame corners IS retail's own terrain, seen from above through its
## own water. Solving retail's camera out of that capture (see
## `CAMERA_FOV_DEGREES`) puts its four frame corners at roughly (-2781, 3163),
## (3088, 3033), (-2533, -1362) and (3336, -1492) against a slab spanning
## x -2784..3237 and y -1372..3447: retail frames its slab almost exactly, and
## the corners that look like open water are inside it.
##
## THE DEFECT THE RULE EXISTS FOR IS THE CUT, NOT THE WATER: the raw straight
## silhouette where the tile slab stops and the void begins, which round 1 and
## round 2 both photographed and which ray-traced back to retail x = -2784,
## `terrain_extent.x_min` to within a unit. So this is now stated as the property
## it always meant - NO PART OF THE SLAB'S CUT RIM MAY PROJECT INTO THE PANEL -
## and tested directly against that rim rather than through a proxy. It is not
## weaker: for a camera looking down at the ground the two are equivalent, and
## a regression that puts the raw silhouette back fails it exactly as loudly.
##
## WHY IT IS COMPUTED AND NOT A CONSTANT. Whether the cut is on screen depends on
## the panel's ASPECT (a wide short panel reaches further sideways for the same
## fitted distance), on the PITCH (a flatter camera sees further up the map), on
## the YAW and on where the player has panned. A single tuned constant can only
## be right for one combination of the four, and round 2's was right for one
## window and wrong for the one that got photographed.
##
## Never above `MAX_ZOOM`, so the reachable range is always inside the stated
## one, and never below `MIN_ZOOM`, so a pan that puts the rim in frame cannot
## leave the camera with nowhere to go.
func zoom_ceiling() -> float:
	if camera == null or viewport == null or not has_map():
		return MAX_ZOOM
	if not slab_cut_edge_is_in_frame(MAX_ZOOM):
		return MAX_ZOOM
	if slab_cut_edge_is_in_frame(MIN_ZOOM):
		# NO PULL-BACK LIMIT CAN HELP HERE, so none is imposed. This is the low
		# oblique: tilt far enough and the top of the frame points at or above the
		# map's own horizon, the rim is in the picture at EVERY distance, and
		# clamping would snap the camera to its closest framing for no gain at
		# all. The player who tilts the camera that far is looking at the skyline
		# on purpose. The guarantee this function makes is therefore exact and
		# bounded: wherever a pull-back limit CAN keep the cut out of frame, it is
		# imposed - which covers the whole of the default pitch band the screen
		# opens at and every framing any capture is taken from.
		return MAX_ZOOM
	var low := MIN_ZOOM
	var high := MAX_ZOOM
	for _step in ZOOM_CEILING_STEPS:
		var middle := (low + high) * 0.5
		if slab_cut_edge_is_in_frame(middle):
			high = middle
		else:
			low = middle
	return low


## WHETHER THE TERRAIN SLAB'S CUT RIM WOULD BE IN THE PICTURE at this zoom.
## Public so a test can assert the property itself instead of a stand-in for it.
## Reads the camera's own fields and writes NOTHING - it is asked about zooms the
## camera is not at, so it must never move it.
##
## THE RIM IS TWELVE LINE SEGMENTS and they are retail's own numbers: the edges
## of the box `terrain_extent` describes, four along the bottom at `z_min`, four
## along the top at `z_max`, and four uprights joining them at the slab's
## corners. Every visible piece of the cut lies on the surface those twelve edges
## bound, so if none of the twelve reaches the frustum the cut is not on screen.
##
## Each segment is clipped against the frustum's five bounding half-spaces
## (near, left, right, bottom, top) in the CAMERA'S OWN BASIS. Every half-space
## is linear in the point, so it is linear in the parameter along the segment,
## and the whole test is a Liang-Barsky interval narrowing: exact, closed-form,
## and free of any sampling density that could step over a thin sliver of cut.
##
## THE SECOND CLAUSE IS THE HORIZON. A frame corner whose ray never reaches
## retail's own height band in front of the camera is not looking at the map at
## all, it is looking past it into the void - the same defect wearing a different
## face - so that counts as the cut being in frame. It is also what makes the
## low-oblique escape in `zoom_ceiling()` fire the way it always has.
##
## ROUND 7 REPLACED THE FRUSTUM WITH A BOX and nothing else about the rule. Under
## a parallel projection the five half-spaces are still five half-spaces - they
## are simply no longer flared by depth: `|across| <= half_width` instead of
## `|across| <= half_angle * depth`. Both forms are linear in the point and so
## still linear along a segment, so the Liang-Barsky narrowing below is exact for
## either, and `_segment_reaches_the_frame` takes both a per-depth slope and a
## constant half-extent so it expresses the general case rather than one of them.
##
## THE HORIZON CLAUSE CHANGED SHAPE FOR THE SAME REASON. Under a frustum the four
## corner RAYS diverge, so "does this corner's ray ever reach retail's height
## band" is four different questions; under a parallel projection every ray runs
## along the view direction and only the four ORIGINS differ. It is still asked
## per corner, and it still answers the same defect: a corner looking past the map
## into the void counts as the cut being in frame.
func slab_cut_edge_is_in_frame(zoom: float) -> bool:
	var basis := _view_basis()
	# The same two lines `_apply_camera` uses to place the camera, so the
	# question asked here is about the picture that would actually be drawn.
	var look_at := _camera_target + (
		basis.x * _framing_offset.x + basis.y * _framing_offset.y) * zoom
	var origin := look_at + _camera_offset_direction() * _camera_standoff
	var panel := Vector2(viewport.size)
	# The panel's own half-extents in WORLD units at this zoom, which is what a
	# parallel projection's frustum is made of.
	var half_vertical := maxf(_camera_distance * zoom, 0.0001) * 0.5
	var half_horizontal := half_vertical * (panel.x / maxf(panel.y, 1.0))
	# THE FRAME THIS ASKS ABOUT IS THE ONE THE PLAYER CAN SEE, and round 9 is where
	# that stopped meaning "the panel". The bottom `occluded_bottom()` pixels are
	# under the HUD's tray and palantir, AND this view inks them itself in
	# `_draw_tray_feather` whether the HUD is there or not, so a rim that only
	# reaches into that band is a rim nobody can look at. Asking about the whole
	# panel instead cost the framing its entire southward slack - see
	# `HUD_OCCLUDED_BOTTOM_FRACTION` - and Mordor with it.
	#
	# IT IS AN INSET ON THE BOTTOM HALF-SPACE AND NOTHING ELSE. The rule is
	# otherwise the round-7 rule exactly: the same twelve edges, the same
	# Liang-Barsky narrowing, the same horizon clause, all still exact.
	var bottom_inset := 0.0
	if panel.y > 1.0:
		bottom_inset = half_vertical * 2.0 * clampf(
			occluded_bottom() / panel.y, 0.0, 0.9)

	# THE HORIZON CLAUSE FIRST, because it is the cheap one. Every corner ray runs
	# along the view direction; only the origins differ.
	var direction := -basis.z
	if absf(direction.y) < 0.00001:
		return true
	for corner_x in HORIZON_CORNER_SIGNS:
		for corner_y in HORIZON_CORNER_SIGNS:
			# The bottom two corners are the corners of the UNOCCLUDED frame.
			var lift_extent := half_vertical if corner_y > 0.0 \
				else -(half_vertical - bottom_inset)
			var corner_origin := origin \
				+ basis.x * (corner_x * half_horizontal) \
				+ basis.y * lift_extent
			for height in _rim_heights:
				if (height - corner_origin.y) / direction.y <= 0.0:
					return true

	# THE TWELVE RIM EDGES, read from the cache `_rebuild_rim_segments` built. See
	# that function for why they are not derived here any more.
	for index in range(0, _rim_segments.size(), 2):
		if _segment_reaches_the_frame(
				_rim_segments[index], _rim_segments[index + 1],
				origin, basis, half_horizontal, half_vertical, bottom_inset):
			return true
	return false


## THE SIGNS OF THE FOUR FRAME CORNERS, hoisted out of the horizon clause. It used
## to build a `PackedFloat32Array` on every call, and this function is called about
## a hundred and seventy times in a single frame of held-key panning.
## TYPED, so the loop variable is a `float` and the arithmetic below it stays
## statically typed - an untyped literal here makes `corner_origin` a Variant and
## the file will not compile.
const HORIZON_CORNER_SIGNS: Array[float] = [-1.0, 1.0]

## THE TWELVE EDGES OF THE TERRAIN SLAB'S RIM, in Godot space, as consecutive
## endpoint pairs, plus the two heights the horizon clause tests against.
##
## WHY THEY ARE CACHED. They are a function of `bundle.terrain_extent` and nothing
## else, and `terrain_extent` is fixed the moment the bundle is bound - the rim of
## Middle-earth does not move when the camera does. `slab_cut_edge_is_in_frame` was
## nevertheless rebuilding all of it on every call: six dictionary lookups and
## float casts, an `Array[Vector2]` of corners, two `[z_min, z_max]` arrays per
## corner, and twelve `world_to_godot` conversions.
##
## THAT IS NOT A MICRO-OPTIMISATION AT THE RATE THIS IS CALLED. Holding a pan key
## at the zoom ceiling runs `_pan_ground`'s six-step pan-wall bisection, and each
## step calls `zoom_ceiling()`, which is itself a twenty-step bisection over this
## predicate - about 176 calls per frame, every frame the key is down, each one
## allocating three arrays and doing twelve coordinate conversions.
##
## THE PREDICATE IS UNCHANGED. The same twelve segments are tested in the same
## order against the same five half-spaces, so the function returns exactly what it
## returned before for every input; only the derivation of constants moved out of
## the loop. `wotr_map3d_runner`'s cut-edge and zoom-ceiling checks are the pin on
## that claim.
var _rim_segments: Array[Vector3] = []
var _rim_heights: Array[float] = []


func _rebuild_rim_segments() -> void:
	_rim_segments = []
	_rim_heights = []
	if not has_map():
		return
	var extent: Dictionary = bundle.terrain_extent
	var x_min := float(extent["x_min"])
	var x_max := float(extent["x_max"])
	var y_min := float(extent["y_min"])
	var y_max := float(extent["y_max"])
	var z_min := float(extent["z_min"])
	var z_max := float(extent["z_max"])
	_rim_heights = [z_min, z_max]
	# `world_to_godot` is (x, z, -y), so each retail corner is CONVERTED rather
	# than transcribed - the same call the old inline derivation made.
	var corners: Array[Vector2] = [
		Vector2(x_min, y_min), Vector2(x_max, y_min),
		Vector2(x_max, y_max), Vector2(x_min, y_max)]
	for index in corners.size():
		var here: Vector2 = corners[index]
		var next: Vector2 = corners[(index + 1) % corners.size()]
		# Four along the bottom at `z_min`, four along the top at `z_max` ...
		for height in _rim_heights:
			_rim_segments.append(BundleScript.world_to_godot(here.x, here.y, height))
			_rim_segments.append(BundleScript.world_to_godot(next.x, next.y, height))
		# ... and four uprights joining them at the slab's corners.
		_rim_segments.append(BundleScript.world_to_godot(here.x, here.y, z_min))
		_rim_segments.append(BundleScript.world_to_godot(here.x, here.y, z_max))


## Whether any point of the segment `from`..`to` lies inside the view frustum.
## Liang-Barsky against the five bounding half-spaces; see
## `slab_cut_edge_is_in_frame` for why that is exact here.
func _segment_reaches_the_frame(
	from: Vector3, to: Vector3, origin: Vector3, basis: Basis,
	half_horizontal: float, half_vertical: float, bottom_inset := 0.0
) -> bool:
	var right := basis.x
	var up := basis.y
	var forward := -basis.z
	var a := from - origin
	var b := to - origin
	# Each half-space as `value(t) >= 0` at the two ends.
	var planes: Array[Vector2] = [
		# In front of the near plane.
		Vector2(a.dot(forward) - camera.near, b.dot(forward) - camera.near),
		# Left, right, bottom, top. The half-extents are CONSTANT in the depth,
		# which is what a parallel projection means; they are still linear in the
		# point, so the narrowing below is exact exactly as before.
		Vector2(a.dot(right) + half_horizontal, b.dot(right) + half_horizontal),
		Vector2(half_horizontal - a.dot(right), half_horizontal - b.dot(right)),
		# The bottom half-space is raised by `bottom_inset`: see the caller for why
		# the band under the tray is not part of the frame this asks about.
		Vector2(a.dot(up) + half_vertical - bottom_inset,
			b.dot(up) + half_vertical - bottom_inset),
		Vector2(half_vertical - a.dot(up), half_vertical - b.dot(up)),
	]
	var low := 0.0
	var high := 1.0
	for plane in planes:
		var start := plane.x
		var finish := plane.y
		var delta := finish - start
		if absf(delta) < 0.000001:
			if start < 0.0:
				return false
			continue
		var crossing := -start / delta
		if delta > 0.0:
			low = maxf(low, crossing)
		else:
			high = minf(high, crossing)
		if low > high:
			return false
	return low <= high


## Re-solve the ceiling and RE-RESOLVE the player's request against it. Called
## from every place that can change what the ceiling is - a resize, a re-fit, a
## pan, an orbit, a zoom - so there is no path through this file that leaves the
## camera further back than the guarantee allows.
##
## IT RESOLVES FROM `_zoom_request`, NOT FROM `_zoom`, and that is the whole
## point (see `_zoom_request`). Clamping `_zoom` against itself is idempotent in
## the wrong direction: it can only ever ratchet the camera inward, so a ceiling
## that rose could never hand back the pull-back it had taken away.
func _clamp_zoom() -> void:
	_zoom_ceiling = zoom_ceiling()
	# `_wall_yield` is the temporary give a pan pressing into the cut-edge wall has
	# taken (see `_yield_zoom_to_the_wall`). It is 1.0 whenever nothing is leaning,
	# which is every framing this screen opens on and every one a capture is taken
	# from, so the resolve below is the round-7 one in every state but that lean.
	_zoom = clampf(_zoom_request * _wall_yield,
		MIN_ZOOM, maxf(MIN_ZOOM, _zoom_ceiling))
	# The territory band's bloom and shoulder are a function of the framing (see
	# `_apply_territory_colors`), and this is the one place every change to the
	# framing passes through. Cheap: 52 regions, a handful of material writes,
	# and only when the camera actually moves.
	_apply_territory_colors()


## Where the live zoom sits in the reachable pull-back, 0 at the closest framing
## and 1 at the furthest. The presentation bands that used to be absolute zooms
## are expressed in this instead, so they mean the same thing on a 4:3 panel and
## on a 2.4:1 one rather than firing at different framings on each.
func _framing_fraction() -> float:
	return clampf(_zoom / maxf(_zoom_ceiling, 0.0001), 0.0, 1.0)


func _apply_camera() -> void:
	if camera == null:
		return
	# THE ZOOM IS THE PICTURE'S SIZE, NOT THE CAMERA'S DISTANCE. Under a parallel
	# projection moving the camera along its own view direction changes nothing at
	# all, so the framing lives entirely in `size` and the standoff is held at the
	# value the fit derived for clipping (see `CAMERA_STANDOFF_SPAN`).
	camera.size = maxf(_camera_distance * _zoom, 0.0001)
	var offset := _camera_offset_direction() * _camera_standoff
	# THE CENTRING IS PART OF THE FRAMING, NOT PART OF THE PAN. The camera and
	# the point it looks at are both shifted by the same vector, so the view
	# direction is untouched and only the framing moves; and the shift scales with
	# the zoom, which makes it a CONSTANT offset on screen - so zooming in on a
	# region does not slide it across the panel.
	var basis := _view_basis()
	var look_at := _camera_target + (
		basis.x * _framing_offset.x + basis.y * _framing_offset.y) * _zoom
	# `look_at_from_position` rather than `look_at`, because this runs before the
	# view is inside a tree when a test drives it directly and `look_at` requires
	# a global transform.
	camera.look_at_from_position(look_at + offset, look_at, Vector3.UP)
	# The engraved province lettering belongs to the strategic framing; it fades
	# out as the camera closes in. Every zoom change funnels through here.
	if _text_plane_material != null:
		_text_plane_material.albedo_color = Color(1.0, 1.0, 1.0, _text_plane_alpha())


# --- region placement ---------------------------------------------------------

func _recompute_world_positions() -> void:
	_world_positions = {}
	var placed: Array[String] = []
	var unplaced: Array[String] = []
	var unsampled: Array[String] = []
	var from_centroid: Array[String] = []
	for row in rows:
		var region_id := String(row["id"])
		var authored := row["position"] as Vector2
		if not bool(row["has_position"]):
			# Retail leaves `CustomCenterPoint` off for a handful of regions and
			# derives the marker from the region's OWN MESH. `livingmap.w3d`
			# carries no such mesh, which is why these used to be listed as
			# unplaceable - but `lmr_fill.w3d` does, and the converter computes an
			# area-weighted centroid of retail's own triangles for every region in
			# it. That is derivation from shipped geometry, so it may be used; it
			# is recorded separately from an authored point so the screen can say
			# which of the two a marker is standing on.
			if region_geometry != null and region_geometry.derived_centroids.has(region_id):
				authored = region_geometry.derived_centroids[region_id] as Vector2
				from_centroid.append(region_id)
			else:
				unplaced.append(region_id)
				continue
		var height := float(bundle.terrain_extent.get("z_max", 0.0)) if has_map() else 0.0
		if has_map():
			var sampled: Dictionary = bundle.sample_height(authored.x, authored.y)
			if bool(sampled["ok"]):
				height = float(sampled["height"])
			else:
				unsampled.append(region_id)
		_world_positions[region_id] = BundleScript.world_to_godot(
			authored.x, authored.y, height)
		placed.append(region_id)
	placed.sort()
	unplaced.sort()
	unsampled.sort()
	from_centroid.sort()
	placed_regions = PackedStringArray(placed)
	unplaced_regions = PackedStringArray(unplaced)
	unsampled_heights = PackedStringArray(unsampled)
	centroid_placed_regions = PackedStringArray(from_centroid)


## `region id -> Array[Vector3]`, every authored build plot's world position with
## its terrain height already sampled.
##
## CACHED BECAUSE IT IS NOW NINETY-EIGHT OF THEM. While plots were drawn for the
## four provinces the player was touching, sampling the terrain under each one on
## every marker rebuild and every overlay repaint was three or four samples; with
## retail's whole authored set on the map (see `plot_regions`) the same code
## sampled the terrain ninety-eight times per pointer move AND ninety-eight times
## per repaint. The height under a plot is a function of retail's terrain and
## retail's authored coordinate, neither of which moves, so it is measured once
## when the plots are handed in.
var _plot_world_positions: Dictionary = {}


func _recompute_plot_world_positions() -> void:
	_plot_world_positions = {}
	if not has_map():
		return
	var fallback := float(bundle.terrain_extent.get("z_max", 0.0))
	for key in plots_by_region.keys():
		var region_id := String(key)
		var points: Array[Vector3] = []
		for spot_value in plots_by_region[key] as Array:
			var spot := spot_value as Vector2
			var height := fallback
			var sampled: Dictionary = bundle.sample_height(spot.x, spot.y)
			if bool(sampled["ok"]):
				height = float(sampled["height"])
			points.append(BundleScript.world_to_godot(spot.x, spot.y, height))
		_plot_world_positions[region_id] = points


## THE PROJECTION, SOLVED ONCE PER REPAINT INSTEAD OF ONCE PER POINT.
##
## WHY THIS EXISTS. `Camera3D.unproject_position` rebuilds the camera's transform,
## inverts it and rebuilds the projection matrix on EVERY CALL. One repaint of
## this overlay makes about 1,400 of them: 52 region anchors, 98 authored build
## plots, and eight corners for each of ~150 standing markers. Profiled with
## `wotr_perf_runner.gd --probe 3` at 1920x1080, the overlay repaint was 4.78 ms
## of a 7.9 ms panning frame, and the two sections that do the projecting - the
## plots at 2.15 ms and the banners at 1.95 ms - were 86% of it.
##
## IT IS THE SAME ARITHMETIC, NOT AN APPROXIMATION OF IT. Godot's own orthogonal
## unprojection is `((local.x / half_x) * 0.5 + 0.5) * panel.x` and the mirror on
## y, with the half-extents taken from `camera.size` and the viewport's aspect;
## that is exactly what is written below, with the inverse transform and the two
## half-extents hoisted out of the loop. `wotr_map3d_runner.gd` projects the same
## way for the same reason (a view driven outside a scene tree), so the runner and
## the view agree by construction rather than by luck.
##
## `transform` and not `global_transform`: the camera's parent is a SubViewport,
## which carries no 3D transform of its own, so the two are the same thing - and
## `global_transform` would require the view to be inside a tree, which a test
## driving it directly is not.
var _projection_inverse := Transform3D()
var _projection_half := Vector2.ONE
var _projection_panel := Vector2.ONE
var _projection_near := 0.05


func _refresh_projection() -> void:
	if camera == null:
		return
	_projection_inverse = camera.transform.affine_inverse()
	_projection_panel = Vector2(viewport.size) if viewport != null else size
	var half_vertical := maxf(float(camera.size), 0.0001) * 0.5
	_projection_half = Vector2(
		half_vertical * (_projection_panel.x / maxf(_projection_panel.y, 1.0)),
		half_vertical)
	_projection_near = camera.near


## Where a world point lands on the panel, using the hoisted projection above.
func _project_world(world: Vector3) -> Vector2:
	var local := _projection_inverse * world
	return Vector2(
		(local.x / _projection_half.x + 1.0) * 0.5 * _projection_panel.x,
		(1.0 - local.y / _projection_half.y) * 0.5 * _projection_panel.y)


## The same for a whole box, in ONE transform rather than eight unprojections.
## Under a parallel projection the map from world to panel is AFFINE, so the
## screen box of an axis-aligned box is its centre's projection plus the absolute
## row sums of the linear part against the half-extents - the standard transformed
## AABB, exact, not a bound. Returns an empty Rect2 when the box is behind the
## camera.
func _project_world_box(bounds: AABB) -> Rect2:
	var centre := bounds.get_center()
	var local := _projection_inverse * centre
	if local.z > -_projection_near:
		return Rect2()
	var half := bounds.size * 0.5
	var basis := _projection_inverse.basis
	var reach_x := absf(basis.x.x) * half.x + absf(basis.y.x) * half.y \
		+ absf(basis.z.x) * half.z
	var reach_y := absf(basis.x.y) * half.x + absf(basis.y.y) * half.y \
		+ absf(basis.z.y) * half.z
	# World half-extents into panel pixels, on each axis.
	var to_pixels := Vector2(
		0.5 * _projection_panel.x / _projection_half.x,
		0.5 * _projection_panel.y / _projection_half.y)
	var at := Vector2(
		(local.x / _projection_half.x + 1.0) * 0.5 * _projection_panel.x,
		(1.0 - local.y / _projection_half.y) * 0.5 * _projection_panel.y)
	var size_px := Vector2(reach_x * to_pixels.x * 2.0, reach_y * to_pixels.y * 2.0)
	return Rect2(at - size_px * 0.5, size_px)


## Where each placed region lands on screen this frame. Recomputed from the
## camera every draw; it is a projection, never stored state.
func _project_positions() -> void:
	_screen_positions = {}
	if camera == null or not has_map():
		return
	_refresh_projection()
	for key in _world_positions.keys():
		var world_position: Vector3 = _world_positions[key]
		if (_projection_inverse * world_position).z > -_projection_near:
			continue
		_screen_positions[String(key)] = _project_world(world_position)


# --- overlay ------------------------------------------------------------------

## HOW BIG THE OVERLAY DRAWS ITSELF AT THIS ZOOM.
##
## Everything the overlay paints - banners, plot rings, the build ring, labels -
## is in SCREEN space, so without this it would stay the same pixel size while
## the world under it grew ~29x. A banner that is a postage stamp over the whole
## map and still a postage stamp when one region fills the panel reads as a
## sticker on the glass rather than as a standard planted in a country.
##
## `_zoom` multiplies the whole-map distance, so it is SMALL when close in. The
## square root keeps the growth gentle - at the deepest zoom the world is ~29x
## nearer and the banner only 2.4x bigger - and both ends are clamped so nothing
## grows without bound or shrinks into invisibility.
func _view_scale() -> float:
	return clampf(sqrt(1.0 / maxf(_zoom, 0.0001)), 0.85, 2.4)


## The label size at this zoom, on a tighter range than the marks: type that
## doubles is unreadable long before it is too small.
func _label_font_size() -> int:
	return int(round(float(LABEL_FONT_SIZE) * clampf(_view_scale(), 0.85, 1.45)))


## HOW LONG THE LAST OVERLAY REPAINT TOOK, in milliseconds, and where it went.
##
## MEASURED RATHER THAN REASONED ABOUT, because round 8 opened with a pan budget
## that had drifted from 5.95 ms to ~9 ms and nobody could say which of the three
## candidates - the camera drive, the cut-edge search, the overlay - had taken it.
## `wotr_perf_runner.gd --probe 3` could isolate the overlay as a whole (panning
## with it hidden measured 4.53 ms against 9.43 with it), and could not see
## INSIDE it, so the answer stopped at "the overlay". These two fields are how it
## goes further. Two `Time` calls per section on a path that already walks 52
## regions and 98 build plots; the cost is under a microsecond and it is only paid
## on a repaint.
var overlay_paint_ms := 0.0
var overlay_section_ms: Dictionary = {}
var _paint_started := 0


func _draw_overlay() -> void:
	_paint_started = Time.get_ticks_usec()
	if not has_map():
		overlay.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.85))
		var font := get_theme_default_font()
		if font != null and not unavailable_reason.is_empty():
			var text := "RETAIL 3D MAP UNAVAILABLE\n\n%s" % unavailable_reason
			overlay.draw_multiline_string(
				font, Vector2(28.0, 48.0), text, HORIZONTAL_ALIGNMENT_LEFT,
				size.x - 56.0, 15, -1, ThemeScript.GOLD)
		return

	_project_positions()

	# AIR FIRST, so everything drawn after it stays crisp. See
	# `_draw_aerial_perspective`.
	_draw_aerial_perspective()

	# Adjacency edges, drawn under the markers - and ONLY for the regions the
	# player is acting from. The full 52-region web was engineering wireframe:
	# retail draws no permanent graph over Middle-earth, it answers "where can I
	# go" when you pick somewhere to go from. Selection and staging regions show
	# their edges; everything else shows terrain.
	var edge_sources: Array[String] = []
	if not selected_region.is_empty():
		edge_sources.append(selected_region)
	for staged_value in staging:
		var staged := String(staged_value)
		if not edge_sources.has(staged):
			edge_sources.append(staged)
	for region_id in edge_sources:
		if not _screen_positions.has(region_id):
			continue
		var from_point: Vector2 = _screen_positions[region_id]
		# ONLY THE LINES THAT LEAD SOMEWHERE THE PLAYER MAY GO, and round 9 cut the
		# rest. Every neighbour used to get a line - a 1.5 px `(0.92, 0.86, 0.66)`
		# hairline to each - and in the round-9 stage capture those read as
		# scratches ruled across Middle-earth: they are not information, they are
		# the same statement the region borders already make. What is left is the
		# ATTACK LANE, in the attacking seat's own hue so it belongs to the same
		# family as the reticle it ends on (`_draw_legal_target_mark`), soft enough
		# to sit under the terrain's own detail.
		var lane := target_ring_color(1.0)
		for neighbour_value in neighbours_by_region.get(region_id, PackedStringArray()):
			var neighbour := String(neighbour_value)
			if not _screen_positions.has(neighbour) or not Array(targets).has(neighbour):
				continue
			overlay.draw_line(from_point, _screen_positions[neighbour],
				Color(lane.r, lane.g, lane.b, 0.30), maxf(3.0 * _view_scale(), 3.0))

	var font := get_theme_default_font()
	for row in rows:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var color := _color_of(int(row["owner"]))
		var has_army := int(row["armies"]) > 0
		# ONCE THE TERRITORY IS SHADED, the marker is no longer how ownership is
		# read - the fill is - and once the ARMY BANNERS are drawn it is no longer
		# how an army is read either. So where both are present it shrinks to a
		# selection anchor. A region with no fill mesh keeps its full-size marker,
		# because for that region the marker is the only thing carrying ownership.
		var shaded := _territory_nodes.has(region_id)
		var radius := MARKER_RADIUS + (2.0 if has_army else 0.0)
		if shaded:
			# No disc is drawn for a shaded region any more, so this is now only
			# the radius the state RINGS are struck at. One value rather than two:
			# the rings are an annotation on the territory, and an annotation that
			# changed size depending on whether an army happened to be standing
			# there would read as a fault rather than as information.
			radius = MARKER_RADIUS * 0.62
		# A SHADED region at rest draws NO dot at all. Retail's map carries no
		# dot per region - ownership is the fill and the border - and fifty-two
		# dots over Middle-earth read as a diagnostic scatter plot. The dot
		# remains for the states that need an anchor (armies, selection, target,
		# staging, hover) and for any region with no fill mesh, where it is the
		# only thing carrying ownership at all.
		var at_rest := (shaded and not has_army
			and region_id != selected_region and region_id != selected_target
			and region_id != hover_region
			and not Array(targets).has(region_id)
			and not Array(staging).has(region_id))
		# ROUND 3 TOOK THE DISC AWAY FROM A SHADED REGION IN EVERY STATE, not
		# only at rest. A blind art review looked at the map and reported "map
		# tokens are untextured grey primitives ... flat discs with a faint inner
		# glyph", and it was right about what it was looking at: two of the three
		# round things on that capture were retail's own art (the Isengard ring
		# and Orthanc, which is a landmark sub-object of `livingmap.w3d`), and
		# the third was THIS - a `draw_circle` in the owner's colour with a black
		# halo, standing where retail puts a sculpted, gold-bezelled token.
		#
		# There is no retail art this project is entitled to put there instead:
		# retail's sculpted tokens are its `LivingWorldBuildingIcon` structures,
		# which this view now stands only on occupied authoritative build plots.
		# An empty region still has no generic token to place. What is fixed here
		# is the part that was this file's fault: a shaded region's state is
		# carried by retail's own geometry - the fill's strength and the bloomed
		# band, which round 3 made legible - plus the thin annotation rings
		# below, and a filled disc on top of that is a primitive standing in
		# front of retail art rather than a marker.
		#
		# A region with NO fill mesh keeps its disc: for that region the disc is
		# the only thing carrying ownership at all, and no disc would be worse
		# than a plain one.
		if not at_rest and not shaded:
			overlay.draw_circle(point, radius + 2.0, Color(0.03, 0.05, 0.03, 0.85))
			overlay.draw_circle(point, radius, color)
		if region_id == selected_region:
			overlay.draw_arc(point, radius + 6.0, 0.0, TAU, 28, ThemeScript.GOLD_BRIGHT, 3.0)
		elif Array(targets).has(region_id):
			_draw_legal_target_mark(point, radius)
		elif Array(staging).has(region_id):
			overlay.draw_arc(point, radius + 4.0, 0.0, TAU, 28, Color(0.85, 0.92, 0.75, 0.6), 1.5)
		if region_id == selected_target:
			overlay.draw_arc(point, radius + 10.0, 0.0, TAU, 28, Color("#e8623f"), 3.0)
		if region_id == hover_region:
			# GOLD, NOT WHITE. The hover ring was `Color(1,1,1,0.55)` on a map whose
			# mountains are rendered in snow; the round-8 review's rule for the
			# selection family applies to it word for word - "selection colour must
			# never collide with a terrain colour".
			overlay.draw_arc(point, radius + 13.0, 0.0, TAU, 28,
				Color(ThemeScript.GOLD_BRIGHT, 0.75), 1.8)

	var after_markers := Time.get_ticks_usec()
	_draw_build_plots()
	var after_plots := Time.get_ticks_usec()
	_draw_army_banners(font)
	var after_banners := Time.get_ticks_usec()
	_draw_region_labels(font)
	var after_labels := Time.get_ticks_usec()
	_draw_radial_menu(font)
	_draw_map_surround(font)
	var finished := Time.get_ticks_usec()
	overlay_paint_ms = float(finished - _paint_started) / 1000.0
	overlay_section_ms = {
		"project+markers": float(after_markers - _paint_started) / 1000.0,
		"plots": float(after_plots - after_markers) / 1000.0,
		"  plots.project": float(_plot_project_us) / 1000.0,
		"banners": float(after_banners - after_plots) / 1000.0,
		"  banners.boxes": float(_marker_box_us) / 1000.0,
		"labels": float(after_labels - after_banners) / 1000.0,
		"ring+surround": float(finished - after_labels) / 1000.0,
	}
	overlay_painted.emit()


# --- the map surround ------------------------------------------------------------

## A SOFT VIGNETTE AND A COMPASS ROSE. Round 1 also drew a hand-built parchment
## band, two box rules and four corner studs here; round 2 REMOVED them, and the
## reference capture is the reason: retail's strategic map is FULL-BLEED - it
## runs to the screen edge on all four sides with the HUD floating over it, and
## no frame of any kind sits between the map and the glass. A drawn gold
## rectangle said "panel inset" in every capture. What remains is the vignette,
## kept because it is atmosphere rather than chrome (and because it melts the
## border-cloud fringe into the corners), and the rose, kept because it is an
## instrument: the camera can be orbited a full circle and the rose is the only
## thing on screen that says which way north went.
##
## Both are HAND-BUILT, NOT CONVERTED: commit 83b1959 established that retail's
## frame art names three .tga files that are in no archive under any name.
##
## Drawn LAST so nothing else paints over the edge, and inside the clipped
## overlay so it cannot reach the rest of the screen.
const SURROUND_INK := Color(0.05, 0.045, 0.03, 0.72)
## The vignette: how deep the darkening reaches in from each panel edge, and how
## dark it is at the very edge. Shallower than round 1's 0.55: over a full-bleed
## map a heavy vignette read as a shadowed frame, which is the thing that was
## just removed.
const VIGNETTE_DEPTH := 110.0
## LIGHTENED IN ROUND 9 FROM 0.30, and the reason is that it is no longer the only
## thing shaping the edges of the frame. `_draw_aerial_perspective` now grades the
## whole board away from the theatre, and at the corners the two stack; at 0.30
## the far east went to mud instead of to distance. The vignette's job is narrow
## again - melting the border-cloud fringe into the corners - and the composition
## is the haze's.
const VIGNETTE_EDGE := Color(0.0, 0.0, 0.0, 0.20)
const VIGNETTE_CLEAR := Color(0.0, 0.0, 0.0, 0.0)
## Where the rose sits and how big it is. Bottom-right, clear of the seat table
## and of the region labels, which crowd the north-west of Middle-earth.
const COMPASS_RADIUS := 34.0
const COMPASS_INSET := Vector2(64.0, 64.0)

func _draw_map_surround(font: Font) -> void:
	# THE VIGNETTE. Four gradient strips, dark at the panel edge and clear a
	# hundred pixels in, so the border-cloud fringe and the sky fade into the
	# corners instead of ending on a visible cut. The corners receive two
	# strips and darken twice, which is exactly the corner falloff wanted.
	var depth := minf(VIGNETTE_DEPTH, minf(size.x, size.y) * 0.25)
	var fade := PackedColorArray([
		VIGNETTE_EDGE, VIGNETTE_EDGE, VIGNETTE_CLEAR, VIGNETTE_CLEAR])
	overlay.draw_polygon(PackedVector2Array([
		Vector2(0, 0), Vector2(size.x, 0),
		Vector2(size.x, depth), Vector2(0, depth)]), fade)
	overlay.draw_polygon(PackedVector2Array([
		Vector2(size.x, size.y), Vector2(0, size.y),
		Vector2(0, size.y - depth), Vector2(size.x, size.y - depth)]), fade)
	overlay.draw_polygon(PackedVector2Array([
		Vector2(0, size.y), Vector2(0, 0),
		Vector2(depth, 0), Vector2(depth, size.y)]), fade)
	overlay.draw_polygon(PackedVector2Array([
		Vector2(size.x, 0), Vector2(size.x, size.y),
		Vector2(size.x - depth, size.y), Vector2(size.x - depth, 0)]), fade)
	# NO BOX RULES AND NO STUDS. Retail's map meets the screen edge bare; the
	# gold rectangle round 1 drew here is what made every capture read as a
	# panel inset rather than a world.
	_draw_tray_feather()
	_draw_compass(font)


## ------------------------------------------------------------------------------
## "YOU CAN ACT HERE": THE MARK ON A REGION THE SELECTION MAY ATTACK.
## PROJECT-AUTHORED. Retail's living-world data authors no legal-target art and
## none is claimed; retail's own answer is the region effect mesh, which this view
## already drives (see `_apply_region_effect_meshes`) and which this stands on top
## of rather than replacing.
## ------------------------------------------------------------------------------
##
## TWO FINDINGS FROM THE ROUND-8 REVIEW COLLAPSE INTO ONE MARK, and that is why
## this is one function and not two.
##
##   * "Unlabelled circular markers on the western map (around x~280,y~205 and
##     x~275,y~455) have no legend and no relationship to anything else on
##     screen." Those were this: a 2 px `#c8483f` circle of radius 11 with a thin
##     red line running off it. They were not unlabelled decoration, they were the
##     legal attack targets and the adjacency the attack would use - the single
##     most important interactive state on the screen, drawn so quietly that a
##     professional reader took them for leftovers.
##
##   * "Nothing in this frame telegraphs that it is animated ... it needs at
##     minimum a lit, pulsing 'you can act here' state, and there isn't one." And:
##     "the screen says choose a region to attack. Nothing on the map indicates
##     WHICH regions are legal targets."
##
## SO THE MARK IS A RETICLE AND IT BREATHES. A steady inner ring in the attacking
## seat's own hue says which set this belongs to, four ticks at the diagonals say
## "aim", and an outer ring expands and fades on `TARGET_PULSE_SECONDS` - a mark
## that moves cannot be mistaken for a leftover, and motion is the one channel on
## this map that nothing in the terrain uses.
##
## THE HUE IS `target_ring_color()`'s, WHICH IS THE ATTACKER'S OWN. Not a colour
## of its own: `TARGET_EDGE_SATURATION` records why - "a target ring in an invented
## colour would be a seventh player on the map" - and reading it from there means
## the flat mark and retail's own 3D curtain cannot drift apart.
##
## COST: it is drawn once per target region, and there are rarely more than a
## handful. The repaint it needs is rate-limited and only runs while targets
## exist; see `drive_target_pulse`.
const TARGET_PULSE_SECONDS := 1.35
## The reticle's radii, as multiples of the marker radius the region is drawn at,
## for the steady ring and for the two ends of the travelling one.
## SIZED IN VIEW UNITS AND SCALED BY `_view_scale()`, because the marker radius
## they are struck from is 5.6 px at the strategic framing and a reticle drawn as
## a fixed offset off that was a 15-px ring on a 2560-px frame - which is how the
## round-8 mark came to be read as "unlabelled circular markers" in the first
## place. Enlarged in round 9 after looking at the capture: the first pass at
## 6/7..22 was still too quiet to say "you can act here".
const TARGET_RING_STEADY := 10.0
const TARGET_RING_TRAVEL := Vector2(12.0, 34.0)
## The soft disc under the reticle. It is what makes the mark findable in
## peripheral vision, which a hairline ring is not, and it is faint enough that
## the ownership fill underneath still names the owner.
const TARGET_HALO_ALPHA := 0.16
## How many times a second the overlay is repainted while a pulse is running. The
## pulse is a 1.35 s breath, so 20 Hz is already three times finer than the eye
## resolves on a slow ramp, and it keeps the idle frame budget
## (`wotr_frame_budget_runner`) as the paint-free thing it has always been on most
## frames.
const TARGET_PULSE_HZ := 20.0

var _pulse_phase := 0.0
var _pulse_repaint_due := 0.0

## ------------------------------------------------------------------------------
## THE SELECTION BREATHES, BECAUSE ITS COLOUR CANNOT MOVE.
## ------------------------------------------------------------------------------
##
## THE ROUND-8 REVIEW: "The hover/selection highlight is near-white, on a map that
## has snowcapped peaks rendered in near-white, in the far north, which is where
## the highlight currently is. The pale wash north of Arnor reads as snow before
## it reads as selection. Selection colour must never collide with a terrain
## colour. Pick a saturated hue or drive it with animated rim light instead of
## fill."
##
## THE FIRST OPTION IS NOT AVAILABLE AND THE ARITHMETIC SAYS WHY, so it was
## checked rather than assumed. The six seat colours sit at hues 218, 5, 115, 48,
## 285 and 178 - every sixty-degree sector of the wheel is already an owner - so
## there is no saturated hue left that would not read as a seventh player on a map
## whose entire ownership language is hue. Worse, the selection's LACK of chroma
## is a pinned property (`wotr_map3d_runner.SELECTION_CORE_SATURATION_CEILING`
## against `BAND_SATURATION_FLOOR`): the two marks are told apart by chroma where
## their shapes overlap, and giving the selection a hue would delete that.
##
## SO IT IS THE SECOND OPTION, WHICH THE REVIEW OFFERED AND WHICH IS THE BETTER
## ONE ANYWAY. The curtain's opacity now breathes on the same phase the legal-
## target reticles do. Snowfields do not breathe, and motion is a channel nothing
## in retail's terrain uses at all - so the mark is separated from the mountain by
## the one axis that has no collision on it, and the achromatic core, its clip to
## white, its sub-threshold peak and its chroma gap from the ownership band are all
## exactly what they were.
##
## The floor is the trough of the breath as a fraction of `SELECTION_EDGE_ALPHA`,
## which stays the peak. It also answers the "pale wash" half directly: the
## curtain's average opacity falls by about a fifth.
const SELECTION_RIM_FLOOR := 0.62


## The multiplier on the selection curtain's opacity this instant. Public and pure
## so a test can assert the mark moves without photographing two frames.
func selection_rim_gain() -> float:
	if selected_region.is_empty() and selected_target.is_empty():
		return 1.0
	# A cosine breath rather than a saw: the curtain is a large soft shape and a
	# linear ramp with a discontinuity at the wrap reads as a flicker on it.
	var breath := 0.5 - 0.5 * cos(_pulse_phase * TAU)
	return lerpf(SELECTION_RIM_FLOOR, 1.0, breath)


## Advance the "you can act here" breath. Split from `_process` for the same
## reason `drive_hover_flare` is: a test drives it directly, without a tree.
##
## IT DOES NOTHING WHEN THERE IS NOTHING TO PULSE. No targets means no phase
## advance and no repaint at all, so the idle strategic map costs exactly what it
## cost before this existed.
func drive_target_pulse(delta: float) -> void:
	if targets.is_empty() and selected_region.is_empty() and selected_target.is_empty():
		return
	_pulse_phase = fposmod(
		_pulse_phase + maxf(delta, 0.0) / maxf(TARGET_PULSE_SECONDS, 0.0001), 1.0)
	_pulse_repaint_due -= maxf(delta, 0.0)
	if _pulse_repaint_due <= 0.0:
		_pulse_repaint_due = 1.0 / TARGET_PULSE_HZ
		# The curtain is retail's own 3D mesh, so its breath is a material write
		# rather than a repaint - see `selection_rim_gain`. One region, one write.
		_write_selection_rim()
		_redraw()


## Push the current breath onto the selection curtain's material, and nothing
## else. Split out of `_apply_territory_colors` so a 20 Hz pulse does not walk all
## fifty-two regions to move one alpha.
func _write_selection_rim() -> void:
	for region_id in [selected_region, selected_target]:
		var key := String(region_id)
		if key.is_empty() or not _territory_nodes.has(key):
			continue
		var slot := _territory_nodes[key] as Dictionary
		_apply_region_effect_meshes(
			key, slot, _owner_of_region(key), _owner_of_region(key) >= 0)


## Where the breath is, 0..1. Public so a test can pin the phase and assert on the
## geometry rather than on a frame that happened to be captured.
func target_pulse_phase() -> float:
	return _pulse_phase


func _draw_legal_target_mark(point: Vector2, radius: float) -> void:
	var scale := _view_scale()
	var hue := target_ring_color(1.0)
	var solid := Color(hue.r, hue.g, hue.b, 1.0)
	var steady := radius + TARGET_RING_STEADY * scale
	# The halo, first and under everything: findable without being looked at.
	overlay.draw_circle(point, steady, Color(solid, TARGET_HALO_ALPHA))
	# The steady ring: the region is in the set whether or not you catch the
	# breath, so the set is readable in a still frame too.
	overlay.draw_arc(point, steady, 0.0, TAU, 32, Color(solid, 0.95),
		maxf(2.6 * scale, 2.6))
	# Four ticks at the diagonals. Short, outboard of the steady ring, and the
	# thing that makes the mark read as "aim here" rather than as a dot.
	for quarter in 4:
		var angle := PI * 0.25 + PI * 0.5 * float(quarter)
		var direction := Vector2(cos(angle), sin(angle))
		overlay.draw_line(
			point + direction * (steady + 3.0 * scale),
			point + direction * (steady + 10.0 * scale),
			Color(solid, 0.9), maxf(2.2 * scale, 2.2))
	# And the breath: one ring travelling outward and fading as it goes.
	var travel := _pulse_phase
	var ring := lerpf(TARGET_RING_TRAVEL.x, TARGET_RING_TRAVEL.y, travel) * scale
	overlay.draw_arc(point, radius + ring, 0.0, TAU, 36,
		Color(solid, 0.6 * (1.0 - travel) * (1.0 - travel)), maxf(2.4 * scale, 2.4))


## ------------------------------------------------------------------------------
## THE MAP GOING INTO SHADOW UNDER THE TRAY, instead of stopping at a seam.
## PROJECT-AUTHORED. Retail's strategic screen is full-bleed and authors nothing
## here.
## ------------------------------------------------------------------------------
##
## THE DEFECT, from the round-8 art review, about the trade that hides southern
## Middle-earth behind the HUD: "There is no edge affordance. The map simply
## terminates against a hard horizontal seam at y~730 with an unornamented
## straight edge ... That does not read as 'occluded', it reads as 'the map ends
## here'." The verdict on the trade was "keep it, but ... feather the map into the
## tray with a gradient rather than a hard seam".
##
## IT IS TWO JOBS IN ONE PASS AND THE SECOND ONE IS LOAD-BEARING.
##
##   1. THE VISIBLE ONE. `TRAY_FEATHER_REACH_FRACTION` of the panel ABOVE the
##      tray is taken into a gradient, so the last band of terrain the player can
##      see is already darkening when the tray's own top edge arrives. The seam
##      stops being where the map ends and becomes where the shadow finishes.
##
##   2. THE INVISIBLE ONE, WHICH IS WHY THIS IS NOT DECORATION. The southward
##      framing bias (`HUD_OCCLUDED_BOTTOM_FRACTION`) is only sound because
##      nothing can be seen in the occluded band, and this view must not have to
##      trust the HUD for that: the chrome is another stream's file, a player can
##      hide the HUD, and a capture can be taken with the tray absent. So the
##      bottom `TRAY_FEATHER_SOLID_FRACTION` of the band is inked SOLID by the map
##      itself. Whatever the slab's rim does down there, it is behind this.
##
## Drawn after the vignette and before the compass, which is the only thing that
## may sit on top of it - see `_draw_compass` for why the rose now rides above the
## band rather than inside it.
const TRAY_FEATHER_REACH_FRACTION := 0.085
## The ink the map goes into. Warm and near-black rather than neutral: it has to
## sit under an oxblood-and-gold tray without reading as a grey card, and the
## vignette above it is already neutral.
const TRAY_FEATHER_INK := Color(0.035, 0.028, 0.022)
## How opaque the feather already is at the tray's own top edge. Under a half, so
## the province at the seam is still readable ground going into shade rather than
## a band of paint.
const TRAY_FEATHER_AT_SEAM := 0.34


func _draw_tray_feather() -> void:
	var band := occluded_bottom()
	if band <= 1.0 or size.y < 8.0:
		return
	var solid_from := size.y - band * TRAY_FEATHER_SOLID_FRACTION
	var seam := size.y - band
	var reach := maxf(size.y * TRAY_FEATHER_REACH_FRACTION, 1.0)
	var clear := maxf(seam - reach, 0.0)
	var transparent := Color(TRAY_FEATHER_INK, 0.0)
	var at_seam := Color(TRAY_FEATHER_INK, TRAY_FEATHER_AT_SEAM)
	var opaque := Color(TRAY_FEATHER_INK, 1.0)
	# The visible ramp: clear terrain into shade, ending at the tray's own edge.
	overlay.draw_polygon(
		PackedVector2Array([
			Vector2(0.0, clear), Vector2(size.x, clear),
			Vector2(size.x, seam), Vector2(0.0, seam)]),
		PackedColorArray([transparent, transparent, at_seam, at_seam]))
	# The rest of the band, reaching full ink well before the panel's foot.
	overlay.draw_polygon(
		PackedVector2Array([
			Vector2(0.0, seam), Vector2(size.x, seam),
			Vector2(size.x, solid_from), Vector2(0.0, solid_from)]),
		PackedColorArray([at_seam, at_seam, opaque, opaque]))
	overlay.draw_rect(
		Rect2(0.0, solid_from, size.x, size.y - solid_from), opaque)


## ------------------------------------------------------------------------------
## AERIAL PERSPECTIVE: AIR BETWEEN THE PLAYER AND THE PART OF THE MAP HE IS NOT
## PLAYING. PROJECT-AUTHORED.
## ------------------------------------------------------------------------------
##
## THE DEFECT, from the round-8 art review: "The right 55% of the map - the entire
## eastern half of the continent - carries zero information, zero ownership, zero
## markers, and is rendered at the same visual weight as the active theatre. That
## is not restraint, that is an unresolved composition. The eye has nothing to do
## over there and no reason to look, yet it occupies more pixels than the actual
## game." Its prescription, verbatim: "depth-of-field falloff, atmospheric haze,
## or reduced saturation on out-of-theatre territory so the eye is pulled to the
## front line. Do not add UI there - add air."
##
## SO NO UI IS ADDED THERE. What is added is one alpha-blended cool grey, radially
## graded away from the theatre. Blending a mid-value cool grey over painted
## terrain does the two things haze does in life at once - it lifts the blacks and
## it pulls the chroma down - so the steppe reads as distance rather than as a
## dimmer copy of the front line. It is not a vignette: a vignette is keyed to the
## PANEL and this is keyed to the WAR.
##
## WHERE THE THEATRE IS, in falling order of how directly the player is in it:
## the selection and its target and the regions it can attack; failing that the
## regions this seat is staging from; failing that every region with an army on
## it; failing that every claimed region. There is always an answer while a game
## is running, and when there is none at all (an empty board, a fallback screen)
## NOTHING IS DRAWN - a haze centred on the middle of the panel by default would
## be a vignette pretending to be an idea.
##
## ONE TEXTURE, BUILT ONCE. A 128 px radial `GradientTexture2D` stretched over the
## panel; the cost is a single `draw_texture_rect` per repaint.
## LOOKED AT, NOT REASONED ABOUT, AND THE FIRST TRY WAS WRONG. The first pass set
## a cool blue-grey (0.42, 0.47, 0.56) at 0.34 and the capture
## (`captures/map-r3-stage`, before) showed the whole eastern and southern two
## thirds of Middle-earth smothered - the note in the constant below it said "above
## about 0.45 the far east stops being terrain and becomes fog" and 0.34 was
## already there, because the haze reaches its full depth at the FURTHEST CORNER
## and the theatre is not in the middle of the panel. So: warmer, so it reads as
## distance in a warm-lit world rather than as weather; lighter; and the clear
## window is wider, so the falloff spends most of its range on the near board.
const AIR_INK := Color(0.52, 0.53, 0.55)
const AIR_DEPTH := 0.20
## Where the clear window around the theatre ends and the haze starts to come in,
## as a fraction of the distance from the theatre to the furthest panel corner.
const AIR_CLEAR_FRACTION := 0.46
## The theatre's own extent is added to the clear window, so a wide front line
## does not get hazed at its own ends. Fraction of the theatre's screen radius.
const AIR_THEATRE_MARGIN := 1.35

var _air_texture: GradientTexture2D = null


## The centre and radius, in panel pixels, of the part of the map the player is
## actually playing - or a zero radius when there is no answer. Public and pure so
## a test can assert the haze is keyed to the war rather than to the panel.
func theatre_focus() -> Dictionary:
	var wanted: Array[String] = []
	if not selected_region.is_empty():
		wanted.append(selected_region)
	if not selected_target.is_empty():
		wanted.append(selected_target)
	for value in targets:
		wanted.append(String(value))
	for value in staging:
		wanted.append(String(value))
	if wanted.is_empty():
		for row in rows:
			if int(row["armies"]) > 0:
				wanted.append(String(row["id"]))
	if wanted.is_empty():
		for row in rows:
			if int(row["owner"]) >= 0:
				wanted.append(String(row["id"]))
	var points: Array[Vector2] = []
	for region_id in wanted:
		if _screen_positions.has(region_id):
			points.append(_screen_positions[region_id] as Vector2)
	if points.is_empty():
		return {"centre": Vector2.ZERO, "radius": 0.0, "regions": 0}
	var centre := Vector2.ZERO
	for point in points:
		centre += point
	centre /= float(points.size())
	var radius := 0.0
	for point in points:
		radius = maxf(radius, centre.distance_to(point))
	return {"centre": centre, "radius": radius, "regions": points.size()}


func _draw_aerial_perspective() -> void:
	var focus := theatre_focus()
	if int(focus["regions"]) <= 0 or size.x < 8.0 or size.y < 8.0:
		return
	var centre := focus["centre"] as Vector2
	# The half-width of a square centred on the theatre that still covers the
	# whole panel, so no corner is left un-hazed by the texture running out.
	var reach := 0.0
	for corner in [Vector2.ZERO, Vector2(size.x, 0.0), Vector2(0.0, size.y), size]:
		reach = maxf(reach, centre.distance_to(corner))
	if reach < 1.0:
		return
	var clear := clampf(
		maxf(AIR_CLEAR_FRACTION * reach, float(focus["radius"]) * AIR_THEATRE_MARGIN)
			/ reach, 0.05, 0.92)
	_air_texture = _air_gradient(clear)
	overlay.draw_texture_rect(_air_texture,
		Rect2(centre - Vector2(reach, reach), Vector2(reach * 2.0, reach * 2.0)), false)


## The haze texture for a given clear-window fraction, rebuilt only when that
## fraction actually moves - the window is a function of where the war is, and the
## war does not move on most repaints.
var _air_clear_built := -1.0

func _air_gradient(clear: float) -> GradientTexture2D:
	if _air_texture != null and absf(_air_clear_built - clear) < 0.01:
		return _air_texture
	var ramp := Gradient.new()
	ramp.offsets = PackedFloat32Array([0.0, clear, lerpf(clear, 1.0, 0.55), 1.0])
	ramp.colors = PackedColorArray([
		Color(AIR_INK, 0.0), Color(AIR_INK, 0.0),
		Color(AIR_INK, AIR_DEPTH * 0.55), Color(AIR_INK, AIR_DEPTH)])
	var texture := GradientTexture2D.new()
	texture.gradient = ramp
	texture.fill = GradientTexture2D.FILL_RADIAL
	texture.fill_from = Vector2(0.5, 0.5)
	texture.fill_to = Vector2(1.0, 0.5)
	texture.width = 128
	texture.height = 128
	_air_clear_built = clear
	return texture


## THE COMPASS ROSE, and the one thing about it that is NOT decoration: it turns
## with the camera. The map can be orbited a full circle, and a rose painted at a
## fixed angle would be a picture of a compass rather than a compass - it would
## say north was up while the player had turned Middle-earth ninety degrees.
## `_yaw` is the camera's own field, read and never written.
func _draw_compass(font: Font) -> void:
	# ABOVE THE TRAY, NOT IN IT. The rose used to sit `COMPASS_INSET` up from the
	# panel's own bottom-right corner, which at every window this screen is
	# captured at is inside the band the strategic tray stands on - so the one
	# instrument on the map has been drawn underneath the HUD for eight rounds and
	# no capture has ever shown it. It now rides on top of the feather's own edge.
	var centre := Vector2(size.x - COMPASS_INSET.x,
		size.y - occluded_bottom() - COMPASS_RADIUS - COMPASS_INSET.y * 0.35)
	if size.x < COMPASS_INSET.x * 2.0 or size.y < COMPASS_INSET.y * 2.0:
		return
	if centre.y < COMPASS_RADIUS + 8.0:
		centre.y = COMPASS_RADIUS + 8.0
	var radius := COMPASS_RADIUS
	overlay.draw_circle(centre, radius + 6.0, SURROUND_INK)
	overlay.draw_arc(centre, radius + 6.0, 0.0, TAU, 48, ThemeScript.GOLD, 1.0)
	overlay.draw_arc(centre, radius * 0.62, 0.0, TAU, 40, Color(0.90, 0.84, 0.62, 0.45), 1.0)
	# The map's north is +Y in retail world units, which `world_to_godot` sends to
	# -Z in Godot. A yaw of zero looks down +Z, so screen-up is north at yaw zero
	# and the rose turns with `_yaw` from there.
	for point in 8:
		var angle := -PI * 0.5 + TAU * float(point) / 8.0 + _yaw
		var long := (point % 2) == 0
		var length := radius if long else radius * 0.52
		var direction := Vector2(cos(angle), sin(angle))
		overlay.draw_line(centre, centre + direction * length,
			ThemeScript.GOLD_BRIGHT if long else Color(0.90, 0.84, 0.62, 0.5),
			2.0 if long else 1.0)
	if font == null:
		return
	var north := -PI * 0.5 + _yaw
	var at := centre + Vector2(cos(north), sin(north)) * (radius + 15.0)
	var glyph := 13
	var measured := font.get_string_size("N", HORIZONTAL_ALIGNMENT_LEFT, -1, glyph)
	overlay.draw_string(font, at - Vector2(measured.x * 0.5, -measured.y * 0.32),
		"N", HORIZONTAL_ALIGNMENT_LEFT, -1, glyph, ThemeScript.GOLD_BRIGHT)


# --- build plots ----------------------------------------------------------------

## EVERY AUTHORED BUILD PLOT ON THE MAP, ALWAYS, WHOEVER HOLDS THE GROUND.
##
## THE OWNER'S WORDS: "the building plots should always be visible even if you're
## an enemy so the player can see what the battlefield [is]." This used to return
## the four regions the player was touching - the selection, its target, the one
## under the pointer and the one the build ring stood open on - so 48 of retail's
## 52 provinces showed no plots at all and the strategic picture ("where can
## anybody build, and how much of it is already theirs") could only be read one
## province at a time. It is now retail's whole authored set: 52 regions, 98
## `BuildingSpot` points, all of them, all the time.
##
## THE ONE THING THAT MADE THAT UNAFFORDABLE IS GONE. Standing 98 foundation
## decals cost nothing extra per frame - they are static geometry - but the marker
## tree was torn down and rebuilt on every pointer move, and rebuilding a hundred
## and fifty nodes per mouse move is not affordable. `_marker_holders` reconciles
## instead, so a hover move now rebuilds the markers that actually changed. That
## change is what buys this one.
##
## OWNERSHIP STAYS LEGIBLE, and it is retail's own data that makes it legible: the
## decal a plot wears is the OWNING SEAT'S `BuildPlotIconName`, so an enemy's
## plots are visibly the enemy's faction art and this view invents no colour for
## them. A province NO seat holds has no `BuildPlotIconName` in retail's data at
## all - retail binds the icon to the player template, not to the region - so it
## gets this project's own ring instead, which is drawn plainly as the
## project-authored mark it is and named in `plot_markers_flat`.
##
## Deterministic: sorted region id, so the order never depends on dictionary
## iteration.
func plot_regions() -> PackedStringArray:
	var wanted: Array[String] = []
	for key in plots_by_region.keys():
		var region_id := String(key)
		if (plots_by_region.get(region_id, []) as Array).is_empty():
			continue
		wanted.append(region_id)
	wanted.sort()
	return PackedStringArray(wanted)


## THE REGIONS WHOSE PLOTS ARE DRAWN AT FULL STRENGTH - the ones the player is
## actually acting on. Every other province's plots are still drawn, at
## `PLOT_DISTANT_ALPHA`, so "where the battlefield is" reads across the whole map
## without the province under the pointer losing its emphasis.
func plot_focus_regions() -> PackedStringArray:
	var wanted: Array[String] = []
	for candidate in [selected_region, hover_region, selected_target,
			String(selected_plot.get("region", ""))]:
		var region_id := String(candidate)
		if region_id.is_empty() or wanted.has(region_id):
			continue
		if (plots_by_region.get(region_id, []) as Array).is_empty():
			continue
		wanted.append(region_id)
	return PackedStringArray(wanted)


## Project every shown plot into screen space. SEPARATE FROM THE DRAWING on
## purpose: a headless test can call this and assert that retail's authored plot
## points land on the map, which no test could do if the arithmetic only existed
## inside a `_draw` callback.
func project_plots() -> Dictionary:
	var projected: Dictionary = {}
	if camera == null or not has_map():
		return projected
	_refresh_projection()
	var panel := Vector2(viewport.size) if viewport != null else size
	for region_id in plot_regions():
		var spots: Array = _plot_world_positions.get(region_id, []) as Array
		var points: Array[Vector2] = []
		for spot_value in spots:
			var world := spot_value as Vector3
			# OFF THE PANEL IS THE SAME AS BEHIND THE CAMERA here, and worth its own
			# clause now that every one of retail's 98 authored plots is projected
			# on every repaint rather than the two or three under the pointer: a
			# plot the player cannot see must not cost a ring, an arc and two lines
			# in the overlay's draw list.
			if (_projection_inverse * world).z > -_projection_near:
				# BEHIND THE CAMERA is not "at the origin". A sentinel far outside
				# any viewport keeps the index aligned with retail's authored plot
				# order without putting a pickable marker in the corner.
				points.append(Vector2(-100000.0, -100000.0))
				continue
			var at: Vector2 = _project_world(world)
			var margin := PLOT_RADIUS * _view_scale() + 4.0
			if at.x < -margin or at.y < -margin 					or at.x > panel.x + margin or at.y > panel.y + margin:
				points.append(Vector2(-100000.0, -100000.0))
				continue
			points.append(at)
		projected[region_id] = points
	return projected


## The ring's radius at this zoom, so the icons stay clear of the plot marker and
## grow with everything else the overlay paints.
func radial_radius() -> float:
	return RADIAL_RADIUS * _view_scale()


## WHERE THE BUILD RING IS ACTUALLY DRAWN, which is the open plot's own screen
## position PULLED INBOARD until the whole ring fits on the panel.
##
## WHY IT IS NOT SIMPLY THE PLOT. A plot near an edge - and the strategic tray
## occupies the bottom of the panel, so "near an edge" includes most of southern
## Middle-earth - put half its ring off the screen, and the half that survived
## read, in a blind report, as "a column of dead buttons hanging off the right of
## the stone disc". Every slot is now reachable by the pointer, which is the
## minimum a menu owes the player; `_draw_radial_menu` draws a leader back to the
## plot so the ring never loses its subject.
##
## Public and separate from the drawing for the same reason `radial_slots` is: a
## test can assert the ring is on the panel without going through a `_draw` call.
func radial_centre() -> Vector2:
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var points: Array = _plot_screen_positions.get(region_id, []) as Array
	if index < 0 or index >= points.size():
		return Vector2.ZERO
	var at: Vector2 = points[index]
	var panel := size
	if panel.x < 4.0 or panel.y < 4.0:
		return at
	# The ring plus one icon plus the caption plate's own gap, which is the real
	# footprint the player has to be able to reach.
	var reach := radial_radius() + RADIAL_ICON * _view_scale() * 0.5 + RADIAL_CAPTION_GAP
	at = Vector2(
		clampf(at.x, reach, maxf(panel.x - reach, reach)),
		clampf(at.y, reach, maxf(panel.y - reach, reach)))
	return _pushed_clear_of_the_hud(at, reach, panel)


## THE HUD'S OWN RECTANGLES, IN THIS VIEW'S COORDINATE SPACE, that the build ring
## must not open underneath. Empty by default: this view does not own the HUD and
## must not guess where it is, so the chrome hands its island rectangles in and
## `radial_centre()` steers around whatever it is given.
##
## THE DEFECT THIS EXISTS FOR, in the chrome stream's own words: "the ring is
## jammed into the top-left corner, overlapping the Treasury plate, with two of
## its icons cut off by the screen edge" - photographed in
## `captures/chrome-r2-final/15b-structure-raised.png`, and reproduced in this
## lane's own `05-zoomed-in`. `radial_centre` clamped against the PANEL only,
## which is the right rule for a decoration and the wrong one for a control: the
## ring's icons are live buttons that spend treasure now, and a live button under
## another panel is a misclick factory.
var hud_keep_out: Array[Rect2] = []


## Hand in the HUD's occupied rectangles. Presentation only, and idempotent.
func set_hud_keep_out(rects: Array) -> void:
	var kept: Array[Rect2] = []
	for value in rects:
		var rect := value as Rect2
		if rect.size.x > 0.0 and rect.size.y > 0.0:
			kept.append(rect)
	var before := occluded_bottom()
	hud_keep_out = kept
	# The band at the foot of the panel is an input to the FRAMING, not only to
	# where a menu may open, so a new set of islands has to re-fit the camera -
	# but ONLY when the band it implies actually moved. The chrome may hand its
	# islands in on every refresh, and re-solving the fit on every refresh would
	# put a camera solve in the frame loop for a set of rectangles that did not
	# change. Compared as a number rather than as rectangles because the number is
	# the only part of them the framing reads.
	if has_map() and not is_equal_approx(before, occluded_bottom()):
		_fit_distance()
		_clamp_zoom()
		_apply_camera()
	_redraw()


## HOW MANY PIXELS OF THE PANEL'S FOOT THE HUD STANDS ON. See
## `HUD_OCCLUDED_BOTTOM_FRACTION` for what this is for and why the fallback is a
## stated assumption rather than a guess.
##
## FROM THE CHROME'S OWN ISLANDS WHEN IT HAS SUPPLIED THEM. The band is the
## deepest strip of the panel's foot that the islands cover ACROSS ITS WHOLE
## WIDTH - a strip only half covered is not occlusion, it is a gap the player can
## see the map through, so the rule takes the HIGHEST top edge among the islands
## that reach the panel's bottom and requires them together to span the width.
## Anything less falls back to the stated fraction, which is the conservative
## direction: a smaller band means a smaller bias and a shallower feather.
func occluded_bottom() -> float:
	var stated := size.y * HUD_OCCLUDED_BOTTOM_FRACTION
	if hud_keep_out.is_empty() or size.x < 4.0 or size.y < 4.0:
		return stated
	# The islands that actually touch the foot of the panel.
	var footers: Array[Rect2] = []
	for island in hud_keep_out:
		if island.end.y >= size.y - 1.0:
			footers.append(island)
	if footers.is_empty():
		return stated
	# Walk the candidate top edges from lowest (shallowest band) upwards and keep
	# the deepest one whose islands still cover every column.
	var tops: Array[float] = []
	for island in footers:
		tops.append(island.position.y)
	tops.sort()
	var deepest := 0.0
	for top in tops:
		var covered: Array[Vector2] = []
		for island in footers:
			if island.position.y <= top:
				covered.append(Vector2(island.position.x, island.end.x))
		covered.sort_custom(func(a: Vector2, b: Vector2) -> bool: return a.x < b.x)
		var reach := 0.0
		for span in covered:
			if span.x > reach + 1.0:
				break
			reach = maxf(reach, span.y)
		if reach >= size.x - 8.0:
			deepest = maxf(deepest, size.y - top)
	return deepest if deepest > 0.0 else stated


## Slide a ring of radius `reach` centred on `at` until it clears every rectangle
## the HUD claimed, without leaving the panel.
##
## THE RULE IS "SMALLEST MOVE THAT GETS OUT", per rectangle, along whichever axis
## needs the least travel - the standard minimum-translation escape - re-clamped
## to the panel after each move and repeated a few times, because escaping one
## island can walk the ring into the next. It is bounded rather than solved: with
## the tray, the palantir and the stats plate all claiming space at once there are
## panel shapes where no free position exists, and in that case the ring stops
## where the last pass left it rather than oscillating. That is still strictly
## better than the panel-only clamp - the ring ends beside the smallest overlap
## instead of on top of the largest - and `radial_centre` remains a pure function
## of the plot, the zoom and the rectangles it was handed, so a test can assert
## against it without opening a menu.
func _pushed_clear_of_the_hud(at: Vector2, reach: float, panel: Vector2) -> Vector2:
	if hud_keep_out.is_empty():
		return at
	var here := at
	for _pass in RADIAL_HUD_ESCAPE_PASSES:
		var moved := false
		for island in hud_keep_out:
			# The rectangle grown by the ring's own radius: the ring clears the
			# island exactly when its CENTRE is outside this.
			var blocked := island.grow(reach)
			if not blocked.has_point(here):
				continue
			var out_left := here.x - blocked.position.x
			var out_right := blocked.end.x - here.x
			var out_up := here.y - blocked.position.y
			var out_down := blocked.end.y - here.y
			var best := minf(minf(out_left, out_right), minf(out_up, out_down))
			if is_equal_approx(best, out_up):
				here.y = blocked.position.y - 0.5
			elif is_equal_approx(best, out_down):
				here.y = blocked.end.y + 0.5
			elif is_equal_approx(best, out_left):
				here.x = blocked.position.x - 0.5
			else:
				here.x = blocked.end.x + 0.5
			here = Vector2(
				clampf(here.x, reach, maxf(panel.x - reach, reach)),
				clampf(here.y, reach, maxf(panel.y - reach, reach)))
			moved = true
		if not moved:
			return here
	return here


## How many escape passes `_pushed_clear_of_the_hud` spends. Three: one to leave
## the island the plot sits under, one for the island that move lands on, and one
## to settle. A fourth has never changed the answer on any of the panel shapes the
## runners drive, and an unbounded loop here would hang the frame on a layout with
## no free position at all.
const RADIAL_HUD_ESCAPE_PASSES := 3


## The screen boxes the radial ring occupies, given the plot it is open on.
## Also separate from the drawing, and for the same reason.
func radial_slots() -> Array[Dictionary]:
	var slots: Array[Dictionary] = []
	if radial_entries.is_empty() or selected_plot.is_empty():
		return slots
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var points: Array = _plot_screen_positions.get(region_id, []) as Array
	if index < 0 or index >= points.size():
		return slots
	var centre := radial_centre()
	var count := radial_entries.size()
	for slot in range(count):
		# Straight up first, then clockwise - the reading order of retail's own
		# ring, and fixed rather than dependent on how many entries there are.
		var angle := -PI * 0.5 + TAU * float(slot) / float(count)
		var at := centre + Vector2(cos(angle), sin(angle)) * radial_radius()
		var icon := RADIAL_ICON * _view_scale()
		slots.append({
			"box": Rect2(at - Vector2(icon * 0.5, icon * 0.5), Vector2(icon, icon)),
			"entry": radial_entries[slot],
		})
	return slots


## The two halves of the plot and banner sections, split out because the first
## profile could not tell "projecting 98 plots" from "drawing 98 rings" and the
## answer decides which one is worth attacking. Read through `overlay_section_ms`.
var _plot_project_us := 0
var _marker_box_us := 0


func _draw_build_plots() -> void:
	var started := Time.get_ticks_usec()
	_plot_screen_positions = project_plots()
	_plot_project_us = Time.get_ticks_usec() - started
	var open_region := String(selected_plot.get("region", ""))
	var open_index := int(selected_plot.get("index", -1))
	var region_ids: Array[String] = []
	for key in _plot_screen_positions.keys():
		region_ids.append(String(key))
	region_ids.sort()
	var scale := _view_scale()
	var radius := PLOT_RADIUS * scale
	var focus := Array(plot_focus_regions())
	for region_id in region_ids:
		var points: Array = _plot_screen_positions[region_id] as Array
		# A PLOT THE PLAYER IS NOT ACTING ON IS STILL DRAWN, just quieter. See
		# `PLOT_DISTANT_ALPHA`.
		var near := focus.has(region_id)
		var strength := 1.0 if near else PLOT_DISTANT_ALPHA
		for index in range(points.size()):
			var at := points[index] as Vector2
			if at.x < -1000.0:
				continue
			# RETAIL'S OWN FOUNDATION DECAL IS ALREADY ON THE GROUND THERE. The
			# flat ring is the stand-in, drawn only where the decal is not.
			if _standing_keys.has("%s.plot%d" % [region_id, index]):
				continue
			var is_open := open_region == region_id and open_index == index
			var ring := ThemeScript.GOLD_BRIGHT if is_open else Color(0.95, 0.88, 0.62, 0.85)
			ring.a *= strength
			_draw_foundation_pad(at, radius, ring, strength, is_open)


## THE MARK ON A BUILD PLOT RETAIL GIVES NO ICON TO. PROJECT-AUTHORED, and this
## is the second time it has been drawn, so both the reason and the replacement
## are recorded.
##
## WHY THERE IS ANYTHING TO DESIGN. Retail binds the plot decal to the PLAYER
## TEMPLATE (`BuildPlotIconName`), not to the region, so a province NO seat holds
## has no plot art in retail's data at all - and thirty-four of the fifty-two
## provinces are unclaimed on turn one. The brief's rule is that an original
## design is allowed and pretending it is retail's is not, so this is drawn as
## plainly this project's and is named in `plot_markers_flat`.
##
## WHAT IT REPLACED AND WHY. Round 7 drew a circle, a 24-segment arc and two
## crossed ticks: a plus inside a ring, ninety-eight times across Middle-earth. It
## read as a survey diagram - the same "engineering scatter plot" a blind review
## used about the region dots - and it said nothing about what a plot IS.
##
## THIS IS A FOUNDATION PAD, seen from above at the map's own pitch: a dark stone
## seat, a rotated square footing in gilt, and one inner rule. The rotation is not
## decoration - retail's own foundation decal is a SQUARE pad and the strategic
## camera looks down at 47 degrees, so a square laid flat on that ground projects
## to a diamond, which is what this draws. It reads as a cleared plot of ground
## rather than as a target reticle, and it costs three canvas commands against the
## old four.
func _draw_foundation_pad(
	at: Vector2, radius: float, ring: Color, strength: float, is_open: bool
) -> void:
	var across := radius * 1.15
	var down := radius * 0.82
	var pad := PackedVector2Array([
		at + Vector2(0.0, -down), at + Vector2(across, 0.0),
		at + Vector2(0.0, down), at + Vector2(-across, 0.0),
	])
	# The seat, so the pad reads against bright terrain as well as dark.
	overlay.draw_colored_polygon(pad, Color(0.04, 0.05, 0.04, 0.62 * strength))
	var outline := PackedVector2Array(pad)
	outline.append(pad[0])
	overlay.draw_polyline(outline, ring, 2.4 if is_open else 1.7)
	# One inner rule across the short axis: enough to say "a footing, not a hole",
	# and not enough to become a diagram again.
	var inner := ring
	inner.a *= 0.55
	overlay.draw_line(at + Vector2(-across * 0.42, 0.0),
		at + Vector2(across * 0.42, 0.0), inner, 1.3)


# --- army banners ----------------------------------------------------------------

## ONE BANNER PER ARMY STACK, carrying retail's own portrait for that army.
##
## The portrait is an atlas crop resolved through retail's own authored links -
## the recruit button for that `PlayerArmy`, the one for that `HeroTemplateName`,
## or the owning template's `GarrisonSelectionPortraitName`. A stack whose
## portrait did NOT resolve gets a plate in the owner's colour with no image on
## it at all, and is named in `banners_without_portrait`. Nothing is substituted.
## The screen boxes retail's own 3D markers occupy this frame, from the projected
## corners of their real world-space bounds. Labels are placed AROUND these for
## exactly the reason commit 83b1959 established for the flat plates: a region
## name written across a banner costs both. Separate from the drawing so a test
## can assert it.
## `marker key -> Rect2`, filled by `project_marker_boxes` in the same sweep. The
## general's medallion has to be hung on the banner it belongs to, and the flat
## list of boxes cannot say which box is whose.
var _marker_boxes_by_key: Dictionary = {}
## `army id -> {box, region}` for the visible general medallions/stand-ins.
## Rebuilt by the draw that actually places them, so input and pixels share one
## geometry rather than approximating a hit target from the region centre.
var _army_hit_boxes: Dictionary = {}


func project_marker_boxes() -> Array[Rect2]:
	var boxes: Array[Rect2] = []
	_marker_boxes_by_key = {}
	if camera == null or not has_map():
		return boxes
	_refresh_projection()
	# EIGHT UNPROJECTIONS PER MARKER BECAME ONE TRANSFORM PER MARKER. There are
	# about 150 standing markers on this map once retail's 98 build plots are
	# raised, so the old loop made ~1,200 `unproject_position` calls on every
	# repaint - measured at 1.95 ms of a 4.78 ms overlay. See `_project_world_box`
	# for why one transform is not an approximation of eight.
	for row in _standing_markers:
		var rect := _project_world_box(row["aabb"] as AABB)
		if rect.size.x > 0.0 and rect.size.y > 0.0:
			boxes.append(rect.grow(3.0))
			_marker_boxes_by_key[String(row["key"])] = rect
	return boxes


func _draw_army_banners(font: Font) -> void:
	banners_drawn = 0
	flat_banners_drawn = 0
	medallions_drawn = 0
	banners_without_portrait = {}
	_army_hit_boxes = {}
	# SEEDED WITH RETAIL'S OWN 3D MARKERS FIRST. A banner that is now a model
	# still occupies screen space, and a label written over it would be the same
	# defect 83b1959 removed for the plates.
	var boxes_started := Time.get_ticks_usec()
	_banner_boxes = project_marker_boxes()
	_marker_box_us = Time.get_ticks_usec() - boxes_started
	if armies_by_region.is_empty():
		return
	var region_ids: Array[String] = []
	for key in armies_by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()
	var scale := _view_scale()
	var fan := BANNER_FAN * scale
	for region_id in region_ids:
		if not _screen_positions.has(region_id):
			continue
		var stacks: Array = armies_by_region[region_id] as Array
		if stacks.is_empty():
			continue
		var anchor: Vector2 = _screen_positions[region_id]
		var shown: int = mini(stacks.size(), MAX_BANNERS_PER_REGION)
		var span := float(shown - 1) * fan
		for index in range(shown):
			var key := "%s#%d" % [region_id, index]
			# RETAIL'S MODEL IS ALREADY STANDING THERE. The flat plate is the
			# STAND-IN, not the marker, so it is drawn only where the model is not
			# - but the model has no FACE on it, and that is what the medallion is.
			if _standing_keys.has(key):
				_draw_general_medallion(key, stacks[index] as Dictionary, font, scale)
				continue
			var stack := stacks[index] as Dictionary
			var at := anchor + Vector2(
				float(index) * fan - span * 0.5,
				-(BANNER_HEIGHT * scale * 0.5 + BANNER_STAFF * scale))
			_draw_one_banner(at, stack, font, scale)
			banners_drawn += 1
			flat_banners_drawn += 1
		if stacks.size() > shown and font != null:
			# THE OVERFLOW COUNT, ON A PLATE. It used to be a bare 12 px "+1" laid
			# straight on Middle-earth beside the banners, and the round-8 review
			# photographed exactly that class of mark: "unplated small white text
			# sitting directly on 3D terrain is the single most reliable visual
			# signature of an unshipped build". It is two glyphs, but two glyphs
			# with no substrate is the same defect as forty.
			var tail := anchor + Vector2(
				span * 0.5 + BANNER_WIDTH * scale * 0.5 + 4.0, -BANNER_STAFF * scale)
			var overflow := "+%d" % (stacks.size() - shown)
			var glyph := maxi(HudChromeScript.TYPE_MAP_FLOOR, 12)
			var measured := font.get_string_size(
				overflow, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph)
			var token := Rect2(tail - Vector2(0.0, float(glyph)),
				measured + Vector2(10.0, 6.0))
			HudChromeScript.draw_text_plate(overlay, token)
			overlay.draw_string(font, token.position + Vector2(5.0, 3.0 + float(glyph) * 0.82),
				overflow, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph, ThemeScript.PARCHMENT)


## ------------------------------------------------------------------------------
## THE GENERAL'S MEDALLION - the commanding hero's face, on the standing banner.
## ------------------------------------------------------------------------------
##
## THE OWNER'S WORDS: "there are no general icons on the flags, it should have the
## faction general icons." He was right, and the reason is worth recording because
## it is not that the portraits were missing. Every army stack already carries a
## `portrait_id` resolved through RETAIL'S OWN authored links - the recruit button
## for that `PlayerArmy`, the button for its `HeroTemplateName`, or the owning
## template's `GarrisonSelectionPortraitName` - and the living-world UI bundle
## resolves all 149 of them to a real `MappedImage` crop. The face was drawn only
## on the FLAT STAND-IN plate, i.e. only on the banners where retail's own 3D
## model had FAILED to stand. Every banner that worked properly had no face on it.
##
## So the medallion is hung on the standing model instead of replacing it. The
## crop is retail's; the DISC, the gilt ring and the owner's outer ring are this
## project's - retail's own living-world HUD sets hero portraits in a round gilt
## bezel on the palantir, and a square sticker on a waving banner would read as a
## sticker. A stack whose portrait did not resolve gets a bare owner-coloured disc
## with the army's initial on it, and is named in `banners_without_portrait`
## exactly as the flat plate's failure always was. Nothing is substituted.
const MEDALLION_SIDES := 28
## The medallion's diameter as a fraction of the standing marker's own projected
## width, and the pixel range it is held inside: too small to read is as useless
## as big enough to bury the banner it is hung on.
const MEDALLION_FRACTION := 0.52
const MEDALLION_MIN_DIAMETER := 15.0
const MEDALLION_MAX_DIAMETER := 54.0
## Where up the marker's own projected box the medallion sits. The banner cloth is
## the top of the model and the staff the bottom, so this is the cloth.
const MEDALLION_HEIGHT_FRACTION := 0.30


func _draw_general_medallion(
	key: String, stack: Dictionary, font: Font, scale: float
) -> void:
	if not _marker_boxes_by_key.has(key):
		return
	var box := _marker_boxes_by_key[key] as Rect2
	var diameter := clampf(
		box.size.x * MEDALLION_FRACTION, MEDALLION_MIN_DIAMETER, MEDALLION_MAX_DIAMETER)
	var radius := diameter * 0.5
	var centre := Vector2(
		box.position.x + box.size.x * 0.5,
		box.position.y + box.size.y * MEDALLION_HEIGHT_FRACTION)
	var army_id := int(stack.get("army_id", -1))
	if army_id >= 0:
		_army_hit_boxes[army_id] = {
			"box": Rect2(centre - Vector2(radius, radius),
				Vector2(diameter, diameter)).grow(5.0),
			"region": String(stack.get("region", "")),
		}
	var owner_color := _color_of(int(stack.get("owner", -1)))
	# The medallion occupies screen space and the label placer has to know, the
	# same way it knows about the plates and the standing markers.
	_banner_boxes.append(Rect2(centre - Vector2(radius, radius),
		Vector2(diameter, diameter)).grow(3.0))

	var portrait_id := String(stack.get("portrait_id", ""))
	var portrait: Texture2D = null
	if has_ui() and not portrait_id.is_empty():
		portrait = ui.image(portrait_id)
	# The bezel: a dark seat under the crop so a portrait with transparent corners
	# does not show the map through the general's head.
	overlay.draw_circle(centre, radius + maxf(2.0 * scale, 2.0), Color(0.05, 0.05, 0.04, 0.92))
	if portrait != null:
		_draw_portrait_disc(centre, radius, portrait)
	else:
		overlay.draw_circle(centre, radius,
			Color(owner_color.r, owner_color.g, owner_color.b, 0.62))
		var label := String(stack.get("label", "?"))
		banners_without_portrait[label] = (
			("retail authors no portrait for this army in the living-world data"
				if portrait_id.is_empty()
				else "the id %s did not resolve to an atlas crop" % portrait_id))
		if font != null:
			var initial := label.substr(0, 1).to_upper()
			var glyph := int(round(maxf(diameter * 0.56, 9.0)))
			var measured := font.get_string_size(
				initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph)
			overlay.draw_string(font, centre + Vector2(
				-measured.x * 0.5, float(glyph) * 0.34),
				initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph, Color(1, 1, 1, 0.9))
	# THE GILT BEZEL, then the owner's own hue outside it. Whose general it is has
	# to survive not recognising the face, which is the same rule the flat plate's
	# frame has always followed.
	overlay.draw_arc(centre, radius + 1.0, 0.0, TAU, MEDALLION_SIDES,
		ThemeScript.GOLD_BRIGHT, maxf(1.4 * scale, 1.4))
	overlay.draw_arc(centre, radius + maxf(2.4 * scale, 2.4), 0.0, TAU, MEDALLION_SIDES,
		owner_color, maxf(1.6 * scale, 1.6))
	if String(stack.get("kind", "")) == "hero":
		# A named hero gets a second, wider gilt ring - the one distinction retail's
		# own data supports here, since `kind` is authored.
		overlay.draw_arc(centre, radius + maxf(4.4 * scale, 4.4), 0.0, TAU,
			MEDALLION_SIDES, Color(ThemeScript.GOLD_BRIGHT, 0.55), maxf(1.2 * scale, 1.2))
	banners_drawn += 1
	medallions_drawn += 1


## A texture drawn as a DISC rather than as a rectangle: a fan of `MEDALLION_SIDES`
## points with UVs on the crop's inscribed circle, so the middle of the portrait
## survives and the corners - which on a `MappedImage` crop are background - do
## not. `draw_texture_rect` cannot do this; there is no clip on a `CanvasItem`.
func _draw_portrait_disc(centre: Vector2, radius: float, texture: Texture2D) -> void:
	var points := PackedVector2Array()
	var uvs := PackedVector2Array()
	for index in MEDALLION_SIDES:
		var angle := TAU * float(index) / float(MEDALLION_SIDES)
		var direction := Vector2(cos(angle), sin(angle))
		points.append(centre + direction * radius)
		uvs.append(Vector2(0.5, 0.5) + direction * 0.5)
	overlay.draw_colored_polygon(points, Color(1, 1, 1, 1), uvs, texture)


func _draw_one_banner(at: Vector2, stack: Dictionary, font: Font, scale: float) -> void:
	var owner_color := _color_of(int(stack.get("owner", -1)))
	var width := BANNER_WIDTH * scale
	var height := BANNER_HEIGHT * scale
	var plate := Rect2(at - Vector2(width * 0.5, height * 0.5), Vector2(width, height))
	_banner_boxes.append(plate.grow(3.0))
	var army_id := int(stack.get("army_id", -1))
	if army_id >= 0:
		_army_hit_boxes[army_id] = {
			"box": plate.grow(5.0),
			"region": String(stack.get("region", "")),
		}
	# The staff, so a banner reads as standing ON the region rather than floating.
	var staff_length := BANNER_STAFF * scale
	overlay.draw_line(
		Vector2(at.x, plate.position.y + height),
		Vector2(at.x, plate.position.y + height + staff_length),
		Color(0.16, 0.13, 0.09, 0.95), maxf(2.0 * scale, 2.0))
	# RETAIL'S OWN FACTION STANDARD, from `reinforcementbanners_001.dds`, flown off
	# the staff. It is bound to the seat by a DERIVED correspondence rather than an
	# authored field - the converter emits it only when `Faction<X> -> Banner_<X>`
	# is a total bijection over retail's seven playable factions - and the screen
	# says so. It is NOT a substitute for a missing portrait: a banner with no
	# portrait still shows an empty plate and still gets named.
	var standard: Texture2D = null
	if has_ui():
		standard = ui.faction_banner(String(stack.get("template", "")))
	if standard != null:
		var pennant_height := staff_length * 0.9
		var pennant := Rect2(
			Vector2(at.x + 1.0, plate.position.y + height + 1.0),
			Vector2(pennant_height * 1.33, pennant_height))
		overlay.draw_texture_rect(standard, pennant, false)
	# ONE GRAMMAR OF BANNER, NOT TWO, AND THIS IS THE HALF THAT CHANGED.
	#
	# THE ROUND-8 REVIEW: "Two visual grammars of banner on the map - dark navy
	# pair in the north, gold-and-navy trio at the frontier. (Taste call, unless
	# they encode different states, in which case it is a legend defect.)"
	#
	# THEY ENCODED NOTHING. It is a legend defect and the establishing answer is
	# recorded here. The gold-and-navy trio are retail's own 3D banner MODELS,
	# standing, wearing the gilt-bezelled general's medallion `_draw_general_
	# medallion` hangs on them. The dark navy pair are THIS function - the flat
	# stand-in drawn where the model did not stand - and they were a square dark
	# plate with a square owner-coloured frame. So the difference on screen was
	# "did retail's marker mesh resolve for this stack", which is an implementation
	# fact about the converter and not a fact about the war. A player reading two
	# grammars off that map would be reading a distinction that is not there.
	#
	# SO THE STAND-IN NOW WEARS THE SAME MEDALLION: the portrait as a disc, the
	# gilt bezel, the owner's ring outside it, the second gilt ring for a hero.
	# Every mark below is the one `_draw_general_medallion` makes, at the plate's
	# own size - the two are deliberately the same code path's worth of drawing so
	# they cannot drift apart again.
	var portrait_radius := minf(width, height) * 0.5
	var portrait_centre := plate.get_center()
	var portrait_id := String(stack.get("portrait_id", ""))
	var portrait: Texture2D = null
	if has_ui() and not portrait_id.is_empty():
		portrait = ui.image(portrait_id)
	overlay.draw_circle(portrait_centre, portrait_radius + maxf(2.0 * scale, 2.0),
		Color(0.05, 0.05, 0.04, 0.92))
	if portrait != null:
		_draw_portrait_disc(portrait_centre, portrait_radius, portrait)
	else:
		# NOT A STAND-IN PORTRAIT. The owner's own colour and the army's initial,
		# which is visibly not retail art - and the id that did not resolve is
		# recorded so the screen can name it.
		overlay.draw_circle(portrait_centre, portrait_radius,
			Color(owner_color.r, owner_color.g, owner_color.b, 0.62))
		var label := String(stack.get("label", "?"))
		banners_without_portrait[label] = (
			("retail authors no portrait for this army in the living-world data"
				if portrait_id.is_empty()
				else "the id %s did not resolve to an atlas crop" % portrait_id))
		if font != null:
			var initial := label.substr(0, 1).to_upper()
			var glyph := int(round(maxf(portrait_radius * 1.12, 9.0)))
			var measured := font.get_string_size(
				initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph)
			overlay.draw_string(font, portrait_centre + Vector2(
				-measured.x * 0.5, float(glyph) * 0.34),
				initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph, Color(1, 1, 1, 0.9))
	overlay.draw_arc(portrait_centre, portrait_radius + 1.0, 0.0, TAU, MEDALLION_SIDES,
		ThemeScript.GOLD_BRIGHT, maxf(1.4 * scale, 1.4))
	overlay.draw_arc(portrait_centre, portrait_radius + maxf(2.4 * scale, 2.4),
		0.0, TAU, MEDALLION_SIDES, owner_color, maxf(1.6 * scale, 1.6))
	if String(stack.get("kind", "")) == "hero":
		overlay.draw_arc(portrait_centre, portrait_radius + maxf(4.4 * scale, 4.4),
			0.0, TAU, MEDALLION_SIDES,
			Color(ThemeScript.GOLD_BRIGHT, 0.55), maxf(1.2 * scale, 1.2))


# --- region labels ----------------------------------------------------------------

## RETAIL'S NAMES, PLACED SO THEY CAN BE READ.
##
## Every region used to draw its label unconditionally, which piles Arnor,
## Ettenmoors and Fornost on top of each other in the north-west and makes all
## three unreadable. Labels are now placed in PRIORITY ORDER and a label whose box
## overlaps one already placed is HELD BACK and counted, rather than drawn into
## the pile. The priority is what the player is actually doing: the selection and
## its target first, then the pointer, then this seat's staging regions and the
## regions it can attack, then everything else by region id so the result is
## deterministic and does not flicker between frames.
## TRUE WHILE RETAIL'S OWN ENGRAVED TEXT PLANE IS THE THING LETTERING THE MAP, in
## which case this view writes no region names on it at all. Public and pure so
## the rule can be asserted without a paint - the drawing below is one `if` on it,
## and a headless runner cannot reach a `draw` callback.
func map_is_lettered_by_the_text_plane() -> bool:
	return _framing_fraction() > LABEL_REVEAL_FRACTION


func _draw_region_labels(font: Font) -> void:
	labels_drawn = 0
	labels_suppressed = 0
	labels_held_for_framing = 0
	if font == null:
		return
	var font_size := _label_font_size()
	var ordered := _label_order()
	# Seeded with the banners, so a name is never written across a portrait. The
	# banners were drawn first for exactly this reason.
	var placed: Array[Rect2] = _banner_boxes.duplicate()
	# AT THE STRATEGIC FRAMING THE MAP IS LETTERED BY THE TEXT PLANE, the way
	# retail's is: no floating white names over Middle-earth at the default
	# zoom. Only the regions the player is acting on keep theirs.
	# ROUND 9 CLOSED THE EXCEPTION THIS LINE USED TO CARRY, and the exception is
	# recorded rather than deleted: the selection, its target and the region under
	# the pointer used to keep their names at the strategic framing while every
	# other region was held back.
	#
	# THE ART REVIEW OF THE ROUND-8 CAPTURE asked for the on-map text layer to go
	# and for labels to be "culled below a zoom threshold". The threshold was here
	# and working; the three exceptions were what survived it, and in the capture
	# they are the worst instance of the defect on the frame - a 13 px sans "Arnor
	# x4" sitting one inch above retail's own engraved ARNOR, on the same ground,
	# at a tenth of its size. Two names for one province, one of them plated by
	# nothing.
	#
	# NOTHING IS LOST BY IT. Retail's TEXT PLANE letters the map at this framing
	# (that is what `_text_plane_alpha` fades in), and it is the lettering a blind
	# review singled out as better than 2006's. The army count the exception also
	# carried is on the banners: one medallion per stack and a plated `+N` for the
	# rest. Zoom past `LABEL_REVEAL_FRACTION` - where the text plane fades out -
	# and every label comes back exactly as before.
	var strategic := map_is_lettered_by_the_text_plane()
	for row in ordered:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		if strategic:
			labels_held_for_framing += 1
			continue
		var point: Vector2 = _screen_positions[region_id]
		var text := String(display_names.get(region_id, region_id))
		var armies := int(row["armies"])
		if armies > 0:
			text += "  x%d" % armies
		var measured := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		# Four places a label may sit, tried in order: right of the marker, left of
		# it, below it, above it. Trying alternates before giving up is what turns
		# "held back" from a common outcome into a rare one, and the order is fixed
		# so a label does not hop between frames.
		var offsets: Array[Vector2] = [
			Vector2(MARKER_RADIUS + 6.0, 4.0),
			Vector2(-(MARKER_RADIUS + 6.0 + measured.x), 4.0),
			Vector2(-measured.x * 0.5, MARKER_RADIUS + measured.y + 2.0),
			Vector2(-measured.x * 0.5, -(MARKER_RADIUS + 6.0)),
		]
		# THE ONES THE PLAYER IS ACTING ON ARE NEVER HELD BACK. A selection whose
		# name vanished because a neighbour got there first would be worse than
		# the overlap this whole function exists to remove.
		var forced := region_id == selected_region or region_id == selected_target or region_id == hover_region
		var origin := point + offsets[0]
		var box := Rect2()
		var found := false
		for offset in offsets:
			origin = point + offset
			box = Rect2(origin - Vector2(0.0, measured.y * 0.8), measured).grow_individual(
				LABEL_PADDING.x, LABEL_PADDING.y, LABEL_PADDING.x, LABEL_PADDING.y)
			var clash := false
			for other in placed:
				if other.intersects(box):
					clash = true
					break
			if not clash:
				found = true
				break
		if not found and not forced:
			labels_suppressed += 1
			continue
		placed.append(box)
		labels_drawn += 1
		var color := ThemeScript.PARCHMENT
		if forced:
			color = ThemeScript.GOLD_BRIGHT
		# NO PLATE. Retail letters its map straight onto the terrain; the grey
		# boxes read as debugging chrome. A heavy dark outline keeps the name
		# readable over any ground the camera can put behind it.
		overlay.draw_string_outline(font, origin, text, HORIZONTAL_ALIGNMENT_LEFT,
			-1, font_size, 5, Color(0.05, 0.04, 0.02, 0.88))
		overlay.draw_string(font, origin, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)


## Regions in the order their labels get first refusal on the space. Total and
## deterministic: every region appears exactly once, and ties break on region id.
func _label_order() -> Array[Dictionary]:
	var scored: Array[Dictionary] = []
	for row in rows:
		var region_id := String(row["id"])
		var rank := 6
		if region_id == selected_region or region_id == selected_target:
			rank = 0
		elif region_id == hover_region:
			rank = 1
		elif Array(targets).has(region_id):
			rank = 2
		elif Array(staging).has(region_id):
			rank = 3
		elif int(row["armies"]) > 0:
			rank = 4
		elif int(row["owner"]) >= 0:
			rank = 5
		scored.append({"rank": rank, "id": region_id, "row": row})
	scored.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a["rank"]) != int(b["rank"]):
			return int(a["rank"]) < int(b["rank"])
		return String(a["id"]) < String(b["id"]))
	var ordered: Array[Dictionary] = []
	for entry in scored:
		ordered.append(entry["row"] as Dictionary)
	return ordered


# --- the radial build menu ----------------------------------------------------------

## RETAIL'S RING OF BUILDING ICONS around the selected plot.
##
## Every entry is a `LivingWorldBuilding` retail marks `AvailableTo` this seat's
## template, with retail's own `ConstructButtonImage` on it. An entry whose image
## did not resolve draws an empty slot with its retail id under it - never a
## substitute icon.
##
## NOTHING HERE BUILDS ANYTHING. Construction is not in the simulation, and the
## screen says so beside the menu rather than letting a clickable ring imply a
## system that does not exist.
## The icon shadow that replaced the opaque plate; see `_draw_radial_menu`.
const RADIAL_ICON_SHADOW_OFFSET := 2.0
const RADIAL_ICON_SHADOW_ALPHA := 0.55
## The clear gap, in the same unscaled units as `RADIAL_RADIUS`, between the outer
## edge of retail's elvish script ring and the nearest edge of a slot icon. See
## the ring-drawing block in `_draw_radial_menu` for the defect it removes.
const RADIAL_RING_CLEARANCE := 5.0
## How much bigger the slot under the pointer is drawn, and how strong the halo
## behind it is. See the hover branch in `_draw_radial_menu`.
const RADIAL_HOVER_GROWTH := 0.18
const RADIAL_HOVER_HALO_ALPHA := 0.30


func _draw_radial_menu(font: Font) -> void:
	var slots := radial_slots()
	if slots.is_empty():
		return
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var anchor: Vector2 = (_plot_screen_positions[region_id] as Array)[index]
	var centre := radial_centre()
	var ring_radius := radial_radius()
	# A LEADER BACK TO THE PLOT when the ring had to be pulled inboard to stay on
	# the panel - see `radial_centre`. Without it the ring reads as a free-floating
	# column of buttons rather than as the menu belonging to one building site,
	# which is exactly how it was reported.
	if centre.distance_to(anchor) > 1.0:
		overlay.draw_line(anchor, centre, Color(0.90, 0.84, 0.62, 0.45),
			maxf(1.5 * _view_scale(), 1.5))
		overlay.draw_arc(anchor, 5.0 * _view_scale(), 0.0, TAU, 16,
			Color(0.95, 0.88, 0.62, 0.8), maxf(1.5 * _view_scale(), 1.5))
	# A soft backing, not a black disc: the ring must sit ON the map, and the
	# terrain reading through it is what keeps it a map element rather than a
	# modal dialog.
	# JUST BIG ENOUGH TO SEAT THE SLOTS, and softer than it was. At
	# `+ icon * 0.6` and 0.38 it read in the round-9 capture as a dark smudge on
	# Middle-earth a good deal wider than the ring standing in it; the field is the
	# ring's, not a plate of its own.
	overlay.draw_circle(centre, ring_radius + RADIAL_ICON * _view_scale() * 0.52,
		Color(0.02, 0.03, 0.02, 0.26))
	# RETAIL'S OWN RING. `apt_LivingWorldUI_1.tga` is the War of the Ring shell's
	# own texture sheet and it carries two elvish-script rings; the gold one is
	# picked by a stated rule over the sheet's pixels, never by a hand-written
	# index. With no bundle this falls back to a plain arc and the screen says the
	# UI bundle is absent.
	# THE SCRIPT RING NOW SITS INSIDE THE ICONS INSTEAD OF THROUGH THEM. The
	# round-8 review: "icons overlapping the ring's own frame - exactly the kind of
	# thing screenshot forums find." It was drawn at
	# `(ring_radius + icon/2) * 2` across, which puts its painted band at the same
	# radius the icons orbit at, so retail's elvish lettering ran straight under
	# every slot. `RADIAL_RING_CLEARANCE` is the gap between the ring's outer edge
	# and the nearest point of an icon; the ring is now a lettered field the slots
	# are hung around, which is a relationship rather than a collision.
	var elvish: Texture2D = ui.chrome_ring("gold") if has_ui() else null
	var inner := maxf(ring_radius - RADIAL_ICON * _view_scale() * 0.5
		- RADIAL_RING_CLEARANCE * _view_scale(), ring_radius * 0.35)
	if elvish != null:
		var span := inner * 2.0
		overlay.draw_texture_rect(elvish,
			Rect2(centre - Vector2(span * 0.5, span * 0.5), Vector2(span, span)), false)
	else:
		overlay.draw_arc(centre, inner, 0.0, TAU, 64, Color(0.90, 0.84, 0.62, 0.4), 1.0)
	# THE CAPTIONS ARE PLACED AGAINST EACH OTHER, not written where each slot
	# happens to sit. See `_draw_radial_caption`.
	var placed_captions: Array[Rect2] = []
	for slot_row in slots:
		var box := slot_row["box"] as Rect2
		var entry := slot_row["entry"] as Dictionary
		# THE SLOT UNDER THE POINTER LIGHTS UP, which is the owner's own word for
		# what these icons were not doing. Three things at once, because one is not
		# enough over painted terrain: the slot GROWS by
		# `RADIAL_HOVER_GROWTH`, a warm halo goes behind it, and retail's own
		# `RadialBorder` around it is drawn at full gold rather than at rest.
		# PROJECT-AUTHORED: retail's living-world data authors no hover art for a
		# build-ring slot, and none is claimed.
		var lit := not hover_build_entry.is_empty() \
			and String(entry.get("id", "")) == hover_build_entry
		if lit:
			box = box.grow(box.size.x * RADIAL_HOVER_GROWTH * 0.5)
			overlay.draw_circle(box.get_center(), box.size.x * 0.72,
				Color(ThemeScript.GOLD_BRIGHT, RADIAL_HOVER_HALO_ALPHA))
		var icon: Texture2D = null
		var image_id := String(entry.get("image_id", ""))
		if has_ui() and not image_id.is_empty():
			icon = ui.image(image_id)
		if icon != null:
			# NO PLATE BEHIND THE ICON. The owner's words: "why are there black
			# boxes around the icons, it should just be icons transparent." There
			# was an opaque near-black square here, `Color(0.05, 0.06, 0.05, 0.95)`
			# grown 2 px past the slot, and it was the most visible of three such
			# plates on this screen.
			#
			# THE CROPS DO NOT NEED IT. Retail's `ConstructButtonImage` crops off
			# `expansion1icons_020.dds` carry a real alpha channel and sample
			# (*, *, *, 0) at their corners, so the plate was not filling a hole in
			# the art - it was covering art that was already transparent where it
			# was meant to be, and squaring off a round icon.
			#
			# WHAT REPLACES IT IS A SHADOW OF THE ICON'S OWN SHAPE, not a smaller
			# box: the crop drawn once in near-black at `RADIAL_ICON_SHADOW_ALPHA`,
			# offset by `RADIAL_ICON_SHADOW_OFFSET`. Because it is the same texture,
			# its alpha is the icon's alpha, so the separation follows the artwork's
			# silhouette and disappears where the artwork does. Retail's own
			# `RadialBorder` ring below still gives every slot its edge.
			var shadow := RADIAL_ICON_SHADOW_OFFSET * _view_scale()
			overlay.draw_texture_rect(icon,
				Rect2(box.position + Vector2(shadow, shadow), box.size), false,
				Color(0.0, 0.0, 0.0, RADIAL_ICON_SHADOW_ALPHA))
			overlay.draw_texture_rect(icon, box, false)
		else:
			# THE PLATE SURVIVES FOR THE FAILURE CASE ONLY. An entry whose retail
			# crop did not resolve has no art to be transparent around, and the
			# plate plus the red "no icon" is the honest report that it did not.
			overlay.draw_rect(box, Color(0.18, 0.16, 0.12, 0.9))
			if font != null:
				overlay.draw_string(font, box.position + Vector2(3.0, RADIAL_ICON * 0.6),
					"no icon", HORIZONTAL_ALIGNMENT_LEFT, RADIAL_ICON - 6.0, 10,
					Color("#c8483f"))
		# RETAIL'S OWN RADIAL BORDER (`radialborders.dds`) around each slot, or a
		# plain rule when the bundle is absent.
		var border: Texture2D = ui.image("RadialBorder") if has_ui() else null
		if border != null:
			overlay.draw_texture_rect(border, box.grow(3.0), false,
				Color(1, 1, 1, 1) if lit else Color(1, 1, 1, 0.82))
		else:
			overlay.draw_arc(box.get_center(), box.size.x * 0.5 + 3.0, 0.0, TAU, 32,
				ThemeScript.GOLD_BRIGHT if lit else Color(0.90, 0.84, 0.62, 0.75),
				2.0 if lit else 1.0)
		if font != null and lit:
			_draw_radial_caption(font, box, ring_caption(entry), centre, placed_captions)


## ------------------------------------------------------------------------------
## THE ON-MAP TEXT LAYER, AND WHY ROUND 9 DELETED MOST OF IT
## ------------------------------------------------------------------------------
##
## AN ART DIRECTOR'S REVIEW OF THE ROUND-8 CAPTURE named this the single
## highest-value change in the whole frame, and quoting it is the shortest way to
## record why: "Four grey-white text strings plus cost numerals at ~10px ...
## stacked over the exact spot the eye goes first, the frontier where the blue
## bloc meets the red one. It is actively defacing the one thing this build
## unambiguously beats the 2006 product at." Its prescription was "delete the
## on-map text label layer, costs and all".
##
## ROUND 8 HAD ALREADY PLATED THESE and it was not enough, which is the finding
## worth keeping. Plating a caption fixes CONTRAST; it does not fix the fact that
## four captions exist at once. Four plates over painted terrain is four holes
## punched in the map, and the map is this screen's best asset.
##
## SO THE ANSWER IS NOT "PLATE IT BETTER", IT IS "DOES IT BELONG HERE AT ALL":
##
##   * THE COST IS GONE FROM THE MAP ENTIRELY. Every price is already in the
##     STRUCTURES roster in the tray, right-aligned under its own `Cost` heading,
##     and on the well's own tooltip. A numeral on the terrain was the third
##     statement of the same number and the only one with no column to align to.
##   * THE NAME IS DRAWN FOR ONE SLOT: the one under the pointer. At rest the ring
##     is retail's own `ConstructButtonImage` art inside retail's own
##     `RadialBorder`, and nothing else - which is what a modern radial menu does,
##     and what the ring's own icons are for. Four simultaneous names were a
##     legend for a picture that does not need one until you point at it.
##
## WHAT THIS COSTS AND WHY IT IS PAID. A player who never moves the pointer over a
## slot never learns the names from the ring. That is acceptable ONLY because the
## same four offerings, with names AND prices AND refusal reasons, are listed in
## the tray's roster at all times - it is a duplicate surface being removed, not
## an only surface. If the roster ever stops carrying them this has to come back.
##
## `radial_caption_plate` below is unchanged and still does the collision work: a
## caption can still be pushed off its default side by a plate already placed
## (the hovered slot is placed against the banners' boxes, not against nothing),
## and the runner still drives it with four captions at once because the
## PLACEMENT arithmetic must stay correct even though the drawing now asks less
## of it.


## THE TEXT ONE BUILD-RING SLOT CARRIES. Public and pure so a test can assert what
## the map is lettered with without opening a menu - and, specifically, that no
## price ever reaches the terrain.
func ring_caption(entry: Dictionary) -> String:
	return String(entry.get("title", entry.get("id", "?")))


## EVERY CAPTION THE RING WOULD DRAW THIS FRAME, in draw order. Empty at rest by
## construction: the ring letters the slot under the pointer and nothing else.
func ring_captions() -> PackedStringArray:
	var drawn := PackedStringArray()
	if hover_build_entry.is_empty():
		return drawn
	for slot_row in radial_slots():
		var entry := slot_row["entry"] as Dictionary
		if String(entry.get("id", "")) == hover_build_entry:
			drawn.append(ring_caption(entry))
	return drawn


## ONE BUILD-RING CAPTION, ON A PLATE, PLACED SO IT DOES NOT LAND ON ANOTHER.
##
## THREE DEFECTS IN ONE LINE OF DRAW CODE, all reported by a blind review of the
## round-6 capture: "Hall of the King's Men 5 / Dark Iron Forge 500 / Angmar
## Fortress 1500 / Mill ... at ~10px unstyled white, overlapping each other".
## Every word of that was accurate and every one of them was this call site:
##
##   * 11 px was written as a literal here and nowhere else, so it drifted from
##     the rest of the HUD. It is now `wotr_hud_chrome.TYPE_MAP_FLOOR`, the
##     shared floor for type set OVER THE MAP - the one place in this project
##     where the field behind the glyphs is painted terrain and can be any value
##     at all. Read from there rather than restated, so the two surfaces cannot
##     disagree about what the floor is.
##   * White type straight onto Middle-earth has no contrast guarantee whatever:
##     the same string is invisible over the Misty Mountains' snow and legible
##     over Mordor. `draw_text_plate()` is the chrome's own answer to that and it
##     is mandatory for anything drawn over the map.
##   * The old placement was a fixed offset under each icon, which is exactly why
##     the captions collided: the ring puts its slots at even angles, and two
##     adjacent slots' captions are wider than the arc between them. Four
##     candidate placements are tried per caption - below, above, outboard right,
##     outboard left - and the first that clears every plate already placed wins.
##     A caption with nowhere to go is DROPPED rather than stacked, because an
##     unreadable pile of overlapping names costs more than the one name it hides
##     and the icon under it still carries retail's own art.
##
## The ring's own geometry is untouched. A blind review called the radial
## placement intent "genuine and reasonable"; only the lettering was placeholder.
const RADIAL_CAPTION_GAP := 8.0
const RADIAL_CAPTION_PAD := Vector2(6.0, 3.0)

## WHERE ONE CAPTION'S PLATE GOES, or a zero-size Rect2 when there is nowhere
## for it. SEPARATE FROM THE DRAWING, and for the same reason `project_plots()`
## is separate from `_draw_build_plots()`: all three of the reported defects
## lived in this arithmetic, and arithmetic that only exists inside a `_draw`
## callback is arithmetic no headless test can reach. Godot refuses `draw_*`
## outside NOTIFICATION_DRAW, so a harness that wanted to check the placement
## had to stand up a real redraw; splitting the decision out means it does not.
##
## PURE: it reads the font, the panel size and the plates already placed, and it
## writes nothing. The caller appends the result.
func radial_caption_plate(
	font: Font, box: Rect2, caption: String, centre: Vector2,
	placed: Array[Rect2]
) -> Rect2:
	var size_px := maxi(HudChromeScript.TYPE_MAP_FLOOR, 12)
	var text_size := font.get_string_size(
		caption, HORIZONTAL_ALIGNMENT_LEFT, -1.0, size_px)
	var plate := Vector2(text_size.x, float(size_px)) + RADIAL_CAPTION_PAD * 2.0
	var outboard := (box.get_center() - centre).normalized()
	if outboard.length_squared() < 0.0001:
		outboard = Vector2.DOWN
	var candidates: Array[Vector2] = [
		# Below the icon, then above it - the two that keep the caption on the
		# ring's own radius and read as a caption rather than a callout.
		Vector2(box.get_center().x - plate.x * 0.5, box.end.y + RADIAL_CAPTION_GAP),
		Vector2(box.get_center().x - plate.x * 0.5,
			box.position.y - RADIAL_CAPTION_GAP - plate.y),
		# Then pushed away from the ring centre on the side the slot is already
		# on, which is where there is most room by construction.
		Vector2(box.end.x + RADIAL_CAPTION_GAP, box.get_center().y - plate.y * 0.5),
		Vector2(box.position.x - RADIAL_CAPTION_GAP - plate.x,
			box.get_center().y - plate.y * 0.5),
	]
	if outboard.x < 0.0:
		# Prefer the outboard side rather than always trying right first.
		var swap: Vector2 = candidates[2]
		candidates[2] = candidates[3]
		candidates[3] = swap
	for position in candidates:
		var rect := Rect2(position, plate)
		# Inside the panel, or it is clipped away and the slot is captionless for
		# no visible reason.
		if rect.position.x < 0.0 or rect.position.y < 0.0 				or rect.end.x > size.x or rect.end.y > size.y:
			continue
		var clear := true
		for taken in placed:
			if taken.intersects(rect):
				clear = false
				break
		if not clear:
			continue
		return rect
	# NOWHERE TO PUT IT, so it is DROPPED rather than stacked. An unreadable pile
	# of overlapping names costs more than the one name it hides, and the icon it
	# belongs to still carries retail's own art.
	return Rect2()


## Draw one caption where `radial_caption_plate` says it goes, and record the
## plate so the next caption has to clear it. The decision is up there; this is
## the three lines that put ink on the panel.
func _draw_radial_caption(
	font: Font, box: Rect2, caption: String, centre: Vector2,
	placed: Array[Rect2]
) -> void:
	var rect := radial_caption_plate(font, box, caption, centre, placed)
	if rect.size.x <= 0.0 or rect.size.y <= 0.0:
		return
	var size_px := maxi(HudChromeScript.TYPE_MAP_FLOOR, 12)
	HudChromeScript.draw_text_plate(overlay, rect)
	overlay.draw_string(font, rect.position + Vector2(
			RADIAL_CAPTION_PAD.x, RADIAL_CAPTION_PAD.y + float(size_px) * 0.82),
		caption, HORIZONTAL_ALIGNMENT_LEFT, rect.size.x - RADIAL_CAPTION_PAD.x * 2.0,
		size_px, ThemeScript.TEXT_LEAF)
	placed.append(rect)


func _color_of(owner: int) -> Color:
	if owner < 0 or owner >= owner_colors.size():
		return neutral_color
	return owner_colors[owner]


## An owner colour pushed to band strength: full chroma, near-full value, hue
## untouched. The reference outlines are BRIGHTER than the seat colours the
## fills use - lifting towards white would grey them, so the lift is in HSV
## where saturation survives it. Presentation only.
##
## ROUND 4 ADDS THE HEADROOM SOLVE. The value lift alone put every seat's band at
## or near 1.0, and `BORDER_HDR_GAIN` then clipped whichever other channels the
## hue happened to carry - which is what turned four of the six seat colours into
## white outlines under a linear tonemap. So after the lift, saturation is raised
## (hue and value untouched, and never lowered) until the SECOND-brightest channel
## would clear `BAND_SECOND_CHANNEL_CEILING` after the gain. See that constant for
## the arithmetic and the measurement. Saturation is the right lever because it
## pulls the two weaker channels towards zero along the hue's own line, so the
## hue that comes out is the hue that went in.
func _band_color(owner_color: Color) -> Color:
	var band := Color.from_hsv(
		owner_color.h,
		clampf(owner_color.s * 1.15, 0.0, 1.0),
		clampf(maxf(owner_color.v * 1.4, 0.85), 0.0, 1.0),
		1.0)
	var value := band.v
	if value <= 0.0:
		return band
	# In HSV each channel is `v * (1 - s * t)` for a per-channel `t` fixed by the
	# hue, so the saturation that lands the second channel exactly on the target
	# is a closed form rather than a search.
	var second := _second_channel(band)
	var target := BAND_SECOND_CHANNEL_CEILING / BORDER_HDR_GAIN
	if second <= target or second >= value:
		return band
	var wanted := (1.0 - target / value) * band.s / (1.0 - second / value)
	return Color.from_hsv(
		band.h, clampf(maxf(band.s, wanted), 0.0, 1.0), value, 1.0)


## The middle of a colour's three channels - the one that decides whether the
## band keeps its hue through the clip, because the brightest is MEANT to clip
## and the dimmest never gets near.
func _second_channel(color: Color) -> float:
	return maxf(minf(color.r, color.g),
		minf(maxf(color.r, color.g), color.b))


## How far over 1.0 this band may be driven. `BORDER_HDR_GAIN` unless the hue's
## second channel cannot survive it even at the saturation `_band_color` solved
## for - violet and teal sit between two primaries and cannot - in which case the
## gain gives way instead, down to `BORDER_HDR_GAIN_MIN` and no further, because
## under that the band stops clearing the glow threshold and loses its shoulder.
func _band_gain(band: Color) -> float:
	var second := _second_channel(band)
	if second <= 0.0:
		return BORDER_HDR_GAIN
	return clampf(BAND_SECOND_CHANNEL_CEILING / second,
		BORDER_HDR_GAIN_MIN, BORDER_HDR_GAIN)


## An owner colour brought to the fill's common chroma. Floors, never a rescale:
## see `FILL_SATURATION_FLOOR`. Hue is never touched - the hue IS the ownership.
func _fill_color(owner_color: Color) -> Color:
	return Color.from_hsv(
		owner_color.h,
		clampf(maxf(owner_color.s, FILL_SATURATION_FLOOR), 0.0, 1.0),
		clampf(maxf(owner_color.v, FILL_VALUE_FLOOR), 0.0, 1.0),
		1.0)


## The materials a region's territory is actually drawn with. Public so a test
## can assert on WHAT IS ON THE SCREEN rather than on what `_apply_territory_colors`
## intended - the two came apart badly enough in round 3 (see
## `BAND_SECOND_CHANNEL_CEILING`) that the distinction has to be testable. Empty
## for a region the bundle carries no territory geometry for.
func territory_materials(region_id: String) -> Dictionary:
	if not _territory_nodes.has(region_id):
		return {}
	var slot := _territory_nodes[region_id] as Dictionary
	return {
		"fill": slot.get("fill_material", null),
		"border": slot.get("border_material", null),
		"glow": slot.get("border_glow_material", null),
		"shoulders": slot.get("border_shoulder_materials", []),
		# Retail's own `LMR_Edge` and `LMR_Highlight`, so a test can assert on the
		# selection and home marks as the separate art they are rather than
		# inferring them from the ownership band.
		"selection_edge": slot.get("selection_edge_material", null),
		"home_highlight": slot.get("home_highlight_material", null),
	}


## Name the regions retail's `HomeRegionHighlight` is drawn for. The caller passes
## the seat capitals it already holds; this view never derives them. Safe to call
## before the territories are built - the marks are pushed on the next colour
## pass either way.
func set_home_regions(home: PackedStringArray) -> void:
	home_regions = home
	_apply_territory_colors()
	_redraw()


## An HDR albedo as the LINEAR tonemapper hands it to the screen. The band
## materials carry values well over 1.0 on purpose, so comparing albedo to albedo
## says nothing about what a player sees; this is the conversion that does.
static func clipped(color: Color) -> Color:
	return Color(minf(color.r, 1.0), minf(color.g, 1.0), minf(color.b, 1.0), 1.0)


## WHAT THE SCREEN ACTUALLY SHOWS for a seat's band, after the linear tonemapper
## has clipped it. The renderer writes `_band_color * _band_gain` into an HDR
## buffer and `TONE_MAPPER_LINEAR` clamps it, so this - not the albedo - is the
## colour a player's eye receives, and it is what a test has to assert on if it
## wants to catch a hue being destroyed on the way to the screen.
func rendered_band_color(owner: int) -> Color:
	var band := _band_color(_color_of(owner))
	var gain := _band_gain(band)
	return clipped(Color(band.r * gain, band.g * gain, band.b * gain, 1.0))


# --- input --------------------------------------------------------------------

## THE HELD-KEY AND SCREEN-EDGE CAMERA DRIVE, polled once a frame.
##
## WHY IT IS POLLED. A held key emits one pressed event and then silence, so a
## camera driven from `_gui_input` would step once per keypress rather than glide;
## and `_gui_input` only fires while this control has the focus, which the
## strategic screen's own buttons take away the moment one is clicked. Polling is
## what makes "hold W" mean what a player means by it.
##
## RESPECTING THE TEXT FIELDS IS NOT OPTIONAL AND IS NOT A HEURISTIC. `_pan_axis`
## refuses outright whenever the focused control is a text entry, so typing a
## save name or a chat line cannot drive Middle-earth sideways. It is asked of the
## VIEWPORT'S focus owner rather than of this control, because the field that
## steals the keystrokes is somewhere else on the screen entirely.
func _process(delta: float) -> void:
	# A HIDDEN MAP IS NOT DRIVEN. The strategic screen lives in a screen stack and
	# keeps processing while something else is on top of it; without this, a `W`
	# typed anywhere in the game would silently move a camera nobody is looking at
	# and the player would come back to a framing he never chose.
	if not is_visible_in_tree():
		return
	drive_hover_flare(delta)
	drive_target_pulse(delta)
	drive_zoom(delta)
	drive_camera(delta)


## The held-key and edge drive itself, split from `_process` so a test can run it
## without a visible tree. `_process` owns the visibility rule and this owns the
## camera; neither duplicates the other.
## ------------------------------------------------------------------------------
## HOW THE CAMERA FEELS, and the two measurements that drove this round of it.
## ------------------------------------------------------------------------------
##
## THE OWNER'S WORDS: "there needs to be a better way for me to zoom out and
## position the camera. It feels very cramped and awful to manoeuvre, it feels
## like the FOV or resolution is very low, like from the 2005 game."
##
## MEASUREMENT ONE, and it is the whole of "cramped". From the framing the screen
## OPENS at, half a second of held `D` moved the camera 4.8 world units - across
## a map 6,021 units wide. The camera was, for practical purposes, BOLTED DOWN
## until the player thought to zoom in first. The cause is not a bug in the pan:
## the view opens AT `zoom_ceiling()` by design (see `DEFAULT_ZOOM`), the ceiling
## is solved against the LIVE pan, and any pan lowers it - so `_pan_ground`'s wall
## search correctly found that no fraction of the step was affordable and moved
## essentially nothing. The rule was right and the resulting FEEL was indefensible.
##
## WHAT CHANGED: THE WALL LEANS INSTEAD OF STANDING. Pressing into it now spends
## `PAN_WALL_ZOOM_YIELD_PER_SECOND` of the player's zoom per second, and the pan
## takes whatever that buys. Measured on the shipped bundle at 2560x1440: a second
## of held `D` keeps 88% of the opening zoom and travels a few hundred units,
## against 36% kept and 1,926 units for the unwalled dive that the wall was
## written to stop, and 100% kept and 9 units for the wall as it stood. It is a
## THIRD behaviour, not a return to the second: the yield is a rate, so the longer
## you push the more you have spent, and letting go stops it dead.
##
## MEASUREMENT TWO: the drive had no ramp at all. The axis went from 0 to 1 and
## back to 0 on the frame a key changed state, so every pan started and stopped
## with a jolt - which is most of what "like from the 2005 game" describes. The
## velocity is now eased in over `PAN_ACCELERATION_SECONDS` and out over
## `PAN_DECELERATION_SECONDS`, which are short enough that the camera still feels
## keyed rather than floaty.
##
## THIS IS PROJECT-AUTHORED FEEL, not retail's. Retail's own camera constants for
## the living world are not in the archives this project has, and none is claimed.
## The RULE the wall enforces - that the terrain slab's cut edge must not enter
## the frame - is still exactly the rule it was.
const PAN_ACCELERATION_SECONDS := 0.12
const PAN_DECELERATION_SECONDS := 0.20
## Below this the eased velocity is treated as zero, so a released key stops the
## camera rather than leaving it creeping.
const PAN_VELOCITY_DEADBAND := 0.004
## How much of the player's zoom a second of pushing into the cut-edge wall
## spends. See the header above for the three-way measurement this sits between.
const PAN_WALL_ZOOM_YIELD_PER_SECOND := 0.12
## How fast the lean is GIVEN BACK once nothing is pressing into the wall - twice
## as fast as it is taken, because a framing that crawls back reads as broken
## while one that snaps back reads as elastic.
const PAN_WALL_ZOOM_RECOVER_PER_SECOND := 0.24
## THE MOST THE LEAN MAY EVER COST, as a multiplier on the player's own zoom
## request. Round 7 had no floor beyond `MIN_ZOOM`, so a pan held against the wall
## - or an edge scroll nobody meant to start - walked the camera all the way down
## to a fifth of a terrain tile. 0.6 leaves the lean enough room to visibly move
## the camera rather than stop dead and nowhere near enough to lose the board.
## The same allowance for a right-drag, which is per EVENT rather than per second
## because a drag's pace is the hand's, not the clock's.
const PAN_WALL_ZOOM_YIELD_PER_DRAG := 0.004
## A pan that achieved less than this fraction of what it asked for was stopped by
## the wall rather than merely clamped.
const PAN_WALL_BLOCKED_FRACTION := 0.35

## The eased -1..1 drive axes. See `PAN_ACCELERATION_SECONDS`.
const PAN_WALL_YIELD_FLOOR := 0.6

var _pan_velocity := Vector2.ZERO
## The temporary give the cut-edge lean has taken out of the player's zoom
## request, 1.0 when none. See `_yield_zoom_to_the_wall`.
var _wall_yield := 1.0


func drive_camera(delta: float) -> void:
	pan_by_axis(_pan_axis(), delta)


## ONE FRAME OF THE DRIVE, given the -1..1 axes directly.
##
## SPLIT OUT SO THE DRIVE CAN BE MEASURED WITHOUT THE INPUT SINGLETON.
## `drive_camera` polls `Input.is_key_pressed`, which answers only for a process
## that has a real viewport and has flushed its buffered events - so a probe or a
## runner that has neither can drive the camera through here and get exactly the
## same arithmetic. `drive_camera` keeps the poll and this keeps the camera;
## neither duplicates the other.
func pan_by_axis(axis_in: Vector2, delta: float) -> void:
	if not has_map() or camera == null:
		return
	var axis := axis_in
	# A diagonal must not be faster than a straight line.
	if axis.length() > 1.0:
		axis = axis.normalized()
	# THE RAMP. Accelerating and decelerating use different time constants because
	# a camera that takes as long to stop as it took to start reads as ice.
	var rising := axis.length() >= _pan_velocity.length()
	var constant := PAN_ACCELERATION_SECONDS if rising else PAN_DECELERATION_SECONDS
	_pan_velocity = _pan_velocity.lerp(
		axis, clampf(1.0 - exp(-maxf(delta, 0.0) / maxf(constant, 0.0001)), 0.0, 1.0))
	if _pan_velocity.length() < PAN_VELOCITY_DEADBAND:
		_pan_velocity = Vector2.ZERO
		return
	# The speed is a fraction of the PICTURE per second, so it is the same gesture
	# at every framing. See `PAN_SCREENS_PER_SECOND`.
	var speed := maxf(_camera_distance * _zoom, 0.0001) * PAN_SCREENS_PER_SECOND
	if Input.is_key_pressed(KEY_SHIFT):
		speed *= PAN_FAST_MULTIPLIER
	if Input.is_key_pressed(KEY_CTRL):
		speed *= PAN_SLOW_MULTIPLIER
	var step := speed * maxf(delta, 0.0)
	var achieved := _pan_ground(
		_pan_velocity.x * step, _pan_velocity.y * step / _ground_foreshortening())
	# PRESSED INTO THE WALL. See the header: the camera leans in rather than
	# refusing to move at all.
	#
	# ONLY FOR A DELIBERATE GESTURE, AND ONLY FOR AS LONG AS IT LASTS. Round 8
	# photographed what the round-7 rule actually did: the lean fired for the EDGE
	# SCROLL as well as for a held key, and it spent `_zoom_request` permanently.
	# So a pointer merely RESTING near the rim of the panel - which is where a
	# mouse sits most of the time, and where the capture runner's own pointer sat -
	# drove the camera into the wall at 12% of the zoom per second for as long as
	# nobody moved it, with no way back but a reset. Every capture in this lane's
	# round-7 set after the first one is a photograph of that: shot 01 frames
	# Middle-earth, and shot 02, taken seconds later with no camera action between
	# them, is a bay coastline at a tenth of the framing.
	#
	# TWO CHANGES AND BOTH ARE ABOUT INTENT. The lean now answers only what
	# `_pan_axis()` asks for - a held key, something the player is doing on purpose
	# - and never `_edge_scroll_axis()`, which is passive. And what it spends is a
	# SEPARATE, RECOVERABLE give (`_wall_yield`) rather than the player's own zoom
	# request, so letting go of the key hands the framing back.
	var deliberate := _pan_axis() != Vector2.ZERO
	if achieved < PAN_WALL_BLOCKED_FRACTION and axis != Vector2.ZERO and deliberate:
		_yield_zoom_to_the_wall(PAN_WALL_ZOOM_YIELD_PER_SECOND * maxf(delta, 0.0))
	elif _wall_yield < 1.0:
		_recover_from_the_wall(PAN_WALL_ZOOM_RECOVER_PER_SECOND * maxf(delta, 0.0))


## Spend `fraction` of the player's zoom request so a blocked pan has somewhere to
## go. Multiplicative, so it means the same thing at every framing, and floored at
## `MIN_ZOOM` so it can never run the camera into the ground.
func _yield_zoom_to_the_wall(fraction: float) -> void:
	# SPENT AS A SEPARATE GIVE, NOT OUT OF `_zoom_request`. Round 7 wrote the
	# reduced value straight into the request, which made the lean PERMANENT: the
	# player's own last explicit zoom was overwritten and the only way back was a
	# reset. `_wall_yield` multiplies the request instead (see `_clamp_zoom`), so
	# the lean is a lease rather than a sale and `_recover_from_the_wall` returns
	# it. Floored at `PAN_WALL_YIELD_FLOOR` so a long hold cannot dive.
	#
	# IT STILL BITES AT ONCE, which is the half of round 7's reasoning that was
	# right and is kept: the give is measured against where the camera ACTUALLY IS
	# rather than against the request, because the view can open with the request
	# above the ceiling and yielding from the request would spend the dead slack
	# between the two before moving anything at all - 4.6 units in the first
	# second, photographed.
	var live_ratio := clampf(_zoom / maxf(_zoom_request, 0.0001), 0.0, 1.0)
	var wanted := maxf(
		minf(_wall_yield, live_ratio) * (1.0 - clampf(fraction, 0.0, 1.0)),
		PAN_WALL_YIELD_FLOOR)
	if is_equal_approx(wanted, _wall_yield):
		return
	_wall_yield = wanted
	# The smoothed wheel must not fight this; see `drive_zoom`.
	_zoom_goal = _zoom_request
	_zoom_easing = false
	_clamp_zoom()
	_apply_camera()
	_redraw()


## Give the lean back. Called every frame the player is NOT pressing into the
## wall, so the framing he asked for returns on its own the moment he stops -
## which is the property that makes the lean bearable at all.
func _recover_from_the_wall(fraction: float) -> void:
	var wanted := minf(_wall_yield + clampf(fraction, 0.0, 1.0), 1.0)
	if is_equal_approx(wanted, _wall_yield):
		return
	_wall_yield = wanted
	_clamp_zoom()
	_apply_camera()
	_redraw()


## The camera drive's own -1..1 axes this frame: `x` across the picture, `y` up
## it. Held keys and the screen edge sum into the same pair, so one is a fallback
## for the other rather than a second code path.
##
## THE FOCUS RULE IS HERE AND ONLY HERE. A `LineEdit`, `TextEdit` or `SpinBox`
## with the focus owns every keystroke, so the keyboard half returns nothing at
## all while one has it; the EDGE half is unaffected, because moving the mouse to
## the rim of the panel is not something a player does by accident while typing.
func _pan_axis() -> Vector2:
	var axis := Vector2.ZERO
	if keyboard_pan_enabled and not _a_text_field_has_the_keyboard():
		if Input.is_action_pressed("cam_forward"):
			axis += Vector2(0.0, 1.0)
		if Input.is_action_pressed("cam_back"):
			axis += Vector2(0.0, -1.0)
		if Input.is_action_pressed("cam_left"):
			axis += Vector2(-1.0, 0.0)
		if Input.is_action_pressed("cam_right"):
			axis += Vector2(1.0, 0.0)
	if edge_scroll_enabled and not _dragging:
		axis += _edge_scroll_axis() * EDGE_SCROLL_MULTIPLIER
	return Vector2(clampf(axis.x, -1.0, 1.0), clampf(axis.y, -1.0, 1.0))


## Whether some text entry currently owns the keyboard. Asked of the focus owner
## by class rather than by a list of node names, so a field added to the strategic
## screen later is covered without this file being told about it.
func _a_text_field_has_the_keyboard() -> bool:
	if not is_inside_tree():
		return false
	var window := get_viewport()
	if window == null:
		return false
	return keyboard_is_owned_by_text(window.gui_get_focus_owner())


## Whether a focused node is a text entry, i.e. whether the keystrokes belong to
## it rather than to the camera. Static and public so a test can assert the RULE
## on every class it is supposed to cover: focus itself cannot be granted in a
## headless run (`grab_focus` needs a visible tree, which a headless root window
## never is), so a test that could only go through the focus owner would be
## asserting nothing at all.
static func keyboard_is_owned_by_text(focused: Node) -> bool:
	if focused == null:
		return false
	return (focused is LineEdit or focused is TextEdit or focused is SpinBox)


## The screen-edge drive's own -1..1 axes. Zero unless the pointer is inside this
## control's own rectangle AND within the edge band, and zero whenever the window
## is not focused - see `EDGE_SCROLL_BAND_FRACTION` for both.
func _edge_scroll_axis() -> Vector2:
	if not is_inside_tree() or not get_window().has_focus():
		return Vector2.ZERO
	var panel := size
	if panel.x < 4.0 or panel.y < 4.0:
		return Vector2.ZERO
	var at := get_local_mouse_position()
	if at.x < 0.0 or at.y < 0.0 or at.x > panel.x or at.y > panel.y:
		return Vector2.ZERO
	var band := maxf(minf(panel.x, panel.y) * EDGE_SCROLL_BAND_FRACTION, 4.0)
	var axis := Vector2.ZERO
	if at.x <= band:
		axis.x = -1.0
	elif at.x >= panel.x - band:
		axis.x = 1.0
	# Screen y grows downward and the camera's forward axis grows up the picture.
	if at.y <= band:
		axis.y = 1.0
	elif at.y >= panel.y - band:
		axis.y = -1.0
	return axis


## WHERE THE GROUND UNDER A PANEL PIXEL IS, on the horizontal plane the camera is
## looking at, or null when the pixel's ray never reaches it.
##
## Built from the camera's own transform - the one `look_at_from_position` wrote -
## rather than from `Camera3D.project_ray_*`, for the reason `_pan` states: this
## view is driven outside a scene tree by the runners, and the projection helpers
## refuse there. Under a parallel projection the ray's DIRECTION is the same for
## every pixel and only its origin moves, which is what the two basis terms are.
func _ground_point_at(pixel: Vector2) -> Variant:
	var ray := _pointer_ray(pixel)
	if ray.is_empty():
		return null
	var origin := ray["origin"] as Vector3
	var direction := ray["direction"] as Vector3
	if absf(direction.y) < 0.0001:
		return null
	var travel := (_camera_target.y - origin.y) / direction.y
	if travel <= 0.0:
		return null
	return origin + direction * travel


## ZOOM THAT LANDS WHERE THE POINTER IS, the way a strategic map is expected to.
##
## WHAT IT DID BEFORE, checked rather than assumed: `_set_zoom` wrote the size and
## nothing else, and `_camera_target` is the middle of the picture - so every
## notch closed on the CENTRE OF THE PANEL and the province the player was
## pointing at slid away from the cursor as he zoomed towards it. On a map where
## the thing you want is almost never in the middle, that is a fight.
##
## THE RULE IS THE ONE EVERY RTS USES: the ground under the cursor does not move.
## It is enforced rather than approximated - the ground point under the pixel is
## measured before the zoom and again after it, and the target is shifted by the
## difference, so whatever the clamps and the ceiling then do to the zoom, the
## anchor is computed against the framing that actually resulted.
##
## IT DEFERS TO THE PAN CLAMP. `_clamp_camera_target` still bounds where the
## camera may be left, so pointing at a corner of the border cloud and spinning
## the wheel cannot walk the camera off Middle-earth; the anchor simply stops
## being exact once the clamp binds, which is the correct order of precedence.
## HOW LONG A WHEEL NOTCH TAKES TO ARRIVE.
##
## PROJECT-AUTHORED, and it exists because a stepped zoom is the other half of
## "awful to manoeuvre". Every notch used to be applied on the frame it arrived:
## the picture jumped by a factor of `ZOOM_STEP` between two frames, and a player
## rolling the wheel got a slide show rather than a movement. 0.11 s is short
## enough that the camera still answers the hand immediately - the first frame of
## the ease already covers about half the notch - and long enough that the eye
## reads a MOVE rather than a CUT.
##
## THE EASE IS GEOMETRIC, not linear, because the zoom is a scale: interpolating
## in the log means each frame closes the same FRACTION of what is left, which is
## what makes a fast roll of several notches feel like one continuous approach
## rather than a series of decelerations.
##
## IT NEVER DEFERS AN API CALL. `_set_zoom`, `focus_region` and `reset_camera`
## still land on the frame they are called on and cancel any ease in flight - a
## test or a capture that asks for a framing gets that framing, not a framing it
## is on the way to. Only the WHEEL is smoothed.
const ZOOM_EASE_SECONDS := 0.11
## Where the eased zoom is heading, the pixel it is anchored on, and whether an
## ease is in flight at all.
var _zoom_goal := DEFAULT_ZOOM
var _zoom_anchor := Vector2.ZERO
var _zoom_easing := false


## Aim the wheel's zoom. The ease itself runs in `drive_zoom`.
func _zoom_towards(value: float, pixel: Vector2) -> void:
	_zoom_goal = clampf(value, MIN_ZOOM, MAX_ZOOM)
	_zoom_anchor = pixel
	_zoom_easing = true


## One frame of the wheel's ease. Split from `_process` so a test can run it
## without a visible tree, the way `drive_camera` is.
func drive_zoom(delta: float) -> void:
	if not _zoom_easing:
		return
	var from := maxf(_zoom_request, 0.00001)
	var to := maxf(_zoom_goal, 0.00001)
	var blend := clampf(
		1.0 - exp(-maxf(delta, 0.0) / maxf(ZOOM_EASE_SECONDS, 0.0001)), 0.0, 1.0)
	var next := exp(lerpf(log(from), log(to), blend))
	if absf(next - to) <= to * 0.004:
		next = to
		_zoom_easing = false
	_zoom_towards_now(next, _zoom_anchor)


func _zoom_towards_now(value: float, pixel: Vector2) -> void:
	var before: Variant = _ground_point_at(pixel)
	_set_zoom(value, true)
	if before == null:
		return
	var after: Variant = _ground_point_at(pixel)
	if after == null:
		return
	var drift: Vector3 = (before as Vector3) - (after as Vector3)
	_camera_target.x += drift.x
	_camera_target.z += drift.z
	_clamp_camera_target()
	_clamp_zoom()
	_apply_camera()
	_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _dragging:
			if not _orbiting:
				_right_drag_distance += motion.relative.length()
			if _orbiting:
				_orbit(motion.relative)
			else:
				_pan(motion.relative)
			return
		# THE BUILD RING IS A MENU STANDING OVER THE MAP, so it is asked first and
		# it swallows the move. Without this the province under the ring kept
		# flaring as the pointer crossed the icons, which reads as the menu not
		# being there at all - and the icons themselves never lit, which is half of
		# what the owner reported.
		var over_entry := build_entry_at(motion.position)
		var entry_id := String(over_entry.get("id", ""))
		if entry_id != hover_build_entry:
			hover_build_entry = entry_id
			build_entry_hovered.emit(
				String(selected_plot.get("region", "")),
				int(selected_plot.get("index", -1)), entry_id)
			_redraw()
		if not over_entry.is_empty():
			return
		var hovered := region_at(motion.position)
		# THE PLOT UNDER THE POINTER, not only the region. Retail gates a build
		# plot's ring on `HideWhenUnhilighted`, which is a per-PLOT state: moving
		# from one plot to the next inside one region has to move the ring, and
		# while hover was tracked per region it could not.
		var over := plot_at(motion.position)
		var moved_region := hovered != hover_region
		var moved_plot := not _same_plot(over, hover_plot)
		if not moved_region and not moved_plot:
			return
		hover_region = hovered
		hover_plot = over
		if moved_region:
			region_hovered.emit(hovered)
			_apply_territory_colors()
		# Retail's hover art is a SLOT of the marker family
		# (`HideWhenUnhilighted`), so what is standing in the world changes when
		# the pointer moves, not only what the overlay paints. Rebuilt for either
		# kind of move, because a plot ring and a region highlight are both slots.
		_rebuild_markers()
		_redraw()
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	match button.button_index:
		MOUSE_BUTTON_WHEEL_UP:
			if button.pressed:
				_zoom_towards(_zoom / ZOOM_STEP, button.position)
		MOUSE_BUTTON_WHEEL_DOWN:
			if button.pressed:
				_zoom_towards(_zoom * ZOOM_STEP, button.position)
		MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE:
			# RIGHT DRAG PANS, MIDDLE DRAG ORBITS - and so does right-drag with a
			# modifier held, because a laptop trackpad has no middle button and a
			# camera control nobody can reach is not a camera control. The choice
			# is stated in the screen's help line rather than left to be found.
			if button.pressed:
				_dragging = true
				_orbiting = (button.button_index == MOUSE_BUTTON_MIDDLE
					or button.shift_pressed or button.alt_pressed)
				if button.button_index == MOUSE_BUTTON_RIGHT and not _orbiting:
					_right_press_position = button.position
					_right_drag_distance = 0.0
			else:
				var was_plain_right := (button.button_index == MOUSE_BUTTON_RIGHT
					and not _orbiting)
				_dragging = false
				_orbiting = false
				if was_plain_right \
						and _right_drag_distance <= RIGHT_CLICK_DRAG_THRESHOLD \
						and button.position.distance_to(_right_press_position) \
							<= RIGHT_CLICK_DRAG_THRESHOLD:
					var ordered_region := region_at(button.position)
					if not ordered_region.is_empty():
						region_commanded.emit(ordered_region)
		MOUSE_BUTTON_LEFT:
			if not button.pressed:
				return
			# THE BUILD RING WINS OVER EVERYTHING. It is a menu the player opened,
			# standing over the map; a click that lands on one of its icons is
			# aimed at that icon and at nothing behind it.
			var entry := build_entry_at(button.position)
			if not entry.is_empty():
				build_entry_clicked.emit(
					String(selected_plot.get("region", "")),
					int(selected_plot.get("index", -1)), String(entry["id"]))
				return
			# Retail selects the army marker itself, not merely the province under
			# it.  Marker hit testing wins over plot/region selection because the
			# portrait is visibly on top of both.
			var army := army_at(button.position)
			if not army.is_empty():
				army_clicked.emit(int(army["army_id"]), String(army["region"]))
				return
			# A PLOT WINS OVER ITS REGION. Plots are only drawn for the region
			# already selected or hovered, so a click that lands on one is
			# unambiguously aimed at it - and the region under it is already the
			# selection, so nothing is reachable only through the region.
			var plot := plot_at(button.position)
			if not plot.is_empty():
				plot_clicked.emit(String(plot["region"]), int(plot["index"]))
				return
			var region_id := region_at(button.position)
			if not region_id.is_empty():
				region_clicked.emit(region_id)


## The visible army marker under `point`, or `{}`.  Highest army id wins only as
## a deterministic proxy for painter order when two medallions overlap.
func army_at(point: Vector2) -> Dictionary:
	var ids: Array[int] = []
	for key in _army_hit_boxes.keys():
		ids.append(int(key))
	ids.sort()
	ids.reverse()
	for army_id in ids:
		var row := _army_hit_boxes[army_id] as Dictionary
		if (row.get("box", Rect2()) as Rect2).has_point(point):
			return {"army_id": army_id, "region": String(row.get("region", ""))}
	return {}


## Whether two `{region, index}` plot references are the same plot. An empty
## dictionary is a legitimate value - the pointer is over no plot - and two
## empties are the same, so this is not `==` on dictionaries.
static func _same_plot(left: Dictionary, right: Dictionary) -> bool:
	return (String(left.get("region", "")) == String(right.get("region", ""))
		and int(left.get("index", -1)) == int(right.get("index", -1)))


## Set the plot the pointer is over directly, as `{region, index}` or `{}`.
## PRESENTATION ONLY, and public for the same reason `focus_region` is: a test or
## a capture can put the pointer on a plot without synthesising motion events
## through a control that has to be in a tree to receive them.
func hover_plot_at(region_id: String, index: int) -> void:
	var wanted: Dictionary = {}
	if not region_id.is_empty() and index >= 0:
		wanted = {"region": region_id, "index": index}
	if _same_plot(wanted, hover_plot):
		return
	hover_plot = wanted
	_rebuild_markers()
	_redraw()


## Light a build-ring slot directly, by entry id, or "" for none. PRESENTATION
## ONLY, and public for the same reason `hover_plot_at` is: a test or a capture
## can put the pointer on a slot without synthesising motion events through a
## control that has to be in a tree to receive them.
func hover_build_entry_at(building_id: String) -> void:
	if building_id == hover_build_entry:
		return
	hover_build_entry = building_id
	_redraw()


## THE BUILD-RING ENTRY UNDER A POINT, as `{id, index, box, entry}`, or `{}`.
##
## The ring's slots are the topmost thing on this screen when it is open - it is a
## menu standing over the map - so this is asked BEFORE the plot and before the
## region, and a hit consumes the event. Deterministic: slots are tested in
## `radial_slots()`'s own order, which is retail's reading order (straight up,
## then clockwise), and the FIRST containing box wins rather than the nearest, so
## a ring whose slots were ever authored to overlap still resolves the same way
## twice.
func build_entry_at(point: Vector2) -> Dictionary:
	var slots := radial_slots()
	for index in slots.size():
		var slot := slots[index] as Dictionary
		if not (slot["box"] as Rect2).has_point(point):
			continue
		var entry := slot["entry"] as Dictionary
		return {
			"id": String(entry.get("id", "")),
			"index": index,
			"box": slot["box"] as Rect2,
			"entry": entry,
		}
	return {}


## The build plot under a point, as `{region, index}`, or `{}`. Deterministic:
## regions are tried in sorted order and plots in their authored order, so a
## click never depends on dictionary iteration order.
## ONLY THE FOCUS REGIONS' PLOTS ARE PICKABLE, and that is not a limitation, it
## is what keeps a plot from stealing a click meant for a province. Every plot on
## the map is DRAWN now (see `plot_regions`), and `_gui_input` gives a plot
## priority over the region under it - which was safe while plots only existed on
## the province already selected or hovered, and would otherwise mean that
## clicking anywhere in Rohan picked whichever of the 98 plots on Middle-earth
## happened to be nearest on screen. A plot becomes pickable exactly when the
## player is already acting on its province, which is when he can do anything
## with it.
func plot_at(point: Vector2) -> Dictionary:
	var best: Dictionary = {}
	var best_distance := PLOT_RADIUS * _view_scale() + PLOT_PICK_SLOP
	var region_ids: Array[String] = []
	for key in plot_focus_regions():
		if _plot_screen_positions.has(key):
			region_ids.append(String(key))
	region_ids.sort()
	for region_id in region_ids:
		var points: Array = _plot_screen_positions[region_id] as Array
		for index in range(points.size()):
			var distance := (point - (points[index] as Vector2)).length()
			if distance <= best_distance:
				best = {"region": region_id, "index": index}
				best_distance = distance
	return best


## THE POINTER'S RAY THROUGH A PANEL PIXEL, as `{origin, direction}` in the
## world's own space, or `{}`.
##
## The same construction `_ground_point_at` makes and for the same reason - built
## from the camera's own transform rather than from `Camera3D.project_ray_*`,
## because the runners drive this view outside a scene tree and those helpers
## refuse there. Under a parallel projection every pixel's ray runs along the view
## direction and only the ORIGIN moves across the camera's near plane, which is
## what the two basis terms are.
func _pointer_ray(pixel: Vector2) -> Dictionary:
	if camera == null or viewport == null:
		return {}
	var panel := Vector2(viewport.size)
	if panel.x < 1.0 or panel.y < 1.0:
		return {}
	var transform: Transform3D = camera.transform
	var half_vertical := maxf(_camera_distance * _zoom, 0.0001) * 0.5
	var half_horizontal := half_vertical * (panel.x / panel.y)
	return {
		"origin": transform.origin \
			+ transform.basis.x * ((pixel.x / panel.x * 2.0 - 1.0) * half_horizontal) \
			+ transform.basis.y * ((1.0 - pixel.y / panel.y * 2.0) * half_vertical),
		"direction": -transform.basis.z,
	}


## THE REGION UNDER A POINT IN THIS CONTROL'S SPACE, or "".
##
## PICKING IS BY THE REGION'S OWN POLYGON. The owner's words: "when I hover over
## an area it should light up and allow me to select the ENTIRE area instead of
## just the node point that has a lower selection." What this used to do was
## exactly the defect he describes - it measured the distance to the region's
## MARKER and answered only within `MARKER_RADIUS + PICK_SLOP`, sixteen pixels,
## so all but a few hundred pixels of a province were dead to the pointer and the
## hover glow could never cover the area because nothing knew the area's shape.
##
## The shape is `LMR_Fill`, retail's own ownership footprint, and the pointer's
## ray is now intersected with retail's own triangles at retail's own heights -
## see `wotr_region_geometry.region_at_ray`. Anywhere inside the province picks
## it; the ocean, the impassable masses and the void beyond the slab pick nothing,
## which is what "" has always meant to the callers.
##
## WHAT IT COSTS. Nothing per frame: this runs on pointer MOTION and on clicks,
## never from `_process`, and one query walks a few tens of grid cells.
##
## THE MARKER FALLBACK SURVIVES FOR EXACTLY ONE CASE and says so: an install with
## no region-geometry bundle draws no territories at all (see
## `wotr_region_geometry.describe_search_failure`), the map is a field of marker
## discs, and picking those discs is then the honest behaviour rather than a
## substitute for a polygon test that has no polygons to run on.
func region_at(point: Vector2) -> String:
	if region_geometry != null and region_geometry.loaded:
		var ray := _pointer_ray(point)
		if ray.is_empty():
			return ""
		return region_geometry.region_at_ray(
			ray["origin"] as Vector3, ray["direction"] as Vector3)
	if _screen_positions.is_empty():
		_project_positions()
	var best := ""
	var best_distance := MARKER_RADIUS + PICK_SLOP
	var ids: Array[String] = []
	for key in _screen_positions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var distance := (point - (_screen_positions[region_id] as Vector2)).length()
		if distance <= best_distance:
			best = region_id
			best_distance = distance
	return best


func _set_zoom(value: float, preserve_ease := false) -> void:
	# The REQUEST is what a wheel notch writes; `_clamp_zoom()` below decides what
	# the camera actually does with it against the live ceiling.
	_zoom_request = clampf(value, MIN_ZOOM, MAX_ZOOM)
	# AN EXPLICIT ZOOM LANDS NOW AND CANCELS THE WHEEL'S EASE. See
	# `ZOOM_EASE_SECONDS`: a caller that asks for a framing gets that framing on
	# this frame, and an ease still in flight would otherwise drag it away again.
	# `drive_zoom` is the ONE caller that is the ease, and it passes `true`.
	if not preserve_ease:
		_zoom_goal = _zoom_request
		_zoom_easing = false
	# A zoom changes how far the target may be dragged from the map, because the
	# clamp is expressed in map units and the useful slack shrinks as the camera
	# closes in. Re-clamping here means zooming out can never leave the target
	# somewhere a pan could not have put it.
	_clamp_camera_target()
	# BEFORE the markers are restood, because their magnification is a function
	# of the zoom and the zoom is not settled until the ceiling has had its say.
	_clamp_zoom()
	# The markers' magnification is a function of the zoom, so they are restood
	# when it moves. Rebuilding rather than rescaling in place keeps ONE place
	# where a marker's transform is composed from retail's own numbers.
	_rebuild_markers()
	_apply_camera()
	_redraw()


## Orbit the camera. Yaw is a full circle; pitch is bounded so the camera cannot
## pass through the ground or over the top of it.
func _orbit(relative: Vector2) -> void:
	_yaw = wrapf(_yaw - relative.x * YAW_PER_PIXEL, -PI, PI)
	_pitch_degrees = clampf(
		_pitch_degrees + relative.y * PITCH_PER_PIXEL,
		MIN_PITCH_DEGREES, MAX_PITCH_DEGREES)
	# The fit depends on the pitch - a flatter angle needs the camera further
	# back for the same map - so the distance is re-fitted as the angle moves.
	# Still only `_camera_distance`; the player's zoom multiplies it untouched.
	_fit_distance()
	_clamp_zoom()
	_apply_camera()
	_redraw()


## Keep the camera target inside the map's own footprint plus a margin. Without
## this a single long drag put Middle-earth off the panel entirely and the only
## way back was `reset_camera()`.
func _clamp_camera_target() -> void:
	if not has_map():
		return
	# THE PAN IS BOUNDED BY THE PLAYABLE BOARD, NOT BY THE SLAB. Round 7 bounded it
	# by `terrain_extent` plus a quarter of the slab's span, which let the player
	# drag the camera 1,500 units out into painted seabed nothing is placed on and
	# then wonder where Middle-earth went. The framing box plus the same quarter
	# still puts any corner province in the middle of the panel - that is what
	# `PAN_MARGIN_FRACTION` is for - and it stops well short of open ocean.
	var box: Dictionary = _framing_box()
	var low := box["low"] as Vector3
	var high := box["high"] as Vector3
	var margin_x := (high.x - low.x) * PAN_MARGIN_FRACTION
	var margin_z := (high.z - low.z) * PAN_MARGIN_FRACTION
	_camera_target.x = clampf(_camera_target.x, low.x - margin_x, high.x + margin_x)
	_camera_target.z = clampf(_camera_target.z, low.z - margin_z, high.z + margin_z)


## Drag the map under the pointer. `relative` is the mouse's own pixel motion.
##
## THE OLD 0.0016 IS GONE AND WITH IT THE ONE THING A RIGHT-DRAG GOT WRONG: the
## ground did not follow the pointer. It was a fixed fraction of the fit scalar
## per pixel, which is only correct at one panel height and one pitch, so the map
## slid faster or slower than the hand at every other framing. Under a parallel
## projection the exact conversion is available in closed form and costs nothing,
## so the ground the drag started on now stays under the cursor.
##
## ONE PIXEL UP THE PANEL IS `camera.size / panel_height` WORLD UNITS of screen
## travel, on both axes - the horizontal follows because the panel's world width
## is `size * aspect` and its pixel width is `height * aspect`. Across the screen
## that is the whole answer, because the camera's own `x` axis lies in the ground
## plane. Up the screen it is not: the ground is pitched away, so a world unit of
## GROUND travel towards the camera only climbs `sin(pitch)` of a world unit up
## the picture, and the ground move has to be divided by it. That factor is why a
## drag felt right at the opening pitch and wrong at a low oblique.
func _pan(relative: Vector2) -> void:
	if not has_map() or viewport == null:
		return
	var per_pixel := _world_units_per_pixel()
	var achieved := _pan_ground(-relative.x * per_pixel,
		relative.y * per_pixel / _ground_foreshortening())
	# THE WALL LEANS FOR A DRAG TOO. See `drive_camera`: a hand dragging a map that
	# will not move is the same defect as a held key that will not move it, and the
	# allowance is per event rather than per second because a drag's pace is the
	# hand's.
	if achieved < PAN_WALL_BLOCKED_FRACTION:
		_yield_zoom_to_the_wall(PAN_WALL_ZOOM_YIELD_PER_DRAG)


## How many world units one pixel of panel spans, at the live zoom. Exact under a
## parallel projection: the panel's world height IS `camera.size`.
func _world_units_per_pixel() -> float:
	var panel_height := maxf(float(viewport.size.y), 1.0)
	return maxf(_camera_distance * _zoom, 0.0001) / panel_height


## How much of a world unit of GROUND travel towards or away from the camera is
## visible as travel up the picture, i.e. `sin` of the angle the camera looks down
## at. Never zero: `MAX_PITCH_DEGREES` holds the camera at least 8 degrees off the
## horizontal, so the reciprocal below is bounded at ~7.2 and a shallow camera
## covers more ground per pixel rather than an infinite amount of it.
func _ground_foreshortening() -> float:
	return maxf(absf(sin(deg_to_rad(_pitch_degrees))), 0.0001)


## Move the point the camera looks at, in WORLD UNITS, across the picture and
## along the ground away from the camera. The one place a pan is actually
## applied: the right-drag, the held keys and the screen edge all resolve to a
## pair of world distances and come through here, so there is exactly one
## definition of what panning does to the camera and to the clamps.
## Returns the FRACTION of the requested step the camera actually took, 0..1, so
## the caller can tell "I was stopped by the cut-edge wall" from "I moved".
func _pan_ground(across_units: float, forward_units: float) -> float:
	if not has_map() or camera == null:
		return 1.0
	if is_zero_approx(across_units) and is_zero_approx(forward_units):
		return 1.0
	# Local basis, not global: the camera's parent viewport sits at the origin,
	# and `global_transform` would require the view to be inside a tree.
	var right := camera.transform.basis.x
	var forward := -camera.transform.basis.z
	forward.y = 0.0
	if forward.length() > 0.0001:
		forward = forward.normalized()
	right.y = 0.0
	if right.length() > 0.0001:
		right = right.normalized()
	var start := _camera_target
	var step := right * across_units + forward * forward_units
	_camera_target = start + step
	_clamp_camera_target()
	# THE PAN STOPS AT THE WALL INSTEAD OF DIVING THROUGH IT - the round-7 defect
	# that only a photograph found. Read this before removing the search.
	#
	# `zoom_ceiling()` is solved against the LIVE PAN, so moving the camera towards
	# a corner of the slab LOWERS it. With the view opening at the ceiling (which
	# is what "zoom all the way out" means, see `DEFAULT_ZOOM`), the first tap of
	# `D` therefore did two things at once: it moved the camera east AND it drove
	# `_zoom` down to the new, smaller ceiling. Photographed, one second of held
	# `D` from the opening framing left the camera looking at a single province -
	# the map appeared to dive at the player for pressing a pan key.
	#
	# THE ZOOM WAS NOT WRONG; THE PAN WAS. Diving is exactly what the cut-edge rule
	# demands of a camera that has been moved somewhere the rule cannot frame from,
	# so the fix is to not move it there: a pan may never cost the player zoom. The
	# largest fraction of the requested step that leaves `_zoom` where it was is
	# found by bisection - monotone, because sliding further towards the rim can
	# only lower the ceiling - so the camera slides up to the wall and stops
	# against it, which is what every strategic map does at full pull-back.
	#
	# IT ONLY EVER BINDS AT THE CEILING. A player who has zoomed in at all has a
	# `_zoom` well under it, the whole step keeps it there, and the search does not
	# run. That is the common case and it costs one comparison.
	var taken := 1.0
	if _pan_would_cost_zoom():
		var low := 0.0
		var high := 1.0
		for _attempt in PAN_WALL_STEPS:
			var middle := (low + high) * 0.5
			_camera_target = start + step * middle
			_clamp_camera_target()
			if _pan_would_cost_zoom():
				high = middle
			else:
				low = middle
		_camera_target = start + step * low
		_clamp_camera_target()
		taken = low
	_clamp_zoom()
	_apply_camera()
	_redraw()
	return taken


## WOULD A PAN TO WHERE THE CAMERA NOW STANDS COST THE PLAYER ZOOM? Exactly the
## question `_resolved_zoom() < _zoom` used to ask, answered in TWO evaluations of
## the cut-edge predicate instead of a twenty-step bisection over it.
##
## WHY THE TWO ARE THE SAME QUESTION. `_zoom` is `clamp(_zoom_request, MIN,
## ceiling)`, so `_zoom <= _zoom_request` always; the resolved zoom is therefore
## below the live one exactly when the CEILING is. And `zoom_ceiling()` is by
## construction the largest zoom at which the slab's cut rim is out of frame, over
## a predicate that is MONOTONE in the zoom - a wider picture can only reveal more
## rim, never less - so "the ceiling is below `_zoom`" is precisely "the rim is in
## frame AT `_zoom`". The two escapes `zoom_ceiling()` makes are the two clauses
## below: no rim at full pull-back means the ceiling is `MAX_ZOOM` and cannot be
## under anything, and rim at every zoom (the low oblique) means it declines to
## clamp at all.
##
## WHAT IT SAVES, and it is why the smoothing above is affordable. Measured at
## 2560x1440: one `zoom_ceiling()` is 0.40 ms, and a panning frame ran EIGHT of
## them - one per bisection step plus the resolve plus `_clamp_zoom` - for about
## 3.2 ms of a 12 ms pan budget spent re-deriving one boolean. One predicate call
## is ~0.018 ms, so the six-step search now costs ~0.22 ms and the frame's only
## remaining full solve is `_clamp_zoom`'s single one.
func _pan_would_cost_zoom() -> bool:
	if not slab_cut_edge_is_in_frame(_zoom):
		return false
	if slab_cut_edge_is_in_frame(MIN_ZOOM):
		return false
	return true


## Reset the camera to the framing the view opens with. Presentation only.
func reset_camera() -> void:
	_zoom_request = DEFAULT_ZOOM
	_zoom_goal = DEFAULT_ZOOM
	_zoom_easing = false
	_pan_velocity = Vector2.ZERO
	# The lean is a lease on the framing and a reset ends it.
	_wall_yield = 1.0
	_yaw = 0.0
	_pitch_degrees = DEFAULT_PITCH_DEGREES
	_frame_camera()
	_rebuild_markers()
	_redraw()


## Point the camera at one region at a given zoom, or at the whole map when
## `region_id` is empty. PRESENTATION ONLY - the same field a drag writes.
## Public so a capture or a test can put the camera somewhere reproducible
## instead of synthesising drag events.
func focus_region(region_id: String, zoom: float) -> void:
	# An explicit framing request ends any lean the pan had taken; otherwise
	# "put the camera at zoom 0.3" would quietly deliver 0.6 x 0.3.
	_wall_yield = 1.0
	if not region_id.is_empty() and _world_positions.has(region_id):
		_camera_target = _world_positions[region_id] as Vector3
	elif region_id.is_empty():
		_frame_camera()
	_zoom_request = clampf(zoom, MIN_ZOOM, MAX_ZOOM)
	_zoom_goal = _zoom_request
	_zoom_easing = false
	_clamp_camera_target()
	_clamp_zoom()
	_rebuild_markers()
	_apply_camera()
	_redraw()


## Set the orbit directly, in radians of yaw and degrees of pitch, clamped to the
## same range a drag is. Also presentation only.
func set_orbit(yaw: float, pitch_degrees: float) -> void:
	_yaw = wrapf(yaw, -PI, PI)
	_pitch_degrees = clampf(pitch_degrees, MIN_PITCH_DEGREES, MAX_PITCH_DEGREES)
	_fit_distance()
	_clamp_zoom()
	_apply_camera()
	_redraw()


## The camera's current framing, for the screen's help line and for a test that
## needs to assert the framing survived a resize. Read-only.
func camera_state() -> Dictionary:
	return {
		"zoom": _zoom,
		# WHAT THE PLAYER ASKED FOR, before the ceiling had its say. Equal to
		# `zoom` whenever the ceiling is not cutting the request short, and above
		# it when it is - so "is this framing the player's or the panel's?" is a
		# question a test can actually ask. See `_zoom_request`.
		"requested": _zoom_request,
		# THE COMPUTED PULL-BACK LIMIT at this panel shape, pitch, yaw and pan.
		# Reported beside the zoom because "the zoom is 0.43" says nothing on its
		# own: 0.43 is the whole board on one panel and half of it on another,
		# and this is the number that says which.
		"ceiling": _zoom_ceiling,
		"yaw": _yaw,
		"pitch": _pitch_degrees,
		"target": _camera_target,
		"distance": _camera_distance,
		# Part of the FIT, like the distance: a resize may move it, and a resize
		# moving it is not the same thing as a resize discarding the player's
		# framing. Reported so a test can tell the two apart.
		"centring": _framing_offset,
	}
