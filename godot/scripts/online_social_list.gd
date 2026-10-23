## OpenBFME. GPL-3.0.
## Display collections only; authentication, mutations and authoritative room/START replies stay strict.
extends RefCounted

const Lists := preload("res://scripts/online_list.gd")
const SAFE_INTEGER := 9007199254740991


static func handles(path: String) -> bool:
	var endpoint := path.get_slice("?", 0)
	return endpoint in ["/v1/friends", "/v1/recent", "/v1/blocks", "/v1/inbox", "/v1/chat"] \
		or endpoint.begins_with("/v1/chat/") and endpoint.ends_with("/messages") \
		and Lists._id(endpoint.trim_prefix("/v1/chat/").trim_suffix("/messages"))


static func parse(body: PackedByteArray, path: String, self_pid := "") -> Dictionary:
	if body.has(0):
		return _error()
	var text := Lists.decode(body)
	var json := JSON.new()
	if json.parse(text) != OK or not json.data is Dictionary or not _number(json.data.get("v")) or json.data.v != 1:
		return _error()
	var data: Dictionary = json.data
	if _private_identity(data):
		return _error()
	var endpoint := path.get_slice("?", 0)
	var valid := false
	match endpoint:
		"/v1/friends":
			valid = _filter(data, "friends", 200, _friend, "pid")
		"/v1/recent":
			valid = _filter(data, "games", 20, _recent)
		"/v1/blocks":
			valid = _cursor(data) and _filter(data, "blocks", 50, _block)
		"/v1/chat":
			valid = _cursor(data) and _filter(data, "threads", 50, _thread, "pid")
		"/v1/inbox":
			valid = _inbox(data)
		_:
			if handles(path):
				valid = _messages(data, path, self_pid)
	if not valid:
		return _error()
	return {"ok": true, "data": data, "text": JSON.stringify(data)}


static func _error() -> Dictionary:
	return {"ok": false, "error": "the lobby returned an invalid social list response"}


static func _private_identity(value: Variant, depth := 0) -> bool:
	if depth > 8: return true
	if value is Dictionary:
		for key in value:
			if key in ["install", "device", "install_id", "device_hash"] or _private_identity(value[key], depth + 1): return true
	elif value is Array:
		for row in value:
			if _private_identity(row, depth + 1): return true
	return false


static func _filter(data: Dictionary, key: String, limit: int, validator: Callable, unique := "") -> bool:
	if not data.get(key) is Array or data[key].size() > limit:
		return false
	var rows: Array = []
	var seen := {}
	for index in data[key].size():
		var row: Variant = data[key][index]
		if validator.call(row) and (unique.is_empty() or not seen.has(row[unique])):
			rows.append(row)
			if not unique.is_empty(): seen[row[unique]] = true
		else:
			print("GAME ONLINE skipped malformed %s list entry at index %d" % [key, index])
	data[key] = rows
	return true


static func _number(value: Variant, most := SAFE_INTEGER) -> bool:
	return Lists._whole(value, most)


static func _text(value: Variant, scalars: int, bytes: int, empty := false, trim := true) -> bool:
	if not value is String or value.length() > scalars or value.to_utf8_buffer().size() > bytes or not empty and value.is_empty():
		return false
	if trim and not value.is_empty() and (_whitespace(value.unicode_at(0)) or _whitespace(value.unicode_at(value.length() - 1))):
		return false
	for c in value:
		if c.unicode_at(0) < 32 or c.unicode_at(0) == 127:
			return false
	return true


static func _whitespace(cp: int) -> bool:
	return cp >= 9 and cp <= 13 or cp in [32, 0x85, 0xa0, 0x1680, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000] or cp >= 0x2000 and cp <= 0x200a


static func _player(row: Variant) -> bool:
	return row is Dictionary and Lists._id(row.get("pid")) and _text(row.get("nick"), 24, 128, false, false) \
		and row.get("code") is String and RegEx.create_from_string("^(?:[0-9A-HJKMNP-TV-Z]{10}|[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{2})\\z").search(row.code) != null


static func _presence(row: Variant) -> bool:
	return row is Dictionary and row.has_all(["pid", "state", "game", "map", "mode", "since", "joinable"]) \
		and Lists._id(row.pid) and row.state in ["offline", "online", "in_room", "matching", "loading", "playing", "away"] \
		and (row.game == null or Lists._id(row.game)) and row.joinable is bool \
		and (row.map == null or row.map is String and row.map.to_utf8_buffer().size() <= 200) \
		and (row.mode == null or row.mode in ["normal", "uber"]) and (_number(row.since) or row.state == "offline" and row.since == null)


