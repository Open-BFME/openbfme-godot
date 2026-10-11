// OpenBFME unit tests. GPL-3.0.
// lane RULES-1: the game rules (Common/MpGameRules.h) against RotWK's tables, the rules in the game setup's wire form and replay header, the rules at the
// game start (start money RW 0x6B0578, command point cap RW 0x6A7DE2, the "ringheroes" game mode RW 0x7E9E83) and OpenBFME's Legendary Heroes (scaled
// heroes, the state hash, two identical AI-vs-AI runs). The lobby's Rules tab is tested with the retail movie in test_apt_skirmish.cpp.

#include "doctest.h"

#include "PeImage.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/MpGameRules.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Damage.h"
#include "GameLogic/LegendaryHeroesGame.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/ScriptEngine/Scripts.h"
#include "GameLogic/SimMath.h"
#include "GameNetwork/NetPacket.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using retailtest::PeImage;

TEST_CASE("rules1: RotWK's rule tables: the per-mode counts, the defaults (RW 0x960A74), the labels (RW 0x960B14)")
{
	CHECK(MpGameRules::numComboBoxes(RULES_MODE_SKIRMISH) == 2);
	CHECK(MpGameRules::numCheckBoxes(RULES_MODE_SKIRMISH) == 3);
	CHECK(MpGameRules::numComboBoxes(RULES_MODE_WOTR) == 5); // RotWK added VictoryType (BFME2 1.06: 4)
	CHECK(MpGameRules::numCheckBoxes(RULES_MODE_WOTR) == 2);
	CHECK(MpGameRules::numComboBoxes(7) == 0);
	CHECK(MpGameRules::comboBoxes(RULES_MODE_NONE) == nullptr);

	const GameRules d = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	const GameRules expected = { 1, 0, 1, 100, 1000, -1, -1, -1, -1, -1 };
	CHECK(d == expected);
	const GameRules wotr = MpGameRules::defaults(RULES_MODE_WOTR);
	const GameRules expectedWotr = { 1, -1, 1, -1, -1, 0, 0, 0, 0, 0 };
	CHECK(wotr == expectedWotr);
	CHECK(MpGameRules::isNone(MpGameRules::defaults(RULES_MODE_NONE)));
	// a demo mode (GlobalData + 0x9D4 = 1 or 2) forces AllowCustomHeroes off; RotWK 2.01's is 0
	CHECK(MpGameRules::kDemoMode == 0);
	CHECK(MpGameRules::defaults(RULES_MODE_SKIRMISH, 1)[RULE_ALLOW_CUSTOM_HEROES] == 0);
	CHECK(MpGameRules::defaults(RULES_MODE_SKIRMISH, 2)[RULE_ALLOW_CUSTOM_HEROES] == 0);
	CHECK(MpGameRules::defaults(RULES_MODE_SKIRMISH, 0)[RULE_ALLOW_CUSTOM_HEROES] == 1);
	CHECK(MpGameRules::defaults(RULES_MODE_NONE, 1)[RULE_ALLOW_CUSTOM_HEROES] == -1);
	CHECK(MpGameRules::countDifferences(d, expected) == 0);
	CHECK(MpGameRules::countDifferences(d, wotr) == 8);

	CHECK(std::string(MpGameRules::ruleLabel(RULE_INITIAL_RESOURCES)) == "RULE:InitialResources");
	CHECK(std::string(MpGameRules::ruleLabel(RULE_VICTORY_TYPE)) == "RULE:VictoryType"); // BFME2: RULE:MapRevealMode
	CHECK(MpGameRules::ruleLabel(10) == nullptr);
	CHECK(MpGameRules::ruleLabel(-1) == nullptr);

	const MpRuleCombo *cash = MpGameRules::findCombo(RULES_MODE_SKIRMISH, RULE_INITIAL_RESOURCES);
	REQUIRE(cash);
	CHECK(cash->count == 11);
	CHECK(cash->choices[0].value == 500);
	CHECK(cash->choices[10].value == 4000);
	const MpRuleCombo *cp = MpGameRules::findCombo(RULES_MODE_SKIRMISH, RULE_COMMAND_POINT_FACTOR);
	REQUIRE(cp);
	CHECK(cp->count == 7);
	CHECK(std::string(cp->choices[0].label) == "VALUE:ThirdX");
	CHECK(cp->choices[6].value == 10000);
	CHECK(MpGameRules::findCheck(RULES_MODE_SKIRMISH, RULE_CLAN_GAME) != nullptr);
	CHECK(MpGameRules::findCheck(RULES_MODE_WOTR, RULE_CLAN_GAME) == nullptr);
}

