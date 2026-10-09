// OpenBFME. GPL-3.0.
// See GameLogic/Module/UpgradeModuleClasses.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/UpgradeModuleClasses.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

// RW 0x4B8C21 -> 0x4B8B37: the 19 word model condition flag array
void parseModelConditionWords(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 19> *>(store)->data(), 19, TheModelConditionNames);
}

// RW 0x8BA71A RemoveConditionFlagsInRange: the line is parsed into the store like RemoveConditionFlags (RW 0x4B8B37); the bits it newly set (~old & new,
// RW 0x8BA73E / 0x4B37A3) must be exactly two (RW 0x4B5D5A, else INIException 1 "you must specifly only two bit flags for a range." RW 0xC6F2F8), and every bit
// from the first to the second, both included, is then set in the store (RW 0x8BA785 .. 0x8BA7C0)
void parseConditionRange(INI *ini, void *, void *store, const void *)
{
	std::array<std::uint32_t, 19> &words = *static_cast<std::array<std::uint32_t, 19> *>(store);
	const std::array<std::uint32_t, 19> old = words;
	ParseBitFlags(ini, words.data(), 19, TheModelConditionNames);
	std::array<std::uint32_t, 19> added{};
	int count = 0;
	for (size_t i = 0; i < 19; ++i)
	{
		added[i] = ~old[i] & words[i];
		for (std::uint32_t w = added[i]; w; w &= w - 1)
		{
			++count;
		}
	}
	if (count != 2)
	{
		throw INIException(1, "you must specifly only two bit flags for a range.");
	}
	bool inRange = false;
	for (unsigned bit = 0; bit < 0x24F; ++bit)
	{
		const bool marked = ((added[bit >> 5] >> (bit & 31)) & 1u) != 0;
		bool last = false;
		if (!inRange)
		{
			if (!marked)
			{
				continue;
			}
			inRange = true;
		}
		else if (marked)
		{
			last = true;
		}
		words[bit >> 5] |= 1u << (bit & 31);
		if (last)
		{
			break;
		}
	}
}

// RW 0x869F22: `ModelConditionState:<name>`; stricmp over the model condition names, -1 when unknown (no error)
void parseTempCondition(INI *ini, void *, void *store, const void *)
{
	const char *tag = ini->getNextToken(ini->getSepsColon());
	if (std::strcmp(tag, "ModelConditionState") != 0)
	{
		throw INIException(3, "AnimState expected for TransportContain::iniParseAnim"); // RW 0xC5A644 (the shared parser's message)
	}
	const char *name = ini->getNextToken();
	int index = -1;
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
		{
			index = i;
			break;
		}
	}
	*static_cast<int *>(store) = index;
}

// RW 0x6C9951 -> 0x6C949F: the weapon set flags
void parseWeaponConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 4> *>(store)->data(), 4, TheWeaponConditionNames);
}

// RW 0x8B9420: `<old> <ignored> <new>` appended
void parseUpgradeTexture(INI *ini, void *, void *store, const void *)
{
	auto *v = static_cast<std::vector<SubObjectsUpgradeModuleData::TextureSwap> *>(store);
	v->push_back({});
	if (const char *a = ini->getNextTokenOrNull())
	{
		v->back().from = a;
	}
	ini->getNextTokenOrNull();
	if (const char *b = ini->getNextTokenOrNull())
	{
		v->back().to = b;
	}
}

