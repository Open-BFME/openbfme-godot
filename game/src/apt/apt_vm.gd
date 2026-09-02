class_name AptVm
extends RefCounted
## Tier-2 APT ActionScript bytecode VM.
##
## Clean-room implementation: opcode values, operand encodings, and stack
## semantics are ported/adapted from the local OpenSAGE ActionScript source
## (workspace/scratch/opensage-source/.../Gui/Apt/ActionScript/, GPLv3). No
## decompilation material was referenced. The 0xB7 (EA_PushLong) operand
## width (little-endian Int32) comes from the corpus width validation in the
## APT inventory report (b7_width_validation.csv), not from any SWF table.
##
## Scope (tier 2): all 87 opcode values measured in the BFME2+RotWK corpus
## burn-down (d2_opcodes.csv). Every other opcode fails closed: it is
## recorded (opcode value + count) in the cumulative `unimplemented`
## histogram and execution of that script halts cleanly - never skipped
## silently, never a crash.
##
## Host calls (function/method calls that leave the script scope, variable
## and member lookups outside script objects, playback controls, URL and
## fscommand dispatch, frame properties, sprite management, trace, and
## randomness) route through a host interface object. The default
## RecordingHost records every call and returns undefined (null); the real
## GUI/game glue surface is a later tier.
##
## Deterministic: no wall clock, no randomness (Random routes through the
## host), no engine state.
##
## Value model: ActionScript undefined is Godot `null`; ActionScript null is
## the AsNull sentinel; booleans, integers, floats, and strings map to
## native Variants; arrays are Godot Arrays; script-defined objects are
## ScriptObject instances; script-defined functions are ScriptFunction
## instances; register-kind constants resolve through RegisterRef at pop
## time (OpenSAGE Value.ResolveRegister).

# --- Opcode values (OpenSAGE InstructionType) -------------------------------
const OP_END := 0x00
const OP_NEXT_FRAME := 0x04
const OP_PLAY := 0x06
const OP_STOP := 0x07
const OP_SUBTRACT := 0x0B
const OP_MULTIPLY := 0x0C
const OP_DIVIDE := 0x0D
const OP_NOT := 0x12
const OP_STRING_EQUALS := 0x13
const OP_POP := 0x17
const OP_TO_INTEGER := 0x18
const OP_GET_VARIABLE := 0x1C
const OP_SET_VARIABLE := 0x1D
const OP_STRING_CONCAT := 0x21
const OP_GET_PROPERTY := 0x22
const OP_SET_PROPERTY := 0x23
const OP_CLONE_SPRITE := 0x24
const OP_REMOVE_SPRITE := 0x25
const OP_TRACE := 0x26
const OP_RANDOM := 0x30
const OP_DELETE := 0x3A
const OP_DELETE2 := 0x3B
const OP_DEFINE_LOCAL := 0x3C
const OP_CALL_FUNCTION := 0x3D
const OP_RETURN := 0x3E
const OP_MODULO := 0x3F
const OP_NEW_OBJECT := 0x40
const OP_VAR := 0x41
const OP_INIT_ARRAY := 0x42
const OP_INIT_OBJECT := 0x43
const OP_TYPE_OF := 0x44
const OP_ADD2 := 0x47
const OP_LESS_THAN2 := 0x48
const OP_EQUALS2 := 0x49
const OP_TO_NUMBER := 0x4A
const OP_TO_STRING := 0x4B
const OP_PUSH_DUPLICATE := 0x4C
const OP_GET_MEMBER := 0x4E
const OP_SET_MEMBER := 0x4F
const OP_INCREMENT := 0x50
const OP_DECREMENT := 0x51
const OP_CALL_METHOD := 0x52
const OP_ENUMERATE2 := 0x55
const OP_PUSH_THIS := 0x56
const OP_PUSH_ZERO := 0x59
const OP_PUSH_ONE := 0x5A
const OP_CALL_FUNC_POP := 0x5B
const OP_CALL_METHOD_POP := 0x5D
const OP_EA_CALL_METHOD := 0x5E
const OP_BITWISE_AND := 0x60
const OP_SHIFT_RIGHT := 0x64
const OP_GREATER := 0x67
const OP_PUSH_THIS_VAR := 0x70
const OP_PUSH_GLOBAL_VAR := 0x71
const OP_ZERO_VAR := 0x72
const OP_PUSH_TRUE := 0x73
const OP_PUSH_FALSE := 0x74
const OP_PUSH_NULL := 0x75
const OP_PUSH_UNDEFINED := 0x76
const OP_GOTO_FRAME := 0x81
const OP_GET_URL := 0x83
const OP_SET_REGISTER := 0x87
const OP_CONSTANT_POOL := 0x88
const OP_GOTO_LABEL := 0x8C
const OP_DEFINE_FUNCTION2 := 0x8E
const OP_PUSH_DATA := 0x96
const OP_BRANCH_ALWAYS := 0x99
const OP_GET_URL2 := 0x9A
const OP_DEFINE_FUNCTION := 0x9B
const OP_BRANCH_IF_TRUE := 0x9D
const OP_GOTO_FRAME2 := 0x9F
const OP_PUSH_STRING := 0xA1
const OP_PUSH_CONSTANT_BYTE := 0xA2
const OP_GET_STRING_VAR := 0xA4
const OP_GET_STRING_MEMBER := 0xA5
const OP_SET_STRING_VAR := 0xA6
const OP_SET_STRING_MEMBER := 0xA7
const OP_PUSH_VALUE_OF_VAR := 0xAE
const OP_GET_NAMED_MEMBER := 0xAF
const OP_CALL_NAMED_FUNC_POP := 0xB0
const OP_CALL_NAMED_FUNC := 0xB1
const OP_CALL_NAMED_METHOD_POP := 0xB2
const OP_CALL_NAMED_METHOD := 0xB3
const OP_PUSH_FLOAT := 0xB4
const OP_PUSH_BYTE := 0xB5
const OP_PUSH_SHORT := 0xB6
const OP_PUSH_LONG := 0xB7

