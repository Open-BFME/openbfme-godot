extends SceneTree
func _initialize() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	for i in 2:
		var began := Time.get_ticks_msec()
		var mounted = fs.mount_retail()
		assert(mounted.ok, str(mounted))
		print("MOUNT CACHE RUN ", i, " ms=", Time.get_ticks_msec()-began, " hashed=", mounted.archives_hashed, " cached=", mounted.md5_cache_hits)
		if i == 1: # The two Data1 archives are always verified entry-by-entry.
			assert(mounted.md5_cache_hits > 0 and mounted.archives_hashed == 2)
	print("RETAIL MOUNT CACHE PASS")
	quit()
