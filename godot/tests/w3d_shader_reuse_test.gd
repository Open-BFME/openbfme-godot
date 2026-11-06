## Actual native factory regression, no retail files or GPU required.
## Run through w3d_shader_reuse_test.py, which builds an isolated test extension.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if not ClassDB.class_exists("W3DShaderReuseProbe"):
		printerr("SHADER REUSE FAIL: use the isolated test runner")
		quit(1)
		return
	var checks: Dictionary = ClassDB.class_call_static("W3DShaderReuseProbe", "run")
	var failures := 0
	for name in checks:
		print("SHADER REUSE ", "ok " if checks[name] else "FAIL ", name)
		if not checks[name]:
			failures += 1
	print("SHADER REUSE ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
