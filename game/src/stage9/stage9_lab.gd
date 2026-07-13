class_name Stage9Lab
extends Node2D
## Playable Auric Loop objective lab with observable audio intent routing.

const WorldScript = preload("res://src/proof_stage9/proof_world.gd")
const CELL: float = 42.0
const ORIGIN: Vector2 = Vector2(28, 180)

var world: RefCounted
var rules_document: Dictionary = {}
var blue_id: int = 0
var red_id: int = 0
var simulation_running: bool = false
var accumulator: float = 0.0
var status_label: Label
var ring_label: Label
var holder_label: Label
var audio_label: Label
var feedback_label: Label
var buttons: Dictionary = {}
var juice_markers: Array[Dictionary] = []


func _ready() -> void:
	_build_hud()
	var error: String = _load_rules()
	if error != "":
		set_feedback(error)
		return
	reset_lab()


func _process(delta: float) -> void:
	if world == null:
		return
	if simulation_running:
		accumulator += minf(delta, 0.2)
		while accumulator >= 0.2:
			accumulator -= 0.2
			world.tick()
	_refresh()


func reset_lab() -> void:
	world = WorldScript.new()
	var error: String = world.setup(rules_document)
	if error != "":
		set_feedback("Rules rejected: " + error)
		return
	blue_id = int(world.add_unit(0, Vector2i(2, 6), 500, 600))
	red_id = int(world.add_unit(1, Vector2i(17, 6), 500, 600))
	simulation_running = false
	accumulator = 0.0
	juice_markers.clear()
	set_feedback("Ready. Spawn the Auric Loop, move a contender to it, then claim.")


func spawn_objective() -> Dictionary:
	var result: Dictionary = world.spawn_ring()
	if bool(result.get("ok", false)):
		_add_juice("spawn", Vector2i(world.ring["position"]), -1)
	set_feedback("Auric Loop spawned at cell %s" % str(result.get("position", "")) if bool(result.get("ok", false)) else "Spawn rejected: " + String(result.get("reason", "")))
	return result


func toggle_objective() -> Dictionary:
	var result: Dictionary = world.set_objective_enabled(not bool(world.objective_enabled))
	set_feedback("Optional objective %s · classic skirmish remains available" % ("ENABLED" if world.objective_enabled else "DISABLED"))
	return result


func move_team_to_ring(team: int) -> bool:
	var id: int = blue_id if team == 0 else red_id
	var accepted: bool = world.order_move(id, Vector2i(world.ring["position"]))
	set_feedback("%s contender moving toward the objective" % ("Blue" if team == 0 else "Red"))
	return accepted


func place_team_at_ring(team: int) -> void:
	var id: int = blue_id if team == 0 else red_id
	var unit: Dictionary = world.entity(id)
	unit["cell"] = world.ring["position"]
	unit["destination"] = unit["cell"]
	unit["order"] = "hold"
	_refresh()


func claim_for_team(team: int) -> Dictionary:
	var id: int = blue_id if team == 0 else red_id
	var result: Dictionary = world.claim_ring(id)
	if bool(result.get("ok", false)):
		_add_juice("claim" if String(result.get("kind", "")) == "claim" else "reclaim", Vector2i(world.entity(id)["cell"]), team)
	set_feedback("%s %s the objective" % ["Blue" if team == 0 else "Red", String(result.get("kind", "claimed")).capitalize()] if bool(result.get("ok", false)) else "Claim rejected: " + String(result.get("reason", "")))
	return result


func defeat_holder() -> Dictionary:
	var holder_id: int = int(world.ring.get("holder_id", 0))
	if holder_id == 0:
		var missing := {"ok": false, "reason": "no_holder"}
		set_feedback("No holder to defeat")
		return missing
	var holder: Dictionary = world.entity(holder_id)
	var source_id: int = red_id if int(holder["team"]) == 0 else blue_id
	var result: Dictionary = world.damage_unit(holder_id, source_id)
	_add_juice("clean_hit", Vector2i(holder["cell"]), int(holder["team"]))
	if bool(result.get("defeated", false)):
		_add_juice("drop", Vector2i(world.ring["position"]), int(holder["team"]))
	set_feedback("Clean non-gory impact: holder defeated; objective dropped with a %d tick reclaim delay" % int(world.rules["reclaimDelayTicks"]) if bool(result.get("defeated", false)) else "Clean non-gory hit feedback routed")
	return result


func advance_ticks(ticks: int) -> void:
	world.advance(ticks)
	set_feedback("Advanced to tick %d" % int(world.tick_index))


