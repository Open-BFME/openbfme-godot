class_name MusicDirector
extends RefCounted
## Selects the music playlist the LOCAL player's faction is authored to hear.
##
## WHAT RETAIL AUTHORS (see importer/openbfme_importer/music_import.py for the
## extraction, and the report it cites for the INI/script evidence):
##
##   * `music.ini` declares MusicTrack leaves and Multisound playlists.
##   * `audiosettings.ini` names a global music-script library map, and that
##     map's `___MusicScript_Test<Faction>` scripts compare
##     SKIRMISH_PLAYER_FACTION for `<Local Player>` against a PlayerTemplate
##     `Side` token, then stash a selector in a counter. The
##     `___MusicScript_Do*` scripts gate on that selector plus an `InPhase*`
##     flag and start one multisound.
##
## So the join key is the retail SIDE token, not the pack faction id. This
## director resolves pack faction id -> side through the same
## `retail_faction_sides.json` table the rest of the runtime uses, then reads
## the side's authored playlists straight out of the pack's `data/music.json`.
##
## WHAT THIS DIRECTOR DOES NOT DO (deliberately, and named as follow-up):
## retail's phase ladder and combat switching are a real state machine -
## basebuilding -> explore happens on "N units further than D from the home
## base" OR a randomised turtle timer OR three action tracks played;
## explore -> explore2 on a level cap / end-level science; the action and
## triumphal tracks are PUSHED over the ambient one on ENGAGED model-condition
## counts and EVA events, then POPPED. None of those inputs exist as authored
## triggers in this runtime yet, so inventing thresholds here would be
## inventing semantics. Every slot IS resolved and shipped; only the automatic
## transitions between them are pending.

const RETAIL_FACTION_SIDES_PATH := "res://data/retail_faction_sides.json"

const SCHEMA := "openbfme.music"
const SCHEMA_VERSION := 0

## The authored slots a caller may ask for. `basebuilding`/`explore`/`explore2`
## are the retail level-0 ambient ladder; `action`/`triumphal` are the level-1
## tracks retail pushes over them; `victory`/`defeat` are the score-screen
## leaves; `shell` is the menu.
const SLOTS: Array[String] = [
	"basebuilding",
	"explore",
	"explore2",
	"action",
	"triumphal",
	"victory",
	"defeat",
]

## How the four states this runtime's simulation actually emits map onto the
## authored slots. This table is the ONLY place the mapping is decided, so a
## reader can see exactly which part is retail-authored (the slot contents) and
## which part is this project's binding (which slot answers which state).
const STATE_SLOTS := {
	"explore": "basebuilding",
	"battle": "action",
	"victory": "victory",
	"defeat": "defeat",
}

var document: Dictionary = {}
var pack_root: String = ""
var sides: Dictionary = {}
var diagnostics: Array[String] = []


static func load_retail_sides(path: String = RETAIL_FACTION_SIDES_PATH) -> Dictionary:
	## pack faction id -> retail PlayerTemplate `Side` token.
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	var value: Variant = (parsed as Dictionary).get("sides", {})
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	return (value as Dictionary).duplicate(true)


func configure(music_document: Dictionary, retail_sides: Dictionary = {}) -> bool:
	## Bind a parsed `data/music.json`. Returns false (and says why in
	## `diagnostics`) when the document cannot answer for any faction, so a
	## caller can fall back rather than play silence and call it success.
	diagnostics.clear()
	document = {}
	pack_root = ""
	sides = retail_sides if not retail_sides.is_empty() else load_retail_sides()
	if music_document.is_empty():
		diagnostics.append("music: no music document was supplied")
		return false
	if String(music_document.get("schema", "")) != SCHEMA:
		diagnostics.append("music: document schema is not %s" % SCHEMA)
		return false
	if int(music_document.get("schemaVersion", -1)) != SCHEMA_VERSION:
		diagnostics.append("music: document schemaVersion is not %d" % SCHEMA_VERSION)
		return false
	for key in ["factions", "playlists", "tracks"]:
		if typeof(music_document.get(key, null)) != TYPE_DICTIONARY:
			diagnostics.append("music: document has no '%s' object" % key)
			return false
	if (music_document["factions"] as Dictionary).is_empty():
		diagnostics.append("music: document binds no faction")
		return false
	if sides.is_empty():
		diagnostics.append("music: retail faction side table is empty")
		return false
	document = music_document
	pack_root = String(music_document.get("_pack_root", ""))
	return true


