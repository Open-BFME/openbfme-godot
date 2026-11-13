// OpenBFME unit tests. GPL-3.0.
// PORT-43 (issue #128): GameData/PowerLimit (GlobalData + 0xE94, row RW 0xC00C00, INI::parseInt RW 0x42EC5E) -> GameLogic + 0x118 (m_rankLevelLimit).
//
// Binary verification against RotWK 2.01 game.dat (md5 daed3668006cd90f01c34e5a7da1901f):
//   * GlobalData::GlobalData (RW 0x6429AD) sets +0xE94 = 10 (RW 0x643827); data\ini\gamedata.ini in both RotWK 2.01 and Ennorath 1.9.8.2
//     (data/ini/gamedata.ini:280) sets PowerLimit = 60.
//   * Sole .text reader of GlobalData + 0xE94: RW 0x631430 inside GameLogic's startNewGame tail helper RW 0x6311ED (ecx -> edi = TheGameLogic, called
//     from GameLogic::startNewGame RW 0x6314CD at RW 0x6315A7; NOT Player init — esi iterates the 20 Player* slots at RW 0x6313D9 .. 0x63141E while edi
//     remains TheGameLogic).
//   * All 5 writers of GameLogic + 0x118 in game.dat:
//       1. RW 0x6301DD (GameLogic::GameLogic RW 0x630023): +0x118 = 1000 (0x3E8).
//       2. RW 0x62A20D (init helper RW 0x62A11A, called at RW 0x631555 in startNewGame; GameLogic::reset RW 0x62D232 leaves +0x118 untouched): +0x118 = 1000.
//       3. RW 0x631420 .. 0x631436 (in RW 0x6311ED): when RW 0x625456 (isInSkirmishOrMultiplayerGame, Economy::isMultiplayerGame: GameLogic + 0x110 in
//          {1 LAN, 2 SKIRMISH, 5 INTERNET}) returns non-zero, +0x118 = [TheWritableGlobalData + 0xE94] (PowerLimit).
//       4. RW 0x7CE020 -> RW 0x7BD1A8 -> RW 0x7BB04E (script action 279 PLAYER_SET_RANKLEVELLIMIT): +0x118 = max(param0, 1).
//       5. RW 0x630D6F .. 0x630D78 (GameLogic::xfer RW 0x6308F3): xferInt on +0x118, skipped on XferCRC (RW 0x630ADF).
//   * Sole non-xfer reader of GameLogic + 0x118 in game.dat: RW 0x6B350B inside TheLivingWorldLogic's RW 0x6B34EE, which is unreachable dead code:
//     its only caller RW 0x7827F6 (in SciencePurchasePointTracker::getMaxRankLevel RW 0x7827C8) already gated on pure RW 0x602E64(TheGameLogic) != 0
//     (GameLogic + 0x114 == 1), and RW 0x6B34FD .. 0x6B3504 tests RW 0x602E64(TheGameLogic) again and jumps to RW 0x6B3512 (InitialMaxRingLevel).

#include "doctest.h"

#include "EconGameData.h"
#include "HudTestUtil.h"
#include "IniTestUtil.h"
#include "LogicTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/GameState.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SpellStores.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
const std::vector<std::string> kCoreBlocks = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };

// the XferVersion byte of obfme:GameLogic in a saveBlocks / saveSnapshot stream: token (1-byte length + text), "BLOK" (4B), end offset (4B), then version
int gameLogicBlockVersion(const std::vector<std::uint8_t> &stream)
{
	const std::string token = "obfme:GameLogic";
	auto at = std::search(stream.begin(), stream.end(), token.begin(), token.end());
	REQUIRE(at != stream.end());
	const size_t i = (size_t)(at - stream.begin()) + token.size() + 8;
	REQUIRE(i < stream.size()); // tolerance: an index guard (the byte read next), not a result
	return stream[i];
}

std::string gameDataWithPowerLimit(int powerLimit)
{
	std::string text = econtest::kGameData;
	const std::string needle = "PowerLimit = 60";
	const size_t pos = text.find(needle);
	REQUIRE(pos != std::string::npos);
	text.replace(pos, needle.size(), "PowerLimit = " + std::to_string(powerLimit));
	return text;
}

