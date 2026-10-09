// OpenBFME unit tests: PlayerTemplate, Player, PlayerList, Team, Money (lane LOGIC-1).
//
// The PlayerTemplate field names, order and parse functions are the binary's (RW 0xBF81A8 + 0xC33CB8, see Common/PlayerTemplate.h); the
// player and team rules are ZH Player.cpp / PlayerList.cpp / Team.cpp as cited.

#include "doctest.h"
#include "LogicTestUtil.h"

#include "GameLogic/Map/SidesList.h"

using namespace logictest;

TEST_CASE("players: the PlayerTemplate block parses every field of the binary's table and keeps retail's defaults")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	const PlayerTemplate *a = lw.templates.findPlayerTemplate("FactionA");
	REQUIRE(a);
	CHECK(a->getName() == "FactionA");
	CHECK(a->m_side == "Alpha");
	CHECK(a->m_playableSide);
	CHECK(a->m_money.countMoney() == 1500);
	CHECK(a->m_startingBuilding == "AlphaKeep");
	CHECK(a->hasStartingBuilding());
	CHECK(a->m_preferredColor.red == doctest::Approx(1.0f));
	CHECK(a->m_preferredColor.green == doctest::Approx(0.0f));
	CHECK(a->m_preferredColor.blue == doctest::Approx(128.0f / 255.0f));
	CHECK_FALSE(a->m_evil);
	const PlayerTemplate *n = lw.templates.findPlayerTemplate("FactionNeutral");
	REQUIRE(n);
	CHECK_FALSE(n->hasStartingBuilding());
	CHECK(n->m_money.countMoney() == 0);
	CHECK(lw.templates.getPlayerTemplateCount() == 3);
	CHECK(lw.templates.findPlayerTemplate("factiona") == nullptr); // case sensitive like NAMEKEY
	// PlayableSide and not IsObserver: A and B, in definition order
	CHECK(lw.templates.playableSideIndices() == std::vector<int>{ 1, 2 });
}

