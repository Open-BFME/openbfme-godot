extends SceneTree
## Synthetic regression oracle for corpus-wide screen VM fixes. Kept separate
## because retail_apt_vm_runner.gd is already over the repository's 1,000-line
## growth ceiling.

const AptVmScript := preload("res://src/apt/apt_vm.gd")
const AptRuntimeHostScript := preload("res://src/apt/apt_runtime_host.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_object_playback_label_is_total()
	_test_zero_parameter_function_ignores_unused_table_pointer()
	print("SCREEN_VM_REGRESSION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed else 0)


func _test_object_playback_label_is_total() -> void:
	var host = AptRuntimeHostScript.new(0, "_root")
	var clip_path: String = host.add_clip("_root", "clip", {"total_frames": 2})
	host.set_scope(clip_path)
	var object_label := RefCounted.new()
	host.playback("goto_frame2", [object_label, 0])
	var clip: Dictionary = host.widget_state(clip_path)
	_check("object_playback_label_is_total",
		String(clip.label) == str(object_label) and not bool(clip.playing))


func _test_zero_parameter_function_ignores_unused_table_pointer() -> void:
	var bytes := PackedByteArray()
	bytes.resize(42)
	bytes[0] = 0x9B  # DefineFunction; operand aligns to byte 4.
	bytes.encode_u32(4, 29)  # function-name string
	bytes.encode_u32(8, 0)  # parameter count
	bytes.encode_u32(12, 0x7FFFFFF0)  # authored but unused table pointer
	bytes.encode_s32(16, 0)  # empty body
	bytes[28] = 0x00  # End
	var name := "emptyHandler".to_utf8_buffer()
	for index in name.size():
		bytes[29 + index] = name[index]
	var vm = AptVmScript.new()
	var result: Dictionary = vm.execute(bytes, [])
	_check("zero_parameter_function_ignores_unused_table_pointer",
		bool(result.completed) and String(result.halted_reason) == "end")


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
		print("SCREEN_VM_REGRESSION PASS " + label)
	else:
		failed += 1
		print("SCREEN_VM_REGRESSION FAIL " + label)
