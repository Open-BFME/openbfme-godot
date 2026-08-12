extends SceneTree
## EVA FIDELITY runner — retail's announcer suppression/behavior fields and the
## per-unit Created mapping, proven against REAL mounted packs.
##
## Two modes, selected by OPENBFME_EVA_FIDELITY_MODE:
##
##   old - pointed at the live selection (`.private/content-packs`): packs that
##         predate the blockEvents/delayMs/createdEvents schema must behave
##         exactly as before (no suppression, no deferral, legacy HeroCreated
##         fail-closed path).
##   new - pointed at a scratch content root with freshly composed overlays:
##         OtherEvaEventsToBlock mutes the blocked pair while the blocker
##         sounds, MillisecondsToWaitBeforePlaying defers and then plays, and
##         eva.hero_created resolves the created object to its authored
##         per-unit event, including ChildObject inherit and spawn-FX
##         EvaEventOwner for fortress heroes (failing closed where retail
##         authors nothing).
##
## Pack roots come from whatever OPENBFME_CONTENT selects; the runner never
## reads or writes the shared selection itself.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# NOT preloaded: retail_vertical_slice.gd names the ContentDB autoload as a
# global identifier, and in `--script` mode a preload compiles before the
# autoloads are registered. Loaded inside _run, after they exist.
const SLICE_SCRIPT_PATH := "res://src/retail_slice/retail_vertical_slice.gd"

const EXPECTED_CHECKS_NEW := 26
const EXPECTED_CHECKS_OLD := 8

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "EVA_FIDELITY_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var mode := OS.get_environment("OPENBFME_EVA_FIDELITY_MODE")
	_check("mode_selected", ["new", "old"].has(mode), "OPENBFME_EVA_FIDELITY_MODE=%s" % mode)
	if not ["new", "old"].has(mode):
		_finish()
		return
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return
	content_db.reload()

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
	var created_events: Dictionary = contract.get("created_events", {}) as Dictionary
	_check(
		"production_reader_returns_a_populated_side_map",
		events.size() > 0 and semantics.size() > 0,
		"events=%d semantics=%d" % [events.size(), semantics.size()]
	)
	if events.is_empty():
		_finish()
		return

	var host_root := ""
	for pack_root_value in content_db.pack_roots:
		host_root = String(pack_root_value)
		break

	var audio_contract := {
		"eva_events": events,
		"eva_semantics": semantics,
		"eva_created_events": created_events,
	}
	var mordor := _make_audio(host_root, audio_contract, "Mordor")
	var isengard := _make_audio(host_root, audio_contract, "Isengard")
	var men := _make_audio(host_root, audio_contract, "Men")
	if mode == "new":
		_run_new_schema_checks(events, semantics, created_events, mordor, isengard, men)
	else:
		_run_old_schema_checks(semantics, created_events, mordor)
	var expected := EXPECTED_CHECKS_NEW if mode == "new" else EXPECTED_CHECKS_OLD
	_check("liveness_check_count", passed + failed == expected, "ran=%d expected=%d" % [passed + failed, expected])
	for audio in [mordor, isengard, men]:
		audio.dispose()
		audio.free()
	_finish()


