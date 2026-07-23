class_name AptRuntimeHost
extends RefCounted
## Tier-3 APT VM host: binds the AptVm host-call surface to a deterministic
## widget/display-state model compatible with the retail HUD APT runtime.
##
## Measured contract (tier-3 evidence, implement-what-the-corpus-uses):
## - MainMenu.apt fixture scripts (apt_vm_fixtures.json, the three pinned
##   entry offsets) make exactly these host calls:
##     get_variable: named child clips (Skirmish, WarOfTheRing, BonusCampaign,
##       Expansion1Campaign, LoadGame, Replay, LocalNetwork, Online, Settings,
##       AdvancedSettings, Credits, OpenButton) plus _root and _parent;
##     set_member: buttonName, closeParent (clip variable writes);
##     get_member: named child clips (SoloPlayNav, MultiPlayNav, OptionsNav);
##     call_method: gotoAndPlay(label), gotoAndStop(label),
##       GetExtern(key, default) on _root.
## - The retail HUD oracles (docs/RETAIL_HUD_*: LIBINGAMEUI_ACTION_ORACLE,
##   PALANTIR_COMMAND_ACTIONS, SIDE_COMMAND_ACTIONS, HOST_BRIDGE, plus the
##   typed programs pinned in retail_hud_apt_runtime.gd) additionally prove:
##     Flash property indices 0.._y..13 (7 = _visible, 13 = _name) via
##       GetProperty/SetProperty; named _x/_y/_width/_height copies;
##     playback: stop/play/gotoAndPlay/goto label+frame;
##     FSCommand dispatch (FSCommand:PlaySound with a retail audio event id);
##     toString / GetFullName-style clip identity strings.
## Everything outside that evidence fails closed: the call is recorded in
## `recorded` (with its arguments), the VM receives undefined / no-op, and
## execution continues cleanly - never a crash, never a half-applied write.
##
## Determinism: no wall clock, no engine singletons, no OS randomness.
## random_int derives from a caller-injected seed (31-bit LCG). Widget
## handles are stable path strings, so every value that reaches the VM stack
## is deterministic and JSON-serializable.

const PATH_SEPARATOR := "."

## Flash frame-property indices proven by the corpus/oracles.
const PROPERTY_INDEX_NAMES := {
	0: "_x", 1: "_y", 2: "_xscale", 3: "_yscale", 4: "_currentframe",
	5: "_totalframes", 6: "_alpha", 7: "_visible", 8: "_width", 9: "_height",
	13: "_name",
}
## Indices writable through SetProperty (read-only identity/frame indices
## are recorded instead).
const WRITABLE_PROPERTY_INDICES := {
	0: true, 1: true, 2: true, 3: true, 6: true, 7: true, 8: true, 9: true,
}
const NUMERIC_MEMBER_PROPERTIES := {
	"_x": true, "_y": true, "_xscale": true, "_yscale": true,
	"_alpha": true, "_width": true, "_height": true,
}

## Fail-closed record of every host call outside the measured contract.
var recorded: Array = []
## Ordered mutation/event log (state writes, playback ops, dispatches).
var events: Array = []
var fs_log: Array = []
var url_log: Array = []
var trace_log: Array = []

var _widgets: Dictionary = {}
var _root_path := ""
var _scope_path := ""
var _globals: Dictionary = {}
var _extern: Dictionary = {}
var _fs_registry: Dictionary = {}
var _function_registry: Dictionary = {}
var _rng_state := 0


func _init(seed_value: int = 0, root_name: String = "_root") -> void:
	_rng_state = (seed_value if seed_value > 0 else -seed_value) & 0x7FFFFFFF
	_root_path = root_name
	_widgets[_root_path] = _new_widget(root_name, "")
	_scope_path = _root_path


# --- Deterministic construction API -------------------------------------------

func _new_widget(name: String, parent_path: String) -> Dictionary:
	return {
		"name": name,
		"parent": parent_path,
		"children": {},
		"frame": 0,
		"total_frames": 1,
		"playing": false,
		"label": "",
		"labels": {},
		"properties": {
			"_x": 0.0, "_y": 0.0, "_xscale": 100.0, "_yscale": 100.0,
			"_alpha": 100.0, "_visible": true, "_width": 0.0, "_height": 0.0,
		},
		"variables": {},
	}


