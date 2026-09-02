class_name NativeMatchLaunch
extends RefCounted
## Retail skirmish setup -> contracts/match-launch-v1. The shell stays the
## authority for choices; this file only validates and serializes the handoff.

const SCHEMA := "openbfme.match-launch.v1"
const SELECTION_SCHEMA := "openbfme.native-selection"
const SELECTION_VERSION := 1
const MAX_SELECTION_BYTES := 16 * 1024 * 1024
const TEAM_IDS := [0, 1, 3, 4, 5, 6, 7, 8]


static func is_available() -> bool:
	var context := native_context()
	return FileAccess.file_exists(String(context.get("bundle_path", ""))) and not (context.get("maps", []) as Array).is_empty()


static func native_context() -> Dictionary:
	var content_root := _content_root()
	var selection_path := content_root.path_join("native/selection.json")
	var selection := _read_json(selection_path, MAX_SELECTION_BYTES)
	if (
		String(selection.get("schema", "")) != SELECTION_SCHEMA
		or int(selection.get("version", -1)) != SELECTION_VERSION
	):
		selection = {}
	var bundle_relative := String(selection.get("bundle", ""))
	var bundle_path := OS.get_environment("OPENBFME_BUNDLE").strip_edges()
	if bundle_path.is_empty():
		bundle_path = _selected_path(content_root, bundle_relative)
	var maps: Array = selection.get("maps", []) as Array
	var map_override := OS.get_environment("OPENBFME_MAP").strip_edges()
	if maps.is_empty() and not map_override.is_empty() and FileAccess.file_exists(map_override):
		maps = [{"slug": _map_slug(map_override), "name": map_override.get_file().get_basename(), "path": map_override, "players": 8}]
	elif not map_override.is_empty() and FileAccess.file_exists(map_override):
		var override_slug := _map_slug(map_override)
		var override_document := _read_json(map_override, MAX_SELECTION_BYTES)
		var override_source := override_document.get("source", {}) as Dictionary
		if not String(override_source.get("path", "")).is_empty():
			override_slug = _map_slug(String(override_source.get("path", "")))
		for index in maps.size():
			if not (maps[index] is Dictionary):
				continue
			var row := (maps[index] as Dictionary).duplicate(true)
			var row_slug := _map_slug(String(row.get("slug", "")))
			if row_slug.contains(override_slug) or override_slug.contains(row_slug):
				row["path"] = map_override
				maps[index] = row
	# The launch contract identifies the bytes actually handed to the host. The
	# selection digest may describe another selected pack when an explicit bundle
	# override is active, so it can never substitute for hashing the resolved file.
	var digest := FileAccess.get_sha256(bundle_path).to_lower() if FileAccess.file_exists(bundle_path) else ""
	return {
		"content_root": content_root,
		"selection_path": selection_path,
		"bundle_path": bundle_path,
		"bundle_relative": bundle_relative,
		"pack_id": "native-%s" % digest.left(12) if _is_sha256(digest) else "native-core",
		"pack_sha256": digest,
		"maps": maps.duplicate(true),
	}


static func maps() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for value in native_context().get("maps", []) as Array:
		if value is Dictionary:
			result.append((value as Dictionary).duplicate(true))
	return result


static func build_from_setup(setup: Node, retail_map_id: String = "", retail_map_name: String = "") -> Dictionary:
	if setup == null:
		return {}
	var context := native_context()
	if not FileAccess.file_exists(String(context.get("bundle_path", ""))):
		return {}
	var map_row := choose_map(context.get("maps", []) as Array, retail_map_id, retail_map_name)
	if map_row.is_empty():
		return {}
	var map_contract_path := _map_contract_path(map_row, String(context.get("content_root", "")))
	if map_contract_path.is_empty():
		return {}
	var players := _players_from_setup(setup, int(map_row.get("players", 8)))
	if players.is_empty():
		return {}
	var resources := _selected_integer(setup.get("initial_resources_opt"), 1200)
	var cp_factor := _selected_float(setup.get("cp_factor_opt"), 1.0)
	var document := {
		"schema": SCHEMA,
		"seed": _stable_seed(retail_map_id, players),
		"pack": {
			"id": String(context.get("pack_id", "native-core")),
			"sha256": String(context.get("pack_sha256", "")),
		},
		"map": {"path": map_contract_path},
		"rules": {
			"tick_ms": 33,
			"starting_resources": maxi(0, resources),
			"command_point_multiplier": maxf(0.0, cp_factor),
			"fog_of_war": true,
			"game_speed": 1.0,
			"victory": "annihilation",
			"classic": false,
			"improvements": {"flow_field_pathing": true, "queue_across_buildings": true},
		},
		"players": players,
		"mode": "skirmish",
	}
	return document if validate(document) else {}