func advance_to_victory() -> void:
	if String(world.ring.get("state", "")) != "held":
		set_feedback("A contender must hold the objective first")
		return
	var remaining: int = maxi(0, int(world.rules["victoryHoldTicks"]) - int(world.ring["ticks_held"]))
	world.advance(remaining)
	set_feedback("Objective hold resolved: %s" % ("Blue wins" if int(world.winner) == 0 else "Red wins"))


func destroy_stronghold(team: int) -> Dictionary:
	var result: Dictionary = world.destroy_stronghold(team)
	if bool(result.get("ok", false)):
		_add_juice("stronghold_loss", Vector2i(1, 6) if team == 0 else Vector2i(18, 6), team)
	set_feedback("%s stronghold destroyed; deterministic %s victory" % ["Blue" if team == 0 else "Red", "Red" if team == 0 else "Blue"] if bool(result.get("ok", false)) else "Stronghold action rejected")
	return result


func toggle_running() -> void:
	simulation_running = not simulation_running
	accumulator = 0.0
	set_feedback("Simulation running" if simulation_running else "Simulation paused")


func set_feedback(text: String) -> void:
	if feedback_label != null:
		feedback_label.text = text
	_refresh()


func board_rect() -> Rect2:
	return Rect2(ORIGIN, Vector2(WorldScript.WIDTH, WorldScript.HEIGHT) * CELL)


func presentation_snapshot() -> Dictionary:
	if world == null:
		return {}
	var contenders: Array[Dictionary] = []
	for id: int in world.entity_ids():
		var unit: Dictionary = world.entity(id)
		var cell: Vector2i = unit["cell"]
		var destination: Vector2i = unit["destination"]
		contenders.append({"id": id, "team": int(unit["team"]), "cell": [cell.x, cell.y], "destination": [destination.x, destination.y], "order": String(unit["order"]), "health": int(unit["health"]), "alive": bool(unit["alive"])})
	var ring_cell: Vector2i = world.ring["position"]
	return {
		"board_rect": [ORIGIN.x, ORIGIN.y, board_rect().size.x, board_rect().size.y],
		"grid_cells": WorldScript.WIDTH * WorldScript.HEIGHT,
		"strongholds": [{"team": 0, "cell": [1, 6], "health": int(world.stronghold_health[0])}, {"team": 1, "cell": [18, 6], "health": int(world.stronghold_health[1])}],
		"contenders": contenders,
		"ring": {"state": String(world.ring["state"]), "cell": [ring_cell.x, ring_cell.y], "holder_id": int(world.ring["holder_id"])},
		"juice": juice_markers.duplicate(true),
	}


func presentation_signature() -> String:
	var value: int = 0x811C9DC5
	for byte: int in JSON.stringify(presentation_snapshot()).to_utf8_buffer():
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return "%08X" % value


func _add_juice(kind: String, cell: Vector2i, team: int) -> void:
	juice_markers.append({"sequence": juice_markers.size() + 1, "kind": kind, "cell": [cell.x, cell.y], "team": team})
	if juice_markers.size() > 12:
		juice_markers.pop_front()
	queue_redraw()


