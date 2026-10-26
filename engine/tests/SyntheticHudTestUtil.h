// OpenBFME. GPL-3.0. No retail mounts, templates, drawables or wall clock.
#pragma once

#include "LogicTestUtil.h"
#include "GameClient/HudInput.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/PlayerCommands.h"
#include <algorithm>

namespace synthetichud
{
// An orthographic view makes a pixel an exact world x/y; terrain is the logic's flat, unmounted ground.
class PixelView : public TacticalView
{
public:
	ICoord2D size() const override { return { 1024, 768 }; }
	bool worldToScreen(const Coord3D &p, ICoord2D &s) const override { s = { (int)p.x, (int)p.y }; return true; }
	bool screenToRay(const ICoord2D &p, Coord3D &o, Coord3D &d) const override
	{
		o = { (float)p.x, (float)p.y, 100.0f };
		d = { 0.0f, 0.0f, -1.0f };
		return true;
	}
	Coord3D position() const override { return m_position; }
	void lookAt(const Coord3D &p) override { m_position = p; }
private:
	Coord3D m_position{ 512, 384, 0 };
};

struct Rig : logictest::LogicWorld
{
	PixelView view;
	MetaMap map;
	CommandList commands;
	GameLogicDispatch dispatch{ *logic };
	PlayerCommands playerCommands;
	std::unique_ptr<HudInput> input;
	int modifiers = 0, timeMs = 1000;

	Rig()
	{
		playerCommands.registerHandlers(dispatch);
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		REQUIRE(w.load(
			"Object TestFighter\n KindOf = SELECTABLE INFANTRY\n WeaponSet\n  Conditions = None\n  Weapon = PRIMARY TestSword\n End\nEnd\n"
			"ChildObject TestArcher TestFighter\nEnd\n"
			"Object TestHorde\n KindOf = SELECTABLE HORDE INFANTRY\nEnd\n"
			"Object TestMember\n KindOf = SELECTABLE INFANTRY HORDE_MEMBER\nEnd\n"
			"Object TestUnarmed\n KindOf = SELECTABLE INFANTRY\nEnd\n") == "");
		players.setLocalPlayer(players.findPlayerWithName("Alice"));
		bind(CMSG_META_BEGIN_PREFER_SELECTION, KEY_NONE, MOD_SHIFT);
		bind(CMSG_META_END_PREFER_SELECTION, KEY_NONE, MOD_SHIFT, TRANSITION_UP);
		bind(CMSG_META_BEGIN_FORCEATTACK, KEY_NONE, MOD_CTRL);
		bind(CMSG_META_END_FORCEATTACK, KEY_NONE, MOD_CTRL, TRANSITION_UP);
		bind(CMSG_META_BEGIN_WAYPOINTS, KEY_NONE, MOD_ALT);
		bind(CMSG_META_END_WAYPOINTS, KEY_NONE, MOD_ALT, TRANSITION_UP);
		bind(CMSG_META_STOP, KEY_S, MOD_NONE);
		bind(CMSG_META_CREATE_FORMATION, KEY_F, MOD_CTRL);
		bind(CMSG_META_CREATE_TEAM1, KEY_1, MOD_CTRL);
		bind(CMSG_META_SELECT_TEAM1, KEY_1, MOD_NONE);
		bind(CMSG_META_ADD_TO_TEAM1, KEY_1, MOD_CTRL | MOD_SHIFT);
		input = std::make_unique<HudInput>(*logic, nullptr, view, commands, MouseSettings{ 15, 5, 150 }, map);
		input->commandTranslator().setUseAlternateMouse(true);
	}
	void bind(int meta, int key, int mods, int transition = TRANSITION_DOWN)
	{
		MetaMapRec &r = map.getMetaMapRec(meta);
		r.key = key; r.modState = mods; r.transition = transition; r.usableIn = COMMANDUSABLE_GAME;
	}
	Object *place(const char *name, int x, int y, const char *owner = "Alice")
	{
		Object *o = make(name, teamOf(owner));
		const Coord3D p{ (float)x, (float)y, 0 };
		o->setPosition(&p);
		return o;
	}
	void move(int x, int y) { input->mouseMove(x, y, modifiers); input->update(); }
	void button(HudInput::Button b, bool down, int x, int y, bool dbl = false, bool gui = false, int elapsed = 40)
	{
		timeMs += elapsed;
		input->mouseButton(b, down, x, y, modifiers, timeMs, dbl, gui);
		input->update();
	}
	void click(int x, int y, HudInput::Button b = HudInput::Button::Left, bool dbl = false)
	{
		move(x, y);
		button(b, true, x, y, dbl, false, 300);
		button(b, false, x, y);
	}
	void drag(int x0, int y0, int x1, int y1, HudInput::Button b = HudInput::Button::Left, int elapsed = 200)
	{
		move(x0, y0); button(b, true, x0, y0, false, false, 300);
		move(x1, y1); button(b, false, x1, y1, false, false, elapsed);
	}
	void modifier(int key, int bit, bool down)
	{
		if (down) modifiers |= bit; else modifiers &= ~bit;
		input->key(key, modifiers | (down ? KEY_STATE_DOWN : KEY_STATE_UP));
		input->update();
	}
	void press(int key, char32_t ch = 0)
	{
		input->key(key, modifiers | KEY_STATE_DOWN, ch); input->update();
		input->key(key, modifiers | KEY_STATE_UP, ch); input->update();
	}
	void drain()
	{
		input->update();
		for (const GameMessage &m : commands.messages()) dispatch.dispatch(m);
		commands.reset();
		REQUIRE(dispatch.errors().empty());
	}
	void selected(std::vector<ObjectID> ids)
	{
		std::string trace;
		for (const auto &line : input->messageLog()) trace += line + "\n";
		INFO(trace);
		auto actual = input->ui().selected();
		std::sort(ids.begin(), ids.end()); std::sort(actual.begin(), actual.end());
		CHECK(actual == ids);
		drain();
		actual = players.getLocalPlayer()->selection();
		std::sort(actual.begin(), actual.end());
		std::string idsTrace = "logic selection:";
		for (ObjectID id : actual) idsTrace += " " + std::to_string(id);
		idsTrace += "; expected:";
		for (ObjectID id : ids) idsTrace += " " + std::to_string(id);
		INFO(idsTrace);
		CHECK(actual == ids);
	}
	const GameMessage &onlyCommand(int type)
	{
		std::string trace;
		for (const auto &line : input->messageLog()) trace += line + "\n";
		INFO(trace);
		REQUIRE(commands.messages().size() == 1);
		const GameMessage &m = commands.messages().front();
		REQUIRE(m.getType() == type);
		CHECK(m.getPlayerIndex() == players.getLocalPlayer()->getPlayerIndex());
		return m;
	}
	void locationCommand(int type, int x, int y)
	{
		const GameMessage &m = onlyCommand(type);
		REQUIRE(m.getArgumentCount() == 1);
		REQUIRE(m.getArgument(0)->type == ARGUMENTDATATYPE_LOCATION);
		CHECK(m.getArgument(0)->location.x == (float)x);
		CHECK(m.getArgument(0)->location.y == (float)y);
		CHECK(m.getArgument(0)->location.z == 0.0f);
	}
};
} // namespace synthetichud
