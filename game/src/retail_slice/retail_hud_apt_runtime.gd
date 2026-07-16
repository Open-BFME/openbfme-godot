class_name RetailHudAptRuntime
extends Control
## Bounded renderer for the declarative subset of the retail BFME2 APT HUD.
##
## Only the source-proven timeline-control, five byte-exact typed initialize
## effects (including three live Palantir strings), three exact typed MinLOD
## branches, the exact resource-flash entry, and two declaration-only Palantir
## CommandButtons registrations are executable. Unproven rendering and
## downstream native/mixer semantics remain explicit blockers.

const MAX_DOCUMENT_BYTES := 32 * 1024 * 1024
const MAX_DRAWS := 100_000
const MAX_TIMELINES := 4096
const MAX_TIMELINE_FRAMES := 100_000
const MAX_TIMELINE_INSTANCES := 100_000
const MAX_ACTION_SCRIPTS := 100_000
const MAX_CLIP_ACTIONS := 100_000
const MAX_TEXTS := 16_384
const MAX_BUTTONS := 16_384
const EXPECTED_SCHEMA := "openbfme.retail-hud-apt-runtime"
const EXPECTED_SCENE_ID := "bfme2.ui.palantir"
const ASSET_FACTORY := preload("res://src/view/asset_factory.gd")
const WND_RUNTIME_SCRIPT := preload("res://src/retail_slice/retail_hud_wnd_runtime.gd")
const EXTERNAL_MOVIE_SLOT_SPECS := [
	{
		"loadOrder": 0, "movie": "InGameSpellBook", "swf": "InGameSpellBook.swf",
		"target": "SpellBookUI", "targetPath": "Palantir.root.frame0/SpellBookUI",
		"loadInstructionOffset": 364906, "sourceOffset": 95944, "depth": 3,
		"recordSha256": "2fdc0ace8677b1243f09fac472fd3a1df1f9750160769ce6dfa17154b4b176cf",
		"matrix": [0.9998626708984375, 0.0, 0.0, 0.9999847412109375],
		"translation": [0.0, 0.0], "frameCount": 18,
		"labels": {"_hide": 0, "_show": 9}, "initialStopFrame": 8,
		"programOffset": 22016,
		"programSha256": "bc9e19fd0ad7500926b31dba505f878012335ec804ec35707cfa733ecb5b943d",
		"defaultState": "hidden-dormant", "normalMenVsMen": "dormant-until-spell-book-host-show",
		"loadedCallback": "OnAptInGameSpellBookLoaded", "unloadedCallback": "OnAptInGameSpellBookUnloaded",
		"argument": "GetFullName(this)", "godotInterface": "RetailSpellBookSlot",
	},
	{
		"loadOrder": 2, "movie": "InGameHelpBox", "swf": "InGameHelpBox.swf",
		"target": "helpBox", "targetPath": "Palantir.root.frame0/helpBox",
		"loadInstructionOffset": 364938, "sourceOffset": 97480, "depth": 176,
		"recordSha256": "db2e7a3042c175a1a9a7ab233f472d482bffd19daf8addf05ea83d36f0090028",
		"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [585.0, 607.0],
		"frameCount": 1, "labels": {}, "initialStopFrame": 0, "programOffset": 3636,
		"programSha256": "71664be06717e6e52ee407d44bda48934a427601d7419d03f26ca75be7eda502",
		"defaultState": "hidden-dormant", "normalMenVsMen": "dormant-until-help-show",
		"loadedCallback": "AptPalantir::OnHelpBoxLoaded", "unloadedCallback": "AptPalantir::OnHelpBoxUnloaded",
		"argument": "clip.toString()", "godotInterface": "RetailHelpBoxSlot",
	},
	{
		"loadOrder": 3, "movie": "InGameHeroSelect", "swf": "InGameHeroSelect.swf",
		"target": "HeroSelectUI", "targetPath": "Palantir.root.frame0/HeroSelectUI",
		"loadInstructionOffset": 364954, "sourceOffset": 97416, "depth": 174,
		"recordSha256": "e1a1230fb4f99386fa721c3c9fc59ac093c68e7c21c6318689acbb7c3c182a05",
		"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [375.0, 700.0],
		"frameCount": 29, "labels": {"_fadein": 9, "_hide": 0, "_show": 19},
		"initialStopFrame": 8, "programOffset": 167740,
		"programSha256": "2c43ab2db3b3f9158706bc28154ed4362c9e6fd237bcc218f9e1100621190b1f",
		"defaultState": "hidden-until-captured-show-result",
		"normalMenVsMen": "unresolved-show-hero-select-interface",
		"loadedCallback": "AptPalantir::OnHeroSelectLoaded", "unloadedCallback": "AptPalantir::OnHeroSelectUnloaded",
		"argument": "clip.toString()", "godotInterface": "RetailHeroSelectSlot",
	},
	{
		"loadOrder": 4, "movie": "InGamePlanningMode", "swf": "InGamePlanningMode.swf",
		"target": "planningModeUI", "targetPath": "Palantir.root.frame0/planningModeUI",
		"loadInstructionOffset": 364970, "sourceOffset": 97608, "depth": 180,
		"recordSha256": "cf671b9ebd00322cfec4a4f996f946f8f8942320fc79f30d4a9f8061257c28c3",
		"matrix": [1.0, 0.0, 0.0, 1.0], "translation": [512.0, 30.0],
		"frameCount": 27, "labels": {"_close": 19, "_init": 0, "_open": 9},
		"initialStopFrame": 8, "programOffset": 28244,
		"programSha256": "6a660a1b59561308ac86226bcbf83f347e87b82e894aeb383b60be2e79d74e97",
		"defaultState": "closed-dormant", "normalMenVsMen": "dormant-until-planning-open",
		"loadedCallback": "AptPalantir::OnPlanningModeUILoaded", "unloadedCallback": "AptPalantir::OnPlanningModeUIUnloaded",
		"argument": "clip.toString()", "godotInterface": "RetailPlanningModeSlot",
	},
]
const EXTERNAL_MOVIE_GATE_IDS := [
	"apt-load-completion-order",
	"hero-select-initial-visibility",
	"palantir-target-removal-order",
	"help-box-alt-anchor-runtime-value",
]
const CLIP_EVENT_NAMES := {
	0x800000: "key-up", 0x400000: "key-down", 0x200000: "mouse-up",
	0x100000: "mouse-down", 0x080000: "mouse-move", 0x040000: "unload",
	0x020000: "enter-frame", 0x010000: "load", 0x008000: "drag-over",
	0x004000: "roll-out", 0x002000: "roll-over", 0x001000: "release-outside",
	0x000800: "release", 0x000400: "press", 0x000200: "drag-out",
	0x000100: "data", 0x000004: "construct", 0x000002: "key-press",
	0x000001: "initialize",
}
const TYPED_INITIALIZE_PROGRAMS := {
	"ingamesidecommandbar:clip-event:13680": {
		"movie": "InGameSideCommandBar",
		"sourceOffset": 13680,
		"instructionOffset": 13944,
		"byteLength": 59,
		"sha256": "782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc",
		"maximumStackDepth": 4,
		"effect": {
			"kind": "define-local-method",
			"receiver": "this",
			"methodName": "SetFlashEffectState",
			"parameters": ["state"],
			"body": {
				"kind": "call-indexed-ancestor-timeline-method",
				"receiverAncestorHops": 2,
				"collection": "flashEffects",
				"index": {"ancestorHops": 1, "property": "_name"},
				"methodName": "gotoAndPlay",
				"arguments": [{"kind": "parameter", "name": "state"}],
			},
			"sourceEvidence": {
				"programId": "ingamesidecommandbar:clip-event:13680",
				"instructionOffset": 13944,
				"instructionEndOffset": 14003,
				"byteLength": 59,
				"sha256": "782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc",
			},
		},
	},
	"libingameui:clip-event:56252": {
		"movie": "libInGameUI",
		"sourceOffset": 56252,
		"instructionOffset": 57324,
		"byteLength": 13,
		"sha256": "0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6",
		"maximumStackDepth": 3,
		"effect": {
			"kind": "set-clip-property",
			"target": "",
			"propertyIndex": 7,
			"propertyName": "_visible",
			"value": false,
			"sourceEvidence": {
				"programId": "libingameui:clip-event:56252",
				"instructionOffset": 57324,
				"instructionEndOffset": 57337,
				"byteLength": 13,
				"sha256": "0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6",
			},
		},
	},
	"palantir:clip-event:375628": {
		"movie": "Palantir", "sourceOffset": 375628,
		"instructionOffset": 377292, "byteLength": 17,
		"sha256": "b603bb6578f35f5f590765c18791b9dc1ab749058bdac674f0a85381c8b900f1",
		"maximumStackDepth": 2,
		"effect": {
			"kind": "bind-live-text", "targetMember": "stringName",
			"aptVariable": "$PalantirResources", "runtimeInputs": ["resources"],
			"formatter": "percent-d-space-when-negative",
			"sourceEvidence": {
				"programId": "palantir:clip-event:375628",
				"instructionOffset": 377292, "instructionEndOffset": 377309,
				"byteLength": 17,
				"sha256": "b603bb6578f35f5f590765c18791b9dc1ab749058bdac674f0a85381c8b900f1",
			},
		},
	},
	"palantir:clip-event:375640": {
		"movie": "Palantir", "sourceOffset": 375640,
		"instructionOffset": 377312, "byteLength": 17,
		"sha256": "3ea5b0333ab5527877ac56d1107ec97caed63bf3a56d1bb89bc2192d6290926f",
		"maximumStackDepth": 2,
		"effect": {
			"kind": "bind-live-text", "targetMember": "stringName",
			"aptVariable": "$PalantirResourceMultiplier", "runtimeInputs": ["resourceMultiplier"],
			"formatter": "x-percent-g-space-when-exactly-one",
			"sourceEvidence": {
				"programId": "palantir:clip-event:375640",
				"instructionOffset": 377312, "instructionEndOffset": 377329,
				"byteLength": 17,
				"sha256": "3ea5b0333ab5527877ac56d1107ec97caed63bf3a56d1bb89bc2192d6290926f",
			},
		},
	},
	"palantir:clip-event:375652": {
		"movie": "Palantir", "sourceOffset": 375652,
		"instructionOffset": 377332, "byteLength": 17,
		"sha256": "ff3de0b7f0a657cb41fb9c3de4e4fc42e68c9753d32a4b42515b5247ff8f357c",
		"maximumStackDepth": 2,
		"effect": {
			"kind": "bind-live-text", "targetMember": "stringName",
			"aptVariable": "$PalantirCommandPoints",
			"runtimeInputs": ["commandPointsCurrent", "commandPointsCap"],
			"formatter": "percent-d-slash-percent-d-current-or-space",
			"sourceEvidence": {
				"programId": "palantir:clip-event:375652",
				"instructionOffset": 377332, "instructionEndOffset": 377349,
				"byteLength": 17,
				"sha256": "ff3de0b7f0a657cb41fb9c3de4e4fc42e68c9753d32a4b42515b5247ff8f357c",
			},
		},
	},
}
const TYPED_MINLOD_PROGRAMS := {
	"palantir:152912": {
		"movie": "Palantir", "sourceOffset": 152912,
		"instructionOffset": 366952, "byteLength": 46,
		"sha256": "069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4",
		"maximumStackDepth": 2,
		"effect": {
			"kind": "conditional-min-lod",
			"condition": {"kind": "required-boolean-input", "name": "MinLOD", "equals": true},
			"whenTrue": [{
				"kind": "stop-timeline-if-property-equals", "target": "this",
				"propertyIndex": 13, "propertyName": "_name", "equals": "GlobeSwirlRender",
			}],
			"whenFalse": [],
			"sourceEvidence": {
				"programId": "palantir:152912", "instructionOffset": 366952,
				"instructionEndOffset": 366998, "byteLength": 46,
				"sha256": "069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4",
			},
		},
	},
	"palantir:333872": {
		"movie": "Palantir", "sourceOffset": 333872,
		"instructionOffset": 370784, "byteLength": 37,
		"sha256": "93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba",
		"maximumStackDepth": 3,
		"effect": {
			"kind": "conditional-min-lod",
			"condition": {"kind": "required-boolean-input", "name": "MinLOD", "equals": true},
			"whenTrue": [
				{"kind": "set-named-clip-property", "target": "effect1", "propertyName": "_visible", "value": false},
				{"kind": "set-named-clip-property", "target": "effect4", "propertyName": "_visible", "value": false},
			],
			"whenFalse": [],
			"sourceEvidence": {
				"programId": "palantir:333872", "instructionOffset": 370784,
				"instructionEndOffset": 370821, "byteLength": 37,
				"sha256": "93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba",
			},
		},
	},
	"palantir:334840": {
		"movie": "Palantir", "sourceOffset": 334840,
		"instructionOffset": 370840, "byteLength": 37,
		"sha256": "0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db",
		"maximumStackDepth": 3,
		"effect": {
			"kind": "conditional-min-lod",
			"condition": {"kind": "required-boolean-input", "name": "MinLOD", "equals": true},
			"whenTrue": [
				{"kind": "set-named-clip-property", "target": "effect2", "propertyName": "_visible", "value": false},
				{"kind": "set-named-clip-property", "target": "effect3", "propertyName": "_visible", "value": false},
			],
			"whenFalse": [],
			"sourceEvidence": {
				"programId": "palantir:334840", "instructionOffset": 370840,
				"instructionEndOffset": 370877, "byteLength": 37,
				"sha256": "0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db",
			},
		},
	},
}
const TYPED_RESOURCE_FLASH_PROGRAM := {
	"scriptId": "palantir:332504", "movie": "Palantir", "sourceOffset": 332504,
	"instructionOffset": 370752, "byteLength": 26,
	"sha256": "0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999",
	"maximumStackDepth": 4,
	"eventId": "Gui_PalantirResourceBarFlash",
}
const TYPED_SIDE_COMMAND_PROGRAMS := {
	"ingamesidecommandbar:6272": {
		"movie": "InGameSideCommandBar", "sourceOffset": 6272,
		"instructionOffset": 11952, "byteLength": 10,
		"sha256": "ee3f7f3c582961473ffbbebe851f0086820fd9fa57c62f3573a021d2c5917557",
		"maximumStackDepth": 2,
		"effects": [{"kind": "side-command-update-neighbor-frame-states", "order": ["next", "prior"]}],
	},
	"ingamesidecommandbar:6368": {
		"movie": "InGameSideCommandBar", "sourceOffset": 6368,
		"instructionOffset": 11992, "byteLength": 18,
		"sha256": "268aab1f60a086e5bf869d83da0aabdfe2f383d688bf98ce6a6a59fab274f040",
		"maximumStackDepth": 2,
		"effects": [
			{"kind": "side-command-update-frame-state"},
			{"kind": "side-command-update-neighbor-frame-states", "order": ["next", "prior"]},
		],
	},
	"ingamesidecommandbar:7296": {
		"movie": "InGameSideCommandBar", "sourceOffset": 7296,
		"instructionOffset": 12148, "byteLength": 73,
		"sha256": "56466dca85c04dd52fd50a5cb02ea625cdad4b776c7ef14a6854f6412caca675",
		"maximumStackDepth": 6,
		"effects": [{
			"kind": "side-command-show-buttons-if-in-game",
			"condition": {"kind": "required-boolean-input", "name": "InGame", "equals": true},
			"targets": ["Button1", "Button2", "Button3", "Button4", "Button5", "Button6", "Button7", "Button8", "Button9", "Button10", "Button11", "Button12", "Button13", "Button14", "Button15"],
			"missingTargetEffect": "ordered-no-op",
		}],
	},
}
const PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS := [
	{"name": "OnMovieClipFrameLoaded", "definitionOffset": 367636, "bodyOffset": 367668, "bodyByteLength": 51, "bodySha256": "2e04d77ff99a163925615cc9e6b2c7d83dbf945b428d3c9baea695a95c1e12fd", "host": "PalantirCommandUI::OnButtonFrameLoaded", "argument": "index=clip._name&name=String(clip)"},
	{"name": "OnMovieClipFrameUnloaded", "definitionOffset": 367719, "bodyOffset": 367748, "bodyByteLength": 36, "bodySha256": "4ab0920334b617d403a746a23b7634ca1c5511974f70fd6020e3e32ac7934214", "host": "PalantirCommandUI::OnButtonFrameUnloaded", "argument": "index=clip._name"},
	{"name": "OnCommandButtonSubMenuLoaded", "definitionOffset": 367784, "bodyOffset": 367816, "bodyByteLength": 59, "bodySha256": "bd603f86f55a96977d0c8d6f001952441af5fca3c4f96d1c59e84ab46eb713bc", "host": "PalantirCommandUI::OnSubMenuLoaded", "argument": "index=clip._name.substr(7)&name=String(clip)"},
	{"name": "OnCommandButtonSubMenuUnloaded", "definitionOffset": 367875, "bodyOffset": 367904, "bodyByteLength": 43, "bodySha256": "c7032d22743076388774d66857f2d788d3facea1433ff415ff287b999a0087f2", "host": "PalantirCommandUI::OnSubMenuUnloaded", "argument": "index=clip._name.substr(7)"},
	{"name": "OnCommandButtonToggleFlashLoaded", "definitionOffset": 367947, "bodyOffset": 367976, "bodyByteLength": 59, "bodySha256": "86c91c219bada277fcccbe6a103b33ce9c17b870d05c0b806728e0051664a02e", "host": "PalantirCommandUI::OnToggleFlashLoaded", "argument": "index=clip._name.substr(11)&name=String(clip)"},
	{"name": "OnCommandButtonToggleFlashUnloaded", "definitionOffset": 368035, "bodyOffset": 368064, "bodyByteLength": 43, "bodySha256": "251de13f80d51b7fcb72b137bd7817901c86b66c37c3dd9d51d24cd525ac3241", "host": "PalantirCommandUI::OnToggleFlashUnloaded", "argument": "index=clip._name.substr(11)"},
]
const PALANTIR_COMMAND_BUTTON_METHODS := [
	{"name": "SetAutoAbilityOverlayState", "definitionOffset": 368160, "bodyOffset": 368192, "bodyByteLength": 45, "bodySha256": "abf82bf818bb3423a889db7f20fb3b9483d5e9e7fda65710988bc50f7343a482", "parameter": "state", "target": "this._parent._parent.AutoAbilityOverlays[this._name]", "dispatch": "target.gotoAndPlay(state)"},
	{"name": "SetFlashEffectState", "definitionOffset": 368242, "bodyOffset": 368272, "bodyByteLength": 45, "bodySha256": "348936c664694b5d48c022b09f90552b509cc052dd8a00390a411d980ef46196", "parameter": "state", "target": "this._parent.FlashEffects[this._name]", "dispatch": "target.gotoAndPlay(state)"},
	{"name": "SetGlassState", "definitionOffset": 368322, "bodyOffset": 368352, "bodyByteLength": 46, "bodySha256": "c3546a83b7f1c52d876e993edea2d3f6e9c8054621f8ea3b4e94d821ba84ddb7", "parameter": "state", "target": "this._parent['glass' + this._name]", "dispatch": "target.gotoAndPlay(state)"},
]
const TYPED_PALANTIR_COMMAND_PROGRAMS := {
	"palantir:169224": {
		"movie": "Palantir", "sourceOffset": 169224,
		"instructionOffset": 367624, "byteLength": 484,
		"sha256": "3e6f347f6c6574a2d40e85f8f564c1f9af1c13513d0f1671298a1484d629fbfc",
		"maximumStackDepth": 6,
		"effects": [{"kind": "palantir-command-register-lifecycle-functions", "invocationDuringRegistration": false, "functions": PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS}],
	},
	"palantir:169256": {
		"movie": "Palantir", "sourceOffset": 169256,
		"instructionOffset": 368120, "byteLength": 293,
		"sha256": "c29cecb1997de0b9de26b4c5ec01761c81d45bc21cadf45dfd1e268ac2cefe3b",
		"maximumStackDepth": 6,
		"effects": [{"kind": "palantir-command-register-button-methods", "buttonOrder": ["0", "1", "2", "3", "4", "5"], "invocationDuringRegistration": false, "methods": PALANTIR_COMMAND_BUTTON_METHODS}],
	},
}
const SIDE_COMMAND_HELPERS := [
	{"name": "GetNextButton", "definitionOffset": 10968, "bodyOffset": 10996, "bodyByteLength": 85, "bodySha256": "a3df6709576a4a211c773964f23a50e74cb2148c23fa5f3e8f1224f5b8b57a13"},
	{"name": "IsNextButtonFrameVisible", "definitionOffset": 11081, "bodyOffset": 11108, "bodyByteLength": 101, "bodySha256": "daa6c68215c032f6ed0819f914138faa968815a25111b996d42e5725b4cf49b5"},
	{"name": "GetPriorButton", "definitionOffset": 11209, "bodyOffset": 11236, "bodyByteLength": 85, "bodySha256": "20a3b9b362d10448c370a7d0f4d466eb64fb422df1759c42b4c81f7af2bce314"},
	{"name": "IsPriorButtonFrameVisible", "definitionOffset": 11321, "bodyOffset": 11348, "bodyByteLength": 101, "bodySha256": "2d80ff5e0dcf689e0af206ce6ffcf3a029f41a419924a6d68710a9e45b717378"},
	{"name": "UpdateFrameState", "definitionOffset": 11449, "bodyOffset": 11476, "bodyByteLength": 185, "bodySha256": "4e3ebf348802940609232fefcd3c8a9693d482524af5bdfc23d00ef55806af53"},
	{"name": "UpdateNeighborFrameStates", "definitionOffset": 11661, "bodyOffset": 11688, "bodyByteLength": 137, "bodySha256": "2ffad24c431995b73b00765b2ad1f8dce45deeed25387e048ea53ffd4baf4d24"},
]
const RESOURCE_FLASH_TIMELINE_SHA256 := "f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6"
const RESOURCE_FLASH_TRIGGER_BODY_SHA256 := "a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788"
const RESOURCE_FLASH_PLACEMENT_SHA256 := "6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527"
const MEN_FORDS_SIDE_FADE_SOURCE_SHA256 := "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"
const MEN_FORDS_RETAIL_INI_SHA256 := {
	"data/ini/commandset.ini": "3d57ff841b93428ce2118d4bff1871684003bb9eacd8d48865f03ce23e4c5300",
	"data/ini/commandbutton.ini": "bd1af6bedd22acd39bd7571011ac153bd6c5e93543e5f487866e81652c9899c0",
	"data/ini/object/goodfaction/hordes/men/menhordes.ini": "5f73cdc4627d9a745fdfcf79be2d3d8379e3e9180595e98b0df62363645516b9",
	"data/ini/object/goodfaction/structures/men/fortress.ini": "6d8030714f46bc147fe55adb9a3f101aabe5a773e6b2fa50c479783b7bdb18a0",
	"data/ini/object/goodfaction/structures/men/farm.ini": "b3a243f0eb887d4127f9596e90a7b2a41e4f482cafe5ec2e538a4fd99d1c941c",
	"data/ini/object/goodfaction/structures/men/barracks.ini": "e91c4d73a80f51e77c9b6fdce063899fc2195a6abd94eff72f9d6f94531158a7",
	"data/ini/object/goodfaction/structures/men/archerrange.ini": "fc0caf596dfd74dcff21bbf645b24f92027d33e91c3185c577f78dcacddd99d6",
	"data/ini/object/goodfaction/structures/men/stable.ini": "d9a04b56739d02fb51545a1bcaac9e8b615bfa0650ef8926cbb1f9c7665ff506",
}
const MEN_FORDS_SIDE_FADE_ROSTER := {
	"bfme2.object.gondor-fighter-horde": {"kind": "battalion", "field": "unit_type", "commandSet": "GondorFighterHordeCommandSet", "eligibleCount": 8},
	"bfme2.object.gondor-tower-guard": {"kind": "battalion", "field": "unit_type", "commandSet": "GondorTowerShieldGuardCommandSet", "eligibleCount": 8},
	"bfme2.object.gondor-archer": {"kind": "battalion", "field": "unit_type", "commandSet": "GondorArcherHordeCommandSet", "eligibleCount": 8},
	"bfme2.object.gondor-knight": {"kind": "battalion", "field": "unit_type", "commandSet": "GondorKnightHordeCommandSet", "eligibleCount": 7},
	"fortress": {"kind": "structure", "field": "structure_kind", "commandSet": "MenFortressCommandSet", "eligibleCount": 4},
	"farm": {"kind": "structure", "field": "structure_kind", "commandSet": "SellableCommandSet", "eligibleCount": 1},
	"barracks": {"kind": "structure", "field": "structure_kind", "commandSet": "GondorBarracksCommandSet", "eligibleCount": 5},
	"archery_range": {"kind": "structure", "field": "structure_kind", "commandSet": "GondorArcheryCommandSet", "eligibleCount": 5},
	"stable": {"kind": "structure", "field": "structure_kind", "commandSet": "GondorStablesCommandSet", "eligibleCount": 4},
}
const MEN_FORDS_MULTI_SELECT_COMMANDS := [
	"Command_ToggleStance", "Command_AttackMove", "Command_Stop",
]

