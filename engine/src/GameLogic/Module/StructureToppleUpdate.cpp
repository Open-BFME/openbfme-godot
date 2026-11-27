// OpenBFME. GPL-3.0.
// See GameLogic/Module/StructureToppleUpdate.h (addresses, donor files, editions).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/StructureToppleUpdate.h"

#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/TransitionDamageFX.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
using SimMath::pc24Add;
using SimMath::pc24Div;
using SimMath::pc24Mul;
using SimMath::pc24Sub;

const char kFile[] = "StructureToppleUpdate.cpp"; // the donor's file / line arguments of the draws (debug only in retail)
// the float constants of the bodies (ZH PI = 3.14159265359f)
const float kPi = 3.14159265359f;
const float kHalfPi = 0x1.921fb6p+0f;   // (float)(kPi / 2)
const float kTwoPi = 0x1.921fb6p+2f;    // (float)(2 * kPi)
const float kPiOver8 = 0x1.921fb6p-2f;  // (float)(kPi / 8)
const float kThetaCeiling = 0x1.0c1524p-1f; // (float)(kPi / 6): applyCrushingDamage's THETA_CEILING
const float kDegToRad = 0x1.1df46ap-6f; // (float)(kPi / 180)
const float kWeaponSpacing = 25.0f;            // WEAPON_SPACING_PERPENDICULAR / _PARALLEL
const float kNoToppleAngle = -9.87654f;        // the module data ctor's ForceToppleAngle (RW 0x65776F)

const char *const kTopplePhaseNames[] = { "INITIAL", "DELAY", "FINAL", nullptr };

// RW 0x8A9AC9 (A): the phase, then every name token of the line
void parseToppleOCL(INI *ini, void *instance, void *, const void *)
{
	StructureToppleUpdateModuleData *d = static_cast<StructureToppleUpdateModuleData *>(instance);
	const int phase = INI::scanIndexList(ini->getNextToken(), kTopplePhaseNames);
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		d->m_ocl[phase].push_back(t);
	}
}

// RW 0x8A9B2B: AngleFX = <degrees> <FXList>
void parseAngleFX(INI *ini, void *instance, void *, const void *)
{
	StructureToppleUpdateModuleData *d = static_cast<StructureToppleUpdateModuleData *>(instance);
	AngleFXInfo info;
	INI::parseReal(ini, instance, &info.angle, nullptr);
	info.angle = pc24Div(pc24Mul(info.angle, 3.14159265359f), 180.0f);
	info.fxList = ini->getNextToken(); // RW 0x73A302: the FX list store is the client's (S-340)
	d->m_angleFX.push_back(info);
}

void parseFXName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73A302: the FX list store is the client's (S-340)
}

// RW 0x73A5C9: ALL / NONE / +TYPE / -TYPE (the same parser as TransitionDamageFX's DamageFXTypes)
void parseDamageFXTypes(INI *ini, void *, void *store, const void *)
{
	std::uint32_t mask = 0xFFFFFFFFu;
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		if (AsciiStringUtil::compareNoCase(t, "ALL") == 0)
		{
			mask = 0xFFFFFFFFu;
		}
		else if (AsciiStringUtil::compareNoCase(t, "NONE") == 0)
		{
			mask = 0;
		}
		else if (t[0] == '+')
		{
			mask |= 1u << ((unsigned)(INI::scanIndexList(t + 1, TheDamageNames) - 1) & 31u);
		}
		else if (t[0] == '-')
		{
			mask &= ~(1u << ((unsigned)(INI::scanIndexList(t + 1, TheDamageNames) - 1) & 31u));
		}
		else
		{
			throw INIException(5, "ALL, NONE, + or - expected");
		}
	}
	*static_cast<std::uint32_t *>(store) = mask;
}

#define ST_OFF(member) (int)offsetof(StructureToppleUpdateModuleData, member)
// RW table 0xC69680 (in the binary's row order) then the DieMux table 0xC76BD8
const FieldParse kStructureToppleParse[] = {
	{ "MinToppleDelay", INI::parseDurationUnsignedInt, nullptr, ST_OFF(m_minToppleDelay) },           // RW 0x73A429
	{ "MaxToppleDelay", INI::parseDurationUnsignedInt, nullptr, ST_OFF(m_maxToppleDelay) },
	{ "MinToppleBurstDelay", INI::parseDurationUnsignedInt, nullptr, ST_OFF(m_minToppleBurstDelay) },
	{ "MaxToppleBurstDelay", INI::parseDurationUnsignedInt, nullptr, ST_OFF(m_maxToppleBurstDelay) },
	{ "ToppleAccelerationFactor", INI::parseReal, nullptr, ST_OFF(m_toppleAccelerationFactor) },     // RW 0x42ED00
	{ "StructuralIntegrity", INI::parseReal, nullptr, ST_OFF(m_structuralIntegrity) },
	{ "StructuralDecay", INI::parseReal, nullptr, ST_OFF(m_structuralDecay) },
	{ "DamageFXTypes", parseDamageFXTypes, nullptr, ST_OFF(m_damageFXTypes) },                        // RW 0x73A5C9
	{ "TopplingFX", parseFXName, nullptr, ST_OFF(m_treeToppleFX) },                                       // RW 0x73A302
	{ "ToppleDelayFX", parseFXName, nullptr, ST_OFF(m_toppleDelayFX) },
	{ "ToppleStartFX", parseFXName, nullptr, ST_OFF(m_toppleStartFX) },
	{ "ToppleDoneFX", parseFXName, nullptr, ST_OFF(m_toppleDoneFX) },
	{ "CrushingFX", parseFXName, nullptr, ST_OFF(m_crushingFX) },
	{ "CrushingWeaponName", INI::parseAsciiString, nullptr, ST_OFF(m_crushingWeaponName) },          // RW 0x42EE5E
	{ "ForceToppleAngle", INI::parseReal, nullptr, ST_OFF(m_forceToppleAngle) },
	{ "OCL", parseToppleOCL, nullptr, 0 },                                                            // RW 0x8A9AC9
	{ "AngleFX", parseAngleFX, nullptr, 0 },                                                          // RW 0x8A9B2B
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, ST_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, ST_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, ST_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, ST_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, ST_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, ST_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef ST_OFF

// Coord2D::toAngle (donor WWMath coord2d.cpp): acos of the clamped x / length, negated below the x axis
float toAngle(float x, float y)
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)pc24Add(pc24Mul(x, x), pc24Mul(y, y))));
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = pc24Div(x, len);
	if (c < -1.0f)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const float a = (float)SimMath::acosDet((double)c);
	return y < 0.0f ? -a : a;
}

