extends RefCounted

## Value model for the Lua 4.0.1 VM, plus the non-table runtime object kinds.
##
## LUA 4.0 HAS NO BOOLEAN TYPE. This is the single most important thing to know
## when reading this VM. There are exactly six types - nil, number, string,
## function, userdata, table - and truth is "not nil". Comparison operators
## return the NUMBER 1 for true and nil for false. `true` and `false` are not
## keywords; in a stock 4.0 interpreter they are ordinary (nil) globals, which
## makes `if true then ... end` silently never run. Because silent fallback is
## a recorded defect class in this repo, LuaStdlib installs loud tripwires on
## those two names instead of letting them read as nil.
##
## GDScript representation:
##     nil       -> null
##     number    -> float (always float, never int - see from_host)
##     string    -> String
##     table     -> LuaTable
##     function  -> Closure (script) or Builtin (host)
##     userdata  -> Userdata (opaque host handle)
##
## GDScript `bool`, `int`, Array and Dictionary are NOT Lua values. from_host()
## converts them at the host boundary so that a registered action can `return
## true` or `return 3` without thinking about it.

const LuaTable := preload("res://src/lua/lua_table.gd")
const LuaNumber := preload("res://src/lua/lua_number.gd")

## Default tags, matching lua.h 4.0's LUA_T* enum so that tag() returns the
## numbers a 4.0 script would recognise.
const TAG_USERDATA := 0
const TAG_NIL := 1
const TAG_NUMBER := 2
const TAG_STRING := 3
const TAG_TABLE := 4
const TAG_FUNCTION := 5

## First tag newtag() may hand out (Lua 4.0's NUM_TAGS).
const FIRST_USER_TAG := 6

## Every tag method event name Lua 4.0.1 recognises, in ltm.c's ORDER IM.
const EVENT_NAMES := [
	"gettable", "settable", "index", "getglobal", "setglobal", "add", "sub",
	"mul", "div", "pow", "unm", "lt", "concat", "gc", "function",
]

## Names ltm.c carries but marks "deprecated options!!". Lua 4.0 implements
## a <= b as not (b < a), a > b as b < a and a >= b as not (a < b), so ALL
## ordering goes through "lt" and setting these does nothing useful. Accepting
## them silently would let a script think it had installed an ordering hook
## that never fires, so settagmethod rejects them by name.
const DEPRECATED_EVENT_NAMES := ["le", "gt", "ge"]


## A function written in Lua. Lua 4.0 closures capture NOTHING implicitly - the
## only capture mechanism is the %upvalue syntax, which snapshots a value at
## closure-creation time. `upvalues` is therefore a plain frozen name->value
## dictionary, not a cell/box graph.
class Closure extends RefCounted:
	var proto: Dictionary          ## AST function node from LuaParser
	var upvalues: Dictionary = {}  ## name -> snapshotted value, read-only
	var chunk: String = "?"        ## source name, for error messages
	var name: String = "?"         ## best-effort name, for error messages


## A function supplied by the host through LuaSandbox.register_function.
class Builtin extends RefCounted:
	var name: String = "?"
	var callable: Callable
	## When true the callable is invoked as f(sandbox, args); otherwise f(args).
	var pass_sandbox: bool = false

	func _init(p_name: String, p_callable: Callable, p_pass_sandbox: bool = false) -> void:
		name = p_name
		callable = p_callable
		pass_sandbox = p_pass_sandbox


## An opaque host handle. Scripts can hold one, pass it around and compare it,
## but cannot inspect it - which is what makes it safe to hand a script a
## reference to a game object without exposing Godot's API surface.
class Userdata extends RefCounted:
	var tag: int = 0
	var payload: Variant = null
	var label: String = "userdata"

	func _init(p_payload: Variant = null, p_label: String = "userdata") -> void:
		payload = p_payload
		label = p_label


## A poisoned global. Reading, calling or indexing one raises a named error
## instead of behaving like nil. Used for constructs that exist in Lua 5.x but
## not in 4.0.1 (setmetatable, pairs, the `string`/`table`/`math` module
## tables, true/false) and for host facilities the deterministic sandbox
## refuses to provide (io, os, clock, dofile).
class Tripwire extends RefCounted:
	var label: String = ""
	var detail: String = ""

	func _init(p_label: String, p_detail: String) -> void:
		label = p_label
		detail = p_detail

	func message() -> String:
		return "%s is not available: %s" % [label, detail]


## A contained script error. It never propagates as a GDScript crash; the
## interpreter carries it back to the sandbox boundary, which hands the host a
## result record.
class Err extends RefCounted:
	## Parse/lex failure, or a construct this VM refuses to accept.
	const KIND_SYNTAX := "syntax"
	## Ordinary runtime error, including error() from script.
	const KIND_RUNTIME := "runtime"
	## Step budget exhausted. Deliberately its own kind so a host can tell a
	## runaway script apart from a buggy one.
	const KIND_BUDGET := "budget"
	## A Lua 5.x-ism or a host facility the sandbox withholds.
	const KIND_UNSUPPORTED := "unsupported"

	var kind: String = KIND_RUNTIME
	var message: String = ""
	var chunk: String = "?"
	var line: int = 0
	## Innermost-first call sites, as "chunk:line in <name>".
	var traceback: Array[String] = []
	## The raw value passed to error(), which need not be a string.
	var value: Variant = null

	func _init(p_kind: String, p_message: String, p_chunk: String = "?", p_line: int = 0) -> void:
		kind = p_kind
		message = p_message
		chunk = p_chunk
		line = p_line

	## The one-line form hosts log and tests match on.
	func text() -> String:
		return "%s:%d: %s" % [chunk, line, message]

	func full_text() -> String:
		var out := "[%s] %s" % [kind, text()]
		for frame in traceback:
			out += "\n\tat %s" % frame
		return out


