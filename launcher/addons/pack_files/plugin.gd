@tool
extends EditorPlugin
## Lane REPRO-1: registers export_plugin.gd for the exports build_launcher.sh runs (godot --headless --export-release); editor only, never
## exported (export_presets.cfg exclude_filter "addons/*").

var _export: EditorExportPlugin


func _enter_tree() -> void:
	_export = preload("res://addons/pack_files/export_plugin.gd").new()
	add_export_plugin(_export)


func _exit_tree() -> void:
	remove_export_plugin(_export)
	_export = null