// normalizeAngle (ZH): into (-pi, pi]
float normalizeAngle(float a)
{
	while (a > kPi)
	{
		a = pc24Sub(a, kTwoPi);
	}
	while (a <= -kPi)
	{
		a = pc24Add(a, kTwoPi);
	}
	return a;
}

// WWMath Matrix3D::In_Place_Pre_Rotate_X / _Y / _Z on the rotation part (the basis, row major, columns = the axes)
void preRotateX(float b[9], float theta)
{
	const float c = SimMath::cosf32(theta), s = SimMath::sinf32(theta);
	for (int j = 0; j < 3; ++j)
	{
		const float t1 = b[3 + j], t2 = b[6 + j];
		b[3 + j] = pc24Sub(pc24Mul(c, t1), pc24Mul(s, t2));
		b[6 + j] = pc24Add(pc24Mul(s, t1), pc24Mul(c, t2));
	}
}
void preRotateY(float b[9], float theta)
{
	const float c = SimMath::cosf32(theta), s = SimMath::sinf32(theta);
	for (int j = 0; j < 3; ++j)
	{
		const float t0 = b[j], t2 = b[6 + j];
		b[j] = pc24Add(pc24Mul(c, t0), pc24Mul(s, t2));
		b[6 + j] = pc24Add(pc24Mul(-s, t0), pc24Mul(c, t2));
	}
}
void preRotateZ(float b[9], float theta)
{
	const float c = SimMath::cosf32(theta), s = SimMath::sinf32(theta);
	for (int j = 0; j < 3; ++j)
	{
		const float t0 = b[j], t1 = b[3 + j];
		b[j] = pc24Sub(pc24Mul(c, t0), pc24Mul(s, t1));
		b[3 + j] = pc24Add(pc24Mul(s, t0), pc24Mul(c, t1));
	}
}

void setBit(Object::ModelConditionBits &bits, int bit)
{
	bits[(size_t)bit >> 5] |= 1u << (bit & 31);
}

// a null FXList (RW 0x73A302: an empty name or "None") plays nothing
bool hasFX(const std::string &name)
{
	return !name.empty() && AsciiStringUtil::compareNoCase(name, "None") != 0;
}
} // namespace

void StructureToppleUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kStructureToppleParse); }

// RW 0x8A8BB4 (A)
StructureToppleUpdate::StructureToppleUpdate(Thing *thing, const StructureToppleUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	m_buildingHeight = ObjectGeometry::maxHeightAbovePosition(getObject()->geometryShapes()); // the object's own GeometryInfo (RW 0x8A8BB4 -> getMaxHeightAbovePosition)
}

// the gate of every FX call (RW 0x8A9167 / 0x8A8E39 / 0x8A91BF / 0x8A8FA5 / 0x8A96A0): no last damage record, or its type is in DamageFXTypes
bool StructureToppleUpdate::damageFXAllowed() const
{
	const BodyModuleInterface *body = getObject()->getBodyModule();
	int type = 0;
	if (!body || !body->lastDamageRecordType(type))
	{
		return true;
	}
	return TransitionDamageFXModuleData::damageTypeAllowed(m_data->m_damageFXTypes, type);
}

// FXList::doFXPos / doFXObj: the FX lists are the client's (S-340); the call is handed to it as an event
void StructureToppleUpdate::doFX(bool atPosition, const char *site, const std::string &fxList, const Coord3D *position)
{
	if (!hasFX(fxList))
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	FXEvent e = FXEventLog::objectEvent(atPosition ? FXEvent::POSITION_FX : FXEvent::OBJECT_FX, site, logic.getFrame(), fxList, *obj);
	if (atPosition)
	{
		e.hasTransform = false;
		e.position = *position;
	}
	logic.fxEvents().emit(e);
}

