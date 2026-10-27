// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectCreationList. See GameLogic/ObjectCreationList.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/ObjectCreationList.h"

#include "Common/BuildAssistant.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ScoreKeeper.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/SimMath.h"

#include <cstddef>

thread_local ObjectCreationListStore *TheObjectCreationListStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
#include "Common/SpecialPowerNames.inc"

const char *const kOCLCpp = "ObjectCreationList.cpp";

// a row the port does not read: its text is kept under its name (S-530)
void parseKept(INI *ini, void *instance, void *, const void *userData)
{
	std::string line;
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		line += line.empty() ? "" : " ";
		line += t;
	}
	static_cast<OCLNugget *>(instance)->otherFields[static_cast<const char *>(userData)] = line;
}
void parseNames(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVector(ini, nullptr, &static_cast<OCLNugget *>(instance)->objectNames, nullptr); // RW 0x42EED6
}

#define N_OFF(member) (int)offsetof(OCLNugget, member)
#define KEPT(name) { name, parseKept, name, 0 }
// RW 0xBF6E50 (the generic nugget, 40 rows, in the binary's order)
void parseDisposition(INI *ini, void *instance, void *store, const void *)
{
	const char *names[33] = {};
	int i = 0;
	for (; kOCLDispositionNames[i]; ++i) names[i] = kOCLDispositionNames[i];
	if (ini->bfme1Data())
	{
		names[13] = "SET_ANGLE"; // community PR #36: BFME1's name of disposition bit 13 (BFME1 data only, Common/BfmeGame.h)
	}
	INI::parseBitString32(ini, instance, store, names);
}

// RW 0x4B8C21 -> 0x4B8B37: the 19 word model condition flag array (StartingConditions)
void parseConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 19> *>(store)->data(), 19, TheModelConditionNames);
}