var contract_declared := false
var contract_ready := false
var presentation_ready := false
var parity_ready := false
var static_subset_opt_in := false
var error := ""
var draw_count := 0
var blocker_count := 0
var timeline_count := 0
var timeline_frame_count := 0
var timeline_instance_count := 0
var action_script_count := 0
var supported_action_script_count := 0
var clip_action_program_count := 0
var supported_clip_action_program_count := 0
var clip_action_count := 0
var clip_action_event_count := 0
var executable_clip_action_event_count := 0
var font_count := 0
var embedded_font_glyph_count := 0
var text_count := 0
var text_instance_count := 0
var button_count := 0
var button_instance_count := 0
var button_action_count := 0
var initial_frame_variant := ""
var side_command_bar_initial_state := ""
var external_movie_slot_count := 0
var external_movie_slots_ready := false
var external_movie_load_order: Array[String] = []
var native_external_reset_order: Array[String] = []
var resource_flash_ready := false
var side_command_topology_ready := false
var side_command_fade_runtime_ready := false
var side_command_fade_eligible := false
var palantir_command_topology_ready := false
var wnd_companion_ready := false
var wnd_typed_callback_count := 0
var diagnostics: Array[Dictionary] = []

var _authored_resolution := Vector2(1024.0, 768.0)
var _draws: Array[Dictionary] = []
var _textures: Dictionary = {}
var _action_scripts: Dictionary = {}
var _clip_action_programs: Dictionary = {}
var _clip_actions: Dictionary = {}
var _fonts: Dictionary = {}
var _texts: Dictionary = {}
var _buttons: Dictionary = {}
var _display_items: Array[Dictionary] = []
var _display_orders: Dictionary = {}
var _configured_pack_root := ""
var _declared_atlas_paths: Dictionary = {}
var _external_movie_slots: Dictionary = {}
var _external_movie_nodes: Array[Node2D] = []
var _staged_external_movie_slots: Array[Dictionary] = []
var _wnd_runtime: RefCounted = null
var _staged_wnd_runtime: RefCounted = null
var _side_command_fade_accumulator := 0.0
var _side_command_fade_state: Dictionary = {}
var _live_text_values := {
	"$PalantirResources": "0",
	"$PalantirResourceMultiplier": " ",
	"$PalantirCommandPoints": "0/0",
}


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)


func _process(delta: float) -> void:
	if not side_command_fade_runtime_ready or _side_command_fade_state.is_empty():
		return
	if not bool(_side_command_fade_state.get("playing", false)):
		_side_command_fade_accumulator = 0.0
		return
	_side_command_fade_accumulator += minf(delta, 0.25)
	while _side_command_fade_accumulator >= 0.033 and bool(_side_command_fade_state.get("playing", false)):
		_side_command_fade_accumulator -= 0.033
		if not advance_side_command_fade_frame():
			return


func configure_from_pack(pack_root: String, allow_static_subset := false) -> bool:
	_reset()
	var mod_loader = get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		return _fail("HUD APT runtime requires the ModLoader autoload")
	var pack_path: String = mod_loader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return _fail("HUD APT runtime requires a contained pack document")
	var pack_value: Variant = mod_loader._read_json(pack_path)
	if typeof(pack_value) != TYPE_DICTIONARY:
		return _fail("HUD APT runtime pack document is invalid")
	var files_value: Variant = (pack_value as Dictionary).get("files", {})
	if typeof(files_value) != TYPE_DICTIONARY:
		return _fail("HUD APT runtime pack files table is invalid")
	var files := files_value as Dictionary
	if not files.has("palantirScene"):
		return true
	contract_declared = true
	var relative := String(files.get("palantirScene", ""))
	var path: String = mod_loader.resolve_pack_path(pack_root, relative)
	if not _safe_file(pack_root, path, "json"):
		return _fail("declared Palantir scene contract is missing or escaped")
	var document := _read_bounded_json(path)
	if document.is_empty():
		return _fail("Palantir scene contract is invalid or empty")
	return configure_document(document, pack_root, allow_static_subset)


func reset_runtime() -> void:
	_reset()


func configure_document(document: Dictionary, pack_root: String, allow_static_subset := false) -> bool:
	_reset()
	contract_declared = true
	static_subset_opt_in = allow_static_subset
	_configured_pack_root = pack_root
	var atlas_inventory: Variant = document.get("atlases", [])
	if typeof(atlas_inventory) != TYPE_ARRAY:
		return _fail("Palantir atlas inventory is invalid")
	for atlas_value in atlas_inventory as Array:
		var relative := String(atlas_value)
		if not relative.begins_with("assets/ui/palantir/atlases/") or not relative.ends_with(".png"):
			return _fail("Palantir atlas inventory contains an unsafe path")
		_declared_atlas_paths[relative] = true
	if not _validate_contract(document, pack_root):
		return false
	_wnd_runtime = _staged_wnd_runtime
	_staged_wnd_runtime = null
	contract_ready = true
	parity_ready = blocker_count == 0
	if blocker_count > 0 and not allow_static_subset:
		return _fail("Palantir APT has unsupported semantics; static subset was not explicitly enabled")
	presentation_ready = draw_count > 0
	if not presentation_ready:
		return _fail("Palantir APT contract has no executable retail draws")
	if blocker_count > 0:
		diagnostics.append({
			"code": "apt-static-subset-explicitly-enabled",
			"blockerCount": blocker_count,
			"parityReady": false,
		})
	if not _bind_external_movie_slots():
		return false
	_set_runtime_metadata()
	queue_redraw()
	return true