TEST_CASE("players: every row of the field table is accepted with a retail-shaped value and an unknown field is a loud error")
{
	LogicWorld lw;
	const std::string all =
		"PlayerTemplate FactionAll\n"
		"  Side = All\n"
		"  PlayableSide = No\n"
		"  DisplayName = SIDE:ALL\n"
		"  StartMoney = 100\n"
		"  PreferredColor = R:1 G:2 B:3\n"
		"  StartingBuilding = Hq\n"
		"  StartingUnit0 = U0\n  StartingUnit9 = U9\n"
		"  StartingUnitOffset0 = X:1.0 Y:2.0 Z:3.0\n"
		"  StartingUnitOffset9 = X:-1.0 Y:-2.0 Z:0.0\n"
		"  StartingUnitTacticalWOTR = T1 T2\n"
		"  ProductionCostChange = SomeUnit 90%\n"
		"  ProductionTimeChange = SomeUnit 110%\n"
		"  ProductionVeterancyLevel = SomeUnit ELITE\n"
		"  IntrinsicSciences = SCIENCE_A SCIENCE_B\n"
		"  IntrinsicSciencesMP = SCIENCE_C\n"
		"  PurchaseScienceCommandSet = CS1\n  PurchaseScienceCommandSetMP = CS2\n"
		"  SpecialPowerShortcutCommandSet = CS3\n  SpecialPowerShortcutWinName = Win\n  SpecialPowerShortcutButtonCount = 4\n"
		"  IsObserver = No\n"
		"  ScoreScreenImage = Img1\n  LoadScreenImage = Img2\n  LoadScreenMusic = Music\n  HeadWaterMark = Img3\n  FlagWaterMark = Img4\n"
		"  EnabledImage = Img5\n  SideIconImage = Img6\n  BeaconName = Beacon\n"
		"  LightPointsUpSound = Snd1\n  ObjectiveAddedSound = Snd2\n  ObjectiveCompletedSound = Snd3\n"
		"  InitialUpgrades = UpA UpB\n"
		"  DefaultPlayerAIType = Ai\n  SpellBook = SB\n  SpellBookMp = SBM\n  Evil = Yes\n"
		"  BuildableHeroesMP = H1 H2\n  BuildableRingHeroesMP = R1\n"
		"  SpellStoreCurrentPowerLabel = L1\n  SpellStoreMaximumPowerLabel = L2\n"
		"  ResourceModifierObjectFilter = ANY\n"
		"  ResourceModifierValues = 1 2 3\n"
		"  MultiSelectionPortrait = Port\n"
		"  IntrinsicSciencePurchasePoints = 7\n  MaxLevelMP = 5\n  MaxLevelSP = 10\n"
		"End\n";
	REQUIRE(lw.w.load(all, INI_LOAD_OVERWRITE, "all.ini") == "");
	const PlayerTemplate *t = lw.templates.findPlayerTemplate("FactionAll");
	REQUIRE(t);
	CHECK(t->m_money.countMoney() == 100);
	CHECK(t->m_startingUnit[0] == "U0");
	CHECK(t->m_startingUnit[9] == "U9");
	CHECK(t->m_startingUnitOffset[0].y == 2.0f);
	CHECK(t->m_startingUnitOffset[9].x == -1.0f);
	CHECK(t->m_startingUnitTacticalWOTR == std::vector<std::string>{ "T1", "T2" });
	CHECK(t->m_productionCostChanges.at("SomeUnit") == doctest::Approx(0.9f));
	CHECK(t->m_productionTimeChanges.at("SomeUnit") == doctest::Approx(1.1f));
	CHECK(t->m_productionVeterancyLevels.at("SomeUnit") == 2);
	CHECK(t->m_intrinsicSciences.size() == 2);
	CHECK(t->m_specialPowerShortcutButtonCount == 4);
	CHECK(t->m_resourceModifierValues == std::vector<int>{ 1, 2, 3 });
	CHECK((bool)t->m_resourceModifierObjectFilter);
	CHECK(t->m_maxLevelSP == 10);
	CHECK(t->m_evil);
	CHECK(t->m_buildableHeroesMP.size() == 2);
	// errors are the INI core's: unknown field, bad colour range, bad veterancy name
	CHECK(lw.w.load("PlayerTemplate Bad1\n  Nonsense = 1\nEnd\n", INI_LOAD_OVERWRITE, "b1.ini").find("Nonsense") != std::string::npos);
	CHECK(lw.w.load("PlayerTemplate Bad2\n  PreferredColor = R:300 G:0 B:0\nEnd\n", INI_LOAD_OVERWRITE, "b2.ini").find("out of range") != std::string::npos);
	CHECK(lw.w.load("PlayerTemplate Bad3\n  ProductionVeterancyLevel = X LEGENDARY\nEnd\n", INI_LOAD_OVERWRITE, "b3.ini") != "");
}

