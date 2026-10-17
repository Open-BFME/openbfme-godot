// OpenBFME. GPL-3.0. Lane LUAAI-1: what a bfme Lua script needs to play a player in place of the skirmish AI (docs/BFME_LUA.md "Lua AI").
//   * bfme_order: the player commands the AI and a human give (the lockstep command path, LiveGame::commands()): the player's selection becomes the
//     ordered objects, the order message follows, then the selection the player had is restored, so a human's later commands are unaffected. The
//     messages run in the next logic frame through the same handlers as a click (AICommands, GarrisonCommands, SpellCommands, UpgradeCommands,
//     BuildCommands, BuildAssistantSell, StancesBehavior, GameLogicDispatch). Refused in network games: they are local (not sent to peers).
//   * bfme_query: read-only answers for the script's decisions (what a building trains / researches, what a builder builds, a legal site, the
//     abilities and their readiness, the sciences, the AI state), read with the simulation held between frames.
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GodotDevice/GodotGameWorld.h"
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <cmath>
#include <limits>

namespace
{
using Value = BfmeLuaValue;
using Table = Value::Table;
using List = Value::List;

godot::Variant layoutVariant(const Value &v)
{
	if (auto p=std::get_if<bool>(&v.data)) return *p;
	if (auto p=std::get_if<double>(&v.data)) return *p;
	if (auto p=std::get_if<std::string>(&v.data)) return godot::String::utf8(p->c_str());
	if (auto p=std::get_if<Table>(&v.data)) { godot::Dictionary d; for (const auto &kv:*p) d[godot::String::utf8(kv.first.c_str())]=layoutVariant(kv.second); return d; }
	if (auto p=std::get_if<List>(&v.data)) { godot::Array a; for(const auto &item:*p) a.push_back(layoutVariant(item)); return a; }
	return {};
}
Value layoutValue(const godot::Variant &v, int depth=0)
{
	if (depth>8) throw std::runtime_error("layout JSON nests too deeply");
	switch(v.get_type()) {
	case godot::Variant::NIL: return {};
	case godot::Variant::BOOL: return bool(v);
	case godot::Variant::INT: case godot::Variant::FLOAT: return double(v);
	case godot::Variant::STRING: return std::string(godot::String(v).utf8().get_data());
	case godot::Variant::ARRAY: { List out; godot::Array a=v; for(int i=0;i<a.size();++i) out.push_back(layoutValue(a[i],depth+1)); return out; }
	case godot::Variant::DICTIONARY: { Table out; godot::Dictionary d=v; auto keys=d.keys(); for(int i=0;i<keys.size();++i) out[godot::String(keys[i]).utf8().get_data()]=layoutValue(d[keys[i]],depth+1); return out; }
	default: throw std::runtime_error("invalid layout JSON value");
	}
}

double number(const Value &v, const char *what, double lo, double hi)
{
	auto n = std::get_if<double>(&v.data);
	if (!n || !std::isfinite(*n) || *n < lo || *n > hi)
		throw std::runtime_error(std::string("invalid ") + what);
	return *n;
}
const Value &at(const std::vector<Value> &args, size_t i, const char *what)
{
	if (i >= args.size())
		throw std::runtime_error(std::string("missing ") + what);
	return args[i];
}
std::string text(const Value &v, const char *what)
{
	auto s = std::get_if<std::string>(&v.data);
	if (!s || s->empty())
		throw std::runtime_error(std::string("invalid ") + what);
	return *s;
}
ObjectID objectId(const Value &v, const char *what)
{
	return (ObjectID)SimMath::ftol2(number(v, what, 1, 2147483647));
}
Coord3D location(const Value &v, GameLogic &logic)
{
	auto t = std::get_if<Table>(&v.data);
	if (!t || !t->count("x") || !t->count("y"))
		throw std::runtime_error("expected a position {x=..., y=...}");
	Coord3D p;
	p.x = float(number(t->at("x"), "position x", -1e7, 1e7));
	p.y = float(number(t->at("y"), "position y", -1e7, 1e7));
	p.z = logic.getGroundHeight(p.x, p.y);
	return p;
}
std::vector<ObjectID> idList(const Value &v)
{
	auto list = std::get_if<List>(&v.data);
	if (!list || list->empty())
		throw std::runtime_error("expected a list of object ids");
	std::vector<ObjectID> out;
	for (const Value &id : *list)
		out.push_back(objectId(id, "object id"));
	return out;
}
const char *canMakeText(CanMakeType r)
{
	switch (r)
	{
	case CANMAKE_OK: return "ok";
	case CANMAKE_NO_MONEY: return "not enough gold";
	case CANMAKE_NO_PREREQUISITES: return "prerequisites missing";
	case CANMAKE_FACTORY_IS_DISABLED: return "building disabled";
	case CANMAKE_QUEUE_FULL: return "production queue full";
	case CANMAKE_PARKING_PLACES_FULL: return "no room for units";
	case CANMAKE_MAXED_OUT_FOR_PLAYER: return "player unit limit reached";
	default: return "unavailable";
	}
}
const char *relationName(Relationship r)
{
	return r == ALLIES ? "ally" : r == ENEMIES ? "enemy" : "neutral";
}
// the buttons of the object's current command set (the control bar's, ZH ControlBar: Object::getCommandSetName)
std::vector<const CommandButton *> commandButtons(const Object &obj)
{
	std::vector<const CommandButton *> out;
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj.getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
		if (const CommandButton *b = set->getCommandButton(i))
			out.push_back(b);
	return out;
}
Table abilityOf(const SpecialPowerModuleInterface &sp)
{
	const SpecialPowerTemplate *t = sp.getSpecialPowerTemplate();
	return {{"name", t ? t->getName() : std::string()}, {"ready", sp.isReady()}, {"percentReady", double(sp.getPercentReady())}};
}
// KindOf of a template by name (ObjectTemplateInfo, as the logic tests it)
bool templateKind(GameLogic &logic, const ThingTemplate *tt, const char *kind)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kind);
	const ObjectTemplateInfo &info = logic.templateInfo(tt);
	return bit >= 0 && (info.kindOf[bit / 32] & (1u << (bit % 32))) != 0;
}
// how far a template's placement footprints reach from its position (every active shape, BuildPlacement::footprintsOf; a box by its corner)
double footprintRadius(GameLogic &logic, const ThingTemplate &tt)
{
	std::vector<BuildPlacement::Footprint> fps;
	if (!BuildPlacement::footprintsOf(logic, tt, Coord3D{0.0f, 0.0f, 0.0f}, 0.0f, fps))
		return 0.0;
	double reach = 0.0;
	for (const BuildPlacement::Footprint &fp : fps)
	{
		const double shape = fp.type == 0 ? std::hypot(double(fp.major), double(fp.minor)) : double(fp.major);
		reach = std::max(reach, std::hypot(double(fp.x), double(fp.y)) + shape);
	}
	return reach;
}
List abilities(const Object &obj)
{
	List out;
	for (const auto &m : obj.modules())
		if (const SpecialPowerModuleInterface *sp = m->getSpecialPower())
			out.emplace_back(abilityOf(*sp));
	return out;
}
} // namespace

