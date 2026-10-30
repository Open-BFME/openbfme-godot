// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ControlBar::processCommandUI (ZH ControlBarCommandProcessing.cpp): a pressed command button becomes the messages of the lockstep command stream (integer and id
// arguments only) or a GUI command mode. Kept apart from the view model (ControlBar.cpp) because it BUILDS GameMessages: this file is in the simulation audit manifest.

#include "GameClient/ControlBar.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/GateModules.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"

#include "Common/BuildAssistant.h"
#include "Common/ModEngineFeatures.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <map>

// lane CAMPUI-1: RW 0x96F53C (InGameToggleStanceCommandButton::SetStance): TheMessageStream's new message 0x468 (MSG_CHANGE_STANCE, vslot 0x48) with the stance as its
// integer (RW 0x7111E5); the logic applies it to the sender's selection (RW 0x77BC59)
bool ControlBar::sendChangeStance(int stance, const char *site)
{
	for (ObjectID id : m_ctx.ui.selected())
	{
		Object *obj = m_ctx.logic.findObjectByID(id);
		if (StancesBehavior *sb = obj ? StancesBehavior::of(*obj) : nullptr)
		{
			sb->traceQueuedStance(stance, site);
		}
	}
	ClientMessage &m = m_ctx.stream.append(MSG_CHANGE_STANCE);
	m.appendInteger(stance);
	return true;
}

