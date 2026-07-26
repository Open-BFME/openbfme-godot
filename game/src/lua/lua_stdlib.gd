extends RefCounted

## The Lua 4.0.1 standard library, with the OLD FLAT NAMES.
##
## This is the single most common source of confusion when porting BFME2
## scripts, so it is worth being blunt: BFME2 ships Lua 4.01, where the string
## and table functions are GLOBALS, not members of module tables.
##
##     Lua 4.0 (what BFME2 uses)      Lua 5.x (what everyone remembers)
##     strsub(s, 1, 3)                string.sub(s, 1, 3)
##     strlen(s)                      string.len(s)  /  #s
##     strfind(s, p)                  string.find(s, p)
##     gsub(s, p, r)                  string.gsub(s, p, r)
##     format("%d", n)                string.format("%d", n)
##     tinsert(t, v)                  table.insert(t, v)
##     tremove(t)                     table.remove(t)
##     getn(t)                        table.getn(t)  /  #t
##     sort(t)                        table.sort(t)
##     sin(x), PI                     math.sin(x), math.pi
##
## Writing `string.sub` here does not fail with "nil value" and a shrug: the
## names `string`, `table`, `math`, `io` and `os` are installed as TRIPWIRES
## that name the flat equivalent when touched.
##
## OMITTED ON PURPOSE - the whole io library, os facilities, dofile, require
## and loadstring. This VM runs inside a lockstep simulation; a script that can
## read the clock, the filesystem or the environment can desync a match or
## exfiltrate data. Each name is a tripwire rather than simply absent, so the
## failure says WHY.
##
## DETERMINISM CAVEAT, stated plainly: sin/cos/tan/asin/acos/atan/exp/log/
## log10 delegate to Godot's math, which delegates to the platform libm. IEEE
## does not require libm transcendentals to be correctly rounded, so these are
## the one place in this VM where two platforms could in principle disagree in
## the last ulp. That is the same exposure the rest of the simulation already
## carries (see the coverage note in tests/retail_state_pin_runner.gd), and it
## is inherited rather than introduced here. sqrt IS correctly rounded by IEEE
## and is safe. Everything else in this VM - arithmetic, comparison, string
## formatting, iteration order - is exactly specified.

const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")
const LuaNumber := preload("res://src/lua/lua_number.gd")
const LuaPatterns := preload("res://src/lua/lua_patterns.gd")
const LuaFormat := preload("res://src/lua/lua_format.gd")

## Names that exist only in Lua 5.x, mapped to what to do instead. Reading,
## calling or indexing any of them raises a named error.
const FIVE_X_TRIPWIRES := {
	"setmetatable": "Lua 4.0.1 uses TAGS, not metatables - see settag(t, newtag()) and settagmethod(tag, event, f)",
	"getmetatable": "Lua 4.0.1 uses TAGS, not metatables - see gettagmethod(tag, event)",
	"rawlen": "Lua 4.0.1 has no rawlen; use getn(t) or strlen(s)",
	"rawequal": "Lua 4.0.1 has no rawequal; '==' never invokes a tag method, so it is already raw",
	"pairs": "Lua 4.0.1 has no pairs; write `for k, v in t do` or use next(t, k)",
	"ipairs": "Lua 4.0.1 has no ipairs; write `for i = 1, getn(t) do`",
	"unpack": "Lua 4.0.1 has no unpack; index the table directly, or use call(f, argtable)",
	"select": "Lua 4.0.1 has no select; a vararg function receives its extra arguments in the local table `arg` (arg.n, arg[1], ...)",
	"pcall": "Lua 4.0.1 has no pcall; use call(f, argtable, 'x') for a protected call",
	"xpcall": "Lua 4.0.1 has no xpcall; use call(f, argtable, 'x', errhandler)",
	"loadstring": "Lua 4.0.1 has no loadstring; use dostring(s), which compiles AND runs",
	"load": "Lua 4.0.1 has no load; use dostring(s)",
	"require": "Lua 4.0.1 has no require, and this sandbox has no module search path",
	"module": "Lua 4.0.1 has no module system",
	"string": "Lua 4.0.1 has no `string` table - the string functions are GLOBALS: strsub, strlen, strfind, strlower, strupper, strrep, strbyte, strchar, format, gsub",
	"table": "Lua 4.0.1 has no `table` table - use the globals tinsert, tremove, getn, sort",
	"math": "Lua 4.0.1 has no `math` table - use the globals sin, cos, sqrt, floor, abs, max, min, mod, random, and the constant PI",
	"true": "Lua 4.0.1 has NO boolean type and `true` is not a keyword - use the number 1 (every value except nil is true)",
	"false": "Lua 4.0.1 has NO boolean type and `false` is not a keyword - use nil",
}

