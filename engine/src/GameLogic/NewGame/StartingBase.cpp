// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/StartingBase.h for the sources of every rule.

#include "GameLogic/NewGame/StartingBase.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Construction.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <variant>

namespace
{
// RW 0x629DD9 (ZH placeObjectAtPosition): the parts this layer has; the rest is S-272
Object *placeObjectAtPosition(GameLogic &logic, Player &player, const std::string &name, const Coord3D &pos, StartingBase::Result &result)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	if (!tt)
	{
		result.errors.push_back("placeObjectAtPosition: no template '" + name + "'");
		return nullptr;
	}
	if (!player.getDefaultTeam())
	{
		result.errors.push_back("placeObjectAtPosition: player '" + player.getPlayerName() + "' has no default team");
		return nullptr;
	}
	Object *obj = logic.newObject(tt, player.getDefaultTeam(), ObjectStatusMaskType{});
	if (!obj)
	{
		result.errors.push_back("placeObjectAtPosition: newObject made nothing for '" + name + "'");
		return nullptr;
	}
	float angle = 0.0f; // the template's PlacementViewAngle (RW tt + 0x4E0, parseAngleReal)
	if (const FieldValue *v = tt->findField("PlacementViewAngle"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			angle = *f;
		}
	}
	obj->setOrientation(angle);
	Coord3D p = pos;
	obj->setPosition(&p);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*obj); // RW 0x629EC9 -> 0x6E85E9 (lane BUILD-1: the pathfinder's map exists before the bases)
	}
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (CreateModuleInterface *create = m->getCreate())
		{
			create->onBuildComplete();
		}
	}
	if (obj->isKindOfName("STRUCTURE"))
	{
		// RW 0x6AAF3B / 0x6AA72B (S-272 -> BUILD-1): the player's structure notifications (the command points were counted at birth: the call is idempotent)
		Construction::onStructureCreated(player, nullptr, *obj);
		Construction::onStructureConstructionComplete(player, nullptr, *obj, false);
	}
	return obj;
}
} // namespace

Coord3D StartingBase::unitPositionFromOffset(const Coord3D &base, const Coord3D &off)
{
	const float x = off.x, y = off.y, z = off.z;
	// RW 0x62AED4 ff: SSE float32 squares and sums, z*z + y*y first, then + x*x
	const float len2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(z, z), SimMath::mulf32(y, y)), SimMath::mulf32(x, x));
	float nx = x, ny = y;
	if (len2 != 0.0f)
	{
		const float r = BFME2_Inverse_Sqrt(len2); // RW 0x441C56
		nx = BFME2_Mul24(x, r);
		ny = BFME2_Mul24(y, r);
	}
	const float c = BFME2_Inverse_Sqrt(2.0f); // 1/sqrt 2 (RW 0xBD889C = 2.0f)
	const float negc = BFME2_Mul24(-1.0f, c);
	// the argument of acos is ((nz + nx) * 0 + ny) in the x87 chain: a finite (nz + nx) times zero is a zero, and adding it leaves ny
	const double arg = (double)ny;
	float A = (float)SimMath::acosDet(arg); // MSVCR71 acos (RW 0xA3D6C2): the deterministic replacement (S-167: the CRT's own bits are not proven equal)
	const float t1 = SimMath::mulf32(ny, 0.0f);
	const float t2 = SimMath::subf32(nx, t1);
	if (0.0f > t2)
	{
		A = SimMath::mulf32(A, -1.0f);
	}
	const float s = SimMath::sinDet(A);   // MSVCR71 sin / cos (RW 0xA3CF90 / 0xA3CF84): the deterministic replacements (S-167)
	const float co = SimMath::cosDet(A);
	const float X = SimMath::subf32(SimMath::mulf32(co, c), SimMath::mulf32(negc, s));
	const float Y = SimMath::addf32(SimMath::mulf32(negc, co), SimMath::mulf32(c, s));
	// |offset|: the x87 chain (z*z + y*y) + x*x at PC24 = float32 sums, the CRT sqrt of the double, stored as float32
	const float len = (float)SimMath::sqrtd((double)len2);
	Coord3D out;
	out.x = SimMath::addf32(SimMath::mulf32(len, X), base.x);
	out.y = SimMath::addf32(SimMath::mulf32(Y, len), base.y);
	out.z = SimMath::addf32(0.0f, base.z);
	return out;
}

