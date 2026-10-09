// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TransportContain / HordeTransportContain at runtime. See GameLogic/Object/Contain/TransportContainRuntime.h for the target facts and the stops. Lane GARRISON-2.

#include "GameLogic/Object/Contain/TransportContainRuntime.h"

#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/TransportContainBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>

namespace
{
const char *const kStopTransport =
	"[S-1104] TransportContain / HordeTransportContain (lane GARRISON-2): ported from RW 0x86A002 / 0x87A48C (who may enter: TransportSlotCount and Slots), 0x86A943 / "
	"0x86ABDC (on containing / removing: held, LOADED, PASSENGER, the WEAPONSTATE conditions, ExitDelay, OrientLikeContainerOnExit, ExitBone, the scatter of a dead "
	"container's rider RW 0x867AD4), OpenContain RW 0x868A70 (the riders on their PassengerBonePrefix bones, ForceOrientationContainer), 0x86B86D (update: "
	"HealthRegen%PerSec, UpgradeCreationTrigger RW 0x86B169, TRANSPORT_MOVING, the PASSENGER_TYPE weapon sets), 0x86A1FA / 0x86A104 (InitialPayload), the horde "
	"transport's 0x87A677 / 0x87A6D3 / 0x87A7B4 (a horde rides: the horde object out of the world, its members on the bones), its death 0x87AE0A (the members thrown "
	"off, GameLogicRandomValue(5, 10)) and the exits RW 0x8682D9 / 0x87A89E. Inference: the payload is made when the object is created (RW's caller of main slot 0x70 "
	"is not located). Not ported: the fades and the drawable bone attachment (client), GrabWeapon / CanGrabStructure (the trolls' grab), ShouldThrowOutPassengers' "
	"throw (RW 0x86A530), GoAggressiveOnExit (the AI attitude), the AI slot 0x1A8 of the exit test, the exits and entries of a SHIP in water (the shore spot RW 0x6EFBB8), an indestructible container's payload, "
	"PassengersInTurret, the arming of the falling riders (slot 0xF8: no caller located)";

const char *const kHordeTransportCpp = "HordeTransportContain.cpp"; // RW 0xC5C568
const char *const kOpenContainCpp = "OpenContain.cpp";             // RW 0xC59A10

int statusBit(const char *name)
{
	return CombatNames::status(name);
}

bool maskTest(const ObjectStatusMaskType &m, int bit)
{
	return bit >= 0 && (m[(size_t)bit >> 5] >> (bit & 31)) & 1u;
}

HordeContainInterface *hordeOf(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}

// RW 0x70C4FE / 0x70B8C7 testSetAndClear(mustBeSet, none): every bit of the mask (an empty mask: true)
bool hasAllKinds(const Object &obj, const KindOfMaskType &mask)
{
	const KindOfMaskType &k = obj.getKindOf();
	for (size_t w = 0; w < k.size(); ++w)
	{
		if ((k[w] & mask[w]) != mask[w])
		{
			return false;
		}
	}
	return true;
}

// RW 0x6C824C: the mask has a bit
bool anyBit(const KindOfMaskType &mask)
{
	for (std::uint32_t w : mask)
	{
		if (w != 0)
		{
			return true;
		}
	}
	return false;
}

void setCondition(Object &obj, int bit, bool on)
{
	if (bit >= 0 && obj.testModelCondition(bit) != on)
	{
		obj.setModelConditionState(bit, on); // RW 0x68B53C after the word change
	}
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// TransportContain
// ---------------------------------------------------------------------------------------------------------------------------------

TransportContain::TransportContain(Thing *thing, const ModuleData *data, const TransportContainModuleData &transport, const DieMuxData &dieMux)
	: OpenContain(thing, data, transport.m_open, dieMux)
	, m_transport(&transport)
{
	// RW 0x86B6DC .. 0x86B728: the upgrade names of UpgradeCreationTrigger, in data order
	for (const UpgradeCreationTriggerEntry &e : transport.m_upgradeCreationTrigger)
	{
		m_pendingUpgradeTriggers.push_back(e.first);
	}
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x86B86D answers OpenContain's 1: every frame
	getObject()->logic().noteStop(kStopTransport);
}

TransportContain::~TransportContain() = default;

void TransportContain::registerClasses(ModuleFactory &modules)
{
	modules.bindTypedData<TransportContainBehaviorData>("TransportContain", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<HordeTransportContainBehaviorData>("HordeTransportContain", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("TransportContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const TransportContainBehaviorData *d = dynamic_cast<const TransportContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("TransportContain: the module data is not typed (TransportContainBehaviorData)");
		}
		return std::make_unique<TransportContain>(thing, data, d->transport, d->m_dieMux);
	});
	modules.bindModuleProc("HordeTransportContain", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const HordeTransportContainBehaviorData *d = dynamic_cast<const HordeTransportContainBehaviorData *>(data);
		if (!d)
		{
			throw std::logic_error("HordeTransportContain: the module data is not typed (HordeTransportContainBehaviorData)");
		}
		return std::make_unique<HordeTransportContain>(thing, data, d->transport, d->m_dieMux);
	});
	SiegeEngineContainRegistry::registerClasses(modules);
	TunnelContain::registerClasses(modules); // the tunnel network (TunnelContainRuntime.h)
}

std::vector<std::string> TransportContain::stopLines()
{
	std::vector<std::string> out = { kStopTransport };
	for (const std::string &s : SiegeEngineContainRegistry::stopLines())
	{
		out.push_back(s);
	}
	return out;
}

int TransportContain::getContainMax() const
{
	return m_transport->m_slotCapacity; // RW 0x869F86: data + 0x98
}

bool TransportContain::forceOrientationContainer() const
{
	return m_transport->m_forceOrientationContainer; // RW 0x869F97: data + 0x13C
}

// RW 0x69029B: the template's TransportSlotCount (a signed byte, + 0x601); a contain whose slot 0x14 answers true sums its riders' (none of the ported classes)
int TransportContain::transportSlotCount(const Object &obj)
{
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	if (const FieldValue *v = tt->findField("TransportSlotCount"))
	{
		if (const long long *i = std::get_if<long long>(v))
		{
			return (int)(signed char)*i;
		}
	}
	return 0;
}

// RW 0x86A002
bool TransportContain::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	static const int temporarilyDefected = statusBit("TEMPORARILY_DEFECTED");
	static const int chunkVendor = CombatNames::kindOf("CHUNK_VENDOR");
	if (obj.testStatus((unsigned)temporarilyDefected))
	{
		return false; // RW 0x86A01A: status 0x3E
	}
	if (m_transport->m_conditionForEntry != -1 && !getObject()->testModelCondition(m_transport->m_conditionForEntry))
	{
		return false; // RW 0x86A02A: ConditionForEntry (data + 0x14C)
	}
	if (!openContainAllows(obj))
	{
		return false; // RW 0x86A06A: OpenContain RW 0x86603B
	}
	(void)checkPath;
	if (obj.isKindOf((unsigned)chunkVendor) && m_transport->m_canGrabStructure)
	{
		return (unsigned)getContainCount() < (unsigned)getContainMax(); // RW 0x86A07D: CanGrabStructure (data + 0x13D)
	}
	const int slots = transportSlotCount(obj);
	if (slots == 0)
	{
		return false;
	}
	if (!checkCapacity)
	{
		return true;
	}
	return (unsigned)((int)getContainCount() + m_extraSlotsInUse + slots) <= (unsigned)getContainMax(); // RW 0x86A0C6
}

