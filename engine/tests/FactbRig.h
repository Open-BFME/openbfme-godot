// OpenBFME unit tests (lane FACT-B). A retail skirmish game with the in-game HUD for a local player of one faction, driven only through what a player
// does: clicks on objects and on the ground, presses of the control bar's buttons (the visible ones, opening the button pages with their own
// PUSH_VISIBLE_COMMAND_RANGE buttons), never the logic's internal shortcuts for what is being tested. Each step runs whole logic frames (0.2 s).

#pragma once

#include "FactbInventory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameLogic/NewGame/NewGame.h"
#include "HudTestUtil.h"

#include "GameClient/ControlBar.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace factb
{
inline void prepareStartedSkirmish(const std::string &faction, NewGameStart &start, bool idleOpponent = false);

// lane FACT-C: while a StartedScope lives, every HudGame made without an explicit choice runs a started skirmish whose slot 1 is a human that never sends
// a command (the FACT-C cases run the long sweeps on the lobby's start without the opponent's raids)
inline bool &startedByDefault()
{
	static bool on = false;
	return on;
}
struct StartedScope
{
	StartedScope() { startedByDefault() = true; }
	~StartedScope() { startedByDefault() = false; }
};

struct HudGame
{
	hudtest::Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	std::unique_ptr<NewGameStart> start;
	Player *enemy = nullptr; ///< Player_2 on the bare map, slot 1 in a started skirmish

	// `started`: the HUD on a started skirmish (prepareStartedSkirmish: the starting base and its castle, slot 1 the enemy) instead of the bare map
	// (lane FACT-C: `started` defaults to an open StartedScope, whose start has the idle opponent)
	HudGame(hudtest::SharedWorld &s, const char *faction, bool started = startedByDefault()) : rig(s, "map mp fall back 4p", faction)
	{
		if (started)
		{
			start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
			prepareStartedSkirmish(faction, *start, startedByDefault());
			auto scope = s.world->enterContext();
			rig.input.reset();
			rig.game->logic().reset();
			rig.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, rig.assets, s.options);
			LiveGame::Options o;
			o.start = start.get();
			std::string err;
			REQUIRE_MESSAGE(rig.game->load(o, &err), err);
			rig.local = rig.game->players().findPlayerWithName(rig.game->report().startSlotPlayers[0]);
			REQUIRE(rig.local);
			enemy = rig.game->players().findPlayerWithName(rig.game->report().startSlotPlayers[1]);
			rig.game->players().setLocalPlayer(rig.local);
			rig.input = std::make_unique<HudInput>(rig.game->logic(), &rig.game->ai(), rig.view, rig.game->commands(), s.mouse, s.meta);
		}
		else
		{
			enemy = rig.game->players().findPlayerWithName("Player_2");
		}
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
		context = hud->enterContext(); // the control bar reads the process-wide stores
		step(3);                       // the starting castle unpacks
	}
	~HudGame()
	{
		context.reset();
		hud.reset();
	}
	GameLogic &logic() { return rig.logic(); }
	unsigned frame() { return logic().getFrame(); }
	// n logic frames of 0.2 s, the HUD refreshed before each
	void step(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.2);
			rig.game->advance(0.2);
		}
	}
	// the pointer to the world point (the camera looks at it first), each raw event followed by the input stack's update (as the device layer runs it)
	float cameraBack = 420.0f; ///< the camera's offset south of the point it looks at (negative: from the north)
	ICoord2D pointAt(const Coord3D &p)
	{
		rig.lookAt(p, cameraBack);
		hud->update(0.01);
		const ICoord2D px = rig.screenOf(p);
		hud->mouseMove(px.x, px.y);
		hud->input().update();
		return px;
	}
	void clickAt(const Coord3D &p, HudInput::Button b = HudInput::Button::Left)
	{
		const ICoord2D px = pointAt(p);
		hud->mouseButton(b, true, px.x, px.y, 0, rig.timeMs += 500);
		hud->input().update();
		hud->mouseButton(b, false, px.x, px.y, 0, rig.timeMs += 40);
		hud->input().update();
	}
	// a left click on the object (the selection reaches the logic as MSG_CREATE_SELECTED_GROUP); true when the selection is the object, or the horde of
	// the member the click hit, or a member of the clicked horde
	// `sameTemplate`: another object of the same template (or its horde) counts too. The click tries a few heights over the position (the pick volume
	// of a tall or long model may not cover its foot)
	bool select(Object *o, bool sameTemplate = false)
	{
		const Object *at = o;
		if (o->getContain() && o->getContain()->getContainCount() > 0 && !o->getContain()->getContainedItemsList()->empty())
		{
			at = o->getContain()->getContainedItemsList()->front(); // a horde is clicked on one of its members (the horde object has no body to pick)
		}
		// the camera from the south, then from the north (a building between the camera and a unit at its door takes the click)
		for (float back : { 420.0f, -420.0f })
		{
			cameraBack = back;
			for (float dz : { 10.0f, 25.0f, 45.0f, 3.0f })
			{
				hud->input().ui().deselectAll();
				step(1);
				clickAt({ at->getPosition()->x, at->getPosition()->y, at->getPosition()->z + dz });
				step(2);
				for (ObjectID id : hud->input().ui().selected())
				{
					const Object *s = logic().findObjectByID(id);
					if (s && (s == o || s->getContainedBy() == o || o->getContainedBy() == s))
					{
						cameraBack = 420.0f;
						return true;
					}
					if (s && sameTemplate && (s->getTemplate() == o->getTemplate() || (s->getContainedBy() && s->getContainedBy()->getTemplate() == o->getTemplate())))
					{
						cameraBack = 420.0f;
						return true;
					}
				}
			}
		}
		cameraBack = 420.0f;
		// a drag box around it (the region select takes objects by their position, not the drawn model's pick): what a player does when the click
		// does not take (another model in front of it)
		hud->input().ui().deselectAll();
		step(1);
		const ICoord2D c = pointAt(*at->getPosition());
		hud->mouseButton(HudInput::Button::Left, true, c.x - 30, c.y - 30, 0, rig.timeMs += 500);
		hud->input().update();
		for (int k = 1; k <= 6; ++k)
		{
			hud->mouseMove(c.x - 30 + 10 * k, c.y - 30 + 10 * k);
			hud->input().update();
		}
		hud->mouseButton(HudInput::Button::Left, false, c.x + 30, c.y + 30, 0, rig.timeMs += 40);
		hud->input().update();
		step(2);
		for (ObjectID id : hud->input().ui().selected())
		{
			const Object *s = logic().findObjectByID(id);
			if (s && (s == o || s->getContainedBy() == o || o->getContainedBy() == s || (sameTemplate && s->getTemplate() == o->getTemplate())))
			{
				return true; // (the box may hold another object too; the control bar shows the first selected one's set)
			}
		}
		return false;
	}
	// the ids of the selection, for the failure messages
	std::string selectionText()
	{
		std::string out;
		for (ObjectID id : hud->input().ui().selected())
		{
			const Object *s = logic().findObjectByID(id);
			out += (out.empty() ? "" : ",") + (s ? s->getTemplate()->getName() : std::string("?")) + "#" + std::to_string(id);
		}
		return out.empty() ? "nothing" : out;
	}
	// the last lines of the message log of the input stack (failure messages)
	std::string lastMessages(size_t n = 4)
	{
		const std::vector<std::string> &log = hud->input().messageLog();
		std::string out;
		for (size_t i = log.size() > n ? log.size() - n : 0; i < log.size(); ++i)
		{
			out += (out.empty() ? "" : " | ") + log[i];
		}
		return out;
	}
	std::vector<ControlBarButton> visible()
	{
		hud->update(0.01);
		std::vector<ControlBarButton> out;
		for (const auto *v : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons() })
		{
			for (const ControlBarButton &b : *v)
			{
				if (b.button)
				{
					out.push_back(b);
				}
			}
		}
		return out;
	}
	// the button `want` accepts, on the visible bar or behind one or two page buttons of the selection's set (`o` is reselected to reset the pages)
	bool reveal(Object *o, const std::function<bool(const CommandButton &)> &want, ControlBarButton &out)
	{
		return revealBar(o, [&](const ControlBarButton &b) { return want(*b.button); }, out);
	}
	// the same with the evaluated button (its revive record, state, cost)
	bool revealBar(Object *o, const std::function<bool(const ControlBarButton &)> &want, ControlBarButton &out)
	{
		auto look = [&]() {
			for (const ControlBarButton &b : visible())
			{
				if (want(b))
				{
					out = b;
					return true;
				}
			}
			return false;
		};
		if (look())
		{
			return true;
		}
		std::vector<std::vector<int>> paths;
		for (const ControlBarButton &b : visible())
		{
			if (b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
			{
				paths.push_back({ b.slot * 2 + (b.inPalantir ? 1 : 0) });
			}
		}
		for (size_t i = 0; i < paths.size(); ++i)
		{
			if (!select(o))
			{
				return false;
			}
			bool pressed = true;
			for (int key : paths[i])
			{
				pressed = pressed && hud->controlBar().pressButton(key / 2, (key & 1) != 0);
				hud->update(0.01);
			}
			if (pressed && look())
			{
				return true;
			}
			if (paths[i].size() == 1)
			{
				for (const ControlBarButton &b : visible())
				{
					if (b.button->m_command == GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE)
					{
						paths.push_back({ paths[i][0], b.slot * 2 + (b.inPalantir ? 1 : 0) });
					}
				}
			}
		}
		return false;
	}
	bool press(const ControlBarButton &b)
	{
		const bool ok = hud->controlBar().pressButton(b.slot, b.inPalantir);
		hud->update(0.01);
		return ok;
	}
	Object *newestOf(const std::string &templateName, ObjectID after)
	{
		Object *best = nullptr;
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate() && o->getTemplate()->getName() == templateName && o->getID() > after && !o->isEffectivelyDead()
				&& o->getControllingPlayer() == rig.local && (!best || o->getID() > best->getID()))
			{
				best = o;
			}
		}
		return best;
	}
	ObjectID highestID()
	{
		ObjectID id = 0;
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			id = std::max(id, o->getID());
		}
		return id;
	}
	// the money the player has spent (ScoreKeeper::addMoneySpent through Money::withdraw, RW 0x7B17EF): a price is the difference, whatever the
	// player earns meanwhile
	unsigned spent() { return rig.local->getScoreKeeper().moneySpent(); }
	// a base for the tests' objects: the point of the map (a 250 grid inside the objects' bounding box) farthest from every object of another player
	// that can fight or spawns fighters (the map's creep lairs and their creeps): a bare map load has no starting bases, and creeps kill what is built
	// near them
	Coord3D home()
	{
		if (m_homeSet)
		{
			return m_home;
		}
		float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
		std::vector<Coord3D> danger;
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			const Coord3D &p = *o->getPosition();
			x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
			if (o->getControllingPlayer() != rig.local && o->getTemplate()
				&& (factb::hasKindOf(*o->getTemplate(), "CAN_ATTACK") || factb::nugget(*o->getTemplate(), "SpawnBehavior")))
			{
				danger.push_back(p);
			}
		}
		REQUIRE(x1 > x0);
		float best = -1.0f;
		for (float x = x0 + 600.0f; x <= x1 - 600.0f; x += 250.0f)
		{
			for (float y = y0 + 600.0f; y <= y1 - 600.0f; y += 250.0f)
			{
				float nearest = 1e9f;
				for (const Coord3D &d : danger)
				{
					nearest = std::min(nearest, (d.x - x) * (d.x - x) + (d.y - y) * (d.y - y));
				}
				if (nearest > best)
				{
					best = nearest;
					m_home = Coord3D{ x, y, 0.0f };
				}
			}
		}
		REQUIRE(best >= 0.0f);
		m_homeSet = true;
		return m_home;
	}
	Coord3D m_home{};
	bool m_homeSet = false;
};

