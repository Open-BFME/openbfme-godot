class_name RetailHudWndRuntime
extends RefCounted
## Bounded typed companion for the exact active BFME2 ControlBar.wnd.
## This is deliberately not a renderer and is not bound into the live HUD.

const CONTRACT_SCHEMA := "openbfme.private-hud-wnd-callback-oracle"
const COMPANION_SCHEMA := "openbfme.retail-hud-wnd-companion"
const SOURCE_SHA256 := "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6"
const ORACLE_AGGREGATES := {
	"callback": "ad97b6c02ed6a46eec745adda4434264b84dcc969b7c46115f6a8a6458d33662",
	"message": "238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c",
	"draw": "748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484",
}
const IMPLEMENTED := [
	"ControlBarInput",
	"ControlBarSystem",
	"GameWinBlockInput",
	"LeftHUDInput",
	"PassSelectedButtonsToParentSystem",
	"W3DCommandBarBackgroundDraw",
	"W3DCommandBarForegroundDraw",
	"W3DCommandBarGenExpDraw",
	"W3DCommandBarGridDraw",
	"W3DCommandBarTopDraw",
	"W3DGadgetPushButtonImageDraw",
	"W3DLeftHUDDraw",
	"W3DNoDraw",
	"W3DPowerDraw",
	"W3DRightHUDDraw",
]
const DRAW_CONTRACT_SCHEMA := "openbfme.private-hud-wnd-draw-semantics"
const MESSAGE_CONTRACT_SCHEMA := "openbfme.private-hud-wnd-message-semantics"
const MEN_V_MEN_REQUIRED_CALLBACKS := [
	"ControlBarInput",
	"GameWinBlockInput",
	"PassSelectedButtonsToParentSystem",
	"LeftHUDInput",
	"ControlBarSystem",
]
const OUTSIDE_SLICE_CALLBACKS := {
	"BeaconWindowInput": "event-dormant",
	"ControlBarObserverSystem": "outside declared player-v-player slice",
}
const UNRESOLVED_BUILTIN_CALLBACKS := [
	"GameWinDefaultInput",
	"GameWinDefaultSystem",
	"GameWinDefaultTooltip",
	"W3DGameWinDefaultDraw",
]
const DRAW_SERVICE_GATES := [
	"background-blend-and-parameter-aliases",
	"foreground-image-indirection",
	"experience-progress-and-blend-aliases",
	"grid-cell-material-parameters",
	"top-tail-draw-parameters",
	"radar-object-and-blend-clipping-structures",
	"power-counters-and-image-blend-state-aliases",
]
const MESSAGE_ALIAS_GATES := [
	"retail-service-class-aliases-for-three-globals",
	"object-field-0x14-type-aliases",
	"command-id-0x42f-or-0x430-selected-object-branch",
	"messages-0x11-0x12-0x18-selection-camera-aliases",
	"eight-cached-handle-semantic-aliases",
	"message-0x4031-matched-service-aliases",
	"message-0x400b-rejection-before-selected-button-fallback",
]
const MESSAGE_SPECS := {
	"ControlBarInput": {
		"entryVa": "0x004d43d0", "endVa": "0x004d43d3", "byteLength": 3,
		"sha256": "4bc724f3b1d0caf4fe369c18cba3102e6c4ea057f63fe1587e3973134a7f755e",
		"interface": "handle_control_bar_input(control, message, data1, data2) -> WndHandled",
		"returnContract": "always 0 (unhandled)", "stateCount": 1, "dependencyCount": 0, "unresolvedCount": 0,
	},
	"PassSelectedButtonsToParentSystem": {
		"entryVa": "0x006c0978", "endVa": "0x006c09df", "byteLength": 103,
		"sha256": "84d4c0731f143e6fe8f24c9b905598a4b1488be0887724c53b198a4bab08b4ec",
		"interface": "forward_selected_button(control, message, data1, data2) -> WndHandled",
		"returnContract": "0 for null control, non-allowlisted message, or absent parent; otherwise exact parent callback return",
		"stateCount": 4, "dependencyCount": 2, "unresolvedCount": 0,
	},
	"GameWinBlockInput": {
		"entryVa": "0x0071435a", "endVa": "0x007143b9", "byteLength": 95,
		"sha256": "40ad155d51e0f767340537a6eb7115a8d318b97fdc8890fe40d0f7c0d6c9f35c",
		"interface": "block_control_bar_input(control, message, data1, data2) -> WndHandled",
		"returnContract": "0 only for messages 0x0015 and 0x0018; 1 otherwise",
		"stateCount": 3, "dependencyCount": 3, "unresolvedCount": 1,
	},
	"LeftHUDInput": {
		"entryVa": "0x008020ce", "endVa": "0x00802455", "byteLength": 903,
		"sha256": "c6fdeb00fffbabe8948133fd836baa9a5d3e47a51609c8abfbc693685b8403a4",
		"interface": "handle_radar_input(control, message, data1, data2, radar_state) -> WndHandled",
		"returnContract": "returns 1 for mode-gated input and recognized radar branches; returns 0 only through exact reject branch 0x0080212f",
		"stateCount": 5, "dependencyCount": 7, "unresolvedCount": 3,
	},
	"ControlBarSystem": {
		"entryVa": "0x00802455", "endVa": "0x00802960", "byteLength": 1291,
		"sha256": "d07f981cdf59ebc3867bece45df556c18f19875c1f96c5198ed41eb0e0121595",
		"interface": "handle_control_bar_system(control, message, data1, data2, cached_controls) -> WndHandled",
		"returnContract": "returns 1 after recognized dispatch; returns 0 for the exact gate/unrecognized paths ending at 0x0080276a",
		"stateCount": 6, "dependencyCount": 7, "unresolvedCount": 3,
	},
}
const DRAW_SPECS := {
	"W3DLeftHUDDraw": {
		"entryVa": "0x0049dcee", "endVa": "0x0049dde6", "byteLength": 248,
		"sha256": "678d3ff87adbc874a7c552a53829dc163e0379ed01b4aa796f5b0cf919e528ae",
		"assets": [], "unresolvedCount": 1,
		"order": ["query global 0x00dfedf0 vslot 0x164", "derive rect", "if radar object exists call its vslot 0x104", "else mode-gated call selected service vslot 0x1c"],
	},
	"W3DRightHUDDraw": {
		"entryVa": "0x0049dde6", "endVa": "0x0049de01", "byteLength": 27,
		"sha256": "058e05d1153aa40c9ec26a7afdcf8a27ad8831a5f8b4edf138af4c4626384259",
		"assets": [], "unresolvedCount": 0,
		"order": ["evaluate predicate", "if negative delegate exact default helper 0x0049dc32", "otherwise emit no draw"],
	},
	"W3DCommandBarGridDraw": {
		"entryVa": "0x0049de26", "endVa": "0x0049df9a", "byteLength": 372,
		"sha256": "7b0f33eca0e080b0050f1746d99ed32598b1211651f9a657a84bb1cdc4dea9d2",
		"assets": [], "unresolvedCount": 1,
		"order": ["predicate 0x0070f45f", "fallback 0x0049dc32 if negative", "derive rect", "0x007143ff grid state", "up to four ordered 0x0044d664 cell draws"],
	},
	"W3DCommandBarTopDraw": {
		"entryVa": "0x0049df9a", "endVa": "0x0049dfdd", "byteLength": 67,
		"sha256": "ab200ff2a46e98dc87c7624664fcf485f11da9de73a266586fefdbbd33791f9b",
		"assets": ["ControlBar.wnd:ButtonGeneral"], "unresolvedCount": 1,
		"order": ["resolve ButtonGeneral", "skip missing window", "predicate 0x00713cd9", "tail-dispatch 0x006a7dd0"],
	},
	"W3DPowerDraw": {
		"entryVa": "0x0049e365", "endVa": "0x0049e71c", "byteLength": 951,
		"sha256": "05190717b451101f2da65d001f8bc9e083df23fa3f45cc32e1a439a8e3dec558",
		"assets": ["PowerPointY", "PowerPointG", "PowerBarSlider"], "unresolvedCount": 1,
		"order": ["lazy-resolve PowerPointY, PowerPointG, PowerBarSlider", "query player/power state", "derive rect", "emit ordered point/slider image draws via vslots 0x108, 0xa8, 0xb0"],
	},
	"W3DCommandBarGenExpDraw": {
		"entryVa": "0x0049ed8d", "endVa": "0x0049f0c8", "byteLength": 827,
		"sha256": "d05aeb522557f45e5ba31cae97a599bce98326e0f499a1599777eac3c4fffccf",
		"assets": ["GenExpBarTop1", "GenExpBarBottom1", "GenExpBar1"], "unresolvedCount": 1,
		"order": ["predicate 0x006aa231", "lazy-resolve three images", "derive rect", "clamp progress to 0..100", "draw bar and top/bottom caps through image vslot 0x108"],
	},
	"W3DCommandBarBackgroundDraw": {
		"entryVa": "0x0049fa82", "endVa": "0x0049fb63", "byteLength": 225,
		"sha256": "e72f3f40b8b7772715d33529bb43b5ba70f8093d9ad1649af77c8904017c269f",
		"assets": ["ControlBar.wnd:BackgroundMarker"], "unresolvedCount": 1,
		"order": ["reject missing owner", "lazy-resolve BackgroundMarker", "resolve marker window", "derive rect", "0x0071aeae pre-state", "0x0071fac5 background draw"],
	},
	"W3DCommandBarForegroundDraw": {
		"entryVa": "0x0049fb63", "endVa": "0x0049fc44", "byteLength": 225,
		"sha256": "42a2e8e0d09554941836372d70c1194bb6dfdaa6685b68944ce8f5078dd77354",
		"assets": ["ControlBar.wnd:BackgroundMarker"], "unresolvedCount": 1,
		"order": ["reject missing owner", "lazy-resolve marker", "derive rect", "0x0071ae93 pre-state", "0x0071fa9a foreground draw"],
	},
	"W3DGadgetPushButtonImageDraw": {
		"entryVa": "0x004a6019", "endVa": "0x004a6097", "byteLength": 126,
		"sha256": "54906e711c013dfbf54e391119e05078fa5545140191d7fe954633cbff312762",
		"assets": [], "unresolvedCount": 0,
		"order": ["predicate 0x00727df6 then delegate 0x004a501c", "else reject missing image", "if flag 0x20 derive rect and call 0x004a56ed", "otherwise fallback 0x004a4b7c"],
	},
	"W3DNoDraw": {
		"entryVa": "0x004b3fd0", "endVa": "0x004b3fd1", "byteLength": 1,
		"sha256": "ae3f4619b0413d70d3004b9131c3752153074e45725be13b9a148978895e359e",
		"assets": [], "unresolvedCount": 0,
		"order": [],
	},
}
const FORWARDED_MESSAGES := [0x4006, 0x4007, 0x4008, 0x4009, 0x400B, 0x4031]
const BINDINGS := {
	"BeaconWindowInput": ["input|8|ControlBar.wnd:BeaconWindow|USER"],
	"ControlBarInput": ["input|0|ControlBar.wnd:ControlBarParent|USER"],
	"ControlBarObserverSystem": ["system|19|ControlBar.wnd:ObserverPlayerInfoWindow|USER", "system|32|ControlBar.wnd:ObserverPlayerListWindow|USER"],
	"ControlBarSystem": ["system|0|ControlBar.wnd:ControlBarParent|USER"],
	"GameWinBlockInput": ["input|4|ControlBar.wnd:|USER", "input|5|ControlBar.wnd:|USER", "input|6|ControlBar.wnd:|USER", "input|7|ControlBar.wnd:CenterBackground|USER", "input|55|ControlBar.wnd:RightHUD|USER", "input|66|ControlBar.wnd:WinUnitSelected|USER", "input|68|ControlBar.wnd:UnitUpgrade1|USER", "input|69|ControlBar.wnd:UnitUpgrade2|USER", "input|71|ControlBar.wnd:UnitUpgrade4|USER", "input|72|ControlBar.wnd:UnitUpgrade5|USER", "input|81|ControlBar.wnd:PowerWindow|USER"],
	"GameWinDefaultInput": ["input|56|ControlBar.wnd:ProductionQueueWindow|USER"],
	"GameWinDefaultSystem": ["system|2|ControlBar.wnd:LeftHUD|USER", "system|80|ControlBar.wnd:LeftHUD1Input|USER"],
	"GameWinDefaultTooltip": ["tooltip|56|ControlBar.wnd:ProductionQueueWindow|USER", "tooltip|80|ControlBar.wnd:LeftHUD1Input|USER"],
	"LeftHUDInput": ["input|80|ControlBar.wnd:LeftHUD1Input|USER"],
	"PassSelectedButtonsToParentSystem": ["system|7|ControlBar.wnd:CenterBackground|USER", "system|8|ControlBar.wnd:BeaconWindow|USER", "system|13|ControlBar.wnd:CommandWindow|USER", "system|16|ControlBar.wnd:UnderConstructionWindow|USER", "system|49|ControlBar.wnd:OCLTimerWindow|USER", "system|55|ControlBar.wnd:RightHUD|USER", "system|56|ControlBar.wnd:ProductionQueueWindow|USER"],
	"W3DCommandBarBackgroundDraw": ["draw|3|ControlBar.wnd:BackgroundMarker|USER"],
	"W3DCommandBarForegroundDraw": ["draw|77|ControlBar.wnd:ForegroundMarker|USER"],
	"W3DCommandBarGenExpDraw": ["draw|53|ControlBar.wnd:GeneralsExp|USER"],
	"W3DCommandBarGridDraw": ["draw|66|ControlBar.wnd:WinUnitSelected|USER"],
	"W3DCommandBarTopDraw": ["draw|86|ControlBar.wnd:OnTopDraw|USER"],
	"W3DGadgetPushButtonImageDraw": ["draw|67|ControlBar.wnd:CameoWindow|USER", "draw|68|ControlBar.wnd:UnitUpgrade1|USER", "draw|69|ControlBar.wnd:UnitUpgrade2|USER", "draw|70|ControlBar.wnd:UnitUpgrade3|USER", "draw|71|ControlBar.wnd:UnitUpgrade4|USER", "draw|72|ControlBar.wnd:UnitUpgrade5|USER"],
	"W3DGameWinDefaultDraw": ["draw|0|ControlBar.wnd:ControlBarParent|USER"],
	"W3DLeftHUDDraw": ["draw|2|ControlBar.wnd:LeftHUD|USER"],
	"W3DNoDraw": ["draw|1|ControlBar.wnd:Munkee|USER", "draw|4|ControlBar.wnd:|USER", "draw|5|ControlBar.wnd:|USER", "draw|6|ControlBar.wnd:|USER", "draw|7|ControlBar.wnd:CenterBackground|USER", "draw|8|ControlBar.wnd:BeaconWindow|USER", "draw|13|ControlBar.wnd:CommandWindow|USER", "draw|16|ControlBar.wnd:UnderConstructionWindow|USER", "draw|19|ControlBar.wnd:ObserverPlayerInfoWindow|USER", "draw|32|ControlBar.wnd:ObserverPlayerListWindow|USER", "draw|49|ControlBar.wnd:OCLTimerWindow|USER", "draw|80|ControlBar.wnd:LeftHUD1Input|USER"],
	"W3DPowerDraw": ["draw|81|ControlBar.wnd:PowerWindow|USER"],
	"W3DRightHUDDraw": ["draw|55|ControlBar.wnd:RightHUD|USER", "draw|56|ControlBar.wnd:ProductionQueueWindow|USER"],
}

