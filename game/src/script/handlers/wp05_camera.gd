extends RefCounted

## WP05-camera - camera movement, zoom, pitch, shake, fades, letterbox,
## follow/tether, spline paths, skybox, UI hide/show and the render toggles that
## sit in WorldBuilder's Camera_ menu.
##
## 74 members: 67 actions, all served, and 7 conditions, all registered as
## BLOCKED for the reason set out under "THE SEVEN CONDITIONS" below.
##
##
## PRESENTATION ONLY - THE WHOLE PACKAGE
## =====================================
## Not one handler here touches the simulation. Every action goes to a
## PRESENTATION SINK (SageScriptWorld.PresentationSink) and nothing else: no
## facet, no env, no counter, no flag. That is not a simplification, it is the
## lockstep contract - see the PresentationSink comment in script_world.gd. A
## camera that could write sim state would desync peers whose local view differs,
## and every peer's view differs.
##
## The sink is also ONE-WAY. `emit()` is its entire surface; nothing is read
## back. That single fact decides both halves of this file: the actions are
## trivially serviceable, and the conditions are not serviceable at all.
##
##
## VALUES ARE FORWARDED VERBATIM - NO UNIT CONVERSION
## ==================================================
## script_world.gd's argument conventions say durations reaching a WORLD FACET
## are interpreter ticks. This package sends nothing to a facet. The sink's own
## contract is narrower and wins here:
##
##     "`values` is the decoded argument list in DECLARATION ORDER [...] the
##      signature's arguments and nothing else: no re-ordering, no defaults
##      filled in, no extra context bolted on."
##
## So MOVE_CAMERA_ALONG_SPLINE_PATH's "8.0 seconds" is emitted as 8.0, and
## CAMERA_BW_MODE_BEGIN's "30 frames" as 30. The presentation adapter owns the
## timebase because only the adapter knows its own frame rate; converting here
## would bake this interpreter's 10 ticks/second into a cutscene and there would
## be no way to tell afterwards which side had done it. A test asserting on a
## recording therefore asserts on the map's own numbers.
##
##
## ARGUMENTS ARE READ POSITIONALLY, BY DECLARED TYPE
## =================================================
## `_forward()` walks the signature by INDEX and decodes each slot with the field
## the DECLARED parameter type implies (SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM
## is the repo's own mapping). It never searches the argument list for a type.
## That distinction is the whole point: sage_scb.py stores integer AND real AND
## text for every non-position argument, so a type-search finds a plausible value
## in the wrong slot every time a signature repeats a type - and this package
## repeats types constantly:
##
##     SETUP_CAMERA(WAYPOINT, REAL, REAL, WAYPOINT)     two waypoints, split
##     CAMERA_LOOK_TOWARD_OBJECT(UNIT, REAL x5)         five interchangeable reals
##     CAMERA_FADE_ADD(REAL, REAL, INT, INT, INT)       two floats, three ints
##     CAMERA_MOD_SET_FINAL_ZOOM(REAL, PERCENT, PERCENT)
##
## Reading SETUP_CAMERA's "the waypoint argument" would silently return the
## position waypoint for the look-at waypoint and produce a camera that points at
## itself, with no error anywhere.
##
## An argument token this package's 74 signatures do not use is BAD_ARGUMENTS,
## not a text() fallback. The vocabulary tables are generated; if a regeneration
## changes a signature under this file, that must surface as a gap naming the
## token rather than as a string where a float belonged.
##
##
## THE SEVEN CONDITIONS - BLOCKED, AND WHY THAT IS THE HONEST ANSWER
## =================================================================
##     CAMERA_ENTERED_AREA, CAMERA_HIT_SPECIFIC_SPLINE_WAYPOINT,
##     CAMERA_MOVEMENT_FINISHED, CAMERA_RESET, CAMERA_ROTATE_DISTANCE,
##     CAMERA_SCROLL_DISTANCE, CAMERA_ZOOM_DISTANCE
##
## All seven ask the camera a QUESTION: where is it, has it stopped, how far has
## the player scrolled. Answering needs a read of camera state, and no such read
## exists on SageScriptWorld - deliberately. Presentation is a one-way channel by
## design, so there is no method to call and, per the convention, adding one here
## is a finding to report rather than a line to write.
##
## They are therefore declared blocked on that missing surface. The alternatives
## were both worse:
##
##   * Handlers returning WORLD_REFUSED would land in `condition_handlers` and be
##     counted by `implemented_conditions()` - coverage credit for seven
##     conditions that cannot be answered by anything.
##   * Registering nothing produces an `unimplemented` gap indistinguishable from
##     ordinary backlog, which is exactly what the blocked mechanism exists to
##     prevent.
##
## Blocked gets all seven the right outcome: a `blocked-subsystem` gap naming the
## missing world surface, false from evaluate_condition whether or not the record
## inverts it, and no coverage credit. A camera condition is never silently false.
##
## UNBLOCKING is a design decision for the world's owner, not a scripting one,
## and it is not free: a condition that reads camera state lets the LOCAL view
## gate script execution, and script execution is simulation. In a single-player
## cutscene that is what retail does; in lockstep multiplayer it is a desync. The
## surface these need is reported alongside this package.

