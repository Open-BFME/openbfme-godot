extends SceneTree
## field:specialpower.ReloadTime
const Support=preload("res://tests/spellbook_signature_runtime_support.gd")
func _initialize()->void:call_deferred("_run")
func _run()->void:var r:=Support.new().run(self,"field:specialpower.ReloadTime");print("SPECIALPOWER_RELOAD_TIME_RESULT passed=%d failed=%d detail=%s"%[1 if r.ok else 0,0 if r.ok else 1,r.detail]);quit(0 if r.ok else 1)
