// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// OpenContain at runtime. See GameLogic/Object/Contain/OpenContainRuntime.h for the target facts and the stops. Lane GARRISON-2 (the parts lane GARRISON-1 ported
// inside GarrisonContain, moved here unchanged, and RW 0x8682D9 / 0x86702D / 0x867242 / 0x866B69 / 0x867D6A).

#include "GameLogic/Object/Contain/OpenContainRuntime.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace
{
const char *const kOpenContainCpp = "OpenContain.cpp"; // the logic RNG call sites of RW's OpenContain (RW 0x866B69 draws at line 0x8DE)

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

} // namespace

// RW 0x68C5AF Object::getSingleLogicalBonePosition through the launch bone provider, then RW 0x70BCE7 (the object's transform, retail's SSE order); false
// without a provider or without exactly one such bone (the caller keeps the object's own position)
bool OpenContain::boneWorldPosition(Object &obj, const std::string &bone, Coord3D &out)
{
	ProjectileLaunchOffsets *provider = obj.logic().combat().launchOffsets();
	float b[12];
	if (!provider || !provider->singleLogicalBone(obj, bone, b))
	{
		return false;
	}
	const float *t = obj.getBasis();
	const Coord3D &p = *obj.getPosition();
	const float tt[3] = { p.x, p.y, p.z };
	float w[3];
	for (int r = 0; r < 3; ++r)
	{
		const float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], b[11]), SimMath::mulf32(t[r * 3 + 1], b[7]));
		w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], b[3])), tt[r]);
	}
	out = Coord3D{ w[0], w[1], w[2] };
	return true;
}

OpenContain::OpenContain(Thing *thing, const ModuleData *data, const OpenContainModuleData &open, const DieMuxData &dieMux)
	: UpdateModule(thing, data)
	, m_open(&open)
	, m_dieMux(&dieMux)
{
	m_enabled = open.m_enabled; // RW 0x867F56: the enabled byte from the data's Enabled (+ 0x84)
}

OpenContain::~OpenContain() = default;

const ObjectStatusMaskType *OpenContain::getObjectStatusOfContained() const
{
	return &openData().m_objectStatusOfContained.mask; // RW 0x865C15: data + 0x58
}

bool OpenContain::isEnclosingContainerFor(const Object &obj) const
{
	static const int enclosed = statusBit("ENCLOSED");
	return maskTest(statusMaskFor(&obj), enclosed); // RW 0x6CAC1F: slot 0xB0(obj), bit 29 of word 1 = status 61
}

const ObjectStatusMaskType &OpenContain::statusMaskFor(const Object *obj) const
{
	(void)obj;
	return openData().m_objectStatusOfContained.mask; // RW 0x865C15: data + 0x58
}

bool OpenContain::isContainedHere(const Object &obj) const
{
	return std::find(m_contained.begin(), m_contained.end(), &obj) != m_contained.end(); // RW 0x8662D0
}

// RW 0x865E78
void OpenContain::removeFromContainList(Object *obj)
{
	auto it = std::find(m_contained.begin(), m_contained.end(), obj);
	if (it != m_contained.end())
	{
		m_contained.erase(it);
	}
}

bool OpenContain::ridersFireFromContainer() const
{
	static const int enclosed = statusBit("ENCLOSED");
	return maskTest(openData().m_objectStatusOfContained.mask, enclosed); // RW 0x6CAC1F: bit 29 of word 1 = status 61
}

bool OpenContain::allowAlliesInside() const
{
	return openData().m_allowAlliesInside; // RW 0x865A55: data + 0x7D
}

