## Lane UI-5: News, every release's notes of the channel on parchment cards, newest first, two columns ("News & patch notes" of the
## prototype). The notes are the releases' own (the release list of the last check, or its cache offline); nothing is made up.
extends Control

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const ReleaseNotes := preload("res://scripts/core/release_notes.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const SHOWN := 12  # releases shown (the API lists 30)

var _grid: GridContainer
var _empty: Label


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2(L10n.t("launcher.news_screen.title")))
	var s := W.scroll(Style.SCREEN_GAP)
	v.add_child(s.scroll)
	_empty = W.wrap(L10n.t("launcher.news_screen.empty"), "Muted")
	s.box.add_child(_empty)
	_grid = GridContainer.new()
	_grid.columns = 2
	_grid.add_theme_constant_override("h_separation", 16)
	_grid.add_theme_constant_override("v_separation", 16)
	_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.box.add_child(_grid)


## releases: [{tag, notes, published}] newest first
func set_releases(releases: Array) -> void:
	for c in _grid.get_children():
		c.queue_free()
	_empty.visible = releases.is_empty()
	for r in releases.slice(0, SHOWN):
		var p := W.parchment(4, Vector2(24, 20), false)
		p.card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		p.box.add_child(W.parch_label(ReleaseNotes.title_of(r.tag), "title", 20))
		var when := ReleaseNotes.date_text(str(r.get("published", "")))
		p.box.add_child(W.parch_label(r.tag + (" · " + when if when != "" else ""), "sub", 15))
		var parsed := ReleaseNotes.parse(str(r.get("notes", "")))
		if parsed.sections.is_empty():
			p.box.add_child(W.parch_label(Paths.scrub(parsed.intro) if parsed.intro != "" else L10n.t("launcher.news_screen.no_notes"), "body", 16))
		for sec in parsed.sections:
			if sec.title != "":
				var t := W.parch_label(String(sec.title), "title", 14)
				p.box.add_child(t)
			for g in sec.groups:   # NOTES-1: the studio-style notes' bold area lines
				if g.area != "":
					p.box.add_child(W.parch_label(Paths.scrub(String(g.area)), "sub", 14))
				for it in g.items:
					p.box.add_child(W.parch_item(Paths.scrub(it), 16))
		_grid.add_child(p.card)
