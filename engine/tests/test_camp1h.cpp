// OpenBFME unit tests: lane CAMP-1H, the campaign's hardening pass. The difficulty bonus of the computer players' objects (Object + 0x45C, RW 0x68B907;
// Player::applyDifficultyBonusesForObject RW 0x6AC32D; initObject RW 0x693D63; OBJECT_ALLOW_BONUSES RW 0x7BD718) and the game difficulty (TheGameLogic
// + 0xA4, prepareNewGame RW 0x77948E). GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>

using namespace hudtest;

namespace
{
const char *const kUpgrades[8] = { "Upgrade_EasyAIMultiPlayer", "Upgrade_MediumAIMultiPlayer", "Upgrade_HardAIMultiPlayer", "Upgrade_BrutalAIMultiPlayer",
	"Upgrade_EasyAISinglePlayer", "Upgrade_MediumAISinglePlayer", "Upgrade_HardAISinglePlayer", "Upgrade_BrutalAISinglePlayer" };

// the one AI difficulty upgrade `o` has ("" none; "several" when more than one)
std::string bonusOf(const Object &o)
{
	std::string found;
	for (const char *u : kUpgrades)
	{
		if (o.hasUpgrade(std::string(u)))
		{
			found = found.empty() ? std::string(u) : std::string("several");
		}
	}
	return found;
}
} // namespace

