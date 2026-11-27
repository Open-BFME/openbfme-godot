// OpenBFME. GPL-3.0.
// See GameLogic/Module/DetachableRiderUpdate.h for the target facts, RotWK 2.01 / BFME2 1.06 addresses, and edition notes.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// Local 174: RW 0x8B2BFD (DetachableRiderUpdateModuleData ??1DetachableRiderUpdateModuleData@@UAE@XZ); RW 0x8B252B (DetachableRiderUpdateModuleData base dtor)
#include "GameLogic/Module/DetachableRiderUpdate.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace
{
// RW 0xDCD5B0: model condition bit 72 = FREEFALL, bit 297 = RIDERLESS
constexpr int kModelConditionFreefall = 72;
constexpr int kModelConditionRiderless = 297;
// RW 0xD8AFF0: object status bit 39 (0x27) = RIDERLESS
constexpr int kStatusRiderless = 39;

// RW 0xC16928 (TheWeaponSlotTypeNames LookupListRec)
const LookupListRec kWeaponSlots[] = {
	{ "PRIMARY", PRIMARY_WEAPON },
	{ "SECONDARY", SECONDARY_WEAPON },
	{ "TERTIARY", TERTIARY_WEAPON },
	{ "QUATERNARY", QUATERNARY_WEAPON },
	{ "QUINARY", QUINARY_WEAPON },
	{ nullptr, 0 }
};

// RW 0x8B2C42 / DONOR BFME2 0x4AEB84 (DetachableRiderUpdateParseDeathEntry.cpp)
void parseDeathEntry(INI *ini, void *instance, void *store, const void *)
{
	DetachableRiderUpdateModuleData::DeathEntry entry;
	const char *tok = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!tok || AsciiStringUtil::compareNoCase(tok, "AnimState") != 0) // RW 0x8B2C78 (stricmp)
	{
		throw INIException(3, "AnimState expected"); // RW 0x8B2D37 (RW 0xC6C23C)
	}
	const int bit = INI::scanIndexList(ini->getNextToken(ini->getSepsColon()), TheModelConditionNames); // RW 0x8B2CA0 (RW 0xDCD5B0)
	BitFlagsSet(entry.m_animState, (size_t)bit);                                                        // RW 0x8B2CA6 .. 0x8B2CC4

	tok = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!tok || AsciiStringUtil::compareNoCase(tok, "AnimTime") != 0) // RW 0x8B2CD5 (stricmp)
	{
		throw INIException(3, "AnimDuration expected"); // RW 0x8B2D30 (RW 0xC6C250)
	}
	INI::parseDurationUnsignedInt(ini, instance, &entry.m_animTime, nullptr); // RW 0x8B2CEC -> RW 0x73A429

	tok = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!tok || AsciiStringUtil::compareNoCase(tok, "RiderOCL") != 0) // RW 0x8B2CFD (stricmp)
	{
		throw INIException(3, "RiderOCL expected"); // RW 0x8B2D29 (RW 0xC6C268)
	}
	// RW 0x8B2D11 -> RW 0x73A368 (INI::parseObjectCreationList): reads next token with default seps; "None" (stricmp) stores nullptr
	const std::string oclName = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(oclName, "None") != 0)
	{
		entry.m_ocl = oclName;
	}
	static_cast<std::vector<DetachableRiderUpdateModuleData::DeathEntry> *>(store)->push_back(std::move(entry)); // RW 0x8B2D20 -> RW 0x8B2BAA
}