func is_ready() -> bool:
	return not document.is_empty()


func resolve_side(faction_id: String) -> String:
	## Pack faction id -> retail side token. Unknown factions REFUSE (empty)
	## rather than silently answering with a default faction's music.
	var slug := faction_id.strip_edges().to_lower()
	if slug == "":
		return ""
	return String(sides.get(slug, ""))


func faction_binding(faction_id: String) -> Dictionary:
	var side := resolve_side(faction_id)
	if side == "":
		return {}
	var factions: Dictionary = document.get("factions", {})
	var row: Variant = factions.get(side, null)
	if typeof(row) != TYPE_DICTIONARY:
		return {}
	return (row as Dictionary).duplicate(true)


func playlist_id_for(faction_id: String, slot: String) -> String:
	## The authored Multisound/MusicTrack id backing one slot for one faction.
	var binding := faction_binding(faction_id)
	if binding.is_empty():
		return ""
	match slot:
		"basebuilding", "explore", "explore2":
			var phases: Variant = binding.get("phases", {})
			if typeof(phases) != TYPE_DICTIONARY:
				return ""
			return String((phases as Dictionary).get(slot, ""))
		"action", "triumphal":
			var level1: Variant = binding.get("level1", {})
			if typeof(level1) != TYPE_DICTIONARY:
				return ""
			return String((level1 as Dictionary).get(slot, ""))
		"victory", "defeat":
			var screens: Variant = binding.get("screens", {})
			if typeof(screens) != TYPE_DICTIONARY:
				return ""
			return String((screens as Dictionary).get(slot, ""))
		_:
			return ""


func track_ids_for(faction_id: String, slot: String) -> Array[String]:
	## Ordered authored track ids for a slot. A slot backed by a bare
	## MusicTrack (the victory/defeat screen leaves) answers with that one id.
	var identifier := playlist_id_for(faction_id, slot)
	return _track_ids_of(identifier)


func _track_ids_of(identifier: String) -> Array[String]:
	var result: Array[String] = []
	if identifier == "":
		return result
	var playlists: Dictionary = document.get("playlists", {})
	var tracks: Dictionary = document.get("tracks", {})
	var playlist: Variant = playlists.get(identifier, null)
	if typeof(playlist) == TYPE_DICTIONARY:
		var members: Variant = (playlist as Dictionary).get("tracks", [])
		if typeof(members) == TYPE_ARRAY:
			for member in members as Array:
				var track_id := String(member)
				# A FAKE (fileless) leaf such as `Silence` stays out of a
				# playable list; the document records it under silentTracks.
				if tracks.has(track_id):
					result.append(track_id)
		return result
	if tracks.has(identifier):
		result.append(identifier)
	return result


func playlist_control(faction_id: String, slot: String) -> Array[String]:
	## Authored `Control =` flags (play_one, loop, random_start, fade_on_kill).
	var identifier := playlist_id_for(faction_id, slot)
	var result: Array[String] = []
	if identifier == "":
		return result
	var playlists: Dictionary = document.get("playlists", {})
	var playlist: Variant = playlists.get(identifier, null)
	if typeof(playlist) != TYPE_DICTIONARY:
		return result
	var flags: Variant = (playlist as Dictionary).get("control", [])
	if typeof(flags) != TYPE_ARRAY:
		return result
	for flag in flags as Array:
		result.append(String(flag))
	return result


func shuffles(faction_id: String, slot: String) -> bool:
	## PLAY_ONE means "start one of these", i.e. the list is a pool to pick
	## from rather than an ordered sequence. Absent the flag, play in order.
	return playlist_control(faction_id, slot).has("play_one")


func loops(faction_id: String, slot: String) -> bool:
	return playlist_control(faction_id, slot).has("loop")


func track_files_for(faction_id: String, slot: String) -> Array[String]:
	## Pack-relative asset paths (e.g. `audio/music/tracks/bagood04_t06.mp3`).
	return _track_files_of(track_ids_for(faction_id, slot))


func _track_files_of(track_ids: Array[String]) -> Array[String]:
	var result: Array[String] = []
	var tracks: Dictionary = document.get("tracks", {})
	for track_id in track_ids:
		var row: Variant = tracks.get(track_id, null)
		if typeof(row) != TYPE_DICTIONARY:
			continue
		var file := String((row as Dictionary).get("file", ""))
		if file != "":
			result.append(file)
	return result