// RW 0x8A8EA9 (A): the OCL half only. The draws: StructureToppleUpdate.cpp's copy of buildNonDupRandomIndexList (BFME2 rva 0x4A54CE, line 536) draws every pick
// first (redrawing a duplicate), then each picked OCL is created at the target, the toppling object its source
void StructureToppleUpdate::doPhaseStuff(StructureTopplePhaseType phase, const Coord3D &target)
{
	const std::vector<std::string> &list = m_data->m_ocl[phase];
	const int size = (int)list.size();
	if (size <= 0)
	{
		return;
	}
	const int count = m_data->m_oclCount[phase];
	if (count > size)
	{
		throw std::logic_error("StructureToppleUpdate: a phase asks for more distinct OCLs than its list has (retail loops forever)");
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	std::vector<int> picked;
	picked.reserve((size_t)(count > 0 ? count : 0));
	for (int i = 0; i < count; ++i)
	{
		int idx;
		bool again;
		do
		{
			idx = logic.random().getValue(0, size - 1, kFile, 536);
			again = false;
			for (int j = 0; j < i; ++j)
			{
				again = again || picked[(size_t)j] == idx;
			}
		} while (again);
		picked.push_back(idx);
	}
	for (int i = 0; i < count; ++i)
	{
		const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(list[(size_t)picked[(size_t)i]]) : nullptr;
		if (ocl)
		{
			ocl->create(logic, obj, target); // RW 0x5F0126(ocl, obj, target, 0, 0)
		}
	}
}

// RW 0x8A9167 (B)
void StructureToppleUpdate::doToppleStartFX(Object *building)
{
	if (damageFXAllowed())
	{
		doFX(true, "ToppleStartFX", m_data->m_toppleStartFX, building->getPosition());
	}
	doPhaseStuff(STPHASE_INITIAL, *building->getPosition());
}

// RW 0x8A8E39 (B): every AngleFX whose angle the fall crosses this frame (cur < angle <= new)
void StructureToppleUpdate::doAngleFX(float curAngle, float newAngle)
{
	const bool allowed = damageFXAllowed();
	for (const AngleFXInfo &info : m_data->m_angleFX)
	{
		if (info.angle > curAngle && info.angle <= newAngle && allowed)
		{
			doFX(false, "ToppleAngleFX", info.fxList, nullptr);
		}
	}
}

// RW 0x8A91BF (B): the delay FX at the burst location; the FX bone particle systems (module data + 0xA0) have no RotWK row and stay empty; the DELAY OCLs
void StructureToppleUpdate::doToppleDelayBurstFX()
{
	if (damageFXAllowed())
	{
		doFX(true, "ToppleDelayFX", m_data->m_toppleDelayFX, &m_delayBurstLocation);
	}
	doPhaseStuff(STPHASE_DELAY, m_delayBurstLocation);
}

// RW 0x8A8C86 (A): BoneFXUpdate::stopAllBoneFX (no BoneFXUpdate runtime here: findModule finds none), the orientation set again (an upright basis), turned
// about Z to the topple direction and applied through Object::teleportTransform (RW 0x68DA67, BFME2 0x28D412)
void StructureToppleUpdate::doToppleDoneStuff()
{
	Object *building = getObject();
	const float origAngle = building->getOrientation();
	building->setOrientation(origAngle);
	const float toppleAngle = toAngle(m_toppleDirectionX, m_toppleDirectionY);
	float basis[9];
	std::memcpy(basis, building->getBasis(), sizeof(basis));
	preRotateZ(basis, pc24Sub(toppleAngle, origAngle));
	const Coord3D pos = *building->getPosition();
	building->teleportTransform(&pos, basis);
}

// RW 0x8A8FA5 (B): one line of crushing shots across the fall line at distance j (jcos, jsin), 25 apart from -facingWidth, then one at +facingWidth, then the
// FINAL OCLs at the line's centre. The offset is (i sin, i cos) as retail writes it (a perpendicular only for the axis-aligned falls)
void StructureToppleUpdate::doDamageLine(Object *building, const WeaponTemplate *wt, float jcos, float jsin, float facingWidth, float toppleAngle)
{
	GameLogic &logic = building->logic();
	const bool allowed = damageFXAllowed();
	const float s = SimMath::sinf32(toppleAngle);
	const float c = SimMath::cosf32(toppleAngle);
	Coord3D target;
	for (float i = -facingWidth; i < facingWidth; i = pc24Add(i, kWeaponSpacing))
	{
		target.x = pc24Add(pc24Add(building->getPosition()->x, jcos), pc24Mul(i, s));
		target.y = pc24Add(pc24Add(building->getPosition()->y, jsin), pc24Mul(i, c));
		target.z = logic.getGroundHeight(target.x, target.y);
		ObjectWeapons::createAndFireTempWeapon(wt, building, target);
		if (allowed)
		{
			doFX(true, "ToppleCrushingFX", m_data->m_crushingFX, &target);
		}
	}
	target.x = pc24Add(pc24Add(building->getPosition()->x, jcos), pc24Mul(facingWidth, s));
	target.y = pc24Add(pc24Add(building->getPosition()->y, jsin), pc24Mul(facingWidth, c));
	target.z = logic.getGroundHeight(target.x, target.y);
	ObjectWeapons::createAndFireTempWeapon(wt, building, target);
	if (allowed)
	{
		doFX(true, "ToppleCrushingFX", m_data->m_crushingFX, &target);
	}
	target.x = pc24Add(building->getPosition()->x, jcos);
	target.y = pc24Add(building->getPosition()->y, jsin);
	target.z = logic.getGroundHeight(target.x, target.y);
	doPhaseStuff(STPHASE_FINAL, target);
}

// RW 0x8A950A (A): theta is the building's angle to the ground; at or below 30 degrees the lines from the last crushed distance to the reach of the top
// (height * (1 - sin theta)) are fired, 25 apart, then one at the reach
void StructureToppleUpdate::applyCrushingDamage(float theta)
{
	if (theta > kThetaCeiling)
	{
		return;
	}
	Object *building = getObject();
	const float orientationAngle = building->getOrientation();
	const float toppleAngle = toAngle(m_toppleDirectionX, m_toppleDirectionY);
	const float angle = pc24Sub(orientationAngle, toppleAngle);
	const std::vector<ObjectGeometry::Shape> &shapes = building->geometryShapes();
	if (shapes.empty())
	{
		throw std::logic_error("StructureToppleUpdate: the toppling object has no geometry (RW 0x8A950A reads Object + 0xCC / + 0xD0)");
	}
	const float minorComponent = pc24Mul(shapes[0].minorRadius, SimMath::cosf32(angle));
	const float majorComponent = pc24Mul(shapes[0].majorRadius, SimMath::sinf32(angle));
	const float facingWidth = pc24Div(SimMath::fstpDword(SimMath::sqrtPC24((double)pc24Add(pc24Mul(majorComponent, majorComponent), pc24Mul(minorComponent, minorComponent)))),
		2.0f); // Coord3D::length (z = 0) / 2
	const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_data->m_crushingWeaponName) : nullptr;
	if (!wt)
	{
		return; // RW 0x8A950A: an unknown CrushingWeaponName finds NULL and crushes nothing (like ExtraDieModules' DeathWeapon)
	}
	const float maxDistance = SimMath::fstpDword(SimMath::pc24MulD((double)m_buildingHeight, SimMath::pc24SubD(1.0, (double)SimMath::sinf32(theta))));
	const float c = SimMath::cosf32(toppleAngle);
	const float s = SimMath::sinf32(toppleAngle);
	float j;
	for (j = m_lastCrushedLocation; j < maxDistance; j = pc24Add(j, kWeaponSpacing))
	{
		doDamageLine(building, wt, pc24Mul(j, c), pc24Mul(j, s), facingWidth, toppleAngle);
	}
	doDamageLine(building, wt, pc24Mul(maxDistance, c), pc24Mul(maxDistance, s), facingWidth, toppleAngle);
	m_lastCrushedLocation = j;
}

