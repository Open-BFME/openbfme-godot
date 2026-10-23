// OpenBFME. GPL-3.0.
// See GameLogic/Object/Contain/HordeContainRuntime.h.

#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/SimMath.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <list>
#include <cmath>
#include <stdexcept>
#include <string>

namespace
{
int hordeMemberStatusBit()
{
	static const int bit = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
	return bit;
}
} // namespace

HordeContain::HordeContain(Thing *thing, const HordeContainBehaviorData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (!data)
	{
		throw std::logic_error("HordeContain: the module data is not typed (HordeContainBehaviorData)");
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	ThingFactory &things = logic.things();
	m_core = std::make_unique<HordeContainCore>(data->horde, logic.random(), [&things](const std::string &slotUnitType, const std::string &memberName) {
		return MapHordeSpawn::unitTypeMatches(things, slotUnitType, memberName);
	});
	// MOVE-1: a game with an AIWorld runs the contain (member pass, reformation, the TransportContain model condition mirror: RW 0x872EFC); a logic without one
	// (the LOGIC-1 fixtures) keeps the static horde: the module sleeps and the members stay where creation put them (S-149)
	m_movementEnabled = logic.aiWorld() != nullptr;
	if (!m_movementEnabled)
	{
		friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
	}
	else
	{
		logic.aiWorld()->noteStop(HordeContain::movementStop());
	}
}

HordeContain::~HordeContain() = default;

const char *HordeContain::movementStop()
{
	return "S-224 HordeContain movement (member pass, reform, refresh) is the RotWK member order (RW 0x877A7A: ahead test, snap threshold, hub RW 0x87468B, farthest-first reform "
		"reassignment RW 0x877E12, formation always ready RW 0x9B501B) with these inferences: a member waiting for the formation (UseSlowHordeMovement) is not counted as work (B1 "
		"sets its work flag and re-runs the pass every frame), so a member left ahead of its slot by a parked horde stays there; the leash command (0, 2) is counted and does nothing "
		"(its identity is unknown); the frozen-formation and engaged-target branches (melee), the flank history, the banner carriers, the layer sync and the every-fifth-frame "
		"pair of HordeContain::update are not ported; the TRANSPORT_MOVING mirror is BFME's TransportContain::update; MACHINE members ride on the horde object; "
		"PRODUCER FOOTPRINT (inference, no retail evidence: RW 0x6F1B3E / 0x6F74D0 show no producer exemption): EVERY far-arm member walk order (the first exit, every later "
		"walk of a displaced or re-formed member) sets the member mover's ignored obstacle to the horde's producer object (none for a horde without a producer), so the "
		"explicit-goal validity check ignores the factory's footprint for that member until the mover clears the ignore (its ignore timer), whatever the member's position";
}

void HordeContain::registerClasses(ModuleFactory &modules)
{
	for (const char *name : { "HordeContain", "HorseHordeContain" })
	{
		modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			return std::make_unique<HordeContain>(thing, dynamic_cast<const HordeContainBehaviorData *>(data));
		});
	}
}

// the slot table is built when the module exists and its object is complete (RW 0x877751, from the contain's creation)
void HordeContain::onObjectCreated()
{
	m_core->buildSlots(true);
}

// HordeContain::update (B1 FormationRefresh0023FA80.cpp:119-176, RW 0x00872EFC - 0x00873179), the movement part: TransportContain::update (the model condition
// mirror), the dirty flag while the horde object moves, the member pass when dirty, the pathfinder goal of a parked horde. Not ported (S-149 / S-222): the flank
// history ring buffer, the banner carrier check, the layer sync, the every-5th-frame `slot070` / `periodic` pair, the melee tick.
const HordeContainModuleData &HordeContain::hordeData() const
{
	return m_data->horde;
}

void HordeContain::setMeleeEngaged(bool on)
{
	if (m_meleeEngaged == on)
	{
		return;
	}
	m_meleeEngaged = on;
	if (!on)
	{
		m_dirty = true; // endMelee: the survivors walk back to their slots
	}
}

