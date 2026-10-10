// OpenBFME unit tests. GPL-3.0.
// Lane END-1: the end of a skirmish game. The ScoreKeeper formula (RW 0x79DFFA / 0x79DF0E / 0x79F704), the GameData rows it reads, and whole retail games on
// Evendim where one side is destroyed: the victory rules, the statistics, the human player's win / loss scripts (Multiplayer_Human), the score screen data, two
// peers of one game, and a second game after the first.

#include "doctest.h"

#include "EndTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/Scripts.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/VictoryConditions.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace endtest;

namespace
{
GameLogicSettings retailMultipliers()
{
	// the values of RotWK 2.01 data\ini\gamedata.ini (the retail test below reads them from the archive)
	GameLogicSettings s;
	s.score.unitsBuilt = 0.0f;
	s.score.unitsDestroyed = 0.0f;
	s.score.structuresBuilt = 10.0f;
	s.score.structuresDestroyed = 500.0f;
	s.score.heroesVetted = 0.0f;
	s.score.unitsVetted = 0.0f;
	s.score.objectivesCompleted = 2000.0f;
	s.score.suppliesCollected = 1.0f;
	s.score.skillPoints = 2.0f;
	s.score.powerPoints = 500.0f;
	s.score.regionCommandPoints = 10.0f;
	s.score.regionResources = 100.0f;
	s.score.regionPowerPoints = 1000.0f;
	s.score.timeTakenMultiplier = 10.0f;
	s.score.timeTakenMaximumScore = 1000.0f;
	s.score.timeTakenMinimumScore = 0.0f;
	s.score.playerEliminated = 0.25f;
	return s;
}

} // namespace

TEST_CASE("end1 score: calculateScore is RW 0x79DFFA (SSE products, every partial sum truncated) with the time-taken term of RW 0x79DF0E")
{
	const GameLogicSettings s = retailMultipliers();
	ScoreKeeper k;
	k.reset(0);
	ScoreKeeper::Totals t;
	t.moneyEarned = 1234;
	t.unitsBuilt = 7;
	t.structuresBuilt = 3;
	t.purchasePoints = 5;
	t.unitsDestroyed[1] = 4;
	t.structuresDestroyed[1] = 2;
	t.unitsDestroyed[0] = 9;      // our own index is skipped (RW 0x79E0D9)
	t.structuresDestroyed[0] = 9;
	k.restoreTotals(t);
	// 10 minutes (3000 frames): 1000 - 10 * 10 = 900; 1234 + 0 + 30 + 2500 + 0 + 900 + 2 * 500
	CHECK(ScoreKeeper::timeTakenScore(s, 3000) == 900);
	CHECK(k.computeScore(s, 3000) == 1234 + 30 + 2500 + 900 + 1000);
	// the end frame (+ 0xF0) replaces the current frame
	t.endFrame = 300 * 5 * 60; // 300 minutes: 1000 - 3000 < 0 = the minimum 0
	k.restoreTotals(t);
	CHECK(ScoreKeeper::timeTakenScore(s, t.endFrame) == 0);
	CHECK(k.computeScore(s, 99) == 1234 + 30 + 2500 + 1000);
	// minutes are frames / 5 (unsigned) / 60 (signed): 299 frames are 0 minutes
	CHECK(ScoreKeeper::timeTakenScore(s, 299) == 1000);
	CHECK(ScoreKeeper::timeTakenScore(s, 300) == 990);
	// truncation of each partial sum: 0.6 supplies per coin of 3 coins = 1.8 -> 1, then + 1 * 0.6 = 1.6 -> 1
	GameLogicSettings f = retailMultipliers();
	f.score.timeTakenMaximumScore = 0.0f;
	f.score.objectivesCompleted = 0.0f;
	f.score.suppliesCollected = 0.6f;
	f.score.unitsBuilt = 0.6f;
	ScoreKeeper k2;
	k2.reset(0);
	ScoreKeeper::Totals t2;
	t2.moneyEarned = 3;
	t2.unitsBuilt = 1;
	k2.restoreTotals(t2);
	CHECK(k2.computeScore(f, 0) == 1);
}