const PACKAGE := "WP05-camera"

const Dispatch := preload("res://src/script/script_dispatch.gd")

## Actions that go to the CAMERA channel. Signatures are shown as the vocabulary
## declares them - see script_action_table.gd, which is the schema `_forward()`
## walks. All 65 are recorded under their own canonical InternalName.
const CAMERA_ACTIONS := [
	"CAMERA_ADD_SHAKER_AT",                    # WAYPOINT, REAL, REAL, REAL
	"CAMERA_BLOOM_EFFECT_BEGIN",               # -
	"CAMERA_BLOOM_EFFECT_END",                 # -
	"CAMERA_BW_MODE_BEGIN",                    # INT
	"CAMERA_BW_MODE_END",                      # -
	"CAMERA_DISABLE_SLAVE_MODE",               # -
	"CAMERA_ENABLE_SLAVE_MODE",                # -
	"CAMERA_FADE_ADD",                         # REAL, REAL, INT, INT, INT
	"CAMERA_FADE_MULTIPLY",                    # REAL, REAL, INT, INT, INT
	"CAMERA_FADE_SATURATE",                    # REAL, REAL, INT, INT, INT
	"CAMERA_FADE_SUBTRACT",                    # REAL, REAL, INT, INT, INT
	"CAMERA_FOLLOW_NAMED",                     # UNIT, BOOLEAN, REAL
	"CAMERA_LETTERBOX_BEGIN",                  # -
	"CAMERA_LETTERBOX_END",                    # -
	"CAMERA_LOOK_TOWARD_OBJECT",               # UNIT, REAL, REAL, REAL, REAL, REAL
	"CAMERA_LOOK_TOWARD_WAYPOINT",             # WAYPOINT, REAL, REAL, REAL, BOOLEAN
	"CAMERA_MOD_FINAL_LOOK_TOWARD",            # WAYPOINT
	"CAMERA_MOD_FREEZE_ANGLE",                 # -
	"CAMERA_MOD_FREEZE_TIME",                  # -
	"CAMERA_MOD_LOOK_TOWARD",                  # WAYPOINT
	"CAMERA_MOD_SET_FINAL_PITCH",              # REAL, PERCENT, PERCENT
	"CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER",   # INT
	"CAMERA_MOD_SET_FINAL_ZOOM",               # REAL, PERCENT, PERCENT
	"CAMERA_MOD_SET_ROLLING_AVERAGE",          # INT
	"CAMERA_MOTION_BLUR",                      # BOOLEAN, BOOLEAN
	"CAMERA_MOTION_BLUR_END_FOLLOW",           # -
	"CAMERA_MOTION_BLUR_FOLLOW",               # INT
	"CAMERA_MOTION_BLUR_JUMP",                 # WAYPOINT, BOOLEAN
	"CAMERA_MOVE_HOME",                        # -
	"CAMERA_REMOVE_AREA_RESTRICTION",          # -
	"CAMERA_RESTRICT_TO_AREA",                 # TRIGGER_AREA
	"CAMERA_RING_MODE_END",                    # INT
	"CAMERA_RING_MODE_START",                  # INT
	"CAMERA_SET_AUDIBLE_DISTANCE",             # REAL      (see CHANNEL ROUTING)
	"CAMERA_SET_DEFAULT",                      # REAL, REAL, REAL
	"CAMERA_STOP_FOLLOW",                      # -
	"CAMERA_STOP_TETHER_NAMED",                # -
	"CAMERA_TETHER_NAMED",                     # UNIT, BOOLEAN, REAL
	"DRAW_SKYBOX_BEGIN",                       # -
	"DRAW_SKYBOX_END",                         # -
	"FOCAL_LENGTH_CAMERA",                     # REAL, REAL, REAL, REAL
	"LOCK_CAMERA",                             # BOOLEAN
	"LOCK_CAMERA_ANGLE_AND_HEIGHT",            # BOOLEAN
	"LOCK_CAMERA_RESET",                       # BOOLEAN
	"LOCK_CAMERA_ROTATION",                    # BOOLEAN
	"LOCK_CAMERA_SCROLL",                      # BOOLEAN
	"LOCK_CAMERA_ZOOM",                        # BOOLEAN
	"MOVE_CAMERA_ALONG_SPLINE_PATH",           # WAYPOINT_PATH, REAL x5
	"MOVE_CAMERA_BY_ANIMATION",                # CAMERA_ANIMATION
	"MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH",   # WAYPOINT_PATH, REAL x5
	"MOVE_CAMERA_TO",                          # CAMERA, REAL, REAL, REAL, REAL
	"MOVE_CAMERA_TO_SELECTION",                # -
	"NAMED_SET_CAMERA_FADING",                 # UNIT, BOOLEAN  (see CHANNEL ROUTING)
	"OVERSIZE_TERRAIN",                        # INT       (see CHANNEL ROUTING)
	"PITCH_CAMERA",                            # REAL, REAL, REAL, REAL
	"RESET_CAMERA",                            # WAYPOINT, REAL, REAL, REAL
	"ROLL_CAMERA",                             # REAL, REAL, REAL, REAL
	"ROTATE_CAMERA",                           # REAL, REAL, REAL, REAL
	"ROTATE_CAMERA_LOCKED",                    # REAL, REAL, REAL, REAL
	"ROTATE_CAMERA_TO_ANGLE",                  # REAL, REAL, REAL, REAL
	"SCREEN_SHAKE",                            # SHAKE_INTENSITY
	"SETUP_CAMERA",                            # WAYPOINT, REAL, REAL, WAYPOINT
	"SET_CAMERA_CLIP_DEPTH_MULTIPLIER",        # REAL
	"TERRAIN_RENDER_DISABLE",                  # BOOLEAN   (see CHANNEL ROUTING)
	"ZOOM_CAMERA",                             # REAL, REAL, REAL, REAL
]