// ---------------------------------------------------------------------------------------------------------------------------------
// who may enter
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x86603B
bool OpenContain::openContainAllows(const Object &obj) const
{
	static const CombatNames::Status &st = CombatNames::statuses();
	static const int temporarilyDefected = statusBit("TEMPORARILY_DEFECTED");
	static const int holdingTheRing = statusBit("HOLDING_THE_RING");
	static const int canEnterAnything = statusBit("CAN_ENTER_ANYTHING");
	static const int comboHorde = CombatNames::kindOf("COMBO_HORDE");
	static const int garrisonKind = CombatNames::kindOf("GARRISON");
	static const int fsCashProducer = CombatNames::kindOf("FS_CASH_PRODUCER");
	static const int chunkVendor = CombatNames::kindOf("CHUNK_VENDOR");
	static const int rockVendor = CombatNames::kindOf("ROCK_VENDOR");
	static const int cavalry = CombatNames::kindOf("CAVALRY");
	static const int mounted = CombatNames::modelCondition("MOUNTED");
	const Object *container = getObject();
	GameLogic &logic = container->logic();
	const OpenContainModuleData &d = openData();
	if (obj.isDestroyed() || obj.testStatus((unsigned)temporarilyDefected) || obj.testStatus((unsigned)holdingTheRing) || !m_enabled)
	{
		return false;
	}
	if (obj.isKindOf((unsigned)comboHorde) && !container->isKindOf((unsigned)garrisonKind) && !container->isKindOf((unsigned)fsCashProducer))
	{
		return false; // RW 0x86609C .. 0x8660BB
	}
	if (obj.isKindOf((unsigned)chunkVendor) || obj.isKindOf((unsigned)rockVendor))
	{
		// RW 0x8660F0: the body's name (body slot 0x74) as a template asked of the filter; the body slot is not identified (S-1100): refused and reported
		logic.reportError("OpenContain of " + container->getTemplate()->getName() + ": the CHUNK_VENDOR / ROCK_VENDOR entry test (RW 0x8660F0) is not ported; " +
			obj.getTemplate()->getName() + " refused (S-1100)");
		return false;
	}
	if (!ObjectFilterMatch::allows(logic, d.m_passengerFilter, obj, container->getControllingPlayer()))
	{
		return false; // RW 0x8660E2: PassengerFilter (data + 0x40) with the container's player
	}
	// RW 0x866165: a filter excluding CAVALRY refuses a MOUNTED object (RW 0x7634A1(9): the exclude mask's bit 9)
	if (((d.m_passengerFilter.excludeKindOf[(size_t)cavalry >> 5] >> (cavalry & 31)) & 1u) && obj.testModelCondition(mounted))
	{
		return false;
	}
	if (!obj.testStatus((unsigned)canEnterAnything))
	{
		const Relationship rel = obj.getRelationship(*container); // RW 0x68D7AB(obj, container)
		bool ok = false;
		if (rel == ENEMIES)
		{
			ok = d.m_allowEnemiesInside; // data + 0x7E
		}
		else if (rel == NEUTRAL)
		{
			ok = d.m_allowNeutralInside; // + 0x7F
		}
		else if (rel == ALLIES)
		{
			ok = (obj.getControllingPlayer() == container->getControllingPlayer() && d.m_allowOwnPlayerInsideOverride) || d.m_allowAlliesInside; // + 0x7C / + 0x7D
			if (!ok && obj.testStatus((unsigned)st.hordeMember))
			{
				ok = false; // RW 0x8661D8: main slot 0x50 (RW 0x7FEAC1: false for every contain class)
			}
		}
		if (!ok)
		{
			return false;
		}
	}
	return true;
}