TEST_CASE("rules1: OpenBFME's Legendary Heroes factors")
{
	CHECK(LegendaryHeroes::valid(1));
	CHECK(LegendaryHeroes::valid(4));
	CHECK_FALSE(LegendaryHeroes::valid(0));
	CHECK_FALSE(LegendaryHeroes::valid(5));
	CHECK(LegendaryHeroes::damageDealtFactor(1) == 1.0f);
	CHECK(LegendaryHeroes::damageDealtFactor(2) == 7.0f / 6.0f);
	CHECK(LegendaryHeroes::damageDealtFactor(3) == 4.0f / 3.0f);
	CHECK(LegendaryHeroes::damageDealtFactor(4) == 1.5f);
	CHECK(LegendaryHeroes::choiceText(1) == u"Off");
	CHECK(LegendaryHeroes::choiceText(4) == u"4x");
	CHECK(LegendaryHeroes::label().find(u"OpenBFME") != std::u16string::npos);
}

TEST_CASE("rules1 binary facts: the tables of RotWK's .rdata are the port's (RW 0x960A10 .. 0x960B14, 0xDB7794 ..)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("rules1 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// the getters return these table addresses (mov eax, imm32 at RW 0x960A1F / 0x960A25 / 0x960A3A / 0x960A40)
	CHECK(pe->u32At(0x960A20) == 0xDB77D0u);
	CHECK(pe->u32At(0x960A26) == 0xDB77BCu);
	CHECK(pe->u32At(0x960A3B) == 0xDB77E4u);
	CHECK(pe->u32At(0x960A41) == 0xDB77C4u);
	for (int rule = 0; rule < NUM_GAME_RULES; ++rule)
	{
		CHECK(pe->cstring(pe->u32At(0xDB7794 + 4u * (unsigned)rule)) == MpGameRules::ruleLabel(rule));
	}
	struct Table
	{
		int mode;
		std::uint32_t combos, checks;
	};
	for (const Table &t : { Table{ RULES_MODE_SKIRMISH, 0xDB77BC, 0xDB77C4 }, Table{ RULES_MODE_WOTR, 0xDB77D0, 0xDB77E4 } })
	{
		for (int i = 0; i < MpGameRules::numComboBoxes(t.mode); ++i)
		{
			const std::uint32_t p = pe->u32At(t.combos + 4u * (unsigned)i);
			const MpRuleCombo &c = *MpGameRules::comboBoxes(t.mode)[i];
			CHECK((int)pe->u32At(p) == c.id);
			CHECK((int)pe->u32At(p + 8) == c.count);
			CHECK((int)pe->u32At(p + 12) == c.defaultIndex);
			const std::uint32_t choices = pe->u32At(p + 4);
			for (int j = 0; j < c.count; ++j)
			{
				CHECK(pe->cstring(pe->u32At(choices + 8u * (unsigned)j)) == c.choices[j].label);
				CHECK((std::int32_t)pe->u32At(choices + 8u * (unsigned)j + 4) == c.choices[j].value);
			}
		}
		for (int i = 0; i < MpGameRules::numCheckBoxes(t.mode); ++i)
		{
			const std::uint32_t p = pe->u32At(t.checks + 4u * (unsigned)i);
			const MpRuleCheck &k = *MpGameRules::checkBoxes(t.mode)[i];
			CHECK((int)pe->u32At(p) == k.id);
			std::vector<std::uint8_t> b;
			REQUIRE(pe->read(p + 4, 1, &b));
			CHECK((b[0] != 0) == k.value);
		}
	}
	// the counts: push 2 / push 5 (combos, RW 0x960A55 / 0x960A59) and push 2 / push 3 (checks, RW 0x960A6C / 0x960A70)
	std::vector<std::uint8_t> b;
	REQUIRE(pe->read(0x960A55, 1 + 1, &b));
	CHECK(b[0] == 0x6A);
	CHECK(b[1] == 5);
	REQUIRE(pe->read(0x960A59, 2, &b));
	CHECK(b[1] == 2);
	REQUIRE(pe->read(0x960A6C, 2, &b));
	CHECK(b[1] == 2);
	REQUIRE(pe->read(0x960A70, 2, &b));
	CHECK(b[1] == 3);
	// the readers the port follows: Player::init's start money (push [eax + 0x70], RW 0x6B0583), the command point cap (mov eax, [eax + 0x6C], RW 0x6A7DFA),
	// the ring heroes game mode (cmp [eax + 0x68], 1, RW 0x7E9EA1)
	REQUIRE(pe->read(0x6B0583, 3, &b));
	CHECK(b == std::vector<std::uint8_t>{ 0xFF, 0x70, 0x70 });
	REQUIRE(pe->read(0x6A7DFA, 3, &b));
	CHECK(b == std::vector<std::uint8_t>{ 0x8B, 0x40, 0x6C });
	REQUIRE(pe->read(0x7E9EA1, 4, &b));
	CHECK(b == std::vector<std::uint8_t>{ 0x83, 0x78, 0x68, 0x01 });
	// GlobalData + 0x9D4 is the demo mode: GlobalData's constructor stores 0 (mov [esi + 0x9D4], ebx with ebx 0, RW 0x642E66), the externs InBetaDemo /
	// InDreamMachineDemo read it (provider RW 0x81545A: cmp [eax + 0x9D4], 1 / 2), the one field table row with offset 0x9D4 is InGameUI's
	REQUIRE(pe->read(0x642E66, 6, &b));
	CHECK(b == std::vector<std::uint8_t>{ 0x89, 0x9E, 0xD4, 0x09, 0x00, 0x00 });
	CHECK(pe->cstring(0xC4FEC0) == "InBetaDemo");
	CHECK(pe->cstring(0xC4FEAC) == "InDreamMachineDemo");
	CHECK(pe->u32At(0x815E81) == 0x81545Au);
	REQUIRE(pe->read(0x815485, 7, &b));
	CHECK(b == std::vector<std::uint8_t>{ 0x83, 0xB8, 0xD4, 0x09, 0x00, 0x00, 0x01 });
	CHECK(pe->cstring(pe->u32At(0xC128B0)) == "HeroInitialSpawnNotificationTimeout");
	CHECK(pe->u32At(0xC128BC) == 0x9D4u);
}