UpdateSleepTime HordeContain::update()
{
	if (!m_movementEnabled)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *horde = getObject();
	AIUpdateInterface *ai = horde->getAIUpdateInterface();
	recordFlankHistory();    // HORDE-2: RW 0x872F05, the first thing the update does
	mirrorMovingCondition(); // TransportContain::update (RW 0x86B86D)
	bannerCheck(false);      // HORDE-2: RW 0x8730A5 (after the melee tick and the parked-goal update; inference: before them here)
	if (m_meleeEngaged || (ai && ai->isMoving()))
	{
		// lane MODULES-3: RW 0x872FED .. 0x873002 -> 0x873155: a moving or melee-engaged horde drops its back-up records and scarer (and is dirty)
		m_backUp.clear();
		m_scarer = INVALID_ID;
		m_dirty = true;
	}
	if (m_meleeEngaged)
	{
		// lane MOVE-2 r3 (S-1502), RW 0x872FD3 .. 0x872FDD -> 0x870A1B: the melee behaviour's update runs here, before the member pass; the pass then runs every
		// frame (dirty, RW 0x873155) and moves the members to their melee destinations (slot 7 RW 0x877D89). COMBAT-1 froze the formation instead.
		if (HordeAIUpdate *hai = dynamic_cast<HordeAIUpdate *>(ai))
		{
			hai->containMeleeUpdate();
		}
	}
	if (!ai)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_garrisoned)
	{
		return UPDATE_SLEEP_NONE; // lane GARRISON-1 (S-1102): the members stand on the garrison points, no member pass
	}
	if (ai->isMoving())
	{
		m_dirty = true; // B1 step 4: a moving horde keeps its members' slots fresh every update
	}
	if (m_dirty || !m_backUp.empty() || m_quarrelB != INVALID_ID) // lane MODULES-3: RW 0x8730CD .. 0x8730E4 (the records and a quarrel keep the pass running)
	{
		m_dirty = false;
		if (m_quarrelB != INVALID_ID && m_quarrelA == INVALID_ID)
		{
			m_quarrelB = INVALID_ID; // RW 0x8730F7 .. 0x873107: the quarrel ended
			m_quarrelDistance.clear();
		}
		const bool work = runMemberPass();
		if (work)
		{
			m_dirty = true; // members still walk, turn or wait: the pass runs again next frame
		}
		else if (!ai->isMoving())
		{
			// B1 line 0x898: a parked horde's goal in the pathfinder is the cell it stands on
			AIWorld &w = ai->world();
			if (w.mapReady())
			{
				w.pathfinder().updateGoal(ai->adapter(), horde->getPosition(), ai->adapter().getLayer());
			}
		}
	}
	return UPDATE_SLEEP_NONE;
}

// the HORDE mover's reform (RW contain vfuncs +0x3C .. +0x40 around the instant turn). RW 0x63F3BF, the base slot +0x3C, is a lone `ret`: beginning a reform does
// nothing in HordeContain. The end (RW 0x877E12) REASSIGNS the members to the slots of the rotated formation: the member whose nearest compatible slot is the
// farthest away takes that slot first, until every member has one (a farthest-first greedy match); the rank fill-in loop follows and the pass is marked dirty.
// (The horde spec says there is no separate slot reassignment step: the RotWK binary has one.)
void HordeContain::beginReform()
{
}

void HordeContain::reassignMembersToNearestSlots()
{
	Object *horde = getObject();
	HordeContainCore::Placement owner;
	owner.position = *horde->getPosition();
	owner.angle = horde->getOrientation();
	owner.computeTrig(); // lane UBER-2B: once for every member below
	const int count = (int)m_core->slots().size();
	std::vector<Coord3D> slotPos((size_t)count);
	for (int i = 0; i < count; ++i)
	{
		slotPos[(size_t)i] = m_core->getSlotWorldPosByIndex(i, owner);
	}
	std::vector<Object *> pending;
	std::list<int> slots;
	for (Object *m : m_members)
	{
		const int idx = m_core->slotOf(m->getID()); // RW reads the map's operator[] (an absent member reads slot 0): a member without a slot is skipped here (S-222)
		if (idx < 0 || idx >= count)
		{
			continue;
		}
		slots.push_back(idx);
		pending.push_back(m);
	}
	while (!pending.empty())
	{
		float globalBest = -1.0f; // RW 0xBD19DC
		size_t chosenMember = 0;
		std::list<int>::iterator chosenNode = slots.end();
		for (size_t i = 0; i < pending.size(); ++i)
		{
			float best = 10000000000.0f; // RW 0xBF7328
			std::list<int>::iterator bestNode = slots.end();
			const Coord3D &mp = *pending[i]->getPosition();
			for (std::list<int>::iterator it = slots.begin(); it != slots.end(); ++it)
			{
				if (!m_core->slotAccepts(*it, pending[i]->getTemplate()->getName()))
				{
					continue;
				}
				const float dx = SimMath::subf32(mp.x, slotPos[(size_t)*it].x);
				const float dy = SimMath::subf32(mp.y, slotPos[(size_t)*it].y);
				const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)); // RW 0x877FE3: dy * dy + dx * dx
				if (best > d)
				{
					bestNode = it;
					best = d;
				}
			}
			if (bestNode == slots.end())
			{
				bestNode = slots.begin(); // RW 0x878008: no compatible slot: the first one, at distance 0
				best = 0.0f;
			}
			if (best > globalBest)
			{
				globalBest = best;
				chosenMember = i;
				chosenNode = bestNode;
			}
		}
		m_core->setMemberSlot(pending[chosenMember]->getID(), *chosenNode);
		slots.erase(chosenNode);
		pending[chosenMember] = pending.back(); // RW 0x878085: the last member takes the vacated place
		pending.pop_back();
	}
	while (m_core->fillInLowestFreeSlot())
	{
	}
}

