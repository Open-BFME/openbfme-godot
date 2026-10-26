// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4a (docs/SAVES.md section 6, tests T1-T5 and T8 as far as the core allows): the Xfer framework (Common/Xfer.h: retail's byte layout of a
// version, a string and a block, the version window, XferCRC's step), GameState (Common/GameState.h: the registry, the refusal, the stream and its failures)
// and the core blocks (GameLogic/GameLogicSnapshot.cpp: teams, players, the frame state, the RNG) on a synthetic game whose changed state the core covers.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/GameState.h"
#include "Common/JobSystem.h"
#include "Common/Science.h"
#include "Common/Snapshot.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/ObjectFilter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> bytes(std::initializer_list<int> v)
{
	std::vector<std::uint8_t> out;
	for (int b : v)
	{
		out.push_back((std::uint8_t)b);
	}
	return out;
}

std::string loadError(const std::vector<std::uint8_t> &data, const std::function<void(Xfer &)> &fn)
{
	try
	{
		XferLoad x(data);
		fn(x);
		return "(loaded)";
	}
	catch (const XferException &e)
	{
		return e.what();
	}
}

// a snapshot of one int, with its own version window and a post-process that records its order
struct IntBlock : Snapshot
{
	std::int32_t value = 0;
	XferVersion version{ 1, 2 };
	GameState *state = nullptr;
	std::vector<std::string> *postLog = nullptr;
	std::string name;
	void crc(Xfer *x) override { xfer(x); }
	void xfer(Xfer *x) override
	{
		XferVersion v = version;
		x->xferVersion(v);
		x->xferInt(&value);
		if (x->isLoading() && state && postLog)
		{
			state->addPostProcessSnapshot(this);
		}
	}
	void loadPostProcess() override { postLog->push_back(name); }
};
} // namespace

TEST_CASE("save4a Xfer: retail's byte layout of a version, the primitives, a short and a long string and nested blocks (RW 0xA1F5B9, 0xA1FB84, 0xA2111A, "
		  "0xA206EF)")
{
	XferSave s;
	XferVersion v{ 1, 3 };
	s.xferVersion(v);
	bool b = true;
	s.xferBool(&b);
	std::int16_t i16 = -2;
	s.xferShort(&i16);
	std::uint32_t u32 = 0x11223344u;
	s.xferUnsignedInt(&u32);
	float f = 1.5f;
	s.xferReal(&f);
	std::string shortS = "ab", longS(300, 'z');
	s.xferAsciiString(&shortS);
	s.beginBlock();
	s.xferAsciiString(&longS);
	s.beginBlock();
	std::uint8_t inner = 7;
	s.xferUnsignedByte(&inner);
	s.endBlock();
	s.endBlock();
	CHECK(s.openBlocks() == 0);
	const std::vector<std::uint8_t> &d = s.data();
	// version 3 (one byte), bool 1, short fe ff, u32 44 33 22 11, 1.5f = 0x3FC00000, "ab" = 02 'a' 'b'
	const std::vector<std::uint8_t> head = bytes({ 3, 1, 0xFE, 0xFF, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0xC0, 0x3F, 2, 'a', 'b' });
	REQUIRE(d.size() == head.size() + 8 + 1 + 4 + 300 + 8 + 1);
	CHECK(std::vector<std::uint8_t>(d.begin(), d.begin() + (std::ptrdiff_t)head.size()) == head);
	size_t at = head.size();
	CHECK(std::vector<std::uint8_t>(d.begin() + (std::ptrdiff_t)at, d.begin() + (std::ptrdiff_t)at + 4) == bytes({ 'K', 'O', 'L', 'B' })); // u32 "BLOK"
	std::uint32_t outerEnd = 0, innerEnd = 0;
	std::memcpy(&outerEnd, &d[at + 4], 4);
	CHECK(outerEnd == d.size()); // the absolute offset just past the block, not a size
	at += 8;
	CHECK(d[at] == 0xFF); // a 300-byte string: 0xFF, then a u32 length
	std::uint32_t len = 0;
	std::memcpy(&len, &d[at + 1], 4);
	CHECK(len == 300);
	at += 5 + 300;
	std::memcpy(&innerEnd, &d[at + 4], 4);
	CHECK(innerEnd == d.size());
	CHECK(d[at + 8] == 7);

	XferLoad l(d);
	XferVersion lv{ 1, 3 };
	l.xferVersion(lv);
	CHECK(lv.current == 3);
	bool lb = false;
	l.xferBool(&lb);
	std::int16_t li16 = 0;
	l.xferShort(&li16);
	std::uint32_t lu32 = 0;
	l.xferUnsignedInt(&lu32);
	float lf = 0.0f;
	l.xferReal(&lf);
	std::string ls, ll;
	l.xferAsciiString(&ls);
	CHECK(l.beginBlock() == 5 + 300 + 8 + 1); // the string (0xFF, u32, 300 bytes), the inner block (BLOK, end) and its byte
	l.xferAsciiString(&ll);
	l.skipBlock();
	l.endBlock();
	CHECK(l.atEnd());
	CHECK(lb == true);
	CHECK(li16 == -2);
	CHECK(lu32 == 0x11223344u);
	CHECK(lf == 1.5f);
	CHECK(ls == "ab");
	CHECK(ll == longS);
}