## Add a clip under parent_path. config may carry labels (String -> int),
## total_frames, frame, playing, and initial property values. Returns the
## new widget's path, or "" on an invalid parent/duplicate name.
func add_clip(parent_path: String, name: String, config: Dictionary = {}) -> String:
	if name == "" or not _widgets.has(parent_path):
		return ""
	var parent: Dictionary = _widgets[parent_path]
	if (parent["children"] as Dictionary).has(name):
		return ""
	var path := parent_path + PATH_SEPARATOR + name
	var widget := _new_widget(name, parent_path)
	var labels_value: Variant = config.get("labels", {})
	if typeof(labels_value) == TYPE_DICTIONARY:
		var labels := {}
		for key in labels_value:
			labels[String(key)] = int(labels_value[key])
		widget["labels"] = labels
	widget["total_frames"] = maxi(1, int(config.get("total_frames", 1)))
	widget["frame"] = int(config.get("frame", 0))
	widget["playing"] = bool(config.get("playing", false))
	for property_name in ["_x", "_y", "_alpha", "_width", "_height"]:
		if config.has(property_name):
			(widget["properties"] as Dictionary)[property_name] = float(config[property_name])
	if config.has("_visible"):
		(widget["properties"] as Dictionary)["_visible"] = bool(config["_visible"])
	_widgets[path] = widget
	(parent["children"] as Dictionary)[name] = path
	return path


func remove_clip(path: String) -> bool:
	if path == _root_path or not _widgets.has(path):
		return false
	var widget: Dictionary = _widgets[path]
	for child_name in (widget["children"] as Dictionary).keys():
		remove_clip((widget["children"] as Dictionary)[child_name])
	var parent_path := String(widget["parent"])
	if _widgets.has(parent_path):
		((_widgets[parent_path] as Dictionary)["children"] as Dictionary).erase(String(widget["name"]))
	_widgets.erase(path)
	if _scope_path == path:
		_scope_path = _root_path
	return true


## Bind the executing script's clip scope (the "this" clip).
func set_scope(path: String) -> bool:
	if not _widgets.has(path):
		return false
	_scope_path = path
	return true


func scope_path() -> String:
	return _scope_path


func root_path() -> String:
	return _root_path


func has_widget(path: String) -> bool:
	return _widgets.has(path)


func widget_state(path: String) -> Dictionary:
	if not _widgets.has(path):
		return {}
	return (_widgets[path] as Dictionary).duplicate(true)


## FSCommand callback registry: cb.call(command, argument).
func register_fs_command(command: String, callback: Callable) -> void:
	_fs_registry[command] = callback


## Named host-function registry (CallFunction): cb.call(args) -> Variant.
func register_function(function_name: String, callback: Callable) -> void:
	_function_registry[function_name] = callback


## GetExtern registry (retail engine-supplied extern values).
func set_extern_value(key: String, value: Variant) -> void:
	_extern[key] = value


func set_global_variable(var_name: String, value: Variant) -> void:
	_globals[var_name] = value


## Playback-only slice of the event log (for timeline-state adapters).
func playback_events() -> Array:
	var out: Array = []
	for event in events:
		if String((event as Dictionary).get("family", "")) == "playback":
			out.append(event)
	return out


## Canonical deterministic state snapshot (JSON-able, sorted at digest time).
func state() -> Dictionary:
	var widgets := {}
	for path in _widgets:
		widgets[path] = (_widgets[path] as Dictionary).duplicate(true)
	return {
		"widgets": widgets,
		"scope": _scope_path,
		"globals": _globals.duplicate(true),
		"fs": fs_log.duplicate(true),
		"urls": url_log.duplicate(true),
		"traces": trace_log.duplicate(true),
		"recorded": recorded.duplicate(true),
	}


func state_digest() -> String:
	return JSON.stringify(state(), "", true).sha256_text().substr(0, 16)


# --- VM host surface (duck-typed to AptVm.RecordingHost) ----------------------