void HordeContain::endReform()
{
	reassignMembersToNearestSlots();
	m_dirty = true; // RW 0x8780B0: byte [this+5] = 1
}

// RW HordeContain vtable +0x1C0 is RW 0x9B501B, `mov al, 1; ret 4`: the formation is always ready for a plain horde (the x0.1 wait of the HORDE mover applies to the
// containers that override it)
bool HordeContain::isFormationReady(float) const
{
	return true;
}

void HordeContain::updateFormation()
{
	m_dirty = true;
	m_formationRefreshFrame = getObject()->logic().getFrame();
}



void HordeContain::onCreate()
{
	createPayload();
}

// B1 HordeContainCreatePayload.cpp (0x0023C000) with TransportContain::createPayload (B1 TransportContain.cpp, 0x0022D2A0) under it
void HordeContain::createPayload()
{
	if (m_payloadCreated)
	{
		return;
	}
	// B1 HordeContainCreatePayload.cpp (retail 0x0023C000): `if (!getObject()->getProducerID())` guards the WHOLE payload creation: an object a factory
	// produces makes no payload; its members are produced one by one from the same queue entry (ProductionUpdate, lane PROD-1). The producer is set
	// before the creation modules run through GameLogic::setPendingProducer (retail sets it after newObject and runs createPayload later: INFERENCE).
	if (getObject()->getProducerID() != INVALID_ID)
	{
		return;
	}
	m_payloadCreated = true;
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	ThingFactory &things = logic.things();
	const TransportContainModuleData &transport = m_data->horde.m_transport;
	ObjectStatusMaskType status{};
	for (const InitialPayloadEntry &e : transport.m_initialPayload)
	{
		if (e.count <= 0)
		{
			logic.reportError("HordeContain of " + horde->getTemplate()->getName() + ": InitialPayload " + e.name + " has a count of " + std::to_string(e.count));
			continue;
		}
		const ThingTemplate *tt = things.findTemplate(e.name);
		if (!tt)
		{
			logic.reportError("HordeContain of " + horde->getTemplate()->getName() + ": InitialPayload template '" + e.name + "' does not exist");
			continue;
		}
		for (int i = 0; i < e.count; ++i)
		{
			Object *member = things.newObject(logic, tt, horde->getTeam(), status);
			if (member)
			{
				addToContain(member);
			}
		}
	}
	// RW 0x871C10 .. 0x871CB4 (lane DECOMP-1; BFME2 decomp HordeContainRva0046E8EE.cpp, tier B same-shape): an unproduced horde destroys (int)((100 - H) * 0.01 *
	// count) of its members that have a body, H being HordeContain + 0x2A8. Its only writer is the constructor (RW 0x872972: 100; a scan of every
	// `mov [reg + 0x2A8]` in RotWK's .text finds no other HordeContain one), so the count is 0 and nothing is destroyed whatever DamagePercentToUnits says
	applyFormationModifiers(); // lane COMBAT-3: RW 0x871D2C, the end of the payload (an unproduced horde only): interface slot 0x1E4
}

// RW 0x86C713 (lane DECOMP-3): the horde's container (+ 0x27C) with a contain (+ 0x258) answers through its slot 0xB4, else false
bool HordeContain::isPassengerAllowedToFire() const
{
	const Object *outer = getObject()->getContainedBy();
	const ContainModuleInterface *c = outer ? outer->getContain() : nullptr;
	return c && c->isPassengerAllowedToFire();
}

bool HordeContain::addToContain(Object *obj)
{
	if (std::find(m_members.begin(), m_members.end(), obj) != m_members.end())
	{
		return false;
	}
	m_members.push_back(obj);
	obj->friend_setContainedBy(getObject());
	// RW 0x872D0A: onContaining records the horde as the member's producer.
	obj->setProducer(getObject());
	const int bit = hordeMemberStatusBit();
	if (bit >= 0)
	{
		obj->setStatus((unsigned)bit, true);
	}
	// lane GARRISON-3: HordeContain::onContaining (RW 0x872D0A) gives a slot and puts the member on it only when memberIndex (H+0x17C) has no entry for it
	// (RW 0x872D87); a member that kept its slot on the way into or out of a garrison / transport rejoins where it stands (the member pass walks it)
	if (m_core->rejoinMember(obj->getID(), obj->getTemplate()->getName()))
	{
		++m_exitStats.membersRejoined;
	}
	else
	{
		m_core->addMember(obj->getID(), obj->getTemplate()->getName());
		placeMember(obj);
	}
	// ForcedLocomotorSet (spec 1.2; B1 MemberSync0023FDB0.cpp:42): a member takes the horde's forced locomotor set when it joins
	const int forced = m_data->horde.m_forcedLocomotorSet;
	if (forced != -1 && m_movementEnabled)
	{
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			if (!ai->chooseLocomotorSet(forced))
			{
				getObject()->logic().reportError("HordeContain of " + getObject()->getTemplate()->getName() + ": member " + obj->getTemplate()->getName() +
					" has no LocomotorSet " + std::string(TheLocomotorSetNames[forced]) + " (ForcedLocomotorSet)");
			}
		}
	}
	return true;
}