void StartingBase::placeForPlayer(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, Result &result)
{
	const TerrainLogic *terrain = logic.terrain();
	if (!terrain)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + ": the logic has no terrain");
		return;
	}
	const std::string startName = "Player_" + std::to_string(slot.startPos + 1) + "_Start";
	const Waypoint *wp = terrain->findWaypointByName(startName);
	if (!wp)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + " (" + player.getPlayerName() + "): the map has no waypoint " + startName);
		return;
	}
	Coord3D pos = wp->location;
	pos.z = logic.getGroundHeight(pos.x, pos.y);
	Object *conYard = nullptr;
	// RW 0x62AD24: `cmp [waypoint + 0x60], 0; jne` skips the structure: the start waypoints of the fortress maps (waypointType 5 on Player_1_Start of every
	// "map wor" fortress map and of Amon Sul Fortress) get none, the map's own fortress (owned by the side Player_<start>, SkirmishSides) is the base; the starting
	// units are placed around the start waypoint (RW 0x62AE37: the base point is the waypoint's location when no structure was made)
	if (!pt.m_startingBuilding.empty() && pt.hasStartingBuilding() && wp->type == 0)
	{
		conYard = placeObjectAtPosition(logic, player, pt.m_startingBuilding, pos, result);
		if (conYard)
		{
			conYard->setName("BASE_FLAG_" + std::to_string(slot.startPos + 1));
			Placed pl;
			pl.templateName = pt.m_startingBuilding;
			pl.id = conYard->getID();
			pl.position = *conYard->getPosition();
			pl.structure = true;
			result.placed.push_back(pl);
		}
	}
	for (int i = 0; i < PlayerTemplate::NUM_STARTING_UNITS; ++i)
	{
		const std::string &name = pt.m_startingUnit[i];
		if (name.empty())
		{
			continue;
		}
		const Coord3D &off = pt.m_startingUnitOffset[i];
		const Coord3D base = conYard ? *conYard->getPosition() : wp->location;
		Coord3D unitPos;
		if (off.x == 0.0f && off.y == 0.0f && off.z == 0.0f) // RW 0x62B0F9: Coord3D::operator== against (0, 0, 0)
		{
			// lane MODS-3 (Edain's StartingUnit5 FreeBuildPorterEgg): RW 0x62B074 .. 0x62B111: findPositionAround(the base point (the zero offset adds
			// nothing), flags 0, minRadius r, maxRadius r * 1.4 (RW 0xBDBC98, mulss), the random start angle (RW 0xBFD290), maxZDelta RW 0xBF7328,
			// no objects); r is 50 (RW 0xBD88C4), the structure's bounding circle radius (Object + 0xB8, RW 0x62AD57) when one was placed, its
			// CastleBehavior's MaxCastleRadius (RW 0x797ED7) when it has one. No position found: the unit is not made (RW 0x62B11F)
			float r = 50.0f;
			if (conYard)
			{
				r = CombatQueries::boundingCircleRadius(*conYard); // Object + 0xB8
				if (const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(conYard->findModule("CastleBehavior")))
				{
					r = castle->data()->m_maxCastleRadius;
				}
			}
			FindPositionOptions options;
			options.flags = FPF_NONE;
			options.minRadius = r;
			options.maxRadius = SimMath::mulf32(r, 1.4f);
			options.startAngle = RANDOM_START_ANGLE;
			Coord3D found = base;
			if (!FindPosition::findPositionAround(logic, base, options, found))
			{
				continue;
			}
			unitPos = found;
		}
		else
		{
			unitPos = unitPositionFromOffset(base, off);
		}
		if (Object *unit = placeObjectAtPosition(logic, player, name, unitPos, result))
		{
			Placed pl;
			pl.templateName = name;
			pl.id = unit->getID();
			pl.position = *unit->getPosition();
			result.placed.push_back(pl);
		}
	}
}

