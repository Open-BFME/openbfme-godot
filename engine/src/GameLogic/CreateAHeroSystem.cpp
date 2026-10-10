// OpenBFME. GPL-3.0.
// TheCreateAHeroSystem and the in-game Create-a-Hero (lane HERO-2). See GameLogic/CreateAHeroSystem.h for the target facts and what is not ported.

#include "GameLogic/CreateAHeroSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/ExperienceLevels.h"
#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <stdexcept>

const char *const TheCreateAHeroBlingTypeNames[] = { "ATTRIBUTE", "APPEARANCE", "INVALID", nullptr }; // RW 0xD9EDB8
const char *const TheCreateAHeroFactionNames[] = { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor", "Neutral", nullptr }; // RW 0xD9EDD0

thread_local CreateAHeroSystem *TheCreateAHeroSystem = nullptr;

namespace
{
const char *const kStop =
	"[S-1226] Create-a-Hero: TheCreateAHeroSystem (CreateAHeroSystem.ini: RW 0x61A10F, the bling / binder / class / subclass parsers RW 0x61E7EE / 0x61E9CD / "
	"0x61FFF2 / 0x61F82B), the .cah record (RW 0x80B4F5 with its CRC-32 checksum, the EA stream header RW 0xA200A3), the system heroes, the object's record "
	"(RW 0x61B17D: the bling choices RW 0x80C3AB, the level chain of its powers RW 0x80AA38 / 0x80A190 / 0x68AB07, the apply RW 0x80ACE3 with the game "
	"mode, bling, class and subclass upgrades), the per-rank command set "
	"(RW 0x809FFB), CanBuildCreateAHeroUpgradeName at the game start (RW 0x61B103) and the build surcharge (RW 0x73C28F, RW 0x809CA6 / 0x75CD7A) are ported; "
	"a slot's whole record travels with the game setup (GameInfo's slot hero, RW 0x61B17D's slot + 0x60 / + 0x64; OpenBFME's encoding: the .cah form, "
	"checked field by field) and every peer installs it at the game start (RW 0x6315F5); "
	"NOT ported: the builder screen (createahero.apt / AptCreateAHero) and saving from it, the lobby screen's hero choice (MpGameSetup's hero list: the setup takes "
	"a record from GameWorld.get_create_a_hero_record / the peer's --create-a-hero), the award bling (RW 0x8096B0 ..) and "
	"the hero statistics file (RW 0x80B339), the drawable's colours and model swaps (RW 0x80959A, + 0x13C / + 0x140), the "
	"Create-a-Hero map mode, the revive discount (S-852); the record's bling map iterates in file order (retail: name key order, INFERENCE)";

void parseBlingType(INI *ini, void *, void *store, const void *)
{
	INI::parseIndexList(ini, nullptr, store, TheCreateAHeroBlingTypeNames);
}

void parseFaction(INI *ini, void *, void *store, const void *)
{
	INI::parseIndexList(ini, nullptr, store, TheCreateAHeroFactionNames);
}

// RW 0x61C0A5 -> 0x61B55B: the faction names of the rest of the line (INFERENCE: read as names of the RW 0xD9EDD0 list; an unknown one throws)
void parseUsableFactions(INI *ini, void *, void *store, const void *)
{
	std::vector<int> &out = *static_cast<std::vector<int> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		int found = -1;
		for (int i = 0; TheCreateAHeroFactionNames[i]; ++i)
		{
			if (std::string(t) == TheCreateAHeroFactionNames[i])
			{
				found = i;
			}
		}
		if (found < 0)
		{
			throw INIException(3, "UsableFactions: '%s' is not a faction", t);
		}
		out.push_back(found);
	}
}

const FieldParse kViewInfoParse[] = { // RW 0xD9EE08
	{ "CloseUpPitch", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, closeUpPitch) },
	{ "CloseUpZoom", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, closeUpZoom) },
	{ "CloseUpFloor", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, closeUpFloor) },
	{ "CloseUpDist", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, closeUpDist) },
	{ "CloseUpShift", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, closeUpShift) },
	{ "PortraitPitch", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, portraitPitch) },
	{ "PortraitZoom", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, portraitZoom) },
	{ "PortraitFloor", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, portraitFloor) },
	{ "PortraitDist", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, portraitDist) },
	{ "PortraitShift", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, portraitShift) },
	{ "NearPitch", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, nearPitch) },
	{ "NearZoom", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, nearZoom) },
	{ "NearFloor", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, nearFloor) },
	{ "NearDist", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, nearDist) },
	{ "NearShift", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, nearShift) },
	{ "FarPitch", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, farPitch) },
	{ "FarZoom", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, farZoom) },
	{ "FarFloor", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, farFloor) },
	{ "FarDist", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, farDist) },
	{ "FarShift", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, farShift) },
	{ "NormalCam", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, normalCam) },
	{ "CameraAngle", INI::parseReal, nullptr, (int)offsetof(CreateAHeroViewInfo, cameraAngle) },
	{ "MapLocation", INI::parseInt, nullptr, (int)offsetof(CreateAHeroViewInfo, mapLocation) },
	{ nullptr, nullptr, nullptr, 0 }
};

void parseViewInfo(INI *ini, void *, void *store, const void *) // RW 0x619141
{
	ini->initFromINI(store, kViewInfoParse);
}

bool missing(const std::string &s)
{
	return s.empty() || s == "None"; // StringBase::isEmpty / isNone
}

