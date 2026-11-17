extends Node3D
## A strategic icon transform/visibility owned by the shared W3DInstancer.
## This preserves the full retail material/house-colour path instead of tinting a plain mesh.
var instancer: Node3D
var instance := -1

func bind(renderer: Node3D, id: int) -> void:
	instancer = renderer
	instance = id
	set_notify_transform(true)
	_sync()

func _notification(what: int) -> void:
	if what == NOTIFICATION_TRANSFORM_CHANGED or what == NOTIFICATION_VISIBILITY_CHANGED or what == NOTIFICATION_ENTER_TREE:
		_sync()
	elif what == NOTIFICATION_EXIT_TREE and is_instance_valid(instancer) and instance >= 0:
		instancer.remove_instance(instance)
		instance = -1

func _sync() -> void:
	if not is_inside_tree() or not is_instance_valid(instancer) or instance < 0:
		return
	instancer.set_instance_transform(instance, global_transform)
	instancer.set_instance_opacity(instance, 1.0 if is_visible_in_tree() else 0.0)
