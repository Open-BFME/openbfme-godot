extends SceneTree
class ShellStub extends Node2D:
 var calls := {}
 var texture: ImageTexture
 func _init():
  var image := Image.create(32,32,false,Image.FORMAT_RGBA8)
  image.fill(Color.WHITE)
  texture=ImageTexture.create_from_image(image)
 func shell_mapped_image(name: String) -> Dictionary:
  calls[name]=int(calls.get(name,0))+1
  return {"ok":true,"texture":texture,"region":Rect2(4,6,16,12),"size":Vector2(16,12)}
 func stage_to_window(point: Vector2) -> Vector2: return point*2.0+Vector2(5,7)
func _initialize(): call_deferred("_run")
func _run():
 var shell := ShellStub.new()
 var graph = load("res://scripts/timeline_graph.gd").new()
 graph.shell=shell
 graph.rect=Rect2(10,20,200,100)
 graph.graph={"lines":[
  {"result":0,"points":PackedVector2Array([Vector2(1,0.5)]),"marks":PackedVector2Array([Vector2(0.25,0.2)])},
  {"result":1,"points":PackedVector2Array([Vector2.ZERO,Vector2(0.5,1)])},
  {"result":2,"points":PackedVector2Array([Vector2(0.75,0)])},
  {"result":3,"points":PackedVector2Array([Vector2.ONE])},
  {"result":4,"points":PackedVector2Array([Vector2.ONE])},
  {"result":0,"points":PackedVector2Array()}]}
 var markers: Array=graph.marker_commands()
 assert(markers.size()==4)
 assert(markers[0].name=="AptTimelineVictorious")
 assert(markers[0].rect==Rect2(194,58,32,24))
 assert(markers[1].name=="AptTimelineFortress")
 assert(markers[1].rect==Rect2(44,88,32,24))
 assert(markers[2].name=="AptTimelineDefeated")
 assert(markers[2].rect==Rect2(94,8,32,24))
 assert(markers[3].name=="AptTimelineDisconnected")
 assert(markers[3].rect==Rect2(144,108,32,24))
 assert(markers[0].region==Rect2(4,6,16,12))
 graph.marker_commands()
 for count in shell.calls.values(): assert(count==1)
 graph.free()
 shell.free()
 if not OS.get_environment("ROTWK_INSTALL").is_empty():
  var fs=ClassDB.instantiate("RetailFileSystem")
  assert(fs.mount_retail().ok)
  var world=ClassDB.instantiate("GameWorld")
  root.add_child(world)
  assert(world.setup(fs).ok)
  var native=ClassDB.instantiate("AptMenuPlayer")
  root.add_child(native)
  assert(native.boot_shell(fs,world,{"skirmish_ini":"user://timeline-markers-test.ini"}).ok)
  native.auto_process=false
  var overlay=load("res://scripts/timeline_graph.gd").new()
  overlay.shell=native
  overlay.rect=Rect2(40,40,500,250)
  overlay.graph={"lines":[
   {"color":0x0080ff,"result":0,"points":PackedVector2Array([Vector2(0,0.1),Vector2(0.9,0.8)]),"marks":PackedVector2Array([Vector2(0.3,0.33)])},
   {"color":0xff3020,"result":1,"points":PackedVector2Array([Vector2(0.6,0.4)])},
   {"color":0x20ff80,"result":2,"points":PackedVector2Array([Vector2(0.8,0.2)])}]}
  root.add_child(overlay)
  var authored: Array=overlay.marker_commands()
  assert(authored.size()==4)
  for marker in authored:
   assert(marker.texture!=null)
   assert(marker.region.has_area())
   assert(marker.rect.has_area())
  for frame in 5: await process_frame
  if DisplayServer.get_name()!="headless":
   await RenderingServer.frame_post_draw
   var capture:=OS.get_environment("OPENBFME_TIMELINE_SCREENSHOT")
   if not capture.is_empty(): assert(root.get_texture().get_image().save_png(capture)==OK)
  overlay.free()
  native.free()
  world.free()
 print("TIMELINE MARKERS PASS")
 quit()
