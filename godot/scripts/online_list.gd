## OpenBFME. GPL-3.0.
## Display-only game lists. Gameplay replies (slots, START, tickets) retain their strict parsing.
extends RefCounted

const MAX_GAMES := 200
const MAX_MINE := 32
const MAX_SLOTS := 8
const TEXT_LIMITS := {"name": 48, "host": 24, "map": 200, "state": 16}


## Decode only valid UTF-8 spans; Godot otherwise logs decoder errors for untrusted bytes.
static func decode(body: PackedByteArray) -> String:
	var parts := PackedStringArray()
	var start := 0
	var at := 0
	while at < body.size():
		var b := int(body[at])
		var count := 1 if b < 0x80 else (2 if b >= 0xc2 and b <= 0xdf else (3 if b >= 0xe0 and b <= 0xef else (4 if b >= 0xf0 and b <= 0xf4 else 0)))
		var valid := count > 0 and at + count <= body.size()
		for offset in range(1, count):
			if not valid:
				break
			var next := int(body[at + offset])
			valid = next >= 0x80 and next <= 0xbf
			if offset == 1:
				valid = valid and not (b == 0xe0 and next < 0xa0 or b == 0xed and next >= 0xa0 or b == 0xf0 and next < 0x90 or b == 0xf4 and next >= 0x90)
		if valid and b != 0:
			at += count
		else:
			parts.append(body.slice(start, at).get_string_from_utf8())
			parts.append("�")
			at += 1
			start = at
	parts.append(body.slice(start).get_string_from_utf8())
	return sanitize("".join(parts))


static func _escape(text: String, at: int) -> int:
	if at + 6 > text.length() or text.substr(at, 2) != "\\u":
		return -1
	var digits := text.substr(at + 2, 4)
	for digit in digits.to_lower():
		if not digit in "0123456789abcdef":
			return -1
	return digits.hex_to_int()


## Walk escapes, not substrings: "\\\\ud800" is literal text, and a valid surrogate pair stays intact.
static func sanitize(text: String) -> String:
	var parts := PackedStringArray()
	var at := 0
	var start := 0
	while at < text.length():
		if text[at] != "\\":
			at += 1
			continue
		var code := _escape(text, at)
		if code >= 0xd800 and code <= 0xdbff:
			var low := _escape(text, at + 6)
			if low >= 0xdc00 and low <= 0xdfff:
				at += 12
				continue
		elif code < 0xdc00 or code > 0xdfff:
			at += 2
			continue
		parts.append(text.substr(start, at - start))
		parts.append("�")
		at += 6
		start = at
	parts.append(text.substr(start))
	return "".join(parts)


static func _whole(value: Variant, most: int) -> bool:
	return (value is float or value is int) and is_finite(float(value)) and value == floorf(value) and value >= 0 and value <= most


static func _id(value: Variant) -> bool:
	if not value is String or value.length() != 16:
		return false
	for digit in value:
		if not digit in "0123456789abcdef":
			return false
	return true


static func _game(value: Variant, mine: bool) -> bool:
	if not value is Dictionary or not _id(value.get("id")):
		return false
	for field in TEXT_LIMITS:
		var text: Variant = value.get(field)
		if not text is String or text.to_utf8_buffer().size() > TEXT_LIMITS[field]:
			return false
	if not _whole(value.get("players"), MAX_SLOTS) or not _whole(value.get("openSlots"), MAX_SLOTS) or not value.get("password") is bool:
		return false
	if mine and not _whole(value.get("slot"), MAX_SLOTS - 1):
		return false
	if value.has("mode") and not value.mode in ["normal", "uber"]:
		return false
	if value.get("hostPlatform") != null and not value.hostPlatform is String:
		return false
	return true


## {ok, data, text} or {ok: false, error}. One bounded, content-free log line per skipped row.
static func parse(body: PackedByteArray, mine := false) -> Dictionary:
	# Godot truncates decoded strings at NUL; do not accept a prefix of the response.
	if body.has(0):
		return {"ok": false, "error": "the lobby answer contains a raw NUL byte"}
	var text := decode(body)
	var json := JSON.new()
	if json.parse(text) != OK or not json.data is Dictionary or typeof(json.data.get("v")) != TYPE_FLOAT or json.data.v != 1.0:
		return {"ok": false, "error": "the lobby answer is not lobby version 1"}
	var data: Dictionary = json.data
	if not data.get("games") is Array or data.games.size() > (MAX_MINE if mine else MAX_GAMES):
		return {"ok": false, "error": "the lobby's games must be a list of at most %d entries" % (MAX_MINE if mine else MAX_GAMES)}
	if mine and data.has("left") and (not data.left is Array or data.left.size() > MAX_MINE):
		return {"ok": false, "error": "the lobby's left must be a list of at most %d game ids" % MAX_MINE}
	var skipped := 0
	var games: Array = []
	for index in data.games.size():
		if _game(data.games[index], mine):
			games.append(data.games[index])
		else:
			print("GAME ONLINE skipped malformed game list entry at index %d" % index)
			skipped += 1
	data.games = games
	if mine and data.has("left"):
		var left: Array = []
		for index in data.left.size():
			if _id(data.left[index]):
				left.append(data.left[index])
			else:
				print("GAME ONLINE skipped malformed left list entry at index %d" % index)
				skipped += 1
		data.left = left
	return {"ok": true, "data": data, "text": text if skipped == 0 else JSON.stringify(data)}
