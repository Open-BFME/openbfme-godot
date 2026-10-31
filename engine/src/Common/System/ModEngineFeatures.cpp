// OpenBFME. GPL-3.0.
// See Common/ModEngineFeatures.h (lane MODS-3).

#include "Common/ModEngineFeatures.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace
{

using F = ModEngineFeature;

// set once at mount, before any data is parsed or a game starts; read by parsers and modules
std::array<std::atomic<bool>, (size_t)F::Count> g_on{};
std::vector<std::string> g_profiles;

} // namespace

const char *ModEngineFeatures::name(ModEngineFeature f)
{
	switch (f)
	{
	case F::EdainUnchainedHerobarKindOf: return "edain-unchained.herobar-kindof";
	case F::EdainUnchainedQueueIgnoreCP: return "edain-unchained.queue-ignore-cp";
	case F::EdainUnchainedCommandSetLimit: return "edain-unchained.commandset-limit";
	case F::EdainUnchainedCommandSetButtons: return "edain-unchained.commandset-button-upgrade";
	case F::EdainUnchainedObjectImageUpgrade: return "edain-unchained.object-image-upgrade";
	case F::EdainUnchainedPurchaseScienceSet: return "edain-unchained.spell-store-upgrade";
	case F::EdainUnchainedFireAtAttacker: return "edain-unchained.fire-at-attacker";
	case F::EdainUnchainedLargeGroupBonus: return "edain-unchained.large-group-bonus";
	case F::EdainUnchainedSciencePrereqs: return "edain-unchained.science-prereqs";
	case F::EdainUnchainedSpawnUnion: return "edain-unchained.spawn-union";
	case F::EdainUnchainedShareExperienceAll: return "edain-unchained.share-experience-all";
	case F::EdainUnchainedTriggerRechargeList: return "edain-unchained.trigger-recharge-list";
	case F::EdainUnchainedUniqueProductionId: return "edain-unchained.unique-production-id";
	case F::EdainUnchainedHeroRecruitParallel: return "edain-unchained.hero-recruit-parallel";
	case F::EdainUnchainedHordeExitAbsorption: return "edain-unchained.horde-exit-absorption";
	case F::EdainUnchainedMultiExecuteGate: return "edain-unchained.multi-execute-gate";
	case F::EdainUnchainedFoundationRebind: return "edain-unchained.foundation-rebind";
	case F::EdainUnchainedHerobar: return "edain-unchained.herobar";
	case F::EdainUnchainedDescriptionTimers: return "edain-unchained.description-timers";
	case F::EdainUnchainedUpgradeDescription: return "edain-unchained.upgrade-description";
	case F::EdainUnchainedObserverCommandRange: return "edain-unchained.observer-command-range";
	case F::EdainUnchainedProductionSplit: return "edain-unchained.production-split";
	case F::Count: break;
	}
	return "?";
}

