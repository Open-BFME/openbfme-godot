## BFME1 saves must not consult RotWK's gi.dat or Create-a-Hero folder.
extends SceneTree

class Filesystem:
	extends RefCounted
	func is_bfme1() -> bool:
		return true

class Shell:
	extends RefCounted
	var calls := 0
	func create_a_hero_save_folder(_install: String) -> Dictionary:
		calls += 1
		return {"ok": false, "error": "BFME1 must not enter CAH"}

class Game:
	extends Node
	var _save_dir_arg := ""
	var _rotwk_install := ""
	var _fs := Filesystem.new()
	var _shell := Shell.new()

func _initialize() -> void:
	var game := Game.new()
	var flow = load("res://scripts/save_flow.gd").new(game)
	assert(flow.save_dir() == OS.get_user_data_dir().path_join("bfme1/Save"))
	assert(flow.save_dir() == OS.get_user_data_dir().path_join("bfme1/Save"))
	assert(game._shell.calls == 0)
	game._save_dir_arg = OS.get_user_data_dir().path_join("bfme1/test-save-override")
	assert(flow.save_dir() == game._save_dir_arg)
	assert(game._shell.calls == 0)
	DirAccess.remove_absolute(game._save_dir_arg)
	game.free()
	var host = load("res://scripts/game.gd").new()
	host._fs = Filesystem.new()
	host._cah_profile = "explicit-test-profile"
	assert(host._cah_profile_dir().is_empty())
	host._cah_begin()
	assert(host._cah == null)
	host._pending_menu_style = "bfme1"
	host.request_menu_style("rotwk")
	assert(host._pending_menu_style == "bfme1")
	host.free()
	print("BFME1 save folder: PASS")
	quit()