## Host facilities the deterministic sandbox refuses to provide at all.
const SANDBOX_TRIPWIRES := {
	"io": "the io library is withheld: this VM runs inside a lockstep simulation and has no filesystem access",
	"os": "the os library is withheld: clock, date and environment access would desync a lockstep simulation",
	"dofile": "dofile is withheld: this sandbox has no filesystem access - use dostring(s) with source the host supplied",
	"openfile": "file I/O is withheld from the deterministic sandbox",
	"readfrom": "file I/O is withheld from the deterministic sandbox",
	"writeto": "file I/O is withheld from the deterministic sandbox",
	"appendto": "file I/O is withheld from the deterministic sandbox",
	"read": "file I/O is withheld from the deterministic sandbox",
	"write": "file I/O is withheld from the deterministic sandbox - use print(), which the host can capture",
	"lines": "file I/O is withheld from the deterministic sandbox",
	"getenv": "environment access is withheld: it would differ between peers of a lockstep match",
	"execute": "process execution is withheld from the sandbox",
	"exit": "exit is withheld: a script must not be able to terminate the host",
	"clock": "clock is withheld: wall-clock time would desync a lockstep simulation - use the tick counter the host exposes",
	"date": "date is withheld: wall-clock time would desync a lockstep simulation",
	"time": "time is withheld: wall-clock time would desync a lockstep simulation",
	"difftime": "time functions are withheld from the deterministic sandbox",
	"tmpname": "filesystem access is withheld from the deterministic sandbox",
	"remove": "filesystem access is withheld from the deterministic sandbox",
	"rename": "filesystem access is withheld from the deterministic sandbox",
}

## Registers the whole library into `sandbox`.
##
## OWNERSHIP NOTE - this is why every function below takes `sandbox` as its
## first parameter instead of reading a stored field. A LuaStdlib instance is
## reachable from the sandbox's globals table (globals -> Builtin -> Callable
## -> this instance), so a back-reference from here to the sandbox would close
## a reference CYCLE, and GDScript's RefCounted does not collect cycles - every
## sandbox ever created would leak. Keeping this object stateless breaks the
## cycle by construction rather than by remembering to call a teardown.
##
## Host code registering its own functions should prefer the same shape:
## register_function(name, callable, true) hands the sandbox to the callable,
## whereas a lambda that CAPTURES the sandbox recreates exactly this cycle.
## See LuaSandbox.dispose() for the escape hatch when capture is unavoidable.
func install(sandbox: Object) -> void:
	# --- base ---
	_register(sandbox, "type", _lua_type)
	_register(sandbox, "tostring", _lua_tostring)
	_register(sandbox, "tonumber", _lua_tonumber)
	_register(sandbox, "print", _lua_print)
	_register(sandbox, "error", _lua_error)
	_register(sandbox, "assert", _lua_assert)
	_register(sandbox, "call", _lua_call)
	_register(sandbox, "dostring", _lua_dostring)
	_register(sandbox, "collectgarbage", _lua_collectgarbage)
	_register(sandbox, "gcinfo", _lua_gcinfo)

	# --- tags and tag methods ---
	_register(sandbox, "tag", _lua_tag)
	_register(sandbox, "newtag", _lua_newtag)
	_register(sandbox, "settag", _lua_settag)
	_register(sandbox, "settagmethod", _lua_settagmethod)
	_register(sandbox, "gettagmethod", _lua_gettagmethod)
	_register(sandbox, "copytagmethods", _lua_copytagmethods)

	# --- globals and raw access ---
	_register(sandbox, "globals", _lua_globals)
	_register(sandbox, "getglobal", _lua_getglobal)
	_register(sandbox, "setglobal", _lua_setglobal)
	_register(sandbox, "rawget", _lua_rawget)
	_register(sandbox, "rawset", _lua_rawset)

	# --- tables (flat names) ---
	_register(sandbox, "next", _lua_next)
	_register(sandbox, "getn", _lua_getn_fn)
	_register(sandbox, "foreach", _lua_foreach)
	_register(sandbox, "foreachi", _lua_foreachi)
	_register(sandbox, "tinsert", _lua_tinsert)
	_register(sandbox, "tremove", _lua_tremove)
	_register(sandbox, "sort", _lua_sort)

	# --- strings (flat names) ---
	_register(sandbox, "strlen", _lua_strlen)
	_register(sandbox, "strsub", _lua_strsub)
	_register(sandbox, "strlower", _lua_strlower)
	_register(sandbox, "strupper", _lua_strupper)
	_register(sandbox, "strrep", _lua_strrep)
	_register(sandbox, "strbyte", _lua_strbyte)
	_register(sandbox, "strchar", _lua_strchar)
	_register(sandbox, "strfind", _lua_strfind)
	_register(sandbox, "gsub", _lua_gsub)
	_register(sandbox, "format", _lua_format)

	# --- math (flat names) ---
	_register(sandbox, "abs", _lua_abs)
	_register(sandbox, "floor", _lua_floor)
	_register(sandbox, "ceil", _lua_ceil)
	_register(sandbox, "sqrt", _lua_sqrt)
	_register(sandbox, "mod", _lua_mod)
	_register(sandbox, "max", _lua_max)
	_register(sandbox, "min", _lua_min)
	_register(sandbox, "sin", _lua_sin)
	_register(sandbox, "cos", _lua_cos)
	_register(sandbox, "tan", _lua_tan)
	_register(sandbox, "asin", _lua_asin)
	_register(sandbox, "acos", _lua_acos)
	_register(sandbox, "atan", _lua_atan)
	_register(sandbox, "atan2", _lua_atan2)
	_register(sandbox, "exp", _lua_exp)
	_register(sandbox, "log", _lua_log)
	_register(sandbox, "log10", _lua_log10)
	_register(sandbox, "deg", _lua_deg)
	_register(sandbox, "rad", _lua_rad)
	_register(sandbox, "frexp", _lua_frexp)
	_register(sandbox, "ldexp", _lua_ldexp)
	_register(sandbox, "random", _lua_random)
	_register(sandbox, "randomseed", _lua_randomseed)
	sandbox.set_global("PI", PI)

	# In Lua 4.0 the core has NO built-in exponentiation: '^' always dispatches
	# to the `pow` tag method, and it is the MATH LIBRARY that installs one on
	# the number tag. Reproducing that is not pedantry - it is why a 4.0 chunk
	# that runs without the math library genuinely cannot use '^'.
	sandbox.set_tag_method(LuaValue.TAG_NUMBER, "pow",
		LuaValue.Builtin.new("pow", _lua_pow, true))

	for name in FIVE_X_TRIPWIRES:
		sandbox.set_global(name, LuaValue.Tripwire.new(
			"'%s'" % name, String(FIVE_X_TRIPWIRES[name])))
	for name in SANDBOX_TRIPWIRES:
		sandbox.set_global(name, LuaValue.Tripwire.new(
			"'%s'" % name, String(SANDBOX_TRIPWIRES[name])))