## Every opcode value present in the corpus burn-down (d2_opcodes.csv):
## all 87 are implemented; anything else fails closed.
const IMPLEMENTED_OPCODES: Array[int] = [
	0x00, 0x04, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x12, 0x13, 0x17, 0x18, 0x1C,
	0x1D, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x30, 0x3A, 0x3B, 0x3C, 0x3D,
	0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x49, 0x4A, 0x4B,
	0x4C, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x55, 0x56, 0x59, 0x5A, 0x5B, 0x5D,
	0x5E, 0x60, 0x64, 0x67, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x81,
	0x83, 0x87, 0x88, 0x8C, 0x8E, 0x96, 0x99, 0x9A, 0x9B, 0x9D, 0x9F, 0xA1,
	0xA2, 0xA4, 0xA5, 0xA6, 0xA7, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4,
	0xB5, 0xB6, 0xB7,
]

# Opcodes whose operands are 4-byte aligned relative to the byte-space start
# (OpenSAGE InstructionAlignment.IsAligned).
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

# Function preload flags (OpenSAGE FunctionPreloadFlags; EA layout).
const PRELOAD_EXTERN := 0x010000
const PRELOAD_PARENT := 0x008000
const PRELOAD_ROOT := 0x004000
const SUPRESS_SUPER := 0x002000
const PRELOAD_SUPER := 0x001000
const SUPRESS_ARGUMENTS := 0x000800
const PRELOAD_ARGUMENTS := 0x000400
const SUPRESS_THIS := 0x000200
const PRELOAD_THIS := 0x000100
const PRELOAD_GLOBAL := 0x000001

const DEFAULT_INSTRUCTION_BUDGET := 100000
const MAX_CALL_DEPTH := 24
const MAX_REGISTERS := 256
const MAX_FUNCTION_PARAMS := 256


## A script-side object: the variables scope and anything the script builds
## locally. Everything else is host territory.
class ScriptObject:
	extends RefCounted
	var object_name: String = ""
	var variables: Dictionary = {}

	func _init(p_name: String = "") -> void:
		object_name = p_name


## A script-defined function (DefineFunction / DefineFunction2). Captures
## the constant pool active at definition time (OpenSAGE Function.Constants)
## and the body byte range inside the movie byte space.
class ScriptFunction:
	extends RefCounted
	var function_name: String = ""
	## Old-style: parameter name Strings. New-style: flat [register:int,
	## name:String] pairs (OpenSAGE Function.Parameters layout).
	var parameters: Array = []
	var body_offset: int = 0
	var body_size: int = 0
	var register_count: int = 4
	var flags: int = 0
	var is_new_version: bool = false
	var pool: Array = []


## ActionScript `null` sentinel (distinct from undefined, which is Godot
## null; OpenSAGE Value.FromObject(null)).
class AsNull:
	extends RefCounted


## A register-kind constant reference; resolves at pop/peek time (OpenSAGE
## Value.ResolveRegister).
class RegisterRef:
	extends RefCounted
	var index: int = 0

	func _init(p_index: int = 0) -> void:
		index = p_index


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

	func delete_member(target: String, member_name: String) -> void:
		_record("delete_member", target + "." + member_name, [])

	func playback(op_name: String, args: Array) -> void:
		_record("playback", op_name, args)

	## Frame property access (GetProperty/SetProperty target paths).
	func get_property(target: String, property_index: int) -> Variant:
		_record("get_property", target + "#%d" % property_index, [])
		return null

	func set_property(target: String, property_index: int, value: Variant) -> void:
		_record("set_property", target + "#%d" % property_index, [value])

	## GetURL/GetURL2 dispatch (OpenSAGE UrlHandler): FSCommand: urls arrive
	## via fs_command, everything else via get_url.
	func fs_command(command: String, arg: String) -> void:
		_record("fs_command", command, [arg])

	func get_url(url: String, target: String) -> void:
		_record("get_url", url, [target])

	func trace_message(message: String) -> void:
		_record("trace", message, [])

	## NewObject constructor for non-script classes. Return null to let the
	## VM substitute a plain ScriptObject.
	func construct_object(type_name: String, args: Array) -> Variant:
		_record("construct_object", type_name, args)
		return null

	## Enumerate2 slot names of a host-side object.
	func enumerate(target: String) -> Array:
		_record("enumerate", target, [])
		return []

	## Random opcode: randomness is host territory so the VM itself stays
	## deterministic. Default: always 0.
	func random_int(max_exclusive: int) -> int:
		_record("random_int", str(max_exclusive), [])
		return 0

	func _record(kind: String, call_name: String, args: Array) -> void:
		calls.append({"kind": kind, "name": call_name, "args": args})


var host: RefCounted = RecordingHost.new()
## Cumulative fail-closed histogram: opcode value -> times encountered.
var unimplemented: Dictionary = {}

var _data: PackedByteArray
var _stack: Array = []
var _scope: ScriptObject
var _global: ScriptObject
var _root: ScriptObject
var _extern: ScriptObject
var _locals: Dictionary = {}
var _params: Dictionary = {}
var _registers: Array = []
var _global_constants: Array = []
var _active_pool: Array = []
var _as_null: AsNull = AsNull.new()
var _fault: String = ""
var _halted_on: Variant = null
var _executed: int = 0
var _budget: int = 0
var _returning: bool = false
var _return_value: Variant = null
var _executing: bool = false


## Execute raw APT ActionScript bytecode.
##
## bytecode: the addressable byte space. Operand offsets in APT bytecode
##   (strings, constant-index tables, function parameter tables) are
##   absolute within the movie's byte space, so pass the whole movie blob
##   and point entry_offset at the script; hand-assembled tests use offset 0.
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
	_global = ScriptObject.new("_global")
	_root = ScriptObject.new("_root")
	_extern = ScriptObject.new("extern")
	_locals = {}
	_params = {}
	_registers = [null, null, null, null]
	_global_constants = constants
	_active_pool = []
	_fault = ""
	_halted_on = null
	_executed = 0
	_budget = max_instructions
	_returning = false
	_return_value = null

	_executing = true
	var completed := _run_frame(entry_offset, 0)
	_executing = false

	var host_calls: Array = []
	if host is RecordingHost:
		host_calls = (host as RecordingHost).calls.duplicate(true)

	var variables_out := {}
	for key in _scope.variables:
		variables_out[key] = _jsonable(_scope.variables[key])

	var reason: String
	if completed:
		reason = "return" if _returning else "end"
	else:
		reason = _fault

	return {
		"completed": completed,
		"halted_on": _halted_on,
		"halted_reason": reason,
		"instructions_executed": _executed,
		"stack_depth": _stack.size(),
		"variables": variables_out,
		"host_calls": host_calls,
		"unimplemented": unimplemented.duplicate(true),
	}


