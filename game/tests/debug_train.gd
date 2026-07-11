extends SceneTree

func _initialize() -> void:
	call_deferred("r")

func r() -> void:
	if ContentDB.units.is_empty():
		ContentDB.reload()
	GameState.reset_match()
	var w = load("res://src/sim/sim_world.gd").new(1)
	GameState.world = w
	GameState.resources[0] = 5000.0
	w.spawn_building("g_fortress", 0, Vector2(0, 0), true, "gondor")
	var bar = w.spawn_building("barracks", 0, Vector2(0, 20), true, "gondor")
	print("barracks built=", bar.built, " id=", bar.id, " type=", bar.type_id)
	var bdef = ContentDB.get_building("barracks")
	print("trains=", bdef.get("trains", []))
	print("enqueue=", w.enqueue_train(bar.id, "soldier"))
	print("queue=", bar.train_queue, " res=", GameState.resources[0])
	print("before count=", w.count_battalions(0))
	for i in 100:
		w.tick(0.1)
		if i % 20 == 0:
			print("t=", i, " queue=", bar.train_queue, " count=", w.count_battalions(0))
	print("after count=", w.count_battalions(0), " stats=", GameState.stats)
	quit(0)
