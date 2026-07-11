extends CanvasLayer
## Command panel inspired by middle-earth-rts ui.js — selection-driven actions.

var match_ref: Node = null
@onready var res_label: Label = $Root/TopBar/Resources
@onready var toast_label: Label = $Root/Toast
@onready var panel: GridContainer = $Root/Bottom/Row/Col/Panel
@onready var info: Label = $Root/Bottom/Row/Col/Info
@onready var help: Label = $Root/Help
@onready var portrait: Label = $Root/Bottom/Row/Portrait
@onready var power_row: HBoxContainer = $Root/Bottom/Powers

var _toast_t: float = 0.0
var _power_arm: String = ""

func _ready() -> void:
	Events.toast.connect(_on_toast)
	Events.resources_changed.connect(func(_s, _a): refresh())
	Events.selection_changed.connect(func(_ids): _rebuild_panel())
	help.text = "LMB select/box · RMB move/attack · A attack-move · S stop · Z stance · F/G/B abilities\nF5 save · F9 load · Esc pause · +/- speed · Ctrl+1-9 groups · click power then ground"
	if panel:
		panel.columns = 6
	_rebuild_panel()
	_rebuild_powers()

func bind_match(m: Node) -> void:
	match_ref = m
	refresh()
	_rebuild_panel()
	_rebuild_powers()

func refresh() -> void:
	if res_label:
		var weather := ""
		if GameState.weather and float(GameState.weather.get("until", 0)) > SimClock.time:
			weather = " | %s" % String(GameState.weather.get("id", ""))
		res_label.text = "  %s vs %s  |  Gold %d  PP %d  |  t=%.0fs  x%.1f%s%s" % [
			GameState.player_faction.capitalize(),
			GameState.enemy_faction.capitalize(),
			int(GameState.resources.get(0, 0)),
			int(GameState.power_points.get(0, 0)),
			SimClock.time,
			SimClock.game_speed,
			"  [PAUSED]" if GameState.paused else "",
			weather,
		]
	if GameState.game_over:
		var wname := "PLAYER" if GameState.winner_side == 0 else "ENEMY"
		info.text = "GAME OVER — %s wins  |  trained %d  slain %d  razed %d  powers %d" % [
			wname,
			int(GameState.stats.get("units_trained", 0)),
			int(GameState.stats.get("enemies_slain", 0)),
			int(GameState.stats.get("buildings_razed", 0)),
			int(GameState.stats.get("powers_cast", 0)),
		]
		if portrait:
			portrait.text = "VICTORY" if GameState.winner_side == 0 else "DEFEAT"
		return
	if GameState.selected_ids.is_empty():
		if portrait:
			portrait.text = "COMMAND"
		info.text = "Select troops or a building · bottom row: build / train / stance / formation"
		return
	if match_ref == null or match_ref.world == null:
		return
	var id: int = GameState.selected_ids[0]
	var b = match_ref.world.get_battalion(id)
	if b:
		var udef: Dictionary = ContentDB.get_unit(b.type_id)
		var stances: Array = ["Aggro", "Defend", "Hold"]
		var st: String = String(stances[clampi(int(b.stance), 0, 2)])
		var multi: String = ""
		if GameState.selected_ids.size() > 1:
			multi = "x%d" % GameState.selected_ids.size()
		info.text = "%s  HP %d/%d  Rank %d  %s  %s" % [
			String(udef.get("name", b.type_id)), int(b.hp), int(b.hp_max), b.rank, st, multi,
		]
		if portrait:
			portrait.text = String(udef.get("name", b.type_id)).substr(0, 14)
		return
	var bud = match_ref.world.get_building(id)
	if bud:
		var bdef: Dictionary = ContentDB.get_building(bud.type_id)
		var q: int = int(bud.train_queue.size())
		var qinfo: String = ""
		if q > 0:
			qinfo = "  Q:%s" % String(bud.train_queue[0].get("id", "?"))
		var building_note: String = ""
		if not bud.built:
			building_note = "building…"
		info.text = "%s  HP %d/%d%s  %s" % [
			String(bdef.get("name", bud.type_id)), int(bud.hp), int(bud.hp_max), qinfo, building_note,
		]
		if portrait:
			portrait.text = String(bdef.get("name", bud.type_id)).substr(0, 14)

func _process(delta: float) -> void:
	if _toast_t > 0.0:
		_toast_t -= delta
		if _toast_t <= 0.0 and toast_label:
			toast_label.text = ""

func _on_toast(msg: String) -> void:
	if toast_label:
		toast_label.text = msg
	_toast_t = 2.8

func _rebuild_powers() -> void:
	if power_row == null:
		return
	for c in power_row.get_children():
		c.queue_free()
	var fac: Dictionary = ContentDB.get_faction(GameState.player_faction)
	var keys: Array = fac.get("powers", [])
	if keys.is_empty():
		keys = ContentDB.powers.keys()
	for pk in keys:
		var pdef: Dictionary = ContentDB.get_power(String(pk))
		if pdef.is_empty():
			continue
		var btn := Button.new()
		var cost := int(pdef.get("cost_pp", 5))
		var nm := String(pdef.get("name", pk))
		btn.text = "%s (%d)" % [nm.substr(0, 10), cost]
		btn.tooltip_text = String(pdef.get("desc", nm)) + " — click then ground if needed"
		var id := String(pk)
		btn.pressed.connect(func(): _arm_power(id))
		power_row.add_child(btn)

