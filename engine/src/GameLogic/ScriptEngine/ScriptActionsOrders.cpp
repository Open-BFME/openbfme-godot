// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine/ScriptActions.cpp).
//
// ScriptActions, lane SCRIPT-4: the guard / idle / stand-ground orders, the team moves to the nearest object, recruiting, the garrison of a named
// building, the unit reference and the timed model condition (RW 0x7CAFA5's cases), by retail use. See ScriptActions.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each body read in Ghidra next to the BFME2 decomp's helper; the jump table RW 0x7CF857):
//   * TEAM_IDLE_FOR_FRAMECOUNT (200) / TEAM_GUARD_FOR_FRAMECOUNT (199) -> RW 0x7CDAC2 (seconds 0), TEAM_IDLE_FOR_SECONDS (414) / TEAM_GUARD_FOR_SECONDS (413)
//     -> RW 0x7CED62 (seconds 1): both RW 0x7C1704 doTeamIdleForFramecount (BFME2 ScriptActions_doTeamIdleForFramecount.cpp, tier A). The GUARD forms
//     idle (ZH has the same shared case; the guard body RW 0x7C9E48 has no caller). The team (RW 0x759FDA), its AI group (RW 0x70042A / 0x7A3D3D), the
//     group's centre (RW 0x76EDA7, unused), groupIdle(CMD_FROM_SCRIPT) (RW 0x771F49), then setSequentialTimer(team, seconds ? n * 5 : n) (RW 0x60396C;
//     5 = LOGICFRAMES_PER_SECOND RW 0xD9F608, imul).
//   * groupIdle RW 0x771F49 (BFME2 0x36FC23, tier B), per member: with an AI (+ 0x260) aiIdle(source) (RW 0x5E821A; for a player's source also + 0x3CC and
//     the mood check: not a script's); without one the contain's passengers with an AI idle (vslot 0x110, callback RW 0x771F28); then the member's spawn
//     interface (RW 0x68C3A3) slot 8 orderSlavesToGoIdle (RW 0x863850).
//   * UNIT_IDLE_FOR_FRAMECOUNT (198) / _SECONDS (412) RW 0x7C9DF6: the unit (RW 0x75A3D8) with an AI: aiIdle(CMD_FROM_SCRIPT), setSequentialTimer(unit, n)
//     (RW 0x603939). UNIT_GUARD_FOR_FRAMECOUNT (197) / _SECONDS (411) RW 0x7C9D7D: aiGuardPosition(its position, GUARDMODE_NORMAL, CMD_FROM_SCRIPT)
//     (RW 0x771790) instead of the idle.
//   * NAMED_GUARD (58) RW 0x7C9306: the unit with an AI leaves its group (RW 0x68C6EE: the port keeps no AI groups) and guards its position.
//     TEAM_GUARD (59) RW 0x7C9367 (BFME2 ScriptActions_doUnitGuardForFramecount.cpp:166): every member in list order but a HORDE_MEMBER (status 0x26) or an
//     UNDER_CONSTRUCTION one (status 2), with an AI, guards its own position (the raw members: not the AI group).
//   * UNIT_GUARD_POSITION (452) RW 0x7C9400: the waypoint (TheTerrainLogic vslot 0x88), the unit with an AI, aiGuardPosition(the waypoint). TEAM_GUARD_POSITION
//     (206) RW 0x7C01B1: the waypoint, the team, its AI group -> RW 0x7726F5: every member with an AI guards the waypoint.
//   * UNIT_STAND_GROUND (500) RW 0x7C6B82 / TEAM_STAND_GROUND (501) RW 0x7C6C2B (BFME2 ScriptActions_doStandGround.cpp, tier A): Object::setStatus(0x44
//     STAND_GROUND, the bool) on the unit / every raw member; a HORDE (template + 0x115 bit 5) with a horde interface (RW 0x68C866) also sets it on each
//     member of its list (slot 0x10C).
//   * TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE (393) RW 0x7C30F7 / _OWNED_BY_PLAYER (394) RW 0x7C31FF (BFME2 ScriptActions_closestOfObjectTypes.cpp, tier A): the
//     team, its centre over EVERY member (RW 0x7A0ECD: dead ones too, the sums in member order, times 1 / n), the type (an object list, else a template:
//     neither -> nothing); the closest object (the partition's getClosestObject RW 0xA39090, FROM_CENTER_2D, 1e6, the type filter, for 394 the owner);
//     an object list takes per type the strictly nearest by the squared 3D distance (RW 0x7C2F85); 394 walks the players of the mask in index order and
//     keeps the strictly nearest by the 3D length (a tie: the lower index); then the team's AI group moves to the object's position (RW 0x774897,
//     CMD_FROM_SCRIPT).
//   * TEAM_RECRUIT_UNITS_FROM_TEAM (398) RW 0x7C2C7A -> Team RW 0x7A449F (BFME2 ScriptGlueRva003C21E5.cpp / TeamGetControllingPlayer.cpp:974): the
//     destination team (created when missing), the source team; the template and the object list of the type (neither required); the destination's
//     centre over its live members (RW 0x7A02E1), once; up to the count: over the source prototype's teams, newest first (prototype + 0x334), their
//     members in order: of the type (RW 0x73D5C2, or the list, RW 0x778EA0), not DISABLED_HELD (+ 0x1C8 bit 3), recruitable (AI + 0x3BE): the nearest by
//     the squared 2D distance, a tie going to the later one (<=); it joins the destination (Object::setTeam RW 0x69954A).
//   * SET_UNIT_REFERENCE (376) RW 0x7C2AAC(ref, unit, 0): the unit (RW 0x75A3D8); the side's reference map (RW 0x608397) and the named cache (RW 0x60A1D5).
//     SET_UNIT_REFERENCE_TO_REFERENCE (378) is RW 0x7C2AAC(ref, source, 1) -> RW 0x608504 (ScriptActionsUnits.cpp, lane MODCOMPAT-AI).
//   * TEAM_GARRISON_SPECIFIC_BUILDING (106) / _INSTANTLY (570) RW 0x7C7091 (BFME2 ScriptActions_garrison.cpp): the team, the named building with a contain
//     (+ 0x258). Instantly: every raw member but a HORDE_MEMBER -> RW 0x7C5DCE: isValidContainerFor(member, 1, 0) (slot 0x98), addToContain (slot 0x9C),
//     and a HORDE whose contain has a horde interface (slot 0x7C) -> RW 0x7C562E: each member of its list (slot 0x10C) leaves the horde for the garrison
//     (slot 0xA8) and is added. Otherwise: the contain's entered-player mask (slot 0x144, RW 0x5F2DC6); a non-STRUCTURE or an entered contain must have been
//     entered by the team's player (the mask equals 1 << its index); then the AI group enters (RW 0x7724BE: aiEnterObject(building, CMD_FROM_SCRIPT), AI
//     command 0x17, RW 0x66C5A4).
//   * NAMED_GARRISON_SPECIFIC_BUILDING (110) / _INSTANTLY (567) RW 0x7C9925 (BFME2 ScriptActions_garrison.cpp doUnitGarrisonSpecificBuilding, tier B): the
//     unit with an AI, the building with a contain; the entered mask as for the team, but the exception is a GARRISON (template + 0x116 bit 6) nobody
//     entered; isValidContainerFor(unit, 1, !instantly); instantly the unit (and a horde's members, RW 0x7C562E) goes in, else aiEnterObject(building,
//     CMD_FROM_SCRIPT).
//   * SET_PLAYER_MONEY_TO_COUNTER (404) RW 0x7C3655: the money of the mask's players summed into the counter. SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER (420)
//     RW 0x7C38F6: the one player of the mask (RW 0x6A85B6), its ScoreKeeper's kills of the raw type name (RW 0x79E4D8) into the counter.
//   * UNIT_SET_MODELCONDITION_FOR_DURATION (434) RW 0x7C6404 (BFME2 ScriptActions_modelConditionTimed.cpp, tier A): the unit; the model condition whose name
//     equals the parameter (case kept, the 591 names RW 0xD9FAD8); frames = _ftol2(5 * seconds) (x87); Object RW 0x695209(bit, frames, fraction) (BFME2
//     ObjectModelConditionFraction.cpp): the horde of the object (RW 0x693A1A(0)) with a horde interface gives its member list: wanted =
//     cvttss2si(count * fraction + 0.5f), at least 1 and at most count; every member draws GameLogicRandomValue(1, remaining--) (RW 0x6D328E, line 0xC48)
//     and takes the condition while the draw <= wanted (each taker lowers wanted); without a horde interface the object itself takes it. Taking it:
//     frames 0 sets the bit for good (when clear), else the SMCHelper's timed bit (RW 0x8E2C0F).
//   * NAMED_USE_COMMANDBUTTON_ABILITY (244) RW 0x7C6243 / _ON_NAMED (202) RW 0x7C62BF / _AT_WAYPOINT (203) RW 0x7C6363 (BFME2 ScriptActions_Rva003C57A9Do.cpp,
//     tier B: RotWK walks 33 slots, bound 0x21, BFME2 32): the unit (RW 0x75A3D8; the target by name RW 0x75A243, the waypoint TheTerrainLogic vslot 0x88:
//     each required), its command set (RW 0x69156B -> 0x71EFA2); every slot (RW 0x80C837) whose button's name equals the parameter (exact) runs
//     Object::doCommandButton[AtObject / AtPosition](button, [target / the waypoint], CMD_FROM_SCRIPT, 0): a button in two slots runs twice.
//   * TEAM_USE_COMMANDBUTTON_ABILITY (245) RW 0x7C11EE / _ON_NAMED (242) RW 0x7C1254 / _AT_WAYPOINT (243) RW 0x7C12DC (BFME2 ScriptGlueRva003C075B.cpp,
//     tier A): the team, [the target / the waypoint], the button by name (RW 0x71D6EA, its final override; no command set test), the team's AI group:
//     AIGroup RW 0x76F9FF / 0x76FA59 / 0x76FA2A run it on every member in the group's order (auto 0).
//   * PLAYER_FIRE_SPECIAL_POWER_AT_WAYPOINT (571) RW 0x7C4C50 (BFME2 ScriptActions_doFireSpecialPowerAtWaypoint.cpp, tier A): the waypoint, the power by name (RW 0x69C146);
//     RW 0x7C112E: the players of the mask in index order, each one's team prototypes (Player + 0x34C), their teams from the list head (+ 0x334, the newest
//     first), the members in order: the FIRST one with a module of the power (RW 0x68C26D) fires it at the position through the module's own slot 0x30
//     with options 0x40000 exactly (no Object gate: a disabled caster fires; no readiness test). NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT (171) RW 0x7C4A99 /
//     _AT_NAMED (172) RW 0x7C4CDB (tier A): the unit's module, slot 0x30 at the waypoint / slot 0x2C at the named target, options 0x40000.
//   * TEAM_HUNT_WITH_COMMAND_BUTTON (282) RW 0x7C46A0 (lane MODCOMPAT-MOD1; BFME2 ScriptActions_doTeamHuntWithCommandButton.cpp, same shape, 32 slots):
//     the team (RW 0x759FDA), the button by name (RW 0x71D6EA); FIRE_WEAPON 0x17 / SWITCH_WEAPON 0x1B go on; SPECIAL_POWER 0x18 / SPELL_BOOK 0x26 with
//     a power go on when the button needs a target object (Options & 7), else the debug line "ERROR-Team hunt with command button - cannot hunt with
//     ability <name>"; without a power nothing; any other command that line. Then every member of the team's list (RW 0x66362D) with an AI
//     (Object + 0x260) whose command set (RW 0x69156B -> 0x71EFA2) holds that very button in one of its 33 slots (RW 0x80C837, pointer equality) gets
//     its CommandButtonHuntUpdate's setCommandButton(name) (RW 0x68BDA5 by name key, RW 0x898EE8); a member without the button or the module gets the
//     debug line "Error - Team hunt with command button - unit type '<template>' is not valid for ability <name>" / "... requires
//     CommandButtonHuntUpdate in .ini definition to hunt with <name>".
//
// INFERENCE / NOT PORTED: the AI recruitable flag (AI + 0x3BE) is read as true (ZH's default; its only writer, the panel flag AI_Recruitable, is not
// ported, S-1186); (lane BFME2CAMP1 ported the area guards UNIT_ / TEAM_GUARD_AREA[_FROM_POSITION] and the squad attacks TEAM_ / NAMED_ATTACK_TEAM,
// AIHunt.cpp); the command-button types that Object::doCommandButton* lacks (fire weapon, the message-stream commands ..., stop S-5305) are noted by
// name (S-1190).

