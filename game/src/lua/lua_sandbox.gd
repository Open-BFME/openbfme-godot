extends RefCounted

## A Lua 4.0.1 sandbox: one isolated script environment.
##
## ============================================================================
## HOST INTERFACE - this is the seam the action/condition dispatch tables bind
## to. Binding ~474 game actions must never require editing the VM.
## ============================================================================
##
## Create one sandbox per scripted object (or per map, or per player - whatever
## granularity you want isolated), register what that object is allowed to
## touch, then run script against it:
##
##     const LuaSandbox := preload("res://src/lua/lua_sandbox.gd")
##
##     var box := LuaSandbox.new("orc_warg_rider")
##     box.register_function("SetUnitHealth", _set_unit_health)
##     box.register_function("GetUnitHealth", _get_unit_health)
##     var outcome := box.do_string("SetUnitHealth('Legolas', 50)", "onCreate")
##     if not outcome.ok:
##         push_error(outcome.error)      # already "chunk:line: message"
##
## --- registering host functions ---------------------------------------------
##
##   register_function(name: String, callable: Callable,
##                     pass_sandbox: bool = false) -> void
##
##     The callable's signature is
##         func(args: Array) -> Variant                  (pass_sandbox false)
##         func(box: LuaSandbox, args: Array) -> Variant (pass_sandbox true)
##
##     `args` holds the Lua values the script passed, already normalised:
##     numbers are float, strings are String, tables are LuaTable, absent
##     arguments simply are not there (check args.size()).
##
##     RETURN VALUE, by GDScript type:
##         Array          -> MULTIPLE Lua return values, converted element-wise.
##                           Return [] for "no results".
##         LuaValue.Err   -> raise a script error (build one with box.error()).
##         bool           -> 1 / nil. Lua 4.0 has no boolean type.
##         int / float    -> number
##         String         -> string
##         Dictionary     -> table (keys visited in sorted order, so the
##                           resulting table's iteration order is deterministic
##                           even if the host Dictionary's was not)
##         null           -> nil
##         anything else  -> an opaque Userdata handle the script can hold and
##                           pass back but cannot inspect
##
##     Registration is per sandbox. Two sandboxes never share globals, tag
##     methods, tags or registered functions - see the isolation tests in
##     tests/lua_host_runner.gd.
##
##   register_functions(mapping: Dictionary, pass_sandbox := false)
##       Bulk form: {"ActionName": callable, ...}. This is the one a generated
##       dispatch table should call.
##
## --- calling back into script ------------------------------------------------
##
##   call_function(name: String, args: Array = []) -> Dictionary
##       Calls a global script function by name with a FRESH step budget.
##
##   do_string(source: String, chunk_name := "=chunk") -> Dictionary
##   compile(source: String, chunk_name := "=chunk") -> Dictionary
##       compile() returns a reusable closure under "closure"; call it later
##       with call_closure() to avoid re-parsing every tick.
##
##   Every one of those returns a RESULT RECORD:
##       {
##         "ok":    bool,
##         "values": Array,       # Lua return values
##         "error": String,       # "chunk:line: message", "" when ok
##         "error_kind": String,  # "syntax" | "runtime" | "budget" | "unsupported"
##         "error_line": int,
##         "error_chunk": String,
##         "err":   LuaValue.Err, # the object, for structured handling
##         "steps": int,          # budget consumed
##       }
##
## --- containment guarantees --------------------------------------------------
##
##   * A script error NEVER crashes the host. It is carried as a value to the
##     sandbox boundary and handed back in the result record.
##   * step_budget caps work per invocation. Exceeding it yields error_kind
##     "budget" and the distinctive message LUA_STEP_BUDGET_EXCEEDED. A
##     protected call (call(f, args, "x")) deliberately CANNOT swallow a budget
##     error - otherwise a runaway script could catch its own leash.
##   * max_call_depth stops runaway recursion before GDScript's stack does,
##     because a blown GDScript stack is a host crash, not a script error.
##   * No filesystem, no OS, no clock, no Godot API is reachable from script.
##     Names such as io/os/clock/dofile exist as tripwires that explain the
##     refusal rather than reading as nil.

