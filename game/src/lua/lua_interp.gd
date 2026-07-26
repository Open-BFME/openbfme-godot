extends RefCounted

## Tree-walking evaluator for the Lua 4.0.1 AST produced by LuaParser.
##
## WHY TREE-WALK AND NOT BYTECODE. A register VM would run maybe 2-4x faster,
## but this VM's job is short, event-driven object and map scripts inside a
## fixed-tick simulation, where the binding cost of a step BUDGET matters far
## more than raw throughput. Three properties decided it:
##   * Error locality. Every AST node carries its source line, so any failure
##     reports chunk:line with no side table to keep in sync. That is the whole
##     point of requirement "fail loudly with source position".
##   * Budget stability. A step is "one AST node evaluated or one statement
##     executed". That count is a property of the SCRIPT, not of a code
##     generator, so retuning the implementation later cannot silently change
##     how much work a given budget buys. A bytecode instruction count would
##     shift under every compiler tweak, and a lockstep sim would desync
##     between two builds that disagree.
##   * Auditability. Determinism review of a lockstep component is a
##     line-by-line human exercise; there is no second artefact to audit here.
##
## ERRORS ARE VALUES, NOT CRASHES. GDScript has no exceptions, so every eval /
## exec entry point checks `_error` after each recursive call and unwinds. A
## script error can never take down the host: it lands in `_error` and the
## sandbox turns it into a result record.
##
## DETERMINISM NOTES.
##   * Table iteration order is the insertion order LuaTable maintains.
##   * tostring(table) prints a per-sandbox SERIAL, never a machine address.
##   * Numeric formatting routes through LuaNumber, which never touches libm.
##   * There is no wall clock, no OS entropy and no filesystem in reach.

const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")
const LuaNumber := preload("res://src/lua/lua_number.gd")
const LuaParser := preload("res://src/lua/lua_parser.gd")

const FLOW_NORMAL := 0
const FLOW_BREAK := 1
const FLOW_RETURN := 2
## Set alongside `_error`; every loop and block checks for it and unwinds.
const FLOW_ERROR := 3

## One activation record. Lua 4.0 closures capture nothing implicitly, so a
## frame owns its locals outright - there are no shared upvalue cells to keep
## alive after the call returns.
class Frame extends RefCounted:
	var scopes: Array = [{}]
	var upvalues: Dictionary = {}
	var chunk: String = "?"
	var name: String = "?"
	var line: int = 0
	var varargs: Variant = null


## The globals table and the tag method registry. The sandbox creates these and
## hands them over, so BOTH objects see the same instances - the interpreter
## reads them directly on every global access and every operator fallback, and
## must not pay a weakref dereference for it.
var globals: LuaTable = null
var tag_methods: Dictionary = {}

## Ceiling on any single string this VM will build. Set by the sandbox; 0
## disables the check.
var max_string_length: int = 1 << 20

## WEAK reference to the owning LuaSandbox.
##
## It has to be weak. The sandbox owns this interpreter, so a strong pointer
## back would close a reference cycle, and GDScript's RefCounted has no cycle
## collector - every sandbox a simulation ever created would leak, which for a
## per-object-sandbox design is a slow-motion memory leak in the sim itself.
## Nothing on the hot path needs it: it is dereferenced only for the rare
## services below (serials, PRNG, output) and to hand the sandbox to
## pass_sandbox host functions.
var _sandbox_ref: WeakRef = null

var _error: LuaValue.Err = null
var _steps: int = 0
var _step_budget: int = 0
var _frames: Array[Frame] = []
var _return_values: Array = []
var _max_call_depth: int = 200


func _init(p_sandbox: Object) -> void:
	_sandbox_ref = weakref(p_sandbox)


## Resolves the owning sandbox. Returns null only if the host dropped the
## sandbox while a script was still running, which callers must handle rather
## than dereference.
func _box() -> Object:
	return _sandbox_ref.get_ref() if _sandbox_ref != null else null


# --- public entry points -----------------------------------------------------

## Runs a callable with a FRESH budget. Returns {"ok", "values", "error"}.
##
## RE-ENTRANCY. A host function invoked BY a script may legitimately want to
## run more script (an "execute script" game action is the obvious case), and
## it reaches that through the same public sandbox methods that land here. If
## this always reset the leash it would (a) clear _frames out from under the
## call that is still unwinding, and (b) hand every nesting level a brand new
## budget - so a script could loop "call a host action that runs a script" and
## never exhaust anything. Nested entry therefore SHARES the running budget and
## preserves the frame stack; only a top-level entry resets. The nested call is
## still protected, so an inner failure is reported rather than tearing down
## the outer run.
func run(fn: Variant, args: Array, budget: int, max_depth: int) -> Dictionary:
	if not _frames.is_empty():
		var nested := call_protected(fn, args)
		return {
			"ok": nested["ok"], "values": nested["values"],
			"error": nested["error"], "steps": _steps,
		}
	_error = null
	_steps = 0
	_step_budget = budget
	_max_call_depth = max_depth
	_frames.clear()
	var values := call_value(fn, args, 0)
	if _error != null:
		return {"ok": false, "values": [], "error": _error, "steps": _steps}
	return {"ok": true, "values": values, "error": null, "steps": _steps}


