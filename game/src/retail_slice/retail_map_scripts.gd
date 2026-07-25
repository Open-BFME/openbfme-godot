class_name RetailMapScripts
extends RefCounted

## Deterministic bounded interpreter for decoded SAGE WorldBuilder map scripts.
##
## Input format: the ``openbfme.map-scripts`` document produced by the importer
## converter ``importer/openbfme_importer/sage_scripts.py`` (schemaVersion 1),
## or, for fixtures and the older skirmish contract extractor, the bare decoded
## Script payload dictionaries that document embeds under ``scripts[i].payload``.
##
## Execution model: call step(sim) once per sim tick after sim.tick(). Every
## active, non-subroutine script whose evaluation interval is due evaluates its
## condition blocks (OrCondition records are OR'd; Condition records inside one
## block are AND'd), then executes its ScriptAction records on true or its
## ScriptActionFalse records on false. CALL_SUBROUTINE runs a named subroutine
## script inline, bounded by MAX_SUBROUTINE_DEPTH and MAX_SUBROUTINE_CALLS.
##
## Determinism: interpreter state (counters, flags, timers, object lists,
## script activity, objective state, the event log) is a pure function of the
## loaded document and the tick index. SET_RANDOM_COUNTER draws from an
## internal splitmix64 stream seeded from the document, never from
## RandomNumberGenerator. Nothing allocates without a bound: the event log,
## object lists, and subroutine recursion are all capped and overflow is
## counted, not silently dropped.
##
## Fail-closed accounting, three buckets, recorded at load time for every
## opcode occurrence and again for every occurrence actually reached:
##   * ``implemented``  - opcodes with real interpreter/simulation semantics.
##   * ``recorded``     - retail presentation opcodes (UI, audio, camera,
##                        objectives, win/lose). These emit a deterministic
##                        event and change no simulation state. They are NEVER
##                        counted as gameplay coverage.
##   * ``unimplemented``- everything else. An unimplemented condition makes its
##                        AND-block false regardless of inversion; an
##                        unimplemented action does nothing. Both are counted.
##
## SEMANTIC_CONDITIONS / SEMANTIC_ACTIONS / RECORDED_ACTIONS below are mirrored
## by SEMANTIC_CONDITION_OPCODES / SEMANTIC_ACTION_OPCODES /
## RECORDED_ACTION_OPCODES in sage_scripts.py, and
## importer/tests/test_sage_scripts.py parses this file to assert they are
## identical. The converter's coverage census therefore cannot claim support
## this interpreter does not have.

# Matches RetailSliceSim.TICK_SECONDS (10 logic ticks per second).
const TICK_SECONDS := 0.1

# SAGE comparison enum used by the COUNTER condition's argumentType-6 slot.
const COMPARE_LESS := 0
const COMPARE_LESS_EQUAL := 1
const COMPARE_EQUAL := 2
const COMPARE_GREATER_EQUAL := 3
const COMPARE_GREATER := 4
const COMPARE_NOT_EQUAL := 5

# Decoded argument type codes, each observed in the retail campaign corpus.
const ARGUMENT_INTEGER := 0
const ARGUMENT_REAL := 1
const ARGUMENT_SCRIPT_NAME := 2
const ARGUMENT_TEAM := 3
const ARGUMENT_COUNTER_NAME := 4
const ARGUMENT_FLAG_NAME := 5
const ARGUMENT_COMPARISON := 6
const ARGUMENT_WAYPOINT := 7
const ARGUMENT_BOOLEAN := 8
const ARGUMENT_TRIGGER_AREA := 9
const ARGUMENT_PLAYER := 11
const ARGUMENT_SOUND := 12
const ARGUMENT_SUBROUTINE := 13
const ARGUMENT_UNIT_NAME := 14
const ARGUMENT_OBJECT_TYPE := 15
const ARGUMENT_POSITION := 16
const ARGUMENT_DIALOG := 21
const ARGUMENT_WAYPOINT_PATH := 24
const ARGUMENT_LOCALIZED_STRING := 25
const ARGUMENT_CAMERA_WAYPOINT := 51
const ARGUMENT_OBJECT_LIST := 48
const ARGUMENT_NOTIFICATION_KIND := 77