#define DRU_OFF(member) (int)offsetof(DetachableRiderUpdateModuleData, member)
// RW 0xC6C2E8 / DONOR BFME2 0xB46668
const FieldParse kDetachableRiderUpdateFieldParse[] = {
	{ "DeathEntry", parseDeathEntry, nullptr, DRU_OFF(m_deathEntries) },
	{ "RiderSubObjects", INI::parseAsciiStringVectorAppend, nullptr, DRU_OFF(m_riderSubObjects) },
	{ "RiderlessWeaponSlot", INI::parseLookupList, kWeaponSlots, DRU_OFF(m_riderlessWeaponSlot) },
	{ "RiderlessHordeFlees", INI::parseBool, nullptr, DRU_OFF(m_riderlessHordeFlees) },
	{ "RemoveRiderlessFromHorde", INI::parseBool, nullptr, DRU_OFF(m_removeRiderlessFromHorde) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DRU_OFF

void parseUpgradeMask(INI *ini, void *, void *store, const void *userData)
{
	UpgradeMaskType *mask = static_cast<UpgradeMaskType *>(store);
	std::vector<std::string> *names = reinterpret_cast<std::vector<std::string> *>(reinterpret_cast<char *>(store) + reinterpret_cast<std::ptrdiff_t>(userData));
	UpgradeCenter::parseUpgradeMask(ini, *mask, names);
}

// RW 0x851412 (SpecialAbilityUpdateModule::iniParseAnimAndDuration) on the embedded UpgradeModuleData at store
void parseUpgradeAnimAndDuration(INI *ini, void *, void *store, const void *)
{
	UpgradeModuleData *d = static_cast<UpgradeModuleData *>(store);
	const char *t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimState") != 0)
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	const std::string state = ini->getNextToken(ini->getSepsColon());
	int cond = -1;
	for (int i = 0; TheModelConditionNames[i] != nullptr; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], state) == 0)
		{
			cond = i;
			break;
		}
	}
	d->m_customAnimCondition = cond;
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

#define DRB_OFF(member) (int)offsetof(DetachableRiderBodyModuleData, member)
#define DRB_UM_OFF(member) (int)offsetof(DetachableRiderBodyModuleData, m_upgradeMuxData.member)
#define DRB_UM_NAMES(mask, names) reinterpret_cast<const void *>((std::ptrdiff_t)(offsetof(UpgradeModuleData, names) - offsetof(UpgradeModuleData, mask)))
// RW 0xC72A0C / DONOR BFME2 0xB4CEF4 + RW 0xC76AD8 (UpgradeMuxData::getFieldParse at +0x64 in RW 0x8C5D4C)
const FieldParse kDetachableRiderBodyFieldParse[] = {
	{ "HealthPercentageWhenRiderDies", INI::parsePercentToReal, nullptr, DRB_OFF(m_healthPercentageWhenRiderDies) },
	{ "StartsActive", INI::parseBool, nullptr, DRB_OFF(m_startsActive) },
	{ "RiderlessDeathChance", INI::parsePercentToReal, nullptr, DRB_OFF(m_riderlessDeathChance) },
	{ "TriggeredBy", parseUpgradeMask, DRB_UM_NAMES(m_activationMask, m_triggeredBy), DRB_UM_OFF(m_activationMask) },
	{ "ConflictsWith", parseUpgradeMask, DRB_UM_NAMES(m_conflictingMask, m_conflictsWith), DRB_UM_OFF(m_conflictingMask) },
	{ "RequiresAllTriggers", INI::parseBool, nullptr, DRB_UM_OFF(m_requiresAllTriggers) },
	{ "RequiresAllConflictingTriggers", INI::parseBool, nullptr, DRB_UM_OFF(m_requiresAllConflictingTriggers) },
	{ "CustomAnimAndDuration", parseUpgradeAnimAndDuration, nullptr, (int)offsetof(DetachableRiderBodyModuleData, m_upgradeMuxData) },
	{ "Permanent", INI::parseBool, nullptr, DRB_UM_OFF(m_permanent) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DRB_UM_NAMES
#undef DRB_UM_OFF
#undef DRB_OFF
} // namespace

// RW 0x8B2FC3 / DONOR BFME2 0x4AEF05 (DetachableRiderUpdateBuildFieldParse.cpp)
void DetachableRiderUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDetachableRiderUpdateFieldParse);
}

// RW 0x8B2941 / DONOR BFME2 0x4AE883 (DetachableRiderUpdateCtor.cpp)
// Local 173: RW 0x887569 (DetachableRiderUpdate onDie FX + module lookup helper)
DetachableRiderUpdate::DetachableRiderUpdate(Thing *thing, const DetachableRiderUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER); // RW 0x8B29A8: push 0x3FFFFFFF
}