namespace godot
{
bool GameWorld::bfme_network_game() const
{
	return m_netHost || m_netClient || m_netStarted || m_netSession;
}

std::string GameWorld::bfme_order(const BfmeLuaRuntime::Mutation &m)
{
	if (bfme_network_game())
		return "orders are local and not available in network games";
	if (m_replayPlayback)
		return "orders are not available while a replay plays";
	auto &logic = m_game->logic();
	const int pi = (int)SimMath::ftol2(number(Value(std::stod(m.id)), "player", 0, 19));
	Player *player = logic.players().getNthPlayer(pi);
	if (!player)
		return "expired player";
	const std::string order = m.field.substr(1);
	auto &commands = m_game->commands();
	// player orders: no selection
	if (order == "purchaseScience")
	{
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(text(at(m.args, 0, "science"), "science")) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
			return "unknown science";
		if (player->science().hasScience(st))
			return "science already owned";
		if (!player->science().isCapableOfPurchasingScience(st))
			return "science unavailable (prerequisites or points)";
		GameMessage msg(MSG_PURCHASE_SCIENCE, pi);
		msg.appendIntegerArgument(pi);
		msg.appendIntegerArgument(st);
		commands.append(msg);
		return {};
	}
	if (order == "castPower")
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(text(at(m.args, 0, "power"), "power")) : nullptr;
		::Object *book = SpecialPowerModules::getSpellBookObject(logic, *player);
		if (!t)
			return "unknown power";
		if (!book)
			return "player has no spell book";
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, t);
		if (!sp)
			return "power is not in the player's spell book";
		if (!sp->isReady())
			return "power is not ready";
		const Value *target = m.args.size() > 1 ? &m.args[1] : nullptr;
		if (!target || std::holds_alternative<std::monostate>(target->data))
		{
			GameMessage msg(MSG_DO_SPELLBOOK_SPECIAL_POWER, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendIntegerArgument(0);
			commands.append(msg);
		}
		else if (std::holds_alternative<double>(target->data))
		{
			GameMessage msg(MSG_DO_SPECIAL_POWER_AT_OBJECT, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendObjectIDArgument(objectId(*target, "target"));
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(book->getID());
			commands.append(msg);
		}
		else
		{
			GameMessage msg(MSG_DO_SPECIAL_POWER_AT_LOCATION, pi);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendLocationArgument(location(*target, logic));
			msg.appendObjectIDArgument(INVALID_ID);
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(book->getID());
			commands.append(msg);
		}
		return {};
	}
	// group orders: every object alive and controlled by the ordering player
	const std::vector<::ObjectID> ids = idList(at(m.args, 0, "objects"));
	for (::ObjectID id : ids)
	{
		const ::Object *obj = logic.findObjectByID(id);
		if (!obj || obj->isDestroyed() || obj->isEffectivelyDead())
			return "expired object " + std::to_string(id);
		if (obj->getControllingPlayer() != player)
			return "object " + std::to_string(id) + " is not controlled by the ordering player";
	}
	if (order == "setRallyPoint")
	{
		const Coord3D dest = location(at(m.args, 1, "position"), logic);
		for (::ObjectID id : ids)
		{
			GameMessage msg(MSG_SET_RALLY_POINT, pi);
			msg.appendObjectIDArgument(id);
			msg.appendLocationArgument(dest);
			msg.appendBooleanArgument(false);
			msg.appendObjectIDArgument(INVALID_ID);
			commands.append(msg);
		}
		return {};
	}
	GameMessage msg(MSG_BEGIN_NETWORK_MESSAGES, pi); // replaced by make() below
	auto make = [&](GameMessageType type) { msg = GameMessage(type, pi); };
	if (order == "moveTo" || order == "attackMoveTo" || order == "forceMoveTo" || order == "addWaypoint" || order == "attackGround")
	{
		make(order == "moveTo" ? MSG_DO_MOVETO : order == "attackMoveTo" ? MSG_DO_ATTACKMOVETO : order == "forceMoveTo" ? MSG_DO_FORCEMOVETO
			: order == "addWaypoint" ? MSG_ADD_WAYPOINT : MSG_DO_FORCE_ATTACK_GROUND);
		msg.appendLocationArgument(location(at(m.args, 1, "position"), logic));
	}
	else if (order == "attack" || order == "forceAttack")
	{
		const ::ObjectID victim = objectId(at(m.args, 1, "target"), "target");
		const ::Object *v = logic.findObjectByID(victim);
		if (!v || v->isDestroyed() || v->isEffectivelyDead())
			return "expired target";
		make(order == "attack" ? MSG_DO_ATTACK_OBJECT : MSG_DO_FORCE_ATTACK_OBJECT);
		msg.appendObjectIDArgument(victim);
		if (order == "forceAttack")
			msg.appendLocationArgument(*v->getPosition());
	}
	else if (order == "stop")
		make(MSG_DO_STOP);
	else if (order == "enter" || order == "repair" || order == "resumeConstruction")
	{
		const ::ObjectID target = objectId(at(m.args, 1, "target"), "target");
		const ::Object *t = logic.findObjectByID(target);
		if (!t || t->isDestroyed())
			return "expired target";
		make(order == "enter" ? MSG_ENTER : order == "repair" ? MSG_DO_REPAIR : MSG_RESUME_CONSTRUCTION);
		if (order == "enter")
			msg.appendObjectIDArgument(INVALID_ID); // GarrisonCommands reads the container from argument 1
		msg.appendObjectIDArgument(target);
	}
	else if (order == "evacuate")
		make(MSG_EVACUATE);
	else if (order == "sell")
		make(MSG_SELL);
	else if (order == "setStance")
	{
		static const std::map<std::string, StanceType> stances{{"battle", STANCE_BATTLE}, {"aggressive", STANCE_AGGRESSIVE},
			{"holdGround", STANCE_HOLD_GROUND}, {"porcupine", STANCE_PORCUPINE}};
		auto s = stances.find(text(at(m.args, 1, "stance"), "stance"));
		if (s == stances.end())
			return "unknown stance (battle, aggressive, holdGround, porcupine)";
		make(MSG_CHANGE_STANCE);
		msg.appendIntegerArgument(s->second);
	}
	else if (order == "researchUpgrade" || order == "cancelUpgrade")
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(text(at(m.args, 1, "upgrade"), "upgrade")) : nullptr;
		if (!u)
			return "unknown upgrade";
		make(order == "researchUpgrade" ? MSG_QUEUE_UPGRADE : MSG_CANCEL_UPGRADE);
		if (order == "researchUpgrade")
			msg.appendIntegerArgument((int)ids.front()); // argument 0 is the producer's id (not read, the control bar writes it)
		msg.appendIntegerArgument(u->getMaskBit());
	}
	else if (order == "useAbility")
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(text(at(m.args, 1, "ability"), "ability")) : nullptr;
		if (!t)
			return "unknown ability";
		bool owned = false;
		for (::ObjectID id : ids)
			owned = owned || SpecialPowerModules::findModule(*logic.findObjectByID(id), t) != nullptr;
		if (!owned)
			return "none of the objects has that ability";
		const Value *target = m.args.size() > 2 ? &m.args[2] : nullptr;
		if (!target || std::holds_alternative<std::monostate>(target->data))
		{
			make(MSG_DO_SPECIAL_POWER);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID); // the source: the selection
		}
		else if (std::holds_alternative<double>(target->data))
		{
			make(MSG_DO_SPECIAL_POWER_AT_OBJECT);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendObjectIDArgument(objectId(*target, "target"));
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID);
		}
		else
		{
			make(MSG_DO_SPECIAL_POWER_AT_LOCATION);
			msg.appendIntegerArgument((int)t->getID());
			msg.appendLocationArgument(location(*target, logic));
			msg.appendObjectIDArgument(INVALID_ID);
			msg.appendIntegerArgument(0);
			msg.appendObjectIDArgument(INVALID_ID);
		}
	}
	else
		return "unsupported order: " + order;
	// select the ordered objects, give the order, then the player's own selection again (a human's next click must not order the script's units)
	const std::vector<::ObjectID> previous = player->selection();
	GameMessage select(MSG_CREATE_SELECTED_GROUP_NO_SOUND, pi);
	select.appendBooleanArgument(true);
	for (::ObjectID id : ids)
		select.appendObjectIDArgument(id);
	commands.append(select);
	commands.append(msg);
	if (previous.empty())
		commands.append(GameMessage(MSG_DESTROY_SELECTED_GROUP, pi));
	else
	{
		GameMessage restore(MSG_CREATE_SELECTED_GROUP_NO_SOUND, pi);
		restore.appendBooleanArgument(true);
		for (::ObjectID id : previous)
			restore.appendObjectIDArgument(id);
		commands.append(restore);
	}
	return {};
}

