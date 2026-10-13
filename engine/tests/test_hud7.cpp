// OpenBFME tests, lane HUD-7: the HUD items of a Discord tester's report (GitGut, v0.3.0-preview.6). GPL-3.0.
//   * the deselect click: RotWK's SelectionTranslator drops the selection on a left click that picks nothing (RW 0x83C44F .. 0x83C4B9 -> RW 0x7BDD53);
//   * the upgrade in progress: RotWK's availability 8 (RW 0x9431A6) shown as the _cantAfford clip (RW 0x943C40, RW 0x9D2C31);
//   * the hero recruit's cancel: the right press of a REVIVE bubble (processCommandUI RW 0x940B0E / 0x940C57) refunds the hero;
//   * the Sell button of the factions FACT-A's test does not cover (MSG_SELL RW 0x77B2C8, BuildAssistant RW 0x7966BB / 0x79468B);
//   * the radar's extent: RotWK's getExtent takes the largest boundary (W3DTerrainLogic slot 0x24, RW 0x4626A3; Radar::newMap RW 0x6D8EE4).
// The retail tests SKIP loudly without the installs; the extent case needs nothing.

#include "doctest.h"

#include "FactATestUtil.h"
#include "HudTestUtil.h"

#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarRadialMenu.h"
#include "Common/INI.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/HudObjects.h"
#include "GameClient/InGameHud.h"
#include "GameClient/Radar.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace hudtest;

namespace
{
bool logHas(const HudInput &in, const std::string &prefix)
{
	for (const std::string &l : in.messageLog())
	{
		if (l.compare(0, prefix.size(), prefix) == 0)
		{
			return true;
		}
	}
	return false;
}

float distance2d(const Coord3D &a, const Coord3D &b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}

// the radial bubble of `slot` (null when the ring does not show it)
const ControlBarRadialMenu::Button *bubble(const ControlBarRadialMenu &radial, int slot)
{
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (b.slot == slot)
		{
			return &b;
		}
	}
	return nullptr;
}

// a press of the bubble through the HUD's mouse, as the player clicks it
void pressBubble(facta::FactRig &r, const ControlBarRadialMenu::Button &b, HudInput::Button which)
{
	const int x = b.rect.x + b.rect.w / 2, y = b.rect.y + b.rect.h / 2;
	r.hud->mouseMove(x, y, 0);
	r.rig.timeMs += 300;
	r.hud->mouseButton(which, true, x, y, 0, r.rig.timeMs);
	r.rig.timeMs += 40;
	r.hud->mouseButton(which, false, x, y, 0, r.rig.timeMs);
	r.rig.timeMs += 300;
}

// the ring's ICON and rim (OVERLAY) ops of `slot`
struct BubbleLook
{
	bool found = false, grayscale = false;
	std::uint32_t iconColor = 0, rimColor = 0;
};
BubbleLook lookOf(const ControlBarRadialMenu &radial, int slot)
{
	BubbleLook l;
	for (const RadialDrawOp &op : radial.ops())
	{
		if (op.slot != slot)
		{
			continue;
		}
		if (op.kind == RadialDrawOp::ICON)
		{
			l.found = true;
			l.grayscale = op.grayscale;
			l.iconColor = op.color;
		}
		else if (op.kind == RadialDrawOp::OVERLAY)
		{
			l.rimColor = op.color;
		}
	}
	return l;
}

// the income the local player's buildings paid since the log was last cleared (the citadel's AutoDeposit: Economy::noteIncome), so a money check is exact
unsigned incomeSince(facta::FactRig &r)
{
	long long sum = 0;
	for (const Economy::IncomeEvent &e : r.logic().economy().incomeLog())
	{
		sum += e.playerIndex == r.local()->getPlayerIndex() ? e.amount : 0;
	}
	return (unsigned)sum;
}

// a window point whose topmost Apt button is the Palantir arc content of `slot` (InGameHud::commandContentAt's own test; the window is 1024 x 768)
bool arcContentPoint(facta::FactRig &r, int slot, int &x, int &y)
{
	Apt &apt = r.hud->windows().apt();
	for (int py = 380; py < 768; py += 3)
	{
		for (int px = 0; px < 1024; px += 3)
		{
			AptButtonInst *b = apt.input().hitTestButtons((float)px, (float)py);
			bool arc = false;
			int at = -1;
			if (b && r.hud->palantir()->contentSlotAt(b->targetPath(), arc, at) && arc && at == slot)
			{
				x = px;
				y = py;
				return true;
			}
		}
	}
	return false;
}