std::string buildRanks(int count)
{
	std::string s = "Rank 1\n  SkillPointsNeededDefault = 0\n  SciencePurchasePointsGranted = 5\nEnd\n";
	for (int r = 2; r <= count; ++r)
	{
		s += "Rank " + std::to_string(r) + "\n  SkillPointsNeededDefault = " + std::to_string(60 * (r - 1)) +
			 "\n  SciencePurchasePointsGranted = 1\nEnd\n";
	}
	return s;
}
} // namespace

TEST_CASE("PORT-43: GameData PowerLimit populates GameLogic + 0x118 in skirmish / LAN / Internet and stays 1000 in SP / tutorial "
		  "(RW 0x6301DD, 0x62A20D, 0x631420 .. 0x631436, 0x625456)")
{
	logictest::LogicWorld lw;
	GameLogic &logic = *lw.logic;

	// Constructor default (RW 0x6301DD): +0x118 = 1000 (0x3E8), unscripted.
	CHECK(logic.getRankLevelLimit() == 1000);
	CHECK_FALSE(logic.isRankLevelLimitScripted());

	// Calling initRankLevelLimit before GameData's economy settings are loaded refuses rather than reading an unloaded 0.
	CHECK_THROWS_AS(logic.initRankLevelLimit(), std::logic_error);

	// Ennorath 1.9.8.2 (data/ini/gamedata.ini:280) and RotWK 2.01 (data\ini\gamedata.ini) both set PowerLimit = 60.
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, logic.economy().settings(), &err), err);
	REQUIRE(logic.economy().settings().powerLimit == 60);

	// RW 0x625456 (Economy::isMultiplayerGame): true for LAN (1), SKIRMISH (2), INTERNET (5) -> +0x118 = PowerLimit (60).
	for (int mode : { EconomyContext::MODE_LAN, EconomyContext::MODE_SKIRMISH, EconomyContext::MODE_INTERNET })
	{
		logic.economy().context().gameMode = mode;
		logic.initRankLevelLimit();
		CHECK(logic.getRankLevelLimit() == 60);
		CHECK_FALSE(logic.isRankLevelLimitScripted());
	}

	// RW 0x625456 is false for SINGLE_PLAYER (0), TUTORIAL (6), REPLAY (3, S-254), SHELL (4) and any other mode value (7) -> +0x118 stays 1000 (RW 0x62A20D).
	for (int mode : std::initializer_list<int>{ EconomyContext::MODE_SINGLE_PLAYER, EconomyContext::MODE_TUTORIAL, EconomyContext::MODE_REPLAY,
			 EconomyContext::MODE_SHELL, 7 })
	{
		logic.economy().context().gameMode = mode;
		logic.initRankLevelLimit();
		CHECK(logic.getRankLevelLimit() == 1000);
		CHECK_FALSE(logic.isRankLevelLimitScripted());
	}

	// A mod overriding GameData PowerLimit (e.g. 85) propagates that exact value in skirmish and resets back to 1000 on GameLogic::reset.
	REQUIRE_MESSAGE(EconomySettings::scan(gameDataWithPowerLimit(85), logic.economy().settings(), &err), err);
	logic.economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	logic.initRankLevelLimit();
	CHECK(logic.getRankLevelLimit() == 85);

	logic.reset();
	CHECK(logic.getRankLevelLimit() == 1000);
	CHECK_FALSE(logic.isRankLevelLimitScripted());
}