const LuaValue := preload("res://src/lua/lua_value.gd")
const LuaTable := preload("res://src/lua/lua_table.gd")
const LuaParser := preload("res://src/lua/lua_parser.gd")
const LuaInterp := preload("res://src/lua/lua_interp.gd")
const LuaStdlib := preload("res://src/lua/lua_stdlib.gd")

## Emitted for every print() and for host-visible diagnostics.
signal output(line: String)

## Steps a single invocation may consume. One step is one AST node evaluated or
## one statement executed. Set to 0 to disable (NOT advisable inside a sim).
var step_budget: int = 200000

## Hard cap on nested Lua calls, checked before GDScript's own stack limit.
var max_call_depth: int = 190

## Ceiling on a single strrep() result, so a script cannot exhaust host memory
## with one call.
var max_string_length: int = 1 << 20

## Backtracking budget for one strfind/gsub. Lua patterns can be exponential.
var pattern_step_budget: int = 1 << 20

## When true, print() also reaches the Godot console. The `output` signal fires
## either way.
var echo_print: bool = false

## Human-readable name for this sandbox, used in default chunk names.
var name: String = "lua"

## Globals and tag methods live here, but the SAME instances are handed to the
## interpreter (see LuaInterp.globals / tag_methods) so the hot path reads them
## without a hop through this object. Both fields must therefore be mutated in
## place, never reassigned - except through set_globals_table, which updates
## both sides.
var _globals: LuaTable = null
## tag (int) -> {event name -> function value}
var _tag_methods: Dictionary = {}
var _next_tag: int = LuaValue.FIRST_USER_TAG
var _interp: LuaInterp = null
## The standard library instance. It MUST be held here: a Godot Callable stores
## only its target's ObjectID, not a strong reference, so the Builtins in the
## globals table would not keep it alive and every stdlib call would fail with
## "its object was freed". Holding it is safe because LuaStdlib is stateless -
## it points at nothing, so this reference closes no cycle.
var _stdlib: LuaStdlib = null
## Object -> serial, for deterministic tostring(table). Holds a strong
## reference, so only objects a script actually stringified are retained.
var _serials: Dictionary = {}
var _next_serial: int = 1
var _rng_state: int = 0
var _output_lines: PackedStringArray = PackedStringArray()


func _init(p_name: String = "lua", install_stdlib: bool = true) -> void:
	name = p_name
	_globals = LuaTable.new()
	_interp = LuaInterp.new(self)
	_interp.globals = _globals
	_interp.tag_methods = _tag_methods
	seed_random(0)
	if install_stdlib:
		_stdlib = LuaStdlib.new()
		_stdlib.install(self)


# =============================================================================
# Host registration
# =============================================================================

## Registers a host function under `name`. See the file header for the exact
## argument and return contract.
func register_function(function_name: String, callable: Callable,
		pass_sandbox: bool = false) -> void:
	_globals.rawset(function_name,
		LuaValue.Builtin.new(function_name, callable, pass_sandbox))


## Bulk registration: {"ActionName": Callable, ...}. Intended for generated
## dispatch tables.
func register_functions(mapping: Dictionary, pass_sandbox: bool = false) -> void:
	for function_name in mapping:
		register_function(String(function_name), mapping[function_name], pass_sandbox)


func unregister_function(function_name: String) -> void:
	_globals.rawset(function_name, null)


func has_function(function_name: String) -> bool:
	return LuaValue.is_callable_value(_globals.rawget(function_name))


## Sets a global from host code. The value is converted with LuaValue.from_host,
## so a Dictionary becomes a table and a bool becomes 1/nil.
func set_global(global_name: String, value: Variant) -> void:
	_globals.rawset(global_name, LuaValue.from_host(value))


## Reads a global as a raw Lua value (no getglobal tag method, no conversion).
func get_global(global_name: String) -> Variant:
	return _globals.rawget(global_name)


## Reads a global converted to plain GDScript (tables become Dictionaries).
func get_global_as_host(global_name: String) -> Variant:
	return LuaValue.to_host(_globals.rawget(global_name))


## Builds an empty Lua table for host code that wants to hand one to a script.
func new_table() -> LuaTable:
	return LuaTable.new()


# =============================================================================
# Running script
# =============================================================================

