extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
 if not ok:
  _failures.append(reason)
  var stack := get_stack()
  printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
 return ok

func _finish(pass_message: String) -> void:
 if _failures.is_empty():
  print(pass_message)
  quit()
  return
 printerr(_failures.size(), " check(s) failed")
 quit(1)

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
 if not _check(markers.size()==4):
  _finish("")
  return
 _check(markers[0].name=="AptTimelineVictorious")
 _check(markers[0].rect==Rect2(194,58,32,24))
 _check(markers[1].name=="AptTimelineFortress")
 _check(markers[1].rect==Rect2(44,88,32,24))
 _check(markers[2].name=="AptTimelineDefeated")
 _check(markers[2].rect==Rect2(94,8,32,24))
 _check(markers[3].name=="AptTimelineDisconnected")
 _check(markers[3].rect==Rect2(144,108,32,24))
 _check(markers[0].region==Rect2(4,6,16,12))
 graph.marker_commands()
 for count in shell.calls.values(): _check(count==1)
 graph.free()
 shell.free()
 if not OS.get_environment("ROTWK_INSTALL").is_empty():
  var fs=ClassDB.instantiate("RetailFileSystem")
  _check(fs.mount_retail().ok)
  var world=ClassDB.instantiate("GameWorld")
  root.add_child(world)
  _check(world.setup(fs).ok)
  var native=ClassDB.instantiate("AptMenuPlayer")
  root.add_child(native)
  _check(native.boot_shell(fs,world,{"skirmish_ini":"user://timeline-markers-test.ini"}).ok)
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
  if not _check(authored.size()==4):
   _finish("")
   return
  for marker in authored:
   _check(marker.texture!=null)
   _check(marker.region.has_area())
   _check(marker.rect.has_area())
  for frame in 5: await process_frame
  if DisplayServer.get_name()!="headless":
   await RenderingServer.frame_post_draw
   var capture:=OS.get_environment("OPENBFME_TIMELINE_SCREENSHOT")
   if not capture.is_empty(): _check(root.get_texture().get_image().save_png(capture)==OK)
  overlay.free()
  native.free()
  world.free()
 _finish("TIMELINE MARKERS PASS")
