// OpenBFME. GPL-3.0.
// See HudInput.h.

#include "GameClient/HudInput.h"

#include "Common/Player.h"
#include "GameLogic/Object/Object.h"

HudInput::HudInput(GameLogic &logic, AIWorld *ai, TacticalView &view, CommandList &commands, const MouseSettings &mouse, const MetaMap &metaMap)
	: m_logic(logic)
	, m_commands(commands)
	, m_ctx{ logic, ai, view, m_ui, m_stream, nullptr, mouse }
	, m_meta(m_ctx, metaMap)
	, m_place(m_ctx)
	, m_guiCommand(m_ctx)
	, m_command(m_ctx)
	, m_selection(m_ctx, m_command)
{
	m_ui.setMessageStream(&m_stream);
	m_stream.attachTranslator(&m_meta, 20);
	m_stream.attachTranslator(&m_place, 30); // BUILD-1 (ZH PlaceEventTranslator)
	m_stream.attachTranslator(&m_guiCommand, 40);
	m_stream.attachTranslator(&m_selection, 50);
	m_stream.attachTranslator(&m_command, 70);
}

void HudInput::mouseMove(int x, int y, int keyState)
{
	ClientMessage &m = m_stream.append(CMSG_RAW_MOUSE_POSITION);
	m.appendPixel({ x, y });
	m.appendInteger(keyState);
}

void HudInput::mouseButton(Button button, bool down, int x, int y, int keyState, int timeMs, bool doubleClick, bool overGui)
{
	bool *ignoreUp = button == Button::Left ? &m_ignoreLeftUp : button == Button::Right ? &m_ignoreRightUp : &m_ignoreMiddleUp;
	if (down && overGui)
	{
		*ignoreUp = true; // the press belongs to the GUI: so does the release
		return;
	}
	if (!down && *ignoreUp)
	{
		*ignoreUp = false;
		return;
	}
	auto add = [&](int type) {
		ClientMessage &m = m_stream.append(type);
		m.appendPixel({ x, y });
		m.appendInteger(keyState);
		m.appendInteger(timeMs);
	};
	if (down)
	{
		const int downType = button == Button::Left ? CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN : button == Button::Right ? CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN : CMSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN;
		add(downType);
		if (doubleClick)
		{
			add(button == Button::Left ? CMSG_RAW_MOUSE_LEFT_DOUBLE_CLICK : button == Button::Right ? CMSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK : CMSG_RAW_MOUSE_MIDDLE_DOUBLE_CLICK);
		}
	}
	else
	{
		add(button == Button::Left ? CMSG_RAW_MOUSE_LEFT_BUTTON_UP : button == Button::Right ? CMSG_RAW_MOUSE_RIGHT_BUTTON_UP : CMSG_RAW_MOUSE_MIDDLE_BUTTON_UP);
	}
}

void HudInput::key(int key, int keyState)
{
	ClientMessage &m = m_stream.append((keyState & KEY_STATE_UP) ? CMSG_RAW_KEY_UP : CMSG_RAW_KEY_DOWN);
	m.appendInteger(key);
	m.appendInteger(keyState);
}

void HudInput::mouseWheel(int spin, int x, int y)
{
	ClientMessage &m = m_stream.append(CMSG_RAW_MOUSE_WHEEL);
	m.appendPixel({ x, y });
	m.appendInteger(spin);
}

void HudInput::attachCamera(TacticalCamera &camera)
{
	m_camera = &camera;
	m_lookAt = std::make_unique<LookAtTranslator>(m_ctx, camera);
	m_stream.attachTranslator(m_lookAt.get(), 60); // ZH GameClient.cpp: LookAtTranslator 60 (RW 0x646BC6 pushes 0x3C)
}

void HudInput::cameraFrame(unsigned nowMs, bool gamePaused)
{
	if (!m_camera)
	{
		return;
	}
	m_lookAt->tick(nowMs);
	m_camera->update(m_ui.isScrolling(), gamePaused);
	m_camera->commitFrame();
}

size_t HudInput::update()
{
	// objects that died leave the selection mirror (ZH: the drawable's destructor deselects)
	std::vector<ObjectID> gone;
	for (ObjectID id : m_ui.selected())
	{
		Object *o = m_logic.findObjectByID(id);
		if (!o || o->isDestroyed())
		{
			gone.push_back(id);
		}
	}
	for (ObjectID id : gone)
	{
		m_ui.forgetObject(id);
	}
	m_place.frame(); // lane QA2-FIX: the placement update's line build step (RW 0x6A2D12) runs in InGameUI::update, before the stream (ZH GameEngine::update)
	const int local = m_ctx.localPlayer() ? m_ctx.localPlayer()->getPlayerIndex() : 0;
	std::vector<ClientMessage> out = m_stream.propagate();
	size_t n = 0;
	for (const ClientMessage &m : out)
	{
		if (m.isLogic())
		{
			GameMessage msg = toGameMessage(m, local);
			if (m_observer)
			{
				m_observer(msg);
			}
			m_commands.append(std::move(msg));
			++n;
		}
	}
	m_sent += n;
	return n;
}