## True while a script is executing, so the sandbox can tell a top-level entry
## from a re-entrant one.
func is_running() -> bool:
	return not _frames.is_empty()


## Charges `count` steps for work a HOST function is about to do on the
## script's behalf, and reports whether the budget is now spent.
##
## This closes the loophole where O(n) work costs O(1) steps. `t.n = 1e9` is
## one step, but it makes getn() report a billion, and tremove/tinsert/sort/
## call then run billion-iteration loops entirely inside GDScript where the
## per-node budget never sees them - a script could stall a lockstep tick
## without ever exceeding its step count. Charging BEFORE the loop means the
## error fires instead of the work happening.
func charge_steps(count: int, line: int = 0) -> bool:
	if count <= 0:
		return _failed()
	_steps += count - 1
	return _charge(line)


## Calls a value and CATCHES any error it raises, leaving the budget and the
## surrounding call in place. Backs call(f, args, "x") and dostring().
##
## A budget error is deliberately NOT catchable. Letting a script swallow
## LUA_STEP_BUDGET_EXCEEDED inside a protected call would hand it a way to
## ignore its own leash, which is precisely the failure the budget exists to
## prevent; it is re-raised instead.
func call_protected(fn: Variant, args: Array, line: int = 0) -> Dictionary:
	var depth_before := _frames.size()
	var values := call_value(fn, args, line)
	if _error != null:
		var caught := _error
		if caught.kind == LuaValue.Err.KIND_BUDGET:
			return {"ok": false, "values": [], "error": caught}
		_error = null
		while _frames.size() > depth_before:
			_frames.pop_back()
		return {"ok": false, "values": [], "error": caught}
	return {"ok": true, "values": values, "error": null}


## Discards a pending error. Only a protected boundary should call this.
func clear_error() -> void:
	_error = null


func steps_used() -> int:
	return _steps


func current_error() -> LuaValue.Err:
	return _error


## Raises a contained script error from host code. Builtins signal failure by
## RETURNING the Err this produces (see LuaSandbox.register_function).
func make_error(message: String, kind: String = LuaValue.Err.KIND_RUNTIME) -> LuaValue.Err:
	var line := 0
	var chunk := "?"
	if not _frames.is_empty():
		line = _frames[-1].line
		chunk = _frames[-1].chunk
	return LuaValue.Err.new(kind, message, chunk, line)


func _raise(message: String, line: int, kind: String = LuaValue.Err.KIND_RUNTIME) -> void:
	if _error != null:
		return
	var chunk := "?"
	if not _frames.is_empty():
		chunk = _frames[-1].chunk
	_error = LuaValue.Err.new(kind, message, chunk, line)
	for index in range(_frames.size() - 1, -1, -1):
		var frame := _frames[index]
		_error.traceback.append("%s:%d in %s" % [frame.chunk, frame.line, frame.name])


func _raise_error_object(err: LuaValue.Err) -> void:
	if _error != null:
		return
	_error = err
	if err.traceback.is_empty():
		for index in range(_frames.size() - 1, -1, -1):
			var frame := _frames[index]
			err.traceback.append("%s:%d in %s" % [frame.chunk, frame.line, frame.name])


func _failed() -> bool:
	return _error != null


## Charges one step and reports whether the budget just ran out. The budget is
## the ONLY thing standing between a `while 1 do end` in a mod script and a
## hung simulation tick, so it is checked on every node, not sampled.
func _charge(line: int) -> bool:
	_steps += 1
	if _step_budget > 0 and _steps > _step_budget:
		if _error == null:
			_raise("LUA_STEP_BUDGET_EXCEEDED: script used more than %d steps in a "
				% _step_budget + "single invocation and was stopped",
				line, LuaValue.Err.KIND_BUDGET)
		return true
	return false


# --- calling -----------------------------------------------------------------

## Calls any callable Lua value. Returns the result list (possibly empty).
func call_value(fn: Variant, args: Array, line: int = 0) -> Array:
	if _failed():
		return []
	if fn is LuaValue.Closure:
		return _call_closure(fn, args, line)
	if fn is LuaValue.Builtin:
		return _call_builtin(fn, args, line)
	if fn is LuaValue.Tripwire:
		_raise(fn.message(), line, LuaValue.Err.KIND_UNSUPPORTED)
		return []
	# Lua 4.0's "function" tag method: calling a non-function dispatches to the
	# tag method of the value being called, with the value itself prepended.
	var handler: Variant = _tag_method_for(fn, "function")
	if handler != null:
		var forwarded: Array = [fn]
		forwarded.append_array(args)
		return call_value(handler, forwarded, line)
	_raise("attempt to call a %s value" % LuaValue.type_name(fn), line)
	return []


