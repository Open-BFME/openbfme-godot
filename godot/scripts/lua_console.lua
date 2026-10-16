-- BFME-style companion console, inspired by BFME2Lua's Command / Log editor. Lane LUAGD-1: written in Lua, as a lua-gdextension
-- script class (a CanvasLayer); game.gd and tests/lua_console_test.gd use it like the GDScript console it replaces:
--   execute (Callable: code -> {ok, output}), history_path, controller_options_path, signal toggled(open),
--   toggle(), is_open(), reload_scripts(), is_toggle_event(event)
-- The console UI runs in lua-gdextension's script state; the commands it sends go to the bfme.* state through `execute`.
local LuaConsole = {
	extends = CanvasLayer,
	toggled = signal("open"),
	execute = property { type = Callable },
	history_path = "user://console_history.cfg",
	controller_options_path = "user://controller_options.cfg",
}

local GOLD = Color("c4a76a")
local INK = Color("10191e")
local TEXT = Color("e3dfcf")
local MUTED = Color("99aaa9")
local ERROR_TEXT = Color("e8a18b")
local UI_SCALE = 2.0
local HISTORY_LIMIT = 256

-- Lua strings are bytes; the text editors count characters
local function ulen(s)
	local _, n = s:gsub("[^\128-\191]", "")
	return n
end

local function trim(s)
	return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function box(color, border, padding)
	local b = StyleBoxFlat:new()
	b.bg_color = color
	b.border_color = border
	b:set_border_width_all(1)
	b:set_content_margin_all(padding or 10)
	b:set_corner_radius_all(2)
	return b
end

local function label(text, color, font_size)
	local l = Label:new()
	l.text = text
	l:add_theme_color_override("font_color", color or TEXT)
	l:add_theme_font_size_override("font_size", font_size or 14)
	return l
end

local function button(text, action)
	local b = Button:new()
	b.text = text
	b.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	b.pressed:connect(Callable(action))
	b.focus_mode = Control.FOCUS_NONE
	return b
end

local function text_view()
	local view = TextEdit:new()
	view.editable = false
	view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	view.custom_minimum_size = Vector2(0, 85)
	view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view:add_theme_font_size_override("font_size", 14)
	return view
end

local function spin_box(name, min, max, step, value, width)
	local s = SpinBox:new()
	s.name = name
	s.min_value = min
	s.max_value = max
	s.step = step
	s.value = value
	if width then s.custom_minimum_size = Vector2(width, 0) end
	return s
end

local function flow(parent)
	local row = HFlowContainer:new()
	parent:add_child(row)
	return row
end

local function set_min_height(control, height)
	control.custom_minimum_size = Vector2(control.custom_minimum_size.x, height)
end