TEST_CASE("rules1: the rules and the Legendary Heroes factor travel in the game setup (LAN options, new game message, replay header); an unknown factor is refused")
{
	NewGameMessage m;
	m.game.mapName = "maps/a/a.map";
	m.game.seed = 77;
	m.game.startingCash = -1;
	m.game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	m.game.rules[RULE_INITIAL_RESOURCES] = 2500;
	m.game.legendaryHeroes = 4;
	NetByteWriter w;
	NetPacket::writeNewGameMessage(w, m);
	NetByteReader r(w.data());
	NewGameMessage back;
	std::string error;
	REQUIRE_MESSAGE(NetPacket::readNewGameMessage(r, back, &error), error);
	CHECK(back.game == m.game);
	CHECK(back.game.rules[RULE_INITIAL_RESOURCES] == 2500);
	CHECK(back.game.legendaryHeroes == 4);
	// the factor is the last i32 of the GameInfo: 5 is refused (a peer of another rule set refuses the options instead of desyncing)
	std::vector<std::uint8_t> bad = w.data();
	bad[bad.size() - 4] = 5;
	NetByteReader r2(bad);
	CHECK_FALSE(NetPacket::readNewGameMessage(r2, back, &error));
	CHECK(error.find("Legendary Heroes") != std::string::npos);
	// a starting cash below -1 is refused, -1 (unset) is not
	m.game.startingCash = -2;
	NetByteWriter w3;
	NetPacket::writeNewGameMessage(w3, m);
	NetByteReader r3(w3.data());
	CHECK_FALSE(NetPacket::readNewGameMessage(r3, back, &error));

	// the replay header (format version 4)
	m.game.startingCash = -1;
	const std::string path = "rules1_test.replay";
	ReplayHeader h;
	h.game = m;
	{
		ReplayWriter rw;
		REQUIRE(rw.open(path, h, &error));
		rw.close(1, 0x1u);
	}
	ReplayFile f;
	REQUIRE_MESSAGE(ReplayFile::load(path, f, &error), error);
	std::remove(path.c_str());
	CHECK(f.header.game.game.legendaryHeroes == 4);
	CHECK(f.header.game.game.rules == m.game.rules);
}

// ---- the game start (retail data) -----------------------------------------------------------------------------------------------------------------