# Bounds. Every one of these is a hard cap; hitting one increments a counter
# rather than growing an allocation or recursing further.
const MAX_EVENTS := 8192
const MAX_SUBROUTINE_DEPTH := 8
const MAX_SUBROUTINE_CALLS_PER_TICK := 256
const MAX_OBJECT_LIST_ENTRIES := 512
const MAX_OBJECT_LISTS := 512
const MAX_COUNTERS := 4096
const MAX_FLAGS := 4096
const MAX_TIMERS := 4096

const SEMANTIC_CONDITIONS := {
	"CONDITION_TRUE": true,
	"CONDITION_FALSE": true,
	"COUNTER": true,
	"COUNTER_COUNTER": true,
	"COUNTER_SECONDS": true,
	"FLAG": true,
	"TIMER_EXPIRED": true,
}

const SEMANTIC_ACTIONS := {
	"CALL_SUBROUTINE": true,
	"DISABLE_SCRIPT": true,
	"ENABLE_SCRIPT": true,
	"INCREMENT_COUNTER": true,
	"NO_OP": true,
	"OBJECTLIST_ADDOBJECTTYPE": true,
	"PLAYER_SET_MONEY": true,
	"SET_COUNTER": true,
	"SET_FLAG": true,
	"SET_MILLISECOND_TIMER": true,
	"SET_RANDOM_COUNTER": true,
	"SET_TIMER": true,
}

const RECORDED_ACTIONS := {
	"CAMERA_LETTERBOX_BEGIN": true,
	"CAMERA_LETTERBOX_END": true,
	"CLOSE_OBJECTIVES_SCREEN": true,
	"DEFEAT": true,
	"DISABLE_INPUT": true,
	"DISPLAY_COUNTDOWN_TIMER": true,
	"DISPLAY_NOTIFICATION_BOX": true,
	"ENABLE_INPUT": true,
	"ENABLE_OBJECTIVES_SCREEN": true,
	"FLASH_OBJECTIVES_BUTTON": true,
	"HIDE_MISSION_OBJECTIVE": true,
	"MARK_MISSION_OBJECTIVE_COMPLETED": true,
	"MOVE_CAMERA_TO": true,
	"PLAY_SOUND_EFFECT": true,
	"PLAY_SOUND_EFFECT_AT": true,
	"PLAY_SOUND_EFFECT_AT_TEAM": true,
	"QUICKVICTORY": true,
	"RESET_CAMERA": true,
	"SHOW_MILITARY_CAPTION": true,
	"SHOW_MISSION_OBJECTIVE": true,
	"SOUND_PLAY_NAMED": true,
	"SPEECH_PLAY": true,
	"VICTORY": true,
	"VICTORY_SCREEN": true,
	"ZOOM_CAMERA": true,
}

const RECORDED_CONDITIONS := {}

# Interpreter variable state. Counter and flag namespaces are shared across
# all loaded scripts, matching the retail per-player script environment.
var counters: Dictionary = {}
var flags: Dictionary = {}
# Timer name -> absolute interpreter tick at which the timer expires. A timer
# stays expired once its tick has passed, matching retail TIMER_EXPIRED.
var timers: Dictionary = {}
# Object-list name -> Array[String] of object type names (OBJECTLIST_ADD...).
var object_lists: Dictionary = {}
# Mission objective id -> {"shown": bool, "completed": bool}.
var mission_objectives: Dictionary = {}
var tick_index: int = 0

# Presentation surface. `events` is a bounded, ordered, deterministic log the
# future campaign presentation layer consumes; it never feeds back into the
# simulation, so it cannot perturb lockstep.
var events: Array[Dictionary] = []
var events_dropped: int = 0
var input_enabled: bool = true
var letterbox_active: bool = false
# "", "victory" or "defeat". First declaration wins.
var outcome: String = ""