#define SB_OFF(m) (int)offsetof(StatusBitsUpgradeModuleData, m)
const FieldParse kStatusBits[] = { // RW 0xC6E574
	{ "StatusToSet", ParseObjectStatusMask, nullptr, SB_OFF(m_statusToSet) },
	{ "StatusToClear", ParseObjectStatusMask, nullptr, SB_OFF(m_statusToClear) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SB_OFF
#define MC_OFF(m) (int)offsetof(ModelConditionUpgradeModuleData, m)
const FieldParse kModelCondition[] = { // RW 0xC6F3A0
	{ "AddConditionFlags", parseModelConditionWords, nullptr, MC_OFF(m_addFlags) },
	{ "RemoveConditionFlags", parseModelConditionWords, nullptr, MC_OFF(m_removeFlags) },
	{ "RemoveConditionFlagsInRange", parseConditionRange, nullptr, MC_OFF(m_removeFlags) },
	{ "AddTempConditionFlag", parseTempCondition, nullptr, MC_OFF(m_tempFlag) },
	{ "TempConditionTime", INI::parseReal, nullptr, MC_OFF(m_tempTime) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef MC_OFF
#define AU_OFF(m) (int)offsetof(ArmorUpgradeModuleData, m)
const FieldParse kArmor[] = { // RW 0xD9F8B0
	{ "KillArmorUpgrade", INI::parseBool, nullptr, AU_OFF(m_killArmorUpgrade) },
	{ "IgnoreArmorUpgrade", INI::parseBool, nullptr, AU_OFF(m_ignoreArmorUpgrade) },
	{ "ArmorSetFlag", INI::parseIndexList, TheArmorSetNames, AU_OFF(m_armorSetFlag) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AU_OFF
const FieldParse kWeaponSet[] = { // RW 0xC6EC90
	{ "WeaponCondition", parseWeaponConditions, nullptr, (int)offsetof(WeaponSetUpgradeModuleData, m_weaponCondition) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kCommandSet[] = { // RW 0xC6DF70
	{ "CommandSet", INI::parseAsciiString, nullptr, (int)offsetof(CommandSetUpgradeModuleData, m_commandSet) },
	{ nullptr, nullptr, nullptr, 0 }
};
#define SO_OFF(m) (int)offsetof(SubObjectsUpgradeModuleData, m)
const FieldParse kSubObjects[] = { // RW 0xC6E928
	{ "ShowSubObjects", INI::parseAsciiStringVectorAppend, nullptr, SO_OFF(m_showSubObjects) },
	{ "HideSubObjects", INI::parseAsciiStringVectorAppend, nullptr, SO_OFF(m_hideSubObjects) },
	{ "UpgradeTexture", parseUpgradeTexture, nullptr, SO_OFF(m_upgradeTexture) },
	{ "FadeTimeInSeconds", INI::parseReal, nullptr, SO_OFF(m_fadeTimeInSeconds) },
	{ "WaitBeforeFadeInSeconds", INI::parseReal, nullptr, SO_OFF(m_waitBeforeFadeInSeconds) },
	{ "RecolorHouse", INI::parseBool, nullptr, SO_OFF(m_recolorHouse) },
	{ "ExcludeSubobjects", INI::parseAsciiStringVectorAppend, nullptr, SO_OFF(m_excludeSubobjects) },
	{ "SkipFadeOnCreate", INI::parseBool, nullptr, SO_OFF(m_skipFadeOnCreate) },
	{ "HideSubObjectsOnRemove", INI::parseBool, nullptr, SO_OFF(m_hideSubObjectsOnRemove) },
	{ "UnHideSubObjectsOnRemove", INI::parseBool, nullptr, SO_OFF(m_unHideSubObjectsOnRemove) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SO_OFF

#define RU_OFF(m) (int)offsetof(RemoveUpgradeUpgradeModuleData, m)
const FieldParse kRemoveUpgrade[] = { // RW 0xC6FB00
	{ "UpgradeToRemove", INI::parseAsciiStringVectorAppend, nullptr, RU_OFF(m_upgradeToRemove) },
	{ "UpgradeGroupsToRemove", INI::parseAsciiStringVectorAppend, nullptr, RU_OFF(m_upgradeGroupsToRemove) },
	{ "SuppressEvaEventForRemoval", INI::parseBool, nullptr, RU_OFF(m_suppressEvaEventForRemoval) },
	{ "RemoveFromAllPlayerObjects", INI::parseBool, nullptr, RU_OFF(m_removeFromAllPlayerObjects) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RU_OFF
#define GU_OFF(m) (int)offsetof(GrantUpgradeCreateModuleData, m)
const FieldParse kGrantUpgradeCreate[] = { // RW 0xC70158
	{ "UpgradeToGrant", INI::parseAsciiString, nullptr, GU_OFF(m_upgradeToGrant) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, GU_OFF(m_exemptStatus) },
	{ "GiveOnBuildComplete", INI::parseBool, nullptr, GU_OFF(m_giveOnBuildComplete) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef GU_OFF

const int kArmorSetConditionNames = 21;
} // namespace

void StatusBitsUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kStatusBits);
}
void ModelConditionUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kModelCondition);
}
void ArmorUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kArmor);
}
void WeaponSetUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kWeaponSet);
}
void CommandSetUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kCommandSet);
}
void SubObjectsUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kSubObjects);
}
void RemoveUpgradeUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kRemoveUpgrade);
}
void GrantUpgradeCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kGrantUpgradeCreate);
}

