// OpenBFME. GPL-3.0.
// Lane CHEAT-1: GameWorld's view of the open Cheat Mode (GameLogic/CheatMode.h; the command API is docs/CHEAT_MODE.md). The panel and any other caller (a
// stream-voting plugin) turn a choice into MSG_OPENBFME_CHEAT through cheat_command; the command then travels the lockstep command list like every other
// player command and every peer runs or refuses it. Nothing here writes logic state; the lists are display data.

#include "GodotDevice/GodotGameWorld.h"

#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/CheatMode.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/VictoryConditions.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <set>
#include <variant>

using namespace godot;

namespace
{
String g(const std::string &s) { return String::utf8(s.c_str()); }
std::string n(const String &s) { return std::string(s.utf8().get_data()); }

Dictionary fail(const std::string &why)
{
	Dictionary d;
	d["ok"] = false;
	d["error"] = g(why);
	return d;
}

bool fieldNumber(const ThingTemplate &tt, const char *name, double &out)
{
	const FieldValue *v = tt.findField(name);
	if (!v)
	{
		return false;
	}
	if (const long long *i = std::get_if<long long>(v))
	{
		out = *i;
		return true;
	}
	if (const float *f = std::get_if<float>(v))
	{
		out = *f;
		return true;
	}
	return false;
}

std::string fieldText(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.findField(name);
	const std::string *s = v ? std::get_if<std::string>(v) : nullptr;
	return s ? *s : std::string();
}
} // namespace

void GameWorld::bindCheatMethods()
{
	ClassDB::bind_method(D_METHOD("cheat_info"), &GameWorld::cheat_info);
	ClassDB::bind_method(D_METHOD("cheat_players"), &GameWorld::cheat_players);
	ClassDB::bind_method(D_METHOD("cheat_units", "player"), &GameWorld::cheat_units);
	ClassDB::bind_method(D_METHOD("cheat_heroes", "player"), &GameWorld::cheat_heroes);
	ClassDB::bind_method(D_METHOD("cheat_command", "command"), &GameWorld::cheat_command);
	ClassDB::bind_method(D_METHOD("take_cheat_lines"), &GameWorld::take_cheat_lines);
}

Dictionary GameWorld::cheat_info() const
{
	Dictionary d;
	d["enabled"] = false;
	if (!m_game)
	{
		return d;
	}
	const CheatMode::State &s = m_game->logic().cheatMode();
	const ::Player *local = m_game->players().getLocalPlayer();
	d["enabled"] = s.enabled();
	d["seat_slot"] = (int64_t)s.slot();
	d["seat_player"] = (int64_t)s.seatPlayer();
	const ::Player *seat = s.enabled() ? m_game->logic().players().getNthPlayer(s.seatPlayer()) : nullptr;
	d["seat_name"] = seat ? g(seat->getPlayerDisplayName()) : String();
	d["local_is_seat"] = s.enabled() && local && local->getPlayerIndex() == s.seatPlayer();
	d["voided"] = s.voided();
	d["executed"] = (int64_t)s.executedCount();
	d["refused"] = (int64_t)s.refusedCount();
	d["label"] = String(u"Cheat Mode");
	return d;
}

Array GameWorld::cheat_players() const
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	GameLogic &logic = m_game->logic();
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const ::Player *p = CheatMode::isGamePlayer(logic, i) ? logic.players().getNthPlayer(i) : nullptr;
		if (!p)
		{
			continue;
		}
		Dictionary d;
		d["index"] = (int64_t)p->getPlayerIndex();
		d["name"] = g(p->getPlayerDisplayName().empty() ? p->getPlayerName() : p->getPlayerDisplayName());
		d["side"] = g(p->getSide());
		d["defeated"] = p->isDefeated();
		out.push_back(d);
	}
	return out;
}

Array GameWorld::cheat_units(int64_t player) const
{
	// the units and heroes of the player's side: every template of that Side a command button builds, with a build cost, not a structure or a projectile, heroes
	// first, then by name. A mod's own units are listed the same way (nothing here names a retail template)
	Array out;
	if (!m_game)
	{
		return out;
	}
	GameLogic &logic = m_game->logic();
	const ::Player *p = logic.players().getNthPlayer((int)player);
	if (!p)
	{
		return out;
	}
	struct Row
	{
		bool hero;
		std::string name;
		int id;
		std::string label;
	};
	std::vector<Row> rows;
	// what a player can build: the templates the command buttons name (a build button's Object, RW parses it into the button at + 0x20); a template no
	// button names (a cinematic double, a map's own creature) is not offered, whatever its fields say
	std::set<const ThingTemplate *> buildable;
	if (m_world)
	{
		CommandStore &store = m_world->commands();
		for (const std::string &name : store.buttonNames())
		{
			const CommandButton *b = store.findCommandButton(name);
			if (b && b->m_thingTemplate)
			{
				buildable.insert(b->m_thingTemplate->getFinalOverride());
			}
		}
	}
	for (const ThingTemplate *tt : logic.things().templates())
	{
		if (!tt || fieldText(*tt, "Side") != p->getSide() || !buildable.count(tt->getFinalOverride()))
		{
			continue;
		}
		tt = tt->getFinalOverride();
		const Economy &economy = logic.economy();
		if (economy.templateKindOf(*tt, "STRUCTURE") || economy.templateKindOf(*tt, "PROJECTILE") || !economy.templateKindOf(*tt, "SELECTABLE"))
		{
			continue;
		}
		const bool hero = economy.templateKindOf(*tt, "HERO");
		double cost = 0;
		if (!fieldNumber(*tt, "BuildCost", cost) || cost <= 0) // a template without a price is no player's unit (a cinematic double, a map's own)
		{
			continue;
		}
		rows.push_back({ hero, tt->getName(), (int)tt->getTemplateID(), CommandButtonHelp::templateLabel(*tt, "DisplayName") });
	}
	// the player's heroes: the recruitable list its fortress offers (Player::heroes, the PlayerTemplate's buildable heroes), named by no build button
	const PlayerHeroList &list = p->heroes();
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		const ThingTemplate *tt = logic.things().findTemplate(list.records()[i].templateName);
		if (!tt || logic.economy().templateKindOf(*tt, "CREATE_A_HERO") || std::any_of(rows.begin(), rows.end(), [&](const Row &r) { return r.name == tt->getName(); }))
		{
			continue;
		}
		rows.push_back({ true, tt->getName(), (int)tt->getTemplateID(), CommandButtonHelp::templateLabel(*tt->getFinalOverride(), "DisplayName") });
	}
	std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.hero != b.hero ? a.hero : a.name < b.name; });
	for (const Row &r : rows)
	{
		Dictionary d;
		d["template"] = g(r.name);
		d["id"] = (int64_t)r.id;
		d["hero"] = r.hero;
		d["label"] = g(r.label);
		out.push_back(d);
	}
	return out;
}

