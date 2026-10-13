// OpenBFME retail tests, lane MAPRESET-1 (stop S-3808; the owner's decision of 2026-10-12: reset the map.ini overrides before every game). GPL-3.0.
//
// RotWK keeps a map's map.ini / solo.ini PlayerTemplate and skirmish AI blocks for the session: PlayerTemplateStore's and TheSkirmishAIManager's reset is the
// empty RW 0x63F3BF and GameLogic::reset drops TheGlobalData's overrides alone (RW 0x62D3F7). PLAY-4 r2 found the effect (a game after Helm's Deep placed
// FactionMordor's starting Porters at Helm's Deep's offsets). The engine departs from retail on purpose: every game starts from a freshly loaded world
// (RetailObjectWorld::reload: before a map's map.ini when an earlier map or game changed the world, and when a game ends), one game per world is live, and
// game setup reads the load's PlayerTemplates (setupPlayerTemplates).
//
// Expected data, read independently from the retail archives (tools/fx/bigfs.py): 123 map directories carry a map.ini (none a solo.ini); PlayerTemplate.ini's
// FactionMordor StartingUnitOffset0 X:1 Y:240, which map wor helms deep's map.ini overrides to X:1 Y:-50 next to a SkirmishAIData block (so do map wor
// dol guldur, erebor, grey havens, minas tirith, rivendell); map wor arnor's map.ini redefines FXParticleSystem AreaDustEffect (as 17 other maps do);
// ExperienceLevels.ini's first level MordorTavernLevel1; ObjectCreationList.ini's OCL_CritterRabbit. The block kinds no retail map.ini holds are driven by
// test map.ini texts parsed through the world's own INI environment with load type 2, the way applyMapIni parses a file. The tests SKIP loudly without the
// installs.
//
// Each test fails without the fix it checks (checked with control builds, see the lane report).

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/INIException.h"
#include "Common/SpecialPower.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/MiscAudio.h"
#include "GameLogic/Weapon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/FXList.h"
#include "GameClient/FXParticleSystem.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Armor.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameNetwork/LockstepDriver.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <cctype>
#include <map>
#include <set>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using starttest::Shared;

const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";
const char *const kDolGuldur = "maps/map wor dol guldur/map wor dol guldur.map";
const char *const kMinasTirith = "maps/map wor minas tirith/map wor minas tirith.map";
// the game every test compares: a 1 v 1 on Tournament Udun, the idle human as FactionMordor (Helm's Deep and Minas Tirith override its starting unit
// offsets), a Medium computer as FactionMen (the fortress maps' SkirmishAIData blocks change what it does)
const char *const kUdun = "maps/map mp tournament udun/map mp tournament udun.map";
const int kFrames = 150;

int factionIndex(const PlayerTemplateStore &store, const std::string &name)
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

const std::vector<MapCacheEntry> &mapCache(Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// slot i: kFactions[i], slot 0 the human, the rest Medium computers, start i
NewGameMessage message(Shared &s, const std::string &map, int players)
{
	static const char *const kFactions[] = { "FactionMordor", "FactionMen", "FactionElves", "FactionIsengard" };
	NewGameMessage m;
	m.game.mapName = map;
	m.game.seed = 9013u;
	m.game.startingCash = 1500;
	for (int i = 0; i < players; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.accepted = true;
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), kFactions[i % 4]);
		REQUIRE(slot.playerTemplate >= 0);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	return m;
}

struct Game
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	void load(Shared &s, const NewGameMessage &m)
	{
		std::string error;
		start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->setupPlayerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, *start, &error), error);
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options o;
		o.start = start.get();
		REQUIRE_MESSAGE(game->load(o, &error), error);
		REQUIRE(game->report().mapIniErrors.empty());
	}
	~Game()
	{
		if (game)
		{
			game->setFrameDriver(nullptr);
		}
	}
};

std::vector<Coord3D> mordorOffsets(const RetailObjectWorld &w)
{
	const PlayerTemplate *pt = const_cast<RetailObjectWorld &>(w).playerTemplates().findPlayerTemplate("FactionMordor");
	REQUIRE(pt);
	return std::vector<Coord3D>(pt->m_startingUnitOffset, pt->m_startingUnitOffset + PlayerTemplate::NUM_STARTING_UNITS);
}

