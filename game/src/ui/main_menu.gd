extends Control

@onready var start_btn: Button = $Center/Start
@onready var tests_btn: Button = $Center/Tests
@onready var quit_btn: Button = $Center/Quit
@onready var status: Label = $Center/Status
@onready var center: VBoxContainer = $Center

var player_opt: OptionButton
var enemy_opt: OptionButton
var map_opt: OptionButton
var diff_opt: OptionButton

func _ready() -> void:
	_build_options()
	start_btn.pressed.connect(_on_start)
	tests_btn.pressed.connect(_on_tests)
	quit_btn.pressed.connect(func(): get_tree().quit())
	status.text = "Content: %d units, %d buildings, %d factions, %d maps, %d powers" % [
		ContentDB.units.size(), ContentDB.buildings.size(), ContentDB.factions.size(),
		ContentDB.maps.size(), ContentDB.powers.size()
	]

func _build_options() -> void:
	player_opt = OptionButton.new()
	enemy_opt = OptionButton.new()
	map_opt = OptionButton.new()
	diff_opt = OptionButton.new()
	for fid in ["gondor", "mordor", "elves", "goblins"]:
		var f: Dictionary = ContentDB.get_faction(fid)
		var name := String(f.get("name", fid))
		player_opt.add_item(name)
		player_opt.set_item_metadata(player_opt.item_count - 1, fid)
		enemy_opt.add_item(name)
		enemy_opt.set_item_metadata(enemy_opt.item_count - 1, fid)
	player_opt.select(0)
	enemy_opt.select(1)
	for mid in ContentDB.maps.keys():
		var m: Dictionary = ContentDB.get_map(mid)
		map_opt.add_item(String(m.get("name", mid)))
		map_opt.set_item_metadata(map_opt.item_count - 1, mid)
	for d in ["easy", "normal", "hard"]:
		diff_opt.add_item(d.capitalize())
		diff_opt.set_item_metadata(diff_opt.item_count - 1, d)
	diff_opt.select(1)
	var row := HBoxContainer.new()
	row.add_child(_labeled("Player", player_opt))
	row.add_child(_labeled("Enemy", enemy_opt))
	center.add_child(row)
	center.move_child(row, 2)
	var row2 := HBoxContainer.new()
	row2.add_child(_labeled("Map", map_opt))
	row2.add_child(_labeled("Difficulty", diff_opt))
	center.add_child(row2)
	center.move_child(row2, 3)

func _labeled(title: String, w: Control) -> VBoxContainer:
	var v := VBoxContainer.new()
	var l := Label.new()
	l.text = title
	v.add_child(l)
	v.add_child(w)
	return v

func _on_start() -> void:
	GameState.player_faction = String(player_opt.get_item_metadata(player_opt.selected))
	GameState.enemy_faction = String(enemy_opt.get_item_metadata(enemy_opt.selected))
	GameState.map_id = String(map_opt.get_item_metadata(map_opt.selected))
	GameState.difficulty = String(diff_opt.get_item_metadata(diff_opt.selected))
	get_tree().change_scene_to_file("res://scenes/match.tscn")

func _on_tests() -> void:
	var runner := load("res://tests/run_stage_tests.gd")
	if runner:
		var r = runner.new()
		var report: String = r.run_all()
		status.text = report
		print(report)
