// OpenBFME. GAMEMODES-1 tests: RotWK 2.01's skirmish / LAN game modes (docs/GAMEMODES.md). RotWK has no King of the Hill, Capture the Flag, Resource
// Race or Hero vs Hero mode; what a skirmish / LAN game can be is the battle judged by VictoryConditions rule 3 (base or army: TheGameLogic + 0x114 == 3),
// the SANDBOX game (one team, GameInfo::isSandbox RW 0x800C8F) and the War of the Ring (lane WOTR-1). The binary facts are read from RW_GAME_DAT (skipped
// without it); the games run on retail data (skipped without the installs).
#include "doctest.h"

#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/MpGameRules.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/VictoryConditions.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using retailtest::PeImage;

// ---- the binary facts ---------------------------------------------------------------------------------------------------------------------------

TEST_CASE("gamemodes1 binary facts: the victory rule is chosen by the game kind, the sandbox is GameInfo vslot 0x50, the lobby's sandbox line (RW 0x808A93, 0x800C8F, 0x8436D8)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("gamemodes1 binary facts (RW_GAME_DAT unset)");
		return;
	}
	auto bytes = [&](std::uint32_t va, std::vector<std::uint8_t> expected) {
		std::vector<std::uint8_t> got;
		REQUIRE(pe->read(va, expected.size(), &got));
		return got == expected;
	};
	// VictoryConditions init RW 0x808A43: mov edx, [TheGameLogic]; ...; cmp dword ptr [edx + 0x114], 3; ...; sete al; inc eax; inc eax; mov [ecx + 0xC], eax
	CHECK(pe->u32At(0x808A8D) == 0xDE412Cu);
	CHECK(bytes(0x808A93, { 0x83, 0xBA, 0x14, 0x01, 0x00, 0x00, 0x03 }));
	CHECK(bytes(0x808A9B, { 0x0F, 0x94, 0xC0, 0x40, 0x40, 0x89, 0x41, 0x0C }));
	// the game kind is 3 from the constructor (RW 0x6301D3) and after MSG_CLEAR_GAME_DATA (RW 0x779F20): mov dword ptr [reg + 0x114], 3
	CHECK(bytes(0x6301D3, { 0xC7, 0x86, 0x14, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00 }));
	CHECK(bytes(0x779F20, { 0xC7, 0x80, 0x14, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00 }));
	// VictoryConditions' vtable RW 0xC4F108 has no setter of + 0xC: its slots 0x24 / 0x28 are init / update (RW 0x808A43 / 0x808F53)
	CHECK(pe->u32At(0xC4F108 + 0x24) == 0x808A43u);
	CHECK(pe->u32At(0xC4F108 + 0x28) == 0x808F53u);
	// the three GameInfo vtables (RW 0xBFD668, 0xC2EF18, 0xC2FB20) have isSandbox RW 0x800C8F in slot 20 (vslot 0x50), getLocalSlotNum RW 0x800B8C in slot 13
	for (std::uint32_t vt : { 0xBFD668u, 0xC2EF18u, 0xC2FB20u })
	{
		CHECK(pe->u32At(vt + 0x50) == 0x800C8Fu);
		CHECK(pe->u32At(vt + 0x34) == 0x800B8Cu);
	}
	// isSandbox starts with call [eax + 0x34] (getLocalSlotNum) and compares a slot's + 0x1C (the team)
	CHECK(bytes(0x800C96, { 0xFF, 0x50, 0x34 }));
	CHECK(bytes(0x800CBF, { 0x8B, 0x79, 0x1C }));
	// its readers: localAlliedVictory (call [eax + 0x50] at RW 0x808C64) and the keep-score switch of update (RW 0x809034)
	CHECK(bytes(0x808C64, { 0xFF, 0x50, 0x50 }));
	CHECK(bytes(0x809034, { 0xFF, 0x50, 0x50 }));
	// the lobby's start check RW 0x8431B8 pushes "GUI:SandboxMode" at RW 0x8436D8 and "GUI:NeedWOTREnemy" at RW 0x8436A3
	CHECK(pe->u32At(0x8436D9) == 0xC54280u);
	CHECK(pe->cstring(0xC54280) == "GUI:SandboxMode");
	CHECK(pe->u32At(0x8436A4) == 0xC54290u);
	CHECK(pe->cstring(0xC54290) == "GUI:NeedWOTREnemy");
	CHECK(pe->cstring(pe->u32At(0x843565)) == "LAN:TooManyPlayers");
	CHECK(pe->cstring(pe->u32At(0x843576)) == "LAN:TooFewPlayers");
	CHECK(pe->cstring(pe->u32At(0x843659)) == "LAN:NeedMoreTeams");
}

