// OpenBFME. GPL-3.0.
//
// HordeContain's side of a garrison (lane GARRISON-1): the HordeContainInterface slots RotWK's HordeGarrisonContain and the AI enter / exit states call. See the
// declarations in GameLogic/Object/Contain/HordeContainRuntime.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the HordeContainInterface vtable RW 0xC5B1F8 at module + 0x11C):
//   * slot 0x18 RW 0x86EE1B: the member's container is this horde, or its id is in the map at interface + 0x54 (the members on the way);
//   * slot 0xA8 RW 0x86EBF4: the id joins the map, the member leaves the contain list (OpenContain's removal, slot 0xA4(member, false));
//   * slot 0x20 RW 0x872A7F -> RW 0x8757FC: the id leaves the map (RW 0x8720C2), the member is added back (slot 0x9C), the horde object's upgrades reach it (slot 0x174);
//   * slots 0xF4 RW 0x86D755 (the map is empty), 0x100 / 0x104 RW 0x872AA1 / 0x872AAE (interface + 0x1EC), 0x124 / 0x128 / 0x12C RW 0x872ADF / 0x872B76 / 0x872AE7
//     (the garrisoned byte + 0x188; clearing it wakes the update next frame, RW 0x850C32(horde, 1));
//   * slot 0x7C RW 0x875C93: every member released (slot 0xA8), the released members as an AIGroup sent to the container's position (RW 0x774897 with the container as
//     the goal object);
//   * slot 0x80 RW 0x875DC3: the container accepts the horde (contain slot 0x98(horde, 1, 0)) and the horde has an AI: the horde's AI command 0x3D (RW 0x87366E,
//     AIHordeEnterState), then every member still in the list is released and the released members get the AI command 0x17 (enter the object, RW 0x7724BE);
//   * slot 0xFC RW 0x876B25 (each frame of AIHordeEnterState): every released member (map order) near the container (2D distance below its locomotor's
//     CloseEnoughDist, RW 0x86C02C) is added (slot 0x9C); else, while no member entered yet, or GameData MaxNumMembersToForceToImmediatelyEnter (+ 0x1230) members were
//     already forced this frame, or the last entry is at most WaitToForceMemberToEnterDelay (+ 0x1234) frames old, an idle member is ordered in again (AI command 0x17,
//     source 2); otherwise it is forced in (slot 0x9C);
//   * slot 0x84 RW 0x875EFD: the horde's AI command 0x3E (AIHordeExitState).
// INFERENCE (stop S-1102): a member on the way whose object is gone leaves the map (retail keeps the id: a dead member would hold the horde in AIHordeEnterState);
// while garrisoned the member pass does not run (the garrisoned byte's reader is not located: the members stand on the garrison points).
// Lane GARRISON-3 (the owner's feedback G6; TARGET FACTS, caveat S-001):
//   * slot 0xA8 RW 0x86EBF4 removes the member through OpenContain's removal (RW 0x8665BE / 0x865EB6), whose HordeContain::onRemoving (contain slot 0x5C,
//     RW 0x86CF2A) frees no slot: the member keeps its memberIndex entry (H+0x17C) on the way; HordeContain::onContaining (contain slot 0x58, RW 0x872D0A)
//     assigns and places only a member memberIndex does not hold (RW 0x872D87), so a member coming back rejoins where it stands;
//   * slot 0x98 RW 0x86F18F (recentreOnMembers) and slot 0x10 RW 0x8759FF (returnToFormation) are what GarrisonContain's exit RW 0x87CA2B calls for the horde
//     object (the reader of the map after an exit, located): see the functions.
// INFERENCE (stop S-1620): a member on the way whose object is gone or dead gives its kept slot back (the caller of RW 0x87370B, HordeContainInterface slot
// 0x34, for such a member is not located); the port keeps HordeContainCore's registered set (H+0x170, RW 0x873F30) apart from the map of the members on the
// way, which RW holds in the same set (interface + 0x54 = H+0x170): returnToFormation walks the members on the way only.

