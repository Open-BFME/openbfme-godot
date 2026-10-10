// OpenBFME retail tests (lane PLAY-1): an upgrade button the player cannot pay for. RotWK's processCommandUI asks canAffordUpgrade(player, upgrade, object,
// show the reason) before it appends MSG_QUEUE_UPGRADE (RW 0x940E45 -> RW 0x66F492): short of the cost the UI says "GUI:NotEnoughMoneyToUpgrade" and nothing
// is sent; with the money the click queues it. Found by the PLAY-1 tour (a barracks level bought with 425 of 500). GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PeImage.h"

#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameHud.h"
#include "GameClient/GUI/ShellServices.h"

#include <string>
#include <vector>

using namespace hudtest;
using retailtest::PeImage;

namespace
{
const ControlBarButton *upgradeButton(const ControlBar &bar, const std::string &upgrade)
{
	for (const auto *list : { &bar.palantirButtons(), &bar.sideButtons() })
	{
		for (const ControlBarButton &b : *list)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_OBJECT_UPGRADE && b.button->m_upgradeName == upgrade)
			{
				return &b;
			}
		}
	}
	return nullptr;
}

size_t queued(const std::vector<std::string> &log)
{
	size_t n = 0;
	for (const std::string &l : log)
	{
		n += l.rfind("MSG_QUEUE_UPGRADE", 0) == 0 ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("play1 hud upgrade: a button short of the upgrade's cost says GUI:NotEnoughMoneyToUpgrade and sends nothing; with the money it queues")
{
	if (!haveWorld("play1 hud upgrade"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig rig(s);
	RecordingShellServices services;
	InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	std::string error;
	REQUIRE_MESSAGE(hud.boot(&error), error);
	const auto worldContext = hud.enterContext();
	const Coord3D spot = rig.freeSpot(2800, 1400, 260.0f);
	Object *barracks = rig.make("GondorBarracks", spot.x, spot.y);
	REQUIRE(barracks);
	rig.lookAt(*barracks->getPosition());
	auto frames = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			hud.update(0.2);
			rig.game->advance(0.2);
		}
	};
	int timeMs = 1000;
	auto click = [&](int x, int y) {
		hud.input().mouseMove(x, y, 0);
		hud.input().update();
		timeMs += 20;
		hud.input().mouseButton(HudInput::Button::Left, true, x, y, 0, timeMs);
		hud.input().update();
		timeMs += 20;
		hud.input().mouseButton(HudInput::Button::Left, false, x, y, 0, timeMs);
		hud.input().update();
	};
	frames(3);
	click(rig.screenOf(*barracks->getPosition()).x, rig.screenOf(*barracks->getPosition()).y);
	frames(3);
	REQUIRE(hud.input().ui().firstSelected() == barracks->getID());
	const ControlBarButton *b = upgradeButton(hud.controlBar(), "Upgrade_GondorBarracksLevel2");
	REQUIRE(b);
	const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade("Upgrade_GondorBarracksLevel2");
	REQUIRE(u);
	const int cost = u->calcCostToBuild(rig.local, barracks);
	REQUIRE(cost > 0);
	// short of the cost by one
	rig.local->getMoney()->withdraw(rig.local->getMoney()->countMoney());
	rig.local->depositMoney((std::uint32_t)cost - 1);
	const size_t before = queued(hud.input().messageLog());
	const size_t said = hud.input().ui().messages().size();
	CHECK_FALSE(hud.controlBar().pressButton(b->slot, b->inPalantir));
	frames(1);
	CHECK(queued(hud.input().messageLog()) == before);
	REQUIRE(hud.input().ui().messages().size() == said + 1);
	CHECK(hud.input().ui().messages().back() == "GUI:NotEnoughMoneyToUpgrade");
	// with the money
	rig.local->depositMoney(1);
	b = upgradeButton(hud.controlBar(), "Upgrade_GondorBarracksLevel2");
	REQUIRE(b);
	CHECK(hud.controlBar().pressButton(b->slot, b->inPalantir));
	frames(1);
	CHECK(queued(hud.input().messageLog()) == before + 1);
}

TEST_CASE("play1 hud upgrade: the binary facts (processCommandUI asks canAffordUpgrade with the reason shown before MSG_QUEUE_UPGRADE)")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("play1 hud upgrade binary facts (RW_GAME_DAT unset)");
		return;
	}
	std::vector<std::uint8_t> code;
	REQUIRE(pe->read(0x940E40, 0x20, &code));
	// push 1; push ebx; push edi; push eax; call RW 0x66F492; test al, al; jz (leave); ... push 0x415 (MSG_QUEUE_UPGRADE)
	CHECK(code[0] == 0x6A);
	CHECK(code[1] == 0x01);
	CHECK(code[5] == 0xE8);
	const std::int32_t rel = (std::int32_t)(code[6] | (code[7] << 8) | (code[8] << 16) | ((std::uint32_t)code[9] << 24));
	CHECK((std::uint32_t)(0x940E4A + rel) == 0x66F492u);
	CHECK(code[0x1A] == 0x68);
	CHECK(code[0x1B] == 0x15);
	CHECK(code[0x1C] == 0x04);
	CHECK(pe->cstring(0xC10BD0) == "GUI:NotEnoughMoneyToUpgrade");
}