TEST_CASE("end1 score: the screen type is 3 for a LAN session whatever the logic's mode says, and the GameData score defaults are the binary's")
{
	CHECK(ScoreScreenData::typeFor(2, false) == 2);
	CHECK(ScoreScreenData::typeFor(2, true) == 3); // a real LAN session runs with the skirmish mode: type 3 from the session
	CHECK(ScoreScreenData::typeFor(1, false) == 3);
	CHECK(ScoreScreenData::typeFor(5, false) == 4);
	// RW 0x643D31 / 0x643D42 (rep stosd of 1.0f over the twenty floats) and RW 0x643D24 / 0x643D48 (PlayerEliminatedMultiplier = the float at RW 0xBD83D4 = 0.1f)
	const GameLogicSettings d;
	CHECK(d.score.unitsBuilt == 1.0f);
	CHECK(d.score.timeTakenMaximumScore == 1.0f);
	CHECK(d.score.skillPoints == 1.0f);
	CHECK(d.score.playerEliminated == 0.1f);
}

TEST_CASE("end1 stops: S-1061 / S-1062 are in the victory system's stops once, S-1060 / S-1063 / S-1064 in the client end of the game")
{
	auto count = [](const std::vector<std::string> &lines, const std::string &id) {
		return std::count_if(lines.begin(), lines.end(), [&](const std::string &l) { return l.rfind("[" + id + "]", 0) == 0; });
	};
	const std::vector<std::string> v = VictoryConditions::stops();
	CHECK(count(v, "S-344") == 1);
	CHECK(count(v, "S-1061") == 1);
	CHECK(count(v, "S-1062") == 1);
	const std::vector<std::string> c = EndGameController::stopLines();
	CHECK(c.size() == 4);
	CHECK(count(c, "S-1921") == 1); // lane PLAY-1: the end-game timer
	CHECK(count(c, "S-1060") == 1);
	CHECK(count(c, "S-1063") == 1);
	CHECK(count(c, "S-1064") == 1);
}

TEST_CASE("end1 score: the score screen graph's axes are RW 0x9257F2's (the y step from the digits of the highest value, x labels from the sample count)")
{
	ScoreScreenData d;
	d.valid = true;
	ScoreScreenData::Entry e;
	for (int f = 0; f < 1500; ++f)
	{
		ScoreKeeper::PerFrameStats st;
		st.recorded = true;
		st.money = (std::uint32_t)(f * 3);
		st.unitsAlive = (std::uint16_t)(f / 100);
		st.score = (float)f * 2.5f;
		e.perFrame.push_back(st);
	}
	d.entries.push_back(e);
	const std::string formats[3] = { "m:s", "h:m", "d h:m" };
	// money: max 4497 -> "4497" has 4 digits, p = 10^2, step = ((44 + 1) * 100) / 10 = 450, the axis top 4500
	ScoreScreenData::Axis a = d.axis(2, formats);
	CHECK(a.samples == 1500);
	CHECK(a.step == 450);
	CHECK(a.top == 4500.0f);
	CHECK(a.yLabels.size() == 11);
	CHECK(a.yLabels[10] == "4500");
	// x: label i is frame (1500 + 0.5) * i * 0.1 -> seconds = frame / 5; 1500 frames = 300 s = "05:00" at i = 10
	CHECK(a.xLabels[0] == "00:00");
	CHECK(a.xLabels[10] == "05:00");
	CHECK(a.totalTime == "05:00");
	CHECK(a.timeFormat == 0);
	// units: max 14 -> 2 digits -> p = 10, step = ((1 + 1) * 10) / 10 = 2
	a = d.axis(0, formats);
	CHECK(a.step == 2);
	CHECK(ScoreScreenData::graphMode("Units") == 0);
	CHECK(ScoreScreenData::graphMode("Structures") == 1);
	CHECK(ScoreScreenData::graphMode("Resources") == 2);
	CHECK(ScoreScreenData::graphMode("anything") == 4);
	CHECK(ScoreScreenData::formatTime("h:m:s", 3725) == "01:02:05");
	CHECK(ScoreScreenData::formatTime("d", 200000) == "2");
}

