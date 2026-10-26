// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4d (docs/SAVES.md section 6, tests T1-T5 and T8 as far as the blocks allow): the blocks obfme:ScriptEngine, obfme:Victory, obfme:Weather,
// obfme:Economy, obfme:CreateAHero, obfme:TerrainTrees (retail, test_save4d_retail.cpp) and obfme:LargeGroupAudio. Each block is saved from a game that
// changed its state to non-default values and loaded into a new game of the same START: the block's state hash section takes the saved game's value (it
// differed before), the state reads back field by field, and the block saves the same bytes again (T2). Retail's byte layouts (the weather, the terrain
// resource manager, the Create-a-Hero record, the wide string) are pinned byte by byte; every refusal is named (T5).

#include "doctest.h"

#include "EconGameData.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/CreateAHeroRecord.h"
#include "Common/Dict.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Science.h"
#include "Common/GameState.h"
#include "Common/JobSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Common/INI.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/System/TerrainResourceManager.h"
#include "GameLogic/SpellStores.h"
#include "GameLogic/VictoryConditions.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{
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

std::uint32_t section(const GameLogic &logic, const std::string &name)
{
	const std::map<std::string, std::uint32_t> v = sectionValues(logic);
	auto it = v.find(name);
	REQUIRE_MESSAGE(it != v.end(), "no state hash section '" << name << "'");
	return it->second;
}

std::string loadError(GameLogic &logic, const std::vector<std::string> &tokens, const std::vector<std::uint8_t> &stream)
{
	try
	{
		logic.gameState().loadBlocks(tokens, stream);
		return "(loaded)";
	}
	catch (const XferException &e)
	{
		return e.what();
	}
}

// the offset of a one-block stream's payload: the 16-byte stream header, the token (a u8 length and its bytes), "BLOK" and the end offset
size_t payloadOf(const std::string &token)
{
	return 16 + 1 + token.size() + 8;
}

// TheAttributeModifierStore with the ModifierList the weather tests name (the weather's load checks the name against the data)
struct ModifierStoreScope
{
	AttributeModifierStore store;
	AttributeModifierStore *previous = TheAttributeModifierStore;
	ModifierStoreScope()
	{
		INIEnvironment env;
		store.registerBlock(env.blocks);
		INI ini(env);
		const std::string text = "ModifierList Darkness\n  Category = SPELL\n  Modifier = ARMOR 50%\n  Duration = 0\nEnd\n";
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		TheAttributeModifierStore = &store;
		ini.loadMemory("Data\\INI\\AttributeModifier.ini", bytes, INI_LOAD_OVERWRITE);
		REQUIRE(store.find("Darkness") != nullptr);
	}
	~ModifierStoreScope() { TheAttributeModifierStore = previous; }
};

void runFrames(GameLogic &logic, int n)
{
	for (int i = 0; i < n; ++i)
	{
		logic.runLogicFrame();
	}
}

std::vector<std::uint8_t> blockBytes(GameLogic &logic, const std::string &token)
{
	std::vector<std::uint8_t> stream;
	logic.gameState().saveBlocks({ token }, stream);
	return stream;
}
} // namespace

TEST_CASE("save4d Xfer: a wide string is retail's (RW 0xA1FC7E): the AsciiString's length prefix counting units, then the units in ONE transfer")
{
	XferSave s;
	std::u16string shortS = u"Bob", longS(300, u'é');
	s.xferUnicodeString(&shortS);
	s.xferUnicodeString(&longS);
	const std::vector<std::uint8_t> &d = s.data();
	REQUIRE(d.size() == 1u + 6u + 5u + 600u);
	CHECK(std::vector<std::uint8_t>(d.begin(), d.begin() + 7) == std::vector<std::uint8_t>{ 3, 'B', 0, 'o', 0, 'b', 0 });
	CHECK(std::vector<std::uint8_t>(d.begin() + 7, d.begin() + 12) == std::vector<std::uint8_t>{ 0xFF, 0x2C, 0x01, 0, 0 });
	CHECK(d[12] == 0xE9);
	CHECK(d[13] == 0x00);
	XferLoad l(d);
	std::u16string a, b;
	l.xferUnicodeString(&a);
	l.xferUnicodeString(&b);
	CHECK(a == shortS);
	CHECK(b == longS);
	CHECK(l.atEnd());
	// XferCRC: the 6 bytes of "Bob" are one transfer (one whole word, then the two trailing bytes), as retail's single xferImplementation
	XferCRC c1, c2;
	c1.xferUnicodeString(&shortS);
	std::uint8_t len = 3;
	c2.xferUnsignedByte(&len);
	std::uint8_t units[6] = { 'B', 0, 'o', 0, 'b', 0 };
	c2.xferUser(units, 6);
	CHECK(c1.getCRC() == c2.getCRC());
	// a declared length beyond the bytes left is named before anything is allocated
	const std::vector<std::uint8_t> bad = { 0xFF, 0x00, 0x00, 0x01, 0x00, 'x', 0 };
	XferLoad lb(bad);
	std::u16string c;
	std::string error;
	try
	{
		lb.xferUnicodeString(&c);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "a wide string declares 65536 units, more than the 2 bytes left hold");
}