std::string GameWorld::bfme_set_ai(int playerIndex, bool enabled)
{
	if (bfme_network_game())
		return "the AI switch is local and not available in network games";
	if (m_replayPlayback)
		return "the AI switch is not available while a replay plays (the recorded game's AI decides)";
	if (!m_game->logic().skirmishAI().setScriptControlled(playerIndex, !enabled))
		return "player has no skirmish AI";
	return {};
}

BfmeLuaRuntime::Value GameWorld::bfme_query(const std::string &name, const std::vector<BfmeLuaRuntime::Value> &args)
{
	// Restricted persistence: named JSON documents in one application-owned folder.
	if (name == "saveBuildingLayout" || name == "loadBuildingLayout" || name == "listBuildingLayouts")
	{
		const String directory = "user://building_layouts";
		if (name == "listBuildingLayouts")
		{
			Value::List result;
			for (const auto &file : DirAccess::get_files_at(directory))
				if (file.ends_with(".json")) result.emplace_back(std::string(file.trim_suffix(".json").utf8().get_data()));
			return result;
		}
		const auto key = text(at(args,0,"layout name"),"layout name");
		if (key.empty() || key.size()>80 || key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
			throw std::runtime_error("layout names must use 1..80 letters, digits, underscores or hyphens");
		const String path = directory + String("/") + String::utf8(key.c_str()) + String(".json");
		if (name == "saveBuildingLayout")
		{
			const auto data = layoutVariant(at(args,1,"layout"));
			Dictionary document; document["version"]=1; document["buildings"]=data;
			const String content = JSON::stringify(document,"  ");
			if (content.utf8().length()>4*1024*1024) throw std::runtime_error("layout exceeds 4 MiB");
			if (DirAccess::make_dir_recursive_absolute(directory)!=OK) throw std::runtime_error("could not create layout directory");
			auto file = FileAccess::open(path+String(".tmp"),FileAccess::WRITE);
			if (file.is_null()) throw std::runtime_error("could not open layout for saving");
			file->store_string(content); file->flush();
			if (file->get_error()!=OK) throw std::runtime_error("could not write layout");
			file->close();
			if (DirAccess::rename_absolute(path+String(".tmp"),path)!=OK) throw std::runtime_error("could not replace saved layout");
			return std::string(path.utf8().get_data());
		}
		auto file = FileAccess::open(path,FileAccess::READ);
		if (file.is_null()) throw std::runtime_error("saved layout not found: "+key);
		if (file->get_length()>4*1024*1024) throw std::runtime_error("layout exceeds 4 MiB");
		Ref<JSON> json; json.instantiate();
		if (json->parse(file->get_as_text())!=OK) throw std::runtime_error("invalid layout JSON");
		Variant data=json->get_data();
		if (data.get_type()!=Variant::DICTIONARY) throw std::runtime_error("invalid layout document");
		Dictionary document=data;
		if (int(document.get("version",0))!=1 || !document.has("buildings")) throw std::runtime_error("unsupported layout document version");
		return layoutValue(document["buildings"]);
	}
	if (!m_game || !m_world)
		throw std::runtime_error("no active match");
	const auto worldContext = m_world->enterContext();
	auto &logic = m_game->logic();
	auto object = [&](size_t i) -> ::Object & {
		::Object *o = logic.findObjectByID(objectId(at(args, i, "object"), "object"));
		if (!o || o->isDestroyed())
			throw std::runtime_error("expired object");
		return *o;
	};
	auto player = [&](size_t i) -> Player & {
		Player *p = logic.players().getNthPlayer((int)SimMath::ftol2(number(at(args, i, "player"), "player", 0, 19)));
		if (!p)
			throw std::runtime_error("expired player");
		return *p;
	};
	auto structure = [&](size_t i) -> const ThingTemplate & {
		const ThingTemplate *tt = m_world->things().findTemplate(text(at(args, i, "building template"), "building template"));
		const ObjectTemplateInfo *info = tt ? &logic.templateInfo(tt->getFinalOverride()) : nullptr;
		if (!info || !(info->kindOf[7 / 32] & (1u << (7 % 32)))) // KindOf STRUCTURE (bit 7, as GodotBfmeLua.cpp's buildingTypes)
			throw std::runtime_error("unknown building template");
		return *tt->getFinalOverride();
	};
	if (name == "trainable")
	{
		::Object &b = object(0);
		Value::List out;
		for (const CommandButton *btn : commandButtons(b))
		{
			if ((btn->m_command != GUI_COMMAND_UNIT_BUILD && btn->m_command != GUI_COMMAND_SECONDARY_UNIT_BUILD) || !btn->m_thingTemplate)
				continue;
			const ThingTemplate *tt = btn->m_thingTemplate->getFinalOverride();
			const CanMakeType r = BuildAssistant::canMakeUnit(b, tt, -1);
			out.emplace_back(Table{{"unit", tt->getName()}, {"status", canMakeText(r)}, {"ok", r == CANMAKE_OK}, {"cost", int(BuildAssistant::buildCost(*tt))},
				{"isBuilder", templateKind(logic, tt, "DOZER")}, {"isHero", templateKind(logic, tt, "HERO")}});
		}
		return out;
	}
	if (name == "canTrain")
	{
		::Object &b = object(0);
		const ThingTemplate *tt = m_world->things().findTemplate(text(at(args, 1, "unit template"), "unit template"));
		if (!tt)
			throw std::runtime_error("unknown unit template");
		const CanMakeType r = BuildAssistant::canMakeUnit(b, tt->getFinalOverride(), -1);
		return Table{{"ok", r == CANMAKE_OK}, {"status", canMakeText(r)}};
	}
	if (name == "buildingLayout")
	{
		::Object &b = object(0);
		Value::List upgrades, links;
		if (TheUpgradeCenter)
			for (const auto *u : TheUpgradeCenter->templates())
				if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT && b.hasUpgrade(u))
					upgrades.emplace_back(u->getUpgradeName());
		Table out{{"upgrades", std::move(upgrades)}, {"castle", b.findModule("CastleBehavior") != nullptr}};
		if (auto castle = dynamic_cast<CastleBehavior *>(b.findModule("CastleBehavior")))
		{
			out["castleReady"] = castle->state() == CastleBehavior::STATE_UNPACKED;
			out["castleError"] = castle->lastError();
		}
		if (auto member = dynamic_cast<CastleMemberBehavior *>(b.findModule("CastleMemberBehavior")))
		{
			out["castleId"] = double(member->castleId());
			out["keepId"] = double(member->keepId());
		}
		if (auto plot = m_game->logic().findObjectByID(b.getProducerID()))
			if (plot->isKindOfName("BASE_FOUNDATION") || plot->isKindOfName("BASE_DEFENSE_FOUNDATION") || plot->isKindOfName("EXPANSION_PAD"))
				out["plot"] = Table{{"template", plot->getTemplate()->getName()}, {"x", double(plot->getPosition()->x)}, {"y", double(plot->getPosition()->y)}};
		if (auto gb = dynamic_cast<GettingBuiltBehavior *>(b.findModule("GettingBuiltBehavior")))
			for (const auto &link : gb->linkedPieces()) links.emplace_back(double(link.id));
		out["links"] = std::move(links);
		return out;
	}
	if (name == "upgrades")
	{
		::Object &b = object(0);
		const Player *owner = b.getControllingPlayer();
		ProductionUpdateInterface *pu = b.getProductionUpdate();
		Value::List out;
		for (const CommandButton *btn : commandButtons(b))
		{
			if (btn->m_command != GUI_COMMAND_PLAYER_UPGRADE && btn->m_command != GUI_COMMAND_OBJECT_UPGRADE)
				continue;
			const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(btn->m_upgradeName) : nullptr;
			if (!u)
				continue;
			const bool objectUpgrade = u->getUpgradeType() == UPGRADE_TYPE_OBJECT;
			// NeededUpgrade: every one of them, or any one with m_neededUpgradeAny (the button's own prerequisite test)
			size_t have = 0;
			for (const std::string &n : btn->m_neededUpgrade)
			{
				const UpgradeTemplate *need = TheUpgradeCenter->findUpgrade(n);
				have += owner && need && (owner->hasUpgradeComplete(need) || b.hasUpgrade(need)) ? 1 : 0;
			}
			const bool needed = btn->m_neededUpgrade.empty() || (btn->m_neededUpgradeAny ? have > 0 : have == btn->m_neededUpgrade.size());
			std::string status = "ok";
			if (objectUpgrade ? b.hasUpgrade(u) : (owner && owner->hasUpgradeComplete(u)))
				status = "complete";
			else if (owner && owner->hasUpgradeInProduction(u))
				status = "researching";
			else if (!needed)
				status = "prerequisites missing";
			else if (!pu || pu->canQueueUpgrade(u) == 4)
				status = "production queue full";
			else if (!UpgradeCenter::canAffordUpgrade(owner, u, &b))
				status = "not enough gold";
			out.emplace_back(Table{{"upgrade", u->getUpgradeName()}, {"kind", objectUpgrade ? "object" : "player"}, {"status", status},
				{"ok", status == "ok"}, {"cost", u->calcCostToBuild(owner, &b)}});
		}
		return out;
	}
	if (name == "buildable")
	{
		::Object &builder = object(0);
		const Player *owner = builder.getControllingPlayer();
		Value::List out;
		for (const CommandButton *btn : commandButtons(builder))
		{
			if ((btn->m_command != GUI_COMMAND_DOZER_CONSTRUCT && btn->m_command != GUI_COMMAND_FOUNDATION_CONSTRUCT) || !btn->m_thingTemplate)
				continue;
			const ThingTemplate *tt = btn->m_thingTemplate->getFinalOverride();
			const int cost = BuildAssistant::calcCostToBuild(*tt, const_cast<Player *>(owner), &builder, -1);
			const bool affordable = owner && cost >= 0 && const_cast<Player *>(owner)->getMoney()->countMoney() >= (unsigned)cost;
			// ECONOMY_STRUCTURE: the resource buildings (farms, Mallorn trees, furnaces ...); FS_FACTORY without it: the production buildings
			const bool economy = templateKind(logic, tt, "ECONOMY_STRUCTURE");
			out.emplace_back(Table{{"building", tt->getName()}, {"cost", cost}, {"affordable", affordable},
				{"onPlot", btn->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT}, {"isEconomy", economy}, {"radius", footprintRadius(logic, *tt)},
				{"isFactory", !economy && templateKind(logic, tt, "FS_FACTORY")}});
		}
		return out;
	}
	if (name == "canBuildAt" || name == "findBuildSite")
	{
		Player &p = player(0);
		const ThingTemplate &tt = structure(1);
		const Coord3D centre = location(at(args, 2, "position"), logic);
		const double radius = name == "findBuildSite" ? number(at(args, 3, "radius"), "radius", 0, 5000) : 0;
		::Object *builder = args.size() > 4 && !std::holds_alternative<std::monostate>(args[4].data) ? &object(4) : nullptr;
		const float angle = args.size() > 5 && std::holds_alternative<double>(args[5].data) ? float(number(args[5], "angle", -1e6, 1e6)) : 0.0f;
		// clearance: the gap kept to every structure (any owner), each taken as the circle its footprints reach: room for units to walk between
		const double clearance = args.size() > 6 && std::holds_alternative<double>(args[6].data) ? number(args[6], "clearance", 0, 1000) : 0.0;
		struct Taken { double x, y, r; };
		std::vector<Taken> taken;
		// avoid: circles {x, y, r} the script keeps free (sites it ordered that are not built yet, sites a construction was refused at)
		if (args.size() > 7)
			if (auto list = std::get_if<Value::List>(&args[7].data))
				for (const Value &v : *list)
					if (auto t = std::get_if<Table>(&v.data); t && t->count("x") && t->count("y"))
						taken.push_back({number(t->at("x"), "avoid x", -1e7, 1e7), number(t->at("y"), "avoid y", -1e7, 1e7),
							t->count("r") ? number(t->at("r"), "avoid r", 0, 5000) : 0.0});
		const size_t avoided = taken.size();
		const double ownRadius = clearance > 0 ? footprintRadius(logic, tt) : 0.0;
		if (clearance > 0)
			for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
				if (!o->isDestroyed() && o->isKindOfName("STRUCTURE") && o->getTemplate())
					taken.push_back({double(o->getPosition()->x), double(o->getPosition()->y), footprintRadius(logic, *o->getTemplate()->getFinalOverride())});
		const double reach = std::max(ownRadius, footprintRadius(logic, tt));
		// a builder: the check its construct() makes (DozerAIUpdate::siteIsLegal, flags 0x49F: terrain, overlap, the owner's shroud ...) and a path
		// from it to the site (the pathfinder's zone test; BuildPlacement's CLEAR_PATH query is S-301); without one: the builder-less construction's
		DozerAIUpdate *dozer = builder ? dynamic_cast<DozerAIUpdate *>(builder->getAIUpdateInterface()) : nullptr;
		if (builder && !dozer)
			throw std::runtime_error("the builder cannot construct");
		auto why = [&](const Coord3D &c) -> std::string {
			for (size_t i = 0; i < taken.size(); ++i)
				if (std::hypot(double(c.x) - taken[i].x, double(c.y) - taken[i].y) < (i < avoided ? reach : ownRadius + clearance) + taken[i].r)
					return i < avoided ? "a site the script avoids" : "too close to a structure";
			if (dozer)
			{
				if (!dozer->siteIsLegal(tt, c, angle, p))
					return "illegal for this builder (terrain, objects or shroud)";
				AIUpdateInterface *ai = builder->getAIUpdateInterface();
				if (logic.aiWorld() && !logic.aiWorld()->pathfinder().clientSafeQuickDoesPathExist(ai->locomotorInfo(), builder->getPosition(), &c))
					return "the builder cannot reach it";
				return {};
			}
			const LegalBuildCode code = BuildPlacement::isLocationLegalToBuild(logic, c, tt, angle, LLF_TERRAIN_RESTRICTIONS | LLF_NO_OBJECT_OVERLAP, nullptr, &p);
			return code == LBC_OK ? std::string() : std::string(BuildPlacement::codeName(code));
		};
		auto legal = [&](const Coord3D &c) { return why(c).empty(); };
		if (name == "canBuildAt")
		{
			const std::string reason = why(centre);
			return Table{{"ok", reason.empty()}, {"reason", reason.empty() ? "ok" : reason}};
		}
		// a square spiral of 40-unit cells out to `radius` (nearest rings first)
		const int rings = (int)SimMath::ftol2(radius / 40.0);
		for (int ring = 0; ring <= rings; ++ring)
			for (int dx = -ring; dx <= ring; ++dx)
				for (int dy = -ring; dy <= ring; ++dy)
				{
					if (std::max(std::abs(dx), std::abs(dy)) != ring)
						continue;
					Coord3D c = centre;
					c.x = SimMath::addf32(c.x, float(dx * 40));
					c.y = SimMath::addf32(c.y, float(dy * 40));
					c.z = logic.getGroundHeight(c.x, c.y);
					if (legal(c))
						return Table{{"x", double(c.x)}, {"y", double(c.y)}, {"z", double(c.z)}};
				}
		return {};
	}
	if (name == "abilities")
		return abilities(object(0));
	if (name == "kindOf")
		return object(0).isKindOfName(text(at(args, 1, "kind"), "kind").c_str());
	if (name == "powers")
	{
		Player &p = player(0);
		::Object *book = SpecialPowerModules::findSpellBookObject(logic, p);
		return book ? abilities(*book) : Value::List{};
	}
	if (name == "sciences")
	{
		Player &p = player(0);
		Value::List out;
		const bool mp = logic.economy().isMultiplayerGame(); // MP science costs (ScienceStore::getSciencePurchaseCost)
		for (const ScienceInfo *si : TheScienceStore ? TheScienceStore->sciences() : std::vector<const ScienceInfo *>{})
		{
			const ScienceType st = si->m_science;
			out.emplace_back(Table{{"science", TheScienceStore->getInternalNameForScience(st)}, {"owned", p.science().hasScience(st)},
				{"canPurchase", !p.science().hasScience(st) && p.science().isCapableOfPurchasingScience(st)},
				{"cost", TheScienceStore->getSciencePurchaseCost(st, mp)}});
		}
		return out;
	}
	if (name == "ai")
	{
		Player &p = player(0);
		const SkirmishAIManager &ai = logic.skirmishAI();
		const AISkirmishPlayer *a = ai.findAI(p.getPlayerIndex());
		Table out{{"hasAI", a != nullptr}, {"enabled", ai.aiDecides(p.getPlayerIndex())}, {"scriptControlled", ai.isScriptControlled(p.getPlayerIndex())}};
		if (a)
		{
			out["disabled"] = a->disabled;
			out["army"] = a->armySide;
			out["networkGame"] = bfme_network_game();
		}
		return out;
	}
	if (name == "wallHubSites") return wall_hub_markers();
	if (name == "farmSites")
	{
		// the map's resource building markers: FarmTemplate objects (the native economy builder's farm sites, AIBaseBuilder::makeFarmSites RW 0x8EEBAC),
		// each with its angle and whether a structure already stands on it
		Value::List out;
		for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->getTemplate() || o->getTemplate()->getName() != "FarmTemplate")
				continue;
			const Coord3D at = *o->getPosition();
			int ownerOnSite = -1;
			bool occupied = false;
			for (::Object *s = logic.getFirstObject(); s && !occupied; s = s->getNextObject())
				if (s != o && !s->isDestroyed() && s->isKindOfName("STRUCTURE") &&
					std::hypot(double(s->getPosition()->x) - at.x, double(s->getPosition()->y) - at.y) < 80.0)
				{
					occupied = true;
					ownerOnSite = s->getControllingPlayer() ? s->getControllingPlayer()->getPlayerIndex() : -1;
				}
			Table t{{"x", double(at.x)}, {"y", double(at.y)}, {"z", double(logic.getGroundHeight(at.x, at.y))}, {"angle", double(o->getOrientation())},
				{"occupied", occupied}};
			if (ownerOnSite >= 0)
				t["playerId"] = ownerOnSite;
			out.emplace_back(std::move(t));
		}
		return out;
	}
	if (name == "startPositions")
	{
		Value::List out;
		for (const StartingBase::Placed &placed : m_game->report().startingObjects)
		{
			if (!placed.structure)
				continue;
			const ::Object *o = logic.findObjectByID(placed.id);
			Table t{{"x", double(placed.position.x)}, {"y", double(placed.position.y)}, {"z", double(placed.position.z)}, {"template", placed.templateName}};
			if (o && o->getControllingPlayer())
				t["playerId"] = o->getControllingPlayer()->getPlayerIndex();
			out.emplace_back(std::move(t));
		}
		return out;
	}
	if (name == "groundHeight")
	{
		const float x = float(number(at(args, 0, "x"), "x", -1e7, 1e7)), y = float(number(at(args, 1, "y"), "y", -1e7, 1e7));
		return double(logic.getGroundHeight(x, y));
	}
	throw std::runtime_error("unknown bfme query: " + name);
}

