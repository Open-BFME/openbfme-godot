extends SceneTree

## SCREENSHOT THE WAR OF THE RING SCREEN, so a claim about how it LOOKS can be
## checked by looking at it.
##
## Everything else in this lane runs headless, and headless Godot does not
## render: it can prove the bundle parsed, the meshes were instanced and the
## regions were placed, and it cannot prove Middle-earth is on the screen. That
## gap is exactly where "the 3D map is a quarter of the panel" and "regions are
## coloured dots" both survived review. So this runner opens a real window,
## drives the screen the way the menu does, lets the renderer settle, and writes
## PNGs.
##
## It asserts nothing ABOUT THE PICTURE. It is a camera, not a test, and it says
## so rather than reporting a pass that would mean nothing. It does fail hard on
## two things that make a picture WORTHLESS AS EVIDENCE - the window not being the
## shape the step asked for, and (new this round) the screen not being mounted the
## way the player gets it.
##
## ------------------------------------------------------------------------------
## IT PHOTOGRAPHS THE SCREEN THROUGH THE MENU, BECAUSE THAT IS WHAT A PLAYER GETS
## ------------------------------------------------------------------------------
##
## This runner used to do `ScreenScript.new()`, anchor it full-rect in its own bare
## window, and photograph that. Seven rounds of visual review were run on those
## pictures. Every one of them was wrong about the only thing that mattered: the
## MENU was seating the same screen in the SOLO PLAY flyout's rectangle, so what
## the owner actually played was an inset panel with the shell's backdrop, the
## "OPEN BFME / THE BATTLE FOR MIDDLE-EARTH II" masthead, the version corner and
## the "Open source engine" line around it. The capture set could not show that,
## because the capture set never went through the menu.
##
## So it goes through the menu now: `scenes/boot.tscn` is instantiated, the shell
## navigates to War of the Ring exactly as a player's clicks would
## (`show_page("wotr")`, which seats the session and configures the screen), and
## the thing photographed is `menu.wotr_screen` in the rectangle THE MENU gave it.
## A run that cannot mount it that way FAILS rather than quietly falling back to a
## standalone screen - a fallback here is precisely how a capture set comes to be
## read as evidence for something it is not a picture of.
##
## THIS RUNNER OPENS ITS WINDOW WHERE THE OWNER CAN SEE IT.
##
## THAT IS A REVERSAL, and the reason is recorded because the previous reasoning is
## still readable in this project's history and was not stupid. This runner used to
## default its window to (6000, 6000) - beyond any plausible desktop - so a capture
## run could never land on top of somebody's work. What that actually produced was
## Godot windows sitting in the owner's taskbar that they could not see, for runs
## they wanted to WATCH: the whole point of a camera run is that a person looks at
## what it photographed, and hiding the subject from the one person who has to judge
## it was the wrong trade.
##
## So the default is now ON-SCREEN at `ONSCREEN_POSITION`, near the top-left of the
## primary monitor, and `--at <x>,<y>` moves it. What is KEPT from the old
## arrangement is the half that was always right and is orthogonal to visibility:
##
##   * `WINDOW_FLAG_NO_FOCUS`. A visible window still must not take the keyboard
##     from whatever the owner is typing into. Visibility and focus are different
##     properties and only one of them was ever the problem.
##   * The position is re-asserted every frame, because the shell centres the
##     window when it applies the stored display settings and a compositor may
##     re-centre on resize. A window that wanders is as unwatchable as a hidden one.
##
## Usage:
##   Godot_v4.7 --path game \
##       --script tests/wotr_capture_runner.gd -- --out <dir> [--at 80,80]
## with `OPENBFME_LIVING_WORLD_DOC`, `OPENBFME_LIVING_MAP` and
## `OPENBFME_LIVING_MAP_REGIONS` pointing at the document and bundles.
##
## SET `OPENBFME_STRATEGIC_UI` TOO, or the pictures are of the wrong screen.
## Without it the strategic APT bundle does not load, and the HUD falls back to
## its own drawn plates: the palantir is an empty ring, the tray's card rail and
## its TERRITORY / ARMIES / STRUCTURES captions are absent, and the command dial
## is not drawn at all. That is a legitimate state and worth a picture ONCE - it
## is what a machine with no converted bundle gets - but a review set taken in it
## is a review of the fallback, not of the screen. The bundle in this checkout is
## `.private/retail-work/strategic-ui`.
## Do NOT pass `--position`: this runner places its own window, and an engine-level
## `--position` fights the per-frame re-assert.

const SHELL_SCENE_PATH := "res://scenes/boot.tscn"
## Idle frames given to the shell between "added to the tree" and "navigate to War
## of the Ring". The menu defers its boot settings and its skirmish availability
## sweep across the first few idle frames (see `main_menu.gd`
## `SKIRMISH_SWEEP_FIRST_FRAME`), and navigating into the middle of that would
## photograph a shell that is still assembling itself.
const SHELL_SETTLE_FRAMES := 12

