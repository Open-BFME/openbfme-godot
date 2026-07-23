extends SceneTree
## Tier-2 APT ActionScript VM runner.
##
## Synthetic hand-assembled bytecode tests covering every implemented opcode
## family (stack in/out, operand decoding, alignment), branch loops,
## host-call recording, the fail-closed unimplemented halt, plus execution
## of real exported fixtures (skip-if-absent) from
## .private/retail-work/reports/apt-vm-fixtures/apt_vm_fixtures.json.
##
## SKIPPED opcodes from the corpus burn-down (d2_opcodes.csv): none - all
## 87 measured opcode values are implemented.
##
## Invocation:
##   Godot_v4.7 --headless --path game --script res://tests/retail_apt_vm_runner.gd

const AptVmScript = preload("res://src/apt/apt_vm.gd")

const FIXTURE_RELATIVE := "../.private/retail-work/reports/apt-vm-fixtures/apt_vm_fixtures.json"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


## Miniature assembler for hand-built APT bytecode. Alignment matches the
## VM contract: aligned opcodes pad to the next 4-byte boundary relative to
## the start of the byte space.
class Asm:
	var bytes := PackedByteArray()

	func op(code: int) -> void:
		bytes.append(code)

	func op_aligned(code: int) -> void:
		bytes.append(code)
		while bytes.size() % 4 != 0:
			bytes.append(0)

	func u8(value: int) -> void:
		bytes.append(value & 0xFF)

	func u16(value: int) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 2)
		bytes.encode_u16(pos, value)

	func u32(value: int) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 4)
		bytes.encode_u32(pos, value)

	func i32(value: int) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 4)
		bytes.encode_s32(pos, value)

	func f32(value: float) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 4)
		bytes.encode_float(pos, value)

	func mark() -> int:
		return bytes.size()

	func patch_u32(pos: int, value: int) -> void:
		bytes.encode_u32(pos, value)

	func patch_i32(pos: int, value: int) -> void:
		bytes.encode_s32(pos, value)

	func cstring(text: String) -> int:
		var pos := bytes.size()
		bytes.append_array(text.to_utf8_buffer())
		bytes.append(0)
		return pos

	## Append a u32 index table (for ConstantPool/PushData) and return its
	## absolute offset.
	func index_table(indices: Array) -> int:
		var pos := bytes.size()
		for index in indices:
			u32(index)
		return pos


func _run() -> void:
	# Tier-1 suite (contracts preserved).
	_test_push_and_variable_ops()
	_test_arithmetic_and_not()
	_test_string_and_members()
	_test_constant_pool_and_push_data()
	_test_branch_loop()
	_test_playback_and_host_calls()
	_test_unimplemented_halt()
	_test_stack_underflow_is_clean()
	# Tier-2 suite.
	_test_tier2_push_family()
	_test_tier2_arithmetic_family()
	_test_tier2_comparison_family()
	_test_tier2_variable_family()
	_test_tier2_register_family()
	_test_tier2_member_family()
	_test_tier2_enumerate_and_init_object()
	_test_tier2_branch_always()
	_test_tier2_function_family()
	_test_tier2_url_playback_family()
	_test_tier2_return_at_root()
	_run_real_fixtures()
	print("RETAIL_APT_VM_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _execute(asm: Asm, constants: Array = [], entry: int = 0) -> Dictionary:
	var vm = AptVmScript.new()
	return vm.execute(asm.bytes, constants, entry)


## Build a pool of string constants: returns the constants array for
## execute(); names index into it by position.
func _string_constants(names: Array) -> Array:
	var out: Array = []
	for entry in names:
		if entry is Dictionary:
			out.append(entry)
		else:
			out.append({"type": 1, "value": String(entry)})
	return out


# --- Tier-1 synthetic tests --------------------------------------------------

## EA_PushOne, EA_PushTrue, EA_PushByte, EA_PushThisVar, SetVariable, End.
func _test_push_and_variable_ops() -> void:
	var asm := Asm.new()
	asm.op(0x5A)  # EA_PushOne
	asm.op(0x73)  # EA_PushTrue
	asm.op(0xB5)  # EA_PushByte
	asm.u8(42)
	asm.op(0x70)  # EA_PushThisVar
	asm.op(0x00)  # End
	var result := _execute(asm)
	_check("push_ops_complete", result["completed"] == true and result["halted_on"] == null)
	_check("push_ops_stack_depth", result["stack_depth"] == 4)
	_check("push_ops_executed", result["instructions_executed"] == 5)


## Add2 (numeric add + string concat) and Not.
func _test_arithmetic_and_not() -> void:
	var asm := Asm.new()
	var name_patch: Array = []
	# sum = 1 + 41
	name_patch.append(_emit_push_string(asm))  # variable name "sum"
	asm.op(0x5A)  # EA_PushOne
	asm.op(0xB5)
	asm.u8(41)
	asm.op(0x47)  # Add2 -> 42
	asm.op(0x1D)  # SetVariable
	# flag = !true
	name_patch.append(_emit_push_string(asm))  # variable name "flag"
	asm.op(0x73)  # EA_PushTrue
	asm.op(0x12)  # Not
	asm.op(0x1D)  # SetVariable
	asm.op(0x00)  # End
	var sum_off := asm.cstring("sum")
	var flag_off := asm.cstring("flag")
	asm.patch_u32(name_patch[0], sum_off)
	asm.patch_u32(name_patch[1], flag_off)
	var result := _execute(asm)
	_check("add2_numeric", result["variables"].get("sum") == 42)
	_check("not_inverts_true", result["variables"].get("flag") == false)
	_check("arithmetic_complete", result["completed"] == true and result["stack_depth"] == 0)


## EA_PushString, Add2 string concat, SetMember/GetNamedMember on the scope
## object, and host-recorded SetMember on an undefined target.
func _test_string_and_members() -> void:
	var asm := Asm.new()
	var patches: Array = []
	# this.greet = "hi" + "!" (SetMember on the scope ScriptObject)
	asm.op(0x70)  # EA_PushThisVar (object)
	patches.append(_emit_push_string(asm))  # member name "greet"
	patches.append(_emit_push_string(asm))  # "hi"
	patches.append(_emit_push_string(asm))  # "!"
	asm.op(0x47)  # Add2 -> "hi!"
	asm.op(0x4F)  # SetMember
	# read it back through EA_GetNamedMember (pool constant 0 = "greet") and
	# store into variable "copy".
	patches.append(_emit_push_string(asm))  # variable name "copy"
	asm.op(0x70)  # EA_PushThisVar
	var pool_patch := _emit_constant_pool(asm, 1)
	asm.op(0xAF)  # EA_GetNamedMember
	asm.u8(0)
	asm.op(0x1D)  # SetVariable
	asm.op(0x00)  # End
	var greet_off := asm.cstring("greet")
	var hi_off := asm.cstring("hi")
	var bang_off := asm.cstring("!")
	var copy_off := asm.cstring("copy")
	asm.patch_u32(patches[0], greet_off)
	asm.patch_u32(patches[1], hi_off)
	asm.patch_u32(patches[2], bang_off)
	asm.patch_u32(patches[3], copy_off)
	var table_off := asm.index_table([0])
	asm.patch_u32(pool_patch, table_off)
	var constants := [{"type": 1, "value": "greet"}]
	var result := _execute(asm, constants)
	_check("set_member_on_scope", result["variables"].get("greet") == "hi!")
	_check("get_named_member_reads_back", result["variables"].get("copy") == "hi!")
	_check("string_members_complete", result["completed"] == true)


## ConstantPool + EA_PushConstantByte + PushData.
func _test_constant_pool_and_push_data() -> void:
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, 2)
	asm.op(0xA2)  # EA_PushConstantByte -> "answer"... pushed as name first
	asm.u8(1)
	asm.op_aligned(0x96)  # PushData: pushes constants[0] twice (7, 7)
	asm.u32(2)
	var data_table_patch := asm.mark()
	asm.u32(0)
	asm.op(0x47)  # Add2 -> 14
	asm.op(0x1D)  # SetVariable
	asm.op(0x00)  # End
	var pool_table := asm.index_table([0, 1])
	var data_table := asm.index_table([0, 0])
	asm.patch_u32(pool_patch, pool_table)
	asm.patch_u32(data_table_patch, data_table)
	var constants := [{"type": 7, "value": 7}, {"type": 1, "value": "answer"}]
	var result := _execute(asm, constants)
	_check("push_data_and_pool", result["variables"].get("answer") == 14)
	_check("pool_complete", result["completed"] == true and result["stack_depth"] == 0)


