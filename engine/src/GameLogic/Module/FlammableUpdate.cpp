// OpenBFME. GPL-3.0.
// See GameLogic/Module/FlammableUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameClient/FXList.h"

#include <cstddef>
#include <cstring>

namespace
{
const unsigned kLogicFramesPerSecond = 5; // RW 0xD9F608

// RW 0x890CD9: `FX:<FXList> [BONE:<bone>]`: the FX keyword (colon separators) is required, else INIException 3 "'fx' expected" (RW 0xC11A64); the list name
// through RW 0x73A302 ("None" is none; any other name must be an FXList of TheFXListStore, else INIException 3); then an optional BONE keyword (colon
// separators) and the bone name (default separators)
void parseFireFXList(INI *ini, void *instance, void *, const void *)
{
	FlammableUpdateModuleData *d = static_cast<FlammableUpdateModuleData *>(instance);
	const char *t = ini->getNextToken(ini->getSepsColon());
	if (AsciiStringUtil::compareNoCase(t, "FX") != 0)
	{
		throw INIException(3, "'fx' expected");
	}
	std::string entry = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(entry, "None") == 0)
	{
		entry.clear();
	}
	else
	{
		if (!TheFXListStore)
		{
			throw INIException(3, "TheFXListStore==NULL");
		}
		TheFXListStore->parseFXListRef(entry); // unknown: INIException 3
	}
	const char *b = ini->getNextTokenOrNull(ini->getSepsColon());
	if (b && AsciiStringUtil::compareNoCase(b, "BONE") == 0)
	{
		if (const char *bone = ini->getNextTokenOrNull())
		{
			entry += std::string(" ") + bone;
		}
	}
	d->m_fireFXList.push_back(entry);
}

// RW 0x73B217 -> 0x73AA94 (parseAudioEventRTS): "NoSound" (any case) clears; otherwise TheAudio must know the event, else INIException 3
// "Invalid Sound '%s'" (RW 0xC2500C). The audio lookup is the one the contain modules use (ContainParseHooks, installed by the retail world);
// without one the name cannot be resolved: an error, never a silent accept (PLAN rule 10).
void parseBurningSound(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *static_cast<std::string *>(store);
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		out.clear();
		return;
	}
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.audioEventExists)
	{
		throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed (acceptance stop S-083)", name.c_str());
	}
	if (!hooks.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	out = name;
}

int modelConditionIndex(const std::string &name) // RW 0x4B3B5B: -1 when absent
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(name, TheModelConditionNames[i]) == 0)
		{
			return i;
		}
	}
	return -1;
}