bool sameOffsets(const std::vector<Coord3D> &a, const std::vector<Coord3D> &b)
{
	bool same = a.size() == b.size();
	for (size_t i = 0; same && i < a.size(); ++i)
	{
		same = a[i].x == b[i].x && a[i].y == b[i].y && a[i].z == b[i].z;
	}
	return same;
}

std::uint32_t aiStoreCrc(RetailObjectWorld &w)
{
	StateHasher h;
	w.skirmishAI().crc(h);
	return h.value();
}

// every entry of the two DamageFX tables equal
bool sameDamageFX(const DamageFX &a, const DamageFX &b)
{
	bool same = true;
	for (int t = 0; same && t < DAMAGEFX_TYPE_COUNT; ++t)
	{
		for (int v = 0; same && v < DAMAGEFX_VET_COUNT; ++v)
		{
			const DamageFX::Entry &x = a.entry(t, v), &y = b.entry(t, v);
			same = x.amountForMajorFX == y.amountForMajorFX && x.majorFX == y.majorFX && x.minorFX == y.minorFX && x.throttleTime == y.throttleTime;
		}
	}
	return same;
}

// an earlier game of a session: `map` loaded and played 5 frames; `during` sees the live game (its map's overrides applied)
void playEarlier(Shared &s, const std::string &map, int players, const std::function<void(RetailObjectWorld &)> &during)
{
	Game g;
	g.load(s, message(s, map, players));
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(g.game->advance(0.2) == 1);
	}
	during(*s.world);
}

// the local player's starting units (the objects it owns at frame 0 whose template is one of its PlayerTemplate's StartingUnit names)
std::vector<ObjectID> startingUnits(LiveGame &game)
{
	Player *local = game.players().getLocalPlayer();
	REQUIRE(local);
	const PlayerTemplate *pt = local->getPlayerTemplate();
	REQUIRE(pt);
	std::vector<ObjectID> out;
	for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != local)
		{
			continue;
		}
		for (const std::string &u : pt->m_startingUnit)
		{
			if (!u.empty() && o->getTemplate()->getName() == u)
			{
				out.push_back(o->getID());
				break;
			}
		}
	}
	return out;
}

struct Played
{
	std::vector<std::uint32_t> hashes; ///< the state hash after every frame
	std::vector<Coord3D> unitsAtStart; ///< the local player's starting units at frame 0, in id order
	Coord3D movedTo{}, movedFrom{};
};

// the compared game on Udun: kFrames frames; with `writer`, recorded, and at frame 10 the local player's starting units are ordered 200 east
Played playUdun(Shared &s, ReplayWriter *writer)
{
	Played out;
	Game g;
	const NewGameMessage m = message(s, kUdun, 2);
	g.load(s, m);
	const std::vector<ObjectID> units = startingUnits(*g.game);
	REQUIRE_FALSE(units.empty());
	for (ObjectID id : units)
	{
		out.unitsAtStart.push_back(*g.game->logic().findObjectByID(id)->getPosition());
	}
	std::unique_ptr<LockstepDriver> driver;
	if (writer)
	{
		driver = std::make_unique<LockstepDriver>(nullptr, writer);
		g.game->setFrameDriver(driver.get());
	}
	const int player = g.game->players().getLocalPlayer()->getPlayerIndex();
	for (int i = 0; i < kFrames; ++i)
	{
		if (writer && i == 10)
		{
			out.movedFrom = *g.game->logic().findObjectByID(units[0])->getPosition();
			GameMessage select(MSG_CREATE_SELECTED_GROUP, player);
			select.appendBooleanArgument(true);
			for (ObjectID id : units)
			{
				select.appendObjectIDArgument(id);
			}
			g.game->commands().append(select);
			GameMessage move(MSG_DO_MOVETO, player);
			move.appendLocationArgument(Coord3D{ out.movedFrom.x + 200.0f, out.movedFrom.y, out.movedFrom.z });
			g.game->commands().append(move);
		}
		REQUIRE(g.game->advance(0.2) == 1);
		out.hashes.push_back(g.game->logic().computeStateHash());
	}
	out.movedTo = *g.game->logic().findObjectByID(units[0])->getPosition();
	if (writer)
	{
		writer->close(g.game->frame(), out.hashes.back());
		CHECK(writer->errors().empty());
	}
	return out;
}

// the first frame whose hash differs, -1 when none
int firstDifference(const std::vector<std::uint32_t> &a, const std::vector<std::uint32_t> &b)
{
	for (size_t i = 0; i < a.size() && i < b.size(); ++i)
	{
		if (a[i] != b[i])
		{
			return (int)i;
		}
	}
	return a.size() == b.size() ? -1 : (int)std::min(a.size(), b.size());
}