func _register(sandbox: Object, name: String, method: Callable) -> void:
	# pass_sandbox = true: the stdlib instance deliberately stores NOTHING, so
	# every entry point receives the environment as its first argument. See the
	# ownership note in install().
	sandbox.register_function(name, method, true)


# --- argument helpers --------------------------------------------------------

func _arg(args: Array, index: int) -> Variant:
	return args[index] if index < args.size() else null


func _bad_argument(sandbox: Object, position: int, function_name: String,
		expected: String, got: Variant) -> LuaValue.Err:
	return sandbox.error("bad argument #%d to `%s' (%s expected, got %s)"
		% [position, function_name, expected, LuaValue.type_name(got)])


## Returns a float, or an Err the caller must return unchanged.
func _check_number(sandbox: Object, args: Array, index: int, function_name: String) -> Variant:
	var value: Variant = LuaValue.coerce_to_number(_arg(args, index))
	if value == null:
		return _bad_argument(sandbox, index + 1, function_name, "number", _arg(args, index))
	return float(value)


func _opt_number(sandbox: Object, args: Array, index: int, function_name: String,
		fallback: float) -> Variant:
	if index >= args.size() or args[index] == null:
		return fallback
	return _check_number(sandbox, args, index, function_name)


func _check_string(sandbox: Object, args: Array, index: int, function_name: String) -> Variant:
	var value: Variant = LuaValue.coerce_to_string(_arg(args, index))
	if value == null:
		return _bad_argument(sandbox, index + 1, function_name, "string", _arg(args, index))
	return String(value)


func _check_table(sandbox: Object, args: Array, index: int, function_name: String) -> Variant:
	var value: Variant = _arg(args, index)
	if not (value is LuaTable):
		return _bad_argument(sandbox, index + 1, function_name, "table", value)
	return value


# --- base library ------------------------------------------------------------

func _lua_type(_sandbox: Object, args: Array) -> Variant:
	return LuaValue.type_name(_arg(args, 0))


func _lua_tostring(sandbox: Object, args: Array) -> Variant:
	return sandbox.tostring_value(_arg(args, 0))


func _lua_tonumber(sandbox: Object, args: Array) -> Variant:
	var value: Variant = _arg(args, 0)
	var base: Variant = _opt_number(sandbox, args, 1, "tonumber", 10.0)
	if base is LuaValue.Err:
		return base
	if int(base) == 10:
		return LuaValue.coerce_to_number(value)
	var text: Variant = LuaValue.coerce_to_string(value)
	if text == null:
		return null
	return LuaNumber.parse(String(text), int(base))


func _lua_print(sandbox: Object, args: Array) -> Variant:
	var parts := PackedStringArray()
	for value in args:
		parts.append(sandbox.tostring_value(value))
	sandbox.emit_output("\t".join(parts))
	return []


func _lua_error(sandbox: Object, args: Array) -> Variant:
	var value: Variant = _arg(args, 0)
	var message := "error"
	if value != null:
		message = sandbox.tostring_value(value)
	var err: LuaValue.Err = sandbox.error(message)
	err.value = value
	return err


func _lua_assert(sandbox: Object, args: Array) -> Variant:
	if LuaValue.truthy(_arg(args, 0)):
		return args
	var message: Variant = _arg(args, 1)
	if message == null:
		return sandbox.error("assertion failed!")
	return sandbox.error(sandbox.tostring_value(message))


