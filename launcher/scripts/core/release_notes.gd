## Lane UI-5: a release's notes as the launcher's cards show them. The notes are the release body tools/release/autorelease.py writes
## (release_body): an intro paragraph, then "## What's new", "## What's fixed", "## Known issues" sections of "- " lines, then a "Built
## from commit ..." line; Updater.plain_text has already undone its Markdown escapes. Only that structure is read: a heading is a line
## starting with "#", an item a line starting with "- " or "* ", everything else is intro text. Nothing is rendered as markup.
extends RefCounted


## {intro: String, sections: [{title, items: [String]}]} (sections in their order; empty ones left out)
static func parse(text: String) -> Dictionary:
	var intro := PackedStringArray()
	var sections := []
	var cur: Dictionary = {}
	for raw in text.split("\n"):
		var line := raw.strip_edges()
		if line == "":
			continue
		if line.begins_with("#"):
			cur = {"title": line.lstrip("#").strip_edges(), "items": []}
			sections.append(cur)
		elif line.begins_with("- ") or line.begins_with("* "):
			if cur.is_empty():
				cur = {"title": "", "items": []}
				sections.append(cur)
			cur.items.append(line.substr(2).strip_edges())
		elif cur.is_empty():
			intro.append(line)
		elif not cur.items.is_empty() and raw.begins_with("  "):
			cur.items[cur.items.size() - 1] += " " + line  # a wrapped item
	return {"intro": " ".join(intro), "sections": sections.filter(func(s): return not s.items.is_empty())}


## the items of the first section titled like `title` (case-insensitive, "What's new"), at most `limit`
static func items(text: String, title: String, limit := 1000) -> Array:
	for s in parse(text).sections:
		if String(s.title).to_lower() == title.to_lower():
			return s.items.slice(0, limit)
	return []


## "2026-10-10T18:03:11Z" -> "10 October 2026" ("" when it is not such a date)
static func date_text(iso: String) -> String:
	var d := Time.get_datetime_dict_from_datetime_string(iso, false)
	if iso.length() < 10 or int(d.get("year", 0)) < 2000 or int(d.get("month", 0)) < 1 or int(d.get("month", 0)) > 12:
		return ""
	var months := ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
	return "%d %s %d" % [int(d.day), months[int(d.month) - 1], int(d.year)]


## "v0.3.0-preview.2" -> "Preview 2 of 0.3.0"; "v1.0.0" -> "Version 1.0.0"
static func title_of(tag: String) -> String:
	var m := RegEx.create_from_string("^v(\\d+\\.\\d+\\.\\d+)(?:-preview\\.(\\d+))?$").search(tag)
	if m == null:
		return tag
	return "Preview %s of %s" % [m.get_string(2), m.get_string(1)] if m.get_string(2) != "" else "Version " + m.get_string(1)
