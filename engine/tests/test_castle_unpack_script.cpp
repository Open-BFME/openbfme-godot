// OpenBFME unit tests (lane CASTLE-UNPACK): the map script commands that hand a castle base to a player, on a synthetic world (no retail files). The rules are
// RotWK's (caveat S-001, static disassembly): NAMED_BASE_UNPACKABLE_FOR_PLAYER RW 0x7E5142, PLAYER_HAS_N_OR_FEWER_BASES RW 0x7E64CD, NAMED_BASE_UNPACK /
// NAMED_BASE_UNPACK_FREE RW 0x7BED64 (see ScriptConditions.cpp and ScriptActionsUnits.cpp). Edain's castle maps run them from their script libraries.  GPL-3.0.

#include "doctest.h"

#include "ProdTestUtil.h"
#include "ScriptTestUtil.h"

#include "BigTestUtil.h"
#include "MapTestUtil.h"
#include "Common/INI.h"
#include "GameClient/MapScriptSetup.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Map/SidesList.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"

#include <vector>

using namespace prodtest;
using namespace scripttest;

namespace
{
const char kObjects[] =
	"Object UKeep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CASTLE_KEEP\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object UExplicitBase\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  BuildCost = 200\n"
	"End\n"
	"Object UFlag\n"
	"  KindOf = IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  CommandSet = UFlagSet\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Alpha Base_U 300\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE\n"
	"  End\n"
	"End\n"
	"Object UVital\n"
	"  KindOf = STRUCTURE IMMOBILE VITAL_FOR_BASE_SURVIVAL\n"
	"End\n"
	"Object UTarget\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 20\n"
	"  End\n"
	"End\n";
const char kCommands[] = "CommandButton Command_UnpackExplicit\n  Command = CASTLE_UNPACK_EXPLICIT_OBJECT\n  Object = UExplicitBase\nEnd\n"
						 "CommandSet UFlagSet\n  1 = Command_UnpackExplicit\nEnd\n";

struct UnpackFx : ProdWorld
{
	std::vector<ScriptList> lists = std::vector<ScriptList>(3);
	Object *flag = nullptr;
	UnpackFx()
	{
		load(kObjects);
		load(kCommands);
		logic->settings().buildRulesLoaded = true;
		CastleTemplate layout;
		layout.name = "Base_U";
		layout.version = 5;
		layout.entries.push_back({ "", "UKeep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		logic->castleTemplates().add(layout);
		flag = make("UFlag", teamOf("Alice"));
		REQUIRE(flag != nullptr);
		REQUIRE(alice()->getSide() == "Alpha");
		REQUIRE(alice()->getMoney()->countMoney() == 1500u);
	}
	void start()
	{
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		engine().newGame(sides, nullptr, nullptr, false);
		flag->setName("Center1"); // after newGame, which clears the named cache (map objects are named after the script engine's newGame)
	}
	ScriptEngine &engine() { return logic->scriptEngine(); }
	CastleBehavior *cb() { return dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior")); }
	bool unpackable(const char *unit, const char *player) { return ScriptConditions::evaluate(engine(), cond("NAMED_BASE_UNPACKABLE_FOR_PLAYER", { sp(14, unit), sp(11, player) })); }
	bool fewerBases(const char *player, int n) { return ScriptConditions::evaluate(engine(), cond("PLAYER_HAS_N_OR_FEWER_BASES", { sp(11, player), ip(0, n) })); }
	void setMoney(Player *p, std::uint32_t amount)
	{
		p->getMoney()->withdraw(p->getMoney()->countMoney(), false);
		p->getMoney()->deposit(amount, false);
	}
};
} // namespace

TEST_CASE("script castle unpack: NAMED_BASE_UNPACKABLE_FOR_PLAYER (RW 0x7E5142): the named castle of the mask's first player that may unpack and afford the side's price, "
		  "else a CASTLE_UNPACK_EXPLICIT_OBJECT button of its command set whose template it affords")
{
	UnpackFx f;
	f.start();
	CHECK(f.unpackable("Center1", "Alice"));          // 1500 >= the side's 300
	CHECK_FALSE(f.unpackable("Center1", "Bob"));      // RW 0x7E5197: the unit's controller is not the player
	CHECK_FALSE(f.unpackable("NoSuchUnit", "Alice")); // RW 0x7E5159
	CHECK_FALSE(f.unpackable("Center1", "Nobody"));   // RW 0x7E516B: an empty mask
	// below the side's price, at the explicit template's BuildCost: the command set path (RW 0x7E51DB .. 0x7E5245)
	f.setMoney(f.alice(), 250);
	CHECK_FALSE(f.cb()->canAffordUnpack(f.alice()));
	CHECK(f.unpackable("Center1", "Alice"));
	f.setMoney(f.alice(), 199);
	CHECK_FALSE(f.unpackable("Center1", "Alice"));
	f.setMoney(f.alice(), 200);
	CHECK(f.unpackable("Center1", "Alice"));
	// a castle that is unpacking no longer can (canUnpack(true): state 0)
	f.setMoney(f.alice(), 1500);
	f.cb()->initiateUnpack(false, nullptr);
	CHECK(f.cb()->state() == CastleBehavior::STATE_PACKED);
	CHECK_FALSE(f.unpackable("Center1", "Alice"));
	CHECK(f.engine().stats().unportedConditions.count("NAMED_BASE_UNPACKABLE_FOR_PLAYER") == 0);
}

TEST_CASE("script castle unpack: PLAYER_HAS_N_OR_FEWER_BASES (RW 0x7E64CD): false before ftol(5 * SecondsBeforeBaseCheckActive), then the mask's VITAL_FOR_BASE_SURVIVAL "
		  "team members (RW 0x6ABB6A) are at most n")
{
	UnpackFx f;
	f.start();
	REQUIRE(f.make("UVital", f.teamOf("Alice")) != nullptr);
	REQUIRE(f.make("UVital", f.teamOf("Alice")) != nullptr);
	REQUIRE(f.make("UVital", f.teamOf("Bob")) != nullptr);
	REQUIRE(f.logic->settings().secondsBeforeBaseCheckActive == 5.0f);
	f.frames(24);
	REQUIRE(f.logic->getFrame() == 24u);
	CHECK_FALSE(f.fewerBases("Alice", 100)); // RW 0x7E650E: frame 24 < 25
	f.frames(1);
	REQUIRE(f.logic->getFrame() == 25u);
	CHECK_FALSE(f.fewerBases("Alice", 1)); // the flag (CASTLE_CENTER, no VITAL_FOR_BASE_SURVIVAL) does not count: 2 > 1
	CHECK(f.fewerBases("Alice", 2));
	CHECK_FALSE(f.fewerBases("Bob", 0));
	CHECK(f.fewerBases("Bob", 1));
	CHECK(f.fewerBases("Nobody", 0)); // an empty mask counts nothing: 0 >= 0
	CHECK_FALSE(f.fewerBases("<All Players>", 2)); // the mask's players summed: 3
	CHECK(f.fewerBases("<All Players>", 3));
	CHECK(f.engine().stats().unportedConditions.count("PLAYER_HAS_N_OR_FEWER_BASES") == 0);
}

TEST_CASE("script castle unpack: NAMED_BASE_UNPACK (RW 0x7BED64) unpacks a castle of the current player for the side's price and names it; the FREE form unpacks it at once")
{
	SUBCASE("the paid unpack, run from the owner's script")
	{
		UnpackFx f;
		f.lists[1].items.push_back(item(script("Unpack", { { cond("CONDITION_TRUE", {}) } }, { act("NAMED_BASE_UNPACK", { sp(14, "Center1"), sp(54, "MyCastle") }) }, {}, true)));
		f.start();
		f.frames(1);
		CHECK(f.alice()->getMoney()->countMoney() == 1500u - 300u); // RW 0x79C17D(0, null) -> the side's price (RW 0x799039)
		CHECK(f.cb()->storedPrice() == 300.0f);
		CHECK(f.cb()->state() != CastleBehavior::STATE_IDLE);
		// RW 0x608397: the reference is keyed by (the current side, the name): Alice's script made it (a lookup outside her scripts does not see it)
		const auto ref = f.engine().references().find({ "Alice", "MyCastle" });
		REQUIRE(ref != f.engine().references().end());
		CHECK(ref->second == f.flag->getID());
		CHECK(f.engine().stats().unportedActions.count("NAMED_BASE_UNPACK") == 0);
	}
	SUBCASE("another side's script does nothing (RW 0x7BED95: the controller is not the current player)")
	{
		UnpackFx f;
		f.lists[2].items.push_back(item(script("Unpack", { { cond("CONDITION_TRUE", {}) } }, { act("NAMED_BASE_UNPACK", { sp(14, "Center1"), sp(54, "MyCastle") }) }, {}, true)));
		f.start();
		f.frames(1);
		CHECK(f.alice()->getMoney()->countMoney() == 1500u);
		CHECK(f.cb()->state() == CastleBehavior::STATE_IDLE);
		CHECK(f.engine().references().empty());
	}
	SUBCASE("without the side's price nothing happens and nothing is named (RW 0x7BEDE5)")
	{
		UnpackFx f;
		f.setMoney(f.alice(), 299);
		f.lists[1].items.push_back(item(script("Unpack", { { cond("CONDITION_TRUE", {}) } }, { act("NAMED_BASE_UNPACK", { sp(14, "Center1"), sp(54, "MyCastle") }) }, {}, true)));
		f.start();
		f.frames(1);
		CHECK(f.alice()->getMoney()->countMoney() == 299u);
		CHECK(f.cb()->state() == CastleBehavior::STATE_IDLE);
		CHECK(f.engine().references().empty());
	}
	SUBCASE("NAMED_BASE_UNPACK_FREE (RW 0x7CE928, flag 1): no price check, the instant unpack, nothing paid; an empty reference names nothing")
	{
		UnpackFx f;
		f.setMoney(f.alice(), 0);
		f.lists[1].items.push_back(item(script("Unpack", { { cond("CONDITION_TRUE", {}) } }, { act("NAMED_BASE_UNPACK_FREE", { sp(14, "Center1"), sp(54, "") }) }, {}, true)));
		f.start();
		f.frames(1);
		CHECK(f.alice()->getMoney()->countMoney() == 0u);
		CHECK(f.cb()->state() == CastleBehavior::STATE_UNPACKED);
		CHECK(f.cb()->storedPrice() == 0.0f);
		REQUIRE(f.cb()->keepId() != INVALID_ID);
		const Object *keep = f.logic->findObjectByID(f.cb()->keepId());
		REQUIRE(keep != nullptr);
		CHECK(keep->getControllingPlayer() == f.alice());
		CHECK(f.engine().stats().unportedActions.count("NAMED_BASE_UNPACK_FREE") == 0);
	}
}

namespace
{
// the creation host of a script (LiveGame's in a game): newObject on the team, the orientation, the position on the ground
struct CreateHost : ScriptEngineHost
{
	GameLogic &logic;
	explicit CreateHost(GameLogic &l) : logic(l) {}
	Object *createObject(const ThingTemplate &tt, Team &team, const Coord3D &pos, float angle) override
	{
		Object *o = logic.newObject(&tt, &team, ObjectStatusMaskType{});
		if (o)
		{
			Coord3D p = pos;
			p.z = logic.getGroundHeight(p.x, p.y);
			o->setOrientation(angle);
			o->setPosition(&p);
		}
		return o;
	}
};
ScriptParameter coord(float x, float y)
{
	ScriptParameter p;
	p.type = 16;
	p.x = x;
	p.y = y;
	return p;
}
ScriptParameter angle(float a)
{
	ScriptParameter p;
	p.type = 17;
	p.realValue = a;
	return p;
}
int countOf(GameLogic &logic, const char *tmpl)
{
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		n += o->getTemplate()->getName() == tmpl && !o->isDestroyed();
	}
	return n;
}
} // namespace

TEST_CASE("script create object: CREATE_OBJECT / UNIT_SPAWN_NAMED_LOCATION_ORIENTATION (RW 0x7CBCE0 / 0x7CE45B -> RW 0x7C595A) make the object on the team at the "
		  "position with the angle; a living object of the name refuses; an unknown template makes nothing")
{
	UnpackFx f;
	CreateHost host(*f.logic);
	f.engine().setHost(&host);
	const std::string team = f.teamOf("Alice")->getName();
	f.lists[1].items.push_back(item(script("Create", { { cond("CONDITION_TRUE", {}) } },
		{ act("CREATE_OBJECT", { sp(15, "UVital"), sp(3, team), coord(300.0f, 320.0f), angle(0.5f) }),
		  act("UNIT_SPAWN_NAMED_LOCATION_ORIENTATION", { sp(14, "Spawned"), sp(15, "UVital"), sp(3, team), coord(360.0f, 300.0f), angle(1.0f) }),
		  act("UNIT_SPAWN_NAMED_LOCATION_ORIENTATION", { sp(14, "Spawned"), sp(15, "UVital"), sp(3, team), coord(400.0f, 300.0f), angle(0.0f) }),
		  act("CREATE_OBJECT", { sp(15, "NoSuchTemplate"), sp(3, team), coord(300.0f, 300.0f), angle(0.0f) }) },
		{}, true)));
	f.start();
	f.frames(1);
	CHECK(countOf(*f.logic, "UVital") == 2); // the second spawn of "Spawned" refuses (the warning RW 0xC362FC); the unknown template makes nothing
	Object *spawned = f.engine().getUnitNamed("Spawned");
	REQUIRE(spawned);
	CHECK(spawned->getPosition()->x == 360.0f);
	CHECK(spawned->getPosition()->y == 300.0f);
	CHECK(spawned->getOrientation() == 1.0f);
	CHECK(spawned->getControllingPlayer() == f.alice());
	bool unnamed = false;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == "UVital" && o != spawned)
		{
			unnamed = o->getPosition()->x == 300.0f && o->getPosition()->y == 320.0f && o->getOrientation() == 0.5f && o->getName().empty();
		}
	}
	CHECK(unnamed);
	CHECK(f.engine().stats().unportedActions.count("CREATE_OBJECT") == 0);
	CHECK(f.engine().stats().unportedActions.count("UNIT_SPAWN_NAMED_LOCATION_ORIENTATION") == 0);
	f.engine().setHost(nullptr);
}

