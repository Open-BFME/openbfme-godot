extends SceneTree
## Runtime proof that authoritative per-member Archer attack tokens present the
## selected-pack streak, audio, and target-impact assets without inventing a
## projectile when no target or token transition exists.

const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const ARCHER_PROJECTILE_PACK_KEY := "gondorArcherProjectile"
const PROJECTILE_ART_PACK_KEY := "projectileArt"
const PROJECTILE_ART_SCHEMA := "openbfme.projectile-art-runtime"
## Every check this runner is expected to reach; see _finish().
const EXPECTED_CHECK_COUNT := 34

var passed := 0
var failed := 0
var fixture_root := ""


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_ARCHER_PROJECTILE_PRESENTATION_RUNNER", 0, 0, true)
	_runner_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE runner-started")
	# OPENBFME_TEST_PACK is an override, not a requirement. Production
	# (retail_battalion._mounted_archer_projectile_root) self-locates the pack
	# that declares the contract; a test that is strictly more brittle than the
	# code it covers reports environment shape as a lane failure.
	var pack_root := OS.get_environment("OPENBFME_TEST_PACK").strip_edges()
	var pack_root_source := "environment"
	if pack_root == "" or not _pack_declares(pack_root, ARCHER_PROJECTILE_PACK_KEY):
		var located := _mounted_root_declaring(ARCHER_PROJECTILE_PACK_KEY)
		if located != "":
			pack_root = located
			pack_root_source = "mounted-root-fallback"
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_PHASE pack-source=%s" % pack_root_source)
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
	var configured_probe = battalion_script.new()
	root.add_child(configured_probe)
	configured_probe.configure(
		78, 0, "bfme2.object.elven-lorien-archer", {}, 0, 0.01, []
	)
	_check(
		"configure_derives_non_gondor_binding_and_mounted_sidecar",
		configured_probe.projectile_object_id == "GoodFactionArrow"
			and configured_probe.archer_projectile_controller != null
			and configured_probe.archer_projectile_controller.contract_ready
	)
	configured_probe.queue_free()
	var ranger_probe = battalion_script.new()
	root.add_child(ranger_probe)
	ranger_probe.configure(
		80, 0, "bfme2.object.gondor-ranger", {}, 0, 0.01, []
	)
	_check(
		"ranger_compiled_binding_is_allowlisted_arrow",
		ranger_probe.projectile_object_id == "GoodFactionArrow"
	)
	_check(
		"ranger_keeps_authored_arrow_launch_bone",
		ranger_probe.weapon_launch_bone == "ARROW"
	)
	_check(
		"ranger_configure_binds_mounted_arrow_sidecar",
		ranger_probe.archer_projectile_controller != null
			and ranger_probe.archer_projectile_controller.contract_ready
	)
	ranger_probe.queue_free()
	var siege_probe = battalion_script.new()
	root.add_child(siege_probe)
	siege_probe.configure(
		79, 0, "bfme2.object.gondor-trebuchet", {}, 0, 0.01, []
	)
	_check(
		"non_arrow_projectile_never_binds_arrow_presentation",
		siege_probe.projectile_object_id == "GondorTrebuchetRockProjectile"
			and siege_probe.archer_projectile_controller == null
	)
	siege_probe.queue_free()
	var archer = battalion_script.new()
	root.add_child(archer)
	var faction_bindings := {
		"men": [ARCHER_OBJECT_ID, "GondorArcherArrow"],
		"elves": ["bfme2.object.elven-lorien-archer", "GoodFactionArrow"],
		"dwarves": ["bfme2.object.dwarven-men-of-dale", "GoodFactionArrow"],
		"isengard": ["bfme2.object.isengard-uruk-crossbow", "GoodFactionArrow"],
		"mordor": ["bfme2.object.mordor-archer", "EvilFactionArrow"],
		"wild": ["bfme2.object.goblin-archer", "EvilFactionArrow"],
		"angmar": ["bfme2.object.angmar-dark-ranger", "EvilFactionArrow"],
		"ranger": ["bfme2.object.gondor-ranger", "GoodFactionArrow"],
	}
	for faction_value in faction_bindings.keys():
		var faction := String(faction_value)
		var binding := faction_bindings[faction] as Array
		_check(
			"compiled_member_id_resolves_%s_projectile_binding" % faction,
			archer._compiled_projectile_object_id(String(binding[0])) == String(binding[1])
		)
	archer.entity_id = 77
	archer.object_id = ARCHER_OBJECT_ID
	archer.projectile_object_id = "GondorArcherArrow"
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
	# Every faction's compiled ranged member carries a retail
	# projectileObjectId.  Presentation must follow that binding instead of
	# rejecting everything except the historical Gondor special case.
	archer.object_id = "bfme2.object.elven-lorien-archer"
	archer.projectile_object_id = "GoodFactionArrow"
	archer.sync_member_states(
		[100], 100, [2], "attack", [2], ["ranged"],
		[target.global_position + Vector3(0.0, 0.15, 0.0)]
	)
	var non_gondor_projectile := archer.archer_projectile_controller.find_child(
		"RetailGondorArcherProjectile_*", false, false
	) as Node
	_check(
		"non_gondor_compiled_projectile_binding_spawns_visible_arrow",
		int(archer.archer_projectiles_presented) == 2
			and int(archer.archer_projectile_controller.active_projectile_node_count) == 1
			and non_gondor_projectile != null
			and String(non_gondor_projectile.get_meta("projectile_object_id", "")) == "GoodFactionArrow"
	)
	await create_timer(1.1).timeout
	_check("impact_lifetime_is_bounded", int(archer.archer_projectile_controller.active_impact_node_count) == 0)
	archer.queue_free()
	target.queue_free()
	await process_frame
	await _check_compiled_projectile_art(battalion_script, controller_script)
	_cleanup_fixture()
	_finish()