func _run_new_schema_checks(events: Dictionary, semantics: Dictionary, created_events: Dictionary, mordor: Node, isengard: Node, men: Node) -> void:
	var has_block := false
	var has_delay := false
	for fields_value in semantics.values():
		var fields := fields_value as Dictionary
		has_block = has_block or fields.has("blockEvents")
		has_delay = has_delay or fields.has("delayMs")
	_check("new_schema_compiles_suppression_and_delay_fields", has_block and has_delay)
	_check(
		"new_schema_carries_created_events_map",
		created_events.size() == 237,
		"createdEvents=%d" % created_events.size()
	)

	# --- OtherEvaEventsToBlock: the UnitUnderAttack mutual suppression ------
	var base := 10_000_000
	var routed: Dictionary = mordor.play_eva_event("UnitUnderAttack", 11, base)
	_check("blocked_pair_under_attack_plays", bool(routed.get("ok", false)), str(routed.get("reason", "")))
	routed = mordor.play_eva_event("UnitUnderAttackFromShroudedUnit", 12, base + 100)
	_check(
		"blocked_pair_shrouded_muted_while_blocker_sounds",
		not bool(routed.get("ok", true)) and String(routed.get("reason", "")) == "eva_blocked",
		str(routed)
	)
	routed = mordor.play_eva_event("UnitUnderAttackFromShroudedUnit", 13, base + 120_000)
	_check("blocked_pair_shrouded_recovers_after_blocker", bool(routed.get("ok", false)), str(routed.get("reason", "")))

	# --- OtherEvaEventsToBlock: ring-hero death suppresses DiscoveredRing ---
	base = 20_000_000
	routed = men.play_eva_event("GaladrielDie", 21, base)
	_check("ring_suppression_death_line_plays", bool(routed.get("ok", false)), str(routed.get("reason", "")))
	routed = men.play_eva_event("DiscoveredRing", 22, base + 500)
	_check(
		"ring_suppression_discovered_ring_muted",
		not bool(routed.get("ok", true)) and String(routed.get("reason", "")) == "eva_blocked",
		str(routed)
	)
	routed = men.play_eva_event("DiscoveredRing", 23, base + 120_000)
	_check("ring_suppression_recovers_after_blocker", bool(routed.get("ok", false)), str(routed.get("reason", "")))

	# --- MillisecondsToWaitBeforePlaying: defer, hold, then play once -------
	base = 30_000_000
	var delay_ms := int((semantics.get("MountainTrollCreated", {}) as Dictionary).get("delayMs", 0))
	_check("mountain_troll_created_carries_retail_delay", delay_ms == 3_000, "delayMs=%d" % delay_ms)
	var expected_troll_sound := String((events.get("MountainTrollCreated", {}) as Dictionary).get("Mordor", ""))
	routed = mordor.play_eva_event("MountainTrollCreated", 31, base)
	_check(
		"delay_defers_first_request",
		not bool(routed.get("ok", true)) and String(routed.get("reason", "")) == "eva_delay" and int(routed.get("due_msec", -1)) == base + delay_ms,
		str(routed)
	)
	routed = mordor.play_eva_event("MountainTrollCreated", 32, base + 500)
	_check(
		"delay_repeat_request_keeps_single_pending",
		not bool(routed.get("ok", true)) and String(routed.get("reason", "")) == "eva_delay" and int(routed.get("due_msec", -1)) == base + delay_ms,
		str(routed)
	)
	_check("delay_not_yet_due_flush_plays_nothing", (mordor.flush_due_eva_events(base + delay_ms - 1) as Array).is_empty())
	var flushed: Array = mordor.flush_due_eva_events(base + delay_ms)
	_check(
		"delay_due_flush_plays_the_authored_sound",
		flushed.size() == 1 and bool((flushed[0] as Dictionary).get("ok", false)) and String((flushed[0] as Dictionary).get("event_id", "")) == expected_troll_sound,
		str(flushed)
	)
	_check("delay_flush_is_single_fire", (mordor.flush_due_eva_events(base + delay_ms + 10_000) as Array).is_empty())

	# --- The same deferral flushes off the EVENT STREAM, not just direct calls.
	base = 40_000_000
	var catapult_delay := int((semantics.get("CatapultCreated", {}) as Dictionary).get("delayMs", 0))
	var expected_catapult_sound := String((events.get("CatapultCreated", {}) as Dictionary).get("Mordor", ""))
	routed = mordor.play_eva_event("CatapultCreated", 41, base)
	var log_mark: int = (mordor.routing_log as Array).size()
	var flush_probe: Array[Dictionary] = [
		{"kind": "noop.flush_probe", "sequence": 42, "tick": (base + catapult_delay) / 100},
	]
	_sync_batch(mordor, flush_probe)
	_check(
		"delay_flushes_via_event_stream_clock",
		String(routed.get("reason", "")) == "eva_delay" and _routed_events_since(mordor, log_mark).has(expected_catapult_sound),
		"reason=%s routed=%s" % [String(routed.get("reason", "")), str(_routed_events_since(mordor, log_mark))]
	)

	# --- eva.hero_created resolves the created OBJECT's authored event ------
	base = 50_000_000
	var expected_nazgul_sound := String((events.get("NazgulCreated", {}) as Dictionary).get("Mordor", ""))
	log_mark = (mordor.routing_log as Array).size()
	_sync_batch(mordor, _hero_created_events("MordorBlackRider", 51, base))
	_check(
		"created_nazgul_resolves_per_unit_event",
		_routed_events_since(mordor, log_mark).has(expected_nazgul_sound),
		"expected=%s routed=%s" % [expected_nazgul_sound, str(_routed_events_since(mordor, log_mark))]
	)
	# The troll's created line honors ITS retail delay on this path too.
	base = 60_000_000
	log_mark = (mordor.routing_log as Array).size()
	_sync_batch(mordor, _hero_created_events("MordorMountainTroll", 61, base))
	var deferred := not _routed_events_since(mordor, log_mark).has(expected_troll_sound)
	flushed = mordor.flush_due_eva_events(base + delay_ms)
	_check(
		"created_mountain_troll_defers_then_plays",
		deferred and flushed.size() == 1 and bool((flushed[0] as Dictionary).get("ok", false)) and String((flushed[0] as Dictionary).get("event_id", "")) == expected_troll_sound,
		str(flushed)
	)
	base = 70_000_000
	var expected_uruk_sound := String((events.get("UrukCreated", {}) as Dictionary).get("Isengard", ""))
	log_mark = (isengard.routing_log as Array).size()
	_sync_batch(isengard, _hero_created_events("IsengardFighter", 71, base))
	_check(
		"created_uruk_resolves_for_second_side",
		_routed_events_since(isengard, log_mark).has(expected_uruk_sound),
		"expected=%s routed=%s" % [expected_uruk_sound, str(_routed_events_since(isengard, log_mark))]
	)
	# Production ring-hero id is the ChildObject, which inherits SauronCreated.
	base = 75_000_000
	var expected_sauron_sound := String((events.get("SauronCreated", {}) as Dictionary).get("Mordor", ""))
	log_mark = (mordor.routing_log as Array).size()
	_sync_batch(mordor, _hero_created_events("MordorSauron_RingHero", 76, base))
	_check(
		"created_ring_hero_inherits_parent_voice_created",
		_routed_events_since(mordor, log_mark).has(expected_sauron_sound),
		"expected=%s routed=%s" % [expected_sauron_sound, str(_routed_events_since(mordor, log_mark))]
	)
	# Fortress heroes: VoiceCreated is commented out and rehooked to spawn FX
	# EvaEventOwner. The remake's only hero-create EVA path is eva.hero_created,
	# so those object ids must map.
	base = 80_000_000
	var expected_witch_king_sound := String((events.get("WitchKingCreated", {}) as Dictionary).get("Mordor", ""))
	log_mark = (mordor.routing_log as Array).size()
	_sync_batch(mordor, _hero_created_events("MordorWitchKingOnFellBeast", 81, base))
	_check(
		"created_witch_king_resolves_spawn_fx_owner",
		_routed_events_since(mordor, log_mark).has(expected_witch_king_sound),
		"expected=%s routed=%s" % [expected_witch_king_sound, str(_routed_events_since(mordor, log_mark))]
	)
	base = 85_000_000
	var expected_lurtz_sound := String((events.get("LurtzCreated", {}) as Dictionary).get("Isengard", ""))
	log_mark = (isengard.routing_log as Array).size()
	_sync_batch(isengard, _hero_created_events("IsengardLurtz", 86, base))
	_check(
		"created_lurtz_resolves_spawn_fx_owner",
		_routed_events_since(isengard, log_mark).has(expected_lurtz_sound),
		"expected=%s routed=%s" % [expected_lurtz_sound, str(_routed_events_since(isengard, log_mark))]
	)
	# A unit the local side cannot build keeps failing closed on the side map.
	_sync_batch(men, _hero_created_events("MordorBlackRider", 91, 90_000_000))
	_check(
		"created_cross_side_unit_fails_closed",
		_has_diagnostic(men, "eva_side_unavailable:NazgulCreated:91"),
		str(men.eva_diagnostics)
	)


