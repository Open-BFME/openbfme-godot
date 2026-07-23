class_name AptVm
extends RefCounted
## Tier-1 APT ActionScript bytecode VM.
##
## Clean-room implementation: opcode values, operand encodings, and stack
## semantics are ported/adapted from the local OpenSAGE ActionScript source
## (.private/scratch/opensage-source/.../Gui/Apt/ActionScript/, GPLv3). No
## decompilation material was referenced.
##
## Scope (deliberately bounded): the top-20 opcodes by union movie count from
## the corpus burn-down (d2_opcodes.csv). Every other opcode fails closed:
## it is recorded (opcode value + count) in the cumulative `unimplemented`
## histogram and execution of that script halts cleanly - never skipped
## silently, never a crash.
##
## Host calls (methods, variable/member lookups that leave the script scope,
## and playback controls such as Stop/Play/GotoLabel) route through a host
## interface object. The default RecordingHost records every call and
## returns undefined (null); the real glue surface is a later tier.
##
## Deterministic: no wall clock, no randomness, no engine state.
##
## Value model (tier 1): ActionScript undefined is represented as Godot
## `null`; booleans, integers, floats, and strings map to native Variants;
## script objects (including the variables scope pushed by EA_PushThisVar)
## are ScriptObject instances.

# --- Tier-1 opcode values (OpenSAGE InstructionType) -----------------------
const OP_END := 0x00
const OP_PLAY := 0x06
const OP_STOP := 0x07
const OP_NOT := 0x12
const OP_SET_VARIABLE := 0x1D
const OP_ADD2 := 0x47
const OP_SET_MEMBER := 0x4F
const OP_PUSH_ONE := 0x5A
const OP_PUSH_THIS_VAR := 0x70
const OP_PUSH_TRUE := 0x73
const OP_CONSTANT_POOL := 0x88
const OP_GOTO_LABEL := 0x8C
const OP_PUSH_DATA := 0x96
const OP_BRANCH_IF_TRUE := 0x9D
const OP_PUSH_STRING := 0xA1
const OP_PUSH_CONSTANT_BYTE := 0xA2
const OP_PUSH_VALUE_OF_VAR := 0xAE
const OP_GET_NAMED_MEMBER := 0xAF
const OP_CALL_NAMED_METHOD_POP := 0xB2
const OP_PUSH_BYTE := 0xB5

const IMPLEMENTED_OPCODES: Array[int] = [
	OP_END, OP_PLAY, OP_STOP, OP_NOT, OP_SET_VARIABLE, OP_ADD2,
	OP_SET_MEMBER, OP_PUSH_ONE, OP_PUSH_THIS_VAR, OP_PUSH_TRUE,
	OP_CONSTANT_POOL, OP_GOTO_LABEL, OP_PUSH_DATA, OP_BRANCH_IF_TRUE,
	OP_PUSH_STRING, OP_PUSH_CONSTANT_BYTE, OP_PUSH_VALUE_OF_VAR,
	OP_GET_NAMED_MEMBER, OP_CALL_NAMED_METHOD_POP, OP_PUSH_BYTE,
]

# Opcodes whose operands are 4-byte aligned relative to the byte-space start
# (OpenSAGE InstructionAlignment.IsAligned). Needed even for opcodes outside
# tier 1 so the alignment contract of implemented ones stays exact.
const ALIGNED_OPCODES := {
	0x9B: true, 0x8E: true, 0x88: true, 0x9D: true, 0x99: true, 0x96: true,
	0x83: true, 0x8C: true, 0x87: true, 0x8B: true, 0x81: true, 0x9F: true,
	0x94: true, 0xA1: true, 0xB8: true, 0xA4: true, 0xA6: true, 0xA5: true,
	0xA7: true,
}

# Constant-entry kinds from the APT CONST container (OpenSAGE
# ConstantEntryType / importer sage_apt.parse_apt_constants).
const CONST_KIND_STRING := 1
const CONST_KIND_NONE := 3
const CONST_KIND_REGISTER := 4
const CONST_KIND_BOOLEAN := 5
const CONST_KIND_FLOAT := 6
const CONST_KIND_INTEGER := 7
const CONST_KIND_LOOKUP := 8

const DEFAULT_INSTRUCTION_BUDGET := 100000