TEST_CASE("save4d weather (RW 0x71A2E3) and large group audio: the blocks carry a game's weather, its modifier and countdowns, the burn decay and the "
		  "audio gate into a game of the same START; retail's field order byte by byte; the anti mask by name (RW 0x71A1A7)")
{
	ModifierStoreScope modifiers;
	logictest::LogicWorld a;
	runFrames(*a.logic, 3);
	auto filter = std::make_shared<ObjectFilter>();
	filter->includeNames = { "AlphaKeep" };
	filter->rule = ObjectFilter::RULE_ANY;
	filter->flag = true;
	GlobalWeatherSystem &w = a.logic->weather();
	w.setWeather(3, 7, 40);
	w.setWeatherModifier(GlobalWeatherSystem::AFFECT_EVIL, filter, a.players.findPlayerWithName("Bob"), "Darkness", (1u << 3) | (1u << 9), 30);
	a.logic->largeGroupAudio().enable(17);
	const std::uint32_t weatherA = section(*a.logic, "weather"), audioA = section(*a.logic, "large group audio");

	const std::vector<std::uint8_t> weather = blockBytes(*a.logic, "obfme:Weather");
	// retail's order: version 4, the weather, the affect kind, the filter (the port's layout, S-4251), the name, the category word 0, the mask, the
	// weather frames, the modifier frames, the anti mask's names; then the OpenBFME trailer (version 1, the burn decay)
	const size_t p = payloadOf("obfme:Weather");
	CHECK(weather[p] == 4);
	CHECK(std::vector<std::uint8_t>(weather.begin() + (std::ptrdiff_t)p + 1, weather.begin() + (std::ptrdiff_t)p + 9) ==
		std::vector<std::uint8_t>{ 3, 0, 0, 0, 2, 0, 0, 0 });
	const std::uint8_t bob = (std::uint8_t)(1u << a.players.findPlayerWithName("Bob")->getPlayerIndex());
	const std::vector<std::uint8_t> expectTail = {
		8, 'D', 'a', 'r', 'k', 'n', 'e', 's', 's', 0, 0, 0, 0,     // the name, the category word (always 0 when saving)
		bob, 0, 0, 0, 40, 0, 0, 0, 30, 0, 0, 0,                    // the player mask, the weather frames, the modifier frames
		1, 2, 0, 0, 0, 5, 'S', 'P', 'E', 'L', 'L', 4, 'S', 'T', 'U', 'N', // BitFlags<15>: version 1, two names in bit order (3 SPELL, 9 STUN)
		1, 7, 0, 0, 0,                                             // the OpenBFME trailer: version 1, the burn decay
	};
	CHECK(std::vector<std::uint8_t>(weather.end() - 7 - (std::ptrdiff_t)expectTail.size(), weather.end() - 7) == expectTail); // 7: the end token
	logictest::LogicWorld b;
	runFrames(*b.logic, 3);
	REQUIRE(section(*b.logic, "weather") != weatherA);
	REQUIRE(section(*b.logic, "large group audio") != audioA);
	b.logic->gameState().loadBlocks({ "obfme:Weather" }, weather);
	const std::vector<std::uint8_t> audio = blockBytes(*a.logic, "obfme:LargeGroupAudio");
	b.logic->gameState().loadBlocks({ "obfme:LargeGroupAudio" }, audio);
	CHECK(section(*b.logic, "weather") == weatherA);
	CHECK(section(*b.logic, "large group audio") == audioA);
	const GlobalWeatherSystem &wb = b.logic->weather();
	CHECK(wb.weather() == 3);
	CHECK(wb.affectKind() == GlobalWeatherSystem::AFFECT_EVIL);
	CHECK(wb.modifierName() == "Darkness");
	CHECK(wb.weatherFramesLeft() == 40u);
	CHECK(wb.modifierFramesLeft() == 30u);
	CHECK(wb.burnDecay() == 7u);
	CHECK(b.logic->largeGroupAudio().gateFrame() == 17);
	CHECK_FALSE(b.logic->largeGroupAudio().disabled());
	CHECK(blockBytes(*b.logic, "obfme:Weather") == weather); // T2
	CHECK(blockBytes(*b.logic, "obfme:LargeGroupAudio") == audio);
	// both run on equal: the countdowns reach the clear at the same frame
	runFrames(*a.logic, 31);
	runFrames(*b.logic, 31);
	CHECK(a.logic->weather().affectKind() == GlobalWeatherSystem::AFFECT_NONE);
	CHECK(section(*b.logic, "weather") == section(*a.logic, "weather"));

	// T5: an anti category the list lacks, a version above the current and below the earliest, retail's messages
	std::vector<std::uint8_t> badName = weather;
	const size_t spell = weather.size() - 7 - 5 - 5 - 5; // the 'S' of "SPELL"
	REQUIRE(badName[spell] == 'S');
	badName[spell] = 'X';
	CHECK(loadError(*b.logic, { "obfme:Weather" }, badName) == "block 'obfme:Weather': AntiCategory flags: no flag named 'XPELL'");
	std::vector<std::uint8_t> v5 = weather;
	v5[p] = 5;
	CHECK(loadError(*b.logic, { "obfme:Weather" }, v5) == "block 'obfme:Weather': Don't know how to load version 5 (most recent is 4)");
	v5[p] = 0;
	CHECK(loadError(*b.logic, { "obfme:Weather" }, v5) == "block 'obfme:Weather': Found version 0 but the earliest supported is 1");
}

TEST_CASE("save4d victory: the rule, the defeat counter, the script flags and the cached players (by index) load into a game of the same START; the "
		  "peer's own slot is not in the bytes")
{
	logictest::LogicWorld a, b;
	for (logictest::LogicWorld *w : { &a, &b })
	{
		// GameData's victory filters (configuration of both games); Bob owns nothing, so no filter can find him a base or an army
		w->logic->settings().victoryRulesLoaded = true;
		w->logic->settings().victoryStructureFilter = std::make_shared<ObjectFilter>();
		w->logic->settings().victoryUnitFilter = std::make_shared<ObjectFilter>();
		w->logic->victory().initWithPlayers({ w->players.findPlayerWithName("Alice"), w->players.findPlayerWithName("Bob") });
	}
	VictoryConditions &va = a.logic->victory();
	va.setRule(VictoryConditions::RULE_BASE_OR_BUILDERS);
	CHECK(va.hasSinglePlayerBeenDefeated(a.players.findPlayerWithName("Bob"))); // no object of Bob: defeated, the counter counts it
	va.setScriptFlag91(true);
	REQUIRE(va.defeatCounter() == 1);
	const std::uint32_t victoryA = section(*a.logic, "victory");
	REQUIRE(section(*b.logic, "victory") != victoryA);
	const std::vector<std::uint8_t> bytes = blockBytes(*a.logic, "obfme:Victory");
	b.logic->gameState().loadBlocks({ "obfme:Victory" }, bytes);
	CHECK(section(*b.logic, "victory") == victoryA);
	const VictoryConditions &vb = b.logic->victory();
	CHECK(vb.rule() == VictoryConditions::RULE_BASE_OR_BUILDERS);
	CHECK(vb.defeatCounter() == 1);
	CHECK(vb.cachedPlayers() == 2);
	CHECK(vb.cachedPlayerIndex(1) == b.players.findPlayerWithName("Bob")->getPlayerIndex());
	CHECK(vb.localSlot() == va.localSlot());
	CHECK(blockBytes(*b.logic, "obfme:Victory") == bytes);
	// T5: a cached player index this game lacks
	std::vector<std::uint8_t> bad = bytes;
	const size_t firstPlayer = payloadOf("obfme:Victory") + 1 + 4 + 4 + 1 + 4 + 4 + 1 + 1;
	REQUIRE(bad[firstPlayer] == (std::uint8_t)a.players.findPlayerWithName("Alice")->getPlayerIndex());
	bad[firstPlayer] = 19;
	CHECK(loadError(*b.logic, { "obfme:Victory" }, bad) == "block 'obfme:Victory': victory: the cached player 0 is player index 19, which this game lacks");
}

