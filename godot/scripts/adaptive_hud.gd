extends CanvasLayer
## Enhanced HUD. Native APIs retain orders, availability, visibility and selection.
var hud: Callable
var gameplay: Callable
var execute: Callable
var open_menu: Callable
var open_console: Callable
var bridge: Node
var mode := 'classic'
var section := ''
var _top: PanelContainer
var _dock: PanelContainer
var _tabs: GridContainer
var _panel: PanelContainer
var _contents: VBoxContainer
var _grid: GridContainer
var _scroll: ScrollContainer
var _status: Label
var _title: Label
var _notice: Label
var _map: Control
var _tiles: Array[Button] = []
var _entries: Array = []
var _selected := 0
var _elapsed := 0.0
var _signature := ''
var _icons := {}
var _live: Node
var _store_owned := false
var _map_large := false
var _rotation := 0.0
var _pad_open := false
var _config := ConfigFile.new()
const SECTIONS=['Commands','Powers','Heroes','Builders','Map','System']

func _ready():
 layer=38
 var loaded=_config.load('user://adaptive-hud.cfg')
 if loaded!=OK and loaded!=ERR_FILE_NOT_FOUND:push_error('Cannot load HUD preference: '+error_string(loaded))
 mode=str(_config.get_value('hud','mode','modern' if OS.has_feature('ios') else 'classic'))
 if mode not in ['classic','modern','hidden']:mode='classic'
 _top=_panel_box();add_child(_top)
 var top=HBoxContainer.new();_top.add_child(top)
 _status=Label.new();_status.text='Battle controls';_status.custom_minimum_size.x=180;top.add_child(_status)
 var toggle=_button('HUD',Vector2(72,64));toggle.tooltip_text='Cycle Classic / Touch & Controller / Hidden · F10';toggle.pressed.connect(cycle_mode);top.add_child(toggle)
 _dock=_panel_box();add_child(_dock)
 var dock=GridContainer.new();_tabs=dock;dock.columns=6;dock.add_theme_constant_override('h_separation',6);dock.add_theme_constant_override('v_separation',6);_dock.add_child(dock)
 for name in SECTIONS:
  var button=_button(name,Vector2(86,62));button.pressed.connect(open_section.bind(name));dock.add_child(button)
 _panel=_panel_box();add_child(_panel)
 _contents=VBoxContainer.new();_contents.add_theme_constant_override('separation',10);_panel.add_child(_contents)
 var heading=HBoxContainer.new();_contents.add_child(heading)
 _title=Label.new();_title.size_flags_horizontal=Control.SIZE_EXPAND_FILL;heading.add_child(_title)
 var close=_button('Close',Vector2(72,52));close.pressed.connect(func():open_section(''));heading.add_child(close)
 _notice=Label.new();_notice.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;_contents.add_child(_notice)
 _scroll=ScrollContainer.new();_scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;_scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;_contents.add_child(_scroll)
 _grid=GridContainer.new();_grid.columns=3;_grid.add_theme_constant_override('h_separation',8);_grid.add_theme_constant_override('v_separation',8);_scroll.add_child(_grid)
 _map=preload('res://scripts/adaptive_minimap.gd').new();_map.hud=hud;_contents.add_child(_map)
 if is_instance_valid(bridge):
  bridge.palette_toggled.connect(_palette)
  bridge.command_action.connect(_controller_action)
  bridge.controller_disconnected.connect(func():_pad_open=false)
 _apply_mode()

func _exit_tree():
 if is_instance_valid(_live):_live.set_native_hud_visible(true)
 if _store_owned and is_instance_valid(_live):_live.close_spell_store()
 Engine.remove_meta('adaptive_hud_rects');Engine.remove_meta('adaptive_hud_active');Engine.remove_meta('adaptive_hud_replaces_native');Engine.remove_meta('touch_build_controls')

func cycle_mode():
 var modes=['classic','modern','hidden'];mode=modes[(modes.find(mode)+1)%3]
 _config.set_value('hud','mode',mode)
 var result=_config.save('user://adaptive-hud.cfg')
 if result!=OK:push_error('Cannot save HUD preference: '+error_string(result))
 open_section('');_apply_mode()