var configured := false
var draw_configured := false
var message_configured := false
var companion_configured := false
var last_error := ""


func configure_document(document: Dictionary) -> bool:
	configured = false
	last_error = ""
	if String(document.get("schema", "")) != CONTRACT_SCHEMA:
		return _fail("WND callback schema changed")
	var summary_value: Variant = document.get("summary", {})
	var callbacks_value: Variant = document.get("callbacks", [])
	if typeof(summary_value) != TYPE_DICTIONARY or typeof(callbacks_value) != TYPE_ARRAY:
		return _fail("WND callback inventory shape is invalid")
	var summary := summary_value as Dictionary
	if (
		int(summary.get("callbackIdentityCount", -1)) != 21
		or int(summary.get("exactRetailHandlerCount", -1)) != 17
		or int(summary.get("runtimeBuiltInHandlerCount", -1)) != 4
		or bool(summary.get("genericDispatchAllowed", true))
	):
		return _fail("WND callback summary changed")
	var callbacks := callbacks_value as Array
	if callbacks.size() != BINDINGS.size():
		return _fail("WND callback count changed")
	var seen := {}
	for row_value in callbacks:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("WND callback row is invalid")
		var row := row_value as Dictionary
		var name := String(row.get("name", ""))
		if not BINDINGS.has(name) or seen.has(name):
			return _fail("WND callback identity changed")
		var actual := _binding_keys(row.get("controls", []))
		if actual != BINDINGS[name]:
			return _fail("WND callback control binding changed: %s" % name)
		if bool(row.get("genericDispatchAllowed", true)):
			return _fail("WND callback generic dispatch was enabled")
		seen[name] = true
	configured = seen.size() == BINDINGS.size()
	return configured