bool OpenContain::isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const
{
	(void)checkCapacity;
	(void)checkPath;
	return openContainAllows(obj);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// entering
// ---------------------------------------------------------------------------------------------------------------------------------

bool OpenContain::addToContain(Object *obj)
{
	if (!obj || obj->getContainedBy())
	{
		return false; // RW 0x8674E0: an object in a container is not added
	}
	openContainAdd(obj);
	return true;
}

// RW 0x8674C2 OpenContain::addToContain
void OpenContain::openContainAdd(Object *obj)
{
	// RW 0x8674D3: wasSelected (the drawable's selection, client side: false in the logic)
	if (obj->getContainedBy())
	{
		return;
	}
	addToContainList(obj);
	if (const Player *p = obj->getControllingPlayer())
	{
		m_playerEnteredMask = 1u << ((unsigned)p->getPlayerIndex() & 31u); // RW 0x8674FA: interface + 0x58
	}
	onContaining(obj, false); // the container's contain slot 0x58
	redeployOccupants();      // main slot 0x48
	objectOnContainedBy(obj); // RW 0x6901AE
	recalcApparentControllingPlayer(); // main slot 0x54 (OpenContain RW 0x8658A3: the sound once per frame, client side)
	// RW 0x86753D: with ModifierToGiveOnExit the {id, frame} pair the exit reads is recorded: not ported (S-1100)
	if (isEnclosingContainerFor(*obj))
	{
		addOrRemoveObjFromWorld(obj, false); // RW 0x867566: main slot 0x60(obj, false)
	}
	++m_openStats.entered;
}

// RW 0x866987
void OpenContain::addToContainList(Object *obj)
{
	static const int stealthGarrison = CombatNames::kindOf("STEALTH_GARRISON");
	m_contained.push_back(obj);
	if (obj->isKindOf((unsigned)stealthGarrison))
	{
		++m_stealthUnitsContained;
	}
}

// RW 0x6901AE Object::onContainedBy
void OpenContain::objectOnContainedBy(Object *rider)
{
	static const int mine = CombatNames::kindOf("MINE");
	static const int unselectable = statusBit("UNSELECTABLE");
	if (!rider->isKindOf((unsigned)mine))
	{
		setStatusMask(*rider, statusMaskFor(rider), true); // RW 0x6901FC: the contain's slot 0xB0 mask, set
	}
	else
	{
		rider->setStatus((unsigned)unselectable, false);
	}
	rider->friend_setContainedBy(getObject());
	rider->friend_setContainedFrame(rider->logic().getFrame());
}

// RW 0x69024C Object::onRemovedFrom
void OpenContain::objectOnRemovedFrom(Object *rider)
{
	setStatusMask(*rider, statusMaskFor(rider), false);
	rider->friend_setContainedBy(nullptr);
	rider->friend_setContainedFrame(0);
}

void OpenContain::onContaining(Object *obj, bool wasSelected)
{
	(void)obj;
	(void)wasSelected; // RW 0x865F9A: the EnterSound (client side)
}

void OpenContain::onRemoving(Object *obj)
{
	// RW 0x8680F4: the ExitSound (client side), the rider's bone records leave (RW 0x867663 / 0x866E08), ModifierToGiveOnExit (S-1100)
	m_riderBoneIndex.erase(obj->getID());
	m_riderBoneName.erase(obj->getID());
}

int OpenContain::getContainMax() const
{
	return openData().m_containMax; // RW 0x86584C: data + 0x70
}

std::string OpenContain::riderBone(ObjectID id) const
{
	auto it = m_riderBoneName.find(id);
	return it == m_riderBoneName.end() ? std::string() : it->second;
}

// RW 0x8671F5: the contained objects (a copy) through main slot 0x68
void OpenContain::redeployOccupants()
{
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	putRidersAtBones(riders);
}

// RW 0x868A70
void OpenContain::putRidersAtBones(const std::vector<Object *> &riders)
{
	enum { MAX_BONES = 32 }; // RW 0x868A9E: 0x20 transforms
	static const int riderIsPilot = statusBit("RIDER_IS_PILOT");
	static const int canAttack = statusBit("CAN_ATTACK");
	static const int noAttack = statusBit("NO_ATTACK");
	Object *container = getObject();
	ProjectileLaunchOffsets *provider = container->logic().combat().launchOffsets();
	const ObjectStatusMaskType &osoc = openData().m_objectStatusOfContained.mask;
	const bool ridersMayFire = maskTest(osoc, canAttack) && !ridersFireFromContainer();
	std::string lastPrefix;
	bool numbered = true; // RW - 0x11: cleared once a prefix answers with its single bone, never set again
	int count = 0;
	float bones[MAX_BONES][12];
	for (Object *rider : riders)
	{
		if (rider->testStatus((unsigned)riderIsPilot))
		{
			continue; // RW 0x868ADC: status 0x28
		}
		// RW 0x86630B: the first PassengerBonePrefix entry whose KindOf the rider has all of (RW 0x70C4FE), "ARROW_" without one
		std::string prefix = "ARROW_";
		for (const PassengerBonePrefixEntry &e : openData().m_passengerBonePrefix)
		{
			bool all = true;
			const KindOfMaskType &k = rider->getKindOf();
			for (size_t w = 0; w < k.size(); ++w)
			{
				if ((k[w] & e.kindOf[w]) != e.kindOf[w])
				{
					all = false;
				}
			}
			if (all)
			{
				prefix = e.bonePrefix;
				break;
			}
		}
		if (prefix != lastPrefix)
		{
			// RW 0x868B2B: RW 0x68C650(prefix, 0x20, the current condition, start 1); none: the single bone RW 0x68C5AF
			count = provider ? provider->multiLogicalBones(*container, prefix, container->getModelConditionBits(), MAX_BONES, bones) : 0;
			if (count < 0)
			{
				count = 0;
			}
			lastPrefix = prefix;
			if (count == 0 && provider && provider->singleLogicalBone(*container, prefix, bones[0]))
			{
				count = 1;
				numbered = false;
			}
		}
		const ObjectID id = rider->getID();
		int index = -1;
		auto known = m_riderBoneIndex.find(id);
		if (known == m_riderBoneIndex.end())
		{
			// RW 0x868BB8 .. 0x868C41: the first bone whose name no rider holds (the name always "<prefix>0<n>" when numbered here)
			int i = 0;
			for (; i < count; ++i)
			{
				std::string name = prefix;
				if (numbered)
				{
					name += "0" + std::to_string(i + 1);
				}
				bool used = false;
				for (const auto &kv : m_riderBoneName)
				{
					used = used || kv.second == name;
				}
				if (!used)
				{
					break;
				}
			}
			if (count >= 1 && i >= count)
			{
				index = -1; // every bone taken: no bone (RW 0x868C2B -> 0x868D4D)
			}
			else
			{
				index = i;
				m_riderBoneIndex[id] = i; // RW 0x868C41: RW 0x7871FC operator[]
			}
		}
		else if (known->second < count)
		{
			index = known->second;
		}
		// INFERENCE (S-1103): with no bone known at all (count 0: no launch bone provider, a game without the W3D assets) RW reads an unset transform; the port
		// treats the rider as one without a bone
		const bool onBone = index >= 0 && count > 0;
		float basis[9];
		Coord3D pos;
		const float *cb = container->getBasis();
		const Coord3D &cp = *container->getPosition();
		bool hidden;
		if (!onBone)
		{
			// RW 0x868D4D: NO_ATTACK in a container whose riders may fire, the container's transform, hidden (main slot 0x6C(rider, 1))
			if (ridersMayFire)
			{
				rider->setStatus((unsigned)noAttack, true);
			}
			for (int k = 0; k < 9; ++k)
			{
				basis[k] = cb[k];
			}
			pos = cp;
			hidden = true;
		}
		else
		{
			if (ridersMayFire)
			{
				rider->setStatus((unsigned)noAttack, false); // RW 0x868C7F
			}
			std::string name = prefix;
			if (numbered)
			{
				char n[16];
				// RW 0x868CC4 .. 0x868CE5: itoa(index + 1), then a '0' in front while the zero-based INDEX is below 10 (the tenth name is "010", the eleventh "11")
				std::snprintf(n, sizeof n, index < 10 ? "0%d" : "%d", index + 1);
				name += n;
			}
			m_riderBoneName[id] = name; // RW 0x868CD7: RW 0x8689EC
			if (openData().m_passengersInTurret)
			{
				++m_turretBonesUnported; // RW 0x868D2B: the turret's bone (RW 0x68E807, S-360): the plain bone stands in
			}
			else
			{
				applyBoneSpecificConditionState(*rider, numbered ? index + 1 : 0); // lane COMBAT-3: RW 0x868F17 .. 0x868F4B
			}
			// the bone through the container's transform (RW 0x68C650 answers world transforms): R = C . B, t = C . b + c
			const float *b = bones[index];
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 3; ++c)
				{
					basis[r * 3 + c] = SimMath::addf32(SimMath::addf32(SimMath::mulf32(cb[r * 3 + 0], b[0 * 4 + c]), SimMath::mulf32(cb[r * 3 + 1], b[1 * 4 + c])),
						SimMath::mulf32(cb[r * 3 + 2], b[2 * 4 + c]));
				}
			}
			const float tt[3] = { cp.x, cp.y, cp.z };
			float w[3];
			for (int r = 0; r < 3; ++r)
			{
				const float v = SimMath::addf32(SimMath::mulf32(cb[r * 3 + 2], b[11]), SimMath::mulf32(cb[r * 3 + 1], b[7]));
				w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(cb[r * 3 + 0], b[3])), tt[r]);
			}
			pos = Coord3D{ w[0], w[1], w[2] };
			hidden = ridersFireFromContainer(); // RW 0x868D3B: main slot 0x6C(rider, ENCLOSED)
		}
		onRiderPlaced(rider, hidden);
		// RW 0x868F72: a rider of a container that is not ENCLOSED takes the whole transform when main slot 0x38 (ForceOrientationContainer) says so, else the position
		if (!ridersFireFromContainer() && forceOrientationContainer())
		{
			rider->setTransform(&pos, basis);
		}
		else
		{
			rider->setPosition(&pos);
		}
	}
}

