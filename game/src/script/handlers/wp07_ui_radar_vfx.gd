extends RefCounted

## WP07-ui-radar-vfx - HUD, notifications, flashing, objectives, command bar,
## radar, input gating, world lights and tree sway.
##
## 58 members: 51 actions served by the UI PRESENTATION SINK, 4 actions and 3
## conditions declared BLOCKED because the world interface has no routing for
## them (see the two blocked sections at the bottom of this comment).
##
##
## EVERYTHING SERVED HERE GOES TO ONE PLACE: world.ui()
## ====================================================
## SageScriptWorld gives WP05/WP06/WP07 three sinks - camera(), audio(), ui() -
## one per presentation work package, not one per subject matter. So every
## action in this file emits on ui(), including EVA_SET_ENABLED_DISABLED (which
## sounds like audio) and DIM_WORLD_LIGHTS / SET_TREE_SWAY (which sound like
## scene). The sink is a destination, not a classifier; the op name carries the
## meaning. Routing one of these to audio() would put a WP07 member in WP06's
## channel and make the recording lie about which package emitted it.
##
## No handler here reads anything back. The sink is one-way by design (lockstep:
## presentation must not be able to influence the simulation), and no handler
## here touches ctx.env either. DISPLAY_COUNTER and HIDE_COUNTER name a counter
## but do not read or write it - they tell the HUD which counter to show.
##
##
## ARGUMENTS ARE EMITTED VERBATIM, IN DECLARATION ORDER
## ====================================================
## `values` is "the signature's arguments and nothing else: no re-ordering, no
## defaults filled in, no extra context bolted on" (PresentationSink in
## script_world.gd). Two consequences worth stating, because both look like
## omissions otherwise:
##
##   * DURATIONS ARE NOT CONVERTED TO TICKS. The world's "durations are always
##     interpreter ticks" convention exists so a FACET never has to guess a
##     timebase. A sink is not a facet: it has no timebase, and the sink
##     contract says a test asserting on a recording asserts on the MAP's own
##     arguments. NAMED_FLASH(UNIT, 5) therefore records 5, the seconds the map
##     wrote, and the presentation adapter converts. If that is ever changed it
##     must be changed for WP05 and WP06 in the same commit, or the three sinks
##     will disagree about what a number means.
##   * ZERO-ARGUMENT ACTIONS EMIT []. Not [true], not [the action name]. The
##     pilot's ENABLE/DISABLE_COUNTDOWN_TIMER_DISPLAY emit a synthesised bool
##     because one shared body serves both names; nothing here needs that, and
##     inventing a value would be a default filled in.
##
##
## WHICH FIELD EACH ARGUMENT IS READ FROM
## ======================================
## Position is settled by the signature (never by type-searching - see
## script_args.gd). WHICH FIELD of the decoded slot to read is a separate
## question, because sage_scb.py stores integer + real + text for every
## non-position argument, so every field always looks populated.
##
## Each handler below reads the field the DECLARED PARAMETER TYPE implies, with
## the signature written above it:
##
##   INT                                    -> integer
##   REAL, ANGLE                            -> real
##   BOOLEAN                                -> boolean (integer != 0)
##   COORD3D                                -> position
##   RADAR_EVENT, COLOR                     -> integer  (see below)
##   COMMAND_BUTTON, OBJECT_TYPE, UNIT,
##   TEAM, HERO_UNIT, COUNTER, DIALOG,
##   LOCALIZED_TEXT, NOTIFICATION_BOX_TYPE  -> text
##
## RADAR_EVENT and COLOR are read as INTEGERS. RADAR_EVENT is an enum the
## reference numbers (Information=0 ... Banner=5) and which ParamTypes.ENUMS
## carries; COLOR is a packed value the reference numbers (normal=0, blue=255,
## red=1, purple=50, yellow=-256, white=-1).
##
## When this package was written, SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM
## had a row for NEITHER, so the shared table would have defaulted both to
## "text" - and reading the wrong field fails silently, because sage_scb.py
## fills integer, real AND text for every argument, so the handler would have
## received "" where the map authored 3. That was reported rather than worked
## around, and 45b0b43 added the missing rows (ten of the sixteen ENUMS members
## were affected, not just these two). The reads below now AGREE with the shared
## table instead of departing from it, and the runner asserts that agreement so
## a revert of those rows fails here rather than silently emptying a value.
##
##
## BLOCKED 1/2: THE MATCH-OUTCOME FAMILY (4 actions)
## =================================================
## VICTORY, DEFEAT, QUICKVICTORY and VICTORY_SCREEN are NOT emitted to the UI
## sink. Deciding a match is simulation, and the sink cannot reach the
## simulation by construction - putting them there would make the package look
## complete while a mission that announces a win did nothing but describe one.
##
## The interface routes victory/defeat to WP08: the Meta facet's own section
## header says "WP08 (campaign, LivingWorld, victory/defeat, game mode)". But
## the only outcome-shaped members it exposes are `declare_local_defeat`
## (documented WP08 LOCALDEFEAT, a different action) and `multiplayer_outcome`
## (a read). There is no world command that ends the match for these four.
##
## RULED AND CONFIRMED: no match-end routing exists in Meta, and the decision is
## the owner's. All four stay blocked; the exact signatures wanted are in the
## report.
##
## VICTORY_SCREEN is the one the owner may want to move. Its catalog entry is
## high-confidence and explicit - "Shows the victory screen, but does not behave
## as a victory" - which reads as pure presentation, i.e. a legitimate
## ui().emit("VICTORY_SCREEN", []). It is blocked here anyway because the owner
## reserved the whole outcome family, and a wrong sink routing is silent while a
## blocked declaration is loud. That flip is the OWNER'S call, not this
## package's; it is a one-line change if and when they make it.
##
##
## BLOCKED 2/2: THE THREE UI-STATE CONDITIONS
## ==========================================
## OBJECTIVES_SCREEN_IS_OPEN, SPELL_STORE_IS_OPEN and
## TOGGLE_STANCE_SUBMENU_IS_OPEN ask whether a piece of HUD is open. There is no
## interface path for that answer: the presentation sink is write-only
## ("nothing is ever read back"), and no facet exposes HUD state. Answering them
## needs an owner decision of the same kind as the open SELECT_OBJECT question -
## whether UI screen state is replicated client state a script may branch on at
## all, which in a lockstep build it cannot be without care.
##
## Blocked is the honest declaration. It produces a `blocked-subsystem` gap
## naming the missing surface, and the condition evaluates FALSE the way every
## unevaluable condition does - as a recorded fail-safe, never as an answer.