# Decoded map world (waypoints, trigger areas, teams, players, named objects)
# from the converter's `world` section. Empty when only bare payloads were
# loaded; every world-dependent opcode stays unimplemented in that case.
var world: Dictionary = {}

# Player-reference text (for example "<This Player>") -> sim team integer.
var player_team_bindings: Dictionary = {}
var default_player_team: int = 0

# Load-time opcode census (fail-closed accounting).
var implemented: Dictionary = {}
var recorded: Dictionary = {}
var unimplemented: Dictionary = {}
# Opcodes actually reached while stepping that this interpreter does not honour.
var runtime_unimplemented: Dictionary = {}
# Bound-overflow accounting, so a truncated run is never mistaken for a clean one.
var bounds_hit: Dictionary = {}

var _scripts: Array[Dictionary] = []
# Script name -> Array[int] of indices into _scripts (retail permits duplicates).
var _by_name: Dictionary = {}
var _rng_state: int = 0x9E3779B97F4A7C15
var _subroutine_calls_this_tick: int = 0


# --- Loading ---------------------------------------------------------------


func load_document(document: Dictionary) -> int:
	## Loads a full `openbfme.map-scripts` document, world included.
	var world_value: Variant = document.get("world", {})
	if world_value is Dictionary and bool((world_value as Dictionary).get("available", false)):
		world = world_value
	var loaded := 0
	for row: Variant in Array(document.get("scripts", [])):
		var payload: Variant = (row as Dictionary).get("payload", {})
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	# Seed the deterministic draw stream from the document identity so two
	# runs of the same mission draw the same sequence and two different
	# missions do not share one.
	var source: Dictionary = document.get("source", {})
	_rng_state = _seed_from_text(String(source.get("sourceSha256", "")))
	return loaded


func load_contract_source(source: Dictionary) -> int:
	## Loads every script from one `sources[i]` entry of the contract JSON.
	var loaded := 0
	for row: Variant in Array(source.get("scripts", [])):
		var payload: Dictionary = (row as Dictionary).get("payload", {})
		if not payload.is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payloads(payloads: Array) -> int:
	var loaded := 0
	for payload: Variant in payloads:
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payload(payload: Dictionary) -> void:
	var condition_blocks: Array[Array] = []
	var true_actions: Array[Dictionary] = []
	var false_actions: Array[Dictionary] = []
	for record: Variant in Array(payload.get("records", [])):
		var record_row: Dictionary = record
		var record_name := String(record_row.get("name", ""))
		var value: Dictionary = record_row.get("value", {})
		if record_name == "OrCondition":
			var block: Array[Dictionary] = []
			for child: Variant in Array(value.get("records", [])):
				var child_row: Dictionary = child
				if String(child_row.get("name", "")) == "Condition":
					block.append(_content_row(child_row.get("value", {}), true))
			condition_blocks.append(block)
		elif record_name == "ScriptAction":
			true_actions.append(_content_row(value, false))
		elif record_name == "ScriptActionFalse":
			false_actions.append(_content_row(value, false))
	var interval_seconds := float(payload.get("evaluationInterval", 0))
	var script_name := String(payload.get("name", ""))
	var index := _scripts.size()
	_scripts.append({
		"name": script_name,
		"active": bool(payload.get("isActive", true)),
		"subroutine": bool(payload.get("isSubroutine", false)),
		"deactivate_upon_success": bool(payload.get("deactivateUponSuccess", false)),
		"eval_interval_ticks": maxi(1, int(round(interval_seconds / TICK_SECONDS))) if interval_seconds > 0.0 else 1,
		"condition_blocks": condition_blocks,
		"true_actions": true_actions,
		"false_actions": false_actions,
	})
	if not _by_name.has(script_name):
		_by_name[script_name] = PackedInt32Array()
	var bucket: PackedInt32Array = _by_name[script_name]
	bucket.append(index)
	_by_name[script_name] = bucket


func script_count() -> int:
	return _scripts.size()