#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "Common/PlayerHeroList.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/Economy.h"

#include "Common/GameCommon.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/ScoreKeeper.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectCommandButton.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"

#include <map>
#include <string>
#include <vector>

namespace
{

const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}

bool isKindOfName(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

bool hasStatus(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	return b >= 0 && o.testStatus((unsigned)b);
}

bool endsWith(const std::string &s, const char *tail)
{
	const std::string t(tail);
	return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

// the frames of a FRAMECOUNT / SECONDS parameter: n, or n * LOGICFRAMES_PER_SECOND (RW 0xD9F608, imul: 32-bit wrap)
std::int32_t framesOf(const ScriptActionRec &a, bool seconds)
{
	const std::int32_t n = param(a, 1).intValue;
	return seconds ? (std::int32_t)((std::uint32_t)n * (std::uint32_t)LOGICFRAMES_PER_SECOND) : n;
}

// RW 0x771F49 groupIdle for a script's group (see the header)
void scriptGroupIdle(const std::vector<Object *> &group, CommandSourceType source)
{
	for (Object *o : group)
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->aiIdle(source);
		}
		else if (ContainModuleInterface *c = o->getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				const std::vector<Object *> passengers(items->begin(), items->end());
				for (Object *p : passengers)
				{
					if (p && p->getAIUpdateInterface())
					{
						p->getAIUpdateInterface()->aiIdle(source); // RW 0x771F28
					}
				}
			}
		}
		if (SpawnBehaviorInterface *s = SpawnBehaviorInterface::of(*o))
		{
			s->orderSlavesToGoIdle(source); // RW 0x68C3A3 slot 8
		}
	}
}