func _validate_contract(document: Dictionary, pack_root: String) -> bool:
	if (
		String(document.get("schema", "")) != EXPECTED_SCHEMA
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("sceneId", "")) != EXPECTED_SCENE_ID
	):
		return _fail("unexpected Palantir APT runtime schema or scene identity")
	if not _is_sha256(String(document.get("aggregateSha256", ""))):
		return _fail("Palantir APT runtime aggregate identity is invalid")
	var resolution_value: Variant = document.get("authoredResolution", [])
	if typeof(resolution_value) != TYPE_ARRAY or (resolution_value as Array).size() != 2:
		return _fail("Palantir authored resolution is invalid")
	var resolution := resolution_value as Array
	_authored_resolution = Vector2(float(resolution[0]), float(resolution[1]))
	if not _finite_vector(_authored_resolution) or _authored_resolution.x <= 0.0 or _authored_resolution.y <= 0.0:
		return _fail("Palantir authored resolution is out of bounds")
	var policy_value: Variant = document.get("renderPolicy", {})
	if typeof(policy_value) != TYPE_DICTIONARY:
		return _fail("Palantir render policy is missing")
	var policy := policy_value as Dictionary
	var resource_value: Variant = document.get("resourceFlash", null)
	var has_resource_flash := typeof(resource_value) == TYPE_DICTIONARY and not (resource_value as Dictionary).is_empty()
	var side_command_value: Variant = document.get("sideCommandTopology", null)
	var has_side_command := typeof(side_command_value) == TYPE_DICTIONARY and not (side_command_value as Dictionary).is_empty()
	var side_fade_value: Variant = document.get("sideCommandFadeRuntime", null)
	var has_side_fade := typeof(side_fade_value) == TYPE_DICTIONARY and not (side_fade_value as Dictionary).is_empty()
	var palantir_command_value: Variant = document.get("palantirCommandTopology", null)
	var has_palantir_command := typeof(palantir_command_value) == TYPE_DICTIONARY and not (palantir_command_value as Dictionary).is_empty()
	var wnd_value: Variant = document.get("wndCompanion", null)
	var has_wnd_companion := typeof(wnd_value) == TYPE_DICTIONARY and not (wnd_value as Dictionary).is_empty()
	if (
		bool(policy.get("actionScriptExecuted", true))
		or not bool(policy.get("boundedActionScriptSubsetExecuted", false))
		or not bool(policy.get("boundedClipActionSubsetExecuted", false))
		or not bool(policy.get("boundedInitialSetupApplied", false))
		or String(policy.get("defaultRuntimeMode", "")) != "fail-closed"
		or not bool(policy.get("staticSubsetRequiresExplicitOptIn", false))
		or bool(policy.get("syntheticFallbackAllowed", true))
		or not bool(policy.get("exactTimelineDisplayLists", false))
		or bool(policy.get("timelinePlaybackBound", true))
		or not bool(policy.get("exactExternalFontLoadingBound", false))
		or not bool(policy.get("exactLiveTextBindingsBound", false))
		or not bool(policy.get("exactUnifiedDisplayOrder", false))
		or not bool(policy.get("exactExternalMovieChildSlotsBound", false))
		or bool(policy.get("exactResourceFlashActionBound", false)) != has_resource_flash
		or bool(policy.get("exactSideCommandTopologyBound", false)) != has_side_command
		or bool(policy.get("exactMenFordsSideCommandFadeRuntimeBound", false)) != has_side_fade
		or bool(policy.get("exactPalantirCommandRegistrationsBound", false)) != has_palantir_command
		or bool(policy.get("resourceFlashNativeTriggerCapturePassed", false))
		or bool(policy.get("resourceFlashMixerOverlapCapturePassed", false))
		or bool(policy.get("externalMovieLifecycleCapturePassed", true))
		or bool(policy.get("renderedTextParityCapturePassed", true))
		or bool(policy.get("exactWndCompanionBound", false)) != has_wnd_companion
		or bool(policy.get("wndLiveDispatchBound", true))
		or bool(policy.get("wndRenderServicesBound", true))
	):
		return _fail("Palantir render policy was weakened")
	if not _validate_wnd_companion(wnd_value):
		return false
	if not _validate_frame_selection(document.get("frameSelection", {})):
		return false
	if not _validate_side_command_fade_runtime(side_fade_value):
		return false
	if not _validate_external_movie_attachments(
		document.get("externalMovieLoads", []),
		document.get("externalMovieAttachments", []),
		document.get("externalMovieLifecycle", {}),
		document.get("sourceDiagnostics", {})
	):
		return false
	var action_script_ids := _validate_action_scripts(document.get("actionScripts", []))
	if action_script_ids.is_empty():
		return false
	if has_side_command and not _validate_side_command_topology(side_command_value, action_script_ids):
		return false
	if has_palantir_command and not _validate_palantir_command_topology(palantir_command_value, action_script_ids):
		return false
	var timeline_ids := _validate_timelines(
		document.get("timelines", []), document.get("timelineInstances", []), action_script_ids
	)
	if timeline_ids.is_empty():
		return false
	if not _validate_resource_flash(resource_value, timeline_ids, action_script_ids):
		return false
	var blocked_clip_action_ids := _validate_clip_actions(
		document.get("clipActionPrograms", []), document.get("clipActions", []), timeline_ids
	)
	if blocked_clip_action_ids.is_empty() and clip_action_count == 0:
		return false
	if not _validate_text_and_buttons(document, pack_root):
		return false
	var blockers_value: Variant = document.get("unsupportedSemantics", [])
	if typeof(blockers_value) != TYPE_ARRAY:
		return _fail("Palantir blocker inventory is invalid")
	blocker_count = (blockers_value as Array).size()
	var blocker_codes: Dictionary = {}
	var capture_blocker_count := 0
	var external_capture_blocker_count := 0
	var resource_trigger_blocker_count := 0
	var resource_mixer_blocker_count := 0
	var wnd_builtin_blocker_count := 0
	var wnd_draw_blocker_count := 0
	var wnd_live_blocker_count := 0
	for blocker_value in blockers_value as Array:
		if typeof(blocker_value) != TYPE_DICTIONARY or String((blocker_value as Dictionary).get("code", "")) == "":
			return _fail("Palantir blocker inventory contains an invalid entry")
		blocker_codes[String((blocker_value as Dictionary).get("code", ""))] = blocker_value
		if String((blocker_value as Dictionary).get("code", "")) == "text-rendered-parity-capture-not-passed":
			capture_blocker_count += 1
			if not _validate_text_capture_blocker(blocker_value as Dictionary):
				return _fail("Palantir rendered text capture blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "external-movie-lifecycle-capture-not-passed":
			external_capture_blocker_count += 1
			if not _validate_external_movie_capture_blocker(blocker_value as Dictionary):
				return _fail("Palantir external movie capture blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "external-movie-target-attachment-not-bound":
			return _fail("Palantir retained an obsolete external movie attachment blocker")
		if String((blocker_value as Dictionary).get("code", "")) == "resource-flash-native-trigger-capture-not-passed":
			resource_trigger_blocker_count += 1
			if not _validate_resource_flash_trigger_blocker(blocker_value as Dictionary):
				return _fail("Palantir resource-flash trigger blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "resource-flash-mixer-overlap-capture-not-passed":
			resource_mixer_blocker_count += 1
			if not _validate_resource_flash_mixer_blocker(blocker_value as Dictionary):
				return _fail("Palantir resource-flash mixer blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "wnd-layout-callbacks-not-bound":
			return _fail("Palantir retained the obsolete broad WND blocker")
		if String((blocker_value as Dictionary).get("code", "")) == "wnd-unresolved-runtime-builtins-not-bound":
			wnd_builtin_blocker_count += 1
			if not _validate_wnd_builtin_blocker(blocker_value as Dictionary):
				return _fail("Palantir WND built-in blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "wnd-dynamic-draw-service-capture-not-passed":
			wnd_draw_blocker_count += 1
			if not _validate_wnd_draw_blocker(blocker_value as Dictionary):
				return _fail("Palantir WND draw-service blocker changed")
		if String((blocker_value as Dictionary).get("code", "")) == "wnd-live-dispatch-render-services-not-bound":
			wnd_live_blocker_count += 1
			if not _validate_wnd_live_blocker(blocker_value as Dictionary):
				return _fail("Palantir WND live-binding blocker changed")
	if capture_blocker_count != 1:
		return _fail("Palantir rendered text capture blocker is missing or duplicated")
	if external_capture_blocker_count != 1:
		return _fail("Palantir external movie capture blocker is missing or duplicated")
	if resource_trigger_blocker_count != (1 if resource_flash_ready else 0):
		return _fail("Palantir resource-flash trigger blocker is missing or duplicated")
	if resource_mixer_blocker_count != (1 if resource_flash_ready else 0):
		return _fail("Palantir resource-flash mixer blocker is missing or duplicated")
	var expected_wnd_blockers := 1 if wnd_companion_ready else 0
	if wnd_builtin_blocker_count != expected_wnd_blockers or wnd_draw_blocker_count != expected_wnd_blockers or wnd_live_blocker_count != expected_wnd_blockers:
		return _fail("Palantir precise WND blockers are missing or duplicated")
	if not _validate_clip_action_blockers(blockers_value as Array, blocked_clip_action_ids):
		return false
	if (
		not blocker_codes.has("palantir-nondefault-frame-selection-not-bound")
		or not blocker_codes.has("timeline-playback-not-bound")
		or blocker_codes.has("side-command-bar-fade-runtime-not-bound")
	):
		return _fail("Palantir bounded selection blockers are missing")
	if not _validate_selection_blockers(blocker_codes, timeline_ids):
		return false
	var draws_value: Variant = document.get("draws", [])
	if typeof(draws_value) != TYPE_ARRAY:
		return _fail("Palantir draw inventory is invalid")
	var values := draws_value as Array
	if values.is_empty() or values.size() > MAX_DRAWS:
		return _fail("Palantir draw inventory is empty or exceeds bounds")
	for value in values:
		if typeof(value) != TYPE_DICTIONARY or not _validate_draw(value as Dictionary, pack_root):
			return false
	_display_items.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.displayOrder) < int(b.displayOrder))
	var summary_value: Variant = document.get("summary", {})
	if typeof(summary_value) != TYPE_DICTIONARY:
		return _fail("Palantir summary is invalid")
	var summary := summary_value as Dictionary
	if (
		int(summary.get("drawCount", -1)) != _draws.size()
		or int(summary.get("blockerCount", -1)) != blocker_count
		or int(summary.get("timelineCount", -1)) != timeline_count
		or int(summary.get("timelineFrameCount", -1)) != timeline_frame_count
		or int(summary.get("timelineInstanceCount", -1)) != timeline_instance_count
		or int(summary.get("actionScriptCount", -1)) != action_script_count
		or int(summary.get("supportedActionScriptCount", -1)) != supported_action_script_count
		or int(summary.get("unsupportedActionScriptCount", -1)) != action_script_count - supported_action_script_count
		or int(summary.get("typedSideCommandActionScriptCount", 0)) != (TYPED_SIDE_COMMAND_PROGRAMS.size() if side_command_topology_ready else 0)
		or int(summary.get("typedMenFordsSideCommandFadeRuntimeCount", 0)) != (1 if side_command_fade_runtime_ready else 0)
		or int(summary.get("typedPalantirCommandActionScriptCount", 0)) != (TYPED_PALANTIR_COMMAND_PROGRAMS.size() if palantir_command_topology_ready else 0)
		or int(summary.get("clipActionProgramCount", -1)) != clip_action_program_count
		or int(summary.get("supportedClipActionProgramCount", -1)) != supported_clip_action_program_count
		or int(summary.get("clipActionCount", -1)) != clip_action_count
		or int(summary.get("clipActionEventCount", -1)) != clip_action_event_count
		or int(summary.get("executableClipActionEventCount", -1)) != executable_clip_action_event_count
		or int(summary.get("fontCount", -1)) != font_count
		or int(summary.get("embeddedFontGlyphCount", -1)) != embedded_font_glyph_count
		or int(summary.get("textCount", -1)) != text_count
		or int(summary.get("textInstanceCount", -1)) != text_instance_count
		or int(summary.get("displayItemCount", -1)) != _display_items.size()
		or int(summary.get("buttonCount", -1)) != button_count
		or int(summary.get("buttonInstanceCount", -1)) != button_instance_count
		or int(summary.get("buttonActionCount", -1)) != button_action_count
		or int(summary.get("externalMovieLoadCount", -1)) != 5
		or int(summary.get("externalMovieAttachmentBlockerCount", -1)) != 0
		or int(summary.get("externalMovieAttachmentCount", -1)) != 4
		or int(summary.get("externalMovieLifecycleCaptureBlockerCount", -1)) != 1
		or bool(summary.get("wndCompanionBound", false)) != wnd_companion_ready
		or int(summary.get("wndTypedCallbackCount", -1)) != wnd_typed_callback_count
		or int(summary.get("wndRequiredMessageCallbackCount", -1)) != (5 if wnd_companion_ready else 0)
		or int(summary.get("wndRequiredMessageUnimplementedCount", -1)) != 0
		or int(summary.get("wndUnresolvedBuiltinCount", -1)) != (4 if wnd_companion_ready else 0)
		or bool(summary.get("staticSubsetReady", false)) != (_draws.size() > 0)
		or bool(summary.get("parityReady", true)) != (blocker_count == 0 and _draws.size() > 0)
	):
		return _fail("Palantir summary does not match its executable inventory")
	draw_count = _draws.size()
	return true


func wnd_companion_runtime() -> RefCounted:
	return _wnd_runtime


func _validate_wnd_companion(value: Variant) -> bool:
	wnd_companion_ready = false
	wnd_typed_callback_count = 0
	_staged_wnd_runtime = null
	if typeof(value) == TYPE_NIL:
		return true
	if typeof(value) != TYPE_DICTIONARY or (value as Dictionary).is_empty():
		return _fail("Palantir WND companion is invalid or empty")
	var runtime: RefCounted = WND_RUNTIME_SCRIPT.new()
	if not runtime.configure_companion_document((value as Dictionary).duplicate(true)):
		return _fail("Palantir WND companion was rejected: %s" % String(runtime.last_error))
	_staged_wnd_runtime = runtime
	wnd_companion_ready = true
	wnd_typed_callback_count = int((value as Dictionary).get("runtimeInventory", {}).get("implementedCallbackCount", -1))
	return wnd_typed_callback_count == 15


func _validate_wnd_builtin_blocker(blocker: Dictionary) -> bool:
	return (
		wnd_companion_ready
		and String(blocker.get("movie", "")) == "controlbar.wnd"
		and int(blocker.get("callbackCount", -1)) == 4
		and blocker.get("callbacks", []) == WND_RUNTIME_SCRIPT.UNRESOLVED_BUILTIN_CALLBACKS
		and not bool(blocker.get("parityReady", true))
	)


func _validate_wnd_draw_blocker(blocker: Dictionary) -> bool:
	return (
		wnd_companion_ready
		and String(blocker.get("movie", "")) == "controlbar.wnd"
		and int(blocker.get("gateCount", -1)) == 7
		and blocker.get("gates", []) == WND_RUNTIME_SCRIPT.DRAW_SERVICE_GATES
		and not bool(blocker.get("parityReady", true))
	)


func _validate_wnd_live_blocker(blocker: Dictionary) -> bool:
	return (
		wnd_companion_ready
		and String(blocker.get("movie", "")) == "controlbar.wnd"
		and int(blocker.get("messageAliasGateCount", -1)) == 7
		and blocker.get("messageAliasGates", []) == WND_RUNTIME_SCRIPT.MESSAGE_ALIAS_GATES
		and not bool(blocker.get("callbackDispatchBound", true))
		and not bool(blocker.get("renderServicesBound", true))
		and not bool(blocker.get("genericDispatchAllowed", true))
		and not bool(blocker.get("fallbackVisualsAllowed", true))
		and not bool(blocker.get("parityReady", true))
	)


func _validate_action_scripts(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_ARRAY:
		_fail("Palantir ActionScript inventory is invalid")
		return {}
	var programs := value as Array
	if programs.is_empty() or programs.size() > MAX_ACTION_SCRIPTS:
		_fail("Palantir ActionScript inventory is empty or exceeds bounds")
		return {}
	var ids: Dictionary = {}
	supported_action_script_count = 0
	for program_value in programs:
		if typeof(program_value) != TYPE_DICTIONARY:
			_fail("Palantir ActionScript program is invalid")
			return {}
		var program := program_value as Dictionary
		var script_id := String(program.get("scriptId", ""))
		var supported := bool(program.get("supported", false))
		var instructions_value: Variant = program.get("instructions", [])
		var effects_value: Variant = program.get("effects", [])
		var unsupported_value: Variant = program.get("unsupportedInstructions", [])
		if (
			script_id == ""
			or ids.has(script_id)
			or String(program.get("movie", "")) == ""
			or int(program.get("sourceOffset", -1)) < 0
			or int(program.get("instructionOffset", -1)) < 0
			or int(program.get("byteLength", 0)) <= 0
			or not _is_sha256(String(program.get("sha256", "")))
			or int(program.get("maximumStackDepth", -1)) < 0
			or int(program.get("terminalStackDepth", -1)) < 0
			or typeof(instructions_value) != TYPE_ARRAY
			or (instructions_value as Array).is_empty()
			or typeof(effects_value) != TYPE_ARRAY
			or typeof(unsupported_value) != TYPE_ARRAY
		):
			_fail("Palantir ActionScript identity or typed inventory changed")
			return {}
		if not _validate_action_instruction_rows(instructions_value as Array):
			return {}
		var typed_minlod := TYPED_MINLOD_PROGRAMS.has(script_id)
		var typed_resource_flash := script_id == String(TYPED_RESOURCE_FLASH_PROGRAM.scriptId)
		var typed_side_command := TYPED_SIDE_COMMAND_PROGRAMS.has(script_id)
		var typed_palantir_command := TYPED_PALANTIR_COMMAND_PROGRAMS.has(script_id)
		if supported:
			if (
				not (unsupported_value as Array).is_empty()
				or int(program.get("terminalStackDepth", -1)) != 0
				or (typed_minlod and not _validate_typed_minlod_program(script_id, program))
				or (typed_resource_flash and not _validate_typed_resource_flash_program(program))
				or (typed_side_command and not _validate_typed_side_command_program(script_id, program))
				or (typed_palantir_command and not _validate_typed_palantir_command_program(script_id, program))
				or (not typed_minlod and not typed_resource_flash and not typed_side_command and not typed_palantir_command and not _validate_action_effects(effects_value as Array))
			):
				_fail("Palantir supported ActionScript retained unsupported semantics: %s" % script_id)
				return {}
			supported_action_script_count += 1
		elif typed_minlod or typed_resource_flash or typed_side_command or typed_palantir_command or (unsupported_value as Array).is_empty() or not (effects_value as Array).is_empty():
			_fail("Palantir blocked ActionScript lost its exact blocker evidence")
			return {}
		ids[script_id] = true
		_action_scripts[script_id] = program
	action_script_count = programs.size()
	return ids


func _validate_action_instruction_rows(rows: Array) -> bool:
	var previous := -1
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("Palantir ActionScript instruction is invalid")
		var row := row_value as Dictionary
		var offset := int(row.get("offset", -1))
		var next_offset := int(row.get("nextOffset", -1))
		if offset <= previous or next_offset <= offset or int(row.get("opcode", -1)) < 0 or String(row.get("name", "")) == "":
			return _fail("Palantir ActionScript instruction sequence changed")
		var body_value: Variant = row.get("body", [])
		if typeof(body_value) != TYPE_ARRAY or not _validate_action_instruction_rows(body_value as Array):
			return false
		previous = offset
	return true


func _validate_action_effects(effects: Array) -> bool:
	for effect_value in effects:
		if typeof(effect_value) != TYPE_DICTIONARY:
			return _fail("Palantir ActionScript effect is invalid")
		var effect := effect_value as Dictionary
		var kind := String(effect.get("kind", ""))
		if kind in ["play", "stop"]:
			continue
		if kind != "goto" or String(effect.get("targetType", "")) not in ["frame", "label", "integer", "string"]:
			return _fail("Palantir ActionScript effect kind changed")
		if not effect.has("target"):
			return _fail("Palantir ActionScript goto target is missing")
	return true


func _validate_typed_side_command_program(script_id: String, program: Dictionary) -> bool:
	var expected_value: Variant = TYPED_SIDE_COMMAND_PROGRAMS.get(script_id, {})
	if typeof(expected_value) != TYPE_DICTIONARY or (expected_value as Dictionary).is_empty():
		return false
	var expected := expected_value as Dictionary
	return (
		bool(program.get("supported", false))
		and String(program.get("actionKind", "")) == "action-script"
		and String(program.get("movie", "")) == String(expected.movie)
		and int(program.get("sourceOffset", -1)) == int(expected.sourceOffset)
		and int(program.get("instructionOffset", -1)) == int(expected.instructionOffset)
		and int(program.get("byteLength", -1)) == int(expected.byteLength)
		and String(program.get("sha256", "")) == String(expected.sha256)
		and int(program.get("maximumStackDepth", -1)) == int(expected.maximumStackDepth)
		and int(program.get("terminalStackDepth", -1)) == 0
		and program.get("effects", []) == expected.effects
	)


func _validate_typed_palantir_command_program(script_id: String, program: Dictionary) -> bool:
	var expected_value: Variant = TYPED_PALANTIR_COMMAND_PROGRAMS.get(script_id, {})
	if typeof(expected_value) != TYPE_DICTIONARY or (expected_value as Dictionary).is_empty():
		return false
	var expected := expected_value as Dictionary
	return (
		bool(program.get("supported", false))
		and String(program.get("actionKind", "")) == "action-script"
		and String(program.get("movie", "")) == String(expected.movie)
		and int(program.get("sourceOffset", -1)) == int(expected.sourceOffset)
		and int(program.get("instructionOffset", -1)) == int(expected.instructionOffset)
		and int(program.get("byteLength", -1)) == int(expected.byteLength)
		and String(program.get("sha256", "")) == String(expected.sha256)
		and int(program.get("maximumStackDepth", -1)) == int(expected.maximumStackDepth)
		and int(program.get("terminalStackDepth", -1)) == 0
		and _validate_palantir_command_registration_effects(script_id, program.get("effects", []))
	)


func _validate_palantir_command_registration_effects(script_id: String, value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 1:
		return false
	var effect_value: Variant = (value as Array)[0]
	if typeof(effect_value) != TYPE_DICTIONARY:
		return false
	var effect := effect_value as Dictionary
	if bool(effect.get("invocationDuringRegistration", true)):
		return false
	if script_id == "palantir:169224":
		return effect.size() == 3 and String(effect.get("kind", "")) == "palantir-command-register-lifecycle-functions" and _palantir_registration_rows_match(effect.get("functions", []), PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS)
	return effect.size() == 4 and String(effect.get("kind", "")) == "palantir-command-register-button-methods" and _string_array_matches(effect.get("buttonOrder", []), ["0", "1", "2", "3", "4", "5"]) and _palantir_registration_rows_match(effect.get("methods", []), PALANTIR_COMMAND_BUTTON_METHODS)


func _palantir_registration_rows_match(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		if actual.size() != row.size():
			return false
		for key_value in row.keys():
			var key := String(key_value)
			if key in ["definitionOffset", "bodyOffset", "bodyByteLength"]:
				if int(actual.get(key, -1)) != int(row[key]):
					return false
			elif String(actual.get(key, "")) != String(row[key]):
				return false
	return true


func _validate_side_command_fade_runtime(value: Variant) -> bool:
	side_command_fade_runtime_ready = false
	_side_command_fade_state.clear()
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Men/Fords side-command FadeIn contract is missing")
	var contract := value as Dictionary
	var source_value: Variant = contract.get("source", {})
	var input_value: Variant = contract.get("typedInput", {})
	var eligibility_value: Variant = contract.get("eligibility", {})
	var timeline_value: Variant = contract.get("timeline", {})
	var native_value: Variant = contract.get("nativeStateMachine", {})
	var gates_value: Variant = contract.get("remainingTraceGates", [])
	if typeof(source_value) != TYPE_DICTIONARY or typeof(input_value) != TYPE_DICTIONARY or typeof(eligibility_value) != TYPE_DICTIONARY or typeof(timeline_value) != TYPE_DICTIONARY or typeof(native_value) != TYPE_DICTIONARY or typeof(gates_value) != TYPE_ARRAY:
		return _fail("Men/Fords side-command FadeIn contract shape is invalid")
	var source := source_value as Dictionary
	var input := input_value as Dictionary
	var eligibility := eligibility_value as Dictionary
	var timeline := timeline_value as Dictionary
	var native := native_value as Dictionary
	if (
		String(contract.get("schema", "")) != "openbfme.retail-hud-men-fords-side-fade"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("movie", "")) != "InGameSideCommandBar"
		or String(source.get("virtualPath", "")) != "InGameSideCommandBar.apt"
		or String(source.get("sha256", "")) != MEN_FORDS_SIDE_FADE_SOURCE_SHA256
		or int(source.get("byteLength", -1)) != 14082
		or source.get("retailIniSha256", {}) != MEN_FORDS_RETAIL_INI_SHA256
		or bool(contract.get("genericActionScriptVmUsed", true))
		or bool(contract.get("genericTimelinePlaybackRequired", true))
	):
		return _fail("Men/Fords side-command FadeIn source identity changed")
	if (
		String(input.get("type", "")) != "MenFordsSelectionCommandContext"
		or String(input.get("selectedIdsField", "")) != "selected_ids"
		or String(input.get("selectedStructureIdField", "")) != "selected_structure_id"
		or String(input.get("entitiesField", "")) != "entities"
		or String(input.get("structuresField", "")) != "structures"
		or String(input.get("winnerField", "")) != "winner"
		or String(input.get("localTeamField", "")) != "local_team"
		or int(input.get("localTeam", -1)) != 0
		or int(input.get("inProgressWinner", 0)) != -1
		or not bool(input.get("selectionKindsMutuallyExclusive", false))
		or not bool(input.get("selectedIdsSortedUnique", false))
	):
		return _fail("Men/Fords side-command typed input changed")
	var roster_value: Variant = eligibility.get("roster", [])
	if typeof(roster_value) != TYPE_ARRAY or (roster_value as Array).size() != MEN_FORDS_SIDE_FADE_ROSTER.size():
		return _fail("Men/Fords side-command roster is incomplete")
	var seen: Dictionary = {}
	for row_value in roster_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("Men/Fords side-command roster row is invalid")
		var row := row_value as Dictionary
		var selector := String(row.get("selectorValue", ""))
		var expected_value: Variant = MEN_FORDS_SIDE_FADE_ROSTER.get(selector, {})
		if typeof(expected_value) != TYPE_DICTIONARY or (expected_value as Dictionary).is_empty() or seen.has(selector):
			return _fail("Men/Fords side-command roster selector changed")
		var expected := expected_value as Dictionary
		var eligible_commands: Variant = row.get("inPalantirYesCommands", [])
		var multi_commands: Variant = row.get("multiSelectCommands", [])
		if (
			String(row.get("selectionKind", "")) != String(expected.kind)
			or String(row.get("selectorField", "")) != String(expected.field)
			or String(row.get("commandSet", "")) != String(expected.commandSet)
			or int(row.get("eligibleCommandCount", -1)) != int(expected.eligibleCount)
			or typeof(eligible_commands) != TYPE_ARRAY
			or (eligible_commands as Array).size() != int(expected.eligibleCount)
			or typeof(multi_commands) != TYPE_ARRAY
		):
			return _fail("Men/Fords side-command roster command mapping changed")
		if String(expected.kind) == "battalion":
			for command in MEN_FORDS_MULTI_SELECT_COMMANDS:
				if not (multi_commands as Array).has(command):
					return _fail("Men/Fords shared multi-selection command changed")
		seen[selector] = true
	if (
		eligibility.get("multiBattalionCommands", []) != MEN_FORDS_MULTI_SELECT_COMMANDS
		or int(eligibility.get("multiBattalionEligibleCommandCount", -1)) != 3
		or bool(eligibility.get("noSelectionEligible", true))
		or bool(eligibility.get("enemyDeadOrPostMatchEligible", true))
	):
		return _fail("Men/Fords side-command eligibility predicate changed")
	if (
		int(timeline.get("frameCount", -1)) != 42
		or int(timeline.get("millisecondsPerFrame", -1)) != 33
		or int(timeline.get("initialFrameZeroBased", -1)) != 0
		or int(timeline.get("fadeInStartOneBased", -1)) != 12
		or int(timeline.get("fadeInEndOneBased", -1)) != 22
		or int(timeline.get("fadeOutStartOneBased", -1)) != 32
		or int(timeline.get("fadeOutEndOneBased", -1)) != 42
	):
		return _fail("Men/Fords side-command FadeIn frame bounds changed")
	var labels_value: Variant = timeline.get("labelsZeroBased", {})
	var targets_value: Variant = timeline.get("targetExamples", {})
	if typeof(labels_value) != TYPE_DICTIONARY or typeof(targets_value) != TYPE_DICTIONARY:
		return _fail("Men/Fords side-command FadeIn labels or target math changed")
	var labels := labels_value as Dictionary
	var targets := targets_value as Dictionary
	if labels.size() != 3 or int(labels.get("_hide", -1)) != 1 or int(labels.get("_fadeIn", -1)) != 11 or int(labels.get("_fadeOut", -1)) != 31 or targets.size() != 5 or int(targets.get("31", -1)) != 12 or int(targets.get("32", -1)) != 22 or int(targets.get("37", -1)) != 17 or int(targets.get("41", -1)) != 13 or int(targets.get("42", -1)) != 12:
		return _fail("Men/Fords side-command FadeIn labels or target math changed")
	if (
		not _integer_array_matches(timeline.get("fadeInBodyRange", []), [8836, 9009])
		or String(timeline.get("fadeInBodySha256", "")) != "e360a3640690bda116ca9437e11bb4ece5f5afbe5f2f46f463facc5540a8939a"
	):
		return _fail("Men/Fords side-command FadeIn body changed")
	if (
		int(timeline.get("completionFrameOneBased", -1)) != 22
		or not _integer_array_matches(timeline.get("completionProgramRange", []), [9404, 10086])
		or String(timeline.get("completionProgramSha256", "")) != "47b0231d9b4f7952f3dba37fd2ba6f3f07914edb3a546ba63a0f873e51ef1a9c"
		or String(timeline.get("completionCallback", "")) != "OnAptInGameSideCommandBarFadeInComplete"
		or not _integer_array_matches(timeline.get("completionStateTransition", []), [2, 3])
		or int(timeline.get("settledStopFrameOneBased", -1)) != 31
		or not _integer_array_matches(timeline.get("settledStopProgramRange", []), [10088, 10090])
		or String(timeline.get("settledStopProgramSha256", "")) != "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd"
	):
		return _fail("Men/Fords side-command FadeIn completion changed")
	if (
		int(native.get("loadedState", -1)) != 1
		or int(native.get("fadingInState", -1)) != 2
		or int(native.get("settledVisibleState", -1)) != 3
		or not _integer_array_matches(native.get("dispatchOnlyOutsideStates", []), [2, 3])
		or bool(native.get("fadeOutBound", true))
		or (gates_value as Array).size() != 1
	):
		return _fail("Men/Fords side-command native state contract changed")
	var gate := (gates_value as Array)[0] as Dictionary
	if String(gate.get("id", "")) != "side-command-native-row-alias-trace" or bool(gate.get("blocksTypedGodotImplementation", true)) or not bool(gate.get("blocksExactNativeAliasParityClaim", false)):
		return _fail("Men/Fords side-command narrow trace gate changed")
	side_command_fade_runtime_ready = true
	_side_command_fade_state = _new_side_command_fade_state()
	return true


func _validate_side_command_topology(value: Variant, action_script_ids: Dictionary) -> bool:
	side_command_topology_ready = false
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Palantir side-command topology is missing")
	var topology := value as Dictionary
	var source_value: Variant = topology.get("source", {})
	var button_set_value: Variant = topology.get("buttonSet", {})
	var button_value: Variant = topology.get("button", {})
	var helper_value: Variant = topology.get("helperLibrary", {})
	var scheduling_value: Variant = topology.get("scheduling", {})
	if (
		String(topology.get("schema", "")) != "openbfme.retail-hud-side-command-topology"
		or int(topology.get("schemaVersion", -1)) != 0
		or String(topology.get("movie", "")) != "InGameSideCommandBar"
		or typeof(source_value) != TYPE_DICTIONARY
		or typeof(button_set_value) != TYPE_DICTIONARY
		or typeof(button_value) != TYPE_DICTIONARY
		or typeof(helper_value) != TYPE_DICTIONARY
		or typeof(scheduling_value) != TYPE_DICTIONARY
		or int(topology.get("unresolvedRuntimeTraceCount", -1)) != 0
	):
		return _fail("Palantir side-command topology identity changed")
	var source := source_value as Dictionary
	if (
		source.size() != 3
		or String(source.get("virtualPath", "")) != "InGameSideCommandBar.apt"
		or int(source.get("byteLength", -1)) != 14082
		or String(source.get("sha256", "")) != "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"
	):
		return _fail("Palantir side-command source identity changed")
	var local_buttons: Array[Dictionary] = []
	for index in range(12):
		local_buttons.append({
			"name": "Button%d" % index, "depth": index * 10 + 1,
			"characterId": 18, "sourceOffset": 7304 + index * 64,
		})
	var show_targets: Array[String] = []
	for index in range(1, 16):
		show_targets.append("Button%d" % index)
	var button_set := button_set_value as Dictionary
	if (
		int(button_set.get("characterId", -1)) != 21
		or not _side_command_local_buttons_match(button_set.get("localButtons", []), local_buttons)
		or not _string_array_matches(button_set.get("showTargets", []), show_targets)
		or not _string_array_matches(button_set.get("staticallyAbsentShowTargets", []), ["Button12", "Button13", "Button14", "Button15"])
		or String(button_set.get("missingTargetEffect", "")) != "ordered-no-op"
	):
		return _fail("Palantir side-command ButtonSet topology changed")
	var button := button_value as Dictionary
	if (
		int(button.get("characterId", -1)) != 18
		or not _integer_dictionary_matches(button.get("labels", {}), {"_hide": 0, "_show": 10})
		or not _side_command_placements_match(button.get("showFramePlacementsBeforeQueuedActions", []), [
			{"sourceOffset": 6392, "depth": 3, "characterId": 16, "name": "Frame"},
			{"sourceOffset": 6456, "depth": 9, "characterId": 17, "name": "ButtonGlass"},
		])
	):
		return _fail("Palantir side-command button frame topology changed")
	var helper := helper_value as Dictionary
	if (
		String(helper.get("scriptId", "")) != "ingamesidecommandbar:6264"
		or int(helper.get("instructionOffset", -1)) != 10956
		or int(helper.get("byteLength", -1)) != 996
		or String(helper.get("sha256", "")) != "abcf2a697a9852b4b61c07de74f7e4151bed6cd467ebd98bb0eb74e17833fa16"
		or not _side_command_helpers_match(helper.get("functions", []))
		or String(helper.get("frameVisiblePredicate", "")) != "neighbor != undefined && neighbor.Frame != undefined"
		or not _side_command_truth_table_matches(helper.get("truthTable", []), [
			{"nextFrameVisible": false, "priorFrameVisible": false, "label": "_topbottom"},
			{"nextFrameVisible": true, "priorFrameVisible": false, "label": "_top"},
			{"nextFrameVisible": false, "priorFrameVisible": true, "label": "_bottom"},
			{"nextFrameVisible": true, "priorFrameVisible": true, "label": "_middle"},
		])
		or not _string_array_matches(helper.get("neighborUpdateOrder", []), ["next", "prior"])
	):
		return _fail("Palantir side-command helper contract changed")
	var typed_ids := TYPED_SIDE_COMMAND_PROGRAMS.keys()
	typed_ids.sort()
	if not _string_array_matches(topology.get("typedScriptIds", []), typed_ids):
		return _fail("Palantir side-command typed script inventory changed")
	for script_id in typed_ids:
		if not action_script_ids.has(String(script_id)):
			return _fail("Palantir side-command typed script is missing")
	var scheduling := scheduling_value as Dictionary
	if (
		not bool(scheduling.get("sameFramePlacementsBeforeQueuedActions", false))
		or bool(scheduling.get("genericActionScriptVmUsed", true))
	):
		return _fail("Palantir side-command frame scheduling changed")
	side_command_topology_ready = true
	return true


func _validate_palantir_command_topology(value: Variant, action_script_ids: Dictionary) -> bool:
	palantir_command_topology_ready = false
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Palantir command topology is missing")
	var topology := value as Dictionary
	var source_value: Variant = topology.get("source", {})
	var command_value: Variant = topology.get("commandButtons", {})
	var collections_value: Variant = topology.get("collections", {})
	var scheduling_value: Variant = topology.get("scheduling", {})
	if (
		String(topology.get("schema", "")) != "openbfme.retail-hud-palantir-command-topology"
		or int(topology.get("schemaVersion", -1)) != 0
		or String(topology.get("movie", "")) != "Palantir"
		or typeof(source_value) != TYPE_DICTIONARY
		or typeof(command_value) != TYPE_DICTIONARY
		or typeof(collections_value) != TYPE_DICTIONARY
		or typeof(scheduling_value) != TYPE_DICTIONARY
		or int(topology.get("unresolvedRuntimeTraceCount", -1)) != 2
	):
		return _fail("Palantir command topology identity changed")
	var source := source_value as Dictionary
	if (
		source.size() != 3
		or String(source.get("virtualPath", "")) != "Palantir.apt"
		or int(source.get("byteLength", -1)) != 378173
		or String(source.get("sha256", "")) != "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140"
	):
		return _fail("Palantir command source identity changed")
	var expected_root := [
		{"sourceOffset": 96392, "depth": 17, "characterId": 86, "name": "CommandUI", "translation": [287.6000061035156, 660.0]},
		{"sourceOffset": 96776, "depth": 44, "characterId": 114, "name": "CommandButtons", "translation": [289.54998779296875, 659.8499755859375]},
		{"sourceOffset": 96840, "depth": 90, "characterId": 122, "name": "AutoAbilityOverlays", "translation": [287.6000061035156, 660.0]},
	]
	var expected_imports := [
		{"localCharacterId": 106, "movie": "libInGameUI", "symbol": "CommandButtonSubMenu"},
		{"localCharacterId": 108, "movie": "libInGameUI", "symbol": "MovieClipFrame"},
		{"localCharacterId": 110, "movie": "libInGameUI", "symbol": "ButtonGlass"},
		{"localCharacterId": 111, "movie": "libInGameUI", "symbol": "CommandButtonToggleFlash"},
	]
	if not _palantir_command_root_matches(topology.get("root", []), expected_root) or not _palantir_command_imports_match(topology.get("imports", []), expected_imports):
		return _fail("Palantir command root or import topology changed")
	var expected_source_order := [
		{"kind": "action-script", "sourceOffset": 169256},
		{"kind": "frame-label", "sourceOffset": 169264},
	]
	for source_offset in range(169280, 170689, 64):
		expected_source_order.append({"kind": "place-object", "sourceOffset": source_offset})
	var expected_placements := [
		{"sourceOffset": 169280, "depth": 1, "characterId": 106, "name": "subMenu0"},
		{"sourceOffset": 169344, "depth": 3, "characterId": 106, "name": "subMenu1"},
		{"sourceOffset": 169408, "depth": 5, "characterId": 106, "name": "subMenu2"},
		{"sourceOffset": 169472, "depth": 7, "characterId": 106, "name": "subMenu3"},
		{"sourceOffset": 169536, "depth": 9, "characterId": 107, "name": ""},
		{"sourceOffset": 169600, "depth": 11, "characterId": 108, "name": "1"},
		{"sourceOffset": 169664, "depth": 13, "characterId": 108, "name": "2"},
		{"sourceOffset": 169728, "depth": 15, "characterId": 108, "name": "3"},
		{"sourceOffset": 169792, "depth": 17, "characterId": 108, "name": "4"},
		{"sourceOffset": 169856, "depth": 19, "characterId": 108, "name": "5"},
		{"sourceOffset": 169920, "depth": 21, "characterId": 108, "name": "0"},
		{"sourceOffset": 169984, "depth": 23, "characterId": 109, "name": ""},
		{"sourceOffset": 170048, "depth": 25, "characterId": 110, "name": "glass0"},
		{"sourceOffset": 170112, "depth": 27, "characterId": 110, "name": "glass5"},
		{"sourceOffset": 170176, "depth": 29, "characterId": 110, "name": "glass4"},
		{"sourceOffset": 170240, "depth": 31, "characterId": 110, "name": "glass3"},
		{"sourceOffset": 170304, "depth": 33, "characterId": 110, "name": "glass2"},
		{"sourceOffset": 170368, "depth": 35, "characterId": 110, "name": "glass1"},
		{"sourceOffset": 170432, "depth": 37, "characterId": 111, "name": "toggleFlash0"},
		{"sourceOffset": 170496, "depth": 39, "characterId": 111, "name": "toggleFlash1"},
		{"sourceOffset": 170560, "depth": 41, "characterId": 111, "name": "toggleFlash2"},
		{"sourceOffset": 170624, "depth": 43, "characterId": 111, "name": "toggleFlash3"},
		{"sourceOffset": 170688, "depth": 45, "characterId": 113, "name": "FlashEffects"},
	]
	var command := command_value as Dictionary
	if (
		int(command.get("characterId", -1)) != 114
		or not _integer_dictionary_matches(command.get("labels", {}), {"_hide": 0, "_show": 9})
		or String(command.get("declarationAction", "")) != "palantir:169224"
		or String(command.get("showAction", "")) != "palantir:169256"
		or not _palantir_command_source_order_matches(command.get("showSourceOrder", []), expected_source_order)
		or not _side_command_placements_match(command.get("showPlacements", []), expected_placements)
		or not _string_array_matches(command.get("numericButtonFrames", []), ["1", "2", "3", "4", "5", "0"])
		or not _string_array_matches(command.get("glassTargets", []), ["glass0", "glass1", "glass2", "glass3", "glass4", "glass5"])
		or not _string_array_matches(command.get("toggleFlashTargets", []), ["toggleFlash0", "toggleFlash1", "toggleFlash2", "toggleFlash3"])
	):
		return _fail("Palantir command authored placements changed")
	var collections := collections_value as Dictionary
	var flash_value: Variant = collections.get("flashEffects", {})
	var overlays_value: Variant = collections.get("autoAbilityOverlays", {})
	if typeof(flash_value) != TYPE_DICTIONARY or typeof(overlays_value) != TYPE_DICTIONARY:
		return _fail("Palantir command target collections are missing")
	var expected_flash_children: Array[Dictionary] = []
	var expected_overlay_children: Array[Dictionary] = []
	for index in range(6):
		expected_flash_children.append({"sourceOffset": 168840 + index * 64, "depth": index * 2 + 1, "characterId": 112, "name": str(index)})
		expected_overlay_children.append({"sourceOffset": 233848 + index * 64, "depth": index * 2 + 1, "characterId": 121, "name": str((index + 1) % 6)})
	var flash := flash_value as Dictionary
	var overlays := overlays_value as Dictionary
	if (
		int(flash.get("characterId", -1)) != 113
		or String(flash.get("placementName", "")) != "FlashEffects"
		or not _side_command_placements_match(flash.get("children", []), expected_flash_children)
		or int(overlays.get("characterId", -1)) != 122
		or String(overlays.get("placementName", "")) != "AutoAbilityOverlays"
		or not _side_command_placements_match(overlays.get("children", []), expected_overlay_children)
	):
		return _fail("Palantir command target collection topology changed")
	if not _palantir_registration_rows_match(topology.get("lifecycleFunctions", []), PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS) or not _palantir_registration_rows_match(topology.get("buttonMethods", []), PALANTIR_COMMAND_BUTTON_METHODS):
		return _fail("Palantir command typed registration metadata changed")
	var typed_ids := TYPED_PALANTIR_COMMAND_PROGRAMS.keys()
	typed_ids.sort()
	if not _string_array_matches(topology.get("typedScriptIds", []), typed_ids):
		return _fail("Palantir command typed script inventory changed")
	for script_id in typed_ids:
		if not action_script_ids.has(String(script_id)):
			return _fail("Palantir command typed script is missing")
	var expected_gates := [
		{"id": "skill-upgrade-root-method-effects", "programId": "palantir:167296", "scenario": "enter CommandUI _show once while InGame is true"},
		{"id": "command-child-lifecycle-host-result", "programId": "palantir:169224", "scenario": "one CommandButtons show-hide cycle"},
	]
	var scheduling := scheduling_value as Dictionary
	if (
		topology.get("remainingTraceGates", []) != expected_gates
		or String(scheduling.get("rawActionRecordEffect", "")) != "deferred"
		or String(scheduling.get("rawPlacementRecordEffect", "")) != "immediate"
		or not bool(scheduling.get("sameFramePlacementsBeforeQueuedActions", false))
		or bool(scheduling.get("genericActionScriptVmUsed", true))
	):
		return _fail("Palantir command trace gate or frame scheduling changed")
	palantir_command_topology_ready = true
	return true


func _palantir_command_root_matches(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		var translation_value: Variant = actual.get("translation", [])
		if (
			actual.size() != 5
			or int(actual.get("sourceOffset", -1)) != int(row.sourceOffset)
			or int(actual.get("depth", -1)) != int(row.depth)
			or int(actual.get("characterId", -1)) != int(row.characterId)
			or String(actual.get("name", "")) != String(row.name)
			or typeof(translation_value) != TYPE_ARRAY
			or (translation_value as Array).size() != 2
			or float((translation_value as Array)[0]) != float(row.translation[0])
			or float((translation_value as Array)[1]) != float(row.translation[1])
		):
			return false
	return true


func _palantir_command_imports_match(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		if actual.size() != 3 or int(actual.get("localCharacterId", -1)) != int(row.localCharacterId) or String(actual.get("movie", "")) != String(row.movie) or String(actual.get("symbol", "")) != String(row.symbol):
			return false
	return true


func _palantir_command_source_order_matches(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		if actual.size() != 2 or String(actual.get("kind", "")) != String(row.kind) or int(actual.get("sourceOffset", -1)) != int(row.sourceOffset):
			return false
	return true


func _string_array_matches(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		if String((value as Array)[index]) != String(expected[index]):
			return false
	return true


func _side_command_local_buttons_match(value: Variant, expected: Array[Dictionary]) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index]
		if actual.size() != 4 or String(actual.get("name", "")) != String(row.name) or int(actual.get("depth", -1)) != int(row.depth) or int(actual.get("characterId", -1)) != int(row.characterId) or int(actual.get("sourceOffset", -1)) != int(row.sourceOffset):
			return false
	return true


func _side_command_placements_match(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		if actual.size() != 4 or String(actual.get("name", "")) != String(row.name) or int(actual.get("depth", -1)) != int(row.depth) or int(actual.get("characterId", -1)) != int(row.characterId) or int(actual.get("sourceOffset", -1)) != int(row.sourceOffset):
			return false
	return true


func _side_command_helpers_match(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != SIDE_COMMAND_HELPERS.size():
		return false
	for index in SIDE_COMMAND_HELPERS.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := SIDE_COMMAND_HELPERS[index] as Dictionary
		if actual.size() != 5 or String(actual.get("name", "")) != String(row.name) or int(actual.get("definitionOffset", -1)) != int(row.definitionOffset) or int(actual.get("bodyOffset", -1)) != int(row.bodyOffset) or int(actual.get("bodyByteLength", -1)) != int(row.bodyByteLength) or String(actual.get("bodySha256", "")) != String(row.bodySha256):
			return false
	return true


func _side_command_truth_table_matches(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	for index in expected.size():
		var actual_value: Variant = (value as Array)[index]
		if typeof(actual_value) != TYPE_DICTIONARY:
			return false
		var actual := actual_value as Dictionary
		var row := expected[index] as Dictionary
		if actual.size() != 3 or typeof(actual.get("nextFrameVisible")) != TYPE_BOOL or typeof(actual.get("priorFrameVisible")) != TYPE_BOOL or bool(actual.nextFrameVisible) != bool(row.nextFrameVisible) or bool(actual.priorFrameVisible) != bool(row.priorFrameVisible) or String(actual.get("label", "")) != String(row.label):
			return false
	return true


func make_side_command_state() -> Dictionary:
	if not side_command_topology_ready:
		return {}
	var buttons := {}
	for index in range(12):
		var name := "Button%d" % index
		buttons[name] = {"name": name, "frame": 0, "label": "_hide", "children": {}}
	return {"buttons": buttons, "dispatchLog": []}


func _new_side_command_fade_state() -> Dictionary:
	return {
		"nativeState": 1,
		"currentFrameOneBased": 1,
		"label": "initial-hidden",
		"playing": false,
		"visible": false,
		"eligible": false,
		"eligibleCommandCount": 0,
		"completionDispatched": false,
		"dispatchLog": [],
	}


func side_command_fade_state() -> Dictionary:
	return _side_command_fade_state.duplicate(true)


func side_command_fade_target(current_frame_one_based: int) -> int:
	if current_frame_one_based < 1 or current_frame_one_based > 42:
		return -1
	if current_frame_one_based >= 32 and current_frame_one_based < 42:
		return 12 + 42 - current_frame_one_based
	return 12


func evaluate_men_fords_selection(context: Dictionary) -> Dictionary:
	for field in ["selected_ids", "selected_structure_id", "entities", "structures", "winner", "local_team"]:
		if not context.has(field):
			return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "missing-%s" % field}
	var selected_value: Variant = context.get("selected_ids", null)
	var entities_value: Variant = context.get("entities", null)
	var structures_value: Variant = context.get("structures", null)
	if typeof(selected_value) != TYPE_ARRAY or typeof(entities_value) != TYPE_DICTIONARY or typeof(structures_value) != TYPE_DICTIONARY:
		return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "invalid-container-shape"}
	if typeof(context.get("selected_structure_id")) != TYPE_INT or typeof(context.get("winner")) != TYPE_INT or typeof(context.get("local_team")) != TYPE_INT:
		return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "invalid-scalar-shape"}
	if int(context.local_team) != 0:
		return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "local-team-changed"}
	var selected_ids: Array[int] = []
	var seen: Dictionary = {}
	for id_value in selected_value as Array:
		if typeof(id_value) != TYPE_INT or int(id_value) <= 0 or seen.has(int(id_value)):
			return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "selected-ids-not-positive-unique"}
		selected_ids.append(int(id_value))
		seen[int(id_value)] = true
	var sorted_ids := selected_ids.duplicate()
	sorted_ids.sort()
	if selected_ids != sorted_ids:
		return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "selected-ids-not-sorted"}
	var selected_structure_id := int(context.selected_structure_id)
	if selected_structure_id < 0 or (selected_structure_id != 0 and not selected_ids.is_empty()):
		return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "selection-kinds-not-exclusive"}
	if selected_structure_id == 0 and selected_ids.is_empty():
		return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "no-selection"}
	if int(context.winner) != -1:
		return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "post-match"}
	var entities := entities_value as Dictionary
	var structures := structures_value as Dictionary
	if selected_structure_id != 0:
		var row_value: Variant = structures.get(selected_structure_id, null)
		if typeof(row_value) != TYPE_DICTIONARY:
			return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "selected-structure-missing"}
		var row := row_value as Dictionary
		var selector := String(row.get("structure_kind", ""))
		var roster_value: Variant = MEN_FORDS_SIDE_FADE_ROSTER.get(selector, {})
		if typeof(roster_value) != TYPE_DICTIONARY or String((roster_value as Dictionary).get("kind", "")) != "structure":
			return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "structure-outside-declared-roster"}
		if int(row.get("team", -1)) != 0 or int(row.get("health", 0)) <= 0:
			return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "structure-not-living-local"}
		var count := int((roster_value as Dictionary).eligibleCount)
		return {"valid": true, "eligible": count > 0, "eligibleCommandCount": count, "reason": "eligible-structure", "selector": selector}
	for id in selected_ids:
		var row_value: Variant = entities.get(id, null)
		if typeof(row_value) != TYPE_DICTIONARY:
			return {"valid": false, "eligible": false, "eligibleCommandCount": 0, "reason": "selected-battalion-missing"}
		var row := row_value as Dictionary
		var selector := String(row.get("unit_type", ""))
		var roster_value: Variant = MEN_FORDS_SIDE_FADE_ROSTER.get(selector, {})
		if typeof(roster_value) != TYPE_DICTIONARY or String((roster_value as Dictionary).get("kind", "")) != "battalion":
			return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "battalion-outside-declared-roster"}
		if int(row.get("team", -1)) != 0 or int(row.get("health", 0)) <= 0:
			return {"valid": true, "eligible": false, "eligibleCommandCount": 0, "reason": "battalion-not-living-local"}
	if selected_ids.size() > 1:
		return {"valid": true, "eligible": true, "eligibleCommandCount": 3, "reason": "eligible-mixed-battalions"}
	var single := entities[selected_ids[0]] as Dictionary
	var single_roster := MEN_FORDS_SIDE_FADE_ROSTER[String(single.unit_type)] as Dictionary
	var count := int(single_roster.eligibleCount)
	return {"valid": true, "eligible": count > 0, "eligibleCommandCount": count, "reason": "eligible-battalion", "selector": String(single.unit_type)}