TEST_CASE("script named damage: NAMED_DAMAGE (RW 0x7CC387 -> RW 0x7C050A) hits the named unit for the amount, unresistable, no source; an unknown name does nothing")
{
	UnpackFx f;
	f.logic->settings().unitDamagedThreshold = 0.65f; // ActiveBody needs GameData's thresholds
	f.logic->settings().unitReallyDamagedThreshold = 0.4f;
	f.logic->settings().bodyThresholdsLoaded = true;
	CreateHost host(*f.logic);
	f.engine().setHost(&host);
	const std::string team = f.teamOf("Alice")->getName();
	f.lists[1].items.push_back(item(script("Spawn", { { cond("CONDITION_TRUE", {}) } },
		{ act("UNIT_SPAWN_NAMED_LOCATION_ORIENTATION", { sp(14, "Victim"), sp(15, "UTarget"), sp(3, team), coord(360.0f, 300.0f), angle(0.0f) }) }, {}, true)));
	f.lists[1].items.push_back(item(script("Hit", { { cond("CONDITION_TRUE", {}) } },
		{ act("NAMED_DAMAGE", { sp(14, "Victim"), ip(0, 7) }), act("NAMED_DAMAGE", { sp(14, "Nobody"), ip(0, 7) }) }, {}, true)));
	f.start();
	Object *victim = nullptr;
	f.frames(1);
	victim = f.engine().getUnitNamed("Victim");
	REQUIRE(victim);
	REQUIRE(victim->getBodyModule());
	f.frames(2);
	CHECK(victim->getBodyModule()->getHealth() == victim->getBodyModule()->getMaxHealth() - 7.0f);
	CHECK(f.engine().stats().unportedActions.count("NAMED_DAMAGE") == 0);
	f.engine().setHost(nullptr);
}

