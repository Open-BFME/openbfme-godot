extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=8
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();var owner:=sim.structures[50] as Dictionary
	sim._attach_spawn_behavior_contract(owner,{"module":"SpawnBehavior","extraction":"typed","fields":{"SpawnNumber":{"value":1},"InitialBurst":{"value":1},"SpawnReplaceDelay":{"milliseconds":200},"SpawnTemplateName":{"value":["NeutralWolf"]},"SpawnedRequireSpawner":{"value":true}}})
	sim._step_spawn_behaviors();var ids:=(owner["spawn_behavior"] as Dictionary).get("spawned_ids",[]) as Array
	_check("descriptor_child_spawned",ids.size()==1)
	if ids.is_empty():print("SPAWN_BEHAVIOR_SCENARIO_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(1);return
	var child:=sim.entities[int(ids[0])] as Dictionary
	_check("lair_surface_recorded",String(child.get("scenario_spawn_surface",""))=="lair-spawn")
	_check("descriptor_health_used",int(child.get("member_maximum_health",0))==321)
	_check("not_inserted_into_production",not (sim._rules.get("unit_rules",{}) as Dictionary).has("NeutralWolf"))
	_check("parent_bound",int(child.get("spawn_behavior_parent_id",0))==50)
	_check("no_unresolved_receipt",not ((owner["spawn_behavior"] as Dictionary).get("unsupported_semantics",[]) as Array).has("unresolved_spawn_template:NeutralWolf"))
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restore",restored.restore(snap));_check("hash_restore",restored.state_hash()==hash)
	print("SPAWN_BEHAVIOR_SCENARIO_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
func _sim()->RetailSliceSim:
	var sim:RetailSliceSim=Sim.new();sim.setup({}, {"game":"rotwk","spawn_initial_battalions":false,"source_map_transform_scale":0.1,"scenario_unit_runtimes":{"NeutralWolf":_unit_document()}});sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear();sim.structures[50]={"id":50,"team":Sim.PLAYER_TEAM,"health":100,"position":Vector2(4,5),"completed_upgrades":[]};return sim
func _unit_document()->Dictionary:return {"objectId":"NeutralWolf","category":"monster","registration":{"production":[],"composition":{"containerObjectId":"NeutralWolf","primaryMemberObjectId":"NeutralWolf"},"scenarioAdmission":{"kind":"authored-non-buildable","role":"creature","surfaces":["lair-spawn"],"buildCommandExposed":false},"presentation":{},"simulation":{"displayName":"Neutral Wolf","buildCost":0,"buildTimeSeconds":1.0,"commandPoints":1,"memberCount":1,"memberHealth":321,"speed":90.0,"visionRange":200.0,"movement":{"acceleration":90.0,"braking":90.0,"turnRateDegreesPerSecond":360.0},"formation":{"positions":[{"x":0.0,"y":0.0}]},"combat":{"attackRange":20.0,"delayBetweenShotsMs":1500.0,"preAttackDelayMs":500.0,"firingDurationMs":500.0,"damage":45}}}}
func _check(label:String,ok:bool)->void:
	if ok:passed+=1
	else:failed+=1;push_error("SPAWN_BEHAVIOR_SCENARIO_RUNTIME_FAIL "+label)
