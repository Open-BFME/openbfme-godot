extends SceneTree
var requests: Array = []
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	assert(setup.ok, str(setup))
	var campaigns := {}
	for campaign in world.get_campaigns(): campaigns[campaign.name] = campaign
	assert(campaigns.GOOD_CAMPAIGN.missions.size() == 8)
	assert(campaigns.EVIL_CAMPAIGN.missions.size() == 8)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {}).ok)
	shell.shell_request.connect(func(action: String, argument: String): requests.append([action, argument]))
	assert(shell.shell_push("MainMenu.apt"))
	for i in 80: shell.tick(0.03)
	for pair in [["GoodCampaign", "E", "GOOD_CAMPAIGN", "MAP GOOD Rivendell"], ["EvilCampaign", "H", "EVIL_CAMPAIGN", "MAP EVIL Lorien"]]:
		assert(shell.shell_fscommand("AptMainMenu::" + pair[0], pair[1]))
		shell.tick(0.03)
		assert(requests.back() == [pair[0], pair[1]], str(requests))
		assert(campaigns[pair[2]].missions[0].map == pair[3])
		var report: Dictionary = world.start_campaign_mission({"campaign":pair[2], "mission":0, "difficulty":0, "logic_thread":false})
		assert(report.ok, str(report.get("errors", [])))
		for i in 4: world.advance(0.2)
		print("BFME2 CAMPAIGN LOADED ", pair[2], " ", pair[3])
		world.clear_game_data(false)
	assert(load("res://scripts/game.gd") != null)
	print("BFME2 CAMPAIGN MENU PASS")
	quit()