TEST_CASE("script library teams: a slot side gets its libraries' teams, owned by it; default and ownerless teams are not (RW 0x7313B3, 0x6B07EF)")
{
	using namespace maptest;
	// a library like Edain's spieler.map: sides "" and "Player" (the second holds the scripts), teams "teamPlayer" (the owner's default team),
	// "Base" owned by Player and again by PlyrCivilian, "Spielmodi" owned by Player, an ownerless "Player_1_Inherit"
	Toc t;
	D side0(t);
	side0.s("playerName", "").b("playerIsHuman", false);
	D side1(t);
	side1.s("playerName", "Player").b("playerIsHuman", false);
	W sl;
	sl.u8(1).i32(2).raw(side0.bytes()).i32(0).raw(side1.bytes()).i32(0);
	Bytes sides = chunk(t, "SidesList", 6, sl.b);
	auto team = [&](const std::string &name, const std::string &owner) {
		D d(t);
		d.s("teamName", name).s("teamOwner", owner).b("teamIsSingleton", false);
		return d.bytes();
	};
	W tw;
	tw.i32(5).raw(team("teamPlayer", "Player")).raw(team("Base", "Player")).raw(team("Player_1_Inherit", "")).raw(team("Base", "PlyrCivilian"))
		.raw(team("Spielmodi", "Player"));
	Bytes teams = chunk(t, "Teams", 1, tw.b);
	Bytes psl = chunk(t, "PlayerScriptsList", 6, cat(chunk(t, "ScriptList", 1, {}), chunk(t, "ScriptList", 1, {})));
	const Bytes lib = file(t, cat(cat(sides, teams), psl));

	Win32BIGFileSystem fsys;
	std::string error;
	auto archive = fsys.openArchiveFromMemory("maps.big",
		std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", { { "libraries\\spieler\\spieler.map", std::string(lib.begin(), lib.end()) } })),
		&error);
	REQUIRE_MESSAGE(archive, error);
	fsys.mountArchive(std::move(archive), "maps.big", false);

	NameKeyGenerator keys;
	keys.init();
	SkirmishAIStore store(keys);
	INIEnvironment env;
	store.registerBlocks(env.blocks);
	{
		INI ini(env);
		const std::string text = "PlayerAIType Multiplayer_Human\n  LibraryMap = \"Libraries\\Spieler\\Spieler.map\"\nEnd\n";
		ini.loadMemory("playerai.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
	}
	REQUIRE(store.findPlayerAIType("Multiplayer_Human"));

	// the game's sides: a kept map side running the library too, then two slot sides
	SidesList game;
	for (const char *n : { "PlyrCivilian", "Player_1", "Player_2" })
	{
		SidesInfo s;
		s.dict.setAsciiString("playerName", n);
		game.sides.push_back(s);
	}
	LoadedMap map;
	MapScriptSetup setup;
	REQUIRE_MESSAGE(setup.build(fsys, store, game, map, { "Player_1", "Player_2" }, { "Multiplayer_Human", "Multiplayer_Human", "Multiplayer_Human" }, &error),
		error);
	std::vector<std::pair<std::string, std::string>> got;
	for (const Dict &d : setup.libraryTeams())
	{
		got.emplace_back(d.getAsciiString("teamOwner"), d.getAsciiString("teamName"));
	}
	// per slot side in side order, the library's teams in its order: both "Base" teams (RW 0x72F3FF appends: no name test), "Spielmodi"
	const std::vector<std::pair<std::string, std::string>> want = { { "Player_1", "Base" }, { "Player_1", "Base" }, { "Player_1", "Spielmodi" },
		{ "Player_2", "Base" }, { "Player_2", "Base" }, { "Player_2", "Spielmodi" } };
	CHECK(got == want);
	// the kept side's library teams are not added (S-1181), and that is said
	bool keptNote = false;
	for (const std::string &n : setup.notes())
	{
		keptNote = keptNote || (n.find("[S-1181]") != std::string::npos && n.find("'PlyrCivilian'") != std::string::npos);
	}
	CHECK(keptNote);
	// every side still runs the library's scripts
	REQUIRE(setup.sides().size() == 3);
	CHECK(setup.sides()[2].libraries.size() == 1);
}