## Actions that go to the UI channel instead. Both take no arguments.
##
## CHANNEL ROUTING. A channel is a DESTINATION, not a meaning - the op name is
## always the canonical InternalName either way - but an adapter subscribes per
## channel, so the wrong channel is a cutscene step that never arrives. Four
## members sit on a boundary and were decided as follows:
##
##   HIDE_UI / SHOW_UI -> ui. The catalog tags them `needs-ui` on top of
##     `needs-camera`; they hide the HUD, they do not move the camera. This
##     matches WP01, which routes ENABLE_COUNTDOWN_TIMER_DISPLAY to ui() for the
##     same reason. Note that CAMERA_LETTERBOX_BEGIN also hides the HUD but is
##     tagged `needs-camera` alone and stays on camera: letterbox is a camera
##     mode with a HUD side effect, HIDE_UI is only the HUD.
##
##   TERRAIN_RENDER_DISABLE / OVERSIZE_TERRAIN -> camera. WorldBuilder files both
##     under Camera_/Terrain/ and both change only what is DRAWN - the terrain the
##     simulation walks and collides against is untouched. SageScriptWorld.Terrain
##     exists but models water height, burn rate, cloud speed, borders, fog state
##     and buildability; it has no render toggle and, being a simulation facet,
##     should not grow one for a draw-time switch.
##
##   NAMED_SET_CAMERA_FADING -> camera. Filed under Unit_/Status/ and it names a
##     unit, but the property is whether that object's DRAWABLE fades as the
##     camera closes on it. The unit name rides through as a string for the
##     adapter to resolve; no unit is looked up here, which is what keeps this
##     handler presentation-only.
##
##   CAMERA_SET_AUDIBLE_DISTANCE -> camera. The one genuine coin-flip in the
##     package: it sets an AUDIO range, but only for the duration of a camera-up
##     shot, and the audio sink belongs to WP06. It is emitted on the camera
##     channel because camera-shot state is what this package's adapter owns.
##     Flagged in the package report so the owner can move it in one line if the
##     audio adapter turns out to be the right listener.
const UI_ACTIONS := [
	"HIDE_UI",   # -
	"SHOW_UI",   # -
]

## The seven camera CONDITIONS, blocked on the missing read surface. See "THE
## SEVEN CONDITIONS" above.
const BLOCKED_CONDITIONS := [
	"CAMERA_ENTERED_AREA",                  # TRIGGER_AREA
	"CAMERA_HIT_SPECIFIC_SPLINE_WAYPOINT",  # WAYPOINT
	"CAMERA_MOVEMENT_FINISHED",             # -
	"CAMERA_RESET",                         # -
	"CAMERA_ROTATE_DISTANCE",               # REAL
	"CAMERA_SCROLL_DISTANCE",               # REAL
	"CAMERA_ZOOM_DISTANCE",                 # REAL
]

