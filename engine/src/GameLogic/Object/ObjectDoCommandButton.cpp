// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Object/Object.cpp: Object::doCommandButton,
// doCommandButtonAtObject, doCommandButtonAtPosition are the donors of the switch).
//
// Lane MODS-3: Object::doCommandButton and its two target forms, the command button a module, a script or the AI presses on one object. TARGET FACTS (RotWK
// game.dat, caveat S-001; BFME2 decomp GameLogic/Object/Object.cpp Object::doCommandButton 0x00296749 is the second source, tier B: RotWK renumbers and adds
// cases; every case below is read from the RotWK disassembly):
//
// doCommandButton(button, cmdSource, fromAutoAbility) (RW 0x696FD2, ret 0xC):
//   * the disabled gate: with `fromAutoAbility`, a disabled object (Object + 0x1C8, RW 0x9325B4) goes on only when the disabled types its AutoAbilityBehavior
//     (RW 0x68BDA5 with the key of "AutoAbilityBehavior" RW 0xC0BD98) processes (update interface slot 1) meet its disabled bits (RW 0x6252E4); without it, a
//     disabled object goes on only with the status SUMMONING_REPLACEMENT (0x66: Object + 0xA0 bit 6). Then the AI (Object + 0x260) is read; a null button
//     does nothing. The command type (button + 0x14):
//   * FOUNDATION_CONSTRUCT (1), UNIT_BUILD (3), DOZER_CONSTRUCT (53) (RW 0x6977C5): the button's template (RW 0x75D1DC) and the first production queue (RW
//     0x68C327(0)); with both, queueCreateUnit(template, -1, requestUniqueUnitID(), -1, 0, "", 0) (queue vtable + 0x20, + 0x8);
//   * PLAYER_UPGRADE / OBJECT_UPGRADE / CASTLE_UPGRADE (6 .. 8) (RW 0x6970D2): when RW 0x695338 answers 0 (upgradeButtonError below), the first queue's
//     queueUpgrade(button + 0x24) (vtable + 0xC; RW reads the queue unchecked: RW 0x695338 already refused without one);
//   * STOP (14): with an AI, aiIdle(cmdSource) (AI + 0x20 -> RW 0x5E821A);
//   * SPECIAL_POWER (24) and SPELL_BOOK (38) (RW 0x6971CF): with a power (button + 0x44), Object::doSpecialPower (RW 0x68E5A3) with the options button + 0x1C |
//     0x40000 (| 0x20000000 with `fromAutoAbility`) and `force` = (cmdSource == CMD_FROM_SCRIPT);
//   * SWITCH_WEAPON (27): Object::setWeaponLock(button + 0x80, LOCKED_PERMANENTLY) (RW 0x69121A);
//   * CASTLE_UNPACK (33) / CASTLE_PACK (34) / CASTLE_UNPACK_EXPLICIT_OBJECT (50) (RW 0x6972CE / 0x697282 / 0x6975C4): the object's CastleBehavior (RW 0x68BDA5
//     with RW 0x797AC1's key); unpack: canUnpack(true) (RW 0x7983E3), isPlayerAllowedToPackOrUnpack(the controlling player, cmdSource) (RW 0x79A472: the
//     second argument only reaches a debug log) and canAffordUnpack (RW 0x79969F): initiateUnpack(false, null) (RW 0x79C17D); pack: unpacked (RW 0x797B63) and
//     allowed: abandon (RW 0x79CB47); explicit: the button's template, canUnpack(true), allowed, canAffordUnpackOf(player, template) (RW 0x797B6D):
//     initiateUnpack(false, template);
//   * TOGGLE_GATE (44) (RW 0x697479): the GateOpenAndCloseBehavior (key RW 0xC0A0A0), else the GateProxyBehavior (RW 0xC05610) (the module's interface at
//     module - 4); when settled (slot 10): open (slot 6) -> the selection's voice response 0x439 (RW 0x8DEDBB on TheInGameUI's selected drawables, a client
//     sound) and close (slot 8); else the voice 0x437 and open (slot 7);
//   * TOGGLE_WEAPONSET (45) (RW 0x69740E): WeaponSetToggle::toggleFromCommandButton (lane AIPOW-1);
//   * SET_STANCE (58) (RW 0x6976E3): the StancesBehavior (RW 0x861E6E / 0x68BDA5); with one, stances[0] (RW 0x75D324, unchecked; RW logs an empty list and a
//     stance outside 0 .. 5 as debug messages) goes to setStance (RW 0x8620DD);
//   * every other type does nothing (no case in the switch).
// doCommandButtonAtObject(button, target, cmdSource, fromAutoAbility) (RW 0x697890, ret 0x10): the same auto-ability gate, but without the flag a disabled
// object does nothing (no SUMMONING_REPLACEMENT exception); STOP (14): aiIdle; FIRE_WEAPON (23): see below; SPECIAL_POWER / SPELL_BOOK: Object::
// doSpecialPowerAtObject (RW 0x68E67A) with the same options; HIJACK_VEHICLE (29): with an AI, aiEnterObject(target, cmdSource) (AI + 0x20 -> RW 0x66C5A4).
// doCommandButtonAtPosition(button, pos, cmdSource, fromAutoAbility) (RW 0x6979D9, ret 0x10): a disabled object does nothing (the flag only reaches the
// options); ATTACK_MOVE (10): see below; STOP (14): aiIdle; FIRE_WEAPON (23): see below; SPECIAL_POWER / SPELL_BOOK: Object::doSpecialPowerAtLocation (RW
// 0x68E754); DOZER_CONSTRUCT (53): see below; WAKE_AUTO_PICKUP (49): AutoPickUpUpdate::wakeAutoPickUp (RW 0x68C8DE, lane MODCOMPAT-MOD1).
//
// NOT PORTED (stop S-5305, noted by name when met; nothing happens): the cases that append a message to the local client's message stream from inside the
// logic (TheMessageStream RW 0xDE6398 slot 0x48: EXIT_CONTAINER 16 -> 0x41D, EVACUATE 17 -> 0x41E with TheInGameUI slot 0xBC and the voice, HORDE_TOGGLE_
// FORMATION 36 -> 0x453, ONE_RING 39 -> 0x454, CREW_EVACUATE 40 -> 0x455, CLOSE_GATE 42 -> 0x439, OPEN_GATE 43 -> 0x437, HORDE_SET_FORMATION 52 -> 0x460);
// FIRE_WEAPON 23 (RW 0x68C91B writes Object + 0x478, an unported field, then the lock and the AI's attack); TOGGLE_WEAPON 35 (RW 0x69720E, the weapon slot
// rule RW 0x75CD39 and the status hand-over RW 0x6CD142 / 0x6CA232); REVIVE 46 (RW 0x6973CB: the build index at button + 0xC0 is the control bar's run-time
// field, RW 0x75D5F5 starts it at -1; the port keeps it in the client's ControlBarButton); START_SELF_REPAIR 51 / START_NEIGHBORHOOD_REPAIR 59 / CANCEL_NEIGHBORHOOD 60 (RW 0x68C3E6: behaviour slot 0x80, no ported
// module answers it); the gate proxy (GateProxyBehavior, S-1940); the target forms' ATTACK_MOVE 10 (RW 0x696266), FIRE_WEAPON and DOZER_CONSTRUCT 53
// (TheBuildAssistant slot 0x38). Client only, counted: the gate voice responses. Inherited from SpecialPowerModules (S-5305 too): RW 0x68E5A3's horde step
// RW 0x68CB85 (a horde that can toggle its formation does so before the power) and its auto-ability disabled exception (options & 0x20000000).
//
// Mod engine feature edain-unchained.queue-ignore-cp (lane MODS-3): Edain Unchained's game.dat hooks the slot 8 calls of the build cases (RW 0x697800, and RW
// 0x697403 in REVIVE): the button's QueueIgnoreCP (+0x10D) is a global around the call, read by BuildAssistant::canMakeUnit at RW 0x79402B. Here it is the
// queueCreateUnit argument `ignoreCommandPoints`.