TEST_CASE("gamemodes1 binary facts: game.dat names none of the modes RotWK does not have")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("gamemodes1 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// the inventory of docs/GAMEMODES.md: no string of the executable names these modes (searched without case and without spaces)
	std::string lowered;
	{
		const char *path = std::getenv("RW_GAME_DAT");
		REQUIRE(path);
		std::ifstream in(path, std::ios::binary);
		REQUIRE(in);
		lowered.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) { return (char)std::tolower((unsigned char)c); });
	for (const char *mode : { "kingofthehill", "king of the hill", "capturetheflag", "capture the flag", "resourcerace", "resource race", "herovshero", "hero vs hero", "captureandhold", "capture and hold" })
	{
		INFO(mode);
		CHECK(lowered.find(mode) == std::string::npos);
	}
	// the one game mode name the script condition IS_GAME_MODE_ACTIVE knows (RW 0x7E9E83, lane RULES-1)
	CHECK(lowered.find("ringheroes") != std::string::npos);
}

// ---- GameInfo::isSandbox (RW 0x800C8F) ----------------------------------------------------------------------------------------------------------

namespace
{
SkirmishGameInfo twoSlots(int teamA, int teamB, SlotState b = SLOT_EASY_AI)
{
	SkirmishGameInfo g;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].teamNumber = teamA;
	g.slots[1].state = b;
	g.slots[1].teamNumber = teamB;
	return g;
}
} // namespace

TEST_CASE("gamemodes1: GameInfo::isSandbox: every other occupied slot on the local slot's team; a slot without a team breaks it; a game alone is a sandbox")
{
	CHECK(twoSlots(0, 0).isSandbox(0));
	CHECK(twoSlots(2, 2).isSandbox(1));
	CHECK_FALSE(twoSlots(0, 1).isSandbox(0));
	CHECK_FALSE(twoSlots(-1, -1).isSandbox(0)); // two players without a team are enemies
	CHECK_FALSE(twoSlots(0, -1).isSandbox(0));
	CHECK(twoSlots(-1, 0).isSandbox(1) == false); // the local slot's own team -1 never matches
	CHECK(twoSlots(0, 0, SLOT_OPEN).isSandbox(0)); // an open or closed slot does not count
	CHECK(twoSlots(-1, 0, SLOT_CLOSED).isSandbox(0)); // alone, even without a team
	// an observer is an occupied slot (RW 0x8009B1): its team counts
	SkirmishGameInfo obs = twoSlots(0, -1, SLOT_PLAYER);
	obs.slots[1].playerTemplate = PLAYERTEMPLATE_OBSERVER;
	CHECK_FALSE(obs.isSandbox(0));
	// no local slot (RW 0x800CA2): the first occupied slot's team stands in, and that slot is checked too
	CHECK(twoSlots(1, 1).isSandbox(-1));
	CHECK_FALSE(twoSlots(-1, -1).isSandbox(-1));
	CHECK_FALSE(twoSlots(1, 2).isSandbox(-1));
}

// ---- the lobby's start check (RW 0x8431B8) ------------------------------------------------------------------------------------------------------

namespace
{
struct GmText : GameTextSource
{
	// every label is its own text, so the checks name the label that was shown
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		out.assign(label.begin(), label.end());
		return true;
	}
};

