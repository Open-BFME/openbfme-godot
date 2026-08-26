extends SceneTree
## FAST GATE for the owner playtest report (2026-08-26): "I can hear combat
## through the fog of war from my enemy attacking mobs and its SUPER loud".
##
## Retail authorities enforced here:
##
## (A) SHROUD CULL — GameSounds.cpp:232-269 (`SoundManager::canPlayNow`): a
##     sound typed `shrouded` whose position is not CELLSHROUD_CLEAR for the
##     local player is refused outright. Combat SFX author
##     `Type = world shrouded everyone` (soundeffects.ini:16682 ImpactHorse,
##     :9411 ImpactSword01), so a battle under the fog is SILENT.
##
## (B) DISTANCE — MilesAudioManager::getEffectiveVolume (ZH reference,
##     MilesAudioManager.cpp:2681-2740): full volume inside MinRange,
##     gain = MinRange / distance beyond it, hard cull at >= MaxRange.
##
## (C) NO SILENT FALLBACKS — an event with no authored MinRange/MaxRange takes
##     the hand-tuned WORLD_SFX_DEFAULT_* values and is NAMED in
##     `unranged_world_sfx`; a world event with no resolvable position plays
##     flat and keeps the pre-existing `unsupported-type:world:<id>` gap.
##
## Everything asserts on player nodes / volume_db / bookkeeping, never on
## audible output. Presentation-side only: no sim object is even constructed.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const FIGHTER_ID := "bfme2.object.gondor-fighter"
const VISIBLE_ENTITY := 7
const SHROUDED_ENTITY := 9
const SHROUDED_POS := Vector2(1500.0, 1500.0)

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()
var _positions: Dictionary = {}
var _listener := Vector2(100.0, 100.0)


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_SFX_SHROUD_ATTENUATION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return
	content_db.reload()
	var pack_root := String(PackCapabilityScript.resolve_host_slice_pack(content_db.pack_meta).get("root", ""))
	_check("host_slice_pack_resolved", pack_root != "", pack_root)
	if pack_root == "":
		_finish()
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(pack_root, true)
	_check("fighter_binds_authored_weapon_firefx", String((audio.playable_unit_weapon_sfx.get(FIGHTER_ID, {}) as Dictionary).get("weapon", "")) == "ImpactSword01")
	audio._entity_object_ids[VISIBLE_ENTITY] = FIGHTER_ID
	audio._entity_object_ids[SHROUDED_ENTITY] = FIGHTER_ID
	_positions = {VISIBLE_ENTITY: _listener, SHROUDED_ENTITY: SHROUDED_POS}
	audio.configure_spatial_audio(_mock_position, _mock_visible, _mock_listener, 1.0)
	var base_db := float(UserSettingsScript.volume_to_db(audio.voice_sfx_volume, audio.muted))

	# ---- (A) an event under the shroud plays NOTHING ----------------------
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 1, "kind": "combat.member_swing", "entity_id": SHROUDED_ENTITY, "target_id": 8, "member_index": 0},
	])
	_check(
		"shrouded_swing_is_culled_and_counted",
		int(audio.shrouded_sfx_drops.get("ImpactSword01", 0)) == 1,
		str(audio.shrouded_sfx_drops)
	)
	_check(
		"shrouded_swing_reaches_no_player_node",
		_sounding("ImpactSword01", audio).is_empty(),
		str(_sounding("ImpactSword01", audio).size())
	)
	_check(
		"shroud_cull_leaves_a_receipt",
		String(audio.last_spatial_sfx_receipt.get("culled", "")) == "shroud",
		str(audio.last_spatial_sfx_receipt)
	)

	# ---- (B) a visible event at the listener plays at full level -----------
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 2, "kind": "combat.member_swing", "entity_id": VISIBLE_ENTITY, "target_id": 8, "member_index": 0},
	])
	var near_players := _sounding("ImpactSword01", audio)
	_check("visible_swing_reaches_a_player_node", near_players.size() == 1, "sounding=%d" % near_players.size())
	var near_db: float = float((near_players[0] as AudioStreamPlayer).volume_db) if near_players.size() == 1 else 999.0
	_check(
		"visible_swing_at_listener_is_unattenuated",
		near_players.size() == 1 and absf(near_db - base_db) < 0.01
			and is_equal_approx(float(audio.last_spatial_sfx_receipt.get("gain", -1.0)), 1.0),
		"near_db=%s base_db=%s receipt=%s" % [near_db, base_db, audio.last_spatial_sfx_receipt]
	)
	# The runtime weapon binding carries no parameter block, so its ranges are
	# the hand-tuned defaults and BOTH assumptions are named, never silent.
	_check(
		"unauthored_ranges_are_named_not_silent",
		audio.unranged_world_sfx.has("ImpactSword01")
			and audio.sfx_semantics_gaps.has("assumed-world-shrouded-unparameterized:ImpactSword01"),
		str(audio.unranged_world_sfx) + " " + str(audio.sfx_semantics_gaps.keys())
	)
	_silence_pool(audio)

	# ---- (B) the same visible event attenuates with camera distance --------
	# distance 500 with default MinRange 250 -> retail inverse gain 250/500 = 0.5.
	_positions[VISIBLE_ENTITY] = _listener + Vector2(500.0, 0.0)
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 3, "kind": "combat.member_swing", "entity_id": VISIBLE_ENTITY, "target_id": 8, "member_index": 1},
	])
	var far_players := _sounding("ImpactSword01", audio)
	var far_db: float = float((far_players[0] as AudioStreamPlayer).volume_db) if far_players.size() == 1 else 999.0
	_check(
		"distant_swing_attenuates_by_inverse_distance",
		far_players.size() == 1
			and is_equal_approx(float(audio.last_spatial_sfx_receipt.get("gain", -1.0)), 0.5)
			and absf(far_db - (base_db + linear_to_db(0.5))) < 0.01,
		"far_db=%s expected=%s receipt=%s" % [far_db, base_db + linear_to_db(0.5), audio.last_spatial_sfx_receipt]
	)
	_check("distant_swing_is_quieter_than_adjacent_swing", far_db < near_db, "far=%s near=%s" % [far_db, near_db])
	_silence_pool(audio)

	# ---- (B) beyond MaxRange the sound is culled, retail-style --------------
	_positions[VISIBLE_ENTITY] = _listener + Vector2(800.0, 0.0)
	audio._next_event_index = 0
	audio.sync_events([
		{"sequence": 4, "kind": "combat.member_swing", "entity_id": VISIBLE_ENTITY, "target_id": 8, "member_index": 2},
	])
	_check(
		"beyond_max_range_is_culled_and_counted",
		int(audio.distance_sfx_drops.get("ImpactSword01", 0)) == 1 and _sounding("ImpactSword01", audio).is_empty(),
		str(audio.distance_sfx_drops)
	)

	# ---- (A) an AUTHORED `world shrouded` definition gates the same way -----
	# ImpactHorse ships `Type = world shrouded everyone` verbatim in the pack.
	audio.call("_play_sfx", audio.route_audio_event("ImpactHorse", 5), SHROUDED_POS)
	_check(
		"authored_shrouded_definition_is_culled_under_shroud",
		int(audio.shrouded_sfx_drops.get("ImpactHorse", 0)) == 1,
		str(audio.shrouded_sfx_drops)
	)

	# ---- (C) regression: no position still means flat + the named gap -------
	audio.call("_play_sfx", audio.route_audio_event("ImpactHorse", 6))
	_check(
		"world_event_without_position_keeps_the_named_gap",
		audio.sfx_semantics_gaps.has("unsupported-type:world:ImpactHorse")
			and _sounding("ImpactHorse", audio).size() == 1,
		str(audio.sfx_semantics_gaps.keys())
	)

	audio.dispose()
	audio.free()
	_finish()


func _mock_position(id: int, _is_structure: bool) -> Variant:
	return _positions.get(id, null)


func _mock_visible(position: Vector2) -> bool:
	return position != SHROUDED_POS


func _mock_listener() -> Variant:
	return _listener


func _sounding(event_id: String, audio) -> Array:
	var result: Array = []
	for pooled in audio.sfx_players:
		if pooled != null and is_instance_valid(pooled) and pooled.playing and String(pooled.get_meta("retail_event_id", "")) == event_id:
			result.append(pooled)
	return result


func _silence_pool(audio) -> void:
	for pooled in audio.sfx_players:
		if pooled != null and is_instance_valid(pooled):
			pooled.stop()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_SFX_SHROUD_ATTENUATION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