bool HordeContain::acceptCreatedMember(Object *obj)
{
	if (std::find(m_members.begin(), m_members.end(), obj) != m_members.end())
	{
		return false;
	}
	// lane AI-2 r4: a horde that is already destroyed (GameLogic::destroyObject ran its onDelete, which destroyed every member, see onDelete) stays findable until the
	// destroy list deletes it; the queue exit (RW 0x8A402D .. 0x8A4083) can still hand it the next produced member. Taken in, that member would outlive its horde with
	// a dangling container (the AI-vs-AI use-after-free in AIUpdateInterface::isIdle / AIWorld::adapterFor). The member goes with its horde instead, as onDelete's
	// rule for every contained object (INFERENCE: RW's acceptance slot is not read for a destroyed horde, S-894)
	if (m_deleting || getObject()->isDestroyed())
	{
		++m_membersRefusedAfterDelete;
		getObject()->logic().destroyObject(obj);
		return false;
	}
	m_members.push_back(obj);
	obj->friend_setContainedBy(getObject());
	obj->setProducer(getObject());
	const int bit = hordeMemberStatusBit();
	if (bit >= 0)
	{
		obj->setStatus((unsigned)bit, true);
	}
	m_core->addMember(obj->getID(), obj->getTemplate()->getName());
	m_producedMembers.push_back(obj->getID());
	// lane AI-2: a produced horde has its members now (createPayload made none for it): the "dies with its last member" rule of removeFromContain applies to it
	// too. Before, a produced horde whose members all died stayed alive with ImmortalBody's 1 hp, and every enemy kept it as its victim forever (AI vs AI stall)
	m_payloadCreated = true;
	return true;
}

std::string HordeContain::getPayloadMemberTemplateName() const
{
	const std::vector<InitialPayloadEntry> &payload = m_data->horde.m_transport.m_initialPayload;
	return payload.size() == 1 ? payload[0].name : std::string();
}

int HordeContain::getSlotCapacity() const
{
	return m_data->horde.m_transport.m_slotCapacity;
}

namespace
{
// body slot 0x14 (RW 0x8C1D75): health / max health in the FPU (PC24), 0 without a positive max health
double bodyRatioWide(const Object &o)
{
	const BodyModuleInterface *body = o.getBodyModule();
	if (!body)
	{
		return 0.0; // INFERENCE: retail calls the slot on every member (each has a body)
	}
	const float maxHealth = body->getMaxHealth();
	return maxHealth > 0.0f ? SimMath::pc24DivW((double)body->getHealth(), (double)maxHealth) : 0.0;
}
} // namespace

float HordeContain::averageMemberHealthRatio() const
{
	// RW 0x86EA09 (lane MOD-4)
	float sum = 0.0f;
	for (const Object *m : m_members) // RW 0x865598: the contain list
	{
		if (m)
		{
			sum = SimMath::fstpDword(SimMath::pc24AddW(bodyRatioWide(*m), (double)sum)); // fadd dword; fstp dword
		}
	}
	for (ObjectID id : m_garrisonEntering) // interface + 0x54: the map in key order, each found by id (RW 0x449681)
	{
		if (const Object *m = getObject()->logic().findObjectByID(id))
		{
			sum = SimMath::fstpDword(SimMath::pc24AddW(bodyRatioWide(*m), (double)sum));
		}
	}
	const float slots = SimMath::fstpDword(SimMath::fildU32((std::uint32_t)getSlotCapacity())); // fild; unsigned correction; fstp dword
	if (slots > 0.0f)
	{
		sum = SimMath::divf32(sum, slots); // divss
	}
	return sum;
}

Coord3D HordeContain::getMemberFormationPosition(const Object *member) const
{
	const Object *horde = getObject();
	HordeContainCore::Placement owner;
	owner.position = *horde->getPosition();
	owner.angle = horde->getOrientation();
	Coord3D pos = owner.position;
	// lane MOVE-2 r3: RW 0x877D89 asks the melee behaviour first while the horde melees (+ 0x184): its slot 0x34 (Amoeba RW 0x98F819) hands out the member's
	// melee destination as it is
	if (m_meleeEngaged)
	{
		if (const HordeAIUpdate *hai = dynamic_cast<const HordeAIUpdate *>(horde->getAIUpdateInterface()))
		{
			if (hai->meleeDestination(member->getID(), pos))
			{
				return pos;
			}
		}
	}
	if (m_core->slotOf(member->getID()) >= 0)
	{
		float slotAngle = 0.0f;
		pos = m_core->getSlotWorldPos(member->getID(), owner, &slotAngle);
	}
	pos.z = horde->logic().getGroundHeight(pos.x, pos.y);
	return pos;
}

