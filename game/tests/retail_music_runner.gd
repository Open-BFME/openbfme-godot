extends SceneTree
## Focused retail music playlist engine audit. Proves per-state playlists are
## built from the selected pack, that tracks advance on the `finished` signal
## with shuffle-no-immediate-repeat, that state changes and track advances keep
## deterministic crossfade bookkeeping, and that mute/volume settings are
## respected. Never writes or copies retail payloads.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MUSIC_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return
	content_db.reload()
	# Same shared host resolution production uses. Taking the root off the shared
	# soldier document instead pointed this runner at whichever supplement won
	# that object id — a faction pack with no assets/audio/music — and every
	# playlist assertion then failed on content the game never reads from there.
	# See the header of retail_vertical_slice._resolve_host_slice_pack.
	var selected_pack_root := String(
		PackCapabilityScript.resolve_host_slice_pack(content_db.pack_meta).get("root", "")
	)
	var external_root := OS.get_environment("OPENBFME_CONTENT")
	_check(
		"selected_private_pack_root_available",
		selected_pack_root != "" and external_root != "" and mod_loader.path_is_within(external_root, selected_pack_root),
		selected_pack_root
	)
	if selected_pack_root == "":
		_finish()
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(selected_pack_root, true)
	_check("production_observability_disabled", not audio.observability_enabled)
	audio._set_music("explore")
	_check(
		"production_transition_keeps_state_without_history",
		audio.current_music_state == "explore"
		and audio.current_music_track_index == 0
		and audio.music_player.playing
		and audio.music_transition_log.is_empty()
	)
	audio.observability_enabled = true
	audio._set_music("battle")

	# -- Playlist construction from the selected pack --------------------------
	for state in AudioScript.MUSIC_STATES:
		_check("playlist_built_%s" % state, audio.music_playlists.has(state) and (audio.music_playlists[state] as Array).size() > 0)
		_check(
			"playlist_primary_matches_legacy_stream_%s" % state,
			audio.music_streams.get(state) == (audio.music_playlists.get(state, []) as Array)[0]
		)
		_check(
			"playlist_streams_and_paths_aligned_%s" % state,
			(audio.music_playlists.get(state, []) as Array).size() == (audio.music_playlist_paths.get(state, []) as Array).size()
		)

	var explore_paths: Array = audio.music_playlist_paths.get("explore", [])
	var battle_paths: Array = audio.music_playlist_paths.get("battle", [])
	var victory_paths: Array = audio.music_playlist_paths.get("victory", [])
	var defeat_paths: Array = audio.music_playlist_paths.get("defeat", [])
	_check("explore_playlist_single_track", explore_paths.size() == 1, str(explore_paths))
	_check("battle_playlist_has_two_tracks", battle_paths.size() == 2, str(battle_paths))
	_check("victory_playlist_single_track", victory_paths.size() == 1, str(victory_paths))
	_check("defeat_playlist_single_track", defeat_paths.size() == 1, str(defeat_paths))
	_check("battle_primary_is_exact_battle_leaf", String(battle_paths[0]).get_file().to_lower() == "battle.mp3", str(battle_paths))
	_check(
		"battle_playlist_includes_alternate_leaf",
		_paths_contain_file(battle_paths, "battle-alternate.mp3"),
		str(battle_paths)
	)
	_check("all_playlist_tracks_are_private_pack_leaves", _all_paths_within(audio, mod_loader, external_root), "")
	_check("orphan_building_track_never_enters_a_playlist", not _any_playlist_contains_file(audio, "building.mp3"), "")

	# -- State change crossfade bookkeeping (sim entry point) ------------------
	audio._set_music("explore")
	_check("state_change_sets_state_explore", audio.current_music_state == "explore")
	_check("state_change_starts_at_first_track", audio.current_music_track_index == 0)
	_check(
		"state_change_active_player_holds_primary_stream",
		audio.music_player.stream == audio.music_playlists["explore"][0] and audio.music_player.playing
	)
	var log_after_explore: int = audio.music_transition_log.size()
	_check("state_change_records_transition", log_after_explore >= 1 and String(audio.music_transition_log[-1].get("reason", "")) == "state-change")
	_check("state_change_marks_crossfade_enabled", bool(audio.music_transition_log[-1].get("crossfade", false)))

	# Re-issuing the same state is a no-op so event bursts never restart music.
	audio._set_music("explore")
	_check("repeated_state_is_noop", audio.music_transition_log.size() == log_after_explore and audio.current_music_track_index == 0)

	audio._set_music("battle")
	_check("state_change_to_battle_uses_primary", audio.music_player.stream == audio.music_playlists["battle"][0] and audio.current_music_track_index == 0)
	_check("state_change_to_battle_active_player_plays", audio.music_player.playing)
	_check("state_change_grows_transition_log", audio.music_transition_log.size() == log_after_explore + 1)

	# -- Advance on finished: shuffle-no-immediate-repeat (battle, 2 tracks) ---
	var prev_index: int = audio.current_music_track_index
	var advance_ok := true
	var no_repeat_ok := true
	var stream_matches_ok := true
	for _i in 12:
		audio._on_music_finished(audio.music_player)
		var new_index: int = audio.current_music_track_index
		if new_index == prev_index:
			no_repeat_ok = false
		if String(audio.music_transition_log[-1].get("reason", "")) != "advance":
			advance_ok = false
		if audio.music_player.stream != audio.music_playlists["battle"][new_index]:
			stream_matches_ok = false
		prev_index = new_index
	_check("advance_on_finished_records_advance_reason", advance_ok)
	_check("multi_track_advance_never_immediately_repeats", no_repeat_ok)
	_check("advance_swaps_active_player_stream_to_new_leaf", stream_matches_ok)
	_check("advance_keeps_active_player_playing", audio.music_player.playing)

	# Ignore finished from the idle (non-active) player.
	var log_before_ignore: int = audio.music_transition_log.size()
	audio._on_music_finished(audio._music_player_alt)
	_check("finished_from_idle_player_ignored", audio.music_transition_log.size() == log_before_ignore)

	# -- Single-track state loops rather than going silent ---------------------
	audio._set_music("victory")
	var victory_stream = audio.music_player.stream
	audio._on_music_finished(audio.music_player)
	_check("single_track_state_loops_same_index", audio.current_music_track_index == 0)
	_check("single_track_state_loops_same_stream", audio.music_player.stream == victory_stream and audio.music_player.stream == audio.music_playlists["victory"][0])
	_check("single_track_loop_keeps_playing", audio.music_player.playing)

	# -- Direct no-immediate-repeat invariant ----------------------------------
	var choose_ok := true
	for _j in 50:
		if audio._choose_next_music_index("battle", 0) != 1:
			choose_ok = false
		if audio._choose_next_music_index("battle", 1) != 0:
			choose_ok = false
	_check("choose_next_never_returns_current_for_multi_track", choose_ok)
	_check("choose_next_loops_single_track_state", audio._choose_next_music_index("victory", 0) == 0)

	# -- Mute / volume interaction ---------------------------------------------
	audio._set_music("battle")
	audio.set_music_volume(0.5, false)
	var expected_half := UserSettingsScript.volume_to_db(0.5, false)
	_check("music_volume_applies_to_active_player", is_equal_approx(audio.music_player.volume_db, expected_half), str(audio.music_player.volume_db))
	_check("idle_music_player_stays_silent", is_equal_approx(audio._music_player_alt.volume_db, UserSettingsScript.SILENT_DB), str(audio._music_player_alt.volume_db))
	audio.set_muted(true, false)
	_check("mute_silences_active_music_player", is_equal_approx(audio.music_player.volume_db, UserSettingsScript.SILENT_DB) and audio.is_muted())
	audio.set_muted(false, false)
	_check("unmute_restores_configured_volume", is_equal_approx(audio.music_player.volume_db, expected_half))

	# -- Headless determinism with playback disabled ---------------------------
	var silent_audio = AudioScript.new()
	root.add_child(silent_audio)
	silent_audio.configure(selected_pack_root, false)
	silent_audio.observability_enabled = true
	_check("disabled_engine_still_builds_playlists", silent_audio.music_playlists.has("battle") and (silent_audio.music_playlists["battle"] as Array).size() == 2)
	silent_audio._set_music("battle")
	_check("disabled_state_change_tracks_index", silent_audio.current_music_state == "battle" and silent_audio.current_music_track_index == 0)
	_check("disabled_engine_emits_no_audio", not silent_audio.music_player.playing)
	_check("disabled_transition_marks_no_crossfade", not bool(silent_audio.music_transition_log[-1].get("crossfade", true)))
	var disabled_prev: int = silent_audio.current_music_track_index
	var disabled_no_repeat := true
	for _k in 8:
		silent_audio._on_music_finished(silent_audio.music_player)
		if silent_audio.current_music_track_index == disabled_prev:
			disabled_no_repeat = false
		disabled_prev = silent_audio.current_music_track_index
	_check("disabled_engine_advances_deterministically", disabled_no_repeat and not silent_audio.music_player.playing)

	silent_audio.dispose()
	silent_audio.free()
	audio.dispose()
	audio.free()
	create_timer(0.5, true, false, true).timeout.connect(_finish)


func _paths_contain_file(paths: Array, file_name: String) -> bool:
	for path in paths:
		if String(path).get_file().to_lower() == file_name.to_lower():
			return true
	return false


func _any_playlist_contains_file(audio, file_name: String) -> bool:
	for state in AudioScript.MUSIC_STATES:
		if _paths_contain_file(audio.music_playlist_paths.get(state, []), file_name):
			return true
	return false


func _all_paths_within(audio, mod_loader: Node, external_root: String) -> bool:
	if external_root == "":
		return false
	for state in AudioScript.MUSIC_STATES:
		for path in Array(audio.music_playlist_paths.get(state, [])):
			if not mod_loader.path_is_within(external_root, String(path)) or not FileAccess.file_exists(String(path)):
				return false
	return true


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_MUSIC_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