const FieldParse kGenericFieldParse[] = {
	{ "PutInContainer", INI::parseAsciiString, nullptr, N_OFF(putInContainer) },           // + 0x10
	{ "ParticleSystem", INI::parseAsciiString, nullptr, N_OFF(particleSystem) },           // + 0x24
	{ "Count", INI::parseInt, nullptr, N_OFF(count) },                                     // + 0x28
	{ "OrientInForceDirection", INI::parseBool, nullptr, N_OFF(orientInForceDirection) }, // + 0xAA
	{ "ExtraBounciness", INI::parseReal, nullptr, N_OFF(extraBounciness) },               // + 0x2C
	{ "ExtraFriction", INI::parseVelocityReal, nullptr, N_OFF(extraFriction) },           // + 0x30 (RW 0x73A4B6)
	{ "Offset", INI::parseCoord3D, nullptr, N_OFF(offset) },                               // + 0x34
	{ "Disposition", parseDisposition, kOCLDispositionNames, N_OFF(disposition) },         // + 0x40
	{ "DispositionIntensity", INI::parseVelocityReal, nullptr, N_OFF(dispositionIntensity) }, // + 0x44 (RW 0x73A4B6)
	{ "DispositionAngle", INI::parseAngleReal, nullptr, N_OFF(dispositionAngle) },        // + 0x48 (RW 0x42EE15)
	{ "VelocityScale", INI::parseReal, nullptr, N_OFF(velocityScale) },                   // + 0x4C
	{ "MinForceMagnitude", INI::parseReal, nullptr, N_OFF(minForceMagnitude) },           // + 0x50
	{ "MaxForceMagnitude", INI::parseReal, nullptr, N_OFF(maxForceMagnitude) },           // + 0x54
	{ "MinForcePitch", INI::parseAngleReal, nullptr, N_OFF(minForcePitch) },              // + 0x58
	{ "MaxForcePitch", INI::parseAngleReal, nullptr, N_OFF(maxForcePitch) },              // + 0x5C
	{ "MinLifetime", INI::parseDurationUnsignedInt, nullptr, N_OFF(minLifetime) },        // + 0x60 (RW 0x73A429)
	{ "MaxLifetime", INI::parseDurationUnsignedInt, nullptr, N_OFF(maxLifetime) },        // + 0x64
	{ "SpreadFormation", INI::parseBool, nullptr, N_OFF(spreadFormation) },               // + 0xAB
	{ "MinDistanceAFormation", INI::parseReal, nullptr, N_OFF(minDistanceAFormation) },   // + 0x80
	{ "MinDistanceBFormation", INI::parseReal, nullptr, N_OFF(minDistanceBFormation) },   // + 0x84
	{ "MaxDistanceFormation", INI::parseReal, nullptr, N_OFF(maxDistanceFormation) },     // + 0x88
	{ "FadeIn", INI::parseBool, nullptr, N_OFF(fadeIn) },                                 // + 0xAC
	{ "FadeOut", INI::parseBool, nullptr, N_OFF(fadeOut) },                               // + 0xAD
	{ "FadeTime", INI::parseDurationUnsignedInt, nullptr, N_OFF(fadeTime) },              // + 0x78
	{ "FadeSound", INI::parseAsciiString, nullptr, N_OFF(fadeSound) },                    // + 0x7C (RW 0x73B217: an audio event name, client)
	{ "PreserveLayer", INI::parseBool, nullptr, N_OFF(preserveLayer) },                   // + 0x9D
	{ "IgnoreAllObjects", INI::parseBool, nullptr, N_OFF(ignoreAllObjects) },             // + 0x9E
	{ "IgnoreEnemyUnits", INI::parseBool, nullptr, N_OFF(ignoreEnemyUnits) },             // + 0xA0
	{ "IgnoreAllyUnits", INI::parseBool, nullptr, N_OFF(ignoreAllyUnits) },               // + 0x9F
	{ "StartingConditions", parseConditions, nullptr, N_OFF(startingConditions) },        // + 0xB4
	{ "IssueMoveAfterCreation", INI::parseBool, nullptr, N_OFF(issueMoveAfterCreation) }, // + 0x100
	{ "OrientInPrimaryDirection", INI::parseBool, nullptr, N_OFF(orientInPrimaryDirection) }, // + 0x101
	{ "OrientInSecondaryDirection", INI::parseBool, nullptr, N_OFF(orientInSecondaryDirection) }, // + 0x102
	{ "OrientationOffset", INI::parseAngleReal, nullptr, N_OFF(orientationOffset) },      // + 0x104 (RW 0x42EE15)
	{ "DestinationPlayer", INI::parseAsciiString, nullptr, N_OFF(destinationPlayer) },    // + 0x108
	{ "MoveUsesStrafeUpdate", INI::parseBool, nullptr, N_OFF(moveUsesStrafeUpdate) },     // + 0x103
	{ "OffsetInLocalSpace", INI::parseBool, nullptr, N_OFF(offsetInLocalSpace) },         // + 0x110
	{ "ClearRemovables", INI::parseBool, nullptr, N_OFF(clearRemovables) },               // + 0x111
	{ "RequiredUpgrades", INI::parseAsciiStringVector, nullptr, N_OFF(requiredUpgrades) },   // + 0x114 (RW 0x42EED6)
	{ "ForbiddenUpgrades", INI::parseAsciiStringVector, nullptr, N_OFF(forbiddenUpgrades) }, // + 0x120
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0xBF74F8 (CreateObject, 17 rows)
const FieldParse kCreateObjectFieldParse[] = {
	{ "ContainInsideSourceObject", INI::parseBool, nullptr, N_OFF(containInsideSourceObject) }, // + 0x9C
	{ "ObjectNames", parseNames, nullptr, 0 },                                                  // + 0x04
	{ "ObjectCount", INI::parseInt, nullptr, N_OFF(objectCount) },                              // + 0x8C
	{ "InheritsVeterancy", INI::parseBool, nullptr, N_OFF(inheritsVeterancy) },                 // + 0xAE
	{ "VeterancyLevel", INI::parseInt, nullptr, N_OFF(veterancyLevel) },                        // + 0xB0
	{ "SkipIfSignificantlyAirborne", INI::parseBool, nullptr, N_OFF(skipIfSignificantlyAirborne) }, // + 0xAF
	{ "InvulnerableTime", INI::parseDurationUnsignedInt, nullptr, N_OFF(invulnerableTime) },    // + 0x6C
	{ "StartingBusyTime", INI::parseDurationUnsignedInt, nullptr, N_OFF(startingBusyTime) },    // + 0xA4
	{ "MinHealth", INI::parsePercentToReal, nullptr, N_OFF(minHealth) },                        // + 0x70 (RW 0x42EEFA)
	{ "MaxHealth", INI::parsePercentToReal, nullptr, N_OFF(maxHealth) },                        // + 0x74
	{ "RequiresLivePlayer", INI::parseBool, nullptr, N_OFF(requiresLivePlayer) },               // + 0x90
	{ "IgnoreCommandPointLimit", INI::parseBool, nullptr, N_OFF(ignoreCommandPointLimit) },     // + 0x91
	{ "InheritAttributesFromSource", INI::parseBool, nullptr, N_OFF(inheritAttributesFromSource) }, // + 0x92
	{ "UseJustBuiltFlag", INI::parseBool, nullptr, N_OFF(useJustBuiltFlag) },                   // + 0x94
	{ "JustBuiltDuration", INI::parseDurationUnsignedInt, nullptr, N_OFF(justBuiltDuration) },  // + 0x98
	{ "InheritScriptingName", INI::parseBool, nullptr, N_OFF(inheritScriptingName) },           // + 0x93
	{ "WaypointSpawnPoints", INI::parseAsciiString, nullptr, N_OFF(waypointSpawnPoints) },      // + 0x10C
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0xBF7640 (CreateDebris)
const FieldParse kCreateDebrisFieldParse[] = {
	{ "ModelNames", parseNames, nullptr, 0 },
	KEPT("AnimationSet"), KEPT("FXFinal"), KEPT("OkToChangeModelColor"), KEPT("Shadow"),
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0xBF72A8 (ApplyRandomForce), RW 0xBF71F8 (FireWeapon), RW 0xBF7258 (Attack)
const FieldParse kForceFieldParse[] = { KEPT("MinForceMagnitude"), KEPT("MaxForceMagnitude"), KEPT("MinForcePitch"), KEPT("MaxForcePitch"), { nullptr, nullptr, nullptr, 0 } };
const FieldParse kFireWeaponFieldParse[] = { KEPT("Weapon"), { nullptr, nullptr, nullptr, 0 } };
const FieldParse kAttackFieldParse[] = { KEPT("NumberOfShots"), KEPT("WeaponSlot"), KEPT("DeliveryDecal"), KEPT("DeliveryDecalRadius"), { nullptr, nullptr, nullptr, 0 } };
#undef KEPT
#undef N_OFF

void parseNugget(INI *ini, ObjectCreationList *list, OCLNugget::Kind kind)
{
	OCLNugget n; // RW 0x5F0689 defaults
	n.kind = kind;
	MultiIniFieldParse multi;
	switch (kind)
	{
	case OCLNugget::CREATE_OBJECT:
		multi.add(kGenericFieldParse);
		multi.add(kCreateObjectFieldParse);
		break;
	case OCLNugget::CREATE_DEBRIS:
		multi.add(kGenericFieldParse);
		multi.add(kCreateDebrisFieldParse);
		break;
	case OCLNugget::APPLY_RANDOM_FORCE:
		multi.add(kForceFieldParse);
		break;
	case OCLNugget::FIRE_WEAPON:
		multi.add(kFireWeaponFieldParse);
		break;
	case OCLNugget::ATTACK:
		multi.add(kAttackFieldParse);
		break;
	}
	ini->initFromINIMulti(&n, multi);
	list->m_nuggets.push_back(std::move(n)); // RW 0x5F042E
}

void nuggetCreateObject(INI *ini, void *instance, void *, const void *) { parseNugget(ini, static_cast<ObjectCreationList *>(instance), OCLNugget::CREATE_OBJECT); }
void nuggetCreateDebris(INI *ini, void *instance, void *, const void *) { parseNugget(ini, static_cast<ObjectCreationList *>(instance), OCLNugget::CREATE_DEBRIS); }
void nuggetForce(INI *ini, void *instance, void *, const void *) { parseNugget(ini, static_cast<ObjectCreationList *>(instance), OCLNugget::APPLY_RANDOM_FORCE); }
void nuggetFireWeapon(INI *ini, void *instance, void *, const void *) { parseNugget(ini, static_cast<ObjectCreationList *>(instance), OCLNugget::FIRE_WEAPON); }
void nuggetAttack(INI *ini, void *instance, void *, const void *) { parseNugget(ini, static_cast<ObjectCreationList *>(instance), OCLNugget::ATTACK); }

// RW 0xBF6970
const FieldParse kNuggetTable[] = {
	{ "CreateObject", nuggetCreateObject, nullptr, 0 },   // RW 0x5F296A
	{ "CreateDebris", nuggetCreateDebris, nullptr, 0 },   // RW 0x5F2C59
	{ "ApplyRandomForce", nuggetForce, nullptr, 0 },      // RW 0x5F063E
	{ "FireWeapon", nuggetFireWeapon, nullptr, 0 },       // RW 0x5F05B8
	{ "Attack", nuggetAttack, nullptr, 0 },               // RW 0x5F05ED
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

ObjectCreationListStore::~ObjectCreationListStore()
{
	if (TheObjectCreationListStore == this)
	{
		TheObjectCreationListStore = nullptr;
	}
}

// RW 0x5F0462
void ObjectCreationListStore::parseObjectCreationListDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	const NameKeyType key = m_keys.nameToKey(name);
	auto it = m_lists.find(key);
	if (it != m_lists.end() && ini->getLoadType() == INI_LOAD_RELOAD)
	{
		it->second->m_retired = true; // RW 0x5F04DE
		m_retired.push_back(it->second);
	}
	auto list = std::make_shared<ObjectCreationList>();
	list->m_name = name;
	m_lists[key] = list; // RW 0x5F0537: the new list replaces the entry
	ini->initFromINI(list.get(), kNuggetTable);
}

void ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(INI *ini)
{
	if (!TheObjectCreationListStore)
	{
		throw INIException(3, "TheObjectCreationListStore==NULL");
	}
	TheObjectCreationListStore->parseObjectCreationListDefinition(ini);
}

const ObjectCreationList *ObjectCreationListStore::findObjectCreationList(const std::string &name) const
{
	const NameKeyType key = m_keys.findKey(name);
	if (key == NAMEKEY_INVALID)
	{
		return nullptr;
	}
	auto it = m_lists.find(key);
	return it == m_lists.end() ? nullptr : it->second.get();
}

namespace
{
constexpr float kTwoPi = 6.2831855f;        // RW 0xBDD38C
constexpr float kPi = 3.14159274101257324f; // RW 0xBDD388
constexpr float kAboveEverything = 99999.0f; // RW 0xBDF350
constexpr float kHundred = 100.0f;          // RW 0xBD88D8

// RW 0x644FD0: into (-pi, pi] (SSE float compares and steps)
float normalizeAngle(float a)
{
	while (a > kPi)
	{
		a = SimMath::subf32(a, kTwoPi);
	}
	while (-kPi >= a)
	{
		a = SimMath::addf32(a, kTwoPi);
	}
	return a;
}

// the parts of RW 0x5F275D / 0x5F0EE6 / 0x5F1B5A the port does not run: counted on the list and noted once per kind (S-530)
void unported(GameLogic &logic, unsigned long long &counter, const char *what)
{
	++counter;
	logic.noteStop(std::string("[S-530] ObjectCreationList: not run: ") + what);
}

float random(GameLogic &logic, float lo, float hi, int line)
{
	return logic.random().getValueReal(lo, hi, kOCLCpp, line); // RW 0x6D332C
}

// RW 0x5F0DC5: a ForbiddenUpgrades name the source or its player has stops the nugget; with RequiredUpgrades one of them must be had (an unknown name is
// skipped: TheUpgradeCenter RW 0x66F5E5 null). The object's test is RW 0x68E040, the player's RW 0x6AB2B1 (+ 0x14C completed bits), read without a null test
bool upgradesAllow(const OCLNugget &n, const Object &source)
{
	const Player *player = source.getControllingPlayer();
	auto has = [&](const std::string &name) {
		const UpgradeTemplate *u = Player::resolveUpgrade(name, false);
		return u && (source.hasUpgrade(u) || (player && player->hasUpgradeComplete(u)));
	};
	for (const std::string &name : n.forbiddenUpgrades)
	{
		if (has(name))
		{
			return false;
		}
	}
	if (n.requiredUpgrades.empty())
	{
		return true;
	}
	for (const std::string &name : n.requiredUpgrades)
	{
		if (has(name))
		{
			return true;
		}
	}
	return false;
}

bool kindBit(GameLogic &logic, const ThingTemplate &tt, const char *kind)
{
	const int k = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	return k >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)k);
}

// RW 0x6ABAF7(1, &tt, 1, &count, 0) -> RW 0x7A0440: the player's teams' members of the template's kind (RW 0x73D5C2) that are not effectively dead (+ 0x458
// bit 0); objects under construction count
int countAliveOfType(GameLogic &logic, const Player &player, const ThingTemplate &tt)
{
	int count = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Team *team = o->getTeam();
		if (team && team->getControllingPlayer() == &player && !o->isEffectivelyDead() && ScriptConditions::equivalentTemplates(o->getTemplate(), &tt))
		{
			++count;
		}
	}
	return count;
}

// RW 0x5EF9C3 (minMag, maxMag, minPitch, maxPitch): a random force (the identity scaled by the magnitude, turned by the yaw about z, then by -pitch)
Coord3D randomForce(GameLogic &logic, const OCLNugget &n)
{
	using namespace SimMath;
	const float yaw = random(logic, 0.0f, kTwoPi, 0x245);
	const float pitch = random(logic, n.minForcePitch, n.maxForcePitch, 0x246);
	const float mag = random(logic, n.minForceMagnitude, n.maxForceMagnitude, 0x247);
	const float z0 = mulf32(mag, 0.0f); // RW 0x5EFA40: the off-diagonal terms (mag * 0)
	// rows (m00 m01 m02) (m10 m11 m12) (m20 m21 m22) = (mag z0 z0) (z0 mag z0) (z0 z0 mag)
	const float c = SimMath::cosf32(yaw), sn = SimMath::sinf32(yaw); // MSVCR71 cos / sin RW 0xA3CF84 / 0xA3CF90 of the double
	const float a0 = addf32(mulf32(z0, sn), mulf32(mag, c));
	const float a1 = addf32(mulf32(mag, sn), mulf32(z0, c));
	const float a2 = addf32(mulf32(z0, sn), mulf32(z0, c));
	const float negPitch = -pitch; // fchs
	const float sp = SimMath::sinf32(negPitch), cp = SimMath::cosf32(negPitch);
	return Coord3D{ subf32(mulf32(cp, a0), mulf32(z0, sp)), subf32(mulf32(a1, cp), mulf32(z0, sp)), subf32(mulf32(a2, cp), mulf32(mag, sp)) };
}

void fling(Object &obj, const Coord3D &force)
{
	if (PhysicsBehavior *p = PhysicsBehavior::find(obj))
	{
		p->fling(force); // RW 0x792DBD(force, 0, 0)
	}
}

// RW 0x68B34C: the locomotor's current speed when it is above 0, else 0 (no AI or no locomotor: 0)
float speedOf(const Object &o)
{
	const AIUpdateInterface *ai = o.getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	return loco && loco->speed() > 0.0f ? loco->speed() : 0.0f;
}

// the direction a source moves in times its speed (RW 0x70BA38: the transform's x axis, column 0 of the basis; RW 0x68B34C)
Coord3D velocityOf(const Object &source)
{
	const float *b = source.getBasis();
	const float speed = speedOf(source);
	return Coord3D{ SimMath::mulf32(b[0], speed), SimMath::mulf32(b[3], speed), SimMath::mulf32(b[6], speed) };
}
} // namespace

