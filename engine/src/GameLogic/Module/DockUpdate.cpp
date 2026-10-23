// OpenBFME. GPL-3.0.
// See GameLogic/Module/DockUpdate.h for the target facts.

#include "GameLogic/Module/DockUpdate.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/Science.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Economy.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <cstddef>
#include <stdexcept>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
const float kApproachMaxRadius = 100.0f; // RW 0xBD88D8
const float kHalf = 0.5f;                // RW 0xBD869C

// RW 0x73AFE3 INI::parseScience: the next token through TheScienceStore (RW 0x73A386); no store: the plain int throw 0xDEAD0001
void parseScience(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken();
	if (!TheScienceStore)
	{
		throw INIPlainIntError("TheScienceStore==NULL"); // RW 0x73AFFA
	}
	*static_cast<int *>(store) = TheScienceStore->friend_lookupScience(token);
}

#define DU_OFF(member) (int)offsetof(DockUpdateModuleData, member)
// RW 0xC87674
const FieldParse kDockUpdateFieldParse[] = {
	{ "NumberApproachPositions", INI::parseInt, nullptr, DU_OFF(m_numberApproachPositions) },
	{ "AllowsPassthrough", INI::parseBool, nullptr, DU_OFF(m_isAllowPassthrough) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DU_OFF
#define SC_OFF(member) (int)offsetof(SupplyCenterDockUpdateModuleData, member)
// RW 0xC68478
const FieldParse kSupplyCenterDockUpdateFieldParse[] = {
	{ "ValueMultiplier", INI::parseReal, nullptr, SC_OFF(m_valueMultiplier) },
	{ "BonusScience", parseScience, nullptr, SC_OFF(m_bonusScience) },
	{ "BonusScienceMultiplier", INI::parsePercentToReal, nullptr, SC_OFF(m_bonusScienceMultiplier) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SC_OFF

Object::ModelConditionBits bitsOf(std::initializer_list<const char *> names)
{
	Object::ModelConditionBits b{};
	for (const char *n : names)
	{
		const int bit = CombatNames::modelCondition(n);
		b[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	return b;
}

bool kindOf(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

// RW 0x68BEED: the object's current locomotor has the AIR surface
bool usesAirborneLocomotor(const Object &obj)
{
	const AIUpdateInterface *ai = obj.getAIUpdateInterface();
	return ai && ai->curLocomotor() && (ai->curLocomotor()->getTemplate().m_surfaces & (unsigned)LOCOMOTORSURFACE_AIR);
}

bool isZero(const Coord3D &c)
{
	return c.x == 0.0f && c.y == 0.0f && c.z == 0.0f;
}
} // namespace

void DockUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDockUpdateFieldParse);
}

void SupplyCenterDockUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	DockUpdateModuleData::buildFieldParse(p); // RW 0x8A4478: the base table first
	p.add(kSupplyCenterDockUpdateFieldParse);
}

std::string DockUpdate::stopLine()
{
	return "[S-5201] DockUpdate (lane LUMBER1, RW 0x99187C): the dock bones (DockStart / DockAction / DockEnd / DockWaiting) are the drawable's pristine "
		   "bones (S-209's), not loaded by the port: every dock position is the dock object's own (exact for RotWK's lumber mills, whose model has no "
		   "Dock* bones and whose NumberApproachPositions is -1); a dock with a positive NumberApproachPositions is reported";
}

// RW 0x99230C
DockUpdate::DockUpdate(Thing *thing, const DockUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_baseData(data)
{
	m_numberApproachPositions = data->m_numberApproachPositions;
	const size_t n = m_numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG ? (size_t)DEFAULT_APPROACH_VECTOR_SIZE
	                                                                          : (m_numberApproachPositions > 0 ? (size_t)m_numberApproachPositions : 0u);
	m_approachPositions.assign(n, Coord3D{ 0.0f, 0.0f, 0.0f });
	m_approachPositionOwners.assign(n, INVALID_ID);
	m_approachPositionReached.assign(n, false);
	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine()); // the dock positions are the dock object's own (no bones)
	}
}

// RW 0x99187C. The bones are the client's (S-5201): none is found, the positions stay (0, 0, 0)
void DockUpdate::loadDockPositions()
{
	if (m_numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG)
	{
		m_numberApproachPositionBones = 0;
	}
	else
	{
		m_numberApproachPositionBones = 0; // DockWaiting bones: none (S-5201)
	}
	m_positionsLoaded = true;
}

// Object RW 0x70BCE7 convertBonePosToWorldPos: a zero local point is the object's position (the only form the port meets, S-5201)
Coord3D DockUpdate::boneToWorld(const Coord3D &) const
{
	return *getObject()->getPosition();
}

// RW 0x991BFE
Coord3D DockUpdate::computeApproachPosition(int index, const Object &docker)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	Object *us = getObject();
	Coord3D working = boneToWorld(m_approachPositions[(size_t)index]);
	if (m_numberApproachPositionBones == 0)
	{
		const Coord3D &dp = *docker.getPosition();
		const Coord3D &up = *us->getPosition();
		float dx = SimMath::sseSub(dp.x, up.x), dy = SimMath::sseSub(dp.y, up.y), dz = SimMath::sseSub(dp.z, up.z);
		const float len2 = SimMath::sseAdd(SimMath::sseAdd(SimMath::sseMul(dz, dz), SimMath::sseMul(dy, dy)), SimMath::sseMul(dx, dx));
		if (len2 != 0.0f)
		{
			const float inv = BFME2_Inverse_Sqrt(len2); // RW 0x441C56, the products x87 PC24
			dx = BFME2_Mul24(dx, inv);
			dy = BFME2_Mul24(dy, inv);
			dz = BFME2_Mul24(inv, dz);
		}
		const float s = SimMath::sseMul(CombatQueries::boundingCircleRadius(*us), kHalf); // us + 0xB8 * 0.5
		working.x = SimMath::sseAdd(SimMath::sseMul(s, dx), working.x);
		working.y = SimMath::sseAdd(SimMath::sseMul(s, dy), working.y);
		working.z = SimMath::sseAdd(SimMath::sseMul(s, dz), working.z);
	}
	FindPositionOptions options;
	options.minRadius = 0.0f;
	options.maxRadius = kApproachMaxRadius;
	options.startAngle = RANDOM_START_ANGLE;
	options.maxZDelta = 1e10f; // RW 0xBF7328
	options.ignoreObject = usesAirborneLocomotor(docker) ? us : nullptr;
	options.sourceToPathToDest = &docker;
	Coord3D best;
	if (FindPosition::findPositionAround(us->logic(), working, options, best))
	{
		return best;
	}
	return working;
}

// RW 0x99175A
bool DockUpdate::isClearToApproach(const Object &docker) const
{
	if (m_numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG)
	{
		return true;
	}
	for (ObjectID id : m_approachPositionOwners)
	{
		if (id == INVALID_ID || id == docker.getID())
		{
			return true;
		}
	}
	return false;
}

// RW 0x99221D
bool DockUpdate::reserveApproachPosition(Object &docker, Coord3D *pos, int *index)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	if (!pos)
	{
		return false;
	}
	int found = -1;
	for (size_t i = 0; i < m_approachPositionOwners.size(); ++i)
	{
		if (m_approachPositionOwners[i] == docker.getID())
		{
			found = (int)i;
			break;
		}
		if (m_approachPositionOwners[i] == INVALID_ID)
		{
			found = (int)i;
			m_approachPositionOwners[i] = docker.getID();
			break;
		}
	}
	if (found < 0)
	{
		if (m_numberApproachPositions != DYNAMIC_APPROACH_VECTOR_FLAG)
		{
			return false;
		}
		m_approachPositions.push_back(Coord3D{ 0.0f, 0.0f, 0.0f });
		m_approachPositionOwners.push_back(INVALID_ID);
		m_approachPositionReached.push_back(false);
		loadDockPositions();
		found = (int)m_approachPositionOwners.size() - 1;
		m_approachPositionOwners[(size_t)found] = docker.getID();
	}
	*pos = computeApproachPosition(found, docker);
	*index = found;
	return true;
}