const PACKAGE := "WP07-ui-radar-vfx"

const Dispatch := preload("res://src/script/script_dispatch.gd")

## Match outcome. See "BLOCKED 1/2" above.
const OUTCOME_SUBSYSTEM := (
	"match outcome (announcing a win or loss and ending the match): no "
	+ "match-end routing in Meta; owner decision pending. The facet offers only "
	+ "declare_local_defeat (WP08 LOCALDEFEAT, a different action) and the "
	+ "multiplayer_outcome read"
)

const BLOCKED_OUTCOME_ACTIONS := [
	"DEFEAT",
	"QUICKVICTORY",
	"VICTORY",
	"VICTORY_SCREEN",
]

## HUD state readback. See "BLOCKED 2/2" above.
const UI_STATE_SUBSYSTEM := (
	"UI screen-state readback; the presentation sink is one-way by design and "
	+ "no facet exposes whether a HUD screen is open"
)

const BLOCKED_UI_STATE_CONDITIONS := [
	"OBJECTIVES_SCREEN_IS_OPEN",
	"SPELL_STORE_IS_OPEN",
	"TOGGLE_STANCE_SUBMENU_IS_OPEN",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- No arguments -----------------------------------------------------
	# Input gating, the objectives / planning / spell-store screens, the radar
	# controls and the world-light pair. All emit [].
	reg.action("CLOSE_OBJECTIVES_SCREEN", _emit_no_arguments)
	reg.action("DIM_WORLD_LIGHTS", _emit_no_arguments)
	reg.action("DISABLE_INPUT", _emit_no_arguments)
	reg.action("DISABLE_OBJECTIVES_SCREEN", _emit_no_arguments)
	reg.action("DISABLE_PLANNING_MODE", _emit_no_arguments)
	reg.action("DISABLE_SPELL_STORE", _emit_no_arguments)
	reg.action("ENABLE_INPUT", _emit_no_arguments)
	reg.action("ENABLE_OBJECTIVES_SCREEN", _emit_no_arguments)
	reg.action("ENABLE_PLANNING_MODE", _emit_no_arguments)
	reg.action("ENABLE_SPELL_STORE", _emit_no_arguments)
	reg.action("RADAR_DISABLE", _emit_no_arguments)
	reg.action("RADAR_ENABLE", _emit_no_arguments)
	reg.action("RADAR_FORCE_ENABLE", _emit_no_arguments)
	reg.action("RADAR_REVERT_TO_NORMAL", _emit_no_arguments)
	reg.action("REFRESH_RADAR", _emit_no_arguments)
	reg.action("RESTORE_WORLD_LIGHTS", _emit_no_arguments)

	# --- (BOOLEAN) --------------------------------------------------------
	# House colour, the EVA announcer, and the three client video options.
	reg.action("ENABLE_HOUSE_COLOR", _emit_boolean)
	reg.action("EVA_SET_ENABLED_DISABLED", _emit_boolean)
	reg.action("OPTIONS_SET_DRAWICON_UI_MODE", _emit_boolean)
	reg.action("OPTIONS_SET_OCCLUSION_MODE", _emit_boolean)
	reg.action("OPTIONS_SET_PARTICLE_CAP_MODE", _emit_boolean)

	# --- (INT) ------------------------------------------------------------
	# Button flashes, where the int is a duration in seconds ...
	reg.action("FLASH_OBJECTIVES_BUTTON", _emit_integer)
	reg.action("FLASH_PLANNING_MODE_BUTTON", _emit_integer)
	reg.action("FLASH_SPELL_STORE_BUTTON", _emit_integer)
	reg.action("SELECT_BUILDER_BUTTON_FLASH", _emit_integer)
	# ... and mission-objective slots, where the int is an objective INDEX.
	# Same shape, different meaning; the op name is what tells them apart.
	reg.action("HIDE_MISSION_OBJECTIVE", _emit_integer)
	reg.action("MARK_MISSION_OBJECTIVE_COMPLETED", _emit_integer)
	reg.action("MARK_MISSION_OBJECTIVE_NOT_COMPLETED", _emit_integer)
	reg.action("SHOW_MISSION_OBJECTIVE", _emit_integer)

	# --- (one string) -----------------------------------------------------
	reg.action("DISPLAY_TEXT", _emit_text)          # (LOCALIZED_TEXT)
	reg.action("HIDE_COUNTDOWN_TIMER", _emit_text)  # (COUNTER)
	reg.action("HIDE_COUNTER", _emit_text)          # (COUNTER)

	# --- (two strings) ----------------------------------------------------
	reg.action("DISPLAY_COUNTDOWN_TIMER", _emit_text_text)  # (COUNTER, LOCALIZED_TEXT)
	reg.action("DISPLAY_COUNTER", _emit_text_text)          # (COUNTER, LOCALIZED_TEXT)
	# (COMMAND_BUTTON, OBJECT_TYPE) - two strings a type-search cannot tell
	# apart, which is why every read in this file is by index.
	reg.action("COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE", _emit_text_text)

	# --- (string, int) ----------------------------------------------------
	# Flashes, where the int is seconds ...
	reg.action("CAMEO_FLASH", _emit_text_integer)               # (COMMAND_BUTTON, INT)
	reg.action("HERO_SELECT_BUTTON_FLASH", _emit_text_integer)  # (HERO_UNIT, INT)
	reg.action("NAMED_FLASH", _emit_text_integer)               # (UNIT, INT)
	reg.action("NAMED_FLASH_WHITE", _emit_text_integer)         # (UNIT, INT)
	reg.action("TEAM_FLASH", _emit_text_integer)                # (TEAM, INT)
	reg.action("TEAM_FLASH_WHITE", _emit_text_integer)          # (TEAM, INT)
	# ... a packed colour ...
	reg.action("NAMED_CUSTOM_COLOR", _emit_text_integer)        # (UNIT, COLOR)
	# ... and the radar-event enum.
	reg.action("OBJECT_CREATE_RADAR_EVENT", _emit_text_integer)  # (UNIT, RADAR_EVENT)
	reg.action("TEAM_CREATE_RADAR_EVENT", _emit_text_integer)    # (TEAM, RADAR_EVENT)

	# --- One of a kind ----------------------------------------------------
	reg.action("COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT", _commandbar_add_button_slot)
	reg.action("DISPLAY_NOTIFICATION_BOX", _display_notification_box)
	reg.action(
		"DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE",
		_display_notification_box_with_image
	)
	reg.action("OBJECT_FORCE_SELECT", _object_force_select)
	reg.action("RADAR_CREATE_EVENT", _radar_create_event)
	reg.action("SET_TREE_SWAY", _set_tree_sway)
	reg.action("SHOW_MILITARY_CAPTION", _show_military_caption)

	# --- Blocked ----------------------------------------------------------
	reg.blocked_actions(BLOCKED_OUTCOME_ACTIONS, OUTCOME_SUBSYSTEM)
	reg.blocked_conditions(BLOCKED_UI_STATE_CONDITIONS, UI_STATE_SUBSYSTEM)


# --- The one thing every handler in this file does -------------------------


static func _emit(ctx: Dictionary, values: Array) -> int:
	## Emits the action's canonical name and its arguments on the UI sink.
	##
	## A world with no UI adapter refuses, and that refusal becomes a
	## WORLD_REFUSED gap naming the action. It never becomes OK: a cutscene that
	## silently did not happen is the failure this whole subsystem exists to
	## make visible.
	var world: SageScriptWorld = ctx["world"]
	if not world.ui().emit(String(ctx["name"]), values):
		ctx["detail"] = "world has no UI presentation sink"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


# --- Shared shapes ---------------------------------------------------------
#
# These bodies are shared by SIGNATURE SHAPE, not by subject. Each is a complete
# implementation of every name registered against it - the op name travels in
# ctx.name, so one body serving sixteen zero-argument actions emits sixteen
# distinguishable records. The runner drives all 51 served names and asserts the
# emitted value count equals the signature's arity, so a name registered against
# the wrong shape fails there rather than quietly emitting too few values.


static func _emit_no_arguments(ctx: Dictionary) -> int:
	# () - emits [], because the signature has no arguments to emit.
	return _emit(ctx, [])


static func _emit_boolean(ctx: Dictionary) -> int:
	# (BOOLEAN)
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.boolean(0)])


