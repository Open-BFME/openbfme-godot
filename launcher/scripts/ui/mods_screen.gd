## Lane MODS-3: Mods. The player's one-click mods from the All In One BFME Launcher's workshop (bfmeladder.com; scripts/core/mod_install.gd):
##   * Play with: the one mod Play starts (or none: the original game), what it is ("ready", or "needs engine features": the engine
##     changes it carries are not in OpenBFME yet, so the game may differ or fail), and the installed mods with Remove;
##   * Get mods: the source, Load the list / Refresh (nothing is asked of the service before that press), the download's progress with
##     Pause and Cancel;
##   * the list: every Rise of the Witch-king package (patches and mods) with its version, author, size and state, its enhancements as
##     boxes to tick, and Install / Update / Apply. The question before the first download is main.gd's (the consent).
## It only shows and asks; scripts/main.gd does the work (signals). Lane UI-5's placeholder ("Edain coming later") is replaced.
extends Control

signal refresh(force: bool)
signal install(guid: String, enhancements: Array)
signal remove(guid: String)
signal pick(guid: String)
signal pause_pressed
signal cancel_pressed

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ModCatalog := preload("res://scripts/core/mod_catalog.gd")
const ModInstall := preload("res://scripts/core/mod_install.gd")

const SOURCE := "Mods from bfmeladder.com, the workshop of the All In One BFME Launcher. They are made by their own authors, not by " + \
	"OpenBFME. Every file is checked against the workshop's list; your game folders are never changed."
const NEEDS := "Needs engine features: its changes to the game program are not in OpenBFME yet, so the game may look or play " + \
	"differently, or fail to start. Not taken: %s."
const NONE := "The original game (no mod)"

var _pick: OptionButton
var _pick_guids: Array = [""]
var _pick_note: Label
var _installed: VBoxContainer
var _note: Label
var _refresh: Button
var _progress: VBoxContainer
var _bar: ProgressBar
var _bar_label: Label
var _pause: Button
var _cancel: Button
var _list: VBoxContainer
var _view := {}       # the last catalogue (ModCatalog.group)
var _records := {}    # guid -> its installed record (openbfme-aio.json)
var _partial := {}    # guid -> name: a download that was started and not finished
var _active := {"guid": "", "enhancements": []}
var _chosen := {}     # package guid -> {enhancement guid: true}: the ticked boxes
var _busy := false


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Mods"))
	var s := W.scroll(Style.SCREEN_GAP)
	v.add_child(s.scroll)
	var grid := W.hbox(16)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.box.add_child(grid)

	var play := W.panel(6)
	play.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	play.panel.size_flags_vertical = Control.SIZE_FILL  # the two cards of the row end together
	grid.add_child(play.panel)
	play.box.add_child(W.h3("Play with"))
	_pick = OptionButton.new()
	_pick.item_selected.connect(func(i: int) -> void: pick.emit(String(_pick_guids[i])))
	play.box.add_child(W.row(_with_note("Mod", "Play starts the game with it."), _pick))
	_pick_note = W.wrap("", "Muted")
	play.box.add_child(_pick_note)
	_installed = W.vbox(0)
	play.box.add_child(_installed)

	var get := W.panel(8)
	get.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	get.panel.size_flags_vertical = Control.SIZE_FILL
	grid.add_child(get.panel)
	get.box.add_child(W.h3("Get mods"))
	get.box.add_child(W.wrap(SOURCE, "Muted"))
	_note = W.wrap("The list is not loaded yet. Nothing is asked of bfmeladder.com before you press Load the list.", "Muted")
	get.box.add_child(_note)
	_progress = W.vbox(6)
	_progress.visible = false
	_bar_label = W.trim_label("", "Muted")
	_progress.add_child(_bar_label)
	_bar = W.progress_bar()
	_progress.add_child(_bar)
	get.box.add_child(_progress)
	var buttons := W.hbox(8)
	_refresh = W.button("Load the list", "refresh", "", 15)
	_refresh.pressed.connect(func() -> void: refresh.emit(not _view.is_empty()))
	buttons.add_child(_refresh)
	_pause = W.button("Pause", "pause", "", 14)
	_pause.pressed.connect(func() -> void: pause_pressed.emit())
	buttons.add_child(_pause)
	_cancel = W.button("Cancel", "stop", "", 14)
	_cancel.pressed.connect(func() -> void: cancel_pressed.emit())
	buttons.add_child(_cancel)
	get.box.add_child(buttons)

	var list := W.panel(0)
	list.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	s.box.add_child(list.panel)
	list.box.add_child(W.h3("The Rise of the Witch-king mods"))
	_list = W.vbox(0)
	list.box.add_child(_list)
	set_state(false, false, false)
	_show()


