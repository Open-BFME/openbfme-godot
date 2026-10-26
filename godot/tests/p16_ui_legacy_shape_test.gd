## P16 preserves the eight-wide legacy UI reports. No server or game startup required.
extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	print("PASS " if ok else "FAIL ", what)
	if not ok:
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("P16_UI_LEGACY_SHAPE SKIP: retail installations required")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	check(mounted.get("ok", false), "retail mount")
	if failures:
		quit(1)
		return
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	check(setup.get("ok", false), "world setup")
	var menu: Node = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(menu)
	var boot: Dictionary = menu.boot_shell(fs, world, {})
	check(boot.get("ok", false), "shell setup")
	if failures:
		quit(1)
		return
	menu.shell_push("Skirmish.apt")
	for i in 90:
		await process_frame
	var lobby: Dictionary = menu.lobby_state()
	check(lobby.get("found", false), "skirmish lobby exists")
	check(lobby.get("slots", []).size() == 8, "legacy lobby report stays eight-wide")
	menu.shell_pop()
	var online: Dictionary = menu.set_online(world, {})
	check(online.get("ok", false), "online client configured without a transport")
	menu.shell_push("OnlineShell.apt")
	for i in 90:
		await process_frame
	var state: Dictionary = menu.online_status()
	check(state.get("screen", false), "online shell exists")
	for field in ["ready", "start_pos", "ready_icons"]:
		check(state.get(field, []).size() == 8, "legacy online %s stays eight-wide" % field)
	print("P16_UI_LEGACY_SHAPE ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