unsigned refundValue(const ThingTemplate &tt)
{
	const FieldValue *v = tt.getFinalOverride()->findField("RefundValue");
	const long long *i = v ? std::get_if<long long>(v) : nullptr;
	return i ? (unsigned)(*i & 0xFFFF) : 0u;
}
} // namespace

TEST_CASE("hud7 extent: RotWK's getExtent is the largest x and the largest y over every boundary, times 10 (RW 0x4626A3), not the active boundary")
{
	WorldHeightMap m;
	m.m_width = 500;
	m.m_height = 650;
	m.m_borderSize = 50;
	m.m_dataSize = m.m_width * m.m_height;
	m.m_data.assign((size_t)m.m_dataSize, 0);
	// map evil withered heath's four boundaries (tests/data/map-survey.json): the first is the smallest; the largest x and y come from the second
	m.m_boundaries = { { 239, 259 }, { 400, 550 }, { 400, 426 }, { 238, 259 } };
	MapChunks chunks;
	TerrainLogic t;
	t.init(m, chunks, nullptr);
	float x = 0, y = 0;
	REQUIRE(t.getLargestExtent(x, y));
	CHECK(x == 4000.0f);
	CHECK(y == 5500.0f);
	// the active boundary (ZH) would have given 2390 x 2590: the radar's picture would show the whole map squeezed into a quarter of it
	REQUIRE(t.getExtent(0, x, y));
	CHECK(x == 2390.0f);
	CHECK(y == 2590.0f);
	// the largest x and the largest y may come from different boundaries
	m.m_boundaries = { { 300, 100 }, { 100, 300 } };
	TerrainLogic t2;
	t2.init(m, chunks, nullptr);
	REQUIRE(t2.getLargestExtent(x, y));
	CHECK(x == 3000.0f);
	CHECK(y == 3000.0f);
	// no boundary: no extent (the radar reports it is not ready)
	m.m_boundaries.clear();
	TerrainLogic t3;
	t3.init(m, chunks, nullptr);
	CHECK_FALSE(t3.getLargestExtent(x, y));
}

TEST_CASE("hud7 radar retail: a map whose later boundary is larger shows the whole extent (Radar::newMap RW 0x6D8EE4 -> RW 0x4626A3)")
{
	if (!haveWorld("hud7 radar extent"))
	{
		return;
	}
	SharedWorld &s = shared();
	// the campaign map with its own players (no lobby slots); the first human side is the local player
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map evil withered heath";
	o.seed = 4711;
	std::string error;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	Player *local = nullptr;
	for (int i = 0; i < game.players().getPlayerCount() && !local; ++i)
	{
		Player *p = game.players().getNthPlayer(i);
		local = p && p->getPlayerType() == PLAYER_HUMAN ? p : nullptr;
	}
	REQUIRE(local != nullptr);
	game.players().setLocalPlayer(local);
	PinholeView view({ 0, -400, 500 }, { 0, 0, 0 }, 1024, 768, 0.7f);
	RecordingShellServices services;
	InGameHud::Config cfg{ game, *s.world, *s.mount->fs, services, view, s.mouse, s.meta, nullptr };
	InGameHud hud(cfg);
	REQUIRE_MESSAGE(hud.boot(&error), error);
	Radar &radar = hud.radar();
	REQUIRE(radar.ready());
	// the far corner of the largest boundary (4000, 5500) is the picture's upper right corner; the first boundary's corner (2390, 2590) lies inside it
	float rx = 0, ry = 0;
	radar.worldToRadar(4000.0f, 5500.0f, 128, rx, ry);
	CHECK(rx == doctest::Approx(128.0f));
	CHECK(ry == doctest::Approx(0.0f));
	radar.worldToRadar(2390.0f, 2590.0f, 128, rx, ry);
	CHECK(rx == doctest::Approx(2390.0f / 4000.0f * 128.0f));
	CHECK(ry == doctest::Approx((1.0f - 2590.0f / 5500.0f) * 128.0f));
	// the picture keeps the extent's aspect inside a square clip (RW 0x6D89F2): 4000 x 5500 is taller than wide
	int ul[2], lr[2];
	radar.drawRect(0, 0, 110, 110, ul, lr);
	CHECK(lr[1] - ul[1] == 110);
	CHECK(lr[0] - ul[0] == (int)(4000.0f * (1.0f / (5500.0f / 110.0f))));
}