func track_paths_for(faction_id: String, slot: String) -> Array[String]:
	## Absolute, containment-checked paths inside the music pack.
	return _track_paths_of(track_files_for(faction_id, slot))


func _track_paths_of(files: Array[String]) -> Array[String]:
	var result: Array[String] = []
	if pack_root == "":
		return result
	# Resolved through ModLoader so the containment check that guards every
	# other pack-relative path guards these too. Looked up as a node rather
	# than as the `ModLoader` global because this script is preloaded by
	# headless --script runners, which compile before autoloads are registered.
	var loader: Node = _mod_loader()
	if loader == null:
		return result
	for file in files:
		var resolved := String(loader.resolve_pack_path(pack_root, "assets/%s" % file))
		if resolved != "":
			result.append(resolved)
	return result


func _mod_loader() -> Node:
	var loop := Engine.get_main_loop()
	if loop == null or not (loop is SceneTree):
		return null
	var tree := loop as SceneTree
	if tree.root == null:
		return null
	return tree.root.get_node_or_null("ModLoader")


func slot_for_state(state: String) -> String:
	return String(STATE_SLOTS.get(state, ""))


func track_paths_for_state(faction_id: String, state: String) -> Array[String]:
	var slot := slot_for_state(state)
	if slot == "":
		return []
	return track_paths_for(faction_id, slot)


func shell_playlist_id(key: String = "shellLowLod") -> String:
	## The menu keeps its authored shell music; this is that declaration,
	## read from miscaudio.ini's MiscAudio block.
	var shell: Variant = document.get("shell", {})
	if typeof(shell) != TYPE_DICTIONARY:
		return ""
	return String((shell as Dictionary).get(key, ""))


func shell_track_ids(key: String = "shellLowLod") -> Array[String]:
	return _track_ids_of(shell_playlist_id(key))


func shell_track_files(key: String = "shellLowLod") -> Array[String]:
	return _track_files_of(shell_track_ids(key))


func shell_track_paths(key: String = "shellLowLod") -> Array[String]:
	## Absolute, containment-checked paths for the authored menu playlist.
	return _track_paths_of(shell_track_files(key))


func shell_control(key: String = "shellLowLod") -> Array[String]:
	## The shell slot's authored `Control =` flags. Retail's ROTWK music.ini
	## declares `Multisound ShellLowLOD` with `Control = PLAY_ONE LOOP`, and
	## `miscaudio.ini`'s MiscAudio block names it as `LowLODShellMusic`. Both
	## flags matter to the menu: PLAY_ONE makes the subsound list a pool to pick
	## ONE entry from, LOOP makes that entry repeat until the menu is left.
	##
	## A MiscAudio slot may also name a bare MusicTrack rather than a Multisound
	## (retail's `Shell2Music` is one); such a slot declares no flags, and the
	## empty array is the honest answer rather than an invented default.
	var identifier := shell_playlist_id(key)
	var result: Array[String] = []
	if identifier == "":
		return result
	var playlists: Dictionary = document.get("playlists", {})
	var playlist: Variant = playlists.get(identifier, null)
	if typeof(playlist) != TYPE_DICTIONARY:
		return result
	var flags: Variant = (playlist as Dictionary).get("control", [])
	if typeof(flags) != TYPE_ARRAY:
		return result
	for flag in flags as Array:
		result.append(String(flag))
	return result


func shell_shuffles(key: String = "shellLowLod") -> bool:
	return shell_control(key).has("play_one")


func shell_loops(key: String = "shellLowLod") -> bool:
	return shell_control(key).has("loop")


func selection_report() -> Dictionary:
	## Diagnostic snapshot: which faction resolves which playlist, and how big
	## it is. Runners assert on this rather than on audible playback.
	var factions: Dictionary = document.get("factions", {})
	var rows: Dictionary = {}
	for faction_id in sides.keys():
		var side := resolve_side(String(faction_id))
		if not factions.has(side):
			continue
		var slots: Dictionary = {}
		for slot in SLOTS:
			slots[slot] = {
				"playlist": playlist_id_for(String(faction_id), slot),
				"tracks": track_ids_for(String(faction_id), slot).size(),
			}
		rows[String(faction_id)] = {"side": side, "slots": slots}
	return rows
