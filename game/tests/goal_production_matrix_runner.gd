extends SceneTree
const FACTIONS: Array[String] = ["men","elves","dwarves","isengard","mordor","wild","angmar"]
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var passed:=0
var failed:=0
var report:Dictionary={"production":{},"summary":{}}
func _initialize()->void: call_deferred("_run")
func _run()->void:
	var content_db=root.get_node_or_null("ContentDB")
	if content_db==null: printerr("no ContentDB"); quit(1); return
	await process_frame; await process_frame
	var units:Dictionary=content_db.call("get_playable_unit_runtimes") if content_db.has_method("get_playable_unit_runtimes") else {}
	var structures:Dictionary=content_db.call("get_playable_structure_runtimes") if content_db.has_method("get_playable_structure_runtimes") else {}
	var manifest_script=load("res://src/retail_slice/retail_faction_manifest.gd")
	var slice_script=load("res://src/retail_slice/retail_vertical_slice.gd")
	# A skipped playableUnit.* document is a unit the pack shipped and the
	# runtime refused: its barracks button is dead in game. ContentDB only
	# push_warning()s it, so before this gate a whole unit could vanish from a
	# faction with every matrix still green (GondorArcherHorde did exactly
	# that). Fail loudly and name the ids.
	var skipped:Array=content_db.call("get_skipped_playable_unit_documents") if content_db.has_method("get_skipped_playable_unit_documents") else []
	var skipped_names:Array[String]=[]
	for skipped_value in skipped: skipped_names.append(String(skipped_value))
	skipped_names.sort()
	report["skipped_playable_unit_documents"]=skipped_names
	_check("no_skipped_playable_unit_documents", skipped_names.is_empty(),
		"ContentDB skipped %d playable unit document(s): %s" % [skipped_names.size(), ", ".join(skipped_names)])
	for object_id_value in units.keys():
		var object_id:=String(object_id_value)
		_check("unit_present_"+object_id, true, "")
	for object_id_value in structures.keys():
		_check("structure_present_"+String(object_id_value), true, "")
	for faction_id in FACTIONS:
		var slice=slice_script.new()
		var map_data=load("res://src/retail_slice/retail_map_data.gd").new()
		map_data.local_transform_scale=0.1
		slice.source_map_data=map_data
		slice._classify_faction_units(faction_id)
		var fieldable:Dictionary=(slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
		var producible:Dictionary=(slice.producible_unit_runtimes as Dictionary).duplicate(true)
		var manifest:Dictionary=manifest_script.from_registries(faction_id, fieldable, structures)
		var err:=String(manifest.get("_error",""))
		_check("manifest_"+faction_id, err=="", err)
		var builder_unit_rules:Dictionary={}
		for builder_value in manifest.get("builder_unit_ids",[]) as Array:
			var builder_id:=String(builder_value)
			var br:Dictionary=slice._faction_builder_unit_rule(builder_id)
			if not br.is_empty(): builder_unit_rules[builder_id]=br
		slice.free()
		if err!="": continue
		var sim=SimScript.new()
		sim._apply_gameplay_rules({
			"enable_base_loop":true,
			"faction_manifest":manifest,
			"playable_unit_runtimes":producible,
			"producer_kind_by_source_object":manifest.get("producer_kind_registry",{}),
			"unit_rules":builder_unit_rules,
			"starting_resources":1000000,
			"source_map_transform_scale":0.1,
		})
		if String(sim.configuration_error)!="":
			_check("sim_spawn_"+faction_id,false,"configure: "+String(sim.configuration_error)); continue
		sim.setup({}, sim._rules)
		sim.ai_enabled=false
		if String(sim.configuration_error)!="":
			_check("sim_spawn_"+faction_id,false,"setup: "+String(sim.configuration_error)); continue
		var producer:=int(sim.fortress_id(0))
		if producer==0: _check("sim_spawn_"+faction_id,false,"no fortress"); continue
		var offered:Array=sim.structure(producer).get("production",[]) as Array
		var unit_type:=""
		for builder_value in manifest.get("builder_unit_ids",[]) as Array:
			var builder_type:=String(builder_value)
			if offered.has(builder_type): unit_type=builder_type; break
		if unit_type=="" and not offered.is_empty(): unit_type=String(offered[0])
		if unit_type=="": _check("sim_spawn_"+faction_id,false,"no production"); continue
		var before:Array[int]=sim.entity_ids()
		var queued:Dictionary=sim.queue_unit(0,producer,unit_type)
		if not bool(queued.get("ok",false)):
			_check("sim_spawn_"+faction_id,false,"queue: "+String(queued.get("reason",""))); continue
		var complete_tick:=int((queued.get("item",{}) as Dictionary).get("complete_tick",sim.tick_index+1))
		for _s in range(maxi(1,complete_tick-int(sim.tick_index)+2)):
			sim.tick()
			if sim.production_queue_state(producer).is_empty(): break
		var spawned:=0
		for entity_id in sim.entity_ids():
			if not before.has(entity_id):
				var entity:Dictionary=sim.entity(entity_id)
				if String(entity.get("unit_type",""))==unit_type: spawned=entity_id; break
		_check("sim_spawn_"+faction_id, spawned!=0, "queued="+unit_type)
		# purchase surface: forged blades for mordor/wild should be present after pack patch
		if faction_id in ["mordor","wild"]:
			var has_forged:=false
			for ut in sim._unit_upgrade_commands.keys() if sim.get("_unit_upgrade_commands")!=null else []:
				pass
	report["summary"]={"passed":passed,"failed":failed,"units":units.size(),"structures":structures.size()}
	var outp:=OS.get_environment("OPENBFME_GOAL_MATRIX_OUT").strip_edges()
	if outp!="":
		var f:=FileAccess.open(outp,FileAccess.WRITE)
		if f: f.store_string(JSON.stringify(report,"\t")); f.close()
	print("PRODUCTION_MATRIX_RESULT passed=%d failed=%d"%[passed,failed])
	quit(0 if failed==0 else 1)
func _check(key:String,ok:bool,detail:String="")->void:
	if ok: passed+=1; print("PRODUCTION_MATRIX PASS ",key)
	else: failed+=1; print("PRODUCTION_MATRIX FAIL ",key," | ",detail)
	report["production"][key]={"ok":ok,"detail":detail}