func sync_men_fords_selection(context: Dictionary) -> bool:
	if not side_command_fade_runtime_ready or _side_command_fade_state.is_empty():
		return false
	var result := evaluate_men_fords_selection(context)
	if not bool(result.get("valid", false)):
		side_command_fade_eligible = false
		diagnostics.append({"code": "men-fords-side-fade-input-rejected", "reason": String(result.get("reason", "invalid"))})
		_set_runtime_metadata()
		return false
	side_command_fade_eligible = bool(result.get("eligible", false))
	_side_command_fade_state.eligible = side_command_fade_eligible
	_side_command_fade_state.eligibleCommandCount = int(result.get("eligibleCommandCount", 0))
	if side_command_fade_eligible and not int(_side_command_fade_state.nativeState) in [2, 3]:
		var target := side_command_fade_target(int(_side_command_fade_state.currentFrameOneBased))
		if target < 0:
			return false
		_side_command_fade_state.currentFrameOneBased = target
		_side_command_fade_state.label = "_fadeIn" if target == 12 else "fade-in-reversal"
		_side_command_fade_state.playing = true
		_side_command_fade_state.visible = true
		_side_command_fade_state.completionDispatched = false
		_side_command_fade_state.nativeState = 2
		(_side_command_fade_state.dispatchLog as Array).append({"kind": "FadeIn", "targetFrameOneBased": target, "nativeState": 2})
		_side_command_fade_accumulator = 0.0
	_set_runtime_metadata()
	return true


func advance_side_command_fade_frame() -> bool:
	if not side_command_fade_runtime_ready or _side_command_fade_state.is_empty():
		return false
	if not bool(_side_command_fade_state.get("playing", false)):
		return true
	var current := int(_side_command_fade_state.get("currentFrameOneBased", -1))
	if current < 12 or current > 31:
		return false
	if current == 22 and not bool(_side_command_fade_state.completionDispatched):
		if int(_side_command_fade_state.nativeState) != 2:
			return false
		_side_command_fade_state.nativeState = 3
		_side_command_fade_state.completionDispatched = true
		_side_command_fade_state.label = "settled-visible"
		(_side_command_fade_state.dispatchLog as Array).append({"kind": "OnAptInGameSideCommandBarFadeInComplete", "frameOneBased": 22, "nativeState": 3})
	if current == 31:
		_side_command_fade_state.playing = false
		_side_command_fade_state.label = "settled-stop"
		(_side_command_fade_state.dispatchLog as Array).append({"kind": "Stop", "frameOneBased": 31})
	else:
		_side_command_fade_state.currentFrameOneBased = current + 1
	_set_runtime_metadata()
	return true


func _side_command_button_index(name: String) -> int:
	if not name.begins_with("Button"):
		return -1
	var suffix := name.substr(6)
	return int(suffix) if suffix.is_valid_int() else -1


func _side_command_has_frame(buttons: Dictionary, name: String) -> bool:
	var value: Variant = buttons.get(name, null)
	if typeof(value) != TYPE_DICTIONARY:
		return false
	var children_value: Variant = (value as Dictionary).get("children", {})
	return typeof(children_value) == TYPE_DICTIONARY and (children_value as Dictionary).has("Frame")


func _side_command_update_frame_state(button_set: Dictionary, name: String) -> bool:
	var buttons_value: Variant = button_set.get("buttons", {})
	var log_value: Variant = button_set.get("dispatchLog", [])
	if typeof(buttons_value) != TYPE_DICTIONARY or typeof(log_value) != TYPE_ARRAY:
		return false
	var buttons := buttons_value as Dictionary
	var button_value: Variant = buttons.get(name, null)
	if typeof(button_value) != TYPE_DICTIONARY:
		return false
	var button := button_value as Dictionary
	var children_value: Variant = button.get("children", {})
	if typeof(children_value) != TYPE_DICTIONARY:
		return false
	var children := children_value as Dictionary
	if not children.has("Frame"):
		return true
	var index := _side_command_button_index(name)
	if index < 0:
		return false
	var next_visible := _side_command_has_frame(buttons, "Button%d" % (index + 1))
	var prior_visible := _side_command_has_frame(buttons, "Button%d" % (index - 1))
	var label := "_topbottom"
	if next_visible and prior_visible:
		label = "_middle"
	elif next_visible:
		label = "_top"
	elif prior_visible:
		label = "_bottom"
	var frame_value: Variant = children.get("Frame", {})
	if typeof(frame_value) != TYPE_DICTIONARY:
		return false
	var frame := frame_value as Dictionary
	frame["label"] = label
	frame["playing"] = true
	children["Frame"] = frame
	button["children"] = children
	buttons[name] = button
	button_set["buttons"] = buttons
	var dispatch_log := log_value as Array
	dispatch_log.append({"kind": "update-frame", "target": name, "label": label})
	button_set["dispatchLog"] = dispatch_log
	return true


func _side_command_update_neighbors(button_set: Dictionary, name: String) -> bool:
	var index := _side_command_button_index(name)
	var buttons_value: Variant = button_set.get("buttons", {})
	if index < 0 or typeof(buttons_value) != TYPE_DICTIONARY:
		return false
	var buttons := buttons_value as Dictionary
	for neighbor_index in [index + 1, index - 1]:
		var neighbor := "Button%d" % neighbor_index
		if buttons.has(neighbor) and not _side_command_update_frame_state(button_set, neighbor):
			return false
	return true


func _side_command_enter_show_frame(button_set: Dictionary, name: String) -> bool:
	var buttons_value: Variant = button_set.get("buttons", {})
	var log_value: Variant = button_set.get("dispatchLog", [])
	if typeof(buttons_value) != TYPE_DICTIONARY or typeof(log_value) != TYPE_ARRAY:
		return false
	var buttons := buttons_value as Dictionary
	var button_value: Variant = buttons.get(name, null)
	if typeof(button_value) != TYPE_DICTIONARY:
		return false
	var button := button_value as Dictionary
	var children_value: Variant = button.get("children", {})
	if typeof(children_value) != TYPE_DICTIONARY:
		return false
	var children := children_value as Dictionary
	var dispatch_log := log_value as Array
	children["Frame"] = {"depth": 3, "characterId": 16, "label": "", "playing": true}
	dispatch_log.append({"kind": "place", "target": name, "child": "Frame", "depth": 3})
	children["ButtonGlass"] = {"depth": 9, "characterId": 17}
	dispatch_log.append({"kind": "place", "target": name, "child": "ButtonGlass", "depth": 9})
	button["children"] = children
	button["frame"] = 10
	button["label"] = "_show"
	buttons[name] = button
	button_set["buttons"] = buttons
	button_set["dispatchLog"] = dispatch_log
	return _side_command_update_frame_state(button_set, name) and _side_command_update_neighbors(button_set, name)


func _execute_typed_side_command_effect(
	script_id: String, program: Dictionary, timeline_state: Dictionary, runtime_inputs: Dictionary
) -> bool:
	if not side_command_topology_ready or not _validate_typed_side_command_program(script_id, program):
		return false
	if script_id == "ingamesidecommandbar:7296":
		if not runtime_inputs.has("InGame") or typeof(runtime_inputs.InGame) != TYPE_BOOL:
			return false
		if not bool(runtime_inputs.InGame):
			return true
		var buttons_value: Variant = timeline_state.get("buttons", {})
		var log_value: Variant = timeline_state.get("dispatchLog", [])
		if typeof(buttons_value) != TYPE_DICTIONARY or typeof(log_value) != TYPE_ARRAY:
			return false
		var buttons := buttons_value as Dictionary
		for index in range(1, 16):
			var name := "Button%d" % index
			var dispatch_log := timeline_state.get("dispatchLog", []) as Array
			dispatch_log.append({"kind": "show-call", "target": name, "present": buttons.has(name)})
			timeline_state["dispatchLog"] = dispatch_log
			if buttons.has(name) and not _side_command_enter_show_frame(timeline_state, name):
				return false
		return true
	var button_set_value: Variant = runtime_inputs.get("buttonSet", null)
	if typeof(button_set_value) != TYPE_DICTIONARY:
		return false
	var name := String(timeline_state.get("name", ""))
	if script_id == "ingamesidecommandbar:6272":
		return _side_command_update_neighbors(button_set_value as Dictionary, name)
	return (
		_side_command_update_frame_state(button_set_value as Dictionary, name)
		and _side_command_update_neighbors(button_set_value as Dictionary, name)
	)


func make_palantir_command_state() -> Dictionary:
	if not palantir_command_topology_ready:
		return {}
	var overlays := {}
	for index in range(6):
		overlays[str(index)] = {"name": str(index), "label": "", "playing": false}
	return {
		"root": {"AutoAbilityOverlays": {"children": overlays}},
		"children": {},
		"lifecycleFunctions": {},
		"registrationLog": [],
		"dispatchLog": [],
	}


