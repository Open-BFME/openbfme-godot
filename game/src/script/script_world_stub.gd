class_name SageScriptWorldStub
extends SageScriptWorld

## In-memory SageScriptWorld for headless tests and for exercising the script
## vocabulary before the simulation binding exists.
##
## Deterministic by construction: the random stream is an explicit 32-bit LCG
## seeded from `seed_value`, not Godot's RNG, so results cannot drift with an
## engine version. Two stubs created with the same seed and driven by the same
## call sequence produce byte-identical state.
##
##
## TWO OPPOSITE POLICIES, AND WHY
## ==============================
## The stub treats simulation and presentation differently on purpose.
##
## SIMULATION FACETS FAIL LOUDLY.
##   Any facet method this stub has not been taught calls `_on_refusal`, which
##   push_error()s. In a test, an unimplemented world method almost always means
##   the test forgot to teach the stub - and a quiet refusal there reads as a
##   passing assertion about a handler that never actually ran. A handler test
##   that wants to exercise the refusal path opts in explicitly with
##   `expect_refusals()`; there is no way to get quiet refusals by accident.
##
## PRESENTATION SINKS RECORD EVERYTHING.
##   Camera, audio and UI actions have no observable simulation effect, so the
##   only thing a handler test can assert on is what the handler emitted. The
##   stub's sinks accept every call and keep it (channel, op, positional
##   arguments). This is the design, not a fallback: see the PresentationSink
##   comment in script_world.gd. A recording nothing asserts against is dead
##   code, and reviewers should treat it as such.

## Numeric constants of the classic Numerical Recipes 32-bit LCG. Chosen
## because it is trivially reproducible in any other language a future tool
## might use to cross-check a trace.
const _LCG_MULTIPLIER := 1664525
const _LCG_INCREMENT := 1013904223
const _LCG_MODULUS := 4294967296

var seed_value: int = 1
var frame: int = 0
var money: Dictionary = {}
var debug_log: Array[String] = []
var refuse_capabilities: Dictionary = {}

## Every facet method this stub was asked for and does not implement, in call
## order, as "method: reason". Asserted on by tests that opt into refusals.
var refusal_log: Array[String] = []

## When true (the default) an unimplemented facet method push_error()s. See the
## class comment: quiet refusals in a test are indistinguishable from a passing
## assertion, so silence has to be asked for.
var loud_refusals: bool = true

## Per-player scalars the Players facet answers from. Tests write these.
var player_scalars: Dictionary = {}

## "player|object_type_list|include_dead" -> int, and
## "player|model_condition" -> int. Tests write these through the helpers.
var player_object_counts: Dictionary = {}

var _state: int = 1


func _init(initial_seed: int = 1) -> void:
	seed_value = initial_seed
	_state = initial_seed & 0xFFFFFFFF


func supports(capability: String) -> bool:
	if bool(refuse_capabilities.get(capability, false)):
		return false
	return capability in [CAP_PLAYER_MONEY, CAP_RANDOM, CAP_DEBUG_OUTPUT]


func refuse(capability: String) -> void:
	## Test hook: make this world decline a capability so the WORLD_REFUSED gap
	## path can be exercised.
	refuse_capabilities[capability] = true


func expect_refusals() -> void:
	## Explicit opt-in to quiet refusals, for the tests whose whole point is the
	## refusal path. Everything still lands in `refusal_log`, so the test must
	## still say what it expected - this suppresses the error, not the evidence.
	loud_refusals = false


func _on_refusal(method: String, reason: String) -> void:
	refusal_log.append("%s%s" % [method, "" if reason == "" else ": %s" % reason])
	if loud_refusals:
		push_error(
			(
				"SageScriptWorldStub: unimplemented world method '%s'%s. Teach the "
				+ "stub, or call expect_refusals() if exercising the refusal path "
				+ "is the point of the test."
			) % [method, "" if reason == "" else " (%s)" % reason]
		)


func refused(method: String) -> bool:
	## Whether the named facet method refused at least once.
	for entry: String in refusal_log:
		if entry == method or entry.begins_with("%s:" % method):
			return true
	return false


func advance_frame() -> void:
	frame += 1


func world_frame() -> int:
	return frame


func _next() -> int:
	_state = (_state * _LCG_MULTIPLIER + _LCG_INCREMENT) % _LCG_MODULUS
	return _state


func random_int(low: int, high: int) -> int:
	if not supports(CAP_RANDOM):
		return low
	if high <= low:
		return low
	return low + (_next() >> 8) % (high - low + 1)


func random_real(low: float, high: float) -> float:
	if not supports(CAP_RANDOM):
		return low
	if high <= low:
		return low
	return low + (high - low) * (float(_next() >> 8) / float(_LCG_MODULUS >> 8))


func player_money(player: String) -> int:
	return int(money.get(player, 0))


func set_player_money(player: String, amount: int) -> bool:
	if not supports(CAP_PLAYER_MONEY):
		return false
	money[player] = amount
	return true


