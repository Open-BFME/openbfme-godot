// OpenBFME. GPL-3.0. The bfme companion's adapter to live engine objects.
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GodotDevice/GodotGameWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
using Value = BfmeLuaValue;
using Table = Value::Table;
Value field(const ThingTemplate &tt, const char *name)
{
	const auto v = tt.findField(name);
	if (!v)
		return {};
	if (auto n = std::get_if<long long>(v))
		return double(*n);
	if (auto n = std::get_if<float>(v))
		return double(*n);
	if (auto s = std::get_if<std::string>(v))
		return *s;
	if (auto b = std::get_if<bool>(v))
		return *b;
	return {};
}
Table definition(const ThingTemplate &tt)
{
	return {{"id", tt.getName()},
			{"name", tt.getName()},
			{"displayName", field(tt, "DisplayName")},
			{"faction", field(tt, "Side")},
			{"side", field(tt, "Side")},
			{"cost", int(BuildAssistant::buildCost(tt))},
			{"refund", field(tt, "RefundValue")},
			{"buildTime", field(tt, "BuildTime")},
			{"trainTime", field(tt, "BuildTime")},
			{"visionRange", field(tt, "VisionRange")},
			{"shroudClearingRange", field(tt, "ShroudClearingRange")}};
}
bool kind(const ObjectTemplateInfo &info, int bit)
{
	return (info.kindOf[bit / 32] & (1u << (bit % 32))) != 0;
}
int numberId(const std::string &id)
{
	size_t end = 0;
	long long n = std::stoll(id, &end);
	if (end != id.size() || n < 0 || n > std::numeric_limits<int>::max())
		throw std::runtime_error("invalid engine ID");
	return int(n);
}
double numeric(const Value &v, double lo, double hi, bool integer = true)
{
	auto n = std::get_if<double>(&v.data);
	if (!n || !std::isfinite(*n) || *n < lo || *n > hi || (integer && double(SimMath::ftol2(*n)) != *n))
		throw std::runtime_error("value out of range");
	return *n;
}
} // namespace
namespace godot
{
BfmeLuaRuntime::Snapshot GameWorld::bfme_snapshot()
{
	BfmeLuaRuntime::Snapshot out;
	out.paused = m_paused;
	if (!m_world)
		return out;
	for (auto master : m_world->things().templates())
	{
		const auto tt = master->getFinalOverride();
		auto info = ObjectTemplateInfoBuilder::build(*tt);
		auto r = definition(*tt);
		out.collections["templates"][tt->getName()] = r;
		if (kind(info, 7))
			out.collections["buildingTypes"][tt->getName()] = r;
		else if (kind(info, 1))
			out.collections["unitTypes"][tt->getName()] = r;
	}
	if (!m_game)
		return out;
	out.ready = true;
	auto &logic = m_game->logic();
	out.frame = logic.getFrame();
	auto &players = logic.players();
	if (auto p = players.getLocalPlayer())
		out.localPlayerId = p->getPlayerIndex();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		auto p = players.getNthPlayer(i);
		if (!p)
			continue;
		const auto &cp = p->commandPoints();
		const int limit = p->commandPointLimit();
		Table r{{"id", i},
				{"name", p->getPlayerName()},
				{"displayName", p->getPlayerDisplayName()},
				{"username", p->getPlayerDisplayName()},
				{"money", double(p->getMoney()->countMoney())},
				{"gold", double(p->getMoney()->countMoney())},
				{"commandPoints", limit},
				{"commandPointsBase", cp.getBase()},
				{"commandPointsBonus", cp.getBonus()},
				{"commandPointsCap", cp.getCap()},
				{"commandPointsUsed", cp.getUsage()},
				{"commandPointsRemaining", cp.getAvailable(limit)},
				{"powerPoints", p->science().getSciencePurchasePoints()},
				{"powerpoint", p->science().getSciencePurchasePoints()},
				{"isAI", p->isSkirmishAI()},
				{"faction", p->getSide()}};
		out.collections["players"][std::to_string(i)] = std::move(r);
	}
	for (auto obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		const auto tt = obj->getTemplate();
		if (!tt)
			continue;
		const auto &info = logic.templateInfo(tt);
		const bool building = kind(info, 7);
		if (!building && !kind(info, 1))
			continue;
		Table r = definition(*tt);
		const auto pos = obj->getPosition();
		r["id"] = double(obj->getID());
		r["objectId"] = double(obj->getID());
		r["typeId"] = tt->getName();
		r["position"] = Table{{"x", double(pos->x)}, {"y", double(pos->y)}, {"z", double(pos->z)}};
		r["x"] = double(pos->x);
		r["y"] = double(pos->y);
		r["z"] = double(pos->z);
		r["rotation"] = double(obj->getOrientation());
		if (auto p = obj->getControllingPlayer())
		{
			r["playerId"] = p->getPlayerIndex();
			r["player"] = p->getPlayerIndex();
		}
		if (auto body = obj->getBodyModule())
		{
			r["health"] = double(body->getHealth());
			r["maxHealth"] = double(body->getMaxHealth());
		}
		if (auto xp = obj->getExperienceTracker())
			r["level"] = xp->getRank();
		if (building)
		{
			r["underConstruction"] = obj->isUnderConstruction();
			r["constructionPercent"] = double(obj->getConstructionPercent());
			r["builderId"] = double(obj->getBuilderID());
		}
		out.collections[building ? "buildingInstances" : "unitInstances"][std::to_string(obj->getID())] = std::move(r);
	}
	return out;
}
std::string GameWorld::bfme_apply(const BfmeLuaRuntime::Mutation &m)
{
	if (!m_game || !m_world)
		return "no active match";
	auto &logic = m_game->logic();
	if (m.collection == "match")
	{
		if (m.field == ":pause")
		{
			m_paused = true;
			return {};
		}
		if (m.field == ":unpause")
		{
			m_paused = false;
			return {};
		}
		return "unsupported match action";
	}
	if (m.collection == "players")
	{
		auto p = logic.players().getNthPlayer(numberId(m.id));
		if (!p)
			return "expired player";
		if (m.field == "money" || m.field == "gold")
		{
			auto amount = std::uint32_t(SimMath::ftol2(numeric(m.value, 0, 2147483647)));
			auto money = p->getMoney();
			// Use native delta bookkeeping, rather than resetting the money object or its income statistics.
			auto old = money->countMoney();
			if (amount > old)
				money->deposit(amount - old, nullptr, true);
			else if (amount < old)
				money->withdraw(old - amount, nullptr, true);
			return {};
		}
		if (m.field == "powerPoints" || m.field == "powerpoint")
		{
			int amount = int(SimMath::ftol2(numeric(m.value, 0, 1000000)));
			p->science().addSciencePurchasePoints(amount - p->science().getSciencePurchasePoints());
			return {};
		}
		auto &cp = p->commandPoints();
		int amount = int(SimMath::ftol2(numeric(m.value, m.field == "commandPointsBonus" ? -1000000 : 0, 1000000)));
		if (m.field == "commandPoints")
		{
			int extra = cp.getBonus();
			for (const auto &r : cp.records())
				if (!r.filter || p->hasObjectMatching(*r.filter, true))
					extra += r.value;
			cp.setFromScript(amount - extra, amount);
		}
		else if (m.field == "commandPointsBase")
			cp.setFromScript(amount, cp.getCap());
		else if (m.field == "commandPointsCap")
			cp.setFromScript(cp.getBase(), amount);
		else if (m.field == "commandPointsBonus")
			cp.addBonus(amount - cp.getBonus());
		else if (m.field == "commandPointsUsed")
			cp.addUsage(amount - cp.getUsage());
		else
			return "unsupported player field";
		return {};
	}
	if (m.collection == "templates" || m.collection == "unitTypes" || m.collection == "buildingTypes")
	{
		auto base = m_world->things().findTemplate(m.id);
		if (!base)
			return "expired template";
		auto tt = const_cast<ThingTemplate *>(base->getFinalOverride());
		static const std::map<std::string, std::string> fields{
			{"cost", "BuildCost"},		{"refund", "RefundValue"},		{"buildTime", "BuildTime"},
			{"trainTime", "BuildTime"}, {"visionRange", "VisionRange"}, {"shroudClearingRange", "ShroudClearingRange"}};
		auto f = fields.find(m.field);
		if (f == fields.end())
			return "unsupported template field";
		bool integer = m.field == "cost" || m.field == "refund";
		double n = numeric(m.value, 0, integer ? 65535 : 1e30, integer);
		FieldValue value = integer ? FieldValue((long long)SimMath::ftol2(n)) : FieldValue(SimMath::fstpDword(n));
		return tt->setRuntimeField(f->second, std::move(value)) ? "" : "template field is unavailable";
	}
	if (m.collection == "unitInstances" || m.collection == "buildingInstances")
	{
		auto obj = logic.findObjectByID(numberId(m.id));
		if (!obj || obj->isDestroyed())
			return "expired object";
		if (m.field == ":kill")
		{
			obj->kill(DEATH_NORMAL);
			return {};
		}
		if (m.field == ":destroy")
		{
			logic.destroyObject(obj);
			return {};
		}
		if (m.field == "ownerPlayer")
		{
			auto p = logic.players().getNthPlayer(int(SimMath::ftol2(numeric(m.value, 0, 19))));
			if (!p || !p->getDefaultTeam())
				return "destination player has no default team";
			obj->setTeam(p->getDefaultTeam());
			return {};
		}
		if (m.field == "level")
		{
			auto xp = obj->getExperienceTracker();
			if (!xp || !xp->isTrainable())
				return "object has no trainable experience tracker";
			int level = int(SimMath::ftol2(numeric(m.value, 1, 100)));
			if (level < xp->getRank())
				xp->setExperienceAndLevel(0, true);
			xp->gainLevels(level - xp->getRank(), true);
			return xp->getRank() == level ? "" : "level unavailable for this object's experience levels";
		}
		return "unsupported instance field or action";
	}
	return "unknown collection";
}
} // namespace godot
