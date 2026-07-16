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
signal cancel_production_requested(queue_index: int)
signal attack_move_requested
signal stop_requested
signal stance_requested
signal command_cap_changed(value: int)
signal weak_fortress_toggled(value: bool)
signal power_purchase_requested(power_id: String, cost: int)
signal power_cast_requested(cast_kind: String)
signal powers_opened
signal construct_requested(structure_kind: String)
signal music_volume_changed(value: float)
signal voice_volume_changed(value: float)
signal mute_changed(value: bool)
signal ui_sound_requested(event_id: String)

const MinimapScript = preload("res://src/retail_slice/retail_minimap.gd")
const PalantirFrameScript = preload("res://src/retail_slice/retail_palantir_frame.gd")
const AptRuntimeScript = preload("res://src/retail_slice/retail_hud_apt_runtime.gd")
const TooltipScript = preload("res://src/retail_slice/retail_tooltip.gd")
const SideCommandBarScript = preload("res://src/retail_slice/retail_side_command_bar.gd")
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
const RETAIL_RADAR_PARCHMENT_REGION := Rect2(4, 4, 214, 214)
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
# command panel, whose origin sits at dock (360, 0).
const RETAIL_COMMAND_SLOT_SOURCE := [
	Vector2(150, 64), Vector2(240, 97), Vector2(309, 159),
	Vector2(315, 231), Vector2(268, 291), Vector2(148, 311),
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
	},
	{
		"action_id": "stance",
		"button_name": "Stance",
		"image_id": "UCCommon_HoldGroundStance",
		"label_id": "CONTROLBAR:ToggleStanceHoldGround",
		"tooltip_id": "CONTROLBAR:ToolTipToggleStanceHoldGround",
	},
	{"action_id": "construct_farm", "button_name": "BuildFarm", "image_id": "BCFarm", "label_id": "CONTROLBAR:ConstructMenFarm", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFarm"},
	{"action_id": "construct_barracks", "button_name": "BuildBarracks", "image_id": "BGBarracks", "label_id": "CONTROLBAR:ConstructMenBarracks", "tooltip_id": "CONTROLBAR:ToolTipConstructMenBarracks"},
	{"action_id": "construct_archery_range", "button_name": "BuildArcheryRange", "image_id": "BGArcheryRange", "label_id": "CONTROLBAR:ConstructMenArcheryRange", "tooltip_id": "CONTROLBAR:ToolTipMenArcheryRange"},
	{"action_id": "construct_stable", "button_name": "BuildStable", "image_id": "BGStables", "label_id": "CONTROLBAR:ConstructMenStable", "tooltip_id": "CONTROLBAR:ToolTipConstructMenStable"},
	{"action_id": "construct_fortress", "button_name": "BuildFortress", "image_id": "BGFortress", "label_id": "CONTROLBAR:ConstructMenFortress", "tooltip_id": "CONTROLBAR:ToolTipConstructMenFortress"},
	# M3 building set. Well and Statue have no retail tooltip string (source
	# authors none); an empty tooltip_id reuses the label, mirroring retail.
	{"action_id": "construct_workshop", "button_name": "BuildWorkshop", "image_id": "BGWorkshop", "label_id": "CONTROLBAR:ConstructMenWorkshop", "tooltip_id": "CONTROLBAR:ToolTipConstructMenWorkshop"},
	{"action_id": "construct_battle_tower", "button_name": "BuildBattleTower", "image_id": "BGBattleTower", "label_id": "CONTROLBAR:ConstructMenSentryTower", "tooltip_id": "CONTROLBAR:ToolTipConstructMenSentryTower"},
	{"action_id": "construct_well", "button_name": "BuildWell", "image_id": "BGWell", "label_id": "CONTROLBAR:ConstructMenWell", "tooltip_id": ""},
	{"action_id": "construct_statue", "button_name": "BuildStatue", "image_id": "BGHeroicStatue", "label_id": "CONTROLBAR:ConstructMenStatue", "tooltip_id": ""},
	{"action_id": "construct_blacksmith", "button_name": "BuildBlacksmith", "image_id": "BGBlacksmith", "label_id": "CONTROLBAR:ConstructMenBlacksmith", "tooltip_id": "CONTROLBAR:ToolTipConstructMenBlacksmith"},
	{"action_id": "construct_marketplace", "button_name": "BuildMarketplace", "image_id": "BGMarketplace", "label_id": "CONTROLBAR:ConstructMenMarketPlace", "tooltip_id": "CONTROLBAR:ToolTipConstructMenMarketPlace"},
]
# Constructs available before the pack's building-stats data is loaded: the
# original five slice buildings. The slice extends this once typed stats for
# the M3 buildings are read from the selected pack.
const DEFAULT_AVAILABLE_CONSTRUCTS := ["farm", "barracks", "archery_range", "stable", "fortress"]
const RETAIL_MEMBER_TO_HORDE := {
	"bfme2.object.gondor-fighter": "bfme2.object.gondor-fighter-horde",
	"bfme2.object.gondor-tower-guard": "bfme2.object.gondor-tower-guard",
	"bfme2.object.gondor-archer": "bfme2.object.gondor-archer",
	"bfme2.object.gondor-knight": "bfme2.object.gondor-knight",
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
var unit_action_buttons: Dictionary = {}
var production_queue_label: Label
var production_progress: ProgressBar
var cancel_production_button: Button
var selection_portrait: TextureRect
var synthetic_palantir_frame: RetailPalantirFrame
var retail_control_bar_frame: RetailPalantirFrame
var retail_apt_runtime: RetailHudAptRuntime
var group_buttons: Dictionary = {}
var pause_panel: PanelContainer
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
var available_construct_kinds: Dictionary = {
	"farm": true, "barracks": true, "archery_range": true, "stable": true, "fortress": true,
}


func set_available_constructs(kinds: Array) -> void:
	available_construct_kinds = {}
	for kind in kinds:
		available_construct_kinds[String(kind)] = true
	# Force the side bar to rebuild with the new construct set.
	_side_bar_fingerprint = "<unset>"
var production_queue_buttons: Array[Button] = []
var power_points_label: Label
var _powers_connectors: Control
var powers_dock: Control
var powers_dock_buttons: Dictionary = {}
var fps_overlay: Label
var _frame_times: PackedFloat32Array = PackedFloat32Array()
var voice_slider: HSlider
var mute_toggle: CheckButton
var command_panel: PanelContainer
var command_grid: Control
var orb_buttons: Dictionary = {}
var powers_palette: Control
var power_buttons: Array[Button] = []
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
var _tooltip_hover_button: Button = null


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
	_build_outcome_layer()
	_build_failure_panel()
	_build_side_command_bar()
	_build_powers_dock()
	_build_retail_tooltip()
	_wire_retail_tooltips()


func configure_minimap(simulation: RefCounted, map_data: RefCounted, camera_value: Camera3D = null, preview: Texture2D = null) -> void:
	minimap.configure(simulation, map_data, preview)
	minimap.world_camera = camera_value


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
	if orb_buttons.has("powers"):
		power_orb_label.text = "%d" % command_points


func set_score_values(units_trained: int, units_lost: int, resources_gathered: int) -> void:
	if score_labels.has("units_trained"):
		(score_labels["units_trained"] as Label).text = "Units Trained  %d" % units_trained
		(score_labels["units_lost"] as Label).text = "Units Lost  %d" % units_lost
		(score_labels["resources_gathered"] as Label).text = "Resources Gathered  %d" % resources_gathered


func set_selection(text: String) -> void:
	selection_label.text = text


func sync_retail_selection_context(
	selected_ids: Array[int],
	selected_structure_id: int,
	entities: Dictionary,
	structures: Dictionary,
	winner: int
) -> bool:
	if retail_apt_runtime == null or not retail_apt_bound:
		return not private_parity_mode_active
	var context := {
		"selected_ids": selected_ids.duplicate(),
		"selected_structure_id": selected_structure_id,
		"entities": entities,
		"structures": structures,
		"winner": winner,
		"local_team": 0,
	}
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


func set_production_state(production: Array, enabled: bool, queue_count: int = 0, queue_state: Array = []) -> void:
	## Only commands authored by the selected producer are exposed. Labels stay
	## source-derived in private parity mode; queue state is metadata, not copy.
	for spec_value in RETAIL_COMMAND_SPECS:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		var button: Button = train_buttons.get(unit_id)
		if button == null:
			continue
		var supported := production.has(unit_id)
		button.visible = supported
		button.disabled = not enabled or not supported
		button.set_meta("producer_queue_count", maxi(0, queue_count))
		if _retail_train_labels.has(unit_id) and not private_parity_mode_active:
			button.text = String(_retail_train_labels[unit_id])
	for button_value in unit_action_buttons.values():
		(button_value as Button).visible = false
	_layout_command_sockets()
	_update_production_queue(queue_state, not production.is_empty())
	_update_retail_selection_portrait(production)


func set_unit_selection_state(selected_ids: Array[int], entities: Dictionary) -> void:
	var has_units := not selected_ids.is_empty()
	var builders_only := has_units
	for selected_id in selected_ids:
		if not bool((entities.get(selected_id, {}) as Dictionary).get("is_builder", false)):
			builders_only = false
			break
	for button_value in unit_action_buttons.values():
		var button := button_value as Button
		var action_id := String(button.get_meta("action_id", ""))
		var is_construct := action_id.begins_with("construct_")
		if is_construct:
			button.visible = builders_only and available_construct_kinds.has(action_id.trim_prefix("construct_"))
		else:
			button.visible = has_units and not builders_only
		button.disabled = not button.visible
	_layout_command_sockets()
	_refresh_side_command_bar(builders_only)
	if not has_units:
		_update_retail_selection_portrait([])
		return
	var first: Dictionary = entities.get(selected_ids[0], {}) as Dictionary
	var member_id := String(first.get("object_id", ""))
	var horde_id := String(RETAIL_MEMBER_TO_HORDE.get(member_id, member_id))
	_show_retail_portrait(horde_id)


func _update_production_queue(queue_state: Array, producer_selected: bool) -> void:
	for index in production_queue_buttons.size():
		var queue_button := production_queue_buttons[index]
		if not producer_selected or index >= queue_state.size():
			queue_button.visible = false
			continue
		var row: Dictionary = queue_state[index]
		var unit_type := String(row.get("unit_type", ""))
		var train_button := train_buttons.get(unit_type) as Button
		queue_button.icon = train_button.icon if train_button != null else null
		queue_button.visible = queue_button.icon != null
	if production_queue_label == null or production_progress == null or cancel_production_button == null:
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
	button.set_meta("active_stance", stance)
	var source_label := String(button.get_meta("retail_label", "Stance"))
	button.tooltip_text = "%s\nCurrent: %s" % [source_label, stance]


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
	var spec: Dictionary = RETAIL_COMMAND_SPECS[0]
	var validation := _validate_retail_command(content_db, expected_pack_root, spec, Vector2i.ZERO)
	var error := String(validation.get("error", ""))
	if error != "":
		return error
	_apply_retail_command(spec, validation)
	train_button.visible = true
	train_button.disabled = false
	retail_train_command_bound = true
	_retail_train_label = String(validation["label"])
	retail_train_icon_aspect_ratio = float(validation["aspect_ratio"])
	return ""


func bind_retail_train_commands(content_db, expected_pack_root: String, private_parity_mode: bool) -> String:
	## The private surface is atomic: exact Gondor control bar, four portraits,
	## four commands, and source strings must all originate in the selected pack.
	_clear_retail_command_bindings(private_parity_mode)
	if not private_parity_mode:
		return ""
	if content_db == null:
		return "ContentDB is unavailable; cannot bind the private Men production UI."
	if not _built or train_buttons.size() != RETAIL_COMMAND_SPECS.size():
		return "The Men production command buttons have not been built."
	if retail_apt_runtime == null:
		return "The retail Palantir APT runtime has not been built."
	var apt_configured := retail_apt_runtime.configure_from_pack(expected_pack_root, true)
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
	var validation_errors: Array[String] = []
	for spec_value in RETAIL_COMMAND_SPECS:
		var spec: Dictionary = spec_value
		var validation := _validate_retail_command(content_db, expected_pack_root, spec)
		var error := String(validation.get("error", ""))
		if error != "":
			validation_errors.append(error)
		else:
			validated[String(spec["unit_id"])] = validation
	var action_validated: Dictionary = {}
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
		var spec: Dictionary = spec_value
		var action_id := String(spec["action_id"])
		var source_size := Vector2i(64, 64) if action_id.begins_with("construct_") else Vector2i(63, 63)
		var validation := _validate_retail_command(content_db, expected_pack_root, spec, source_size)
		var error := String(validation.get("error", ""))
		if error != "":
			validation_errors.append(error)
		else:
			action_validated[action_id] = validation
	var portrait_validated: Dictionary = {}
	for spec_value in RETAIL_PORTRAIT_SPECS:
		var spec: Dictionary = spec_value
		var validation := _validate_retail_image(
			content_db,
			expected_pack_root,
			String(spec["image_id"]),
			RETAIL_PORTRAIT_SOURCE_SIZE
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
	for spec_value in RETAIL_COMMAND_SPECS:
		var spec: Dictionary = spec_value
		_apply_retail_command(spec, validated[String(spec["unit_id"])])
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
		var spec: Dictionary = spec_value
		_apply_retail_action(spec, action_validated[String(spec["action_id"])])
	for spec_value in RETAIL_PORTRAIT_SPECS:
		var spec: Dictionary = spec_value
		var unit_id := String(spec["unit_id"])
		_retail_portrait_textures[unit_id] = portrait_validated[unit_id]["texture"]
	var portrait_bindings: Dictionary = {}
	for spec_value in RETAIL_PORTRAIT_SPECS:
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
	_retail_train_label = String(_retail_train_labels[String(RETAIL_COMMAND_SPECS[0]["unit_id"])])
	retail_train_icon_aspect_ratio = float(train_button.get_meta("retail_icon_aspect_ratio", 0.0))
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
	var image_validation := _validate_retail_image(content_db, expected_pack_root, image_id, exact_size)
	if String(image_validation.get("error", "")) != "":
		return image_validation

	var label_text := String(content_db.get_retail_string(label_id, _MISSING_RETAIL_STRING))
	if label_text == _MISSING_RETAIL_STRING:
		return {"error": "Required localized string '%s' is missing." % label_id}
	# An empty tooltip_id means retail authors no description for this command;
	# the label doubles as the tooltip (source behavior, not an invention).
	var tooltip_text := label_text
	if tooltip_id != "":
		tooltip_text = String(content_db.get_retail_string(tooltip_id, _MISSING_RETAIL_STRING))
		if tooltip_text == _MISSING_RETAIL_STRING:
			return {"error": "Required localized string '%s' is missing." % tooltip_id}
	image_validation["label"] = label_text
	image_validation["tooltip"] = tooltip_text
	return image_validation


func _validate_retail_image(content_db, expected_pack_root: String, image_id: String, exact_size: Vector2i) -> Dictionary:
	var image_definition: Dictionary = content_db.get_retail_ui_image(image_id)
	if image_definition.is_empty():
		return {"error": "Required UI manifest image '%s' is missing." % image_id}
	var image_pack_root := String(image_definition.get("_pack_root", ""))
	if expected_pack_root == "" or image_pack_root != expected_pack_root:
		return {"error": "Required UI image '%s' did not come from the selected private pack." % image_id}

	var image_path := String(content_db.resolve_retail_ui_image_path(image_id))
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


func _apply_retail_action(spec: Dictionary, validation: Dictionary) -> void:
	var action_id := String(spec["action_id"])
	var button: Button = unit_action_buttons[action_id]
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
	for spec_value in RETAIL_COMMAND_SPECS:
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
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
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
	private_parity_mode_active = true
	if synthetic_palantir_frame != null:
		synthetic_palantir_frame.fail_closed_private_shell()
	if retail_control_bar_frame != null:
		retail_control_bar_frame.fail_closed_private_shell()
		retail_control_bar_frame.visible = false
	if retail_apt_runtime != null:
		retail_apt_runtime.reset_runtime()
		retail_apt_runtime.visible = false
	for node_name in ["ObjectiveBanner", "ControlGroupStrip", "FeedbackPanel", "DiagnosticsPanel"]:
		var node := get_node_or_null(node_name) as Control
		if node != null:
			node.visible = false
	var empty := StyleBoxEmpty.new()
	for node_path in ["CommandPanel", "PalantirDock/ResourceStrip"]:
		var panel := get_node_or_null(node_path) as PanelContainer
		if panel != null:
			panel.add_theme_stylebox_override("panel", empty)
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
	for spec_value in RETAIL_PORTRAIT_SPECS:
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
	heading.text = "MEN OF THE WEST"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_color_override("font_color", Color("dccb95"))
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	selection_label = Label.new()
	selection_label.name = "SelectionSummary"
	selection_label.text = "No battalion selected"
	selection_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	selection_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	selection_label.add_theme_color_override("font_color", Color("d0e1e9"))
	selection_label.custom_minimum_size.y = 44
	column.add_child(selection_label)
	selection_portrait = TextureRect.new()
	selection_portrait.name = "SelectionPortrait"
	selection_portrait.custom_minimum_size = Vector2(76, 76)
	selection_portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	selection_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	selection_portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	selection_portrait.visible = false
	column.add_child(selection_portrait)
	command_grid = Control.new()
	command_grid.name = "CommandGrid"
	command_grid.custom_minimum_size = Vector2(216, 250)
	# The grid spans the whole command panel; with the default STOP filter it
	# swallowed every click aimed at column widgets it overlaps (the Cancel
	# training button most visibly). Buttons inside the grid still receive
	# input — IGNORE only exempts the grid itself.
	command_grid.mouse_filter = Control.MOUSE_FILTER_IGNORE
	column.add_child(command_grid)
	var slot_index := 0
	for spec_value in RETAIL_COMMAND_SPECS:
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
		_place_command_button(button, slot_index)
		slot_index += 1
		train_buttons[unit_id] = button
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
		var spec: Dictionary = spec_value
		var action_id := String(spec["action_id"])
		var button := Button.new()
		button.name = String(spec["button_name"])
		button.text = String(spec["button_name"])
		button.custom_minimum_size = Vector2(54, 54)
		button.disabled = true
		button.visible = false
		button.set_meta("action_id", action_id)
		_style_button(button)
		if action_id == "attack_move":
			button.pressed.connect(func() -> void: attack_move_requested.emit())
		elif action_id == "stop":
			button.pressed.connect(func() -> void: stop_requested.emit())
		elif action_id == "stance":
			button.pressed.connect(func() -> void: stance_requested.emit())
		elif action_id.begins_with("construct_"):
			button.pressed.connect(_emit_construct_requested.bind(action_id.trim_prefix("construct_")))
		_place_command_button(button, 0)
		unit_action_buttons[action_id] = button
	train_button = train_buttons[String(RETAIL_COMMAND_SPECS[0]["unit_id"])]
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
	_build_powers_palette()
	_build_score_overlay()


func _place_command_button(button: Button, slot: int) -> void:
	button.position = RETAIL_COMMAND_SLOT_SOURCE[slot]
	button.size = RETAIL_COMMAND_SLOT_SIZE
	command_grid.add_child(button)


func _layout_command_sockets() -> void:
	# Single placement authority: whatever commands are visible for the
	# current selection occupy the six dish sockets in declaration order.
	# Icons render inside the socket art itself, so nothing can drift off the
	# ring the way the old static per-creation slots did.
	var occupants: Array[Button] = []
	for spec_value in RETAIL_COMMAND_SPECS:
		var train_button_row: Button = train_buttons.get(String((spec_value as Dictionary)["unit_id"]))
		if train_button_row != null and train_button_row.visible:
			occupants.append(train_button_row)
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
		var action_button: Button = unit_action_buttons.get(String((spec_value as Dictionary)["action_id"]))
		if action_button != null and action_button.visible:
			occupants.append(action_button)
	for index in occupants.size():
		if index >= RETAIL_COMMAND_SLOT_SOURCE.size():
			occupants[index].visible = false
			continue
		occupants[index].position = RETAIL_COMMAND_SLOT_SOURCE[index]
		occupants[index].size = RETAIL_COMMAND_SLOT_SIZE


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
			power_orb_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			power_orb_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			power_orb_label.add_theme_font_size_override("font_size", 16)
			power_orb_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
			button.add_child(power_orb_label)


# Spellbook layout: four tiers as columns, three powers per tier, same-row
# progression between tiers. Costs and the exact retail tree are provisional
# until the M3 SpellBook INI extraction; castable powers work today.
const POWER_TIER_COST := [1, 2, 3, 4]
const CASTABLE_POWERS := {
	"SBGood_Heal": "heal",
	"SBGood_RallyingCall": "rally",
}


func _power_index_tier(index: int) -> int:
	return index / 3


func _build_powers_palette() -> void:
	# The spellbook is its own screen: solid backdrop, no HUD cluster behind
	# it, tier columns with progression connectors, live power-point balance.
	powers_palette = Control.new()
	powers_palette.name = "RetailPowersPalette"
	powers_palette.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	powers_palette.visible = false
	powers_palette.mouse_filter = Control.MOUSE_FILTER_STOP
	powers_palette.z_index = 10
	add_child(powers_palette)
	var backdrop := ColorRect.new()
	backdrop.name = "PowersBackdrop"
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.035, 0.05, 0.04)
	backdrop.mouse_filter = Control.MOUSE_FILTER_STOP
	backdrop.gui_input.connect(func(event: InputEvent) -> void:
		if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
			_toggle_powers_palette()
	)
	powers_palette.add_child(backdrop)
	var column := VBoxContainer.new()
	column.name = "PowersColumn"
	column.set_anchors_preset(Control.PRESET_CENTER)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 24)
	column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	powers_palette.add_child(column)
	var title := Label.new()
	title.text = "POWERS OF THE WEST"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 42)
	title.add_theme_color_override("font_color", Color("e9d489"))
	column.add_child(title)
	power_points_label = Label.new()
	power_points_label.name = "PowerPointsLabel"
	power_points_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	power_points_label.add_theme_font_size_override("font_size", 20)
	power_points_label.add_theme_color_override("font_color", Color("bfe0f2"))
	column.add_child(power_points_label)
	var tree_holder := Control.new()
	tree_holder.name = "PowersTree"
	tree_holder.custom_minimum_size = Vector2(4 * 150 + 90, 3 * 132 + 20)
	column.add_child(tree_holder)
	_powers_connectors = Control.new()
	_powers_connectors.name = "PowerConnectors"
	_powers_connectors.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_powers_connectors.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_powers_connectors.draw.connect(func() -> void:
		# Progression connectors: same-row link from each tier to the next.
		for index in power_buttons.size():
			if _power_index_tier(index) >= 3:
				continue
			var from_button := power_buttons[index]
			var to_button := power_buttons[index + 3]
			var from_point := from_button.position + Vector2(from_button.size.x, from_button.size.y * 0.5)
			var to_point := to_button.position + Vector2(0.0, to_button.size.y * 0.5)
			_powers_connectors.draw_line(from_point, to_point, Color(0.55, 0.48, 0.28, 0.8), 2.0, true)
	)
	tree_holder.add_child(_powers_connectors)
	for index in RETAIL_POWER_IMAGE_IDS.size():
		var tier := _power_index_tier(index)
		var row := index % 3
		var button := Button.new()
		button.name = "Power%02d" % index
		button.position = Vector2(tier * 150 + 45, row * 132 + 10)
		button.size = Vector2(104, 104)
		button.disabled = true
		button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		button.expand_icon = true
		button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		button.set_meta("power_id", RETAIL_POWER_IMAGE_IDS[index])
		button.set_meta("power_cost", POWER_TIER_COST[tier])
		button.pressed.connect(_on_power_button_pressed.bind(index))
		button.mouse_entered.connect(func() -> void:
			ui_sound_requested.emit("Gui_UpgradeButtonGlow")
		)
		for state in ["normal", "hover", "pressed", "disabled"]:
			button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
		var cost_badge := Label.new()
		cost_badge.name = "Cost"
		cost_badge.text = str(POWER_TIER_COST[tier])
		cost_badge.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
		cost_badge.offset_left = -26
		cost_badge.offset_top = -26
		cost_badge.add_theme_font_size_override("font_size", 17)
		cost_badge.add_theme_color_override("font_color", Color("f1d06e"))
		cost_badge.mouse_filter = Control.MOUSE_FILTER_IGNORE
		button.add_child(cost_badge)
		tree_holder.add_child(button)
		power_buttons.append(button)
	var hint := Label.new()
	hint.name = "PowersHint"
	hint.text = "Earn power points in battle. Heal and Rallying Call are castable; summons unlock with the faction expansion."
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hint.add_theme_font_size_override("font_size", 15)
	hint.add_theme_color_override("font_color", Color("9fb2a1"))
	column.add_child(hint)