namespace
{
// WOTR2 milestone 6: CarryoverUnit RW 0x780172: the entry's experience when above the new object's (setExperienceAndLevel RW 0x79D8EF with the base, then the
// tracker's "gained a level" + 0x20 cleared), then its upgrade set on the object (RW 0x68CC6C, 0x6936FE; a horde's members too)
void carryOver(Object &o, float experience, const std::vector<std::string> &upgrades, const std::string &what, std::vector<std::string> &errors)
{
	ExperienceTracker *xp = o.getExperienceTracker();
	if (xp && xp->getExperience() < experience)
	{
		xp->setExperienceAndLevel(experience, true);
		xp->clearGainedLevel();
	}
	for (const std::string &u : upgrades)
	{
		const UpgradeTemplate *t = Player::resolveUpgrade(u, false);
		if (!t)
		{
			errors.push_back(what + ": the army entry's upgrade " + u + " is not an Upgrade");
			continue;
		}
		o.giveUpgrade(t);
	}
}

// [S-1329 inference] four abreast, 70 apart, the first row 100 from the anchor (ArmyPlacer's WalkOnTemplate formation, RW 0x781FAF, is not ported)
Coord3D armySlot(GameLogic &logic, const Coord3D &anchor, int k)
{
	Coord3D pos = anchor;
	pos.x = SimMath::addf32(pos.x, SimMath::sseFromInt32((k % 4) * 70 - 105));
	pos.y = SimMath::addf32(pos.y, SimMath::sseFromInt32((k / 4) * 70 + 100));
	pos.z = logic.getGroundHeight(pos.x, pos.y);
	return pos;
}
} // namespace