TEST_CASE("PORT-43: script action 279 PLAYER_SET_RANKLEVELLIMIT writes GameLogic + 0x118 clamped to >= 1, hashes, and round-trips in obfme:GameLogic v6 "
		  "(RW 0x7CE020 -> 0x7BD1A8 -> 0x7BB04E, RW 0x630D6F)")
{
	using namespace scripttest;

	ScriptGame g;
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, g.lw.logic->economy().settings(), &err), err);
	g.lw.logic->economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	g.lw.logic->initRankLevelLimit();
	REQUIRE(g.lw.logic->getRankLevelLimit() == 60);

	// Unscripted game keeps obfme:GameLogic at version 1 so existing save streams and golden hashes do not shift.
	std::vector<std::uint8_t> unscriptedCore;
	g.lw.logic->gameState().saveBlocks(kCoreBlocks, unscriptedCore);
	CHECK(gameLogicBlockVersion(unscriptedCore) == 1);
	const std::uint32_t unscriptedHash = g.lw.logic->computeStateHash();

	// Execute PLAYER_SET_RANKLEVELLIMIT(25) through the real ScriptEngine action dispatcher (case 279, RW 0x7CE020).
	g.lists[1].items.push_back(item(script("SetLimit25", { { cond("CONDITION_TRUE", {}) } }, { act("PLAYER_SET_RANKLEVELLIMIT", { ip(0, 25) }) }, {}, true)));
	g.start();
	g.frame();
	CHECK(g.engine().stats().unportedActions.empty());
	CHECK(g.lw.logic->getRankLevelLimit() == 25);
	CHECK(g.lw.logic->isRankLevelLimitScripted());
	const std::uint32_t hash25 = g.lw.logic->computeStateHash();
	CHECK(hash25 != unscriptedHash);

	// RW 0x7BB052 .. 0x7BB059: values < 1 clamp to 1 (`cmp eax, 1; jge; mov eax, 1`).
	g.lw.logic->setRankLevelLimit(0);
	CHECK(g.lw.logic->getRankLevelLimit() == 1);
	g.lw.logic->setRankLevelLimit(-17);
	CHECK(g.lw.logic->getRankLevelLimit() == 1);
	const std::uint32_t hash1 = g.lw.logic->computeStateHash();

	g.lw.logic->setRankLevelLimit(42);
	CHECK(g.lw.logic->getRankLevelLimit() == 42);
	const std::uint32_t hash42 = g.lw.logic->computeStateHash();
	CHECK(hash42 != hash1);

	// Full snapshot save/load round-trip preserves scripted limit (v6) and exact state hash.
	std::vector<std::uint8_t> fullSnap;
	REQUIRE_MESSAGE(g.lw.logic->saveSnapshot(fullSnap, &err), err);
	CHECK(gameLogicBlockVersion(fullSnap) == 6);

	ScriptGame loader;
	REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, loader.lw.logic->economy().settings(), &err), err);
	loader.lists[1].items.push_back(
		item(script("SetLimit25", { { cond("CONDITION_TRUE", {}) } }, { act("PLAYER_SET_RANKLEVELLIMIT", { ip(0, 25) }) }, {}, true)));
	loader.start();
	loader.lw.logic->economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	loader.lw.logic->initRankLevelLimit();
	REQUIRE(loader.lw.logic->getRankLevelLimit() == 60);
	REQUIRE_MESSAGE(loader.lw.logic->loadSnapshot(fullSnap, &err), err);
	CHECK(loader.lw.logic->getRankLevelLimit() == 42);
	CHECK(loader.lw.logic->isRankLevelLimitScripted());
	CHECK(loader.lw.logic->computeStateHash() == hash42);

	// Loading an unscripted v1 snapshot into a scripted GameLogic clears the scripted flag and restores the economy mode's limit (60 in skirmish).
	loader.lw.logic->economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	loader.lw.logic->gameState().loadBlocks(kCoreBlocks, unscriptedCore);
	CHECK_FALSE(loader.lw.logic->isRankLevelLimitScripted());
	CHECK(loader.lw.logic->getRankLevelLimit() == 60);

	// Corrupting the saved int32 limit at the tail of a v6 obfme:GameLogic core stream to 0 is rejected by XferException.
	std::vector<std::uint8_t> v6Core;
	g.lw.logic->gameState().saveBlocks(kCoreBlocks, v6Core);
	REQUIRE(gameLogicBlockVersion(v6Core) == 6);
	const std::string randToken = "obfme:Random";
	auto randAt = std::search(v6Core.begin(), v6Core.end(), randToken.begin(), randToken.end());
	REQUIRE(randAt != v6Core.end());
	// Immediately before obfme:Random's 1-byte length prefix sit the 4 little-endian bytes of m_rankLevelLimit (42).
	const size_t limitOffset = (size_t)(randAt - v6Core.begin()) - 1 - 4;
	REQUIRE(v6Core[limitOffset] == 42);
	v6Core[limitOffset] = 0;
	CHECK_THROWS_AS(loader.lw.logic->gameState().loadBlocks(kCoreBlocks, v6Core), XferException);
}

