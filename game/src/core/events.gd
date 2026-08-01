extends Node
## Global signal bus. Gameplay never connects view→sim directly.

const BootProfile = preload("res://src/core/boot_profile.gd")

signal content_reloaded
signal match_started
signal match_ended(winner_side: int)
signal selection_changed(ids: Array)
signal resources_changed(side: int, amount: float)
signal toast(message: String)
signal entity_spawned(kind: StringName, id: int)
signal entity_died(kind: StringName, id: int)
signal building_completed(id: int)
signal ability_used(battalion_id: int, ability_id: StringName)
signal fog_dirty


func _ready() -> void:
	# FIRST autoload in project.godot order, so this is the first autoload _ready
	# in the whole boot. The stage it closes is NOT what its old name suggested:
	# Godot loads the MAIN SCENE RESOURCE between instantiating the autoloads and
	# adding them to the tree, so this delta is dominated by that scene's script
	# compilation, not by engine or driver work.
	#
	# That was the whole diagnosis. With scenes/boot.tscn as the main scene this
	# delta measured 2,816 ms; with the one-node scenes/startup_boot.tscn it is
	# ~15 ms. Godot's own floor, measured against an empty project on this
	# machine, is ~213 ms and is the only part nobody here can shorten.
	BootProfile.mark("main_scene_resource_load")

## Boot instrumentation. Godot loads and instantiates autoloads one at a time in
## project.godot order, so this _init fires immediately after this script (and
## its preload closure) finished compiling. The gap between consecutive
## `autoload_compiled:*` marks is therefore that autoload's own load+compile
## cost - the only way to attribute the pre-_ready block without guessing, and
## how it was established that all six autoloads together cost ~600 ms while the
## main scene's preload chain cost 2,816 ms. No-op unless boot profiling is on.
func _init() -> void:
	BootProfile.mark("autoload_compiled:Events")