TEST_CASE("hud7 radar view box: a corner whose ray meets the terrain-average plane behind the eye still gives its point (RW 0x44D2C7 takes the four unconditionally)")
{
	if (!haveWorld("hud7 view box"))
	{
		return;
	}
	SharedWorld &s = shared();
	facta::FactRig r(s, "FactionMen");
	Radar &radar = r.hud->radar();
	REQUIRE(radar.ready());
	const float z = radar.terrainAverageZ();
	// a camera that looks almost level: the top corners' rays climb, so they meet the plane behind the eye
	r.rig.view.set({ 2000.0f, 1000.0f, z + 60.0f }, { 2000.0f, 1800.0f, z + 20.0f });
	float out[8];
	REQUIRE(radar.viewBoxCorners(0.0f, 0.0f, 128, 128, out)); // before HUD-7: false, the box was not drawn
	const ICoord2D size = r.rig.view.size();
	const ICoord2D corners[4] = { { 0, 0 }, { size.x, 0 }, { size.x, size.y }, { 0, size.y } };
	const float kx = radar.extentWidth() * 0.0078125f, ky = radar.extentHeight() * 0.0078125f;
	int behind = 0;
	for (int i = 0; i < 4; ++i)
	{
		Coord3D o, d;
		REQUIRE(r.rig.view.screenToRay(corners[i], o, d));
		const float t = (z - o.z) / d.z;
		behind += t < 0.0f ? 1 : 0;
		// ZH Find_X_At_Z / Find_Y_At_Z: the ray's line at the height, in radar pixels (RW 0x44D41B)
		CHECK(out[i * 2] == doctest::Approx((o.x + d.x * t) / kx));
		CHECK(out[i * 2 + 1] == doctest::Approx(127.0f - (o.y + d.y * t) / ky));
	}
	CHECK(behind == 2); // the two top corners
}

TEST_CASE("hud7 deselect: in RotWK's mouse setup a left click on empty ground drops the selection (RW 0x83C44F); a targeting command, Esc and the standard setup keep it")
{
	if (!haveWorld("hud7 deselect"))
	{
		return;
	}
	SharedWorld &s = shared();
	REQUIRE(!templates(s).infantry.empty());
	Rig r(s);
	r.input->commandTranslator().setUseAlternateMouse(true); // RotWK's default (GlobalData + 0x5C = 1, RW 0x642A4B)
	const Coord3D c = r.freeSpot(2000.0f, 1300.0f, 400.0f);
	Object *unit = r.make(templates(s).infantry.front(), c.x, c.y);
	r.lookAt(c);
	r.frame(2);
	const Coord3D start = *unit->getPosition();
	const ICoord2D unitPx = r.screenOf({ start.x, start.y, start.z + 6.0f });
	const ICoord2D ground = r.screenOf({ c.x + 160.0f, c.y + 120.0f, r.logic().getGroundHeight(c.x + 160.0f, c.y + 120.0f) });
	REQUIRE(HudObjects::pickForHover(r.input->context(), ground) == nullptr); // nothing to pick there
	auto selectUnit = [&]() {
		r.leftClick(unitPx.x, unitPx.y);
		r.frame(1);
		REQUIRE(r.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
		REQUIRE(r.local->selection() == std::vector<ObjectID>{ unit->getID() });
	};

	// RotWK's setup: the click on the ground deselects, in the client at once and in the logic at its next frame (MSG_DESTROY_SELECTED_GROUP); the unit
	// gets no order and stays where it was
	selectUnit();
	r.leftClick(ground.x, ground.y);
	CHECK(r.input->ui().selected().empty());
	CHECK(logHas(*r.input, "MSG_DESTROY_SELECTED_GROUP"));
	CHECK_FALSE(logHas(*r.input, "MSG_DO_MOVETO"));
	r.frame(5);
	CHECK(r.local->selection().empty());
	CHECK(distance2d(*unit->getPosition(), start) < 1.0f);

	// a GUI command that waits for its target (SPECIAL_POWER, RW 0x83C473 .. 0x83C494): the click is the target's, the selection stays
	selectUnit();
	const CommandButton *power = TheCommandStore ? TheCommandStore->findCommandButton("Command_SpecialAbilityCaptainOfGondorBoromir") : nullptr;
	REQUIRE(power != nullptr);
	REQUIRE(power->m_command == GUI_COMMAND_SPECIAL_POWER);
	r.input->ui().setGUICommand(power);
	r.leftClick(ground.x, ground.y);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
	r.input->ui().setGUICommand(nullptr);
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ unit->getID() });

	// Esc: RotWK's SelectionTranslator has no key case and CommandMap.ini binds Esc only with Shift (RELOAD_RAPID_ITERATION_FEATURE): the selection stays
	r.pressKey(KEY_ESC);
	r.frame(1);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
	CHECK(r.local->selection() == std::vector<ObjectID>{ unit->getID() });

	// the standard setup orders with the left click: the click on the ground moves the unit and keeps the selection (the right click deselects there)
	r.input->commandTranslator().setUseAlternateMouse(false);
	r.leftClick(ground.x, ground.y);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ unit->getID() });
	CHECK(logHas(*r.input, "MSG_DO_MOVETO"));
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ unit->getID() });
}