TEST_CASE("players: the PlayerTemplate field table has the binary's 64 rows in the binary's order")
{
	// RW 0xBF81A8 (61 rows) + 0xC33CB8 (3): the names, in order, from the extractor dump
	LogicWorld lw;
	const std::vector<std::string> names = { "Side", "PlayableSide", "DisplayName", "StartMoney", "PreferredColor", "StartingBuilding", "StartingUnit0", "StartingUnit1",
		"StartingUnit2", "StartingUnit3", "StartingUnit4", "StartingUnit5", "StartingUnit6", "StartingUnit7", "StartingUnit8", "StartingUnit9", "StartingUnitOffset0",
		"StartingUnitOffset1", "StartingUnitOffset2", "StartingUnitOffset3", "StartingUnitOffset4", "StartingUnitOffset5", "StartingUnitOffset6", "StartingUnitOffset7",
		"StartingUnitOffset8", "StartingUnitOffset9", "StartingUnitTacticalWOTR", "ProductionCostChange", "ProductionTimeChange", "ProductionVeterancyLevel",
		"IntrinsicSciences", "IntrinsicSciencesMP", "PurchaseScienceCommandSet", "PurchaseScienceCommandSetMP", "SpecialPowerShortcutCommandSet",
		"SpecialPowerShortcutWinName", "SpecialPowerShortcutButtonCount", "IsObserver", "ScoreScreenImage", "LoadScreenImage", "LoadScreenMusic", "HeadWaterMark",
		"FlagWaterMark", "EnabledImage", "SideIconImage", "BeaconName", "LightPointsUpSound", "ObjectiveAddedSound", "ObjectiveCompletedSound", "InitialUpgrades",
		"DefaultPlayerAIType", "SpellBook", "SpellBookMp", "Evil", "BuildableHeroesMP", "BuildableRingHeroesMP", "SpellStoreCurrentPowerLabel",
		"SpellStoreMaximumPowerLabel", "ResourceModifierObjectFilter", "ResourceModifierValues", "MultiSelectionPortrait", "IntrinsicSciencePurchasePoints", "MaxLevelMP",
		"MaxLevelSP" };
	CHECK(names.size() == 64);
	// every name must be accepted as a field
	for (const std::string &n : names)
	{
		std::string value = "1";
		if (n == "PlayableSide" || n == "IsObserver" || n == "Evil")
		{
			value = "Yes";
		}
		else if (n == "PreferredColor")
		{
			value = "R:1 G:1 B:1";
		}
		else if (n.rfind("StartingUnitOffset", 0) == 0)
		{
			value = "X:1 Y:1 Z:1";
		}
		else if (n == "ProductionCostChange" || n == "ProductionTimeChange")
		{
			value = "U 100%";
		}
		else if (n == "ProductionVeterancyLevel")
		{
			value = "U VETERAN";
		}
		else if (n == "ResourceModifierObjectFilter")
		{
			value = "ALL";
		}
		const std::string err = lw.w.load("PlayerTemplate Row_" + n + "\n  " + n + " = " + value + "\nEnd\n", INI_LOAD_OVERWRITE, n + ".ini");
		CHECK_MESSAGE(err.empty(), n << ": " << err);
	}
}

TEST_CASE("players: a load type 2 block overrides the stored template; a type 1 block of an existing name merges into it")
{
	LogicWorld lw;
	REQUIRE(lw.w.load("PlayerTemplate FactionA\n  StartMoney = 9\nEnd\n", INI_LOAD_CREATE_OVERRIDES, "o.ini") == "");
	CHECK(lw.templates.findPlayerTemplate("FactionA")->m_money.countMoney() == 9);
	CHECK(lw.templates.findPlayerTemplate("FactionA")->m_side == "Alpha"); // the rest is the old one's
	CHECK(lw.templates.getPlayerTemplateCount() == 3);
	REQUIRE(lw.w.load("PlayerTemplate FactionA\n  StartMoney = 11\nEnd\n", INI_LOAD_OVERWRITE, "o2.ini") == "");
	CHECK(lw.templates.findPlayerTemplate("FactionA")->m_money.countMoney() == 11);
	CHECK(lw.templates.getPlayerTemplateCount() == 3);
}

TEST_CASE("players: Money deposits, withdraws at most what is there and reports what it took (ZH Money.cpp)")
{
	Money m;
	CHECK(m.countMoney() == 0);
	m.deposit(500);
	CHECK(m.withdraw(200) == 200);
	CHECK(m.countMoney() == 300);
	CHECK(m.withdraw(1000) == 300);
	CHECK(m.countMoney() == 0);
	m.deposit(7);
	m.init();
	CHECK(m.countMoney() == 0);
}

