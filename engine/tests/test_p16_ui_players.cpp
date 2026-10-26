// OpenBFME tests: extended player-status, tribute and score presentation. GPL-3.0.

#include "StartShellFx.h"
#include "Common/ExtendedPlayerColors.h"
#include "GameClient/GUI/AptScreens/AptTimeLine.h"
#include "GameClient/GUI/AptScreens/AptDisconnectScreen.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/TributeInfo.h"

#include <algorithm>

namespace
{
struct ExtendedTribute : TributeSource
{
	int capacity = 16;
	std::vector<TributePlayerRow> players;
	bool localActive() const override { return true; }
	bool transferAllowed() const override { return true; }
	int rowLimit() const override { return capacity > 8 ? capacity : TributeSource::rowLimit(); }
	std::vector<TributePlayerRow> tributePlayers() override { return players; }
	void send(int, int, std::uint32_t) override {}
};
}

TEST_CASE("p16 ui disconnect: fifteen remote rows preserve holes and original kick indices")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	AptDisconnectScreen screen(*fx.wm, *fx.shell);
	std::vector<AptDisconnectScreen::Row> rows(15);
	for (int i = 0; i < 15; ++i)
	{
		rows[(size_t)i] = { true, "Player " + std::to_string(i + 2), i, 100 - i * 5, true };
	}
	rows[9] = {}; // a hole retains its translated position
	CHECK_FALSE(screen.apply(rows, 16));
	fx.tick(90);
	REQUIRE(screen.apply(rows, 16));
	REQUIRE(screen.rowRange());
	std::vector<int> kicks;
	screen.onKick = [&](int row) { kicks.push_back(row); };
	starttest::chooseComboRow(fx, screen.rowRange(), 8);
	fx.tick(30);
	CHECK(screen.firstRow() == 8);
	CHECK(*fx.wm->aptText("DisconnectScreen::PlayerName0") == "Player 10");
	CHECK(*fx.wm->aptText("DisconnectScreen::PlayerName1") == " ");
	CHECK(*fx.wm->aptText("DisconnectScreen::PlayerName6") == "Player 16");
	CHECK(*fx.wm->aptText("DisconnectScreen::VotesReceived6") == "14");
	auto *bar = fx.at(screen.level(), "UIClip.ProgressBars.Bar6");
	REQUIRE(bar);
	AptValue percent;
	REQUIRE(bar->getMember("textPercent", percent));
	CHECK(percent.toString() == "30%");
	auto *button = starttest::firstButtonIn(fx.at(screen.level(), "KickButtons.Kick6"));
	REQUIRE(button);
	starttest::clickButton(fx, *button);
	CHECK(kicks == std::vector<int>{ 14 });
	fx.wm->fscommand("AptDisconnectScreen::Kick", "1");
	CHECK_FALSE(screen.error().empty());
	CHECK(kicks == std::vector<int>{ 14 });
	CHECK_FALSE(screen.apply(rows, 8));
	CHECK_FALSE(screen.error().empty());
	rows.resize(8);
	REQUIRE(screen.apply(rows, 9));
	CHECK(screen.firstRow() == 0);
	starttest::chooseComboRow(fx, screen.rowRange(), 1);
	CHECK(screen.firstRow() == 1);
	CHECK(*fx.wm->aptText("DisconnectScreen::PlayerName6") == "Player 9");
	std::array<AptDisconnectScreen::Row, 7> legacy{};
	REQUIRE(screen.apply(legacy));
	CHECK(screen.rowRange() == nullptr);
	CHECK(screen.firstRow() == 0);
	for (const char *bad : { "7", "-1", "1x" })
	{
		fx.wm->fscommand("AptDisconnectScreen::Kick", bad);
		CHECK_FALSE(screen.error().empty());
	}
	CHECK(kicks == std::vector<int>{ 14 });
	CHECK(fx.wm->noteCount("invoke-failed") == 0);
}

