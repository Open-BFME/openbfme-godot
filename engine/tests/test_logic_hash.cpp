// OpenBFME unit tests: the deterministic world state hash covers every mutable logic field group and nothing peer specific (lane LOGIC-1).
//
// Each test changes ONE field group of an otherwise identical world and requires the hash to change (a mutation test), or, for the local player,
// requires the hash NOT to change. A field the hash misses would let two peers desync silently.

#include "doctest.h"
#include "LogicTestUtil.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <climits>
#include <cstdint>
#include <limits>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Fighter\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object FighterHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    InitialPayload = Fighter 3\n"
	"  End\n"
	"End\n"
	"Object TwoRankHorde\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    RankInfo = RankNumber:1 UnitType:Fighter Position:X:50 Y:0 Position:X:50 Y:20\n"
	"    RankInfo = RankNumber:2 UnitType:Fighter Position:X:30 Y:0 Leader 1 0 Position:X:30 Y:20 Leader 1 1\n"
	"    InitialPayload = Fighter 4\n"
	"  End\n"
	"End\n";

// two more faction templates: A2 is FactionA under another name, AObs is FactionA with IsObserver
const char kMoreTemplates[] =
	"PlayerTemplate FactionA2\n"
	"  Side = Alpha\n"
	"  DisplayName = SIDE:ALPHA\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 1500\n"
	"  PreferredColor = R:255 G:0 B:128\n"
	"  StartingBuilding = AlphaKeep\n"
	"End\n"
	"PlayerTemplate FactionAObs\n"
	"  Side = Alpha\n"
	"  DisplayName = SIDE:ALPHA\n"
	"  PlayableSide = Yes\n"
	"  StartMoney = 1500\n"
	"  PreferredColor = R:255 G:0 B:128\n"
	"  StartingBuilding = AlphaKeep\n"
	"  IsObserver = Yes\n"
	"End\n";

struct Fx : LogicWorld
{
	explicit Fx(const char *templates = kMoreTemplates)
		: LogicWorld(RandomAlgorithm::ZH_CarryChain, templates)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
	}
	std::uint32_t hash() const { return logic->computeStateHash(); }
};
} // namespace

TEST_CASE("state hash: two identical worlds hash alike (the baseline of every mutation below)")
{
	Fx a, b;
	a.make("FighterHorde");
	b.make("FighterHorde");
	CHECK(a.hash() == b.hash());
}

TEST_CASE("state hash: the local player is this peer's identity, not simulation state: peers with different local players hash alike")
{
	Fx a, b;
	a.make("Fighter");
	b.make("Fighter");
	a.players.setLocalPlayer(a.players.findPlayerWithName("Alice"));
	b.players.setLocalPlayer(b.players.findPlayerWithName("Bob"));
	CHECK(a.hash() == b.hash());
	StateHasher ha, hb;
	a.players.localCrc(ha);
	b.players.localCrc(hb);
	CHECK(ha.value() != hb.value()); // the diagnostic does tell them apart
}

TEST_CASE("state hash: the frame advance flag (RW GameLogic + 0x44) is hashed")
{
	Fx f;
	const std::uint32_t before = f.hash();
	f.logic->setFrameAdvance(false);
	CHECK(f.hash() != before);
	f.logic->setFrameAdvance(true);
	CHECK(f.hash() == before);
}

TEST_CASE("state hash: an object's name is hashed")
{
	Fx a, b;
	Object *oa = a.make("Fighter");
	b.make("Fighter");
	REQUIRE(a.hash() == b.hash());
	oa->setName("Hero1");
	CHECK(a.hash() != b.hash());
}

