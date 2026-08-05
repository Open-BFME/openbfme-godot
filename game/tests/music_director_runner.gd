extends SceneTree
## Asserts every playable faction resolves a NON-EMPTY, FACTION-CORRECT music
## playlist out of pack data, and that the director is what selects it.
##
## No audio is played and no retail bytes are read. The fixture below mirrors
## the SHAPE the importer emits (`openbfme.music`, see
## importer/openbfme_importer/music_import.py) with synthetic track ids, so this
## runner is fast and runs with or without a music pack installed. When a real
## music pack IS installed, the same assertions are re-run against it.

const MusicDirector = preload("res://src/core/music_director.gd")
const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")

## The seven playable pack faction ids and the retail side token each maps to,
## mirroring game/data/retail_faction_sides.json.
const FACTION_SIDES := {
	"angmar": "Angmar",
	"dwarves": "Dwarves",
	"elves": "Elves",
	"isengard": "Isengard",
	"men": "Men",
	"mordor": "Mordor",
	"wild": "Wild",
}

## Alignment per side, as the retail ___MusicScript_Test* scripts declare it.
const SIDE_ALIGNMENT := {
	"Men": "good",
	"Elves": "good",
	"Dwarves": "good",
	"Isengard": "evil",
	"Mordor": "evil",
	"Wild": "evil",
	"Angmar": "evil",
}

## Each side's authored ambient playlist family. Angmar deliberately shares
## Mordor's: RotWK's ___MusicScript_TestAngmar sets the SAME selector (5) as
## ___MusicScript_TestMordor, so Angmar hears Mordor's music in retail.
const SIDE_FAMILY := {
	"Men": "Mannish",
	"Elves": "Elven",
	"Dwarves": "Dwarven",
	"Isengard": "Isengard",
	"Mordor": "Mordor",
	"Wild": "Wild",
	"Angmar": "Mordor",
}