// the earlier session of the shared world: Helm's Deep (PlayerTemplate and AI overrides) and Dol Guldur (AI overrides), each checked live during its game
// and gone after it
void helmsDeepThenDolGuldur(Shared &s)
{
	const std::vector<Coord3D> pristineOffsets = mordorOffsets(*s.world);
	const std::uint32_t pristineAi = aiStoreCrc(*s.world);
	REQUIRE(pristineOffsets[0].x == 1.0f);
	REQUIRE(pristineOffsets[0].y == 240.0f);
	playEarlier(s, kHelmsDeep, 4, [&](RetailObjectWorld &w) {
		CHECK(mordorOffsets(w)[0].x == 1.0f);
		CHECK(mordorOffsets(w)[0].y == -50.0f);
		CHECK(aiStoreCrc(w) != pristineAi);
	});
	CHECK(sameOffsets(mordorOffsets(*s.world), pristineOffsets));
	CHECK(aiStoreCrc(*s.world) == pristineAi);
	playEarlier(s, kDolGuldur, 4, [&](RetailObjectWorld &w) { CHECK(aiStoreCrc(w) != pristineAi); });
	CHECK(aiStoreCrc(*s.world) == pristineAi);
}

// what a load's history could change, read through the public stores: every name key, every template's name / id / override link, the PlayerTemplates'
// starting unit offsets, the skirmish AI data's CRC, the upgrade mask bits, the FXList parse counters, the store sizes, the macro count. Equal for two
// freshly loaded worlds; a reload makes the whole state again, so this is a probe of it, not its definition
std::vector<std::string> fingerprint(RetailObjectWorld &w)
{
	auto context = w.enterContext();
	std::vector<std::string> out;
	const NameKeyGenerator &keys = w.nameKeys();
	out.push_back("keys " + std::to_string(keys.nextId()));
	for (NameKeyType k = 1; k < keys.nextId(); k = (NameKeyType)(k + 1))
	{
		out.push_back("key " + std::to_string(k) + " " + keys.keyToName(k));
	}
	for (const ThingTemplate *t : w.things().templates())
	{
		out.push_back("thing " + t->getName() + " " + std::to_string(t->getTemplateID()) + (t->getFinalOverride() != t ? " overridden" : ""));
	}
	for (int i = 0; i < w.playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = w.playerTemplates().getNthPlayerTemplate(i);
		std::string line = "player-template " + pt->getName() + " " + std::to_string(pt->m_money.countMoney());
		for (int u = 0; u < PlayerTemplate::NUM_STARTING_UNITS; ++u)
		{
			line += " " + std::to_string(pt->m_startingUnitOffset[u].x) + "," + std::to_string(pt->m_startingUnitOffset[u].y);
		}
		out.push_back(line);
	}
	out.push_back("skirmish-ai " + std::to_string(aiStoreCrc(w)) + " types " + std::to_string(w.skirmishAI().playerAITypes().size()));
	out.push_back("upgrades " + std::to_string(w.upgrades().size()) + " next-bit " + std::to_string(w.upgrades().nextMaskBit()));
	out.push_back("fx-lists " + std::to_string(w.fxLists().listCount()) + " unvalidated " + std::to_string(w.fxLists().unvalidatedSoundNames()) + "/" +
		std::to_string(w.fxLists().unvalidatedEvaNames()));
	out.push_back("levels " + std::to_string(w.experienceLevels().levelCount()) + " scalar-tables " + std::to_string(w.experienceLevels().scalarTableCount()));
	out.push_back("commands " + std::to_string(w.commands().buttonCount()) + "/" + std::to_string(w.commands().setCount()));
	out.push_back("modifiers " + std::to_string(w.attributeModifiers().size()) + " stances " + std::to_string(w.stances().size()) + " nuggets " +
		std::to_string(w.emotions().nuggets().size()));
	out.push_back("special-powers " + std::to_string(TheSpecialPowerStore->getNumSpecialPowers()) + " weapons " + std::to_string(TheWeaponStore->size()) + " ocls " +
		std::to_string(TheObjectCreationListStore->size()) + " locomotors " + std::to_string(TheLocomotorStore->size()));
	out.push_back("macros " + std::to_string(w.iniMacros().size()) + " create-a-hero " + std::to_string(w.createAHeroSystem().heroRevivalDiscount));
	return out;
}