func configure_draw_document(document: Dictionary) -> bool:
	draw_configured = false
	last_error = ""
	if String(document.get("schema", "")) != DRAW_CONTRACT_SCHEMA:
		return _fail_draw("WND draw schema changed")
	if String(document.get("aggregateSha256", "")) != "748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484":
		return _fail_draw("WND draw aggregate identity changed")
	var summary_value: Variant = document.get("summary", {})
	var callbacks_value: Variant = document.get("callbacks", [])
	if typeof(summary_value) != TYPE_DICTIONARY or typeof(callbacks_value) != TYPE_ARRAY:
		return _fail_draw("WND draw inventory shape is invalid")
	var summary := summary_value as Dictionary
	if (
		int(summary.get("drawCallbackCount", -1)) != 10
		or int(summary.get("exactFullBodyCount", -1)) != 10
		or int(summary.get("provenNoOpCount", -1)) != 1
		or bool(summary.get("callbacksImplemented", true))
		or bool(summary.get("inventedVisualsAllowed", true))
		or bool(summary.get("genericDispatchAllowed", true))
	):
		return _fail_draw("WND draw summary changed")
	var callbacks := callbacks_value as Array
	if callbacks.size() != DRAW_SPECS.size():
		return _fail_draw("WND draw callback count changed")
	var seen := {}
	for row_value in callbacks:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail_draw("WND draw callback row is invalid")
		var row := row_value as Dictionary
		var name := String(row.get("name", ""))
		if not DRAW_SPECS.has(name) or seen.has(name):
			return _fail_draw("WND draw callback identity changed")
		var expected := DRAW_SPECS[name] as Dictionary
		if (
			String(row.get("entryVa", "")) != String(expected.entryVa)
			or String(row.get("endVa", "")) != String(expected.endVa)
			or int(row.get("byteLength", -1)) != int(expected.byteLength)
			or String(row.get("sha256", "")) != String(expected.sha256)
			or row.get("assets", []) != expected.assets
			or row.get("order", []) != expected.order
			or typeof(row.get("inputs", [])) != TYPE_ARRAY
			or String(row.get("state", "")) == ""
			or typeof(row.get("unresolved", [])) != TYPE_ARRAY
			or (row.get("unresolved", []) as Array).size() != int(expected.unresolvedCount)
			or String(row.get("interface", "")) != "draw_%s(control, instance_data, typed_state) -> list[DrawCommand]" % name.trim_prefix("W3D").trim_suffix("Draw").to_lower()
			or bool(row.get("genericDispatchAllowed", true))
			or _draw_binding_keys(row.get("controls", [])) != BINDINGS[name]
		):
			return _fail_draw("WND draw callback contract changed: %s" % name)
		if name == "W3DNoDraw" and String(row.get("proof", "")) != "single byte 0xc3 RET; no reads, writes, calls, draw commands, or return guarantee":
			return _fail_draw("WND no-draw proof changed")
		seen[name] = true
	draw_configured = seen.size() == DRAW_SPECS.size()
	return draw_configured


