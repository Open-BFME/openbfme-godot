extends SceneTree
## Accelerated full-match lifecycle evidence. This is intentionally separate
## from the real-time rendered soak: simulation time may advance quickly here,
## but every terminal state still comes from the public match clock and rules.

const SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const EXPECTED_MATCHES := 3
const ADVANCE_CHUNK_TICKS := 300
const MAXIMUM_MATCH_TICKS := 36000
const READY_TIMEOUT_MSEC := 120000

var _failed := false


func _initialize() -> void:
	create_timer(180.0, true, false, true).timeout.connect(_fail.bind("match lifecycle watchdog timeout"))
	call_deferred("_run")


func _run() -> void:
	var output := OS.get_environment("OPENBFME_M2_LIFECYCLE_OUTPUT").replace("\\", "/")
	if output == "" or not output.contains("/.private/") or output.get_extension().to_lower() != "json":
		_fail("OPENBFME_M2_LIFECYCLE_OUTPUT must be a JSON file below .private")
		return
	var packed := load(SCENE_PATH) as PackedScene
	if packed == null:
		_fail("retail vertical-slice scene did not load")
		return
	var asset_factory = load("res://src/view/asset_factory.gd")
	var rows: Array[Dictionary] = []
	var expected_bundle := ""
	var expected_signature := ""
	var ready_starts := 0
	var teardowns_confirmed := 0
	for match_index in EXPECTED_MATCHES:
		var ready_started := Time.get_ticks_msec()
		var slice = packed.instantiate()
		root.add_child(slice)
		while not bool(slice.ready_ok):
			await process_frame
			if String(slice.failure_reason) != "":
				_fail("match %d readiness failed: %s" % [match_index + 1, String(slice.failure_reason)])
				return
			if Time.get_ticks_msec() - ready_started > READY_TIMEOUT_MSEC:
				_fail("match %d readiness timed out" % [match_index + 1])
				return
		var ready_duration_msec := Time.get_ticks_msec() - ready_started
		ready_starts += 1
		var bundle_sha256 := String(slice.selected_pack_root).replace("\\", "/").get_file()
		if not _is_sha256(bundle_sha256):
			_fail("match %d selected pack has no bundle SHA-256" % [match_index + 1])
			return
		if expected_bundle == "":
			expected_bundle = bundle_sha256
		elif bundle_sha256 != expected_bundle:
			_fail("match %d mounted another bundle" % [match_index + 1])
			return

		var player_fortress_id := int(slice.simulation.fortress_id(0))
		var defeat_chain := {
			"constructionStartedSequence": 0,
			"constructionTargetId": 0,
			"constructionCompletedSequence": 0,
			"productionCompletedSequence": 0,
			"fortressHitSequence": 0,
			"fortressDestroyedSequence": 0,
			"defeatSequence": 0,
		}
		while int(slice.simulation.winner) == -1 and int(slice.simulation.tick_index) < MAXIMUM_MATCH_TICKS:
			slice.step_for_test(mini(ADVANCE_CHUNK_TICKS, MAXIMUM_MATCH_TICKS - int(slice.simulation.tick_index)))
			_observe_defeat_chain(slice.simulation.events, player_fortress_id, defeat_chain)
		if int(slice.simulation.winner) != 1:
			_fail("match %d did not reach the default enemy-AI defeat by tick %d" % [match_index + 1, int(slice.simulation.tick_index)])
			return
		var player_fortress_health := int(slice.simulation.structure(player_fortress_id).get("health", -1))
		var construction_started_sequence := int(defeat_chain.constructionStartedSequence)
		var construction_completed_sequence := int(defeat_chain.constructionCompletedSequence)
		var production_completed_sequence := int(defeat_chain.productionCompletedSequence)
		var fortress_hit_sequence := int(defeat_chain.fortressHitSequence)
		var fortress_destroyed_sequence := int(defeat_chain.fortressDestroyedSequence)
		var defeat_sequence := int(defeat_chain.defeatSequence)
		var defeat_event_count := _event_count(slice.simulation.events, "match.defeat")
		var outcome_presented := bool(slice.hud.outcome_layer.visible) and String(slice.hud.outcome_title.text) == "DEFEAT"
		var music_state := String(slice.audio_system.current_music_state)
		if player_fortress_health > 0 or not (
			construction_started_sequence > 0
			and construction_completed_sequence > construction_started_sequence
			and production_completed_sequence > construction_completed_sequence
			and fortress_hit_sequence > production_completed_sequence
			and fortress_destroyed_sequence > fortress_hit_sequence
			and defeat_sequence > fortress_destroyed_sequence
		):
			_fail("match %d lacks construction-to-production-to-attack-to-defeat causality" % [match_index + 1])
			return
		if defeat_event_count != 1 or not outcome_presented or music_state != "defeat":
			_fail("match %d lacks one imported defeat outcome and music state" % [match_index + 1])
			return
		var signature := String(slice.simulation.state_signature())
		if expected_signature == "":
			expected_signature = signature
		elif signature != expected_signature:
			_fail("match %d deterministic signature changed" % [match_index + 1])
			return

		var row := {
			"index": match_index + 1,
			"bundleSha256": bundle_sha256,
			"winner": int(slice.simulation.winner),
			"terminalTick": int(slice.simulation.tick_index),
			"stateSignature": signature,
			"playerFortressHealth": player_fortress_health,
			"constructionStartedSequence": construction_started_sequence,
			"constructionCompletedSequence": construction_completed_sequence,
			"productionCompletedSequence": production_completed_sequence,
			"fortressHitSequence": fortress_hit_sequence,
			"fortressDestroyedSequence": fortress_destroyed_sequence,
			"defeatSequence": defeat_sequence,
			"defeatEventCount": defeat_event_count,
			"outcomePresented": outcome_presented,
			"musicState": music_state,
			"sceneFreed": false,
			"meshCacheCleared": false,
			"readyDurationMsec": ready_duration_msec,
		}
		var scene_reference: WeakRef = weakref(slice)
		slice.queue_free()
		slice = null
		await process_frame
		await process_frame
		row.sceneFreed = scene_reference.get_ref() == null
		row.meshCacheCleared = int(asset_factory.mesh_cache_size()) == 0
		if not bool(row.sceneFreed) or not bool(row.meshCacheCleared):
			_fail("match %d scene or mesh cache survived teardown" % [match_index + 1])
			return
		teardowns_confirmed += 1
		rows.append(row)

	var evidence := {
		"schema": "openbfme.m2-match-lifecycle",
		"schemaVersion": 0,
		"profileSha256": OS.get_environment("OPENBFME_M2_PROFILE_SHA256"),
		"bundleSha256": expected_bundle,
		"gitRevision": OS.get_environment("OPENBFME_M2_GIT_REVISION"),
		"dirtyStateDigest": OS.get_environment("OPENBFME_M2_DIRTY_STATE_DIGEST"),
		"expectedMatches": EXPECTED_MATCHES,
		"completedMatches": rows.size(),
		"readyStarts": ready_starts,
		"teardownsConfirmed": teardowns_confirmed,
		"matches": rows,
		"deterministicSignature": expected_signature,
		"diagnosticCount": 0,
	}
	var parent := output.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(parent) != OK:
		_fail("match lifecycle evidence directory could not be created")
		return
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		_fail("match lifecycle evidence could not be opened")
		return
	file.store_string(JSON.stringify(evidence, "  ", false) + "\n")
	file.close()
	print("M2_MATCH_LIFECYCLE_RESULT matches=%d starts=%d teardowns=%d signature=%s bundle=%s" % [
		rows.size(), ready_starts, teardowns_confirmed, expected_signature, expected_bundle,
	])
	quit(0)