// "" when equal, else the first differing line of each and the counts
std::string firstDifferentLine(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
	for (size_t i = 0; i < a.size() || i < b.size(); ++i)
	{
		const std::string x = i < a.size() ? a[i] : "<end>", y = i < b.size() ? b[i] : "<end>";
		if (x != y)
		{
			return "line " + std::to_string(i) + ": [" + x + "] vs [" + y + "] (" + std::to_string(a.size()) + " / " + std::to_string(b.size()) + " lines)";
		}
	}
	return "";
}

// the directories of the retail maps with a map.ini or a solo.ini, e.g. "maps\map wor arnor"
std::vector<std::string> mapIniDirectories(Shared &s)
{
	FilenameList files;
	s.mount->fs->getFileListInDirectory("maps\\", "maps\\", "*.ini", files, true);
	std::set<std::string> dirs;
	for (const std::string &f : files)
	{
		std::string lower = f;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)(c == '/' ? '\\' : std::tolower(c)); });
		const size_t cut = lower.rfind('\\');
		const std::string leaf = cut == std::string::npos ? lower : lower.substr(cut + 1);
		if (leaf == "map.ini" || leaf == "solo.ini")
		{
			dirs.insert(lower.substr(0, cut));
		}
	}
	return std::vector<std::string>(dirs.begin(), dirs.end());
}

struct Synthetic
{
	std::string kind, text;
	bool throws; ///< the block is refused for load type 2 (retail's map.ini errors)
	const char *file = "map.ini"; ///< solo.ini for #define (INI::runDefinePass refuses macros in a file named map.ini, code 8)
};