// ---- StatusBitsUpgrade --------------------------------------------------------------------------------------------------------------------
namespace
{
void setStatusMask(Object &obj, const ObjectStatusMaskType &mask, bool on) // RW 0x68D440
{
	for (unsigned bit = 0; bit < (unsigned)(sizeof(ObjectStatusMaskType) * 8); ++bit)
	{
		if (MaskTest(mask, bit))
		{
			obj.setStatus(bit, on);
		}
	}
}
} // namespace

void StatusBitsUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	setStatusMask(*obj, m_data->m_statusToSet, true);
	setStatusMask(*obj, m_data->m_statusToClear, false);
	setCustomAnim(true);
}

void StatusBitsUpgrade::processUpgradeRemoval()
{
	Object *obj = getObject();
	setStatusMask(*obj, m_data->m_statusToSet, false);
	setStatusMask(*obj, m_data->m_statusToClear, true);
}

// ---- ModelConditionUpgrade ----------------------------------------------------------------------------------------------------------------
namespace
{
bool anyBit(const std::array<std::uint32_t, 19> &w) // RW 0x4B3783
{
	for (std::uint32_t x : w)
	{
		if (x)
		{
			return true;
		}
	}
	return false;
}
} // namespace

void ModelConditionUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	const Object::ModelConditionBits none{};
	if (anyBit(m_data->m_removeFlags))
	{
		obj->clearAndSetModelConditionFlags(m_data->m_removeFlags, none); // RW 0x5E3B79
	}
	if (anyBit(m_data->m_addFlags))
	{
		obj->clearAndSetModelConditionFlags(none, m_data->m_addFlags); // RW 0x5E3BA5
	}
	if (m_data->m_tempFlag != -1 && m_data->m_tempTime > 0.0f) // RW 0x8BA6B4 comiss against 0.0
	{
		// RW 0x8BA6C1: fild LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5); fmul dword time; _ftol2
		const std::int64_t frames = SimMath::ftol2(SimMath::pc24MulW(5.0, (double)m_data->m_tempTime));
		obj->setSpecialModelConditionState(m_data->m_tempFlag, (UnsignedInt)frames); // RW 0x68B581
	}
}

void ModelConditionUpgrade::processUpgradeRemoval()
{
	Object *obj = getObject();
	const Object::ModelConditionBits none{};
	if (anyBit(m_data->m_removeFlags))
	{
		obj->clearAndSetModelConditionFlags(none, m_data->m_removeFlags);
	}
	if (anyBit(m_data->m_addFlags))
	{
		obj->clearAndSetModelConditionFlags(m_data->m_addFlags, none);
	}
}