namespace
{
const char kEconObjects[] =
	"Object Farm\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  Behavior = TerrainResourceBehavior ModuleTag_Money\n"
	"    Radius = 60\n"
	"    MaxIncome = 100\n"
	"    IncomeInterval = 15000\n"
	"  End\n"
	"End\n";

// a flat 400 x 400 world with a blocked (water) strip, as the ECON-1 tests build it
class Terrain : public TerrainResourceTerrain
{
public:
	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override
	{
		loX = loY = 0.0f;
		hiX = hiY = 400.0f;
	}
	bool cellTypeAt(float x, float y, int &type) const override
	{
		type = x < 40.0f ? 1 : 0;
		return x >= 0 && y >= 0 && x < 400.0f && y < 400.0f;
	}
};

struct EconWorld : logictest::LogicWorld
{
	Terrain terrain;
	EconWorld()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load(kEconObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		std::string e;
		REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, logic->economy().settings(), &e), e);
		logic->economy().initAllCommandPoints();
		logic->economy().resources().init(terrain, 20.0f);
	}
	Object *farmAt(const char *owner, int cx, int cy, bool complete)
	{
		Object *o = make("Farm", teamOf(owner));
		Coord3D p{ (float)cx * 20.0f, (float)cy * 20.0f, 0.0f };
		o->setPosition(&p);
		logic->runLogicFrame(); // the module claims on its first update
		if (complete)
		{
			o->friend_onBuildComplete();
		}
		return o;
	}
};
} // namespace

TEST_CASE("save4d economy: the terrain resource manager in retail's DoXfer layout (RW 0x75C7AE) and the context load into a game of the same START; the "
		  "complete flags retail drops are kept (S-4253); other GameData is refused")
{
	EconWorld a;
	a.farmAt("Alice", 10, 10, true);
	a.farmAt("Bob", 12, 10, false);
	a.logic->economy().setScoring(false);
	const TerrainResourceManager &ma = a.logic->economy().resources();
	REQUIRE(ma.claimants().size() == 2u);
	REQUIRE(ma.claimants()[0].complete);
	REQUIRE_FALSE(ma.claimants()[1].complete);
	const std::uint32_t economyA = section(*a.logic, "economy");
	const std::vector<std::uint8_t> bytes = blockBytes(*a.logic, "obfme:Economy");

	// retail's layout after the settings hash and the context: version 3, the claimant count, each id / radius / visible, the region, the size, the cell
	// size, the largest radius, the cell count
	const size_t trm = payloadOf("obfme:Economy") + 1 + 4 + 4 + 4 + 1 + 1 + 4 + 1 + 1;
	CHECK(bytes[trm] == 3);
	CHECK(std::vector<std::uint8_t>(bytes.begin() + (std::ptrdiff_t)trm + 1, bytes.begin() + (std::ptrdiff_t)trm + 5) == std::vector<std::uint8_t>{ 2, 0, 0, 0 });
	const size_t grid = trm + 5 + 2 * 9 + 24;
	CHECK(std::vector<std::uint8_t>(bytes.begin() + (std::ptrdiff_t)grid, bytes.begin() + (std::ptrdiff_t)grid + 8) ==
		std::vector<std::uint8_t>{ 20, 0, 0, 0, 20, 0, 0, 0 });
	CHECK(std::vector<std::uint8_t>(bytes.begin() + (std::ptrdiff_t)grid + 16, bytes.begin() + (std::ptrdiff_t)grid + 20) ==
		std::vector<std::uint8_t>{ 0x90, 0x01, 0, 0 }); // 400 cells
	// the trailer: version 1, the two claimants' complete flags
	CHECK(std::vector<std::uint8_t>(bytes.end() - 7 - 3, bytes.end() - 7) == std::vector<std::uint8_t>{ 1, 1, 0 });

	EconWorld b;
	b.make("Farm", b.teamOf("Alice")); // the same objects (ids 1 and 2), which have not claimed their ground (no frame ran)
	b.make("Farm", b.teamOf("Bob"));
	REQUIRE(b.logic->economy().resources().claimants().empty());
	REQUIRE(section(*b.logic, "economy") != economyA);
	b.logic->gameState().loadBlocks({ "obfme:Economy" }, bytes);
	CHECK(section(*b.logic, "economy") == economyA);
	const TerrainResourceManager &mb = b.logic->economy().resources();
	REQUIRE(mb.claimants().size() == 2u);
	CHECK(mb.claimants()[0].id == ma.claimants()[0].id);
	CHECK(mb.claimants()[0].complete);
	CHECK_FALSE(mb.claimants()[1].complete);
	CHECK(mb.blockedCells() == 40u); // the two water columns, recomputed from the loaded cells
	CHECK(mb.cellAt(10, 10)->state == TerrainResourceManager::CELL_OWNED);
	CHECK(mb.cellAt(10, 10)->claims.front().objectId == ma.claimants()[0].id);
	CHECK_FALSE(b.logic->economy().context().scoring);
	CHECK(blockBytes(*b.logic, "obfme:Economy") == bytes);

	// T5: another game (its lobby's command points), other GameData economy values
	EconWorld d;
	d.logic->economy().context().lobbyCommandPointPercent = 150;
	CHECK(loadError(*d.logic, { "obfme:Economy" }, bytes) ==
		"block 'obfme:Economy': economy: the snapshot's game (mode 2, kind 3, command points 100%) is not this game's (mode 2, kind 3, command points 150%)");
	EconWorld c;
	c.logic->economy().settings().goodLimit += 1;
	CHECK(loadError(*c.logic, { "obfme:Economy" }, bytes) ==
		"block 'obfme:Economy': economy: the snapshot's GameData economy settings differ from this game's (another data set)");
}

TEST_CASE("save4d Create-a-Hero: the game's heroes in retail's record layout (RW 0x80B4F5, version 8) with the game's validity and flags")
{
	// a synthetic hero of no Create-a-Hero data: TheCreateAHeroSystem of a retail world a test before left on this thread (thread_local) must not judge it
	CreateAHeroSystem *const savedCah = TheCreateAHeroSystem;
	TheCreateAHeroSystem = nullptr;
	struct Restore
	{
		CreateAHeroSystem *saved;
		~Restore() { TheCreateAHeroSystem = saved; }
	} restoreCah{ savedCah };
	CreateAHeroHero hero;
	hero.objectID = 77;
	hero.name = u"Gréta";
	hero.classIndex = 2;
	hero.subClassIndex = 1;
	hero.primaryColor = 0xFF102030u;
	hero.powers[0].commandButton = "Command_CAHPower";
	hero.powers[0].expLevel = 3;
	hero.powers[0].buttonIndex = 4;
	hero.setBling("Head", 2);
	hero.setBling("Cape", 1);
	hero.uniqueID = "uid-1";
	hero.isSystemHero = true;
	hero.checksum = 0x12345678u;
	hero.valid = false;
	hero.flags = 0x2FFu;
	hero.version = 7;
	logictest::LogicWorld a, b;
	a.logic->createAHeroes().assign(*a.players.findPlayerWithName("Bob"), hero);
	const std::uint32_t cahA = section(*a.logic, "create-a-hero");
	const std::vector<std::uint8_t> bytes = blockBytes(*a.logic, "obfme:CreateAHero");
	// the record starts with retail's version 8, the id, the name (a u8 count of units)
	const size_t rec = payloadOf("obfme:CreateAHero") + 1 + 4 + 4;
	CHECK(std::vector<std::uint8_t>(bytes.begin() + (std::ptrdiff_t)rec, bytes.begin() + (std::ptrdiff_t)rec + 7) ==
		std::vector<std::uint8_t>{ 8, 77, 0, 0, 0, 5, 'G' });
	CHECK(b.logic->createAHeroes().heroes().empty());
	b.logic->gameState().loadBlocks({ "obfme:CreateAHero" }, bytes);
	CHECK(section(*b.logic, "create-a-hero") == cahA);
	const auto &heroes = b.logic->createAHeroes().heroes();
	REQUIRE(heroes.size() == 1u);
	CHECK(heroes.begin()->second == hero); // every field, the game's version 7, validity and flags included
	CHECK(blockBytes(*b.logic, "obfme:CreateAHero") == bytes);
}