TEST_CASE("save4a Xfer: the version window (RW 0xA1F5B9): any version from the earliest to the current loads into `current`, others are retail's errors")
{
	const auto loadVersion = [](int onDisk, XferVersion window) {
		XferVersion v = window;
		const std::string e = loadError(bytes({ onDisk }), [&](Xfer &x) { x.xferVersion(v); });
		return e == "(loaded)" ? "loaded " + std::to_string(v.current) : e;
	};
	CHECK(loadVersion(2, { 2, 4 }) == "loaded 2");
	CHECK(loadVersion(3, { 2, 4 }) == "loaded 3");
	CHECK(loadVersion(4, { 2, 4 }) == "loaded 4");
	CHECK(loadVersion(1, { 2, 4 }) == "Found version 1 but the earliest supported is 2");
	CHECK(loadVersion(5, { 2, 4 }) == "Don't know how to load version 5 (most recent is 4)");
	XferSave s;
	XferVersion v{ 2, 4 };
	s.xferVersion(v);
	CHECK(s.data() == bytes({ 4 }));
}

TEST_CASE("save4a Xfer: a load never reads past its stream or its block, and names what is wrong")
{
	CHECK(loadError(bytes({ 1, 2 }), [](Xfer &x) {
		std::uint32_t v;
		x.xferUnsignedInt(&v);
	}) == "the snapshot ends early: 4 bytes wanted at offset 0, 2 left in the stream");
	CHECK(loadError(bytes({ 2 }), [](Xfer &x) {
		bool b;
		x.xferBool(&b);
	}) == "a bool transfer read 2");
	CHECK(loadError(bytes({ 0xFF, 0x00, 0x00, 0x00, 0x40, 'a' }), [](Xfer &x) {
		std::string s;
		x.xferAsciiString(&s);
	}) == "a string declares 1073741824 bytes, more than the 1 left");
	CHECK(loadError(bytes({ 0x00, 0x00, 0x01, 0x00 }), [](Xfer &x) {
		std::uint32_t n = 0;
		x.xferCount(&n, 1, 1);
	}) == "a list declares 65536 entries of at least 1 bytes, more than the 0 bytes left");
	CHECK(loadError(bytes({ 'X', 'O', 'L', 'B', 8, 0, 0, 0 }), [](Xfer &x) { x.beginBlock(); }) == "expected a block ('BLOK') at offset 0");
	CHECK(loadError(bytes({ 'K', 'O', 'L', 'B', 12, 0, 0, 0 }), [](Xfer &x) { x.beginBlock(); }) == "the block at offset 0 ends at 12, outside 8..8");
	// a block holding two bytes read as one, and as four
	const std::vector<std::uint8_t> two = bytes({ 'K', 'O', 'L', 'B', 10, 0, 0, 0, 5, 6 });
	CHECK(loadError(two, [](Xfer &x) {
		x.beginBlock();
		std::uint8_t b;
		x.xferUnsignedByte(&b);
		x.endBlock();
	}) == "a block's load read less than it holds (at offset 9, the block ends at 10)");
	CHECK(loadError(two, [](Xfer &x) {
		x.beginBlock();
		std::uint32_t v;
		x.xferUnsignedInt(&v);
	}) == "the snapshot ends early: 4 bytes wanted at offset 8, 2 left in the block");
}

TEST_CASE("save4a XferCRC: every transfer adds its whole little-endian words, then one rotate-add per trailing byte (RW 0xA211DF); blocks add nothing")
{
	XferCRC c;
	std::uint32_t a = 0x01020304u;
	std::uint8_t b = 0x7F;
	std::string s = "hello"; // a length byte, then "hell" as a word and 'o' as a byte
	c.xferUnsignedInt(&a);
	c.beginBlock();
	c.xferUnsignedByte(&b);
	c.endBlock();
	c.xferAsciiString(&s);
	std::uint32_t crc = 0;
	const auto step = [&crc](std::uint32_t w) { crc = ((crc << 1) | (crc >> 31)) + w; };
	step(0x01020304u);
	step(0x7Fu);
	step(5u);
	step((std::uint32_t)'h' | ((std::uint32_t)'e' << 8) | ((std::uint32_t)'l' << 16) | ((std::uint32_t)'l' << 24));
	step((std::uint32_t)'o');
	CHECK(c.getCRC() == crc);
}