// RW 0x7C6B82 / 0x7C6C2B: the status on the object, and on every member of a horde's list
void setStandGround(Object &o, bool on)
{
	static const int standGround = ObjectTemplateInfoBuilder::objectStatusIndex("STAND_GROUND");
	o.setStatus((unsigned)standGround, on);
	if (!isKindOfName(o, "HORDE"))
	{
		return;
	}
	ContainModuleInterface *c = o.getContain();
	if (!c || !c->getHordeContainInterface()) // RW 0x68C866
	{
		return;
	}
	if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
	{
		const std::vector<Object *> members(items->begin(), items->end()); // slot 0x10C fills a list first
		for (Object *m : members)
		{
			m->setStatus((unsigned)standGround, on);
		}
	}
}

std::vector<ObjectID> idsOf(const std::vector<Object *> &objects)
{
	std::vector<ObjectID> ids;
	ids.reserve(objects.size());
	for (const Object *o : objects)
	{
		ids.push_back(o->getID());
	}
	return ids;
}

// RW 0x7A02E1: the mean position of the live members (not dead, not destroyed); (0, 0, 0) without one
Coord3D teamCentreLiving(const Team &t)
{
	Coord3D c{ 0.0f, 0.0f, 0.0f };
	int n = 0;
	for (const Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		if (!o->isEffectivelyDead() && !o->isDestroyed())
		{
			c.x = SimMath::sseAdd(c.x, o->getPosition()->x);
			c.y = SimMath::sseAdd(o->getPosition()->y, c.y);
			c.z = SimMath::sseAdd(o->getPosition()->z, c.z);
			++n;
		}
	}
	if (n > 0)
	{
		const float inv = SimMath::sseDiv(1.0f, SimMath::sseFromInt32(n));
		c.x = SimMath::sseMul(inv, c.x);
		c.y = SimMath::sseMul(inv, c.y);
		c.z = SimMath::sseMul(inv, c.z);
	}
	return c;
}

// the partition's closest object of the template (RW 0xA39090: FROM_CENTER_2D within 1e6), owned by `owner` when there is one
Object *closestOfTemplate(GameLogic &logic, const Coord3D &c, const ThingTemplate *tt, const Player *owner)
{
	auto typed = [tt](Object &o) { return ScriptConditions::equivalentTemplates(o.getTemplate(), tt); };
	auto owned = [owner](Object &o) { return o.getControllingPlayer() == owner; };
	PartitionFilterFn<decltype(typed)> fTyped(typed);
	PartitionFilterFn<decltype(owned)> fOwned(owned);
	if (owner)
	{
		return logic.partition().getClosestObject(c, 1000000.0f, FROM_CENTER_2D, { &fTyped, &fOwned });
	}
	return logic.partition().getClosestObject(c, 1000000.0f, FROM_CENTER_2D, { &fTyped });
}

// RW 0x7C2F85: per type of the list (a name without a template is skipped) the closest; the strictly nearest by the squared 3D distance (FLT_MAX first)
Object *closestOfList(GameLogic &logic, const Coord3D &c, const std::vector<std::string> &list, const Player *owner)
{
	Object *found = nullptr;
	float best = 3.4028235e38f;
	for (const std::string &typeName : list)
	{
		const ThingTemplate *tt = logic.things().findTemplate(typeName);
		Object *o = tt ? closestOfTemplate(logic, c, tt, owner) : nullptr;
		if (!o)
		{
			continue;
		}
		const float dx = SimMath::sseSub(c.x, o->getPosition()->x);
		const float dz = SimMath::sseSub(c.z, o->getPosition()->z);
		const float dy = SimMath::sseSub(c.y, o->getPosition()->y);
		const float d = SimMath::sseAdd(SimMath::sseAdd(SimMath::sseMul(dz, dz), SimMath::sseMul(dy, dy)), SimMath::sseMul(dx, dx));
		if (d < best)
		{
			best = d;
			found = o;
		}
	}
	return found;
}

// RW 0x695209 (see the header): `apply` for the object, or for the drawn share of its horde's members
template <typename F>
void applyToFractionOfHorde(GameLogic &logic, Object &o, float fraction, F apply)
{
	Object *horde = o.getHordeObject(false); // RW 0x693A1A(0)
	ContainModuleInterface *ct = horde ? horde->getContain() : nullptr;
	const ContainModuleInterface::ContainedItemsList *items = ct && ct->getHordeContainInterface() ? ct->getContainedItemsList() : nullptr;
	if (!items)
	{
		apply(o);
		return;
	}
	const std::vector<Object *> members(items->begin(), items->end());
	int remaining = (int)members.size();
	int wanted = SimMath::cvttss2si(SimMath::sseAdd(SimMath::sseMul(SimMath::sseFromInt32(remaining), fraction), 0.5f)); // RW 0xBD869C
	if ((wanted < remaining ? wanted : remaining) < 1)
	{
		wanted = 1;
	}
	else if (wanted >= remaining)
	{
		wanted = remaining;
	}
	for (Object *m : members)
	{
		const int r = logic.random().getValue(1, remaining, "Object.cpp", 0xC48); // RW 0x6D328E
		--remaining;
		if (r <= wanted)
		{
			--wanted;
			apply(*m);
		}
	}
}