namespace
{
struct AudioHost : ScriptEngineHost
{
	Object *createObject(const ThingTemplate &, Team &, const Coord3D &, float) override { return nullptr; }
	bool audioLengthMs(const std::string &name, std::int32_t &ms, bool &picked) override
	{
		ms = name == "Horn" ? 4000 : 0;
		picked = false;
		return name == "Horn";
	}
};

// the scripts of the script engine tests; `swapped` lists Alice's two scripts in the other order (the same counts, another tree)
void buildScripts(ScriptGame &g, bool swapped = false)
{
	if (swapped)
	{
		g.lists[1].items.push_back(item(script("later", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "later") }) })));
	}
	g.lists[1].items.push_back(item(script("start", { { cond("CONDITION_TRUE", {}) } },
		{ act("SET_MILLISECOND_TIMER", { sp(4, "t"), rp(9.0f) }), act("INCREMENT_COUNTER", { ip(0, 3), sp(4, "starts") }), act("SET_FLAG", { sp(5, "go"), ip(8, 1) }),
			act("DISABLE_SCRIPT", { sp(2, "later") }), act("OBJECTLIST_ADDOBJECTTYPE", { sp(48, "L"), sp(15, "Orc") }) },
		{}, true)));
	if (!swapped)
	{
		g.lists[1].items.push_back(item(script("later", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "later") }) })));
	}
	auto delayed = script("delayed", { { cond("CONDITION_TRUE", {}) } }, { act("INCREMENT_COUNTER", { ip(0, 1), sp(4, "Bob/d") }) });
	delayed->delayEvaluationSeconds = 4;
	std::vector<ScriptItem> groupItems;
	groupItems.push_back(item(std::move(delayed)));
	g.lists[2].items.push_back(group("Waves", true, false, std::move(groupItems)));
	g.lists[2].items.push_back(group("Off", false, false, {}));
}
} // namespace

TEST_CASE("save4d script engine: counters, flags, timers, the scripts' active flags and evaluation frames, object lists, sequential scripts and the audio "
		  "timers load into a game of the same map scripts and run on equal; Script / ScriptGroup / SequentialScript records keep retail's layout")
{
	AudioHost hostA, hostB;
	ScriptGame a, b;
	buildScripts(a);
	buildScripts(b);
	a.start();
	b.start();
	a.engine().setHost(&hostA);
	b.engine().setHost(&hostB);
	a.frame();
	a.frame();
	CHECK_FALSE(a.engine().hasFinishedAudio("Horn", false)); // the timer starts: 4000 ms = 20 frames from now
	ScriptEngine::SequentialScript seq;
	seq.object = 41;
	seq.side = "Alice";
	seq.scriptName = "later";
	seq.loopCount = 2;
	a.engine().appendSequentialScript(seq);
	REQUIRE(a.counter("Alice/starts") == 3);
	REQUIRE_FALSE(a.engine().scriptActive("Alice/later"));
	const std::uint32_t scriptsA = section(*a.lw.logic, "script engine");
	const std::vector<std::uint8_t> bytes = blockBytes(*a.lw.logic, "obfme:ScriptEngine");
	REQUIRE(section(*b.lw.logic, "script engine") != scriptsA);
	b.lw.logic->gameState().loadBlocks({ "obfme:ScriptEngine" }, bytes);
	CHECK(section(*b.lw.logic, "script engine") == scriptsA);
	CHECK(b.counter("Alice/starts") == 3);
	CHECK(b.counter("Alice/t") == a.counter("Alice/t"));
	CHECK(*b.engine().findFlag("Alice/go"));
	CHECK_FALSE(b.engine().scriptActive("Alice/later"));
	CHECK_FALSE(b.engine().scriptActive("Alice/start")); // the one-shot ran
	CHECK(b.engine().findGroup("Bob/Waves")->scripts[0]->frameToEvaluateAt == a.engine().findGroup("Bob/Waves")->scripts[0]->frameToEvaluateAt);
	REQUIRE(b.engine().findObjectList("L"));
	CHECK(*b.engine().findObjectList("L") == std::vector<std::string>{ "Orc" });
	REQUIRE(b.engine().sequentialScripts().size() == 1u);
	CHECK(b.engine().sequentialScripts()[0][0].object == 41u);
	CHECK(b.engine().sequentialScripts()[0][0].script == b.engine().findScript("Alice/later")->def); // resolved in this game's tree
	REQUIRE(b.engine().audioTimers().size() == 1u);
	CHECK(b.engine().audioTimers()[0].endFrame == a.engine().audioTimers()[0].endFrame);
	CHECK(blockBytes(*b.lw.logic, "obfme:ScriptEngine") == bytes); // T2
	// T1 as far as the block allows: the frame state is the core's (loaded too), then both run on with equal sections
	b.lw.logic->gameState().loadBlocks({ "obfme:GameLogic", "obfme:Random" }, [&] {
		std::vector<std::uint8_t> core;
		a.lw.logic->gameState().saveBlocks({ "obfme:GameLogic", "obfme:Random" }, core);
		return core;
	}());
	for (int i = 0; i < 30; ++i)
	{
		a.frame();
		b.frame();
		CHECK(section(*b.lw.logic, "script engine") == section(*a.lw.logic, "script engine"));
	}
	CHECK(b.counter("Bob/d") == a.counter("Bob/d"));
	CHECK(b.counter("Bob/d") == 2); // the 4-second delay is 20 frames (RW 0x603878: 5 * delay + frame): it fired in the first frame and 20 frames later, of 32
	CHECK(b.engine().hasFinishedAudio("Horn", false));

	// T5: another map's scripts
	ScriptGame c;
	c.lists[1].items.push_back(item(script("only", { { cond("CONDITION_TRUE", {}) } }, {})));
	c.start();
	CHECK(loadError(*c.lw.logic, { "obfme:ScriptEngine" }, bytes) ==
		"block 'obfme:ScriptEngine': script engine: side 'Alice' has 2 scripts and 0 groups in the snapshot, 1 and 0 in this game");
	logictest::LogicWorld noScripts;
	CHECK(loadError(*noScripts.logic, { "obfme:ScriptEngine" }, bytes) ==
		"block 'obfme:ScriptEngine': script engine: the snapshot's game has map scripts, this game has none");
}

