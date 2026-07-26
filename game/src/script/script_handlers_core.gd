class_name SageScriptCoreHandlers
extends RefCounted

## Tranche 1 of the SAGE script vocabulary: everything that needs no simulation.
##
## Scope rule for this file: a handler belongs here if it can be fully served by
## the interpreter's own state (counters, flags, timers, script enable bits) or
## by a capability the SageScriptWorld interface already exposes. Anything that
## needs units, teams, waypoints, camera or audio waits for a later tranche and
## stays an explicit `unimplemented` gap until then.
##
## Every signature below is transcribed from the generated vocabulary tables,
## and arguments are read POSITIONALLY in declaration order - several of these
## repeat a parameter type (SET_FLAG_TO_FLAG, COUNTER_COUNTER,
## SET_RANDOM_COUNTER) and a type-search read would silently pick the wrong one.
##
## Note the argument ORDER of the increment/decrement and msec-timer families:
## the value comes FIRST and the counter/timer name SECOND
## (INCREMENT_COUNTER(INT, COUNTER), ADD_TO_MSEC_TIMER(REAL, COUNTER)). That is
## not a transcription slip; it is how SAGE declares them.

const Dispatch := preload("res://src/script/script_dispatch.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const World := preload("res://src/script/script_world.gd")

## UNRESOLVED UNIT QUESTION - read before changing.
##
## The SET_MILLISECOND_TIMER / SET_RANDOM_MSEC_TIMER / ADD_TO_MSEC_TIMER /
## SUB_FROM_MSEC_TIMER family is named in milliseconds but the community
## reference describes every one of them in SECONDS ("Set timer <COUNTER> to
## expire in <REAL> seconds", WorldBuilder menu "Seconds countdown timer"). The
## most likely explanation is that the engine stores milliseconds internally
## while WorldBuilder authors seconds - which leaves open which of the two a
## decoded .map argument actually carries.
##
## This repo has no decoded sample to settle it (the skirmish contract JSON is
## not present on a plain checkout), so the unit is a NAMED CHOICE rather than a
## silent assumption. The default matches the behaviour already shipped in
## game/src/retail_slice/retail_map_scripts.gd, so nothing changes meaning; set
## `MSEC_FAMILY_UNIT_IS_SECONDS = true` if a real map ever proves otherwise, and
## expect every timer in the game to change duration by 1000x when you do.
const MSEC_FAMILY_UNIT_IS_SECONDS := false


static func milliseconds_from_argument(value: float) -> float:
	return value * 1000.0 if MSEC_FAMILY_UNIT_IS_SECONDS else value


static func register_all(dispatch: SageScriptDispatch) -> void:
	## Registers tranche 1. Returns nothing: a name that is not in the
	## vocabulary is rejected loudly by the dispatcher itself.
	_register_actions(dispatch)
	_register_conditions(dispatch)


static func _register_actions(dispatch: SageScriptDispatch) -> void:
	dispatch.register_action("NO_OP", _no_op)

	# Flags
	dispatch.register_action("SET_FLAG", _set_flag)
	dispatch.register_action("SET_FLAG_TO_FLAG", _set_flag_to_flag)

	# Counters
	dispatch.register_action("SET_COUNTER", _set_counter)
	dispatch.register_action("SET_COUNTER_TO_COUNTER", _set_counter_to_counter)
	dispatch.register_action("INCREMENT_COUNTER", _increment_counter)
	dispatch.register_action("DECREMENT_COUNTER", _decrement_counter)
	dispatch.register_action("COUNTER_MATH_VALUE", _counter_math_value)
	dispatch.register_action("COUNTER_MATH_COUNTER", _counter_math_counter)
	dispatch.register_action("SET_RANDOM_COUNTER", _set_random_counter)
	dispatch.register_deliberate_refusal(
		"SET_COUNTER_TO_CLIENT_RANDOM_VALUE", _set_counter_to_client_random_value
	)

	# Timers
	dispatch.register_action("SET_TIMER", _set_timer)
	dispatch.register_action("SET_RANDOM_TIMER", _set_random_timer)
	dispatch.register_action("SET_MILLISECOND_TIMER", _set_millisecond_timer)
	dispatch.register_action("SET_RANDOM_MSEC_TIMER", _set_random_msec_timer)
	dispatch.register_action("ADD_TO_MSEC_TIMER", _add_to_msec_timer)
	dispatch.register_action("SUB_FROM_MSEC_TIMER", _sub_from_msec_timer)
	dispatch.register_action("STOP_TIMER", _stop_timer)
	dispatch.register_action("RESTART_TIMER", _restart_timer)

	# Script control
	dispatch.register_action("ENABLE_SCRIPT", _enable_script)
	dispatch.register_action("DISABLE_SCRIPT", _disable_script)

	# Player economy (the one world capability tranche 1 needs)
	dispatch.register_action("PLAYER_SET_MONEY", _player_set_money)
	dispatch.register_action("SET_PLAYER_MONEY_TO_COUNTER", _set_player_money_to_counter)

	# Diagnostics
	dispatch.register_action("DEBUG_STRING", _debug_output)
	dispatch.register_action("DEBUG_MESSAGE_BOX", _debug_output)
	dispatch.register_action("DEBUG_CRASH_BOX", _debug_output)


static func _register_conditions(dispatch: SageScriptDispatch) -> void:
	dispatch.register_condition("CONDITION_TRUE", _condition_true)
	dispatch.register_condition("CONDITION_FALSE", _condition_false)
	dispatch.register_condition("COUNTER", _condition_counter)
	dispatch.register_condition("COUNTER_COUNTER", _condition_counter_counter)
	dispatch.register_condition("FLAG", _condition_flag)
	dispatch.register_condition("TIMER_EXPIRED", _condition_timer_expired)


# --- Actions --------------------------------------------------------------


static func _no_op(_ctx: Dictionary) -> int:
	return Dispatch.Status.OK


static func _set_flag(ctx: Dictionary) -> int:
	# SET_FLAG(FLAG, BOOLEAN)
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].set_flag(args.text(0), args.boolean(1, true))
	return Dispatch.Status.OK