## Compiles without running. Returns a result record whose "closure" holds a
## reusable LuaValue.Closure on success.
func compile(source: String, chunk_name: String = "") -> Dictionary:
	var chunk := chunk_name if chunk_name != "" else name
	var parser := LuaParser.new(chunk)
	var proto: Variant = parser.parse(source)
	if proto == null:
		return _failure_record(parser.error, 0)
	var closure := LuaValue.Closure.new()
	closure.proto = proto
	closure.chunk = chunk
	closure.name = "chunk '%s'" % chunk
	var record := _success_record([], 0)
	record["closure"] = closure
	return record


## Compiles and runs `source` with a fresh step budget.
func do_string(source: String, chunk_name: String = "") -> Dictionary:
	var compiled := compile(source, chunk_name)
	if not bool(compiled["ok"]):
		return compiled
	return call_closure(compiled["closure"], [])


## Runs an already-compiled closure with a fresh step budget.
func call_closure(closure: LuaValue.Closure, args: Array = []) -> Dictionary:
	var converted: Array = []
	for value in args:
		converted.append(LuaValue.from_host(value))
	var outcome := _interp.run(closure, converted, step_budget, max_call_depth)
	if not bool(outcome["ok"]):
		return _failure_record(outcome["error"], int(outcome["steps"]))
	return _success_record(outcome["values"], int(outcome["steps"]))


## Calls a global script function by name, with a fresh step budget. This is
## the host->script direction: event hooks, map triggers, object callbacks.
func call_function(function_name: String, args: Array = []) -> Dictionary:
	var target: Variant = _globals.rawget(function_name)
	if not LuaValue.is_callable_value(target):
		var err := LuaValue.Err.new(LuaValue.Err.KIND_RUNTIME,
			"no script function named '%s' (found a %s)"
			% [function_name, LuaValue.type_name(target)], name, 0)
		return _failure_record(err, 0)
	var converted: Array = []
	for value in args:
		converted.append(LuaValue.from_host(value))
	var outcome := _interp.run(target, converted, step_budget, max_call_depth)
	if not bool(outcome["ok"]):
		return _failure_record(outcome["error"], int(outcome["steps"]))
	return _success_record(outcome["values"], int(outcome["steps"]))


# =============================================================================
# Services used by the interpreter and the standard library
# =============================================================================

func globals_table() -> LuaTable:
	return _globals


## Backs globals(t). Replacing the globals table is how a host gives one script
## a narrower environment than another without a second sandbox. The
## interpreter holds the same reference and must be repointed with us.
func set_globals_table(table: LuaTable) -> void:
	_globals = table
	_interp.globals = table


func interpreter() -> LuaInterp:
	return _interp


func tag_method(tag: int, event: String) -> Variant:
	var events: Variant = _tag_methods.get(tag)
	if events == null:
		return null
	return events.get(event)


func set_tag_method(tag: int, event: String, method: Variant) -> void:
	if not _tag_methods.has(tag):
		_tag_methods[tag] = {}
	if method == null:
		_tag_methods[tag].erase(event)
	else:
		_tag_methods[tag][event] = method


func copy_tag_methods(to_tag: int, from_tag: int) -> void:
	var source: Variant = _tag_methods.get(from_tag)
	if source == null:
		_tag_methods.erase(to_tag)
		return
	_tag_methods[to_tag] = source.duplicate()


func new_tag() -> int:
	var tag := _next_tag
	_next_tag += 1
	return tag


## Calls a Lua value during an active run. Used by foreach, sort and gsub.
func call_value(fn: Variant, args: Array) -> Array:
	return _interp.call_value(fn, args)


## Protected variant used by call(f, args, "x") and dostring.
func call_value_protected(fn: Variant, args: Array) -> Dictionary:
	return _interp.call_protected(fn, args)


## dostring() inside an already-running script: shares the budget and the frame
## stack rather than resetting them.
func do_string_nested(source: String, chunk_name: String) -> Dictionary:
	var compiled := compile(source, chunk_name)
	if not bool(compiled["ok"]):
		return compiled
	var outcome := _interp.call_protected(compiled["closure"], [])
	if not bool(outcome["ok"]):
		return _failure_record(outcome["error"], _interp.steps_used())
	return _success_record(outcome["values"], _interp.steps_used())