TEST_CASE("save4d: the version windows of the retail record layouts: Script (1, 2), SequentialScript (1, 1), the terrain resource manager (1, 3), the "
		  "Create-a-Hero record (1, 8)")
{
	CHECK(CreateAHeroHero::XFER_VERSION == 8);
	CreateAHeroHero hero;
	hero.name = u"A";
	XferSave s;
	hero.xfer(s);
	std::vector<std::uint8_t> bytes = s.data();
	bytes[0] = 9;
	std::string error;
	try
	{
		XferLoad l(bytes);
		CreateAHeroHero back;
		back.xfer(l);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "Don't know how to load version 9 (most recent is 8)");
	// a version 6 record has no unique id, flag or checksum (RW 0x80B4F5's branches): it loads from its shorter bytes
	bytes = s.data();
	bytes[0] = 6;
	bytes.resize(bytes.size() - 1 - 1 - 4); // the empty unique id, the system hero flag, the checksum
	XferLoad l6(bytes);
	CreateAHeroHero old;
	old.xfer(l6);
	CHECK(l6.atEnd());
	CHECK(old.name == u"A");

	logictest::LogicWorld world;
	TerrainResourceManager m(*world.logic);
	XferSave t;
	m.xfer(t);
	std::vector<std::uint8_t> trm = t.data();
	REQUIRE(trm[0] == 3);
	trm[0] = 4;
	error.clear();
	try
	{
		XferLoad l(trm);
		m.xfer(l);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "Don't know how to load version 4 (most recent is 3)");
}

TEST_CASE("save4d T4: the registry gives the seven SAVE-4d blocks their xfer; with SAVE-4b and 4c merged (SAVE-4e) the snapshot is written")
{
	logictest::LogicWorld f;
	std::vector<std::string> ours;
	for (const GameState::Block &b : f.logic->gameState().blocks())
	{
		if (b.owner == "SAVE-4d")
		{
			ours.push_back(b.token + (b.snapshot ? " xfer" : " declared") + (b.lists & (1u << SNAPSHOT_DEEPCRC_LOGICONLY) ? " +deepcrc" : ""));
		}
	}
	CHECK(ours == std::vector<std::string>{ "obfme:ScriptEngine xfer +deepcrc", "obfme:Victory xfer", "obfme:Weather xfer", "obfme:Economy xfer",
					  "obfme:CreateAHero xfer", "obfme:TerrainTrees xfer", "obfme:LargeGroupAudio xfer" });
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(f.logic->saveSnapshot(out, &error), error);
	CHECK(!out.empty());
}

TEST_CASE("save4d T8 as far as the blocks allow: the SAVE-4d blocks of a game played under Uber are the same bytes at 1, 4 and 16 logic threads")
{
	const std::vector<std::string> kOurs = { "obfme:ScriptEngine", "obfme:Victory", "obfme:Weather", "obfme:Economy", "obfme:CreateAHero",
		"obfme:TerrainTrees", "obfme:LargeGroupAudio" };
	const int before = JobSystem::logic().threadCount();
	std::vector<std::uint8_t> first;
	for (int threads : { 1, 4, 16 })
	{
		JobSystem::logic().setThreadCount(threads);
		AudioHost host;
		ScriptGame g;
		buildScripts(g);
		g.start();
		g.engine().setHost(&host);
		g.lw.logic->setSimulationProfile(SimulationProfile::Uber);
		g.lw.logic->weather().setWeather(2, 1, 9);
		for (int i = 0; i < 12; ++i)
		{
			g.frame();
		}
		g.engine().hasFinishedAudio("Horn", false);
		std::vector<std::uint8_t> stream;
		g.lw.logic->gameState().saveBlocks(kOurs, stream);
		if (first.empty())
		{
			first = stream;
		}
		CHECK(stream == first);
	}
	JobSystem::logic().setThreadCount(before);
}

namespace
{
void putU32(std::vector<std::uint8_t> &b, size_t at, std::uint32_t v)
{
	REQUIRE(at + 4 <= b.size()); // tolerance: a test helper's bounds check (the patched word lies inside the stream)
	for (int i = 0; i < 4; ++i)
	{
		b[at + (size_t)i] = (std::uint8_t)(v >> (8 * i));
	}
}

std::uint32_t getU32(const std::vector<std::uint8_t> &b, size_t at)
{
	return (std::uint32_t)b[at] | ((std::uint32_t)b[at + 1] << 8) | ((std::uint32_t)b[at + 2] << 16) | ((std::uint32_t)b[at + 3] << 24);
}

size_t find(const std::vector<std::uint8_t> &b, const std::vector<std::uint8_t> &pattern)
{
	auto it = std::search(b.begin(), b.end(), pattern.begin(), pattern.end());
	REQUIRE(it != b.end());
	REQUIRE(std::search(it + 1, b.end(), pattern.begin(), pattern.end()) == b.end()); // the pattern is unique in the stream
	return (size_t)(it - b.begin());
}
} // namespace