Array GameWorld::cheat_heroes(int64_t player) const
{
	// the player's living objects that can gain levels, heroes first (the Heroes tab)
	Array heroes;
	if (!m_game)
	{
		return heroes;
	}
	GameLogic &logic = m_game->logic();
	const ::Player *p = logic.players().getNthPlayer((int)player);
	for (const ::Object *o = logic.getFirstObject(); o && p; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != p || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		const ExperienceTracker *xp = o->getExperienceTracker();
		if (!xp || !xp->isTrainable() || !o->isKindOfName("HERO"))
		{
			continue;
		}
		Dictionary d;
		d["id"] = (int64_t)o->getID();
		d["template"] = g(o->getTemplate()->getName());
		d["label"] = g(CommandButtonHelp::templateLabel(*o->getTemplate(), "DisplayName"));
		d["level"] = (int64_t)xp->getRank();
		heroes.push_back(d);
	}
	return heroes;
}

Dictionary GameWorld::cheat_command(const Dictionary &spec)
{
	// { command: "add_resources" | "add_command_points" | "add_power_points" | "spawn" | "level_up" | "reveal_map" | "end_game" | "void_game", target: player
	// index, value: int, template: name (spawn), object: id (level_up), x, y (spawn: SAGE coordinates) } -> { ok, error }
	if (!m_game)
	{
		return fail("no game");
	}
	const CheatMode::State &s = m_game->logic().cheatMode();
	if (!s.enabled())
	{
		return fail("this is not a Cheat Mode game");
	}
	const ::Player *local = m_game->players().getLocalPlayer();
	if (!local || local->getPlayerIndex() != s.seatPlayer())
	{
		return fail("only the host's seat sends Cheat Mode commands");
	}
	const int command = CheatMode::commandFromName(n(String(spec.get("command", String()))));
	if (command == 0)
	{
		return fail("unknown command " + n(String(spec.get("command", String()))));
	}
	const int target = (int)(int64_t)spec.get("target", (int64_t)-1);
	const int value = (int)(int64_t)spec.get("value", (int64_t)0);
	int extra = 0;
	Coord3D at{ 0.0f, 0.0f, 0.0f };
	if (command == (int)CheatMode::Command::Spawn)
	{
		const ThingTemplate *tt = m_game->logic().things().findTemplate(n(String(spec.get("template", String()))));
		if (!tt)
		{
			return fail("no unit " + n(String(spec.get("template", String()))));
		}
		extra = (int)tt->getTemplateID(); // the master template's id, as MSG_QUEUE_UNIT_CREATE names one (findByTemplateID, then its final override)
		at.x = (float)(double)spec.get("x", 0.0);
		at.y = (float)(double)spec.get("y", 0.0);
	}
	else if (command == (int)CheatMode::Command::LevelUp)
	{
		extra = (int)(int64_t)spec.get("object", (int64_t)0);
	}
	m_game->commands().append(CheatMode::makeMessage(local->getPlayerIndex(), (CheatMode::Command)command, target, value, extra, &at));
	Dictionary d;
	d["ok"] = true;
	return d;
}

Array GameWorld::take_cheat_lines()
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	for (const CheatMode::LogEntry &e : m_game->takeCheatAnnouncements())
	{
		Dictionary d;
		d["frame"] = (int64_t)e.frame;
		d["text"] = g(e.text);
		d["executed"] = e.executed;
		d["command"] = String(CheatMode::commandName(e.command));
		// the unit a spawn or a level up named, so the line can show its game text (DisplayName) instead of the template's name
		const ThingTemplate *tt = nullptr;
		if (e.command == (int)CheatMode::Command::Spawn && e.extra > 0 && e.extra <= 0xFFFF)
		{
			tt = m_game->logic().things().findByTemplateID((unsigned short)e.extra);
		}
		else if (e.command == (int)CheatMode::Command::LevelUp)
		{
			const ::Object *o = m_game->logic().findObjectByID((::ObjectID)(std::uint32_t)e.extra);
			tt = o ? o->getTemplate() : nullptr;
		}
		if (tt)
		{
			tt = tt->getFinalOverride();
			d["template"] = g(tt->getName());
			d["label"] = g(CommandButtonHelp::templateLabel(*tt, "DisplayName"));
		}
		out.push_back(d);
	}
	return out;
}
