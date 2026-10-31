## OpenBFME. GPL-3.0.
## Keep decoding in sync with godot/scripts/online_list.gd (separate Godot projects).
extends RefCounted

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