func _call_closure(closure: LuaValue.Closure, args: Array, line: int) -> Array:
	if _frames.size() >= _max_call_depth:
		_raise("stack overflow: Lua call depth exceeded %d frames" % _max_call_depth, line)
		return []
	var proto: Dictionary = closure.proto
	var frame := Frame.new()
	frame.scopes = [{}]
	frame.upvalues = closure.upvalues
	frame.chunk = closure.chunk
	frame.name = closure.name
	frame.line = int(proto["line"])

	var params: PackedStringArray = proto["params"]
	var scope: Dictionary = frame.scopes[0]
	for i in range(params.size()):
		scope[params[i]] = args[i] if i < args.size() else null

	if bool(proto["is_vararg"]):
		# Lua 4.0 has no `...` expression: the extra arguments arrive as a
		# local table named `arg`, whose `n` field holds how many there were.
		var extra := LuaTable.new()
		var count := 0
		for i in range(params.size(), args.size()):
			count += 1
			extra.rawset(float(count), args[i])
		extra.rawset("n", float(count))
		scope["arg"] = extra

	_frames.append(frame)
	_return_values = []
	var flow := _exec_block(proto["body"], frame)
	var results: Array = _return_values if flow == FLOW_RETURN else []
	_return_values = []
	_frames.pop_back()
	if _failed():
		return []
	return results


## Invokes a host-registered function.
##
## RETURN CONTRACT (also documented on LuaSandbox.register_function):
##   Array               -> multiple Lua return values, converted element-wise
##   LuaValue.Err        -> raise that error inside the script
##   anything else       -> a single value, run through LuaValue.from_host so a
##                          handler may return bool / int / String / Dictionary
func _call_builtin(builtin: LuaValue.Builtin, args: Array, line: int) -> Array:
	if not builtin.callable.is_valid():
		_raise("host function '%s' is no longer callable (its object was freed)"
			% builtin.name, line)
		return []
	var frame := Frame.new()
	frame.chunk = _frames[-1].chunk if not _frames.is_empty() else "?"
	frame.name = "host function '%s'" % builtin.name
	frame.line = line
	_frames.append(frame)
	var raw: Variant = null
	if builtin.pass_sandbox:
		var box := _box()
		if box == null:
			_frames.pop_back()
			_raise("host function '%s' needs its sandbox, which the host has "
				% builtin.name + "already released", line)
			return []
		raw = builtin.callable.call(box, args)
	else:
		raw = builtin.callable.call(args)
	_frames.pop_back()
	if _failed():
		return []
	if raw is LuaValue.Err:
		_raise_error_object(raw)
		return []
	if raw is Array:
		var converted: Array = []
		for item in raw:
			converted.append(LuaValue.from_host(item))
		return converted
	return [LuaValue.from_host(raw)]


# --- tag methods -------------------------------------------------------------

func _tag_of(value: Variant) -> int:
	if value is LuaTable:
		return value.tag
	if value is LuaValue.Userdata:
		return value.tag
	return LuaValue.default_tag(value)


func _tag_method_for(value: Variant, event: String) -> Variant:
	var events: Variant = tag_methods.get(_tag_of(value))
	if events == null:
		return null
	return events.get(event)


## Lua 4.0's getbinmethod: the first operand's tag method wins; the second is
## consulted only when the first has none.
func _binary_tag_method(a: Variant, b: Variant, event: String) -> Variant:
	var handler: Variant = _tag_method_for(a, event)
	if handler != null:
		return handler
	return _tag_method_for(b, event)


func _call_tag_method(handler: Variant, args: Array, line: int) -> Variant:
	var results := call_value(handler, args, line)
	if results.is_empty():
		return null
	return results[0]


# --- indexing ----------------------------------------------------------------

func _gettable(obj: Variant, key: Variant, line: int) -> Variant:
	if obj is LuaTable:
		# lvm.c luaV_gettable takes the raw path when
		#     ttype(t) == LUA_TTABLE && (htag == LUA_TTABLE || gettm(GETTABLE) == NULL)
		# so a table with the DEFAULT tag never fires `gettable`, but one given
		# a custom tag that has the method does - and then `index` is not
		# consulted at all.
		var custom_handler: Variant = null
		if obj.tag != LuaValue.TAG_TABLE:
			custom_handler = _tag_method_for(obj, "gettable")
		if custom_handler != null:
			return _call_tag_method(custom_handler, [obj, key], line)
		var value: Variant = obj.rawget(key)
		if value != null:
			return value
		# Only a MISS consults `index`; a present field never does.
		var index_handler: Variant = _tag_method_for(obj, "index")
		if index_handler != null:
			return _call_tag_method(index_handler, [obj, key], line)
		return null
	if obj is LuaValue.Tripwire:
		_raise(obj.message(), line, LuaValue.Err.KIND_UNSUPPORTED)
		return null
	var gettable_handler: Variant = _tag_method_for(obj, "gettable")
	if gettable_handler != null:
		return _call_tag_method(gettable_handler, [obj, key], line)
	_raise("attempt to index a %s value" % LuaValue.type_name(obj), line)
	return null


## The ONE place a table key is validated. Every write path - assignment,
## table constructor and rawset - routes through it, because a guard that only
## one of the three consults is not a guard: t[0/0] would be refused by
## `t[k] = v` and quietly accepted by `{[0/0] = v}` and `rawset`.
## Returns true when the key is bad and an error has been raised.
func reject_bad_key(key: Variant, line: int) -> bool:
	if key == null:
		_raise("table index is nil", line)
		return true
	if key is float and is_nan(key):
		_raise("table index is NaN: NaN never equals itself, so the entry "
			+ "could never be read back - Lua 4.0 permitted this, this VM "
			+ "refuses it because a silently unreachable field is a defect",
			line, LuaValue.Err.KIND_UNSUPPORTED)
		return true
	return false