void GameWorld::bfme_snapshot_ai(BfmeLuaRuntime::Snapshot &out)
{
	// lane LUAAI-1: the fields a script AI decides from, added to the console snapshot
	out.networkGame = bfme_network_game();
	out.replay = m_replayPlayback != nullptr;
	auto &logic = m_game->logic();
	auto &players = logic.players();
	const SkirmishAIManager &ai = logic.skirmishAI();
	for (auto &kv : out.collections["players"])
	{
		const Player *p = players.getNthPlayer(std::stoi(kv.first));
		if (!p)
			continue;
		Table &r = kv.second;
		r["hasAI"] = ai.findAI(p->getPlayerIndex()) != nullptr;
		r["aiEnabled"] = ai.aiDecides(p->getPlayerIndex());
		r["scriptControlled"] = ai.isScriptControlled(p->getPlayerIndex());
		r["defeated"] = p->isDefeated();
		r["observer"] = p->isObserver();
		Table relations;
		for (int i = 0; i < players.getPlayerCount(); ++i)
			if (const Player *other = players.getNthPlayer(i))
				if (other != p)
					relations[std::to_string(i)] = relationName(p->getRelationship(other));
		r["relations"] = std::move(relations);
	}
	for (const char *kind : {"unitInstances", "buildingInstances"})
		for (auto &kv : out.collections[kind])
		{
			::Object *o = logic.findObjectByID((::ObjectID)std::stoul(kv.first));
			if (!o)
				continue;
			Table &r = kv.second;
			r["isHero"] = o->isKindOfName("HERO");
			r["isBuilder"] = o->isKindOfName("DOZER");
			if (o->isKindOfName("STRUCTURE"))
			{
				r["radius"] = footprintRadius(logic, *o->getTemplate()->getFinalOverride());
				r["isEconomy"] = o->isKindOfName("ECONOMY_STRUCTURE");
				r["isFactory"] = !o->isKindOfName("ECONOMY_STRUCTURE") && o->isKindOfName("FS_FACTORY");
			}
			ContainModuleInterface *contain = o->getContain();
			r["isHorde"] = contain && contain->getHordeContainInterface() != nullptr;
			const AIUpdateInterface *aiu = o->getAIUpdateInterface();
			r["isIdle"] = aiu ? aiu->isIdle() : true;
			if (const ProductionUpdateInterface *pu = o->getProductionUpdate())
			{
				r["productionCount"] = pu->getProductionCount();
				if (const ProductionEntry *e = pu->firstProduction())
				{
					r["producing"] = e->objectToProduce ? e->objectToProduce->getName() : e->upgradeToResearch ? e->upgradeToResearch->getUpgradeName() : std::string();
					r["productionPercent"] = double(e->percentComplete);
				}
			}
		}
}
} // namespace godot