TEST_CASE("hud7 upgrade: an object upgrade being researched shows RotWK's availability 8 (the _cantAfford clip, RW 0x943C40 / 0x9D2C31); a second press charges nothing")
{
	if (!haveWorld("hud7 upgrade"))
	{
		return;
	}
	SharedWorld &s = shared();
	facta::FactRig r(s, "FactionMen");
	// the mapped images the ring's rim overlays need (RadialBorder)
	MappedImageCollection images;
	INIEnvironment imageEnv;
	imageEnv.fileSystem = s.mount->fs.get();
	images.registerBlocks(imageEnv.blocks);
	INI ini(imageEnv);
	ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE);
	r.hud->setMappedImages(&images);
	r.local()->getMoney()->deposit(10000, false);
	Object *fortress = r.make("MenFortressCitadel", r.spot(2000.0f, 1300.0f, 400.0f));
	r.frames(3);
	REQUIRE(r.select(fortress));
	const std::string name = "Command_PurchaseUpgradeMenFortressBanners";
	const ControlBarButton *b = r.find(name);
	REQUIRE(b != nullptr);
	REQUIRE(b->button->m_command == GUI_COMMAND_OBJECT_UPGRADE);
	CHECK(b->state == ButtonState::Enabled);
	const int slot = b->slot;
	ControlBarRadialMenu &radial = r.hud->radialMenu();
	r.frames(1);
	BubbleLook look = lookOf(radial, slot);
	REQUIRE(look.found); // the upgrade is a bubble of the fortress's upgrade ring
	CHECK_FALSE(look.grayscale);
	CHECK(look.iconColor == 0xFFFFFFFFu);
	CHECK(look.rimColor == 0xFFFFFFFFu);
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(b->button->m_upgradeName) : nullptr;
	REQUIRE(u != nullptr);
	const unsigned cost = (unsigned)u->calcCostToBuild(r.local(), fortress);
	REQUIRE(cost > 0u);
	// a real "cannot afford" (availability 6, RW 0x94311C): the money taken away, the bubble keeps its colour dimmed (ALWAYS_COLOR) with a white rim
	const unsigned rich = r.money();
	r.local()->getMoney()->withdraw(rich, false);
	r.frames(2);
	b = r.find(name);
	REQUIRE(b != nullptr);
	CHECK(b->state == ButtonState::CantAfford);
	r.frames(1);
	look = lookOf(radial, b->slot);
	REQUIRE(look.found);
	CHECK_FALSE(look.grayscale);
	CHECK(look.iconColor == 0xFF909090u);
	CHECK(look.rimColor == 0xFFFFFFFFu);
	r.local()->getMoney()->deposit(rich, false);
	r.frames(2);
	b = r.find(name);
	REQUIRE(b != nullptr);
	REQUIRE(b->state == ButtonState::Enabled);
	ProductionUpdateInterface *pu = fortress->getProductionUpdate();
	REQUIRE(pu != nullptr);

	const unsigned before = r.money();
	r.logic().economy().clearIncomeLog();
	REQUIRE(r.bar().pressButton(b->slot, b->inPalantir));
	r.frames(2);
	CHECK(r.money() == before - cost + incomeSince(r));
	CHECK(pu->isUpgradeInQueue(u));
	CHECK_FALSE(fortress->hasUpgrade(u));
	b = r.find(name);
	REQUIRE(b != nullptr); // shown: availability 8 is never hidden (HIDE_WHILE_DISABLED tests availability 0 only, RW 0x943B39)
	CHECK(b->state == ButtonState::Purchased);
	CHECK(std::string(AptPalantir::commandStateName(b->state, b->button->m_options)) == "_cantAfford");
	CHECK(std::string(AptPalantir::commandStateName(ButtonState::Restricted, b->button->m_options)) == "_disabled"); // what it showed before HUD-7
	// the bubble (W3DGadgetPushButtonImageDrawOne RW 0x4A5112 with the statuses ALWAYS_COLOR | GRAY_OVERLAY): the image in colour times 0x909090, a grey rim;
	// an unavailable upgrade bubble of the same ring (availability 0: GRAY_OVERLAY only) is grayscale with the grey rim
	look = lookOf(radial, slot);
	REQUIRE(look.found);
	CHECK_FALSE(look.grayscale);
	CHECK(look.iconColor == 0xFF909090u);
	CHECK(look.rimColor == 0xFF909090u);
	// every bubble of the ring has the look of its availability
	for (const ControlBarRadialMenu::Button &other : radial.buttons())
	{
		const BubbleLook l = lookOf(radial, other.slot);
		if (!l.found)
		{
			continue; // no image (none in this ring)
		}
		const ButtonState st = other.source.state;
		INFO(other.source.button->m_name << " state " << (int)st);
		const bool enabled = st == ButtonState::Enabled || st == ButtonState::Active;
		CHECK(l.grayscale == (st == ButtonState::Restricted));
		CHECK(l.iconColor == (!enabled && (st == ButtonState::CantAfford || st == ButtonState::Purchased) ? 0xFF909090u : 0xFFFFFFFFu));
		CHECK(l.rimColor == (st == ButtonState::Restricted || st == ButtonState::Purchased ? 0xFF909090u : 0xFFFFFFFFu));
	}

	// a second press reaches processCommandUI (RW 0x940D49 asks only canAffordUpgrade); the queue refuses the duplicate: no second charge, one entry
	r.bar().pressButton(b->slot, b->inPalantir);
	r.frames(2);
	CHECK(r.money() == before - cost + incomeSince(r));
	int entries = 0;
	for (const ProductionEntry *e = pu->firstProduction(); e; e = pu->nextProduction(e))
	{
		entries += e->upgradeToResearch == u ? 1 : 0;
	}
	CHECK(entries == 1);

	// researched: the object has it, the button keeps availability 8
	const int limit = (int)(5.0f * u->getBuildTimeSeconds()) + 60;
	for (int i = 0; i < limit && !fortress->hasUpgrade(u); ++i)
	{
		r.frames(1);
	}
	CHECK(fortress->hasUpgrade(u));
	b = r.find(name);
	REQUIRE(b != nullptr);
	CHECK(b->state == ButtonState::Purchased);
}