func has_error() -> bool:
	return _interp.current_error() != null


func current_error() -> LuaValue.Err:
	return _interp.current_error()


## Builds a contained script error. Host functions signal failure by RETURNING
## the value this produces.
func error(message: String) -> LuaValue.Err:
	return _interp.make_error(message)


func tostring_value(value: Variant) -> String:
	return _interp.tostring_value(value)


## Stable identity for tostring(table). A pointer would differ between peers of
## a lockstep match; a serial handed out in script-visible order does not.
func object_serial(obj: Object) -> int:
	if _serials.has(obj):
		return int(_serials[obj])
	var serial := _next_serial
	_next_serial += 1
	_serials[obj] = serial
	return serial


func emit_output(line: String) -> void:
	_output_lines.append(line)
	if echo_print:
		print("[%s] %s" % [name, line])
	output.emit(line)


## Everything print() has produced since the last clear.
func output_lines() -> PackedStringArray:
	return _output_lines


func clear_output() -> void:
	_output_lines = PackedStringArray()


# =============================================================================
# Deterministic PRNG
# =============================================================================

## random()/randomseed() are backed by a sandbox-owned xorshift64*, NOT by the
## host's RNG and NOT by libc rand(). Two peers seeded alike produce identical
## streams on every platform, which is the whole point; the trade is that the
## numbers differ from what retail Lua 4.01 would produce for the same seed.
func seed_random(value: int) -> void:
	# 0 is a fixed point of xorshift, so it is mapped to a fixed odd constant.
	_rng_state = value if value != 0 else 0x2545F4914F6CDD1D
	for _warmup in range(4):
		_advance_random()


func next_random() -> float:
	# Top 53 bits give a double in [0, 1) with no rounding surprises, matching
	# how a double's mantissa is sized.
	return float(_logical_shift_right(_advance_random(), 11)) / 9007199254740992.0


func _advance_random() -> int:
	var x := _rng_state
	x ^= x << 13
	x ^= _logical_shift_right(x, 7)
	x ^= x << 17
	_rng_state = x
	return x


## GDScript's >> is an ARITHMETIC shift, which sign-extends and would break the
## xorshift recurrence for negative states. Masking restores a logical shift.
static func _logical_shift_right(value: int, bits: int) -> int:
	return (value >> bits) & ((1 << (64 - bits)) - 1)


# =============================================================================
# Result records
# =============================================================================

## Releases everything this sandbox owns.
##
## Calling it is OPTIONAL: the VM itself forms no reference cycles, so a
## sandbox that simply goes out of scope is freed. Two things can still keep
## memory alive, and dispose() is the answer to both:
##
##   1. A host function registered as a LAMBDA THAT CAPTURES THE SANDBOX -
##      globals -> Builtin -> Callable -> lambda -> sandbox is a cycle, and
##      GDScript's RefCounted does not collect cycles. Prefer
##      register_function(name, callable, true), which hands the sandbox to the
##      callable instead of capturing it; use dispose() when capture is
##      genuinely unavoidable.
##   2. A script that builds CYCLIC DATA (t.self = t). Real Lua has a garbage
##      collector for this; RefCounted does not.
##
## After dispose() the sandbox must not be used again.
func dispose() -> void:
	_globals = LuaTable.new()
	_tag_methods.clear()
	_serials.clear()
	_output_lines = PackedStringArray()
	if _interp != null:
		_interp.globals = _globals
	_interp = null
	_stdlib = null


func _success_record(values: Array, steps: int) -> Dictionary:
	return {
		"ok": true, "values": values, "error": "", "error_kind": "",
		"error_line": 0, "error_chunk": "", "err": null, "steps": steps,
	}


func _failure_record(err: LuaValue.Err, steps: int) -> Dictionary:
	if err == null:
		err = LuaValue.Err.new(LuaValue.Err.KIND_RUNTIME, "unknown error", name, 0)
	return {
		"ok": false, "values": [], "error": err.text(), "error_kind": err.kind,
		"error_line": err.line, "error_chunk": err.chunk, "err": err,
		"steps": steps,
	}