static func _set_flag_to_flag(ctx: Dictionary) -> int:
	# SET_FLAG_TO_FLAG(FLAG destination, FLAG source)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	env.set_flag(args.text(0), env.flag(args.text(1)))
	return Dispatch.Status.OK


static func _set_counter(ctx: Dictionary) -> int:
	# SET_COUNTER(COUNTER, INT)
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].set_counter(args.text(0), args.integer(1))
	return Dispatch.Status.OK


static func _set_counter_to_counter(ctx: Dictionary) -> int:
	# SET_COUNTER_TO_COUNTER(COUNTER destination, COUNTER source)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	env.set_counter(args.text(0), env.counter(args.text(1)))
	return Dispatch.Status.OK


static func _increment_counter(ctx: Dictionary) -> int:
	# INCREMENT_COUNTER(INT amount, COUNTER) - value first, name second.
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].add_counter(args.text(1), args.integer(0))
	return Dispatch.Status.OK


static func _decrement_counter(ctx: Dictionary) -> int:
	# DECREMENT_COUNTER(INT amount, COUNTER) - value first, name second.
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].add_counter(args.text(1), -args.integer(0))
	return Dispatch.Status.OK


static func _apply_math(env: SageScriptEnv, name: String, operator: int, operand: int) -> int:
	var current := env.counter(name)
	match operator:
		ParamTypes.MATH_ADD:
			env.set_counter(name, current + operand)
		ParamTypes.MATH_SUBTRACT:
			env.set_counter(name, current - operand)
		ParamTypes.MATH_MULTIPLY:
			env.set_counter(name, current * operand)
		ParamTypes.MATH_DIVIDE:
			# Division by zero has no sourced retail behaviour. Refusing is the
			# only honest answer: silently leaving the counter alone would look
			# like the operation succeeded.
			if operand == 0:
				return Dispatch.Status.BAD_ARGUMENTS
			env.set_counter(name, current / operand)
		_:
			return Dispatch.Status.BAD_ARGUMENTS
	return Dispatch.Status.OK


static func _counter_math_value(ctx: Dictionary) -> int:
	# COUNTER_MATH_VALUE(COUNTER, MATH_OPERATOR, INT)
	var args: SageScriptArgs = ctx["args"]
	var status := _apply_math(ctx["env"], args.text(0), args.integer(1), args.integer(2))
	if status != Dispatch.Status.OK:
		ctx["detail"] = "unsupported MATH_OPERATOR %d or division by zero" % args.integer(1)
	return status