func _settable(obj: Variant, key: Variant, value: Variant, line: int) -> void:
	# A table consults `settable` only when it carries a NON-DEFAULT tag that
	# actually has the method - lvm.c luaV_settable tests
	#     ttype(t) == LUA_TTABLE && (htag == LUA_TTABLE || gettm(SETTABLE) == NULL)
	# for the raw path. A plain {} therefore never fires it, but a settag'd
	# table does. Getting this wrong made settagmethod(tag, "settable") a
	# silent no-op even though settagmethod accepted the registration.
	if obj is LuaTable:
		var custom_handler: Variant = null
		if obj.tag != LuaValue.TAG_TABLE:
			custom_handler = _tag_method_for(obj, "settable")
		if custom_handler != null:
			call_value(custom_handler, [obj, key, value], line)
			return
		if reject_bad_key(key, line):
			return
		obj.rawset(key, value)
		return
	if obj is LuaValue.Tripwire:
		_raise(obj.message(), line, LuaValue.Err.KIND_UNSUPPORTED)
		return
	var handler: Variant = _tag_method_for(obj, "settable")
	if handler != null:
		call_value(handler, [obj, key, value], line)
		return
	_raise("attempt to index a %s value" % LuaValue.type_name(obj), line)


# --- globals -----------------------------------------------------------------

func _get_global(name: String, line: int) -> Variant:
	var value: Variant = globals.rawget(name)
	# Lua 4.0 selects the getglobal tag method from the tag of the CURRENT
	# value, which is why a hook on the nil tag can trap reads of undefined
	# globals.
	var handler: Variant = _tag_method_for(value, "getglobal")
	if handler != null:
		return _call_tag_method(handler, [name, value], line)
	if value is LuaValue.Tripwire:
		_raise(value.message(), line, LuaValue.Err.KIND_UNSUPPORTED)
		return null
	return value


func _set_global(name: String, value: Variant, line: int) -> void:
	var old: Variant = globals.rawget(name)
	var handler: Variant = _tag_method_for(old, "setglobal")
	if handler != null:
		call_value(handler, [name, old, value], line)
		return
	globals.rawset(name, value)


# --- arithmetic, comparison, concatenation -----------------------------------

func _arith(op: String, a: Variant, b: Variant, event: String, line: int) -> Variant:
	# Lua 4.0's `^` has NO built-in implementation: the core always dispatches
	# to the `pow` tag method, which the math library installs on the number
	# tag. Without the math library loaded, 2^3 is genuinely an error - that is
	# faithful 4.0, not a bug here.
	if event != "pow":
		var na: Variant = LuaValue.coerce_to_number(a)
		var nb: Variant = LuaValue.coerce_to_number(b)
		if na != null and nb != null:
			match op:
				"+": return float(na) + float(nb)
				"-": return float(na) - float(nb)
				"*": return float(na) * float(nb)
				"/":
					# IEEE division: x/0 yields inf/nan rather than trapping,
					# exactly as Lua 4.0 on a C double.
					return float(na) / float(nb)
	var handler: Variant = _binary_tag_method(a, b, event)
	if handler != null:
		return _call_tag_method(handler, [a, b, event], line)
	var offender: Variant = a if LuaValue.coerce_to_number(a) == null else b
	_raise("attempt to perform arithmetic on a %s value"
		% LuaValue.type_name(offender), line)
	return null


func _negate(value: Variant, line: int) -> Variant:
	var number: Variant = LuaValue.coerce_to_number(value)
	if number != null:
		return -float(number)
	var handler: Variant = _tag_method_for(value, "unm")
	if handler != null:
		# The 4.0 manual's unm_event pseudo-code is tm(op, nil, "unm") - the
		# operand, then NIL, then the event name. Passing (op, op) instead
		# looks harmless until a tag method inspects its second argument.
		return _call_tag_method(handler, [value, null, "unm"], line)
	_raise("attempt to perform arithmetic on a %s value"
		% LuaValue.type_name(value), line)
	return null


