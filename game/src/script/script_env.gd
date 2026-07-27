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
##
## WHERE THE STATE LIVES (the snapshot/hash boundary)
## --------------------------------------------------
## counters, flags, timers, script_enabled and tick_index are SIM-OUTCOME-
## BEARING STATE: script actions mutate them mid-match and later actions and
## conditions read them, so under lockstep every peer must carry identical
## values and a snapshot must reproduce them (the same defect class e56a0d4
## closed for unit references). They are therefore exposed as properties over
## ONE of two backings:
##
##   * STANDALONE (default): plain local dictionaries, exactly the historical
##     behaviour. Tests and the executor construct the env with no simulation
##     and nothing changes for them.
##   * ATTACHED: after attach_state_store(store, key) - installed by
##     RetailSliceSim.attach_script_env(env, team) - every read and write goes
##     through the sim-owned `script_env_state[team]` entry, which the sim
##     hashes, snapshots, restores and clears with the rest of its match
##     state. Attachment holds the sim's Dictionary BY REFERENCE (Godot
##     dictionaries are shared), so the env needs no object reference to the
##     sim and the sim's setup()/restore() are visible to the env instantly -
##     which is also why those sim paths must mutate the dictionary IN PLACE
##     rather than rebinding the variable.
##
## ticks_per_second and retail_frames_per_second are match CONFIGURATION, not
## state: nothing mutates them mid-match, and every tick count they produce
## lands in `timers`, which IS hashed - so a misconfigured peer diverges
## visibly on the first conversion rather than silently. frame_conversions is
## a DIAGNOSTIC (nothing in script logic reads it back); it stays process-
## local even when attached, like the executor's own tallies.

## Interpreter tick rate. Matches RetailSliceSim.TICK_SECONDS (0.1 s).
var ticks_per_second: float = 10.0

## Assumed retail logic rate used to convert SET_TIMER frame counts to ticks.
var retail_frames_per_second: float = 30.0

## Standalone backing (see the class comment). Attached envs ignore these.
var _local_state: Dictionary = {
	"counters": {},
	"flags": {},
	"timers": {},
	"script_enabled": {},
}
var _local_tick_index: int = 0

## Sim-owned backing when attached: _shared_store[_shared_key] is this env's
## entry ({"tick": int, "counters": {}, "flags": {}, "timers": {},
## "script_enabled": {}}), materialized lazily so an untouched env leaves the
## store byte-empty (the sim's empty-is-absent hash discipline).
var _shared_store: Dictionary = {}
var _shared_key: Variant = null
var _attached := false

## Lifetime witness for the shared store's OWNER (installed by
## RetailSliceSim.attach_script_env, absent when a test attaches a bare
## dictionary). Godot dictionaries are refcounted, so when the owning sim is
## freed the store this env holds stays alive as an ORPHAN: every read and
## write would keep "working" against state that nothing hashes, snapshots or
## clears any more - the exact silent-divergence shape this repo bans. The
## witness is a WeakRef to the owner, checked on every attached access; a dead
## witness makes the env refuse loudly instead of succeeding silently.
var _witness: WeakRef = null
var _stale_reported := false

var counters: Dictionary:
	get:
		return _collection("counters")

var flags: Dictionary:
	get:
		return _collection("flags")

## name -> {"remaining": float ticks, "running": bool}
var timers: Dictionary:
	get:
		return _collection("timers")

## script name -> bool. Absent means "the loader's own default applies".
var script_enabled: Dictionary:
	get:
		return _collection("script_enabled")

var tick_index: int:
	get:
		if not _attached:
			return _local_tick_index
		if attachment_stale():
			_report_stale()
			return 0
		return int((_shared_store.get(_shared_key, {}) as Dictionary).get("tick", 0))
	set(value):
		if _attached:
			if attachment_stale():
				_report_stale()
				return
			_shared_entry()["tick"] = value
		else:
			_local_tick_index = value

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


# --- State backing (standalone vs sim-attached) ----------------------------