// RW 0x8B2A30 / DONOR BFME2 0x4AE972 (DetachableRiderUpdateStubs.cpp): bits DISABLED_HELD (3) and DISABLED_PARALYZED (4)
DisabledMaskType DetachableRiderUpdate::getDisabledTypesToProcess() const
{
	return (DisabledMaskType)((1u << DISABLED_HELD) | (1u << DISABLED_PARALYZED));
}

// RW 0x8B2A46 / DONOR BFME2 0x4AE988 (DetachableRiderUpdateKillRider.cpp)
void DetachableRiderUpdate::killRider()
{
	if (m_data->m_deathEntries.empty()) // RW 0x8B2A55: test eax, eax; je 0x8B2AB4
	{
		return;
	}
	Object *obj = getObject();
	const int r = obj->logic().random().getValue(0, (int)m_data->m_deathEntries.size() - 1, "DetachableRiderUpdate.cpp", 0x114); // RW 0x8B2A62 .. 0x8B2A70 -> RW 0x6D328E (line 0x114)
	m_whichDeathEntry = (r != 0); // RW 0x8B2A78: test eax, eax; setne al; mov [esi+0x21], al
	const size_t idx = m_whichDeathEntry ? 1u : 0u;
	const DetachableRiderUpdateModuleData::DeathEntry &entry = m_data->m_deathEntries[idx];
	obj->clearAndSetModelConditionFlags(Object::ModelConditionBits{}, entry.m_animState); // RW 0x8B2A90 -> RW 0x5E3BA5
	obj->setDisabled(DISABLED_PARALYZED, (UnsignedInt)UPDATE_SLEEP_FOREVER);              // RW 0x8B2A95 -> RW 0x692432(4)
	m_isRiderDead = true;                                                                 // RW 0x8B2AA1
	setWakeFrame(obj, entry.m_animTime != 0 ? (UpdateSleepTime)entry.m_animTime : UPDATE_SLEEP_NONE); // RW 0x8B2AAF -> RW 0x850C32
}