func _apply_mode():
 Engine.set_meta('adaptive_hud_active',mode=='modern')
 Engine.set_meta('adaptive_hud_replaces_native',mode!='classic')
 if is_instance_valid(_live):_live.set_native_hud_visible(mode=='classic')
 _signature=''

func _unhandled_key_input(event):
 if event is InputEventKey and event.pressed and not event.echo and event.keycode==KEY_F10:
  cycle_mode();get_viewport().set_input_as_handled()

func open_section(value:String):
 var next_section='' if value==section else value
 if _store_owned and next_section!='Powers' and is_instance_valid(_live):_live.close_spell_store();_store_owned=false
 section=next_section;_signature='';_selected=0

func _palette(open:bool):
 if mode!='modern':return
 _pad_open=open
 if open:
  section='Commands';_selected=0;_signature=''
 else:
  open_section('')

func _process(delta):
 var active:bool=gameplay.is_valid() and gameplay.call()
 var live:Node=hud.call() if hud.is_valid() else null
 if live!=_live:
  if is_instance_valid(_live):_live.set_native_hud_visible(true)
  _live=live;_icons.clear();_apply_mode()
 _top.visible=active and is_instance_valid(_live)
 _status.visible=mode!='hidden'
 _dock.visible=_top.visible and mode=='modern'
 _panel.visible=_dock.visible and not section.is_empty()
 if not active:
  if _store_owned and is_instance_valid(_live):_live.close_spell_store();_store_owned=false
  Engine.set_meta('adaptive_hud_rects',[]);return
 if not is_instance_valid(_live):return
 Engine.set_meta('touch_build_controls',mode=='modern')
 _elapsed+=delta
 if _elapsed>=.2:
  _elapsed=0
  if mode=='modern':_refresh()
 _layout()
 var rects=[]
 for control in [_top,_dock,_panel]:
  if control.visible:rects.append(control.get_global_rect())
 Engine.set_meta('adaptive_hud_rects',rects)

func _safe_rect()->Rect2:
 var view=get_viewport().get_visible_rect()
 var safe=DisplayServer.get_display_safe_area()
 var window=DisplayServer.window_get_size()
 if OS.has_feature('ios') and safe.size.x>0 and window.x>0 and window.y>0:
  var factor=view.size/Vector2(window)
  return Rect2(Vector2(safe.position)*factor,Vector2(safe.size)*factor).intersection(view)
 return view

func _layout():
 var safe=_safe_rect().grow(-12)
 var scale_factor=clampf(safe.size.y/800.0,.72,1.35)
 _tabs.columns=3 if safe.size.x<600*scale_factor else 6
 for p in [_top,_dock,_panel]:p.scale=Vector2.ONE*scale_factor
 _top.size=_top.get_combined_minimum_size();_top.position=safe.position
 _dock.size=_dock.get_combined_minimum_size();_dock.position=safe.end-Vector2(_dock.size.x*scale_factor,_dock.size.y*scale_factor)
 var available=Vector2(minf(520*scale_factor,safe.size.x),maxf(80,safe.size.y-_top.size.y*scale_factor-_dock.size.y*scale_factor-24))
 _panel.size=available/scale_factor
 _panel.position=Vector2(safe.end.x-available.x,_top.position.y+_top.size.y*scale_factor+10)
 _grid.columns=maxi(1,int((_panel.size.x-24)/112))
 _map.custom_minimum_size=Vector2(0,minf(360 if _map_large else 220,maxf(64,_panel.size.y-200)))
 _map.size_flags_horizontal=Control.SIZE_EXPAND_FILL

