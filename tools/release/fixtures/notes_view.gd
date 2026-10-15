## NOTES-1: prints what a launcher's Play card and News screen would show for a release body, once with the parser of the launcher
## that shipped in v0.4.0-alpha.3 (release_notes_alpha3.gd, a frozen copy) and once with this tree's launcher/scripts/core/release_notes.gd.
## godot --headless --path <any project dir> --script <this file> -- <notes file>
extends SceneTree

func _init() -> void:
	var path := OS.get_cmdline_user_args()[0]
	var text := FileAccess.get_file_as_string(path)
	var old = load("res://release_notes_alpha3.gd")
	var cur = load("res://release_notes.gd")
	# the Play card of the alpha.3 launcher (play_screen.gd set_notes): "What's new", else "What's fixed"
	var items: Array = old.items(text, "What's new", 100)
	if items.is_empty():
		items = old.items(text, "What's fixed", 100)
	print("OLD PLAY CARD: ", items.size(), " lines")
	for it in items:
		print("  OLD PLAY: ", it)
	for s in old.parse(text).sections:
		print("OLD NEWS SECTION: ", s.title, " (", s.items.size(), " lines)")
	items = cur.play_items(text, 100)
	print("NEW PLAY CARD: ", items.size(), " lines")
	for it in items:
		print("  NEW PLAY: ", it)
	for s in cur.parse(text).sections:
		print("NEW NEWS SECTION: ", s.title, " (", s.items.size(), " lines) areas: ", ", ".join(s.groups.map(func(g): return g.area)))
	quit(0)