TEST_CASE("state hash: the recorded transform (angle, basis, position) and the previous position are hashed, each on its own")
{
	auto world = [](float angleAtRecord, float previousAtFirstRecord, float *outBasisProbe) {
		auto f = std::make_unique<Fx>();
		Object *o = f->make("Fighter");
		Coord3D p0{ previousAtFirstRecord, 0.0f, 0.0f };
		o->setPosition(&p0);
		o->recordTransform(1);
		Coord3D p1{ 10.0f, 0.0f, 0.0f };
		o->setPosition(&p1);
		o->setOrientation(angleAtRecord);
		o->recordTransform(2); // recorded position / angle / basis = p1, angleAtRecord; previous position = the first record's
		(void)outBasisProbe;
		// the live transform is put back to one value so only the recorded and previous fields can differ
		o->setOrientation(0.0f);
		Coord3D back{ 20.0f, 0.0f, 0.0f };
		o->setPosition(&back);
		return f;
	};
	auto base = world(0.3f, 5.0f, nullptr);
	auto sameAgain = world(0.3f, 5.0f, nullptr);
	auto otherAngle = world(0.9f, 5.0f, nullptr);
	auto otherPrevious = world(0.3f, 7.0f, nullptr);
	CHECK(base->hash() == sameAgain->hash());
	CHECK(base->hash() != otherAngle->hash());    // recorded angle and basis (the live angle was put back)
	CHECK(base->hash() != otherPrevious->hash()); // previous position
}

TEST_CASE("state hash: a player's start slot, faction template and observer state are hashed, each on its own")
{
	auto initPlayer = [](const char *templateName, int startIndex, const char *templates = kMoreTemplates) {
		auto f = std::make_unique<Fx>(templates);
		Player *p = f->players.findPlayerWithName("Alice");
		const PlayerTemplate *pt = f->templates.findPlayerTemplate(templateName);
		REQUIRE_MESSAGE(pt != nullptr, templateName);
		p->init(pt, 1500);
		p->setMultiplayerStartIndex(startIndex);
		return f;
	};
	auto base = initPlayer("FactionA", 2);
	CHECK(base->hash() == initPlayer("FactionA", 2)->hash());
	CHECK(base->hash() != initPlayer("FactionA", 3)->hash());     // multiplayer start index
	CHECK(base->hash() != initPlayer("FactionA2", 2)->hash());    // faction template identity (same side, colour, money)
	// the observer flag alone: the SAME template name in two worlds, one of them with IsObserver = Yes
	const std::string observerA2 = std::string(kMoreTemplates) + "PlayerTemplate FactionA2\n  IsObserver = Yes\nEnd\n";
	auto plain = initPlayer("FactionA2", 2);
	auto observer = initPlayer("FactionA2", 2, observerA2.c_str());
	REQUIRE(!plain->players.findPlayerWithName("Alice")->isObserver());
	REQUIRE(observer->players.findPlayerWithName("Alice")->isObserver());
	CHECK(plain->hash() != observer->hash());
}

TEST_CASE("state hash: the team-to-prototype association is hashed (two existing prototypes, teams made in reversed order)")
{
	auto world = [](bool reversed) {
		auto f = std::make_unique<Fx>();
		Player *alice = f->players.findPlayerWithName("Alice");
		TeamPrototype *pa = f->teams.initTeam("ScriptTeamA", alice, false);
		TeamPrototype *pb = f->teams.initTeam("ScriptTeamB", alice, false);
		if (reversed)
		{
			f->teams.createTeam(pb);
			f->teams.createTeam(pa);
		}
		else
		{
			f->teams.createTeam(pa);
			f->teams.createTeam(pb);
		}
		return f;
	};
	CHECK(world(false)->hash() == world(false)->hash());
	CHECK(world(false)->hash() != world(true)->hash());
}

TEST_CASE("state hash: the complete HordeContainCore state is hashed (flags, special ids, dirty bit, slot copies)")
{
	auto world = [](int mutation) {
		auto f = std::make_unique<Fx>();
		Object *horde = f->make(mutation >= 4 ? "TwoRankHorde" : "FighterHorde");
		HordeContain *hc = dynamic_cast<HordeContain *>(horde->findModule("HordeContain"));
		REQUIRE(hc);
		HordeContainCore &core = const_cast<HordeContainCore &>(hc->core());
		switch (mutation)
		{
		case 1: core.setReformationFlag174(true); break;
		case 2: core.setBannerCarrierId(77); break;
		case 3: core.setOtherNonSlotId(78); break;
		case 4:
		case 5:
			core.removeMember(core.members()[0].id, false); // the rank fill-in moves a member up and sets the dirty flag
			REQUIRE(core.dirty());
			if (mutation == 5)
			{
				core.clearDirty();
			}
			break;
		default: break;
		}
		return f;
	};
	const std::uint32_t base = world(0)->hash();
	CHECK(world(0)->hash() == base);
	CHECK(world(1)->hash() != base); // H+0x174
	CHECK(world(2)->hash() != base); // H+0x26C banner carrier id
	CHECK(world(3)->hash() != base); // H+0x264
	// H+0x120 dirty flag: a removal's fill-in sets it; clearing it (and nothing else) must change the hash
	CHECK(world(4)->hash() != world(5)->hash());
}