TEST_CASE("camp1h difficulty bonus: a computer player's new object gets its AI difficulty upgrade by game mode; humans, non-playable sides and a "
		  "disabled script engine flag get none")
{
	if (!haveWorld("camp1h difficulty bonus"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig r(s);
	GameLogic &logic = r.logic();
	Player *ai = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(ai != nullptr);
	REQUIRE(ai->getPlayerType() == PLAYER_COMPUTER);
	REQUIRE(ai->getPlayerTemplate() != nullptr);
	REQUIRE(ai->getPlayerTemplate()->m_playableSide);
	CHECK(logic.getGameDifficulty() == 1);                     // GameLogic::reset RW 0x62D3EB
	CHECK(logic.scriptEngine().objectsReceiveDifficultyBonus()); // ScriptEngine::reset RW 0x609857

	// a multiplayer game (RW 0x625456): the AI's difficulty (+ 0x30: the skirmish level), the MultiPlayer upgrades
	logic.economy().context().gameMode = EconomyContext::MODE_SKIRMISH;
	ai->setSkirmishDifficulty(2);
	Object *hard = r.make("MordorLumberMill", 1200, 1200, ai);
	CHECK(hard->isReceivingDifficultyBonus());
	CHECK(bonusOf(*hard) == "Upgrade_HardAIMultiPlayer");
	ai->setSkirmishDifficulty(3);
	CHECK(bonusOf(*r.make("MordorLumberMill", 1400, 1200, ai)) == "Upgrade_BrutalAIMultiPlayer");
	// an AI without a skirmish level answers the script engine's difficulty (an AIPlayer, RW 0x8F7FEB): 1
	ai->setSkirmishDifficulty(-1);
	CHECK(bonusOf(*r.make("MordorLumberMill", 1600, 1200, ai)) == "Upgrade_MediumAIMultiPlayer");

	// a human player's object: the flag is set (RW 0x693D63), the bonus is not given (RW 0x68B68A)
	Object *mine = r.make("MordorLumberMill", 1200, 1600);
	CHECK(mine->isReceivingDifficultyBonus());
	CHECK(bonusOf(*mine).empty());
	// the neutral player's template is not a PlayableSide (+ 0x151)
	Player *neutral = r.game->players().getNeutralPlayer();
	REQUIRE(neutral != nullptr);
	if (neutral->getPlayerTemplate() == nullptr || !neutral->getPlayerTemplate()->m_playableSide)
	{
		CHECK(bonusOf(*r.make("MordorLumberMill", 1600, 1600, neutral)).empty());
	}

	// not a multiplayer game (a campaign mission): TheGameLogic + 0xA4, the SinglePlayer upgrades
	logic.economy().context().gameMode = EconomyContext::MODE_SINGLE_PLAYER;
	ai->setSkirmishDifficulty(2); // ignored here
	logic.setGameDifficulty(0);
	CHECK(bonusOf(*r.make("MordorLumberMill", 1800, 1200, ai)) == "Upgrade_EasyAISinglePlayer");
	logic.setGameDifficulty(2);
	CHECK(bonusOf(*r.make("MordorLumberMill", 2000, 1200, ai)) == "Upgrade_HardAISinglePlayer");
	// a difficulty outside 0 .. 3 names no upgrade
	logic.setGameDifficulty(4);
	CHECK(bonusOf(*r.make("MordorLumberMill", 2200, 1200, ai)).empty());
	logic.setGameDifficulty(1);

	// the script engine's + 0x1A5D5 off: new objects do not receive it; turning an object's flag off takes nothing back (RW 0x6AC32D returns at once)
	logic.scriptEngine().setObjectsReceiveDifficultyBonus(false);
	Object *later = r.make("MordorLumberMill", 2400, 1200, ai);
	CHECK_FALSE(later->isReceivingDifficultyBonus());
	CHECK(bonusOf(*later).empty());
	const std::uint32_t before = logic.computeStateHash();
	hard->setReceivingDifficultyBonus(false);
	CHECK_FALSE(hard->isReceivingDifficultyBonus());
	CHECK(bonusOf(*hard) == "Upgrade_HardAIMultiPlayer");
	CHECK(logic.computeStateHash() != before); // + 0x45C is hashed
	// and on again: the bonus of the current mode and difficulty
	later->setReceivingDifficultyBonus(true);
	CHECK(bonusOf(*later) == "Upgrade_MediumAISinglePlayer");
	logic.scriptEngine().setObjectsReceiveDifficultyBonus(true);
}

TEST_CASE("camp1h difficulty: the game difficulty and the bonus flag are hashed; the script engine's reset restores both script values")
{
	if (!haveWorld("camp1h difficulty hash"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig r(s);
	GameLogic &logic = r.logic();
	const std::uint32_t h0 = logic.computeStateHash();
	logic.setGameDifficulty(2);
	const std::uint32_t h1 = logic.computeStateHash();
	CHECK(h1 != h0);
	logic.scriptEngine().setObjectsReceiveDifficultyBonus(false);
	logic.scriptEngine().setGameDifficulty(0);
	// the script engine is hashed when the map has scripts (GameLogic::hashState): only a script action changes the flag
	CHECK((logic.computeStateHash() != h1) == logic.scriptEngine().loaded());
	logic.scriptEngine().reset(); // RW 0x609685: + 0x1A5C4 = 1, + 0x1A5D5 = 1
	CHECK(logic.scriptEngine().gameDifficulty() == 1);
	CHECK(logic.scriptEngine().objectsReceiveDifficultyBonus());
	logic.setGameDifficulty(1);
}

// ---- TheVideoPlayer (GameClient/VideoPlayer.h) -----------------------------------------------------------------------------------------------------

#include "Common/Audio/AudioIni.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/LinearCampaign.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <cstdlib>

TEST_CASE("camp1h VideoPlayer: the Video block (RW 0xCFD2E8), a redefinition replaces, the lookup ignores case; the EA VP6 header")
{
	VideoPlayer vp;
	INIEnvironment env;
	vp.registerBlock(env.blocks);
	INI ini(env);
	const std::string text = "Video Angmar_Campaign_M4Open\n  Filename = BAM4O\n  Comment = \"intro\"\nEnd\n"
							 "Video Second\n  Filename = X\n  HasSubtitles = Yes\n  Volume = 50%\n  IsDefault = Yes\nEnd\n"
							 "Video Second\n  Filename = Y\nEnd\n";
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("Data\\INI\\Video.ini", bytes, INI_LOAD_OVERWRITE);
	REQUIRE(vp.videos().size() == 2);
	const Video *v = vp.getVideo("angmar_campaign_m4open");
	REQUIRE(v != nullptr);
	CHECK(v->filename == "BAM4O");
	CHECK(v->comment == "intro");
	const Video *second = vp.getVideo("Second");
	REQUIRE(second != nullptr);
	CHECK(second->filename == "Y");     // replaced whole (donor RW 0x689FD0)
	CHECK_FALSE(second->hasSubtitles);
	CHECK(vp.getVideo("Third") == nullptr);
	VideoStreamInfo none = vp.locate("Third", "/nonexistent", "", "English");
	CHECK_FALSE(none.ok);
	CHECK(none.error.find("can't be found") != std::string::npos);
	VideoStreamInfo missing = vp.locate("Second", "/nonexistent", "", "English");
	CHECK_FALSE(missing.ok);
	CHECK(missing.error.find("Could not open VP6 video file") != std::string::npos);

	// "MVhd", 0x20, "vp60", 704 x 400, 1600 frames, the largest chunk, 983010 / 32767 frames per second
	const std::uint8_t head[32] = { 'M', 'V', 'h', 'd', 0x20, 0, 0, 0, 'v', 'p', '6', '0', 0xC0, 0x02, 0x90, 0x01, 0x40, 0x06, 0, 0, 0xEC, 0x51, 0x01, 0,
		0xE2, 0xFF, 0x0E, 0x00, 0xFF, 0x7F, 0, 0 };
	VideoStreamInfo h;
	std::string err;
	REQUIRE(VideoPlayer::readHeader(head, sizeof head, h, &err));
	CHECK(h.width == 704);
	CHECK(h.height == 400);
	CHECK(h.frames == 1600);
	CHECK(h.durationMs == doctest::Approx(1600.0 * 1000.0 * 32767.0 / 983010.0));
	std::uint8_t bad[32];
	std::copy(head, head + 32, bad);
	bad[0] = 'X';
	CHECK_FALSE(VideoPlayer::readHeader(bad, sizeof bad, h, &err));
	CHECK_FALSE(VideoPlayer::readHeader(head, 16, h, &err));
}

TEST_CASE("camp1h VideoPlayer: retail - every Angmar campaign movie is found in Data\\Movies and plays its narration event (RW 0x49112C)")
{
	if (!haveWorld("camp1h retail movies"))
	{
		return;
	}
	const char *root = std::getenv("ROTWK_INSTALL");
	REQUIRE(root != nullptr);
	SharedWorld &s = shared();
	VideoPlayer vp;
	INIEnvironment env;
	env.fileSystem = s.mount->fs.get();
	vp.registerBlock(env.blocks);
	INI ini(env);
	for (const std::string &f : VideoPlayer::loadOrder())
	{
		ini.load(f, INI_LOAD_OVERWRITE);
	}
	const AudioIniState &audio = s.world->audio();
	auto known = [&audio](const std::string &e) { return audio.infos.find(e) != nullptr; };
	int movies = 0;
	for (const LinearCampaign &c : s.world->linearCampaigns().campaigns())
	{
		std::vector<std::string> titles = { c.overallIntroMovie };
		for (const LinearCampaignMission &m : c.missions)
		{
			titles.push_back(m.introMovie);
		}
		for (const std::string &t : titles)
		{
			if (t.empty())
			{
				continue;
			}
			const VideoStreamInfo info = vp.open(t, root, "", "English", known);
			INFO(t << ": " << info.error);
			CHECK(info.ok);
			CHECK(info.durationMs > 1000.0);
			CHECK_FALSE(info.audioEvents.empty()); // the DialogEvent of the movie's file (speech.ini, SubmixSlider = Movie)
			++movies;
		}
	}
	CHECK(movies >= 9);
	// MAP ANG Amon Sul's intro (LinearCampaignExpansion1.ini: IntroMovie = Angmar_Campaign_M4Open): BAM4O.vp6, 1600 frames at 30 per second
	const VideoStreamInfo m4 = vp.open("Angmar_Campaign_M4Open", root, "", "English", known);
	REQUIRE(m4.ok);
	CHECK(m4.frames == 1600);
	CHECK(m4.durationMs == doctest::Approx(53333.333));
	CHECK(m4.audioEvents == std::vector<std::string>{ "BAM4O" });
}

// ---- the ModifierList value through a macro (RW 0x806264) -----------------------------------------------------------------------------------------

#include "GameLogic/AttributeModifiers.h"

TEST_CASE("camp1h ModifierList: a value given by a percent macro is a percentage (RW 0x8062A8 expands the token before looking for '%')")
{
	AttributeModifierStore store;
	INIEnvironment env;
	store.registerBlock(env.blocks);
	INI ini(env);
	const std::string text = "#define HALF 50%\n#define PLAIN 3\n"
							 "ModifierList CAMP1H_A\n  Category = LEVEL\n  Modifier = ARMOR HALF\n  Modifier = DAMAGE_MULT 120%\n  Modifier = DAMAGE_ADD PLAIN\n  Duration = 0\nEnd\n";
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	AttributeModifierStore *previous = TheAttributeModifierStore;
	TheAttributeModifierStore = &store;
	ini.loadMemory("Data\\INI\\AttributeModifier.ini", bytes, INI_LOAD_OVERWRITE);
	TheAttributeModifierStore = previous;
	const ModifierListTemplate *l = store.find("CAMP1H_A");
	REQUIRE(l != nullptr);
	float v = 0.0f;
	REQUIRE(l->value(ATTRIBUTE_ARMOR, nullptr, v));
	CHECK(v == doctest::Approx(0.5f));
	REQUIRE(l->value(ATTRIBUTE_DAMAGE_MULT, nullptr, v));
	CHECK(v == doctest::Approx(1.2f));
	REQUIRE(l->value(ATTRIBUTE_DAMAGE_ADD, nullptr, v));
	CHECK(v == doctest::Approx(3.0f));
}

TEST_CASE("camp1h ModifierList: retail - the AI difficulty bonuses and the stonework armor read as gamedata.ini's percentages")
{
	if (!haveWorld("camp1h retail modifier lists"))
	{
		return;
	}
	SharedWorld &s = shared();
	const AttributeModifierStore &store = s.world->attributeModifiers();
	auto damage = [&](const char *list) {
		const ModifierListTemplate *l = store.find(list);
		float v = -1.0f;
		REQUIRE_MESSAGE(l != nullptr, list);
		REQUIRE(l->value(ATTRIBUTE_DAMAGE_MULT, nullptr, v));
		return v;
	};
	CHECK(damage("EasyAISinglePlayer_Bonus") == doctest::Approx(0.25f));  // EASY_AI_SINGLE_PLAYER_DAMAGE_MULT 25%
	CHECK(damage("MediumAISinglePlayer_Bonus") == doctest::Approx(1.0f));
	CHECK(damage("HardAISinglePlayer_Bonus") == doctest::Approx(1.2f));    // HARD_AI_SINGLE_PLAYER_DAMAGE_MULT 120%
	CHECK(damage("MediumAIMultiPlayer_Bonus") == doctest::Approx(1.0f));
	CHECK(damage("HardAIMultiPlayer_Bonus") == doctest::Approx(1.0f));
}