func _execute_typed_palantir_command_effect(script_id: String, program: Dictionary, timeline_state: Dictionary) -> bool:
	if not palantir_command_topology_ready or not _validate_typed_palantir_command_program(script_id, program):
		return false
	var registration_value: Variant = timeline_state.get("registrationLog", [])
	var dispatch_value: Variant = timeline_state.get("dispatchLog", [])
	if typeof(registration_value) != TYPE_ARRAY or typeof(dispatch_value) != TYPE_ARRAY:
		return false
	var registration_log := registration_value as Array
	if script_id == "palantir:169224":
		var functions_value: Variant = timeline_state.get("lifecycleFunctions", {})
		if typeof(functions_value) != TYPE_DICTIONARY:
			return false
		var functions := functions_value as Dictionary
		for specification_value in PALANTIR_COMMAND_LIFECYCLE_FUNCTIONS:
			var specification := specification_value as Dictionary
			var name := String(specification.name)
			functions[name] = specification.duplicate(true)
			registration_log.append({"kind": "register-lifecycle-function", "name": name})
		timeline_state["lifecycleFunctions"] = functions
		timeline_state["registrationLog"] = registration_log
		return true
	var children_value: Variant = timeline_state.get("children", {})
	if typeof(children_value) != TYPE_DICTIONARY:
		return false
	var children := children_value as Dictionary
	for index in range(6):
		var name := str(index)
		var button_value: Variant = children.get(name, null)
		if typeof(button_value) != TYPE_DICTIONARY:
			return false
		var button := button_value as Dictionary
		var methods_value: Variant = button.get("methods", {})
		if typeof(methods_value) != TYPE_DICTIONARY:
			return false
		var methods := methods_value as Dictionary
		for specification_value in PALANTIR_COMMAND_BUTTON_METHODS:
			var specification := specification_value as Dictionary
			var method_name := String(specification.name)
			methods[method_name] = specification.duplicate(true)
			registration_log.append({"kind": "register-button-method", "button": name, "method": method_name})
		button["methods"] = methods
		children[name] = button
	timeline_state["children"] = children
	timeline_state["registrationLog"] = registration_log
	return true


func enter_palantir_command_show_frame(timeline_state: Dictionary) -> bool:
	if not palantir_command_topology_ready:
		return false
	var children_value: Variant = timeline_state.get("children", {})
	var log_value: Variant = timeline_state.get("registrationLog", [])
	if typeof(children_value) != TYPE_DICTIONARY or typeof(log_value) != TYPE_ARRAY:
		return false
	var children := children_value as Dictionary
	var registration_log := log_value as Array
	var placements := [
		{"name": "subMenu0", "depth": 1, "characterId": 106},
		{"name": "subMenu1", "depth": 3, "characterId": 106},
		{"name": "subMenu2", "depth": 5, "characterId": 106},
		{"name": "subMenu3", "depth": 7, "characterId": 106},
		{"name": "", "depth": 9, "characterId": 107},
		{"name": "1", "depth": 11, "characterId": 108},
		{"name": "2", "depth": 13, "characterId": 108},
		{"name": "3", "depth": 15, "characterId": 108},
		{"name": "4", "depth": 17, "characterId": 108},
		{"name": "5", "depth": 19, "characterId": 108},
		{"name": "0", "depth": 21, "characterId": 108},
		{"name": "", "depth": 23, "characterId": 109},
		{"name": "glass0", "depth": 25, "characterId": 110},
		{"name": "glass5", "depth": 27, "characterId": 110},
		{"name": "glass4", "depth": 29, "characterId": 110},
		{"name": "glass3", "depth": 31, "characterId": 110},
		{"name": "glass2", "depth": 33, "characterId": 110},
		{"name": "glass1", "depth": 35, "characterId": 110},
		{"name": "toggleFlash0", "depth": 37, "characterId": 111},
		{"name": "toggleFlash1", "depth": 39, "characterId": 111},
		{"name": "toggleFlash2", "depth": 41, "characterId": 111},
		{"name": "toggleFlash3", "depth": 43, "characterId": 111},
		{"name": "FlashEffects", "depth": 45, "characterId": 113},
	]
	for placement_value in placements:
		var placement := placement_value as Dictionary
		var name := String(placement.name)
		registration_log.append({"kind": "place", "name": name, "depth": int(placement.depth)})
		if name == "":
			continue
		var clip := {"name": name, "depth": int(placement.depth), "characterId": int(placement.characterId), "label": "", "playing": false}
		if name.is_valid_int():
			clip["methods"] = {}
		elif name == "FlashEffects":
			var flash_children := {}
			for index in range(6):
				flash_children[str(index)] = {"name": str(index), "label": "", "playing": false}
			clip["children"] = flash_children
		children[name] = clip
	timeline_state["children"] = children
	timeline_state["registrationLog"] = registration_log
	return execute_action_script("palantir:169256", timeline_state)


func execute_palantir_command_button_method(timeline_state: Dictionary, button_name: String, method_name: String, state_label: String) -> bool:
	if not palantir_command_topology_ready or button_name not in ["0", "1", "2", "3", "4", "5"]:
		return false
	var children_value: Variant = timeline_state.get("children", {})
	var root_value: Variant = timeline_state.get("root", {})
	var log_value: Variant = timeline_state.get("dispatchLog", [])
	if typeof(children_value) != TYPE_DICTIONARY or typeof(root_value) != TYPE_DICTIONARY or typeof(log_value) != TYPE_ARRAY:
		return false
	var children := children_value as Dictionary
	var button_value: Variant = children.get(button_name, null)
	if typeof(button_value) != TYPE_DICTIONARY:
		return false
	var methods_value: Variant = (button_value as Dictionary).get("methods", {})
	if typeof(methods_value) != TYPE_DICTIONARY or not (methods_value as Dictionary).has(method_name):
		return false
	var target_value: Variant = null
	var target_path := ""
	if method_name == "SetAutoAbilityOverlayState":
		var overlays_value: Variant = (root_value as Dictionary).get("AutoAbilityOverlays", {})
		if typeof(overlays_value) != TYPE_DICTIONARY:
			return false
		var overlay_children_value: Variant = (overlays_value as Dictionary).get("children", {})
		if typeof(overlay_children_value) != TYPE_DICTIONARY:
			return false
		target_value = (overlay_children_value as Dictionary).get(button_name, null)
		target_path = "AutoAbilityOverlays/%s" % button_name
	elif method_name == "SetFlashEffectState":
		var flash_value: Variant = children.get("FlashEffects", null)
		if typeof(flash_value) != TYPE_DICTIONARY or typeof((flash_value as Dictionary).get("children", {})) != TYPE_DICTIONARY:
			return false
		target_value = ((flash_value as Dictionary).get("children", {}) as Dictionary).get(button_name, null)
		target_path = "FlashEffects/%s" % button_name
	elif method_name == "SetGlassState":
		target_value = children.get("glass%s" % button_name, null)
		target_path = "glass%s" % button_name
	else:
		return false
	if typeof(target_value) != TYPE_DICTIONARY:
		return false
	var target := target_value as Dictionary
	target["label"] = state_label
	target["playing"] = true
	var dispatch_log := log_value as Array
	dispatch_log.append({"kind": "gotoAndPlay", "button": button_name, "method": method_name, "target": target_path, "state": state_label})
	timeline_state["dispatchLog"] = dispatch_log
	return true


func execute_action_script(
	script_id: String, timeline_state: Dictionary, runtime_inputs: Dictionary = {}
) -> bool:
	var value: Variant = _action_scripts.get(script_id, {})
	if typeof(value) != TYPE_DICTIONARY or not bool((value as Dictionary).get("supported", false)):
		return false
	if TYPED_MINLOD_PROGRAMS.has(script_id):
		return _execute_typed_minlod_effect(
			script_id, value as Dictionary, timeline_state, runtime_inputs
		)
	if script_id == String(TYPED_RESOURCE_FLASH_PROGRAM.scriptId):
		return _execute_typed_resource_flash_effect(value as Dictionary, timeline_state)
	if TYPED_SIDE_COMMAND_PROGRAMS.has(script_id):
		return _execute_typed_side_command_effect(
			script_id, value as Dictionary, timeline_state, runtime_inputs
		)
	if TYPED_PALANTIR_COMMAND_PROGRAMS.has(script_id):
		return _execute_typed_palantir_command_effect(script_id, value as Dictionary, timeline_state)
	return _execute_timeline_effects((value as Dictionary).get("effects", []) as Array, timeline_state)


func play_command_point_effect(timeline_state: Dictionary) -> bool:
	if not resource_flash_ready:
		return false
	if (
		String(timeline_state.get("timelineId", "")) != "palantir:309"
		or not timeline_state.has("frame")
		or typeof(timeline_state.get("frame")) != TYPE_INT
		or not timeline_state.has("playing")
		or typeof(timeline_state.get("playing")) != TYPE_BOOL
		or typeof(timeline_state.get("audioEventIntents", [])) != TYPE_ARRAY
	):
		return false
	var updated := timeline_state.duplicate(true)
	updated["frame"] = 8
	updated["label"] = "_go"
	updated["playing"] = true
	if not execute_action_script("palantir:332504", updated):
		return false
	timeline_state.clear()
	timeline_state.merge(updated, true)
	return true


func _validate_typed_resource_flash_program(program: Dictionary) -> bool:
	var effects_value: Variant = program.get("effects", [])
	if (
		String(program.get("actionKind", "")) != "action-script"
		or String(program.get("movie", "")) != String(TYPED_RESOURCE_FLASH_PROGRAM.movie)
		or int(program.get("sourceOffset", -1)) != int(TYPED_RESOURCE_FLASH_PROGRAM.sourceOffset)
		or int(program.get("instructionOffset", -1)) != int(TYPED_RESOURCE_FLASH_PROGRAM.instructionOffset)
		or int(program.get("byteLength", -1)) != int(TYPED_RESOURCE_FLASH_PROGRAM.byteLength)
		or String(program.get("sha256", "")) != String(TYPED_RESOURCE_FLASH_PROGRAM.sha256)
		or int(program.get("maximumStackDepth", -1)) != int(TYPED_RESOURCE_FLASH_PROGRAM.maximumStackDepth)
		or typeof(effects_value) != TYPE_ARRAY
		or (effects_value as Array).size() != 2
	):
		return false
	var effects := effects_value as Array
	if typeof(effects[0]) != TYPE_DICTIONARY or typeof(effects[1]) != TYPE_DICTIONARY:
		return false
	var play := effects[0] as Dictionary
	var audio := effects[1] as Dictionary
	var source_value: Variant = audio.get("sourceEvidence", {})
	return (
		play == {"kind": "play-current-timeline"}
		and String(audio.get("kind", "")) == "emit-retail-audio-event-intent"
		and String(audio.get("receiver", "")) == "_root"
		and String(audio.get("method", "")) == "PlaySound"
		and audio.get("arguments", []) == [String(TYPED_RESOURCE_FLASH_PROGRAM.eventId)]
		and bool(audio.get("discardReturn", false))
		and String(audio.get("precondition", "")) == "Palantir root Initialized is truthy"
		and String(audio.get("dispatch", "")) == "FSCommand:PlaySound"
		and typeof(source_value) == TYPE_DICTIONARY
		and String((source_value as Dictionary).get("programId", "")) == "palantir:332504"
		and int((source_value as Dictionary).get("instructionOffset", -1)) == 370752
		and int((source_value as Dictionary).get("instructionEndOffset", -1)) == 370778
		and int((source_value as Dictionary).get("byteLength", -1)) == 26
		and String((source_value as Dictionary).get("sha256", "")) == String(TYPED_RESOURCE_FLASH_PROGRAM.sha256)
	)


func _execute_typed_resource_flash_effect(program: Dictionary, timeline_state: Dictionary) -> bool:
	if not _validate_typed_resource_flash_program(program):
		return false
	if (
		String(timeline_state.get("timelineId", "")) != "palantir:309"
		or not timeline_state.has("playing")
		or typeof(timeline_state.get("playing")) != TYPE_BOOL
		or typeof(timeline_state.get("audioEventIntents", [])) != TYPE_ARRAY
	):
		return false
	var intents := (timeline_state.get("audioEventIntents", []) as Array).duplicate(true)
	intents.append({
		"eventId": String(TYPED_RESOURCE_FLASH_PROGRAM.eventId),
		"dispatch": "FSCommand:PlaySound",
		"sourceScriptId": "palantir:332504",
	})
	timeline_state["playing"] = true
	timeline_state["audioEventIntents"] = intents
	return true


func _validate_typed_minlod_program(script_id: String, program: Dictionary) -> bool:
	var expected_value: Variant = TYPED_MINLOD_PROGRAMS.get(script_id, {})
	if typeof(expected_value) != TYPE_DICTIONARY or (expected_value as Dictionary).is_empty():
		return false
	var expected := expected_value as Dictionary
	var effects_value: Variant = program.get("effects", [])
	return (
		bool(program.get("supported", false))
		and String(program.get("actionKind", "")) == "action-script"
		and String(program.get("movie", "")) == String(expected.get("movie", ""))
		and int(program.get("sourceOffset", -1)) == int(expected.get("sourceOffset", -2))
		and int(program.get("instructionOffset", -1)) == int(expected.get("instructionOffset", -2))
		and int(program.get("byteLength", -1)) == int(expected.get("byteLength", -2))
		and String(program.get("sha256", "")) == String(expected.get("sha256", "invalid"))
		and int(program.get("maximumStackDepth", -1)) == int(expected.get("maximumStackDepth", -2))
		and int(program.get("terminalStackDepth", -1)) == 0
		and typeof(effects_value) == TYPE_ARRAY
		and (effects_value as Array).size() == 1
		and typeof((effects_value as Array)[0]) == TYPE_DICTIONARY
		and _validate_typed_minlod_effect(script_id, (effects_value as Array)[0] as Dictionary)
	)


func _validate_typed_minlod_effect(script_id: String, effect: Dictionary) -> bool:
	var expected := TYPED_MINLOD_PROGRAMS[script_id] as Dictionary
	var condition_value: Variant = effect.get("condition", {})
	var source_value: Variant = effect.get("sourceEvidence", {})
	var true_value: Variant = effect.get("whenTrue", [])
	var false_value: Variant = effect.get("whenFalse", [])
	if (
		effect.size() != 5
		or String(effect.get("kind", "")) != "conditional-min-lod"
		or typeof(condition_value) != TYPE_DICTIONARY
		or (condition_value as Dictionary).size() != 3
		or String((condition_value as Dictionary).get("kind", "")) != "required-boolean-input"
		or String((condition_value as Dictionary).get("name", "")) != "MinLOD"
		or typeof((condition_value as Dictionary).get("equals")) != TYPE_BOOL
		or not bool((condition_value as Dictionary).get("equals"))
		or typeof(source_value) != TYPE_DICTIONARY
		or (source_value as Dictionary).size() != 5
		or String((source_value as Dictionary).get("programId", "")) != script_id
		or int((source_value as Dictionary).get("instructionOffset", -1)) != int(expected.instructionOffset)
		or int((source_value as Dictionary).get("instructionEndOffset", -1)) != int(expected.instructionOffset) + int(expected.byteLength)
		or int((source_value as Dictionary).get("byteLength", -1)) != int(expected.byteLength)
		or String((source_value as Dictionary).get("sha256", "")) != String(expected.sha256)
		or typeof(true_value) != TYPE_ARRAY
		or typeof(false_value) != TYPE_ARRAY
		or not (false_value as Array).is_empty()
	):
		return false
	var rows := true_value as Array
	if script_id == "palantir:152912":
		if rows.size() != 1 or typeof(rows[0]) != TYPE_DICTIONARY:
			return false
		var row := rows[0] as Dictionary
		return (
			row.size() == 5
			and String(row.get("kind", "")) == "stop-timeline-if-property-equals"
			and String(row.get("target", "")) == "this"
			and int(row.get("propertyIndex", -1)) == 13
			and String(row.get("propertyName", "")) == "_name"
			and String(row.get("equals", "")) == "GlobeSwirlRender"
		)
	var expected_targets := ["effect1", "effect4"] if script_id == "palantir:333872" else ["effect2", "effect3"]
	if rows.size() != expected_targets.size():
		return false
	for index in rows.size():
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return false
		var row := rows[index] as Dictionary
		if (
			row.size() != 4
			or String(row.get("kind", "")) != "set-named-clip-property"
			or String(row.get("target", "")) != String(expected_targets[index])
			or String(row.get("propertyName", "")) != "_visible"
			or typeof(row.get("value")) != TYPE_BOOL
			or bool(row.get("value"))
		):
			return false
	return true


func _execute_typed_minlod_effect(
	script_id: String,
	program: Dictionary,
	timeline_state: Dictionary,
	runtime_inputs: Dictionary
) -> bool:
	if not _validate_typed_minlod_program(script_id, program):
		return false
	if not runtime_inputs.has("MinLOD") or typeof(runtime_inputs.get("MinLOD")) != TYPE_BOOL:
		return false
	if script_id == "palantir:152912":
		if (
			not timeline_state.has("_name")
			or typeof(timeline_state.get("_name")) != TYPE_STRING
			or not timeline_state.has("playing")
			or typeof(timeline_state.get("playing")) != TYPE_BOOL
		):
			return false
		if bool(runtime_inputs["MinLOD"]) and String(timeline_state["_name"]) == "GlobeSwirlRender":
			timeline_state["playing"] = false
		return true
	var targets_value: Variant = timeline_state.get("targets", {})
	if typeof(targets_value) != TYPE_DICTIONARY:
		return false
	var target_names: Array = (
		["effect1", "effect4"]
		if script_id == "palantir:333872"
		else ["effect2", "effect3"]
	)
	var targets := targets_value as Dictionary
	for target_name_value in target_names:
		var target_name := String(target_name_value)
		var target_value: Variant = targets.get(target_name, {})
		if (
			typeof(target_value) != TYPE_DICTIONARY
			or not (target_value as Dictionary).has("_visible")
			or typeof((target_value as Dictionary).get("_visible")) != TYPE_BOOL
		):
			return false
	if not bool(runtime_inputs["MinLOD"]):
		return true
	var updated_targets := targets.duplicate(true)
	for target_name_value in target_names:
		var target_name := String(target_name_value)
		var target := (updated_targets[target_name] as Dictionary).duplicate(true)
		target["_visible"] = false
		updated_targets[target_name] = target
	timeline_state["targets"] = updated_targets
	return true


func execute_clip_action(clip_action_id: String, event_mask: int, timeline_state: Dictionary) -> bool:
	var binding_value: Variant = _clip_actions.get(clip_action_id, {})
	if typeof(binding_value) != TYPE_DICTIONARY:
		return false
	for event_value in (binding_value as Dictionary).get("events", []) as Array:
		if typeof(event_value) != TYPE_DICTIONARY:
			return false
		var event := event_value as Dictionary
		if int(event.get("eventMask", -1)) != event_mask:
			continue
		if not bool(event.get("executable", false)):
			return false
		var program_value: Variant = _clip_action_programs.get(String(event.get("programId", "")), {})
		if typeof(program_value) != TYPE_DICTIONARY or not bool((program_value as Dictionary).get("supported", false)):
			return false
		if TYPED_INITIALIZE_PROGRAMS.has(String(event.get("programId", ""))):
			return _execute_typed_initialize_effect(program_value as Dictionary, timeline_state)
		return _execute_timeline_effects((program_value as Dictionary).get("effects", []) as Array, timeline_state)
	return false


func _execute_timeline_effects(effects: Array, timeline_state: Dictionary) -> bool:
	for effect_value in effects:
		var effect := effect_value as Dictionary
		match String(effect.get("kind", "")):
			"play":
				timeline_state["playing"] = true
			"stop":
				timeline_state["playing"] = false
			"goto":
				if String(effect.get("targetType", "")) in ["frame", "integer"]:
					timeline_state["frame"] = int(effect.get("target", -1))
				else:
					timeline_state["label"] = String(effect.get("target", ""))
			_:
				return false
	return true


func _execute_typed_initialize_effect(program: Dictionary, target_state: Dictionary) -> bool:
	var program_id := String(program.get("programId", ""))
	if not _validate_typed_initialize_program(program_id, program):
		return false
	var effect := (program.get("effects", []) as Array)[0] as Dictionary
	match String(effect.get("kind", "")):
		"define-local-method":
			var methods_value: Variant = target_state.get("localMethods", {})
			if typeof(methods_value) != TYPE_DICTIONARY:
				return false
			var methods := (methods_value as Dictionary).duplicate(true)
			methods["SetFlashEffectState"] = (effect.get("body", {}) as Dictionary).duplicate(true)
			target_state["localMethods"] = methods
			return true
		"set-clip-property":
			target_state["_visible"] = false
			return true
		"bind-live-text":
			target_state["liveTextBinding"] = {
				"targetMember": String(effect.get("targetMember", "")),
				"aptVariable": String(effect.get("aptVariable", "")),
				"runtimeInputs": (effect.get("runtimeInputs", []) as Array).duplicate(),
				"formatter": String(effect.get("formatter", "")),
			}
			return true
	return false


func _validate_typed_initialize_program(program_id: String, program: Dictionary) -> bool:
	var expected_value: Variant = TYPED_INITIALIZE_PROGRAMS.get(program_id, {})
	if typeof(expected_value) != TYPE_DICTIONARY or (expected_value as Dictionary).is_empty():
		return false
	var expected := expected_value as Dictionary
	var effects_value: Variant = program.get("effects", [])
	return (
		bool(program.get("supported", false))
		and String(program.get("movie", "")) == String(expected.get("movie", ""))
		and int(program.get("sourceOffset", -1)) == int(expected.get("sourceOffset", -2))
		and int(program.get("instructionOffset", -1)) == int(expected.get("instructionOffset", -2))
		and int(program.get("byteLength", -1)) == int(expected.get("byteLength", -2))
		and int(program.get("instructionEndOffset", -1)) == int(expected.get("instructionOffset", -2)) + int(expected.get("byteLength", -2))
		and String(program.get("sha256", "")) == String(expected.get("sha256", "invalid"))
		and int(program.get("maximumStackDepth", -1)) == int(expected.get("maximumStackDepth", -2))
		and int(program.get("terminalStackDepth", -1)) == 0
		and typeof(effects_value) == TYPE_ARRAY
		and (effects_value as Array).size() == 1
		and typeof((effects_value as Array)[0]) == TYPE_DICTIONARY
		and _validate_typed_initialize_effect(program_id, (effects_value as Array)[0] as Dictionary)
	)