// RW 0x8B29B8 / DONOR BFME2 0x4AE8FA (DetachableRiderUpdateRestoreRider.cpp)
void DetachableRiderUpdate::restoreRider()
{
	m_isRiderDead = false;
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

// RW 0x8B2D59 / DONOR BFME2 0x4AEC9B (DetachableRiderUpdateUpdate.cpp)
UpdateSleepTime DetachableRiderUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	ObjectClientHooks *hooks = obj->clientHooks();
	obj->clearDisabled(DISABLED_PARALYZED); // RW 0x8B2D74 -> RW 0x692443(4)

	if (m_isRiderDead) // RW 0x8B2D7D
	{
		if (hooks) // RW 0x8B2D87
		{
			for (const std::string &sub : m_data->m_riderSubObjects)
			{
				hooks->showSubObject(*obj, sub, false, false); // RW 0x8B2DA8 -> RW 0x672823, RW 0x8B2DCE -> RW 0x4AE5B0
			}
		}

		const size_t idx = m_whichDeathEntry ? 1u : 0u;
		if (idx < m_data->m_deathEntries.size())
		{
			const DetachableRiderUpdateModuleData::DeathEntry &entry = m_data->m_deathEntries[idx];
			if (!entry.m_ocl.empty() && TheObjectCreationListStore)
			{
				if (const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(entry.m_ocl))
				{
					ocl->create(logic, obj, *obj->getPosition(), nullptr, 0); // RW 0x8B2DE6 -> RW 0x5F0126
				}
			}
			obj->clearAndSetModelConditionFlags(entry.m_animState, Object::ModelConditionBits{}); // RW 0x8B2DF8 -> RW 0x5E3B79
		}

		ObjectWeapons::setObjectWeaponLock(*obj, m_data->m_riderlessWeaponSlot, LOCKED_PERMANENTLY); // RW 0x8B2E04 -> RW 0x69121A(slot, 2)
		obj->setStatus(kStatusRiderless, true);                                                      // RW 0x8B2E0F -> RW 0x62684D(0x27, 1)
		obj->setModelConditionState(kModelConditionRiderless, true);                                 // RW 0x8B2E29 -> RW 0x68B53C

		if (m_data->m_removeRiderlessFromHorde) // RW 0x8B2E2E
		{
			if (Object *parent = obj->getContainedBy()) // RW 0x8B2E3A
			{
				if (ContainModuleInterface *contain = parent->getContain()) // RW 0x8B2E46
				{
					contain->removeFromContain(obj); // RW 0x8B2E5D (vslot 0x50)
				}
				if (AIUpdateInterface *ai = obj->getAIUpdateInterface()) // RW 0x8B2E80
				{
					ai->aiIdle(CMD_FROM_SCRIPT); // RW 0x8B2E8A -> RW 0x5E821A(2)
				}
			}
			(void)logic; // RW 0x8B2E9B -> RW 0x625759: selection is client-side in upstream/main
		}
		else if (m_data->m_riderlessHordeFlees) // RW 0x8B2EA0
		{
			if (Object *parent = obj->getContainedBy()) // RW 0x8B2EAE
			{
				ContainModuleInterface *contain = parent->getContain();
				if (contain && contain->getHordeContainInterface()) // RW 0x8B2EC3 .. 0x8B2ED2
				{
					const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList();
					UnsignedInt riderlessCount = 0;
					if (items)
					{
						for (Object *member : *items)
						{
							if (member && member->testStatus(kStatusRiderless)) // RW 0x8B2F01
							{
								++riderlessCount;
							}
						}
					}
					if (riderlessCount == contain->getContainCount() && items && !items->empty()) // RW 0x8B2F15
					{
						const std::vector<Object *> snapshot(items->begin(), items->end());
						for (Object *member : snapshot)
						{
							if (member)
							{
								member->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x8B2F37 -> RW 0x699282
							}
						}
					}
				}
			}
			else
			{
				obj->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x8B2F45 -> RW 0x699282
			}
		}
	}
	else // RW 0x8B2F4E: !m_isRiderDead (after restoreRider)
	{
		if (hooks)
		{
			for (const std::string &sub : m_data->m_riderSubObjects)
			{
				hooks->showSubObject(*obj, sub, true, false); // RW 0x8B2F68 -> RW 0x672823, RW 0x8B2F87 -> RW 0x4AE5B0
			}
		}
		if (!obj->isDestroyed()) // RW 0x8B2F92 -> RW 0x68DF11(2)
		{
			if (ObjectWeapons *weapons = obj->getWeapons())
			{
				weapons->releaseWeaponLock(LOCKED_PERMANENTLY);
			}
		}
		obj->setStatus(kStatusRiderless, false);                      // RW 0x8B2F9D -> RW 0x62684D(0x27, 0)
		obj->setModelConditionState(kModelConditionRiderless, false); // RW 0x8B2FB4 -> RW 0x68B53C
	}

	return UPDATE_SLEEP_FOREVER; // RW 0x8B2FB8: 0x3FFFFFFF
}

void DetachableRiderUpdate::crc(StateHasher &hasher) const
{
	hasher.addBool(m_isRiderDead);
	hasher.addBool(m_whichDeathEntry);
}

// RW 0x8B285F / DONOR BFME2 0x4AE7A2. Version 2 is the native layout (UpdateModule + m_isRiderDead, m_whichDeathEntry).
// Version 1 is the pre-port UnportedUpdateModule shell (UpdateModule + m_calls), which resets rider flags to defaults.
void DetachableRiderUpdate::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x);
	if (v.current < 2)
	{
		std::uint32_t legacyCalls = 0; // UnportedUpdateModule::m_calls
		x.xferUnsignedInt(&legacyCalls);
		m_isRiderDead = false;
		m_whichDeathEntry = false;
		return;
	}
	x.xferBool(&m_isRiderDead);
	x.xferBool(&m_whichDeathEntry);
}

void DetachableRiderUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<DetachableRiderUpdateModuleData>("DetachableRiderUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("DetachableRiderUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const DetachableRiderUpdateModuleData *typed = dynamic_cast<const DetachableRiderUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("DetachableRiderUpdate: the module data is not typed");
			}
			return std::make_unique<DetachableRiderUpdate>(thing, typed);
		});
}