func refresh_powers(points: int, purchased: Array) -> void:
	if power_points_label != null:
		power_points_label.text = "Power points: %d" % points
	for index in power_buttons.size():
		var button := power_buttons[index]
		var power_id := String(button.get_meta("power_id", ""))
		var cost := int(button.get_meta("power_cost", 1))
		var tier := _power_index_tier(index)
		var owned := purchased.has(power_id)
		var prerequisite_met := tier == 0 or purchased.has(String(power_buttons[index - 3].get_meta("power_id", "")))
		button.disabled = owned == false and (points < cost or not prerequisite_met)
		if owned:
			button.self_modulate = Color(1.15, 1.1, 0.85)
			button.tooltip_text = "Purchased%s" % (" — click to cast" if CASTABLE_POWERS.has(power_id) else "")
			button.disabled = not CASTABLE_POWERS.has(power_id)
		else:
			button.self_modulate = Color.WHITE if not button.disabled else Color(0.55, 0.55, 0.55)
	_refresh_powers_dock(purchased)


func _on_power_button_pressed(index: int) -> void:
	var button := power_buttons[index]
	var power_id := String(button.get_meta("power_id", ""))
	if button.self_modulate.g > 1.0 and CASTABLE_POWERS.has(power_id):
		power_cast_requested.emit(String(CASTABLE_POWERS[power_id]))
		_toggle_powers_palette()
		return
	power_purchase_requested.emit(power_id, int(button.get_meta("power_cost", 1)))


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
	if ui_font != null:
		for label in [resource_label, command_points_label, power_orb_label]:
			if label != null:
				(label as Label).add_theme_font_override("font", ui_font)
		if retail_tooltip != null:
			retail_tooltip.set_retail_font(ui_font)
	if retail_side_command_bar != null:
		retail_side_command_bar.bind_socket_texture(_atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION))
	minimap.bind_retail_parchment(_atlas_region(_retail_palantir_atlas, RETAIL_RADAR_PARCHMENT_REGION))
	for id in orb_buttons.keys():
		var orb := orb_buttons[id] as Button
		orb.icon = _atlas_region(_retail_palantir_atlas, RETAIL_ORB_REGIONS[id])
		orb.expand_icon = true
		orb.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	command_points_label.add_theme_color_override("font_color", Color("f1d06e"))
	var power_socket := StyleBoxTexture.new()
	power_socket.texture = _atlas_region(_retail_palantir_atlas, RETAIL_EMPTY_SOCKET_REGION)
	for index in RETAIL_POWER_IMAGE_IDS.size():
		var validation := _validate_retail_image(content_db, expected_pack_root, RETAIL_POWER_IMAGE_IDS[index], Vector2i(64, 64))
		if String(validation.get("error", "")) == "":
			power_buttons[index].icon = validation["texture"]
			for state in ["normal", "hover", "pressed", "disabled"]:
				power_buttons[index].add_theme_stylebox_override(state, power_socket)
			power_buttons[index].add_theme_constant_override("icon_max_width", 76)
	var shell := _atlas_region(retail_apt_runtime.exact_atlas_texture(RETAIL_PALANTIR_FRAME_ATLAS), Rect2(Vector2.ZERO, RETAIL_PALANTIR_FRAME_SOURCE_SIZE))
	var palette_shell := TextureRect.new()
	palette_shell.name = "RetailPowersFrame"
	palette_shell.texture = shell
	palette_shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	palette_shell.stretch_mode = TextureRect.STRETCH_SCALE
	palette_shell.mouse_filter = Control.MOUSE_FILTER_IGNORE
	powers_palette.add_child(palette_shell)
	powers_palette.move_child(palette_shell, 0)
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
	# Retail-style production queue: up to five clickable slots under the
	# palantir dish; clicking a queued item cancels it (retail behavior).
	if production_queue_buttons.is_empty():
		for index in 5:
			var queue_button := Button.new()
			queue_button.name = "QueueSlot%d" % index
			queue_button.position = Vector2(60 + index * 40, 344)
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
	resource_strip.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	var resource_row := resource_label.get_parent()
	resource_row.remove_child(resource_label)
	resource_row.remove_child(command_points_label)
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
	button.add_theme_constant_override("icon_max_width", int(RETAIL_COMMAND_SLOT_SIZE.x) - 16)
	button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
	button.mouse_entered.connect(func() -> void:
		ui_sound_requested.emit("Gui_UpgradeButtonGlow")
	)
	button.pressed.connect(func() -> void:
		ui_sound_requested.emit("Gui_PalantirCommandButtonClick")
	)


