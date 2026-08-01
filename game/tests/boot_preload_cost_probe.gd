extends SceneTree
## Diagnostic probe (not an assertion runner): times ResourceLoader.load of each
## script main_menu.gd preloads, so the boot report can attribute the main-scene
## load stage to individual scripts instead of guessing. Each load is timed cold
## in its own dependency order; later entries are cheaper because shared
## dependencies are already cached, which is exactly the number we want (the
## marginal cost of keeping that preload on the boot path).

const CANDIDATES: Array[String] = [
	"res://src/ui/openbfme_theme.gd",
	"res://src/ui/openbfme_nav_diamonds.gd",
	"res://src/ui/openbfme_shell_flyout.gd",
	"res://src/ui/retail_shell_apt_runtime.gd",
	"res://src/retail_slice/retail_vertical_slice.gd",
	"res://src/retail_slice/retail_faction_manifest.gd",
	"res://src/content/pack_capability.gd",
	"res://src/ui/multiplayer_lobby.gd",
	"res://src/retail_slice/retail_lockstep_session.gd",
	"res://src/ui/wotr_screen.gd",
	"res://src/ui/wotr_setup_screen.gd",
	"res://src/wotr/wotr_session.gd",
	"res://src/wotr/wotr_state.gd",
	"res://src/wotr/wotr_battle.gd",
	"res://src/ui/openbfme_menu_backdrop.gd",
	"res://src/ui/options_screen.gd",
	"res://src/ui/skirmish_setup.gd",
	"res://src/ui/stats_screen.gd",
	"res://src/ui/multiplayer_flyout.gd",
]


func _init() -> void:
	var total := 0
	for path in CANDIDATES:
		var mark := Time.get_ticks_msec()
		var res := ResourceLoader.load(path)
		var cost := Time.get_ticks_msec() - mark
		total += cost
		print("PRELOAD_COST %-6d ms  %s%s" % [cost, path, "" if res != null else "   <FAILED>"])
	print("PRELOAD_COST TOTAL %d ms" % total)
	quit()
