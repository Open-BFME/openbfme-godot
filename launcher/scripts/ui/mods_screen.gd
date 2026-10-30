## Lane MODS-3: Mods. The player's one-click mods from the All In One BFME Launcher's workshop (bfmeladder.com; scripts/core/mod_install.gd):
##   * Play with: the one mod Play starts (or none: the original game), what it is ("ready", or "needs engine features": the engine
##     changes it carries are not in OpenBFME yet, so the game may differ or fail), and the installed mods with Remove;
##   * Get mods: the source, Load the list / Refresh (nothing is asked of the service before that press), the download's progress with
##     Pause and Cancel;
##   * the list: every Rise of the Witch-king package (patches and mods) with its version, author, size and state, its enhancements as
##     boxes to tick, and Install / Update / Apply. The question before the first download is main.gd's (the consent);
##   * lane SDK-1: Asset packs: the HD model and texture packs in the game's asset-packs folder (scripts/core/asset_packs.gd), each with
##     what it holds, its problems, and a box to turn it on or off; Open the folder.
## It only shows and asks; scripts/main.gd does the work (signals). Lane UI-5's placeholder ("Edain coming later") is replaced.
extends Control

signal refresh(force: bool)
signal install(guid: String, enhancements: Array)
signal remove(guid: String)
signal pick(guid: String)
signal pause_pressed
signal cancel_pressed
signal asset_pack_toggled(name: String, on: bool) # lane SDK-1
signal open_asset_packs

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ModCatalog := preload("res://scripts/core/mod_catalog.gd")
const ModInstall := preload("res://scripts/core/mod_install.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const SOURCE := "launcher.mods_screen.source"
const NEEDS := "launcher.mods_screen.needs"
const NONE := "launcher.mods_screen.none"
const ACTIONS := {"install": "launcher.mods_screen.action_install", "continue": "launcher.mods_screen.action_continue",
	"update": "launcher.mods_screen.action_update", "apply": "launcher.mods_screen.action_apply", "installed": "launcher.mods_screen.action_installed"}
const PACKS := "launcher.mods_screen.packs_note" # lane SDK-1

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
var _packs_box: VBoxContainer # lane SDK-1
var _packs := []


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2(L10n.t("launcher.mods_screen.title")))
	var s := W.scroll(Style.SCREEN_GAP)
	v.add_child(s.scroll)
	var grid := W.hbox(16)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.box.add_child(grid)

	var play := W.panel(6)
	play.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	play.panel.size_flags_vertical = Control.SIZE_FILL  # the two cards of the row end together
	grid.add_child(play.panel)
	play.box.add_child(W.h3(L10n.t("launcher.mods_screen.play_with")))
	_pick = OptionButton.new()
	_pick.item_selected.connect(func(i: int) -> void: pick.emit(String(_pick_guids[i])))
	play.box.add_child(W.row(_with_note(L10n.t("launcher.mods_screen.mod"), L10n.t("launcher.mods_screen.mod_note")), _pick))
	_pick_note = W.wrap("", "Muted")
	play.box.add_child(_pick_note)
	_installed = W.vbox(0)
	play.box.add_child(_installed)

	var get := W.panel(8)
	get.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	get.panel.size_flags_vertical = Control.SIZE_FILL
	grid.add_child(get.panel)
	get.box.add_child(W.h3(L10n.t("launcher.mods_screen.get_mods")))
	get.box.add_child(W.wrap(L10n.t(SOURCE), "Muted"))
	_note = W.wrap(L10n.t("launcher.mods_screen.not_loaded"), "Muted")
	get.box.add_child(_note)
	_progress = W.vbox(6)
	_progress.visible = false
	_bar_label = W.trim_label("", "Muted")
	_progress.add_child(_bar_label)
	_bar = W.progress_bar()
	_progress.add_child(_bar)
	get.box.add_child(_progress)
	var buttons := W.hbox(8)
	_refresh = W.button(L10n.t("launcher.mods_screen.load_list"), "refresh", "", 15)
	_refresh.pressed.connect(func() -> void: refresh.emit(not _view.is_empty()))
	buttons.add_child(_refresh)
	_pause = W.button(L10n.t("launcher.games_screen.pause"), "pause", "", 14)
	_pause.pressed.connect(func() -> void: pause_pressed.emit())
	buttons.add_child(_pause)
	_cancel = W.button(L10n.t("common.cancel"), "stop", "", 14)
	_cancel.pressed.connect(func() -> void: cancel_pressed.emit())
	buttons.add_child(_cancel)
	get.box.add_child(buttons)

	var packs := W.panel(6)
	packs.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	packs.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	s.box.add_child(packs.panel)
	var open := W.button(L10n.t("launcher.mods_screen.packs_open"), "folder", "", 14)
	open.pressed.connect(func() -> void: open_asset_packs.emit())
	var head := W.hbox(8) # the title with the folder button at the right, where the list's buttons end
	var title := W.h3(L10n.t("launcher.mods_screen.packs_title"))
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(title)
	head.add_child(open)
	packs.box.add_child(head)
	packs.box.add_child(W.wrap(L10n.t(PACKS), "Muted"))
	_packs_box = W.vbox(0)
	packs.box.add_child(_packs_box)

	var list := W.panel(0)
	list.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	s.box.add_child(list.panel)
	list.box.add_child(W.h3(L10n.t("launcher.mods_screen.list_title")))
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
	set_note(L10n.t("launcher.mods_screen.read_at", {"count": view.get("packages", []).size(), "time": "%02d:%02d" % [t.hour, t.minute],
		"cached": L10n.t("launcher.mods_screen.kept") if from_cache else ""}))
	_refresh.text = L10n.t("launcher.mods_screen.refresh")
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


