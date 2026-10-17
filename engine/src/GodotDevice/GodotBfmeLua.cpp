// OpenBFME. GPL-3.0. The bfme companion's adapter to live engine objects.
#include "Common/BuildAssistant.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Construction.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotInGameHud.h"
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
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
	if (auto hud = Object::cast_to<InGameHudNode>(ObjectDB::get_instance(ObjectID(m_luaHudSource))))
	{
		Coord3D point;
		if (hud->cursor_map_position(this, point))
			out.cursorPosition = Table{{"x", double(point.x)}, {"y", double(point.y)}, {"z", double(point.z)}};
		out.selection = hud->selected_ids(this);
	}
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
	bfme_snapshot_ai(out);
	return out;
}
unsigned GameWorld::bfme_construct(const BfmeLuaRuntime::Mutation &m)
{
	if (!m_game || !m_world) throw std::runtime_error("no active match");
	auto &logic = m_game->logic();
	if (m.args.size() != 6 && m.args.size() != 7) throw std::runtime_error("invalid construction arguments");
	auto name = std::get_if<std::string>(&m.value.data);
	auto tt = name ? m_world->things().findTemplate(*name) : nullptr;
	if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 7))
		throw std::runtime_error("unknown building template");
	tt = tt->getFinalOverride();
	Coord3D pos{float(numeric(m.args[0], -1e7, 1e7, false)), float(numeric(m.args[1], -1e7, 1e7, false)), 0};
	int builderId = int(numeric(m.args[2], 0, 2147483647));
	int playerId = int(numeric(m.args[3], 0, 19));
	float speed = float(numeric(m.args[4], 0.01, 100, false));
	float angle = float(numeric(m.args[5], -1e6, 1e6, false));
	auto player = logic.players().getNthPlayer(playerId);
	if (!player || !player->getDefaultTeam()) throw std::runtime_error("invalid construction player");
	::Object *builder = builderId ? logic.findObjectByID(builderId) : nullptr;
	DozerAIUpdate *dozer = builder ? dynamic_cast<DozerAIUpdate *>(builder->getAIUpdateInterface()) : nullptr;
	if (builderId && (!builder || builder->isDestroyed() || builder->isEffectivelyDead() ||
		builder->getControllingPlayer() != player || !dozer))
		throw std::runtime_error("builder must be a living builder owned by the player");
	const int cost = BuildAssistant::calcCostToBuild(*tt, player, builder, -1);
	if (cost < 0 || player->getMoney()->countMoney() < (unsigned)cost)
		throw std::runtime_error("not enough money for construction");
	const int plotId = m.args.size() == 7 ? int(numeric(m.args[6], 0, 2147483647)) : 0;
	if (plotId)
	{
		auto plot = logic.findObjectByID(plotId);
		if (builderId || !plot || plot->isDestroyed() || plot->isEffectivelyDead() || plot->getControllingPlayer() != player ||
			!(plot->isKindOfName("BASE_FOUNDATION") || plot->isKindOfName("BASE_DEFENSE_FOUNDATION") || plot->isKindOfName("EXPANSION_PAD")))
			throw std::runtime_error("plot must be a living foundation owned by the player");
		auto obj = Construction::constructOnPlot(*plot, *tt, *plot->getPosition(), angle, *player, false);
		if (!obj) throw std::runtime_error("plot construction refused (occupied or unavailable)");
		obj->setScriptConstructionSpeed(speed);
		return obj->getID();
	}
	// Wall spans deliberately join/overlap their neighboring pieces. A layout
	// restores the final segment/tower/gate templates directly, without deleting
	// neighbors a second time through ReplaceSelfUpgrade.
	auto wall = [](const ObjectTemplateInfo &info) {
		for (const char *k : {"WALL_SEGMENT", "WALL_HUB", "WALL_UPGRADE", "WALL_GATE", "DEFENSIVE_WALL"})
		{
			int bit = ObjectTemplateInfoBuilder::kindOfIndex(k);
			if (bit >= 0 && kind(info, bit)) return true;
		}
		return false;
	};
	const bool joiningWall = !builderId && wall(logic.templateInfo(tt));
	if (joiningWall)
	{
		std::vector<BuildPlacement::Footprint> mine, other;
		BuildPlacement::footprintsOf(logic, *tt, pos, angle, mine);
		for (auto o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (ConstructionSite::isNeutralDecoration(logic, *o)) continue;
			if (o->isDestroyed() || !o->isInWorld() || !o->getTemplate() || !o->isKindOfName("IMMOBILE") ||
				o->isKindOfName("INERT") || o->isKindOfName("CLEARED_BY_BUILD") || o->isKindOfName("MINE")) continue;
			if (o->getControllingPlayer() == player && wall(logic.templateInfo(o->getTemplate()))) continue;
			BuildPlacement::footprintsOf(logic, *o->getTemplate(), *o->getPosition(), o->getOrientation(), other);
			for (const auto &a : mine) for (const auto &b : other)
				if (BuildPlacement::footprintsCollide(a, b)) throw std::runtime_error("wall site overlaps another structure");
		}
	}
	// Scripted construction may select any structure template, but placement and
	// builder pathfinding use the same gates as normal construction.
	if (BuildPlacement::isLocationLegalToBuild(logic, pos, *tt, angle,
		LLF_TERRAIN_RESTRICTIONS | (joiningWall ? 0 : LLF_NO_OBJECT_OVERLAP), builder, player) != LBC_OK)
		throw std::runtime_error("illegal construction site");
	auto obj = dozer ? dozer->construct(*tt, pos, angle, *player)
		: Construction::constructWithoutBuilder(logic, *tt, pos, angle, *player);
	if (!obj) throw std::runtime_error("construction refused (site or builder unavailable)");
	obj->setScriptConstructionSpeed(speed);
	return obj->getID();
}
std::string GameWorld::bfme_construction_status(unsigned id)
{
	if (!m_game) return "match ended";
	auto obj = m_game->logic().findObjectByID(id);
	if (!obj || obj->isDestroyed() || obj->isEffectivelyDead()) return "construction cancelled or building destroyed";
	return obj->isUnderConstruction() ? "building" : "complete";
}