// ---- ArmorUpgrade -------------------------------------------------------------------------------------------------------------------------
int ArmorUpgrade::modelConditionOfArmorSetFlag(int flag)
{
	// RW 0xDB1AB0 (21 entries, one per armor set name RW 0xD9FA80), resolved by the names of the bits it holds
	static const char *const kConditionOfFlag[kArmorSetConditionNames] = {
		"ARMORSET_VETERAN", "ARMORSET_ELITE", "ARMORSET_HERO", "ARMORSET_PLAYER_UPGRADE", "ARMORSET_WEAK_VERSUS_BASEDEFENSES",
		"ARMORSET_ALTERNATE_FORMATION", "ARMORSET_MOUNTED", "ARMORSET_PLAYER_UPGRADE_2", "ARMORSET_PLAYER_UPGRADE_3", "ARMORSET_UNBESIEGEABLE",
		"ARMORSET_PLAYER_UPGRADE", // AS_TOWER -> 322 (the PLAYER_UPGRADE condition) in RW
		"ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN", // CREATE_A_HERO_01 .. 10 -> 316 in RW
		"ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN", "ARMORSET_VETERAN"
	};
	if (flag < 0 || flag >= kArmorSetConditionNames)
	{
		throw std::logic_error("ArmorUpgrade: armor set flag out of range");
	}
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], kConditionOfFlag[flag]) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("ArmorUpgrade: model condition ") + kConditionOfFlag[flag] + " is not in the name table");
}

void ArmorUpgrade::apply(bool set)
{
	Object *obj = getObject();
	if (!obj->getBodyModule())
	{
		return; // RW 0x8B7651
	}
	obj->setArmorSetFlag(m_data->m_armorSetFlag, set); // body slot 0x34 / 0x38
	const int bit = modelConditionOfArmorSetFlag(m_data->m_armorSetFlag);
	if (obj->testModelCondition(bit) != set)
	{
		obj->setModelConditionState(bit, set); // RW 0x68B53C refreshes the drawable
	}
}

void ArmorUpgrade::upgradeImplementation()
{
	if (m_data->m_ignoreArmorUpgrade)
	{
		return; // RW 0x8B7636
	}
	setCustomAnim(true);
	apply(!m_data->m_killArmorUpgrade);
}

void ArmorUpgrade::processUpgradeRemoval()
{
	if (m_data->m_ignoreArmorUpgrade)
	{
		return;
	}
	apply(m_data->m_killArmorUpgrade);
}

// ---- WeaponSetUpgrade ---------------------------------------------------------------------------------------------------------------------
void WeaponSetUpgrade::upgradeImplementation()
{
	setCustomAnim(true);
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		w->setWeaponSetFlags(m_data->m_weaponCondition, true); // RW 0x68DECA
	}
	else
	{
		// INFERENCE: RW 0x68DECA sets the object's flags whether or not it has weapons; here the flags live in ObjectWeapons (made only for a template that
		// can have weapons, RW 0x73C191), so an object without one has no weapon set to change
	}
}

void WeaponSetUpgrade::processUpgradeRemoval()
{
	if (ObjectWeapons *w = getObject()->getWeapons())
	{
		w->setWeaponSetFlags(m_data->m_weaponCondition, false); // RW 0x6911B7
	}
	setCustomAnim(false);
	setUpgradeExecuted(false);
}

// ---- CommandSetUpgrade --------------------------------------------------------------------------------------------------------------------
void CommandSetUpgrade::upgradeImplementation()
{
	setCustomAnim(true);
	getObject()->setCommandSetOverride(m_data->m_commandSet); // RW 0x693B94 (the control bar refresh that follows is the client's)
}

void CommandSetUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded())
	{
		return;
	}
	setCustomAnim(false);
	Object *obj = getObject();
	if (obj->getCommandSetOverride() == m_data->m_commandSet) // RW 0x8B7D2B
	{
		obj->setCommandSetOverride(std::string());
	}
	setUpgradeExecuted(false);
}

void CommandSetUpgrade::postUpgradeCheck()
{
	processUpgradeRemoval(); // RW 0x8B7CB9 (slot 8)
	Object *obj = getObject();
	const Player *player = obj->getControllingPlayer();
	if (!player)
	{
		throw std::logic_error("CommandSetUpgrade::postUpgradeCheck: no controlling player (RW 0x8B7CBF reads it unchecked)");
	}
	UpgradeMaskType mask = player->getCompletedUpgradeMask();
	mask.orWith(obj->getUpgradeMask());
	attemptUpgrade(mask);
}