func _concat(a: Variant, b: Variant, line: int) -> Variant:
	var sa: Variant = LuaValue.coerce_to_string(a)
	var sb: Variant = LuaValue.coerce_to_string(b)
	if sa != null and sb != null:
		# Concatenation is the cheapest memory bomb in the language: `s = s..s`
		# doubles per statement, so about forty statements - forty steps, well
		# inside any budget - reaches terabyte scale and takes the host down
		# with an allocation failure the VM cannot contain. The size is checked
		# BEFORE allocating, so the refusal costs nothing.
		var combined: int = String(sa).length() + String(sb).length()
		if max_string_length > 0 and combined > max_string_length:
			_raise("concatenation would produce a %d character string, over "
				% combined + "this sandbox's %d character limit" % max_string_length,
				line)
			return null
		return String(sa) + String(sb)
	var handler: Variant = _binary_tag_method(a, b, "concat")
	if handler != null:
		# 4.0 passes the event name as a third argument to EVERY binary tag
		# method - lvm.c's call_binTM pushes luaT_eventname[event]
		# unconditionally, and the manual's 4.8 pseudo-code is
		# tm(op1, op2, "concat"). A handler shared between events reads that
		# argument to tell which one fired.
		return _call_tag_method(handler, [a, b, "concat"], line)
	# KNOWN DEVIATION, deliberate. 4.0's call_binTM tries a THIRD lookup before
	# giving up - luaT_gettm(L, 0, event), i.e. the handler registered against
	# tag 0 - and only errors if that is also absent. _binary_tag_method consults
	# the two operands' tags and stops.
	#
	# Note what tag 0 is and is not: in 4.0 `#define LUA_TUSERDATA 0`, so it is
	# the DEFAULT USERDATA TAG, not a universal catch-all. This VM agrees
	# (LuaValue.TAG_USERDATA == 0), so settagmethod(0, 'concat', f) already
	# fires whenever an operand is default-tagged userdata. What is missing is
	# only the neither-operand-matched fallback.
	#
	# The same gap exists in _arith and _negate, which share _binary_tag_method /
	# _tag_method_for, because 4.0 routes add/sub/mul/div/pow and unm through
	# call_arith -> call_binTM as well.
	#
	# Not implemented because a tag-0 handler would then intercept every failed
	# concat, compare and arithmetic op in the sandbox, and nothing in the retail
	# corpus registers one.
	var offender: Variant = a if sa == null else b
	_raise("attempt to concatenate a %s value" % LuaValue.type_name(offender), line)
	return null


## `a < b`. Everything else routes here, because Lua 4.0 defines
##     a > b   as  b < a
##     a <= b  as  not (b < a)
##     a >= b  as  not (a < b)
## and therefore only ever needs the `lt` event.
func _less_than(a: Variant, b: Variant, line: int) -> Variant:
	if LuaValue.is_number(a) and LuaValue.is_number(b):
		return LuaValue.from_bool(float(a) < float(b))
	if LuaValue.is_string(a) and LuaValue.is_string(b):
		return LuaValue.from_bool(String(a) < String(b))
	var handler: Variant = _binary_tag_method(a, b, "lt")
	if handler != null:
		# As above: manual 4.8 gives tm(op1, op2, "lt"). Note the handler sees
		# "lt" for all four comparisons, because >, <= and >= are rewritten in
		# terms of < before reaching here - so a handler cannot distinguish
		# `a > b` from `b < a`, which is 4.0's behaviour, not a shortcut.
		return LuaValue.from_bool(LuaValue.truthy(
			_call_tag_method(handler, [a, b, "lt"], line)))
	_raise("attempt to compare %s with %s"
		% [LuaValue.type_name(a), LuaValue.type_name(b)], line)
	return null


# --- statements --------------------------------------------------------------

func _exec_block(statements: Array, frame: Frame) -> int:
	frame.scopes.append({})
	var flow := _exec_statements(statements, frame)
	frame.scopes.pop_back()
	return flow


func _exec_statements(statements: Array, frame: Frame) -> int:
	for statement in statements:
		var flow := _exec_statement(statement, frame)
		if flow != FLOW_NORMAL:
			return flow
	return FLOW_NORMAL


func _exec_statement(statement: Dictionary, frame: Frame) -> int:
	var line := int(statement["line"])
	frame.line = line
	if _charge(line):
		return FLOW_ERROR

	match int(statement["k"]):
		LuaParser.S_LOCAL:
			var values := _eval_list(statement["exprs"], frame)
			if _failed():
				return FLOW_ERROR
			var names: PackedStringArray = statement["names"]
			var scope: Dictionary = frame.scopes[-1]
			for i in range(names.size()):
				scope[names[i]] = values[i] if i < values.size() else null
			return FLOW_NORMAL

		LuaParser.S_ASSIGN:
			var values := _eval_list(statement["exprs"], frame)
			if _failed():
				return FLOW_ERROR
			var targets: Array = statement["targets"]
			for i in range(targets.size()):
				var value: Variant = values[i] if i < values.size() else null
				_assign(targets[i], value, frame)
				if _failed():
					return FLOW_ERROR
			return FLOW_NORMAL

		LuaParser.S_CALL:
			_eval_multi(statement["call"], frame)
			return FLOW_ERROR if _failed() else FLOW_NORMAL

		LuaParser.S_DO:
			return _exec_block(statement["body"], frame)

		LuaParser.S_IF:
			for clause in statement["clauses"]:
				var condition: Variant = _eval(clause[0], frame)
				if _failed():
					return FLOW_ERROR
				if LuaValue.truthy(condition):
					return _exec_block(clause[1], frame)
			if statement["else_body"] != null:
				return _exec_block(statement["else_body"], frame)
			return FLOW_NORMAL

		LuaParser.S_WHILE:
			while true:
				if _charge(line):
					return FLOW_ERROR
				var condition: Variant = _eval(statement["cond"], frame)
				if _failed():
					return FLOW_ERROR
				if not LuaValue.truthy(condition):
					return FLOW_NORMAL
				var flow := _exec_block(statement["body"], frame)
				if flow == FLOW_BREAK:
					return FLOW_NORMAL
				if flow == FLOW_RETURN or flow == FLOW_ERROR:
					return flow
			return FLOW_NORMAL

		LuaParser.S_REPEAT:
			# The until-expression is evaluated OUTSIDE the body's scope, so it
			# CANNOT see locals the body declared. lparser.c repeatstat calls
			#     block(ls); check_match(ls, TK_UNTIL, ...); cond(ls, &v);
			# and block() runs removelocalvars() as it leaves - the body's
			# locals are gone by the time the condition is compiled, so
			#     repeat local done = f() until done
			# reads the GLOBAL `done` in Lua 4.0. Letting the condition see the
			# body's scope is Lua 5.1's rule and would make that loop terminate
			# when 4.0 spins forever.
			while true:
				if _charge(line):
					return FLOW_ERROR
				frame.scopes.append({})
				var flow := _exec_statements(statement["body"], frame)
				frame.scopes.pop_back()
				if flow == FLOW_BREAK:
					return FLOW_NORMAL
				if flow == FLOW_RETURN or flow == FLOW_ERROR:
					return flow
				var condition: Variant = _eval(statement["cond"], frame)
				if _failed():
					return FLOW_ERROR
				if LuaValue.truthy(condition):
					return FLOW_NORMAL
			return FLOW_NORMAL

		LuaParser.S_FORNUM:
			return _exec_numeric_for(statement, frame, line)

		LuaParser.S_FORIN:
			return _exec_generic_for(statement, frame, line)

		LuaParser.S_RETURN:
			var values := _eval_list(statement["exprs"], frame)
			if _failed():
				return FLOW_ERROR
			_return_values = values
			return FLOW_RETURN

		LuaParser.S_BREAK:
			return FLOW_BREAK

	_raise("internal error: unknown statement kind %d" % int(statement["k"]), line)
	return FLOW_ERROR


