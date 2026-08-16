extends SceneTree
## Generic ParticleSysBone attachment beyond the water-ripple path.
##
## AnimationState-scoped rows follow the selector's picked conditions.
## ModelConditionState rows stay up while their flags remain active.
## BurstDelay / InitialDelay are composed from RetailFxTiming.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const ParticleSysBoneScript := preload("res://src/retail_slice/particle_sys_bone.gd")
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "PARTICLE_SYS_BONE_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var rows := [
		_typed("IdleAnimationState", [], "NONE", "IdleGlow", false),
		_typed("AnimationState", ["MOVING"], "BONE", "FX_Dust", true),
		_typed("ModelConditionState", ["DAMAGED"], "FIREBONE", "FireBuildingSmall", false),
	]
	var idle: Dictionary = ParticleSysBoneScript.select(rows, [], [])
	_check("ParticleSysBone idle row starts with the idle state", _has_system(idle, "IdleGlow") and not _has_system(idle, "FX_Dust"))
	var moving: Dictionary = ParticleSysBoneScript.select(rows, ["MOVING"], ["MOVING"])
	_check("AnimationState MOVING dust replaces idle glow", _has_system(moving, "FX_Dust") and not _has_system(moving, "IdleGlow"))
	var damaged_move: Dictionary = ParticleSysBoneScript.select(rows, ["MOVING"], ["MOVING", "DAMAGED"])
	_check("ModelConditionState DAMAGED fire stays up under a MOVING clip", _has_system(damaged_move, "FireBuildingSmall") and _has_system(damaged_move, "FX_Dust"))
	_check("FollowBone is retained on the MOVING dust row", bool((moving.get("attachments", []) as Array)[0].get("followBone", false)))
	var visual := Node3D.new()
	var skeleton := Skeleton3D.new()
	skeleton.add_bone("BONE")
	visual.add_child(skeleton)
	get_root().add_child(visual)
	var applied: Dictionary = ParticleSysBoneScript.apply(visual, moving.get("attachments", []), {
		"FX_Dust": {
			"initialDelayFrames": {"minimum": 0.0, "maximum": 0.0},
			"burstDelayFrames": {"minimum": 2.0, "maximum": 2.0},
		},
	}, 4)
	_check("apply parents an emitter under the authored bone", int(applied.get("applied", 0)) == 1)
	var follow := skeleton.get_node_or_null("Follow_BONE") as BoneAttachment3D
	_check("FollowBone:Yes uses BoneAttachment3D", follow != null and follow.bone_idx == 0)
	var timing: Node = follow.get_node_or_null("PSB_FX_Dust/RetailFxTiming") if follow != null else null
	_check("BurstDelay/InitialDelay compose RetailFxTiming", timing != null and int(applied.get("timed", 0)) == 1)
	if timing != null:
		_check("composed timing emits on the authored burst cadence", int(timing.call("advance_frames", 0.0)) == 1 and int(timing.call("advance_frames", 2.0)) == 1)
	else:
		_check("composed timing emits on the authored burst cadence", false)
	ParticleSysBoneScript.apply(visual, idle.get("attachments", []))
	_check("leaving MOVING tears down the FollowBone dust", skeleton.get_node_or_null("Follow_BONE") == null)
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	battalion.bind_particle_sys_bone_contracts({"moduleContracts": rows})
	_check("battalion binds typed ParticleSysBone moduleContracts", battalion.particle_sys_bone_contracts.size() == 3)
	battalion.free()
	visual.free()
	_finish()


func _typed(state_kind: String, conditions: Array, bone: String, system_id: String, follow: bool) -> Dictionary:
	return {
		"module": "ParticleSysBone",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": state_kind,
			"conditions": {"value": conditions},
			"bone": {"value": bone},
			"particleSystem": {"value": system_id},
			"FollowBone": {"value": follow},
		},
	}


func _has_system(result: Dictionary, system_id: String) -> bool:
	for row_value in result.get("attachments", []) as Array:
		if String((row_value as Dictionary).get("particleSystem", "")) == system_id:
			return true
	return false


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("PARTICLE_SYS_BONE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