// ---- SubObjectsUpgrade --------------------------------------------------------------------------------------------------------------------
void SubObjectsUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	const UpgradeModuleData *d = muxData();
	if (obj->getUpgradeMask().testForAny(d->m_conflictingMask)) // RW 0x8B8FD7
	{
		return;
	}
	if (const Player *p = obj->getControllingPlayer())
	{
		if (p->getCompletedUpgradeMask().testForAny(d->m_conflictingMask))
		{
			return;
		}
	}
	m_shown = true;
	if (ObjectClientHooks *draw = obj->clientHooks()) // SMOOTH-1: the client hooks (events), the module answer from the template
	{
		UpgradeModuleClasses::Stats &s = UpgradeModuleClasses::stats();
		if (m_data->m_fadeTimeInSeconds != 0.0f && !m_data->m_skipFadeOnCreate)
		{
			++s.subObjectFadesNotPorted;
		}
		for (const std::string &n : m_data->m_hideSubObjects) // RW 0x8B909E
		{
			if (!draw->showModule(*obj, n, false, true))
			{
				draw->showSubObject(*obj, n, false, true);
			}
		}
		for (const std::string &n : m_data->m_showSubObjects) // RW 0x8B9050
		{
			draw->showModule(*obj, n, true, false);
			draw->showSubObject(*obj, n, true, true);
		}
		if (!m_data->m_upgradeTexture.empty()) // RW 0x8B911E
		{
			++s.textureSwapsNotPorted;
			if (m_data->m_recolorHouse)
			{
				++s.houseRecolorsNotPorted;
			}
		}
	}
	setCustomAnim(true);
}

void SubObjectsUpgrade::processUpgradeRemoval()
{
	Object *obj = getObject();
	ObjectClientHooks *draw = obj->clientHooks(); // SMOOTH-1
	if (draw && (m_data->m_hideSubObjectsOnRemove || m_data->m_unHideSubObjectsOnRemove))
	{
		if (m_data->m_hideSubObjectsOnRemove)
		{
			for (const std::string &n : m_data->m_showSubObjects)
			{
				if (!draw->showModule(*obj, n, false, true))
				{
					draw->showSubObject(*obj, n, false, true);
				}
			}
			if (!m_data->m_upgradeTexture.empty())
			{
				++UpgradeModuleClasses::stats().textureSwapsNotPorted;
			}
		}
		if (m_data->m_unHideSubObjectsOnRemove)
		{
			for (const std::string &n : m_data->m_hideSubObjects)
			{
				draw->showModule(*obj, n, true, false);
				draw->showSubObject(*obj, n, true, true);
			}
		}
	}
	setCustomAnim(false);
}

void SubObjectsUpgrade::crc(StateHasher &h) const
{
	UpgradeModule::crc(h);
	h.addBool(m_shown);
}

// ---- RemoveUpgradeUpgrade -----------------------------------------------------------------------------------------------------------------
void RemoveUpgradeUpgrade::upgradeImplementation()
{
	if (isAlreadyUpgraded())
	{
		return; // RW 0x8BC1C5
	}
	Object *obj = getObject();
	for (const std::string &name : m_data->m_upgradeToRemove)
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
		if (!u)
		{
			UpgradeModuleClasses::unknownUpgradeNames().push_back("RemoveUpgradeUpgrade: " + name); // RW 0x8BC1F4: a debug message only
			continue;
		}
		Player *player = obj->getControllingPlayer();
		if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			if (player)
			{
				player->removeUpgrade(u, m_data->m_suppressEvaEventForRemoval);
			}
		}
		else
		{
			obj->removeUpgrade(u); // RW 0x691438
			if (m_data->m_removeFromAllPlayerObjects && player)
			{
				player->removeUpgrade(u, false);
			}
		}
	}
	// RW 0x8BC27B .. 0x8BC37E: the object's upgrades of the named groups, except the ones that trigger this module
	const UpgradeMaskType &triggers = muxData()->m_activationMask;
	for (const std::string &group : m_data->m_upgradeGroupsToRemove)
	{
		const UpgradeMaskType mask = obj->getUpgradeMask(); // a copy per group (RW 0x8BC2C2)
		for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
		{
			if (!mask.test(bit) || triggers.test(bit) || !TheUpgradeCenter)
			{
				continue;
			}
			const UpgradeTemplate *u = TheUpgradeCenter->findUpgradeByMaskBit((int)bit);
			if (u && u->m_groupName == group) // name keys: case sensitive
			{
				obj->removeUpgrade(u);
			}
		}
	}
	setUpgradeExecuted(true); // RW 0x8BC395
	setCustomAnim(true);
}

