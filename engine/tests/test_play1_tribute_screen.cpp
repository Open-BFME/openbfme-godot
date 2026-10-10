// OpenBFME unit tests. GPL-3.0.
// Lane PLAY-1: PlayerTribute.apt (AptPlayerTribute) over a fake game (TributeSource; the status rows are HUD-5's PlayerStatusInfo): the tribute rows with their sliders, a
// slider drag, Reset and Send through the retail movie. SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "StartShellFx.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/PlayerStatusInfo.h"
#include "GameClient/GUI/TributeInfo.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <functional>
#include <string>
#include <vector>

namespace
{
using namespace starttest;

struct FakeTribute : public TributeSource
{
	bool active = true;
	std::vector<TributePlayerRow> players;
	struct Sent
	{
		int from, to;
		std::uint32_t amount;
	};
	std::vector<Sent> sent;
	bool localActive() const override { return active; }
	bool allowed = true;
	bool transferAllowed() const override { return allowed; }
	std::vector<TributePlayerRow> tributePlayers() override { return players; }
	void send(int from, int to, std::uint32_t amount) override { sent.push_back({ from, to, amount }); }
};

// the Status page's rows (HUD-5's PlayerStatusInfo: the host builds them, GUI/PlayerStatusInfo.h)
PlayerStatusInfo fakeStatus()
{
	PlayerStatusInfo st;
	st.inSkirmish = true;
	for (const char *name : { "Owner", "Ally AI", "Enemy AI" })
	{
		PlayerStatusRow r;
		r.slot = (int)st.rows.size();
		r.fields[0] = name;
		r.fields[1] = "Random";
		r.fields[2] = "Team 1";
		r.fields[3] = "Alive";
		st.rows.push_back(r);
	}
	return st;
}

FakeTribute fakeGame()
{
	FakeTribute f;
	TributePlayerRow l;
	l.playerIndex = 3;
	l.name = "Owner";
	l.cash = 1000;
	l.local = true;
	TributePlayerRow al;
	al.playerIndex = 4;
	al.name = "Ally AI";
	al.cash = 500;
	f.players = { l, al };
	return f;
}
} // namespace

namespace
{
std::string field(ShellFx &fx, int level, int row, int field)
{
	const std::string name = "APT:_level" + std::to_string(level) + ".OuterFrame.Pages.TributePage.enabledContent.Table." + std::to_string(row) + "_field" +
							 std::to_string(field);
	const std::string *t = fx.wm->aptText(name);
	return t ? *t : std::string("<none>");
}

AptButtonInst *button(ShellFx &fx, int level, const std::string &path)
{
	return firstButtonIn(fx.at(level, path));
}
} // namespace

