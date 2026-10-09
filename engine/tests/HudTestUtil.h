// OpenBFME unit tests. GPL-3.0.
// Fixture of the HUD-1 tests: the retail object world and a LiveGame on a skirmish map with a tactical view, the HUD input stack (HudInput) and helpers that
// drive it with synthetic mouse and keyboard events, as the device layer would. The retail tests SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#pragma once

#include "doctest.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/HudInput.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/TacticalView.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Thing/ThingTemplate.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace hudtest
{
struct SharedWorld
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
	MapObjectOptions options;
	MouseSettings mouse;
	std::unique_ptr<INIEnvironment> metaEnv;
	MetaMap meta;
};

inline SharedWorld &shared()
{
	static SharedWorld s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
			else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapObjectGameData::loadPlayerTemplates(*s.mount->fs, s.options, &s.error)
				|| !MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error))
			{
				s.world.reset();
			}
			else if (!MouseSettings::load(*s.mount->fs, s.mouse, &s.error))
			{
				s.world.reset();
			}
			else
			{
				s.metaEnv = std::make_unique<INIEnvironment>();
				s.metaEnv->fileSystem = s.mount->fs.get();
				s.meta.registerBlocks(*s.metaEnv);
				// the default map then the language's (retail data: Data\INI\CommandMap.ini has one block, the English archive's CommandMap.ini 94)
				if (!s.meta.load(*s.metaEnv, "Data\\INI\\CommandMap.ini", &s.error) || !s.meta.load(*s.metaEnv, "CommandMap.ini", &s.error))
				{
					s.world.reset();
				}
			}
		}
	}
	return s;
}