func _toggle_powers_palette() -> void:
	powers_palette.visible = not powers_palette.visible
	score_overlay.visible = false
	# The spellbook is its own screen; the control-bar cluster hides while
	# it is open so the palantir frame does not bleed through the backdrop.
	var cluster_visible := not powers_palette.visible
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
	if powers_palette.visible:
		powers_opened.emit()
	ui_sound_requested.emit("Gui_PalantirChoosePowerClick" if powers_palette.visible else "Gui_CloseSpellStoreClick")


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
	panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	panel.offset_left = -510
	panel.offset_top = -150
	panel.offset_right = -18
	panel.offset_bottom = -92
	panel.add_theme_stylebox_override("panel", _panel)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(panel)
	feedback_label = Label.new()
	feedback_label.name = "Feedback"
	feedback_label.text = "Select a blue battalion or Barracks."
	feedback_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	feedback_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.add_theme_color_override("font_color", Color("e6d28a"))
	panel.add_child(feedback_label)


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
	match_clock_label = Label.new()
	match_clock_label.name = "MatchClock"
	match_clock_label.text = "Game Time  00:00"
	match_clock_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	match_clock_label.add_theme_font_size_override("font_size", 18)
	match_clock_label.add_theme_color_override("font_color", Color("c8dbe4"))
	column.add_child(match_clock_label)
	music_slider = _add_slider(column, "Music", func(value: float) -> void: music_volume_changed.emit(value))
	voice_slider = _add_slider(column, "Voice / Sound FX", func(value: float) -> void: voice_volume_changed.emit(value))
	mute_toggle = CheckButton.new()
	mute_toggle.text = "Mute all audio"
	mute_toggle.toggled.connect(func(value: bool) -> void: mute_changed.emit(value))
	column.add_child(mute_toggle)
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
	command_cap_slider.max_value = 1000
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
	_add_action_button(column, "Resume", func() -> void: pause_requested.emit())
	_add_action_button(column, "Restart Battle", func() -> void: restart_requested.emit())
	_add_action_button(column, "Return to Main Menu", func() -> void: main_menu_requested.emit())
	_add_action_button(column, "Quit", func() -> void: quit_requested.emit())


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
	# Retail docks purchased powers on the right screen edge (above the side
	# command bar's builder column) so casts do not require reopening the
	# spellbook. Buttons appear here as powers are purchased.
	powers_dock = Control.new()
	powers_dock.name = "RetailPowersDock"
	powers_dock.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	powers_dock.mouse_filter = Control.MOUSE_FILTER_IGNORE
	powers_dock.z_index = 6
	add_child(powers_dock)