func configure_message_document(document: Dictionary) -> bool:
	message_configured = false
	last_error = ""
	if String(document.get("schema", "")) != MESSAGE_CONTRACT_SCHEMA:
		return _fail_message("WND message schema changed")
	if String(document.get("aggregateSha256", "")) != "238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c":
		return _fail_message("WND message aggregate identity changed")
	var summary_value: Variant = document.get("summary", {})
	var handlers_value: Variant = document.get("handlers", [])
	if typeof(summary_value) != TYPE_DICTIONARY or typeof(handlers_value) != TYPE_ARRAY:
		return _fail_message("WND message inventory shape is invalid")
	var summary := summary_value as Dictionary
	if (
		int(summary.get("prioritizedHandlerCount", -1)) != 5
		or int(summary.get("fullyExactHandlerCount", -1)) != 3
		or int(summary.get("boundedOpaqueBranchHandlerCount", -1)) != 2
		or bool(summary.get("implemented", true))
		or bool(summary.get("genericDispatchAllowed", true))
		or document.get("retainedNotImplemented", {}) != OUTSIDE_SLICE_CALLBACKS
	):
		return _fail_message("WND message summary or slice boundary changed")
	var handlers := handlers_value as Array
	if handlers.size() != MESSAGE_SPECS.size():
		return _fail_message("WND message handler count changed")
	var seen := {}
	for row_value in handlers:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail_message("WND message handler row is invalid")
		var row := row_value as Dictionary
		var name := String(row.get("name", ""))
		if not MESSAGE_SPECS.has(name) or seen.has(name):
			return _fail_message("WND message handler identity changed")
		var expected := MESSAGE_SPECS[name] as Dictionary
		if (
			String(row.get("entryVa", "")) != String(expected.entryVa)
			or String(row.get("endVa", "")) != String(expected.endVa)
			or int(row.get("byteLength", -1)) != int(expected.byteLength)
			or String(row.get("sha256", "")) != String(expected.sha256)
			or String(row.get("interface", "")) != String(expected.interface)
			or String(row.get("returnContract", "")) != String(expected.returnContract)
			or typeof(row.get("states", [])) != TYPE_ARRAY
			or (row.get("states", []) as Array).size() != int(expected.stateCount)
			or typeof(row.get("dependencies", [])) != TYPE_ARRAY
			or (row.get("dependencies", []) as Array).size() != int(expected.dependencyCount)
			or typeof(row.get("unresolved", [])) != TYPE_ARRAY
			or (row.get("unresolved", []) as Array).size() != int(expected.unresolvedCount)
			or _binding_keys(row.get("controls", [])) != BINDINGS[name]
			or bool(row.get("genericDispatchAllowed", true))
		):
			return _fail_message("WND message handler contract changed: %s" % name)
		seen[name] = true
	message_configured = seen.size() == MESSAGE_SPECS.size()
	return message_configured