#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectDoCommandButton.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoPickUpUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/WeaponSetToggle.h"

#include <string>

namespace
{
const char kStopPrefix[] = "[S-5305] Object::doCommandButton (RW 0x696FD2 / 0x697890 / 0x6979D9): ";

ObjectDoCommandButton::Stats g_stats;

std::string commandName(int command)
{
	const char *n = GUICommandName(command);
	return n && *n ? n : "command " + std::to_string(command);
}

// a case that is registered as not ported: noted by name (report().stops), nothing happens
void unported(Object &obj, const CommandButton &b, const char *form, const char *what)
{
	++g_stats.unported;
	obj.logic().noteStop(std::string(kStopPrefix) + form + " " + commandName(b.m_command) + " (" + what + ") is not ported: the button does nothing");
}

const SpecialPowerTemplate *powerOf(const CommandButton &b)
{
	// button + 0x44, resolved at parse time in retail (the port keeps the name, S-200); an unknown name is a null power
	return TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
}

const UpgradeTemplate *upgradeOf(const std::string &name)
{
	return TheUpgradeCenter && !name.empty() ? TheUpgradeCenter->findUpgrade(name) : nullptr;
}

unsigned powerOptions(const CommandButton &b, bool fromAutoAbility)
{
	unsigned options = b.m_options | 0x40000u; // RW 0x6971DD
	if (fromAutoAbility)
	{
		options |= 0x20000000u; // RW 0x6971E8
	}
	return options;
}

// RW 0x696FEA .. 0x69706C / 0x6978A6 .. 0x69791E: the auto ability's disabled types meet the object's disabled bits
bool autoAbilityProcessesDisabled(Object &obj)
{
	const UpdateModule *aa = dynamic_cast<const UpdateModule *>(obj.findModule("AutoAbilityBehavior"));
	if (!aa)
	{
		// RW reads the module's interface (module + 0x10) unchecked: a press "from the auto ability" of an object without one is no retail state
		obj.logic().reportError("Object::doCommandButton: an auto ability press on " + obj.getTemplate()->getName() +
								" (disabled), which has no AutoAbilityBehavior (RW 0x697037 reads it unchecked)");
		return false;
	}
	return (aa->getDisabledTypesToProcess() & obj.getDisabledMask()) != 0;
}

// RW 0x695338 (BFME2 decomp Object::rva00294ADD): 0 when the upgrade button may queue its upgrade, else 9
int upgradeButtonError(Object &obj, const CommandButton &b)
{
	const UpgradeTemplate *up = upgradeOf(b.m_upgradeName); // button + 0x24
	if (!up)
	{
		return 9;
	}
	ProductionUpdateInterface *pu = obj.getProductionUpdate(); // RW 0x68C363
	if (!pu || pu->isFactoryDisabled())                         // queue slot 25 (vtable + 0x64)
	{
		return 9;
	}
	static const int kWallUpgrade = ObjectTemplateInfoBuilder::kindOfIndex("WALL_UPGRADE"); // template + 0x11A & 0x40 (KindOf mask at + 0x108)
	if (kWallUpgrade >= 0 && obj.isKindOf((unsigned)kWallUpgrade))
	{
		const BodyModuleInterface *body = obj.getBodyModule(); // Object + 0x25C, read unchecked
		if (!body)
		{
			obj.logic().reportError("Object::doCommandButton: the WALL_UPGRADE object " + obj.getTemplate()->getName() + " has no body (RW 0x69537A reads it unchecked)");
			return 9;
		}
		if (body->getDamageState() == BODY_RUBBLE) // body slot 9 (vtable + 0x24) == 3
		{
			return 9;
		}
	}
	if (up->getUpgradeType() == UPGRADE_TYPE_OBJECT && (obj.hasUpgrade(up) || !obj.affectedByUpgrade(up))) // RW 0x691421 / 0x694914
	{
		return 9;
	}
	Player *player = obj.getControllingPlayer(); // RW 0x68B678
	if (!player)
	{
		return 0;
	}
	// Player + 0x5C (the player type) not human and no skirmish AI for the player (RW 0x6A950B): no NeededUpgrade test
	if (player->getPlayerType() != PLAYER_HUMAN && !obj.logic().skirmishAI().findAI(player->getPlayerIndex()))
	{
		return 0;
	}
	if (!(b.m_options & COMMAND_OPTION_NEED_UPGRADE)) // button + 0x1C & 0x40
	{
		return 0;
	}
	// NeededUpgrade (button + 0x28, a null entry skipped) with NeededUpgradeAny (+ 0x34): any -> the first held one passes; all -> an OBJECT one missing fails
	// at the end, a PLAYER one missing fails at once (RW 0x6953E9 .. 0x695473)
	const bool any = b.m_neededUpgradeAny;
	bool fail = any;
	for (const std::string &name : b.m_neededUpgrade)
	{
		const UpgradeTemplate *u = upgradeOf(name);
		if (!u)
		{
			continue;
		}
		if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			const bool has = player->hasUpgradeComplete(u); // RW 0x6AC2AF
			if (any && has)
			{
				return 0;
			}
			if (!any && !has)
			{
				return 9;
			}
		}
		else if (u->getUpgradeType() == UPGRADE_TYPE_OBJECT)
		{
			const bool has = obj.hasUpgrade(u); // RW 0x691421
			if (any && has)
			{
				return 0;
			}
			if (!any && !has)
			{
				fail = true;
			}
		}
	}
	return fail ? 9 : 0;
}