bool ControlBar::pressButton(int slot, bool inPalantir)
{
	const ControlBarButton *cb = find(slot, inPalantir);
	Object *obj = m_ctx.logic.findObjectByID(m_source);
	if (!cb || !cb->button || !obj)
	{
		return false;
	}
	const CommandButton &b = *cb->button;
	// lane MODS-3: RW 0x941BD3 (processContextSensitiveButtonClick): an inactive local player (an observer, a defeated player: RW 0x6A87F5) presses
	// nothing; edain-unchained.observer-command-range (its 0xEDF000) lets one page the command ranges. Retail's refusal is ported for that feature only
	// (the port's retail bar has none yet, S-290)
	if (ModEngineFeatures::has(ModEngineFeature::EdainUnchainedObserverCommandRange) && !localPlayerActive(m_ctx.localPlayer()) &&
		b.m_command != GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.m_command != GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE)
	{
		return false;
	}
	// RW 0x940435: IsClickable (+0x105) gates processCommandUI. INFERENCE: the NONPRESSABLE refusal: RotWK's processCommandUI never tests
	// that option bit (its option tests are 0x227, bits 5, 17 and 31); retail draws such a button with its _static clip (RW 0x9D2BFA), which
	// takes no click, so a direct / shortcut press is refused here instead of casting its placeholder power.
	if (!b.m_isClickable || b.hasOption(COMMAND_OPTION_NONPRESSABLE))
	{
		return false;
	}
	// ZH processCommandUI: only the owner's objects take commands (a foreign selection has no set here)
	if (cb->state == ButtonState::Restricted || cb->state == ButtonState::NotReady)
	{
		return false;
	}
	m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
	if (obj->isKindOfName("WALL_HUB"))
	{
		m_ctx.ui.setLineBuildStarted(false); // lane QA2-FIX: RW 0x94078F: a wall hub's command first clears the line build flag (TheInGameUI vslot 0x100(0))
	}
	// an upgrade button's affordability is asked live by its own case (canAffordUpgrade, RW 0x940E45: "GUI:NotEnoughMoneyToUpgrade"); lane FACT-A: its
	// CantAfford state (RW 0x942733 code 6) is the bar's last evaluation, not a refusal of its own
	if (cb->commandPointsShort)
	{
		return false; // RW 0x940B84 .. 0x940B8A: canMakeUnit 7 (command points short) leaves processCommandUI with no message
	}
	if (cb->state == ButtonState::CantAfford && b.m_command != GUI_COMMAND_OBJECT_UPGRADE && b.m_command != GUI_COMMAND_PLAYER_UPGRADE
		&& b.m_command != GUI_COMMAND_CASTLE_UPGRADE)
	{
		m_ctx.ui.message("GUI:NotEnoughMoneyToBuild"); // processCommandUI: CANMAKE_NO_MONEY
		return false;
	}
	switch (b.m_command)
	{
		case GUI_COMMAND_UNIT_BUILD:
		{
			const ThingTemplate *tt = b.getThingTemplate();
			const CanMakeType make = BuildAssistant::canMakeUnit(*obj, tt, -1);
			if (make == CANMAKE_QUEUE_FULL)
			{
				m_ctx.ui.message("GUI:ProductionQueueFull");
				return false;
			}
			if (make == CANMAKE_MAXED_OUT_FOR_PLAYER)
			{
				m_ctx.ui.message("GUI:UnitMaxedOut");
				return false;
			}
			if (make != CANMAKE_OK)
			{
				return false;
			}
			// MSG_QUEUE_UNIT_CREATE, RotWK layout (RW 0x77A764): { byte fromBuildIndex, int templateId, int value30, byte batch, byte secondary }
			ClientMessage &m = m_ctx.stream.append(MSG_QUEUE_UNIT_CREATE);
			m.appendBoolean(false);
			m.appendInteger((int)tt->getTemplateID());
			m.appendInteger(-1);
			m.appendBoolean(false);
			m.appendBoolean(false);
			return true;
		}
		case GUI_COMMAND_DOZER_CONSTRUCT:
		case GUI_COMMAND_FOUNDATION_CONSTRUCT:
		{
			// ZH processCommandUI: a build button asks the user interface for a site (placeBuildAvailable); the PlaceEventTranslator sends the construct message when the
			// player clicks a legal one. A plot's button (FOUNDATION_CONSTRUCT) builds on the plot itself: the message goes out at once with the plot's own place
			const ThingTemplate *tt = b.getThingTemplate();
			const CanMakeType make = BuildAssistant::canMakeUnit(*obj, tt, -1);
			if (make == CANMAKE_MAXED_OUT_FOR_PLAYER)
			{
				m_ctx.ui.message("GUI:UnitMaxedOut");
				return false;
			}
			if (make != CANMAKE_OK)
			{
				return false;
			}
			if (b.m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT)
			{
				ClientMessage &m = m_ctx.stream.append(MSG_FOUNDATION_CONSTRUCT);
				m.appendInteger((int)tt->getTemplateID());
				m.appendLocation(*obj->getPosition());
				m.appendReal(0.0f);
				return true;
			}
			m_ctx.ui.placeBuildAvailable(tt->getName(), obj->getID(), &b); // RW 0x940AA1, then the button as the GUI command (RW 0x94089B; see InGameUI.h)
			return true;
		}
		case GUI_COMMAND_SET_STANCE:
		{
			// lane PLAY-4: a stance button sends MSG_CHANGE_STANCE (0x468) with its stance (as InGameToggleStanceCommandButton's SetStance RW 0x96F53C does; the
			// logic case RW 0x77BC59)
			const int stance = b.m_stances.empty() ? -1 : b.m_stances.front();
			if (stance < 0)
			{
				++m_unported["STANCE (no stance)"];
				return false;
			}
			return sendChangeStance(stance);
		}
		case GUI_COMMAND_TOGGLE_STANCE:
			// lane CAMPUI-1: retail never sends a stance for the toggle's own press: on the Palantir its content is an InGameToggleStanceCommandButton (RW 0x92FF5C)
			// whose press opens / closes the radial of its Stances (RW 0x96FAC6, AptPalantir::pressToggleStance), and only an item's press sends (RW 0x96F53C ->
			// sendChangeStance). A press that reaches the command processor (no sub-menu: BFME1 data, a hotkey, another bar) is counted, never turned into a stance
			++m_unported["TOGGLE_STANCE (no sub-menu)"];
			return false;
		case GUI_COMMAND_FOUNDATION_CONSTRUCT_CANCEL:
			// lane PLAY-4: processCommandUI's case for command 2 (RW 0x9409A1, jump table RW 0x941AC3 / 0x941B63): the object's controlling player is the local
			// one: MSG_DOZER_CANCEL_CONSTRUCT (0x41B; the logic case RW 0x77B1A0, BuildCommands)
			if (obj->getControllingPlayer() != m_ctx.localPlayer())
			{
				return false;
			}
			m_ctx.stream.append(MSG_DOZER_CANCEL_CONSTRUCT);
			return true;
		case GUI_COMMAND_STOP:
			m_ctx.stream.append(MSG_DO_STOP);
			return true;
		case GUI_COMMAND_SELL:
			m_ctx.stream.append(MSG_SELL);
			return true;
		case GUI_COMMAND_EVACUATE:
		{
			// lane GARRISON-1 (ZH processCommandUI GUI_COMMAND_EVACUATE): a container with riders gets MSG_EVACUATE for the selection (the logic case RW 0x77AD4E ->
			// RW 0x7725DF). The garrison's inventory view that shows this button is ControlBar::structureInventory (RW 0x94518D, lane UI-1)
			ContainModuleInterface *c = obj->getContain();
			if (!c || c->getContainCount() == 0)
			{
				return false;
			}
			m_ctx.stream.append(MSG_EVACUATE);
			return true;
		}
		case GUI_COMMAND_EXIT_CONTAINER:
		{
			// lane UI-1: RW 0x940FEF: the pressed window's rider from the slot table (RW 0xDEB888, filled by RW 0x942395 / 0x9450F0); a rider that no longer exists
			// sends nothing; else MSG_EXIT (1053) with the rider (RW 0x94103B) and the control bar's source object (RW 0x94152F)
			Object *rider = cb->rider != INVALID_ID ? m_ctx.logic.findObjectByID(cb->rider) : nullptr;
			if (!rider)
			{
				return false;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_EXIT);
			m.appendObjectID(rider->getID());
			m.appendObjectID(m_source);
			return true;
		}
		case GUI_COMMAND_TOGGLE_WEAPONSET:
		{
			// lane HUD-4: RW 0x9412C8: MSG_WEAPONSET_TOGGLE with the context object's id (the logic case RW 0x77B529, GameLogic/WeaponSetToggle.cpp); the
			// button's client side (RW 0x75D22E: the image index update) is not ported (S-1672)
			ClientMessage &m = m_ctx.stream.append(MSG_WEAPONSET_TOGGLE);
			m.appendObjectID(m_source);
			return true;
		}
		case GUI_COMMAND_TOGGLE_GATE:
		case GUI_COMMAND_OPEN_GATE:
		case GUI_COMMAND_CLOSE_GATE:
		{
			// lane HUD-5: RW 0x9410D7 (TOGGLE_GATE): the context object's gate (GateOpenAndCloseBehavior; GateProxyBehavior is not ported, S-1941), only when settled:
			// MSG_CLOSE_GATE when it is open, else MSG_OPEN_GATE; RW 0x9410AB: OPEN_GATE / CLOSE_GATE send their own message. The object id is the argument
			// (RW 0x94152F). The unit voice response (RW 0x8DEDBB) is the client's (not ported)
			GateOpenAndCloseBehavior *gate = obj ? GateOpenAndCloseBehavior::findGate(*obj) : nullptr;
			int type = b.m_command == GUI_COMMAND_OPEN_GATE ? MSG_OPEN_GATE : MSG_CLOSE_GATE;
			if (b.m_command == GUI_COMMAND_TOGGLE_GATE)
			{
				if (!gate || !gate->isSettled())
				{
					return false;
				}
				type = gate->isOpen() ? MSG_CLOSE_GATE : MSG_OPEN_GATE;
			}
			ClientMessage &m = m_ctx.stream.append(type);
			m.appendObjectID(m_source);
			return true;
		}
		case GUI_COMMAND_HORDE_TOGGLE_FORMATION:
		{
			// RW 0x940435 case 0x24: multi-select votes on the NEXT template id; ties keep the lowest id.
			// Explicit set messages preserve already-matching and incompatible formations in simulation.
			if (m_ctx.ui.selected().size() > 1)
			{
				std::map<unsigned short, unsigned> votes;
				std::vector<ObjectID> hordes;
				for (ObjectID id : m_ctx.ui.selected())
				{
					Object *selected = m_ctx.logic.findObjectByID(id);
					auto *hc = selected ? dynamic_cast<HordeContain *>(selected->getContain()) : nullptr;
					if (!hc) continue;
					hordes.push_back(id);
					if (const ThingTemplate *next = hc->alternateFormationTemplate())
					{
						++votes[next->getTemplateID()];
					}
				}
				unsigned most = 0;
				unsigned short target = 0;
				for (const auto &vote : votes)
				{
					if (vote.second > most)
					{
						most = vote.second;
						target = vote.first;
					}
				}
				if (most)
				{
					for (ObjectID id : hordes)
					{
						ClientMessage &m = m_ctx.stream.append(MSG_HORDE_SET_FORMATION);
						m.appendObjectID(id);
						m.appendInteger(target);
					}
				}
				return true;
			}
			// lane HORDE-2: the logic case (RW 0x77BE83) reads argument 0 as the horde's object id: the control bar's source object
			ClientMessage &m = m_ctx.stream.append(MSG_HORDE_TOGGLE_FORMATION);
			m.appendObjectID(m_source);
			return true;
		}
		case GUI_COMMAND_ATTACK_MOVE:
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
		case GUI_COMMAND_SET_RALLY_POINT:
			m_ctx.ui.setGUICommand(&b); // waits for the target (GUICommandTranslator)
			return true;
		case GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE:
			m_rangeStack.push_back({ b.m_commandRangeStart, b.m_commandRangeCount });
			return true;
		case GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE:
			if (m_rangeStack.size() > 1)
			{
				m_rangeStack.pop_back();
			}
			return true;
		case GUI_COMMAND_OBJECT_UPGRADE:
		case GUI_COMMAND_PLAYER_UPGRADE:
		{
			// MSG_QUEUE_UPGRADE: argument 1 is the upgrade's mask bit (the dispatcher case RW 0x77A6FD reads only that one; lane UPGRADE-1 executes it)
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u)
			{
				++m_unported[b.m_command == GUI_COMMAND_OBJECT_UPGRADE ? "OBJECT_UPGRADE (no upgrade)" : "PLAYER_UPGRADE (no upgrade)"];
				return false;
			}
			// lane PLAY-1: RotWK's processCommandUI asks canAffordUpgrade(player, upgrade, object, show the reason) first (RW 0x940E45 -> RW 0x66F492): short of
			// the cost the UI says "GUI:NotEnoughMoneyToUpgrade" and nothing is sent
			if (!UpgradeCenter::canAffordUpgrade(obj->getControllingPlayer(), u, obj))
			{
				m_ctx.ui.message("GUI:NotEnoughMoneyToUpgrade");
				return false;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_QUEUE_UPGRADE);
			m.appendObjectID(obj->getID());
			m.appendInteger(u->getMaskBit());
			return true;
		}
		case GUI_COMMAND_CASTLE_UPGRADE:
		{
			// lane FACT-A: RW 0x940435 case 8: canAffordUpgrade with the reason shown (RW 0x66F492: "GUI:NotEnoughMoneyToUpgrade"); a queue that refuses it
			// (canQueueUpgrade slot 1 == 4): "GUI:ProductionQueueFull"; an object that has it: nothing; an object whose CastleMemberBehavior names a castle that
			// exists: MSG_QUEUE_UPGRADE { the object, the upgrade's mask bit } (RW 0x940F88, as an OBJECT_UPGRADE); otherwise nothing
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u)
			{
				++m_unported["CASTLE_UPGRADE (no upgrade)"];
				return false;
			}
			if (!UpgradeCenter::canAffordUpgrade(obj->getControllingPlayer(), u, obj))
			{
				m_ctx.ui.message("GUI:NotEnoughMoneyToUpgrade");
				return false;
			}
			ProductionUpdateInterface *pu = obj->getProductionUpdate();
			if (pu && pu->canQueueUpgrade(u) == 4)
			{
				m_ctx.ui.message("GUI:ProductionQueueFull");
				return false;
			}
			if (obj->hasUpgrade(u))
			{
				return true;
			}
			const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(obj->findModule("CastleMemberBehavior"));
			if (!member || member->castleId() == INVALID_ID || !m_ctx.logic.findObjectByID(member->castleId()))
			{
				return true;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_QUEUE_UPGRADE);
			m.appendObjectID(obj->getID());
			m.appendInteger(u->getMaskBit());
			return true;
		}
		case GUI_COMMAND_REVIVE:
		{
			// lane HERO-1 (S-850): the record's MSG_QUEUE_UNIT_CREATE with the build-index flag (RotWK layout { byte fromBuildIndex, int index, int value30, byte batch,
			// byte secondary }); a record already in production is not queued again
			if (cb->reviveIndex < 0 || cb->queued > 0)
			{
				return false;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_QUEUE_UNIT_CREATE);
			m.appendBoolean(true);
			m.appendInteger(cb->reviveIndex);
			m.appendInteger(-1);
			m.appendBoolean(false);
			m.appendBoolean(false);
			return true;
		}
		case GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE:
		{
			// lane FACT-A: RW 0x940435 case 0x30: MSG_TOGGLE_NO_AUTO_ACQUIRE { objectID the context object } (the button's image toggle RW 0x75D366 is the client's)
			ClientMessage &m = m_ctx.stream.append(MSG_TOGGLE_NO_AUTO_ACQUIRE);
			m.appendObjectID(obj->getID());
			return true;
		}
		case GUI_COMMAND_FIRE_WEAPON:
		{
			// lane FACT-A: RW 0x940435. A button with a target option (Options & 0x227: the three NEED_TARGET_*_OBJECT, NEED_TARGET_POS, CONTEXTMODE_COMMAND) waits
			// for the target (setGUICommand, RW 0x94089C; the click is GUICommandTranslator's); otherwise case 0x17 sends MSG_DO_WEAPON (RW 0x941459)
			// { int WeaponSlot (button + 0x80), int MaxShotsToFire (button + 0xA0) }
			if (b.hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT | COMMAND_OPTION_NEED_TARGET_POS
					| COMMAND_OPTION_CONTEXTMODE_COMMAND))
			{
				// RW 0x940877..0x94089C: pressing the current target command cancels it.
				m_ctx.ui.setGUICommand(m_ctx.ui.getGUICommand() == &b ? nullptr : &b);
				return true;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_DO_WEAPON);
			m.appendInteger(b.m_weaponSlot);
			m.appendInteger(b.m_maxShotsToFire);
			return true;
		}
		case GUI_COMMAND_BLOODTHIRSTY:
			m_ctx.ui.setGUICommand(&b);
			return true;
		case GUI_COMMAND_SPECIAL_POWER:
		{
			// lane HERO-1 (S-850): a hero ability. A button that needs a target waits for it (GUICommandTranslator), the others go out at once:
			// MSG_DO_SPECIAL_POWER { int id, int options, objectID source }
			const SpecialPowerTemplate *t = specialPowerOf(b);
			if (!t)
			{
				++m_unported["SPECIAL_POWER (no template)"];
				return false;
			}
			// RW 0x940877: the same Options & 0x227 gate as FIRE_WEAPON, including
			// context-only buttons handled by the special-power dispatcher.
			if (b.hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT | COMMAND_OPTION_NEED_TARGET_POS
					| COMMAND_OPTION_CONTEXTMODE_COMMAND))
			{
				m_ctx.ui.setGUICommand(m_ctx.ui.getGUICommand() == &b ? nullptr : &b);
				return true;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_DO_SPECIAL_POWER);
			m.appendInteger((int)t->getID());
			m.appendInteger((int)b.m_options);
			m.appendObjectID(obj->getID());
			return true;
		}
		case GUI_COMMAND_CASTLE_UNPACK:
			m_ctx.stream.append(MSG_CASTLE_UNPACK); // lane BACKLOG2: RW 0x940AB6 (no arguments)
			return true;
		case GUI_COMMAND_CASTLE_PACK:
			m_ctx.stream.append(MSG_CASTLE_PACK); // RW 0x940AAC
			return true;
		case GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT:
		{
			// RW 0x940AC0 .. 0x940AE9: the button's template (RW 0x75D1DC; none: nothing), its id (+ 0x5E8) as the integer argument
			const ThingTemplate *tt = b.getThingTemplate();
			if (!tt)
			{
				return false;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_CASTLE_UNPACK_EXPLICIT_OBJECT);
			m.appendInteger((int)tt->getTemplateID());
			return true;
		}
		default:
			++m_unported[std::to_string(b.m_command)];
			return false;
	}
}

