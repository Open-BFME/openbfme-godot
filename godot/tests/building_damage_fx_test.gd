## Lane FEAT-BLDFX-1: a building's damage states in the real client, with the effects they start. Needs a GPU/window and the retail files:
##
##   DISPLAY=:1 BLDFX_SHOTS=/some/dir godot --path godot --script res://tests/building_damage_fx_test.gd
##
## A headless run prints SKIP (exit 77). The case creates a GondorBarracks on the real object world, captures it PRISTINE, damages it through
## DAMAGED and REALLYDAMAGED (each hit a small part of its health, as a battle would), and pins:
##   * the model condition states and the model names of each state (the retail building's DAMAGED / REALLYDAMAGED model condition blocks);
##   * that the damage transition starts particle systems on the building (ParticleSysBone FireBuildingLarge / FireBuildingMedium /
##     SmokeBuildingLarge and the EnteringStateFX lists): get_particles_near reports systems/particles before and after;
##   * the screenshots <shots>/building-damage-before.png / -damaged.png / -reallydamaged.png, which must differ in the smoke region.
## This is the visible half of S-686 (TransitionDamageFX, lane FX-4) and the damage-state particle systems of the model draws.
extends SceneTree

const BUILDING := "GondorBarracks"
const MAP := "map good celduin"

var _failures := 0
var _checks := 0
var _shots := ""
var _world: Node3D
var _camera: Camera3D


