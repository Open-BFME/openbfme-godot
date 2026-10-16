## Original BFME2Lua/Xbox controller strips, kept in an ignored local asset folder.
extends RefCounted
static var _cache: Dictionary = {}
static func texture(name: String) -> Texture2D:
	if not _cache.has(name):
		var path := "res://controller_art/%s.tga" % name
		if ResourceLoader.exists(path):
			_cache[name] = load(path)
		else:
			push_error("Missing original controller artwork: " + path)
			_cache[name] = null
	return _cache[name]
static func frame(name: String, fps: float = 30.0) -> Texture2D:
	var strip := texture(name)
	if strip == null:
		return null
	var side := strip.get_height()
	var count := maxi(1, strip.get_width() / side)
	var atlas := AtlasTexture.new()
	atlas.atlas = strip
	atlas.region = Rect2((int(Time.get_ticks_msec() * fps / 1000.0) % count) * side, 0, side, side)
	return atlas
