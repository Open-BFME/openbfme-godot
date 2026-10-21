// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4b on the retail game (docs/SAVES.md T1, T2, T4 as far as SAVE-4b's blocks allow, and the measurement the brief asks for): a real 4-player
// skirmish (four Medium AIs on Fall Back 8p) at frame 3,000 is saved with SAVE-4b's blocks and the core, and loaded into the same START's game at frame
// 2,000: every object of the saved game is made again (the later game's objects are gone), and each object, the scheduler and the object-side subsystems
// hash as in the saved game; saving again gives the same bytes. The AI update family's modules (AIUpdateInterface and its subclasses) are SAVE-4c's: the
// test leaves them out on both sides (GameLogic::setSnapshotModuleFilter), named. The blocks' size and write time are reported.
// SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// SAVE-4a's game: four Medium AIs (Men, Mordor, Elves, Isengard) on the 8-player Fall Back, each on a team of its own
NewGameMessage fourPlayerMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const char *factions[4] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = SLOT_MED_AI;
		sl.name = u"Computer";
		sl.playerTemplate = templateIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(sl.playerTemplate != -1);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	return m;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void load(starttest::Shared &s, const NewGameMessage &m, Game &out)
{
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

void runTo(Game &g, UnsignedInt frame)
{
	while (g.game->logic().getFrame() < frame)
	{
		REQUIRE(g.game->advance(0.2) == 1);
		g.game->drainFrames();
	}
	REQUIRE(g.game->logic().getFrame() == frame);
}

// SAVE-4b's blocks, after the core they need loaded first (the object ids and teams), in the registry's order
const std::vector<std::string> kCore = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
const std::vector<std::string> kSave4b = { "obfme:Objects", "obfme:UpdateScheduler", "obfme:TunnelTracker", "obfme:Combat", "obfme:SellList",
	"obfme:Experience", "obfme:Invisibility", "obfme:DamageOverTime" };

std::vector<std::string> coreAndSave4b()
{
	std::vector<std::string> v = kCore;
	v.insert(v.end(), kSave4b.begin(), kSave4b.end());
	return v;
}

// the AI update family (AIUpdateInterface and its subclasses: SAVE-4c's xfers)
bool isAIUpdateFamily(const BehaviorModule &m)
{
	return const_cast<BehaviorModule &>(m).getAIUpdateInterface() != nullptr;
}

struct ObjectRecord
{
	ObjectID id;
	std::string templateName;
	std::uint32_t hash; ///< Object::crc without the AI update family's modules
	bool operator==(const ObjectRecord &o) const { return id == o.id && templateName == o.templateName && hash == o.hash; }
};

std::vector<ObjectRecord> objectRecords(const GameLogic &logic)
{
	std::vector<ObjectRecord> out;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		StateHasher h;
		o->crcOmittingModules(h, isAIUpdateFamily);
		out.push_back(ObjectRecord{ o->getID(), o->getTemplate()->getName(), h.value() });
	}
	return out;
}

std::map<std::string, std::uint32_t> sectionValues(const GameLogic &logic)
{
	std::vector<GameLogic::StateHashSection> sections;
	logic.computeStateHashBreakdown(sections, nullptr);
	std::map<std::string, std::uint32_t> out;
	for (const GameLogic::StateHashSection &s : sections)
	{
		out[s.name] = s.value;
	}
	return out;
}

// the sections SAVE-4b's blocks and the core hold entirely ("objects" holds the AI update family's modules too: compared object by object above)
const std::vector<std::string> kSave4bSections = { "frame, object ids, RNG", "players and teams", "scheduler", "combat", "sell list", "experience",
	"invisibility", "damage over time" };
} // namespace