static func _emit_integer(ctx: Dictionary) -> int:
	# (INT)
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.integer(0)])


static func _emit_text(ctx: Dictionary) -> int:
	# (LOCALIZED_TEXT) or (COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0)])


static func _emit_text_text(ctx: Dictionary) -> int:
	# (COUNTER, LOCALIZED_TEXT) or (COMMAND_BUTTON, OBJECT_TYPE)
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.text(1)])


static func _emit_text_integer(ctx: Dictionary) -> int:
	# (COMMAND_BUTTON | HERO_UNIT | UNIT | TEAM, INT | COLOR | RADAR_EVENT)
	# The name is always first and the number always second across all nine
	# names registered here. RADAR_EVENT and COLOR are integers - see the file
	# comment on which field each parameter type is read from.
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.integer(1)])


# --- One of a kind ---------------------------------------------------------


static func _commandbar_add_button_slot(ctx: Dictionary) -> int:
	# COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT(COMMAND_BUTTON, OBJECT_TYPE, INT)
	# Two strings then the slot number (1-12). The strings are adjacent and
	# interchangeable to anything but an index.
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.text(1), args.integer(2)])


static func _display_notification_box(ctx: Dictionary) -> int:
	# DISPLAY_NOTIFICATION_BOX(NOTIFICATION_BOX_TYPE, LOCALIZED_TEXT, INT)
	# INT is a duration in seconds, 0 meaning indefinite. Emitted as written -
	# 0 is a real value here, not a missing one.
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.text(1), args.integer(2)])