void queueBuild(Object &obj, const CommandButton &b, const ThingTemplate *tt)
{
	ProductionUpdateInterface *pu = obj.getProductionUpdate(); // RW 0x68C327(0)
	if (!pu || !tt)
	{
		return;
	}
	const ProductionID id = pu->requestUniqueUnitID(); // queue slot 2
	// queue slot 8 (RW 0x697802); QueueIgnoreCP: only the mod engine feature edain-unchained.queue-ignore-cp parses it (false otherwise)
	pu->queueCreateUnit(tt, -1, id, -1, false, std::string(), false, b.m_queueIgnoreCP);
	++g_stats.builds;
}
} // namespace

const ObjectDoCommandButton::Stats &ObjectDoCommandButton::stats()
{
	return g_stats;
}

void ObjectDoCommandButton::resetStats()
{
	g_stats = Stats{};
}

// RW 0x696FD2
void Object::doCommandButton(const CommandButton *button, CommandSourceType cmdSource, bool fromAutoAbility)
{
	static const int kSummoningReplacement = ObjectTemplateInfoBuilder::objectStatusIndex("SUMMONING_REPLACEMENT"); // 0x66 (Object + 0xA0 bit 6)
	if (getDisabledMask() != 0)
	{
		if (fromAutoAbility ? !autoAbilityProcessesDisabled(*this) : !(kSummoningReplacement >= 0 && testStatus((unsigned)kSummoningReplacement)))
		{
			return;
		}
	}
	AIUpdateInterface *ai = getAIUpdateInterface(); // Object + 0x260
	if (!button)
	{
		return;
	}
	const CommandButton &b = *button;
	++g_stats.pressed;
	switch (b.m_command)
	{
	case GUI_COMMAND_FOUNDATION_CONSTRUCT:
	case GUI_COMMAND_UNIT_BUILD:
	case GUI_COMMAND_DOZER_CONSTRUCT:
		queueBuild(*this, b, b.getThingTemplate()); // RW 0x6977C5 (the template first, RW 0x75D1DC)
		return;
	case GUI_COMMAND_PLAYER_UPGRADE:
	case GUI_COMMAND_OBJECT_UPGRADE:
	case GUI_COMMAND_CASTLE_UPGRADE:
		if (upgradeButtonError(*this, b) == 0) // RW 0x6970D5
		{
			getProductionUpdate()->queueUpgrade(upgradeOf(b.m_upgradeName)); // RW 0x6970F2 (queue slot 3)
			++g_stats.upgrades;
		}
		return;
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(cmdSource); // RW 0x69710B
		}
		return;
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(b))
		{
			SpecialPowerModules::doSpecialPower(*this, t, powerOptions(b, fromAutoAbility), cmdSource == CMD_FROM_SCRIPT); // RW 0x6971FA -> RW 0x68E5A3
			++g_stats.powers;
		}
		return;
	case GUI_COMMAND_SWITCH_WEAPON:
		ObjectWeapons::setObjectWeaponLock(*this, b.m_weaponSlot, LOCKED_PERMANENTLY); // RW 0x697337
		return;
	case GUI_COMMAND_CASTLE_UNPACK:
	case GUI_COMMAND_CASTLE_PACK:
	case GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT:
	{
		const ThingTemplate *tt = b.m_command == GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT ? b.getThingTemplate() : nullptr; // RW 0x6975C6 (before the module)
		CastleBehavior *castle = dynamic_cast<CastleBehavior *>(findModule("CastleBehavior")); // RW 0x797AC1 / 0x68BDA5
		if (!castle)
		{
			return;
		}
		Player *player = getControllingPlayer();
		if (b.m_command == GUI_COMMAND_CASTLE_PACK)
		{
			if (castle->isUnpacked() && castle->isPlayerAllowedToPackOrUnpack(player)) // RW 0x69729B / 0x6972B5
			{
				castle->abandonByPlayer(); // RW 0x6972C4
				++g_stats.castles;
			}
			return;
		}
		if (b.m_command == GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT && !tt)
		{
			return;
		}
		if (!castle->canUnpack(true) || !castle->isPlayerAllowedToPackOrUnpack(player))
		{
			return;
		}
		if (tt ? castle->canAffordUnpackOf(player, tt) : castle->canAffordUnpack(player)) // RW 0x697626 / 0x69731A
		{
			castle->initiateUnpack(false, tt); // RW 0x697639
			++g_stats.castles;
		}
		return;
	}
	case GUI_COMMAND_TOGGLE_GATE:
	{
		GateOpenAndCloseBehavior *gate = dynamic_cast<GateOpenAndCloseBehavior *>(findModule("GateOpenAndCloseBehavior")); // RW 0x6974B1
		if (!gate)
		{
			if (findModule("GateProxyBehavior")) // RW 0x6974D6
			{
				unported(*this, b, "doCommandButton", "a GateProxyBehavior gate, S-1940");
			}
			return;
		}
		if (!gate->isSettled()) // slot 10
		{
			return;
		}
		++g_stats.voiceResponsesNotPlayed; // RW 0x69751B / 0x69753B: the selection's voice response 0x439 / 0x437 (client sound)
		if (gate->isOpen())
		{
			gate->close(); // slot 8
		}
		else
		{
			gate->open(); // slot 7
		}
		++g_stats.gates;
		return;
	}
	case GUI_COMMAND_TOGGLE_WEAPONSET:
		WeaponSetToggle::toggleFromCommandButton(*this); // RW 0x69740E (lane AIPOW-1)
		++g_stats.toggles;
		return;
	case GUI_COMMAND_SET_STANCE:
	{
		StancesBehavior *s = StancesBehavior::of(*this); // RW 0x861E6E / 0x68BDA5
		if (!s)
		{
			return;
		}
		// RW 0x697759: stances[0] read unchecked (RW 0x75D324); an empty list or a stance outside 0 .. 5 are debug messages in retail, then setStance takes
		// the value anyway: neither is a defined retail state, so the port names it and does not set it
		if (b.m_stances.empty() || b.m_stances[0] < 0 || b.m_stances[0] >= STANCE_COUNT)
		{
			logic().reportError("Object::doCommandButton: SET_STANCE button " + b.m_name + " names no stance in 0 .. 5 (RW 0x697759 / 0x697767)");
			return;
		}
		s->setStance((StanceType)b.m_stances[0], "Object::doCommandButton:RW_6977BB");
		return;
	}
	case GUI_COMMAND_EXIT_CONTAINER:
	case GUI_COMMAND_EVACUATE:
	case GUI_COMMAND_HORDE_TOGGLE_FORMATION:
	case GUI_COMMAND_ONE_RING:
	case GUI_COMMAND_CREW_EVACUATE:
	case GUI_COMMAND_CLOSE_GATE:
	case GUI_COMMAND_OPEN_GATE:
	case GUI_COMMAND_HORDE_SET_FORMATION:
		unported(*this, b, "doCommandButton", "a message appended to the local client's message stream from the logic");
		return;
	case GUI_COMMAND_FIRE_WEAPON:
		unported(*this, b, "doCommandButton", "Object + 0x478, RW 0x68C91B");
		return;
	case GUI_COMMAND_TOGGLE_WEAPON:
		unported(*this, b, "doCommandButton", "RW 0x69720E");
		return;
	case GUI_COMMAND_REVIVE:
		unported(*this, b, "doCommandButton", "the control bar's build index at button + 0xC0, RW 0x6973CB");
		return;
	case GUI_COMMAND_WAKE_AUTO_PICKUP:
	{
		// lane MODCOMPAT-MOD1: RW 0x697397 .. 0x6973C1: the number is 1 for OPTION_ONE (0x2000), 2 for OPTION_TWO (0x4000), 3 for OPTION_THREE (0x8000),
		// else 0 (EBX, zeroed at RW 0x696FE0); RW 0x68C8DE wakes the AutoPickUpUpdates of that number
		const int number = (b.m_options & 0x2000u) ? 1 : (b.m_options & 0x4000u) ? 2 : (b.m_options & 0x8000u) ? 3 : 0;
		AutoPickUpUpdate::wakeAutoPickUp(*this, number);
		return;
	}
	case GUI_COMMAND_START_SELF_REPAIR:
	case GUI_COMMAND_START_NEIGHBORHOOD_REPAIR:
	case GUI_COMMAND_CANCEL_NEIGHBORHOOD:
		unported(*this, b, "doCommandButton", "behaviour slot 0x80, RW 0x68C3E6");
		return;
	default:
		return; // no case in RW 0x696FD2's switch
	}
}