static func type_name(value: Variant) -> String:
	if value == null:
		return "nil"
	if value is float or value is int:
		return "number"
	if value is String or value is StringName:
		return "string"
	if value is LuaTable:
		return "table"
	if value is Closure or value is Builtin:
		return "function"
	if value is Userdata:
		return "userdata"
	if value is Tripwire:
		return "nil"
	return "userdata"


static func default_tag(value: Variant) -> int:
	if value == null:
		return TAG_NIL
	if value is float or value is int:
		return TAG_NUMBER
	if value is String or value is StringName:
		return TAG_STRING
	if value is LuaTable:
		return TAG_TABLE
	if value is Closure or value is Builtin:
		return TAG_FUNCTION
	return TAG_USERDATA


## Lua 4.0 truth: everything except nil is true. Zero and "" are TRUE.
static func truthy(value: Variant) -> bool:
	return value != null


## The Lua value for a GDScript truth. There is no boolean type, so true is the
## number 1 and false is nil.
static func from_bool(flag: bool) -> Variant:
	return 1.0 if flag else null


static func is_number(value: Variant) -> bool:
	return value is float or value is int


static func is_string(value: Variant) -> bool:
	return value is String or value is StringName


static func is_callable_value(value: Variant) -> bool:
	return value is Closure or value is Builtin


## Raw equality (the `==` operator). Lua 4.0 does NOT coerce for equality:
## '0' == 0 is false because the types differ. Tables, functions and userdata
## compare by identity.
static func raw_equal(a: Variant, b: Variant) -> bool:
	if a == null and b == null:
		return true
	if a == null or b == null:
		return false
	var ta := type_name(a)
	if ta != type_name(b):
		return false
	match ta:
		"number":
			return float(a) == float(b)
		"string":
			return String(a) == String(b)
		"userdata":
			# A userdata's identity is the HOST OBJECT's identity, not the
			# wrapper's. from_host() mints a fresh Userdata every time it
			# converts, so comparing wrappers would make the same game object
			# handed to a script twice look like two different things.
			if a is Userdata and b is Userdata:
				if a.payload == null and b.payload == null:
					return a == b
				return a.payload == b.payload
			return a == b
		_:
			return a == b


## String/number coercion used by arithmetic (Section 4.2 of the 4.0 manual):
## an arithmetic operand that is a string is converted to a number if it looks
## like one. Returns null when no conversion is possible.
static func coerce_to_number(value: Variant) -> Variant:
	if value is float:
		return value
	if value is int:
		return float(value)
	if value is String or value is StringName:
		return LuaNumber.parse(String(value))
	return null


## The reverse coercion: a number used where a string is expected. Returns null
## for values that are neither, so callers can raise a typed error.
static func coerce_to_string(value: Variant) -> Variant:
	if value is String:
		return value
	if value is StringName:
		return String(value)
	if value is float:
		return LuaNumber.to_lua_string(value)
	if value is int:
		return LuaNumber.to_lua_string(float(value))
	return null


## Converts a GDScript value handed over by host code into a Lua value.
##
## This is the ergonomic seam for the action/condition dispatch tables: a
## registered handler may return `true`, `42`, `"text"`, an Array or a
## Dictionary and get something a script can actually use.
##
##     null                 -> nil
##     bool                 -> 1 / nil            (Lua 4.0 has no booleans)
##     int / float          -> number
##     String / StringName  -> string
##     Array                -> table with keys 1..n and an `n` field, matching
##                             the Lua 4.0 convention that array length lives
##                             in `n` (see getn/tinsert)
##     Dictionary           -> table with the same keys, recursively converted
##     LuaTable/Closure/... -> unchanged
##     anything else        -> Userdata wrapper, so a Node or Resource can be
##                             passed through a script without being reachable
##
## NOTE: an Array returned by a registered function at the TOP level means
## "multiple return values" and is intercepted before this function is reached
## (see LuaInterp._call_builtin). Nested arrays become tables as documented.
static func from_host(value: Variant) -> Variant:
	if value == null:
		return null
	if value is bool:
		return from_bool(value)
	if value is int:
		return float(value)
	if value is float:
		return value
	if value is StringName:
		return String(value)
	if value is String:
		return value
	if value is LuaTable or value is Closure or value is Builtin \
			or value is Userdata or value is Tripwire:
		return value
	if value is Array:
		var array_table := LuaTable.new()
		for i in range(value.size()):
			array_table.rawset(float(i + 1), from_host(value[i]))
		array_table.rawset("n", float(value.size()))
		return array_table
	if value is Dictionary:
		var dict_table := LuaTable.new()
		# Sorted key order so the resulting table's deterministic iteration
		# order does not inherit the host Dictionary's insertion order, which
		# a caller may have built non-deterministically.
		var keys: Array = value.keys()
		keys.sort_custom(func(a, b): return str(a) < str(b))
		for key in keys:
			dict_table.rawset(from_host(key), from_host(value[key]))
		return dict_table
	return Userdata.new(value, type_name(value))


## Converts a Lua value into plain GDScript for host consumption. Tables become
## Dictionaries with their deterministic key order preserved; cycles are cut
## with the string "<cycle>" rather than hanging.
static func to_host(value: Variant, _seen: Array = []) -> Variant:
	if value is LuaTable:
		for entry in _seen:
			if entry == value:
				return "<cycle>"
		var seen := _seen.duplicate()
		seen.append(value)
		var out := {}
		for key in value.keys_in_order():
			out[to_host(key, seen)] = to_host(value.rawget(key), seen)
		return out
	if value is Closure:
		return "function: %s" % value.name
	if value is Builtin:
		return "function: %s" % value.name
	if value is Userdata:
		return value.payload
	return value
