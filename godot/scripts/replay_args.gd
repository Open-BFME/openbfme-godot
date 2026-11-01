extends RefCounted


static func parse(args: PackedStringArray) -> Dictionary:
	var path := ""
	var option := ""
	var seen := false
	var i := 0
	while i < args.size():
		var a := args[i]
		if a in ["--replay", "--load"] or a.begins_with("--replay=") or a.begins_with("--load="):
			if seen:
				return {"ok": false, "error": "Specify only one --replay or --load file."}
			seen = true
			option = "--load" if a.begins_with("--load") else "--replay"
			if a == option:
				i += 1
				if i == args.size() or args[i].begins_with("--"):
					return {"ok": false, "error": option + " requires a file path."}
				path = args[i]
			else:
				path = a.substr(option.length() + 1)
			if path.is_empty():
				return {"ok": false, "error": option + " requires a file path."}
		i += 1
	if seen:
		for a in args:
			if a in ["--auto", "--qa", "--qa-idle"] or a.begins_with("--net-") or a.begins_with("--lan") or a.begins_with("--campaign") or a.begins_with("--online-") or (option == "--load" and a.begins_with("--replay-")):
				return {"ok": false, "error": option + " cannot be combined with other game startup options."}
	return {"ok": true, "path": path if option == "--replay" else "", "load_path": path if option == "--load" else "", "error": ""}