// RW 0x5F1B5A: what the nugget does to one object it made (see the header)
void ObjectCreationList::doStuffToObj(GameLogic &logic, const OCLNugget &n, Object *obj, const Coord3D &where, float angle, const Object *source, unsigned lifetime, int index) const
{
	using namespace SimMath;
	if (!obj)
	{
		return;
	}
	obj->clearAndSetModelConditionFlags(Object::ModelConditionBits{}, n.startingConditions); // RW 0x5F1B9E: RW 0x68D607(empty, StartingConditions)
	if (LifetimeUpdate *life = dynamic_cast<LifetimeUpdate *>(obj->findModule("LifetimeUpdate"))) // RW 0x5F1BD7
	{
		if (lifetime != 0)
		{
			life->setLifetimeRange(lifetime, lifetime); // RW 0x7A7E41
		}
		else if (n.maxLifetime > 0)
		{
			life->setLifetimeRange(n.minLifetime, n.maxLifetime);
		}
	}
	if (n.useJustBuiltFlag)
	{
		obj->setSpecialModelConditionState(0xDA, n.justBuiltDuration); // RW 0x5F1D12: JUST_BUILT (RW 0x68B581)
	}
	if (n.startingBusyTime > 0 && obj->getAIUpdateInterface())
	{
		unported(logic, m_unportedUses, "StartingBusyTime (AI command 0x31 through AI vslot 0x140, RW 0x5F1D2E)");
	}
	Coord3D offset = n.offset;
	if (n.offsetInLocalSpace && source)
	{
		unported(logic, m_unportedUses, "OffsetInLocalSpace (the offset turned by the source's transform, RW 0x5E07F7)");
	}
	// RW 0x5F1D9E (SSE): the spot; ON_GROUND_ALIGNED and SEND_IT_OUT write their height into it (ebp - 0x28), the later setPosition calls see it
	Coord3D pos{ addf32(where.x, offset.x), addf32(where.y, offset.y), addf32(where.z, offset.z) };
	// RW 0x5F1DC5: ParticleSystem (client)
	const float health = random(logic, n.minHealth, n.maxHealth, 0x50A); // RW 0x5F1E70: drawn for every object
	if (BodyModuleInterface *body = obj->getBodyModule())
	{
		body->setInitialHealthPercent(mulf32(health, kHundred)); // body vslot 0x58 (RW 0x5F1E98)
	}
	if (SlavedUpdateInterface *slaved = SlavedUpdateInterface::of(*obj)) // RW 0x5F1EA6: the first module whose behaviour slot 0x68 answers
	{
		slaved->onEnslave(source); // RW 0x5F1EC4: slot 1
	}
	if (n.invulnerableTime > 0)
	{
		unported(logic, m_unportedUses, "InvulnerableTime (RW 0x68C6C8: RW 0x68B303 / 0x8E3ABB)");
	}
	Coord3D force{ 0.0f, 0.0f, 0.0f };
	const unsigned d = n.disposition;
	if ((d & OCLNugget::SPAWNED_MODELCONDITION_TRIGGER) && source && obj->findModule("SpawnedModelConditionCreate"))
	{
		unported(logic, m_unportedUses, "SPAWNED_MODELCONDITION_TRIGGER (SpawnedModelConditionCreate RW 0x7A7960)");
	}
	if (d & OCLNugget::LIKE_EXISTING)
	{
		obj->setOrientation(angle); // RW 0x5F1F71 (with OffsetInLocalSpace: the source's transform RW 0x70BA76, counted above)
		obj->setPosition(&pos);     // + 0x1C0 = angle (RW 0x5F1F7E: the drawable's interpolation angle, client)
	}
	else if (d & OCLNugget::ABSOLUTE_ANGLE)
	{
		obj->setOrientation(n.dispositionAngle); // RW 0x5F1F93
		obj->setPosition(&pos);
	}
	else if ((d & OCLNugget::RELATIVE_ANGLE) && source)
	{
		obj->setOrientation(normalizeAngle(addf32(n.dispositionAngle, source->getOrientation()))); // RW 0x5F1FA6 .. 0x5F1FCE
		obj->setPosition(&pos);
	}
	else if (d & OCLNugget::SPAWN_AROUND)
	{
		// RW 0x5F1FE4: a = (float)index * DispositionAngle (fild, fmul, fstp dword); s / c the MSVCR71 sin / cos of a (double -> float); the spot is
		// (c * r - s * 0) + x, (r * s + c * 0) + y with r = DispositionIntensity (SSE)
		const float a = fstpDword(pc24MulW((double)index, (double)n.dispositionAngle));
		const float sn = SimMath::sinf32(a), c = SimMath::cosf32(a), r = n.dispositionIntensity;
		const Coord3D around{ addf32(subf32(mulf32(c, r), mulf32(sn, 0.0f)), pos.x), addf32(addf32(mulf32(r, sn), mulf32(c, 0.0f)), pos.y), pos.z };
		obj->setOrientation(a);
		obj->setPosition(&around);
	}
	if (d & OCLNugget::ANIMATED)
	{
		obj->setModelConditionState(5, true); // RW 0x5F2086: Object + 0x10C bit 5, then the drawable refresh (RW 0x68B53C); nothing else is done
		return;
	}
	PhysicsBehavior *physics = PhysicsBehavior::find(*obj); // Object + 0x264
	if ((d & OCLNugget::INHERIT_VELOCITY) && source && physics)
	{
		// RW 0x5F20B7 .. 0x5F2105: the source's x axis * its speed added to the force (SSE)
		const Coord3D v = velocityOf(*source);
		force = Coord3D{ addf32(v.x, force.x), addf32(v.y, force.y), addf32(v.z, force.z) };
	}
	const bool sourcePhysics = source && PhysicsBehavior::find(*const_cast<Object *>(source)) != nullptr; // source + 0x264
	for (unsigned impact : { (unsigned)OCLNugget::FORWARD_IMPACT, (unsigned)OCLNugget::REVERSE_IMPACT })
	{
		if (!(d & impact) || !sourcePhysics || !physics)
		{
			continue;
		}
		// RW 0x5F2126 / 0x5F2183: the source's x axis (RW 0x70BA38) times its speed (RW 0x68B34C) * VelocityScale (* -1.0, RW 0xBD19DC, for REVERSE_IMPACT)
		// on the x87 (PC24), each component stored as a float, flung at once
		const float *b = source->getBasis();
		double k = pc24MulW((double)speedOf(*source), (double)n.velocityScale);
		if (impact == OCLNugget::REVERSE_IMPACT)
		{
			k = pc24MulW(k, -1.0);
		}
		fling(*obj, Coord3D{ fstpDword(pc24MulW((double)b[0], k)), fstpDword(pc24MulW((double)b[3], k)), fstpDword(pc24MulW((double)b[6], k)) });
	}
	if ((d & OCLNugget::BUILDING_CHUNKS) && physics)
	{
		// RW 0x5F21D6: (offset.x, offset.y, 0) normalised (RW 0x403175) * VelocityScale * DispositionIntensity, flung at once
		Coord3D dir{ offset.x, offset.y, 0.0f };
		const float len = (float)SimMath::length3d(dir.x, dir.y, dir.z); // RW 0x403175 Coord3D::normalize: the length as a float, 1 / it, three SSE products
		if (len != 0.0f)
		{
			const float inv = divf32(1.0f, len);
			dir = Coord3D{ mulf32(dir.x, inv), mulf32(dir.y, inv), mulf32(dir.z, inv) };
		}
		const float k = mulf32(n.velocityScale, n.dispositionIntensity);
		fling(*obj, Coord3D{ mulf32(dir.x, k), mulf32(dir.y, k), mulf32(k, dir.z) });
	}
	if ((d & OCLNugget::FADE_AND_DIE_ORNAMENT) && obj->findModule("FadeAndDieOrnamentUpdate"))
	{
		unported(logic, m_unportedUses, "FADE_AND_DIE_ORNAMENT (FadeAndDieOrnamentUpdate RW 0x7A76CF)");
	}
	if (d & OCLNugget::ON_GROUND_ALIGNED)
	{
		// RW 0x5F2296: z = 99999 (RW 0xBDF350); the layer for it (RW 0x680B70: the ground, S-161); a random angle (line 0x5B6); the water surface with
		// USE_WATER_SURFACE when the spot is under water (TheTerrainLogic vslot 0x4C), else the layer's height (vslot 0x1C: the ground's); + 1.0 off the ground
		pos.z = kAboveEverything;
		obj->setOrientation(random(logic, 0.0f, kTwoPi, 0x5B6));
		bool onWater = false;
		if (d & OCLNugget::USE_WATER_SURFACE)
		{
			AIWorld *ai = logic.aiWorld();
			const PathfindTerrain *terrain = ai ? ai->pathfinder().terrainView() : nullptr;
			if (!terrain)
			{
				unported(logic, m_unportedUses, "USE_WATER_SURFACE without a pathfinder terrain (TheTerrainLogic vslot 0x4C)");
			}
			else
			{
				float waterZ = pos.z;
				onWater = terrain->isUnderwater(pos.x, pos.y, &waterZ, nullptr);
				if (onWater)
				{
					pos.z = waterZ;
				}
			}
		}
		if (!onWater)
		{
			pos.z = logic.getGroundHeight(pos.x, pos.y);
		}
		obj->setPosition(&pos); // RW 0x5F2348: setLayer(ground) and the position
	}
	if (d & OCLNugget::SEND_IT_OUT)
	{
		// RW 0x5F2364: a random angle (line 0x5C5), the ground height (vslot 0x18), then two force terms in [-4 * intensity, 4 * intensity] (0x5CB, 0x5CC)
		obj->setOrientation(random(logic, 0.0f, kTwoPi, 0x5C5));
		pos.z = logic.getGroundHeight(pos.x, pos.y);
		obj->setPosition(&pos);
		const float k = mulf32(n.dispositionIntensity, 4.0f); // RW 0xBD88C0
		const float lo = subf32(0.0f, k);
		const float fx = random(logic, lo, k, 0x5CB);
		const float fy = random(logic, lo, k, 0x5CC);
		force.x = fstpDword(pc24AddW((double)force.x, (double)fx)); // x87 fadd, fstp dword
		force.y = fstpDword(pc24AddW((double)force.y, (double)fy));
		// OrientInForceDirection: retail computes atan2(fx, ...) and drops it (RW 0x5F2446: fstp st0)
	}
	if (d & (OCLNugget::SEND_IT_FLYING | OCLNugget::SEND_IT_UP | OCLNugget::RANDOM_FORCE))
	{
		obj->setPosition(&pos); // RW 0x5F2479 (with OffsetInLocalSpace: the source's transform first, counted above)
		if (physics)
		{
			physics->setAllowBouncing(true); // RW 0x5F248B: + 0x5D = 1
			Coord3D f;
			if (d & OCLNugget::SEND_IT_FLYING)
			{
				const float h = mulf32(n.dispositionIntensity, 4.0f), v = mulf32(n.dispositionIntensity, 3.0f); // RW 0xBD88C0 / 0xBDD42C
				const float lo = subf32(0.0f, h);
				f.x = random(logic, lo, h, 0x5E7);
				f.y = random(logic, lo, h, 0x5E8);
				f.z = random(logic, mulf32(v, 0.33f), v, 0x5E9); // RW 0xBF7348
			}
			else if (d & OCLNugget::SEND_IT_UP)
			{
				const float h = mulf32(n.dispositionIntensity, 2.0f), v = mulf32(n.dispositionIntensity, 4.0f); // RW 0xBD889C / 0xBD88C0
				const float lo = subf32(0.0f, h);
				f.x = random(logic, lo, h, 0x5F3);
				f.y = random(logic, lo, h, 0x5F4);
				f.z = random(logic, mulf32(v, 0.75f), v, 0x5F5); // RW 0xBDC628
			}
			else
			{
				f = randomForce(logic, n); // RW 0x5F25E9
			}
			force = Coord3D{ addf32(force.x, f.x), addf32(force.y, f.y), addf32(force.z, f.z) }; // RW 0x5F25F8 (SSE)
		}
	}
	if ((d & OCLNugget::FLOATING) && obj->findModule("FloatUpdate"))
	{
		unported(logic, m_unportedUses, "FLOATING (FloatUpdate + 0x20, RW 0x5F2688)");
	}
	// RW 0x5F268C: |force|^2 (SSE: z*z + y*y + x*x) above 0 (RW 0xC1B594) with a PhysicsBehavior flings the object
	const float f2 = addf32(addf32(mulf32(force.z, force.z), mulf32(force.y, force.y)), mulf32(force.x, force.x));
	if (f2 > 0.0f && physics)
	{
		physics->fling(force);
	}
	if (n.containInsideSourceObject)
	{
		// RW 0x5F26CC: the source's contain takes the object when it is a valid container for it (vslot 0x98(obj, 1, 0), 0x9C); else the object is destroyed
		Object *container = const_cast<Object *>(source); // retail's source is not const
		ContainModuleInterface *contain = container ? container->getContain() : nullptr;
		if (contain && contain->isValidContainerFor(*obj, true, false))
		{
			contain->addToContain(obj);
		}
		else
		{
			logic.destroyObject(obj); // RW 0x62BBAB
		}
	}
}

