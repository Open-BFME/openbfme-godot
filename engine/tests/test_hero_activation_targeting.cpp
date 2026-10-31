// OpenBFME. GPL-3.0. Retail 2.01 hero targeting through the real control bar
// and raw mouse stream. Skips without ROTWK_INSTALL / BFME2_INSTALL.
#include "HudTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/ShroudManager.h"

using namespace hudtest;

namespace
{
struct ActivationRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;

	explicit ActivationRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		rig.game->shroud().revealMapForPlayerPermanently(rig.index());
	}
	~ActivationRig() { hud.reset(); }
	void update() { hud->update(0.034); } // no logic advance: inspect commands before execution/recharge
	ControlBarButton select(Object &hero, const char *command)
	{
		hero.getExperienceTracker()->gainExpForLevel(10, true, false);
		// Level upgrades and UnpauseSpecialPower modules run on logic frames.
		for (int i = 0; i < 20; ++i) { update(); rig.game->advance(0.2); }
		hud->input().ui().deselectAll(false);
		hud->input().ui().selectObject(hero.getID());
		update();
		for (const auto *buttons : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons() })
			for (const auto &b : *buttons)
				if (b.button && b.button->m_name == command) return b;
		return {};
	}
	ICoord2D aim(const Coord3D &where)
	{
		rig.lookAt(where);
		const ICoord2D pixel = rig.screenOf(where);
		hud->mouseMove(pixel.x, pixel.y);
		update();
		return pixel;
	}
	void click(HudInput::Button button, const ICoord2D &p)
	{
		hud->mouseButton(button, true, p.x, p.y, 0, rig.timeMs += 500);
		hud->mouseButton(button, false, p.x, p.y, 0, rig.timeMs += 40);
		update();
	}
	std::vector<GameMessage> casts() const
	{
		std::vector<GameMessage> result;
		for (const auto &m : rig.game->commands().messages())
			if (m.getType() == MSG_DO_SPECIAL_POWER_AT_LOCATION || m.getType() == MSG_DO_SPECIAL_POWER_AT_OBJECT || m.getType() == MSG_DO_SPECIAL_POWER)
				result.push_back(m);
		return result;
	}
};
}

TEST_CASE("hero activation targeting: retail Gimli and Rogash Leap and Gloin Smash draw their landing radius and retain invalid aims")
{
	if (!haveWorld("hero activation targeting radius")) return;
	struct Case { const char *hero, *button, *cursor, *texture; float diameter; };
	for (const Case &c : { Case{ "DwarvenGimli", "Command_RohanGimliLeap", "LeapRadiusCursor", "SCCLeap", 70.0f },
		Case{ "AngmarRogash", "Command_RogashLeap", "RogashLeapRadiusCursor", "SCCRogashLeap", 70.0f },
		Case{ "DwarvenGloin", "Command_SpecialAbilityGloinSmash", "LeapRadiusCursor", "SCCLeap", 500.0f } })
	{
		INFO(std::string(c.hero));
		ActivationRig h(shared());
		auto context = h.hud->enterContext();
		const Coord3D home = h.rig.freeSpot(1800, 1400, 300.0f);
		Object *hero = h.rig.make(c.hero, home.x, home.y);
		REQUIRE(hero);
		const auto b = h.select(*hero, c.button);
		REQUIRE(b.button);
		REQUIRE(b.state == ButtonState::Enabled);
		const Coord3D target{ home.x + 250, home.y, h.rig.logic().getGroundHeight(home.x + 250, home.y) };
		const auto pixel = h.aim(target); // beyond Leap's StartAbilityRange: targeting must allow approach
		REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
		h.update();
		CHECK_FALSE(h.hud->spellBar().targeting());
		CHECK(h.hud->input().ui().getGUICommand() == b.button);
		CHECK(h.hud->radiusCursor().type() == c.cursor);
		const auto &decal = h.hud->radiusCursor().decal();
		CHECK(decal.visible);
		CHECK(decal.texture == c.texture);
		CHECK(decal.size == c.diameter); // 2 * the retail power's RadiusCursorRadius
		CHECK(decal.valid);
		CHECK(h.hud->input().ui().cursor() == "None");
		// Re-click cancels; re-arm without a cast (ControlBar's target toggle).
		const auto before = h.casts().size();
		REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
		h.update();
		CHECK(h.casts().size() == before);
		CHECK_FALSE(h.hud->radiusCursor().active());
		CHECK(h.hud->input().ui().getGUICommand() == nullptr);
		REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
		h.update();
		float loX = 0, loY = 0, hiX = 0, hiY = 0;
		h.rig.game->ai().pathfinder().terrainView()->getMaximumPathfindExtent(loX, loY, hiX, hiY);
		const Coord3D edge{ loX + 15.0f, home.y, h.rig.logic().getGroundHeight(loX + 15.0f, home.y) };
		const auto edgePixel = h.aim(edge);
		CHECK_FALSE(decal.valid);
		h.click(HudInput::Button::Left, edgePixel);
		CHECK(h.casts().size() == before);
		CHECK(h.hud->input().ui().getGUICommand() == b.button);
		CHECK(h.hud->radiusCursor().active());
		h.aim(target);
		h.click(HudInput::Button::Right, pixel);
		CHECK(h.casts().size() == before);
		CHECK(h.hud->input().ui().getGUICommand() == nullptr);
		CHECK_FALSE(h.hud->radiusCursor().active());
		CHECK(h.hud->input().ui().firstSelected() == hero->getID());
		REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
		h.click(HudInput::Button::Left, pixel);
		const auto casts = h.casts();
		REQUIRE(casts.size() == before + 1);
		const auto &cast = casts.back();
		CHECK(cast.getType() == MSG_DO_SPECIAL_POWER_AT_LOCATION);
		CHECK(cast.getArgument(3)->integer == (int)b.button->m_options);
		CHECK(cast.getArgument(4)->objectID == INVALID_ID); // selection, as RW 0x81E147
		Coord3D clicked;
		REQUIRE(h.rig.view.screenToTerrain(pixel, h.rig.logic(), clicked));
		CHECK(cast.getArgument(1)->location.x == clicked.x);
		CHECK(cast.getArgument(1)->location.y == clicked.y);
		CHECK(cast.getArgument(1)->location.z == clicked.z);
		CHECK(h.hud->input().ui().getGUICommand() == nullptr);
		CHECK_FALSE(h.hud->radiusCursor().active());
	}
}