// RW 0x86A943 TransportContain::onContaining
void TransportContain::onContaining(Object *obj, bool wasSelected)
{
	static const int transportMoving = CombatNames::modelCondition("TRANSPORT_MOVING");
	static const int loaded = CombatNames::modelCondition("LOADED");
	static const int passenger = CombatNames::modelCondition("PASSENGER");
	static const int workingPassenger = CombatNames::kindOf("WORKING_PASSENGER");
	static const int canAttack = statusBit("CAN_ATTACK");
	OpenContain::onContaining(obj, wasSelected);           // RW 0x86A956
	Object *container = getObject();
	obj->setDisabled(3, (UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x86A962: DISABLED_HELD (RW 0x692432)
	if (m_moving && obj->testModelCondition(transportMoving))
	{
		setCondition(*obj, transportMoving, false);         // RW 0x86A96E .. 0x86A985 (+ 0x117 bit 1)
	}
	m_extraSlotsInUse += transportSlotCount(*obj) - 1;      // RW 0x86A991
	if (getContainCount() == 1)
	{
		setCondition(*container, loaded, true);            // RW 0x86A9AB .. 0x86A9C1 (+ 0x114 bit 23)
	}
	// RW 0x86A9C6 .. 0x86AA14: with a drawable (every rider here) PASSENGER, or condition 90 for a rider that may fire from here (contain slot 0xB4 RW 0x865A7E:
	// CAN_ATTACK in ObjectStatusOfContained, the container's own container asked in turn) and is not a WORKING_PASSENGER; RW 0x67449C is the client's
	bool mayFire = maskTest(openData().m_objectStatusOfContained.mask, canAttack);
	for (const Object *c = container->getContainedBy(); mayFire && c; c = c->getContainedBy())
	{
		const ContainModuleInterface *cc = c->getContain();
		const ObjectStatusMaskType *m = cc ? cc->getObjectStatusOfContained() : nullptr;
		mayFire = m && maskTest(*m, canAttack);
	}
	const int riderCondition = (mayFire && !obj->isKindOf((unsigned)workingPassenger)) ? 90 : passenger;
	setCondition(*obj, riderCondition, true);
	setWeaponStateOfRider(*obj);                           // contain slot 0xEC (RW 0x86A43E)
	onRiderPlaced(obj, ridersFireFromContainer());          // contain slot 0x6C (rider, the ENCLOSED bit)
	if (!m_transport->m_grabWeapon.name.empty())
	{
		++m_stats.unported; // RW 0x86AA48 .. 0x86AA8E: GrabWeapon (the troll's grab) fired at the rider (S-1103)
	}
	if (m_transport->m_shouldThrowOutPassengers)
	{
		m_throwOutFrame = m_transport->m_throwOutPassengersDelay + container->logic().getFrame(); // RW 0x86AA97: data + 0x154
	}
	// RW 0x86AAAE .. 0x86ABCB: FadePassengerOnEnter (client side)
}

// RW 0x86ABDC TransportContain::onRemoving
void TransportContain::onRemoving(Object *obj)
{
	static const int transportMoving = CombatNames::modelCondition("TRANSPORT_MOVING");
	static const int loaded = CombatNames::modelCondition("LOADED");
	static const int passenger = CombatNames::modelCondition("PASSENGER");
	OpenContain::onRemoving(obj);                     // RW 0x86ABED: OpenContain RW 0x8680F4
	Object *container = getObject();
	GameLogic &logic = container->logic();
	obj->clearDisabled(3);                            // RW 0x86ABF4: RW 0x692443(3)
	setCondition(*obj, transportMoving, false);       // RW 0x86ABFF .. 0x86AC11
	// RW 0x86AC2B .. 0x86AC8B: ExitBone (data + 0xA0): the rider is put on the container's single bone of that name
	if (!m_transport->m_exitBone.empty())
	{
		Coord3D at;
		if (boneWorldPosition(*container, m_transport->m_exitBone, at))
		{
			obj->setPosition(&at);
		}
	}
	if (m_transport->m_orientLikeContainerOnExit)
	{
		obj->setOrientation(container->getOrientation()); // RW 0x86AC95: data + 0x13F
	}
	m_extraSlotsInUse += 1 - transportSlotCount(*obj);    // RW 0x86ACA7
	if (getContainCount() == 0)
	{
		setCondition(*container, loaded, false);         // RW 0x86ACBA .. 0x86ACD7
	}
	setCondition(*obj, passenger, false);                // RW 0x86ACDC .. 0x86ACF5 (+ 0x117 bit 0)
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (m_transport->m_goAggressiveOnExit && ai)
	{
		++m_stats.unported; // RW 0x86AD06: the AI's attitude AGGRESSIVE (RW 0x66E12A(2)): not in the port (S-1103)
	}
	if (container->isEffectivelyDead() && !obj->isEffectivelyDead())
	{
		scatterToNearbyPosition(obj); // RW 0x86AD18: RW 0x867AD4
	}
	if (m_transport->m_resetMoodCheckTimeOnExit && ai)
	{
		ai->wakeUpNow(); // RW 0x86AD2D: RW 0x662DB3 (the mood check now)
	}
	m_frameExitNotBusy = m_transport->m_exitDelay + logic.getFrame(); // RW 0x86AD3F: data + 0xAC
	// RW 0x86AD48 .. 0x86AD8A: the drawables' bone release with ReleaseSnappyness (client side)
	clearWeaponStates(); // contain slot 0xF0 (RW 0x86A4E4)
	// RW 0x86AD95 .. 0x86AEA1: FadePassengerOnExit (client side)
}

// RW 0x86B77B (contain slot 0x6C): the rider's drawable hidden as asked (its bone attachment is the client's)
void TransportContain::onRiderPlaced(Object *rider, bool hidden)
{
	rider->setDrawableHidden(hidden);
}

// RW 0x86A43E
void TransportContain::setWeaponStateOfRider(const Object &rider)
{
	static const int one = CombatNames::modelCondition("WEAPONSTATE_ONE");
	static const int two = CombatNames::modelCondition("WEAPONSTATE_TWO");
	static const int three = CombatNames::modelCondition("WEAPONSTATE_THREE");
	clearWeaponStates(); // RW 0x86A444: slot 0xF0
	Object *container = getObject();
	if (anyBit(m_transport->m_typeOneForWeaponState) && hasAllKinds(rider, m_transport->m_typeOneForWeaponState))
	{
		setCondition(*container, one, true);
	}
	else if (anyBit(m_transport->m_typeTwoForWeaponState) && hasAllKinds(rider, m_transport->m_typeTwoForWeaponState))
	{
		setCondition(*container, two, true);
	}
	else if (anyBit(m_transport->m_typeThreeForWeaponState) && hasAllKinds(rider, m_transport->m_typeThreeForWeaponState))
	{
		setCondition(*container, three, true);
	}
}

// RW 0x86A4E4
void TransportContain::clearWeaponStates()
{
	static const int one = CombatNames::modelCondition("WEAPONSTATE_ONE");
	static const int two = CombatNames::modelCondition("WEAPONSTATE_TWO");
	static const int three = CombatNames::modelCondition("WEAPONSTATE_THREE");
	Object *container = getObject();
	setCondition(*container, one, false);
	setCondition(*container, two, false);
	setCondition(*container, three, false);
}

// RW 0x86A731
void TransportContain::letRidersUpgradeWeaponSet()
{
	enum { PASSENGER_TYPE_ONE = 4, PASSENGER_TYPE_TWO = 5 }; // the WeaponSetType names RW (VETERAN ELITE HERO PLAYER_UPGRADE PASSENGER_TYPE_ONE PASSENGER_TYPE_TWO ...)
	bool one = false, two = false;
	for (const Object *r : m_contained)
	{
		if (hasAllKinds(*r, m_transport->m_typeOneForWeaponSet))
		{
			one = true; // RW 0x86A766: data + 0xB0 (an empty mask matches every rider)
		}
		else if (hasAllKinds(*r, m_transport->m_typeTwoForWeaponSet))
		{
			two = true; // data + 0xCC
		}
	}
	ObjectWeapons *w = getObject()->getWeapons();
	if (!w)
	{
		return;
	}
	const auto has = [w](int bit) { return ((w->weaponSetFlags()[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0; };
	if (one)
	{
		if (!has(PASSENGER_TYPE_ONE))
		{
			w->setWeaponSetFlag(PASSENGER_TYPE_ONE, true); // RW 0x691059(4)
		}
	}
	else if (has(PASSENGER_TYPE_ONE))
	{
		w->setWeaponSetFlag(PASSENGER_TYPE_ONE, false); // RW 0x691106(4)
	}
	if (two)
	{
		if (!has(PASSENGER_TYPE_TWO))
		{
			w->setWeaponSetFlag(PASSENGER_TYPE_TWO, true);
		}
	}
	else if (has(PASSENGER_TYPE_TWO))
	{
		w->setWeaponSetFlag(PASSENGER_TYPE_TWO, false);
	}
}

// RW 0x867AD4 OpenContain::scatterToNearbyPosition
void TransportContain::scatterToNearbyPosition(Object *rider)
{
	Object *container = getObject();
	GameLogic &logic = container->logic();
	float angle = logic.random().getValueReal(0.0f, 6.28318548f, kOpenContainCpp, 0x3E0); // [0xBDD38C] = 2 pi
	if (!ridersFireFromContainer())
	{
		// RW 0x867B12: the container's angle plus the rider's relative angle (RW 0x4B3D8D relativeAngle2D: acos of the facing . the unit offset, the sign of the cross)
		const Coord3D &cp = *container->getPosition();
		const Coord3D &rp = *rider->getPosition();
		const float dx = SimMath::subf32(rp.x, cp.x), dy = SimMath::subf32(rp.y, cp.y);
		const float len = (float)SimMath::sqrtd((double)SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)));
		float rel = 0.0f;
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			const float ux = SimMath::mulf32(inv, dx), uy = SimMath::mulf32(inv, dy);
			const float *b = container->getBasis();
			const float fx = b[0], fy = b[3]; // RW 0x70B9E0: the unit direction (cos, sin of the angle): the basis' first column
			float dot = SimMath::addf32(SimMath::mulf32(fx, ux), SimMath::mulf32(fy, uy));
			if (dot < -1.0f || dot > 1.0f)
			{
				dot = dot < -1.0f ? -1.0f : 1.0f;
			}
			rel = (float)SimMath::acosDet((double)dot);
			if (SimMath::subf32(SimMath::mulf32(fx, uy), SimMath::mulf32(fy, ux)) < 0.0f)
			{
				rel = SimMath::subf32(0.0f, rel);
			}
		}
		angle = SimMath::addf32(rel, container->getOrientation());
	}
	const float radius = CombatQueries::boundingCircleRadius(*container); // + 0xB8
	const float dist = logic.random().getValueReal(radius, SimMath::mulf32(radius, 1.5f), kOpenContainCpp, 0x3EE); // [0xBDE8C8] = 1.5
	const Coord3D &cp = *container->getPosition();
	Coord3D pos;
	pos.x = SimMath::addf32(SimMath::mulf32(SimMath::cosf32(angle), dist), cp.x);
	pos.y = SimMath::addf32(SimMath::mulf32(SimMath::sinf32(angle), dist), cp.y);
	pos.z = logic.getGroundHeight(pos.x, pos.y); // RW 0x867BAF: the terrain's layer height
	AIUpdateInterface *ai = rider->getAIUpdateInterface();
	if (!ai)
	{
		rider->setPosition(&pos);
	}
	else
	{
		if (ridersFireFromContainer())
		{
			rider->setPosition(&cp); // RW 0x867BE4 .. 0x867BFC (the pathfinder's RW 0x6260E1 in between)
		}
		ai->mover().ignoreObstacle(container->getID()); // RW 0x66831A
		ai->aiMoveToPosition(pos, CMD_FROM_AI);         // RW 0x66C4CA(&pos, 2)
	}
	++m_stats.scattered;
}

// RW 0x86A2D0 (main slot 0x6C)
bool TransportContain::isSpecificRiderFreeToExit(const Object &rider) const
{
	const Object *container = getObject();
	// RW 0x86A2E8: the container's AI slot 0x1A8 (rider) is not identified: taken as 0 (S-1103)
	const AIUpdateInterface *cai = container->getAIUpdateInterface();
	if (cai && cai->curLocomotor() && (cai->curLocomotor()->getTemplate().m_surfaces & (unsigned)LOCOMOTORSURFACE_AIR))
	{
		return true; // RW 0x68BEED: the container's locomotor (+ 0x1F0) has the AIR surface (template + 0x14 bit 3)
	}
	const AIUpdateInterface *ai = rider.getAIUpdateInterface();
	if (!ai || !ai->curLocomotor())
	{
		return false; // RW 0x86A31A: the rider's locomotor (+ 0x1F0)
	}
	if (isShipOnWater(*container))
	{
		return false; // RW 0x86A330: a SHIP in water asks RW 0x6EFBB8 for a shore spot: S-1103's; a ship on dry ground takes the ground test below
	}
	// RW 0x86A36C: RW 0x6E8707 (ZH Pathfinder::validMovementTerrain) for the rider's locomotor at the container's position on the ground layer (RW 0x68BBE0: the
	// layer of the position; 2.01 maps have no bridge layers)
	return OpenContain::validMovementTerrain(const_cast<AIUpdateInterface &>(*ai), *container->getPosition());
}

// RW 0x86B05F
void TransportContain::killRidersWhoAreNotFreeToExit()
{
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	for (Object *r : riders)
	{
		if (!isSpecificRiderFreeToExit(*r))
		{
			if (m_transport->m_destroyRidersWhoAreNotFreeToExit)
			{
				r->logic().destroyObject(r); // data + 0x142
			}
			else
			{
				r->kill(0); // RW 0x698EC3(8, 0)
			}
			++m_stats.ridersKilledNotFree;
		}
	}
}

// RW 0x869E73 (exit interface slot 0)
bool TransportContain::isExitBusy() const
{
	return getObject()->logic().getFrame() < m_frameExitNotBusy;
}

// RW 0x869E5D (exit interface slot 4): DOOR_1 when the rider is free to exit, DOOR_NONE_AVAILABLE (-1) otherwise
int TransportContain::reserveDoorForExit(const Object &obj)
{
	return isSpecificRiderFreeToExit(obj) ? 0 : -1;
}

void TransportContain::onObjectCreated()
{
	if (!m_payloadCreated)
	{
		m_payloadCreated = true;
		createPayload();
	}
}

// RW 0x86A1FA
void TransportContain::createPayload()
{
	Object *container = getObject();
	GameLogic &logic = container->logic();
	ContainModuleInterface *contain = container->getContain();
	for (const InitialPayloadEntry &e : m_transport->m_initialPayload)
	{
		if (e.count < 1)
		{
			return; // RW 0x86A21A
		}
		const ThingTemplate *t = logic.things().findTemplate(e.name);
		if (!t)
		{
			logic.reportError("TransportContain of " + container->getTemplate()->getName() + ": InitialPayload " + e.name + " is not a template");
			return; // RW 0x86A228
		}
		if (!contain)
		{
			return;
		}
		// RW 0x86A23B / 0x86A2B1: enableLoadSounds off and on (client side)
		for (int i = 0; i < e.count; ++i)
		{
			createPayloadObject(*t, *contain, true); // the creator at + 0xFC: RW 0x86A104(t, contain, container, name, 1)
		}
	}
}

// RW 0x86A104
Object *TransportContain::createPayloadObject(const ThingTemplate &t, ContainModuleInterface &contain, bool checkCapacity)
{
	Object *container = getObject();
	GameLogic &logic = container->logic();
	// RW 0x86A118 .. 0x86A15C: CREATE_DRAWABLE_WITH_LOW_DETAIL for a horde payload of a low detail setting (client side): no status
	Object *o = logic.newObject(&t, container->getTeam(), ObjectStatusMaskType{}); // RW 0x6D165E(t, container + 0x31C)
	if (!o)
	{
		return nullptr;
	}
	++m_stats.payloadObjects;
	o->setPosition(container->getPosition()); // the new object stands at the container (RW's new objects start at the origin until a contain places them)
	o->friend_onBuildComplete();              // RW 0x86A16E .. 0x86A18A: the create modules' slot 1
	if (contain.isValidContainerFor(*o, checkCapacity, false))
	{
		// RW 0x86A19C .. 0x86A1D2: an indestructible container (body slot 0x8C) makes the payload indestructible (body slot 0x88(1)): not in the port's bodies (S-1103)
		contain.addToContain(o);
	}
	else
	{
		++m_stats.payloadRefused;
	}
	return o;
}

// RW 0x86B169
void TransportContain::processUpgradeCreationTriggers()
{
	if (m_pendingUpgradeTriggers.empty())
	{
		return;
	}
	Object *container = getObject();
	GameLogic &logic = container->logic();
	ContainModuleInterface *contain = container->getContain();
	if (!contain)
	{
		return;
	}
	bool anyFound = false;
	for (std::string &name : m_pendingUpgradeTriggers)
	{
		if (name.empty())
		{
			continue;
		}
		const UpgradeCreationTriggerEntry *entry = nullptr;
		for (const UpgradeCreationTriggerEntry &e : m_transport->m_upgradeCreationTrigger)
		{
			if (e.first == name)
			{
				entry = &e; // RW 0x86A8BD
				break;
			}
		}
		if (!entry)
		{
			continue;
		}
		anyFound = true;
		if (!container->hasUpgrade(name))
		{
			continue; // RW 0x66F5E5 (the upgrade template) / RW 0x691421 (the object has it)
		}
		name.clear();
		const ThingTemplate *t = logic.things().findTemplate(entry->second);
		if (!t)
		{
			continue;
		}
		const int slots = (int)(signed char)[&]() {
			const ThingTemplate *tt = t->getFinalOverride();
			const FieldValue *v = tt->findField("TransportSlotCount");
			const long long *i = v ? std::get_if<long long>(v) : nullptr;
			return i ? (int)*i : 0;
		}();
		for (unsigned k = 0; k < entry->count; ++k)
		{
			// RW 0x869E1A: room for the template's slots (none: no room)
			if (slots == 0 || (unsigned)((int)getContainCount() + m_extraSlotsInUse + slots) > (unsigned)getContainMax())
			{
				break;
			}
			Object *o = logic.newObject(t, container->getTeam(), ObjectStatusMaskType{});
			if (!o)
			{
				break;
			}
			o->setPosition(container->getPosition());
			if (!contain->isValidContainerFor(*o, false, false))
			{
				logic.destroyObject(o); // RW 0x86B2A5
				break;
			}
			contain->addToContain(o); // RW 0x86B2D6: contain slot 0x9C
			++m_stats.upgradeObjects;
		}
	}
	if (!anyFound)
	{
		m_pendingUpgradeTriggers.clear(); // RW 0x86B30E
	}
}

// RW 0x86B86D
UpdateSleepTime TransportContain::update()
{
	static const int moving = CombatNames::modelCondition("MOVING");
	static const int transportMoving = CombatNames::modelCondition("TRANSPORT_MOVING");
	Object *container = getObject();
	GameLogic &logic = container->logic();
	if (m_transport->m_shouldThrowOutPassengers && m_throwOutFrame <= logic.getFrame())
	{
		++m_stats.unported; // RW 0x86B885: main slot 0x74 (RW 0x86A530, the physics throw: S-1103)
	}
	// RW 0x86B897 .. 0x86B91F: HealthRegen%PerSec (data + 0xA8) heals each rider below its maximum by max x % x 0.2 x 0.01 per frame (x87)
	if (m_transport->m_healthRegenPercentPerSec != 0.0f)
	{
		const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
		for (Object *r : riders)
		{
			BodyModuleInterface *body = r->getBodyModule();
			if (body && body->getHealth() < body->getMaxHealth())
			{
				const double amount = SimMath::mulD(SimMath::mulD(SimMath::mulD((double)body->getMaxHealth(), (double)m_transport->m_healthRegenPercentPerSec), (double)0.2f), (double)0.01f);
				r->attemptHealing((float)amount, container); // RW 0x690532
				++m_stats.healed;
			}
		}
	}
	processUpgradeCreationTriggers(); // RW 0x86B925
	// RW 0x86B92C .. 0x86B9DE: the container's MOVING mirrored onto the riders' TRANSPORT_MOVING when it changes
	const bool isMoving = container->testModelCondition(moving);
	if (isMoving != m_moving)
	{
		m_moving = isMoving;
		for (Object *r : m_contained)
		{
			setCondition(*r, transportMoving, m_moving);
		}
	}
	const UpdateSleepTime s = OpenContain::update(); // RW 0x86B9DE
	letRidersUpgradeWeaponSet();                     // main slot 0x78
	return s;
}

void TransportContain::crc(StateHasher &h) const
{
	OpenContain::crc(h);
	h.addI32(m_extraSlotsInUse);
	h.addU32(m_frameExitNotBusy);
	h.addU32(m_throwOutFrame);
	h.addBool(m_moving);
	h.addBool(m_payloadCreated);
	h.addU32((std::uint32_t)m_pendingUpgradeTriggers.size());
	for (const std::string &s : m_pendingUpgradeTriggers)
	{
		h.addString(s);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// HordeTransportContain
// ---------------------------------------------------------------------------------------------------------------------------------

HordeTransportContain::HordeTransportContain(Thing *thing, const ModuleData *data, const TransportContainModuleData &transport, const DieMuxData &dieMux)
	: TransportContain(thing, data, transport, dieMux)
{
}

// RW 0x990CB9: a contained horde whose contain counts `obj` as a member or one on its way in
bool HordeTransportContain::memberInsideHorde(const Object &obj, Object **horde) const
{
	for (Object *c : m_contained)
	{
		HordeContainInterface *h = hordeOf(*c);
		if (h && h->isMemberOrEntering(obj))
		{
			if (horde)
			{
				*horde = c;
			}
			return true;
		}
	}
	return false;
}

// RW 0x87A48C
bool HordeTransportContain::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	static const int hordeMember = statusBit("HORDE_MEMBER");
	(void)checkPath;
	if (!openContainAllows(obj))
	{
		return false; // RW 0x87A49A: OpenContain RW 0x86603B
	}
	if (!obj.getContain() && obj.testStatus((unsigned)hordeMember))
	{
		return memberInsideHorde(obj, nullptr); // RW 0x87A4B5: RW 0x990CB9
	}
	if (checkCapacity)
	{
		// RW 0x87A4D0 .. 0x87A50F: Slots (data + 0x98) below count + extra (contain slot 0xCC RW 0x86B050) + the object's slots refuses (unsigned)
		if ((unsigned)m_transport->m_slotCapacity < (unsigned)((int)getContainCount() + m_extraSlotsInUse + transportSlotCount(obj)))
		{
			return false;
		}
	}
	return true;
}

// RW 0x87A677
bool HordeTransportContain::addToContain(Object *obj)
{
	static const int hordeMember = statusBit("HORDE_MEMBER");
	static const int weaponStateContained = CombatNames::modelCondition("WEAPONSTATE_CONTAINED");
	enum { CONTAINED = 20 }; // the WeaponSetType CONTAINED (RW 0x691014 / 0x691059(0x14))
	if (!obj || obj->getContainedBy())
	{
		return false; // RW 0x87A67F: + 0x27C
	}
	HordeContainInterface *h = hordeOf(*obj);
	const auto addContainedWeaponSet = [](Object *o) {
		ObjectWeapons *w = o->getWeapons();
		if (w && w->hasWeaponSetFor(CONTAINED))
		{
			w->setWeaponSetFlag(CONTAINED, true);
			return true;
		}
		return false;
	};
	if (!h && obj->testStatus((unsigned)hordeMember))
	{
		// RW 0x87A5A6: a member of a horde inside rejoins it (RW 0x990DEA) ...
		Object *horde = nullptr;
		if (!memberInsideHorde(*obj, &horde))
		{
			redeployOccupants();
			return false;
		}
		HordeContainInterface *hh = hordeOf(*horde);
		bool joined = false;
		if (!hh->allMembersEntered())
		{
			hh->acceptMemberFromGarrison(obj); // horde slot 0x20
			hh->setLastMemberEnteredFrame(obj->logic().getFrame());
			if (hh->allMembersEntered())
			{
				hh->setLastMemberEnteredFrame(0); // RW 0x990E35 (the global byte RW 0xDE7744 + 0x28 aside)
			}
			joined = true;
		}
		else
		{
			hh->setLastMemberEnteredFrame(0);
		}
		if (joined)
		{
			// ... the CONTAINED weapon set and WEAPONSTATE_CONTAINED (+ 0x128 bit 18), the redeploy, TransportContain::onContaining(member, false)
			if (addContainedWeaponSet(obj))
			{
				setCondition(*obj, weaponStateContained, true);
			}
			redeployOccupants();
			TransportContain::onContaining(obj, false);
		}
		redeployOccupants(); // RW 0x87A6A2: main slot 0x48
		return joined;
	}
	if (h)
	{
		h->setGarrisoned(true); // RW 0x87A69A: horde slot 0x124
	}
	// RW 0x87A329: OpenContain's add, the horde's world exit RW 0x990E5F (UNSELECTABLE only for a selected drawable: client side; out of the world unless it holds
	// the Ring), the CONTAINED weapon set
	openContainAdd(obj);
	static const int holdingTheRing = statusBit("HOLDING_THE_RING");
	if (obj->isInWorld() && !obj->testStatus((unsigned)holdingTheRing))
	{
		obj->logic().friend_containLeaveWorld(*obj);
	}
	addContainedWeaponSet(obj);
	redeployOccupants(); // RW 0x87A6A2
	return true;
}

// RW 0x87A6D3
void HordeTransportContain::removeFromContain(Object *obj)
{
	static const int hordeMember = statusBit("HORDE_MEMBER");
	static const int weaponStateContained = CombatNames::modelCondition("WEAPONSTATE_CONTAINED");
	enum { CONTAINED = 20 };
	if (!obj)
	{
		return;
	}
	const auto clearContainedWeaponSet = [](Object *o) {
		if (ObjectWeapons *w = o->getWeapons())
		{
			w->setWeaponSetFlag(CONTAINED, false); // RW 0x691106(0x14)
		}
	};
	// RW 0x87A361: OpenContain's removal, CONTAINED cleared, RW 0x990EE6: back into the world, not UNSELECTABLE
	const auto removeRider = [this, &clearContainedWeaponSet](Object *o) {
		openContainRemove(o);
		clearContainedWeaponSet(o);
		if (!o->isInWorld())
		{
			o->logic().friend_containEnterWorld(*o);
		}
		static const int unselectable = statusBit("UNSELECTABLE");
		o->setStatus((unsigned)unselectable, false);
	};
	HordeContainInterface *h = hordeOf(*obj);
	if (h)
	{
		removeRider(obj);
		h->setGarrisoned(false); // horde slot 0x128
		h->returnToFormation(true); // horde slot 0x10 RW 0x8759FF(1): the members on their way rejoin the horde and it re-forms (lane GARRISON-3)
		return;
	}
	if (!obj->testStatus((unsigned)hordeMember))
	{
		removeRider(obj);
		return;
	}
	// RW 0x87A606: a member: OpenContain's removal (not in this list: nothing), TransportContain::onRemoving, CONTAINED and WEAPONSTATE_CONTAINED cleared, back into
	// the world, a hidden drawable shown
	openContainRemove(obj);
	TransportContain::onRemoving(obj);
	clearContainedWeaponSet(obj);
	setCondition(*obj, weaponStateContained, false);
	if (!obj->isInWorld())
	{
		obj->logic().friend_containEnterWorld(*obj);
	}
	obj->setDrawableHidden(false);
}

// RW 0x87A51F: the horde object stands at the transport's position (the transform's translation)
void HordeTransportContain::putHordeAtContainer(Object *horde)
{
	horde->setPosition(getObject()->getPosition());
}

// RW 0x87A7B4
void HordeTransportContain::redeployOccupants()
{
	static const int hordeKind = CombatNames::kindOf("HORDE");
	std::vector<Object *> riders;
	const std::vector<Object *> contained(m_contained.begin(), m_contained.end());
	for (Object *c : contained)
	{
		if (!c->isKindOf((unsigned)hordeKind))
		{
			riders.push_back(c);
			continue;
		}
		ContainModuleInterface *hc = c->getContain();
		putHordeAtContainer(c);
		if (const ContainedItemsList *members = hc ? hc->getContainedItemsList() : nullptr)
		{
			riders.insert(riders.end(), members->begin(), members->end()); // horde slot 0x108
		}
	}
	putRidersAtBones(riders); // RW 0x866AE3 -> main slot 0x68
}

// RW 0x87A3C2: RW 0x990C6E (a hidden member leaves the world, a shown one out of it enters it again), then TransportContain's RW 0x86B77B
void HordeTransportContain::onRiderPlaced(Object *rider, bool hidden)
{
	if (!hidden)
	{
		if (rider->isDrawableHidden() && !rider->isInWorld())
		{
			rider->logic().friend_containEnterWorld(*rider);
		}
	}
	else if (!rider->isDrawableHidden() && rider->isInWorld())
	{
		rider->logic().friend_containLeaveWorld(*rider);
	}
	TransportContain::onRiderPlaced(rider, hidden);
}

// RW 0x87A559 -> RW 0x991027: a horde rider gets horde slot 0x84 (its AI command 0x3E), another rider with an AI aiExit (command 0x1A)
void HordeTransportContain::orderAllPassengersToExit(int source)
{
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
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

// RW 0x87A89E: a horde: its members out of the world first, then every member (each released from the horde, slot 0xA8, and through OpenContain's exit RW 0x8682D9),
// then the horde object; any other rider through RW 0x8682D9
void HordeTransportContain::exitObjectViaDoor(Object *obj, int door)
{
	if (!obj)
	{
		return;
	}
	HordeContainInterface *h = hordeOf(*obj);
	if (!h)
	{
		OpenContain::exitObjectViaDoor(obj, door);
		return;
	}
	std::vector<Object *> members;
	if (const ContainedItemsList *items = obj->getContain()->getContainedItemsList())
	{
		members.assign(items->begin(), items->end());
	}
	for (Object *m : members)
	{
		if (!m->isInWorld())
		{
			h->releaseMemberForGarrison(m);
			OpenContain::exitObjectViaDoor(m, door);
		}
	}
	for (Object *m : members)
	{
		h->releaseMemberForGarrison(m);
		OpenContain::exitObjectViaDoor(m, door);
	}
	OpenContain::exitObjectViaDoor(obj, door);
}

// RW 0x87AE0A (the die interface; no DieMux test)
void HordeTransportContain::onDie(const DieModuleInterface::Event &event)
{
	(void)event;
	m_dying = true;
	if (openData().m_ejectPassengersOnDeath)
	{
		// RW 0x87AA2C: every member of every horde rider, in turn on the bones of its prefix (the index wraps), thrown off and killed
		const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
		int index = 0;
		for (Object *r : riders)
		{
			HordeContainInterface *h = hordeOf(*r);
			if (!h)
			{
				continue;
			}
			std::vector<Object *> members;
			if (const ContainedItemsList *items = r->getContain()->getContainedItemsList())
			{
				members.assign(items->begin(), items->end());
			}
			for (Object *m : members)
			{
				index = throwMemberOff(m, h, index, 5, 10, 0x234) + 1; // RW 0x87AB4F: the index after the reset keeps cycling
			}
		}
		return;
	}
	if (!(m_killingRiders && openData().m_killPassengersOnDeath))
	{
		return; // RW 0x86588C: + 0xE1 and KillPassengersOnDeath
	}
	// RW 0x990F0A: every rider (a horde: its members) out of the contain (no-op for members), RW 0x690728, killed, hidden
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	for (Object *r : riders)
	{
		HordeContainInterface *h = hordeOf(*r);
		std::vector<Object *> victims;
		if (!h)
		{
			victims.push_back(r);
		}
		else if (const ContainedItemsList *items = r->getContain()->getContainedItemsList())
		{
			victims.assign(items->begin(), items->end());
		}
		for (Object *v : victims)
		{
			removeFromContain(v);
			v->kill(0);
			v->setDrawableHidden(true);
			++m_openStats.ridersDestroyedOnDeath;
		}
	}
}

// RW 0x87AA2C's member body / main slot 0x3C RW 0x87AE43: the member's bone (`boneIndex` < 0: the member's recorded bone, else the index wrapped to the bone count),
// out of the horde (slot 0xA8) and of this contain, on the bone, flung with GameLogicRandomValue(lo, hi) (a member with a PhysicsBehavior; the physics shock RW
// 0x792A69 and STUNNED_FLAILING are S-782's / the client's), killed
int HordeTransportContain::throwMemberOff(Object *member, HordeContainInterface *horde, int boneIndex, int lo, int hi, int line)
{
	static const int stunnedFlailing = CombatNames::modelCondition("STUNNED_FLAILING");
	Object *container = getObject();
	GameLogic &logic = container->logic();
	// the member's PassengerBonePrefix (RW 0x86630B with the member)
	std::string prefix = "ARROW_";
	for (const PassengerBonePrefixEntry &e : openData().m_passengerBonePrefix)
	{
		bool all = true;
		for (size_t w = 0; w < member->getKindOf().size(); ++w)
		{
			all = all && (member->getKindOf()[w] & e.kindOf[w]) == e.kindOf[w];
		}
		if (all)
		{
			prefix = e.bonePrefix;
			break;
		}
	}
	ProjectileLaunchOffsets *provider = logic.combat().launchOffsets();
	float bones[32][12];
	int count = provider ? provider->multiLogicalBones(*container, prefix, container->getModelConditionBits(), 32, bones) : 0;
	if (count <= 0 && provider && provider->singleLogicalBone(*container, prefix, bones[0]))
	{
		count = 1;
	}
	int idx = boneIndex;
	if (idx < 0)
	{
		auto it = m_riderBoneIndex.find(member->getID());
		idx = it == m_riderBoneIndex.end() ? 0 : it->second;
	}
	if (count <= idx)
	{
		idx = 0;
	}
	horde->releaseMemberForGarrison(member); // horde slot 0xA8
	removeFromContain(member);               // contain slot 0xA4(member, false)
	if (count > 0)
	{
		const float *cb = container->getBasis();
		const Coord3D &cp = *container->getPosition();
		const float *b = bones[idx];
		const float tt[3] = { cp.x, cp.y, cp.z };
		float w[3];
		for (int r = 0; r < 3; ++r)
		{
			const float v = SimMath::addf32(SimMath::mulf32(cb[r * 3 + 2], b[11]), SimMath::mulf32(cb[r * 3 + 1], b[7]));
			w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(cb[r * 3 + 0], b[3])), tt[r]);
		}
		Coord3D at{ w[0], w[1], w[2] };
		member->setPosition(&at); // RW 0x87AB6C: RW 0x70C201 (the bone's translation)
	}
	if (PhysicsBehavior *phys = PhysicsBehavior::find(*member))
	{
		const float k = (float)logic.random().getValue(lo, hi, kHordeTransportCpp, line);
		const float *m = member->getBasis();
		setCondition(*member, stunnedFlailing, true); // RW 0x87ABA6 .. 0x87ABBB (+ 0x118 bit 31)
		phys->fling(Coord3D{ SimMath::mulf32(m[0], k), SimMath::mulf32(m[3], k), k });
	}
	member->kill(0); // RW 0x698EC3(8, 0)
	++m_openStats.ridersDestroyedOnDeath;
	return idx;
}

// RW 0x87AD94
UpdateSleepTime HordeTransportContain::update()
{
	if (m_ridersFalling)
	{
		GameLogic &logic = getObject()->logic();
		if (m_fallTimer == -1000)
		{
			m_fallTimer = logic.random().getValue(3, 5, kHordeTransportCpp, 0x35A);
			// RW 0x87AC9A: every member whose drawable is hidden (or that has none) leaves its horde and this contain, is killed and stays hidden
			const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
			for (Object *r : riders)
			{
				HordeContainInterface *h = hordeOf(*r);
				if (!h)
				{
					continue;
				}
				std::vector<Object *> members;
				if (const ContainedItemsList *items = r->getContain()->getContainedItemsList())
				{
					members.assign(items->begin(), items->end());
				}
				for (Object *m : members)
				{
					if (m->isDrawableHidden())
					{
						h->releaseMemberForGarrison(m);
						removeFromContain(m);
						m->kill(0);
						m->setDrawableHidden(true);
					}
				}
			}
		}
		if (m_fallTimer < 1)
		{
			// RW 0x87A72F: the first member of each horde rider, when shown, falls off (main slot 0x3C RW 0x87AE43: its recorded bone, GameLogicRandomValue(3, 8))
			const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
			for (Object *r : riders)
			{
				HordeContainInterface *h = hordeOf(*r);
				const ContainedItemsList *items = h ? r->getContain()->getContainedItemsList() : nullptr;
				if (!items || items->empty())
				{
					continue;
				}
				Object *first = items->front();
				if (!first->isDrawableHidden())
				{
					throwMemberOff(first, h, -1, 3, 8, 0x2BE);
				}
			}
			m_fallTimer = logic.random().getValue(0, 4, kHordeTransportCpp, 0x367);
		}
		--m_fallTimer;
	}
	return TransportContain::update(); // RW 0x87ADE7: RW 0x86B86D
}

void HordeTransportContain::crc(StateHasher &h) const
{
	TransportContain::crc(h);
	h.addI32(m_fallTimer);
	h.addBool(m_ridersFalling);
}