## a name with a muted line below (Settings' rows)
static func _with_note(name: String, note: String) -> VBoxContainer:
	var v := W.vbox(1)
	var n := W.label(name)
	Style.text(n, "Inter", 500, 14, Style.TEXT)
	v.add_child(n)
	v.add_child(W.wrap(note, "Muted"))
	return v


## the catalogue (ModCatalog.group); `time`: when it was read (Unix seconds)
func set_catalogue(view: Dictionary, time: float, from_cache := false) -> void:
	_view = view
	for row in view.get("packages", []):
		if not _chosen.has(row.guid):
			_chosen[row.guid] = {}
			for g in _records.get(row.guid, {}).get("enhancements", []):
				_chosen[row.guid][g] = true
	var t := Time.get_datetime_dict_from_unix_time(int(time) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60)
	set_note("%d packages, read at %02d:%02d%s." % [view.get("packages", []).size(), t.hour, t.minute, " (kept from the last half hour)" if from_cache else ""])
	_refresh.text = "Refresh"
	_show()


func set_note(text: String) -> void:
	_note.text = text


## records: ModInstall.installed(); active: ModInstall.active(); partial: guid -> name of the downloads not finished
func set_installed(records: Array, active: Dictionary, partial: Dictionary) -> void:
	_records = {}
	for r in records:
		_records[r.guid] = r
	_active = active
	_partial = partial
	_show()


## running: a mod download runs; paused: one is paused (Resume continues it); busy: another job runs
func set_state(running: bool, paused: bool, busy: bool) -> void:
	_busy = running or paused or busy
	_refresh.disabled = _busy
	_pause.visible = running or paused
	_cancel.visible = running or paused
	_pause.text = "Resume" if paused else "Pause"
	_pause.icon = VectorIcon.make("resume" if paused else "pause", 14)
	_progress.visible = running or paused
	_pick.disabled = running
	for b in get_tree().get_nodes_in_group("mods_action") if is_inside_tree() else []:
		b.disabled = _busy or b.get_meta("done", false)


func set_progress(text: String, done: int, total: int) -> void:
	_bar_label.text = text
	_bar.max_value = maxi(total, 1)
	_bar.value = done


## the ticked enhancements of a package, in the list's order
func chosen(guid: String) -> Array:
	var out := []
	for row in _view.get("packages", []):
		if row.guid == guid:
			for e in row.enhancements:
				if _chosen.get(guid, {}).get(e.guid, false):
					out.append(e.guid)
	return out


static func _sorted(a: Array) -> Array:
	var b := a.duplicate()
	b.sort()
	return b