#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"

#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

bool HordeContain::isMemberOrEntering(const Object &member) const
{
	return member.getContainedBy() == getObject() || m_garrisonEntering.count(member.getID()) != 0;
}

void HordeContain::releaseMemberForGarrison(Object *member)
{
	if (!member)
	{
		return;
	}
	m_garrisonEntering.insert(member->getID());
	removeMember(member, true); // contain slot 0xA4(member, false): the member keeps its slot (RW 0x86CF2A)
}

void HordeContain::forgetMemberOnTheWay(ObjectID id)
{
	m_garrisonEntering.erase(id);
	if (m_core->slotOf(id) >= 0)
	{
		// S-1620 inference: RW 0x87370B's release of the slot (no member template flag: the object may be gone)
		m_core->removeMember(id, false);
		m_dirty = true;
		++m_exitStats.slotsFreedOnTheWay;
	}
	if (m_members.empty() && m_garrisonEntering.empty() && m_payloadCreated && !m_deleting && !getObject()->isDestroyed())
	{
		getObject()->logic().destroyObject(getObject()); // the "dies with its last member" rule of removeFromContain
	}
}

void HordeContain::acceptMemberFromGarrison(Object *member)
{
	if (!member)
	{
		return;
	}
	m_garrisonEntering.erase(member->getID());
	addToContain(member);
	// slot 0x174 (RW 0x87566B): the horde object's upgrades reach the member
	Object *horde = getObject();
	if (member->getContainedBy() == horde)
	{
		member->friend_orUpgradeMask(horde->getUpgradeMask());
		member->updateUpgradeModules();
	}
}

void HordeContain::setGarrisoned(bool on)
{
	m_garrisoned = on;
	if (!on)
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x872B8B: the update next frame
		m_dirty = true;
	}
}

void HordeContain::releaseMembersToward(Object *container, int source)
{
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		releaseMemberForGarrison(m);
	}
	if (!container)
	{
		return;
	}
	std::vector<ObjectID> ids(m_garrisonEntering.begin(), m_garrisonEntering.end());
	AIGroup group(getObject()->logic(), ids);
	group.groupMoveToPosition(*container->getPosition(), false, (CommandSourceType)source);
}

void HordeContain::enterContainer(Object *container, int source)
{
	Object *horde = getObject();
	if (!container || !container->getContain() || !container->getContain()->isValidContainerFor(*horde, true, false))
	{
		return;
	}
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	ai->aiHordeEnter(container, (CommandSourceType)source); // RW 0x87366E: AI command 0x3D
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		releaseMemberForGarrison(m);
	}
	const std::vector<ObjectID> entering(m_garrisonEntering.begin(), m_garrisonEntering.end());
	GameLogic &logic = horde->logic();
	for (ObjectID id : entering)
	{
		Object *m = logic.findObjectByID(id);
		if (m && m->getAIUpdateInterface())
		{
			m->getAIUpdateInterface()->aiEnterObject(container, (CommandSourceType)source); // RW 0x7724BE -> RW 0x66C5A4
		}
	}
}

void HordeContain::pushMembersIntoContainer(Object *container)
{
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	unsigned forceLeft = logic.settings().maxNumMembersToForceToImmediatelyEnter; // RW 0x876B2E: GameData + 0x1230
	const std::vector<ObjectID> entering(m_garrisonEntering.begin(), m_garrisonEntering.end());
	for (ObjectID id : entering)
	{
		Object *m = logic.findObjectByID(id);
		if (!m || m->isEffectivelyDead())
		{
			forgetMemberOnTheWay(id); // S-1102 / S-1620 inference
			continue;
		}
		AIUpdateInterface *mai = m->getAIUpdateInterface();
		bool near = false;
		if (mai && mai->curLocomotor())
		{
			// RW 0x86C02C: SSE, (dy * dy) + (dx * dx) < CloseEnoughDist^2 (locomotor + 0x3C)
			const float dx = SimMath::subf32(m->getPosition()->x, container->getPosition()->x);
			const float dy = SimMath::subf32(m->getPosition()->y, container->getPosition()->y);
			const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
			const float r = mai->closeEnoughDist();
			near = d < SimMath::mulf32(r, r);
		}
		ContainModuleInterface *contain = container->getContain();
		if (near && contain)
		{
			contain->addToContain(m);
			continue;
		}
		const unsigned last = m_lastMemberEnteredFrame;
		if (last == 0 || forceLeft == 0 || logic.getFrame() - last <= logic.settings().waitToForceMemberToEnterDelay || !contain)
		{
			if (mai && mai->isIdle())
			{
				mai->aiEnterObject(container, CMD_FROM_AI); // RW 0x876BED: AI command 0x17 with source 2
			}
		}
		else
		{
			contain->addToContain(m);
			--forceLeft;
		}
	}
}

