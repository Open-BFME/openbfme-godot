extends SceneTree
## Asset-free regression: effect playback survives state refreshes, icon transforms/visibility
## reach the W3D renderer, and ambient handles are removed on leaving the map.
const Effects := preload("res://scripts/wotr_region_fx.gd")
const IconModel := preload("res://scripts/wotr_icon_model.gd")
const WarMap := preload("res://scripts/wotr.gd")
var failures := 0

class Renderer extends Node3D:
	var transform_seen := Transform3D.IDENTITY
	var opacity_seen := -1.0
	var removed: Array = []
	func set_instance_transform(_id: int, value: Transform3D) -> void:
		transform_seen = value
	func set_instance_opacity(_id: int, value: float) -> void:
		opacity_seen = value
	func remove_instance(id: int) -> void:
		removed.append(id)

class BuildingMap extends "res://scripts/wotr.gd":
	var icon_builds := 0
	func _icon_node(_icon: Dictionary, _size: int, _construction: bool, _owner := -1, _moving := false, _producing := false) -> Node3D:
		icon_builds += 1
		return Node3D.new()
	func _ground(_x: float, _y: float) -> float:
		return 0.0

class Audio extends Node:
	var removed: Array = []
	var played: Array = []
	func is_valid_event(_event: String) -> bool: return true
	func play_sound(event: String) -> int:
		played.append(event)
		return 8
	func play_sound_at(event: String, position: Vector3) -> int:
		played.append([event, position])
		return 9
	func remove_event(handle: int) -> void: removed.append(handle)

class Game extends Node:
	var _audio := Audio.new()

func check(ok: bool, reason: String) -> void:
	if not ok:
		failures += 1
		printerr("WOTR PRESENTATION FAIL: ", reason)

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	check(ClassDB.class_has_method("W3DInstancer", "set_instance_scene_fog"), "strategic icon fog binding is available")
	var points := [{"time": 0.0, "position": Vector3.ZERO}, {"time": 1.0, "position": Vector3.ONE}, {"time": 2.0, "position": Vector3.ZERO}]
	check(Effects.curve_value(points, 0.5) == 0.5, "ascending curve interpolates")
	check(Effects.curve_value(points, 1.5) == 0.5, "descending curve interpolates")
	check(Effects.curve_value(points, 3.0) == 0.0, "curve holds final intensity")
	var fx := Effects.new()
	fx.set_process(false)
	root.add_child(fx)
	var mesh := MeshInstance3D.new()
	fx.add_child(mesh)
	fx.definitions = {"edge:0": {"points": points, "opacity": 1.0}}
	fx.meshes = {"edge:0": {0: mesh}}
	fx.set_region(0, Color.TRANSPARENT, Color.TRANSPARENT, Color.RED)
	var material: Material = mesh.material_override
	fx.advance(0.5)
	check(mesh.visible, "selection becomes visible during fade")
	check(material.get_shader_parameter("intensity") == 0.5, "selection uses authored curve")
	for i in 10:
		fx.set_region(0, Color.TRANSPARENT, Color.TRANSPARENT, Color.RED)
	check(mesh.material_override == material, "unchanged refresh reuses material")
	check(fx.overlay_materials.size() == 1, "unchanged refresh does not retain extra materials")
	check(fx.active_effects["edge:0"][0].time == 0.5, "unchanged refresh does not restart fade")
	fx.set_selection(-1, Color.RED)
	check(not mesh.visible, "selection change immediately hides old edge")
	fx.advance(0.5)
	check(not mesh.visible, "old playback cannot reshow deselected region")
	fx.free()

	var renderer := Renderer.new()
	root.add_child(renderer)
	var parent := Node3D.new()
	root.add_child(parent)
	var icon := IconModel.new()
	icon.bind(renderer, 7)
	parent.add_child(icon)
	parent.position = Vector3(10, 20, 30)
	icon.position = Vector3(1, 2, 3)
	icon.force_update_transform()
	check(renderer.transform_seen.origin == Vector3(11, 22, 33), "parent and icon motion reach renderer")
	parent.hide()
	check(renderer.opacity_seen == 0.0, "hiding map hides instance")
	parent.show()
	check(renderer.opacity_seen == 1.0, "returning to map restores instance")
	icon.free()
	check(renderer.removed == [7], "freeing icon removes native instance once")
	parent.free()
	renderer.free()

	check(WarMap._building_producing({"production": [{"slot": 2}]}, 2), "production marker uses the region's queued building slot")
	check(not WarMap._building_producing({"production": [{"slot": 2}]}, 1), "another building's queue does not show a production marker")
	var buildings := BuildingMap.new()
	buildings.state = {"players": [{"template": "Player"}], "regions": [{"owner": 0, "buildings": [{"template": "Farm", "slot": 0, "turns_left": 0}], "production": []}]}
	buildings.data = {"player_templates": [{"name": "Player", "build_plot_icon": "Plot"}], "buildings": [{"name": "Farm", "icon": "FarmIcon"}],
		"icons": [{"kind": 1, "name": "FarmIcon"}, {"kind": 2, "name": "Plot"}]}
	buildings.regions = [{"building_spots": [Vector2(1, 2), Vector2(3, 4)]}]
	buildings.map_root = Node3D.new()
	buildings.add_child(buildings.map_root)
	root.add_child(buildings)
	buildings._place_buildings()
	check(buildings.icon_builds == 2, "building and empty plot each create one icon")
	var initial_building: Node = buildings.building_nodes[0]
	var initial_plot: Node = buildings.building_nodes[1]
	for i in 100:
		buildings._place_buildings()
	check(buildings.icon_builds == 2, "unchanged refreshes create no native instance tombstones")
	check(buildings.building_nodes[0] == initial_building and buildings.building_nodes[1] == initial_plot, "unchanged building and plot keep their nodes")
	buildings.state.regions[0].production = [{"slot": 0}]
	buildings._place_buildings()
	check(buildings.icon_builds == 3, "production transition rebuilds only its building")
	check(buildings.building_nodes[1] == initial_plot, "production transition retains the empty plot")
	buildings.state.regions[0].owner = -1
	buildings._place_buildings()
	check(buildings.building_nodes.size() == 1, "unowned region loses its empty plot icon")
	buildings.free()

	var game := Game.new()
	var war := WarMap.new()
	war.init(game)
	war.data = {"map_info": {"enter_map_sound": "Enter"}, "map_sounds": [{"name": "wind", "sound": "Wind", "flags": 8, "position": Vector3(1, 2, 3)}]}
	war._start_map_audio()
	check(game._audio.played == ["Enter", ["Wind", Vector3(1, 2, 3)]], "map entry starts authored audio in SAGE coordinates")
	war._stop_map_audio()
	check(game._audio.removed == [9], "leaving map stops ambient handle")
	war._stop_map_audio()
	check(game._audio.removed == [9], "stopping map audio is idempotent")
	war.free()
	game._audio.free()
	game.free()
	if failures == 0:
		print("WOTR PRESENTATION PASS")
	quit(0 if failures == 0 else 1)