TEST_CASE("save4d hostile streams: an oversized count, a duplicate or dangling reference and an out-of-range enum are each refused by name, before the "
		  "block's state changes")
{
	SUBCASE("victory: a count beyond the block, a rule outside 0 .. 3, a player cached twice")
	{
		logictest::LogicWorld a, b;
		for (logictest::LogicWorld *w : { &a, &b })
		{
			w->logic->victory().initWithPlayers({ w->players.findPlayerWithName("Alice"), w->players.findPlayerWithName("Bob") });
		}
		a.logic->victory().setScriptFlag91(true);
		const std::vector<std::uint8_t> good = blockBytes(*a.logic, "obfme:Victory");
		const size_t p = payloadOf("obfme:Victory");
		const std::uint32_t before = section(*b.logic, "victory");
		std::vector<std::uint8_t> bad = good;
		putU32(bad, p + 120, 0x7FFFFFFFu); // the event count
		CHECK(loadError(*b.logic, { "obfme:Victory" }, bad) ==
			"block 'obfme:Victory': a list declares 2147483647 entries of at least 12 bytes, more than the 0 bytes left");
		bad = good;
		putU32(bad, p + 1, 9);
		CHECK(loadError(*b.logic, { "obfme:Victory" }, bad) == "block 'obfme:Victory': victory: a rule of 9");
		bad = good;
		putU32(bad, p + 25, getU32(good, p + 20)); // the second cached player: the first's index
		CHECK(loadError(*b.logic, { "obfme:Victory" }, bad) ==
			"block 'obfme:Victory': victory: player index " + std::to_string(getU32(good, p + 20)) + " is cached twice");
		CHECK(section(*b.logic, "victory") == before); // nothing taken
		CHECK(b.logic->victory().defeatCounter() == 0);
	}
	SUBCASE("weather and the audio gate: a weather, an affect kind and a gate state outside their sets")
	{
		ModifierStoreScope modifiers;
		logictest::LogicWorld a, b;
		a.logic->weather().setWeather(2, 0, 10);
		const std::vector<std::uint8_t> good = blockBytes(*a.logic, "obfme:Weather");
		const size_t p = payloadOf("obfme:Weather");
		const std::uint32_t before = section(*b.logic, "weather");
		std::vector<std::uint8_t> bad = good;
		putU32(bad, p + 1, 5);
		CHECK(loadError(*b.logic, { "obfme:Weather" }, bad) == "block 'obfme:Weather': weather: a weather of 5");
		bad = good;
		putU32(bad, p + 5, 7);
		CHECK(loadError(*b.logic, { "obfme:Weather" }, bad) == "block 'obfme:Weather': weather: an affect kind of 7");
		CHECK(section(*b.logic, "weather") == before);
		std::vector<std::uint8_t> gate = blockBytes(*a.logic, "obfme:LargeGroupAudio"); // disabled, -1
		const size_t g = payloadOf("obfme:LargeGroupAudio");
		putU32(gate, g + 2, 17);
		CHECK(loadError(*b.logic, { "obfme:LargeGroupAudio" }, gate) == "block 'obfme:LargeGroupAudio': large group audio: a gate disabled at frame 17");
		CHECK(b.logic->largeGroupAudio().gateFrame() == -1);
	}
	SUBCASE("economy: a claimant listed twice, a claimant whose object does not exist, a cell state outside 0 .. 3, a blocked cell moved, a cell count "
			"beyond the block")
	{
		EconWorld a;
		Object *f1 = a.farmAt("Alice", 10, 10, true);
		Object *f2 = a.farmAt("Bob", 12, 10, false);
		const std::vector<std::uint8_t> good = blockBytes(*a.logic, "obfme:Economy");
		const size_t trm = payloadOf("obfme:Economy") + 21;
		const size_t grid = trm + 5 + 2 * 9 + 24;
		REQUIRE(getU32(good, trm + 5) == f1->getID());
		REQUIRE(getU32(good, trm + 14) == f2->getID());
		EconWorld b;
		b.make("Farm", b.teamOf("Alice"));
		b.make("Farm", b.teamOf("Bob"));
		const std::uint32_t before = section(*b.logic, "economy");
		std::vector<std::uint8_t> bad = good;
		putU32(bad, trm + 14, f1->getID());
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': terrain resource manager: object " + std::to_string(f1->getID()) + " claims twice");
		bad = good;
		REQUIRE(good[grid + 20] == 1); // cell 0 lies in the water strip
		bad[grid + 20] = 5;
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) == "block 'obfme:Economy': terrain resource manager: cell 0 has the state 5");
		bad[grid + 20] = 0;
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': terrain resource manager: cell 0 is blocked in this game's map, not in the snapshot");
		bad = good;
		putU32(bad, grid + 16, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': a list declares 268435456 entries of at least 8 bytes, more than the " + std::to_string(good.size() - 7 - (grid + 20)) +
			" bytes left");
		CHECK(section(*b.logic, "economy") == before);
		CHECK(b.logic->economy().resources().claimants().empty());
		// cell 210 (10, 10), owned by Alice's complete farm: its entry names a player the game lacks, an object that claims nothing, an incomplete owner
		size_t cell = grid + 20;
		for (int i = 0; i < 210; ++i)
		{
			cell += 8 + 8 * (size_t)getU32(good, cell + 4); // a cell: its state, its entry count, its entries
		}
		REQUIRE(getU32(good, cell) == 3u);
		REQUIRE(getU32(good, cell + 4) == 1u);
		REQUIRE(getU32(good, cell + 12) == f1->getID());
		bad = good;
		putU32(bad, cell + 8, 19);
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) == "block 'obfme:Economy': terrain resource manager: cell 210 names player index 19, which this game lacks");
		bad = good;
		putU32(bad, cell + 12, 9999);
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) == "block 'obfme:Economy': terrain resource manager: cell 210 names the object 9999, which claims no ground");
		bad = good;
		putU32(bad, cell + 8, (std::uint32_t)b.players.findPlayerWithName("Bob")->getPlayerIndex());
		putU32(bad, cell + 12, f2->getID());
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': terrain resource manager: cell 210 is owned by the object " + std::to_string(f2->getID()) + ", whose claim is not complete");
		// an entry count of cell 0 beyond the block
		bad = good;
		putU32(bad, grid + 24, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': a list declares 268435456 entries of at least 8 bytes, more than the " + std::to_string(good.size() - 7 - (grid + 28)) +
			" bytes left");
		// a claimant count beyond the block
		bad = good;
		putU32(bad, trm + 1, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:Economy" }, bad) ==
			"block 'obfme:Economy': a list declares 268435456 entries of at least 9 bytes, more than the " + std::to_string(good.size() - 7 - (trm + 5)) +
			" bytes left");
		CHECK(section(*b.logic, "economy") == before);
		CHECK(b.logic->economy().resources().claimants().empty());
	}
	SUBCASE("economy: a dangling claimant is refused after every block loaded, the economy unchanged")
	{
		EconWorld a;
		a.farmAt("Alice", 10, 10, true);
		a.farmAt("Bob", 12, 10, false);
		a.logic->economy().setScoring(false);
		const std::vector<std::uint8_t> good = blockBytes(*a.logic, "obfme:Economy");
		EconWorld b;
		b.make("Farm", b.teamOf("Alice")); // Bob's farm (object 2) does not exist in this game
		const std::uint32_t before = section(*b.logic, "economy");
		CHECK(loadError(*b.logic, { "obfme:Economy" }, good) == "terrain resource manager: the claimant object 2 does not exist");
		CHECK(section(*b.logic, "economy") == before);
		CHECK(b.logic->economy().resources().claimants().empty());
		CHECK(b.logic->economy().context().scoring); // the staged switch was not taken either
	}
	SUBCASE("Create-a-Hero: a player listed twice, a player the game lacks, a record version outside 1 .. 8")
	{
		CreateAHeroHero hero;
		hero.name = u"Twin";
		logictest::LogicWorld a, b;
		Player *alice = a.players.findPlayerWithName("Alice");
		Player *bob = a.players.findPlayerWithName("Bob");
		a.logic->createAHeroes().assign(*alice, hero);
		a.logic->createAHeroes().assign(*bob, hero);
		const std::vector<std::uint8_t> good = blockBytes(*a.logic, "obfme:CreateAHero");
		const size_t first = payloadOf("obfme:CreateAHero") + 1 + 4;
		const size_t entry = (good.size() - 7 - 4 - first) / 2; // two equal records, then the choices' count
		REQUIRE(getU32(good, first) == (std::uint32_t)alice->getPlayerIndex());
		REQUIRE(getU32(good, first + entry) == (std::uint32_t)bob->getPlayerIndex());
		std::vector<std::uint8_t> bad = good;
		putU32(bad, first + entry, (std::uint32_t)alice->getPlayerIndex());
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': create-a-hero: player index " + std::to_string(alice->getPlayerIndex()) + " twice");
		bad = good;
		putU32(bad, first + entry, 19);
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': create-a-hero: a hero of player index 19, which this game lacks");
		bad = good;
		bad[first + entry - 6] = 0; // the first entry's version byte
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': create-a-hero: the hero of player index " + std::to_string(alice->getPlayerIndex()) + " has the record version 0");
		bad = good;
		putU32(bad, first + entry - 4, 0x400u); // the first entry's dirty flags
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': create-a-hero: the hero of player index " + std::to_string(alice->getPlayerIndex()) + " has the dirty flags 0x400");
		// the counts: the heroes (an entry is at least 183 bytes), a record's bling (5), the choices (8)
		bad = good;
		putU32(bad, first - 4, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': a list declares 268435456 entries of at least 183 bytes, more than the " + std::to_string(good.size() - 7 - first) +
			" bytes left");
		const size_t bling = first + 4 + 1 + 4 + (1 + 2 * 4) + 4 + 4 + 8 + 12 + 15 * 9; // the first record's BlingCount ("Twin", empty buttons)
		REQUIRE(getU32(good, bling) == 0u);
		bad = good;
		putU32(bad, bling, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': a list declares 268435456 entries of at least 5 bytes, more than the " + std::to_string(good.size() - 7 - (bling + 4)) +
			" bytes left");
		bad = good;
		putU32(bad, good.size() - 7 - 4, 0x10000000u);
		CHECK(loadError(*b.logic, { "obfme:CreateAHero" }, bad) ==
			"block 'obfme:CreateAHero': a list declares 268435456 entries of at least 8 bytes, more than the 0 bytes left");
		CHECK(b.logic->createAHeroes().heroes().empty());
	}
	SUBCASE("script engine: a counter count beyond the block, a sequential script of a team and a unit, a sequential script's team that does not exist")
	{
		AudioHost hostA, hostB;
		ScriptGame a, b;
		buildScripts(a);
		buildScripts(b);
		a.start();
		b.start();
		a.engine().setHost(&hostA);
		b.engine().setHost(&hostB);
		a.frame();
		const std::uint32_t before = section(*b.lw.logic, "script engine");
		const std::vector<std::uint8_t> good = blockBytes(*a.lw.logic, "obfme:ScriptEngine");
		// the counters' count precedes the first key, ("Alice", "starts")
		const size_t key = find(good, { 5, 'A', 'l', 'i', 'c', 'e', 6, 's', 't', 'a', 'r', 't', 's' });
		REQUIRE(getU32(good, key - 4) == (std::uint32_t)a.engine().allCounters().size());
		std::vector<std::uint8_t> bad = good;
		putU32(bad, key - 4, 0x10000000u);
		CHECK(loadError(*b.lw.logic, { "obfme:ScriptEngine" }, bad) ==
			"block 'obfme:ScriptEngine': a list declares 268435456 entries of at least 8 bytes, more than the " + std::to_string(good.size() - 7 - key) +
			" bytes left");
		CHECK(section(*b.lw.logic, "script engine") == before);
		ScriptEngine::SequentialScript both;
		both.team = 5;
		both.object = 41;
		both.side = "Alice";
		both.scriptName = "later";
		a.engine().appendSequentialScript(both);
		CHECK(loadError(*b.lw.logic, { "obfme:ScriptEngine" }, blockBytes(*a.lw.logic, "obfme:ScriptEngine")) ==
			"block 'obfme:ScriptEngine': script engine: a sequential script of team 5 and unit 41 (one of the two)");
		CHECK(section(*b.lw.logic, "script engine") == before);
		ScriptGame c;
		buildScripts(c);
		c.start();
		ScriptEngine::SequentialScript team;
		team.team = 999;
		team.side = "Alice";
		team.scriptName = "later";
		c.engine().appendSequentialScript(team);
		CHECK(loadError(*b.lw.logic, { "obfme:ScriptEngine" }, blockBytes(*c.lw.logic, "obfme:ScriptEngine")) ==
			"script engine: a sequential script of team 999, which this game lacks");
		CHECK(section(*b.lw.logic, "script engine") == before); // the staged state was not taken
		CHECK(b.engine().sequentialScripts().empty());
		CHECK(b.counter("Alice/starts") == -999);
	}
	SUBCASE("script engine: the same counts in another order are another tree, refused by the script's name")
	{
		ScriptGame a, swapped;
		buildScripts(a);
		buildScripts(swapped, true);
		a.start();
		swapped.start();
		a.frame();
		const std::uint32_t before = section(*swapped.lw.logic, "script engine");
		CHECK(loadError(*swapped.lw.logic, { "obfme:ScriptEngine" }, blockBytes(*a.lw.logic, "obfme:ScriptEngine")) ==
			"block 'obfme:ScriptEngine': script engine: side 'Alice': script 0 is 'start' in the snapshot, 'later' in this game");
		CHECK(section(*swapped.lw.logic, "script engine") == before);
	}
}

