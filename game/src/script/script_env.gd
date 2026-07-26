class_name SageScriptEnv
extends RefCounted

## The script interpreter's own mutable state: counters, flags, timers and
## per-script enable bits.
##
## Deliberately separate from the world. Everything here is pure bookkeeping the
## script language owns, so it can be exercised, hashed and diffed without a
## simulation. Counter and flag namespaces are global across all loaded scripts,
## which matches the retail per-player script environment.
##
## TIMEBASE (the one number here that is not transcribed from a source)
## -------------------------------------------------------------------
## SAGE map scripts express timers in two units: frames (SET_TIMER) and
## milliseconds (SET_MILLISECOND_TIMER). This interpreter ticks at
## `ticks_per_second` (10, matching RetailSliceSim.TICK_SECONDS), while retail
## SAGE authored frame counts against a 30 Hz logic rate.
## `retail_frames_per_second` is therefore an ASSUMPTION, not a sourced fact:
## the community reference documents the actions but not the logic rate. It is
## exposed as a field so a map or a measurement can override it, and any
## frame-denominated timer converted through it is reported by
## `frame_conversions` so the assumption is visible rather than buried.

## Interpreter tick rate. Matches RetailSliceSim.TICK_SECONDS (0.1 s).
var ticks_per_second: float = 10.0

## Assumed retail logic rate used to convert SET_TIMER frame counts to ticks.
var retail_frames_per_second: float = 30.0

var counters: Dictionary = {}
var flags: Dictionary = {}

## name -> {"remaining": float ticks, "running": bool}
var timers: Dictionary = {}

## script name -> bool. Absent means "the loader's own default applies".
var script_enabled: Dictionary = {}

var tick_index: int = 0

## How many timers were set in frame units and converted through the assumed
## retail logic rate. Non-zero means the assumption above is load-bearing.
var frame_conversions: int = 0


## Installed by whatever actually runs scripts, so CALL_SUBROUTINE can run one.
##
## Signature: func(subroutine_name: String) -> bool, returning whether the
## subroutine was found and executed. It is a Callable rather than a hard
## reference because the interpreter that owns script bodies does not exist yet
## and the environment must not grow a dependency on it.
##
## If nothing installs one, CALL_SUBROUTINE reports UNSUPPORTED and the
## dispatcher records a gap. It deliberately does NOT queue the call for a
## consumer that may never arrive: a queue nobody drains is a silent no-op
## wearing a data structure.
var subroutine_runner: Callable = Callable()


func milliseconds_per_tick() -> float:
	return 1000.0 / ticks_per_second


func ticks_from_seconds(seconds: float) -> int:
	## Seconds -> interpreter ticks, rounded up, matching the timer conversion so
	## a counter set to N seconds and a timer set to N seconds agree exactly.
	return int(ceil(seconds * ticks_per_second))


func has_subroutine_runner() -> bool:
	return subroutine_runner.is_valid()


func call_subroutine(name: String) -> bool:
	if not has_subroutine_runner():
		return false
	return bool(subroutine_runner.call(name))


func advance() -> void:
	## One interpreter tick. Call before evaluating scripts for that tick.
	tick_index += 1
	for name: Variant in timers:
		var timer: Dictionary = timers[name]
		if bool(timer["running"]):
			timer["remaining"] = float(timer["remaining"]) - 1.0


# --- Counters -------------------------------------------------------------


func counter(name: String) -> int:
	return int(counters.get(name, 0))


func set_counter(name: String, value: int) -> void:
	counters[name] = value


func add_counter(name: String, delta: int) -> void:
	counters[name] = counter(name) + delta


# --- Flags ----------------------------------------------------------------


func flag(name: String) -> bool:
	return bool(flags.get(name, false))


func set_flag(name: String, value: bool) -> void:
	flags[name] = value


# --- Timers ---------------------------------------------------------------


func set_timer_ticks(name: String, ticks: float) -> void:
	timers[name] = {"remaining": maxf(1.0, ticks), "running": true}


func set_timer_milliseconds(name: String, milliseconds: float) -> void:
	set_timer_ticks(name, ceil(milliseconds / milliseconds_per_tick()))


func set_timer_frames(name: String, frames: int) -> void:
	frame_conversions += 1
	set_timer_ticks(name, ceil(float(frames) * ticks_per_second / retail_frames_per_second))


func adjust_timer_milliseconds(name: String, milliseconds: float) -> bool:
	if not timers.has(name):
		return false
	var timer: Dictionary = timers[name]
	timer["remaining"] = float(timer["remaining"]) + milliseconds / milliseconds_per_tick()
	return true


func stop_timer(name: String) -> bool:
	if not timers.has(name):
		return false
	timers[name]["running"] = false
	return true


func restart_timer(name: String) -> bool:
	if not timers.has(name):
		return false
	timers[name]["running"] = true
	return true


func timer_expired(name: String) -> bool:
	## A timer that was never set has NOT expired. Retail treats an unset timer
	## as not-expired, and treating it as expired would make an unarmed gate
	## fire, which is exactly the kind of silent wrong answer this repo bans.
	if not timers.has(name):
		return false
	return float(timers[name]["remaining"]) <= 0.0


func timer_remaining_ticks(name: String) -> float:
	if not timers.has(name):
		return 0.0
	return float(timers[name]["remaining"])


# --- Script enable bits ---------------------------------------------------


func set_script_enabled(name: String, enabled: bool) -> void:
	script_enabled[name] = enabled


func is_script_enabled(name: String, fallback: bool) -> bool:
	return bool(script_enabled.get(name, fallback))


# --- Diagnostics ----------------------------------------------------------


func snapshot() -> Dictionary:
	## Order-stable snapshot for equality checks in tests.
	var timer_view := {}
	var timer_names := timers.keys()
	timer_names.sort()
	for name: Variant in timer_names:
		timer_view[name] = [
			float(timers[name]["remaining"]), bool(timers[name]["running"])
		]
	var counter_names := counters.keys()
	counter_names.sort()
	var counter_view := {}
	for name: Variant in counter_names:
		counter_view[name] = int(counters[name])
	var flag_names := flags.keys()
	flag_names.sort()
	var flag_view := {}
	for name: Variant in flag_names:
		flag_view[name] = bool(flags[name])
	return {
		"tick": tick_index,
		"counters": counter_view,
		"flags": flag_view,
		"timers": timer_view,
	}