## BranchIfTrue loop: run the body three times, driven by pre-pushed
## conditions, incrementing variable n each pass (backward branch).
func _test_branch_loop() -> void:
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, 1)
	asm.op(0xA2)  # name "n"
	asm.u8(0)
	asm.op(0xB5)
	asm.u8(0)
	asm.op(0x1D)  # n = 0
	# conditions bottom-to-top: false-ish 0, then 1, 1 (loop twice more).
	asm.op(0xB5)
	asm.u8(0)
	asm.op(0x5A)  # 1
	asm.op(0x5A)  # 1
	var loop_start := asm.mark()
	# n = n + 1
	asm.op(0xA2)  # name "n"
	asm.u8(0)
	asm.op(0xAE)  # EA_PushValueOfVar "n"
	asm.u8(0)
	asm.op(0x5A)  # 1
	asm.op(0x47)  # Add2
	asm.op(0x1D)  # SetVariable
	# BranchIfTrue back to loop_start (offset relative to next instruction).
	asm.op_aligned(0x9D)
	var branch_patch := asm.mark()
	asm.i32(0)
	var after_branch := asm.mark()
	asm.op(0x00)  # End
	asm.patch_i32(branch_patch, loop_start - after_branch)
	var pool_table := asm.index_table([0])
	asm.patch_u32(pool_patch, pool_table)
	var constants := [{"type": 1, "value": "n"}]
	var result := _execute(asm, constants)
	_check("branch_loop_runs_three_times", result["variables"].get("n") == 3)
	_check("branch_loop_complete", result["completed"] == true and result["stack_depth"] == 0)


## Stop/Play/GotoLabel playback routing, EA_CallNamedMethodPop argument
## marshaling, and EA_PushValueOfVar host fallback - all recorded by the
## default host and returning undefined.
func _test_playback_and_host_calls() -> void:
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, 2)
	asm.op(0x07)  # Stop
	asm.op(0x06)  # Play
	asm.op_aligned(0x8C)  # GotoLabel
	var label_patch := asm.mark()
	asm.u32(0)
	# this.DoIt(7): arg, argc, object, call
	asm.op(0xB5)
	asm.u8(7)
	asm.op(0x5A)  # argc = 1
	asm.op(0x70)  # EA_PushThisVar
	asm.op(0xB2)  # EA_CallNamedMethodPop pool[0] = "DoIt"
	asm.u8(0)
	# unknown variable lookup goes to the host and pushes undefined.
	asm.op(0xAE)  # EA_PushValueOfVar pool[1] = "mystery"
	asm.u8(1)
	asm.op(0x12)  # Not -> true (undefined is falsy)
	asm.op(0x00)  # End
	var label_off := asm.cstring("Menu")
	asm.patch_u32(label_patch, label_off)
	var pool_table := asm.index_table([0, 1])
	asm.patch_u32(pool_patch, pool_table)
	var constants := [{"type": 1, "value": "DoIt"}, {"type": 1, "value": "mystery"}]
	var result := _execute(asm, constants)
	var calls: Array = result["host_calls"]
	var kinds: Array = []
	for call in calls:
		kinds.append([call["kind"], call["name"]])
	_check("playback_calls_recorded", kinds.has(["playback", "stop"]) \
		and kinds.has(["playback", "play"]) and kinds.has(["playback", "goto_label"]))
	var method_ok := false
	for call in calls:
		if call["kind"] == "call_method" and call["name"] == "[object this].DoIt" \
				and call["args"] == [7]:
			method_ok = true
	_check("call_named_method_pop_recorded", method_ok)
	var lookup_ok := false
	for call in calls:
		if call["kind"] == "get_variable" and call["name"] == "mystery":
			lookup_ok = true
	_check("host_variable_lookup_recorded", lookup_ok)
	_check("undefined_is_falsy_after_host_call",
		result["completed"] == true and result["stack_depth"] == 1)