static func _display_notification_box_with_image(ctx: Dictionary) -> int:
	# DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE
	#   (NOTIFICATION_BOX_TYPE, LOCALIZED_TEXT, INT, OBJECT_TYPE)
	# THREE strings around one int, and the third string is at index 3, AFTER
	# the int. A reader that collected the strings and then the numbers would
	# produce the same four values in a different order and look correct.
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.text(1), args.integer(2), args.text(3)])


static func _object_force_select(ctx: Dictionary) -> int:
	# OBJECT_FORCE_SELECT(TEAM, OBJECT_TYPE, BOOLEAN, DIALOG)
	# Selects a team's first object of a type, optionally centring the view,
	# while playing a line of dialog.
	#
	# ROUTED TO THE UI SINK, AND THERE IS AN OPEN OWNER QUESTION BEHIND THAT.
	# Whether selection is simulation or client state is unresolved (the sim
	# deliberately excludes selection from state_hash). The interface does carry
	# `units.set_selected`, but its doc comment routes it to WP17
	# (SELECT_OBJECT / DESELECT) and it takes ONE OBJECT NAME - there is no
	# world method that resolves "a team's first object of type X", so this
	# action could not use it even if the routing allowed. Emitting on the sink
	# is what the current interface routes for WP07; if selection is later ruled
	# simulation state, this handler moves with that ruling.
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.text(1), args.boolean(2), args.text(3)])