func _refresh_powers_dock(purchased: Array) -> void:
	if powers_dock == null:
		return
	var castable: Array = []
	for power_id in purchased:
		if CASTABLE_POWERS.has(String(power_id)):
			castable.append(String(power_id))
	# Drop buttons for powers no longer owned (match reset) before adding new.
	for existing_id in powers_dock_buttons.keys().duplicate():
		if not castable.has(existing_id):
			(powers_dock_buttons[existing_id] as Button).queue_free()
			powers_dock_buttons.erase(existing_id)
	var viewport := powers_dock.get_viewport_rect().size
	if viewport.x <= 0.0 or viewport.y <= 0.0:
		viewport = Vector2(1024.0, 768.0)
	var column_x := viewport.x - 60.0 - 14.0
	var start_y := 120.0 / 768.0 * viewport.y
	for order in castable.size():
		var power_id: String = castable[order]
		var button: Button = powers_dock_buttons.get(power_id)
		if button == null:
			button = Button.new()
			button.name = "PowerDock_%s" % power_id
			button.size = Vector2(60, 60)
			button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
			button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
			button.expand_icon = true
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			button.add_theme_constant_override("icon_max_width", 44)
			# Reuse the spellbook button's already-validated icon + socket art.
			for index in power_buttons.size():
				if String(power_buttons[index].get_meta("power_id", "")) != power_id:
					continue
				button.icon = power_buttons[index].icon
				for state in ["normal", "hover", "pressed", "disabled", "focus"]:
					button.add_theme_stylebox_override(state, power_buttons[index].get_theme_stylebox(state))
				break
			button.tooltip_text = "Cast — click, then left-click the battlefield (right-click cancels)"
			var cast_kind := String(CASTABLE_POWERS[power_id])
			button.pressed.connect(func() -> void:
				power_cast_requested.emit(cast_kind)
				ui_sound_requested.emit("Gui_PalantirChoosePowerClick")
			)
			_wire_button_feel(button)
			powers_dock.add_child(button)
			powers_dock_buttons[power_id] = button
		button.position = Vector2(column_x, start_y + float(order) * 70.0)


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


