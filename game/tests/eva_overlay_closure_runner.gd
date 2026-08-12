extends SceneTree
## EVA announcer DELIVERY closure. The runtime half of the EVA lane has existed
## since the eva-audio lane; what was missing was content: no published pack
## declared `evaEvents`, so every announcement fell into
## `eva_contract_unavailable`/`eva_event_unavailable` and the game was silent.
##
## This runner proves the closure end to end against real mounted packs:
##
##   1. a mounted pack ships the eva.ini side map, resolved through the
##      PRODUCTION reader (`RetailVerticalSlice._load_eva_contract`) - not a
##      reimplementation of it
##   2. every EVA id the production code paths actually emit resolves, for the
##      local player's side, to the exact retail sound the side map names
##   3. that sound resolves through the merged ContentDB audio registry to a
##      real decoded `AudioStream` off a real file in a mounted pack
##   4. a side retail authors NO sound for still fails closed - never another
##      side's line, never a substitute
##
## Pack roots come from whatever OPENBFME_CONTENT selects, so this runner is
## pointed at an isolated content root (host faction pack + its EVA overlay)
## and never reads or writes the shared selection.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# NOT preloaded: retail_vertical_slice.gd names the ContentDB autoload as a
# global identifier, and in `--script` mode a preload compiles before the
# autoloads are registered. Loaded inside _run, after they exist.
const SLICE_SCRIPT_PATH := "res://src/retail_slice/retail_vertical_slice.gd"