bool ControlBar::cancelQueued(int slot, bool inPalantir, bool shift)
{
	const ControlBarButton *cb = find(slot, inPalantir);
	if (cb && cb->button && cb->button->m_command == GUI_COMMAND_REVIVE)
	{
		// lane HUD-7, TARGET FACTS (RotWK processCommandUI RW 0x940435: the jump table sends REVIVE (0x2E) to the UNIT_BUILD case RW 0x940B0E, which takes the
		// button's record index (+ 0xC0) instead of a template; caveat S-001): the right press (RW 0x940C57) cancels only for a CANCELABLE button (+ 0x1F & 0x80)
		// of an object the local player controls (RW 0x68B678) that has a production interface (RW 0x68C327); MSG_CANCEL_UNIT_CREATE (0x418) { bool byIndex
		// (true), int the record index, bool all (false: RW 0x940CC6 leaves it clear for a REVIVE) }. RotWK does not ask whether the record is queued: the
		// logic's cancel by index (RW 0x8A047B) finds nothing then
		Object *obj = m_ctx.logic.findObjectByID(m_source);
		if (!cb->button->hasOption(COMMAND_OPTION_CANCELABLE) || cb->reviveIndex < 0 || !obj || obj->getControllingPlayer() != m_ctx.localPlayer()
			|| !obj->getProductionUpdate())
		{
			return false;
		}
		ClientMessage &m = m_ctx.stream.append(MSG_CANCEL_UNIT_CREATE);
		m.appendBoolean(true);
		m.appendInteger(cb->reviveIndex);
		m.appendBoolean(false);
		return true;
	}
	if (cb && cb->button
		&& (cb->button->m_command == GUI_COMMAND_PLAYER_UPGRADE || cb->button->m_command == GUI_COMMAND_OBJECT_UPGRADE
			|| cb->button->m_command == GUI_COMMAND_CASTLE_UPGRADE))
	{
		// lane FBSIM-1 (FB-0037), TARGET FACTS (processCommandUI RW 0x940435, the right press: the byte argument at ebp + 0xC is 0; caveat S-001): PLAYER_UPGRADE
		// (RW 0x940CEA) and CASTLE_UPGRADE (RW 0x940EAB) jump to RW 0x940FB3, OBJECT_UPGRADE (RW 0x940D49) to RW 0x940E77. Each cancels only for a CANCELABLE
		// button (Options + 0x1C bit 31) with an upgrade (+ 0x24) of an object whose controlling player (RW 0x68B678) is the local one: MSG_CANCEL_UPGRADE
		// (0x416) { int the upgrade's mask bit (+ 0x38) } (RW 0x940AE9 -> 0x940FA9). RotWK does not ask whether the upgrade is queued (the logic's
		// cancelUpgrade RW 0x8A1140 finds nothing then) nor what the button's state is
		Object *obj = m_ctx.logic.findObjectByID(m_source);
		const UpgradeTemplate *u = TheUpgradeCenter && !cb->button->m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(cb->button->m_upgradeName) : nullptr;
		if (!u || !cb->button->hasOption(COMMAND_OPTION_CANCELABLE) || !obj || obj->getControllingPlayer() != m_ctx.localPlayer())
		{
			return false;
		}
		ClientMessage &m = m_ctx.stream.append(MSG_CANCEL_UPGRADE);
		m.appendInteger(u->getMaskBit());
		return true;
	}
	if (!cb || !cb->button || cb->button->m_command != GUI_COMMAND_UNIT_BUILD || cb->queued <= 0)
	{
		return false;
	}
	Object *obj = m_ctx.logic.findObjectByID(m_source);
	const ThingTemplate *tt = cb->button->getThingTemplate();
	if (!tt || !cb->button->hasOption(COMMAND_OPTION_CANCELABLE) || !obj || obj->getControllingPlayer() != m_ctx.localPlayer()
		|| !obj->getProductionUpdate())
	{
		return false;
	}
	// MSG_CANCEL_UNIT_CREATE, RotWK layout (RW 0x77A844): { byte byIndex, int templateId, byte all }
	ClientMessage &m = m_ctx.stream.append(MSG_CANCEL_UNIT_CREATE);
	m.appendBoolean(false);
	m.appendInteger((int)tt->getTemplateID());
	// RW 0x940CB7 .. 0x940CE2: the u16 at ThingTemplate + 0x5F0 is MaxSimultaneousOfType, not HERO.
	m.appendBoolean(shift && BuildAssistant::maxSimultaneousOfType(*tt) == 0);
	return true;
}