TEST_CASE("save4b retail T1 and T2 (SAVE-4b's part): a 4-player skirmish at frame 3,000 loads into the same START's game at frame 2,000; every object, "
		  "the scheduler and the object-side subsystems hash as saved, and the snapshot saves the same bytes again; the size and write time are reported")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4190);
	// the loaded game has a world of its own (one live game per world, S-3808); its loads and queries run with its world's stores selected
	starttest::Shared *peer = starttest::peerWorld();
	REQUIRE(peer);
	REQUIRE_MESSAGE(peer->world, peer->error);
	Game a, b;
	load(*s, m, a);
	{
		auto peerContext = peer->world->enterContext();
		load(*peer, m, b);
	}
	runTo(a, 3000);
	runTo(b, 2000);
	GameLogic &la = a.game->logic();
	GameLogic &lb = b.game->logic();
	la.setSnapshotModuleFilter(isAIUpdateFamily);
	lb.setSnapshotModuleFilter(isAIUpdateFamily);

	const std::vector<ObjectRecord> saved = objectRecords(la);
	const std::map<std::string, std::uint32_t> savedSections = sectionValues(la);
	// the game at 2,000 differs in each compared part (the load must change it): its objects, and every section the game changed in those 1,000 frames
	REQUIRE_FALSE(objectRecords(lb) == saved);
	std::set<ObjectID> laterIds, earlierIds;
	for (const ObjectRecord &r : saved)
	{
		laterIds.insert(r.id);
	}
	for (const Object *o = lb.getFirstObject(); o; o = o->getNextObject())
	{
		earlierIds.insert(o->getID());
	}
	size_t made = 0, gone = 0;
	for (ObjectID id : laterIds)
	{
		made += earlierIds.count(id) ? 0 : 1;
	}
	for (ObjectID id : earlierIds)
	{
		gone += laterIds.count(id) ? 0 : 1;
	}
	REQUIRE(made > 0); // tolerance: a precondition, the game created objects between the two frames (the load must make them; their number is the game's)
	REQUIRE(gone > 0); // tolerance: a precondition, the game destroyed objects between the two frames (the load must remove them)

	std::vector<std::uint8_t> stream;
	la.gameState().saveBlocks(coreAndSave4b(), stream);
	{
		auto peerContext = peer->world->enterContext();
		lb.gameState().loadBlocks(coreAndSave4b(), stream);

		CHECK(lb.getFrame() == 3000);
		CHECK(lb.getObjectCount() == la.getObjectCount());
		const std::vector<ObjectRecord> loaded = objectRecords(lb);
		REQUIRE(loaded.size() == saved.size());
		size_t differing = 0;
		for (size_t i = 0; i < saved.size(); ++i)
		{
			if (!(loaded[i] == saved[i]))
			{
				if (differing < 10)
				{
					MESSAGE("object " << i << ": saved " << saved[i].id << " '" << saved[i].templateName << "' " << saved[i].hash << ", loaded " << loaded[i].id << " '"
						<< loaded[i].templateName << "' " << loaded[i].hash);
				}
				++differing;
			}
		}
		CHECK(differing == 0);
		const std::map<std::string, std::uint32_t> loadedSections = sectionValues(lb);
		for (const std::string &name : kSave4bSections)
		{
			CHECK_MESSAGE(loadedSections.count(name) == savedSections.count(name), name);
			if (savedSections.count(name) == 1 && loadedSections.count(name) == 1)
			{
				CHECK_MESSAGE(loadedSections.at(name) == savedSections.at(name), name);
			}
		}
		// T2: the loaded game saves the same bytes
		std::vector<std::uint8_t> again;
		lb.gameState().saveBlocks(coreAndSave4b(), again);
		CHECK(again == stream);
	}

	// the measurement (no threshold): each SAVE-4b block's size and the blocks' write time, the best of 20 (the AI update family's modules are left out: their
	// state is SAVE-4c's)
	std::string sizes;
	size_t total = 0;
	for (const std::string &t : kSave4b)
	{
		std::vector<std::uint8_t> one;
		la.gameState().saveBlocks({ t }, one);
		const size_t payload = one.size() - 16 - 7; // without the stream's 16-byte header and the 7-byte end token
		total += payload;
		sizes += t + " " + std::to_string(payload) + " B; ";
	}
	double best = 1e9;
	for (int i = 0; i < 20; ++i)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<std::uint8_t> w;
		la.gameState().saveBlocks(kSave4b, w);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		best = ms < best ? ms : best;
	}
	MESSAGE("SAVE-4b blocks of a 4-player skirmish at frame 3000 (" << la.getObjectCount() << " objects; made again " << made << ", removed " << gone
		<< "): " << total << " bytes (" << sizes << "), written in " << best << " ms (best of 20)");
}