func _exec_numeric_for(statement: Dictionary, frame: Frame, line: int) -> int:
	var start_value: Variant = LuaValue.coerce_to_number(_eval(statement["start"], frame))
	if _failed():
		return FLOW_ERROR
	var stop_value: Variant = LuaValue.coerce_to_number(_eval(statement["stop"], frame))
	if _failed():
		return FLOW_ERROR
	var step_value: Variant = 1.0
	if statement["step"] != null:
		step_value = LuaValue.coerce_to_number(_eval(statement["step"], frame))
		if _failed():
			return FLOW_ERROR
	if start_value == null or stop_value == null or step_value == null:
		_raise("'for' initial value, limit and step must all be numbers", line)
		return FLOW_ERROR
	var step := float(step_value)
	if step == 0.0:
		# Stock Lua 4.0 spins forever here and the step budget would eventually
		# fire, but "budget exhausted" is a terrible diagnosis for a typo.
		_raise("'for' step is zero, which can never terminate", line)
		return FLOW_ERROR

	var index := float(start_value)
	var limit := float(stop_value)
	var control := String(statement["var"])
	while (step > 0.0 and index <= limit) or (step < 0.0 and index >= limit):
		if _charge(line):
			return FLOW_ERROR
		frame.scopes.append({control: index})
		var flow := _exec_statements(statement["body"], frame)
		frame.scopes.pop_back()
		if flow == FLOW_BREAK:
			return FLOW_NORMAL
		if flow == FLOW_RETURN or flow == FLOW_ERROR:
			return flow
		# The control variable is a fresh copy each iteration, so assigning to
		# it inside the body cannot perturb the loop - Lua's rule.
		index += step
	return FLOW_NORMAL


## `for k, v in t do` - Lua 4.0's generic for walks a TABLE with next(). It is
## not the 5.0 iterator-triple form, so `for k,v in pairs(t)` cannot work here
## (and `pairs` is a tripwire that says so).
func _exec_generic_for(statement: Dictionary, frame: Frame, line: int) -> int:
	var subject: Variant = _eval(statement["subject"], frame)
	if _failed():
		return FLOW_ERROR
	if not (subject is LuaTable):
		_raise("'for ... in' expects a table in Lua 4.0.1 but got a %s value "
			% LuaValue.type_name(subject) + "(the iterator-function form "
			+ "`for k,v in pairs(t)` is Lua 5.0)", line, LuaValue.Err.KIND_UNSUPPORTED)
		return FLOW_ERROR
	var table: LuaTable = subject
	var key_name := String(statement["key_var"])
	var value_name := String(statement["value_var"])
	var cursor: Variant = null
	while true:
		if _charge(line):
			return FLOW_ERROR
		var entry: Variant = table.next_entry(cursor)
		if entry == null or (entry is Array and entry.is_empty()):
			return FLOW_NORMAL
		cursor = entry[0]
		frame.scopes.append({key_name: entry[0], value_name: entry[1]})
		var flow := _exec_statements(statement["body"], frame)
		frame.scopes.pop_back()
		if flow == FLOW_BREAK:
			return FLOW_NORMAL
		if flow == FLOW_RETURN or flow == FLOW_ERROR:
			return flow
	return FLOW_NORMAL