// RW 0x61DBC8: the Attribute sub-block (table RW 0xD9EF88)
void parseAttribute(INI *ini, void *, void *store, const void *)
{
	static const FieldParse table[] = {
		{ "GroupName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroAttribute, groupName) },
		{ "MinValueUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroAttribute, minValueUpgrade) },
		{ "MaxValueUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroAttribute, maxValueUpgrade) },
		{ "DefaultValueUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroAttribute, defaultValueUpgrade) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	CreateAHeroAttribute a;
	ini->initFromINI(&a, table);
	if (a.groupName.empty())
	{
		throw INIException(3, "No group name specified while parsing a Create-a-Hero Attribute"); // RW 0xBFC238
	}
	if (missing(a.minValueUpgrade))
	{
		throw INIException(3, "No minValueUpgradeName specified while parsing a Create-a-Hero Attribute"); // RW 0xBFC280
	}
	if (missing(a.maxValueUpgrade))
	{
		throw INIException(3, "No maxValueUpgradeName specified while parsing a Create-a-Hero Attribute"); // RW 0xBFC2D0
	}
	if (missing(a.defaultValueUpgrade))
	{
		throw INIException(3, "No defaultValueUpgradeName specified while parsing a Create-a-Hero Attribute"); // RW 0xBFC320
	}
	CreateAHeroSubClass &s = *static_cast<CreateAHeroSubClass *>(store);
	if (!s.findAttribute(a.groupName)) // RW 0x61DB90: the first of a group wins
	{
		s.attributes.push_back(a);
	}
}

// RW 0x61EB52
void parseBlingUpgrades(INI *ini, void *, void *store, const void *)
{
	if (!TheCreateAHeroSystem)
	{
		throw INIException(8, "BlingUpgrades: TheCreateAHeroSystem is not installed");
	}
	CreateAHeroSubClass &s = *static_cast<CreateAHeroSubClass *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		std::string name = t;
		bool isDefault = false;
		if (name.size() > 1 && name[0] == '@')
		{
			isDefault = true;
			name = name.substr(1);
		}
		const int index = TheCreateAHeroSystem->findBlingByUpgrade(name); // RW 0x61D21B
		if (index >= 0)
		{
			TheCreateAHeroSystem->addBlingToSubClass(s, index, isDefault); // RW 0x61E66F
		}
	}
}

const FieldParse kSubClassParse[] = { // RW 0xD9F088
	{ "BlingUpgrades", parseBlingUpgrades, nullptr, 0 },
	{ "Awards", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(CreateAHeroSubClass, awards) },
	{ "Stats", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(CreateAHeroSubClass, stats) },
	{ "UpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSubClass, upgradeName) },
	{ "NameTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSubClass, nameTag) },
	{ "DescriptionTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSubClass, descriptionTag) },
	{ "IconImage", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSubClass, iconImage) },
	{ "ButtonImage", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSubClass, buttonImage) },
	{ "DefaultPrimaryColor", INI::parseColorInt, nullptr, (int)offsetof(CreateAHeroSubClass, defaultPrimaryColor) },
	{ "DefaultSecondaryColor", INI::parseColorInt, nullptr, (int)offsetof(CreateAHeroSubClass, defaultSecondaryColor) },
	{ "DefaultTertiaryColor", INI::parseColorInt, nullptr, (int)offsetof(CreateAHeroSubClass, defaultTertiaryColor) },
	{ "SpendableAttributePoints", INI::parseInt, nullptr, (int)offsetof(CreateAHeroSubClass, spendableAttributePoints) },
	{ "Attribute", parseAttribute, nullptr, 0 },
	{ "DefaultFaction", parseFaction, nullptr, (int)offsetof(CreateAHeroSubClass, defaultFaction) },
	{ "UsableFactions", parseUsableFactions, nullptr, (int)offsetof(CreateAHeroSubClass, usableFactions) },
	{ "ViewInfo", parseViewInfo, nullptr, (int)offsetof(CreateAHeroSubClass, viewInfo) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x61F82B: one SubClass of the class being parsed
void parseSubClass(INI *ini, void *, void *store, const void *)
{
	CreateAHeroClass &cls = *static_cast<CreateAHeroClass *>(store);
	CreateAHeroSubClass s;
	ini->initFromINI(&s, kSubClassParse);
	TheCreateAHeroSystem->finishSubClass(s, cls, ini);
	cls.subClasses.push_back(std::move(s)); // RW 0x61EDE5
}

const FieldParse kClassParse[] = { // RW 0xD9F198
	{ "UpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroClass, upgradeName) },
	{ "NameTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroClass, nameTag) },
	{ "DescriptionTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroClass, descriptionTag) },
	{ "PowersDescTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroClass, powersDescTag) },
	{ "IconImage", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroClass, iconImage) },
	{ "SubClass", parseSubClass, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

void parseBlingBlock(INI *ini, void *, void *, const void *) // RW 0x61E7EE
{
	static const FieldParse table[] = { // RW 0xD9EFD8
		{ "BlingUpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBling, upgradeName) },
		{ "NameTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBling, nameTag) },
		{ "DescriptionTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBling, descriptionTag) },
		{ "GroupName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBling, groupName) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	CreateAHeroBling b;
	ini->initFromINI(&b, table);
	TheCreateAHeroSystem->addBling(std::move(b), ini);
}

void parseBinderBlock(INI *ini, void *, void *, const void *) // RW 0x61E9CD
{
	static const FieldParse table[] = { // RW 0xD9F028
		{ "GroupName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBlingBinder, groupName) },
		{ "LabelTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBlingBinder, labelTag) },
		{ "DescriptionTag", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroBlingBinder, descriptionTag) },
		{ "UISlot", INI::parseUnsignedInt, nullptr, (int)offsetof(CreateAHeroBlingBinder, uiSlot) },
		{ "BlingType", parseBlingType, nullptr, (int)offsetof(CreateAHeroBlingBinder, blingType) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	CreateAHeroBlingBinder b;
	ini->initFromINI(&b, table);
	if (b.groupName.empty())
	{
		throw INIException(3, "No group name specified while parsing a Create-a-Hero bling binder"); // RW 0xBFC238
	}
	if (missing(b.labelTag))
	{
		throw INIException(3, "No LabelTag specified while parsing a Create-a-Hero bling binder"); // RW 0xBFC4B0
	}
	if (missing(b.descriptionTag))
	{
		throw INIException(3, "No DescTag specified while parsing a Create-a-Hero bling binder"); // RW 0xBFC4F8
	}
	if (b.uiSlot == 0xFFFFFFFFu)
	{
		throw INIException(3, "No UISlot specified while parsing a Create-a-Hero bling binder"); // RW 0xBFC57C
	}
	if (b.blingType == CAH_BLING_INVALID)
	{
		throw INIException(3, "No blingType specified while parsing a Create-a-Hero bling binder"); // RW 0xBFC538
	}
	TheCreateAHeroSystem->addBinder(std::move(b));
}

void parseClassBlock(INI *ini, void *, void *, const void *) // RW 0x61FFF2
{
	CreateAHeroClass c;
	ini->initFromINI(&c, kClassParse);
	TheCreateAHeroSystem->addClass(std::move(c), ini);
}

const FieldParse kSystemParse[] = { // RW 0xBFCBD8
	{ "CreateAHeroBling", parseBlingBlock, nullptr, 0 },
	{ "CreateAHeroBlingBinder", parseBinderBlock, nullptr, 0 },
	{ "CreateAHeroClass", parseClassBlock, nullptr, 0 },
	{ "CreateAHeroMapModeUpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, mapModeUpgradeName) },
	{ "CreateAHeroGameModeUpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, gameModeUpgradeName) },
	{ "CanBuildCreateAHeroUpgradeName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, canBuildUpgradeName) },
	{ "CommandSetTemplate", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, commandSetTemplate) },
	{ "StratigicDefeatStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 0 * sizeof(std::string)) },
	{ "StratigicVictoryStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 1 * sizeof(std::string)) },
	{ "StratigicMPDefeatStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 2 * sizeof(std::string)) },
	{ "StratigicMPVictoryStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 3 * sizeof(std::string)) },
	{ "SkirmishDefeatStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 4 * sizeof(std::string)) },
	{ "SkirmishVictoryStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 5 * sizeof(std::string)) },
	{ "OpenPlayDefeatStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 6 * sizeof(std::string)) },
	{ "OpenPlayVictoryStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 7 * sizeof(std::string)) },
	{ "StratigicCampainDefeatStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 8 * sizeof(std::string)) },
	{ "StratigicCampainVictoryStatName", INI::parseAsciiString, nullptr, (int)(offsetof(CreateAHeroSystem, statNames) + 9 * sizeof(std::string)) },
	{ "WeaponGroupName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, weaponGroupName) },
	{ "SpecialAnimPercentChance", INI::parseReal, nullptr, (int)offsetof(CreateAHeroSystem, specialAnimPercentChance) },
	{ "HeroRevivalDiscount", INI::parseInt, nullptr, (int)offsetof(CreateAHeroSystem, heroRevivalDiscount) },
	{ "SpecialPowerDiscountPerLevel", INI::parseInt, nullptr, (int)offsetof(CreateAHeroSystem, specialPowerDiscountPerLevel) },
	{ "SelectedCheerAninName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, selectedCheerAnimName) },
	{ "ExamineWeaponAninName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, examineWeaponAnimName) },
	{ "ExamineSelfAninName", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, examineSelfAnimName) },
	{ "ExamineAnimTweakValue", INI::parseInt, nullptr, (int)offsetof(CreateAHeroSystem, examineAnimTweakValue) },
	{ "ModelSwapFadeDown", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, modelSwapFadeDown) },
	{ "ModelSwapFadeUp", INI::parseAsciiString, nullptr, (int)offsetof(CreateAHeroSystem, modelSwapFadeUp) },
	{ nullptr, nullptr, nullptr, 0 }
};

const UpgradeTemplate *findUpgrade(const std::string &name)
{
	return TheUpgradeCenter && !name.empty() ? TheUpgradeCenter->findUpgrade(name) : nullptr;
}

unsigned groupOrderOf(const CreateAHeroSystem &sys, int blingIndex)
{
	const CreateAHeroBling *b = sys.bling(blingIndex);
	const UpgradeTemplate *u = b ? findUpgrade(b->upgradeName) : nullptr;
	if (!u)
	{
		throw std::logic_error("CreateAHeroSystem: the bling's upgrade vanished"); // addBling checked it
	}
	return u->m_groupOrder;
}

int playerKey(const Player &p)
{
	return p.getPlayerIndex();
}
} // namespace

// ---- the data ---------------------------------------------------------------------------------------------------------------------------------------------------------
const std::vector<int> *CreateAHeroSubClass::findGroup(const std::string &group) const
{
	for (const auto &g : blingGroups)
	{
		if (g.first == group)
		{
			return &g.second;
		}
	}
	return nullptr;
}

const CreateAHeroAttribute *CreateAHeroSubClass::findAttribute(const std::string &group) const
{
	for (const CreateAHeroAttribute &a : attributes)
	{
		if (a.groupName == group)
		{
			return &a;
		}
	}
	return nullptr;
}

void CreateAHeroSystem::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("CreateAHeroSystem", [](INI *ini) {
		if (!TheCreateAHeroSystem)
		{
			throw INIException(8, "CreateAHeroSystem block: TheCreateAHeroSystem is not installed");
		}
		TheCreateAHeroSystem->parseSystem(ini);
	});
}

void CreateAHeroSystem::parseSystem(INI *ini)
{
	ini->initFromINI(this, kSystemParse);
	m_loaded = true;
}

void CreateAHeroSystem::load(INIEnvironment &env)
{
	INI ini(env);
	ini.load("Data\\INI\\CreateAHeroSystem.ini", INI_LOAD_OVERWRITE); // RW 0xBFC114
}

const CreateAHeroBling *CreateAHeroSystem::bling(int index) const
{
	return index >= 0 && (size_t)index < m_blings.size() ? &m_blings[(size_t)index] : nullptr;
}

int CreateAHeroSystem::findBlingByUpgrade(const std::string &upgradeName) const
{
	for (size_t i = 0; i < m_blings.size(); ++i)
	{
		if (m_blings[i].upgradeName == upgradeName)
		{
			return (int)i;
		}
	}
	return -1;
}

const CreateAHeroSubClass *CreateAHeroSystem::subClass(std::uint32_t cls, std::uint32_t sub) const
{
	if (cls >= m_classes.size() || sub >= m_classes[cls].subClasses.size())
	{
		return nullptr;
	}
	return &m_classes[cls].subClasses[sub];
}

const CreateAHeroBlingBinder *CreateAHeroSystem::binder(const std::string &group) const
{
	for (const CreateAHeroBlingBinder &b : m_binders)
	{
		if (b.groupName == group)
		{
			return &b;
		}
	}
	return nullptr;
}

void CreateAHeroSystem::addBling(CreateAHeroBling b, INI *)
{
	if (missing(b.upgradeName))
	{
		throw INIException(3, "No upgrade name specified while parsing a Create-a-Hero bling"); // RW 0xBFC39C
	}
	if (b.groupName.empty())
	{
		throw INIException(3, "No group name specified while parsing a Create-a-Hero bling"); // RW 0xBFC3D8
	}
	if (!findUpgrade(b.upgradeName))
	{
		throw INIException(3, "Upgrade %s not found while parsing a Create-a-Hero bling", b.upgradeName.c_str()); // RW 0xBFC454
	}
	if (findBlingByUpgrade(b.upgradeName) >= 0)
	{
		throw INIException(3, "A Create-a-Hero Bling with the upgrade %s already exists", b.upgradeName.c_str()); // RW 0xBFC418
	}
	m_blings.push_back(std::move(b)); // RW 0x61E774 -> 0x61E6F3
}

void CreateAHeroSystem::addBinder(CreateAHeroBlingBinder b)
{
	if (!binder(b.groupName)) // RW 0x61E78F: found or appended
	{
		m_binders.push_back(std::move(b));
	}
}

void CreateAHeroSystem::addClass(CreateAHeroClass c, INI *)
{
	if (missing(c.upgradeName))
	{
		throw INIException(3, "No upgrade name specified while parsing a Create-a-Hero class"); // RW 0xBFC39C
	}
	if (!findUpgrade(c.upgradeName))
	{
		throw INIException(3, "Upgrade %s not found while parsing a Create-a-Hero class", c.upgradeName.c_str()); // RW 0xBFC6D0
	}
	for (const CreateAHeroClass &o : m_classes) // RW 0x61B025
	{
		if (o.upgradeName == c.upgradeName)
		{
			throw INIException(3, "A Create-a-Hero Class with the upgrade %s already exists", c.upgradeName.c_str()); // RW 0xBFC800
		}
	}
	m_classes.push_back(std::move(c));
}

void CreateAHeroSystem::addBlingToSubClass(CreateAHeroSubClass &s, int blingIndex, bool isDefault) const
{
	const CreateAHeroBling *b = bling(blingIndex);
	if (!b)
	{
		return;
	}
	const std::string &group = b->groupName;
	// the default (+0x48): a new group starts at 0, a marked entry sets it
	bool known = false;
	for (auto &d : s.blingDefaults)
	{
		if (d.first == group)
		{
			known = true;
			if (isDefault)
			{
				d.second = blingIndex;
			}
		}
	}
	if (!known)
	{
		s.blingDefaults.emplace_back(group, 0);
	}
	// the list (+0x24)
	std::vector<int> *list = nullptr;
	for (auto &g : s.blingGroups)
	{
		if (g.first == group)
		{
			list = &g.second;
		}
	}
	if (!list)
	{
		s.blingGroups.emplace_back(group, std::vector<int>());
		list = &s.blingGroups.back().second;
	}
	if (std::find(list->begin(), list->end(), blingIndex) == list->end())
	{
		list->push_back(blingIndex);
		// RW 0x61E123 with RW 0x61AA91 (GroupOrder less): an insertion sort for the lists of this size (stable)
		std::stable_sort(list->begin(), list->end(), [this](int a, int c) { return groupOrderOf(*this, a) < groupOrderOf(*this, c); });
	}
}

void CreateAHeroSystem::finishSubClass(CreateAHeroSubClass &s, const CreateAHeroClass &cls, INI *) const
{
	if (missing(s.upgradeName))
	{
		throw INIException(3, "No upgrade name specified while parsing a Create-a-Hero subclass"); // RW 0xBFC39C
	}
	for (const CreateAHeroSubClass &o : cls.subClasses)
	{
		if (o.upgradeName == s.upgradeName) // compareNoCase in retail
		{
			throw INIException(3, "A Create-a-Hero SubClass with the upgrade %s already exists", s.upgradeName.c_str()); // RW 0xBFC708
		}
	}
	if (!findUpgrade(s.upgradeName))
	{
		throw INIException(3, "Upgrade %s not found while parsing a Create-a-Hero subclass", s.upgradeName.c_str()); // RW 0xBFC6D0
	}
	// RW 0x61FA6E .. : every upgrade of an Attribute group of the subclass that is a bling. The center's list (RW 0x5F2D61, next at + 100) is walked newest
	// first (INFERENCE: ZH's UpgradeCenter links a new template at the head); the stable GroupOrder sort makes the walk order matter only for equal orders
	if (TheUpgradeCenter)
	{
		const std::vector<UpgradeTemplate *> &all = TheUpgradeCenter->templates();
		for (auto it = all.rbegin(); it != all.rend(); ++it)
		{
			const UpgradeTemplate *u = *it;
			if (u->m_groupName.empty() || !s.findAttribute(u->m_groupName))
			{
				continue;
			}
			const int index = findBlingByUpgrade(u->getUpgradeName());
			if (index >= 0)
			{
				addBlingToSubClass(s, index, false);
			}
		}
	}
}

// ---- the game side ------------------------------------------------------------------------------------------------------------------------------------------------
bool CreateAHeroSystem::validateHero(const CreateAHeroHero &h, const CommandStore &commands, std::string *why) const
{
	auto fail = [&](const std::string &w) {
		if (why)
		{
			*why = "Create-a-Hero record: " + w;
		}
		return false;
	};
	if (h.version != CreateAHeroHero::XFER_VERSION || !h.valid || h.checksum != h.computeChecksum() || h.flags != CreateAHeroHero::LOAD_FLAGS)
	{
		return fail("not a version 8 record with its checksum and the load flags");
	}
	if (h.name.empty())
	{
		return fail("no name");
	}
	const CreateAHeroSubClass *sub = subClass(h.classIndex, h.subClassIndex);
	if (!sub)
	{
		return fail("class " + std::to_string(h.classIndex) + " subclass " + std::to_string(h.subClassIndex) + " is not in CreateAHeroSystem.ini");
	}
	for (int i = 0; i < CreateAHeroHero::POWER_COUNT; ++i)
	{
		const CreateAHeroPower &p = h.powers[(size_t)i];
		const std::string at = "power " + std::to_string(i) + " ";
		if (p.commandButton.empty())
		{
			if (p.expLevel != 0 || p.buttonIndex != 0)
			{
				return fail(at + "has no button but a rank or slot");
			}
			continue;
		}
		if (!commands.findCommandButton(p.commandButton))
		{
			return fail(at + "names the unknown button " + p.commandButton);
		}
		if (p.expLevel >= (std::uint32_t)CreateAHeroHero::POWER_COUNT)
		{
			return fail(at + "unlock rank " + std::to_string(p.expLevel));
		}
		if (p.buttonIndex >= (std::uint32_t)CommandSet::MAX_BUTTONS)
		{
			return fail(at + "command set slot " + std::to_string(p.buttonIndex));
		}
	}
	for (size_t i = 0; i < h.bling.size(); ++i)
	{
		const std::string &group = h.bling[i].first;
		for (size_t j = 0; j < i; ++j)
		{
			if (h.bling[j].first == group)
			{
				return fail("the bling group " + group + " twice");
			}
		}
		const std::vector<int> *list = sub->findGroup(group);
		if (!list)
		{
			return fail("the bling group " + group + " is not one of the subclass");
		}
		if (h.bling[i].second >= list->size())
		{
			return fail("the bling group " + group + " index " + std::to_string(h.bling[i].second) + " of " + std::to_string(list->size()));
		}
	}
	if (h.uniqueID.empty())
	{
		return fail("no unique id");
	}
	for (char c : h.uniqueID)
	{
		if (c <= ' ' || c > '~')
		{
			return fail("a unique id character outside printable ASCII");
		}
	}
	return true;
}

void CreateAHeroGame::assign(Player &player, const CreateAHeroHero &hero)
{
	m_heroes[playerKey(player)] = hero;
	m_choices.erase(playerKey(player));
	// the record's power cost (+ 0x134; RW 0x73C2C3 computes it on the first build cost query when it is 0: the buttons do not change, so computing it here
	// gives the same number)
	player.setCreateAHeroSurcharge(powerCost(hero));
}

void CreateAHeroGame::startGame()
{
	if (!TheCreateAHeroSystem || TheCreateAHeroSystem->canBuildUpgradeName.empty())
	{
		return;
	}
	const UpgradeTemplate *u = findUpgrade(TheCreateAHeroSystem->canBuildUpgradeName);
	for (auto &h : m_heroes)
	{
		if (Player *p = m_logic.players().getNthPlayer(h.first))
		{
			p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, true); // RW 0x61B167: addUpgrade(upgrade, 2, 1)
		}
	}
}

CreateAHeroHero *CreateAHeroGame::heroOf(const Player &player)
{
	auto it = m_heroes.find(playerKey(player));
	return it == m_heroes.end() ? nullptr : &it->second;
}

CreateAHeroHero *CreateAHeroGame::heroOfObject(std::uint32_t objectID)
{
	if (!objectID)
	{
		return nullptr;
	}
	for (auto &h : m_heroes)
	{
		if (h.second.objectID == objectID)
		{
			return &h.second;
		}
	}
	return nullptr;
}

const std::vector<std::pair<std::string, std::vector<int>>> &CreateAHeroGame::choicesOf(std::uint32_t objectID) const
{
	static const std::vector<std::pair<std::string, std::vector<int>>> none;
	for (const auto &h : m_heroes)
	{
		if (h.second.objectID == objectID)
		{
			auto it = m_choices.find(h.first);
			return it == m_choices.end() ? none : it->second;
		}
	}
	return none;
}

void CreateAHeroGame::onCreated(Object &obj)
{
	Player *player = obj.getControllingPlayer();
	CreateAHeroHero *hero = player ? heroOf(*player) : nullptr;
	if (!hero)
	{
		++m_stats.noRecord; // RW 0x61B17D: no record, nothing happens
		return;
	}
	if (!TheCreateAHeroSystem)
	{
		throw std::logic_error("CreateAHeroGame::onCreated: TheCreateAHeroSystem is not installed");
	}
	++m_stats.created;
	const int key = playerKey(*player);
	// RW 0x80C3AB: the record's choices are the subclass's lists (the award bling is not ported, S-1226); flag 0x80 cleared
	hero->flags &= ~0x80u;
	std::vector<std::pair<std::string, std::vector<int>>> &choices = m_choices[key];
	choices.clear();
	if (const CreateAHeroSubClass *s = TheCreateAHeroSystem->subClass(hero->classIndex, hero->subClassIndex))
	{
		choices = s->blingGroups;
	}
	hero->objectID = obj.getID(); // RW 0x80967A
	buildLevels(*hero, obj);      // RW 0x80AA38
	apply(*hero, obj, CreateAHeroHero::LOAD_FLAGS); // vtable slot 0x10 (RW 0x80ACE3) with 0x2FF
	if (m_logic.economy().isMultiplayerGame()) // RW 0x61B28B: the player loses the right to build another
	{
		player->removeUpgrade(findUpgrade(TheCreateAHeroSystem->canBuildUpgradeName), true);
	}
}

void CreateAHeroGame::applyBuilder(Player &player, const CreateAHeroHero &hero, Object &obj)
{
	if (!TheCreateAHeroSystem)
	{
		throw std::logic_error("CreateAHeroGame::applyBuilder: TheCreateAHeroSystem is not installed");
	}
	const int key = playerKey(player);
	const bool fresh = m_heroes.find(key) == m_heroes.end() || m_heroes[key].objectID != obj.getID() || m_heroes[key].classIndex != hero.classIndex ||
		m_heroes[key].subClassIndex != hero.subClassIndex;
	CreateAHeroHero &stored = m_heroes[key];
	stored = hero;
	std::vector<std::pair<std::string, std::vector<int>>> &choices = m_choices[key];
	choices.clear();
	if (const CreateAHeroSubClass *s = TheCreateAHeroSystem->subClass(stored.classIndex, stored.subClassIndex)) // RW 0x80C3AB
	{
		choices = s->blingGroups;
	}
	stored.objectID = obj.getID(); // RW 0x80967A
	apply(stored, obj, fresh ? (std::uint32_t)CreateAHeroHero::LOAD_FLAGS : hero.flags);
}

// RW 0x692ACC: the TriggeredBy upgrade of the object's UnpauseSpecialPowerUpgrade for the power (compareNoCase on the name), "" when none
static std::string unpauseUpgradeOf(const Object &obj, const std::string &power)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		const UnpauseSpecialPowerUpgrade *u = dynamic_cast<const UnpauseSpecialPowerUpgrade *>(m.get());
		const UnpauseSpecialPowerUpgradeModuleData *d = u ? dynamic_cast<const UnpauseSpecialPowerUpgradeModuleData *>(u->muxData()) : nullptr;
		const std::string name = d && d->m_specialPowerTemplate ? d->m_specialPowerTemplate->getName() : (d ? d->m_specialPowerTemplateName : std::string());
		if (d && AsciiStringUtil::compareNoCase(name, power) == 0)
		{
			return d->m_triggeredBy.empty() ? std::string() : d->m_triggeredBy.front(); // RW 0x8B96EC -> 0x8D280A
		}
	}
	return std::string();
}

bool CreateAHeroGame::powerLevel(const CreateAHeroHero &hero, int power, const std::string &level, const std::string &newLevel, const std::string &target, Object &obj)
{
	const CommandButton *b = TheCommandStore ? TheCommandStore->findCommandButton(hero.powers[(size_t)power].commandButton) : nullptr; // RW 0x71D6EA
	if (!b)
	{
		return false;
	}
	if (b->m_command == GUI_COMMAND_SPECIAL_POWER || b->m_command == GUI_COMMAND_SPECIAL_POWER_TOGGLE) // 0x18 / 0x25
	{
		if (b->m_specialPowerName.empty())
		{
			return false;
		}
		const std::string upgrade = unpauseUpgradeOf(obj, b->m_specialPowerName);
		if (upgrade.empty())
		{
			return false;
		}
		return TheExperienceLevelSystem->cloneLevelForTarget(level, newLevel, target, upgrade, std::string()); // RW 0x68AB07
	}
	if ((b->m_options & 0x40u) && !b->m_upgradeName.empty()) // NEED_UPGRADE with one upgrade (RW 0x80A23B: the list holds one name)
	{
		return TheExperienceLevelSystem->cloneLevelForTarget(level, newLevel, target, b->m_upgradeName, std::string());
	}
	return false;
}

void CreateAHeroGame::buildLevels(CreateAHeroHero &hero, Object &obj)
{
	ExperienceTracker *t = obj.getExperienceTracker();
	if (!t || !TheExperienceLevelSystem)
	{
		return;
	}
	const std::string target = "CreateAHero_" + hero.uniqueID; // RW 0x80AA76: "%s_%s" (RW 0xC4F2B0) of "CreateAHero" (RW 0xC11A74)
	t->setLevelTargetName(target);                               // RW 0x80AA8A -> 0x809DAC: the tracker's + 0x30
	int levels = 0; // RW 0x80AAB2 .. 0x80AAF1: "CreateAHeroLevel%d" (RW 0xC4F29C) from 1 while it exists
	while (TheExperienceLevelSystem->findLevel("CreateAHeroLevel" + std::to_string(levels + 1)))
	{
		++levels;
	}
	std::vector<bool> touched((size_t)levels + 1, false);
	for (int i = 1; i <= CreateAHeroHero::POWER_COUNT; ++i)
	{
		const std::string level = "CreateAHeroLevel" + std::to_string(i);
		const std::string newLevel = level + "_" + hero.uniqueID;
		if (!hero.powers[(size_t)(i - 1)].commandButton.empty() && powerLevel(hero, i - 1, level, newLevel, target, obj) && i <= levels)
		{
			touched[(size_t)i] = true; // RW 0x80AB76
		}
		if (i <= levels && !touched[(size_t)i]) // RW 0x80ABA6: the level the map walk is at, untouched: a copy without an upgrade
		{
			TheExperienceLevelSystem->cloneLevelForTarget(level, newLevel, target, std::string(), std::string());
		}
	}
}

void CreateAHeroGame::apply(CreateAHeroHero &hero, Object &obj, std::uint32_t flags)
{
	const CreateAHeroSystem &sys = *TheCreateAHeroSystem;
	auto give = [&](const std::string &name) {
		if (const UpgradeTemplate *u = findUpgrade(name))
		{
			obj.giveUpgrade(u); // RW 0x69388B
			++m_stats.upgradesGiven;
			return true;
		}
		++m_stats.missingUpgrades;
		return false;
	};
	// RW 0x80AD1F .. 0x80AD6E: the game mode upgrade on, the map mode one off (outside the builder screen, + 0x18C)
	const UpgradeTemplate *mapMode = findUpgrade(sys.mapModeUpgradeName);
	const UpgradeTemplate *gameMode = findUpgrade(sys.gameModeUpgradeName);
	if (mapMode && gameMode && !sys.inBuilder)
	{
		obj.giveUpgrade(gameMode);
		obj.removeUpgrade(mapMode); // RW 0x691438
	}
	hero.flags |= flags;
	const auto &choices = choicesOf(hero.objectID);
	if (hero.flags & 4) // RW 0x80AD80: the bling
	{
		for (const auto &b : hero.bling)
		{
			std::string upgradeName;
			for (const auto &g : choices) // RW 0x619B87 -> 0x80A2DC / 0x80A311: the record's list of the group, the entry at the index
			{
				if (g.first == b.first && b.second < g.second.size())
				{
					if (const CreateAHeroBling *bl = sys.bling(g.second[b.second]))
					{
						upgradeName = bl->upgradeName;
					}
				}
			}
			if (upgradeName.empty())
			{
				++m_stats.missingUpgrades; // an empty name: findUpgrade answers null, giveUpgrade gets nothing
				continue;
			}
			give(upgradeName);
		}
		hero.flags = (hero.flags & ~4u) | 8u;
	}
	if (hero.flags & 3) // RW 0x80AE6D: the class, then (when it exists) the subclass
	{
		const std::string classUpgrade = hero.classIndex < sys.classes().size() ? sys.classes()[hero.classIndex].upgradeName : std::string(); // RW 0x619E8C
		if (give(classUpgrade))
		{
			const CreateAHeroSubClass *s = sys.subClass(hero.classIndex, hero.subClassIndex); // RW 0x619EB5
			give(s ? s->upgradeName : std::string());
		}
		hero.flags = (hero.flags & ~3u) | 0x80u;
	}
	// RW 0x80AF0B: flag 8 colours the drawable (RW 0x80959A: kind 3 with +0x2C / +0x30 / +0x34 pushed as its three colours, then RW 0x6727B0) and clears;
	// lane CAH-2: the colours go to the client through its hooks (never logic state). INFERENCE: retail clears flag 8 only when the object has a drawable
	// (RW 0x80AF6F inside that branch); the port clears it always so a run without a client hashes the same flags. RW 0x80AF60: 0x200 is RW 0x80A77C
	// (the stats, S-1226)
	if (hero.flags & 8u)
	{
		if (ObjectClientHooks *client = m_logic.clientHooks())
		{
			client->setCustomColors(obj, 3, hero.primaryColor, hero.secondaryColor, hero.tertiaryColor);
		}
	}
	hero.flags &= ~(8u | 0x200u);
}

void CreateAHeroGame::buildCommandSet(Object &obj, int rank)
{
	CreateAHeroHero *hero = heroOfObject(obj.getID()); // RW 0x619562
	if (!hero)
	{
		return;
	}
	if (!TheCommandStore || !TheCreateAHeroSystem)
	{
		throw std::logic_error("CreateAHeroGame::buildCommandSet: the command store or TheCreateAHeroSystem is not installed");
	}
	char name[512];
	std::snprintf(name, sizeof(name), "CommandSet_%s_%s_rank_%d", obj.getName().c_str(), hero->uniqueID.c_str(), rank); // RW 0xC4F270
	CommandSet *set = TheCommandStore->newDynamicCommandSet(name); // RW 0x72028B(name, 1), cleared by RW 0x80C8D2
	if (const CommandSet *tmpl = TheCommandStore->findCommandSet(TheCreateAHeroSystem->commandSetTemplate)) // RW 0x71EFA2
	{
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			if (const CommandButton *b = tmpl->getCommandButton(i))
			{
				set->setDynamicButton(i, b); // RW 0x80C8EF
			}
		}
	}
	for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
	{
		bool found = false;
		for (const CreateAHeroPower &p : hero->powers)
		{
			if (p.commandButton.empty() || p.buttonIndex != (std::uint32_t)slot)
			{
				continue;
			}
			if (found && !(p.expLevel < (std::uint32_t)rank)) // RW 0x80A0F1 .. 0x80A0FC (unsigned)
			{
				continue;
			}
			const CommandButton *b = TheCommandStore->findCommandButton(p.commandButton); // RW 0x71D6EA
			if (!b)
			{
				++m_stats.unknownButtons;
				continue;
			}
			set->setDynamicButton(slot, b);
			found = true;
		}
	}
	if (const CommandButton *attackMove = TheCommandStore->findCommandButton("Command_AttackMove")) // RW 0xC4F25C
	{
		set->setDynamicButton(16, attackMove);
	}
	obj.setCommandSetOverride(name); // + 0x438
	++m_stats.commandSets;
}

// RW 0x75CD7A: a power button's cost at a level
static int powerButtonCost(const CommandButton &b, int level, int discountPerLevel)
{
	const int cost = b.m_createAHeroUICostIfSelected;
	if (cost <= 0) // `ja`: unsigned above 0
	{
		return 0;
	}
	if ((unsigned)level < (unsigned)b.m_createAHeroUIMinimumLevel)
	{
		return cost;
	}
	const unsigned d = (unsigned)(level - b.m_createAHeroUIMinimumLevel) * (unsigned)discountPerLevel;
	if (!d)
	{
		return cost;
	}
	return (int)((unsigned)cost - ((unsigned)cost * d) / 100u);
}

int CreateAHeroGame::powerCost(const CreateAHeroHero &hero) const
{
	return powerCostOf(hero);
}

int CreateAHeroGame::powerCostOf(const CreateAHeroHero &hero)
{
	int total = 0;
	for (int i = 0; i < CreateAHeroHero::POWER_COUNT; ++i) // RW 0x809CA6
	{
		const CreateAHeroPower &p = hero.powers[(size_t)i];
		if (p.commandButton.empty() || !TheCommandStore)
		{
			continue;
		}
		if (const CommandButton *b = TheCommandStore->findCommandButton(p.commandButton))
		{
			total += powerButtonCost(*b, i + 1, TheCreateAHeroSystem ? TheCreateAHeroSystem->specialPowerDiscountPerLevel : 0); // `lea ecx, [ebx + 1]`
		}
	}
	return total;
}

// every field of every record (the choices the game reads: class, subclass, colours, the 15 powers with their unlock rank and button slot, the bling, the unique
// id the level chain and command sets are named by) and the per-player bling lists of RW 0x80C3AB, each container with its count
std::uint32_t CreateAHeroGame::crc() const
{
	StateHasher h;
	h.addU32((std::uint32_t)m_heroes.size());
	for (const auto &e : m_heroes)
	{
		const CreateAHeroHero &r = e.second;
		h.addI32(e.first);
		h.addU32(r.version);
		h.addU32(r.objectID);
		h.addU32((std::uint32_t)r.name.size());
		for (char16_t c : r.name)
		{
			h.addU32((std::uint32_t)c);
		}
		h.addU32(r.classIndex);
		h.addU32(r.subClassIndex);
		h.addU32(r.primaryColor);
		h.addU32(r.secondaryColor);
		h.addU32(r.tertiaryColor);
		h.addU32((std::uint32_t)r.powers.size());
		for (const CreateAHeroPower &p : r.powers)
		{
			h.addString(p.commandButton);
			h.addU32(p.expLevel);
			h.addU32(p.buttonIndex);
		}
		h.addU32((std::uint32_t)r.bling.size());
		for (const auto &b : r.bling)
		{
			h.addString(b.first);
			h.addU32(b.second);
		}
		h.addString(r.uniqueID);
		h.addBool(r.isSystemHero);
		h.addU32(r.checksum);
		h.addBool(r.valid);
		h.addU32(r.flags);
	}
	h.addU32((std::uint32_t)m_choices.size());
	for (const auto &c : m_choices)
	{
		h.addI32(c.first);
		h.addU32((std::uint32_t)c.second.size());
		for (const auto &g : c.second)
		{
			h.addString(g.first);
			h.addU32((std::uint32_t)g.second.size());
			for (int b : g.second)
			{
				h.addI32(b);
			}
		}
	}
	return h.value();
}

std::vector<std::string> CreateAHeroGame::stopLines()
{
	return { kStop };
}

// ---- the system heroes --------------------------------------------------------------------------------------------------------------------------------------------
std::vector<CreateAHeroHero> CreateAHeroLibrary::systemHeroes(ArchiveFileSystem &fs, std::vector<std::string> *errors)
{
	FilenameList files;
	fs.getFileListInDirectory("", "", "*.cah", files, true);
	std::vector<CreateAHeroHero> out;
	for (const std::string &f : files)
	{
		std::string lower = f;
		for (char &c : lower)
		{
			c = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c == '/' ? '\\' : c);
		}
		if (lower.rfind("data\\systemheroes\\", 0) != 0)
		{
			continue;
		}
		std::vector<std::uint8_t> bytes;
		std::string err;
		CreateAHeroHero h;
		if (!fs.readFile(f, bytes, &err) || !h.load(bytes, &err))
		{
			if (errors)
			{
				errors->push_back(f + ": " + err);
			}
			continue;
		}
		out.push_back(std::move(h));
	}
	return out;
}