func get_variable(var_name: String) -> Variant:
	if var_name == "_root":
		return _root_path
	if var_name == "_parent":
		var parent := String((_widgets[_scope_path] as Dictionary)["parent"])
		if parent != "":
			return parent
		return _record("get_variable", var_name, [])
	var scope: Dictionary = _widgets[_scope_path]
	if (scope["children"] as Dictionary).has(var_name):
		return (scope["children"] as Dictionary)[var_name]
	if (scope["variables"] as Dictionary).has(var_name):
		return (scope["variables"] as Dictionary)[var_name]
	if _globals.has(var_name):
		return _globals[var_name]
	return _record("get_variable", var_name, [])


func get_member(target: String, member_name: String) -> Variant:
	var path := _resolve_widget(target)
	if path == "":
		return _record("get_member", target + "." + member_name, [])
	var widget: Dictionary = _widgets[path]
	if (widget["children"] as Dictionary).has(member_name):
		return (widget["children"] as Dictionary)[member_name]
	match member_name:
		"_name":
			return String(widget["name"])
		"_currentframe":
			return int(widget["frame"])
		"_totalframes":
			return int(widget["total_frames"])
		"_visible":
			return bool((widget["properties"] as Dictionary)["_visible"])
	if NUMERIC_MEMBER_PROPERTIES.has(member_name):
		return float((widget["properties"] as Dictionary)[member_name])
	if (widget["variables"] as Dictionary).has(member_name):
		return (widget["variables"] as Dictionary)[member_name]
	return _record("get_member", path + "." + member_name, [])


func set_member(target: String, member_name: String, value: Variant) -> void:
	var path := _resolve_widget(target)
	if path == "":
		_record("set_member", target + "." + member_name, [value])
		return
	var widget: Dictionary = _widgets[path]
	if member_name == "_visible":
		(widget["properties"] as Dictionary)["_visible"] = _to_bool(value)
		_event("property", path, {"property": "_visible", "value": _to_bool(value)})
		return
	if NUMERIC_MEMBER_PROPERTIES.has(member_name):
		var number := _to_float(value)
		(widget["properties"] as Dictionary)[member_name] = number
		_event("property", path, {"property": member_name, "value": number})
		return
	if member_name in ["_name", "_currentframe", "_totalframes"]:
		_record("set_member", path + "." + member_name, [value])
		return
	if not _jsonable_value(value):
		_record("set_member", path + "." + member_name, [value])
		return
	(widget["variables"] as Dictionary)[member_name] = value
	_event("variable", path, {"name": member_name, "value": value})


func delete_member(target: String, member_name: String) -> void:
	var path := _resolve_widget(target)
	if path != "" and ((_widgets[path] as Dictionary)["variables"] as Dictionary).has(member_name):
		((_widgets[path] as Dictionary)["variables"] as Dictionary).erase(member_name)
		_event("delete", path, {"name": member_name})
		return
	_record("delete_member", target + "." + member_name, [])


func call_method(target: String, method_name: String, args: Array) -> Variant:
	var path := _resolve_widget(target)
	if path == "":
		return _record("call_method", target + "." + method_name, args)
	match method_name:
		"gotoAndPlay":
			if args.size() == 1:
				_goto(path, args[0], true)
				return null
		"gotoAndStop":
			if args.size() == 1:
				_goto(path, args[0], false)
				return null
		"play":
			(_widgets[path] as Dictionary)["playing"] = true
			_event_playback("play", path, {})
			return null
		"stop":
			(_widgets[path] as Dictionary)["playing"] = false
			_event_playback("stop", path, {})
			return null
		"toString":
			return path
		"GetExtern":
			if args.size() >= 1 and _extern.has(String(args[0])):
				_event("extern", path, {"key": String(args[0])})
				return _extern[String(args[0])]
	return _record("call_method", path + "." + method_name, args)


func call_function(fn_name: String, args: Array) -> Variant:
	if _function_registry.has(fn_name):
		_event("function", _scope_path, {"name": fn_name, "args": args.duplicate(true)})
		return (_function_registry[fn_name] as Callable).call(args)
	return _record("call_function", fn_name, args)