static func _counter_math_counter(ctx: Dictionary) -> int:
	# COUNTER_MATH_COUNTER(COUNTER, MATH_OPERATOR, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	var status := _apply_math(env, args.text(0), args.integer(1), env.counter(args.text(2)))
	if status != Dispatch.Status.OK:
		ctx["detail"] = "unsupported MATH_OPERATOR %d or division by zero" % args.integer(1)
	return status


static func _set_random_counter(ctx: Dictionary) -> int:
	# SET_RANDOM_COUNTER(COUNTER, INT low, INT high)
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.supports(World.CAP_RANDOM):
		ctx["detail"] = "world provides no deterministic random stream"
		return Dispatch.Status.WORLD_REFUSED
	ctx["env"].set_counter(args.text(0), world.random_int(args.integer(1), args.integer(2)))
	return Dispatch.Status.OK


static func _set_counter_to_client_random_value(ctx: Dictionary) -> int:
	# SET_COUNTER_TO_CLIENT_RANDOM_VALUE(COUNTER, INT, INT)
	#
	# Refused ON PURPOSE, not unimplemented. The source reference states this
	# action draws from the CLIENT-side random stream and "will cause desyncs if
	# used for multiplayer maps". This project runs lockstep multiplayer, so
	# serving it would inject a divergence that no test could reproduce. It is
	# recorded as a `deliberate` gap so a map that uses it is visible.
	ctx["detail"] = (
		"client-side random is desync-prone by design and is refused in a "
		+ "lockstep build; see the reference's own warning"
	)
	return Dispatch.Status.DELIBERATE


static func _set_timer(ctx: Dictionary) -> int:
	# SET_TIMER(COUNTER, INT frames)
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].set_timer_frames(args.text(0), args.integer(1))
	return Dispatch.Status.OK


static func _set_random_timer(ctx: Dictionary) -> int:
	# SET_RANDOM_TIMER(COUNTER, INT low frames, INT high frames)
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.supports(World.CAP_RANDOM):
		ctx["detail"] = "world provides no deterministic random stream"
		return Dispatch.Status.WORLD_REFUSED
	ctx["env"].set_timer_frames(
		args.text(0), world.random_int(args.integer(1), args.integer(2))
	)
	return Dispatch.Status.OK


static func _set_millisecond_timer(ctx: Dictionary) -> int:
	# SET_MILLISECOND_TIMER(COUNTER, REAL) - see MSEC_FAMILY_UNIT_IS_SECONDS.
	var args: SageScriptArgs = ctx["args"]
	ctx["env"].set_timer_milliseconds(
		args.text(0), milliseconds_from_argument(args.real(1))
	)
	return Dispatch.Status.OK


static func _set_random_msec_timer(ctx: Dictionary) -> int:
	# SET_RANDOM_MSEC_TIMER(COUNTER, REAL low, REAL high)
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.supports(World.CAP_RANDOM):
		ctx["detail"] = "world provides no deterministic random stream"
		return Dispatch.Status.WORLD_REFUSED
	var drawn := world.random_real(args.real(1), args.real(2))
	ctx["env"].set_timer_milliseconds(args.text(0), milliseconds_from_argument(drawn))
	return Dispatch.Status.OK


static func _add_to_msec_timer(ctx: Dictionary) -> int:
	# ADD_TO_MSEC_TIMER(REAL amount, COUNTER) - value first, timer second.
	var args: SageScriptArgs = ctx["args"]
	if not ctx["env"].adjust_timer_milliseconds(
		args.text(1), milliseconds_from_argument(args.real(0))
	):
		ctx["detail"] = "timer '%s' has never been set" % args.text(1)
		return Dispatch.Status.BAD_ARGUMENTS
	return Dispatch.Status.OK


static func _sub_from_msec_timer(ctx: Dictionary) -> int:
	# SUB_FROM_MSEC_TIMER(REAL amount, COUNTER) - value first, timer second.
	var args: SageScriptArgs = ctx["args"]
	if not ctx["env"].adjust_timer_milliseconds(
		args.text(1), -milliseconds_from_argument(args.real(0))
	):
		ctx["detail"] = "timer '%s' has never been set" % args.text(1)
		return Dispatch.Status.BAD_ARGUMENTS
	return Dispatch.Status.OK