TEST_CASE("save4a GameState: the stream (RW 0xA20DD0 / 0x6DDF2F framing), the block order, the post-process order and every load failure named (T5)")
{
	GameState gs;
	std::vector<std::string> post;
	IntBlock a, b;
	a.state = b.state = &gs;
	a.postLog = b.postLog = &post;
	a.name = "a";
	b.name = "b";
	a.value = 11;
	b.value = -3;
	gs.addSnapshotBlock("test:A", &a, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", { "section a" });
	gs.addSnapshotBlock("test:B", &b, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", { "section b" });
	std::vector<std::uint8_t> stream;
	std::string error;
	REQUIRE_MESSAGE(gs.saveSnapshot({ "section a", "section b" }, stream, &error), error);
	// "ALAE" "2STR" 1 0, then "test:A" BLOK end [2, 11], "test:B" BLOK end [2, -3], "SG_EOF"
	std::vector<std::uint8_t> expect = bytes({ 'A', 'L', 'A', 'E', '2', 'S', 'T', 'R', 1, 0, 0, 0, 0, 0, 0, 0 });
	const auto putBlock = [&expect](const char *token, std::int32_t value) {
		expect.push_back((std::uint8_t)std::strlen(token));
		expect.insert(expect.end(), token, token + std::strlen(token));
		expect.insert(expect.end(), { 'K', 'O', 'L', 'B' });
		const std::uint32_t end = (std::uint32_t)(expect.size() + 4 + 1 + 4);
		const std::uint8_t *e = (const std::uint8_t *)&end;
		expect.insert(expect.end(), e, e + 4);
		expect.push_back(2);
		const std::uint8_t *v = (const std::uint8_t *)&value;
		expect.insert(expect.end(), v, v + 4);
	};
	putBlock("test:A", 11);
	putBlock("test:B", -3);
	expect.insert(expect.end(), { 6, 'S', 'G', '_', 'E', 'O', 'F' });
	CHECK(stream == expect);

	a.value = b.value = 0;
	REQUIRE_MESSAGE(gs.loadSnapshot({ "section a", "section b" }, stream, &error), error);
	CHECK(a.value == 11);
	CHECK(b.value == -3);
	CHECK(post == std::vector<std::string>{ "a", "b" }); // queued during the loads, run after the last block, in queue order

	const auto loadFails = [&gs](std::vector<std::uint8_t> data) {
		std::string e;
		return gs.loadSnapshot({ "section a", "section b" }, data, &e) ? std::string("(loaded)") : e;
	};
	SUBCASE("damaged streams")
	{
		// cut inside test:A's end offset, then inside its payload (its end lies past the stream)
		CHECK(loadFails(std::vector<std::uint8_t>(stream.begin(), stream.begin() + 30))
			== "the snapshot could not be loaded: the snapshot ends early: 4 bytes wanted at offset 27, 3 left in the stream");
		CHECK(loadFails(std::vector<std::uint8_t>(stream.begin(), stream.begin() + 33))
			== "the snapshot could not be loaded: the block at offset 23 ends at 36, outside 31..33");
		std::vector<std::uint8_t> magic = stream;
		magic[0] = 'X';
		CHECK(loadFails(magic) == "the snapshot could not be loaded: not a snapshot stream (no \"ALAE\" \"2STR\" header)");
		std::vector<std::uint8_t> ver = stream;
		ver[8] = 2;
		CHECK(loadFails(ver) == "the snapshot could not be loaded: snapshot stream version 2 (this build reads version 1)");
		std::vector<std::uint8_t> tagged = stream;
		tagged[12] = 1;
		CHECK(loadFails(tagged) == "the snapshot could not be loaded: a tagged snapshot stream (OpenBFME writes and reads untagged streams only)");
		std::vector<std::uint8_t> trailing = stream;
		trailing.push_back(0);
		CHECK(loadFails(trailing) == "the snapshot could not be loaded: the snapshot has 1 bytes after its end token");
	}
	SUBCASE("a version above the class's current and below its earliest (each class's window)")
	{
		std::vector<std::uint8_t> high = stream;
		high[16 + 7 + 8] = 3; // test:A's version byte
		CHECK(loadFails(high) == "the snapshot could not be loaded: block 'test:A': Don't know how to load version 3 (most recent is 2)");
		std::vector<std::uint8_t> low = stream;
		low[16 + 7 + 8] = 0;
		CHECK(loadFails(low) == "the snapshot could not be loaded: block 'test:A': Found version 0 but the earliest supported is 1");
	}
	SUBCASE("an unknown block, a missing block, a block twice")
	{
		GameState other;
		IntBlock c;
		other.addSnapshotBlock("test:A", &a, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", {});
		other.addSnapshotBlock("test:C", &c, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", {});
		std::vector<std::uint8_t> withC;
		other.saveBlocks({ "test:A", "test:C" }, withC);
		CHECK(loadFails(withC) == "the snapshot could not be loaded: the snapshot carries an unknown block 'test:C'");
		std::vector<std::uint8_t> onlyA, twice;
		other.saveBlocks({ "test:A" }, onlyA);
		CHECK(loadFails(onlyA) == "the snapshot could not be loaded: the snapshot lacks the blocks test:B");
		gs.saveBlocks({ "test:A", "test:A", "test:B" }, twice);
		CHECK(loadFails(twice) == "the snapshot could not be loaded: the snapshot carries the block 'test:A' twice");
	}
}

TEST_CASE("snapshot reader: every truncated block boundary and impossible block end is refused before changing state")
{
	GameState state;
	IntBlock first, second;
	first.value = 123;
	second.value = 456;
	state.addSnapshotBlock("test:A", &first, 1u << SNAPSHOT_SAVELOAD, "test", {});
	state.addSnapshotBlock("test:B", &second, 1u << SNAPSHOT_SAVELOAD, "test", {});
	std::vector<std::uint8_t> good;
	std::string error;
	REQUIRE_MESSAGE(state.saveSnapshot({}, good, &error), error);
	for (size_t cut = 0; cut < good.size(); ++cut)
	{
		const std::vector<std::uint8_t> prefix(good.begin(), good.begin() + (std::ptrdiff_t)cut);
		CAPTURE(cut);
		CHECK_FALSE(state.checkFraming(prefix, &error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(state.loadSnapshot({}, prefix, &error));
		CHECK(first.value == 123);
		CHECK(second.value == 456);
	}
	std::vector<std::uint8_t> bad = good;
	const std::array<std::uint8_t, 4> marker{ 'K', 'O', 'L', 'B' };
	const auto at = std::search(bad.begin(), bad.end(), marker.begin(), marker.end());
	REQUIRE(at != bad.end());
	const std::uint32_t huge = 1u << 31;
	std::memcpy(&bad[(size_t)(at - bad.begin()) + 4], &huge, 4);
	CHECK_FALSE(state.loadSnapshot({}, bad, &error));
	CHECK(error.find("outside") != std::string::npos);
	CHECK(first.value == 123);
	CHECK(second.value == 456);
}

TEST_CASE("snapshot reader: 2^31 Xfer entry counts and string lengths refuse before allocation")
{
	CHECK(loadError(bytes({ 0, 0, 0, 0x80 }), [](Xfer &x) {
		std::uint32_t count = 0;
		x.xferCount(&count, 1, 1);
	}) == "a list declares 2147483648 entries of at least 1 bytes, more than the 0 bytes left");
	CHECK(loadError(bytes({ 0xFF, 0, 0, 0, 0x80 }), [](Xfer &x) {
		std::string text;
		x.xferAsciiString(&text);
	}) == "a string declares 2147483648 bytes, more than the 0 left");
}

TEST_CASE("save4a GameState: no partial snapshots: a block without an xfer and a state hash section without a block are a named refusal (T4)")
{
	GameState gs;
	IntBlock a;
	gs.addSnapshotBlock("test:A", &a, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", { "section a" });
	gs.declareBlockWithoutXfer("test:Later", 1u << SNAPSHOT_SAVELOAD, "SAVE-4b", { "section later" });
	gs.declareRebuiltSection("section rebuilt", "configuration");
	std::vector<std::uint8_t> out;
	std::string error;
	CHECK_FALSE(gs.saveSnapshot({ "section a", "section later", "section rebuilt", "section new" }, out, &error));
	CHECK(error == "the snapshot is refused: 1 sections of the state hash have no snapshot block: section new; 1 simulation blocks have no xfer yet: test:Later (SAVE-4b)");
	CHECK(out.empty());
	CHECK_FALSE(gs.loadSnapshot({ "section a", "section later", "section rebuilt" }, out, &error));
	CHECK(error == "this build cannot load a snapshot: 1 simulation blocks have no xfer yet: test:Later (SAVE-4b)");
	IntBlock later;
	gs.provideSnapshot("test:Later", &later, "SAVE-4b");
	CHECK(gs.refusal({ "section a", "section later", "section rebuilt" }).empty());
	REQUIRE_MESSAGE(gs.saveSnapshot({ "section a", "section later", "section rebuilt" }, out, &error), error);
}

TEST_CASE("save4a: the game's block registry is the exact inventory: every simulation subsystem's block with its xfer, by the lane that ported it (T4)")
{
	logictest::LogicWorld f;
	std::vector<std::string> registry;
	for (const GameState::Block &b : f.logic->gameState().blocks())
	{
		registry.push_back(b.token + (b.snapshot ? " xfer " : " declared ") + b.owner);
	}
	CHECK(registry == std::vector<std::string>{
						  "obfme:TeamFactory xfer SAVE-4a",
						  "obfme:Players xfer SAVE-4a",
						  "obfme:GameLogic xfer SAVE-4a",
						  "obfme:Random xfer SAVE-4a",
						  "obfme:Objects xfer SAVE-4b",
						  "obfme:UpdateScheduler xfer SAVE-4b",
						  "obfme:TunnelTracker xfer SAVE-4b",
						  "obfme:Combat xfer SAVE-4b",
						  "obfme:SellList xfer SAVE-4b",
						  "obfme:Experience xfer SAVE-4b",
						  "obfme:Invisibility xfer SAVE-4b",
						  "obfme:DamageOverTime xfer SAVE-4b",
						  "obfme:Partition xfer SAVE-4c",
						  "obfme:Emotions xfer SAVE-4c",
						  "obfme:AIWorld xfer SAVE-4c",
						  "obfme:Shroud xfer SAVE-4c",
						  "obfme:SkirmishAI xfer SAVE-4c",
						  "obfme:ScriptEngine xfer SAVE-4d",
						  "obfme:Victory xfer SAVE-4d",
						  "obfme:Weather xfer SAVE-4d",
						  "obfme:Economy xfer SAVE-4d",
						  "obfme:CreateAHero xfer SAVE-4d",
						  "obfme:TerrainTrees xfer SAVE-4d",
						  "obfme:LargeGroupAudio xfer SAVE-4d",
						  "CHUNK_ObjectivesMenu xfer CAMPUI-1",
						  "CHUNK_MissionObjectives xfer CAMPUI-1",
					  });
	// this synthetic game's state hash sections are all declared (no shroud or pathfinder contributor: those exist in a real game, test_save4a_retail.cpp)
	CHECK(f.logic->gameState().undeclaredSections(f.logic->stateHashSectionNames()).empty());
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(f.logic->saveSnapshot(out, &error), error); // lane SAVE-4e: every block has its xfer (SAVE-4b, 4c, 4d merged)
	CHECK(!out.empty());
	CHECK(f.logic->gameState().missingBlocks(SNAPSHOT_SAVELOAD).empty());
	CHECK(f.logic->gameState().missingBlocks(SNAPSHOT_DEEPCRC_LOGICONLY).empty());
}

namespace
{
// a synthetic game has no science data: TheScienceStore of a retail world a test before left on this thread (thread_local) must not judge its sciences
// (lane SAVE-4e: the Players block checks a disabled / hidden science against the store when there is one)
struct NoScienceStore
{
	ScienceStore *saved = TheScienceStore;
	NoScienceStore() { TheScienceStore = nullptr; }
	~NoScienceStore() { TheScienceStore = saved; }
};

const std::vector<std::string> kCore = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };
const std::vector<std::string> kCoreSections = { "frame, object ids, RNG", "players and teams" };

void runFrames(GameLogic &logic, int n)
{
	for (int i = 0; i < n; ++i)
	{
		for (int phase = 1; phase <= 6; ++phase)
		{
			logic.update(phase);
		}
	}
}

// what both games share: the START (the map's prototypes: a non-singleton prototype the scripts can make teams of) and 37 frames (running frames also
// advances state outside the core, the weather's and the invisibility manager's, whose blocks are SAVE-4d's and SAVE-4b's)
void startExtras(logictest::LogicWorld &f)
{
	f.teams.initTeam("Raiders", f.players.findPlayerWithName("Bob"), false);
	runFrames(*f.logic, 37);
}

// what the game changed after the START: only the core's state (frames, the RNG, the frame state, players, teams)
void playCoreState(logictest::LogicWorld &f)
{
	f.activateUpgrades();
	GameLogic &logic = *f.logic;
	for (int i = 0; i < 5; ++i)
	{
		logic.random().getValue(0, 1000, __FILE__, __LINE__);
	}
	logic.setGameDifficulty(2);
	GameRules rules = MpGameRules::none();
	rules[0] = 3;
	logic.setGameRules(rules);
	logic.setLegendaryHeroes(4);
	logic.setFrameAdvance(false);
	logic.noteExplicitObjectID(41); // the next object id moves past an id the game used
	Player *alice = f.players.findPlayerWithName("Alice");
	Player *bob = f.players.findPlayerWithName("Bob");
	alice->depositMoney(700);
	alice->withdrawMoney(150);
	alice->commandPoints().addUsage(12);
	alice->commandPoints().addBonus(3);
	auto filter = std::make_shared<ObjectFilter>();
	filter->includeNames = { "AlphaKeep" };
	filter->rule = ObjectFilter::RULE_ANY;
	alice->commandPoints().addRecord(25, 9, filter);
	alice->addCompletedUpgrade("Upgrade_Basic");
	alice->addCostModifier(filter, { 0.25f, -0.5f }, 17, true);
	alice->addUpgradeDiscount("SomeSource", { 0.1f, 0.2f }, { "Upgrade_X" });
	alice->setPlayerRelationship(bob, NEUTRAL);
	alice->setDefeatFrame(30);
	alice->setBountyPercent(0.5f);
	alice->setAttackedBy(1, 33);
	alice->setOrderMode(1);
	alice->setTemplateBuildable(12, false);
	alice->setScienceAvailability(77, 2);
	alice->selection() = { 4, 5, 6 };
	alice->hotkeySquad(3) = { 6, 4 };
	HeroRecord hero;
	hero.experience = 12.5f;
	hero.rank = 2;
	hero.templateName = "AlphaHero";
	hero.upgradeMask.set(3);
	alice->heroes().addRecord(hero);
	bob->setDefeated(true);
	bob->getScoreKeeper().setEndFrame(36);
	TeamPrototype *raiders = f.teams.findTeamPrototype("Raiders");
	Team *t1 = f.teams.createTeam(raiders, true);
	Team *t2 = f.teams.createTeam(raiders, false);
	t1->setState("Attacking");
	t1->setCustomState("Rally", true);
	t1->setTeamTargetId(99);
	t1->setCurrentWaypointId(5);
	t1->setTeamRelationship(alice->getDefaultTeam(), ALLIES);
	t2->setPlayerRelationship(alice, ENEMIES);
	t2->scriptState().genericNextFrame[4] = 120;
	t2->scriptState().attemptGeneric[4] = false;
	alice->setTeamRelationship(t2, ENEMIES);
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
} // namespace

TEST_CASE("save4a core blocks: a synthetic game whose changes the core covers is saved, loaded into a new game of the same START and has the same state "
		  "hash, the same players, teams and next random numbers, and goes on equal (T1 as far as the core allows); the stream is reproducible (T2)")
{
	NoScienceStore noScienceStore;
	logictest::LogicWorld saved;
	startExtras(saved);
	logictest::LogicWorld fresh;
	startExtras(fresh);
	const std::map<std::string, std::uint32_t> untouched = sectionValues(*fresh.logic);
	playCoreState(saved);
	// the core covers what the game changed: every section outside the core's equals a game that never ran (isolation, not a threshold)
	const std::map<std::string, std::uint32_t> changed = sectionValues(*saved.logic);
	REQUIRE(changed.size() == untouched.size());
	std::vector<std::string> changedSections;
	for (const auto &kv : changed)
	{
		if (untouched.at(kv.first) != kv.second)
		{
			changedSections.push_back(kv.first);
		}
	}
	CHECK(changedSections == std::vector<std::string>{ "frame, object ids, RNG", "players and teams" });

	saved.activateUpgrades();
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(kCore, stream);
	const std::uint32_t savedHash = saved.logic->computeStateHash();

	fresh.activateUpgrades();
	REQUIRE(fresh.logic->computeStateHash() != savedHash);
	fresh.logic->gameState().loadBlocks(kCore, stream);
	CHECK(fresh.logic->computeStateHash() == savedHash);
	CHECK(sectionValues(*fresh.logic) == changed);
	// the effects in the world, by name
	GameLogic &logic = *fresh.logic;
	const Player *alice = fresh.players.findPlayerWithName("Alice");
	CHECK(logic.getFrame() == 37);
	CHECK(logic.getGameDifficulty() == 2);
	CHECK(logic.legendaryHeroes() == 4);
	CHECK(logic.getNextObjectID() == 42);
	CHECK(alice->getMoney()->countMoney() == 1500 + 700 - 150);
	CHECK(alice->hasUpgradeComplete("Upgrade_Basic"));
	CHECK(alice->getRelationship(fresh.players.findPlayerWithName("Bob")) == NEUTRAL);
	CHECK(alice->hotkeySquad(3) == std::vector<ObjectID>{ 6, 4 });
	CHECK(alice->heroes().records().at(0).templateName == "AlphaHero");
	CHECK(fresh.players.findPlayerWithName("Bob")->isDefeated());
	const TeamPrototype *raiders = fresh.teams.findTeamPrototype("Raiders");
	REQUIRE(raiders->teams().size() == 2);
	CHECK(raiders->teams()[0]->getState() == "Attacking");
	CHECK(raiders->teams()[0]->hasCustomState("Rally"));
	CHECK(raiders->teams()[1]->getRelationship(alice->getDefaultTeam()) == ENEMIES);
	CHECK(alice->getRelationship(raiders->teams()[1]) == ENEMIES);
	// the frame travels too: a game of the same START that never ran a frame reads frame 37
	{
		logictest::LogicWorld atStart;
		atStart.teams.initTeam("Raiders", atStart.players.findPlayerWithName("Bob"), false);
		atStart.activateUpgrades();
		REQUIRE(atStart.logic->getFrame() == 0);
		atStart.logic->gameState().loadBlocks(kCore, stream);
		CHECK(atStart.logic->getFrame() == 37);
		fresh.activateUpgrades();
	}
	// T2: save(load(save)) is the same bytes
	std::vector<std::uint8_t> again;
	fresh.logic->gameState().saveBlocks(kCore, again);
	CHECK(again == stream);
	// the generator goes on with the uninterrupted game's numbers, and the frames stay equal
	for (int i = 0; i < 20; ++i)
	{
		CHECK(fresh.logic->random().getValue(0, 1 << 30, __FILE__, __LINE__) == saved.logic->random().getValue(0, 1 << 30, __FILE__, __LINE__));
	}
	saved.logic->setFrameAdvance(true);
	fresh.logic->setFrameAdvance(true);
	runFrames(*saved.logic, 10);
	runFrames(*fresh.logic, 10);
	CHECK(fresh.logic->getFrame() == 47);
	CHECK(fresh.logic->computeStateHash() == saved.logic->computeStateHash());
	// the deep logic CRC of the core equals over the two games (XferCRC over the same fields)
	XferCRC a, b;
	for (const std::string &t : kCore)
	{
		a.xferSnapshot(saved.logic->gameState().findBlock(t)->snapshot);
		b.xferSnapshot(fresh.logic->gameState().findBlock(t)->snapshot);
	}
	CHECK(a.getCRC() == b.getCRC());
}

TEST_CASE("save4a core blocks: a snapshot of another game is refused naming what differs (T5: the player count, a player, a team prototype, the RNG "
		  "algorithm, a team the game has and the snapshot lacks)")
{
	NoScienceStore noScienceStore;
	logictest::LogicWorld saved;
	startExtras(saved);
	playCoreState(saved);
	std::vector<std::uint8_t> stream;
	saved.logic->gameState().saveBlocks(kCore, stream);
	const auto loadInto = [&stream](logictest::LogicWorld &w) {
		w.activateUpgrades();
		try
		{
			w.logic->gameState().loadBlocks(kCore, stream);
			return std::string("(loaded)");
		}
		catch (const XferException &e)
		{
			return std::string(e.what());
		}
	};
	SUBCASE("another map's team prototypes")
	{
		logictest::LogicWorld other;
		CHECK(loadInto(other) == "block 'obfme:TeamFactory': the snapshot has 4 team prototypes, this game 3");
	}
	SUBCASE("another prototype")
	{
		logictest::LogicWorld other;
		other.teams.initTeam("Raiders", other.players.findPlayerWithName("Alice"), false);
		CHECK(loadInto(other) == "block 'obfme:TeamFactory': the snapshot's team prototype 4 'Bob/Raiders' is not this game's 4 'Alice/Raiders'");
	}
	SUBCASE("a game that made a team the snapshot does not have (lane SAVE-4e: loads, the later team removed by the post-process)")
	{
		logictest::LogicWorld other;
		startExtras(other);
		for (int i = 0; i < 3; ++i)
		{
			other.teams.createTeam(other.teams.findTeamPrototype("Raiders"));
		}
		const size_t before = other.teams.teamCount();
		CHECK(loadInto(other) == "(loaded)");
		CHECK(other.teams.teamCount() == before - 1); // the snapshot's 5 teams; the game's sixth (made after the snapshot's moment) is gone
	}
	SUBCASE("a player of another faction")
	{
		logictest::LogicWorld other;
		startExtras(other);
		SkirmishSetup setup;
		setup.players.push_back({ "Alice", "FactionB", true, 0, 0, 0 });
		REQUIRE(other.players.applySkirmishSlots(setup).empty());
		CHECK(loadInto(other) == "block 'obfme:Players': the snapshot's player 1 'Alice' (FactionA) is not this game's player 1 'Alice' (FactionB)");
	}
	SUBCASE("the other random generator (S-080: the START names it)")
	{
		logictest::LogicWorld other(RandomAlgorithm::RotWK_GameDat_LCG);
		startExtras(other);
		CHECK(loadInto(other) == "block 'obfme:Random': the snapshot's logic random generator is algorithm 0, this game's is 1");
	}
}

TEST_CASE("save4a T8: the core's blocks and (lane SAVE-4e, every block with its xfer) the whole snapshot are the same bytes under Uber at 1, 4 and 16 "
		  "logic threads")
{
	const int before = JobSystem::logic().threadCount();
	std::vector<std::uint8_t> first, firstWhole;
	for (int threads : { 1, 4, 16 })
	{
		JobSystem::logic().setThreadCount(threads);
		logictest::LogicWorld w;
		startExtras(w);
		w.logic->setSimulationProfile(SimulationProfile::Uber);
		playCoreState(w);
		std::vector<std::uint8_t> stream;
		w.logic->gameState().saveBlocks(kCore, stream);
		std::vector<std::uint8_t> whole;
		std::string error;
		REQUIRE_MESSAGE(w.logic->saveSnapshot(whole, &error), error);
		if (first.empty())
		{
			first = stream;
			firstWhole = whole;
		}
		CHECK(stream == first);
		CHECK(whole == firstWhole);
	}
	JobSystem::logic().setThreadCount(before);
}

TEST_CASE("save4a: the container carries the snapshot part like the others: read back, digest-checked, bounded (T5)")
{
	SaveGameFile s;
	s.header.profile = ProfileIdentity::fromText("data test\n");
	s.header.executionProfile = "enhanced";
	s.header.buildVersion = "v";
	s.header.engineId = "e";
	s.snapshot = { 'A', 'L', 'A', 'E', 9, 8, 7 };
	const std::vector<std::uint8_t> good = s.encode();
	SaveGameFile back;
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::parse(good, back, &error), error);
	CHECK(back.snapshot == s.snapshot);
	std::vector<std::uint8_t> bad = good;
	bad.back() ^= 1;
	CHECK_FALSE(SaveGameFile::parse(bad, back, &error));
	CHECK(error == "the save is damaged (the snapshot part's SHA-256 does not match its content)");
	// a declared size above the limit is refused before anything is read (a sparse file holds the bytes: the limit refuses it, not the file's size)
	const size_t snapAt = good.size() - s.snapshot.size() - 40;
	std::vector<std::uint8_t> head(good.begin(), good.begin() + (std::ptrdiff_t)(snapAt + 40));
	const std::uint32_t declared = (std::uint32_t)SaveGameFile::kMaxSnapshotBytes + 1;
	std::memcpy(head.data() + snapAt + 4, &declared, 4);
	const std::string path = "save4a_large.obfmesave";
	{
		std::ofstream o(path, std::ios::binary | std::ios::trunc);
		o.write((const char *)head.data(), (std::streamsize)head.size());
	}
	std::filesystem::resize_file(path, head.size() + declared);
	CHECK_FALSE(SaveGameFile::load(path, back, &error));
	std::filesystem::remove(path);
	CHECK(error == "the save's snapshot part declares 268435457 bytes (at most 268435456)");
}

// ---- review r1 (Sol): the four fixes, each pinned ------------------------------------------------------------------------------------------------------

TEST_CASE("save4a r1: coverage is checked against the SELECTED list, on save and on load: a section declared only by a block of another list is not "
		  "covered, and a load is refused for a section no block holds")
{
	GameState gs;
	IntBlock a;
	gs.addSnapshotBlock("test:A", &a, 1u << SNAPSHOT_SAVELOAD, "SAVE-4a", { "section a" });
	gs.declareBlockWithoutXfer("test:DeepOnly", 1u << SNAPSHOT_DEEPCRC_LOGICONLY, "SAVE-4c", { "section d" }); // list 1 only
	std::vector<std::uint8_t> out;
	std::string error;
	// list 0 has no block for "section d": an empty-of-d snapshot is refused (before: list 1's declaration let it through)
	CHECK_FALSE(gs.saveSnapshot({ "section a", "section d" }, out, &error));
	CHECK(error == "the snapshot is refused: 1 sections of the state hash have no snapshot block: section d");
	CHECK(gs.undeclaredSections({ "section a", "section d" }, SNAPSHOT_SAVELOAD) == std::vector<std::string>{ "section d" });
	CHECK(gs.undeclaredSections({ "section d" }, SNAPSHOT_DEEPCRC_LOGICONLY).empty());
	// a stream of the covered state loads; the same stream into a game that has a section no block holds (a new contributor) is refused
	REQUIRE_MESSAGE(gs.saveSnapshot({ "section a" }, out, &error), error);
	REQUIRE_MESSAGE(gs.loadSnapshot({ "section a" }, out, &error), error);
	CHECK_FALSE(gs.loadSnapshot({ "section a", "section new" }, out, &error));
	CHECK(error == "this build cannot load a snapshot: 1 sections of the state hash have no snapshot block: section new");
}

TEST_CASE("save4a r1: a container's count is bounded by its entries' minimum encoded size before anything is allocated, and the load's encoded and "
		  "allocation budgets hold across containers")
{
	SUBCASE("a malformed hero list: 1000 records declared in 4,101 bytes (before: about a megabyte allocated)")
	{
		std::vector<std::uint8_t> data = bytes({ 1, 0xE8, 0x03, 0x00, 0x00 }); // version 1, count 1000
		data.resize(4101, 0);
		PlayerHeroList heroes;
		CHECK(loadError(data, [&](Xfer &x) { heroes.xfer(x); }) == "a list declares 1000 entries of at least 184 bytes, more than the 4096 bytes left");
		CHECK(heroes.records().empty());
	}
	SUBCASE("two counts each within the bytes left, together more than the stream holds (the encoded budget)")
	{
		std::vector<std::uint8_t> data = bytes({ 3, 0, 0, 0, 3, 0, 0, 0 });
		data.resize(20, 0);
		CHECK(loadError(data, [](Xfer &x) {
			std::uint32_t a = 0, b = 0;
			x.xferCount(&a, 4, 4);
			x.xferCount(&b, 4, 4);
		}) == "the lists declare 24 bytes of entries in all, more than the stream's 20");
	}
	SUBCASE("entries far larger in memory than encoded (the allocation budget, 64 bytes per stream byte)")
	{
		std::vector<std::uint8_t> data = bytes({ 100, 0, 0, 0 });
		data.resize(104, 0);
		CHECK(loadError(data, [](Xfer &x) {
			std::uint32_t n = 0;
			x.xferCount(&n, 1, 1000);
		}) == "the load would allocate 100000 bytes, more than 64 times the stream's 104");
	}
}

TEST_CASE("save4a r1: team links are validated before any is made: a member listed twice is refused by name and id, and no list is changed (before: "
		  "the intrusive list looped)")
{
	logictest::LogicWorld f;
	REQUIRE_MESSAGE(f.w.load("Object Plain\nEnd\n").empty(), "Plain");
	Team *team = f.teamOf("Alice");
	f.make("Plain", team, 7001);
	f.make("Plain", team, 7002);
	REQUIRE(team->getMemberCount() == 2);
	std::vector<std::uint8_t> stream;
	f.logic->gameState().saveBlocks({ "obfme:TeamFactory" }, stream);
	// the team's member list: count 2, 7001, 7002 -> 7001, 7001
	const std::vector<std::uint8_t> list = bytes({ 2, 0, 0, 0, 0x59, 0x1B, 0, 0, 0x5A, 0x1B, 0, 0 });
	auto at = std::search(stream.begin(), stream.end(), list.begin(), list.end());
	REQUIRE(at != stream.end());
	at[8] = 0x59;
	std::string error;
	try
	{
		f.logic->gameState().loadBlocks({ "obfme:TeamFactory" }, stream);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "team " + std::to_string(team->getID()) + " 'teamAlice': its member object 7001 is listed twice (also in team " + std::to_string(team->getID()) + ")");
	CHECK(team->getMemberCount() == 2);
	CHECK(team->getFirstMember()->getID() == 7001);
	CHECK(team->getLastMember()->getID() == 7002);
}

namespace
{
// an XferLoad that records what each transfer's buffer held before the load wrote it
struct SpyLoad : XferLoad
{
	using XferLoad::XferLoad;
	std::vector<std::uint32_t> seen;
	void xferImplementation(void *data, size_t size) override
	{
		std::uint32_t before = 0;
		std::memcpy(&before, data, size < 4 ? size : 4);
		seen.push_back(before);
		XferLoad::xferImplementation(data, size);
	}
};
} // namespace

TEST_CASE("save4a r1: a load never reads its destination: a bool, an enum and a real are read into zeroed temporaries (before: the destination's old "
		  "value was read, undefined for an uninitialized bool)")
{
	const std::vector<std::uint8_t> data = bytes({ 1, 2, 0, 0, 0, 0, 0, 0x80, 0x3F });
	SpyLoad x(data);
	bool b = true;
	Relationship r = ALLIES;
	float f = 2.5f;
	x.xferBool(&b);
	x.xferEnum(&r);
	x.xferReal(&f);
	CHECK(x.seen == std::vector<std::uint32_t>{ 0, 0, 0 });
	CHECK(b == true);
	CHECK(r == ALLIES);
	CHECK(f == 1.0f);
}