// RW 0x5F0EE6: the creation core of a CreateObject nugget (see the header)
std::vector<Object *> ObjectCreationList::createCore(GameLogic &logic, const OCLNugget &n, const Coord3D &where, float angle, const Object *source, unsigned lifetime) const
{
	std::vector<Object *> made;
	if (n.objectNames.empty())
	{
		return made; // RW 0x5F0F72
	}
	Player *sourcePlayer = source ? source->getControllingPlayer() : nullptr;
	if (n.requiresLivePlayer && (!sourcePlayer || sourcePlayer->isObserver() || sourcePlayer->isDefeated()))
	{
		return made; // RW 0x5F0F94: no source or its player not active (RW 0x6AAC52)
	}
	// RW 0x5F0FBF: DestinationPlayer's (RW 0x6A8466) or the source's player's default team (Player + 0x30C)
	Team *team = nullptr;
	if (!n.destinationPlayer.empty())
	{
		if (Player *p = logic.players().findPlayerWithName(n.destinationPlayer))
		{
			team = p->getDefaultTeam();
		}
	}
	else if (sourcePlayer)
	{
		team = sourcePlayer->getDefaultTeam();
	}
	if (!n.putInContainer.empty())
	{
		unported(logic, m_unportedUses, "PutInContainer (RW 0x5F101E)");
	}
	for (int i = 0; i < n.count; ++i)
	{
		const int idx = logic.random().getValue(0, (int)n.objectNames.size() - 1, kOCLCpp, 0x686); // RW 0x5F11CD
		const ThingTemplate *tt = logic.things().findTemplate(n.objectNames[(size_t)idx]);
		if (!tt)
		{
			continue; // RW 0x5F11FE
		}
		if (!n.ignoreCommandPointLimit && sourcePlayer && !sourcePlayer->canAffordCommandPoints(*tt))
		{
			break; // RW 0x5F123F -> 0x5F127A
		}
		const int maxOfType = BuildAssistant::maxSimultaneousOfType(*tt); // template + 0x5F0
		if (sourcePlayer && maxOfType != 0 && countAliveOfType(logic, *sourcePlayer, *tt) >= maxOfType)
		{
			break; // RW 0x5F1266 .. 0x5F1278
		}
		if (kindBit(logic, *tt, "OPTIMIZED_PROP") || kindBit(logic, *tt, "TREE") || kindBit(logic, *tt, "SHRUB"))
		{
			// RW 0x5F12BE: a template KindOf + 0x114 bit 4 (OPTIMIZED_PROP) or + 0x113 bit 6 / 7 (TREE / SHRUB) is no object: TheTerrainLogic adds a tree
			// (+ 0x110 bit 30, RW 0x683D89), a shrub (bit 31, RW 0x6808ED) or a prop (RW 0x67D742) at the position with the template's + 0x4F0 scale
			unported(logic, m_unportedUses, "a TREE / SHRUB / OPTIMIZED_PROP template (the terrain's props, RW 0x683D89 / 0x6808ED / 0x67D742)");
			continue;
		}
		if (n.clearRemovables)
		{
			unported(logic, m_unportedUses, "ClearRemovables (RW 0x796576 / 0x797465 / 0x684BCA)");
		}
		if (!team)
		{
			unported(logic, m_unportedUses, "an object without a team (no source player and no DestinationPlayer: RW 0x6D165E with a null team)");
			continue;
		}
		// RW 0x5F1340: the source player's score counting off while the object is made (ScoreKeeper + 0x110, RW 0x79DC9E)
		ScoreKeeper *keeper = sourcePlayer ? &sourcePlayer->getScoreKeeper() : nullptr;
		const bool counting = keeper && keeper->counting();
		if (keeper)
		{
			keeper->setCounting(false);
		}
		Object *o = logic.newObject(tt, team, ObjectStatusMaskType{}); // RW 0x5F1391 -> ThingFactory::newObject
		if (keeper)
		{
			keeper->setCounting(counting);
		}
		if (!o)
		{
			continue;
		}
		made.push_back(o); // RW 0x5F13A0
		// RW 0x5F13C8: InheritAttributesFromSource (the drawable, client)
		if (n.inheritScriptingName && source && !source->getName().empty() && n.count == 1)
		{
			o->setName(source->getName()); // RW 0x5F140D: TheScriptEngine RW 0x759467
		}
		// RW 0x5F1412: PreserveLayer keeps the source's layer (RW 0x68BBE0: the ground, S-161); no container is made (PutInContainer counted)
		Coord3D at = where;
		if (!n.waypointSpawnPoints.empty())
		{
			unported(logic, m_unportedUses, "WaypointSpawnPoints (RW 0x5F1A60)");
		}
		else if (n.spreadFormation)
		{
			// RW 0x5F14DA: the min radius drawn between MinDistanceAFormation and MinDistanceBFormation (line 0x719), MaxDistanceFormation, a random start
			// angle (RW 0xBF6968), maxZDelta 1e10 (RW 0xBF7328), FPF_USE_HIGHEST_LAYER and the ignore flags; with a source FindPositionAround from the
			// position, the source's position when no spot is found
			FindPositionOptions fpo;
			fpo.minRadius = random(logic, n.minDistanceAFormation, n.minDistanceBFormation, 0x719);
			fpo.maxRadius = n.maxDistanceFormation;
			fpo.startAngle = RANDOM_START_ANGLE;
			fpo.maxZDelta = 1e10f;
			unsigned flags = FPF_USE_HIGHEST_LAYER;
			if (n.ignoreAllObjects)
			{
				flags += FPF_IGNORE_ALL_OBJECTS; // RW 0x5F1554: add esi, 4
			}
			if (n.ignoreEnemyUnits)
			{
				flags |= FPF_IGNORE_ENEMY_UNITS;
			}
			if (n.ignoreAllyUnits)
			{
				flags |= FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS;
			}
			if (n.disposition & OCLNugget::USE_WATER_SURFACE)
			{
				flags |= FPF_IGNORE_WATER;
			}
			if (n.disposition & OCLNugget::USE_CLIFF)
			{
				flags |= FPF_CLEAR_CELLS_ONLY;
			}
			fpo.flags = flags;
			AIWorld *ai = logic.aiWorld();
			if (!ai || !ai->pathfinder().terrainView())
			{
				unported(logic, m_unportedUses, "SpreadFormation in a game without a pathfinder terrain (nothing for FindPositionAround to search)");
			}
			else if (source)
			{
				if (!FindPosition::findPositionAround(logic, where, fpo, at))
				{
					at = *source->getPosition(); // RW 0x5F15AC
				}
			}
			else
			{
				unported(logic, m_unportedUses, "SpreadFormation without a source (retail reads an unset position, RW 0x5F1592)");
			}
		}
		doStuffToObj(logic, n, o, at, angle, source, lifetime, i); // RW 0x5F15FB (an object ContainInsideSourceObject destroyed is only queued: the rest runs)
		// RW 0x5F1600: onBuildComplete with the source player's score counting off (retail reads the source's player without a null test)
		if (keeper)
		{
			keeper->setCounting(false);
		}
		o->friend_onBuildComplete(); // RW 0x5F1623 -> 0x68D252
		if (keeper)
		{
			keeper->setCounting(counting);
		}
		if (n.veterancyLevel > 0)
		{
			if (ExperienceTracker *xp = o->getExperienceTracker())
			{
				xp->gainExpForLevel(n.veterancyLevel, true, false); // RW 0x5F164A: RW 0x79DA0A(level, 1, 0)
			}
		}
		o->setProducer(source); // RW 0x5F1656: RW 0x68B6A1
		// RW 0x5F1661: a STRUCTURE (template + 0x108 bit 7) enters the pathfinder map (RW 0x6E85E9).
		// newObject only allocates the object; register after its final placement and orientation.
		if (o->isKindOf((unsigned)CombatNames::kinds().structure))
		{
			if (AIWorld *ai = logic.aiWorld()) ai->addObjectToPathfindMap(*o);
		}
		// RW 0x5F1678 .. 0x5F17B7: FadeIn / FadeOut (the FadeSound and the drawable fade), the drawable refresh: client
	}
	return made;
}