namespace
{
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

// Evendim, slot 0 / 1 on start positions 0 / 1: a computer of `difficultyA` / `difficultyB` (0 .. 3) or an idle human (-1)
NewGameMessage evendim(const starttest::Shared &s, int difficultyA, int difficultyB, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = -1; // as the lobby leaves it: the InitialResources rule gives the money
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	const int difficulty[2] = { difficultyA, difficultyB };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = difficulty[i] < 0 ? SLOT_PLAYER : (SlotState)(SLOT_EASY_AI + difficulty[i]);
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
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
};

void load(starttest::Shared &s, const NewGameMessage &m, Game &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
}

Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

bool ringHeroesActive(GameLogic &logic)
{
	ScriptCondition c;
	c.type = c.resolved = ScriptTemplates::findCondition("IS_GAME_MODE_ACTIVE");
	REQUIRE(c.resolved >= 0);
	ScriptParameter p;
	p.stringValue = "RingHeroes"; // any case (RW 0x4372C0)
	c.params.push_back(p);
	return ScriptConditions::evaluate(logic.scriptEngine(), c);
}

// the first BuildableHeroesMP template of the player's faction, made for `owner` (null: the neutral player)
Object *makeHero(GameLogic &logic, const Player &faction, Player *owner)
{
	const PlayerTemplate *pt = faction.getPlayerTemplate();
	REQUIRE(pt);
	REQUIRE_FALSE(pt->m_buildableHeroesMP.empty());
	const ThingTemplate *tt = logic.things().findTemplate(pt->m_buildableHeroesMP[0]);
	REQUIRE(tt);
	Player *p = owner ? owner : logic.players().getNeutralPlayer();
	REQUIRE(p);
	Object *o = logic.newObject(tt, p->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o);
	return o;
}

float damageTaken(Object &o, float amount)
{
	ActiveBody *body = dynamic_cast<ActiveBody *>(o.getBodyModule());
	REQUIRE(body);
	const float before = body->getHealth();
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_SLASH;
	info.m_input.m_amount = amount;
	body->attemptDamage(info);
	return before - body->getHealth();
}
} // namespace

TEST_CASE("rules1 retail: the rules reach the game start: InitialResources is the money, CommandPointFactor scales the cap, AllowRingHeroes is the ringheroes game mode")
{
	OPENBFME_REQUIRE_START(s);
	// two games with an idle human in slot 0: the defaults (1000, 100 %, ring heroes on) and changed rules (2500, 200 %, ring heroes off)
	NewGameMessage a = evendim(*s, -1, 0, 31);
	a.game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	NewGameMessage b = a;
	b.game.rules[RULE_INITIAL_RESOURCES] = 2500;
	b.game.rules[RULE_COMMAND_POINT_FACTOR] = 200;
	b.game.rules[RULE_ALLOW_RING_HEROES] = 0;
	Game ga, gb;
	load(*s, a, ga);
	load(*s, b, gb);
	GameLogic &la = ga.game->logic();
	GameLogic &lb = gb.game->logic();
	Player *ha = playerAtStart(la, 0);
	Player *hb = playerAtStart(lb, 0);
	REQUIRE(ha);
	REQUIRE(hb);
	// RW 0x6B0578: FactionMen's StartMoney is 0, so the rule is the money (GameData's DefaultStartingCash 1500 is the branch without a game)
	CHECK(ha->getMoney()->countMoney() == 1000);
	CHECK(hb->getMoney()->countMoney() == 2500);
	// RW 0x6A7DE2: cap * percent / 100
	CHECK(hb->commandPoints().getCap() == 2 * ha->commandPoints().getCap());
	CHECK(ha->commandPoints().getCap() > 0);
	// RW 0x7E9E83
	CHECK(ringHeroesActive(la));
	CHECK_FALSE(ringHeroesActive(lb));
	// the rules are part of the state hash
	CHECK(la.computeStateHash() != lb.computeStateHash());

	// a message built without a lobby (no rule block) keeps GameData's DefaultStartingCash and the old answers
	NewGameMessage c = evendim(*s, -1, 0, 31);
	Game gc;
	load(*s, c, gc);
	GameLogic &lc = gc.game->logic();
	CHECK(playerAtStart(lc, 0)->getMoney()->countMoney() == 1500);
	CHECK(playerAtStart(lc, 0)->commandPoints().getCap() == ha->commandPoints().getCap());
	CHECK_FALSE(ringHeroesActive(lc));
}