func _assign(target: Dictionary, value: Variant, frame: Frame) -> void:
	var line := int(target["line"])
	match int(target["k"]):
		LuaParser.E_NAME:
			var name := String(target["name"])
			for index in range(frame.scopes.size() - 1, -1, -1):
				if frame.scopes[index].has(name):
					frame.scopes[index][name] = value
					return
			_set_global(name, value, line)
		LuaParser.E_INDEX:
			var obj: Variant = _eval(target["obj"], frame)
			if _failed():
				return
			var key: Variant = _eval(target["key"], frame)
			if _failed():
				return
			_settable(obj, key, value, line)
		_:
			_raise("cannot assign to this expression", line)


# --- expressions -------------------------------------------------------------

## Evaluates an expression list, expanding the LAST element's multiple values
## and truncating every earlier element to one. This is the rule behind
## `f(g())`, `return g()`, `a, b = g()` and `{g()}`.
func _eval_list(exprs: Array, frame: Frame) -> Array:
	var values: Array = []
	var count := exprs.size()
	for i in range(count):
		if _failed():
			return values
		if i == count - 1:
			values.append_array(_eval_multi(exprs[i], frame))
		else:
			values.append(_eval(exprs[i], frame))
	return values


## Evaluates an expression that may produce several values.
func _eval_multi(node: Dictionary, frame: Frame) -> Array:
	var kind := int(node["k"])
	if kind == LuaParser.E_CALL or kind == LuaParser.E_METHCALL:
		var line := int(node["line"])
		frame.line = line
		if _charge(line):
			return []
		var target: Variant = null
		var args: Array = []
		if kind == LuaParser.E_CALL:
			target = _eval(node["fn"], frame)
			if _failed():
				return []
			args = _eval_list(node["args"], frame)
		else:
			var receiver: Variant = _eval(node["obj"], frame)
			if _failed():
				return []
			target = _gettable(receiver, String(node["method"]), line)
			if _failed():
				return []
			args = [receiver]
			args.append_array(_eval_list(node["args"], frame))
		if _failed():
			return []
		return call_value(target, args, line)
	var single: Variant = _eval(node, frame)
	if _failed():
		return []
	return [single]


func _eval(node: Dictionary, frame: Frame) -> Variant:
	var line := int(node["line"])
	if _charge(line):
		return null

	match int(node["k"]):
		LuaParser.E_NUMBER:
			return node["v"]
		LuaParser.E_STRING:
			return node["v"]
		LuaParser.E_NIL:
			return null

		LuaParser.E_NAME:
			var name := String(node["name"])
			for index in range(frame.scopes.size() - 1, -1, -1):
				var scope: Dictionary = frame.scopes[index]
				if scope.has(name):
					return scope[name]
			return _get_global(name, line)

		LuaParser.E_UPVALUE:
			var upvalue_name := String(node["name"])
			if not frame.upvalues.has(upvalue_name):
				# Reaching here means the closure was built without this name,
				# which can only happen if the AST was mutated behind our back.
				_raise("upvalue '%%%s' was not captured by this closure" % upvalue_name,
					line, LuaValue.Err.KIND_UNSUPPORTED)
				return null
			return frame.upvalues[upvalue_name]

		LuaParser.E_PAREN:
			return _eval(node["e"], frame)

		LuaParser.E_INDEX:
			var obj: Variant = _eval(node["obj"], frame)
			if _failed():
				return null
			var key: Variant = _eval(node["key"], frame)
			if _failed():
				return null
			return _gettable(obj, key, line)

		LuaParser.E_CALL, LuaParser.E_METHCALL:
			var results := _eval_multi(node, frame)
			return results[0] if not results.is_empty() else null

		LuaParser.E_FUNCTION:
			return _make_closure(node, frame)

		LuaParser.E_TABLE:
			return _build_table(node, frame)

		LuaParser.E_AND:
			var left_and: Variant = _eval(node["l"], frame)
			if _failed():
				return null
			# Lua 4.0: `a and b` is a if a is nil, else b. No booleans exist.
			if not LuaValue.truthy(left_and):
				return null
			return _eval(node["r"], frame)

		LuaParser.E_OR:
			var left_or: Variant = _eval(node["l"], frame)
			if _failed():
				return null
			if LuaValue.truthy(left_or):
				return left_or
			return _eval(node["r"], frame)

		LuaParser.E_UNOP:
			var operand: Variant = _eval(node["e"], frame)
			if _failed():
				return null
			if String(node["op"]) == "not":
				return LuaValue.from_bool(not LuaValue.truthy(operand))
			return _negate(operand, line)

		LuaParser.E_BINOP:
			return _eval_binop(node, frame, line)

	_raise("internal error: unknown expression kind %d" % int(node["k"]), line)
	return null