## call(f, argtable [, mode [, errhandler]])
##
## `mode` is a string of flags, exactly as in Lua 4.0:
##   "x" - protected: a runtime error inside f is caught and call returns nil
##   "p" - pack: results come back as a table with an `n` field
func _lua_call(sandbox: Object, args: Array) -> Variant:
	var fn: Variant = _arg(args, 0)
	var argument_table: Variant = _arg(args, 1)
	var call_args: Array = []
	if argument_table is LuaTable:
		var count := _getn(argument_table)
		for i in range(1, count + 1):
			call_args.append(argument_table.rawget(float(i)))
	elif argument_table != null:
		return _bad_argument(sandbox, 2, "call", "table", argument_table)
	var mode := ""
	if _arg(args, 2) != null:
		var mode_text: Variant = LuaValue.coerce_to_string(_arg(args, 2))
		if mode_text == null:
			return _bad_argument(sandbox, 3, "call", "string", _arg(args, 2))
		mode = String(mode_text)
	var protected := mode.contains("x")
	var packed := mode.contains("p")

	var outcome: Dictionary = sandbox.call_value_protected(fn, call_args)
	if not bool(outcome["ok"]):
		if not protected:
			# Unprotected: re-raise so the error keeps travelling outward.
			return outcome["error"]
		var handler: Variant = _arg(args, 3)
		if handler != null:
			var handled: Dictionary = sandbox.call_value_protected(
				handler, [outcome["error"].text()])
			if bool(handled["ok"]) and not handled["values"].is_empty():
				return handled["values"][0]
		return null
	var results: Array = outcome["values"]
	if packed:
		var table := LuaTable.new()
		for i in range(results.size()):
			table.rawset(float(i + 1), results[i])
		table.rawset("n", float(results.size()))
		return table
	return results


func _lua_dostring(sandbox: Object, args: Array) -> Variant:
	var source: Variant = _check_string(sandbox, args, 0, "dostring")
	if source is LuaValue.Err:
		return source
	var name := "dostring"
	if _arg(args, 1) != null:
		var supplied: Variant = LuaValue.coerce_to_string(_arg(args, 1))
		if supplied != null:
			name = String(supplied)
	var outcome: Dictionary = sandbox.do_string_nested(String(source), name)
	if not bool(outcome["ok"]):
		# Lua 4.0's dostring returns nil on failure rather than propagating the
		# error. The message would otherwise vanish entirely, so it goes to the
		# host's output channel instead of being swallowed.
		sandbox.emit_output("dostring failed: %s" % outcome["error"].text())
		return null
	var values: Array = outcome["values"]
	# Lua 4.0's passresults() pushes "at least one result to signal no errors"
	# when the chunk itself returned nothing, so that `if dostring(s) then`
	# distinguishes success from failure. 4.0 pushes a NULL userdata; we use
	# the number 1, which is truthy the same way and is inspectable.
	return values if not values.is_empty() else [1.0]


## A deterministic no-op. GDScript owns memory here; exposing a real collector
## knob would let a script perturb timing without changing state.
func _lua_collectgarbage(_sandbox: Object, _args: Array) -> Variant:
	return []


func _lua_gcinfo(_sandbox: Object, _args: Array) -> Variant:
	return [0.0, 0.0]


# --- tags --------------------------------------------------------------------

func _lua_tag(_sandbox: Object, args: Array) -> Variant:
	var value: Variant = _arg(args, 0)
	if value is LuaTable:
		return float(value.tag)
	if value is LuaValue.Userdata:
		return float(value.tag)
	return float(LuaValue.default_tag(value))


func _lua_newtag(sandbox: Object, _args: Array) -> Variant:
	return float(sandbox.new_tag())


func _lua_settag(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "settag")
	if table is LuaValue.Err:
		return table
	var tag: Variant = _check_number(sandbox, args, 1, "settag")
	if tag is LuaValue.Err:
		return tag
	table.tag = int(tag)
	return table


func _lua_settagmethod(sandbox: Object, args: Array) -> Variant:
	var tag: Variant = _check_number(sandbox, args, 0, "settagmethod")
	if tag is LuaValue.Err:
		return tag
	var event: Variant = _check_string(sandbox, args, 1, "settagmethod")
	if event is LuaValue.Err:
		return event
	var name := String(event)
	if LuaValue.DEPRECATED_EVENT_NAMES.has(name):
		return sandbox.error(("tag method event '%s' is deprecated in Lua 4.0.1 " % name)
			+ "and never fires: the core implements a>b as b<a, a<=b as "
			+ "not (b<a) and a>=b as not (a<b), so ALL ordering dispatches "
			+ "through the 'lt' event - install 'lt' instead")
	if not LuaValue.EVENT_NAMES.has(name):
		return sandbox.error("'%s' is not a valid tag method event; Lua 4.0.1 "
			% name + "recognises: %s" % ", ".join(LuaValue.EVENT_NAMES))
	var method: Variant = _arg(args, 2)
	if method != null and not LuaValue.is_callable_value(method):
		return _bad_argument(sandbox, 3, "settagmethod", "function or nil", method)
	var previous: Variant = sandbox.tag_method(int(tag), name)
	sandbox.set_tag_method(int(tag), name, method)
	return previous


func _lua_gettagmethod(sandbox: Object, args: Array) -> Variant:
	var tag: Variant = _check_number(sandbox, args, 0, "gettagmethod")
	if tag is LuaValue.Err:
		return tag
	var event: Variant = _check_string(sandbox, args, 1, "gettagmethod")
	if event is LuaValue.Err:
		return event
	return sandbox.tag_method(int(tag), String(event))


func _lua_copytagmethods(sandbox: Object, args: Array) -> Variant:
	var to_tag: Variant = _check_number(sandbox, args, 0, "copytagmethods")
	if to_tag is LuaValue.Err:
		return to_tag
	var from_tag: Variant = _check_number(sandbox, args, 1, "copytagmethods")
	if from_tag is LuaValue.Err:
		return from_tag
	sandbox.copy_tag_methods(int(to_tag), int(from_tag))
	return to_tag