// RW 0x991DBF
bool DockUpdate::advanceApproachPosition(Object &docker, Coord3D *pos, int *index)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	if (!pos)
	{
		return false;
	}
	const int idx = *index;
	if (idx < 1 || m_approachPositionOwners[(size_t)(idx - 1)] != INVALID_ID)
	{
		return false;
	}
	m_approachPositionOwners[(size_t)(idx - 1)] = docker.getID();
	m_approachPositionReached[(size_t)(idx - 1)] = false;
	m_approachPositionOwners[(size_t)idx] = INVALID_ID;
	m_approachPositionReached[(size_t)idx] = false;
	*pos = computeApproachPosition(idx - 1, docker);
	*index = idx - 1;
	return true;
}

// RW 0x991715
bool DockUpdate::isClearToEnter(const Object &docker) const
{
	return docker.getID() == m_activeDocker;
}

// RW 0x9919A0
bool DockUpdate::isClearToAdvance(const Object &docker, int index) const
{
	if (index < 0 || (size_t)index >= m_approachPositionOwners.size())
	{
		return false; // RW: idx < 0; an index past the vectors (a queue the removal shifted) reads nothing here
	}
	return m_approachPositionOwners[(size_t)index] == docker.getID() && m_approachPositionReached[(size_t)index] && index > 0 &&
	       m_approachPositionOwners[(size_t)(index - 1)] == INVALID_ID;
}