func active_script_names() -> Array[String]:
	var names: Array[String] = []
	for state in _scripts:
		if bool(state["active"]):
			names.append(String(state["name"]))
	return names


func coverage_summary() -> Dictionary:
	## Slot totals per bucket, for reporting. Recorded slots are presentation
	## only and are deliberately reported apart from implemented slots.
	return {
		"implementedSlots": _sum_values(implemented),
		"recordedSlots": _sum_values(recorded),
		"unimplementedSlots": _sum_values(unimplemented),
		"distinctUnimplemented": unimplemented.size(),
	}


# --- Stepping --------------------------------------------------------------


func step(sim) -> void:
	## Advances the interpreter by one tick against the supplied RetailSliceSim.
	tick_index += 1
	_subroutine_calls_this_tick = 0
	# Iterate by index over a snapshot size: ENABLE_SCRIPT may only flip
	# activity flags on already-loaded scripts, never append, so the bound is
	# fixed for the whole tick.
	var count := _scripts.size()
	for index in range(count):
		var state: Dictionary = _scripts[index]
		if not bool(state["active"]) or bool(state["subroutine"]):
			continue
		var interval := int(state["eval_interval_ticks"])
		if interval > 1 and tick_index % interval != 0:
			continue
		_run_script(state, sim, 0)


func _run_script(state: Dictionary, sim, depth: int) -> void:
	var fired := _evaluate_condition_blocks(state["condition_blocks"])
	var actions: Array[Dictionary] = state["true_actions"] if fired else state["false_actions"]
	for action in actions:
		if bool(action["enabled"]):
			_execute_action(action, sim, depth)
	if fired and bool(state["deactivate_upon_success"]):
		state["active"] = false


func _content_row(value: Dictionary, is_condition: bool) -> Dictionary:
	var internal: Dictionary = value.get("internalName", {})
	var opcode := String(internal.get("name", "<missing>"))
	var row := {
		"opcode": opcode,
		"arguments": Array(value.get("arguments", [])),
		"enabled": bool(value.get("enabled", true)),
	}
	if is_condition:
		row["inverted"] = bool(value.get("inverted", false))
	var semantic: Dictionary = SEMANTIC_CONDITIONS if is_condition else SEMANTIC_ACTIONS
	var presentation: Dictionary = RECORDED_CONDITIONS if is_condition else RECORDED_ACTIONS
	var bucket := unimplemented
	if semantic.has(opcode):
		bucket = implemented
	elif presentation.has(opcode):
		bucket = recorded
	bucket[opcode] = int(bucket.get(opcode, 0)) + 1
	return row


func _evaluate_condition_blocks(blocks: Array[Array]) -> bool:
	# OrCondition blocks are OR'd; conditions inside one block are AND'd.
	# A script without any condition block never fires (fail-closed).
	for block: Array in blocks:
		var block_true := true
		var considered := false
		for condition: Dictionary in block:
			if not bool(condition["enabled"]):
				continue
			considered = true
			if not _evaluate_condition(condition):
				block_true = false
				break
		if block_true and considered:
			return true
	return false