# --- Interpreter core ---------------------------------------------------------

## Run one instruction stream (root script or function body) until End,
## Return, or a fault. Returns true on clean End/Return. Function bodies
## additionally end cleanly at their declared byte boundary (end_ip):
## retail APT bodies are bounded by DefineFunction/2 body size and carry no
## trailing End opcode, so falling off the boundary is a bare return - never
## a walk into the enclosing instruction stream.
func _run_frame(entry_ip: int, depth: int, end_ip: int = -1) -> bool:
	var ip := entry_ip
	while true:
		if _executed >= _budget:
			_fault = "instruction_budget"
			return false
		if end_ip >= 0 and ip >= end_ip:
			return true
		if ip < 0 or ip >= _data.size():
			_fault = "out_of_bounds"
			return false
		var op := _data.decode_u8(ip)
		ip += 1
		if ALIGNED_OPCODES.has(op):
			ip = (ip + 3) & ~3

		match op:
			OP_END:
				_executed += 1
				return true
			OP_RETURN:
				_executed += 1
				_returning = true
				# OpenSAGE pops the pushed return value after the Return flag
				# is seen; a bare return leaves undefined.
				_return_value = _stack.pop_back() if not _stack.is_empty() else null
				_return_value = _resolve(_return_value)
				return true

			# --- Playback ----------------------------------------------------
			OP_STOP:
				_executed += 1
				host.playback("stop", [])
			OP_PLAY:
				_executed += 1
				host.playback("play", [])
			OP_NEXT_FRAME:
				_executed += 1
				host.playback("next_frame", [])
			OP_GOTO_LABEL:
				if not _require(ip, 4):
					return false
				var label_off := _data.decode_u32(ip)
				ip += 4
				var label := _read_cstring(label_off)
				if _fault != "":
					return false
				_executed += 1
				host.playback("goto_label", [label])
			OP_GOTO_FRAME:
				if not _require(ip, 4):
					return false
				var frame := _data.decode_s32(ip)
				ip += 4
				_executed += 1
				host.playback("goto_frame", [frame])
			OP_GOTO_FRAME2:
				# Aligned i32 flags; bit 0 = play after the jump. The frame
				# (label string or number) comes from the stack.
				if not _require(ip, 4):
					return false
				var gf2_flags := _data.decode_s32(ip)
				ip += 4
				var gf2_frame: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				host.playback("goto_frame2", [_jsonable(gf2_frame), gf2_flags & 1])
			OP_CLONE_SPRITE:
				var cs_depth: Variant = _pop()
				var cs_target: Variant = _pop()
				var cs_source: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				host.playback("clone_sprite",
					[_jsonable(cs_source), _jsonable(cs_target), _jsonable(cs_depth)])
			OP_REMOVE_SPRITE:
				var rs_target: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				host.playback("remove_sprite", [_jsonable(rs_target)])

			# --- URL / engine commands ---------------------------------------
			OP_GET_URL:
				# Aligned; two u32 absolute string offsets: url, target.
				if not _require(ip, 8):
					return false
				var u_url := _read_cstring(_data.decode_u32(ip))
				var u_target := _read_cstring(_data.decode_u32(ip + 4))
				ip += 8
				if _fault != "":
					return false
				_executed += 1
				_dispatch_url(u_url, u_target)
			OP_GET_URL2:
				var u2_target: Variant = _pop()
				var u2_url: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_dispatch_url(_as_string(u2_url), _as_string(u2_target))
			OP_TRACE:
				var trace_val: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				host.trace_message(_as_string(trace_val))
			OP_RANDOM:
				# Randomness is delegated to the host to keep the VM
				# deterministic (OpenSAGE uses System.Random here).
				var rnd_max := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(int(host.random_int(rnd_max)))

			# --- Stack pushes -------------------------------------------------
			OP_PUSH_ZERO:
				_executed += 1
				_stack.push_back(0)
			OP_PUSH_ONE:
				_executed += 1
				_stack.push_back(1)
			OP_PUSH_TRUE:
				_executed += 1
				_stack.push_back(true)
			OP_PUSH_FALSE:
				_executed += 1
				_stack.push_back(false)
			OP_PUSH_NULL:
				_executed += 1
				_stack.push_back(_as_null)
			OP_PUSH_UNDEFINED:
				_executed += 1
				_stack.push_back(null)
			OP_PUSH_THIS, OP_PUSH_THIS_VAR:
				_executed += 1
				_stack.push_back(_scope)
			OP_PUSH_GLOBAL_VAR:
				_executed += 1
				_stack.push_back(_global)
			OP_PUSH_BYTE:
				if not _require(ip, 1):
					return false
				_executed += 1
				_stack.push_back(int(_data.decode_u8(ip)))
				ip += 1
			OP_PUSH_SHORT:
				if not _require(ip, 2):
					return false
				_executed += 1
				_stack.push_back(int(_data.decode_u16(ip)))
				ip += 2
			OP_PUSH_LONG:
				# Little-endian Int32 (corpus-validated width; see header).
				if not _require(ip, 4):
					return false
				_executed += 1
				_stack.push_back(int(_data.decode_s32(ip)))
				ip += 4
			OP_PUSH_FLOAT:
				if not _require(ip, 4):
					return false
				_executed += 1
				_stack.push_back(float(_data.decode_float(ip)))
				ip += 4
			OP_PUSH_STRING:
				if not _require(ip, 4):
					return false
				var str_off := _data.decode_u32(ip)
				ip += 4
				var text := _read_cstring(str_off)
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(text)
			OP_PUSH_DUPLICATE:
				var dup_val: Variant = _peek()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(dup_val)
			OP_POP:
				_pop()
				if _fault != "":
					return false
				_executed += 1

			# --- Arithmetic / bitwise ----------------------------------------
			OP_NOT:
				var not_val: Variant = _pop()
				if _fault != "":
					return false
				var flag := _to_boolean(not_val)
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(not flag)
			OP_ADD2:
				var add_a: Variant = _pop()
				var add_b: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				if _is_numeric(add_a) and _is_numeric(add_b):
					_stack.push_back(_to_integer(add_b) + _to_integer(add_a))
				else:
					_stack.push_back(_as_string(add_b) + _as_string(add_a))
			OP_SUBTRACT:
				var sub_a := _to_number(_pop())
				var sub_b := _to_number(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(sub_b - sub_a)
			OP_MULTIPLY:
				var mul_a := _to_number(_pop())
				var mul_b := _to_number(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(mul_b * mul_a)
			OP_DIVIDE:
				var div_a := _to_number(_pop())
				var div_b := _to_number(_pop())
				if _fault != "":
					return false
				_executed += 1
				if div_a == 0.0:
					if is_nan(div_b) or div_b == 0.0:
						_stack.push_back(NAN)
					else:
						_stack.push_back(INF if div_b > 0.0 else -INF)
				else:
					_stack.push_back(div_b / div_a)
			OP_MODULO:
				var mod_a := _to_number(_pop())
				var mod_b := _to_number(_pop())
				if _fault != "":
					return false
				_executed += 1
				if mod_a == 0.0 or is_nan(mod_a) or is_nan(mod_b):
					_stack.push_back(NAN)
				else:
					_stack.push_back(fmod(mod_b, mod_a))
			OP_INCREMENT:
				var inc := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(inc + 1)
			OP_DECREMENT:
				var dec := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(dec - 1)
			OP_BITWISE_AND:
				var and_a := _to_integer(_pop())
				var and_b := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(and_b & and_a)
			OP_SHIFT_RIGHT:
				# Arithmetic right shift of b by (a & 31).
				var shr_count := _to_integer(_pop())
				var shr_value := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(shr_value >> (shr_count & 31))
			OP_TO_INTEGER:
				var toi: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_to_integer(toi))
			OP_TO_NUMBER:
				# ECMA-flavored total conversion (OpenSAGE int-parses the
				# string, which is partial; adapted to stay total).
				var ton: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_to_number(ton))
			OP_TO_STRING:
				var tos: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_as_string(tos))

			# --- Comparisons ---------------------------------------------------
			OP_EQUALS2:
				var eq_a: Variant = _pop()
				var eq_b: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_abstract_equals(eq_a, eq_b))
			OP_LESS_THAN2:
				# ECMA 11.8.5 via OpenSAGE: NaN operands push undefined.
				var lt_a := _to_number(_pop())
				var lt_b := _to_number(_pop())
				if _fault != "":
					return false
				_executed += 1
				if is_nan(lt_a) or is_nan(lt_b):
					_stack.push_back(null)
				else:
					_stack.push_back(lt_b < lt_a)
			OP_GREATER:
				var gt_a := _to_integer(_pop())
				var gt_b := _to_integer(_pop())
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(gt_b > gt_a)
			OP_STRING_EQUALS:
				var seq_a: Variant = _pop()
				var seq_b: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_as_string(seq_b) == _as_string(seq_a))
			OP_STRING_CONCAT:
				var sc_a: Variant = _pop()
				var sc_b: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_as_string(sc_b) + _as_string(sc_a))
			OP_TYPE_OF:
				var tv: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_type_of(tv))

			# --- Branches ------------------------------------------------------
			OP_BRANCH_IF_TRUE:
				if not _require(ip, 4):
					return false
				var branch_off := _data.decode_s32(ip)
				ip += 4
				var cond: Variant = _pop()
				if _fault != "":
					return false
				var taken := _to_boolean(cond)
				if _fault != "":
					return false
				_executed += 1
				if taken:
					# Offset is relative to the next instruction start.
					ip += branch_off
			OP_BRANCH_ALWAYS:
				if not _require(ip, 4):
					return false
				var always_off := _data.decode_s32(ip)
				ip += 4
				_executed += 1
				ip += always_off

			# --- Constant pool -------------------------------------------------
			OP_CONSTANT_POOL:
				var pool: Array = _read_constant_refs(ip)
				ip += 8
				if _fault != "":
					return false
				_executed += 1
				_active_pool = pool
			OP_PUSH_DATA:
				var pushed: Array = _read_constant_refs(ip)
				ip += 8
				if _fault != "":
					return false
				_executed += 1
				for value in pushed:
					_stack.push_back(value)
			OP_PUSH_CONSTANT_BYTE:
				if not _require(ip, 1):
					return false
				var const_id := int(_data.decode_u8(ip))
				ip += 1
				if const_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				_executed += 1
				_stack.push_back(_active_pool[const_id])

			# --- Registers -----------------------------------------------------
			OP_SET_REGISTER:
				# Aligned i32 register id; value stays on the stack (peek).
				if not _require(ip, 4):
					return false
				var reg_id := _data.decode_s32(ip)
				ip += 4
				var reg_val: Variant = _peek()
				if _fault != "":
					return false
				if reg_id < 0 or reg_id >= MAX_REGISTERS:
					_fault = "bad_register"
					return false
				while _registers.size() <= reg_id:
					_registers.append(null)
				_executed += 1
				_registers[reg_id] = reg_val

			# --- Variables ------------------------------------------------------
			OP_PUSH_VALUE_OF_VAR:
				if not _require(ip, 1):
					return false
				var var_id := int(_data.decode_u8(ip))
				ip += 1
				if var_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var var_name := _as_string(_resolve(_active_pool[var_id]))
				_executed += 1
				_stack.push_back(_lookup_variable(var_name))
			OP_GET_VARIABLE:
				var gv_name_v: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_lookup_variable(_as_string(gv_name_v)))
			OP_SET_VARIABLE:
				var set_value: Variant = _pop()
				var set_name_v: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				var set_name := _as_string(set_name_v)
				# Locals shadow scope variables (OpenSAGE SetVariable).
				if _locals.has(set_name):
					_locals[set_name] = set_value
				else:
					_scope.variables[set_name] = set_value
			OP_DEFINE_LOCAL:
				var dl_value: Variant = _pop()
				var dl_name_v: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_locals[_as_string(dl_name_v)] = dl_value
			OP_ZERO_VAR:
				var zv_name: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_scope.variables[_as_string(zv_name)] = 0
			OP_GET_STRING_VAR:
				# Aligned u32 string offset: the variable name.
				if not _require(ip, 4):
					return false
				var gsv_name := _read_cstring(_data.decode_u32(ip))
				ip += 4
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_lookup_variable(gsv_name))
			OP_SET_STRING_VAR:
				# Aligned u32 string offset: the VALUE; the name is popped
				# (OpenSAGE SetStringVar).
				if not _require(ip, 4):
					return false
				var ssv_value := _read_cstring(_data.decode_u32(ip))
				ip += 4
				var ssv_name: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_scope.variables[_as_string(ssv_name)] = ssv_value
			OP_DELETE2:
				var d2_name_v: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				var d2_name := _as_string(d2_name_v)
				if _locals.has(d2_name):
					_locals.erase(d2_name)
				else:
					_scope.variables.erase(d2_name)
			OP_VAR:
				# Declaration-only opcode; no observable effect (OpenSAGE Var).
				_executed += 1

			# --- Members / objects ----------------------------------------------
			OP_SET_MEMBER:
				var member_value: Variant = _pop()
				var member_name_v: Variant = _pop()
				var member_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_set_member(member_obj, _as_string(member_name_v), member_value)
			OP_GET_MEMBER:
				var gm_name_v: Variant = _pop()
				var gm_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_get_member(gm_obj, _as_string(gm_name_v)))
			OP_GET_NAMED_MEMBER:
				if not _require(ip, 1):
					return false
				var get_id := int(_data.decode_u8(ip))
				ip += 1
				if get_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var get_name := _as_string(_resolve(_active_pool[get_id]))
				var get_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_get_member(get_obj, get_name))
			OP_GET_STRING_MEMBER:
				# Aligned u32 string offset: the member name; object popped.
				if not _require(ip, 4):
					return false
				var gsm_name := _read_cstring(_data.decode_u32(ip))
				ip += 4
				var gsm_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_get_member(gsm_obj, gsm_name))
			OP_SET_STRING_MEMBER:
				# Aligned u32 string offset: the VALUE; member name and object
				# are popped (OpenSAGE SetStringMember).
				if not _require(ip, 4):
					return false
				var ssm_value := _read_cstring(_data.decode_u32(ip))
				ip += 4
				var ssm_name: Variant = _pop()
				var ssm_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_set_member(ssm_obj, _as_string(ssm_name), ssm_value)
			OP_DELETE:
				# Pops member name then object (adapted: OpenSAGE's Delete
				# body is empty, which would corrupt the stack).
				var del_name_v: Variant = _pop()
				var del_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				var del_name := _as_string(del_name_v)
				if del_obj is ScriptObject:
					del_obj.variables.erase(del_name)
				else:
					host.delete_member(_describe(del_obj), del_name)
			OP_NEW_OBJECT:
				var no_name := _as_string(_pop())
				var no_argc := _to_integer(_pop())
				if _fault != "":
					return false
				if no_argc < 0 or no_argc > MAX_FUNCTION_PARAMS:
					_fault = "bad_argc"
					return false
				var no_args: Array = []
				for _i in range(no_argc):
					no_args.append(_jsonable(_pop()))
					if _fault != "":
						return false
				_executed += 1
				var built: Variant = host.construct_object(no_name, no_args)
				if built == null:
					# Unknown host class: substitute a plain script object so
					# the script can keep attaching members.
					built = ScriptObject.new(no_name)
				_stack.push_back(built)
			OP_INIT_ARRAY:
				var ia_argc := _to_integer(_pop())
				if _fault != "":
					return false
				if ia_argc < 0 or ia_argc > MAX_FUNCTION_PARAMS:
					_fault = "bad_argc"
					return false
				var arr: Array = []
				for _i in range(ia_argc):
					arr.append(_pop())
					if _fault != "":
						return false
				_executed += 1
				_stack.push_back(arr)
			OP_INIT_OBJECT:
				var io_argc := _to_integer(_pop())
				if _fault != "":
					return false
				if io_argc < 0 or io_argc > MAX_FUNCTION_PARAMS:
					_fault = "bad_argc"
					return false
				var io_obj := ScriptObject.new("object")
				for _i in range(io_argc):
					var io_value: Variant = _pop()
					var io_name: Variant = _pop()
					if _fault != "":
						return false
					io_obj.variables[_as_string(io_name)] = io_value
				_executed += 1
				_stack.push_back(io_obj)
			OP_ENUMERATE2:
				var en_obj: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				# Terminator first (AS null), then one name per slot.
				_stack.push_back(_as_null)
				if en_obj is ScriptObject:
					for slot_name in en_obj.variables:
						_stack.push_back(String(slot_name))
				else:
					for slot_name in host.enumerate(_describe(en_obj)):
						_stack.push_back(String(slot_name))

			# --- Frame properties ------------------------------------------------
			OP_GET_PROPERTY:
				var gp_index := _to_integer(_pop())
				var gp_target: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(host.get_property(_as_string(gp_target), gp_index))
			OP_SET_PROPERTY:
				var sp_value: Variant = _pop()
				var sp_index := _to_integer(_pop())
				var sp_target: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				host.set_property(_as_string(sp_target), sp_index, _jsonable(sp_value))

			# --- Function definition ----------------------------------------------
			OP_DEFINE_FUNCTION:
				# Aligned 24-byte operand: u32 name offset, u32 param count,
				# u32 param table offset (u32 string offsets), i32 body size,
				# 8 unused bytes. Body bytes follow the operand.
				if not _require(ip, 24):
					return false
				var df_name := _read_cstring(_data.decode_u32(ip))
				var df_nparams := int(_data.decode_u32(ip + 4))
				var df_table := _data.decode_u32(ip + 8)
				var df_size := _data.decode_s32(ip + 12)
				ip += 24
				if _fault != "":
					return false
				if df_nparams > MAX_FUNCTION_PARAMS \
						or (df_nparams > 0 and not _require(df_table, df_nparams * 4)):
					_fault = "bad_function"
					return false
				var df_params: Array = []
				for pi in range(df_nparams):
					df_params.append(_read_cstring(_data.decode_u32(df_table + pi * 4)))
					if _fault != "":
						return false
				if df_size < 0 or ip + df_size > _data.size():
					_fault = "bad_function"
					return false
				var fn := ScriptFunction.new()
				fn.function_name = df_name
				fn.parameters = df_params
				fn.body_offset = ip
				fn.body_size = df_size
				fn.register_count = 4
				fn.is_new_version = false
				fn.pool = _active_pool.duplicate()
				ip += df_size
				_executed += 1
				if df_name.length() > 0:
					_scope.variables[df_name] = fn
					_notify_function_defined(df_name, fn)
				else:
					_stack.push_back(fn)
			OP_DEFINE_FUNCTION2:
				# Aligned 28-byte operand: u32 name offset, u32 param count,
				# u8 register count, u24 flags, u32 param table offset (each
				# entry i32 register + u32 string offset), i32 body size,
				# 8 unused bytes. Body bytes follow the operand.
				if not _require(ip, 28):
					return false
				var d2_name := _read_cstring(_data.decode_u32(ip))
				var d2_nparams := int(_data.decode_u32(ip + 4))
				var d2_nregs := int(_data.decode_u8(ip + 8))
				var d2_flags := int(_data.decode_u8(ip + 9)) \
					| (int(_data.decode_u8(ip + 10)) << 8) \
					| (int(_data.decode_u8(ip + 11)) << 16)
				var d2_table := _data.decode_u32(ip + 12)
				var d2_size := _data.decode_s32(ip + 16)
				ip += 28
				if _fault != "":
					return false
				if d2_nparams > MAX_FUNCTION_PARAMS \
						or (d2_nparams > 0 and not _require(d2_table, d2_nparams * 8)):
					_fault = "bad_function"
					return false
				var d2_params: Array = []
				for pi in range(d2_nparams):
					d2_params.append(int(_data.decode_s32(d2_table + pi * 8)))
					d2_params.append(_read_cstring(_data.decode_u32(d2_table + pi * 8 + 4)))
					if _fault != "":
						return false
				if d2_size < 0 or ip + d2_size > _data.size():
					_fault = "bad_function"
					return false
				var fn2 := ScriptFunction.new()
				fn2.function_name = d2_name
				fn2.parameters = d2_params
				fn2.body_offset = ip
				fn2.body_size = d2_size
				fn2.register_count = maxi(4, d2_nregs)
				fn2.flags = d2_flags
				fn2.is_new_version = true
				fn2.pool = _active_pool.duplicate()
				ip += d2_size
				_executed += 1
				if d2_name.length() > 0:
					_scope.variables[d2_name] = fn2
					_notify_function_defined(d2_name, fn2)
				else:
					_stack.push_back(fn2)

			# --- Function / method calls -------------------------------------------
			OP_CALL_FUNCTION:
				var cf_name := _as_string(_pop())
				var cf_args := _pop_call_args()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_call_named_function(cf_name, cf_args, depth))
				if _fault != "":
					return false
			OP_CALL_FUNC_POP:
				var cfp_name := _as_string(_pop())
				var cfp_args := _pop_call_args()
				if _fault != "":
					return false
				_executed += 1
				_call_named_function(cfp_name, cfp_args, depth)
				if _fault != "":
					return false
			OP_CALL_NAMED_FUNC_POP:
				if not _require(ip, 1):
					return false
				var cnfp_id := int(_data.decode_u8(ip))
				ip += 1
				if cnfp_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var cnfp_name := _as_string(_resolve(_active_pool[cnfp_id]))
				var cnfp_args := _pop_call_args()
				if _fault != "":
					return false
				_executed += 1
				_call_named_function(cnfp_name, cnfp_args, depth)
				if _fault != "":
					return false
			OP_CALL_NAMED_FUNC:
				if not _require(ip, 1):
					return false
				var cnf_id := int(_data.decode_u8(ip))
				ip += 1
				if cnf_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var cnf_name := _as_string(_resolve(_active_pool[cnf_id]))
				var cnf_args := _pop_call_args()
				if _fault != "":
					return false
				_executed += 1
				_stack.push_back(_call_named_function(cnf_name, cnf_args, depth))
				if _fault != "":
					return false
			OP_CALL_NAMED_METHOD_POP:
				if not _require(ip, 1):
					return false
				var method_id := int(_data.decode_u8(ip))
				ip += 1
				if method_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var method_name := _as_string(_resolve(_active_pool[method_id]))
				var call_obj: Variant = _pop()
				var cm_args := _pop_call_args()
				if _fault != "":
					return false
				_executed += 1
				_call_method_on(call_obj, method_name, cm_args, depth)
				if _fault != "":
					return false
			OP_CALL_NAMED_METHOD:
				# Calls the method, then stores the result into a local whose
				# name is popped from the stack (OpenSAGE CallNamedMethod).
				if not _require(ip, 1):
					return false
				var cnm_id := int(_data.decode_u8(ip))
				ip += 1
				if cnm_id >= _active_pool.size():
					_fault = "bad_constant"
					return false
				var cnm_name := _as_string(_resolve(_active_pool[cnm_id]))
				var cnm_obj: Variant = _pop()
				var cnm_args := _pop_call_args()
				if _fault != "":
					return false
				var cnm_result: Variant = _call_method_on(cnm_obj, cnm_name, cnm_args, depth)
				if _fault != "":
					return false
				var cnm_var: Variant = _pop()
				if _fault != "":
					return false
				_executed += 1
				_locals[_as_string(cnm_var)] = cnm_result
			OP_CALL_METHOD_POP, OP_CALL_METHOD, OP_EA_CALL_METHOD:
				# Method name popped first. Empty name means the function
				# value itself is next on the stack (OpenSAGE CallMethod /
				# CallMethodPop). Non-Pop variants push the result.
				var m_name := _as_string(_pop())
				if _fault != "":
					return false
				var m_result: Variant = null
				if m_name.length() > 0:
					var m_obj: Variant = _pop()
					var m_args := _pop_call_args()
					if _fault != "":
						return false
					m_result = _call_method_on(m_obj, m_name, m_args, depth)
				else:
					var m_fn: Variant = _pop()
					var m_args := _pop_call_args()
					if _fault != "":
						return false
					if m_fn is ScriptFunction:
						m_result = _invoke_function(m_fn, m_args, _scope, depth)
					else:
						m_result = host.call_function(_describe(m_fn), m_args)
				if _fault != "":
					return false
				_executed += 1
				if op != OP_CALL_METHOD_POP:
					_stack.push_back(m_result)

			_:
				# Fail closed: record and halt this script cleanly.
				unimplemented[op] = int(unimplemented.get(op, 0)) + 1
				_halted_on = op
				_fault = "unimplemented_opcode"
				return false

		if _fault != "":
			return false
	return false