struct GmSource : SkirmishSetupSource
{
	std::vector<MapCacheEntry> mapList;
	std::vector<SkirmishFaction> factionList;
	std::vector<SkirmishColor> colorList;
	std::vector<int> cash{ 1000 };
	GmSource()
	{
		MapCacheEntry m;
		m.name = "maps/four/four.map";
		m.displayName = u"$Map:Four";
		m.numPlayers = 4;
		m.isMultiplayer = true;
		m.isOfficial = true;
		mapList.push_back(m);
		for (int i = 0; i < 4; ++i)
		{
			SkirmishColor c;
			c.name = "Color" + std::to_string(i);
			c.rgb = 0x101010u * (unsigned)(i + 1);
			colorList.push_back(c);
		}
	}
	const std::vector<SkirmishFaction> &factions() const override { return factionList; }
	const std::vector<SkirmishColor> &colors() const override { return colorList; }
	const std::vector<MapCacheEntry> &maps() const override { return mapList; }
	int defaultStartingCash() const override { return 1000; }
	const std::vector<int> &startingCashChoices() const override { return cash; }
	int mapContentsMask(const MapCacheEntry &) const override { return 1; }
	bool fileExists(const std::string &) const override { return false; }
	bool hasFileSystem() const override { return false; }
};
} // namespace

TEST_CASE("gamemodes1: the start check: one team is a sandbox game (GUI:SandboxMode, the start goes on); RotWK's refusals in RotWK's order")
{
	GmSource src;
	GmText text;
	SkirmishGameSetup setup(src, &text);
	REQUIRE(setup.init(u"P", 7, "maps/four/four.map", nullptr));
	// the lobby's default: the player and an easy computer, both without a team: two random teams, an ordinary game
	SkirmishGameSetup::StartCheck c = setup.checkStart();
	CHECK(c.ok);
	CHECK_FALSE(c.sandbox);
	CHECK(c.lines.empty());
	CHECK(c.stops.empty());
	CHECK(c.numHumans == 1);
	// both on team 1: one team -> "Entering sandbox mode..." and the start goes on
	REQUIRE(setup.setSlotTeam(0, 1));
	REQUIRE(setup.setSlotTeam(1, 1));
	c = setup.checkStart();
	CHECK(c.ok);
	CHECK(c.sandbox);
	REQUIRE(c.lines.size() == 1);
	CHECK(c.lines[0].text == u"GUI:SandboxMode");
	CHECK(c.lines[0].kind == 0); // a local line: the skirmish owner (RW 0x928482) shows nothing, the LAN owner a chat line
	CHECK(setup.info().isSandbox(0));
	// the player alone (the computer's slot closed): a sandbox too
	REQUIRE(setup.setSlotState(1, SLOT_CLOSED, u""));
	c = setup.checkStart();
	CHECK(c.ok);
	CHECK(c.sandbox);
	// in the War of the Ring with its enemy flag (+ 0x3A5 & 1) one team is refused: first the two-humans gate (RW 0x8434DE), with two humans GUI:NeedWOTREnemy
	setup.setStrategicMode(true);
	c = setup.checkStart(0x100);
	CHECK_FALSE(c.ok);
	CHECK(c.refusalLabel == "GUI:NeedHumanPlayers");
	REQUIRE(c.stops.size() == 1);
	CHECK(c.stops[0].rfind("[S-3610] start check:", 0) == 0);
	SkirmishGameInfo two = setup.info();
	two.slots[1].state = SLOT_PLAYER;
	two.slots[1].teamNumber = 1;
	two.slots[0].startPos = 0;
	two.slots[1].startPos = 1;
	setup.replaceInfo(two);
	c = setup.checkStart(0x100);
	CHECK_FALSE(c.ok);
	CHECK(c.refusalLabel == "GUI:NeedWOTREnemy");
	REQUIRE(c.lines.size() == 2); // RW 0x8406DB (kind 1), then kind 2
	CHECK(c.lines[0].kind == 1);
	CHECK(c.lines[1].kind == 2);
	// ... and a War of the Ring slot without a start position: GUI:NeedStartSpots (RW 0x8435F9)
	two.slots[1].startPos = -1;
	setup.replaceInfo(two);
	c = setup.checkStart(0x100);
	CHECK(c.refusalLabel == "GUI:NeedStartSpots");
	setup.setStrategicMode(false);
	// too many players for the map (RW 0x843564): LAN:TooManyPlayers with the map's count
	SkirmishGameInfo many = setup.info();
	for (int i = 0; i < 5; ++i)
	{
		many.slots[i].state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		many.slots[i].teamNumber = -1;
	}
	setup.replaceInfo(many);
	c = setup.checkStart();
	CHECK(c.refusalLabel == "LAN:TooManyPlayers");
	// a human without the map and no transfer: GUI:PlayerNoMap (a line) then GUI:CouldNotTransferMap
	SkirmishGameInfo nomap = twoSlots(-1, -1, SLOT_PLAYER);
	nomap.mapName = "maps/four/four.map";
	nomap.slots[1].hasMap = false;
	setup.replaceInfo(nomap);
	c = setup.checkStart();
	CHECK(c.refusalLabel == "GUI:CouldNotTransferMap");
	REQUIRE(c.lines.size() == 2);
	CHECK(c.lines[0].text.find(u"GUI:PlayerNoMap") == 0);
	CHECK(c.lines[0].kind == 2);
	CHECK(c.lines[1].text == u"GUI:CouldNotTransferMap");
	CHECK(c.lines[1].kind == 1);
	CHECK(setup.checkStart(0, true).ok); // the map can be sent
	// the clan game rule: the players carry no clan tag
	SkirmishGameInfo clan = twoSlots(0, 1, SLOT_PLAYER);
	clan.mapName = "maps/four/four.map";
	clan.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	clan.rules[RULE_CLAN_GAME] = 1;
	setup.replaceInfo(clan);
	CHECK(setup.checkStart().refusalLabel == "CLAN:ErrorMissingClanAffiliation");
	// a host's start intent with a player not ready (flags 0x20)
	SkirmishGameInfo notReady = twoSlots(0, 1, SLOT_PLAYER);
	notReady.mapName = "maps/four/four.map";
	setup.replaceInfo(notReady);
	CHECK(setup.checkStart(0x20).refusalLabel == "GUI:NotifiedStartIntent");
	CHECK(setup.checkStart().ok);
}