func _evaluate_condition(condition: Dictionary) -> bool:
	var opcode := String(condition["opcode"])
	var arguments: Array = condition["arguments"]
	var inverted := bool(condition.get("inverted", false))
	var result: bool
	match opcode:
		"CONDITION_TRUE":
			result = true
		"CONDITION_FALSE":
			result = false
		"COUNTER":
			# Retail signature: [counter name, comparison, integer].
			var name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var value := _int_at(arguments, 2, ARGUMENT_INTEGER, 0)
			result = _compare(int(counters.get(name, 0)), comparison, value)
		"COUNTER_COUNTER":
			# Retail signature: [counter name, comparison, counter name].
			var left := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison2 := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var right := _text_at(arguments, 2, ARGUMENT_COUNTER_NAME)
			result = _compare(
				int(counters.get(left, 0)), comparison2, int(counters.get(right, 0))
			)
		"COUNTER_SECONDS":
			# Retail signature: [timer name, comparison, real seconds].
			# Compares the timer's remaining seconds; an unarmed timer reads 0.
			var timer_name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison3 := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var seconds := _real_at(arguments, 2, ARGUMENT_REAL, 0.0)
			var remaining_ticks := 0
			if timers.has(timer_name):
				remaining_ticks = maxi(0, int(timers[timer_name]) - tick_index)
			# Whole-tick comparison keeps this integral and therefore exactly
			# reproducible across platforms; retail timers are tick-quantised.
			result = _compare(
				remaining_ticks, comparison3, int(floor(seconds / TICK_SECONDS))
			)
		"FLAG":
			var flag_name := _text_at(arguments, 0, ARGUMENT_FLAG_NAME)
			var expected := _int_at(arguments, 1, ARGUMENT_BOOLEAN, 1) != 0
			result = bool(flags.get(flag_name, false)) == expected
		"TIMER_EXPIRED":
			var expiring := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			result = timers.has(expiring) and tick_index >= int(timers[expiring])
		_:
			# Fail-closed: an unimplemented condition makes its AND-block
			# false regardless of inversion, and the miss is counted.
			runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1
			return false
	return not result if inverted else result


func _execute_action(action: Dictionary, sim, depth: int) -> void:
	var opcode := String(action["opcode"])
	var arguments: Array = action["arguments"]
	match opcode:
		"NO_OP":
			pass
		"SET_COUNTER":
			# Retail signature: [counter name, integer].
			_set_counter(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				_int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			)
		"INCREMENT_COUNTER":
			# Retail signature: [integer amount, counter name].
			var name := _text_at(arguments, 1, ARGUMENT_COUNTER_NAME)
			_set_counter(
				name,
				int(counters.get(name, 0)) + _int_at(arguments, 0, ARGUMENT_INTEGER, 1)
			)
		"SET_RANDOM_COUNTER":
			# Retail signature: [counter name, low, high], inclusive.
			var random_name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var low := _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			var high := _int_at(arguments, 2, ARGUMENT_INTEGER, 0)
			_set_counter(random_name, _draw_inclusive(low, high))
		"SET_FLAG":
			# Retail signature: [flag name, boolean].
			if flags.size() < MAX_FLAGS or flags.has(_text_at(arguments, 0, ARGUMENT_FLAG_NAME)):
				flags[_text_at(arguments, 0, ARGUMENT_FLAG_NAME)] = (
					_int_at(arguments, 1, ARGUMENT_BOOLEAN, 1) != 0
				)
			else:
				_note_bound("flags")
		"SET_MILLISECOND_TIMER":
			# Retail signature: [timer name, real milliseconds].
			_arm_timer(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				maxi(1, int(ceil(
					_real_at(arguments, 1, ARGUMENT_REAL, 0.0) / (TICK_SECONDS * 1000.0)
				)))
			)
		"SET_TIMER":
			# Retail signature: [timer name, integer seconds].
			_arm_timer(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				maxi(1, int(round(
					float(_int_at(arguments, 1, ARGUMENT_INTEGER, 0)) / TICK_SECONDS
				)))
			)
		"ENABLE_SCRIPT":
			_set_script_active(_text_at(arguments, 0, ARGUMENT_SCRIPT_NAME), true)
		"DISABLE_SCRIPT":
			_set_script_active(_text_at(arguments, 0, ARGUMENT_SCRIPT_NAME), false)
		"CALL_SUBROUTINE":
			_call_subroutine(_text_at(arguments, 0, ARGUMENT_SUBROUTINE), sim, depth)
		"OBJECTLIST_ADDOBJECTTYPE":
			# Retail signature: [object-list name, object type].
			_object_list_add(
				_text_at(arguments, 0, ARGUMENT_OBJECT_LIST),
				_text_at(arguments, 1, ARGUMENT_OBJECT_TYPE)
			)
		"PLAYER_SET_MONEY":
			# Retail signature: [player, integer]. The only simulation write.
			var player := _text_at(arguments, 0, ARGUMENT_PLAYER)
			var amount := _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			var team := int(player_team_bindings.get(player, default_player_team))
			sim.team_resources[team] = amount
		_:
			if RECORDED_ACTIONS.has(opcode):
				_record_presentation(opcode, arguments)
			else:
				runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1