TEST_CASE("play1 tribute screen: the status rows, the tribute rows with their sliders, a slider drag, Send and Reset through PlayerTribute.apt")
{
	OPENBFME_REQUIRE_START(s);
	ShellFx fx(*s, 1);
	FakeTribute game = fakeGame();
	PlayerStatusInfo status = fakeStatus();
	fx.environment.tribute = &game;
	fx.environment.playerStatus = &status;
	fx.shell->push("PlayerTribute.apt");
	fx.tick(90);
	AptPlayerTribute *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	const int level = screen->level();
	CHECK(screen->statusRowsShown() == 3); // HUD-5's status page (RW 0x9151C3: one row per occupied slot) next to the tribute page
	AptButtonInst *tab = button(fx, level, "OuterFrame.Pages.tributeTab");
	REQUIRE(tab);
	clickButton(fx, *tab);
	fx.tick(90);
	// RW 0x914B54: the local player first, then the ally; each row's HorzSlider / TextEntry reach the screen through "<row>_InitSlider" / "_InitTextEntry"
	// (the placeholders' Load event names them, RW 0x915F5D registers them)
	std::vector<AptPlayerTribute::RowState> rows = screen->tributeRows();
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].playerIndex == 3);
	CHECK(rows[0].local);
	CHECK(rows[1].playerIndex == 4);
	CHECK(rows[1].slider);
	CHECK(rows[1].entry);
	CHECK(rows[1].maximum == 1000); // RW 0x91578C: the local cash
	CHECK(fx.wm->noteCount("unknown-screen-ref") == 0);
	CHECK(field(fx, level, 0, 0) == "Owner");
	CHECK(field(fx, level, 0, 1) == "1000");
	CHECK(field(fx, level, 1, 1) == "500");
	{
		// a player's drag on the ally row's slider (the press on the track's left end, the gadget's GSM_SLIDER_TRACK reaches the screen with the slider as
		// its first datum): the range is 0 .. 99999 (RW 0x914B07), so a short drag asks for more than the local cash and is held at it
		GameWindow *w = fx.wm->componentWindow("_level" + std::to_string(level) + ".OuterFrame.Pages.TributePage.enabledContent.Table.1.ColoredItems.Slider.instance1");
		REQUIRE(w);
		int x = 0, y = 0, cw = 0, ch = 0;
		w->winGetScreenPosition(&x, &y);
		w->winGetSize(&cw, &ch);
		const float py = (float)y + 8.0f; // above the thumb (ZH's HORIZONTAL_SLIDER_THUMB_POSITION puts it 10 below the track's top)
		fx.wm->postMouseMove((float)x + 8.0f, py);
		fx.tick(1);
		fx.wm->postMouseButton(true);
		fx.tick(1);
		for (int i = 1; i <= 8; ++i)
		{
			fx.wm->postMouseMove((float)x + 8.0f + cw * 0.2f * i / 8.0f, py);
			fx.tick(1);
		}
		fx.wm->postMouseButton(false);
		fx.tick(2);
		CHECK(screen->tributeRows()[1].amount == 1000u);
		CHECK(field(fx, level, 0, 1) == "0");
		CHECK(field(fx, level, 1, 1) == "1500");
	}
	// a drag to 300: the ally's row shows 800, the local row 700 (RW 0x9155C2)
	REQUIRE(screen->setRowAmountBySlider(1, 300));
	fx.tick(2);
	CHECK(screen->tributeRows()[1].amount == 300u);
	CHECK(field(fx, level, 0, 1) == "700");
	CHECK(field(fx, level, 1, 1) == "800");
	// beyond the local cash: held at the maximum
	REQUIRE(screen->setRowAmountBySlider(1, 5000));
	CHECK(screen->tributeRows()[1].amount == 1000u);
	// Reset (RW 0x915862): every amount back to 0
	REQUIRE(screen->setRowAmountBySlider(1, 120));
	AptButtonInst *resetButton = button(fx, level, "OuterFrame.Pages.TributePage.enabledContent.MainButtons.Reset");
	REQUIRE(resetButton);
	clickButton(fx, *resetButton);
	fx.tick(30);
	CHECK(screen->tributeRows()[1].amount == 0u);
	CHECK(field(fx, level, 0, 1) == "1000");
	// the cash is read live: the local player earns 200
	game.players[0].cash = 1200;
	fx.tick(2);
	CHECK(field(fx, level, 0, 1) == "1200");
	CHECK(screen->tributeRows()[1].maximum == 1200u);
	REQUIRE(screen->setRowAmountBySlider(1, 250));
	// Send: MSG_GIVE_MONEY(local, ally, amount) (RW 0x914DD6), then the movie closes the screen (_root.close(0))
	AptButtonInst *sendButton = button(fx, level, "OuterFrame.Pages.TributePage.enabledContent.MainButtons.Send");
	REQUIRE(sendButton);
	clickButton(fx, *sendButton);
	fx.tick(30); // the button's release animation ends in OnButtonClick
	REQUIRE(game.sent.size() == 1);
	CHECK(game.sent[0].from == 3);
	CHECK(game.sent[0].to == 4);
	CHECK(game.sent[0].amount == 250u);
	// the movie's close plays OuterFrame's "_exit", whose last frame calls "_level1_ReturnToGame" (the host pops the screen)
	for (int i = 0; i < 300 && fx.services.countRequests(ShellAction::TributeReturnToGame) == 0; ++i)
	{
		fx.tick(1);
	}
	CHECK(fx.services.countRequests(ShellAction::TributeReturnToGame) == 1);
}

TEST_CASE("play1 tribute screen: before NumMinutesBeforePlayersCanTransferMoney the tribute page stays disabled (no rows), then opens")
{
	OPENBFME_REQUIRE_START(s);
	ShellFx fx(*s, 1);
	FakeTribute game = fakeGame();
	game.allowed = false;
	fx.environment.tribute = &game;
	fx.shell->push("PlayerTribute.apt");
	fx.tick(90);
	AptPlayerTribute *screen = dynamic_cast<AptPlayerTribute *>(fx.shell->top());
	REQUIRE(screen);
	AptButtonInst *tab = button(fx, screen->level(), "OuterFrame.Pages.tributeTab");
	REQUIRE(tab);
	clickButton(fx, *tab);
	fx.tick(90);
	CHECK(screen->tributeRows().empty()); // RW 0x9164EB: no SetState("_enabled") yet
	game.allowed = true;
	fx.tick(30);
	CHECK(screen->tributeRows().size() == 2);
}