# --- Call helpers -------------------------------------------------------------

## Tier-4 hook: a named DefineFunction/DefineFunction2 executed at clip scope
## is also offered to the host so it can register clip-attached handlers
## (measured families only; the host fails closed on the rest). Hosts that do
## not implement the hook (RecordingHost) are unaffected.
func _notify_function_defined(fn_name: String, fn: ScriptFunction) -> void:
	if host.has_method("on_function_defined"):
		host.on_function_defined(fn_name, fn, self)


## Tier-4 public dispatch: execute a previously defined ScriptFunction whose
## body lives in this VM's byte space. Re-entrant safe: when called from
## inside a host call during execute(), the frame save/restore in
## _invoke_function keeps the outer frame intact and a fault propagates
## through _fault as usual. When called detached (after execute returned),
## the budget and fault state are reset first so repeated dispatches stay
## independent and deterministic.
func call_registered_function(fn: ScriptFunction, args: Array) -> Dictionary:
	var detached := not _executing
	if detached:
		_executed = 0
		_budget = DEFAULT_INSTRUCTION_BUDGET
		_fault = ""
		_halted_on = null
		_returning = false
		_return_value = null
	var value: Variant = _invoke_function(fn, args, null, 0)
	return {
		"completed": _fault == "",
		"fault": _fault,
		"value": _jsonable(value),
	}