func _refresh():
 var resource:Dictionary=_live.get_adaptive_resources()
 if not resource.is_empty():_status.text='Gold %s   Army %s/%s   Power %s'%[resource.money,resource.used,resource.limit,resource.power]
 var placing:Dictionary=_live.get_placement()
 if placing.get('placing',false) and section!='Placement':section='Placement';_signature=''
 elif not placing.get('placing',false) and section=='Placement':section='Commands';_signature=''
 _entries=[]
 _title.text=section
 _map.visible=section=='Map'
 _scroll.visible=section!='Map'
 _notice.text='Hold RT (or Tab) · D-pad moves · A activates · B cancels' if _pad_open else 'Tap a panel to expand · tap it again to collapse'
 match section:
  'Commands':
   var seen={}
   for entry in _live.get_command_buttons():
    var key='%s:%s'%[entry.slot,entry.in_palantir]
    if int(entry.state)!=0 and not seen.has(key):_entries.append(entry);seen[key]=true
   if _entries.is_empty():_notice.text='Select a unit, builder, plot, or building to see its commands.'
  'Heroes','Builders':
   for entry in _live.get_controller_objects(section.to_lower()):_entries.append({'title':entry.name,'id':entry.id})
  'Powers':
   var spells:Dictionary=_live.get_spellbook_state()
   if _store_owned and spells.get('store',{}).get('open',false):
    var store:Dictionary=spells.store
    _notice.text='Power points: %s'%store.get('points',0)
    _entries.append_array(store.get('purchase_entries',[]))
    _entries.append({'title':'Back to powers','back_store':true})
   else:
    _entries.append({'title':'Unlock powers','store':true})
    _entries.append_array(spells.get('power_entries',[]))
  'Placement':
   _notice.text='Drag on terrain to position. Lock, then rotate and confirm.'
   for title in ['Reposition','Lock','Rotate left','Rotate right','Build','Cancel']:
    _entries.append({'title':title,'placement':title,'state':1 if title!='Build' or (placing.get('anchored',false) and placing.get('legal',-1)==0) else 2})
  'Map':
   _map.refresh();_entries=[{'title':'Look / orders','map_orders':true,'detail':'Orders' if _map.order_mode else 'Look'},{'title':'Resize map','map_size':true}]
   _scroll.visible=true;_notice.text='Tap / drag to look. Orders sends selected units. Controller: D-pad aims, A looks, B orders.'
  'System':
   for title in ['Pause / options','Deselect','Stop','Select army','Console','Classic HUD','Hide HUD']:_entries.append({'title':title,'system':title})
 var signature=JSON.stringify(_entries)
 if signature!=_signature:
  _signature=signature;_render()

func _render():
 _selected=clampi(_selected,0,maxi(0,_entries.size()-1))
 for child in _grid.get_children():_grid.remove_child(child);child.queue_free()
 _tiles.clear()
 for i in _entries.size():
  var entry:Dictionary=_entries[i]
  var title=str(entry.get('title',entry.get('template',entry.get('name','Command'))))
  if title.is_empty():title=str(entry.get('command','Command'))
  var suffix=''
  if int(entry.get('cost',0))>0:suffix+='\n%d gold'%int(entry.cost)
  if int(entry.get('queued',0))>0:suffix+=' · ×%d'%int(entry.queued)
  if entry.has('detail'):suffix+='\n'+str(entry.detail)
  var button=_button(title+suffix,Vector2(104,56 if section=='Map' else 104))
  button.tooltip_text=title+'\n'+str(entry.get('description',''))
  var icon_name=str(entry.get('image',''))
  if not icon_name.is_empty():
   if not _icons.has(icon_name):_icons[icon_name]=_live.get_command_icon(icon_name)
   button.icon=_icons[icon_name];button.expand_icon=true;button.icon_alignment=HORIZONTAL_ALIGNMENT_CENTER;button.vertical_icon_alignment=VERTICAL_ALIGNMENT_TOP;button.add_theme_constant_override('icon_max_width',48)
  button.disabled=int(entry.get('state',1)) not in [1,5]
  button.pressed.connect(_activate.bind(i,false));_grid.add_child(button);_tiles.append(button)
  if float(entry.get('timer',-1))>=0:
   var progress=ProgressBar.new();progress.mouse_filter=Control.MOUSE_FILTER_IGNORE;progress.show_percentage=false;progress.max_value=1;progress.value=float(entry.timer);progress.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE);progress.offset_top=-6;button.add_child(progress)
 _highlight()