## THE SHUTTER OPENS ON A CONDITION, NOT ON A COUNT.
##
## This runner used to wait a fixed 45 frames after asking for a window size and
## then photograph whatever was on screen. That is not a settle guarantee, and the
## evidence that it was not is four rounds deep: this lane reported "the window
## manager handed back the desktop size"; the setup lane had to render through a
## `SubViewport` because "the request was clamped by the compositor"; the map lane
## found shots 01-07 photographing a stale layout; and a blind reviewer judged a
## 2.29:1 frame and correctly said the UI had not reflowed for it. Every one of
## those is what a shot taken MID-RESIZE looks like. Measured on this machine, the
## engine spends ~12.6 seconds before its first frame and ~9 ms per frame after,
## so 45 frames is under a second of a budget that is mostly not frames at all.
##
## So the wait is now three conditions, all of which must hold together:
##
##   1. A minimum quiet period since the action (`SETTLE_MIN_FRAMES`), because the
##      3D map draws into a `SubViewport` whose first frames are empty.
##   2. The window size has been UNCHANGED for `WINDOW_STABLE_FRAMES` consecutive
##      frames - a compositor that animates a resize passes through many sizes.
##   3. That stable size IS the size the step asked for, on the window, on the
##      root viewport and on the screen the layout is computed from.
##
## A step that never converges inside `SETTLE_FRAME_CAP` is a HARD FAILURE: the
## picture is still written so the set stays complete, the caveat is printed on
## it, and the runner exits non-zero. A screenshot of the wrong window shape has
## been feeding this project's visual review for several rounds, and a quiet PNG
## is exactly how that kept happening.
const SETTLE_MIN_FRAMES := 45
const WINDOW_STABLE_FRAMES := 12
## ~15 seconds at 60 fps, and this runner's own measured per-frame cost is 9 ms.
const SETTLE_FRAME_CAP := 900
## Frames between "the size converged, relayout" and the shutter, so the map's
## SubViewport has redrawn at the new size before it is photographed.
const SHUTTER_DELAY_FRAMES := 10
const WINDOW_SIZE := Vector2i(1860, 800)

## WHERE THE WINDOW GOES WHEN NOBODY SAYS. On the primary monitor, clear of the
## top-left corner so the title bar and the taskbar do not fight over it. See the
## header for why this is on-screen rather than at (6000, 6000) as it used to be:
## the owner has to be able to WATCH a camera run, and a window they cannot see is
## a window they cannot judge. `--at <x>,<y>` moves it; `WINDOW_FLAG_NO_FOCUS`
## keeps it from stealing the keyboard whether it is visible or not.
const ONSCREEN_POSITION := Vector2i(80, 80)

## ------------------------------------------------------------------------------
## THE SHOT IS RENDERED INTO A STAGE, NOT INTO THE WINDOW. THAT IS A REVERSAL.
## ------------------------------------------------------------------------------
##
## Everything below this line about window sizes was written on the belief - stated
## at `_window_size`, and true on the machine it was measured on - that
## `root.size` is the render size and the compositor's outer window is a separate,
## harmless thing. On THIS machine it is not true, and the run that proved it is
## worth writing down rather than deleting:
##
##   * The primary display is 2048x1152.
##   * Asking for 1860x800 settled `root.size` at 1860x558. Asking for 2560x1440
##     settled it at 2560x1198. Asking for 1100x700 settled it at 1100x458.
##     Every one of them is exactly 242 short in height, which is a compositor
##     reserving chrome and a taskbar, and NONE of them is the size asked for.
##   * Fourteen of twenty-one shots failed this runner's own size check, which is
##     the check working. The pictures were also visibly TORN - the readback
##     caught the frame mid-resize while two other Godot processes were competing
##     for the same display - so they were not merely the wrong shape, they were
##     not pictures of one layout at all.
##
## A capture set whose size is a property of whichever monitor the run happened on
## cannot be compared against retail's 2560x1440 oracle, and 2560x1440 is not
## reachable in a window on a 2048x1152 desktop AT ALL. So the shell is mounted
## into a `SubViewport` sized by the plan, the SubViewport's texture is what is
## saved, and the host window is small on purpose. `wotr_setup_capture_runner` made
## exactly this move for exactly this reason and its reasoning is quoted there.
##
## WHAT IS KEPT, because it was never the part that was wrong:
##   * The shell is still mounted THROUGH THE MENU and the screen is still
##     photographed in the rectangle the menu gives it. That is the discipline this
##     runner exists for and a stage does not weaken it - the shell fills the stage
##     the same way it filled the window, and the same assertion holds it there.
##   * The size check at the shutter still runs, and still fails the run. It just
##     now checks a number this runner controls instead of one it can only ask for.
##   * The host window is still on-screen and still refuses focus, so the owner can
##     watch a run. What they watch is smaller than the shot, which is the honest
##     trade: a watchable window and a comparable picture are different sizes on
##     this desktop and cannot both be the same surface.
const HOST_WINDOW_SIZE := Vector2i(960, 540)

## THE TWO WINDOWS THE LAYOUT IS ASSERTED AT AND WAS NEVER PHOTOGRAPHED AT.
##
## `wotr_region_card_runner` holds the layout to five window sizes and asserts
## two properties at the ends of that range: at 1100x700 the map must not shrink
## below its stated floor and the sidebar must give way instead, and at
## 2560x1351 - the owner's own window, which is why that odd number is in the
## runner - the map must be nearly twice the area it has at the authored size.
## Both were arithmetic only. Every shot this runner took was at 1860x800, so
## "the sidebar gives way rather than sliding over Middle-earth" and "a bigger
## window is a bigger map" were claims with no picture behind them, which is
## exactly the gap this runner exists to close.
##
## NEITHER NUMBER IS CHOSEN HERE. Both are transcribed from
## `wotr_region_card_runner.gd`, which is where they are asserted: 1100x700 from
## `the_map_never_shrinks_below_its_stated_floor`, and 2560x1351 - the owner's
## own window, which is why it is an odd number rather than a round one - from
## that runner's `SIZES` list and its
## `the_map_grows_with_the_window_rather_than_staying_at_its_authored_size`.
## Picking a different pair here would photograph a layout nobody checks.
const LAYOUT_FLOOR_WINDOW := Vector2i(1100, 700)
const OWNERS_WINDOW := Vector2i(2560, 1351)
## RETAIL'S OWN ASPECT, and the only frame the HUD is fairly judged in.
##
## The oracle capture (`reference/.../game.dat_l1eJcM0zCw.jpg`) is 2560x1440.
## Every shot this runner took was at some other aspect, and the widest of them -
## the owner's 2560x1351 - is the one a blind review was handed. It marked the
## composition down for not having reflowed to a frame retail never composed for,
## which was a fair reading of an unfair comparison. This window is 16:9 exactly,
## it is the last size in `wotr_region_card_runner.SIZES` (so the layout is
## ASSERTED at it as well as photographed at it), and the shot's own pixel size is
## printed and checked below so a window manager that hands back something else
## cannot pass the picture off as retail's aspect.
const RETAIL_ASPECT_WINDOW := Vector2i(2560, 1440)