TEST_CASE("players: the skirmish setup makes the neutral player first, teams, colours, money and relationships (ZH PlayerList / Player::init)")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	PlayerList &pl = lw.players;
	REQUIRE(pl.getPlayerCount() == 3);
	CHECK(pl.getNeutralPlayer()->getPlayerName() == "");
	Player *alice = pl.findPlayerWithName("Alice");
	Player *bob = pl.findPlayerWithName("Bob");
	REQUIRE(alice);
	REQUIRE(bob);
	CHECK(alice->getPlayerIndex() == 1);
	CHECK(bob->getPlayerIndex() == 2);
	CHECK(alice->getSide() == "Alpha");
	CHECK(alice->getPlayerType() == PLAYER_HUMAN);
	CHECK(bob->getPlayerType() == PLAYER_COMPUTER);
	CHECK(bob->isSkirmishAI());
	CHECK(pl.getLocalPlayer() == alice);
	// money: FactionA has 1500; FactionB has 0 so it takes the game's starting cash (2000) (ZH Player.cpp:458-467)
	CHECK(alice->getMoney()->countMoney() == 1500);
	CHECK(bob->getMoney()->countMoney() == 2000);
	// colour: (Int)(c * 255.0f) per channel | 0xff000000 (ZH RGBColor::getAsInt)
	CHECK(alice->getPlayerColor() == 0xFFFF0080u);
	CHECK(bob->getPlayerColor() == 0xFF0040FFu);
	// teams: "team" + player name, singleton, owned by the player
	Team *at = alice->getDefaultTeam();
	REQUIRE(at);
	CHECK(at->getName() == "teamAlice");
	CHECK(at->getControllingPlayer() == alice);
	CHECK(pl.teams().findTeam("teamAlice") == at);
	CHECK(pl.getNeutralPlayer()->getDefaultTeam()->getName() == "team");
	// the team colour tints models only for a faction that can be played (a StartingBuilding) or a colour the map / lobby chose (S-119)
	CHECK(alice->hasTeamColor());
	CHECK_FALSE(pl.getNeutralPlayer()->hasTeamColor());
	// relationships: different team numbers = enemies, self allies, the neutral player neutral
	CHECK(alice->getRelationship(bob) == ENEMIES);
	CHECK(bob->getRelationship(alice) == ENEMIES);
	CHECK(alice->getRelationship(alice) == ALLIES);
	CHECK(alice->getRelationship(pl.getNeutralPlayer()) == NEUTRAL);
	CHECK(pl.getNeutralPlayer()->getRelationship(alice) == NEUTRAL);
	CHECK(at->getRelationship(bob->getDefaultTeam()) == ENEMIES); // the team asks its player
	CHECK(at->getRelationship(at) == ALLIES);
	at->setTeamRelationship(bob->getDefaultTeam(), ALLIES);        // a team override wins
	CHECK(at->getRelationship(bob->getDefaultTeam()) == ALLIES);
	CHECK(alice->getRelationship(bob->getDefaultTeam()) == ENEMIES);
}

TEST_CASE("players: allies share a team number; an explicit starting money overrides every template; a duplicate or unknown faction is reported")
{
	LogicWorld lw;
	SkirmishSetup s;
	s.players.push_back({ "P1", "FactionA", true, 3, 0xAABBCC, 0 });
	s.players.push_back({ "P2", "FactionB", false, 3, 0, 1 });
	s.players.push_back({ "P3", "FactionA", false, 4, 0, 2 });
	s.players.push_back({ "P3", "FactionA", false, 4, 0, 2 }); // duplicate name
	s.players.push_back({ "P4", "NoSuchFaction", false, -1, 0, 3 });
	s.startingMoney = 5000;
	const std::vector<std::string> errors = lw.players.setupSkirmish(s);
	CHECK(errors.size() == 2);
	Player *p1 = lw.players.findPlayerWithName("P1"), *p2 = lw.players.findPlayerWithName("P2"), *p3 = lw.players.findPlayerWithName("P3");
	CHECK(p1->getRelationship(p2) == ALLIES);
	CHECK(p1->getRelationship(p3) == ENEMIES);
	CHECK(p1->getMoney()->countMoney() == 5000);
	CHECK(p3->getMoney()->countMoney() == 5000);
	CHECK(p1->getPlayerColor() == 0xFFAABBCCu);
	CHECK(lw.players.getPlayerCount() == 5); // neutral + P1, P2, P3, P4 (the duplicate is skipped)
}

namespace
{
SidesInfo side(const std::string &name, const std::string &faction, bool human, const std::string &enemies = "", const std::string &allies = "")
{
	SidesInfo s;
	s.dict.setAsciiString("playerName", name);
	s.dict.setAsciiString("playerFaction", faction);
	s.dict.setBool("playerIsHuman", human);
	s.dict.setAsciiString("playerEnemies", enemies);
	s.dict.setAsciiString("playerAllies", allies);
	return s;
}
Dict team(const std::string &name, const std::string &owner, bool singleton)
{
	Dict d;
	d.setAsciiString("teamName", name);
	d.setAsciiString("teamOwner", owner);
	d.setBool("teamIsSingleton", singleton);
	return d;
}
} // namespace