TEST_CASE("rules1 retail: Legendary Heroes 4x: a player's heroes get 4x health and take a quarter of the damage, the neutral player's do not; two AI-vs-AI runs hash the same")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m = evendim(*s, 3, 3, 4711);
	m.game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	m.game.legendaryHeroes = 4;
	const int frames = 600; // two game minutes

	std::vector<std::uint32_t> hashes[2];
	for (int run = 0; run < 2; ++run)
	{
		Game g;
		load(*s, m, g);
		GameLogic &logic = g.game->logic();
		CHECK(logic.legendaryHeroes() == 4);
		Player *men = playerAtStart(logic, 0);
		REQUIRE(men);
		// each side gets its first BuildableHeroesMP hero beside its base at the start, so legendary heroes take part in the hashed game
		for (int k = 0; k < 2; ++k)
		{
			Player *p = playerAtStart(logic, k);
			REQUIRE(p);
			const Object *base = nullptr;
			for (Object *o = logic.getFirstObject(); o && !base; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == p && o->isKindOfName("STRUCTURE"))
				{
					base = o;
				}
			}
			REQUIRE(base);
			Object *hero = makeHero(logic, *p, p);
			Coord3D at = *base->getPosition();
			at.x = SimMath::addf32(at.x, 120.0f);
			hero->setPosition(&at);
			REQUIRE(hero->legendaryFactor() == 4);
		}
		for (int i = 0; i < frames; ++i)
		{
			g.game->advance(0.2);
			hashes[run].push_back(logic.computeStateHash());
		}
		// every hero of a playable side made by the game (built by the AI, or Create-a-Hero / ring heroes when they come) is legendary
		int heroes = 0;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->legendaryFactor() == 4)
			{
				++heroes;
				const ActiveBody *body = dynamic_cast<const ActiveBody *>(o->getBodyModule());
				REQUIRE(body);
				CHECK(body->bodyDamageScalar() == 0.25f);
				CHECK(LegendaryHeroesGame::damageDealtScalar(*o) == 1.5f);
			}
		}
		MESSAGE("run " << run << ": " << heroes << " legendary heroes after " << frames << " frames, hash " << hashes[run].back());
		CHECK(heroes >= 1);
		if (run == 0)
		{
			// the scaled values: the same hero template for the Men player and for the neutral player (not a playable side)
			Object *legendary = makeHero(logic, *men, men);
			Object *plain = makeHero(logic, *men, nullptr);
			REQUIRE(legendary->legendaryFactor() == 4);
			REQUIRE(plain->legendaryFactor() == 1);
			const float maxPlain = plain->getBodyModule()->getMaxHealth();
			REQUIRE(maxPlain > 0.0f);
			CHECK(legendary->getBodyModule()->getMaxHealth() == 4.0f * maxPlain);
			CHECK(legendary->getBodyModule()->getHealth() == 4.0f * plain->getBodyModule()->getHealth());
			const float hitPlain = damageTaken(*plain, 100.0f);
			const float hitLegendary = damageTaken(*legendary, 100.0f);
			REQUIRE(hitPlain > 0.0f);
			CHECK(hitLegendary == 0.25f * hitPlain);
			CHECK(LegendaryHeroesGame::damageDealtScalar(*legendary) == 1.5f);
			CHECK(LegendaryHeroesGame::damageDealtScalar(*plain) == 1.0f);

			// ownership transfers (one policy: the creation decides, for health, armour and damage alike): the legendary hero given to the neutral player
			// stays legendary; the plain one given to the Men player stays plain
			const ActiveBody *lb = dynamic_cast<const ActiveBody *>(legendary->getBodyModule());
			const ActiveBody *pb = dynamic_cast<const ActiveBody *>(plain->getBodyModule());
			const float legendaryMax = lb->getMaxHealth(), plainMax = pb->getMaxHealth();
			legendary->setTeam(logic.players().getNeutralPlayer()->getDefaultTeam());
			plain->setTeam(men->getDefaultTeam());
			REQUIRE(legendary->getControllingPlayer() == logic.players().getNeutralPlayer());
			REQUIRE(plain->getControllingPlayer() == men);
			CHECK(LegendaryHeroesGame::damageDealtScalar(*legendary) == 1.5f);
			CHECK(lb->bodyDamageScalar() == 0.25f);
			CHECK(lb->getMaxHealth() == legendaryMax);
			CHECK(LegendaryHeroesGame::damageDealtScalar(*plain) == 1.0f);
			CHECK(pb->bodyDamageScalar() == 1.0f);
			CHECK(pb->getMaxHealth() == plainMax);
		}
	}
	// the two runs agree on every frame's hash (the scaled-value heroes below are made after the hashes of run 0 were taken)
	REQUIRE(hashes[0].size() == hashes[1].size());
	CHECK(hashes[0] == hashes[1]);

	// the factor is in the hash from the first frame on: the same game with the rule off hashes differently
	NewGameMessage off = m;
	off.game.legendaryHeroes = LegendaryHeroes::kOff;
	Game g;
	load(*s, off, g);
	g.game->advance(0.2);
	CHECK(g.game->logic().legendaryHeroes() == LegendaryHeroes::kOff);
	CHECK(g.game->logic().computeStateHash() != hashes[0][0]);
}
