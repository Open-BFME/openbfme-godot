// OpenBFME. GPL-3.0.
// See Common/Upgrade.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "Common/Upgrade.h"

#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ObjectFilter.h"

#include <cstddef>

thread_local UpgradeCenter *TheUpgradeCenter = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
const char *const kUpgradeTypeNames[] = { "PLAYER", "OBJECT", nullptr };                 // RW 0xDA05C8
const char *const kVeterancyNames[] = { "REGULAR", "VETERAN", "ELITE", "HEROIC", nullptr }; // RW 0xD9F5E4
const char *const kHeuristicNames[] = {                                                  // RW 0xDB4F44 (8)
	"AI_UPGRADEHEURISTIC_IMPORTANT", "AI_UPGRADEHEURISTIC_BOILINGOIL", "AI_UPGRADEHEURISTIC_FACTORY_UNITUNLOCK", "AI_UPGRADEHEURISTIC_FORTRESS",
	"AI_UPGRADEHEURISTIC_ANTICAVALRY", "AI_UPGRADEHEURISTIC_ANTIARCHER", "AI_UPGRADEHEURISTIC_ANTISPECIAL", "AI_UPGRADEHEURISTIC_ANTIINFANTRY"
};

const UpgradeParseServices &services()
{
	static const UpgradeParseServices none;
	return TheUpgradeCenter ? TheUpgradeCenter->parseServices() : none;
}

// RW 0x73B217 -> 0x73AA94: "NoSound" (any case) clears; else TheAudio must know the event (INIException 3 "Invalid Sound '%s'", RW 0xC2500C)
void parseSound(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	const UpgradeParseServices &s = services();
	if (!s.audioEventExists)
	{
		throw INIException(8, "Upgrade: no audio lookup is installed for '%s' (TheAudio, RW 0xDE42FC)", name.c_str());
	}
	if (!s.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	*static_cast<std::string *>(store) = name;
}

// RW 0x5DE588 -> 0x5DE0D8: "None" (any case) is -1; else a known Eva event
void parseEva(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		*static_cast<int *>(store) = -1;
		return;
	}
	const UpgradeParseServices &s = services();
	if (!s.evaEventIndex)
	{
		throw INIException(8, "Upgrade: no Eva lookup is installed for '%s' (TheEva, RW 0xDE3670)", name.c_str());
	}
	const int index = s.evaEventIndex(name);
	if (index < 0)
	{
		throw INIException(3, "Expected a recognized Eva event name or 'None'; got '%s'", name.c_str());
	}
	*static_cast<int *>(store) = index;
}

// RW 0x76392F into a fresh filter (a template copied from DefaultUpgrade shares the pointer until it parses its own)
void parseFilter(INI *ini, void *instance, void *store, const void *userData)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, userData);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

// RW 0x548990: the next ascii string, kept as its name key (the name here)
void parseNameKey(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextAsciiString();
}

// RW 0x8E02AF: the index of the next string in RW 0xDB4F44 (AsciiString::compare: case sensitive), -1 when no name matches (no error)
void parseHeuristic(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextAsciiString();
	int index = -1;
	for (int i = 0; i < 8; ++i)
	{
		if (name == kHeuristicNames[i])
		{
			index = i;
			break;
		}
	}
	*static_cast<int *>(store) = index;
}