TEST_CASE("PORT-43 retail: LiveGame skirmish load copies gamedata.ini PowerLimit (60) into GameLogic + 0x118 (RW 0x6311ED)")
{
	if (!hudtest::haveWorld("PORT-43 retail skirmish"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig r(s);
	CHECK(r.logic().economy().settings().powerLimit == 60);
	CHECK(r.logic().economy().isMultiplayerGame());
	CHECK(r.logic().getRankLevelLimit() == 60);
	CHECK_FALSE(r.logic().isRankLevelLimitScripted());
}

TEST_CASE("PORT-43 neighbor: PlayerScience max rank and skill-point cap follow MaxLevelMP / MaxLevelSP and +0x28, never PowerLimit or GameLogic + 0x118 "
		  "(RW 0x7827C8, 0x782865, 0x782AA4, 0x6B34EE)")
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	SpellStores stores(keys);
	keys.init();
	stores.install();
	SpellStores::registerBlocks(fx.env.blocks);
	REQUIRE(initest::loadError(fx.env, "ranks.ini", buildRanks(6)).empty());

	PlayerTemplate tpl;
	tpl.m_side = "Men";
	tpl.m_maxLevelMP = 150;
	tpl.m_maxLevelSP = 4;
	ScoreKeeper score;

	// Even when GameData PowerLimit is 2 and PLAYER_SET_RANKLEVELLIMIT sets +0x118 to 2, PlayerScience ignores both (RW 0x6B350B is unreachable in game.dat).
	logictest::LogicWorld lw;
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::scan(gameDataWithPowerLimit(2), lw.logic->economy().settings(), &err), err);
	lw.logic->economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	lw.logic->initRankLevelLimit();
	lw.logic->setRankLevelLimit(2);

	PlayerScience mp;
	mp.bind(&tpl, SpellGameMode{ true, false }, &score);
	mp.reset();
	CHECK(mp.getMaxRankLevel() == 6); // min(rank count 6, MaxLevelMP 150) == 6, NOT 2
	CHECK(mp.setRankLevel(5));
	CHECK(mp.getRankLevel() == 5);
	CHECK(mp.addSkillPoints(1000.0f, false));
	CHECK(mp.getRankLevel() == 6);

	PlayerScience sp;
	sp.bind(&tpl, SpellGameMode{ false, false }, &score);
	sp.reset();
	CHECK(sp.getMaxRankLevel() == 4); // min(rank count 6, MaxLevelSP 4) == 4, NOT 2 and NOT 1000

	// PLAYER_SET_MAX_SPELLPOINTS writes SciencePurchasePointTracker + 0x28 (setRankLevelCap), checked on entry to addSkillPoints (RW 0x782AA4),
	// independent of GameLogic + 0x118.
	mp.reset();
	mp.setRankLevelCap(3);
	for (int i = 0; i < 6; ++i)
	{
		mp.addSkillPoints(60.0f, false);
	}
	CHECK(mp.getRankLevel() == 3);
	CHECK_FALSE(mp.addSkillPoints(60.0f, false));
	CHECK(mp.getRankLevelCap() == 3);
}

TEST_CASE("PORT-43 neighbor: EconomySettings::crc hashes PowerLimit while unscripted GameLogic hash and obfme:GameLogic version stay unchanged")
{
	logictest::LogicWorld a, b;
	std::string err;
	REQUIRE_MESSAGE(EconomySettings::scan(gameDataWithPowerLimit(60), a.logic->economy().settings(), &err), err);
	REQUIRE_MESSAGE(EconomySettings::scan(gameDataWithPowerLimit(61), b.logic->economy().settings(), &err), err);

	// Economy::crc (via EconomySettings::crc) already distinguishes PowerLimit = 60 vs 61 before initRankLevelLimit even runs.
	CHECK(a.logic->computeStateHash() != b.logic->computeStateHash());

	// Calling initRankLevelLimit in skirmish does not shift the unscripted GameLogic state hash or obfme:GameLogic version.
	const std::uint32_t beforeInit = a.logic->computeStateHash();
	a.logic->economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	a.logic->initRankLevelLimit();
	CHECK(a.logic->computeStateHash() == beforeInit);

	std::vector<std::uint8_t> core;
	a.logic->gameState().saveBlocks(kCoreBlocks, core);
	CHECK(gameLogicBlockVersion(core) == 1);
}