// ---- the games (retail data) --------------------------------------------------------------------------------------------------------------------

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

// Evendim, slot 0 an idle human (Men, start 0), slot 1 an idle human or a computer (Mordor, start 1), on the given teams
NewGameMessage evendim(const starttest::Shared &s, int teamA, int teamB, SlotState b, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = -1;
	m.game.rules = MpGameRules::defaults(RULES_MODE_SKIRMISH);
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	const int teams[2] = { teamA, teamB };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : b;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = teams[i];
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

// the player's first BuildableHeroesMP hero (not the Create-a-Hero placeholder) beside its first structure: an army unit (VictoryConditionUnitObjectFilter: ANY -DOZER -NOT_AUTOACQUIRABLE ...)
Object *makeArmy(GameLogic &logic, Player &p)
{
	const PlayerTemplate *pt = p.getPlayerTemplate();
	REQUIRE(pt);
	const ThingTemplate *tt = nullptr;
	for (const std::string &name : pt->m_buildableHeroesMP)
	{
		if (!tt && name != "CreateAHero") // the Create-a-Hero placeholder is no unit of its own
		{
			tt = logic.things().findTemplate(name);
		}
	}
	REQUIRE(tt);
	const Object *base = nullptr;
	for (Object *o = logic.getFirstObject(); o && !base; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &p && o->isKindOfName("STRUCTURE"))
		{
			base = o;
		}
	}
	REQUIRE(base);
	Object *o = logic.newObject(tt, p.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o);
	Coord3D at = *base->getPosition();
	at.x = SimMath::addf32(at.x, 300.0f);
	o->setPosition(&at);
	return o;
}

// kills every live object of `p` that matches: structures (true) or everything else (false)
int killAll(GameLogic &logic, const Player &p, bool structures)
{
	std::vector<Object *> victims;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &p && !o->isEffectivelyDead() && !o->isDestroyed() && o->getBodyModule() && o->isKindOfName("STRUCTURE") == structures)
		{
			victims.push_back(o);
		}
	}
	for (Object *o : victims)
	{
		if (!o->isEffectivelyDead() && !o->isDestroyed())
		{
			o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		}
	}
	return (int)victims.size();
}