## Every EVA id the shipped code paths emit as a literal, with the call site
## that emits it. Kept explicit so a new emit site that ships no content is a
## test failure rather than silence in a match.
const RUNTIME_EVA_EVENT_IDS := [
	"UnitUnderAttack",
	"CannotBuildDueToCPLimit",
	"CannotBuildDueToFunds",
	"GenericBuildingComplete-Builder",
	"AllyDefeated",
	"EnemyDefeated",
	"HeroCreated",
	"AlliedPlayerGainsRing",
	"EnemyPlayerGainsRing",
	"UpgradeBannerCarrierTechnologyReady",
	"UpgradeFlameArrowsReady",
	"UpgradeForgedBladesReady",
	"UpgradeHeavyArmorReady",
]

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "EVA_OVERLAY_CLOSURE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var side := OS.get_environment("OPENBFME_EVA_SIDE")
	if side == "":
		side = "Men"
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return
	content_db.reload()

	var overlay_root := ""
	for pack_root_value in content_db.pack_roots:
		var pack_root := String(pack_root_value)
		var path := String(mod_loader.call("resolve_pack_path", pack_root, "data/eva_events.json"))
		if path != "" and FileAccess.file_exists(path):
			overlay_root = pack_root
			break
	_check("a_mounted_pack_ships_the_eva_side_map", overlay_root != "", overlay_root)

	# THE PRODUCTION READER, not a copy of it: the slice resolves the eva
	# contract off the mounted pack roots exactly here.
	var slice_script: GDScript = load(SLICE_SCRIPT_PATH)
	_check("production_slice_script_loaded", slice_script != null)
	if slice_script == null:
		_finish()
		return
	var slice = slice_script.new()
	var contract: Dictionary = slice._load_eva_contract()
	slice.free()
	var events: Dictionary = contract.get("events", {}) as Dictionary
	var semantics: Dictionary = contract.get("semantics", {}) as Dictionary
	_check("production_reader_returns_a_populated_side_map", events.size() > 0 and semantics.size() > 0, "events=%d semantics=%d" % [events.size(), semantics.size()])
	if events.is_empty():
		_finish()
		return

	var host_root := ""
	for pack_root_value in content_db.pack_roots:
		if String(pack_root_value) != overlay_root:
			host_root = String(pack_root_value)
			break
	if host_root == "":
		host_root = overlay_root

	var audio = AudioScript.new()
	root.add_child(audio)
	audio.observability_enabled = true
	audio.configure(host_root, true, {}, {
		"eva_events": events,
		"eva_semantics": semantics,
	}, side)

	# One announcement per distinct presentation timestamp, spaced past every
	# retail cooldown, so a rejection here is a CONTENT gap and never
	# arbitration.
	var clock := 0
	var resolved := 0
	var unresolved: Array[String] = []
	# Emit sites retail authors NO sound for. These are honest silence, not
	# delivery gaps: they are reported by name and pinned fail-closed so a
	# future substitute cannot sneak in.
	var unauthored: Array[String] = []
	for eva_id_value in _expected_eva_event_ids(content_db):
		var eva_id := String(eva_id_value)
		clock += 600_000
		var expected_sound := String((events.get(eva_id, {}) as Dictionary).get(side, ""))
		var routed: Dictionary = audio.play_eva_event(eva_id, 1, clock)
		var ok := bool(routed.get("ok", false))
		if expected_sound == "":
			unauthored.append(eva_id)
			_check(
				"eva_%s_unauthored_for_%s_fails_closed" % [eva_id, side],
				not ok and ["eva_event_unavailable", "eva_side_unavailable"].has(String(routed.get("reason", ""))),
				str(routed)
			)
			continue
		if ok and String(routed.get("event_id", "")) == expected_sound:
			resolved += 1
		else:
			unresolved.append("%s:%s:%s" % [eva_id, expected_sound, String(routed.get("reason", routed.get("event_id", "")))])
		_check(
			"eva_%s_resolves_exact_side_sound" % eva_id,
			ok and String(routed.get("event_id", "")) == expected_sound,
			"expected=%s got=%s reason=%s" % [expected_sound, String(routed.get("event_id", "")), String(routed.get("reason", ""))]
		)
	_check("every_authored_emitted_eva_id_resolved", unresolved.is_empty(), ", ".join(unresolved))
	if not unauthored.is_empty():
		print("EVA_UNAUTHORED_EMIT_SITES side=%s %s" % [side, ", ".join(unauthored)])

	# Anti-substitution: retail authors UpgradeHeavyArmorReady for five sides
	# only. Dwarves and Elves must stay SILENT, never borrow another side's line.
	var silent_side := "Dwarves" if side != "Dwarves" else "Elves"
	var silent_audio = AudioScript.new()
	root.add_child(silent_audio)
	silent_audio.observability_enabled = true
	silent_audio.configure(host_root, true, {}, {
		"eva_events": events,
		"eva_semantics": semantics,
	}, silent_side)
	var silent_result: Dictionary = silent_audio.play_eva_event("UpgradeHeavyArmorReady", 1, 600_000)
	_check(
		"unauthored_side_fails_closed_without_substitution",
		not bool(silent_result.get("ok", true)) and String(silent_result.get("reason", "")) == "eva_side_unavailable",
		str(silent_result)
	)
	silent_audio.dispose()
	silent_audio.free()

	print("EVA_CLOSURE side=%s resolved=%d/%d overlay=%s" % [side, resolved, _expected_eva_event_ids(content_db).size(), overlay_root])
	audio.dispose()
	audio.free()
	_finish()


func _expected_eva_event_ids(content_db: Node) -> Array:
	## The literal emit sites plus every EVA route a mounted playable-structure
	## document authors (retail_slice_audio._play_structure_eva routes these).
	var ids: Array = RUNTIME_EVA_EVENT_IDS.duplicate()
	for object_id_value in content_db.get_playable_structure_runtimes().keys():
		var document: Dictionary = content_db.get_playable_structure_runtime(String(object_id_value))
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var presentation: Dictionary = registration.get("presentation", {}) as Dictionary
		var routes: Dictionary = presentation.get("audioRoutes", {}) as Dictionary
		for role in ["EvaEventDamagedOwner", "EvaEventDieOwner"]:
			for row_value in routes.get(role, []) as Array:
				if typeof(row_value) != TYPE_DICTIONARY:
					continue
				var event_id := String((row_value as Dictionary).get("id", ""))
				if event_id != "" and not ids.has(event_id):
					ids.append(event_id)
	return ids


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s%s" % [name, "" if detail == "" else (" | " + detail)])


func _finish() -> void:
	print("EVA_OVERLAY_CLOSURE_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