func _wire_retail_tooltips() -> void:
	for spec_value in RETAIL_COMMAND_SPECS:
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
	for spec_value in RETAIL_UNIT_ACTION_SPECS:
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
		power.set_meta("tooltip_power_id", String(RETAIL_POWER_IMAGE_IDS[index]))
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
	var timer := get_tree().create_timer(RETAIL_TOOLTIP_HOVER_DELAY)
	timer.timeout.connect(func() -> void:
		if _tooltip_hover_button == button and is_instance_valid(button):
			show_retail_tooltip(button)
	)


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
		String(content.get("description", ""))
	)
	var screen := get_viewport_rect().size
	var anchor := button.get_global_rect().get_center().x if button.is_inside_tree() else screen.x * 0.5
	var bar_top := screen.y - RETAIL_PALANTIR_FRAME_DISPLAY_SIZE.y
	if command_panel != null and command_panel.is_inside_tree():
		bar_top = minf(bar_top, command_panel.global_position.y)
	retail_tooltip.place_above_bar(anchor, bar_top, screen)


func _resolve_tooltip_content(button: Button) -> Dictionary:
	var group := String(button.get_meta("tooltip_group", ""))
	match group:
		"train":
			var unit_id := String(button.get_meta("tooltip_unit_id", ""))
			var fallback_label := String(button.get_meta("tooltip_fallback_label", ""))
			var fallback_desc := String(button.get_meta("tooltip_fallback_desc", ""))
			var desc := button.tooltip_text if button.tooltip_text != "" else fallback_desc
			return {
				"title": command_label(unit_id, fallback_label),
				"cost": int(_retail_command_costs.get(unit_id, -1)),
				"shortcut": "",
				"description": desc,
			}
		"action":
			var action_id := String(button.get_meta("tooltip_action_id", ""))
			var fallback_label := String(button.get_meta("tooltip_fallback_label", ""))
			var title := String(button.get_meta("retail_label", fallback_label))
			var cost := -1
			if action_id.begins_with("construct_"):
				cost = int(_retail_command_costs.get(action_id.trim_prefix("construct_"), -1))
			return {
				"title": title,
				"cost": cost,
				"shortcut": "",
				"description": button.tooltip_text,
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
			return {
				"title": _retail_power_title(power_id),
				"cost": -1,
				"shortcut": "",
				"description": "",
			}
		"side_build":
			var kind := String(button.get_meta("construct_kind", ""))
			return {
				"title": String(button.get_meta("tooltip_title", kind.capitalize())),
				"cost": int(_retail_command_costs.get(kind, -1)),
				"shortcut": "",
				"description": String(button.get_meta("tooltip_desc", "")),
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
		for spec_value in RETAIL_UNIT_ACTION_SPECS:
			var spec: Dictionary = spec_value
			var action_id := String(spec["action_id"])
			if not action_id.begins_with("construct_"):
				continue
			var kind := action_id.trim_prefix("construct_")
			if not available_construct_kinds.has(kind):
				continue
			var button: Button = unit_action_buttons.get(action_id)
			var title := kind.capitalize()
			var desc := ""
			if button != null:
				title = String(button.get_meta("retail_label", title))
				desc = button.tooltip_text
			constructs.append({
				"kind": kind,
				"icon": button.icon if button != null else null,
				"title": title,
				"description": desc,
			})
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
