// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ScienceStore. See Common/Science.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "Common/Science.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <algorithm>
#include <cstddef>

thread_local ScienceStore *TheScienceStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
bool isNoCase(const char *token, const char *word)
{
	return AsciiStringUtil::compareNoCase(token, word) == 0;
}

#define SI_OFF(member) (int)offsetof(ScienceInfo, member)
// RW 0xBF8788, in the binary's order
void parseLabel(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73B192 translates the label (GameText not loaded: S-520)
}
const FieldParse kScienceFieldParse[] = {
	{ "PrerequisiteSciences", ScienceParse::parsePrerequisiteSciences, nullptr, SI_OFF(m_prereqSciences) },
	{ "SciencePurchasePointCost", INI::parseInt, nullptr, SI_OFF(m_sciencePurchasePointCost) },
	{ "SciencePurchasePointCostMP", INI::parseInt, nullptr, SI_OFF(m_sciencePurchasePointCostMP) },
	{ "IsGrantable", INI::parseBool, nullptr, SI_OFF(m_grantable) },
	{ "DisplayName", parseLabel, nullptr, SI_OFF(m_name) },
	{ "Description", parseLabel, nullptr, SI_OFF(m_description) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SI_OFF
} // namespace

ScienceStore::ScienceStore(NameKeyGenerator &keys)
	: m_keys(keys)
{
}

ScienceStore::~ScienceStore()
{
	if (TheScienceStore == this)
	{
		TheScienceStore = nullptr;
	}
}

ScienceInfo *ScienceStore::newInfo()
{
	m_owned.push_back(std::unique_ptr<ScienceInfo>(new ScienceInfo));
	return m_owned.back().get();
}

// RW 0x5FF7DA
void ScienceStore::parseScienceDefinition(INI *ini)
{
	const char *c = ini->getNextToken();
	const std::string name = c;
	const ScienceType st = m_keys.nameToKey(name);
	ScienceInfo *found = nullptr;
	for (ScienceInfo *si : m_sciences) // the stored entries, not their overrides
	{
		if (si->m_science == st)
		{
			found = si;
			break;
		}
	}
	ScienceInfo *info = nullptr;
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		info = newInfo();
		if (found)
		{
			ScienceInfo *final = found->getFinalOverride(); // RW 0x5FF857 .. 0x5FF863
			*info = *final;                                 // RW 0x5FF789: the data (the override links are the info's own)
			info->m_nextOverride = nullptr;
			final->m_nextOverride = info;
			info->m_isOverride = true;
		}
		else
		{
			info->m_isOverride = true;
			m_sciences.push_back(info);
		}
	}
	else if (found)
	{
		if (ini->getLoadType() != INI_LOAD_RELOAD)
		{
			throw INIException(3, "duplicate science %s!\n", name.c_str());
		}
		// RW 0x5FF2DC: the found one is retired (flag 1) and leaves the vector; the new one (flag 0) is appended
		m_replaced.push_back(found);
		found->m_replaceFlag = 1;
		m_sciences.erase(std::find(m_sciences.begin(), m_sciences.end(), found));
		info = newInfo();
		m_sciences.push_back(info);
		info->m_replaceFlag = 0;
	}
	else
	{
		info = newInfo();
		m_sciences.push_back(info);
	}
	ini->initFromINI(info, kScienceFieldParse);
	info->m_science = st;
}

void ScienceStore::parseScienceDefinitionGlobal(INI *ini)
{
	if (TheScienceStore) // RW 0x5FF7FB: without a store the block is not read
	{
		TheScienceStore->parseScienceDefinition(ini);
	}
}

void ScienceStore::resetOverrides()
{
	// ZH ScienceStore::reset / Overridable::deleteOverrides: an entry that is itself an override goes, others lose their override chain
	std::vector<ScienceInfo *> kept;
	for (ScienceInfo *si : m_sciences)
	{
		if (!si->m_isOverride)
		{
			si->m_nextOverride = nullptr;
			kept.push_back(si);
		}
	}
	m_sciences.swap(kept);
	std::vector<std::unique_ptr<ScienceInfo>> owned;
	for (auto &p : m_owned)
	{
		if (!p->m_isOverride)
		{
			owned.push_back(std::move(p));
		}
	}
	m_owned.swap(owned);
}

