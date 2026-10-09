// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GarrisonContain / HordeGarrisonContain at runtime. See GameLogic/Object/Contain/GarrisonContainRuntime.h for the target facts and the stops. Lane GARRISON-1.

#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"

#include "Common/NumericState.h"
#include "GameLogic/AI/AIPathfind.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace
{
// RW 0x6F5F65 (lane GARRISON-2): the path test to a container whose entry point lies in its footprint. The container's cell is not an obstacle: the quick path
// test to its position (RW's second probe at the position plus the template's first offset, RW 0xAD1AE0(0), is not read: S-1107). An obstacle: from the centre
// cell (RW 0x6E8CE6, centred) a line (RW 0x6E94B2) of d = 2 - (int)(bounding circle radius x -0.1) cells in each of the four directions (+x, -x, +y, -y, RW
// 0x6F5EF3) finds the first cell of another obstacle id (RW 0x6E86D2); its zone (the terrain variant RW 0x938D89 of the locomotor) must be the unit's zone (the
// portal search RW 0x6F5547 between two zones is not ported, S-161: different zones fail)
bool quickPathToContainer(Pathfinder &pf, const PathfindLocomotorInfo &loco, const Coord3D &from, const Object &container)
{
	Coord3D to = *container.getPosition();
	ICoord2D centre;
	pf.worldToCell(&to, true, &centre);
	const PathfindCell *goal = pf.getCell(LAYER_GROUND, centre.x, centre.y);
	if (!goal)
	{
		return false;
	}
	if (goal->getType() != PathfindCell::CELL_OBSTACLE)
	{
		return pf.clientSafeQuickDoesPathExist(loco, &from, &to);
	}
	const PathfindMovement mv = Pathfinder::makeMovement(nullptr, loco);
	const PathfindCell *fromCell = pf.getClippedCell(LAYER_GROUND, &from);
	if (!fromCell)
	{
		return false;
	}
	const zoneStorageType fromZone = pf.zoneManager().getEffectiveZone(mv, true, fromCell->getZone());
	const PathfindObjectID obstacle = goal->getObstacleID();
	const int d = 2 - SimMath::truncToInt32(NumericState::pc24Mul(CombatQueries::boundingCircleRadius(container), -0.1f));
	const int dirs[4][2] = { { d, 0 }, { -d, 0 }, { 0, d }, { 0, -d } };
	for (const auto &dir : dirs)
	{
		// RW 0x6E94B2: Bresenham from the centre to the end cell, both included
		const int x0 = centre.x, y0 = centre.y, x1 = centre.x + dir[0], y1 = centre.y + dir[1];
		const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
		for (int k = 0; k <= steps; ++k)
		{
			const int x = steps == 0 ? x0 : x0 + (x1 - x0) * k / steps;
			const int y = steps == 0 ? y0 : y0 + (y1 - y0) * k / steps;
			const PathfindCell *c = pf.getCell(LAYER_GROUND, x, y);
			if (!c)
			{
				break; // off the grid: this direction finds nothing
			}
			if (c->getObstacleID() != obstacle)
			{
				if (pf.zoneManager().getEffectiveZone(mv, true, c->getZone()) == fromZone)
				{
					return true;
				}
				break;
			}
		}
	}
	return false;
}

const char *const kStopRuntime =
	"[S-1100] GarrisonContain / HordeGarrisonContain (lane GARRISON-1): ported from RW 0x86603B / 0x87B8C5 / 0x87D0E0 (who may enter), 0x8674C2 / 0x87CFEB / 0x990DEA "
	"(enter: a horde and then its members), 0x87BEC8 / 0x87BFA6 (on containing / removing), 0x87C2B7 .. 0x87BD77 (the garrison points: the PassengerBonePrefix bones of "
	"the three damage states, the point nearest to a rider's victim), 0x87C8DF (update), 0x867120 (death: EjectPassengersOnDeath ejects, else the riders are "
	"destroyed) and 0x87D3CA / 0x87CA2B (exit: ExitDelay between members, the EntryPosition with the exit cell test RW 0x6E8707 (lane GARRISON-3), the exit path to the ExitOffset). Not ported: the door model conditions, the "
	"enter / exit sounds, ModifierToGiveOnExit, the apparent controlling player and the radar, DamagePercentToUnits' passenger damage, HealObjects, MobileGarrison, "
	"KillPassengersOnDeath's fling, the rally point (S-1620), the exit position search of removeAllContained (RW 0x87B3EC), the CHUNK_VENDOR / "
	"ROCK_VENDOR entry test (RW 0x8660F0) and the second path test of the horde garrison's checkPath (RW 0x6F5F65)";

const char *const kStopNotPorted =
	"[S-1103] not ported by lanes GARRISON-1 / GARRISON-2: HordeTransportContainDamage (5 templates, an unported module, S-140), the siege "
	"tower's and the ladders' docking (SiegeDeploySpecialPower, SiegeAIUpdate, SiegeDockingBehavior, DynamicPortalBehaviour; the wall layer, S-164), the bridge layer (BridgeBehavior / "
	"BridgeTowerBehavior / BridgeScaffoldBehavior, the pathfinder's bridge layers and destroyed bridges, S-362), the AI mood multipliers of the garrison range cap "
	"(RW 0x6FF44A), the garrison pips (the exit / evacuate buttons: lane UI-1, S-1260), MSG_EVACUATE_CONTESTERS";

const char *const kStopLink =
	"[S-1102] a horde in a HordeGarrisonContain (lane GARRISON-1, HordeGarrisonLink.cpp): ported from the HordeContainInterface slots RW 0x86EE1B / 0x86EBF4 / "
	"0x8757FC / 0x875DC3 / 0x876B25 / 0x875C93 (the exit's re-join, RW 0x86F18F / 0x8759FF: lane GARRISON-3, S-1620); inference: a member on the way whose object is gone leaves the map, the member pass does not run while garrisoned, the members' AI processes DISABLED_HELD (RW 0x855830) so they fight from inside";

const char *const kStopExit =
	"[S-1620] a horde leaving a garrison (lane GARRISON-3, the owner's feedback G6): ported from RW 0x87D3CA / 0x87CA2B (every member released at the "
	"EntryPosition, the cell test RW 0x6E8707 and its two fallbacks along the facing, the ExitOffset adjusted by RW 0x6F3C87, AI command 10 with the container), "
	"RW 0x86EBF4 / 0x86CF2A / 0x872D0A (a member on the way keeps its slot and rejoins unplaced), RW 0x86F18F (the horde recentred on its members) and RW "
	"0x8759FF (the members taken back, the fill-in, the re-form RW 0x877E12, the synced model conditions RW 0x86D061). Not ported: the contain's rally point "
	"(exit slots 0x1C / 0x20: the ExitOffset is the second waypoint), the layers of RW 0x68BB9D, the UNDER_CONSTRUCTION branch of RW 0x8759FF (busy and walk "
	"back, counted). Inference: a member on the way that is gone or dead gives its slot back; the registered set of RW 0x873F30 and the map of the members on the "
	"way are kept apart (RW: one set, interface + 0x54)";

int statusBit(const char *name)
{
	return CombatNames::status(name);
}

bool maskTest(const ObjectStatusMaskType &m, int bit)
{
	return bit >= 0 && (m[(size_t)bit >> 5] >> (bit & 31)) & 1u;
}

// RW 0x6901AE / 0x69024C set and clear every bit of the mask through RW 0x68D440
void setStatusMask(Object &obj, const ObjectStatusMaskType &mask, bool on)
{
	for (int bit = 0; bit < (int)(mask.size() * 32); ++bit)
	{
		if (maskTest(mask, bit))
		{
			obj.setStatus((unsigned)bit, on);
		}
	}
}

HordeContainInterface *hordeOf(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

// RW 0x87B6B2 calcDistSqr: x87 at 24 bits, ((dz * dz + dy * dy) + dx * dx); the differences are exact float subtractions
float calcDistSqr(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::pc24Sub(a.x, b.x);
	const float dy = SimMath::pc24Sub(a.y, b.y);
	const float dz = SimMath::pc24Sub(a.z, b.z);
	return SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(dz, dz), SimMath::pc24Mul(dy, dy)), SimMath::pc24Mul(dx, dx));
}