void HordeContain::exitContainer(Object *container, int source)
{
	if (AIUpdateInterface *ai = getObject()->getAIUpdateInterface())
	{
		ai->aiHordeExit(container, (CommandSourceType)source); // RW 0x775AFB: AI command 0x3E
	}
}

// RW 0x86F18F (HordeContainInterface slot 0x98). TARGET: the positions of the members on the way (the map at interface + 0x54, id order) that are not disabled
// with type 4 (RW 0x6916BF: object + 0x1C8 bit 4 clear), then of the members in the contain list (slot 0x118), are summed (SSE, position + sum for the map,
// sum + position for the list; commutative), the centroid is the sum times 1 / count (RW 0xBD1908 = 1.0 divided by the count's cvtsi2ss); the best distance
// starts at RW 0xBD19DC = -1.0 and the best position at `about`'s. The members on the way are candidates as they stand; a list member only on a valid
// movement position for `about`'s locomotor set (RW 0x6EA4E7 with `about`'s AI + 0x1DC and layer); distance (dz * dz + dy * dy) + dx * dx, a candidate wins
// while the best is negative or strictly greater. `about` is moved there (RW 0x70C201) and takes the winner's layer (RW 0x68BB9D: every 2.01 map has the ground
// layer only, the port's objects carry none). The same function as HordeAIUpdate::recentreOnMembers (the melee caller, which has no members on the way).
void HordeContain::recentreOnMembers(Object *about)
{
	if (!about)
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	const auto notDisabled4 = [](const Object &o) { return (o.getDisabledMask() & (1u << 4)) == 0u; };
	std::vector<Object *> onTheWay;
	for (ObjectID id : m_garrisonEntering)
	{
		Object *m = logic.findObjectByID(id);
		if (m && notDisabled4(*m))
		{
			onTheWay.push_back(m);
		}
	}
	float sx = 0.0f, sy = 0.0f, sz = 0.0f;
	int count = 0;
	for (Object *m : onTheWay)
	{
		sx = SimMath::addf32(m->getPosition()->x, sx);
		sy = SimMath::addf32(m->getPosition()->y, sy);
		sz = SimMath::addf32(m->getPosition()->z, sz);
		++count;
	}
	const std::vector<Object *> listed(m_members.begin(), m_members.end());
	for (Object *m : listed)
	{
		sx = SimMath::addf32(sx, m->getPosition()->x);
		sy = SimMath::addf32(sy, m->getPosition()->y);
		sz = SimMath::addf32(sz, m->getPosition()->z);
		++count;
	}
	if (count <= 0)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, SimMath::sseFromInt32(count));
	const float cx = SimMath::mulf32(inv, sx), cy = SimMath::mulf32(sy, inv), cz = SimMath::mulf32(sz, inv);
	float best = -1.0f; // RW 0xBD19DC
	Coord3D pos = *about->getPosition();
	const auto consider = [&](const Coord3D &p) {
		const float dx = SimMath::subf32(p.x, cx), dy = SimMath::subf32(p.y, cy), dz = SimMath::subf32(p.z, cz);
		const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		if (best < 0.0f || best > d)
		{
			best = d;
			pos = p;
		}
	};
	for (Object *m : onTheWay)
	{
		consider(*m->getPosition());
	}
	AIUpdateInterface *ai = about->getAIUpdateInterface();
	for (Object *m : listed)
	{
		Coord3D p = *m->getPosition();
		if (!ai || !ai->world().mapReady())
		{
			continue; // RW reads `about`'s AI (+ 0x260) unconditionally; the horde objects of a game have one, a map-less fixture has no pathfinder to ask
		}
		if (!ai->world().pathfinder().validMovementPosition(&ai->adapter(), ai->locomotorInfo(), ai->adapter().getLayer(), &p))
		{
			continue;
		}
		consider(*m->getPosition());
	}
	// RW 0x70C201: only `about` moves; a horde's contain would carry its members along unless it is told the move is the horde's own (HordeAIUpdate's rule)
	HordeContainInterface *hci = about->getContain() ? about->getContain()->getHordeContainInterface() : nullptr;
	if (hci)
	{
		hci->setLocomoting(true);
	}
	about->setPosition(&pos);
	if (hci)
	{
		hci->setLocomoting(false);
	}
}

