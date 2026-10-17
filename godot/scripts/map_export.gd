extends SceneTree
## Batch worker. Run through tools/maps/export_maps.py; each map gets a fresh process.
var request: Dictionary

func _initialize():
 call_deferred('_run')

func _save(path: String, value) -> bool:
 var file=FileAccess.open(path,FileAccess.WRITE)
 if file==null:
  push_error('Cannot write '+path);return false
 file.store_string(JSON.stringify(_json(value),'  '));file.close();return true

func _json(value):
 if value is Dictionary:
  var result={}
  for key in value:result[str(key)]=_json(value[key])
  return result
 if value is Array or value is PackedFloat32Array or value is PackedFloat64Array or value is PackedStringArray:
  var result=[]
  for item in value:result.append(_json(item))
  return result
 if value is Vector3:return {'x':value.x,'y':value.y,'z':value.z}
 if value is Vector2:return {'x':value.x,'y':value.y}
 if value is Color:return {'r':value.r,'g':value.g,'b':value.b,'a':value.a}
 return value

func _fail(message: String):
 _save(request.output,{'ok':false,'errors':[message]});quit(1)

func _run():
 var args=OS.get_cmdline_user_args()
 if args.size()!=1:push_error('Expected a request JSON path');quit(1);return
 var parsed=JSON.parse_string(FileAccess.get_file_as_string(args[0]))
 if not parsed is Dictionary:push_error('Invalid request JSON');quit(1);return
 request=parsed
 if not ClassDB.class_exists('RetailFileSystem'):_fail('OpenBFME GDExtension is not loaded');return
 var fs=ClassDB.instantiate('RetailFileSystem')
 var mounted:Dictionary=fs.mount_retail()
 if not mounted.get('ok',false):_fail('Retail mount failed: '+str(mounted));return
 if request.mode=='catalog':
  var world=ClassDB.instantiate('GameWorld');root.add_child(world)
  var setup:Dictionary=world.setup(fs)
  if not setup.get('ok',false):_fail('World setup failed: '+str(setup));return
  var shell=ClassDB.instantiate('AptMenuPlayer');root.add_child(shell)
  var boot:Dictionary=shell.boot_shell(fs,world,{'skirmish_ini':request.profile})
  if not boot.get('ok',false):_fail('Shell setup failed: '+str(boot));return
  if not shell.shell_push('Skirmish.apt'):_fail('Cannot open skirmish catalog');return
  for i in 80:shell.tick(.03)
  var applied:Dictionary=shell.lobby_apply({'profile':'MapExport'})
  if not applied.get('ok',false):_fail('Cannot initialize catalog: '+str(applied));return
  for i in 30:shell.tick(.03)
  var entries:Array=shell.lobby_map_catalog()
  if entries.is_empty():_fail('Mounted skirmish catalog is empty');return
  quit(0 if _save(request.output,{'ok':true,'content_signature':fs.content_signature(),'maps':entries}) else 1);return
 var entry:Dictionary=request.entry
 var key=str(entry.key).replace('\\','/').get_file().get_basename()
 var visual=not request.metadata_only
 var builder=ClassDB.instantiate('MapTerrainBuilder')
 var probes=PackedVector2Array()
 for start in entry.starts:probes.append(Vector2(start.x,start.y))
 var terrain=builder.build_map(fs,key,{'terrain':visual,'water':visual,'rivers':visual,'roads':visual,'markers':false,'fog':false,'height_probes':probes})
 var report:Dictionary=builder.get_report()
 var result={'schema_version':1,'ok':terrain!=null,'map':entry,'content_signature':fs.content_signature(),
  'terrain':report.get('map_info',{}),'camera':report.get('camera',{}),'errors':report.get('errors',[]),
  'preview':{'status':'disabled' if not visual else 'failed','model':null,
   'model_unavailable_reason':'Custom terrain blending and W3D bone-palette shaders cannot be preserved by direct glTF export.'}}
 if terrain==null:quit(0 if _save(request.output,result) else 1);return
 var scanner=ClassDB.instantiate('MapObjectBuilder')
 result.features=scanner.scan_definition(fs,key)
 if not result.features.get('ok',false):result.ok=false
 if visual:
  if DisplayServer.get_name()=='headless':
   result.preview.error='A rendering display is required for screenshots; use metadata-only on headless hosts.'
  else:
   var viewport=SubViewport.new();viewport.size=Vector2i(request.width,request.height);viewport.own_world_3d=true
   viewport.render_target_update_mode=SubViewport.UPDATE_ALWAYS;root.add_child(viewport)
   var scene=Node3D.new();viewport.add_child(scene);scene.add_child(terrain)
   var view:Dictionary=report.get('camera',{})
   var sx=float(view.get('size_x',1000));var sy=float(view.get('size_y',1000))
   var helpers=load('res://scripts/skirmish_map_browser.gd').new()
   helpers._clip_materials(terrain,Vector4(0,0,sx,sy))
   var objects=scanner.build_objects(fs,key,{'clip_rect':Rect2(0,0,sx,sy),'skybox':false,'map_ini':'objects','animations':false,'texture_animation':false})
   result.object_report=scanner.get_report()
   if objects:terrain.add_child(objects)
   else:result.ok=false;result.preview.object_error='Object layer failed; screenshot contains terrain only.'
   var environment=WorldEnvironment.new();environment.environment=Environment.new()
   environment.environment.background_mode=Environment.BG_COLOR;environment.environment.background_color=Color('#0b1114')
   environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;scene.add_child(environment)
   var sun=DirectionalLight3D.new();scene.add_child(sun)
   helpers.environment=environment;helpers.sun=sun;helpers._light(result.object_report.get('lighting',{}));helpers.free()
   var camera=Camera3D.new();camera.near=5;camera.far=100000;camera.fov=45;scene.add_child(camera);camera.current=true
   var center:Vector3=view.get('center',Vector3.ZERO)
   # Fit a bounding sphere, including elevation, for either portrait or landscape output.
   var vertical=deg_to_rad(camera.fov)*.5
   var half_angle=minf(vertical,atan(tan(vertical)*float(request.width)/float(request.height)))
   var info:Dictionary=result.terrain
   var elevation=float(info.get('elevation_max',0))-float(info.get('elevation_min',0))
   var distance=Vector3(sx,elevation,sy).length()*.55/sin(half_angle)
   camera.position=center+Vector3(0,sin(.85),cos(.85))*distance;camera.look_at(center)
   for frame in 4:await process_frame
   await RenderingServer.frame_post_draw
   var image=viewport.get_texture().get_image()
   var status=ERR_CANT_CREATE if image==null or image.is_empty() else image.save_png(request.image)
   if status==OK:result.preview.status='screenshot';result.preview.image=str(request.image).get_file()
   else:result.preview.error='Screenshot failed: '+error_string(status)
 else:terrain.free()
 quit(0 if _save(request.output,result) else 1)
