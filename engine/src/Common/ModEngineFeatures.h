// OpenBFME. GPL-3.0.
//
// Lane MODS-3: engine features of mods that change the game program. OpenBFME never runs a mod's game.dat / lotrbfme2ep1.exe. Each known
// modified binary (by MD5) is a profile: the named OpenBFME features that re-implement its changes (decoded against the retail binary in
// logs/mods-engine-edits-catalogue.md, lane MODS-3). A mod stack names its modified binaries (tools/mod/aio_package.py / the launcher record
// them in openbfme-aio.json "engine_edits"); the game receives `--mod-engine=<md5>` for each and turns the profile's features on. An MD5 no
// profile knows is an error: the mod's engine changes would be missing (never a silent run without them).
//
// The active features are part of the profile identity (ProfileIdentity extra features "mod-engine:<feature>"): two peers with different
// engine features refuse each other, and a replay names them. Code that implements a feature asks ModEngineFeatures::has(...); with no mod
// every feature is off and the game is retail 2.01.

#pragma once

#include <string>
#include <vector>

enum class ModEngineFeature
{
	// Edain Unchained 4.4.3 (its game.dat: install/ENGINEPATCHES_README.md names the patches)
	EdainUnchainedHerobarKindOf,       // KindOf HEROBAR (bit 222) / HEROBAR_GROUP (bit 223) in the name table (.hbar)
	EdainUnchainedQueueIgnoreCP,       // CommandButton QueueIgnoreCP (.qcp)
	EdainUnchainedCommandSetLimit,     // CommandSet slots 1..64 (.cmdext)
	EdainUnchainedCommandSetButtons,   // CommandSetUpgrade CommandButtons (.cmdbtn)
	EdainUnchainedObjectImageUpgrade,  // the ObjectImageUpgrade module (.objimg)
	EdainUnchainedPurchaseScienceSet,  // PurchaseScienceCommandSetUpgrade (.ssupgr)
	EdainUnchainedFireAtAttacker,      // FireWeaponWhenDamagedBehavior FireAtAttacker (.faa)
	EdainUnchainedLargeGroupBonus,     // LargeGroupBonusUpdate TriggeredBy / ConflictsWith / RequiresAll* / CountLooseObjects (.lgbupd)
	EdainUnchainedSciencePrereqs,      // PrerequisiteSciences may name a later science, checked after Science.ini (.scipre)
	EdainUnchainedSpawnUnion,          // every SpawnBehavior of an object (.spwnun)
	EdainUnchainedShareExperienceAll,  // every ShareExperienceBehavior, uncompounded (.sharexp)
	EdainUnchainedTriggerRechargeList, // OnTriggerRechargeSpecialPower takes a list (.trglst)
	EdainUnchainedUniqueProductionId,  // one production id counter for every building (.prodid)
	EdainUnchainedHeroRecruitParallel, // a hero entry not ready does not hold the queue (.hqueue)
	EdainUnchainedHordeExitAbsorption, // a unit joins the last horde out of the door only into a free slot of its type (.hrdexit)
	EdainUnchainedMultiExecuteGate,    // a group special power skips a member whose button the model conditions disable (.mxgate)
	EdainUnchainedFoundationRebind,    // a ReplaceSelfUpgrade on a plot or castle keep hands the plot to its first piece (.fndrbd)
	EdainUnchainedHerobar,             // HEROBAR objects in the hero bar, HEROBAR_GROUP ones merged into one cycling slot (.hbar)
	EdainUnchainedDescriptionTimers,   // the command button help's cooldown / build time / research time line (.dsctim)
	EdainUnchainedUpgradeDescription,  // an owned upgrade's help keeps its description, the purchased text below it (.upgdsc)
	EdainUnchainedObserverCommandRange, // an observer may page the command ranges (PUSH / POP_VISIBLE_COMMAND_RANGE) (.obscmd)
	EdainUnchainedProductionSplit,     // the PRODUCTION_MONEY / _UNIT / _UPGRADE / _CONSTRUCTION attribute modifiers (.prodsp)
	Count
};

class ModEngineFeatures
{
public:
	// the feature's stable name (identity text, reports): "edain-unchained.herobar-kindof"
	static const char *name(ModEngineFeature f);

	// a known modified binary: its MD5 (lower-case hex), what it is, and the features that stand in for it (possibly none: e.g. a launcher, or
	// only install / copy-protection checks a port does not have)
	struct Profile
	{
		const char *md5;
		const char *what;
		std::vector<ModEngineFeature> features;
		const char *notes; // what of the binary is NOT re-implemented ("" when nothing)
	};
	static const std::vector<Profile> &knownBinaries();

	// turn on the profiles of these MD5s (replacing the set). False + *error naming every unknown MD5; every feature is off then (retail).
	static bool activate(const std::vector<std::string> &md5s, std::string *error);
	static void reset(); // retail: every feature off
	static bool has(ModEngineFeature f);
	// the active profiles' names and features, for the identity ("mod-engine:<feature>") and reports
	static std::vector<std::string> identityFeatures();
	static std::vector<std::string> activeProfiles();
	// docs/STOPS.md S-5304: what the active profiles leave out or do differently, one line per active profile with notes ("" for none)
	static std::vector<std::string> stopLines();
};