TEST_CASE("end1 retail: GameData's ScoreKeeper rows (RW 0xC00F00 .. 0xC01040) and the human script library Multiplayer_Human")
{
	OPENBFME_REQUIRE_START(s);
	const GameLogicSettings &g = s->settings;
	CHECK((bool)g.objectsThatScore);
	CHECK(g.score.structuresBuilt == 10.0f);
	CHECK(g.score.structuresDestroyed == 500.0f);
	CHECK(g.score.objectivesCompleted == 2000.0f);
	CHECK(g.score.suppliesCollected == 1.0f);
	CHECK(g.score.skillPoints == 2.0f);
	CHECK(g.score.powerPoints == 500.0f);
	CHECK(g.score.timeTakenMultiplier == 10.0f);
	CHECK(g.score.timeTakenMaximumScore == 1000.0f);
	CHECK(g.score.normalVictoryRequiredScore == 10000.0f);
	CHECK(g.score.playerEliminated == 0.25f);
	CHECK(g.unappliedFields.count("GameData.ScoreKeeper_UnitsBuiltMultiplier") == 0);
	// the library the binary gives every human side (RW 0x731C69): its win / loss scripts are the data's
	EndGameScripts scripts;
	std::string error;
	REQUIRE_MESSAGE(scripts.load(*s->mount->fs, "Multiplayer_Human", &error), error);
	CHECK(scripts.scriptCount() >= 3);
	EndGameView view;
	view.localPlayerIndex = 1;
	view.frame = 10;
	EndGameScripts::Actions a = scripts.update(view, 10);
	CHECK_FALSE(a.victory);
	CHECK_FALSE(a.defeat);
	view.alliedVictory = true;
	a = scripts.update(view, 11);
	CHECK(a.victory);
	a = scripts.update(view, 12); // "Multiplayer Win" is one-shot
	CHECK_FALSE(a.victory);
	// every condition / action the port does not answer is counted, never silently true
	MESSAGE("unported: " << scripts.unported().size());
}