void StartingBase::placeLivingWorldBattle(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, const LivingWorldBattleStart &battle, Result &result)
{
	const TerrainLogic *terrain = logic.terrain();
	if (!terrain)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + ": the logic has no terrain");
		return;
	}
	if (slotNum == battle.ownerSlot)
	{
		// RW 0x629F34: the region's buildings, building i on "Player_1_BuildPlot_<i + 1>" (a missing waypoint: that building is not placed, as retail)
		for (size_t i = 0; i < battle.ownerBuildings.size(); ++i)
		{
			const std::string plot = "Player_1_BuildPlot_" + std::to_string(i + 1);
			const Waypoint *wp = terrain->findWaypointByName(plot);
			if (!wp || battle.ownerBuildings[i].empty())
			{
				result.notes.push_back("slot " + std::to_string(slotNum) + ": the map has no waypoint " + plot + " for " + battle.ownerBuildings[i] + " (retail skips it)");
				continue;
			}
			Coord3D pos = wp->location;
			pos.z = logic.getGroundHeight(pos.x, pos.y);
			if (Object *b = placeObjectAtPosition(logic, player, battle.ownerBuildings[i], pos, result))
			{
				// WOTR2 milestone 7: RW 0x629F34 tags the building with the owner's garrison (ArmySummarySystem::assignArmyID RW 0x80FD9C) and its Living World
				// building id (Object + 0x484)
				const LivingWorldBattleStart::SlotStart *own = battle.slotStart(slotNum);
				LivingWorldBattleTags::Tag tag;
				tag.armyId = own ? own->garrisonArmyId : 0;
				tag.buildingId = i < battle.ownerBuildingIds.size() ? battle.ownerBuildingIds[i] : 0;
				logic.livingWorldTags().objects[b->getID()] = tag;
				Placed pl;
				pl.templateName = battle.ownerBuildings[i];
				pl.id = b->getID();
				pl.position = *b->getPosition();
				pl.structure = true;
				result.placed.push_back(pl);
			}
		}
	}
	const std::string startName = "Player_" + std::to_string(slot.startPos + 1) + "_Start";
	const Waypoint *start = terrain->findWaypointByName(startName);
	if (!start)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + " (" + player.getPlayerName() + "): the map has no waypoint " + startName + " for its armies");
		return;
	}
	int k = 0;
	auto place = [&](const std::string &name, int armyId, int memberIndex, const std::string &spawnId) -> Object * {
		const Coord3D pos = armySlot(logic, start->location, k);
		++k;
		if (Object *o = placeObjectAtPosition(logic, player, name, pos, result))
		{
			Placed pl;
			pl.templateName = name;
			pl.id = o->getID();
			pl.position = *o->getPosition();
			pl.armyId = armyId;
			pl.memberIndex = memberIndex;
			result.placed.push_back(pl);
			LivingWorldBattleTags::Tag tag; // WOTR2 milestone 7: ArmySummaryEntry::createObject RW 0x80EDF1 (+ 0x47C the army, + 0x480 the entry's spawn id)
			tag.armyId = armyId;
			tag.spawnId = spawnId;
			logic.livingWorldTags().objects[o->getID()] = tag;
			return o;
		}
		return nullptr;
	};
	// WOTR2 milestone 6: ArmySummary::Load RW 0x812346 (initial load, the player's FIRST loaded army, game kind != 3): after that army's entries, every
	// StartingUnitTacticalWOTR of the PlayerTemplate (INI order), then the bonus units, all with the player's garrison army id (memberIndex -1)
	const LivingWorldBattleStart::SlotStart *ss = battle.slotStart(slotNum);
	bool startingDone = !ss;
	auto placeStarting = [&]() {
		for (const std::string &name : pt.m_startingUnitTacticalWOTR)
		{
			place(name, ss->garrisonArmyId, -1, std::string());
		}
		for (const std::string &name : ss->bonusUnits)
		{
			place(name, ss->garrisonArmyId, -1, std::string());
		}
		startingDone = true;
	};
	// WOTR2 milestone 7 (step 5): a reinforcement army (state 2, RW 0x6B851C) is not loaded now: its entries wait in the battle's records for its call-in
	for (const LivingWorldBattleStart::ArmyStart &as : battle.armies)
	{
		if (as.slot != slotNum || as.state != 2)
		{
			continue;
		}
		LivingWorldBattleTags::Reinforcement r;
		r.armyId = as.armyId;
		r.playerIndex = player.getPlayerIndex();
		r.state = 2;
		r.arrivalFrame = (std::uint32_t)as.arrivalFrame;
		r.spawnPoints = as.spawnPoints;
		for (const LivingWorldBattleStart::Member &m : battle.members)
		{
			if (m.armyId == as.armyId)
			{
				r.members.push_back({ m.thingTemplate, m.memberIndex, m.quantity, m.experience, m.upgrades, m.spawnId });
			}
		}
		logic.livingWorldTags().reinforcements.push_back(r);
	}
	auto loadedAtStart = [&](int armyId) {
		const LivingWorldBattleStart::ArmyStart *as = battle.armyStart(armyId);
		return !as || as->state != 2;
	};
	int firstArmy = 0;
	for (const LivingWorldBattleStart::Member &m : battle.members)
	{
		if (m.slot != slotNum || !loadedAtStart(m.armyId))
		{
			continue;
		}
		if (firstArmy == 0)
		{
			firstArmy = m.armyId;
		}
		else if (m.armyId != firstArmy && !startingDone)
		{
			placeStarting();
		}
		for (int q = 0; q < m.quantity; ++q)
		{
			Object *o = place(m.thingTemplate, m.armyId, m.memberIndex, m.spawnId);
			if (o)
			{
				carryOver(*o, m.experience, m.upgrades, "slot " + std::to_string(slotNum) + " " + m.thingTemplate, result.errors);
			}
		}
	}
	if (!startingDone)
	{
		placeStarting(); // the first army was the last one (or the empty garrison: RW 0x8125FC still loads it)
	}
}

// WOTR2 milestone 7 (step 5): RW 0x80F4D3 and the computer players' call-in RW 0x9A0426 (see StartingBase.h)
void StartingBase::updateLivingWorldReinforcements(GameLogic &logic)
{
	LivingWorldBattleTags &tags = logic.livingWorldTags();
	if (!tags.active)
	{
		return;
	}
	std::vector<int> aiReady;
	for (LivingWorldBattleTags::Reinforcement &r : tags.reinforcements)
	{
		if (r.state == 2 && (std::uint32_t)logic.getFrame() >= r.arrivalFrame) // RW 0x80F444: unsigned >=
		{
			r.state = 3;
		}
		const Player *p = logic.players().getNthPlayer(r.playerIndex);
		if (r.state == 3 && p && p->getPlayerType() != PLAYER_HUMAN)
		{
			aiReady.push_back(r.armyId);
		}
	}
	for (int id : aiReady)
	{
		callInLivingWorldArmy(logic, id);
	}
}

