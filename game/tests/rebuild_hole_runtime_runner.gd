extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=19
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();var lair_id:=sim.spawn_scenario_structure("NeutralLair",Sim.CREEP_TEAM,Vector2(4,5),"map-placement");_check("lair_spawned",lair_id>0)
	var lair:=sim.structures[lair_id] as Dictionary;_check("expose_contract_attached",lair.has("rebuild_hole_expose"))
	var expose:=lair.get("rebuild_hole_expose",{}) as Dictionary;_check("fade_in_ticks_exact",int(expose.get("fade_in_ticks",0))==20);_check("fade_in_presentation_receipted",(expose.get("unsupported_semantics",[]) as Array).has("presentation_binding:FadeInTimeSeconds"));_check("canonical_transfer_attackers_no_is_executable",not (expose.get("unsupported_semantics",[]) as Array).has("unsupported_rebuild_semantic:TransferAttackers"))
	var hole_id:=sim._expose_rebuild_hole(lair_id,lair,77);_check("hole_exposed",hole_id>0 and sim.structures.has(hole_id))
	var hole:=sim.structures[hole_id] as Dictionary
	_check("hole_health_overridden",int(hole.get("health",0))==500 and int(hole.get("maximum_health",0))==500)
	_check("rebuild_contract_attached",hole.has("rebuild_hole_behavior"))
	_check("worker_object_name_consumed",String((hole.get("rebuild_hole_behavior",{}) as Dictionary).get("worker_object_id",""))=="Worker_Test")
	_check("owner_link_exact",String(hole.get("rebuild_owner_object_id",""))=="NeutralLair")
	_check("attacker_not_transferred_when_authored_no",int(hole.get("rebuild_transfer_attacker_id",0))==0)
	hole["health"]=400
	var before_tick:=sim.tick_index
	sim.tick();sim.winner=-1;_check("regen_remainder_is_per_frame",int(hole.get("health",0))==400 and is_equal_approx(float((hole.get("rebuild_hole_behavior",{}) as Dictionary).get("regen_remainder",0.0)),0.25));sim.tick();sim.winner=-1;sim.tick();sim.winner=-1;sim.tick();sim.winner=-1;_check("regen_completes_exact_fraction",int(hole.get("health",0))==401);_check("respawn_not_early",sim.structures.has(hole_id))
	for unused in 6:sim.tick();sim.winner=-1
	_check("respawn_exact_delay",not sim.structures.has(hole_id))
	var rebuilt_id:=-1
	for id in sim.structure_ids():
		if id!=lair_id and String((sim.structures[id] as Dictionary).get("source_object_id",""))=="NeutralLair":rebuilt_id=id
	_check("lair_rebuilt",rebuilt_id>0)
	_check("delay_ticks_exact",sim.tick_index-before_tick==10)
	var rebuilt_event:Dictionary={};for event_value in sim.events:
		var event:=event_value as Dictionary
		if String(event.get("kind",""))=="rebuild_hole.rebuilt":rebuilt_event=event
	_check("completion_carries_worker_object",String(rebuilt_event.get("worker_object_id",""))=="Worker_Test")
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_hash_restore",restored.restore(snap) and restored.state_hash()==hash)
	print("REBUILD_HOLE_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
func _sim()->RetailSliceSim:
	var lair:=_structure("NeutralLair","lair",["map-placement","lair-spawn"],777,[_expose()]);var hole:=_structure("NeutralHole","neutral-structure",["object-creation-list"],100,[_rebuild()]);var sim:RetailSliceSim=Sim.new();sim.setup({}, {"game":"rotwk","spawn_initial_battalions":false,"scenario_structure_runtimes":{"NeutralLair":lair,"NeutralHole":hole}});sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear();return sim
func _structure(id:String,role:String,surfaces:Array,health:int,contracts:Array)->Dictionary:return {"schema":"openbfme.playable-structure-runtime","objectId":id,"registration":{"production":{"evidence":"authored-neutral-map","routes":[]},"scenarioAdmission":{"kind":"authored-neutral-non-buildable","role":role,"surfaces":surfaces,"buildCommandExposed":false},"gameplay":{"health":{"primary":{"module":"ActiveBody","maxHealth":{"authored":str(health),"value":health}}},"scalarFields":{},"moduleContracts":contracts},"presentation":{"buildingLifecycle":{"simulationFacts":{"maximumHealth":health}}}}}
func _expose()->Dictionary:return {"module":"RebuildHoleExposeDie","extraction":"typed","runtimeStatus":"executable","fields":{"ExemptStatus":{"value":["SOLD"]},"HoleName":{"value":"NeutralHole"},"HoleMaxHealth":{"value":500.0},"FadeInTimeSeconds":{"value":2.0},"TransferAttackers":{"value":false}}}
func _rebuild()->Dictionary:return {"module":"RebuildHoleBehavior","extraction":"typed","runtimeStatus":"executable","fields":{"WorkerObjectName":{"value":"Worker_Test"},"WorkerRespawnDelay":{"milliseconds":1000},"HoleHealthRegen%PerSecond":{"ratio":0.005,"percent":0.5}}}
func _check(label:String,ok:bool)->void:
	if ok:passed+=1
	else:failed+=1;push_error("REBUILD_HOLE_RUNTIME_FAIL "+label)
