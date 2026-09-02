class_name NativeMapBootstrap
extends RefCounted
## Presentation-side setup requests. It never mutates deterministic state directly.

const FIGHTER_PREFERRED := {"Men": "GondorFighterHorde", "Mordor": "MordorFighterHorde"}
const ARCHER_PREFERRED := {"Men": "GondorArcherHorde", "Mordor": "MordorArcherHorde"}
const FORTRESS_PREFERRED := {"Men": "MenFortress", "Mordor": "MordorFortress"}


static func spawn_match(client, match: Dictionary, document, catalog: Array[Dictionary]) -> Dictionary:
	var result := {
		"fortresses": [],
		"hordes": [],
		"horde_owners": {},
		"starts": {},
	}
	var players := match.get("players", []) as Array
	for player_index in players.size():
		var player := players[player_index] as Dictionary
		var start_index := int(player.get("start_position", player_index))
		var faction := String(player.get("faction", ""))
		var side := faction.trim_prefix("Faction")
		var start: Vector2 = document.start_horizontal(start_index)
		(result.starts as Dictionary)[player_index] = start
		var fortress := choose_fortress_template(catalog, side)
		if fortress.is_empty():
			return {"error": "no loaded fortress template for side %s" % side}
		var fortress_reply: Dictionary = client.spawn_at_start(fortress, int(player.get("seat", player_index)), start_index)
		if fortress_reply.is_empty():
			return {"error": client.last_error()}
		(result.fortresses as Array).append(fortress)
		var opponent_index := 1 - player_index if players.size() == 2 else (player_index + 1) % players.size()
		var opponent_player := players[opponent_index] as Dictionary
		var opponent_start: Vector2 = document.start_horizontal(
			int(opponent_player.get("start_position", opponent_index))
		)
		var inward := start.direction_to(opponent_start)
		if inward.length_squared() < 0.01:
			inward = Vector2.RIGHT
		var side_axis := Vector2(-inward.y, inward.x)
		var fighter := choose_horde_template(
			catalog, String(FIGHTER_PREFERRED.get(side, "%sFighterHorde" % side)), side, "fighter"
		)
		var archer := choose_horde_template(
			catalog, String(ARCHER_PREFERRED.get(side, "%sArcherHorde" % side)), side, "archer"
		)
		if fighter.is_empty() or archer.is_empty():
			return {"error": "no loaded initial hordes for side %s" % side}
		var requests := [
			[fighter, start + inward * 310.0 - side_axis * 65.0],
			[fighter, start + inward * 325.0],
			[fighter, start + inward * 310.0 + side_axis * 65.0],
			[archer, start + inward * 245.0],
		]
		for request in requests:
			var reply: Dictionary = client.spawn(String(request[0]), int(player.get("seat", player_index)), request[1] as Vector2)
			if reply.is_empty():
				return {"error": client.last_error()}
			var id := int(reply.get("id", 0))
			(result.hordes as Array).append(id)
			(result.horde_owners as Dictionary)[id] = player_index
	print("SIM_HOST_MAP_FORTRESSES player0=%s player1=%s" % [String((result.fortresses as Array)[0]), String((result.fortresses as Array)[1])])
	return result


static func choose_fortress_template(template_rows: Array[Dictionary], side: String) -> String:
	var preferred := String(FORTRESS_PREFERRED.get(side, "%sFortress" % side))
	var candidates: Array[String] = []
	for row in template_rows:
		if bool(row.get("horde", false)) or String(row.get("side", "")).nocasecmp_to(side) != 0:
			continue
		var name := String(row.get("name", ""))
		if name.nocasecmp_to(preferred) == 0:
			return name
		var kindof := row.get("kindof", []) as Array
		var castle_like := false
		for token in kindof:
			if String(token) in ["CASTLE_KEEP", "COMMANDCENTER", "VITAL_FOR_BASE_SURVIVAL"]:
				castle_like = true
		if castle_like and (
			name.to_lower().contains("fortress")
			or name.to_lower().contains("castle")
			or name.to_lower().contains("keep")
		):
			candidates.append(name)
	candidates.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return "" if candidates.is_empty() else candidates[0]


static func choose_horde_template(
	template_rows: Array[Dictionary], preferred: String, side: String, role: String
) -> String:
	var candidates: Array[String] = []
	for row in template_rows:
		if not bool(row.get("horde", false)) or String(row.get("side", "")).nocasecmp_to(side) != 0:
			continue
		var name := String(row.get("name", ""))
		if name.nocasecmp_to(preferred) == 0:
			return name
		if name.to_lower().contains(role.to_lower()):
			candidates.append(name)
	if candidates.is_empty():
		for row in template_rows:
			if bool(row.get("horde", false)) and String(row.get("side", "")).nocasecmp_to(side) == 0:
				candidates.append(String(row.get("name", "")))
	candidates.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return "" if candidates.is_empty() else candidates[0]