#define UT_OFF(member) (int)offsetof(UpgradeTemplate, member)
const FieldParse kUpgradeFieldParse[] = { // RW 0xC107F8
	{ "DisplayName", INI::parseAsciiString, nullptr, UT_OFF(m_displayName) },
	{ "Tooltip", INI::parseAsciiString, nullptr, UT_OFF(m_tooltip) },
	{ "Type", INI::parseIndexList, kUpgradeTypeNames, UT_OFF(m_type) },
	{ "BuildTime", INI::parseReal, nullptr, UT_OFF(m_buildTime) },
	{ "BuildCost", INI::parseInt, nullptr, UT_OFF(m_cost) },
	{ "ButtonImage", INI::parseAsciiString, nullptr, UT_OFF(m_buttonImage) },
	{ "ResearchSound", parseSound, nullptr, UT_OFF(m_researchSound) },
	{ "ResearchCompleteEvaEvent", parseEva, nullptr, UT_OFF(m_researchCompleteEvaEvent) },
	{ "UnitSpecificSound", parseSound, nullptr, UT_OFF(m_unitSpecificSound) },
	{ "UpgradeFX", INI::parseAsciiString, nullptr, UT_OFF(m_upgradeFX) },
	{ "LocalPlayerGainsUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_localPlayerGainsUpgradeEvaEvent) },
	{ "AlliedPlayerGainsUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_alliedPlayerGainsUpgradeEvaEvent) },
	{ "EnemyPlayerGainsUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_enemyPlayerGainsUpgradeEvaEvent) },
	{ "LocalPlayerLosesUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_localPlayerLosesUpgradeEvaEvent) },
	{ "AlliedPlayerLosesUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_alliedPlayerLosesUpgradeEvaEvent) },
	{ "EnemyPlayerLosesUpgradeEvaEvent", parseEva, nullptr, UT_OFF(m_enemyPlayerLosesUpgradeEvaEvent) },
	{ "Cursor", INI::parseAsciiString, nullptr, UT_OFF(m_cursor) },
	{ "PersistsInCampaign", INI::parseBool, nullptr, UT_OFF(m_persistsInCampaign) },
	{ "NoUpgradeDiscount", INI::parseBool, nullptr, UT_OFF(m_noUpgradeDiscount) },
	{ "SubUpgradeTemplateNames", INI::parseAsciiStringVector, nullptr, UT_OFF(m_subUpgradeTemplateNames) },
	{ "UseObjectTemplateForCostDiscount", INI::parseAsciiString, nullptr, UT_OFF(m_useObjectTemplateForCostDiscount) },
	{ "RequiredObjectFilter", parseFilter, nullptr, UT_OFF(m_requiredObjectFilter) },
	{ "GroupName", parseNameKey, nullptr, UT_OFF(m_groupName) },
	{ "GroupOrder", INI::parseUnsignedInt, nullptr, UT_OFF(m_groupOrder) },
	{ "StrategicIcon", INI::parseAsciiString, nullptr, UT_OFF(m_strategicIcon) },
	{ "SkirmishAIHeuristic", parseHeuristic, nullptr, UT_OFF(m_skirmishAIHeuristic) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef UT_OFF
} // namespace

UpgradeTemplate::UpgradeTemplate() = default;

const FieldParse *UpgradeTemplate::fieldParse()
{
	return kUpgradeFieldParse;
}

UpgradeMaskType UpgradeTemplate::grantMask() const
{
	UpgradeMaskType m;
	if (m_subUpgrades.empty())
	{
		if (m_maskBit >= 0)
		{
			m.set((unsigned)m_maskBit);
		}
		return m;
	}
	for (const UpgradeTemplate *sub : m_subUpgrades)
	{
		if (sub && sub->m_maskBit >= 0) // RW 0x693817 skips a null entry
		{
			m.set((unsigned)sub->m_maskBit);
		}
	}
	return m;
}

int UpgradeTemplate::calcCostToBuild(const Player *player, const Object *producer) const
{
	if (m_costDiscountTemplate) // RW 0x66F2D1 .. 0x66F2FA
	{
		return BuildAssistant::calcCostToBuild(*m_costDiscountTemplate, player, producer, m_cost);
	}
	int cost = m_cost;
	if (!player)
	{
		return cost;
	}
	if (!m_subUpgrades.empty())
	{
		cost = 0;
		for (const UpgradeTemplate *sub : m_subUpgrades)
		{
			// RW 0x66F34D (the producer skip is S-486): the int sum wraps modulo 2^32 (x86 add), defined here
			const std::uint32_t sum = std::uint32_t(cost) + std::uint32_t(sub->calcCostToBuild(nullptr, nullptr));
			cost = sum <= 0x7fffffffU ? int(sum) : int(std::int64_t(sum) - 0x100000000LL);
		}
	}
	float factor = 1.0f;
	if (m_type == UPGRADE_TYPE_OBJECT && !m_noUpgradeDiscount)
	{
		factor = NumericState::pc24Add(player->getUpgradeCostChange(m_name), 1.0f); // fadd [1.0]; fstp
	}
	return SimMath::cvttss2si(SimMath::sseMul(SimMath::sseFromInt32(cost), factor));
}

int UpgradeTemplate::calcTimeToBuild(const Player *) const
{
	return SimMath::cvttss2si(SimMath::sseMul(SimMath::sseFromInt32(5), m_buildTime)); // RW 0x66F203: LOGICFRAMES_PER_SECOND (RW 0xD9F608) * BuildTime
}

UpgradeCenter::UpgradeCenter() = default;

bool UpgradeCenter::canAffordUpgrade(const Player *player, const UpgradeTemplate *upgrade, const Object *producer)
{
	if (!player || !upgrade)
	{
		return false;
	}
	const int cost = upgrade->calcCostToBuild(player, producer);
	return (std::uint32_t)cost <= player->getMoney()->countMoney(); // RW 0x66F4B0: unsigned compare
}

void UpgradeCenter::resolveTemplates(const ThingFactory &things)
{
	for (UpgradeTemplate *t : m_byBit)
	{
		if (t)
		{
			t->m_costDiscountTemplate = t->m_useObjectTemplateForCostDiscount.empty() ? nullptr : things.findTemplate(t->m_useObjectTemplateForCostDiscount);
		}
	}
}

UpgradeTemplate *UpgradeCenter::newUpgrade(const std::string &name, bool assignBit)
{
	auto t = std::make_unique<UpgradeTemplate>();
	auto def = m_byName.find("DefaultUpgrade"); // RW 0xC10D04
	if (def != m_byName.end())
	{
		*t = *def->second; // RW 0x66FA3C operator= (the mask bit and the name are replaced below)
	}
	t->m_name = name;
	t->m_subUpgrades.clear();
	t->m_maskBit = -1;
	if (assignBit)
	{
		if (m_nextBit >= (int)UpgradeMaskType::BITS)
		{
			throw INIException(3, "Upgrade %s: more than %u upgrades (the upgrade mask has %u bits, RW 0x8D2835)", name.c_str(), UpgradeMaskType::BITS, UpgradeMaskType::BITS);
		}
		t->m_maskBit = m_nextBit++;
	}
	UpgradeTemplate *raw = t.get();
	auto it = m_byName.find(name);
	if (it != m_byName.end())
	{
		m_retired.push_back(std::move(it->second));
		it->second = std::move(t);
	}
	else
	{
		m_byName.emplace(name, std::move(t));
	}
	if (raw->m_maskBit >= 0)
	{
		if ((size_t)raw->m_maskBit >= m_byBit.size())
		{
			m_byBit.resize((size_t)raw->m_maskBit + 1, nullptr);
		}
		m_byBit[(size_t)raw->m_maskBit] = raw;
	}
	return raw;
}

void UpgradeCenter::init()
{
	for (int level = 1; level <= 3; ++level) // RW 0x66FDD3: VETERAN, ELITE, HEROIC
	{
		const std::string name = std::string("Upgrade_Veterancy_") + kVeterancyNames[level]; // RW 0x66F403
		UpgradeTemplate *t = newUpgrade(name, true);
		t->m_type = UPGRADE_TYPE_OBJECT; // RW 0x66F51D
		t->m_cost = 0;
		t->m_buildTime = 0.0f;
	}
}

void UpgradeCenter::parseUpgradeDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	auto it = m_byName.find(name);
	if (it == m_byName.end())
	{
		ini->initFromINI(newUpgrade(name, true), kUpgradeFieldParse);
		return;
	}
	if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		const int bit = it->second->m_maskBit; // RW 0x66FD40: the new template keeps the old bit
		UpgradeTemplate *t = newUpgrade(name, false);
		t->m_maskBit = bit;
		if (bit >= 0)
		{
			m_byBit[(size_t)bit] = t;
		}
		ini->initFromINI(t, kUpgradeFieldParse);
		return;
	}
	UpgradeTemplate scratch; // RW 0x66FD88: parsed into a stack template and discarded
	ini->initFromINI(&scratch, kUpgradeFieldParse);
}

void UpgradeCenter::parseUpgradeDefinitionGlobal(INI *ini)
{
	if (!TheUpgradeCenter)
	{
		throw INIException(8, "Upgrade block: TheUpgradeCenter is not installed");
	}
	TheUpgradeCenter->parseUpgradeDefinition(ini);
}

void UpgradeCenter::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("Upgrade", [](INI *ini) { UpgradeCenter::parseUpgradeDefinitionGlobal(ini); });
}

bool UpgradeCenter::resolveSubUpgrades(std::string *error)
{
	for (UpgradeTemplate *t : m_byBit)
	{
		if (!t)
		{
			continue;
		}
		t->m_subUpgrades.clear();
		for (const std::string &n : t->m_subUpgradeTemplateNames)
		{
			const UpgradeTemplate *sub = findUpgrade(n);
			if (!sub)
			{
				// INFERENCE: the RW resolution site of +0x10 into +0x1C was not read; an unknown name is reported, never dropped (PLAN rule 10)
				if (error)
				{
					*error = "Upgrade " + t->getUpgradeName() + ": SubUpgradeTemplateNames names '" + n + "', which is not an Upgrade";
				}
				return false;
			}
			t->m_subUpgrades.push_back(sub);
		}
	}
	return true;
}

const UpgradeTemplate *UpgradeCenter::findUpgrade(const std::string &name) const
{
	auto it = m_byName.find(name);
	return it == m_byName.end() ? nullptr : it->second.get();
}

const UpgradeTemplate *UpgradeCenter::findUpgradeByMaskBit(int bit) const
{
	return bit >= 0 && (size_t)bit < m_byBit.size() ? m_byBit[(size_t)bit] : nullptr;
}

const UpgradeTemplate *UpgradeCenter::findVeterancyUpgrade(int level) const
{
	if (level < 0 || level > 3)
	{
		return nullptr;
	}
	return findUpgrade(std::string("Upgrade_Veterancy_") + kVeterancyNames[level]);
}

void UpgradeCenter::parseUpgradeMask(INI *ini, UpgradeMaskType &mask, std::vector<std::string> *names)
{
	mask = UpgradeMaskType{}; // RW 0x66F603: memset 0x90
	if (names)
	{
		names->clear();
	}
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		const std::string text = ini->preprocessMacro(t); // a macro may expand to several names
		size_t pos = 0;
		while (pos < text.size())
		{
			const size_t b = text.find_first_not_of(" \n\r\t", pos);
			if (b == std::string::npos)
			{
				break;
			}
			size_t e = text.find_first_of(" \n\r\t", b);
			if (e == std::string::npos)
			{
				e = text.size();
			}
			const std::string piece = text.substr(b, e - b);
			pos = e;
			if (!TheUpgradeCenter)
			{
				throw INIException(8, "An upgrade mask names '%s' but TheUpgradeCenter is not installed", piece.c_str());
			}
			const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(piece);
			if (!u)
			{
				if (AsciiStringUtil::compareNoCase(piece, "None") == 0) // AsciiString::isNone
				{
					continue;
				}
				throw INIException(3, "An upgrade mask references %s, which is not an Upgrade", piece.c_str()); // RW 0xC10C90
			}
			if (u->getMaskBit() >= 0)
			{
				mask.set((unsigned)u->getMaskBit());
			}
			if (names)
			{
				names->push_back(piece);
			}
		}
	}
}