const CONDITION_SUBSYSTEM := (
	"camera state read-back (SageScriptWorld exposes camera as a one-way "
	+ "presentation sink with no query surface, so camera position, movement "
	+ "completion, spline progress and player scroll/zoom/rotate distance cannot "
	+ "be read by anything)"
)

## Parameter type tokens that appear in this package's 74 signatures and carry a
## NAME (read from the decoded record's `text` field). Listed explicitly rather
## than defaulted to, so that a token this file has never seen is a loud
## BAD_ARGUMENTS instead of a string quietly standing in for a number.
const NAME_TOKENS := [
	"CAMERA",            # a named camera marker placed in WorldBuilder
	"CAMERA_ANIMATION",  # a 3DSMax camera animation asset name
	"TRIGGER_AREA",      # a named polygon trigger area
	"UNIT",              # an object's WorldBuilder script name
	"WAYPOINT",          # a named waypoint
	"WAYPOINT_PATH",     # a named waypoint chain
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	for name: String in CAMERA_ACTIONS:
		reg.action(name, _camera_action)
	for name: String in UI_ACTIONS:
		reg.action(name, _ui_action)
	reg.blocked_conditions(BLOCKED_CONDITIONS, CONDITION_SUBSYSTEM)


# --- Handlers -------------------------------------------------------------
#
# Two handlers for 67 actions, because a presentation action has no per-action
# behaviour to write: the sink contract is "the op name and the signature's
# arguments in declaration order", identically for all of them. The per-action
# content is the SIGNATURE, and the signature already lives in the vocabulary
# table that `_forward()` walks - transcribing it into 67 hand-written bodies
# would create 67 chances to transcribe it wrong, with the vocabulary as the
# thing they would silently disagree with.
#
# This is not a generic fallback. `_forward()` refuses anything it cannot decode
# exactly, and every one of the 67 names is listed above by hand, so an action
# that is not in this package cannot reach these handlers.


static func _camera_action(ctx: Dictionary) -> int:
	var world: SageScriptWorld = ctx["world"]
	return _forward(ctx, world.camera(), "camera")


static func _ui_action(ctx: Dictionary) -> int:
	var world: SageScriptWorld = ctx["world"]
	return _forward(ctx, world.ui(), "ui")


static func _forward(
	ctx: Dictionary, sink: SageScriptWorld.PresentationSink, channel: String
) -> int:
	var args: SageScriptArgs = ctx["args"]
	var params: Array = args.signature()

	# Arity is validated before a handler runs, EXCEPT for entries the source
	# reference marks uncertain, which are arity-exempt. None of this package's
	# 74 signatures is uncertain today, so this check should never fire - it is
	# here because positional reads are only safe while it holds, and a silently
	# short argument list would be read as a shorter action.
	if args.size() != params.size():
		ctx["detail"] = (
			"signature declares %d argument(s) %s but the record carries %d; "
			+ "positional reads are unsafe against a mismatched arity"
		) % [params.size(), str(params), args.size()]
		return Dispatch.Status.BAD_ARGUMENTS

	var values: Array = []
	for index in range(params.size()):
		var token := String(params[index])
		var decoded: Variant = _decode(args, index, token)
		if decoded == null:
			ctx["detail"] = (
				"argument %d declares parameter type '%s', which this package has "
				+ "no decoding rule for; refusing rather than guessing a field"
			) % [index, token]
			return Dispatch.Status.BAD_ARGUMENTS
		values.append(decoded)

	if not sink.emit(String(ctx["name"]), values):
		ctx["detail"] = "world has no %s presentation sink" % channel
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _decode(args: SageScriptArgs, index: int, token: String) -> Variant:
	## Reads slot `index` using the field its DECLARED type implies. Returns null
	## for a token this package's signatures do not use - the caller turns that
	## into BAD_ARGUMENTS. Never searches the argument list.
	##
	## The field choices follow SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM.
	## PERCENT is an integer there (an ease-in/ease-out percentage, not a
	## fraction) and SHAKE_INTENSITY is an enum passed as its raw int, per
	## script_world.gd's argument conventions.
	match token:
		"REAL":
			return args.real(index)
		"INT":
			return args.integer(index)
		"PERCENT":
			return args.integer(index)
		"SHAKE_INTENSITY":
			return args.integer(index)
		"BOOLEAN":
			return args.boolean(index)
	if NAME_TOKENS.has(token):
		return args.text(index)
	return null