TEST_CASE("players: newGame builds the players, teams, relationships and default teams from a SidesList (ZH PlayerList::newGame)")
{
	LogicWorld lw;
	SidesList sides;
	sides.sides.push_back(side("", "FactionNeutral", false));
	sides.sides.push_back(side("Player_1", "FactionA", true, "Player_2", ""));
	sides.sides.push_back(side("Player_2", "FactionB", false, "Player_1", ""));
	sides.sides.push_back(side("Player_3", "FactionB", false, "", "Player_2 Player_9"));
	sides.teams.push_back(team("team", "", true));
	sides.teams.push_back(team("teamPlayer_1", "Player_1", true));
	sides.teams.push_back(team("teamPlayer_2", "Player_2", true));
	sides.teams.push_back(team("squad", "Player_2", false));
	const std::vector<std::string> errors = lw.players.newGame(sides, 3000);
	// Player_3 has no default team in the Teams chunk; Player_9 does not exist
	REQUIRE(errors.size() == 2);
	CHECK(errors[0].find("unknown ally 'Player_9'") != std::string::npos);
	CHECK(errors[1].find("no default team 'teamPlayer_3'") != std::string::npos);
	PlayerList &pl = lw.players;
	REQUIRE(pl.getPlayerCount() == 4);
	Player *p1 = pl.findPlayerWithName("Player_1"), *p2 = pl.findPlayerWithName("Player_2"), *p3 = pl.findPlayerWithName("Player_3");
	CHECK(p1->getRelationship(p2) == ENEMIES);
	CHECK(p2->getRelationship(p1) == ENEMIES);
	CHECK(p3->getRelationship(p2) == ALLIES);
	CHECK(p1->getRelationship(p3) == NEUTRAL); // nothing says anything: NEUTRAL (ZH Player::getRelationship)
	CHECK(p1->getRelationship(p1) == ALLIES);
	CHECK(p1->getDefaultTeam()->getName() == "teamPlayer_1");
	CHECK(pl.getNeutralPlayer()->getDefaultTeam()->getName() == "team");
	CHECK(p3->getDefaultTeam() == nullptr);
	CHECK(pl.getLocalPlayer() == p1);
	CHECK(p1->getMoney()->countMoney() == 1500);
	CHECK(p2->getMoney()->countMoney() == 3000); // FactionB has no money: the game's default
	// a team that is not a singleton gets its team on the first lookup (ZH TeamFactory::findTeam), owned by the prototype's player;
	// "Owner/team" names find a team by owner and name (RW 0x7A7483)
	Team *squad = pl.teams().findTeam("Player_2/squad");
	REQUIRE(squad);
	CHECK(squad->getControllingPlayer() == p2);
	CHECK(pl.teams().findTeam("Player_2/squad") == squad);
	CHECK(pl.teams().findTeam("Player_3/squad") == nullptr); // the owner part must match
	CHECK(pl.teams().findTeam("Player_2/teamPlayer_2") == p2->getDefaultTeam());
	CHECK(pl.teams().findTeam("Player_1/teamPlayer_2") == nullptr); // the owner part must match
	CHECK(pl.teams().findTeam("/team") == pl.getNeutralPlayer()->getDefaultTeam());
	CHECK(pl.findTeamOrPlayerDefaultTeam("teamPlayer_2") == p2->getDefaultTeam());
	CHECK(pl.findTeamOrPlayerDefaultTeam("Player_2") == p2->getDefaultTeam()); // an owner may name a player (ZH validateTeam)
	CHECK(pl.findTeamOrPlayerDefaultTeam("nobody") == nullptr);
}

TEST_CASE("players: a side naming a missing PlayerTemplate is reported (ZH: an obsolete map)")
{
	LogicWorld lw;
	SidesList sides;
	sides.sides.push_back(side("", "FactionNeutral", false));
	sides.sides.push_back(side("Lost", "FactionZ", true));
	sides.teams.push_back(team("team", "", true));
	sides.teams.push_back(team("teamLost", "Lost", true));
	const std::vector<std::string> errors = lw.players.newGame(sides, 100);
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].find("PlayerTemplate 'FactionZ' not found") != std::string::npos);
	CHECK(lw.players.findPlayerWithName("Lost")->getMoney()->countMoney() == 0); // no template, no money rule
}