func _validate_typed_initialize_effect(program_id: String, effect: Dictionary) -> bool:
	var source_value: Variant = effect.get("sourceEvidence", {})
	if typeof(source_value) != TYPE_DICTIONARY:
		return false
	var source := source_value as Dictionary
	var expected := TYPED_INITIALIZE_PROGRAMS[program_id] as Dictionary
	if (
		String(source.get("programId", "")) != program_id
		or int(source.get("instructionOffset", -1)) != int(expected.get("instructionOffset", -2))
		or int(source.get("instructionEndOffset", -1)) != int(expected.get("instructionOffset", -2)) + int(expected.get("byteLength", -2))
		or int(source.get("byteLength", -1)) != int(expected.get("byteLength", -2))
		or String(source.get("sha256", "")) != String(expected.get("sha256", "invalid"))
	):
		return false
	if program_id == "ingamesidecommandbar:clip-event:13680":
		var body_value: Variant = effect.get("body", {})
		if typeof(body_value) != TYPE_DICTIONARY:
			return false
		var body := body_value as Dictionary
		var index_value: Variant = body.get("index", {})
		var arguments_value: Variant = body.get("arguments", [])
		return (
			String(effect.get("kind", "")) == "define-local-method"
			and String(effect.get("receiver", "")) == "this"
			and String(effect.get("methodName", "")) == "SetFlashEffectState"
			and effect.get("parameters", []) == ["state"]
			and String(body.get("kind", "")) == "call-indexed-ancestor-timeline-method"
			and int(body.get("receiverAncestorHops", -1)) == 2
			and String(body.get("collection", "")) == "flashEffects"
			and typeof(index_value) == TYPE_DICTIONARY
			and int((index_value as Dictionary).get("ancestorHops", -1)) == 1
			and String((index_value as Dictionary).get("property", "")) == "_name"
			and String(body.get("methodName", "")) == "gotoAndPlay"
			and typeof(arguments_value) == TYPE_ARRAY
			and (arguments_value as Array).size() == 1
			and typeof((arguments_value as Array)[0]) == TYPE_DICTIONARY
			and String(((arguments_value as Array)[0] as Dictionary).get("kind", "")) == "parameter"
			and String(((arguments_value as Array)[0] as Dictionary).get("name", "")) == "state"
		)
	if program_id == "libingameui:clip-event:56252":
		return (
			String(effect.get("kind", "")) == "set-clip-property"
			and String(effect.get("target", "invalid")) == ""
			and int(effect.get("propertyIndex", -1)) == 7
			and String(effect.get("propertyName", "")) == "_visible"
			and effect.has("value")
			and typeof(effect.get("value")) == TYPE_BOOL
			and not bool(effect.get("value"))
		)
	if program_id in [
		"palantir:clip-event:375628",
		"palantir:clip-event:375640",
		"palantir:clip-event:375652",
	]:
		var expected_effect := expected.get("effect", {}) as Dictionary
		return (
			String(effect.get("kind", "")) == "bind-live-text"
			and String(effect.get("targetMember", "")) == String(expected_effect.targetMember)
			and String(effect.get("aptVariable", "")) == String(expected_effect.aptVariable)
			and effect.get("runtimeInputs", []) == expected_effect.runtimeInputs
			and String(effect.get("formatter", "")) == String(expected_effect.formatter)
			and effect.size() == 6
		)
	return false


func _validate_clip_actions(programs_value: Variant, bindings_value: Variant, timeline_ids: Dictionary) -> Dictionary:
	if typeof(programs_value) != TYPE_ARRAY:
		_fail("Palantir clip-action program inventory is invalid")
		return {}
	var programs := programs_value as Array
	if programs.is_empty() or programs.size() > MAX_CLIP_ACTIONS:
		_fail("Palantir clip-action program inventory is empty or exceeds bounds")
		return {}
	var program_ids: Dictionary = {}
	supported_clip_action_program_count = 0
	for program_value in programs:
		if typeof(program_value) != TYPE_DICTIONARY:
			_fail("Palantir clip-action program is invalid")
			return {}
		var program := program_value as Dictionary
		var program_id := String(program.get("programId", ""))
		var supported := bool(program.get("supported", false))
		var instructions_value: Variant = program.get("instructions", [])
		var effects_value: Variant = program.get("effects", [])
		var unsupported_value: Variant = program.get("unsupportedInstructions", [])
		if (
			program_id == ""
			or program_ids.has(program_id)
			or String(program.get("actionKind", "")) != "clip-action-event"
			or String(program.get("movie", "")) == ""
			or int(program.get("sourceOffset", -1)) < 0
			or int(program.get("instructionOffset", -1)) < 0
			or int(program.get("byteLength", 0)) <= 0
			or int(program.get("instructionEndOffset", -1)) != int(program.get("instructionOffset", -1)) + int(program.get("byteLength", 0))
			or not _is_sha256(String(program.get("sha256", "")))
			or int(program.get("maximumStackDepth", -1)) < 0
			or int(program.get("terminalStackDepth", -1)) < 0
			or typeof(instructions_value) != TYPE_ARRAY
			or (instructions_value as Array).is_empty()
			or typeof(effects_value) != TYPE_ARRAY
			or typeof(unsupported_value) != TYPE_ARRAY
		):
			_fail("Palantir clip-action program identity or byte range changed")
			return {}
		if not _validate_action_instruction_rows(instructions_value as Array):
			return {}
		if supported:
			var typed_initialize := TYPED_INITIALIZE_PROGRAMS.has(program_id)
			if (
				not (unsupported_value as Array).is_empty()
				or int(program.get("terminalStackDepth", -1)) != 0
				or (typed_initialize and not _validate_typed_initialize_program(program_id, program))
				or (not typed_initialize and not _validate_action_effects(effects_value as Array))
			):
				_fail("Palantir supported clip action retained unsupported semantics")
				return {}
			supported_clip_action_program_count += 1
		elif TYPED_INITIALIZE_PROGRAMS.has(program_id) or (unsupported_value as Array).is_empty() or not (effects_value as Array).is_empty():
			_fail("Palantir blocked clip action lost exact opcode evidence")
			return {}
		program_ids[program_id] = true
		_clip_action_programs[program_id] = program
	clip_action_program_count = programs.size()
	if typeof(bindings_value) != TYPE_ARRAY:
		_fail("Palantir clip-action binding inventory is invalid")
		return {}
	var bindings := bindings_value as Array
	if bindings.is_empty() or bindings.size() > MAX_CLIP_ACTIONS:
		_fail("Palantir clip-action binding inventory is empty or exceeds bounds")
		return {}
	var blocked: Dictionary = {}
	clip_action_event_count = 0
	executable_clip_action_event_count = 0
	font_count = 0
	embedded_font_glyph_count = 0
	text_count = 0
	text_instance_count = 0
	button_count = 0
	button_instance_count = 0
	button_action_count = 0
	for binding_value in bindings:
		if typeof(binding_value) != TYPE_DICTIONARY:
			_fail("Palantir clip-action binding is invalid")
			return {}
		var binding := binding_value as Dictionary
		var clip_action_id := String(binding.get("clipActionId", ""))
		var target_timeline_id := String(binding.get("targetTimelineId", ""))
		var events_value: Variant = binding.get("events", [])
		if (
			clip_action_id == ""
			or _clip_actions.has(clip_action_id)
			or String(binding.get("movie", "")) == ""
			or int(binding.get("sourceOffset", -1)) < 0
			or int(binding.get("clipActionsOffset", -1)) < 0
			or int(binding.get("headerEndOffset", -1)) != int(binding.get("clipActionsOffset", -1)) + 8
			or int(binding.get("eventTableOffset", -1)) < 0
			or not _is_sha256(String(binding.get("headerSha256", "")))
			or String(binding.get("targetPath", "")) == ""
			or int(binding.get("targetSourceCharacterId", -1)) < 0
			or String(binding.get("targetMovie", "")) == ""
			or int(binding.get("targetCharacterId", -1)) < 0
			or String(binding.get("targetKind", "")) not in ["movie", "sprite"]
			or String(binding.get("targetClipId", "")) == ""
			or (target_timeline_id != "" and not timeline_ids.has(target_timeline_id))
			or typeof(events_value) != TYPE_ARRAY
			or (events_value as Array).is_empty()
			or int(binding.get("eventCount", -1)) != (events_value as Array).size()
		):
			_fail("Palantir clip-action target or record identity changed")
			return {}
		var binding_blocked := false
		for event_index in (events_value as Array).size():
			var event_value: Variant = (events_value as Array)[event_index]
			if typeof(event_value) != TYPE_DICTIONARY:
				_fail("Palantir clip-action event is invalid")
				return {}
			var event := event_value as Dictionary
			var mask := int(event.get("eventMask", 0))
			var names := _clip_event_names(mask)
			var program_id := String(event.get("programId", ""))
			if (
				int(event.get("eventIndex", -1)) != event_index
				or int(event.get("eventOffset", -1)) < 0
				or int(event.get("eventEndOffset", -1)) != int(event.get("eventOffset", -1)) + 12
				or names.is_empty()
				or event.get("eventNames", []) != names
				or int(event.get("keyCode", -1)) < 0
				or int(event.get("keyCode", -1)) > 255
				or int(event.get("nextEventOffset", -1)) < 0
				or not _is_sha256(String(event.get("recordSha256", "")))
				or not program_ids.has(program_id)
			):
				_fail("Palantir clip-action event mask, order, or byte evidence changed")
				return {}
			var program := _clip_action_programs[program_id] as Dictionary
			var expected_executable := (
				names == ["initialize"]
				and int(event.get("keyCode", -1)) == 0
				and int(event.get("nextEventOffset", -1)) == 0
				and (target_timeline_id != "" or TYPED_INITIALIZE_PROGRAMS.has(program_id))
				and bool(program.get("supported", false))
			)
			if bool(event.get("executable", false)) != expected_executable:
				_fail("Palantir clip-action executable subset was widened or weakened")
				return {}
			if expected_executable:
				if String(event.get("dispatchOrder", "")) != "after-target-create-and-name-before-display-list-insert":
					_fail("Palantir clip-action initialize ordering changed")
					return {}
				executable_clip_action_event_count += 1
			else:
				binding_blocked = true
		clip_action_event_count += (events_value as Array).size()
		if binding_blocked:
			var blocker_code := String(binding.get("blockerCode", ""))
			if not blocker_code.begins_with("clip-action-"):
				_fail("Palantir blocked clip action lost its exact category")
				return {}
			blocked[clip_action_id] = blocker_code
		_clip_actions[clip_action_id] = binding
	clip_action_count = bindings.size()
	return blocked


func _clip_event_names(mask: int) -> Array:
	var names: Array = []
	var known_mask := 0
	for flag_value in CLIP_EVENT_NAMES:
		var flag := int(flag_value)
		known_mask |= flag
		if mask & flag:
			names.append(String(CLIP_EVENT_NAMES[flag]))
	if mask <= 0 or mask & ~known_mask:
		return []
	return names


func _validate_clip_action_blockers(blockers: Array, blocked: Dictionary) -> bool:
	var seen: Dictionary = {}
	for blocker_value in blockers:
		if typeof(blocker_value) != TYPE_DICTIONARY:
			return _fail("Palantir blocker inventory contains an invalid entry")
		var blocker := blocker_value as Dictionary
		var code := String(blocker.get("code", ""))
		if code == "clip-actions-not-executed":
			return _fail("Palantir generic clip-action blocker was retained")
		var clip_action_id := String(blocker.get("clipActionId", ""))
		if clip_action_id == "":
			continue
		if not blocked.has(clip_action_id) or seen.has(clip_action_id) or code != String(blocked[clip_action_id]):
			return _fail("Palantir clip-action blocker category or identity changed")
		var binding := _clip_actions[clip_action_id] as Dictionary
		if (
			int(blocker.get("sourceOffset", -1)) != int(binding.get("sourceOffset", -2))
			or int(blocker.get("clipActionsOffset", -1)) != int(binding.get("clipActionsOffset", -2))
			or String(blocker.get("targetPath", "")) != String(binding.get("targetPath", ""))
			or typeof(blocker.get("events", [])) != TYPE_ARRAY
			or (blocker.get("events", []) as Array).is_empty()
		):
			return _fail("Palantir clip-action blocker lost exact event evidence")
		seen[clip_action_id] = true
	if seen.size() != blocked.size():
		return _fail("Palantir blocked clip-action inventory is incomplete")
	return true


func _validate_timelines(timelines_value: Variant, instances_value: Variant, action_script_ids: Dictionary) -> Dictionary:
	if typeof(timelines_value) != TYPE_ARRAY:
		_fail("Palantir exact timeline inventory is invalid")
		return {}
	var timelines := timelines_value as Array
	if timelines.is_empty() or timelines.size() > MAX_TIMELINES:
		_fail("Palantir exact timeline inventory is empty or exceeds bounds")
		return {}
	var timeline_ids: Dictionary = {}
	timeline_frame_count = 0
	for timeline_value in timelines:
		if typeof(timeline_value) != TYPE_DICTIONARY:
			_fail("Palantir exact timeline contains an invalid entry")
			return {}
		var timeline := timeline_value as Dictionary
		var timeline_id := String(timeline.get("timelineId", ""))
		var frames_value: Variant = timeline.get("frames", [])
		var frame_count := int(timeline.get("frameCount", -1))
		if (
			timeline_id == ""
			or timeline_ids.has(timeline_id)
			or String(timeline.get("movie", "")) == ""
			or int(timeline.get("characterId", -1)) < 0
			or not bool(timeline.get("displayListComplete", false))
			or typeof(frames_value) != TYPE_ARRAY
			or frame_count <= 1
			or (frames_value as Array).size() != frame_count
		):
			_fail("Palantir exact timeline identity or frame count is invalid")
			return {}
		timeline_ids[timeline_id] = true
		timeline_frame_count += frame_count
		if timeline_frame_count > MAX_TIMELINE_FRAMES:
			_fail("Palantir exact timeline frames exceed bounds")
			return {}
		for frame_index in frame_count:
			var frame_value: Variant = (frames_value as Array)[frame_index]
			if typeof(frame_value) != TYPE_DICTIONARY:
				_fail("Palantir exact timeline frame is invalid")
				return {}
			var frame := frame_value as Dictionary
			if int(frame.get("frameIndex", -1)) != frame_index:
				_fail("Palantir exact timeline frame sequence changed")
				return {}
			var display_value: Variant = frame.get("displayList", [])
			var operations_value: Variant = frame.get("operations", [])
			if typeof(display_value) != TYPE_ARRAY or typeof(operations_value) != TYPE_ARRAY:
				_fail("Palantir exact timeline display list is invalid")
				return {}
			var prior_depth := -2147483648
			for entry_value in display_value as Array:
				if typeof(entry_value) != TYPE_DICTIONARY:
					_fail("Palantir exact timeline display entry is invalid")
					return {}
				var entry := entry_value as Dictionary
				var depth := int(entry.get("depth", -2147483648))
				if (
					depth <= prior_depth
					or int(entry.get("characterId", -1)) < 0
					or not _finite_number_array(entry.get("matrix", []), 4)
					or not _finite_number_array(entry.get("translation", []), 2)
					or not _finite_number_array(entry.get("tint", []), 4)
					or not _finite_number_array(entry.get("additive", []), 4)
					or not is_finite(float(entry.get("ratio", NAN)))
					or typeof(entry.get("sourceOffsets", [])) != TYPE_ARRAY
					or (entry.get("sourceOffsets", []) as Array).is_empty()
				):
					_fail("Palantir exact timeline display entry changed")
					return {}
				prior_depth = depth
			var actions_value: Variant = frame.get("actionScripts", [])
			if typeof(actions_value) != TYPE_ARRAY:
				_fail("Palantir timeline ActionScript references are invalid")
				return {}
			for action_value in actions_value as Array:
				if typeof(action_value) != TYPE_DICTIONARY or not action_script_ids.has(String((action_value as Dictionary).get("scriptId", ""))):
					_fail("Palantir timeline ActionScript reference changed")
					return {}
	timeline_count = timelines.size()
	if typeof(instances_value) != TYPE_ARRAY:
		_fail("Palantir timeline instance inventory is invalid")
		return {}
	var instances := instances_value as Array
	if instances.is_empty() or instances.size() > MAX_TIMELINE_INSTANCES:
		_fail("Palantir timeline instance inventory is empty or exceeds bounds")
		return {}
	for instance_value in instances:
		if typeof(instance_value) != TYPE_DICTIONARY:
			_fail("Palantir timeline instance is invalid")
			return {}
		var instance := instance_value as Dictionary
		if (
			not timeline_ids.has(String(instance.get("timelineId", "")))
			or String(instance.get("path", "")) == ""
			or not _finite_number_array(instance.get("matrix", []), 4)
			or not _finite_number_array(instance.get("translation", []), 2)
			or not _finite_number_array(instance.get("tint", []), 4)
			or not _finite_number_array(instance.get("additive", []), 4)
		):
			_fail("Palantir timeline instance changed")
			return {}
	timeline_instance_count = instances.size()
	return timeline_ids


func _validate_external_movie_attachments(loads_value: Variant, attachments_value: Variant, lifecycle_value: Variant, diagnostics_value: Variant) -> bool:
	if (
		typeof(loads_value) != TYPE_ARRAY
		or (loads_value as Array).size() != 5
		or typeof(attachments_value) != TYPE_ARRAY
		or (attachments_value as Array).size() != EXTERNAL_MOVIE_SLOT_SPECS.size()
		or typeof(lifecycle_value) != TYPE_DICTIONARY
		or typeof(diagnostics_value) != TYPE_DICTIONARY
	):
		return _fail("Palantir external movie attachment inventory is invalid")
	var expected_load_movies := [
		"InGameSpellBook", "InGameSideCommandBar", "InGameHelpBox",
		"InGameHeroSelect", "InGamePlanningMode",
	]
	for index in expected_load_movies.size():
		var load_value: Variant = (loads_value as Array)[index]
		if typeof(load_value) != TYPE_DICTIONARY:
			return _fail("Palantir external movie load row is invalid")
		var load := load_value as Dictionary
		var expected_runtime := "already-bound-root-layer" if index == 1 else "exact-palantir-child-slot-bound"
		if (
			int(load.get("loadOrder", -1)) != index
			or String(load.get("movie", "")) != expected_load_movies[index]
			or String(load.get("runtimeAttachment", "")) != expected_runtime
			or not bool(load.get("sourceLoadReachable", false))
			or not bool(load.get("sourceClosurePresent", false))
		):
			return _fail("Palantir external movie load order or binding changed")
	_staged_external_movie_slots.clear()
	external_movie_load_order.clear()
	for index in EXTERNAL_MOVIE_SLOT_SPECS.size():
		var value: Variant = (attachments_value as Array)[index]
		if typeof(value) != TYPE_DICTIONARY:
			return _fail("Palantir external movie slot is invalid")
		var row := value as Dictionary
		var expected := EXTERNAL_MOVIE_SLOT_SPECS[index] as Dictionary
		var placeholder_value: Variant = row.get("placeholder", {})
		var root_value: Variant = row.get("sourceRoot", {})
		var callback_value: Variant = row.get("lifecycle", {})
		if (
			typeof(placeholder_value) != TYPE_DICTIONARY
			or typeof(root_value) != TYPE_DICTIONARY
			or typeof(callback_value) != TYPE_DICTIONARY
		):
			return _fail("Palantir external movie slot evidence is invalid")
		var placeholder := placeholder_value as Dictionary
		var source_root := root_value as Dictionary
		var callback := callback_value as Dictionary
		if (
			int(row.get("loadOrder", -1)) != int(expected.loadOrder)
			or int(row.get("loadInstructionOffset", -1)) != int(expected.loadInstructionOffset)
			or String(row.get("movie", "")) != String(expected.movie)
			or String(row.get("swf", "")) != String(expected.swf)
			or String(row.get("target", "")) != String(expected.target)
			or String(row.get("targetPath", "")) != String(expected.targetPath)
			or String(row.get("attachmentKind", "")) != "replace-authored-empty-child-clip"
			or String(row.get("godotInterface", "")) != String(expected.godotInterface)
			or bool(row.get("genericVmRequired", true))
			or bool(row.get("independentRootAllowed", true))
			or String(row.get("defaultState", "")) != String(expected.defaultState)
			or String(row.get("normalMenVsMen", "")) != String(expected.normalMenVsMen)
		):
			return _fail("Palantir external movie slot identity changed")
		if (
			int(placeholder.get("sourceOffset", -1)) != int(expected.sourceOffset)
			or String(placeholder.get("recordSha256", "")) != String(expected.recordSha256)
			or int(placeholder.get("characterId", -1)) != 41
			or int(placeholder.get("depth", -1)) != int(expected.depth)
			or not _number_array_matches(placeholder.get("matrix", []), expected.matrix)
			or not _number_array_matches(placeholder.get("translation", []), expected.translation)
			or not _number_array_matches(placeholder.get("tint", []), [1.0, 1.0, 1.0, 1.0])
			or not _number_array_matches(placeholder.get("additive", []), [0.0, 0.0, 0.0, 0.0])
		):
			return _fail(
				"Palantir external movie authored transform changed: %s"
				% JSON.stringify(placeholder)
			)
		if (
			String(source_root.get("characterKind", "")) != "movie"
			or int(source_root.get("entryFrame", -1)) != 0
			or int(source_root.get("frameCount", -1)) != int(expected.frameCount)
			or not _integer_dictionary_matches(source_root.get("labels", {}), expected.labels)
			or int(source_root.get("initialStopFrame", -1)) != int(expected.initialStopFrame)
			or int(source_root.get("programOffset", -1)) != int(expected.programOffset)
			or String(source_root.get("programSha256", "")) != String(expected.programSha256)
		):
			return _fail("Palantir external movie source root changed")
		if (
			String(callback.get("loadedCallback", "")) != String(expected.loadedCallback)
			or String(callback.get("unloadedCallback", "")) != String(expected.unloadedCallback)
			or String(callback.get("argument", "")) != String(expected.argument)
			or bool(callback.get("dispatchBound", true))
		):
			return _fail("Palantir external movie lifecycle callback changed")
		var state := {
			"movie": String(expected.movie),
			"target": String(expected.target),
			"targetPath": String(expected.targetPath),
			"loadOrder": int(expected.loadOrder),
			"depth": int(expected.depth),
			"matrix": (expected.matrix as Array).duplicate(),
			"translation": (expected.translation as Array).duplicate(),
			"tint": [1.0, 1.0, 1.0, 1.0],
			"additive": [0.0, 0.0, 0.0, 0.0],
			"entryFrame": 0,
			"currentFrame": int(expected.initialStopFrame),
			"labels": (expected.labels as Dictionary).duplicate(),
			"defaultState": String(expected.defaultState),
			"visible": false,
			"lifecycleState": "attached-awaiting-capture",
			"loadedCallback": String(expected.loadedCallback),
			"unloadedCallback": String(expected.unloadedCallback),
		}
		_staged_external_movie_slots.append(state)
		external_movie_load_order.append(String(expected.movie))
	var lifecycle := lifecycle_value as Dictionary
	if (
		lifecycle.get("initialSetupLoadOrder", []) != expected_load_movies
		or lifecycle.get("blockedTargetLoadOrder", []) != external_movie_load_order
		or lifecycle.get("nativeRetainedSlots", {}) != {"HeroSelectUI": "+0xc4", "helpBox": "+0xc8", "planningModeUI": "+0xcc"}
		or lifecycle.get("nativeResetClearOrder", []) != ["HeroSelectUI", "helpBox", "planningModeUI"]
		or String(lifecycle.get("nativeResetSha256", "")) != "caa92439a63eac781e297a16ace1e3f48e79abe8750f6b3b1e8a5637d6a61587"
		or String(lifecycle.get("spellBookResetPath", "")) != "separate-fscommand-relative-order-unresolved"
		or String(lifecycle.get("runtimeLoadPolicy", "")) != "atomic-authored-issue-order-without-callback-dispatch"
		or String(lifecycle.get("runtimeResetPolicy", "")) != "atomic-clear-without-synthetic-unload-dispatch"
	):
		return _fail("Palantir external movie native lifecycle evidence changed")
	native_external_reset_order.assign(["HeroSelectUI", "helpBox", "planningModeUI"])
	var flagged_value: Variant = (diagnostics_value as Dictionary).get("flaggedNullClipActionPointers", [])
	if typeof(flagged_value) != TYPE_ARRAY or (flagged_value as Array).size() != 1:
		return _fail("Palantir HeroSelect flagged-null evidence is missing")
	var flagged_value_row: Variant = (flagged_value as Array)[0]
	if typeof(flagged_value_row) != TYPE_DICTIONARY:
		return _fail("Palantir HeroSelect flagged-null evidence is invalid")
	var flagged := flagged_value_row as Dictionary
	if (
		String(flagged.get("movie", "")) != "InGameHeroSelect"
		or String(flagged.get("sourceVirtualPath", "")) != "InGameHeroSelect.apt"
		or int(flagged.get("sourceOffset", -1)) != 166756
		or int(flagged.get("flags", -1)) != 182
		or int(flagged.get("clipActionsOffset", -1)) != 0
		or String(flagged.get("recordSha256", "")) != "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135"
	):
		return _fail("Palantir HeroSelect flagged-null evidence changed")
	return true