func _run_old_schema_checks(semantics: Dictionary, created_events: Dictionary, mordor: Node) -> void:
	var has_new_fields := false
	for fields_value in semantics.values():
		var fields := fields_value as Dictionary
		has_new_fields = has_new_fields or fields.has("blockEvents") or fields.has("delayMs")
	_check(
		"old_schema_premise_no_suppression_delay_or_created_map",
		not has_new_fields and created_events.is_empty(),
		"blockOrDelay=%s createdEvents=%d" % [str(has_new_fields), created_events.size()]
	)
	# Today's behavior, unchanged: the shrouded pair double-fires because the
	# old schema carries no block list.
	var base := 10_000_000
	var routed: Dictionary = mordor.play_eva_event("UnitUnderAttack", 11, base)
	var under_attack_ok := bool(routed.get("ok", false))
	routed = mordor.play_eva_event("UnitUnderAttackFromShroudedUnit", 12, base + 100)
	_check(
		"old_packs_shrouded_pair_still_double_fires",
		under_attack_ok and bool(routed.get("ok", false)),
		str(routed)
	)
	# ... and MillisecondsToWaitBeforePlaying is not honored: the line plays
	# the moment it is requested.
	routed = mordor.play_eva_event("MountainTrollCreated", 21, 20_000_000)
	_check("old_packs_troll_created_plays_immediately", bool(routed.get("ok", false)), str(routed.get("reason", "")))
	# The legacy invented HeroCreated id still fails closed on the side map
	# (retail authors no HeroCreated block, so there is no side sound for it).
	_sync_batch(mordor, _hero_created_events("MordorBlackRider", 31, 30_000_000))
	_check(
		"old_packs_keep_legacy_hero_created_fail_closed",
		_has_diagnostic(mordor, "eva_side_unavailable:HeroCreated:31"),
		str(mordor.eva_diagnostics)
	)


