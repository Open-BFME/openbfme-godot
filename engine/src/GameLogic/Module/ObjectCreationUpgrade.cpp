// OpenBFME. GPL-3.0.
// See GameLogic/Module/ObjectCreationUpgrade.h for the sources and what is not ported.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ObjectCreationUpgrade.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Common/INIException.h"
#include "Common/ModelState.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace
{
// RW 0x73A368: an ObjectCreationList name, "None" stores none
void parseOCLName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = name == "None" ? std::string() : name;
}

// RW 0x851412 (the anim-and-duration parser the upgrade modules share): AnimState:<cond> AnimTime:<ms> [TriggerTime:<ms>] into (+0, +4, +8)
void parseDeathAnim(INI *ini, void *instance, void *, const void *)
{
	ObjectCreationUpgradeModuleData *d = static_cast<ObjectCreationUpgradeModuleData *>(instance);
	const char *t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	d->m_deathAnimCondition = ModelCondition::indexOf(ini->getNextToken(ini->getSepsColon())); // RW 0x4B3B5B
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimTime") != 0)
	{
		throw INIException(3, "AnimTime expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	INI::parseDurationUnsignedInt(ini, nullptr, &d->m_deathAnimFrames, nullptr);
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (t && std::strcmp(t, "TriggerTime") == 0)
	{
		INI::parseDurationUnsignedInt(ini, nullptr, &d->m_deathTriggerFrames, nullptr);
	}
}

#define OCU_OFF(m) (int)offsetof(ObjectCreationUpgradeModuleData, m)
const FieldParse kObjectCreationUpgradeFields[] = { // RW 0xC6E2F8
	{ "UpgradeObject", parseOCLName, nullptr, OCU_OFF(m_upgradeObject) },
	{ "Delay", INI::parseDurationUnsignedInt, nullptr, OCU_OFF(m_delay) },
	{ "RemoveUpgrade", INI::parseAsciiString, nullptr, OCU_OFF(m_removeUpgrade) },
	{ "GrantUpgrade", INI::parseAsciiString, nullptr, OCU_OFF(m_grantUpgrade) },
	{ "ThingToSpawn", INI::parseAsciiString, nullptr, OCU_OFF(m_thingToSpawn) },
	{ "Offset", INI::parseCoord3D, nullptr, OCU_OFF(m_offset) },
	{ "Angle", INI::parseAngleReal, nullptr, OCU_OFF(m_angle) },
	{ "DestroyWhenSold", INI::parseBool, nullptr, OCU_OFF(m_destroyWhenSold) },
	{ "DeathAnimAndDuration", parseDeathAnim, nullptr, 0 },
	{ "FadeInTime", INI::parseUnsignedInt, nullptr, OCU_OFF(m_fadeInTime) },
	{ "UseBuildingProduction", INI::parseBool, nullptr, OCU_OFF(m_useBuildingProduction) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef OCU_OFF

unsigned long long s_stopUses = 0; // the unported parts met, summed over the modules (the stop line's count)
} // namespace

void ObjectCreationUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kObjectCreationUpgradeFields); // RW 0x8B81FF: its own table first, then the mux table
	buildBaseFieldParse(p);
}

ObjectCreationUpgrade::ObjectCreationUpgrade(Thing *thing, const ObjectCreationUpgradeModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
	, m_data(data)
{
	if (!data->m_upgradeObject.empty())
	{
		++m_unported; // UpgradeObject: no code of the update reads it
		++s_stopUses;
	}
}

void ObjectCreationUpgrade::upgradeImplementation()
{
	// RW 0x8B8281
	if (m_pending)
	{
		return;
	}
	m_waitFrame = getObject()->logic().getFrame() + m_data->m_delay;
	m_pending = true;
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x850C32
}

void ObjectCreationUpgrade::processUpgradeRemoval()
{
	// RW 0x8B87C5
	setUpgradeExecuted(false);
	m_waitFrame = UPDATE_SLEEP_FOREVER;
	m_pending = false;
	if (m_data->m_destroyWhenSold)
	{
		++m_unported; // the slave of the owner's SlaveWatcherBehavior killed: that module has no runtime (S-1995)
		++s_stopUses;
	}
}

UpdateSleepTime ObjectCreationUpgrade::update()
{
	// RW 0x8B8464
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (!m_pending || logic.getFrame() <= m_waitFrame)
	{
		return UPDATE_SLEEP_NONE;
	}
	const ObjectCreationUpgradeModuleData *d = m_data;
	if (!d->m_thingToSpawn.empty())
	{
		const ThingTemplate *tt = logic.things().findTemplate(d->m_thingToSpawn); // RW 0x6D1305
		if (d->m_useBuildingProduction)
		{
			// RW 0x8B84C8 .. 0x8B84FD: the owner's queue makes it (the Fire Drake out of the dragon nest): a unique id (slot 2), then queueCreateUnit(template,
			// build index -1, the id, -1, no batch, "", 1) (slot 8); a refusal tries again next frame. No ProductionUpdate: straight to the grants
			if (ProductionUpdateInterface *pu = obj->getProductionUpdate())
			{
				const ProductionID id = pu->requestUniqueUnitID();
				if (!tt || !pu->queueCreateUnit(tt, -1, id, -1, false, std::string(), true))
				{
					return UPDATE_SLEEP_NONE;
				}
				++m_queued;
			}
		}
		else
		{
			Player *player = obj->getControllingPlayer();
			Team *team = player ? player->getDefaultTeam() : obj->getTeam();
			Object *made = tt && team ? logic.newObject(tt, team, ObjectStatusMaskType{}) : nullptr; // TheThingFactory vslot 0x38
			if (!made)
			{
				return UPDATE_SLEEP_FOREVER; // RW 0x8B8625: nothing made, nothing granted (still pending, as retail: only the removal clears it)
			}
			// RW 0x8B8515 .. 0x8B85C6: the owner's transform applied to Offset (row by row, in the binary's order)
			const float *m = obj->getBasis();
			const Coord3D &t = *obj->getPosition();
			const Coord3D &o = d->m_offset;
			Coord3D at;
			at.x = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(m[1], o.y), SimMath::mulf32(m[2], o.z)), SimMath::mulf32(m[0], o.x)), t.x);
			at.y = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(m[3], o.x), SimMath::mulf32(m[4], o.y)), SimMath::mulf32(m[5], o.z)), t.y);
			at.z = SimMath::addf32(SimMath::addf32(SimMath::addf32(SimMath::mulf32(m[6], o.x), SimMath::mulf32(m[7], o.y)), SimMath::mulf32(m[8], o.z)), t.z);
			made->setOrientation(SimMath::addf32(obj->getOrientation(), d->m_angle)); // RW 0x8B8683 (+ 0x1C0)
			made->setPosition(&at);                                                  // RW 0x70C201
			made->friend_onBuildComplete();                                          // RW 0x8B86DB .. 0x8B8708: its create modules
			m_spawned = made->getID();
			if (d->m_fadeInTime > 0)
			{
				++m_unported; // the drawable's fade in (client)
				++s_stopUses;
			}
			++m_unported; // the owner's SlaveWatcherBehavior is not told (RW 0x887AF5; S-1995)
			++s_stopUses;
		}
	}
	if (const UpgradeTemplate *grant = TheUpgradeCenter && !d->m_grantUpgrade.empty() ? TheUpgradeCenter->findUpgrade(d->m_grantUpgrade) : nullptr) // RW 0x66F5E5
	{
		obj->giveUpgrade(grant); // RW 0x69388B
	}
	if (const UpgradeTemplate *remove = TheUpgradeCenter && !d->m_removeUpgrade.empty() ? TheUpgradeCenter->findUpgrade(d->m_removeUpgrade) : nullptr)
	{
		obj->removeUpgrade(remove); // RW 0x691438
	}
	return UPDATE_SLEEP_FOREVER; // RW 0x8B874B (the pending flag stays: only processUpgradeRemoval clears it)
}

void ObjectCreationUpgrade::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	crcMux(h);
	h.addU32(m_waitFrame);
	h.addBool(m_pending);
	h.addU32((std::uint32_t)m_spawned);
	h.addU32(m_queued);
	h.addU64(m_unported);
}

void ObjectCreationUpgrade::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ObjectCreationUpgradeModuleData>("ObjectCreationUpgrade", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ObjectCreationUpgrade", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const ObjectCreationUpgradeModuleData *typed = dynamic_cast<const ObjectCreationUpgradeModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("ObjectCreationUpgrade: the module data is not typed");
		}
		return std::make_unique<ObjectCreationUpgrade>(thing, typed);
	});
}

std::vector<std::string> ObjectCreationUpgrade::acceptanceStops()
{
	return { "[S-2007] ObjectCreationUpgrade (RW 0x8B8281 / 0x8B8464 / 0x8B87C5) runs: Delay, ThingToSpawn at Offset and Angle, GrantUpgrade, RemoveUpgrade; not ported: "
			 "UpgradeObject, the SlaveWatcherBehavior link and DestroyWhenSold's kill (S-1995), FadeInTime (client): " +
			 std::to_string(s_stopUses) + " uses" };
}