bool sameCoord(const Coord3D &a, const Coord3D &b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z; // RW 0x87B722 .. 0x87B747: ucomiss, equal only
}
} // namespace

GarrisonContain::GarrisonContain(Thing *thing, const ModuleData *data, const GarrisonContainModuleData &garrison, const DieMuxData &dieMux,
	const HordeGarrisonContainModuleData *horde)
	: OpenContain(thing, data, garrison.m_open, dieMux)
	, m_garrison(&garrison)
	, m_horde(horde)
{
	for (auto &row : m_pointPositions)
	{
		row.fill(Coord3D{ 0.0f, 0.0f, 0.0f });
	}
	// RW 0x87D334 .. 0x87D34C (HordeGarrisonContain's constructor): the object's model condition word + 0x114 bit 10 (bit 74, CONSTRUCTION_COMPLETE) when it was clear
	if (m_horde)
	{
		if (Object *obj = getObject())
		{
			const int bit = CombatNames::modelCondition("CONSTRUCTION_COMPLETE");
			if (!obj->testModelCondition(bit))
			{
				obj->setModelConditionState(bit, true);
			}
		}
	}
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x87C8DF answers 1: every frame
	for (const std::string &line : stopLines())
	{
		getObject()->logic().noteStop(line); // the stops of the garrisons reach GameLogic::report().stops in every game that has one
	}
	getObject()->logic().noteStop(GarrisonRules::stopLine());
}

GarrisonContain::~GarrisonContain() = default;

void GarrisonContain::registerClasses(ModuleFactory &modules)
{
	modules.bindTypedData<GarrisonContainBehaviorData>("GarrisonContain", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<HordeGarrisonContainBehaviorData>("HordeGarrisonContain", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("GarrisonContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const GarrisonContainBehaviorData *d = dynamic_cast<const GarrisonContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("GarrisonContain: the module data is not typed (GarrisonContainBehaviorData)");
		}
		return std::make_unique<GarrisonContain>(thing, data, d->garrison, d->m_dieMux, nullptr);
	});
	modules.bindModuleProc("HordeGarrisonContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const HordeGarrisonContainBehaviorData *d = dynamic_cast<const HordeGarrisonContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("HordeGarrisonContain: the module data is not typed (HordeGarrisonContainBehaviorData)");
		}
		return std::make_unique<GarrisonContain>(thing, data, d->horde.m_garrison, d->m_dieMux, &d->horde);
	});
}

std::vector<std::string> GarrisonContain::stopLines()
{
	return { kStopRuntime, kStopLink, kStopNotPorted, kStopExit };
}





int GarrisonContain::getContainMax() const
{
	return openData().m_containMax; // RW 0x86584C: data + 0x70
}

// RW 0x87CEB3: the point through the container's transform, SSE order ((m[r][2] z + m[r][1] y) + m[r][0] x) + t[r]
void GarrisonContain::transformPoint(const Coord3D &local, Coord3D &out) const
{
	const Object *obj = getObject();
	const float *m = obj->getBasis();
	const Coord3D &t = *obj->getPosition();
	const float tt[3] = { t.x, t.y, t.z };
	float w[3];
	for (int r = 0; r < 3; ++r)
	{
		float v = SimMath::addf32(SimMath::mulf32(m[r * 3 + 2], local.z), SimMath::mulf32(m[r * 3 + 1], local.y));
		v = SimMath::addf32(v, SimMath::mulf32(m[r * 3 + 0], local.x));
		w[r] = SimMath::addf32(v, tt[r]);
	}
	out = Coord3D{ w[0], w[1], w[2] };
}

// slots 0x158 / 0x15C / 0x160: the horde garrison's data positions through the transform (RW 0x87D1A7 / 0x87D1E0 / 0x87D219); OpenContain's answer is the container's
// position (RW 0x8657D1 / 0x867ACD, the SHIP branch aside)
bool GarrisonContain::getEntryPosition(Coord3D &out) const
{
	if (!m_horde)
	{
		out = *getObject()->getPosition();
		return true;
	}
	transformPoint(m_horde->m_entryPosition, out);
	return true;
}

bool GarrisonContain::getEntryOffset(Coord3D &out) const
{
	if (!m_horde)
	{
		out = *getObject()->getPosition(); // RW 0x8657B6: slot 0x158's answer for a SHIP, else the position
		return true;
	}
	transformPoint(m_horde->m_entryOffset, out);
	return true;
}