var _out_dir := ""
## The shell, and the screen the shell owns. `_screen` stays null until the menu
## has navigated to War of the Ring; nothing is photographed before then.
var _menu: Node = null
## The stage the shell is mounted into and the shot is read back from. See
## `HOST_WINDOW_SIZE` for why this exists and what it replaced.
var _stage: SubViewport = null
var _screen: Control = null
## Idle frames since the shell was added, and whether the navigation has been done.
var _shell_frames := 0
var _mounted := false
var _shot := 0
## Whether this shot's action has already been applied. See `_process`.
var _applied := false
## Frames waited on the CURRENT shot since its action was applied.
var _waited := 0
## The window size seen last frame, and how many consecutive frames it has been
## unchanged. A compositor that animates a resize passes through many sizes; a
## shot taken during that photographs a layout computed for none of them.
var _observed := Vector2i.ZERO
var _stable := 0
## Frames left between "the size converged and the screen relaid out" and the
## shutter. -1 means the shot has not converged yet.
var _shutter := -1
## Every step whose window never became the size it asked for, or whose written
## PNG is not that size. NON-EMPTY MEANS THIS RUN FAILED and the process exits
## non-zero: a capture set with a wrong-shaped frame in it is worse than no
## capture set, because it is read as evidence.
var _broken: Array[String] = []
var _plan: Array[Dictionary] = []