// one test map.ini per block kind with a real parser in the world (and a stub block): a new entry and, where the kind has one, an override of a
// loaded one. Names of loaded entries come from the loaded stores.
std::vector<Synthetic> syntheticMapInis(RetailObjectWorld &w)
{
	REQUIRE(TheLocomotorStore);
	REQUIRE(TheWeaponStore);
	REQUIRE(TheDamageFXStore);
	REQUIRE(TheSpecialPowerStore);
	REQUIRE(w.fxParticleSystems());
	REQUIRE_FALSE(w.things().templates().empty());
	REQUIRE_FALSE(w.commands().buttonNames().empty());
	REQUIRE_FALSE(w.commands().setNames().empty());
	REQUIRE_FALSE(TheLocomotorStore->names().empty());
	REQUIRE_FALSE(w.fxLists().listNames().empty());
	REQUIRE_FALSE(w.fxParticleSystems()->templateNames().empty());
	REQUIRE_FALSE(TheWeaponStore->templates().empty());
	REQUIRE_FALSE(TheDamageFXStore->names().empty());
	REQUIRE(TheSpecialPowerStore->getSpecialPowerByIndex(0));
	const bool groupAudioLoaded = !w.audio().largeGroupAudio.maps().empty();
	const std::string thing = w.things().templates().front()->getName();
	const std::string button = w.commands().buttonNames().front(), set = w.commands().setNames().front();
	const std::string locomotor = TheLocomotorStore->names().front();
	const std::string fxList = w.fxLists().listNames().front();
	const std::string particle = w.fxParticleSystems()->templateNames().front();
	const std::string weapon = TheWeaponStore->templates().front()->getName();
	std::string damageFX; // the first DamageFX whose table differs from a cleared one: a map's empty block for it clears it in place
	for (const std::string &n : TheDamageFXStore->names())
	{
		if (damageFX.empty() && !sameDamageFX(*TheDamageFXStore->findDamageFX(n), DamageFX()))
		{
			damageFX = n;
		}
	}
	REQUIRE_FALSE(damageFX.empty());
	REQUIRE_FALSE(w.audio().infos.all().empty());
	const std::string sound = w.audio().infos.all().front()->audioName;
	const std::string power = TheSpecialPowerStore->getSpecialPowerByIndex(0)->getName();
	const std::string eva = EvaEventStore::predefinedNames()[1];
	const std::string groupAudio = groupAudioLoaded ? w.audio().largeGroupAudio.maps().front().name : std::string("MapReset1GroupAudio");
	return {
		{ "#define (solo.ini)", "#define MAPRESET1_MACRO 7\n", false, "solo.ini" },
		{ "Object (new)", "Object MapReset1Object\nEnd\n", false },
		{ "Object (override)", "Object " + thing + "\n  BuildCost = 4321\nEnd\n", false },
		{ "ChildObject", "ChildObject MapReset1Child " + thing + "\nEnd\n", false },
		{ "ObjectReskin", "ObjectReskin MapReset1Reskin " + thing + "\nEnd\n", false },
		{ "PlayerTemplate (new)", "PlayerTemplate MapReset1Faction\n  Side = MapReset1\n  PlayableSide = Yes\nEnd\n", false },
		{ "PlayerTemplate (override)", "PlayerTemplate FactionMordor\n  StartMoney = 4321\nEnd\n", false },
		{ "CommandButton (new)", "CommandButton Command_MapReset1\nEnd\n", false },
		{ "CommandButton (override)", "CommandButton " + button + "\nEnd\n", false },
		{ "CommandSet (new)", "CommandSet MapReset1Set\n  1 = " + button + "\nEnd\n", false },
		{ "CommandSet (override)", "CommandSet " + set + "\n  1 = Command_MapReset1\nEnd\n", false },
		{ "Upgrade", "Upgrade Upgrade_MapReset1\nEnd\n", false },
		{ "ExperienceLevel (override)", "ExperienceLevel MordorTavernLevel1\n  RequiredExperience = 4321\nEnd\n", false },
		{ "ExperienceScalarTable", "ExperienceScalarTable MapReset1Scalars\nEnd\n", false },
		{ "ModifierList", "ModifierList ModifierList_MapReset1\nEnd\n", false },
		{ "StanceTemplate", "StanceTemplate MapReset1Stances\nEnd\n", false },
		{ "EmotionNugget", "EmotionNugget MapReset1Nugget\nEnd\n", false },
		{ "CreateAHeroSystem", "CreateAHeroSystem\n  HeroRevivalDiscount = 33\nEnd\n", false },
		{ "LinearCampaign", "LinearCampaign MapReset1Campaign\nEnd\n", true },
		{ "Science", "Science SCIENCE_MapReset1\nEnd\n", false },
		{ "Rank (override)", "Rank 1\nEnd\n", false },
		{ "SpecialPower (new)", "SpecialPower MapReset1Power\nEnd\n", false },
		{ "SpecialPower (override)", "SpecialPower " + power + "\nEnd\n", false },
		{ "ObjectCreationList (new)", "ObjectCreationList OCL_MapReset1\nEnd\n", false },
		{ "ObjectCreationList (replace)", "ObjectCreationList OCL_CritterRabbit\nEnd\n", false },
		{ "SkirmishAIData", "SkirmishAIData TheSkirmishAIData\n  DisableBaseBuilding = Yes\nEnd\n", false },
		{ "ArmyDefinition", "ArmyDefinition MapReset1Army\n  Side = MapReset1\nEnd\n", false },
		{ "AIBase", "AIBase MapReset1Base\n  Side = MapReset1\n  Map = MapReset1Map\nEnd\n", false },
		{ "AIDozerAssignment", "AIDozerAssignment\n  Side = MapReset1\n  Unit = MapReset1Dozer\nEnd\n", false },
		{ "PlayerAIType", "PlayerAIType MapReset1AI\nEnd\n", false },
		{ "LoadSubsystem", "LoadSubsystem MapReset1Subsystem\nEnd\n", false },
		{ "Locomotor (new)", "Locomotor MapReset1Locomotor\nEnd\n", false },
		{ "Locomotor (override)", "Locomotor " + locomotor + "\nEnd\n", false },
		{ "FXList (new)", "FXList FX_MapReset1\nEnd\n", false },
		{ "FXList (replace)", "FXList " + fxList + "\nEnd\n", false },
		{ "FXParticleSystem (new)", "FXParticleSystem MapReset1Particles\nEnd\n", false },
		{ "FXParticleSystem (replace)", "FXParticleSystem " + particle + "\nEnd\n", false },
		{ "Weapon (new)", "Weapon MapReset1Weapon\nEnd\n", false },
		{ "Weapon (override)", "Weapon " + weapon + "\n  AttackRange = 432\nEnd\n", false },
		{ "Armor", "Armor MapReset1Armor\nEnd\n", false },
		{ "DamageFX", "DamageFX " + damageFX + "\nEnd\n", false },
		{ "PredefinedEvaEvent", "PredefinedEvaEvent " + eva + "\n  Priority = 9\nEnd\n", false },
		{ "NewEvaEvent", "NewEvaEvent MapReset1Eva\n  Priority = 9\nEnd\n", false },
		{ "LargeGroupAudioMap", "LargeGroupAudioMap " + groupAudio + "\nEnd\n", false },
		{ "MiscAudio", "MiscAudio\n  " + MiscAudio::fieldNames().front() + " = " + sound + "\nEnd\n", false },
		{ "AudioLOD", "AudioLOD High\n  MaximumAmbientStreams = 77\nEnd\n", false },
		{ "AudioEvent", "AudioEvent MapReset1Sound\nEnd\n", true },
		{ "Multisound", "Multisound MapReset1Multi\nEnd\n", true },
		{ "AudioSettings", "AudioSettings\nEnd\n", true },
		{ "AnimationSoundClientBehaviorGlobalSetting", "AnimationSoundClientBehaviorGlobalSetting\nEnd\n", true },
		{ "Weather (a recording stub)", "Weather\nEnd\n", false },
	};
}