## Any opcode outside the measured 87 halts cleanly and lands in the
## histogram (0x62 BitwiseXOr is unmeasured in the corpus).
func _test_unimplemented_halt() -> void:
	var asm := Asm.new()
	asm.op(0x5A)  # EA_PushOne (implemented)
	asm.op(0x62)  # BitwiseXOr - NOT in the measured corpus, must fail closed
	asm.op(0x5A)  # must never run
	asm.op(0x00)
	var vm = AptVmScript.new()
	var result: Dictionary = vm.execute(asm.bytes, [])
	_check("unimplemented_halts", result["halted_on"] == 0x62 \
		and result["halted_reason"] == "unimplemented_opcode")
	_check("unimplemented_recorded", result["unimplemented"].get(0x62, 0) == 1)
	_check("unimplemented_stops_execution",
		result["instructions_executed"] == 1 and result["completed"] == false)
	# Histogram is cumulative per VM instance.
	var result2: Dictionary = vm.execute(asm.bytes, [])
	_check("unimplemented_histogram_accumulates",
		result2["unimplemented"].get(0x62, 0) == 2)


func _test_stack_underflow_is_clean() -> void:
	var asm := Asm.new()
	asm.op(0x12)  # Not on an empty stack
	asm.op(0x00)
	var result := _execute(asm)
	_check("stack_underflow_halts_cleanly",
		result["completed"] == false and result["halted_reason"] == "stack_underflow" \
		and result["halted_on"] == null)


# --- Tier-2 synthetic tests ---------------------------------------------------

## Emit: push variable name (pool id), run body callable, SetVariable.
func _set_var(asm: Asm, name_id: int, body: Callable) -> void:
	asm.op(0xA2)
	asm.u8(name_id)
	body.call()
	asm.op(0x1D)