## Pop the argument count and then the arguments (OpenSAGE
## FunctionCommon.GetArgumentsFromStack).
func _pop_call_args() -> Array:
	var argc := _to_integer(_pop())
	if _fault != "":
		return []
	if argc < 0 or argc > MAX_FUNCTION_PARAMS:
		_fault = "bad_argc"
		return []
	var args: Array = []
	for _i in range(argc):
		args.append(_pop())
		if _fault != "":
			return []
	return args


## Look a function up by name (params -> locals -> scope) and call it;
## unknown names go to the host. Returns the result value.
func _call_named_function(fn_name: String, args: Array, depth: int) -> Variant:
	var fn: Variant = null
	if _params.has(fn_name):
		fn = _params[fn_name]
	elif _locals.has(fn_name):
		fn = _locals[fn_name]
	elif _scope.variables.has(fn_name):
		fn = _scope.variables[fn_name]
	if fn is ScriptFunction:
		return _invoke_function(fn, args, _scope, depth)
	return host.call_function(fn_name, _jsonable_args(args))


## Call a named method on an object: script-defined members execute in the
## VM, everything else goes to the host.
func _call_method_on(obj: Variant, method_name: String, args: Array, depth: int) -> Variant:
	if obj is ScriptObject and obj.variables.has(method_name) \
			and obj.variables[method_name] is ScriptFunction:
		return _invoke_function(obj.variables[method_name], args, obj, depth)
	return host.call_method(_describe(obj), method_name, _jsonable_args(args))