// RW 0x868F0B .. 0x868F4B (lane COMBAT-3): a rider on a bone loses PASSENGER_VARIATION_1 .. 5 (RW 0x4B5AA9(0, 0xC2 .. 0xC6), RW 0x5E3B79) and takes the
// BoneSpecificConditionState flags (data + 0x4C, RW 0x603AF6) of its bone's number (RW 0x68C650's sixth argument: RW 0x4C3731 stores the number of the name
// "<prefix>NN", 0 for the plain name; the port's bone walk stops at the first miss, so the number is the index + 1), set by RW 0x5E3BA5. Grond's trolls get
// PASSENGER_VARIATION_1 .. 4 this way and pick their push / pull animations by it.
void OpenContain::applyBoneSpecificConditionState(Object &rider, unsigned boneNumber)
{
	static const int first = CombatNames::modelCondition("PASSENGER_VARIATION_1");
	Object::ModelConditionBits clear{};
	for (int b = first; b < first + 5; ++b)
	{
		clear[(size_t)b >> 5] |= 1u << (b & 31);
	}
	Object::ModelConditionBits set{};
	const auto &map = openData().m_boneSpecificConditionState;
	auto it = map.find(boneNumber);
	if (it != map.end())
	{
		for (size_t w = 0; w < set.size(); ++w)
		{
			set[w] = it->second[w];
		}
	}
	rider.clearAndSetModelConditionFlags(clear, set);
}