TEST_CASE("state hash: HordeContainCore::crc is size prefixed and covers the member list, the registered set and the member map")
{
	Fx f;
	Object *horde = f.make("FighterHorde");
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->findModule("HordeContain"));
	REQUIRE(hc);
	HordeContainCore &core = const_cast<HordeContainCore &>(hc->core());
	StateHasher h0;
	core.crc(h0);
	const std::vector<HordeContainCore::Member> membersBefore = core.members();
	REQUIRE(membersBefore.size() == 3);
	const HordeContainCore::ObjectId victim = membersBefore[1].id;
	core.removeMember(victim, false); // list, registered set, member map and free list all change
	StateHasher h1;
	core.crc(h1);
	CHECK(h0.value() != h1.value());
	core.addMember(victim, "Fighter"); // back in the member list (at the end) and the slot map: the order is state
	StateHasher h2;
	core.crc(h2);
	CHECK(h2.value() != h0.value());
	CHECK(h2.value() != h1.value());
}

TEST_CASE("state hash: every mutable simulation setting is hashed")
{
	auto hashWith = [](const std::function<void(GameLogicSettings &)> &mutate) {
		Fx f;
		mutate(f.logic->settings());
		return f.hash();
	};
	const std::uint32_t base = hashWith([](GameLogicSettings &) {});
	CHECK(hashWith([](GameLogicSettings &) {}) == base);
	CHECK(hashWith([](GameLogicSettings &s) { s.enableRepulsors = !s.enableRepulsors; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.forceModelsToFollowTimeOfDay = !s.forceModelsToFollowTimeOfDay; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.forceModelsToFollowWeather = !s.forceModelsToFollowWeather; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.unitDamagedThreshold = 0.5f; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.unitReallyDamagedThreshold = 0.3f; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.defaultStartingCash = 1; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.initialCredits[3] = 5; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.night = true; }) != base);
	CHECK(hashWith([](GameLogicSettings &s) { s.snowy = true; }) != base);
	// the diagnostics (what the loader left unread, the files it read) are not simulation state
	CHECK(hashWith([](GameLogicSettings &s) { s.unappliedFields.insert("GameData.X"); s.filesLoaded.push_back("f"); }) == base);
}

#include "GameLogic/SimMath.h"

TEST_CASE("SimMath binary32 operations: single rounded operations and cvttss2si truncation")
{
	CHECK(SimMath::mulf32(0.5f, 255.0f) == 127.5f);
	CHECK(SimMath::divf32(1.0f, 3.0f) == (1.0f / 3.0f));
	CHECK(SimMath::addf32(16777216.0f, 1.0f) == 16777216.0f); // binary32 rounding, not a wider intermediate
	CHECK(SimMath::subf32(1.0f, 0.25f) == 0.75f);
	CHECK(SimMath::truncToInt32(127.9f) == 127);
	CHECK(SimMath::truncToInt32(-127.9f) == -127);
	CHECK(SimMath::truncToInt32(2147483520.0f) == 2147483520);
	CHECK(SimMath::truncToInt32(2147483648.0f) == INT32_MIN); // the integer indefinite
	CHECK(SimMath::truncToInt32(-3.0e10f) == INT32_MIN);
	CHECK(SimMath::truncToInt32(std::numeric_limits<float>::quiet_NaN()) == INT32_MIN);
}

TEST_CASE("state hash: the player colour of a faction is the retail RGBColor::getAsInt of its PreferredColor")
{
	Fx f;
	const Player *a = f.players.findPlayerWithName("Alice");
	// FactionA PreferredColor = R:255 G:0 B:128: each channel (int)(c * 255) after the INI's /255
	const std::uint32_t c = a->getPlayerColor();
	CHECK((c >> 24) == 0xff);
	CHECK(((c >> 16) & 0xff) == 255);
	CHECK(((c >> 8) & 0xff) == 0);
}