// RW 0x812563 -> ArmySummary::Load(false, false, 0) RW 0x812119 -> ArmyPlacer RW 0x782214 (reinforcement): the entries at the spawn point, a move to the gather point
bool StartingBase::callInLivingWorldArmy(GameLogic &logic, int armyId)
{
	LivingWorldBattleTags &tags = logic.livingWorldTags();
	LivingWorldBattleTags::Reinforcement *r = tags.reinforcement(armyId);
	Player *player = r ? logic.players().getNthPlayer(r->playerIndex) : nullptr;
	const TerrainLogic *terrain = logic.terrain();
	if (!r || r->state != 3 || !player || !terrain)
	{
		return false;
	}
	r->state = 4;
	// getSpawnPositions RW 0x80F754: the first choice the map has (a missing gather point: the spawn point)
	const Waypoint *spawn = nullptr;
	const Waypoint *gather = nullptr;
	for (const auto &choice : r->spawnPoints)
	{
		spawn = terrain->findWaypointByName(choice.first);
		if (spawn)
		{
			gather = terrain->findWaypointByName(choice.second);
			break;
		}
	}
	if (!spawn)
	{
		return true; // no point at all: retail's placement fails the same way (nothing appears) [S-1329]
	}
	const Coord3D target = gather ? gather->location : spawn->location;
	Result result;
	int k = 0;
	const LivingWorldBattleTags::Reinforcement rec = *r; // the records may grow while objects are made
	for (const LivingWorldBattleTags::Member &m : rec.members)
	{
		for (int q = 0; q < m.quantity; ++q)
		{
			Object *o = placeObjectAtPosition(logic, *player, m.thingTemplate, armySlot(logic, spawn->location, k++), result);
			if (!o)
			{
				continue;
			}
			LivingWorldBattleTags::Tag tag;
			tag.armyId = rec.armyId;
			tag.spawnId = m.spawnId;
			logic.livingWorldTags().objects[o->getID()] = tag;
			carryOver(*o, m.experience, m.upgrades, "reinforcement " + m.thingTemplate, result.errors);
			AICommand c; // the group move to the gather point (RW 0x76E8B0 in the WalkOnTemplate formation; here each unit to the point)
			c.type = AICMD_MOVE_TO_POSITION;
			c.path.push_back(target);
			logic.aiCommands().issue(*o, std::move(c));
		}
	}
	logic.livingWorldTags().arrivals.push_back(rec.armyId);
	return true;
}

// MSG_CALL_IN_REINFORCEMENTS (0x45F, the army id: the banner's button) and MSG_SUMMON_REINFORCEMENTS (0x45E: engage from state 0, never in a War of the Ring
// battle, RW 0x80FEB2). [port decision, stricter than retail: an army is called in only by its own player]
void StartingBase::registerLivingWorldHandlers(GameLogicDispatch &dispatcher)
{
	dispatcher.registerHandler(MSG_CALL_IN_REINFORCEMENTS, "WOTR2-M7", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a = m.getArgument(0);
		if (!a)
		{
			return false;
		}
		LivingWorldBattleTags::Reinforcement *r = logic.livingWorldTags().reinforcement(a->integer);
		if (r && r->playerIndex == m.getPlayerIndex())
		{
			callInLivingWorldArmy(logic, a->integer);
		}
		return true;
	});
	dispatcher.registerHandler(MSG_SUMMON_REINFORCEMENTS, "WOTR2-M7", [](GameLogic &, const GameMessage &m) { return m.getArgument(0) != nullptr; });
}

std::vector<std::string> StartingBase::stopLines()
{
	return {
		"[S-272] starting base: ported from the RotWK 2.01 disassembly (RW 0x62AC17; S-001 caveat). Not ported: Player::onStructureCreated / onStructureConstructionComplete / "
		"onUnitCreated, the team activation, the pathfinder registration and the adjusted destination of mobile units, the partition search of a "
		"zero-offset starting unit (an error), and the CRT acos / sin / cos parity (S-081 / S-167)",
	};
}