func _check_compiled_projectile_art(battalion_script: GDScript, controller_script: GDScript) -> void:
	## Art must resolve BY the compiled projectileObjectId from whichever
	## mounted pack ships it -- never by borrowing another projectile's row.
	fixture_root = _build_projectile_art_fixture()
	_check("projectile_art_fixture_pack_written", fixture_root != "")
	if fixture_root == "":
		return
	var resolver = battalion_script.new()
	root.add_child(resolver)
	var evil := resolver._compiled_projectile_art({"_pack_root": fixture_root}, "EvilFactionArrow") as Dictionary
	_check(
		"compiled_art_resolves_by_projectile_object_id",
		not evil.is_empty()
			and String(evil.get("_pack_root", "")) == fixture_root
			and String((evil.get("entry", {}) as Dictionary).get("projectileObjectId", "")) == "EvilFactionArrow"
	)
	_check(
		"compiled_art_never_borrows_a_foreign_projectile_row",
		(resolver._compiled_projectile_art({"_pack_root": fixture_root}, "GoodFactionArrow") as Dictionary).is_empty()
	)
	var art_controller = controller_script.new()
	root.add_child(art_controller)
	var bound: bool = art_controller.configure_from_projectile_art(
		"EvilFactionArrow", fixture_root, evil.get("entry", {}) as Dictionary, 0.01
	)
	_check(
		"compiled_art_binds_its_own_streak_texture",
		bound
			and bool(art_controller.contract_ready)
			and String(art_controller.art_binding) == "compiled-projectile-art"
			and String(art_controller.art_projectile_object_id) == "EvilFactionArrow"
			and int(art_controller.validated_streak_texture_count) == 1
	)
	_check(
		"compiled_art_does_not_claim_the_gondor_impact_closure",
		not bool(art_controller.parity_ready)
			and int(art_controller.validated_impact_model_count) == 0
			and _has_diagnostic(art_controller.diagnostics, "projectile-art-impact-closure-absent")
	)
	var art_projectile: Node3D = art_controller.present_authoritative_projectile(
		1, Transform3D.IDENTITY, false, 0, false
	)
	_check(
		"compiled_art_projectile_carries_its_binding_provenance",
		art_projectile != null
			and String(art_projectile.get_meta("art_binding", "")) == "compiled-projectile-art"
			and String(art_projectile.get_meta("art_projectile_object_id", "")) == "EvilFactionArrow"
	)
	art_controller.remove_projectile(1)
	art_controller.queue_free()
	# Absent art must be a NAMED diagnostic, not a silent contract-error string.
	var content_db := _content_db()
	var mounted: Array[String] = []
	if content_db != null:
		mounted.assign(content_db.pack_roots)
		content_db.pack_roots = [] as Array[String]
	# This provocation is supposed to fail closed, so mute the engine warning it
	# legitimately raises; the proof gates read a stray WARNING as a defect.
	battalion_script.suppress_engine_warnings = true
	var starved = battalion_script.new()
	root.add_child(starved)
	starved.projectile_object_id = "EvilFactionArrow"
	starved._configure_combat_visual_contract({})
	battalion_script.suppress_engine_warnings = false
	_check(
		"absent_projectile_art_reports_a_named_diagnostic",
		_has_diagnostic(starved.combat_visual_diagnostics, "arrow-art-unresolved")
			and not bool(starved.combat_visual_source_closure_present)
	)
	starved.queue_free()
	if content_db != null:
		content_db.pack_roots = mounted
	# With today's packs only the Gondor sidecar exists, so a non-Gondor
	# projectile is a borrow: that must be named too.
	var borrower = battalion_script.new()
	root.add_child(borrower)
	borrower.projectile_object_id = "EvilFactionArrow"
	borrower._configure_combat_visual_contract({})
	# Not an OR over both codes -- that passes no matter what happens. Assert the
	# code that MUST fire for the binding that actually occurred: a bound
	# sidecar is a borrow and must say so; compiled art must NOT claim a borrow.
	var borrower_bound_sidecar := (
		borrower.archer_projectile_controller != null
		and String(borrower.archer_projectile_controller.art_binding) == "gondor-arrow-closure"
	)
	var borrower_bound_compiled := (
		borrower.archer_projectile_controller != null
		and String(borrower.archer_projectile_controller.art_binding) == "compiled-projectile-art"
	)
	var borrow_named := _has_diagnostic(
		borrower.combat_visual_diagnostics, "arrow-art-shared-good-fallback"
	)
	_check(
		"shared_good_arrow_borrow_reports_a_named_diagnostic",
		(borrow_named if borrower_bound_sidecar
			else (not borrow_named if borrower_bound_compiled
				else _has_diagnostic(borrower.combat_visual_diagnostics, "arrow-art-unresolved")))
	)
	borrower.queue_free()
	# Precedence: the sidecar is GondorArcherArrow's OWN full closure (streak +
	# g_arrow impact + 100 audio leaves). Once republished packs also ship an
	# art-only row for it, the fuller closure must still win.
	var mounted_fixture := false
	if content_db != null and not content_db.pack_roots.has(fixture_root):
		content_db.pack_roots.append(fixture_root)
		mounted_fixture = true
	var gondor = battalion_script.new()
	root.add_child(gondor)
	gondor.projectile_object_id = "GondorArcherArrow"
	gondor._configure_combat_visual_contract({})
	_check(
		"gondor_sidecar_full_closure_outranks_art_only_row",
		gondor.archer_projectile_controller != null
			and bool(gondor.archer_projectile_controller.contract_ready)
			and String(gondor.archer_projectile_controller.art_binding) == "gondor-arrow-closure"
			and int(gondor.archer_projectile_controller.validated_impact_model_count) == 1
	)
	gondor.queue_free()
	if mounted_fixture and content_db != null:
		content_db.pack_roots.erase(fixture_root)
	# The diagnostics must be CONSUMED, not just recorded. Prove the slice's
	# aggregate pulls a battalion's rows up (and deduplicates them) rather than
	# leaving an array nobody reads.
	var slice_script := load("res://src/retail_slice/retail_vertical_slice.gd") as GDScript
	var slice = slice_script.new()
	var donor = battalion_script.new()
	donor.object_id = "bfme2.object.mordor-archer"
	donor.combat_visual_diagnostics.append({
		"code": "arrow-art-shared-good-fallback",
		"projectileObjectId": "EvilFactionArrow",
	})
	donor.combat_visual_diagnostics.append({
		"code": "arrow-art-shared-good-fallback",
		"projectileObjectId": "EvilFactionArrow",
	})
	slice._aggregate_arrow_art_diagnostics(donor)
	slice._aggregate_arrow_art_diagnostics(donor)
	_check(
		"slice_aggregates_and_deduplicates_arrow_art_diagnostics",
		slice.arrow_art_diagnostics.size() == 1
			and String(slice.arrow_art_diagnostics[0].get("code", "")) == "arrow-art-shared-good-fallback"
			and (slice.get_meta("arrow_art_diagnostics", []) as Array).size() == 1
	)
	donor.free()
	slice.free()
	resolver.queue_free()
	await process_frame