## A script-side object: the variables scope and anything the script builds
## locally. Everything else is host territory.
class ScriptObject:
	extends RefCounted
	var object_name: String = ""
	var variables: Dictionary = {}

	func _init(p_name: String = "") -> void:
		object_name = p_name


## Default host interface: records every call and returns undefined (null).
## Later tiers replace this with the real GUI/game glue surface.
class RecordingHost:
	extends RefCounted
	var calls: Array = []

	func call_function(fn_name: String, args: Array) -> Variant:
		_record("call_function", fn_name, args)
		return null

	func call_method(target: String, method_name: String, args: Array) -> Variant:
		_record("call_method", target + "." + method_name, args)
		return null

	func get_variable(var_name: String) -> Variant:
		_record("get_variable", var_name, [])
		return null

	func get_member(target: String, member_name: String) -> Variant:
		_record("get_member", target + "." + member_name, [])
		return null

	func set_member(target: String, member_name: String, value: Variant) -> void:
		_record("set_member", target + "." + member_name, [value])

	func playback(op_name: String, args: Array) -> void:
		_record("playback", op_name, args)

	func _record(kind: String, call_name: String, args: Array) -> void:
		calls.append({"kind": kind, "name": call_name, "args": args})


var host: RefCounted = RecordingHost.new()
## Cumulative fail-closed histogram: opcode value -> times encountered.
var unimplemented: Dictionary = {}

var _data: PackedByteArray
var _stack: Array = []
var _scope: ScriptObject
var _global_constants: Array = []
var _active_pool: Array = []
var _fault: String = ""


