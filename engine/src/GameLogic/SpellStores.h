// OpenBFME. GPL-3.0.
//
// SpellStores: the three global stores of the spell book, owned together (lane SPELL-1): TheScienceStore (RW 0xDE3B20, the `Science` block),
// TheRankInfoStore (RW 0xDE3B2C, `Rank`) and TheSpecialPowerStore (RW 0xDE878C, `SpecialPower`). install() makes them the process-wide stores
// as removable owner registrations (Common/GlobalOwnerChain.h, the WeaponStores pattern); registerBlocks() installs the three real block
// parsers in place of their recording stubs.

#pragma once

#include <memory>

class NameKeyGenerator;
class INIBlockRegistry;
class ScienceStore;
class RankInfoStore;
class SpecialPowerStore;
class ObjectCreationListStore;

class SpellStores
{
public:
	explicit SpellStores(NameKeyGenerator &keys);
	SpellStores(const SpellStores &) = delete;
	SpellStores &operator=(const SpellStores &) = delete;
	~SpellStores();

	void install();
	void uninstall();
	bool isInstalled() const;
	static const void *currentOwner(); ///< the owner whose stores are current (a SpellStores *), nullptr when none
	// the between-maps reset: every load type 2 override of the three stores goes
	void resetOverrides();
	// "Science", "Rank", "SpecialPower" (RW 0x5FF7DA, 0x5FFCAA, 0x7B212F); the handlers use the globals, like retail
	static void registerBlocks(INIBlockRegistry &registry);
	static const char *const *blockKeywords(); ///< the three keywords, NULL terminated

	ScienceStore &sciences() { return *m_sciences; }
	RankInfoStore &ranks() { return *m_ranks; }
	SpecialPowerStore &specialPowers() { return *m_specialPowers; }
	const ScienceStore &sciences() const { return *m_sciences; }
	const RankInfoStore &ranks() const { return *m_ranks; }
	const SpecialPowerStore &specialPowers() const { return *m_specialPowers; }
	// SPELL-1 part 2: TheObjectCreationListStore (RW 0xDE370C, the `ObjectCreationList` block RW 0x5F0462)
	ObjectCreationListStore &objectCreationLists() { return *m_ocls; }
	const ObjectCreationListStore &objectCreationLists() const { return *m_ocls; }

private:
	std::unique_ptr<ScienceStore> m_sciences;
	std::unique_ptr<RankInfoStore> m_ranks;
	std::unique_ptr<SpecialPowerStore> m_specialPowers;
	std::unique_ptr<ObjectCreationListStore> m_ocls;
};