void RemoveUpgradeUpgrade::processUpgradeRemoval()
{
	setCustomAnim(false); // RW 0x8BC045
	setUpgradeExecuted(false);
}

// ---- GrantUpgradeCreate -------------------------------------------------------------------------------------------------------------------
void GrantUpgradeCreate::grant()
{
	Object *obj = getObject();
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_data->m_upgradeToGrant) : nullptr; // RW 0x66F5E5
	if (!u)
	{
		UpgradeModuleClasses::unknownUpgradeNames().push_back("GrantUpgradeCreate: " + m_data->m_upgradeToGrant);
		return;
	}
	if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
	{
		if (Player *p = obj->getControllingPlayer())
		{
			p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false); // RW 0x6AEE22(u, 2, 0)
		}
	}
	else
	{
		obj->giveUpgrade(u); // RW 0x69388B
	}
}

void GrantUpgradeCreate::onCreate()
{
	if (m_data->m_giveOnBuildComplete)
	{
		return;
	}
	static const unsigned kUnderConstruction = 2; // RW 0x8BD17B: status bit 2 (UNDER_CONSTRUCTION, RW 0x62684D(2, on))
	if (MaskTest(m_data->m_exemptStatus, kUnderConstruction) && !getObject()->testStatus(kUnderConstruction))
	{
		grant();
	}
}

void GrantUpgradeCreate::onBuildComplete()
{
	if (!m_needToRunOnBuildComplete)
	{
		return;
	}
	m_needToRunOnBuildComplete = false;
	grant();
}

void GrantUpgradeCreate::crc(StateHasher &h) const
{
	h.addBool(m_needToRunOnBuildComplete);
}

std::vector<std::string> &UpgradeModuleClasses::unknownUpgradeNames()
{
	static std::vector<std::string> names;
	return names;
}

// ---- registration -------------------------------------------------------------------------------------------------------------------------
UpgradeModuleClasses::Stats &UpgradeModuleClasses::stats()
{
	static Stats s;
	return s;
}