func _show() -> void:
	# Play with: the picker, what the picked mod is, the installed ones
	_pick.clear()
	_pick_guids = [""]
	_pick.add_item(NONE)
	var names: Array = _records.values()
	names.sort_custom(func(a, b): return String(a.name).naturalnocasecmp_to(String(b.name)) < 0)
	for r in names:
		_pick.add_item("%s %s" % [r.name, r.version])
		_pick_guids.append(r.guid)
	var at := _pick_guids.find(_active.guid)
	if at < 0 and _active.guid != "":
		_pick.add_item("%s (not installed)" % _active.guid)
		_pick_guids.append(_active.guid)
		at = _pick_guids.size() - 1
	_pick.select(maxi(at, 0))
	var picked: Dictionary = _records.get(_active.guid, {})
	if _active.guid == "":
		_pick_note.text = "Play starts The Rise of the Witch-king 2.01 as it is."
	elif picked.is_empty():
		_pick_note.text = "This mod is not installed any more: Play does not start. Pick another one, or none."
	else:
		var with := PackedStringArray()
		for g in picked.get("enhancements", []):
			with.append(_name_of(g))
		var text := "Ready: Play starts The Rise of the Witch-king with %s %s%s." % [picked.name, picked.version, " and " + ", ".join(with) if not with.is_empty() else ""]
		if not picked.get("engine_edits", []).is_empty():
			text = NEEDS % ", ".join(PackedStringArray(picked.engine_edits))
		if _chosen.has(_active.guid) and _sorted(chosen(_active.guid)) != _sorted(picked.get("enhancements", [])):
			text += " The ticked enhancements are not installed yet: press Apply."
		_pick_note.text = text
	_pick_note.add_theme_color_override("font_color", Style.WARN if picked.get("engine_edits", []).size() > 0 or (_active.guid != "" and picked.is_empty()) else Style.TEXT3)
	for c in _installed.get_children() + _list.get_children():
		c.get_parent().remove_child(c)
		c.queue_free()
	var rows := names.map(func(r): return [r.guid, "%s %s" % [r.name, r.version], r.get("engine_edits", []).is_empty()])
	for g in _partial:
		if not _records.has(g):
			rows.append([g, "%s (download not finished)" % _partial[g], true])
	for i in rows.size():
		var rm := W.button("Remove", "stop", "", 14)
		rm.add_to_group("mods_action")
		rm.disabled = _busy
		var guid: String = rows[i][0]
		rm.pressed.connect(func() -> void: remove.emit(guid))
		var left := _with_note(String(rows[i][1]), "Installed: ready" if rows[i][2] else "Installed: needs engine features")
		_installed.add_child(W.row(left, rm, i == rows.size() - 1))
	# the list
	var packages: Array = _view.get("packages", [])
	var loose: Array = _view.get("loose", [])
	if packages.is_empty() and loose.is_empty():
		_list.add_child(W.row(W.wrap("Load the list to see the mods you can install.", "Muted"), null, true))
		return
	var all := packages + loose
	for i in all.size():
		_list.add_child(_package_row(all[i], i == all.size() - 1, i >= packages.size()))


func _name_of(guid: String) -> String:
	for row in _view.get("packages", []) + _view.get("loose", []):
		if row.guid == guid:
			return row.name
		for e in row.enhancements:
			if e.guid == guid:
				return e.name
	return guid


## one line of a row in a state's colour: ready (green), needs engine features (amber), cannot be installed (red)
static func _state_line(row: Dictionary) -> Label:
	var text := "Ready: it changes only the game's data, which OpenBFME loads like the original game does."
	var color := Style.GOOD
	if row.error != "":
		text = "Cannot be installed: " + row.error
		color = Style.BAD
	elif not row.engine_edits.is_empty():
		text = NEEDS % ", ".join(PackedStringArray(row.engine_edits))
		color = Style.WARN
	var l := W.wrap(text, "Muted")
	l.add_theme_color_override("font_color", color)
	return l