// RW 0x008C5D2A / DONOR BFME2 0x4C1B80 (DetachableRiderBodyFieldParse.cpp)
void DetachableRiderBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	ActiveBodyModuleData::buildFieldParse(p); // RW 0x8C5D3F -> RW 0x8C2379
	p.add(kDetachableRiderBodyFieldParse);    // RW 0x8C5D47 (RW 0xC72A0C + RW 0xC76AD8)
}

// RW 0x8C5E2C / DONOR BFME2 0x4C1C82 (DetachableRiderBodyApply.cpp)
DetachableRiderBody::DetachableRiderBody(Thing *thing, const DetachableRiderBodyModuleData *data)
	: ActiveBody(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, &data->m_upgradeMuxData)
	, m_riderData(data)
{
	if (startsActive()) // RW 0x8C5E80: cmp byte ptr [eax+0x198], bl
	{
		giveSelfUpgrade(); // RW 0x8C5E8B -> RW 0x855388
	}
}

// RW 0x8C5EF3 / DONOR BFME2 0x4C1D49 (DetachableRiderBodyAttemptDamage.cpp)
void DetachableRiderBody::attemptDamage(DamageInfo &info)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	const bool active = isAlreadyUpgraded(); // RW 0x8C5F12 (mux slot 0)
	const float roll = logic.random().getValueReal(0.0f, 1.0f, "DetachableRiderBody.cpp", 0x5E); // RW 0x8C5F1A .. 0x8C5F2F -> RW 0x6D332C (line 0x5E), drawn on every hit, before the gates
	bool keepRider = roll < m_riderData->m_riderlessDeathChance; // RW 0x8C5F34 .. 0x8C5F47
	if (obj.testModelCondition(kModelConditionFreefall)) // RW 0x8C5F4F
	{
		keepRider = true;
	}
	if (active && !keepRider && !obj.testStatus(kStatusRiderless) && !obj.isDisabledByType(DISABLED_PARALYZED)) // RW 0x8C5F59 .. 0x8C5F86
	{
		const float health = getHealth(); // RW 0x8C5F90 (body slot 9)
		const float floor = SimMath::pc24Mul(getMaxHealth(), m_riderData->m_healthPercentageWhenRiderDies); // RW 0x8C5F9A .. 0x8C5FB2 (body slot 10 * +0x194)
		if (info.m_input.m_kill || SimMath::sseSub(health, info.m_input.m_amount) <= 0.0f) // RW 0x8C5FA8 .. 0x8C5FC6
		{
			info.m_input.m_amount = SimMath::sseSub(health, floor); // RW 0x8C5FCA .. 0x8C5FD4
			info.m_input.m_kill = false;                            // RW 0x8C5FD9
			if (DetachableRiderUpdate *upd = dynamic_cast<DetachableRiderUpdate *>(obj.findModule("DetachableRiderUpdate"))) // RW 0x8C5FC8 .. 0x8C6010
			{
				upd->killRider(); // RW 0x8C6017 -> RW 0x8B2A46
			}
		}
	}
	ActiveBody::attemptDamage(info); // RW 0x8C601F -> RW 0x8C3FA3
}

void DetachableRiderBody::crc(StateHasher &hasher) const
{
	ActiveBody::crc(hasher);
	crcMux(hasher);
}

// RW 0x8C5EB5 / DONOR BFME2 0x4C1D0B. Version 2 is the native layout (ActiveBody::xfer + UpgradeMux::xferMux).
// Version 1 is the pre-port UnportedBehaviorModule shell (BehaviorModule::xfer only), which resets body and mux to defaults.
void DetachableRiderBody::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	if (v.current < 2)
	{
		xferBehaviorModule(x);
		resetActiveBody();
		setUpgradeExecuted(false);
		return;
	}
	ActiveBody::xfer(x);
	xferMux(x);
}

void DetachableRiderBody::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<DetachableRiderBodyModuleData>("DetachableRiderBody", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("DetachableRiderBody", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const DetachableRiderBodyModuleData *typed = dynamic_cast<const DetachableRiderBodyModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("DetachableRiderBody: the module data is not typed");
			}
			return std::make_unique<DetachableRiderBody>(thing, typed);
		});
}