# --- globals -----------------------------------------------------------------

## globals() returns the current globals table; globals(t) replaces it and
## returns the OLD one. Lua 4.0 has no separate `setglobals` - this one
## function is both halves, which is why the sandbox's per-object environment
## swap goes through here.
func _lua_globals(sandbox: Object, args: Array) -> Variant:
	var previous: LuaTable = sandbox.globals_table()
	if args.size() > 0 and args[0] != null:
		var replacement: Variant = _check_table(sandbox, args, 0, "globals")
		if replacement is LuaValue.Err:
			return replacement
		sandbox.set_globals_table(replacement)
	return previous


func _lua_getglobal(sandbox: Object, args: Array) -> Variant:
	var name: Variant = _check_string(sandbox, args, 0, "getglobal")
	if name is LuaValue.Err:
		return name
	return sandbox.globals_table().rawget(String(name))


func _lua_setglobal(sandbox: Object, args: Array) -> Variant:
	var name: Variant = _check_string(sandbox, args, 0, "setglobal")
	if name is LuaValue.Err:
		return name
	sandbox.globals_table().rawset(String(name), _arg(args, 1))
	return []


func _lua_rawget(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "rawget")
	if table is LuaValue.Err:
		return table
	return table.rawget(_arg(args, 1))


func _lua_rawset(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "rawset")
	if table is LuaValue.Err:
		return table
	var key: Variant = _arg(args, 1)
	if key == null:
		return sandbox.error("table index is nil")
	table.rawset(key, _arg(args, 2))
	return table


# --- tables ------------------------------------------------------------------

## Lua 4.0's luaL_getn: an `n` field wins if it holds a number, otherwise count
## the 1..k run. Note that this is NOT the number of keys - a table with holes
## reports the border, and one with an `n` field reports whatever `n` says.
func _getn(table: LuaTable) -> int:
	var n: Variant = table.rawget("n")
	if LuaValue.is_number(n):
		return int(n)
	return table.border()


## Lua 4.0's luaL_setn: unconditionally writes the `n` field. This is why
## tinsert on a 4.0 table leaves an `n` behind and 5.x's table.insert does not.
func _setn(table: LuaTable, n: int) -> void:
	table.rawset("n", float(n))


func _lua_next(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "next")
	if table is LuaValue.Err:
		return table
	var entry: Variant = table.next_entry(_arg(args, 1))
	if entry == null:
		return sandbox.error("invalid key to `next'")
	if entry.is_empty():
		return null
	return [entry[0], entry[1]]


func _lua_getn_fn(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "getn")
	if table is LuaValue.Err:
		return table
	return float(_getn(table))


func _lua_foreach(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "foreach")
	if table is LuaValue.Err:
		return table
	var fn: Variant = _arg(args, 1)
	if not LuaValue.is_callable_value(fn):
		return _bad_argument(sandbox, 2, "foreach", "function", fn)
	# Snapshot the key order so mutating the table inside the callback cannot
	# make the traversal itself non-deterministic.
	for key in table.keys_in_order():
		var value: Variant = table.rawget(key)
		if value == null:
			continue
		var results: Array = sandbox.call_value(fn, [key, value])
		if sandbox.has_error():
			return null
		if not results.is_empty() and results[0] != null:
			return results[0]
	return null


func _lua_foreachi(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "foreachi")
	if table is LuaValue.Err:
		return table
	var fn: Variant = _arg(args, 1)
	if not LuaValue.is_callable_value(fn):
		return _bad_argument(sandbox, 2, "foreachi", "function", fn)
	var count := _getn(table)
	for i in range(1, count + 1):
		var results: Array = sandbox.call_value(fn, [float(i), table.rawget(float(i))])
		if sandbox.has_error():
			return null
		if not results.is_empty() and results[0] != null:
			return results[0]
	return null


func _lua_tinsert(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "tinsert")
	if table is LuaValue.Err:
		return table
	var n := _getn(table) + 1
	var position := n
	var value: Variant = null
	if args.size() <= 2:
		value = _arg(args, 1)
	else:
		var supplied: Variant = _check_number(sandbox, args, 1, "tinsert")
		if supplied is LuaValue.Err:
			return supplied
		position = int(supplied)
		if position > n:
			n = position
		value = _arg(args, 2)
	_setn(table, n)
	var cursor := n
	while cursor > position:
		table.rawset(float(cursor), table.rawget(float(cursor - 1)))
		cursor -= 1
	table.rawset(float(position), value)
	return []


func _lua_tremove(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "tremove")
	if table is LuaValue.Err:
		return table
	var n := _getn(table)
	var position := n
	if args.size() > 1 and args[1] != null:
		var supplied: Variant = _check_number(sandbox, args, 1, "tremove")
		if supplied is LuaValue.Err:
			return supplied
		position = int(supplied)
	if n <= 0:
		return null
	_setn(table, n - 1)
	var removed: Variant = table.rawget(float(position))
	var cursor := position
	while cursor < n:
		table.rawset(float(cursor), table.rawget(float(cursor + 1)))
		cursor += 1
	table.rawset(float(n), null)
	return removed


