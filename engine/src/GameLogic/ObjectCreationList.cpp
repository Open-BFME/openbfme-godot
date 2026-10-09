// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectCreationList. See GameLogic/ObjectCreationList.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/ObjectCreationList.h"

#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
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
const FieldParse kGenericFieldParse[] = {
	{ "PutInContainer", INI::parseAsciiString, nullptr, N_OFF(putInContainer) },
	{ "ParticleSystem", INI::parseAsciiString, nullptr, N_OFF(particleSystem) },
	{ "Count", INI::parseInt, nullptr, N_OFF(count) },
	KEPT("OrientInForceDirection"), KEPT("ExtraBounciness"), KEPT("ExtraFriction"),
	{ "Offset", INI::parseCoord3D, nullptr, N_OFF(offset) },
	{ "Disposition", INI::parseBitString32, kOCLDispositionNames, N_OFF(disposition) },
	KEPT("DispositionIntensity"), KEPT("DispositionAngle"), KEPT("VelocityScale"), KEPT("MinForceMagnitude"), KEPT("MaxForceMagnitude"),
	KEPT("MinForcePitch"), KEPT("MaxForcePitch"), KEPT("MinLifetime"), KEPT("MaxLifetime"), KEPT("SpreadFormation"), KEPT("MinDistanceAFormation"),
	KEPT("MinDistanceBFormation"), KEPT("MaxDistanceFormation"), KEPT("FadeIn"), KEPT("FadeOut"), KEPT("FadeTime"), KEPT("FadeSound"),
	KEPT("PreserveLayer"), KEPT("IgnoreAllObjects"), KEPT("IgnoreEnemyUnits"), KEPT("IgnoreAllyUnits"), KEPT("StartingConditions"),
	KEPT("IssueMoveAfterCreation"), KEPT("OrientInPrimaryDirection"), KEPT("OrientInSecondaryDirection"), KEPT("OrientationOffset"),
	KEPT("DestinationPlayer"), KEPT("MoveUsesStrafeUpdate"), KEPT("OffsetInLocalSpace"), KEPT("ClearRemovables"), KEPT("RequiredUpgrades"),
	KEPT("ForbiddenUpgrades"),
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0xBF74F8 (CreateObject, 17 rows)
const FieldParse kCreateObjectFieldParse[] = {
	KEPT("ContainInsideSourceObject"),
	{ "ObjectNames", parseNames, nullptr, 0 },
	KEPT("ObjectCount"), KEPT("InheritsVeterancy"), KEPT("VeterancyLevel"), KEPT("SkipIfSignificantlyAirborne"), KEPT("InvulnerableTime"),
	KEPT("StartingBusyTime"), KEPT("MinHealth"), KEPT("MaxHealth"),
	{ "RequiresLivePlayer", INI::parseBool, nullptr, N_OFF(requiresLivePlayer) },
	{ "IgnoreCommandPointLimit", INI::parseBool, nullptr, N_OFF(ignoreCommandPointLimit) },
	KEPT("InheritAttributesFromSource"), KEPT("UseJustBuiltFlag"), KEPT("JustBuiltDuration"), KEPT("InheritScriptingName"), KEPT("WaypointSpawnPoints"),
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

// RW 0x5F00CA -> 0x5F275D -> 0x5F0EE6 (the ported part: see the header)
std::vector<Object *> ObjectCreationList::create(GameLogic &logic, const Object *source, const Coord3D &where) const
{
	std::vector<Object *> made;
	for (const OCLNugget &n : m_nuggets)
	{
		if (n.kind != OCLNugget::CREATE_OBJECT || n.objectNames.empty())
		{
			++m_unportedUses;
			continue;
		}
		if (!n.otherFields.empty() || !n.particleSystem.empty() || !n.putInContainer.empty() || n.disposition != 2)
		{
			++m_unportedUses; // fields whose effect is not ported (S-530); the objects are still made
		}
		Team *team = source ? source->getTeam() : nullptr;
		Player *owner = source ? source->getControllingPlayer() : nullptr;
		for (int i = 0; i < n.count; ++i)
		{
			const int idx = logic.random().getValue(0, (int)n.objectNames.size() - 1, kOCLCpp, 0x686); // RW 0x5F11CD
			const ThingTemplate *tt = logic.things().findTemplate(n.objectNames[(size_t)idx]);
			if (!tt)
			{
				continue; // RW 0x5F11FE
			}
			if (!n.ignoreCommandPointLimit && owner && !owner->canAffordCommandPoints(*tt))
			{
				break; // RW 0x5F123F -> 0x5F127A
			}
			if (!team)
			{
				++m_unportedUses;
				continue;
			}
			Object *o = logic.newObject(tt, team, ObjectStatusMaskType{});
			if (!o)
			{
				continue;
			}
			const Coord3D pos{ SimMath::addf32(where.x, n.offset.x), SimMath::addf32(where.y, n.offset.y), SimMath::addf32(where.z, n.offset.z) };
			o->setPosition(&pos);
			o->friend_onBuildComplete(); // RW 0x5F1623 -> 0x68D252
			made.push_back(o);
		}
	}
	return made;
}