## Execute a ScriptFunction body in a fresh frame (own stack, locals,
## params, and registers; OpenSAGE VM.Execute). Returns the return value.
func _invoke_function(fn: ScriptFunction, args: Array, this_obj: Variant, depth: int) -> Variant:
	if depth + 1 >= MAX_CALL_DEPTH:
		_fault = "call_depth"
		return null
	# Save the caller frame.
	var saved_stack := _stack
	var saved_locals := _locals
	var saved_params := _params
	var saved_registers := _registers
	var saved_pool := _active_pool
	var saved_scope := _scope
	var saved_returning := _returning
	var saved_return_value: Variant = _return_value

	_stack = []
	_locals = {}
	_params = {}
	_registers = []
	_registers.resize(maxi(4, fn.register_count))
	_active_pool = fn.pool
	if this_obj is ScriptObject:
		_scope = this_obj
	_returning = false
	_return_value = null

	if not fn.is_new_version:
		for i in range(fn.parameters.size()):
			var pname := String(fn.parameters[i])
			_params[pname] = args[i] if i < args.size() else null
	else:
		var pair_count := fn.parameters.size() / 2
		for i in range(pair_count):
			var reg := int(fn.parameters[i * 2])
			var pname := String(fn.parameters[i * 2 + 1])
			var arg: Variant = args[i] if i < args.size() else null
			if reg != 0 and reg < _registers.size():
				_registers[reg] = arg
			else:
				_params[pname] = arg
		_preload_registers(fn.flags, args)

	var completed := _run_frame(fn.body_offset, depth + 1, fn.body_offset + fn.body_size)
	var ret: Variant = null
	if completed and _returning:
		ret = _return_value

	# Restore the caller frame (a fault propagates through _fault).
	_stack = saved_stack
	_locals = saved_locals
	_params = saved_params
	_registers = saved_registers
	_active_pool = saved_pool
	_scope = saved_scope
	_returning = saved_returning
	_return_value = saved_return_value
	return ret