// RW 0x9919F3
void DockUpdate::getEnterPosition(Object &docker, Coord3D *pos)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	if (!pos)
	{
		return;
	}
	if (isZero(m_enterPosition))
	{
		*pos = usesAirborneLocomotor(docker) ? *getObject()->getPosition() : *docker.getPosition();
		return;
	}
	*pos = boneToWorld(m_enterPosition);
}

// RW 0x991A6C (the zero test reads the ENTER position, as retail)
void DockUpdate::getDockPosition(Object &docker, Coord3D *pos)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	if (!pos)
	{
		return;
	}
	*pos = isZero(m_enterPosition) ? *docker.getPosition() : boneToWorld(m_dockPosition);
}

// RW 0x991ACF (the zero test reads the ENTER position, as retail)
void DockUpdate::getExitPosition(Object &docker, Coord3D *pos)
{
	if (!m_positionsLoaded)
	{
		loadDockPositions();
	}
	if (!pos)
	{
		return;
	}
	*pos = isZero(m_enterPosition) ? *docker.getPosition() : boneToWorld(m_exitPosition);
}

// RW 0x991B32
void DockUpdate::onApproachReached(Object &docker)
{
	for (size_t i = 0; i < m_approachPositionOwners.size(); ++i)
	{
		if (m_approachPositionOwners[i] == docker.getID())
		{
			m_approachPositionReached[i] = true;
			return;
		}
	}
}

// RW 0x991B80: the later owners (up to the first free slot) move down one place; the last slot is cleared whatever the loop stopped at
void DockUpdate::removeOwner(int index)
{
	if (m_approachPositionOwners.empty())
	{
		return;
	}
	const int last = (int)m_approachPositionOwners.size() - 1;
	int i = index;
	while (i < last && m_approachPositionOwners[(size_t)i] != INVALID_ID)
	{
		m_approachPositionOwners[(size_t)i] = m_approachPositionOwners[(size_t)(i + 1)];
		m_approachPositionReached[(size_t)i] = m_approachPositionReached[(size_t)(i + 1)];
		++i;
	}
	m_approachPositionOwners[(size_t)last] = INVALID_ID;
	m_approachPositionReached[(size_t)last] = false;
}