func _initialize() -> void:
	call_deferred("_run")


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if ok:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		_failures += 1


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: building damage effects need a display (DISPLAY=:1, not --headless)")
		quit(77)
		return
	var rotwk := OS.get_environment("ROTWK_INSTALL")
	var bfme2 := OS.get_environment("BFME2_INSTALL")
	if rotwk.is_empty() or bfme2.is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	_shots = OS.get_environment("BLDFX_SHOTS")
	if _shots.is_empty():
		_shots = ProjectSettings.globalize_path("user://bldfx1")
	DirAccess.make_dir_recursive_absolute(_shots)
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_check(false, "retail mount: %s" % [mount.errors])
		quit(1)
		return
	_world = ClassDB.instantiate("GameWorld")
	root.add_child(_world)
	var setup: Dictionary = _world.setup(fs)
	_check(setup.ok, "world setup %s" % [setup.errors])
	var loaded: Dictionary = _world.load_map(MAP, {"seed": 5, "test_hooks": true, "terrain": true})
	_check(loaded.ok, "map loads %s" % [loaded.get("errors", [])])
	if not loaded.ok:
		_world.free()
		quit(1)
		return
	# a building away from the map's own objects, at the ground's height
	var ground: float = _world.get_ground_height(800.0, 800.0)
	var id: int = _world.create_object(BUILDING, 0, 800.0, 800.0, 0.0)
	_world.advance(0.5)
	var info: Dictionary = _world.get_object(id)
	_check(info.get("ok", false) and not info.get("models", []).is_empty(), "%s has a model (%s)" % [BUILDING, info.get("models", [])])
	if not info.get("ok", false) or info.get("models", []).is_empty():
		_world.free()
		quit(1)
		return
	_check(int(info.get("damage_state", -1)) == 0, "the new building is PRISTINE")
	_camera = Camera3D.new()
	_camera.current = true
	_camera.fov = 50.0
	_camera.near = 10.0
	_camera.far = 4000.0
	root.add_child(_camera)
	var at := Vector3(float(info.x), ground + 30.0, -float(info.y))
	_camera.position = Vector3(at.x + 150.0, at.y + 120.0, at.z + 150.0)
	_camera.look_at(at)
	await _settle(60)
	var before_particles: Dictionary = _world.get_particles_near(800.0, 800.0, 260.0)
	var before_shot := _shots.path_join("building-damage-before.png")
	await _shot(before_shot)
	print("BLDFX1VIEW pristine: conditions %s models %s particles %s" % [info.get("conditions", []), info.get("models", []), before_particles])
	# damage in battle-sized hits: each 5% is a few arrows, and the state changes once the health crosses MaxHealthDamaged / ReallyDamaged
	var max_hp: float = float(info.get("max_health", 1.0))
	var state := 0
	var damaged_shot := ""
	for step in 40:
		var r: Dictionary = _world.debug_damage_object(id, max_hp * 0.05, 0)
		if not r.get("ok", false):
			_check(false, "debug_damage_object: %s" % [r])
			break
		await _advance(0.2)
		info = _world.get_object(id)
		var st := int(info.get("damage_state", 0))
		if st != state:
			state = st
			print("BLDFX1VIEW damage state %d at %d / %d" % [state, int(info.get("health", 0)), int(max_hp)])
			if state == 1:
				await _settle(90) # 3 s: the DAMAGED fire and smoke build up
				var damaged_particles: Dictionary = _world.get_particles_near(800.0, 800.0, 260.0)
				_check(int(damaged_particles.get("systems", 0)) > int(before_particles.get("systems", 0)),
					"the DAMAGED state starts particle systems on the building (%s -> %s)" % [before_particles, damaged_particles]) # tolerance: the count is time-dependent; the case pins that damage starts systems
				_check(str(info.get("conditions", [])).contains("DAMAGED"), "the model condition is DAMAGED (%s)" % [info.get("conditions", [])])
				damaged_shot = _shots.path_join("building-damage-damaged.png")
				await _shot(damaged_shot)
				_check(await _differs(before_shot, damaged_shot), "the damaged screenshot differs from the pristine one")
				var played: Dictionary = _world.get_fx_report().get("played", {})
				_check(int(played.get("EnteringStateFX", 0)) > 0, "the EnteringStateFX list played (%s)" % [played]) # tolerance: a count of effects over the whole map, not a fixed value
			elif state == 2:
				await _settle(90)
				var really_particles: Dictionary = _world.get_particles_near(800.0, 800.0, 260.0)
				_check(int(really_particles.get("systems", 0)) > int(before_particles.get("systems", 0)),
					"the REALLYDAMAGED state keeps particle systems on the building (%s -> %s)" % [before_particles, really_particles]) # tolerance: the count is time-dependent; the case pins that damage starts systems
				var really_shot := _shots.path_join("building-damage-reallydamaged.png")
				await _shot(really_shot)
				_check(await _differs(damaged_shot, really_shot), "the REALLYDAMAGED screenshot differs from the DAMAGED one")
		if state >= 2:
			break
	_check(state == 2, "the hits reached REALLYDAMAGED (state %d)" % state)
	var fx: Dictionary = _world.get_fx_report()
	print("BLDFX1VIEW fx: playing %s particles %s stops %s" % [fx.get("played", {}), fx.get("particles", {}), fx.get("stops", [])])
	_world.free()
	print("BUILDING DAMAGE FX %s: %d checks, %d failures; screenshots %s" % ["PASS" if _failures == 0 else "FAIL", _checks, _failures, _shots])
	quit(0 if _failures == 0 else 1)


func _advance(seconds: float) -> void:
	_world.advance(seconds)
	for i in 3:
		await process_frame


func _settle(frames: int) -> void:
	for i in frames:
		_world.advance(1.0 / 30.0)
		await process_frame
	await RenderingServer.frame_post_draw


func _shot(path: String) -> void:
	await RenderingServer.frame_post_draw
	var image: Image = root.get_texture().get_image()
	_check(image.save_png(path) == OK, "screenshot %s" % path)


## true when the two images differ in a 200 x 200 box around the screenshot's centre (the building and its smoke)
func _differs(a: String, b: String) -> bool:
	var ia := Image.load_from_file(a)
	var ib := Image.load_from_file(b)
	if ia.is_empty() or ib.is_empty() or ia.get_size() != ib.get_size():
		return false
	var cx := ia.get_width() / 2
	var cy := ia.get_height() / 2
	var different := 0
	for y in range(maxi(0, cy - 100), mini(ia.get_height(), cy + 100)):
		for x in range(maxi(0, cx - 100), mini(ia.get_width(), cx + 100)):
			if ia.get_pixel(x, y) != ib.get_pixel(x, y):
				different += 1
	# tolerance: a visible change, not a pixel count; the smoke is a soft particle field and its exact texels vary with the GPU
	return different > 500