func construct_object(type_name: String, args: Array) -> Variant:
	# No host classes are proven by the corpus fixtures; the VM substitutes
	# a plain script object for the null return.
	return _record("construct_object", type_name, args)


func enumerate(target: String) -> Array:
	var path := _resolve_widget(target)
	if path == "":
		_record("enumerate", target, [])
		return []
	return ((_widgets[path] as Dictionary)["children"] as Dictionary).keys()


func playback(op_name: String, args: Array) -> void:
	var widget: Dictionary = _widgets[_scope_path]
	match op_name:
		"stop":
			widget["playing"] = false
			_event_playback("stop", _scope_path, {})
		"play":
			widget["playing"] = true
			_event_playback("play", _scope_path, {})
		"next_frame":
			widget["frame"] = mini(int(widget["frame"]) + 1, int(widget["total_frames"]) - 1)
			widget["playing"] = false
			_event_playback("next_frame", _scope_path, {"frame": int(widget["frame"])})
		"goto_frame":
			if args.size() == 1:
				widget["frame"] = int(args[0])
				_event_playback("goto_frame", _scope_path, {"frame": int(args[0])})
			else:
				_record("playback", op_name, args)
		"goto_label":
			if args.size() == 1:
				_apply_label(_scope_path, String(args[0]))
				_event_playback("goto_label", _scope_path, {"label": String(args[0])})
			else:
				_record("playback", op_name, args)
		"goto_frame2":
			if args.size() == 2:
				var play_after := int(args[1]) == 1
				if args[0] is int or args[0] is float:
					widget["frame"] = int(args[0])
				else:
					_apply_label(_scope_path, String(args[0]))
				widget["playing"] = play_after
				_event_playback("goto_frame2", _scope_path,
					{"target": args[0], "play": play_after})
			else:
				_record("playback", op_name, args)
		"clone_sprite":
			if args.size() == 3:
				_clone_sprite(String(args[0]), String(args[1]), int(args[2]))
			else:
				_record("playback", op_name, args)
		"remove_sprite":
			var target_path := _resolve_widget(String(args[0])) if args.size() == 1 else ""
			if target_path != "" and remove_clip(target_path):
				_event_playback("remove_sprite", target_path, {})
			else:
				_record("playback", op_name, args)
		_:
			_record("playback", op_name, args)


func get_property(target: String, property_index: int) -> Variant:
	var path := _resolve_widget(target)
	if path == "" or not PROPERTY_INDEX_NAMES.has(property_index):
		return _record("get_property", target + "#%d" % property_index, [])
	var widget: Dictionary = _widgets[path]
	match property_index:
		4:
			return int(widget["frame"])
		5:
			return int(widget["total_frames"])
		7:
			return bool((widget["properties"] as Dictionary)["_visible"])
		13:
			return String(widget["name"])
	return float((widget["properties"] as Dictionary)[String(PROPERTY_INDEX_NAMES[property_index])])


func set_property(target: String, property_index: int, value: Variant) -> void:
	var path := _resolve_widget(target)
	if path == "" or not WRITABLE_PROPERTY_INDICES.has(property_index):
		_record("set_property", target + "#%d" % property_index, [value])
		return
	var property_name := String(PROPERTY_INDEX_NAMES[property_index])
	var widget: Dictionary = _widgets[path]
	if property_index == 7:
		(widget["properties"] as Dictionary)["_visible"] = _to_bool(value)
		_event("property", path, {"property": "_visible", "value": _to_bool(value)})
		return
	var number := _to_float(value)
	(widget["properties"] as Dictionary)[property_name] = number
	_event("property", path, {"property": property_name, "value": number})


func fs_command(command: String, arg: String) -> void:
	fs_log.append({"command": command, "argument": arg})
	_event("fs_command", _scope_path, {"command": command, "argument": arg})
	if _fs_registry.has(command):
		(_fs_registry[command] as Callable).call(command, arg)


func get_url(url: String, target: String) -> void:
	# URL navigation stays recorded-only: no navigation surface is proven.
	url_log.append({"url": url, "target": target})
	_record("get_url", url, [target])


