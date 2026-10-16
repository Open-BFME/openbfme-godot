extends SceneTree
## Lane LUAAI-1: a computer player taken over by the opt-in Lua skirmish AI (godot/dev/05_skirmish_ai.lua) in a real skirmish on Evendim. The native AI stops
## deciding, the Lua AI builds and trains through the bfme API, and a unit order moves a human unit. Needs ROTWK_INSTALL / BFME2_INSTALL
## (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
var _failures := 0
var _world: Node3D


func _initialize() -> void:
	call_deferred("_main")


func _main() -> void:
	var code := _run()
	print("LUA AI %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _lua(code: String) -> String:
	var r: Dictionary = _world.execute_lua(code)
	if not r.ok:
		print("  lua error: ", r.output)
		_failures += 1
	return String(r.output).strip_edges()


func _frames(n: int) -> void:
	_world.update_lua() # the queued orders and switches go to the next frame's command list first
	for i in n:
		_world.advance(0.2)


func _message() -> Dictionary:
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Human", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": 0}
	slots[1] = {"state": 3, "name": "Medium AI", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": 1}
	return {"mode": "skirmish", "difficulty": 1, "rank_points": 0, "map": "maps/map mp evendim/map mp evendim.map", "map_crc": 0, "map_size": 0, "map_mask": 0,
		"seed": 4242, "starting_cash": 2000, "slots": slots}


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		print("FAIL: mount")
		return 1
	_world = ClassDB.instantiate("GameWorld")
	root.add_child(_world)
	if not _world.setup(fs).ok:
		print("FAIL: setup")
		return 1
	var prep: Dictionary = _world.prepare_new_game(_message())
	var started: Dictionary = _world.start_new_game({}) if prep.ok else {"ok": false}
	_check(started.ok, "the skirmish starts")
	if not started.ok:
		return 1
	_check(_lua("bfme.console.reload()").contains("Reloaded res://dev/05_skirmish_ai.lua"), "the Lua AI script loads")
	var ai := int(_lua("return bfme.ai.list()[1].id"))
	_check(_lua("return bfme.players[%d].aiEnabled" % ai) == "true", "the computer starts with the native AI")
	_frames(1)
	_check(_lua("return luaai.config.autoEnable") == "false", "Lua replacement is disabled by default")
	_check(_lua("return bfme.players[%d].aiEnabled" % ai) == "true", "the native AI stays enabled by default")
	# Opt in to automatic takeover for the rest of this Lua AI exercise.
	_lua("luaai.config.autoEnable = true")
	# one think now: the update callback runs every thinkMs of wall-clock time, which two quick frames need not reach
	_lua("luaai.thinkAll()")
	_frames(1)
	_frames(1)
	_check(_lua("return luaai.players[%d] ~= nil" % ai) == "true", "the Lua AI took the computer player at match start (autoEnable)")
	_check(_lua("local s=bfme.players[%d]:getAIStatus(); return s.scriptControlled, s.enabled" % ai) == "true\tfalse", "the native AI no longer decides")

	# a unit order for the human player
	var uid := _lua("local p=bfme.players.localPlayer or bfme.players[1]; local u; for _,x in ipairs(p:units()) do if not x.isBuilder then u=x end end; u=u or p:units()[1]; start={x=u.x,y=u.y}; moved=u:moveTo({x=u.x+250,y=u.y}); return u.id")
	_frames(60)
	_check(_lua("return bfme.mutations.getStatus(moved)") == "applied", "the move order was issued")
	var moved := float(_lua("local u=bfme.unitInstances[%s]; return math.abs(u.x-start.x)+math.abs(u.y-start.y)" % uid))
	_check(moved > 100.0, "the ordered unit moved (%.0f units)" % moved)

	# the Lua AI plays five minutes of game time, thinking every second of it
	var before := _lua("local p=bfme.players[%d]; return #p:buildings()..' '..#p:units()" % ai).split(" ")
	for second in 300:
		_frames(5)
		_lua("luaai.thinkAll()")
	var after := _lua("local p=bfme.players[%d]; return #p:buildings()..' '..#p:units()" % ai).split(" ")
	print("  Lua AI: buildings %s -> %s, units %s -> %s; %s" % [before[0], after[0], before[1], after[1], _lua("return luaai.status()")])
	# the layout: where the Lua AI's buildings stand around its fortress (degrees from the front, distance)
	print(_lua("""local p=bfme.players[%d]; local st=luaai.players[p.id]; local f=bfme.buildingInstances[st.fortress]; local out={}
for _,b in ipairs(p:buildings()) do if b.isEconomy or b.isFactory then
 local dx,dy=b.x-f.x,b.y-f.y; local ahead=dx*math.cos(st.front)+dy*math.sin(st.front); local side=-dx*math.sin(st.front)+dy*math.cos(st.front)
 out[#out+1]=string.format('  %%-26s %%-8s ahead %%6.0f  side %%6.0f', b.typeId, b.isEconomy and 'economy' or 'factory', ahead, side) end end
return 'fortress radius '..math.floor(f.radius)..'\\n'..table.concat(out,'\\n')""" % ai))
	# production in front of the fortress, resources behind and on the flanks, all close by (the fortress's own parts excluded)
	_check(_lua("""local p=bfme.players[%d]; local st=luaai.players[p.id]; local f=bfme.buildingInstances[st.fortress]; local bad={}
for _,b in ipairs(p:buildings()) do local d=bfme.map.distance(b,f); if (b.isEconomy or b.isFactory) and d>f.radius then
 local a=math.abs((math.deg(math.atan(b.y-f.y,b.x-f.x)-st.front)+540)%%360-180)
 if (b.isFactory and (d>1500 or a>100)) then bad[#bad+1]=b.typeId end end end
return #bad""" % ai) == "0", "production buildings stand in front of the fortress, within 1500")
	# resource buildings: on the map's markers (FarmTemplate), else spaced apart
	print(_lua("""local p=bfme.players[%d]; local sites=bfme.map.getFarmSites(); local out={'farm markers: '..#sites}
for _,b in ipairs(p:buildings()) do if b.isEconomy then local best; for _,m in ipairs(sites) do local d=bfme.map.distance(b,m); if not best or d<best then best=d end end
 out[#out+1]=string.format('  %%-20s nearest marker %%5.0f', b.typeId, best or -1) end end
return table.concat(out,'\\n')""" % ai))
	_check(_lua("""local p=bfme.players[%d]; local sites=bfme.map.getFarmSites(); local eco={}; local bad=0
for _,b in ipairs(p:buildings()) do if b.isEconomy then eco[#eco+1]=b end end
for i,b in ipairs(eco) do local onMarker=false; for _,m in ipairs(sites) do if bfme.map.distance(b,m)<=luaai.config.farmSiteSnap+10 then onMarker=true end end
 if not onMarker then for j,o in ipairs(eco) do if j~=i and bfme.map.distance(b,o) < luaai.config.economySpacing*0.8 then bad=bad+1 end end end end
return bad""" % ai) == "0", "resource buildings stand on the map's markers or apart from each other")
	# valid placements: the engine started the constructions, no two structures overlap (footprint circles), no foundation is left unbuilt
	print("  constructions: ", _lua("return luaai.stats.placed..' placed, '..luaai.stats.refused..' refused '..table.concat(luaai.stats.lastRefusals or {}, '; ')"))
	_check(int(_lua("return luaai.stats.placed")) >= 3, "the Lua AI's constructions were started by the engine")
	# an occupied spot is refused with its reason; the search from it finds a spot the same check accepts
	var occupied := _lua("""local p=bfme.players[%d]; local f=bfme.buildingInstances[luaai.players[p.id].fortress]; local builder
for _,u in ipairs(p:units()) do if u.isBuilder then builder=u end end
local name; for _,s in ipairs(builder:getBuildableStructures()) do if s.isFactory then name=s.building end end
local ok,why=p:canBuildAt(name, f, builder)
local site=p:findBuildSite(name, f, 900, builder, {clearance=50})
local ok2=site and p:canBuildAt(name, site, builder)
return tostring(ok)..'|'..why..'|'..tostring(ok2)""" % ai).split("|")
	_check(occupied[0] == "false" and occupied[2] == "true", "the fortress's spot is refused (%s) and the search finds an accepted one" % occupied[1])
	_check(_lua("""local p=bfme.players[%d]; local st=luaai.players[p.id]; local f=bfme.buildingInstances[st.fortress]; local list={}
for _,b in ipairs(p:buildings()) do if (b.isEconomy or b.isFactory) and bfme.map.distance(b,f)>f.radius then list[#list+1]=b end end
local overlaps=0
for i=1,#list do for j=i+1,#list do if bfme.map.distance(list[i],list[j]) < (list[i].radius+list[j].radius)*0.95 then overlaps=overlaps+1 end end end
return overlaps""" % ai) == "0", "no two of the Lua AI's structures overlap")
	_check(int(_lua("local n=0; for _,b in ipairs(bfme.players[%d]:buildings()) do if b.underConstruction and (b.constructionPercent or 0)<=0 then n=n+1 end end; return n" % ai)) <= 1, "no foundation is left unbuilt")
	# the build order: unit production buildings alongside the resource buildings, not mostly resources
	var mix := _lua("""local p=bfme.players[%d]; local f=bfme.buildingInstances[luaai.players[p.id].fortress]; local e,pr=0,0
for _,b in ipairs(p:buildings()) do if bfme.map.distance(b,f)>f.radius then if b.isEconomy then e=e+1 elseif b.isFactory then pr=pr+1 end end end
return e..' '..pr""" % ai).split(" ")
	print("  resource buildings %s, production buildings %s" % [mix[0], mix[1]])
	_check(int(mix[1]) >= 3 and int(mix[0]) <= int(mix[1]) + 2, "at least 3 production buildings and at most 2 more resource buildings than production")
	_check(int(after[0]) > int(before[0]), "the Lua AI built structures")
	_check(int(after[1]) > int(before[1]), "the Lua AI trained units")
	_check(not _lua("return luaai.status()").contains("last error"), "the Lua AI ran without errors")
	_check(_lua("return bfme.players[%d]:getAIStatus().scriptControlled" % ai) == "true", "the native AI stayed off")
	# handed back
	_lua("luaai.disableAll()")
	_frames(1)
	_check(_lua("return bfme.players[%d].aiEnabled" % ai) == "true", "the native AI decides again once handed back")
	return 0 if _failures == 0 else 1