// RW 0x5FEC35
const ScienceInfo *ScienceStore::findScienceInfo(ScienceType st) const
{
	for (const ScienceInfo *si : m_sciences)
	{
		const ScienceInfo *f = si->getFinalOverride();
		if (f->m_science == st)
		{
			return f;
		}
	}
	return nullptr;
}

// RW 0x5FEC64
int ScienceStore::getSciencePurchaseCost(ScienceType st, bool multiplayerCosts) const
{
	const ScienceInfo *si = findScienceInfo(st);
	if (!si)
	{
		return 0;
	}
	return multiplayerCosts ? si->m_sciencePurchasePointCostMP : si->m_sciencePurchasePointCost;
}

// RW 0x5FECBA
bool ScienceStore::isScienceGrantable(ScienceType st) const
{
	const ScienceInfo *si = findScienceInfo(st);
	return si ? si->m_grantable : false;
}

// RW 0x5FED05
bool ScienceStore::playerHasPrereqsForScience(const ScienceOwner &player, ScienceType st) const
{
	const ScienceInfo *si = findScienceInfo(st);
	if (!si)
	{
		return false;
	}
	if (si->m_prereqSciences.empty())
	{
		return true; // RW 0x5FED23: no group at all
	}
	for (const ScienceVec &group : si->m_prereqSciences)
	{
		bool all = true;
		for (ScienceType need : group)
		{
			if (!player.hasScience(need))
			{
				all = false;
				break;
			}
		}
		if (all)
		{
			return true;
		}
	}
	return false;
}

// RW 0x5FED5B
bool ScienceStore::playerHasRootPrereqsAndCanPurchase(const ScienceOwner &player, ScienceType st, bool multiplayerCosts) const
{
	if (!playerHasPrereqsForScience(player, st))
	{
		return false;
	}
	const int points = player.getSciencePurchasePoints();
	return getSciencePurchaseCost(st, multiplayerCosts) <= points;
}

// RW 0x5FEEC7
ScienceType ScienceStore::friend_lookupScience(const char *name) const
{
	const ScienceType st = m_keys.nameToKey(name); // RW 0x5FEECE: nameToKey (creates the key)
	if (!isValidScience(st))
	{
		throw INIException(3, "Science name %s not known! (Did you define it in Science.ini?)", name);
	}
	return st;
}

ScienceType ScienceStore::getScienceFromInternalName(const std::string &name) const
{
	const NameKeyType key = m_keys.findKey(name);
	if (key == NAMEKEY_INVALID || !isValidScience(key))
	{
		return SCIENCE_INVALID;
	}
	return key;
}

std::string ScienceStore::getInternalNameForScience(ScienceType st) const
{
	return m_keys.keyToName(st);
}

std::vector<const ScienceInfo *> ScienceStore::sciences() const
{
	std::vector<const ScienceInfo *> out;
	for (const ScienceInfo *si : m_sciences)
	{
		out.push_back(si->getFinalOverride());
	}
	return out;
}

namespace ScienceParse
{
namespace
{
ScienceType scanScience(const char *token)
{
	if (!TheScienceStore)
	{
		throw INIException(3, "TheScienceStore==NULL");
	}
	return TheScienceStore->friend_lookupScience(token); // RW 0x73A386
}
} // namespace

// RW 0x73B4A0
void parseScienceVector(INI *ini, void *, void *store, const void *)
{
	ScienceVec *v = static_cast<ScienceVec *>(store);
	v->clear();
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		if (isNoCase(token, "None"))
		{
			v->clear();
			return;
		}
		v->push_back(scanScience(token));
	}
}

// RW 0x73BCBF
void parsePrerequisiteSciences(INI *ini, void *, void *store, const void *)
{
	std::vector<ScienceVec> *groups = static_cast<std::vector<ScienceVec> *>(store);
	groups->resize(1); // RW 0x73BC98(1)
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		if (isNoCase(token, "None"))
		{
			groups->back().clear();
			break;
		}
		if (isNoCase(token, "OR"))
		{
			if (groups->back().empty())
			{
				break;
			}
			groups->push_back(ScienceVec());
			continue;
		}
		groups->back().push_back(scanScience(token));
	}
	if (groups->back().empty()) // RW 0x73BD7B
	{
		groups->clear();
	}
}
} // namespace ScienceParse