inline bool haveWorld(const char *what)
{
	SharedWorld &s = shared();
	if (!s.mount)
	{
		retailtest::printSkip(what);
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

// a retail game with the HUD input on it
struct Rig
{
	SharedWorld &sh;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	PinholeView view;
	std::unique_ptr<HudInput> input;
	Player *local = nullptr;
	int timeMs = 1000;
	int keyState = 0;

	explicit Rig(SharedWorld &s, const char *mapName = "map mp fall back 4p", const char *faction = "FactionMen") // lane SPELL-2: the local faction
		: sh(s)
		, source(*s.mount->fs)
		, assets(source)
	{
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = mapName;
		o.seed = 4711;
		o.slots.players.push_back({ "Player_1", faction, true, 0, 0, 0 });
		o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
		std::string err;
		REQUIRE_MESSAGE(game->load(o, &err), err);
		local = game->players().findPlayerWithName("Player_1");
		REQUIRE(local != nullptr);
		game->players().setLocalPlayer(local);
		view = PinholeView({ 0, -400, 500 }, { 0, 0, 0 }, 1024, 768, 0.7f);
		input = std::make_unique<HudInput>(game->logic(), &game->ai(), view, game->commands(), s.mouse, s.meta);
	}
	~Rig()
	{
		input.reset();
		game->logic().reset();
	}

	GameLogic &logic() { return game->logic(); }
	int index() const { return local->getPlayerIndex(); }

	// the camera looks at `target` from the south at a fixed pitch
	void lookAt(const Coord3D &target, float back = 420.0f, float up = 520.0f)
	{
		view.set({ target.x, target.y - back, target.z + up }, target);
	}
	ICoord2D screenOf(const Coord3D &world) const
	{
		ICoord2D p{ -1, -1 };
		REQUIRE(view.worldToScreen(world, p));
		return p;
	}
	// one logic frame after the queued input
	void frame(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			input->update();
			game->advance(0.2);
		}
	}
	void move(int x, int y)
	{
		timeMs += 20;
		input->mouseMove(x, y, keyState);
		input->update();
	}
	void button(HudInput::Button b, bool down, int x, int y, bool dbl = false)
	{
		timeMs += 20;
		input->mouseButton(b, down, x, y, keyState, timeMs, dbl);
		input->update();
	}
	void leftClick(int x, int y, bool dbl = false)
	{
		move(x, y);
		button(HudInput::Button::Left, true, x, y, dbl);
		button(HudInput::Button::Left, false, x, y);
	}
	void rightClick(int x, int y)
	{
		move(x, y);
		button(HudInput::Button::Right, true, x, y);
		button(HudInput::Button::Right, false, x, y);
	}
	void leftDrag(int x0, int y0, int x1, int y1)
	{
		move(x0, y0);
		button(HudInput::Button::Left, true, x0, y0);
		const int steps = 6;
		for (int i = 1; i <= steps; ++i)
		{
			move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
		}
		button(HudInput::Button::Left, false, x1, y1);
	}
	// a key press with the modifier keys held, each modifier going down before the key and up after it (as a keyboard sends them: a key that arrives with a
	// new modifier state would be taken for the modifier change)
	void pressKey(int key, int modifiers = 0)
	{
		const int ctrl = modifiers & KEY_STATE_CONTROL, shift = modifiers & KEY_STATE_SHIFT, alt = modifiers & KEY_STATE_ALT;
		if (ctrl) setModifier(KEY_LCTRL, KEY_STATE_LCONTROL, true);
		if (shift) setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, true);
		if (alt) setModifier(KEY_LALT, KEY_STATE_LALT, true);
		input->key(key, KEY_STATE_DOWN | keyState);
		input->update();
		input->key(key, KEY_STATE_UP | keyState);
		input->update();
		if (alt) setModifier(KEY_LALT, KEY_STATE_LALT, false);
		if (shift) setModifier(KEY_LSHIFT, KEY_STATE_LSHIFT, false);
		if (ctrl) setModifier(KEY_LCTRL, KEY_STATE_LCONTROL, false);
	}
	void setModifier(int modifierKey, int flag, bool down)
	{
		keyState = down ? (keyState | flag) : (keyState & ~flag);
		input->key(modifierKey, (down ? KEY_STATE_DOWN : KEY_STATE_UP) | keyState);
		input->update();
	}

	// the nearest point to (x, y) (a ring search, step 60) at least `clear` units from every object and inside the map
	Coord3D freeSpot(float x, float y, float clear = 140.0f)
	{
		for (int ring = 0; ring < 40; ++ring)
		{
			for (int k = 0; k < (ring == 0 ? 1 : 8 * ring); ++k)
			{
				const int per = ring == 0 ? 1 : 8 * ring;
				const float a = 6.2831853f * (float)k / (float)per;
				const float px = x + std::cos(a) * 60.0f * (float)ring, py = y + std::sin(a) * 60.0f * (float)ring;
				bool ok = true;
				for (Object *o = logic().getFirstObject(); o && ok; o = o->getNextObject())
				{
					const float dx = o->getPosition()->x - px, dy = o->getPosition()->y - py;
					ok = dx * dx + dy * dy >= clear * clear;
				}
				if (ok)
				{
					return Coord3D{ px, py, 0.0f };
				}
			}
		}
		FAIL("no free spot near the point");
		return Coord3D{};
	}
	// makes an object for the local player (or `owner`) at (x, y)
	Object *make(const std::string &templateName, float x, float y, Player *owner = nullptr)
	{
		std::string err;
		Object *o = game->createObject(templateName, (owner ? owner : local)->getPlayerIndex(), Coord3D{ x, y, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(o != nullptr, err);
		return o;
	}
};
} // namespace hudtest

namespace hudtest
{
inline bool hasNugget(const ThingTemplate &tt, const char *name)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name.find(name) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

// The templates the command buttons of the install build (lexical order of the buttons), by kind: infantry that is not a horde, a horde, and a structure that produces.
struct Templates
{
	std::vector<std::string> infantry, hordes;
};
inline const Templates &templates(SharedWorld &s)
{
	static Templates t;
	static bool built = false;
	if (!built)
	{
		built = true;
		const KindOfMaskType none{};
		(void)none;
		for (const std::string &name : s.world->commands().buttonNames())
		{
			const CommandButton *b = s.world->commands().findCommandButton(name);
			if (!b || b->m_command != GUI_COMMAND_UNIT_BUILD || !b->getThingTemplate())
			{
				continue;
			}
			const ThingTemplate &tt = *b->getThingTemplate()->getFinalOverride();
			const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(tt);
			const bool infantry = MaskTest(info.kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY"));
			const bool selectable = MaskTest(info.kindOf, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("SELECTABLE"));
			if (!selectable || !hasNugget(tt, "AIUpdate"))
			{
				continue;
			}
			std::vector<std::string> &dest = hasNugget(tt, "HordeContain") ? t.hordes : t.infantry;
			if ((infantry || hasNugget(tt, "HordeContain")) && std::find(dest.begin(), dest.end(), tt.getName()) == dest.end())
			{
				dest.push_back(tt.getName());
			}
		}
	}
	return t;
}
} // namespace hudtest