TEST_CASE("save4b retail T4: a real 4-player skirmish's snapshot is written (SAVE-4c and 4d merged, SAVE-4e); every module class of its objects saves "
		  "with an xfer of its own, the AI update family's (SAVE-4e) included")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4191), g);
	runTo(g, 1500);
	GameLogic &logic = g.game->logic();
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(logic.saveSnapshot(out, &error), error);
	// lane SAVE-4e: without the filter every module saves, the AI update family through its own classes' xfers (their state is obfme:AIWorld's)
	std::vector<std::uint8_t> stream;
	logic.gameState().saveBlocks(coreAndSave4b(), stream);
	CHECK(!stream.empty());
	// with the AI update family left out, every module of every object saves through its own class's xfer (Object::xfer refuses an inherited one)
	logic.setSnapshotModuleFilter(isAIUpdateFamily);
	std::set<std::string> classes, aiClasses;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			(isAIUpdateFamily(*m) ? aiClasses : classes).insert(m->getModuleClassName());
		}
	}
	stream.clear();
	logic.gameState().saveBlocks(coreAndSave4b(), stream);
	std::string list;
	for (const std::string &c : classes)
	{
		list += c + " ";
	}
	std::string aiList;
	for (const std::string &c : aiClasses)
	{
		aiList += c + " ";
	}
	MESSAGE("module classes saved: " << classes.size() << ": " << list);
	// the exact inventory of this game's module classes (the INI class names; the unported ones save through their shell's xfer, S-140). Re-measured by
	// SAVE-4e on the merged trees (archive b5c2095d, then 612cff02 with merge queue q16's pathfinder patch), the same on the Deck and a CI gate: the
	// AIs' play changed since 0a18ec56. Ambient wandering now consumes its own logic RNG draws; at frame 1,500
	// this deterministic game also covers AutoHealBehavior and RepairSpecialPower (still no CommandButtonHuntUpdate).
	CHECK(classes == std::set<std::string>{
		"AISpecialPowerUpdate", "ActiveBody", "ArmorUpgrade", "ArrowStormUpdate", "AttributeModifierAuraUpdate", "AttributeModifierPoolUpdate",
		"AttributeModifierUpgrade", "AudioLoopUpgrade", "AutoAbilityBehavior", "AutoDepositUpdate", "AutoHealBehavior", "AutoPickUpUpdate",
		"BannerCarrierUpdate", "BezierProjectileBehavior", "BloodthirstyUpdate", "BoredUpdate", "BuildableHeroListUpgrade", "BuildingBehavior",
		"CastleBehavior", "CastleMemberBehavior", "CastleUpgrade", "CitadelSlaughterHordeContain", "ClickReactionBehavior", "CloudBreakSpecialPower",
		"CommandPointsUpgrade", "CommandSetUpgrade", "CostModifierUpgrade", "CreateObjectDie", "CritterEmitterUpdate",
		"DamageFilteredCreateObjectDie", "DarknessSpecialPower", "DefectionHelper", "DelayedDeathBody", "DestroyDie", "DevastateSpecialPower",
		"DoCommandUpgrade", "DualWeaponBehavior", "ElvenWoodSpecialPower", "EmotionTrackerUpdate", "FXListDie", "FiringTrackerHelper",
		"FoundationAIUpdate", "FreezingRainSpecialPower", "GeometryUpgrade", "GettingBuiltBehavior", "GiveUpgradeUpdate", "GrabPassengerSpecialPower",
		"GrantUpgradeCreate", "GuardingHelper", "HighlanderBody", "HitReactionBehavior", "HordeContain", "HordeDispatchSpecialPower", "HordeMemberCollide",
		"HordeNotifyTargetsOfImminentProbableCrushingUpdate", "HorseHordeContain", "ImmortalBody", "InactiveBody", "InvisibilityUpdate", "KeepObjectDie",
		"LargeGroupAudioUpdate", "LargeGroupBonusUpdate", "LevelUpUpgrade", "LifetimeUpdate", "ModelConditionUpgrade", "MonitorConditionUpdate",
		"OCLSpecialPower", "ObjectCreationUpgrade", "PassiveAreaEffectBehavior", "PhysicsBehavior", "PickupStuffUpdate", "PlayerHealSpecialPower",
		"PlayerUpgradeSpecialPower", "PorcupineFormationBodyModule", "ProductionSpeedBonus", "ProductionUpdate", "QueueProductionExitUpdate",
		"RebuildHoleExposeDie", "RecoveryHelper", "RefundDie", "RepairSpecialPower", "SMCHelper", "ScavengerSpecialPower", "ShareExperienceBehavior",
		"SiegeDeployHordeSpecialPower", "SlaughterHordeContain", "SlaveWatcherBehavior", "SlavedUpdate", "SlowDeathBehavior", "SpawnBehavior",
		"SpawnPointProductionExitUpdate", "SpawnUnitBehavior", "SpecialAbilityUpdate", "SpecialPowerModule", "SpellRechargeModifierUpgrade",
		"SquishCollide", "StancesBehavior", "StatusBitsUpgrade", "StealthDetectorUpdate", "StructureBody", "StructureCollapseUpdate", "SubObjectsUpgrade",
		"TaintSpecialPower", "TemporarilyDefectUpdate", "TerrainResourceBehavior", "TransitionDamageFX", "TransportContain", "UnpauseSpecialPowerUpgrade",
		"UntamedAllegianceSpecialPower", "WeaponFireSpecialAbilityUpdate", "WeaponSetUpgrade", "WeaponStatusHelper" });
	MESSAGE("AI update family classes left to SAVE-4c: " << aiList);
	CHECK(aiClasses == std::set<std::string>{ "AIUpdateInterface", "AnimalAIUpdate", "DozerAIUpdate", "HordeAIUpdate" });
}
