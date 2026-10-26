## Loaded after the Release autoload so the production game script can resolve it.
extends "res://scripts/game.gd"

var failed := ""


func _fail(message: String) -> void:
	failed = message