// parses `text` as a map.ini of this world (load type 2); "" or the INI error
std::string loadMapIniText(RetailObjectWorld &w, const std::string &text, const char *file = "map.ini")
{
	try
	{
		INI ini(w.iniEnvironment());
		ini.loadMemory(std::string("maps\\mapreset1 test\\") + file, std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_CREATE_OVERRIDES);
	}
	catch (const std::exception &e)
	{
		return std::string("error: ") + e.what();
	}
	return "";
}
} // namespace

TEST_CASE("mapreset1 retail: after every retail map's map.ini, a test map.ini of every block kind and a game with Create-a-Hero levels, the world is a fresh load again")
{
	OPENBFME_REQUIRE_START(s);
	std::vector<std::string> fresh;
	{
		std::unique_ptr<Shared> w0 = starttest::freshWorld();
		REQUIRE(w0);
		REQUIRE_MESSAGE(w0->world, w0->error);
		fresh = fingerprint(*w0->world);
	}
	std::unique_ptr<Shared> h = starttest::freshWorld();
	REQUIRE(h);
	REQUIRE_MESSAGE(h->world, h->error);
	RetailObjectWorld &w = *h->world;
	auto context = w.enterContext();
	REQUIRE_MESSAGE(firstDifferentLine(fingerprint(w), fresh) == "", "two loads alike");
	CHECK_FALSE(w.changedSinceLoad());

	// every retail map's map.ini, one after another: each one applies to a fresh load
	const std::vector<std::string> dirs = mapIniDirectories(*h);
	CHECK(dirs.size() == 123);
	for (const std::string &dir : dirs)
	{
		w.applyMapIni(dir);
	}
	w.applyMapIni("maps\\map wor helms deep");
	CHECK(w.changedSinceLoad());
	CHECK(mordorOffsets(w)[0].y == -50.0f);
	// a test map.ini per block kind on top (the kinds no retail map.ini holds)
	for (const Synthetic &k : syntheticMapInis(w))
	{
		CAPTURE(k.kind);
		const std::string error = loadMapIniText(w, k.text, k.file);
		CAPTURE(error);
		CHECK(error.empty() == !k.throws);
	}
	CHECK(firstDifferentLine(fingerprint(w), fresh) != "");
	// the reload makes the whole state again: other stores, a fresh load's data
	const ThingFactory *before = &w.things();
	w.reload();
	CHECK(&w.things() != before);
	CHECK_FALSE(w.changedSinceLoad());
	{
		const std::string difference = firstDifferentLine(fingerprint(w), fresh);
		CHECK_MESSAGE(difference.empty(), difference);
	}

	// a game: its map, and what its Create-a-Hero does to the level store while it is live (RW 0x68AB07: a level for the hero, an existing one edited)
	{
		Game g;
		g.load(*h, message(*h, kHelmsDeep, 4));
		REQUIRE(TheExperienceLevelSystem);
		CHECK(TheExperienceLevelSystem->cloneLevelForTarget("MordorTavernLevel1", "CreateAHeroLevelMapReset1", "MapReset1Hero", "", "MapReset1Modifier"));
		CHECK(TheExperienceLevelSystem->cloneLevelForTarget("MordorTavernLevel1", "MordorTavernLevel1", "MapReset1Hero", "", "MapReset1Modifier"));
		CHECK(w.experienceLevels().findLevel("CreateAHeroLevelMapReset1"));
		for (int i = 0; i < 25; ++i)
		{
			REQUIRE(g.game->advance(0.2) == 1);
		}
		CHECK(mordorOffsets(w)[0].y == -50.0f);
		CHECK(firstDifferentLine(fingerprint(w), fresh) != "");
	}
	// its end loaded the world again
	CHECK_FALSE(w.changedSinceLoad());
	CHECK_FALSE(w.experienceLevels().findLevel("CreateAHeroLevelMapReset1"));
	{
		const std::string difference = firstDifferentLine(fingerprint(w), fresh);
		CHECK_MESSAGE(difference.empty(), difference);
	}
}