void UpgradeModuleClasses::registerAll(ModuleFactory &modules)
{
	modules.bindTypedData<StatusBitsUpgradeModuleData>("StatusBitsUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<StatusBitsUpgrade, StatusBitsUpgradeModuleData>(modules, "StatusBitsUpgrade");
	modules.bindTypedData<ModelConditionUpgradeModuleData>("ModelConditionUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<ModelConditionUpgrade, ModelConditionUpgradeModuleData>(modules, "ModelConditionUpgrade");
	modules.bindTypedData<ArmorUpgradeModuleData>("ArmorUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<ArmorUpgrade, ArmorUpgradeModuleData>(modules, "ArmorUpgrade");
	modules.bindTypedData<WeaponSetUpgradeModuleData>("WeaponSetUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<WeaponSetUpgrade, WeaponSetUpgradeModuleData>(modules, "WeaponSetUpgrade");
	modules.bindTypedData<CommandSetUpgradeModuleData>("CommandSetUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<CommandSetUpgrade, CommandSetUpgradeModuleData>(modules, "CommandSetUpgrade");
	modules.bindTypedData<SubObjectsUpgradeModuleData>("SubObjectsUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<SubObjectsUpgrade, SubObjectsUpgradeModuleData>(modules, "SubObjectsUpgrade");
	modules.bindTypedData<RemoveUpgradeUpgradeModuleData>("RemoveUpgradeUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<RemoveUpgradeUpgrade, RemoveUpgradeUpgradeModuleData>(modules, "RemoveUpgradeUpgrade");
	modules.bindTypedData<GrantUpgradeCreateModuleData>("GrantUpgradeCreate", MODULETYPE_BEHAVIOR);
	bindRuntime<GrantUpgradeCreate, GrantUpgradeCreateModuleData>(modules, "GrantUpgradeCreate");
}

std::vector<std::string> UpgradeModuleClasses::stopLines()
{
	const Stats &s = stats();
	return {
		"[S-480] upgrade module classes: the UpgradeMux and StatusBits / ModelCondition / Armor / WeaponSet / CommandSet / SubObjects / RemoveUpgrade / CommandPoints / "
		"CostModifier upgrades and GrantUpgradeCreate run; LevelUpUpgrade and ExperienceScalarUpgrade (lane XP-1) run; every other upgrade class the binary registers (AttributeModifierUpgrade, "
		"UnpauseSpecialPowerUpgrade, GeometryUpgrade, ObjectCreationUpgrade, ReplaceSelfUpgrade, MaxHealthUpgrade, LocomotorSetUpgrade, "
		"DoCommandUpgrade, CastleUpgrade, ...) and InheritUpgradeCreate / UpgradeDie are UnportedModules (S-140): a granted upgrade does nothing through them; " +
			std::to_string(unknownUpgradeNames().size()) + " unknown upgrade names met at run time (RW's debug messages)",
		"[S-481] upgrade Eva events: the gain / lose events of Player::addUpgrade / removeUpgrade (RW 0x6AE483, 0x6AE546 -> TheEva RW 0x5DD9EE) are counted per player "
		"(Player::upgradeEvaEventsNotPlayed), not played",
		"[S-482] upgrade walks: onUpgradeCompleted / onUpgradeRemoved (RW 0x6AE483 / 0x6AE546) walk the player's teams' members; here the logic object list in list order "
		"(the order an upgrade reaches the objects in is inferred)",
		"[S-483] upgrade queries not ported: Object::hasUpgrade's first query of the module at Object + 0x258 (slots 0x7C / 0xB0, RW 0x68E040) and the castle's mask in "
		"Object::updateUpgradeModules (RW 0x797BEF: a castle member's castle object)",
		"[S-484] SubObjectsUpgrade: the fade (FadeTimeInSeconds / WaitBeforeFadeInSeconds, RW 0x8B8F40), the UpgradeTexture swap (RW 0x8B911E) and the house recolour "
		"(RW 0x67273A) are not drawn: " + std::to_string(s.subObjectFadesNotPorted) + " fades shown instantly, " + std::to_string(s.textureSwapsNotPorted) +
		" texture swaps and " + std::to_string(s.houseRecolorsNotPorted) + " recolours not applied",
		"[S-486] upgrade research details not ported: the sub upgrade cost skip of UpgradeTemplate::calcCostToBuild (RW 0x694BF8 / slot 0xAC), the castle cost "
		"records of queueUpgrade (RW 0x8A10A0) and of the completion (RW 0x79D322 / 0x79D833), the first and third clauses of Object::affectedByUpgrade (RW 0x6939DF / "
		"0x693A1A, RW 0x6ABDC2), HordeContain's second member list (+0x150) in the upgrade hand-down (RW 0x87566B), the research sound, Eva event, UpgradeFX and UI "
		"messages of the completion",
		"[S-485] closed: ModelConditionUpgrade RemoveConditionFlagsInRange (RW 0x8BA71A) is ported (the Create-A-Hero model condition upgrades use it)",
	};
}