func _load_rules() -> String:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/stage9_ring_rules.json")
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK or typeof(parser.data) != TYPE_DICTIONARY:
		return "Stage 9 ring rules failed to load"
	rules_document = parser.data
	return ""


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root_control := Control.new()
	root_control.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root_control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root_control)
	var title := Label.new()
	title.position = Vector2(28, 24)
	title.text = "STAGE 9 · AURIC LOOP OBJECTIVE + AUDIO ROUTING LAB"
	title.add_theme_font_size_override("font_size", 27)
	title.add_theme_color_override("font_color", Color("f2cf68"))
	root_control.add_child(title)
	status_label = Label.new()
	status_label.position = Vector2(28, 66)
	status_label.add_theme_font_size_override("font_size", 17)
	root_control.add_child(status_label)
	ring_label = Label.new()
	ring_label.position = Vector2(28, 98)
	ring_label.add_theme_color_override("font_color", Color("ffd976"))
	root_control.add_child(ring_label)
	holder_label = Label.new()
	holder_label.position = Vector2(28, 128)
	holder_label.add_theme_color_override("font_color", Color("a8dfff"))
	root_control.add_child(holder_label)

	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	panel.offset_left = -650
	panel.offset_top = 24
	panel.offset_right = -22
	panel.offset_bottom = -22
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	root_control.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "Relic Objective Lifecycle"
	heading.add_theme_font_size_override("font_size", 22)
	column.add_child(heading)
	_add_button(column, "spawn", "1 · Spawn Auric Loop", spawn_objective)
	_add_button(column, "toggle_objective", "Enable / Disable Optional Objective", toggle_objective)
	var move_row := HBoxContainer.new()
	_add_button(move_row, "move_blue", "Move Blue to Loop", move_team_to_ring.bind(0))
	_add_button(move_row, "move_red", "Move Red to Loop", move_team_to_ring.bind(1))
	column.add_child(move_row)
	var claim_row := HBoxContainer.new()
	_add_button(claim_row, "claim_blue", "Claim / Reclaim Blue", claim_for_team.bind(0))
	_add_button(claim_row, "claim_red", "Claim / Reclaim Red", claim_for_team.bind(1))
	column.add_child(claim_row)
	_add_button(column, "defeat_holder", "Defeat Current Holder (Drop)", defeat_holder)
	_add_button(column, "advance_delay", "Advance Reclaim Delay (2 ticks)", advance_ticks.bind(2))
	_add_button(column, "victory", "Advance Exact Hold Time to Victory", advance_to_victory)
	var loss_row := HBoxContainer.new()
	_add_button(loss_row, "destroy_blue", "Destroy Blue Stronghold", destroy_stronghold.bind(0))
	_add_button(loss_row, "destroy_red", "Destroy Red Stronghold", destroy_stronghold.bind(1))
	column.add_child(loss_row)
	_add_button(column, "run", "Run / Pause 5 ticks per second", toggle_running)
	_add_button(column, "reset", "Reset Objective Lab", reset_lab)
	var explanation := Label.new()
	explanation.text = "Holder consequences (external rules):\n• speed 80% · damage 125% · globally revealed\n• 12 uninterrupted hold ticks wins\n• defeat drops at the holder cell · reclaim waits 2 ticks\n• objective can be disabled for classic skirmish\n\nHits, claims and drops use blood-free geometric feedback. No sound device is required: music/SFX cues below are deterministic intents routed by ID and sequence."
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	explanation.add_theme_color_override("font_color", Color("a9bec9"))
	column.add_child(explanation)
	var audio_heading := Label.new()
	audio_heading.text = "OBSERVABLE AUDIO EVENT LOG"
	audio_heading.add_theme_color_override("font_color", Color("f2cf68"))
	column.add_child(audio_heading)
	audio_label = Label.new()
	audio_label.custom_minimum_size.y = 180
	audio_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	audio_label.add_theme_color_override("font_color", Color("9ce5b2"))
	column.add_child(audio_label)
	feedback_label = Label.new()
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.add_theme_color_override("font_color", Color("ffd28c"))
	column.add_child(feedback_label)


func _add_button(parent: Node, key: String, text: String, callback: Callable) -> void:
	var button := Button.new()
	button.text = text
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.pressed.connect(callback)
	parent.add_child(button)
	buttons[key] = button


func _refresh() -> void:
	if world == null:
		return
	status_label.text = "TICK %d · HASH %s · %s · MUSIC %s · OBJECTIVE %s · STRONGHOLDS %d / %d" % [int(world.tick_index), world.state_hash_text(), "RUNNING" if simulation_running else "PAUSED", String(world.music_state).to_upper(), "ON" if world.objective_enabled else "OFF", int(world.stronghold_health[0]), int(world.stronghold_health[1])]
	ring_label.text = "AURIC LOOP: %s · CELL %s · CLAIMS %d · HOLD %d/%d" % [String(world.ring["state"]).to_upper(), str(world.ring["position"]), int(world.ring["claim_count"]), int(world.ring["ticks_held"]), int(world.rules["victoryHoldTicks"])]
	var holder_id: int = int(world.ring["holder_id"])
	if holder_id == 0:
		holder_label.text = "HOLDER: none · normal movement and damage"
	else:
		var holder: Dictionary = world.entity(holder_id)
		holder_label.text = "HOLDER: %s #%d · SPEED %d%% · DAMAGE %d · REVEALED %s" % ["BLUE" if int(holder["team"]) == 0 else "RED", holder_id, int(world.effective_speed_permille(holder_id)) / 10, int(world.effective_damage(holder_id)), "YES" if bool(world.rules["revealHolder"]) else "NO"]
	if int(world.winner) != -1:
		holder_label.text += " · WINNER %s (%s)" % ["BLUE" if int(world.winner) == 0 else "RED", String(world.victory_reason)]
	var lines: Array[String] = []
	for row: Dictionary in world.audio.events:
		lines.append("%02d  T%02d  %-8s  %s  team=%d entity=%d" % [int(row["sequence"]), int(row["tick"]), String(row["kind"]), String(row["event_id"]), int(row["team"]), int(row["entity_id"])])
	audio_label.text = "No routed events yet." if lines.is_empty() else "\n".join(lines)
	queue_redraw()


