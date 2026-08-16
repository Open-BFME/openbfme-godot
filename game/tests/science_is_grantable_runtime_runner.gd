extends SceneTree
## definition-kind:Science; field:science.IsGrantable
const Support = preload("res://tests/spellbook_signature_runtime_support.gd")
func _initialize()->void: call_deferred("_run")
func _run()->void: var r:=Support.new().run(self,"field:science.IsGrantable");print("SCIENCE_IS_GRANTABLE_RESULT passed=%d failed=%d detail=%s"%[1 if r.ok else 0,0 if r.ok else 1,r.detail]);quit(0 if r.ok else 1)