// RW 0x8A96A0 (B)
UpdateSleepTime StructureToppleUpdate::update()
{
	if (m_toppleState == TOPPLESTATE_STANDING)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *building = getObject();
	GameLogic &logic = building->logic();
	const bool doneFXAllowed = damageFXAllowed(); // the last damage record is read once, at the top
	if (m_toppleState == TOPPLESTATE_WAITINGFORTOPPLESTART)
	{
		const unsigned now = logic.getFrame();
		if (now >= m_nextBurstFrame)
		{
			doToppleDelayBurstFX();
			m_nextBurstFrame = now + (unsigned)logic.random().getValue((int)m_data->m_minToppleBurstDelay, (int)m_data->m_maxToppleBurstDelay, kFile, 230);
		}
		if (now >= m_toppleFrame)
		{
			m_toppleState = TOPPLESTATE_TOPPLING;
			m_structuralIntegrity = m_data->m_structuralIntegrity;
		}
	}
	if (m_toppleState == TOPPLESTATE_TOPPLING)
	{
		const unsigned now = logic.getFrame();
		const float toppleAcceleration =
			pc24Mul(m_data->m_toppleAccelerationFactor, pc24Mul(SimMath::sinf32(m_accumulatedAngle), pc24Sub(1.0f, m_structuralIntegrity)));
		m_toppleVelocity = pc24Add(m_toppleVelocity, toppleAcceleration);
		if (m_structuralIntegrity > 0.0f)
		{
			m_structuralIntegrity = pc24Mul(m_structuralIntegrity, m_data->m_structuralDecay);
			if (m_structuralIntegrity < 0.0f)
			{
				m_structuralIntegrity = 0.0f;
			}
		}
		doAngleFX(m_accumulatedAngle, pc24Add(m_accumulatedAngle, m_toppleVelocity));
		m_accumulatedAngle = pc24Add(m_accumulatedAngle, m_toppleVelocity);
		applyCrushingDamage(pc24Sub(kHalfPi, m_accumulatedAngle));
		if (m_accumulatedAngle >= kHalfPi)
		{
			m_toppleVelocity = pc24Sub(m_toppleVelocity, pc24Sub(m_accumulatedAngle, kHalfPi));
			m_accumulatedAngle = kHalfPi;
			m_toppleState = TOPPLESTATE_WAITINGFORDONE;
			applyCrushingDamage(0.0f);
			doPhaseStuff(STPHASE_FINAL, *building->getPosition());
			if (doneFXAllowed)
			{
				doFX(false, "ToppleDoneFX", m_data->m_toppleDoneFX, nullptr);
			}
			m_toppleFrame = logic.getFrame();
		}
		if (now >= m_nextBurstFrame)
		{
			doToppleDelayBurstFX();
			m_nextBurstFrame = now + (unsigned)logic.random().getValue((int)m_data->m_minToppleBurstDelay, (int)m_data->m_maxToppleBurstDelay, kFile, 279);
		}
		// the fall: the transform turned about X by -v * dir.y and about Y by v * dir.x (Thing::setTransformMatrix, no teleport)
		float basis[9];
		std::memcpy(basis, building->getBasis(), sizeof(basis));
		preRotateX(basis, pc24Mul(-m_toppleVelocity, m_toppleDirectionY));
		preRotateY(basis, pc24Mul(m_toppleVelocity, m_toppleDirectionX));
		const Coord3D pos = *building->getPosition();
		building->Thing::setTransform(&pos, basis);
	}
	if (m_toppleState == TOPPLESTATE_WAITINGFORDONE)
	{
		if (m_toppleFrame <= logic.getFrame())
		{
			Object::ModelConditionBits clear{};
			Object::ModelConditionBits set{};
			setBit(clear, CombatNames::modelCondition("RUBBLE"));
			setBit(set, CombatNames::modelCondition("POST_COLLAPSE"));
			building->clearAndSetModelConditionFlags(clear, set);
			// body vslot updateBodyParticleSystems: the body's particle systems are the client's (S-340)
			doToppleDoneStuff();
			m_toppleState = TOPPLESTATE_DONE;
			return UPDATE_SLEEP_FOREVER;
		}
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8A92EA (B)
void StructureToppleUpdate::beginStructureTopple(const DieModuleInterface::Event &event)
{
	Object *building = getObject();
	GameLogic &logic = building->logic();
	const unsigned now = logic.getFrame();
	m_toppleFrame = now + (unsigned)logic.random().getValue((int)m_data->m_minToppleDelay, (int)m_data->m_maxToppleDelay, kFile, 132);
	Object *attacker = event.sourceId != INVALID_ID ? logic.findObjectByID(event.sourceId) : nullptr;
	float toppleAngle;
	if (m_data->m_forceToppleAngle != kNoToppleAngle)
	{
		toppleAngle = normalizeAngle(pc24Add(pc24Mul(m_data->m_forceToppleAngle, kDegToRad), building->getOrientation()));
	}
	else if (!attacker)
	{
		toppleAngle = logic.random().getValueReal(0.0f, kTwoPi, kFile, 151);
	}
	else
	{
		// away from the attacker: the building's position minus the attacker's (a killer to the east sends the building west)
		m_toppleDirectionX = pc24Sub(building->getPosition()->x, attacker->getPosition()->x);
		m_toppleDirectionY = pc24Sub(building->getPosition()->y, attacker->getPosition()->y);
		toppleAngle = toAngle(m_toppleDirectionX, m_toppleDirectionY);
		toppleAngle = pc24Add(toppleAngle, logic.random().getValueReal(-kPiOver8, kPiOver8, kFile, 164));
	}
	m_toppleDirectionX = SimMath::cosf32(toppleAngle);
	m_toppleDirectionY = SimMath::sinf32(toppleAngle);
	// ScriptEngine::adjustToppleDirection (donor): replaces the direction with one a NAMED_SET_TOPPLE_DIRECTION action (script action 230) stored for the
	// object; that action has no case here (S-1180 counts it), so the list is empty and the direction stands (S-345)
	const std::vector<ObjectGeometry::Shape> &shapes = building->geometryShapes();
	if (shapes.empty())
	{
		throw std::logic_error("StructureToppleUpdate: the toppling object has no geometry (RW 0x8A92EA reads Object + 0xCC / + 0xD0)");
	}
	const float averageRadius = pc24Div(pc24Add(shapes[0].majorRadius, shapes[0].minorRadius), 2.0f);
	const float explosionRadius = SimMath::fstpDword(SimMath::pc24MulD((double)averageRadius, 0.90));
	m_delayBurstLocation.x = pc24Add(building->getPosition()->x, pc24Mul(explosionRadius, SimMath::cosf32(toppleAngle)));
	m_delayBurstLocation.y = pc24Add(building->getPosition()->y, pc24Mul(explosionRadius, SimMath::sinf32(toppleAngle)));
	m_delayBurstLocation.z = logic.getGroundHeight(m_delayBurstLocation.x, m_delayBurstLocation.y);
	doToppleStartFX(building);
	m_nextBurstFrame = now + (unsigned)logic.random().getValue((int)m_data->m_minToppleBurstDelay, (int)m_data->m_maxToppleBurstDelay, kFile, 181);
	m_toppleState = TOPPLESTATE_WAITINGFORTOPPLESTART;
	setWakeFrame(building, UPDATE_SLEEP_NONE);
}

// BFME2 0x8A5E5B (no RotWK address in the decomp notes; S-345)
void StructureToppleUpdate::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (!m_data->m_dieMux.isDieApplicable(*obj, event)) // RW 0x8D29A9, with SIEGEFALL-1's killer-angle window
	{
		return;
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->onDie(); // markAsDead
	}
	// GameLogic::deselectObject(obj, PLAYERMASK_ALL, true): the selection is InGameUI's, which forgets a destroyed object (S-340)
	beginStructureTopple(event);
}

void StructureToppleUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_toppleFrame);
	h.addFloat(m_toppleDirectionX);
	h.addFloat(m_toppleDirectionY);
	h.addU32((std::uint32_t)m_toppleState);
	h.addFloat(m_toppleVelocity);
	h.addFloat(m_accumulatedAngle);
	h.addFloat(m_structuralIntegrity);
	h.addFloat(m_lastCrushedLocation);
	h.addU32(m_nextBurstFrame);
	h.addFloat(m_delayBurstLocation.x);
	h.addFloat(m_delayBurstLocation.y);
	h.addFloat(m_delayBurstLocation.z);
}