std::vector<std::string> HudInput::acceptanceStops()
{
	std::vector<std::string> out = {
		"[S-280] translator pipeline: the translator priorities are ZH's (MetaEvent 20, PlaceEvent 30 (BUILD-1), GUICommand 40, Selection 50, Command 70; HotKey is not ported; LookAt 60 is lane CAM-1's, attached by attachCamera), the RotWK 2.01 "
		"translators were not read; a drag selection does not emit MSG_AREA_SELECTION (GameMessage has no pixel-region argument; the logic does not act on the message); MSG_ADD_TO_TEAM0..9 (1138 .. 1147) "
		"is not generated and stays unhandled by the dispatcher (S-208)",
		"[S-281] CommandMap: the block grammar is ZH's MetaMap table (Key, Transition, Modifiers, UseableIn, Category, Description, DisplayName); the RotWK field table and the INI load order "
		"(Data\\INI\\CommandMap.ini then the language's CommandMap.ini) were not read; the team message bodies (create / select / add) are ZH's (Player::processCreateTeamGameMessage ...)",
		"[S-282] tactical view: without HudInput::attachCamera the view is a PinholeView the device fills in (a stand-in with no limits); the retail camera, its translator and "
		"its stops are lane CAM-1's (TacticalCamera::acceptanceStops(), S-450 .. S-459)",
		"[S-283] InGameUI: only the state the translators and the control bar read is ported; radius decals, subtitles, floating text, superweapon timers, popups and the "
		"alternate-mouse bookkeeping are not (the building placement is BUILD-1's, S-306)",
		"[S-284] picking: without a drawable ray test (HudContext::pickRay, installed by InGameHud: S-1200) an object is hit by a ray against its Geometry volume (cylinder of the bounding "
		"radius, height at least the radius), the nearest first; ZH tests the model's polygons; a region holds the objects whose position projects into it",
		"[S-285] mouse setup: the retail default of the AlternateMouseSetup option (which button orders) was not read; CommandTranslator::setUseAlternateMouse selects it, false is ZH's default",
		"[S-286] context commands: resume construction, dock, repair, heal, capture / hijack / sabotage / salvage / snipe, combat drop, special powers, weapon fire commands, unit voice "
		"responses and the academy statistics are not ported; an attack is issued against an enemy when a selected object (or a member of a selected horde) can possibly have a weapon (RW 0x73C191), standing "
		"for ActionManager::canAttackObject; a click on a container a selected object may enter is MSG_ENTER (lane GARRISON-1, ActionManager::canEnterObject; the order of the context checks is not read); select-all takes every mobile object of the player across the map",
		"[S-287] meta commands: every meta command RotWK's handler (RW 0x81F8D8) executes is ported (select matching units and view home base: S-1202; select hero, the "
		"stance keys and view last radar event: S-1673 / S-1674, lane HUD-4); select next / previous unit and worker have no case in RotWK and do nothing (S-1673); "
		"CommandTranslator::unportedMeta() counts what remains",

		"[S-288] selection filters: the shroud (VIS-1) is not applied to what a pick or a drag may select (an enemy's invisible, undetected object is not picked: lane STEALTH-1, "
		"InvisibilityManager::clientLook); the double click selects the matching units of the screen by template identity, across the map with the alt key",
	};
	for (const std::string &s : PlaceEventTranslator::stopLines()) // lane QA2-FIX
	{
		out.push_back(s);
	}
	return out;
}

std::vector<std::string> HudInput::stops() const
{
	std::vector<std::string> out = acceptanceStops();
	out.push_back("[S-1673] hotkeys (HUD-4): Ctrl+H walks the local player's objects for the next HERO after the selected one and selects it (RW 0x81FDA2 / 0x81DABC; "
				  "INFERENCE: the player's team list is its prototypes in creation order, the gate RW 0x81FDA8 is a local player, no drawable test); D / F / G send "
				  "MSG_CHANGE_STANCE 2 / 1 / 3 (RW 0x8201F5 .. 0x820247); Shift+Up / Shift+Down (and the unit cycle keys) have no handler in RotWK 2.01 and pass "
				  "unexecuted, as there");
	if (m_camera)
	{
		for (const std::string &s : TacticalCamera::acceptanceStops())
		{
			out.push_back(s);
		}
	}
	for (const auto &kv : m_command.unportedMeta())
	{
		out.push_back("[S-287] met: " + kv.first + " x" + std::to_string(kv.second));
	}
	for (const auto &kv : m_command.retailNoOpMeta())
	{
		out.push_back("[S-1673] met (no retail handler): " + kv.first + " x" + std::to_string(kv.second));
	}
	return out;
}
