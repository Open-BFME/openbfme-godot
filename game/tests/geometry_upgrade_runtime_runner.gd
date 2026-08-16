extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 28
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [
		_contract("Geom_Any", ["Upgrade_A", "Upgrade_B"], false, ["Upgrade_Block"], ["Armor", "Banner*"], ["Base"], true),
		_contract("Geom_All", ["Upgrade_A", "Upgrade_B"], true, [], ["All_On"], ["All_Off"], false),
	]
	_spawn(sim, 1, Sim.PLAYER_TEAM)
	_spawn(sim, 90, Sim.ENEMY_TEAM)
	var row := sim.entities[1] as Dictionary
	_check("typed_contracts_attach", (row.get("geometry_upgrades", []) as Array).size() == 2)
	_check("no_trigger_has_empty_authoritative_state", (row.get("geometry_visibility", {}) as Dictionary).is_empty())
	_check("deferred_mesh_and_animation_fields_are_receipted", _receipts(row).size() == 4)
	sim.set_entity_upgrade_state(1, "Upgrade_A", true)
	row = sim.entities[1] as Dictionary
	_check("any_trigger_activates", _policy_active(row, "Geom_Any"))
	_check("show_tokens_preserve_authored_spelling", (row["geometry_visibility"] as Dictionary).get("show", []) == ["Armor", "Banner*"])
	_check("hide_tokens_preserve_authored_spelling", (row["geometry_visibility"] as Dictionary).get("hide", []) == ["Base"])
	_check("requires_all_waits", not _policy_active(row, "Geom_All"))
	sim.set_entity_upgrade_state(1, "upgrade_b", true)
	_check("case_insensitive_all_mux_activates", _policy_active(sim.entities[1] as Dictionary, "Geom_All"))
	_check("active_rows_merge_deterministically", (sim.entities[1]["geometry_visibility"] as Dictionary).get("show", []) == ["All_On", "Armor", "Banner*"])
	var hash := sim.state_hash(); var snap := sim.snapshot(); var restored := _sim()
	_check("snapshot_restores", restored.restore(snap))
	_check("geometry_state_hash_round_trips", restored.state_hash() == hash)
	restored.tick(); sim.tick()
	_check("restored_lifecycle_steps_identically", restored.state_hash() == sim.state_hash())
	sim.set_entity_upgrade_state(1, "Upgrade_Block", true)
	row = sim.entities[1] as Dictionary
	_check("conflict_deactivates_matching_policy", not _policy_active(row, "Geom_Any"))
	_check("conflict_leaves_independent_policy", (row["geometry_visibility"] as Dictionary).get("show", []) == ["All_On"])
	sim.set_entity_upgrade_state(1, "Upgrade_A", false)
	_check("trigger_removal_deactivates_requires_all", not _policy_active(sim.entities[1] as Dictionary, "Geom_All"))
	_check("all_inactive_erases_visibility_state", not (sim.entities[1] as Dictionary).has("geometry_visibility"))
	sim.set_entity_upgrade_state(1, "Upgrade_Block", false); sim.set_entity_upgrade_state(1, "Upgrade_B", false)
	sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", true)
	_check("team_upgrade_activates_owned_geometry", _policy_active(sim.entities[1] as Dictionary, "Geom_Any"))
	sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", false)
	_check("team_upgrade_removal_clears_geometry", not (sim.entities[1] as Dictionary).has("geometry_visibility"))

	var opaque := _sim(); var c := _contract("Opaque", ["Upgrade_A"], false, [], ["X"], [], false); c["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [c]; _spawn(opaque, 2, Sim.PLAYER_TEAM)
	_check("opaque_contract_fails_closed", not (opaque.entities[2] as Dictionary).has("geometry_upgrades"))
	var structure_sim := _sim(); _spawn(structure_sim, 10, Sim.PLAYER_TEAM); _spawn(structure_sim, 91, Sim.ENEMY_TEAM)
	structure_sim.register_structure_module_contracts("FixtureStructure", [_contract("Geom_Structure", ["Upgrade_A"], false, [], ["ROOF"], ["FOUNDATION"], false)])
	structure_sim.structures[700] = {"id":700,"team":Sim.PLAYER_TEAM,"source_object_id":"FixtureStructure","structure_kind":"fixture","health":100,"maximum_health":100,"completed_upgrades":["Upgrade_A"],"position":Vector2.ZERO,"queue":[],"upgrade_queue":[]}
	structure_sim.tick(); var structure_row := structure_sim.structures[700] as Dictionary
	_check("structure_contract_attaches_to_authoritative_row", (structure_row.get("geometry_upgrades",[]) as Array).size()==1)
	_check("structure_upgrade_activates_visibility_state", (structure_row.get("geometry_visibility",{}) as Dictionary).get("show",[])==["ROOF"])
	structure_row["completed_upgrades"]=[]; structure_sim.tick(); structure_row=structure_sim.structures[700] as Dictionary
	_check("structure_upgrade_removal_clears_state", not structure_row.has("geometry_visibility"))

	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd") as GDScript
	var presenter = structure_script.new(); var root := Node3D.new(); root.name = "Root"
	var armor := Node3D.new(); armor.name = "Armor"; root.add_child(armor)
	var banner_a := Node3D.new(); banner_a.name = "Banner01"; root.add_child(banner_a)
	var banner_b := Node3D.new(); banner_b.name = "BannerPole"; root.add_child(banner_b)
	var base := Node3D.new(); base.name = "Base"; root.add_child(base)
	presenter._active_body = root
	var visual_result: Dictionary = presenter.apply_geometry_upgrade_visibility({"show": ["ARMOR", "BANNER*"], "hide": ["BASE"]})
	_check("structure_seam_shows_exact_subobject", armor.visible)
	_check("structure_seam_applies_retail_prefix_wildcard", banner_a.visible and banner_b.visible)
	_check("structure_seam_hides_exact_subobject", not base.visible)
	_check("structure_seam_reports_matches", int(visual_result.get("matched_count", 0)) == 4)
	var missing: Dictionary = presenter.apply_geometry_upgrade_visibility({"show": ["DOES_NOT_EXIST"], "hide": []})
	_check("structure_seam_reports_unresolved_token", (missing.get("unmatched_tokens", []) as Array) == ["DOES_NOT_EXIST"])
	presenter.apply_geometry_upgrade_visibility({})
	_check("structure_seam_restores_model_baseline_on_removal", armor.visible and banner_a.visible and banner_b.visible and base.visible)
	presenter.free(); root.free()

	var ran := passed + failed
	if ran != EXPECTED_CHECKS: failed += 1; printerr("GEOMETRY_UPGRADE_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("GEOMETRY_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(tag:String,triggers:Array,requires_all:bool,conflicts:Array,show:Array,hide:Array,deferred:bool)->Dictionary:
	var fields := {"TriggeredBy":{"value":triggers.duplicate()},"ShowGeometry":{"value":show.duplicate()},"HideGeometry":{"value":hide.duplicate()},"ConflictsWith":{"value":conflicts.duplicate()},"RequiresAllTriggers":{"value":requires_all}}
	if deferred: fields["deferredFields"]=[{"name":"CustomAnimAndDuration","authored":"USER_1 500"},{"name":"WallBoundsMesh","authored":"P1"},{"name":"RampMesh1","authored":"R1"},{"name":"RampMesh2","authored":"R2"}]
	return {"module":"GeometryUpgrade","runtimeStatus":"deferred" if deferred else "executable","extraction":"typed","tag":tag,"sourceIni":"data/ini/object/fixture.ini","line":10 if tag=="Geom_Any" else 20,"fields":fields}

func _sim()->RetailSliceSim:
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[id]=_rule()
	var sim:RetailSliceSim=Sim.new(); sim.setup({}, {"unit_rules":rules}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); return sim
func _spawn(sim:RetailSliceSim,id:int,team:int)->void: sim._add_battalion(id,team,Vector2.ZERO,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _rule()->Dictionary: return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _policy_active(row:Dictionary,tag:String)->bool:
	for v in row.get("geometry_upgrades",[]) as Array:
		if String((v as Dictionary).get("tag",""))==tag:return bool((v as Dictionary).get("active",false))
	return false
func _receipts(row:Dictionary)->Array:
	var out:Array=[]; for v in row.get("geometry_upgrades",[]) as Array: out.append_array((v as Dictionary).get("unsupported_semantics",[]) as Array)
	return out
func _check(label:String,condition:bool)->void:
	if condition:passed+=1
	else:failed+=1;push_error("GEOMETRY_UPGRADE_RUNTIME_FAIL %s"%label)