## EA_PushZero/False/Null/Undefined/Float/Short/Long/This/GlobalVar,
## PushDuplicate, and TypeOf.
func _test_tier2_push_family() -> void:
	var names := ["zero", "fals", "undef", "nul", "flt", "shrt", "lng",
		"dup", "t_this", "t_glob", "t_undef", "t_num"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	_set_var(asm, 0, func() -> void: asm.op(0x59))  # EA_PushZero
	_set_var(asm, 1, func() -> void: asm.op(0x74))  # EA_PushFalse
	_set_var(asm, 2, func() -> void: asm.op(0x76))  # EA_PushUndefined
	_set_var(asm, 3, func() -> void: asm.op(0x75))  # EA_PushNull
	_set_var(asm, 4, func() -> void:
		asm.op(0xB4)  # EA_PushFloat (unaligned f32)
		asm.f32(2.5))
	_set_var(asm, 5, func() -> void:
		asm.op(0xB6)  # EA_PushShort (u16)
		asm.u16(0x1234))
	_set_var(asm, 6, func() -> void:
		asm.op(0xB7)  # EA_PushLong (corpus-validated i32)
		asm.i32(999999))
	_set_var(asm, 7, func() -> void:
		asm.op(0xB5)
		asm.u8(5)
		asm.op(0x4C)  # PushDuplicate
		asm.op(0x47))  # Add2 -> 10
	_set_var(asm, 8, func() -> void:
		asm.op(0x56)  # EA_PushThis
		asm.op(0x44))  # TypeOf -> "object"
	_set_var(asm, 9, func() -> void:
		asm.op(0x71)  # EA_PushGlobalVar
		asm.op(0x44))  # TypeOf -> "object"
	_set_var(asm, 10, func() -> void:
		asm.op(0x76)
		asm.op(0x44))  # TypeOf undefined -> "undefined"
	_set_var(asm, 11, func() -> void:
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0x44))  # TypeOf number -> "number"
	asm.op(0x00)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var vars: Dictionary = result["variables"]
	_check("t2_push_zero_false", vars.get("zero") == 0 and vars.get("fals") == false)
	_check("t2_push_undefined", vars.has("undef") and vars.get("undef") == null)
	_check("t2_push_null_sentinel", vars.get("nul") == "null")
	_check("t2_push_float", absf(float(vars.get("flt", 0.0)) - 2.5) < 0.0001)
	_check("t2_push_short_long",
		vars.get("shrt") == 0x1234 and vars.get("lng") == 999999)
	_check("t2_push_duplicate", vars.get("dup") == 10)
	_check("t2_typeof_objects",
		vars.get("t_this") == "object" and vars.get("t_glob") == "object")
	_check("t2_typeof_scalars",
		vars.get("t_undef") == "undefined" and vars.get("t_num") == "number")
	_check("t2_push_family_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## Subtract/Multiply/Divide/Modulo/Increment/Decrement/BitwiseAnd/
## ShiftRight/ToInteger/ToNumber/ToString.
func _test_tier2_arithmetic_family() -> void:
	var names := ["sub", "mul", "dv", "dz", "md", "inc", "dec", "band",
		"shr", "toi", "ton", "tos", "2.9", "3.5"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	_set_var(asm, 0, func() -> void:
		asm.op(0xB5)
		asm.u8(10)
		asm.op(0xB5)
		asm.u8(4)
		asm.op(0x0B))  # Subtract -> 6
	_set_var(asm, 1, func() -> void:
		asm.op(0xB5)
		asm.u8(6)
		asm.op(0xB5)
		asm.u8(7)
		asm.op(0x0C))  # Multiply -> 42
	_set_var(asm, 2, func() -> void:
		asm.op(0xB5)
		asm.u8(84)
		asm.op(0xB5)
		asm.u8(2)
		asm.op(0x0D))  # Divide -> 42
	_set_var(asm, 3, func() -> void:
		asm.op(0x5A)
		asm.op(0x59)
		asm.op(0x0D))  # 1/0 -> Infinity
	_set_var(asm, 4, func() -> void:
		asm.op(0xB5)
		asm.u8(7)
		asm.op(0xB5)
		asm.u8(4)
		asm.op(0x3F))  # Modulo -> 3
	_set_var(asm, 5, func() -> void:
		asm.op(0xB5)
		asm.u8(41)
		asm.op(0x50))  # Increment -> 42
	_set_var(asm, 6, func() -> void:
		asm.op(0xB5)
		asm.u8(43)
		asm.op(0x51))  # Decrement -> 42
	_set_var(asm, 7, func() -> void:
		asm.op(0xB5)
		asm.u8(0xFF)
		asm.op(0xB5)
		asm.u8(0x0F)
		asm.op(0x60))  # BitwiseAnd -> 15
	_set_var(asm, 8, func() -> void:
		asm.op(0xB5)
		asm.u8(16)
		asm.op(0xB5)
		asm.u8(2)
		asm.op(0x64))  # ShiftRight -> 4
	_set_var(asm, 9, func() -> void:
		asm.op(0xA2)
		asm.u8(12)  # "2.9"
		asm.op(0x18))  # ToInteger -> 2
	_set_var(asm, 10, func() -> void:
		asm.op(0xA2)
		asm.u8(13)  # "3.5"
		asm.op(0x4A))  # ToNumber -> 3.5
	_set_var(asm, 11, func() -> void:
		asm.op(0xB5)
		asm.u8(42)
		asm.op(0x4B))  # ToString -> "42"
	asm.op(0x00)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var vars: Dictionary = result["variables"]
	_check("t2_subtract", float(vars.get("sub", 0.0)) == 6.0)
	_check("t2_multiply", float(vars.get("mul", 0.0)) == 42.0)
	_check("t2_divide", float(vars.get("dv", 0.0)) == 42.0)
	_check("t2_divide_by_zero_is_inf",
		vars.get("dz") is float and is_inf(float(vars.get("dz"))))
	_check("t2_modulo", float(vars.get("md", 0.0)) == 3.0)
	_check("t2_increment_decrement", vars.get("inc") == 42 and vars.get("dec") == 42)
	_check("t2_bitwise_and", vars.get("band") == 15)
	_check("t2_shift_right", vars.get("shr") == 4)
	_check("t2_to_integer", vars.get("toi") == 2)
	_check("t2_to_number", absf(float(vars.get("ton", 0.0)) - 3.5) < 0.0001)
	_check("t2_to_string", vars.get("tos") == "42")
	_check("t2_arithmetic_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## Equals2/LessThan2/Greater/StringEquals/StringConcat.
func _test_tier2_comparison_family() -> void:
	var names := ["eq_ii", "eq_if", "eq_mix", "lt", "lt_nan", "gt",
		"streq", "cat", "3", "a", "b"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	_set_var(asm, 0, func() -> void:
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0x49))  # Equals2 int/int -> true
	_set_var(asm, 1, func() -> void:
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0xB4)
		asm.f32(3.0)
		asm.op(0x49))  # Equals2 int/float -> true (numeric)
	_set_var(asm, 2, func() -> void:
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0xA2)
		asm.u8(8)  # "3"
		asm.op(0x49))  # Equals2 number/string -> false (type mismatch)
	_set_var(asm, 3, func() -> void:
		asm.op(0xB5)
		asm.u8(2)
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0x48))  # LessThan2: 2 < 3 -> true
	_set_var(asm, 4, func() -> void:
		asm.op(0x76)  # undefined -> NaN operand
		asm.op(0x5A)
		asm.op(0x48))  # LessThan2 with NaN -> undefined
	_set_var(asm, 5, func() -> void:
		asm.op(0xB5)
		asm.u8(5)
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0x67))  # Greater: 5 > 3 -> true
	_set_var(asm, 6, func() -> void:
		asm.op(0xA2)
		asm.u8(9)
		asm.op(0xA2)
		asm.u8(9)
		asm.op(0x13))  # StringEquals "a"=="a" -> true
	_set_var(asm, 7, func() -> void:
		asm.op(0xA2)
		asm.u8(9)
		asm.op(0xA2)
		asm.u8(10)
		asm.op(0x21))  # StringConcat -> "ab"
	asm.op(0x00)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var vars: Dictionary = result["variables"]
	_check("t2_equals2_numeric", vars.get("eq_ii") == true and vars.get("eq_if") == true)
	_check("t2_equals2_type_mismatch", vars.get("eq_mix") == false)
	_check("t2_less_than2", vars.get("lt") == true)
	_check("t2_less_than2_nan_undefined",
		vars.has("lt_nan") and vars.get("lt_nan") == null)
	_check("t2_greater", vars.get("gt") == true)
	_check("t2_string_equals_concat",
		vars.get("streq") == true and vars.get("cat") == "ab")
	_check("t2_comparison_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## GetVariable, DefineLocal (+ SetVariable local shadowing), Delete2,
## EA_ZeroVar, EA_GetStringVar, EA_SetStringVar.
func _test_tier2_variable_family() -> void:
	var names := ["x", "gx", "loc", "locread", "temp", "tempread", "z", "sv"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	# x = 7
	_set_var(asm, 0, func() -> void:
		asm.op(0xB5)
		asm.u8(7))
	# gx = GetVariable("x") (0x1C pops the name)
	_set_var(asm, 1, func() -> void:
		asm.op(0xA2)
		asm.u8(0)
		asm.op(0x1C))
	# DefineLocal loc = 5; SetVariable loc = 9 must hit the LOCAL, so the
	# scope never sees "loc"; read back via this.locread = loc.
	asm.op(0xA2)
	asm.u8(2)
	asm.op(0xB5)
	asm.u8(5)
	asm.op(0x3C)  # DefineLocal
	asm.op(0xA2)
	asm.u8(2)
	asm.op(0xB5)
	asm.u8(9)
	asm.op(0x1D)  # SetVariable -> updates the local
	asm.op(0x70)  # this
	asm.op(0xA2)
	asm.u8(3)  # member name "locread"
	asm.op(0xAE)
	asm.u8(2)  # value of "loc" (params -> locals -> scope)
	asm.op(0x4F)  # SetMember: this.locread = 9
	# temp = 1; Delete2 "temp"; reading it again falls through to the host.
	_set_var(asm, 4, func() -> void: asm.op(0x5A))
	asm.op(0xA2)
	asm.u8(4)
	asm.op(0x3B)  # Delete2
	asm.op(0x70)
	asm.op(0xA2)
	asm.u8(5)  # "tempread"
	asm.op(0xAE)
	asm.u8(4)  # "temp" -> host fallback (undefined)
	asm.op(0x4F)
	# EA_ZeroVar: z = 0
	asm.op(0xA2)
	asm.u8(6)
	asm.op(0x72)
	# EA_SetStringVar: sv = "hello" (name popped, value is the operand)
	asm.op(0xA2)
	asm.u8(7)
	asm.op_aligned(0xA6)
	var ssv_patch := asm.mark()
	asm.u32(0)
	# EA_GetStringVar reads x back; store via this.gsx.
	asm.op(0x70)
	asm.op(0xA2)
	asm.u8(1)  # reuse "gx" as member name target check? use distinct: keep gx2 via locread? Use member name "gx" on this - fine, SetMember overwrites variable gx? No: this IS the scope, so it overwrites gx. Compare against 7 either way.
	asm.op_aligned(0xA4)
	var gsv_patch := asm.mark()
	asm.u32(0)
	asm.op(0x4F)
	asm.op(0x00)
	var hello_off := asm.cstring("hello")
	var xname_off := asm.cstring("x")
	asm.patch_u32(ssv_patch, hello_off)
	asm.patch_u32(gsv_patch, xname_off)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var vars: Dictionary = result["variables"]
	_check("t2_get_variable", vars.get("gx") == 7)
	_check("t2_define_local_shadowing",
		vars.get("locread") == 9 and not vars.has("loc"))
	_check("t2_delete2",
		vars.has("tempread") and vars.get("tempread") == null and not vars.has("temp"))
	var host_saw_temp := false
	for call in result["host_calls"]:
		if call["kind"] == "get_variable" and call["name"] == "temp":
			host_saw_temp = true
	_check("t2_delete2_host_fallback", host_saw_temp)
	_check("t2_zero_var", vars.get("z") == 0)
	_check("t2_set_string_var", vars.get("sv") == "hello")
	_check("t2_get_string_var", vars.get("gx") == 7)
	_check("t2_variable_family_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## SetRegister (aligned i32 operand, peek semantics) and register-kind
## constant resolution at pop time.
func _test_tier2_register_family() -> void:
	var asm := Asm.new()
	# constants: [0]="regval" (string), [1]=register 2 reference.
	var pool_patch := _emit_constant_pool(asm, 2)
	asm.op(0xB5)
	asm.u8(77)
	asm.op_aligned(0x87)  # SetRegister r2 = 77 (value stays on the stack)
	asm.i32(2)
	asm.op(0x17)  # Pop it
	asm.op(0xA2)  # name "regval"
	asm.u8(0)
	asm.op(0xA2)  # register-kind constant -> resolves to r2 at pop
	asm.u8(1)
	asm.op(0x1D)  # regval = 77
	asm.op(0x00)
	asm.patch_u32(pool_patch, asm.index_table([0, 1]))
	var constants := [
		{"type": 1, "value": "regval"},
		{"type": 4, "value": 2},
	]
	var result := _execute(asm, constants)
	_check("t2_set_register_and_resolve", result["variables"].get("regval") == 77)
	_check("t2_register_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## InitArray, GetMember (array length/index + object member), NewObject
## (host constructor fallback), EA_GetStringMember/EA_SetStringMember,
## Delete, GetProperty/SetProperty.
func _test_tier2_member_family() -> void:
	var names := ["arr", "len", "at1", "obj", "Color", "tagread", "length",
		"tag", "spr"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	# arr = [1, 2, 3]
	_set_var(asm, 0, func() -> void:
		asm.op(0xB5)
		asm.u8(3)
		asm.op(0xB5)
		asm.u8(2)
		asm.op(0x5A)  # 1 on top -> element 0
		asm.op(0xB5)
		asm.u8(3)  # count
		asm.op(0x42))  # InitArray
	# len = arr.length (GetMember pops member then object)
	_set_var(asm, 1, func() -> void:
		asm.op(0xAE)
		asm.u8(0)
		asm.op(0xA2)
		asm.u8(6)  # "length"
		asm.op(0x4E))
	# at1 = arr[1]
	_set_var(asm, 2, func() -> void:
		asm.op(0xAE)
		asm.u8(0)
		asm.op(0x5A)  # member name 1
		asm.op(0x4E))
	# obj = new Color(9): args reversed, argc, class name, NewObject.
	_set_var(asm, 3, func() -> void:
		asm.op(0xB5)
		asm.u8(9)
		asm.op(0x5A)  # argc = 1
		asm.op(0xA2)
		asm.u8(4)  # "Color"
		asm.op(0x40))
	# obj.tag = "hi" via EA_SetStringMember (member name popped, value operand)
	asm.op(0xAE)
	asm.u8(3)  # obj
	asm.op(0xA2)
	asm.u8(7)  # member name "tag"
	asm.op_aligned(0xA7)
	var ssm_patch := asm.mark()
	asm.u32(0)
	# tagread = obj.tag via EA_GetStringMember (object popped, name operand)
	asm.op(0xA2)
	asm.u8(5)  # variable name "tagread"
	asm.op(0xAE)
	asm.u8(3)
	asm.op_aligned(0xA5)
	var gsm_patch := asm.mark()
	asm.u32(0)
	asm.op(0x1D)
	# Delete obj.tag (pops member name then object)
	asm.op(0xAE)
	asm.u8(3)
	asm.op(0xA2)
	asm.u8(7)
	asm.op(0x3A)
	# GetProperty(target "spr", property 2) -> host, pushes undefined; Pop.
	asm.op(0xA2)
	asm.u8(8)
	asm.op(0xB5)
	asm.u8(2)
	asm.op(0x22)
	asm.op(0x17)
	# SetProperty(target "spr", property 2, value 9) -> host.
	asm.op(0xA2)
	asm.u8(8)
	asm.op(0xB5)
	asm.u8(2)
	asm.op(0xB5)
	asm.u8(9)
	asm.op(0x23)
	asm.op(0x00)
	var tag_value_off := asm.cstring("hi")
	var tag_name_off := asm.cstring("tag")
	asm.patch_u32(ssm_patch, tag_value_off)
	asm.patch_u32(gsm_patch, tag_name_off)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var vars: Dictionary = result["variables"]
	_check("t2_init_array", vars.get("arr") == [1, 2, 3])
	_check("t2_array_members", vars.get("len") == 3 and vars.get("at1") == 2)
	_check("t2_new_object_fallback", vars.get("obj") == "[object Color]")
	var construct_seen := false
	var props_seen := 0
	for call in result["host_calls"]:
		if call["kind"] == "construct_object" and call["name"] == "Color" \
				and call["args"] == [9]:
			construct_seen = true
		if call["kind"] in ["get_property", "set_property"] \
				and call["name"] == "spr#2":
			props_seen += 1
	_check("t2_construct_recorded", construct_seen)
	_check("t2_string_member_roundtrip", vars.get("tagread") == "hi")
	_check("t2_property_host_calls", props_seen == 2)
	_check("t2_member_family_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## InitObject + Enumerate2: fold the enumerated names + null terminator
## into one string, then hand it to the host via EA_CallNamedFuncPop.
func _test_tier2_enumerate_and_init_object() -> void:
	var names := ["k1", "k2", "report"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	# {k1: 1, k2: 2} -> InitObject pops (value, name) pairs.
	asm.op(0xA2)
	asm.u8(0)
	asm.op(0x5A)  # 1
	asm.op(0xA2)
	asm.u8(1)
	asm.op(0xB5)
	asm.u8(2)
	asm.op(0xB5)
	asm.u8(2)  # pair count
	asm.op(0x43)  # InitObject
	asm.op(0x55)  # Enumerate2: pushes null, then names in insertion order
	asm.op(0x21)  # StringConcat: "k2" + "k1" -> "k2k1"
	asm.op(0x21)  # StringConcat: null + "k2k1" -> "nullk2k1"
	asm.op(0x5A)  # argc = 1
	asm.op(0xB0)  # EA_CallNamedFuncPop "report"
	asm.u8(2)
	asm.op(0x00)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var reported := false
	for call in result["host_calls"]:
		if call["kind"] == "call_function" and call["name"] == "report" \
				and call["args"] == ["nullk2k1"]:
			reported = true
	_check("t2_enumerate2_and_init_object", reported)
	_check("t2_enumerate_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## BranchAlways skips a would-be assignment (forward jump, aligned i32
## relative to the next instruction).
func _test_tier2_branch_always() -> void:
	var names := ["n"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, 1)
	_set_var(asm, 0, func() -> void: asm.op(0x5A))  # n = 1
	asm.op_aligned(0x99)  # BranchAlways over the n = 2 block
	var branch_patch := asm.mark()
	asm.i32(0)
	var jump_from := asm.mark()
	_set_var(asm, 0, func() -> void:
		asm.op(0xB5)
		asm.u8(2))
	var jump_to := asm.mark()
	asm.op(0x00)
	asm.patch_i32(branch_patch, jump_to - jump_from)
	asm.patch_u32(pool_patch, asm.index_table([0]))
	var result := _execute(asm, _string_constants(names))
	_check("t2_branch_always_skips", result["variables"].get("n") == 1)
	_check("t2_branch_always_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## DefineFunction (named + params + Return), DefineFunction2 (anonymous,
## register params, PreloadThis), CallFunction, EA_CallNamedFunc,
## CallMethod on a pushed function value, script method dispatch through
## EA_CallNamedMethodPop, and the host fallback for unknown functions.
func _test_tier2_function_family() -> void:
	var names := ["addOne", "x", "res", "res2", "res3", "resh", "Mystery",
		"m", "notify", "obj2", "marked", ""]
	var reg2_const := {"type": 4, "value": 2}
	var reg1_const := {"type": 4, "value": 1}
	var constants := _string_constants(names)
	constants.append(reg2_const)  # id 12
	constants.append(reg1_const)  # id 13
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, constants.size())

	# --- function addOne(x) { return x + 1 } (DefineFunction, old style) ---
	asm.op_aligned(0x9B)
	var df_name_patch := asm.mark()
	asm.u32(0)  # name offset -> "addOne"
	asm.u32(1)  # param count
	var df_table_patch := asm.mark()
	asm.u32(0)  # param table offset
	var df_size_patch := asm.mark()
	asm.i32(0)  # body size
	asm.u32(0)  # 8 unused bytes
	asm.u32(0)
	var df_body_start := asm.mark()
	asm.op(0xAE)  # x (parameter lookup)
	asm.u8(1)
	asm.op(0x5A)
	asm.op(0x47)  # x + 1
	asm.op(0x3E)  # Return
	asm.patch_i32(df_size_patch, asm.mark() - df_body_start)

	# res = addOne(41): name-under-value, arg, argc, function name.
	asm.op(0xA2)
	asm.u8(2)  # "res"
	asm.op(0xB5)
	asm.u8(41)
	asm.op(0x5A)  # argc
	asm.op(0xA2)
	asm.u8(0)  # "addOne"
	asm.op(0x3D)  # CallFunction pushes 42
	asm.op(0x1D)

	# res3 = addOne(4) via EA_CallNamedFunc (result pushed).
	asm.op(0xA2)
	asm.u8(4)  # "res3"
	asm.op(0xB5)
	asm.u8(4)
	asm.op(0x5A)
	asm.op(0xB1)  # EA_CallNamedFunc "addOne"
	asm.u8(0)
	asm.op(0x1D)

	# resh = Mystery() -> host.call_function, pushes undefined.
	asm.op(0xA2)
	asm.u8(5)  # "resh"
	asm.op(0x59)  # argc = 0
	asm.op(0xA2)
	asm.u8(6)  # "Mystery"
	asm.op(0x3D)
	asm.op(0x1D)

	# --- anonymous DefineFunction2: param x in r2, PreloadThis in r1;
	# body: this.marked = true; return x + 1 ---
	asm.op(0xA2)
	asm.u8(3)  # "res2" (name for the eventual SetVariable)
	asm.op(0xB5)
	asm.u8(9)  # arg
	asm.op(0x5A)  # argc = 1
	asm.op_aligned(0x8E)
	var d2_name_patch := asm.mark()
	asm.u32(0)  # name offset -> "" (anonymous)
	asm.u32(1)  # param count
	asm.u8(3)  # register count
	asm.u8(0x00)  # flags u24 little-endian: PRELOAD_THIS = 0x000100
	asm.u8(0x01)
	asm.u8(0x00)
	var d2_table_patch := asm.mark()
	asm.u32(0)  # param table offset (i32 register + u32 string offset)
	var d2_size_patch := asm.mark()
	asm.i32(0)  # body size
	asm.u32(0)  # 8 unused bytes
	asm.u32(0)
	var d2_body_start := asm.mark()
	asm.op(0xA2)  # r1 = this (register-kind constant id 13)
	asm.u8(13)
	asm.op(0xA2)
	asm.u8(10)  # member name "marked"
	asm.op(0x73)  # true
	asm.op(0x4F)  # this.marked = true
	asm.op(0xA2)  # r2 = x (register-kind constant id 12)
	asm.u8(12)
	asm.op(0x5A)
	asm.op(0x47)  # x + 1
	asm.op(0x3E)  # Return
	asm.patch_i32(d2_size_patch, asm.mark() - d2_body_start)
	# CallMethod with empty name: the function value is on the stack.
	asm.op(0xA2)
	asm.u8(11)  # "" (empty method name)
	asm.op(0x52)  # CallMethod -> pushes 10
	asm.op(0x1D)  # res2 = 10

	# --- obj2 = new Widget-less object via InitObject, then obj2.m =
	# anonymous old-style function calling this.notify(7); invoke through
	# EA_CallNamedMethodPop (discards the result). ---
	asm.op(0xA2)
	asm.u8(9)  # "obj2"
	asm.op(0x59)  # 0 pairs
	asm.op(0x43)  # InitObject
	asm.op(0x1D)  # obj2 = {}
	asm.op(0xAE)  # obj2
	asm.u8(9)
	asm.op(0xA2)
	asm.u8(7)  # member name "m"
	asm.op_aligned(0x9B)  # anonymous DefineFunction -> pushed on the stack
	var mf_name_patch := asm.mark()
	asm.u32(0)  # name -> ""
	asm.u32(0)  # no params
	var mf_table_patch := asm.mark()
	asm.u32(0)
	var mf_size_patch := asm.mark()
	asm.i32(0)
	asm.u32(0)
	asm.u32(0)
	var mf_body_start := asm.mark()
	asm.op(0xB5)  # arg 7
	asm.u8(7)
	asm.op(0x5A)  # argc = 1
	asm.op(0x70)  # this (== obj2 during the method call)
	asm.op(0xB2)  # EA_CallNamedMethodPop "notify" -> host
	asm.u8(8)
	asm.op(0x00)  # End of body
	asm.patch_i32(mf_size_patch, asm.mark() - mf_body_start)
	asm.op(0x4F)  # obj2.m = function
	# obj2.m() via EA_CallNamedMethodPop.
	asm.op(0x59)  # argc = 0
	asm.op(0xAE)  # obj2
	asm.u8(9)
	asm.op(0xB2)
	asm.u8(7)  # "m"
	asm.op(0x00)  # End

	# String blobs + patches.
	var addone_off := asm.cstring("addOne")
	var x_off := asm.cstring("x")
	var empty_off := asm.cstring("")
	var df_table_off := asm.index_table([x_off])
	asm.patch_u32(df_name_patch, addone_off)
	asm.patch_u32(df_table_patch, df_table_off)
	asm.patch_u32(d2_name_patch, empty_off)
	var d2_table_off := asm.mark()
	asm.i32(2)  # parameter register
	asm.u32(x_off)  # parameter name
	asm.patch_u32(d2_table_patch, d2_table_off)
	asm.patch_u32(mf_name_patch, empty_off)
	asm.patch_u32(mf_table_patch, asm.index_table([]))
	asm.patch_u32(pool_patch, asm.index_table(range(constants.size())))

	var result := _execute(asm, constants)
	var vars: Dictionary = result["variables"]
	_check("t2_define_function_and_call", vars.get("res") == 42)
	_check("t2_call_named_func_pushes", vars.get("res3") == 5)
	_check("t2_host_function_fallback",
		vars.has("resh") and vars.get("resh") == null)
	var host_fn_seen := false
	var notify_seen := false
	for call in result["host_calls"]:
		if call["kind"] == "call_function" and call["name"] == "Mystery":
			host_fn_seen = true
		if call["kind"] == "call_method" and call["name"] == "[object object].notify" \
				and call["args"] == [7]:
			notify_seen = true
	_check("t2_host_function_recorded", host_fn_seen)
	_check("t2_define_function2_registers", vars.get("res2") == 10)
	_check("t2_preload_this_marked", vars.get("marked") == true)
	_check("t2_script_method_dispatch", notify_seen)
	_check("t2_function_family_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## GetURL/GetURL2 (fscommand + url), GotoFrame/GotoFrame2/NextFrame,
## CloneSprite/RemoveSprite, Trace, Random.
func _test_tier2_url_playback_family() -> void:
	var names := ["FSCommand:Quit", "0", "shell", "a", "b"]
	var asm := Asm.new()
	var pool_patch := _emit_constant_pool(asm, names.size())
	# GetURL (operands: url + target string offsets)
	asm.op_aligned(0x83)
	var url_patch := asm.mark()
	asm.u32(0)
	var target_patch := asm.mark()
	asm.u32(0)
	# GetURL2 with an FSCommand url (pops target, then url)
	asm.op(0xA2)
	asm.u8(0)  # "FSCommand:Quit"
	asm.op(0xA2)
	asm.u8(1)  # "0"
	asm.op(0x9A)
	# GotoFrame 3
	asm.op_aligned(0x81)
	asm.i32(3)
	# GotoFrame2 (flags bit0 = play), frame label popped
	asm.op(0xA2)
	asm.u8(2)  # "shell"
	asm.op_aligned(0x9F)
	asm.i32(1)
	# NextFrame
	asm.op(0x04)
	# CloneSprite (pops depth, target, source)
	asm.op(0xA2)
	asm.u8(3)  # source "a"
	asm.op(0xA2)
	asm.u8(4)  # target "b"
	asm.op(0xB5)
	asm.u8(5)  # depth
	asm.op(0x24)
	# RemoveSprite
	asm.op(0xA2)
	asm.u8(4)
	asm.op(0x25)
	# Trace
	asm.op(0xA2)
	asm.u8(2)
	asm.op(0x26)
	# Random(10) -> host (default 0), then Pop
	asm.op(0xB5)
	asm.u8(10)
	asm.op(0x30)
	asm.op(0x17)
	asm.op(0x00)
	var url_off := asm.cstring("menu.apt")
	var tgt_off := asm.cstring("spr")
	asm.patch_u32(url_patch, url_off)
	asm.patch_u32(target_patch, tgt_off)
	asm.patch_u32(pool_patch, asm.index_table(range(names.size())))
	var result := _execute(asm, _string_constants(names))
	var seen := {}
	for call in result["host_calls"]:
		seen[[call["kind"], call["name"]]] = call["args"]
	_check("t2_get_url_recorded", seen.get(["get_url", "menu.apt"]) == ["spr"])
	_check("t2_fscommand_recorded", seen.get(["fs_command", "Quit"]) == ["0"])
	_check("t2_goto_frame", seen.get(["playback", "goto_frame"]) == [3])
	_check("t2_goto_frame2", seen.get(["playback", "goto_frame2"]) == ["shell", 1])
	_check("t2_next_frame", seen.has(["playback", "next_frame"]))
	_check("t2_clone_sprite", seen.get(["playback", "clone_sprite"]) == ["a", "b", 5])
	_check("t2_remove_sprite", seen.get(["playback", "remove_sprite"]) == ["b"])
	_check("t2_trace", seen.has(["trace", "shell"]))
	_check("t2_random_via_host", seen.has(["random_int", "10"]))
	_check("t2_url_family_complete",
		result["completed"] == true and result["stack_depth"] == 0)


## Return at the root frame completes cleanly with reason "return".
func _test_tier2_return_at_root() -> void:
	var asm := Asm.new()
	asm.op(0x5A)
	asm.op(0x3E)  # Return
	asm.op(0xB5)  # must never run
	asm.u8(99)
	asm.op(0x00)
	var result := _execute(asm)
	_check("t2_return_at_root",
		result["completed"] == true and result["halted_reason"] == "return" \
		and result["instructions_executed"] == 2)


# --- Real fixtures ----------------------------------------------------------

func _run_real_fixtures() -> void:
	var fixture_path := ProjectSettings.globalize_path("res://").path_join(FIXTURE_RELATIVE)
	if not FileAccess.file_exists(fixture_path):
		print("RETAIL_APT_VM SKIP real_fixtures (absent: %s)" % fixture_path)
		return
	var raw := FileAccess.get_file_as_string(fixture_path)
	var fixture: Variant = JSON.parse_string(raw)
	if fixture == null or not (fixture is Dictionary):
		_check("real_fixture_parses", false, "unreadable fixture JSON")
		return
	var movies: Dictionary = fixture.get("movies", {})
	var scripts: Array = fixture.get("scripts", [])
	if scripts.size() < 3:
		_check("real_fixture_has_scripts", false)
		return
	var blobs := {}
	var constants := {}
	for movie_name in movies:
		var movie: Dictionary = movies[movie_name]
		blobs[movie_name] = (movie["apt_data_hex"] as String).hex_decode()
		constants[movie_name] = movie["constants"]
	var all_complete := true
	var per_script: Array = []
	for script in scripts:
		var movie_name: String = script["movie"]
		var entry := int(script["entry_offset"])
		# Fresh VM per script: the call-log assertion below is per script.
		var vm = AptVmScript.new()
		var result: Dictionary = vm.execute(blobs[movie_name], constants[movie_name], entry)
		var executed := int(result["instructions_executed"])
		all_complete = all_complete and bool(result["completed"])
		var digest := JSON.stringify(result["host_calls"]).sha256_text().substr(0, 16)
		per_script.append({
			"movie": movie_name, "entry": entry, "executed": executed,
			"host_calls": (result["host_calls"] as Array).size(),
			"digest": digest, "reason": result["halted_reason"],
		})
		print("RETAIL_APT_VM fixture movie=%s entry=0x%X executed=%d reason=%s halted_on=%s host_calls=%d digest=%s" % [
			movie_name, entry, executed, result["halted_reason"],
			("0x%02X" % result["halted_on"]) if result["halted_on"] != null else "-",
			result["host_calls"].size(), digest,
		])
		if not result["unimplemented"].is_empty():
			var histogram_text := ""
			var keys: Array = result["unimplemented"].keys()
			keys.sort()
			for key in keys:
				histogram_text += "0x%02X:%d " % [key, result["unimplemented"][key]]
			print("RETAIL_APT_VM fixture_unimplemented {%s}" % histogram_text.strip_edges())
	_check("real_fixtures_complete_clean", all_complete,
		"tier-2 must run every fixture script to End/Return")
	# Asserted call logs: pinned per-script host-call counts and digests
	# (deterministic; re-pin only when the fixture file itself changes).
	var expected := {
		"MainMenu.apt|0x11BA0": {"host_calls": 32, "digest": "03e03ce0b520247a"},
		"MainMenu.apt|0x11938": {"host_calls": 20, "digest": "9d3dec1dc439a8bd"},
		"MainMenu.apt|0x11A68": {"host_calls": 20, "digest": "763f6b866dc9a916"},
	}
	var pins_ok := true
	for entry_info in per_script:
		var key := "%s|0x%X" % [entry_info["movie"], entry_info["entry"]]
		if not expected.has(key):
			pins_ok = false
			print("RETAIL_APT_VM fixture_pin_missing %s host_calls=%d digest=%s" % [
				key, entry_info["host_calls"], entry_info["digest"]])
			continue
		var pin: Dictionary = expected[key]
		if int(pin["host_calls"]) != int(entry_info["host_calls"]) \
				or String(pin["digest"]) != String(entry_info["digest"]):
			pins_ok = false
			print("RETAIL_APT_VM fixture_pin_mismatch %s host_calls=%d digest=%s" % [
				key, entry_info["host_calls"], entry_info["digest"]])
	_check("real_fixture_call_logs_pinned", pins_ok)
	var best_executed := 0
	for entry_info in per_script:
		best_executed = maxi(best_executed, int(entry_info["executed"]))
	_check("real_fixture_executes_10_plus", best_executed >= 10,
		"best=%d" % best_executed)


# --- Helpers ----------------------------------------------------------------

## Emit EA_PushString with a placeholder string offset; returns the patch
## position for the u32 operand.
func _emit_push_string(asm: Asm) -> int:
	asm.op_aligned(0xA1)
	var pos := asm.mark()
	asm.u32(0)
	return pos


## Emit ConstantPool with the given constant count and a placeholder table
## offset; returns the patch position of the table u32 operand.
func _emit_constant_pool(asm: Asm, count: int) -> int:
	asm.op_aligned(0x88)
	asm.u32(count)
	var table_pos := asm.mark()
	asm.u32(0)
	return table_pos


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_APT_VM PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_APT_VM FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
