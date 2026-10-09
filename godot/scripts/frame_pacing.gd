## Lane SMOOTH-1: the client's frame pacing. The simulation runs at 5 logic frames a second whatever the render rate (GameWorld interpolates the drawables, the
## HUD the camera); the render loop is NOT capped to retail's 30 client frames a second: by default it runs at the display's refresh rate (vsync, so 144 Hz on
## a 144 Hz display, frames delivered evenly), with no frame cap and no coupling to Godot's physics tick (nothing in the game uses _physics_process).
## User settings (command line, after `--`):
##   --vsync=on|off|mailbox|adaptive   on (default): one frame per refresh; mailbox: uncapped rendering, the newest frame shown at each refresh (no tearing);
##                                     adaptive: vsync that tears instead of halving the rate on a late frame; off: uncapped, may tear
##   --max-fps=<n>                     a frame cap (0 = none, the default)
extends RefCounted


static func apply_from_args(args: PackedStringArray) -> String:
	var mode := DisplayServer.VSYNC_ENABLED
	var max_fps := 0
	for arg in args:
		if arg.begins_with("--vsync="):
			match arg.substr(8):
				"off":
					mode = DisplayServer.VSYNC_DISABLED
				"mailbox":
					mode = DisplayServer.VSYNC_MAILBOX
				"adaptive":
					mode = DisplayServer.VSYNC_ADAPTIVE
				_:
					mode = DisplayServer.VSYNC_ENABLED
		elif arg.begins_with("--max-fps="):
			max_fps = maxi(int(arg.substr(10)), 0)
	DisplayServer.window_set_vsync_mode(mode)
	Engine.max_fps = max_fps
	var names := {DisplayServer.VSYNC_DISABLED: "off", DisplayServer.VSYNC_ENABLED: "on", DisplayServer.VSYNC_ADAPTIVE: "adaptive", DisplayServer.VSYNC_MAILBOX: "mailbox"}
	var refresh := DisplayServer.screen_get_refresh_rate()
	return "vsync %s (driver: %s), max_fps %d, display refresh %.1f Hz" % [names.get(mode, "?"), names.get(DisplayServer.window_get_vsync_mode(), "?"), max_fps, refresh]