namespace
{
// RW 0x86D061: the model conditions a member takes from its horde object (the 0x4C-byte mask built at RW 0x86D086 .. 0x86D0EB; the names of the binary's
// table at those bit numbers)
const Object::ModelConditionBits &hordeSyncedConditions()
{
	static const Object::ModelConditionBits mask = [] {
		Object::ModelConditionBits m{};
		for (const char *name : { "ATTACKING", "EMOTION_ALERT", "EMOTION_AFRAID", "EMOTION_TERROR", "RAISING_FLAG", "SELECTED", "EMOTION_LOOK_TO_SKY",
				 "EMOTION_CELEBRATING", "EMOTION_MORALE_HIGH", "EMOTION_MORALE_LOW", "EMOTION_COWER", "WADING", "SWIMMING", "SPECIAL_WEAPON_ONE",
				 "SPECIAL_WEAPON_TWO", "SPECIAL_WEAPON_THREE", "CHANT_FOR_GROND", "UNCONTROLLABLE", "EMOTION_TAUNTING", "EMOTION_UNCONTROLLABLY_AFRAID",
				 "SPECIAL_WEAPON_FOUR", "SPECIAL_WEAPON_FIVE", "SPECIAL_WEAPON_SIX" })
		{
			const int bit = CombatNames::modelCondition(name);
			m[(size_t)bit >> 5] |= 1u << (bit & 31);
		}
		return m;
	}();
	return mask;
}
} // namespace

