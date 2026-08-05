class_name RetailHud
extends Control
## Player-facing Stage 15 HUD. Diagnostics still exist, but are opt-in so the
## normal surface reads like a game rather than a proof harness.

signal pause_requested
signal restart_requested
signal main_menu_requested
signal quit_requested
signal group_recall_requested(group: int)
signal group_assign_requested(group: int)
signal train_requested(unit_id: String)
signal structure_upgrade_requested(upgrade_id: String)
signal cancel_production_requested(queue_index: int)
signal attack_move_requested
signal stop_requested
signal stance_requested
signal formation_requested
signal command_cap_changed(value: int)
signal weak_fortress_toggled(value: bool)
signal cheat_resources_requested
signal cheat_finish_work_requested
signal cheat_level_up_requested
## Playtest tools, reached from the pause screen's Playtest Tools entry.
## That entry sits behind dev_hud_enabled() with the rest of the dev
## surface: these signals drive local, unreplicated writes to hashed
## simulation state, so a build that exposed them to an ordinary player
## would hand every multiplayer peer a desync button.
signal playtest_resources_requested(amount: int)
signal playtest_power_points_requested(amount: int)
signal playtest_max_level_requested(scope: String)
signal playtest_heal_requested
signal power_purchase_requested(power_id: String, cost: int)
signal power_cast_requested(cast_kind: String)
signal ability_cast_requested(unit_id: String, ability_id: String)
signal powers_opened
signal powers_reset_requested
## Emitted whenever the spellbook orb closes (ACCEPT, backdrop, Esc, or arming
## a cast): the slice treats every close as the retail ACCEPT commit.
signal powers_closed
signal construct_requested(structure_kind: String)
signal hero_recall_requested(hero_id: int)
signal expansion_requested(expansion_kind: String)
signal battalion_upgrade_requested(upgrade_id: String)
signal music_volume_changed(value: float)
signal voice_volume_changed(value: float)
signal mute_changed(value: bool)
signal ui_sound_requested(event_id: String)