// RW 0x865BA6
void OpenContain::monitorConditionChanges()
{
	const Object::ModelConditionBits &now = getObject()->getModelConditionBits(); // the drawable's words (the drawable mirrors the object's)
	bool same = true;
	for (size_t i = 0; i < now.size(); ++i)
	{
		same = same && now[i] == m_watchedConditions[i];
	}
	if (!same)
	{
		redeployOccupants(); // main slot 0x48
		for (size_t i = 0; i < now.size(); ++i)
		{
			m_watchedConditions[i] = now[i];
		}
	}
}

// RW 0x86708F OpenContain::onDelete: every rider leaves (contain slot 0xA4(rider, true)), loses its container, and a HERO is killed, any other rider destroyed
void OpenContain::onDelete()
{
	static const int hero = CombatNames::kindOf("HERO");
	m_deleting = true; // RW 0x867096: + 0xDF
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	for (Object *r : riders)
	{
		removeFromContain(r);
		r->friend_setContainedBy(nullptr);
		if (r->isKindOf((unsigned)hero))
		{
			r->kill(0); // RW 0x698EC3(8, 0)
		}
		else
		{
			r->logic().destroyObject(r); // RW 0x62BBAB
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// leaving
// ---------------------------------------------------------------------------------------------------------------------------------

void OpenContain::removeFromContain(Object *obj)
{
	if (obj)
	{
		openContainRemove(obj);
	}
}

// RW 0x8665BE -> RW 0x865EB6
void OpenContain::openContainRemove(Object *obj)
{
	if (!isContainedHere(*obj))
	{
		// RW 0x8665D6 (contain slot 0xE8): "OpenContain::removeFromContain: The object is not in the container" (a debug message): nothing happens
		return;
	}
	static const int noAttack = statusBit("NO_ATTACK");
	static const int porterTagged = statusBit("PORTER_TAGGED");
	static const int tagged = statusBit("TAGGED");
	static const int stealthGarrison = CombatNames::kindOf("STEALTH_GARRISON");
	static const int canAttack = statusBit("CAN_ATTACK");
	static const int enclosed = statusBit("ENCLOSED");
	const ObjectStatusMaskType &osoc = statusMaskFor(obj);
	if (maskTest(osoc, canAttack) && !maskTest(osoc, enclosed))
	{
		obj->setStatus((unsigned)noAttack, false); // RW 0x8665FF
	}
	// RW 0x865EB6
	if (obj->testStatus((unsigned)porterTagged))
	{
		obj->setStatus((unsigned)porterTagged, false);
	}
	removeFromContainList(obj); // main slot 0x34 (RW 0x865E78)
	if (obj->isKindOf((unsigned)stealthGarrison) && m_stealthUnitsContained > 0)
	{
		--m_stealthUnitsContained; // the exposed stealth units' detection (RW 0x865EE7 .. 0x865EFB) is not ported (S-1100)
	}
	obj->setStatus((unsigned)tagged, false); // RW 0x865F05
	if (isEnclosingContainerFor(*obj))
	{
		addOrRemoveObjFromWorld(obj, true); // RW 0x865F23
	}
	// RW 0x865F28 .. 0x865F37: an object that is not effectively dead gets its owner's team again (RW 0x68BBE0 / 0x68BB9D: its own team, unchanged in the port)
	recalcApparentControllingPlayer(); // main slot 0x58 (OpenContain RW 0x865932: the exit sound once per frame, client side)
	onRemoving(obj);                   // the container's contain slot 0x5C
	objectOnRemovedFrom(obj);          // RW 0x69024C
	++m_openStats.left;
}

// RW 0x866675
void OpenContain::removeAllContained()
{
	while (!m_contained.empty())
	{
		Object *r = m_contained.front();
		removeFromContain(r);
		if (!m_contained.empty() && m_contained.front() == r)
		{
			openContainRemove(r); // a derived removal that took another path: the list still holds it
		}
		++m_openStats.ejectedOnDeath;
	}
}

// RW 0x865D3D OpenContain::addOrRemoveObjFromWorld
void OpenContain::addOrRemoveObjFromWorld(Object *obj, bool add)
{
	GameLogic &logic = obj->logic();
	if (!add)
	{
		// RW 0x865D47: RW 0x68C6EE (the object's AI / locomotor stop, not ported), out of the world, the drawable hidden, the radar (not ported)
		if (obj->isInWorld())
		{
			logic.friend_containLeaveWorld(*obj);
		}
		obj->setDrawableHidden(true);
	}
	else
	{
		// RW 0x865D7B: RW 0x696E63 teleports it to the container (setPosition; the recorded transform and the AI's notification RW 0x5E821A / 0x66276B are not
		// ported), into the world, the drawable shown
		obj->setPosition(getObject()->getPosition());
		if (!obj->isInWorld())
		{
			logic.friend_containEnterWorld(*obj);
		}
		obj->setDrawableHidden(false);
	}
	// RW 0x865DB7 .. 0x865DF5: the object's own riders follow when this contain encloses them
	if (ContainModuleInterface *c = obj->getContain())
	{
		if (const ContainedItemsList *items = c->getContainedItemsList())
		{
			const std::vector<Object *> riders(items->begin(), items->end());
			for (Object *r : riders)
			{
				if (isEnclosingContainerFor(*r))
				{
					addOrRemoveObjFromWorld(r, add);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// death, exit
// ---------------------------------------------------------------------------------------------------------------------------------

// RW 0x867120 OpenContain::onDie (the die interface)
void OpenContain::onDie(const DieModuleInterface::Event &event)
{
	m_dying = true; // RW 0x86713A
	Object *container = getObject();
	if (!m_dieMux->isDieApplicable(*container, event))
	{
		return;
	}
	if (openData().m_ejectPassengersOnDeath)
	{
		if (openData().m_damagePercentToUnits > 0.0f)
		{
			processDamageToContained(); // RW 0x867182: contain slot 0x148
		}
		killRidersWhoAreNotFreeToExit(); // main slot 0x64 (OpenContain RW 0x63F3BF: nothing)
		removeAllContained();            // slot 0xA8(false)
		return;
	}
	// RW 0x86718A .. 0x8671D9: every rider is hidden and destroyed
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	for (Object *r : riders)
	{
		r->setDrawableHidden(true);
		container->logic().destroyObject(r);
		++m_openStats.ridersDestroyedOnDeath;
	}
}

// RW 0x867242: the riders (contain slot 0x118) and then the crew (slot 0x11C, SiegeEngineContain's), each through RW 0x866B69, each list copied first
void OpenContain::processDamageToContained()
{
	for (int pass = 0; pass < 2; ++pass)
	{
		const ContainedItemsList *list = pass == 0 ? &m_contained : crewList();
		if (!list || list->empty())
		{
			continue;
		}
		const std::vector<Object *> riders(list->begin(), list->end());
		for (Object *r : riders)
		{
			processDamageToRider(r);
		}
	}
}

// RW 0x866B69
void OpenContain::processDamageToRider(Object *rider)
{
	static const int tree = CombatNames::kindOf("TREE");
	static const int rock = CombatNames::kindOf("ROCK");
	Object *container = getObject();
	GameLogic &logic = container->logic();
	if (rider->isKindOf((unsigned)tree) || rider->isKindOf((unsigned)rock))
	{
		// RW 0x866B88 (RW 0x46E72F(0, 0x5E, 0x61): KindOf TREE / ROCK) .. 0x866C08: a held tree or rock is hidden and destroyed
		rider->setDrawableHidden(true);
		logic.destroyObject(rider);
		++m_openStats.ridersDestroyedOnDeath;
		return;
	}
	if (!openData().m_killPassengersOnDeath)
	{
		// RW 0x866BB2 .. 0x866C02: UNRESISTABLE damage of MaxHealth x DamagePercentToUnits from the container, death type 3; 100 % kills what is left
		BodyModuleInterface *body = rider->getBodyModule();
		if (!body)
		{
			return;
		}
		DamageInfo info;
		info.m_input.m_sourceID = container->getID();
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_deathType = 3;
		info.m_input.m_amount = (float)SimMath::mulD((double)body->getMaxHealth(), (double)openData().m_damagePercentToUnits); // x87 product, stored as float
		rider->attemptDamage(info);
		if (!rider->isDestroyed() && openData().m_damagePercentToUnits == 1.0f)
		{
			rider->kill(0); // RW 0x698EC3(8, 0)
		}
		return;
	}
	// RW 0x866C0C .. 0x866CD6: KillPassengersOnDeath: out of the contain, flung (a rider with a PhysicsBehavior: GameLogicRandomValue(2, 5) x its facing in x / y and up),
	// killed, and the kill is the container's last damager's
	removeFromContain(rider); // contain slot 0xA4(rider, false)
	if (PhysicsBehavior *phys = PhysicsBehavior::find(*rider))
	{
		const float k = (float)logic.random().getValue(2, 5, kOpenContainCpp, 0x8DE);
		const float *m = rider->getBasis();
		phys->fling(Coord3D{ SimMath::mulf32(m[0], k), SimMath::mulf32(m[3], k), k });
	}
	rider->kill(0);
	if (const ActiveBody *ab = dynamic_cast<const ActiveBody *>(container->getBodyModule()))
	{
		if (Object *killer = logic.findObjectByID(ab->lastDamager()))
		{
			killer->scoreTheKill(*rider, 1); // RW 0x6955BC(rider, 1)
		}
	}
	++m_openStats.ridersDestroyedOnDeath;
}

// RW 0x867D6A: every rider with an AI gets aiExit (RW 0x7716C1) with the container
void OpenContain::orderAllPassengersToExit(int source)
{
	const std::vector<Object *> riders(m_contained.begin(), m_contained.end());
	for (Object *r : riders)
	{
		if (AIUpdateInterface *ai = r->getAIUpdateInterface())
		{
			ai->aiExit(getObject(), (CommandSourceType)source);
		}
	}
}

void OpenContain::onObjectWantsToEnterOrExit(Object *obj, int wants)
{
	if (!obj)
	{
		return;
	}
	if (wants == 2)
	{
		m_wanters.erase(obj->getID()); // ZH OpenContain::onObjectWantsToEnterOrExit: WANTS_NEITHER erases
	}
	else
	{
		m_wanters[obj->getID()] = wants;
	}
}

// RW 0x86702D OpenContain::update: the entering player's mask clears, the condition watch (main slot 0x44), the door timer (+ 0x60: the DOOR_1_OPENING -> DOOR_1_CLOSING conditions), the wanters whose objects are gone leave (RW 0x866B17),
// RW 0x8669B1's held-rider kill timer (+ 0xE4 / + 0xEC: written by the troll's grab, not ported: never armed); every frame
UpdateSleepTime OpenContain::update()
{
	m_playerEnteredMask = 0;   // RW 0x867030: + 0x68
	monitorConditionChanges(); // main slot 0x44
	if (m_doorOpenFrames != 0 && --m_doorOpenFrames == 0)
	{
		static const int opening = CombatNames::modelCondition("DOOR_1_OPENING");
		static const int closing = CombatNames::modelCondition("DOOR_1_CLOSING");
		Object *container = getObject();
		if (container->testModelCondition(opening) || !container->testModelCondition(closing))
		{
			container->setModelConditionState(opening, false);
			container->setModelConditionState(closing, true);
		}
	}
	if (!m_wanters.empty())
	{
		GameLogic &logic = getObject()->logic();
		for (auto it = m_wanters.begin(); it != m_wanters.end();)
		{
			const Object *o = logic.findObjectByID(it->first);
			it = (!o || o->isDestroyed()) ? m_wanters.erase(it) : std::next(it);
		}
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8682D9 OpenContain::exitObjectViaDoor
void OpenContain::exitObjectViaDoor(Object *obj, int door)
{
	(void)door;
	if (!obj)
	{
		return;
	}
	Object *container = getObject();
	removeFromContain(obj); // RW 0x8682EF: contain slot 0xA4(obj, false)
	// RW 0x8682F8 .. 0x868323: DoorOpenTime (data + 0x78) arms the door timer and shows DOOR_1_OPENING (not DOOR_1_CLOSING)
	m_doorOpenFrames = openData().m_doorOpenTime;
	if (m_doorOpenFrames != 0)
	{
		static const int opening = CombatNames::modelCondition("DOOR_1_OPENING");
		static const int closing = CombatNames::modelCondition("DOOR_1_CLOSING");
		if (container->testModelCondition(closing) || !container->testModelCondition(opening))
		{
			container->setModelConditionState(closing, false);
			container->setModelConditionState(opening, true);
		}
	}
	++m_openStats.exitedViaDoor;
	const int paths = openData().m_numberOfExitPaths; // data + 0x74
	if (paths < 1)
	{
		// RW 0x868334 .. 0x86852B: the SHIP branch (a landing position on the shore, RW 0x6EFBB8) is not ported (S-1103); the rider stays where removeFromContain put it
		static const int ship = CombatNames::kindOf("SHIP");
		if (container->isKindOf((unsigned)ship))
		{
			++m_shipExitsUnported;
		}
		return; // RW 0x86852B: RW 0x6E85E9 (the pathfinder's unit map) follows the object's position in the port
	}
	// RW 0x868538 .. 0x8685E3: the bones ExitStart / ExitEnd, numbered (two digits) when there are several paths, the paths taken in turn
	std::string startBone = "ExitStart", endBone = "ExitEnd";
	if (paths > 1)
	{
		char n[8];
		std::snprintf(n, sizeof n, "%02d", m_nextExitPath);
		startBone += n;
		endBone += n;
		m_nextExitPath = m_nextExitPath % paths + 1;
	}
	Coord3D start = *container->getPosition(), end = *container->getPosition();
	boneWorldPosition(*container, startBone, start);
	boneWorldPosition(*container, endBone, end);
	obj->setPosition(&start);                           // RW 0x868600: RW 0x70C201
	obj->setOrientation(container->getOrientation());   // RW 0x86860B: RW 0x70C31E
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai)
	{
		// RW 0x868637 .. 0x8686D5: the container is no obstacle for it (RW 0x66831A), the move goes to the end bone, then to the rally point when one is set (exit
		// interface + 0xAC, not written by the ported contains), as an exit production path (RW 0x77113C, CMD_FROM_AI); the destination adjustment RW 0x6F3C87 is
		// S-1100's
		std::vector<Coord3D> path;
		path.push_back(end);
		path.push_back(end);
		ai->aiFollowPath(path, container, CMD_FROM_AI, true);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
void OpenContain::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_contained.size());
	for (const Object *o : m_contained)
	{
		h.addU32(o->getID());
	}
	h.addU32(m_stealthUnitsContained);
	h.addU32(m_playerEnteredMask);
	h.addU32((std::uint32_t)m_wanters.size());
	for (const auto &kv : m_wanters)
	{
		h.addU32(kv.first);
		h.addI32(kv.second);
	}
	h.addBool(m_enabled);
	h.addBool(m_dying);
	h.addBool(m_killingRiders);
	h.addI32(m_nextExitPath);
	h.addU32(m_doorOpenFrames);
	h.addBool(m_deleting);
	h.addU32((std::uint32_t)m_riderBoneIndex.size());
	for (const auto &kv : m_riderBoneIndex)
	{
		h.addU32(kv.first);
		h.addI32(kv.second);
	}
	h.addU32((std::uint32_t)m_riderBoneName.size());
	for (const auto &kv : m_riderBoneName)
	{
		h.addU32(kv.first);
		h.addString(kv.second);
	}
	for (std::uint32_t w : m_watchedConditions)
	{
		h.addU32(w);
	}
}

bool OpenContain::getEntryPosition(Coord3D &out) const
{
	// RW 0x8657D1
	const Object *obj = getObject();
	if (isShipOnWater(*obj))
	{
		++m_shipEntriesUnported; // RW 0x6EFBB8's shore spot (S-1104); a ship on dry ground: its position (RW 0x6EFBB8's first branch)
	}
	out = *obj->getPosition();
	return true;
}

bool OpenContain::isShipOnWater(const Object &container)
{
	// the SHIP test of RW 0x8657D1 / 0x86A2D0 and RW 0x6EFBB8's: TheTerrainLogic slot 0x4C (isUnderwater) at the container's position
	static const int kShip = CombatNames::kindOf("SHIP");
	if (!container.isKindOf((unsigned)kShip))
	{
		return false;
	}
	AIWorld *ai = container.logic().aiWorld();
	const PathfindTerrain *terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	if (!terrain)
	{
		throw std::logic_error("OpenContain: a SHIP's water test without a map terrain");
	}
	const Coord3D *p = container.getPosition();
	return terrain->isUnderwater(p->x, p->y, nullptr, nullptr);
}

bool OpenContain::validMovementTerrain(AIUpdateInterface &ai, const Coord3D &at)
{
	// RW 0x6E8707: the cell of floor(x * 0.1), floor(y * 0.1) (RW 0xBD83D4, x87 at 24 bits) on the layer given (the callers pass 1, the ground)
	Pathfinder &pf = ai.world().pathfinder();
	const PathfindCell *cell = pf.getCell(LAYER_GROUND, SimMath::floorToInt(NumericState::pc24Mul(at.x, 0.1f)), SimMath::floorToInt(NumericState::pc24Mul(at.y, 0.1f)));
	if (!cell)
	{
		return false;
	}
	const PathfindCell::CellType type = cell->getType();
	if (type == PathfindCell::CELL_OBSTACLE || type == PathfindCell::CELL_BRIDGE_IMPASSABLE)
	{
		return true; // RW 0x6E8758: types 4 and 5 (ZH: an obstacle or impassable cell under a transport lets the rider out)
	}
	// RW 0x6E8762: a clear cell whose bits 4..9 are not 0x10 lets every rider out; the port's cell keeps no such field (its occupants are lists): a clear cell takes
	// the surface test, which every ground or air locomotor passes on a clear cell (table entry 0x09)
	return (Pathfinder::validLocomotorSurfacesForCellType(type) & ai.curLocomotor()->getTemplate().m_surfaces) != 0u;
}

bool OpenContain::getEntryOffset(Coord3D &out) const
{
	// RW 0x8657B6: a SHIP asks slot 0x158, any other container answers its position
	return getEntryPosition(out);
}

bool OpenContain::getExitOffset(Coord3D &out) const
{
	out = *getObject()->getPosition(); // RW 0x867ACD
	return true;
}
