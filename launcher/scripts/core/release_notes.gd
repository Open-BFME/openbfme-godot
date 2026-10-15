## Lane UI-5: a release's notes as the launcher's cards show them. The notes are the release body tools/release/autorelease.py writes
## (release_notes): sections of "- " lines, then a "Built from commit ..." line; Updater.plain_text has already undone its Markdown
## escapes. Lane NOTES-1: the studio-style patch notes have the sections Added, Changed, Bug fixes, Removed and Known issues (releases up
## to v0.4.0-alpha.3 have "What's new" and "What's fixed"; the first studio-style release keeps those two titles; every title is read),
## and inside a section a bold line `**Area**` groups the bullets under it. Only that structure is read: a heading is a line starting
## with "#", an item a line starting with "- " or "* ", an area a line that is only `**text**`; tables, links and loose text are
## dropped. Nothing is rendered as markup.
extends RefCounted

const Semver := preload("res://scripts/core/semver.gd")


## the titles of the Play card's lines, in the order tried: the added lines (both generations of titles), then the fixed ones, then the changed ones
const PLAY_TITLES := [["What's new", "Added"], ["What's fixed", "Bug fixes"], ["Changed"]]
const AREA_MAX := 40


## {intro: String, sections: [{title, items: [String], groups: [{area, items}]}]} (sections in their order; empty ones left out). `items`
## is every bullet of the section in order; `groups` the same bullets by their bold area line ("" before the first one).
static func parse(text: String) -> Dictionary:
	var intro := PackedStringArray()
	var sections := []
	var cur: Dictionary = {}
	var area := ""
	for raw in text.split("\n"):
		var line := raw.strip_edges()
		if line == "":
			continue
		if line.begins_with("#"):
			cur = {"title": line.lstrip("#").strip_edges(), "items": [], "groups": []}
			sections.append(cur)
			area = ""
		elif line.begins_with("- ") or line.begins_with("* "):
			if cur.is_empty():
				cur = {"title": "", "items": [], "groups": []}
				sections.append(cur)
			var item := line.substr(2).strip_edges()
			cur.items.append(item)
			if cur.groups.is_empty() or cur.groups[cur.groups.size() - 1].area != area:
				cur.groups.append({"area": area, "items": []})
			cur.groups[cur.groups.size() - 1].items.append(item)
		elif not cur.is_empty() and line.length() > 4 and line.begins_with("**") and line.ends_with("**") and not line.contains("|") \
				and line.length() - 4 <= AREA_MAX and not line.substr(2, line.length() - 4).contains("**"):
			area = line.substr(2, line.length() - 4).strip_edges()
		elif cur.is_empty():
			intro.append(line)
		elif not cur.items.is_empty() and raw.begins_with("  "):
			cur.items[cur.items.size() - 1] += " " + line  # a wrapped item
			cur.groups[cur.groups.size() - 1].items[cur.groups[cur.groups.size() - 1].items.size() - 1] += " " + line
	return {"intro": " ".join(intro), "sections": sections.filter(func(s): return not s.items.is_empty())}


## the items of the first section titled like `title` (case-insensitive, "What's new"), at most `limit`
static func items(text: String, title: String, limit := 1000) -> Array:
	for s in parse(text).sections:
		if String(s.title).to_lower() == title.to_lower():
			return s.items.slice(0, limit)
	return []


## the Play card's lines: the first of PLAY_TITLES' groups (added, fixed, changed; old and new titles) that has any, at most `limit`
static func play_items(text: String, limit := 1000) -> Array:
	for titles in PLAY_TITLES:
		for t in titles:
			var found := items(text, t, limit)
			if not found.is_empty():
				return found
	return []


## "2026-10-10T18:03:11Z" -> "10 October 2026" ("" when it is not such a date)
static func date_text(iso: String) -> String:
	var d := Time.get_datetime_dict_from_datetime_string(iso, false)
	if iso.length() < 10 or int(d.get("year", 0)) < 2000 or int(d.get("month", 0)) < 1 or int(d.get("month", 0)) > 12:
		return ""
	var months := ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"]
	return "%d %s %d" % [int(d.day), months[int(d.month) - 1], int(d.year)]


## "v0.3.0-preview.2" -> "Preview 2 of 0.3.0"; "v0.4.5" -> "Alpha 0.4.5" and "v0.4.0-alpha.3" -> "v0.4.0 Alpha 3" (lane VERSION-1,
## Semver.display)
static func title_of(tag: String) -> String:
	var m := RegEx.create_from_string("^v(\\d+\\.\\d+\\.\\d+)-preview\\.(\\d+)$").search(tag)
	if m == null:
		return Semver.display(tag)
	return "Preview %s of %s" % [m.get_string(2), m.get_string(1)]