static func choose_map(rows: Array, retail_map_id: String, retail_map_name: String = "") -> Dictionary:
	var wanted := _map_slug(retail_map_id)
	var name_slug := _map_slug(retail_map_name)
	for value in rows:
		if not (value is Dictionary):
			continue
		var row := value as Dictionary
		var slug := String(row.get("slug", ""))
		if (
			(not wanted.is_empty() and (slug == wanted or slug.ends_with(wanted) or wanted.ends_with(slug)))
			or (not name_slug.is_empty() and (slug == name_slug or slug.ends_with(name_slug)))
		):
			return row.duplicate(true)
	for value in rows:
		if value is Dictionary and String((value as Dictionary).get("kind", "")) == "multiplayer":
			return (value as Dictionary).duplicate(true)
	return (rows[0] as Dictionary).duplicate(true) if not rows.is_empty() and rows[0] is Dictionary else {}


static func validate(document: Dictionary) -> bool:
	if String(document.get("schema", "")) != SCHEMA or int(document.get("seed", -1)) < 0:
		return false
	var pack := document.get("pack", {}) as Dictionary
	if String(pack.get("id", "")).is_empty() or not _is_sha256(String(pack.get("sha256", ""))):
		return false
	var map := document.get("map", {}) as Dictionary
	if String(map.get("path", "")).is_empty():
		return false
	var rules := document.get("rules", {}) as Dictionary
	if (
		int(rules.get("tick_ms", 0)) < 1
		or int(rules.get("starting_resources", -1)) < 0
		or float(rules.get("command_point_multiplier", -1.0)) < 0.0
		or String(rules.get("victory", "")) not in ["annihilation", "fortress", "timed"]
	):
		return false
	var players := document.get("players", []) as Array
	if players.is_empty() or players.size() > 8:
		return false
	var seats: Dictionary = {}
	for value in players:
		if not (value is Dictionary):
			return false
		var player := value as Dictionary
		var seat := int(player.get("seat", -1))
		if seat < 0 or seat > 7 or seats.has(seat):
			return false
		seats[seat] = true
		if (
			int(player.get("team", -1)) < 0
			or String(player.get("faction", "")).is_empty()
			or String(player.get("controller", "")) not in ["human", "ai", "observer", "none"]
		):
			return false
	return true


static func _players_from_setup(setup: Node, start_count: int) -> Array:
	var armies := setup.get("row_army_opts") as Array
	var controllers := setup.get("row_controller_opts") as Array
	var difficulties := setup.get("row_difficulty_opts") as Array
	var teams := setup.get("team_dropdowns") as Array
	var colors := setup.get("color_dropdowns") as Array
	var result: Array = []
	for row in mini(armies.size(), 8):
		var army := armies[row] as OptionButton
		var controller_option := controllers[row] as OptionButton
		if army == null or army.selected < 0 or controller_option == null or controller_option.selected < 0:
			continue
		var faction := _faction_contract(String(army.get_item_metadata(army.selected)))
		if faction.is_empty():
			continue
		var is_human := controller_option.get_item_text(controller_option.selected) == "Human"
		var player := {
			"seat": row,
			"team": _team_id(teams[row] as OptionButton, row),
			"faction": faction,
			"controller": "human" if is_human else "ai",
			"color": _selected_index(colors[row] as OptionButton, row),
			"handicap": 1.0,
			"name": "Player %d" % (row + 1),
		}
		if not is_human:
			var difficulty := difficulties[row] as OptionButton
			var tier := String(difficulty.get_item_metadata(difficulty.selected)) if difficulty != null and difficulty.selected >= 0 else "medium"
			player["ai_difficulty"] = tier if tier in ["easy", "medium", "hard", "brutal"] else "medium"
		result.append(player)
	_assign_start_positions(setup, result, start_count)
	return result