TEST_CASE("hud7 hero cancel: a right click on the recruiting hero's bubble cancels it with the full refund (RW 0x940B0E / 0x940C57 -> MSG_CANCEL_UNIT_CREATE by index)")
{
	if (!haveWorld("hud7 hero cancel"))
	{
		return;
	}
	SharedWorld &s = shared();
	facta::FactRig r(s, "FactionMen");
	r.local()->getMoney()->deposit(10000, false);
	Object *fortress = r.make("MenFortressCitadel", r.spot(2000.0f, 1300.0f, 400.0f));
	r.frames(3);
	REQUIRE(r.select(fortress));
	r.frames(5);
	ControlBarRadialMenu &radial = r.hud->radialMenu();
	int ringSlot = -1;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE && b.source.button->m_name.find("Revivables") != std::string::npos)
		{
			ringSlot = b.slot;
		}
	}
	REQUIRE(ringSlot >= 0);
	pressBubble(r, *bubble(radial, ringSlot), HudInput::Button::Left);
	r.frames(3);
	// the first hero the player can recruit now, and a second one for the control
	int heroSlot = -1, otherSlot = -1;
	for (const ControlBarRadialMenu::Button &b : radial.buttons())
	{
		if (b.source.button && b.source.button->m_command == GUI_COMMAND_REVIVE && b.source.state == ButtonState::Enabled)
		{
			if (heroSlot < 0)
			{
				heroSlot = b.slot;
			}
			else if (otherSlot < 0)
			{
				otherSlot = b.slot;
			}
		}
	}
	REQUIRE(heroSlot >= 0);
	REQUIRE(otherSlot >= 0);
	const ControlBarRadialMenu::Button *hero = bubble(radial, heroSlot);
	REQUIRE(hero != nullptr);
	REQUIRE(hero->source.button->hasOption(COMMAND_OPTION_CANCELABLE));
	const int index = hero->source.reviveIndex;
	const ThingTemplate *tt = r.local()->heroes().templateAt(r.logic(), index);
	REQUIRE(tt != nullptr);
	const unsigned cost = (unsigned)hero->source.cost;
	REQUIRE(cost > 0u);
	ProductionUpdateInterface *pu = fortress->getProductionUpdate();
	REQUIRE(pu != nullptr);
	INFO("hero " << tt->getName() << " record " << index << " cost " << cost);

	// a right click on a hero that is not being recruited: the cancel finds nothing (RotWK does not ask, RW 0x940C57), nothing changes
	const unsigned start = r.money();
	r.logic().economy().clearIncomeLog();
	pressBubble(r, *bubble(radial, otherSlot), HudInput::Button::Right);
	r.frames(2);
	CHECK(r.money() == start + incomeSince(r));
	CHECK(pu->getProductionCount() == 0);

	// recruited by a left click: paid, the record in production
	pressBubble(r, *bubble(radial, heroSlot), HudInput::Button::Left);
	r.frames(2);
	CHECK(r.money() == start - cost + incomeSince(r));
	CHECK(pu->getProductionCount() == 1);
	REQUIRE(r.local()->heroes().at(index) != nullptr);
	CHECK(r.local()->heroes().at(index)->startFrame >= 0);
	REQUIRE(bubble(radial, heroSlot) != nullptr);
	CHECK(bubble(radial, heroSlot)->source.state == ButtonState::NotReady); // the recruit's clock

	// the right click on the same bubble cancels it: the full price back, the record out of production, the queue empty
	const unsigned cancelsBefore = radial.cancels();
	pressBubble(r, *bubble(radial, heroSlot), HudInput::Button::Right);
	r.frames(2);
	CHECK(radial.cancels() == cancelsBefore + 1);
	CHECK(r.money() == start + incomeSince(r)); // the full price back
	CHECK(pu->getProductionCount() == 0);
	CHECK(r.local()->heroes().at(index)->startFrame == -1);
	CHECK(bubble(radial, heroSlot)->source.state == ButtonState::Enabled);
	// and no hero comes out later
	r.frames(5 * 60);
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		CHECK_MESSAGE(!(o->getTemplate() == tt && o->getControllingPlayer() == r.local()), "a cancelled hero was made: " << tt->getName());
	}
}