namespace
{
// TheScienceStore with two sciences (as test_audio4.cpp builds it)
struct Sciences
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	SpellStores stores;
	Sciences()
		: stores(keys)
	{
		keys.init();
		stores.install();
		SpellStores::registerBlocks(fx.env.blocks);
		const std::string err = initest::loadError(fx.env, "science.ini",
			"Science SCIENCE_A\nEnd\nScience SCIENCE_B\nEnd\nRank 1\n  SkillPointsNeededDefault = 0\nEnd\nRank 2\n  SkillPointsNeededDefault = 100\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Sciences() { stores.uninstall(); }
	ScienceType sci(const char *name) const { return stores.sciences().getScienceFromInternalName(name); }
};

// a script game whose engine holds every container: a prototype with a generic script hook (3: Alice/later), a named object, a sequential script, an
// audio timer, an object list, flags and counters, and Alice's sciences A and B
struct FullScriptGame
{
	ScriptGame g;
	AudioHost host;
	TeamPrototype *proto = nullptr;
	Object *dummy = nullptr;
	FullScriptGame(const Sciences &s)
	{
		buildScripts(g);
		const std::string err = g.lw.w.load("Object Dummy\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
		Dict hooks;
		hooks.setAsciiString("teamGenericScriptHook3", "later");
		proto = g.lw.teams.initTeam("Raiders", g.lw.players.findPlayerWithName("Alice"), false, &hooks);
		g.start();
		g.engine().setHost(&host);
		dummy = g.lw.make("Dummy");
		Player *alice = g.lw.players.findPlayerWithName("Alice");
		REQUIRE(alice->science().addScience(s.sci("SCIENCE_A")));
		REQUIRE(alice->science().addScience(s.sci("SCIENCE_B")));
	}
	// what only the saved game does: frames, the generic copies, the named object, the queue read, the timer, the sequential script
	void play(const Sciences &s)
	{
		g.frame();
		Team *t = g.lw.teams.createTeam(proto, true);
		t->scriptState().ready = true;
		g.engine().updateGenericScripts(*t);
		dummy->setName("HeroN1");                  // the named cache (Object::setName)
		g.engine().setReference("RefN2", *dummy); // lane CASTLE-UNPACK: an object reference (RW 0x608397), the side "" outside the update
		CHECK(g.engine().didPlayerAcquireScience(*g.lw.players.findPlayerWithName("Alice"), s.sci("SCIENCE_B"), true)); // B consumed, A pending
		CHECK_FALSE(g.engine().hasFinishedAudio("Horn", false));
		ScriptEngine::SequentialScript seq;
		seq.object = dummy->getID();
		seq.side = "Alice";
		seq.scriptName = "later";
		g.engine().appendSequentialScript(seq);
	}
};
} // namespace

TEST_CASE("save4d script engine: a non-default acquired-science queue and generic script copies round-trip; every container's count beyond its block, "
		  "a science the data lacks and a prototype's hook count other than 32 are refused by name, the engine unchanged")
{
	Sciences s;
	FullScriptGame a(s), b(s);
	a.play(s);
	const std::uint32_t scriptsA = section(*a.g.lw.logic, "script engine");
	const std::vector<std::uint8_t> good = blockBytes(*a.g.lw.logic, "obfme:ScriptEngine");
	const std::uint32_t before = section(*b.g.lw.logic, "script engine");
	REQUIRE(before != scriptsA);

	// the hostile copies first (b unchanged after each)
	auto refusedBeyond = [&](size_t countAt, size_t minBytes) {
		std::vector<std::uint8_t> bad = good;
		putU32(bad, countAt, 0x10000000u);
		CHECK(loadError(*b.g.lw.logic, { "obfme:ScriptEngine" }, bad) ==
			"block 'obfme:ScriptEngine': a list declares 268435456 entries of at least " + std::to_string(minBytes) + " bytes, more than the " +
			std::to_string(good.size() - 7 - (countAt + 4)) + " bytes left");
	};
	const std::uint32_t protoId = (std::uint32_t)a.proto->getID();
	const size_t generic = find(good, { (std::uint8_t)protoId, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 1 }); // the id, 32 hooks, hooks 0 .. 2 empty, hook 3 set
	// the map is in prototype id order: the players' own prototypes (older, smaller ids) have copies with no script (an id, 32, 32 empty hooks: 40 bytes)
	size_t first = generic;
	while (getU32(good, first - 36) == 32u && std::all_of(good.begin() + (std::ptrdiff_t)(first - 32), good.begin() + (std::ptrdiff_t)first,
												 [](std::uint8_t c) { return c == 0; }))
	{
		first -= 40;
	}
	REQUIRE(getU32(good, first - 4) == (std::uint32_t)((generic - first) / 40 + 1)); // ours is the newest prototype: the last entry
	refusedBeyond(first - 4, 40); // the prototypes (an id, a count, 32 empty hooks)
	refusedBeyond(generic + 4, 1);  // a prototype's hooks
	std::vector<std::uint8_t> bad = good;
	putU32(bad, generic + 4, 31);
	CHECK(loadError(*b.g.lw.logic, { "obfme:ScriptEngine" }, bad) ==
		"block 'obfme:ScriptEngine': script engine: prototype " + std::to_string(protoId) + " has 31 generic scripts (32 hooks)");
	const size_t flag = find(good, { 5, 'A', 'l', 'i', 'c', 'e', 2, 'g', 'o' });
	refusedBeyond(flag - 4, 3); // the flags
	const size_t list = find(good, { 1, 'L', 1, 0, 0, 0, 3, 'O', 'r', 'c' });
	refusedBeyond(list - 4, 5); // the object lists
	refusedBeyond(list + 2, 1); // a list's types
	const size_t named = find(good, { 6, 'H', 'e', 'r', 'o', 'N', '1' });
	refusedBeyond(named - 4, 5); // the named objects
	const std::uint8_t id = (std::uint8_t)a.dummy->getID();
	const size_t seq = find(good, { 1, 0, 0, 0, 0, id, 0, 0, 0, 5, 'A', 'l', 'i', 'c', 'e', 5, 'l', 'a', 't', 'e', 'r' });
	refusedBeyond(seq - 8, 4);  // the chains
	refusedBeyond(seq - 4, 24); // a chain's records
	const Player *alice = a.g.lw.players.findPlayerWithName("Alice");
	const std::uint32_t generation = alice->science().scriptNoticeGeneration();
	const std::uint32_t sciA = (std::uint32_t)s.sci("SCIENCE_A");
	const size_t queue = find(good, { 1, (std::uint8_t)generation, (std::uint8_t)(generation >> 8), (std::uint8_t)(generation >> 16), (std::uint8_t)(generation >> 24),
		2, 0, 0, 0, 1, 0, 0, 0, (std::uint8_t)sciA, (std::uint8_t)(sciA >> 8), (std::uint8_t)(sciA >> 16), (std::uint8_t)(sciA >> 24) });
	refusedBeyond(queue + 9, 4); // Alice's queue's pending sciences
	bad = good;
	putU32(bad, queue + 13, 12345);
	CHECK(loadError(*b.g.lw.logic, { "obfme:ScriptEngine" }, bad) ==
		"block 'obfme:ScriptEngine': an acquired-science queue holds the science 12345, which this game's data lacks");
	const size_t horn = find(good, { 4, 'H', 'o', 'r', 'n' });
	refusedBeyond(horn - 4, 5); // the audio timers
	const size_t reference = find(good, { 0, 5, 'R', 'e', 'f', 'N', '2' }); // lane CASTLE-UNPACK: the side "", the name
	refusedBeyond(reference - 4, 6);                                       // the object references
	CHECK(section(*b.g.lw.logic, "script engine") == before);

	// the round trip
	b.g.lw.logic->gameState().loadBlocks({ "obfme:ScriptEngine" }, good);
	CHECK(section(*b.g.lw.logic, "script engine") == scriptsA);
	CHECK(blockBytes(*b.g.lw.logic, "obfme:ScriptEngine") == good); // T2
	// the queue: A is pending at the replay position 2 (B was consumed); B is not
	Player *aliceB = b.g.lw.players.findPlayerWithName("Alice");
	CHECK_FALSE(b.g.engine().didPlayerAcquireScience(*aliceB, s.sci("SCIENCE_B"), false));
	CHECK(b.g.engine().didPlayerAcquireScience(*aliceB, s.sci("SCIENCE_A"), false));
	// the prototype's copies (private to the engine) are in the section compared above: per hook its script and its own active flag
	CHECK(b.g.engine().audioTimers().size() == 1u);
	CHECK(b.g.engine().getUnitNamed("HeroN1") == b.dummy);
	CHECK(b.g.engine().getUnitNamed("RefN2") == b.dummy); // lane CASTLE-UNPACK
	CHECK(b.g.engine().references().size() == 1u);
	CHECK(b.g.engine().sequentialScripts().size() == 1u);
}