func configure_companion_document(document: Dictionary) -> bool:
	configured = false
	draw_configured = false
	message_configured = false
	companion_configured = false
	last_error = ""
	if String(document.get("schema", "")) != COMPANION_SCHEMA or int(document.get("schemaVersion", -1)) != 0:
		return _fail_companion("WND companion schema changed")
	var source_value: Variant = document.get("source", {})
	var inventory_value: Variant = document.get("runtimeInventory", {})
	var bindings_value: Variant = document.get("callbackBindings", {})
	var gates_value: Variant = document.get("dynamicGates", {})
	var live_value: Variant = document.get("liveBinding", {})
	if (
		typeof(source_value) != TYPE_DICTIONARY
		or typeof(inventory_value) != TYPE_DICTIONARY
		or typeof(bindings_value) != TYPE_DICTIONARY
		or typeof(gates_value) != TYPE_DICTIONARY
		or typeof(live_value) != TYPE_DICTIONARY
	):
		return _fail_companion("WND companion shape is invalid")
	var source := source_value as Dictionary
	if (
		String(source.get("virtualPath", "")) != "window/controlbar.wnd"
		or String(source.get("sha256", "")) != SOURCE_SHA256
		or int(source.get("windowCount", -1)) != 87
		or int(source.get("callbackCount", -1)) != 21
		or String(source.get("activationAuthority", "")) != "active-companion-not-candidate-dead"
		or document.get("oracleAggregates", {}) != ORACLE_AGGREGATES
	):
		return _fail_companion("WND companion source or oracle identity changed")
	var bindings := bindings_value as Dictionary
	if bindings.size() != BINDINGS.size():
		return _fail_companion("WND companion callback count changed")
	for name_value in BINDINGS.keys():
		var name := String(name_value)
		if not bindings.has(name) or bindings[name] != BINDINGS[name]:
			return _fail_companion("WND companion callback binding changed: %s" % name)
	var inventory := inventory_value as Dictionary
	if (
		inventory.get("implementedCallbacks", []) != IMPLEMENTED
		or int(inventory.get("implementedCallbackCount", -1)) != IMPLEMENTED.size()
		or inventory.get("requiredMessageCallbacks", []) != MEN_V_MEN_REQUIRED_CALLBACKS
		or int(inventory.get("requiredMessageCallbackCount", -1)) != MEN_V_MEN_REQUIRED_CALLBACKS.size()
		or inventory.get("requiredMessageUnimplemented", ["missing"]) != []
		or inventory.get("outsideSlice", {}) != OUTSIDE_SLICE_CALLBACKS
		or inventory.get("unresolvedBuiltins", []) != UNRESOLVED_BUILTIN_CALLBACKS
	):
		return _fail_companion("WND companion runtime inventory changed")
	var gates := gates_value as Dictionary
	if gates.get("drawAndService", []) != DRAW_SERVICE_GATES or gates.get("messageAliases", []) != MESSAGE_ALIAS_GATES:
		return _fail_companion("WND companion dynamic gates changed")
	var live := live_value as Dictionary
	if (
		bool(live.get("callbackDispatchBound", true))
		or bool(live.get("renderServicesBound", true))
		or bool(live.get("genericDispatchAllowed", true))
		or bool(live.get("fallbackVisualsAllowed", true))
	):
		return _fail_companion("WND companion live binding was weakened")
	configured = true
	draw_configured = true
	message_configured = true
	companion_configured = true
	return true


func control_bar_input(control: Dictionary, _message: int, _data1: int, _data2: int) -> Dictionary:
	if not _can_message_execute("ControlBarInput", control, "input"):
		return _rejected()
	return {"ok": true, "handled": 0, "effects": []}


func game_win_block_input(control: Dictionary, message: int, _data1: int, _data2: int, services: Dictionary) -> Dictionary:
	if not _can_message_execute("GameWinBlockInput", control, "input"):
		return _rejected()
	if message in [0x0015, 0x0018]:
		return {"ok": true, "handled": 0, "effects": []}
	if message != 0x0006:
		return {"ok": true, "handled": 1, "effects": []}
	var commit_value: Variant = services.get("commit_effects")
	if typeof(commit_value) != TYPE_CALLABLE or not (commit_value as Callable).is_valid():
		return _execution_fail("GameWinBlockInput service commit is absent")
	var effects := [
		{"service": "0x00e03220", "target": "0x00852354", "args": [0]},
		{"service": "0x00e03220", "target": "0x0082fa01", "args": []},
		{"service": "0x00dfea3c", "vslot": "0x1a4", "args": [0]},
		{"service": "0x00dfedf0", "vslot": "0x0ac", "args": [0]},
		{"service": "0x00dfedf0", "vslot": "0x06c", "args": [0]},
	]
	if not bool((commit_value as Callable).call(effects.duplicate(true))):
		return _execution_fail("GameWinBlockInput atomic service commit failed")
	return {"ok": true, "handled": 1, "effects": effects}


func pass_selected_buttons_to_parent_system(control: Dictionary, message: int, data1: int, data2: int, services: Dictionary) -> Dictionary:
	if not _can_message_execute("PassSelectedButtonsToParentSystem", control, "system"):
		return _rejected()
	if message not in FORWARDED_MESSAGES:
		return {"ok": true, "handled": 0, "effects": []}
	var resolve_value: Variant = services.get("resolve_parent")
	var forward_value: Variant = services.get("forward_message")
	if (
		typeof(resolve_value) != TYPE_CALLABLE
		or not (resolve_value as Callable).is_valid()
		or typeof(forward_value) != TYPE_CALLABLE
		or not (forward_value as Callable).is_valid()
	):
		return _execution_fail("PassSelectedButtons typed services are absent")
	var parent: Variant = (resolve_value as Callable).call(control.duplicate(true))
	if parent == null:
		return {"ok": true, "handled": 0, "effects": []}
	var handled := int((forward_value as Callable).call(parent, message, data1, data2))
	return {"ok": true, "handled": handled, "effects": [{"target": "parent", "message": message, "data1": data1, "data2": data2}]}


func left_hud_input(control: Dictionary, message: int, data1: int, data2: int, radar_state: Dictionary) -> Dictionary:
	if not _can_message_execute("LeftHUDInput", control, "input"):
		return _rejected()
	if (
		not _required_bool(radar_state, "modeByte10")
		or not _required_bool(radar_state, "modeByte11")
		or not _required_bool(radar_state, "predicate006aa08e")
	):
		return _execution_fail("LeftHUDInput mode predicate state is incomplete")
	var gates := [
		"object-field-0x14-type-aliases",
		"command-id-0x42f-or-0x430-selected-object-branch",
		"messages-0x11-0x12-0x18-selection-camera-aliases",
	]
	if not bool(radar_state.modeByte10) and not bool(radar_state.modeByte11) and not bool(radar_state.predicate006aa08e):
		return _message_result(1, [], gates)
	if message in [0x0006, 0x0008, 0x0009, 0x000A, 0x000E]:
		return _message_result(1, [], gates)
	if message in [0x0005, 0x000D]:
		if not _required_bool(radar_state, "selectedObjectPresent"):
			return _execution_fail("LeftHUDInput selected-object state is absent")
		if bool(radar_state.selectedObjectPresent) and (not radar_state.has("objectField14") or typeof(radar_state.objectField14) != TYPE_INT or int(radar_state.objectField14) not in [0x0A, 0x18, 0x20, 0x26]):
			return _execution_fail("LeftHUDInput object field alias is outside the exact set")
		var effects := [
			{"kind": "derive-control-rect", "target": "0x00713bc6"},
			{"kind": "derive-control-rect", "target": "0x00713b3c"},
			{"kind": "project-pointer", "target": "0x006d81ec", "data1": data1, "data2": data2},
			{"kind": "project-pointer", "target": "0x006d7744"},
			{
				"kind": "route-opaque-radar-action",
				"selectedObjectPresent": bool(radar_state.selectedObjectPresent),
				"objectField14": int(radar_state.get("objectField14", -1)),
				"candidateCommandIds": [0x42F, 0x430],
			},
		]
		return _message_result(1, effects, gates)
	if message in [0x0000, 0x0011, 0x0012, 0x0018]:
		return _message_result(1, [
			{"kind": "query-selected-object-and-radar-projection"},
			{
				"kind": "opaque-selection-camera-branch",
				"message": message,
				"candidateTargets": ["0x00dfdca0:vslot-0x4c", "selection-services"],
			},
		], gates)
	return _message_result(0, [], gates)