bool GarrisonContain::getExitOffset(Coord3D &out) const
{
	if (!m_horde)
	{
		out = *getObject()->getPosition();
		return true;
	}
	transformPoint(m_horde->m_exitOffset, out);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// who may enter
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x87D252 (the horde garrison's main slot 0x70): a CAN_ENTER_ANYTHING object of another player always fits; otherwise the contained count is below ContainMax
bool GarrisonContain::hasRoomFor(const Object &obj) const
{
	static const int canEnterAnything = statusBit("CAN_ENTER_ANYTHING");
	if (obj.testStatus((unsigned)canEnterAnything) && getObject()->getControllingPlayer() != obj.getControllingPlayer())
	{
		return true;
	}
	return (int)getContainCount() < getContainMax();
}

// RW 0x87B8C5 (GarrisonContain, through OpenContain's RW 0x86603B)
bool GarrisonContain::garrisonAllows(const Object &obj) const
{
	static const int sold = statusBit("SOLD");
	static const int garrisonableUntilDestroyed = CombatNames::kindOf("GARRISONABLE_UNTIL_DESTROYED");
	const Object *container = getObject();
	if (!openContainAllows(obj)) // RW 0x87B8D7: OpenContain RW 0x86603B
	{
		return false;
	}
	// ---- GarrisonContain RW 0x87B8C5 ----
	const BodyModuleInterface *body = container->getBodyModule();
	if (!body || !(body->getHealth() > 0.0f))
	{
		return false;
	}
	if (container->testStatus((unsigned)sold))
	{
		return false;
	}
	if (body->getDamageState() == BODY_REALLYDAMAGED && !container->isKindOf((unsigned)garrisonableUntilDestroyed))
	{
		return false;
	}
	return true;
}

// the contain interface's slot 0x98: GarrisonContain's wrapper RW 0x87B935, HordeGarrisonContain's RW 0x87D0E0; both ask RW 0x87B8C5 first
bool GarrisonContain::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	static const CombatNames::Status &st = CombatNames::statuses();
	if (!garrisonAllows(obj))
	{
		return false;
	}
	if (!m_horde)
	{
		// RW 0x87B935 (GarrisonContain's contain interface slot 0x98, the wrapper of RW 0x87B8C5): a NO_GARRISON object (template + 0x10B bit 3, KindOf 27) never
		// enters; with checkCapacity the contained count (interface slot 0x114(0) = RW 0x865CE3: + 0x38) must be below ContainMax (slot 0x70 = RW 0x86584C), a
		// signed compare
		static const int noGarrison = CombatNames::kindOf("NO_GARRISON");
		(void)checkPath;
		if (obj.isKindOf((unsigned)noGarrison))
		{
			return false;
		}
		return !checkCapacity || (int)getContainCount() < getContainMax();
	}
	// ---- HordeGarrisonContain RW 0x87D0E0 ----
	if (checkPath)
	{
		// RW 0x87D114 .. 0x87D153: a path from the object to the EntryOffset (RW 0x6F5BB0), else to the container itself (RW 0x6F5F65, lane GARRISON-2)
		const AIUpdateInterface *ai = obj.getAIUpdateInterface();
		if (ai)
		{
			Coord3D to;
			getEntryOffset(to);
			Coord3D from = *obj.getPosition();
			Pathfinder &pf = ai->world().pathfinder();
			if (!pf.clientSafeQuickDoesPathExist(ai->locomotorInfo(), &from, &to) && !quickPathToContainer(pf, ai->locomotorInfo(), from, *getObject()))
			{
				return false;
			}
		}
	}
	if (hordeOf(obj))
	{
		return checkCapacity ? hasRoomFor(obj) : true;
	}
	if (obj.testStatus((unsigned)st.hordeMember))
	{
		Object *horde = nullptr;
		return hordeMemberInside(obj, &horde); // RW 0x87D19A: the member's horde is inside
	}
	return hasRoomFor(obj); // RW 0x87D176: tested whatever checkCapacity says
}

// RW 0x990CB9: a contained object whose horde contain counts `obj` as a member (or one on its way in)
bool GarrisonContain::hordeMemberInside(const Object &obj, Object **horde) const
{
	for (Object *c : *getContainedItemsList()) // contain slot 0x118 (a tunnel answers its network's list)
	{
		HordeContainInterface *h = hordeOf(*c);
		if (h && h->isMemberOrEntering(obj))
		{
			*horde = c;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// entering
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x87CFEB (HordeGarrisonContain) / RW 0x8674C2 (OpenContain)
bool GarrisonContain::addToContain(Object *obj)
{
	if (!obj || obj->getContainedBy())
	{
		return false; // RW 0x87CFF1 / 0x8674E0: an object in a container is not added
	}
	if (!m_horde)
	{
		openContainAdd(obj);
		return true;
	}
	if (HordeContainInterface *h = hordeOf(*obj))
	{
		h->setGarrisoned(true); // RW 0x87D013: horde slot 0x124
		openContainAdd(obj);    // main slot 0x74 = RW 0x87CE1B: OpenContain's add ...
		// ... then RW 0x990E5F: a selected horde of the local player leaves the selection (client side), it takes UNSELECTABLE and leaves the world unless it holds the Ring
		static const int unselectable = statusBit("UNSELECTABLE");
		static const int holdingTheRing = statusBit("HOLDING_THE_RING");
		obj->setStatus((unsigned)unselectable, true);
		if (obj->isInWorld() && !obj->testStatus((unsigned)holdingTheRing))
		{
			obj->logic().friend_containLeaveWorld(*obj);
		}
		redeployOccupants(); // main slot 0x48
		return true;
	}
	Object *horde = nullptr;
	if (hordeMemberInside(*obj, &horde))
	{
		// main slot 0x78 = RW 0x87CD76 -> RW 0x990DEA: the member's horde takes it back (when the horde still waits for members) ...
		HordeContainInterface *h = hordeOf(*horde);
		bool joined = false;
		if (!h->allMembersEntered())
		{
			h->acceptMemberFromGarrison(obj);
			h->setLastMemberEnteredFrame(obj->logic().getFrame());
			if (h->allMembersEntered())
			{
				// RW 0x990E35: a global byte (RW 0xDE7744 + 0x28) is raised, the frame cleared
				h->setLastMemberEnteredFrame(0);
			}
			joined = true;
		}
		else
		{
			h->setLastMemberEnteredFrame(0);
		}
		if (joined)
		{
			++m_stats.membersEntered;
			onContaining(obj, false); // RW 0x87CD96: GarrisonContain::onContaining(member, false)
			redeployOccupants();
		}
		redeployOccupants(); // RW 0x87D024: main slot 0x48 again
		return joined;
	}
	openContainAdd(obj); // main slot 0x74
	redeployOccupants();
	return true;
}





// RW 0x87BEC8 GarrisonContain::onContaining
void GarrisonContain::onContaining(Object *obj, bool wasSelected)
{
	(void)wasSelected; // RW 0x865F9A OpenContain::onContaining: the EnterSound (client side)
	static const CombatNames::Status &st = CombatNames::statuses();
	Object *container = getObject();
	obj->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER);       // RW 0x87BEE6: DISABLED_HELD (RW 0x692432)
	obj->setStatus((unsigned)st.insideGarrison, true);            // RW 0x87BEF1
	container->setStatus((unsigned)st.canAttack, true);           // RW 0x87BEFC
	obj->setWeaponBonusCondition(0, true);                        // RW 0x87BF01: + 0x39C bit 0 (GARRISONED)
	obj->setPosition(container->getPosition());                   // RW 0x87BF0E
	if (ridersFireFromContainer())
	{
		obj->setDrawableHidden(true); // RW 0x87BF38
	}
	recalcApparentControllingPlayer(); // RW 0x87BF41: slot 0x50
	// RW 0x87BF44 .. 0x87BF9F: the rider's DISABLED type 0x14 (none in the port's 11 types) is cleared on it and its members: no type 0x14 exists (S-148)
}

// ---------------------------------------------------------------------------------------------------------------------------------
// leaving
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x87D04B (HordeGarrisonContain) / RW 0x8665BE (OpenContain)
void GarrisonContain::removeFromContain(Object *obj)
{
	if (!obj)
	{
		return;
	}
	if (!m_horde)
	{
		openContainRemove(obj);
		return;
	}
	if (HordeContainInterface *h = hordeOf(*obj))
	{
		openContainRemove(obj);           // RW 0x87CE3D: OpenContain's removal ...
		Object *rider = obj;              // ... and RW 0x990EE6: back into the world, not UNSELECTABLE
		if (!rider->isInWorld())
		{
			rider->logic().friend_containEnterWorld(*rider);
		}
		static const int unselectable = statusBit("UNSELECTABLE");
		rider->setStatus((unsigned)unselectable, false);
		h->setGarrisoned(false); // horde slot 0x128
	}
	else
	{
		static const CombatNames::Status &st = CombatNames::statuses();
		if (obj->testStatus((unsigned)st.hordeMember))
		{
			// RW 0x87CDD6: a member: GarrisonContain::onRemoving, its drawable shown, back into the world
			onRemoving(obj);
			obj->setDrawableHidden(false);
			if (!obj->isInWorld())
			{
				obj->logic().friend_containEnterWorld(*obj);
			}
		}
		else
		{
			openContainRemove(obj);
			obj->setDrawableHidden(false); // RW 0x87D0A5 .. 0x87D0BF: a hidden drawable is shown
		}
	}
	m_nextExitFrame = m_horde->m_exitDelay + getObject()->logic().getFrame(); // RW 0x87D0C4: ExitDelay + now
}


// RW 0x87BFA6 GarrisonContain::onRemoving
void GarrisonContain::onRemoving(Object *obj)
{
	static const CombatNames::Status &st = CombatNames::statuses();
	Object *container = getObject();
	// RW 0x8680F4 OpenContain::onRemoving: the ExitSound, the door conditions, ModifierToGiveOnExit (S-1100)
	removeObjectFromGarrisonPoint(obj, -1);                  // RW 0x87BFCC
	obj->setWeaponBonusCondition(0, false);                  // RW 0x87BFD1
	obj->clearDisabled(3);                                   // RW 0x87BFDE: DISABLED_HELD
	obj->setStatus((unsigned)st.insideGarrison, false);      // RW 0x87BFE9
	if (getContainCount() == 0)
	{
		container->setStatus((unsigned)st.canAttack, false); // RW 0x87C024
		m_hiddenFlag = false;
		static const int garrisoned = CombatNames::modelCondition("GARRISONED");
		if (container->testModelCondition(garrisoned))
		{
			container->setModelConditionState(garrisoned, false); // RW 0x87C03C
		}
	}
	else if ((int)getContainCount() != (int)m_stealthUnitsContained)
	{
		m_hiddenFlag = false;
	}
	recalcApparentControllingPlayer(); // RW 0x87C06B: slot 0x50
	// RW 0x87C073: the killing branch (KillPassengersOnDeath with the dying byte and no health left): the rider is flung and killed, an ENCLOSED one destroyed
	if (m_killingRiders && openData().m_killPassengersOnDeath && container->getBodyModule() && !(container->getBodyModule()->getHealth() > 0.0f))
	{
		++m_openStats.ridersDestroyedOnDeath;
		container->logic().reportError("GarrisonContain of " + container->getTemplate()->getName() + ": the KillPassengersOnDeath branch of RW 0x87BFA6 is not ported (S-1100)");
	}
}


// RW 0x87C6CE GarrisonContain::recalcApparentControllingPlayer, the ported part: with riders the container shows GARRISONED (RW 0x87C86E, for the
// local player's own or a not-stealthed garrison: the logic sets it whatever the viewer, S-1100), then the points are loaded once (RW 0x87C895)
void GarrisonContain::recalcApparentControllingPlayer()
{
	Object *container = getObject();
	if (getContainCount() == 0)
	{
		m_hiddenFlag = false;
		return;
	}
	static const int garrisoned = CombatNames::modelCondition("GARRISONED");
	if (!container->testModelCondition(garrisoned))
	{
		container->setModelConditionState(garrisoned, true);
	}
	if (!m_pointsLoaded)
	{
		loadGarrisonPoints();
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the garrison points
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x87B0F1
int GarrisonContain::findConditionIndex() const
{
	const BodyModuleInterface *body = getObject()->getBodyModule();
	const int state = body ? (int)body->getDamageState() : 0;
	if (state == 0)
	{
		return 0;
	}
	if (state == 1)
	{
		return 1;
	}
	return state > 1 && state < 4 ? 2 : -1;
}

// RW 0x87C2B7 GarrisonContain::loadGarrisonPoints
void GarrisonContain::loadGarrisonPoints()
{
	Object *container = getObject();
	for (auto &row : m_pointPositions)
	{
		row.fill(*container->getPosition());
	}
	// the bone prefix: RW 0x86630B(null) is the first PassengerBonePrefix entry's prefix, "ARROW_" (RW 0xC59988) when there is none
	const std::vector<PassengerBonePrefixEntry> &prefixes = openData().m_passengerBonePrefix;
	const std::string prefix = prefixes.empty() ? std::string("ARROW_") : prefixes.front().bonePrefix;
	// the three damage states' flags: RW 0x87C30F .. 0x87C3F8 (clear DAMAGED / REALLYDAMAGED / RUBBLE / SPECIAL_DAMAGED and set GARRISONED; clear REALLYDAMAGED /
	// RUBBLE / SPECIAL_DAMAGED and set DAMAGED; clear DAMAGED / RUBBLE / SPECIAL_DAMAGED and set REALLYDAMAGED) applied to the object's flags for the query
	static const int damaged = CombatNames::modelCondition("DAMAGED");
	static const int reallyDamaged = CombatNames::modelCondition("REALLYDAMAGED");
	static const int rubble = CombatNames::modelCondition("RUBBLE");
	static const int specialDamaged = CombatNames::modelCondition("SPECIAL_DAMAGED");
	static const int garrisoned = CombatNames::modelCondition("GARRISONED");
	const int clearBits[3][3] = { { damaged, reallyDamaged, rubble }, { reallyDamaged, rubble, specialDamaged }, { damaged, rubble, specialDamaged } };
	const int setBits[3] = { garrisoned, damaged, reallyDamaged };
	ProjectileLaunchOffsets *provider = container->logic().combat().launchOffsets();
	for (int c = 0; c < GARRISON_POINT_CONDITIONS; ++c)
	{
		Object::ModelConditionBits bits = container->getModelConditionBits();
		auto clearBit = [&bits](int b) { bits[(size_t)b >> 5] &= ~(1u << (b & 31)); };
		for (int b : clearBits[c])
		{
			clearBit(b);
		}
		if (c == 0)
		{
			clearBit(specialDamaged);
		}
		bits[(size_t)setBits[c] >> 5] |= 1u << (setBits[c] & 31);
		int count = 0;
		if (!provider)
		{
			++m_stats.boneQueriesWithoutProvider; // no W3D assets: no bones (S-460)
		}
		else
		{
			float bones[MAX_GARRISON_POINTS][12];
			count = provider->multiLogicalBones(*container, prefix, bits, MAX_GARRISON_POINTS, bones);
			for (int i = 0; i < count; ++i)
			{
				// RW 0x68C6BD: RW 0x70BCE7 convertBonePosToWorldPos, the translation through the object's transform
				transformPoint(Coord3D{ bones[i][3], bones[i][7], bones[i][11] }, m_pointPositions[c][(size_t)i]);
			}
			if (count < 0)
			{
				count = 0;
			}
		}
		m_pointCount[c] = count;
	}
	m_pointsLoaded = true; // RW 0x87C54C
	if (garrison().m_mobileGarrison)
	{
		++m_stats.mobileUnported; // RW 0x87C55D (S-1100)
	}
}

// RW slot 0x6C of the main vtable (BFME1 getObjectGarrisonPointIndex): the first point that holds `id`
int GarrisonContain::getObjectGarrisonPointIndex(ObjectID id) const
{
	if (id == INVALID_ID)
	{
		return -1;
	}
	for (int i = 0; i < MAX_GARRISON_POINTS; ++i)
	{
		if (m_points[(size_t)i].object == id)
		{
			return i;
		}
	}
	return -1;
}

// RW 0x87B6E1
int GarrisonContain::findClosestFreeGarrisonPointIndex(int condition, const Coord3D *targetPos) const
{
	if (!targetPos || m_pointsInUse == MAX_GARRISON_POINTS || condition < 0 || m_pointCount[condition] <= m_pointsInUse)
	{
		return -1;
	}
	if (sameCoord(*targetPos, *getObject()->getPosition()))
	{
		for (int i = 0; i < MAX_GARRISON_POINTS; ++i)
		{
			if (m_points[(size_t)i].object == INVALID_ID)
			{
				return i;
			}
		}
		return condition; // RW 0x87B763: the loop's fall-through answers the condition index
	}
	int best = -1;
	float bestDist = 3.4028234663852886e+38f; // RW 0xBD1910
	for (int i = 0; i < m_pointCount[condition]; ++i)
	{
		if (m_points[(size_t)i].object != INVALID_ID)
		{
			continue;
		}
		const float d = calcDistSqr(*targetPos, m_pointPositions[(size_t)condition][(size_t)i]);
		if (bestDist > d) // RW 0x87B7B0: fcompi, strictly nearer
		{
			bestDist = d;
			best = i;
		}
	}
	return best;
}

// RW 0x87B042
void GarrisonContain::putObjectAtGarrisonPoint(Object *obj, ObjectID target, int condition, int index)
{
	if (!obj || index < 0 || index >= MAX_GARRISON_POINTS || condition < 0 || condition >= GARRISON_POINT_CONDITIONS)
	{
		return;
	}
	GarrisonPoint &p = m_points[(size_t)index];
	if (p.object != INVALID_ID)
	{
		return;
	}
	Coord3D pos = m_pointPositions[(size_t)condition][(size_t)index];
	obj->setPosition(&pos);
	p.object = obj->getID();
	p.target = target;
	p.placedFrame = obj->logic().getFrame();
	++m_pointsInUse;
	++m_stats.pointsTaken;
}

// RW 0x87B123: index -1 removes `obj` from every point that holds it; else the point is freed when it holds it
void GarrisonContain::removeObjectFromGarrisonPoint(Object *obj, int index)
{
	if (!obj)
	{
		return;
	}
	if (index == -1)
	{
		for (int i = 0; i < MAX_GARRISON_POINTS; ++i)
		{
			if (m_points[(size_t)i].object == obj->getID())
			{
				removeObjectFromGarrisonPoint(obj, i);
			}
		}
		return;
	}
	if (index < 0 || index >= MAX_GARRISON_POINTS || m_points[(size_t)index].object != obj->getID())
	{
		return;
	}
	m_points[(size_t)index] = GarrisonPoint();
	--m_pointsInUse;
}

// RW 0x87B83E
void GarrisonContain::putObjectAtBestGarrisonPoint(Object *obj, Object *target, const Coord3D *targetPos)
{
	if (!obj)
	{
		return;
	}
	const Coord3D *pos = target ? target->getPosition() : targetPos;
	if (!pos)
	{
		return;
	}
	if (getObjectGarrisonPointIndex(obj->getID()) != -1)
	{
		return; // RW 0x87B86A: already on a point
	}
	const int condition = findConditionIndex();
	const int index = findClosestFreeGarrisonPointIndex(condition, pos);
	if (index == -1)
	{
		return;
	}
	putObjectAtGarrisonPoint(obj, target ? target->getID() : INVALID_ID, condition, index);
}

// RW 0x87C5D8: a point whose object is gone, or whose object's weapon (slot 0) answers RW 0x441B59, is freed. The weapon test is not identified (S-1100): only
// the vanished objects are freed
void GarrisonContain::removeInvalidObjectsFromGarrisonPoints()
{
	if (m_pointsInUse == 0)
	{
		return;
	}
	for (int i = 0; i < MAX_GARRISON_POINTS; ++i)
	{
		const ObjectID id = m_points[(size_t)i].object;
		if (id != INVALID_ID && !getObject()->logic().findObjectByID(id))
		{
			m_points[(size_t)i] = GarrisonPoint();
			--m_pointsInUse;
		}
	}
}

namespace
{
// the victim / victim position pair of RW 0x668303 / 0x664CB3 (AI getCurrentVictim / the attack position)
void victimOf(Object &obj, Object *&victim, const Coord3D *&victimPos)
{
	victim = nullptr;
	victimPos = nullptr;
	if (AIUpdateInterface *ai = obj.getAIUpdateInterface())
	{
		victim = ai->currentVictim();
	}
}
} // namespace

// RW 0x87BBE8 GarrisonContain::addValidObjectsToGarrisonPoints
void GarrisonContain::addValidObjectsToGarrisonPoints()
{
	static const int contesting = statusBit("CONTESTING_BUILDING");
	const std::vector<Object *> contained(m_contained.begin(), m_contained.end());
	for (Object *obj : contained)
	{
		if (obj->testStatus((unsigned)contesting))
		{
			continue;
		}
		HordeContainInterface *h = hordeOf(*obj);
		std::vector<Object *> riders;
		if (!obj->getContain())
		{
			riders.push_back(obj);
		}
		else if (h)
		{
			// RW 0x87BC4E: the horde's members (horde slot 0x108) that have a weapon in slot 0 not answering RW 0x441B59 (not identified: every armed member, S-1100)
			if (const ContainedItemsList *members = obj->getContain()->getContainedItemsList())
			{
				for (Object *m : *members)
				{
					if (m->hasAnyWeapon())
					{
						riders.push_back(m);
					}
				}
			}
		}
		else
		{
			continue; // RW 0x87BC40: a rider with a contain that is not a horde
		}
		for (Object *r : riders)
		{
			if (!r->getAIUpdateInterface())
			{
				continue;
			}
			Object *victim = nullptr;
			const Coord3D *victimPos = nullptr;
			victimOf(*r, victim, victimPos);
			const Coord3D *pos = nullptr;
			if (!victim && !victimPos)
			{
				if (ridersFireFromContainer())
				{
					continue; // RW 0x87BCD9 / 0x87BD4D: an ENCLOSED rider without a victim takes no point
				}
				pos = r->getPosition();
			}
			else
			{
				pos = victimPos;
			}
			putObjectAtBestGarrisonPoint(r, victim, pos);
		}
	}
}

// RW 0x87C631 GarrisonContain::redeployOccupants
void GarrisonContain::redeployOccupants()
{
	Object *container = getObject();
	static const int garrisoned = CombatNames::modelCondition("GARRISONED");
	if (getContainCount() > 0 && !container->testModelCondition(garrisoned))
	{
		container->setModelConditionState(garrisoned, true); // RW 0x87C672
	}
	// RW 0x87C679 .. 0x87C6C7: the points' + 0x08 field is kept over the remove / add pass for the objects that keep a point
	std::array<GarrisonPoint, MAX_GARRISON_POINTS> saved = m_points;
	removeInvalidObjectsFromGarrisonPoints();
	addValidObjectsToGarrisonPoints();
	for (const GarrisonPoint &p : saved)
	{
		if (p.object == INVALID_ID)
		{
			continue;
		}
		const int index = getObjectGarrisonPointIndex(p.object);
		if (index != -1)
		{
			m_points[(size_t)index].placedFrame = p.placedFrame;
		}
	}
}

// RW 0x87BD77 GarrisonContain::trackTargets
void GarrisonContain::trackTargets()
{
	const int condition = findConditionIndex();
	const std::vector<Object *> contained(m_contained.begin(), m_contained.end());
	for (Object *obj : contained)
	{
		const int index = getObjectGarrisonPointIndex(obj->getID());
		if (index == -1 || !obj->getAIUpdateInterface())
		{
			continue;
		}
		Object *victim = nullptr;
		const Coord3D *victimPos = nullptr;
		victimOf(*obj, victim, victimPos);
		const Coord3D *pos = victim ? victim->getPosition() : victimPos;
		if (!pos)
		{
			continue;
		}
		const int best = findClosestFreeGarrisonPointIndex(condition, pos);
		if (best == -1)
		{
			continue;
		}
		const float toPoint = calcDistSqr(*pos, m_pointPositions[(size_t)condition][(size_t)best]);
		if (toPoint < calcDistSqr(*pos, *obj->getPosition()))
		{
			removeObjectFromGarrisonPoint(obj, index);
			putObjectAtGarrisonPoint(obj, victim ? victim->getID() : INVALID_ID, condition, best);
			++m_stats.pointsMoved;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// update, death, creation
// ---------------------------------------------------------------------------------------------------------------------------------

void GarrisonContain::onObjectCreated()
{
	createInitialRoster();
}

// RW 0x87BA4C: InitialRoster count objects of the named template for the container's player, each added when the contain accepts it (slot 0x98(obj, 1, 0))
void GarrisonContain::createInitialRoster()
{
	if (m_rosterCreated)
	{
		return;
	}
	m_rosterCreated = true;
	const GarrisonContainModuleData &g = garrison();
	if (g.m_initialRosterCount <= 0)
	{
		return;
	}
	Object *container = getObject();
	GameLogic &logic = container->logic();
	const ThingTemplate *t = logic.things().findTemplate(g.m_initialRosterName);
	Player *owner = container->getControllingPlayer();
	if (!t || !owner)
	{
		logic.reportError("GarrisonContain of " + container->getTemplate()->getName() + ": InitialRoster " + g.m_initialRosterName + " cannot be made");
		return;
	}
	for (int i = 0; i < g.m_initialRosterCount; ++i)
	{
		Object *o = logic.newObject(t, owner->getDefaultTeam(), ObjectStatusMaskType{});
		if (o && isValidContainerFor(*o, true, false))
		{
			addToContain(o);
		}
	}
}

// RW 0x87C8DF GarrisonContain::update
UpdateSleepTime GarrisonContain::update()
{
	OpenContain::update(); // RW 0x87C8E9: OpenContain::update RW 0x86702D (the door timer, the wanters)
	const std::vector<Object *> contained(m_contained.begin(), m_contained.end());
	for (Object *obj : contained)
	{
		if (obj->isEffectivelyDead())
		{
			removeFromContain(obj); // RW 0x87C928: interface slot 0xA4(obj, false); + 0x444 (the safe occlusion frame) is client side
		}
	}
	removeInvalidObjectsFromGarrisonPoints();
	addValidObjectsToGarrisonPoints();
	trackTargets();
	if (garrison().m_doHealing && !m_contained.empty())
	{
		++m_stats.healUnported; // RW 0x87BE7C (S-1100)
	}
	return UPDATE_SLEEP_NONE;
}


// RW 0x87B4DC GarrisonContain::removeAllContained: with riders the exit position search (RW 0x87B3EC, not ported), OpenContain's RW 0x866675 (each rider through
// RW 0x865EB6 until the list is empty), slot 0x50
void GarrisonContain::removeAllContained()
{
	if (getContainCount() == 0)
	{
		return;
	}
	while (!m_contained.empty())
	{
		Object *r = m_contained.front();
		removeFromContain(r);
		if (!m_contained.empty() && m_contained.front() == r)
		{
			openContainRemove(r); // the horde garrison's removal of a member took another path: the list still holds it
		}
		++m_openStats.ejectedOnDeath;
	}
	recalcApparentControllingPlayer();
}

void GarrisonContain::onDelete()
{
	// ZH OpenContain::onDelete is not in RotWK's GarrisonContain table: the riders stay where removeAllContained / onDie left them. A container deleted with riders
	// (a destroyObject without a death) lets them go here (inference, S-1100), the parts of onRemoving that touch the riders only (the container is going away):
	// out of the contain, back in the world, shown, no longer held or INSIDE_GARRISON, a horde no longer garrisoned and its members likewise
	static const CombatNames::Status &st = CombatNames::statuses();
	auto release = [](Object *o) {
		o->clearDisabled(3);
		o->setStatus((unsigned)st.insideGarrison, false);
		o->setWeaponBonusCondition(0, false);
		o->setDrawableHidden(false);
		if (!o->isInWorld() && !o->isDestroyed())
		{
			o->logic().friend_containEnterWorld(*o);
		}
	};
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	m_contained.clear();
	for (Object *r : riders)
	{
		if (HordeContainInterface *h = hordeOf(*r))
		{
			h->setGarrisoned(false);
			if (const ContainedItemsList *members = r->getContain()->getContainedItemsList())
			{
				for (Object *m : *members)
				{
					release(m);
				}
			}
		}
		objectOnRemovedFrom(r);
		release(r);
	}
	m_points.fill(GarrisonPoint());
	m_pointsInUse = 0;
}

void GarrisonContain::containReactToTransformChange()
{
	// ZH OpenContain::containReactToTransformChange moves the riders with the container; a garrison does not move (MobileGarrison is not ported)
}

// ---------------------------------------------------------------------------------------------------------------------------------
// exiting
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x991027 (the horde garrison's slot 0x80): every rider: a horde gets slot 0x84 (its AI command 0x3E), another rider with an AI gets aiExit (command 0x1A)
void GarrisonContain::orderAllPassengersToExit(int source)
{
	const ContainedItemsList &list = *getContainedItemsList(); // RW 0x991027 reads contain slot 0x118 (a tunnel answers its network's list)
	const std::vector<Object *> riders(list.begin(), list.end());
	for (Object *r : riders)
	{
		if (HordeContainInterface *h = hordeOf(*r))
		{
			h->exitContainer(getObject(), source);
		}
		else if (r->getContain())
		{
			continue;
		}
		else if (AIUpdateInterface *ai = r->getAIUpdateInterface())
		{
			ai->aiExit(getObject(), (CommandSourceType)source);
		}
	}
}


// RW 0x87CE66 (the horde garrison's exit interface slot 0): busy until the next exit frame; GarrisonContain's RW 0x9188EB is never busy
bool GarrisonContain::isExitBusy() const
{
	return m_horde && getObject()->logic().getFrame() < m_nextExitFrame;
}

// exit interface slot 4 RW 0x88B20C: door 0 (DOOR_1) for every object
int GarrisonContain::reserveDoorForExit(const Object &obj)
{
	(void)obj;
	return 0;
}

// RW 0x87D3CA (HordeGarrisonContain) / RW 0x87CA2B (GarrisonContain)
void GarrisonContain::exitObjectViaDoor(Object *obj, int door)
{
	if (!obj)
	{
		return;
	}
	HordeContainInterface *h = m_horde ? hordeOf(*obj) : nullptr;
	if (!h)
	{
		exitOneObject(obj, door);
		return;
	}
	// the horde's members: first those out of the world, then the rest, one per call when ExitDelay is set; then the horde object
	std::vector<Object *> members;
	if (const ContainedItemsList *items = obj->getContain()->getContainedItemsList())
	{
		members.assign(items->begin(), items->end());
	}
	for (Object *m : members)
	{
		if (!m->isInWorld())
		{
			h->releaseMemberForGarrison(m); // RW 0x87D434: horde slot 0xA8
			exitOneObject(m, door);
			if (m_horde->m_exitDelay != 0)
			{
				return;
			}
		}
	}
	members.clear();
	if (const ContainedItemsList *items = obj->getContain()->getContainedItemsList())
	{
		members.assign(items->begin(), items->end());
	}
	for (Object *m : members)
	{
		h->releaseMemberForGarrison(m);
		exitOneObject(m, door);
		if (m_horde->m_exitDelay != 0)
		{
			return;
		}
	}
	exitOneObject(obj, door); // RW 0x87D492: the horde object last (RW 0x87CA2B takes its members back, slot 0x10)
}

// RW 0x87CA2B (TARGET, read in full; lane GARRISON-3, the owner's feedback G6):
//   1. contain slot 0xA4(obj, false): out of the contain;
//   2. the angle is the container's (+ 0x44), the position the EntryPosition (contain slot 0x158); with an AI (+ 0x260) and a current locomotor (AI + 0x1F0) the
//      cell is tested (RW 0x6E8707 on the ground layer): a failed test moves the point back along the container's facing by its bounding circle radius
//      (+ 0xB8; x87: pos - cos(angle) * r, pos - sin(angle) * r), a second failure to the other side (pos + 2 * cos(angle) * r, the doubling `fadd st0, st0`),
//      a third to the container's position;
//   3. RW 0x696E63(pos, 1) (Object::setPosition), RW 0x70C31E(angle), RW 0x6E85E9 (the pathfinder's addObjectToPathfindMap; a unit has no footprint);
//   4. with an AI and the object not a held horde member (RW 0x6939DF): the ExitOffset (contain slot 0x160) is adjusted for the object's locomotor set (RW
//      0x6F3C87, adjustToPossibleDestination) and is the first waypoint; the second is the rally point (exit slot 0x20, RW 0x865694: interface + 0xA0 when
//      the byte + 0xAC is set) or the ExitOffset again; around the second waypoint the object's horde (RW 0x694BF8: the object itself when it is a HORDE (RW
//      0x693A1A), else its horde container) is recentred on its members (slot 0x98, RW 0x86F18F) and takes back its members on the way (slot 0x10(0), RW
//      0x8759FF), once before the rally point when there is one and once after; then AI command 10 (RW 0x87C9B8: aiFollowExitProductionPath(path, container,
//      CMD_FROM_AI)) and RW 0x68B3AB (the drawable's rally flag, client);
//   5. recalcApparentControllingPlayer (main slot 0x50).
// A member of a horde garrison comes here released (no container, no horde): it walks the exit path on its own until the horde object, which leaves last, takes it
// back and re-forms (the members then walk to their slots from where they stand). NOT PORTED (S-1620): the rally point of a contain (exit slots 0x1C / 0x20 are
// not stored by the port's contains: the path takes the ExitOffset twice), the layer of RW 0x68BB9D (2.01 maps have the ground layer only).
void GarrisonContain::exitOneObject(Object *obj, int door)
{
	(void)door;
	Object *container = getObject();
	removeFromContain(obj); // RW 0x87CA41: slot 0xA4(obj, false)
	const float angle = container->getOrientation(); // RW 0x87CA56: + 0x44
	Coord3D pos;
	getEntryPosition(pos); // RW 0x87CA64: slot 0x158
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai && ai->curLocomotor() && ai->world().mapReady() && !OpenContain::validMovementTerrain(*ai, pos))
	{
		++m_exitStats.exitCellRetries;
		const float r = CombatQueries::boundingCircleRadius(*container); // RW 0x87CACC: + 0xB8
		double s, c;
		SimMath::sinCosDet((double)angle, s, c); // RW 0x42F4E0 / 0x42F4D0 (x87 fcos / fsin, S-167)
		pos.x = SimMath::fstpDword(SimMath::pc24SubW((double)pos.x, SimMath::pc24MulW(c, (double)r))); // fmul r; fsubr x; fstp
		pos.y = SimMath::fstpDword(SimMath::pc24SubW((double)pos.y, SimMath::pc24MulW(s, (double)r)));
		if (!OpenContain::validMovementTerrain(*ai, pos))
		{
			const double cr = SimMath::pc24MulW(c, (double)r), sr = SimMath::pc24MulW(s, (double)r);
			pos.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(cr, cr), (double)pos.x)); // fmul r; fadd st0, st0; fadd x; fstp
			pos.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(sr, sr), (double)pos.y));
			if (!OpenContain::validMovementTerrain(*ai, pos))
			{
				pos = *container->getPosition(); // RW 0x87CB5E: + 0x38
				++m_exitStats.exitAtContainerPosition;
			}
		}
	}
	obj->setPosition(&pos);   // RW 0x87CB79: RW 0x696E63(pos, 1)
	obj->setOrientation(angle); // RW 0x87CB88
	if (AIWorld *world = obj->logic().aiWorld())
	{
		world->addObjectToPathfindMap(*obj); // RW 0x87CB98: RW 0x6E85E9
	}
	static const CombatNames::Status &st = CombatNames::statuses();
	const bool heldMember = obj->testStatus((unsigned)st.hordeMember) && obj->getContainedBy(); // RW 0x6939DF
	if (ai && !heldMember)
	{
		Coord3D exitPos;
		getExitOffset(exitPos); // RW 0x87CBB9: slot 0x160
		if (ai->world().mapReady())
		{
			ai->world().pathfinder().adjustToPossibleDestination(ai->adapter(), ai->locomotorInfo(), &exitPos); // RW 0x87CBF2: RW 0x6F3C87(obj, AI + 0x1CC, &pos)
		}
		std::vector<Coord3D> path;
		path.push_back(exitPos); // RW 0x87CC0E
		// RW 0x87CC13: exit slot 0x20, the contain's rally point: not stored by the port's contains (S-1620), the second waypoint is the ExitOffset again
		HordeContainInterface *horde = nullptr;
		static const int hordeKind = CombatNames::kindOf("HORDE");
		if (obj->isKindOf((unsigned)hordeKind))
		{
			horde = hordeOf(*obj); // RW 0x694BF8 -> RW 0x693A1A(0): the object itself
		}
		else if (Object *c = obj->getContainedBy())
		{
			horde = c->isKindOf((unsigned)hordeKind) ? hordeOf(*c) : nullptr; // its horde (RW 0x693A1A: + 0x27C with the HORDE KindOf)
		}
		path.push_back(exitPos); // RW 0x87CC57
		if (horde)
		{
			horde->recentreOnMembers(obj); // RW 0x87CC72: slot 0x98(obj)
			horde->returnToFormation(false); // RW 0x87CC7F: slot 0x10(0)
			++m_exitStats.hordeRegroups;
		}
		ai->aiFollowPath(path, container, CMD_FROM_AI, true); // RW 0x87CC8F: RW 0x87C9B8(path, container, 2), AI command 10
		++m_exitStats.exitPaths;
	}
	recalcApparentControllingPlayer(); // RW 0x87CCB7: slot 0x50
}

// ---------------------------------------------------------------------------------------------------------------------------------
void GarrisonContain::crc(StateHasher &h) const
{
	OpenContain::crc(h);
	h.addBool(m_rosterCreated);
	h.addBool(m_hiddenFlag);
	for (const GarrisonPoint &p : m_points)
	{
		h.addU32(p.object);
		h.addU32(p.target);
		h.addU32(p.placedFrame);
	}
	h.addI32(m_pointsInUse);
	for (int c = 0; c < GARRISON_POINT_CONDITIONS; ++c)
	{
		h.addI32(m_pointCount[c]);
		for (int i = 0; i < m_pointCount[c]; ++i)
		{
			h.addFloat(m_pointPositions[(size_t)c][(size_t)i].x);
			h.addFloat(m_pointPositions[(size_t)c][(size_t)i].y);
			h.addFloat(m_pointPositions[(size_t)c][(size_t)i].z);
		}
	}
	h.addBool(m_pointsLoaded);
	h.addU32(m_nextExitFrame);
}