// RW 0x8A8F1B (A): the update module's part first, then version 1 and the fields +0x24 .. +0x48 (the state as 4 raw bytes); m_buildingHeight is not saved
void StructureToppleUpdate::xfer(Xfer &x)
{
	xferUpdateModule(x);
	SnapshotXfer::version(x, 1, 1);
	x.xferUnsignedInt(&m_toppleFrame);
	x.xferReal(&m_toppleDirectionX);
	x.xferReal(&m_toppleDirectionY);
	SnapshotXfer::enumIn(x, m_toppleState, TOPPLESTATE_STANDING, TOPPLESTATE_DONE, "a structure topple's state");
	x.xferReal(&m_toppleVelocity);
	x.xferReal(&m_accumulatedAngle);
	x.xferReal(&m_structuralIntegrity);
	x.xferReal(&m_lastCrushedLocation);
	x.xferUnsignedInt(&m_nextBurstFrame);
	SnapshotXfer::coord3D(x, m_delayBurstLocation);
}

// ---- ToppleUpdate (RW 0x0064ED68 / RW 0x0064EDA0 / RW 0x008AB767 .. RW 0x008ABE65; DONOR BFME2 ToppleUpdate.cpp) -----------------------------------------
namespace
{
const float kTopplePi = 3.14159274f;           // RW 0xBDD388
const float kToppleNegPi = -3.14159274f;       // RW 0xBDD390
const float kToppleTwoPi = 6.28318548f;        // RW 0xBDD38C
const float kToppleHalfPi = 1.57079637f;       // RW 0xBD89D0
const float kToppleAngularLimit = 1.52170897f; // RW 0xC6A298 (PI/2 - PI/64)
const float kToppleMinBounceVel = 0.05f;       // RW 0xBDD760
const float kToppleBounceDecayStep = 0.025f;   // RW 0xC67BB4
const float kToppleBounceLimit = 0.01f;        // RW 0xBE5600
const float kToppleBounceSoundLimit = 0.03f;   // RW 0xBDC540
const float kToppleKillDamage = 999999.0f;     // RW 0xC6A294

#define TU_OFF(member) (int)offsetof(ToppleUpdateModuleData, member)
// RW 0xC6A490 (buildFieldParse RW 0x008AB8C6, DONOR BFME2 0x008A81F0)
const FieldParse kToppleParse[] = {
	{ "ToppleFX", parseFXName, nullptr, TU_OFF(m_treeToppleFX) },                                       // RW 0x73A302
	{ "BounceFX", parseFXName, nullptr, TU_OFF(m_bounceFX) },                                       // RW 0x73A302
	{ "StumpName", INI::parseAsciiString, nullptr, TU_OFF(m_stumpName) },                           // RW 0x42EE5E
	{ "KillWhenStartToppling", INI::parseBool, nullptr, TU_OFF(m_killWhenStartToppled) },           // RW 0x42E558
	{ "KillWhenFinishedToppling", INI::parseBool, nullptr, TU_OFF(m_killWhenToppled) },             // RW 0x42E558
	{ "KillStumpWhenToppled", INI::parseBool, nullptr, TU_OFF(m_killStumpWhenToppled) },             // RW 0x42E558
	{ "ToppleLeftOrRightOnly", INI::parseBool, nullptr, TU_OFF(m_toppleLeftOrRightOnly) },          // RW 0x42E558
	{ "ReorientToppledRubble", INI::parseBool, nullptr, TU_OFF(m_reorientToppledRubble) },          // RW 0x42E558
	{ "InitialVelocityPercent", INI::parsePercentToReal, nullptr, TU_OFF(m_initialVelocityPercent) }, // RW 0x42EEFA
	{ "InitialAccelPercent", INI::parsePercentToReal, nullptr, TU_OFF(m_initialAccelPercent) },       // RW 0x42EEFA
	{ "BounceVelocityPercent", INI::parsePercentToReal, nullptr, TU_OFF(m_bounceVelocityPercent) },   // RW 0x42EEFA
	{ "MinimumToppleSpeed", INI::parsePositiveNonZeroReal, nullptr, TU_OFF(m_minimumToppleSpeed) },   // RW 0x42ED1C
	{ nullptr, nullptr, nullptr, 0 }
};
#undef TU_OFF

// RW 0x00644FD0: SSE angle normalisation into (-pi, pi]
float toppleNormalizeAngle(float a)
{
	while (a > kTopplePi)
	{
		a = SimMath::subf32(a, kToppleTwoPi);
	}
	while (kToppleNegPi >= a)
	{
		a = SimMath::addf32(a, kToppleTwoPi);
	}
	return a;
}

// RW 0x008AB78D (DONOR BFME2 0x008A80B7): picks the candidate angle (a1 or a2) closer to desired
float angleClosestTo(float a1, float a2, float desired)
{
	a1 = toppleNormalizeAngle(a1);
	a2 = toppleNormalizeAngle(a2);
	const float diff1 = toppleNormalizeAngle(SimMath::pc24Sub(desired, a1));
	const float diff2 = toppleNormalizeAngle(SimMath::pc24Sub(desired, a2));
	const float absDiff2 = SimMath::fstpDword(SimMath::absD((double)diff2));
	const double absDiff1 = SimMath::absD((double)diff1);
	return (double)absDiff2 > absDiff1 ? a1 : a2;
}

// RW 0x00403175 Coord3D::normalize
void normalizeCoord3D(Coord3D &v)
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(v.x, v.y, v.z)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		v.x = SimMath::mulf32(v.x, inv);
		v.y = SimMath::mulf32(v.y, inv);
		v.z = SimMath::mulf32(v.z, inv);
	}
}

