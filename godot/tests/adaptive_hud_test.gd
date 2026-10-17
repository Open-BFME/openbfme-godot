extends SceneTree
## UI contract with a mock source: no retail data or GDExtension required.
class Source extends Node:
 var native_visible:=true
 var activated:=[]
 var placement:Dictionary={}
 var order:Dictionary={}
 var store:=false
 func set_native_hud_visible(value):native_visible=value
 func get_adaptive_resources():return {"money":9000,"used":120,"limit":500,"power":5}
 func get_placement():return placement
 func get_command_buttons():return [{'slot':3,'in_palantir':false,'name':'BuildHall','title':'Barracks','state':1,'cost':300,'queued':2,'timer':.4}]
 func activate_command(slot,arc,cancel):activated.append([slot,arc,cancel]);return true
 func get_command_icon(_key):return null
 func get_controller_objects(_role):return [{'id':9,'name':'Builder'}]
 func get_spellbook_state():return {'power_entries':[{'title':'Heal','spell':0,'state':1}], 'store':{'open':store,'points':5,'purchase_entries':[{'title':'Rally','purchase':2,'state':1}]}}
 func open_spell_store():store=true;return true
 func close_spell_store():store=false
 func spell_store_click(_index):return true
 func press_spell_slot(index):activated.append(['spell',index]);return true
 func select_controller_group(ids):activated.append(['select',ids]);return ids.size()
 func deselect_all():pass
 func inject_key(_key,_down):pass
 func touch_build(action,_direction):activated.append(['placement',action]);return true
 func get_adaptive_minimap():
  var image=Image.create(256,256,false,Image.FORMAT_RGBA8);image.fill(Color('#263e38'))
  return {'ready':true,'texture':ImageTexture.create_from_image(image),'dots':[{'uv':Vector2(.4,.5),'mine':true,'color':Color('#80bbff')}],'view_box':PackedVector2Array([Vector2(.2,.3),Vector2(.6,.3),Vector2(.6,.7),Vector2(.2,.7)])}
 func adaptive_minimap_click(uv,move):order={'uv':uv,'move':move};return true

func _initialize():call_deferred('_run')
func _run():
 root.size=Vector2i(1280,800)
 var source=Source.new();root.add_child(source)
 var ui=load('res://scripts/adaptive_hud.gd').new()
 ui.hud=func():return source
 ui.gameplay=func():return true
 ui.execute=func(_code):return {'ok':true,'output':'9000\t120\t500\t5\n'}
 ui.open_menu=func():pass
 root.add_child(ui);ui.mode='modern';ui._apply_mode()
 for i in 6:await process_frame
 assert(not source.native_visible)
 ui.open_section('Commands');ui._refresh();await process_frame
 assert(ui._entries.size()==1 and ui._tiles.size()==1)
 ui._activate(0,false);assert(source.activated[-1]==[3,false,false])
 ui._palette(true);ui._controller_action('rightPressCommand');assert(source.activated[-1]==[3,false,true])
 ui.open_section('Powers');ui._refresh();ui._activate(0,false);assert(source.store)
 ui.open_section('Map');ui._refresh();assert(not source.store)
 ui._map.activate(Vector2(.3,.6),true);assert(source.order.move and source.order.uv==Vector2(.3,.6))
 for dimensions in [Vector2i(393,852),Vector2i(852,393),Vector2i(1280,800)]:
  root.size=dimensions
  for frame in 8:await process_frame
  ui._layout();await process_frame
  var safe=ui._safe_rect()
  assert(safe.encloses(ui._dock.get_global_rect()),'Dock fits '+str(dimensions)+' '+str(ui._dock.get_global_rect()))
  assert(safe.encloses(ui._panel.get_global_rect()),'Panel fits '+str(dimensions)+' '+str(ui._panel.get_global_rect()))
 ui.open_section('Map');await process_frame;assert(not ui._panel.visible)
 ui.mode='hidden';ui._apply_mode();await process_frame;assert(not source.native_visible and not ui._dock.visible)
 ui.mode='classic';ui._apply_mode();await process_frame;assert(source.native_visible)
 if '--visual' in OS.get_cmdline_user_args():
  ui.mode='modern';ui._apply_mode();ui.open_section('Commands');ui._refresh()
  for frame in 8:await process_frame
  await RenderingServer.frame_post_draw
  root.get_texture().get_image().save_png('/tmp/adaptive-hud-commands.png')
 print('ADAPTIVE HUD PASS')
 ui.free();source.free();quit()