func control_bar_system(control: Dictionary, message: int, _data1: int, data2: int, cached_controls: Dictionary) -> Dictionary:
	if not _can_message_execute("ControlBarSystem", control, "system"):
		return _rejected()
	if not _required_bool(cached_controls, "gameStateGateActive"):
		return _execution_fail("ControlBarSystem game-state gate is absent")
	var gates := [
		"eight-cached-handle-semantic-aliases",
		"message-0x4031-matched-service-aliases",
		"message-0x400b-rejection-before-selected-button-fallback",
	]
	if bool(cached_controls.gameStateGateActive):
		return _message_result(0, [], gates)
	match message:
		0x0001:
			return _message_result(1, [{
				"kind": "resolve-and-cache-control",
				"resolver": "0x00df36a4",
				"destination": "0x00e02f00",
			}], gates)
		0x4006, 0x4007:
			return _message_result(1, [{
				"kind": "selected-button-dispatch",
				"target": "0x0071abd4",
				"message": message,
				"data2": data2,
			}], gates)
		0x4008, 0x4009, 0x400B:
			return _message_result(1, [
				{
					"kind": "lazy-resolve-cached-controls",
					"resolver": "0x00df36a4",
					"destinations": ["0x00e02f00", "0x00e02f04", "0x00e02f08", "0x00e02f0c", "0x00e02f10", "0x00e02f14", "0x00e02f1c", "0x00e02f20"],
				},
				{
					"kind": "route-opaque-cached-control",
					"message": message,
					"data2": data2,
					"message400bOrdering": "cached-control-rejection-before-selected-button-fallback",
				},
			], gates)
		0x4031:
			if not _required_bool(cached_controls, "matchedCachedControl"):
				return _execution_fail("ControlBarSystem 0x4031 match state is absent")
			var effects := [
				{"kind": "resolve-data2-to-control", "target": "0x009c4ae9", "data2": data2},
				{"kind": "compare-cached-control-handles", "range": "0x00e02f00..0x00e02f24"},
			]
			if bool(cached_controls.matchedCachedControl):
				effects.append({"kind": "dispatch-opaque-matched-service"})
			return _message_result(1 if bool(cached_controls.matchedCachedControl) else 0, effects, gates)
	return _message_result(0, [], gates)


func w3d_command_bar_background_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DCommandBarBackgroundDraw", control):
		return _rejected()
	if not _required_bool(state, "ownerPresent"):
		return _execution_fail("WND background owner state is absent")
	if not bool(state.ownerPresent):
		return _draw_result([], ["background-blend-and-parameter-aliases"])
	if not _required_bool(state, "markerWindowPresent") or not _required_rect(state, "rect") or not _required_token(state, "instanceDrawState"):
		return _execution_fail("WND background typed state is incomplete")
	var commands := [
		{"kind": "lazy-resolve-image", "cache": "0x00de60ec", "asset": "ControlBar.wnd:BackgroundMarker"},
		{"kind": "resolve-window", "controlId": "ControlBar.wnd:BackgroundMarker"},
	]
	if not bool(state.markerWindowPresent):
		return _draw_result(commands, ["background-blend-and-parameter-aliases"])
	commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
	commands.append({"kind": "retail-call", "target": "0x0071aeae", "state": String(state.instanceDrawState)})
	commands.append({"kind": "retail-draw", "target": "0x0071fac5", "asset": "ControlBar.wnd:BackgroundMarker", "rect": (state.rect as Array).duplicate()})
	return _draw_result(commands, ["background-blend-and-parameter-aliases"])


func w3d_command_bar_foreground_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DCommandBarForegroundDraw", control):
		return _rejected()
	if not _required_bool(state, "ownerPresent"):
		return _execution_fail("WND foreground owner state is absent")
	if not bool(state.ownerPresent):
		return _draw_result([], ["foreground-image-indirection"])
	if not _required_bool(state, "markerWindowPresent") or not _required_rect(state, "rect") or not _required_token(state, "instanceDrawState"):
		return _execution_fail("WND foreground typed state is incomplete")
	var commands := [
		{"kind": "lazy-resolve-image", "cache": "0x00de60fc", "asset": "ControlBar.wnd:BackgroundMarker"},
		{"kind": "resolve-window", "controlId": "ControlBar.wnd:BackgroundMarker"},
	]
	if not bool(state.markerWindowPresent):
		return _draw_result(commands, ["foreground-image-indirection"])
	commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
	commands.append({"kind": "retail-call", "target": "0x0071ae93", "state": String(state.instanceDrawState)})
	commands.append({"kind": "retail-draw", "target": "0x0071fa9a", "asset": "ControlBar.wnd:BackgroundMarker", "rect": (state.rect as Array).duplicate()})
	return _draw_result(commands, ["foreground-image-indirection"])


