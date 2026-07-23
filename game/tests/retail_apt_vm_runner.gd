extends SceneTree
## Tier-1 APT ActionScript VM runner.
##
## Synthetic hand-assembled bytecode tests for every implemented opcode,
## a branch loop, host-call recording, the fail-closed unimplemented halt,
## plus execution of real exported fixtures (skip-if-absent) from
## .private/retail-work/reports/apt-vm-fixtures/apt_vm_fixtures.json.
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

	func u32(value: int) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 4)
		bytes.encode_u32(pos, value)

	func i32(value: int) -> void:
		var pos := bytes.size()
		bytes.resize(pos + 4)
		bytes.encode_s32(pos, value)

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
	_test_push_and_variable_ops()
	_test_arithmetic_and_not()
	_test_string_and_members()
	_test_constant_pool_and_push_data()
	_test_branch_loop()
	_test_playback_and_host_calls()
	_test_unimplemented_halt()
	_test_stack_underflow_is_clean()
	_run_real_fixtures()
	print("RETAIL_APT_VM_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _execute(asm: Asm, constants: Array = [], entry: int = 0) -> Dictionary:
	var vm = AptVmScript.new()
	return vm.execute(asm.bytes, constants, entry)


# --- Synthetic tests --------------------------------------------------------

## EA_PushOne, EA_PushTrue, EA_PushByte, EA_PushThisVar, SetVariable, End.
func _test_push_and_variable_ops() -> void:
	var asm := Asm.new()
	# n = 42 ("n" arrives via EA_PushString below in the string test; here we
	# use PushString-free flow: name from constant pool is tested later, so
	# push name via EA_PushString requires a string blob - instead exercise
	# SetVariable with a string name pushed by EA_PushString in its own test
	# and here keep pure stack pushes.
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
	# name = pool[1] ("answer"), value = pool[0] pushed twice via PushData +
	# Add2 (7 + 7 = 14).
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
	# n = 0 - 0 is synthesized as !true -> false -> ToInteger 0 via Add2 with
	# byte 0 instead: name, byte 0, SetVariable.
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


## Any opcode outside the top-20 halts cleanly and lands in the histogram.
func _test_unimplemented_halt() -> void:
	var asm := Asm.new()
	asm.op(0x5A)  # EA_PushOne (implemented)
	asm.op(0x17)  # Pop - NOT tier 1, must fail closed
	asm.op(0x5A)  # must never run
	asm.op(0x00)
	var vm = AptVmScript.new()
	var result: Dictionary = vm.execute(asm.bytes, [])
	_check("unimplemented_halts", result["halted_on"] == 0x17 \
		and result["halted_reason"] == "unimplemented_opcode")
	_check("unimplemented_recorded", result["unimplemented"].get(0x17, 0) == 1)
	_check("unimplemented_stops_execution",
		result["instructions_executed"] == 1 and result["completed"] == false)
	# Histogram is cumulative per VM instance.
	var result2: Dictionary = vm.execute(asm.bytes, [])
	_check("unimplemented_histogram_accumulates",
		result2["unimplemented"].get(0x17, 0) == 2)


func _test_stack_underflow_is_clean() -> void:
	var asm := Asm.new()
	asm.op(0x12)  # Not on an empty stack
	asm.op(0x00)
	var result := _execute(asm)
	_check("stack_underflow_halts_cleanly",
		result["completed"] == false and result["halted_reason"] == "stack_underflow" \
		and result["halted_on"] == null)


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
	if scripts.is_empty():
		_check("real_fixture_has_scripts", false)
		return
	var blobs := {}
	var constants := {}
	for movie_name in movies:
		var movie: Dictionary = movies[movie_name]
		blobs[movie_name] = (movie["apt_data_hex"] as String).hex_decode()
		constants[movie_name] = movie["constants"]
	var vm = AptVmScript.new()
	var best_executed := 0
	var all_clean := true
	var total_implemented := 0
	for script in scripts:
		var movie_name: String = script["movie"]
		var entry := int(script["entry_offset"])
		var result: Dictionary = vm.execute(blobs[movie_name], constants[movie_name], entry)
		var executed := int(result["instructions_executed"])
		total_implemented += executed
		best_executed = max(best_executed, executed)
		var clean: bool = result["completed"] \
			or result["halted_reason"] == "unimplemented_opcode"
		all_clean = all_clean and clean
		print("RETAIL_APT_VM fixture movie=%s entry=0x%X executed=%d reason=%s halted_on=%s host_calls=%d" % [
			movie_name, entry, executed, result["halted_reason"],
			("0x%02X" % result["halted_on"]) if result["halted_on"] != null else "-",
			result["host_calls"].size(),
		])
	var histogram: Dictionary = vm.unimplemented
	var histogram_text := ""
	var keys: Array = histogram.keys()
	keys.sort()
	for key in keys:
		histogram_text += "0x%02X:%d " % [key, histogram[key]]
	print("RETAIL_APT_VM fixture_totals implemented_instructions=%d unimplemented_histogram={%s}" % [
		total_implemented, histogram_text.strip_edges()])
	_check("real_fixtures_run_clean", all_clean)
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