// RW 0x991E5E
void DockUpdate::onEnterReached(Object &docker)
{
	static const Object::ModelConditionBits clear = bitsOf({ "DOCKING_ENDING" });
	static const Object::ModelConditionBits set = bitsOf({ "DOCKING_BEGINNING", "DOCKING" });
	getObject()->clearAndSetModelConditionFlags(clear, set); // RW 0x68D607
	docker.clearAndSetModelConditionFlags(clear, set);
	m_dockerInside = true;
	for (size_t i = 0; i < m_approachPositionOwners.size(); ++i)
	{
		if (m_approachPositionOwners[i] == docker.getID())
		{
			removeOwner((int)i);
			return;
		}
	}
}

// RW 0x9917A3: DOCKING_BEGINNING off and DOCKING_ACTIVE on, for each of the two unless already so
void DockUpdate::onDockReached(Object &docker)
{
	const int begin = CombatNames::modelCondition("DOCKING_BEGINNING");
	const int active = CombatNames::modelCondition("DOCKING_ACTIVE");
	for (Object *o : { getObject(), &docker })
	{
		if (o->testModelCondition(begin) || !o->testModelCondition(active))
		{
			o->clearAndSetModelConditionFlags(bitsOf({ "DOCKING_BEGINNING" }), bitsOf({ "DOCKING_ACTIVE" }));
		}
	}
}

// RW 0x991800
void DockUpdate::onExitReached(Object &docker)
{
	static const Object::ModelConditionBits clear = bitsOf({ "DOCKING_ACTIVE", "DOCKING" });
	static const Object::ModelConditionBits set = bitsOf({ "DOCKING_ENDING" });
	getObject()->clearAndSetModelConditionFlags(clear, set);
	docker.clearAndSetModelConditionFlags(clear, set);
	m_dockerInside = false;
	if (docker.getID() == m_activeDocker)
	{
		m_activeDocker = INVALID_ID;
	}
}

// RW 0x991EF8
void DockUpdate::cancelDock(Object &docker)
{
	for (size_t i = 0; i < m_approachPositionOwners.size(); ++i)
	{
		if (m_approachPositionOwners[i] == docker.getID())
		{
			removeOwner((int)i); // the first match only
			break;
		}
	}
	if (m_activeDocker == docker.getID())
	{
		m_activeDocker = INVALID_ID;
		m_dockerInside = false;
		static const Object::ModelConditionBits clear = bitsOf({ "DOCKING", "DOCKING_BEGINNING", "DOCKING_ACTIVE", "DOCKING_ENDING" });
		static const Object::ModelConditionBits none{};
		getObject()->clearAndSetModelConditionFlags(clear, none); // RW 0x5E3B79
		if (!docker.isDestroyed())
		{
			// a docker that is being deleted (its AI's dock state exits in the AI's destructor) keeps its flags: its drawable is gone already, and nothing
			// reads an object's model conditions after it was destroyed
			docker.clearAndSetModelConditionFlags(clear, none);
		}
	}
}

// RW 0x991F8A
UpdateSleepTime DockUpdate::update()
{
	if (m_activeDocker == INVALID_ID && !m_dockCrippled)
	{
		for (size_t i = 0; i < m_approachPositionReached.size(); ++i)
		{
			if (m_approachPositionReached[i])
			{
				m_activeDocker = m_approachPositionOwners[i];
				break;
			}
		}
	}
	else
	{
		Object *obj = getObject();
		if (kindOf(*obj, "SUPPLY_SOURCE"))
		{
			Object *docker = obj->logic().findObjectByID(m_activeDocker);
			if (docker && kindOf(*docker, "DOZER") && kindOf(*docker, "HARVESTER") && docker->testModelCondition(CombatNames::modelCondition("DOCKING_BEGINNING")))
			{
				docker->setModelConditionState(CombatNames::modelCondition("MOVING"), false);
			}
		}
	}
	return UPDATE_SLEEP_NONE;
}

void DockUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	for (const Coord3D *c : { &m_enterPosition, &m_dockPosition, &m_exitPosition })
	{
		h.addFloat(c->x);
		h.addFloat(c->y);
		h.addFloat(c->z);
	}
	h.addI32(m_numberApproachPositions);
	h.addI32(m_numberApproachPositionBones);
	h.addBool(m_positionsLoaded);
	h.addU32((std::uint32_t)m_approachPositions.size());
	for (size_t i = 0; i < m_approachPositions.size(); ++i)
	{
		h.addFloat(m_approachPositions[i].x);
		h.addFloat(m_approachPositions[i].y);
		h.addFloat(m_approachPositions[i].z);
		h.addU32(m_approachPositionOwners[i]);
		h.addBool(m_approachPositionReached[i]);
	}
	h.addU32(m_activeDocker);
	h.addBool(m_dockerInside);
	h.addBool(m_dockCrippled);
	h.addBool(m_dockOpen);
}

// RW 0x99248C: UpdateModule::xfer first, then version 1 and the fields
void DockUpdate::xferDockUpdate(Xfer &x)
{
	xferUpdateModule(x);
	SnapshotXfer::version(x, 1, 1);
	for (Coord3D *c : { &m_enterPosition, &m_dockPosition, &m_exitPosition })
	{
		x.xferReal(&c->x);
		x.xferReal(&c->y);
		x.xferReal(&c->z);
	}
	x.xferInt(&m_numberApproachPositions);
	x.xferBool(&m_positionsLoaded);
	x.xferInt(&m_numberApproachPositionBones); // OpenBFME: retail recomputes it when the positions load again (RW 0x99198A)
	std::uint32_t n = (std::uint32_t)m_approachPositions.size();
	x.xferCount(&n, 12 + 4 + 1, sizeof(Coord3D) + sizeof(ObjectID) + 1);
	if (x.isLoading())
	{
		if (m_numberApproachPositions != m_baseData->m_numberApproachPositions)
		{
			throw XferException("dock update: the snapshot's approach position count " + std::to_string(m_numberApproachPositions) + " is not the data's " +
				std::to_string(m_baseData->m_numberApproachPositions));
		}
		m_approachPositions.assign(n, Coord3D{ 0.0f, 0.0f, 0.0f });
		m_approachPositionOwners.assign(n, INVALID_ID);
		m_approachPositionReached.assign(n, false);
	}
	for (Coord3D &c : m_approachPositions)
	{
		x.xferReal(&c.x);
		x.xferReal(&c.y);
		x.xferReal(&c.z);
	}
	for (ObjectID &id : m_approachPositionOwners)
	{
		x.xferObjectID(&id);
	}
	for (size_t i = 0; i < m_approachPositionReached.size(); ++i)
	{
		bool b = m_approachPositionReached[i];
		x.xferBool(&b);
		m_approachPositionReached[i] = b;
	}
	x.xferObjectID(&m_activeDocker);
	x.xferBool(&m_dockerInside);
	x.xferBool(&m_dockCrippled);
	x.xferBool(&m_dockOpen);
}

// ---- SupplyCenterDockUpdate -------------------------------------------------------------------------------------------------------
SupplyCenterDockUpdate::SupplyCenterDockUpdate(Thing *thing, const SupplyCenterDockUpdateModuleData *data)
	: DockUpdate(thing, data)
	, m_data(data)
{
}

void SupplyCenterDockUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<SupplyCenterDockUpdateModuleData>("SupplyCenterDockUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SupplyCenterDockUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SupplyCenterDockUpdateModuleData *typed = dynamic_cast<const SupplyCenterDockUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("SupplyCenterDockUpdate: the module data is not typed");
		}
		return std::make_unique<SupplyCenterDockUpdate>(thing, typed);
	});
}

