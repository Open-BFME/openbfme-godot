extends SceneTree
## Runtime proof that authoritative per-member Archer attack tokens present the
## selected-pack streak, audio, and target-impact assets without inventing a
## projectile when no target or token transition exists.

const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_ARCHER_PROJECTILE_PRESENTATION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE runner-started")
	var pack_root := OS.get_environment("OPENBFME_TEST_PACK").strip_edges()
	_check(
		"selected_archer_pack_available",
		pack_root != "" and pack_root.is_absolute_path() and not pack_root.begins_with("res://")
	)
	if pack_root == "":
		_finish()
		return
	# These runtime loads deliberately happen after project autoloads are live.
	# SceneTree --script entry points are compiled before autoload singleton names
	# are registered, so preloading either dependency here would reject their
	# legitimate ContentDB/ModLoader references.
	var battalion_script := load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var controller_script := load("res://src/retail_slice/retail_archer_projectile_controller.gd") as GDScript
	_check("runtime_dependencies_compile", battalion_script != null and controller_script != null)
	if battalion_script == null or controller_script == null:
		_finish()
		return
	var archer = battalion_script.new()
	root.add_child(archer)
	archer.entity_id = 77
	archer.object_id = ARCHER_OBJECT_ID
	archer.member_count = 1
	archer._source_unit_scale = 0.01
	archer.clip_map = {"idle": "fixture", "attack": "fixture"}
	archer.clip_sets = {"idle": ["fixture"], "attack": ["fixture"]}
	archer.member_health_ratios[0] = 1.0
	archer.member_attack_tokens[0] = 0
	archer.member_action_states[0] = "idle"
	archer.member_animation_players[0] = []
	var member := Node3D.new()
	member.name = "AuthoritativeArcherMember"
	var skeleton := Skeleton3D.new()
	skeleton.name = "AuthoritativeArcherSkeleton"
	skeleton.add_bone("ARROWNOCK")
	skeleton.set_bone_pose_position(0, Vector3(0.25, 1.25, 0.1))
	member.add_child(skeleton)
	archer.member_visuals[0] = member
	archer.add_child(member)
	var controller = controller_script.new()
	controller.name = "RetailArcherProjectileController"
	archer.archer_projectile_controller = controller
	archer.add_child(controller)
	_check(
		"controller_is_bound_to_authoritative_archer",
		archer.object_id == ARCHER_OBJECT_ID
			and archer.archer_projectile_controller == controller
			and controller.get_parent() == archer
	)
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE configuring-pack")
	controller.configure_from_pack(pack_root, 0.01)
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE pack-configured")
	var target := Node3D.new()
	target.name = "AuthoritativeProjectileTarget"
	target.position = Vector3(3.0, 0.0, 0.0)
	root.add_child(target)
	await process_frame
	_check(
		"selected_pack_projectile_contract_ready",
		archer.archer_projectile_controller != null
			and bool(archer.archer_projectile_controller.contract_ready)
			and int(archer.archer_projectile_controller.validated_fire_audio_leaf_count) == 32
			and int(archer.archer_projectile_controller.validated_impact_audio_leaf_count) == 68
	)
	archer.sync_member_states([100], 100, [0], "attack")
	_check("no_token_transition_creates_no_projectile", int(archer.archer_projectile_controller.active_projectile_node_count) == 0)
	archer.set_attack_target(target, 0.15, 0.1)
	archer.sync_member_states(
		[100],
		100,
		[1],
		"attack",
		[1],
		["ranged"],
		[target.global_position + Vector3(0.0, 0.15, 0.0)]
	)
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE projectile-spawned")
	_check(
		"member_attack_token_spawns_exact_streak",
		int(archer.archer_projectiles_presented) == 1
			and int(archer.archer_projectile_controller.active_projectile_node_count) == 1
	)
	var projectile_root := controller.find_child("RetailGondorArcherProjectile_*", false, false) as Node3D
	var streak := projectile_root.find_child("ExactEXArrowStreak01Texture", false, false) as MeshInstance3D if projectile_root != null else null
	_check(
		"streak_length_trails_authoritative_velocity",
		streak != null and is_equal_approx(streak.rotation.x, PI * 0.5) and streak.position.z > 0.0
	)
	_check(
		"projectile_launches_from_animated_arrow_nock",
		projectile_root != null
			and String(projectile_root.get_meta("launch_bone", "")) == "ARROWNOCK"
			and (projectile_root.get_meta("launch_global", Vector3.ZERO) as Vector3).is_equal_approx(skeleton.to_global(skeleton.get_bone_global_pose(0).origin))
	)
	await create_timer(0.2).timeout
	_check(
		"projectile_arrival_spawns_exact_target_impact",
		int(archer.archer_projectiles_presented) == 1
			and int(archer.archer_impacts_presented) == 1
			and int(archer.archer_projectile_controller.active_projectile_node_count) == 0
			and int(archer.archer_projectile_controller.active_impact_node_count) == 1
	)
	await create_timer(1.1).timeout
	_check("impact_lifetime_is_bounded", int(archer.archer_projectile_controller.active_impact_node_count) == 0)
	archer.queue_free()
	target.queue_free()
	await process_frame
	_finish()


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
		print("RETAIL_ARCHER_PROJECTILE_PRESENTATION PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_ARCHER_PROJECTILE_PRESENTATION FAIL %s" % name)


func _finish() -> void:
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