## DefineFunction2 register preloads, in OpenSAGE ActionContext.Preload
## order (this, arguments, super, root, parent, global, extern). Registers
## start at 1. Suppress flags simply skip the implicit `this` local.
func _preload_registers(flags: int, args: Array) -> void:
	var reg := 1
	if flags & PRELOAD_THIS:
		if reg < _registers.size():
			_registers[reg] = _scope
		reg += 1
	if flags & PRELOAD_ARGUMENTS:
		if reg < _registers.size():
			_registers[reg] = args.duplicate()
		reg += 1
	if flags & PRELOAD_SUPER:
		# Prototype chains are outside tier 2; super preloads undefined.
		reg += 1
	if flags & PRELOAD_ROOT:
		if reg < _registers.size():
			_registers[reg] = _root
		reg += 1
	if flags & PRELOAD_PARENT:
		# No display hierarchy in the VM; parent preloads undefined.
		reg += 1
	if flags & PRELOAD_GLOBAL:
		if reg < _registers.size():
			_registers[reg] = _global
		reg += 1
	if flags & PRELOAD_EXTERN:
		if reg < _registers.size():
			_registers[reg] = _extern
		reg += 1
	if not (flags & SUPRESS_THIS):
		_locals["this"] = _scope


# --- Variable / member helpers --------------------------------------------------

## Params -> locals -> scope variables -> host (OpenSAGE PushValueOfVar
## lookup order, with the host as the out-of-scope fallback).
func _lookup_variable(var_name: String) -> Variant:
	if _params.has(var_name):
		return _params[var_name]
	if _locals.has(var_name):
		return _locals[var_name]
	if _scope.variables.has(var_name):
		return _scope.variables[var_name]
	return host.get_variable(var_name)