func _draw() -> void:
	if world == null:
		return
	draw_rect(board_rect(), Color("101d28"), true)
	for y: int in range(WorldScript.HEIGHT):
		for x: int in range(WorldScript.WIDTH):
			var rect := Rect2(ORIGIN + Vector2(x, y) * CELL, Vector2.ONE * CELL)
			draw_rect(rect, Color("172b36") if (x + y) % 2 == 0 else Color("132630"), true)
			draw_rect(rect, Color(0.3, 0.5, 0.58, 0.18), false, 1)
	# Strongholds are original geometric markers, not franchise assets.
	var blue_fort: Vector2 = _cell_center(Vector2i(1, 6))
	var red_fort: Vector2 = _cell_center(Vector2i(18, 6))
	draw_rect(Rect2(blue_fort - Vector2(15, 15), Vector2(30, 30)), Color("357fb0"), true)
	draw_rect(Rect2(red_fort - Vector2(15, 15), Vector2(30, 30)), Color("b04b4b"), true)
	_draw_health_bar(blue_fort + Vector2(-22, 21), int(world.stronghold_health[0]), 1000, Color("58baff"))
	_draw_health_bar(red_fort + Vector2(-22, 21), int(world.stronghold_health[1]), 1000, Color("ff7168"))
	_draw_board_text(blue_fort + Vector2(-38, -25), "BLUE HOLD", Color("86d3ff"))
	_draw_board_text(red_fort + Vector2(-32, -25), "RED HOLD", Color("ff9b95"))
	var ring_center: Vector2 = _cell_center(Vector2i(world.ring["position"]))
	var ring_state: String = String(world.ring["state"])
	if ring_state == "dormant":
		draw_arc(ring_center, 12, 0, TAU, 24, Color("78838b"), 2)
		draw_arc(ring_center, 7, 0, TAU, 24, Color("55616a"), 1)
	else:
		draw_circle(ring_center, 13, Color("ffd45c"), false, 5)
		draw_circle(ring_center, 6, Color("fff0a0"), false, 2)
	_draw_board_text(ring_center + Vector2(-34, -20), "LOOP %s" % ring_state.to_upper(), Color("ffd976") if ring_state != "dormant" else Color("87939b"))
	for id: int in world.entity_ids():
		var unit: Dictionary = world.entity(id)
		var center: Vector2 = _cell_center(Vector2i(unit["cell"]))
		var color := Color("58baff") if int(unit["team"]) == 0 else Color("ff7168")
		if not bool(unit["alive"]): color = Color("59636b")
		draw_circle(center, 12, color)
		draw_string(ThemeDB.fallback_font, center + Vector2(-5, 4), str(id), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color("071018"))
		_draw_health_bar(center + Vector2(-18, 17), int(unit["health"]), int(unit["maximum_health"]), color)
		_draw_board_text(center + Vector2(-25, -18), "BLUE" if int(unit["team"]) == 0 else "RED", color)
		if int(world.ring["holder_id"]) == id:
			draw_arc(center, 18, 0, TAU, 32, Color("ffd45c"), 4)
	for marker: Dictionary in juice_markers:
		var pair: Array = marker["cell"]
		_draw_juice(String(marker["kind"]), _cell_center(Vector2i(int(pair[0]), int(pair[1]))))
	draw_rect(board_rect(), Color("52788a"), false, 3)


func _cell_center(cell: Vector2i) -> Vector2:
	return ORIGIN + (Vector2(cell) + Vector2(0.5, 0.5)) * CELL


func _draw_health_bar(position: Vector2, health: int, maximum: int, color: Color) -> void:
	var size := Vector2(44, 5)
	draw_rect(Rect2(position, size), Color("071018"), true)
	var ratio: float = clampf(float(health) / float(maxi(1, maximum)), 0.0, 1.0)
	draw_rect(Rect2(position + Vector2.ONE, Vector2((size.x - 2.0) * ratio, size.y - 2.0)), color, true)


func _draw_board_text(position: Vector2, text: String, color: Color) -> void:
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("071018"), 3)
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, color)


func _draw_juice(kind: String, center: Vector2) -> void:
	match kind:
		"spawn", "claim", "reclaim":
			draw_arc(center, 22, 0, TAU, 32, Color(1.0, 0.84, 0.36, 0.8), 2)
			draw_arc(center, 27, 0, TAU, 32, Color(1.0, 0.94, 0.63, 0.45), 2)
		"clean_hit":
			for direction: Vector2 in [Vector2.RIGHT, Vector2.DOWN, Vector2.LEFT, Vector2.UP]:
				draw_line(center + direction * 14, center + direction * 24, Color("fff2b0"), 3)
		"drop":
			var diamond := PackedVector2Array([center + Vector2(0, -24), center + Vector2(24, 0), center + Vector2(0, 24), center + Vector2(-24, 0), center + Vector2(0, -24)])
			draw_polyline(diamond, Color("ffb45f"), 3)
		"stronghold_loss":
			draw_arc(center, 28, 0, TAU, 32, Color("ff746c"), 4)