-- the gold corner ornaments of the frame (the panel's draw signal)
local function draw_ornaments(panel)
	local size = panel.size
	for _, corner in ipairs { Vector2(9, 9), Vector2(size.x - 9, 9), Vector2(9, size.y - 9), Vector2(size.x - 9, size.y - 9) } do
		local direction = Vector2(corner.x < size.x / 2 and 1 or -1, corner.y < size.y / 2 and 1 or -1)
		panel:draw_line(corner, corner + Vector2(25 * direction.x, 0), GOLD, 2)
		panel:draw_line(corner, corner + Vector2(0, 25 * direction.y), GOLD, 2)
		local center = corner + direction * 5
		panel:draw_colored_polygon(PackedVector2Array(Array {
			center + Vector2(0, -3), center + Vector2(3, 0), center + Vector2(0, 3), center + Vector2(-3, 0) }), GOLD)
	end
end

local function console_theme()
	local theme = Theme:new()
	local serif = SystemFont:new()
	serif.font_names = PackedStringArray(Array { "Palatino", "Georgia", "DejaVu Serif" })
	serif.oversampling = UI_SCALE
	theme.default_font = serif
	theme.default_font_size = 15
	theme:set_color("font_color", "Label", TEXT)
	for _, state in ipairs { "normal", "hover", "pressed", "focus" } do
		theme:set_stylebox(state, "Button", box(state == "hover" and Color("28383c") or Color("19272d"), state ~= "normal" and GOLD or Color("655f48"), 8))
		theme:set_color(state == "normal" and "font_color" or ("font_" .. state .. "_color"), "Button", TEXT)
	end
	local mono = SystemFont:new()
	mono.font_names = PackedStringArray(Array { "Menlo", "Consolas", "DejaVu Sans Mono" })
	mono.oversampling = UI_SCALE
	for _, type in ipairs { "TextEdit", "CodeEdit" } do
		theme:set_font("font", type, mono)
		theme:set_color("font_color", type, TEXT)
		theme:set_color("font_readonly_color", type, TEXT)
		theme:set_color("caret_color", type, GOLD)
		theme:set_color("selection_color", type, Color("415c65"))
		theme:set_stylebox("normal", type, box(Color("0c1418"), Color("425252")))
		theme:set_stylebox("read_only", type, box(Color("101b20"), Color("354648")))
		theme:set_stylebox("focus", type, box(Color(0, 0, 0, 0), GOLD, 0))
	end
	theme:set_stylebox("panel", "TabContainer", box(Color("142126"), Color("6d674e")))
	theme:set_stylebox("tab_selected", "TabContainer", box(Color("26383b"), GOLD, 9))
	theme:set_stylebox("tab_unselected", "TabContainer", box(Color("142126"), Color("465352"), 9))
	theme:set_color("font_selected_color", "TabContainer", GOLD)
	theme:set_color("font_unselected_color", "TabContainer", MUTED)
	return theme
end

local function lua_highlighter()
	local syntax = CodeHighlighter:new()
	syntax.number_color = Color("d5b875")
	syntax.symbol_color = Color("b2c4c5")
	syntax.function_color = Color("a3c6ce")
	syntax.member_variable_color = TEXT
	for _, keyword in ipairs { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
		"nil", "not", "or", "repeat", "return", "then", "true", "until", "while" } do
		syntax:add_keyword_color(keyword, Color("d1ad73"))
	end
	syntax:add_color_region('"', '"', Color("9cc599"))
	syntax:add_color_region("'", "'", Color("9cc599"))
	syntax:add_color_region("--[[", "]]", Color("748c89"))
	syntax:add_color_region("--", "", Color("748c89"), true)
	return syntax
end

function LuaConsole:_ready()
	self._history = Array()
	self._history_index = 0
	self._draft = ""
	self._layout_queued = false
	self:_load_history()
	self.layer = 100
	local root = Control:new()
	root:set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	self:add_child(root)
	self._root = root
	local veil = ColorRect:new()
	veil.color = Color(0.015, 0.025, 0.03, 0.66)
	veil:set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root:add_child(veil)
	local panel = PanelContainer:new()
	root:add_child(panel)
	self._panel = panel
	panel.draw:connect(Callable(function() draw_ornaments(panel) end))
	panel.minimum_size_changed:connect(Callable(self, "_request_layout"))
	panel.theme = console_theme()
	local frame = box(INK, GOLD, 22)
	frame.shadow_color = Color(0, 0, 0, 0.7)
	frame.shadow_size = 20
	panel:add_theme_stylebox_override("panel", frame)

	local chrome = VBoxContainer:new()
	chrome:add_theme_constant_override("separation", 10)
	panel:add_child(chrome)
	local scroll = ScrollContainer:new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	chrome:add_child(scroll)
	local rows = VBoxContainer:new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows:add_theme_constant_override("separation", 10)
	scroll:add_child(rows)
	local header = HBoxContainer:new()
	rows:add_child(header)
	local title = label("LUA CONSOLE", GOLD, 25)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header:add_child(title)
	header:add_child(button("Close  `", function() self:toggle() end))
	rows:add_child(label("P A L A N T Í R   /   S C R I P T I N G", MUTED, 11))

	local tabs = TabContainer:new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows:add_child(tabs)
	self._tabs = tabs
	local command = VBoxContainer:new()
	command.name = "Command"
	command:add_theme_constant_override("separation", 8)
	tabs:add_child(command)
	self._output = text_view()
	command:add_child(self._output)
	local editor_heading = HBoxContainer:new()
	editor_heading:add_child(label("SCRIPT", GOLD, 12))
	local editor_hint = label("Enter to run · Shift+Enter for a new line", MUTED, 12)
	editor_hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	editor_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	editor_heading:add_child(editor_hint)
	command:add_child(editor_heading)
	local entry = CodeEdit:new()
	entry.placeholder_text = "bfme.players.localPlayer.money = 10000"
	entry.custom_minimum_size = Vector2(0, 110)
	entry.size_flags_vertical = Control.SIZE_EXPAND_FILL
	entry.size_flags_stretch_ratio = 0.5
	entry.gutters_draw_line_numbers = true
	entry.indent_size = 4
	entry.indent_use_spaces = true
	entry:add_theme_font_size_override("font_size", 14)
	entry:add_theme_color_override("font_color", TEXT)
	entry.syntax_highlighter = lua_highlighter()
	command:add_child(entry)
	entry.gui_input:connect(Callable(self, "_entry_input"))
	self._entry = entry
	local log = text_view()
	log.name = "Log"
	tabs:add_child(log)
	self._log = log
	self:_build_cheats()
	self:_build_controller_options()
	local wall_rules = ResourceLoader:load("res://scripts/wall_balance_options.gd")
	print("DBG wall", wall_rules, wall_rules and wall_rules:get_class())
	wall_rules:call("build", tabs)
	print("DBG built")
	tabs.tab_changed:connect(Callable(function(_index)
		if tabs.current_tab == 0 then entry:grab_focus() end
	end))

	local toolbar = HFlowContainer:new()
	toolbar:add_theme_constant_override("separation", 8)
	chrome:add_child(toolbar)
	self._status = label("READY · LuaJIT", MUTED, 12)
	self._status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar:add_child(self._status)
	toolbar:add_child(button("Reload scripts", function() self:reload_scripts() end))
	toolbar:add_child(button("Clear", function() self:_clear_visible() end))
	toolbar:add_child(button("Run script", function()
		tabs.current_tab = 0
		self:_submit(entry.text)
	end))
	local footer = label("` toggle · Esc close · ↑ / ↓ history · Ctrl+Shift+F5 reload", MUTED, 12)
	footer.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	chrome:add_child(footer)
	self:_append("BFME scripting console\nInspect the realm, shape the battle.\n\nTry  bfme.match.getStatus()  or  bfme.players.localPlayer.money\n")
	self:get_viewport().size_changed:connect(Callable(self, "_layout"))
	self:_layout()
	root:hide()
end

function LuaConsole:_build_cheats()
	local scroll = ScrollContainer:new()
	scroll.name = "Cheats"
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	self._tabs:add_child(scroll)
	local rows = VBoxContainer:new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows:add_theme_constant_override("separation", 10)
	scroll:add_child(rows)
	rows:add_child(label("SHAPE THE BATTLE", GOLD, 16))
	local hint = label("Applies to the current match. Results appear in Log.", MUTED, 12)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows:add_child(hint)
	local local_player = "assert(bfme.players.localPlayer, 'No local player in this match')"
	local gold_row = flow(rows)
	gold_row:add_child(label("Local player gold"))
	local gold = spin_box("GoldAmount", 0, 2147483647, 100, 100000, 150)
	gold_row:add_child(gold)
	gold_row:add_child(button("Set gold", function()
		self:_submit(string.format("%s.money = %d", local_player, math.floor(gold.value)))
	end))
	local quick = flow(rows)
	quick:add_child(button("+100k gold", function()
		self:_submit("local p=" .. local_player .. "; p.money=math.min(2147483647,p.money+100000)")
	end))
	quick:add_child(button("+100k CP", function()
		self:_submit("local p=" .. local_player .. "; p.commandPoints=math.min(1000000,p.commandPoints+100000)")
	end))
	local visibility = flow(rows)
	visibility:add_child(button("Reveal map", function() self:_submit(local_player .. ":revealMap()") end))
	visibility:add_child(button("Disable fog of war", function() self:_submit(local_player .. ":setFogOfWar(false)") end))
	visibility:add_child(button("Restore fog of war", function() self:_submit(local_player .. ":setFogOfWar(true)") end))
	local speed_row = flow(rows)
	speed_row:add_child(label("Game speed"))
	local speed = spin_box("GameSpeed", 0, 100, 0.25, 1, 110)
	speed_row:add_child(speed)
	speed_row:add_child(button("Apply speed", function()
		self:_submit("bfme.match.setGameSpeed(" .. tostring(speed.value) .. ")")
	end))
	rows:add_child(label("0 stops time · 1 normal · 2 twice as fast · max 100", MUTED, 12))
	local actions = flow(rows)
	actions:add_child(button("Normal speed", function()
		speed.value = 1
		self:_submit("bfme.match.setGameSpeed(1)")
	end))
	actions:add_child(button("Pause", function() self:_submit("bfme.match.pause()") end))
	actions:add_child(button("Resume", function() self:_submit("bfme.match.unpause()") end))
	-- lane LUAAI-1: the computer players to the Lua skirmish AI (godot/dev/05_skirmish_ai.lua) and back
	local ai_row = flow(rows)
	ai_row:add_child(button("Computers: Lua AI", function() self:_submit("assert(luaai,'Reload scripts first'); return luaai.enableAll()") end))
	ai_row:add_child(button("Computers: native AI", function() self:_submit("assert(luaai,'Reload scripts first'); luaai.disableAll()") end))
	ai_row:add_child(button("Lua AI status", function() self:_submit("assert(luaai,'Reload scripts first'); return luaai.status()") end))
end

function LuaConsole:_build_controller_options()
	local rows = VBoxContainer:new()
	rows.name = "Controller"
	rows:add_theme_constant_override("separation", 12)
	self._tabs:add_child(rows)
	rows:add_child(label("CONTROLLER OPTIONS", GOLD, 16))
	local saved = ConfigFile:new()
	local loaded = saved:load(self.controller_options_path) == OK
	local function save(key, value)
		saved:set_value("controller", key, value)
		local err = saved:save(self.controller_options_path)
		if err ~= OK then
			self:_append("Could not save controller options: " .. error_string(err) .. "\n")
		end
	end
	local row = flow(rows)
	row:add_child(label("Selection assistance radius"))
	local radius = spin_box("SelectionAssistRadius", 0, 200, 1, 48)
	radius.suffix = " px"
	if loaded then radius.value = saved:get_value("controller", "selection_assist_radius", 48) end
	row:add_child(radius)
	row:add_child(button("Apply radius", function()
		local value = math.floor(radius.value)
		self:_submit(string.format("controller.config.selectionAssistRadius = %d", value))
		save("selection_assist_radius", value)
	end))
	local grid_row = flow(rows)
	grid_row:add_child(label("Placement grid spacing"))
	local grid = spin_box("PlacementGridSpacing", 10, 200, 5, saved:get_value("controller", "placement_grid_spacing", 50))
	grid.suffix = " map units"
	grid_row:add_child(grid)
	grid_row:add_child(button("Apply grid", function()
		local value = math.floor(grid.value)
		self:_submit(string.format("controller.config.placementGridSpacing = %d", value))
		save("placement_grid_spacing", value)
	end))
	rows:add_child(label("Hold LT or Shift during placement to show the grid and snap position and rotation (45°).", MUTED, 12))
	local hint = label("A selects the nearest friendly, unselected unit when nothing is directly under the reticle. Radius scales from 720p. Set 0 to disable. Saved between sessions.", MUTED, 12)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows:add_child(hint)
end

function LuaConsole:_layout()
	local viewport_size = self:get_viewport():get_visible_rect().size
	local logical = viewport_size / UI_SCALE
	local compact = logical.y < 600
	set_min_height(self._entry, compact and 70 or 110)
	set_min_height(self._output, compact and 40 or 85)
	set_min_height(self._log, compact and 40 or 85)
	local panel = self._panel
	panel.scale = Vector2(UI_SCALE, UI_SCALE)
	panel.size = Vector2(math.min(1060, logical.x - 16), math.min(740, logical.y - 16))
	panel.position = (viewport_size - panel.size * UI_SCALE) / 2
	panel:queue_redraw()
end

function LuaConsole:_request_layout()
	if not self._layout_queued then
		self._layout_queued = true
		self:call_deferred("_settle_layout")
	end
end

function LuaConsole:_settle_layout()
	self._layout_queued = false
	self:_layout()
end

-- backtick (no modifier) or Ctrl+Shift+C
function LuaConsole:is_toggle_event(event)
	if event == nil or not event:is_class("InputEventKey") then
		return false
	end
	local backtick = event.keycode == KEY_QUOTELEFT or event.physical_keycode == KEY_QUOTELEFT
	local plain = not event.shift_pressed and not event.ctrl_pressed and not event.alt_pressed and not event.meta_pressed
	return (backtick and plain) or (event.keycode == KEY_C and event.ctrl_pressed and event.shift_pressed and not event.alt_pressed and not event.meta_pressed)
end

function LuaConsole:_input(event)
	local key = event:is_class("InputEventKey")
	if key and event.keycode == KEY_F5 and event.ctrl_pressed and event.shift_pressed then
		if event.pressed and not event.echo then self:reload_scripts() end
		self:get_viewport():set_input_as_handled()
	elseif self:is_toggle_event(event) then
		if event.pressed and not event.echo then self:toggle() end
		self:get_viewport():set_input_as_handled()
	elseif self:is_open() and key and event.keycode == KEY_ESCAPE then
		if event.pressed and not event.echo then self:toggle() end
		self:get_viewport():set_input_as_handled()
	end
end

function LuaConsole:is_open()
	return self._root ~= nil and self._root.visible
end

function LuaConsole:toggle()
	local root = self._root
	root.visible = not root.visible
	if root.visible then
		self:_request_layout()
		if self._tabs.current_tab == 0 then self._entry:grab_focus() end
	else
		self._entry:release_focus()
	end
	self.toggled:emit(root.visible)
end

-- Append through the editor API so selecting / copying old results stays undisturbed.
function LuaConsole:_write(view, text)
	local last = view:get_line_count() - 1
	view:insert_text(text, last, ulen(view:get_line(last)))
	local all = view.text
	if #all > 200000 then
		local start = #all - 160000 + 1
		while start <= #all and all:byte(start) >= 128 and all:byte(start) < 192 do start = start + 1 end -- a whole UTF-8 character
		view.text = all:sub(start)
	end
	-- Wrapped text updates the scroll range after insertion: read the new range once that update has run, even if the user had
	-- scrolled up before the append
	Callable(function()
		if is_instance_valid(view) then view:set_v_scroll(view:get_v_scroll_bar().max_value) end
	end):call_deferred()
end

function LuaConsole:_append(text)
	self:_write(self._output, text)
	self:_write(self._log, text)
end

function LuaConsole:_clear_visible()
	if self._tabs.current_tab == 0 then
		self._output.text = ""
		self._entry:grab_focus()
	else
		self._log.text = ""
		self._log:grab_focus()
	end
end

function LuaConsole:_load_history()
	local config = ConfigFile:new()
	local err = config:load(self.history_path)
	if err ~= OK and err ~= ERR_FILE_NOT_FOUND then
		push_warning("Could not load console history: " .. error_string(err))
	end
	local history = Array()
	if err == OK then
		local commands = config:get_value("console", "history", Array())
		if type(commands) == "userdata" and commands:is(Array) then
			for i = 0, #commands - 1 do
				local command = commands[i]
				if type(command) == "string" and trim(command) ~= "" then history:append(command) end
			end
			if #history > HISTORY_LIMIT then history = history:slice(#history - HISTORY_LIMIT) end
		end
	end
	self._history = history
	self._history_index = #history
end

function LuaConsole:_save_history()
	local config = ConfigFile:new()
	config:set_value("console", "history", self._history)
	local err = config:save(self.history_path)
	if err ~= OK then push_warning("Could not save console history: " .. error_string(err)) end
end

function LuaConsole:_set_status(text, ok)
	self._status.text = text
	self._status:add_theme_color_override("font_color", ok and MUTED or ERROR_TEXT)
end

function LuaConsole:_submit(code)
	if trim(code) == "" then return end
	local history = self._history
	if #history == 0 or history[#history - 1] ~= code then
		history:append(code)
		if #history > HISTORY_LIMIT then history:pop_front() end
		self:_save_history()
	end
	self._history_index = #history
	self._draft = ""
	self._entry:clear()
	if trim(code) == "clear" then
		self:_clear_visible()
		return
	end
	self:_append("\n› " .. code .. "\n")
	local result = self.execute:call(code)
	local output = tostring(result:get("output", ""))
	local ok = result:get("ok", false) == true
	if output ~= "" then
		self:_append(output .. (output:sub(-1) == "\n" and "" or "\n"))
	elseif not ok then
		self:_append("Command failed.\n")
	end
	self:_set_status(ok and "READY · command complete" or "ERROR · see output", ok)
	if self._tabs.current_tab == 0 then self._entry:grab_focus() end
end

function LuaConsole:reload_scripts()
	self:_append("\n› Reload scripts\n")
	local result = self.execute:call("bfme.console.reload()")
	local ok = result:get("ok", false) == true
	local output = tostring(result:get("output", ""))
	self:_append(output .. (output:sub(-1) == "\n" and "" or "\n"))
	self:_set_status(ok and "READY · scripts reloaded" or "RELOAD ERROR · see output", ok)
end

function LuaConsole:_entry_input(event)
	if not event:is_class("InputEventKey") or not event.pressed then return end
	local entry = self._entry
	if event.keycode == KEY_ENTER or event.keycode == KEY_KP_ENTER then
		if not event.shift_pressed then
			entry:accept_event()
			if not event.echo then self:_submit(entry.text) end
		end
		return
	end
	if event.keycode ~= KEY_UP and event.keycode ~= KEY_DOWN then return end
	-- Keep normal multiline cursor movement away from the first / last editor line.
	if event.keycode == KEY_UP and entry:get_caret_line() ~= 0 then return end
	if event.keycode == KEY_DOWN and entry:get_caret_line() ~= entry:get_line_count() - 1 then return end
	local history = self._history
	if self._history_index == #history then self._draft = entry.text end
	self._history_index = math.max(0, math.min(#history, self._history_index + (event.keycode == KEY_UP and -1 or 1)))
	entry.text = self._history_index == #history and self._draft or history[self._history_index]
	local last = entry:get_line_count() - 1
	entry:set_caret_line(last)
	entry:set_caret_column(ulen(entry:get_line(last)))
	entry:accept_event()
end

return LuaConsole
