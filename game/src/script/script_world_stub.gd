class_name SageScriptWorldStub
extends SageScriptWorld

## In-memory SageScriptWorld for headless tests and for exercising the script
## vocabulary before the simulation binding exists.
##
## Deterministic by construction: the random stream is an explicit 32-bit LCG
## seeded from `seed_value`, not Godot's RNG, so results cannot drift with an
## engine version. Two stubs created with the same seed and driven by the same
## call sequence produce byte-identical state.

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