func debug_message(channel: String, text: String) -> bool:
	if not supports(CAP_DEBUG_OUTPUT):
		return false
	debug_log.append("%s: %s" % [channel, text])
	return true


# --- Test fixture helpers -------------------------------------------------


func set_player_scalar(player: String, key: String, value: Variant) -> void:
	## Teaches the Players facet one scalar. `key` is the facet method name, so
	## the fixture reads like the assertion: set_player_scalar(p,
	## "command_points_used", 4).
	player_scalars["%s|%s" % [player, key]] = value


func player_scalar(player: String, key: String) -> Variant:
	return player_scalars.get("%s|%s" % [player, key], null)


func set_player_object_count(
	player: String, object_type_list: String, include_dead: bool, count: int
) -> void:
	player_object_counts["%s|%s|%s" % [player, object_type_list, str(include_dead)]] = count


func set_player_model_condition_count(
	player: String, model_condition: String, count: int
) -> void:
	player_object_counts["%s|mc:%s" % [player, model_condition]] = count


# --- Facet wiring ---------------------------------------------------------
#
# Only the facets the stub can actually answer are overridden. Everything else
# inherits the refusing base, which is the honest state of a world that models
# almost nothing yet.


func _make_players() -> Players:
	return StubPlayers.new()


func _make_camera() -> PresentationSink:
	return RecordingSink.new("camera")


func _make_audio() -> PresentationSink:
	return RecordingSink.new("audio")


func _make_ui() -> PresentationSink:
	return RecordingSink.new("ui")


class StubPlayers:
	extends SageScriptWorld.Players

	## The player reads WP01 needs, answered from fixture data. A scalar the
	## test never set is REFUSED, not defaulted to zero: "no value" and "zero
	## command points" are different answers and a counter must not be written
	## from the first as if it were the second.

	func _scalar(player: String, key: String) -> SageWorldQuery:
		var stub := world as SageScriptWorldStub
		var value: Variant = stub.player_scalar(player, key)
		if value == null:
			return _refuse_query(
				"players.%s" % key, "no fixture value for player '%s'" % player
			)
		return SageWorldQuery.hit(value)

	func command_points_available(player: String) -> SageWorldQuery:
		return _scalar(player, "command_points_available")

	func command_points_total(player: String) -> SageWorldQuery:
		return _scalar(player, "command_points_total")

	func command_points_used(player: String) -> SageWorldQuery:
		return _scalar(player, "command_points_used")

	func light_points(player: String) -> SageWorldQuery:
		return _scalar(player, "light_points")

	func threat_at(player: String, threat_finder: String, threat_type: String) -> SageWorldQuery:
		var stub := world as SageScriptWorldStub
		var key := "threat_at|%s|%s" % [threat_finder, threat_type]
		var value: Variant = stub.player_scalar(player, key)
		if value == null:
			return _refuse_query(
				"players.threat_at",
				"no fixture threat for player '%s' finder '%s' type '%s'"
				% [player, threat_finder, threat_type]
			)
		return SageWorldQuery.hit(value)

	func object_count_of_types(
		player: String, object_type_list: String, include_dead: bool
	) -> SageWorldQuery:
		var stub := world as SageScriptWorldStub
		var key := "%s|%s|%s" % [player, object_type_list, str(include_dead)]
		if not stub.player_object_counts.has(key):
			return _refuse_query(
				"players.object_count_of_types",
				"no fixture count for '%s'" % key
			)
		return SageWorldQuery.hit(int(stub.player_object_counts[key]))

	func object_count_with_model_condition(
		player: String, model_condition: String
	) -> SageWorldQuery:
		var stub := world as SageScriptWorldStub
		var key := "%s|mc:%s" % [player, model_condition]
		if not stub.player_object_counts.has(key):
			return _refuse_query(
				"players.object_count_with_model_condition",
				"no fixture count for '%s'" % key
			)
		return SageWorldQuery.hit(int(stub.player_object_counts[key]))


class RecordingSink:
	extends SageScriptWorld.PresentationSink

	## Records every presentation call so handler tests can assert on it.
	##
	## Deliberate sink, NOT a silent fallback - see the PresentationSink comment
	## in script_world.gd. Each record is {"channel", "op", "values"}; `values`
	## is deep-duplicated so a later mutation of the argument array cannot
	## rewrite history.

	var records: Array[Dictionary] = []

	func emit(op: String, values: Array) -> bool:
		records.append({"channel": channel, "op": op, "values": values.duplicate(true)})
		return true

	func ops() -> Array[String]:
		var out: Array[String] = []
		for record: Dictionary in records:
			out.append(String(record["op"]))
		return out

	func values_for(op: String) -> Array:
		## Positional arguments of the FIRST call to `op`, or [] if never called.
		for record: Dictionary in records:
			if String(record["op"]) == op:
				return record["values"]
		return []

	func count_for(op: String) -> int:
		var total := 0
		for record: Dictionary in records:
			if String(record["op"]) == op:
				total += 1
		return total

	func clear() -> void:
		records.clear()
