## The public game list on the online lobby: GET /v1/games (counts and names the lobby already publishes; no sign-in).
## One request, the same HttpFetch.get_small pattern as the player count. An answer that is not version 1, or any row that is
## not a public game, is refused whole: the launcher never shows a list it could not read.
extends RefCounted

const HttpFetch := preload("res://scripts/net/http_fetch.gd")

const BODY_CAP := 262144  # 200 games, each with a 200-byte map, still fit (JSON and the extra public fields included)
const MAX_GAMES := 200
const MIN_GAP_SECONDS := 5.0
const NAME_MAX_BYTES := 48  # lobby str(name, 48): UTF-8 bytes, the same bound as services/online/lobby
const HOST_MAX_BYTES := 24  # a slot name (the public host)
const MAP_MAX_BYTES := 200  # setup.map


## {ok, games, why}. `games` is [] unless the whole answer is version 1 and every row is a public game (an empty list is success).
static func parse(code: int, body: PackedByteArray) -> Dictionary:
	if code != 200:
		return {"ok": false, "games": [], "why": "the lobby answered %d" % code}
	var parser := JSON.new()
	var j: Variant = parser.data if parser.parse(body.get_string_from_utf8()) == OK else null
	if typeof(j) != TYPE_DICTIONARY or j.get("v") != 1.0:
		return {"ok": false, "games": [], "why": "not a version 1 answer"}
	var rows: Variant = j.get("games")
	if typeof(rows) != TYPE_ARRAY:
		return {"ok": false, "games": [], "why": "games is missing"}
	if rows.size() > MAX_GAMES:
		return {"ok": false, "games": [], "why": "more than %d games" % MAX_GAMES}
	var games: Array = []
	for row in rows:
		var g := _game(row)
		if g.is_empty():
			return {"ok": false, "games": [], "why": "a game row is malformed"}
		games.append(g)
	return {"ok": true, "games": games, "why": ""}


## a public game, or {} when the row is not one
static func _game(row: Variant) -> Dictionary:
	if typeof(row) != TYPE_DICTIONARY:
		return {}
	var id_v: Variant = row.get("id")
	if typeof(id_v) != TYPE_STRING:
		return {}
	var id := str(id_v)
	if id.length() != 16:
		return {}
	for i in id.length():
		var c := id.unicode_at(i)
		if not ((c >= 0x30 and c <= 0x39) or (c >= 0x61 and c <= 0x66)):
			return {}
	if not _text_ok(row.get("name"), NAME_MAX_BYTES) or not _text_ok(row.get("host"), HOST_MAX_BYTES) or not _text_ok(row.get("map"), MAP_MAX_BYTES):
		return {}
	if not _count_ok(row.get("players")) or not _count_ok(row.get("openSlots")):
		return {}
	var state: Variant = row.get("state")
	if state != "open" and state != "started":
		return {}
	var password: Variant = row.get("password")
	if typeof(password) != TYPE_BOOL:
		return {}
	return {
		"id": id,
		"name": row.name,
		"host": row.host,
		"map": row.map,
		"players": int(row.players),
		"openSlots": int(row.openSlots),
		"state": state,
		"password": password,
	}


static func _text_ok(v: Variant, max_bytes: int) -> bool:
	if typeof(v) != TYPE_STRING:
		return false
	var text := str(v)
	if text.is_empty() or text.to_utf8_buffer().size() > max_bytes:
		return false
	for i in text.length():
		var c := text.unicode_at(i)
		if c < 0x20 or c == 0x7f:
			return false
	return true


static func _count_ok(v: Variant) -> bool:
	if typeof(v) == TYPE_INT:
		return v >= 0 and v <= 16
	if typeof(v) != TYPE_FLOAT or v != floorf(v):
		return false
	return v >= 0.0 and v <= 16.0


## the launcher may join this row (no password, still open, a free slot). Locked, started and full games stay on the list.
static func joinable(game: Dictionary) -> bool:
	return game.get("state") == "open" and game.get("password") == false and int(game.get("openSlots", 0)) >= 1


## the name column: one suffix when the row cannot be joined from the launcher
static func display_name(game: Dictionary) -> String:
	var name := str(game.get("name", ""))
	if game.get("state") == "started":
		return name + " (started)"
	if game.get("password") == true:
		return name + " (locked)"
	if int(game.get("openSlots", 0)) < 1:
		return name + " (full)"
	return name


## asks the lobby (blocking: run it on a thread). `http`: an HttpFetch of its own. {ok, games, why}
static func fetch(http: HttpFetch, lobby_url: String) -> Dictionary:
	var r := http.get_small(lobby_url.trim_suffix("/") + "/v1/games", BODY_CAP, "application/json")
	if not r.ok:
		return {"ok": false, "games": [], "why": r.error}
	return parse(r.code, r.body)