// RW 0x851412 (iniParseAnimAndDuration, the same parser the upgrade modules use): `AnimState:<cond> AnimTime:<ms> [TriggerTime:<ms>]`
void parseAnimAndDuration(INI *ini, void *instance, void *, const void *)
{
	FlammableUpdateModuleData *d = static_cast<FlammableUpdateModuleData *>(instance);
	const char *t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	d->m_customAnimCondition = modelConditionIndex(ini->getNextToken(ini->getSepsColon()));
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimTime") != 0)
	{
		throw INIException(3, "AnimTime expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	INI::parseDurationUnsignedInt(ini, nullptr, &d->m_customAnimFrames, nullptr);
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (t && std::strcmp(t, "TriggerTime") == 0)
	{
		INI::parseDurationUnsignedInt(ini, nullptr, &d->m_customTriggerFrames, nullptr);
	}
}

#define FU_OFF(m) (int)offsetof(FlammableUpdateModuleData, m)
// RW 0xC62E30 in the binary's row order
const FieldParse kFlammableUpdate[] = {
	{ "BurnedDelay", INI::parseDurationUnsignedInt, nullptr, FU_OFF(m_burnedDelay) },
	{ "AflameDuration", INI::parseDurationUnsignedInt, nullptr, FU_OFF(m_aflameDuration) },
	{ "AflameDamageDelay", INI::parseDurationUnsignedInt, nullptr, FU_OFF(m_aflameDamageDelay) },
	{ "AflameDamageAmount", INI::parseInt, nullptr, FU_OFF(m_aflameDamageAmount) },
	{ "BurningSoundName", parseBurningSound, nullptr, FU_OFF(m_burningSoundName) },
	{ "FlameDamageLimit", INI::parseReal, nullptr, FU_OFF(m_flameDamageLimit) },
	{ "FlameDamageExpiration", INI::parseDurationUnsignedInt, nullptr, FU_OFF(m_flameDamageExpiration) },
	{ "FireFXList", parseFireFXList, nullptr, 0 },
	{ "SetBurnedStatus", INI::parseBool, nullptr, FU_OFF(m_setBurnedStatus) },
	{ "SwapModelWhenAflame", INI::parseBool, nullptr, FU_OFF(m_swapModelWhenAflame) },
	{ "SwapModelWhenQuenched", INI::parseBool, nullptr, FU_OFF(m_swapModelWhenQuenched) },
	{ "SwapTextureWhenAflame", INI::parseBool, nullptr, FU_OFF(m_swapTextureWhenAflame) },
	{ "SwapTextureWhenQuenhed", INI::parseBool, nullptr, FU_OFF(m_swapTextureWhenQuenched) }, // [sic] RW's row name
	{ "BurnContained", INI::parseBool, nullptr, FU_OFF(m_burnContained) },
	{ "RunToWater", INI::parseBool, nullptr, FU_OFF(m_runToWater) },
	{ "RunToWaterDepth", INI::parseReal, nullptr, FU_OFF(m_runToWaterDepth) },
	{ "RunToWaterSearchRadius", INI::parseReal, nullptr, FU_OFF(m_runToWaterSearchRadius) },
	{ "RunToWaterSearchIncrement", INI::parseReal, nullptr, FU_OFF(m_runToWaterSearchIncrement) },
	{ "PanicLocomotorWhileAflame", INI::parseBool, nullptr, FU_OFF(m_panicLocomotorWhileAflame) },
	{ "CustomAnimAndDuration", parseAnimAndDuration, nullptr, 0 },
	{ "DamageType", INI::parseIndexList, TheDamageNames, FU_OFF(m_damageType) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef FU_OFF

// RW 0x73A368: an ObjectCreationList name, "None" stores none
void parseOCLName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}
const FieldParse kFireSpreadUpdate[] = { // RW 0xC62828
	{ "OCLEmbers", parseOCLName, nullptr, (int)offsetof(FireSpreadUpdateModuleData, m_oclEmbers) },
	{ "MinSpreadDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(FireSpreadUpdateModuleData, m_minSpreadDelay) },
	{ "MaxSpreadDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(FireSpreadUpdateModuleData, m_maxSpreadDelay) },
	{ "SpreadTryRange", INI::parseReal, nullptr, (int)offsetof(FireSpreadUpdateModuleData, m_spreadTryRange) },
	{ nullptr, nullptr, nullptr, 0 }
};

const int kStatusAflame = 10; // RW status 0xA (TheObjectStatusNames[10] AFLAME)
const int kStatusBurned = 11; // RW status 0xB (BURNED)

template <class M>
M *findModule(Object &obj)
{
	for (const auto &m : obj.modules())
	{
		if (M *x = dynamic_cast<M *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

void noteStop(const Object &obj, const std::string &what)
{
	obj.logic().noteStop("[S-983] " + what);
}

} // namespace

void FlammableUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFlammableUpdate);
}

void FireSpreadUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFireSpreadUpdate);
}

// ---- FlammableUpdate ----------------------------------------------------------------------------------------------------------------
// RW 0x88FCD8
FlammableUpdate::FlammableUpdate(Thing *thing, const FlammableUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
	, m_flameDamageLimit(data->m_flameDamageLimit)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// the object's position under water (TheTerrainLogic slot 0x4C, RW 0x8904F3); a SHIP is never asked (template + 0x11F bit 7)
bool FlammableUpdate::isUnderwater() const
{
	const Object *obj = getObject();
	static const int kShip = ObjectTemplateInfoBuilder::kindOfIndex("SHIP");
	if (kShip >= 0 && obj->isKindOf((unsigned)kShip))
	{
		return false;
	}
	AIWorld *ai = obj->logic().aiWorld();
	const PathfindTerrain *terrain = ai ? ai->pathfinder().terrainView() : nullptr;
	if (!terrain)
	{
		noteStop(*obj, "FlammableUpdate: the game has no map terrain, so the under-water test answers 'not under water'");
		return false;
	}
	const Coord3D *p = obj->getPosition();
	return terrain->isUnderwater(p->x, p->y, nullptr, nullptr);
}

// RW 0x8904B8
void FlammableUpdate::onDamage(const DamageInfo &info)
{
	if (info.m_output.m_actualDamageClipped > 0.0f) // RW 0x8904D0: comiss with 0.0
	{
		m_lastFlameDamager = info.m_input.m_sourceID;
	}
	Object *obj = getObject();
	const bool underwater = isUnderwater();
	if ((m_data->m_damageType != 0 && m_data->m_damageType != info.m_input.m_damageType) || underwater)
	{
		return;
	}
	const unsigned now = obj->logic().getFrame();
	if ((int)(now - m_data->m_flameDamageExpiration) > (int)m_lastFlameDamage) // RW 0x890547: signed jle
	{
		m_flameDamageLimit = m_data->m_flameDamageLimit;
	}
	m_lastFlameDamage = now;
	if (obj->testStatus(kStatusAflame) || obj->testStatus(kStatusBurned))
	{
		return;
	}
	m_flameDamageLimit = SimMath::subf32(m_flameDamageLimit, info.m_output.m_actualDamageDealt);
	if (!(m_flameDamageLimit <= 0.0f)) // RW 0x890587: comiss 0, x; jb
	{
		return;
	}
	m_flameDamageLimit = 0.0f;
	tryToIgnite();
	if (obj->getContain())
	{
		noteStop(*obj, m_data->m_burnContained ? "FlammableUpdate: BurnContained's damage to the passengers (RW 0x8905B9) is not ported"
											   : "FlammableUpdate: the contain module's slot 0x88(2) at ignition (RW 0x890656) is not ported");
	}
}

// RW 0x89015F
void FlammableUpdate::onHealing(const DamageInfo &info)
{
	Object *obj = getObject();
	if (!obj->testStatus(kStatusAflame))
	{
		return;
	}
	if (!(m_data->m_flameDamageLimit > m_flameDamageLimit))
	{
		return;
	}
	m_flameDamageLimit = SimMath::addf32(info.m_input.m_amount, m_flameDamageLimit);
	if (m_flameDamageLimit >= m_data->m_flameDamageLimit) // RW 0x890192: comiss; jb
	{
		stopBurning();
		m_flameDamageLimit = m_data->m_flameDamageLimit;
	}
}

// RW 0x8901AB
void FlammableUpdate::tryToIgnite()
{
	if (m_status != FS_NORMAL)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	obj->setStatus(kStatusAflame, true);
	static const int kAflame = CombatNames::modelCondition("AFLAME"); // bit 78 (Object + 0x114 bit 14)
	obj->setModelConditionState(kAflame, true);
	if (FireSpreadUpdate *spread = findModule<FireSpreadUpdate>(*obj)) // RW 0x89023B: the module named FireSpreadUpdate
	{
		spread->startFireSpreading(); // reads the object's AFLAME status, set above
	}
	m_status = FS_AFLAME; // RW 0x890251
	noteStop(*obj, "FlammableUpdate: the ignition's client parts (burning sound, FireFXList), the body slot 0x2C, the script event 10 and EntEnragedUpdate are not run");
	const unsigned now = logic.getFrame();
	m_aflameEndFrame = m_data->m_aflameDuration > 0 ? now + m_data->m_aflameDuration : (unsigned)UPDATE_SLEEP_FOREVER;
	m_burnedEndFrame = m_data->m_burnedDelay ? now + m_data->m_burnedDelay : 0;
	m_damageEndFrame = m_data->m_aflameDamageDelay ? now + m_data->m_aflameDamageDelay : 0;
	setWakeFrame(obj, calcSleepDelay());
	if (m_data->m_swapModelWhenAflame)
	{
		static const int kBurntModel = CombatNames::modelCondition("BURNT_MODEL"); // bit 190
		obj->setModelConditionState(kBurntModel, true);
	}
	if (m_data->m_swapTextureWhenAflame)
	{
		static const int kBurntTexture = CombatNames::modelCondition("BURNT_TEXTURE"); // bit 191
		obj->setModelConditionState(kBurntTexture, true);
	}
	if (m_data->m_customAnimFrames > 0)
	{
		noteStop(*obj, "FlammableUpdate: CustomAnimAndDuration's anim state, disable and status 0x52 (RW 0x890441) are not ported");
		m_customAnimEndFrame = now + m_data->m_customAnimFrames;
	}
	else
	{
		m_customAnimEndFrame = now; // RW 0x89048A
	}
	setWakeFrame(obj, UPDATE_SLEEP(1)); // RW 0x890495
}

// RW 0x88FDFA
void FlammableUpdate::doAflameDamage()
{
	DamageInfo d;
	d.m_input.m_sourceID = m_lastFlameDamager;
	d.m_input.m_amount = (float)m_data->m_aflameDamageAmount; // cvtsi2ss
	d.m_input.m_damageType = 6;                                // FLAME
	d.m_input.m_deathType = DEATH_BURNED;
	d.m_input.m_damageSubType = 2;
	++m_aflameDamageTicks;
	getObject()->attemptDamage(d); // RW 0x698E7D
}

// RW 0x88FF6F
void FlammableUpdate::stopBurning()
{
	Object *obj = getObject();
	m_status = obj->testStatus(kStatusBurned) ? FS_BURNED : FS_NORMAL;
	m_aflameEndFrame = 1;
	setWakeFrame(obj, UPDATE_SLEEP(1));
	obj->setStatus(3, false); // RW 0x88FFB3 (the run-to-water statuses)
	obj->setStatus(5, false);
	static const int kBurned = CombatNames::modelCondition("BURNED"); // bit 80
	if (m_data->m_setBurnedStatus)
	{
		obj->setStatus(kStatusBurned, true);
		obj->setModelConditionState(kBurned, true);
	}
	if (m_data->m_swapModelWhenQuenched)
	{
		obj->setModelConditionState(CombatNames::modelCondition("BURNT_MODEL"), false);
	}
	if (m_data->m_swapTextureWhenQuenched)
	{
		obj->setModelConditionState(CombatNames::modelCondition("BURNT_TEXTURE"), false);
	}
	obj->setStatus(kStatusAflame, false);
	static const int kAflame = CombatNames::modelCondition("AFLAME");
	obj->setModelConditionState(kAflame, false);
	noteStop(*obj, "FlammableUpdate: the quench's RW 0x68F2F1(0), script event 11, EntEnragedUpdate, sound stop, panic locomotor end and body slot 0x2C are not run");
}

// RW 0x88FB79
UpdateSleepTime FlammableUpdate::calcSleepDelay() const
{
	if (m_runningToWater)
	{
		return UPDATE_SLEEP_NONE;
	}
	const unsigned now = getObject()->logic().getFrame();
	if (m_status != FS_AFLAME || m_aflameEndFrame == 0 || m_aflameEndFrame <= now)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	unsigned wake = m_aflameEndFrame;
	if (m_burnedEndFrame != 0 && m_burnedEndFrame < wake && m_burnedEndFrame > now)
	{
		wake = m_burnedEndFrame;
	}
	if (m_damageEndFrame != 0 && m_damageEndFrame < wake && m_damageEndFrame > now)
	{
		wake = m_damageEndFrame;
	}
	return UPDATE_SLEEP((int)(wake - now));
}

// RW 0x890738
UpdateSleepTime FlammableUpdate::update()
{
	Object *obj = getObject();
	const unsigned now = obj->logic().getFrame();
	if (m_customAnimEndFrame != 0)
	{
		if (now < m_customAnimEndFrame)
		{
			return UPDATE_SLEEP_NONE;
		}
		m_customAnimEndFrame = 0;
		if (m_data->m_runToWater)
		{
			noteStop(*obj, "FlammableUpdate: RunToWater's water search and move (RW 0x890782) is not ported");
		}
		return UPDATE_SLEEP_NONE;
	}
	if (isUnderwater() && obj->getAIUpdateInterface())
	{
		noteStop(*obj, "FlammableUpdate: an object under water with an AI is not tested with RW 0x664485; its aflame end is not shortened");
	}
	if (m_damageEndFrame != 0 && now >= m_damageEndFrame)
	{
		m_damageEndFrame = m_data->m_aflameDamageDelay + now;
		doAflameDamage();
	}
	if (m_burnedEndFrame != 0 && now >= m_burnedEndFrame && m_data->m_setBurnedStatus)
	{
		obj->setStatus(kStatusBurned, true);
		static const int kBurned = CombatNames::modelCondition("BURNED");
		obj->setModelConditionState(kBurned, true);
	}
	if (m_aflameEndFrame != 0 && now >= m_aflameEndFrame)
	{
		stopBurning();
	}
	return calcSleepDelay();
}

void FlammableUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addI32(m_status);
	hasher.addU32(m_aflameEndFrame);
	hasher.addU32(m_burnedEndFrame);
	hasher.addU32(m_damageEndFrame);
	hasher.addFloat(m_flameDamageLimit);
	hasher.addU32(m_lastFlameDamage);
	hasher.addBool(m_runningToWater);
	hasher.addU32(m_customAnimEndFrame);
	hasher.addU32(m_lastFlameDamager);
	hasher.addU32(m_aflameDamageTicks);
}

// ---- FireSpreadUpdate ---------------------------------------------------------------------------------------------------------------
// RW 0x88EDDA
FireSpreadUpdate::FireSpreadUpdate(Thing *thing, const FireSpreadUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x88EE4A
UpdateSleepTime FireSpreadUpdate::calcNextSpreadDelay()
{
	int delay = getObject()->logic().random().getValue((int)m_data->m_minSpreadDelay, (int)m_data->m_maxSpreadDelay, "FireSpreadUpdate.cpp", 0x98);
	if ((unsigned)delay < 1u)
	{
		delay = 1;
	}
	return UPDATE_SLEEP(delay);
}

// RW 0x88EFA9
void FireSpreadUpdate::startFireSpreading()
{
	Object *obj = getObject();
	if (obj->testStatus(kStatusAflame))
	{
		setWakeFrame(obj, calcNextSpreadDelay());
	}
}

// RW 0x88EE6E
UpdateSleepTime FireSpreadUpdate::update()
{
	Object *obj = getObject();
	if (!obj->testStatus(kStatusAflame))
	{
		return UPDATE_SLEEP_FOREVER;
	}
	GameLogic &logic = obj->logic();
	if (!m_data->m_oclEmbers.empty())
	{
		if (const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_oclEmbers) : nullptr)
		{
			ocl->create(logic, obj, *obj->getPosition()); // RW 0x5F0126 (S-980: the position variant)
		}
	}
	if (m_data->m_spreadTryRange != 0.0f) // RW 0x88EEB9: ucomiss, jnp on equal
	{
		// RW 0x88EEF1: ThePartitionManager's closest object within SpreadTryRange, distance type 2 (FROM_BOUNDINGSPHERE_2D), with the one filter RW 0xC627A0
		// (RW 0x88ED46: the object has a FlammableUpdate (RW 0x68BDA5) whose RW 0x88FBE6 says it would ignite); the burning object itself fails that filter (lane MODULES-2)
		PartitionFilterFn wouldIgnite([](Object &o) {
			FlammableUpdate *f = findModule<FlammableUpdate>(o);
			return f && f->wouldIgnite();
		});
		Object *closest = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_spreadTryRange, FROM_BOUNDINGSPHERE_2D, { &wouldIgnite });
		FlammableUpdate *best = closest ? findModule<FlammableUpdate>(*closest) : nullptr; // RW 0x88EF7F
		if (best)
		{
			best->tryToIgnite(); // RW 0x88EF8A
			++m_spreads;
		}
		else
		{
			noteStop(*obj, "FireSpreadUpdate: the burnable terrain object branch (TheGlobalData + 0xBC, RW 0x67F52B / 0x68449B) is not ported");
		}
	}
	return calcNextSpreadDelay();
}

void FireSpreadUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_spreads);
}