TEST_CASE("hud7 sell purchased: a barracks a porter built and paid for is sold by a mouse click on the Palantir's Sell button: the exact refund, no other income, then it is gone")
{
	if (!haveWorld("hud7 sell purchased"))
	{
		return;
	}
	SharedWorld &s = shared();
	facta::FactRig r(s, "FactionMen");
	r.local()->depositMoney(3000);
	const ThingTemplate *what = r.logic().things().findTemplate("GondorBarracks");
	REQUIRE(what != nullptr);
	CHECK(refundValue(*what) == 0u); // no RefundValue: the refund is SellPercentage (gamedata.ini: 50%) of what was paid, times the health left
	REQUIRE(r.logic().settings().sellPercentageLoaded);
	REQUIRE(r.logic().settings().sellPercentage == doctest::Approx(0.5f));
	Coord3D site = r.spot(2200.0f, 1300.0f, 330.0f);
	Object *porter = r.make("MenPorter", Coord3D{ site.x - 160.0f, site.y, 0.0f });
	r.frames(3);
	REQUIRE(r.select(porter));
	const ControlBarButton *build = r.find("Command_PorterConstructMenBarracks");
	REQUIRE(build != nullptr);
	REQUIRE(build->state == ButtonState::Enabled);
	const unsigned cost = (unsigned)BuildAssistant::calcCostToBuild(*what, r.local(), porter, -1);
	REQUIRE(cost > 0u);

	// bought: the porter's button, a click on the ground (the price is taken when the site is laid)
	const unsigned beforeBuild = r.money();
	REQUIRE(r.bar().pressButton(build->slot, build->inPalantir));
	r.frames(1);
	REQUIRE(r.hud->input().ui().isPlacing());
	r.clickWorld(site);
	for (int k = 1; k < 8 && r.hud->input().ui().isPlacing(); ++k)
	{
		site = r.spot(1400.0f + 330.0f * (float)k, 1300.0f + (k % 2 ? 700.0f : 0.0f), 330.0f);
		r.clickWorld(site);
	}
	r.frames(3);
	Object *barracks = nullptr;
	for (Object *o = r.logic().getFirstObject(); o; o = o->getNextObject())
	{
		barracks = o->getTemplate() == what && o->getControllingPlayer() == r.local() ? o : barracks;
	}
	REQUIRE(barracks != nullptr);
	const unsigned paid = beforeBuild - r.money();
	CHECK(paid == cost);
	const ObjectID id = barracks->getID();
	for (int i = 0; i < 2000 && barracks->isUnderConstruction(); ++i)
	{
		r.frames(1);
	}
	REQUIRE_FALSE(barracks->isUnderConstruction());
	REQUIRE(barracks->getBodyModule() != nullptr);
	CHECK(barracks->getBodyModule()->getHealth() == barracks->getBodyModule()->getMaxHealth());
	REQUIRE(r.select(barracks));

	// the control window: the player has no income here (no farm, no other source): 30 frames change nothing
	const unsigned beforeSell = r.money();
	r.logic().economy().clearIncomeLog();
	r.frames(30);
	REQUIRE(r.money() == beforeSell);
	REQUIRE(incomeSince(r) == 0u);

	// the Sell button ("Demolish building") of the Palantir's arc, pressed by a left click on its window point
	const ControlBarButton *sell = r.find("Command_Sell");
	REQUIRE(sell != nullptr);
	REQUIRE(sell->button->m_command == GUI_COMMAND_SELL);
	REQUIRE(sell->state == ButtonState::Enabled);
	int bx = 0, by = 0;
	REQUIRE_MESSAGE(arcContentPoint(r, sell->slot, bx, by), "no window point routes to the Sell button's content");
	r.hud->mouseMove(bx, by, 0);
	r.frames(1);
	r.hud->mouseButton(HudInput::Button::Left, true, bx, by, 0, r.rig.timeMs += 300);
	r.hud->mouseButton(HudInput::Button::Left, false, bx, by, 0, r.rig.timeMs += 40);
	r.frames(3);
	const unsigned refund = (unsigned)(double)((float)(unsigned)(double)((float)paid * 0.5f) * 1.0f);
	CHECK(r.money() == beforeSell + refund);
	CHECK(refund == paid / 2u);
	CHECK(r.hud->input().ui().selected().empty());
	CHECK(r.local()->selection().empty());
	// and it goes: the sale's collapse (BuildAssistant::update RW 0x79468B) ends with the object removed from the world; the money stays
	bool gone = false;
	for (int i = 0; i < 600 && !gone; ++i)
	{
		r.frames(1);
		gone = r.logic().findObjectByID(id) == nullptr;
	}
	CHECK(gone);
	CHECK(incomeSince(r) == 0u); // nothing else paid the player meanwhile
	CHECK(r.money() == beforeSell + refund);
	std::printf("hud7 sell purchased: GondorBarracks paid %u, refund %u, money %u -> %u\n", paid, refund, beforeSell, r.money());
}