bool hasEvent(const VictoryConditions &v, int kind, int playerIndex)
{
	for (const VictoryConditions::Event &e : v.events())
	{
		if (e.kind == kind && e.playerIndex == playerIndex)
		{
			return true;
		}
	}
	return false;
}

struct Outcome
{
	std::vector<std::uint32_t> hashes;
	int defeatFrame = -1;
	int victoryFrame = -1;
};
} // namespace

TEST_CASE("gamemodes1 retail: the skirmish battle is judged by rule 3: with retail's filters the last structure decides; builders and an army keep no one alive; two runs hash alike")
{
	OPENBFME_REQUIRE_START(s);
	// retail's VictoryConditionUnitObjectFilter is "ANY -DOZER -NOT_AUTOACQUIRABLE -MordorWorker": ANY with no + kind accepts no template (RW 0x763543's
	// rule 2 needs a non-empty include mask), so rule 3 ("a structure, else an army") is "a structure" in a retail game
	const ObjectFilter &units = *s->settings.victoryUnitFilter;
	CHECK(units.rule == ObjectFilter::RULE_ANY);
	const NewGameMessage m = evendim(*s, -1, -1, SLOT_PLAYER, 5150);
	Outcome out[2];
	for (int run = 0; run < 2; ++run)
	{
		Game g;
		load(*s, m, g);
		GameLogic &logic = g.game->logic();
		VictoryConditions &v = logic.victory();
		CHECK(logic.economy().context().gameKind == 3);
		CHECK_FALSE(logic.economy().context().sandbox);
		CHECK(v.rule() == VictoryConditions::RULE_BASE_OR_ARMY);
		Player *alice = playerAtStart(logic, 0);
		Player *bob = playerAtStart(logic, 1);
		REQUIRE(alice);
		REQUIRE(bob);
		const Object *hero = makeArmy(logic, *bob);
		const ObjectID heroId = hero->getID();
		CHECK_FALSE(ObjectFilterMatch::allows(logic, units, *hero, nullptr));
		for (int f = 0; f < 120; ++f)
		{
			if (f == 40)
			{
				CHECK(killAll(logic, *bob, true) > 0); // every structure of Bob's: the hero and the builders stay
				int builders = 0;
				for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				{
					builders += o->getControllingPlayer() == bob && !o->isEffectivelyDead() && o->isKindOfName("DOZER") ? 1 : 0;
				}
				CHECK(builders > 0);
				CHECK_FALSE(logic.findObjectByID(heroId)->isEffectivelyDead());
				// rule 2 (a War of the Ring battle) would keep Bob for his builders; rule 3 does not (the queries change nothing)
				v.setRule(VictoryConditions::RULE_BASE_OR_BUILDERS);
				CHECK_FALSE(v.wouldBeDefeated(bob));
				v.setRule(VictoryConditions::RULE_BASE_OR_ARMY);
				CHECK(v.wouldBeDefeated(bob));
				CHECK_FALSE(v.wouldBeDefeated(alice));
			}
			g.game->advance(0.2);
			out[run].hashes.push_back(logic.computeStateHash());
			if (out[run].defeatFrame < 0 && hasEvent(v, VictoryConditions::Event::PLAYER_DEFEATED, bob->getPlayerIndex()))
			{
				out[run].defeatFrame = (int)logic.getFrame();
			}
			if (out[run].victoryFrame < 0 && hasEvent(v, VictoryConditions::Event::ALLIANCE_VICTORY, alice->getPlayerIndex()))
			{
				out[run].victoryFrame = (int)logic.getFrame();
			}
		}
		CHECK(bob->isDefeated());
		CHECK_FALSE(alice->isDefeated());
		const Object *heroAfter = logic.findObjectByID(heroId);
		CHECK((heroAfter == nullptr || heroAfter->isEffectivelyDead())); // killPlayer took the army
		CHECK(v.singleAllianceRemaining());
		CHECK(v.hasAchievedVictory(alice));
		CHECK(v.localAlliedVictory()); // the local player (slot 0) won: MULTIPLAYER_ALLIED_VICTORY, the victory screen
		bool scriptVictory = false;
		for (const ScriptEngine::EndRequest &e : logic.scriptEngine().endRequests())
		{
			scriptVictory = scriptVictory || (e.victory && e.playerIndex == alice->getPlayerIndex());
		}
		CHECK(scriptVictory); // Multiplayer_Human's VICTORY for the winner's side
		CHECK_FALSE(logic.economy().context().scoring); // RW 0x809040: the score stops with the single alliance outside a sandbox
		CHECK(out[run].defeatFrame > 40);
		CHECK(out[run].defeatFrame <= 43);
		CHECK(out[run].victoryFrame >= out[run].defeatFrame);
		MESSAGE("battle run " << run << ": Bob defeated at frame " << out[run].defeatFrame << ", victory at " << out[run].victoryFrame << ", hash " << out[run].hashes.back());
	}
	CHECK(out[0].hashes == out[1].hashes);
	CHECK(out[0].defeatFrame == out[1].defeatFrame);
	CHECK(out[0].victoryFrame == out[1].victoryFrame);
}