// A skirmish set up as the lobby starts it (NewGame::prepareNewGame): `faction` in slot 0 (human) and an easy AI FactionMen in slot 1, on fall back 4p.
// LiveGame::Options::start with it places every occupied slot's starting base and unpacks its castle (RW 0x62AD24)
// lane FACT-C: `idleOpponent` makes slot 1 a second human that never sends a command
inline void prepareStartedSkirmish(const std::string &faction, NewGameStart &start, bool idleOpponent)
{
	hudtest::SharedWorld &s = hudtest::shared();
	auto scope = s.world->enterContext();
	std::string error;
	static GameLogicSettings settings;
	static std::vector<MapCacheEntry> maps;
	if (maps.empty())
	{
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, settings, &error), error);
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, maps, &error), error);
	}
	auto templateIndex = [&](const std::string &name) {
		for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
		{
			if (s.world->playerTemplates().getNthPlayerTemplate(i)->getName() == name)
			{
				return i;
			}
		}
		return -1;
	};
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = 5;
	m.game.startingCash = 1000;
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = i == 0 || idleOpponent ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = i == 0 ? u"Host" : (idleOpponent ? u"Idle" : u"Easy");
		sl.accepted = true;
		sl.playerTemplate = templateIndex(i == 0 ? faction : "FactionMen");
		REQUIRE(sl.playerTemplate >= 0);
		sl.startPos = i;
		sl.color = i;
	}
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), settings, maps, RandomAlgorithm::ZH_CarryChain, start, &error), error);
}