TEST_CASE("p16 ui loading: sixteen cards retain colours, teams and progress through the real range control")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	SkirmishGameInfo game;
	game.mapStartCount = 16;
	for (int i = 0; i < 16; ++i)
	{
		auto &slot = game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = asciiToU16("Player " + std::to_string(i + 1));
		slot.playerTemplate = 0;
		slot.color = i;
		slot.teamNumber = i / 2;
	}
	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text, [&](const std::string &n) { return fx.skins.images.findImageByName(n) != nullptr; });
	REQUIRE(fx.loadScreen.error.empty());
	REQUIRE(fx.loadScreen.cards[15].occupied);
	CHECK(fx.loadScreen.cards[15].playerName == u"Player 16");
	CHECK(fx.loadScreen.cards[15].color == ExtendedPlayerColors::additions[5].rgb);
	fx.shell->push("LoadScreen.apt");
	fx.tick(90);
	auto *screen = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(screen);
	CHECK(screen->cardCount() == 16);
	REQUIRE(screen->cardRange());
	starttest::chooseComboRow(fx, screen->cardRange(), 8);
	fx.tick(3);
	for (const char *column : { "Rank", "PlayerName", "TeamNumber", "ArmyName", "Level", "Fellowship" })
	{
		const std::string base = "UIClip." + std::string(column) + ".";
		auto *first = fx.at(screen->level(), base + "0");
		auto *last = fx.at(screen->level(), base + "15");
		REQUIRE(first);
		REQUIRE(last);
		CHECK_FALSE(first->globallyVisible());
		CHECK(last->globallyVisible());
	}
	CHECK(screen->setProgress(15, 73));
	CHECK(screen->progressCalls().back() == std::pair<int, int>{ 15, 73 });
	auto *bar = fx.at(screen->level(), "UIClip.ProgressBars.Bar15");
	REQUIRE(bar);
	CHECK(bar->globallyVisible());
	auto *nameText = fx.at(screen->level(), "UIClip.PlayerName.15.StringInst");
	REQUIRE(nameText);
	AptValue nameValue;
	REQUIRE(nameText->getMember("stringValue", nameValue));
	CHECK(nameValue.toString() == "$LoadingScreen::PlayerName15");
	std::string shown;
	CHECK(fx.wm->aptTextShown("LoadingScreen::PlayerName15", shown));
	CHECK(shown == "Player 16");
	AptValue percent;
	REQUIRE(bar->getMember("targetLevel", percent));
	CHECK(percent.toNumber() == 73);
	AptValue imageType;
	REQUIRE(fx.at(screen->level(), "UIClip.Level.15")->getMember("_type", imageType));
	CHECK(imageType.toString() == "RenderImage");
	CHECK(fx.wm->errors().empty());
	for (const auto &note : fx.wm->apt().notes())
	{
		if (note.kind == "call-without-function") CHECK(note.detail.find("SetPercent") == std::string::npos);
	}
	CHECK_FALSE(screen->setProgress(16, 20));
	CHECK_FALSE(screen->setProgress(-1, 20));
	REQUIRE(fx.wm->aptText("LoadingScreen::TeamNumber15"));
	CHECK(*fx.wm->aptText("LoadingScreen::TeamNumber15") == "8");
	game.mapStartCount = 8;
	CHECK_FALSE(makeLoadScreenInfo(game, fx.setup, &fx.text).error.empty());
	for (int i = 8; i < 16; ++i) game.slots[i].state = SLOT_CLOSED;
	fx.loadScreen = makeLoadScreenInfo(game, fx.setup, &fx.text);
	REQUIRE(fx.loadScreen.error.empty());
	fx.shell->pop();
	fx.tick(30);
	fx.shell->push("LoadScreen.apt");
	fx.tick(90);
	screen = dynamic_cast<AptLoadScreen *>(fx.shell->top());
	REQUIRE(screen);
	CHECK(screen->cardCount() == 8);
	CHECK(screen->cardRange() == nullptr);
}

TEST_CASE("p16 ui status: all sixteen slots retain names and effective colours with numeric extended teams")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	SkirmishGameInfo game;
	game.mapStartCount = 16;
	PlayerStatusInput input;
	input.game = &game;
	input.localSlot = 0;
	input.showRandomColor = false;
	REQUIRE(fx.setup.colors().size() == 10);
	for (int i = 0; i < 16; ++i)
	{
		auto &slot = game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = asciiToU16("Player " + std::to_string(i + 1));
		slot.playerTemplate = slot.origPlayerTemplate = 0;
		slot.color = slot.origColor = i;
		slot.teamNumber = i / 2;
		input.slots[i].hasPlayer = true;
	}
	PlayerStatusInfo status = makePlayerStatusInfo(input, fx.setup, &fx.text);
	REQUIRE(status.rows.size() == 16);
	for (int i = 0; i < 16; ++i)
	{
		CAPTURE(i);
		const auto &row = status.rows[i];
		CHECK(row.slot == i);
		CHECK(row.fields[0] == "Player " + std::to_string(i + 1));
		const auto rgb = i < 10 ? fx.setup.colors()[i].rgb : ExtendedPlayerColors::additions[i - 10].rgb;
		CHECK((std::uint32_t)row.color == (0xFF000000u | rgb));
		const auto expectedTeam = i < 8 ? "Team " + std::to_string(i / 2 + 1) : std::to_string(i / 2 + 1);
		CHECK(row.fields[2] == expectedTeam);
	}
	fx.environment.playerStatus = &status;
	fx.shell->push("PlayerTribute.apt");
	fx.tick(90);
	auto *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	REQUIRE(screen->statusPageLoaded());
	std::string count;
	CHECK(fx.wm->getExtern("_level" + std::to_string(screen->level()) + ".OuterFrame.Pages.StatusPage_NumOfPlayers", count) == AptExternResult::Value);
	CHECK(count == "16");
	CHECK(screen->statusRowsShown() == 16);
	REQUIRE(screen->statusRange());
	CHECK(GadgetComboBoxGetLength(screen->statusRange()) == 9);
	GadgetComboBoxSetSelectedPos(screen->statusRange(), 8, true);
	fx.tick(3);
	auto *last = fx.at(screen->level(), "OuterFrame.Pages.StatusPage.Table.15");
	REQUIRE(last);
	CHECK(last->globallyVisible());
	CHECK_FALSE(fx.at(screen->level(), "OuterFrame.Pages.StatusPage.Table.0")->globallyVisible());
}