func _eval_binop(node: Dictionary, frame: Frame, line: int) -> Variant:
	var a: Variant = _eval(node["l"], frame)
	if _failed():
		return null
	var b: Variant = _eval(node["r"], frame)
	if _failed():
		return null
	match String(node["op"]):
		"+": return _arith("+", a, b, "add", line)
		"-": return _arith("-", a, b, "sub", line)
		"*": return _arith("*", a, b, "mul", line)
		"/": return _arith("/", a, b, "div", line)
		"^": return _arith("^", a, b, "pow", line)
		"..": return _concat(a, b, line)
		"==": return LuaValue.from_bool(LuaValue.raw_equal(a, b))
		"~=": return LuaValue.from_bool(not LuaValue.raw_equal(a, b))
		"<": return _less_than(a, b, line)
		">": return _less_than(b, a, line)
		"<=":
			var swapped: Variant = _less_than(b, a, line)
			if _failed():
				return null
			return LuaValue.from_bool(not LuaValue.truthy(swapped))
		">=":
			var forward: Variant = _less_than(a, b, line)
			if _failed():
				return null
			return LuaValue.from_bool(not LuaValue.truthy(forward))
	_raise("internal error: unknown binary operator '%s'" % node["op"], line)
	return null


## Builds a closure, snapshotting its %upvalues from the CURRENT frame.
##
## This is the whole of Lua 4.0's closure model: values are copied once, here,
## and are immutable afterwards. There are no cells and no shared state, which
## is why a 4.0 closure cannot be used to build a counter the way a 5.x one can.
func _make_closure(node: Dictionary, frame: Frame) -> LuaValue.Closure:
	var closure := LuaValue.Closure.new()
	closure.proto = node
	closure.chunk = frame.chunk
	closure.name = String(node["name"])
	var names: PackedStringArray = node["upvalues"]
	for name in names:
		closure.upvalues[name] = _lookup_for_capture(name, frame, int(node["line"]))
	return closure


func _lookup_for_capture(name: String, frame: Frame, line: int) -> Variant:
	for index in range(frame.scopes.size() - 1, -1, -1):
		var scope: Dictionary = frame.scopes[index]
		if scope.has(name):
			return scope[name]
	# Not a local of the defining function: fall back to the global of that
	# name, snapshotted now. Reading it through _get_global keeps any getglobal
	# tag method in play.
	return _get_global(name, line)


func _build_table(node: Dictionary, frame: Frame) -> Variant:
	var table := LuaTable.new()
	# Each positional field is closed to ONE value, including the last.
	# lparser.c's listfields reaches every element through exp1(), which calls
	# luaK_tostack(ls, &v, 1) - there is no LUA_MULTRET in 4.0's constructor
	# path. So `{f()}` holds exactly one element even when f returns three;
	# multret in constructors is Lua 5.0. Using _eval_list here (which expands
	# the tail, correctly, for call arguments and return statements) would
	# quietly give `{f()}` extra elements.
	# Evaluation order follows SOURCE order, not a fixed array-then-hash walk.
	# lparser.c emits constructor_part #1 completely before #2, and emission
	# order is execution order, so `{x = f(); g(), h()}` runs f, g, h. Walking
	# the arrays in a fixed order would run g, h, f and silently reorder side
	# effects in the keyed-first form this parser now accepts.
	if bool(node.get("keyed_first", false)):
		if not _build_table_hash(node, table, frame):
			return null
		if not _build_table_array(node, table, frame):
			return null
	else:
		if not _build_table_array(node, table, frame):
			return null
		if not _build_table_hash(node, table, frame):
			return null
	return table


func _build_table_array(node: Dictionary, table: LuaTable, frame: Frame) -> bool:
	var array_items: Array = node["array"]
	for i in range(array_items.size()):
		var item: Variant = _eval(array_items[i], frame)
		if _failed():
			return false
		table.rawset(float(i + 1), item)
	return true


func _build_table_hash(node: Dictionary, table: LuaTable, frame: Frame) -> bool:
	for pair in node["hash"]:
		var key: Variant = _eval(pair[0], frame)
		if _failed():
			return false
		var value: Variant = _eval(pair[1], frame)
		if _failed():
			return false
		if reject_bad_key(key, int(node["line"])):
			return false
		table.rawset(key, value)
	return true


# --- tostring ----------------------------------------------------------------

## Lua's tostring(). Tables and functions render with a per-sandbox SERIAL
## rather than a pointer: a machine address would differ between two peers of a
## lockstep match and between two runs on one machine, so any script that
## printed or concatenated one would desync. Serial numbers are handed out on
## first use, in script-visible order, which is deterministic.
func tostring_value(value: Variant) -> String:
	if value == null:
		return "nil"
	if LuaValue.is_number(value):
		return LuaNumber.to_lua_string(float(value))
	if LuaValue.is_string(value):
		return String(value)
	if value is LuaTable:
		return "table: %08d" % _serial_of(value)
	if value is LuaValue.Closure:
		return "function: %08d" % _serial_of(value)
	if value is LuaValue.Builtin:
		return "function: builtin: %s" % value.name
	if value is LuaValue.Userdata:
		# Serial keyed on the PAYLOAD, so the same host object always
		# stringifies the same way even though from_host mints a new wrapper
		# on every conversion (see LuaValue.raw_equal).
		if value.payload is Object:
			return "userdata: %08d" % _serial_of(value.payload)
		return "userdata: %08d" % _serial_of(value)
	return "nil"


## Deterministic identity number for tostring(). Delegates to the sandbox,
## which owns the counter; a released sandbox degrades to 0 rather than
## crashing a script that is stringifying during teardown.
func _serial_of(obj: Object) -> int:
	var box := _box()
	return box.object_serial(obj) if box != null else 0