// RW 0x86EF13 (horde interface slot 0xC8; lane MOVE-3): the pathfinder's updateGoal (RW 0x8E24D3) remembers a horde's goal without reserving cells and calls this. While
// the horde melees (interface + 0x184) nothing happens; else, when the horde has a goal (RW 0x68B411: the goal cell's centre, RW 0x8E1BD4 -> 0x6ED049), its angle
// (RW 0x68B425: angle code x pi / 6) is turned (fsincos) and every member of the contain list (module slot 0x118) whose + 0x458 bit 0 is clear (not effectively
// dead) gets its goal (RW 0x68B3BD -> 0x8E28DB) at the slot record's first coordinates (+ 4 / + 8 of record RW 0x86DEAF: the member map's index, 0 when absent)
// turned by that angle: x = gx + (c * sx - sy * s), y = gy + (sy * c + s * sx), z = gz, on the horde goal's layer (RW 0x68B43B)
void HordeContain::reserveMemberGoals()
{
	if (m_meleeEngaged)
	{
		return;
	}
	Object *horde = getObject();
	static const std::string kStopRankBox =
		"[S-1832] a horde's destination check (RW 0x6F1584: the LARGE_RECTANGLE_PATHFIND box of its geometry, 6 x 10 cells for a 30 x 50 box, centred on the goal) "
		"meets only the member goals reserved inside it (RW 0x86EF13): hordes whose ranks all stand 30 or more ahead of the horde object (ElvenLorienArcherHorde, "
		"DwarvenAxeThrowerHorde) can be sent to one point and stand on it together; not verified against a retail run";
	horde->logic().noteStop(kStopRankBox);
	AIWorld *world = horde->logic().aiWorld();
	if (world == nullptr || !world->mapReady())
	{
		return;
	}
	Pathfinder &pf = world->pathfinder();
	Coord3D goal;
	if (!pf.goalPosition(world->adapterFor(*horde), &goal))
	{
		return;
	}
	const float angle = pf.goalAngle(horde->getID());
	const PathfindLayerEnum layer = pf.goalLayer(horde->getID());
	const std::vector<HordeContainCore::Slot> &slots = m_core->slots();
	const float c = SimMath::cosf32(angle), s = SimMath::sinf32(angle);
	for (Object *m : m_members) // a member's goal reservation does not touch the contain list
	{
		if (m == nullptr || m->isEffectivelyDead())
		{
			continue;
		}
		const int idx = m_core->slotIndexOrZero(m->getID());
		if (idx < 0 || (size_t)idx >= slots.size())
		{
			continue; // a horde without slot records: RW would read past its table; the port reserves nothing
		}
		const float sx = slots[(size_t)idx].x, sy = slots[(size_t)idx].y;
		Coord3D p;
		p.x = SimMath::addf32(goal.x, SimMath::subf32(SimMath::mulf32(c, sx), SimMath::mulf32(sy, s)));
		p.y = SimMath::addf32(goal.y, SimMath::addf32(SimMath::mulf32(sy, c), SimMath::mulf32(s, sx)));
		p.z = goal.z;
		pf.updateGoal(world->adapterFor(*m), &p, layer);
	}
}

void HordeContain::removeFromContain(Object *obj)
{
	removeMember(obj, false);
}

void HordeContain::removeMember(Object *obj, bool keepSlot)
{
	auto it = std::find(m_members.begin(), m_members.end(), obj);
	if (it == m_members.end())
	{
		return;
	}
	m_members.erase(it);
	m_producedMembers.erase(std::remove(m_producedMembers.begin(), m_producedMembers.end(), obj->getID()), m_producedMembers.end());
	if (obj->getID() == m_bannerCarrier)
	{
		m_bannerCarrier = 0; // HORDE-2 (inference: the RW writer of H+0x26C on removal is not read)
	}
	obj->friend_setContainedBy(nullptr);
	if (keepSlot)
	{
		m_core->releaseMember(obj->getID()); // lane GARRISON-3: RW 0x86CF2A frees no slot, no fill-in, the dirty byte untouched
	}
	else
	{
		m_core->removeMember(obj->getID(), false);
		// RW 0x8737EE / 0x873885: remember the removed member's source at removal time, not hit time.
		if (const auto *body = dynamic_cast<const ActiveBody *>(obj->getBodyModule()))
		{
			m_removedMemberAttacker = body->lastDamager();
		}
		m_lastMemberRemovedFrame = getObject()->logic().getFrame();
		m_dirty = true; // the ranks behind a dead man close up (the member pass refills the slot)
	}
	if (m_data->horde.m_forcedLocomotorSet != -1 && m_movementEnabled)
	{
		if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
		{
			ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL); // B1 HordeContainOnRemoving.cpp:266-268
		}
	}
	// COMBAT-1: a horde whose last member is gone is destroyed (its ImmortalBody cannot die; the command points of the horde object leave with it, RW 0x69031F). Hordes that
	// were never filled (a map-less fixture) and a horde that is itself being destroyed are left alone
	Object *horde = getObject();
	// lane GARRISON-1: members on their way into or out of a garrison (released through slot 0xA8, RW 0x86EBF4) still belong to the horde
	if (m_members.empty() && m_garrisonEntering.empty() && m_payloadCreated && !m_deleting && !horde->isDestroyed())
	{
		horde->logic().destroyObject(horde);
	}
}