// RW 0x8759FF (HordeContainInterface slot 0x10). TARGET: `regroup` is dropped unless the horde stands on the ground layer (RW 0x68BBE0 == 1: always here);
// nothing without the horde's AI (+ 0x260). Then at most 100 steps (RW 0x875A3B: the counter is decremented first, the loop ends below 0) over the FIRST
// entry of the map of the members on the way: a gone object leaves the map (RW 0x8720C2); a member not disabled with type 4 (RW 0x6916BF) rejoins (module
// slot 0x98, RW 0x8757FC: it leaves the map, OpenContain's add, the horde's upgrades); with the horde UNDER_CONSTRUCTION (status 2, RW 0x44DDEC: the production
// exit) and the horde's template not CAVALRY (KindOf byte + 0x109 bit 1) the member is made busy (AI command 0x31, RW 0x852E2A(0, 2)) and walks back when it is more than 10 (RW
// 0xBD83D8) from the horde (RW 0x665777): not ported (S-1620, counted; a garrison's horde is never under construction); a member not on the ground layer drops
// `regroup`. A disabled member stays first in the map and uses the steps up. When a member rejoined: the fill-in loop (module slot 0x94, RW 0x873D48), the
// re-form (slot 0x40, RW 0x877E12), every member takes the horde's synced model conditions (RW 0x86DC38 -> 0x86D061, clearAndSet RW 0x68D607); `regroup`:
// slot 0xCC (RW 0x86F053: every member, listed then on the way, not IS_MELEE_ATTACKING, leaves the pathfind map, RW 0x6E85FB) and the horde's position
// adjusted for its locomotor set (RW 0x6FE456, no group destination), applied when it moved more than 10 and less than 150 (RW 0xC041F8) (a horde without
// AI returns here, RW 0x875C03). Last, the interface bytes + 4 / + 5 (the port's dirty flag) take "a member rejoined".
void HordeContain::returnToFormation(bool regroup)
{
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	static const int underConstruction = CombatNames::status("UNDER_CONSTRUCTION");
	bool any = false;
	int guard = 100;
	while (!m_garrisonEntering.empty())
	{
		--guard;
		if (guard < 0)
		{
			++m_exitStats.stuckDisabledMembers;
			break;
		}
		const ObjectID id = *m_garrisonEntering.begin();
		Object *m = logic.findObjectByID(id);
		if (!m)
		{
			forgetMemberOnTheWay(id); // RW 0x8720C2 (the slot: S-1620 inference)
			continue;
		}
		if ((m->getDisabledMask() & (1u << 4)) != 0u)
		{
			continue;
		}
		acceptMemberFromGarrison(m); // module slot 0x98 (RW 0x8757FC)
		if (horde->testStatus((unsigned)underConstruction) && m->getAIUpdateInterface())
		{
			++m_exitStats.underConstructionUnported; // RW 0x875AAF .. 0x875B74 (S-1620)
		}
		any = true;
	}
	if (any)
	{
		++m_exitStats.returnsToFormation;
		while (m_core->fillInLowestFreeSlot()) // module slot 0x94 (RW 0x873D48)
		{
		}
		endReform(); // slot 0x40 (RW 0x877E12)
		const Object::ModelConditionBits &mask = hordeSyncedConditions();
		const Object::ModelConditionBits &own = horde->getModelConditionBits();
		const std::vector<Object *> members(m_members.begin(), m_members.end()); // RW 0x865598: the contain list copied
		for (Object *m : members)
		{
			Object::ModelConditionBits set{}, clear{};
			for (size_t w = 0; w < set.size(); ++w)
			{
				set[w] = mask[w] & own[w];
				clear[w] = ~own[w] & mask[w];
			}
			m->clearAndSetModelConditionFlags(clear, set); // RW 0x68D607
		}
		if (regroup)
		{
			static const int meleeAttacking = CombatNames::status("IS_MELEE_ATTACKING");
			AIWorld &world = ai->world();
			// slot 0xCC (RW 0x86F053)
			for (Object *m : members)
			{
				if (!m->testStatus((unsigned)meleeAttacking))
				{
					world.removeObjectFromPathfindMap(*m);
				}
			}
			const std::vector<ObjectID> onTheWay(m_garrisonEntering.begin(), m_garrisonEntering.end());
			for (ObjectID id : onTheWay)
			{
				Object *m = logic.findObjectByID(id);
				if (m && !m->testStatus((unsigned)meleeAttacking))
				{
					world.removeObjectFromPathfindMap(*m);
				}
			}
			if (world.mapReady())
			{
				Coord3D p = *horde->getPosition();
				world.pathfinder().adjustDestination(ai->adapter(), ai->locomotorInfo(), &p, nullptr); // RW 0x6FE456(horde, + 0x1CC, &p, 0)
				const Coord3D &h = *horde->getPosition();
				const float d = SimMath::fstpDword(SimMath::length3d(SimMath::subf32(h.x, p.x), SimMath::subf32(h.y, p.y), SimMath::subf32(h.z, p.z))); // RW 0x403111, fstp dword
				if (d > 10.0f && d < 150.0f)
				{
					setLocomoting(true); // RW 0x70C201 moves the horde object only
					horde->setPosition(&p);
					setLocomoting(false);
				}
			}
		}
	}
	m_dirty = any; // RW 0x875C86: interface + 4 / + 5
}