func trace_message(message: String) -> void:
	trace_log.append(message)


## Deterministic 31-bit LCG seeded by the caller; the host core never touches
## engine or OS randomness.
func random_int(max_exclusive: int) -> int:
	_rng_state = (_rng_state * 1103515245 + 12345) & 0x7FFFFFFF
	if max_exclusive <= 0:
		return 0
	return _rng_state % max_exclusive


# --- Internals -----------------------------------------------------------------

## Resolve a VM target description to a widget path: exact path, "this"/"",
## _root/_parent, or a child of the current scope by name.
func _resolve_widget(target: String) -> String:
	if _widgets.has(target):
		return target
	if target == "" or target == "this":
		return _scope_path
	if target == "_root":
		return _root_path
	if target == "_parent":
		return String((_widgets[_scope_path] as Dictionary)["parent"])
	var scope_children: Dictionary = (_widgets[_scope_path] as Dictionary)["children"]
	if scope_children.has(target):
		return String(scope_children[target])
	return ""


func _goto(path: String, frame_or_label: Variant, playing: bool) -> void:
	var widget: Dictionary = _widgets[path]
	if frame_or_label is int or frame_or_label is float:
		widget["frame"] = int(frame_or_label)
	else:
		_apply_label(path, String(frame_or_label))
	widget["playing"] = playing
	_event_playback("gotoAndPlay" if playing else "gotoAndStop", path,
		{"target": frame_or_label})


func _apply_label(path: String, label: String) -> void:
	var widget: Dictionary = _widgets[path]
	widget["label"] = label
	if (widget["labels"] as Dictionary).has(label):
		widget["frame"] = int((widget["labels"] as Dictionary)[label])


func _clone_sprite(source: String, target: String, depth: int) -> void:
	var source_path := _resolve_widget(source)
	if source_path == "" or source_path == _root_path:
		_record("playback", "clone_sprite", [source, target, depth])
		return
	var parent_path := String((_widgets[source_path] as Dictionary)["parent"])
	var clone_path := add_clip(parent_path, target)
	if clone_path == "":
		_record("playback", "clone_sprite", [source, target, depth])
		return
	var source_widget: Dictionary = _widgets[source_path]
	var clone: Dictionary = _widgets[clone_path]
	clone["labels"] = (source_widget["labels"] as Dictionary).duplicate(true)
	clone["total_frames"] = int(source_widget["total_frames"])
	clone["properties"] = (source_widget["properties"] as Dictionary).duplicate(true)
	clone["variables"] = (source_widget["variables"] as Dictionary).duplicate(true)
	_event_playback("clone_sprite", clone_path, {"source": source_path, "depth": depth})


func _event(family: String, path: String, payload: Dictionary) -> void:
	var event := {"family": family, "path": path}
	event.merge(payload)
	events.append(event)


func _event_playback(op: String, path: String, payload: Dictionary) -> void:
	var event := {"family": "playback", "op": op, "path": path}
	event.merge(payload)
	events.append(event)


## Fail-closed: record the out-of-contract call and hand undefined back.
func _record(kind: String, call_name: String, args: Array) -> Variant:
	var safe_args: Array = []
	for arg in args:
		safe_args.append(arg if _jsonable_value(arg) else str(arg))
	recorded.append({"kind": kind, "name": call_name, "args": safe_args})
	return null


func _to_float(value: Variant) -> float:
	if value is bool:
		return 1.0 if value else 0.0
	if value is int or value is float:
		return float(value)
	if value is String and (value as String).is_valid_float():
		return (value as String).to_float()
	return 0.0


func _to_bool(value: Variant) -> bool:
	if value is bool:
		return value
	if value is int or value is float:
		return float(value) != 0.0
	if value is String:
		return value != "" and value != "0" and value != "false"
	return false


func _jsonable_value(value: Variant) -> bool:
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return true
		TYPE_ARRAY:
			for element in value as Array:
				if not _jsonable_value(element):
					return false
			return true
		TYPE_DICTIONARY:
			for key in value as Dictionary:
				if not (key is String) or not _jsonable_value((value as Dictionary)[key]):
					return false
			return true
	return false