func _make_audio(host_root: String, contract: Dictionary, side: String) -> Node:
	var audio = AudioScript.new()
	root.add_child(audio)
	audio.observability_enabled = true
	audio.configure(host_root, true, {}, contract, side)
	return audio


func _sync_batch(audio: Node, events: Array[Dictionary]) -> void:
	# Production sync_events is a cursor over one growing sim stream. This
	# runner feeds isolated batches, so rewind the cursor before each one.
	audio._next_event_index = 0
	audio.sync_events(events)


func _hero_created_events(object_id: String, sequence: int, clock_msec: int) -> Array[Dictionary]:
	return [{
		"kind": "eva.hero_created",
		"sequence": sequence,
		"entity_id": 0,
		"target_id": 0,
		"tick": clock_msec / 100,
		"team": 0,
		"object_id": object_id,
		"unit_type": object_id,
	}]


func _routed_events_since(audio: Node, mark: int) -> Array[String]:
	var ids: Array[String] = []
	var log := audio.routing_log as Array
	for index in range(mark, log.size()):
		var entry := log[index] as Dictionary
		if bool(entry.get("ok", false)):
			ids.append(String(entry.get("event_id", "")))
	return ids


func _has_diagnostic(audio: Node, diagnostic: String) -> bool:
	return (audio.eva_diagnostics as Array[String]).has(diagnostic)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s%s" % [name, "" if detail == "" else (" | " + detail)])


func _finish() -> void:
	print("EVA_FIDELITY_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