static func _stop_timer(ctx: Dictionary) -> int:
	# STOP_TIMER(COUNTER)
	var args: SageScriptArgs = ctx["args"]
	if not ctx["env"].stop_timer(args.text(0)):
		ctx["detail"] = "timer '%s' has never been set" % args.text(0)
		return Dispatch.Status.BAD_ARGUMENTS
	return Dispatch.Status.OK


static func _restart_timer(ctx: Dictionary) -> int:
	# RESTART_TIMER(COUNTER)
	var args: SageScriptArgs = ctx["args"]
	if not ctx["env"].restart_timer(args.text(0)):
		ctx["detail"] = "timer '%s' has never been set" % args.text(0)
		return Dispatch.Status.BAD_ARGUMENTS
	return Dispatch.Status.OK


static func _enable_script(ctx: Dictionary) -> int:
	# ENABLE_SCRIPT(SCRIPT)
	ctx["env"].set_script_enabled((ctx["args"] as SageScriptArgs).text(0), true)
	return Dispatch.Status.OK


static func _disable_script(ctx: Dictionary) -> int:
	# DISABLE_SCRIPT(SCRIPT)
	ctx["env"].set_script_enabled((ctx["args"] as SageScriptArgs).text(0), false)
	return Dispatch.Status.OK


static func _player_set_money(ctx: Dictionary) -> int:
	# PLAYER_SET_MONEY(PLAYER, INT)
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.set_player_money(args.text(0), args.integer(1)):
		ctx["detail"] = "world declined to set money for player '%s'" % args.text(0)
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _set_player_money_to_counter(ctx: Dictionary) -> int:
	# SET_PLAYER_MONEY_TO_COUNTER(PLAYER, COUNTER)
	# Reads money INTO the counter ("Store amount of <PLAYER> money to the
	# counter <COUNTER>"), despite the action name reading the other way round.
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.supports(World.CAP_PLAYER_MONEY):
		ctx["detail"] = "world does not model player money"
		return Dispatch.Status.WORLD_REFUSED
	ctx["env"].set_counter(args.text(1), world.player_money(args.text(0)))
	return Dispatch.Status.OK


static func _debug_output(ctx: Dictionary) -> int:
	# DEBUG_STRING / DEBUG_MESSAGE_BOX / DEBUG_CRASH_BOX (Text)
	var world: SageScriptWorld = ctx["world"]
	if not world.debug_message(String(ctx["name"]), (ctx["args"] as SageScriptArgs).text(0)):
		ctx["detail"] = "world has no debug output channel"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


# --- Conditions (must not mutate env or world) ----------------------------


static func _condition_true(ctx: Dictionary) -> int:
	ctx["result"] = true
	return Dispatch.Status.OK


static func _condition_false(ctx: Dictionary) -> int:
	ctx["result"] = false
	return Dispatch.Status.OK


static func _condition_counter(ctx: Dictionary) -> int:
	# COUNTER(COUNTER, COMPARISON, INT)
	var args: SageScriptArgs = ctx["args"]
	ctx["result"] = ParamTypes.compare_int(
		(ctx["env"] as SageScriptEnv).counter(args.text(0)),
		args.integer(1),
		args.integer(2)
	)
	return Dispatch.Status.OK


static func _condition_counter_counter(ctx: Dictionary) -> int:
	# COUNTER_COUNTER(COUNTER, COMPARISON, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	ctx["result"] = ParamTypes.compare_int(
		env.counter(args.text(0)), args.integer(1), env.counter(args.text(2))
	)
	return Dispatch.Status.OK


static func _condition_flag(ctx: Dictionary) -> int:
	# FLAG(FLAG, BOOLEAN)
	var args: SageScriptArgs = ctx["args"]
	ctx["result"] = (ctx["env"] as SageScriptEnv).flag(args.text(0)) == args.boolean(1, true)
	return Dispatch.Status.OK


static func _condition_timer_expired(ctx: Dictionary) -> int:
	# TIMER_EXPIRED(COUNTER)
	ctx["result"] = (ctx["env"] as SageScriptEnv).timer_expired(
		(ctx["args"] as SageScriptArgs).text(0)
	)
	return Dispatch.Status.OK
