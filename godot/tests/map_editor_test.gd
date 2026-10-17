extends SceneTree
class EditorHUD extends Node:
 var picked := 0
 func deselect_all():pass
 func pixel_to_world(pixel):return pixel
 func world_to_pixel(point):return point
 func pick_probe(_pixel,_id):return {'picked':picked}
var failures := 0
func check(ok:bool,what:String):
 if not ok:failures+=1;push_error(what)
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 if not fs.mount_retail().ok:quit(77);return
 var world=ClassDB.instantiate('GameWorld');root.add_child(world)
 if not world.setup(fs).ok:quit(1);return
 if not world.load_map('map wor buckland',{'terrain':true}).ok:quit(1);return
 var query:Dictionary=world.map_editor_terrain({})
 check(query.ok and not query.textures.is_empty(),'Terrain catalog')
 world.set_paused(true)
 var point=Vector2(1700,1700)
 var height:float=world.get_ground_height(point.x,point.y)
 var edited:Dictionary=world.map_editor_terrain({'points':[point],'radius':40,'mode':'raise','amount':10})
 check(edited.get('ok',false),'Raise terrain: '+str(edited))
 check(is_equal_approx(world.get_ground_height(point.x,point.y),height+10),'Live terrain height follows stroke')
 check(world.get_node_or_null('Terrain')!=null,'Rebuilt terrain mesh exists')
 var undo:Dictionary=world.map_editor_terrain({'patch':edited.undo})
 check(undo.ok and is_equal_approx(world.get_ground_height(point.x,point.y),height),'Undo height stroke')
 var paint:Dictionary=world.map_editor_terrain({'points':[point],'radius':20,'mode':'paint','tile':query.textures[0].tile})
 check(paint.ok and paint.changed>0,'Ground texture stroke')
 var tile_a=world.map_editor_terrain({'sample':point})
 var tile_x=world.map_editor_terrain({'sample':point+Vector2(10,0)})
 var tile_y=world.map_editor_terrain({'sample':point+Vector2(0,10)})
 check(tile_a.tile!=tile_x.tile and tile_a.tile!=tile_y.tile,'Paint selects neighboring source tile quadrants instead of repeating one quarter')
 if tile_a.texture_cells>2:
  var tile_next=world.map_editor_terrain({'sample':point+Vector2(20,0)})
  check(int(tile_a.tile/4)!=int(tile_next.tile/4),'Paint advances through full source texture tiles')

 check(world.map_editor_terrain({'patch':paint.undo}).ok,'Undo texture stroke')
 # Real HUD ray picking must include an object without SELECTABLE.
 var pick_camera=Camera3D.new();root.add_child(pick_camera)
 var real_hud=ClassDB.instantiate('InGameHudNode');root.add_child(real_hud)
 var player=world.execute_lua('return bfme.players.localPlayer.name').output.strip_edges()
 var attached=real_hud.setup(fs,world,pick_camera,player,{'camera_start':Vector3(1700,0,-1900)})
 check(attached.get('ok',false),'Real editor picking HUD setup')
 var fountain=world.create_object('DaleFountain',0,1700,1900,0)
 world.map_editor_terrain({'sync':true})
 await process_frame
 await process_frame
 var hit=false
 for y in [0.0,10.0,30.0,50.0]:
  var pixel=pick_camera.unproject_position(Vector3(1700,world.get_ground_height(1700,1900)+y,-1900))
  if real_hud.editor_pick(pixel)==fountain:hit=true
 check(hit,'Editor ray picks a non-selectable fountain')
 world.destroy_object(fountain);world.map_editor_terrain({'sync':true})
 real_hud.free();pick_camera.free()
 var hud=EditorHUD.new();root.add_child(hud)
 var editor=load('res://scripts/map_editor.gd').new()
 editor.world=world;editor.hud=hud;editor.fs=fs;root.add_child(editor)
 world.set_shroud_drawn(true)
 editor.set_active(true)
 check(editor.active and world.is_paused() and not editor.catalog.is_empty(),'Editor enters paused mode with catalog')
 check(not world.get_shroud_report().displayed,'Editor removes fog rendering while simulation is paused')
 var sampled=world.map_editor_terrain({'sample':point})
 check(sampled.get('ok',false) and sampled.has('texture'),'Sample terrain under cursor')
 editor.tool.select(7);editor.cursor=point;editor._update_controls();editor._pick_terrain(false)
 check(editor.texture_preview.texture!=null,'Picker previews terrain under cursor')
 editor._pick_terrain(true)
 check(editor.tool.selected==6 and editor.texture_filtered[editor.texture.selected].name.to_lower()==str(sampled.texture).to_lower(),'Picker selects sampled texture and enters paint mode')
 editor.grid_rotation.button_pressed=true
 check(is_equal_approx(editor._terrain_command().grid_angle,PI/4),'45 degree brush grid option')
 editor.grid_rotation.button_pressed=false
 var original_roads=editor.roads.duplicate(true);var road_count=editor.roads.size()
 check(editor.road_type.item_count>0,'Road type catalog')
 editor.tool.select(8);editor._update_controls();editor._new_road()
 editor.cursor=Vector2(1600,1600);editor._road_press()
 editor.cursor=Vector2(1800,1600);editor._road_press()
 editor.cursor=Vector2(1800,1800);editor._road_press()
 check(editor.roads.size()==road_count+2,'Place connected road segments')
 check(world.map_editor_terrain({}).roads.size()==road_count+2,'Road changes reach live map')
 editor.road_action.select(0);editor.cursor=Vector2(1800,1600);editor._road_press()
 editor.cursor=Vector2(1850,1650);editor._road_release()
 check(editor._road_point(editor.roads[-2].points[1])==Vector2(1850,1650) and editor._road_point(editor.roads[-1].points[0])==Vector2(1850,1650),'Dragging shared road endpoint keeps path connected')
 editor._undo()
 editor.road_action.select(2);editor.cursor=Vector2(1700,1600);editor._road_press()
 check(editor.roads.size()==road_count+3,'Insert road control point')
 editor._undo();editor._undo();editor._undo()
 check(editor.roads==original_roads and editor.overlay.is_empty(),'Road undo restores authored segments')
 editor._new_road();editor.cursor=Vector2(1600,1600);editor._road_press();editor.cursor=Vector2(1800,1600);editor._road_press()
 editor.save_name.text='editor-road-regression';editor._save();editor._undo();editor._load()
 check(editor.roads.size()==road_count+1 and world.map_editor_terrain({}).roads.size()==road_count+1,'Road overlay JSON round trip')
 editor._undo();DirAccess.remove_absolute('user://map_edits/editor-road-regression.json')
 check(editor.panel.theme.default_font_size==24,'Larger editor font theme')
 check(editor.texture_preview.texture!=null,'Selected terrain texture has an image preview')
 check(editor.texture_catalog.size()>query.textures.size(),'Catalog includes textures outside current map')
 check(editor.texture_category.item_count>1 and editor.texture_subcategory.item_count>1,'Terrain categories and regions')
 editor.texture_search.text='AsphaltType2';editor._filter_textures()
 check(editor.texture.item_count==1,'Terrain search filters by name')
 editor.tool.select(6);editor.points.clear();editor.points.append(point);editor._apply()
 check(editor.undo_stack.size()==1,'Paint catalog texture not originally in map')
 editor._undo();editor.texture_search.text='';editor._filter_textures()
 if editor.texture.item_count>1:
  editor.texture.select(1);editor._texture_selected(1)
  check(editor.texture_preview.texture!=null,'Changing terrain selection refreshes image preview')
 editor._filter('DaleBuilding04');check(editor.objects.item_count==1,'Object search')
 editor.objects.select(0);editor.tool.select(1);editor._update_controls();editor.points.clear();editor.points.append(point)
 editor.angle_control.value=45
 editor.cursor=point
 await process_frame
 await process_frame
 check(editor.preview_instance>=0,'Selected template has a live placement preview')
 check(editor.toolbar.visible and editor.lookup.visible and editor.mode_buttons.size()==10,'Toolbar modes and dedicated lookup window')
 check(editor.object_renderer!=null and editor.object_view.own_world_3d and editor.object_image.texture!=null,'Isolated render-to-texture object viewer')
 check(editor.object_camera.size>0,'Object preview camera is framed from model bounds')
 var visible_before=int(world.get_node('LiveObjects').get_stats().visible_draw_instances)
 check(editor.panel.size.x<700,'Editor panel stays bounded despite long catalog entries')
 var press=InputEventMouseButton.new();press.button_index=MOUSE_BUTTON_LEFT;press.pressed=true;press.position=point
 press.shift_pressed=true
 check(editor.handle_input(press),'Placement mouse press reaches map')
 var release=InputEventMouseButton.new();release.button_index=MOUSE_BUTTON_LEFT;release.pressed=false;release.position=point+Vector2(100,100);release.shift_pressed=true
 check(editor.handle_input(release),'Placement mouse release reaches map')
 check(editor.points.size()==1 and editor.points[0]==point,'Rotation drag preserves anchored position')
 check(editor._snap_point(Vector2(1718,1684))==Vector2(1700,1700),'Shift snaps placement position to grid')
 check(is_equal_approx(editor._placement_angle(),PI/4),'Shift snaps drag rotation to 45 degrees')
 check(int(world.get_node('LiveObjects').get_stats().visible_draw_instances)>visible_before,'Placed object is rendered immediately while paused')
 check(editor.undo_stack.size()==1,'Place action has undo')
 var id=int(editor.undo_stack[0].remove[0]);check(world.get_object(id).ok,'Placed object exists')
 check(is_equal_approx(world.get_object(id).angle,PI/4),'Placed object retains preview angle')
 hud.picked=id;editor.tool.select(9);editor.start=point;editor.cursor=point;editor._apply()
 check(editor.selected_objects==[id],'Editor selects object for owner editing')
 editor.owner_slot.value=1;editor._apply_owner()
 check(world.get_object(id).player_id==1,'Selected object owner changes in place')
 editor._undo();check(world.get_object(id).player_id==0,'Undo owner change')
 hud.picked=id;editor.tool.select(0);editor.start=point;editor.cursor=point;editor._apply()
 check(not world.get_object(id).ok,'Click delete removes object while paused')
 editor._undo();check(editor.overlay.size()==1,'Undo delete')
 editor._undo();check(editor.overlay.is_empty(),'Undo placement')
 var remaining=0
 for raw in world.get_object_ids():
  var data=world.get_object(raw)
  if data.get('ok',false) and data.template=='DaleBuilding04' and Vector2(data.x,data.y).distance_to(point)<1:remaining+=1
 check(remaining==0,'Undo delete followed by undo placement tracks replacement IDs')
 # Area deletion includes unselected objects by their projected origins.
 var a=world.create_object('DaleBuilding04',0,1650,1650,0)
 var b=world.create_object('DaleBuilding04',0,1750,1750,0)
 editor.tool.select(0);editor.start=Vector2(1600,1600);editor.cursor=Vector2(1800,1800);editor._apply()
 check(not world.get_object(a).ok and not world.get_object(b).ok,'Drag rectangle deletes multiple objects')
 editor._undo()
 editor.tool.select(3);editor.cursor=point;editor.strength.value=10
 editor._begin_stroke()
 check(editor.dragging and is_equal_approx(world.get_ground_height(point.x,point.y),height+10),'Terrain updates before brush release')
 editor.points.append(point+Vector2(10,0));editor._paint_live()
 check(is_equal_approx(world.get_ground_height(point.x,point.y),height+10),'Overlapping live samples apply height only once')
 editor.dragging=false;editor._apply()
 check(editor.undo_stack[-1].has('terrain'),'Live stroke has a single terrain undo')
 editor.save_name.text='editor-regression';editor._save()
 check(FileAccess.file_exists('user://map_edits/editor-regression.json'),'JSON edit file saved')
 editor._undo();editor._load()
 check(editor.overlay.size()==1,'JSON edits loaded')
 check(is_equal_approx(world.get_ground_height(point.x,point.y),height+10),'Loaded height stroke affects live terrain')
 editor._undo()
 if '--visual' in OS.get_cmdline_user_args():
  root.size=Vector2i(1280,900)
  var camera=Camera3D.new();world.add_child(camera)
  camera.position=Vector3(1700,800,-1200);camera.look_at(Vector3(1700,100,-1700));camera.current=true
  editor.tool.select(6);editor._update_controls();editor.cursor=Vector2(850,400)
  await process_frame
  await process_frame
  await RenderingServer.frame_post_draw
  root.get_texture().get_image().save_png('/tmp/map-editor-ui.png')
  editor.tool.select(1);editor._update_controls()
  await process_frame
  await process_frame
  await RenderingServer.frame_post_draw
  root.get_texture().get_image().save_png('/tmp/map-editor-object-ui.png')
 editor.set_active(false);check(world.is_paused(),'Previous pause state restored')
 check(world.get_shroud_report().displayed,'Editor restores previous fog rendering')
 DirAccess.remove_absolute('user://map_edits/editor-regression.json')
 editor.free();hud.free();world.free()
 print('MAP EDITOR ', 'PASS' if failures==0 else 'FAIL');quit(0 if failures==0 else 1)
