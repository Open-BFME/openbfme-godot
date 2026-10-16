extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var lua = ClassDB.instantiate("LuaState")
	lua.open_libraries()
	var mode: String = OS.get_cmdline_user_args()[0]
	var code: String = {
		"callable": "local s=ResourceLoader:load('res://scripts/wall_balance_options.gd'); local t=TabContainer:new(); Callable(s,'build'):call(t); return t:get_tab_count()",
		"instance": "local s=ResourceLoader:load('res://scripts/wall_balance_options.gd'); local t=TabContainer:new(); s:new():build(t); return t:get_tab_count()",
		"callv": "local s=ResourceLoader:load('res://scripts/wall_balance_options.gd'); local t=TabContainer:new(); s:callv('build', Array{t}); return t:get_tab_count()",
		"call": "local s=ResourceLoader:load('res://scripts/wall_balance_options.gd'); local t=TabContainer:new(); s:call('build', t); return t:get_tab_count()",
		"objcall": "local t=TabContainer:new(); t:call('set_tab_title', 0, 'x'); local n=Node:new(); n:call('set_name', 'abc'); return n.name",
	}[mode]
	print("RESULT ", mode, " ", lua.do_string(code))
	quit()