## sort(t [, comp]) over t[1 .. getn(t)].
##
## A bottom-up MERGE sort, not Godot's sort_custom, for three reasons: it is
## stable, it performs a fixed comparison sequence for a given input (so two
## peers agree even when the comparator is inconsistent), and it can abort
## cleanly the moment a comparator raises - which an engine sort cannot.
func _lua_sort(sandbox: Object, args: Array) -> Variant:
	var table: Variant = _check_table(sandbox, args, 0, "sort")
	if table is LuaValue.Err:
		return table
	var comparator: Variant = _arg(args, 1)
	if comparator != null and not LuaValue.is_callable_value(comparator):
		return _bad_argument(sandbox, 2, "sort", "function", comparator)
	var count := _getn(table)
	if count < 2:
		return []
	var items: Array = []
	for i in range(1, count + 1):
		items.append(table.rawget(float(i)))

	var width := 1
	while width < items.size():
		var merged: Array = []
		var start := 0
		while start < items.size():
			var middle: int = mini(start + width, items.size())
			var stop: int = mini(start + width * 2, items.size())
			var left := start
			var right := middle
			while left < middle and right < stop:
				var take_right: Variant = _sort_less(sandbox, comparator, items[right], items[left])
				if take_right is LuaValue.Err:
					return take_right
				if bool(take_right):
					merged.append(items[right])
					right += 1
				else:
					merged.append(items[left])
					left += 1
			while left < middle:
				merged.append(items[left])
				left += 1
			while right < stop:
				merged.append(items[right])
				right += 1
			start += width * 2
		items = merged
		width *= 2

	for i in range(items.size()):
		table.rawset(float(i + 1), items[i])
	return []


func _sort_less(sandbox: Object, comparator: Variant, a: Variant, b: Variant) -> Variant:
	if comparator == null:
		if LuaValue.is_number(a) and LuaValue.is_number(b):
			return float(a) < float(b)
		if LuaValue.is_string(a) and LuaValue.is_string(b):
			return String(a) < String(b)
		return sandbox.error("attempt to compare %s with %s in `sort' "
			% [LuaValue.type_name(a), LuaValue.type_name(b)]
			+ "(pass a comparison function as the second argument)")
	var results: Array = sandbox.call_value(comparator, [a, b])
	if sandbox.has_error():
		return sandbox.current_error()
	return not results.is_empty() and results[0] != null


# --- strings -----------------------------------------------------------------

## Lua's index convention: 1-based, and a negative index counts back from the
## end (-1 is the last character).
static func _relative_position(position: int, length: int) -> int:
	if position >= 0:
		return position
	return length + position + 1


