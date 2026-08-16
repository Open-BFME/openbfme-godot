extends SceneTree
## field:experiencelevel.ExperienceAward
const Support=preload("res://tests/experiencelevel_runtime_support.gd")
func _initialize()->void:var r:=Support.new().run("field:experiencelevel.ExperienceAward");print("EXPERIENCE_AWARD_RESULT passed=%d failed=%d detail=%s"%[1 if r.ok else 0,0 if r.ok else 1,r.detail]);quit(0 if r.ok else 1)