TEST_CASE("hud7 sell: every sellable structure of Isengard, Mordor, the Goblins and Angmar sells from its Sell button (the factions FACT-A's test leaves out)")
{
	if (!haveWorld("hud7 sell"))
	{
		return;
	}
	SharedWorld &s = shared();
	const int structureBit = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	const int notSellableBit = ObjectTemplateInfoBuilder::kindOfIndex("NOT_SELLABLE");
	// the inventory (facta::reachableTemplates: the start, the builders' constructions, the castles' expansions): every STRUCTURE with a Sell button and
	// without KindOf NOT_SELLABLE, by name; and the structures without, each with its reason. A change of either list is a finding, not noise
	const std::string noButton = "no SELL button in its command set";
	const std::map<std::string, std::pair<std::set<std::string>, std::map<std::string, std::string>>> expected = {
		{ "FactionIsengard",
		  { { "IsengardArmory", "IsengardBallistaExpansion", "IsengardBattleTower", "IsengardCastleWallGate", "IsengardCastleWallHub", "IsengardCastleWallHubExpansion",
		      "IsengardCastleWallSegment", "IsengardFortressCitadel", "IsengardFurnace", "IsengardLumberMill", "IsengardMineLauncherExpansion", "IsengardSiegeWorks",
		      "IsengardTavern", "IsengardTowerExpansion", "IsengardUrukPit", "IsengardWallCliffCap", "IsengardWargPit", "IsengardWargSentry" },
		    { { "IsengardFortress", noButton }, { "IsengardFortressExpansionPadCorner", noButton }, { "MordorBase_DEMO", noButton }, { "MordorBase", noButton } } } },
		{ "FactionMordor",
		  { { "MordorBattleTower", "MordorFortressBarricadeExpansion", "MordorFortressCitadel", "MordorGateWatchersExpansion", "MordorHaradrimPalace",
		      "MordorLumberMill", "MordorMumakilPen", "MordorOrcPit", "MordorSiegeWorks", "MordorSlaughterHouse", "MordorTrollCage", "MordorWallCatapultExpansion" },
		    { { "MordorFortress", noButton }, { "MordorFortressExpansionPadCorner", noButton }, { "MordorFortressLavaMoat", noButton }, { "MordorBase_DEMO", noButton },
		      { "MordorBase", noButton } } } },
		{ "FactionWild",
		  { { "GoblinCave", "GoblinFissure", "WildArrowDenExpansion", "WildBurrowsExpansion", "WildFortressCitadel", "WildGiantSentryExpansion", "WildLumberMill",
		      "WildMineShaft", "WildSentryTower", "WildSpiderHolesExpansion", "WildSpiderPit", "WildTreasureTrove" },
		    { { "WildFortress", noButton }, { "WildFortressExpansionPadSide", noButton }, { "WildFortressExpansionPadCorner", noButton }, { "MordorBase_DEMO", noButton },
		      { "MordorBase", noButton } } } },
		{ "FactionAngmar",
		  { { "AngmarBarracks", "AngmarBattleTowerExpansion", "AngmarCatapultExpansion", "AngmarDen", "AngmarForgeWorks", "AngmarFortressCitadel",
		      "AngmarHallofTwilight", "AngmarKennelExpansion", "AngmarMill", "AngmarSentryTower", "AngmarWallCliffCap", "AngmarWallGateSmall", "AngmarWallHubSmall",
		      "AngmarWallHubSmallExpansion", "AngmarWallPosternGateSmall", "AngmarWallSegmentSmall", "AngmarWallTowerSmall" },
		    { { "AngmarFortress", noButton }, { "AngmarFortressExpansionPadSide", noButton }, { "AngmarFortressExpansionPadCorner", noButton },
		      { "AngmarFortressSpikes", noButton }, { "AngmarWallCatapultSmall", noButton } } } },
	};
	for (const char *f : { "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		facta::FactRig r(s, f);
		std::set<std::string> soldNames;
		std::map<std::string, std::string> excluded;
		size_t sold = 0, notSellable = 0;
		float x = 1300.0f;
		for (const std::string &name : facta::reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(name);
			const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt->getFinalOverride());
			if (!MaskTest(info.kindOf, (unsigned)structureBit))
			{
				continue;
			}
			const CommandButton *sell = facta::setButton(facta::templateCommandSet(*tt), GUI_COMMAND_SELL);
			if (!sell || MaskTest(info.kindOf, (unsigned)notSellableBit))
			{
				excluded[name] = !sell ? noButton : std::string("KindOf NOT_SELLABLE");
				std::printf("hud7 sell exclusion %s %s: %s\n", f, name.c_str(), excluded[name].c_str());
				++notSellable;
				continue;
			}
			const Coord3D at = r.spot(x, 1700.0f, 300.0f);
			x = x > 3400.0f ? 1300.0f : x + 350.0f;
			Object *o = r.make(name, at);
			const float paid = (float)BuildAssistant::calcCostToBuild(*tt, r.local(), nullptr, -1);
			o->setBuildCostPaid(paid);
			r.frames(3);
			REQUIRE(o->getBodyModule());
			const float health = o->getBodyModule()->getHealth(), maxHealth = o->getBodyModule()->getMaxHealth();
			// RefundValue, else SellPercentage (gamedata.ini: 50%) of what was paid, times the health left (BuildAssistant::update RW 0x794729)
			const unsigned rv = refundValue(*tt);
			const unsigned base = rv != 0 ? rv : (unsigned)(double)(paid * 0.5f);
			const unsigned expect = (unsigned)(double)((float)base * (health / maxHealth));
			const ObjectID id = o->getID();
			INFO(f << " " << name << " paid " << paid);
			REQUIRE(r.select(o));
			const unsigned before = r.money();
			r.logic().economy().clearIncomeLog();
			const ControlBarButton *b = r.find(sell->m_name);
			REQUIRE(b != nullptr);
			CHECK(b->state == ButtonState::Enabled);
			REQUIRE(r.bar().pressButton(b->slot, b->inPalantir));
			r.frames(3);
			Object *after = r.logic().findObjectByID(id);
			CHECK((after == nullptr || after->isEffectivelyDead()));
			CHECK(r.money() == before + expect + incomeSince(r));
			CHECK(r.hud->input().ui().selected().empty());
			CHECK(r.local()->selection().empty());
			soldNames.insert(name);
			std::printf("hud7 sell %s %s: paid %.0f refund %u\n", f, name.c_str(), paid, expect);
			++sold;
			r.frames(10);
		}
		std::printf("hud7 sell %s: %zu sold, %zu structures without a Sell button or NOT_SELLABLE\n", f, sold, notSellable);
		CHECK(soldNames == expected.at(f).first);
		CHECK(excluded == expected.at(f).second);
		CHECK(r.logic().sellList().empty());
	}
}