TEST_CASE("p16 ui tribute: extended games expose all allied rows while legacy sources keep the retail limit")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	ExtendedTribute game;
	SUBCASE("extended") { game.capacity = 16; }
	SUBCASE("legacy") { game.capacity = 8; }
	for (int i = 0; i < game.capacity; ++i)
	{
		TributePlayerRow row;
		row.playerIndex = i + 3;
		row.name = "Player " + std::to_string(i + 1);
		row.cash = 1000;
		row.local = i == 0;
		game.players.push_back(row);
	}
	PlayerStatusInfo status;
	fx.environment.playerStatus = &status;
	fx.environment.tribute = &game;
	fx.shell->push("PlayerTribute.apt");
	fx.tick(90);
	auto *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	auto *tab = starttest::firstButtonIn(fx.at(screen->level(), "OuterFrame.Pages.tributeTab"));
	REQUIRE(tab);
	starttest::clickButton(fx, *tab);
	fx.tick(90);
	const auto rows = screen->tributeRows();
	REQUIRE(rows.size() == (size_t)game.rowLimit());
	CHECK(rows.back().playerIndex == game.rowLimit() + 2);
	CHECK(rows.front().local);
	if (game.capacity > 8)
	{
		REQUIRE(screen->tributeRange());
		GadgetComboBoxSetSelectedPos(screen->tributeRange(), 9, true);
		fx.tick(3);
		CHECK(screen->setRowAmountBySlider(15, 100));
		CHECK(screen->tributeRows()[15].amount == 100);
	}
	else CHECK_FALSE(screen->tributeRange());
}

TEST_CASE("p16 ui score: colour faction graph focus and statistics reach entry sixteen")
{
	OPENBFME_REQUIRE_START(s);
	starttest::ShellFx fx(*s, 1);
	ScoreScreenData score;
	score.valid = true;
	score.type = 2;
	for (int i = 0; i < 16; ++i)
	{
		ScoreScreenData::Entry entry;
		entry.playerIndex = i + 3;
		entry.name = "Player " + std::to_string(i + 1);
		entry.side = "Men";
		entry.color = i < 10 ? fx.setup.colors()[i].rgb : ExtendedPlayerColors::additions[i - 10].rgb;
		entry.local = i == 0;
		entry.unitsBuilt = i + 1;
		score.entries.push_back(entry);
	}
	fx.environment.scoreScreen = &score;
	fx.shell->push("TimeLine.apt");
	fx.tick(90);
	auto *screen = dynamic_cast<AptTimeLine *>(fx.shell->top());
	REQUIRE(screen);
	REQUIRE(screen->playerRange());
	starttest::chooseComboRow(fx, screen->playerRange(), 8);
	fx.tick(3);
	auto *last = fx.at(screen->level(), "Main.Button15");
	REQUIRE(last);
	CHECK(last->globallyVisible());
	CHECK_FALSE(fx.at(screen->level(), "Main.Button0")->globallyVisible());
	float x0, y0, x1, y1, x, y;
	REQUIRE(last->contentBounds(x0, y0, x1, y1));
	last->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	starttest::clickAt(fx, x, y);
	fx.tick(40);
	AptValue focus;
	REQUIRE(fx.wm->apt().level(screen->level())->getMember("FocusPlayer", focus));
	CHECK(focus.toInteger() == 15);
	CHECK(screen->graphFocus(15) == 1.0f);
	std::string value;
	CHECK(fx.wm->getExtern("TimeLine:PlayerColor:15", value) == AptExternResult::Value);
	CHECK(value == "0x633b20");
	CHECK(fx.wm->getExtern("TimeLine:PlayerFaction:15", value) == AptExternResult::Value);
	CHECK(value == "Men");
	fx.tick(300);
	auto *more = fx.at(screen->level(), "Main.MainButtons.More");
	REQUIRE(more);
	REQUIRE(more->globallyVisible());
	REQUIRE(more->contentBounds(x0, y0, x1, y1));
	more->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	starttest::clickAt(fx, x, y);
	fx.tick(90);
	REQUIRE(screen->statsList());
	CHECK_FALSE(last->globallyVisible());
	CHECK(screen->playerRange()->winIsHidden());
	auto *next = fx.at(screen->level(), "Main.PlayerCarrots.Carrot1");
	REQUIRE(next);
	REQUIRE(next->globallyVisible());
	REQUIRE(next->contentBounds(x0, y0, x1, y1));
	next->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
	for (int i = 0; i < 14; ++i)
	{
		fx.wm->postMouseMove(x, y);
		fx.tick(2);
		fx.wm->postMouseButton(true);
		fx.tick(2);
		fx.wm->postMouseButton(false);
		fx.tick(20);
	}
	CHECK(std::find(screen->shownColumns().begin(), screen->shownColumns().end(), 15) != screen->shownColumns().end());
	REQUIRE_FALSE(screen->statRows().empty());
	for (const auto &row : screen->statRows())
	{
		CHECK(row.values.size() == 16);
	}
}