TEST_CASE("gamemodes1 retail: a sandbox game (one team) is never won, keeps its score switch on, and a player who loses everything is still defeated; two runs hash alike")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = evendim(*s, 0, 0, SLOT_EASY_AI, 77);
	REQUIRE(m.game.isSandbox(0));
	Outcome out[2];
	for (int run = 0; run < 2; ++run)
	{
		Game g;
		load(*s, m, g);
		GameLogic &logic = g.game->logic();
		VictoryConditions &v = logic.victory();
		CHECK(logic.economy().context().sandbox);
		CHECK(v.rule() == VictoryConditions::RULE_BASE_OR_ARMY);
		Player *human = playerAtStart(logic, 0);
		Player *ally = playerAtStart(logic, 1);
		REQUIRE(human);
		REQUIRE(ally);
		for (int f = 0; f < 200; ++f)
		{
			if (f == 60)
			{
				// one alliance from the first judgement (frame 25) on, but nobody has been defeated: no victory
				CHECK(v.singleAllianceRemaining());
				CHECK(v.endFrame() == 25);
				CHECK_FALSE(v.localAlliedVictory());
				CHECK(logic.economy().context().scoring); // RW 0x809034: the sandbox keeps the keep-score switch on
				killAll(logic, *ally, true);
				killAll(logic, *ally, false);
			}
			g.game->advance(0.2);
			out[run].hashes.push_back(logic.computeStateHash());
			if (out[run].defeatFrame < 0 && hasEvent(v, VictoryConditions::Event::PLAYER_DEFEATED, ally->getPlayerIndex()))
			{
				out[run].defeatFrame = (int)logic.getFrame();
			}
		}
		CHECK(ally->isDefeated());
		CHECK(out[run].defeatFrame > 60);
		CHECK(v.defeatCounter() > 0);
		CHECK(v.hasAchievedVictory(human)); // the rule's answer ...
		CHECK_FALSE(v.localAlliedVictory()); // ... but RW 0x808C64: a sandbox shows no victory
		CHECK(logic.economy().context().scoring);
		// the human side's Multiplayer_Human library asks MULTIPLAYER_ALLIED_VICTORY (RW 0x7EC1D3 -> vslot 0x48): no VICTORY request
		int victories = 0;
		for (const ScriptEngine::EndRequest &e : logic.scriptEngine().endRequests())
		{
			victories += e.victory ? 1 : 0;
		}
		CHECK(victories == 0);
		MESSAGE("sandbox run " << run << ": the ally defeated at frame " << out[run].defeatFrame << ", hash " << out[run].hashes.back());
	}
	CHECK(out[0].hashes == out[1].hashes);
	CHECK(out[0].defeatFrame == out[1].defeatFrame);

	// the sandbox flag is in the hash (and only when set: a game that is not a sandbox hashes as it did before the flag existed)
	Game g;
	load(*s, m, g);
	const std::uint32_t with = g.game->logic().computeStateHash();
	g.game->logic().economy().context().sandbox = false;
	CHECK(g.game->logic().computeStateHash() != with);
	// two teams: not a sandbox
	CHECK_FALSE(evendim(*s, 0, 1, SLOT_EASY_AI, 77).game.isSandbox(0));
}