// RW 0x0068B34C: current locomotor speed per logic frame when > 0, else 0
float currentLocomotorSpeed(const Object &o)
{
	AIUpdateInterface *ai = o.getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	return loco && loco->speed() > 0.0f ? loco->speed() : 0.0f;
}

// RW 0x008AB990 (DONOR BFME2 0x008A82BA): 999999.0f DAMAGE_UNRESISTABLE (8) with DEATH_TOPPLED (6)
void toppleKillObject(Object &obj)
{
	DamageInfo info;
	info.m_input.m_sourceID = INVALID_ID;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_TOPPLED;
	info.m_input.m_amount = kToppleKillDamage;
	obj.attemptDamage(info);
}

void doToppleObjFX(Object &obj, const char *site, const std::string &fxList)
{
	if (!hasFX(fxList))
	{
		return;
	}
	GameLogic &logic = obj.logic();
	logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fxList, obj));
}
} // namespace

// RW 0x008AB8C6 (DONOR BFME2 0x008A81F0; data ctor RW 0x008AB849, dtor RW 0x008ABA7D / RW 0x008ABA61, factory RW 0x0064EDA0)
void ToppleUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kToppleParse);
}

// RW 0x008AB8D7 (DONOR BFME2 0x008A8201; factory RW 0x0064ED68, dtor RW 0x008AB767 / RW 0x008AB974, name RW 0x008AB787, key RW 0x008AB804)
ToppleUpdate::ToppleUpdate(Thing *thing, const ToppleUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x008AB9CB (DONOR BFME2 0x008A82F5 Rva004A82F5Topple.cpp)
void ToppleUpdate::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	if (!other)
	{
		return;
	}
	Object *self = getObject();
	if (ObjectCrush::crusherLevel(*other) <= ObjectCrush::crushableLevel(*self))
	{
		return;
	}
	Coord3D delta{
		SimMath::subf32(self->getPosition()->x, other->getPosition()->x),
		SimMath::subf32(self->getPosition()->y, other->getPosition()->y),
		0.0f
	};
	float speed = currentLocomotorSpeed(*other);
	if (speed == 0.0f)
	{
		if (const Object *container = other->getContainedBy()) // RW 0x8ABA38: other + 0x27C
		{
			speed = currentLocomotorSpeed(*container);
		}
	}
	self->topple(&delta, speed, TOPPLE_OPTIONS_NONE);
}