// RW 0x7C5DCE / 0x7C9A0B after the entry test: the unit into the contain (slot 0x9C); a HORDE whose contain has a horde interface (slot 0x7C) -> RW 0x7C562E:
// each member of its list (slot 0x10C) leaves the horde for the garrison (slot 0xA8) and is added
void garrisonNow(ContainModuleInterface &contain, Object &u)
{
	contain.addToContain(&u);
	ContainModuleInterface *own = isKindOfName(u, "HORDE") ? u.getContain() : nullptr;
	HordeContainInterface *h = own ? own->getHordeContainInterface() : nullptr;
	if (h && own->getContainedItemsList())
	{
		const std::vector<Object *> members(own->getContainedItemsList()->begin(), own->getContainedItemsList()->end());
		for (Object *m : members)
		{
			h->releaseMemberForGarrison(m);
			contain.addToContain(m);
		}
	}
}

// RW 0x7C112E (see the header): true when a caster was found
bool fireSpecialPowerAtPosition(ScriptEngine &engine, const std::string &playerName, const SpecialPowerTemplate &sp, const Coord3D &pos)
{
	GameLogic &logic = engine.logic();
	for (Player *p : ScriptConditions::players(engine, playerName))
	{
		for (const auto &proto : logic.players().teams().prototypes())
		{
			if (proto->getControllingPlayer() != p)
			{
				continue;
			}
			const std::vector<Team *> &instances = proto->teams();
			for (auto it = instances.rbegin(); it != instances.rend(); ++it) // + 0x334: from the head, the newest first
			{
				for (Object *o : ScriptConditions::members(**it))
				{
					if (SpecialPowerModuleInterface *m = SpecialPowerModules::findModule(*o, &sp))
					{
						m->doSpecialPowerAtLocation(pos, 0x40000u); // slot 0x30
						return true;
					}
				}
			}
		}
	}
	return false;
}

void noteUnportedButton(ScriptEngine &engine, const std::string &action, const CommandButton &b)
{
	engine.note("[S-1190] " + action + ": command button " + b.m_name + " (command " + std::to_string(b.m_command) + ") has a retail case that is not ported");
}

} // namespace