## lane SDK-1: the asset packs (AssetPacks.list())
func set_asset_packs(packs: Array) -> void:
	_packs = packs
	for c in _packs_box.get_children():
		c.queue_free()
	if packs.is_empty():
		_packs_box.add_child(W.wrap(L10n.t("launcher.mods_screen.packs_none"), "Muted"))
		return
	for i in packs.size():
		var p: Dictionary = packs[i]
		var left := W.vbox(1)
		var n := W.label(p.name)
		Style.text(n, "Inter", 500, 14, Style.TEXT)
		left.add_child(n)
		left.add_child(W.wrap(L10n.t("launcher.mods_screen.packs_contents", {"models": p.models, "textures": p.textures, "size": ModInstall.size_text(int(p.bytes))}), "Muted"))
		for why in p.problems:
			var l := W.wrap(L10n.t("launcher.mods_screen.packs_problem", {"why": why}), "Muted")
			l.add_theme_color_override("font_color", Style.BAD)
			left.add_child(l)
		# lane SDK-1b (b): what the game left out at its last start; the Details toggle (beside On) lists the files and the reasons
		var details: VBoxContainer = null
		if p.problems.is_empty():
			if not p.get("checked", false):
				left.add_child(W.wrap(L10n.t("launcher.mods_screen.packs_unchecked"), "Muted"))
			var findings := []
			if not p.get("misfits", []).is_empty():
				findings.append([L10n.t("launcher.mods_screen.packs_misfits", {"count": p.misfits.size()}), p.misfits])
			if not findings.is_empty():
				details = W.vbox(1)
				details.visible = false
				for f in findings:
					var l := W.wrap(f[0], "Muted")
					l.add_theme_color_override("font_color", Style.WARN)
					left.add_child(l)
					for item in f[1]:
						details.add_child(W.wrap(L10n.t("launcher.mods_screen.packs_finding", {"file": str(item.get("file", "")), "why": str(item.get("why", ""))}), "Muted"))
				left.add_child(details)
		var box := CheckBox.new()
		box.text = L10n.t("launcher.mods_screen.packs_on")
		box.button_pressed = p.enabled
		var name: String = p.name
		box.toggled.connect(func(on: bool) -> void: asset_pack_toggled.emit(name, on))
		var right: Control = box
		if details != null:
			var both := W.hbox(8)
			var toggle := W.button(L10n.t("launcher.mods_screen.packs_details"), "", "", 12)
			var shown := details
			toggle.pressed.connect(func() -> void:
				shown.visible = not shown.visible
				toggle.text = L10n.t("launcher.mods_screen.packs_hide_details") if shown.visible else L10n.t("launcher.mods_screen.packs_details"))
			both.add_child(toggle)
			both.add_child(box)
			right = both
		_packs_box.add_child(W.row(left, right, i == packs.size() - 1))


