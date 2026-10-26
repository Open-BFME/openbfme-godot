// OpenBFME. GPL-3.0.
// See GameLogic/CheatMode.h. Lane CHEAT-1.

#include "GameLogic/CheatMode.h"

#include "Common/CommandPoints.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/VictoryConditions.h"

#include <exception>
#include <utility>

namespace CheatMode
{
namespace
{
Player *playerAt(GameLogic &logic, int index)
{
	Player *p = logic.players().getNthPlayer(index);
	return p && p->getPlayerIndex() == index ? p : nullptr;
}


} // namespace

// a player of the game: a playable faction's player (its template has a StartingBuilding, as Player::hasTeamColor counts one), not the neutral player, not
// an observer. The map's own sides (civilians, creeps) are not: no name is compared, so a mod's factions count the same way
bool isGamePlayer(const GameLogic &logic, int index)
{
	const Player *p = logic.players().getNthPlayer(index);
	return p && p->getPlayerIndex() == index && p != logic.players().getNeutralPlayer() && !p->isObserver() && p->getPlayerTemplate() &&
		p->getPlayerTemplate()->hasStartingBuilding();
}

namespace
{
std::string nameOf(GameLogic &logic, int index)
{
	const Player *p = playerAt(logic, index);
	if (!p)
	{
		return "player " + std::to_string(index);
	}
	return p->getPlayerDisplayName().empty() ? p->getPlayerName() : p->getPlayerDisplayName();
}

bool allies(const Player *a, const Player *b)
{
	return a == b || (a->getRelationship(b) == ALLIES && b->getRelationship(a) == ALLIES);
}

const GameMessageArgument *intArg(const GameMessage &m, std::size_t i)
{
	const GameMessageArgument *a = m.getArgument(i);
	return a && a->type == ARGUMENTDATATYPE_INTEGER ? a : nullptr;
}

// the effect of one checked command: true when it changed the game; `text` is what it did, or why not
bool runCommand(GameLogic &logic, const GameMessage &msg, Command command, int target, int value, int extra, std::string &text)
{
	State &state = logic.cheatMode();
	Player *p = playerAt(logic, target);
	const bool needsPlayer = command != Command::VoidGame;
	if (needsPlayer && (!p || !isGamePlayer(logic, target)))
	{
		text = "there is no player " + std::to_string(target) + " in the game";
		return false;
	}
	const std::string who = needsPlayer ? nameOf(logic, target) : std::string();
	auto range = [&](int lo, int hi, const char *what) {
		if (value < lo || value > hi)
		{
			text = std::string(what) + " must be " + std::to_string(lo) + " .. " + std::to_string(hi);
			return false;
		}
		return true;
	};
	switch (command)
	{
	case Command::AddResources:
		if (!range(1, kMaxResources, "resources"))
		{
			return false;
		}
		p->getMoney()->deposit((unsigned)value, nullptr, false); // ZH CommandXlat.cpp:3370 (deposit 10000); RW 0x7B18B8; no score, no sound
		text = "gave " + std::to_string(value) + " resources to " + who;
		return true;
	case Command::AddCommandPoints:
		if (!range(1, kMaxCommandPoints, "command points"))
		{
			return false;
		}
		p->commandPoints().addBonus(value); // RW 0x6A7C3E: the bonus counts in the limit (CommandPoints::getLimit)
		text = "gave " + std::to_string(value) + " command points to " + who;
		return true;
	case Command::AddPowerPoints:
		if (!range(1, kMaxPowerPoints, "power points"))
		{
			return false;
		}
		p->science().addSciencePurchasePoints(value); // ZH CommandXlat.cpp:3403; RW 0x782764
		text = "gave " + std::to_string(value) + " power points to " + who;
		return true;
	case Command::Spawn:
	{
		if (!range(1, kMaxSpawn, "the number of units"))
		{
			return false;
		}
		const GameMessageArgument *where = msg.getArgument(4);
		if (!where || where->type != ARGUMENTDATATYPE_LOCATION || !p->getDefaultTeam())
		{
			text = !p->getDefaultTeam() ? who + " has no team" : "a spawn needs a location";
			return false;
		}
		const ThingTemplate *tt = extra >= 0 && extra <= 0xFFFF ? logic.things().findByTemplateID((unsigned short)extra) : nullptr;
		if (tt)
		{
			tt = tt->getFinalOverride(); // as MSG_QUEUE_UNIT_CREATE finds its template (RW 0x77A764)
		}
		if (!tt)
		{
			text = "there is no unit with the template id " + std::to_string(extra);
			return false;
		}
		const std::string &name = tt->getName();
		if (logic.economy().templateKindOf(*tt, "STRUCTURE") || logic.economy().templateKindOf(*tt, "PROJECTILE"))
		{
			text = name + " is a structure or a projectile, not a unit";
			return false;
		}
		if (logic.economy().templateKindOf(*tt, "CREATE_A_HERO"))
		{
			text = name + " is a Create-a-Hero: it is made from its player's own record, never on its own";
			return false;
		}
		// rows of 4 at 15 world units (SimMath: the simulation's float operations); every point must be on the map
		std::vector<Coord3D> points;
		for (int i = 0; i < value; ++i)
		{
			Coord3D pos;
			pos.x = SimMath::addf32(where->location.x, SimMath::mulf32(SimMath::sseFromInt32(i % 4), 15.0f));
			pos.y = SimMath::addf32(where->location.y, SimMath::mulf32(SimMath::sseFromInt32(i / 4), 15.0f));
			pos.z = 0.0f;
			points.push_back(pos);
		}
		const TerrainLogic *t = logic.terrain();
		if (!t)
		{
			text = "the game has no map";
			return false;
		}
		float lx, ly, hx, hy;
		t->getMapExtentIncludingBorder(lx, ly, hx, hy);
		for (const Coord3D &pos : points)
		{
			// NaN fails every comparison: !(lo <= v && v <= hi) refuses it too
			if (!(pos.x >= lx && pos.x <= hx && pos.y >= ly && pos.y <= hy))
			{
				text = "a spawn point is off the map";
				return false;
			}
		}
		int made = 0;
		for (Coord3D pos : points)
		{
			// the first free spot around the point (TerrainLogic::FindPositionAround RW 0x68592D, from angle 0: no random number), so a unit spawned at
			// a building's centre stands beside it; none within 200 units: the point itself
			FindPositionOptions around;
			around.minRadius = 0.0f;
			around.maxRadius = 200.0f;
			around.startAngle = 0.0f;
			Coord3D free;
			if (logic.aiWorld() && FindPosition::findPositionAround(logic, pos, around, free))
			{
				pos = free;
			}
			pos.z = logic.getGroundHeight(pos.x, pos.y);
			Object *o = logic.newObject(tt, p->getDefaultTeam(), ObjectStatusMaskType{});
			if (!o)
			{
				continue;
			}
			// RW 0x629DD9 placeObjectAtPosition's order (GameLogic/NewGame/StartingBase.cpp): orientation, position, the pathfinder footprint, onBuildComplete
			o->setOrientation(0.0f);
			o->setPosition(&pos);
			if (AIWorld *ai = logic.aiWorld())
			{
				ai->addObjectToPathfindMap(*o);
			}
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				if (CreateModuleInterface *create = m->getCreate())
				{
					create->onBuildComplete();
				}
			}
			++made;
		}
		text = "spawned " + std::to_string(made) + " " + name + " for " + who;
		return made > 0;
	}
	case Command::LevelUp:
	{
		if (!range(1, kMaxLevels, "levels"))
		{
			return false;
		}
		Object *o = logic.findObjectByID((ObjectID)(std::uint32_t)extra);
		if (!o || o->isDestroyed() || o->isEffectivelyDead() || o->getControllingPlayer() != p)
		{
			text = "object " + std::to_string((std::uint32_t)extra) + " is not a living unit of " + who;
			return false;
		}
		ExperienceTracker *xp = o->getExperienceTracker();
		if (!xp || !xp->isTrainable())
		{
			text = o->getTemplate()->getName() + " cannot gain levels";
			return false;
		}
		xp->gainLevels(value, true); // ZH CommandXlat.cpp:4212; RW 0x79D7E5
		text = "gave " + std::to_string(value) + (value == 1 ? " level to " : " levels to ") + who + "'s " + o->getTemplate()->getName();
		return true;
	}
	case Command::RevealMap:
		if (!range(0, 1, "the reveal value"))
		{
			return false;
		}
		if (!logic.shroud())
		{
			text = "the game has no shroud";
			return false;
		}
		if (state.revealed(target) == (value == 1))
		{
			text = std::string("the map is ") + (value ? "revealed" : "not revealed") + " for " + who + " already";
			return false;
		}
		if (value == 1)
		{
			logic.shroud()->revealMapForPlayerPermanently(target); // ZH LookAtXlat.cpp:531; RW 0xB4F5D0
		}
		else
		{
			logic.shroud()->undoRevealMapForPlayerPermanently(target); // RW 0xB519B0
		}
		state.setRevealed(target, value == 1);
		text = std::string(value ? "revealed the map for " : "hid the map again for ") + who;
		return true;
	case Command::EndGame:
	{
		int killed = 0;
		VictoryConditions &v = logic.victory();
		for (int i = 0; i < logic.players().getPlayerCount(); ++i)
		{
			Player *other = isGamePlayer(logic, i) ? playerAt(logic, i) : nullptr;
			if (other && !other->isDefeated() && !allies(other, p))
			{
				v.killPlayer(other); // RW 0x6ABE7C
				++killed;
			}
		}
		if (killed == 0)
		{
			text = who + "'s alliance is the only one left already";
			return false;
		}
		text = "ended the game: " + who + "'s alliance wins";
		return true;
	}
	case Command::VoidGame:
	{
		if (state.voided())
		{
			text = "the game is void already";
			return false;
		}
		state.setVoided();
		VictoryConditions &v = logic.victory();
		for (int i = 0; i < logic.players().getPlayerCount(); ++i)
		{
			Player *other = isGamePlayer(logic, i) ? playerAt(logic, i) : nullptr;
			if (other && !other->isDefeated())
			{
				v.killPlayer(other);
			}
		}
		text = "declared the game void: no winner";
		return true;
	}
	}
	text = "unknown command " + std::to_string((int)command);
	return false;
}
} // namespace