TEST_CASE("hero activation targeting: Gloin's object power rejects allied clicks and preserves an enemy click's position")
{
	if (!haveWorld("hero activation targeting object")) return;
	ActivationRig h(shared());
	auto context = h.hud->enterContext();
	const Coord3D home = h.rig.freeSpot(1800, 1400, 300.0f);
	Object *hero = h.rig.make("DwarvenGloin", home.x, home.y);
	REQUIRE(hero);
	const auto b = h.select(*hero, "Command_SpecialAbilityDwarvenGloinSlam");
	REQUIRE(b.button);
	REQUIRE(b.state == ButtonState::Enabled);
	Object *ally = h.rig.make("GondorFighter", home.x + 100, home.y);
	Object *enemy = h.rig.make("GondorFighter", home.x + 200, home.y, h.rig.game->players().findPlayerWithName("Player_2"));
	REQUIRE(ally);
	REQUIRE(enemy);
	// Use the input stack's geometry picker: this tests dispatch independently
	// of the rendered model / draw snapshot (the live HUD has its own ray picker).
	h.hud->input().context().pickRay = {};
	const auto before = h.casts().size();
	REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
	auto p = h.aim(*ally->getPosition());
	h.click(HudInput::Button::Left, p);
	CHECK(h.casts().size() == before);
	CHECK(h.hud->input().ui().getGUICommand() == b.button);
	p = h.aim(*enemy->getPosition());
	h.click(HudInput::Button::Left, p);
	const auto casts = h.casts();
	REQUIRE(casts.size() == before + 1);
	const auto &cast = casts.back();
	CHECK(cast.getType() == MSG_DO_SPECIAL_POWER_AT_OBJECT);
	CHECK(cast.getArgument(1)->objectID == enemy->getID());
	CHECK(cast.getArgument(2)->integer == (int)b.button->m_options);
	CHECK(cast.getArgument(3)->objectID == INVALID_ID);
	REQUIRE(cast.getArgumentCount() == 5);
	Coord3D clicked;
	REQUIRE(h.rig.view.screenToTerrain(p, h.rig.logic(), clicked));
	CHECK(cast.getArgument(4)->location.x == clicked.x);
	CHECK(cast.getArgument(4)->location.y == clicked.y);
	CHECK(h.hud->input().ui().getGUICommand() == nullptr);
}