func _validate_external_movie_capture_blocker(blocker: Dictionary) -> bool:
	var gates_value: Variant = blocker.get("gates", [])
	if typeof(gates_value) != TYPE_ARRAY or (gates_value as Array).size() != EXTERNAL_MOVIE_GATE_IDS.size():
		return false
	var ids: Array[String] = []
	for gate_value in gates_value as Array:
		if typeof(gate_value) != TYPE_DICTIONARY:
			return false
		var gate := gate_value as Dictionary
		if String(gate.get("id", "")) == "" or String(gate.get("trace", "")) == "":
			return false
		ids.append(String(gate.id))
	return (
		String(blocker.get("movie", "")) == "Palantir"
		and int(blocker.get("gateCount", -1)) == 4
		and ids == EXTERNAL_MOVIE_GATE_IDS
		and blocker.get("targets", []) == ["SpellBookUI", "helpBox", "HeroSelectUI", "planningModeUI"]
		and _integer_array_matches(blocker.get("loadOrder", []), [0, 2, 3, 4])
		and not bool(blocker.get("heroInitialVisibilityGuessed", true))
		and not bool(blocker.get("asyncCompletionOrderGuessed", true))
		and not bool(blocker.get("unloadOrderGuessed", true))
		and not bool(blocker.get("parityReady", true))
	)


func _validate_resource_flash(value: Variant, timeline_ids: Dictionary, action_script_ids: Dictionary) -> bool:
	resource_flash_ready = false
	if typeof(value) != TYPE_DICTIONARY or (value as Dictionary).is_empty():
		return not action_script_ids.has("palantir:332504")
	if not timeline_ids.has("palantir:309") or not action_script_ids.has("palantir:332504"):
		return _fail("Palantir resource-flash timeline or action is missing")
	var contract := value as Dictionary
	var typed_value: Variant = contract.get("typedInput", {})
	var visual_value: Variant = contract.get("visual", {})
	var entry_value: Variant = contract.get("entryAction", {})
	var audio_value: Variant = contract.get("audioEventIntent", {})
	var policy_value: Variant = contract.get("runtimePolicy", {})
	if (
		typeof(typed_value) != TYPE_DICTIONARY
		or typeof(visual_value) != TYPE_DICTIONARY
		or typeof(entry_value) != TYPE_DICTIONARY
		or typeof(audio_value) != TYPE_DICTIONARY
		or typeof(policy_value) != TYPE_DICTIONARY
	):
		return _fail("Palantir resource-flash contract is invalid")
	var typed := typed_value as Dictionary
	var effect_value: Variant = typed.get("effect", {})
	var visual := visual_value as Dictionary
	var entry := entry_value as Dictionary
	var audio := audio_value as Dictionary
	var policy := policy_value as Dictionary
	var stopped_value: Variant = visual.get("stoppedFrame", {})
	var entry_frame_value: Variant = visual.get("entryFrame", {})
	var return_value: Variant = visual.get("returnFrame", {})
	if (
		typeof(stopped_value) != TYPE_DICTIONARY
		or typeof(entry_frame_value) != TYPE_DICTIONARY
		or typeof(return_value) != TYPE_DICTIONARY
	):
		return _fail("Palantir resource-flash frame evidence is invalid")
	var stopped := stopped_value as Dictionary
	var entry_frame := entry_frame_value as Dictionary
	var returned := return_value as Dictionary
	if (
		String(typed.get("receiver", "")) != "Palantir root"
		or String(typed.get("method", "")) != "PlayCommandPointEffect"
		or typed.get("arguments", []) != []
		or int(typed.get("bodyOffset", -1)) != 361152
		or int(typed.get("bodyByteLength", -1)) != 16
		or String(typed.get("bodySha256", "")) != RESOURCE_FLASH_TRIGGER_BODY_SHA256
		or typeof(effect_value) != TYPE_DICTIONARY
		or (effect_value as Dictionary) != {"target": "CommandPointsFlash", "method": "gotoAndPlay", "arguments": ["_go"]}
		or String(visual.get("instanceName", "")) != "CommandPointsFlash"
		or String(visual.get("timelineId", "")) != "palantir:309"
		or int(visual.get("characterId", -1)) != 309
		or String(visual.get("placementPath", "")) != "layer:1:Palantir/148"
		or String(visual.get("placementSha256", "")) != RESOURCE_FLASH_PLACEMENT_SHA256
		or int(visual.get("frameCount", -1)) != 58
		or int(visual.get("millisecondsPerFrame", -1)) != 33
		or String(visual.get("timelineSha256", "")) != RESOURCE_FLASH_TIMELINE_SHA256
		or int(stopped.get("index", -1)) != 0
		or String(stopped.get("label", "")) != "_stop"
		or String(stopped.get("script", "")) != "palantir:332480"
		or int(entry_frame.get("index", -1)) != 8
		or String(entry_frame.get("label", "")) != "_go"
		or String(entry_frame.get("script", "")) != "palantir:332504"
		or int(returned.get("index", -1)) != 57
		or String(returned.get("script", "")) != "palantir:358480"
		or int(visual.get("entryToReturnIntervals", -1)) != 49
		or int(visual.get("authoredFrameIntervalSpanMilliseconds", -1)) != 1617
		or String(visual.get("retriggerPolicy", "")) != "rewind-one-placed-instance-to-entry-frame"
		or String(entry.get("scriptId", "")) != "palantir:332504"
		or not _integer_array_matches(entry.get("instructionRange", []), [370752, 370778])
		or int(entry.get("byteLength", -1)) != 26
		or String(entry.get("sha256", "")) != String(TYPED_RESOURCE_FLASH_PROGRAM.sha256)
		or entry.get("effectsInAuthoredOrder", []) != ((_action_scripts["palantir:332504"] as Dictionary).get("effects", []))
		or String(audio.get("eventId", "")) != String(TYPED_RESOURCE_FLASH_PROGRAM.eventId)
		or String(audio.get("dispatch", "")) != "FSCommand:PlaySound"
		or String(audio.get("nativeHandlerSha256", "")) != "d7552e58b40a463b9f39d1cb6a3fa92dd0a6d8c0014fbc5234380865c447c6da"
		or String(audio.get("leafSha256", "")) != "f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350"
		or not is_equal_approx(float(audio.get("leafDurationSeconds", -1.0)), 2.130408163265306)
		or int(audio.get("requestMode", -1)) != 2
		or bool(audio.get("existingVoiceSuppressionInHandler", true))
		or bool(policy.get("nativeCounterAutoTriggerBound", true))
		or bool(policy.get("mixerOverlapPolicyBound", true))
		or bool(policy.get("genericDispatchAllowed", true))
		or bool(policy.get("fallbackAllowed", true))
	):
		return _fail("Palantir resource-flash exact contract changed")
	resource_flash_ready = true
	return true


func _validate_resource_flash_trigger_blocker(blocker: Dictionary) -> bool:
	return (
		String(blocker.get("movie", "")) == "Palantir"
		and String(blocker.get("method", "")) == "PlayCommandPointEffect"
		and blocker.get("nativeAptStubRange", []) == ["0x007fe9bb", "0x007fe9da"]
		and String(blocker.get("nativeAptStubSha256", "")) == "0db675e029ff06307ba4b9185ffed58c6adbf316667c1b3a62232089f4acb55d"
		and blocker.get("callerVas", []) == ["0x006d48e3", "0x006d4a02"]
		and String(blocker.get("gate", "")) == "semantic names of the two stripped native counters"
		and not bool(blocker.get("autoTriggerBound", true))
		and not bool(blocker.get("parityReady", true))
	)


func _validate_resource_flash_mixer_blocker(blocker: Dictionary) -> bool:
	return (
		String(blocker.get("movie", "")) == "Palantir"
		and String(blocker.get("eventId", "")) == String(TYPED_RESOURCE_FLASH_PROGRAM.eventId)
		and is_equal_approx(float(blocker.get("leafDurationSeconds", -1.0)), 2.130408163265306)
		and String(blocker.get("gate", "")) == "two requests less than one leaf duration apart"
		and blocker.get("requiredObservations", []) == ["vslot-0x64-request-objects", "returned-voice-handles", "audible-mixer-result"]
		and not bool(blocker.get("mixerOverlapPolicyBound", true))
		and not bool(blocker.get("parityReady", true))
	)


func _bind_external_movie_slots() -> bool:
	if _staged_external_movie_slots.size() != EXTERNAL_MOVIE_SLOT_SPECS.size():
		return _fail("Palantir external movie slots were not staged atomically")
	var prepared: Array[Node2D] = []
	for state in _staged_external_movie_slots:
		var target := String(state.target)
		if target == "" or has_node(NodePath(target)):
			for prepared_node in prepared:
				prepared_node.free()
			return _fail("Palantir external movie target collides with an existing child")
		var matrix := state.matrix as Array
		var translation := state.translation as Array
		var node := Node2D.new()
		node.name = target
		node.transform = Transform2D(
			Vector2(float(matrix[0]), float(matrix[1])),
			Vector2(float(matrix[2]), float(matrix[3])),
			Vector2(float(translation[0]), float(translation[1]))
		)
		node.z_index = int(state.depth)
		node.visible = false
		node.set_meta("retail_external_movie_slot", state.duplicate(true))
		prepared.append(node)
	for index in prepared.size():
		var node := prepared[index]
		var state := _staged_external_movie_slots[index].duplicate(true)
		add_child(node)
		state["nodePath"] = String(node.get_path())
		_external_movie_nodes.append(node)
		_external_movie_slots[String(state.target)] = state
	external_movie_slot_count = _external_movie_slots.size()
	external_movie_slots_ready = external_movie_slot_count == EXTERNAL_MOVIE_SLOT_SPECS.size()
	return external_movie_slots_ready


func external_movie_slot_state(target: String) -> Dictionary:
	var value: Variant = _external_movie_slots.get(target, {})
	return (value as Dictionary).duplicate(true) if typeof(value) == TYPE_DICTIONARY else {}


