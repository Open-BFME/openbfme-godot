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
	case F::Patch202HoldGroundKeepsMove: return "patch-202.hold-ground-keeps-move";
	case F::Patch202BuildingLimitCountsPhantoms: return "patch-202.building-limit-counts-phantoms";
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
		// Patch 2.02 9.8.5 / 9.8.6 (2026-11-21): the first 2.02 binary with engine patches (the Ennorath team's, in named sections like Edain's).
		// Its .cmdext is Edain Unchained's commandset-limit (the same populate hook at +0x47D after RW 0x71CF0A, CommandSet 0xA0 -> 0x11C, the
		// bound 0x21 -> 0x40 at RW 0x7950E2 / 0x80C8E3 / 0x80C8FC / 0x80C91D) and its .obscmd is Edain's observer-command-range (RW 0x941BD2:
		// commands 0x37 / 0x38, PUSH / POP_VISIBLE_COMMAND_RANGE, pass for an inactive local player). Its .stancem (0xF34000, jumped to from RW
		// 0x862161 in setStance) is patch-202.hold-ground-keeps-move (GameLogic/Module/StancesBehavior.cpp); its .bldcap (0xF21000, the callback
		// canMakeUnit pushes at RW 0x79406F) is patch-202.building-limit-counts-phantoms (Common/BuildAssistant.cpp). Its .bldlvl and .bldext wrap
		// the control bar's availability and press (RW 0x942733, 0x940435) for a selection of several buildings: they belong to .bldgrp. The rest is
		// listed in the notes; the sections' purpose comes from the decoded hooks and, where marked, from the patch's own notes (2.02 Patch Notes.txt).
		{ "fc32e4c4142b5c607b90972eaf448400", "Patch 2.02 9.8.6 lotrbfme2ep1.exe (a whole game binary)",
			{ F::EdainUnchainedCommandSetLimit, F::EdainUnchainedObserverCommandRange, F::Patch202HoldGroundKeepsMove, F::Patch202BuildingLimitCountsPhantoms },
			"not yet re-implemented, game rules: units leaving a container together on Evacuate (.padsel: by the patch "
			"notes), the upgrade limit 5120 (.upg5k: OpenBFME holds 2048 upgrades, S-5303); interface and replays: select every building of a "
			"kind and give orders to all of them (.bldgrp, .bldlvl, .bldext), chat in replays and muting a player (.rpchat, .chatmut), replay pause (.rppause), the map search (.mapfind), hotkeys "
			"and text shortcuts (.hotkeys, .keydef, .keycmd, .txtclip), the camera zoom reset (.camfix), the settings menu's resolutions, 30 / 60 "
			"FPS and Delay Fix switch (.optui, .optfix, .ressort, .fpsvar, .fpsdraw, .fxrate, .dlyfix), War of the Ring with 8 players, online "
			"and without a host for land transfer (.wotrui, .wotrobs, .wtrview, .wtrdisp, .lobfac), a 500 ms throttle (.spstore); the mod's "
			"settings menu (apt/Options.big) is left out for the original game's",
			// its reworked Options.apt drives the new settings through .optui / .optfix: OpenBFME's Options screen (AptOptionsScreen) cannot find
			// retail's frames and bottom bar in it, so the menu draws broken and, from the main menu, cannot be left
			{ { "apt/options.big", "its settings menu needs the game program's own settings code (.optui), which OpenBFME does not have" } } },
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

std::vector<ModEngineFeatures::LeftOutArchive> ModEngineFeatures::leftOutArchives()
{
	std::vector<LeftOutArchive> out;
	for (const Profile &p : knownBinaries())
	{
		if (std::find(g_profiles.begin(), g_profiles.end(), std::string(p.what)) != g_profiles.end())
		{
			out.insert(out.end(), p.leftOut.begin(), p.leftOut.end());
		}
	}
	return out;
}