# --- Semantic helpers ------------------------------------------------------


func _set_counter(name: String, value: int) -> void:
	if counters.has(name) or counters.size() < MAX_COUNTERS:
		counters[name] = value
	else:
		_note_bound("counters")


func _arm_timer(name: String, duration_ticks: int) -> void:
	if timers.has(name) or timers.size() < MAX_TIMERS:
		timers[name] = tick_index + duration_ticks
	else:
		_note_bound("timers")


func _set_script_active(name: String, active: bool) -> void:
	if not _by_name.has(name):
		# Retail scripts routinely name a script in another player's list. We
		# cannot honour that from a single loaded environment, so record the
		# miss instead of pretending the toggle landed.
		_note_bound("unknown_script_name")
		return
	for index: int in (_by_name[name] as PackedInt32Array):
		(_scripts[index] as Dictionary)["active"] = active


func _call_subroutine(name: String, sim, depth: int) -> void:
	if depth >= MAX_SUBROUTINE_DEPTH:
		_note_bound("subroutine_depth")
		return
	if _subroutine_calls_this_tick >= MAX_SUBROUTINE_CALLS_PER_TICK:
		_note_bound("subroutine_calls")
		return
	if not _by_name.has(name):
		_note_bound("unknown_script_name")
		return
	for index: int in (_by_name[name] as PackedInt32Array):
		_subroutine_calls_this_tick += 1
		if _subroutine_calls_this_tick > MAX_SUBROUTINE_CALLS_PER_TICK:
			_note_bound("subroutine_calls")
			return
		_run_script(_scripts[index] as Dictionary, sim, depth + 1)


func _object_list_add(list_name: String, object_type: String) -> void:
	if not object_lists.has(list_name):
		if object_lists.size() >= MAX_OBJECT_LISTS:
			_note_bound("object_lists")
			return
		object_lists[list_name] = PackedStringArray()
	var entries: PackedStringArray = object_lists[list_name]
	if entries.size() >= MAX_OBJECT_LIST_ENTRIES:
		_note_bound("object_list_entries")
		return
	if entries.has(object_type):
		return
	entries.append(object_type)
	object_lists[list_name] = entries


# --- Presentation recording -----------------------------------------------


func _record_presentation(opcode: String, arguments: Array) -> void:
	var event := {"tick": tick_index, "opcode": opcode}
	match opcode:
		"SHOW_MISSION_OBJECTIVE":
			var shown := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(shown)["shown"] = true
			event["objective"] = shown
		"HIDE_MISSION_OBJECTIVE":
			var hidden := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(hidden)["shown"] = false
			event["objective"] = hidden
		"MARK_MISSION_OBJECTIVE_COMPLETED":
			var done := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(done)["completed"] = true
			event["objective"] = done
		"FLASH_OBJECTIVES_BUTTON":
			event["objective"] = _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
		"DISPLAY_NOTIFICATION_BOX":
			event["kind"] = _text_at(arguments, 0, ARGUMENT_NOTIFICATION_KIND)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
		"SHOW_MILITARY_CAPTION":
			event["text"] = _text_at(arguments, 0, ARGUMENT_LOCALIZED_STRING)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"DISPLAY_COUNTDOWN_TIMER":
			event["timer"] = _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
		"SPEECH_PLAY":
			event["speech"] = _text_at(arguments, 0, ARGUMENT_DIALOG)
		"PLAY_SOUND_EFFECT", "PLAY_SOUND_EFFECT_AT", "PLAY_SOUND_EFFECT_AT_TEAM", "SOUND_PLAY_NAMED":
			event["sound"] = _text_at(arguments, 0, ARGUMENT_SOUND)
		"MOVE_CAMERA_TO":
			event["target"] = _text_at(arguments, 0, ARGUMENT_CAMERA_WAYPOINT)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"RESET_CAMERA":
			event["target"] = _text_at(arguments, 0, ARGUMENT_WAYPOINT)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"DISABLE_INPUT":
			input_enabled = false
		"ENABLE_INPUT":
			input_enabled = true
		"CAMERA_LETTERBOX_BEGIN":
			letterbox_active = true
		"CAMERA_LETTERBOX_END":
			letterbox_active = false
		"VICTORY", "QUICKVICTORY", "VICTORY_SCREEN":
			if outcome == "":
				outcome = "victory"
		"DEFEAT":
			if outcome == "":
				outcome = "defeat"
	if events.size() >= MAX_EVENTS:
		events_dropped += 1
		return
	events.append(event)


