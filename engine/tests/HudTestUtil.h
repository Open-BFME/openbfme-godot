// OpenBFME unit tests. GPL-3.0.
// Fixture of the HUD-1 tests: the retail object world and a LiveGame on a skirmish map with a tactical view, the HUD input stack (HudInput) and helpers that
// drive it with synthetic mouse and keyboard events, as the device layer would. The retail tests SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#pragma once

#include "doctest.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
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
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <sstream>
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

	// lane VISUAL-2 r2: `allyFaction` adds a computer Player_3 on the local player's team (start 2)
	explicit Rig(SharedWorld &s, const char *mapName = "map mp fall back 4p", const char *faction = "FactionMen", const char *allyFaction = nullptr) // lane SPELL-2: the local faction
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
		if (allyFaction)
		{
			o.slots.players.push_back({ "Player_3", allyFaction, false, 0, 0, 2 });
		}
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

	// whether every ground pathfind cell within `radius` of (x, y) is clear ground (not water, cliff, an obstacle's footprint or off the map)
	bool clearGround(float x, float y, float radius)
	{
		const float cell = 10.0f; // the pathfind cell (Pathfinder::worldToCell)
		Pathfinder &pf = game->ai().pathfinder();
		const int r = (int)(radius / cell);
		const int cx = (int)std::floor(x / cell), cy = (int)std::floor(y / cell);
		for (int j = -r; j <= r; ++j)
		{
			for (int i = -r; i <= r; ++i)
			{
				if (i * i + j * j > r * r)
				{
					continue;
				}
				const PathfindCell *c = pf.getCell(LAYER_GROUND, cx + i, cy + j);
				if (!c || c->getType() != PathfindCell::CELL_CLEAR)
				{
					return false;
				}
			}
		}
		return true;
	}
	// the nearest point to (x, y) (a ring search, step 60, out to 4800) on clear ground within 40 of it and at least `clear` units from: the point make()
	// placed each object that still exists at, and the current position of every other object except the map's critters (mobile and IGNORED_IN_GUI:
	// the wandering rabbits, dogs, crows) and props (no AI: the ground test sees their footprints); a creep (a lair, its guards) attacks what comes
	// near: 400 more, unless `nearCreeps`. `reach`: the offsets of the scene's other members, each on clear ground with a clear straight way from the
	// spot (a melee attacker walks it). `walkFrom`: a unit there walks to the spot, no cliff or water on the straight way.
	// Lane PATHPERF-1 (the MERGEQ-2 FACT failures): the spot does not depend on where the critters and the actors of earlier items walked, so a
	// pathfinder change does not move a scene's actors, and no actor stands on a cliff, in water or in a footprint (a unit there cannot move, and the
	// spot an earlier ring search gave depended on the critters' routes). A ground test wider than 40 finds no spot for the large scenes of the maps.
	// freeSpot's test of one point: false with `terrain` set when the ground fails, false when an object is too close
	bool spotFree(float px, float py, float clear, bool nearCreeps, const std::vector<std::pair<float, float>> &reach, const Coord3D *walkFrom, bool &terrain)
	{
		bool ground = clearGround(px, py, 40.0f);
		for (size_t m = 0; m < reach.size() && ground; ++m)
		{
			// a member of the scene at (px, py) + reach[m]: on clear ground, and the straight way to it too (a melee attacker walks it)
			const float len = std::sqrt(reach[m].first * reach[m].first + reach[m].second * reach[m].second);
			const int steps = (int)(len / 20.0f);
			for (int t = 1; t <= steps && ground; ++t)
			{
				ground = clearGround(px + reach[m].first * (float)t / (float)steps, py + reach[m].second * (float)t / (float)steps, 20.0f);
			}
			ground = ground && clearGround(px + reach[m].first, py + reach[m].second, 40.0f);
		}
		if (ground && walkFrom)
		{
			// a unit at walkFrom (beside its building's footprint) walks here: no cliff or water on the straight way
			const float wx = px - walkFrom->x, wy = py - walkFrom->y;
			const int steps = (int)(std::sqrt(wx * wx + wy * wy) / 10.0f);
			for (int t = 1; t <= steps && ground; ++t)
			{
				const PathfindCell *pc = game->ai().pathfinder().getCell(LAYER_GROUND, (int)std::floor((walkFrom->x + wx * (float)t / (float)steps) / 10.0f),
					(int)std::floor((walkFrom->y + wy * (float)t / (float)steps) / 10.0f));
				ground = pc && pc->getType() != PathfindCell::CELL_CLIFF && pc->getType() != PathfindCell::CELL_WATER;
			}
		}
		terrain = !ground;
		if (!ground)
		{
			return false;
		}
		for (Object *o = logic().getFirstObject(); o; o = o->getNextObject())
		{
			const bool mobile = o->getAIUpdateInterface() != nullptr && !o->isKindOfName("STRUCTURE");
			if ((mobile && o->isKindOfName("IGNORED_IN_GUI")) || (!mobile && !o->isKindOfName("STRUCTURE")) || placedIds.count(o->getID()))
			{
				continue; // a critter, a prop (its footprint: clearGround) or an actor of the rig (its spawn point below)
			}
			// a creep (the map's PlyrCreeps: a lair and its guards) attacks what comes within its guard range: 400 more
			const Player *owner = o->getControllingPlayer();
			const float keep = owner && owner->getPlayerName() == "PlyrCreeps" && !nearCreeps ? clear + 400.0f : clear;
			const float dx = o->getPosition()->x - px, dy = o->getPosition()->y - py;
			if (dx * dx + dy * dy < keep * keep)
			{
				return false;
			}
		}
		for (const auto &pl : placed)
		{
			const float dx = pl.second.x - px, dy = pl.second.y - py;
			if (dx * dx + dy * dy < clear * clear && logic().findObjectByID(pl.first) != nullptr)
			{
				return false;
			}
		}
		return true;
	}
	// lane PLAY-4 r3: with `mapMargin` >= 0 the point stays that far inside the pathfinder's logical extent (10 per cell): a scene that clicks an enemy needs
	// it, since off the map the shroud never clears and the click is refused (S-565)
	Coord3D freeSpot(float x, float y, float clear = 140.0f, bool nearCreeps = false, const std::vector<std::pair<float, float>> &reach = {}, const Coord3D *walkFrom = nullptr,
		float mapMargin = -1.0f)
	{
		Coord3D at{};
		std::string why;
		if (!tryFreeSpot(x, y, clear, at, &why, nearCreeps, reach, walkFrom, mapMargin))
		{
			FAIL(why);
		}
		return at;
	}
	// freeSpot without the failure: false (and `why`) when no point passes (a scene that lowers its clearance until one does)
	bool tryFreeSpot(float x, float y, float clear, Coord3D &at, std::string *why = nullptr, bool nearCreeps = false, const std::vector<std::pair<float, float>> &reach = {},
		const Coord3D *walkFrom = nullptr, float mapMargin = -1.0f)
	{
		ICoord2D lo{}, hi{};
		if (mapMargin >= 0.0f && logic().aiWorld())
		{
			logic().aiWorld()->pathfinder().getLogicalExtent(lo, hi);
		}
		const bool bounded = hi.x > lo.x && hi.y > lo.y;
		int terrain = 0, objects = 0, outside = 0;
		for (int ring = 0; ring < 80; ++ring)
		{
			for (int k = 0; k < (ring == 0 ? 1 : 8 * ring); ++k)
			{
				const int per = ring == 0 ? 1 : 8 * ring;
				const float a = 6.2831853f * (float)k / (float)per;
				const float px = x + std::cos(a) * 60.0f * (float)ring, py = y + std::sin(a) * 60.0f * (float)ring;
				if (bounded && !(px - mapMargin >= 10.0f * (float)lo.x && px + mapMargin <= 10.0f * (float)(hi.x + 1) && py - mapMargin >= 10.0f * (float)lo.y &&
								 py + mapMargin <= 10.0f * (float)(hi.y + 1)))
				{
					++outside;
					continue;
				}
				bool offGround = false;
				if (spotFree(px, py, clear, nearCreeps, reach, walkFrom, offGround))
				{
					at = Coord3D{ px, py, 0.0f };
					return true;
				}
				++(offGround ? terrain : objects);
			}
		}
		if (why)
		{
			std::ostringstream w;
			w << "no free spot near (" << x << ", " << y << ") clear " << clear << ": " << terrain << " points off clear ground, " << objects << " too close to an object, "
			  << outside << " outside the map margin " << mapMargin;
			*why = w.str();
		}
		return false;
	}
	std::vector<std::pair<ObjectID, Coord3D>> placed; // every object make() placed, and where
	std::set<ObjectID> placedIds;
	// makes an object for the local player (or `owner`) at (x, y)
	Object *make(const std::string &templateName, float x, float y, Player *owner = nullptr)
	{
		std::string err;
		Object *o = game->createObject(templateName, (owner ? owner : local)->getPlayerIndex(), Coord3D{ x, y, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(o != nullptr, err);
		placed.push_back({ o->getID(), Coord3D{ x, y, 0.0f } });
		placedIds.insert(o->getID());
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
// lane SELECT-1: the world points inside a drawable's oriented pick box (the first top-LOD W3D_BOX_ATTRIBUTE_ORIENTED box of its drawn model, on its pivot's bind pose):
// the centre and the centre +- 80 % of each half extent along the box's own axes. Empty when the model has no such box
inline std::vector<Coord3D> pickBoxPoints(const Drawable &d)
{
	std::vector<Coord3D> out;
	for (const DrawEntry &e : d.entries())
	{
		const RenderObjPrototype *proto = e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.moduleHidden && !e.conditionHidden ? e.draw->currentModel() : nullptr;
		if (!proto)
		{
			continue;
		}
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			if (sub.Type != RenderSubObject::SUB_BOX || !sub.Box || sub.Aggregate || (sub.Box->Attributes & 1u) == 0)
			{
				continue;
			}
			const HTreeClass *tree = proto->Tree ? proto->Tree : &proto->DefaultTree;
			HTreePose pose;
			tree->Base_Pose(Matrix3D(), pose);
			const Matrix3D pivot = sub.BoneIndex >= 0 && sub.BoneIndex < (int)pose.Transform.size() ? pose.Transform[(size_t)sub.BoneIndex] : Matrix3D();
			const W3dBoxStruct &b = *sub.Box;
			const float c[3] = { b.Center.X, b.Center.Y, b.Center.Z }, ex[3] = { b.Extent.X, b.Extent.Y, b.Extent.Z };
			std::vector<std::array<float, 3>> local{ { c[0], c[1], c[2] } };
			for (int axis = 0; axis < 3; ++axis)
			{
				for (float sign : { -0.8f, 0.8f })
				{
					std::array<float, 3> p{ c[0], c[1], c[2] };
					p[(size_t)axis] += sign * ex[axis];
					local.push_back(p);
				}
			}
			const float *basis = d.getBasis();
			const float scale = d.getInstanceScale();
			const Coord3D at = d.entryPosition(e);
			for (const std::array<float, 3> &p : local)
			{
				float m[3];
				for (int r = 0; r < 3; ++r)
				{
					m[r] = (pivot.Row[r][0] * p[0] + pivot.Row[r][1] * p[1] + pivot.Row[r][2] * p[2] + pivot.Row[r][3]) * scale;
				}
				out.push_back(Coord3D{ at.x + basis[0] * m[0] + basis[1] * m[1] + basis[2] * m[2], at.y + basis[3] * m[0] + basis[4] * m[1] + basis[5] * m[2],
					at.z + basis[6] * m[0] + basis[7] * m[1] + basis[8] * m[2] });
			}
			return out; // RW 0x59C680: the first one answers
		}
	}
	return out;
}
} // namespace hudtest
