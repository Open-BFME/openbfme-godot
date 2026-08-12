extends SceneTree
## castleSiege contract versioning proof (lane L1).
##
## The mounted pack predates per-map capability derivation, so v1 documents
## (exact-order, exact-length five-blocker array) must keep loading.  The v2
## schema carries the derived per-map requirement set under "required"; the
## runtime computes blockers = required - implemented at load time, so v2 can
## express "three blockers" which the v1 seal never could.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 15

const V1_CONTRACT := {
	"family": "retail-castle-siege-skirmish",
	"gameplayStatus": "blocked-named-gaps",
	"blockers": [
		"walkable-walls",
		"defendable-gates",
		"wall-garrisons",
		"wall-mounted-defenses",
		"skirmish-ai-libraries",
	],
	"admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
}
const V1_BLOCKERS: Array[String] = [
	"walkable-walls",
	"defendable-gates",
	"wall-garrisons",
	"wall-mounted-defenses",
	"skirmish-ai-libraries",
]

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_SIEGE_CONTRACT", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _v2(required: Array) -> Dictionary:
	return {
		"version": 2,
		"family": "retail-castle-siege-skirmish",
		"gameplayStatus": "blocked-named-gaps",
		"admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
		"required": required,
	}


func _load(data, contract: Variant) -> bool:
	return bool(data._load_castle_siege_contract(contract))


func _run() -> void:
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_check("map_data_script_parses", map_data_script != null)
	if map_data_script == null:
		_finish()
		return

	# Absent contract: an ordinary map, no blockers.
	var plain = map_data_script.new()
	_check("absent_contract_loads", _load(plain, null), String(plain.error))
	_check("absent_contract_has_no_blockers", plain.castle_gameplay_blockers.is_empty())

	# v1: the legacy admission-lane seal still loads byte-for-byte.
	var v1 = map_data_script.new()
	_check("v1_contract_loads", _load(v1, V1_CONTRACT), String(v1.error))
	_check("v1_blockers_surface_exactly", v1.castle_gameplay_blockers == V1_BLOCKERS,
		str(v1.castle_gameplay_blockers))
	var v1_incomplete = map_data_script.new()
	var v1_short := V1_CONTRACT.duplicate(true)
	v1_short["blockers"] = ["walkable-walls"]
	_check("v1_incomplete_inventory_fails_closed",
		not _load(v1_incomplete, v1_short)
		and String(v1_incomplete.error) == "invalid castleSiege blocker inventory",
		String(v1_incomplete.error))

	# v2: a per-map subset the v1 seal could never express (Fornost needs only
	# gates and garrisons; nothing else is placed on that map).
	var v2 = map_data_script.new()
	_check("v2_subset_contract_loads", _load(v2, _v2(["defendable-gates", "wall-garrisons"])),
		String(v2.error))
	_check("v2_subset_blockers_computed",
		v2.castle_gameplay_blockers == Array(["defendable-gates", "wall-garrisons"], TYPE_STRING, "", null),
		str(v2.castle_gameplay_blockers))
	var v2_full = map_data_script.new()
	_check("v2_full_vocabulary_loads",
		_load(v2_full, _v2([
			"walkable-walls", "scaleable-walls", "defendable-gates",
			"wall-garrisons", "wall-mounted-defenses", "skirmish-ai-libraries",
		])), String(v2_full.error))
	_check("v2_full_blockers_computed",
		v2_full.castle_gameplay_blockers.size() == 6
		and v2_full.castle_gameplay_blockers[1] == "scaleable-walls",
		str(v2_full.castle_gameplay_blockers))

	# v2 fails closed on every malformed shape.
	var v2_unknown = map_data_script.new()
	_check("v2_unknown_capability_fails_closed",
		not _load(v2_unknown, _v2(["walkable-walls", "moonbeams"])),
		String(v2_unknown.error))
	var v2_order = map_data_script.new()
	_check("v2_noncanonical_order_fails_closed",
		not _load(v2_order, _v2(["defendable-gates", "walkable-walls"])),
		String(v2_order.error))
	var v2_empty = map_data_script.new()
	_check("v2_empty_required_fails_closed", not _load(v2_empty, _v2([])),
		String(v2_empty.error))
	var v2_duplicate = map_data_script.new()
	_check("v2_duplicate_capability_fails_closed",
		not _load(v2_duplicate, _v2(["walkable-walls", "walkable-walls"])),
		String(v2_duplicate.error))
	var v2_extra_key = map_data_script.new()
	var v2_hardcoded := _v2(["defendable-gates"])
	v2_hardcoded["blockers"] = ["defendable-gates"]
	_check("v2_authored_blockers_key_fails_closed",
		not _load(v2_extra_key, v2_hardcoded), String(v2_extra_key.error))

	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_SIEGE_CONTRACT PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_SIEGE_CONTRACT FAIL %s %s" % [name, detail])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("CASTLE_SIEGE_CONTRACT FAIL liveness ran %d checks, expected %d" % [ran, EXPECTED_CHECKS])
	print("CASTLE_SIEGE_CONTRACT_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
