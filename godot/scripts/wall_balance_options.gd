extends RefCounted

static func build(tabs: TabContainer, options_path := "user://wall_balance.cfg") -> void:
	var scroll := ScrollContainer.new()
	scroll.name = "Wall rules"
	tabs.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(rows)
	var hint := Label.new()
	hint.text = "BFME2Lua wall rebalance · Changes apply to the next match. Tower assets require restarting the app."
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows.add_child(hint)
	var config := ConfigFile.new()
	config.load("res://dev/wall_balance.cfg")
	var saved := ConfigFile.new()
	if saved.load(options_path) == OK: config = saved
	for entry in [["enabled","Enable wall rebalance",true], ["new_towers","Elven and Isengard wall arrow towers",true], ["intermediate_hubs","Intermediate wall hubs",false], ["share_arrows","Share fortress arrow upgrades with towers",true], ["starting_walls","Begin with completed walls and front/back gates",false], ["require_enclosed","Require enclosure for non-resource buildings (human players)",true], ["gate_gap","Require native clearance beside gates",false]]:
		var check := CheckBox.new()
		check.text = entry[1]
		check.button_pressed = config.get_value("walls",entry[0],entry[2])
		check.toggled.connect(func(value: bool): config.set_value("walls",entry[0],value))
		rows.add_child(check)
	for entry in [["wall_damage","Wall damage"],["fortress_damage","Fortress damage"]]:
		var row := HBoxContainer.new()
		rows.add_child(row)
		var label := Label.new()
		label.text = entry[1]
		row.add_child(label)
		var options := OptionButton.new()
		for title in ["Native", "Wall-capable attackers only", "Siege engines only"]: options.add_item(title)
		options.select(int(config.get_value("walls",entry[0],1)))
		options.item_selected.connect(func(index: int): config.set_value("walls",entry[0],index))
		row.add_child(options)
	for entry in [["enclosed_income","Enclosed resource income",2.0,1.0,10.0], ["starting_radius","Starting wall radius (map units)",450.0,150.0,1500.0], ["starting_spacing","Starting hub spacing (map units)",300.0,100.0,1000.0], ["wall_health","Wall health",6.0,0.01,10000.0],["hub_health","Hub health",30.0,0.01,10000.0],["gate_health","Gate health",6.0,0.01,10000.0],["wall_cost","Wall cost",0.5,0.0,100.0],["hub_cost","Run hub cost",0.7,0.0,100.0],["outer_hub_cost","Standalone hub cost",0.3,0.0,100.0],["expansion_cost","Wall expansion cost",0.8,0.0,100.0],["fortress_range","Fortress wall range",2.0,0.01,100.0],["hub_range","Standalone hub wall range",2.0,0.01,100.0]]:
		var row := HBoxContainer.new()
		rows.add_child(row)
		var label := Label.new()
		label.text = entry[1] + ("" if str(entry[0]).begins_with("starting_") else " multiplier")
		label.custom_minimum_size.x = 280
		row.add_child(label)
		var value := SpinBox.new()
		value.min_value = entry[3]
		value.max_value = entry[4]
		value.step = 0.05
		value.value = config.get_value("walls",entry[0],entry[2])
		value.value_changed.connect(func(number: float): config.set_value("walls",entry[0],number))
		row.add_child(value)
	var save := Button.new()
	save.text = "Save wall rules"
	save.pressed.connect(func():
		var error := config.save(options_path)
		hint.text = "Saved. Applies to the next match; restart the app for tower changes." if error == OK else "Could not save wall rules: " + error_string(error))
	rows.add_child(save)
