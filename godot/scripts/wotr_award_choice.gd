# OpenBFME. GPL-3.0.
# The port's territory-award choice UI. Simulation rules and authorization live
# in LivingWorldLogic (RW 0x900E58 / BFME2 RegionAwardDispute.cpp RVA 0x4FBED6).
# Native controls expose the verified choice; retail APT presentation remains S-1334.
extends RefCounted

var dialog: AcceptDialog
var shown_region := -1
var sent := false


func sync(war: Node) -> void:
	var pending: Dictionary = {}
	if war.active and not war.in_battle():
		for d: Dictionary in war.state.get("region_award_disputes", []):
			if int(d.resolver) == war.local_player:
				pending = d
				break
	if pending.is_empty():
		if is_instance_valid(dialog):
			dialog.queue_free()
			dialog = null
		shown_region = -1
		sent = false
		return
	if int(pending.region) == shown_region and is_instance_valid(dialog):
		if not dialog.visible:
			dialog.popup_centered(Vector2i(420, 180))
		return
	if is_instance_valid(dialog):
		dialog.queue_free()
	shown_region = int(pending.region)
	sent = false
	dialog = AcceptDialog.new()
	dialog.title = "Award territory"
	dialog.exclusive = true
	dialog.dialog_hide_on_ok = false
	dialog.get_ok_button().hide()
	war.add_child(dialog)
	var choices := VBoxContainer.new()
	dialog.add_child(choices)
	var label := Label.new()
	label.text = "Choose who receives %s" % str(war.regions[shown_region].name)
	choices.add_child(label)
	for claimant: int in pending.players:
		var button := Button.new()
		button.text = str(war.state.players[claimant].name)
		choices.add_child(button)
		button.pressed.connect(func() -> void:
			if sent:
				return
			var result: Dictionary = war.lw.resolve_region_award(shown_region, claimant)
			if result.ok:
				sent = true
				war._run_logic()
			else:
				label.text = str(result.error)
		)
	dialog.popup_centered(Vector2i(420, 180))
