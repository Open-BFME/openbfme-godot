extends SceneTree
## field:specialpower.ForbiddenObjectRange
const Support=preload("res://tests/specialpower_filter_runtime_support.gd")
func _initialize()->void:var r:=Support.new().run("field:specialpower.ForbiddenObjectRange");print("SPECIALPOWER_FORBIDDEN_RANGE_RESULT passed=%d failed=%d detail=%s"%[1 if r.ok else 0,0 if r.ok else 1,r.detail]);quit(0 if r.ok else 1)