static func _radar_create_event(ctx: Dictionary) -> int:
	# RADAR_CREATE_EVENT(COORD3D, RADAR_EVENT)
	# The only member of this package carrying a position. A COORD3D argument
	# has no integer/real/text payload at all (sage_scb.py writes three floats
	# and nothing else), so reading it as anything but a position yields "".
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.position(0), args.integer(1)])


static func _set_tree_sway(ctx: Dictionary) -> int:
	# SET_TREE_SWAY(ANGLE wind direction, ANGLE sway amount, ANGLE lean amount,
	#               INT frames per sway, REAL randomness)
	# THREE ANGLES IN A ROW, all reals, all meaning different things. Position
	# is the only thing separating them; this is the signature in the package
	# that a type-search gets most confidently wrong.
	#
	# `frames per sway` is emitted as the frame count the map wrote. It is not
	# converted to ticks: it is an animation rate for the renderer, not a
	# simulation duration, and the sink passes arguments through verbatim.
	var args: SageScriptArgs = ctx["args"]
	return _emit(
		ctx,
		[args.real(0), args.real(1), args.real(2), args.integer(3), args.real(4)]
	)


static func _show_military_caption(ctx: Dictionary) -> int:
	# SHOW_MILITARY_CAPTION(LOCALIZED_TEXT, REAL seconds)
	var args: SageScriptArgs = ctx["args"]
	return _emit(ctx, [args.text(0), args.real(1)])
