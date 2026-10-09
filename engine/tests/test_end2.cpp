// OpenBFME unit tests. GPL-3.0.
// Lane END-2: the score screen's statistics page (the cells of RW 0x9F07C7 / 0x9F0885 / 0x9F0E7A, the rows of RW 0x9CDEC1, the shown entries of RW 0x9CD723),
// the ScoreKeeper's spend by kind (RW 0x79DFC2), favourite unit and hero counts (RW 0x79E2A8 / 0x79E404), and the quit menu's surrender (MSG_SELF_DESTRUCT,
// RW 0x77CA3D) on two peers of one retail game.

#include "doctest.h"

#include "EndTestUtil.h"
#include "StartTestUtil.h"

#include "Common/ScoreKeeper.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/VictoryConditions.h"

#include <map>
#include <string>

using namespace endtest;

namespace
{
// a game text stand-in: the singular labels as retail's English text has them, the plurals with %d
std::string fakeText(const std::string &label)
{
	static const std::map<std::string, std::string> t = { { "TIME:Day", "1 Day" }, { "TIME:Days", "%d Days" }, { "TIME:Hour", "1 Hour" },
		{ "TIME:Hours", "%d Hours" }, { "TIME:Minute", "1 Minute" }, { "TIME:Minutes", "%d Minutes" }, { "TIME:Second", "1 Second" },
		{ "TIME:Seconds", "%d Seconds" }, { "GUI:None", "None" } };
	auto it = t.find(label);
	return it == t.end() ? label : it->second;
}

const ThingTemplate *firstTemplateOfKind(GameLogic &logic, const char *kind, const char *notKind = nullptr)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	const int notBit = notKind ? ObjectTemplateInfoBuilder::kindOfIndex(notKind) : -1;
	for (const ThingTemplate *tt : logic.things().templates())
	{
		const FieldValue *dn = tt->findField("DisplayName");
		if (!dn || std::holds_alternative<std::monostate>(*dn) || tt->getName().rfind("CINE_", 0) == 0)
		{
			continue; // a template the player can build: it has a display name
		}
		const KindOfMaskType &k = logic.templateInfo(tt->getFinalOverride()).kindOf;
		if (MaskTest(k, (unsigned)bit) && (notBit < 0 || !MaskTest(k, (unsigned)notBit)))
		{
			return tt;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("end2 stats: the cells of RW 0x9F07C7 (integer), 0x9F0885 (ratio) and 0x9F0E7A (duration)")
{
	const ScoreScreenData::StatCell i = ScoreScreenData::intCell(42);
	CHECK(i.text == "42");
	CHECK(i.value == 42.0f);
	CHECK(ScoreScreenData::intCell(-1).value == 4294967296.0f - 1.0f); // the unsigned conversion (RW 0xBD8698)
	// a numerator under 1e-4 is "-" with value 0; a denominator under 1e-4 is taken as 1
	CHECK(ScoreScreenData::ratioCell(0.0f, 5.0f, ".").text == "-");
	CHECK(ScoreScreenData::ratioCell(0.0f, 5.0f, ".").value == 0.0f);
	CHECK(ScoreScreenData::ratioCell(7.0f, 2.0f, ".").text == "3.50");
	CHECK(ScoreScreenData::ratioCell(7.0f, 0.0f, ".").text == "7.00");
	CHECK(ScoreScreenData::ratioCell(1.0f, 3.0f, ",").text == "0,33"); // the locale's separator replaces the point
	// the duration: the parts that are not zero, the singular label as it is, the plural with the count, separated and trimmed; nothing is "0 "
	CHECK(ScoreScreenData::timeCell(0, fakeText).text == "0 ");
	CHECK(ScoreScreenData::timeCell(1, fakeText).text == "1 Second");
	CHECK(ScoreScreenData::timeCell(125, fakeText).text == "2 Minutes 5 Seconds");
	CHECK(ScoreScreenData::timeCell(3600 + 60, fakeText).text == "1 Hour 1 Minute");
	CHECK(ScoreScreenData::timeCell(2 * 86400 + 3 * 3600 + 61, fakeText).text == "2 Days 3 Hours"); // a day drops the minutes and seconds (RW 0x9F0F2C)
	CHECK(ScoreScreenData::timeCell(125, fakeText).value == 125.0f);
}

TEST_CASE("end2 stats: the 24 rows of RW 0x9CDEC1, the rows without values, the best value and the shown entries of RW 0x9CD723")
{
	ScoreScreenData d;
	d.valid = true;
	d.entries.resize(2);
	d.entries[0].sessionSeconds = 300;
	d.entries[0].structuresBuilt = 4;
	d.entries[0].unitsBuilt = 10;
	d.entries[0].unitsLost = 4;
	d.entries[0].unitsDestroyed = 8;
	d.entries[0].moneyEarned = 2500;
	d.entries[0].moneySpent = 2000;
	d.entries[0].structuresDestroyed = 2;
	d.entries[1].sessionSeconds = 200;
	d.entries[1].structuresBuilt = 6;
	d.entries[1].favoriteUnitFound = true;
	d.entries[1].favoriteUnitLabel = "OBJECT:Soldier";
	const std::vector<ScoreScreenData::StatRow> rows = d.statRows(fakeText);
	REQUIRE(rows.size() == 24);
	CHECK(rows[0].label == "STAT:RTS_SESSION_LENGTH");
	CHECK(rows[23].label == "STAT:RTS_HEROES_LOST");
	CHECK_FALSE(rows[17].shown); // the time to the last spell level: no value
	CHECK_FALSE(rows[21].shown); // the purchases of each hero: no value
	CHECK(rows[0].values[0].text == "5 Minutes");
	CHECK(rows[0].best == 300.0f);
	CHECK(rows[1].values[0].text == "4");
	CHECK(rows[1].best == 6.0f); // the second entry's 6 is the row's best
	CHECK(rows[7].values[0].text == "2.00");  // units killed / units lost
	CHECK(rows[7].values[1].text == "-");
	CHECK(rows[9].values[0].text == "None");  // no favourite unit: GUI:None
	CHECK(rows[9].values[1].text == "OBJECT:Soldier");
	CHECK(rows[10].values[0].text == "500.00"); // money per whole minute
	CHECK(rows[18].values[0].text == "0.50"); // (2 structures + 8 units) * 100 / 2000 spent
	CHECK(rows[19].values[0].text == "2.50");  // units built / lost
	CHECK(rows[0].values.size() == 8);         // RW 0x9F19DC(8)
	// SetPlayerFocus: the local player stays in the first column, the others from the argument; an observer's three columns all move
	CHECK(ScoreScreenData::focusColumns(1, 2, false, { 0, 0, 0 }) == std::array<int, 3>{ 0, 1, -1 });
	CHECK(ScoreScreenData::focusColumns(0, 5, false, { 0, 0, 0 }) == std::array<int, 3>{ 0, 1, 2 });
	CHECK(ScoreScreenData::focusColumns(3, 5, false, { 0, 0, 0 }) == std::array<int, 3>{ 0, 3, 4 });
	CHECK(ScoreScreenData::focusColumns(9, 5, false, { 0, 0, 0 }) == std::array<int, 3>{ 0, 3, 4 }); // past the end: count - 2
	CHECK(ScoreScreenData::focusColumns(2, 5, true, { 0, 0, 0 }) == std::array<int, 3>{ 2, 3, 4 });
	CHECK(ScoreScreenData::focusColumns(1, 0, false, { 0, 0, 0 }) == std::array<int, 3>{ 0, 0, 0 }); // no entries: unchanged
}

TEST_CASE("end2 retail: the spend by kind (RW 0x79DFC2), the HORDE map, the favourite unit and the hero counts on a retail game")
{
	OPENBFME_REQUIRE_START(s);
	Peer p;
	p.load(*s, humanVersus(*s, "FactionMen", "FactionMordor", 31u), 0);
	for (int f = 0; f < 5; ++f)
	{
		p.step(200.0 * (f + 1));
	}
	GameLogic &logic = p.logic();
	Player *gondor = playerAtStart(logic, 0);
	REQUIRE(gondor != nullptr);
	ScoreKeeper &k = gondor->getScoreKeeper();
	const ThingTemplate *hero = firstTemplateOfKind(logic, "HERO");
	const ThingTemplate *structure = firstTemplateOfKind(logic, "STRUCTURE", "HERO");
	const ThingTemplate *horde = firstTemplateOfKind(logic, "HORDE", "HORDE_MONSTER");
	REQUIRE(hero);
	REQUIRE(structure);
	REQUIRE(horde);
	k.addMoneySpentByKind(logic, hero, 1000);
	k.addMoneySpentByKind(logic, nullptr, 50); // no template: the heroes' field
	k.addMoneySpentByKind(logic, structure, 300);
	k.addMoneySpentByKind(logic, horde, 200);
	k.addMoneySpentByKind(logic, horde, -200); // the queue cancel takes it back
	CHECK(k.moneySpentOnHeroes() == 1050);
	CHECK(k.moneySpentOnStructures() == 300);
	CHECK(k.moneySpentOnUnits() == 0);
	CHECK(k.moneySpentByKind() == 1350);
	// a horde made while counting: the HORDE map counts it and it is the favourite (no unit was built before)
	CHECK(k.counting());
	CHECK(k.favoriteUnit(logic).empty());
	Object *o = logic.things().newObject(logic, horde, gondor->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(o != nullptr);
	CHECK(k.hordesBuiltByTemplate().count(horde->getName()) == 1);
	CHECK(k.favoriteUnit(logic) == horde->getName());
	CHECK(k.heroesBuilt(logic) == 0);
	Object *h = logic.things().newObject(logic, hero, gondor->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(h != nullptr);
	CHECK(k.heroesBuilt(logic) == (k.builtByTemplate().count(hero->getName()) ? k.builtByTemplate().at(hero->getName()) : 0));
	MESSAGE("hero " << hero->getName() << " counted " << k.heroesBuilt(logic) << ", horde " << horde->getName() << ", favourite " << k.favoriteUnit(logic));
	// the score screen carries them
	std::vector<int> slots;
	for (const std::string &name : p.game->report().startSlotPlayers)
	{
		const Player *pl = name.empty() ? nullptr : logic.players().findPlayerWithName(name);
		slots.push_back(pl ? pl->getPlayerIndex() : -1);
	}
	const ScoreScreenData d = ScoreScreenData::build(logic, 2, slots, {});
	REQUIRE_FALSE(d.entries.empty());
	CHECK(d.entries[0].spentHeroes == 1050);
	CHECK(d.entries[0].spentStructures == 300);
	CHECK_FALSE(d.entries[0].favoriteUnitLabel.empty());
}

TEST_CASE("end2 retail: the quit menu's surrender (MSG_SELF_DESTRUCT false) and exit (true) on two peers of one game: the same frame, the same defeat, the end screens")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage message = humanVersus(*s, "FactionMen", "FactionMordor", 77u);
	Peer peers[2];
	peers[0].load(*s, message, 0);
	peers[1].load(*s, message, 1);
	Player *mordor[2] = { playerAtStart(peers[0].logic(), 1), playerAtStart(peers[1].logic(), 1) };
	REQUIRE(mordor[0]);
	REQUIRE(mordor[1]);
	double now = 0.0;
	for (int f = 0; f < 20; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
	}
	// the Mordor player (peer 1's local player) surrenders: its command reaches both peers' command lists for the same frame (the lockstep's job)
	for (int k = 0; k < 2; ++k)
	{
		GameMessage m(MSG_SELF_DESTRUCT, mordor[k]->getPlayerIndex());
		m.appendBooleanArgument(false);
		peers[k].game->commands().append(m);
	}
	unsigned defeated = 0;
	for (int f = 0; f < 20; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
		REQUIRE(peers[0].logic().computeStateHash() == peers[1].logic().computeStateHash());
		if (!defeated && mordor[0]->isDefeated())
		{
			defeated = peers[0].logic().getFrame();
		}
	}
	CHECK(defeated > 0);
	CHECK(mordor[1]->isDefeated());
	const VictoryConditions &v = static_cast<const GameLogic &>(peers[0].logic()).victory();
	CHECK(v.selfDestructs() == 1);
	CHECK(v.singleAllianceRemaining());
	CHECK(peers[0].requested(EndGameRequest::SHOW_END_GAME, "APT:EndVictorious"));
	CHECK(peers[1].requested(EndGameRequest::SHOW_END_GAME, "APT:EndDefeat"));
	// a malformed message (no argument) is counted unhandled, not executed; a second surrender of a defeated player changes nothing more
	GameMessage bad(MSG_SELF_DESTRUCT, mordor[0]->getPlayerIndex());
	peers[0].game->commands().append(bad);
	peers[0].step(now += 200.0);
	CHECK(v.selfDestructs() == 1);
	// Exit with the transfer flag and no living ally: the sender is killed (RW 0x77CADD)
	Peer third;
	third.load(*s, humanVersus(*s, "FactionElves", "FactionMordor", 9u), 0);
	third.step(200.0);
	Player *elves = playerAtStart(third.logic(), 0);
	GameMessage exitMsg(MSG_SELF_DESTRUCT, elves->getPlayerIndex());
	exitMsg.appendBooleanArgument(true);
	third.game->commands().append(exitMsg);
	for (int f = 0; f < 30; ++f)
	{
		third.step(400.0 + 200.0 * f);
	}
	CHECK(elves->isDefeated());
	CHECK(static_cast<const GameLogic &>(third.logic()).victory().assetTransfersUnported() == 0);
	CHECK(third.requested(EndGameRequest::SHOW_END_GAME, "APT:EndDefeat"));
}