static func _assign_start_positions(setup: Node, players: Array, start_count: int) -> void:
	var available: Array[int] = []
	for index in maxi(players.size(), start_count):
		available.append(index)
	var human_index := 0
	for index in players.size():
		if String((players[index] as Dictionary).get("controller", "")) == "human":
			human_index = index
			break
	var authored_start := 0
	var game_state := setup.get_node_or_null("/root/GameState")
	if game_state != null:
		# The retail shell exposes Player_N_Start as one-based labels; map-v1 and
		# match-launch-v1 address the same starts with zero-based indices.
		var shell_start := int(game_state.get("retail_player_start_index"))
		if shell_start > 0 and shell_start <= available.size():
			authored_start = shell_start - 1
	(players[human_index] as Dictionary)["start_position"] = authored_start
	available.erase(authored_start)
	for index in players.size():
		if index == human_index:
			continue
		(players[index] as Dictionary)["start_position"] = available.pop_front()


static func _team_id(option: OptionButton, row: int) -> int:
	if option != null and option.selected >= 0:
		var selected := int(option.get_item_metadata(option.selected))
		if selected >= 0:
			return selected
	return int(TEAM_IDS[row])


static func _selected_index(option: OptionButton, fallback: int) -> int:
	return option.selected if option != null and option.selected >= 0 else fallback


static func _map_contract_path(row: Dictionary, content_root: String) -> String:
	var artifact_path := String(row.get("path", ""))
	if not FileAccess.file_exists(artifact_path):
		artifact_path = _selected_path(content_root, artifact_path)
	var document := _read_json(artifact_path, MAX_SELECTION_BYTES)
	var source := document.get("source", {}) as Dictionary
	return String(source.get("path", ""))


static func _faction_contract(value: String) -> String:
	var slug := value.strip_edges().to_lower().trim_prefix("faction")
	var names := {
		"men": "Men", "elves": "Elves", "dwarves": "Dwarves",
		"isengard": "Isengard", "mordor": "Mordor", "wild": "Wild", "angmar": "Angmar",
	}
	return "Faction%s" % String(names.get(slug, "")) if names.has(slug) else ""


static func _selected_integer(value: Variant, fallback: int) -> int:
	var option := value as OptionButton
	return int(option.get_item_metadata(option.selected)) if option != null and option.selected >= 0 else fallback


static func _selected_float(value: Variant, fallback: float) -> float:
	var option := value as OptionButton
	return float(option.get_item_metadata(option.selected)) if option != null and option.selected >= 0 else fallback


static func _stable_seed(map_id: String, players: Array) -> int:
	var material := "%s|%s" % [map_id, JSON.stringify(players)]
	var value := 0x811C9DC5
	for byte in material.to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0x7FFFFFFF
	return value


static func _map_slug(value: String) -> String:
	var stem := value.replace("\\", "/").get_file().get_basename().to_lower()
	stem = stem.trim_prefix("bfme2.map.").trim_prefix("rotwk.map.")
	for prefix in ["map mp ", "map ", "mp "]:
		if stem.begins_with(prefix):
			stem = stem.trim_prefix(prefix)
	var result := ""
	var separator := false
	for character in stem:
		var code := character.unicode_at(0)
		if (code >= 97 and code <= 122) or (code >= 48 and code <= 57):
			result += character
			separator = false
		elif not result.is_empty() and not separator:
			result += "-"
			separator = true
	return result.trim_suffix("-")


static func _content_root() -> String:
	var configured := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	return ProjectSettings.globalize_path(configured).simplify_path() if not configured.is_empty() else OS.get_executable_path().get_base_dir().path_join("content-packs").simplify_path()


static func _selected_path(content_root: String, relative: String) -> String:
	var normalized := relative.replace("\\", "/").strip_edges()
	if normalized.is_empty() or normalized.is_absolute_path():
		return ""
	for part in normalized.split("/", false):
		if part in [".", ".."]:
			return ""
	return content_root.path_join(normalized).simplify_path()


static func _read_json(path: String, maximum_bytes: int) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	var value: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return value as Dictionary if value is Dictionary else {}


static func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not "0123456789abcdef".contains(character):
			return false
	return true