## Execute raw APT ActionScript bytecode.
##
## bytecode: the addressable byte space. Operand offsets in APT bytecode
##   (strings, constant-index tables) are absolute within the movie's byte
##   space, so pass the whole movie blob and point entry_offset at the
##   script; hand-assembled tests simply use offset 0.
## constants: the movie's CONST entries, each {"type": int, "value": ...}.
## Returns {halted_on, halted_reason, completed, instructions_executed,
##   stack_depth, variables, host_calls, unimplemented}.
func execute(
	bytecode: PackedByteArray,
	constants: Array,
	entry_offset: int = 0,
	max_instructions: int = DEFAULT_INSTRUCTION_BUDGET,
) -> Dictionary:
	_data = bytecode
	_stack = []
	_scope = ScriptObject.new("this")
	_global_constants = constants
	_active_pool = []
	_fault = ""

	var ip := entry_offset
	var executed := 0
	var completed := false
	var halted_on: Variant = null

	while true:
		if executed >= max_instructions:
			_fault = "instruction_budget"
			break
		if ip < 0 or ip >= _data.size():
			_fault = "out_of_bounds"
			break
		var op := _data.decode_u8(ip)
		ip += 1
		if ALIGNED_OPCODES.has(op):
			ip = (ip + 3) & ~3

		match op:
			OP_END:
				executed += 1
				completed = true
				break
			OP_STOP:
				executed += 1
				host.playback("stop", [])
			OP_PLAY:
				executed += 1
				host.playback("play", [])
			OP_GOTO_LABEL:
				if not _require(ip, 4):
					break
				var label_off := _data.decode_u32(ip)
				ip += 4
				var label := _read_cstring(label_off)
				if _fault != "":
					break
				executed += 1
				host.playback("goto_label", [label])
			OP_PUSH_ONE:
				executed += 1
				_stack.push_back(1)
			OP_PUSH_TRUE:
				executed += 1
				_stack.push_back(true)
			OP_PUSH_THIS_VAR:
				executed += 1
				_stack.push_back(_scope)
			OP_PUSH_BYTE:
				if not _require(ip, 1):
					break
				executed += 1
				_stack.push_back(int(_data.decode_u8(ip)))
				ip += 1
			OP_PUSH_STRING:
				if not _require(ip, 4):
					break
				var str_off := _data.decode_u32(ip)
				ip += 4
				var text := _read_cstring(str_off)
				if _fault != "":
					break
				executed += 1
				_stack.push_back(text)
			OP_NOT:
				var not_val: Variant = _pop()
				if _fault != "":
					break
				var flag := _to_boolean(not_val)
				if _fault != "":
					break
				executed += 1
				_stack.push_back(not flag)
			OP_ADD2:
				var add_a: Variant = _pop()
				var add_b: Variant = _pop()
				if _fault != "":
					break
				executed += 1
				if _is_numeric(add_a) and _is_numeric(add_b):
					_stack.push_back(_to_integer(add_b) + _to_integer(add_a))
				else:
					_stack.push_back(_as_string(add_b) + _as_string(add_a))
			OP_BRANCH_IF_TRUE:
				if not _require(ip, 4):
					break
				var branch_off := _data.decode_s32(ip)
				ip += 4
				var cond: Variant = _pop()
				if _fault != "":
					break
				var taken := _to_boolean(cond)
				if _fault != "":
					break
				executed += 1
				if taken:
					# Offset is relative to the next instruction start.
					ip += branch_off
			OP_CONSTANT_POOL:
				var pool: Array = _read_constant_refs(ip)
				ip += 8
				if _fault != "":
					break
				executed += 1
				_active_pool = pool
			OP_PUSH_DATA:
				var pushed: Array = _read_constant_refs(ip)
				ip += 8
				if _fault != "":
					break
				executed += 1
				for value in pushed:
					_stack.push_back(value)
			OP_PUSH_CONSTANT_BYTE:
				if not _require(ip, 1):
					break
				var const_id := int(_data.decode_u8(ip))
				ip += 1
				if const_id >= _active_pool.size():
					_fault = "bad_constant"
					break
				executed += 1
				_stack.push_back(_active_pool[const_id])
			OP_PUSH_VALUE_OF_VAR:
				if not _require(ip, 1):
					break
				var var_id := int(_data.decode_u8(ip))
				ip += 1
				if var_id >= _active_pool.size():
					_fault = "bad_constant"
					break
				var var_name := _as_string(_active_pool[var_id])
				executed += 1
				if _scope.variables.has(var_name):
					_stack.push_back(_scope.variables[var_name])
				else:
					_stack.push_back(host.get_variable(var_name))
			OP_SET_VARIABLE:
				var set_value: Variant = _pop()
				var set_name_v: Variant = _pop()
				if _fault != "":
					break
				executed += 1
				_scope.variables[_as_string(set_name_v)] = set_value
			OP_SET_MEMBER:
				var member_value: Variant = _pop()
				var member_name_v: Variant = _pop()
				var member_obj: Variant = _pop()
				if _fault != "":
					break
				executed += 1
				var member_name := _as_string(member_name_v)
				if member_obj is ScriptObject:
					member_obj.variables[member_name] = member_value
				else:
					host.set_member(
						_describe(member_obj), member_name, _jsonable(member_value))
			OP_GET_NAMED_MEMBER:
				if not _require(ip, 1):
					break
				var get_id := int(_data.decode_u8(ip))
				ip += 1
				if get_id >= _active_pool.size():
					_fault = "bad_constant"
					break
				var get_name := _as_string(_active_pool[get_id])
				var get_obj: Variant = _pop()
				if _fault != "":
					break
				executed += 1
				if get_obj is ScriptObject and get_obj.variables.has(get_name):
					_stack.push_back(get_obj.variables[get_name])
				else:
					_stack.push_back(host.get_member(_describe(get_obj), get_name))
			OP_CALL_NAMED_METHOD_POP:
				if not _require(ip, 1):
					break
				var method_id := int(_data.decode_u8(ip))
				ip += 1
				if method_id >= _active_pool.size():
					_fault = "bad_constant"
					break
				var method_name := _as_string(_active_pool[method_id])
				var call_obj: Variant = _pop()
				var argc_v: Variant = _pop()
				if _fault != "":
					break
				var argc := _to_integer(argc_v)
				if _fault != "":
					break
				var args: Array = []
				for _i in range(argc):
					args.append(_jsonable(_pop()))
					if _fault != "":
						break
				if _fault != "":
					break
				executed += 1
				host.call_method(_describe(call_obj), method_name, args)
			_:
				# Fail closed: record and halt this script cleanly.
				unimplemented[op] = int(unimplemented.get(op, 0)) + 1
				halted_on = op
				_fault = "unimplemented_opcode"
				break
		if _fault != "":
			break

	var host_calls: Array = []
	if host is RecordingHost:
		host_calls = (host as RecordingHost).calls.duplicate(true)

	var variables_out := {}
	for key in _scope.variables:
		variables_out[key] = _jsonable(_scope.variables[key])

	return {
		"completed": completed,
		"halted_on": halted_on,
		"halted_reason": "end" if completed else _fault,
		"instructions_executed": executed,
		"stack_depth": _stack.size(),
		"variables": variables_out,
		"host_calls": host_calls,
		"unimplemented": unimplemented.duplicate(true),
	}


