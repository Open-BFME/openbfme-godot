// OpenBFME. GPL-3.0.
// See HudInput.h.

#include "GameClient/HudInput.h"
#include "GameClient/FormationAssistant.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameClient/HudObjects.h"

#include "Common/Thing/ThingTemplate.h"

#include <stdexcept>
#include <string>
#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/Player.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/SimMath.h"
#include <algorithm>
#include <cmath>

HudInput::HudInput(GameLogic &logic, AIWorld *ai, TacticalView &view, CommandList &commands, const MouseSettings &mouse, const MetaMap &metaMap)
	: m_logic(logic)
	, m_commands(commands)
	, m_ctx{ logic, ai, view, m_ui, m_stream, nullptr, mouse }
	, m_meta(m_ctx, metaMap)
	, m_formation(m_ctx)
	, m_place(m_ctx)
	, m_guiCommand(m_ctx)
	, m_command(m_ctx)
	, m_selection(m_ctx, m_command)
	, m_hotKey(m_ctx)
{
	m_ui.setMessageStream(&m_stream);
	m_ui.setLogic(&logic); // lane INPUT-1: getFrameSelectionChanged
	// lane INPUT-1: RotWK's GameClient::init (RW 0x646771) attaches WindowTranslator 4 / 10, a translator at 5 (RW 0x83EE14), MetaEvent 20, HotKey 25, the War of
	// the Ring translator 27 (RW 0x838EBA), PlaceEvent 30, Formation 35, GUICommand 40, Selection 50, LookAt 60, Command 70, HintSpy 100 and the dispatcher last
	m_stream.attachTranslator(&m_meta, 20);
	m_stream.attachTranslator(&m_hotKey, 25);
	m_stream.attachTranslator(&m_place, 30); // BUILD-1 (ZH PlaceEventTranslator)
	m_stream.attachTranslator(&m_formation, 35);
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

void HudInput::key(int key, int keyState, char32_t character)
{
	m_keyDown[key & 0xFF] = (keyState & KEY_STATE_UP) == 0; // lane BACKLOG3
	ClientMessage &m = m_stream.append((keyState & KEY_STATE_UP) ? CMSG_RAW_KEY_UP : CMSG_RAW_KEY_DOWN);
	m.appendInteger(key);
	m.appendInteger(keyState);
	m.appendInteger((int)character);
}

void HudInput::resetKeys()
{
	m_formation.cancel();
	for (bool &k : m_keyDown)
	{
		k = false;
	}
}

void HudInput::cancelHeld()
{
	resetKeys();
	if (m_lookAt)
	{
		m_lookAt->cancelHeld();
	}
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
	m_lookAt->setKeyboard([this](int key) { return isKeyDown(key); }); // lane BACKLOG3
	m_stream.attachTranslator(m_lookAt.get(), 60); // ZH GameClient.cpp: LookAtTranslator 60 (RW 0x646BC6 pushes 0x3C)
}

void HudInput::cameraFrame(unsigned nowMs, bool gamePaused)
{
	if (!m_camera)
	{
		return;
	}
	m_ui.advanceClientFrame(); // lane PLAY-1: the client frame the move hints age by
	m_lookAt->tick(nowMs);
	m_camera->update(m_ui.isScrolling(), gamePaused);
	m_camera->commitFrame();
}

size_t HudInput::update()
{
	// objects that died leave the selection mirror (ZH: the drawable's destructor deselects); lane FACT-A: so does a structure being sold, which the logic marked
	// SOLD and dropped from every player's selection (BuildAssistant::sellObject RW 0x7966BB -> GameLogic::deselectObject(obj, 0xFFFFF, affectClient = 1)
	// RW 0x7967A7, whose client half is InGameUI::deselectDrawable RW 0x6257E5; INFERENCE: the mirror follows the SOLD status set in the same call. Not
	// UNSELECTABLE alone: an unpacked castle is UNSELECTABLE and RW deselects nothing there)
	static const int sold = ObjectTemplateInfoBuilder::objectStatusIndex("SOLD");
	std::vector<ObjectID> gone;
	for (ObjectID id : m_ui.selected())
	{
		Object *o = m_logic.findObjectByID(id);
		// An occupied plot hidden by Construction::constructOnPlot leaves the client selection, closing its command menu.
		if (!o || o->isDestroyed() || (sold >= 0 && o->testStatus((unsigned)sold)) ||
			(o->isDrawableHidden() &&
				(o->isKindOfName("BASE_FOUNDATION") || o->isKindOfName("BASE_DEFENSE_FOUNDATION") || o->isKindOfName("EXPANSION_PAD"))))
		{
			gone.push_back(id);
		}
	}
	// lane VISUAL-2 r2, TARGET FACT (InGameUI::update, RW 0x6A2449 .. 0x6A24BF): when the client frame is a multiple of 30 (DAT 0xD9F60C), every selected
	// drawable whose object's shroud status for the local player (RW 0x68D8F7) is FOGGED or SHROUDED is deselected (vslot 0x10C, deselectDrawable): an
	// enemy selected while seen does not stay selected in the fog. Once per client frame; the status is clientObjectStatus while the shroud is displayed
	const ShroudManager *sm = m_logic.shroud();
	const Player *local = m_logic.players().getLocalPlayer();
	if (sm && sm->displayed() && local && m_ui.clientFrame() % 30 == 0 && m_ui.clientFrame() != m_fogDeselectFrame)
	{
		m_fogDeselectFrame = m_ui.clientFrame();
		for (ObjectID id : m_ui.selected())
		{
			const Object *o = m_logic.findObjectByID(id);
			const ObjectShroudStatus st = o ? sm->clientObjectStatus(*o, local->getPlayerIndex()) : OBJECTSHROUD_INVALID;
			if (st == OBJECTSHROUD_FOGGED || st == OBJECTSHROUD_SHROUDED)
			{
				gone.push_back(id);
			}
		}
	}
	for (ObjectID id : gone)
	{
		m_ui.forgetObject(id);
	}
	m_formation.frame();
	m_place.frame(); // lane QA2-FIX: the placement update's line build step (RW 0x6A2D12) runs in InGameUI::update, before the stream (ZH GameEngine::update)
	const size_t n = flush();
	// lane BUILD-5: the placement update's ghost step (RW 0x6A2AE5) runs in the client's draw, after this frame's input (it reads the live pointer); a device
	// defers it until its displayed camera is set (setPlacementDeferred)
	if (!m_placementDeferred)
	{
		m_place.handleBuildPlacements();
	}
	return n;
}

// lane OWNERFIX-1: the GameMessage as a stream message (the reverse of toGameMessage); an argument kind the HUD does not send is an error
void HudInput::submit(const GameMessage &message)
{
	ClientMessage &m = m_stream.append(message.getType());
	for (size_t i = 0; i < message.getArgumentCount(); ++i)
	{
		const GameMessageArgument &a = *message.getArgument(i);
		switch (a.type)
		{
			case ARGUMENTDATATYPE_INTEGER: m.appendInteger(a.integer); break;
			case ARGUMENTDATATYPE_REAL: m.appendReal(a.real); break;
			case ARGUMENTDATATYPE_BOOLEAN: m.appendBoolean(a.boolean); break;
			case ARGUMENTDATATYPE_OBJECTID: m.appendObjectID(a.objectID); break;
			case ARGUMENTDATATYPE_LOCATION: m.appendLocation(a.location); break;
			default: throw std::logic_error("HudInput::submit: message " + std::to_string(message.getType()) + " argument " + std::to_string(i) + " has a kind the stream does not carry");
		}
	}
}

void HudInput::scriptInput(bool enable, const std::function<void()> &controlBarReset)
{
	m_scriptSteps.clear();
	m_ui.setStepLog(&m_scriptSteps);
	m_ui.applyScriptInput(enable);
	if (enable)
	{
		return;
	}
	if (controlBarReset)
	{
		controlBarReset(); // RW 0x7BC5D9 -> RW 0x8076CD, before the LookAt flags (RW 0x7BC5E4 -> RW 0x83AB5D): the order of RW 0x7BC586's tail
	}
	if (m_lookAt)
	{
		m_lookAt->clearFlags(); // RW 0x83AB5D
		m_scriptSteps.push_back("lookAtFlags");
	}
}

size_t HudInput::flush()
{
	const int local = m_ctx.localPlayer() ? m_ctx.localPlayer()->getPlayerIndex() : 0;
	std::vector<ClientMessage> out = m_stream.propagate();
	// PR #88 review fix 1: only while the gesture is live (placement active or a chord button still held). m_consumed outlives the order until
	// the next press (it swallows the trailing release and clicks); cancelling the LookAt pointer on it left the camera dead after every order.
	if (m_formation.gestureLive()) {
		m_selection.setLeftMouseButton(false);
		m_ui.setSelecting(false);
		m_ui.endAreaSelectHint();
		if (m_lookAt) m_lookAt->cancelPointer();
	}
	size_t n = 0;
	for (const ClientMessage &m : out)
	{
		if (m.isLogic())
		{
			hintSpy(m); // lane PLAY-1: ZH HintSpyTranslator (priority 100) sees what reaches the end of the stream
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

void HudInput::placementUpdate(bool judge)
{
	m_place.handleBuildPlacements(judge);
}

// RotWK HintSpy (RW 0x838225 .. 0x83825E): the move family only, then createMoveHint's tests (RW 0x69F54F)
void HudInput::hintSpy(const ClientMessage &m)
{
	switch (m.type())
	{
		case MSG_DO_MOVETO:
		case MSG_DO_ATTACKMOVETO:
		case MSG_DO_FORCEMOVETO:
			break;
		default:
			return;
	}
	if (m.argumentCount() == 0 || m.arg(0).kind != ClientArgKind::Location)
	{
		return;
	}
	const Coord3D pos = m.arg(0).location;
	// RW 0x690E97 (stop S-1920: only its IMMOBILE test is ported; its held / disabled tests RW 0x9325B4, 0x46E918(0x81), 0x44DDEC(0x3B) are not)
	auto canMove = [](const Object &o) { return !o.isKindOfName("IMMOBILE"); };
	if (m_ui.getSelectCount() == 1)
	{
		const Object *o = m_logic.findObjectByID(m_ui.firstSelected());
		if (o && !canMove(*o))
		{
			return;
		}
	}
	// RW 0x69E8D0: a point the local player does not see now (RW 0xB4FB20 over the local player's shroud index, Player + 0x54: a looker count of 0 or the
	// never-seen -1, or outside the grid) is always hinted; a seen point when some selected object can move and is ARMY_OF_DEAD (template + 0x11A & 0x80,
	// KINDOF bit 151) or has an AI (Object + 0x260) whose pathfinder accepts the spot (RW 0x66403D -> RW 0x6F5BB0; stop S-1920: taken as accepted)
	bool hinted = true;
	const Player *local = m_ctx.localPlayer();
	const ShroudManager *shroud = m_logic.shroud();
	int cx = 0, cy = 0;
	if (local && shroud && shroud->worldToCell(pos.x, pos.y, cx, cy) && shroud->lookerCount(local->getPlayerIndex(), cx, cy) > 0)
	{
		hinted = false;
		for (ObjectID id : m_ui.selected())
		{
			const Object *o = m_logic.findObjectByID(id);
			if (o && canMove(*o) && (o->isKindOfName("ARMY_OF_DEAD") || o->getAIUpdateInterface() != nullptr))
			{
				hinted = true;
				break;
			}
		}
	}
	if (hinted)
	{
		m_ui.createMoveHint(pos);
	}
}

std::vector<std::string> HudInput::acceptanceStops()
{
	std::vector<std::string> out = {
		"[S-280] translator pipeline (narrowed, lane INPUT-1): RotWK's priorities (GameClient::init RW 0x646771): MetaEvent 20, HotKey 25, PlaceEvent 30, GUICommand 40, "
		"Selection 50, LookAt 60, Command 70; the Window (4 / 10), RW 0x83EE14 (5), War of the Ring (27) translators are not ported; the HotKey "
		"manager registers the command bar only (no message-type actions, spell book, radial menu or garrison hotkeys; its sounds are counted, not played); the "
		"SelectionTranslator's clicks and drag box were not compared with RW 0x83C29E; a drag selection does not emit MSG_AREA_SELECTION",
		"[S-281] CommandMap (narrowed, lane INPUT-1): RotWK's field table RW 0xBF0E70 and modifier values are ported; open: the files the subsystem entry "
		"\"CommandMap.ini\" (RW 0x63C4F6) resolves to (here Data\\INI\\CommandMap.ini, then the language's CommandMap.ini) and the MetaEventTranslator's PLANNING "
		"bit (RW 0x5DA83D); the German Y / Z swap follows the keyboard layout (RW 0x63F024)",
		"[S-282] tactical view: without HudInput::attachCamera the view is a PinholeView the device fills in (a stand-in with no limits); the retail camera, its translator and "
		"its stops are lane CAM-1's (TacticalCamera::acceptanceStops(), S-450 .. S-459)",
		"[S-283] InGameUI: only the state the translators and the control bar read is ported; radius decals, subtitles, superweapon timers, popups and the "
		"alternate-mouse bookkeeping are not (the building placement is BUILD-1's, S-306)",
		"[S-284] picking: without a drawable ray test (HudContext::pickRay, installed by InGameHud: S-1200) an object is hit by a ray against its Geometry volume (cylinder of the bounding "
		"radius, height at least the radius), the nearest first; ZH tests the model's polygons; a region holds the objects whose position projects into it",
		"[S-285] mouse setup: RESOLVED for the default (lane PLAY-1): RotWK's GlobalData sets m_useAlternateMouse = 1 (RW 0x642A4B: the right click orders) and Options.ini "
		"AlternateMouseSetup replaces it (RW 0x6E61D4: true unless the value is \"yes\"); the device layer (game.gd) passes that to CommandTranslator::setUseAlternateMouse; still "
		"not ported: the alternate setup's own left-click rules beyond ZH's (the selection never carries a context command)",
		"[S-286] context commands: resume construction, dock, repair, heal, capture / hijack / sabotage / salvage / snipe, combat drop, special powers, weapon fire commands, unit voice "
		"responses and the academy statistics are not ported; an attack is issued against an enemy when a selected object (or a member of a selected horde) can possibly have a weapon (RW 0x73C191), standing "
		"for ActionManager::canAttackObject; a click on a container a selected object may enter is MSG_ENTER (lane GARRISON-1, ActionManager::canEnterObject; the order of the context checks is not read); select-all takes every mobile object of the player across the map",
		"[S-287] meta commands: the cases of RotWK's handler (RW 0x81F8D8) are ported where the effect exists (select matching units and view home base: S-1202; select hero, the "
		"stance keys and view last radar event: S-1673 / S-1674, lane HUD-4; sell, the spell store, diplomacy, screenshot, camera reset and the no-op cases: lane INPUT-1); "
		"select next / previous unit and worker have no case in RotWK and do nothing (S-1673); CommandTranslator::unportedMeta() counts the cases whose effect is not "
		"ported (chat, control bar toggle, cheer, beacon, order synchronize, planning, fast forward, the camera key flags); TOGGLE_ATTACKMOVE has no case in RotWK (a retail no-op: "
		"A is the Attack Move button's hotkey); BEGIN_ / END_FORCEATTACK set the force-attack mode as ZH does, an OpenBFME addition: RotWK's table sends them to the default case (S-1675; its Ctrl + left press is S-4360); ORDERMODE sends MSG_CHANGE_ORDERMODE, the "
		"logic stores the order mode, whose AiOrdersManager queue is not ported (S-281: the orders ignore it)",

		"[S-288] selection filters: a click picks only what the device layer draws (QA-1's pickRay: not SHROUDED, FOGGED, invisible or hidden by a container); a drag box "
		"leaves out an enemy / neutral object the local player sees FOGGED or worse (lane VISUAL-2 r2: addDrawableToList's rule from the ZH / BFME2 decomp source, "
		"INFERENCE that RotWK keeps it: its RotWK site was not located) and an enemy's invisible, undetected object (lane STEALTH-1, InvisibilityManager::clientLook); "
		"the double click selects the matching units of the screen by template identity, across the map with the alt key",
	};
	for (const std::string &s : PlaceEventTranslator::stopLines()) // lane QA2-FIX
	{
		out.push_back(s);
	}
	// lane PLAY-1: the move hint (InGameUI::createMoveHint)
	out.push_back("[S-1920] move hint (RotWK HintSpy RW 0x838225, createMoveHint RW 0x69F54F, drawMoveHints RW 0x48EDED: one marker at a time, 25 slots, 40 client frames): "
				  "not ported: RW 0x690E97's held / disabled tests (only IMMOBILE), the pathfinder's acceptance of the spot (RW 0x66403D -> 0x6F5BB0: taken as accepted), "
				  "the lift to the water surface (TerrainLogic vslot 0x4C)");
	out.push_back("[S-1954] RotWK's pick (HUD-5): the cast's collision types (RW 0x4B583C), the hit order (RW 0x48AB28 / 0x489BE6) and each caller's pick types are ported: "
				  "RW 0x71083F (SELECTABLE | SCRIPT_TARGETABLE, FORCEATTACKABLE when forcing, SHRUBBERY / ROCK from the GUI command or the selection, RW 0x71077B), the point selection | OWN "
				  "(RW 0x485CB8), the hover forced (RW 0x83CC13), the order click (RW 0x81FBB3), the double click SELECTABLE (RW 0x81F7C5), a GUI command's object target "
				  "(RW 0x83D41A); the double click runs in CommandTranslator (RW 0x81F791), promotes HORDE_MEMBER and expands the current selection across the screen "
				  "(RW 0x9403ED / 0x69D90C); client selectability uses RW 0x68DE58 including the wall special-power override. Not ported: the Living World input lock, "
				  "pickDrawable's forceAttack argument (the cast flag RW 0x48ABFF), the model draw's own "
				  "override (W3DModelDraw + 0x214), drawable rubble state and final draw-module collision suppression (RW 0x4B59BD / 0x4B59E4); "
				  "SCRIPT_TARGETABLE's type replacement and the KeepSelectableWhenDead exception are ported");
	out.push_back("[S-3302] building placement ghost (PLAY-1, PLAY-3, BUILD-5): the ghost is drawables of the template (PlacementIcons: RW 0x69C5E6 / 0x6A2AE5) with "
				  "BUILD_PLACEMENT_CURSOR, opacity 0.45, the builder's colour (RW 0x674504) and the draw module's own model, animation and BeginScript; a castle's layout "
				  "pieces turned by the ghost's angle, its BASE_SITE slots judged one by one; the tint through the W3D lighting (ZH W3DScene's colorTint); the faction bib "
				  "(RW 0x49249D extents, TBRedBib / TBBib); W_Arrow while anchored (RW 0x48EAD8); the alpha blend (ZH dx8renderer's translucent pass, W3DShaderKey::AlphaOverride); "
				  "not ported: the restricted-area code 8 (RW 0x707141), its shadow (SHADOW_VOLUME; no drawable casts one yet, S-3304), RotWK's own "
				  "tint lighting (the donor is ZH's), the bib quad's own buffer (ZH's W3DBibBuffer)");
	out.push_back(FormationPlacement::stopLine());
	return out;
}

std::vector<std::string> HudInput::stops() const
{
	std::vector<std::string> out = acceptanceStops();
	// lane FACT-C (S-286 keeps the other context commands)
	out.push_back("[S-2000] special power targets: a SPECIAL_POWER button waiting for its target sends RotWK's messages on the click (evaluateContextCommand RW 0x81E60B -> "
				  "issueSpecialPowerCommand RW 0x81E147: AT_OBJECT for an object of a relationship the options allow, AT_LOCATION with the chained CommandTrigger buttons, "
				  "source NONE); the action check RW 0x6A4260 (range, KindOf and the power's own target rules) and the kind-of / option 0x10 / 0x200000 target drops "
				  "before it are not ported: the option's target kind alone makes a click valid");
	out.push_back("[S-2002] capture context command: a click on a building the selection can capture issues the first selected object's capture button (power type 0x1A / 0x1D) "
				  "at it (RW 0x81E60B, canCaptureBuilding RW 0x82D0A1); not ported: a capture power's ObjectFilter (none in retail data; counted), the status pair + 0x438 / "
				  "+ 0x110, the + 0x80 word, the shroud test, CAPTURE_FORBIDDEN, the per-player refusal RW 0x6943B2, stealth riders and the vision test");
	out.push_back("[S-4944] siege deploy click (lane SIEGE-1): a right click on a wall a selected ladder / tower can dock at is the deploy (RW 0x81E60B's siege branch, "
				  "InGameUI RW 0x69D674; MSG_DO_SPECIAL_POWER_AT_OBJECT with Command_SpecialAbilitySiegeDeploy's power, the attack hint); not identified: the InGameUI "
				  "byte + 0x8B0 the branch requires clear (taken as clear); the button's unit voice is not played");
	out.push_back("[S-1673] hotkeys (HUD-4): Ctrl+H walks the local player's objects for the next HERO after the selected one and selects it (RW 0x81FDA2 / 0x81DABC; "
				  "INFERENCE: the player's team list is its prototypes in creation order, the gate RW 0x81FDA8 is a local player, no drawable test); D / F / G send "
				  "MSG_CHANGE_STANCE 2 / 1 / 3 (RW 0x8201F5 .. 0x820247); Shift+Up / Shift+Down (and the unit cycle keys) have no handler in RotWK 2.01 and pass "
				  "unexecuted, as there");
	// lane INPUT-1 r3
	// lane INPUT-1 r3, restated by lane BACKLOG3 with the binary's facts
	out.push_back("[S-1675] force attack (an OpenBFME addition, NOT retail): Ctrl (BEGIN_ / END_FORCEATTACK) sets InGameUI's force-attack mode as ZH's CommandXlat.cpp:3483 "
				  "does. TARGET FACT: RotWK 2.01 never turns the mode on (its meta jump table RW 0x8207A0 sends 0x74 / 0x75 to the default case beside the inlined "
				  "setters of force move, waypoints and prefer selection; the setter RW 0x69AD1F has no reference, every other store to InGameUI + 0x8B8 clears it); "
				  "the mode's readers are RotWK's (RW 0x81FBBC -> 0x81E4B5, RW 0x81DB0C). Kept because it was released; the owner decides whether it stays");
	out.push_back("[S-4360] Ctrl + left button (lane BACKLOG3): RotWK's SelectionTranslator tests TheKeyboard's Ctrl at the left button's press (RW 0x83CA72 -> RW "
				  "0x63F135), sets InGameUI + 0x924 and collects the pointer's points (RW 0x6A091A: a list at + 0x910 with its bounds at + 0x914, a new point every "
				  "third of the drag tolerance); at the release RW 0x6A0A5E walks the drawables with the test RW 0x69CBBE against those points (fewer than three "
				  "points or an empty box: the pick at the box). Not ported: a Ctrl + drag is the plain box selection here, and Ctrl is the force-attack key (S-1675)");
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

bool FormationPlacement::withinChordDistance(const ICoord2D &first, const ICoord2D &next)
{
	// BFME2 RVA 0x431978. Widen before subtracting so hostile device coordinates cannot overflow.
	const auto dx = (std::int64_t)next.x - first.x;
	const auto dy = (std::int64_t)next.y - first.y;
	return dx > -5 && dx < 5 && dy > -5 && dy < 5;
}

bool FormationPlacement::isStartOfDrag(const ClientMessage &first, const ClientMessage &next)
{
	// BFME2 RVA 0x431BDD: unsigned subtraction, including timestamp wraparound.
	const int left = CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, right = CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN;
	if (!((first.type() == left && next.type() == right) || (first.type() == right && next.type() == left)))
		return false;
	return withinChordDistance(first.arg(0).pixel, next.arg(0).pixel) && (std::uint32_t(next.arg(2).integer) - std::uint32_t(first.arg(2).integer)) < 500u;
}

const char *FormationPlacement::stopLine()
{
	return "[S-4464] Two-button formation: BFME2 gesture, INI cell filters, repeated rows, selection criteria, preview textures and four-argument formation orders are ported. "
		"RotWK parity, native formation-group pathing/slot optimization and firing-arc AI state parity remain unverified; current execution uses per-slot movement and locomotor facing.";
}

void FormationPlacement::replayFirstPress(const ClientMessage &before)
{
	if (!m_firstPress)
		return;
	// BFME2 RVA 0x431C35: reinsert the stored press before the event that ended the wait.
	// This translator already handled it; all later translators see the original press first.
	m_ctx.stream.insertBefore(m_firstPress->type(), before) = *m_firstPress;
	m_firstPress.reset();
}

// The checks begin() makes before it reads the terrain: the input gates, formation data with templates, the firing-arc pick at the pixel
// and at least one eligible selected object. PR #88 review fix 2: translate() buffers a first press only when these pass, so a press that
// cannot start a formation (placing a building, no eligible selection, BFME1 data without FormationAssistant.ini) reaches selection and
// the LookAt translator in the same frame, as before.
bool FormationPlacement::eligible(const ICoord2D &pixel, bool &rotatingArc, std::vector<ObjectID> &ids) const
{
	const auto &ui = m_ctx.ui;
	ids.clear();
	rotatingArc = false;
	if (!ui.getInputEnabled() || ui.isPlacing() || (ui.getGUICommand() && ui.getGUICommand()->m_command != GUI_COMMAND_ATTACK_MOVE) || !m_ctx.localPlayer() ||
		!m_ctx.formations || m_ctx.formations->templates.empty())
		return false;
	if (const Object *picked = HudObjects::pickForHover(m_ctx, pixel))
	{
		if (const auto *field = picked->getTemplate()->findField("FiringArc"))
		{
			if (const auto *arc = std::get_if<float>(field))
				rotatingArc = *arc < 360.f;
		}
	}
	for (ObjectID id : ui.selected())
	{
		Object *o = m_ctx.logic.findObjectByID(id);
		if (!o || o->isDestroyed() || o->isEffectivelyDead() || o->getControllingPlayer() != m_ctx.localPlayer())
			continue;
		auto *ai = o->getAIUpdateInterface();
		if (ai && (rotatingArc || !ai->isImmobile()) && !o->isKindOfName("HORDE_MEMBER") && ObjectFilterMatch::allows(m_ctx.logic, m_ctx.formations->valid, *o, nullptr))
			ids.push_back(id);
	}
	return !ids.empty();
}

bool FormationPlacement::canStart(const ICoord2D &pixel) const
{
	bool rotatingArc = false;
	std::vector<ObjectID> ids;
	return eligible(pixel, rotatingArc, ids);
}

// Two-button placement takes priority over selection and right-drag scrolling.
bool FormationPlacement::begin(const ICoord2D &pixel, bool controller)
{
	bool rotatingArc = false;
	std::vector<ObjectID> ids;
	Coord3D anchor;
	if (!eligible(pixel, rotatingArc, ids) || !m_ctx.view.screenToTerrain(pixel, m_ctx.logic, anchor))
		return false;
	m_rotatingArc = rotatingArc;
	m_selection = std::move(ids);
	m_anchor = anchor;
	m_controller = controller;
	m_originalSelection = m_ctx.ui.selected();
	m_ctx.ui.formationActive = true;
	m_consumed = true;
	aim(pixel);
	return true;
}

// PR #88 review fix 6: the drag angle and the template index go into MSG_DO_MOVETO_FORMATION, so they are computed with SimMath (one SSE
// rounding per operation, the CRT-identical atan2, the deterministic sin / cos of the sim handler AICommands::formationMove), never libm.
void FormationPlacement::aim(const ICoord2D &pixel)
{
	auto &ui = m_ctx.ui;
	Coord3D end;
	if (!m_ctx.view.screenToTerrain(pixel, m_ctx.logic, end))
		return;
	if (!std::isfinite(end.x) || !std::isfinite(end.y))
		return;
	float mx = 0, my = 0;
	if (auto *terrain = m_ctx.logic.terrain(); terrain && terrain->getLargestExtent(mx, my))
	{
		if (end.x < 0 || end.y < 0 || end.x > mx || end.y > my)
			return;
	}
	m_end = end;
	const float dx = SimMath::subf32(end.x, m_anchor.x), dy = SimMath::subf32(end.y, m_anchor.y);
	const float length = SimMath::length2d(dx, dy);
	const float angle = length > 1 ? SimMath::fstpDword(SimMath::atan2d(dy, dx)) : 0.0f;
	ui.formationType = m_ctx.formations->choose(length, m_selection.size());
	ui.formationAnchor = m_anchor;
	ui.formationAngle = angle;
	ui.formationDragLength = length;
	ui.formationData = m_ctx.formations;
	const auto slots = m_ctx.formations->layout(m_ctx.logic, m_selection, ui.formationType, m_anchor);
	const float rotated = SimMath::addf32(angle, 1.5707964f);
	const float c = SimMath::cosDet(rotated), s = SimMath::sinDet(rotated);
	ui.formationSlots.clear();
	for (const auto &slot : slots)
	{
		Coord3D p{ SimMath::subf32(SimMath::addf32(m_anchor.x, SimMath::mulf32(slot.offset.x, c)), SimMath::mulf32(slot.offset.y, s)),
				   SimMath::addf32(SimMath::addf32(m_anchor.y, SimMath::mulf32(slot.offset.x, s)), SimMath::mulf32(slot.offset.y, c)), 0 };
		p.z = m_ctx.logic.getGroundHeight(p.x, p.y);
		ui.formationSlots.push_back({ slot.object, p, angle, 0, slot.preview });
	}
}

void FormationPlacement::commit()
{
	auto &ui = m_ctx.ui;
	const bool guiAttackMove = ui.getGUICommand() && ui.getGUICommand()->m_command == GUI_COMMAND_ATTACK_MOVE;
	if (m_rotatingArc || !ui.formationSlots.empty())
	{
		ClientMessage &order = m_ctx.stream.append(m_rotatingArc ? MSG_DO_ROTATE_FIRINGARC : MSG_DO_MOVETO_FORMATION);
		if (m_rotatingArc)
			order.appendLocation(m_end);
		else
		{
			order.appendLocation(ui.formationAnchor);
			order.appendReal(ui.formationAngle);
			order.appendInteger(ui.formationType);
			order.appendBoolean(guiAttackMove || ui.isInAttackMoveToMode());
		}
	}
	ui.formationActive = false;
	ui.formationExpires = ui.clientFrame() + 45;
	ui.clearAttackMoveToMode();
	// PR #88 review fix 3: the order used the pending attack-move button; leave the GUI command mode too (GUICommandTranslator clears it
	// after its own attack-move order), or the next left click would send MSG_DO_ATTACKMOVETO
	if (guiAttackMove)
		ui.setGUICommand(nullptr);
}

void FormationPlacement::frame() {
 if(m_ctx.ui.formationActive && (!m_ctx.ui.getInputEnabled() || m_ctx.ui.isQuitMenuVisible() || m_ctx.ui.selected()!=m_originalSelection)) cancel();
}
void FormationPlacement::cancel() {
 m_ctx.ui.formationActive=false;m_ctx.ui.formationSlots.clear();m_left=m_right=false;m_firstPress.reset();
}
MessageDisposition FormationPlacement::translate(const ClientMessage &msg)
{
	const int t = msg.type();
	if (m_ctx.ui.formationActive && (!m_ctx.ui.getInputEnabled() || m_ctx.ui.isPlacing() || (m_ctx.ui.getGUICommand() && m_ctx.ui.getGUICommand()->m_command != GUI_COMMAND_ATTACK_MOVE)))
		cancel();
	if (t == CMSG_RAW_KEY_DOWN && msg.arg(0).integer == KEY_ESC && m_ctx.ui.formationActive)
	{
		cancel();
		return MessageDisposition::Destroy;
	}
	const bool leftDown = t == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, rightDown = t == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN;
	const bool leftUp = t == CMSG_RAW_MOUSE_LEFT_BUTTON_UP, rightUp = t == CMSG_RAW_MOUSE_RIGHT_BUTTON_UP;
	if (leftDown || rightDown)
	{
		const bool idle = !m_left && !m_right && !m_ctx.ui.formationActive;
		if (idle)
			m_consumed = false;
		if (leftDown)
			m_left = true;
		else
			m_right = true;
		if (m_firstPress)
		{
			if (isStartOfDrag(*m_firstPress, msg) && begin(m_firstPress->arg(0).pixel))
			{
				m_firstPress.reset();
				aim(msg.arg(0).pixel);
				return MessageDisposition::Destroy;
			}
			replayFirstPress(msg);
		}
		else if (!m_consumed && !m_ctx.ui.formationActive && canStart(msg.arg(0).pixel))
		{
			// BFME2 RVA 0x431AEC: neither selection nor right-drag scrolling sees a
			// speculative first press. A failed chord replays it with its original modifiers/time.
			m_firstPress = std::make_unique<ClientMessage>(msg);
			return MessageDisposition::Destroy;
		}
	}
	if (m_firstPress &&
		((t == CMSG_RAW_MOUSE_POSITION && !withinChordDistance(m_firstPress->arg(0).pixel, msg.arg(0).pixel)) ||
		 (leftUp && m_firstPress->type() == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN) || (rightUp && m_firstPress->type() == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN)))
		replayFirstPress(msg);
	if (leftUp || rightUp)
	{
		if (leftUp)
			m_left = false;
		else
			m_right = false;
		// BFME2 RVAs 0x4320B1 / 0x431E95: either release finalizes placement.
		if (m_ctx.ui.formationActive && !m_controller)
			commit();
	}
	if (t == CMSG_RAW_MOUSE_POSITION && m_ctx.ui.formationActive && !m_controller)
		aim(msg.arg(0).pixel);
	const bool click = t == CMSG_MOUSE_LEFT_CLICK || t == CMSG_MOUSE_RIGHT_CLICK || t == CMSG_MOUSE_LEFT_DOUBLE_CLICK || t == CMSG_MOUSE_RIGHT_DOUBLE_CLICK;
	if (m_consumed && (leftDown || rightDown || leftUp || rightUp || click || (t == CMSG_RAW_MOUSE_POSITION && m_ctx.ui.formationActive)))
		return MessageDisposition::Destroy;
	return MessageDisposition::Keep;
}