void State::record(LogEntry e)
{
	(e.executed ? m_executed : m_refused) += 1;
	if (m_log.size() >= kLogCap)
	{
		m_log.erase(m_log.begin());
	}
	m_log.push_back(e);
	if (m_announce.size() < kLogCap)
	{
		m_announce.push_back(std::move(e));
	}
}

std::vector<LogEntry> State::takeAnnouncements()
{
	std::vector<LogEntry> out;
	out.swap(m_announce);
	return out;
}

void State::crc(StateHasher &h) const
{
	if (!enabled())
	{
		return;
	}
	h.addI32(m_slot);
	h.addI32(m_player);
	h.addU32(m_revealedMask);
	h.addBool(m_voided);
	h.addU32(m_executed);
	h.addU32(m_refused);
}

void State::xfer(Xfer &x)
{
	x.xferInt(&m_slot);
	x.xferInt(&m_player);
	x.xferUnsignedInt(&m_revealedMask);
	x.xferBool(&m_voided);
	x.xferUnsignedInt(&m_executed);
	x.xferUnsignedInt(&m_refused);
	if (x.isLoading() && (m_slot < -1 || m_slot > 15 || m_player < -1 || m_player > 31 || (m_slot < 0) != (m_player < 0)))
	{
		throw XferException("the game's cheat seat (slot " + std::to_string(m_slot) + ", player " + std::to_string(m_player) + ")");
	}
}

void registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_OPENBFME_CHEAT, "CHEAT-1", [](GameLogic &l, const GameMessage &m) { return execute(l, m); });
}

bool execute(GameLogic &logic, const GameMessage &msg)
{
	State &state = logic.cheatMode();
	LogEntry entry;
	entry.frame = logic.getFrame();
	entry.player = msg.getPlayerIndex();
	const GameMessageArgument *a0 = intArg(msg, 0), *a1 = intArg(msg, 1), *a2 = intArg(msg, 2), *a3 = intArg(msg, 3);
	if (a0 && a1 && a2)
	{
		entry.command = a0->integer;
		entry.target = a1->integer;
		entry.value = a2->integer;
		entry.extra = a3 ? a3->integer : 0;
	}
	const bool needsExtra = entry.command == (int)Command::Spawn || entry.command == (int)Command::LevelUp;
	std::string why;
	// the checks, in this order on every peer
	if (!state.enabled())
	{
		why = "refused: this is not a Cheat Mode game";
	}
	else if (msg.getPlayerIndex() != state.seatPlayer())
	{
		why = "refused: " + nameOf(logic, msg.getPlayerIndex()) + " is not the host";
	}
	else if (!a0 || !a1 || !a2 || (needsExtra && !a3))
	{
		why = "refused: a malformed command";
	}
	else if (entry.command < kFirstCommand || entry.command > kLastCommand)
	{
		why = "refused: unknown command " + std::to_string(entry.command);
	}
	if (!why.empty())
	{
		entry.text = "Cheat Mode: " + why;
		state.record(std::move(entry));
		return true; // logged; never the dispatcher's unhandled count
	}
	std::string text;
	try
	{
		entry.executed = runCommand(logic, msg, (Command)entry.command, entry.target, entry.value, entry.extra, text);
	}
	catch (const std::exception &x)
	{
		// the host may name any unit: a port stop (std::logic_error) it reaches ends the command, not the game; every peer throws at the same point of
		// the same command, so what ran before the throw is the same everywhere
		entry.executed = false;
		text = std::string("failed: ") + x.what();
	}
	entry.text = "Cheat Mode: " + nameOf(logic, msg.getPlayerIndex()) + (entry.executed ? " " + text : " (not done): " + text);
	state.record(std::move(entry));
	return true;
}

GameMessage makeMessage(int player, Command command, int target, int value, int extra, const Coord3D *location)
{
	GameMessage m(MSG_OPENBFME_CHEAT, player);
	m.appendIntegerArgument((int)command);
	m.appendIntegerArgument(target);
	m.appendIntegerArgument(value);
	if (command == Command::Spawn || command == Command::LevelUp)
	{
		m.appendIntegerArgument(extra);
	}
	if (command == Command::Spawn && location)
	{
		m.appendLocationArgument(*location);
	}
	return m;
}

namespace
{
const char *const kNames[] = { "add_resources", "add_command_points", "add_power_points", "spawn", "level_up", "reveal_map", "end_game", "void_game" };
}

const char *commandName(int command)
{
	return command >= kFirstCommand && command <= kLastCommand ? kNames[command - kFirstCommand] : "unknown";
}

int commandFromName(const std::string &name)
{
	for (int c = kFirstCommand; c <= kLastCommand; ++c)
	{
		if (name == kNames[c - kFirstCommand])
		{
			return c;
		}
	}
	return 0;
}
} // namespace CheatMode