// the started skirmish (prepareStartedSkirmish); `inspect` sees the game and slot 0's player
inline void withStartedSkirmish(const std::string &faction, const std::function<void(LiveGame &, Player &)> &inspect)
{
	hudtest::SharedWorld &s = hudtest::shared();
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	prepareStartedSkirmish(faction, start);
	auto scope = s.world->enterContext();
	std::string error;
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	Player *p = game.players().findPlayerWithName(game.report().startSlotPlayers[0]);
	REQUIRE(p);
	game.advance(1.0); // the castle unpacks
	inspect(game, *p);
	game.logic().reset();
}

// the inventory of a faction with the objects its starting base and castle hold in a started skirmish (the keep, the expansion pads, the wall hubs,
// the starting units): found by starting one and listing slot 0's objects, so nothing is named here
inline const Inventory &inventory(const std::string &faction)
{
	static std::map<std::string, Inventory> cache;
	auto it = cache.find(faction);
	if (it == cache.end())
	{
		std::vector<std::pair<std::string, std::string>> seeds;
		withStartedSkirmish(faction, [&](LiveGame &game, Player &p) {
			for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == &p && o->getTemplate())
				{
					seeds.push_back({ o->getTemplate()->getName(), "the started skirmish's base" });
				}
			}
		});
		auto scope = hudtest::shared().world->enterContext();
		it = cache.emplace(faction, build(*hudtest::shared().world, faction, seeds)).first;
	}
	return it->second;
}
} // namespace factb
