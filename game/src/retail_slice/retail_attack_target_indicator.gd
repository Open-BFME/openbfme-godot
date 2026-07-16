class_name RetailAttackTargetIndicator
extends Node3D
## Presentation-only acknowledgement of the live target shared by the selected
## battalions. The texture is supplied by the validated retail command UI.

var _sprite: Sprite3D
var _phase := 0.0


func configure(texture: Texture2D) -> void:
	if _sprite == null:
		_sprite = Sprite3D.new()
		_sprite.name = "RetailAttackTargetIcon"
		_sprite.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		_sprite.no_depth_test = true
		_sprite.render_priority = 10
		_sprite.pixel_size = 0.012
		add_child(_sprite)
	_sprite.texture = texture
	visible = false
	set_process(texture != null)


func show_target(world_position: Vector3, height: float) -> void:
	if _sprite == null or _sprite.texture == null:
		visible = false
		return
	position = world_position + Vector3.UP * maxf(2.2, height)
	visible = true


func clear_target() -> void:
	visible = false


func _process(delta: float) -> void:
	if not visible:
		return
	_phase += delta
	var pulse := 1.0 + sin(_phase * 7.0) * 0.09
	_sprite.scale = Vector3.ONE * pulse
