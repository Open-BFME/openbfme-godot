// OpenBFME. GPL-3.0.
//
// SpellStores: see GameLogic/SpellStores.h. Lane SPELL-1.

#include "GameLogic/SpellStores.h"

#include "Common/GlobalOwnerChain.h"
#include "Common/INI.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/RankInfo.h"

namespace
{
// never destroyed: owners may outlive static destruction order
GlobalOwnerChain<ScienceStore *> &scienceChain()
{
	static thread_local ThreadOwnerChain<ScienceStore *> chain(TheScienceStore); // SMOOTH-1: per thread
	return chain.get();
}
GlobalOwnerChain<RankInfoStore *> &rankChain()
{
	static thread_local ThreadOwnerChain<RankInfoStore *> chain(TheRankInfoStore); // SMOOTH-1: per thread
	return chain.get();
}
GlobalOwnerChain<SpecialPowerStore *> &specialPowerChain()
{
	static thread_local ThreadOwnerChain<SpecialPowerStore *> chain(TheSpecialPowerStore); // SMOOTH-1: per thread
	return chain.get();
}
GlobalOwnerChain<ObjectCreationListStore *> &oclChain()
{
	static thread_local ThreadOwnerChain<ObjectCreationListStore *> chain(TheObjectCreationListStore); // SMOOTH-1: per thread
	return chain.get();
}
} // namespace

SpellStores::SpellStores(NameKeyGenerator &keys)
	: m_sciences(new ScienceStore(keys))
	, m_ranks(new RankInfoStore)
	, m_specialPowers(new SpecialPowerStore)
	, m_ocls(new ObjectCreationListStore(keys))
{
}

SpellStores::~SpellStores()
{
	uninstall();
}

void SpellStores::install()
{
	scienceChain().install(this, m_sciences.get());
	rankChain().install(this, m_ranks.get());
	specialPowerChain().install(this, m_specialPowers.get());
	oclChain().install(this, m_ocls.get());
}

void SpellStores::uninstall()
{
	scienceChain().remove(this);
	rankChain().remove(this);
	specialPowerChain().remove(this);
	oclChain().remove(this);
}

bool SpellStores::isInstalled() const
{
	return scienceChain().contains(this);
}

const void *SpellStores::currentOwner()
{
	return scienceChain().current();
}

void SpellStores::resetOverrides()
{
	m_sciences->resetOverrides();
	m_ranks->resetOverrides();
	m_specialPowers->resetOverrides();
}

void SpellStores::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("Science", [](INI *ini) { ScienceStore::parseScienceDefinitionGlobal(ini); });               // RW 0x5FF7DA
	registry.registerBlock("Rank", [](INI *ini) { RankInfoStore::parseRankDefinitionGlobal(ini); });                    // RW 0x5FFCAA
	registry.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); }); // RW 0x7B212F
	registry.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); }); // RW 0x5F0462
}

const char *const *SpellStores::blockKeywords()
{
	static const char *const k[] = { "Science", "Rank", "SpecialPower", "ObjectCreationList", nullptr };
	return k;
}