const std::vector<ModEngineFeatures::Profile> &ModEngineFeatures::knownBinaries()
{
	// the MD5s of the modified binaries of the All In One launcher's RotWK packages (logs/mods-engine-edits-catalogue.md, lane MODS-3)
	static const std::vector<Profile> profiles = {
		{ "5c31f5036cacb7690a6161b104a3efdb", "Edain Unchained 4.4.3 game.dat",
			{ F::EdainUnchainedHerobarKindOf, F::EdainUnchainedQueueIgnoreCP, F::EdainUnchainedCommandSetLimit, F::EdainUnchainedCommandSetButtons,
				F::EdainUnchainedObjectImageUpgrade, F::EdainUnchainedPurchaseScienceSet, F::EdainUnchainedFireAtAttacker,
				F::EdainUnchainedLargeGroupBonus, F::EdainUnchainedSciencePrereqs, F::EdainUnchainedSpawnUnion, F::EdainUnchainedShareExperienceAll,
				F::EdainUnchainedTriggerRechargeList, F::EdainUnchainedUniqueProductionId, F::EdainUnchainedHeroRecruitParallel,
				F::EdainUnchainedHordeExitAbsorption, F::EdainUnchainedMultiExecuteGate,
				F::EdainUnchainedFoundationRebind, F::EdainUnchainedHerobar,
				F::EdainUnchainedDescriptionTimers, F::EdainUnchainedUpgradeDescription, F::EdainUnchainedObserverCommandRange,
				F::EdainUnchainedProductionSplit },
			"not yet re-implemented: the production rate of PRODUCTION_UNIT / _UPGRADE (the port has no PRODUCTION rate yet, S-203; no Edain data names "
			"them), desert weather (SAND, DESERT: no Edain data or map names them), the per-frame CRC, the hero bar's 32 slots (inert: Edain keeps the "
			"retail 16-slot movie) (logs/mods-engine-edits-catalogue.md)" },
		{ "4b7718e9548e6ceac7252978346a0266", "Edain Volker Sub Mod 1.7 game.dat", {},
			"not yet re-implemented: 2.02 frame pacing, desync reported in chat instead of ending the game, per-frame CRC, 31 factions in the "
			"LAN game info (OpenBFME's lobby already carries them)" },
		{ "7a4068f87ea5c3b98e03ae4e83e2b806", "Patch 2.02 lotrbfme2ep1.exe (a whole game binary)", {},
			"install / copy-protection checks (none in OpenBFME); EA's frame pacing restored (presentation timing, not lockstep state)" },
		{ "a6ce9397ee16376e7c84a72218ffe03a", "2.02 Delayfix Addon lotrbfme2ep1.exe", {},
			"install checks (none in OpenBFME); the 2.01 frame pacing (OpenBFME's)" },
		{ "f7ce90438eb50da4ce63804ba2688b6b", "RJ-RotWK launcher (a Qt program that starts the game, not a game binary)", {}, "" },
		{ "243edd56bb09405626bb455a67b5aa46", "RJ-RotWK file.dat (the launcher's settings)", {}, "" },
	};
	return profiles;
}

bool ModEngineFeatures::activate(const std::vector<std::string> &md5s, std::string *error)
{
	std::vector<const Profile *> chosen;
	std::string unknown;
	for (const std::string &m : md5s)
	{
		const std::string low = AsciiStringUtil::lowered(m);
		const Profile *found = nullptr;
		for (const Profile &p : knownBinaries())
		{
			if (low == p.md5)
			{
				found = &p;
			}
		}
		if (!found)
		{
			unknown += (unknown.empty() ? "" : ", ") + m;
			continue;
		}
		chosen.push_back(found);
	}
	if (!unknown.empty())
	{
		reset(); // nothing of a half-known engine: retail
		if (error)
		{
			*error = "this mod changes the game program in a way OpenBFME does not know yet (unknown game binary " + unknown +
				"): its engine changes would be missing";
		}
		return false;
	}
	reset();
	for (const Profile *p : chosen)
	{
		g_profiles.push_back(p->what);
		for (ModEngineFeature f : p->features)
		{
			g_on[(size_t)f].store(true);
		}
	}
	return true;
}

void ModEngineFeatures::reset()
{
	for (auto &b : g_on)
	{
		b.store(false);
	}
	g_profiles.clear();
}

bool ModEngineFeatures::has(ModEngineFeature f)
{
	return g_on[(size_t)f].load(std::memory_order_relaxed);
}

std::vector<std::string> ModEngineFeatures::identityFeatures()
{
	std::vector<std::string> out;
	for (size_t i = 0; i < (size_t)F::Count; ++i)
	{
		if (g_on[i].load())
		{
			out.push_back(std::string("mod-engine:") + name((F)i));
		}
	}
	return out;
}

std::vector<std::string> ModEngineFeatures::stopLines()
{
	std::vector<std::string> out;
	for (const Profile &p : knownBinaries())
	{
		if (*p.notes && std::find(g_profiles.begin(), g_profiles.end(), std::string(p.what)) != g_profiles.end())
		{
			out.push_back(std::string("S-5304: ") + p.what + ": " + p.notes);
		}
	}
	return out;
}

std::vector<std::string> ModEngineFeatures::activeProfiles()
{
	return g_profiles;
}