bool ControlBar::rightPressButton(int slot, bool inPalantir, bool shift)
{
	const ControlBarButton *cb = find(slot, inPalantir);
	Object *obj = m_ctx.logic.findObjectByID(m_source);
	if (!cb || !cb->button || !obj)
	{
		return false;
	}
	// lane OWNERFIX-3 (INFERENCE, stop S-5040): another player's command set is shown disabled (ControlBar::foreignDisable, RW 0x94397F); a CANCELABLE
	// button's right press would still reach processCommandUI for that object, whose messages the logic refuses for a non-owner: none is sent
	if (m_foreignView)
	{
		return false;
	}
	const CommandButton &b = *cb->button;
	// lane AUTOCAST-1, the command button clip's right press (RW 0x9D6E54: TheAptWindowManager + 0x318 == 2; the state _up goes to RW 0x9D288A, any other
	// to RW 0x9D28D4): _disabled (1) and _cantAfford (3) reach the control bar only for a CANCELABLE button (CommandButton + 0x1F & 0x80); every other state
	// (_notReady included) sends GWM_RIGHT_UP (0x4009) to the window's owner, ControlBar's system callback, i.e. processCommandUI(button, 0)
	const bool cancelable = b.hasOption(COMMAND_OPTION_CANCELABLE);
	if ((cb->state == ButtonState::Restricted || cb->state == ButtonState::CantAfford || cb->state == ButtonState::Purchased) && !cancelable) // lane HUD-7: Purchased is a _cantAfford clip
	{
		return false;
	}
	// processCommandUI (RW 0x940435), the right press: nothing for a button that is not clickable (+ 0x105); "autoable" (the byte at ebp + 0xB) is
	// AutoAbility (+ 0x10C) or CANCELABLE. A FIRE_WEAPON / SPECIAL_POWER button whose command is not available (RW 0x75CC3E) stops there unless autoable;
	// the other commands' availability (RW 0x942733) is asked only when not autoable, and a right press stops after it either way
	// INFERENCE: the NONPRESSABLE refusal (RW 0x940435 does not test the bit; retail shows such a button _static, RW 0x9D2BFA), as pressButton
	if (!b.m_isClickable || b.hasOption(COMMAND_OPTION_NONPRESSABLE))
	{
		return false;
	}
	const bool autoable = b.m_autoAbility || cancelable;
	if (!autoable)
	{
		// RW 0x940877 .. 0x94089C: a button that needs a target (Options & 0x227) leaves the GUI command mode (setGUICommand(null)); INFERENCE: the
		// availability tests before it (RW 0x75CC3E / 0x942733) are not repeated here: leaving the mode on a press of an unavailable button is harmless
		if (b.m_options & 0x227u)
		{
			m_ctx.ui.setGUICommand(nullptr);
		}
		return false;
	}
	switch (b.m_command)
	{
		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_SPECIAL_POWER_TOGGLE:
		{
			// RW 0x941511 (case 0x18) / RW 0x9418F7 (case 0x25): MSG_DO_AUTO_ABILITY { int the power's id (RW 0x688D3C + 0x14), objectID the object (+ 0x74) }
			const SpecialPowerTemplate *t = specialPowerOf(b);
			if (!t)
			{
				++m_unported["AUTO_ABILITY (no template)"];
				return false;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_DO_AUTO_ABILITY);
			m.appendInteger((int)t->getID());
			m.appendObjectID(obj->getID());
			++m_autoToggles;
			return true;
		}
		case GUI_COMMAND_FIRE_WEAPON:
		{
			// RW 0x941391 (case 0x17): MSG_DO_AUTO_ABILITY_WEAPON { int WeaponSlot (+ 0x80), objectID the object }
			ClientMessage &m = m_ctx.stream.append(MSG_DO_AUTO_ABILITY_WEAPON);
			m.appendInteger(b.m_weaponSlot);
			m.appendObjectID(obj->getID());
			++m_autoToggles;
			return true;
		}
		case GUI_COMMAND_UNIT_BUILD:
		case GUI_COMMAND_REVIVE: // lane HUD-7: the same case in RotWK (RW 0x940B0E)
			return cancelable && cancelQueued(slot, inPalantir, shift); // RW 0x940C59 .. 0x940C70 (the production cancel, lane HUD-6)
		case GUI_COMMAND_PLAYER_UPGRADE:
		case GUI_COMMAND_OBJECT_UPGRADE:
		case GUI_COMMAND_CASTLE_UPGRADE:
			return cancelQueued(slot, inPalantir, shift); // lane FBSIM-1: the upgrade cancel RW 0x940E77 / 0x940FB3
		default:
			return false; // INFERENCE / not ported here: the other right press cases stay the left press handlers' (S-3460)
	}
}