void HordeContain::onDelete()
{
	m_deleting = true; // the members that go now do not take the horde down a second time
	// ZH OpenContain::onDelete: destroyObject on every contained object (a copy: destroying a member removes it from this list)
	std::vector<Object *> snapshot(m_members.begin(), m_members.end());
	GameLogic &logic = getObject()->logic();
	for (Object *m : snapshot)
	{
		logic.destroyObject(m);
	}
}

int HordeContain::getMemberSlot(const Object *member) const
{
	return m_core->slotOf(member->getID());
}

unsigned HordeContain::getSlotCount() const
{
	return (unsigned)m_core->slots().size();
}

std::vector<int> HordeContain::freeSlotIndices() const
{
	return std::vector<int>(m_core->freeList().begin(), m_core->freeList().end());
}

bool HordeContain::hasRandomOffset() const
{
	return m_data->horde.m_randomOffset.x > 0.0f || m_data->horde.m_randomOffset.y > 0.0f;
}

unsigned HordeContain::unplacedMembers() const
{
	unsigned n = 0;
	for (const Object *m : m_members)
	{
		n += m_core->slotOf(m->getID()) < 0 ? 1u : 0u;
	}
	return n;
}

void HordeContain::placeMember(Object *member)
{
	Object *horde = getObject();
	HordeContainCore::Placement owner;
	owner.position = *horde->getPosition();
	owner.angle = horde->getOrientation();
	const int slot = m_core->slotOf(member->getID());
	Coord3D pos = owner.position;
	if (slot >= 0)
	{
		float slotAngle = 0.0f;
		pos = m_core->getSlotWorldPos(member->getID(), owner, &slotAngle);
	}
	pos.z = horde->logic().getGroundHeight(pos.x, pos.y); // the same ground-height rule as the object loop (MapObjectDrawables)
	member->setPosition(&pos);
	member->setOrientation(owner.angle); // inference: a member faces its horde's angle (S-149)
}

// Lane BACKLOG4 (S-813). RW 0x86D881, interface slot 0x1B8 (BFME2 decomp HordeContain::rva0046A712, tier A): slot 0x40 (the reform's end), then every
// contained member: Object RW 0x696E63(slot 7's position, 0), Thing::setOrientation(slot 7's angle). The member's slot position and angle are placeMember's
// (the ground height and the horde's angle: S-149). A garrisoned horde's members are placed by the garrison, as in containReactToTransformChange.
void HordeContain::teleportMembers()
{
	if (m_garrisoned)
	{
		return;
	}
	endReform();
	const std::vector<Object *> members(m_members.begin(), m_members.end()); // the member's aiIdle may not change the list; a copy keeps the walk defined
	for (Object *m : members)
	{
		const Coord3D pos = getMemberFormationPosition(m);
		m->teleportTo(&pos, false);
		m->setOrientation(getObject()->getOrientation());
	}
}

// RW 0x86D8FE, interface slot 0x1B4 (BFME2 decomp HordeContain::rva0046A78F, tier A): slot 0x40, then every contained member gets the transform with its
// translation replaced by slot 7's position (Object RW 0x68DA67)
void HordeContain::teleportMembersTransform(const float basis[9])
{
	if (m_garrisoned)
	{
		return;
	}
	endReform();
	const std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		const Coord3D pos = getMemberFormationPosition(m);
		m->teleportTransform(&pos, basis);
	}
}

// ZH OpenContain::containReactToTransformChange: the contents follow the container
void HordeContain::containReactToTransformChange()
{
	if (m_garrisoned)
	{
		return; // lane GARRISON-1: the garrison places the members
	}
	if (m_locomoting)
	{
		return; // MOVE-1: the horde's own locomotor moved it; its members walk to their slots (the member pass)
	}
	for (Object *m : m_members)
	{
		if (std::find(m_producedMembers.begin(), m_producedMembers.end(), m->getID()) == m_producedMembers.end())
		{
			placeMember(m);
		}
	}
}