std::string GameWorld::bfme_apply(const BfmeLuaRuntime::Mutation &m)
{
	if (m.collection == "graphics" && m.field == ":setTextureOverride")
	{
		Ref<Image> image;
		if (!std::holds_alternative<std::monostate>(m.value.data))
		{
			const auto path = std::get_if<std::string>(&m.value.data);
			if (!path || (path->rfind("res://", 0) != 0 && path->rfind("user://", 0) != 0))
				return "texture overrides require a res:// or user:// path";
			const String filename = String::utf8(path->c_str());
			if (!FileAccess::file_exists(filename))
				return "texture override file not found: " + *path;
			const auto bytes = FileAccess::get_file_as_bytes(filename);
			image.instantiate();
			const auto extension = filename.get_extension().to_lower();
			Error error = extension == "dds"   ? image->load_dds_from_buffer(bytes)
						  : extension == "tga" ? image->load_tga_from_buffer(bytes)
						  : extension == "png" ? image->load_png_from_buffer(bytes)
											   : ERR_FILE_UNRECOGNIZED;
			if (error != OK || image->is_empty())
				return "could not decode texture override: " + *path;
		}
		W3DMaterialFactory::Set_Texture_Override(m.id, image);
		return "";
	}
	if (m.collection == "templates" || m.collection == "unitTypes" || m.collection == "buildingTypes")
	{
		if (!m_world) return "definitions are not loaded";
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
	if (!m_game || !m_world)
		return "no active match";
	auto &logic = m_game->logic();
	if (m.collection == "orders")
		return bfme_order(m);
	if (m.collection == "match")
	{
		if (m.field == ":planningMarker")
		{
			const auto value=std::get_if<std::string>(&m.value.data);if(!value)return "expected marker name";const std::string name=*value;
			if(name!="GondorBuildingFoundation_Independant" && name!="GondorBuildingFoundation" && name!="MenWallHubSmall") return "unsupported planning marker";
			if(m.args.size()!=4) return "invalid marker arguments";
			const auto status=bfme_snapshot();if(status.networkGame || status.replay) return "planning markers require a local match";
			const int player=int(numeric(m.args[2],0,19));
			return create_object(String(name.c_str()),player,numeric(m.args[0],-1e7,1e7,false),numeric(m.args[1],-1e7,1e7,false),numeric(m.args[3],-1e6,1e6,false))>=0 ? "" : "marker creation failed";
		}
		if (m.field == ":clearNeutralDecorations")
		{
			auto enabled = std::get_if<bool>(&m.value.data);
			if (!enabled) return "expected enabled boolean";
			logic.settings().clearNeutralDecorations = *enabled;
			return {};
		}
		if (m.field == ":setGameSpeed")
		{
			set_time_scale(numeric(m.value, 0, 100, false));
			return {};
		}
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
		if (m.field == ":setAIEnabled")
		{
			const bool *enabled = std::get_if<bool>(&m.value.data);
			if (!enabled)
				return "expected boolean";
			return bfme_set_ai(p->getPlayerIndex(), *enabled);
		}
		if (m.field == "revealMap" || m.field == "fogOfWar")
		{
			const bool *value = std::get_if<bool>(&m.value.data);
			if (!value || !logic.shroud()) return "map visibility is unavailable";
			if (m.field == "fogOfWar") logic.shroud()->setCheatFogDisabled(p->getPlayerIndex(), !*value);
			else if (*value) logic.shroud()->revealMapForPlayer(p->getPlayerIndex());
			return {};
		}
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
	if (m.collection == "unitInstances" || m.collection == "buildingInstances")
	{
		auto obj = logic.findObjectByID(numberId(m.id));
		if (!obj || obj->isDestroyed())
			return "expired object";
		if (m.field == ":giveUpgrade")
		{
			auto name = std::get_if<std::string>(&m.value.data);
			auto u = name && TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(*name) : nullptr;
			if (!u || u->getUpgradeType() != UPGRADE_TYPE_OBJECT) return "expected an object upgrade";
			obj->giveUpgrade(*name);
			return {};
		}
		if (m.field == ":linkWallPiece")
		{
			auto other = logic.findObjectByID(unsigned(numeric(m.value, 1, 2147483647)));
			auto gb = dynamic_cast<GettingBuiltBehavior *>(obj->findModule("GettingBuiltBehavior"));
			if (!gb || !other || other->isDestroyed() || other->getControllingPlayer() != obj->getControllingPlayer()) return "invalid linked wall piece";
			gb->addLinkedPiece(*other);
			return {};
		}
		if (m.field == ":trainUnit")
		{
			if (m.collection != "buildingInstances" || !obj->isKindOfName("STRUCTURE") || obj->isEffectivelyDead())
				return "producer must be a living building";
			auto production = obj->getProductionUpdate();
			if (!production || obj->isUnderConstruction()) return "building is not ready for production";
			auto name = std::get_if<std::string>(&m.value.data);
			auto tt = name ? m_world->things().findTemplate(*name) : nullptr;
			if (!tt || !kind(logic.templateInfo(tt->getFinalOverride()), 1) || kind(logic.templateInfo(tt->getFinalOverride()), 7))
				return "unknown unit template";
			tt = tt->getFinalOverride();
			if (m.args.size() != 1) return "expected production count";
			const int count = int(numeric(m.args[0], 1, 1000));
			for (int queued = 0; queued < count; ++queued)
			{
				// Same eligibility, unique IDs and queue path as MSG_QUEUE_UNIT_CREATE
				// (GameLogicDispatch::queueUnitCreate). One entry per requested recruitment,
				// so money/queue limits are rechecked rather than using the five-unit batch flag.
				const auto allowed = BuildAssistant::canMakeUnit(*obj, tt, -1);
				std::string reason;
				switch (allowed)
				{
				case CANMAKE_OK: break;
				case CANMAKE_NO_MONEY: reason = "not enough gold"; break;
				case CANMAKE_NO_PREREQUISITES: reason = "unit unavailable at this building or prerequisites missing"; break;
				case CANMAKE_FACTORY_IS_DISABLED: reason = "building disabled"; break;
				case CANMAKE_QUEUE_FULL: reason = "production queue full"; break;
				case CANMAKE_PARKING_PLACES_FULL: reason = "no room for units"; break;
				case CANMAKE_MAXED_OUT_FOR_PLAYER: reason = "player unit limit reached"; break;
				default: reason = "production unavailable (code " + std::to_string(allowed) + ")"; break;
				}
				if (reason.empty() && !production->queueCreateUnit(tt, -1, production->requestUniqueUnitID(), -1, false, {}, false))
					reason = "native production queue refused request";
				if (!reason.empty()) return "queued " + std::to_string(queued) + " of " + std::to_string(count) + ": " + reason;
			}
			return {};
		}
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