func _observe_defeat_chain(events: Array, player_fortress_id: int, chain: Dictionary) -> void:
	for event_value in events:
		var event := event_value as Dictionary
		var kind := String(event.get("kind", ""))
		var sequence := int(event.get("sequence", 0))
		if kind == "construction.started" and int(event.get("team", -1)) == 1 and int(chain.constructionStartedSequence) == 0:
			chain.constructionStartedSequence = sequence
			chain.constructionTargetId = int(event.get("target_id", 0))
		elif kind == "construction.completed" and int(event.get("target_id", 0)) == int(chain.constructionTargetId) and int(chain.constructionCompletedSequence) == 0:
			chain.constructionCompletedSequence = sequence
		elif kind == "production.complete" and int(event.get("team", -1)) == 1 and int(chain.productionCompletedSequence) == 0:
			chain.productionCompletedSequence = sequence
		elif kind == "combat.hit_structure" and int(event.get("target_id", 0)) == player_fortress_id and int(chain.fortressHitSequence) == 0:
			chain.fortressHitSequence = sequence
		elif kind == "structure.destroyed" and int(event.get("target_id", 0)) == player_fortress_id and int(chain.fortressDestroyedSequence) == 0:
			chain.fortressDestroyedSequence = sequence
		elif kind == "match.defeat" and int(chain.defeatSequence) == 0:
			chain.defeatSequence = sequence


func _event_count(events: Array, kind: String) -> int:
	var result := 0
	for event_value in events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			result += 1
	return result


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not String(character) in "0123456789abcdef":
			return false
	return true


func _fail(message: String) -> void:
	if _failed:
		return
	_failed = true
	printerr("M2_MATCH_LIFECYCLE_FAIL %s" % message)
	quit(1)
