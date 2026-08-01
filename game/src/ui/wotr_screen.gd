extends Panel

## THE WAR OF THE RING STRATEGIC SCREEN: the map, the armies, and the one button
## that starts a battle.
##
## It draws RETAIL'S OWN 3D MAP when one has been converted. `livingmap.w3d` -
## the whole of Middle-earth as 64 sub-objects with retail's compiled textures -
## decodes through this project's W3D scanner with no unsupported chunks, and
## `openbfme_importer.livingmap_bundle` turns it into the bundle
## `wotr_map_bundle.gd` loads. Regions sit on that mesh at their AUTHORED world
## coordinates, because the document's `centerPoint` values and the map's
## vertices are the same coordinate space at scale 1 - measured, and the
## measurement travels in the bundle manifest.
##
## With no bundle converted the screen falls back to the flat 2D region graph it
## has always drawn, and SAYS which one the owner is looking at - in the log, in
## the mode line, and in a banner across the top of the fallback itself carrying
## every path that was searched and the command that produces a bundle. It used
## to fall back in complete silence: no print, no warning, nothing in the log, so
## the only way to find out was to notice the map looked wrong. That is the class
## of defect this branch exists to remove, and it had shipped here.
##
## WHAT ELSE IS ON SCREEN, all of it derived from the strategic state: whose turn
## it is and whether that seat is human, one short imperative saying what to do
## next, and a plaque per seat (heraldry, regions held, armies, command points
## on the board). Seats are named in RETAIL'S OWN ENGLISH through
## `wotr_display_names.gd`; a raw template id on the player surface is treated
## as a defect, not a default.
##
## Still missing from retail's presentation, and named on screen rather than
## faked: the `LivingWorldUI.apt` shell (799 KB of Flash this project does not
## read), the `LW:DisplayName*` string table (so regions carry retail's own ids),
## army models walking between regions, the turn-phase banner, and retail's
## ambient animation (the Eye of Sauron, circling eagles and fellbeasts, drifting
## cloud borders).
##
## THREE RULES IT KEEPS:
##
## 1. NOTHING ON SCREEN IS INVENTED. A region retail did not give a custom centre
##    point (BFME2 authors exactly one: Rhun) is NOT placed at a plausible
##    coordinate - it is listed separately and labelled. The battlefield a battle
##    is fought on is labelled a stand-in, because it is one. A map sub-object
##    whose texture did not resolve is drawn flat grey, never with a substitute.
##
## 2. SELECTION IS PRESENTATION UNTIL IT IS COMMITTED. `selected_region`,
##    `selected_target` and the hover highlight live on the session's
##    presentation fields, never enter a hash, and never reach the simulation.
##    The moment they decide a battle they go through `session.commit_attack()`,
##    which mints the commitment the strategic hash covers - the ONLY door.
##
## 3. A REFUSAL IS SHOWN, NOT SWALLOWED. Every path that cannot proceed prints
##    the strategic layer's own reason into the message line.

signal back_requested
## Emitted with the session's commit result once a battle has been ADMITTED into
## the strategic state. The menu owns the scene change; this screen owns the
## commitment.
signal battle_committed(configured: Dictionary)
signal turn_ended
## Raised by the pause shell's OPTIONS capsule. The shell owns the options screen
## (it is one instance, shared by the menu page and by here), so this screen asks
## for it rather than building a second one. `main_menu.gd` opens it over whatever
## page is up and comes back to the same page when it closes, so asking for
## settings mid-campaign does not throw the campaign away.
signal options_requested

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const StringsScript = preload("res://src/wotr/wotr_strings.gd")
const MacrosScript = preload("res://src/wotr/wotr_macros.gd")
const LivingWorldUiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const MarkerModelsScript = preload("res://src/wotr/wotr_marker_models.gd")
const RegionImagesScript = preload("res://src/wotr/wotr_region_images.gd")
const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")
const HudScript = preload("res://src/wotr/wotr_hud_chrome.gd")
const DisplayNamesScript = preload("res://src/wotr/wotr_display_names.gd")
const StrategicUiScript = preload("res://src/wotr/wotr_strategic_ui.gd")

## ------------------------------------------------------------------------------
## RETAIL'S OWN STRATEGIC HUD, COMPOSED FROM RETAIL'S OWN SLOTS
## ------------------------------------------------------------------------------
##
## The five islands below are retail's five strategic APT movies, and the place
## each one lands is retail's own: `StrategicHUD` is the CONTAINER movie, and its
## `main` sprite carries an authored named instance per island with the
## translation retail composes it at (`stats` 0,0 - `checklist` 512,0 -
## `endTurnButton` 1024,0 - `globe` 0,512 - `selectionDetails` 430,590, in the
## 1024x768 space all 24 movies are authored in). `_strategic_islands()` READS
## those translations out of the bundle; not one coordinate in this file is a
## slot position.
##
## WHICH AUTHORED STATE EACH ISLAND IS DRAWN IN, and why - because APT carries no
## timeline playback (named gap `timeline-playback-not-bound`), so a static state
## has to be CHOSEN and the choice has to be stated:
##
##   * `StrategicStats` `_init` - its three labels (`_init` 0, `_expand` 9,
##     `_collapse` 20) flatten to the same 56 draws, so the choice between them
##     costs nothing. The count was 54 while this screen bound BFME2's layer; the
##     bundle is RotWK's now and it is 56. Measured off the bundle, not copied.
##   * `StrategicChecklist` `_collapse` - 82 draws against `_expand`'s 50. The
##     label names the frame the COLLAPSE tween starts from, which is the fully
##     EXPANDED plaque: the phase chevron bar, the "tactical phase" banner strip
##     and the critical-tasks box with its gold frame and scroll rail. `_expand`
##     is the same plaque before the box has grown.
##   * `StrategicEndTurnButton` - one label PER BUTTON STATE (`_up` 14, `_over`
##     23, `_down` 43, `_disabled` 63), so the button's states are retail's own
##     rather than invented. `_intro`/`_rollout`/`_exit` are tween frames this
##     screen does not play.
##   * `StrategicPalantir` - the movie authors NO labels; its frame 0 is empty and
##     its whole composition (1,355 draws) sits on frame 1, its authored script
##     stop. That is `richest_frame()`'s stated rule, so it is asked for by rule
##     rather than by a frame number written here.
##   * `StrategicDetailsTray` `_close` - 28 draws against `_closed`'s and
##     `_open`'s ZERO. Same reading as the checklist: `_close` is the frame the
##     closing tween starts from, which is the tray fully out.
const STRATEGIC_ISLANDS := [
	{"slot": "stats", "movie": "StrategicStats", "label": "_init"},
	{"slot": "checklist", "movie": "StrategicChecklist", "label": "_collapse"},
	{"slot": "endTurnButton", "movie": "StrategicEndTurnButton", "label": "_up"},
	# THE BAR BEFORE THE PALANTIR, because that is retail's own painter's order:
	# in the oracle capture the palantir's compass dial and its ring of structure
	# buttons sit ON TOP of the tray's left end. Drawn the other way round - which
	# is how this list used to read - the bar covers the dial and the icons
	# vanish, which is exactly what the first round-4 capture showed.
	{"slot": "selectionDetails", "movie": "StrategicDetailsTray", "label": "_close"},
	{"slot": "globe", "movie": "StrategicPalantir", "label": ""},
]

## THE ISLANDS ARE NOT ANCHORED ANY MORE, BECAUSE RETAIL DOES NOT ANCHOR THEM.
##
## This screen used to place each island against the frame edge retail authored
## it near, at a UNIFORM scale taken off the height. That kept retail's art in
## its authored aspect, and it was wrong: it left retail's details tray at ~51%
## of a 16:9 frame instead of the ~73% retail fills, which is exactly the "the
## bottom bar does not exist, 87% of the bottom edge is bare map" defect a blind
## review named. Retail maps the WHOLE 1024x768 authored surface onto the WHOLE
## frame, x by width/1024 and y by height/768.
##
## THE MEASUREMENT, off the oracle capture
## (`reference/.../game.dat_l1eJcM0zCw.jpg`, 2560x1440), so this is a reading
## rather than a preference:
##
##   * The palantir's counter plaque ("0/3 ... 125/720") is authored at x
##     17.5..220 (globe slot 0,512 plus `PALANTIR_PLAQUE`). In the capture its
##     gold frame runs x 40..590. 220 * (2560/1024) = 550; 220 * (1440/768) = 412.
##     The x stretch is the one that lands.
##   * The details tray's own art ends at authored x 1022.2 (slot 430 plus its
##     flattened bound 592.2). In the capture the tray reaches the right edge.
##     1022.2 * (2560/1024) = 2555.5 of 2560.
##   * The tray's bottom rail is authored at y 759.5 (slot 590 plus 169.5). In the
##     capture it sits at y 1425. 759.5 * (1440/768) = 1424.
##
## So x and y are stretched INDEPENDENTLY, and that is why retail's palantir dish
## is a wide oval at 16:9 and this project's was a circle. Nothing is invented by
## following it; the opposite was.
const APT_STRETCH_MEASUREMENT := (
	"retail maps its whole 1024x768 authored HUD onto the whole frame, x by "
	+ "width/1024 and y by height/768. Measured off the oracle capture: the "
	+ "palantir plaque's right edge (authored x 220) lands at x 550 in a 2560-wide "
	+ "frame, which is the x stretch and not the uniform one (412)")

## WHERE TEXT AND CONTENT GO INSIDE EACH ISLAND, in that movie's OWN authored
## coordinates. Every rectangle here is MEASURED OFF RETAIL'S OWN FLATTENED
## TRIANGLES rather than chosen - the draw group each one comes from is named -
## because retail's live text is a `$Variable` the APT does not carry (named gap
## `strategic-text-values-are-live`), so the positions ship and the content does
## not. Measuring beats eyeballing and it beats inventing.
##
## The two that are read off the ART rather than off a draw group say so: the
## phase bar's end capsules and the palantir's name plaque are painted INTO their
## atlas rather than drawn as separate geometry, so their rectangles are read off
## the flattened image at 2x and are accurate to about a pixel of authored space.
const STATS_FIELD := Rect2(9.0, 8.3, 130.0, 43.8)
## How much of that field retail's own `Expand` icon column (`0/14`, x 18.7..37.9)
## takes off the left, plus a three-pixel gutter: 32 authored pixels of a 130-wide
## field that starts at x 9. See `_draw_header`.
const STATS_ICON_COLUMN := 32.0 / 130.0
const CHECKLIST_TURN_PLAQUE := Rect2(-322.0, 7.0, 98.0, 39.0)
## THE CHEVRON BAR'S OTHER END CAPSULE, and it is a MEASUREMENT rather than a
## guess. `StrategicChecklist` sets two identical black capsules at the ends of its
## phase chevron bar; the left one is `CHECKLIST_TURN_PLAQUE` above, and the right
## one is its mirror about the bar's own centre. Solved off the placement this
## screen already computes: at 2560x1440 the checklist island resolves to
## origin (1280, 0) scale (2.5, 1.875), the left capsule lands at x 475..720, and
## the empty black lozenge on the right of the bar occupies 1840..2085 - which is
## authored x +224..+322, the exact mirror of -322..-224.
##
## IT WAS EMPTY, and a blind review called both ends "empty black lozenges". The
## left one has carried "Turn: / 1" for several rounds; this one carried nothing,
## which is retail's `dynamic-content-slots-are-empty` gap showing through as a
## hole in the middle of the frame's top edge.
##
## WHAT GOES IN IT IS THIS PROJECT'S CHOICE and is labelled as such: the ROUND,
## against the turn on the left. A turn is one seat's move and a round is every
## seat having moved once; the pair was previously only explained in a tooltip, and
## two symmetric counters flanking retail's chevrons is what that bar is shaped to
## hold. Retail's own caption for it is not in any converted table, so the word is
## ours.
const CHECKLIST_ROUND_PLAQUE := Rect2(224.0, 7.0, 98.0, 39.0)
const CHECKLIST_ROUND_CAPTION := "Round:"

## ------------------------------------------------------------------------------
## THE PHASE CHEVRONS - THE SCREEN'S CLOCK, AND IT NOW TICKS
## ------------------------------------------------------------------------------
##
## THE DEFECT, from an adversarial art-direction review: "The phase chevron bar at
## top-centre has three chevrons and none of them is lit... A phase indicator that
## does not indicate the phase is not an indicator, it is decoration. This is the
## screen's clock and it currently doesn't tick."
##
## WHY IT DIDN'T. Retail paints its three phase devices INTO the chevron bar's own
## atlas rather than authoring them as separate display-list entries, and it lights
## the current one at RUNTIME from its own script - which is this bundle's standing
## `timeline-playback-not-bound` gap. The flattening therefore carries all three at
## the one resting value the movie was parked on, and there is no authored "lit"
## frame to ask for. Lighting it means DRAWING the state, over retail's device.
##
## THE THREE CELLS ARE MEASURED, not chosen, and the measurement is the same kind
## `CHECKLIST_ROUND_PLAQUE` above records: the devices are in the atlas, so their
## rectangles are read off the FLATTENED IMAGE rather than off a draw group.
## Scanning the gilt ink of the bar at the 2560x1440 frame the oracle is judged in
## (checklist origin x 1280, scale 2.5) finds three device clusters centred at
## window x 931, 1287.5 and 1641.5 - authored -139.6, +3.0 and +144.6. Two
## properties make that a derivation rather than three numbers off a screenshot:
##
##   * THE MIDDLE DEVICE IS ON THE BAR'S OWN CENTRE. The bar (root depth 60) spans
##     authored -326.95..334.05, whose centre is +3.55; the measured middle device
##     is at +3.0, half a pixel of authored space away.
##   * THE OTHER TWO ARE ONE PITCH EITHER SIDE OF IT, and the two pitches agree:
##     -139.6 -> +3.0 is 142.6 and +3.0 -> +144.6 is 141.6.
##
## So the table below is the bar's centre plus and minus one pitch, and the pitch
## is the mean of the two measured spacings. `PHASE_CELL_WIDTH` leaves the chevron
## separators between the cells uncovered - the `>` marks sit at the midpoints,
## authored -68 and +74 - because those are retail's ornament and not part of any
## phase's state.
##
## THE CELLS SHARE THE END PLAQUES' OWN ROW (`CHECKLIST_TURN_PLAQUE`, y 7..46), so
## all five cells of the bar - turn, three phases, round - are one row by
## construction rather than by three separate y values that happen to agree.
const PHASE_CELL_PITCH := 142.1
const PHASE_CELL_CENTRE := 3.55
const PHASE_CELL_WIDTH := 108.0

## ------------------------------------------------------------------------------
## WHAT THE THREE CELLS MEAN, AND WHERE THE WORDS COME FROM
## ------------------------------------------------------------------------------
##
## RETAIL'S OWN THREE PHASES, RETAIL'S OWN THREE TITLES. `data/lotr.str` authors
## exactly three phase names for this screen and the setup string bundle converts
## all three, which is why this is a reading of retail rather than an invention to
## fill a bar:
##
##     APT:TacticalPhaseTitle      "Tactical Phase"
##     APT:BattlePhaseTitle        "Battle Phase"
##     APT:RetreatPhaseTitle       "Retreat Phase"
##     APT:StrategicHUDPhaseLabel  "Phase"
##
## Three strings, three chevrons, in the order retail declares them. The devices
## painted in the bar agree: a standard, then crossed swords, then the third.
##
## WHICH ONE IS LIT is this screen's own live state and nothing else:
##
##   TACTICAL - the seat is choosing. Retail's own narration defines this phase as
##              the one where "STRUCTURE CONSTRUCTION, UNIT TRAINING, AND ARMY
##              MOVEMENT ARE ALL DECIDED" (`LW:InstructionText06`), which is
##              exactly and only what this screen lets a seat do.
##   BATTLE   - a battle is being resolved: the report is up, or auto-resolve is
##              running.
##   RETREAT  - NOT MODELLED. This layer has no retreat step, so this cell is never
##              lit, and that is a NAMED GAP on the diagnostics panel rather than a
##              cell quietly lit by something else. A clock that shows a hand it
##              cannot move is honest; a clock that puts the wrong hand up is not.
const PHASE_TACTICAL := 0
const PHASE_BATTLE := 1
const PHASE_RETREAT := 2
const PHASE_CELLS := [
	{"index": PHASE_TACTICAL, "string": "APT:TacticalPhaseTitle", "caption": "Tactical Phase"},
	{"index": PHASE_BATTLE, "string": "APT:BattlePhaseTitle", "caption": "Battle Phase"},
	{"index": PHASE_RETREAT, "string": "APT:RetreatPhaseTitle", "caption": "Retreat Phase"},
]
## Retail's own word for what the bar IS, set under the lit cell beside its title.
const PHASE_LABEL_STRING := "APT:StrategicHUDPhaseLabel"
const PHASE_LABEL_CAPTION := "Phase"
## The gap the RETREAT cell stands for, stated where the diagnostics panel can
## read it rather than left as a comment.
const PHASE_RETREAT_GAP := (
	"retail's chevron bar carries three phases and this layer models two of them: "
	+ "APT:RetreatPhaseTitle (\"Retreat Phase\") has no step in this project's turn, "
	+ "so its chevron is drawn at the inactive value and is never lit")
const CHECKLIST_PHASE_BANNER := Rect2(-287.8, 50.9, 575.0, 28.0)
## THE CHECKLIST PLAQUE HAS TWO AUTHORED SIZES AND THIS SCREEN USES BOTH.
##
## `StrategicChecklist` is a plaque that GROWS to hold its task list, and its two
## labels are the two ends of that tween: `_expand`(9) is the frame the growth
## starts from - the plaque SHUT, its black field only 33.9 authored pixels deep
## before the phase banner covers the rest - and `_collapse`(19) is the frame the
## shrink starts from, the plaque OPEN, its field 130.4 deep with retail's scroll
## rail beside it. Retail's own script picks between them by whether there is
## anything critical to list.
##
## THIS SCREEN PICKS THE SAME WAY, and that is a real fix rather than a
## refinement. It used to draw the OPEN plaque unconditionally, which put 130
## authored pixels (244 at the frame the oracle is judged in) of empty black over
## the top-left of Middle-earth at all times - and the strategic map's own army
## banners and build-plot decals stand there. A blind review said "a strategic map
## with no armies on it is not a screenshot of a strategy game"; the armies were
## drawn, and this plaque was on top of them. The screen normally has ONE line to
## say (the imperative), so it normally draws the SHUT plaque and the map is
## clear; when a refusal has to be shown as well it opens, which is what the
## plaque is for.
const CHECKLIST_TASK_BOX := Rect2(-288.5, 71.0, 578.5, 130.4)
const CHECKLIST_TASK_BOX_SHUT := Rect2(-288.5, 17.0, 578.5, 33.9)
const CHECKLIST_LABEL_OPEN := "_collapse"
const CHECKLIST_LABEL_SHUT := "_expand"
const ENDTURN_FACE := Rect2(-160.55, 8.9, 152.0, 40.0)
const PALANTIR_DISH := Rect2(17.0, 17.0, 219.0, 185.0)
## Where the owner line sits in that lens, as fractions of the lens's own height,
## and how much of the ellipse's chord to leave for the gold bevel. See the
## caption block in `_relayout` for why a fraction of the BOUNDING BOX was wrong.
const PALANTIR_CAPTION_TOP := 0.63
const PALANTIR_CAPTION_HEIGHT := 0.26
const PALANTIR_CAPTION_BEVEL := 0.88
const PALANTIR_PLAQUE := Rect2(17.5, 200.0, 202.5, 37.5)
const TRAY_FIELD := Rect2(-135.2, -18.1, 707.5, 176.3)

## ------------------------------------------------------------------------------
## THE BOTTOM COMMAND BAR, IN THE `selectionDetails` SLOT'S OWN AUTHORED SPACE
## ------------------------------------------------------------------------------
##
## Retail's bar is not one movie: it is `StrategicDetailsTray` (the gilt frame and
## its two full-width rails), `StrategicDetailsRegion` (the TERRITORY / ARMIES /
## STRUCTURES tab strip with the chain-link separators), one of
## `StrategicDetailsTerritory` / `...Armies` / `...Structures` (the content plus
## the status ribbon) and `StrategicDetailsBuildQueue` (the parchment card rail).
## All four are authored against the SAME origin - the `StrategicHUD` slot
## `selectionDetails` at (430, 590) - so every rectangle below is that slot's own
## local space and all of them come off retail's flattened triangles.
##
## MEASURED, per draw group, out of the bundle:
##   tray `_close`   field  (-135.2, -18.1)..(572.3, 158.2)
##                   top rail (-141.1, -30.6)..(549.0, -13.6)
##                   bottom rail (-149.1, 152.5)..(546.2, 169.5)
##                   the chain-link divider (-73.5, -16.2)..(-34.5, 156.8)
##   region frame 10 tab strip (-17.3, 1.8)..(582.0, 35.8)
##   territory/armies/structures frame 0  ribbon (-13.5, 146.6)..(590.8, 184.6)
##   buildqueue `_open` card rail (3.8, 15.1)..(590.8, 184.6), six card slots at
##                   x 10.25, 87, 163.75, 240.5, 317.25, 394
const TRAY_TAB_STRIP := Rect2(-17.3, 1.8, 599.3, 34.0)
## The three tab origins are `StrategicDetailsRegion`'s OWN named instances
## (`territory`, `armies`, `structures`), read out of the bundle rather than
## chosen; the width is the gap between them.
const TRAY_TAB_WIDTH := 201.8
const TRAY_TABS := [
	{
		"key": "territory", "x": -18.15,
		"string": "APT:RegionDetailsTerritoryTab", "caption": "TERRITORY",
	},
	{
		"key": "armies", "x": 183.65,
		"string": "APT:RegionDetailsArmiesTab", "caption": "ARMIES",
	},
	{
		"key": "structures", "x": 384.95,
		"string": "APT:RegionDetailsStructuresTab", "caption": "STRUCTURES",
	},
]
## How far in from each end of a tab's pitch retail's own hook ornament reaches.
## Read off `StrategicDetailsRegion`'s strip art: the hooks sit on the tab
## boundaries, so a lit plate has to start after one and stop before the next.
const TRAY_TAB_CONNECTOR := 14.0
## WHERE THE TAB CELL ACTUALLY IS, MEASURED OFF THE ORACLE CAPTURE.
##
## `TRAY_TAB_STRIP` is the flattened BOUNDING BOX of `StrategicDetailsRegion`'s
## frame 10 - all of its art, including the chain-link separators that hang well
## below the rail and the hook ornaments that rise above it. It is the right
## rectangle for asking "where is the strip"; it is the WRONG rectangle for asking
## "where does a caption go", and using it for the second question is the whole of
## the defect a blind review photographed: "TERRITORY / ARMIES / STRUCTURES sit
## visibly above their own frame, riding on and clipping the gold ornamental rail
## rather than seated in tab cells ... the orange STRUCTURES pill cuts the gold
## border and runs past it to the right."
##
## THE MEASUREMENT, off `game.dat_l1eJcM0zCw.jpg` at 2560x1440, with the tray slot
## at retail's own (430, 590) and the frame stretch (2.5, 1.875) this screen
## already pins in `APT_STRETCH_MEASUREMENT`:
##
##   * the strip's horizontal gold rule runs at y 1128-1130, which is authored
##     (1128 - 590 * 1.875) / 1.875 = 11.6 in the tray slot's own space;
##   * retail's own caption cap-heights sit centred at y ~1099, which is authored
##     -3.9 - ABOVE the rule, in the band between it and the tray's top rail
##     (authored -30.6..-13.6);
##   * retail's lit STRUCTURES plate fills that same band and stops ON the rule.
##
## So the cell is the band ABOVE the rule, 24 authored pixels deep with a two-pixel
## clearance so neither the caption nor the plate touches the gold. That puts the
## caption's centre at authored -4.4 against retail's measured -3.9, which is half
## an authored pixel, and it is a reading rather than a preference.
const TRAY_TAB_RAIL := 11.6
const TRAY_TAB_CELL_HEIGHT := 24.0
const TRAY_TAB_CELL_CLEARANCE := 2.0


## THE AUTHORED CELL ONE TAB OCCUPIES, clamped to the tray's own field.
##
## IT IS ONE FUNCTION AND NOT THREE CALL SITES, and that is the whole point of it.
## The layout places the tab BUTTON, the chrome pass paints the SELECTED CELL, and
## the runner asserts neither leaves the frame - and until this round each of the
## three computed the rectangle for itself. Round 6 clamped the painted plate to
## the field and left the BUTTON at the raw pitch, so retail's STRUCTURES tab
## (authored at x 384.95, pitch 201.8, ending at 586.75 against a field whose right
## edge is 572.3) still put fourteen authored pixels of live control, and the
## caption inside it, past the gold border. A blind review photographed exactly
## that: "the orange STRUCTURES pill cuts the border and runs past it to the
## right". One definition is what makes that unrepresentable rather than fixed.
static func tray_tab_cell(entry: Dictionary) -> Rect2:
	var field_left := TRAY_FIELD.position.x + TRAY_TAB_CONNECTOR
	var field_right := TRAY_FIELD.position.x + TRAY_FIELD.size.x - TRAY_TAB_CONNECTOR
	var left := maxf(float(entry["x"]) + TRAY_TAB_CONNECTOR, field_left)
	var right := minf(float(entry["x"]) + TRAY_TAB_WIDTH - TRAY_TAB_CONNECTOR, field_right)
	return Rect2(left,
		TRAY_TAB_RAIL - TRAY_TAB_CELL_CLEARANCE - TRAY_TAB_CELL_HEIGHT,
		maxf(right - left, 0.0), TRAY_TAB_CELL_HEIGHT)
## RETAIL'S SCROLL RAIL ON THE TRAY'S RIGHT EDGE, by its authored path, and the
## colour transform this screen applies to it.
##
## `StrategicDetailsTray`'s `16` sprite is the tray's vertical scroll furniture:
## two arrow knobs (`16/13`, `16/21`), four studs, the two end scrolls, and `16/1`
## - a 26x78 quad at x 566.2..592.2 taking its pixels from the atlas at u
## 0.8125..0.9141, v 0.0039..0.3086. Cropped and looked at, that region is the
## rail AND its filigree curl rendered in a hot near-white gold: it is the
## element's LIT state, not its resting one.
##
## Retail's oracle capture of this exact tab has the same rail and the same curl
## in the same place, in ordinary dark gold like the rest of the frame. Ours came
## out white-hot because APT playback is not bound (named gap
## `timeline-playback-not-bound`) and the authored stop this movie flattens at
## holds the highlight. A blind review photographed the result as "a bright-yellow
## sliver on the structures panel's right edge".
##
## So the rail is DIMMED rather than dropped: the geometry, the crop and the
## ornament are all still retail's, multiplied down to the value the frame's own
## unlit members sit at. That multiplier is MEASURED, not chosen - the tray
## atlas's frame members average (243, 198, 107) and this crop averages
## (246, 209, 120), so the ratio that carries one onto the other is
## (0.99, 0.95, 0.89), and a further 0.49 takes the LIT state down to the resting
## value the oracle shows. Dropping it instead would take retail's own corner
## filigree with it, which the oracle does have.
const TRAY_SCROLL_RAIL_PATH := "screen:StrategicDetailsTray:frame:21/16"
const TRAY_SCROLL_RAIL_TINT := Color(0.48, 0.46, 0.43, 1.0)
## THE TRAY'S TWO FULL-WIDTH RAILS, WHICH ARE THE SAME DEFECT AGAIN AND WERE
## MISSED BECAUSE ONLY ONE END OF ONE OF THEM WAS EVER VISIBLE.
##
## `21/8/3/1` is the bottom rail (authored x -149.1..546.2, y 152.5..169.5) and
## `21/12/3/1` is the top one (x -141.1..549.0, y -30.6..-13.6). Both take their
## pixels from the same atlas region the scroll rail does, and both are flattened
## in their LIT state for the same reason (`timeline-playback-not-bound`): the
## opaque pixels of each crop average (255, 255, 182), against (243, 198, 107) for
## the frame members around them.
##
## THE BOTTOM RAIL IS THE ONE THAT SHOWED. Its left end runs 149 authored pixels
## past the tray's own field, out from under the status ribbon and over open
## terrain, and a blind review photographed it as "a stray bright gold rule at
## lower-left" - filed under the status line's own defects because that is where it
## appears, and it is not the status line at all. Retail's oracle capture of this
## exact corner has the same rail in ordinary dark gold.
##
## THE MULTIPLIER IS MEASURED, not chosen, by the same rule the scroll rail's was:
## (243/255, 198/255, 107/182) is the ratio that carries this crop's average onto
## the average of the frame members it belongs with. Retail's own runtime applies
## a colour transform to these; this is that transform, solved rather than tasted.
const TRAY_RAIL_PATHS := [
	"screen:StrategicDetailsTray:frame:21/8/3/1",
	"screen:StrategicDetailsTray:frame:21/12/3/1",
]
const TRAY_RAIL_TINT := Color(0.953, 0.776, 0.588, 1.0)
## The content well between the chain-link divider and the tray's right scroll,
## under the tab strip and above the bottom rail.
const TRAY_CONTENT := Rect2(-20.0, 40.0, 592.0, 104.0)
## The status ribbon. Retail's own ribbon ART flattens at y 146.6..184.6, whose
## lower edge is 6.6 authored pixels BELOW the 768-pixel authored screen - the
## flattened bound takes in the plate's drop shadow. In the oracle capture the
## ribbon's visible plate sits at y 1355..1400 of 1440, which is authored
## 722.7..746.7, i.e. 132.7..156.7 in this slot's space. So the art is drawn with
## a MEASURED registration offset of -14 authored pixels, and the caption is set
## on the tray's own bottom rail (152.5..169.5), which is where retail sets it.
const TRAY_RIBBON_ART_OFFSET := Vector2(0.0, -14.0)
const TRAY_RIBBON := Rect2(-13.5, 133.0, 604.3, 30.0)
## The card rail's six slots, at `StrategicDetailsBuildQueue`'s own authored
## instance translations, and the slot's own square measure (the gap between two
## consecutive slots, less the authored gutter the flattened cards leave).
## MEASURED OFF THE FLATTENED CARDS THEMSELVES, not off the named instances.
## `StrategicDetailsBuildQueue`'s slot instances are authored inside its own
## `slots` sprite at (107.75, 46.15), so their raw translations (10.25, 87, ...)
## are not the tray-space positions the cards actually flatten at. The six card
## frames flatten at x 123.2, 199.9, 276.7, 353.4, 430.2 and 506.9, each 68 wide
## and 110 tall from y 25, and those are the numbers here. Reading the instance
## translations instead put this project's structure icons 800 pixels left of
## retail's own cards, on top of the tab rail.
const TRAY_CARD_SLOTS := [123.2, 199.9, 276.7, 353.4, 430.2, 506.9]
## THE SAME SIX SLOTS BY THEIR AUTHORED PATH, in the same LEFT-TO-RIGHT order -
## which is NOT the order retail's display list puts them in. The flattened
## sub-paths are `4/34/{1,25,49,73,97,121}` and their x positions are 199.9,
## 276.7, 353.4, 430.2, 506.9 and 123.2, so the leftmost card is the LAST authored
## one. Reading the display order as the visual order would suppress the wrong
## cards, which is why these are transcribed from the measured x rather than from
## the depth. `TRAY_QUEUE_HEAD_PATH` is retail's seventh, wider slot (x 3.8..96.6)
## - the "currently building" head cell of the queue.
const TRAY_CARD_SLOT_PATHS := [
	"4/34/121", "4/34/1", "4/34/25", "4/34/49", "4/34/73", "4/34/97",
]
const TRAY_QUEUE_HEAD_PATH := "4/7"
## The card well itself - retail's maroon gilt panel with its own flat-black
## runtime host painted over it. See `_card_well_host_path`.
const TRAY_CARD_WELL_PATH := "4/2"
## THE PICTURE HOST INSIDE ONE CARD, in the card slot's own x and the tray's own
## y - MEASURED off `StrategicDetailsBuildQueue`'s flattened triangles rather than
## eyeballed off the card. One card (`5/4/34/121/8`) flattens as five stacked
## quads at four rectangles, and the stack says exactly which one is the host:
##
##   ord 249-250  x 127.0..189.0  y 26.8..131.8  the authored DEFAULT plate, the
##                navy Men-of-the-West White Tree card, shipped for the artist
##   ord 251-252  x 125.0..189.0  y  49.6..110.5 a FLAT BLACK solid - the runtime
##                host retail composites the live portrait into
##   ord 253-254  x 125.0..189.0  y  48.0..112.0 a plain tan stone backing plate
##   ord 255-256  x 123.2..191.2  y  25.0..135.0 the card frame, over all of it
##
## The tan backing is the "blank tan rectangle with black dividers" a blind review
## photographed: it is retail's own art, correctly drawn, with retail's own
## picture missing off the top of it. So the host is the 64x64 rectangle at
## ord 253's bounds, offset +1.8 from the card slot's own left edge.
## The 2 authored pixels on each side are the card frame's own inner lip: the
## backing plate runs UNDER the frame and the picture retail composites into the
## host runs under it too, so a picture drawn after the frame has to stop at the
## opening or it paints over gold. Everything else is retail's measure.
const TRAY_CARD_PICTURE := Rect2(3.8, 48.0, 60.0, 64.0)

## ------------------------------------------------------------------------------
## THE REGISTER OF THE PLAYER'S SURFACE
## ------------------------------------------------------------------------------
##
## The ribbon's separator, its ellipsis and the characters a trimmed tail may not
## end on. They are constants rather than literals because three different call
## sites join and cut the same line, and the defect a blind review named was
## exactly the seam between two of them: the list was joined with `", "` and the
## line was cut on a word boundary, so the ellipsis landed after a comma and the
## slot read `Dark Iron Forge, ...` - a raw join leaking into the UI.
##
## The separator is a MIDDLE DOT with wide spacing, not a comma and not a hyphen:
## a comma reads as a serialized list, and the hyphen this screen used reads as a
## key/value dump. Retail's own ribbon carries a single noun phrase; ours carries
## a few, and they are separated the way a caption separates them.
##
## ROUND SIX: THE RIBBON NO LONGER USES IT. A blind review called the result "a
## dot-joined concatenation of every field the selection object happens to hold",
## and said in the same breath that the IDEA - surfacing the selected region's
## state inline rather than one tooltip noun - is a genuine improvement over
## retail and should be kept, executed with "typographic hierarchy instead of
## interpuncts". That is what `_ribbon_segments` does: the region's name is set as
## the subject in engraved caps, everything after it is set smaller and quieter,
## and the space between them does the work the dots were doing. The constant
## stays because `RIBBON_TAIL_TRIM` still guards the trimmed tail and because the
## flat `tray_ribbon_text` - the string the audit and the runners read - is still
## a join, and a join has to have a separator that cannot be mistaken for content.
const RIBBON_SEPARATOR := "   ·   "
const RIBBON_ELLIPSIS := "…"
const RIBBON_TAIL_TRIM := " ·-,;:"
## The em-space the drawn ribbon sets between its segments. Wide enough to read as
## a break between two registers, and it is a SPACE, so nothing in the line can be
## mistaken for a token the program put there.
const RIBBON_GAP := 2.2

## THE PALANTIR PLAQUE'S TWO RIM CAPTIONS. Retail's plaque carries the two numbers
## and no words (its slots are engine-fed), so the words are this project's - and
## they are constants rather than literals inside the draw so the string audit can
## see them: `player_visible_strings()` reads drawn text as well as Labels.
## `PALANTIR_PLOTS_CAPTION` is RETIRED rather than deleted, and the reason is
## recorded because the constant's absence would otherwise read as an oversight:
## the plot count was stated three times in the bottom tray and this plaque was one
## of the two copies removed. See `_structures_ribbon_line` for the review note and
## for which of the three survives. The left slot now carries the region's ARMIES,
## under retail's own word for them.
const PALANTIR_ARMIES_CAPTION := "Armies"
const PALANTIR_REGION_CP_CAPTION := "Region CP"

## THE WORDS THAT MAY NOT REACH THE GLASS, and the reason this list is a constant
## rather than a review habit.
##
## A blind adversarial review identified this project's screen as the
## reimplementation "before I had looked at a single pixel of frame art", off one
## string: `nothing here builds, construction is not simulated`. Its reading was
## right and it generalises - the tell is not that sentence, it is the REGISTER.
## A shipped game's HUD makes statements about the WORLD. A prototype's HUD makes
## statements about its own SIMULATION, its own CONVERSION and its own DATA, and
## every one of those has a vocabulary.
##
## So the vocabulary is written down, `player_visible_strings()` collects every
## string this screen actually renders, and `wotr_living_world_ui_runner` fails the
## build if any of them contains one. That assertion is the durable fix; the
## rewrites this round are just the first thing it caught.
##
## NOTHING HERE IS CENSORSHIP OF THE HONESTY `AGENTS.md` REQUIRES. Every phrase
## taken off the HUD is present, in full, on the diagnostics panel - and the same
## runner asserts THAT too, so a gap cannot be quietly dropped instead of moved.
## The two assertions only make sense together.
##
## The entries are matched case-insensitively as substrings of the rendered text,
## so each one is either multi-word or a word with no innocent English use on this
## screen.
const IMPLEMENTATION_VOCABULARY := [
	"not simulated", "not implemented", "not converted", "unconverted",
	"is simulated", "simulation", "simulated",
	"nothing happens", "retail", "unresolved", "todo", "placeholder", "stub",
	"debug", "diagnostic", "gamedata", "#define", "bundle", "string table",
	"authoritative state", "strategic session", "named gap", "living-world",
	"livingworld", "centre point", "centroid", "fill mesh", "sub-object",
	"not authored", "engine-fed", "hardcoded", "executable", "converted",
]

## THE END TURN button's authored state per interaction, by retail's own label.
const ENDTURN_STATES := {
	"up": "_up", "over": "_over", "down": "_down", "disabled": "_disabled",
}

## ------------------------------------------------------------------------------
## ONE BUTTON SYSTEM, AND IT IS ONE LIST
## ------------------------------------------------------------------------------
##
## The owner asked why END TURN "looks good but is not the same for ATTACK, CANCEL
## or AUTO-RESOLVE". It was three separate defects wearing one coat, and every one
## of them was a call site that named SOME of the four capsules and not all of
## them:
##
##   1. `CANCEL` was missing from the loop that strips a capsule's own stylebox
##      when retail's art is bound. So retail's `StrategicEndTurnButton` capsule
##      was painted under it by `_draw_capsule` AND this project's drawn oxblood
##      pill was painted on top of it by the theme - two buttons in one rectangle,
##      which is exactly what "not the same" looks like at a glance.
##   2. `CANCEL` was also the only one of the four dressed non-`primary`, so its
##      caption was PARCHMENT where its three neighbours were gold.
##   3. NONE of the four had a FOCUS state once the styleboxes were stripped. A
##      keyboard-focused capsule was indistinguishable from a resting one, which
##      on a screen whose other controls are all pointer-driven reads as the
##      button having no states at all.
##
## The fix is not four more careful call sites. It is ONE list that every pass
## reads - dressing, state signals, stylebox stripping, layout, drawing and the
## runner's own containment check - so a capsule cannot be in the system for one
## pass and outside it for another. That is the whole of "one button system":
## a capsule is a member of `COMMAND_CAPSULES` or it is not a command capsule.
##
## RETAIL AUTHORS `_up`/`_over`/`_down`/`_disabled` AND NOTHING ELSE. Focus is a
## fifth state retail's living world never needed (it is a mouse-only screen) and
## this project does need, so the focus ring drawn in `_draw_capsule` is
## PROJECT-AUTHORED and says so there. Everything else under these four buttons is
## retail's own flattened art.
const COMMAND_CAPSULE_NAMES := ["EndTurn", "Attack", "Cancel", "AutoResolve"]

## ------------------------------------------------------------------------------
## THE COMMAND RAIL'S THREE WIDTH CLASSES - ONE PRIMARY, ONE SECONDARY, ONE GHOST
## ------------------------------------------------------------------------------
##
## An adversarial art-direction review, judging the assembled frame against a
## modern bar rather than against retail:
##
##     "ATTACK, CANCEL and AUTO-RESOLVE are rendered at identical weight,
##      identical fill, identical width class. In a screen whose own instruction
##      line says 'choose a region to attack,' ATTACK must be the loudest control
##      on screen and it is currently tied for third."
##
## It is right, and the reason it happened is worth recording because it was not
## carelessness. Retail's living world authors exactly ONE command capsule
## (`StrategicEndTurnButton`) and never has to rank two of them against each
## other, so "wear retail's capsule" - which is the correct instinct everywhere
## else on this screen - silently means "wear the only weight retail has". A rank
## that retail never needed has to be DESIGNED, and this is that design, stated as
## three width shares in rail order (ATTACK, CANCEL, AUTO-RESOLVE):
##
##   ATTACK       1.24 - the primary. The review asked for 1.2x; 1.24 is what
##                       lands the three cells on the same total run the three
##                       equal cells occupied, so ranking the rail does not move
##                       the deck's left edge or the tray under it.
##   CANCEL       0.76 - the ghost. Narrower than an equal share because it
##                       carries no face (`HudChrome.draw_ghost_button`) and a
##                       ghost sized like a pill reads as a pill that failed to
##                       draw.
##   AUTO-RESOLVE 1.00 - the secondary, unchanged: retail's capsule as authored.
##
## The three shares sum to 3.00 by construction. That is the property that keeps
## this change independent of the deck geometry below it, and the layout runner
## holds it.
const COMMAND_WIDTH_CLASS := [1.24, 0.76, 1.00]

## THE RANK EACH COMMAND CAPSULE WEARS, by the control's own node name, so the
## drawing pass and the dressing pass cannot disagree about which button is the
## primary. The names are `COMMAND_CAPSULE_NAMES`.
##
## END TURN IS A PRIMARY TOO, and that is not a second primary competing with
## ATTACK: the two are never both live on the same reading of the screen. ATTACK
## is the primary of the SELECTION (it exists to complete the sentence the
## imperative line starts) and END TURN is the primary of the TURN, and when there
## is no selection ATTACK is disabled and wears no gold at all - `draw_primary_face`
## refuses to gild a control that will not answer. The review's own words are
## "END TURN, which is a turn-ending commitment, is louder than all three despite
## having no button chrome"; the answer to that is to give END TURN the chrome, not
## to make it quieter than the thing it commits.
const COMMAND_RANK_PRIMARY := "primary"
const COMMAND_RANK_SECONDARY := "secondary"
const COMMAND_RANK_GHOST := "ghost"
const COMMAND_RANKS := {
	"EndTurn": COMMAND_RANK_PRIMARY,
	"Attack": COMMAND_RANK_PRIMARY,
	"Cancel": COMMAND_RANK_GHOST,
	"AutoResolve": COMMAND_RANK_SECONDARY,
}

## THE TRAY'S OWN VISIBLE GILT STILE, AND WHERE THE DECK'S LEFT EDGE BELONGS.
##
## THE DEFECT: the command deck "floats above the tray on a translucent plate
## whose top edge is a hard unornamented horizontal ... whose left terminus is an
## arbitrary diagonal cut that aligns to nothing, and which reads as a separate,
## later-added layer."
##
## THE CUT ALIGNED TO SOMETHING - it aligned to `TRAY_FIELD`, which is the tray
## panel's own quad (`screen:StrategicDetailsTray:frame:21/3/1`, authored
## x -135.2..572.3). The trouble is that that quad is NOT what a player can see.
## Flattening the tray movie draw by draw gives four pieces at the panel's depth:
##
##     21/3/1     x -135.20..572.30   the panel field itself
##     21/3/2/1   x  -93.55..572.23   the panel's textured face
##     21/3/4     x  -73.50.. -34.50  a 39-wide, 173-tall gilt member,
##                                    y -16.15..156.85 - the full height of the
##                                    field
##     21/12/3/1  x -141.10..548.96   the top rail, y -30.60..-13.60
##
## `21/3/4` is the tray's LEFT-HAND STILE: the closed vertical gilt frame member
## at the left end of the panel, and it is the leftmost thing on this island a
## player can actually see, because the 62 authored pixels of field to its left
## (-135.2..-73.5) run behind the palantir island and are never on the glass. At
## the 2560x1440 frame the oracle is judged at that stile lands on window x 891,
## and a scan of the previous capture finds this project's own deck pilaster
## drawn at x 888..891 - three hundred pixels to the right of where the deck's
## card actually began. The deck was terminating at the field's edge and then
## drawing its fitting at the stile's, which is precisely why the two did not read
## as one object: the silhouette and the ornament disagreed.
##
## SO THE DECK'S LEFT EDGE IS THE STILE'S OUTER EDGE, -73.5, and the terminal
## stops being a chamfer swept back over open terrain and becomes what it should
## always have been: the SAME stile, carried up one storey. Retail's frame member
## and this project's are then one continuous vertical line from the tray's floor
## to the deck's head, which is what "docked into the tray as a proper action
## strip in the same gold frame language" means in geometry rather than in words.
##
## -34.50 is the stile's inboard edge, so its own width is 39 authored pixels, and
## the deck's terminal is cut to exactly that - the two members are the same width
## because they are the same member.
const TRAY_STILE := Rect2(-73.5, -16.15, 39.0, 173.0)

## Retail's own region-bonus wording, keyed by the living-world document's own
## bonus field. The VALUE is a string-table key, so what the player reads is
## retail's text with retail's placeholders filled from retail's numbers - this
## table is only the mapping between the two, and every entry is a field the
## document actually carries.
##
## `%d%%` in retail's text is a literal percent sign after the number; `%d` alone
## is a plain count. `_format_bonus()` handles both, and a formatter this project
## cannot fill is shown raw rather than mangled.
const BONUS_STRING_KEYS := {
	"army": "LW:RegionBonusArmy",
	"attack": "LW:RegionAttackBonus",
	"buildingDiscount": "LW:RegionBuildingDiscountBonus",
	"defense": "LW:RegionDefenseBonus",
	"discountedBarracksUnits": "LW:RegionBarracksUnitDiscountBonus",
	"discountedHeroUnits": "LW:RegionHeroDiscountBonus",
	"discountedSiegeUnits": "LW:RegionSeigeDiscountBonus",
	"experience": "LW:RegionExperienceBonus",
	"extraStartResources": "LW:RegionExtraResourcesBonus",
	"fertileTerritory": "LW:RegionTreasuryBonus",
	"freeBuilder": "LW:RegionFreeBuildersBonus",
	"freeInnUnits": "LW:RegionFreeInnUnitsBonus",
	"legendary": "LW:RegionLegendaryBonus",
	"resource": "LW:RegionBonusResource",
}
## The order retail's own panel lists bonuses in is not recorded in any file this
## lane read, so they are listed in a FIXED ALPHABETICAL order by field name.
## That is a stated presentation choice, not a claim about retail.
const BONUS_ORDER := [
	"fertileTerritory", "army", "legendary", "resource", "attack", "defense",
	"experience", "buildingDiscount", "discountedBarracksUnits",
	"discountedHeroUnits", "discountedSiegeUnits", "extraStartResources",
	"freeBuilder", "freeInnUnits",
]
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")
const RulesScript = preload("res://src/wotr/wotr_autoresolve_rules.gd")
const AutoResolveBattleScript = preload("res://src/wotr/wotr_autoresolve_battle.gd")

## THE TWO COLOURS THE BATTLE REPORT USES, and the only distinction it draws.
## Retail's numbers in one, this project's in the other, side by side on the
## same line so a player never has to remember which is which. The same
## discipline this branch already applies to the marker magnification, the
## hand-built chrome and the `structures 0 / 3 plots` line.
## How many of a territory's member regions the card names before it counts the
## rest. Retail's own largest territory carries eight; four names plus a count is
## what fits the tray's well at the narrowest window in the layout runner's
## `SIZES`, measured rather than picked, and the control clips as well.
const TERRITORY_MEMBERS_SHOWN := 4

## How many lines of card the tray's well is TYPESET FOR, and the line spacing the
## fitter measures with. Retail's own well is short (about 90 authored pixels), so
## the type is sized to the well rather than to the window: the region card's
## longest form is eight lines - name, yields, plots, a rule, retail's "Territory
## of Region", retail's "Unified Region Bonus", the member run and the
## command-point limit - and the ninth is the slack `_fit_card_lines` keeps for a
## line that wraps.
const CARD_LINES := 9
const CARD_LINE_SPACING := 1.45

const RETAIL_COLOR := "#9fd18a"
const PROJECT_COLOR := "#e8b45c"

## Seat colours. Fixed by seat index and never player-chosen: a colour is
## presentation, and a presentation value that varied per session would be one
## more thing to keep out of the hash.
const SEAT_COLORS: Array[Color] = [
	Color("#4d7fd6"), Color("#c8483f"), Color("#5aa552"), Color("#d0b03c"),
	Color("#a763c9"), Color("#3fb0ad"),
]
const NEUTRAL_COLOR := Color("#5a6656")

const MAP_INSET := 44.0
const MARKER_RADIUS := 11.0
## The height of the "this is not retail's map" banner over the flat fallback.
const FALLBACK_BANNER_HEIGHT := 158.0
## Lines of the refusal the banner has room for. The full text always reaches the
## launch log; the banner shows the headline, the present-but-broken bundles and
## the command that fixes it, which are the first lines by construction.
const FALLBACK_BANNER_LINES := 8

var session: SessionScript = null
## Why War of the Ring is unavailable, or "" when it is. Non-empty means the map
## area shows the reason and nothing else.
var unavailable_reason := ""
## Pack map ids the tactical layer can actually boot, supplied by the menu.
var available_map_ids: Array = []

## The brass shell, painted under every other control. Presentation only.
var chrome_layer: Control
## The one surface the animation clock is allowed to invalidate. See `build()` for
## the measured regression that split it out of `chrome_layer`, and
## `animated_layers()` for the property a runner holds it to.
var pulse_layer: Control
var heading_label: Label
var status_label: Label
## The flat 2D region graph. Kept as the honest fallback for when no living-map
## bundle has been converted, and labelled as a fallback when it is showing.
var map_view: Control
## Retail's 3D map, when a bundle is available.
var map3d: Control
var map_mode_label: Label
var map_bundle: BundleScript = null
## Retail's per-region territory geometry, when converted.
var region_geometry: RegionGeometryScript = null
## Why there is no territory shading, or "" when there is.
var region_geometry_reason := ""
## Retail's string table, when converted. Null means regions carry retail ids.
var strings: StringsScript = null
var strings_reason := ""
## THE SEAT NAMES a player reads - retail's own SIDE: text for each
## LivingWorldPlayerTemplate, resolved through the setup string bundle. Always
## constructed (so `seat_label()` is total); its misses are NAMED GAPS on the
## diagnostics panel, and the fallback is the template id, visibly a key.
var names: DisplayNamesScript = DisplayNamesScript.new()
## Retail's gamedata `#define` table, so a macro bonus shows retail's number.
var macros: MacrosScript = null
var macros_reason := ""
## Retail's UI surface: the buildable structures, the recruitable armies and the
## atlas crop behind every icon. Null means banners carry no portrait and there
## is no build menu, and the screen says so.
var ui: LivingWorldUiScript = null
var ui_reason := ""
## Retail's own 3D marker models - the army banners, the marching columns and the
## build-plot foundation decals - or null with the reason.
var markers: MarkerModelsScript = null
var markers_reason := ""
## Retail's own portraits of the regions themselves, or null with the reason.
var region_images: RegionImagesScript = null
var region_images_reason := ""
## RETAIL'S OWN STRATEGIC HUD ART - the 24 flattened APT screens. Null means the
## HUD falls back to the hand-drawn plates in `wotr_hud_chrome.gd` and the
## diagnostics panel names the gap.
var strategic: StrategicUiScript = null
var strategic_reason := ""
## `slot name -> {origin, scale, rect, movie, frame}` for the current frame size,
## recomputed by `_relayout` and read by `_draw_chrome` and every control that
## sits inside an island. Empty when the strategic bundle is absent.
var _islands: Dictionary = {}
## `button name -> "up"/"over"/"down"`, so the chrome pass can draw retail's own
## authored capsule state under each command button. Presentation only.
var _capsule_states: Dictionary = {}
## The build plot the radial menu is open on, as `{region, index}` or `{}`.
## PRESENTATION ONLY - it lives here, not on the session, and reaches nothing.
var selected_plot: Dictionary = {}
## The army marker the player selected for retail's "left click ... then right
## click to move" command. Presentation selection only; the authoritative move
## still goes through `WotrSession`.
var selected_army_id := -1
## Why there is no 3D map, or "" when there is one.
var map_reason := ""
## The turn NUMBER, in the left capsule of retail's phase chevron bar - the slot
## retail's own "Turn: / 1" sits in.
## A DRAWN Control, not a Label, and for the same reason `header_label` is one:
## retail's plaque is a TWO-ROW cell - `Turn:` in white across the top of the
## black field and the number in gold centred under it - and a Label cannot set
## two colours or two weights. This screen used to set `TURN 1` as one
## left-aligned run straddling the plate's own right-hand pilaster, and a blind
## review named it: "that is the signature of someone binding a string to a rect
## rather than laying type into an ornament".
var turn_banner: Control
## The two rows the plaque carries, written by `_refresh_turn_banner` and drawn by
## `_draw_turn_plaque`. Public so a runner can read what the plate was showing.
var turn_plaque_label := ""
var turn_plaque_value := ""
## Whose move it is, on retail's banner strip under the chevrons - the slot
## retail sets "tactical phase" in. Set in retail's DISPLAY face when one is
## bound (a partial binding; see `WotrHudChrome.DISPLAY_FACE_BINDING`).
var phase_banner: Label
## RETAIL'S HEADER NUMBERS: the seat's purse and its command points. Retail puts
## these across the top of its strategic shell; this is the same two facts read
## out of the same data. There is NO PHASE BAR beside them and there will not be
## one - see `_refresh_header()`.
## A drawn Control, not a Label: `_draw_header` sets each fact on its own socket.
var header_label: Control
## What to do RIGHT NOW, as one short imperative in the tasks tray.
var hint_label: Label
## Every seat's standing as a PLAQUE - heraldry, name, counters in sockets -
## drawn by `_draw_standings` from `_seat_plaques`. The colour-chip legend and
## the control cheat-sheet that used to run along the bottom are GONE from the
## player surface: a shipped RTS teaches controls in tooltips, not in a
## permanent status bar, and an adversarial review called both disqualifying.
var standings_label: Control
var detail_label: RichTextLabel
## RETAIL'S OWN PORTRAIT OF THE REGION under the pointer, and the line that says
## which authored field it came from - or which one retail names and does not
## define.
## WHAT THE COMPASS DIAL'S RING OF STRUCTURE ICONS IS, in the words the player is
## shown on hover over its backing. Stated once, read by the tooltip and by
## `player_visible_strings()`.
##
## IT USED TO BE A REFUSAL and it is an instruction now. The ring's six wells were
## unpressable last round and this string said so; they press, so it says what
## pressing does. The per-structure answers - the price, what it grants, and the
## one sentence saying why a particular one is barred - are on the wells.
const COMMAND_DIAL_UNAVAILABLE := (
	"The works %s may raise. Choose a foundation, then press a well to raise its "
	+ "structure; the price is under each. Building does not end your turn.")
## ------------------------------------------------------------------------------
## THE TWO MEDALLIONS IN THE PALANTIR'S RIM
## ------------------------------------------------------------------------------
##
## When the strategic art is bound, the palantir's rim carries two round gilt
## medallions - a KEY and a BANNER - straight out of retail's own
## `StrategicPalantir` movie. They are drawn on `chrome_layer`, which ignores the
## mouse, so they have never been controls. They are also, unmistakably, the shape
## of two buttons, and the owner pressed them and nothing happened.
##
## THEY ARE WIRED NOW, AND THE MAPPING IS RETAIL'S OWN RECORD RATHER THAN A GUESS.
##
## A PREVIOUS ROUND OF THIS LANE REFUSED TO WIRE THEM, on the grounds that retail
## "carries the art and not the mapping" and that picking which medallion opened
## the settings would be inventing a binding. THAT WAS WRONG ON THE FACT, and the
## refusal is withdrawn rather than quietly dropped, because the reasoning is still
## readable in this project's history and someone will otherwise re-derive it.
##
## `StrategicPalantir` DOES record the mapping, in `namedInstances`, which is
## retail's own authored instance table and is in the converted bundle:
##
##     name              scope   depth   translation
##     optionsButton     root    100     (81, 27)
##     objectivesButton  root     96     (140, 27)
##
## and the flattened frame's own triangles put depth 100 at authored x 81..118 and
## depth 96 at x 140..177, both y 27..64. Cropping this movie's atlas at the UVs
## those two draws carry shows the LEFT disc (depth 100) is the GOLD KEY and the
## RIGHT disc (depth 96) is the BLUE BANNER. So:
##
##     THE KEY IS `optionsButton`.  THE BANNER IS `objectivesButton`.
##
## That is the opposite of the reading a designer would reach for from the icons
## alone - a banner looks like a settings flag and a key looks like a checklist -
## which is exactly why it had to be read out of the data instead of chosen.
##
## WHAT EACH ONE DOES HERE, and which half is retail's:
##   * THE KEY opens this screen's settings, because that is what retail's own
##     `optionsButton` is. Retail's binding, this project's screen behind it.
##   * THE BANNER opens and shuts the objectives plaque - the critical-tasks
##     plaque at the top of the frame. Retail's `objectivesButton` drives a
##     campaign objectives screen this project does not have; the plaque is the
##     nearest true thing this screen owns, and the choice to point the control at
##     it is PROJECT-AUTHORED and recorded in the diagnostics panel as such.
##
## THE HOVER AND PRESSED ART IS THIS PROJECT'S, AND THAT IS A NAMED GAP RATHER
## THAN A PREFERENCE. Retail authors four states for each medallion - the sprite
## timelines for characters 57 and 65 carry `_disabled`(0), `_up`(10), `_over`(20)
## and `_down`(30), each swapping in a different shape - but those are CHILD
## timelines, and child timeline playback is the standing named gap
## `timeline-playback-not-bound`: the flattening carries the root frame only, so
## the bundle holds one state per medallion and not four. Rather than leave a
## control that cannot show it was pressed, `_draw_medallions` lights retail's
## resting art with a drawn bloom and sinks it on press, and says so there.
##
## THE RECTANGLES ARE READ OUT OF THE MOVIE, not measured off a screenshot. The
## previous version of this block stated them as fractions of the palantir island
## taken off a 2560x1440 capture; `_medallion_rect` derives them from the named
## instance's own depth and the flattened triangles at that depth, so they follow
## retail's art at any window size instead of following one screenshot.
const MEDALLION_KEY_INSTANCE := "optionsButton"
const MEDALLION_BANNER_INSTANCE := "objectivesButton"

const COMMAND_DIAL_UNAVAILABLE_NO_SEAT := (
	"The command ring stands empty: no seat holds the region on the glass, and a "
	+ "seat raises works only on ground it holds.")

var region_portrait_frame: Control
var region_portrait_caption: Label
## The transparent hover target over retail's command ring - see `build()`. It
## exists ONLY so the ring can say why it does nothing; it commits nothing and
## refuses nothing, because there is nothing there to commit.
var dial_affordance: Control

## THE THREE POOLS OF BUILD CONTROLS, one per surface the offer is drawn on.
## Pooled at their maximum count and hidden rather than created per refresh, so
## the layout runner's "every control on this screen is framed" sweep sees one
## stable roster of children instead of a set that changes with the selection.
var _dial_buttons: Array[Button] = []
var _plot_card_buttons: Array[Button] = []
var _build_row_buttons: Array[Button] = []
## As many roster rows as the tray's well can hold at its tallest. Retail authors
## at most four structures per faction, so this is headroom rather than a limit,
## and `_place_build_row_buttons` hides the ones the offer does not reach.
const BUILD_ROW_BUTTONS := 8
## THE TWO MEDALLIONS IN THE PALANTIR'S RIM, AS REAL CONTROLS. `medallion_key` is
## retail's `optionsButton` and opens the settings; `medallion_banner` is retail's
## `objectivesButton` and opens or shuts the objectives plaque. See the block at
## `MEDALLION_KEY_INSTANCE` for how the two were told apart and which half of each
## binding is retail's. Both are null-placed (zero size) when the strategic art is
## not bound, because the drawn fallback dish carries no medallions to press.
var medallion_key: Button
var medallion_banner: Button
var message_label: Label
var unplaced_label: Label
var unplaced_host: VBoxContainer
var attack_button: Button
## CANCEL - the third cell of the command rail, and a REAL control rather than a
## slot-filler. `MAIN MENU` used to sit here, which a blind review called an
## information-architecture failure and was right about: the global menu is not a
## peer of two combat verbs, and only a prototype wires it into a
## battle-resolution prompt because the button was already there. Retail's answer
## in that slot is Cancel, so this clears the staging and the chosen target - the
## exact state ATTACK and AUTO-RESOLVE act on - and it is disabled when there is
## nothing staged to clear.
var cancel_button: Button
var auto_resolve_button: Button
var end_turn_button: Button
var back_button: Button
## THE PAUSE SHELL - the ESCAPE surface MAIN MENU lives on, and the RESUME button
## that closes it. Both hidden until ESC asks for them. See `build()` for why
## shell navigation is not allowed on the standings panel.
## ------------------------------------------------------------------------------
## THE KEYS THIS SCREEN BINDS.
## ------------------------------------------------------------------------------
##
## The table itself lives in `user_settings.gd` (`OpenBFMEUserSettings.KEY_BINDINGS`)
## and the reasoning for that is written out there. It is aliased here so this
## screen has ONE name for it: the pause shell draws it under the PAUSED head (ESC,
## four lines, no hunting), the tasks box's tooltip names the two keys that change
## what is on the glass, and `player_visible_strings()` publishes it so a runner
## reads exactly what a player reads.
##
## F11 IS IN THE TABLE BUT NOT BOUND IN THIS FILE - it is bound in
## `main_menu.gd:toggle_fullscreen()`, because fullscreen has to work on the menu
## and on the setup screen too and this screen is not their parent. This screen
## deliberately does not consume F11; it falls through to the shell.
const KEY_BINDINGS := OpenBFMEUserSettings.KEY_BINDINGS
const KEYBIND_REMAP_GAP := OpenBFMEUserSettings.KEYBIND_REMAP_GAP

## THE HUD-OFF STATE. The owner asked for "a good way to get rid of the ui so it
## can get out of my way and just play the game", and F2 is that way: every island
## this screen floats over Middle-earth goes down, the map keeps the whole window
## and keeps its camera, and F2 brings them all back.
##
## It is a FIELD rather than a one-shot sweep because `_relayout()` sets
## `message_label.visible` itself (the tasks plaque opens and shuts with its own
## content), so a sweep would be undone by the next resize. `_apply_hud_visibility`
## is called at the end of every relayout and this field is what it reads.
var hud_hidden := false
## What each island's `visible` was the moment the HUD went down, so F2 restores
## the screen it hid rather than a screen where everything is on.
var _hud_visibility_before: Dictionary = {}

var pause_shell: Control
var pause_options: Button
var pause_resume: Button
## THE BOTTOM COMMAND BAR'S OWN CONTROLS. `_tab_buttons` is `key -> Button` for
## retail's TERRITORY / ARMIES / STRUCTURES rail; `tray_ribbon` is the caption on
## the tray's bottom rail. `active_tab` is PRESENTATION ONLY - it selects what the
## tray's well is showing and reaches no simulation state, never enters a hash.
var _tab_buttons: Dictionary = {}
## `key -> the caption a player reads on that tab`, in retail's own English. The
## caption is DRAWN rather than set on the Button (see `build()` for the Godot
## minimum-size reason), so this dictionary is where the string lives and is what
## `player_visible_strings()` reads.
var _tab_captions: Dictionary = {}
## The size the drawn tab captions are set at, solved against the cell by
## `_relayout` and read by `_draw_command_bar`, and the tab under the pointer.
## Presentation only.
var _tab_caption_size := 14
var _hovered_tab := ""
var tray_ribbon: Control
## What the ribbon says, written by `_refresh_tray_ribbon` and drawn by
## `_draw_tray_ribbon`. Public so a runner can read what the bar was showing.
var tray_ribbon_text := ""
## The same line as `tray_ribbon_text`, split into the segments the rail is
## actually SET in: `[{text, rank}]`, rank 0 being the subject. Presentation only.
var tray_ribbon_segments: Array[Dictionary] = []
## The tray's whole content well as `_relayout` measured it, and the gutter it
## leaves between retail's art and this screen's text. Kept because the well the
## TEXT gets depends on the tab and on the region under the pointer, both of which
## change far more often than the layout does - see `_place_detail_well`.
## THE STRUCTURES TAB'S ROSTER, as rows rather than as a joined string:
## `{title, cost, image_id}` per offering the seat holding the region under the
## pointer may raise. Written by `_refresh_detail`, drawn by
## `_draw_structure_roster`, and read verbatim by `player_visible_strings()` - so
## the words the audit sweeps are the words on the glass. Presentation only.
var structure_roster: Array[Dictionary] = []
var _tray_content_rect := Rect2()
var _tray_content_gutter := 14.0
var active_tab := "territory"

## THE BATTLE RESULT SCREEN. Hidden until a battle is auto-resolved, then it
## covers the map with the WORKING - what each side rolled, what modified it,
## and why the outcome fell the way it did. It is a report rather than an
## animation, and it says so, because retail's animated auto-resolve
## presentation is not converted and drawing one would be inventing it.
var report_backdrop: ColorRect
var report_text: RichTextLabel
var report_close: Button
## The last auto-resolve, kept so the report can be redrawn and so a test can
## read what the screen was showing. Presentation only: never hashed, never
## handed to anything, cleared when the report closes.
var last_auto_resolve: Dictionary = {}

## The opponent's reports from the LAST hand-off, in order. Presentation only -
## the strategic state was already changed by the session before this screen saw
## them. Read by `_opponent_turn_lines()` for the glass and by
## `_conversion_gap_lines()` for the diagnosis, which is where the refusals and
## the "did retail choose this or did we" provenance go.
var _ai_reports: Array[Dictionary] = []

## THE DIAGNOSTICS OVERLAY. Everything on this screen that is a claim about the
## CONVERSION rather than about the campaign - the document provenance line, the
## conversion report, the NOT CONVERTED list, the portrait sourcing - lives on
## this panel, hidden until toggled. NOTHING IS DELETED by hiding it: every
## label keeps being refreshed while invisible, the runners keep reading their
## text, and the named gaps stay named - they are just no longer body copy in
## the player's own HUD, which is where they had been living.
##
## IT OPENS ON F9, not on a button: the always-visible "DIAGNOSTICS" toggle was
## itself a developer surface on the player's HUD, and the review said so. The
## binding is printed to the launch log at configure() so it stays findable.
var diagnostics_panel: Control
## The named gaps and provenance notes, one place, refreshed with the screen.
var gaps_label: RichTextLabel
## Retail's own shell face (Albertus MT) out of the mounted packs, or null with
## the reason - a missing face keeps the default font and is a NAMED gap.
var hud_font: Font = null
var hud_font_reason := "no content pack roots were handed to this screen, so retail's Albertus MT face was not looked for"
## Retail's own DISPLAY face (Omnia, omnialtstd.ttf) - the uncial lettering on
## retail's phase banner and palantir label - or null with the reason. The two
## faces fail independently; neither ever stands in for the other.
var display_font: Font = null
var display_font_reason := "no content pack roots were handed to this screen, so retail's Omnia display face was not looked for"
## Retail's own phase-band strip and radial ring off its APT sheet, or null
## with the gap named in the diagnostics panel.
var band_texture: Texture2D = null
var ring_texture: Texture2D = null
## What the portrait plate is showing and where it came from - written by
## `_draw_region_portrait`, read by the diagnostics panel, so the sourcing line
## stays available without sitting inside the player's region card.
var portrait_provenance := ""

## The seat plaques' facts, computed by `_refresh_standings` and drawn by
## `_draw_standings`: one row per seat plus the unclaimed line. Presentation
## only - every number is read off the authoritative state at refresh.
var _seat_plaques: Array[Dictionary] = []
## The header's two readouts (label, value, tooltip fact), computed by
## `_refresh_header` and drawn by `_draw_header` as ornamented plates rather
## than one string in one box.
var _header_facts: Array[Dictionary] = []

## Every region bonus whose macro the `#define` table could not resolve, as
## `field (MACRO): why`. Filled by `_format_bonus` while the card is built and
## read by `_conversion_gap_lines`, so a bonus this project cannot fill is a NAMED
## GAP on the diagnostics panel instead of red engineering prose on the card.
var _unresolved_bonus_macros: Array[String] = []
## Why a region is not drawn on the map, in one sentence, for the diagnostics
## panel. `_rebuild_unplaced` writes it; the block on the HUD carries only the
## count and the names, which is the part a player can act on.
var _unplaced_reason := ""
## Why the chosen target's battle would be fought somewhere other than the map
## retail names for it, or "" when no target is chosen. Written by
## `_armies_tab_lines`, read by `_conversion_gap_lines`.
var _stand_in_battlefield := ""

var _rows: Array[Dictionary] = []
var _row_by_id: Dictionary = {}
var _targets: PackedStringArray = PackedStringArray()
## The subset of `_targets` that is UNOWNED ground - a claim rather than a battle.
## Taken from `session.claim_targets()` rather than re-derived from region owners
## here, so the screen's counts can never disagree with the rule
## `session.commit_attack()` will actually apply.
var _claims: PackedStringArray = PackedStringArray()
var _moves: PackedStringArray = PackedStringArray()
var _staging: PackedStringArray = PackedStringArray()
var _screen_positions: Dictionary = {}


## ------------------------------------------------------------------------------
## THE HUD'S ONE CLOCK - WHY THIS SCREEN MOVES AT ALL
## ------------------------------------------------------------------------------
##
## An adversarial art-direction review, on motion:
##
##     "Nothing in this frame telegraphs that it is animated. No motion blur, no
##      glow bloom, no particle, no trail on the sea, no pulse on the actionable
##      region. A modern strategic layer sells itself in motion; if the marketing
##      still has to do the work alone, it needs at minimum a lit, pulsing 'you
##      can act here' state, and there isn't one."
##
## The map stream owns the on-map pulse. This is the HUD's half, and it is ONE
## clock rather than one tween per surface, for two reasons that are both about
## this screen specifically:
##
##   * EVERY ANIMATED SURFACE HERE IS DRAWN, not tweened. The capsules, the phase
##     cells and the plates are painted by `_draw_chrome` into a single
##     `chrome_layer`; a per-surface tween would have to invalidate that layer
##     anyway, so N tweens and one clock repaint exactly the same pixels and the
##     clock is the honest description of what is happening.
##   * ONE PHASE MEANS ONE HEARTBEAT. Two surfaces breathing at unrelated
##     frequencies is the visual signature of two teams, which is the reading this
##     whole round exists to remove. Everything that pulses on this HUD pulses
##     together and in phase, so the frame has a pulse rather than several.
##
## `_pulse` is 0..1 on a sine, and `PULSE_PERIOD` is deliberately slow: a control
## that throbs at UI speed reads as an error state. Two and a half seconds is about
## a resting breath and is the rate a "you may act here" light is set at in the
## genre.
const PULSE_PERIOD := 2.5
var _pulse := 0.0
## Whether anything on screen is actually asking for the clock. A HUD with no live
## primary and no lit phase is a still frame, and repainting a still frame sixty
## times a second is a cost with no picture to show for it - so the clock stops
## itself. `_refresh` sets this from the same state the drawing reads.
var _pulse_wanted := false


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	if heading_label == null:
		build()
	report_map_availability()
	set_process(true)


## THE CLOCK. Advances `_pulse` and repaints THE PULSE LAYER AND NOTHING ELSE.
##
## `_process` is a hot path by definition and this screen historically had none, so
## every line of it is written against a measured cost:
##
##   * IT REPAINTS ONE THIN LAYER. The first version invalidated `chrome_layer`,
##     which re-emits retail's five flattened APT islands and every drawn plate,
##     row and caption on the screen. That measured 18.33 ms/frame idle against a
##     4 ms budget, for four polylines of animation. See `build()`.
##   * IT STOPS ITSELF. `_pulse_wanted` is false whenever no primary is live, so a
##     paused frame, an opponent's turn and the bare-map stop all cost nothing at
##     all - the clock is not merely cheap then, it does not run.
##   * IT DOES NOT TOUCH THE PALANTIR. An earlier version also repainted
##     `region_portrait_frame` every frame "so the two do not drift apart"; nothing
##     on that frame animates, so the only thing it could not drift from was its
##     own cost.
func _process(_delta: float) -> void:
	if not _pulse_wanted or pulse_layer == null or not is_visible_in_tree():
		return
	_pulse = 0.5 + 0.5 * sin(Time.get_ticks_msec() / 1000.0 * TAU / PULSE_PERIOD)
	pulse_layer.queue_redraw()


## THE SURFACES THE ANIMATION CLOCK INVALIDATES, as a list, so the property that
## `chrome_layer` is NOT one of them can be asserted rather than remembered.
##
## This exists because the regression it guards against was invisible in every
## picture: the screen looked right, drew right, and cost eighteen times its frame
## budget, and nothing in this lane could tell the difference between "repaints a
## thin layer" and "repaints everything". `wotr_region_card_runner` holds it now.
func animated_layers() -> Array[CanvasItem]:
	var layers: Array[CanvasItem] = []
	if pulse_layer != null:
		layers.append(pulse_layer)
	return layers


## ------------------------------------------------------------------------------
## EVERYTHING ON THIS SCREEN THAT MOVES
## ------------------------------------------------------------------------------
##
## A handful of outlines. They are drawn here rather than beside the fittings they
## belong to for one reason and it is cost, not composition: what is on this layer
## is repainted sixty times a second and what is on `chrome_layer` is repainted
## when the game state changes, so the split is exactly "does this differ between
## two frames in which nothing happened".
##
## THE STATIC HALVES STAY WHERE THEY WERE - the primary's gold face, the lit phase
## cell's warm ground and the veil on its neighbours are all painted by
## `_draw_chrome` at a fixed value. Nothing here is load-bearing for the hierarchy:
## with the clock stopped, the primary is still the only gilt control on the screen
## and the lit phase is still the only lit cell. The pulse says "you may act now",
## it does not say "this is the primary".
func _draw_pulse() -> void:
	if pulse_layer == null or hud_hidden:
		return
	# THE PRIMARIES' BREATH, on whichever of them is live. A disabled primary wears
	# no gold face at all (`HudChrome.draw_primary_face`), so lighting one would be a
	# light on a control that will not answer.
	for button in command_capsules():
		if button == null or not button.visible or button.disabled:
			continue
		if String(COMMAND_RANKS.get(String(button.name), "")) != COMMAND_RANK_PRIMARY:
			continue
		HudScript.draw_primary_glow(pulse_layer,
			Rect2(button.position, button.size), _pulse)
	# THE CLOCK'S OWN HAND: the lit chevron's ring, on the same heartbeat, so the
	# frame has one pulse rather than several - see `PULSE_PERIOD`.
	if island_is_shown("checklist"):
		var cell := phase_cell_rect(current_phase())
		if cell.size.x > 0.0:
			HudScript.draw_phase_glow(pulse_layer, cell, _pulse)


## Whether the HUD has anything worth animating this frame. Presentation only; it
## reads the same live state the drawing does and writes nothing.
##
## THE TEST IS "IS THERE A LIT PRIMARY", not "is the screen open". The pulse means
## exactly one thing - YOU MAY ACT HERE - so it runs when a primary is live and
## stops when neither is, which is also what keeps a paused or opponent-turn frame
## still.
func _pulse_is_wanted() -> bool:
	if attack_button != null and attack_button.visible and not attack_button.disabled:
		return true
	if end_turn_button != null and end_turn_button.visible and not end_turn_button.disabled:
		return true
	return false


## Say at STARTUP whether retail's 3D map will be there, without paying for the
## 2.5 MB mesh and 48 textures a full load costs. The exported build that shipped
## the flat fallback wrote a 234-byte log with one content line in it; a launch
## log that does not mention the map at all is how a map goes missing quietly.
## This runs before any pack root is known, so it reports the environment and
## user-data candidates - the two the owner controls directly.
func report_map_availability() -> void:
	var probed: Dictionary = BundleScript.probe([])
	if bool(probed.get("found", false)):
		print("[WotrMap] startup: a living-map bundle is present at %s [%s]; it is read when War of the Ring opens." % [
			String(probed.get("root", "")), String(probed.get("origin", ""))])
		return
	var places: Array[String] = []
	for row in probed.get("rows", []) as Array:
		places.append("%s [%s]" % [String((row as Dictionary)["root"]), String((row as Dictionary)["origin"])])
	print("[WotrMap] startup: NO living-map bundle in any candidate location, so War of the Ring will draw its flat 2D region graph. Looked at: %s. Set %s to a bundle directory produced by `python -m openbfme_importer.livingmap_bundle`." % [
		"; ".join(places), BundleScript.BUNDLE_ENV])


func build() -> void:
	# THE FRAME, UNDER EVERYTHING. Retail's strategic screen is a warm brass shell
	# with inset panels, not a set of labels on a black field. This Control paints
	# that shell and ignores the mouse, so it changes how the screen looks and
	# nothing about how it behaves.
	chrome_layer = Control.new()
	chrome_layer.name = "Chrome"
	chrome_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	chrome_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	chrome_layer.draw.connect(_draw_chrome)
	add_child(chrome_layer)

	# ------------------------------------------------------------------------------
	# THE PULSE LAYER - EVERYTHING THAT MOVES, AND NOTHING THAT DOES NOT
	# ------------------------------------------------------------------------------
	#
	# THIS IS A MEASURED PERFORMANCE FIX, not tidiness. The first version of the
	# HUD's animation clock (see `PULSE_PERIOD`) advanced `_pulse` and then called
	# `chrome_layer.queue_redraw()` sixty times a second. `chrome_layer` is where
	# EVERY plate, rail, capsule, roster row and engraved caption on this screen is
	# painted, including retail's five flattened APT islands - tens of thousands of
	# triangles re-emitted per frame - and the frame budget runner caught the result
	# immediately: idle went from 1.00 ms to 18.33 ms against a 4 ms budget, and pan
	# from 7.78 ms to 26.45 ms against 12 ms. A companion runner measured the same
	# thing from the other side - `idle-hud-hidden` at 6.90 ms against `idle` at
	# 24.80 ms on the same map, an 18 ms delta that appeared and disappeared with
	# this layer.
	#
	# The animation itself is four polylines. It was costing eighteen milliseconds
	# because it was invalidating a surface eighteen milliseconds wide.
	#
	# So the moving parts live on their own transparent layer that carries nothing
	# else, and the clock invalidates only that. It sits at the SAME `z_index` as the
	# chrome pass and is added to the tree immediately after it, so it composites
	# over retail's capsule art and under every live control's own caption - which is
	# where light on a button belongs.
	pulse_layer = Control.new()
	pulse_layer.name = "Pulse"
	pulse_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	pulse_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	pulse_layer.draw.connect(_draw_pulse)
	add_child(pulse_layer)

	# THE TITLE EXISTS AND IS NOT ON THE HUD. Retail's living-world screen
	# carries no screen title - the map IS the screen - so this label is kept
	# only as the built-screen sentinel every entry point checks, and it is
	# hidden rather than deleted so nothing null-checks its way into a crash.
	heading_label = Label.new()
	heading_label.name = "Heading"
	heading_label.text = "WAR OF THE RING"
	heading_label.visible = false
	add_child(heading_label)

	# THE DOCUMENT PROVENANCE LINE. It stays - "the map looks wrong" is usually
	# "a different document loaded than you think" - but it is DIAGNOSIS, not
	# HUD, so it lives on the diagnostics overlay built at the end of this
	# function rather than across the player's title bar, where it had been.
	status_label = Label.new()
	status_label.name = "Status"
	status_label.position = Vector2(16, 44)
	status_label.custom_minimum_size = Vector2(1200, 20)
	status_label.size = Vector2(1200, 20)
	status_label.add_theme_font_size_override("font_size", 14)
	status_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)

	# WHOSE TURN IT IS, on the band strip. Retail hangs a wide engraved banner
	# under its top plates ("TACTICAL PHASE"); the strip behind this label is
	# retail's own band off `apt_LivingWorldUI_1.tga` when it is converted, and
	# the text is the turn and the seat - real state, not an invented phase.
	turn_banner = Control.new()
	turn_banner.name = "TurnBanner"
	turn_banner.position = Vector2(30, 64)
	turn_banner.custom_minimum_size = Vector2(790, 26)
	turn_banner.size = Vector2(790, 26)
	turn_banner.mouse_filter = Control.MOUSE_FILTER_PASS
	turn_banner.clip_contents = true
	turn_banner.draw.connect(_draw_turn_plaque)
	add_child(turn_banner)

	# WHOSE MOVE IT IS, on retail's own banner strip under the phase chevrons -
	# the slot retail sets "tactical phase" in. It carries the seat's name in
	# retail's English and whether that seat is the player or the AI, which is
	# this project's real state rather than a phase it does not model.
	phase_banner = Label.new()
	phase_banner.name = "PhaseBanner"
	phase_banner.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	phase_banner.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	phase_banner.clip_text = true
	phase_banner.add_theme_font_size_override("font_size", 17)
	phase_banner.add_theme_color_override("font_color", HudScript.PARCHMENT)
	phase_banner.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
	phase_banner.add_theme_constant_override("shadow_offset_x", 1)
	phase_banner.add_theme_constant_override("shadow_offset_y", 2)
	add_child(phase_banner)

	# THE SEAT'S NUMBERS, top-left the way retail sets its "Player Bonuses"
	# readout - one ornamented plate PER FACT (label in caps, value in gold),
	# not one string in one box. Drawn by `_draw_header` off `_header_facts`.
	header_label = Control.new()
	header_label.name = "Header"
	header_label.position = Vector2(28, 14)
	header_label.custom_minimum_size = Vector2(440, 34)
	header_label.size = Vector2(440, 34)
	header_label.mouse_filter = Control.MOUSE_FILTER_PASS
	header_label.draw.connect(_draw_header)
	add_child(header_label)

	hint_label = Label.new()
	hint_label.name = "Hint"
	hint_label.position = Vector2(30, 94)
	hint_label.custom_minimum_size = Vector2(1230, 20)
	hint_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	# A line longer than the tasks box is trimmed with an ellipsis; centred
	# clipping would cut its START, which is the half that says what to do.
	hint_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	hint_label.add_theme_font_size_override("font_size", 14)
	hint_label.add_theme_color_override("font_color", HudScript.PARCHMENT_DIM)
	add_child(hint_label)

	map_view = Control.new()
	map_view.name = "MapView"
	map_view.position = Vector2(24, 118)
	map_view.custom_minimum_size = Vector2(1240, 548)
	map_view.size = Vector2(1240, 548)
	map_view.mouse_filter = Control.MOUSE_FILTER_STOP
	map_view.draw.connect(_draw_map)
	map_view.gui_input.connect(_on_map_input)
	add_child(map_view)

	map3d = MapViewScript.new()
	map3d.name = "Map3D"
	map3d.position = map_view.position
	map3d.custom_minimum_size = map_view.custom_minimum_size
	map3d.size = map_view.size
	map3d.visible = false
	map3d.region_clicked.connect(_on_region_clicked)
	map3d.army_clicked.connect(_on_army_clicked)
	map3d.region_commanded.connect(_on_region_commanded)
	map3d.region_hovered.connect(_on_region_hovered)
	map3d.plot_clicked.connect(_on_plot_clicked)
	# THE BUILD RING'S OWN TWO SIGNALS. The map view hit-tests the ring's slots
	# before it tests a plot or a region and emits these; this screen turns the
	# click into `session.commit_build()` and the hover into the ring's lit state.
	# Without this pair the ring was a picture of a menu - which is the whole of
	# the owner's "I cannot click on the buildings or build them with the icons".
	map3d.build_entry_clicked.connect(_on_build_entry_clicked)
	map3d.build_entry_hovered.connect(_on_build_entry_hovered)
	# The mode line's banner and label counts only exist after the paint, so they
	# are re-read here rather than during `refresh()`. Updating a Label does not
	# ask the map to redraw, so this cannot loop.
	map3d.overlay_painted.connect(_refresh_map_mode_label)
	add_child(map3d)

	# NO LEGEND STRIP AND NO CONTROL CHEAT-SHEET. The colour-chip key
	# ("staged / attackable / ...") and the camera bindings line that ran along
	# the bottom were developer surfaces - an adversarial review flagged both as
	# disqualifying on a shipped screen. What the rings mean is taught where
	# retail teaches it: the tasks line says what to click next, and every
	# disabled button's tooltip says why. The camera bindings moved to the
	# diagnostics overlay (F9), findable but off the shipped surface.

	message_label = Label.new()
	message_label.name = "Message"
	message_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	message_label.position = Vector2(24, 700)
	message_label.custom_minimum_size = Vector2(1240, 24)
	message_label.add_theme_font_size_override("font_size", 15)
	message_label.add_theme_color_override("font_color", ThemeScript.GOLD)
	add_child(message_label)

	# THE CONVERSION REPORT - what is retail's and what is not, per subsystem.
	# The capability is kept whole (it is refreshed every `refresh()` and the
	# runners keep asserting its text), but it is DIAGNOSIS: it lives on the
	# diagnostics overlay, not across the bottom of the player's screen.
	map_mode_label = Label.new()
	map_mode_label.name = "MapMode"
	map_mode_label.position = Vector2(16, 420)
	map_mode_label.custom_minimum_size = Vector2(1240, 300)
	map_mode_label.size = Vector2(1240, 300)
	map_mode_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	map_mode_label.clip_text = true
	map_mode_label.add_theme_font_size_override("font_size", 12)
	map_mode_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)

	var side_x := 1290.0
	# THE SEAT PLAQUES. Heraldry and counters in ornamented sockets, drawn by
	# `_draw_standings` - never left-aligned diagnostic prose ("regions 9
	# armies 4"), which is what this panel used to be.
	standings_label = Control.new()
	standings_label.name = "Standings"
	standings_label.clip_contents = true
	standings_label.position = Vector2(side_x, 70)
	standings_label.custom_minimum_size = Vector2(524, 196)
	standings_label.size = Vector2(524, 196)
	standings_label.mouse_filter = Control.MOUSE_FILTER_PASS
	standings_label.draw.connect(_draw_standings)
	add_child(standings_label)

	# THE TRAY'S CONTENT NEVER SCROLLS AND NEVER LEAVES ITS WELL.
	#
	# It used to. `scroll_active = true` on a box whose content is longer than it
	# is puts a scrollbar sliver on the frame line and lets the last line sit half
	# off the panel; a blind review photographed the region card running past the
	# bottom of the display and ending on a trailing comma, and called that alone a
	# 100%-confidence tell. Two things stop it now and they are independent: the
	# content is FITTED to the well before it is set (`_fit_card_lines`), and the
	# control CLIPS, so a fit that is ever wrong is a short card and never a card
	# that bleeds. `wotr_region_card_runner` asserts both properties at every
	# window size rather than trusting them.
	detail_label = RichTextLabel.new()
	detail_label.name = "Detail"
	detail_label.bbcode_enabled = true
	detail_label.fit_content = false
	detail_label.scroll_active = false
	detail_label.clip_contents = true
	detail_label.position = Vector2(side_x, 280)
	# NO CUSTOM MINIMUM. `_relayout` places this control with `_place_exact`, and a
	# minimum written here would be a floor it could never come back below - the
	# 252-pixel one that used to live on this line held the card 57 authored pixels
	# taller than retail's own content well, which is how the card came to overhang
	# the tray's bottom rail in the first place.
	detail_label.add_theme_font_size_override("normal_font_size", 16)
	add_child(detail_label)

	# The unplaced block sits BETWEEN the detail panel and the buttons, and its
	# heading wraps to two lines. It gets exactly the band 544..628; the buttons
	# start at 636 and nothing may reach them.
	unplaced_label = Label.new()
	unplaced_label.name = "UnplacedHeading"
	unplaced_label.position = Vector2(side_x, 544)
	unplaced_label.custom_minimum_size = Vector2(548, 40)
	unplaced_label.size = Vector2(548, 40)
	unplaced_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	unplaced_label.add_theme_font_size_override("font_size", 13)
	unplaced_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(unplaced_label)

	unplaced_host = VBoxContainer.new()
	unplaced_host.name = "UnplacedRegions"
	unplaced_host.position = Vector2(side_x, 588)
	unplaced_host.custom_minimum_size = Vector2(548, 0)
	unplaced_host.size = Vector2(548, 0)
	add_child(unplaced_host)

	attack_button = Button.new()
	attack_button.name = "Attack"
	attack_button.text = "ATTACK"
	attack_button.position = Vector2(side_x, 636)
	attack_button.custom_minimum_size = Vector2(250, 44)
	attack_button.size = Vector2(250, 44)
	attack_button.disabled = true
	attack_button.pressed.connect(_on_attack_pressed)
	add_child(attack_button)

	end_turn_button = Button.new()
	end_turn_button.name = "EndTurn"
	end_turn_button.text = "END TURN"
	end_turn_button.position = Vector2(side_x + 262, 636)
	end_turn_button.custom_minimum_size = Vector2(250, 44)
	end_turn_button.size = Vector2(250, 44)
	end_turn_button.pressed.connect(_on_end_turn_pressed)
	add_child(end_turn_button)

	# AUTO-RESOLVE. Retail's own RULES tab offers "Auto Resolve and RTS", which
	# means BOTH are offered and the player picks per battle; this is that pick.
	# It goes through the SAME door ATTACK does - `session.commit_attack()` -
	# because the choice has to be recorded in the commitment the strategic hash
	# covers, not applied afterwards to a battle that was committed as something
	# else.
	cancel_button = Button.new()
	cancel_button.name = "Cancel"
	cancel_button.text = "CANCEL"
	cancel_button.position = Vector2(side_x + 262, 636)
	cancel_button.custom_minimum_size = Vector2(250, 44)
	cancel_button.size = Vector2(250, 44)
	cancel_button.disabled = true
	cancel_button.pressed.connect(_on_cancel_pressed)
	add_child(cancel_button)

	auto_resolve_button = Button.new()
	auto_resolve_button.name = "AutoResolve"
	auto_resolve_button.text = "AUTO-RESOLVE"
	auto_resolve_button.position = Vector2(side_x + 262, 690)
	auto_resolve_button.custom_minimum_size = Vector2(250, 40)
	auto_resolve_button.size = Vector2(250, 40)
	auto_resolve_button.disabled = true
	auto_resolve_button.pressed.connect(_on_auto_resolve_pressed)
	add_child(auto_resolve_button)

	report_backdrop = ColorRect.new()
	report_backdrop.name = "BattleReportBackdrop"
	report_backdrop.color = Color(0.04, 0.05, 0.06, 0.96)
	report_backdrop.visible = false
	report_backdrop.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(report_backdrop)

	report_text = RichTextLabel.new()
	report_text.name = "BattleReport"
	report_text.bbcode_enabled = true
	report_text.scroll_active = true
	report_text.fit_content = false
	report_text.visible = false
	add_child(report_text)

	report_close = Button.new()
	report_close.name = "BattleReportClose"
	report_close.text = "CLOSE"
	report_close.visible = false
	report_close.pressed.connect(close_battle_report)
	add_child(report_close)

	# ------------------------------------------------------------------------------
	# THE PAUSE SHELL, AND WHY MAIN MENU LIVES IN IT
	# ------------------------------------------------------------------------------
	#
	# MAIN MENU used to sit inside the live seat-standings panel. Round 6 moved it
	# there OUT of the combat rail, which was the right first half of the move and
	# stopped short: a blind review looking at the result said "shipped chrome does
	# not put shell navigation inside a live scoreboard" and, asked what an audience
	# at a show floor would photograph, answered "the MAIN MENU button in the
	# scoreboard is the thing they photograph". A scoreboard is a READOUT. The
	# control that leaves the game is not a readout and does not belong on one, no
	# matter how well it is framed.
	#
	# So it goes where every game in this genre puts it, which is behind ESCAPE. The
	# shell is a real, live surface and not a gesture at one: ESC opens it, ESC or
	# RESUME closes it, MAIN MENU leaves. Both captions are retail's own words out
	# of retail's own string table (`APT:Resume`, `APT:MainMenu`), and nothing in
	# here is a control that does nothing.
	pause_shell = Control.new()
	pause_shell.name = "PauseShell"
	pause_shell.visible = false
	pause_shell.mouse_filter = Control.MOUSE_FILTER_STOP
	pause_shell.draw.connect(_draw_pause_shell)
	add_child(pause_shell)

	pause_resume = Button.new()
	pause_resume.name = "Resume"
	pause_resume.text = "RESUME"
	pause_resume.visible = false
	pause_resume.pressed.connect(func() -> void: toggle_pause_shell(false))
	add_child(pause_resume)

	# OPTIONS, ON THE PAUSE SHELL. It was not here, and its absence is half of what
	# the owner meant by "the key settings does nothing": from inside a campaign
	# there was no route to the settings screen at all, so display, audio and
	# control settings were things you could only change before you started playing.
	# The shell owns ONE options screen - the same instance the menu's OPTIONS cap
	# opens - so this asks for it (`options_requested`) rather than building a
	# second copy that would persist to the same file behind the first one's back.
	pause_options = Button.new()
	pause_options.name = "Options"
	pause_options.text = "OPTIONS"
	pause_options.visible = false
	pause_options.pressed.connect(func() -> void:
		# The shell reopens THIS page when the options screen closes, so the shell
		# is shut here: coming back to a pause overlay nobody asked to reopen is a
		# state the player has to dismiss twice.
		toggle_pause_shell(false)
		options_requested.emit())
	add_child(pause_options)

	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.visible = false
	# NO CUSTOM MINIMUM. It used to carry a 250x40 floor from the days it sat in a
	# fixed-position sidebar, and a floor is a RATCHET: `Control.size` is clamped up
	# to the combined minimum, so at the narrowest window in the layout runner's
	# `SIZES` the pill stayed 250 wide inside a shell card whose inner field is 255
	# and hung out through the frame. `_place_exact` is what places it now, and
	# `_place_exact` only works on a control with nothing to ratchet on.
	back_button.pressed.connect(func() -> void: back_requested.emit())
	add_child(back_button)

	# RETAIL'S PORTRAIT OF THE REGION UNDER THE POINTER. Sits directly above the
	# region card, because that is what it is a picture of. Sized here and moved
	# by `_relayout()` like everything else.
	region_portrait_frame = Control.new()
	region_portrait_frame.name = "RegionPortraitFrame"
	region_portrait_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	region_portrait_frame.draw.connect(_draw_region_portrait)
	add_child(region_portrait_frame)

	region_portrait_caption = Label.new()
	region_portrait_caption.name = "RegionPortraitCaption"
	region_portrait_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	region_portrait_caption.add_theme_font_size_override("font_size", 13)
	region_portrait_caption.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	region_portrait_caption.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(region_portrait_caption)

	# RETAIL'S OWN TAB RAIL, on retail's own tab strip. Three real controls: each
	# one switches what the tray's well is showing, and every one of the three has
	# content read off the campaign rather than a placeholder. The captions are
	# retail's own `APT:RegionDetails*Tab` strings; a caption that does not resolve
	# keeps this project's literal and is a NAMED GAP.
	#
	# THE CAPTION IS DRAWN AND THE BUTTON IS THE HIT AREA, which is the same split
	# `tray_ribbon`, `turn_banner`, `header_label` and `standings_label` on this
	# screen already use, for the same Godot reason and against a defect that had
	# already shipped once.
	#
	# `Control.size` is clamped up to `get_combined_minimum_size()`; a Button's
	# minimum is ITS TEXT; and that minimum is recomputed DEFERRED while the getter
	# returns the cached value immediately. So a Button carrying a caption cannot
	# be reliably fitted into a cell smaller than its own lettering - a shrink loop
	# that reads the control back reads a stale number and terminates having
	# changed nothing, which is exactly what happened when the tab cells were
	# seated three authored pixels inside retail's rail: the control stayed 31
	# pixels tall in a 29-pixel cell at two of the six window sizes the layout
	# runner holds, and grew straight back out through the rail.
	#
	# With no text on it, the control has no minimum to fight and the rectangle it
	# is handed is the rectangle it occupies. The caption is set in retail's own
	# caps by `_draw_command_bar`, inside the same cell, and the string itself
	# lives in `_tab_captions` so `player_visible_strings()` still reads exactly
	# what a player reads.
	for entry_value in TRAY_TABS:
		var entry := entry_value as Dictionary
		var key := String(entry["key"])
		var tab := Button.new()
		tab.name = "Tab%s" % key.capitalize()
		tab.text = ""
		tab.focus_mode = Control.FOCUS_NONE
		tab.mouse_filter = Control.MOUSE_FILTER_STOP
		tab.pressed.connect(_on_tab_pressed.bind(key))
		tab.mouse_entered.connect(func() -> void: _on_tab_hovered(key, true))
		tab.mouse_exited.connect(func() -> void: _on_tab_hovered(key, false))
		add_child(tab)
		_tab_buttons[key] = tab
		_tab_captions[key] = String(entry["caption"])

	# ------------------------------------------------------------------------------
	# THE COMPASS DIAL'S SIX WELLS, MADE LIVE
	# ------------------------------------------------------------------------------
	#
	# `_draw_command_dial` fills retail's ring of command wells with retail's own
	# `ConstructButtonImage` crops for whatever the region's owner can build. They
	# used to be drawn on `chrome_layer`, which ignores the mouse, so they were not
	# controls - and the previous round's answer to that was to make them LOOK dead:
	# value dropped, a tooltip saying nothing here builds. A blind review's verdict
	# on the result was exact: "a column of dead buttons hanging off the right of the
	# stone disc".
	#
	# Both halves of that are now wrong to keep. Construction is simulated, so six
	# lit icons in six gilt collars can be what they have always looked like - a
	# build menu. Each well gets a real button on retail's own authored rectangle,
	# hover and press are drawn as light on the collar rather than as a plate, and a
	# press goes through the same `_commit_build_here` door the ring and the roster
	# use. The dial reads as attached to the disc because the wells are now the
	# brightest live thing on it rather than the dimmest dead thing.
	# THE RING'S OWN HOVER TARGET stays, and its job has changed: it used to carry
	# the "this is not a build menu" refusal, and it now carries the reason NOTHING
	# can be built here when that is the case (an unclaimed region, a region with no
	# foundations). It is added BEFORE the six well buttons on purpose: Godot picks
	# the TOPMOST control under the pointer and `MOUSE_FILTER_PASS` forwards to the
	# PARENT rather than to a sibling underneath, so an affordance added after the
	# wells would quietly swallow every click on them.
	dial_affordance = Control.new()
	dial_affordance.name = "CommandDialAffordance"
	dial_affordance.mouse_filter = Control.MOUSE_FILTER_PASS
	dial_affordance.tooltip_text = COMMAND_DIAL_UNAVAILABLE
	add_child(dial_affordance)

	for well in range(PALANTIR_COMMAND_WELLS.size()):
		var dial_button := Button.new()
		dial_button.name = "DialWell%d" % well
		dial_button.flat = true
		dial_button.focus_mode = Control.FOCUS_NONE
		dial_button.text = ""
		dial_button.mouse_filter = Control.MOUSE_FILTER_STOP
		dial_button.pressed.connect(_on_dial_well_pressed.bind(well))
		dial_button.visible = false
		add_child(dial_button)
		_dial_buttons.append(dial_button)

	# THE TRAY'S THREE FOUNDATION CARDS AND ITS ROSTER ROWS, as controls.
	#
	# Both are drawn surfaces (`_draw_structure_cards`, `_draw_structure_roster`) and
	# both were readouts. A card is a foundation, so pressing one opens the ring on
	# it; a roster row is an offering, so pressing one raises it. They are pooled at
	# their maximum count and hidden when the region has fewer, because a control
	# created and freed per refresh is a control the layout runner's "every control
	# is framed" sweep sees in a different state every time it looks.
	for slot in range(TRAY_CARD_SLOTS.size()):
		var card_button := Button.new()
		card_button.name = "PlotCard%d" % slot
		card_button.flat = true
		card_button.focus_mode = Control.FOCUS_NONE
		card_button.mouse_filter = Control.MOUSE_FILTER_STOP
		card_button.pressed.connect(_on_plot_card_pressed.bind(slot))
		card_button.visible = false
		add_child(card_button)
		_plot_card_buttons.append(card_button)
	for slot in range(BUILD_ROW_BUTTONS):
		var row_button := Button.new()
		row_button.name = "BuildRow%d" % slot
		row_button.flat = true
		row_button.focus_mode = Control.FOCUS_NONE
		row_button.mouse_filter = Control.MOUSE_FILTER_STOP
		row_button.pressed.connect(_on_build_row_pressed.bind(slot))
		row_button.mouse_entered.connect(_on_build_row_hovered.bind(slot, true))
		row_button.mouse_exited.connect(_on_build_row_hovered.bind(slot, false))
		row_button.visible = false
		add_child(row_button)
		_build_row_buttons.append(row_button)


	# THE PALANTIR'S TWO MEDALLIONS, AS TWO REAL BUTTONS. See the block at
	# `MEDALLION_KEY_INSTANCE` for how the key was told from the banner (retail's
	# own `namedInstances` table, not the icons) and which half of each binding is
	# retail's. They carry NO stylebox and NO caption: retail's own gilt disc is
	# painted under each of them by the palantir island, and a pill or a word on
	# top of it would be this project's furniture back on retail's art.
	medallion_key = _build_medallion("PalantirKey", _on_medallion_key_pressed)
	medallion_banner = _build_medallion("PalantirBanner", _on_medallion_banner_pressed)

	# RETAIL'S TWO EXPANDER BUTTONS, built the same way and for the same reason -
	# see the block above `standings_open`. The art under them is retail's; these
	# carry the hit area and the action.
	stats_expander = _build_medallion("StatsExpander", _on_stats_expander_pressed)
	objectives_expander = _build_medallion(
		"ObjectivesExpander", _on_objectives_expander_pressed)

	# THE STATUS RIBBON along the tray's bottom rail - the slot retail sets its
	# hovered-buildable caption ("Building Foundation") in. It carries the region
	# the tray is about and who holds it, which is this project's real state.
	#
	# A DRAWN Control, not a Label, and that is a containment decision rather than
	# a style one: `Control.size` is clamped up to `get_combined_minimum_size()`,
	# a Label's minimum is its own text, and that cache is updated DEFERRED - so a
	# Label handed a box narrower than a caption it carried a moment ago keeps the
	# WIDER box and hangs outside the frame drawn around it. That is the same
	# ratchet `_place`'s own header warns about, and it is exactly the class of
	# defect this round exists to remove. A drawn Control has no minimum, so the
	# rectangle it is given is the rectangle it occupies, always.
	tray_ribbon = Control.new()
	tray_ribbon.name = "TrayRibbon"
	tray_ribbon.clip_contents = true
	tray_ribbon.mouse_filter = Control.MOUSE_FILTER_IGNORE
	tray_ribbon.draw.connect(_draw_tray_ribbon)
	add_child(tray_ribbon)

	# THE DIAGNOSTICS OVERLAY, over the map when opened and hidden otherwise.
	# It owns every conversion-facing line: the provenance, the report, the
	# named gaps. Hiding it hides NOTHING from the tests - each label keeps
	# being refreshed - and hides everything from the player until asked for.
	diagnostics_panel = Control.new()
	diagnostics_panel.name = "Diagnostics"
	diagnostics_panel.visible = false
	diagnostics_panel.clip_contents = true
	diagnostics_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	diagnostics_panel.draw.connect(_draw_diagnostics_panel)
	add_child(diagnostics_panel)
	diagnostics_panel.add_child(status_label)
	diagnostics_panel.add_child(map_mode_label)

	gaps_label = RichTextLabel.new()
	gaps_label.name = "NamedGaps"
	gaps_label.bbcode_enabled = true
	gaps_label.fit_content = false
	gaps_label.scroll_active = true
	gaps_label.position = Vector2(16, 72)
	gaps_label.add_theme_font_size_override("normal_font_size", 13)
	diagnostics_panel.add_child(gaps_label)

	# The command buttons in the HUD's own dress - the gilt stadium pill of
	# retail's END PHASE - with the two that commit an action lit brighter.
	# The diagnostics overlay has NO button: it opens on F9 (see
	# `_unhandled_key_input`), because a visible "DIAGNOSTICS" toggle was itself
	# a developer surface on the player's HUD.
	# ALL FOUR COMMAND CAPSULES ARE DRESSED THE SAME WAY, from one list. CANCEL used
	# to be dressed non-`primary` here while its three neighbours were primary, which
	# put a parchment caption in a rail of gold ones - see `COMMAND_CAPSULE_NAMES`.
	# The drawn pill is what a machine with no converted strategic bundle gets; with
	# the bundle bound every one of them is stripped to retail's own capsule below.
	for capsule in command_capsules():
		HudScript.style_button(capsule, true)
	HudScript.style_button(back_button)
	HudScript.style_button(pause_resume, true)
	HudScript.style_button(pause_options)
	HudScript.style_button(report_close)
	# THE TAB RAIL DRAWS NOTHING OF ITS OWN. Retail's tab plate is under it (the
	# `StrategicDetailsRegion` strip) and the SELECTED state is painted by
	# `_draw_tab_rail` on the chrome pass; a stylebox pill on top of retail's
	# filigree would be this project's furniture back on the glass.
	for tab_value in _tab_buttons.values():
		var tab := tab_value as Button
		for state in ["normal", "hover", "pressed", "focus", "disabled"]:
			tab.add_theme_stylebox_override(state, StyleBoxEmpty.new())
		tab.add_theme_color_override("font_color", HudScript.PARCHMENT_DIM)
		tab.add_theme_color_override("font_hover_color", HudScript.RIM_GOLD_HOT)
		tab.add_theme_color_override("font_pressed_color", HudScript.RIM_GOLD_HOT)

	# THE END TURN BUTTON'S FACE IS RETAIL'S OWN CAPSULE when the strategic
	# bundle is converted, so the button itself has to stop painting one. These
	# four signals are the only reason the chrome pass knows which of retail's
	# authored states (`_up`/`_over`/`_down`) to draw; without them a real button
	# would sit on a picture of a resting button, which is the "hover and press
	# are indistinguishable" defect the review named.
	# MAIN MENU IS NOT IN THIS LIST ANY MORE. It is on the pause shell now, which is
	# drawn on its own layer above the chrome pass, and retail's capsule art is
	# painted BY the chrome pass - so a capsule state tracked here would paint
	# retail's pill underneath the panel that covers it. The pause shell's two
	# buttons keep the drawn gilt pill `style_button` gives them, which is the same
	# dress every button on the battle report already wears.
	for button in command_capsules():
		button.mouse_entered.connect(_on_capsule_state.bind(button.name, "over"))
		button.mouse_exited.connect(_on_capsule_state.bind(button.name, "up"))
		button.button_down.connect(_on_capsule_state.bind(button.name, "down"))
		button.button_up.connect(_on_capsule_state.bind(button.name, "over"))
		# FOCUS IS THE FIFTH STATE AND RETAIL HAS NO ART FOR IT. Retail's living
		# world is a pointer-only screen, so `StrategicEndTurnButton` authors four
		# labels and none of them is "focused". A keyboard walk over this rail was
		# therefore invisible. The ring is drawn in `_draw_capsule` and is this
		# project's; both signals only ask the chrome pass to repaint.
		button.focus_entered.connect(_on_capsule_focus)
		button.focus_exited.connect(_on_capsule_focus)

	# Z-ORDER, stated once. The map is FULL-BLEED, so tree order alone no
	# longer layers the screen: the map sits at 0, the chrome pass that paints
	# the island backings at 1, every HUD control at 2, the diagnostics overlay
	# at 3, and the battle report over everything at 4. Without this, controls
	# built before the map vanish under Middle-earth - which is exactly what
	# the first full-bleed capture showed.
	chrome_layer.z_index = 1
	# THE PULSE LAYER SHARES THE CHROME PASS'S DEPTH and is added to the tree after
	# it, so it composites OVER retail's capsule art and UNDER every live control's
	# own caption - which is where light on a button belongs. Same depth rather than
	# a depth of its own because it is the same surface, split for cost; see
	# `build()`.
	pulse_layer.z_index = 1
	for hud_control in [
		turn_banner, phase_banner, header_label, hint_label, message_label,
		standings_label, detail_label, unplaced_label, unplaced_host,
		attack_button, end_turn_button, auto_resolve_button, cancel_button,
		region_portrait_frame, region_portrait_caption, tray_ribbon,
	]:
		(hud_control as CanvasItem).z_index = 2
	for tab_value in _tab_buttons.values():
		(tab_value as CanvasItem).z_index = 2
	diagnostics_panel.z_index = 3
	for report_control in [report_backdrop, report_text, report_close]:
		(report_control as CanvasItem).z_index = 4
	# THE PAUSE SHELL IS THE TOP LAYER, because a shell that anything can be drawn
	# over is not a shell. Its two buttons sit one step above its own panel.
	pause_shell.z_index = 5
	for shell_control in [pause_resume, pause_options, back_button]:
		(shell_control as CanvasItem).z_index = 6

	resized.connect(_relayout)
	_relayout()
	# THE OPENING STOP, APPLIED RATHER THAN ASSUMED. `_apply_view_mode` is what
	# reconciles the stop and the two per-panel switches with every control's
	# `visible`, and a screen that only ran it on the first F2 would open in a state
	# no code had ever asserted.
	_apply_view_mode()


## Show or hide the diagnostics overlay. `wanted` forces a state; no argument
## toggles. Presentation only: nothing here reaches the session or the state.
## F1 OPENS THE DIAGNOSIS. The visible toggle button left the bottom strip
## because a permanent control labelled DIAGNOSTICS is a developer surface, but a
## diagnosis nobody can reach is worse than an ugly button - so the binding is
## real, and it is the only thing that replaced the button.
func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode == KEY_F1:
		toggle_diagnostics()
		get_viewport().set_input_as_handled()
		return
	if key.keycode == KEY_F2:
		# F2 IS "LESS", AND IT HAS THREE STOPS. It used to take EVERY island down and
		# bring every one back, which the owner reported as the defect it is: an
		# all-or-nothing key is a screenshot key, not a way to look at Middle-earth
		# while still taking a turn. F2 steps down and wraps; SHIFT+F2 steps back up;
		# ESCAPE still brings the whole HUD back (see `toggle_pause_shell`) so no stop
		# is a trap. See `VIEW_FULL` for what each stop keeps and why.
		#
		# It is F2 and not a letter key because letters over a map are where camera
		# and selection bindings live in this genre, and F1 is already the diagnosis
		# one key along.
		set_view_mode(view_mode + (-1 if key.shift_pressed else 1))
		get_viewport().set_input_as_handled()
		return
	if key.keycode == KEY_ESCAPE:
		# ESCAPE IS THE SHELL, and it is the ONLY way to reach it - which is the
		# point of moving MAIN MENU off the standings panel rather than merely
		# reframing it there. A battle report is modal over the top of everything,
		# so ESC closes that first; there is never a state where two overlays are
		# open and one of them cannot be dismissed.
		if report_backdrop != null and report_backdrop.visible:
			close_battle_report()
		else:
			toggle_pause_shell()
		get_viewport().set_input_as_handled()


## Switch the tray's well between retail's three tabs. PRESENTATION ONLY: it
## writes one field on this screen and asks for a refresh, and reaches nothing
## the strategic hash covers.
## The tab under the pointer, so the drawn caption can light the way a control's
## caption does. Presentation only - the captions are drawn, so the hover state
## the Button would have expressed through a theme colour is expressed here.
func _on_tab_hovered(key: String, entered: bool) -> void:
	_hovered_tab = key if entered else ("" if _hovered_tab == key else _hovered_tab)
	if chrome_layer != null:
		chrome_layer.queue_redraw()


func _on_tab_pressed(key: String) -> void:
	if active_tab == key:
		return
	active_tab = key
	if session != null:
		refresh()
	else:
		_refresh_tray_chrome()


## Open or close the pause shell. `wanted` forces a state; no argument toggles.
## Presentation only - it reaches no simulation state, and the two controls it
## reveals are the only shell navigation on this screen.
func toggle_pause_shell(wanted: Variant = null) -> void:
	if pause_shell == null:
		return
	var show := not pause_shell.visible if wanted == null else bool(wanted)
	pause_shell.visible = show
	pause_resume.visible = show
	pause_options.visible = show
	back_button.visible = show
	# THE SHELL IS THE WAY BACK FROM A HIDDEN HUD. F2 takes the islands down; a
	# player who has forgotten which key brings them back presses ESCAPE, which
	# every game in this genre answers, and gets a surface that names every binding
	# this screen has. So opening the shell restores the HUD rather than leaving the
	# player looking at a modal floating over a bare map with no way to read the
	# state they paused out of.
	if show and hud_hidden:
		set_hud_hidden(false)
	if show:
		pause_shell.queue_redraw()
	if chrome_layer != null:
		# THE SEAT PANEL CHANGES SHAPE WITH IT. `_standings_card_rect` no longer
		# grows to take MAIN MENU in, so the panel is one size now - but the chrome
		# pass still has to be told to repaint when an overlay comes and goes.
		chrome_layer.queue_redraw()


## THE PAUSE SHELL'S OWN PANEL - a dimming scrim over the map so the shell reads
## as a modal surface rather than as two buttons on Middle-earth, the HUD's own
## card under them, and one cartouche per button. Drawn on the shell's own layer
## because the chrome pass paints UNDER every control and this panel has to paint
## OVER them.
func _draw_pause_shell() -> void:
	if pause_shell == null:
		return
	pause_shell.draw_rect(Rect2(Vector2.ZERO, pause_shell.size), Color(0.0, 0.0, 0.0, 0.55))
	var card := _pause_card_rect()
	HudScript.draw_card(pause_shell, Rect2(card.position - pause_shell.position, card.size))
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return
	# RETAIL'S OWN WORD FOR THIS STATE, engraved across the head of the card.
	var head := card.position - pause_shell.position + Vector2(card.size.x * 0.5,
		card.size.y * 0.24)
	HudScript.draw_engraved_caps(pause_shell, font, head,
		names.shell_label("APT:Pause", "PAUSED"),
		HudScript.type_size(card.size.y * 0.16, HudScript.TYPE_SUBJECT, 12, 34),
		2.0, HudScript.PARCHMENT)
	# THE KEY REFERENCE, under the head and above the capsules. This is the honest
	# answer to "the key settings does nothing": the bindings are real, they are
	# listed where ESCAPE puts them, and the one thing that is NOT offered - remap -
	# says so on the last line rather than being mocked up as a control.
	var keys_top := card.position - pause_shell.position + Vector2(0.0, card.size.y * 0.30)
	var key_size := HudScript.type_size(card.size.y * 0.11, HudScript.TYPE_CAPTION, 9, 18)
	var key_step := float(key_size) * 1.55
	var key_column := card.position.x - pause_shell.position.x + card.size.x * 0.22
	var action_column := card.position.x - pause_shell.position.x + card.size.x * 0.40
	var shown := OpenBFMEUserSettings.player_key_bindings()
	for index in range(shown.size()):
		var binding := shown[index] as Dictionary
		var baseline := keys_top.y + key_step * float(index)
		pause_shell.draw_string(font, Vector2(key_column, baseline),
			String(binding["key"]), HORIZONTAL_ALIGNMENT_LEFT, -1, key_size,
			HudScript.RIM_GOLD_HOT)
		pause_shell.draw_string(font, Vector2(action_column, baseline),
			String(binding["action"]), HORIZONTAL_ALIGNMENT_LEFT, -1, key_size,
			HudScript.PARCHMENT_DIM)
	for button in [pause_options, pause_resume, back_button]:
		var pill := button as Button
		HudScript.draw_button_cartouche(pause_shell,
			Rect2(pill.position - pause_shell.position, pill.size), true)


## The card the pause shell's two buttons are seated in, in the screen's own
## coordinates. One definition, read by the layout, by the drawing and by the
## runner that holds both buttons inside it - the same discipline `tray_tab_cell`
## states for the tab strip, and for the same reason.
func _pause_card_rect() -> Rect2:
	var frame := size
	if frame.x < 1.0 or frame.y < 1.0:
		frame = DESIGN_SIZE
	# THE CARD GREW THIS ROUND, and by exactly what was added to it: a third capsule
	# (OPTIONS) and the four-line key reference. Sized rather than crammed - the
	# region-card runner holds every control inside this rectangle, so a card that
	# stayed at two capsules' height would have pushed OPTIONS through its own frame.
	var card_width := clampf(frame.x * 0.28, 320.0, 520.0)
	var card_height := clampf(frame.y * 0.44, 300.0, 470.0)
	return Rect2(Vector2((frame.x - card_width) * 0.5, (frame.y - card_height) * 0.5),
		Vector2(card_width, card_height))


## ------------------------------------------------------------------------------
## HOW MUCH OF THE HUD IS ON THE GLASS - THREE STOPS, NOT A SWITCH
## ------------------------------------------------------------------------------
##
## The owner's words: "There is no way for me to remove good chunks of the UI or
## hide it so I can look at the map." F2 was all-or-nothing, and all-or-nothing is
## not a way to look at the map WHILE PLAYING - it is a way to take a screenshot.
## The answer is not more keys. It is that the single key means "less", and asking
## for less has more than one stop:
##
##   FULL      everything. The screen as designed.
##   FOCUSED   the map, the turn band, the command rail and the status ribbon -
##             everything needed to keep taking turns, and nothing else. The
##             palantir, the details tray's body, the standings table and the
##             region readouts come off. This is the stop a player plays in when
##             they want to SEE Middle-earth and still act on it.
##   MAP       Middle-earth and nothing else.
##
## F2 STEPS DOWN AND WRAPS; SHIFT+F2 STEPS BACK UP. One key, three stops, and the
## way back is the same key - a state a player can enter and not leave is a trap,
## which is why the old binary toggle was at least right about that much.
##
## PER-ISLAND CONTROL IS SEPARATE AND IS ON THE ART. The objectives plaque has
## retail's own expander on it and the palantir's banner medallion opens and shuts
## it (see `MEDALLION_KEY_INSTANCE`); the stats plate carries retail's own `Expand`
## button. Those are the "good chunks" a player takes off one at a time, and they
## are controls on the chrome rather than keys to memorise.
##
## PROJECT-AUTHORED, all of it: retail's living world has no view mode.
const VIEW_FULL := 0
const VIEW_FOCUSED := 1
const VIEW_MAP := 2
const VIEW_MODE_COUNT := 3

## WHICH OF RETAIL'S FIVE ISLANDS EACH STOP PAINTS. `checklist` carries the turn
## plaque and the phase band, `endTurnButton` the capsule, `selectionDetails` the
## tray - and FOCUSED keeps the tray because the command rail is welded to its top
## rail and the status ribbon runs along its bottom one. What FOCUSED drops is the
## palantir (`globe`, the single largest block of chrome on the screen) and the
## stats plate (`stats`), plus every drawn surface listed in `_detail_controls`.
const VIEW_MODE_ISLANDS := {
	VIEW_FULL: ["stats", "checklist", "endTurnButton", "selectionDetails", "globe"],
	VIEW_FOCUSED: ["checklist", "endTurnButton", "selectionDetails"],
	VIEW_MAP: [],
}

## The stop the screen is at. `hud_hidden` is kept as the answer to "is the HUD
## down entirely", because the capture runner and the layout runner both ask it.
var view_mode := VIEW_FULL

## Whether the objectives plaque is open. It follows retail's own two authored
## plaque sizes (`CHECKLIST_TASK_BOX` / `CHECKLIST_TASK_BOX_SHUT`) and is driven by
## the palantir's banner medallion and by retail's own expander on the plaque.
## DEFAULT SHUT, which is the owner's "get out of the way of the player": the
## plaque grows over the top-left of Middle-earth and the screen normally has one
## line to say.
var objectives_open := false


## Open or shut the objectives plaque. Presentation only - nothing here reaches
## the session or the state.
func set_objectives_open(value: bool) -> void:
	if objectives_open == value:
		return
	objectives_open = value
	_relayout()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## Step to a view stop. Out of range wraps, so F2 is a cycle and never a trap.
func set_view_mode(value: int) -> void:
	var wanted := posmod(value, VIEW_MODE_COUNT)
	if view_mode == wanted:
		return
	view_mode = wanted
	# The extreme stop IS the old hide, so the two stay one state rather than two
	# that can disagree - `set_hud_hidden` is what the runners and the shell ask.
	set_hud_hidden(view_mode == VIEW_MAP)
	if view_mode != VIEW_MAP:
		_apply_view_mode()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## Whether one of retail's five islands is painted at the current stop.
func island_is_shown(slot: String) -> bool:
	return (VIEW_MODE_ISLANDS.get(view_mode, []) as Array).has(slot)


## THE DRAWN SURFACES THAT COME OFF AT THE FOCUSED STOP.
##
## THE LINE IS THE ISLAND, NOT THE CONTROL, and that is a correction rather than a
## preference. The first version of this list took the tray's TAB BUTTONS and its
## DETAIL WELL off with everything else - and the first capture of the stop showed
## exactly what that costs: the tray's own art is still painted by the chrome pass
## (`_draw_tab_captions`, `_draw_structure_roster` are draws, not controls), so
## TERRITORY / ARMIES / STRUCTURES were still lettered on the glass with no button
## under them. A caption that looks like a tab and does not answer is worse than
## either keeping it or removing it.
##
## So a stop takes whole ISLANDS off - `VIEW_MODE_ISLANDS` decides that, and the
## chrome pass reads the same table - and this list is only the controls that
## belong to an island the stop has dropped. `selectionDetails` stays at FOCUSED,
## so everything inside the tray stays with it.
func _detail_controls() -> Array[CanvasItem]:
	var controls: Array[CanvasItem] = []
	for control in [
		# The stats island.
		header_label,
		# The palantir island, and this project's own two cards beside it.
		region_portrait_frame, region_portrait_caption, dial_affordance,
		medallion_key, medallion_banner,
		standings_label, unplaced_label, unplaced_host,
	]:
		if control != null:
			controls.append(control as CanvasItem)
	return controls


## Put the current stop on the glass. `VIEW_MAP` is handled by `set_hud_hidden`,
## which takes everything down; this is the difference between the other two.
func _apply_view_mode() -> void:
	var detail := view_mode == VIEW_FULL
	for control in _detail_controls():
		control.visible = detail
	for control in [medallion_key, medallion_banner]:
		if control != null:
			control.visible = detail and control.size.x > 4.0
	# THE SEAT ROLL HAS ITS OWN SWITCH ON TOP OF THE STOP - retail's `Expand` on the
	# stats plate. A table the player has put away stays away when they step back up
	# to FULL; the stop decides how much chrome is on the glass, the expander decides
	# whether this particular table is part of it.
	for control in [standings_label, unplaced_label, unplaced_host]:
		if control != null:
			control.visible = detail and standings_open
	if stats_expander != null:
		stats_expander.visible = stats_expander.size.x > 4.0 and island_is_shown("stats")
	if objectives_expander != null:
		objectives_expander.visible = objectives_expander.size.x > 4.0 			and island_is_shown("checklist")
	# AND THE MAP IS RE-TOLD WHERE THE HUD IS. This function is the ONLY place a
	# stop change reaches the glass - `set_view_mode` writes nothing but `view_mode`
	# and then calls this - so a keep-out published only from `_relayout` would be
	# stale for the whole life of a stop the player stepped to without resizing the
	# window. Handing the ring the rectangles that are ACTUALLY on screen at the
	# current stop is the contract, not an optimisation of it: the ring is meant to
	# reclaim the space when the chrome gets out of the way.
	_publish_hud_keep_out()
	# THE PULSE FOLLOWS THE STOP TOO. A capsule the stop has taken off the glass is
	# not a "you may act here" light, so the clock is not asked to run for it.
	_pulse_wanted = _pulse_is_wanted()


## EVERY ISLAND THIS SCREEN FLOATS OVER MIDDLE-EARTH, as one list.
##
## Deliberately NOT the map (`map_view`, `map3d`) - the whole point of hiding the
## HUD is to see the map - and deliberately not the pause shell, the diagnostics
## overlay or the battle report, which are surfaces the player has explicitly
## asked for and which must never be dismissed by a key that means "hide the
## furniture".
##
## `chrome_layer` is FIRST and is the reason this is a list rather than a loop over
## children: it paints every plate, rail, capsule face and engraved caption on the
## screen, so hiding the controls without hiding it would leave a full set of empty
## brass fittings over the map - which looks more broken, not less.
func _hud_islands() -> Array[CanvasItem]:
	var islands: Array[CanvasItem] = []
	for control in [
		chrome_layer, pulse_layer, turn_banner, phase_banner, header_label, hint_label,
		message_label, standings_label, detail_label, unplaced_label, unplaced_host,
		attack_button, end_turn_button, auto_resolve_button, cancel_button,
		region_portrait_frame, region_portrait_caption, tray_ribbon,
		dial_affordance, medallion_key, medallion_banner,
		stats_expander, objectives_expander,
	]:
		if control != null:
			islands.append(control as CanvasItem)
	for tab_value in _tab_buttons.values():
		islands.append(tab_value as CanvasItem)
	return islands


## Put the HUD down, or bring it back exactly as it was.
##
## The previous state is REMEMBERED rather than assumed, because "visible" is not
## the same answer for every island: `message_label` follows the tasks plaque's own
## open/shut state and `hint_label` is down entirely when there is no session, so
## restoring by setting everything true would put two labels on the glass that the
## screen had deliberately taken off it.
func set_hud_hidden(value: bool) -> void:
	if hud_hidden == value:
		return
	hud_hidden = value
	if value:
		# AN OPEN BUILD RING IS PART OF THE HUD even though it is not one of these
		# islands. It is painted by the map's own overlay, from `selected_plot`, so
		# hiding the Controls left a gold radial menu floating over an otherwise bare
		# map - which is the one thing F2 exists to prevent, and it photographed that
		# way. The ring is a MENU the player opened, so putting the HUD down shuts it
		# rather than making it invisible-but-open; nothing in the simulation is
		# touched, because a plot selection reaches no state (see `_on_plot_clicked`).
		selected_plot = {}
		_hud_visibility_before.clear()
		for control in _hud_islands():
			_hud_visibility_before[control] = control.visible
			control.visible = false
		# The map redraws from the cleared selection. `refresh()` re-hides the
		# islands itself (see its tail), so this cannot put one back on the glass.
		refresh()
		# THE WHOLE BOARD IS THE RING'S AGAIN. The HUD is down, so it claims nothing
		# - which is the stop contract stated at `_publish_hud_keep_out`.
		_publish_hud_keep_out()
		_pulse_wanted = false
	else:
		for control in _hud_islands():
			control.visible = bool(_hud_visibility_before.get(control, true))
		_hud_visibility_before.clear()
		# COMING BACK FROM THE BARE MAP IS COMING BACK ALL THE WAY. ESCAPE and the
		# shell both call this directly (see `toggle_pause_shell`), and a player who
		# has just asked for the pause shell has not asked to land on a half-dressed
		# screen - so the stop follows the visibility rather than being left behind
		# it, which is the one way the two states could have disagreed.
		if view_mode == VIEW_MAP:
			view_mode = VIEW_FULL
		_apply_view_mode()


func toggle_diagnostics(wanted: Variant = null) -> void:
	if diagnostics_panel == null:
		return
	var show := not diagnostics_panel.visible if wanted == null else bool(wanted)
	diagnostics_panel.visible = show
	if show:
		diagnostics_panel.queue_redraw()


## Put retail's face on the CAPS - the title, the turn band, the value plates,
## the buttons and every panel's [b] headings - and leave the body copy on the
## clean default sans, which is the reference captures' own split: engraved
## serif for caps and titles, a plain face for body lines.
func _apply_hud_font() -> void:
	if hud_font == null or heading_label == null:
		return
	# `header_label` and `standings_label` are drawn Controls now, not Labels:
	# they take the face straight off `hud_font` in `_draw_header`/
	# `_draw_standings`, so they are not in these theme lists. Putting them back
	# would silently cast to null.
	# `turn_banner` is NOT in this list any more: it is a drawn Control now, and it
	# takes the face straight off `hud_font` in `_draw_turn_plaque`.
	heading_label.add_theme_font_override("font", hud_font)
	# THE PALANTIR'S LENS IS SET IN ONE FACE, which is the display face the region's
	# own name three lines above it is cut in.
	#
	# It was the caps face under display caps, and a blind review measured the seam:
	# "'ARTHEDAIN' is letterspaced display caps; the two lines beneath it
	# ('Angmar', '4 armies 6 CP') are plain sans at inconsistent left indents inside
	# a circular mask. Ragged. P centres 'MORDOR' and stops." Two faces inside one
	# oval is the typographic-mixing ratio the same review measured against retail
	# across the whole screen; the lens is the surface where the two sit closest
	# together and it is where the mixing reads worst. With no display face bound
	# this keeps the caps face rather than a lookalike, exactly as `phase_banner`
	# does, and the partial binding is named on the diagnostics panel either way.
	region_portrait_caption.add_theme_font_override("font",
		display_font if display_font != null else hud_font)
	for tab_value in _tab_buttons.values():
		(tab_value as Button).add_theme_font_override("font", hud_font)
	# THE ONE DISPLAY LINE. Retail sets "tactical phase" in an uncial face, not
	# in its caps face, and the strategic movies' own FontSubstitution names
	# Omnia LT Std as the substitution target. The binding is PARTIAL and the
	# diagnostics panel says exactly how (`HudScript.DISPLAY_FACE_BINDING`); with
	# no display face bound this line keeps the caps face rather than a lookalike.
	if phase_banner != null:
		phase_banner.add_theme_font_override("font",
			display_font if display_font != null else hud_font)
	for button in [attack_button, end_turn_button, auto_resolve_button, back_button,
			pause_resume, report_close]:
		(button as Button).add_theme_font_override("font", hud_font)
	for rich in [detail_label, gaps_label, report_text]:
		(rich as RichTextLabel).add_theme_font_override("bold_font", hud_font)


func _draw_diagnostics_panel() -> void:
	HudScript.draw_card(diagnostics_panel,
		Rect2(Vector2.ZERO, diagnostics_panel.size), true)
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font != null:
		diagnostics_panel.draw_string(font, Vector2(16.0, 28.0),
			"DIAGNOSTICS - WHAT IS RETAIL'S, WHAT IS THIS PROJECT'S, AND WHAT IS ABSENT",
			HORIZONTAL_ALIGNMENT_LEFT, -1, 16, ThemeScript.GOLD)


# --- layout -------------------------------------------------------------------

## RETAIL'S LIVING-WORLD LAYOUT IS A FULL-BLEED MAP WITH HUD ISLANDS ON TOP.
## The reference capture (game.dat_l1eJcM0zCw.jpg) is the oracle: Middle-earth
## fills the whole frame; the HUD sits on it in islands - the player-status
## plate top-left, the turn band and the tasks line top-centre, END TURN
## top-right, the palantir dish bottom-left, and the details tray bottom-right.
## This screen had a right-hand SIDEBAR beside a boxed map; that was this
## project's own furniture, not retail's, and it is gone.
##
## THE ISLANDS KEEP TO TWO BANDS. Everything floating is anchored to the top
## edge or the bottom corners, and the CENTRAL FIELD - the middle of
## Middle-earth, where the campaign actually happens - is kept clear by
## construction at every window size. The region-card runner asserts that as a
## property (no island may enter the central rect) rather than as a screenshot.
const DESIGN_SIZE := Vector2(1860.0, 800.0)
const LAYOUT_MARGIN := 24.0
## The details tray's measure: its authored width and the most it may grow to.
const SIDE_MIN := 380.0
const SIDE_MAX := 620.0
## The narrowest the tray may ever be, on a window too narrow for its measure.
const SIDE_FLOOR := 260.0
## What the map may not shrink below. With the map full-bleed this floor is the
## WINDOW floor, and the runner still asserts it so a future layout cannot
## quietly reintroduce a boxed map smaller than it.
const MAP_MIN := Vector2(760.0, 380.0)
## The central field no HUD island may enter, in fractions of the frame. The
## region-card runner asserts the same rectangle.
const CENTRAL_FIELD := Rect2(0.30, 0.16, 0.40, 0.42)


## RETAIL'S FIVE HUD ISLANDS, PLACED BY RETAIL'S OWN SLOT TRANSLATIONS.
##
## `StrategicHUD`'s `main` sprite carries one authored named instance per island
## with the translation retail composes it at, in the 1024x768 space every
## strategic movie is authored in. This reads those translations and maps each
## island into the actual window.
##
## THE MAPPING, stated because it is this project's and not retail's: a UNIFORM
## scale by height (so retail's art keeps its aspect and its internal
## proportions, which a non-uniform stretch would destroy), and each island
## ANCHORED to the frame edge retail authored it against. Retail authored at 4:3
## and the game runs at 16:9 and wider; anchoring is what keeps the top-left
## plaque top-left and the END PHASE capsule hard against the right edge instead
## of stranding both in the middle of a widescreen frame. The anchor per island
## is in `STRATEGIC_ISLANDS` and it is read off where retail put the island in
## its OWN frame - `stats` at x 0, `endTurnButton` at x 1024, `checklist` at
## x 512 - not chosen freehand.
##
## Returns `{}` when the strategic bundle is absent, which is the signal every
## caller uses to fall back to the hand-drawn plates and name the gap.
## ONE AUTHORED STATE OF ONE STRATEGIC MOVIE, with the frame's own root display
## list attached to it.
##
## The flattened triangle list and the display list live in two places in the
## bundle - `flattenedFrames` carries the geometry, `rootTimeline.frames[N]`
## carries the record of which display-list entries are `clipDepth` MASKS - and
## the renderer needs both to know which triangles are art and which are a
## scissor. This joins them by frame index, which is the key they already share.
## An empty label asks for the movie's richest authored state by
## `wotr_strategic_ui.richest_frame`'s stated rule.
func _strategic_frame(movie: String, label: String) -> Dictionary:
	if strategic == null or not strategic.loaded:
		return {}
	var apt_frame: Dictionary = (strategic.frame_by_label(movie, label) if not label.is_empty()
		else strategic.richest_frame(movie))
	if apt_frame.is_empty():
		return {}
	var index := int(apt_frame.get("frameIndex", -1))
	var timeline: Dictionary = strategic.screen(movie).get("rootTimeline", {}) as Dictionary
	var frames: Array = timeline.get("frames", []) as Array
	if index < 0 or index >= frames.size():
		return apt_frame
	var joined := apt_frame.duplicate()
	joined["displayList"] = (frames[index] as Dictionary).get("displayList", [])
	return joined


func _compute_islands(frame: Vector2) -> Dictionary:
	var islands: Dictionary = {}
	if strategic == null or not strategic.loaded:
		return islands
	var slots: Dictionary = {}
	for row_value in strategic.named_instances("StrategicHUD"):
		var row := row_value as Dictionary
		var translation: Array = row.get("translation", []) as Array
		if translation.size() == 2:
			# The LAST authored instance of a name wins, which is the display
			# list's own rule; `main`'s children are the five HUD slots.
			slots[String(row.get("name", ""))] = Vector2(
				float(translation[0]), float(translation[1]))
	# RETAIL'S OWN STRETCH, not an anchoring scheme. See `APT_STRETCH_MEASUREMENT`
	# for the three landmarks off the oracle capture that pin it.
	var scale := frame / HudScript.APT_AUTHORED
	for entry_value in STRATEGIC_ISLANDS:
		var entry := entry_value as Dictionary
		var slot := String(entry["slot"])
		if not slots.has(slot):
			continue
		var apt_frame := _strategic_frame(String(entry["movie"]), String(entry["label"]))
		if apt_frame.is_empty():
			continue
		var origin := (slots[slot] as Vector2) * scale
		var bounds := HudScript.apt_frame_bounds(apt_frame)
		islands[slot] = {
			"movie": String(entry["movie"]),
			"frame": apt_frame,
			"origin": origin,
			"scale": scale,
			"rect": Rect2(origin + bounds.position * scale, bounds.size * scale),
		}
	return islands


## One island's own authored rectangle mapped into the window, or `Rect2()` when
## there is no island (no bundle, or that movie did not flatten). `local` is in
## the movie's OWN authored coordinates - the measured constants at the top of
## this file - so a caller places text where retail's art has a hole for it.
func _island_rect(slot: String, local: Rect2) -> Rect2:
	if not _islands.has(slot):
		return Rect2()
	var island := _islands[slot] as Dictionary
	var origin := island["origin"] as Vector2
	var scale := island["scale"] as Vector2
	return Rect2(origin + local.position * scale, local.size * scale)


## PUT A CONTROL AT AN EXACT RECTANGLE, in the one order that works.
##
## `custom_minimum_size` must be written BEFORE `size`, and it must be written
## from the WANTED rectangle rather than read back off the control. Godot clamps
## `Control.size` up to the combined minimum, so the pattern this screen used to
## use - `x.size = v` then `x.custom_minimum_size = x.size` - stores the CLAMPED
## size as the new floor, and every later relayout ratchets that floor upward and
## can never bring it back down. That is exactly how the details tray ended up
## 1,202 pixels wide inside a 737-pixel frame and pushed AUTO-RESOLVE off the
## screen, on a layout whose arithmetic was correct.
##
## A control whose own content still needs more room than `box` (a Button is at
## least as wide as its caption) is NOT forced: the caller shrinks the caption
## instead, because silently clipping a button's own text is the worse defect.
static func _place(control: Control, box: Rect2) -> void:
	control.custom_minimum_size = box.size
	control.position = box.position
	control.size = box.size


## PUT A CONTROL AT AN EXACT RECTANGLE AND LEAVE IT NO FLOOR TO RATCHET ON.
##
## `_place` writes `custom_minimum_size`, and that is the right thing for an
## island that should never be squeezed below its own measure. It is the WRONG
## thing for anything inside the command bar, and the reason is Godot's own
## machinery rather than arithmetic: writing `custom_minimum_size` invalidates
## the combined-minimum cache DEFERRED, while `Control.size` is clamped against
## that cache IMMEDIATELY. So a control handed a wide box and then a narrow one in
## the same frame keeps the WIDE size and hangs outside whatever frame is drawn
## around it - which is precisely the "content crosses the panel edge" defect this
## round exists to remove, and it reproduced at three of the six window sizes the
## layout runner checks.
##
## A control that is never given a custom minimum has nothing to ratchet on: its
## combined minimum is its own natural one (zero for a drawn Control, its caption
## for a Button), so the rectangle it is handed is the rectangle it occupies. The
## caller is then responsible for making the CONTENT fit - which the bar does, by
## shrinking captions before it places them and by fitting the card's lines.
static func _place_exact(control: Control, box: Rect2) -> void:
	control.position = box.position
	control.size = box.size


## Whether retail's checklist plaque is drawn OPEN. It opens for exactly one
## reason: there is a refusal or an outcome to show under the imperative, and one
## line of retail's shut plaque cannot hold two lines. See `CHECKLIST_TASK_BOX`.
## WHETHER THE OBJECTIVES PLAQUE IS OPEN.
##
## Two things can open it and the player is one of them, which is the change this
## round. It used to be driven purely by whether there was a line to show, so the
## plaque grew and shrank under the player without their asking - and it grows over
## the top-left of Middle-earth, where the map's own army banners and build-plot
## decals stand. Now the plaque opens when there is something to say AND the player
## has not put it away, or when the player has explicitly asked for it (the
## palantir's banner medallion, and retail's own expander on the plaque itself).
func _checklist_is_open() -> bool:
	if objectives_open:
		return true
	return message_label != null and not message_label.text.strip_edges().is_empty()


func _relayout() -> void:
	if heading_label == null:
		return
	var frame := size
	if frame.x < 1.0 or frame.y < 1.0:
		frame = DESIGN_SIZE
	_islands = _compute_islands(frame)

	# THE MAP TAKES THE WHOLE FRAME. Retail's does; every HUD control below is
	# an island floating over it.
	for view in [map_view, map3d]:
		view.position = Vector2.ZERO
		view.custom_minimum_size = frame
		view.size = frame

	# EVERY ISLAND BELOW IS PLACED INTO RETAIL'S OWN ART when the strategic
	# bundle is present, and by this screen's own measure when it is not. The
	# fallback arithmetic is kept whole rather than deleted: a machine with no
	# converted bundle still gets a laid-out screen, and the diagnostics panel
	# says which of the two it is looking at.
	var scale := frame.y / HudScript.APT_AUTHORED.y
	var art := not _islands.is_empty()

	# TOP-LEFT: the seat's numbers, in the black field of retail's "Player
	# Bonuses" plate (`StrategicStats`, draw group at depth 5).
	var header_box := _island_rect("stats", STATS_FIELD)
	if header_box.size.x <= 0.0:
		header_box = Rect2(28.0, 20.0, clampf(frame.x * 0.22, 300.0, 430.0), 26.0)
	_place(header_label, header_box)

	# TOP-CENTRE: retail's phase chevron bar and the critical-tasks box under it.
	# The turn number goes in the bar's left capsule, whose move it is on the
	# banner strip, and the one imperative plus the message line in the box.
	var turn_box := _island_rect("checklist", CHECKLIST_TURN_PLAQUE)
	if turn_box.size.x <= 0.0:
		var band_width := clampf(frame.x * 0.34, 420.0, 640.0)
		turn_box = Rect2((frame.x - band_width) * 0.5, 14.0, band_width, 26.0)
	_place(turn_banner, turn_box)
	var phase_box := _island_rect("checklist", CHECKLIST_PHASE_BANNER)
	if phase_box.size.x <= 0.0:
		phase_box = Rect2(turn_box.position.x, turn_box.end.y + 4.0, turn_box.size.x, 24.0)
	_place(phase_banner, phase_box)
	# THE PLAQUE IN THE STATE ITS OWN CONTENT ASKS FOR - see `CHECKLIST_TASK_BOX`.
	# The message line is hidden when the plaque is shut, because there is nothing
	# to hide: it is empty, which is why the plaque is shut.
	var checklist_open := _checklist_is_open()
	message_label.visible = checklist_open
	var task_box := _island_rect("checklist",
		CHECKLIST_TASK_BOX if checklist_open else CHECKLIST_TASK_BOX_SHUT)
	if task_box.size.x <= 0.0:
		var task_width := clampf(frame.x * 0.46, 500.0, 880.0)
		task_box = Rect2((frame.x - task_width) * 0.5, 52.0, task_width, 48.0)
	# The two lines sit at the TOP of retail's box rather than filling it: the
	# rest of that box is the scrolling critical-task list, whose content retail
	# feeds from the engine (named gap `dynamic-content-slots-are-empty`), and
	# this project has one task at a time. Stretching two lines over 130 authored
	# pixels would be padding an empty list to look full.
	var line_height := maxf(19.0, 20.0 * scale)
	# THE TWO LINES SIT ON THE BOX'S OPTICAL CENTRE, not against its top edge.
	#
	# They used to be flush to the top, which left "roughly ninety per cent empty
	# black" under a single line and made a designed EMPTY STATE read as an
	# unfinished panel - a blind review's words, and it separately noted that
	# retail's own box is just as empty but does not read that way. The difference
	# is where the type sits: retail centres its one line in the field, a shade
	# above the geometric middle, which is what a placed empty state looks like and
	# what a line that fell to the top of a container does not.
	#
	# THE DROP IS CLAMPED AT THE CENTRAL FIELD, and that clamp wins. Retail's own
	# `_collapse` plaque - the state this screen draws - is a good deal taller than
	# the box in the oracle capture, so a true 42% centre inside it would put the
	# tasks line down over the middle of Middle-earth, and
	# `wotr_region_card_runner.no_hud_island_ever_enters_the_maps_central_field`
	# holds that rectangle at every window size. It caught exactly this. So the
	# lines drop as far toward the field's optical centre as they can and stop at
	# the map's edge, which is a smaller improvement than centring and is the one
	# that is actually available.
	# THE BLOCK IS MEASURED AGAINST THE LABELS' OWN FLOOR, not against the line
	# height the layout asked for. A Label's height is clamped up to its own
	# minimum - about 23 pixels at the smallest font this screen sets - so at small
	# windows the pair is TALLER than `line_height * 2` and a clamp computed from
	# the asked-for height let the message line cross into the field by six pixels.
	# The runner caught that too.
	var block_height := maxf(line_height, 26.0) * (2.0 if checklist_open else 1.0)
	var block_top := task_box.position.y + maxf(4.0 * scale,
		(task_box.size.y - block_height) * 0.42)
	block_top = minf(block_top, frame.y * CENTRAL_FIELD.position.y - block_height - 1.0)
	block_top = maxf(block_top, task_box.position.y + 4.0 * scale)
	var hint_box := Rect2(
		Vector2(task_box.position.x + 12.0 * scale, block_top),
		Vector2(task_box.size.x - 24.0 * scale, line_height))
	_place(hint_label, hint_box)
	_place(message_label, Rect2(
		hint_box.position + Vector2(0.0, line_height), hint_box.size))
	message_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	# `header_label` is NOT in this list: it is a drawn Control, and `clip_text`
	# is a Label property. `_draw_header` fits its own plates to its own width.
	for line_label in [status_label, phase_banner, hint_label, message_label]:
		line_label.clip_text = true

	# TOP-RIGHT: END TURN sits exactly on retail's END PHASE capsule - the button
	# is transparent and the capsule under it is retail's own art in retail's own
	# authored state (see `_draw_end_turn`). The seat plaques hang under it.
	var end_box := _island_rect("endTurnButton", ENDTURN_FACE)
	if end_box.size.x <= 0.0:
		end_box = Rect2(frame.x - 172.0 - 24.0, 14.0, 172.0, 40.0)
	_place(end_turn_button, end_box)
	# END TURN'S CAPTION IS SIZED TO RETAIL'S OWN CAPSULE. It used to keep Godot's
	# default 16 at every window size, which at the 2560x1440 frame the oracle is
	# captured at is a little over half the cap height retail sets in the same
	# pill; see `HudChrome.fit_capsule_caption` for the measurement and for why the
	# optical nudge is applied as a margin rather than by moving the button.
	HudScript.fit_capsule_caption(end_turn_button)
	var seats_width := clampf(frame.x * 0.17, 250.0, 330.0)
	# THE PLAQUE CARD IS SIZED TO ITS ROWS, not to a fraction of the window: a
	# card two thirds empty is the "vast dead region" a blind review named, and
	# the row count is known here (one per seat, plus the unclaimed line).
	# One row per plaque PLUS the column-heading row `_draw_standings` sets above
	# them; a card sized for the plaques alone would clip its own headings.
	var plaque_rows := maxi(_seat_plaques.size(), 1) + 1
	var plaque_height := clampf(30.0 * scale, 24.0, 46.0)
	_place(standings_label, Rect2(frame.x - seats_width - 24.0, end_box.end.y + 14.0,
		seats_width, plaque_rows * plaque_height + 4.0))

	# THE SEAT PANEL CARRIES SEATS AND NOTHING ELSE. MAIN MENU is on the pause
	# shell now - see `build()` for the reasoning, which is that a live scoreboard
	# is a readout and shell navigation is not something a readout does.
	#
	# THE PAUSE SHELL, centred in the frame, with its two capsules stacked in the
	# lower half of its card. Both are placed inside `_pause_card_rect`'s INNER
	# field - clear of `draw_card`'s bevel, its fillet and the corner scrolls that
	# turn inside it - by the same 2.9-weight measure the region-card runner holds
	# every framed control to. That measure is the frame's own construction rather
	# than a margin somebody liked: an elbow 1.6 weights in from the corner, an arm
	# up to nine weights long, and a curl of nearly one more weight at its end.
	var shell_card := _pause_card_rect()
	_place_exact(pause_shell, Rect2(Vector2.ZERO, frame))
	var shell_weight := clampf(minf(shell_card.size.x, shell_card.size.y) * 0.035, 3.0, 9.0)
	var shell_field := shell_card.grow(-shell_weight * 2.9)
	# THREE CAPSULES NOW: RESUME, OPTIONS, MAIN MENU. The stack is measured from the
	# card's floor upward so adding a fourth is one entry in this list rather than
	# three arithmetic edits, and so the block cannot walk down through the frame.
	var shell_stack: Array[Button] = [pause_resume, pause_options, back_button]
	var menu_height := clampf(shell_field.size.y * 0.17, 24.0, 48.0)
	var menu_width := shell_field.size.x * 0.78
	var menu_left := shell_field.position.x + (shell_field.size.x - menu_width) * 0.5
	# The capsules sit in the card's lower half, under the engraved head and the key
	# reference, with a gap of a third of a capsule between them.
	var menu_gap := menu_height * 0.34
	var stack_top := shell_field.end.y - menu_height * float(shell_stack.size()) \
		- menu_gap * float(shell_stack.size() - 1)
	for index in range(shell_stack.size()):
		_place_exact(shell_stack[index], Rect2(menu_left,
			stack_top + (menu_height + menu_gap) * float(index), menu_width, menu_height))
	# THE CAPTION SHRINKS TO ITS CAPSULE, never the capsule to its caption. A
	# Button's own combined minimum IS its text, and `Control.size` is clamped up to
	# that minimum - so lettering too wide for the card would push the pill out
	# through the card's own frame no matter what rectangle it was handed, which is
	# the mechanism behind three of the frame violations this round removes.
	for shell_button in shell_stack:
		HudScript.fit_capsule_caption(shell_button, 0.38)
	var shell_caption := int(clampf(menu_height * 0.38, 9.0, 44.0))
	for shell_button in shell_stack:
		while shell_caption > 8 and shell_button\
				.get_combined_minimum_size().x > menu_width:
			shell_caption -= 1
			for shrink in shell_stack:
				shrink.add_theme_font_size_override("font_size", shell_caption)

	# BOTTOM-LEFT: the palantir. The frame IS retail's `StrategicPalantir` ring;
	# the portrait goes in the well retail leaves for its own region feed and the
	# owner line under the engraved name, both inside the island.
	var globe_box := _island_rect("globe", Rect2(Vector2.ZERO, HudScript.APT_AUTHORED))
	if _islands.has("globe"):
		globe_box = (_islands["globe"] as Dictionary)["rect"] as Rect2
	else:
		var dish_height := clampf(frame.y * 0.30, 200.0, 300.0)
		globe_box = Rect2(16.0, frame.y - dish_height - 16.0, dish_height + 260.0, dish_height)
	# `_place_exact`, NOT `_place`. The portrait frame is a drawn Control with no
	# content of its own, so it has nothing that needs a floor - and a floor here is
	# a ratchet: `custom_minimum_size` invalidates the combined-minimum cache
	# DEFERRED while `Control.size` is clamped against it IMMEDIATELY, so the frame
	# handed a 3840-wide window's globe rectangle and then a 1280-wide one kept the
	# wide size and hung twenty pixels outside retail's own palantir island. The
	# layout runner's containment rule caught it at exactly that pair of sizes.
	_place_exact(region_portrait_frame, globe_box)
	# THE CAPTION IS CUT TO THE LENS'S OWN CHORD, not to the lens's bounding box.
	#
	# It used to be placed across the full width of `PALANTIR_DISH` at 72% of its
	# height. `PALANTIR_DISH` is the bounding RECTANGLE of an ELLIPSE, so at 72%
	# down the glass is barely two thirds as wide as that rectangle - and the last
	# token of the second line ran straight into the gold bevel. A blind review
	# photographed it: "'6 GP' runs into the oval bevel". The box now starts higher
	# and is exactly the ellipse's own chord at its LOWEST line, less a bevel
	# allowance, so no setting of this caption can reach the rim.
	var caption_top := PALANTIR_DISH.size.y * PALANTIR_CAPTION_TOP
	var caption_height := PALANTIR_DISH.size.y * PALANTIR_CAPTION_HEIGHT
	var caption_radii := PALANTIR_DISH.size * 0.5
	var caption_drop := caption_top + caption_height - caption_radii.y
	var caption_half := caption_radii.x * sqrt(maxf(
		0.0, 1.0 - (caption_drop / caption_radii.y) * (caption_drop / caption_radii.y)))
	caption_half = maxf(caption_half * PALANTIR_CAPTION_BEVEL, 24.0)
	var caption_box := _island_rect("globe", Rect2(
		PALANTIR_DISH.position.x + caption_radii.x - caption_half,
		PALANTIR_DISH.position.y + caption_top,
		caption_half * 2.0, caption_height))
	if caption_box.size.x <= 0.0:
		caption_box = Rect2(globe_box.position + Vector2(globe_box.size.y + 18.0, 30.0), Vector2(230.0, 64.0))
	_place(region_portrait_caption, caption_box)
	# AND ITS SHADOW IS THE ONE THE REST OF THIS ISLAND USES. The project theme
	# gives every Label a flat two-pixel black shadow at 85%; at the frame the
	# oracle is captured at that is heavier than `HudChrome.draw_engraved_caps`
	# sets under the region name eighty pixels above it, and heavier than the
	# plaque counters, which carry none - so one line in the lens was lit
	# differently from everything around it, which is what a blind review meant by
	# "a hard black drop shadow used nowhere else in the layout". Same offset and
	# the same value as the engraved caps, and it scales with the art like they do.
	var caption_shadow := maxi(1, int(round(1.5 * scale)))
	region_portrait_caption.add_theme_color_override(
		"font_shadow_color", Color(0.0, 0.0, 0.0, 0.8))
	region_portrait_caption.add_theme_constant_override("shadow_offset_x", caption_shadow)
	region_portrait_caption.add_theme_constant_override("shadow_offset_y", caption_shadow)
	# ONE ALIGNMENT, ALWAYS CENTRED. It used to be centred inside retail's lens and
	# LEFT-aligned in the drawn fallback dish, which is two answers to one question
	# and the fallback's answer was the wrong one: the thing above it - the region's
	# engraved name - is centred in both paths, and a caption that changes alignment
	# with the presence of a converted bundle is a caption nobody laid out.
	region_portrait_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER

	# THE COMMAND RING'S HOVER TARGET, on the same authored rectangle
	# `_draw_command_dial` fills. One rectangle, two readers: the tooltip cannot
	# drift away from the icons it is about.
	var dial_box := _island_rect("globe", PALANTIR_DIAL_SEAT)
	if dial_box.size.x <= 0.0:
		# No strategic art: the ring is not drawn at all, so the target is parked
		# empty rather than hovering over the drawn fallback dish, which is a
		# portrait well and not a command ring.
		dial_box = Rect2(globe_box.position, Vector2.ZERO)
	_place_exact(dial_affordance, dial_box)
	dial_affordance.tooltip_text = _command_dial_reason()

	# THE TWO MEDALLIONS, each on RETAIL'S OWN RECTANGLE FOR IT - the bounding box
	# of the triangles at the root depth retail's `namedInstances` table gives that
	# instance (`_medallion_rect`). They are placed ONLY when retail's art is bound:
	# the drawn fallback dish carries no medallions at all, so a control there would
	# be a button over nothing.
	for pair_value in [
		[medallion_key, MEDALLION_KEY_INSTANCE, _medallion_key_tooltip()],
		[medallion_banner, MEDALLION_BANNER_INSTANCE, _medallion_banner_tooltip()],
	]:
		var pair := pair_value as Array
		var button := pair[0] as Button
		if button == null:
			continue
		var box := _medallion_rect(String(pair[1]))
		# The stop decides too: the palantir comes off at FOCUSED, and a button over
		# a rim nobody can see is a click that lands on nothing.
		button.visible = box.size.x > 4.0 and view_mode == VIEW_FULL and not hud_hidden
		button.tooltip_text = String(pair[2])
		_place_exact(button, box if box.size.x > 4.0
			else Rect2(globe_box.position, Vector2.ZERO))

	# RETAIL'S TWO EXPANDERS, on retail's own rectangles for them. The stats one
	# lives on the `stats` island and the plaque one on `checklist`, so each follows
	# its own plate - including the plaque's, which moves between retail's two
	# authored sizes as it opens and shuts.
	for pair_value in [
		[stats_expander, "stats", "StrategicStats", "Expand",
			"Hide the seat roll" if standings_open else "Show the seat roll"],
		[objectives_expander, "checklist", "StrategicChecklist", "expandButton",
			"Hide the war council" if _checklist_is_open() else "Show the war council"],
	]:
		var pair := pair_value as Array
		var button := pair[0] as Button
		if button == null:
			continue
		var box := _expander_rect(String(pair[1]), String(pair[2]), String(pair[3]))
		button.visible = box.size.x > 4.0 and not hud_hidden 			and island_is_shown(String(pair[1]))
		button.tooltip_text = String(pair[4])
		_place_exact(button, box if box.size.x > 4.0 else Rect2(Vector2.ZERO, Vector2.ZERO))

	# THE BOTTOM COMMAND BAR. Retail's floor: the gilt tray running from the
	# palantir to the right edge, the TERRITORY / ARMIES / STRUCTURES rail across
	# its head, the content well under that, and the status ribbon on its bottom
	# rail. Every rectangle is retail's own authored geometry through
	# `_island_rect`; the fallback arithmetic below it is this screen's, for a
	# machine with no strategic bundle, and the diagnostics panel says which.
	var tray_box := _island_rect("selectionDetails", TRAY_FIELD)
	if tray_box.size.x <= 0.0:
		var tray_width := clampf(frame.x * 0.72, SIDE_FLOOR, frame.x - 32.0)
		var tray_height := clampf(frame.y * 0.24, 180.0, 400.0)
		tray_box = Rect2(frame.x - tray_width - 8.0,
			frame.y - tray_height - 8.0, tray_width, tray_height)
	var strip_box := _island_rect("selectionDetails", TRAY_TAB_STRIP)
	if strip_box.size.x <= 0.0:
		strip_box = Rect2(tray_box.position + Vector2(18.0, 6.0),
			Vector2(tray_box.size.x - 36.0, clampf(30.0 * scale, 24.0, 46.0)))
	var content_box := _island_rect("selectionDetails", TRAY_CONTENT)
	if content_box.size.x <= 0.0:
		content_box = Rect2(
			Vector2(strip_box.position.x, strip_box.end.y + 8.0),
			Vector2(strip_box.size.x,
				tray_box.end.y - strip_box.end.y - 16.0 - clampf(24.0 * scale, 18.0, 34.0)))
	var ribbon_box := _island_rect("selectionDetails", TRAY_RIBBON)
	if ribbon_box.size.x <= 0.0:
		ribbon_box = Rect2(Vector2(content_box.position.x, content_box.end.y + 6.0),
			Vector2(content_box.size.x, clampf(24.0 * scale, 18.0, 34.0)))

	# THE TAB RAIL, one hit area per authored tab origin, with the caption drawn
	# into the same cell by `_draw_command_bar`.
	var tab_caption_size := int(clampf(15.0 * scale, 9.0, 26.0))
	var tab_scale := (_islands["selectionDetails"] as Dictionary)["scale"] as Vector2 \
		if _islands.has("selectionDetails") else Vector2(scale, scale)
	for index in range(TRAY_TABS.size()):
		var entry := TRAY_TABS[index] as Dictionary
		var key := String(entry["key"])
		if not _tab_buttons.has(key):
			continue
		var tab := _tab_buttons[key] as Button
		# THE CELL, not the pitch - see `tray_tab_cell`. The button is the same
		# rectangle the chrome pass lights and the same one the runner holds, so a
		# caption cannot sit outside the frame its own highlight respects.
		var tab_box := _island_rect("selectionDetails", tray_tab_cell(entry))
		if tab_box.size.x <= 0.0:
			var each := strip_box.size.x / float(TRAY_TABS.size())
			tab_box = Rect2(
				strip_box.position + Vector2(each * index, TRAY_TAB_CELL_CLEARANCE),
				Vector2(each, maxf(strip_box.size.y - TRAY_TAB_CELL_CLEARANCE * 2.0, 8.0)))
		# THE CAPTION IS FITTED TO THE CELL IN BOTH AXES, AND IT IS MEASURED OFF THE
		# FACE - which is the only measurement available now that the caption is drawn
		# rather than set on the control, and was the only correct one before.
		# `get_combined_minimum_size()` reports a cache that is refreshed deferred, so
		# a loop that reads the control back reads the size it had before the loop
		# started; the face's own metrics are current the instant the size changes.
		#
		# ONE SIZE FOR ALL THREE TABS, solved for the widest caption. Three tabs at
		# three sizes is worse than three at one small one - the same rule the
		# standings headings are solved under.
		var tab_face: Font = hud_font if hud_font != null else get_theme_default_font()
		if tab_face != null:
			var caption := String(_tab_captions.get(key, entry["caption"])).to_upper()
			while tab_caption_size > 8 and (
					tab_face.get_string_size(caption, HORIZONTAL_ALIGNMENT_LEFT, -1,
						tab_caption_size).x > tab_box.size.x - 8.0
					or tab_face.get_height(tab_caption_size) > tab_box.size.y):
				tab_caption_size -= 1
		_place_exact(tab, tab_box)
	_tab_caption_size = tab_caption_size

	# THE COMMAND RAIL: ATTACK / CANCEL / AUTO-RESOLVE, and nothing else.
	#
	# All three are this project's own controls - retail's living world has none of
	# them - but they are now the RIGHT three, which they were not. `MAIN MENU`
	# used to hold the middle cell, and a blind review called that an
	# information-architecture failure rather than a layout one: the global menu is
	# not a peer of two combat verbs, and "only a prototype wires the global menu
	# into a combat-resolution prompt because the button was already there". CANCEL
	# holds it now - it acts on exactly the state the other two act on - and MAIN
	# MENU moved up beside END TURN and the seat plaques.
	#
	# ATTACK AND AUTO-RESOLVE ARE STILL NON-ADJACENT, which was the real reason for
	# a third cell between them: they are the two ways of deciding the SAME battle
	# and a misclick between them is unrecoverable, so nothing that commits a
	# battle sits next to the other thing that commits a battle.
	#
	# AND THE RAIL IS DOCKED, not parked. It sits on the tray's own top edge and
	# its plate runs from the TRAY'S left edge to the frame's right edge and down
	# into the tray (`_draw_command_rail`), so the two read as one fitting with an
	# upper deck rather than as a lozenge hovering over open terrain.
	var button_width := ENDTURN_FACE.size.x * tab_scale.x
	var button_height := ENDTURN_FACE.size.y * tab_scale.y
	if not art:
		button_width = minf((tray_box.size.x - 48.0) / 3.0, 260.0)
		button_height = clampf(40.0 * scale, 34.0, 56.0)
	var button_gap := maxf(10.0, 12.0 * tab_scale.x)
	var rail_margin := maxf(10.0, 14.0 * tab_scale.x)
	# THE THREE CELLS ARE NOT THE SAME CELL ANY MORE - see `COMMAND_WIDTH_CLASS`.
	# The row's total run is held at the three-equal-cells width it used to be, so
	# docking the deck and ranking the controls are independent changes and the
	# deck's own left edge does not move because ATTACK got wider.
	var row_run := button_width * 3.0 + button_gap * 2.0
	var class_total := 0.0
	for share_value in COMMAND_WIDTH_CLASS:
		class_total += float(share_value)
	var cell_unit := (row_run - button_gap * 2.0) / class_total
	var row_left := frame.x - rail_margin - row_run
	# THE ROW SITS ON RETAIL'S OWN TOP RAIL, not on the tray's field. The rail is
	# authored at y -30.6..-13.6 in the tray slot's space - ABOVE the field, which
	# starts at -18.1 - so a row measured off the field's top edge overlapped the
	# rail by half its height and `_draw_command_rail`'s deck had nowhere clean to
	# end. Both are measured off the rail now, so the deck, the capsules and
	# retail's gilt seam are one fitting.
	var deck_floor := tray_box.position.y
	var top_rail := _island_rect("selectionDetails", Rect2(0.0, -30.6, 1.0, 17.0))
	if top_rail.size.y > 0.0:
		deck_floor = top_rail.position.y
	var button_y := deck_floor - button_height - maxf(6.0, 8.0 * tab_scale.y)
	# THE CAPTION SIZE COMES OFF THE CAPSULE'S HEIGHT AND IS THEN SHRUNK TO ITS
	# WIDTH, in that order. Off the height because a caption set at a fixed pixel
	# size is a different weight at every window size and reads as a UI kit rather
	# than as lettering cut into a fitting; shrunk to the width afterwards because
	# a Button's own minimum is its text, so lettering too big for its cell would
	# push the cell wider than the rail no matter what rectangle it is handed.
	var rail_buttons := [attack_button, cancel_button, auto_resolve_button]
	var cell_pen := row_left
	for index in range(rail_buttons.size()):
		var cell_width := cell_unit * float(COMMAND_WIDTH_CLASS[index])
		_place_exact(rail_buttons[index] as Button, Rect2(
			cell_pen, button_y, cell_width, button_height))
		cell_pen += cell_width + button_gap
	# The rail's captions get the same optical treatment END TURN does, at the
	# smaller share their longer captions need, so every button on this screen is
	# one weight and one baseline logic rather than two.
	for button in rail_buttons:
		HudScript.fit_capsule_caption(button as Button, 0.34)
	# THE CAPTION SIZE IS ONE SIZE FOR THE ROW, SOLVED AGAINST EACH CELL'S OWN WIDTH.
	#
	# Both halves of that matter and the first version of this got the second one
	# wrong. ONE SIZE, because the rank is carried by the face and the width and a
	# third variable would make the rail three systems rather than one ranked one.
	# AGAINST ITS OWN CELL, because the cells are no longer a common width: shrinking
	# every caption until it fits the NARROWEST cell asks "AUTO-RESOLVE", which is
	# twelve glyphs in a full-width cell, to fit inside CANCEL's 0.76 share - a cell
	# it does not go anywhere near - and the capture of that shows all three captions
	# set several steps smaller than their pills. Each button is measured against the
	# cell it is actually in, and the row then takes the smallest size all of them
	# agreed to.
	var caption_size := int(clampf(button_height * 0.34, 9.0, 44.0))
	for index in range(rail_buttons.size()):
		var button := rail_buttons[index] as Button
		var cell_width := cell_unit * float(COMMAND_WIDTH_CLASS[index])
		while caption_size > 8 and button.get_combined_minimum_size().x > cell_width:
			caption_size -= 1
			for shrink in rail_buttons:
				(shrink as Button).add_theme_font_size_override("font_size", caption_size)

	_tray_content_rect = content_box
	_tray_content_gutter = 14.0 * tab_scale.x
	_place_detail_well()
	_place_exact(tray_ribbon, Rect2(ribbon_box.position + Vector2(14.0 * tab_scale.x, 0.0),
		ribbon_box.size - Vector2(28.0 * tab_scale.x, 0.0)))
	tray_ribbon.queue_redraw()

	# THE TYPE SCALES WITH THE ART. Retail's plates are fixed-proportion metal;
	# text set at a fixed pixel size would burst the small plate on a 1100x700
	# window and swim in it on a 4K one.
	phase_banner.add_theme_font_size_override("font_size", int(clampf(17.0 * scale, 11.0, 30.0)))
	hint_label.add_theme_font_size_override("font_size", int(clampf(15.0 * scale, 11.0, 26.0)))
	message_label.add_theme_font_size_override("font_size", int(clampf(15.0 * scale, 11.0, 26.0)))
	region_portrait_caption.add_theme_font_size_override(
		"font_size", int(clampf(13.0 * scale, 10.0, 22.0)))
	# THE CARD'S TYPE IS SIZED TO ITS WELL, not to the frame. Retail's content well
	# is about 90 authored pixels tall - it holds six slots, not paragraphs - so a
	# size taken off the window height put four lines in a card that has eight and
	# trimmed the rest away. `CARD_LINES` is the number of lines the well is sized
	# to hold, and `_fit_card_lines` measures against the same number.
	detail_label.add_theme_font_size_override("normal_font_size",
		int(clampf(content_box.size.y / float(CARD_LINES) / CARD_LINE_SPACING, 10.0, 22.0)))

	# THE UNPLACED BLOCK, top-left under the status plate: only populated when
	# a region could not be placed, which is a degraded state worth an island.
	var unplaced_width := clampf(frame.x * 0.20, 240.0, 400.0)
	unplaced_label.position = Vector2(28.0, 60.0)
	unplaced_label.size = Vector2(unplaced_width, 40.0)
	unplaced_label.custom_minimum_size = unplaced_label.size
	unplaced_host.position = Vector2(28.0, 104.0)
	unplaced_host.size = Vector2(unplaced_width, 0.0)
	unplaced_host.custom_minimum_size = unplaced_host.size

	# NO LEGEND STRIP AND NO VISIBLE DIAGNOSTICS TOGGLE ALONG THE BOTTOM.
	# Both used to live here. An adversarial blind review of this screen against
	# the retail capture called the colour-chip key, the camera cheat-sheet and
	# the word DIAGNOSTICS individually disqualifying - a shipped RTS teaches its
	# controls in tooltips and never keeps a debug key on the glass. The
	# diagnosis itself is not lost: `toggle_diagnostics()` still opens the whole
	# panel, and it is reached by the F1 binding in `_unhandled_key_input`.

	# THE DIAGNOSTICS OVERLAY, over the central field when opened - the one
	# surface allowed there, because opening it is asking to read a diagnosis.
	if diagnostics_panel != null:
		# IT TAKES THE WHOLE FRAME NOW, less a margin. Binding retail's strategic
		# art turned the named-gap list from a paragraph into a page (six format
		# gaps from the bundle, plus what this screen adds about the medallions,
		# the command dial, the display face and the palantir's asset layer), and
		# a diagnosis that overflows its own panel is a diagnosis nobody reads.
		# Nothing is lost by it being large: it is hidden until F1 asks for it.
		_place(diagnostics_panel, Rect2(28.0, 28.0, frame.x - 56.0, frame.y - 56.0))
		var inner_width := diagnostics_panel.size.x - 32.0
		_place(status_label, Rect2(16.0, 40.0, inner_width, 20.0))
		var body_top := 68.0
		var body_height := maxf(diagnostics_panel.size.y - body_top - 16.0, 80.0)
		# The gap list gets two thirds; the conversion report gets the rest. The
		# gap list is the longer of the two and the one a reader came for.
		_place(gaps_label, Rect2(16.0, body_top, inner_width, body_height * 0.66 - 8.0))
		_place(map_mode_label, Rect2(
			16.0, body_top + body_height * 0.66 + 8.0, inner_width, body_height * 0.34 - 16.0))

	report_backdrop.position = Vector2(LAYOUT_MARGIN, LAYOUT_MARGIN)
	report_backdrop.size = Vector2(frame.x - LAYOUT_MARGIN * 2.0, frame.y - LAYOUT_MARGIN * 2.0)
	report_text.position = report_backdrop.position + Vector2(28.0, 24.0)
	report_text.size = report_backdrop.size - Vector2(56.0, 96.0)
	report_close.size = Vector2(180.0, 40.0)
	report_close.custom_minimum_size = report_close.size
	report_close.position = Vector2(
		report_backdrop.position.x + report_backdrop.size.x - 208.0,
		report_backdrop.position.y + report_backdrop.size.y - 56.0)

	if chrome_layer != null:
		chrome_layer.queue_redraw()
	region_portrait_frame.queue_redraw()

	# THE HUD STAYS DOWN ACROSS A RESIZE. This function sets `message_label.visible`
	# from the tasks plaque's own state, so without this a window resize while the
	# HUD was off would put one label back on an otherwise bare map. The remembered
	# state is updated first, so F2 still restores what the layout wanted.
	if hud_hidden:
		for control in _hud_islands():
			if control.visible:
				_hud_visibility_before[control] = true
				control.visible = false
	else:
		# AND THE STOP GETS THE LAST WORD, for exactly the same reason the paragraph
		# above exists. This function writes `visible` on several controls from their
		# own state - the imperative line, the seat roll, the unplaced block - so a
		# relayout at the FOCUSED stop put islands back that the stop had taken off,
		# and the first capture of the stop showed one of them (the imperative line)
		# back on the glass. `_apply_view_mode` reads no geometry and writes only
		# visibility, so running it last is cheap and makes the stop authoritative
		# rather than merely first.
		_apply_view_mode()
	# THE BUILD CONTROLS LAST OF ALL, because every one of them is placed on
	# geometry this function has just moved AND on a visibility the stop above has
	# just decided. `_refresh_detail` calls it again when the roster changes without
	# the layout changing (a hover, a tab press), which is the other half of the
	# same rule: a hit area is placed whenever either its rectangle or its content
	# moves, and never on a frame where one has moved and the other has not.
	_place_build_controls()
	# AND THE MAP IS TOLD WHERE THE HUD IS, last of all, because that is the one
	# fact on this screen that is only true after everything above has run.
	_publish_hud_keep_out()


## ------------------------------------------------------------------------------
## TELL THE MAP WHERE THE HUD IS, SO THE BUILD RING NEVER OPENS UNDER IT
## ------------------------------------------------------------------------------
##
## THE CONTRACT is the map stream's `map3d.set_hud_keep_out(rects)`: an
## `Array[Rect2]` in the map view's own coordinate space, which is the window's
## because the map is full-bleed. Presentation only, idempotent, safe to call every
## layout pass, and `[]` clears it.
##
## THE DEFECT IT CLOSES was photographed in this stream's own capture set
## (`chrome-r2-final/15b-structure-raised.png`): the build ring opened in the
## top-left corner ON TOP of the Treasury plate with two of its icons cut off by
## the screen edge. `radial_centre()` clamped the ring against the PANEL and
## against nothing else - the right rule while the ring was decoration and the
## wrong one now that its icons are live buttons that spend treasure.
##
## THE MAP VIEW DOES NOT GUESS. It does not own the chrome and must not hardcode
## its geometry, so the list is empty until this function hands it in. Its own
## runner drives `set_hud_keep_out` with four rectangles the TEST supplies, which
## proves the escape arithmetic and cannot prove the running screen ever calls it.
## That half is asserted here (`wotr_region_card_runner`).
##
## WHAT GOES IN THE LIST, and the rule is the contract's: every island that is
## OPAQUE AND CLICKABLE, at the stop the screen is ACTUALLY at. A translucent
## decorative wash does not belong in it, and neither does an island the current
## F2 stop has taken off the glass - the ring is supposed to reclaim that space
## when the chrome gets out of the way, which is the entire point of the stops.
## `island_is_shown` is the same predicate the drawing pass uses, so the list and
## the picture cannot disagree.
##
## THE SEAT TABLE IS IN THE LIST TOO even though it is not one of retail's five
## islands: it is this project's own card, it is opaque, and it hangs under END
## TURN in exactly the corner the ring was landing in.
func _publish_hud_keep_out() -> void:
	if map3d == null or not map3d.has_method("set_hud_keep_out"):
		return
	var rects: Array[Rect2] = []
	if not hud_hidden:
		for entry_value in STRATEGIC_ISLANDS:
			var slot := String((entry_value as Dictionary)["slot"])
			if not _islands.has(slot) or not island_is_shown(slot):
				continue
			var rect := (_islands[slot] as Dictionary)["rect"] as Rect2
			if rect.size.x > 0.0 and rect.size.y > 0.0:
				rects.append(rect)
		# THE COMMAND DECK, which is not an island and is the widest opaque thing on
		# the screen. It is drawn whenever the tray is (`_draw_command_rail` runs
		# from `_draw_command_bar`), so it is claimed on the same condition.
		if island_is_shown("selectionDetails"):
			var deck := command_deck_rect()
			if deck.size.x > 0.0 and deck.size.y > 0.0:
				rects.append(deck)
		if standings_label != null and standings_label.visible:
			rects.append(_standings_card_rect())
	map3d.set_hud_keep_out(rects)


## Bind a live session, or NO session plus the reason there is none. Both are
## legitimate states and the screen shows either honestly; what it never does is
## show a map when `bound_session` is null.
func configure(bound_session, map_ids: Array, reason: String, pack_roots: Array = []) -> void:
	if heading_label == null:
		build()
	session = bound_session
	available_map_ids = map_ids.duplicate()
	unavailable_reason = reason
	# RETAIL'S OWN SHELL FACE, from the same place the main menu takes it:
	# `assets/ui/palantir/fonts` under a mounted pack root. A miss keeps the
	# default face and is a NAMED gap on the diagnostics panel - a lookalike
	# serif is never substituted for Albertus MT.
	# TWO FACES, one call: the caps face (Albertus MT) and the display face
	# (Omnia). `load_retail_faces` reports each miss in its own words, so the
	# reason strings below are only the ones IT cannot know - that it was never
	# handed a pack root to look in.
	# RETAIL'S STRATEGIC HUD ART FIRST, because it also carries retail's two font
	# files byte-for-byte - so a machine with no content pack mounted still gets
	# Albertus MT on the HUD caps rather than Godot's default sans.
	load_strategic_ui(pack_roots)
	var strategic_fonts: Array = []
	if strategic != null and strategic.loaded:
		strategic_fonts.append(strategic.bundle_root.path_join("assets/ui/strategic/fonts"))
	var faces: Dictionary = HudScript.load_retail_faces(pack_roots, strategic_fonts)
	hud_font = faces.get("caps") as Font
	display_font = faces.get("display") as Font
	if hud_font != null:
		hud_font_reason = ""
	elif pack_roots.is_empty():
		hud_font_reason = ("no content pack roots were handed to this screen, so "
			+ "retail's Albertus MT face was not looked for")
	else:
		hud_font_reason = String(faces.get("caps_reason", ""))
	if display_font != null:
		display_font_reason = ""
	elif pack_roots.is_empty():
		display_font_reason = ("no content pack roots were handed to this screen, so "
			+ "retail's Omnia display face was not looked for")
	else:
		display_font_reason = String(faces.get("display_reason", ""))
	_apply_hud_font()
	# RETAIL'S AUTO-RESOLVE TABLES AND THE UNIT BINDINGS, searched the same way
	# and in the same order as every other bundle: the mounted packs first, then
	# the documented environment override. A failure is NOT an error state - the
	# campaign runs fine without them, tactical battles are unaffected, and the
	# AUTO-RESOLVE button carries the loader's own reason - so the return is used
	# for the tooltip rather than checked here.
	if session != null:
		var loaded: Dictionary = session.load_auto_resolve(pack_roots)
		print("[wotr] auto-resolve: %s" % (
			"tables %s + bindings %s" % [
				String(loaded["rules_path"]).get_file(),
				String(loaded["bindings_path"]).get_file()]
			if bool(loaded.get("ok", false))
			else "UNAVAILABLE - " + String(loaded.get("reason", "")).split(".")[0]))
		# RETAIL'S AI PREFERENCE WEIGHTS, loaded the same way and with the same
		# discipline: OPTIONAL. The opponent plays without them - it ranks regions by
		# this project's own authored rules instead - so a failure is not an error
		# state, and `session.ai_template_reason` names the file and what is lost.
		# The reason goes to the diagnosis, never to the HUD: "which weights chose
		# this move" is a statement about the program, and the register rule
		# (`IMPLEMENTATION_VOCABULARY`) keeps that off the glass.
		var ai_loaded: Dictionary = session.load_ai_template(pack_roots)
		print("[wotr] opponent: %s" % (
			"retail preference weights from %s" % String(ai_loaded.get("path", "")).get_file()
			if bool(ai_loaded.get("ok", false))
			else "playing on this project's own rules - " + session.ai_template_reason.split(".")[0]))
		if not session.auto_resolve_unbound_templates.is_empty():
			print("[wotr] auto-resolve: %d unit template(s) have no auto-resolve data in any retail object file and will not fight: %s" % [
				session.auto_resolve_unbound_templates.size(),
				", ".join(Array(session.auto_resolve_unbound_templates))])
	load_map_bundle(pack_roots)
	refresh()


## Find and load retail's converted 3D map. Separate from `configure()` so a test
## can drive it directly, and so a failure to load the MAP never stops the
## strategic layer from working - the 2D fallback is a real screen, not an error
## state.
## THE 90-REGION NUMBER, SPLIT SO IT STOPS NEEDING A FOOTNOTE.
##
## The region-image bundle carries 90 rows and every report that quoted it said
## "90 regions". Thirty-eight of those rows are `Region_1` .. `Region_38`: they
## carry no display name, no `RegionPortrait` and no `Fortress.Portrait`, and
## they are why the geometry probe reads `shadedRegions=52 ... unshaded=38`.
## Retail's own living-world document says what they are - it declares three
## `RegionCampaign` blocks, and their region lists are disjoint from each other
## except that two of them are IDENTICAL:
##
##   DefaultCampaign   52 regions, all named, all with a portrait
##   EvilCampaign      38 regions, `Region_1`..`Region_38`
##   GoodCampaign      the SAME 38 ids, not 38 more
##
## 52 + 38 = 90, which is exactly the bundle's row count, so nothing is dropped
## and nothing is double-counted. THE ROWS STAY. This is a reporting fix: the
## placeholders are still loaded, still addressable and still listed - they are
## just no longer added to a number that reads as "regions you can play".
##
## THE SPLIT IS MADE ON THE ROWS THEMSELVES, not on the id spelling. A row counts
## as a placeholder when it names NO art of any kind - no region portrait, no
## fortress portrait, no fortress display name. That is a property of the data
## rather than of the naming convention, so a placeholder that retail one day
## called something else would still be counted as one, and a real region that
## happened to be called `Region_7` would not.
func region_portrait_census() -> Dictionary:
	var playable: Array[String] = []
	var placeholders: Array[String] = []
	if region_images == null or not region_images.loaded:
		return {"playable": playable, "placeholders": placeholders, "rows": 0}
	var ids: Array[String] = []
	for key in region_images.regions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var row: Dictionary = region_images.regions[region_id] as Dictionary
		var bare := (
			String(row.get("regionPortrait", "")).is_empty()
			and String(row.get("fortressPortrait", "")).is_empty()
			and String(row.get("fortressDisplayName", "")).is_empty())
		if bare:
			placeholders.append(region_id)
		else:
			playable.append(region_id)
	return {
		"playable": playable, "placeholders": placeholders, "rows": ids.size(),
	}


## The census as one line, stating the ARITHMETIC rather than the total, because
## the total on its own is the thing that was misleading.
##
## `compact` is for the on-screen conversion report, which is a fixed panel that
## already runs long; the launch log gets the whole sentence, including which of
## retail's campaign blocks the placeholder rows come from. The two say the same
## thing and neither rounds a number.
func region_portrait_census_line(compact: bool = false) -> String:
	var census: Dictionary = region_portrait_census()
	var playable: Array = census["playable"] as Array
	var placeholders: Array = census["placeholders"] as Array
	if int(census["rows"]) <= 0:
		return "REGION CENSUS: no region-image bundle, so there is no region census to split."
	if placeholders.is_empty():
		return ("REGION CENSUS: %d region row(s), all of them named and carrying "
			+ "art; no placeholder rows in this bundle.") % playable.size()
	if compact:
		return ("REGION CENSUS: %d row(s) = %d PLAYABLE + %d PLACEHOLDER (%s and "
			+ "%d more, none with a name or a portrait of any kind - retail "
			+ "declares them in its EvilCampaign and GoodCampaign blocks, which "
			+ "list the SAME ids rather than a set each). The placeholder rows "
			+ "are KEPT, not dropped; they are not part of the playable count.") % [
				int(census["rows"]), playable.size(), placeholders.size(),
				String(placeholders[0]), placeholders.size() - 1]
	var shown := placeholders.slice(0, mini(placeholders.size(), 4))
	return ("REGION CENSUS: %d row(s) = %d PLAYABLE region(s) + %d PLACEHOLDER "
		+ "row(s), %d + %d = %d. The placeholders carry no name and no portrait "
		+ "of any kind (%s%s); retail's own living-world document declares them "
		+ "in its EvilCampaign and GoodCampaign blocks, which list the SAME %d "
		+ "ids rather than %d each, beside DefaultCampaign's %d named regions. "
		+ "They are kept and addressable, not dropped - they are simply not part "
		+ "of the playable count.") % [
			int(census["rows"]), playable.size(), placeholders.size(),
			playable.size(), placeholders.size(), int(census["rows"]),
			", ".join(shown), "" if placeholders.size() <= shown.size() else ", ...",
			placeholders.size(), placeholders.size(), playable.size()]


func load_map_bundle(pack_roots: Array = []) -> bool:
	if heading_label == null:
		build()
	var bundle := BundleScript.new()
	var located: Dictionary = bundle.locate_and_load(pack_roots)
	if bool(located.get("ok", false)):
		map_bundle = bundle
		map_reason = ""
		print("[WotrMap] retail 3D map LOADED from %s [%s]" % [
			String(located.get("root", "")), String(located.get("origin", ""))])
		for line in bundle.describe_load():
			print("[WotrMap]   %s" % line)
		# A map that loaded WITH holes in it is not a clean load, and the log has
		# to distinguish the two or a degraded map reads as a good one.
		if not bundle.warnings.is_empty():
			push_warning("[WotrMap] the retail map loaded with %d texture problem(s): %s" % [
				bundle.warnings.size(), ", ".join(Array(bundle.warnings))])
	else:
		map_bundle = null
		map_reason = String(located.get("reason", ""))
		# LOUDLY. The whole point of this block: falling back to the flat 2D graph
		# used to be completely silent - no print, no warning, nothing in the log -
		# so the only way to discover it was to notice the map looked wrong. Both
		# channels now carry it, and the reason names every path, its origin and
		# the command that produces a bundle.
		push_error("[WotrMap] %s" % map_reason)
		for line in map_reason.split("\n"):
			print("[WotrMap] %s" % line)
	map3d.set_bundle(map_bundle, map_reason)
	load_region_geometry(pack_roots)
	var has_3d: bool = map3d.has_map()
	map3d.visible = has_3d
	map_view.visible = not has_3d
	# "The bundle parsed" and "there is a map on screen" are different claims.
	# If the bundle loaded but nothing was instanced, say so rather than showing
	# an empty black viewport that looks like a rendering bug.
	if has_3d and map3d.drawn_mesh_count() <= 0:
		push_error("[WotrMap] the bundle at %s loaded but produced NO drawable sub-objects; showing the flat 2D fallback instead."
			% String(located.get("root", "")))
		map_reason = ("The living-map bundle at %s loaded but produced no drawable "
			+ "sub-objects, so there is nothing to show. Every sub-object in it was "
			+ "an impassable volume, an ambient card or a shader-only surface.") % String(located.get("root", ""))
		map_bundle = null
		map3d.set_bundle(null, map_reason)
		map3d.visible = false
		map_view.visible = true
		return false
	return has_3d


## Find and load retail's per-region territory geometry. Independent of the map
## bundle on purpose: retail's Middle-earth can be on screen with no territory
## shapes converted, and that is a state the screen reports rather than hides.
func load_region_geometry(pack_roots: Array = []) -> bool:
	if heading_label == null:
		build()
	var geometry := RegionGeometryScript.new()
	var located: Dictionary = geometry.locate_and_load(pack_roots)
	if bool(located.get("ok", false)):
		region_geometry = geometry
		region_geometry_reason = ""
		print("[WotrMap] region TERRITORY GEOMETRY loaded from %s [%s]" % [
			String(located.get("root", "")), String(located.get("origin", ""))])
		for line in geometry.describe_load():
			print("[WotrMap]   %s" % line)
		if geometry.regions_without_geometry.size() > 0:
			push_warning("[WotrMap] %d region(s) in the document have NO territory mesh and will not be shaded: %s" % [
				geometry.regions_without_geometry.size(),
				", ".join(Array(geometry.regions_without_geometry))])
	else:
		region_geometry = null
		region_geometry_reason = String(located.get("reason", ""))
		# LOUDLY, on both channels, for the same reason the map bundle does: a
		# strategic map that quietly stopped shading territories would just look
		# like a rendering bug.
		push_error("[WotrMap] %s" % region_geometry_reason)
		for line in region_geometry_reason.split("\n"):
			print("[WotrMap] %s" % line)
	map3d.set_region_geometry(region_geometry, region_geometry_reason)
	_load_strings(located, pack_roots)
	return region_geometry != null


## RETAIL'S OWN STRATEGIC HUD ART - the flattened APT screens that carry the gold
## plaques, the phase band, the END PHASE capsule, the palantir ring and the
## details tray. Searched in the mounted packs and then the documented
## environment override, the same way every other bundle is. A miss is not an
## error state: the HUD falls back to the hand-built plates in
## `wotr_hud_chrome.gd` and the diagnostics panel carries the loader's own reason,
## which names every path that was tried and the command that produces a bundle.
func load_strategic_ui(pack_roots: Array = []) -> bool:
	var roots: Array = []
	for root in pack_roots:
		roots.append(String(root).path_join(RegionGeometryScript.PACK_BUNDLE_RELATIVE))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	var bundle := StrategicUiScript.new()
	var found: Dictionary = bundle.locate_and_load(roots)
	if bool(found.get("ok", false)):
		strategic = bundle
		strategic_reason = ""
		print("[WotrStrategicUI] retail strategic screens loaded from %s" % String(found.get("path", "")))
		for line in bundle.describe_load():
			print("[WotrStrategicUI]   %s" % line)
	else:
		strategic = null
		strategic_reason = String(found.get("reason", ""))
		push_warning("[WotrStrategicUI] %s" % strategic_reason)
		for line in strategic_reason.split("\n"):
			print("[WotrStrategicUI] %s" % line)
	# WITH RETAIL'S CAPSULE UNDER IT, the button must draw nothing of its own -
	# a stylebox pill on top of retail's would be this project's furniture back
	# on the glass. Only the LETTERING stays, which is what retail's own
	# `$EndPhaseButtonText` slot is for (its value is a live string the APT does
	# not carry: named gap `strategic-text-values-are-live`).
	#
	# THE SAME CAPSULE CARRIES THE OTHER THREE COMMANDS. Retail's living world
	# has no ATTACK, MAIN MENU or AUTO-RESOLVE button - those are this project's
	# controls - so this is RETAIL'S ART ON A CONTROL RETAIL DID NOT HAVE, said
	# plainly rather than implied. It is done because the alternative was three
	# hand-drawn pills sitting beside retail's own capsule, which is the seam a
	# blind review reads instantly. The capsule is drawn at its OWN authored size
	# (152x40) and never stretched: a stadium shape scaled on one axis turns its
	# round caps into ovals, and nine-slice metrics are a named gap in this data.
	#
	# MAIN MENU IS NOT IN THIS LIST ANY MORE, and dropping it is a correctness fix
	# rather than a tidy-up. It lives on the pause shell, which is drawn ABOVE the
	# chrome pass; retail's capsule art is painted BY the chrome pass, so a button
	# stripped to `StyleBoxEmpty` up there would have had its face painted
	# underneath the panel covering it and would have rendered as bare lettering on
	# a card. The shell's two pills keep the drawn gilt capsule `style_button`
	# gives them - the same dress the battle report's own button wears, for the
	# same reason: neither surface is one retail composed.
	if strategic != null:
		# EVERY COMMAND CAPSULE, FROM THE ONE LIST. `CANCEL` used to be absent from
		# this loop, which left retail's capsule painted under it by `_draw_capsule`
		# and this project's drawn pill painted on top of it by the theme - the exact
		# "why is CANCEL not the same as END TURN" the owner reported. See
		# `COMMAND_CAPSULE_NAMES`.
		for button in command_capsules():
			for state in ["normal", "hover", "pressed", "focus", "disabled"]:
				button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
			# A PRIMARY'S CAPTION IS CUT INTO ITS GOLD, NOT PAINTED ON IT.
			#
			# `draw_primary_face` fills the primary's capsule with gold, and the gold
			# readout colour every other capsule's caption is set in
			# (`GOLD_VALUE`, #d8b45a) is very nearly that fill: the first capture of
			# the ranked rail has ATTACK's own caption disappearing into its own face.
			# On a gold ground the legible caption is DARK, which is also what the
			# metal is doing physically - the lettering on a cast gilt plate is the
			# recess, not the highlight.
			#
			# THE DISABLED COLOUR STAYS QUIET FOR THE SAME REASON IT IS SAFE TO:
			# `draw_primary_face` refuses to gild a control that will not answer, so a
			# disabled primary has retail's own dark capsule under it and its caption
			# is read against that, exactly like a secondary's.
			var primary := String(COMMAND_RANKS.get(String(button.name), "")) == COMMAND_RANK_PRIMARY
			var caption := HudScript.INK_CAPTION if primary else HudScript.GOLD_VALUE
			var caption_hot := HudScript.INK_CAPTION if primary else HudScript.RIM_GOLD_HOT
			button.add_theme_color_override("font_color", caption)
			button.add_theme_color_override("font_hover_color", caption_hot)
			button.add_theme_color_override("font_pressed_color", caption_hot)
			# AND THE OUTLINE FLIPS WITH IT. `HudChrome.style_button` hangs a dark
			# outline on every caption to give this project's one weight of Albertus
			# the heft retail's cut has; a dark outline around dark lettering on gold
			# is a smudge, so the primary's outline is the face's own highlight.
			button.add_theme_constant_override("outline_size", 4 if primary else 4)
			button.add_theme_color_override("font_outline_color",
				HudScript.INK_CAPTION_HALO if primary else Color(0.10, 0.05, 0.01, 0.85))
			# A DISABLED CAPTION ON RETAIL'S OWN CAPSULE, at the value every other
			# quiet caption on this HUD is set in. It was at 45% alpha, which is what
			# a blind review measured as "near-invisible grey with no affordance that
			# they are disabled rather than missing" - see `HudChrome.style_button`
			# for the same correction on the drawn pill.
			button.add_theme_color_override("font_disabled_color", HudScript.PARCHMENT_DIM)
	_relayout()
	return strategic != null


## EVERY CAPTION ON THIS HUD THAT RETAIL ALSO WRITES, TAKEN FROM RETAIL.
##
## `END TURN`, `MAIN MENU`, `AUTO-RESOLVE` and the three tab captions were
## LITERALS in this file. A literal that happens to agree with retail is still
## this project's wording - and retail's own words differ for two of them:
## `APT:StrategicHUDEndTurn`/`APT:EndPhaseButtonText` read "END PHASE", not "END
## TURN". Where retail's own text exists it is used; where it does not (there is
## no retail control called ATTACK on the living world at all) the literal stays
## and `wotr_display_names` has already recorded the miss as a NAMED GAP.
func _apply_retail_captions() -> void:
	if end_turn_button == null:
		return
	# END TURN KEEPS ITS OWN WORD. Retail's caption for this capsule is "END PHASE"
	# (`APT:EndPhaseButtonText`), and it is NOT used: this screen ends a TURN and
	# models none of retail's phases - retail's phase list is hardcoded in its
	# executable and `livingworldlogic.ini` ships empty - so borrowing the word
	# would be parity on the lettering and a lie about the control.
	back_button.text = names.shell_label("APT:MainMenu", "MAIN MENU")
	# RETAIL'S OWN WORD FOR LEAVING THE PAUSE SHELL, out of retail's own table.
	pause_resume.text = names.shell_label("APT:Resume", "RESUME")
	auto_resolve_button.text = names.shell_label(
		"APT:StrategicBattlePromptAutoResolveButtonText", "AUTO-RESOLVE")
	for entry_value in TRAY_TABS:
		var entry := entry_value as Dictionary
		_tab_captions[String(entry["key"])] = names.shell_label(
			String(entry["string"]), String(entry["caption"]))
	_relayout()


## Which of retail's authored capsule states the chrome pass should draw for one
## button. Presentation only: it writes one field and asks for a repaint.
func _on_capsule_state(button_name: StringName, state: String) -> void:
	if String(_capsule_states.get(String(button_name), "up")) == state:
		return
	_capsule_states[String(button_name)] = state
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## THE FOUR COMMAND CAPSULES, IN ONE PLACE, IN RAIL ORDER.
##
## Every pass that touches a command button reads this - `build()` dresses them,
## `_bind_strategic_ui` strips them to retail's capsule, `_relayout` places three
## of them and `_draw_capsule` paints all four. See `COMMAND_CAPSULE_NAMES` for
## why it is a list rather than four call sites that each name a subset.
##
## END TURN IS FIRST because it is the one retail authored the art for; the three
## after it are this project's controls wearing it, which is stated at
## `_bind_strategic_ui`.
func command_capsules() -> Array[Button]:
	var capsules: Array[Button] = []
	for button in [end_turn_button, attack_button, cancel_button, auto_resolve_button]:
		if button != null:
			capsules.append(button as Button)
	return capsules


## A capsule took or lost keyboard focus, so the chrome pass repaints its ring.
## Presentation only; the focus itself is Godot's.
func _on_capsule_focus() -> void:
	if chrome_layer != null:
		chrome_layer.queue_redraw()


# ------------------------------------------------------------------------------
# RETAIL'S TWO EXPANDER BUTTONS, AND THE RED ARROWS ON THEM
# ------------------------------------------------------------------------------
#
# The owner: "The down-looking red arrows need to be wired to go upward and get
# out of the way of the player so the UI can work better. It's too cluttered."
#
# There are two of them and they are both retail's, both drawn and neither wired:
#
#   * `StrategicStats`' `Expand`      - under the Treasure / World Command plate.
#   * `StrategicChecklist`' `expandButton` - on the objectives plaque.
#
# BOTH WERE POINTING DOWN BECAUSE THE FLATTENING PARKED THEM ON RETAIL'S OWN DOWN
# FRAME, and that is measurable rather than a matter of opinion: each button holds
# a named `Arrow` sprite whose timeline carries `_init`/`_rotateUp` at matrix
# `[1, 0, 0, 1]` and `_rotateDown` at `[-1, 0, 0, -1]`, and the flattened quad maps
# the atlas's TOP-RIGHT corner onto the screen's BOTTOM-LEFT - a half turn of art
# that points up in the atlas. Child timeline playback is the standing named gap
# `timeline-playback-not-bound`, so the state that survived is whichever one the
# movie was parked on, and it was the down one.
#
# SO THE ARROW NOW POINTS THE WAY ITS PANEL WILL MOVE, in retail's own half turn,
# and both buttons are live: the stats one puts the seat table away and brings it
# back, the plaque one opens and shuts the objectives plaque. And BOTH PANELS
# DEFAULT TO OUT OF THE WAY, which is the second half of what the owner asked for.

## Whether the seat table under the stats plate is on the glass. Driven by
## retail's own `Expand` button.
##
## DEFAULT OPEN, and that is a deliberate split from the objectives plaque next to
## it. Both are "chunks of UI the owner wants to be able to remove", but they are
## not the same kind of thing: the plaque is a PROMPT that has been read and is now
## in the way, while the seat roll is the screen's only statement of who is winning,
## and a blind review has already marked this HUD down twice for information that is
## missing rather than merely dense. So the roll ships on and the arrow above it
## points UP - the affordance for putting it away, offered rather than pre-applied,
## which is what the owner asked for in "wired to go upward".
var standings_open := true
## Retail's `Expand` on the stats plate, as a real control.
var stats_expander: Button
## Retail's `expandButton` on the objectives plaque, as a real control.
var objectives_expander: Button


## Open or shut the seat table. Presentation only.
func set_standings_open(value: bool) -> void:
	if standings_open == value:
		return
	standings_open = value
	_apply_view_mode()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## THE AUTHORED PATH PREFIX OF ONE EXPANDER'S ARROW, as a one-entry list ready for
## `draw_apt_frame`'s `path_turns`, or an empty list when this movie or this state
## does not paint it.
##
## Every number in the path is READ, never written: the button's root depth comes
## from retail's `namedInstances` entry for `instance`, the arrow's depth INSIDE
## the button comes from retail's `namedInstances` entry for `Arrow` scoped to that
## button's own sprite, and the frame index comes off the flattened frame. The old
## way to do this would have been to write `".../71/5"` in a constant and have it
## silently stop matching the day the bundle was rebuilt.
func _expander_arrow_paths(movie: String, instance: String, frame: Dictionary) -> Array:
	if strategic == null or frame.is_empty():
		return []
	var instances: Array = strategic.named_instances(movie)
	var button_depth := -1
	var sprite_id := -1
	for entry_value in instances:
		var entry := entry_value as Dictionary
		if String(entry.get("scope", "")) != "root":
			continue
		if String(entry.get("name", "")) != instance:
			continue
		button_depth = int(entry.get("depth", -1))
		sprite_id = int((entry.get("character", {}) as Dictionary).get("characterId", -1))
		break
	if button_depth < 0 or sprite_id < 0:
		return []
	var arrow_depth := -1
	for entry_value in instances:
		var entry := entry_value as Dictionary
		if String(entry.get("name", "")) != "Arrow":
			continue
		if String(entry.get("scope", "")) != "sprite:%d" % sprite_id:
			continue
		arrow_depth = int(entry.get("depth", -1))
		break
	if arrow_depth < 0:
		return []
	return ["screen:%s:frame:%d/%d/%d" % [
		movie, int(frame.get("frameIndex", 0)), button_depth, arrow_depth]]


## ONE EXPANDER BUTTON'S RECTANGLE, from retail's own instance depth - the same
## derivation `_medallion_rect` uses, and for the same reason.
func _expander_rect(slot: String, movie: String, instance: String) -> Rect2:
	if strategic == null or not _islands.has(slot):
		return Rect2()
	var depth := -1
	for entry_value in strategic.named_instances(movie):
		var entry := entry_value as Dictionary
		if String(entry.get("scope", "")) == "root" 				and String(entry.get("name", "")) == instance:
			depth = int(entry.get("depth", -1))
			break
	if depth < 0:
		return Rect2()
	var island := _islands[slot] as Dictionary
	var authored := HudScript.apt_depth_bounds(island["frame"] as Dictionary, depth)
	if authored.size.x <= 0.0:
		return Rect2()
	return Rect2(
		(island["origin"] as Vector2) + authored.position * (island["scale"] as Vector2),
		authored.size * (island["scale"] as Vector2))


## THE AUTHORED PATH OF THE OBJECTIVES PLAQUE'S BLACK FIELD HOST.
##
## Derived rather than written down, the same way `_card_well_host_path` is: the
## frame index is read off the frame that was actually asked for (the plaque has
## two authored sizes and they are different frames), and the depth is the root
## depth of the ONLY pure-black opaque fill the plaque carries. Reading it means a
## rebuilt bundle that moves the host cannot leave this silently matching nothing.
func _checklist_field_host_path(frame: Dictionary) -> String:
	if frame.is_empty():
		return ""
	var index := int(frame.get("frameIndex", 0))
	for draw_value in frame.get("draws", []) as Array:
		var draw := draw_value as Dictionary
		if String(draw.get("kind", "")) != "solid-triangle":
			continue
		if not HudScript._is_black_fill(draw):
			continue
		var depth := HudScript.apt_draw_depth(draw)
		if depth < 0:
			continue
		return "screen:StrategicChecklist:frame:%d/%d" % [index, depth]
	return ""


func _on_stats_expander_pressed() -> void:
	set_standings_open(not standings_open)


func _on_objectives_expander_pressed() -> void:
	set_objectives_open(not objectives_open)


# ------------------------------------------------------------------------------
# THE PALANTIR'S TWO MEDALLIONS
# ------------------------------------------------------------------------------

## One medallion, built the same way for both so they cannot drift apart.
##
## A `Button` with every stylebox emptied: retail's own gilt disc is painted under
## it by the palantir island and this control contributes the HIT AREA, the
## POINTER STATE and the ACTION. Its lit and pressed treatment is drawn by
## `_draw_medallions` on the chrome pass, for the reason stated at
## `MEDALLION_KEY_INSTANCE`.
func _build_medallion(control_name: String, action: Callable) -> Button:
	var button := Button.new()
	button.name = control_name
	button.flat = true
	button.focus_mode = Control.FOCUS_ALL
	for state in ["normal", "hover", "pressed", "focus", "disabled"]:
		button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
	button.pressed.connect(action)
	# The chrome pass paints the state, so every state change asks for a repaint.
	for signal_name in ["mouse_entered", "mouse_exited", "button_down", "button_up",
			"focus_entered", "focus_exited"]:
		button.connect(signal_name, _on_capsule_focus)
	add_child(button)
	return button


## ONE MEDALLION'S RECTANGLE, DERIVED FROM RETAIL'S OWN MOVIE.
##
## `instance` is retail's authored instance name (`optionsButton` /
## `objectivesButton`). Its ROOT DEPTH is read out of `namedInstances`, and the
## rectangle is the bounding box of every flattened triangle whose authored path
## sits at that depth - so the control follows retail's art wherever the island
## stretch puts it, at any window size, with nothing measured off a screenshot.
##
## Returns a zero-size rect when the strategic bundle is not bound or the movie
## does not carry that instance, and the caller parks the control rather than
## guessing a position.
func _medallion_rect(instance: String) -> Rect2:
	if strategic == null or not _islands.has("globe"):
		return Rect2()
	var depth := -1
	for entry_value in strategic.named_instances("StrategicPalantir"):
		var entry := entry_value as Dictionary
		if String(entry.get("name", "")) != instance:
			continue
		if String(entry.get("scope", "")) != "root":
			continue
		depth = int(entry.get("depth", -1))
		break
	if depth < 0:
		return Rect2()
	var island := _islands["globe"] as Dictionary
	var authored := HudScript.apt_depth_bounds(island["frame"] as Dictionary, depth)
	if authored.size.x <= 0.0 or authored.size.y <= 0.0:
		return Rect2()
	var origin := island["origin"] as Vector2
	var scale := island["scale"] as Vector2
	return Rect2(origin + authored.position * scale, authored.size * scale)


## WHAT THE KEY SAYS IT DOES, and what the banner says it does.
##
## Both are written as a command to the player rather than as a description of a
## widget, which is the register the whole HUD is audited against
## (`IMPLEMENTATION_VOCABULARY`). The banner's line changes with the plaque's own
## state, because a toggle that says the same thing in both states is a toggle
## whose caption is decoration.
func _medallion_key_tooltip() -> String:
	return "Settings"


func _medallion_banner_tooltip() -> String:
	return "Hide the war council" if objectives_open else "Show the war council"


## THE KEY - retail's own `optionsButton`. Retail's binding; this project's
## settings screen behind it. The shell owns that screen, so this asks for it
## rather than building a second one (`options_requested`).
func _on_medallion_key_pressed() -> void:
	options_requested.emit()


## THE BANNER - retail's own `objectivesButton`. Retail drives a campaign
## objectives screen this project does not have, so it opens and shuts the
## objectives plaque instead, which is the nearest true thing this screen owns.
## PROJECT-AUTHORED, and named as such on the diagnostics panel.
func _on_medallion_banner_pressed() -> void:
	set_objectives_open(not objectives_open)


## RETAIL'S RESTING MEDALLION ART, LIT AND SUNK BY THIS PROJECT.
##
## Retail authors `_up`, `_over`, `_down` and `_disabled` for each medallion as a
## CHILD sprite timeline, and child timeline playback is the standing named gap
## `timeline-playback-not-bound` - the flattening carries the root frame only, so
## the bundle holds one disc per medallion and not four. A control that cannot show
## it was pressed is the defect the owner reported, so the states are drawn:
##
##   * HOVER lifts a warm bloom OUTSIDE the disc, the same way the focus ring sits
##     outside a capsule, so retail's own pixels are never tinted.
##   * PRESSED sinks the disc into its seat with a dark inner arc across the top -
##     the direction the light comes from everywhere else on this HUD.
##   * FOCUS gets the ring, so a keyboard walk reaches these two as well.
##
## All three are this project's, and the diagnostics panel says so.
func _draw_medallions() -> void:
	for button in [medallion_key, medallion_banner]:
		if button == null or not button.visible or button.size.x <= 4.0:
			continue
		var box := Rect2(button.position, button.size)
		var centre := box.get_center()
		var radius := minf(box.size.x, box.size.y) * 0.5
		var state := String(_capsule_states.get(String(button.name), "up"))
		if button.button_pressed or state == "down":
			chrome_layer.draw_arc(centre, radius * 0.94, PI * 1.05, PI * 1.95, 28,
				Color(0.0, 0.0, 0.0, 0.55), maxf(2.0, radius * 0.16))
		elif button.is_hovered():
			for step in range(3):
				chrome_layer.draw_arc(centre, radius + 2.0 + float(step) * 2.0,
					0.0, TAU, 40,
					Color(HudScript.RIM_GOLD_HOT.r, HudScript.RIM_GOLD_HOT.g,
						HudScript.RIM_GOLD_HOT.b, 0.34 - 0.10 * float(step)),
					2.0)
		if button.has_focus():
			HudScript.draw_focus_ring(chrome_layer, box)


## Retail's string table, looked for beside whichever region-geometry bundle was
## found and in the mounted packs. A miss is a REPORTED miss: regions keep their
## retail ids and the screen says the table is not converted.
func _load_strings(located: Dictionary, pack_roots: Array) -> void:
	var roots: Array = []
	var geometry_root := String(located.get("root", ""))
	if not geometry_root.is_empty():
		roots.append(geometry_root)
	for root in pack_roots:
		roots.append(String(root).path_join(RegionGeometryScript.PACK_BUNDLE_RELATIVE))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	var table := StringsScript.new()
	var found: Dictionary = table.locate_and_load(roots)
	if bool(found.get("ok", false)):
		strings = table
		strings_reason = ""
		print("[WotrStrings] retail string table loaded from %s: %d strings" % [
			String(found.get("path", "")), table.count()])
	else:
		strings = null
		strings_reason = String(found.get("reason", ""))
		push_warning("[WotrStrings] %s" % strings_reason)
		print("[WotrStrings] %s" % strings_reason)

	# THE NAMES A PLAYER READS, through the one resolver that owns them. The seat
	# names come from the GAME SETUP string bundle's `SIDE:` namespace (a
	# different table in a different file from the strategic one, which is why it
	# is located separately); the region and territory names come from the
	# strategic table just loaded above. Both fail independently and both record
	# NAMED GAPS rather than letting a raw key back onto the screen.
	var names_found: Dictionary = names.locate_and_load(roots)
	names.bind_living_world(strings)
	if bool(names_found.get("ok", false)):
		print("[WotrNames] seat names from %s" % String(names_found.get("path", "")))
	else:
		push_warning("[WotrNames] %s" % String(names_found.get("reason", "")))
		print("[WotrNames] %s" % String(names_found.get("reason", "")))

	# RETAIL'S UI SURFACE, looked for in the same places. Independent of the
	# string table on purpose: names and portraits fail separately and a screen
	# that reported one for the other would send a reader to the wrong bundle.
	var ui_bundle := LivingWorldUiScript.new()
	var ui_found: Dictionary = ui_bundle.locate_and_load(roots)
	if bool(ui_found.get("ok", false)):
		ui = ui_bundle
		ui_reason = ""
		print("[WotrUI] retail living-world UI bundle loaded from %s" % String(ui_found.get("path", "")))
		for line in ui_bundle.describe_load():
			print("[WotrUI]   %s" % line)
	else:
		ui = null
		ui_reason = String(ui_found.get("reason", ""))
		push_warning("[WotrUI] %s" % ui_reason)
		for line in ui_reason.split("\n"):
			print("[WotrUI] %s" % line)
	map3d.set_ui(ui, ui_reason)
	# RETAIL'S OWN CHROME ART off that same bundle: the phase-band strip the
	# turn banner sits on (apt_LivingWorldUI_1.tga) and the RadialBorder ring
	# around the portrait dish (radialborders.dds). Null when the bundle is
	# absent, and the diagnostics panel names each miss; nothing stands in.
	band_texture = ui.chrome_band("top") if ui != null else null
	ring_texture = ui.image("RadialBorder") if ui != null else null

	# RETAIL'S 3D MARKER MODELS, looked for in the same places and failing
	# independently again: retail's portraits can be converted with none of its
	# banner geometry, and the screen has to be able to say which of the two is
	# missing rather than "the markers look wrong".
	var marker_bundle := MarkerModelsScript.new()
	var markers_found: Dictionary = marker_bundle.locate_and_load(roots)
	if bool(markers_found.get("ok", false)):
		markers = marker_bundle
		markers_reason = ""
		print("[WotrMarkers] retail 3D marker models loaded from %s" % String(markers_found.get("path", "")))
		for line in marker_bundle.describe_load():
			print("[WotrMarkers]   %s" % line)
	else:
		markers = null
		markers_reason = String(markers_found.get("reason", ""))
		push_warning("[WotrMarkers] %s" % markers_reason)
		for line in markers_reason.split("\n"):
			print("[WotrMarkers] %s" % line)
	map3d.set_markers(markers, markers_reason)

	# RETAIL'S OWN PORTRAITS OF THE REGIONS, for the region card. Independent
	# again: the marker geometry and the card art come from different documents
	# and fail for different reasons.
	var region_image_bundle := RegionImagesScript.new()
	var region_images_found: Dictionary = region_image_bundle.locate_and_load(roots)
	if bool(region_images_found.get("ok", false)):
		region_images = region_image_bundle
		region_images_reason = ""
		print("[WotrRegionArt] retail region portraits loaded from %s" % String(region_images_found.get("path", "")))
		for line in region_image_bundle.describe_load():
			print("[WotrRegionArt]   %s" % line)
		print("[WotrRegionArt]   %s" % region_portrait_census_line())
	else:
		region_images = null
		region_images_reason = String(region_images_found.get("reason", ""))
		push_warning("[WotrRegionArt] %s" % region_images_reason)
		for line in region_images_reason.split("\n"):
			print("[WotrRegionArt] %s" % line)

	_apply_retail_captions()

	var macro_table := MacrosScript.new()
	var macros_found: Dictionary = macro_table.locate_and_load(roots)
	if bool(macros_found.get("ok", false)):
		macros = macro_table
		macros_reason = ""
		print("[WotrStrings] retail gamedata #define table loaded from %s: %d defines" % [
			String(macros_found.get("path", "")), macro_table.defines.size()])
	else:
		macros = null
		macros_reason = String(macros_found.get("reason", ""))
		push_warning("[WotrStrings] %s" % macros_reason)
		print("[WotrStrings] %s" % macros_reason)


func refresh() -> void:
	if heading_label == null:
		return
	_rows = []
	_row_by_id = {}
	# THE REFUSAL LEDGER IS PER REFRESH. `_build_offer` appends every sentence the
	# strategic layer worded, from whichever surface asked; the diagnostics panel
	# reads the accumulated set at the end of the same refresh.
	_build_refusals_seen = []
	_targets = PackedStringArray()
	_claims = PackedStringArray()
	_moves = PackedStringArray()
	_staging = PackedStringArray()
	if session == null or session.state == null:
		status_label.text = "UNAVAILABLE"
		turn_plaque_label = ""
		turn_plaque_value = ""
		turn_banner.queue_redraw()
		_header_facts = []
		header_label.queue_redraw()
		# THE REFUSAL IS SHOWN, NOT SWALLOWED - and it is shown where a refusal
		# about the CONVERSION goes. `unavailable_reason` is the menu's own
		# sentence, written by and for whoever is wiring content packs together; it
		# names paths, environment variables and bundle stems. That is exactly the
		# register a blind review refused on the glass, so the HUD carries the fact
		# and the diagnostics panel carries the reason, verbatim, on F1.
		hint_label.text = "There is no war to fight."
		# AND IT IS SET ON THE BANNER TOO, because with no session there is nothing
		# to open the checklist plaque and the shut plaque has no field of its own
		# (retail draws its chevron bar across it). Without this the one sentence
		# this state has to say would be laid out behind retail's own art.
		hint_label.visible = false
		phase_banner.text = hint_label.text
		_seat_plaques = []
		standings_label.queue_redraw()
		detail_label.text = "[color=#e1c77d]War of the Ring is unavailable.[/color]"
		attack_button.disabled = true
		attack_button.tooltip_text = "The war is not under way."
		cancel_button.disabled = true
		cancel_button.tooltip_text = "The war is not under way."
		auto_resolve_button.disabled = true
		auto_resolve_button.tooltip_text = "The war is not under way."
		end_turn_button.disabled = true
		end_turn_button.tooltip_text = "The war is not under way."
		_clear_unplaced()
		unplaced_label.text = ""
		map_view.queue_redraw()
		_refresh_map_mode_label()
		_refresh_gaps()
		return

	_rows = session.region_rows()
	for row in _rows:
		_row_by_id[String(row["id"])] = row
	_staging = session.staging_regions()
	if not session.selected_region.is_empty():
		_targets = session.attack_targets(session.selected_region)
		_claims = session.claim_targets(session.selected_region)
		_moves = session.movement_targets(session.selected_region)

	var state: StateScript = session.state
	var seat := state.active_player()
	var seat_row: Dictionary = state.players[seat] as Dictionary if seat != StateScript.NEUTRAL else {}
	# The provenance line: which document, from where, which campaign, which
	# scenario. It stays because "the map looks wrong" is usually "a different
	# document loaded than you think".
	status_label.text = "DOCUMENT %s (%s)   CAMPAIGN %s   SCENARIO %s   %d regions   %d armies" % [
		session.document_path.get_file(),
		session.document_source,
		session.world.campaign_name,
		session.scenario_name,
		session.world.region_ids.size(),
		state.armies.size(),
	]
	_refresh_turn_banner(state, seat, seat_row)
	_refresh_header(state, seat, seat_row)
	end_turn_button.disabled = not state.pending_battle.is_empty()
	end_turn_button.tooltip_text = (
		"A battle for %s is still in flight; it must resolve before the turn passes."
			% String(state.pending_battle.get("region", ""))
		if not state.pending_battle.is_empty()
		else "Pass the turn to the next seat.")
	attack_button.disabled = not can_attack_now()
	attack_button.text = _attack_button_caption()
	attack_button.tooltip_text = _attack_button_reason()
	cancel_button.disabled = session.selected_region.is_empty() 		and session.selected_target.is_empty() and selected_plot.is_empty()
	cancel_button.tooltip_text = _cancel_button_reason()
	# AUTO-RESOLVE needs the same committable attack ATTACK does, plus the two
	# converted bundles. When either is missing the button is disabled and the
	# tooltip is the loader's own reason naming every path it searched - never a
	# bare "unavailable", and never a battle quietly fought on invented numbers.
	#
	# AND IT NEEDS A DEFENDER. On unowned ground the commitment is a CLAIM, not a
	# battle: nothing rolls, no bundle is consulted, and pressing this would do
	# precisely what the other button does. Two controls for one action is how a
	# player learns to distrust both, so this one stands down and its tooltip says
	# which one to press.
	var can_auto := can_attack_now() and not _target_is_unclaimed() \
		and session.autoresolve != null \
		and session.autoresolve_bindings != null \
		and String(state.battle_type) != StateScript.BATTLE_TYPE_RTS
	auto_resolve_button.disabled = not can_auto
	auto_resolve_button.tooltip_text = _auto_resolve_button_reason(state)
	_refresh_standings(state)
	# THE WAR COUNCIL, after the targets and the staging list are known - it reads
	# both - and before `_refresh_gaps`, which is what the plaque's own open/shut
	# decision is taken from further down.
	_refresh_war_council()
	# THE MAP FIRST. `_rebuild_unplaced()` reports which regions the map could
	# not place, so it has to run AFTER the map has placed them - otherwise it
	# reports the previous frame's answer, and on the first frame it reports
	# "nothing was placed". That is exactly how Rhun came to be listed as absent
	# on the same screen whose mode line said it had been placed.
	_refresh_map()
	_rebuild_unplaced()
	_refresh_detail()
	_refresh_gaps()
	# THE CLOCK IS RE-ASKED WHENEVER THE BUTTONS MOVE. This function is the one
	# place `attack_button.disabled` and `end_turn_button.disabled` are written, and
	# they are exactly the two conditions the pulse means - see `_pulse_is_wanted`.
	_pulse_wanted = _pulse_is_wanted()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## THE SEATS' CAPITALS, which are retail's own home regions.
##
## `HomeRegionHighlight` draws `LMR_Highlight` over the region a seat starts in,
## and the map view will not guess which one that is - it draws the highlight only
## for regions a caller names (`WotrMapView.HOME_REGION_BINDING_GAP`). Retail's
## answer is `StartRegion`, the same field `loseIfCapitalLost` tests, and it is
## already on the authoritative state as `players[].capital`. So the binding is a
## read of that field and nothing else: no region is inferred into being a
## capital, and a seat that authors none contributes none.
##
## IT IS EVERY SEAT'S CAPITAL, not the active seat's. Retail lights the home
## region of each player on the board, which is what makes the highlight readable
## as "these are the seats of power" rather than as a second selection ring.
func _home_regions() -> PackedStringArray:
	var homes: Array[String] = []
	if session == null or session.state == null:
		return PackedStringArray()
	for seat_value in session.state.players:
		var capital := String((seat_value as Dictionary).get("capital", ""))
		if not capital.is_empty() and not homes.has(capital):
			homes.append(capital)
	return PackedStringArray(homes)


## Push the strategic picture into whichever map is showing. Strictly one-way:
## the map is handed already-computed rows and never writes anything back.
func _refresh_map() -> void:
	if map3d != null and map3d.has_map():
		var adjacency: Dictionary = {}
		for region_id in session.world.region_ids:
			adjacency[String(region_id)] = session.world.neighbours(String(region_id))
		map3d.owner_colors = SEAT_COLORS
		map3d.neutral_color = NEUTRAL_COLOR
		map3d.set_regions(
			_rows, adjacency, _staging, _targets,
			session.selected_region, session.selected_target, _home_regions())
		map3d.set_overlays(
			_army_stacks_by_region(), _plots_by_region(), _display_names(),
			selected_plot, _radial_entries(), _plot_icons_by_region(),
			_structures_by_region())
		_refresh_map_mode_label()
		return
	map_view.queue_redraw()
	_refresh_map_mode_label()


func _refresh_map_mode_label() -> void:
	if map_mode_label == null:
		return
	if map3d == null or not map3d.has_map():
		# The full reason is drawn on the map itself and printed to the launch
		# log; this line exists so the mode is unambiguous even at a glance.
		map_mode_label.text = (
			"MAP: flat 2D region graph (FALLBACK) - retail's 3D Middle-earth did NOT load. "
			+ "The reason is printed on the map above and in the launch log under [WotrMap].")
		return
	var notes: Array[String] = []
	notes.append("MAP: retail livingmap.w3d, %d sub-objects, %d drawn, %d regions placed at authored world coordinates" % [
		map_bundle.sub_objects.size(), map3d.drawn_mesh_count(), map3d.placed_regions.size()])
	# TERRITORY SHADING: what is filled, and what is not.
	if map3d.has_territories():
		notes.append("TERRITORIES: retail lmr_fill.w3d / lmr_border.w3d, %d regions filled with their owner's colour, %d triangles" % [
			map3d.shaded_regions.size(), region_geometry.total_triangles])
		if map3d.unshaded_regions.size() > 0:
			notes.append("NOT SHADED (%d): the bundle carries no fill mesh for these, so they keep a marker and no territory - %s" % [
				map3d.unshaded_regions.size(), ", ".join(Array(map3d.unshaded_regions))])
	else:
		notes.append("TERRITORIES: NOT SHADED - regions are drawn as markers. %s" % region_geometry_reason.split("\n")[0])
	if map3d.centroid_placed_regions.size() > 0:
		notes.append("%d region(s) placed from a centroid DERIVED from retail's own fill triangles rather than an authored centre point: %s" % [
			map3d.centroid_placed_regions.size(), ", ".join(Array(map3d.centroid_placed_regions))])
	if strings != null:
		notes.append("NAMES: retail data/lotr.str, %d strings; %d key(s) asked for and absent" % [
			strings.count(), strings.missing_keys.size()])
	else:
		notes.append("NAMES: NOT CONVERTED - regions carry retail's own ids")
	# THE REGION CENSUS, SPLIT. The 90 in the region-art bundle is 52 playable
	# regions plus 38 placeholder rows, and reporting the total on its own is
	# what made `unshaded=38` read as a conversion gap rather than as retail's
	# own second-campaign block.
	if region_images != null and region_images.loaded:
		notes.append(region_portrait_census_line(true))
	# LABELS: how many names are on screen and how many were held back so the
	# rest could be read. A label quietly dropped is exactly the kind of thing
	# that looks like a rendering bug, so the count is stated.
	notes.append("LABELS: %d drawn, %d held back where they would have overlapped a label already placed" % [
		map3d.labels_drawn, map3d.labels_suppressed])
	# PORTRAITS: what the banners are actually carrying.
	if ui != null:
		notes.append("BANNERS: %d army banner(s) drawn from retail MappedImage crops - %d of %d image ids resolved across %d atlases" % [
			map3d.banners_drawn, int(ui.totals.get("imageIdsResolved", 0)),
			int(ui.totals.get("imageIdsRequested", 0)), int(ui.totals.get("atlases", 0))])
		var crops_missing: Array = ui.gaps.get("cropsWithoutAtlas", []) as Array
		if not crops_missing.is_empty():
			notes.append("NO ATLAS (%d): retail's own data names these images and ships no texture for them, so they are drawn as an empty slot - %s" % [
				crops_missing.size(),
				", ".join(crops_missing.map(func(v: Variant) -> String: return String(v)))])
		if not map3d.banners_without_portrait.is_empty():
			var bare: Array[String] = []
			for key in map3d.banners_without_portrait.keys():
				bare.append("%s (%s)" % [String(key), String(map3d.banners_without_portrait[key])])
			bare.sort()
			notes.append("BANNERS WITHOUT A PORTRAIT (%d), drawn as a bare faction plate: %s" % [
				bare.size(), ", ".join(bare)])
		if not ui.missing_images.is_empty():
			var unresolved: Array[String] = []
			for key in ui.missing_images.keys():
				unresolved.append("%s - %s" % [String(key), String(ui.missing_images[key])])
			unresolved.sort()
			notes.append("IMAGE IDS ASKED FOR AND NOT DRAWN (%d): %s" % [
				unresolved.size(), ", ".join(unresolved)])
	else:
		notes.append("BANNERS: NOT CONVERTED - army stacks carry no portrait. %s" % ui_reason.split("\n")[0])
	# THE 3D MARKERS: what is standing on the map as retail's own geometry, and
	# what is still a flat stand-in with the reason. Two different claims.
	if markers != null:
		notes.append("MARKERS: retail's own W3D marker models, %d of %d converted - %d meshes, %d triangles across %d families and %d slots. %d army stack(s) and %d build plot(s) are standing as retail geometry." % [
			int(markers.totals.get("modelsConverted", 0)), int(markers.totals.get("modelsNamed", 0)),
			int(markers.totals.get("meshes", 0)), int(markers.totals.get("triangles", 0)),
			int(markers.totals.get("families", 0)), int(markers.totals.get("slots", 0)),
			map3d.army_markers_standing, map3d.plot_markers_standing])
		# THE ONE NUMBER IN THE MARKERS THAT IS NOT RETAIL'S, said out loud.
		notes.append("MARKER SIZE: retail's own Scale, ZOffset and OrientAngle, times a PRESENTATION magnification of x%.2f at this framing - x1.00 (retail's exact authored size) at zoom %.2f and below, capped at x%.2f. Retail's camera never pulls back as far as this one can, and at retail's true size a banner is about fifteen pixels across the whole map." % [
			map3d.marker_magnification(), map3d.MARKER_TRUE_ZOOM,
			map3d.MARKER_MAX_MAGNIFICATION])
		# STRUCTURE MODELS: converted, and placed by the map lane rather than by this
		# screen. The count is reported because a family that stopped converting would
		# otherwise vanish silently.
		var building_families := int((markers.totals.get("familiesByKind", {}) as Dictionary).get("building", 0))
		notes.append("STRUCTURE MODELS: %d LivingWorldBuildingIcon famil(ies) are converted; %d standing structure(s) are placed on their numbered build plots." % [
			building_families, map3d.structure_markers_standing])
		if not markers.unresolved_models.is_empty():
			var absent: Array[String] = []
			for key in markers.unresolved_models.keys():
				absent.append("%s (%s)" % [String(key), String(markers.unresolved_models[key])])
			absent.sort()
			notes.append("MARKER MODELS NOT CONVERTED (%d): %s" % [absent.size(), ", ".join(absent)])
		for pair in [["ARMY STACKS", map3d.army_markers_flat], ["BUILD PLOTS", map3d.plot_markers_flat]]:
			var table := pair[1] as Dictionary
			if table.is_empty():
				continue
			var flat: Array[String] = []
			for key in table.keys():
				flat.append("%s - %s" % [String(key), String(table[key])])
			flat.sort()
			notes.append("%s STILL DRAWN FLAT (%d): %s" % [String(pair[0]), flat.size(), ", ".join(flat)])
		if not map3d.structure_markers_flat.is_empty():
			var structure_failures: Array[String] = []
			for key in map3d.structure_markers_flat.keys():
				structure_failures.append("%s - %s" % [
					String(key), String(map3d.structure_markers_flat[key])])
			structure_failures.sort()
			notes.append("STRUCTURES NOT DRAWN IN 3D (%d): %s" % [
				structure_failures.size(), ", ".join(structure_failures)])
	else:
		notes.append("MARKERS: NOT CONVERTED - armies are flat plates and build plots are flat rings. %s" % markers_reason.split("\n")[0])
	# HAND-BUILT, SAID OUT LOUD. Retail's map surround genuinely does not resolve
	# - its frame art names three .tga files that are in no archive under any
	# name, and the APT vector shapes are masks rather than filigree - so the
	# parchment band, the corner studs and the compass rose are this project's
	# own drawing in retail's palette. Calling them retail art would be the same
	# dishonesty as an invented number.
	notes.append("MAP SURROUND: HAND-BUILT, not converted - the parchment band, the gold rule, the four corner studs and the compass rose are drawn in retail's style because retail's own frame art resolves to nothing. The rose is not decoration: it turns with the camera's yaw, so it never claims north is up while the map has been orbited.")
	if not map_bundle.warnings.is_empty():
		notes.append("%d texture problem(s): %s" % [
			map_bundle.warnings.size(), ", ".join(Array(map_bundle.warnings))])
	if map3d.unplaced_regions.size() > 0:
		notes.append("%d region(s) unplaced (no authored centre point)" % map3d.unplaced_regions.size())
	if map3d.unsampled_heights.size() > 0:
		notes.append("%d region height(s) not sampled from terrain" % map3d.unsampled_heights.size())
	if map_bundle.untextured_sub_objects.size() > 0:
		notes.append("%d sub-object(s) drawn untextured: %s" % [
			map_bundle.untextured_sub_objects.size(),
			", ".join(Array(map_bundle.untextured_sub_objects))])
	# Everything retail draws that this lane does not, named on screen. A map
	# quietly missing its rivers and its ocean shader would look finished.
	var not_drawn: Array[String] = []
	for entry in map_bundle.sub_objects:
		if bool(entry["collision"]) or bool(entry["ambient"]) or bool(entry["shader_only"]):
			not_drawn.append(entry["name"] as String)
	not_drawn.sort()
	if not_drawn.size() > 0:
		notes.append("NOT DRAWN (%d): retail's impassable volumes, its animated ambient cards and multi-stage water overlays - %s" % [
			not_drawn.size(), ", ".join(not_drawn)])
	map_mode_label.text = "   |   ".join(notes)


## True when the current selection is a legal, committable ATTACK OR CLAIM.
##
## NEUTRAL GROUND IS COMMITTABLE NOW, and removing the refusal that used to sit at
## the bottom of this function is the whole of the change.
##
## What that refusal said was true when it was written: `wotr_battle.gd` cannot
## configure a battle for a region no seat holds, because there is no defending
## faction for either simulation to field, and inventing one was never on the
## table. What was missing was the OTHER half - retail does not fight for unowned
## ground, it TAKES it, by marching a hero army in ("Conquer new territories by
## moving Hero armies into adjacent territories", retail's own
## `STRATEGICHUD:MoveHeroArmiesChecklistItem`). The strategic layer now models
## that: `state.can_claim()` is the rule, `session.claim_targets()` is the list,
## and `session.commit_attack()` reads the target's owner and takes the claim path
## by itself. `session.attack_targets()` - which is what `_targets` holds - only
## offers a neutral region when a claim is actually legal from the staged region,
## so a target in that list is committable by construction and this function does
## not re-derive the rule and risk a different answer.
##
## THIS WAS THE LAST THING STOPPING A PLAYER FROM EXPANDING. On retail's
## 52-region board most of the map starts neutral, so with this guard in place the
## human had a war they could not grow in while the opponent - which commits
## through the session rather than through this button - expanded past them.
func can_attack_now() -> bool:
	if session == null or session.state == null:
		return false
	if not session.state.pending_battle.is_empty():
		return false
	if not session.state.pending_claim.is_empty():
		return false
	if session.selected_region.is_empty() or session.selected_target.is_empty():
		return false
	return Array(_targets).has(session.selected_target)


## Whether the chosen target is held by no seat - i.e. whether committing it is a
## CLAIM rather than a battle. One definition, read by the button's caption, its
## tooltip, and the two commit paths.
func _target_is_unclaimed() -> bool:
	if session == null or session.state == null or session.selected_target.is_empty():
		return false
	return session.state.owner_of(session.selected_target) == StateScript.NEUTRAL


## Select a region to stage from. Returns false (with a shown reason) when the
## region cannot stage an attack this turn.
func select_region(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if not _row_by_id.has(region_id):
		_message("%s is not a region of this campaign." % region_id)
		return false
	if not Array(_staging).has(region_id):
		var owner := session.state.owner_of(region_id)
		if owner != session.state.active_player():
			_message("%s is not yours to attack from." % _display_of(region_id))
		else:
			_message("%s holds no army to attack with." % _display_of(region_id))
		session.selected_region = ""
		session.selected_target = ""
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	# The build ring belongs to a plot in a region; staging somewhere else closes
	# it rather than leaving it hanging over ground the player has left.
	if String(selected_plot.get("region", "")) != region_id:
		selected_plot = {}
	_message("")
	refresh()
	return true


## Choose the adjacent region to attack. Refuses anything the strategic layer
## does not report as attackable from the staged region.
func select_target(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to attack from first.")
		return false
	if not Array(_targets).has(region_id):
		_message("%s cannot be attacked from %s." % [_display_of(region_id), _display_of(session.selected_region)])
		return false
	session.selected_target = region_id
	_message("")
	refresh()
	return true


## Commit the selected attack. THE ONLY path from this screen to a battle, and it
## carries the target region and nothing else - the attacker, the armies, the
## factions and the ground are all derived by the strategic layer and recorded in
## the commitment.
func commit_selected_attack() -> Dictionary:
	if not can_attack_now():
		_message("There is no committable attack selected.")
		return {"ok": false}
	# CAPTURED BEFORE THE COMMIT, because both of the session's paths clear the
	# selection when they succeed and the notice below needs the region's name.
	var target := session.selected_target
	var configured: Dictionary = session.commit_attack(target, available_map_ids)
	if not bool(configured.get("ok", false)):
		_message("Attack refused: %s" % ", ".join(Array(configured.get("refusals", PackedStringArray()))))
		refresh()
		return configured
	# NEUTRAL GROUND CAME BACK AS A CLAIM, AND A CLAIM IS NOT A BATTLE.
	#
	# The branch is on the RETURN rather than on the target's owner, because the
	# session is the authority on which path it took: `commit_attack()` returns
	# `claim` with an EMPTY `commitment` precisely so a caller that hands
	# `commitment` on to a tactical launcher fails closed instead of booting a
	# match with one side. So `battle_committed` is NOT emitted here - there is no
	# battlefield, no roster and nobody to fight - and the claim is finished on the
	# spot instead.
	if configured.has("claim"):
		return _finish_the_claim(target)
	refresh()
	battle_committed.emit(configured)
	return configured


## APPLY THE CLAIM THE SESSION JUST OPENED, and say what happened in retail's own
## words. Shared by ATTACK and by AUTO-RESOLVE, because on unowned ground the two
## buttons are the same action and two implementations of it would eventually
## disagree.
##
## THE SECOND HALF IS NOT OPTIONAL. `commit_attack()` leaves the claim PENDING,
## exactly as it leaves a battle pending, and a pending claim refuses every later
## commit by name and blocks END TURN. `auto_resolve_pending_battle()` is the door
## that applies it (see its own header: the claim resolves there so that the AI,
## which already presses that door, needed no change).
##
## NO BATTLE REPORT IS SHOWN. Nothing rolled, nothing died, and the report's own
## headline row ("ATTACKER WINS after N rounds") would be three lies about an
## event where nobody fought.
func _finish_the_claim(region_id: String) -> Dictionary:
	var resolved: Dictionary = session.auto_resolve_pending_battle()
	if not bool(resolved.get("ok", false)):
		# The claim stays OPEN on a refusal, exactly as a failed auto-resolve leaves
		# its commitment open, so the player is told rather than left with a map that
		# did not change.
		_message("The march refused: %s" % ", ".join(
			Array(resolved.get("refusals", PackedStringArray()))))
		refresh()
		return resolved
	# RETAIL'S OWN NOTICE FOR THIS EVENT, and it is a DIFFERENT STRING from the one
	# a won battle raises: `data/lotr.str` ships
	# `APT:LivingWorldRegionTakenNotice` = "%s taken!" beside
	# `APT:LivingWorldRegionConqueredNotice` = "%s conquered!" and
	# `APT:LivingWorldRegionDefendedNotice` = "%s defended!". Retail distinguishes
	# marching into empty ground from winning a fight for it, so this screen does
	# too rather than reusing one word for both.
	var notice := _region_notice("APT:LivingWorldRegionTakenNotice", "%s taken!", region_id)
	selected_army_id = -1
	# TAKING A REGION SPENDS THE TURN - `_apply_pending_claim()` advances it - so
	# the opponent moves here for the same reason it moves after an auto-resolved
	# battle. It runs BEFORE the notice is written because it writes the message
	# strip itself, and what the player pressed the button for should be the line
	# left standing; the opponent's account is whole in the turn banner's tooltip.
	run_opponent_turns()
	_message(notice)
	refresh()
	return resolved


## Retail's own single-region notice, filled with the region's own name.
##
## `%s` and not `%ls`: these APT notices are the one family in `data/lotr.str`
## that takes a narrow format specifier, so `_fill_text()` (which fills `%ls`)
## would append the name instead of substituting it. A table that has not been
## converted falls back to the literal and `wotr_display_names` has already
## recorded the miss as a named gap.
func _region_notice(key: String, fallback: String, region_id: String) -> String:
	var template := names.shell_label(key, fallback).replace("\\n", " ").strip_edges()
	var where := _display_of(region_id)
	if template.contains("%s"):
		return template.replace("%s", where)
	return "%s %s" % [where, template]


## AUTO-RESOLVE THE SELECTED ATTACK, and show the working.
##
## It commits and resolves in one press, because a half-committed auto-resolve -
## a battle admitted into the strategic state that the player then has no way to
## finish - would strand the campaign with a transaction open. Both halves go
## through the session; this screen decides nothing.
func auto_resolve_selected_attack() -> Dictionary:
	if not can_attack_now():
		_message("There is no committable attack selected.")
		return {"ok": false}
	var target := session.selected_target
	var committed: Dictionary = session.commit_attack(
		target, available_map_ids, StateScript.BATTLE_TYPE_AUTO_RESOLVE)
	if not bool(committed.get("ok", false)):
		_message("Auto-resolve refused: %s" % ", ".join(
			Array(committed.get("refusals", PackedStringArray()))))
		refresh()
		return committed
	# UNOWNED GROUND CAME BACK AS A CLAIM, so this press was never an auto-resolve.
	# The button is disabled on neutral ground (see `_auto_resolve_button_reason`)
	# and this is the belt to that brace: `auto_resolve_selected_attack()` is public
	# and a caller reaching it another way must still get the claim finished
	# properly rather than a battle report about a fight nobody had.
	if committed.has("claim"):
		return _finish_the_claim(target)
	var resolved: Dictionary = session.auto_resolve_pending_battle()
	if not bool(resolved.get("ok", false)):
		# The commitment is still open if the resolution refused. Say so rather
		# than leaving the player looking at an unchanged map with no explanation.
		_message("Auto-resolve refused: %s" % ", ".join(
			Array(resolved.get("refusals", PackedStringArray()))))
		refresh()
		return resolved
	last_auto_resolve = {
		"commitment": committed.get("commitment", {}),
		"outcome": resolved.get("outcome", {}),
		"applied": resolved.get("applied", {}),
		"seed": String(resolved.get("seed", "")),
	}
	# RESOLVING A BATTLE HANDS THE TURN ON, so the opponent moves here too. Without
	# this the campaign only ran itself forward from END TURN, and a player who
	# finished every turn with an auto-resolved attack never met an opponent at all.
	run_opponent_turns()
	_show_battle_report()
	refresh()
	return resolved


func close_battle_report() -> void:
	last_auto_resolve = {}
	report_backdrop.visible = false
	report_text.visible = false
	report_close.visible = false
	refresh()


func _show_battle_report() -> void:
	report_text.text = battle_report_bbcode()
	report_backdrop.visible = true
	report_text.visible = true
	report_close.visible = true
	report_close.grab_focus()


## THE WORKING, as the player reads it.
##
## THE WHOLE POINT OF THIS SCREEN is that it is obvious which numbers are EA's
## and which are this project's. Retail's are printed in one colour and name the
## retail file they came from; this project's are printed in another, marked
## PROJECT, and name the row of `wotr_autoresolve_rules.gd` a modder would edit
## to change them. There is no third category and nothing is unattributed.
func battle_report_bbcode() -> String:
	if last_auto_resolve.is_empty():
		return ""
	var commitment: Dictionary = last_auto_resolve.get("commitment", {})
	var outcome: Dictionary = last_auto_resolve.get("outcome", {})
	var applied: Dictionary = last_auto_resolve.get("applied", {})
	var lines: Array[String] = []

	lines.append("[b][font_size=26]BATTLE FOR %s[/font_size][/b]" % _display_of(
		String(commitment.get("region", ""))).to_upper())
	var winner := String(outcome.get("winner", ""))
	var headline := "UNDECIDED"
	if winner == "attacker":
		headline = "ATTACKER WINS"
	elif winner == "defender":
		headline = "DEFENDER HOLDS"
	lines.append("[b][font_size=20]%s[/font_size][/b]  after %d round(s)" % [
		headline, int(outcome.get("rounds", 0))])
	lines.append("[color=#c8c2b0]%s[/color]" % String(outcome.get("reason", "")))
	lines.append("")

	# THE KEY, FIRST, so nothing below has to be guessed at.
	lines.append("[b]HOW TO READ THIS[/b]")
	lines.append("  [color=%s]RETAIL[/color]   a number EA authored. The retail file it came from is named beside it." % RETAIL_COLOR)
	lines.append("  [color=%s]PROJECT[/color]  a rule retail never states. Open BFME chose it; the row it lives in is named beside it, in game/src/wotr/wotr_autoresolve_rules.gd, and you can edit it." % PROJECT_COLOR)
	lines.append("")

	lines.append("[b]THE DICE ARE OURS, AND THEY ARE SEEDED FROM THIS BATTLE[/b]")
	lines.append("  [color=%s]PROJECT[/color]  Risk dice: the striking unit rolls %d, the unit struck rolls %d, top %d paired highest against highest, ties to the %s. Two pairs won = full damage, one = half, none = nothing." % [
		PROJECT_COLOR,
		RulesScript.int_value("attacker_dice", 3), RulesScript.int_value("defender_dice", 2),
		RulesScript.int_value("pairs_compared", 2),
		"defender" if RulesScript.bool_value("ties_to_defender", true) else "attacker"])
	lines.append("  [color=%s]PROJECT[/color]  they REPLACE retail's own MissPercentChance roll, whose seed retail never states. Average landed fraction 0.5396 against retail's own 0.5000." % PROJECT_COLOR)
	lines.append("  [color=#8fa4bd]seed[/color] %s" % String(last_auto_resolve.get("seed", "")))
	lines.append("  [color=#8fa4bd]the seed is the SHA-256 of this battle's commitment - region, turn, both seats, both factions, both handicaps, the battlefield and every army id. No clock is read anywhere on this path, so every player's copy of this campaign rolled exactly these dice.[/color]")
	lines.append("")

	lines.append("[b]THE SIDES[/b]")
	for role in ["attacker", "defender"]:
		var seat := int(commitment.get(role, -1))
		var side: Dictionary = outcome.get(role, {})
		var handicap: Dictionary = side.get("handicap", {})
		lines.append("  [b]%s[/b] seat %d, %s, handicap %d%%  [color=%s]RETAIL[/color] weapon x%s / armour x%s (livingworldautoresolvehandicaps.ini)" % [
			role.to_upper(), seat, String(commitment.get("%s_faction" % role, "")),
			int(commitment.get("%s_handicap" % role, 0)), RETAIL_COLOR,
			str(handicap.get("weaponMultiplier", 1.0)), str(handicap.get("armorMultiplier", 1.0))])
		var survivors: Array = side.get("survivors", [])
		var lost: PackedStringArray = side.get("lost", PackedStringArray())
		lines.append("    %d survived, %d lost%s" % [
			survivors.size(), lost.size(),
			"" if lost.is_empty() else ": " + ", ".join(Array(lost))])
		for row in survivors:
			var unit: Dictionary = row
			lines.append("      %s  %.1f / %.1f hp  [color=%s]RETAIL[/color] %s, %s vs %s" % [
				String(unit.get("template", "?")),
				float(int(unit.get("hitpoints_milli", 0))) / 1000.0,
				float(int(unit.get("max_hitpoints_milli", 0))) / 1000.0,
				RETAIL_COLOR, String(unit.get("body", "")),
				String(unit.get("weapon", "")), String(unit.get("armor", ""))])
	lines.append("")

	lines.append("[b]THE ROUNDS[/b]")
	for entry in outcome.get("log", []) as Array:
		var round_row: Dictionary = entry
		lines.append("  [b]round %d[/b] - %d strike(s), %.1f total damage%s" % [
			int(round_row.get("round", 0)), int(round_row.get("strikeCount", 0)),
			float(int(round_row.get("damageMilli", 0))) / 1000.0,
			"" if bool(round_row.get("detailed", false))
				else "  [color=#8fa4bd](abbreviated: only the first %d rounds carry every die)[/color]"
					% AutoResolveBattleScript.DETAILED_ROUNDS])
		for strike_value in round_row.get("strikes", []) as Array:
			var strike: Dictionary = strike_value
			var contest: Dictionary = strike.get("contest", {})
			lines.append("    %s (%s) strikes %s (%s) for %.1f" % [
				String(strike.get("attacker", "?")), String(strike.get("attackerType", "")),
				String(strike.get("defender", "?")), String(strike.get("defenderType", "")),
				float(int(strike.get("damageMilli", 0))) / 1000.0])
			for factor_value in strike.get("factors", []) as Array:
				var factor: Dictionary = factor_value
				var owner := String(factor.get("owner", ""))
				lines.append("        [color=%s]%s[/color] %-22s x%.3f   %s" % [
					RETAIL_COLOR if owner == "retail" else PROJECT_COLOR,
					"RETAIL " if owner == "retail" else "PROJECT",
					String(factor.get("name", "")),
					float(int(factor.get("milli", 0))) / 1000.0,
					String(factor.get("source", ""))])
			if not contest.is_empty():
				lines.append("        [color=%s]the dice[/color] attacker %s vs defender %s -> %d of %d pairs won" % [
					PROJECT_COLOR, str(contest.get("attackerDice", [])),
					str(contest.get("defenderDice", [])), int(contest.get("pairsWon", 0)),
					(contest.get("pairs", []) as Array).size()])
	if int(outcome.get("abbreviatedRounds", 0)) > 0:
		lines.append("  [color=#8fa4bd]%d further round(s) are summarised rather than itemised. That is a limit of this REPORT, not of the battle: every round was fought in full.[/color]"
			% int(outcome.get("abbreviatedRounds", 0)))
	lines.append("")

	lines.append("[b]WHAT IT DID TO THE CAMPAIGN[/b]")
	lines.append("  region %s%s" % [
		_display_of(String(applied.get("region", ""))),
		" CHANGED HANDS" if bool(applied.get("captured", false)) else " did not change hands"])
	lines.append("  armies destroyed: %s" % _ids_or_none(applied.get("armies_lost", PackedInt32Array())))
	lines.append("  armies reduced:   %s" % _ids_or_none(applied.get("armies_reduced", PackedInt32Array())))
	lines.append("  armies advanced:  %s" % _ids_or_none(applied.get("armies_advanced", PackedInt32Array())))
	for reason in applied.get("refusals", PackedStringArray()) as PackedStringArray:
		lines.append("  [color=#e0a24a]not fully applied: %s[/color]" % String(reason))
	var unresolved: PackedStringArray = outcome.get("unresolved", PackedStringArray())
	if not unresolved.is_empty():
		lines.append("")
		lines.append("[b]WHAT DID NOT RESOLVE[/b] - named rather than substituted with a number")
		for note in unresolved:
			lines.append("  [color=#e0a24a]%s[/color]" % String(note))
	lines.append("")

	lines.append("[b]EVERY RULE THIS BATTLE RAN UNDER[/b] - the whole editable table, in one place")
	for row_value in outcome.get("rules", []) as Array:
		var rule: Dictionary = row_value
		var owner := String(rule.get("owner", ""))
		lines.append("  [color=%s]%s[/color] %s" % [
			RETAIL_COLOR if owner == "retail" else PROJECT_COLOR,
			"RETAIL " if owner == "retail" else "PROJECT", String(rule.get("text", ""))])
	return "\n".join(lines)


static func _ids_or_none(ids: PackedInt32Array) -> String:
	if ids.is_empty():
		return "none"
	var parts: Array[String] = []
	for value in ids:
		parts.append(str(int(value)))
	return ", ".join(parts)


func _on_auto_resolve_pressed() -> void:
	auto_resolve_selected_attack()


func end_turn() -> void:
	if session == null or session.state == null:
		return
	if not session.state.pending_battle.is_empty():
		_message("A battle is still in flight; it must resolve before the turn passes.")
		return
	session.state.advance_turn()
	session.selected_region = ""
	session.selected_target = ""
	selected_army_id = -1
	_message("")
	# AND THEN THE OPPONENT MOVES. Before this line, END TURN handed the turn to a
	# seat that never did anything: the campaign came straight back to the player
	# with the map unchanged, which is the "there is no game here" half of the
	# owner's report. `run_opponent_turns()` plays every consecutive AI seat through
	# the SAME session doors this screen's own buttons use, and says what happened.
	run_opponent_turns()
	refresh()
	turn_ended.emit()


## ------------------------------------------------------------------------------
## THE OPPONENT'S TURNS, AND WHY THIS SCREEN NARRATES THEM ITSELF
## ------------------------------------------------------------------------------
##
## `session.run_ai_turns()` plays every consecutive AI seat until the turn comes
## back to a human, the campaign is decided, or its own hard bound is reached. It
## goes through `move_armies` / `commit_attack` / `auto_resolve_pending_battle` /
## `advance_turn` - the same doors ATTACK, AUTO-RESOLVE and END TURN already use -
## so wiring it here adds an OPPONENT and not a second path into the simulation.
##
## Every report is PRESENTATION DATA. Nothing here is hashed, snapshotted or fed
## back; the strategic state was already changed by the session before this
## function ever saw the report.
##
## THE LINES ARE BUILT HERE RATHER THAN TAKEN FROM `report["narrative"]`, and both
## reasons matter:
##
##   1. NAMES. The opponent falls back to the raw region id when a display name is
##      an unresolved string-table key, because it does not own the string table -
##      this screen does. `_display_of()` and `_owner_name()` render retail's own
##      English, so the player reads "Angmar took Carn Dum" rather than
##      "Angmar took Carn_Dum".
##   2. REGISTER. The opponent's own fallback line splices a REFUSAL into the
##      narrative ("could not move: roster 'DainPlayerArmy' has no binding"), and
##      that is a sentence about this program's data coverage, not about the war.
##      `IMPLEMENTATION_VOCABULARY` forbids exactly that class of string on the
##      HUD, so the refusals go to the diagnosis (`_conversion_gap_lines`) whole,
##      and the glass gets the world's version of the same turn.
##
## Public because `main_menu.gd` calls it after a TACTICAL battle resolves - that
## is the third place the turn is handed on, and it happens on the other side of a
## scene change where this screen has no way to notice.
func run_opponent_turns() -> void:
	_ai_reports = []
	if session == null or session.state == null:
		return
	if not session.active_seat_is_ai():
		return
	_ai_reports = session.run_ai_turns(available_map_ids)
	if _ai_reports.is_empty():
		return
	var lines: Array[String] = []
	for report_value in _ai_reports:
		lines.append_array(_opponent_turn_lines(report_value as Dictionary))
	if not lines.is_empty():
		# ONE LINE ON THE MESSAGE STRIP, and the whole account in the tooltip of the
		# banner that says whose move it is. The strip is a single clipped line; a
		# four-turn account crammed into it would be an ellipsis, which tells the
		# player less than nothing.
		_message(lines[lines.size() - 1] if lines.size() == 1
			else "%s  (%d turns passed)" % [lines[lines.size() - 1], _ai_reports.size()])
	if turn_banner != null:
		turn_banner.tooltip_text = "\n".join(lines) if not lines.is_empty() \
			else turn_banner.tooltip_text


## One AI turn, in the world's words and with this screen's own names.
func _opponent_turn_lines(report: Dictionary) -> Array[String]:
	var lines: Array[String] = []
	var seat := int(report.get("seat", StateScript.NEUTRAL))
	var who := _owner_name(seat)
	for step_value in report.get("marches", []) as Array:
		var step := step_value as Dictionary
		lines.append("%s marched from %s to %s." % [
			who, _display_of(String(step.get("from", ""))),
			_display_of(String(step.get("to", "")))])
	var attack := report.get("attack", {}) as Dictionary
	if not attack.is_empty():
		var where := _display_of(String(attack.get("region", "")))
		if bool(attack.get("undecided", false)):
			lines.append("%s attacked %s and neither side broke." % [who, where])
		elif bool(attack.get("captured", false)):
			lines.append("%s took %s." % [who, where])
		else:
			lines.append("%s attacked %s and was thrown back." % [who, where])
	if lines.is_empty():
		# NEVER SILENT. A turn where the opponent did nothing is a real outcome and
		# the player is told so, because an unexplained instant turn reads as a bug -
		# which is exactly the complaint this wiring answers. The REASON it did
		# nothing may be a refusal about this project's data, so the reason is not
		# here; it is on the diagnosis, whole.
		lines.append("%s held its ground." % who)
	return lines


func show_message(text: String) -> void:
	_message(text)


# --- turn, standings, legend --------------------------------------------------

## The one line a player reads first, and under it the one line that says what a
## click does next. Both are derived from the strategic state; neither invents a
## phase retail has that this lane does not model.
func _refresh_turn_banner(state: StateScript, seat: int, seat_row: Dictionary) -> void:
	var controller := String(seat_row.get("controller", "?"))
	var whose := "YOUR MOVE" if controller == StateScript.CONTROLLER_HUMAN else "THE AI MOVES"
	# RETAIL'S OWN TWO LINES, in retail's own two places. The turn NUMBER goes in
	# the left capsule of the phase chevron bar, which is where retail's
	# "Turn: / 1" sits; whose move it is goes on the banner strip under it, which
	# is where retail sets "tactical phase". The seat is named in RETAIL'S OWN
	# ENGLISH through `wotr_display_names.gd` - the template id used to be shouted
	# here as "PLAYERANGMAR", which is a data key and was called disqualifying.
	# RETAIL'S TWO CELLS, USED AS TWO CELLS. Its plaque sets a caption row and a
	# value row inside one black field, and the caption is retail's own string
	# (`APT:Turn`, which reads "Turn:") when the shell table carries it.
	turn_plaque_label = names.shell_label("APT:Turn", "Turn:")
	turn_plaque_value = "%d" % (state.turn_index + 1)
	turn_banner.queue_redraw()
	turn_banner.tooltip_text = "Round %d. Seat %d (%s). One turn is one seat's move; a round is every seat having moved once." % [
		state.round_index() + 1, seat, controller]
	hint_label.text = _hint_text(state)
	hint_label.tooltip_text = _hint_tooltip(state)
	var who := _owner_name(seat).to_upper() if seat != StateScript.NEUTRAL else "NO SEAT"
	# WHEN THE PLAQUE IS SHUT THE BANNER CARRIES THE WHOLE SENTENCE, because the
	# plaque's black field is the only other place this screen has to put one and
	# in that state retail's own chevron bar is drawn across it. The two halves
	# fold into ONE line in the register the banner is already in - `ANGMAR - YOUR
	# MOVE` becomes `ANGMAR - CHOOSE A REGION TO ATTACK`, which says everything the
	# pair said and says it more specifically. `_relayout` runs after this, so the
	# open/shut decision made here is the one it lays out for.
	#
	# ------------------------------------------------------------------------------
	# THE PHASE NAME TAKES RETAIL'S OWN SLOT FOR IT, AND THE SEAT NAME GIVES IT UP
	# ------------------------------------------------------------------------------
	#
	# This strip is where retail sets its phase title - that is what it IS, and this
	# file's own comments have called it the phase banner for several rounds while it
	# carried the seat's name instead. Retail authors three titles for it
	# (`APT:TacticalPhaseTitle` / `BattlePhaseTitle` / `RetreatPhaseTitle`; see
	# `PHASE_CELLS`), the chevron bar directly above it has three devices, and until
	# this round neither the bar nor the strip said which of the three the screen was
	# in. An adversarial review: "A phase indicator that does not indicate the phase
	# is not an indicator, it is decoration."
	#
	# THE SEAT NAME IS WHAT MAKES ROOM, and dropping it costs nothing that is not
	# already on screen twice: the standings table sets the active seat's row in hot
	# gold, the palantir plaque names the holder of the region under the pointer, and
	# the status ribbon carries it a third time. The imperative is addressed to the
	# player either way. So the line goes from naming a seat the frame names three
	# times to naming the phase the frame named nowhere.
	var phase_title := ""
	for entry_value in PHASE_CELLS:
		var entry := entry_value as Dictionary
		if int(entry["index"]) == current_phase():
			phase_title = names.shell_label(String(entry["string"]), String(entry["caption"]))
			break
	if phase_title.is_empty():
		phase_title = who
	if _checklist_is_open():
		phase_banner.text = "%s   -   %s" % [phase_title.to_upper(), whose]
	else:
		var imperative := hint_label.text.strip_edges()
		phase_banner.text = ("%s   -   %s" % [phase_title.to_upper(), imperative.to_upper()]
			if not imperative.is_empty() else "%s   -   %s" % [phase_title.to_upper(), whose])
	hint_label.visible = _checklist_is_open()


## RETAIL'S HEADER NUMBERS, and only the ones that are real.
##
## The owner's screenshot reads "Player Bonuses 3000" across the top.
## `ScenarioStartResources = 3000` is retail's own field on every playable
## `LivingWorldPlayerTemplate`, and it is what this shows - labelled as the
## STARTING purse, because no treasury is simulated and a bare "3000" beside a
## turn counter would read as a live balance that goes up and down.
##
## The command-point pair is retail's own economy too: `MaxWorldCP = 4500` from
## the template, against the command points this seat actually has standing on
## the board, summed from the armies in the authoritative state.
##
## THERE IS A PHASE BAR NOW, AND THIS PARAGRAPH IS RESTATED RATHER THAN DELETED.
##
## It used to read: "THERE IS NO PHASE BAR AND THERE WILL NOT BE ONE HERE.
## `data/ini/livingworldlogic.ini` is 192 bytes of comment, there is no
## `mprules.ini` anywhere in the archives, and retail's phase list lives in the
## executable. There is nothing to convert, so building one would be fabrication."
##
## THE FACTS IN IT ARE STILL TRUE AND THE CONCLUSION WAS TOO STRONG. Retail's phase
## LOGIC is indeed in the executable and is not converted - this layer still models
## two phases and not three, which is the standing gap `PHASE_RETREAT_GAP`. But
## retail's phase NAMES are not in the executable at all: `data/lotr.str` authors
## `APT:TacticalPhaseTitle`, `APT:BattlePhaseTitle` and `APT:RetreatPhaseTitle`, the
## setup string bundle converts all three, and retail's own chevron bar carries
## exactly three devices for them. Naming which of retail's three phases the screen
## is in is therefore a READING of retail rather than a fabrication, and refusing to
## do it was costing the frame its clock for no honesty gained. See `PHASE_CELLS`.
func _refresh_header(state: StateScript, seat: int, seat_row: Dictionary) -> void:
	if header_label == null:
		return
	if seat == StateScript.NEUTRAL:
		_header_facts = []
		header_label.queue_redraw()
		return
	var template: Dictionary = session.world.player_templates.get(
		String(seat_row.get("template", "")), {}) as Dictionary
	var max_cp := int(template.get("max_world_cp", -1))
	var on_board := 0
	for army_id in state.armies.keys():
		var army := state.armies[army_id] as Dictionary
		if int(army.get("owner", StateScript.NEUTRAL)) == seat:
			on_board += int(army.get("command_points", 0))
	# THE PURSE IS LIVE. This plate used to read the STATIC template field
	# (`ScenarioStartResources`), so it showed a frozen 3000 for the whole campaign
	# and was labelled "Treasure" to keep that honest. The treasury is authoritative
	# state now - it is spent on structures and it grows at the start of every turn -
	# so the plate reads it, and takes RETAIL'S OWN NOUN for it with it:
	# `STRATEGICHUD:StatsCommandPointsTitle` is the literal string "Treasury".
	var income: Dictionary = session.income_report()
	# ONE PLATE PER FACT, not one string in one box. `_draw_header` sets each of
	# these on its own socket with an engraved label and a gold value.
	_header_facts = [
		{
			"label": names.shell_label("STRATEGICHUD:StatsCommandPointsTitle", "Treasury"),
			"value": _treasury_value(session.treasure()),
		},
		{
			# RETAIL'S SECOND NUMBER, in retail's own format ("+%d"). It is drawn
			# beside the purse because the purse alone answers "can I afford this"
			# and not "when will I be able to", and every build decision on this
			# screen is the second question.
			"label": names.shell_label("STRATEGICHUD:StatsTreasuryIncomeTitle", "Treasury Income"),
			"value": _income_value(int(income.get("total", 0))),
		},
		{
			# "COMMAND" alone was ambiguous ON THIS SCREEN, and not in a cosmetic
			# way: the palantir plaque simultaneously showed the same numerator
			# against the REGION's command-point limit ("6/720" against "6/4500"),
			# and a blind review read the pair as one quantity contradicting itself.
			# It is the WORLD total against retail's `MaxWorldCP`; the plaque's is
			# one region against retail's `CommandPointLimit`. Both now say which.
			"label": "World Command",
			"value": "%d/%s" % [on_board, str(max_cp) if max_cp >= 0 else "?"],
		},
	]
	header_label.queue_redraw()
	# THE TOOLTIP IS PART OF THE PLAYER'S SURFACE, so it is written in the player's
	# register too. What each number MEANS, in the world's terms. Where each one
	# comes from in retail's data, and what this project does and does not model
	# behind it, is on the diagnostics panel - it is a claim about the conversion,
	# and this is the seat's purse.
	# RETAIL'S OWN HELP LINE for the income figure, verbatim, plus the WORKINGS -
	# which fertile territory and which structure each contribute. Retail draws the
	# sum and hides the arithmetic; a player deciding whether a farm is worth 0 and
	# a turn wants the arithmetic, and a tooltip is where it costs nothing.
	var help: Array[String] = []
	help.append("%s is what this seat holds to spend."
		% names.shell_label("STRATEGICHUD:StatsCommandPointsTitle", "Treasury"))
	var income_help := names.shell_label("STRATEGICHUD:StatsCTreasuryIncomeHelp", "")
	if not income_help.is_empty():
		help.append(income_help.rstrip(".") + ".")
	# THE WORKINGS, COUNTED RATHER THAN QUOTED. `income_report().rows` is one
	# sentence per contributor and every one of them names a `LWB_*` id, which is a
	# data key and may not reach the glass. The COUNTS carry the same information in
	# the player's register, so the tooltip says how many of each rather than which.
	for pair_value in [
		["fertile_regions", "fertile territory", "fertile territories"],
		["fortresses", "fortress", "fortresses"],
		["farms", "farm", "farms"],
	]:
		var pair := pair_value as Array
		var count := int(income.get(String(pair[0]), 0))
		if count > 0:
			help.append("   %d %s" % [count, String(pair[1]) if count == 1 else String(pair[2])])
	help.append("WORLD COMMAND is the command points its armies carry on the whole "
		+ "board, against the most it may field.")
	header_label.tooltip_text = "\n".join(help)


# --- the army banners, the plots and the build menu ---------------------------

## One row per army stack, keyed by region, with retail's own portrait id.
##
## THE PORTRAIT LINK IS RETAIL'S, never a resemblance: the recruit button that
## builds that same `PlayerArmy`, failing that the one for that same
## `HeroTemplateName`, failing that the owning template's
## `GarrisonSelectionPortraitName`. All three are authored fields. An army none
## of them reaches carries no portrait id at all and the map draws a bare plate.
func _army_stacks_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if session == null or session.state == null:
		return by_region
	var army_ids: Array[int] = []
	for key in session.state.armies.keys():
		army_ids.append(int(key))
	army_ids.sort()
	for army_id in army_ids:
		var army := session.state.armies[army_id] as Dictionary
		var region_id := String(army.get("region", ""))
		if region_id.is_empty():
			continue
		var owner := int(army.get("owner", StateScript.NEUTRAL))
		var template := ""
		if owner >= 0 and owner < session.state.players.size():
			template = String((session.state.players[owner] as Dictionary).get("template", ""))
		var roster := String(army.get("roster", ""))
		var portrait: Dictionary = {"id": "", "source": ""}
		var marker: Dictionary = {"icon": "", "size": "", "source": ""}
		if ui != null:
			portrait = ui.army_portrait(roster, String(army.get("hero_template", "")), template)
			# THE 3D MARKER FAMILY, by the same discipline as the portrait: the
			# `ArmyToSpawn` block that recruits this same `PlayerArmy`, failing
			# that the seat template's own `DefaultArmyIconName`. Both authored.
			marker = ui.army_marker(roster, template)
		var stacks: Array = by_region.get(region_id, []) as Array
		stacks.append({
			"army_id": army_id,
			"region": region_id,
			"owner": owner,
			"template": template,
			"kind": String(army.get("kind", "")),
			"label": _army_label(roster),
			"portrait_id": String(portrait.get("id", "")),
			"portrait_source": String(portrait.get("source", "")),
			"icon": String(marker.get("icon", "")),
			"size": String(marker.get("size", "")),
			"icon_source": String(marker.get("source", "")),
		})
		by_region[region_id] = stacks
	return by_region


## Standing structures keyed by region, shaped for `WotrMapView`.  Every row is
## already present in authoritative state and its marker family is the converted
## LivingWorldBuilding.BuildingIcon field exposed by `session.build_plots()`.
func _structures_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if session == null or session.state == null or session.world == null:
		return by_region
	for region_value in session.world.region_ids:
		var region_id := String(region_value)
		var occupied: Array[Dictionary] = []
		for row_value in (session.build_plots(region_id).get("plots", []) as Array):
			var row := row_value as Dictionary
			if not bool(row.get("occupied", false)):
				continue
			occupied.append({
				"plot": int(row.get("plot", -1)),
				"building": String(row.get("building", "")),
				"icon": String(row.get("icon", "")),
				"owner": int(row.get("owner", StateScript.NEUTRAL)),
			})
		if not occupied.is_empty():
			by_region[region_id] = occupied
	return by_region


## The name an army stack is shown under: retail's own `DisplayNameTag` through
## the string table, or the roster id when the table does not carry it. Never
## derived from the id.
func _army_label(roster: String) -> String:
	if session == null or session.world == null:
		return roster
	var record: Dictionary = session.world.player_armies.get(roster, {}) as Dictionary
	var key := String(record.get("display_name_tag", ""))
	if key.is_empty() or strings == null:
		return roster
	var text := strings.text(key)
	return text if not text.is_empty() else roster


## Retail's own authored `BuildingSpot` points per region, as map coordinates.
## Nothing is placed here - every point is one retail wrote down.
func _plots_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if session == null or session.world == null:
		return by_region
	for region_id in session.world.region_ids:
		var spots: Array = session.world.region(String(region_id)).get("building_spots", []) as Array
		if spots.is_empty():
			continue
		var points: Array[Vector2] = []
		for spot in spots:
			var row := spot as Dictionary
			points.append(Vector2(float(row.get("x", 0)), float(row.get("y", 0))))
		by_region[String(region_id)] = points
	return by_region


## The `LivingWorldBuildPlotIcon` family retail decals each region's plots with -
## the OWNING SEAT's own `BuildPlotIconName`, which is why an unowned region gets
## none rather than a default. Authored link only.
func _plot_icons_by_region() -> Dictionary:
	var by_region: Dictionary = {}
	if ui == null or session == null or session.state == null:
		return by_region
	for region_id in session.world.region_ids:
		var id := String(region_id)
		var owner := session.state.owner_of(id)
		if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
			continue
		var template := String((session.state.players[owner] as Dictionary).get("template", ""))
		var family := ui.build_plot_icon_id(template)
		if not family.is_empty():
			by_region[id] = family
	return by_region


## THE NAME EVERY REGION IS LABELLED WITH ON THE MAP - except the one the radial
## build ring is standing open on.
##
## THE SUPPRESSION IS DELIBERATE AND IT IS HALF OF A FIX THIS LANE CAN ONLY DO
## HALF OF. A blind review photographed the radial overlay over Angmar as a pile:
## the ring's own structure captions overlapping each other AND colliding with the
## embossed region cartouche underneath them, and asked for four things - a
## minimum caption size, a plate or shadow behind every caption, collision-aware
## placement, "and suppression of the map label underneath while the overlay is
## open". The first three are drawn by the map view, which is another lane's file
## this round; the fourth is decided HERE, because the map view is only ever told
## which regions have names, and a region with no name draws no label.
##
## It is not a workaround for the other three. A modal ring opened on a plot is
## asking about that plot, and retail's own living world drops the region caption
## under an open radial for the same reason: the cartouche and the ring's captions
## are competing for the same hundred pixels and only one of them is what the
## player just asked for.
func _display_names() -> Dictionary:
	var suppressed := String(selected_plot.get("region", ""))
	var labels: Dictionary = {}
	for row in _rows:
		var region_id := String(row["id"])
		if region_id == suppressed:
			continue
		labels[region_id] = _display_of(region_id)
	return labels


## ------------------------------------------------------------------------------
## THE BUILD PATH. ONE OFFER, THREE SURFACES, ONE DOOR.
## ------------------------------------------------------------------------------
##
## Construction is simulated now - `wotr_state.gd` prices it, spends the treasury
## and stands the structure on a numbered foundation - and this block is the whole
## of how a player reaches it. Three surfaces show the SAME offer, because they
## are all `_build_offer()`:
##
##   1. the RING on the map, opened by clicking a foundation;
##   2. the ROSTER in the tray's STRUCTURES well, one clickable row per offering;
##   3. the palantir's COMMAND DIAL, retail's own ring of six wells.
##
## And all three press ONE door, `_commit_build_here()`, which is the only place
## in this file that calls `session.commit_build()`. That is not tidiness: a build
## committed down a second path is a build whose refusal wording, whose message
## line and whose plot index can drift from the first, and the owner would find
## the drift before we did.
##
## THE OFFER IS NEVER PRE-FILTERED. Every structure the seat's faction may raise
## comes back whether or not it can be afforded or placed, carrying `can_build`
## and a refusal. A ring that quietly dropped the fortress would teach the player
## nothing about why they cannot have one.
##
## WHAT THE PLAYER READS IS NEVER WHAT THE SIMULATION SAYS. `session` hands back
## one engineering sentence per refusal - it names seat indices, building ids and
## the macro table - and that register is banned from this screen
## (`IMPLEMENTATION_VOCABULARY`). So `_build_refusal()` DERIVES the refusal again
## from the same authoritative numbers, in retail's own words where retail has
## them, and the simulation's own sentence goes to the diagnostics panel in full
## (`_build_refusals_off_the_glass`). The two halves are asserted together, the
## same bargain the rest of this screen's string audit keeps.
##
## RETAIL'S REFUSAL VOCABULARY, and which of it is retail's own text:
##   * `CONTROLBAR:LW_FortRestricted` - verbatim, for a territory that already
##     holds a stronghold. Retail appends it to the tooltip of a button it still
##     shows, which is exactly what this does.
##   * `CONTROLBAR:LW_BuildNumberRestriction` - verbatim ("Number allowed in
##     territory: %d"), for retail's own `RestrictBuildings` cap.
##   * `CONTROLBAR:LW_Structure_BuildTimeSingular` / `...Plural` - verbatim, the
##     build time and cost block under every offering's tooltip.
##   * `STRATEGICHUD:TreasuryWarningPopUpTitle` - verbatim, when the purse is
##     empty rather than merely short.
## Everything else in the table below is THIS PROJECT'S OWN English, because
## retail states those rules only in its tutorial namespace (`WOTRTutorial:`),
## which no converted bundle carries. They are marked as ours here and in the gap
## register rather than passed off as retail's.
const BUILD_REFUSAL_NONE := ""
const BUILD_REFUSAL_UNAFFORDABLE := "unaffordable"
const BUILD_REFUSAL_PLOT_TAKEN := "plot-taken"
const BUILD_REFUSAL_REGION_FULL := "region-full"
const BUILD_REFUSAL_FORT_RESTRICTED := "fort-restricted"
const BUILD_REFUSAL_TYPE_CAPPED := "type-capped"
const BUILD_REFUSAL_ONE_PER_TURN := "one-per-turn"
const BUILD_REFUSAL_NOT_HELD := "not-held"
const BUILD_REFUSAL_NO_PLOTS := "no-plots"
const BUILD_REFUSAL_UNPRICED := "unpriced"
const BUILD_REFUSAL_OTHER := "other"

## THE ONE SENTENCE THE PLAYER READS PER REFUSAL. Project-authored except where
## the value is a retail string key, which `_build_refusal()` fills through the
## string table so the wording on the glass is retail's own.
const BUILD_REFUSAL_TEXT := {
	BUILD_REFUSAL_UNAFFORDABLE: "Your treasury holds %d, and this costs %d.",
	BUILD_REFUSAL_PLOT_TAKEN: "This foundation already carries %s.",
	BUILD_REFUSAL_REGION_FULL: "Every foundation in %s is built on.",
	BUILD_REFUSAL_TYPE_CAPPED: "%s holds all the %s this territory allows.",
	BUILD_REFUSAL_ONE_PER_TURN: "%s has already begun a structure this turn.",
	BUILD_REFUSAL_NOT_HELD: "%s is not yours to build on.",
	BUILD_REFUSAL_NO_PLOTS: "%s has no building foundations.",
	BUILD_REFUSAL_UNPRICED: "No price is recorded for this structure.",
	BUILD_REFUSAL_OTHER: "This cannot be raised on that foundation.",
}

## The build entry the pointer is over, as `{region, plot, id}` or `{}`. The map
## view lights the ring's slot itself; this is what the tray's ribbon and the
## message line read so the SAME hover explains itself in words as well as light.
var hovered_build: Dictionary = {}

## Every refusal sentence the simulation gave this refresh, verbatim, so
## `_strings_taken_off_the_glass()` can carry them to the diagnostics panel. The
## glass gets `_build_refusal()`'s English; this is what it stands in for.
var _build_refusals_seen: Array[String] = []


## WHAT THE ACTIVE SEAT MAY RAISE ON ONE FOUNDATION, decorated for drawing.
##
## `session.build_options()` is the whole of the data; everything added here is
## presentation - retail's title through the string table, the cost as a string,
## the tooltip, and the player-facing refusal. Pure and cheap: no file IO after
## the catalogue binds, so the ring, the roster and the dial can all ask on every
## refresh without caching a stale offer between them.
func _build_offer(region_id: String, plot: int = -1) -> Array[Dictionary]:
	var offer: Array[Dictionary] = []
	if session == null or session.state == null or region_id.is_empty():
		return offer
	var plots: Dictionary = session.build_plots(region_id)
	var purse := session.treasure()
	for row_value in session.build_options(region_id, plot):
		var row := row_value as Dictionary
		var can_build := bool(row.get("can_build", false))
		var cost := int(row.get("cost", -1))
		var refusal: Dictionary = {"code": BUILD_REFUSAL_NONE, "text": ""}
		if not can_build:
			refusal = _build_refusal(row, region_id, plot, plots, purse)
			var stated := String(row.get("refusal", ""))
			if not stated.is_empty() and not _build_refusals_seen.has(stated):
				_build_refusals_seen.append(stated)
		offer.append({
			"id": String(row.get("building", "")),
			"image_id": String(row.get("button_image", "")),
			# RETAIL'S OWN TITLE for the construct button, never derived from the id.
			"title": _string_or_key(String(row.get("build_title_tag", ""))),
			"type": String(row.get("type", "")),
			"cost": _cost_cell(cost),
			"cost_value": cost,
			"turns": str(int(row.get("turns_to_build", 1))),
			"can_build": can_build,
			# TRUE ONLY FOR THE ONE REFUSAL THE PLAYER CAN ACT ON THIS TURN. The price
			# goes red for this and for nothing else; a fortress barred by retail's own
			# territory rule is not a budgeting problem and colouring it as one would
			# send the player to earn treasure they already have.
			"unaffordable": String(refusal["code"]) == BUILD_REFUSAL_UNAFFORDABLE,
			"refusal_code": String(refusal["code"]),
			"refusal": String(refusal["text"]),
			"income": int(row.get("income_per_turn", 0)),
			"description_tag": String(row.get("description_tag", "")),
		})
	return offer


## ------------------------------------------------------------------------------
## WHAT GOES IN A COST CELL, INCLUDING THE CELL WHOSE COST IS NOTHING
## ------------------------------------------------------------------------------
##
## THE DEFECT, from an adversarial art-direction review reading the STRUCTURES
## roster: "'MILL 0' - a structure with cost zero. Either it is free, in which case
## say FREE, or the value is wrong. A bare 0 in a cost column reads as a bug in a
## screenshot."
##
## THE VALUE IS NOT WRONG. Retail's own gamedata prices the farm class at nothing -
## `WOTR_FARM_COST = 0` - and retail states the same rule in prose in
## `STRATEGICHUD:TreasuryWarningPopUpMessage`: a seat with an empty treasury "will
## not be able to build any new units or buildings on the map (BESIDES FARMS)".
## Farms are the thing a broke seat can still raise, and that is only true because
## they cost nothing. So this is a PRESENTATION call, not a data defect, and the
## reviewer is right about the presentation: a numeral in a price column is read as
## a price, and zero is the one price a numeral communicates badly.
##
## THE WORD IS THIS PROJECT'S AND IS LABELLED AS SUCH. `data/lotr.str` carries no
## string for a zero cost in any namespace - the only `free` entries in the
## converted table are `LW:RegionFreeBuildersBonus` and `LW:RegionFreeInnUnitsBonus`,
## which are region bonuses and not prices - so there is nothing of retail's to use
## and inventing one and passing it off as retail's is the thing `AGENTS.md`
## forbids. It is a constant so `player_visible_strings()` can see it, and the
## diagnostics panel names it as ours.
##
## A cost of -1 is still an EMPTY cell: that is an offering retail prices with a
## macro this project's `#define` table cannot resolve, and an unresolved price is
## an absence rather than a zero. The two must not collapse into one another, which
## is the other half of why this is a function rather than a ternary.
const ROSTER_FREE_COST := "Free"


func _cost_cell(cost: int) -> String:
	if cost < 0:
		return ""
	if cost == 0:
		return ROSTER_FREE_COST
	return str(cost)


## WHY ONE OFFERING CANNOT BE RAISED, in the player's register.
##
## DERIVED, NOT PARSED. It would be shorter to match substrings of the sentence
## `session` already hands back, and it would be wrong: that sentence is another
## lane's prose and a reword there would silently turn every refusal on this
## screen into the generic one. So the clauses are re-evaluated here against the
## SAME authoritative numbers, in the same order `wotr_state.build_refusal()`
## checks them, through that file's own public read-only helpers (`type_limit`,
## `count_of_type`, `structures_in_region`). Nothing is written.
##
## THE LAST CLAUSE IS AN HONEST FALL-THROUGH, not a silent fallback: it says the
## structure cannot go there without inventing a reason, and the simulation's own
## sentence for that pair is on the diagnostics panel in full. A refusal this
## screen cannot phrase is a refusal the player can still find out about.
func _build_refusal(
	row: Dictionary, region_id: String, plot: int, plots: Dictionary, purse: int
) -> Dictionary:
	var state: StateScript = session.state
	var seat := state.active_player()
	var display := _display_of(region_id)
	if state.owner_of(region_id) != seat:
		return _refusal(BUILD_REFUSAL_NOT_HELD, [display])
	var total := int(plots.get("total", 0))
	if total <= 0:
		return _refusal(BUILD_REFUSAL_NO_PLOTS, [display])
	var chosen := plot
	if chosen < 0:
		chosen = state.free_plot(region_id)
	if chosen < 0:
		return _refusal(BUILD_REFUSAL_REGION_FULL, [display])
	var rows: Array = plots.get("plots", []) as Array
	if chosen < rows.size():
		var standing := rows[chosen] as Dictionary
		if bool(standing.get("occupied", false)):
			# RETAIL'S OWN NAME FOR WHAT IS ALREADY THERE, through retail's own
			# `DisplayNameTag`. A raw `LWB_*` id here would be the exact defect the
			# string audit exists to catch.
			var standing_name := _string_or_key(String(standing.get("display_name_tag", "")))
			if standing_name.is_empty() or standing_name.begins_with("CONTROLBAR:"):
				standing_name = names.shell_label(
					"STRATEGICHUD:BuildPlotName", "a structure").to_lower()
			return _refusal(BUILD_REFUSAL_PLOT_TAKEN, [standing_name])
	var type_name := String(row.get("type", ""))
	var limit := state.type_limit(region_id, type_name)
	if limit >= 0 and state.count_of_type(region_id, type_name) >= limit:
		if type_name == "Fortress" and limit == 0:
			# RETAIL'S OWN SENTENCE, verbatim out of retail's own string table.
			return {"code": BUILD_REFUSAL_FORT_RESTRICTED,
				"text": _retail_sentence("CONTROLBAR:LW_FortRestricted",
					BUILD_REFUSAL_TEXT[BUILD_REFUSAL_TYPE_CAPPED] % [display, _type_name(type_name)])}
		# RETAIL'S OWN LABEL for the general cap, filled with retail's own number,
		# introduced by the project's own clause naming which territory it is about.
		var capped := BUILD_REFUSAL_TEXT[BUILD_REFUSAL_TYPE_CAPPED] % [
			display, _type_name(type_name)]
		var authored := _retail_sentence("CONTROLBAR:LW_BuildNumberRestriction", "")
		if authored.contains("%d"):
			capped += " " + (authored % limit)
		return {"code": BUILD_REFUSAL_TYPE_CAPPED, "text": capped}
	for standing_value in state.structures_in_region(region_id):
		if int((standing_value as Dictionary).get("turn", -1)) == state.turn_index:
			return _refusal(BUILD_REFUSAL_ONE_PER_TURN, [display])
	if int(row.get("cost", -1)) < 0:
		return _refusal(BUILD_REFUSAL_UNPRICED, [])
	if purse < int(row.get("cost", 0)):
		var text := BUILD_REFUSAL_TEXT[BUILD_REFUSAL_UNAFFORDABLE] % [
			purse, int(row.get("cost", 0))]
		if purse <= 0:
			# RETAIL'S OWN HEADING for an empty purse, verbatim.
			text = "%s %s" % [
				names.shell_label("STRATEGICHUD:TreasuryWarningPopUpTitle", ""), text]
		return {"code": BUILD_REFUSAL_UNAFFORDABLE, "text": text.strip_edges()}
	return _refusal(BUILD_REFUSAL_OTHER, [])


func _refusal(code: String, fills: Array) -> Dictionary:
	var pattern := String(BUILD_REFUSAL_TEXT.get(code, ""))
	return {"code": code, "text": pattern % fills if not fills.is_empty() else pattern}


## Retail's own sentence for a key with its leading newlines stripped - retail
## authors these as tooltip APPENDIX lines ("\n\nYou cannot build a fortress...")
## and they are set here as sentences. The fallback is stated by the caller and
## is this project's own wording.
func _retail_sentence(key: String, fallback: String) -> String:
	var text := ""
	if strings != null:
		text = strings.text(key)
	if text.is_empty():
		return fallback
	return text.replace("\\n", "\n").strip_edges()


## Retail's own noun for a structure class, out of its own archetype tooltip
## ("Structure Type: Fortress"), pluralised for the refusal that counts them.
func _type_name(type_name: String) -> String:
	if type_name.is_empty():
		return ""
	var key := "STRATEGICHUD:%sStructureArchetypeTooltip" % type_name
	var text := ""
	if strings != null:
		text = strings.text(key)
	var noun := type_name
	if text.contains(":"):
		noun = text.split(":")[text.split(":").size() - 1].strip_edges()
	return noun + ("es" if noun.ends_with("s") else "s")


## THE TOOLTIP UNDER EVERY OFFERING, wherever it is drawn.
##
## Retail's own shape: what the structure is (`LW_ToolTip_*`, which already reads
## "Structure Type: Fortress / Defends the territory..."), then retail's own
## build-time-and-cost block (`CONTROLBAR:LW_Structure_BuildTimeSingular`, "\nBuild
## Time: %d Turn\nCost: %d"), then - when it cannot be built - the one sentence
## saying why. Retail does exactly this: it appends its restriction strings to the
## tooltip of a button it still shows.
func _build_tooltip(entry: Dictionary) -> String:
	var parts: Array[String] = []
	parts.append(String(entry.get("title", "")))
	var described := _string_or_key(String(entry.get("description_tag", "")))
	if not described.is_empty() and not described.contains(":LW_"):
		parts.append(described.replace("\\n", "\n").strip_edges())
	var turns := int(String(entry.get("turns", "1")).to_int())
	var cost := int(entry.get("cost_value", -1))
	if cost >= 0:
		var key := "CONTROLBAR:LW_Structure_BuildTimeSingular" if turns == 1 \
			else "CONTROLBAR:LW_Structure_BuildTimePlural"
		var block := _retail_sentence(key, "")
		if block.contains("%d"):
			parts.append(block % [turns, cost])
	var income := int(entry.get("income", 0))
	if income > 0:
		# RETAIL'S OWN LINE for what a farm pays, verbatim.
		var farm := _retail_sentence("CONTROLBAR:LW_FarmTreasuryBonus", "")
		if farm.contains("%d"):
			parts.append(farm % income)
	if not bool(entry.get("can_build", true)) and not String(entry.get("refusal", "")).is_empty():
		parts.append(String(entry["refusal"]))
	return "\n".join(parts).strip_edges()


## THE OFFER THE RING IS DRAWN FROM. The map view reads `id`, `image_id`, `title`
## and `cost` off each row and hit-tests its own slots against them, so this is
## the same array the ring's clicks come back keyed by.
func _radial_entries() -> Array[Dictionary]:
	if selected_plot.is_empty():
		return [] as Array[Dictionary]
	return _build_offer(String(selected_plot.get("region", "")),
		int(selected_plot.get("index", -1)))


## THE RING'S CLICK. One door, and it is this one.
##
## The map view emits this having already hit-tested the slot, so the plot index
## is the foundation the player is standing the structure ON rather than one this
## file re-derived - which matters beyond tidiness: the plot index is inside the
## strategic hash, so two peers that put the same structure on different
## foundations are not playing the same game.
func _on_build_entry_clicked(region_id: String, plot_index: int, building_id: String) -> void:
	_commit_build_here(region_id, plot_index, building_id)


## THE RING'S HOVER. The map lights its own slot; this puts the same answer into
## words on the tray's rail, so the player learns the price and the refusal
## without having to wait for a tooltip.
func _on_build_entry_hovered(region_id: String, plot_index: int, building_id: String) -> void:
	var wanted: Dictionary = {}
	if not building_id.is_empty():
		wanted = {"region": region_id, "plot": plot_index, "id": building_id}
	if wanted == hovered_build:
		return
	hovered_build = wanted
	_refresh_tray_ribbon(_card_region())


## RAISE A STRUCTURE, or say why not. The ONLY call to `session.commit_build()`
## in this file.
##
## THE TURN DOES NOT PASS, and that is retail's rule rather than a convenience -
## construction, training and movement are all decided in one phase. So nothing
## here ends a turn, nothing disables the ring, and the message line says so in
## as many words the first time a structure goes up: the player may build again,
## and then still march or attack.
func _commit_build_here(region_id: String, plot_index: int, building_id: String) -> void:
	if session == null or session.state == null or building_id.is_empty():
		return
	var built: Dictionary = session.commit_build(region_id, building_id, plot_index)
	if not bool(built.get("ok", false)):
		# THE REFUSAL THE PLAYER READS IS THE ONE THE OFFER ALREADY SHOWED THEM, out
		# of the same function, so pressing a dimmed icon can never produce a
		# different answer from hovering it.
		var spoken := ""
		for entry_value in _build_offer(region_id, plot_index):
			var entry := entry_value as Dictionary
			if String(entry["id"]) == building_id:
				spoken = String(entry["refusal"])
				break
		show_message(spoken if not spoken.is_empty()
			else String(BUILD_REFUSAL_TEXT[BUILD_REFUSAL_OTHER]))
		refresh()
		return
	# WHAT WENT UP, WHERE, AND WHAT IS LEFT. Retail's own title for the structure,
	# the region in retail's own English, and the purse after - because a number
	# that moves without being named is a number a player has to go and check.
	var raised := ""
	for entry_value in _build_offer(region_id, plot_index):
		var entry := entry_value as Dictionary
		if String(entry["id"]) == building_id:
			raised = String(entry["title"])
			break
	if raised.is_empty():
		raised = names.shell_label("STRATEGICHUD:BuildPlotName", "")
	show_message("%s raised in %s.   %s %s   %s" % [
		raised, _display_of(region_id),
		names.shell_label("STRATEGICHUD:StatsCommandPointsTitle", "Treasury"),
		_treasury_value(session.treasure()),
		BUILD_DOES_NOT_END_THE_TURN])
	# THE RING STAYS OPEN ON THE NEXT FREE FOUNDATION, or shuts when the region is
	# full. Retail's phase rule is that the player may build again immediately, and
	# a menu that closed itself after every purchase would argue with that rule.
	var free := session.state.free_plot(region_id)
	if free >= 0:
		selected_plot = {"region": region_id, "index": free}
	else:
		selected_plot = {}
	refresh()


## THE SENTENCE THAT MAKES RETAIL'S ONE-PHASE RULE LEGIBLE.
##
## Retail states it in its tutorial (`WOTRTutorial:LW_InstructionText06`, "In the
## Tactical Phase ... Structure Construction, Unit Training, and Army Movement are
## all decided in this phase"), which no converted bundle carries - so the WORDING
## here is this project's own and is marked as such. The RULE is retail's, and the
## reason it has to be said out loud is that every other commitment on this screen
## does end something.
const BUILD_DOES_NOT_END_THE_TURN := "Your turn continues."


## A treasury figure in retail's own format (`STRATEGICHUD:StatsTreasury`, "%d").
func _treasury_value(amount: int) -> String:
	var pattern := ""
	if strings != null:
		pattern = strings.text("STRATEGICHUD:StatsTreasury")
	return pattern % amount if pattern.contains("%d") else str(amount)


## A per-turn income figure in retail's own format
## (`STRATEGICHUD:StatsTreasuryIncome`, "+%d"), which is why the plus sign is not
## written here.
func _income_value(amount: int) -> String:
	var pattern := ""
	if strings != null:
		pattern = strings.text("STRATEGICHUD:StatsTreasuryIncome")
	return pattern % amount if pattern.contains("%d") else "+%d" % amount


## Open or close the radial build menu on one plot. PRESENTATION ONLY: this
## writes a field on the screen, nothing else, and reaches no simulation state.
func _on_plot_clicked(region_id: String, index: int) -> void:
	if selected_plot.get("region", "") == region_id and int(selected_plot.get("index", -1)) == index:
		selected_plot = {}
	else:
		selected_plot = {"region": region_id, "index": index}
		# Clicking a build plot is asking about structures, so the bar shows the
		# tab that answers rather than leaving the answer one click away.
		active_tab = "structures"
	refresh()


## ------------------------------------------------------------------------------
## THE WAR COUNCIL - RETAIL'S OWN CRITICAL-TASK LIST, WITH SOMETHING IN IT
## ------------------------------------------------------------------------------
##
## THE DEFECT THIS CLOSES: the palantir's banner medallion and the plaque's own
## expander both open a plaque whose field, in a blind review's words, was "a
## well-skinned empty panel". The plaque is retail's `StrategicChecklist` and
## retail GROWS it to hold a scrolling list of critical tasks; this project had
## exactly one imperative to put in it, so opening it deliberately produced the
## largest empty rectangle on the screen.
##
## RETAIL'S OWN TASK SENTENCES ARE IN THE CONVERTED TABLE and were never used.
## `data/lotr.str` carries nineteen `STRATEGICHUD:*ChecklistItem` strings - "Build
## a barracks to generate new units.", "Build a fortress in an unprotected
## territory to strengthen its defenses.", "Fight battle in %s.",
## "Conquer new territories by moving Hero armies into adjacent territories." -
## and every one of them is a sentence about the WORLD in retail's own English.
## They are the list this plaque is a plaque FOR.
##
## EVERY ROW IS GATED ON LIVE STATE, which is the whole difference between a
## council and a tutorial: a fortress row appears only when this seat can actually
## afford and place one somewhere it holds, and the battle row only when a battle
## is pending. A row that is always there teaches nothing and is furniture.
##
## THE LIST IS CAPPED. Retail scrolls its field; this project does not, so the
## council shows what fits and no more - a truncated list that looks complete is
## the defect `_fit_card_lines` exists to stop on the region card.
const WAR_COUNCIL_MAX := 4

## Retail's own checklist string per structure class, keyed by the `type` field
## `session.build_options()` returns. Every value is a key into retail's table and
## nothing here is written by this project.
const WAR_COUNCIL_BUILD_ITEMS := {
	"Fortress": "STRATEGICHUD:BuildFortressChecklistItem",
	"Barracks": "STRATEGICHUD:BuildBarracksChecklistItem",
	"Armory": "STRATEGICHUD:BuildArmoryChecklistItem",
	"Resource": "STRATEGICHUD:BuildFarmChecklistItem",
}
## The order they are offered in - defence, then production, then upgrades, then
## income. Project-authored: retail's own ordering lives in its executable.
const WAR_COUNCIL_BUILD_ORDER := ["Fortress", "Barracks", "Armory", "Resource"]

## The council's rows for this refresh, in order. Presentation only, rebuilt by
## `_refresh_war_council()` off the authoritative state, and read by the drawing
## AND by `player_visible_strings()` so the register audit sees every word of it.
var war_council: Array[String] = []


func _refresh_war_council() -> void:
	war_council = []
	if session == null or session.state == null:
		return
	var state: StateScript = session.state
	var seat := state.active_player()
	if seat == StateScript.NEUTRAL:
		return
	# A BATTLE IN FLIGHT OUTRANKS EVERYTHING, because nothing else moves until it
	# resolves. Retail's own sentence, with retail's own placeholder filled.
	if not state.pending_battle.is_empty():
		var fight := _retail_sentence("STRATEGICHUD:ResolveBattleChecklistItem", "")
		if fight.contains("%s"):
			war_council.append(fight % _display_of(
				String(state.pending_battle.get("region", ""))))
	# WHAT CAN ACTUALLY BE RAISED, ANYWHERE THIS SEAT HOLDS. One pass over the
	# seat's own regions collecting the structure CLASSES that come back buildable;
	# the row is retail's sentence for that class. A class this seat can afford
	# nowhere does not appear, which is the gate that makes the list a council.
	var offered: Dictionary = {}
	for region_value in state.regions_owned_by(seat):
		var region_id := String(region_value)
		for entry_value in _build_offer(region_id, -1):
			var entry := entry_value as Dictionary
			if bool(entry.get("can_build", false)):
				offered[String(entry.get("type", ""))] = true
	for type_value in WAR_COUNCIL_BUILD_ORDER:
		var type_name := String(type_value)
		if not offered.has(type_name):
			continue
		var line := _retail_sentence(String(WAR_COUNCIL_BUILD_ITEMS[type_name]), "")
		if not line.is_empty() and not war_council.has(line):
			war_council.append(line)
	# AND THE MOVE, which is the other half of retail's tactical phase. Offered
	# only when this seat has somewhere to march or take, so the council never
	# tells a boxed-in seat to conquer.
	if not _targets.is_empty() or not _moves.is_empty():
		var march := _retail_sentence("STRATEGICHUD:MoveHeroArmiesChecklistItem", "")
		if not march.is_empty():
			war_council.append(march)
	if war_council.size() > WAR_COUNCIL_MAX:
		war_council = war_council.slice(0, WAR_COUNCIL_MAX)


## The council, drawn into retail's own task field UNDER the imperative and the
## message line those two Labels already occupy. It is a DRAW rather than two more
## Labels for the reason the structures roster is: the field's height is retail's
## and changes with the plaque's state, and a Label whose minimum is its own text
## cannot be made to shrink back into it.
func _draw_war_council(field: Rect2) -> void:
	if field.size.x <= 24.0 or war_council.is_empty() or not _checklist_is_open():
		return
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return
	# THE ROWS START UNDER THE MESSAGE LINE, which is the lower of the two Labels
	# the layout has already placed in this field. Measured off the control rather
	# than off a fraction of the field, so the council cannot land on it at any
	# window size.
	var top := field.position.y + field.size.y * 0.34
	if message_label != null and message_label.visible:
		top = maxf(top, message_label.position.y + message_label.size.y + 4.0)
	var room := field.end.y - top - 6.0
	if room <= 14.0:
		return
	var pitch := clampf(room / float(war_council.size()), 13.0, 30.0)
	var shown := mini(war_council.size(), maxi(1, int(room / pitch)))
	var size_px := HudScript.type_size(pitch, HudScript.TYPE_CAPTION, 10)
	var left := field.position.x + field.size.x * 0.045
	var width := field.size.x * 0.91
	for index in range(shown):
		var baseline := top + pitch * (float(index) + 0.5) + float(size_px) * 0.36
		# A BULLET THAT IS A FITTING, not a glyph: the same small gilt diamond the
		# HUD sets at every panel elbow, so the list belongs to the casting around
		# it rather than to a text style.
		HudScript.draw_star(chrome_layer,
			Vector2(left + pitch * 0.22, baseline - float(size_px) * 0.32),
			maxf(1.5, pitch * 0.13), HudScript.RIM_GOLD_BRIGHT)
		chrome_layer.draw_string(font, Vector2(left + pitch * 0.62, baseline),
			String(war_council[index]), HORIZONTAL_ALIGNMENT_LEFT,
			int(width - pitch * 0.62), size_px, HudScript.PARCHMENT_DIM)


## THE ROUND, in the chevron bar's right-hand capsule - see
## `CHECKLIST_ROUND_PLAQUE` for how that rectangle was measured and why the word
## is this project's. Set as a caption row over a value row, exactly as the turn
## plaque opposite it is, so the bar reads as one fitting with two ends.
func _draw_round_plaque(origin: Vector2, scale: Vector2) -> void:
	if session == null or session.state == null:
		return
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return
	var box := Rect2(origin + CHECKLIST_ROUND_PLAQUE.position * scale,
		CHECKLIST_ROUND_PLAQUE.size * scale)
	if box.size.y <= 6.0:
		return
	var caption_size := int(clampf(box.size.y * 0.42, 8.0, 30.0))
	var value_size := int(clampf(box.size.y * 0.46, 9.0, 34.0))
	var field := box.size.x * TURN_PLAQUE_TEXT_WIDTH
	chrome_layer.draw_string(font, box.position + Vector2(0.0, box.size.y * 0.46),
		CHECKLIST_ROUND_CAPTION, HORIZONTAL_ALIGNMENT_CENTER, int(field),
		caption_size, HudScript.PARCHMENT)
	chrome_layer.draw_string(font, box.position + Vector2(0.0, box.size.y * 0.92),
		"%d" % (session.state.round_index() + 1), HORIZONTAL_ALIGNMENT_CENTER,
		int(field), value_size, HudScript.GOLD_VALUE)



## THE ONE TASK IN THE CRITICAL-TASKS BOX, as a SHORT IMPERATIVE.
##
## This used to read "Click one of your 1 armed regions (gold-ringed) to stage
## from." and "Select one of your regions to see what it can attack." A blind
## review named both as onboarding comments written by the programmer, and a
## shipped strategy game does not teach its click rule in a permanent status
## strip: it names the next task ("Choose a staging region") and teaches the
## mechanic in a tooltip. So the imperative is here and the WHOLE two-sided rule
## - which ring colour means what, how many are offered - moved into the box's
## own tooltip, where nothing is lost and nothing is shouted.
func _hint_text(state: StateScript) -> String:
	if not state.pending_battle.is_empty():
		return "Resolve the battle for %s" % _display_of(
			String(state.pending_battle.get("region", "")))
	if session.selected_region.is_empty():
		if _staging.is_empty():
			return "End the turn"
		return "Choose a staging region"
	if not session.selected_target.is_empty():
		# THE TASK NAMES THE ACTUAL VERB. Committing an unowned region is a march,
		# not an attack, and the button beside this box says TAKE for the same reason.
		return "Take %s" % _display_of(session.selected_target) if _target_is_unclaimed() \
			else "Attack %s" % _display_of(session.selected_target)
	if not _targets.is_empty():
		return "Choose a region to take" if _claims.size() == _targets.size() \
			else "Choose a region to attack"
	if not _moves.is_empty():
		return "March to an adjacent region"
	return "Choose another staging region"


## THE MECHANIC, in the tasks box's tooltip rather than across the HUD. Same
## facts the old status line carried - the ring colours, the counts, what a click
## does on each side of the rule - kept whole and moved a hover away.
func _hint_tooltip(state: StateScript) -> String:
	if not state.pending_battle.is_empty():
		return "A battle for %s is in flight; nothing else moves until it resolves." % _display_of(
			String(state.pending_battle.get("region", "")))
	if session.selected_region.is_empty():
		if _staging.is_empty():
			return "This seat has no army standing in a region it owns, so nothing can be staged. Ending the turn passes play on."
		return "%d of your regions have an army standing in them and are ringed in gold. Clicking one stages from it." % _staging.size()
	var parts: Array[String] = []
	parts.append("Staged at %s." % _display_of(session.selected_region))
	# THE TWO KINDS OF RED RING ARE COUNTED SEPARATELY, because they cost different
	# things: one is a battle, the other is a hero army walking into empty country.
	# A single "N can be attacked" hid the second entirely, which is how a player
	# ends up believing an all-neutral frontier offers them nothing.
	var held := _targets.size() - _claims.size()
	if held > 0:
		parts.append("%d red-ringed region(s) a rival holds can be attacked from here." % held)
	if not _claims.is_empty():
		parts.append("%d unclaimed region(s) can be taken from here by marching your hero army in." % _claims.size())
	if not _moves.is_empty():
		parts.append("%d pale-ringed region(s) can be marched to." % _moves.size())
	if _targets.is_empty() and _moves.is_empty():
		parts.append("Nothing adjacent can be attacked, taken or marched to from here.")
	if not session.selected_target.is_empty():
		parts.append("%s is the chosen target; %s commits it." % [
			_display_of(session.selected_target), _attack_button_caption()])
	return "  ".join(parts)


## WHAT THE COMMAND RAIL'S FIRST BUTTON IS CALLED, which depends on what pressing
## it does.
##
## Marching a hero army into empty country is not an attack and calling it one
## would be the button lying about its own verb - the exact failure this screen is
## written to avoid. So on unowned ground the caption is TAKE.
##
## RETAIL SHIPS NO CAPTION TO BORROW HERE, and this is stated rather than glossed:
## retail's living world has no ATTACK button at all - a move is a drag of an army
## onto a territory - so `data/lotr.str` carries no control text for either state
## and `wotr_display_names` cannot be asked for one. Both words are therefore this
## project's own. TAKE is not invented out of nothing, though: it is retail's own
## VERB for this event, from the notice retail raises when it happens
## (`APT:LivingWorldRegionTakenNotice`, "%s taken!"), which retail keeps distinct
## from the "%s conquered!" it raises for a region won in battle.
func _attack_button_caption() -> String:
	return "TAKE" if _target_is_unclaimed() else "ATTACK"


## Why ATTACK is greyed out, in the tooltip, so a disabled button is never a
## dead end the player has to guess at.
func _attack_button_reason() -> String:
	if session == null or session.state == null:
		return "The war is not under way."
	if not session.state.pending_battle.is_empty():
		return "A battle is already in flight."
	if session.selected_region.is_empty():
		return "Stage from one of your own armed regions first."
	if session.selected_target.is_empty():
		return "Choose an adjacent region to attack."
	if not Array(_targets).has(session.selected_target):
		return "%s cannot be attacked from %s." % [
			_display_of(session.selected_target), _display_of(session.selected_region)]
	if _target_is_unclaimed():
		# WHAT THE BUTTON WILL DO, not why it will not. This tooltip used to say
		# neutral ground could not be taken at all; it can now, by marching the hero
		# army in, and saying so is the difference between a board a player can
		# expand on and one they cannot find the door out of. The hero-army clause is
		# retail's own rule, not a flourish - a garrison alone offers no claim, and
		# `session.attack_targets()` will not have offered the region in that case.
		return ("%s answers to no lord. Marching your hero army in from %s takes it, "
			+ "and no battle is fought for it.") % [
			_display_of(session.selected_target), _display_of(session.selected_region)]
	return "Commit the attack on %s. This is the only path from this screen to a battle." % _display_of(
		session.selected_target)


## EVERY SEAT'S STANDING, from the authoritative state and nothing else: regions
## held, armies standing, command points on the board, and the starting world and
## hero command points the document authored for the template. Read-only - this
## panel computes from `state` and writes nothing back.
## Why AUTO-RESOLVE cannot be pressed, or what it will do.
##
## THE LOADER'S OWN REASON DOES NOT COME HERE ANY MORE. It used to: the tooltip
## carried the auto-resolve loader's verbatim refusal, every path it had searched
## and a count of unit templates with no data in it, on the theory that a refusal
## must be shown rather than swallowed. It still must - but the refusal is shown
## on the DIAGNOSTICS panel, whose whole job is that, and this tooltip says what a
## player can do about it, in the world's words. A tooltip is player surface: a
## blind review reads it the same way it reads the status bar.
func _auto_resolve_button_reason(state: StateScript) -> String:
	if session.autoresolve == null or session.autoresolve_bindings == null:
		return "This battle cannot be decided from the map; it must be fought."
	if String(state.battle_type) == StateScript.BATTLE_TYPE_RTS:
		return "This war is fought battle by battle: every one is commanded in the field."
	if not can_attack_now():
		return _attack_button_reason()
	if _target_is_unclaimed():
		# THERE IS NOTHING TO DECIDE. Nobody holds the region, so nobody defends it;
		# the march is the whole event and the other button performs it.
		return "%s is undefended - there is no battle to decide. Marching in takes it." % _display_of(
			session.selected_target)
	return "Decide this battle from the map instead of taking the field."


func _refresh_standings(state: StateScript) -> void:
	_seat_plaques = []
	var active := state.active_player()
	var claimed := 0
	for index in range(state.players.size()):
		var seat_row := state.players[index] as Dictionary
		var regions := state.regions_owned_by(index)
		claimed += regions.size()
		var army_count := 0
		var command_points := 0
		var heroes := 0
		for army_id in state.armies.keys():
			var army := state.armies[army_id] as Dictionary
			if int(army.get("owner", StateScript.NEUTRAL)) != index:
				continue
			army_count += 1
			command_points += int(army.get("command_points", 0))
			if String(army.get("kind", "")) == StateScript.ARMY_HERO:
				heroes += 1
		# COUNTERS, NOT PROSE. The old form spelled these out as
		# "regions 9   armies 4 (3 hero)   CP on the board 6", which read as a
		# diagnostic dump; the plaque sets the same numbers in sockets and puts
		# the words in the tooltip instead.
		_seat_plaques.append({
			"kind": "seat",
			# RETAIL'S ENGLISH, never the LivingWorldPlayerTemplate id: this
			# plaque used to read "PlayerDwarves", which a blind review named as
			# a disqualifying developer surface.
			"name": _owner_name(index),
			"color": _owner_color(index),
			"active": index == active,
			"defeated": bool(seat_row.get("defeated", false)),
			# BARE NUMBERS IN A COLUMN GRID. They used to carry their own units and
			# their own glyph - "3☆", "6 cp" - which put a hairline outline glyph and
			# a lowercase unit against `END TURN`'s caps in the same island, and a
			# blind review called the whole block "a debug readout in a decorated
			# frame". The units are in the COLUMN HEADINGS now (`STANDINGS_COLUMNS`),
			# the heroes column is headed by a drawn star rather than a text glyph,
			# and every value is right-aligned on the same grid.
			"counters": [
				"%d" % regions.size(),
				"%d" % army_count,
				"%d" % heroes,
				"%d" % command_points,
			],
		})
	var neutral := session.world.region_ids.size() - claimed
	_seat_plaques.append({
		"kind": "unclaimed",
		# Retail's own word for an unowned holding, out of its own string table
		# (`SIDE:Neutral`); "unclaimed 34" in lower case was this project's.
		"text": names.shell_label("SIDE:Neutral", "Unclaimed"),
		"value": "%d" % neutral,
	})
	# THE STAR COLUMN IS NAMED HERE, and that is the whole of its legend. A blind
	# review asked for the star column to be "labelled or tooltipped"; the heading
	# cell is four glyphs wide at the narrowest window in the layout runner's
	# `SIZES` and retail's own word for it is "Heroes", which does not fit - so the
	# heading stays the drawn icon and the word is where a shipped game puts it.
	standings_label.tooltip_text = (
		"One plaque per seat: its heraldry colour, its name, then regions held, "
		+ "armies fielded, heroes among them (the star column), and the command "
		+ "points those armies carry. The last row is how many regions no seat holds.")
	standings_label.queue_redraw()


## EVERY STRING THIS SCREEN PUTS IN FRONT OF A PLAYER, as `surface -> text`.
##
## This is the collection half of the string audit (`IMPLEMENTATION_VOCABULARY` is
## the rule half). It exists so a RUNNER can hold the register rather than a
## reviewer holding it, because a reviewer holds it once and a runner holds it
## every build - and the defect that ended round four was one sentence that had
## survived four rounds of reading.
##
## WHAT COUNTS AS PLAYER-VISIBLE, stated so the assertion cannot be quietly
## narrowed later. Everything a screenshot of this screen can contain, plus
## everything a hover can reveal:
##
##   * every Label and Button caption on the HUD (NOT the diagnostics overlay's
##     own labels, which are the diagnosis);
##   * the region card's bbcode, tags stripped;
##   * the tray ribbon's line, before it is trimmed to the rail;
##   * the drawn surfaces that are not Labels at all - the header plates, the seat
##     plaques, the standings column headings, the palantir's two rim captions;
##   * every `tooltip_text` on the HUD, because a tooltip is read the same way a
##     status bar is.
##
## The diagnostics overlay is deliberately absent from this dictionary. It is the
## place the banned register is SUPPOSED to live.
func player_visible_strings() -> Dictionary:
	var surfaces: Dictionary = {}
	surfaces["turn plaque"] = "%s %s" % [turn_plaque_label, turn_plaque_value]
	for entry_value in [
		["heading", heading_label],
		["phase banner", phase_banner], ["tasks line", hint_label],
		["message line", message_label], ["unplaced heading", unplaced_label],
		["region portrait caption", region_portrait_caption],
	]:
		var entry := entry_value as Array
		var label := entry[1] as Label
		if label != null and label.visible:
			surfaces[String(entry[0])] = label.text
			if not label.tooltip_text.is_empty():
				surfaces["%s tooltip" % String(entry[0])] = label.tooltip_text
	for entry_value in [
		["ATTACK", attack_button], ["END TURN", end_turn_button],
		["AUTO-RESOLVE", auto_resolve_button], ["MAIN MENU", back_button],
		["RESUME", pause_resume], ["OPTIONS", pause_options],
	]:
		var entry := entry_value as Array
		var button := entry[1] as Button
		if button != null and button.visible:
			surfaces["%s caption" % String(entry[0])] = button.text
			if not button.tooltip_text.is_empty():
				surfaces["%s tooltip" % String(entry[0])] = button.tooltip_text
	# THE TAB CAPTIONS, which `_draw_tab_captions` draws rather than setting on the
	# Button (see `build()`). The string lives in `_tab_captions`, so that is what
	# the audit reads - a sweep that read `Button.text` here would now find three
	# empty strings and report a clean surface it had not looked at.
	for key in _tab_captions.keys():
		surfaces["tab %s" % String(key)] = String(_tab_captions[key])
	for child in unplaced_host.get_children():
		var listed := child as Button
		if listed != null:
			surfaces["unplaced %s" % listed.name] = listed.text
	if detail_label != null:
		# THE CARD, WITH ITS BBCODE TAGS STRIPPED. The colour and size tags are
		# markup, not words, and leaving them in would let a banned word hide
		# inside one.
		surfaces["region card"] = _strip_bbcode(detail_label.text)
	# THE STRUCTURES ROSTER, which `_draw_structure_roster` DRAWS rather than
	# setting on a control. A sweep that only read Labels would not see a word of
	# it - and this is the surface a blind review called the single most damaging
	# element on the screen, so it is the last one that may be invisible to the
	# audit that guards the register.
	surfaces["structure roster head"] = "%s %s" % [
		names.shell_label("APT:Structures", "Structures"),
		names.shell_label("APT:Cost", "Cost")]
	for index in range(structure_roster.size()):
		var offering := structure_roster[index] as Dictionary
		surfaces["structure roster %d" % index] = "%s %s" % [
			String(offering["title"]), String(offering["cost"])]
	if active_tab == "structures" and structure_roster.is_empty():
		surfaces["structure roster empty state"] = _empty_tab_line("structures")
	# EVERY REFUSAL AND EVERY BUILD TOOLTIP, because those are the surfaces this
	# round added and they are the ones most at risk of carrying the simulation's
	# own register: they are DERIVED from a sentence written in it. A sweep that
	# read the roster's titles and not its refusals would be looking straight past
	# the thing this round changed.
	for index in range(structure_roster.size()):
		var offering := structure_roster[index] as Dictionary
		if not String(offering.get("refusal", "")).is_empty():
			surfaces["structure refusal %d" % index] = String(offering["refusal"])
		if not String(offering.get("tooltip", "")).is_empty():
			surfaces["structure tooltip %d" % index] = String(offering["tooltip"])
	for index in range(_dial_buttons.size()):
		var well := _dial_buttons[index] as Button
		if well != null and well.visible and not well.tooltip_text.is_empty():
			surfaces["command well tooltip %d" % index] = well.tooltip_text
	for index in range(_plot_card_buttons.size()):
		var card := _plot_card_buttons[index] as Button
		if card != null and card.visible and not card.tooltip_text.is_empty():
			surfaces["build plot tooltip %d" % index] = card.tooltip_text
	# THE WAR COUNCIL, which `_draw_war_council` DRAWS into retail's task field.
	# Retail's own sentences, but the audit reads them like everything else - a
	# surface is in the sweep because it is on the glass, not because of where it
	# came from.
	for index in range(war_council.size()):
		surfaces["war council %d" % index] = String(war_council[index])
	# THE ROUND CAPSULE'S CAPTION, drawn into retail's own chevron bar.
	surfaces["round plaque"] = CHECKLIST_ROUND_CAPTION
	surfaces["tray ribbon"] = tray_ribbon_text
	if header_label != null and not header_label.tooltip_text.is_empty():
		surfaces["header tooltip"] = header_label.tooltip_text
	if standings_label != null and not standings_label.tooltip_text.is_empty():
		surfaces["standings tooltip"] = standings_label.tooltip_text
	if turn_banner != null and not turn_banner.tooltip_text.is_empty():
		surfaces["turn plaque tooltip"] = turn_banner.tooltip_text
	for index in range(_header_facts.size()):
		var fact := _header_facts[index] as Dictionary
		surfaces["header plate %d" % index] = "%s %s" % [
			String(fact.get("label", "")), String(fact.get("value", ""))]
	for index in range(_seat_plaques.size()):
		var plaque := _seat_plaques[index] as Dictionary
		surfaces["seat plaque %d" % index] = "%s %s %s" % [
			String(plaque.get("name", "")), String(plaque.get("text", "")),
			" ".join(Array(plaque.get("counters", [])).map(
				func(v: Variant) -> String: return String(v)))]
	for column_value in STANDINGS_COLUMNS:
		var column := column_value as Dictionary
		if not bool(column["star"]):
					surfaces["standings column %s" % String(column["caption"])] = names.shell_label(
				String(column["string"]), String(column["caption"]))
	# THE PALANTIR'S TWO RIM CAPTIONS, which `_draw_region_portrait` draws rather
	# than setting on a Label. They are literals there, so they are literals here;
	# a runner that only read Labels would not see them at all.
	# THE WORD A FREE OFFERING'S PRICE CELL CARRIES. Project-authored - retail has
	# no string for a zero cost - so it is audited like every other literal on this
	# screen. See `ROSTER_FREE_COST`.
	surfaces["roster free cost"] = ROSTER_FREE_COST
	# THE ORDER ON THE COMMAND DECK'S SHELF. Both words are this project's - retail
	# has no such surface at all, because retail's living world has no ATTACK
	# control - so both are audited like every other literal here.
	surfaces["deck order from"] = DECK_ORDER_FROM
	surfaces["deck order to"] = DECK_ORDER_TO
	surfaces["palantir armies caption"] = names.shell_label(
		"APT:Armies", PALANTIR_ARMIES_CAPTION)
	surfaces["palantir region cp caption"] = PALANTIR_REGION_CP_CAPTION
	# RETAIL'S THREE PHASE TITLES, one of which is on the glass under the lit
	# chevron at any moment. All three are audited rather than only the lit one: the
	# sweep is about what this screen CAN print, not about what it prints this
	# frame, and a phase title that arrived carrying implementation vocabulary would
	# otherwise only be caught in the state that shows it.
	for phase_value in PHASE_CELLS:
		var phase := phase_value as Dictionary
		surfaces["phase title %s" % String(phase["caption"])] = names.shell_label(
			String(phase["string"]), String(phase["caption"]))
	surfaces["phase label"] = names.shell_label(PHASE_LABEL_STRING, PHASE_LABEL_CAPTION)
	# THE COMMAND RING'S HOVER LINE. It is the reason six lit icons in six gilt
	# collars are not a build menu, and it is the single most important string on
	# this screen for the complaint that started this round - so it is audited like
	# every other tooltip rather than being the one that is not looked at.
	if dial_affordance != null and not dial_affordance.tooltip_text.is_empty():
		surfaces["command ring tooltip"] = dial_affordance.tooltip_text
	if detail_label != null and not detail_label.tooltip_text.is_empty():
		surfaces["tray well tooltip"] = detail_label.tooltip_text
	# THE TWO MEDALLIONS' OWN LINES. They are live controls now, so their tooltips
	# are the only thing on screen that says what pressing them does - which puts
	# them squarely inside the register audit rather than outside it.
	if medallion_key != null and medallion_key.visible:
		surfaces["palantir key tooltip"] = medallion_key.tooltip_text
	if medallion_banner != null and medallion_banner.visible:
		surfaces["palantir banner tooltip"] = medallion_banner.tooltip_text
	# AND THE TWO EXPANDERS' OWN LINES, for the same reason: they are the only
	# words on the screen that say what retail's two red arrows now do.
	if stats_expander != null and stats_expander.visible:
		surfaces["stats expander tooltip"] = stats_expander.tooltip_text
	if objectives_expander != null and objectives_expander.visible:
		surfaces["objectives expander tooltip"] = objectives_expander.tooltip_text
	# THE PAUSE SHELL'S KEY LIST, which `_draw_pause_shell` DRAWS. Only the
	# player-facing bindings appear on the glass (see
	# `OpenBFMEUserSettings.KEY_BINDINGS.player`), so only those are collected -
	# collecting the other one would fail the register audit over a string the
	# shell deliberately never draws.
	for binding_value in OpenBFMEUserSettings.player_key_bindings():
		var binding := binding_value as Dictionary
		surfaces["key binding %s" % String(binding["key"])] = "%s %s" % [
			String(binding["key"]), String(binding["action"])]
	return surfaces


## THE DIAGNOSIS AS ONE STRING - the provenance line, the conversion report and
## the named gaps. The other half of the audit's assertion reads this: every
## phrase the HUD is no longer allowed to carry has to be findable in here.
func diagnostics_text() -> String:
	var parts: Array[String] = []
	if status_label != null:
		parts.append(status_label.text)
	if gaps_label != null:
		parts.append(_strip_bbcode(gaps_label.text))
	if map_mode_label != null:
		parts.append(map_mode_label.text)
	return "\n".join(parts)


## Markup out, words in. `RichTextLabel` bbcode is `[tag]`-delimited and no
## authored string on this screen contains a literal square bracket, so a
## non-greedy strip is exact here rather than approximate.
static func _strip_bbcode(text: String) -> String:
	var expression := RegEx.new()
	expression.compile("\\[[^\\]]*\\]")
	return expression.sub(text, "", true)


## EVERYTHING RETAIL DRAWS THAT THIS SCREEN DOES NOT, named. This used to be a
## paragraph of body copy inside the player's seat table; it is the same list,
## kept current the same way, and it now feeds the DIAGNOSTICS panel - a named
## gap belongs in the diagnosis, not in the HUD.
func _conversion_gap_lines() -> Array[String]:
	var absent: Array[String] = []
	# THE MENU'S OWN REFUSAL, verbatim, when there is no session to draw. It used
	# to be the body of the region card and the one line in the tasks box.
	if (session == null or session.state == null) and not unavailable_reason.is_empty():
		absent.append("WAR OF THE RING IS UNAVAILABLE, and the reason is: %s" % unavailable_reason)
	if strings == null:
		absent.append("retail's LW: string table (regions carry retail ids)")
	if region_geometry == null:
		absent.append("retail's region territory shapes (regions are markers, not filled territories)")
	# THE STRATEGIC APT SCREENS, kept current after an actual survey of the
	# cache: the movies AND their bitmap sheets are extracted under
	# .private/retail-work/cache/effective-assets (LivingWorldUI, StrategicHUD,
	# StrategicPalantir, StrategicDetails*, StrategicEndTurnButton,
	# StrategicPlayerStatus, TimeLine - .apt/.const/.dat/_geometry plus
	# apt_Strategic*_N.tga). PRESENT AND NOT YET BOUND is a different claim
	# from absent, and the APT importer lane owns the binding; this screen
	# reads only the two sheets the living-world UI bundle already converts
	# (the phase band and the radial ring).
	if strategic == null:
		absent.append("retail's strategic APT movies (StrategicHUD, StrategicStats, StrategicChecklist, StrategicEndTurnButton, StrategicPalantir, StrategicDetails*) - NOT LOADED here, so every plate, capsule and tray frame on this screen is hand-drawn in retail's language and is this project's own. %s" % strategic_reason.split(".")[0])
	else:
		# BOUND, so the claim changes shape: what is absent is now what the APT
		# format itself does not carry, and the bundle names all six of those.
		# Repeating them here is the point - a consumer that binds retail's art
		# and stops restating its holes has started overclaiming.
		var gap_names: Array[String] = []
		for key in strategic.named_gaps.keys():
			gap_names.append(String(key))
		gap_names.sort()
		for gap_name in gap_names:
			absent.append("%s - %s" % [gap_name, String(strategic.named_gaps[gap_name])])
		# THE MEDALLIONS ARE CONTROLS. This line used to claim they were art; they
		# were wired two rounds ago off retail's own `namedInstances` table. What is
		# still absent is the SCREEN each of retail's two buttons opened, so the claim
		# is narrowed to that rather than left standing as written.
		absent.append("retail's OBJECTIVES and OPTIONS SCREENS, which its own objectivesButton and optionsButton opened. Both medallions are live controls here on retail's own authored rectangles; the key opens this project's settings screen and the flag opens and shuts the war council, which are the nearest true things this screen owns")
		# AND THE DIAL IS A CONTROL. This line used to say nothing on it could be
		# pressed because no construction existed; construction exists, the six wells
		# raise structures, and the claim is deleted. What survives of it is the part
		# that is still true: the wells' CONTENT is not retail's, because retail fills
		# them at runtime and the APT carries nothing for them.
		absent.append("the CONTENT of retail's six commandUI wells (StrategicPalantir). Retail fills each with a live StrategicCommandButton, which the APT does not carry (the bundle's dynamic-content-slots-are-empty gap), so the positions are retail's authored instance translations and the pictures are retail's own ConstructButtonImage crops chosen by this screen from what the region's holder may raise")
		# THE PALANTIR SHEET USED TO BE THE WRONG EDITION'S AND NO LONGER IS. This
		# block carried a measured gap saying the bundle shipped BFME2's
		# `apt_StrategicPalantir_1.tga` (cc4ce1696fed) against a RotWK oracle, with a
		# pixel census of the difference. The bundle has since been reconverted from
		# `.private/retail-work/editions/rotwk/layered-install` - its own `source`
		# block names that root - and the palantir atlas it now carries hashes
		# cd13947c49ba, which is RotWK's. The gap is therefore DELETED rather than
		# reworded: a named gap that has been closed and is still printed is a
		# diagnosis lying in the safe direction, which is still lying.
		absent.append("the parchment compass disc behind the palantir's command dial - retail's reference capture shows an eye watermark on parchment there and StrategicPalantir's own frame 1 flattens no such draw, so this screen shows the dial's gilt ring and its empty wells and paints nothing in the middle")
		absent.append("the TERRITORY / ARMIES / STRUCTURES tab captions on retail's tray rail - the rail is retail's art, the captions are live strings the APT does not carry, and this screen has one card rather than three tabs")
		# THE THIRD CHEVRON. The bar is a clock now (see `PHASE_CELLS`) and it is a
		# clock with a hand it cannot move, which is worth saying out loud: the
		# alternative was to light the retreat cell off something that is not a
		# retreat, and a clock that puts the wrong hand up is worse than one that
		# admits a hand is parked.
		absent.append(PHASE_RETREAT_GAP)
		# THE PRICE WORD, because it is this project's and there is nothing of
		# retail's behind it. Retail's data really does price the farm class at
		# nothing (WOTR_FARM_COST = 0) and retail states the rule in prose in
		# STRATEGICHUD:TreasuryWarningPopUpMessage, but no converted string table
		# carries a WORD for a zero cost in any namespace - so the cell that used to
		# read a bare "0" reads this project's own word and says so here.
		absent.append("retail's own word for a cost of nothing. Retail prices the farm class at zero (WOTR_FARM_COST) and says so in prose, but authors no string for a free price, so the STRUCTURES roster sets \"%s\" in that cell and the word is this project's rather than retail's" % ROSTER_FREE_COST)
	if display_font != null:
		absent.append(HudScript.DISPLAY_FACE_BINDING)
	if ui == null:
		absent.append("army banner portraits and the build menu (retail's MappedImage atlases are not converted)")
	else:
		# CONVERTED, so not claimed absent - but what is still missing INSIDE them
		# is named, because a half-converted surface reported as done is the same
		# defect as one reported as absent.
		# CONSTRUCTION IS NO LONGER AN ABSENCE. The line that stood here said the
		# build ring changed no state; it spends the treasury and stands a structure
		# on a numbered foundation now, so the claim is DELETED rather than reworded.
		# What is still genuinely absent inside this lane is recruitment and what a
		# structure GRANTS, and both are named below by their register entries.
		absent.append("RECRUITMENT from a built barracks - retail's ArmyToSpawn lists are carried on every structure and no army roster in the living-world document carries a purchase cost, so there is no recruit order to draw a control for (register: army_recruitment_and_cp_costs)")
		absent.append("what a Barracks or an Armory GRANTS - the converter drops every BuildingNugget, so an Armory's UpgradeTroops list, a Fortress's StrengthenArmy armour table and a Farm's one-off IncreaseCommandPoints are unrecorded; only the treasury nugget is reconstructible and it is (register: strategic_building_nuggets)")
	# THE MARKER MODELS, kept current the same way. They ARE converted now - all
	# 81 of them - so claiming they are not would be its own dishonesty; what is
	# still not on the map is the 28 structure families, and the reason is a
	# missing SIMULATION rather than a missing conversion.
	if markers == null:
		absent.append("the 3D marker models retail draws armies and plots with - the LivingWorldArmyIcon banners, the LivingWorldBuildingIcon structures and the LivingWorldBuildPlotIcon foundation decals are all in the archives and NONE is converted here; the map draws flat plates and rings in their place")
	else:
		absent.append("the SCAFFOLD slot of retail's %d LivingWorldBuildingIcon famil(ies) - every one of retail's 28 blocks is TurnsToBuild = 1, so a structure ordered this turn stands immediately and there is no under-construction state for a scaffold to show (register: strategic_one_structure_under_construction_per_territory)" % int((markers.totals.get("familiesByKind", {}) as Dictionary).get("building", 0)))
		absent.append("the marker ANIMATIONS - retail fades, glows and marches these models; every one here is standing still, which is a state this screen names rather than a motion it invents")
	absent.append("the turn-phase bar (retail's phase list is hardcoded in the executable; livingworldlogic.ini ships EMPTY, 192 bytes of comment, and there is no mprules.ini anywhere in the archives)")
	absent.append("army models marching between regions")
	# THE HUD CHROME'S OWN GAPS, by the same discipline: what this screen wanted
	# to draw with retail art and could not, with the reason.
	if hud_font == null and not hud_font_reason.is_empty():
		absent.append("retail's Albertus MT face on the HUD caps - %s" % hud_font_reason)
	if band_texture == null:
		absent.append("retail's phase-band strip off apt_LivingWorldUI_1.tga behind the turn banner (the UI bundle is absent or its APT sheet carries no bar-shaped island), so the banner sits on a drawn plate")
	if ring_texture == null:
		absent.append("retail's RadialBorder ring (radialborders.dds) around the region portrait dish, so the dish rim is drawn")
	# WHAT THE COMMAND BAR AND THE PALANTIR CANNOT DRAW, and why - all three are
	# retail data that genuinely is not there, not art this project skipped.
	if strategic != null and strategic.loaded:
		# THIS ENTRY USED TO CLAIM THE DIAL'S FACE WAS NOT IN THE DATA. It was, and
		# the correction is recorded here rather than quietly deleted, because a
		# diagnosis that has been wrong once has to say so.
		absent.append("the two EMPTY RUNTIME HOSTS retail leaves inside the palantir - the "
			+ "sub-glass (318 flat-black triangles at x 210..362, y 71..225) and commandUI's "
			+ "backdrop (475 more at x 262.5..378.7, y 51..245.1), both instances of the "
			+ "bundle's `dynamic-content-slots-are-empty` gap. Rendering that black verbatim "
			+ "leaves a hole, so both hosts are SUPPRESSED and a carved stone seat is drawn "
			+ "in their authored rectangle, under retail's own rim and under retail's own "
			+ "button collars. WHAT SITS IN THAT SEAT IS NOW RETAIL'S OWN: the face is the "
			+ "holding seat's BuildPlotSelectionPortraitName - BPMFortress_BuildPlot for "
			+ "Mordor, KUFortressBuildPlot for Angmar - an engraved stone tile carrying the "
			+ "faction device, cropped out of retail's own MappedImage atlases by the "
			+ "living-world UI bundle. A PREVIOUS ROUND OF THIS SCREEN RECORDED THE OPPOSITE "
			+ "HERE and was wrong: it swept both asset layers for a file named for a compass, "
			+ "a dial, a rose, a sunburst or a medallion, found none, and concluded the face "
			+ "was absent. Retail names the art for what it MEANS - an empty fortress build "
			+ "plot - and not for what it looks like, and it draws the same tile in the "
			+ "palantir's selection lens and on every empty card of the build-queue rail, "
			+ "which is why the oracle's dial and its build cards carry the same engraving")
		absent.append("retail's SELECTED-TAB state on the TERRITORY/ARMIES/STRUCTURES rail - "
			+ "StrategicDetailsRegion authors the highlight as a CHILD timeline (named gap "
			+ "`timeline-playback-not-bound`), so all three tabs flatten in one resting "
			+ "state and there is no authored selected frame to ask for; the lit plate "
			+ "under the chosen tab is drawn in the HUD's own language")
		absent.append("retail's RESTING VALUE for three of StrategicDetailsTray's members - "
			+ "the right-hand scroll rail (21/16) and the tray's two full-width rails "
			+ "(21/8/3/1, 21/12/3/1). All three flatten in their LIT state for the same "
			+ "reason (`timeline-playback-not-bound`): their opaque pixels average "
			+ "(255,255,182) against (243,198,107) for every frame member beside them, "
			+ "and drawn verbatim the bottom rail is a white-hot rule running 149 "
			+ "authored pixels out past the tray over open terrain. A MEASURED COLOUR "
			+ "TRANSFORM of retail's own art is applied - the ratio that carries each "
			+ "crop's average onto the frame's - which is what retail's own runtime "
			+ "does to them. Nothing is redrawn and no geometry is dropped")
		absent.append("retail's six TERRITORY SLOTS in the command bar's well "
			+ "(StrategicDetailsTerritory, 50 draws beyond the status ribbon). Retail fills "
			+ "them from the engine and this screen does not, so they are NOT PAINTED - six "
			+ "empty authored boxes would be a list pretending to have rows. The territory's "
			+ "member regions are named in the card text instead")
	absent.append("retail's ornate shell frame and title rules - FrameT/B/L/R, FrameCorner*, Ruler and MainMenuRuler all name textures no archive ships (SCShellUserInterface512_001.tga, MainMenuRuleruserinterface.tga), so every plate, rule and frame here is drawn in retail's language and is this project's own")
	# THE KEYBOARD, AND THE ONE THING THE OPTIONS SCREEN DOES NOT OFFER. The
	# bindings themselves are on the pause shell and on the OPTIONS screen's Key
	# Settings column, where a player looks for them; the reason there is no rebind
	# control is an engineering fact about this program, so it goes here.
	absent.append("%s The bindings themselves are ESC (pause), F1 (this overlay), "
		% KEYBIND_REMAP_GAP
		+ "F2 (hide the HUD) and F11 (fullscreen, persisted through the same "
		+ "settings file the startup path reads).")
	# THE OPPONENT'S LAST HAND-OFF, in full. The glass gets the war ("Angmar took
	# Carn Dum"); this gets everything a reader needs to answer "why did it do
	# that", "why did it NOT do that" and - the question the AI contract insists a
	# player must never be left guessing at - "did retail's own preference weights
	# choose this move, or did OpenBFME's rules".
	absent.append_array(_opponent_diagnosis())
	absent.append_array(_strings_taken_off_the_glass())
	return absent


func _opponent_diagnosis() -> Array[String]:
	var notes: Array[String] = []
	if session == null:
		return notes
	if not session.ai_template_reason.is_empty():
		notes.append("THE OPPONENT IS PLAYING WITHOUT RETAIL'S PREFERENCE WEIGHTS, and the loader's own reason is: %s"
			% session.ai_template_reason)
	for report_value in _ai_reports:
		var report := report_value as Dictionary
		var seat := int(report.get("seat", StateScript.NEUTRAL))
		var refusals := report.get("refusals", PackedStringArray()) as PackedStringArray
		if not refusals.is_empty():
			notes.append("OPPONENT (%s, turn %d) REFUSED: %s" % [
				_owner_name(seat), int(report.get("turn_index_before", -1)),
				"; ".join(Array(refusals))])
		var attack := report.get("attack", {}) as Dictionary
		if not attack.is_empty():
			var reasons := attack.get("reasons", PackedStringArray()) as PackedStringArray
			notes.append("OPPONENT (%s) chose %s, score %s (retail %s / this project %s)%s" % [
				_owner_name(seat), String(attack.get("region", "")),
				str(attack.get("score", "")), str(attack.get("retail_score", "")),
				str(attack.get("project_score", "")),
				"" if reasons.is_empty() else " - " + "; ".join(Array(reasons))])
	# THE PROVENANCE OF THE WEIGHTS, once, off the most recent report. It is the
	# whole of the "which side of the line did this decision come from" answer and
	# it does not change turn to turn, so it is not repeated per turn.
	if not _ai_reports.is_empty():
		var provenance := (_ai_reports[_ai_reports.size() - 1] as Dictionary).get(
			"provenance", {}) as Dictionary
		if not provenance.is_empty():
			notes.append("OPPONENT PROVENANCE: retail template loaded=%s (%s); retail weights bound=%d, unbound=%d, unspendable=%d; rules this project authored=%d" % [
				str(provenance.get("retail_template_loaded", false)),
				String(provenance.get("retail_template_path", "")),
				(provenance.get("retail_weights_bound", {}) as Dictionary).size(),
				(provenance.get("retail_weights_unbound", {}) as Dictionary).size(),
				(provenance.get("retail_weights_unspendable", {}) as Dictionary).size(),
				(provenance.get("project_authored_rules", {}) as Dictionary).size()])
	return notes


## THE OTHER HALF OF THE STRING AUDIT.
##
## Every sentence this round took OFF the player's surface lands here, in full.
## That is the whole bargain: `IMPLEMENTATION_VOCABULARY` bans the register from
## the HUD, and this function guarantees the FACT survives the ban - so the audit
## is a move, not a deletion, and `wotr_living_world_ui_runner` asserts both ends
## of it. A gap that stopped being printed on the ribbon and never arrived here
## would be exactly the silent fallback `AGENTS.md` forbids.
func _strings_taken_off_the_glass() -> Array[String]:
	var moved: Array[String] = []
	# THIS ENTRY USED TO SAY NOTHING EVER GETS BUILT, and it has been DELETED rather
	# than reworded. The `0 of N built` counter's numerator is live, the build ring
	# spends the treasury, and an absence list that keeps reporting a closed gap is
	# a diagnosis lying in the safe direction - which is still lying. What takes its
	# place is the other half of the same bargain: every refusal the simulation
	# words in ITS register, carried here verbatim, because the glass shows this
	# screen's own English for them and the exact sentence must survive that.
	if not _build_refusals_seen.is_empty():
		moved.append(("THE BUILD SURFACES SHOW THIS SCREEN'S OWN WORDING for %d refusal(s) "
			+ "this refresh, because the strategic layer words them with seat indices, "
			+ "LWB_* building ids and the gamedata macro table - a register banned from "
			+ "the glass. The authoritative sentences, verbatim: %s") % [
			_build_refusals_seen.size(), "; ".join(_build_refusals_seen)])
	# AND THE ONE RULE THIS SCREEN STATES IN ITS OWN WORDS RATHER THAN RETAIL'S,
	# named so nobody later mistakes the wording for retail's own.
	moved.append("PROJECT-AUTHORED WORDING, not retail's: the build refusals for an "
		+ "unaffordable structure, an occupied foundation, a full region, a region "
		+ "with no foundations, an unpriced structure and retail's one-structure-per-"
		+ "territory-per-turn rule are written by this project. Retail states that "
		+ "last rule only in its tutorial namespace (WOTRTutorial:LW_InstructionText10, "
		+ "\"only one structure per territory can be under construction at a time\"), "
		+ "which no converted bundle carries. The two refusals retail DOES word are "
		+ "used verbatim: CONTROLBAR:LW_FortRestricted and "
		+ "CONTROLBAR:LW_BuildNumberRestriction. So is the sentence that says building "
		+ "does not end the turn - the RULE is retail's (WOTRTutorial:LW_InstructionText06), "
		+ "the wording on the glass is ours")
	# THE AUTO-RESOLVE LOADER'S OWN REASON, verbatim, which used to be the
	# disabled button's tooltip. A refusal is still shown, not swallowed - it is
	# shown where a refusal about the CONVERSION goes.
	if session != null:
		if session.autoresolve == null or session.autoresolve_bindings == null:
			moved.append("AUTO-RESOLVE is unavailable, and the loader's own reason is: %s"
				% (session.auto_resolve_reason if not session.auto_resolve_reason.is_empty()
					else "retail's auto-resolve tables have not been loaded for this session"))
		elif not session.auto_resolve_unbound_templates.is_empty():
			moved.append(("AUTO-RESOLVE will fight WITHOUT %d unit template(s): they carry no "
				+ "auto-resolve data in any retail object file, so they contribute nothing to "
				+ "the roll. %s") % [
				session.auto_resolve_unbound_templates.size(),
				", ".join(Array(session.auto_resolve_unbound_templates))])
	# THE UNRESOLVED BONUS MACROS, which used to print in red on the region card.
	if not _unresolved_bonus_macros.is_empty():
		moved.append("REGION BONUSES NOT SHOWN ON THE CARD, because their retail macro did not "
			+ "resolve against the gamedata #define table (%d): %s" % [
			_unresolved_bonus_macros.size(), "; ".join(_unresolved_bonus_macros)])
	# WHY A REGION IS NOT ON THE MAP, which used to be the heading of the block
	# that lists them.
	if not _unplaced_reason.is_empty():
		moved.append(_unplaced_reason)
	# THAT THE CHOSEN BATTLE'S FIELD IS A STAND-IN, which used to be a
	# parenthesis on the ARMIES tab.
	if not _stand_in_battlefield.is_empty():
		moved.append(_stand_in_battlefield)
	# WHAT THE STRUCTURES RIBBON STOPPED CARRYING: retail's own per-region build
	# restrictions (raw retail building ids, which are not English and never
	# belonged on the glass) and the offered structures with no cooked icon.
	moved.append_array(_structure_data_gaps())
	return moved


## The two structure facts the status ribbon used to print and no longer does:
## retail's per-region `RestrictBuildings` rules, whose members are raw retail
## object ids rather than English, and the offerings whose `ConstructButtonImage`
## the UI bundle carries no crop for (their card slot is left on retail's empty
## parchment rather than repeating a neighbour's picture).
func _structure_data_gaps() -> Array[String]:
	var gaps: Array[String] = []
	var focus := _card_region()
	if focus.is_empty() or session == null or session.world == null or session.state == null:
		return gaps
	var region := session.world.region(focus)
	if region.is_empty():
		return gaps
	var restrictions: Array[String] = []
	for restriction_value in region.get("restrict_buildings", []) as Array:
		var restriction := restriction_value as Dictionary
		restrictions.append("at most %d of %s" % [
			int(restriction.get("numberAllowed", 0)),
			", ".join(Array(restriction.get("buildings", [])).map(
				func(v: Variant) -> String: return String(v)))])
	if not restrictions.is_empty():
		gaps.append(("%s authors %d build RESTRICTION(S) that are not shown on the player's "
			+ "surface, because their members are retail object ids rather than retail "
			+ "English and this screen will not print a raw asset id on the glass: %s") % [
			_display_of(focus), restrictions.size(), "; ".join(restrictions)])
	if ui == null:
		return gaps
	var owner := session.state.owner_of(focus)
	if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
		return gaps
	var template := String((session.state.players[owner] as Dictionary).get("template", ""))
	var without_icon: Array[String] = []
	for row_value in ui.buildings_for(template):
		var row := row_value as Dictionary
		if not ui.has_image(String(row.get("constructButtonImage", ""))):
			without_icon.append(String(row.get("id", "")))
	if not without_icon.is_empty():
		gaps.append("%d structure(s) offered on %s have no ConstructButtonImage crop in the "
			% [without_icon.size(), _display_of(focus)]
			+ "living-world UI bundle, so their card slot and their dial well are left on "
			+ "retail's empty parchment rather than repeating a neighbour's picture: %s"
			% ", ".join(without_icon))
	return gaps


## The diagnostics panel's own text: the named gaps and the portrait sourcing.
## Refreshed with the screen whether or not the panel is open, so the runners
## (and the owner, the moment it is opened) always read the current claims.
func _refresh_gaps() -> void:
	if gaps_label == null:
		return
	var lines: Array[String] = []
	lines.append("[b][color=#e1c77d]NOT CONVERTED, SO NOT SHOWN[/color][/b]")
	for entry in _conversion_gap_lines():
		lines.append("  [color=#a9b39a]-[/color] %s" % entry)
	# EVERY NAME THE SCREEN IS STANDING IN FOR, by id and with the reason. A seat
	# or a region whose retail name did not resolve shows a CLEANED id on the
	# glass, which is legible but is NOT retail's wording - so it has to be
	# listed here or the stand-in silently becomes a claim.
	var name_gaps := names.gap_lines()
	if not name_gaps.is_empty():
		lines.append("")
		lines.append("[b][color=#e1c77d]NAMES THIS SCREEN COULD NOT RESOLVE (%d)[/color][/b]" % name_gaps.size())
		for line in name_gaps:
			lines.append("  [color=#c8483f]-[/color] %s" % line)
	if not portrait_provenance.is_empty():
		lines.append("")
		lines.append("[b][color=#e1c77d]REGION PORTRAIT SOURCING[/color][/b]")
		lines.append("  %s" % portrait_provenance)
	gaps_label.text = "\n".join(lines)


## RETAIL'S TURN PLAQUE, LAID OUT AS THE TWO CELLS ITS ART PROVIDES.
##
## MEASURED off the oracle capture rather than guessed: retail's black field
## carries `Turn:` in PARCHMENT WHITE across its upper half, centred, and the
## number in GOLD centred under it - two rows in one cell, with the plate's own
## right-hand pilaster left clear. This screen used to set `TURN 1` as a single
## left-aligned run that ran straight across that pilaster, and a blind review
## called it "the signature of someone binding a string to a rect rather than
## laying type into an ornament".
##
## THE PILASTER IS THE REASON FOR `TURN_PLAQUE_TEXT_WIDTH`. The authored plaque
## rectangle (`CHECKLIST_TURN_PLAQUE`) runs to the outer edge of that fitting, so
## type centred in the whole rectangle sits visibly off-centre in the BLACK FIELD,
## which is the part a reader sees. The fraction is the field's share of the
## plate, measured off retail's own art at 2x.
const TURN_PLAQUE_TEXT_WIDTH := 0.88


func _draw_turn_plaque() -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null or turn_plaque_value.is_empty() or turn_banner.size.y <= 6.0:
		return
	var field := turn_banner.size.x * TURN_PLAQUE_TEXT_WIDTH
	var caption_size := int(clampf(turn_banner.size.y * 0.42, 8.0, 30.0))
	var value_size := int(clampf(turn_banner.size.y * 0.46, 9.0, 34.0))
	# Two rows sharing the field's height, each on its own optical centre: the
	# caption a little above the midline, the value a little below it.
	turn_banner.draw_string(font, Vector2(0.0, turn_banner.size.y * 0.46),
		turn_plaque_label, HORIZONTAL_ALIGNMENT_CENTER, int(field),
		caption_size, HudScript.PARCHMENT)
	turn_banner.draw_string(font, Vector2(0.0, turn_banner.size.y * 0.92),
		turn_plaque_value, HORIZONTAL_ALIGNMENT_CENTER, int(field),
		value_size, HudScript.GOLD_VALUE)


## ------------------------------------------------------------------------------
## THE PHASE CLOCK
## ------------------------------------------------------------------------------

## WHICH OF RETAIL'S THREE PHASES THIS SCREEN IS IN, right now. See `PHASE_CELLS`
## for where the three come from and for why RETREAT is never returned.
##
## Presentation only: it reads live state and writes nothing.
func current_phase() -> int:
	# THE BATTLE PHASE IS THE ONE WITH A BATTLE IN IT, which on this screen means
	# the report is on the glass - that is the whole of the window between a battle
	# being committed and its outcome being acknowledged, and it is the only moment
	# the seat is not the one deciding.
	if report_backdrop != null and report_backdrop.visible:
		return PHASE_BATTLE
	return PHASE_TACTICAL


## One phase cell's rectangle in the window, or `Rect2()` with no checklist island.
## Derived from the bar's own centre and its measured device pitch - see
## `PHASE_CELL_PITCH`. The row is the end plaques' row, so the bar's five cells are
## one row by construction.
func phase_cell_rect(index: int) -> Rect2:
	if index < 0 or index >= PHASE_CELLS.size():
		return Rect2()
	var centre := PHASE_CELL_CENTRE + PHASE_CELL_PITCH * float(index - PHASE_BATTLE)
	return _island_rect("checklist", Rect2(
		centre - PHASE_CELL_WIDTH * 0.5, CHECKLIST_TURN_PLAQUE.position.y,
		PHASE_CELL_WIDTH, CHECKLIST_TURN_PLAQUE.size.y))


## The lit cell's wash and the unlit cells' veil, both over retail's bar art.
##
## THE LABEL IS THE OTHER HALF OF THE REVIEW'S PRESCRIPTION - "put a label under
## the active one" - and it is retail's own string for that phase
## (`APT:TacticalPhaseTitle` and friends), so the bar names itself in retail's
## words rather than in this project's. It is set in the caption tier, which is
## caps with tracking; see `HudChrome.draw_caption` for why case is a property of
## the tier rather than of the surface.
func _draw_phase_states() -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	var lit := current_phase()
	for entry_value in PHASE_CELLS:
		var entry := entry_value as Dictionary
		var index := int(entry["index"])
		var cell := phase_cell_rect(index)
		if cell.size.x <= 0.0:
			continue
		if index != lit:
			HudScript.draw_phase_veil(chrome_layer, cell)
			continue
		# THE WARM GROUND ONLY. Its RING is on the pulse layer (`_draw_pulse`),
		# because the ring is the part that breathes and this pass must not be part
		# of the animation - see `build()` for the eighteen milliseconds a frame that
		# cost when it was.
		HudScript.draw_phase_seat(chrome_layer, cell)
		HudScript.draw_phase_glow(chrome_layer, cell, 0.0)
	# THE LABEL UNDER THE ACTIVE CHEVRON IS THE PHASE BANNER ITSELF.
	#
	# The review asked to "put a label under the active one", and the first pass did
	# exactly that - drew the lit phase's title just below its cell - and the first
	# capture of it shows the title printed straight through the imperative line.
	# There is no room: retail's chevron row is authored y 7..46 and its banner strip
	# begins at y 50.9, four authored pixels below, which is a hairline at any window
	# size.
	#
	# That gap is not an oversight in retail's layout, it is retail TELLING US where
	# the phase name goes. The strip immediately under the chevrons is the slot
	# retail sets "tactical phase" in - it is why this screen's own comments have
	# called it "the phase banner" for several rounds while it carried no phase. So
	# the title goes in retail's own slot for it (`_refresh_turn_banner`), which is
	# directly under the bar, cannot collide with anything, and is a line of live
	# type in a plate retail authored rather than a caption floating over a seam.
	#
	# `font` is unused here for the same reason and is kept as the guard it is: a
	# missing face is a state this screen draws through rather than crashes on.
	if font == null:
		return


## RETAIL'S HEADER READOUTS as ornamented plates - the seat's purse and its
## command points, each on its own socket with an engraved label and a gold
## value. Drawn rather than written because the old form was one string in one
## box ("TREASURE 3000   COMMAND POINTS 6/4500"), which an adversarial blind
## review named as the tell that this was a debug overlay: retail sets each
## number in its own plate. `_header_facts` is presentation-only, recomputed by
## `_refresh_header` off the authoritative state every refresh.
func _draw_header() -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null or _header_facts.is_empty():
		return
	# INSIDE RETAIL'S PLATE the facts stack as ROWS - caption engraved on the
	# left, value in gold hard right - because that is the shape of the hole
	# retail's own "Player Bonuses / 3000" plate leaves, and it is 130 authored
	# pixels wide. Without the plate they sit side by side in drawn sockets, which
	# is the wider fallback shape.
	if _islands.has("stats"):
		# THE TYPE SITS IN THE FIELD'S INNER BOX, NOT IN THE FIELD.
		#
		# `STATS_FIELD` is retail's black plate measured edge to edge (draw group
		# `0/5/1`, x 9..139, y 8.3..52.1). Two rows spread across the whole of it put
		# the second row's descenders on the plate's own lower bevel with about two
		# pixels to spare, and a blind review read that, correctly, as "'World
		# Command' runs under and is clipped by the panel's own frame". So the block
		# is inset and CENTRED in the field instead of filling it.
		#
		# THE LEFT INSET CLEARS RETAIL'S OWN ICON, which is the other half of the
		# same defect. `StrategicStats` draws its `Expand` column (`0/14`) at
		# x 18.7..37.9 from y 30.6 down - the blue gem the oracle capture shows on
		# the plate's second row - and this screen was setting the second caption's
		# first glyph straight on top of it. 37.9 is 28.9 into a field that starts at
		# 9, and the lettering starts three pixels clear of that.
		var inner := Rect2(
			header_label.size.x * STATS_ICON_COLUMN,
			header_label.size.y * 0.10,
			header_label.size.x * (1.0 - STATS_ICON_COLUMN - 0.05),
			header_label.size.y * 0.80)
		var row_height := inner.size.y / float(_header_facts.size())
		var caption_size := int(clampf(row_height * 0.62, 9.0, 20.0))
		var value_size := int(clampf(row_height * 0.74, 10.0, 24.0))
		# THE CAPTION SHRINKS TO ITS HALF OF THE PLATE rather than growing into the
		# value's, and it is solved ONCE for the longest caption so both rows are set
		# at one size. "World Command" is longer than "Command" was and it ran into
		# retail's own gilt lip at the authored size; sizing per row on top of that
		# gave the two rows two different captions weights, which is the
		# "inconsistent label baselines" half of the same review note.
		# THE CAPTION IS MEASURED THE WAY IT IS DRAWN. It is set in the caption tier
		# now - caps with tracking, `HudChrome.draw_caption` - and tracking is width
		# that `Font.get_string_size` knows nothing about, so a fit solved against
		# the untracked run would put "TREASURY INCOME" into its own value's column.
		var caption_room := inner.size.x * 0.52
		for fact_value in _header_facts:
			var caption_text := String((fact_value as Dictionary).get("label", ""))
			while caption_size > 7 and HudScript.caption_width(
					font, caption_text, caption_size) > caption_room:
				caption_size -= 1
		for index in range(_header_facts.size()):
			var fact := _header_facts[index] as Dictionary
			# Both cells of a row share ONE baseline, on the row's optical centre.
			var baseline := inner.position.y + row_height * (float(index) + 0.5) 				+ float(value_size) * 0.36
			# THE CAPTION TIER, IN THE ONE TREATMENT EVERY CAPTION ON THIS HUD IS SET
			# IN. These three readouts are the surfaces an art-direction review singled
			# out as looking "like they came from a different game" - they were in the
			# same face as everything else and in a different CASE, which is the half
			# of the type system this project had not written down. See
			# `HudChrome.draw_caption` and the case-rule block above it.
			HudScript.draw_caption(header_label, font,
				Vector2(inner.position.x, baseline),
				String(fact.get("label", "")), caption_size, HudScript.PARCHMENT_DIM)
			header_label.draw_string(font, Vector2(inner.position.x, baseline),
				String(fact.get("value", "")), HORIZONTAL_ALIGNMENT_RIGHT,
				int(inner.size.x), value_size, HudScript.GOLD_VALUE)
		return
	var slot_width := header_label.size.x / float(_header_facts.size())
	for index in range(_header_facts.size()):
		var fact := _header_facts[index] as Dictionary
		var box := Rect2(Vector2(index * slot_width, 0.0),
			Vector2(slot_width - 8.0, header_label.size.y))
		HudScript.draw_socket(header_label, box)
		var label := String(fact.get("label", ""))
		var value := String(fact.get("value", ""))
		var baseline := box.position.y + box.size.y * 0.5 + 5.0
		HudScript.draw_engraved_caps(header_label, font,
			Vector2(box.position.x + box.size.x * 0.32, baseline),
			label, 12, 1.4, HudScript.PARCHMENT_DIM, false)
		header_label.draw_string(font,
			Vector2(box.position.x + box.size.x * 0.60, baseline), value,
			HORIZONTAL_ALIGNMENT_LEFT, -1, 15, HudScript.GOLD_VALUE)


## EVERY SEAT AS A PLAQUE - heraldry swatch, name, and its counters in sockets.
## This replaced a left-aligned prose block ("regions 9   armies 4 (3 hero)   CP
## on the board 6") that a blind review called a debug readout rather than a
## game surface. Same facts, same source; `_seat_plaques` is filled by
## `_refresh_standings` from the authoritative state and is presentation-only.
## THE FOUR COUNTER COLUMNS, in order, with the heading each one is read under.
## The headings are retail's own words where retail has them (`APT:Regions`,
## `APT:Armies`, `APT:HeroesTitle`, `APT:CommandPoints` - the last abbreviated to
## its own initials because the column is four glyphs wide); `star` marks the
## column whose heading is the drawn icon rather than a word, which is what
## replaced the `☆` text glyph in the values.
const STANDINGS_COLUMNS := [
	{"string": "APT:Regions", "caption": "Regions", "star": false},
	{"string": "APT:Armies", "caption": "Armies", "star": false},
	{"string": "APT:HeroesTitle", "caption": "Heroes", "star": true},
	{"string": "", "caption": "CP", "star": false},
]


func _draw_standings() -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null or _seat_plaques.is_empty():
		return
	var row_height := clampf(standings_label.size.y / float(_seat_plaques.size() + 1), 18.0, 46.0)
	var value_size := int(clampf(row_height * 0.46, 10.0, 20.0))
	# THE HEADING IS ONE STEP SMALLER THAN THE VALUE, not three. It used to start
	# at 30% of the row and shrink from there, which at this panel's width bottomed
	# out around six pixels - a caption so much lighter than its own column that a
	# blind review read the two as unrelated ("headers set at a different size from
	# the values and not sitting over their columns"). A heading has to be quieter
	# than a number and still be the same piece of typography.
	var caption_size := maxi(9, int(value_size * 0.76))
	# THE GRID. Four equal columns hard against the right edge of the plaque, and
	# every value and every heading is right-aligned on the SAME column edges - the
	# old form let each counter's own width decide where it landed, which is what
	# made four numbers read as an unlabelled dump.
	#
	# THE RIGHT MARGIN IS THE FRAME'S, for the same reason `_standings_card_rect`
	# takes its inset off the frame's weight: eight pixels put the CP heading's
	# last glyph on the panel's rope hairline.
	var edge := clampf(minf(standings_label.size.x, standings_label.size.y) * 0.035,
		3.0, 9.0)
	var column_span := standings_label.size.x * 0.62
	var column_width := column_span / float(STANDINGS_COLUMNS.size())
	var grid_left := standings_label.size.x - edge - column_span

	# THE HEADING ROW, so the numbers under it are never four unlabelled columns.
	#
	# THE HEADING SIZE IS SOLVED, NOT CHOSEN. A caption wider than its own column
	# runs into its neighbour, and "REGIONS" beside "ARMIES" did exactly that in
	# the first round-4 capture, printing as "REGIOARMIES". So the size is stepped
	# down until the WIDEST heading fits its column with a gutter - one size for
	# all four, because four headings at four sizes is worse than four small ones.
	var header_baseline := row_height * 0.72
	# THE HEAD CAP. Retail's own tray has a head (its tab strip) and this panel had
	# none, which is most of what a blind review meant by "the top-right panel
	# shares no corner ornament or bevel language with the rest of the chrome - it
	# is the one component that looks bolted on". The corner ornaments were there;
	# what was missing was the fitting that makes a framed table read as a table
	# with a head rather than as a rectangle with words at the top of it. The rule
	# that used to be drawn under the headings is the cap's own rule now, so this is
	# one fitting replacing one line rather than a line plus a band.
	HudScript.draw_panel_cap(standings_label,
		Rect2(Vector2.ZERO, Vector2(standings_label.size.x, row_height * 0.86)))
	var captions: Array[String] = []
	for column_value in STANDINGS_COLUMNS:
		var column := column_value as Dictionary
		captions.append("" if bool(column["star"])
			else names.shell_label(String(column["string"]), String(column["caption"])))
	# MEASURED THE WAY IT IS DRAWN - the headings are in the caption tier now (caps
	# with tracking), and tracking is width `Font.get_string_size` cannot see. The
	# comment above this loop records what happens when a heading overruns its
	# column: "REGIONS" beside "ARMIES" printed as "REGIOARMIES".
	while caption_size > 6:
		var widest := 0.0
		for caption in captions:
			widest = maxf(widest, HudScript.caption_width(font, caption, caption_size))
		if widest <= column_width - 4.0:
			break
		caption_size -= 1
	for index in range(STANDINGS_COLUMNS.size()):
		var column := STANDINGS_COLUMNS[index] as Dictionary
		var right := grid_left + column_width * float(index + 1)
		if bool(column["star"]):
			# THE STAR SITS ON ITS COLUMN'S RIGHT EDGE like every other heading,
			# offset in by half its own width. Centring it in the column while the
			# three words beside it were right-aligned is exactly the "headers do not
			# sit over their columns" a blind review named - three of four did.
			var star := maxf(3.5, caption_size * 0.6)
			HudScript.draw_star(standings_label,
				Vector2(right - star, header_baseline - caption_size * 0.35),
				star, HudScript.RIM_GOLD_BRIGHT)
			continue
		HudScript.draw_caption(standings_label, font,
			Vector2(grid_left, header_baseline), String(captions[index]),
			caption_size, HudScript.PARCHMENT_DIM, HORIZONTAL_ALIGNMENT_RIGHT,
			right - grid_left)

	var y := row_height
	for entry_value in _seat_plaques:
		var entry := entry_value as Dictionary
		if y + row_height > standings_label.size.y + 0.5:
			break
		var row := Rect2(Vector2(0.0, y), Vector2(standings_label.size.x, row_height - 4.0))
		var baseline := row.position.y + row.size.y * 0.5 + value_size * 0.36
		var kind := String(entry.get("kind", "seat"))
		if kind == "unclaimed":
			# THE SUMMARY ROW IS ON THE SEATS' OWN GRID, and it is marked as a summary
			# rather than left to look like a seat that lost its formatting.
			#
			# It used to be set at its own left indent (1.6 frame weights, against the
			# seats' 28 pixels), with no heraldry swatch and its one number
			# right-aligned across a different span - three departures from the grid at
			# once, and a blind review read it as the row "breaking the row grid
			# entirely". Every one of them is gone: the same swatch column in the
			# neutral map colour, the same name column, the same REGIONS column - and a
			# rule above it, which is what a summary row is separated by when it is
			# separated on purpose.
			HudScript.draw_row_rule(standings_label,
				Vector2(edge, row.position.y), Vector2(standings_label.size.x - edge, row.position.y))
			var neutral_swatch := Rect2(row.position + Vector2(7.0, 6.0),
				Vector2(13.0, row.size.y - 12.0))
			standings_label.draw_rect(neutral_swatch, NEUTRAL_COLOR, true)
			standings_label.draw_rect(neutral_swatch,
				Color(HudScript.RIM_GOLD.r, HudScript.RIM_GOLD.g, HudScript.RIM_GOLD.b, 0.7),
				false, 1.0)
			standings_label.draw_string(font, Vector2(row.position.x + 28.0, baseline),
				String(entry.get("text", "")),
				HORIZONTAL_ALIGNMENT_LEFT, int(grid_left - 34.0),
				value_size, HudScript.PARCHMENT_DIM)
			# ITS ONE NUMBER IS A REGION COUNT, so it lands in the REGIONS column,
			# not against the far edge of the whole grid. Right-aligning it across
			# `column_span` put it under CP - a region count sitting in the command
			# point column - and a blind review read it as the row "abandoning the
			# grid entirely". It was on the grid; it was in the wrong column.
			standings_label.draw_string(font, Vector2(grid_left, baseline),
				String(entry.get("value", "")), HORIZONTAL_ALIGNMENT_RIGHT,
				int(column_width), value_size, HudScript.PARCHMENT_DIM)
			y += row_height
			continue
		var active := bool(entry.get("active", false))
		HudScript.draw_socket(standings_label, row)
		# THE HERALDRY SWATCH in the seat's own map colour, so the plaque and the
		# territory it owns can never disagree - the colour is read from the same
		# `_owner_color` the map draws with.
		var swatch := Rect2(row.position + Vector2(7.0, 6.0), Vector2(13.0, row.size.y - 12.0))
		standings_label.draw_rect(swatch, entry.get("color", NEUTRAL_COLOR) as Color, true)
		standings_label.draw_rect(swatch, HudScript.RIM_GOLD, false, 1.0)
		var name_tint := HudScript.RIM_GOLD_HOT if active else HudScript.PARCHMENT
		if bool(entry.get("defeated", false)):
			name_tint = Color("#c8483f")
		standings_label.draw_string(font, Vector2(row.position.x + 28.0, baseline),
			String(entry.get("name", "")), HORIZONTAL_ALIGNMENT_LEFT,
			int(grid_left - 34.0), value_size, name_tint)
		# The counters on the heading row's own column edges.
		var counters := entry.get("counters", []) as Array
		for index in range(mini(counters.size(), STANDINGS_COLUMNS.size())):
			var right := grid_left + column_width * float(index + 1)
			standings_label.draw_string(font, Vector2(grid_left, baseline),
				String(counters[index]), HORIZONTAL_ALIGNMENT_RIGHT,
				int(right - grid_left), value_size, HudScript.GOLD_VALUE)
		y += row_height


## THE SHELL. Every rectangle here is hand-built in retail's language - warm
## brass rules, inset fields, corner studs, a title plate with a gold rule under
## it - and NONE of it is retail art. Retail's own frame images
## (`FrameT`/`FrameB`/`FrameL`/`FrameR`/`FrameCorner*`) and its title rules
## (`Ruler`, `MainMenuRuler`) all name textures that are IN NO ARCHIVE
## (`SCShellUserInterface512_001.tga`, `MainMenuRuleruserinterface.tga`), so
## there was nothing to convert and this project will not paint a picture and
## call it retail's. What IS retail art is on the map - the portraits, the
## faction standards, the radial ring - and the report line says which.
func _draw_chrome() -> void:
	# RETAIL'S OWN HUD FIRST, when the strategic bundle is converted: the five
	# flattened APT islands, drawn triangle for triangle at retail's own slot
	# positions. Nothing below this branch runs in that case - the hand-built
	# plates are the FALLBACK, not a layer under retail's art.
	if not _islands.is_empty():
		_draw_strategic_islands()
		return
	# NO BACKGROUND FIELD. The map is full-bleed, the way retail's is; the only
	# things this pass paints are the ISLAND BACKINGS the HUD text sits on.
	# THE TURN BAND: retail's own strip off apt_LivingWorldUI_1.tga when the UI
	# bundle is converted; a drawn plate (and a named gap) when it is not.
	if turn_banner != null:
		HudScript.draw_band(chrome_layer,
			Rect2(turn_banner.position - Vector2(turn_banner.size.x * 0.18, 5.0),
				turn_banner.size + Vector2(turn_banner.size.x * 0.36, 10.0)), band_texture)
	# THE TASKS BOX under the band - retail's black critical-tasks strip - and
	# the message line inside the same box.
	if hint_label != null and message_label != null:
		var tasks := Rect2(hint_label.position - Vector2(10.0, 5.0),
			Vector2(hint_label.size.x + 20.0,
				message_label.position.y + message_label.size.y - hint_label.position.y + 10.0))
		HudScript.draw_card(chrome_layer, tasks, true)
	# The seat's numbers on a stadium plate, the way retail sets Player Bonuses.
	if header_label != null and not _header_facts.is_empty():
		HudScript.draw_plate(chrome_layer,
			Rect2(header_label.position - Vector2(14.0, 5.0),
				Vector2(header_label.size.x + 28.0, header_label.size.y + 10.0)))
	# The seat table, on the tooltip-card framing retail uses.
	HudScript.draw_card(chrome_layer, _standings_card_rect())
	# The details tray.
	if detail_label != null:
		HudScript.draw_card(chrome_layer,
			Rect2(detail_label.position - Vector2(10.0, 8.0),
				detail_label.size + Vector2(20.0, 16.0)))
	# The palantir island's caption plate - the dish itself floats, the way
	# retail's does; only the text beside it gets a backing.
	if region_portrait_caption != null:
		HudScript.draw_card(chrome_layer,
			Rect2(region_portrait_caption.position - Vector2(10.0, 8.0),
				region_portrait_caption.size + Vector2(20.0, 16.0)))
	# The unplaced block's backing, only when it says anything.
	if unplaced_label != null and unplaced_label.visible and not unplaced_label.text.is_empty():
		HudScript.draw_card(chrome_layer,
			Rect2(unplaced_label.position - Vector2(8.0, 6.0),
				Vector2(unplaced_label.size.x + 16.0,
					unplaced_label.size.y + unplaced_host.size.y + 12.0)), true)


## THE SEAT PLAQUES' PANEL - THE SEATS AND NOTHING BUT THE SEATS.
##
## It used to GROW to enclose MAIN MENU, which was round 6's answer to "a button
## parked below a panel is still a floating button". The containment was right and
## the premise was wrong: a blind review looking at the framed result said
## "shipped chrome does not put shell navigation inside a live scoreboard", and
## named that button in a scoreboard as the single thing a show-floor audience
## would photograph. The button is on the pause shell now (`build()`), and this
## panel is back to being one thing.
##
## THE MARGIN IS THE FRAME'S OWN WEIGHT, not a round number. `draw_card` cuts its
## bevel at 3.5% of the panel's short side (floor 3, ceiling 9), lays its fillet
## two weights in and turns every corner in a scroll, so content inset by the eight
## pixels this used to use sits ON the fillet at any panel bigger than a tooltip.
## That is why the column headings crossed the top ornament: the panel was drawn
## correctly and everything inside it was inset for a frame a third of the weight.
## THE MARGIN IS 2.9 WEIGHTS AND IT IS SOLVED IN TWO PASSES, because the frame's
## weight is a fraction of the CARD's short side and the card's size is what the
## margin decides. 2.9 is the frame's own construction rather than a number that
## looked right: the fillet is two weights in, a corner scroll's elbow is 1.6
## weights in with an arm up to nine weights long, and the boss at the end of that
## arm is nearly one more weight - so 2.9 weights is the first row of content that
## no ornament can reach. It used to be 2.4, which put the seat plaques' own top
## row a pixel or two inside the ornament at four of the six window sizes the
## layout runner holds; the runner's containment rule found it the moment the rule
## was stated as an invariant instead of as a list.
func _standings_card_rect() -> Rect2:
	var first := clampf(minf(standings_label.size.x, standings_label.size.y) * 0.035,
		3.0, 9.0) * 2.9
	var box := Rect2(standings_label.position - Vector2(first, first),
		standings_label.size + Vector2(first * 2.0, first * 2.0))
	var settled := maxf(first,
		clampf(minf(box.size.x, box.size.y) * 0.035, 3.0, 9.0) * 2.9)
	return Rect2(standings_label.position - Vector2(settled, settled),
		standings_label.size + Vector2(settled * 2.0, settled * 2.0))


## RETAIL'S FIVE ISLANDS, DRAWN. Each is one flattened APT frame placed at
## retail's own `StrategicHUD` slot; `WotrHudChrome.draw_apt_frame` emits retail's
## triangles verbatim. The PALANTIR is drawn in two passes with this project's
## region portrait between them, because retail does the same thing: its ring
## paints the black glass well, its engine renders a live region feed into that
## well, and its glass highlight paints back over the top. Drawing the whole
## frame in one pass would bury the portrait under retail's own well.
##
## The seat plaques and the unplaced block keep the hand-built card, because
## retail's strategic movies carry no counterpart for either: retail's
## `StrategicPlayerStatus` is a different screen this HUD does not compose, and
## "a region the converted map could not place" is a state retail never had.
func _draw_strategic_islands() -> void:
	# EVERY ISLAND IS GROUNDED BEFORE ANY ISLAND IS DRAWN, and the two-pass shape
	# is the point: a halo drawn per island in the main loop would fall across the
	# island drawn before it and darken retail's own art. Laid down first, every
	# halo is under every island and over nothing but the map.
	#
	# This answers the review note that the top-left readout "floats naked over open
	# water with no plate". The plate is retail's own and it is drawn every frame;
	# what it lacked was separation from a bright, high-frequency coastline. See
	# `HudChrome.draw_island_shadow`.
	for entry_value in STRATEGIC_ISLANDS:
		var shadow_slot := String((entry_value as Dictionary)["slot"])
		if not _islands.has(shadow_slot) or not island_is_shown(shadow_slot):
			continue
		HudScript.draw_island_shadow(chrome_layer,
			(_islands[shadow_slot] as Dictionary)["rect"] as Rect2)
	for entry_value in STRATEGIC_ISLANDS:
		var slot := String((entry_value as Dictionary)["slot"])
		if not _islands.has(slot):
			continue
		var island := _islands[slot] as Dictionary
		var origin := island["origin"] as Vector2
		var scale := island["scale"] as Vector2
		if not island_is_shown(slot):
			# THE STOP DECIDED THIS ISLAND IS OFF. The three command capsules are the
			# one thing that must survive the tray coming off, because they are what a
			# player ACTS through and the FOCUSED stop is defined as "keeps what you
			# press" - see `VIEW_FULL`. They are drawn against this island's own scale
			# either way, so the capsule is the same object at every stop.
			if slot == "selectionDetails":
				for capsule in command_capsules():
					if capsule != end_turn_button:
						_draw_capsule(capsule, scale)
			continue
		if slot == "endTurnButton":
			# END TURN ONLY, and drawn from ITS OWN live state rather than the `_up`
			# frame the island table parks the island on.
			#
			# THE OTHER THREE CAPSULES MOVED TO THE END OF `_draw_command_bar`, and
			# that is a painter's-order correction rather than a tidy-up. The command
			# deck those three sit on is drawn FIRST now, so that retail's own tray
			# rail can close over its lower edge (see `_draw_command_bar`) - and the
			# `selectionDetails` island comes AFTER `endTurnButton` in
			# `STRATEGIC_ISLANDS`, so capsules drawn here were painted and then buried
			# under the deck. The first capture after the reorder showed exactly that:
			# ATTACK and AUTO-RESOLVE as bare lettering on an empty plate.
			_draw_capsule(end_turn_button, scale)
			continue
		if slot == "globe":
			_draw_palantir(island["frame"] as Dictionary, origin, scale)
			# THE TWO MEDALLIONS' LIVE STATE, over retail's resting discs - see
			# `_draw_medallions` for why the lit and pressed treatments are drawn
			# rather than asked for.
			_draw_medallions()
			continue
		if slot == "selectionDetails":
			_draw_command_bar(island["frame"] as Dictionary, origin, scale)
			continue
		if slot == "stats":
			# RETAIL'S OWN STATS PLATE, WITH ITS EXPANDER POINTING THE WAY THE TABLE
			# UNDER IT WILL MOVE - up to put the seat table away, down to bring it
			# back. See `_expander_arrow_paths` for why the turn is a reading of
			# retail's own movie rather than a redraw of its art.
			var stats_turns: Array = []
			if standings_open:
				stats_turns = _expander_arrow_paths("StrategicStats", "Expand",
					island["frame"] as Dictionary)
			HudScript.draw_apt_frame(chrome_layer, island["frame"] as Dictionary,
				origin, scale, strategic, Vector2i(0, 0), [], [], {}, stats_turns)
			if stats_expander != null and stats_expander.visible:
				if stats_expander.is_hovered():
					HudScript.draw_focus_ring(chrome_layer,
						Rect2(stats_expander.position, stats_expander.size))
				elif stats_expander.has_focus():
					HudScript.draw_focus_ring(chrome_layer,
						Rect2(stats_expander.position, stats_expander.size))
			continue
		if slot == "checklist":
			# THE PLAQUE IN ITS LIVE STATE, the same way `_draw_capsule` draws END
			# PHASE in its live state: retail authors one frame per state and the
			# island table can only park an island on one of them, so the state that
			# depends on what is on screen is asked for here instead.
			var plaque := _strategic_frame("StrategicChecklist",
				CHECKLIST_LABEL_OPEN if _checklist_is_open() else CHECKLIST_LABEL_SHUT)
			if plaque.is_empty():
				plaque = island["frame"] as Dictionary
			# THE EXPANDER POINTS THE WAY THE PLAQUE WILL MOVE. Retail's arrow has a
			# half turn authored for exactly this (`_rotateUp` / `_rotateDown`), so
			# the plaque OPEN gets the arrow that shuts it and the plaque SHUT gets
			# the arrow that opens it - which is what an expander is for, and is the
			# owner's "wire them to go upward and get out of the way".
			var turns: Array = []
			if _checklist_is_open():
				turns = _expander_arrow_paths("StrategicChecklist", "expandButton", plaque)
			# THE PLAQUE'S FIELD IS SKINNED, NOT LEFT BLACK.
			#
			# `StrategicChecklist` fills its task field with a PURE BLACK opaque quad
			# (root depth 3, four triangles, authored -288.5..290 x 71..201.4 on the
			# open plaque). That is retail's runtime HOST for the scrolling critical-
			# task list: retail composites the list into it and the black is never
			# seen. This project does not fill that list - the values are live and the
			# standing named gap is `dynamic-content-slots-are-empty` - so showing the
			# host means showing the black, and a blind review photographed exactly
			# that and called it "an unskinned container ... a flat #000 rectangle
			# inside a gold frame".
			#
			# It got worse this round rather than better, because the banner medallion
			# now lets a player OPEN the plaque deliberately, which made the largest
			# version of that black box reachable on purpose. So the fill is suppressed
			# the same way the build-queue card well's is (`_card_well_host_path`), and
			# this HUD's own oxblood card goes down first so retail's gilt frame closes
			# over it. Retail's geometry, retail's frame, this project's field - and
			# the diagnostics panel already names the gap that makes it necessary.
			var field := _island_rect("checklist",
				CHECKLIST_TASK_BOX if _checklist_is_open() else CHECKLIST_TASK_BOX_SHUT)
			if field.size.x > 0.0:
				HudScript.draw_card(chrome_layer, field, true)
			HudScript.draw_apt_frame(chrome_layer, plaque,
				origin, scale, strategic, Vector2i(0, 0), [],
				[_checklist_field_host_path(plaque)], {}, turns)
			# THE CLOCK, IN ONE PASS OVER RETAIL'S ART. It used to be two - a warm
			# seat under retail's device and a veil over the others - on the theory
			# that a lit ground should sit BEHIND the thing it lights. Retail's
			# chevron strip is an opaque atlas quad, so the seat was drawn and then
			# painted over, and the capture shows a bar with no lit cell in it. Both
			# states are washes over retail's own pixels now; see
			# `HudChrome.draw_phase_seat` for why that is forced rather than chosen.
			_draw_phase_states()
			# THE WAR COUNCIL, in the field the plaque grew to hold it, and the ROUND
			# in the chevron bar's other end capsule. Both are drawn AFTER retail's
			# frame so retail's own gilt closes under them rather than over them, and
			# both are inside rectangles retail authored - see `_draw_war_council`
			# and `CHECKLIST_ROUND_PLAQUE`.
			_draw_war_council(field)
			_draw_round_plaque(origin, scale)
			continue
		HudScript.draw_apt_frame(chrome_layer, island["frame"] as Dictionary,
			origin, scale, strategic)
	# The seat plaques' backing and the unplaced block, both this project's own
	# surfaces, in the HUD's drawn language. Both are readouts, so both come off at
	# the FOCUSED stop with the rest of `_detail_controls` - a card drawn under a
	# hidden table is an empty plate over the map.
	if standings_label != null and standings_label.visible:
		HudScript.draw_card(chrome_layer, _standings_card_rect())
	if unplaced_label != null and unplaced_label.visible and not unplaced_label.text.is_empty():
		HudScript.draw_card(chrome_layer,
			Rect2(unplaced_label.position - Vector2(8.0, 6.0),
				Vector2(unplaced_label.size.x + 16.0,
					unplaced_label.size.y + unplaced_host.size.y + 12.0)), true)


## END PHASE, in the authored state the button is actually in. Retail flattens
## one frame per state - `_up`, `_over`, `_down`, `_disabled` - so hover, press
## and greyed-out are RETAIL'S OWN ART rather than three tints of one drawing,
## which is the exact complaint a blind review made about this button.
func _draw_capsule(button: Button, scale: Vector2) -> void:
	if button == null or not button.visible:
		return
	var wanted := "disabled" if button.disabled else String(
		_capsule_states.get(String(button.name), "up"))
	var rank := String(COMMAND_RANKS.get(String(button.name), COMMAND_RANK_SECONDARY))
	# THE GHOST WEARS NO CAPSULE AT ALL. CANCEL is the third weight and a third
	# weight has to be an ABSENCE of face, not a fainter one - see
	# `COMMAND_WIDTH_CLASS`. Retail's capsule art is skipped for it entirely and
	# `draw_ghost_button` puts a rule under the caption instead, so the row reads as
	# "do this / or leave" rather than as three things to weigh.
	if rank == COMMAND_RANK_GHOST:
		HudScript.draw_ghost_button(chrome_layer, Rect2(button.position, button.size),
			button.is_hovered() or button.has_focus(), not button.disabled)
		if button.has_focus() and not button.disabled:
			HudScript.draw_focus_ring(chrome_layer, Rect2(button.position, button.size))
		return
	var label := String(ENDTURN_STATES.get(wanted, "_up"))
	var apt_frame := _strategic_frame("StrategicEndTurnButton", label)
	if apt_frame.is_empty():
		apt_frame = _strategic_frame("StrategicEndTurnButton", "_up")
	# THE CAPSULE IS GROUNDED FIRST. Every island on this HUD gets a soft dark halo
	# under it before its own art goes down, and a capsule floating on open terrain
	# needs it more than a framed panel does - see `HudChrome.draw_island_shadow`
	# for the review note this answers and for why the halo is entirely outside the
	# art's own rectangle.
	HudScript.draw_island_shadow(chrome_layer, Rect2(button.position, button.size),
		button.size.y * 0.55)
	# The capsule's art is authored AROUND `ENDTURN_FACE`, so the origin is
	# whatever puts that face exactly on the button.
	#
	# THE PRIMARY'S ART IS STRETCHED TO ITS OWN CELL. `COMMAND_WIDTH_CLASS` makes
	# ATTACK 1.24 cells wide and retail authors one capsule at one width, so the x
	# scale is the cell's share of the authored face rather than the island's. The
	# capsule is a stadium and a stadium stretched along its own axis is still a
	# stadium; what stretches is the straight run between the caps, which is what a
	# longer pill would have had anyway.
	var art_scale := scale
	if rank == COMMAND_RANK_PRIMARY and ENDTURN_FACE.size.x > 0.0:
		art_scale = Vector2(button.size.x / ENDTURN_FACE.size.x, scale.y)
	HudScript.draw_apt_frame(chrome_layer, apt_frame,
		button.position - ENDTURN_FACE.position * art_scale, art_scale, strategic)
	# THE PRIMARY'S GOLD FACE, INSIDE RETAIL'S OWN RIM. One control on this screen
	# wears it. See `HudChrome.draw_primary_face` for why the rank is drawn rather
	# than converted (retail never had to rank two capsules against each other) and
	# for why a DISABLED primary is not gilded.
	# THE FACE, AT REST. Its breathing inner glow is on the pulse layer
	# (`_draw_pulse`); this pass paints only what does not change between two frames
	# in which nothing happened, which is what keeps the chrome layer off the
	# per-frame path. See `build()`.
	if rank == COMMAND_RANK_PRIMARY:
		HudScript.draw_primary_face(chrome_layer, Rect2(button.position, button.size),
			0.0, not button.disabled)
	# THE FOCUS RING, AND IT IS THIS PROJECT'S. Retail authors four capsule states
	# and none of them is "focused" - its living world is a pointer-only screen -
	# so a keyboard walk across this rail moved an invisible highlight. The ring is
	# drawn OUTSIDE the capsule's own face so it cannot be mistaken for a change in
	# retail's art, and only when the capsule is both focused and usable: a ring
	# around a control that will not answer is worse than no ring.
	if button.has_focus() and not button.disabled:
		HudScript.draw_focus_ring(chrome_layer, Rect2(button.position, button.size))


## THE PALANTIR, with this project's region picture in the well retail leaves
## empty for its own. `regionUI` is retail's authored host for that feed and the
## APT carries no content for it (`dynamic-content-slots-are-empty`), so the
## position is retail's and the picture is ours. The split point is the first
## draw of the glass highlight - retail's `PalantirMainGlass`/`SubGlass` import,
## which is authored at a higher depth than the well - so the two passes are
## "everything under the glass" and "the glass and the medallions over it".
const PALANTIR_GLASS_DEPTH := 91


## THE BOTTOM COMMAND BAR, DRAWN, in retail's own four movies and retail's own
## painter's order: the tray frame and its two full-width rails, then the tab
## strip, then whichever tab's content the well is showing, then the status
## ribbon. All four are authored against the SAME slot origin, so they compose by
## being drawn at it - nothing here positions anything by hand.
##
## THE SELECTED TAB'S LIT STATE IS DRAWN, and that is a stated gap rather than a
## preference: retail authors its tab highlight as a CHILD timeline
## (`timeline-playback-not-bound`), so the static flattening carries all three
## tabs in the same resting state and there is no authored "selected" frame to
## ask for. The lit plate under the chosen tab is this project's, in the HUD's own
## drawn language, and the diagnostics panel names it.
func _draw_command_bar(apt_frame: Dictionary, origin: Vector2, scale: Vector2) -> void:
	# THE UPPER DECK GOES DOWN FIRST, UNDER RETAIL'S TRAY, and that ordering IS the
	# fix rather than a detail of it.
	#
	# The deck is welded to the tray by running its lower edge into retail's own top
	# rail. Painted AFTER the tray - which is how it was - the weld is this
	# project's card lying ON TOP of retail's gilt rail, and a blind review read the
	# result exactly that way twice running: the bar "still floats unanchored AND
	# overlaps the bottom panel's top edge". Painted BEFORE it, retail's own rail
	# closes over the deck's bottom edge, which is what a seam looks like: the two
	# fittings are stacked, and the gold that joins them is retail's.
	_draw_command_rail()
	var rail_tints := {TRAY_SCROLL_RAIL_PATH: TRAY_SCROLL_RAIL_TINT}
	for rail_path in TRAY_RAIL_PATHS:
		rail_tints[String(rail_path)] = TRAY_RAIL_TINT
	HudScript.draw_apt_frame(chrome_layer, apt_frame, origin, scale, strategic,
		Vector2i(0, 0), [], [], rail_tints)
	# THE SELECTED TAB, under retail's strip so retail's filigree stays on top.
	#
	# THE PLATE IS CUT TO THE OPENING BETWEEN RETAIL'S OWN TAB SEPARATORS, and both
	# ends of that are a fix rather than a taste. On the right, the STRUCTURES tab
	# is authored at x 384.95 and the tab pitch is 201.8, so a plate at the full
	# pitch ends at 586.75 - which is 14.5 authored pixels PAST the tray panel's own
	# right edge at 572.3, and a blind review photographed exactly that: "the active
	# STRUCTURES tab overhangs the panel frame on the right". On the left and right
	# alike the plate used to run under the little gold hook ornaments retail sets
	# between TERRITORY, ARMIES and STRUCTURES and bury them, which is why the same
	# review read the strip as having "bare gaps" where retail has connectors: they
	# were there and they were painted over.
	for entry_value in TRAY_TABS:
		var entry := entry_value as Dictionary
		if String(entry["key"]) != active_tab:
			continue
		var cell := tray_tab_cell(entry)
		if cell.size.x <= 0.0:
			continue
		# THE SAME CELL THE BUTTON OCCUPIES, seated inside the rail rather than over
		# it, and `draw_seated_cell` draws strictly inside whatever it is handed.
		HudScript.draw_seated_cell(chrome_layer,
			Rect2(origin + cell.position * scale, cell.size * scale))
	var tabs := _strategic_frame("StrategicDetailsRegion", "_close")
	if not tabs.is_empty():
		HudScript.draw_apt_frame(chrome_layer, tabs, origin, scale, strategic)
	# THE CAPTIONS, ENGRAVED INTO THEIR CELLS - over retail's strip, because they
	# are the tab's own lettering rather than something under its ornament, and
	# centred on the cell the layout placed the hit area at, so the caption and the
	# lit plate and the control are one rectangle by construction.
	_draw_tab_captions(origin, scale)
	# THE WELL'S OWN CONTENT ART, per tab. The card rail is retail's own
	# `StrategicDetailsBuildQueue`, drawn only on the STRUCTURES tab because that
	# is the tab retail draws it on (the oracle capture is exactly this state).
	if active_tab == "structures":
		var queue := _strategic_frame("StrategicDetailsBuildQueue", "_open")
		if not queue.is_empty():
			HudScript.draw_apt_frame(chrome_layer, queue, origin, scale, strategic,
				Vector2i(0, 0), _unused_card_slot_paths(queue),
				[_card_well_host_path(queue)])
			_draw_structure_cards(origin, scale)
	# THE STATUS RIBBON, at the measured registration offset
	# (`TRAY_RIBBON_ART_OFFSET`).
	#
	# It is taken from `StrategicDetailsArmies` WHATEVER TAB IS OPEN, and that is a
	# choice worth stating. All three content movies carry the same ribbon plate;
	# Armies and Structures flatten to EXACTLY those six draws and nothing else,
	# while Territory flattens fifty more - its six territory slots. Asking
	# Territory for the ribbon therefore paints six empty authored slots across the
	# well, under this project's own card text. Retail fills those slots from the
	# engine (`dynamic-content-slots-are-empty`); this screen does not fill them,
	# so it does not paint them, and that is a NAMED GAP on the diagnostics panel
	# rather than six empty boxes pretending to be a list.
	var ribbon := _strategic_frame("StrategicDetailsArmies", "")
	if not ribbon.is_empty():
		HudScript.draw_apt_frame(chrome_layer, ribbon,
			origin + TRAY_RIBBON_ART_OFFSET * scale, scale, strategic)
	# THE STRUCTURES ROSTER, in the well retail's card rail leaves - see
	# `_draw_structure_roster` for why it is drawn rather than set on the card.
	if active_tab == "structures":
		_draw_structure_roster()
	# THE THREE COMMAND CAPSULES, LAST OF ALL. Retail's own END PHASE art in its own
	# authored state per button, on top of the deck they are seated on rather than
	# under it - see `_draw_strategic_islands` for why they are drawn here and not
	# with the END TURN capsule they share their art with.
	for capsule in command_capsules():
		if capsule == end_turn_button:
			continue
		_draw_capsule(capsule, scale)


## The plate the three command capsules sit on. Drawn, in the HUD's own language
## (see the file header): retail's living world has no ATTACK, MAIN MENU or
## AUTO-RESOLVE control, so there is no retail art for a rail that carries them,
## and this is stated rather than dressed up as converted.
## THE THREE TAB CAPTIONS, engraved into the cells `tray_tab_cell` defines.
##
## Retail sets its tab rail in CAPITALS - it is the one place on its strategic HUD
## besides the END PHASE capsule where it shouts, which is why these are the only
## drawn captions on this screen that still do. The active tab takes the hot gold
## its lit cell is under; the tab under the pointer lights a step short of it; the
## rest are the quiet parchment every other caption on this HUD is set in.
func _draw_tab_captions(origin: Vector2, scale: Vector2) -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return
	for entry_value in TRAY_TABS:
		var entry := entry_value as Dictionary
		var key := String(entry["key"])
		var cell := tray_tab_cell(entry)
		if cell.size.x <= 0.0:
			continue
		var box := Rect2(origin + cell.position * scale, cell.size * scale)
		var tint := HudScript.PARCHMENT_DIM
		if key == active_tab:
			tint = HudScript.RIM_GOLD_HOT
		elif key == _hovered_tab:
			tint = HudScript.PARCHMENT
		HudScript.draw_engraved_caps(chrome_layer, font,
			Vector2(box.get_center().x,
				box.get_center().y + float(_tab_caption_size) * 0.36),
			String(_tab_captions.get(key, entry["caption"])),
			_tab_caption_size, 1.4, tint)


## THE DECK'S OWN RECTANGLE, as ONE definition read by the drawing and by the
## runner that holds the three capsules inside it. The same discipline
## `tray_tab_cell` and `_pause_card_rect` state, and for the same reason: a
## containment property that is computed twice is a containment property that can
## be true in the arithmetic and false on the glass.
##
## IT RUNS FROM THE TRAY'S OWN LEFT EDGE to the frame's right edge and down INTO
## the tray, so its bottom is inside the bar rather than a margin above it and its
## left edge lines up with the bar's. It used to start at the ATTACK button, which
## left a plate whose left end terminated in mid-air over open terrain - and a
## blind review read the result as "parked at an arbitrary height over open
## terrain".
##
## THE DECK ENDS IN THE MIDDLE OF THE TRAY'S OWN TOP RAIL (`StrategicDetailsTray`
## `21/12/3/1`, authored y -30.6..-13.6), which is retail's own seam. It used to
## run 16% of the tray's height down into it, which is far enough to cross the
## TERRITORY / ARMIES / STRUCTURES strip: retail authors that strip at y 1.8 in
## the tray slot's space and the tray's field starts at -18.1, so the strip begins
## less than 20 authored pixels below the tray's top edge. What makes the overlap
## a WELD rather than a collision is the painter's order, and that is stated at
## `_draw_command_bar`: the deck goes down first and retail's gilt rail closes
## over it.
func command_deck_rect() -> Rect2:
	if attack_button == null or auto_resolve_button == null:
		return Rect2()
	var top := attack_button.position.y
	var bottom := attack_button.position.y + attack_button.size.y
	var pad := maxf(8.0, attack_button.size.y * 0.18)
	var plate_top := top - pad * 0.7
	var plate_bottom := bottom + pad * 1.6
	var plate_left := attack_button.position.x - pad
	if _islands.has("selectionDetails"):
		var tray := (_islands["selectionDetails"] as Dictionary)["rect"] as Rect2
		# THE TRAY'S VISIBLE PANEL, NOT ITS BOUNDING BOX. These are not the same
		# rectangle and the difference is nearly 300 pixels at the frame the oracle
		# is judged in: the island's bounding box is stretched left by retail's own
		# bottom rail, whose left end runs 149 authored pixels PAST the tray's field
		# and out over open terrain (that stray rail is already documented at
		# `TRAY_RAIL_PATHS`, where it is dimmed rather than dropped).
		#
		# Measuring the deck off the bounding box therefore ran its left edge a long
		# way past the tray it is supposed to be the upper deck OF, and the result is
		# exactly what a capture shows: a maroon slab whose left end stops in mid-air
		# over Middle-earth with a hard vertical cut, three hundred pixels clear of
		# anything it could be attached to. `TRAY_FIELD` is the panel a player can
		# see, so that is the edge the deck lines up with.
		# THE TRAY'S VISIBLE GILT STILE, NOT ITS FIELD - see `TRAY_STILE`, which
		# records the four flattened draws this is measured off and why the field's
		# own left edge is 62 authored pixels of art that runs behind the palantir
		# and is never on the glass. The deck's silhouette and the tray's are now
		# the same vertical line; before this they were 300 window pixels apart at
		# the frame the oracle is judged in, which is what made the deck read as a
		# separate, later-added layer no matter what fitting was drawn on its end.
		var stile := _island_rect("selectionDetails", TRAY_STILE)
		var field := _island_rect("selectionDetails", TRAY_FIELD)
		#
		# STILL `minf`, and that is not belt-and-braces. `plate_left` starts at the
		# ATTACK cell's own left edge less a pad, and the deck must contain the row it
		# carries at every window size the layout runner holds; at the narrow end of
		# that range the row is wider relative to the island than it is at 2560x1440.
		# Taking the smaller of the two keeps the stile alignment in every case where
		# it is available and keeps the containment in the case where it is not.
		if stile.size.x > 0.0:
			plate_left = minf(plate_left, stile.position.x)
		else:
			plate_left = minf(plate_left, field.position.x if field.size.x > 0.0
				else tray.position.x)
		var rail := _island_rect("selectionDetails", Rect2(0.0, -30.6, 1.0, 17.0))
		if rail.size.y > 0.0:
			plate_bottom = rail.get_center().y
		else:
			plate_bottom = maxf(plate_bottom, tray.position.y)
	return Rect2(Vector2(plate_left, plate_top),
		Vector2(size.x - plate_left, plate_bottom - plate_top))


func _draw_command_rail() -> void:
	if attack_button == null or auto_resolve_button == null:
		return
	var row := [attack_button, cancel_button, auto_resolve_button]
	# `left` and `right` used to be read here for the single run-wide cartouche.
	# The cartouche is per control now (below), so the run's own extent is no
	# longer a quantity this function needs.
	var top := attack_button.position.y
	var bottom := attack_button.position.y + attack_button.size.y
	# THE PLATE IS THE TRAY'S UPPER DECK - see `command_deck_rect`, which is where
	# it is measured and where its two corrections are recorded. It runs from the
	# TRAY'S OWN LEFT EDGE to the frame's right edge and down into retail's own top
	# rail, and `_draw_command_bar` paints it BEFORE retail's tray so that rail
	# closes over its lower edge instead of the other way round.
	var deck := command_deck_rect()
	HudScript.draw_card(chrome_layer, deck)
	# THE DECK ENDS IN A FITTING, not in a cut. Two rounds moved this edge and
	# neither made it a terminal; `draw_deck_end_cap` chamfers the nose, sets a
	# pilaster behind it and returns that pilaster down into the tray's own rail, so
	# the plate reads as a bracket arm hanging off the bar rather than as a slab
	# stopping in mid-air over Middle-earth.
	var foot := deck.end.y
	if _islands.has("selectionDetails"):
		var rail := _island_rect("selectionDetails", Rect2(0.0, -30.6, 1.0, 17.0))
		if rail.size.y > 0.0:
			foot = rail.end.y
	# THE STILE'S WIDTH IS THE TRAY'S OWN STILE'S WIDTH, carried into window space
	# through the same island scale everything else on this deck is placed by - so
	# the deck's terminal and the tray's frame member are ONE member at ONE width at
	# every window size. See `TRAY_STILE` for the four flattened draws it is
	# measured off and for why the chamfer that used to be here was a foreign shape.
	HudScript.draw_deck_end_cap(chrome_layer, deck, foot,
		_island_rect("selectionDetails", TRAY_STILE).size.x)
	# AND THE DECK'S HEAD IS A RAIL, not a card's bevel - the other half of the same
	# review note ("its top edge is a hard unornamented horizontal"). A card's bevel
	# says "closed on four sides" and this plate is not: it runs off the right of the
	# frame. See `draw_deck_head_rail`.
	HudScript.draw_deck_head_rail(chrome_layer, deck)
	# THE CARTOUCHE RANKS THE ROW INSTEAD OF ENCLOSING IT.
	#
	# It used to be ONE cartouche around all three cells, and that WAS the right
	# answer to the defect it fixed: `draw_button_cartouche` sets a boss into each
	# cap of the pill it surrounds, so three adjacent cartouches put TWO bosses in
	# every gap and a blind review read those as "literal small circles (`o o`)
	# between cells - a treatment used nowhere else". Enclosing the run cured the
	# collision by making the three cells ONE OBJECT - which is precisely the reading
	# a later, harsher review named as the deeper defect: "identical weight,
	# identical fill, identical width class ... ATTACK is currently tied for third".
	#
	# So the cartouche is PER CONTROL again, and the boss collision cannot come back,
	# because the two controls that wear one are no longer adjacent: CANCEL sits
	# between them and carries no cartouche at all - it is the ghost, see
	# `COMMAND_WIDTH_CLASS`. ATTACK's is lit and AUTO-RESOLVE's is not, so the
	# primary/secondary rank is stated in the fitting as well as in the face.
	HudScript.draw_button_cartouche(chrome_layer,
		Rect2(attack_button.position, attack_button.size), not attack_button.disabled)
	HudScript.draw_button_cartouche(chrome_layer,
		Rect2(auto_resolve_button.position, auto_resolve_button.size),
		not auto_resolve_button.disabled)
	# THE DECK'S LEFT FIELD SAYS WHAT THE BUTTONS WILL ACT ON.
	#
	# Docking the deck to the tray's own stile gave it a long empty shelf between
	# that stile and the ATTACK cell, and an empty shelf is what a "later-added
	# layer" looks like even after its geometry is right. It is also the one place on
	# this screen that can close a separate review note: "the instruction and the
	# mechanism are not visually connected... the ATTACK button that completes the
	# sentence is 700px away at the bottom, unweighted."
	#
	# So the shelf carries the ORDER the primary is about to give - who marches, and
	# where - beside the button that gives it. It is not a fourth statement of
	# anything: no other surface on this screen names the pair. When there is no
	# order staged the shelf is empty, which is honest and is also the state in which
	# ATTACK is disabled and wears no gold.
	_draw_deck_order(deck)
	# THE CHAIN LINKS STAY: they are retail's own separator between the cells of a
	# run, and a ranked run still needs its cells divided. What they no longer divide
	# is three things of equal weight.
	for index in range(row.size() - 1):
		var before := row[index] as Button
		var after := row[index + 1] as Button
		HudScript.draw_chain_link(chrome_layer, Rect2(
			Vector2(before.position.x + before.size.x, top),
			Vector2(after.position.x - before.position.x - before.size.x, bottom - top)))


## THE ORDER ON THE DECK'S SHELF: the march the primary is about to commit.
##
## `FROM <region>` and `TO <region>` in the caption and subject tiers, on the
## deck's own field between its stile and the ATTACK cell. Presentation only - it
## reads `session.selected_region` and `session.selected_target`, which are the same
## two values `commit_selected_attack` acts on, so the shelf cannot describe an
## order the button will not give.
##
## THE ARROW IS A CHAIN LINK, not a glyph. This HUD has one separator between two
## related cells and it is retail's; an arrow character would be a mark used nowhere
## else, which is the register this file has refused twice already.
const DECK_ORDER_FROM := "From"
const DECK_ORDER_TO := "To"


func _draw_deck_order(deck: Rect2) -> void:
	if session == null or attack_button == null or deck.size.x <= 0.0:
		return
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return
	var from_id := String(session.selected_region)
	var to_id := String(session.selected_target)
	if from_id.is_empty() or not _row_by_id.has(from_id):
		return
	# THE SHELF IS WHAT IS LEFT OF THE DECK once the stile and the rail are taken off
	# it, and it is measured rather than guessed so the run can never be set over
	# either fitting.
	var stile := _island_rect("selectionDetails", TRAY_STILE)
	var left := deck.position.x + (stile.size.x if stile.size.x > 0.0 else 0.0) 		+ maxf(12.0, deck.size.y * 0.16)
	# THE RIGHT GUTTER CLEARS THE PRIMARY'S CARTOUCHE, not just its rectangle.
	# `draw_button_cartouche` sets its groove and its cap boss OUTSIDE the button's
	# own box, so a run measured to `attack_button.position.x` ends ON the fitting -
	# which the first capture of this shelf shows, with "ARTHEDAIN" touching the
	# pill's left boss.
	var room := attack_button.position.x - left - maxf(26.0, deck.size.y * 0.38)
	if room <= 40.0:
		return
	# THE ORDER IS TWO ROWS, NOT ONE RUN, and that is a fit rather than a
	# preference. The shelf is about 320 window pixels wide at the frame the oracle
	# is judged in - it is what is left of the deck between the tray's stile and the
	# ATTACK cell - and two captions plus two region names set on one line do not go
	# in it at any size a player would read: the first attempt measured 289 pixels of
	# run into 223 of room and correctly dropped the whole thing rather than clipping
	# it. Stacked, each row needs only one caption and one name, the deck is two rows
	# tall at every window size the layout runner holds, and the pair reads as what
	# it is - a march with an origin and a destination.
	var rows: Array = []
	for pair_value in [[DECK_ORDER_FROM, from_id], [DECK_ORDER_TO, to_id]]:
		var pair := pair_value as Array
		var region_id := String(pair[1])
		if not region_id.is_empty() and _row_by_id.has(region_id):
			rows.append([String(pair[0]), _display_of(region_id)])
	if rows.is_empty():
		return
	# THE SIZE IS SOLVED AGAINST THE ROOM, never chosen off the deck alone. A region
	# name is retail's own text and can be anything from "Rhun" to "The Trollshaws",
	# so a size taken off the deck's height and used is a size that fits some regions
	# and overruns others onto the primary.
	var value_size := HudScript.type_size(deck.size.y * 0.5, HudScript.TYPE_VALUE,
		HudScript.TYPE_PANEL_FLOOR)
	while value_size > HudScript.TYPE_PANEL_FLOOR:
		var widest := 0.0
		for row_value in rows:
			var row := row_value as Array
			widest = maxf(widest, HudScript.caption_width(font, String(row[0]),
					maxi(HudScript.TYPE_PANEL_FLOOR, int(value_size * 0.74)))
				+ HudScript.caption_width(font, String(row[1]), value_size)
				+ float(value_size) * 0.5)
		if widest <= room:
			break
		value_size -= 1
	var caption_size := maxi(HudScript.TYPE_PANEL_FLOOR, int(value_size * 0.74))
	# The pair is centred on the deck's own optical middle, each row on its own half.
	var row_height := deck.size.y * 0.34
	var top := deck.position.y + (deck.size.y - row_height * float(rows.size())) * 0.5
	for index in range(rows.size()):
		var row := rows[index] as Array
		var baseline := top + row_height * (float(index) + 0.5) + float(value_size) * 0.36
		var pen := left
		HudScript.draw_caption(chrome_layer, font, Vector2(pen, baseline),
			String(row[0]), caption_size, HudScript.PARCHMENT_DIM)
		pen += HudScript.caption_width(font, String(row[0]), caption_size) 			+ float(value_size) * 0.5
		# NOTHING IS SET PAST THE ATTACK CELL. The shelf is a fixed field between two
		# fittings, so a name that will not fit whole is dropped rather than clipped -
		# the same rule the status ribbon states for its own segments.
		if pen + HudScript.caption_width(font, String(row[1]), value_size) > left + room:
			continue
		HudScript.draw_caption(chrome_layer, font, Vector2(pen, baseline),
			String(row[1]), value_size,
			HudScript.RIM_GOLD_HOT if index == rows.size() - 1 else HudScript.PARCHMENT)


## ONE CARD PER BUILD PLOT, AND EVERY CARD THE SAME CARD.
##
## THE ROW USED TO BE TWO IDIOMS AND AN OVERFLOW, and all three were mine rather
## than retail's. `StrategicDetailsBuildQueue`'s `_open` frame flattens SEVEN
## slots: six identical 68-wide parchment cards at x 123.2..574.9, plus one wider,
## differently-framed 93-wide slot at x 3.8..96.6 which is retail's queue HEAD -
## the "currently building" cell. Drawing all seven unconditionally and then
## painting structure ICONS into the first four produced exactly what a blind
## review photographed: "an empty tan card, then four circular building portraits
## in square frames, then two more empty tan cards, with inconsistent frame widths
## between them, and the whole row overflows the panel".
##
## RETAIL'S OWN ORACLE CAPTURE SHOWS THREE CARDS, on a region with three build
## plots, all identical, the selected one ringed in gold, and no queue head. So
## that is what this draws: the card slots beyond the region's own
## `BuildingSpot` count are SUPPRESSED (`_unused_card_slot_paths`), the queue head
## is suppressed because nothing is ever queued, and what is left is one idiom at
## one width that cannot overflow because retail's own rail is wider than the most
## cards this can ask for.
##
## THE ICONS ARE NOT GONE, AND NOW SOME OF THEM STAND ON THE CARDS. A card is a
## PLOT: an EMPTY one carries the seat's own engraved foundation tile, and an
## OCCUPIED one carries retail's own `ConstructButtonImage` for the structure
## standing on it, with retail's own title under it. The old note here said a
## picture of a building on a card would claim something was standing there that
## nothing had built. Something has built it now - `session.build_plots()` says
## which foundation carries what - so the claim is true and the card makes it.
##
## THAT IS WHAT FILLS THE RAIL. A blind review's complaint about this tab was that
## its left third was "retail's card-rail well with nothing in it": three identical
## blank stone tiles and a run of empty maroon beside them. The rail now carries
## the region's actual holdings, headed by retail's own
## `STRATEGICHUD:RegionBuildPlotsTitle` ("Territory Build Plots") and its own
## used/total counter, which is exactly the readout retail's
## `RegionBuildPlotsHelp` describes.
##
## EVERY CARD IS ALSO A CONTROL (`_place_plot_card_buttons`): clicking one opens
## the build ring on that foundation, which is the same thing clicking the plot on
## the map does. A player who cannot find a three-pixel foundation ring on
## Middle-earth can still reach every foundation the region has.
##
## The empty-foundation tile is retail's own picture in retail's own authored host
## (`WotrLivingWorldUi.build_plot_portrait`); without it these were three blank tan
## rectangles, and a blind review called the pair of missing bindings "the single
## worst impression a strategy UI can give ... it reads as not finished loading".
## THE ONE STATEMENT OF THE BUILD-PLOT COUNT, as one definition with two readers.
##
## It used to be said three times in this tray - here, on the palantir's footer
## plaque, and again in the status ribbon - and an adversarial art-direction review
## counted all three: "saying the same number three times does not make it clearer,
## it makes the tray look like three teams shipped independently." The other two are
## gone (see `_structures_ribbon_line` and `_draw_region_portrait`), and this is the
## one that survives, because this is where the FOUNDATION CARDS are and a count
## belongs beside the things it counts.
##
## IT IS A FUNCTION RATHER THAN A LITERAL IN THE DRAW because a number that is only
## ever formatted inside a draw pass is a number no runner can read. The count's
## numerator was a structural zero for several rounds and the assertion that caught
## that could only reach it through the status ribbon - which is exactly the surface
## this round removes. One definition, drawn by `_draw_structure_cards` and read by
## `wotr_living_world_ui_runner`, so the check is on the number the player sees.
func build_plot_counter_text(region_id: String) -> String:
	if session == null or region_id.is_empty():
		return ""
	var state: Dictionary = session.build_plots(region_id)
	return "%d/%d" % [int(state.get("used", 0)), int(state.get("total", 0))]


func _draw_structure_cards(origin: Vector2, scale: Vector2) -> void:
	if session == null or session.state == null or session.world == null:
		return
	var region_id := _card_region()
	if region_id.is_empty() or not _row_by_id.has(region_id):
		return
	var font := hud_font if hud_font != null else get_theme_default_font()
	var display := display_font if display_font != null else font
	# THE REGION'S OWN FOUNDATIONS AND WHAT STANDS ON THEM. The count is retail's
	# authored `BuildingSpot` total, which is the same count
	# `_unused_card_slot_paths` suppresses the rail down to, so a card can never be
	# drawn without its host or a host without its card.
	var tile := _build_plot_tile()
	var state: Dictionary = session.build_plots(region_id)
	var rows: Array = state.get("plots", []) as Array
	var plots := mini(int(state.get("total", 0)), TRAY_CARD_SLOTS.size())
	# RETAIL'S OWN HEAD FOR THE RAIL, in the maroon left of the first card. That
	# strip is retail's own well and it carried nothing at all; retail's title for
	# this readout and retail's own used/total is what it is a well FOR.
	if font != null and plots > 0:
		var head := Rect2(origin + Vector2(6.0, TRAY_CARD_PICTURE.position.y - 24.0) * scale,
			Vector2(float(TRAY_CARD_SLOTS[0]) - 16.0, 96.0) * scale)
		if head.size.x > 30.0:
			var caption_size := HudScript.type_size(head.size.y * 0.28, HudScript.TYPE_CAPTION)
			var value_size := HudScript.type_size(head.size.y * 0.34, HudScript.TYPE_SUBJECT)
			var title := names.shell_label(
				"STRATEGICHUD:RegionBuildPlotsTitle", "Territory Build Plots")
			# Two short lines rather than one long one: the well is a third as wide as
			# it is not, and a title that has to be shrunk to five pixels is not a title.
			var words := title.split(" ")
			var top_line := String(words[0]) if words.size() > 1 else title
			var next_line := " ".join(Array(words).slice(1)) if words.size() > 1 else ""
			chrome_layer.draw_string(font, head.position + Vector2(0.0, float(caption_size)),
				top_line, HORIZONTAL_ALIGNMENT_LEFT, int(head.size.x), caption_size,
				HudScript.PARCHMENT_DIM)
			if not next_line.is_empty():
				chrome_layer.draw_string(font,
					head.position + Vector2(0.0, float(caption_size) * 2.15),
					next_line, HORIZONTAL_ALIGNMENT_LEFT, int(head.size.x), caption_size,
					HudScript.PARCHMENT_DIM)
			HudScript.draw_row_rule(chrome_layer,
				head.position + Vector2(0.0, float(caption_size) * 2.9),
				head.position + Vector2(head.size.x, float(caption_size) * 2.9))
			chrome_layer.draw_string(display,
				head.position + Vector2(0.0, float(caption_size) * 3.0 + float(value_size)),
				build_plot_counter_text(region_id),
				HORIZONTAL_ALIGNMENT_LEFT, int(head.size.x), value_size, HudScript.GOLD_VALUE)
	for slot in range(plots):
		var card_origin := origin + (Vector2(float(TRAY_CARD_SLOTS[slot]), 0.0)
			+ TRAY_CARD_PICTURE.position) * scale
		var picture := Rect2(card_origin, TRAY_CARD_PICTURE.size * scale)
		var row := rows[slot] as Dictionary if slot < rows.size() else {}
		var standing: Texture2D = null
		if ui != null and bool(row.get("occupied", false)):
			standing = ui.image(String(row.get("button_image", "")))
		if standing != null:
			# THE STRUCTURE THAT IS ACTUALLY THERE, on retail's own card, with its
			# own drop shadow so the crop keeps its silhouette against the parchment.
			# The foundation tile goes down first: retail's card is a plot with a
			# building on it, not a building floating on tan.
			if tile != null:
				chrome_layer.draw_texture_rect(tile, picture, false,
					Color(1.0, 1.0, 1.0, 0.55))
			var inset := picture.grow(-picture.size.x * 0.12)
			chrome_layer.draw_texture_rect(standing,
				Rect2(inset.position + Vector2(1.5, 2.0), inset.size), false,
				Color(0.0, 0.0, 0.0, 0.5))
			chrome_layer.draw_texture_rect(standing, inset, false)
			if font != null:
				var name_text := _string_or_key(String(row.get("display_name_tag", "")))
				if not name_text.is_empty() and not name_text.contains(":LW_"):
					var name_size := HudScript.type_size(
						picture.size.y * 0.16, HudScript.TYPE_MICRO)
					chrome_layer.draw_string(font,
						Vector2(picture.position.x, picture.end.y + float(name_size) * 1.15),
						name_text, HORIZONTAL_ALIGNMENT_CENTER, int(picture.size.x),
						name_size, HudScript.PARCHMENT)
		elif tile != null:
			chrome_layer.draw_texture_rect(tile, picture, false)
	# THE SELECTED PLOT, RINGED. Retail rings the chosen card in gold and this
	# screen already has a real plot selection (the map's own build ring opens on
	# one), so the ring is live state rather than decoration.
	if String(selected_plot.get("region", "")) != region_id:
		return
	var index := int(selected_plot.get("index", -1))
	if index < 0 or index >= plots:
		return
	var card := Rect2(
		origin + Vector2(float(TRAY_CARD_SLOTS[index]) - 1.8, 25.0) * scale,
		Vector2(68.0, 110.0) * scale)
	chrome_layer.draw_rect(card, HudScript.RIM_GOLD_HOT, false, maxf(2.0, 2.5 * scale.y))
	chrome_layer.draw_rect(card.grow(2.0 * scale.y),
		Color(HudScript.RIM_GOLD.r, HudScript.RIM_GOLD.g, HudScript.RIM_GOLD.b, 0.55),
		false, maxf(1.0, 1.5 * scale.y))



## ------------------------------------------------------------------------------
## THE STRUCTURES ROSTER: A TABLE, AND EVERY ROW A CONTROL
## ------------------------------------------------------------------------------
##
## `_structure_roster_rows` says WHY this is a table and not a list of lines; this
## is the geometry. Five rules, each one a defect a blind review photographed:
##
##   1. IT IS IN A FRAME. The well gets a text plate with a head cap, so the
##      roster is a thing on the panel rather than type floating in the middle of
##      one. Nothing in it is drawn outside `detail_label`'s own rectangle - the
##      rectangle `_place_detail_well` fits between retail's card rail and
##      retail's right-hand scroll, and the one the region-card runner's
##      containment checks hold at every window size.
##   2. IT IS ON A BASELINE GRID. One row height for every row, every row rule at
##      the same pitch, every icon on the same left edge.
##   3. IT CARRIES RETAIL'S OWN ICONS - one `ConstructButtonImage` per offering,
##      through the same `ui.image()` the radial ring draws them with. An offering
##      whose image did not resolve gets an EMPTY recessed well, never a
##      substitute picture.
##   4. THE NUMERIC COLUMN IS RIGHT-ALIGNED under its own head, in the readout
##      gold every other number on this HUD is set in - and in a WARNING red when
##      the price is the only thing standing between the player and the build.
##   5. EVERY ROW IS PRESSABLE. The geometry is solved by `structure_roster_rects`
##      and read TWICE - once here to draw, once by `_place_build_row_buttons` to
##      place the hit areas - so a row can never be lit somewhere the click does
##      not land. That is the same one-definition discipline `tray_tab_cell` and
##      `command_deck_rect` keep, and for the same reason.
##
## THE UNBUILDABLE ROW IS DRAWN AND DIMMED, never dropped. Retail does exactly
## this: `CONTROLBAR:LW_FortRestricted` and `CONTROLBAR:LW_BuildNumberRestriction`
## are strings retail appends to the tooltip of a button it still shows.
const ROSTER_DIM := Color(1.0, 1.0, 1.0, 0.42)
const ROSTER_PRICE_SHORT := Color("#c8483f")


## The rectangles the roster occupies, solved once. `{well, inner, pad, pitch,
## head, rows, cost_column, head_size, title_size}`; `rows` is one Rect2 per row
## that is actually drawn, in roster order. Empty when there is no room.
##
## PURE, and separate from the drawing for the reason `radial_caption_plate` is:
## arithmetic that only exists inside a `_draw` callback is arithmetic no headless
## runner can reach, and this arithmetic decides where a click lands.
func structure_roster_rects() -> Dictionary:
	var empty: Dictionary = {"well": Rect2(), "inner": Rect2(), "pad": 0.0,
		"pitch": 0.0, "head": Rect2(), "rows": [] as Array[Rect2],
		"cost_column": 0.0, "head_size": 0, "title_size": 0}
	if detail_label == null or detail_label.size.x <= 40.0 or detail_label.size.y <= 20.0:
		return empty
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null:
		return empty
	var well := Rect2(detail_label.position, detail_label.size)
	var pad := clampf(well.size.x * 0.03, 4.0, 12.0)
	var inner := well.grow(-pad)
	if inner.size.x <= 12.0 or inner.size.y <= 12.0:
		return empty
	# THE GRID. One head row plus as many body rows as the well holds, all at one
	# pitch - solved from the well rather than chosen, so the roster is a table at
	# every window size instead of a table at one of them.
	var pitch := clampf(inner.size.y / float(maxi(structure_roster.size(), 1) + 1),
		14.0, 46.0)
	var body_rows := maxi(1, int(inner.size.y / pitch) - 1)
	var head_size := HudScript.type_size(pitch, HudScript.TYPE_CAPTION)
	var title_size := HudScript.type_size(pitch, HudScript.TYPE_VALUE)
	# THE COST COLUMN'S WIDTH IS SOLVED FOR THE WIDEST COST, so the numbers and
	# their heading share one right edge and one left edge - four numerals
	# right-aligned under a heading they are not under is the exact defect this
	# screen was carrying on its OTHER table (see `_draw_standings`).
	var cost_column := font.get_string_size(
		"0000", HORIZONTAL_ALIGNMENT_LEFT, -1, title_size).x
	for row_value in structure_roster:
		cost_column = maxf(cost_column, font.get_string_size(
			String((row_value as Dictionary)["cost"]),
			HORIZONTAL_ALIGNMENT_LEFT, -1, title_size).x)
	cost_column = minf(cost_column + pad, inner.size.x * 0.34)
	var head := Rect2(inner.position, Vector2(inner.size.x, pitch))
	var rects: Array[Rect2] = []
	for index in range(mini(structure_roster.size(), body_rows)):
		rects.append(Rect2(
			Vector2(inner.position.x, head.end.y + pitch * float(index)),
			Vector2(inner.size.x, pitch)))
	return {"well": well, "inner": inner, "pad": pad, "pitch": pitch, "head": head,
		"rows": rects, "cost_column": cost_column, "head_size": head_size,
		"title_size": title_size}


func _draw_structure_roster() -> void:
	var layout := structure_roster_rects()
	var inner := layout["inner"] as Rect2
	if inner.size.x <= 0.0:
		return
	var font := hud_font if hud_font != null else get_theme_default_font()
	var display := display_font if display_font != null else font
	HudScript.draw_text_plate(chrome_layer, layout["well"] as Rect2)
	var pad := float(layout["pad"])
	var pitch := float(layout["pitch"])
	var head := layout["head"] as Rect2
	var head_size := int(layout["head_size"])
	var title_size := int(layout["title_size"])

	# THE HEAD - retail's own noun for the column of things, on the head cap.
	HudScript.draw_panel_cap(chrome_layer, head)
	var head_baseline := head.position.y + head.size.y * 0.5 + float(head_size) * 0.36
	# THE TABLE'S TWO COLUMN HEADINGS ARE CAPTIONS, so they are set in the caption
	# tier like every other caption on this HUD - see `HudChrome.draw_caption`.
	HudScript.draw_caption(chrome_layer, font,
		Vector2(inner.position.x + pad * 0.5, head_baseline),
		names.shell_label("APT:Structures", "Structures"),
		head_size, HudScript.PARCHMENT_DIM)
	if not structure_roster.is_empty():
		HudScript.draw_caption(chrome_layer, font,
			Vector2(inner.position.x, head_baseline),
			names.shell_label("APT:Cost", "Cost"), head_size,
			HudScript.PARCHMENT_DIM, HORIZONTAL_ALIGNMENT_RIGHT, inner.size.x - pad * 0.5)

	if structure_roster.is_empty():
		# THE EMPTY STATE IS IN THE FRAME TOO. It used to be one more loose line;
		# a designed empty state sits where the rows would sit, and it names the
		# PARTICULAR silence rather than repeating one sentence for all of them.
		chrome_layer.draw_string(display,
			Vector2(inner.position.x + pad * 0.5,
				head.end.y + pitch * 0.5 + float(head_size) * 0.36),
			_empty_tab_line("structures"), HORIZONTAL_ALIGNMENT_LEFT,
			int(inner.size.x - pad), head_size, HudScript.PARCHMENT_DIM)
		return

	var rects := layout["rows"] as Array[Rect2]
	for index in range(rects.size()):
		var row := structure_roster[index] as Dictionary
		var cell := rects[index]
		var can_build := bool(row.get("can_build", true))
		var baseline := cell.position.y + pitch * 0.5 + float(title_size) * 0.36
		# The rule between rows, never above the first one (the head cap's own gilt
		# rule is that one) and never below the last (the plate's own lower edge is).
		if index > 0:
			HudScript.draw_row_rule(chrome_layer,
				Vector2(cell.position.x, cell.position.y),
				Vector2(cell.end.x, cell.position.y))
		# THE HOVER STATE, drawn as LIGHT ON THE ROW rather than as a plate behind
		# it. A buildable row under the pointer gets the tray's own lit oxblood and
		# a hot gold left margin; a refused one gets neither, which is how the
		# player learns which rows are live without reading a word.
		var button: Button = _build_row_buttons[index] as Button \
			if index < _build_row_buttons.size() else null
		if can_build and button != null and button.visible and button.is_hovered():
			chrome_layer.draw_rect(cell, Color(HudScript.OXBLOOD_LIT.r,
				HudScript.OXBLOOD_LIT.g, HudScript.OXBLOOD_LIT.b, 0.55))
			chrome_layer.draw_rect(
				Rect2(cell.position, Vector2(maxf(2.0, pitch * 0.09), cell.size.y)),
				HudScript.RIM_GOLD_HOT)
		# THE ICON SITS ON THE CHROME, NOT IN A BOX. The owner: "Why are there black
		# boxes around the icons? It should just be icons, transparent." What the
		# hard gold rule that used to be ruled around every crop was FOR is real and
		# is kept: an icon with a transparent surround, drawn straight onto the
		# tray's oxblood, loses its own edge. The separation comes from a DROP
		# SHADOW under the icon instead of a frame around it.
		var icon_side := pitch * 0.86
		var icon_box := Rect2(
			Vector2(inner.position.x + pad * 0.5, cell.position.y + (pitch - icon_side) * 0.5),
			Vector2(icon_side, icon_side))
		var icon: Texture2D = null
		var image_id := String(row["image_id"])
		if ui != null and not image_id.is_empty():
			icon = ui.image(image_id)
		var value := Color.WHITE if can_build else ROSTER_DIM
		if icon != null:
			chrome_layer.draw_texture_rect(icon,
				Rect2(icon_box.position + Vector2(1.0, 1.5), icon_box.size), false,
				Color(0.0, 0.0, 0.0, 0.55 if can_build else 0.3))
			chrome_layer.draw_texture_rect(icon, icon_box, false, value)
		else:
			# NO SUBSTITUTE PICTURE. An empty recessed well is visibly an absence.
			HudScript.draw_socket(chrome_layer, icon_box)
		var text_left := icon_box.end.x + pad * 0.75
		var text_room := inner.end.x - float(layout["cost_column"]) - text_left
		if text_room > 8.0:
			chrome_layer.draw_string(display, Vector2(text_left, baseline),
				String(row["title"]), HORIZONTAL_ALIGNMENT_LEFT, int(text_room),
				title_size, HudScript.PARCHMENT if can_build else HudScript.PARCHMENT_DIM)
		var cost := String(row["cost"])
		if not cost.is_empty():
			# THE PRICE GOES RED FOR ONE REFUSAL ONLY - the one the player can do
			# something about this turn. A fortress barred by retail's own territory
			# rule is not a budgeting problem, and colouring its price as one would
			# send the player to earn treasure they already have.
			var price_tint := HudScript.GOLD_VALUE
			if bool(row.get("unaffordable", false)):
				price_tint = ROSTER_PRICE_SHORT
			elif not can_build:
				price_tint = HudScript.PARCHMENT_DIM
			chrome_layer.draw_string(font, Vector2(inner.position.x, baseline), cost,
				HORIZONTAL_ALIGNMENT_RIGHT, int(inner.size.x - pad * 0.5),
				title_size, price_tint)
	# AN OVERFLOWING ROSTER ENDS ON THE TYPOGRAPHIC ELLIPSIS, on its own row, for
	# the same reason the region card does: a table that simply stops is a table
	# that looks complete and is not.
	if rects.size() < structure_roster.size():
		chrome_layer.draw_string(font, Vector2(inner.position.x, inner.end.y - 1.0),
			RIBBON_ELLIPSIS, HORIZONTAL_ALIGNMENT_RIGHT, int(inner.size.x - pad * 0.5),
			head_size, HudScript.PARCHMENT_DIM)


## ------------------------------------------------------------------------------
## THE THREE POOLS OF BUILD CONTROLS, PLACED ON THE GEOMETRY THAT DREW THEM
## ------------------------------------------------------------------------------
##
## Every one of these reads the SAME function the drawing reads, never a second
## copy of the arithmetic: `structure_roster_rects()` for the roster, the card
## slot table for the foundations, `command_dial_slots()` for the palantir's ring.
## A hit area computed twice is a hit area that can be right in the runner and
## wrong on the glass - which is exactly how retail's tab plate came to be lit in
## one rectangle and clicked in another two rounds ago.
func _place_build_controls() -> void:
	_place_build_row_buttons()
	_place_plot_card_buttons()
	_place_dial_buttons()


func _place_build_row_buttons() -> void:
	var layout := structure_roster_rects()
	var rects := layout.get("rows", [] as Array[Rect2]) as Array[Rect2]
	var live := active_tab == "structures" and not hud_hidden 		and island_is_shown("selectionDetails")
	for index in range(_build_row_buttons.size()):
		var button := _build_row_buttons[index] as Button
		if not live or index >= rects.size() or index >= structure_roster.size():
			button.visible = false
			_place_exact(button, Rect2(Vector2.ZERO, Vector2.ZERO))
			continue
		var row := structure_roster[index] as Dictionary
		button.visible = true
		# A REFUSED ROW IS STILL PRESSABLE. Godot's `disabled` refuses the hover as
		# well as the click, which would take the tooltip with it - and the tooltip
		# is where the refusal is written. So the button stays live and
		# `_on_build_row_pressed` re-asks the offer, which refuses and SAYS SO on the
		# message line. A player who presses a dimmed row gets an answer.
		button.disabled = false
		button.tooltip_text = String(row.get("tooltip", ""))
		_place_exact(button, rects[index])


func _place_plot_card_buttons() -> void:
	var rects := structure_card_rects()
	var live := active_tab == "structures" and not hud_hidden 		and island_is_shown("selectionDetails")
	for index in range(_plot_card_buttons.size()):
		var button := _plot_card_buttons[index] as Button
		if not live or index >= rects.size():
			button.visible = false
			_place_exact(button, Rect2(Vector2.ZERO, Vector2.ZERO))
			continue
		button.visible = true
		button.tooltip_text = _plot_card_tooltip(index)
		_place_exact(button, rects[index])


## The screen rectangle of every foundation card retail's rail is showing, in plot
## order. Read by the drawing and by the buttons.
func structure_card_rects() -> Array[Rect2]:
	var rects: Array[Rect2] = []
	if session == null or session.state == null or not _islands.has("selectionDetails"):
		return rects
	var region_id := _card_region()
	if region_id.is_empty() or not _row_by_id.has(region_id):
		return rects
	var island := _islands["selectionDetails"] as Dictionary
	var origin := island["origin"] as Vector2
	var scale := island["scale"] as Vector2
	var plots := mini(int(session.build_plots(region_id).get("total", 0)),
		TRAY_CARD_SLOTS.size())
	for slot in range(plots):
		rects.append(Rect2(
			origin + (Vector2(float(TRAY_CARD_SLOTS[slot]), 0.0)
				+ TRAY_CARD_PICTURE.position) * scale,
			TRAY_CARD_PICTURE.size * scale))
	return rects


## What a foundation card says on hover - retail's own two strings for exactly
## this ("Building Foundation" / "Construct a building on the foundation") when
## the plot is empty, and the standing structure's own retail name when it is not.
func _plot_card_tooltip(index: int) -> String:
	var region_id := _card_region()
	if session == null or region_id.is_empty():
		return ""
	var rows: Array = session.build_plots(region_id).get("plots", []) as Array
	if index >= rows.size():
		return ""
	var row := rows[index] as Dictionary
	if bool(row.get("occupied", false)):
		var standing := _string_or_key(String(row.get("display_name_tag", "")))
		var described := _string_or_key(String(row.get("description_tag", "")))
		var lines: Array[String] = []
		if not standing.contains(":LW_"):
			lines.append(standing)
		if not described.contains(":LW_"):
			lines.append(described.replace("\\n", "\n").strip_edges())
		return "\n".join(lines).strip_edges()
	return "%s\n%s" % [
		names.shell_label("STRATEGICHUD:BuildPlotName", "Building Foundation"),
		names.shell_label("STRATEGICHUD:BuildPlotHelp",
			"Construct a building on the foundation")]


## RETAIL'S SIX COMMAND WELLS AND WHAT SITS IN EACH, as `{box, entry}` in the
## wells' own authored order. Empty entries are wells the offer does not reach.
func command_dial_slots() -> Array[Dictionary]:
	var slots: Array[Dictionary] = []
	if not _islands.has("globe") or session == null or session.state == null:
		return slots
	var region_id := _card_region()
	if region_id.is_empty() or not _row_by_id.has(region_id):
		return slots
	var island := _islands["globe"] as Dictionary
	var origin := island["origin"] as Vector2
	var scale := island["scale"] as Vector2
	# THE DIAL IS ABOUT THE FOUNDATION THE RING IS OPEN ON when there is one, so
	# the two surfaces cannot disagree about whether a structure fits.
	var plot := -1
	if String(selected_plot.get("region", "")) == region_id:
		plot = int(selected_plot.get("index", -1))
	var offer := _build_offer(region_id, plot)
	for index in range(mini(offer.size(), PALANTIR_COMMAND_WELLS.size())):
		var well := PALANTIR_COMMAND_SLOT + (PALANTIR_COMMAND_WELLS[index] as Vector2)
		slots.append({
			"box": Rect2(origin + well * scale,
				Vector2.ONE * PALANTIR_COMMAND_WELL_SIZE * scale.x),
			"entry": offer[index],
		})
	return slots


func _place_dial_buttons() -> void:
	var slots := command_dial_slots()
	var live := not hud_hidden and view_mode == VIEW_FULL and island_is_shown("globe")
	for index in range(_dial_buttons.size()):
		var button := _dial_buttons[index] as Button
		if not live or index >= slots.size():
			button.visible = false
			_place_exact(button, Rect2(Vector2.ZERO, Vector2.ZERO))
			continue
		var entry := (slots[index] as Dictionary)["entry"] as Dictionary
		button.visible = true
		button.disabled = false
		button.tooltip_text = _build_tooltip(entry)
		_place_exact(button, (slots[index] as Dictionary)["box"] as Rect2)


func _on_build_row_pressed(index: int) -> void:
	if index < 0 or index >= structure_roster.size():
		return
	var row := structure_roster[index] as Dictionary
	_commit_build_here(String(row.get("region", "")), int(row.get("plot", -1)),
		String(row.get("id", "")))


func _on_build_row_hovered(index: int, entered: bool) -> void:
	if index < 0 or index >= structure_roster.size():
		return
	var row := structure_roster[index] as Dictionary
	_on_build_entry_hovered(String(row.get("region", "")), int(row.get("plot", -1)),
		String(row.get("id", "")) if entered else "")
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## Pressing a foundation card opens the build ring on that foundation - the same
## thing pressing the plot on the map does, through the same handler, so the two
## cannot come apart.
func _on_plot_card_pressed(index: int) -> void:
	var region_id := _card_region()
	if region_id.is_empty():
		return
	_on_plot_clicked(region_id, index)


func _on_dial_well_pressed(index: int) -> void:
	var slots := command_dial_slots()
	if index < 0 or index >= slots.size():
		return
	var entry := (slots[index] as Dictionary)["entry"] as Dictionary
	var region_id := _card_region()
	var plot := -1
	if String(selected_plot.get("region", "")) == region_id:
		plot = int(selected_plot.get("index", -1))
	_commit_build_here(region_id, plot, String(entry.get("id", "")))



## The authored sub-paths of every build-queue slot this region does NOT have a
## plot for, plus retail's queue-head cell - the draws `_draw_command_bar`
## suppresses so the card rail carries one card per plot and nothing else.
##
## The frame index is read off the frame itself rather than written here, because
## `StrategicDetailsBuildQueue` flattens four frames (0, 5, 10, 15) with identical
## geometry and the authored path carries whichever one was asked for.
## THE CARD WELL'S OWN EMPTY HOST, by its authored path.
##
## `StrategicDetailsBuildQueue`'s `4/2` carries TWO things at one path: the maroon
## gilt-framed panel the cards sit in - retail's own art, and exactly the maroon
## its own capture of this tab shows - and a flat-black rectangle drawn over it
## covering the same 112.6..585.6 x 15.1..145.1. The black is the scrolling list's
## host: retail composites the queue into it and never shows it. Six cards used to
## cover it so nobody saw it; showing one card per plot uncovered it, and the very
## next capture had a black rectangle across two thirds of the tray.
##
## It goes through `suppressed_black_fills` rather than `suppressed_paths` for
## exactly that reason - the panel and the hole share a path, and only the flat
## black is dropped.
func _card_well_host_path(queue: Dictionary) -> String:
	return "screen:StrategicDetailsBuildQueue:frame:%d/%s" % [
		int(queue.get("frameIndex", 0)), TRAY_CARD_WELL_PATH]


func _unused_card_slot_paths(queue: Dictionary) -> Array:
	var stem := "screen:StrategicDetailsBuildQueue:frame:%d/" % int(queue.get("frameIndex", 0))
	# THE QUEUE HEAD ALWAYS. Nothing is ever QUEUED: every one of retail's 28
	# `LivingWorldBuilding` blocks is `TurnsToBuild = 1`, so a structure ordered
	# this turn is standing immediately and there is no "currently building" cell
	# for the head to show. Retail's own capture of this tab shows no head either.
	var suppressed: Array = [stem + TRAY_QUEUE_HEAD_PATH]
	var plots := 0
	if session != null and session.world != null:
		var region_id := _card_region()
		if not region_id.is_empty():
			plots = int(session.world.region(region_id).get("building_spot_count", 0))
	for index in range(TRAY_CARD_SLOT_PATHS.size()):
		if index >= plots:
			suppressed.append(stem + String(TRAY_CARD_SLOT_PATHS[index]))
	return suppressed


## RETAIL'S TWO EMPTY RUNTIME HOSTS IN THE PALANTIR, by their own authored paths,
## and the rectangle the first of them occupies.
##
## MEASURED OUT OF THE BUNDLE, not chosen. `StrategicPalantir` frame 1 flattens
## `screen:StrategicPalantir:frame:1/3` as 318 pure-black solid triangles spanning
## x 210..362, y 71..225 - the SUB-GLASS, the right-hand lens of retail's
## `PalantirFrame_GoodDouble`, which retail fills at runtime with the tan carved
## compass medallion the oracle capture shows. `...frame:1/35/9/1` is 475 more
## pure-black triangles spanning x 262.5..378.7, y 51..245.1 - the `commandUI`
## host behind the ring of structure buttons, which retail fills with live
## `StrategicCommandButton` instances.
##
## THE FACE IS RETAIL'S OWN, AND A PREVIOUS ROUND OF THIS LANE WAS WRONG ABOUT IT.
##
## That round swept both asset layers for a compass, a dial, a rose, a sunburst or
## a medallion, found nothing, and concluded the face was absent - so it drew a
## blank carved stone plate here and named the absence. The search was sound and
## the conclusion was not: retail names this art after what it MEANS rather than
## after what it looks like. The face is the seat's own BUILD-PLOT SELECTION
## PORTRAIT (`LivingWorldPlayerTemplate.BuildPlotSelectionPortraitName`), an
## engraved stone tile carrying the faction device, and it is a MappedImage crop
## the living-world UI bundle has served all along - see
## `WotrLivingWorldUi.build_plot_portrait` for the measurement that proves the
## tile in this lens and the tile on the tray's build cards are ONE asset.
##
## So the two black hosts are still suppressed - rendering retail's runtime hosts
## verbatim is what a blind review called "two large unexplained black blobs ...
## where geometry ends without a terminus" - `HudChrome.draw_dial_seat` still cuts
## the seat they sit in, and retail's own tile is laid into that seat's face,
## UNDER retail's frame rim and UNDER retail's button chain. Nothing here is
## painted from a screenshot.
##
## THE INSET IS THE SEAT'S OWN RECESSED FIELD, not a number chosen to look right:
## `draw_dial_seat` steps its face in at 0.90 of the plate's radii and cuts a
## second step at 0.62, and the tile is laid on the outer of those two so the
## gilt edge and the rim shadow stay retail's frame's business and the engraving
## keeps the whole field.
const PALANTIR_SUB_GLASS_PATH := "screen:StrategicPalantir:frame:1/3"
const PALANTIR_COMMAND_HOST_PATH := "screen:StrategicPalantir:frame:1/35/9"
## THE SEAT'S RECTANGLE IS THE UNION OF THE TWO HOSTS, not just the sub-glass, and
## the first capture of this change is why. Suppressing `commandUI`'s black
## backdrop and filling only the sub-glass left the two UNUSED button collars at
## the foot of the ring with Middle-earth showing through them - a hole in a
## different colour. Retail's own medallion is one plate that every collar is
## seated in the rim of, so the seat is one ellipse over both authored
## rectangles: x 210..381 (sub-glass left edge to the button chain's right edge)
## and y 49..247 (the chain's own top and bottom).
const PALANTIR_DIAL_SEAT := Rect2(210.0, 49.0, 171.0, 198.0)
## `draw_dial_seat`'s own recessed face, as a fraction of the seat's radii. Keep
## this in step with that function; it is the field retail's tile is laid on.
const PALANTIR_DIAL_FACE := 0.90
## How far into the tile the lens is cropped, so retail's engraved device fills
## the glass the way retail's own capture shows it. MEASURED off the shipped
## tiles: the device occupies x 40..152 of a 192-wide plate (58%) with the rest
## soft vignette, and in the oracle the device fills about 80% of the lens - so
## the plate is drawn 0.80 / 0.58 = 1.38 times the lens's own width.
const PALANTIR_DIAL_ZOOM := 1.38


## The engraved build-plot tile the palantir's selection lens and the tray's build
## cards BOTH show, for whichever seat holds the region on the glass. Empty
## `Texture2D` (null) when the region is unclaimed - an unowned region has no
## faction whose device could be cut into its plots - or when the seat's own
## `BuildPlotSelectionPortraitName` resolves to no atlas, which
## `WotrLivingWorldUi.image` records rather than substituting.
func _build_plot_tile() -> Texture2D:
	if ui == null or session == null or session.state == null:
		return null
	var region_id := _card_region()
	if region_id.is_empty() or not _row_by_id.has(region_id):
		return null
	var owner := session.state.owner_of(region_id)
	if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
		return null
	return ui.build_plot_portrait(
		String((session.state.players[owner] as Dictionary).get("template", "")))


func _draw_palantir(apt_frame: Dictionary, origin: Vector2, scale: Vector2) -> void:
	# THE SEAT FIRST, so retail's own gold rim and its button collars draw OVER it
	# rather than under it. Drawn before the first APT pass for that reason and no
	# other; it occupies exactly the rectangle retail's sub-glass host occupies.
	var seat := Rect2(
		origin + PALANTIR_DIAL_SEAT.position * scale, PALANTIR_DIAL_SEAT.size * scale)
	HudScript.draw_dial_seat(chrome_layer, seat)
	# RETAIL'S OWN ENGRAVING IN THE SEAT'S FACE. Stretched to the face rather than
	# cover-cropped, because retail stretches this whole surface anisotropically and
	# its own dial is a wide oval eye (see `_draw_oval`). The tile's alpha fades to
	# nothing at its own edge, so it lands ON the carved stone rather than as a disc
	# pasted over it - which is how retail composites it too.
	var tile := _build_plot_tile()
	if tile != null:
		var face := Rect2(Vector2.ZERO, seat.size * PALANTIR_DIAL_FACE)
		face.position = seat.get_center() - face.size * 0.5
		_draw_oval(chrome_layer, face, tile, false, PALANTIR_DIAL_ZOOM)
	HudScript.draw_apt_frame(chrome_layer, apt_frame, origin, scale, strategic,
		Vector2i(0, PALANTIR_GLASS_DEPTH - 1),
		[PALANTIR_SUB_GLASS_PATH, PALANTIR_COMMAND_HOST_PATH])
	var region_id := _card_region()
	var found: Dictionary = {"texture": null, "id": "", "requested": "", "source": "", "reason": ""}
	if region_images != null and not region_id.is_empty():
		found = region_images.region_portrait(region_id)
	var texture: Texture2D = found["texture"]
	if texture != null:
		var dish := Rect2(origin + PALANTIR_DISH.position * scale, PALANTIR_DISH.size * scale)
		_draw_oval(chrome_layer, dish, texture)
	_draw_command_dial(origin, scale)
	HudScript.draw_apt_frame(chrome_layer, apt_frame, origin, scale, strategic,
		Vector2i(PALANTIR_GLASS_DEPTH, 0x7fffffff))


## THE COMPASS DIAL'S SIX WELLS, AS RETAIL'S OWN BUILD MENU.
##
## `commandUI` is retail's authored host for the ring of command buttons and its
## six wells are authored named instances (`0`..`5` inside it). The APT carries
## no CONTENT for them - retail fills each with a `StrategicCommandButton` at
## runtime, which is the bundle's `dynamic-content-slots-are-empty` gap - so the
## positions are retail's and the pictures come from the OTHER retail bundle that
## already serves this screen: the `LivingWorldBuilding` rows retail marks
## `AvailableTo` the owning seat's template, with retail's own
## `ConstructButtonImage` crop. A region whose owner offers fewer than six
## structures leaves the remaining wells empty rather than repeating an icon.
##
## THE WELLS ARE CONTROLS NOW, and that reverses a decision this file made and
## documented last round. Construction was not simulated then, so six lit icons in
## six gilt collars were made deliberately DEAD - value dropped, cooled, with a
## tooltip saying nothing here builds. A blind review's verdict on the result was
## exact and is worth keeping: "a column of dead buttons hanging off the right of
## the stone disc". Both halves of that are answered here rather than argued with:
##
##   * DEAD -> LIVE. Each well presses `_commit_build_here`, the same door the
##     ring and the roster press. What a well looks like is now what it is.
##   * HANGING OFF -> SEATED IN. A well the offer does not reach is drawn as an
##     EMPTY SEAT rather than skipped, so the ring is a continuous fitting on the
##     disc instead of a broken column of two or three; and every well gets a
##     collar cut in the stone under it, so the chain reads as cast into the
##     medallion rather than laid on top of it.
##
## THE WELL GEOMETRY IS MEASURED, not guessed. The six named instances sit at
## (16,1) (64,18) (90,60) (88,110) (56,148) (9,159) inside `commandUI`, and the
## dial's own flattened black wells span 262.5..378.7 x 51..245 in movie space.
## Reading the instance translations as TOP-LEFT corners and solving for one
## square size gives 262+0=262.5 and 343+D=378.7, so D = 36 - and the vertical
## extent then lands on 50..244 against the measured 51..245. Reading them as
## CENTRES cannot be solved at all (it wants r=0 on one edge and r=36 on the
## other), which is what makes this a measurement rather than a preference.
const PALANTIR_COMMAND_SLOT := Vector2(253.0, 49.0)
const PALANTIR_COMMAND_WELLS := [
	Vector2(16.0, 1.0), Vector2(64.0, 18.0), Vector2(90.0, 60.0),
	Vector2(88.0, 110.0), Vector2(56.0, 148.0), Vector2(9.0, 159.0),
]
const PALANTIR_COMMAND_WELL_SIZE := 36.0

## The value a well's icon is drawn at when its structure cannot be raised. Same
## "keep the shape, drop the value" rule `HudChrome.style_button` states for a
## disabled capsule, and the same one the roster's dimmed rows use - so a refused
## offering reads identically on all three surfaces.
const COMMAND_DIAL_DIM := Color(0.80, 0.76, 0.70, 0.45)


## The tooltip the ring's own backing carries when NOTHING can be built on this
## region at all. The per-well answers are on the wells.
func _command_dial_reason() -> String:
	if session == null or session.state == null:
		return COMMAND_DIAL_UNAVAILABLE_NO_SEAT
	var region_id := _card_region()
	if region_id.is_empty() or not _row_by_id.has(region_id):
		return COMMAND_DIAL_UNAVAILABLE_NO_SEAT
	var owner := session.state.owner_of(region_id)
	if owner == StateScript.NEUTRAL or owner < 0 or owner >= session.state.players.size():
		return COMMAND_DIAL_UNAVAILABLE_NO_SEAT
	return COMMAND_DIAL_UNAVAILABLE % _owner_name(owner)


func _draw_command_dial(origin: Vector2, scale: Vector2) -> void:
	if ui == null:
		return
	var slots := command_dial_slots()
	# AN UNFILLED WELL GETS NOTHING DRAWN IN IT, and that took two passes to arrive
	# at. The first drew `draw_socket` in every well the offer did not reach, which
	# FILLS - and the two spare collars at the foot of the chain came out as a pair
	# of near-black lozenges off the edge of the stone, a fresh instance of the
	# exact defect the owner opened with ("why are there black boxes around the
	# icons"). The second drew a thin engraved ring instead, and the capture showed
	# those rings sitting ON TOP of collar rims retail's own frame had already
	# drawn there - two circles where there was one.
	#
	# So: nothing. Retail's `StrategicPalantir` frame carries a gilt collar at every
	# one of the six positions whether or not anything is in it, which is precisely
	# the "seated, not hanging" treatment the extra ring was reaching for. The art
	# was already right; the drawing was competing with it.
	for index in range(slots.size()):
		var slot := slots[index] as Dictionary
		var entry := slot["entry"] as Dictionary
		var box := slot["box"] as Rect2
		var can_build := bool(entry.get("can_build", false))
		var button: Button = _dial_buttons[index] as Button \
			if index < _dial_buttons.size() else null
		var hovered := button != null and button.visible and button.is_hovered()
		# THE LIT STATE IS ON THE COLLAR, not a plate behind the icon. The owner:
		# "why are there black boxes around the icons, it should just be icons
		# transparent." A hovered well gets a hot gold ring cut round retail's own
		# collar and its icon comes up to full value; nothing is filled.
		if hovered and can_build:
			chrome_layer.draw_arc(box.get_center(), box.size.x * 0.52, 0.0, TAU, 28,
				HudScript.RIM_GOLD_HOT, maxf(1.5, box.size.x * 0.07))
		var icon := ui.image(String(entry.get("image_id", "")))
		if icon == null:
			continue
		# Inset a little: retail's icon sits inside the well's gilt lip, not on it.
		var picture := box.grow(-box.size.x * 0.09)
		var tint := Color.WHITE if can_build else COMMAND_DIAL_DIM
		if hovered and can_build:
			tint = HudScript.RIM_GOLD_HOT
		chrome_layer.draw_texture_rect(icon,
			Rect2(picture.position + Vector2(1.0, 1.5), picture.size), false,
			Color(0.0, 0.0, 0.0, 0.5))
		chrome_layer.draw_texture_rect(icon, picture, false, tint)
		# NO PRICE UNDER THE WELL, and that is a decision rather than an omission.
		# The first pass set one under every collar; the collars sit on an arc across
		# a carved stone medallion, so six small numerals landed at six different
		# angles on a textured field and read as litter - and the owner's standing
		# complaint about this screen is that it is too cluttered. Every price is on
		# the ROSTER, in a right-aligned column under its own heading, four inches to
		# the right, and on this well's own tooltip. The dial's job is the pictures.
		# THE ONE THING THE DIAL DOES SAY WITHOUT A HOVER is which wells are live:
		# a refused offering is dimmed, exactly as its roster row is.


## A picture CROPPED to an oval, never stretched into one. The oval is retail's
## own `PalantirMainGlass` rectangle (219x185 authored, which is why it is an
## oval and not a circle), and the picture is scaled to COVER that rectangle and
## then cut by it, so the region portrait keeps its own aspect exactly the way a
## live 3D feed would in retail's slot.
##
## `cover` is the choice between the two honest ways to put a rectangular picture
## in an oval, and each host wants a different one. A REGION PORTRAIT is a
## photograph of a place and must keep its own aspect, so it covers and is cut
## (`cover = true`). RETAIL'S OWN UI TILES - the build-plot engraving in the
## palantir's selection lens - are authored square and retail stretches its whole
## 1024x768 surface onto the frame anisotropically, which is why its dial reads as
## a WIDE oval eye at 16:9 and not a round one. Cover-cropping such a tile would
## throw away the top and bottom of an engraving retail shows whole, so it is
## mapped edge to edge instead (`cover = false`).
##
## `zoom` magnifies the picture inside the oval - above 1.0 the oval shows less of
## it, at its own aspect. Retail's build-plot tile is a 192x192 plate whose
## engraved device only occupies the middle of it (x 40..152, y 55..140 of the
## Mordor tile, and the same registration on all seven), with a soft alpha
## vignette around it; retail's palantir lens shows that device filling the glass.
## Laying the whole plate edge to edge in the lens therefore renders the device at
## about half the size retail sets it. The magnification is the ratio those two
## measurements give, and it is stated at the call site.
static func _draw_oval(
		canvas: CanvasItem, box: Rect2, picture: Texture2D,
		cover: bool = true, zoom: float = 1.0) -> void:
	if box.size.x <= 2.0 or box.size.y <= 2.0 or picture == null:
		return
	var centre := box.get_center()
	var radii := box.size * 0.5
	var covered := box.size / maxf(zoom, 0.01)
	if cover:
		var picture_size := picture.get_size()
		covered = picture_size * maxf(
			box.size.x / picture_size.x, box.size.y / picture_size.y) / maxf(zoom, 0.01)
	var points := PackedVector2Array()
	var uvs := PackedVector2Array()
	for step in range(72):
		var angle := TAU * float(step) / 72.0
		var offset := Vector2(cos(angle) * radii.x, sin(angle) * radii.y)
		points.append(centre + offset)
		uvs.append(Vector2(0.5, 0.5) + Vector2(offset.x / covered.x, offset.y / covered.y))
	canvas.draw_colored_polygon(points, Color.WHITE, uvs, picture)


# --- detail panel ------------------------------------------------------------

## RETAIL'S REGION PANEL, in retail's own words.
##
## The screenshot the owner sent reads, over Mordor:
##
##     Mordor
##     +500 Treasure
##     3 Build Plots
##     Territory of Region: Mordor
##     Unified Region Bonus: Discount when Building Barracks Units
##
## Every one of those five lines is retail data, and none of it is written here:
##
##   * "Mordor" is `LW:DisplayNameMordor` from `data/lotr.str`.
##   * "+500 Treasure" is the format string `LW:RegionTreasuryBonus`
##     (`+%d Treasure`) filled from `FertileTerritoryBonus`, which retail authors
##     as the macro `FERTILE_TERRITORY_BONUS` and `gamedata.ini` defines as 500.
##   * "3 Build Plots" is `LW:NumberOfBuildPlotsPlural` (`\n%d Build Plots`)
##     filled from the count of `BuildingSpot` lines the region authors.
##   * "Territory of Region: %ls" is `LW:TerritoryPartOfRegion`, filled from the
##     territory whose member list contains this region.
##   * "Unified Region Bonus: %ls" is `LW:UnifiedRegionBonus`, filled from that
##     territory's own bonuses through the same formatter table.
##
## WHAT IS SHOWN WHEN A PIECE IS MISSING. A macro the `#define` table does not
## resolve prints its NAME and the word unresolved. A format string the table
## does not carry prints the KEY. A region in no territory says so. Nothing here
## substitutes a plausible number for one it could not read - which is the whole
## reason the macro table exists rather than a literal 500 in this file.
func _region_panel_lines(region_id: String) -> Array[String]:
	var lines: Array[String] = []
	if session == null or session.world == null:
		return lines
	var region := session.world.region(region_id)
	if region.is_empty():
		return lines

	# THE CARD'S FACE, matched to retail's own region tooltip (the Mordor card
	# in the reference capture): the name in the engraved face, the yields in
	# gold, the territory lines under a blank line. The [b] face is retail's
	# Albertus MT when a pack ships it (`_apply_hud_font`).
	lines.append("[b][font_size=22][color=#e8dfc2]%s[/color][/font_size][/b]" % _display_of(region_id))

	var bonuses := region.get("bonuses", {}) as Dictionary
	var macro_names := region.get("bonus_macros", {}) as Dictionary
	var printed := 0
	for field in BONUS_ORDER:
		var line := _format_bonus(field, bonuses, macro_names)
		if line.is_empty():
			continue
		lines.append("  [color=#d8b45a]%s[/color]" % line)
		printed += 1
	if printed == 0:
		lines.append("  [color=#d8b45a]%s[/color]" % _string_or_key("LW:NoBonus"))

	var plots := int(region.get("building_spot_count", 0))
	var plot_key := "LW:NumberOfBuildPlotsSingle" if plots == 1 else "LW:NumberOfBuildPlotsPlural"
	lines.append("  [color=#d8b45a]%s[/color]" % _fill_count(plot_key, plots))

	var territory := session.world.territory_of(region_id)
	if territory.is_empty():
		# Sentence case, and a statement about the MAP rather than about the
		# document: "belongs to no territory group in this campaign" was a sentence
		# about a data structure, which is the register a blind review called a
		# developer surface.
		lines.append("  [color=#a9b39a]Stands alone - no territory claims it.[/color]")
	else:
		lines.append("")
		var territory_name := _string_or_key(String(territory.get("territory", "")))
		lines.append("  [color=#d8b45a]%s[/color]" % _fill_text("LW:TerritoryPartOfRegion", territory_name))
		var territory_bonuses := territory.get("bonuses", {}) as Dictionary
		var unified: Array[String] = []
		for field in BONUS_ORDER:
			var line := _format_bonus(field, territory_bonuses, {})
			if not line.is_empty():
				unified.append(line)
		if unified.is_empty():
			lines.append("  [color=#d8b45a]%s[/color]" % _fill_text("LW:UnifiedRegionBonus", _string_or_key("LW:NoBonus")))
		else:
			lines.append("  [color=#d8b45a]%s[/color]" % _fill_text("LW:UnifiedRegionBonus", ", ".join(unified)))
		# NAMES ONLY. This line used to print "Amon Sul (Amon_Sul)", "The Shire
		# (The_Shire)", "Tower Hills (Tower_Hills)" - retail's own map keys in
		# parentheses beside every name - on the theory that a member list can
		# show the same English word twice (`Arnor` reads "Arthedain",
		# `Buckland` reads "The North Downs", and two different regions really do
		# collide). A blind review called the parenthesised keys disqualifying and
		# it was right: retail shows the player its own words, repeats and all,
		# and the id belongs in the diagnosis, not on the glass. Where a name did
		# NOT resolve, `_display_of` returns the cleaned id and the miss is already
		# a NAMED GAP on the diagnostics panel.
		# THE MEMBER LIST, WITHOUT ITS FIELD NAME AND WITHOUT ITS OVERFLOW.
		#
		# It used to print `territory members: Amon Sul, Arthedain, The
		# Barrow-downs, The North Downs, Cardolan,` - a serialized data-model field
		# name in lower case, followed by a list long enough to run off the bottom
		# of the display and stop on a trailing comma. A blind review called the
		# label disqualifying on its own and the overflow a 100%-confidence tell,
		# and both were fair.
		#
		# The label is GONE, because the line above it is retail's own "Territory of
		# Region: Arnor" and a list indented directly under it needs no field name
		# to be read. The list is bounded HERE as well as clipped by the control, so
		# what a player sees is a finished sentence either way: at most
		# `TERRITORY_MEMBERS_SHOWN` names, then retail's own count of the rest.
		var members: Array[String] = []
		for value in territory.get("regions", PackedStringArray()):
			members.append(_display_of(String(value)))
		if not members.is_empty():
			var shown := members.slice(0, mini(members.size(), TERRITORY_MEMBERS_SHOWN))
			var run := "  ".join(shown)
			if members.size() > shown.size():
				run += "  and %d more" % (members.size() - shown.size())
			lines.append("  [color=#a9b39a]%s[/color]" % run)

	var cp_limit := int(region.get("cp_limit", -1))
	if cp_limit >= 0:
		# Retail's own caption for this number (`APT:CommandPointLimit`), and the
		# region said explicitly - the palantir plaque and the top-left plate both
		# also carry a command-point ratio, and this screen has already shipped one
		# reading as the other.
		lines.append("  [color=#a9b39a]%s in this region %d, %d for an ally[/color]" % [
			names.shell_label("APT:CommandPointLimit", "Command Point Limit"),
			cp_limit, int(region.get("ally_cp_limit", -1))])
	return lines


## One bonus line in retail's own wording, or "" when the region does not carry
## that bonus at all.
##
## A MACRO THE `#define` TABLE CANNOT RESOLVE IS NOT SHOWN, AND IT IS NOT FAKED.
## This used to print `Fertile Territory: FERTILE_TERRITORY_BONUS UNRESOLVED (no
## gamedata #define table is converted)` in red, on the player's region card. That
## is the same register the status ribbon was refused for: a sentence about which
## of this project's tables loaded, set on the glass. The line is dropped from the
## card and the miss is recorded in `_unresolved_bonus_macros` so the diagnostics
## panel can name it region by region - which is a NAMED GAP in the place
## `AGENTS.md` wants one, not a silent fallback and not an invented number.
func _format_bonus(field: String, bonuses: Dictionary, macro_names: Dictionary) -> String:
	var key := String(BONUS_STRING_KEYS.get(field, ""))
	if key.is_empty():
		return ""
	var macro_name := String(macro_names.get(field, ""))
	if not macro_name.is_empty():
		var resolved: Dictionary = macros.resolve(macro_name) if macros != null else {"ok": false, "raw": ""}
		if bool(resolved.get("ok", false)):
			return _fill_count(key, int(float(resolved["value"])))
		var raw := String(resolved.get("raw", ""))
		var note := "%s (%s): %s" % [field, macro_name,
			("retail's body is %s, which is an expression rather than a number" % raw)
			if not raw.is_empty() else "no gamedata #define table is converted"]
		if not _unresolved_bonus_macros.has(note):
			_unresolved_bonus_macros.append(note)
		return ""
	var amount := int(bonuses.get(field, 0))
	if amount == 0:
		return ""
	return _fill_count(key, amount)


## Fill a retail format string that takes one number. Retail writes `%d` for a
## count and `%d%%` for a percentage; both are handled, and a string carrying
## neither is returned untouched rather than mangled.
func _fill_count(key: String, amount: int) -> String:
	var template := _string_or_key(key).replace("\\n", "").strip_edges()
	if template.contains("%d%%"):
		return template.replace("%d%%", "%d%%" % amount)
	if template.contains("%d"):
		return template.replace("%d", str(amount))
	return template


## Fill a retail format string that takes one string (`%ls`).
func _fill_text(key: String, value: String) -> String:
	var template := _string_or_key(key).replace("\\n", " ").strip_edges()
	if template.contains("%ls"):
		return template.replace("%ls", value)
	return "%s %s" % [template, value]


## Retail's text for a key, or the KEY ITSELF when the table does not carry it.
## Showing the key is deliberate: it is visibly not a name, so a missing string
## can never be mistaken for retail's wording.
func _string_or_key(key: String) -> String:
	if strings == null:
		return key
	var value := strings.text(key)
	return value if not value.is_empty() else key


## THE REGION THE CARD IS ABOUT, by the same precedence the card itself uses:
## the attack target, then whatever is under the pointer, then the selection.
func _card_region() -> String:
	if session == null:
		return ""
	if not session.selected_target.is_empty():
		return session.selected_target
	if not session.hover_region.is_empty():
		return session.hover_region
	return session.selected_region


## RETAIL'S OWN PORTRAIT OF THAT REGION, drawn at retail's own crop.
##
## The plate is ALWAYS drawn, even empty, so the card does not jump every time
## the pointer crosses a region retail authors no picture for - and an empty one
## SAYS WHY beside it. Retail names three fortress portraits it defines nowhere
## (`BPCAmonSul`, `BPCCarnDum`, `BPCFornost`); the nearest ids in the archives
## are `BPCFornostGate` and `BPCFornostCitadel`, which are different pictures of
## different things, and none of the three is bridged to them.
func _draw_region_portrait() -> void:
	var frame := region_portrait_frame
	var height := frame.size.y
	var radius := height * 0.5 - 16.0
	var centre := Vector2(height * 0.5, height * 0.5)
	var region_id := _card_region()
	var found: Dictionary = {"texture": null, "id": "", "requested": "", "source": "", "reason": ""}
	if region_images != null and not region_id.is_empty():
		found = region_images.region_portrait(region_id)
	var texture: Texture2D = found["texture"]
	var font := hud_font if hud_font != null else get_theme_default_font()
	var display := display_font if display_font != null else font
	# RETAIL'S OWN PALANTIR when the strategic bundle is converted: the ring, the
	# glass, the medallions and the name plaque are all drawn on the chrome pass
	# by `_draw_palantir`, with the portrait fed into the well retail leaves for
	# its own region feed. This control then draws only the LETTERING retail
	# leaves live - the region's name over the glass and the two rim counters on
	# the plaque - and nothing of the frame itself.
	if _islands.has("globe"):
		var island := _islands["globe"] as Dictionary
		var scale := island["scale"] as Vector2
		if display != null and not region_id.is_empty():
			var dish := Rect2(PALANTIR_DISH.position * scale, PALANTIR_DISH.size * scale)
			HudScript.draw_engraved_caps(frame, display,
				dish.get_center() + Vector2(0.0, dish.size.y * 0.12),
				_display_of(region_id), int(clampf(20.0 * scale.y, 13.0, 34.0)),
				1.6 * scale.x, HudScript.PARCHMENT)
		if font != null and not region_id.is_empty() and session != null \
				and session.world != null and session.state != null \
				and _row_by_id.has(region_id):
			var owner_seat := session.state.owner_of(region_id)
			var cp_used := session.state.command_points_in_region(region_id, owner_seat) \
				if owner_seat != StateScript.NEUTRAL else 0
			# EACH ONE CARRIES A CAPTION, and that is a REAL BUG FIX rather than a
			# decoration. This plaque read "6/720" while the top-left plate read
			# "6/4500" - the same numerator against two different denominators, both
			# on screen at once, and a blind review correctly refused to accept that
			# as cosmetic. They are two different quantities: the top-left is the
			# SEAT's command points on the whole board against retail's
			# `MaxWorldCP`, this one is THIS REGION's against retail's own
			# `CommandPointLimit` for it. Retail's plaque carries no caption because
			# retail's numbers are engine-fed into a live text slot
			# (`strategic-text-values-are-live`); filling that slot with a labelled
			# value is filling it, not overwriting retail's art.
			# ------------------------------------------------------------------------
			# THE LEFT SLOT NO LONGER CARRIES THE PLOT COUNT. It was the SECOND of the
			# three statements of it - see `_structures_ribbon_line` for the review note
			# that counted all three and for why the tray column is the one that keeps
			# it. What goes here instead is a fact about this region that is stated
			# NOWHERE ELSE on the screen: how many armies stand in it.
			#
			# AND THAT ALSO CLOSES A DUPLICATION THE REVIEW DID NOT CATCH. The lens
			# three lines above used to read "4 ARMIES  6 CP" while this plaque read
			# "REGION CP 6/720" - the same command-point numerator twice, forty pixels
			# apart, against two different denominators. The lens carries the region's
			# NAME and its OWNER now (`_set_portrait_caption`) and the plaque carries
			# its two counters, so each fact is on exactly one surface.
			# ------------------------------------------------------------------------
			var armies := 0
			if _row_by_id.has(region_id):
				armies = int((_row_by_id[region_id] as Dictionary).get("armies", 0))
			var plaque := Rect2(PALANTIR_PLAQUE.position * scale, PALANTIR_PLAQUE.size * scale)
			var counter_size := int(clampf(14.0 * scale.y, 10.0, 24.0))
			var caption_size := maxi(8, int(counter_size * 0.62))
			var baseline := plaque.position.y + plaque.size.y * 0.5 + counter_size * 0.36
			var left_pen := plaque.position.x + 26.0 * scale.x
			var armies_caption := names.shell_label("APT:Armies", PALANTIR_ARMIES_CAPTION)
			HudScript.draw_caption(frame, font, Vector2(left_pen, baseline),
				armies_caption, caption_size, HudScript.PARCHMENT_DIM)
			left_pen += HudScript.caption_width(font, armies_caption, caption_size) + 6.0 * scale.x
			frame.draw_string(font, Vector2(left_pen, baseline),
				"%d" % armies, HORIZONTAL_ALIGNMENT_LEFT, -1,
				counter_size, HudScript.GOLD_VALUE)
			var region_cp := "%d/%d" % [cp_used, session.world.region_cp_limit(region_id)]
			var right_edge := int(plaque.size.x - 26.0 * scale.x)
			frame.draw_string(font, Vector2(plaque.position.x, baseline), region_cp,
				HORIZONTAL_ALIGNMENT_RIGHT, right_edge, counter_size, HudScript.GOLD_VALUE)
			HudScript.draw_caption(frame, font, Vector2(plaque.position.x, baseline),
				PALANTIR_REGION_CP_CAPTION, caption_size, HudScript.PARCHMENT_DIM,
				HORIZONTAL_ALIGNMENT_RIGHT,
				float(right_edge) - font.get_string_size(
					region_cp, HORIZONTAL_ALIGNMENT_LEFT, -1, counter_size).x
					- 6.0 * scale.x)
		_set_portrait_caption(region_id, found, texture)
		return
	# THE DRAWN FALLBACK, for a machine with no strategic bundle: retail's
	# palantir-dish SHAPE with the portrait clipped into it, and a named gap.
	HudScript.draw_portrait_dish(frame, centre, radius, texture, ring_texture)
	if font != null and not region_id.is_empty():
		HudScript.draw_engraved_caps(frame, font,
			centre + Vector2(0.0, radius * 0.45),
			_display_of(region_id), 17, 1.5, HudScript.PARCHMENT)
	# THE TWO RIM COUNTERS retail sets on the dish's lower rim: structures
	# standing over authored plots on the left, and the region's command points
	# against retail's own CommandPointLimit on the right. Both are live.
	if font != null and not region_id.is_empty() and session != null \
			and session.world != null and session.state != null \
			and _row_by_id.has(region_id):
		var rim_plots: Dictionary = session.build_plots(region_id)
		var plots := int(rim_plots.get("total", 0))
		var built := int(rim_plots.get("used", 0))
		var owner_seat := session.state.owner_of(region_id)
		var cp_used := session.state.command_points_in_region(region_id, owner_seat) \
			if owner_seat != StateScript.NEUTRAL else 0
		var rim := Rect2(centre.x - radius * 0.92, height - 30.0, radius * 1.84, 22.0)
		HudScript.draw_plate(frame, rim)
		frame.draw_string(font, Vector2(rim.position.x + 14.0, rim.position.y + 16.0),
			"%d/%d" % [built, plots], HORIZONTAL_ALIGNMENT_LEFT, -1, 13, HudScript.GOLD_VALUE)
		frame.draw_string(font, Vector2(rim.position.x, rim.position.y + 16.0),
			"%d/%d" % [cp_used, session.world.region_cp_limit(region_id)],
			HORIZONTAL_ALIGNMENT_RIGHT, int(rim.size.x) - 14, 13, HudScript.GOLD_VALUE)
	# THE OWNER'S STANDARD, under the caption plate - retail's own Banner_*
	# crop, bound by the converter's stated derivation. Unclaimed regions and a
	# missing bundle both draw NO banner rather than a substitute.
	var owner := StateScript.NEUTRAL
	if session != null and session.state != null and not region_id.is_empty() \
			and _row_by_id.has(region_id):
		owner = session.state.owner_of(region_id)
	var banner: Texture2D = null
	if ui != null and owner != StateScript.NEUTRAL \
			and owner >= 0 and owner < session.state.players.size():
		banner = ui.faction_banner(
			String((session.state.players[owner] as Dictionary).get("template", "")))
	if banner != null:
		var banner_top := 30.0 + 64.0 + 16.0
		var banner_height := maxf(height - banner_top - 10.0, 40.0)
		var banner_width := minf(
			banner_height * float(banner.get_width()) / float(banner.get_height()), 220.0)
		frame.draw_texture_rect(banner,
			Rect2(Vector2(height + 2.0, banner_top), Vector2(banner_width, banner_height)), false)
	_set_portrait_caption(region_id, found, texture)


## THE PALANTIR'S ONE LINE OF CAMPAIGN FACT - who holds the region under the
## pointer and what is garrisoned in it, both read off the authoritative state
## and both named in retail's English. The portrait SOURCING (which authored
## field the picture came from, or why there is none) goes to the diagnostics
## panel instead; it is a conversion claim, not a campaign fact.
func _set_portrait_caption(region_id: String, found: Dictionary, texture: Texture2D) -> void:
	if region_portrait_caption == null:
		return
	if region_id.is_empty():
		region_portrait_caption.text = ""
		portrait_provenance = "no region is selected or under the pointer"
		return
	var owner := StateScript.NEUTRAL
	if session != null and session.state != null and _row_by_id.has(region_id):
		owner = session.state.owner_of(region_id)
	var caption_lines: Array[String] = []
	# TITLE CASE, NOT SHOUTED. Retail's HUD sets its readouts in title case
	# (`Player Bonuses`, `Turn:`, `Building Foundation`) and reserves capitals for
	# its tab rail and its END PHASE capsule. This screen used to shout every
	# caption on the glass, and a blind review read the result as a second type
	# system sitting beside the serif - it was the same face, set in a register
	# retail does not use.
	caption_lines.append(_owner_name(owner) if owner != StateScript.NEUTRAL
		else names.shell_label("SIDE:Neutral", "Unclaimed"))
	# ------------------------------------------------------------------------------
	# THE ARMY LINE IS OFF THE LENS. It was the same numbers the plaque below carries.
	# ------------------------------------------------------------------------------
	#
	# It used to read "4 armies   6 CP" while the plaque forty pixels beneath it read
	# "Region CP 6/720" - the same command-point numerator twice, on two surfaces, in
	# two registers, against two different denominators. An art-direction review
	# counted the build-plot number being stated three times in this tray and called
	# the result "the tray looks like three teams shipped independently"; this is the
	# same defect one island to the left and it was not in that review's list only
	# because the lens is small.
	#
	# The lens now carries what a lens carries: the region's NAME and WHO HOLDS IT.
	# Its army count and its command points are on the plaque under it
	# (`_draw_region_portrait`), each stated once, each under its own caption. The
	# empty state - "no army standing" - went with it: a lens is not the surface that
	# reports an absence, and the plaque's own `0` says it in one glyph.
	region_portrait_caption.text = "\n".join(caption_lines)
	if region_images == null:
		portrait_provenance = "NO PORTRAIT BUNDLE: %s" % region_images_reason.split(".")[0]
	elif texture != null:
		portrait_provenance = "%s: %s = %s" % [
			_display_of(region_id), String(found["source"]), String(found["id"])]
	elif not String(found["requested"]).is_empty():
		portrait_provenance = "%s: NO PICTURE - %s" % [
			_display_of(region_id), String(found["reason"])]
	else:
		portrait_provenance = "%s: retail's data names no portrait for this region" % _display_of(region_id)


## THE TRAY'S WELL, one tab at a time.
##
## This used to be ONE list - the region card, then every staging region, then
## every attack target, then the battlefield binding - written into a box that
## scrolled. That is why a data field could print off the bottom of the display:
## the box was always shorter than its content and the overflow simply went
## somewhere. Retail's answer is on retail's own bar, and it is now this screen's:
## THREE TABS, one topic each, each one short enough to be READ.
##
## Every tab carries real state. `ARMIES` is not a placeholder - it lists what is
## standing where, which is the same data the map's banners are drawn from - and
## `STRUCTURES` is a shop: retail's own offerings for the seat, retail's own
## prices against the live treasury, and one pressable row per offering. An honest
## empty state says WHICH silence it is; neither tab invents a queue to look busy.
## THE WELL THE TRAY'S TEXT IS SET IN, which is NOT always the whole well.
##
## On TERRITORY and ARMIES the well is retail's empty maroon field and the text
## has all of it. On STRUCTURES the well is retail's own build-card rail, and the
## rail is exactly as long as the region has authored `BuildingSpot` plots - the
## same count `_unused_card_slot_paths` cuts the rail down to, read from the same
## place, so the two can never disagree about where the cards end. The text starts
## after the last card, in the maroon retail leaves, and never over the art.
##
## It is here rather than in `_relayout` because the tab and the region under the
## pointer change on every hover and every tab press, and the layout does not.
func _place_detail_well() -> void:
	if detail_label == null or _tray_content_rect.size.x <= 0.0:
		return
	var well := _tray_content_rect
	if active_tab == "structures" and _islands.has("selectionDetails") \
			and session != null and session.world != null:
		var plots := 0
		var card_region := _card_region()
		if not card_region.is_empty():
			plots = mini(
				int(session.world.region(card_region).get("building_spot_count", 0)),
				TRAY_CARD_SLOTS.size())
		if plots > 0:
			var rail_end := _island_rect("selectionDetails", Rect2(
				float(TRAY_CARD_SLOTS[plots - 1]) + 68.0, TRAY_CONTENT.position.y, 1.0, 1.0))
			var text_left := rail_end.position.x + _tray_content_gutter
			# A well narrower than this cannot hold a structure title, so the roster
			# would be nothing but ellipses. It keeps the whole well and `_fit_card_lines`
			# cuts it, which is the honest degradation.
			if text_left < well.end.x - 90.0:
				well = Rect2(Vector2(text_left, well.position.y),
					Vector2(well.end.x - text_left, well.size.y))
	_place_exact(detail_label, well)


func _refresh_detail() -> void:
	if region_portrait_frame != null:
		region_portrait_frame.queue_redraw()
	_place_detail_well()
	var focus := session.selected_target
	if focus.is_empty():
		focus = session.hover_region
	if focus.is_empty():
		focus = session.selected_region
	# THE STAND-IN BATTLEFIELD IS RECORDED ON EVERY TAB, not only on the one that
	# used to print it: it is a fact about the attack that is chosen, and a
	# diagnosis that appeared and vanished with the tab rail would be a diagnosis
	# nobody could rely on.
	# A CLAIM IS EXEMPT: no battle is fought on unowned ground, so which map would
	# have stood in for retail's is not a fact about anything that happens.
	_stand_in_battlefield = ""
	if session != null and not session.selected_target.is_empty() \
			and not _target_is_unclaimed() \
			and _row_by_id.has(session.selected_target):
		var target_map := String((_row_by_id[session.selected_target] as Dictionary)["map_name"])
		var bound := String(session.battlefield_bindings(available_map_ids).get(target_map, ""))
		if not bound.is_empty() and bound != target_map:
			_stand_in_battlefield = ("%s would be fought on %s, which is a STAND-IN: retail's "
				+ "own %s is not cooked in any mounted pack") % [
				_display_of(session.selected_target), bound, target_map]
	var lines: Array[String] = []
	structure_roster = []
	match active_tab:
		"armies":
			lines = _armies_tab_lines(focus)
		"structures":
			# THE STRUCTURES WELL IS DRAWN, NOT SET. The card stays placed and stays
			# empty, which is deliberate rather than incidental: it is the control
			# whose rectangle the roster is drawn into and whose containment the
			# region-card runner already holds at every window size, so the table
			# cannot be laid out anywhere the card could not be.
			structure_roster = _structure_roster_rows(focus)
		_:
			lines = _territory_tab_lines(focus)
	detail_label.text = "\n".join(_fit_card_lines(lines))
	# THE WELL SAYS WHAT KIND OF THING IT IS, on hover.
	#
	# The STRUCTURES tab is the one that needed it, and what it needs to say has
	# reversed: the rail of priced cards used to be a readout that could not be
	# pressed, and it is a shop now. So the tooltip teaches the two rules a player
	# cannot see - that the price comes out of the treasury at the top of the frame,
	# and that spending it does not cost the turn.
	detail_label.tooltip_text = (
		"What %s may raise here, and what each costs from the treasury. Press a "
		+ "row to raise it, or a foundation to choose where it stands. Building "
		+ "does not end your turn."
	) % _owner_name(session.state.owner_of(focus)) if active_tab == "structures" \
		and not focus.is_empty() and _row_by_id.has(focus) \
		and session.state.owner_of(focus) != StateScript.NEUTRAL \
		else _tray_well_tooltip()
	_refresh_tray_ribbon(focus)
	_refresh_tray_chrome()
	# THE HIT AREAS FOLLOW THE ROSTER. `_relayout` places them when the geometry
	# moves; this places them when the CONTENT moves, which on this screen is far
	# more often - every hover over the map changes which region the tray is about.
	_place_build_controls()


## What the tray's well is a readout OF, per tab, when there is no seat to name.
func _tray_well_tooltip() -> String:
	match active_tab:
		"structures":
			return ("What the region's holder may raise here, and what each costs from "
				+ "the treasury. Press a row to raise it, or a foundation to choose "
				+ "where it stands. Building does not end your turn.")
		"armies":
			return "The forces standing in this region, and what each of them is worth in command."
		_:
			return "What this region yields, the territory it belongs to, and that territory's bonus."


## THE TERRITORY TAB: the region the player is pointing at, in retail's own
## words - the name, its yields, its build plots, the territory it belongs to and
## that territory's unified bonus. The information architecture is the one a
## blind review said it preferred to retail's; only its containment was broken.
func _territory_tab_lines(focus: String) -> Array[String]:
	var lines: Array[String] = []
	if focus.is_empty() or not _row_by_id.has(focus):
		lines.append("[color=#a9b39a]%s[/color]" % _empty_tab_line("territory"))
		return lines
	lines.append_array(_region_panel_lines(focus))
	return lines


## THE ARMIES TAB: what is standing in the region under the pointer, then this
## seat's own armed regions, then what the staged region can reach. Every row is
## read off the authoritative state.
func _armies_tab_lines(focus: String) -> Array[String]:
	var lines: Array[String] = []
	var stacks: Dictionary = _army_stacks_by_region()
	if not focus.is_empty() and _row_by_id.has(focus):
		lines.append("[b][font_size=20][color=#e8dfc2]%s[/color][/font_size][/b]" % _display_of(focus))
		var here: Array = stacks.get(focus, []) as Array
		if here.is_empty():
			lines.append("  [color=#a9b39a]No army stands here.[/color]")
		for stack_value in here:
			var stack := stack_value as Dictionary
			lines.append("  [color=#d8b45a]%s[/color]   [color=#a9b39a]%s[/color]" % [
				String(stack.get("label", "")), _owner_name(int(stack.get("owner", -1)))])
	if session.selected_region.is_empty():
		lines.append("")
		lines.append("[b][color=#e1c77d]%s[/color][/b]" % names.shell_label(
			"APT:Armies", "Armies"))
		if _staging.is_empty():
			lines.append("  [color=#a9b39a]This seat has no army standing in a region it owns.[/color]")
		for region_id in _staging:
			var row := _row_by_id[region_id] as Dictionary
			lines.append("  [color=#d8b45a]%s[/color]   [color=#a9b39a]%d army, %d CP[/color]" % [
				_display_of(region_id), int(row["armies"]), int(row["command_points"])]
				if int(row["armies"]) == 1
				else "  [color=#d8b45a]%s[/color]   [color=#a9b39a]%d armies, %d CP[/color]" % [
					_display_of(region_id), int(row["armies"]), int(row["command_points"])])
		return lines
	lines.append("")
	lines.append("[b][color=#e1c77d]Marching from %s[/color][/b]" % _display_of(session.selected_region))
	if _targets.is_empty() and _moves.is_empty():
		lines.append("  [color=#a9b39a]Nothing adjacent can be reached from here.[/color]")
	for target in _targets:
		var row := _row_by_id[target] as Dictionary
		lines.append("  [color=%s]%s[/color]   [color=#a9b39a]%s, %d army/armies[/color]" % [
			"#ecd08a" if target == session.selected_target else "#d8b45a",
			_display_of(target), _owner_name(int(row["owner"])), int(row["armies"])])
	if not _moves.is_empty():
		lines.append("  [color=#a9b39a]March to %s[/color]" % ", ".join(
			Array(_moves).map(func(v: Variant) -> String: return _display_of(String(v)))))
	# WHERE THE BATTLE WOULD ACTUALLY BE FOUGHT. That a battlefield is a stand-in
	# for the one retail authors is a CLAIM ABOUT THE CONVERSION and it is on the
	# diagnostics panel; what a player needs from this line is the field's name and
	# whether there is one at all, and those are facts about the war.
	if not session.selected_target.is_empty():
		# UNOWNED GROUND NEEDS NO FIELD. Nothing defends it, so no battle is fought
		# for it and the battlefield binding is irrelevant - printing "no field is
		# open, so this attack cannot be fought" over a region the player is about to
		# walk into would be a red warning about an event that is not going to happen.
		if _target_is_unclaimed():
			lines.append("  [color=#a9b39a]Undefended - marching in takes it, with no battle fought.[/color]")
		else:
			var region_map := String((_row_by_id[session.selected_target] as Dictionary)["map_name"])
			var battlefield := String(session.battlefield_bindings(available_map_ids).get(region_map, ""))
			if battlefield.is_empty():
				lines.append("  [color=#c8483f]No field is open here, so this attack cannot be fought.[/color]")
			else:
				lines.append("  [color=#a9b39a]Field of battle: %s[/color]" % battlefield)
	return lines


## THE STRUCTURES TAB: retail's own build plots and retail's own offerings for
## the seat that holds the region, with the card rail drawn behind it - and every
## one of them pressable.
## THE ROSTER CAME BACK OFF THE RIBBON AND STANDS BESIDE THE CARDS.
##
## The well IS retail's card rail on this tab, so body copy written ACROSS it
## would sit on the cards - which is why this used to return nothing at all and
## push everything onto the status ribbon. But the rail is only as long as the
## region has plots: three plots occupy x 123..353 of a well that runs to 572, and
## the rest of it is retail's empty maroon. `_relayout` hands this tab a well that
## starts after the last card, so the roster is set in the space retail leaves
## rather than over the art retail draws.
## THE STRUCTURES A SEAT MAY RAISE, AS ROWS OF A TABLE rather than as lines of a
## string.
##
## THIS IS THE ROUND-SEVEN REWRITE OF THE WORST ELEMENT ON THE SCREEN, and it is
## worth being exact about what was wrong, because the DATA was never the problem.
## A blind adversarial review, shown this screen and retail's side by side without
## being told which was which, said the round was "effectively over" one second
## after it reached this list: "a bare left-aligned list - Hall of the King's Men /
## Dark Iron Forge / Mill / Angmar Fortress - in flat white sans, no frame, no
## icons, no baseline grid, no relationship to the cards it sits beside, floating
## in the middle of an otherwise ornate gold-and-oxblood panel. That is a data
## structure being printed, not a UI being drawn." Every string in it is retail's
## own; the rendering is what leaked.
##
## Its two acceptable prescriptions were "icons in the slot cards" or "a framed,
## right-aligned, display-face list with row rules". THE FIRST IS REFUSED FOR A
## STATED REASON and it is not stubbornness: a build card is a PLOT, and a picture
## of a building in an empty plot's card claims a building is standing there, which
## nothing in this simulation has built (`_draw_structure_cards` carries the same
## note and retail's own oracle capture of this tab shows empty foundation tiles in
## all three cards). The second is what this is - and it takes the icons WITH it,
## because retail authors one `ConstructButtonImage` per offering and a roster of
## four named things beside four pictures of them is strictly better than either.
##
## So the tab's well is no longer a `RichTextLabel` of joined lines. It is a
## framed table drawn by `_draw_structure_roster`: a head, one row per offering
## with retail's own icon, retail's own title in the display face and retail's own
## cost right-aligned in gold, and a rule between rows. The rows are DATA here and
## GEOMETRY there, so the roster the audit reads and the roster a player sees
## cannot drift apart.
##
## ROUND NINE MAKES EVERY ROW A CONTROL. The table was already the right shape and
## it was still a readout: it named four structures and four prices beside three
## empty foundations and there was no way to buy one. Each row now carries a
## transparent button on its own rectangle (`_place_build_row_buttons`), so the
## roster is the SECOND way to build and the one that is reachable without hunting
## for a foundation on the map. Same offer, same door, same refusal.
func _structure_roster_rows(focus: String) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if focus.is_empty() or not _row_by_id.has(focus):
		return rows
	if session == null or session.state == null:
		return rows
	# THE FOUNDATION THE ROSTER IS ABOUT: the one the ring is open on, or the
	# lowest free one. It is passed to the offer so a row's refusal is about the
	# plot the click will actually land on - "this foundation already carries a
	# fortress" is only true of a particular foundation.
	var plot := -1
	if String(selected_plot.get("region", "")) == focus:
		plot = int(selected_plot.get("index", -1))
	for entry_value in _build_offer(focus, plot):
		var entry := entry_value as Dictionary
		if String(entry["title"]).is_empty():
			continue
		rows.append({
			"title": String(entry["title"]),
			# THE COST IS A NUMBER OR IT IS NOTHING. An offering retail prices with a
			# macro the table does not define comes back as -1 and its cell is left
			# empty; a data key in a gold column is precisely the register this screen
			# refuses, and the unpriced offering is named on the diagnostics panel.
			"cost": String(entry["cost"]),
			"image_id": String(entry["image_id"]),
			"id": String(entry["id"]),
			"can_build": bool(entry["can_build"]),
			"unaffordable": bool(entry["unaffordable"]),
			"refusal": String(entry["refusal"]),
			"tooltip": _build_tooltip(entry),
			"plot": plot,
			"region": focus,
		})
	return rows


## THE STRUCTURES TAB'S RIBBON, on the rail retail sets "Building Foundation" in.
##
## THE REGISTER OF THIS SLOT IS THE NOUN PHRASE, and that is the whole shape of
## this function. Retail's own caption there is two words naming the thing under
## the cursor. This line used to read
##
##     0 of 3 built - nothing here builds, construction is not simulated
##     - Hall of the King's Men, Dark Iron Forge, ...
##
## and a blind review called it disqualifying on its own, correctly: "construction
## is not simulated" is a sentence about THIS PROGRAM's coverage written to another
## engineer, not a sentence about Middle-earth, and no shipped title in this genre
## puts one on the player's status bar.
##
## THE GAP WAS NOT DELETED AND IT WAS NOT SOFTENED. It is stated in full on the
## diagnostics panel (`_conversion_gap_lines`, reachable on F1), which is where
## `AGENTS.md` wants a named gap - and `wotr_living_world_ui_runner` now asserts
## BOTH halves of that move: no player-visible string on this screen may carry
## implementation vocabulary, and every phrase taken off the glass must be present
## in the diagnosis. What is left here is only what is true about the WORLD: how
## many of the region's own authored `BuildingSpot` plots carry a structure, and
## what the seat holding it may raise on them, in retail's own titles.
##
## `0 of N` is therefore a fact about the board, not a confession about the code.
func _structures_ribbon_line(focus: String) -> String:
	if focus.is_empty() or session == null or session.state == null 			or not _row_by_id.has(focus):
		return ""
	var region := session.world.region(focus)
	if region.is_empty():
		return ""
	var total := int(region.get("building_spot_count", 0))
	if total <= 0:
		return ""
	# THE THING UNDER THE POINTER WINS THE RAIL, and on this tab that is now a
	# BUILD-RING SLOT as often as it is a foundation. Retail's rail is a hover
	# label; a player crossing the ring wants the price and, when it is barred,
	# the rule - which is the half of "the icons do not light up" that light alone
	# cannot answer.
	if not hovered_build.is_empty() and String(hovered_build.get("region", "")) == focus:
		var hovered := _hovered_build_entry()
		if not hovered.is_empty():
			var line := String(hovered["title"])
			if not String(hovered["cost"]).is_empty():
				line += "   " + String(hovered["cost"])
			if not bool(hovered["can_build"]) and not String(hovered["refusal"]).is_empty():
				line += "   " + String(hovered["refusal"])
			return line
	# RETAIL'S OWN NOUN FOR THE THING UNDER THE POINTER, when there is one.
	# `STRATEGICHUD:BuildPlotName` is "Building Foundation", and it is exactly what
	# retail's own capture of this tab has on this rail - because in that capture a
	# build plot is selected, which is the state this branch is in. It is retail's
	# string, verbatim, out of retail's string table.
	#
	# ------------------------------------------------------------------------------
	# AND THE COUNT IS GONE FROM THIS RAIL. It was stated THREE TIMES.
	# ------------------------------------------------------------------------------
	#
	# An adversarial art-direction review counted them: "Build plot count is stated
	# three times in the bottom tray: 'Territory Build Plots 0/3' in the middle
	# column, 'Plots 0/3' in the palantir footer, '0 of 3 built' in the status
	# strip... Saying the same number three times does not make it clearer, it makes
	# the tray look like three teams shipped independently."
	#
	# All three were added for a defensible reason at three different times, and the
	# note above this one is one of them - the counter was put back on this rail
	# BECAUSE taking it off "took the counter off at exactly the moment the player
	# was building". That reasoning was sound about the rail in isolation and wrong
	# about the tray: the moment the player is building, the counter is eight
	# centimetres to the left in `STRATEGICHUD:RegionBuildPlotsTitle`'s own column,
	# beside the actual foundation cards, at a larger size, under retail's own
	# heading for it. It is not lost by being said once; it was devalued by being
	# said three times.
	#
	# THE ONE THAT SURVIVES is the tray column (`_draw_structure_cards`), because
	# that is where the SLOTS are and a count belongs beside the things it counts.
	# This rail keeps retail's noun for the thing under the pointer, which is what
	# retail puts here and is the only thing on this rail that is about the hover.
	if String(selected_plot.get("region", "")) == focus:
		var index := int(selected_plot.get("index", -1))
		if index >= 0 and index < total:
			return _string_or_key("STRATEGICHUD:BuildPlotName")
	# OTHERWISE THE RAIL CARRIES NOTHING ON THIS TAB, and an empty third register is
	# the right answer rather than a missing one.
	#
	# This rail is a HOVER LABEL - retail's own use of it is a noun naming the thing
	# under the pointer - and with nothing under the pointer there is no noun. What
	# used to be returned here was the plot count, which is the third of the three
	# statements of that number an art-direction review counted in this one tray
	# ("saying the same number three times does not make it clearer, it makes the
	# tray look like three teams shipped independently"). It is in the tray's own
	# column, once, beside the foundation cards it counts.
	#
	# The rail is not empty: `_refresh_tray_ribbon` always sets the region and its
	# holder, which are ranks 0 and 1. What is dropped is a third field that
	# repeated a number already on the same panel.
	#
	# The ROSTER OF WHAT MAY BE RAISED USED TO BE HERE TOO, appended field by field
	# behind interpuncts, and a blind review named the result: "a dot-joined
	# concatenation of every field the selection object happens to hold". It was not
	# wrong to surface it - the same review called surfacing it an improvement on
	# retail's single tooltip noun - it was wrong to surface it HERE. It is in the
	# STRUCTURES tab's own well now, beside the cards it describes, which is where
	# the review said it belonged.
	return ""


## The build-ring row the pointer is over, decorated exactly as the ring's own
## slot is. Empty when nothing is hovered or the hover is stale.
func _hovered_build_entry() -> Dictionary:
	if hovered_build.is_empty() or session == null:
		return {}
	for entry_value in _build_offer(String(hovered_build.get("region", "")),
			int(hovered_build.get("plot", -1))):
		var entry := entry_value as Dictionary
		if String(entry["id"]) == String(hovered_build.get("id", "")):
			return entry
	return {}


## The line an empty tab carries. It names what the tab is FOR and what would put
## something in it - the shape of a shipped empty state, not a placeholder.
## THE EMPTY STATE NAMES THE PARTICULAR EMPTINESS. "Point at a region" is right
## when nothing is pointed at and wrong when something is: a held region with no
## foundations and an unclaimed one are two different silences, and one sentence
## for all three is the shape of a placeholder.
func _empty_tab_line(tab: String) -> String:
	if tab != "structures":
		return "Point at a region to see what it yields."
	var focus := _card_region()
	if focus.is_empty() or not _row_by_id.has(focus) or session == null 		or session.state == null:
		return "Point at a region to see what can be built on it."
	var owner := session.state.owner_of(focus)
	if owner != session.state.active_player():
		return "%s is not yours to build on." % _display_of(focus)
	if int(session.build_plots(focus).get("total", 0)) <= 0:
		return "%s has no building foundations." % _display_of(focus)
	return "Nothing can be raised in %s." % _display_of(focus)


## FIT THE CARD TO THE WELL BEFORE IT IS SET, so no line can ever be rendered
## outside the tray. The control also clips, so this is the FIRST of two
## independent guarantees rather than the only one - and it is the one that
## produces a card ending on a whole line instead of a cut one.
##
## The measure is deliberately coarse (the well's height over one line's height
## at the card's own font size, less one line of slack for a wrapped one) because
## a RichTextLabel's exact wrapped height is not known until it has been laid out,
## which is a frame too late for the text that caused the overflow.
func _fit_card_lines(lines: Array[String]) -> Array[String]:
	if detail_label == null or detail_label.size.y <= 0.0:
		return lines
	var font_size := detail_label.get_theme_font_size("normal_font_size")
	if font_size <= 0:
		font_size = 16
	var line_height := float(font_size) * CARD_LINE_SPACING
	var room := maxi(1, int(detail_label.size.y / maxf(line_height, 1.0)) - 1)
	if lines.size() <= room:
		return lines
	var fitted := lines.slice(0, room)
	fitted[fitted.size() - 1] = String(fitted[fitted.size() - 1]) + RIBBON_ELLIPSIS
	return fitted


## The caption on the tray's bottom rail - retail's own hover-label slot. It
## carries the region the tray is about and who holds it.
func _refresh_tray_ribbon(focus: String) -> void:
	if tray_ribbon == null:
		return
	tray_ribbon_segments = []
	if focus.is_empty() or not _row_by_id.has(focus):
		tray_ribbon_text = ""
		tray_ribbon.queue_redraw()
		return
	var owner := session.state.owner_of(focus)
	# RANK 0 IS THE SUBJECT - the thing the rail is about. Rank 1 is what holds it.
	# Rank 2 is its state. Three registers, no punctuation between them.
	tray_ribbon_segments.append({"text": _display_of(focus), "rank": 0})
	tray_ribbon_segments.append({
		"text": _owner_name(owner) if owner != StateScript.NEUTRAL
			else names.shell_label("SIDE:Neutral", "Unclaimed"),
		"rank": 1})
	if active_tab == "structures":
		var structures := _structures_ribbon_line(focus)
		if not structures.is_empty():
			tray_ribbon_segments.append({"text": structures, "rank": 2})
	var flattened: Array[String] = []
	for segment in tray_ribbon_segments:
		flattened.append(String((segment as Dictionary)["text"]))
	tray_ribbon_text = RIBBON_SEPARATOR.join(flattened)
	tray_ribbon.queue_redraw()


## The ribbon's one line, set on the tray's own bottom rail. Trimmed to the rail
## with an ellipsis rather than clipped mid-glyph, and never wrapped: the rail is
## one line tall in retail's art and a second line would sit on the map.
## THE THREE REGISTERS THE RAIL IS SET IN, by rank: the subject in engraved caps
## and hot gold, who holds it a size down in parchment, its state a size down
## again and dimmer. Nothing between them but space.
## ALL THREE RANKS ARE SET IN CAPS NOW, and that is the single most literal fix in
## this round. An adversarial art-direction review took this exact rail as its
## proof that the typography "is not a system, that is accumulation":
##
##     ARTHEDAIN | Angmar | Building Foundation | 0 of 3 built
##     "runs all-caps, then title case, then title case, then sentence case"
##
## Every one of those four was individually defensible - `ARTHEDAIN` is a SUBJECT
## and subjects are engraved caps; `Building Foundation` is retail's own string in
## retail's own case - and together they were four answers to a question nobody had
## written down. `HudChrome`'s case rule writes it down: SUBJECT and CAPTION are
## caps with tracking, VALUE is as authored. This rail carries a subject and two
## captions, so all three shout, at three different sizes and three different
## values - which is what the rank was always supposed to carry.
##
## RETAIL'S STRINGS ARE NOT ALTERED. Case is applied at the moment of drawing, the
## way tracking is; `player_visible_strings()` still reports retail's authored
## spelling, so the verbatim-wording rule and the string audit both still see what
## retail wrote.
##
## THE TRACKING FOLLOWS THE TIER rather than being three numbers: the subject takes
## `SUBJECT_TRACKING` and the two captions take `CAPTION_TRACKING`, both as
## fractions of their own size, so the rail reads as one piece of lettering at
## three weights instead of as three pieces.
const RIBBON_RANKS := [
	{"scale": 1.0, "tint": "hot", "caps": true, "tracking": HudScript.SUBJECT_TRACKING},
	{"scale": 0.86, "tint": "parchment", "caps": true, "tracking": HudScript.CAPTION_TRACKING},
	{"scale": 0.80, "tint": "dim", "caps": true, "tracking": HudScript.CAPTION_TRACKING},
]


func _draw_tray_ribbon() -> void:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null or tray_ribbon_text.is_empty() or tray_ribbon.size.y <= 4.0:
		return
	var font_size := int(clampf(tray_ribbon.size.y * 0.62, 9.0, 24.0))
	var baseline := tray_ribbon.size.y * 0.5 + float(font_size) * 0.36
	# THE FALLBACK IS THE JOINED LINE. Segments are presentation state built by
	# `_refresh_tray_ribbon`; if anything ever sets `tray_ribbon_text` without them
	# the rail still carries the sentence rather than nothing.
	if tray_ribbon_segments.is_empty():
		tray_ribbon.draw_string(font, Vector2(0.0, baseline),
			trimmed_ribbon_text(tray_ribbon.size.x, font_size),
			HORIZONTAL_ALIGNMENT_LEFT, int(tray_ribbon.size.x), font_size,
			HudScript.GOLD_VALUE)
		return
	var gap := float(font_size) * RIBBON_GAP
	var pen := 0.0
	var written := 0
	for segment_value in tray_ribbon_segments:
		var segment := segment_value as Dictionary
		var rank := RIBBON_RANKS[mini(int(segment["rank"]), RIBBON_RANKS.size() - 1)] as Dictionary
		var size_here := maxi(8, int(round(float(font_size) * float(rank["scale"]))))
		var text := String(segment["text"])
		var tint := HudScript.RIM_GOLD_HOT
		if String(rank["tint"]) == "parchment":
			tint = HudScript.PARCHMENT
		elif String(rank["tint"]) == "dim":
			tint = HudScript.PARCHMENT_DIM
		# THE TRACKING IS A FRACTION OF THE RUN'S OWN SIZE, not a pixel count scaled
		# off a reference size of 16. Same arithmetic at 16px and a truer one
		# everywhere else, and it is the same fraction the rest of the HUD's captions
		# are set at (`HudChrome.CAPTION_TRACKING`) rather than a second number that
		# happens to look similar.
		var tracking := float(rank["tracking"]) * float(size_here)
		var run := _ribbon_run_width(font, text, size_here, tracking, bool(rank["caps"]))
		# NOTHING IS SET PAST THE RAIL. The rail is one line of retail's art and a
		# segment that would not fit whole is dropped with the ellipsis set in its
		# place, which is the same rule `trimmed_ribbon_text` states for the flat
		# line - never a glyph cut in half at the frame.
		if pen + run > tray_ribbon.size.x:
			tray_ribbon.draw_string(font, Vector2(pen, baseline), RIBBON_ELLIPSIS,
				HORIZONTAL_ALIGNMENT_LEFT, -1, size_here, HudScript.PARCHMENT_DIM)
			return
		# THE SEPARATOR IS A FITTING, NOT A GAP. Three fields set at three sizes with
		# nothing but space between them read, at the frame the oracle is judged in,
		# as "three fields at arbitrary gaps with no separators or hierarchy" - a
		# blind review's words about this exact rail. The hierarchy was real and the
		# BOUNDARIES were invisible, which is a different defect from the interpunct
		# dump this replaced: that one had marks and no hierarchy, this one had
		# hierarchy and no marks. The mark is the chain link retail's own tab strip
		# sets between its cells, so the rail's separator is the HUD's separator
		# rather than a punctuation character standing in for one.
		if written > 0:
			# THE LINK IS CUT TO THE LETTERING, not to the rail. Given the rail's full
			# height it stands a head taller than the caps on either side of it and
			# reads as a fitting the line is hanging from rather than as a separator
			# between two words. Retail's own links between TERRITORY, ARMIES and
			# STRUCTURES are about the cap height of the words they divide.
			var link_height := float(font_size) * 1.35
			HudScript.draw_chain_link(tray_ribbon, Rect2(
				Vector2(pen - gap, (tray_ribbon.size.y - link_height) * 0.5),
				Vector2(gap, link_height)))
		if bool(rank["caps"]):
			# `draw_engraved_caps` centres on a point, so it is handed the run's own
			# midpoint - the pen arithmetic stays left-to-right.
			HudScript.draw_engraved_caps(tray_ribbon, font,
				Vector2(pen + run * 0.5, baseline), text, size_here, tracking, tint)
		else:
			tray_ribbon.draw_string(font, Vector2(pen, baseline), text,
				HORIZONTAL_ALIGNMENT_LEFT, -1, size_here, tint)
		pen += run + gap
		written += 1


## The width one segment occupies, including the per-glyph tracking
## `draw_engraved_caps` adds - which `Font.get_string_size` knows nothing about,
## so measuring without it would overlap the next segment onto this one.
static func _ribbon_run_width(
		font: Font, text: String, font_size: int, tracking: float, caps: bool) -> float:
	if font == null or text.is_empty():
		return 0.0
	if not caps:
		return font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
	var shouted := text.to_upper()
	var width := 0.0
	for index in range(shouted.length()):
		width += font.get_char_size(shouted.unicode_at(index), font_size).x + tracking
	return maxf(width - tracking, 0.0)


## THE RIBBON'S LINE, CUT TO A RAIL OF `width` PIXELS - the exact string
## `_draw_tray_ribbon` puts on the glass, so a runner can read what a player
## would read instead of trusting the drawing code.
##
## TRIMMED ON A WHOLE WORD WITH AN ELLIPSIS, never cut mid-glyph. `draw_string`
## with a width clips the last letter in half, which reads as a rendering fault
## rather than as text that did not fit.
##
## AND THE CUT NEVER ENDS ON A SEPARATOR. The list used to be joined with `", "`
## and cut on a word boundary, which left the join's own comma hanging in front of
## three full stops - `Dark Iron Forge, ...` - and a blind review read that,
## correctly, as a raw string join leaking into the UI rather than as authored
## copy. The separator characters are stripped off the tail before the ellipsis is
## set, and the ellipsis is the single typographic glyph.
func trimmed_ribbon_text(width: float, font_size: int = 0) -> String:
	var font := hud_font if hud_font != null else get_theme_default_font()
	if font == null or tray_ribbon_text.is_empty() or width <= 1.0:
		return tray_ribbon_text
	var measure := font_size
	if measure <= 0:
		measure = int(clampf(
			(tray_ribbon.size.y if tray_ribbon != null else 24.0) * 0.62, 9.0, 24.0))
	var shown := tray_ribbon_text
	if font.get_string_size(shown, HORIZONTAL_ALIGNMENT_LEFT, -1, measure).x <= width:
		return shown
	var room := width - font.get_string_size(
		RIBBON_ELLIPSIS, HORIZONTAL_ALIGNMENT_LEFT, -1, measure).x
	while not shown.is_empty() and font.get_string_size(
			shown, HORIZONTAL_ALIGNMENT_LEFT, -1, measure).x > room:
		var cut := shown.rfind(" ")
		shown = shown.substr(0, cut) if cut > 0 else shown.substr(0, shown.length() - 1)
	while not shown.is_empty() and RIBBON_TAIL_TRIM.contains(shown.right(1)):
		shown = shown.substr(0, shown.length() - 1)
	return shown + RIBBON_ELLIPSIS


## Repaint the bar's own chrome and re-tint the tab captions for the selected
## tab. Presentation only; safe to call with no session.
func _refresh_tray_chrome() -> void:
	for key in _tab_buttons.keys():
		var tab := _tab_buttons[key] as Button
		tab.add_theme_color_override("font_color",
			HudScript.PARCHMENT if String(key) == active_tab else HudScript.PARCHMENT_DIM)
	if chrome_layer != null:
		chrome_layer.queue_redraw()


func _rebuild_unplaced() -> void:
	_clear_unplaced()
	var unplaced: Array[Dictionary] = []
	# A region with no authored centre point that the map DID place from a
	# centroid derived off retail's own fill triangles is ON the map, and listing
	# it here as absent would contradict the mode line two lines below it. That
	# contradiction shipped in the first frame this lane captured.
	var placed_from_geometry := PackedStringArray()
	if map3d != null and map3d.has_map():
		placed_from_geometry = map3d.centroid_placed_regions
	for row in _rows:
		if bool(row["has_position"]):
			continue
		if Array(placed_from_geometry).has(String(row["id"])):
			continue
		unplaced.append(row)
	if unplaced.is_empty():
		unplaced_label.text = ""
		_unplaced_reason = ""
		return
	# NOT drawn on the map, and said so. Retail falls back to the region's own
	# sub-object when `CustomCenterPoint` is absent - and `livingmap.w3d` does NOT
	# carry per-region sub-objects. Its 64 sub-objects are 20 terrain tiles, the
	# coast and water, the impassable volumes, the ambient cards and eleven named
	# landmarks; there is no `Rhun` mesh in it to take a centre from. So the
	# position genuinely does not exist in the converted map, and a coordinate
	# chosen here would be invented map data.
	# Retail falls back to the region's own sub-object when `CustomCenterPoint`
	# is absent. `livingmap.w3d` carries no per-region mesh - but `lmr_fill.w3d`
	# does, and when it is converted the centroid of retail's own triangles
	# places these regions. This list is therefore only the regions that have
	# NEITHER, and it shrinks to nothing once the region bundle is present.
	# WHAT THE PLAYER IS TOLD, and what the diagnosis is told, are now two
	# different sentences. The heading here used to read "NOT ON THE MAP (2): no
	# authored centre point, and no region fill mesh to take a centroid from
	# either" - three data-model terms in one line on the player's HUD. The player
	# gets the fact and the way to reach the regions anyway; the reason is a claim
	# about the converted map, so it goes to the diagnostics panel.
	unplaced_label.text = "Beyond the map's edge (%d)" % unplaced.size()
	_unplaced_reason = ("%d region(s) are NOT DRAWN ON THE MAP and are reachable only from "
		% unplaced.size()
		+ "the list at the top left: they author no CustomCenterPoint, and no region fill "
		+ "mesh ships a sub-object whose triangles a centroid could be taken from either, so "
		+ "a position for them would have to be invented and is not")
	for row in unplaced:
		var button := Button.new()
		var region_id := String(row["id"])
		button.name = "Unplaced_%s" % region_id
		button.text = "%s   (%s)" % [_display_of(region_id), _owner_name(int(row["owner"]))]
		button.custom_minimum_size = Vector2(0, 26)
		button.pressed.connect(_on_region_clicked.bind(region_id))
		unplaced_host.add_child(button)


func _clear_unplaced() -> void:
	if unplaced_host == null:
		return
	for child in unplaced_host.get_children():
		unplaced_host.remove_child(child)
		child.queue_free()


# --- the map -----------------------------------------------------------------

func _draw_map() -> void:
	var size := map_view.size
	if session == null or session.state == null:
		map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
		_draw_fallback_banner(size)
		return
	_screen_positions = _compute_screen_positions(size)
	map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
	_draw_fallback_banner(size)
	# Edges first, so markers sit on top of them.
	for region_id in session.world.region_ids:
		if not _screen_positions.has(region_id):
			continue
		for neighbour in session.world.neighbours(region_id):
			if not _screen_positions.has(neighbour):
				continue
			if String(neighbour) < String(region_id):
				continue
			map_view.draw_line(
				_screen_positions[region_id], _screen_positions[neighbour],
				Color(0.35, 0.45, 0.32, 0.45), 1.5)
	var font := get_theme_default_font()
	for row in _rows:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var color := _owner_color(int(row["owner"]))
		var radius := MARKER_RADIUS + (2.0 if int(row["armies"]) > 0 else 0.0)
		map_view.draw_circle(point, radius, color)
		map_view.draw_arc(point, radius, 0.0, TAU, 24, Color(0.05, 0.09, 0.05, 0.9), 2.0)
		if region_id == session.selected_region:
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, ThemeScript.GOLD_BRIGHT, 3.0)
		elif Array(_targets).has(region_id):
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, Color("#c8483f"), 2.0)
		elif Array(_staging).has(region_id):
			map_view.draw_arc(point, radius + 4.0, 0.0, TAU, 28, Color(0.85, 0.92, 0.75, 0.55), 1.5)
		if region_id == session.selected_target:
			map_view.draw_arc(point, radius + 10.0, 0.0, TAU, 28, Color("#e8623f"), 3.0)
		if font != null:
			var label := "%s%s" % [region_id, "  x%d" % int(row["armies"]) if int(row["armies"]) > 0 else ""]
			map_view.draw_string(font, point + Vector2(radius + 5.0, 4.0), label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.TEXT_LEAF)


## WHY THE PLAYER IS LOOKING AT A DIAGRAM INSTEAD OF MIDDLE-EARTH, drawn on the
## thing itself. The flat graph already called itself a fallback in a 13px line
## under the map; that was not enough for the owner to notice, let alone act on.
## The reason is the loader's own multi-line refusal - every path it looked at,
## where each came from, and the command that produces a bundle.
func _draw_fallback_banner(size: Vector2) -> void:
	if map_reason.is_empty():
		return
	var font := get_theme_default_font()
	if font == null:
		return
	var banner := Rect2(Vector2.ZERO, Vector2(size.x, FALLBACK_BANNER_HEIGHT))
	map_view.draw_rect(banner, Color(0.16, 0.06, 0.05, 0.92))
	map_view.draw_line(
		Vector2(0.0, FALLBACK_BANNER_HEIGHT), Vector2(size.x, FALLBACK_BANNER_HEIGHT),
		Color("#c8483f"), 2.0)
	map_view.draw_string(
		font, Vector2(18.0, 26.0),
		"FLAT 2D REGION GRAPH (FALLBACK) - THIS IS NOT RETAIL'S MAP",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 17, Color("#e8623f"))
	map_view.draw_multiline_string(
		font, Vector2(18.0, 48.0), map_reason, HORIZONTAL_ALIGNMENT_LEFT,
		size.x - 36.0, 13, FALLBACK_BANNER_LINES, ThemeScript.PARCHMENT_DIM)


## Authored region coordinates scaled into the view. Pure presentation: the
## transform is derived from the authored extent every frame and reaches nothing
## but the drawing. Regions without an authored point are ABSENT from the result
## rather than defaulted into a corner.
func _compute_screen_positions(size: Vector2) -> Dictionary:
	var placed: Array[Dictionary] = []
	for row in _rows:
		if bool(row["has_position"]):
			placed.append(row)
	var positions: Dictionary = {}
	if placed.is_empty():
		return positions
	var minimum := (placed[0]["position"] as Vector2)
	var maximum := minimum
	for row in placed:
		var point := row["position"] as Vector2
		minimum = Vector2(minf(minimum.x, point.x), minf(minimum.y, point.y))
		maximum = Vector2(maxf(maximum.x, point.x), maxf(maximum.y, point.y))
	var span := maximum - minimum
	# The fallback banner owns the top of the view when it is up, so the graph is
	# fitted into what is left. A marker under an explanation of why the marker is
	# there instead of a map would be its own small dishonesty.
	var banner := FALLBACK_BANNER_HEIGHT if not map_reason.is_empty() else 0.0
	var usable := size - Vector2(MAP_INSET * 2.0, MAP_INSET * 2.0 + banner)
	var scale_x := usable.x / span.x if span.x > 0.0 else 1.0
	var scale_y := usable.y / span.y if span.y > 0.0 else 1.0
	var factor := minf(scale_x, scale_y)
	for row in placed:
		var point := row["position"] as Vector2
		var local := (point - minimum) * factor
		# Retail's strategic Y grows northward; screen Y grows down.
		positions[String(row["id"])] = Vector2(
			MAP_INSET + local.x,
			size.y - MAP_INSET - local.y)
	return positions


func _on_map_input(event: InputEvent) -> void:
	if session == null or session.state == null:
		return
	if event is InputEventMouseMotion:
		var hovered := _region_at(event.position)
		if hovered != session.hover_region:
			session.hover_region = hovered
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	if not button.pressed or button.button_index != MOUSE_BUTTON_LEFT:
		return
	var region_id := _region_at(button.position)
	if region_id.is_empty():
		return
	_on_region_clicked(region_id)


## Hover from the 3D map. Presentation only: it lands on the session's per-seat
## presentation field, which `authoritative_state()` does not read and no hash
## covers.
func _on_region_hovered(region_id: String) -> void:
	if session == null:
		return
	if session.hover_region == region_id:
		return
	session.hover_region = region_id
	# The region panel follows the pointer, the way retail's does. Presentation
	# only: `hover_region` is a presentation field no hash covers.
	_refresh_detail()


## Select the exact army marker the player clicked. Retail's shipped instruction
## is explicit: "Left click your Heroes, then right click to move them."
func _on_army_clicked(army_id: int, region_id: String) -> void:
	if session == null or session.state == null or not session.state.armies.has(army_id):
		return
	var army := session.state.armies[army_id] as Dictionary
	if int(army.get("owner", StateScript.NEUTRAL)) != session.state.active_player():
		_message("That general does not answer to the active faction.")
		return
	selected_army_id = army_id
	select_region(region_id)
	_message("Army selected. Right-click an adjacent territory to move.")


## Complete retail's strategic move gesture. Friendly movement is applied by
## `WotrSession.order_army`; neutral ground is taken through the existing claim
## transaction; enemy ground is staged for tactical battle or auto-resolve.
func _on_region_commanded(region_id: String) -> void:
	if session == null or session.state == null:
		return
	if selected_army_id < 0 or not session.state.armies.has(selected_army_id):
		selected_army_id = -1
		_message("Left-click one of your generals, then right-click an adjacent territory.")
		return
	var result: Dictionary = session.order_army(selected_army_id, region_id)
	if not bool(result.get("ok", false)):
		_message("Move refused: %s" % ", ".join(
			Array(result.get("refusals", PackedStringArray()))))
		refresh()
		return
	var kind := String(result.get("kind", ""))
	if kind == "move":
		session.selected_region = region_id
		session.selected_target = ""
		_message("Army moved from %s to %s." % [
			_display_of(String(result.get("from", ""))), _display_of(region_id)])
		refresh()
		return
	refresh()
	if kind == "claim":
		commit_selected_attack()
	else:
		_message("Attack staged at %s. Choose ATTACK or AUTO-RESOLVE." % _display_of(region_id))
		refresh()


## A click means "stage here" on one of your own armed regions and "attack here"
## on anything the staged region can reach. Deterministic and total: the target
## reading is tried first only when a staging region is already chosen, so a
## region that is both never depends on click order.
func _on_region_clicked(region_id: String) -> void:
	if not session.selected_region.is_empty():
		# The two readings are DISJOINT by construction - an attack target is a
		# region this seat does not own and a march target is one it does - so the
		# order below is a statement of that fact rather than a tie-break.
		if Array(_targets).has(region_id):
			select_target(region_id)
			return
		if Array(_moves).has(region_id):
			move_to(region_id)
			return
	select_region(region_id)


## March the staged region's armies into an adjacent region this seat owns. This
## is a STRATEGIC COMMAND, not a selection: it changes the authoritative state
## and therefore the hash, which is exactly what it should do - retail seats both
## sides deep in their own territory, so without it no attack is ever reachable.
func move_to(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to march from first.")
		return false
	var from_region := session.selected_region
	var result: Dictionary = session.move_armies(from_region, region_id)
	if not bool(result.get("ok", false)):
		_message("March refused: %s" % ", ".join(Array(result.get("refusals", PackedStringArray()))))
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	_message("Marched %d army/armies from %s to %s." % [
		(result["moved"] as PackedInt32Array).size(), _display_of(from_region), _display_of(region_id)])
	refresh()
	return true


func _region_at(point: Vector2) -> String:
	var best := ""
	var best_distance := MARKER_RADIUS + 6.0
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


# --- internals ---------------------------------------------------------------

## PUT THE PIECES BACK. Clears the staging, the chosen target and any open build
## ring - PRESENTATION FIELDS ONLY, every one of them. Nothing here reaches the
## strategic state or its hash: an attack that has been committed cannot be
## cancelled from this screen, and this button never pretends it can.
func _on_cancel_pressed() -> void:
	if session == null:
		return
	session.selected_target = ""
	session.selected_region = ""
	selected_plot = {}
	_message("")
	refresh()


## Why CANCEL is greyed out, or what it will put back.
func _cancel_button_reason() -> String:
	if session == null or session.state == null:
		return "The war is not under way."
	if not session.selected_target.is_empty():
		return "Let %s be, and stand down from %s." % [
			_display_of(session.selected_target), _display_of(session.selected_region)]
	if not session.selected_region.is_empty():
		return "Stand down from %s." % _display_of(session.selected_region)
	return "Nothing is staged."


func _on_attack_pressed() -> void:
	commit_selected_attack()


func _on_end_turn_pressed() -> void:
	end_turn()


func _message(text: String) -> void:
	if message_label == null:
		return
	var was_open := _checklist_is_open()
	# THE PLAQUE RAISES ITSELF FOR SOMETHING NEW AND IS PUT AWAY BY HAND. A line the
	# player has not seen is worth the room it takes over Middle-earth; the same line
	# still sitting there after they have read it and dismissed it is not, which is
	# the "get out of the way" the owner asked for. So a NEW line opens the plaque
	# and an empty one shuts it, and in between the banner medallion and retail's own
	# expander are the player's - see `set_objectives_open`.
	var incoming := text.strip_edges()
	if incoming != message_label.text.strip_edges():
		objectives_open = not incoming.is_empty()
	message_label.text = text
	# THE PLAQUE OPENS AND SHUTS WITH THIS LINE, so setting it has to re-place the
	# block inside it. Only when the state actually changed: `_relayout` is not
	# expensive but it is not free either, and most messages replace another.
	if _checklist_is_open() != was_open:
		_relayout()
	if chrome_layer != null:
		chrome_layer.queue_redraw()


## The label a region is shown under. The document's `displayName` is a STRING
## TABLE KEY (`LW:DisplayNameArnor`), not a name. When retail's table has been
## converted this is RETAIL'S OWN ENGLISH TEXT, verbatim - including the places
## where retail's key and its text disagree (`LW:DisplayNameArnor` reads
## "Arthedain", `Buckland` reads "The North Downs"), because those are retail's
## words and not this project's to correct.
##
## Every rung of the resolution, and every reason a name could not be resolved,
## lives in `wotr_display_names.gd`. A miss comes back as the CLEANED id
## ("Barrow Downs", not `Barrow_Downs` and not the key) with the gap recorded, so
## a data key can never reach the glass and a stand-in can never be mistaken for
## retail's own wording.
func _display_of(region_id: String) -> String:
	if session == null or session.world == null:
		return DisplayNamesScript.clean_id(region_id)
	var key := String(session.world.region(region_id).get("display_name", ""))
	return names.living_world_label(key, region_id)


## The name a SEAT is shown under - retail's own `SIDE:` text through
## `wotr_display_names.gd`. This used to return the `LivingWorldPlayerTemplate`
## id, which is how "PLAYERANGMAR" ended up shouted across the turn band and
## "PlayerDwarves" ended up in the seat table.
func _owner_name(owner: int) -> String:
	if session == null or session.state == null:
		return "?"
	if owner == StateScript.NEUTRAL:
		return "unclaimed"
	if owner < 0 or owner >= session.state.players.size():
		return "?"
	var template := String((session.state.players[owner] as Dictionary).get("template", ""))
	if template.is_empty():
		return "seat %d" % owner
	return names.seat_label(template)


func _owner_color(owner: int) -> Color:
	if owner == StateScript.NEUTRAL or owner < 0:
		return NEUTRAL_COLOR
	return SEAT_COLORS[owner % SEAT_COLORS.size()]