void HordeContain::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_dirty);
	h.addBool(m_workDone);
	h.addBool(m_propagatedMoving);
	h.addBool(m_meleeEngaged); // COMBAT-1: the formation is frozen while the horde melees
	crcWallScaling(h);         // lane BACKLOG-8
	h.addU32(m_formationRefreshFrame);
	if (m_lastMemberRemovedFrame != 0xFFFFFFFFu) // lane DEVPORT-3C: ROTWK-C's removal record (RW H+0x298 / + 0x29C), hashed once written (a horde that never lost a member keeps its hash)
	{
		h.addU32(0x29800000u);
		h.addU32(m_lastMemberRemovedFrame);
		h.addU32(m_removedMemberAttacker);
	}
	h.addBool(m_payloadCreated);
	h.addU32((std::uint32_t)m_garrisonEntering.size()); // lane GARRISON-1
	for (ObjectID id : m_garrisonEntering)
	{
		h.addU32(id);
	}
	h.addU32(m_lastMemberEnteredFrame);
	h.addBool(m_garrisoned);
	h.addU64(m_membersRefusedAfterDelete);
	h.addU32((std::uint32_t)m_experiencePools.size()); // lane XP-1 (std::map: key order)
	for (const auto &kv : m_experiencePools)
	{
		h.addU32(kv.first);
		h.addFloat(kv.second);
	}
	h.addU32((std::uint32_t)m_producedMembers.size());
	for (ObjectID id : m_producedMembers)
	{
		h.addU32(id);
	}
	h.addU32((std::uint32_t)m_members.size());
	for (const Object *m : m_members)
	{
		h.addU32(m->getID());
		h.addI32(m_core->slotOf(m->getID()));
	}
	m_core->crc(h); // the complete core: slots, member <-> slot map, free list, member list, registered set, special ids, flags
	// HORDE-2 flank state (the map in key order)
	h.addU32((std::uint32_t)m_flankAngles.size());
	for (float a : m_flankAngles)
	{
		h.addFloat(a);
	}
	h.addU32(m_flankIndex);
	h.addU32((std::uint32_t)m_flankers.size());
	for (const auto &kv : m_flankers)
	{
		h.addU32(kv.first);
		h.addU32(kv.second);
	}
	// lane MODULES-3: the emotion state (maps in key order)
	h.addU32((std::uint32_t)m_backUp.size());
	for (const auto &kv : m_backUp)
	{
		h.addU32(kv.first);
		h.addFloat(kv.second.position.x);
		h.addFloat(kv.second.position.y);
		h.addFloat(kv.second.position.z);
		h.addU32(kv.second.delay);
	}
	h.addU32(m_scarer);
	h.addBool(m_cowering);
	h.addBool(m_hasFacePoint);
	h.addFloat(m_facePoint.x);
	h.addFloat(m_facePoint.y);
	h.addFloat(m_facePoint.z);
	h.addU32(m_quarrelA);
	h.addU32(m_quarrelB);
	for (std::uint32_t w : m_quarrelFighterFlags)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : m_quarrelSpectatorFlags)
	{
		h.addU32(w);
	}
	h.addU32((std::uint32_t)m_quarrelDistance.size());
	for (const auto &kv : m_quarrelDistance)
	{
		h.addU32(kv.first);
		h.addI32(kv.second);
	}
	h.addBool(m_canFlank);
	h.addU32(m_bannerCarrier);
	h.addU32(m_bannerCountdown);
	// lane INTEG-1: the stance's MeleeBehavior (slot 0x260): its presence and every field of the selected configuration (stable values, never an address)
	h.addBool(m_stanceMeleeBehavior != nullptr);
	if (const MeleeBehaviorModuleData *mb = m_stanceMeleeBehavior.get())
	{
		h.addI32((int)mb->m_kind);
		h.addBool(mb->m_followLeader);
		h.addFloat(mb->m_distanceToActiveLeader);
		h.addFloat(mb->m_distanceToPassiveLeader);
		h.addFloat(mb->m_facingBonus);
		h.addFloat(mb->m_angleLimitCos);
		h.addFloat(mb->m_innerRange);
		h.addFloat(mb->m_outerRange);
		h.addFloat(mb->m_outerRangeBuildings);
		for (std::uint32_t w : mb->m_idleModelConditions)
		{
			h.addU32(w);
		}
		h.addU32(mb->m_delayUntilIdle);
		h.addU32(mb->m_delayRandomActivateMin);
		h.addU32(mb->m_delayRandomActivateMax);
	}
}