const PHASES: Array[String] = ["basebuilding", "explore", "explore2"]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_assert_fixture()
	_assert_installed_pack()
	print("MUSIC_DIRECTOR_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _assert_fixture() -> void:
	var document := _fixture_document()
	var director: RefCounted = MusicDirector.new()
	_check("fixture_document_configures", director.configure(document, FACTION_SIDES))
	_check("director_reports_ready", director.is_ready())

	for faction_id in FACTION_SIDES.keys():
		var side := String(FACTION_SIDES[faction_id])
		_check(
			"side_resolves_%s" % faction_id,
			director.resolve_side(String(faction_id)) == side
		)
		# EVERY faction must answer with a non-empty list in EVERY ambient
		# phase. A silent faction is the exact failure this runner exists for.
		for phase in PHASES:
			var tracks: Array[String] = director.track_ids_for(String(faction_id), phase)
			_check("%s_%s_non_empty" % [faction_id, phase], not tracks.is_empty())
			_check(
				"%s_%s_is_faction_correct" % [faction_id, phase],
				director.playlist_id_for(String(faction_id), phase)
					== _expected_playlist(side, phase)
			)
			_check(
				"%s_%s_files_resolve" % [faction_id, phase],
				director.track_files_for(String(faction_id), phase).size() == tracks.size()
			)
		# Level-1 (action / triumphal) is authored per ALIGNMENT, not per side.
		var alignment := String(SIDE_ALIGNMENT[side])
		_check(
			"%s_action_matches_alignment" % faction_id,
			director.playlist_id_for(String(faction_id), "action")
				== "Action%sMusic" % alignment.capitalize()
		)
		_check(
			"%s_triumphal_matches_alignment" % faction_id,
			director.playlist_id_for(String(faction_id), "triumphal")
				== "Triumphal%sMusic" % alignment.capitalize()
		)
		# Score-screen leaves: alignment-bound, except Angmar's dedicated leaf.
		var expected_victory := (
			"VictoryScreenAngmar" if side == "Angmar"
			else "VictoryScreen%s" % alignment.capitalize()
		)
		_check(
			"%s_victory_leaf" % faction_id,
			director.playlist_id_for(String(faction_id), "victory") == expected_victory
		)
		_check(
			"%s_defeat_leaf" % faction_id,
			director.playlist_id_for(String(faction_id), "defeat")
				== "LoseScreen%s" % alignment.capitalize()
		)

	# Good and evil must never collapse onto the same ambient playlist.
	_check(
		"good_and_evil_ambient_differ",
		director.playlist_id_for("men", "basebuilding")
			!= director.playlist_id_for("mordor", "basebuilding")
	)
	# ...but Angmar and Mordor must, because retail authors it that way.
	_check(
		"angmar_shares_mordor_ambient",
		director.playlist_id_for("angmar", "explore")
			== director.playlist_id_for("mordor", "explore")
	)
	# Two evil factions that DON'T share a selector must still differ.
	_check(
		"isengard_and_mordor_ambient_differ",
		director.playlist_id_for("isengard", "explore")
			!= director.playlist_id_for("mordor", "explore")
	)

	# Authored control flags drive rotation, not a guess in the player.
	_check("play_one_is_shuffle", director.shuffles("men", "basebuilding"))
	_check("ambient_does_not_loop_one_leaf", not director.loops("men", "basebuilding"))
	_check("shell_loops", director.loops("men", "shell") == false)
	_check("shell_playlist_declared", director.shell_playlist_id() == "ShellLowLOD")
	_check("shell_tracks_non_empty", not director.shell_track_ids().is_empty())
	# THE MENU'S BINDING. Retail declares the shell theme in miscaudio.ini's
	# MiscAudio block (`LowLODShellMusic = ShellLowLOD`) and backs it in
	# music.ini with `Multisound ShellLowLOD / Control = PLAY_ONE LOOP`. The
	# director must carry BOTH flags forward, because the menu player reads them
	# to decide whether it picks one entry from a pool and whether it repeats.
	_check("shell_control_is_play_one_loop", director.shell_control() == ["play_one", "loop"])
	_check("shell_shuffles_from_play_one", director.shell_shuffles())
	_check("shell_loops_from_loop_flag", director.shell_loops())
	_check(
		"shell_track_files_resolve",
		director.shell_track_files().size() == director.shell_track_ids().size()
	)
	# High and low LOD are separate MiscAudio keys and must be readable apart.
	_check("shell_high_lod_declared", director.shell_playlist_id("shellHighLod") != "")
	_check("shell_unknown_key_refuses", director.shell_playlist_id("shellSideways") == "")
	_check("shell_unknown_key_has_no_tracks", director.shell_track_ids("shellSideways").is_empty())
	# Without a pack root there is nowhere to resolve a file TO, and the honest
	# answer is an empty list rather than an unresolved relative path.
	_check("shell_paths_need_a_pack_root", director.shell_track_paths().is_empty())

	# The four states this runtime emits bind to authored slots, and nothing else.
	_check("state_explore_slot", director.slot_for_state("explore") == "basebuilding")
	_check("state_battle_slot", director.slot_for_state("battle") == "action")
	_check("state_victory_slot", director.slot_for_state("victory") == "victory")
	_check("state_defeat_slot", director.slot_for_state("defeat") == "defeat")
	_check("unknown_state_refuses", director.slot_for_state("dancing") == "")

	# Unknown factions REFUSE rather than inheriting a default faction's music.
	_check("unknown_faction_refuses", director.resolve_side("moria") == "")
	_check(
		"unknown_faction_has_no_playlist",
		director.track_ids_for("moria", "explore").is_empty()
	)

	# A report row per faction is what an operator reads.
	var report: Dictionary = director.selection_report()
	_check("report_covers_every_faction", report.size() == FACTION_SIDES.size())

	# Fail-closed configuration.
	var empty: RefCounted = MusicDirector.new()
	_check("empty_document_refuses", not empty.configure({}, FACTION_SIDES))
	_check("empty_document_says_why", not empty.diagnostics.is_empty())
	var foreign: RefCounted = MusicDirector.new()
	_check(
		"foreign_schema_refuses",
		not foreign.configure({"schema": "openbfme.objects", "schemaVersion": 0}, FACTION_SIDES)
	)


func _assert_installed_pack() -> void:
	## Only meaningful once a music pack is published into the selected content
	## root; otherwise this is a no-op so the runner stays fast and green.
	var content_db: Node = root.get_node_or_null("ContentDB")
	if content_db == null:
		return
	var document: Dictionary = content_db.get("music_document")
	if document.is_empty():
		return
	var director: RefCounted = MusicDirector.new()
	_check("installed_pack_configures", director.configure(document))
	for faction_id in FACTION_SIDES.keys():
		var side := String(FACTION_SIDES[faction_id])
		var factions: Dictionary = document.get("factions", {})
		if not factions.has(side):
			continue
		for phase in PHASES:
			_check(
				"installed_%s_%s_non_empty" % [faction_id, phase],
				not director.track_ids_for(String(faction_id), phase).is_empty()
			)
		_check(
			"installed_%s_ambient_is_faction_correct" % faction_id,
			director.playlist_id_for(String(faction_id), "basebuilding")
				== _expected_playlist(side, "basebuilding")
		)
	_check("installed_shell_playlist_declared", director.shell_playlist_id() != "")
	_check("installed_shell_tracks_non_empty", not director.shell_track_ids().is_empty())
	_check(
		"installed_shell_files_resolve",
		director.shell_track_files().size() == director.shell_track_ids().size()
	)
	_check("installed_shell_paths_resolve", not director.shell_track_paths().is_empty())
	_assert_slice_audio_binds(document)
	_assert_shell_music_binds()


func _assert_shell_music_binds() -> void:
	## THE MENU-MUSIC HOOKUP. `GameAudio.set_music_state` had no caller at all,
	## which is why the main menu was silent. This closes that loop: the autoload
	## the menu asks must actually resolve the AUTHORED shell playlist out of the
	## installed pack. Playback is not asserted - only the resolved binding - so
	## this stays a headless check.
	var shell_audio: Node = root.get_node_or_null("GameAudio")
	if shell_audio == null:
		return
	_check("shell_audio_exposes_state_setter", shell_audio.has_method("set_music_state"))
	_check("shell_audio_exposes_handoff", shell_audio.has_method("stop_music"))
	if not shell_audio.has_method("_resolve_shell_music"):
		_check("shell_audio_exposes_resolver", false)
		return
	var resolved: bool = bool(shell_audio.call("_resolve_shell_music"))
	_check("shell_audio_resolves_installed_playlist", resolved)
	if not resolved:
		var why: Variant = shell_audio.get("shell_music_diagnostics")
		if typeof(why) == TYPE_ARRAY:
			push_error("MUSIC_DIRECTOR_SHELL_WHY %s" % str(why))
		return
	_check(
		"shell_audio_playlist_matches_director",
		String(shell_audio.get("shell_music_playlist")) != ""
	)
	var paths: Variant = shell_audio.get("shell_music_paths")
	_check(
		"shell_audio_binds_playable_paths",
		typeof(paths) == TYPE_ARRAY and not (paths as Array).is_empty()
	)
	# ROTWK authors the shell as PLAY_ONE LOOP; both flags must reach the player.
	_check("shell_audio_carries_loop_flag", bool(shell_audio.get("shell_music_loops")))
	_check("shell_audio_carries_play_one_flag", bool(shell_audio.get("shell_music_shuffles")))


func _assert_slice_audio_binds(document: Dictionary) -> void:
	## Closes the loop: the real in-game audio object must actually REPLACE its
	## filename-convention playlists with the authored faction ones. Playback
	## stays off; only the resolved binding is asserted.
	var pack_root := String(document.get("_pack_root", ""))
	if pack_root == "":
		return
	var audio := AudioScript.new()
	root.add_child(audio)
	audio.configure(pack_root, false, {}, {}, "Men")
	var slots: Dictionary = audio.music_faction_slots
	_check("slice_audio_binds_faction_music", not slots.is_empty())
	if not slots.is_empty():
		var explore: Dictionary = slots.get("explore", {})
		_check(
			"slice_audio_explore_uses_authored_playlist",
			String(explore.get("playlist", "")) == "BaseBuildingMannishMusic"
		)
		_check("slice_audio_explore_has_tracks", int(explore.get("tracks", 0)) > 1)
		_check("slice_audio_explore_shuffles", bool(explore.get("shuffle", false)))
		var battle: Dictionary = slots.get("battle", {})
		_check(
			"slice_audio_battle_uses_authored_playlist",
			String(battle.get("playlist", "")) == "ActionGoodMusic"
		)
	audio.queue_free()


func _expected_playlist(side: String, phase: String) -> String:
	var family := String(SIDE_FAMILY[side])
	match phase:
		"basebuilding":
			return "BaseBuilding%sMusic" % family
		"explore":
			return "Explore%sMusic" % family
		"explore2":
			return "Explore2%sMusic" % family
		_:
			return ""


func _fixture_document() -> Dictionary:
	var tracks: Dictionary = {}
	var playlists: Dictionary = {}

	var add_playlist := func(identifier: String, control: Array, count: int) -> void:
		var members: Array = []
		for index in range(count):
			var track_id := "%s_%02d" % [identifier, index]
			tracks[track_id] = {
				"id": track_id,
				"file": "audio/music/tracks/%s.mp3" % track_id.to_lower(),
				"source": "data/audio/tracks/%s.mp3" % track_id.to_lower(),
				"volume": 55,
			}
			members.append(track_id)
		playlists[identifier] = {
			"id": identifier,
			"control": control,
			"tracks": members,
			"kind": "multisound",
		}

	for family in ["Mannish", "Elven", "Dwarven", "Isengard", "Mordor", "Wild"]:
		add_playlist.call("BaseBuilding%sMusic" % family, ["play_one"], 3)
		add_playlist.call("Explore%sMusic" % family, ["play_one"], 4)
		add_playlist.call("Explore2%sMusic" % family, ["play_one"], 5)
	for alignment in ["Good", "Evil"]:
		add_playlist.call("Action%sMusic" % alignment, ["play_one"], 2)
		add_playlist.call("Triumphal%sMusic" % alignment, ["play_one"], 2)
	add_playlist.call("ShellLowLOD", ["play_one", "loop"], 1)

	for leaf in [
		"VictoryScreenGood", "VictoryScreenEvil", "VictoryScreenAngmar",
		"LoseScreenGood", "LoseScreenEvil",
	]:
		tracks[leaf] = {
			"id": leaf,
			"file": "audio/music/tracks/%s.mp3" % leaf.to_lower(),
			"source": "data/audio/tracks/%s.mp3" % leaf.to_lower(),
		}

	var factions: Dictionary = {}
	var selectors := {"Men": 1, "Elves": 2, "Dwarves": 3, "Isengard": 4, "Mordor": 5, "Angmar": 5, "Wild": 6}
	for side in SIDE_ALIGNMENT.keys():
		var alignment := String(SIDE_ALIGNMENT[side])
		var family := String(SIDE_FAMILY[side])
		factions[side] = {
			"side": side,
			"alignment": alignment,
			"selector": int(selectors[side]),
			"derivation": "script-bound",
			"phases": {
				"basebuilding": "BaseBuilding%sMusic" % family,
				"explore": "Explore%sMusic" % family,
				"explore2": "Explore2%sMusic" % family,
			},
			"level1": {
				"action": "Action%sMusic" % alignment.capitalize(),
				"triumphal": "Triumphal%sMusic" % alignment.capitalize(),
			},
			"screens": {
				"victory": (
					"VictoryScreenAngmar" if side == "Angmar"
					else "VictoryScreen%s" % alignment.capitalize()
				),
				"defeat": "LoseScreen%s" % alignment.capitalize(),
			},
			"screensDerivation": "name-bound",
		}

	return {
		"schema": "openbfme.music",
		"schemaVersion": 0,
		"source": {"fixture": true},
		"factions": factions,
		"playlists": playlists,
		"tracks": tracks,
		"shell": {"shellLowLod": "ShellLowLOD", "shellHighLod": "ShellLowLOD"},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("MUSIC_DIRECTOR_FAIL %s" % label)