// RW 0x697890
void Object::doCommandButtonAtObject(const CommandButton *button, Object *target, CommandSourceType cmdSource, bool fromAutoAbility)
{
	if (getDisabledMask() != 0 && !(fromAutoAbility && autoAbilityProcessesDisabled(*this)))
	{
		return;
	}
	AIUpdateInterface *ai = getAIUpdateInterface();
	if (!button)
	{
		return;
	}
	const CommandButton &b = *button;
	++g_stats.pressed;
	switch (b.m_command)
	{
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(cmdSource); // RW 0x6979C3
		}
		return;
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(b))
		{
			SpecialPowerModules::doSpecialPowerAtObject(*this, t, target, powerOptions(b, fromAutoAbility), cmdSource == CMD_FROM_SCRIPT); // RW 0x697978 -> 0x68E67A
			++g_stats.powers;
		}
		return;
	case GUI_COMMAND_HIJACK_VEHICLE:
		if (ai)
		{
			ai->aiEnterObject(target, cmdSource); // RW 0x69798C -> RW 0x66C5A4
		}
		return;
	case GUI_COMMAND_FIRE_WEAPON:
		unported(*this, b, "doCommandButtonAtObject", "Object + 0x478, RW 0x68C91B");
		return;
	default:
		return;
	}
}