## running: a mod download runs; paused: one is paused (Resume continues it); busy: another job runs
func set_state(running: bool, paused: bool, busy: bool) -> void:
	_busy = running or paused or busy
	_refresh.disabled = _busy
	_pause.visible = running or paused
	_cancel.visible = running or paused
	_pause.text = L10n.t("launcher.games_screen.resume") if paused else L10n.t("launcher.games_screen.pause")
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
	_pick.add_item(L10n.t(NONE))
	var names: Array = _records.values()
	names.sort_custom(func(a, b): return String(a.name).naturalnocasecmp_to(String(b.name)) < 0)
	for r in names:
		_pick.add_item(L10n.t("launcher.mods_screen.name_version", {"name": r.name, "version": r.version}))
		_pick_guids.append(r.guid)
	var at := _pick_guids.find(_active.guid)
	if at < 0 and _active.guid != "":
		_pick.add_item(L10n.t("launcher.mods_screen.not_installed", {"guid": _active.guid}))
		_pick_guids.append(_active.guid)
		at = _pick_guids.size() - 1
	_pick.select(maxi(at, 0))
	var picked: Dictionary = _records.get(_active.guid, {})
	if _active.guid == "":
		_pick_note.text = L10n.t("launcher.mods_screen.plays_original")
	elif picked.is_empty():
		_pick_note.text = L10n.t("launcher.mods_screen.gone")
	else:
		var with := PackedStringArray()
		for g in picked.get("enhancements", []):
			with.append(_name_of(g))
		var text := L10n.t("launcher.mods_screen.ready_with", {"name": picked.name, "version": picked.version,
			"extras": L10n.t("launcher.mods_screen.and_extras", {"list": ", ".join(with)}) if not with.is_empty() else ""})
		if not picked.get("engine_edits", []).is_empty():
			text = L10n.t(NEEDS, {"edits": ", ".join(PackedStringArray(picked.engine_edits))})
		if _chosen.has(_active.guid) and _sorted(chosen(_active.guid)) != _sorted(picked.get("enhancements", [])):
			text += L10n.t("launcher.mods_screen.press_apply")
		_pick_note.text = text
	_pick_note.add_theme_color_override("font_color", Style.WARN if picked.get("engine_edits", []).size() > 0 or (_active.guid != "" and picked.is_empty()) else Style.TEXT3)
	for c in _installed.get_children() + _list.get_children():
		c.get_parent().remove_child(c)
		c.queue_free()
	var rows := names.map(func(r): return [r.guid, L10n.t("launcher.mods_screen.name_version", {"name": r.name, "version": r.version}), r.get("engine_edits", []).is_empty()])
	for g in _partial:
		if not _records.has(g):
			rows.append([g, L10n.t("launcher.mods_screen.partial", {"name": _partial[g]}), true])
	for i in rows.size():
		var rm := W.button(L10n.t("launcher.mods_screen.remove"), "stop", "", 14)
		rm.add_to_group("mods_action")
		rm.disabled = _busy
		var guid: String = rows[i][0]
		rm.pressed.connect(func() -> void: remove.emit(guid))
		var left := _with_note(String(rows[i][1]), L10n.t("launcher.mods_screen.installed_ready") if rows[i][2] else L10n.t("launcher.mods_screen.installed_needs"))
		_installed.add_child(W.row(left, rm, i == rows.size() - 1))
	# the list
	var packages: Array = _view.get("packages", [])
	var loose: Array = _view.get("loose", [])
	if packages.is_empty() and loose.is_empty():
		_list.add_child(W.row(W.wrap(L10n.t("launcher.mods_screen.load_to_see"), "Muted"), null, true))
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
	var text := L10n.t("launcher.mods_screen.state_ready")
	var color := Style.GOOD
	if row.error != "":
		text = L10n.t("launcher.mods_screen.state_error", {"error": row.error})
		color = Style.BAD
	elif not row.engine_edits.is_empty():
		text = L10n.t(NEEDS, {"edits": ", ".join(PackedStringArray(row.engine_edits))})
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
	var kind := L10n.t("launcher.mods_screen.kind_alone") if alone else (L10n.t("launcher.mods_screen.kind_enhancement") if row.type == 2 else L10n.t("launcher.mods_screen.mod"))
	left.add_child(W.label(L10n.t("launcher.mods_screen.row_line", {"kind": kind, "version": row.version,
		"author": row.author if row.author != "" else L10n.t("launcher.mods_screen.unknown_author"),
		"size": ModInstall.size_text(int(row.get("bytes", row.size)))}), "Muted"))
	left.add_child(_state_line(row))
	for e in row.enhancements:
		var box := CheckBox.new()
		box.text = L10n.t("launcher.mods_screen.enhancement_box", {"name": e.name, "version": e.version,
			"status": e.status if e.error == "" else L10n.t("launcher.mods_screen.cannot_install")})
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
	var text := "install"
	if _partial.has(row.guid):
		text = "continue"
	elif not rec.is_empty() and rec.version != row.version:
		text = "update"
	elif not rec.is_empty() and _sorted(rec.get("enhancements", [])) != _sorted(want):
		text = "apply"
	elif not rec.is_empty():
		text = "installed"
	var go := W.button(L10n.t(ACTIONS[text]), "download", "", 15)
	go.add_to_group("mods_action")
	go.set_meta("done", text == "installed" or row.error != "")
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
	set_asset_packs([{"name": "HD Gondor", "enabled": true, "models": 12, "textures": 40, "bytes": 182000000, "problems": PackedStringArray(), "checked": true,
			"misfits": [{"file": "art/w3d/gb/gbcitadel.w3d", "why": "different bones or models than the game's"}]},
		{"name": "Sharper Trees", "enabled": false, "models": 0, "textures": 9, "bytes": 21000000, "problems": PackedStringArray(), "checked": false,
			"misfits": []}])
	_records = {}
	_chosen = {"demo-realms": {"demo-nointro": true}}
	set_installed([{"guid": "demo-realms", "name": "Example Realms", "version": "1.2.0", "enhancements": ["demo-nointro"], "engine_edits": []}],
		{"guid": "demo-realms", "enhancements": ["demo-nointro"]}, {})
	set_catalogue({"packages": [patch, realms, tales], "loose": [e.call("demo-zoom", "Zoom Out", "1.0.3", ModCatalog.READY, [])]},
		Time.get_unix_time_from_system())