func w3d_command_bar_gen_exp_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DCommandBarGenExpDraw", control):
		return _rejected()
	if not _required_bool(state, "predicatePassed"):
		return _execution_fail("WND experience predicate state is absent")
	var commands := [{"kind": "retail-predicate", "target": "0x006aa231", "passed": bool(state.predicatePassed)}]
	if not bool(state.predicatePassed):
		return _draw_result(commands, ["experience-progress-and-blend-aliases"])
	if not _required_rect(state, "rect") or not _required_token(state, "instanceDrawState") or not state.has("progress") or typeof(state.progress) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(state.progress)):
		return _execution_fail("WND experience typed state is incomplete")
	var progress := clampf(float(state.progress), 0.0, 100.0)
	for asset in ["GenExpBarTop1", "GenExpBarBottom1", "GenExpBar1"]:
		commands.append({"kind": "lazy-resolve-image", "asset": asset})
	commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
	commands.append({"kind": "clamp-progress", "minimum": 0.0, "maximum": 100.0, "value": progress})
	for asset in ["GenExpBarTop1", "GenExpBarBottom1", "GenExpBar1"]:
		commands.append({"kind": "image-draw", "asset": asset, "vslot": "0x108", "progress": progress, "state": String(state.instanceDrawState)})
	return _draw_result(commands, ["experience-progress-and-blend-aliases"])


func w3d_command_bar_grid_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DCommandBarGridDraw", control):
		return _rejected()
	if not _required_bool(state, "predicateNegative"):
		return _execution_fail("WND grid predicate state is absent")
	var commands := [{"kind": "retail-predicate", "target": "0x0070f45f", "negative": bool(state.predicateNegative)}]
	if bool(state.predicateNegative):
		commands.append({"kind": "delegate", "target": "0x0049dc32"})
		return _draw_result(commands, ["grid-cell-material-parameters"])
	if not _required_rect(state, "rect") or not _required_token(state, "gridState") or typeof(state.get("cells", [])) != TYPE_ARRAY or (state.cells as Array).size() > 4:
		return _execution_fail("WND grid typed state is incomplete")
	for cell_value in state.cells as Array:
		if not _rect_value_valid(cell_value):
			return _execution_fail("WND grid cell rectangle is invalid")
	commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
	commands.append({"kind": "retail-call", "target": "0x007143ff", "state": String(state.gridState)})
	for index in (state.cells as Array).size():
		commands.append({"kind": "grid-cell-draw", "target": "0x0044d664", "cellIndex": index, "rect": ((state.cells as Array)[index] as Array).duplicate()})
	return _draw_result(commands, ["grid-cell-material-parameters"])


func w3d_command_bar_top_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DCommandBarTopDraw", control):
		return _rejected()
	if not _required_bool(state, "buttonGeneralPresent"):
		return _execution_fail("WND top ButtonGeneral state is absent")
	var commands := [{"kind": "resolve-window", "controlId": "ControlBar.wnd:ButtonGeneral"}]
	if not bool(state.buttonGeneralPresent):
		return _draw_result(commands, ["top-tail-draw-parameters"])
	if not _required_bool(state, "predicatePassed"):
		return _execution_fail("WND top predicate state is absent")
	commands.append({"kind": "retail-predicate", "target": "0x00713cd9", "passed": bool(state.predicatePassed)})
	if bool(state.predicatePassed):
		commands.append({"kind": "tail-dispatch", "target": "0x006a7dd0", "asset": "ControlBar.wnd:ButtonGeneral"})
	return _draw_result(commands, ["top-tail-draw-parameters"])


func w3d_gadget_push_button_image_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DGadgetPushButtonImageDraw", control):
		return _rejected()
	if not _required_bool(state, "predicateDelegate"):
		return _execution_fail("WND push-button predicate state is absent")
	var commands := [{"kind": "retail-predicate", "target": "0x00727df6", "delegate": bool(state.predicateDelegate)}]
	if bool(state.predicateDelegate):
		commands.append({"kind": "delegate", "target": "0x004a501c"})
		return _draw_result(commands, [])
	if not _required_bool(state, "imagePresent"):
		return _execution_fail("WND push-button image state is absent")
	if not bool(state.imagePresent):
		return _draw_result(commands, [])
	if not _required_bool(state, "flag20"):
		return _execution_fail("WND push-button flag state is absent")
	if bool(state.flag20):
		if not _required_rect(state, "rect"):
			return _execution_fail("WND push-button rectangle is absent")
		commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
		commands.append({"kind": "image-draw", "target": "0x004a56ed", "imageSource": "control+0x84"})
	else:
		commands.append({"kind": "fallback", "target": "0x004a4b7c"})
	return _draw_result(commands, [])


func w3d_left_hud_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DLeftHUDDraw", control):
		return _rejected()
	if not _required_bool(state, "radarObjectPresent") or not _required_rect(state, "rect") or not _required_byte(state, "modeByte10") or not _required_byte(state, "modeByte11"):
		return _execution_fail("WND left HUD typed state is incomplete")
	var commands := [
		{"kind": "service-query", "service": "0x00dfedf0", "vslot": "0x164"},
		{"kind": "derive-rect", "rect": (state.rect as Array).duplicate()},
	]
	if bool(state.radarObjectPresent):
		commands.append({"kind": "radar-draw", "receiver": "queried-radar-object", "vslot": "0x104"})
	else:
		commands.append({"kind": "mode-gated-service-draw", "receiver": "opaque-mode-selected-service", "vslot": "0x1c", "modeBytes": [int(state.modeByte10), int(state.modeByte11)]})
	return _draw_result(commands, ["radar-object-and-blend-clipping-structures"])


func w3d_power_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DPowerDraw", control):
		return _rejected()
	if not _required_rect(state, "rect") or not _required_token(state, "powerState"):
		return _execution_fail("WND power typed state is incomplete")
	var commands := []
	for asset in ["PowerPointY", "PowerPointG", "PowerBarSlider"]:
		commands.append({"kind": "lazy-resolve-image", "asset": asset})
	commands.append({"kind": "query-player-power", "state": String(state.powerState)})
	commands.append({"kind": "derive-rect", "rect": (state.rect as Array).duplicate()})
	commands.append({"kind": "image-draw", "asset": "PowerPointY", "vslot": "0x108", "geometry": "retail-computed"})
	commands.append({"kind": "image-draw", "asset": "PowerPointG", "vslot": "0xa8", "geometry": "retail-computed"})
	commands.append({"kind": "image-draw", "asset": "PowerBarSlider", "vslot": "0xb0", "geometry": "retail-computed-clipped-slider"})
	return _draw_result(commands, ["power-counters-and-image-blend-state-aliases"])