const MinimapScript = preload("res://src/retail_slice/retail_minimap.gd")
const PalantirFrameScript = preload("res://src/retail_slice/retail_palantir_frame.gd")
const AptRuntimeScript = preload("res://src/retail_slice/retail_hud_apt_runtime.gd")
const TooltipScript = preload("res://src/retail_slice/retail_tooltip.gd")
const SideCommandBarScript = preload("res://src/retail_slice/retail_side_command_bar.gd")
const PowersOrbScript = preload("res://src/retail_slice/retail_powers_orb.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const RETAIL_TOOLTIP_HOVER_DELAY := 0.4
const RETAIL_TRAIN_ICON_ID := "BGBarracks_Soldiers"
const RETAIL_TRAIN_LABEL_ID := "CONTROLBAR:ConstructGondorFighterHorde"
const RETAIL_TRAIN_TOOLTIP_ID := "CONTROLBAR:ToolTipBuildGondorFighterHorde"
const RETAIL_COMMAND_BAR_IMAGE_ID := "SGCommandBar"
const RETAIL_COMMAND_BAR_SOURCE_SIZE := Vector2i(1024, 256)
const RETAIL_PALANTIR_FRAME_ATLAS := "assets/ui/palantir/atlases/apt-palantirexport-17-fb63d3d26008.png"
const RETAIL_PALANTIR_ATLAS := "assets/ui/palantir/atlases/apt-palantir-1-d9888d52cd89.png"
const RETAIL_PALANTIR_FRAME_SOURCE_SIZE := Vector2i(384, 256)
const RETAIL_PALANTIR_FRAME_DISPLAY_SIZE := Vector2(540, 360)
const RETAIL_PALANTIR_AUTHORED_SIZE := Vector2(384, 256)
const RETAIL_PALANTIR_DISPLAY_SIZE := Vector2(880, 360)
# Regions are the exact APT atlas rectangles selected by Palantir DAT image IDs.
#
# REMOVED - `RETAIL_RADAR_PARCHMENT_REGION := Rect2(4, 4, 214, 214)`.
# That rectangle crops the big brown sphere in the TOP-LEFT of
# `apt-palantir-1-d9888d52cd89.png`, and that atlas is the SPELL / SUMMON-POWER
# sheet: starbursts, sparkle sprites and power-button badges. The sphere is the
# palantir ORB globe (the static MinLOD frame behind `GlobeSwirlRender`), and it
# was being stretched across the whole radar disc as if it were the map. That is
# the "the radar has a palantir icon over it" the owner reported: a leather ball
# where the battlefield should be.
#
# There is no authored radar-fill bitmap to swap in. Every palantir FRAME atlas
# (`apt-palantirexport-11/14/17/20`) is a ring with a fully transparent
# interior - retail composites the LIVE MAP inside the bezel rather than
# shipping a backing image. So the radar's backdrop is now the map's own
# converted preview art, bound in `configure_minimap` where that texture is
# actually known, and a map that publishes no preview falls back to the
# synthetic schematic instead of borrowing a spell sprite.
const RETAIL_EMPTY_SOCKET_REGION := Rect2(558, 23, 56, 53)
const RETAIL_ORB_REGIONS := {
	"options": Rect2(701, 133, 36, 36),
	"powers": Rect2(487, 188, 36, 36),
	"score": Rect2(627, 217, 38, 38),
}
# Bottom-left cluster geometry measured against the retail 1.06 reference
# capture (bfme2-ref-120s/135s.png, 1440p, scaled to the 1080p dock). The
# authored frame atlas renders as two separately scaled pieces in retail 16:9.
const RETAIL_RADAR_CENTER := Vector2(225, 198)
const RETAIL_RADAR_RADIUS := 181.0
const RETAIL_DISH_CENTER := Vector2(587, 219)
const RETAIL_DISH_RADIUS := 118.0
# Purchased-power dock column, measured from the retail in-game capture
# (game.dat_AMUCzAwKh9.jpg, 2560x1440): the first purchased power hangs off
# the palantir's upper-left rim, socket center (-225,-260) from the minimap
# dish center, ~110px sockets, stacking downward in MenSpellBookCommandSet
# cast-slot order. Scaled 0.75 into the 1080p dock: (-169,-195) from
# RETAIL_RADAR_CENTER, 82px stride.
const RETAIL_POWER_DOCK_FIRST_CENTER := Vector2(56, 3)
const RETAIL_POWER_DOCK_STRIDE := 82.0
const RETAIL_POWER_DOCK_SIZE := Vector2(76, 76)
# Layering matches retail: dish backing disc, dish ring (masked annulus of the
# authored ring in the frame atlas, circle center (284.5, 145.5)), then the
# radar assembly on top. The dish transform differs from the radar piece's
# uniform 1.5 exactly as the retail 16:9 composition renders it; the annulus
# keeps the ring band plus interior sheen. Final pixel-exact transform is a P1
# item (parse the dish sprite matrix from the APT timeline instances).
const RETAIL_FRAME_PIECES := [
	{"kind": "disc", "center": Vector2(587, 219), "radius": 119.0, "color": Color(0.035, 0.04, 0.03, 1.0)},
	{"kind": "dish_ring", "region": Rect2(193, 50, 190, 192), "dest": Rect2(420, 45, 346, 349),
		"annulus_center": Vector2(91.5, 95.5), "annulus_inner": 68.0, "annulus_outer": 96.0},
	{"region": Rect2(0, 0, 250, 256), "dest": Rect2(19, 6, 375, 384)},
]
const RETAIL_ORB_RECTS := {
	"options": Rect2(129, 34, 64, 64),
	"powers": Rect2(202, 18, 79, 79),
	"score": Rect2(290, 35, 64, 64),
}
# Command sockets ring the palantir dish; coordinates are relative to the
# command panel, whose origin sits at dock (360, 0). The lowest socket is
# raised from the capture-measured y=311 so its 64px button stays inside the
# 360px dock (and therefore on-screen) instead of clipping below the bottom
# edge.
const RETAIL_COMMAND_SLOT_SOURCE := [
	Vector2(150, 64), Vector2(240, 97), Vector2(309, 159),
	Vector2(315, 231), Vector2(268, 291), Vector2(148, 296),
]
const RETAIL_COMMAND_SLOT_SIZE := Vector2(64, 64)
const RETAIL_POWER_IMAGE_IDS := [
	"SBGood_RallyingCall", "SBGood_Heal", "SBGood_MenLoneTower", "SBGood_ElvenWood",
	"SBGood_ArrowVolley", "SBGood_TomBombadil", "SBGood_SummonHobbits", "SBGood_SummonDunedain",
	"SBGood_RohanAllies", "SBGood_CloudBreak", "SBGood_ArmyoftheDead", "SBGood_Earthquake",
]
const RETAIL_COMMAND_SPECS := [
	{
		"unit_id": "bfme2.object.gondor-fighter-horde",
		"button_name": "TrainSoldiers",
		"fallback_label": "Train Gondor Soldiers",
		"fallback_tooltip": "Queue one 15-member Gondor Soldier battalion",
		"image_id": "BGBarracks_Soldiers",
		"label_id": "CONTROLBAR:ConstructGondorFighterHorde",
		"tooltip_id": "CONTROLBAR:ToolTipBuildGondorFighterHorde",
	},
	{
		"unit_id": "bfme2.object.gondor-tower-guard",
		"button_name": "TrainTowerGuards",
		"fallback_label": "Train Gondor Tower Guards",
		"fallback_tooltip": "Queue one Gondor Tower Guard battalion",
		"image_id": "BGBarracks_TowerGuard",
		"label_id": "CONTROLBAR:ConstructGondorShieldGuardHorde",
		"tooltip_id": "CONTROLBAR:ToolTipBuildGondorShieldGuardHorde",
	},
	{
		"unit_id": "bfme2.object.gondor-archer",
		"button_name": "TrainArchers",
		"fallback_label": "Train Gondor Archers",
		"fallback_tooltip": "Queue one Gondor Archer battalion",
		"image_id": "BGArcheryRange_Archers",
		"label_id": "CONTROLBAR:ConstructGondorArcherHorde",
		"tooltip_id": "CONTROLBAR:ToolTipBuildGondorArcherHorde",
	},
	{
		"unit_id": "bfme2.object.gondor-knight",
		"button_name": "TrainKnights",
		"fallback_label": "Train Gondor Knights",
		"fallback_tooltip": "Queue one Gondor Knight battalion",
		"image_id": "BGStables_Knights",
		"label_id": "CONTROLBAR:ConstructGondorKnightHorde",
		"tooltip_id": "CONTROLBAR:ToolTipBuildGondorKnightHorde",
	},
]
const RETAIL_PORTRAIT_SPECS := [
	{
		"unit_id": "bfme2.object.gondor-fighter-horde",
		"image_id": "UPGondor_Soldier",
	},
	{
		"unit_id": "bfme2.object.gondor-tower-guard",
		"image_id": "UPGondor_TowerGuard",
	},
	{
		"unit_id": "bfme2.object.gondor-archer",
		"image_id": "UPGondor_Archer",
	},
	{
		"unit_id": "bfme2.object.gondor-knight",
		"image_id": "UPGondor_Knight",
	},
	{
		"unit_id": "bfme2.object.men-porter",
		"image_id": "UPGondor_Porter",
	},
]
const RANGER_COMMAND_SPEC := {
	"unit_id": "bfme2.object.gondor-ranger-horde",
	"button_name": "TrainRangers",
	"fallback_label": "Train Ithilien Rangers",
	"fallback_tooltip": "Requires a level 2 Archery Range",
	"image_id": "BGArcheryRange_Rangers",
	"label_id": "CONTROLBAR:ConstructGondorRangerHorde",
	"tooltip_id": "CONTROLBAR:ToolTipBuildGondorRangerHorde",
}
const RANGER_PORTRAIT_SPEC := {
	"unit_id": "bfme2.object.gondor-ranger-horde",
	"image_id": "UPGondor_Ranger",
}
const TREBUCHET_OBJECT_ID := "bfme2.object.gondor-trebuchet"
## Fortress expansion pad commands (REF-33/34): the fortress's authored pad
## command sets from the expansion documents, validated like any other retail
## command. Wall-hub expansion is excluded — the wall system is its own lane.
const EXPANSION_COMMAND_SPECS := {
	"arrow_tower_expansion": {
		"button_name": "BuildArrowTowerExpansion",
		"image_id": "BGFortress_ArrowTower",
		"label_id": "CONTROLBAR:ConstructMenArrowTowerExpansion",
		"tooltip_id": "CONTROLBAR:ToolTipConstructMenArrowTowerExpansion",
	},
	"trebuchet_expansion": {
		"button_name": "BuildTrebuchetExpansion",
		"image_id": "BGFortress_Trebuchet",
		"label_id": "CONTROLBAR:ConstructMenTrebuchetExpansion",
		"tooltip_id": "CONTROLBAR:ToolTipConstructMenTrebuchetExpansion",
	},
	"trebuchet_side_expansion": {
		"button_name": "BuildTrebuchetSideExpansion",
		"image_id": "BGFortress_Trebuchet",
		"label_id": "CONTROLBAR:ConstructMenTrebuchetExpansion",
		"tooltip_id": "CONTROLBAR:ToolTipConstructMenTrebuchetExpansion",
	},
	"garrison_dormitory": {
		"button_name": "BuildGarrisonDormitory",
		"image_id": "BGFortress_Dormitory",
		"label_id": "CONTROLBAR:ConstructMenDormitory",
		"tooltip_id": "CONTROLBAR:ToolTipConstructMenDormitory",
	},
}
## Forge research rides the doc-driven rows only: the compiled research
## surface (structure_upgrade_commands) carries its own pack image/label/
## tooltip/cost ids, so the hardcoded FORGE_UPGRADE_SPECS table is retired
## (nothing may emit the stale provisional ids from the UI).
const ARCHERY_LEVEL_TWO_ACTION_SPEC := {
	"action_id": "upgrade_archery_range_level2",
	"button_name": "UpgradeArcheryRangeLevel2",
	"image_id": "UCCommon_UpgradeStructureNew",
	"label_id": "CONTROLBAR:ConstructGondorArcheryRangeLevel2Upgrade",
	"tooltip_id": "CONTROLBAR:ToolTipBuildGondorArcheryRangeLevel2Upgrade",
}
const RETAIL_PORTRAIT_SOURCE_SIZE := Vector2i(191, 191)
const RETAIL_UNIT_ACTION_SPECS := [
	{
		"action_id": "attack_move",
		"button_name": "AttackMove",
		"image_id": "UCCommon_AttackMove",
		"label_id": "CONTROLBAR:AttackMove",
		"tooltip_id": "CONTROLBAR:ToolTipAttackMove",
	},
	{
		"action_id": "stop",
		"button_name": "Stop",
		"image_id": "UCCommon_Stop",
		"label_id": "CONTROLBAR:Stop",
		"tooltip_id": "CONTROLBAR:ToolTipCommandStop",
		"preferred_slot": 5,
	},
	{
		"action_id": "stance",
		"button_name": "Stance",
		"image_id": "UCCommon_BattleStance",
		"label_id": "CONTROLBAR:ToggleStanceHoldGround",
		"tooltip_id": "CONTROLBAR:ToolTipToggleStanceHoldGround",
		"preferred_slot": 0,
	},
	{
		"action_id": "formation",
		"button_name": "Formation",
		"image_id": "UCCommon_PorcupineFormation",
		"label_id": "CONTROLBAR:ToggleBlockFormation",
		"tooltip_id": "CONTROLBAR:ToolTipToggleGFighterLineToBlockFormation",
	},
	{"action_id": "construct_farm", "button_name": "BuildFarm", "image_id": "BCFarm", "label_id": "CONTROLBAR:ConstructMenFarm", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFarm"},
	{"action_id": "construct_barracks", "button_name": "BuildBarracks", "image_id": "BGBarracks", "label_id": "CONTROLBAR:ConstructMenBarracks", "tooltip_id": "CONTROLBAR:ToolTipConstructMenBarracks"},
	{"action_id": "construct_archery_range", "button_name": "BuildArcheryRange", "image_id": "BGArcheryRange", "label_id": "CONTROLBAR:ConstructMenArcheryRange", "tooltip_id": "CONTROLBAR:ToolTipMenArcheryRange"},
	{"action_id": "construct_stable", "button_name": "BuildStable", "image_id": "BGStables", "label_id": "CONTROLBAR:ConstructMenStable", "tooltip_id": "CONTROLBAR:ToolTipConstructMenStable"},
	{"action_id": "construct_fortress", "button_name": "BuildFortress", "image_id": "BGFortress", "label_id": "CONTROLBAR:ConstructMenFortress", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFortress"},
]
## Stance image/string ids from the retail UI pack (common to all factions).
## Aggressive uses the retail typo "Aggresive" in the authored texture id.
const STANCE_UI := {
	"HoldGround": {
		"image_id": "UCCommon_HoldGroundStance",
		"label_id": "CONTROLBAR:ToggleStanceHoldGround",
		"tooltip_id": "CONTROLBAR:ToolTipToggleStanceHoldGround",
		"fallback_label": "Hold Ground",
	},
	"Battle": {
		"image_id": "UCCommon_BattleStance",
		"label_id": "CONTROLBAR:ToggleStanceHoldGround",
		"tooltip_id": "CONTROLBAR:ToolTipToggleStanceHoldGround",
		"fallback_label": "Battle",
	},
	"Aggressive": {
		"image_id": "UCCommon_AggresiveStance",
		"label_id": "CONTROLBAR:ToggleStanceHoldGround",
		"tooltip_id": "CONTROLBAR:ToolTipToggleStanceHoldGround",
		"fallback_label": "Aggressive",
	},
}
const FORMATION_UI := {
	"Line": {
		"image_id": "UCCommon_PorcupineFormation",
		"label_id": "CONTROLBAR:ToggleBlockFormation",
		"tooltip_id": "CONTROLBAR:ToolTipToggleGFighterLineToBlockFormation",
		"fallback_label": "Line formation",
	},
	"Block": {
		"image_id": "UCCommon_PorcupineFormation",
		"label_id": "CONTROLBAR:ToggleBlockFormation",
		"tooltip_id": "CONTROLBAR:ToolTipToggleGFighterLineToBlockFormation",
		"fallback_label": "Block formation",
	},
}
const RETAIL_MEMBER_TO_HORDE := {
	"bfme2.object.gondor-fighter": "bfme2.object.gondor-fighter-horde",
	"bfme2.object.gondor-tower-guard": "bfme2.object.gondor-tower-guard",
	"bfme2.object.gondor-archer": "bfme2.object.gondor-archer",
	"bfme2.object.gondor-knight": "bfme2.object.gondor-knight",
	"bfme2.object.gondor-ranger": "bfme2.object.gondor-ranger-horde",
	TREBUCHET_OBJECT_ID: TREBUCHET_OBJECT_ID,
	"bfme2.object.men-porter": "bfme2.object.men-porter",
}
const MAX_RETAIL_COMMAND_ICON_BYTES := 16 * 1024 * 1024
const MAX_RETAIL_COMMAND_ICON_DIMENSION := 4096
const _MISSING_RETAIL_STRING := "\u001fopenbfme-missing-retail-string\u001f"

var minimap: RetailMinimap
var objective_label: Label
var selection_label: Label
var feedback_label: Label
var resource_label: Label
var command_points_label: Label
var train_button: Button
var train_buttons: Dictionary = {}
var hero_selection_panel: PanelContainer
var hero_selection_grid: Control
var hero_buttons: Dictionary = {}
var unit_action_buttons: Dictionary = {}
var production_queue_label: Label
var production_progress: ProgressBar
var cancel_production_button: Button
## Battalion OBJECT_UPGRADE purchase buttons keyed by upgrade id (doc-driven
## command-surface rows from the sim's battalion_upgrade_commands).
var _battalion_upgrade_buttons: Dictionary = {}
var _doc_upgrade_buttons: Dictionary = {}
var selection_portrait: TextureRect
var _selection_rank_pips: RankPipsOverlay
var synthetic_palantir_frame: RetailPalantirFrame
var retail_control_bar_frame: RetailPalantirFrame
var retail_apt_runtime: RetailHudAptRuntime
var group_buttons: Dictionary = {}
var pause_panel: PanelContainer
var playtest_panel: PanelContainer
var playtest_command_cap_slider: HSlider
var playtest_command_cap_label: Label
var failure_panel: PanelContainer
var outcome_layer: Control
var outcome_title: Label
var outcome_detail: Label
var diagnostics_panel: PanelContainer
var diagnostics_label: Label
var music_slider: HSlider
var match_clock_label: Label
var fps_toggle: CheckButton
var command_cap_slider: HSlider
var weak_fortress_toggle: CheckButton
var _side_bar_fingerprint := "<unset>"
var production_queue_buttons: Array[Button] = []
var power_points_label: Label
var powers_dock: Control
var powers_dock_buttons: Dictionary = {}
var fps_overlay: Label
var _frame_times: PackedFloat32Array = PackedFloat32Array()
var voice_slider: HSlider
var mute_toggle: CheckButton
var command_panel: PanelContainer
var command_grid: Control
var command_socket_layer: Control
var orb_buttons: Dictionary = {}
var powers_palette: PowersOrbScript
var power_buttons: Array[Button] = []
var _spellbook_power_rows: Array = []
## The spellbook document's own pack + its imageBindings: per-faction icon
## crops resolve from that pack (never the Men pack's ui_manifest for others).
var _spellbook_doc_pack_root := ""
var _spellbook_image_bindings: Dictionary = {}
var _retail_ui_font_cached: Font = null
var _last_power_states: Dictionary = {}
var score_overlay: Control
var score_labels: Dictionary = {}
var _retail_palantir_atlas: Texture2D
var resource_strip: PanelContainer
var power_orb_label: Label
var retail_train_command_bound := false
var retail_train_commands_bound := false
var retail_portraits_bound := false
var retail_control_bar_bound := false
var retail_apt_bound := false
var retail_presentation_bound := false
var private_parity_mode_active := false
var retail_train_icon_aspect_ratio := 0.0
var _retail_train_label := ""
var _retail_train_labels: Dictionary = {}
var _retail_portrait_textures: Dictionary = {}
var _built := false
var _normal_button: StyleBoxFlat
var _hover_button: StyleBoxFlat
var _pressed_button: StyleBoxFlat
var _panel: StyleBoxFlat
var _last_resources := 0
var _last_command_points := 0
var _last_command_cap := 0
var retail_tooltip: RetailTooltip
var retail_side_command_bar: RetailSideCommandBar
var _retail_command_costs: Dictionary = {}
var _retail_command_build_seconds: Dictionary = {}
var _tooltip_hover_button: Button = null
## Monotonic hover id: the pending SceneTreeTimer callback binds this int (never
## the Button object) so a freed button can never become a dangling capture.
var _tooltip_hover_token: int = 0
var _retail_command_specs: Array = RETAIL_COMMAND_SPECS.duplicate(true)
var _retail_portrait_specs: Array = RETAIL_PORTRAIT_SPECS.duplicate(true)
var _retail_action_specs: Array = RETAIL_UNIT_ACTION_SPECS.duplicate(true)
var _ranger_content_enabled := false
var _trebuchet_content_enabled := false
var _generic_playable_units: Dictionary = {}
var _generic_playable_routes: Dictionary = {}
var _generic_playable_route_validations: Dictionary = {}
var _hero_command_specs: Array[Dictionary] = []
var _hero_routes: Dictionary = {}
var _hero_route_validations: Dictionary = {}
# Hero SPECIAL_POWER abilities: converted doc rows per runtime unit id, and
# the palantir socket buttons built from them (unit_id -> ability_id -> Button).
var _hero_ability_specs: Dictionary = {}
var hero_ability_buttons: Dictionary = {}
var _retail_ability_tooltips: Dictionary = {}
## When non-empty, only these structure kinds appear as construct actions
## (radial + side command bar). Empty means "all construct_* action specs".
var _manifest_construct_kinds: Array[String] = []
var _spellbook_runtime: Dictionary = {}
var _spellbook_power_ids: Array[String] = []
## Pack binding retained so stance/formation buttons can re-resolve retail art
## after the player cycles those orders (same pack the train buttons used).
var _bound_content_db = null
var _bound_pack_root := ""
## Pack roots runtime-backed images may resolve from: the selected host pack
## plus the faction manifest's converted faction packs (supplemental packs).
var _allowed_image_pack_roots: Array[String] = []
## The faction whose construct surface was last registered ("" = Men default).
## Men-only hardcoded surfaces (forge research) scope themselves against it.
var _faction_surface := ""
var _faction_heading_label: Label = null
var _data_driven_train_surface := false
## Reviewer-visible bind diagnostics: every use of a recorded non-retail
## fallback (specs marked "authored_fallback": true) is logged here during
## bind_retail_train_commands. Missing localized strings otherwise fail closed.
var retail_bind_diagnostics: Array[String] = []
## Retail top-right event feed ("Construction Complete: X", "Insufficient
## funds."). Lines stack newest-at-bottom, alternating gold/white per entry,
## then fade out after EVENT_FEED_SECONDS.
var event_feed: VBoxContainer
var _event_feed_gold_next := false
var _retail_command_point_costs: Dictionary = {}
## Floating construction progress above building sites (REF-27 "Building: 15%").
var _construction_layer: Control
var _construction_labels: Array[Label] = []
## "Level: N" caption + thin progress bar at the bottom of the palantir dish,
## shown for the selected structure or hero (REF-25/41).
var _dish_level_label: Label
var _dish_level_bar: ProgressBar
## Recruited-hero strip bottom-center: small portraits with level badges and
## health bars (REF-24/41).
var hero_bar: HBoxContainer
var _hero_bar_buttons: Dictionary = {}
## Floating radial command buttons above the selected producer (REF-25/33/35):
## the same train/research/roster commands the palantir sockets carry, fanned
## in an arc over the building like retail.
var _radial_layer: Control
var _radial_buttons: Array[Button] = []
var _radial_fingerprint := "<unset>"
## Validated fortress expansion command presentation (icon/label/tooltip).
var _retail_expansion_validated: Dictionary = {}
## Doc-driven fortress expansion command specs discovered from the selected
## faction's playableStructure documents ({kind: spec}). EXPANSION_COMMAND_SPECS
## (the Men/Gondor localized table) still wins for the kinds it covers, so the
## Men surface is byte-identical; every other faction's pads get their own
## authored commands here instead of an empty plot menu. Registered before
## bind_retail_train_commands so the validation pass sees them.
var _manifest_expansion_specs: Dictionary = {}


func configure_manifest_expansion_commands(specs: Dictionary) -> void:
	## `specs` is {expansion_kind: {image_id, structure_object_id,
	## fallback_label, fallback_tooltip, button_name}} projected by the slice
	## from each expansion document's own authored construct route.
	_manifest_expansion_specs = specs.duplicate(true)
const EVENT_FEED_SECONDS := 6.0
const EVENT_FEED_MAX_LINES := 8
const EVENT_FEED_GOLD := Color("e9d84e")
const EVENT_FEED_WHITE := Color("f2f0e6")
## Dev console default: OFF. Flip for a dev build, or set OPENBFME_DEV_HUD=1.
const DEV_HUD_DEFAULT := false
## Retail's porter strip (REF-29/32) carries only the porter's own build menu:
## the fortress is the pre-placed starting building, and expansions ride the
## fortress/wall command sets, not the porter's.
const PORTER_STRIP_EXCLUDED_KINDS := [
	"fortress",
	"arrow_tower_expansion",
	"trebuchet_expansion",
	"trebuchet_side_expansion",
	"wall_hub_small_expansion",
]


## Owner: the porter menu is economy/production buildings ONLY — Castle Wall
## pieces, gates, fortress/citadel kinds, and every expansion are not
## available here (they also ship no icons); expansions live on the fortress
## pads' radial. Semantic per kind string so all six factions filter alike.
static func _porter_strip_excluded(kind: String) -> bool:
	if PORTER_STRIP_EXCLUDED_KINDS.has(kind):
		return true
	return (
		kind.contains("wall")
		or kind.contains("gate")
		or kind.contains("expansion")
		or kind.contains("fortress")
		or kind.contains("citadel")
	)
## Strip order measured from the retail porter build menu (REF-29/32, top to
## bottom: farm, battle tower, archery range, barracks, siege works,
## blacksmith, well, statue, stable, marketplace, wall hub). Kinds absent here
## fall back to manifest order after the evidenced ones.
const PORTER_STRIP_RETAIL_ORDER := [
	"farm",
	"battle_tower",
	"archery_range",
	"barracks",
	"workshop",
	"forge",
	"well",
	"statue",
	"stable",
	"marketplace",
	"wall_hub_small",
]

## Known Men construct button art (image/label ids from the host pack UI).
## IDs must exist in the cooked pack strings.json / ui_manifest.json — inventing
## ConstructMenForge / ToolTipConstructMenStatue etc. fail-closes the whole HUD.
const MANIFEST_CONSTRUCT_TEMPLATES := {
	"farm": {"button_name": "BuildFarm", "image_id": "BCFarm", "label_id": "CONTROLBAR:ConstructMenFarm", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFarm"},
	"barracks": {"button_name": "BuildBarracks", "image_id": "BGBarracks", "label_id": "CONTROLBAR:ConstructMenBarracks", "tooltip_id": "CONTROLBAR:ToolTipConstructMenBarracks"},
	"archery_range": {"button_name": "BuildArcheryRange", "image_id": "BGArcheryRange", "label_id": "CONTROLBAR:ConstructMenArcheryRange", "tooltip_id": "CONTROLBAR:ToolTipMenArcheryRange"},
	"stable": {"button_name": "BuildStable", "image_id": "BGStables", "label_id": "CONTROLBAR:ConstructMenStable", "tooltip_id": "CONTROLBAR:ToolTipConstructMenStable"},
	"fortress": {"button_name": "BuildFortress", "image_id": "BGFortress", "label_id": "CONTROLBAR:ConstructMenFortress", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFortress"},
	"workshop": {"button_name": "BuildWorkshop", "image_id": "BGWorkshop", "label_id": "CONTROLBAR:ConstructMenWorkshop", "tooltip_id": "CONTROLBAR:ToolTipConstructMenWorkshop"},
	"marketplace": {"button_name": "BuildMarketplace", "image_id": "BGMarketplace", "label_id": "CONTROLBAR:ConstructMenMarketPlace", "tooltip_id": "CONTROLBAR:ToolTipConstructMenMarketPlace"},
	"forge": {"button_name": "BuildBlacksmith", "image_id": "BGBlacksmith", "label_id": "CONTROLBAR:ConstructMenBlacksmith", "tooltip_id": "CONTROLBAR:ToolTipConstructMenBlacksmith"},
	"well": {"button_name": "BuildWell", "image_id": "BGWell", "label_id": "CONTROLBAR:ConstructMenWell", "tooltip_id": "OBJECT:HearthWellDescription"},
	"statue": {"button_name": "BuildStatue", "image_id": "BGHeroicStatue", "label_id": "CONTROLBAR:ConstructMenStatue", "tooltip_id": "OBJECT:HeroicStatueDescription"},
	"battle_tower": {"button_name": "BuildBattleTower", "image_id": "BGBattleTower", "label_id": "CONTROLBAR:ConstructMenSentryTower", "tooltip_id": "CONTROLBAR:ToolTipConstructMenSentryTower"},
	"wall_hub_small": {"button_name": "BuildWallHub", "image_id": "BGWall_WallHub", "label_id": "CONTROLBAR:ConstructMenWallHub", "tooltip_id": "CONTROLBAR:ToolTipConstructMenWallHub"},
	"wall_hub_small_expansion": {"button_name": "BuildWallHubExpansion", "image_id": "BGWall_WallHub", "label_id": "CONTROLBAR:Command_ConstructMenWallHubExpansion", "tooltip_id": "CONTROLBAR:ToolTipCommand_ConstructMenWallHubExpansion"},
	"arrow_tower_expansion": {"button_name": "BuildArrowTower", "image_id": "BGFortress_ArrowTower", "label_id": "CONTROLBAR:ConstructMenArrowTowerExpansion", "tooltip_id": "CONTROLBAR:ToolTipConstructMenArrowTowerExpansion"},
	"trebuchet_expansion": {"button_name": "BuildTrebExpansion", "image_id": "BGFortress_Trebuchet", "label_id": "CONTROLBAR:ConstructMenTrebuchetExpansion", "tooltip_id": "CONTROLBAR:ToolTipConstructMenTrebuchetExpansion"},
	"trebuchet_side_expansion": {"button_name": "BuildTrebSideExpansion", "image_id": "BGFortress_Trebuchet", "label_id": "CONTROLBAR:ConstructMenTrebuchetExpansion", "tooltip_id": "CONTROLBAR:ToolTipConstructMenTrebuchetExpansion"},
}


func configure_faction_surface(manifest: Dictionary) -> void:
	## Faction-agnostic chrome. Construct kinds should already be registered
	## before build(); this still refreshes the filter list and the heading.
	var faction := String(manifest.get("faction", "men")).strip_edges()
	if _faction_heading_label != null:
		_faction_heading_label.text = _faction_display_name(faction)
	# Safe after build: only updates the visibility filter (does not invent
	# new action button nodes once the HUD is built).
	configure_manifest_construct_kinds(
		manifest.get("structure_kinds", []) as Array,
		faction,
		manifest.get("structure_training_summaries", {}) as Dictionary,
		manifest.get("structure_construct_icons", {}) as Dictionary
	)


## The Men/Gondor construct templates bind only Men packs — their localized
## strings and icons live in the Men content set. Other factions must never
## wear Men icons/labels (the reported "porter uses Men icons" bug).
static func _men_construct_surface(faction: String) -> bool:
	return faction.to_lower() in ["", "men", "gondor", "rohan"]


func _faction_display_name(faction: String) -> String:
	match faction.to_lower():
		"men", "gondor":
			return "MEN OF THE WEST"
		"elves":
			return "ELVES"
		"dwarves":
			return "DWARVES"
		"isengard":
			return "ISENGARD"
		"mordor":
			return "MORDOR"
		"wild":
			return "WILD"
		"rohan":
			return "ROHAN"
		_:
			return faction.replace("_", " ").to_upper() if faction != "" else "FACTION"


func configure_manifest_construct_kinds(kinds: Array, faction: String = "", training_summaries: Dictionary = {}, construct_icons: Dictionary = {}) -> void:
	## Filters construct UI to the faction manifest's structure_kinds and
	## ensures each kind has a construct action spec (workshop included when
	## present). Specs may still be registered after build so a post-reload
	## full-pack manifest can feed bind_retail_train_commands; button nodes for
	## late-added actions are created during that bind path.
	## `construct_icons` is the manifest's doc-driven per-kind construct icon
	## table ({kind: {image_id, structure_object_id}}) sourced from each
	## structure doc's own imageBindings.
	_faction_surface = faction
	_manifest_construct_kinds.clear()
	var existing: Dictionary = {}
	for spec_value in _retail_action_specs:
		var action_id := String((spec_value as Dictionary).get("action_id", ""))
		if action_id.begins_with("construct_"):
			existing[action_id.trim_prefix("construct_")] = true
	for kind_value in kinds:
		var kind := String(kind_value).strip_edges()
		if kind == "":
			continue
		_manifest_construct_kinds.append(kind)
		if existing.has(kind):
			# A Men template spec registered earlier must never survive onto a
			# non-Men faction's surface: re-register it as the faction's own
			# doc-driven spec (icon from its structure doc when bound) or the
			# honest fallback (the reported porter-uses-Men-art bug).
			if not _men_construct_surface(faction):
				for spec_index in _retail_action_specs.size():
					var prior: Dictionary = _retail_action_specs[spec_index]
					if String(prior.get("action_id", "")) == "construct_%s" % kind:
						_retail_action_specs[spec_index] = _construct_faction_spec(kind, training_summaries, construct_icons)
						break
			continue
		# Men/Gondor templates bind only on the Men surface. Non-Men factions
		# resolve construct icons from their OWN structure docs' imageBindings
		# (the construct commandbutton's converted crop, e.g. BEElvenBarracks);
		# a kind whose doc records a binding gap keeps the recorded authored
		# text fallback — never Men art passed off as another faction's.
		var template: Dictionary = {}
		if _men_construct_surface(faction):
			template = MANIFEST_CONSTRUCT_TEMPLATES.get(kind, {}) as Dictionary
		if template.is_empty():
			_retail_action_specs.append(_construct_faction_spec(kind, training_summaries, construct_icons))
			existing[kind] = true
			continue
		_retail_action_specs.append({
			"action_id": "construct_%s" % kind,
			"button_name": String(template.get("button_name", "Build_%s" % kind)),
			"image_id": String(template.get("image_id", "")),
			"label_id": String(template.get("label_id", "")),
			"tooltip_id": String(template.get("tooltip_id", "")),
			"authored_fallback": bool(template.get("authored_fallback", false)),
			"fallback_label": kind.replace("_", " ").capitalize(),
			"fallback_tooltip": "Construct %s" % kind.replace("_", " ").capitalize(),
		})
		existing[kind] = true
	_side_bar_fingerprint = ""


## Honest spec for a faction whose pack ships no localized construct
## strings/icons: kind-derived label plus the doc-derived "Trains <units>"
## production summary when the manifest carries one — reviewer-visible
## authored copy, never another faction's retail strings.
func _construct_fallback_spec(kind: String, training_summaries: Dictionary) -> Dictionary:
	var kind_label := kind.replace("_", " ").capitalize()
	var tooltip := "Construct %s" % kind_label
	var trained: Array = training_summaries.get(kind, []) as Array
	if not trained.is_empty():
		var names: Array[String] = []
		for name_value in trained:
			names.append(String(name_value))
		tooltip = "Trains %s" % _join_english_list(names)
	return {
		"action_id": "construct_%s" % kind,
		"button_name": "Build_%s" % kind,
		"image_id": "",
		"label_id": "",
		"tooltip_id": "",
		"authored_fallback": true,
		"fallback_label": kind_label,
		"fallback_tooltip": tooltip,
	}


## Doc-driven construct spec for a faction kind: the honest fallback text
## (kind label + "Trains <units>" summary) plus, when the kind's structure doc
## binds its construct commandbutton crop, that doc's own icon. Missing or
## invalid bindings keep the text-only socket — never another faction's art.
func _construct_faction_spec(kind: String, training_summaries: Dictionary, construct_icons: Dictionary) -> Dictionary:
	var spec := _construct_fallback_spec(kind, training_summaries)
	var icon: Dictionary = construct_icons.get(kind, {}) as Dictionary
	var image_id := String(icon.get("image_id", ""))
	var structure_object_id := String(icon.get("structure_object_id", ""))
	if image_id != "" and structure_object_id != "":
		spec["image_id"] = image_id
		spec["structure_object_id"] = structure_object_id
	return spec


static func _join_english_list(names: Array[String]) -> String:
	if names.size() == 1:
		return names[0]
	if names.size() == 2:
		return "%s and %s" % [names[0], names[1]]
	var head: Array[String] = names.slice(0, names.size() - 1)
	return "%s, and %s" % [", ".join(head), names[names.size() - 1]]


func configure_spellbook_runtime(document: Dictionary) -> void:
	## When a pack ships openbfme.spellbook-runtime, derive the palantir rows
	## (power id, icon crop, MP cost, authored purchase slot, pack strings) so
	## the orb lays out the retail tree. Sim stays authoritative for state;
	## this is presentation data only. Invalid/absent docs keep the fallback
	## icon-id rows so the screen still fails closed with 12 locked entries.
	_spellbook_power_rows.clear()
	_spellbook_doc_pack_root = ""
	_spellbook_image_bindings = {}
	if document.is_empty():
		return
	if String(document.get("schema", "")) != "openbfme.spellbook-runtime":
		return
	_spellbook_runtime = document.duplicate(true)
	_spellbook_power_ids.clear()
	# Per-faction icon binding: the document's own pack carries its icon crops
	# (each faction's spellbook pack ships assets/ui/spellbook/<f>spellbook).
	_spellbook_doc_pack_root = String(document.get("_pack_root", ""))
	var registration: Dictionary = _spellbook_runtime.get("registration", {}) as Dictionary
	_spellbook_image_bindings = (registration.get("presentation", {}) as Dictionary).get("imageBindings", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	var string_bindings: Dictionary = (registration.get("presentation", {}) as Dictionary).get("stringBindings", {}) as Dictionary
	var purchasable_sciences: Dictionary = {}
	for science_value in power_tree.get("sciences", []) as Array:
		if typeof(science_value) != TYPE_DICTIONARY:
			continue
		var science := science_value as Dictionary
		var purchase: Dictionary = science.get("purchase", {}) as Dictionary
		if purchase.is_empty():
			continue
		purchasable_sciences[String(science.get("id", ""))] = {
			"slot": int(purchase.get("slot", 0)),
			"cost": int((science.get("pointCostMP", {}) as Dictionary).get("value", 0)),
		}
	var powers: Array = power_tree.get("powers", []) as Array
	for power_value in powers:
		if typeof(power_value) != TYPE_DICTIONARY:
			continue
		var power := power_value as Dictionary
		var power_id := String(power.get("id", ""))
		var cast: Dictionary = power.get("cast", {}) as Dictionary
		var icon_id := ""
		for icon_value in cast.get("iconIds", []) as Array:
			icon_id = String(icon_value)
			if icon_id != "":
				break
		if icon_id != "" and not _spellbook_power_ids.has(icon_id):
			_spellbook_power_ids.append(icon_id)
		var science_id := ""
		for required_value in power.get("requiredSciences", []) as Array:
			if purchasable_sciences.has(String(required_value)):
				science_id = String(required_value)
				break
		var science_row: Dictionary = purchasable_sciences.get(science_id, {}) as Dictionary
		var text_ids: Array = cast.get("textIds", []) as Array
		var label := ""
		var tooltip := ""
		if text_ids.size() > 0:
			label = String(string_bindings.get(String(text_ids[0]), ""))
		if text_ids.size() > 1:
			tooltip = String(string_bindings.get(String(text_ids[1]), ""))
		_spellbook_power_rows.append({
			"power_id": power_id,
			"icon_id": icon_id,
			"cost": int(science_row.get("cost", 0)),
			"purchase_slot": int(science_row.get("slot", 0)),
			"label": label if label != "" else _retail_power_title(icon_id),
			"tooltip": tooltip,
		})
	_spellbook_power_rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.get("purchase_slot", 0)) != int(b.get("purchase_slot", 0)):
			return int(a.get("purchase_slot", 0)) < int(b.get("purchase_slot", 0))
		return String(a.get("power_id", "")).naturalnocasecmp_to(String(b.get("power_id", ""))) < 0
	)
	_configure_orb_rows()


func enable_playable_unit_content(runtimes: Dictionary, producer_kinds: Dictionary = {}) -> String:
	if _built:
		return "Playable-unit HUD content must be enabled before build."
	# Data-driven pack surface: train/portrait buttons come only from converted
	# playableUnit.* documents so Men/Elves/Dwarves/... share one HUD path.
	# The historical Men four-unit hardwire stays only when no runtimes ship.
	if not runtimes.is_empty():
		_retail_command_specs.clear()
		_retail_portrait_specs.clear()
		_data_driven_train_surface = true
	var pending_commands: Array[Dictionary] = []
	var pending_heroes: Array[Dictionary] = []
	var pending_portraits: Array[Dictionary] = []
	var seen: Dictionary = {}
	var occupied_routes: Dictionary = {}
	var occupied_hero_ordinals: Dictionary = {}
	for index in _retail_command_specs.size():
		var value := _retail_command_specs[index] as Dictionary
		seen[String(value.get("unit_id", "")).to_lower()] = index
	var object_ids: Array[String] = []
	for value in runtimes.keys():
		object_ids.append(String(value))
	object_ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for object_id in object_ids:
		var document_value: Variant = runtimes.get(object_id)
		if typeof(document_value) != TYPE_DICTIONARY:
			return "Playable-unit HUD runtime '%s' is invalid." % object_id
		var document := document_value as Dictionary
		var declared_surfaces: Dictionary = {}
		for route_value in Array((document.get("registration", {}) as Dictionary).get("production", [])):
			if typeof(route_value) == TYPE_DICTIONARY:
				declared_surfaces[String((route_value as Dictionary).get("surface", ""))] = true
		if declared_surfaces.size() > 1:
			return "Playable-unit HUD runtime '%s' has conflicting production surfaces." % object_id
		var specs := PlayableUnitAdapter.hud_specs(document)
		if specs.is_empty():
			return "Playable-unit HUD runtime '%s' has incomplete UI evidence." % object_id
		var surfaces: Dictionary = {}
		for route_spec in specs:
			var source_producer := String(route_spec.get("producer_source_object_id", ""))
			var producer_kind := String(producer_kinds.get(source_producer, source_producer))
			var surface := String(route_spec.get("surface", ""))
			var slot := int(route_spec.get("slot", 0))
			var roster_ordinal := int(route_spec.get("roster_ordinal", 0))
			var route_position := slot if surface == "command-socket" else roster_ordinal
			var slot_key := "%s:%s:%d" % [producer_kind, surface, route_position]
			var semantic_key := "%s|%s|%s" % [String(route_spec.get("image_id", "")), String(route_spec.get("label_id", "")), String(route_spec.get("tooltip_id", ""))]
			if (
				producer_kind == ""
				or surface not in ["command-socket", "hero-roster"]
				or (surface == "command-socket" and (slot < 1 or slot > RETAIL_COMMAND_SLOT_SOURCE.size() or roster_ordinal != 0))
				or (surface == "hero-roster" and (roster_ordinal < 1 or slot != 0))
			):
				return "Playable-unit HUD runtime '%s' has an invalid or colliding producer slot." % object_id
			if occupied_routes.has(slot_key) and String(occupied_routes[slot_key]) != semantic_key:
				return "Playable-unit HUD runtime '%s' has conflicting command-set variants in one producer slot." % object_id
			if surface == "hero-roster":
				var ordinal_key := "%s:%d" % [producer_kind, roster_ordinal]
				if occupied_hero_ordinals.has(ordinal_key):
					return "Playable-unit HUD runtime '%s' duplicates hero roster ordinal %d." % [object_id, roster_ordinal]
				occupied_hero_ordinals[ordinal_key] = object_id
			route_spec["producer_kind"] = producer_kind
			route_spec["runtime_object_id"] = object_id
			occupied_routes[slot_key] = semantic_key
			surfaces[surface] = true
		if surfaces.size() != 1:
			return "Playable-unit HUD runtime '%s' has conflicting production surfaces." % object_id
		var spec := specs[0]
		var unit_id := String(spec.get("unit_id", ""))
		if unit_id == "":
			return "Playable-unit HUD runtime '%s' has no runtime unit id." % object_id
		# Converted SPECIAL_POWER abilities (hero docs): retain the projected
		# rows so build() can lay out palantir sockets from authored slots.
		var ability_rows := PlayableUnitAdapter.ability_rules(document)
		if not ability_rows.is_empty():
			for ability_value in ability_rows:
				(ability_value as Dictionary)["runtime_object_id"] = object_id
			_hero_ability_specs[unit_id] = ability_rows
		if surfaces.has("hero-roster"):
			spec["runtime_object_id"] = object_id
			spec["route_specs"] = specs.duplicate(true)
			pending_heroes.append(spec)
			# Heroes still need a palantir select portrait when the hero itself
			# is the selection — without this entry the dish stays empty.
			pending_portraits.append({
				"unit_id": unit_id,
				"image_id": String(spec["portrait_image_id"]),
				"runtime_object_id": object_id,
			})
			continue
		spec["replace_index"] = int(seen.get(unit_id.to_lower(), -1))
		if int(spec["replace_index"]) < 0:
			seen[unit_id.to_lower()] = _retail_command_specs.size() + pending_commands.size()
		spec["runtime_object_id"] = object_id
		spec["route_specs"] = specs.duplicate(true)
		pending_commands.append(spec)
		pending_portraits.append({
			"unit_id": unit_id,
			"image_id": String(spec["portrait_image_id"]),
			"runtime_object_id": object_id,
		})
	for spec in pending_commands:
		var replace_index := int(spec.get("replace_index", -1))
		var route_specs: Array = (spec.get("route_specs", []) as Array).duplicate(true)
		spec.erase("replace_index")
		spec.erase("route_specs")
		if replace_index >= 0:
			_retail_command_specs[replace_index] = spec
		else:
			_retail_command_specs.append(spec)
		_generic_playable_units[String(spec["unit_id"])] = String(spec["runtime_object_id"])
		_generic_playable_routes[String(spec["unit_id"])] = route_specs
	for spec in pending_portraits:
		var replaced := false
		for index in _retail_portrait_specs.size():
			if String((_retail_portrait_specs[index] as Dictionary).get("unit_id", "")).to_lower() == String(spec["unit_id"]).to_lower():
				_retail_portrait_specs[index] = spec
				replaced = true
				break
		if not replaced:
			_retail_portrait_specs.append(spec)
	pending_heroes.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return int(a.get("roster_ordinal", 0)) < int(b.get("roster_ordinal", 0))
	)
	for spec in pending_heroes:
		var route_specs: Array = (spec.get("route_specs", []) as Array).duplicate(true)
		spec.erase("route_specs")
		_hero_command_specs.append(spec)
		_generic_playable_units[String(spec["unit_id"])] = String(spec["runtime_object_id"])
		_hero_routes[String(spec["unit_id"])] = route_specs
	return ""


func enable_ranger_content(contract: Dictionary) -> String:
	if _built:
		return "Ranger HUD content must be enabled before build."
	if contract.is_empty():
		return ""
	var presentation: Dictionary = contract.get("presentation", {}) as Dictionary
	var train_image: Dictionary = presentation.get("trainButtonImage", {}) as Dictionary
	var portrait_image: Dictionary = presentation.get("portraitImage", {}) as Dictionary
	if (
		String(presentation.get("hordeObjectId", "")) != String(RANGER_COMMAND_SPEC["unit_id"])
		or String(train_image.get("id", "")) != String(RANGER_COMMAND_SPEC["image_id"])
		or String(portrait_image.get("id", "")) != String(RANGER_PORTRAIT_SPEC["image_id"])
	):
		return "Ranger HUD contract identity is invalid."
	_retail_command_specs.append(RANGER_COMMAND_SPEC.duplicate(true))
	_retail_portrait_specs.append(RANGER_PORTRAIT_SPEC.duplicate(true))
	_retail_action_specs.append(ARCHERY_LEVEL_TWO_ACTION_SPEC.duplicate(true))
	_ranger_content_enabled = true
	return ""


func enable_trebuchet_content(contract: Dictionary) -> String:
	if _built:
		return "Trebuchet HUD content must be enabled before build."
	if contract.is_empty():
		return ""
	var presentation: Dictionary = contract.get("presentation", {}) as Dictionary
	var text: Dictionary = presentation.get("text", {}) as Dictionary
	if (
		String(contract.get("schema", "")) != "openbfme.trebuchet-runtime-contract"
		or String(contract.get("capabilityStatus", "")) != "bounded-direct-structure-ready"
		or String(presentation.get("trainButtonImage", "")) == ""
		or String(presentation.get("workshopButtonImage", "")) == ""
		or String(presentation.get("portraitImage", "")) == ""
		or String(text.get("trainLabel", "")) == ""
		or String(text.get("trainTooltip", "")) == ""
		or String(text.get("workshopLabel", "")) == ""
		or String(text.get("workshopTooltip", "")) == ""
	):
		return "Trebuchet HUD contract identity is invalid."
	_retail_command_specs.append({
		"unit_id": TREBUCHET_OBJECT_ID,
		"button_name": "TrainTrebuchet",
		"fallback_label": "Train Gondor Trebuchet",
		"fallback_tooltip": "Queue one Gondor Trebuchet",
		"image_id": String(presentation["trainButtonImage"]),
		"label_id": String(text["trainLabel"]),
		"tooltip_id": String(text["trainTooltip"]),
	})
	_retail_portrait_specs.append({
		"unit_id": TREBUCHET_OBJECT_ID,
		"image_id": String(presentation["portraitImage"]),
	})
	var has_workshop_action := false
	for spec_value in _retail_action_specs:
		if String((spec_value as Dictionary).get("action_id", "")) == "construct_workshop":
			# Prefer typed trebuchet contract art over a blank manifest template.
			(spec_value as Dictionary)["image_id"] = String(presentation["workshopButtonImage"])
			(spec_value as Dictionary)["label_id"] = String(text["workshopLabel"])
			(spec_value as Dictionary)["tooltip_id"] = String(text["workshopTooltip"])
			has_workshop_action = true
			break
	if not has_workshop_action:
		_retail_action_specs.append({
			"action_id": "construct_workshop",
			"button_name": "BuildWorkshop",
			"image_id": String(presentation["workshopButtonImage"]),
			"label_id": String(text["workshopLabel"]),
			"tooltip_id": String(text["workshopTooltip"]),
		})
	_trebuchet_content_enabled = true
	return ""


func _ready() -> void:
	if not _built:
		build()


func build() -> void:
	if _built:
		return
	_built = true
	name = "RetailHud"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_styles()
	_build_objective_banner()
	_build_palantir()
	_build_command_panel()
	_build_control_groups()
	_build_feedback()
	_build_diagnostics()
	_build_pause_panel()
	_build_playtest_panel()
	_build_outcome_layer()
	_build_failure_panel()
	_build_side_command_bar()
	_build_powers_dock()
	_build_event_feed()
	_build_construction_progress_layer()
	_build_dish_level_caption()
	_build_hero_bar()
	_build_radial_layer()
	_build_retail_tooltip()
	_wire_retail_tooltips()


func configure_minimap(simulation: RefCounted, map_data: RefCounted, camera_value: Camera3D = null, preview: Texture2D = null) -> void:
	minimap.configure(simulation, map_data, preview)
	minimap.world_camera = camera_value
	# THE RADAR'S BACKDROP IS THE MAP, not a palantir sprite. Retail leaves the
	# bezel's interior transparent and composites the battlefield inside it, so
	# the closest converted equivalent is this map's own published preview art.
	# A map that publishes none binds nothing, and the minimap's synthetic
	# schematic draws instead - the slice already reports that as a named map-art
	# degradation (`map_preview (no preview published by this map pack ...)`).
	minimap.bind_retail_parchment(preview)


func set_resources(resources: int, command_points: int, command_cap: int) -> void:
	_last_resources = resources
	_last_command_points = command_points
	_last_command_cap = command_cap
	resource_label.text = "%d" % resources
	command_points_label.text = "%d/%d" % [command_points, command_cap]
	if retail_apt_runtime != null:
		# The normal Men/Fords simulation has no resource multiplier mechanic;
		# retail therefore receives the exact hidden-at-1.0 value explicitly.
		if not retail_apt_runtime.set_live_text_values(
			resources, 1.0, command_points, command_cap
		):
			retail_apt_bound = false
			retail_apt_runtime.visible = false
	# The star orb shows SPELLBOOK POWER POINTS (REF-24 "6", REF-29 "5"), not
	# command points; refresh_powers owns that label.



func set_score_values(units_trained: int, units_lost: int, resources_gathered: int) -> void:
	if score_labels.has("units_trained"):
		(score_labels["units_trained"] as Label).text = "Units Trained  %d" % units_trained
		(score_labels["units_lost"] as Label).text = "Units Lost  %d" % units_lost
		(score_labels["resources_gathered"] as Label).text = "Resources Gathered  %d" % resources_gathered


func set_selection(text: String) -> void:
	selection_label.text = text


## The real local seat the last selection context carried (test/diagnostic
## surface; -1 until the first sync). A lockstep guest passes 1, not 0.
var last_selection_context_local_team := -1
var _side_fade_local_seat_noted := false


func sync_retail_selection_context(
	selected_ids: Array[int],
	selected_structure_id: int,
	entities: Dictionary,
	structures: Dictionary,
	winner: int,
	local_team: int = 0
) -> bool:
	last_selection_context_local_team = local_team
	if retail_apt_runtime == null or not retail_apt_bound:
		return not private_parity_mode_active
	var context := {
		"selected_ids": selected_ids.duplicate(),
		"selected_structure_id": selected_structure_id,
		"entities": entities,
		"structures": structures,
		"winner": winner,
		"local_team": local_team,
	}
	if local_team != 0:
		# The Men/Fords side-command fade contract pins the team-0 seat (its
		# typed input declares localTeam=0); a non-zero local seat (lockstep
		# guest) records a provisional and keeps the bound static surface
		# instead of tearing the HUD down mid-match. Fail closed on the fade
		# animation, never on the match.
		if not _side_fade_local_seat_noted:
			_side_fade_local_seat_noted = true
			retail_bind_diagnostics.append(
				"men-fords-side-fade-provisional: local_team=%d seat is outside the team-0 fade contract; selection fade sync skipped" % local_team
			)
		return true
	if retail_apt_runtime.sync_men_fords_selection(context):
		return true
	retail_apt_bound = false
	retail_control_bar_bound = false
	retail_presentation_bound = false
	retail_apt_runtime.visible = false
	return false


func retail_side_command_fade_state() -> Dictionary:
	return retail_apt_runtime.side_command_fade_state() if retail_apt_runtime != null else {}


func set_objective(text: String) -> void:
	objective_label.text = text


func set_feedback(text: String, warning: bool = false) -> void:
	feedback_label.text = text
	feedback_label.add_theme_color_override("font_color", Color("f3b176") if warning else Color("e6d28a"))


func set_train_state(enabled: bool, label: String = "Train Gondor Soldiers") -> void:
	## Compatibility surface for the original single-Soldier command tests.
	train_button.disabled = not enabled
	train_button.text = _retail_train_label if retail_train_command_bound else label


func set_production_state(
	production: Array,
	enabled: bool,
	queue_count: int = 0,
	queue_state: Array = [],
	locked_units: Array = [],
	completed_upgrades: Array = [],
	upgrade_queue: Array = [],
	producer_kind: String = "",
	doc_upgrade_commands: Array = []
) -> void:
	## Only commands authored by the selected producer are exposed. Labels stay
	## source-derived in private parity mode; queue state is metadata, not copy.
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		var button: Button = train_buttons.get(unit_id)
		if button == null:
			continue
		var supported := production.has(unit_id)
		if supported and _generic_playable_routes.has(unit_id):
			for route_value in _generic_playable_routes[unit_id] as Array:
				var route := route_value as Dictionary
				if producer_kind == "" or String(route.get("producer_kind", "")) == producer_kind:
					button.set_meta("retail_command_slot", int(route.get("slot", 0)))
					var validation: Dictionary = (_generic_playable_route_validations.get(unit_id, {}) as Dictionary).get(String(route.get("producer_kind", "")), {})
					if not validation.is_empty():
						_apply_retail_command(route, validation)
					elif not private_parity_mode_active:
						button.text = String(route.get("fallback_label", ""))
						button.tooltip_text = String(route.get("fallback_tooltip", ""))
					break
		button.visible = supported
		button.disabled = not enabled or not supported or locked_units.has(unit_id)
		button.set_meta("producer_queue_count", maxi(0, queue_count))
		if _retail_train_labels.has(unit_id) and not private_parity_mode_active:
			button.text = String(_retail_train_labels[unit_id])
	var hero_surface_visible := false
	for spec in _hero_command_specs:
		var unit_id := String(spec["unit_id"])
		var button: Button = hero_buttons.get(unit_id)
		if button == null:
			continue
		var route_matches := false
		for route_value in _hero_routes.get(unit_id, []) as Array:
			var route := route_value as Dictionary
			if producer_kind == "" or String(route.get("producer_kind", "")) == producer_kind:
				route_matches = true
				button.set_meta("retail_roster_ordinal", int(route.get("roster_ordinal", 0)))
				button.set_meta("retail_command_id", String(route.get("command_id", "")))
				var validation: Dictionary = (_hero_route_validations.get(unit_id, {}) as Dictionary).get(String(route.get("producer_kind", "")), {})
				if not validation.is_empty():
					_apply_retail_hero_command(route, validation)
				elif not private_parity_mode_active:
					button.text = String(route.get("fallback_label", ""))
					button.tooltip_text = String(route.get("fallback_tooltip", ""))
				break
		var supported := production.has(unit_id) and route_matches
		button.visible = supported
		button.disabled = not enabled or not supported or locked_units.has(unit_id)
		button.set_meta("producer_queue_count", maxi(0, queue_count))
		hero_surface_visible = hero_surface_visible or supported
	if hero_selection_panel != null:
		# Parity mode floats the roster as radial portraits above the fortress
		# (REF-35); the framed panel is the public fallback.
		hero_selection_panel.visible = hero_surface_visible and not private_parity_mode_active
	_update_doc_upgrade_buttons(doc_upgrade_commands, completed_upgrades, upgrade_queue)
	for action_id_value in unit_action_buttons.keys():
		var action_id := String(action_id_value)
		var action_button := unit_action_buttons[action_id] as Button
		if action_id == "upgrade_archery_range_level2":
			action_button.visible = (
				_ranger_content_enabled
				and production.has(String(RANGER_COMMAND_SPEC["unit_id"]))
				and not completed_upgrades.has("Upgrade_GondorArcheryRangeLevel2")
			)
			action_button.disabled = not enabled or not upgrade_queue.is_empty()
		# Unit-order/construct visibility is owned exclusively by
		# set_unit_selection_state. Both run every presentation frame; hiding
		# the buttons here and re-showing them there flickered visibility every
		# frame, which dropped the viewport's mouse focus (in-flight clicks
		# never completed) and reset hover state (no highlight) on every
		# palantir socket button.
	_layout_command_sockets()
	_update_production_queue(queue_state, not production.is_empty())
	_update_retail_selection_portrait(production)


func set_unit_selection_state(selected_ids: Array[int], entities: Dictionary, current_tick: int = -1) -> void:
	var has_units := not selected_ids.is_empty()
	var builders_only := has_units
	for selected_id in selected_ids:
		if not bool((entities.get(selected_id, {}) as Dictionary).get("is_builder", false)):
			builders_only = false
			break
	_update_hero_ability_buttons(selected_ids, entities, current_tick)
	for button_value in unit_action_buttons.values():
		var button := button_value as Button
		var action_id := String(button.get_meta("action_id", ""))
		if action_id.begins_with("upgrade_"):
			continue
		var is_construct := action_id.begins_with("construct_")
		if is_construct:
			# Retail: the porter's builds live ONLY on the right-edge side
			# command bar (REF-29/32); the palantir sockets keep his orders.
			button.visible = false
		elif action_id == "stop" or action_id == "stance":
			# The porter's palantir carries Stop + stance (the two icons at the
			# dish's upper right in REF-32); attack-move/formation stay combat-only.
			button.visible = has_units
		else:
			button.visible = has_units and not builders_only
		button.disabled = not button.visible
	_layout_command_sockets()
	_refresh_side_command_bar(builders_only)
	if not has_units:
		_update_retail_selection_portrait([])
		if _selection_rank_pips != null:
			_selection_rank_pips.set_pips(0)
		return
	var first: Dictionary = entities.get(selected_ids[0], {}) as Dictionary
	# The runtime unit id is the portrait key on data-driven packs; the legacy
	# member→horde map only covers the tiny Men fixture.
	var unit_type := String(first.get("unit_type", ""))
	if unit_type != "" and _retail_portrait_textures.has(unit_type):
		_show_retail_portrait(unit_type)
	else:
		var member_id := String(first.get("object_id", ""))
		var horde_id := String(RETAIL_MEMBER_TO_HORDE.get(member_id, member_id))
		_show_retail_portrait(horde_id)
	if _selection_rank_pips != null:
		# Rank pips mirror the live level the XP pipeline raises (rank 1 hides).
		_selection_rank_pips.set_pips(maxi(0, int(first.get("level", 1)) - 1))
	# Keep stance/formation chrome in sync with the live selection.
	set_active_stance(String(first.get("stance", "Battle")))
	set_active_formation(String(first.get("formation_mode", "Line")))


func _ability_spec_for(unit_id: String, ability_id: String) -> Dictionary:
	for ability_value in _hero_ability_specs.get(unit_id, []) as Array:
		if String((ability_value as Dictionary).get("ability_id", "")) == ability_id:
			return ability_value as Dictionary
	return {}


func _ability_button_tooltip(unit_id: String, ability_id: String, level: int, remaining_ticks: int) -> String:
	## Tooltip with the doc's authored name/description plus the live gate
	## state. Unavailable abilities always say why; nothing is dressed up as
	## ready when it is not.
	var spec := _ability_spec_for(unit_id, ability_id)
	if spec.is_empty():
		return ""
	var label := String(_retail_train_labels.get("ability:%s" % ability_id, String(spec.get("fallback_label", ability_id))))
	var base := String(_retail_ability_tooltips.get(ability_id, String(spec.get("fallback_tooltip", ""))))
	var lines: Array[String] = []
	if label != "":
		lines.append(label)
	if base != "" and base != label:
		lines.append(base)
	if not bool(spec.get("castable", false)):
		var reason := String(spec.get("availability_reason", ""))
		lines.append("Unavailable: %s" % (reason if reason != "" else "not implemented"))
	elif not bool(spec.get("level_gate_resolved", true)):
		lines.append("Unavailable: level requirement unresolved")
	elif level < int(spec.get("required_level", 1)):
		lines.append("Requires level %d (current: %d)" % [int(spec.get("required_level", 1)), level])
	elif remaining_ticks > 0:
		lines.append("Recharging (%ds)" % ceili(float(remaining_ticks) * 0.1))
	return "\n".join(lines)


func _update_hero_ability_buttons(selected_ids: Array[int], entities: Dictionary, current_tick: int) -> void:
	## Show the selected hero's converted SPECIAL_POWER abilities with live
	## cooldown sweep and level-gated state. Buttons never leave the converted
	## doc surface: no ability row, no button.
	var hero_unit_id := ""
	var hero_row: Dictionary = {}
	for selected_id in selected_ids:
		var row: Dictionary = entities.get(selected_id, {}) as Dictionary
		if String(row.get("category", "")) == "hero" and _hero_ability_specs.has(String(row.get("unit_type", ""))):
			hero_unit_id = String(row.get("unit_type", ""))
			hero_row = row
			break
	for unit_id_value in hero_ability_buttons.keys():
		var unit_id := String(unit_id_value)
		var show := unit_id == hero_unit_id
		var level := int(hero_row.get("level", 1)) if show else 1
		var states: Dictionary = (hero_row.get("ability_states", {}) as Dictionary) if show else {}
		for ability_id_value in (hero_ability_buttons[unit_id] as Dictionary).keys():
			var ability_id := String(ability_id_value)
			var button: Button = (hero_ability_buttons[unit_id] as Dictionary)[ability_id]
			button.visible = show
			if not show:
				continue
			var spec := _ability_spec_for(unit_id, ability_id)
			var state: Dictionary = states.get(ability_id, {}) as Dictionary
			var ready_tick := int(state.get("cooldown_ready_tick", 0))
			var cooldown_ticks := int(state.get("cooldown_ticks", 0))
			var remaining := maxi(0, ready_tick - current_tick) if current_tick >= 0 else 0
			var castable := bool(spec.get("castable", false))
			var gate_ok := bool(spec.get("level_gate_resolved", true))
			var level_ok := level >= int(spec.get("required_level", 1))
			button.disabled = not castable or not gate_ok or not level_ok or remaining > 0
			# Retail greys level-gated/recharging ability icons in the sockets
			# (REF-41 Boromir's locked ranks render dark).
			button.self_modulate = Color(0.45, 0.45, 0.5) if button.disabled else Color.WHITE
			var sweep: TextureProgressBar = button.get_node_or_null("CooldownSweep") as TextureProgressBar
			if sweep != null:
				var cooling := remaining > 0 and cooldown_ticks > 0
				sweep.visible = cooling and sweep.texture_progress != null
				sweep.value = clampf(float(remaining) / float(maxi(1, cooldown_ticks)), 0.0, 1.0)
			button.tooltip_text = _ability_button_tooltip(unit_id, ability_id, level, remaining)


func _emit_ability_cast_requested(unit_id: String, ability_id: String) -> void:
	ui_sound_requested.emit("Gui_PalantirButtonClick")
	ability_cast_requested.emit(unit_id, ability_id)


func _apply_retail_ability_command(button: Button, spec: Dictionary, validation: Dictionary) -> void:
	## Pack-bound retail art/strings for one converted hero ability button.
	button.icon = validation["texture"] as Texture2D
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", 48)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.text = ""
	var ability_id := String(spec.get("ability_id", ""))
	var label := String(validation.get("label", ""))
	var tooltip := String(validation.get("tooltip", ""))
	button.set_meta("retail_icon_id", String(spec.get("icon_id", "")))
	button.set_meta("retail_icon_path", String(validation.get("path", "")))
	button.set_meta("ability_label", label)
	button.set_meta("ability_tooltip_base", tooltip)
	_retail_train_labels["ability:%s" % ability_id] = label
	_retail_ability_tooltips[ability_id] = tooltip
	var sweep: TextureProgressBar = button.get_node_or_null("CooldownSweep") as TextureProgressBar
	if sweep != null and button.icon != null:
		sweep.texture_progress = button.icon


func _update_doc_upgrade_buttons(commands: Array, completed_upgrades: Array, upgrade_queue: Array) -> void:
	## Doc-driven building levels: one button per purchasable upgrade chain step
	## on the selected building's live command set. Icon/label/tooltip bind from
	## the pack (fail-closed via the retail command validation); cost reads from
	## the compiled chain. The slice's structure_upgrade_requested signal and
	## the sim's generic contract own the purchase itself.
	var offered: Dictionary = {}
	for command_value in commands:
		if typeof(command_value) != TYPE_DICTIONARY:
			continue
		var command := command_value as Dictionary
		var upgrade_id := String(command.get("upgrade_id", ""))
		if upgrade_id == "":
			continue
		offered[upgrade_id] = command
	for upgrade_id_value in _doc_upgrade_buttons.keys():
		var existing := _doc_upgrade_buttons[upgrade_id_value] as Button
		existing.visible = offered.has(upgrade_id_value)
		if offered.has(upgrade_id_value):
			var command: Dictionary = offered[upgrade_id_value]
			existing.disabled = not upgrade_queue.is_empty()
			_refresh_doc_upgrade_tooltip(existing, command)
	for upgrade_id_value in offered.keys():
		if _doc_upgrade_buttons.has(upgrade_id_value):
			continue
		var command: Dictionary = offered[upgrade_id_value]
		var slot := int(command.get("slot", 0))
		if slot < 1:
			# Doc-driven chains always author a command-set slot; anything else
			# is the legacy overlay contract, which keeps its bespoke surface.
			continue
		var button := Button.new()
		button.name = "DocUpgrade_%s" % String(upgrade_id_value).trim_prefix("Upgrade_")
		button.custom_minimum_size = Vector2(54, 54)
		button.set_meta("action_id", "doc_upgrade:%s" % upgrade_id_value)
		if private_parity_mode_active:
			# Retail chrome: the palantir socket art + validated icon, never the
			# public blue text button (owner: research rendered as plain bars).
			if _retail_palantir_atlas != null:
				var socket_box := StyleBoxTexture.new()
				socket_box.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
				for state in ["normal", "hover", "pressed", "disabled", "focus"]:
					button.add_theme_stylebox_override(state, socket_box)
			button.expand_icon = true
			button.add_theme_constant_override("icon_max_width", 48)
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		else:
			_style_button(button)
		button.pressed.connect(func() -> void: structure_upgrade_requested.emit(String(upgrade_id_value)))
		_place_command_button(button, clampi(slot - 1, 0, RETAIL_COMMAND_SLOT_SOURCE.size() - 1))
		_doc_upgrade_buttons[upgrade_id_value] = button
		_rebind_order_action_button(
			button,
			String(command.get("image_id", "")),
			String(command.get("label_id", "")),
			String(command.get("tooltip_id", "")),
			_honest_upgrade_label(upgrade_id_value)
		)
		if button.icon == null:
			# No converted button art: the honest doc-derived label renders as
			# text; the raw CONTROLBAR id must never reach the screen (recorded).
			button.text = _honest_upgrade_label(upgrade_id_value)
			button.add_theme_font_size_override("font_size", 10)
			button.add_theme_color_override("font_color", Color("e6d9ae"))
			button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
			retail_bind_diagnostics.append(
				"doc-upgrade-unbound-recorded: '%s' renders doc-derived text — no converted button image/localized string in the selected pack" % upgrade_id_value
			)
		_refresh_doc_upgrade_tooltip(button, command)


## Honest display label for an upgrade id whose pack ships no localized string
## (e.g. elven EregionForge chains): prettified from the doc-authored id, never
## the raw CONTROLBAR: key. "Upgrade_EregionForgeLevel2" -> "Eregion Forge Level 2".
func _honest_upgrade_label(upgrade_id: String) -> String:
	var base := upgrade_id.trim_prefix("Upgrade_")
	var words := ""
	for index in base.length():
		var ch := base.substr(index, 1)
		var prior := base.substr(index - 1, 1) if index > 0 else ""
		if index > 0 and ch != ch.to_lower() and (prior == prior.to_lower() or prior.is_valid_int()):
			words += " "
		elif index > 0 and ch.is_valid_int() and not prior.is_valid_int():
			words += " "
		words += ch
	return words.strip_edges()


func _refresh_doc_upgrade_tooltip(button: Button, command: Dictionary) -> void:
	var base := String(button.get_meta("retail_label", button.tooltip_text))
	var cost := int(command.get("cost", 0))
	var tooltip_id := String(command.get("tooltip_id", ""))
	var source_tooltip := button.tooltip_text
	if _bound_content_db != null and tooltip_id != "":
		source_tooltip = String(_bound_content_db.get_retail_string(tooltip_id, source_tooltip))
	button.tooltip_text = "%s\n%s\nCost: %d" % [base, source_tooltip, cost] if base != source_tooltip else "%s\nCost: %d" % [source_tooltip, cost]


## Battalion OBJECT_UPGRADE purchase surface (compiled per unit doc): one
## socket button per authored purchase row while the owning battalion is
## selected. Icons/localized strings bind from the pack (fail-closed honest
## text + recorded diagnostics, never a raw id); the NeededUpgrade tech gate
## greys/locks the button until the team research completes; a queued purchase
## sweeps the CCW dial with its live countdown.
func set_battalion_upgrade_state(commands: Array, queue_rows: Array) -> void:
	var offered: Dictionary = {}
	for command_value in commands:
		if typeof(command_value) != TYPE_DICTIONARY:
			continue
		var command := command_value as Dictionary
		var upgrade_id := String(command.get("upgrade_id", ""))
		if upgrade_id == "" or bool(command.get("applied", false)):
			continue
		offered[upgrade_id] = command
	for upgrade_id_value in _battalion_upgrade_buttons.keys():
		var existing := _battalion_upgrade_buttons[upgrade_id_value] as Button
		existing.visible = offered.has(upgrade_id_value)
		if offered.has(upgrade_id_value):
			_sync_battalion_upgrade_button(existing, offered[upgrade_id_value], queue_rows)
	for upgrade_id_value in offered.keys():
		if _battalion_upgrade_buttons.has(upgrade_id_value):
			continue
		var command: Dictionary = offered[upgrade_id_value]
		var button := _make_battalion_upgrade_button(upgrade_id_value, command)
		_battalion_upgrade_buttons[upgrade_id_value] = button
		_sync_battalion_upgrade_button(button, command, queue_rows)
	_layout_command_sockets()


func _make_battalion_upgrade_button(upgrade_id: String, command: Dictionary) -> Button:
	var button := Button.new()
	button.name = "BattalionUpgrade_%s" % upgrade_id.trim_prefix("Upgrade_")
	button.custom_minimum_size = Vector2(54, 54)
	button.set_meta("action_id", "battalion_upgrade:%s" % upgrade_id)
	button.set_meta("battalion_upgrade_slot", int(command.get("slot", 0)))
	if private_parity_mode_active:
		if _retail_palantir_atlas != null:
			var socket_box := StyleBoxTexture.new()
			socket_box.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
			for state in ["normal", "hover", "pressed", "disabled", "focus"]:
				button.add_theme_stylebox_override(state, socket_box)
		button.expand_icon = true
		button.add_theme_constant_override("icon_max_width", 48)
		button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	else:
		_style_button(button)
	button.pressed.connect(func() -> void: battalion_upgrade_requested.emit(upgrade_id))
	_place_command_button(button, clampi(int(command.get("slot", 1)) - 1, 0, RETAIL_COMMAND_SLOT_SOURCE.size() - 1))
	_rebind_order_action_button(
		button,
		String(command.get("image_id", "")),
		String(command.get("label_id", "")),
		String(command.get("tooltip_id", "")),
		_honest_upgrade_label(upgrade_id)
	)
	if button.icon == null:
		button.text = _honest_upgrade_label(upgrade_id)
		button.add_theme_font_size_override("font_size", 10)
		button.add_theme_color_override("font_color", Color("e6d9ae"))
		button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		retail_bind_diagnostics.append(
			"battalion-upgrade-unbound-recorded: '%s' renders doc-derived text — no converted button image/localized string in the selected pack" % upgrade_id
		)
	return button


func _sync_battalion_upgrade_button(button: Button, command: Dictionary, queue_rows: Array) -> void:
	var research_owned := bool(command.get("research_owned", false))
	var queue_row: Dictionary = {}
	for row_value in queue_rows:
		var row: Dictionary = row_value
		if String(row.get("upgrade_id", "")) == String(command.get("upgrade_id", "")):
			queue_row = row
			break
	var queued := not queue_row.is_empty()
	if queued:
		queue_row["active"] = true
		_sync_queue_button_dial(button, queue_row)
	else:
		var stale_dial := button.get_node_or_null("TrainingDial") as TextureProgressBar
		if stale_dial != null:
			stale_dial.visible = false
		var stale_countdown := button.get_node_or_null("TrainingCountdown") as Label
		if stale_countdown != null:
			stale_countdown.visible = false
	button.disabled = not research_owned or queued
	# Locked rows grey until the NeededUpgrade technology is researched.
	button.self_modulate = Color(0.45, 0.45, 0.5) if not research_owned else Color.WHITE
	_refresh_battalion_upgrade_tooltip(button, command)


func _refresh_battalion_upgrade_tooltip(button: Button, command: Dictionary) -> void:
	var base := String(button.get_meta("retail_label", _honest_upgrade_label(String(command.get("upgrade_id", "")))))
	var cost := int(command.get("cost", 0))
	var tooltip_id := String(command.get("tooltip_id", ""))
	var source_tooltip := button.tooltip_text
	if _bound_content_db != null and tooltip_id != "":
		source_tooltip = String(_bound_content_db.get_retail_string(tooltip_id, source_tooltip))
	var lines: Array[String] = []
	lines.append("%s\n%s\nCost: %d" % [base, source_tooltip, cost] if base != source_tooltip else "%s\nCost: %d" % [source_tooltip, cost])
	if not bool(command.get("research_owned", false)):
		lines.append("Requires %s" % _required_tech_label(command))
	button.tooltip_text = "\n".join(lines)


## The missing-technology line for a locked purchase: the authored
## lacks-prerequisite string when the pack carries it, else the doc-derived
## prettified tech id — never a raw id.
func _required_tech_label(command: Dictionary) -> String:
	var lacks_id := String(command.get("lacks_prerequisite_label_id", ""))
	if _bound_content_db != null and lacks_id != "":
		var resolved := String(_bound_content_db.get_retail_string(lacks_id, ""))
		if resolved != "":
			return resolved
	return _honest_upgrade_label(String(command.get("required_upgrade", "")))


## Retail training timer: the active queue slot sweeps a COUNTERCLOCKWISE
## radial dial (retail sweep direction) and counts the remaining seconds down
## live; queued-behind slots just dim. Children are built once per button.
func _sync_queue_button_dial(queue_button: Button, row: Dictionary) -> void:
	var dial := queue_button.get_node_or_null("TrainingDial") as TextureProgressBar
	if dial == null:
		dial = TextureProgressBar.new()
		dial.name = "TrainingDial"
		dial.fill_mode = TextureProgressBar.FILL_COUNTER_CLOCKWISE
		dial.min_value = 0.0
		dial.max_value = 1.0
		dial.step = 0.001
		dial.mouse_filter = Control.MOUSE_FILTER_IGNORE
		dial.tint_progress = Color(0.02, 0.03, 0.03, 0.55)
		dial.tint_under = Color(0.0, 0.0, 0.0, 0.0)
		dial.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		queue_button.add_child(dial)
	var countdown := queue_button.get_node_or_null("TrainingCountdown") as Label
	if countdown == null:
		countdown = Label.new()
		countdown.name = "TrainingCountdown"
		countdown.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		countdown.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		countdown.add_theme_font_size_override("font_size", 12)
		countdown.add_theme_color_override("font_color", Color("f5ecc8"))
		countdown.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
		countdown.add_theme_constant_override("shadow_offset_x", 1)
		countdown.add_theme_constant_override("shadow_offset_y", 1)
		countdown.mouse_filter = Control.MOUSE_FILTER_IGNORE
		countdown.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		queue_button.add_child(countdown)
	var active := bool(row.get("active", false))
	var progress := clampf(float(row.get("progress", 0.0)), 0.0, 1.0)
	dial.visible = active and queue_button.icon != null
	if dial.visible:
		if dial.texture_progress == null:
			dial.texture_progress = queue_button.icon
		# CCW fill drains as training completes.
		dial.value = 1.0 - progress
	countdown.visible = active
	countdown.text = "%ds" % ceili(float(row.get("remaining_seconds", 0.0))) if active else ""
	queue_button.self_modulate = Color.WHITE if active else Color(0.55, 0.55, 0.55)


## Retail-style production queue chips: up to five clickable slots under the
## palantir dish; clicking a queued item cancels it (retail behavior). Extracted
## from the parity chrome bind so the chip surface can be exercised without a
## full pack bind (runner coverage for the training dial/countdown).
func _ensure_production_queue_chips() -> void:
	if command_grid == null or not production_queue_buttons.is_empty():
		return
	for index in 5:
		var queue_button := Button.new()
		queue_button.name = "QueueSlot%d" % index
		# 318 keeps the 36px chip inside the 360px frame panel (344 ran 20px off
		# the screen bottom, hiding the training dial entirely).
		queue_button.position = Vector2(60 + index * 40, 318)
		queue_button.size = Vector2(36, 36)
		for state in ["normal", "hover", "pressed", "disabled", "focus"]:
			queue_button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
		queue_button.expand_icon = true
		queue_button.visible = false
		queue_button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		queue_button.tooltip_text = "Click to cancel"
		queue_button.pressed.connect(func() -> void: cancel_production_requested.emit(index))
		command_grid.add_child(queue_button)
		production_queue_buttons.append(queue_button)


func _update_production_queue(queue_state: Array, producer_selected: bool) -> void:
	for index in production_queue_buttons.size():
		var queue_button := production_queue_buttons[index]
		if not producer_selected or index >= queue_state.size():
			queue_button.visible = false
			continue
		var row: Dictionary = queue_state[index]
		var unit_type := String(row.get("unit_type", ""))
		var train_button := train_buttons.get(unit_type) as Button
		if train_button == null:
			# Fortress hero recruits queue through the same production surface;
			# their icons live on the hero roster buttons, not the train set.
			train_button = hero_buttons.get(unit_type) as Button
		queue_button.icon = train_button.icon if train_button != null else null
		queue_button.visible = queue_button.icon != null
		_sync_queue_button_dial(queue_button, row)
	if production_queue_label == null or production_progress == null or cancel_production_button == null:
		return
	# Retail shows the queue as dish-side icons only; the text/progress/cancel
	# chrome is the public surface and hides in parity mode.
	if private_parity_mode_active:
		production_queue_label.visible = false
		production_progress.visible = false
		cancel_production_button.visible = false
		return
	production_queue_label.visible = producer_selected
	production_progress.visible = producer_selected and not queue_state.is_empty()
	cancel_production_button.visible = producer_selected and not queue_state.is_empty()
	cancel_production_button.disabled = queue_state.is_empty()
	if not producer_selected:
		production_queue_label.text = ""
		production_progress.value = 0.0
		return
	if queue_state.is_empty():
		production_queue_label.text = "Production queue ready"
		production_progress.value = 0.0
		return
	var active: Dictionary = queue_state[0]
	var unit_id := String(active.get("unit_type", ""))
	var unit_name := command_label(unit_id, "Unit")
	var progress := clampf(float(active.get("progress", 0.0)), 0.0, 1.0)
	production_queue_label.text = "%s  %d%%  (%d queued)" % [unit_name, roundi(progress * 100.0), queue_state.size()]
	production_progress.value = progress * 100.0
	cancel_production_button.set_meta("queue_index", int(active.get("index", 0)))


func command_label(unit_id: String, fallback: String = "Unit") -> String:
	return String(_retail_train_labels.get(unit_id, fallback))


func retail_action_texture(action_id: String) -> Texture2D:
	var button := unit_action_buttons.get(action_id) as Button
	return button.icon if button != null else null


func set_active_stance(stance: String) -> void:
	var button := unit_action_buttons.get("stance") as Button
	if button == null:
		return
	var key := stance if STANCE_UI.has(stance) else "Battle"
	if String(button.get_meta("active_stance", "")) == key:
		# Called every presentation frame; rebinding an unchanged stance would
		# re-decode the icon and churn the button visual state per frame.
		return
	button.set_meta("active_stance", key)
	var ui: Dictionary = STANCE_UI[key] as Dictionary
	var fallback := String(ui.get("fallback_label", key))
	_rebind_order_action_button(
		button,
		String(ui.get("image_id", "")),
		String(ui.get("label_id", "")),
		String(ui.get("tooltip_id", "")),
		fallback
	)
	var source_label := String(button.get_meta("retail_label", fallback))
	button.tooltip_text = "%s\nCurrent: %s (click to cycle)" % [source_label, key]


func set_active_formation(formation: String) -> void:
	var button := unit_action_buttons.get("formation") as Button
	if button == null:
		return
	var key := formation if FORMATION_UI.has(formation) else "Line"
	if String(button.get_meta("active_formation", "")) == key:
		return
	button.set_meta("active_formation", key)
	var ui: Dictionary = FORMATION_UI[key] as Dictionary
	var fallback := String(ui.get("fallback_label", key))
	_rebind_order_action_button(
		button,
		String(ui.get("image_id", "")),
		String(ui.get("label_id", "")),
		String(ui.get("tooltip_id", "")),
		fallback
	)
	var source_label := String(button.get_meta("retail_label", fallback))
	button.tooltip_text = "%s\nCurrent: %s (click to cycle)" % [source_label, key]


func _rebind_order_action_button(
	button: Button,
	image_id: String,
	label_id: String,
	tooltip_id: String,
	fallback_label: String
) -> void:
	## Prefer the pack-bound retail icon/string; if the pack is not bound yet
	## (public/dev surface), keep a readable text label so the control is not blank.
	if _bound_content_db == null or _bound_pack_root == "" or image_id == "":
		if button.icon == null:
			button.text = fallback_label
		button.set_meta("retail_label", fallback_label)
		return
	var validation := _validate_retail_command(
		_bound_content_db,
		_bound_pack_root,
		{
			"image_id": image_id,
			"label_id": label_id,
			"tooltip_id": tooltip_id,
			"action_id": String(button.get_meta("action_id", "")),
		},
		Vector2i(63, 63)
	)
	if String(validation.get("error", "")) != "":
		if button.icon == null:
			button.text = fallback_label
		button.set_meta("retail_label", fallback_label)
		button.tooltip_text = fallback_label
		return
	button.icon = validation["texture"] as Texture2D
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", 56)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.text = ""
	button.tooltip_text = String(validation["tooltip"])
	button.set_meta("retail_label", String(validation["label"]))
	button.set_meta("retail_icon_id", image_id)
	button.set_meta("retail_label_id", label_id)
	button.set_meta("retail_tooltip_id", tooltip_id)


func bind_retail_train_command(content_db, expected_pack_root: String, private_parity_mode: bool) -> String:
	## Compatibility binder for the historical Soldier-only external fixture.
	_clear_retail_command_bindings(true)
	if not private_parity_mode:
		train_button.visible = true
		train_button.disabled = false
		return ""
	if content_db == null:
		return "ContentDB is unavailable; cannot bind the private Barracks command UI."
	if not _built or train_button == null:
		return "The Barracks command button has not been built."
	var spec: Dictionary = _retail_command_specs[0]
	var validation := _validate_retail_command(content_db, expected_pack_root, spec, Vector2i.ZERO)
	var error := String(validation.get("error", ""))
	if error != "":
		return error
	_apply_retail_command(spec, validation)
	train_button.visible = true
	train_button.disabled = false
	retail_train_command_bound = true
	_retail_train_label = String(validation["label"])
	# The legacy single-command compatibility surface keeps the validated
	# localized label as button text (the full multi-command surface is
	# icon-only sockets by contrast).
	train_button.text = _retail_train_label
	retail_train_icon_aspect_ratio = float(validation["aspect_ratio"])
	return ""


func bind_retail_train_commands(content_db, expected_pack_root: String, private_parity_mode: bool, faction_pack_roots: Array = []) -> String:
	## Private surface is pack-atomic: control bar, portraits, train commands,
	## and common unit orders (stop/stance/formation/attack-move) all resolve
	## from the selected pack. Train buttons are either the legacy Men four
	## or the data-driven playableUnit set enabled before build(). Runtime-backed
	## unit images may additionally resolve from the faction manifest's converted
	## faction packs (faction_pack_roots) when a supplemental faction pack is
	## mounted alongside the host pack.
	_clear_retail_command_bindings(private_parity_mode)
	retail_bind_diagnostics.clear()
	if not private_parity_mode:
		return ""
	if content_db == null:
		return "ContentDB is unavailable; cannot bind the private production UI."
	if not _built:
		return "The production command buttons have not been built."
	if train_buttons.size() != _retail_command_specs.size():
		return "Production command button count does not match the command surface (%d vs %d)." % [
			train_buttons.size(), _retail_command_specs.size()
		]
	if _retail_command_specs.is_empty() and _hero_command_specs.is_empty() and not _data_driven_train_surface:
		return "No production commands are configured for the selected pack."
	_bound_content_db = content_db
	_bound_pack_root = expected_pack_root
	set_allowed_image_pack_roots([expected_pack_root] + faction_pack_roots)
	if retail_apt_runtime == null:
		return "The retail Palantir APT runtime has not been built."
	var apt_configured := retail_apt_runtime.configure_from_pack(expected_pack_root, true)
	if not apt_configured or not retail_apt_runtime.contract_declared:
		# The HUD APT bundle is host-pack payload. An expansion faction's lean
		# supplemental pack ships only its own units, structures and spellbook,
		# so the runtime configures against it "successfully" while declaring no
		# contract, and the HUD then falls back to a manifest path demanding
		# images (SGCommandBar) that no pack carries. Retry against the recorded
		# host/faction roots; a genuine host declares its contract on the first
		# attempt, so this costs a BFME2 faction nothing.
		for root in _allowed_image_pack_roots:
			if _same_pack_root(root, expected_pack_root):
				continue
			if retail_apt_runtime.configure_from_pack(root, true) and retail_apt_runtime.contract_declared:
				apt_configured = true
				break
	var use_apt := retail_apt_runtime.contract_declared
	if not apt_configured:
		return "Private retail HUD APT is incomplete: %s" % retail_apt_runtime.error
	if use_apt:
		var source_font := retail_apt_runtime.external_albertus_font()
		if source_font == null:
			return "Private retail HUD APT did not expose its validated Albertus MT font."
		_apply_source_font(self, source_font)
	if use_apt and not retail_apt_runtime.set_live_text_values(
		_last_resources, 1.0, _last_command_points, _last_command_cap
	):
		return "Private retail HUD APT rejected its deterministic live values: %s" % retail_apt_runtime.error
	var validated: Dictionary = {}
	var route_validated: Dictionary = {}
	var validation_errors: Array[String] = []
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		var validation := _validate_retail_command(content_db, expected_pack_root, spec)
		var error := String(validation.get("error", ""))
		if error != "":
			validation_errors.append(error)
		else:
			validated[String(spec["unit_id"])] = validation
	for unit_id_value in _generic_playable_routes.keys():
		var unit_id := String(unit_id_value)
		var by_producer: Dictionary = {}
		for route_value in _generic_playable_routes[unit_id] as Array:
			var route := route_value as Dictionary
			var validation := _validate_retail_command(content_db, expected_pack_root, route)
			var error := String(validation.get("error", ""))
			if error != "":
				validation_errors.append(error)
			else:
				by_producer[String(route.get("producer_kind", ""))] = validation
		route_validated[unit_id] = by_producer
	var hero_validated: Dictionary = {}
	for unit_id_value in _hero_routes.keys():
		var unit_id := String(unit_id_value)
		var by_producer: Dictionary = {}
		for route_value in _hero_routes[unit_id] as Array:
			var route := route_value as Dictionary
			var validation := _validate_retail_command(content_db, expected_pack_root, route, Vector2i.ZERO)
			var error := String(validation.get("error", ""))
			if error != "":
				validation_errors.append(error)
			else:
				by_producer[String(route.get("producer_kind", ""))] = validation
		hero_validated[unit_id] = by_producer
	var ability_validated: Dictionary = {}
	for unit_id_value in _hero_ability_specs.keys():
		var unit_id := String(unit_id_value)
		var by_ability: Dictionary = {}
		for ability_value in _hero_ability_specs[unit_id] as Array:
			var ability := ability_value as Dictionary
			var ability_id := String(ability.get("ability_id", ""))
			var spec := {
				"image_id": String(ability.get("icon_id", "")),
				"label_id": String(ability.get("label_id", "")),
				"tooltip_id": String(ability.get("tooltip_id", "")),
				"fallback_label": String(ability.get("fallback_label", "")),
				"fallback_tooltip": String(ability.get("fallback_tooltip", "")),
				"runtime_object_id": String(ability.get("runtime_object_id", "")),
			}
			var validation := _validate_retail_command(content_db, expected_pack_root, spec, Vector2i.ZERO)
			var error := String(validation.get("error", ""))
			if error != "":
				validation_errors.append(error)
			else:
				by_ability[ability_id] = {"spec": spec, "validation": validation}
		ability_validated[unit_id] = by_ability
	var action_validated: Dictionary = {}
	for spec_value in _retail_action_specs:
		var spec: Dictionary = spec_value
		var action_id := String(spec["action_id"])
		var is_construct := action_id.begins_with("construct_")
		# Construct buttons for data-driven buildings with no UC image id bind
		# text-only ONLY when the spec carries the reviewer-visible authored
		# fallback marker; anything else fails closed like any other command.
		if String(spec.get("image_id", "")).strip_edges() == "":
			if not bool(spec.get("authored_fallback", false)):
				validation_errors.append("Construct command '%s' has no UC image id and no recorded authored fallback." % action_id)
				continue
			retail_bind_diagnostics.append(
				"authored-fallback-not-retail: '%s' is text-only with recorded English text — no UC image/string ids exist in the selected pack" % action_id
			)
			action_validated[action_id] = {
				"texture": null,
				"label": String(spec.get("fallback_label", action_id)),
				"tooltip": String(spec.get("fallback_tooltip", "")),
				"path": "",
				"source_size": Vector2i.ZERO,
				"aspect_ratio": 1.0,
				"text_only": true,
			}
			continue
		var source_size := Vector2i(64, 64) if is_construct else Vector2i(63, 63)
		var validation := _validate_retail_command(content_db, expected_pack_root, spec, source_size)
		var error := String(validation.get("error", ""))
		if error != "":
			# Construct art may be an incomplete cook. Text-only demotion keeps
			# the LOCALIZED strings (never hand-written text) and is recorded in
			# the bind diagnostics; if the strings are gone too, fail closed.
			# Doc-driven faction specs (authored_fallback + structure doc icon)
			# demote to their recorded honest fallback text instead — a broken
			# faction icon must never abort the bind or borrow other art.
			if is_construct and bool(spec.get("authored_fallback", false)):
				retail_bind_diagnostics.append(
					"construct-art-missing-recorded: '%s' is text-only with recorded English text — icon validation failed: %s" % [action_id, error]
				)
				action_validated[action_id] = {
					"texture": null,
					"label": String(spec.get("fallback_label", action_id)),
					"tooltip": String(spec.get("fallback_tooltip", "")),
					"path": "",
					"source_size": Vector2i.ZERO,
					"aspect_ratio": 1.0,
					"text_only": true,
				}
			elif is_construct:
				var text_label := String(content_db.get_retail_string(String(spec.get("label_id", "")), _MISSING_RETAIL_STRING))
				var text_tooltip := String(content_db.get_retail_string(String(spec.get("tooltip_id", "")), _MISSING_RETAIL_STRING))
				if text_label == _MISSING_RETAIL_STRING or text_tooltip == _MISSING_RETAIL_STRING:
					validation_errors.append(error)
				else:
					retail_bind_diagnostics.append(
						"construct-art-missing-recorded: '%s' is text-only with localized strings — icon validation failed: %s" % [action_id, error]
					)
					action_validated[action_id] = {
						"texture": null,
						"label": text_label,
						"tooltip": text_tooltip,
						"path": "",
						"source_size": Vector2i.ZERO,
						"aspect_ratio": 1.0,
						"text_only": true,
					}
			else:
				validation_errors.append(error)
		else:
			action_validated[action_id] = validation
	var expansion_validated: Dictionary = {}
	for kind_value in EXPANSION_COMMAND_SPECS.keys():
		var kind := String(kind_value)
		var expansion_spec: Dictionary = (EXPANSION_COMMAND_SPECS[kind] as Dictionary).duplicate()
		expansion_spec["action_id"] = "expansion_%s" % kind
		var validation := _validate_retail_command(content_db, expected_pack_root, expansion_spec, Vector2i(64, 64))
		var error := String(validation.get("error", ""))
		if error != "":
			validation_errors.append(error)
		else:
			expansion_validated[kind] = validation
	# Doc-driven pad commands for every faction the localized Men table does not
	# cover. A broken faction icon demotes to the recorded honest text (the same
	# contract as the construct sockets above) instead of aborting the bind — an
	# unbindable icon must never silently empty a fortress plot.
	for kind_value in _manifest_expansion_specs.keys():
		var expansion_kind := String(kind_value)
		if expansion_validated.has(expansion_kind) or EXPANSION_COMMAND_SPECS.has(expansion_kind):
			continue
		var doc_spec: Dictionary = (_manifest_expansion_specs[expansion_kind] as Dictionary).duplicate()
		doc_spec["action_id"] = "expansion_%s" % expansion_kind
		doc_spec["authored_fallback"] = true
		var doc_validation := _validate_retail_command(content_db, expected_pack_root, doc_spec, Vector2i(64, 64))
		var doc_error := String(doc_validation.get("error", ""))
		if doc_error == "":
			expansion_validated[expansion_kind] = doc_validation
			continue
		retail_bind_diagnostics.append(
			"expansion-art-missing-recorded: '%s' is text-only with recorded English text — icon validation failed: %s" % [String(doc_spec["action_id"]), doc_error]
		)
		expansion_validated[expansion_kind] = {
			"texture": null,
			"label": String(doc_spec.get("fallback_label", expansion_kind)),
			"tooltip": String(doc_spec.get("fallback_tooltip", "")),
			"path": "",
			"source_size": Vector2i.ZERO,
			"aspect_ratio": 1.0,
			"text_only": true,
		}
	var portrait_validated: Dictionary = {}
	for spec_value in _retail_portrait_specs:
		var spec: Dictionary = spec_value
		# Unit SelectPortraits are authored 191x191; hero SelectPortraits ("HP*")
		# are authored 192x192. Trust the pack-declared square size in that band.
		var expected_portrait_size := RETAIL_PORTRAIT_SOURCE_SIZE
		var portrait_runtime_id := String(spec.get("runtime_object_id", ""))
		if portrait_runtime_id != "":
			var portrait_runtime: Dictionary = content_db.get_playable_unit_runtime(portrait_runtime_id)
			var portrait_metadata: Dictionary = (
				(portrait_runtime.get("registration", {}) as Dictionary).get("imageBindingMetadata", {}) as Dictionary
			).get(String(spec["image_id"]), {}) as Dictionary
			var declared := Vector2i(int(portrait_metadata.get("width", 0)), int(portrait_metadata.get("height", 0)))
			if declared.x == declared.y and declared.x in [191, 192]:
				expected_portrait_size = declared
		var validation := _validate_retail_image(
			content_db,
			expected_pack_root,
			String(spec["image_id"]),
			expected_portrait_size,
			portrait_runtime_id
		)
		var error := String(validation.get("error", ""))
		if error != "":
			validation_errors.append(error)
		else:
			portrait_validated[String(spec["unit_id"])] = validation
	var control_bar_validation: Dictionary = {}
	if not use_apt:
		control_bar_validation = _validate_retail_image(
			content_db,
			expected_pack_root,
			RETAIL_COMMAND_BAR_IMAGE_ID,
			RETAIL_COMMAND_BAR_SOURCE_SIZE
		)
		var control_bar_error := String(control_bar_validation.get("error", ""))
		if control_bar_error != "":
			validation_errors.append(control_bar_error)
	if not validation_errors.is_empty():
		return "Private retail HUD is incomplete: %s" % "; ".join(validation_errors)
	_retail_expansion_validated = expansion_validated
	_generic_playable_route_validations = route_validated
	_hero_route_validations = hero_validated
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		_apply_retail_command(spec, validated[String(spec["unit_id"])])
	for spec_value in _retail_action_specs:
		var spec: Dictionary = spec_value
		_apply_retail_action(spec, action_validated[String(spec["action_id"])])
	for spec in _hero_command_specs:
		var unit_id := String(spec["unit_id"])
		var producer_kind := String(((_hero_routes[unit_id] as Array)[0] as Dictionary).get("producer_kind", ""))
		_apply_retail_hero_command(spec, (hero_validated[unit_id] as Dictionary)[producer_kind])
	for unit_id_value in ability_validated.keys():
		var unit_id := String(unit_id_value)
		for ability_id_value in (ability_validated[unit_id] as Dictionary).keys():
			var ability_id := String(ability_id_value)
			var button: Button = (hero_ability_buttons.get(unit_id, {}) as Dictionary).get(ability_id)
			if button == null:
				continue
			var row: Dictionary = (ability_validated[unit_id] as Dictionary)[ability_id]
			_apply_retail_ability_command(button, (row.get("spec", {}) as Dictionary), (row.get("validation", {}) as Dictionary))
	for spec_value in _retail_portrait_specs:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		_retail_portrait_textures[unit_id] = portrait_validated[unit_id]["texture"]
	var portrait_bindings: Dictionary = {}
	for spec_value in _retail_portrait_specs:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		portrait_bindings[unit_id] = {
			"image_id": String(spec["image_id"]),
			"path": String(portrait_validated[unit_id]["path"]),
			"source_size": Vector2i(portrait_validated[unit_id]["source_size"]),
		}
	selection_portrait.set_meta("retail_portrait_bindings", portrait_bindings)
	retail_portraits_bound = true
	if use_apt:
		var frame_texture := retail_apt_runtime.exact_atlas_texture(RETAIL_PALANTIR_FRAME_ATLAS)
		_retail_palantir_atlas = retail_apt_runtime.exact_atlas_texture(RETAIL_PALANTIR_ATLAS)
		if _retail_palantir_atlas == null:
			return "Private retail HUD APT did not expose the Palantir UI atlas."
		var frame_pieces: Array[Dictionary] = []
		for piece_value in RETAIL_FRAME_PIECES:
			frame_pieces.append((piece_value as Dictionary).duplicate())
		retail_control_bar_bound = retail_control_bar_frame.bind_retail_composition(
			frame_texture,
			"PalantirFrame_GoodDouble",
			RETAIL_PALANTIR_FRAME_ATLAS,
			RETAIL_PALANTIR_FRAME_SOURCE_SIZE,
			frame_pieces
		)
		retail_control_bar_frame.visible = retail_control_bar_bound
		_bind_retail_bottom_left_art(content_db, expected_pack_root)
		# The source-proven subset is usable while its single seven-gate rendered
		# capture blocker keeps parity_ready false and visible in diagnostics.
		retail_apt_bound = retail_apt_runtime.presentation_ready and retail_apt_runtime.contract_ready
		retail_apt_runtime.visible = retail_apt_bound
		retail_control_bar_bound = retail_control_bar_bound and retail_apt_bound
	else:
		retail_control_bar_bound = retail_control_bar_frame.bind_retail_shell(
			control_bar_validation["texture"] as Texture2D,
			RETAIL_COMMAND_BAR_IMAGE_ID,
			String(control_bar_validation["path"]),
			Vector2i(control_bar_validation["source_size"])
		)
		retail_control_bar_frame.visible = retail_control_bar_bound
	retail_train_commands_bound = true
	retail_train_command_bound = true
	retail_presentation_bound = retail_train_commands_bound and retail_portraits_bound and retail_control_bar_bound
	if not _retail_command_specs.is_empty() and train_button != null:
		var first_unit := String(_retail_command_specs[0]["unit_id"])
		_retail_train_label = String(_retail_train_labels.get(first_unit, ""))
		retail_train_icon_aspect_ratio = float(train_button.get_meta("retail_icon_aspect_ratio", 0.0))
	# Seed stance/formation icons to their default retail art after pack bind.
	set_active_stance("Battle")
	set_active_formation("Line")
	return "" if retail_presentation_bound else "Private retail HUD failed to apply its validated presentation atomically."


func _validate_retail_command(
	content_db,
	expected_pack_root: String,
	spec: Dictionary,
	exact_size: Vector2i = Vector2i(64, 64)
) -> Dictionary:
	var image_id := String(spec["image_id"])
	var label_id := String(spec["label_id"])
	var tooltip_id := String(spec["tooltip_id"])
	var image_validation := _validate_retail_image(
		content_db, expected_pack_root, image_id, exact_size,
		String(spec.get("runtime_object_id", "")),
		String(spec.get("structure_object_id", ""))
	)
	if String(image_validation.get("error", "")) != "":
		return image_validation

	var runtime_object_id := String(spec.get("runtime_object_id", ""))
	var runtime_registration: Dictionary = {}
	if runtime_object_id != "":
		runtime_registration = (content_db.get_playable_unit_runtime(runtime_object_id).get("registration", {}) as Dictionary)
	var runtime_strings: Dictionary = runtime_registration.get("stringBindings", {}) as Dictionary
	# Command ids retail references but never localizes. RotWK's own
	# data/ini/commandbutton.ini:3281 sets Faramir's DescriptLabel to
	# CONTROLBAR:ToolTipFaramirMount and no retail .str file defines it (same
	# for CONTROLBAR:SpecialAbilityShieldBubble at :2340). There is no text to
	# resolve and never will be, so fail-closing here blocked the whole Men HUD
	# — and with it every Men match, single-player and multiplayer. The
	# importer records these ids explicitly; render blank and diagnose.
	var runtime_source_null: Dictionary = {}
	for source_null_value in (runtime_registration.get("sourceNullStringIds", []) as Array):
		runtime_source_null[String(source_null_value)] = true
	var fallback_label := String(spec.get("fallback_label", "")).strip_edges()
	var fallback_tooltip := String(spec.get("fallback_tooltip", "")).strip_edges()
	# Fail closed on missing localized strings: a non-empty id that resolves
	# nowhere is a pack gap and aborts the bind — hand-written English is never
	# silently presented as retail text. The ONLY sanctioned fallback is an
	# explicitly marked spec ("authored_fallback": true, recorded in
	# retail_bind_diagnostics) for content whose pack ships no strings at all
	# (e.g. faction buildings with unconverted Elven CONTROLBAR keys).
	var spec_name := String(spec.get("action_id", spec.get("button_name", spec.get("image_id", ""))))
	var authored_fallback := bool(spec.get("authored_fallback", false))
	var label_text := ""
	if label_id.strip_edges() == "":
		if not authored_fallback:
			return {"error": "Command '%s' has no localized label id and no recorded authored fallback." % spec_name}
		label_text = fallback_label if fallback_label != "" else spec_name
	else:
		label_text = String(runtime_strings.get(label_id, content_db.get_retail_string(label_id, _MISSING_RETAIL_STRING)))
		if label_text == _MISSING_RETAIL_STRING:
			if not runtime_source_null.has(label_id):
				return {"error": "Required localized string '%s' is missing." % label_id}
			label_text = ""
			retail_bind_diagnostics.append(
				"retail-unlocalized-label: '%s' references '%s', which retail's own string table never defines" % [spec_name, label_id]
			)
	var tooltip_text := ""
	if tooltip_id.strip_edges() == "":
		if authored_fallback and fallback_tooltip != "":
			tooltip_text = fallback_tooltip
		elif authored_fallback:
			tooltip_text = label_text
		else:
			return {"error": "Command '%s' has no localized tooltip id and no recorded authored fallback." % spec_name}
	else:
		tooltip_text = String(runtime_strings.get(tooltip_id, content_db.get_retail_string(tooltip_id, _MISSING_RETAIL_STRING)))
		if tooltip_text == _MISSING_RETAIL_STRING:
			if not runtime_source_null.has(tooltip_id):
				return {"error": "Required localized string '%s' is missing." % tooltip_id}
			tooltip_text = label_text
			retail_bind_diagnostics.append(
				"retail-unlocalized-tooltip: '%s' references '%s', which retail's own string table never defines" % [spec_name, tooltip_id]
			)
	if authored_fallback and (label_id.strip_edges() == "" or tooltip_id.strip_edges() == ""):
		retail_bind_diagnostics.append(
			"authored-fallback-not-retail: '%s' uses recorded English text ('%s') — no localized string exists in the selected pack" % [spec_name, label_text]
		)
	image_validation["label"] = label_text
	image_validation["tooltip"] = tooltip_text
	return image_validation


func _same_pack_root(left: String, right: String) -> bool:
	## Windows path separators and simplify_path drift must not fail-closed a
	## valid image that was loaded from the selected bundle.
	if left == right:
		return true
	var a := left.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()
	var b := right.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()
	return a != "" and a == b


func set_allowed_image_pack_roots(roots: Array) -> void:
	## Records every pack root runtime-backed images may resolve from. Public so
	## consumer runners can exercise the multi-pack acceptance seam directly;
	## bind_retail_train_commands always drives it in production.
	_allowed_image_pack_roots.clear()
	for root_value in roots:
		var root := String(root_value).strip_edges()
		if root == "":
			continue
		var duplicate := false
		for existing in _allowed_image_pack_roots:
			if _same_pack_root(existing, root):
				duplicate = true
				break
		if not duplicate:
			_allowed_image_pack_roots.append(root)


func _pack_root_allowed(image_pack_root: String) -> bool:
	## Fail closed for images with no pack backing; otherwise accept any
	## recorded host/faction pack root.
	if image_pack_root.strip_edges() == "":
		return false
	for allowed in _allowed_image_pack_roots:
		if _same_pack_root(image_pack_root, allowed):
			return true
	return false


func _validate_retail_image(
	content_db, expected_pack_root: String, image_id: String, exact_size: Vector2i,
	runtime_object_id: String = "", structure_object_id: String = ""
) -> Dictionary:
	var image_definition: Dictionary = content_db.get_retail_ui_image(image_id)
	if structure_object_id != "":
		# Structure docs carry their converted construct-button/portrait crops
		# under registration.presentation.imageBindings (faction packs).
		var structure_runtime: Dictionary = content_db.get_playable_structure_runtime(structure_object_id)
		if structure_runtime.is_empty():
			return {"error": "Playable-structure runtime '%s' is missing." % structure_object_id}
		var structure_presentation := (structure_runtime.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary
		var structure_metadata := (structure_presentation.get("imageBindingMetadata", {}) as Dictionary).get(image_id, {}) as Dictionary
		image_definition = {
			"_pack_root": String(structure_runtime.get("_pack_root", "")),
			"path": String((structure_presentation.get("imageBindings", {}) as Dictionary).get(image_id, "")),
		}
		if not structure_metadata.is_empty():
			image_definition["width"] = int(structure_metadata.get("width", 0))
			image_definition["height"] = int(structure_metadata.get("height", 0))
	elif runtime_object_id != "":
		var runtime: Dictionary = content_db.get_playable_unit_runtime(runtime_object_id)
		if runtime.is_empty():
			return {"error": "Playable-unit runtime '%s' is missing." % runtime_object_id}
		var registration := runtime.get("registration", {}) as Dictionary
		var metadata := (registration.get("imageBindingMetadata", {}) as Dictionary).get(image_id, {}) as Dictionary
		image_definition = {
			"_pack_root": String(runtime.get("_pack_root", "")),
			"path": String((registration.get("imageBindings", {}) as Dictionary).get(image_id, "")),
		}
		if not metadata.is_empty():
			image_definition["width"] = int(metadata.get("width", 0))
			image_definition["height"] = int(metadata.get("height", 0))
	elif image_definition.is_empty():
		return {"error": "Required UI manifest image '%s' is missing." % image_id}
	var image_pack_root := String(image_definition.get("_pack_root", ""))
	if runtime_object_id != "" or structure_object_id != "":
		# Runtime-backed unit/structure images may live in a supplemental
		# faction pack: accept the host pack or any manifest-declared faction
		# pack root, and fail closed when the image has no pack backing at all.
		if not _pack_root_allowed(image_pack_root):
			return {"error": "Required UI image '%s' did not come from the selected or faction private packs." % image_id}
	elif not _same_pack_root(image_pack_root, expected_pack_root) and not _pack_root_allowed(image_pack_root):
		# Shared HUD chrome (the command-bar buttons, fortress expansion icons,
		# SGCommandBar) is host-pack payload. An expansion faction cooks as a
		# LEAN supplemental pack that deliberately ships none of it — the host
		# pinned to BFME2 1.06 bytes owns those surfaces and the supplement
		# borrows them. Requiring strict equality with the selected pack made
		# every Angmar match fail HUD validation on images the mounted host pack
		# was already providing. Accept the recorded host/faction roots, and
		# still fail closed for an image with no pack backing at all.
		return {"error": "Required UI image '%s' did not come from the selected or faction private packs." % image_id}

	var image_path := ""
	if structure_object_id != "":
		image_path = String(content_db.resolve_playable_structure_image_path(structure_object_id, image_id))
	elif runtime_object_id != "":
		image_path = String(content_db.resolve_playable_unit_image_path(runtime_object_id, image_id))
	else:
		image_path = String(content_db.resolve_retail_ui_image_path(image_id))
	if image_path == "":
		return {"error": "Required UI image '%s' does not resolve inside the selected private pack." % image_id}
	if image_path.get_extension().to_lower() != "png":
		return {"error": "Required UI image '%s' must resolve to a PNG, got '%s'." % [image_id, image_path.get_file()]}
	if not bool(content_db.is_resolved_asset_path(image_path)):
		return {"error": "Required UI image '%s' resolved outside the mounted content-pack boundary." % image_id}

	var image_file := FileAccess.open(image_path, FileAccess.READ)
	if image_file == null:
		return {"error": "Required UI image '%s' could not be opened at its resolved pack path." % image_id}
	var encoded_size := image_file.get_length()
	var encoded := image_file.get_buffer(encoded_size) if encoded_size > 0 and encoded_size <= MAX_RETAIL_COMMAND_ICON_BYTES else PackedByteArray()
	image_file.close()
	if encoded_size <= 0 or encoded_size > MAX_RETAIL_COMMAND_ICON_BYTES:
		return {"error": "Required UI image '%s' has an unsafe encoded size of %d bytes." % [image_id, encoded_size]}
	if encoded.size() < 33 or not _has_png_signature(encoded):
		return {"error": "Required UI image '%s' could not be decoded as PNG (invalid signature)." % image_id}
	if _png_u32_be(encoded, 8) != 13 or encoded.slice(12, 16).get_string_from_ascii() != "IHDR":
		return {"error": "Required UI image '%s' could not be decoded as PNG (invalid IHDR)." % image_id}
	var header_width := _png_u32_be(encoded, 16)
	var header_height := _png_u32_be(encoded, 20)
	if (
		header_width <= 0
		or header_height <= 0
		or header_width > MAX_RETAIL_COMMAND_ICON_DIMENSION
		or header_height > MAX_RETAIL_COMMAND_ICON_DIMENSION
	):
		return {"error": "Required UI image '%s' has unsafe PNG dimensions %dx%d." % [image_id, header_width, header_height]}
	var declared_width := int(image_definition.get("width", header_width))
	var declared_height := int(image_definition.get("height", header_height))
	if declared_width != header_width or declared_height != header_height:
		return {"error": "Required UI image '%s' declares %dx%d but its PNG header is %dx%d." % [image_id, declared_width, declared_height, header_width, header_height]}

	var decoded := Image.new()
	var decode_error := decoded.load_png_from_buffer(encoded)
	if decode_error != OK or decoded.is_empty():
		return {"error": "Required UI image '%s' could not be decoded as PNG (error %d)." % [image_id, decode_error]}
	var source_width := decoded.get_width()
	var source_height := decoded.get_height()
	if (
		source_width <= 0
		or source_height <= 0
		or source_width > MAX_RETAIL_COMMAND_ICON_DIMENSION
		or source_height > MAX_RETAIL_COMMAND_ICON_DIMENSION
	):
		return {"error": "Required UI image '%s' has unsafe decoded dimensions %dx%d." % [image_id, source_width, source_height]}
	if declared_width != source_width or declared_height != source_height:
		return {"error": "Required UI image '%s' decoded to %dx%d but its manifest declares %dx%d." % [
			image_id,
			source_width,
			source_height,
			declared_width,
			declared_height,
		]}
	if exact_size.x > 0 and exact_size.y > 0 and Vector2i(source_width, source_height) != exact_size:
		return {"error": "Required UI image '%s' must be exactly %dx%d, got %dx%d." % [
			image_id,
			exact_size.x,
			exact_size.y,
			source_width,
			source_height,
		]}

	var texture := ImageTexture.create_from_image(decoded)
	if texture == null:
		return {"error": "Required UI image '%s' decoded but could not create a Godot texture." % image_id}
	return {
		"error": "",
		"texture": texture,
		"path": image_path,
		"source_size": Vector2i(source_width, source_height),
		"aspect_ratio": float(source_width) / float(source_height),
	}


func _apply_retail_command(spec: Dictionary, validation: Dictionary) -> void:
	var unit_id := String(spec["unit_id"])
	var button: Button = train_buttons[unit_id]
	button.icon = validation["texture"] as Texture2D
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", 48)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.text = ""
	button.tooltip_text = String(validation["tooltip"])
	button.set_meta("retail_icon_id", String(spec["image_id"]))
	button.set_meta("retail_label_id", String(spec["label_id"]))
	button.set_meta("retail_tooltip_id", String(spec["tooltip_id"]))
	button.set_meta("retail_icon_path", String(validation["path"]))
	button.set_meta("retail_icon_source_size", Vector2i(validation["source_size"]))
	button.set_meta("retail_icon_aspect_ratio", float(validation["aspect_ratio"]))
	_retail_train_labels[unit_id] = String(validation["label"])


func _apply_retail_hero_command(spec: Dictionary, validation: Dictionary) -> void:
	var unit_id := String(spec["unit_id"])
	var button: Button = hero_buttons.get(unit_id)
	if button == null:
		return
	button.icon = validation["texture"] as Texture2D
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", 56)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.text = ""
	button.tooltip_text = String(validation["tooltip"])
	button.set_meta("retail_icon_id", String(spec["image_id"]))
	button.set_meta("retail_label_id", String(spec["label_id"]))
	button.set_meta("retail_tooltip_id", String(spec["tooltip_id"]))
	button.set_meta("retail_label", String(validation["label"]))
	button.set_meta("retail_icon_path", String(validation["path"]))
	button.set_meta("retail_icon_source_size", Vector2i(validation["source_size"]))
	_retail_train_labels[unit_id] = String(validation["label"])


## One command-bar action button per registered spec. Shared by build() and
## the bind path: construct action specs may be registered AFTER build (the
## post-reload chrome refresh, incl. a lockstep guest's own-faction surface),
## and the docstring on configure_manifest_construct_kinds promises those
## late-added actions receive their button nodes during bind.
func _ensure_action_button(spec: Dictionary) -> Button:
	var action_id := String(spec["action_id"])
	if unit_action_buttons.has(action_id):
		return unit_action_buttons[action_id]
	var button := Button.new()
	button.name = String(spec["button_name"])
	button.text = String(spec["button_name"])
	button.custom_minimum_size = Vector2(54, 54)
	button.disabled = true
	button.visible = false
	button.set_meta("action_id", action_id)
	button.set_meta("preferred_slot", int(spec.get("preferred_slot", -1)))
	_style_button(button)
	if action_id == "attack_move":
		button.pressed.connect(func() -> void: attack_move_requested.emit())
	elif action_id == "stop":
		button.pressed.connect(func() -> void: stop_requested.emit())
	elif action_id == "stance":
		button.pressed.connect(func() -> void: stance_requested.emit())
	elif action_id == "formation":
		button.pressed.connect(func() -> void: formation_requested.emit())
	elif action_id.begins_with("construct_"):
		button.pressed.connect(_emit_construct_requested.bind(action_id.trim_prefix("construct_")))
	elif action_id == "upgrade_archery_range_level2":
		button.pressed.connect(func() -> void: structure_upgrade_requested.emit("Upgrade_GondorArcheryRangeLevel2"))
	_place_command_button(button, 0)
	unit_action_buttons[action_id] = button
	return button


func _apply_retail_action(spec: Dictionary, validation: Dictionary) -> void:
	var action_id := String(spec["action_id"])
	var button: Button = _ensure_action_button(spec)
	if bool(validation.get("text_only", false)) or validation.get("texture") == null:
		button.icon = null
		button.text = String(validation.get("label", spec.get("fallback_label", action_id)))
		button.tooltip_text = String(validation.get("tooltip", button.text))
		button.set_meta("retail_label", button.text)
		return
	button.icon = validation["texture"] as Texture2D
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", 56)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.text = ""
	button.tooltip_text = String(validation["tooltip"])
	button.set_meta("retail_icon_id", String(spec["image_id"]))
	button.set_meta("retail_label_id", String(spec["label_id"]))
	button.set_meta("retail_tooltip_id", String(spec["tooltip_id"]))
	button.set_meta("retail_label", String(validation["label"]))
	button.set_meta("retail_icon_path", String(validation["path"]))
	button.set_meta("retail_icon_source_size", Vector2i(validation["source_size"]))


func _clear_retail_command_bindings(hide_commands: bool) -> void:
	retail_train_command_bound = false
	retail_train_commands_bound = false
	retail_portraits_bound = false
	retail_control_bar_bound = false
	retail_apt_bound = false
	retail_presentation_bound = false
	retail_train_icon_aspect_ratio = 0.0
	_retail_train_label = ""
	_retail_train_labels.clear()
	_retail_portrait_textures.clear()
	_allowed_image_pack_roots.clear()
	if selection_portrait != null:
		selection_portrait.texture = null
		selection_portrait.visible = false
		if selection_portrait.has_meta("retail_portrait_bindings"):
			selection_portrait.remove_meta("retail_portrait_bindings")
		if selection_portrait.has_meta("retail_active_portrait_unit_id"):
			selection_portrait.remove_meta("retail_active_portrait_unit_id")
	if hide_commands:
		_apply_private_fail_closed_presentation()
	else:
		_restore_public_presentation()
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		var button: Button = train_buttons.get(String(spec["unit_id"]))
		if button == null:
			continue
		button.icon = null
		button.text = String(spec["fallback_label"])
		button.tooltip_text = String(spec["fallback_tooltip"])
		button.disabled = hide_commands
		button.visible = not hide_commands
		for metadata_key in [
			"retail_icon_id",
			"retail_label_id",
			"retail_tooltip_id",
			"retail_icon_path",
			"retail_icon_source_size",
			"retail_icon_aspect_ratio",
		]:
			if button.has_meta(metadata_key):
				button.remove_meta(metadata_key)
	for spec in _hero_command_specs:
		var button: Button = hero_buttons.get(String(spec["unit_id"]))
		if button == null:
			continue
		button.icon = null
		button.text = String(spec["fallback_label"])
		button.tooltip_text = String(spec["fallback_tooltip"])
		button.disabled = hide_commands
		button.visible = not hide_commands
	if hero_selection_panel != null:
		hero_selection_panel.visible = false
	for spec_value in _retail_action_specs:
		var spec: Dictionary = spec_value
		var button: Button = unit_action_buttons.get(String(spec["action_id"]))
		if button == null:
			continue
		button.icon = null
		button.text = String(spec["button_name"])
		button.tooltip_text = ""
		button.disabled = hide_commands
		button.visible = not hide_commands


func _apply_private_fail_closed_presentation() -> void:
	## Private parity paints commands into the retail control-bar sockets.
	## Keep playable feedback/selection text visible — hiding them made orders
	## and stance changes look like "nothing happened" and blocked game text.
	private_parity_mode_active = true
	if synthetic_palantir_frame != null:
		synthetic_palantir_frame.fail_closed_private_shell()
	if retail_control_bar_frame != null:
		retail_control_bar_frame.fail_closed_private_shell()
		retail_control_bar_frame.visible = false
	if retail_apt_runtime != null:
		retail_apt_runtime.reset_runtime()
		retail_apt_runtime.visible = false
	for node_name in ["DiagnosticsPanel"]:
		var node := get_node_or_null(node_name) as Control
		if node != null:
			node.visible = false
	# Objective + feedback + control groups stay functional, but retail 1.06
	# has no such on-screen chrome (no objective banner, no group strip, no
	# bottom-right feedback box): the nodes keep state for runners while the
	# visuals hide in parity mode. Game messaging moves to the retail event
	# feed and tooltip surface.
	var empty := StyleBoxEmpty.new()
	for node_name in ["ObjectiveBanner", "ControlGroupStrip", "FeedbackPanel"]:
		var playable_node := get_node_or_null(node_name) as Control
		if playable_node != null:
			playable_node.visible = false
			if playable_node is PanelContainer:
				playable_node.add_theme_stylebox_override("panel", empty)
	for node_path in ["CommandPanel", "PalantirDock/ResourceStrip"]:
		var panel := get_node_or_null(node_path) as PanelContainer
		if panel != null:
			panel.add_theme_stylebox_override("panel", empty)
	# Selection summary still needs a dark plate so Albertus text is readable
	# over the battlefield and the control-bar art.
	if command_panel != null and selection_label != null:
		command_panel.z_index = maxi(command_panel.z_index, 6)
		selection_label.add_theme_color_override("font_color", Color("f0e6c8"))
		selection_label.clip_text = false
	for button_value in train_buttons.values():
		var button: Button = button_value
		for state in ["normal", "hover", "pressed", "focus", "disabled"]:
			button.add_theme_stylebox_override(state, empty)
	for button_value in unit_action_buttons.values():
		for state in ["normal", "hover", "pressed", "focus", "disabled"]:
			(button_value as Button).add_theme_stylebox_override(state, empty)


func _restore_public_presentation() -> void:
	private_parity_mode_active = false
	if synthetic_palantir_frame != null:
		synthetic_palantir_frame.show_public_synthetic_shell()
	if retail_control_bar_frame != null:
		retail_control_bar_frame.fail_closed_private_shell()
		retail_control_bar_frame.visible = false
	if retail_apt_runtime != null:
		retail_apt_runtime.reset_runtime()
		retail_apt_runtime.visible = false
	for node_name in ["ObjectiveBanner", "ControlGroupStrip", "FeedbackPanel"]:
		var node := get_node_or_null(node_name) as Control
		if node != null:
			node.visible = true
	var command_panel := get_node_or_null("CommandPanel") as PanelContainer
	if command_panel != null:
		command_panel.add_theme_stylebox_override("panel", _panel)
	var resource_strip := get_node_or_null("PalantirDock/ResourceStrip") as PanelContainer
	if resource_strip != null:
		resource_strip.add_theme_stylebox_override("panel", _panel)
	for button_value in train_buttons.values():
		_style_button(button_value as Button)
	for button_value in unit_action_buttons.values():
		_style_button(button_value as Button)


func _update_retail_selection_portrait(production: Array) -> void:
	if selection_portrait == null or not retail_presentation_bound:
		return
	selection_portrait.texture = null
	selection_portrait.visible = false
	for spec_value in _retail_portrait_specs:
		var unit_id := String((spec_value as Dictionary)["unit_id"])
		if production.has(unit_id) and _retail_portrait_textures.has(unit_id):
			_show_retail_portrait(unit_id)
			return
	if selection_portrait.has_meta("retail_active_portrait_unit_id"):
		selection_portrait.remove_meta("retail_active_portrait_unit_id")


func _show_retail_portrait(unit_id: String) -> void:
	selection_portrait.texture = _retail_portrait_textures.get(unit_id) as Texture2D
	selection_portrait.visible = selection_portrait.texture != null
	if selection_portrait.visible:
		selection_portrait.set_meta("retail_active_portrait_unit_id", unit_id)


func _apply_source_font(node: Node, font: FontFile) -> void:
	if node is Label:
		(node as Label).add_theme_font_override("font", font)
	elif node is Button:
		(node as Button).add_theme_font_override("font", font)
	for child in node.get_children():
		_apply_source_font(child, font)


func _png_u32_be(bytes: PackedByteArray, offset: int) -> int:
	if offset < 0 or offset + 4 > bytes.size():
		return -1
	return (int(bytes[offset]) << 24) | (int(bytes[offset + 1]) << 16) | (int(bytes[offset + 2]) << 8) | int(bytes[offset + 3])


func _has_png_signature(bytes: PackedByteArray) -> bool:
	return (
		bytes.size() >= 8
		and bytes[0] == 0x89
		and bytes[1] == 0x50
		and bytes[2] == 0x4e
		and bytes[3] == 0x47
		and bytes[4] == 0x0d
		and bytes[5] == 0x0a
		and bytes[6] == 0x1a
		and bytes[7] == 0x0a
	)


func set_control_groups(groups: Dictionary) -> void:
	for group in range(1, 10):
		var count := 0
		var values: Variant = groups.get(group, [])
		if typeof(values) == TYPE_ARRAY:
			count = (values as Array).size()
		var button: Button = group_buttons[group]
		button.text = "%d\n%s" % [group, str(count) if count > 0 else "-"]
		button.tooltip_text = "Group %d: click to recall, Ctrl+click to assign" % group


func show_diagnostics(text: String, visible: bool) -> void:
	diagnostics_label.text = text
	diagnostics_panel.visible = visible and not private_parity_mode_active


func show_pause(value: bool) -> void:
	pause_panel.visible = value
	# Unpausing must not leave the playtest sheet floating over the battle.
	if not value and playtest_panel != null:
		playtest_panel.visible = false
	if value:
		outcome_layer.visible = false


func show_outcome(winner: int, detail: String = "") -> void:
	pause_panel.visible = false
	outcome_title.text = "VICTORY" if winner == 0 else "DEFEAT"
	outcome_title.add_theme_color_override("font_color", Color("f4d785") if winner == 0 else Color("e37973"))
	outcome_detail.text = detail if detail != "" else ("The enemy fortress has fallen." if winner == 0 else "Your fortress has fallen.")
	outcome_layer.visible = true


func hide_outcome() -> void:
	outcome_layer.visible = false


func show_failure(message: String) -> void:
	failure_panel.visible = true
	var label := failure_panel.get_node("FailureMargin/FailureColumn/Message") as Label
	label.text = message


func hide_failure() -> void:
	failure_panel.visible = false


func apply_audio_values(music: float, voice: float, muted: bool) -> void:
	music_slider.set_value_no_signal(clampf(music, 0.0, 1.0))
	voice_slider.set_value_no_signal(clampf(voice, 0.0, 1.0))
	mute_toggle.set_pressed_no_signal(muted)


func _build_styles() -> void:
	_panel = StyleBoxFlat.new()
	_panel.bg_color = Color(0.018, 0.035, 0.055, 0.92)
	_panel.border_color = Color("6f8491")
	_panel.set_border_width_all(2)
	_panel.set_corner_radius_all(4)
	_panel.shadow_color = Color(0, 0, 0, 0.65)
	_panel.shadow_size = 8
	_panel.content_margin_left = 14
	_panel.content_margin_right = 14
	_panel.content_margin_top = 10
	_panel.content_margin_bottom = 10
	_normal_button = _button_box(Color("112a3d"), Color("617d91"))
	_hover_button = _button_box(Color("1e4e6c"), Color("a7c8d9"))
	_pressed_button = _button_box(Color("2e6785"), Color("e0d09a"))


func _button_box(background: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = background
	box.border_color = border
	box.set_border_width_all(2)
	box.set_corner_radius_all(3)
	box.content_margin_left = 10
	box.content_margin_right = 10
	box.content_margin_top = 7
	box.content_margin_bottom = 7
	return box


func _style_button(button: Button) -> void:
	button.add_theme_stylebox_override("normal", _normal_button)
	button.add_theme_stylebox_override("hover", _hover_button)
	button.add_theme_stylebox_override("pressed", _pressed_button)
	button.add_theme_stylebox_override("focus", _hover_button)
	button.add_theme_color_override("font_color", Color("c7dbe5"))
	button.add_theme_color_override("font_hover_color", Color("ffffff"))
	button.add_theme_font_size_override("font_size", 15)


func _build_objective_banner() -> void:
	var banner := PanelContainer.new()
	banner.name = "ObjectiveBanner"
	banner.set_anchors_preset(Control.PRESET_CENTER_TOP)
	banner.offset_left = -340
	banner.offset_top = 16
	banner.offset_right = 340
	banner.offset_bottom = 76
	banner.add_theme_stylebox_override("panel", _panel)
	banner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(banner)
	objective_label = Label.new()
	objective_label.name = "Objective"
	objective_label.text = "DESTROY THE ENEMY FORTRESS"
	objective_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	objective_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	objective_label.add_theme_font_size_override("font_size", 21)
	objective_label.add_theme_color_override("font_color", Color("e1d4ab"))
	banner.add_child(objective_label)


func _build_palantir() -> void:
	retail_apt_runtime = AptRuntimeScript.new()
	retail_apt_runtime.name = "RetailPalantirAptRuntime"
	retail_apt_runtime.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	retail_apt_runtime.mouse_filter = Control.MOUSE_FILTER_IGNORE
	retail_apt_runtime.z_index = 0
	retail_apt_runtime.visible = false
	add_child(retail_apt_runtime)
	retail_control_bar_frame = PalantirFrameScript.new()
	retail_control_bar_frame.name = "RetailControlBarFrame"
	retail_control_bar_frame.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	retail_control_bar_frame.offset_left = 0
	retail_control_bar_frame.offset_top = -RETAIL_PALANTIR_FRAME_DISPLAY_SIZE.y
	retail_control_bar_frame.offset_right = RETAIL_PALANTIR_DISPLAY_SIZE.x
	retail_control_bar_frame.offset_bottom = 0
	retail_control_bar_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	retail_control_bar_frame.z_index = 1
	retail_control_bar_frame.fail_closed_private_shell()
	retail_control_bar_frame.visible = false
	add_child(retail_control_bar_frame)
	var dock := Control.new()
	dock.name = "PalantirDock"
	dock.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	dock.offset_left = 0
	dock.offset_top = -RETAIL_PALANTIR_FRAME_DISPLAY_SIZE.y
	dock.offset_right = RETAIL_PALANTIR_DISPLAY_SIZE.x
	dock.offset_bottom = 0
	dock.mouse_filter = Control.MOUSE_FILTER_IGNORE
	dock.z_index = 2
	add_child(dock)
	# The ornamental control paints the Palantir backing and bezel. Keep it
	# behind the radar; drawing it afterward would cover the source map with its
	# opaque inner disc even though input and mapping still worked.
	synthetic_palantir_frame = PalantirFrameScript.new()
	synthetic_palantir_frame.name = "OrnamentalFrame"
	synthetic_palantir_frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dock.add_child(synthetic_palantir_frame)
	minimap = MinimapScript.new()
	minimap.name = "PalantirRadar"
	# Radar interior circle measured from the retail reference capture:
	# center (225, 198), radius 181 in the 1080p dock.
	minimap.position = RETAIL_RADAR_CENTER - Vector2(RETAIL_RADAR_RADIUS, RETAIL_RADAR_RADIUS)
	minimap.size = Vector2(RETAIL_RADAR_RADIUS, RETAIL_RADAR_RADIUS) * 2.0
	minimap.custom_minimum_size = minimap.size
	minimap.mouse_filter = Control.MOUSE_FILTER_STOP
	dock.add_child(minimap)
	_build_orb_buttons(dock)
	resource_strip = PanelContainer.new()
	resource_strip.name = "ResourceStrip"
	# The frame art's resource recess: dock x 68..367, y 306..342.
	resource_strip.position = Vector2(68, 306)
	resource_strip.size = Vector2(299, 36)
	resource_strip.add_theme_stylebox_override("panel", _panel)
	dock.add_child(resource_strip)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 34)
	resource_strip.add_child(row)
	var resource_icon := Label.new()
	resource_icon.text = "◆"
	resource_icon.add_theme_color_override("font_color", Color("d6aa55"))
	resource_icon.add_theme_font_size_override("font_size", 20)
	row.add_child(resource_icon)
	resource_label = Label.new()
	resource_label.name = "Resources"
	resource_label.text = "0"
	resource_label.add_theme_color_override("font_color", Color("f1d06e"))
	resource_label.add_theme_font_size_override("font_size", 18)
	row.add_child(resource_label)
	var cp_icon := Label.new()
	cp_icon.text = "⚔"
	cp_icon.add_theme_font_size_override("font_size", 18)
	row.add_child(cp_icon)
	command_points_label = Label.new()
	command_points_label.name = "CommandPoints"
	command_points_label.text = "0 / 200"
	command_points_label.add_theme_color_override("font_color", Color("d5e5ed"))
	row.add_child(command_points_label)


func _build_command_panel() -> void:
	command_panel = PanelContainer.new()
	command_panel.name = "CommandPanel"
	command_panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	command_panel.offset_left = 360
	command_panel.offset_top = -360
	command_panel.offset_right = 880
	command_panel.offset_bottom = 0
	command_panel.add_theme_stylebox_override("panel", _panel)
	command_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	command_panel.z_index = 4
	add_child(command_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	command_panel.add_child(column)
	var heading := Label.new()
	heading.name = "FactionHeading"
	heading.text = "FACTION"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_color_override("font_color", Color("dccb95"))
	heading.add_theme_font_size_override("font_size", 18)
	heading.clip_text = false
	column.add_child(heading)
	_faction_heading_label = heading
	selection_label = Label.new()
	selection_label.name = "SelectionSummary"
	selection_label.text = "No battalion selected"
	selection_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	selection_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	selection_label.add_theme_color_override("font_color", Color("d0e1e9"))
	selection_label.custom_minimum_size = Vector2(200, 44)
	selection_label.clip_text = false
	selection_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_child(selection_label)
	selection_portrait = TextureRect.new()
	selection_portrait.name = "SelectionPortrait"
	selection_portrait.custom_minimum_size = Vector2(76, 76)
	selection_portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	selection_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	selection_portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	selection_portrait.visible = false
	column.add_child(selection_portrait)
	_selection_rank_pips = RankPipsOverlay.new()
	_selection_rank_pips.name = "SelectionRankPips"
	_selection_rank_pips.set_anchors_preset(Control.PRESET_FULL_RECT)
	selection_portrait.add_child(_selection_rank_pips)
	command_grid = Control.new()
	command_grid.name = "CommandGrid"
	command_grid.custom_minimum_size = Vector2(216, 250)
	# The grid spans the whole command panel; with the default STOP filter it
	# swallowed every click aimed at column widgets it overlaps (the Cancel
	# training button most visibly). Buttons inside the grid still receive
	# input — IGNORE only exempts the grid itself.
	command_grid.mouse_filter = Control.MOUSE_FILTER_IGNORE
	column.add_child(command_grid)
	# Socket buttons are authored in command-panel coordinates (they ring the
	# palantir dish). Parenting them inside the VBox column pushed them down by
	# the heading/selection/portrait stack, which shoved the lower sockets off
	# the bottom of the screen — so they live on a dedicated layer pinned to
	# the panel origin instead.
	command_socket_layer = Control.new()
	command_socket_layer.name = "CommandSocketLayer"
	command_socket_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	command_panel.add_child(command_socket_layer)
	var slot_index := 0
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		var button := Button.new()
		button.name = String(spec["button_name"])
		button.text = String(spec["fallback_label"])
		button.tooltip_text = String(spec["fallback_tooltip"])
		button.custom_minimum_size = Vector2(54, 54)
		button.disabled = true
		button.visible = false
		_style_button(button)
		button.pressed.connect(_emit_train_requested.bind(unit_id))
		button.set_meta("retail_command_slot", int(spec.get("slot", slot_index + 1)))
		button.set_meta("runtime_object_id", String(spec.get("runtime_object_id", "")))
		_place_command_button(button, clampi(slot_index, 0, RETAIL_COMMAND_SLOT_SOURCE.size() - 1))
		slot_index += 1
		train_buttons[unit_id] = button
	hero_selection_panel = PanelContainer.new()
	hero_selection_panel.name = "HeroSelectionSurface"
	hero_selection_panel.position = Vector2(118, 28)
	hero_selection_panel.size = Vector2(300, 190)
	hero_selection_panel.add_theme_stylebox_override("panel", _panel)
	hero_selection_panel.visible = false
	hero_selection_panel.z_index = 6
	command_grid.add_child(hero_selection_panel)
	hero_selection_grid = Control.new()
	hero_selection_grid.name = "HeroRosterGrid"
	hero_selection_grid.custom_minimum_size = Vector2(288, 176)
	hero_selection_panel.add_child(hero_selection_grid)
	for spec in _hero_command_specs:
		var unit_id := String(spec["unit_id"])
		var ordinal := int(spec.get("roster_ordinal", 0))
		var button := Button.new()
		button.name = String(spec["button_name"])
		button.text = String(spec["fallback_label"])
		button.tooltip_text = String(spec["fallback_tooltip"])
		button.position = Vector2(float((ordinal - 1) % 4) * 68.0 + 8.0, float((ordinal - 1) / 4) * 78.0 + 8.0)
		button.size = Vector2(64, 64)
		button.disabled = true
		button.visible = false
		button.set_meta("retail_roster_ordinal", ordinal)
		button.set_meta("runtime_object_id", String(spec.get("runtime_object_id", "")))
		button.set_meta("retail_surface", "hero-roster")
		_style_button(button)
		button.pressed.connect(_emit_train_requested.bind(unit_id))
		hero_selection_grid.add_child(button)
		hero_buttons[unit_id] = button
	# Hero SPECIAL_POWER abilities ride the palantir command sockets at their
	# authored command-set slots (converted icons, cooldown sweep, level gates).
	# Slots beyond the retail ring (capture/attack/stop) stay hidden.
	for unit_id_value in _hero_ability_specs.keys():
		var ability_unit_id := String(unit_id_value)
		var buttons_for_unit: Dictionary = {}
		for ability_value in _hero_ability_specs[ability_unit_id] as Array:
			var ability := ability_value as Dictionary
			var ability_id := String(ability.get("ability_id", ""))
			var ability_button := Button.new()
			ability_button.name = "Ability_%s_%s" % [ability_unit_id.get_slice(".", -1), ability_id]
			ability_button.text = String(ability.get("fallback_label", ability_id))
			ability_button.tooltip_text = String(ability.get("fallback_tooltip", ""))
			ability_button.custom_minimum_size = Vector2(54, 54)
			ability_button.disabled = true
			ability_button.visible = false
			ability_button.set_meta("ability_id", ability_id)
			ability_button.set_meta("unit_id", ability_unit_id)
			ability_button.set_meta("retail_command_slot", int(ability.get("slot", 0)))
			ability_button.set_meta("targeting", String(ability.get("targeting", "self")))
			ability_button.set_meta("ability_label", String(ability.get("fallback_label", ability_id)))
			ability_button.set_meta("ability_tooltip_base", String(ability.get("fallback_tooltip", "")))
			_style_button(ability_button)
			ability_button.pressed.connect(_emit_ability_cast_requested.bind(ability_unit_id, ability_id))
			var sweep := TextureProgressBar.new()
			sweep.name = "CooldownSweep"
			sweep.fill_mode = TextureProgressBar.FILL_CLOCKWISE
			sweep.min_value = 0.0
			sweep.max_value = 1.0
			sweep.step = 0.001
			sweep.value = 0.0
			sweep.visible = false
			sweep.mouse_filter = Control.MOUSE_FILTER_IGNORE
			sweep.tint_progress = Color(0.02, 0.03, 0.03, 0.72)
			sweep.tint_under = Color(0.0, 0.0, 0.0, 0.0)
			sweep.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			ability_button.add_child(sweep)
			_place_command_button(ability_button, clampi(int(ability.get("slot", 1)) - 1, 0, RETAIL_COMMAND_SLOT_SOURCE.size() - 1))
			buttons_for_unit[ability_id] = ability_button
		hero_ability_buttons[ability_unit_id] = buttons_for_unit
	for spec_value in _retail_action_specs:
		_ensure_action_button(spec_value as Dictionary)
	if not _retail_command_specs.is_empty():
		train_button = train_buttons.get(String(_retail_command_specs[0]["unit_id"])) as Button
	elif not train_buttons.is_empty():
		train_button = train_buttons.values()[0] as Button
	else:
		train_button = Button.new()
		train_button.name = "TrainPlaceholder"
		train_button.visible = false
		command_grid.add_child(train_button)
	production_queue_label = Label.new()
	production_queue_label.name = "ProductionQueueLabel"
	production_queue_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	production_queue_label.add_theme_color_override("font_color", Color("d0e1e9"))
	production_queue_label.visible = false
	column.add_child(production_queue_label)
	production_progress = ProgressBar.new()
	production_progress.name = "ProductionProgress"
	production_progress.min_value = 0.0
	production_progress.max_value = 100.0
	production_progress.show_percentage = false
	production_progress.visible = false
	column.add_child(production_progress)
	cancel_production_button = Button.new()
	cancel_production_button.name = "CancelProduction"
	cancel_production_button.text = "Cancel training"
	cancel_production_button.visible = false
	cancel_production_button.disabled = true
	cancel_production_button.pressed.connect(_emit_cancel_production_requested)
	_style_button(cancel_production_button)
	column.add_child(cancel_production_button)
	# Armory research rides the doc-driven rows only (compiled research via
	# structure_upgrade_commands); the hardcoded forge button surface is retired.
	_build_powers_palette()
	_build_score_overlay()


func _place_command_button(button: Button, slot: int) -> void:
	button.position = RETAIL_COMMAND_SLOT_SOURCE[slot]
	button.size = RETAIL_COMMAND_SLOT_SIZE
	command_socket_layer.add_child(button)


func _layout_command_sockets() -> void:
	var occupied: Dictionary = {}
	# Battalion purchase rows hold their authored command-set slots while the
	# owning battalion is selected; runtime order buttons yield to them.
	for upgrade_id_value in _battalion_upgrade_buttons.keys():
		var upgrade_button := _battalion_upgrade_buttons[upgrade_id_value] as Button
		if not upgrade_button.visible:
			continue
		var upgrade_slot := int(upgrade_button.get_meta("battalion_upgrade_slot", 0)) - 1
		if upgrade_slot < 0 or upgrade_slot >= RETAIL_COMMAND_SLOT_SOURCE.size() or occupied.has(upgrade_slot):
			upgrade_button.visible = false
			continue
		upgrade_button.position = RETAIL_COMMAND_SLOT_SOURCE[upgrade_slot]
		upgrade_button.size = RETAIL_COMMAND_SLOT_SIZE
		occupied[upgrade_slot] = true
	for spec_value in _retail_command_specs:
		var train_button_row: Button = train_buttons.get(String((spec_value as Dictionary)["unit_id"]))
		if train_button_row != null and train_button_row.visible:
			var slot := int(train_button_row.get_meta("retail_command_slot", 0)) - 1
			if slot < 0 or slot >= RETAIL_COMMAND_SLOT_SOURCE.size() or occupied.has(slot):
				train_button_row.visible = false
				continue
			train_button_row.position = RETAIL_COMMAND_SLOT_SOURCE[slot]
			train_button_row.size = RETAIL_COMMAND_SLOT_SIZE
			occupied[slot] = true
	# Hero ability buttons occupy their authored command-set slots while the
	# hero is selected (visibility is owned by _update_hero_ability_buttons).
	for unit_id_value in hero_ability_buttons.keys():
		for button_value in (hero_ability_buttons[unit_id_value] as Dictionary).values():
			var ability_button := button_value as Button
			if not ability_button.visible:
				continue
			var ability_slot := int(ability_button.get_meta("retail_command_slot", 0)) - 1
			if ability_slot < 0 or ability_slot >= RETAIL_COMMAND_SLOT_SOURCE.size() or occupied.has(ability_slot):
				ability_button.visible = false
				continue
			ability_button.position = RETAIL_COMMAND_SLOT_SOURCE[ability_slot]
			ability_button.size = RETAIL_COMMAND_SLOT_SIZE
			occupied[ability_slot] = true
	# Preferred sockets first (retail layout: stance rides the top socket,
	# stop the bottom one), then everything else takes the first free slot.
	for spec_value in _retail_action_specs:
		var action_button: Button = unit_action_buttons.get(String((spec_value as Dictionary)["action_id"]))
		if action_button == null or not action_button.visible:
			continue
		var preferred := int(action_button.get_meta("preferred_slot", -1))
		if preferred >= 0 and preferred < RETAIL_COMMAND_SLOT_SOURCE.size() and not occupied.has(preferred):
			action_button.position = RETAIL_COMMAND_SLOT_SOURCE[preferred]
			action_button.size = RETAIL_COMMAND_SLOT_SIZE
			occupied[preferred] = true
			action_button.set_meta("socket_assigned", true)
		else:
			action_button.set_meta("socket_assigned", false)
	for spec_value in _retail_action_specs:
		var action_button: Button = unit_action_buttons.get(String((spec_value as Dictionary)["action_id"]))
		if action_button == null or not action_button.visible or bool(action_button.get_meta("socket_assigned", false)):
			continue
		var slot := 0
		while slot < RETAIL_COMMAND_SLOT_SOURCE.size() and occupied.has(slot):
			slot += 1
		if slot >= RETAIL_COMMAND_SLOT_SOURCE.size():
			action_button.visible = false
			continue
		action_button.position = RETAIL_COMMAND_SLOT_SOURCE[slot]
		action_button.size = RETAIL_COMMAND_SLOT_SIZE
		occupied[slot] = true


func _build_orb_buttons(dock: Control) -> void:
	for id in ["options", "powers", "score"]:
		var rect := RETAIL_ORB_RECTS[id] as Rect2
		var button := Button.new()
		button.name = "%sOrb" % String(id).capitalize()
		button.position = rect.position
		button.size = rect.size
		button.text = "" if id != "powers" else "0"
		button.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
		button.add_theme_stylebox_override("hover", StyleBoxEmpty.new())
		button.add_theme_stylebox_override("pressed", StyleBoxEmpty.new())
		button.add_theme_stylebox_override("disabled", StyleBoxEmpty.new())
		button.add_theme_font_size_override("font_size", 17)
		button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		button.mouse_entered.connect(func() -> void:
			ui_sound_requested.emit("Gui_UpgradeButtonGlow")
		)
		if id == "options":
			button.pressed.connect(func() -> void:
				ui_sound_requested.emit("Gui_PalantirButtonClick")
				pause_requested.emit()
			)
		elif id == "powers":
			button.pressed.connect(_toggle_powers_palette)
		else:
			button.pressed.connect(_toggle_score_overlay)
		dock.add_child(button)
		orb_buttons[id] = button
		if id == "powers":
			button.text = ""
			power_orb_label = Label.new()
			power_orb_label.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			# The count centers on the star gem, whose art sits slightly low in
			# the orb frame (REF-24/29/52).
			power_orb_label.offset_top = 6.0
			power_orb_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			power_orb_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			power_orb_label.add_theme_font_size_override("font_size", 26)
			power_orb_label.add_theme_color_override("font_color", Color.WHITE)
			power_orb_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
			power_orb_label.add_theme_constant_override("shadow_offset_x", 1)
			power_orb_label.add_theme_constant_override("shadow_offset_y", 1)
			power_orb_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
			button.add_child(power_orb_label)


# The spellbook is the retail palantir orb (retail_powers_orb.gd): tier rows
# by doc MP cost (5/10/15/25), authored purchase slots within a tier, doc
# costs, and sim-driven states. The icon-id list + grid below stay only as
# the doc-less fixture fallback so the screen still fails closed with 12
# locked entries; slots/costs mirror the authored MenSpellStoreCommandSet
# (commandset.ini), identical to the doc when present. The configured path
# never reads them.
const POWER_FALLBACK_GRID := {
	"SBGood_Heal": {"cost": 5, "slot": 1},
	"SBGood_RallyingCall": {"cost": 5, "slot": 2},
	"SBGood_ElvenWood": {"cost": 5, "slot": 3},
	"SBGood_MenLoneTower": {"cost": 10, "slot": 4},
	"SBGood_ArrowVolley": {"cost": 10, "slot": 5},
	"SBGood_TomBombadil": {"cost": 10, "slot": 6},
	"SBGood_SummonHobbits": {"cost": 10, "slot": 7},
	"SBGood_RohanAllies": {"cost": 15, "slot": 8},
	"SBGood_CloudBreak": {"cost": 15, "slot": 9},
	"SBGood_SummonDunedain": {"cost": 15, "slot": 10},
	"SBGood_ArmyoftheDead": {"cost": 25, "slot": 11},
	"SBGood_Earthquake": {"cost": 25, "slot": 12},
}


func _build_powers_palette() -> void:
	# The orb is its own screen; the control-bar cluster hides while it is open
	# so the palantir frame does not bleed through the backdrop.
	powers_palette = PowersOrbScript.new()
	powers_palette.name = "RetailPowersPalette"
	powers_palette.z_index = 10
	add_child(powers_palette)
	_configure_orb_rows()
	powers_palette.power_purchase_requested.connect(func(power_id: String, cost: int) -> void:
		power_purchase_requested.emit(power_id, cost)
	)
	powers_palette.power_cast_requested.connect(func(power_id: String) -> void:
		# Arming a targeted cast closes the orb (a close is the retail ACCEPT).
		_set_powers_palette_visible(false)
		power_cast_requested.emit(power_id)
	)
	powers_palette.powers_reset_requested.connect(func() -> void:
		powers_reset_requested.emit()
	)
	powers_palette.powers_accepted.connect(func() -> void:
		_set_powers_palette_visible(false)
	)
	powers_palette.feedback_requested.connect(func(text: String, is_error: bool) -> void:
		set_feedback(text, is_error)
	)


func _configure_orb_rows() -> void:
	## Pushes the current row set (doc-derived when configured, icon-id
	## fallback otherwise) into the orb; safe before/after build.
	if powers_palette == null or not powers_palette.has_method("configure"):
		return
	var rows := _spellbook_power_rows
	if rows.is_empty():
		rows = []
		for index in RETAIL_POWER_IMAGE_IDS.size():
			var fallback: Dictionary = POWER_FALLBACK_GRID.get(RETAIL_POWER_IMAGE_IDS[index], {"cost": 5, "slot": index + 1})
			rows.append({
				"power_id": RETAIL_POWER_IMAGE_IDS[index],
				"icon_id": RETAIL_POWER_IMAGE_IDS[index],
				"cost": int(fallback.get("cost", 5)),
				"purchase_slot": int(fallback.get("slot", index + 1)),
				"label": _retail_power_title(RETAIL_POWER_IMAGE_IDS[index]),
				"tooltip": "",
			})
	powers_palette.configure(rows, _retail_ui_font_cached)
	power_buttons = powers_palette.power_buttons
	power_points_label = powers_palette.power_points_value


func _spellbook_icon_id_for(power_id: String) -> String:
	for row_value in _spellbook_power_rows:
		if String((row_value as Dictionary).get("power_id", "")) == power_id:
			return String((row_value as Dictionary).get("icon_id", ""))
	return power_id


func _spellbook_icon_from_doc_pack(icon_id: String) -> Texture2D:
	## Per-faction icon binding: the spellbook document's own imageBindings
	## resolve inside ITS pack (the same fail-closed PNG discipline as the
	## ui_manifest validation, without requiring one per faction pack).
	if icon_id == "" or _spellbook_doc_pack_root == "" or not _spellbook_image_bindings.has(icon_id):
		return null
	var relative := String(_spellbook_image_bindings.get(icon_id, ""))
	if relative == "" or not ModLoader.is_safe_relative_path(relative):
		return null
	var image_path := ModLoader.resolve_pack_path(_spellbook_doc_pack_root, relative)
	if image_path == "" or image_path.get_extension().to_lower() != "png":
		return null
	var image_file := FileAccess.open(image_path, FileAccess.READ)
	if image_file == null:
		return null
	var encoded_size := image_file.get_length()
	var encoded := image_file.get_buffer(encoded_size) if encoded_size > 0 and encoded_size <= MAX_RETAIL_COMMAND_ICON_BYTES else PackedByteArray()
	image_file.close()
	if encoded.size() < 33 or not _has_png_signature(encoded):
		return null
	if _png_u32_be(encoded, 8) != 13 or encoded.slice(12, 16).get_string_from_ascii() != "IHDR":
		return null
	var header_width := _png_u32_be(encoded, 16)
	var header_height := _png_u32_be(encoded, 20)
	if header_width <= 0 or header_height <= 0 or header_width > MAX_RETAIL_COMMAND_ICON_DIMENSION or header_height > MAX_RETAIL_COMMAND_ICON_DIMENSION:
		return null
	var decoded := Image.new()
	if decoded.load_png_from_buffer(encoded) != OK or decoded.is_empty():
		return null
	return ImageTexture.create_from_image(decoded)


func power_display_name(power_id: String) -> String:
	## The pack-authored CONTROLBAR label for a power id (no invented names).
	for row_value in _spellbook_power_rows:
		if String((row_value as Dictionary).get("power_id", "")) == power_id:
			return String((row_value as Dictionary).get("label", ""))
	return _retail_power_title(power_id)


func refresh_powers(points: int, purchased: Array, states: Dictionary = {}) -> void:
	_last_power_states = states
	if power_orb_label != null:
		# Power-points count rides inside the star orb (white, centered).
		power_orb_label.text = "%d" % points
	if powers_palette != null and powers_palette.has_method("refresh_state"):
		var state := states if not states.is_empty() else _fallback_power_state(points, purchased)
		state["points"] = points
		powers_palette.refresh_state(state)
	_refresh_powers_dock(purchased, states)


func _fallback_power_state(points: int, purchased: Array) -> Dictionary:
	## Doc-less/fixture state: ownership shows, but without the sim's tree the
	## purchase/cast logic stays unavailable (fail closed, never invented).
	var powers: Dictionary = {}
	for row_value in _spellbook_power_rows:
		var row := row_value as Dictionary
		var power_id := String(row.get("power_id", ""))
		powers[power_id] = {
			"owned": purchased.has(power_id),
			"cost": int(row.get("cost", 0)),
			"purchase_slot": int(row.get("purchase_slot", 0)),
			"purchasable": false,
			"castable": false,
			"locked_reason": "spellbook tree unavailable",
			"cooldown": {"total_ticks": 0, "remaining_ticks": 0, "progress": 1.0},
		}
	return {"points": points, "powers": powers}


func _build_score_overlay() -> void:
	score_overlay = Control.new()
	score_overlay.name = "RetailScoreOverlay"
	score_overlay.set_anchors_preset(Control.PRESET_CENTER_TOP)
	score_overlay.position = Vector2(-240, 80)
	score_overlay.size = Vector2(480, 250)
	score_overlay.visible = false
	score_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	score_overlay.z_index = 10
	add_child(score_overlay)
	var column := VBoxContainer.new()
	column.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 28)
	score_overlay.add_child(column)
	var title := Label.new()
	title.text = "Score"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 26)
	column.add_child(title)
	for key in ["units_trained", "units_lost", "resources_gathered"]:
		var label := Label.new()
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.add_theme_font_size_override("font_size", 20)
		column.add_child(label)
		score_labels[key] = label
	set_score_values(0, 0, 0)


func _atlas_region(texture: Texture2D, region: Rect2) -> AtlasTexture:
	var atlas := AtlasTexture.new()
	atlas.atlas = texture
	atlas.region = region
	atlas.filter_clip = true
	return atlas


func _retail_ui_font(expected_pack_root: String) -> FontFile:
	var fonts_dir := expected_pack_root.path_join("assets/ui/palantir/fonts")
	var dir := DirAccess.open(fonts_dir)
	if dir == null:
		return null
	for file in dir.get_files():
		if file.get_extension() == "otf" or file.get_extension() == "ttf":
			var font := FontFile.new()
			if font.load_dynamic_font(fonts_dir.path_join(file)) == OK:
				return font
	return null


func _circle_masked(rect_control: Control) -> void:
	var shader := Shader.new()
	shader.code = """
shader_type canvas_item;
void fragment() {
	vec2 offset = UV - vec2(0.5);
	if (dot(offset, offset) > 0.25) {
		discard;
	}
}
"""
	var material := ShaderMaterial.new()
	material.shader = shader
	rect_control.material = material


func _bind_retail_bottom_left_art(content_db, expected_pack_root: String) -> void:
	# The retail frame composition draws on retail_control_bar_frame (z 1,
	# below the dock). Dropping the radar beneath it puts the ring bevel over
	# the map edge while orbs, sockets, labels, and the portrait stay on top.
	minimap.z_index = -2
	var ui_font := _retail_ui_font(expected_pack_root)
	if ui_font != null and ui_font != _retail_ui_font_cached:
		_retail_ui_font_cached = ui_font
		# Rebuild the orb with the retail font; icons are re-bound right below.
		_configure_orb_rows()
	if ui_font != null:
		for label in [resource_label, command_points_label, power_orb_label]:
			if label != null:
				(label as Label).add_theme_font_override("font", ui_font)
		if retail_tooltip != null:
			retail_tooltip.set_retail_font(ui_font)
	if retail_side_command_bar != null:
		retail_side_command_bar.bind_socket_texture(_atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION))
	for id in orb_buttons.keys():
		var orb := orb_buttons[id] as Button
		orb.icon = _atlas_region(_retail_palantir_atlas, RETAIL_ORB_REGIONS[id])
		orb.expand_icon = true
		orb.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	command_points_label.add_theme_color_override("font_color", Color("f1d06e"))
	var power_socket := StyleBoxTexture.new()
	power_socket.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
	# Bind the converted spellbook icon crops by power id (doc purchase-slot
	# order need not match the legacy icon-id list order). Icons resolve from
	# the spellbook document's own pack first — each faction's crops ship with
	# its own tree (the Men ui_manifest path stays the fallback for the host).
	for power_button in power_buttons:
		var power_id := String(power_button.get_meta("power_id", ""))
		var icon_id := _spellbook_icon_id_for(power_id)
		var doc_icon := _spellbook_icon_from_doc_pack(icon_id)
		if doc_icon != null:
			power_button.icon = doc_icon
			for state in ["normal", "hover", "pressed", "disabled"]:
				power_button.add_theme_stylebox_override(state, power_socket)
			power_button.add_theme_constant_override("icon_max_width", 76)
			continue
		var validation := _validate_retail_image(content_db, expected_pack_root, icon_id, Vector2i(64, 64))
		if String(validation.get("error", "")) == "":
			power_button.icon = validation["texture"]
			for state in ["normal", "hover", "pressed", "disabled"]:
				power_button.add_theme_stylebox_override(state, power_socket)
			power_button.add_theme_constant_override("icon_max_width", 76)
	var shell := _atlas_region(retail_apt_runtime.exact_atlas_texture(RETAIL_PALANTIR_FRAME_ATLAS), Rect2(Vector2.ZERO, RETAIL_PALANTIR_FRAME_SOURCE_SIZE))
	var score_shell := TextureRect.new()
	score_shell.name = "RetailScoreFrame"
	score_shell.texture = shell
	score_shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	score_shell.stretch_mode = TextureRect.STRETCH_SCALE
	score_shell.mouse_filter = Control.MOUSE_FILTER_IGNORE
	score_overlay.add_child(score_shell)
	score_overlay.move_child(score_shell, 0)
	command_panel.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	var old_grid_parent := command_grid.get_parent()
	if old_grid_parent != command_panel:
		old_grid_parent.remove_child(command_grid)
		command_panel.add_child(command_grid)
	command_grid.position = Vector2.ZERO
	command_grid.size = command_panel.size
	# Reparenting the grid pushed it after the socket layer; the interactive
	# socket buttons must draw above the empty-socket art in the grid.
	if command_socket_layer != null and command_socket_layer.get_parent() == command_panel:
		command_panel.move_child(command_socket_layer, command_panel.get_child_count() - 1)
	for slot in RETAIL_COMMAND_SLOT_SOURCE.size():
		var socket := TextureRect.new()
		socket.name = "RetailEmptySocket%d" % slot
		socket.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
		socket.position = RETAIL_COMMAND_SLOT_SOURCE[slot]
		socket.size = RETAIL_COMMAND_SLOT_SIZE
		socket.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		socket.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		socket.mouse_filter = Control.MOUSE_FILTER_IGNORE
		command_grid.add_child(socket)
		command_grid.move_child(socket, slot)
	# The selection portrait fills the palantir dish interior (dish center in
	# panel coordinates: dock (587, 219) minus panel origin (360, 0)), clipped
	# to the dish circle, and drawn beneath the command sockets. It must live
	# in the plain-Control command grid: the PanelContainer would force any
	# direct child to fill the whole panel.
	var old_portrait_parent := selection_portrait.get_parent()
	if old_portrait_parent != command_grid:
		old_portrait_parent.remove_child(selection_portrait)
		command_grid.add_child(selection_portrait)
	command_grid.move_child(selection_portrait, 0)
	selection_portrait.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	selection_portrait.custom_minimum_size = Vector2.ZERO
	var dish_panel_center := RETAIL_DISH_CENTER - Vector2(360, 0)
	selection_portrait.position = dish_panel_center - Vector2(RETAIL_DISH_RADIUS, RETAIL_DISH_RADIUS)
	selection_portrait.size = Vector2(RETAIL_DISH_RADIUS, RETAIL_DISH_RADIUS) * 2.0
	selection_portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	selection_portrait.stretch_mode = TextureRect.STRETCH_SCALE
	_circle_masked(selection_portrait)
	# Retail-style production queue chips under the palantir dish.
	_ensure_production_queue_chips()
	resource_strip.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	var resource_row := resource_label.get_parent()
	resource_row.remove_child(resource_label)
	resource_row.remove_child(command_points_label)
	# Retail resource strip: gold pile glyph + amount on the left, command
	# points on the right, both gold Albertus over the frame art recess.
	var resource_glyph := resource_strip.get_node_or_null("ResourceGlyph") as Label
	if resource_glyph == null:
		resource_glyph = Label.new()
		resource_glyph.name = "ResourceGlyph"
		resource_glyph.text = "◆"
		resource_glyph.add_theme_color_override("font_color", Color("d6aa55"))
		resource_glyph.add_theme_font_size_override("font_size", 15)
		resource_glyph.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		resource_glyph.mouse_filter = Control.MOUSE_FILTER_IGNORE
		if _retail_ui_font_cached != null:
			resource_glyph.add_theme_font_override("font", _retail_ui_font_cached)
		resource_strip.add_child(resource_glyph)
	resource_glyph.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	resource_glyph.position = Vector2(22, 0)
	resource_glyph.size = Vector2(22, 36)
	resource_strip.add_child(resource_label)
	resource_strip.add_child(command_points_label)
	resource_label.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	resource_label.position = Vector2(44, 0)
	resource_label.size = Vector2(120, 36)
	resource_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	resource_label.add_theme_font_size_override("font_size", 20)
	command_points_label.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	command_points_label.position = Vector2(150, 0)
	command_points_label.size = Vector2(136, 36)
	command_points_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	command_points_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	command_points_label.add_theme_font_size_override("font_size", 20)
	resource_row.visible = false
	for child in command_panel.find_children("*", "Label", true, false):
		(child as Label).visible = false
	selection_portrait.visible = false
	for button_value in train_buttons.values():
		_make_retail_icon_only(button_value as Button)
		_wire_button_feel(button_value as Button)
	for button_value in unit_action_buttons.values():
		_make_retail_icon_only(button_value as Button)
		_wire_button_feel(button_value as Button)
	for orb_value in orb_buttons.values():
		_wire_button_feel(orb_value as Button)
	for power_button in power_buttons:
		_wire_button_feel(power_button)
	for queue_button in production_queue_buttons:
		_wire_button_feel(queue_button)


func _make_retail_icon_only(button: Button) -> void:
	button.text = ""
	# The socket art is the button's own background, so icon and socket can
	# never drift apart; icons scale smoothly instead of pixelating.
	var socket_box := StyleBoxTexture.new()
	socket_box.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
	for state in ["normal", "hover", "pressed", "disabled", "focus"]:
		button.add_theme_stylebox_override(state, socket_box)
	button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	button.expand_icon = true
	button.add_theme_constant_override("icon_max_width", int(RETAIL_COMMAND_SLOT_SIZE.x) - 10)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.mouse_entered.connect(func() -> void:
		ui_sound_requested.emit("Gui_UpgradeButtonGlow")
	)
	button.pressed.connect(func() -> void:
		ui_sound_requested.emit("Gui_PalantirCommandButtonClick")
	)


func powers_palette_open() -> bool:
	return powers_palette != null and powers_palette.visible


func close_powers_palette() -> bool:
	## Returns true when a visible spellbook was closed (ESC cancel path).
	if powers_palette == null or not powers_palette.visible:
		return false
	_set_powers_palette_visible(false)
	return true


func _toggle_powers_palette() -> void:
	_set_powers_palette_visible(not powers_palette.visible)


func _set_powers_palette_visible(open: bool) -> void:
	var was_open := powers_palette.visible
	powers_palette.visible = open
	score_overlay.visible = false
	# The spellbook is its own screen; the control-bar cluster hides while
	# it is open so the palantir frame does not bleed through the backdrop.
	var cluster_visible := not open
	if minimap != null and minimap.get_parent() != null:
		(minimap.get_parent() as CanvasItem).visible = cluster_visible
	if retail_control_bar_frame != null:
		retail_control_bar_frame.visible = cluster_visible and retail_control_bar_bound
	if command_panel != null:
		command_panel.visible = cluster_visible
	if retail_side_command_bar != null and not cluster_visible:
		retail_side_command_bar.visible = false
	if powers_dock != null:
		powers_dock.visible = cluster_visible
	if open:
		powers_opened.emit()
	elif was_open:
		# Every close path is the retail ACCEPT: the session's picks commit.
		powers_closed.emit()
	ui_sound_requested.emit("Gui_PalantirChoosePowerClick" if open else "Gui_CloseSpellStoreClick")


func _toggle_score_overlay() -> void:
	score_overlay.visible = not score_overlay.visible
	powers_palette.visible = false
	ui_sound_requested.emit("Gui_PalantirButtonClick")


func _emit_train_requested(unit_id: String) -> void:
	train_requested.emit(unit_id)


func _emit_construct_requested(structure_kind: String) -> void:
	construct_requested.emit(structure_kind)


func _emit_cancel_production_requested() -> void:
	if cancel_production_button == null or cancel_production_button.disabled:
		return
	cancel_production_requested.emit(int(cancel_production_button.get_meta("queue_index", 0)))


func _build_control_groups() -> void:
	var strip := PanelContainer.new()
	strip.name = "ControlGroupStrip"
	strip.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	strip.offset_left = -344
	strip.offset_top = -82
	strip.offset_right = 344
	strip.offset_bottom = -16
	strip.add_theme_stylebox_override("panel", _panel)
	strip.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(strip)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 5)
	strip.add_child(row)
	for group in range(1, 10):
		var button := Button.new()
		button.name = "Group%d" % group
		button.text = "%d\n-" % group
		button.custom_minimum_size = Vector2(66, 46)
		_style_button(button)
		button.gui_input.connect(_on_group_button_input.bind(group))
		row.add_child(button)
		group_buttons[group] = button


func _on_group_button_input(event: InputEvent, group: int) -> void:
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		if (event as InputEventMouseButton).ctrl_pressed:
			group_assign_requested.emit(group)
		else:
			group_recall_requested.emit(group)
		accept_event()


func _build_feedback() -> void:
	var panel := PanelContainer.new()
	panel.name = "FeedbackPanel"
	# Compact corner box tucked above the command dock — not a mid-screen
	# banner that eats half the battlefield view.
	panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	panel.offset_left = -510
	panel.offset_top = -150
	panel.offset_right = -18
	panel.offset_bottom = -92
	panel.z_index = 12
	panel.add_theme_stylebox_override("panel", _panel)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(panel)
	feedback_label = Label.new()
	feedback_label.name = "Feedback"
	feedback_label.text = "Select a battalion or a producer."
	feedback_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	feedback_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	feedback_label.clip_text = true
	feedback_label.add_theme_color_override("font_color", Color("f2e6b8"))
	feedback_label.add_theme_font_size_override("font_size", 14)
	panel.add_child(feedback_label)


func _build_event_feed() -> void:
	event_feed = VBoxContainer.new()
	event_feed.name = "RetailEventFeed"
	event_feed.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	event_feed.offset_left = -620
	event_feed.offset_top = 10
	event_feed.offset_right = -14
	event_feed.offset_bottom = 400
	event_feed.alignment = BoxContainer.ALIGNMENT_BEGIN
	event_feed.add_theme_constant_override("separation", 0)
	event_feed.mouse_filter = Control.MOUSE_FILTER_IGNORE
	event_feed.z_index = 12
	add_child(event_feed)


## One retail event-feed line. Alternating gold/white mirrors the retail
## queue's per-entry color alternation; each line fades out on its own timer.
func push_event_feed(text: String) -> void:
	if event_feed == null or text.strip_edges() == "":
		return
	while event_feed.get_child_count() >= EVENT_FEED_MAX_LINES:
		var oldest := event_feed.get_child(0)
		event_feed.remove_child(oldest)
		oldest.queue_free()
	var label := Label.new()
	label.name = "Event%d" % (int(Time.get_ticks_msec()) % 100000)
	label.text = text
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	label.add_theme_font_size_override("font_size", 21)
	label.add_theme_color_override("font_color", EVENT_FEED_GOLD if _event_feed_gold_next else EVENT_FEED_WHITE)
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
	label.add_theme_constant_override("shadow_offset_x", 2)
	label.add_theme_constant_override("shadow_offset_y", 2)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if _retail_ui_font_cached != null:
		label.add_theme_font_override("font", _retail_ui_font_cached)
	_event_feed_gold_next = not _event_feed_gold_next
	event_feed.add_child(label)
	if is_inside_tree():
		# The tween is bound to the LABEL, not the HUD: when a line is evicted
		# early by the EVENT_FEED_MAX_LINES trim above, the tween dies with it.
		# A HUD-bound tween would outlive the freed label and (with a lambda
		# capturing it) raise "Lambda capture at index 0 was freed" every time a
		# line is evicted -- which is constantly during a busy battle. The
		# callback is a method Callable on the label for the same reason: Tween
		# validity-checks a Callable's object, but never a lambda's captures.
		var tween := label.create_tween()
		tween.tween_interval(EVENT_FEED_SECONDS)
		tween.tween_property(label, "modulate:a", 0.0, 1.2)
		tween.tween_callback(label.queue_free)


func event_feed_lines() -> Array[String]:
	var lines: Array[String] = []
	if event_feed == null:
		return lines
	for child in event_feed.get_children():
		if child is Label:
			lines.append((child as Label).text)
	return lines


func _build_construction_progress_layer() -> void:
	_construction_layer = Control.new()
	_construction_layer.name = "ConstructionProgressLayer"
	_construction_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_construction_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_construction_layer.z_index = 9
	add_child(_construction_layer)


## entries: [{"position": Vector2 (screen), "percent": int, "seconds_left": int}].
## Labels are reused across frames; extras hide.
func sync_construction_progress(entries: Array) -> void:
	if _construction_layer == null:
		return
	while _construction_labels.size() < entries.size():
		var label := Label.new()
		label.name = "ConstructionProgress%d" % _construction_labels.size()
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.add_theme_font_size_override("font_size", 15)
		label.add_theme_color_override("font_color", Color("f2e9c8"))
		label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
		label.add_theme_constant_override("shadow_offset_x", 1)
		label.add_theme_constant_override("shadow_offset_y", 1)
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		if _retail_ui_font_cached != null:
			label.add_theme_font_override("font", _retail_ui_font_cached)
		_construction_layer.add_child(label)
		_construction_labels.append(label)
	for index in _construction_labels.size():
		var label := _construction_labels[index]
		if index >= entries.size():
			label.visible = false
			continue
		var entry: Dictionary = entries[index]
		label.visible = true
		label.text = "Building: %d%% • %ds left" % [int(entry.get("percent", 0)), int(entry.get("seconds_left", 0))]
		label.position = Vector2(entry.get("position", Vector2.ZERO)) - Vector2(80, 12)
		label.size = Vector2(160, 24)


func _build_dish_level_caption() -> void:
	## Lives in the command panel's grid (dish-local coordinates) so it renders
	## above the selection portrait but below the command sockets.
	_dish_level_label = Label.new()
	_dish_level_label.name = "DishLevelCaption"
	_dish_level_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_dish_level_label.add_theme_font_size_override("font_size", 15)
	_dish_level_label.add_theme_color_override("font_color", Color("f0e6c8"))
	_dish_level_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	_dish_level_label.add_theme_constant_override("shadow_offset_x", 1)
	_dish_level_label.add_theme_constant_override("shadow_offset_y", 1)
	_dish_level_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_dish_level_label.visible = false
	command_grid.add_child(_dish_level_label)
	_dish_level_bar = ProgressBar.new()
	_dish_level_bar.name = "DishLevelBar"
	_dish_level_bar.min_value = 0.0
	_dish_level_bar.max_value = 1.0
	_dish_level_bar.step = 0.001
	_dish_level_bar.value = 0.0
	_dish_level_bar.show_percentage = false
	_dish_level_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var bar_back := StyleBoxFlat.new()
	bar_back.bg_color = Color(0.05, 0.04, 0.03, 0.9)
	bar_back.border_color = Color("8a7648")
	bar_back.set_border_width_all(1)
	_dish_level_bar.add_theme_stylebox_override("background", bar_back)
	var bar_fill := StyleBoxFlat.new()
	bar_fill.bg_color = Color("c9a83c")
	_dish_level_bar.add_theme_stylebox_override("fill", bar_fill)
	_dish_level_bar.visible = false
	command_grid.add_child(_dish_level_bar)
	_layout_dish_level_caption()


func _layout_dish_level_caption() -> void:
	if _dish_level_label == null or command_panel == null:
		return
	# Dish center in command-panel coordinates: dock (587, 219) minus the panel
	# origin (360, 0). The caption rides the dish's lower interior like retail.
	var dish_panel_center := RETAIL_DISH_CENTER - Vector2(360, 0)
	_dish_level_label.position = dish_panel_center + Vector2(-60, 42)
	_dish_level_label.size = Vector2(120, 20)
	_dish_level_bar.position = dish_panel_center + Vector2(-46, 63)
	_dish_level_bar.size = Vector2(92, 7)


## caption: e.g. "Level: 1"; progress 0..1 (retail level-XP bar; 0 when the
## sim has no XP source). Visible only while a structure or hero is selected.
func set_dish_level(caption: String, progress: float) -> void:
	if _dish_level_label == null:
		return
	# The caption rides the retail dish interior; only parity mode binds that
	# geometry, so the public surface keeps it hidden.
	var show := caption != "" and retail_presentation_bound
	_dish_level_label.visible = show
	_dish_level_bar.visible = show
	if show:
		_dish_level_label.text = caption
		_dish_level_bar.value = clampf(progress, 0.0, 1.0)


func _build_hero_bar() -> void:
	hero_bar = HBoxContainer.new()
	hero_bar.name = "RetailHeroBar"
	hero_bar.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	hero_bar.offset_left = -220
	hero_bar.offset_top = -86
	hero_bar.offset_right = 220
	hero_bar.offset_bottom = -6
	hero_bar.alignment = BoxContainer.ALIGNMENT_CENTER
	hero_bar.add_theme_constant_override("separation", 10)
	hero_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	hero_bar.z_index = 5
	add_child(hero_bar)


## heroes: [{"id": int, "unit_type": String, "name": String, "level": int,
## "health": int, "maximum_health": int, "selected": bool}]. Rebuilds only on
## roster change; health/selection refresh in place each call.
func sync_hero_bar(heroes: Array) -> void:
	if hero_bar == null:
		return
	var alive: Dictionary = {}
	for hero_value in heroes:
		var hero: Dictionary = hero_value
		var hero_id := int(hero.get("id", 0))
		alive[hero_id] = true
		var button: Button = _hero_bar_buttons.get(hero_id)
		if button == null:
			button = Button.new()
			button.name = "HeroBar%d" % hero_id
			button.custom_minimum_size = Vector2(58, 72)
			button.size = Vector2(58, 72)
			for state in ["normal", "hover", "pressed", "disabled", "focus"]:
				button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
			button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
			var portrait := TextureRect.new()
			portrait.name = "Portrait"
			portrait.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			portrait.offset_bottom = -14
			portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			portrait.stretch_mode = TextureRect.STRETCH_SCALE
			portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
			_circle_masked(portrait)
			button.add_child(portrait)
			var badge := Label.new()
			badge.name = "LevelBadge"
			badge.position = Vector2(-2, 34)
			badge.size = Vector2(20, 20)
			badge.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			badge.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			badge.add_theme_font_size_override("font_size", 13)
			badge.add_theme_color_override("font_color", Color("f5e9c8"))
			badge.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
			badge.add_theme_constant_override("shadow_offset_x", 1)
			badge.add_theme_constant_override("shadow_offset_y", 1)
			badge.mouse_filter = Control.MOUSE_FILTER_IGNORE
			button.add_child(badge)
			var health := ProgressBar.new()
			health.name = "HealthBar"
			health.position = Vector2(0, 58)
			health.size = Vector2(58, 7)
			health.min_value = 0.0
			health.max_value = 1.0
			health.step = 0.001
			health.show_percentage = false
			health.mouse_filter = Control.MOUSE_FILTER_IGNORE
			var back := StyleBoxFlat.new()
			back.bg_color = Color(0.04, 0.05, 0.03, 0.9)
			health.add_theme_stylebox_override("background", back)
			var fill := StyleBoxFlat.new()
			fill.bg_color = Color("3fae3f")
			health.add_theme_stylebox_override("fill", fill)
			button.add_child(health)
			button.pressed.connect(func() -> void: hero_recall_requested.emit(hero_id))
			hero_bar.add_child(button)
			_hero_bar_buttons[hero_id] = button
		var portrait_rect := button.get_node("Portrait") as TextureRect
		portrait_rect.texture = _retail_portrait_textures.get(String(hero.get("unit_type", ""))) as Texture2D
		(button.get_node("LevelBadge") as Label).text = "%d" % int(hero.get("level", 1))
		var health_bar := button.get_node("HealthBar") as ProgressBar
		health_bar.value = clampf(float(hero.get("health", 0)) / float(maxi(1, int(hero.get("maximum_health", 1)))), 0.0, 1.0)
		# Retail highlights the currently-selected hero's portrait ring gold.
		button.self_modulate = Color(1.35, 1.22, 0.75) if bool(hero.get("selected", false)) else Color.WHITE
		# REF-41 hero hover: retail tooltip panel, not a native tooltip.
		button.tooltip_text = ""
		button.set_meta("tooltip_group", "hero_bar")
		button.set_meta("hero_name", String(hero.get("name", "Hero")))
		button.set_meta("hero_level", int(hero.get("level", 1)))
		button.set_meta("hero_health", int(hero.get("health", 0)))
		_register_button_tooltip(button)
	for existing_id in _hero_bar_buttons.keys().duplicate():
		if not alive.has(int(existing_id)):
			(_hero_bar_buttons[existing_id] as Button).queue_free()
			_hero_bar_buttons.erase(existing_id)
	hero_bar.visible = not heroes.is_empty()


func _build_radial_layer() -> void:
	_radial_layer = Control.new()
	_radial_layer.name = "RetailRadialCommands"
	_radial_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_radial_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_radial_layer.z_index = 7
	add_child(_radial_layer)


## entries: [{"command_kind": "train"|"hero"|"upgrade", "id": String,
## "icon": Texture2D, "enabled": bool, "label": String, "tooltip": String}].
## anchor: screen position the arc floats over (the selected producer).
func sync_radial_commands(anchor: Vector2, entries: Array) -> void:
	if _radial_layer == null:
		return
	var fingerprint := ""
	for entry_value in entries:
		var entry: Dictionary = entry_value
		fingerprint += "%s:%s:%s:%s:%s;" % [
			String(entry.get("command_kind", "")), String(entry.get("id", "")),
			str(bool(entry.get("enabled", false))), str(entry.get("icon") != null),
			String(entry.get("text", ""))
		]
	if fingerprint != _radial_fingerprint:
		_radial_fingerprint = fingerprint
		for button in _radial_buttons:
			button.queue_free()
		_radial_buttons.clear()
		for entry_value in entries:
			var entry: Dictionary = entry_value
			var button := Button.new()
			button.name = "Radial_%s_%s" % [String(entry.get("command_kind", "")), String(entry.get("id", "")).get_slice(".", -1)]
			button.custom_minimum_size = Vector2(64, 64)
			button.size = Vector2(64, 64)
			button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
			button.icon = entry.get("icon") as Texture2D
			button.expand_icon = true
			button.add_theme_constant_override("icon_max_width", 48)
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
			if button.icon == null and String(entry.get("text", "")) != "":
				# Iconless but doc-honest entries (unconverted art/strings):
				# small text in the socket, same discipline as the side bar.
				button.text = String(entry.get("text", ""))
				button.add_theme_font_size_override("font_size", 10)
				button.add_theme_color_override("font_color", Color("e6d9ae"))
				button.add_theme_color_override("font_hover_color", Color("fff3c8"))
				button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
			if _retail_palantir_atlas != null:
				var socket_box := StyleBoxTexture.new()
				socket_box.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
				for state in ["normal", "hover", "pressed", "disabled", "focus"]:
					button.add_theme_stylebox_override(state, socket_box)
			else:
				for state in ["normal", "hover", "pressed", "disabled", "focus"]:
					button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
			button.tooltip_text = String(entry.get("tooltip", ""))
			var command_kind := String(entry.get("command_kind", ""))
			var command_id := String(entry.get("id", ""))
			button.set_meta("tooltip_group", "expansion" if command_kind == "expansion" else "train")
			button.set_meta("tooltip_unit_id", command_id)
			button.set_meta("tooltip_fallback_label", String(entry.get("label", "")))
			button.set_meta("tooltip_fallback_desc", String(entry.get("tooltip", "")))
			if command_kind == "upgrade":
				button.pressed.connect(func() -> void: structure_upgrade_requested.emit(command_id))
			elif command_kind == "expansion":
				button.pressed.connect(func() -> void: expansion_requested.emit(command_id))
			else:
				button.pressed.connect(_emit_train_requested.bind(command_id))
			_register_button_tooltip(button)
			_wire_button_feel(button)
			_radial_layer.add_child(button)
			_radial_buttons.append(button)
	var count := _radial_buttons.size()
	# Arc radius grows with the roster so full fortress command sets (heroes +
	# porter + 4 expansion commands) do not overlap (REF-33/35).
	var radius := maxf(118.0, float(count) * 22.0)
	for index in count:
		var button := _radial_buttons[index]
		var entry: Dictionary = entries[index]
		button.visible = true
		button.disabled = not bool(entry.get("enabled", false))
		button.modulate.a = 1.0 if bool(entry.get("enabled", false)) else 0.45
		# Fan across the upper half-circle above the anchor (REF-25 four
		# buttons arc over the barracks door).
		var t := 0.5 if count == 1 else float(index) / float(count - 1)
		var angle := PI + PI * t
		button.position = anchor + Vector2(cos(angle), sin(angle)) * radius - button.size * 0.5
		# Live training dial + countdown on the radial menu's training icons
		# (owner: the queue-chip CCW sweep, everywhere a unit trains). Updated
		# here in the per-frame layout pass so the buttons are not rebuilt
		# every tick; entries without an active queue row hide any stale dial.
		var queue_row: Dictionary = entry.get("queue_row", {}) as Dictionary
		if not queue_row.is_empty():
			_sync_queue_button_dial(button, queue_row)
		else:
			var stale_dial := button.get_node_or_null("TrainingDial") as TextureProgressBar
			if stale_dial != null:
				stale_dial.visible = false
			var stale_countdown := button.get_node_or_null("TrainingCountdown") as Label
			if stale_countdown != null:
				stale_countdown.visible = false
	_radial_layer.visible = count > 0


func hide_radial_commands() -> void:
	if _radial_layer != null:
		_radial_layer.visible = false


## Validated expansion command presentation for the fortress radial; empty
## when unbound or the pack lacks the command (fail closed).
func retail_expansion_command(kind: String) -> Dictionary:
	return (_retail_expansion_validated.get(kind, {}) as Dictionary).duplicate()


func radial_command_count() -> int:
	return _radial_buttons.size() if _radial_layer != null and _radial_layer.visible else 0


func _build_diagnostics() -> void:
	diagnostics_panel = PanelContainer.new()
	diagnostics_panel.name = "DiagnosticsPanel"
	diagnostics_panel.position = Vector2(16, 16)
	diagnostics_panel.size = Vector2(560, 120)
	diagnostics_panel.add_theme_stylebox_override("panel", _panel)
	diagnostics_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	diagnostics_panel.visible = false
	add_child(diagnostics_panel)
	diagnostics_label = Label.new()
	diagnostics_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	diagnostics_label.add_theme_font_size_override("font_size", 13)
	diagnostics_label.add_theme_color_override("font_color", Color("a9c8d7"))
	diagnostics_panel.add_child(diagnostics_label)


func _build_pause_panel() -> void:
	pause_panel = PanelContainer.new()
	pause_panel.name = "PausePanel"
	pause_panel.set_anchors_preset(Control.PRESET_CENTER)
	pause_panel.offset_left = -245
	pause_panel.offset_top = -260
	pause_panel.offset_right = 245
	pause_panel.offset_bottom = 260
	pause_panel.add_theme_stylebox_override("panel", _panel)
	pause_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	pause_panel.visible = false
	add_child(pause_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	pause_panel.add_child(column)
	var heading := Label.new()
	heading.text = "BATTLE PAUSED"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_font_size_override("font_size", 28)
	heading.add_theme_color_override("font_color", Color("d9c996"))
	column.add_child(heading)
	music_slider = _add_slider(column, "Music", func(value: float) -> void: music_volume_changed.emit(value))
	voice_slider = _add_slider(column, "Voice / Sound FX", func(value: float) -> void: voice_volume_changed.emit(value))
	mute_toggle = CheckButton.new()
	mute_toggle.text = "Mute all audio"
	mute_toggle.toggled.connect(func(value: bool) -> void: mute_changed.emit(value))
	column.add_child(mute_toggle)
	# Dev/debug console: match clock, FPS overlay, command-cap slider, weak
	# fortresses, and the resource cheat are development tools, not retail
	# chrome. They build only when OPENBFME_DEV_HUD=1 (or DEV_HUD_DEFAULT is
	# flipped for a dev build); the retail pause screen stays clean.
	if dev_hud_enabled():
		_build_dev_console(column)
		# Playtest Tools arrived from the audit lane OUTSIDE this guard, i.e. on
		# the pause screen of every build. Its entries (grant resources, grant
		# power points, force max level, heal) write hashed simulation state
		# locally and never travel through the lockstep command stream, so in a
		# multiplayer match one press desyncs every peer. It belongs behind the
		# same OPENBFME_DEV_HUD gate as the console it sits next to.
		_add_action_button(column, "Playtest Tools", func() -> void: show_playtest(true))
	_add_action_button(column, "Resume", func() -> void: pause_requested.emit())
	_add_action_button(column, "Restart Battle", func() -> void: restart_requested.emit())
	_add_action_button(column, "Return to Main Menu", func() -> void: main_menu_requested.emit())
	_add_action_button(column, "Quit", func() -> void: quit_requested.emit())


func _build_playtest_panel() -> void:
	## Dev-only playtest surface (pause → Playtest Tools, itself behind
	## dev_hud_enabled()). It sits on top of the pause panel and closes back to
	## it, so nothing here can be reached mid-battle by accident.
	playtest_panel = PanelContainer.new()
	playtest_panel.name = "PlaytestPanel"
	playtest_panel.set_anchors_preset(Control.PRESET_CENTER)
	playtest_panel.offset_left = -260
	playtest_panel.offset_top = -300
	playtest_panel.offset_right = 260
	playtest_panel.offset_bottom = 300
	playtest_panel.add_theme_stylebox_override("panel", _panel)
	playtest_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	playtest_panel.visible = false
	playtest_panel.z_index = 20
	add_child(playtest_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	playtest_panel.add_child(column)
	var heading := Label.new()
	heading.text = "PLAYTEST TOOLS"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_font_size_override("font_size", 26)
	heading.add_theme_color_override("font_color", Color("d9c996"))
	column.add_child(heading)
	var note := Label.new()
	note.text = "Development aids. These bypass the retail economy and do not\nproduce evidence usable for a parity gate."
	note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	note.add_theme_font_size_override("font_size", 12)
	note.add_theme_color_override("font_color", Color("8fa3ad"))
	column.add_child(note)

	_add_section_label(column, "Economy")
	_add_action_button(column, "Resources → 999,999", func() -> void:
		playtest_resources_requested.emit(999999)
	)
	_add_action_button(column, "Resources +50,000 (F7)", func() -> void:
		cheat_resources_requested.emit()
	)
	playtest_command_cap_label = Label.new()
	playtest_command_cap_label.name = "PlaytestCommandCapLabel"
	playtest_command_cap_label.text = "Command point cap: 200"
	playtest_command_cap_label.add_theme_color_override("font_color", Color("c8dbe4"))
	column.add_child(playtest_command_cap_label)
	playtest_command_cap_slider = HSlider.new()
	playtest_command_cap_slider.name = "PlaytestCommandCapSlider"
	playtest_command_cap_slider.min_value = 100
	playtest_command_cap_slider.max_value = 5000
	playtest_command_cap_slider.step = 50
	playtest_command_cap_slider.value = 200
	playtest_command_cap_slider.value_changed.connect(func(value: float) -> void:
		playtest_command_cap_label.text = "Command point cap: %d" % int(value)
		command_cap_changed.emit(int(value))
	)
	column.add_child(playtest_command_cap_slider)

	_add_section_label(column, "Spellbook")
	_add_action_button(column, "Power points +10", func() -> void:
		playtest_power_points_requested.emit(10)
	)
	_add_action_button(column, "Power points +100 (buy the whole tree)", func() -> void:
		playtest_power_points_requested.emit(100)
	)

	_add_section_label(column, "Units")
	_add_action_button(column, "Max level: selected", func() -> void:
		playtest_max_level_requested.emit("selected")
	)
	_add_action_button(column, "Max level: all my units", func() -> void:
		playtest_max_level_requested.emit("all")
	)
	_add_action_button(column, "Full health: selected", func() -> void:
		playtest_heal_requested.emit()
	)

	_add_action_button(column, "Back", func() -> void: show_playtest(false))


func _add_section_label(parent: VBoxContainer, text: String) -> void:
	var label := Label.new()
	label.text = text.to_upper()
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color("8fa3ad"))
	parent.add_child(label)


func show_playtest(value: bool) -> void:
	# Second gate, deliberately redundant with the pause-screen entry: the panel
	# writes unreplicated simulation state, so it must be impossible to open in
	# a build that did not opt into the dev surface, whatever calls this.
	if playtest_panel == null or not dev_hud_enabled():
		return
	playtest_panel.visible = value
	# The pause panel stays built but hidden underneath, so "Back" returns to
	# it rather than dropping the player into an unpaused battle.
	if pause_panel != null:
		pause_panel.visible = not value


## Keeps the playtest command-cap slider in step with the live cap (the dev
## console owns the same signal, and the slice can change the cap on load).
func set_playtest_command_cap(value: int) -> void:
	if playtest_command_cap_slider == null:
		return
	playtest_command_cap_slider.set_value_no_signal(float(value))
	if playtest_command_cap_label != null:
		playtest_command_cap_label.text = "Command point cap: %d" % value


## Dev-console gate: env flag (per-run) or constant (dev builds). Default OFF.
static func dev_hud_enabled() -> bool:
	return DEV_HUD_DEFAULT or OS.get_environment("OPENBFME_DEV_HUD") == "1"


func _build_dev_console(column: VBoxContainer) -> void:
	var dev_heading := Label.new()
	dev_heading.text = "DEV CONSOLE (OPENBFME_DEV_HUD)"
	dev_heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	dev_heading.add_theme_font_size_override("font_size", 13)
	dev_heading.add_theme_color_override("font_color", Color("8fa3ad"))
	column.add_child(dev_heading)
	match_clock_label = Label.new()
	match_clock_label.name = "MatchClock"
	match_clock_label.text = "Game Time  00:00"
	match_clock_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	match_clock_label.add_theme_font_size_override("font_size", 18)
	match_clock_label.add_theme_color_override("font_color", Color("c8dbe4"))
	column.add_child(match_clock_label)
	fps_toggle = CheckButton.new()
	fps_toggle.name = "FpsToggle"
	fps_toggle.text = "Show FPS / frametime"
	fps_toggle.toggled.connect(set_fps_overlay_visible)
	column.add_child(fps_toggle)
	var cap_label := Label.new()
	cap_label.name = "CommandCapLabel"
	cap_label.text = "Command point cap: 200"
	cap_label.add_theme_color_override("font_color", Color("c8dbe4"))
	column.add_child(cap_label)
	command_cap_slider = HSlider.new()
	command_cap_slider.name = "CommandCapSlider"
	command_cap_slider.min_value = 100
	command_cap_slider.max_value = 600
	command_cap_slider.step = 20
	command_cap_slider.value = 200
	command_cap_slider.value_changed.connect(func(value: float) -> void:
		cap_label.text = "Command point cap: %d" % int(value)
		command_cap_changed.emit(int(value))
	)
	column.add_child(command_cap_slider)
	weak_fortress_toggle = CheckButton.new()
	weak_fortress_toggle.name = "WeakFortressToggle"
	weak_fortress_toggle.text = "Testing: weak fortresses (1500 HP)"
	weak_fortress_toggle.toggled.connect(func(value: bool) -> void: weak_fortress_toggled.emit(value))
	column.add_child(weak_fortress_toggle)
	_add_action_button(column, "Testing: +50,000 resources (F7)", func() -> void: cheat_resources_requested.emit())
	_add_action_button(column, "Testing: finish builds/queues/cooldowns (F6)", func() -> void: cheat_finish_work_requested.emit())
	_add_action_button(column, "Testing: level up selected units (F4)", func() -> void: cheat_level_up_requested.emit())


func set_match_clock_seconds(seconds: float) -> void:
	if match_clock_label == null:
		return
	var total := maxi(0, int(seconds))
	match_clock_label.text = "Game Time  %02d:%02d" % [total / 60, total % 60]


func set_fps_overlay_visible(value: bool) -> void:
	if fps_overlay == null:
		fps_overlay = Label.new()
		fps_overlay.name = "FpsOverlay"
		fps_overlay.set_anchors_preset(Control.PRESET_TOP_RIGHT)
		fps_overlay.offset_left = -240
		fps_overlay.offset_top = 8
		fps_overlay.offset_right = -10
		fps_overlay.offset_bottom = 70
		fps_overlay.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		fps_overlay.add_theme_font_size_override("font_size", 15)
		fps_overlay.add_theme_color_override("font_color", Color("d9e6ec"))
		fps_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
		fps_overlay.z_index = 30
		# The overlay must keep updating while the game is paused.
		fps_overlay.process_mode = Node.PROCESS_MODE_ALWAYS
		add_child(fps_overlay)
	fps_overlay.visible = value
	if fps_toggle != null and fps_toggle.button_pressed != value:
		fps_toggle.set_pressed_no_signal(value)
	set_process(value or is_processing())


var input_debug_label: Label


func set_input_debug_visible(value: bool) -> void:
	# Live input inspector (F8): shows which Control the mouse is actually
	# over, so "dead button" reports can name the click-swallower directly.
	if input_debug_label == null:
		input_debug_label = Label.new()
		input_debug_label.name = "InputDebug"
		input_debug_label.set_anchors_preset(Control.PRESET_TOP_RIGHT)
		input_debug_label.offset_left = -520
		input_debug_label.offset_top = 80
		input_debug_label.offset_right = -10
		input_debug_label.offset_bottom = 130
		input_debug_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		input_debug_label.add_theme_font_size_override("font_size", 14)
		input_debug_label.add_theme_color_override("font_color", Color("ffd27a"))
		input_debug_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		input_debug_label.z_index = 30
		input_debug_label.process_mode = Node.PROCESS_MODE_ALWAYS
		add_child(input_debug_label)
	input_debug_label.visible = value
	set_process(true)


func _process(delta: float) -> void:
	if input_debug_label != null and input_debug_label.visible:
		var hovered := get_viewport().gui_get_hovered_control()
		input_debug_label.text = "mouse %s\nhovered: %s" % [
			get_viewport().get_mouse_position(),
			str(hovered.get_path()) if hovered != null else "<world>",
		]
	if fps_overlay == null or not fps_overlay.visible:
		return
	_frame_times.append(delta * 1000.0)
	if _frame_times.size() > 120:
		_frame_times = _frame_times.slice(_frame_times.size() - 120)
	var worst := 0.0
	var total := 0.0
	for value in _frame_times:
		total += value
		worst = maxf(worst, value)
	var average := total / maxf(1.0, float(_frame_times.size()))
	fps_overlay.text = "FPS %d\nframe %.2f ms avg\nworst %.2f ms" % [
		Engine.get_frames_per_second(), average, worst
	]


func flash_command_points() -> void:
	# Retail flashes the command-point counter when a train order is rejected
	# at the cap; mirror that so queue refusals are impossible to miss.
	if command_points_label == null:
		return
	var tween := create_tween()
	for _cycle in 3:
		tween.tween_property(command_points_label, "modulate", Color(1.0, 0.25, 0.2), 0.12)
		tween.tween_property(command_points_label, "modulate", Color.WHITE, 0.12)


func _add_slider(parent: VBoxContainer, title: String, callback: Callable) -> HSlider:
	var label := Label.new()
	label.text = title
	label.add_theme_color_override("font_color", Color("c8dbe4"))
	parent.add_child(label)
	var slider := HSlider.new()
	slider.min_value = 0.0
	slider.max_value = 1.0
	slider.step = 0.01
	slider.value = 0.8
	slider.custom_minimum_size.y = 28
	slider.value_changed.connect(callback)
	parent.add_child(slider)
	return slider


func _menu_glass_box(background: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = border
	style.set_border_width_all(1)
	style.set_corner_radius_all(3)
	style.content_margin_left = 14.0
	style.content_margin_right = 14.0
	style.content_margin_top = 6.0
	style.content_margin_bottom = 6.0
	return style


func _add_action_button(parent: VBoxContainer, text: String, callback: Callable) -> void:
	# Retail menu buttons are green glass with a gold rim and pale-gold text;
	# hybrid equivalent from the retail palette rather than APT execution.
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 42
	button.add_theme_stylebox_override("normal", _menu_glass_box(Color(0.086, 0.184, 0.118), Color(0.42, 0.5, 0.3)))
	button.add_theme_stylebox_override("hover", _menu_glass_box(Color(0.13, 0.27, 0.16), Color(0.72, 0.66, 0.38)))
	button.add_theme_stylebox_override("pressed", _menu_glass_box(Color(0.055, 0.12, 0.08), Color(0.72, 0.66, 0.38)))
	button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	button.add_theme_color_override("font_color", Color(0.85, 0.9, 0.78))
	button.add_theme_color_override("font_hover_color", Color(0.96, 0.9, 0.62))
	button.add_theme_color_override("font_pressed_color", Color(0.7, 0.72, 0.58))
	button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	button.pressed.connect(callback)
	parent.add_child(button)


func _build_outcome_layer() -> void:
	outcome_layer = Control.new()
	outcome_layer.name = "OutcomeLayer"
	outcome_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outcome_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	outcome_layer.visible = false
	add_child(outcome_layer)
	var dim := ColorRect.new()
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.color = Color(0, 0, 0, 0.72)
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	outcome_layer.add_child(dim)
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -360
	panel.offset_top = -190
	panel.offset_right = 360
	panel.offset_bottom = 190
	panel.add_theme_stylebox_override("panel", _panel)
	outcome_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 20)
	panel.add_child(column)
	outcome_title = Label.new()
	outcome_title.name = "OutcomeTitle"
	outcome_title.text = "VICTORY"
	outcome_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_title.add_theme_font_size_override("font_size", 64)
	column.add_child(outcome_title)
	outcome_detail = Label.new()
	outcome_detail.name = "OutcomeDetail"
	outcome_detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_detail.add_theme_font_size_override("font_size", 20)
	outcome_detail.add_theme_color_override("font_color", Color("cbd8dd"))
	column.add_child(outcome_detail)
	_add_action_button(column, "Play Again", func() -> void: restart_requested.emit())
	_add_action_button(column, "Main Menu", func() -> void: main_menu_requested.emit())


func _build_failure_panel() -> void:
	failure_panel = PanelContainer.new()
	failure_panel.name = "FailurePanel"
	failure_panel.set_anchors_preset(Control.PRESET_CENTER)
	failure_panel.offset_left = -390
	failure_panel.offset_top = -155
	failure_panel.offset_right = 390
	failure_panel.offset_bottom = 155
	failure_panel.add_theme_stylebox_override("panel", _panel)
	failure_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	failure_panel.visible = false
	add_child(failure_panel)
	var margin := MarginContainer.new()
	margin.name = "FailureMargin"
	margin.add_theme_constant_override("margin_left", 24)
	margin.add_theme_constant_override("margin_right", 24)
	margin.add_theme_constant_override("margin_top", 20)
	margin.add_theme_constant_override("margin_bottom", 20)
	failure_panel.add_child(margin)
	var column := VBoxContainer.new()
	column.name = "FailureColumn"
	column.add_theme_constant_override("separation", 16)
	margin.add_child(column)
	var label := Label.new()
	label.name = "Message"
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 18)
	label.add_theme_color_override("font_color", Color("e7bd96"))
	column.add_child(label)
	_add_action_button(column, "Return to Main Menu", func() -> void: main_menu_requested.emit())


func _build_side_command_bar() -> void:
	retail_side_command_bar = SideCommandBarScript.new()
	retail_side_command_bar._build()
	retail_side_command_bar.construct_requested.connect(_emit_construct_requested)
	add_child(retail_side_command_bar)


func _build_powers_dock() -> void:
	# Retail docks purchased powers in a column hanging off the palantir's
	# upper-left rim (see RETAIL_POWER_DOCK_* measurements), cast-slot ordered
	# per the authored MenSpellBookCommandSet, so casts do not require
	# reopening the spellbook. Buttons appear here as powers are purchased.
	powers_dock = Control.new()
	powers_dock.name = "RetailPowersDock"
	powers_dock.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	powers_dock.mouse_filter = Control.MOUSE_FILTER_IGNORE
	powers_dock.z_index = 6
	add_child(powers_dock)


func _refresh_powers_dock(purchased: Array, states: Dictionary = {}) -> void:
	if powers_dock == null:
		return
	var state_powers: Dictionary = states.get("powers", {}) as Dictionary
	var castable: Array = []
	for power_id_value in purchased:
		var power_id := String(power_id_value)
		# With sim state, castability is the doc-verdict; without it nothing
		# is docked (fail closed — no invented cast buttons).
		if not state_powers.is_empty():
			if bool((state_powers.get(power_id, {}) as Dictionary).get("castable", false)):
				castable.append(power_id)
	# The authored dock order is the cast command set (MenSpellBookCommandSet
	# slot), not purchase-click order: the retail palantir column is the cast
	# bar, so a rally bought before heal still docks below it.
	if not state_powers.is_empty():
		castable.sort_custom(func(a: String, b: String) -> bool:
			return int((state_powers.get(a, {}) as Dictionary).get("cast_slot", 0)) < int((state_powers.get(b, {}) as Dictionary).get("cast_slot", 0))
		)
	# Drop buttons for powers no longer owned (match reset) before adding new.
	for existing_id in powers_dock_buttons.keys().duplicate():
		if not castable.has(existing_id):
			(powers_dock_buttons[existing_id] as Button).queue_free()
			powers_dock_buttons.erase(existing_id)
	var viewport := powers_dock.get_viewport_rect().size
	if viewport.x <= 0.0 or viewport.y <= 0.0:
		viewport = Vector2(1920.0, 1080.0)
	# Retail hangs the dock from the palantir's upper-left rim: the first cast
	# slot sits highest, and the column DESCENDS so the last socket hangs just
	# above the minimap top (dock_top − socket/2 − 3; spellbook runner
	# contract, REF-24). When the roster would rise past the screen top,
	# stride and sockets shrink uniformly so every owned power stays visible.
	var dock_top := viewport.y - RETAIL_PALANTIR_FRAME_DISPLAY_SIZE.y
	var count := castable.size()
	var stride := RETAIL_POWER_DOCK_STRIDE
	var socket_size := clampf(stride - 6.0, 28.0, RETAIL_POWER_DOCK_SIZE.x)
	var lowest_center := dock_top - socket_size * 0.5 - 3.0
	if count > 1 and lowest_center - stride * float(count - 1) < socket_size * 0.5 + 4.0:
		stride = (lowest_center - socket_size * 0.5 - 4.0) / float(count - 1)
		socket_size = clampf(stride - 6.0, 28.0, RETAIL_POWER_DOCK_SIZE.x)
	for order in count:
		var power_id: String = castable[order]
		var button: Button = powers_dock_buttons.get(power_id)
		if button == null:
			button = Button.new()
			button.name = "PowerDock_%s" % power_id
			button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
			button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
			button.expand_icon = true
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			# Reuse the spellbook button's already-validated icon + socket art.
			for index in power_buttons.size():
				if String(power_buttons[index].get_meta("power_id", "")) != power_id:
					continue
				button.icon = power_buttons[index].icon
				for state in ["normal", "hover", "pressed", "disabled", "focus"]:
					button.add_theme_stylebox_override(state, power_buttons[index].get_theme_stylebox(state))
				break
			button.tooltip_text = "Cast — click, then left-click the battlefield (right-click cancels)"
			button.pressed.connect(func() -> void:
				var dock_state: Dictionary = (_last_power_states.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
				var dock_cooldown: Dictionary = dock_state.get("cooldown", {}) as Dictionary
				if int(dock_cooldown.get("remaining_ticks", 0)) > 0:
					set_feedback("Power recharging.", true)
					return
				power_cast_requested.emit(power_id)
				ui_sound_requested.emit("Gui_PalantirChoosePowerClick")
			)
			_wire_button_feel(button)
			powers_dock.add_child(button)
			powers_dock_buttons[power_id] = button
		button.size = Vector2(socket_size, socket_size)
		button.custom_minimum_size = button.size
		button.add_theme_constant_override("icon_max_width", int(socket_size) - 10)
		# Cooldown state: dim the dock icon while the reload runs.
		var dock_state: Dictionary = state_powers.get(power_id, {}) as Dictionary
		var dock_cooldown: Dictionary = dock_state.get("cooldown", {}) as Dictionary
		button.self_modulate = Color(0.45, 0.45, 0.45) if int(dock_cooldown.get("remaining_ticks", 0)) > 0 else Color.WHITE
		# First cast slot highest; later slots descend toward the rim.
		var center := Vector2(
			RETAIL_POWER_DOCK_FIRST_CENTER.x,
			lowest_center - stride * float(count - 1 - order)
		)
		button.position = center - button.size * 0.5


func _build_retail_tooltip() -> void:
	retail_tooltip = TooltipScript.new()
	retail_tooltip._build()
	add_child(retail_tooltip)


## Costs originate from the caller (never invented here). Keys are train
## unit_ids and construct structure kinds; values are the exact resource costs
## from the sim's UNIT_PRODUCTION_RULES / STRUCTURE_BUILD_RULES. Buttons whose
## cost is not supplied show title + description only.
func set_command_costs(costs: Dictionary) -> void:
	_retail_command_costs = costs.duplicate(true)


func set_command_build_seconds(seconds: Dictionary) -> void:
	_retail_command_build_seconds = seconds.duplicate(true)


## Command-point costs per train unit_id (from the sim's production rules).
## Shown as the retail "Command Points: N" tooltip line for unit commands.
func set_command_point_costs(costs: Dictionary) -> void:
	_retail_command_point_costs = costs.duplicate(true)


func _with_build_time(description: String, command_id: String) -> String:
	var seconds := float(_retail_command_build_seconds.get(command_id, -1.0))
	if seconds <= 0.0:
		return description
	var row := "Build time: %ds" % ceili(seconds)
	return row if description.strip_edges() == "" else "%s\n%s" % [description, row]


func _wire_retail_tooltips() -> void:
	for spec_value in _retail_command_specs:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		var button: Button = train_buttons.get(unit_id)
		if button == null:
			continue
		button.set_meta("tooltip_group", "train")
		button.set_meta("tooltip_unit_id", unit_id)
		button.set_meta("tooltip_fallback_label", String(spec["fallback_label"]))
		button.set_meta("tooltip_fallback_desc", String(spec["fallback_tooltip"]))
		_register_button_tooltip(button)
	for spec in _hero_command_specs:
		var unit_id := String(spec["unit_id"])
		var button: Button = hero_buttons.get(unit_id)
		if button == null:
			continue
		button.set_meta("tooltip_group", "train")
		button.set_meta("tooltip_unit_id", unit_id)
		button.set_meta("tooltip_fallback_label", String(spec["fallback_label"]))
		button.set_meta("tooltip_fallback_desc", String(spec["fallback_tooltip"]))
		_register_button_tooltip(button)
	for spec_value in _retail_action_specs:
		var spec: Dictionary = spec_value
		var action_id := String(spec["action_id"])
		var button: Button = unit_action_buttons.get(action_id)
		if button == null:
			continue
		button.set_meta("tooltip_group", "action")
		button.set_meta("tooltip_action_id", action_id)
		button.set_meta("tooltip_fallback_label", String(spec["button_name"]))
		_register_button_tooltip(button)
	for id in orb_buttons.keys():
		var orb := orb_buttons[id] as Button
		orb.set_meta("tooltip_group", "orb")
		orb.set_meta("tooltip_orb_title", String(id).capitalize())
		_register_button_tooltip(orb)
	for index in power_buttons.size():
		var power := power_buttons[index]
		power.set_meta("tooltip_group", "power")
		power.set_meta("tooltip_power_id", _spellbook_icon_id_for(String(power.get_meta("power_id", ""))))
		_register_button_tooltip(power)


func _wire_button_feel(button: Button) -> void:
	# Hybrid interaction feel: retail-style warm glow on hover and a press
	# dip, applied by modulation so the authored icon art stays untouched.
	if button.has_meta("feel_wired"):
		return
	button.set_meta("feel_wired", true)
	button.mouse_entered.connect(func() -> void:
		if not button.disabled:
			button.self_modulate = Color(1.22, 1.16, 1.02)
	)
	button.mouse_exited.connect(func() -> void:
		button.self_modulate = Color.WHITE
	)
	button.button_down.connect(func() -> void:
		button.self_modulate = Color(0.82, 0.78, 0.7)
	)
	button.button_up.connect(func() -> void:
		button.self_modulate = Color(1.22, 1.16, 1.02) if button.is_hovered() else Color.WHITE
	)


func _register_button_tooltip(button: Button) -> void:
	if button.has_meta("tooltip_registered"):
		return
	button.set_meta("tooltip_registered", true)
	button.mouse_entered.connect(_begin_tooltip_hover.bind(button))
	button.mouse_exited.connect(_end_tooltip_hover.bind(button))


func _begin_tooltip_hover(button: Button) -> void:
	_tooltip_hover_button = button
	if not is_inside_tree():
		return
	# A SceneTreeTimer outlives the hovered button (hero-bar and spellbook-dock
	# buttons are freed and rebuilt while the pointer rests on them), so the
	# callback must NOT be a lambda capturing the button: Godot validates lambda
	# captures before the body runs, so an inner is_instance_valid() guard cannot
	# suppress "Lambda capture at index 0 was freed". Bind an int token instead
	# and re-resolve the button from the member on the way out.
	_tooltip_hover_token += 1
	var timer := get_tree().create_timer(RETAIL_TOOLTIP_HOVER_DELAY)
	timer.timeout.connect(_on_tooltip_hover_elapsed.bind(_tooltip_hover_token))


func _on_tooltip_hover_elapsed(token: int) -> void:
	if token != _tooltip_hover_token:
		return
	if not is_instance_valid(_tooltip_hover_button):
		_tooltip_hover_button = null
		return
	show_retail_tooltip(_tooltip_hover_button)


func _end_tooltip_hover(button: Button) -> void:
	if _tooltip_hover_button == button:
		_tooltip_hover_button = null
		if retail_tooltip != null:
			retail_tooltip.hide_tooltip()


## Direct hover-path entry (also used by the focused runner): resolves the
## button's pack-sourced strings/cost and shows the retail tooltip above the
## bottom bar.
func show_retail_tooltip(button: Button) -> void:
	if retail_tooltip == null or button == null:
		return
	var content := _resolve_tooltip_content(button)
	if String(content.get("title", "")) == "":
		return
	retail_tooltip.show_content(
		String(content["title"]),
		int(content.get("cost", -1)),
		String(content.get("shortcut", "")),
		String(content.get("description", "")),
		int(content.get("command_points", -1))
	)
	var screen := get_viewport_rect().size
	retail_tooltip.place_retail_anchor(screen)


func _resolve_tooltip_content(button: Button) -> Dictionary:
	var group := String(button.get_meta("tooltip_group", ""))
	match group:
		"train":
			var unit_id := String(button.get_meta("tooltip_unit_id", ""))
			var fallback_label := String(button.get_meta("tooltip_fallback_label", ""))
			var fallback_desc := String(button.get_meta("tooltip_fallback_desc", ""))
			var desc := button.tooltip_text if button.tooltip_text != "" else fallback_desc
			var label := command_label(unit_id, fallback_label)
			return {
				"title": label,
				"cost": int(_retail_command_costs.get(unit_id, -1)),
				"shortcut": RetailTooltip.extract_hotkey_letter(label),
				"description": _with_build_time(desc, unit_id),
				"command_points": int(_retail_command_point_costs.get(unit_id, -1)),
			}
		"action":
			var action_id := String(button.get_meta("tooltip_action_id", ""))
			var fallback_label := String(button.get_meta("tooltip_fallback_label", ""))
			var title := String(button.get_meta("retail_label", fallback_label))
			var cost := -1
			var time_key := ""
			if action_id.begins_with("construct_"):
				time_key = action_id.trim_prefix("construct_")
				cost = int(_retail_command_costs.get(time_key, -1))
			elif action_id == "upgrade_archery_range_level2":
				cost = int(_retail_command_costs.get("Upgrade_GondorArcheryRangeLevel2", -1))
			return {
				"title": title,
				"cost": cost,
				"shortcut": RetailTooltip.extract_hotkey_letter(title),
				"description": _with_build_time(button.tooltip_text, time_key),
			}
		"expansion":
			var kind := String(button.get_meta("tooltip_unit_id", ""))
			var expansion_title := String((_retail_expansion_validated.get(kind, {}) as Dictionary).get("label", ""))
			return {
				"title": expansion_title,
				"cost": int(_retail_command_costs.get(kind, -1)),
				"shortcut": RetailTooltip.extract_hotkey_letter(expansion_title),
				"description": _with_build_time(String((_retail_expansion_validated.get(kind, {}) as Dictionary).get("tooltip", "")), kind),
			}
		"orb":
			return {
				"title": String(button.get_meta("tooltip_orb_title", "")),
				"cost": -1,
				"shortcut": "",
				"description": "",
			}
		"power":
			var power_id := String(button.get_meta("tooltip_power_id", ""))
			var power_description := ""
			for row_value in _spellbook_power_rows:
				var row: Dictionary = row_value
				if String(row.get("icon_id", "")) == power_id or String(row.get("power_id", "")) == power_id:
					power_description = String(row.get("tooltip", ""))
					break
			return {
				"title": _retail_power_title(power_id),
				"cost": -1,
				"shortcut": "",
				"description": power_description,
			}
		"side_build":
			var kind := String(button.get_meta("construct_kind", ""))
			var side_title := String(button.get_meta("tooltip_title", kind.capitalize()))
			return {
				"title": side_title,
				"cost": int(_retail_command_costs.get(kind, -1)),
				"shortcut": RetailTooltip.extract_hotkey_letter(side_title),
				"description": String(button.get_meta("tooltip_desc", "")),
			}
		"hero_bar":
			return {
				"title": String(button.get_meta("hero_name", "Hero")),
				"cost": -1,
				"shortcut": "",
				"description": "Level: %d / Maximum Level: 10\nHealth: %d" % [
					int(button.get_meta("hero_level", 1)),
					int(button.get_meta("hero_health", 0)),
				],
			}
	return {"title": ""}


func _retail_power_title(power_id: String) -> String:
	# The power's name is the image id itself (e.g. "SBGood_RallyingCall"),
	# formatted for display; no lore string is invented.
	var base := power_id
	var underscore := base.find("_")
	if underscore >= 0:
		base = base.substr(underscore + 1)
	var out := ""
	for i in base.length():
		var ch := base[i]
		if i > 0 and ch == ch.to_upper() and ch != ch.to_lower():
			out += " "
		out += ch
	return out


func _refresh_side_command_bar(builders_only: bool) -> void:
	if retail_side_command_bar == null:
		return
	if builders_only:
		var constructs: Array = []
		# Prefer manifest structure_kinds order so workshop and extra buildings
		# appear in the same order the sim can construct them.
		var kind_order: Array = []
		if not _manifest_construct_kinds.is_empty():
			for kind_value in _manifest_construct_kinds:
				kind_order.append(String(kind_value))
		else:
			for spec_value in _retail_action_specs:
				var action_id := String((spec_value as Dictionary).get("action_id", ""))
				if action_id.begins_with("construct_"):
					kind_order.append(action_id.trim_prefix("construct_"))
		for kind_value in kind_order:
			var kind := String(kind_value)
			if kind == "" or _porter_strip_excluded(kind):
				continue
			var action_id := "construct_%s" % kind
			var button: Button = unit_action_buttons.get(action_id)
			var title := kind.replace("_", " ").capitalize()
			var desc := ""
			if button != null:
				title = String(button.get_meta("retail_label", title))
				desc = button.tooltip_text
			var icon: Texture2D = button.icon if button != null else null
			# Iconless entries (factions without converted build art today)
			# still render — as honest text in the socket, never Men art.
			constructs.append({
				"kind": kind,
				"icon": icon,
				"title": title,
				"description": desc,
				"text_only": icon == null,
			})
		constructs.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
			var left_rank: int = PORTER_STRIP_RETAIL_ORDER.find(String(left.get("kind", "")))
			var right_rank: int = PORTER_STRIP_RETAIL_ORDER.find(String(right.get("kind", "")))
			if left_rank < 0:
				left_rank = PORTER_STRIP_RETAIL_ORDER.size()
			if right_rank < 0:
				right_rank = PORTER_STRIP_RETAIL_ORDER.size()
			return left_rank < right_rank
		)
		# This runs every presentation frame; rebuilding the buttons each call
		# destroyed and recreated them faster than clicks could land (hover
		# flickered, presses died between generations). Rebuild only when the
		# construct set actually changes.
		var fingerprint := ""
		for entry_value in constructs:
			var entry_row: Dictionary = entry_value
			# Icon availability is part of the fingerprint: the first call can
			# happen before retail icons bind, and caching that iconless
			# generation left the side bar black.
			fingerprint += "%s:%s;" % [String(entry_row.get("kind", "")), entry_row.get("icon") != null]
		if fingerprint != _side_bar_fingerprint:
			_side_bar_fingerprint = fingerprint
			retail_side_command_bar.configure_from_constructs(constructs)
			for side_button in retail_side_command_bar.side_buttons():
				side_button.set_meta("tooltip_group", "side_build")
				var kind := String(side_button.get_meta("construct_kind", ""))
				for entry_value in constructs:
					var entry: Dictionary = entry_value
					if String(entry.get("kind", "")) == kind:
						side_button.set_meta("tooltip_title", String(entry.get("title", "")))
						side_button.set_meta("tooltip_desc", String(entry.get("description", "")))
				_register_button_tooltip(side_button)
				_wire_button_feel(side_button)
	retail_side_command_bar.set_builder_visible(builders_only)


class RankPipsOverlay:
	## Placeholder-styled rank pips on the selection portrait, styled after the
	## retail level badges (reference/INDEX.md REF-24); no retail art claims.
	extends Control
	var pips := 0


	func _ready() -> void:
		mouse_filter = Control.MOUSE_FILTER_IGNORE
		visible = false


	func set_pips(count: int) -> void:
		pips = maxi(0, count)
		visible = pips > 0
		queue_redraw()


	func _draw() -> void:
		for pip_index in range(pips):
			var x := 4.0 + float(pip_index) * 9.0
			var top := size.y - 10.0
			draw_colored_polygon(
				PackedVector2Array([
					Vector2(x, top + 7.0),
					Vector2(x + 4.0, top),
					Vector2(x + 8.0, top + 7.0),
				]),
				Color(0.95, 0.85, 0.35, 0.95)
			)