func _objective(id: int) -> Dictionary:
	if not mission_objectives.has(id):
		mission_objectives[id] = {"shown": false, "completed": false}
	return mission_objectives[id]


func _note_bound(reason: String) -> void:
	bounds_hit[reason] = int(bounds_hit.get(reason, 0)) + 1


# --- Deterministic draw stream --------------------------------------------


func _seed_from_text(text: String) -> int:
	# FNV-1a over the document identity, so the stream is a pure function of
	# the mission and never of wall-clock time or engine RNG state.
	var hash_value := 0x2545F4914F6CDD1D
	for index in range(text.length()):
		hash_value ^= text.unicode_at(index)
		hash_value *= 0x100000001B3
	if hash_value == 0:
		hash_value = 0x9E3779B97F4A7C15
	return hash_value


func _next_random() -> int:
	# splitmix64. Integer-only, so it is bit-identical on every platform.
	_rng_state += 0x9E3779B97F4A7C15
	var z := _rng_state
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
	z = (z ^ (z >> 27)) * 0x94D049BB133111EB
	return z ^ (z >> 31)


func _draw_inclusive(low: int, high: int) -> int:
	var lower := mini(low, high)
	var upper := maxi(low, high)
	var span := upper - lower + 1
	if span <= 1:
		return lower
	var draw := _next_random()
	if draw < 0:
		draw = -(draw + 1)
	return lower + (draw % span)


# --- Argument access -------------------------------------------------------
#
# Retail argument lists are positional. Each accessor takes both the slot index
# and the argumentType the retail corpus proves belongs in that slot, and
# fails closed to the supplied fallback when the wire disagrees, so a decoded
# shape we have not seen cannot be silently misread as a different type.


func _argument_at(arguments: Array, index: int, argument_type: int) -> Dictionary:
	if index < 0 or index >= arguments.size():
		return {}
	var row: Dictionary = arguments[index]
	if int(row.get("argumentType", -1)) != argument_type:
		return {}
	return row


func _text_at(arguments: Array, index: int, argument_type: int) -> String:
	return String(_argument_at(arguments, index, argument_type).get("text", ""))


func _int_at(arguments: Array, index: int, argument_type: int, fallback: int) -> int:
	var row := _argument_at(arguments, index, argument_type)
	return int(row.get("integer", fallback)) if not row.is_empty() else fallback


func _real_at(arguments: Array, index: int, argument_type: int, fallback: float) -> float:
	var row := _argument_at(arguments, index, argument_type)
	return float(row.get("real", fallback)) if not row.is_empty() else fallback


func _compare(left: int, comparison: int, right: int) -> bool:
	match comparison:
		COMPARE_LESS:
			return left < right
		COMPARE_LESS_EQUAL:
			return left <= right
		COMPARE_EQUAL:
			return left == right
		COMPARE_GREATER_EQUAL:
			return left >= right
		COMPARE_GREATER:
			return left > right
		COMPARE_NOT_EQUAL:
			return left != right
	return false


func _sum_values(histogram: Dictionary) -> int:
	var total := 0
	for value: Variant in histogram.values():
		total += int(value)
	return total