static func _friend(row: Variant) -> bool:
	return _player(row) and _number(row.get("since")) and _presence(row.get("presence")) and row.presence.pid == row.pid


static func _recent(row: Variant) -> bool:
	return row is Dictionary and Lists._id(row.get("game")) and _number(row.get("at")) \
		and _filter(row, "players", 7, _recent_player, "pid")


static func _recent_player(row: Variant) -> bool:
	return _player(row) and row.get("target") is String and not row.target.is_empty() and row.target.to_utf8_buffer().size() <= 128


static func _request(row: Variant) -> bool:
	return row is Dictionary and Lists._id(row.get("id")) and _player(row.get("from")) and _player(row.get("to")) and _number(row.get("at"))


static func _invite(row: Variant) -> bool:
	return row is Dictionary and row.has_all(["mode", "note"]) and Lists._id(row.get("id")) and Lists._id(row.get("game")) \
		and _player(row.get("from")) and _player(row.get("to")) and row.get("kind") in ["invite", "challenge"] \
		and (row.mode == null or row.mode in ["normal", "uber"]) and _text(row.note, 200, 800, true) and _number(row.get("expires"))


static func _notice(row: Variant) -> bool:
	return row is Dictionary and row.has("invite") and Lists._id(row.get("id")) and row.get("kind") in ["declined", "busy", "expired", "recovered"] \
		and (row.invite == null or Lists._id(row.invite)) and _number(row.get("at"))


static func _head(row: Variant) -> bool:
	return row is Dictionary and Lists._id(row.get("pid")) and _number(row.get("last_id")) and _number(row.get("read_id")) and row.read_id <= row.last_id


static func _thread(row: Variant) -> bool:
	return _player(row) and _head(row)


static func _block(row: Variant) -> bool:
	return _player(row) and _text(row.get("note"), 200, 800, true) and _number(row.get("at"))


static func _cursor(data: Dictionary) -> bool:
	return data.has("next") and (data.next == null or Lists._id(data.next))


static func _inbox(data: Dictionary) -> bool:
	var seq: Variant = data.get("seq")
	if not seq is String or RegEx.create_from_string("^[0-9a-f]{16}:[0-9]{1,16}\\z").search(seq) == null \
			or int(seq.get_slice(":", 1)) > SAFE_INTEGER or not data.get("reset") is bool:
		return false
	var stats: Variant = data.get("stats")
	if not stats is Dictionary or not _number(stats.get("as_of")):
		return false
	for key in ["players", "games_open", "games_running"]:
		if not _number(stats.get(key), 1000000): return false
	if data.has("backoff") and not _number(data.backoff, 60): return false
	if data.has("wait") and not _number(data.wait, 25): return false
	return _filter(data, "presence", 200, _presence, "pid") and _filter(data, "requests", 250, _request, "id") \
		and _filter(data, "invites", 200, _invite, "id") and _filter(data, "notices", 50, _notice, "id") and _filter(data, "chat", 200, _head, "pid")


static func _message(row: Variant) -> bool:
	return row is Dictionary and _number(row.get("id")) and row.id > 0 and Lists._id(row.get("from")) and Lists._id(row.get("to")) \
		and _text(row.get("text"), 1000, 4000) and _number(row.get("at")) and row.get("client_id") is String \
		and RegEx.create_from_string("^[0-9a-f]{32}\\z").search(row.client_id) != null


static func _messages(data: Dictionary, path: String, self_pid: String) -> bool:
	var peer := path.get_slice("?", 0).trim_prefix("/v1/chat/").trim_suffix("/messages")
	var after := 0
	for query in path.get_slice("?", 1).split("&"):
		if query.begins_with("after="): after = int(query.trim_prefix("after="))
	if not Lists._id(self_pid) or self_pid == peer or not data.get("messages") is Array or data.messages.size() > 50 \
			or not data.has("next") or not _number(data.get("last_id")) or not _number(data.get("read_id")) or data.read_id > data.last_id:
		return false
	if data.next != null:
		if not _number(data.next) or data.next <= after or data.next > data.last_id or data.messages.is_empty(): return false
		if not data.messages.is_empty() and data.messages.back() is Dictionary and _number(data.messages.back().get("id")) and data.next != data.messages.back().id:
			return false
	var progress := {"id": after}
	var valid := func(row: Variant) -> bool:
		if not _message(row) or row.id <= progress.id or row.id > data.last_id: return false
		if not (row.from == self_pid and row.to == peer or row.from == peer and row.to == self_pid): return false
		progress.id = row.id
		return true
	if not _filter(data, "messages", 50, valid): return false
	# Native pagination refers to the last visible row; never synthesize messages for discarded entries.
	if data.next != null: data.next = null if data.messages.is_empty() else data.messages.back().id
	return true