## Route every counter/flag/timer/enable-bit read and write through a shared,
## sim-owned store so the script engine's state sits inside the simulation's
## snapshot/hash boundary. `store` is held by reference; `key` is this env's
## namespace in it (the sim uses the script player's team id, matching the
## per-player retail script context and script_unit_references' keying).
##
## Refuses (false + push_error) rather than guessing when:
##   * the env is already attached - re-aiming an env would silently strand
##     the state it wrote through the first store;
##   * the env has local state - migrating it would smuggle unhashed history
##     into the boundary; attach at construction time, before any script runs.
## An EXISTING entry under `key` is adopted as-is: that is exactly the
## restore-then-attach path a snapshot-adopting peer takes.
##
## `witness` names the OBJECT whose lifetime the store is tied to (the sim).
## When supplied, every attached access first proves the witness is still
## alive; accessing the store of a freed owner refuses loudly (see _witness).
## Held by WeakRef only - no strong reference, no RefCounted cycle, and no
## layering dependency from the script engine onto the simulation.
func attach_state_store(store: Dictionary, key: Variant, witness: Object = null) -> bool:
	if _attached:
		push_error("SageScriptEnv: already attached to a shared state store")
		return false
	if _local_tick_index != 0 \
			or not (_local_state["counters"] as Dictionary).is_empty() \
			or not (_local_state["flags"] as Dictionary).is_empty() \
			or not (_local_state["timers"] as Dictionary).is_empty() \
			or not (_local_state["script_enabled"] as Dictionary).is_empty():
		push_error(
			"SageScriptEnv: refusing to attach an env that already holds local "
			+ "state - attach before any script mutates it"
		)
		return false
	_shared_store = store
	_shared_key = key
	_witness = weakref(witness) if witness != null else null
	_attached = true
	return true


func is_attached() -> bool:
	return _attached


## True when this env attached with `owner` as its lifetime witness and the
## witness is still alive. The registration choke point
## (RetailSliceSim.register_script_executor) uses this to refuse an executor
## whose env is attached to a DIFFERENT sim - or to none at all.
func attached_to(owner: Object) -> bool:
	return _attached and _witness != null and _witness.get_ref() == owner


## The namespace key this env is attached under (the sim uses the script
## player's TEAM id), or null for a standalone env. The registration choke
## point reads this to refuse an executor whose env is keyed to a different
## team than the registration asks for - attached-to-the-right-sim alone is
## not enough (the 0dce37e review registered a team-0 env under team 1 and
## it ran in the wrong slot for 20 ticks with zero faults).
func attachment_key() -> Variant:
	return _shared_key if _attached else null


## True when the object whose store this env writes has been freed: the store
## is an orphan nothing hashes or snapshots. Every accessor below refuses
## (loudly, once) while this holds. Always false for a standalone env and for
## an attachment made without a witness (bare-dictionary test attachments).
func attachment_stale() -> bool:
	return _attached and _witness != null and _witness.get_ref() == null


func _report_stale() -> void:
	## push_error ONCE per env, not per access: the first orphaned access is
	## the defect report; ten thousand copies of it would bury the log.
	if _stale_reported:
		return
	_stale_reported = true
	push_error(
		"SageScriptEnv: the simulation owning this env's state store has been "
		+ "freed; refusing every further read and write - the store is an "
		+ "orphan outside any snapshot/hash boundary"
	)


func _shared_entry() -> Dictionary:
	if not _shared_store.has(_shared_key):
		_shared_store[_shared_key] = {}
	return _shared_store[_shared_key]


func _collection(name: String) -> Dictionary:
	if not _attached:
		return _local_state[name]
	if attachment_stale():
		# Refuse: hand back a throwaway dictionary so a write lands NOWHERE
		# (visibly, via the error above) instead of mutating the orphan.
		_report_stale()
		return {}
	var entry := _shared_entry()
	if not entry.has(name):
		entry[name] = {}
	return entry[name]


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