TEST_CASE("end1 retail: a skirmish on Evendim where one side is destroyed: statistics, victory, the end sequence and the score screen of both peers, then a second game")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage message = humanVersus(*s, "FactionMen", "FactionMordor", 77u);
	Peer peers[2];
	peers[0].load(*s, message, 0);
	peers[1].load(*s, message, 1);
	Player *gondor[2] = { playerAtStart(peers[0].logic(), 0), playerAtStart(peers[1].logic(), 0) };
	Player *mordor[2] = { playerAtStart(peers[0].logic(), 1), playerAtStart(peers[1].logic(), 1) };
	for (int k = 0; k < 2; ++k)
	{
		REQUIRE(gondor[k] != nullptr);
		REQUIRE(mordor[k] != nullptr);
	}
	CHECK(peers[0].logic().players().getLocalPlayer() == gondor[0]);
	CHECK(peers[1].logic().players().getLocalPlayer() == mordor[1]);
	double now = 0.0;
	// 40 frames of play: the counting is on (RW 0x6B197E), the starting objects are alive but not "built"
	for (int f = 0; f < 40; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
		CHECK(peers[0].logic().computeStateHash() == peers[1].logic().computeStateHash());
	}
	const ScoreKeeper &gk = gondor[0]->getScoreKeeper();
	const ScoreKeeper &mk = mordor[0]->getScoreKeeper();
	CHECK(gk.counting());
	CHECK(gk.playerIndex() == gondor[0]->getPlayerIndex());
	CHECK(gk.structuresAlive() > 0);
	CHECK(gk.unitsBuilt() == 0); // the starting units were made before counting began
	CHECK(gk.perFrameStats().size() == (size_t)peers[0].logic().getFrame() + 1);
	CHECK(gk.perFrameStats().back().recorded);
	CHECK(gk.perFrameStats().back().money == gondor[0]->getMoney()->countMoney());
	CHECK(gk.perFrameStats().back().structuresAlive == (std::uint16_t)gk.structuresAlive());
	const int mordorStructures = mk.structuresAlive();
	const int mordorUnits = mk.unitsAlive();
	CHECK(mordorStructures > 0);
	MESSAGE("Mordor at frame " << peers[0].logic().getFrame() << ": structures alive " << mordorStructures << ", units alive " << mordorUnits << ", money " << mordor[0]->getMoney()->countMoney());
	// Gondor's fortress destroys everything Mordor has, in both peers at the same frame (a deterministic input)
	for (int k = 0; k < 2; ++k)
	{
		Object *source = firstObjectOf(peers[k].logic(), *gondor[k], true);
		REQUIRE(source != nullptr);
		CHECK(destroyAll(peers[k].logic(), *mordor[k], *source) > 0);
	}
	unsigned defeatFrame = 0;
	for (int f = 0; f < 60; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
		REQUIRE(peers[0].logic().computeStateHash() == peers[1].logic().computeStateHash());
		if (!defeatFrame && mordor[0]->isDefeated())
		{
			defeatFrame = peers[0].logic().getFrame();
		}
	}
	REQUIRE(defeatFrame > 0);
	const VictoryConditions &v = static_cast<const GameLogic &>(peers[0].logic()).victory();
	CHECK(v.singleAllianceRemaining());
	CHECK(v.endFrame() == defeatFrame);
	CHECK(mordor[0]->getDefeatFrame() == defeatFrame);
	CHECK(mk.endFrame() == defeatFrame);  // Player + 0x4CC is ScoreKeeper + 0xF0
	CHECK(mordor[0]->getMoney()->countMoney() == 0); // killPlayer withdraws everything (RW 0x6ABF57)
	CHECK(mk.structuresLost() == mordorStructures);
	CHECK(gk.totalStructuresDestroyed() == mk.structuresLost());
	CHECK(gk.structuresDestroyed(mordor[0]->getPlayerIndex()) == mordorStructures);
	CHECK(mk.lastAttackerIndex() == gondor[0]->getPlayerIndex());
	CHECK(mk.structuresAlive() == 0);
	CHECK_FALSE(peers[0].logic().economy().context().scoring); // the keep-score switch clears at the single alliance (RW 0x80903F)
	// the elimination bonus (RW 0x6ABF9E: the loser's newest per-frame score * 0.25 to its last attacker) is behind the keep-score switch, which the single
	// alliance cleared earlier in the same update (RW 0x80903F before the loop at RW 0x809047): in a game of two it is never paid
	CHECK(gk.eliminationBonus() == 0.0f);
	CHECK(mk.perFrameStats().back().score > 0.0f);
	// the stats stop for the dead player and go on for the living one
	const unsigned frame = peers[0].logic().getFrame();
	CHECK(gk.perFrameStats().size() == (size_t)frame + 1);
	CHECK(mk.perFrameStats().size() == (size_t)defeatFrame); // the defeat frame's own entry is not written (the player is dead at the end of that frame)
	// the end sequence: the winner's script runs VICTORY, the loser was shown the defeat at once (VictoryConditions' local defeat) and its script does not
	// show a second end screen
	CHECK(peers[0].requested(EndGameRequest::SHOW_END_GAME, "APT:EndVictorious"));
	CHECK(peers[0].requested(EndGameRequest::EVA, std::string()));
	CHECK(peers[0].requested(EndGameRequest::MESSAGE, "GUI:PlayerHasBeenDefeated"));
	CHECK(peers[0].end.victoryScreen());
	CHECK(peers[1].requested(EndGameRequest::SHOW_END_GAME, "APT:EndDefeat"));
	CHECK_FALSE(peers[1].requested(EndGameRequest::SHOW_END_GAME, "APT:EndVictorious"));
	CHECK_FALSE(peers[1].end.victoryScreen());
	for (const EndGameRequest &r : peers[0].requests)
	{
		if (r.kind == EndGameRequest::SHOW_END_GAME)
		{
			CHECK(r.sound == "Gui_VictoryScreen");
			CHECK(r.cheer == "Gui_VictoryCheerGood"); // Men are good
			CHECK_FALSE(r.evil);
		}
		if (r.kind == EndGameRequest::EVA)
		{
			CHECK(r.eva == 7); // EnemyDefeated
		}
	}
	// lane PLAY-1: VICTORY (RW 0x7C45E0) and DEFEAT (RW 0x7BF15E) start the script engine's end-game timer (RW 0x602FFE: 25 logic frames); when it runs out
	// the game is left to the score screen (RW 0x603533: MSG_CLEAR_GAME_DATA). The loser's DEFEAT shows no second screen but still starts the timer
	for (int f = 0; f < 30; ++f)
	{
		now += 200.0 / 30.0; // the logic frames of the test are faster than the end screen's 7 s of real time
		peers[0].step(now);
		peers[1].step(now);
	}
	for (int k = 0; k < 2; ++k)
	{
		CHECK(std::count_if(peers[k].requests.begin(), peers[k].requests.end(), [](const EndGameRequest &r) { return r.kind == EndGameRequest::CLEAR_GAME_DATA; }) == 1);
		CHECK(peers[k].end.endGameTimer() == 0);
	}
	const unsigned victoryShown = peers[0].frameOf(EndGameRequest::SHOW_END_GAME, "APT:EndVictorious");
	REQUIRE(victoryShown > 0);
	CHECK(peers[0].frameOf(EndGameRequest::CLEAR_GAME_DATA, std::string()) == victoryShown + (unsigned)EndGameController::kEndGameTimerFrames);
	CHECK(peers[1].frameOf(EndGameRequest::CLEAR_GAME_DATA, std::string()) >= peers[1].frameOf(EndGameRequest::SHOW_END_GAME, "APT:EndDefeat") + 25u);
	// 7 s of real time later the end screen hides and the sound fades towards the score screen (RW 0x808A31 / 0x808D1B)
	now += EndGameController::kEndGameMs + 1.0;
	peers[0].end.update(nullptr, peers[0].lastFrame, now);
	peers[1].end.update(nullptr, peers[1].lastFrame, now);
	for (int k = 0; k < 2; ++k)
	{
		for (EndGameRequest &r : peers[k].end.takeRequests())
		{
			peers[k].requests.push_back(r);
		}
		CHECK(peers[k].requested(EndGameRequest::HIDE_END_GAME, std::string()));
		CHECK(peers[k].requested(EndGameRequest::TRANSITION, "MPorSkirmishFadeToScoreScreen"));
		CHECK(peers[k].end.fadeToScore());
		CHECK_FALSE(peers[k].end.endGameShowing());
	}
	// the score screen of each peer: the local player first, the same results and scores
	ScoreScreenData d[2];
	for (int k = 0; k < 2; ++k)
	{
		std::vector<int> slots;
		for (const std::string &name : peers[k].game->report().startSlotPlayers)
		{
			const Player *p = name.empty() ? nullptr : peers[k].logic().players().findPlayerWithName(name);
			slots.push_back(p ? p->getPlayerIndex() : -1);
		}
		d[k] = ScoreScreenData::build(peers[k].logic(), 3, slots, {});
		REQUIRE(d[k].entries.size() == 2);
		CHECK(d[k].entries[0].local);
	}
	CHECK(d[0].entries[0].playerIndex == gondor[0]->getPlayerIndex());
	CHECK(d[1].entries[0].playerIndex == mordor[1]->getPlayerIndex());
	CHECK(d[0].entries[0].result == 0);
	CHECK(d[0].entries[1].result == 1);
	CHECK(d[1].entries[0].result == 1);
	CHECK(d[1].entries[1].result == 0);
	CHECK(d[0].entries[0].score == d[1].entries[1].score);
	CHECK(d[0].entries[1].score == d[1].entries[0].score);
	CHECK(d[0].entries[0].perFrame.size() == d[1].entries[1].perFrame.size());
	CHECK(d[0].entries[0].structuresDestroyed == mordorStructures);
	MESSAGE("scores: Gondor " << d[0].entries[0].score << ", Mordor " << d[0].entries[1].score << "; Gondor destroyed " << d[0].entries[0].structuresDestroyed
				<< " structures and " << d[0].entries[0].unitsDestroyed << " units, Mordor lost " << d[0].entries[1].unitsLost << " units");
	// the way back: both games go (their workers, drawables and objects), then a second game starts on the same world and runs
	peers[0].game.reset();
	peers[1].game.reset();
	Peer second;
	second.load(*s, humanVersus(*s, "FactionMordor", "FactionElves", 5u), 0);
	for (int f = 0; f < 30; ++f)
	{
		second.step(now += 200.0);
	}
	CHECK(second.logic().getFrame() >= 30);
	CHECK_FALSE(second.end.endGameShown());
	CHECK(second.logic().players().getLocalPlayer()->getScoreKeeper().perFrameStats().size() == (size_t)second.logic().getFrame() + 1);
}