func _content_db() -> Node:
	var content_db := root.get_node_or_null("/root/ContentDB")
	if content_db != null and (content_db.pack_roots as Array).is_empty():
		var workspace := ProjectSettings.globalize_path("res://../.private/content-packs")
		if DirAccess.dir_exists_absolute(workspace):
			OS.set_environment("OPENBFME_CONTENT", workspace)
			content_db.reload()
	return content_db


func _pack_declares(pack_root: String, key: String) -> bool:
	if pack_root == "":
		return false
	var pack_path := pack_root.path_join("pack.json")
	if not FileAccess.file_exists(pack_path):
		return false
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(pack_path))
	if typeof(value) != TYPE_DICTIONARY:
		return false
	var files: Variant = (value as Dictionary).get("files", {})
	return typeof(files) == TYPE_DICTIONARY and (files as Dictionary).has(key)


func _mounted_root_declaring(key: String) -> String:
	var content_db := _content_db()
	if content_db == null:
		return ""
	for root_value in content_db.pack_roots:
		var candidate := String(root_value)
		if _pack_declares(candidate, key):
			return candidate
	return ""


func _write_json(path: String, value: Variant) -> bool:
	if DirAccess.make_dir_recursive_absolute(path.get_base_dir()) != OK:
		return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value))
	file.close()
	return true