func _get_member(obj: Variant, member_name: String) -> Variant:
	if obj is ScriptObject:
		if obj.variables.has(member_name):
			return obj.variables[member_name]
		return host.get_member(_describe(obj), member_name)
	if obj is Array:
		if member_name == "length":
			return obj.size()
		if member_name.is_valid_int():
			var index := member_name.to_int()
			if index >= 0 and index < obj.size():
				return obj[index]
			return null
		return host.get_member(_describe(obj), member_name)
	return host.get_member(_describe(obj), member_name)


func _set_member(obj: Variant, member_name: String, value: Variant) -> void:
	# Tier-4 raw-byte lane: the retail HUD registers clip handlers by
	# assigning anonymous DefineFunction/2 results to members of `this` or of
	# a host clip path (e.g. `this.SetFlashEffectState = function(state)...`).
	# Offer those to the host so measured handler families can register from
	# the real bytecode body; the host fails closed on unmeasured names.
	# Plain script objects keep pure in-VM member semantics.
	if value is ScriptFunction and host.has_method("on_member_function_assigned") \
			and ((obj is ScriptObject and obj == _scope) or not (obj is ScriptObject or obj is Array)):
		host.on_member_function_assigned(_describe(obj), member_name, value, self)
		if obj is ScriptObject:
			obj.variables[member_name] = value
		return
	if obj is ScriptObject:
		obj.variables[member_name] = value
	elif obj is Array and member_name.is_valid_int():
		var index := member_name.to_int()
		if index >= 0 and index < obj.size():
			obj[index] = value
		elif index >= 0 and index <= obj.size() + MAX_FUNCTION_PARAMS:
			obj.resize(index + 1)
			obj[index] = value
	else:
		host.set_member(_describe(obj), member_name, _jsonable(value))


## GetURL/GetURL2 dispatch (OpenSAGE UrlHandler.Handle).
func _dispatch_url(url: String, target: String) -> void:
	if url.begins_with("FSCommand:"):
		host.fs_command(url.substr(10), target)
	else:
		host.get_url(url, target)


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
		CONST_KIND_REGISTER:
			# Resolves against the live registers at pop time (OpenSAGE
			# Value.FromRegister / ResolveRegister).
			return RegisterRef.new(int(entry.get("value", 0)))
		CONST_KIND_INTEGER, CONST_KIND_LOOKUP:
			return int(entry.get("value", 0))
		_:
			_fault = "bad_constant"
			return null


# --- Value model ------------------------------------------------------------

## Register references resolve when values leave the stack (OpenSAGE
## ActionContext.Pop/Peek). Out-of-range registers resolve to the raw index
## (OpenSAGE Value.ResolveRegister).
func _resolve(value: Variant) -> Variant:
	if value is RegisterRef:
		if value.index >= 0 and value.index < _registers.size():
			return _registers[value.index]
		return value.index
	return value


func _pop() -> Variant:
	if _stack.is_empty():
		_fault = "stack_underflow"
		return null
	return _resolve(_stack.pop_back())


func _peek() -> Variant:
	if _stack.is_empty():
		_fault = "stack_underflow"
		return null
	return _resolve(_stack.back())


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
	if value is AsNull:
		return false
	if value is ScriptObject or value is ScriptFunction or value is Array:
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
	if value is AsNull:
		return 0.0
	if value is ScriptObject or value is ScriptFunction or value is Array:
		return NAN
	_fault = "type_error"
	return NAN


## ECMA 9.4 ToInteger (OpenSAGE Value.ToInteger): NaN -> 0, truncate to zero.
func _to_integer(value: Variant) -> int:
	var number := _to_number(value)
	if is_nan(number) or is_inf(number):
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
		if is_nan(value):
			return "NaN"
		if is_inf(value):
			return "Infinity" if value > 0.0 else "-Infinity"
		if value == floor(value) and absf(value) < 1e15:
			return str(int(value))
		return str(value)
	if value is AsNull:
		return "null"
	if value is ScriptObject:
		return value.object_name
	if value is ScriptFunction:
		return "function " + value.function_name
	if value is Array:
		var parts: PackedStringArray = []
		for element in value:
			parts.append(_as_string(_resolve(element)))
		return ",".join(parts)
	if value is RegisterRef:
		return _as_string(_resolve(value))
	return str(value)


## Abstract equality (adapted from OpenSAGE Value.Equals: differing types
## compare false; extended so int/float compare numerically and object-like
## values compare by identity, keeping the operation total).
func _abstract_equals(a: Variant, b: Variant) -> bool:
	if _is_numeric(a) and _is_numeric(b):
		return float(a) == float(b)
	if a is bool and b is bool:
		return a == b
	if a is String and b is String:
		return a == b
	if a == null and b == null:
		return true
	if a is AsNull and b is AsNull:
		return true
	if (a is ScriptObject or a is ScriptFunction or a is Array) \
			or (b is ScriptObject or b is ScriptFunction or b is Array):
		return is_same(a, b)
	return false


func _type_of(value: Variant) -> String:
	if value == null:
		return "undefined"
	if value is bool:
		return "boolean"
	if value is int or value is float:
		return "number"
	if value is String:
		return "string"
	if value is ScriptFunction:
		return "function"
	# ScriptObject, AsNull, and arrays are all "object" (movieclip detection
	# needs the display tree, which is host territory).
	return "object"


func _describe(value: Variant) -> String:
	if value == null:
		return "undefined"
	if value is AsNull:
		return "null"
	if value is ScriptObject:
		return "[object %s]" % value.object_name
	if value is ScriptFunction:
		return "[function %s]" % value.function_name
	if value is Array:
		return "[array %d]" % value.size()
	return _as_string(value)


func _jsonable(value: Variant) -> Variant:
	if value is ScriptObject or value is ScriptFunction or value is AsNull:
		return _describe(value)
	if value is RegisterRef:
		return _jsonable(_resolve(value))
	if value is Array:
		var out: Array = []
		for element in value:
			out.append(_jsonable(element))
		return out
	return value


func _jsonable_args(args: Array) -> Array:
	var out: Array = []
	for arg in args:
		out.append(_jsonable(arg))
	return out