#include "AptRetail.h"
#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cstdlib>
#include <functional>

// A diagnostic, not a test: prints the ActionScript of a retail movie (OPENBFME_END1_APT=<movie name>, default timeline) with every string and constant operand.
TEST_CASE("end1 diagnostic: dump a movie's ActionScript (OPENBFME_END1_APT=<name>)" * doctest::skip())
{
	OPENBFME_REQUIRE_RETAIL(mount);
	AptArchiveFileSource source(mount.fs);
	AptLoader loader(source);
	const char *env = std::getenv("OPENBFME_END1_APT");
	std::string err;
	std::shared_ptr<const AptFile> f = loader.loadMovie(env ? env : "timeline", &err);
	REQUIRE_MESSAGE((bool)f, err);
	std::function<void(const AptCodeBlock &, int)> dump = [&](const AptCodeBlock &b, int depth) {
		for (const AptInstruction &i : b.instructions)
		{
			std::string line(depth * 2, ' ');
			line += AptActionDecoder::opcodeName(i.opcode);
			if (!i.text.empty())
			{
				line += " '" + i.text + "'";
			}
			for (const AptConstRef &c : i.constants)
			{
				line += " [" + (c.text.empty() ? "t" + std::to_string(c.type) + ":" + std::to_string(c.raw) : "'" + c.text + "'") + "]";
			}
			if (i.intOperand)
			{
				line += " #" + std::to_string(i.intOperand);
			}
			if (i.branchTarget >= 0)
			{
				line += " ->" + std::to_string(i.branchTarget);
			}
			std::printf("%s\n", line.c_str());
			if (i.function && i.function->body)
			{
				std::string params;
				for (const AptFunctionParam &prm : i.function->params)
				{
					params += (params.empty() ? "" : ", ") + prm.name;
				}
				std::printf("%s function %s(%s)\n", std::string(depth * 2, ' ').c_str(), i.function->name.c_str(), params.c_str());
				dump(*i.function->body, depth + 1);
			}
		}
	};
	for (std::uint32_t off : f->programOffsets())
	{
		std::shared_ptr<const AptCodeBlock> b = f->codeAt(off, &err);
		if (b)
		{
			std::printf("== program at %u (Apt version %u)\n", off, b->swfVersion);
			dump(*b, 0);
		}
	}
}
