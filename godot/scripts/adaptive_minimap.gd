extends Control
## Native visibility-filtered radar data, presented in a resizable touch surface.
var hud: Callable
var state := {}
var order_mode := false
var cursor := Vector2(.5,.5)
var _dragging := false

func _ready():
 mouse_filter=Control.MOUSE_FILTER_STOP
 custom_minimum_size=Vector2(220,220)
 tooltip_text='Tap or drag to look · Orders mode sends the current selection'

func refresh():
 var live:Node=hud.call() if hud.is_valid() else null
 state=live.get_adaptive_minimap() if is_instance_valid(live) else {}
 queue_redraw()

func picture_rect() -> Rect2:
 var side=minf(size.x,size.y)
 return Rect2((size-Vector2.ONE*side)*.5,Vector2.ONE*side)

func activate(uv:Vector2, order:bool):
 var live:Node=hud.call() if hud.is_valid() else null
 if is_instance_valid(live):live.adaptive_minimap_click(uv,order)

func _gui_input(event):
 var point:=Vector2.ZERO
 var act:=false
 if event is InputEventScreenTouch:
  _dragging=event.pressed;point=event.position;act=event.pressed
 elif event is InputEventScreenDrag:
  point=event.position;act=_dragging and not order_mode
 elif event is InputEventMouseButton:
  if event.button_index==MOUSE_BUTTON_LEFT:
   _dragging=event.pressed;point=event.position;act=event.pressed
 elif event is InputEventMouseMotion:
  point=event.position;act=_dragging and not order_mode
 if act:
  var rect=picture_rect()
  if rect.has_point(point):cursor=(point-rect.position)/rect.size;activate(cursor,order_mode)
 accept_event()

func _draw():
 var rect=picture_rect()
 draw_style_box(_border(),Rect2(Vector2.ZERO,size))
 if not state.get('ready',false):
  draw_string(ThemeDB.fallback_font,Vector2(20,35),'Radar unavailable',HORIZONTAL_ALIGNMENT_LEFT,-1,16,Color.WHITE);return
 draw_texture_rect(state.texture,rect,false)
 for dot in state.get('dots',[]):
  draw_circle(rect.position+dot.uv*rect.size,3 if dot.mine else 2,dot.color)
 var points:=PackedVector2Array()
 for point in state.get('view_box',[]):points.append(rect.position+point*rect.size)
 if points.size()==4:points.append(points[0]);draw_polyline(points,Color('#f7d487'),2,true)
 var p=rect.position+cursor*rect.size
 draw_line(p-Vector2(6,0),p+Vector2(6,0),Color.WHITE,1)
 draw_line(p-Vector2(0,6),p+Vector2(0,6),Color.WHITE,1)

func _border()->StyleBoxFlat:
 var box=StyleBoxFlat.new();box.bg_color=Color('#0a111a');box.border_color=Color('#b49b65');box.set_border_width_all(2);box.set_corner_radius_all(8);return box