func _package_row(row: Dictionary, last: bool, alone: bool) -> VBoxContainer:
	var left := W.vbox(3)
	var title := W.label(row.name)
	Style.text(title, "Inter", 600, 15, Style.TEXT)
	left.add_child(title)
	# the service's Type 0 is a patch or a mod installed into the game folder (Edain), Type 1 a mod in its own folder: both are mods here
	var kind := "Enhancement of the original game" if alone else ("Enhancement" if row.type == 2 else "Mod")
	left.add_child(W.label("%s · %s · by %s · about %s" % [kind, row.version, row.author if row.author != "" else "unknown",
		ModInstall.size_text(int(row.get("bytes", row.size)))], "Muted"))
	left.add_child(_state_line(row))
	for e in row.enhancements:
		var box := CheckBox.new()
		box.text = "%s %s (%s)" % [e.name, e.version, e.status if e.error == "" else "cannot be installed"]
		box.disabled = e.error != ""
		box.set_pressed_no_signal(_chosen.get(row.guid, {}).get(e.guid, false))
		var g: String = row.guid
		var eg: String = e.guid
		box.toggled.connect(func(on: bool) -> void:
			if not _chosen.has(g):
				_chosen[g] = {}
			_chosen[g][eg] = on
			_show())
		box.tooltip_text = ", ".join(PackedStringArray(e.engine_edits)) if not e.engine_edits.is_empty() else ""
		left.add_child(box)
	var rec: Dictionary = _records.get(row.guid, {})
	var want := chosen(row.guid)
	var text := "Install"
	if _partial.has(row.guid):
		text = "Continue"
	elif not rec.is_empty() and rec.version != row.version:
		text = "Update"
	elif not rec.is_empty() and _sorted(rec.get("enhancements", [])) != _sorted(want):
		text = "Apply"
	elif not rec.is_empty():
		text = "Installed"
	var go := W.button(text, "download", "", 15)
	go.add_to_group("mods_action")
	go.set_meta("done", text == "Installed" or row.error != "")
	go.disabled = _busy or go.get_meta("done")
	var guid: String = row.guid
	go.pressed.connect(func() -> void: install.emit(guid, chosen(guid)))
	return W.row(left, go, last)


## --ui-demo=mods: a made-up catalogue and one installed mod (no network, nothing written)
func demo() -> void:
	var e := func(guid: String, name: String, version: String, status: String, edits: Array) -> Dictionary:
		return {"guid": guid, "name": name, "version": version, "author": "Demo Team", "type": 2, "size": 2000000, "status": status,
			"engine_edits": PackedStringArray(edits), "error": "", "enhancements": []}
	var realms := {"guid": "demo-realms", "name": "Example Realms", "version": "1.2.0", "author": "Realm Builders", "type": 0, "size": 3200000000,
		"status": ModCatalog.READY, "engine_edits": PackedStringArray(), "error": "",
		"enhancements": [e.call("demo-nointro", "Realms No Intro", "1.0.0", ModCatalog.READY, []), e.call("demo-font", "Realms Big Font", "1.0.1", ModCatalog.READY, [])]}
	var patch := {"guid": "demo-patch", "name": "Made-Up Patch 9.9", "version": "9.8.0", "author": "The Test Team", "type": 0, "size": 1300000000,
		"status": ModCatalog.NEEDS, "engine_edits": PackedStringArray(["Made-Up Patch 9.9: lotrbfme2ep1.exe", "Made-Up Patch 9.9: game.dat"]), "error": "",
		"enhancements": [e.call("demo-timer", "Patch Timer Addon", "1.0.0", ModCatalog.NEEDS, ["Patch Timer Addon: timer.asi"])]}
	var tales := {"guid": "demo-tales", "name": "Tiny Tales", "version": "2.0.1", "author": "A. Storyteller", "type": 1, "size": 650000000,
		"status": ModCatalog.READY, "engine_edits": PackedStringArray(), "error": "", "enhancements": []}
	_records = {}
	_chosen = {"demo-realms": {"demo-nointro": true}}
	set_installed([{"guid": "demo-realms", "name": "Example Realms", "version": "1.2.0", "enhancements": ["demo-nointro"], "engine_edits": []}],
		{"guid": "demo-realms", "enhancements": ["demo-nointro"]}, {})
	set_catalogue({"packages": [patch, realms, tales], "loose": [e.call("demo-zoom", "Zoom Out", "1.0.3", ModCatalog.READY, [])]},
		Time.get_unix_time_from_system())