func _initialize() -> void:
	_out_dir = _argument("--out", "user://wotr-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.size = HOST_WINDOW_SIZE
	window.title = "OpenBFME - War of the Ring capture"
	_keep_the_window_out_of_the_owners_way()
	# NO CONTENT STRETCH IN THE CAMERA. The project stretches canvas_items over
	# a 1920x1080 base, so below that base the canvas and the window disagree
	# and a screen laid out for the window renders into the top-left corner of
	# a larger canvas - pass5's 1100x700 shot photographed exactly that mixed
	# state. This runner's whole job is "photograph the layout at THIS pixel
	# size", so the canvas is pinned to the window, one canvas unit per pixel.
	window.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED

	# THE SHELL, NOT A BARE SCREEN. See the header: the screen is photographed in
	# the rectangle the MENU gives it, because that is the only rectangle a player
	# ever sees. Everything the screen needs - the living-world document, the
	# session, the mounted pack roots that carry the retail face - is found by the
	# menu's own code on the way in, so none of it is arranged here any more.
	var packed: Resource = load(SHELL_SCENE_PATH)
	if packed == null:
		push_error("[capture] %s failed to load; there is nothing to photograph." % SHELL_SCENE_PATH)
		quit(1)
		return
	# THE STAGE. See `HOST_WINDOW_SIZE` for why the shell is mounted into a
	# `SubViewport` instead of into the window it used to be mounted into.
	_stage = SubViewport.new()
	_stage.size = WINDOW_SIZE
	# ALWAYS, not ONCE: the plan drives the screen between shots, and a viewport
	# that updated once would photograph the opening state twenty-one times.
	_stage.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_stage.transparent_bg = false
	# The stage must take the pointer and the keyboard the way the window did, or
	# every hover, click and held key in the plan lands nowhere.
	_stage.handle_input_locally = true
	_stage.gui_embed_subwindows = true
	window.add_child(_stage)

	_menu = (packed as PackedScene).instantiate()
	_stage.add_child(_menu)
	print("[capture] shell mounted from %s into a %s stage inside a %s window; navigating to War of the Ring in %d frame(s)." % [
		SHELL_SCENE_PATH, str(_stage.size), str(Vector2i(window.size)), SHELL_SETTLE_FRAMES])

	# The shots, in order. Each is a name plus something to do to the screen
	# first, so the captures show different states rather than the same frame
	# three times.
	_plan = [
		# THE WINDOW IS RESTATED ON THE FIRST SHOT, not only on the ones that
		# change it. `_initialize` asks for `WINDOW_SIZE` before the window
		# exists, and a window manager is free to hand back something else - this
		# one hands back the desktop size. Every early shot was then a HUD laid
		# out for the window it got and a 3D map still sized for the one that was
		# asked for, which photographs as a letterboxed map inside a full-bleed
		# HUD and reads as a layout bug that is not there.
		{"name": "01-opening", "action": "", "window": WINDOW_SIZE},
		{"name": "02-region-hovered", "action": "hover"},
		{"name": "03-staged", "action": "stage"},
		# The build plots and the ring of structures around one. Retail's screen
		# is as much a builder as a map, and a capture set that never opens the
		# ring cannot show whether the ring works.
		{"name": "04-build-plot", "action": "plot"},
		# THE CAMERA. The owner asked to "zoom around the 3d map and zoom way in
		# and out like in a regular skirmish match", so the set has to show both
		# ends of the range and an angle that is not the default one - a camera
		# claim nobody photographed is a claim nobody checked.
		{"name": "05-zoomed-in", "action": "zoom_in"},
		{"name": "06-orbited", "action": "orbit"},
		{"name": "07-zoomed-out", "action": "zoom_out"},
		# THE TWO WINDOWS THE LAYOUT IS ASSERTED AT AND WAS NEVER PHOTOGRAPHED
		# AT. Both reset the camera first, so what differs between 08, 09 and 01
		# is the WINDOW and nothing else: three shots of one framing at three
		# sizes is a comparison, three shots of three cameras is not.
		{"name": "08-layout-floor", "action": "reset", "window": LAYOUT_FLOOR_WINDOW},
		{"name": "09-owners-window", "action": "reset", "window": OWNERS_WINDOW},
		# BACK TO THE AUTHORED SIZE, and this is not a spare picture: the layout
		# has just been driven down to its floor and back up again, so if a
		# control does not come back, 10 and 01 differ and the pair says so.
		{"name": "10-back-at-the-authored-size", "action": "reset", "window": WINDOW_SIZE},
		# THE DIAGNOSTICS OVERLAY, opened. The conversion report and the named
		# gaps moved off the player-facing surface onto this panel; a capture
		# set that never opens it could not show that the capability survived
		# the move.
		# The window is RESTATED here even though shot 10 already set it: the
		# OS is free to have fought the resize in between, and a diagnostics
		# shot at a size the layout was not computed for photographs a mixed
		# state that reads as a layout bug.
		{"name": "11-diagnostics-open", "action": "diagnostics", "window": WINDOW_SIZE},
		# RETAIL'S OWN 16:9 FRAME, four shots: the opening state, a region under the
		# pointer (which is what the region card is a card OF), and the bar's other
		# two tabs. The comparison against the oracle is made on these.
		{"name": "12-retail-aspect", "action": "reset", "window": RETAIL_ASPECT_WINDOW},
		{"name": "13-retail-aspect-territory", "action": "tab_territory"},
		{"name": "14-retail-aspect-armies", "action": "tab_armies"},
		{"name": "15-retail-aspect-structures", "action": "tab_structures"},
		# THE PAUSE SHELL. MAIN MENU moved off the live standings panel and onto an
		# ESCAPE surface this round, and a surface nobody photographed is a surface
		# nobody checked - which is exactly how the button ended up framed inside a
		# scoreboard for two rounds running.
		# AND ONE RAISED. 15 shows the shop; this shows a purchase. Two pictures are
			# the only honest evidence that the ring is a control rather than a
			# picture of one - the treasury falls by the price, a structure stands on
			# its foundation card, and the plot counters move.
			{"name": "15b-structure-raised", "action": "build"},
			{"name": "16-pause-shell", "action": "pause"},
		# AND BACK OUT OF IT, because a modal that opens is half a modal. If the
		# shell does not close, 17 and 15 differ and the pair says so.
		{"name": "17-shell-closed", "action": "resume"},
		# THE HUD OFF AND BACK ON. F2 is the owner's "get out of my way" key, and a
		# key that hides the HUD is only believable as a pair of pictures: 18 must be
		# Middle-earth and nothing else, and 19 must be indistinguishable from 17.
		{"name": "18-hud-hidden", "action": "hide_hud"},
		{"name": "19-hud-restored", "action": "show_hud"},
		# A HELD PAN KEY, FROM THE OPENING FRAMING. Every camera shot in this set
		# (05, 06, 07) drives the wheel or writes an orbit; not one of them holds a
		# key. That blindness hid a real defect: with the view opening AT the zoom
		# ceiling, one tap of D moved the camera where the cut-edge rule could not
		# frame from, so the rule pulled the zoom in behind it and a second of held D
		# left a single province filling the screen. A shot taken after a HELD key is
		# the only one that could have shown it, so the set now takes one.
		#
		# The key goes down in the action and comes up in the NEXT step's, so it is
		# held across this step's whole settle period - `SETTLE_MIN_FRAMES` frames at
		# minimum, which is the ~1s the defect needed.
		{"name": "20-panned-with-a-held-key", "action": "pan_hold", "window": WINDOW_SIZE},
		{"name": "21-after-the-pan-key-is-released", "action": "pan_release"},
		# THE VIEW STOPS, AT RETAIL'S OWN ASPECT. F2 stopped being a switch this
		# round (see `wotr_screen.gd:VIEW_FULL`), and "the HUD hides in three
		# amounts" is a claim that is worth exactly as much as its pictures. 22 is
		# the FOCUSED stop - the one a player plays in - and it has to be visibly
		# less chrome than 12 and visibly more than 18, or the middle stop is a
		# rename rather than a mode.
		{"name": "22-focused-view", "action": "view_focused", "window": RETAIL_ASPECT_WINDOW},
		# AND THE OBJECTIVES PLAQUE OPEN, which is the per-panel half of the same
		# answer: the stops take whole islands off, the plaque's own expander and the
		# palantir's banner medallion take one panel off. A capture set that only
		# ever showed the plaque in one state could not show it had two.
		{"name": "23-objectives-open", "action": "objectives_open"},
		{"name": "24-objectives-shut", "action": "objectives_shut"},
	]
	print("[capture] writing to %s" % _out_dir)


func _process(_delta: float) -> bool:
	# THE WINDOW IS DRAGGED BACK OFF-SCREEN EVERY FRAME while a shot is settling.
	# The shell applies the STORED display settings on a deferred call during its
	# first idle frames (`main_menu.gd:_apply_boot_settings`), and the windowed
	# branch of that applier CENTRES the window on the desktop - which would walk
	# this run straight onto the owner's display. Re-asserting is cheaper than
	# special-casing, and it covers a compositor that re-centres on resize too.
	_keep_the_window_out_of_the_owners_way(true)

	if not _mounted:
		_shell_frames += 1
		if _shell_frames < SHELL_SETTLE_FRAMES:
			return false
		_mount_through_the_menu()
		return false

	if _shot >= _plan.size():
		if _broken.is_empty():
			print("[capture] wrote %d image(s), every one of them at the size its step asked for." % _plan.size())
			quit(0)
		else:
			push_error("[capture] %d of %d shot(s) were NOT at the size their step asked for: %s" % [
				_broken.size(), _plan.size(), "; ".join(_broken)])
			print("[capture] FAILED: %d shot(s) are pictures of a window shape nobody asked for - %s" % [
				_broken.size(), "; ".join(_broken)])
			quit(1)
		return true
	var step := _plan[_shot]

	# THE ACTION IS APPLIED A WHOLE SETTLE PERIOD BEFORE THE SHOT. `_process` runs
	# before the frame is drawn, so `root.get_texture()` here still holds the
	# PREVIOUS frame - applying and capturing in one visit photographed the state
	# the screen was in before the action, and every shot in the first capture set
	# this lane took was one step stale because of it. `_applied` makes the two
	# halves separate visits.
	if not _applied:
		# THE WINDOW FIRST, then the action. A resize relayouts the screen and
		# re-fits the camera, so doing it after would photograph a camera fitted
		# to the previous panel.
		if step.has("window"):
			_ask_for_window(step["window"] as Vector2i)
		_apply(String(step["action"]))
		_applied = true
		_waited = 0
		_stable = 0
		_shutter = -1
		_observed = _window_size()
		return false

	# THE SETTLE, AS A CONDITION. See `SETTLE_MIN_FRAMES` for why this is not a
	# frame count any more.
	if _shutter < 0:
		_waited += 1
		var now := _window_size()
		if now == _observed:
			_stable += 1
		else:
			_observed = now
			_stable = 0
		var wanted_size: Vector2i = step["window"] as Vector2i if step.has("window") 			else _observed
		# THE STAGE, AND THE SCREEN INSIDE IT. `root.size` is NOT checked any more:
		# It is the host window's, which is deliberately a different (small) size now -
		# see `HOST_WINDOW_SIZE`. What has to agree is the surface the picture is read
		# back from and the surface the layout was computed for.
		var right_size := now == wanted_size 			and (_screen == null or Vector2i(_screen.size) == wanted_size)
		if _stable >= WINDOW_STABLE_FRAMES and _waited >= SETTLE_MIN_FRAMES and right_size:
			# CONVERGED. Relayout once at the size that actually arrived, then give
			# the map's own SubViewport a few frames to redraw into it.
			if _screen != null:
				# The screen is anchored to the window and has already followed it; the
				# relayout is asked for so the geometry report below describes the frame
				# the picture is actually taken at.
				_screen._relayout()
				# WHERE EVERY ISLAND AND EVERY CONTROL ACTUALLY LANDED, at the size
				# the picture is actually taken at. Reporting it before the window
				# had settled - which is what this runner used to do - described a
				# layout that was never photographed.
				_report_geometry(String(step["name"]))
			_shutter = SHUTTER_DELAY_FRAMES
			return false
		if _waited >= SETTLE_FRAME_CAP:
			# NEVER CONVERGED. The picture is still taken so the set stays complete
			# and the caveat is printed on it, but the RUN fails: a quiet PNG of the
			# wrong window shape is how four rounds of visual review were misled.
			var complaint := "%s wanted %s, window settled at %s (stable %d frame(s) of %d waited)" % [
				String(step["name"]), str(wanted_size), str(now), _stable, _waited]
			_broken.append(complaint)
			push_error("[capture] %s" % complaint)
			print("[capture] WINDOW NEVER CONVERGED - %s" % complaint)
			if _screen != null:
				# The screen is anchored to the window and has already followed it; the
				# relayout is asked for so the geometry report below describes the frame
				# the picture is actually taken at.
				_screen._relayout()
				_report_geometry(String(step["name"]))
			_shutter = SHUTTER_DELAY_FRAMES
			return false
		# Re-assert the request about once a second while waiting; some compositors
		# drop a resize request made while another is in flight.
		if step.has("window") and _waited % 60 == 0:
			print("[capture] %s: window is %s, want %s - re-asserting after %d frame(s)" % [
				String(step["name"]), str(now), str(step["window"]), _waited])
			_ask_for_window(step["window"] as Vector2i)
		return false

	if _shutter > 0:
		_shutter -= 1
		return false

	_applied = false
	_shutter = -1
	# THE STAGE'S OWN TEXTURE, not the window's. See `HOST_WINDOW_SIZE`.
	var image: Image = _stage.get_texture().get_image()
	var path: String = _out_dir.path_join("%s.png" % String(step["name"]))
	var error := image.save_png(path)
	if error != OK:
		push_error("[capture] could not write %s (error %d)" % [path, error])
	else:
		# THE WRITTEN PIXELS ARE THE CLAIM, so they are what is checked - not the
		# window, not the viewport. A shot whose PNG is not the requested size is a
		# picture of a layout nobody designed, and the previous set was judged
		# against retail's 16:9 while being 2.29:1.
		var got := Vector2i(image.get_width(), image.get_height())
		var wanted_pixels: Vector2i = step["window"] as Vector2i if step.has("window") else got
		var aspect := float(got.x) / maxf(float(got.y), 1.0)
		if got != wanted_pixels:
			var complaint := "%s is %s, its step asked for %s" % [
				String(step["name"]), str(got), str(wanted_pixels)]
			if not _broken.has(complaint):
				_broken.append(complaint)
			push_error("[capture] %s" % complaint)
		print("[capture] %s  %dx%d  aspect %.3f%s" % [
			path, got.x, got.y, aspect,
			"" if got == wanted_pixels
				else "  WRONG SIZE - the step asked for %s" % str(wanted_pixels)])
	_shot += 1
	return false


## PUT THE WINDOW WHERE IT CANNOT BE SEEN AND STOP IT TAKING THE KEYBOARD.
##
## Both halves matter and they fail independently, which is why both are here (see
## the header). `--at <x>,<y>` on the command line overrides the position for
## someone who deliberately wants to watch a run; there is no override for the
## focus flags, because "let this steal focus" is never a thing worth asking for.
##
## Every call is attempted and reported rather than assumed: `WINDOW_FLAG_NO_FOCUS`
## is honoured on Windows and X11 and `WINDOW_FLAG_ALWAYS_ON_BOTTOM` is not
## honoured everywhere, so what is actually in force is printed. A run that could
## not get the flags is still a run - it just says so, instead of quietly
## interrupting somebody.
func _keep_the_window_out_of_the_owners_way(quiet := false) -> void:
	var wanted := _wanted_window_position()
	# `WINDOW_FLAG_NO_FOCUS` is the one that matters and the one every desktop
	# platform Godot 4 supports honours. There is no always-on-bottom flag in this
	# engine version, so the off-screen position is what keeps the window out of
	# sight and this flag is what keeps it out of the keyboard.
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS, true)
	root.set_flag(Window.FLAG_NO_FOCUS, true)
	if DisplayServer.window_get_position() != wanted:
		DisplayServer.window_set_position(wanted)
	if quiet:
		return
	print("[capture] window at %s (asked for %s), no-focus %s" % [
		str(DisplayServer.window_get_position()), str(wanted),
		DisplayServer.window_get_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS)])


