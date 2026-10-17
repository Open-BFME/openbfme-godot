extends SceneTree
## Lane LUAGD-1: lua-gdextension (LuaJIT) compiled into the openbfme extension. Its classes are registered (LuaState, LuaTable, Lua as a
## script language), and the bfme.* API runs on a LuaState: GDScript reaches it through GameWorld.get_lua_state(), the console path
## (execute_lua) keeps its execution budget and its sandbox. Needs no game files. Exit codes: 0 pass, 1 fail.
var _failures := 0


func _initialize() -> void:
	call_deferred("_main")


func _main() -> void:
	_run()
	print("LUA GDEXTENSION %s" % ["PASS" if _failures == 0 else "FAIL"])
	quit(0 if _failures == 0 else 1)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> void:
	for cls in ["LuaState", "LuaTable", "LuaFunction", "LuaCoroutine", "LuaError", "LuaScript", "LuaParser"]:
		_check(ClassDB.class_exists(cls), "class %s registered" % cls)

	# a plain LuaState from GDScript, the addon's own usage
	var lua: RefCounted = ClassDB.instantiate("LuaState")
	lua.call("open_libraries")
	var r: Variant = lua.call("do_string", "local v = Vector2(3, 4); return { len = v:length(), jit = jit ~= nil, version = _VERSION }")
	_check(r != null and r.get_class() == "LuaTable", "do_string returns a LuaTable")
	if r != null and r.get_class() == "LuaTable":
		_check(is_equal_approx(float(r.get("len")), 5.0), "Godot Variant types in Lua (Vector2(3, 4):length() == %s)" % r.get("len"))
		_check(String(r.get("version")) == "Lua 5.1", "the runtime is LuaJIT (%s)" % r.get("version"))
	var err: Variant = lua.call("do_string", "error('boom')")
	_check(err != null and err.get_class() == "LuaError", "a Lua error is a LuaError")

	# A callback made in a temporary coroutine must keep a main-state reference.
	var callback: Variant = lua.call("do_string", "local co=coroutine.create(function() return Callable(function(value) return value+1 end) end); local ok,cb=coroutine.resume(co); assert(ok); return cb")
	lua.call("do_string", "collectgarbage('collect'); collectgarbage('collect')")
	_check(callback is Callable and callback.call(41) == 42, "Godot callback survives collection of its creating coroutine")
	# the bfme.* API on the addon's LuaState
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var state: RefCounted = world.call("get_lua_state")
	_check(state != null and state.get_class() == "LuaState", "GameWorld.get_lua_state() is a LuaState")
	if state != null:
		var globals: Variant = state.get("globals")
		var bfme: Variant = globals.get("bfme") if globals != null else null
		_check(bfme != null and bfme.get_class() in ["LuaTable", "LuaUserdata"], "bfme is a global of that state (%s)" % [bfme.get_class() if bfme != null else null])
		var probe: Variant = state.call("do_string", "return bfme == import('bfme'), Vector3(1, 2, 3).y, io == nil and os == nil and jit == nil")
		_check(probe is Array and probe.size() == 3 and probe[0] == true and is_equal_approx(float(probe[1]), 2.0) and probe[2] == true,
			"bfme, Godot Variant types, and the sandbox in one state (%s)" % [probe])
	var shared: Dictionary = world.call("execute_lua", "gdx_marker = 42")
	_check(shared.ok, "the console runs in the same state")
	if state != null:
		_check(int(state.get("globals").get("gdx_marker")) == 42, "a console global is visible through get_lua_state()")
	var budget: Dictionary = world.call("execute_lua", "while true do end")
	_check(not budget.ok and String(budget.output).contains("execution limit"), "the execution budget stops a loop (%s)" % String(budget.output).strip_edges())
	var classes: Dictionary = world.call("execute_lua", "return OS, Engine, FileAccess, bytes_to_var_with_objects")
	_check(classes.ok and String(classes.output).strip_edges() == "nil\tnil\tnil\tnil", "no Godot classes, singletons or object-making utility functions (%s)" % String(classes.output).strip_edges())
	world.queue_free()
