extends SceneTree
# Runtime fixture: original BFME1 assets, including an installation with patch maps mounted.
# No extracted assets are stored in the repository.
func _initialize() -> void:
	call_deferred("_run")

func tick(shell, count: int = 3) -> void:
	for i in count:
		shell.tick(0.033)

func gadget(shell, name: String) -> Dictionary:
	for item in shell.list_gadgets():
		if item.name == name and not item.hidden:
			return item
	assert(false, "Missing gadget: " + name)
	return {}

func click(shell, position: Vector2) -> void:
	shell.post_mouse_move_stage(position)
	shell.post_mouse_button(true)
	tick(shell, 1)
	shell.post_mouse_button(false)
	tick(shell)

func choose(shell, name: String, row: int) -> void:
	var item = gadget(shell, name)
	click(shell, Vector2(item.x + item.w - 10, item.y + 12))
	# Native combo: 26 pixel edit area followed by 20 pixel list rows.
	click(shell, Vector2(item.x + 25, item.y + 26 + row * 20 + 10))

func _run() -> void:
	var install = OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIRMISH MENU SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var folder = OS.get_temp_dir().path_join("openbfme-menu-" + str(Time.get_ticks_usec()))
	assert(DirAccess.make_dir_recursive_absolute(folder) == OK)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini": folder.path_join("Skirmish.ini")}).ok)
	assert(shell.shell_push("MainMenu.apt"))
	tick(shell, 90)
	assert(shell.shell_fscommand("AptMainMenu::Skirmish", ""))
	tick(shell, 120)
	assert(shell.lobby_apply({"profile": "Menu A"}).ok)
	tick(shell, 90)
	var catalog = shell.lobby_map_catalog()
	var maps = catalog.filter(func(map): return map.official)
	assert(maps.size() == 40, "Original BFME1 retail multiplayer catalog")
	var names: Array = []
	for map in maps:
		assert(map.official)
		assert(not map.name.begins_with("$"))
		assert(not map.name.begins_with("Map:"))
		names.append(map.name)
	assert(names.has("Fords of Isen"))
	assert(names.has("Eastern Rohan"))
	assert(not names.has("Andrast II"), "Exclude 2.22 additions")
	assert(gadget(shell, "MapList").entries == 40)
	assert(gadget(shell, "MpGameSetup::MapType").entries == 2)
	assert(gadget(shell, "Skirmish::PlayerProfile").entries == 3)
	assert(gadget(shell, "Skirmish::PlayerProfile").text == "Menu A")
	var list = gadget(shell, "MapList")
	shell.post_mouse_move_stage(Vector2(list.x + 30, list.y + 30))
	shell.post_mouse_wheel(10)
	tick(shell)
	var top = gadget(shell, "MapList").display_pos
	shell.post_mouse_wheel(-10)
	tick(shell)
	assert(gadget(shell, "MapList").display_pos > top) # tolerance: scrolling changes the offset; font metrics determine its size
	# Drag the thumb from the bottom toward the top of the native vertical slider.
	var before_drag = gadget(shell, "MapList").display_pos
	shell.post_mouse_move_stage(Vector2(list.x + list.w - 11, list.y + list.h - 35))
	shell.post_mouse_button(true)
	tick(shell, 1)
	shell.post_mouse_move_stage(Vector2(list.x + list.w - 11, list.y + 35))
	tick(shell, 1)
	shell.post_mouse_button(false)
	tick(shell)
	# tolerance: the drag must move upward; exact pixels depend on font metrics.
	assert(gadget(shell, "MapList").display_pos < before_drag)
	choose(shell, "MpGameSetup::MapType", 1)
	assert(gadget(shell, "MpGameSetup::MapType").selected == 1)
	assert(gadget(shell, "MapList").entries == catalog.size() - maps.size())
	choose(shell, "MpGameSetup::MapType", 0)
	assert(gadget(shell, "MapList").entries == 40)
	choose(shell, "Skirmish::PlayerProfile", 1) # New Profile
	tick(shell, 30)
	assert(shell.lobby_apply({"profile": "Menu B"}).ok)
	tick(shell, 90)
	assert(gadget(shell, "Skirmish::PlayerProfile").text == "Menu B")
	assert(gadget(shell, "Skirmish::PlayerProfile").entries == 4)
	choose(shell, "Skirmish::PlayerProfile", 0)
	assert(gadget(shell, "Skirmish::PlayerProfile").text == "Menu A")
	choose(shell, "Skirmish::PlayerProfile", 3) # Delete Profile
	tick(shell, 30)
	assert(shell.shell_fscommand("AptSkirmish::Skirmish::PersonaRemove", ""))
	tick(shell, 60)
	assert(gadget(shell, "Skirmish::PlayerProfile").text == "Menu B")
	assert(gadget(shell, "Skirmish::PlayerProfile").entries == 3)
	choose(shell, "0/PlayerTemplate", 1)
	tick(shell, 60)
	var report = shell.get_shell_report()
	assert(report.errors.is_empty(), str(report.errors))
	for note in report.note_lines:
		assert(not "extern-read-unhandled: LevelBar" in note, note)
		assert(not "profile-stats:" in note, note)
	print("BFME1 SKIRMISH MENU PASS: original maps, names, filters, scrolling and profile actions")
	quit(0)