func _lua_strlen(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strlen")
	if text is LuaValue.Err:
		return text
	return float(String(text).length())


func _lua_strsub(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strsub")
	if text is LuaValue.Err:
		return text
	var subject := String(text)
	var length := subject.length()
	var raw_start: Variant = _check_number(sandbox, args, 1, "strsub")
	if raw_start is LuaValue.Err:
		return raw_start
	var raw_stop: Variant = _opt_number(sandbox, args, 2, "strsub", -1.0)
	if raw_stop is LuaValue.Err:
		return raw_stop
	var start := _relative_position(int(raw_start), length)
	var stop := _relative_position(int(raw_stop), length)
	if start < 1:
		start = 1
	if stop > length:
		stop = length
	if start > stop:
		return ""
	return subject.substr(start - 1, stop - start + 1)


func _lua_strlower(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strlower")
	if text is LuaValue.Err:
		return text
	return String(text).to_lower()


func _lua_strupper(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strupper")
	if text is LuaValue.Err:
		return text
	return String(text).to_upper()


func _lua_strrep(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strrep")
	if text is LuaValue.Err:
		return text
	var count: Variant = _check_number(sandbox, args, 1, "strrep")
	if count is LuaValue.Err:
		return count
	var times := int(count)
	if times <= 0:
		return ""
	var produced := String(text).length() * times
	var limit: int = sandbox.max_string_length
	if produced > limit:
		return sandbox.error("strrep would produce a %d character string, "
			% produced + "over this sandbox's %d character limit" % limit)
	return String(text).repeat(times)


func _lua_strbyte(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strbyte")
	if text is LuaValue.Err:
		return text
	var subject := String(text)
	var raw_index: Variant = _opt_number(sandbox, args, 1, "strbyte", 1.0)
	if raw_index is LuaValue.Err:
		return raw_index
	var index := _relative_position(int(raw_index), subject.length())
	if index < 1 or index > subject.length():
		return null
	return float(subject.unicode_at(index - 1))


func _lua_strchar(sandbox: Object, args: Array) -> Variant:
	var out := ""
	for i in range(args.size()):
		var code: Variant = _check_number(sandbox, args, i, "strchar")
		if code is LuaValue.Err:
			return code
		out += String.chr(int(code))
	return out


func _lua_strfind(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "strfind")
	if text is LuaValue.Err:
		return text
	var pattern: Variant = _check_string(sandbox, args, 1, "strfind")
	if pattern is LuaValue.Err:
		return pattern
	var subject := String(text)
	var raw_init: Variant = _opt_number(sandbox, args, 2, "strfind", 1.0)
	if raw_init is LuaValue.Err:
		return raw_init
	var init := _relative_position(int(raw_init), subject.length()) - 1
	if init < 0:
		init = 0
	elif init > subject.length():
		init = subject.length()
	var plain := LuaValue.truthy(_arg(args, 3))

	var matcher := LuaPatterns.new(sandbox.pattern_step_budget)
	var found: Variant = matcher.find(subject, String(pattern), init, plain)
	if matcher.failed():
		return sandbox.error(matcher.error_message)
	if found == null:
		return null
	var results: Array = [float(int(found["start"]) + 1), float(found["end"])]
	results.append_array(found["captures"])
	return results


## gsub(s, pattern, repl [, n]). `repl` may be a string (with %0..%9
## back-references) or a function. A TABLE replacement is Lua 5.0 and is
## rejected by name rather than silently ignored.
func _lua_gsub(sandbox: Object, args: Array) -> Variant:
	var text: Variant = _check_string(sandbox, args, 0, "gsub")
	if text is LuaValue.Err:
		return text
	var pattern: Variant = _check_string(sandbox, args, 1, "gsub")
	if pattern is LuaValue.Err:
		return pattern
	var subject := String(text)
	var replacement: Variant = _arg(args, 2)
	var max_count := subject.length() + 1
	if args.size() > 3 and args[3] != null:
		var supplied: Variant = _check_number(sandbox, args, 3, "gsub")
		if supplied is LuaValue.Err:
			return supplied
		max_count = int(supplied)

	var replacement_string := ""
	var replacement_function: Variant = null
	if LuaValue.is_string(replacement) or LuaValue.is_number(replacement):
		replacement_string = String(LuaValue.coerce_to_string(replacement))
	elif LuaValue.is_callable_value(replacement):
		replacement_function = replacement
	elif replacement is LuaTable:
		return sandbox.error("gsub with a TABLE replacement was introduced in "
			+ "Lua 5.0; Lua 4.0.1 accepts a string or a function only")
	else:
		return _bad_argument(sandbox, 3, "gsub", "string or function", replacement)

	var abort: Array = [null]
	var matcher := LuaPatterns.new(sandbox.pattern_step_budget)
	var applier := func(whole: String, captures: Array) -> Variant:
		if abort[0] != null:
			return ""
		if replacement_function != null:
			var call_args: Array = captures if not captures.is_empty() else [whole]
			var results: Array = sandbox.call_value(replacement_function, call_args)
			if sandbox.has_error():
				abort[0] = sandbox.current_error()
				return ""
			if results.is_empty() or results[0] == null:
				return null
			var produced: Variant = LuaValue.coerce_to_string(results[0])
			if produced == null:
				abort[0] = sandbox.error("invalid replacement value (a %s)"
					% LuaValue.type_name(results[0]))
				return ""
			return produced
		return _expand_replacement(sandbox, replacement_string, whole, captures, abort)

	var outcome: Variant = matcher.gsub(subject, String(pattern), max_count, applier)
	if abort[0] != null:
		return abort[0]
	if matcher.failed():
		return sandbox.error(matcher.error_message)
	return [String(outcome["text"]), float(outcome["count"])]


## Expands %0..%9 in a gsub replacement string. %0 is the whole match, %1..%9
## are captures, and %% is a literal per cent.
func _expand_replacement(sandbox: Object, template: String, whole: String,
		captures: Array, abort: Array) -> String:
	var out := ""
	var i := 0
	while i < template.length():
		var c := template.unicode_at(i)
		if c != 37:  # '%'
			out += template.substr(i, 1)
			i += 1
			continue
		i += 1
		if i >= template.length():
			out += "%"
			break
		var next_char := template.unicode_at(i)
		if next_char < 48 or next_char > 57:
			out += template.substr(i, 1)
			i += 1
			continue
		var index := next_char - 48
		if index == 0:
			out += whole
		elif index <= captures.size():
			var capture: Variant = captures[index - 1]
			var as_text: Variant = LuaValue.coerce_to_string(capture)
			out += String(as_text) if as_text != null else ""
		else:
			abort[0] = sandbox.error("invalid capture index %%%d in replacement string"
				% index)
			return out
		i += 1
	return out


func _lua_format(sandbox: Object, args: Array) -> Variant:
	var spec: Variant = _check_string(sandbox, args, 0, "format")
	if spec is LuaValue.Err:
		return spec
	var rest: Array = args.slice(1)
	var outcome := LuaFormat.format(String(spec), rest,
		func(value): return sandbox.tostring_value(value))
	if not bool(outcome["ok"]):
		return sandbox.error(String(outcome["error"]))
	return String(outcome["text"])


# --- math --------------------------------------------------------------------

func _unary_math(sandbox: Object, args: Array, function_name: String, op: Callable) -> Variant:
	var value: Variant = _check_number(sandbox, args, 0, function_name)
	if value is LuaValue.Err:
		return value
	return op.call(float(value))


func _lua_abs(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "abs", func(x): return absf(x))


func _lua_floor(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "floor", func(x): return floorf(x))


func _lua_ceil(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "ceil", func(x): return ceilf(x))


func _lua_sqrt(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "sqrt", func(x): return sqrt(x))


func _lua_sin(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "sin", func(x): return sin(x))


func _lua_cos(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "cos", func(x): return cos(x))


func _lua_tan(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "tan", func(x): return tan(x))


func _lua_asin(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "asin", func(x): return asin(x))


func _lua_acos(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "acos", func(x): return acos(x))


func _lua_atan(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "atan", func(x): return atan(x))


func _lua_exp(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "exp", func(x): return exp(x))


func _lua_log(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "log", func(x): return log(x))


func _lua_log10(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "log10", func(x): return log(x) / log(10.0))


func _lua_deg(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "deg", func(x): return rad_to_deg(x))


func _lua_rad(sandbox: Object, args: Array) -> Variant:
	return _unary_math(sandbox, args, "rad", func(x): return deg_to_rad(x))


func _lua_atan2(sandbox: Object, args: Array) -> Variant:
	var y: Variant = _check_number(sandbox, args, 0, "atan2")
	if y is LuaValue.Err:
		return y
	var x: Variant = _check_number(sandbox, args, 1, "atan2")
	if x is LuaValue.Err:
		return x
	return atan2(float(y), float(x))


## C's fmod - truncated, so the result takes the sign of the DIVIDEND. This is
## not Lua 5.x's '%' operator, which floors and takes the divisor's sign.
func _lua_mod(sandbox: Object, args: Array) -> Variant:
	var a: Variant = _check_number(sandbox, args, 0, "mod")
	if a is LuaValue.Err:
		return a
	var b: Variant = _check_number(sandbox, args, 1, "mod")
	if b is LuaValue.Err:
		return b
	return fmod(float(a), float(b))


func _lua_max(sandbox: Object, args: Array) -> Variant:
	if args.is_empty():
		return _bad_argument(sandbox, 1, "max", "number", null)
	var best: Variant = _check_number(sandbox, args, 0, "max")
	if best is LuaValue.Err:
		return best
	for i in range(1, args.size()):
		var candidate: Variant = _check_number(sandbox, args, i, "max")
		if candidate is LuaValue.Err:
			return candidate
		if float(candidate) > float(best):
			best = candidate
	return best


func _lua_min(sandbox: Object, args: Array) -> Variant:
	if args.is_empty():
		return _bad_argument(sandbox, 1, "min", "number", null)
	var best: Variant = _check_number(sandbox, args, 0, "min")
	if best is LuaValue.Err:
		return best
	for i in range(1, args.size()):
		var candidate: Variant = _check_number(sandbox, args, i, "min")
		if candidate is LuaValue.Err:
			return candidate
		if float(candidate) < float(best):
			best = candidate
	return best


## frexp(x) -> m, e with x = m * 2^e and 0.5 <= |m| < 1. Implemented with pure
## multiplies and divides by powers of two, which are exact in IEEE-754, so the
## result is bit-identical everywhere.
func _lua_frexp(sandbox: Object, args: Array) -> Variant:
	var value: Variant = _check_number(sandbox, args, 0, "frexp")
	if value is LuaValue.Err:
		return value
	var x := float(value)
	if x == 0.0 or is_nan(x) or is_inf(x):
		return [x, 0.0]
	var exponent := 0
	var mantissa := absf(x)
	while mantissa >= 1.0:
		mantissa *= 0.5
		exponent += 1
	while mantissa < 0.5:
		mantissa *= 2.0
		exponent -= 1
	if x < 0.0:
		mantissa = -mantissa
	return [mantissa, float(exponent)]


func _lua_ldexp(sandbox: Object, args: Array) -> Variant:
	var mantissa: Variant = _check_number(sandbox, args, 0, "ldexp")
	if mantissa is LuaValue.Err:
		return mantissa
	var exponent: Variant = _check_number(sandbox, args, 1, "ldexp")
	if exponent is LuaValue.Err:
		return exponent
	var result := float(mantissa)
	var steps := int(exponent)
	# Repeated exact doubling/halving instead of pow(2, e): same value, no libm.
	while steps > 0:
		result *= 2.0
		steps -= 1
	while steps < 0:
		result *= 0.5
		steps += 1
	return result


## The `pow` tag method the math library installs on the number tag. Lua 4.0's
## core never computes '^' itself, so this IS exponentiation for the VM.
func _lua_pow(sandbox: Object, args: Array) -> Variant:
	var base: Variant = LuaValue.coerce_to_number(_arg(args, 0))
	var exponent: Variant = LuaValue.coerce_to_number(_arg(args, 1))
	if base == null or exponent == null:
		var offender: Variant = _arg(args, 0) if base == null else _arg(args, 1)
		return sandbox.error("attempt to perform arithmetic on a %s value in `^'"
			% LuaValue.type_name(offender))
	return pow(float(base), float(exponent))


func _lua_random(sandbox: Object, args: Array) -> Variant:
	var unit: float = sandbox.next_random()
	if args.is_empty():
		return unit
	var lower := 1.0
	var upper: Variant = _check_number(sandbox, args, 0, "random")
	if upper is LuaValue.Err:
		return upper
	var high := float(upper)
	if args.size() > 1:
		var second: Variant = _check_number(sandbox, args, 1, "random")
		if second is LuaValue.Err:
			return second
		lower = high
		high = float(second)
	if lower > high:
		return sandbox.error("bad argument to `random' (interval is empty)")
	return floorf(unit * (high - lower + 1.0)) + lower


func _lua_randomseed(sandbox: Object, args: Array) -> Variant:
	var seed_value: Variant = _check_number(sandbox, args, 0, "randomseed")
	if seed_value is LuaValue.Err:
		return seed_value
	sandbox.seed_random(int(seed_value))
	return []