func _arm_power(power_id: String) -> void:
	_power_arm = power_id
	if match_ref and match_ref.has_method("arm_power"):
		match_ref.arm_power(power_id)
	Events.toast.emit("Power armed: %s — click ground" % power_id)

func _rebuild_panel() -> void:
	if panel == null:
		return
	for c in panel.get_children():
		c.queue_free()
	if match_ref == null:
		return
	# Always: stances + formations when troops selected
	var has_troops := false
	var has_building := false
	var building = null
	if match_ref.world:
		for id in GameState.selected_ids:
			var bat = match_ref.world.get_battalion(id)
			if bat and not bat.dead and bat.side == GameState.SIDE_PLAYER:
				has_troops = true
			var bud = match_ref.world.get_building(id)
			if bud and not bud.dead and bud.side == GameState.SIDE_PLAYER:
				has_building = true
				building = bud
	if has_troops:
		_add_btn("Stop", func(): match_ref.world.order_stop(GameState.selected_ids))
		_add_btn("A-Move", func(): match_ref.attack_move_arm = true; Events.toast.emit("Attack-move: right-click"))
		_add_btn("Aggro", func(): match_ref.world.set_stance(GameState.selected_ids, 0); Events.toast.emit("Aggressive"))
		_add_btn("Defend", func(): match_ref.world.set_stance(GameState.selected_ids, 1); Events.toast.emit("Defensive"))
		_add_btn("Hold", func(): match_ref.world.set_stance(GameState.selected_ids, 2); Events.toast.emit("Hold Ground"))
		_add_btn("Line", func(): GameState.formation = "line"; Events.toast.emit("Line"))
		_add_btn("Column", func(): GameState.formation = "column"; Events.toast.emit("Column"))
		_add_btn("Wedge", func(): GameState.formation = "wedge"; Events.toast.emit("Wedge"))
		_add_btn("Loose", func(): GameState.formation = "loose"; Events.toast.emit("Loose"))
		# hero abilities for first selected hero
		for id in GameState.selected_ids:
			var bat = match_ref.world.get_battalion(id)
			if bat == null or bat.dead:
				continue
			var udef: Dictionary = ContentDB.get_unit(bat.type_id)
			if not bool(udef.get("hero", false)):
				continue
			var abs: Array = udef.get("abilities", [])
			var i := 0
			for ak in abs:
				if i >= 4:
					break
				var adef: Dictionary = ContentDB.get_ability(String(ak))
				var req := 1
				if i >= 2:
					req = 3
				elif i == 1:
					req = 2
				var label := String(adef.get("name", ak))
				if bat.rank < req:
					label = "%s [R%d]" % [label.substr(0, 8), req]
				var key := String(ak)
				var bid := int(bat.id)
				_add_btn(label.substr(0, 12), func(): match_ref.world.try_cast_ability(bid, key))
				i += 1
			break
	if has_building and building:
		var bdef: Dictionary = ContentDB.get_building(building.type_id)
		for t in bdef.get("trains", []):
			var tid := String(t)
			var udef2: Dictionary = ContentDB.get_unit(tid)
			var cost := int(udef2.get("cost", 0))
			var nm := String(udef2.get("name", tid))
			_add_btn("%s %d" % [nm.substr(0, 9), cost], func(): match_ref.train_at_selected(tid))
		for h in bdef.get("heroes", []):
			var hid := String(h)
			var hdef: Dictionary = ContentDB.get_unit(hid)
			var hcost := int(hdef.get("cost", 0))
			# revival scaling shown roughly
			if int(GameState.hero_deaths.get(hid, 0)) > 0:
				hcost = int(hcost * (0.6 + 0.2 * int(GameState.hero_deaths[hid])))
			_add_btn("%s %d" % [String(hdef.get("name", hid)).substr(0, 9), hcost], func(): match_ref.train_at_selected(hid))
		for r in bdef.get("research", []):
			var rid := String(r)
			if GameState.has_upgrade(GameState.SIDE_PLAYER, rid):
				continue
			var rdef: Dictionary = ContentDB.get_research(rid)
			var rc := int(rdef.get("cost", 400))
			var rnm := String(rdef.get("name", rid))
			var bld_id := int(building.id)
			_add_btn("%s %d" % [rnm.substr(0, 9), rc], func(): match_ref.world.enqueue_research(bld_id, rid))
	# Build menu when no selection or fortress selected
	if GameState.selected_ids.is_empty() or (has_building and building and bool(ContentDB.get_building(building.type_id).get("fortress", false))):
		var fac: Dictionary = ContentDB.get_faction(GameState.player_faction)
		for bkey in fac.get("buildings", []):
			var bid := String(bkey)
			var bd: Dictionary = ContentDB.get_building(bid)
			if bd.is_empty() or bool(bd.get("fortress", false)):
				continue
			var cost := int(bd.get("cost", 0))
			var nm := String(bd.get("name", bid))
			_add_btn("%s %d" % [nm.substr(0, 9), cost], func(): match_ref.begin_place(bid))

func _add_btn(text: String, cb: Callable) -> void:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(96, 36)
	b.pressed.connect(cb)
	panel.add_child(b)