# --- Decode helpers ---------------------------------------------------------

func _require(offset: int, byte_count: int) -> bool:
	if offset < 0 or offset + byte_count > _data.size():
		_fault = "out_of_bounds"
		return false
	return true


func _read_cstring(offset: int) -> String:
	if offset < 0 or offset >= _data.size():
		_fault = "out_of_bounds"
		return ""
	var end := offset
	while end < _data.size() and _data.decode_u8(end) != 0:
		end += 1
	if end >= _data.size():
		_fault = "out_of_bounds"
		return ""
	return _data.slice(offset, end).get_string_from_utf8()


## ConstantPool / PushData operand: u32 count then u32 absolute table offset
## of u32 indices into the movie's CONST entries; each index resolves to a
## value (OpenSAGE Value.ResolveConstant).
func _read_constant_refs(offset: int) -> Array:
	var out: Array = []
	if not _require(offset, 8):
		return out
	var count := _data.decode_u32(offset)
	var table := _data.decode_u32(offset + 4)
	if count > 0x10000 or not _require(table, int(count) * 4):
		_fault = "bad_constant"
		return out
	for i in range(count):
		var index := _data.decode_u32(table + i * 4)
		out.append(_resolve_global_constant(int(index)))
		if _fault != "":
			return []
	return out


func _resolve_global_constant(index: int) -> Variant:
	if index < 0 or index >= _global_constants.size():
		_fault = "bad_constant"
		return null
	var entry: Dictionary = _global_constants[index]
	var kind := int(entry.get("type", -1))
	match kind:
		CONST_KIND_STRING:
			return String(entry.get("value", ""))
		CONST_KIND_NONE:
			return null
		CONST_KIND_BOOLEAN:
			return bool(entry.get("value", false))
		CONST_KIND_FLOAT:
			return float(entry.get("value", 0.0))
		CONST_KIND_INTEGER, CONST_KIND_REGISTER, CONST_KIND_LOOKUP:
			# Registers are outside tier 1; the raw number is retained so the
			# value stays deterministic and printable.
			return int(entry.get("value", 0))
		_:
			_fault = "bad_constant"
			return null


# --- Value model ------------------------------------------------------------

func _pop() -> Variant:
	if _stack.is_empty():
		_fault = "stack_underflow"
		return null
	return _stack.pop_back()


func _is_numeric(value: Variant) -> bool:
	# Booleans are intentionally not numeric (OpenSAGE Value.IsNumericType).
	return (value is int and not (value is bool)) or value is float


func _to_boolean(value: Variant) -> bool:
	if value == null:
		return false
	if value is bool:
		return value
	if value is int:
		return value != 0
	if value is float:
		return value != 0.0
	if value is String:
		# ECMA ToBoolean adaptation; kept total so real scripts keep running.
		return value != ""
	if value is ScriptObject:
		return true
	_fault = "type_error"
	return false


func _to_number(value: Variant) -> float:
	if value == null:
		return NAN
	if value is bool:
		return 1.0 if value else 0.0
	if value is int or value is float:
		return float(value)
	if value is String:
		if value.is_valid_float():
			return value.to_float()
		return NAN
	_fault = "type_error"
	return NAN


## ECMA 9.4 ToInteger (OpenSAGE Value.ToInteger): NaN -> 0, truncate to zero.
func _to_integer(value: Variant) -> int:
	var number := _to_number(value)
	if is_nan(number):
		return 0
	return int(number)


func _as_string(value: Variant) -> String:
	if value == null:
		return ""
	if value is String:
		return value
	if value is bool:
		return "true" if value else "false"
	if value is int:
		return str(value)
	if value is float:
		if value == floor(value) and absf(value) < 1e15:
			return str(int(value))
		return str(value)
	if value is ScriptObject:
		return value.object_name
	return str(value)


func _describe(value: Variant) -> String:
	if value == null:
		return "undefined"
	if value is ScriptObject:
		return "[object %s]" % value.object_name
	return _as_string(value)


func _jsonable(value: Variant) -> Variant:
	if value is ScriptObject:
		return _describe(value)
	return value