func _activate(index:int,cancel:bool):
 if index<0 or index>=_entries.size() or not is_instance_valid(_live):return
 var entry:Dictionary=_entries[index]
 if entry.has('slot'):_live.activate_command(entry.slot,entry.in_palantir,cancel)
 elif entry.has('id'):_live.select_controller_group(PackedInt64Array([entry.id]));open_section('Commands')
 elif entry.has('spell'):_live.press_spell_slot(entry.spell)
 elif entry.has('store'):_store_owned=_live.open_spell_store()
 elif entry.has('purchase'):_live.spell_store_click(entry.purchase)
 elif entry.has('back_store'):_live.close_spell_store();_store_owned=false
 elif entry.has('map_orders'):_map.order_mode=not _map.order_mode
 elif entry.has('map_size'):_map_large=not _map_large
 elif entry.has('placement'):
  var actions={'Reposition':'reposition','Lock':'lock','Build':'confirm','Cancel':'cancel'}
  if actions.has(entry.placement):_live.touch_build(actions[entry.placement],Vector2.ZERO)
  else:
   _rotation+=-PI/8 if entry.placement=='Rotate left' else PI/8
   _live.touch_build('rotate',Vector2.from_angle(_rotation))
 elif entry.has('system'):
  match entry.system:
   'Pause / options':open_menu.call()
   'Deselect':_live.deselect_all()
   'Stop':_live.inject_key(31,true);_live.inject_key(31,false)
   'Select army':
    var ids=PackedInt64Array()
    for unit in _live.get_controller_objects('units'):ids.append(unit.id)
    _live.select_controller_group(ids)
   'Console':if open_console.is_valid():open_console.call()
   'Classic HUD':mode='classic';_config.set_value('hud','mode',mode);_config.save('user://adaptive-hud.cfg');open_section('');_apply_mode()
   'Hide HUD':mode='hidden';_config.set_value('hud','mode',mode);_config.save('user://adaptive-hud.cfg');open_section('');_apply_mode()
 _signature='';_refresh()

func _controller_action(action:String):
 if mode!='modern' or not _pad_open:return
 if section=='Placement' and action=='rightPressCommand':
  _live.touch_build('cancel',Vector2.ZERO);_refresh();return
 if section=='Map' and action in ['previousCommand','nextCommand','shortcutUp','shortcutDown','pressCommand','rightPressCommand']:
  var offsets={'previousCommand':Vector2(-.05,0),'nextCommand':Vector2(.05,0),'shortcutUp':Vector2(0,-.05),'shortcutDown':Vector2(0,.05)}
  if offsets.has(action):_map.cursor=(_map.cursor+offsets[action]).clamp(Vector2.ZERO,Vector2.ONE);_map.queue_redraw()
  else:_map.activate(_map.cursor,action=='rightPressCommand')
  return
 match action:
  'previousCommand':_selected=maxi(0,_selected-1)
  'nextCommand':_selected=mini(_entries.size()-1,_selected+1)
  'shortcutUp','shortcutDown':
   var current=SECTIONS.find(section);section=SECTIONS[posmod(current+(-1 if action=='shortcutUp' else 1),SECTIONS.size())];_selected=0;_signature='';_refresh()
  'pressCommand','rightPressCommand':_activate(_selected,action=='rightPressCommand')
  'backPalette':open_section('')
 _highlight()

func _highlight():
 for i in _tiles.size():_tiles[i].modulate=Color('#ffe2a3') if _pad_open and i==_selected else Color.WHITE
 if _pad_open and _selected>=0 and _selected<_tiles.size():_scroll.ensure_control_visible(_tiles[_selected])

func _panel_box()->PanelContainer:
 var panel=PanelContainer.new();var style=StyleBoxFlat.new();style.bg_color=Color('#101c2af5');style.border_color=Color('#52667d');style.set_border_width_all(1);style.set_corner_radius_all(14)
 for edge in ['left','right','top','bottom']:style.set('content_margin_'+edge,12)
 panel.add_theme_stylebox_override('panel',style);return panel

func _button(title:String,minimum:Vector2)->Button:
 var button=Button.new();button.text=title;button.custom_minimum_size=minimum;button.focus_mode=Control.FOCUS_NONE;button.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;button.add_theme_font_size_override('font_size',16)
 for state in ['normal','hover','pressed','disabled']:
  var style=StyleBoxFlat.new();style.bg_color=Color('#283b50') if state!='pressed' else Color('#53694f');style.border_color=Color('#6a7f97') if state!='disabled' else Color('#33404e');style.set_border_width_all(1);style.set_corner_radius_all(10);style.content_margin_left=8;style.content_margin_right=8;style.content_margin_top=8;style.content_margin_bottom=8;button.add_theme_stylebox_override(state,style)
 return button