TEST_CASE("mapreset1 retail: while a game of a world is live, a second game (any map), another map.ini and a reload are refused by name")
{
	OPENBFME_REQUIRE_START(s);
	RetailObjectWorld &w = *s->world;
	{
		Game first;
		first.load(*s, message(*s, kHelmsDeep, 4));
		REQUIRE(w.gameLive());
		for (const char *map : { kUdun, kHelmsDeep })
		{
			CAPTURE(map);
			NewGameStart start(RandomAlgorithm::ZH_CarryChain);
			std::string error;
			REQUIRE_MESSAGE(NewGame::prepareNewGame(message(*s, map, map == kUdun ? 2 : 4), w.setupPlayerTemplates(), s->settings, mapCache(*s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
			ArchiveW3DFileSource source(*s->mount->fs);
			WW3DAssetManager assets(source);
			LiveGame second(w, *s->mount->fs, assets, s->options);
			LiveGame::Options o;
			o.start = &start;
			CHECK_FALSE(second.load(o, &error));
			CHECK(error.find("a game of this world is still live") != std::string::npos);
		}
		CHECK_THROWS_AS(w.applyMapIni("maps\\map wor arnor"), std::logic_error);
		CHECK_THROWS_AS(w.reload(), std::logic_error);
		// the live game's map is untouched
		CHECK(w.gameLive());
		CHECK(mordorOffsets(w)[0].y == -50.0f);
	}
	CHECK_FALSE(w.gameLive());
	CHECK_FALSE(w.changedSinceLoad());
	CHECK(mordorOffsets(w)[0].y == 240.0f); // the game's end loaded the world again
	// a map applied outside a game (a viewer) is gone when a game of another map loads: the world is loaded again under it
	w.applyMapIni("maps\\map wor helms deep");
	CHECK(w.changedSinceLoad());
	CHECK(mordorOffsets(w)[0].y == -50.0f);
	{
		Game g;
		g.load(*s, message(*s, kUdun, 2));
		CHECK(mordorOffsets(w)[0].y == 240.0f);
	}
	CHECK_FALSE(w.changedSinceLoad());
}

TEST_CASE("mapreset1 retail: game setup reads the templates the load made, not a live map's (a random faction resolves as in a fresh world)")
{
	OPENBFME_REQUIRE_START(s);
	RetailObjectWorld &w = *s->world;
	NewGameMessage m = message(*s, kUdun, 2);
	m.game.slots[1].playerTemplate = PLAYERTEMPLATE_RANDOM;
	auto resolved = [&](const PlayerTemplateStore &store) {
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		std::string error;
		REQUIRE_MESSAGE(NewGame::prepareNewGame(m, store, s->settings, mapCache(*s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
		return start.message.game.slots[1].playerTemplate;
	};
	const int before = resolved(w.setupPlayerTemplates());
	const int loadedCount = w.playerTemplates().getPlayerTemplateCount();
	// a map (a live game's, the shell map's) adds playable factions
	std::string text;
	for (int i = 0; i < 8; ++i)
	{
		text += "PlayerTemplate MapReset1Faction" + std::to_string(i) + "\n  Side = MapReset1\n  PlayableSide = Yes\n  StartingBuilding = Nothing\nEnd\n";
	}
	REQUIRE(loadMapIniText(w, text) == "");
	CHECK(w.playerTemplates().getPlayerTemplateCount() == loadedCount + 8);
	CHECK(w.setupPlayerTemplates().getPlayerTemplateCount() == loadedCount);
	CHECK(resolved(w.setupPlayerTemplates()) == before);
	CHECK(resolved(w.playerTemplates()) != before); // what setup would pick from the live store (the control)
	w.reload();
	CHECK(resolved(w.playerTemplates()) == before);
}

TEST_CASE("mapreset1: a world that was never loaded refuses a reload and a map.ini (no silent default)")
{
	OPENBFME_REQUIRE_START(s);
	RetailObjectWorld unloaded(*s->mount->fs);
	CHECK_THROWS_AS(unloaded.reload(), std::logic_error);
	CHECK_THROWS_AS(unloaded.applyMapIni("maps\\map wor helms deep"), std::logic_error);
}

TEST_CASE("mapreset1 retail: a game after Helm's Deep and Dol Guldur hashes, frame by frame, like the same game in a freshly loaded world")
{
	OPENBFME_REQUIRE_START(s);
	helmsDeepThenDolGuldur(*s);
	const Played sequential = playUdun(*s, nullptr);
	std::unique_ptr<Shared> fresh = starttest::freshWorld();
	REQUIRE(fresh);
	REQUIRE_MESSAGE(fresh->world, fresh->error);
	Played freshGame;
	{
		auto freshContext = fresh->world->enterContext();
		REQUIRE_FALSE(fresh->world->changedSinceLoad()); // no map.ini was ever applied to it
		freshGame = playUdun(*fresh, nullptr);
	}
	REQUIRE(sequential.hashes.size() == (size_t)kFrames);
	CHECK(firstDifference(sequential.hashes, freshGame.hashes) == -1);
	// the effect the earlier maps had changed: the human's starting units stand where a fresh world puts them
	REQUIRE(sequential.unitsAtStart.size() == freshGame.unitsAtStart.size());
	for (size_t i = 0; i < sequential.unitsAtStart.size(); ++i)
	{
		CHECK(sequential.unitsAtStart[i].x == freshGame.unitsAtStart[i].x);
		CHECK(sequential.unitsAtStart[i].y == freshGame.unitsAtStart[i].y);
	}
}

TEST_CASE("mapreset1 retail: two peers whose sessions played different maps before start the same game in sync")
{
	OPENBFME_REQUIRE_START(s);
	std::unique_ptr<Shared> other = starttest::freshWorld();
	REQUIRE(other);
	REQUIRE_MESSAGE(other->world, other->error);
	// peer A: Helm's Deep, then Dol Guldur; peer B: Minas Tirith (its own PlayerTemplate offsets and AI data)
	helmsDeepThenDolGuldur(*s);
	Played b;
	{
		auto otherContext = other->world->enterContext();
		const std::vector<Coord3D> pristine = mordorOffsets(*other->world);
		playEarlier(*other, kMinasTirith, 4, [&](RetailObjectWorld &w) { CHECK_FALSE(sameOffsets(mordorOffsets(w), pristine)); });
		b = playUdun(*other, nullptr);
	}
	const Played a = playUdun(*s, nullptr);
	REQUIRE(a.hashes.size() == (size_t)kFrames);
	CHECK(firstDifference(a.hashes, b.hashes) == -1);
}

TEST_CASE("mapreset1 retail: a game recorded after Helm's Deep plays back in a freshly loaded world (the catch-up of a rejoining player) with every frame's hash equal")
{
	OPENBFME_REQUIRE_START(s);
	const std::string path = "mapreset1_catchup.replay";
	helmsDeepThenDolGuldur(*s);
	Played recorded;
	{
		ReplayWriter writer;
		ReplayHeader h;
		h.game = message(*s, kUdun, 2);
		h.recordingSlot = 0;
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, h, &error), error);
		recorded = playUdun(*s, &writer);
	}
	// the order moved the starting units (the replay carries commands that change the game)
	CHECK(recorded.movedTo.x > recorded.movedFrom.x + 20.0f);
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	REQUIRE(replay.complete);
	REQUIRE(replay.hashes.size() == (size_t)kFrames);
	std::unique_ptr<Shared> fresh = starttest::freshWorld();
	REQUIRE(fresh);
	REQUIRE_MESSAGE(fresh->world, fresh->error);
	auto freshContext = fresh->world->enterContext();
	Game g;
	g.load(*fresh, replay.header.game);
	ReplayPlayback playback(replay);
	RegisterLogicCRCHandler(g.game->dispatch());
	g.game->setFrameDriver(&playback);
	while (!playback.finished(g.game->logic()))
	{
		g.game->advance(0.2);
	}
	g.game->setFrameDriver(nullptr);
	CHECK(playback.hashesCompared() == (unsigned long long)kFrames);
	CHECK_FALSE(playback.hasMismatch());
	std::remove(path.c_str());
}