func w3d_right_hud_draw(control: Dictionary, _instance_data: Dictionary, state: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DRightHUDDraw", control):
		return _rejected()
	if not _required_bool(state, "predicateNegative"):
		return _execution_fail("WND right HUD predicate state is absent")
	var commands := [{"kind": "retail-predicate", "target": "0x0070f45f", "negative": bool(state.predicateNegative)}]
	if bool(state.predicateNegative):
		commands.append({"kind": "delegate", "target": "0x0049dc32"})
	return _draw_result(commands, [])


func w3d_no_draw(control: Dictionary, _instance_data: Dictionary) -> Dictionary:
	if not _can_draw_execute("W3DNoDraw", control):
		return _rejected()
	return _draw_result([], [])


func unimplemented_callbacks() -> Array[String]:
	var result: Array[String] = []
	for name_value in BINDINGS.keys():
		var name := String(name_value)
		if name not in IMPLEMENTED:
			result.append(name)
	result.sort()
	return result


func implemented_draw_callbacks() -> Array[String]:
	var result: Array[String] = []
	for name_value in DRAW_SPECS.keys():
		result.append(String(name_value))
	result.sort()
	return result


func unimplemented_draw_callbacks() -> Array[String]:
	return ["W3DGameWinDefaultDraw"]


func unimplemented_non_draw_callbacks() -> Array[String]:
	var result: Array[String] = []
	for name in unimplemented_callbacks():
		if name not in unimplemented_draw_callbacks():
			result.append(name)
	return result


func men_v_men_required_callbacks() -> Array[String]:
	return MEN_V_MEN_REQUIRED_CALLBACKS.duplicate()


func men_v_men_required_unimplemented_callbacks() -> Array[String]:
	var result: Array[String] = []
	for name_value in MEN_V_MEN_REQUIRED_CALLBACKS:
		var name := String(name_value)
		if name not in IMPLEMENTED:
			result.append(name)
	return result


func outside_slice_callbacks() -> Dictionary:
	return OUTSIDE_SLICE_CALLBACKS.duplicate()


func unresolved_builtin_callbacks() -> Array[String]:
	return UNRESOLVED_BUILTIN_CALLBACKS.duplicate()


func message_dynamic_gates() -> Dictionary:
	return {
		"GameWinBlockInput": [MESSAGE_ALIAS_GATES[0]],
		"LeftHUDInput": MESSAGE_ALIAS_GATES.slice(1, 4),
		"ControlBarSystem": MESSAGE_ALIAS_GATES.slice(4, 7),
	}


func _can_message_execute(callback: String, control: Dictionary, kind: String) -> bool:
	if not message_configured:
		last_error = "WND message runtime is not configured"
		return false
	return _can_execute(callback, control, kind)


func _message_result(handled: int, effects: Array, dynamic_gates: Array) -> Dictionary:
	return {
		"ok": true,
		"handled": handled,
		"effects": effects.duplicate(true),
		"commands": [],
		"dynamicGates": dynamic_gates.duplicate(),
	}


func _can_draw_execute(callback: String, control: Dictionary) -> bool:
	if not draw_configured:
		last_error = "WND draw runtime is not configured"
		return false
	return _can_execute(callback, control, "draw")


func _draw_result(commands: Array, dynamic_gates: Array) -> Dictionary:
	return {
		"ok": true,
		"commands": commands.duplicate(true),
		"effects": [],
		"dynamicGates": dynamic_gates.duplicate(),
		"renderingCommitted": false,
	}


func _required_bool(state: Dictionary, key: String) -> bool:
	return state.has(key) and typeof(state.get(key)) == TYPE_BOOL


func _required_byte(state: Dictionary, key: String) -> bool:
	return state.has(key) and typeof(state.get(key)) == TYPE_INT and int(state.get(key)) >= 0 and int(state.get(key)) <= 255


func _required_token(state: Dictionary, key: String) -> bool:
	return state.has(key) and typeof(state.get(key)) == TYPE_STRING and String(state.get(key)) != ""


func _required_rect(state: Dictionary, key: String) -> bool:
	return state.has(key) and _rect_value_valid(state.get(key))


func _rect_value_valid(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 4:
		return false
	for item in value as Array:
		if typeof(item) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(item)):
			return false
	return true


func _can_execute(callback: String, control: Dictionary, kind: String) -> bool:
	if not configured:
		last_error = "WND runtime is not configured"
		return false
	var key := "%s|%d|%s|%s" % [kind, int(control.get("windowIndex", -1)), String(control.get("controlId", "")), String(control.get("controlType", ""))]
	if key not in BINDINGS[callback]:
		last_error = "WND typed control identity changed: %s" % callback
		return false
	last_error = ""
	return true


func _binding_keys(value: Variant) -> Array:
	if typeof(value) != TYPE_ARRAY:
		return []
	var result := []
	for control_value in value as Array:
		if typeof(control_value) != TYPE_DICTIONARY:
			return []
		var control := control_value as Dictionary
		result.append("%s|%d|%s|%s" % [String(control.get("kind", "")), int(control.get("windowIndex", -1)), String(control.get("controlId", "")), String(control.get("controlType", ""))])
	return result


func _draw_binding_keys(value: Variant) -> Array:
	if typeof(value) != TYPE_ARRAY:
		return []
	var result := []
	for control_value in value as Array:
		if typeof(control_value) != TYPE_DICTIONARY:
			return []
		var control := control_value as Dictionary
		result.append("draw|%d|%s|%s" % [int(control.get("windowIndex", -1)), String(control.get("controlId", "")), String(control.get("controlType", ""))])
	return result


func _execution_fail(message: String) -> Dictionary:
	last_error = message
	return _rejected()


func _rejected() -> Dictionary:
	return {"ok": false, "handled": 0, "effects": [], "commands": []}


func _fail(message: String) -> bool:
	last_error = message
	configured = false
	return false


func _fail_draw(message: String) -> bool:
	last_error = message
	draw_configured = false
	return false


func _fail_message(message: String) -> bool:
	last_error = message
	message_configured = false
	return false


func _fail_companion(message: String) -> bool:
	last_error = message
	configured = false
	draw_configured = false
	message_configured = false
	companion_configured = false
	return false