TEST_CASE("hero activation targeting: combined object and ground options prefer allowed objects and keep CommandTrigger ground casts")
{
	if (!haveWorld("hero activation targeting combined")) return;
	ActivationRig h(shared());
	auto context = h.hud->enterContext();
	const Coord3D home = h.rig.freeSpot(1800, 1400, 300.0f);
	Object *hero = h.rig.make("DwarvenGimli", home.x, home.y);
	REQUIRE(hero);
	const auto b = h.select(*hero, "Command_RohanGimliLeap");
	REQUIRE(b.button);
	// Exercise the generic combination on a real loaded power; retail Gimli's
	// own button is position-only. Mod data may combine either target kind.
	CommandButton combined = *b.button;
	combined.m_options |= COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT;
	combined.m_commandTrigger = { "Command_RogashLeap" };
	Object *ally = h.rig.make("GondorFighter", home.x + 100, home.y);
	Object *enemy = h.rig.make("GondorFighter", home.x + 200, home.y, h.rig.game->players().findPlayerWithName("Player_2"));
	Object *neutral = h.rig.make("GondorFighter", home.x + 300, home.y, h.rig.game->players().getNeutralPlayer());
	REQUIRE(ally);
	REQUIRE(enemy);
	REQUIRE(neutral);
	h.hud->input().context().pickRay = {};
	for (Object *target : { ally, enemy, neutral })
	{
		const auto before = h.casts().size();
		h.hud->input().ui().setGUICommand(&combined);
		const auto pixel = h.aim(*target->getPosition());
		h.click(HudInput::Button::Left, pixel);
		const auto casts = h.casts();
		const bool objectTarget = target != ally;
		REQUIRE(casts.size() == before + (objectTarget ? 1 : 2));
		const auto &cast = casts[before];
		CHECK(cast.getType() == (objectTarget ? MSG_DO_SPECIAL_POWER_AT_OBJECT : MSG_DO_SPECIAL_POWER_AT_LOCATION));
		if (objectTarget)
		{
			CHECK(cast.getArgument(1)->objectID == target->getID());
			CHECK(cast.getArgument(2)->integer == (int)combined.m_options);
		}
		else
		{
			CHECK(cast.getArgument(2)->objectID == INVALID_ID);
			CHECK(cast.getArgument(3)->integer == (int)combined.m_options);
			CHECK(casts[before + 1].getArgument(0)->integer == (int)TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityRogashLeap")->getID());
		}
		CHECK(h.hud->input().ui().getGUICommand() == nullptr);
	}
}

TEST_CASE("hero activation targeting: a context-only targetless power issues once instead of remaining stuck")
{
	if (!haveWorld("hero activation targeting context only")) return;
	ActivationRig h(shared());
	auto context = h.hud->enterContext();
	const Coord3D home = h.rig.freeSpot(1800, 1400, 300.0f);
	Object *hero = h.rig.make("AngmarRogash", home.x, home.y);
	REQUIRE(hero);
	const auto b = h.select(*hero, "Command_SpecialAbilityRageoftheNorth");
	REQUIRE(b.button);
	CommandButton contextOnly = *b.button;
	contextOnly.m_options = COMMAND_OPTION_CONTEXTMODE_COMMAND;
	h.hud->input().ui().setGUICommand(&contextOnly);
	const auto before = h.casts().size();
	const auto pixel = h.aim(Coord3D{ home.x + 100, home.y, home.z });
	// The double-click path must also dispatch a pending power, rather than
	// expanding selection and leaving the mode behind.
	h.hud->mouseButton(HudInput::Button::Left, true, pixel.x, pixel.y, 0, 1000, true);
	h.hud->mouseButton(HudInput::Button::Left, false, pixel.x, pixel.y, 0, 1050);
	h.update();
	const auto casts = h.casts();
	REQUIRE(casts.size() == before + 1);
	CHECK(casts.back().getType() == MSG_DO_SPECIAL_POWER);
	CHECK(casts.back().getArgument(1)->integer == (int)COMMAND_OPTION_CONTEXTMODE_COMMAND);
	CHECK(h.hud->input().ui().getGUICommand() == nullptr);
	CHECK(h.hud->input().ui().firstSelected() == hero->getID());
}