func _validate_frame_selection(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Palantir bounded frame selection is missing")
	var selection := value as Dictionary
	if (
		String(selection.get("policy", "")) != "bounded-retail-initial-setup-plus-men-fords-side-fade"
		or bool(selection.get("actionScriptVmUsed", true))
		or String(selection.get("unknownStatePolicy", "")) != "fail-closed"
	):
		return _fail("Palantir bounded frame selection policy is invalid")
	var palantir_value: Variant = selection.get("palantir", {})
	if typeof(palantir_value) != TYPE_DICTIONARY:
		return _fail("Palantir initial frame selection is missing")
	var palantir := palantir_value as Dictionary
	if (
		String(palantir.get("initialSetupState", "")) != "_good"
		or String(palantir.get("selectedVariant", "")) != "good-double"
		or int(palantir.get("rootCharacterId", -1)) != 105
		or int(palantir.get("selectedFrameIndex", -1)) != 19
		or int(palantir.get("localImportCharacterId", -1)) != 102
		or String(palantir.get("importMovie", "")) != "PalantirExport"
		or String(palantir.get("importSymbol", "")) != "PalantirFrame_GoodDouble"
		or int(palantir.get("exportCharacterId", -1)) != 19
	):
		return _fail("Palantir initial frame selection is not the proven retail _good variant")
	var body_value: Variant = palantir.get("initialSetupBody", {})
	if typeof(body_value) != TYPE_DICTIONARY:
		return _fail("Palantir InitialSetup bytecode evidence is missing")
	var body := body_value as Dictionary
	var range_value: Variant = body.get("byteRange", [])
	if (
		typeof(range_value) != TYPE_ARRAY
		or (range_value as Array).size() != 2
		or int((range_value as Array)[0]) != 364716
		or int((range_value as Array)[1]) != 365033
		or int(body.get("byteLength", -1)) != 317
		or String(body.get("sha256", "")) != "55735eb6de14ebf8e03267e14bb52feea4ff51b6dca64d3bba731a450a8e74d6"
	):
		return _fail("Palantir InitialSetup bytecode evidence changed")
	var side_value: Variant = selection.get("inGameSideCommandBar", {})
	if typeof(side_value) != TYPE_DICTIONARY:
		return _fail("Palantir side command bar initial state is missing")
	var side := side_value as Dictionary
	var translation_value: Variant = side.get("buttonSetTranslation", [])
	if typeof(translation_value) != TYPE_ARRAY or (translation_value as Array).size() != 2:
		return _fail("Palantir side command bar translation is invalid")
	var translation := translation_value as Array
	if (
		String(side.get("initialState", "")) != "hidden-offscreen"
		or int(side.get("initialFrameIndex", -1)) != 0
		or int(side.get("hiddenLabelFrameIndex", -1)) != 1
		or int(side.get("settledFrameIndex", -1)) != 10
		or int(side.get("fadeInLabelFrameIndex", -1)) != 11
		or bool(side.get("fadeInApplied", true))
		or not bool(side.get("selectionDrivenFadeInBound", false))
		or String(side.get("fadeRuntimeContract", "")) != "sideCommandFadeRuntime"
		or not is_equal_approx(float(translation[0]), 1048.300048828125)
		or not is_equal_approx(float(translation[1]), 361.29998779296875)
	):
		return _fail("Palantir side command bar state was guessed or changed")
	initial_frame_variant = "good-double"
	side_command_bar_initial_state = "hidden-offscreen"
	return true


func _validate_selection_blockers(blockers: Dictionary, timeline_ids: Dictionary) -> bool:
	var variants := blockers["palantir-nondefault-frame-selection-not-bound"] as Dictionary
	var unbound_value: Variant = variants.get("unboundStates", [])
	if (
		String(variants.get("movie", "")) != "Palantir"
		or String(variants.get("appliedState", "")) != "_good"
		or typeof(unbound_value) != TYPE_ARRAY
		or unbound_value != ["_evil", "_evilSingle", "_goodSingle"]
	):
		return _fail("Palantir non-default selection blocker is invalid")
	var playback := blockers["timeline-playback-not-bound"] as Dictionary
	var ids_value: Variant = playback.get("timelineIds", [])
	if (
		String(playback.get("movie", "")) != "APT closure"
		or String(playback.get("selectionPolicy", "")) != "static-selected-frames-only"
		or typeof(ids_value) != TYPE_ARRAY
		or (ids_value as Array).size() != timeline_ids.size()
	):
		return _fail("Palantir timeline playback blocker is invalid")
	for timeline_id in ids_value as Array:
		if not timeline_ids.has(String(timeline_id)):
			return _fail("Palantir timeline playback blocker lost an exact timeline")
	return true


func _validate_text_capture_blocker(blocker: Dictionary) -> bool:
	var ids_value: Variant = blocker.get("textCharacterIds", [])
	var gates_value: Variant = blocker.get("gates", [])
	var resolution_value: Variant = blocker.get("resolution", [])
	if (
		typeof(ids_value) != TYPE_ARRAY
		or (ids_value as Array).size() != 3
		or int((ids_value as Array)[0]) != 130
		or int((ids_value as Array)[1]) != 132
		or int((ids_value as Array)[2]) != 134
		or typeof(gates_value) != TYPE_ARRAY
		or (gates_value as Array).size() != 7
		or typeof(resolution_value) != TYPE_ARRAY
		or (resolution_value as Array).size() != 2
		or int((resolution_value as Array)[0]) != 1024
		or int((resolution_value as Array)[1]) != 768
	):
		return false
	var expected_gates := [
		"font-size-device-mapping",
		"baseline-and-glyph-origin",
		"antialiasing-and-cff-hinting",
		"final-color-and-alpha-blend",
		"ancestor-clipping",
		"final-composite-order",
		"runtime-font-winner",
	]
	for index in expected_gates.size():
		if String((gates_value as Array)[index]) != expected_gates[index]:
			return false
	return (
		String(blocker.get("movie", "")) == "Palantir"
		and String(blocker.get("fontId", "")) == "palantir:63"
		and int(blocker.get("gateCount", -1)) == 7
		and bool(blocker.get("retailVsGodotCaptureRequired", false))
		and not bool(blocker.get("fallbackAllowed", true))
		and not bool(blocker.get("parityReady", true))
	)


func _validate_text_and_buttons(document: Dictionary, pack_root: String) -> bool:
	var fonts_value: Variant = document.get("fonts", [])
	var texts_value: Variant = document.get("texts", [])
	var text_instances_value: Variant = document.get("textInstances", [])
	var buttons_value: Variant = document.get("buttons", [])
	var button_instances_value: Variant = document.get("buttonInstances", [])
	if (
		typeof(fonts_value) != TYPE_ARRAY
		or typeof(texts_value) != TYPE_ARRAY
		or typeof(text_instances_value) != TYPE_ARRAY
		or typeof(buttons_value) != TYPE_ARRAY
		or typeof(button_instances_value) != TYPE_ARRAY
	):
		return _fail("Palantir text/button inventories are invalid")
	var fonts := fonts_value as Array
	var texts := texts_value as Array
	var text_instances := text_instances_value as Array
	var buttons := buttons_value as Array
	var button_instances := button_instances_value as Array
	if fonts.is_empty() or fonts.size() > MAX_TEXTS or texts.is_empty() or texts.size() > MAX_TEXTS:
		return _fail("Palantir font/text inventory is empty or exceeds bounds")
	if buttons.is_empty() or buttons.size() > MAX_BUTTONS or button_instances.size() > MAX_BUTTONS:
		return _fail("Palantir button inventory is empty or exceeds bounds")
	for font_value in fonts:
		if typeof(font_value) != TYPE_DICTIONARY:
			return _fail("Palantir font definition is invalid")
		var font := font_value as Dictionary
		var font_id := String(font.get("fontId", ""))
		var glyphs_value: Variant = font.get("glyphCharacterIds", [])
		if (
			font_id == ""
			or _fonts.has(font_id)
			or String(font.get("movie", "")) == ""
			or int(font.get("characterId", -1)) < 0
			or int(font.get("sourceOffset", -1)) < 0
			or int(font.get("definitionByteLength", -1)) != 20
			or not _is_sha256(String(font.get("definitionSha256", "")))
			or String(font.get("name", "")) == ""
			or typeof(glyphs_value) != TYPE_ARRAY
			or int(font.get("glyphCount", -1)) != (glyphs_value as Array).size()
			or bool(font.get("fontPayloadContained", false)) != ((glyphs_value as Array).size() > 0)
		):
			return _fail("Palantir font identity or glyph inventory changed")
		for glyph in glyphs_value as Array:
			if int(glyph) < 0:
				return _fail("Palantir font glyph reference is invalid")
		embedded_font_glyph_count += (glyphs_value as Array).size()
		var normalized_font := font.duplicate(true)
		if (glyphs_value as Array).is_empty():
			var runtime_font := _load_exact_external_font(font, pack_root)
			if runtime_font == null:
				return false
			normalized_font["fontRuntime"] = runtime_font
		_fonts[font_id] = normalized_font
	font_count = fonts.size()
	for text_value in texts:
		if typeof(text_value) != TYPE_DICTIONARY:
			return _fail("Palantir text definition is invalid")
		var text := text_value as Dictionary
		var text_id := String(text.get("textId", ""))
		var bounds_value: Variant = text.get("bounds", [])
		if (
			text_id == ""
			or _texts.has(text_id)
			or String(text.get("movie", "")) == ""
			or int(text.get("characterId", -1)) < 0
			or int(text.get("sourceOffset", -1)) < 0
			or int(text.get("definitionByteLength", -1)) != 60
			or not _is_sha256(String(text.get("definitionSha256", "")))
			or not _fonts.has(String(text.get("fontId", "")))
			or not _finite_number_array(bounds_value, 4)
			or int(text.get("alignmentCode", -1)) not in [0, 1, 2]
			or _color(text.get("color", [])).a < 0.0
			or not is_finite(float(text.get("fontHeight", -1.0)))
			or float(text.get("fontHeight", -1.0)) <= 0.0
			or String(text.get("contentPolicy", "")) not in ["dynamic-variable", "static-placeholder"]
		):
			return _fail("Palantir text identity, font, or layout changed")
		var bounds := bounds_value as Array
		if float(bounds[2]) < float(bounds[0]) or float(bounds[3]) < float(bounds[1]):
			return _fail("Palantir text bounds are inverted")
		if String(text.get("contentPolicy", "")) == "dynamic-variable" and String(text.get("variableName", "")) == "":
			return _fail("Palantir dynamic text lost its variable name")
		_texts[text_id] = text
	text_count = texts.size()
	for instance_value in text_instances:
		if typeof(instance_value) != TYPE_DICTIONARY:
			return _fail("Palantir text instance is invalid")
		var instance := instance_value as Dictionary
		var text_id := String(instance.get("textId", ""))
		var transformed_bounds := _vector2_array(instance.get("transformedBounds", []))
		if (
			not _texts.has(text_id)
			or String(instance.get("path", "")) == ""
			or not _finite_number_array(instance.get("matrix", []), 4)
			or not _finite_number_array(instance.get("translation", []), 2)
			or not _finite_number_array(instance.get("tint", []), 4)
			or not _finite_number_array(instance.get("additive", []), 4)
			or transformed_bounds.size() != 4
			or _color(instance.get("transformedColor", [])).a < 0.0
		):
			return _fail("Palantir text instance transform is invalid")
		var source_value: Variant = instance.get("runtimeSource", {})
		if typeof(source_value) != TYPE_DICTIONARY:
			return _fail("Palantir text runtime source is invalid")
		var source := source_value as Dictionary
		if String((_texts[text_id] as Dictionary).get("contentPolicy", "")) == "dynamic-variable":
			if not _validate_dynamic_text_source(source, _texts[text_id] as Dictionary):
				return false
		var normalized_instance := instance.duplicate(true)
		normalized_instance["displayKind"] = "text"
		if not _register_display_item(normalized_instance):
			return false
	text_instance_count = text_instances.size()
	for button_value in buttons:
		if typeof(button_value) != TYPE_DICTIONARY or not _validate_button(button_value as Dictionary):
			return false
	button_count = buttons.size()
	for instance_value in button_instances:
		if typeof(instance_value) != TYPE_DICTIONARY or not _validate_button_instance(instance_value as Dictionary):
			return false
	button_instance_count = button_instances.size()
	return true


func _load_exact_external_font(font: Dictionary, pack_root: String) -> FontFile:
	var value: Variant = font.get("externalFont", {})
	if typeof(value) != TYPE_DICTIONARY:
		_fail("Palantir external Albertus binding is missing")
		return null
	var binding := value as Dictionary
	if (
		String(font.get("fontId", "")) != "palantir:63"
		or String(font.get("name", "")) != "Albertus MT"
		or String(binding.get("fontId", "")) != "palantir:63"
		or String(binding.get("fontName", "")) != "Albertus MT"
		or String(binding.get("resourceId", "")) != "men-hud-font-albertus-mt"
		or String(binding.get("sourceVirtualPath", "")) != "albertusmt.otf"
		or String(binding.get("cookedFont", "")) != "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf"
		or String(binding.get("sourceSha256", "")) != "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
		or int(binding.get("byteLength", -1)) != 24712
		or String(binding.get("family", "")) != "Albertus MT"
		or String(binding.get("subfamily", "")) != "Regular"
		or String(binding.get("postScriptName", "")) != "AlbertusMT"
		or String(binding.get("outlineFormat", "")) != "CFF"
		or int(binding.get("unitsPerEm", -1)) != 1000
		or int(binding.get("glyphCount", -1)) != 298
		or int(binding.get("embeddedBitmapStrikeCount", -1)) != 0
		or String(binding.get("runtimeLoading", "")) != "sha256-verified-fontfile"
		or bool(binding.get("fallbackAllowed", true))
	):
		_fail("Palantir external Albertus binding changed")
		return null
	var mod_loader = get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		_fail("HUD APT runtime requires the ModLoader autoload")
		return null
	var path: String = mod_loader.resolve_pack_path(pack_root, String(binding.cookedFont))
	if not _safe_file(pack_root, path, "otf"):
		_fail("Palantir external Albertus font is missing or escaped")
		return null
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != int(binding.byteLength):
		_fail("Palantir external Albertus font length changed")
		return null
	file.close()
	if FileAccess.get_sha256(path).to_lower() != String(binding.sourceSha256):
		_fail("Palantir external Albertus font SHA-256 changed")
		return null
	var runtime_font := FontFile.new()
	if runtime_font.load_dynamic_font(path) != OK:
		_fail("Palantir external Albertus font could not be loaded")
		return null
	return runtime_font


func _register_display_item(row: Dictionary) -> bool:
	var order := int(row.get("displayOrder", -1))
	if order < 0 or _display_orders.has(order):
		return _fail("Palantir unified display-list order changed")
	_display_orders[order] = true
	_display_items.append(row)
	return true


func _validate_dynamic_text_source(source: Dictionary, text: Dictionary) -> bool:
	var program_id := String(source.get("programId", ""))
	var clip_action_id := String(source.get("clipActionId", ""))
	var program_value: Variant = _clip_action_programs.get(program_id, {})
	if (
		String(source.get("kind", "")) != "initialize-string-member"
		or String(source.get("variableName", "")) != String(text.get("variableName", ""))
		or String(source.get("initialValue", "")) == ""
		or String(source.get("sourceMovie", "")) == ""
		or String(source.get("sourceInstanceName", "")) == ""
		or String(source.get("sourcePath", "")) == ""
		or not _clip_actions.has(clip_action_id)
		or typeof(program_value) != TYPE_DICTIONARY
		or not _is_sha256(String(source.get("programSha256", "")))
		or String(source.get("programSha256", "")) != String((program_value as Dictionary).get("sha256", ""))
		or not bool(source.get("localizationOrLiveValueBound", false))
		or bool(source.get("fallbackAllowed", true))
	):
		return _fail("Palantir dynamic text source proof changed")
	var instructions_value: Variant = (program_value as Dictionary).get("instructions", [])
	if typeof(instructions_value) != TYPE_ARRAY or (instructions_value as Array).size() != 4:
		return _fail("Palantir dynamic text source instruction count changed")
	var instructions := instructions_value as Array
	var names: Array[String] = []
	for instruction_value in instructions:
		if typeof(instruction_value) != TYPE_DICTIONARY:
			return _fail("Palantir dynamic text source instruction is invalid")
		names.append(String((instruction_value as Dictionary).get("name", "")))
	if names != ["push-this-variable", "push-string", "set-string-member", "end"]:
		return _fail("Palantir dynamic text source opcode sequence changed")
	if (
		String((instructions[1] as Dictionary).get("operand", "")) != String(text.get("variableName", ""))
		or String((instructions[2] as Dictionary).get("operand", "")) != String(source.get("initialValue", ""))
	):
		return _fail("Palantir dynamic text source operands changed")
	var effects_value: Variant = (program_value as Dictionary).get("effects", [])
	if (
		typeof(effects_value) != TYPE_ARRAY
		or (effects_value as Array).size() != 1
		or typeof((effects_value as Array)[0]) != TYPE_DICTIONARY
	):
		return _fail("Palantir live text typed effect is missing")
	var effect := (effects_value as Array)[0] as Dictionary
	if (
		String(effect.get("kind", "")) != "bind-live-text"
		or String(effect.get("targetMember", "")) != String(source.get("variableName", ""))
		or String(effect.get("aptVariable", "")) != String(source.get("initialValue", ""))
		or effect.get("runtimeInputs", []) != source.get("runtimeInputs", [])
		or String(effect.get("formatter", "")) != String(source.get("formatter", ""))
	):
		return _fail("Palantir live text binding changed")
	return true


func _validate_button(button: Dictionary) -> bool:
	var button_id := String(button.get("buttonId", ""))
	var vertices := _vector2_array(button.get("vertices", []))
	var triangles_value: Variant = button.get("triangles", [])
	var records_value: Variant = button.get("records", [])
	var actions_value: Variant = button.get("actions", [])
	if (
		button_id == ""
		or _buttons.has(button_id)
		or String(button.get("movie", "")) == ""
		or int(button.get("characterId", -1)) < 0
		or int(button.get("sourceOffset", -1)) < 0
		or int(button.get("definitionByteLength", -1)) != 60
		or not _is_sha256(String(button.get("definitionSha256", "")))
		or not _finite_number_array(button.get("bounds", []), 4)
		or vertices.size() < 3
		or typeof(triangles_value) != TYPE_ARRAY
		or (triangles_value as Array).is_empty()
		or typeof(records_value) != TYPE_ARRAY
		or (records_value as Array).is_empty()
		or typeof(actions_value) != TYPE_ARRAY
		or String(button.get("visualStatePolicy", "")) != "source-records-only"
		or String(button.get("eventPolicy", "")) != "source-actions-only"
	):
		return _fail("Palantir button identity or typed inventory changed")
	for triangle_value in triangles_value as Array:
		if typeof(triangle_value) != TYPE_ARRAY or (triangle_value as Array).size() != 3:
			return _fail("Palantir button triangle is invalid")
		for index in triangle_value as Array:
			if int(index) < 0 or int(index) >= vertices.size():
				return _fail("Palantir button triangle index is out of bounds")
	var hit_count := 0
	for record_value in records_value as Array:
		if typeof(record_value) != TYPE_DICTIONARY:
			return _fail("Palantir button state record is invalid")
		var record := record_value as Dictionary
		var states_value: Variant = record.get("states", [])
		if (
			int(record.get("recordIndex", -1)) < 0
			or int(record.get("sourceOffset", -1)) < 0
			or int(record.get("stateMask", 0)) <= 0
			or typeof(states_value) != TYPE_ARRAY
			or (states_value as Array).is_empty()
			or not _finite_number_array(record.get("matrix", []), 4)
			or not _finite_number_array(record.get("translation", []), 2)
			or not _finite_number_array(record.get("color", []), 4)
			or not _finite_number_array(record.get("unknown", []), 4)
		):
			return _fail("Palantir button state record changed")
		for state in states_value as Array:
			if String(state) not in ["up", "over", "down", "hit"]:
				return _fail("Palantir button state name changed")
		if (states_value as Array).has("hit"):
			hit_count += 1
	if hit_count != 1:
		return _fail("Palantir button hit state is not unique")
	button_action_count += (actions_value as Array).size()
	_buttons[button_id] = button
	return true


func _validate_button_instance(instance: Dictionary) -> bool:
	var button_id := String(instance.get("buttonId", ""))
	var definition_value: Variant = _buttons.get(button_id, {})
	var vertices := _vector2_array(instance.get("hitVertices", []))
	var triangles_value: Variant = instance.get("hitTriangles", [])
	var bindings_value: Variant = instance.get("eventBindings", [])
	if (
		typeof(definition_value) != TYPE_DICTIONARY
		or String(instance.get("path", "")) == ""
		or not _finite_number_array(instance.get("matrix", []), 4)
		or not _finite_number_array(instance.get("translation", []), 2)
		or not _finite_number_array(instance.get("tint", []), 4)
		or not _finite_number_array(instance.get("additive", []), 4)
		or typeof(instance.get("hitTransform", {})) != TYPE_DICTIONARY
		or vertices.size() != ((definition_value as Dictionary).get("vertices", []) as Array).size()
		or typeof(triangles_value) != TYPE_ARRAY
		or triangles_value != (definition_value as Dictionary).get("triangles", [])
		or typeof(bindings_value) != TYPE_ARRAY
	):
		return _fail("Palantir button instance transform or hit mesh changed")
	var hit_transform := instance.get("hitTransform", {}) as Dictionary
	if not _finite_number_array(hit_transform.get("matrix", []), 4) or not _finite_number_array(hit_transform.get("translation", []), 2):
		return _fail("Palantir button hit transform is invalid")
	return true


func _validate_draw(row: Dictionary, pack_root: String) -> bool:
	var kind := String(row.get("kind", ""))
	if not ["solid-triangle", "textured-triangle"].has(kind):
		return _fail("Palantir draw kind is unsupported")
	var points := _vector2_array(row.get("points", []))
	var color := _color(row.get("color", []))
	if points.size() != 3 or color.a < 0.0:
		return _fail("Palantir draw geometry or color is invalid")
	var normalized := row.duplicate(true)
	normalized["pointsRuntime"] = points
	normalized["colorRuntime"] = color
	if kind == "textured-triangle":
		var uvs := _vector2_array(row.get("uvs", []))
		if uvs.size() != 3:
			return _fail("Palantir textured draw UVs are invalid")
		var relative := String(row.get("atlas", ""))
		var mod_loader = get_node_or_null("/root/ModLoader")
		if mod_loader == null:
			return _fail("HUD APT runtime requires the ModLoader autoload")
		var path: String = mod_loader.resolve_pack_path(pack_root, relative)
		if not _safe_file(pack_root, path, "png") or not _is_sha256(String(row.get("atlasSha256", ""))):
			return _fail("Palantir textured draw atlas is missing, escaped, or unattested")
		var texture: Texture2D = _textures.get(path)
		if texture == null:
			texture = ASSET_FACTORY.load_texture_asset(path)
			if texture == null:
				return _fail("Palantir retail atlas could not be loaded")
			_textures[path] = texture
		# Keep a validated relative-path alias for consumers which need to bind
		# an exact source atlas as a single control-bar shell. The absolute key
		# remains the renderer's canonical cache key.
		_textures[relative] = texture
		normalized["uvsRuntime"] = uvs
		normalized["textureRuntime"] = texture
	normalized["displayKind"] = "draw"
	if not _register_display_item(normalized):
		return false
	_draws.append(normalized)
	return true


func set_live_text_values(
	resources: int,
	resource_multiplier: float,
	command_points_current: int,
	command_points_cap: int
) -> bool:
	if not is_finite(resource_multiplier):
		return _fail("Palantir resource multiplier is non-finite")
	_live_text_values["$PalantirResources"] = "%d" % resources if resources >= 0 else " "
	_live_text_values["$PalantirResourceMultiplier"] = (
		" " if resource_multiplier == 1.0 else "x%s" % String.num(resource_multiplier)
	)
	if command_points_current < 0:
		_live_text_values["$PalantirCommandPoints"] = " "
	elif command_points_cap < 0:
		_live_text_values["$PalantirCommandPoints"] = "%d" % command_points_current
	else:
		_live_text_values["$PalantirCommandPoints"] = "%d/%d" % [command_points_current, command_points_cap]
	queue_redraw()
	return true


func live_text_value(apt_variable: String) -> String:
	return String(_live_text_values.get(apt_variable, ""))


func external_albertus_font() -> FontFile:
	var value: Variant = _fonts.get("palantir:63", {})
	if typeof(value) != TYPE_DICTIONARY:
		return null
	return (value as Dictionary).get("fontRuntime") as FontFile


func exact_atlas_texture(relative_path: String) -> Texture2D:
	## Exposes only an atlas that passed the scene-contract hash, containment,
	## and PNG validation performed during configure_document().
	var value: Variant = _textures.get(relative_path)
	if value == null and _declared_atlas_paths.has(relative_path):
		var mod_loader = get_node_or_null("/root/ModLoader")
		if mod_loader != null:
			var path: String = mod_loader.resolve_pack_path(_configured_pack_root, relative_path)
			if _safe_file(_configured_pack_root, path, "png"):
				var loaded := ASSET_FACTORY.load_texture_asset(path)
				if loaded != null:
					_textures[path] = loaded
					_textures[relative_path] = loaded
					value = loaded
	return value as Texture2D if value is Texture2D else null


func _draw() -> void:
	if not presentation_ready:
		return
	var scale := Vector2(size.x / _authored_resolution.x, size.y / _authored_resolution.y)
	for row in _display_items:
		if String(row.get("displayKind", "")) == "text":
			_draw_live_text(row, scale)
			continue
		var source_points := row.get("pointsRuntime", PackedVector2Array()) as PackedVector2Array
		var points := PackedVector2Array()
		for point in source_points:
			points.append(point * scale)
		var color := row.get("colorRuntime", Color.TRANSPARENT) as Color
		if String(row.get("kind", "")) == "solid-triangle":
			draw_colored_polygon(points, color)
		else:
			var colors := PackedColorArray([color, color, color])
			var uvs := row.get("uvsRuntime", PackedVector2Array()) as PackedVector2Array
			var texture := row.get("textureRuntime") as Texture2D
			draw_polygon(points, colors, uvs, texture)


func _draw_live_text(instance: Dictionary, scale: Vector2) -> void:
	var text_value: Variant = _texts.get(String(instance.get("textId", "")), {})
	if typeof(text_value) != TYPE_DICTIONARY:
		return
	var text := text_value as Dictionary
	var font_value: Variant = _fonts.get(String(text.get("fontId", "")), {})
	if typeof(font_value) != TYPE_DICTIONARY:
		return
	var font := (font_value as Dictionary).get("fontRuntime") as FontFile
	if font == null:
		return
	var source := instance.get("runtimeSource", {}) as Dictionary
	var value := String(_live_text_values.get(String(source.get("initialValue", "")), ""))
	if value == "":
		return
	var corners := _vector2_array(instance.get("transformedBounds", []))
	if corners.size() != 4:
		return
	var left := corners[0].x
	var right := corners[0].x
	var top := corners[0].y
	var bottom := corners[0].y
	for corner in corners:
		left = minf(left, corner.x)
		right = maxf(right, corner.x)
		top = minf(top, corner.y)
		bottom = maxf(bottom, corner.y)
	left *= scale.x
	right *= scale.x
	top *= scale.y
	bottom *= scale.y
	var font_size := maxi(1, int(float(text.get("fontHeight", 14.0)) * scale.y))
	var measured := font.get_string_size(value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size)
	var x := left
	match int(text.get("alignmentCode", -1)):
		0:
			x = right - measured.x
		1:
			x = left + (right - left - measured.x) * 0.5
		2:
			x = left
		_:
			return
	var y := top + (bottom - top - measured.y) * 0.5
	var origin := Vector2(float(int(x)), float(int(y)))
	var color := _color(instance.get("transformedColor", []))
	draw_string(font, origin, value, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, color)


func _vector2_array(value: Variant) -> PackedVector2Array:
	var result := PackedVector2Array()
	if typeof(value) != TYPE_ARRAY:
		return result
	for item_value in value as Array:
		if typeof(item_value) != TYPE_ARRAY or (item_value as Array).size() != 2:
			return PackedVector2Array()
		var item := item_value as Array
		var point := Vector2(float(item[0]), float(item[1]))
		if not _finite_vector(point):
			return PackedVector2Array()
		result.append(point)
	return result


func _color(value: Variant) -> Color:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 4:
		return Color(-1.0, -1.0, -1.0, -1.0)
	var row := value as Array
	var color := Color(float(row[0]), float(row[1]), float(row[2]), float(row[3]))
	if not is_finite(color.r) or not is_finite(color.g) or not is_finite(color.b) or not is_finite(color.a):
		return Color(-1.0, -1.0, -1.0, -1.0)
	if color.r < 0.0 or color.r > 1.0 or color.g < 0.0 or color.g > 1.0 or color.b < 0.0 or color.b > 1.0 or color.a < 0.0 or color.a > 1.0:
		return Color(-1.0, -1.0, -1.0, -1.0)
	return color


func _safe_file(pack_root: String, path: String, extension: String) -> bool:
	var mod_loader = get_node_or_null("/root/ModLoader")
	return (
		mod_loader != null
		and path != ""
		and path.get_extension().to_lower() == extension
		and mod_loader.path_is_within(pack_root, path)
		and FileAccess.file_exists(path)
	)


func _read_bounded_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > MAX_DOCUMENT_BYTES:
		return {}
	var value: Variant = JSON.parse_string(file.get_as_text())
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for index in value.length():
		var code := value.unicode_at(index)
		if not ((code >= 48 and code <= 57) or (code >= 97 and code <= 102)):
			return false
	return true


func _finite_vector(value: Vector2) -> bool:
	return is_finite(value.x) and is_finite(value.y)


func _finite_number_array(value: Variant, expected_size: int) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected_size:
		return false
	for item in value as Array:
		if not is_finite(float(item)):
			return false
	return true


func _number_array_matches(value: Variant, expected: Array, tolerance := 0.000000000001) -> bool:
	if not _finite_number_array(value, expected.size()):
		return false
	var actual := value as Array
	for index in expected.size():
		if absf(float(actual[index]) - float(expected[index])) > tolerance:
			return false
	return true


func _integer_dictionary_matches(value: Variant, expected: Dictionary) -> bool:
	if typeof(value) != TYPE_DICTIONARY or (value as Dictionary).size() != expected.size():
		return false
	var actual := value as Dictionary
	for key in expected:
		if not actual.has(key) or int(actual[key]) != int(expected[key]):
			return false
	return true


func _integer_array_matches(value: Variant, expected: Array) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != expected.size():
		return false
	var actual := value as Array
	for index in expected.size():
		if int(actual[index]) != int(expected[index]):
			return false
	return true


func _fail(message: String) -> bool:
	error = message
	presentation_ready = false
	parity_ready = false
	side_command_fade_runtime_ready = false
	side_command_fade_eligible = false
	_side_command_fade_state.clear()
	wnd_companion_ready = false
	wnd_typed_callback_count = 0
	_wnd_runtime = null
	_staged_wnd_runtime = null
	diagnostics.append({"code": "palantir-apt-runtime-rejected", "message": message})
	_set_runtime_metadata()
	queue_redraw()
	return false


func _reset() -> void:
	# Atomic local teardown only. Retail unload callbacks and their relative
	# ordering remain behind the external lifecycle capture blocker.
	for node in _external_movie_nodes:
		if is_instance_valid(node):
			if node.get_parent() == self:
				remove_child(node)
			node.free()
	_external_movie_nodes.clear()
	_external_movie_slots.clear()
	_staged_external_movie_slots.clear()
	contract_declared = false
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	static_subset_opt_in = false
	error = ""
	draw_count = 0
	blocker_count = 0
	timeline_count = 0
	timeline_frame_count = 0
	timeline_instance_count = 0
	action_script_count = 0
	supported_action_script_count = 0
	clip_action_program_count = 0
	supported_clip_action_program_count = 0
	clip_action_count = 0
	clip_action_event_count = 0
	executable_clip_action_event_count = 0
	initial_frame_variant = ""
	side_command_bar_initial_state = ""
	external_movie_slot_count = 0
	external_movie_slots_ready = false
	external_movie_load_order.clear()
	native_external_reset_order.clear()
	resource_flash_ready = false
	side_command_topology_ready = false
	side_command_fade_runtime_ready = false
	side_command_fade_eligible = false
	palantir_command_topology_ready = false
	wnd_companion_ready = false
	wnd_typed_callback_count = 0
	_wnd_runtime = null
	_staged_wnd_runtime = null
	_side_command_fade_accumulator = 0.0
	_side_command_fade_state.clear()
	diagnostics.clear()
	_draws.clear()
	_textures.clear()
	_action_scripts.clear()
	_clip_action_programs.clear()
	_clip_actions.clear()
	_fonts.clear()
	_texts.clear()
	_buttons.clear()
	_display_items.clear()
	_display_orders.clear()
	_configured_pack_root = ""
	_declared_atlas_paths.clear()
	_live_text_values = {
		"$PalantirResources": "0",
		"$PalantirResourceMultiplier": " ",
		"$PalantirCommandPoints": "0/0",
	}
	_set_runtime_metadata()
	queue_redraw()


func _set_runtime_metadata() -> void:
	set_meta("contract_declared", contract_declared)
	set_meta("contract_ready", contract_ready)
	set_meta("presentation_ready", presentation_ready)
	set_meta("parity_ready", parity_ready)
	set_meta("static_subset_opt_in", static_subset_opt_in)
	set_meta("draw_count", draw_count)
	set_meta("blocker_count", blocker_count)
	set_meta("timeline_count", timeline_count)
	set_meta("timeline_frame_count", timeline_frame_count)
	set_meta("timeline_instance_count", timeline_instance_count)
	set_meta("action_script_count", action_script_count)
	set_meta("supported_action_script_count", supported_action_script_count)
	set_meta("clip_action_program_count", clip_action_program_count)
	set_meta("supported_clip_action_program_count", supported_clip_action_program_count)
	set_meta("clip_action_count", clip_action_count)
	set_meta("clip_action_event_count", clip_action_event_count)
	set_meta("executable_clip_action_event_count", executable_clip_action_event_count)
	set_meta("font_count", font_count)
	set_meta("embedded_font_glyph_count", embedded_font_glyph_count)
	set_meta("text_count", text_count)
	set_meta("text_instance_count", text_instance_count)
	set_meta("button_count", button_count)
	set_meta("button_instance_count", button_instance_count)
	set_meta("button_action_count", button_action_count)
	set_meta("initial_frame_variant", initial_frame_variant)
	set_meta("side_command_bar_initial_state", side_command_bar_initial_state)
	set_meta("external_movie_slot_count", external_movie_slot_count)
	set_meta("external_movie_slots_ready", external_movie_slots_ready)
	set_meta("external_movie_load_order", external_movie_load_order.duplicate())
	set_meta("native_external_reset_order", native_external_reset_order.duplicate())
	set_meta("resource_flash_ready", resource_flash_ready)
	set_meta("side_command_topology_ready", side_command_topology_ready)
	set_meta("side_command_fade_runtime_ready", side_command_fade_runtime_ready)
	set_meta("side_command_fade_eligible", side_command_fade_eligible)
	set_meta("side_command_fade_state", _side_command_fade_state.duplicate(true))
	set_meta("palantir_command_topology_ready", palantir_command_topology_ready)
	set_meta("wnd_companion_ready", wnd_companion_ready)
	set_meta("wnd_typed_callback_count", wnd_typed_callback_count)
	set_meta("error", error)