bool ScriptActions::executeOrderAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	GameLogic &logic = engine.logic();
	const TerrainLogic *terrain = logic.terrain();
	if (name == "TEAM_IDLE_FOR_FRAMECOUNT" || name == "TEAM_GUARD_FOR_FRAMECOUNT" || name == "TEAM_IDLE_FOR_SECONDS" || name == "TEAM_GUARD_FOR_SECONDS")
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue)) // RW 0x7C1704
		{
			scriptGroupIdle(teamAIGroup(*t), CMD_FROM_SCRIPT);
			engine.setSequentialTimer(INVALID_ID, t->getID(), framesOf(a, endsWith(name, "SECONDS")));
		}
		return true;
	}
	if (name == "UNIT_IDLE_FOR_FRAMECOUNT" || name == "UNIT_IDLE_FOR_SECONDS" || name == "UNIT_GUARD_FOR_FRAMECOUNT" || name == "UNIT_GUARD_FOR_SECONDS")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C9DF6 / 0x7C9D7D
		AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
		if (!ai)
		{
			return true;
		}
		if (name.compare(0, 10, "UNIT_GUARD") == 0)
		{
			ai->aiGuardPosition(*o->getPosition(), 0, CMD_FROM_SCRIPT); // GUARDMODE_NORMAL
		}
		else
		{
			ai->aiIdle(CMD_FROM_SCRIPT);
		}
		engine.setSequentialTimer(o->getID(), 0, framesOf(a, endsWith(name, "SECONDS")));
		return true;
	}
	if (name == "NAMED_GUARD")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C9306
		if (o && o->getAIUpdateInterface())
		{
			o->getAIUpdateInterface()->aiGuardPosition(*o->getPosition(), 0, CMD_FROM_SCRIPT);
		}
		return true;
	}
	if (name == "TEAM_GUARD")
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue)) // RW 0x7C9367
		{
			for (Object *o : ScriptConditions::members(*t))
			{
				if (hasStatus(*o, "HORDE_MEMBER") || hasStatus(*o, "UNDER_CONSTRUCTION") || !o->getAIUpdateInterface())
				{
					continue;
				}
				o->getAIUpdateInterface()->aiGuardPosition(*o->getPosition(), 0, CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (name == "UNIT_GUARD_POSITION" || name == "TEAM_GUARD_POSITION")
	{
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr; // RW 0x7C9400 / 0x7C01B1: the waypoint first
		if (!w)
		{
			return true;
		}
		std::vector<Object *> who;
		if (name == "TEAM_GUARD_POSITION")
		{
			if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
			{
				who = teamAIGroup(*t);
			}
		}
		else if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			who.push_back(o);
		}
		for (Object *o : who)
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiGuardPosition(w->location, 0, CMD_FROM_SCRIPT); // RW 0x7726F5 for the group
			}
		}
		return true;
	}
	if (name == "UNIT_GUARD_AREA" || name == "UNIT_GUARD_AREA_FROM_POSITION" || name == "TEAM_GUARD_AREA" || name == "TEAM_GUARD_AREA_FROM_POSITION")
	{
		// lane BFME2CAMP1: executeAction cases 0x1C6 / 0x1E6 -> RW 0x7C94B1(unit, area, position) and 0xD0 / 0x1E7 -> RW 0x7C02A6(team, area, position); the
		// plain forms pass "" as the position. The area by name first (RW 0x759194), then the unit (RW 0x75A3D8, with an AI) or the team (RW 0x759FDA); both
		// needed. The position is TheTerrainLogic's waypoint of that name (vslot 0x88): none -> aiGuardArea(area, 0, CMD_FROM_SCRIPT) (RW 0x7718E1), else
		// aiGuardAreaFromPosition(area, 0, CMD_FROM_SCRIPT, the waypoint's location) (RW 0x77194F); a team: its AI group's members with an AI (RW 0x7727B8 /
		// 0x7727F9)
		const bool team = name[0] == 'T';
		const bool fromPosition = name.size() > 14 && name.compare(name.size() - 14, 14, "_FROM_POSITION") == 0;
		const TriggerArea *area = engine.findTrigger(param(a, 1).stringValue);
		std::vector<Object *> who;
		if (team)
		{
			Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
			if (t && area)
			{
				who = teamAIGroup(*t);
			}
		}
		else if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			if (o->getAIUpdateInterface() && area)
			{
				who.push_back(o);
			}
		}
		if (who.empty())
		{
			return true;
		}
		const Waypoint *w = fromPosition && terrain ? terrain->findWaypointByName(param(a, 2).stringValue) : nullptr;
		for (Object *o : who)
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiGuardArea(*area, 0, CMD_FROM_SCRIPT, w ? &w->location : nullptr);
			}
		}
		return true;
	}
	if (name == "UNIT_STAND_GROUND" || name == "TEAM_STAND_GROUND")
	{
		const bool on = param(a, 1).intValue != 0;
		if (name == "TEAM_STAND_GROUND")
		{
			if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
			{
				for (Object *o : ScriptConditions::members(*t))
				{
					setStandGround(*o, on);
				}
			}
		}
		else if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			setStandGround(*o, on);
		}
		return true;
	}
	if (name == "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE" || name == "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER")
	{
		const bool byPlayer = name == "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER"; // RW 0x7C31FF; else RW 0x7C30F7
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		Coord3D c;
		if (!t || !ScriptConditions::teamCentreAllMembers(*t, c))
		{
			return true;
		}
		const std::vector<std::string> *list = engine.findObjectList(param(a, 1).stringValue);
		const ThingTemplate *tt = list ? nullptr : logic.things().findTemplate(param(a, 1).stringValue);
		if (!list && !tt)
		{
			return true;
		}
		auto closestFor = [&](const Player *p) { return list ? closestOfList(logic, c, *list, p) : closestOfTemplate(logic, c, tt, p); };
		Object *best = nullptr;
		if (!byPlayer)
		{
			best = closestFor(nullptr);
		}
		else
		{
			float bestDist = 99999.0f; // RW 0xBDF350 (the first hit always wins: `best` is null)
			for (Player *p : ScriptConditions::players(engine, param(a, 2).stringValue))
			{
				Object *o = closestFor(p);
				if (!o)
				{
					continue;
				}
				const float d = SimMath::fstpDword(SimMath::length3d(SimMath::sseSub(o->getPosition()->x, c.x), SimMath::sseSub(o->getPosition()->y, c.y),
					SimMath::sseSub(o->getPosition()->z, c.z))); // Coord3D::length
				if (!best || d < bestDist)
				{
					best = o;
					bestDist = d;
				}
			}
		}
		if (best)
		{
			AIGroup group(logic, idsOf(teamAIGroup(*t)));
			group.groupMoveToPosition(*best->getPosition(), false, CMD_FROM_SCRIPT); // RW 0x774897
		}
		return true;
	}
	if (name == "TEAM_RECRUIT_UNITS_FROM_TEAM")
	{
		Team *dst = ScriptConditions::team(engine, param(a, 0).stringValue, true); // RW 0x7C2C7A
		Team *src = dst ? ScriptConditions::team(engine, param(a, 3).stringValue) : nullptr;
		if (!dst || !src || !src->getPrototype())
		{
			return true;
		}
		const ThingTemplate *tt = logic.things().findTemplate(param(a, 2).stringValue);
		const std::vector<std::string> *list = engine.findObjectList(param(a, 2).stringValue);
		const Coord3D home = teamCentreLiving(*dst); // RW 0x7A02E1, once
		const std::int32_t maxCount = param(a, 1).intValue;
		for (std::int32_t count = 0; count < maxCount; ++count) // RW 0x7A449F
		{
			Object *best = nullptr;
			float bestD = 0.0f;
			const std::vector<Team *> instances = src->getPrototype()->teams();
			for (auto it = instances.rbegin(); it != instances.rend(); ++it) // prototype + 0x334: newest first
			{
				for (Object *o = (*it)->getFirstMember(); o; o = o->friend_teamNext())
				{
					bool match = tt && ScriptConditions::equivalentTemplates(o->getTemplate(), tt);
					if (!match && list && o->getTemplate())
					{
						for (const std::string &n : *list)
						{
							if (n == o->getTemplate()->getName()) // RW 0x778EA0
							{
								match = true;
								break;
							}
						}
					}
					if (!match || (o->getDisabledMask() & (1u << 3)) != 0) // DISABLED_HELD; AI + 0x3BE: see the header
					{
						continue;
					}
					const float dx = SimMath::sseSub(home.x, o->getPosition()->x);
					const float dy = SimMath::sseSub(home.y, o->getPosition()->y);
					const float d = SimMath::sseAdd(SimMath::sseMul(dy, dy), SimMath::sseMul(dx, dx));
					if (!best || d <= bestD)
					{
						bestD = d;
						best = o;
					}
				}
			}
			if (!best)
			{
				break;
			}
			best->setTeam(dst); // RW 0x69954A
		}
		return true;
	}
	if (name == "SET_UNIT_REFERENCE")
	{
		if (Object *o = engine.getUnitNamed(param(a, 1).stringValue)) // RW 0x7C2AAC(ref, unit, 0)
		{
			engine.setReference(param(a, 0).stringValue, *o); // RW 0x7C2AF6 -> RW 0x608397 (lane MODCOMPAT-AI: the side's reference map)
			engine.nameInCache(param(a, 0).stringValue, *o);  // RW 0x7C2B07 -> RW 0x60A1D5
		}
		return true;
	}
	if (name == "EXIT_SPECIFIC_BUILDING")
	{
		// lane BFME2CAMP1: RW 0x7C970A: the named object (RW 0x75A243) that is a STRUCTURE (template + 0x108 bit 7); with an AI it gets AI command 0x1B
		// (RW 0x6AF1AC(0, CMD_FROM_SCRIPT) -> RW 0x66E0DC: with a contain, its first argument 0 skips the contain's slot 0x90, the AI goes idle (RW 0x5E821A)
		// and the contain's slot 0x80 orders every rider out); without one the contain's slot 0xA8 (OpenContain::removeAllContained RW 0x866675) lets them
		// all out at once
		static const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
		Object *b = engine.getUnitNamed(param(a, 0).stringValue);
		if (!b || structure < 0 || !b->isKindOf((unsigned)structure))
		{
			return true;
		}
		if (AIUpdateInterface *ai = b->getAIUpdateInterface())
		{
			ContainModuleInterface *c = b->getContain();
			if (ai->acceptCommand(CMD_FROM_SCRIPT, 0x1B) && c) // RW 0x667174, then RW 0x66E0E5
			{
				ai->aiIdle(CMD_FROM_SCRIPT);
				c->orderAllPassengersToExit((int)CMD_FROM_SCRIPT);
			}
		}
		else if (OpenContain *open = dynamic_cast<OpenContain *>(b->getContain()))
		{
			open->removeAllContained();
		}
		else if (b->getContain())
		{
			engine.note("[S-1180] EXIT_SPECIFIC_BUILDING: a contain without OpenContain's removeAllContained (contain slot 0xA8) lets nobody out");
		}
		return true;
	}
	if (name == "TEAM_GARRISON_SPECIFIC_BUILDING" || name == "TEAM_GARRISON_SPECIFIC_BUILDING_INSTANTLY")
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue); // RW 0x7C7091
		Object *b = t ? engine.getUnitNamed(param(a, 1).stringValue) : nullptr;
		ContainModuleInterface *contain = b ? b->getContain() : nullptr;
		if (!contain)
		{
			return true;
		}
		if (name == "TEAM_GARRISON_SPECIFIC_BUILDING_INSTANTLY")
		{
			for (Object *u : ScriptConditions::members(*t))
			{
				if (hasStatus(*u, "HORDE_MEMBER") || !contain->isValidContainerFor(*u, true, false)) // RW 0x7C5DCE
				{
					continue;
				}
				garrisonNow(*contain, *u);
			}
			return true;
		}
		const std::uint32_t entered = contain->getPlayerWhoEntered(); // slot 0x144
		if (!isKindOfName(*b, "STRUCTURE") || entered != 0)
		{
			const Player *owner = t->getControllingPlayer();
			if (!owner || entered != (1u << ((unsigned)owner->getPlayerIndex() & 31u)))
			{
				return true; // INFERENCE: retail reads the controlling player without a test
			}
		}
		AIGroup group(logic, idsOf(teamAIGroup(*t))); // RW 0x7724BE: each member with an AI
		for (Object *o : group.members())
		{
			o->getAIUpdateInterface()->aiEnterObject(b, CMD_FROM_SCRIPT); // RW 0x66C5A4, AI command 0x17
		}
		return true;
	}
	if (name == "NAMED_GARRISON_SPECIFIC_BUILDING" || name == "NAMED_GARRISON_SPECIFIC_BUILDING_INSTANTLY")
	{
		Object *u = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C9925: the unit (RW 0x75A3D8) with an AI
		if (!u || !u->getAIUpdateInterface())
		{
			return true;
		}
		Object *b = engine.getUnitNamed(param(a, 1).stringValue); // RW 0x75A243
		ContainModuleInterface *contain = b ? b->getContain() : nullptr;
		if (!contain)
		{
			return true;
		}
		const bool instant = name == "NAMED_GARRISON_SPECIFIC_BUILDING_INSTANTLY";
		const std::uint32_t entered = contain->getPlayerWhoEntered(); // slot 0x144
		if (!isKindOfName(*b, "GARRISON") || entered != 0) // template + 0x116 bit 6: GARRISON (the team form tests STRUCTURE)
		{
			const Player *owner = u->getControllingPlayer();
			if (!owner || entered != (1u << ((unsigned)owner->getPlayerIndex() & 31u)))
			{
				return true; // INFERENCE: retail reads the controlling player without a test
			}
		}
		if (!contain->isValidContainerFor(*u, true, !instant)) // slot 0x98(unit, 1, !instant)
		{
			return true;
		}
		if (instant)
		{
			garrisonNow(*contain, *u);
		}
		else
		{
			u->getAIUpdateInterface()->aiEnterObject(b, CMD_FROM_SCRIPT); // RW 0x66C5A4
		}
		return true;
	}
	if (name == "SET_PLAYER_MONEY_TO_COUNTER")
	{
		// RW 0x7C3655 (BFME2 ScriptActions::Rva003C2BC0Do, tier A): the money (Player + 0x90's + 4) of every player of the mask, summed (int), into the
		// counter (get or create, RW 0x60817A)
		std::int32_t sum = 0;
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			sum = (std::int32_t)((std::uint32_t)sum + (std::uint32_t)p->getMoney()->countMoney());
		}
		engine.counter(param(a, 1).stringValue).value = sum;
		return true;
	}
	if (name == "SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER")
	{
		// RW 0x7C38F6 (BFME2 Rva003C2E61Do, tier B): the player only for a one-player mask (RW 0x6A85B6); its ScoreKeeper (+ 0x3DC) RW 0x79E4D8(the raw type
		// name: no object list): the destroyed-by-template entries of that name over the 20 victims; the counter (get or create) takes it
		const Player *p = ScriptConditions::playerOfMask(engine, param(a, 0).stringValue);
		if (!p)
		{
			return true;
		}
		std::int32_t kills = 0;
		for (int i = 0; i < ScoreKeeper::MAX_PLAYERS; ++i)
		{
			const std::map<std::string, int> &m = p->getScoreKeeper().destroyedByTemplate(i);
			const auto it = m.find(param(a, 1).stringValue);
			kills += it != m.end() ? it->second : 0;
		}
		engine.counter(param(a, 2).stringValue).value = kills;
		return true;
	}
	if (name == "UNIT_SET_MODELCONDITION_FOR_DURATION")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C6404
		if (!o)
		{
			return true;
		}
		int bit = -1;
		for (int i = 0; i < 591 && TheModelConditionNames[i] && bit < 0; ++i)
		{
			if (param(a, 1).stringValue == TheModelConditionNames[i]) // RW 0x4065AA: case kept
			{
				bit = i;
			}
		}
		if (bit < 0)
		{
			return true;
		}
		const int frames = (int)SimMath::ftol2(SimMath::pc24MulW(5.0, (double)param(a, 2).realValue)); // RW 0x7C645C: fild 5, fmul seconds, _ftol2
		applyToFractionOfHorde(logic, *o, param(a, 3).realValue, [bit, frames](Object &target) {
			if (frames == 0)
			{
				if (!target.testModelCondition(bit))
				{
					target.setModelConditionState(bit, true);
				}
			}
			else
			{
				target.setSpecialModelConditionState(bit, (UnsignedInt)frames);
			}
		});
		return true;
	}
	if (name == "HUMAN_IMPASSABLE_AREA")
	{
		// lane BFME2CAMP1: RW 0x7C4040 (area, on): the area by name (RW 0x759194; none: nothing); TheAI's pathfinder (+ 0x10) RW 0x934D51(area, on): the
		// area's integer bounds (RW 0x6E4D2C: floor of the low corner, ceil of the high) divided by 10 (C division) give the cell range; every ground cell
		// (RW 0x5E2E9C(1, x, y)) whose centre (x * 10 + 5, y * 10 + 5) is in the area (RW 0x6E4DC7: the integers as floats) whose bit 18 (impassable to
		// human players, cell + 0xC) changes (RW 0x9349C8) marks its zone block dirty (RW 0x937D96). Then (not a reload: the third argument 0) the
		// engine records (name, on) at + 0x191D0 (RW 0x6093D8), the list a retail load applies again; the port's snapshot keeps the cells' bit 18 itself
		// (AIPathfindSnapshot.cpp), so the list is not kept
		const TriggerArea *area = engine.findTrigger(param(a, 0).stringValue);
		AIWorld *ai = logic.aiWorld();
		if (!area || area->points.empty() || !ai || !ai->mapReady())
		{
			return true;
		}
		const bool on = param(a, 1).intValue != 0;
		float loX = area->points[0].x, loY = area->points[0].y, hiX = loX, hiY = loY;
		for (const Point2F &q : area->points)
		{
			loX = q.x < loX ? q.x : loX;
			loY = q.y < loY ? q.y : loY;
			hiX = q.x > hiX ? q.x : hiX;
			hiY = q.y > hiY ? q.y : hiY;
		}
		const int x0 = SimMath::floorToInt(loX) / 10, y0 = SimMath::floorToInt(loY) / 10;
		const int x1 = SimMath::ceilToInt(hiX) / 10, y1 = SimMath::ceilToInt(hiY) / 10;
		Pathfinder &pf = ai->pathfinder();
		for (int cx = x0; cx <= x1; ++cx)
		{
			for (int cy = y0; cy <= y1; ++cy)
			{
				if (!ScriptConditions::pointInTrigger(*area, SimMath::sseFromInt32(cx * 10 + 5), SimMath::sseFromInt32(cy * 10 + 5)))
				{
					continue;
				}
				PathfindCell *cell = pf.getCell(LAYER_GROUND, cx, cy);
				if (cell && cell->getImpassableToPlayers() != on)
				{
					cell->setImpassableToPlayers(on);
					pf.zoneManager().markCellDirty(cx, cy);
				}
			}
		}
		return true;
	}
	if (name == "CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL")
	{
		// lane BFME2CAMP1: RW 0x7C6D8E (template, player, level): the template (RW 0x6D1305; none: nothing), the first player of the mask (RW 0x758F7C ->
		// RW 0x6A85EE; none: nothing); a record of the template for the player's hero list (Player + 0x758, RW 0x780713); a level other than -1 that the
		// experience levels have for the template (RW 0x689509: the first level of the template's list whose final override is valid for the game type,
		// RW 0x688DB0, and has that rank, + 0xFC) gives the record its RequiredExperience (+ 0x18, cvtsi2ss) and rank and base rank (+ 0xFC, RW 0x688EEC);
		// otherwise the rank is RW 0x73CE2A's; the record is appended (RW 0x781861 -> RW 0x6E40A6)
		const ThingTemplate *tt = logic.things().findTemplate(param(a, 0).stringValue);
		const std::vector<Player *> ps = tt ? ScriptConditions::players(engine, param(a, 1).stringValue) : std::vector<Player *>{};
		if (ps.empty())
		{
			return true;
		}
		HeroRecord r = PlayerHeroList::recordFromTemplate(*tt);
		const int level = param(a, 2).intValue;
		const ExperienceLevelTemplate *found = nullptr;
		if (level != -1 && TheExperienceLevelSystem)
		{
			const bool multiplayer = logic.economy().isMultiplayerGame(); // RW 0x625456
			if (const std::vector<const ExperienceLevelTemplate *> *levels = TheExperienceLevelSystem->levelsFor(tt->getName()))
			{
				for (const ExperienceLevelTemplate *l : *levels)
				{
					const ExperienceLevelTemplate *f = l ? l->getFinal() : nullptr;
					if (f && !(multiplayer ? f->m_singlePlayerOnly : f->m_multiPlayerOnly) && f->m_rank == level)
					{
						found = f;
						break;
					}
				}
			}
		}
		if (found)
		{
			r.experience = SimMath::sseFromInt32(found->m_requiredExperience); // cvtsi2ss
			r.rank = found->m_rank;
			r.baseRank = found->m_rank;
		}
		ps.front()->heroes().addRecord(r);
		return true;
	}
	if (name == "TEAM_SET_MODELCONDITION_FOR_DURATION")
	{
		// lane BFME2CAMP1: RW 0x7C649A (4 arguments, ret 0x10): the team (RW 0x759FDA); the model condition whose name equals the parameter (case kept, RW
		// 0x4065AA over the 591 names RW 0xD9FAD8; none: nothing); frames = _ftol2(5 * seconds) (RW 0x7C6502, x87); every member in the team's order (RW
		// 0x66362D) gets RW 0x68B581(bit, frames) -> the SMCHelper's timed bit (RW 0x8E2C0F) whatever the frames; the fraction (the 4th) is not read
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		if (!t)
		{
			return true;
		}
		int bit = -1;
		for (int i = 0; i < 591 && TheModelConditionNames[i] && bit < 0; ++i)
		{
			if (param(a, 1).stringValue == TheModelConditionNames[i])
			{
				bit = i;
			}
		}
		if (bit < 0)
		{
			return true;
		}
		const int frames = (int)SimMath::ftol2(SimMath::pc24MulW(5.0, (double)param(a, 2).realValue));
		for (Object *o : ScriptConditions::members(*t))
		{
			o->setSpecialModelConditionState(bit, (UnsignedInt)frames);
		}
		return true;
	}
	if (name == "NAMED_USE_COMMANDBUTTON_ABILITY" ||name == "NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED" || name == "NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C6243 / 0x7C62BF / 0x7C6363
		if (!o)
		{
			return true;
		}
		Object *target = nullptr;
		const Waypoint *w = nullptr;
		if (name == "NAMED_USE_COMMANDBUTTON_ABILITY_ON_NAMED" && !(target = engine.getUnitNamed(param(a, 2).stringValue)))
		{
			return true;
		}
		if (name == "NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT" && !(w = terrain ? terrain->findWaypointByName(param(a, 2).stringValue) : nullptr))
		{
			return true;
		}
		const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(o->getCommandSetName()) : nullptr; // RW 0x69156B / 0x71EFA2
		const std::string &ability = param(a, 1).stringValue;
		for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i) // RW 0x80C837, bound 0x21
		{
			const CommandButton *b = set->getCommandButton(i);
			if (!b || b->m_name.empty() || b->m_name != ability)
			{
				continue;
			}
			const bool ran = target ? ObjectCommandButton::doCommandButtonAtObject(*o, b, target, CMD_FROM_SCRIPT)
							: w      ? ObjectCommandButton::doCommandButtonAtPosition(*o, b, w->location, CMD_FROM_SCRIPT)
									 : ObjectCommandButton::doCommandButton(*o, b, CMD_FROM_SCRIPT);
			if (!ran)
			{
				noteUnportedButton(engine, name, *b);
			}
		}
		return true;
	}
	if (name == "TEAM_USE_COMMANDBUTTON_ABILITY" || name == "TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED" || name == "TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT")
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue); // RW 0x7C11EE / 0x7C1254 / 0x7C12DC
		if (!t)
		{
			return true;
		}
		Object *target = nullptr;
		const Waypoint *w = nullptr;
		if (name == "TEAM_USE_COMMANDBUTTON_ABILITY_ON_NAMED" && !(target = engine.getUnitNamed(param(a, 2).stringValue)))
		{
			return true;
		}
		if (name == "TEAM_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT" && !(w = terrain ? terrain->findWaypointByName(param(a, 2).stringValue) : nullptr))
		{
			return true;
		}
		const CommandButton *b = TheCommandStore ? TheCommandStore->findCommandButton(param(a, 1).stringValue) : nullptr; // RW 0x71D6EA
		if (!b)
		{
			return true;
		}
		bool reported = false;
		for (Object *o : teamAIGroup(*t)) // RW 0x76F9FF / 0x76FA59 / 0x76FA2A: the group's order
		{
			const bool ran = target ? ObjectCommandButton::doCommandButtonAtObject(*o, b, target, CMD_FROM_SCRIPT)
							: w      ? ObjectCommandButton::doCommandButtonAtPosition(*o, b, w->location, CMD_FROM_SCRIPT)
									 : ObjectCommandButton::doCommandButton(*o, b, CMD_FROM_SCRIPT);
			if (!ran && !reported)
			{
				noteUnportedButton(engine, name, *b);
				reported = true;
			}
		}
		return true;
	}
	if (name == "TEAM_PARTIAL_USE_COMMANDBUTTON")
	{
		// lane MODCOMPAT-AI: RW 0x7CAFA5 case 0x111 -> RW 0x7C7D12(percent, team, button): the team (RW 0x759FDA, not created; none: nothing), the button by
		// name (TheControlBar RW 0x71D6EA; none: nothing); the members in retail's order (RW 0x66362D / 0x6632C9: the newest first) that pass the
		// ActionManager's test with no target and no location (RW 0x75CF21(member, 0, 0, 1)) are collected; count = the x87 (pc24) fild n, fmul percent,
		// fmul 0.01f (RW 0xBE5600), stored as a double, the CRT ceil (RW [0xBD0588]), stored as a float, fistp; the first `count` of them run
		// Object::doCommandButton(button, CMD_FROM_SCRIPT, 0) (RW 0x696FD2)
		Team *t = ScriptConditions::team(engine, param(a, 1).stringValue);
		const CommandButton *b = t && TheCommandStore ? TheCommandStore->findCommandButton(param(a, 2).stringValue) : nullptr;
		if (!b)
		{
			return true;
		}
		std::vector<Object *> usable;
		for (Object *o = t->getLastMember(); o; o = o->friend_teamPrev())
		{
			if (ActionManager::canUseCommandButton(*o, *b))
			{
				usable.push_back(o);
			}
		}
		const double wanted = SimMath::pc24MulW(SimMath::pc24MulW((double)(std::int32_t)usable.size(), (double)param(a, 0).realValue), (double)0.01f);
		const std::int32_t count = SimMath::fistp32((double)SimMath::fstpDword(SimMath::ceilD(wanted)));
		for (std::int32_t i = 0; i < count && i < (std::int32_t)usable.size(); ++i)
		{
			if (!ObjectCommandButton::doCommandButton(*usable[(size_t)i], b, CMD_FROM_SCRIPT))
			{
				noteUnportedButton(engine, name, *b); // one note per button run (S-1190)
			}
		}
		return true;
	}
	if (name == "TEAM_HUNT_WITH_COMMAND_BUTTON")
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue); // RW 0x7C46A0 -> RW 0x759FDA
		const std::string &ability = param(a, 1).stringValue;
		const CommandButton *b = t && TheCommandStore ? TheCommandStore->findCommandButton(ability) : nullptr; // RW 0x71D6EA
		if (!b)
		{
			return true;
		}
		switch (b->m_command) // + 0x14
		{
		case GUI_COMMAND_FIRE_WEAPON:
		case GUI_COMMAND_SWITCH_WEAPON:
			break;
		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_SPELL_BOOK:
			if (b->m_specialPowerName.empty()) // + 0x44
			{
				return true;
			}
			if (b->m_options & 7u) // + 0x1C: NEED_TARGET_ENEMY / NEUTRAL / ALLY_OBJECT
			{
				break;
			}
			engine.note("ERROR-Team hunt with command button - cannot hunt with ability " + ability);
			return true;
		default:
			engine.note("ERROR-Team hunt with command button - cannot hunt with ability " + ability);
			return true;
		}
		for (Object *o : ScriptConditions::members(*t)) // RW 0x66362D: the team's member list
		{
			if (!o->getAIUpdateInterface()) // Object + 0x260
			{
				continue;
			}
			const CommandSet *set = TheCommandStore->findCommandSet(o->getCommandSetName()); // RW 0x69156B / 0x71EFA2
			bool found = false;
			for (int i = 0; set && i < CommandSet::MAX_BUTTONS && !found; ++i) // RW 0x80C837, bound 0x21
			{
				found = set->getCommandButton(i) == b;
			}
			if (!found)
			{
				engine.note("Error - Team hunt with command button - unit type '" + o->getTemplate()->getName() + "' is not valid for ability " + ability);
				continue;
			}
			if (CommandButtonHuntUpdate *hunt = dynamic_cast<CommandButtonHuntUpdate *>(o->findModule("CommandButtonHuntUpdate"))) // RW 0x68BDA5
			{
				hunt->setCommandButton(ability); // RW 0x898EE8
			}
			else
			{
				engine.note("Error - Team hunt with command button - unit type '" + o->getTemplate()->getName() +
					"' requires CommandButtonHuntUpdate in .ini definition to hunt with " + ability);
			}
		}
		return true;
	}
	if (name == "PLAYER_FIRE_SPECIAL_POWER_AT_WAYPOINT")
	{
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 2).stringValue) : nullptr; // RW 0x7C4C50
		const SpecialPowerTemplate *sp = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(param(a, 1).stringValue) : nullptr; // RW 0x69C146
		if (w && sp)
		{
			fireSpecialPowerAtPosition(engine, param(a, 0).stringValue, *sp, w->location);
		}
		return true;
	}
	if (name == "NAMED_FIRE_SPECIAL_POWER_AT_WAYPOINT" || name == "NAMED_FIRE_SPECIAL_POWER_AT_NAMED")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue); // RW 0x7C4A99 / 0x7C4CDB
		const SpecialPowerTemplate *sp = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(param(a, 1).stringValue) : nullptr;
		SpecialPowerModuleInterface *m = o && sp ? SpecialPowerModules::findModule(*o, sp) : nullptr; // RW 0x68C26D
		if (!m)
		{
			return true;
		}
		if (name == "NAMED_FIRE_SPECIAL_POWER_AT_NAMED")
		{
			if (Object *target = engine.getUnitNamed(param(a, 2).stringValue))
			{
				m->doSpecialPowerAtObject(target, 0x40000u); // slot 0x2C
			}
		}
		else if (const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 2).stringValue) : nullptr)
		{
			m->doSpecialPowerAtLocation(w->location, 0x40000u); // slot 0x30
		}
		return true;
	}
	return false;
}