// RW 0x8A47C4
UpdateSleepTime SupplyCenterDockUpdate::update()
{
	const UpdateSleepTime r = DockUpdate::update();
	Object *obj = getObject();
	const int bonus = CombatNames::modelCondition("UPGRADE_ECONOMY_BONUS"); // 332 (word + 0x134 bit 0x1000)
	if (!obj->testModelCondition(bonus))
	{
		const Player *owner = obj->getControllingPlayer();
		if (owner && owner->hasScience(m_data->m_bonusScience)) // RW 0x6AC207
		{
			obj->setModelConditionState(bonus, true);
		}
	}
	return r;
}

// RW 0x8A45C6 .. 0x8A46C0 without the deposit: `boxes` times ValuePerSupplyBox (Player RW 0x6AA7CE, fild with the unsigned fix-up, x87 fadd at PC24, fstp
// dword per box), then above 0.0 (RW 0xC1B594): * the PRODUCTION attribute modifier product of the dock (RW 0x68C82D(0xD), out 1.0 first) * ValueMultiplier
// (SSE, in that order), the multiplayer money multiplier and the handicap (Economy::applyIncomeMultipliers: RW 0x8A4644 .. 0x8A4679), back to a float, *
// BonusScienceMultiplier when the owner has BonusScience (SSE); the amount is the MSVCR71 ceil of that as a double, fstp dword, fistp
SupplyCenterDockUpdate::Delivery SupplyCenterDockUpdate::computeDelivery(Object &dock, int boxes) const
{
	Delivery d;
	GameLogic &logic = dock.logic();
	const Player *owner = dock.getControllingPlayer();
	if (!owner)
	{
		return d;
	}
	const std::uint32_t perBox = (std::uint32_t)logic.economy().settings().valuePerSupplyBox; // GlobalData + 0xA5C
	float value = 0.0f;
	for (int i = 0; i < boxes; ++i)
	{
		value = NumericState::fstpDword(NumericState::pc24AddW(NumericState::fildU32(perBox), (double)value));
	}
	if (!(value > 0.0f))
	{
		return d;
	}
	const float mult = logic.economy().attributeModifierProduct(dock, Economy::ATTRIBUTE_MODIFIER_PRODUCTION);
	value = NumericState::sseMul(NumericState::sseMul(mult, value), m_data->m_valueMultiplier);
	value = NumericState::sseFromInt32(logic.economy().applyIncomeMultipliers(*owner, value));
	if (owner->hasScience(m_data->m_bonusScience))
	{
		value = NumericState::sseMul(m_data->m_bonusScienceMultiplier, value);
	}
	d.paid = true;
	d.value = value;
	d.amount = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD((double)value)));
	return d;
}

// RW 0x8A4574
bool SupplyCenterDockUpdate::action(Object &docker, Object *)
{
	AIUpdateInterface *ai = docker.getAIUpdateInterface();
	SupplyTruckAIInterface *sup = ai ? ai->getSupplyTruckAIInterface() : nullptr;
	if (!sup)
	{
		return false;
	}
	Object *dock = getObject();
	Player *owner = dock->getControllingPlayer(); // RW 0x8A45B7, read before the boxes go
	int boxes = 0;
	while (sup->loseOneBox()) // slot 0x08, one ValuePerSupplyBox per box
	{
		++boxes;
	}
	if (!owner)
	{
		return false;
	}
	const Delivery d = computeDelivery(*dock, boxes);
	if (!d.paid)
	{
		return false;
	}
	GameLogic &logic = dock->logic();
	owner->depositMoney((std::uint32_t)d.amount, true); // RW 0x7B18B8(amount, Player + 0x3DC, 1)
	// RW 0x8A46DA .. 0x8A4702: the dock's experience tracker (+ 0x26C), when it accepts points (RW 0x79D322), gets the float value (after the science
	// multiplier, before the ceil): RW 0x79D833(value, 1, 1, 1, 0) through the economy's experience hook
	logic.economy().grantBuildingExperience(*dock, d.value);
	logic.economy().noteIncome(docker, *owner, d.amount); // the "GUI:AddCash" text at the docker (RW 0x8A470A .. 0x8A477D, client)
	return false;
}

void SupplyCenterDockUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1); // RW 0x8A44F8
	xferDockUpdate(x);
}