func _art_row(projectile_object_id: String, virtual_path: String, texture_relative: String) -> Dictionary:
	return {
		"projectileObjectId": projectile_object_id,
		"objectKind": "Object",
		"source": {"virtualPath": virtual_path, "line": 22},
		"draws": [
			{
				"kind": "W3DStreakDraw",
				"instanceTag": "ModuleTag_Draw2",
				"length": 15,
				"width": 2,
				"numSegments": 1,
				"additive": false,
				"color": {"r": 255, "g": 255, "b": 255},
				"texture": texture_relative,
				"weatherTextures": {},
				"source": {
					"definingObject": projectile_object_id,
					"virtualPath": virtual_path,
					"line": 30,
				},
			}
		],
	}


func _build_projectile_art_fixture() -> String:
	## A pack that ships compiled art for EvilFactionArrow ONLY, with a
	## deliberately distinct (red) streak texture so a Good-arrow borrow is
	## visible as a failure rather than an indistinguishable pass.
	var fixture := "user://projectile-art-%d" % (Time.get_ticks_usec() & 0xFFFFFF)
	var absolute := ProjectSettings.globalize_path(fixture)
	var texture_relative := "assets/textures/projectiles/evilfactionarrowstreak.png"
	var image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	image.fill(Color(1.0, 0.0, 0.0, 1.0))
	var texture_path := absolute.path_join(texture_relative)
	if DirAccess.make_dir_recursive_absolute(texture_path.get_base_dir()) != OK:
		return ""
	if image.save_png(texture_path) != OK:
		return ""
	if not _write_json(
		absolute.path_join("pack.json"),
		{
			"id": "projectile-art-fixture",
			"schema": "openbfme.content-pack",
			"schemaVersion": 0,
			"files": {PROJECTILE_ART_PACK_KEY: "data/projectile-art.json"},
		}
	):
		return ""
	if not _write_json(
		absolute.path_join("data/projectile-art.json"),
		{
			"schema": PROJECTILE_ART_SCHEMA,
			"schemaVersion": 0,
			"projectiles": [
				_art_row("EvilFactionArrow", "data/ini/object/evilfaction/evilfactionsubobjects.ini", texture_relative),
				# Shipped so the runner can prove the Gondor sidecar's FULL closure
				# still outranks an art-only row for its own projectile.
				_art_row("GondorArcherArrow", "data/ini/object/goodfaction/goodfactionsubobjects.ini", texture_relative),
			],
			"unconvertedModelProjectileObjectIds": [],
			"unsupportedDrawModules": [],
		}
	):
		return ""
	return absolute


func _cleanup_fixture() -> void:
	if fixture_root == "":
		return
	for relative in [
		"assets/textures/projectiles/evilfactionarrowstreak.png",
		"data/projectile-art.json",
		"pack.json",
	]:
		DirAccess.remove_absolute(fixture_root.path_join(relative))
	for relative in ["assets/textures/projectiles", "assets/textures", "assets", "data"]:
		DirAccess.remove_absolute(fixture_root.path_join(relative))
	DirAccess.remove_absolute(fixture_root)
	fixture_root = ""


func _has_diagnostic(rows: Array, code: String) -> bool:
	for row_value in rows:
		if typeof(row_value) == TYPE_DICTIONARY and String((row_value as Dictionary).get("code", "")) == code:
			return true
	return false


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
		print("RETAIL_ARCHER_PROJECTILE_PRESENTATION PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_ARCHER_PROJECTILE_PRESENTATION FAIL %s" % name)


func _finish() -> void:
	# Liveness guard. A GDScript runtime error inside an awaited section
	# unwinds it silently, so a half-run suite would otherwise print a green
	# RESULT with fewer checks (measured: 25 of 33 on the pre-fix runtime).
	if passed + failed != EXPECTED_CHECK_COUNT:
		failed += 1
		printerr(
			"RETAIL_ARCHER_PROJECTILE_PRESENTATION FAIL every_check_ran expected=%d actual=%d"
			% [EXPECTED_CHECK_COUNT, passed + failed - 1]
		)
	print("RETAIL_ARCHER_PROJECTILE_PRESENTATION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