## Hold or release the pan key the way a keyboard does. `Input.parse_input_event`
## feeds the input singleton, which is what `Input.is_key_pressed` reads, so the
## map's own `_process` sees a genuinely held key rather than a one-frame event.
func _press_pan_key(down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = KEY_D
	event.physical_keycode = KEY_D
	event.pressed = down
	Input.parse_input_event(event)


func _wanted_window_position() -> Vector2i:
	var override := _argument("--at", "")
	if override.is_empty():
		return ONSCREEN_POSITION
	var parts := override.split(",", false)
	if parts.size() == 2 and parts[0].is_valid_int() and parts[1].is_valid_int():
		return Vector2i(int(parts[0]), int(parts[1]))
	return ONSCREEN_POSITION


## NAVIGATE THE SHELL THE WAY A PLAYER'S CLICKS WOULD, and photograph what that
## leaves on the glass.
##
## `show_page("wotr")` is the shell's own public entry: it compiles the strategic
## screen, seats a session on whatever living-world document the content layer
## found, configures the screen with the mounted pack roots (which is where the
## retail face comes from), and shows the page. There is no second route into the
## strategic layer and this runner deliberately does not invent one.
##
## A REFUSAL IS A HARD FAILURE HERE. The shell states why War of the Ring is
## unavailable and that sentence is printed - but a capture set of a screen that
## was never mounted is worse than no capture set, so the run exits non-zero rather
## than photographing seventeen pictures of a main menu.
func _mount_through_the_menu() -> void:
	_mounted = true
	if _menu == null or not _menu.has_method("show_page"):
		_broken.append("the shell did not instantiate, so nothing could be mounted")
		push_error("[capture] the shell did not instantiate.")
		return
	if not bool(_menu.call("show_page", "wotr")):
		var reason := String(_menu.call("wotr_unavailable_reason")) \
			if _menu.has_method("wotr_unavailable_reason") else "<no reason given>"
		_broken.append("the shell refused to open War of the Ring: %s" % reason)
		push_error("[capture] the shell refused to open War of the Ring: %s" % reason)
		print("[capture] NOTHING WAS PHOTOGRAPHED. %s" % reason)
		return
	_screen = _menu.get("wotr_screen") as Control
	if _screen == null:
		_broken.append("the shell reported War of the Ring open but owns no strategic screen")
		push_error("[capture] the shell owns no strategic screen.")
		return
	# WHAT RECTANGLE THE MENU ACTUALLY GAVE IT, printed on every run. This is the
	# number that was wrong for seven rounds and that no picture could show.
	print("[capture] mounted through the menu: page=%s screen rect %s in a %s window; shell chrome hidden: %s" % [
		String(_menu.call("get_current_page")),
		str(Rect2(_screen.get_global_rect())), str(_window_size()),
		str(_menu.call("shell_chrome_is_hidden")) if _menu.has_method("shell_chrome_is_hidden")
			else "<the shell cannot say>"])
	if not Rect2(Vector2.ZERO, Vector2(_window_size())).is_equal_approx(Rect2(_screen.get_global_rect())):
		var complaint := "the menu seated the strategic screen at %s inside a %s window - the capture set is NOT a picture of a full-window game" % [
			str(Rect2(_screen.get_global_rect())), str(_window_size())]
		_broken.append(complaint)
		push_error("[capture] %s" % complaint)

	# EDGE SCROLL OFF FOR THE WHOLE RUN, and this is a measurement rule rather than
	# a claim about the feature.
	#
	# The map's edge scroll reads the REAL pointer, and this runner's window is on
	# the owner's screen with the owner's mouse wherever the owner left it. Round
	# 7's whole capture set is the consequence: a pointer resting inside the 1.4%
	# edge band scrolled the camera for every one of the ~24 settle frames between
	# shots, so shot 01 frames Middle-earth and shot 02 - taken seconds later, with
	# NO camera action between them - is a bay coastline. Twenty-three of
	# twenty-five pictures were of a camera nobody asked for, and two rounds of
	# visual review were run on them.
	#
	# The defect that made it catastrophic is fixed in the view (see
	# `wotr_map_view.gd:pan_by_axis` - the cut-edge lean no longer answers a
	# passive edge scroll, and what it spends comes back). This stays off anyway:
	# a capture must be a picture of the state its step set, and a state that
	# depends on where somebody's mouse is resting is not one. The keyboard pan is
	# left ON, because shot 20 photographs a held key on purpose.
	if _screen.map3d != null:
		_screen.map3d.edge_scroll_enabled = false
		print("[capture] edge scroll disabled for the run: the pointer is the owner's, not this runner's")


## Ask the OS for a window size, on every surface the layout reads it from. The
## window, the root viewport and the screen are set together because they can
## disagree for a frame and the layout is computed from the last of the three.
func _ask_for_window(wanted: Vector2i) -> void:
	# THE STAGE IS SET, NOT ASKED. A `SubViewport`'s size is this process's own
	# property: it is exactly what was written, on the frame it was written, with no
	# compositor in the loop. That is the whole point of the reversal recorded at
	# `HOST_WINDOW_SIZE` - the previous version of this function ASKED an operating
	# system for a rectangle and then had to police the answer, and on this desktop
	# the answer was 242 pixels short every single time.
	if _stage == null:
		return
	_stage.size = wanted
	print("[capture] stage set to %s (host window %s, display %s)" % [
		str(_stage.size), str(DisplayServer.window_get_size()),
		str(DisplayServer.screen_get_size())])


## THE SIZE THE SHOT WILL ACTUALLY BE, which is the ROOT VIEWPORT's, not the OS
## window's - and the difference between the two is worth writing down, because
## chasing the wrong one is what this whole change is about.
##
## `root.get_texture().get_image()` renders the root VIEWPORT, and with
## `content_scale_mode` disabled (set in `_initialize`, for exactly this reason)
## that viewport is one canvas unit per pixel at `root.size`. MEASURED on this
## machine: asking for 1860x800 gives `root.size == (1860, 800)` and a 1860x800
## PNG, while `DisplayServer.window_get_size()` reports (2560, 1351) - the
## compositor's own outer window, which it refuses to shrink below its restored
## bounds and which SCALES the render into itself. Gating the shutter on the
## display server's number therefore waits forever for a size the picture never
## has, which is what the first version of this check did.
##
## The OS window's size is still reported per step by `_ask_for_window`, because a
## render size and a window size that disagree mean the visible window is scaling
## the frame - true here, harmless for a capture, and a thing a reader of this log
## should not have to rediscover.
func _window_size() -> Vector2i:
	return _stage.size if _stage != null else Vector2i(root.size)


func _apply(action: String) -> void:
	if action.is_empty() or _screen == null or _screen.session == null:
		return
	match action:
		"reset":
			# Retail's opening framing, so a window shot is about the WINDOW - and
			# with the diagnostics overlay shut, because shot 11 leaves it open and a
			# window shot with a full-screen panel over it photographs the panel.
			_screen.toggle_diagnostics(false)
			_screen.map3d.reset_camera()
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"hover":
			# Point at whatever region the strategic layer says the active seat
			# could stage from - a real region, chosen by the state, not a name
			# written into this file.
			var staging: PackedStringArray = _screen.session.staging_regions()
			if not staging.is_empty():
				_screen._on_region_hovered(staging[0])
				_screen.refresh()
		"stage":
			var staging: PackedStringArray = _screen.session.staging_regions()
			if not staging.is_empty():
				_screen.select_region(staging[0])
				var targets: PackedStringArray = _screen.session.attack_targets(staging[0])
				if not targets.is_empty():
					_screen.select_target(targets[0])
		"plot":
			# The first region THE STATE says this seat owns that authors a build
			# plot - chosen by the document, not written into this file.
			# Prefer a region the seat can actually STAGE from, because
			# `select_region` refuses anything else and the ring would then open
			# over a region the screen is not looking at.
			var owned: Array[String] = []
			for region_id in _screen.session.staging_regions():
				owned.append(String(region_id))
			for region_id in _screen.session.state.regions_owned_by(
					_screen.session.state.active_player()):
				if not owned.has(String(region_id)):
					owned.append(String(region_id))
			for region_id in owned:
				var region: Dictionary = _screen.session.world.region(String(region_id))
				if int(region.get("building_spot_count", 0)) <= 0:
					continue
				_screen.select_region(String(region_id))
				_screen._on_plot_clicked(String(region_id), 0)
				print("[capture] build ring opened on %s plot 1 of %d" % [
					String(region_id), int(region.get("building_spot_count", 0))])
				break
		"build":
			# ACTUALLY RAISE ONE. The set could show the ring OPEN and could not show
			# that pressing it does anything, which for four rounds was the owner's
			# loudest complaint ("I cannot click on the buildings or build them with
			# the icons"). This drives the SIGNAL HANDLER the map view calls when a
			# click lands on a ring slot, so what the next shot photographs is the
			# state a player's click produces: the treasury down by the price, a
			# structure standing on its card, and both counters moved.
			var built := false
			for region_id in _screen.session.state.regions_owned_by(
					_screen.session.state.active_player()):
				var free: int = _screen.session.state.free_plot(String(region_id))
				if free < 0:
					continue
				_screen.select_region(String(region_id))
				_screen._on_region_hovered(String(region_id))
				_screen._on_plot_clicked(String(region_id), free)
				for entry_value in _screen._radial_entries():
					var entry := entry_value as Dictionary
					if not bool(entry["can_build"]):
						continue
					var purse: int = _screen.session.treasure()
					_screen._on_build_entry_clicked(
						String(region_id), free, String(entry["id"]))
					print("[capture] raised %s on %s plot %d; treasury %d -> %d" % [
						String(entry["title"]), String(region_id), free,
						purse, _screen.session.treasure()])
					built = true
					break
				if built:
					break
			if not built:
				print("[capture] NOTHING WAS RAISED: %s" % _screen.session.build_reason())
			_screen._on_tab_pressed("structures")
			_screen.refresh()
		"zoom_in":
			# Onto the selected region, at the deep end of the range, so the shot
			# shows what "zoom way in" actually looks like rather than a nudge.
			_screen.map3d.focus_region(_screen.session.selected_region, 0.10)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"orbit":
			_screen.map3d.set_orbit(0.9, -24.0)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"zoom_out":
			_screen.map3d.set_orbit(0.0, -52.0)
			_screen.map3d.focus_region("", 1.3)
			print("[capture] camera %s" % str(_screen.map3d.camera_state()))
		"diagnostics":
			_screen.toggle_diagnostics(true)
			print("[capture] diagnostics overlay opened")
		"pause":
			_screen.toggle_pause_shell(true)
			print("[capture] pause shell opened; MAIN MENU at %s" % str(
				Rect2(_screen.back_button.position, _screen.back_button.size)))
		"resume":
			# Driven through the control a player would press rather than by writing
			# the field, for the same reason the tabs are.
			_screen.pause_resume.emit_signal("pressed")
			print("[capture] pause shell closed (shell visible: %s)" % str(
				_screen.pause_shell.visible))
		"pan_hold":
			# THE OPENING FRAMING FIRST, so the shot is about the held key and nothing
			# else, then D goes down and STAYS down until the next step lifts it.
			# `Input.parse_input_event` is used rather than calling a camera method
			# because the map reads `Input.is_key_pressed(...)` in its own `_process`:
			# writing a camera field would photograph a state no keypress produced.
			_screen.toggle_diagnostics(false)
			_screen.map3d.reset_camera()
			_press_pan_key(true)
			print("[capture] D held down from %s" % str(_screen.map3d.camera_state()))
		"pan_release":
			_press_pan_key(false)
			print("[capture] D released at %s" % str(_screen.map3d.camera_state()))
		"hide_hud", "show_hud":
			# Driven through the same call F2 makes, so the picture is of the binding
			# rather than of a field this runner set.
			_screen.set_hud_hidden(action == "hide_hud")
			print("[capture] HUD hidden: %s" % str(_screen.hud_hidden))
		"view_focused":
			# The MIDDLE stop, reached the way F2 reaches it rather than by writing
			# the field - a picture of a field this runner set proves nothing about
			# the key that is supposed to set it.
			_screen.toggle_diagnostics(false)
			_screen.map3d.reset_camera()
			_screen.set_view_mode(_screen.VIEW_FOCUSED)
			print("[capture] view stop %d, HUD hidden %s" % [
				_screen.view_mode, str(_screen.hud_hidden)])
		"objectives_open", "objectives_shut":
			# BACK TO THE WHOLE HUD FIRST, so 23 differs from 12 in the PLAQUE and in
			# nothing else. Driven through the same call the palantir's banner
			# medallion and retail's own plaque expander both make.
			_screen.set_view_mode(_screen.VIEW_FULL)
			_screen.set_objectives_open(action == "objectives_open")
			print("[capture] objectives plaque open: %s" % str(_screen.objectives_open))
		"tab_territory", "tab_armies", "tab_structures":
			# The bar's tabs are real controls, so they are driven the way a player
			# drives them rather than by writing the field: a capture that set the
			# field directly could not tell a live tab from a dead one.
			_screen.toggle_diagnostics(false)
			var key := action.substr(4)
			if _screen._tab_buttons.has(key):
				(_screen._tab_buttons[key] as Button).emit_signal("pressed")
			print("[capture] tab -> %s (screen says %s)" % [key, _screen.active_tab])


## The composed geometry behind one shot: retail's own HUD islands with the
## authored slot each came from, then every HUD control's actual rectangle. A
## control whose rectangle is bigger than the one the layout asked for (Godot
## clamps `size` up to a Control's own minimum) shows up here and nowhere else.
func _report_geometry(shot: String) -> void:
	var islands: Dictionary = _screen._islands
	if islands.is_empty():
		print("[capture] %s: NO retail strategic islands - the HUD is drawing its own plates." % shot)
	for slot in islands.keys():
		var island: Dictionary = islands[slot] as Dictionary
		print("[capture] %s island %-18s %-26s %s" % [
			shot, String(slot), String(island["movie"]), str(island["rect"])])
	for control in [
		_screen.header_label, _screen.turn_banner, _screen.phase_banner,
		_screen.hint_label, _screen.message_label, _screen.end_turn_button,
		_screen.standings_label, _screen.region_portrait_frame,
		_screen.region_portrait_caption, _screen.detail_label,
		_screen.attack_button, _screen.back_button, _screen.auto_resolve_button,
	]:
		var node := control as Control
		print("[capture] %s control %-22s pos %s size %s (min %s)" % [
			shot, node.name, str(node.position), str(node.size),
			str(node.get_combined_minimum_size())])


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
