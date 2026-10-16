extends SceneTree
func _initialize(): call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 assert(fs.mount_retail().ok)
 var w=ClassDB.instantiate('GameWorld');root.add_child(w);assert(w.setup(fs).ok)
 for map in ['map wor buckland','map wor minhiriath','map wor rohan']:
  assert(w.load_map(map,{'slots':[{'player':'Player_1','faction':'FactionMen','human':true}]}).ok)
  var result=w.execute_lua("local m=bfme.map.getWallHubSites(); assert(#m>0); assert(m[1].properties.GenericAIObjectWallHubNumber~=nil); return #m")
  assert(result.ok,str(result))
  print('WALL MARKER DATA ',map,': ',result.output)
  var camera=Camera3D.new();root.add_child(camera)
  var hud=ClassDB.instantiate('InGameHudNode');root.add_child(hud)
  var setup=hud.setup(fs,w,camera,'Player_1',{'camera_start':Vector3(1800,0,-1800)})
  assert(setup.ok,str(setup))
  hud.set_view_options({'map_site_markers':true})
  var node=w.get_node('FarmAndWallHubMarkers')
  assert(node.mesh.get_surface_count()==1)
  var vertices=node.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
  assert(vertices.size()>=int(result.output)*68)
  print('WALL MARKER RENDER ',vertices.size(),' vertices')
  hud.free();camera.free()
 print('WALL MARKER DATA PASS')
 quit()
