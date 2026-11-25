// OpenBFME. GPL-3.0.
// See GameLogic/Module/FreeLifeBody.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/FreeLifeBody.h"

#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "Common/ModelState.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace
{
typedef FreeLifeBodyModuleData Data;

// RW 0x851412 (iniParseAnimAndDuration): `AnimState:<cond> AnimTime:<ms> [TriggerTime:<ms>]`; this module keeps no trigger time
void parseAnim(INI *ini, void *instance, void *, const void *)
{
	Data *d = static_cast<Data *>(instance);
	const char *t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	d->m_animCondition = ModelCondition::indexOf(ini->getNextToken(ini->getSepsColon()));
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimTime") != 0)
	{
		throw INIException(3, "AnimTime expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	INI::parseDurationUnsignedInt(ini, nullptr, &d->m_animFrames, nullptr);
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (t && std::strcmp(t, "TriggerTime") == 0)
	{
		unsigned ignored = 0;
		INI::parseDurationUnsignedInt(ini, nullptr, &ignored, nullptr);
	}
}

void parseUpgradeName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73AF89 looks the template up; the name resolves at the test (DelayedDeathBody's way)
}

// RW 0xC727F8, in the binary's row order
const FieldParse kFields[] = {
	{ "FreeLifeHealthPercent", INI::parsePercentToReal, nullptr, (int)offsetof(Data, m_freeLifeHealthPercent) },
	{ "FreeLifeTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_freeLifeTime) },
	{ "FreeLifeInvincible", INI::parseBool, nullptr, (int)offsetof(Data, m_freeLifeInvincible) },
	{ "FreeLifePrerequisiteUpgrade", parseUpgradeName, nullptr, (int)offsetof(Data, m_prerequisiteUpgrade) },
	{ "FreeLifeAnimAndDuration", parseAnim, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

// RW 0x8C59F5 (FreeLifeBody::buildFieldParse)
void FreeLifeBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x8C545F (RespawnBody::buildFieldParse)
	RespawnBodyModuleData::buildFieldParse(p); // RW 0xC71D68, 0xC723FC
	p.add(kFields);                            // RW 0xC727F8
}

FreeLifeBody::FreeLifeBody(Thing *thing, const FreeLifeBodyModuleData *data)
	: RespawnBody(thing, data)
	, m_freeLife(data)
{
}

// RW 0x8C5AE6
void FreeLifeBody::attemptDamage(DamageInfo &info)
{
	if (m_freeLife->m_freeLifeInvincible && m_freeLife->m_animCondition >= 0 && getObject()->testModelCondition(m_freeLife->m_animCondition))
	{
		return;
	}
	ActiveBody::attemptDamage(info); // RW 0x8C3FA3
}

// RW 0x8C5B86
void FreeLifeBody::internalChangeHealth(float delta)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	const Data *d = m_freeLife;
	bool free = wouldDie(delta) && d->m_freeLifeHealthPercent > 0.0f;
	if (free && !d->m_prerequisiteUpgrade.empty())
	{
		const Player *player = obj.getControllingPlayer();
		free = (player && player->hasUpgradeComplete(d->m_prerequisiteUpgrade)) || obj.hasUpgrade(d->m_prerequisiteUpgrade);
	}
	if (free && m_used && !(logic.getFrame() - m_lastFrame > d->m_freeLifeTime))
	{
		free = false;
	}
	if (!free)
	{
		RespawnBody::internalChangeHealth(delta); // RW 0x8C5CD9
		return;
	}
	m_used = true;
	m_lastFrame = logic.getFrame();
	if (d->m_animCondition >= 0)
	{
		obj.setSpecialModelConditionState(d->m_animCondition, d->m_animFrames); // RW 0x68B581
	}
	setInitialHealthPercent(SimMath::mulf32(d->m_freeLifeHealthPercent, 100.0f)); // slot 0x58 (RW 0xBD88D8 = 100.0)
	if (const DamageInfo *info = currentDamageInfo())
	{
		if (Object *source = logic.findObjectByID(info->m_input.m_sourceID))
		{
			source->scoreTheKill(obj, 3); // RW 0x8C5CC6
		}
	}
}

void FreeLifeBody::crc(StateHasher &h) const
{
	RespawnBody::crc(h);
	h.addBool(m_used);
	h.addU32(m_lastFrame);
}

// RW 0x008C5B1E (OpenBFME native snapshot order, S-4190; the 3 immutable data copies are read from m_freeLife)
void FreeLifeBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	RespawnBody::xfer(x);
	x.xferBool(&m_used);
	x.xferUnsignedInt(&m_lastFrame);
}

void FreeLifeBody::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("FreeLifeBody", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("FreeLifeBody", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error("FreeLifeBody: the module data is not typed");
		}
		return std::make_unique<FreeLifeBody>(thing, typed);
	});
}