// ---- lane SAVE-4b: the snapshot (OpenBFME native, S-4190) ---------------------------------------------------------------------------------------------------
// The members as ids in contain order (the Objects block has made them), the core, then the crc's fields and the flags the crc leaves out. The stance's
// MeleeBehavior (slot 0x260) is template data the stance selected: its configuration travels by value and a load keeps a copy (only its fields are read;
// the AI's melee runtime is the AI update's, SAVE-4c). The pass, exit and attack statistics are the report's.
void HordeContain::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	GameLogic &logic = getObject()->logic();
	SnapshotXfer::objectPointers(x, logic, m_members, "a horde's member", getObject()->getID());
	m_core->xfer(x);
	SnapshotXfer::map(x, m_experiencePools, 6, "a horde's experience pools", [&](unsigned short &k) { x.xferUnsignedShort(&k); },
		[&](float &v) { x.xferReal(&v); });
	SnapshotXfer::ids(x, m_producedMembers);
	x.xferBool(&m_payloadCreated);
	SnapshotXfer::u64(x, m_membersRefusedAfterDelete);
	x.xferBool(&m_movementEnabled);
	x.xferBool(&m_dirty);
	x.xferBool(&m_workDone);
	x.xferBool(&m_meleeEngaged);
	x.xferBool(&m_deleting);
	x.xferBool(&m_locomoting);
	x.xferBool(&m_propagatedMoving);
	x.xferUnsignedInt(&m_formationRefreshFrame);
	SnapshotXfer::reals(x, m_flankAngles);
	x.xferUnsignedInt(&m_flankIndex);
	// lane SAVE-4e (closed sets): the next angle slot once the angles are full (HordeFlank RW: m_flankAngles[m_flankIndex])
	SnapshotXfer::checkRange(m_flankIndex, 0, m_flankAngles.empty() ? 0 : (std::int64_t)m_flankAngles.size() - 1, "a horde's flank angle index");
	SnapshotXfer::map(x, m_flankers, 8, "a horde's flankers", [&](ObjectID &id) { x.xferObjectID(&id); }, [&](unsigned &f) { x.xferUnsignedInt(&f); });
	x.xferBool(&m_canFlank);
	x.xferObjectID(&m_bannerCarrier);
	x.xferUnsignedInt(&m_bannerCountdown);
	SnapshotXfer::ids(x, m_garrisonEntering);
	x.xferUnsignedInt(&m_lastMemberEnteredFrame);
	x.xferBool(&m_garrisoned);
	bool melee = !x.isLoading() && m_stanceMeleeBehavior != nullptr;
	x.xferBool(&melee);
	if (!melee)
	{
		if (x.isLoading())
		{
			m_stanceMeleeBehavior.reset();
		}
	}
	else
	{
		MeleeBehaviorModuleData::Kind kind = x.isLoading() ? MeleeBehaviorModuleData::SWARM : m_stanceMeleeBehavior->m_kind;
		SnapshotXfer::enumIn(x, kind, MeleeBehaviorModuleData::SWARM, MeleeBehaviorModuleData::AMOEBA, "a horde's melee behaviour kind");
		std::shared_ptr<MeleeBehaviorModuleData> loaded = x.isLoading() ? std::make_shared<MeleeBehaviorModuleData>(kind) : m_stanceMeleeBehavior;
		MeleeBehaviorModuleData &mb = *loaded;
		x.xferBool(&mb.m_followLeader);
		x.xferReal(&mb.m_distanceToActiveLeader);
		x.xferReal(&mb.m_distanceToPassiveLeader);
		x.xferReal(&mb.m_facingBonus);
		x.xferReal(&mb.m_angleLimitCos);
		x.xferReal(&mb.m_innerRange);
		x.xferReal(&mb.m_outerRange);
		x.xferReal(&mb.m_outerRangeBuildings);
		SnapshotXfer::maskWords(x, mb.m_idleModelConditions, SnapshotXfer::modelConditionCount(), "a horde member's idle model conditions", "the model conditions");
		x.xferUnsignedInt(&mb.m_delayUntilIdle);
		x.xferUnsignedInt(&mb.m_delayRandomActivateMin);
		x.xferUnsignedInt(&mb.m_delayRandomActivateMax);
		m_stanceMeleeBehavior = loaded;
	}
	SnapshotXfer::map(x, m_backUp, 16, "a horde's back-up records", [&](ObjectID &id) { x.xferObjectID(&id); }, [&](BackUpEntry &e) {
		SnapshotXfer::coord3D(x, e.position);
		x.xferUnsignedInt(&e.delay);
	});
	x.xferObjectID(&m_scarer);
	x.xferBool(&m_cowering);
	SnapshotXfer::coord3D(x, m_facePoint);
	x.xferBool(&m_hasFacePoint);
	x.xferObjectID(&m_quarrelA);
	x.xferObjectID(&m_quarrelB);
	SnapshotXfer::maskWords(x, m_quarrelFighterFlags, SnapshotXfer::modelConditionCount(), "a horde's quarrel fighter conditions", "the model conditions"); // lane SAVE-4e
	SnapshotXfer::maskWords(x, m_quarrelSpectatorFlags, SnapshotXfer::modelConditionCount(), "a horde's quarrel spectator conditions", "the model conditions");
	SnapshotXfer::map(x, m_quarrelDistance, 8, "a horde's quarrel distances", [&](ObjectID &id) { x.xferObjectID(&id); }, [&](int &d) { x.xferInt(&d); });
	// lane SAVE-4e (merge q20): the state the archive's lanes added after SAVE-4b, each part of crc() above: DEVPORT-3C's member removal record (RW H+0x298 /
	// + 0x29C) and BACKLOG-8's wall scaling (H + 0x2F0 / + 0x2F4 / + 0x2EC)
	x.xferUnsignedInt(&m_lastMemberRemovedFrame);
	x.xferObjectID(&m_removedMemberAttacker);
	x.xferObjectID(&m_wallToScale);
	x.xferInt(&m_wallIdlePasses);
	SnapshotXfer::checkRange(m_wallIdlePasses, 0, 4, "a horde's wall scaling idle passes"); // checkWallScalingDone: the fifth idle pass ends the scaling and resets it
	x.xferReal(&m_scalingReach);
}