// RW 0x008ABAAD (DONOR BFME2 0x008A83D7)
void ToppleUpdate::applyTopplingForce(const Coord3D *toppleDirection, float toppleSpeed, unsigned options)
{
	Object *self = getObject();
	if (self->isEffectivelyDead())
	{
		return;
	}
	if (m_data->m_minimumToppleSpeed > toppleSpeed)
	{
		toppleSpeed = m_data->m_minimumToppleSpeed;
	}
	setWakeFrame(self, UPDATE_SLEEP_NONE);
	if (m_data->m_killWhenStartToppled)
	{
		setWakeFrame(self, UPDATE_SLEEP_FOREVER);
		self->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		return;
	}
	m_toppleDirection = toppleDirection ? *toppleDirection : Coord3D{ 0.0f, 0.0f, 0.0f };
	normalizeCoord3D(m_toppleDirection);
	// RW 0x8ABB3E: ScriptEngine::adjustToppleDirection (RW 0x758E47, set by NAMED_SET_TOPPLE_DIRECTION, action 230, S-345 / S-1180)
	m_angularVelocity = SimMath::mulf32(m_data->m_initialVelocityPercent, toppleSpeed);
	m_angularAcceleration = SimMath::mulf32(m_data->m_initialAccelPercent, toppleSpeed);
	m_toppleState = TOPPLE_FALLING;
	m_options = options;
	// RW 0x8ABB78 .. 0x8ABBC4: stops SwayClientUpdate on the drawable (client-side)
	const float currentAngle = toppleNormalizeAngle(self->getOrientation());
	float toppleAngle = toppleNormalizeAngle(SimMath::fstpDword(SimMath::atan2d(m_toppleDirection.y, m_toppleDirection.x)));
	if (m_data->m_toppleLeftOrRightOnly)
	{
		toppleAngle = angleClosestTo(SimMath::addf32(currentAngle, kToppleHalfPi), SimMath::subf32(currentAngle, kToppleHalfPi), toppleAngle);
		m_toppleDirection.x = SimMath::cosf32(toppleAngle);
		m_toppleDirection.y = SimMath::sinf32(toppleAngle);
		if (AIWorld *ai = self->logic().aiWorld())
		{
			ai->removeObjectFromPathfindMap(*self); // RW 0x6E85FB
		}
	}
	const float desiredAngle = angleClosestTo(SimMath::addf32(toppleAngle, kToppleHalfPi), SimMath::subf32(toppleAngle, kToppleHalfPi), currentAngle);
	const double twoVel = SimMath::pc24AddW((double)m_angularVelocity, (double)m_angularVelocity);
	const float ratio = SimMath::fstpDword(SimMath::floorD(SimMath::pc24DivW((double)kToppleAngularLimit, twoVel)));
	int count = SimMath::fistp32((double)ratio);
	if (count < 1)
	{
		count = 1;
	}
	m_numAngleDeltaX = count;
	m_angleDeltaX = SimMath::divf32(SimMath::subf32(desiredAngle, currentAngle), SimMath::sseFromInt32(m_numAngleDeltaX));
	self->setModelConditionState(CombatNames::modelCondition("TOPPLED"), true); // RW 0x8ABCEF: bit 0
	doToppleObjFX(*self, "ToppleUpdate ToppleFX", m_data->m_treeToppleFX);
	if (!m_data->m_stumpName.empty())
	{
		GameLogic &logic = self->logic();
		const ThingTemplate *tmpl = logic.things().findTemplate(m_data->m_stumpName); // RW 0x6D1305
		if (Object *stump = tmpl ? logic.newObject(tmpl, nullptr, ObjectStatusMaskType{}) : nullptr) // RW 0x6D165E
		{
			stump->setPosition(self->getPosition());       // RW 0x70C201
			stump->setOrientation(self->getOrientation()); // RW 0x70C31E
			m_stumpID = stump->getID();
			// RW 0x8ABD89 .. 0x8ABDA7: when the owner's drawable has +0x262 bit 0 set (burned tint), sets model condition BURNED (bit 80) on the stump.
			// INFERENCE: on the logic side without a drawable, read from the owner's BURNED model condition / status.
			const int burnedCond = CombatNames::modelCondition("BURNED");
			const int burnedStatus = CombatNames::status("BURNED");
			if ((burnedCond >= 0 && self->testModelCondition(burnedCond)) || (burnedStatus >= 0 && self->testStatus((unsigned)burnedStatus)))
			{
				stump->setModelConditionState(burnedCond, true);
			}
		}
	}
}