// RW 0x5F275D: a CreateObject nugget (vslot 0xC) for the primary object `source` at `where` (see the header)
std::vector<Object *> ObjectCreationList::createObjectNugget(GameLogic &logic, const OCLNugget &n, const Object *source, const Coord3D &where, const Coord3D *secondary, unsigned lifetime) const
{
	if (source && !upgradesAllow(n, *source))
	{
		return {}; // RW 0x5F2770
	}
	float angle = 0.0f;
	if (n.orientInPrimaryDirection)
	{
		if (source)
		{
			angle = SimMath::addf32(n.orientationOffset, source->getOrientation()); // RW 0x5F27A3: OrientationOffset + the source's angle (+ 0x44)
		}
		else
		{
			unported(logic, m_unportedUses, "OrientInPrimaryDirection without a source (retail reads it unchecked, RW 0x5F27A3)");
		}
	}
	std::vector<Object *> made = createCore(logic, n, where, angle, source, lifetime);
	if (!secondary)
	{
		return made;
	}
	if (n.issueMoveAfterCreation)
	{
		for (Object *o : made)
		{
			AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr; // RW 0x5F287F: + 0x260
			if (!ai)
			{
				continue;
			}
			if (n.moveUsesStrafeUpdate)
			{
				unported(logic, m_unportedUses, "MoveUsesStrafeUpdate (RW 0x7A82AF)");
			}
			ai->aiMoveToPosition(*secondary, CMD_FROM_PLAYER); // RW 0x5F28A7: AI command 0 from source 0 (RW 0x7536DD(0, 0)) through the AI's vslot 0x20
		}
	}
	if (n.orientInSecondaryDirection)
	{
		for (Object *o : made)
		{
			if (!o)
			{
				continue;
			}
			// RW 0x5F28F8: atan2(secondary.y - y, secondary.x - x) on doubles, RW 0x644FD0, setOrientation
			const float dy = SimMath::subf32(secondary->y, o->getPosition()->y), dx = SimMath::subf32(secondary->x, o->getPosition()->x);
			// The secondary-facing pass follows createCore's registration. Refresh the
			// footprint explicitly: this port's transform callback only updates unit cells.
			AIWorld *world = o->isKindOf((unsigned)CombatNames::kinds().structure) ? logic.aiWorld() : nullptr;
			if (world) world->removeObjectFromPathfindMap(*o);
			o->setOrientation(normalizeAngle(SimMath::fstpDword(SimMath::atan2d(dy, dx))));
			if (world) world->addObjectToPathfindMap(*o);
		}
	}
	return made;
}

// RW 0x5F00CA: every nugget's vslot 0xC in order
std::vector<Object *> ObjectCreationList::create(GameLogic &logic, const Object *source, const Coord3D &where, const Coord3D *secondary, unsigned lifetime) const
{
	std::vector<Object *> made;
	for (const OCLNugget &n : m_nuggets)
	{
		if (n.kind != OCLNugget::CREATE_OBJECT)
		{
			unported(logic, m_unportedUses, "CreateDebris / ApplyRandomForce / FireWeapon / Attack nuggets");
			continue;
		}
		for (Object *o : createObjectNugget(logic, n, source, where, secondary, lifetime))
		{
			made.push_back(o);
		}
	}
	return made;
}