// RW 0x6979D9
void Object::doCommandButtonAtPosition(const CommandButton *button, const Coord3D &pos, CommandSourceType cmdSource, bool fromAutoAbility)
{
	if (getDisabledMask() != 0) // RW 0x6979E5: no exception
	{
		return;
	}
	AIUpdateInterface *ai = getAIUpdateInterface();
	if (!button)
	{
		return;
	}
	const CommandButton &b = *button;
	++g_stats.pressed;
	switch (b.m_command)
	{
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(cmdSource); // RW 0x697AC5
		}
		return;
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(b))
		{
			SpecialPowerModules::doSpecialPowerAtLocation(*this, t, pos, powerOptions(b, fromAutoAbility), cmdSource == CMD_FROM_SCRIPT); // RW 0x697A8A -> 0x68E754
			++g_stats.powers;
		}
		return;
	case GUI_COMMAND_ATTACK_MOVE:
		unported(*this, b, "doCommandButtonAtPosition", "aiAttackMoveToPosition RW 0x696266");
		return;
	case GUI_COMMAND_FIRE_WEAPON:
		unported(*this, b, "doCommandButtonAtPosition", "Object + 0x478, RW 0x68C91B");
		return;
	case GUI_COMMAND_DOZER_CONSTRUCT:
		unported(*this, b, "doCommandButtonAtPosition", "TheBuildAssistant slot 0x38, RW 0x697A57");
		return;
	default:
		return;
	}
}