// RW 0x008ABE65 (DONOR BFME2 0x008A878F)
UpdateSleepTime ToppleUpdate::update()
{
	if (m_toppleState == TOPPLE_UPRIGHT || m_toppleState == TOPPLE_DOWN)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *self = getObject();
	if (m_numAngleDeltaX != 0)
	{
		float basis[9];
		std::memcpy(basis, self->getBasis(), sizeof(basis));
		preRotateZ(basis, m_angleDeltaX);
		const Coord3D pos = *self->getPosition();
		self->Thing::setTransform(&pos, basis); // RW 0x70BA76
		--m_numAngleDeltaX;
	}
	float curVelToUse = m_angularVelocity;
	if (SimMath::addf32(m_angularAccumulation, curVelToUse) > kToppleAngularLimit)
	{
		curVelToUse = SimMath::subf32(kToppleAngularLimit, m_angularAccumulation);
	}
	{
		float basis[9];
		std::memcpy(basis, self->getBasis(), sizeof(basis));
		preRotateX(basis, SimMath::pc24Mul(SimMath::subf32(0.0f, curVelToUse), m_toppleDirection.y));
		preRotateY(basis, SimMath::pc24Mul(curVelToUse, m_toppleDirection.x));
		const Coord3D pos = *self->getPosition();
		self->Thing::setTransform(&pos, basis); // RW 0x70BA76
	}
	m_angularAccumulation = SimMath::addf32(m_angularAccumulation, curVelToUse);
	if (m_angularAccumulation >= kToppleAngularLimit && m_angularVelocity > 0.0f)
	{
		const float bounceVel = SimMath::subf32(m_data->m_bounceVelocityPercent, m_bounceVelocityDecay);
		const float bounceToUse = kToppleMinBounceVel > bounceVel ? kToppleMinBounceVel : bounceVel;
		m_angularVelocity = SimMath::subf32(0.0f, SimMath::mulf32(bounceToUse, m_angularVelocity));
		m_bounceVelocityDecay = SimMath::addf32(m_bounceVelocityDecay, kToppleBounceDecayStep);
		const double absVel = SimMath::absD((double)m_angularVelocity);
		if ((m_options & TOPPLE_OPTIONS_NO_BOUNCE) != 0 || (double)kToppleBounceLimit > absVel)
		{
			m_angularVelocity = 0.0f;
			m_toppleState = TOPPLE_DOWN;
			if (m_data->m_killWhenToppled)
			{
				toppleKillObject(*self); // RW 0x8AB990
				if (m_data->m_reorientToppledRubble)
				{
					const float h = ObjectGeometry::maxHeightAbovePosition(self->geometryShapes()); // RW 0xAD1920
					const float *b = self->getBasis();
					const Coord3D *p = self->getPosition();
					const Coord3D newPos{
						SimMath::addf32(SimMath::mulf32(b[2], h), p->x),
						SimMath::addf32(SimMath::mulf32(b[5], h), p->y),
						SimMath::addf32(SimMath::mulf32(b[8], h), p->z)
					};
					self->setPosition(&newPos);       // RW 0x70C201
					self->setOrientation(self->getOrientation()); // RW 0x70C31E
				}
			}
			if (m_data->m_killStumpWhenToppled)
			{
				if (Object *stump = self->logic().findObjectByID(m_stumpID))
				{
					toppleKillObject(*stump); // RW 0x8AB990
				}
			}
		}
		else if (absVel >= (double)kToppleBounceSoundLimit && (m_options & TOPPLE_OPTIONS_NO_FX) == 0)
		{
			doToppleObjFX(*self, "ToppleUpdate BounceFX", m_data->m_bounceFX);
		}
	}
	else
	{
		m_angularVelocity = SimMath::addf32(m_angularVelocity, m_angularAcceleration);
	}
	if (ObjectClientHooks *client = self->clientHooks())
	{
		client->setShadowsEnabled(*self, false); // RW 0x672D28(0)
	}
	return UPDATE_SLEEP_NONE;
}

void ToppleUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addFloat(m_angularVelocity);
	h.addFloat(m_angularAcceleration);
	h.addFloat(m_toppleDirection.x);
	h.addFloat(m_toppleDirection.y);
	h.addFloat(m_toppleDirection.z);
	h.addI32((int)m_toppleState);
	h.addFloat(m_angularAccumulation);
	h.addFloat(m_angleDeltaX);
	h.addI32(m_numAngleDeltaX);
	h.addBool(m_doBounceFX);
	h.addU32(m_options);
	h.addU32((std::uint32_t)m_stumpID);
	h.addFloat(m_bounceVelocityDecay);
}

// RW 0x008ABDBD (DONOR BFME2 0x008A86E7)
// OpenBFME native snapshot uses version 2 (version 1 was UnportedUpdateModule::xfer: UpdateModule + calls counter).
void ToppleUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x);
	if (v.current < 2)
	{
		std::uint32_t legacyCalls = 0;
		x.xferUnsignedInt(&legacyCalls);
		m_angularVelocity = 0.0f;
		m_angularAcceleration = 0.0f;
		m_toppleDirection = Coord3D{ 0.0f, 0.0f, 0.0f };
		m_toppleState = TOPPLE_UPRIGHT;
		m_angularAccumulation = 0.0f;
		m_angleDeltaX = 0.0f;
		m_numAngleDeltaX = 0;
		m_doBounceFX = false;
		m_options = TOPPLE_OPTIONS_NONE;
		m_stumpID = INVALID_ID;
		m_bounceVelocityDecay = 0.0f;
		return;
	}
	x.xferReal(&m_angularVelocity);
	x.xferReal(&m_angularAcceleration);
	SnapshotXfer::coord3D(x, m_toppleDirection);
	SnapshotXfer::enumIn(x, m_toppleState, TOPPLE_UPRIGHT, TOPPLE_DOWN, "a ToppleUpdate's state");
	x.xferReal(&m_angularAccumulation);
	x.xferReal(&m_angleDeltaX);
	x.xferInt(&m_numAngleDeltaX);
	x.xferBool(&m_doBounceFX);
	SnapshotXfer::maskWithin(x, m_options, (std::uint64_t)(TOPPLE_OPTIONS_NO_BOUNCE | TOPPLE_OPTIONS_NO_FX), "a ToppleUpdate's options", "TOPPLE_OPTIONS");
	x.xferObjectID(&m_stumpID);
	x.xferReal(&m_bounceVelocityDecay);
}
