// OpenBFME. GPL-3.0.
#include "GameLogic/ScriptEngine/ScriptEditionRules.h"

#include "Common/BuildAssistant.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"

namespace
{
template <typename Record>
const ScriptParameter &param(const Record &record, size_t index)
{
	static const ScriptParameter empty;
	return index < record.params.size() ? record.params[index] : empty;
}
} // namespace

bool ScriptEditionRules::execute(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	GameLogic &logic = engine.logic();
	if (!logic.things().bfme1Data())
	{
		return false;
	}

	if (name == "BUILD_BASE_BUILDING" || name == "BUILD_BASE_BUILDING_IN_SLOT" ||
		name == "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER" || name == "BUILD_BUILDING_ON_FOUNDATION")
	{
		// DONOR BFME1 ScriptActionsBuildBase.cpp: current owner, affordability,
		// construction through the foundation interface, then bind the reference.
		const bool slot = name == "BUILD_BASE_BUILDING_IN_SLOT";
		const bool marker = name == "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER";
		const size_t baseIndex = marker ? 3 : slot ? 2 : 1;
		Object *base = engine.getUnitNamed(param(a, baseIndex).stringValue);
		// S-5017: a keep reference uses its owning castle; a direct pad keeps its own interface.
		if (base && name != "BUILD_BUILDING_ON_FOUNDATION" && base->getTemplate()->bfme1Data())
			if (auto *member = dynamic_cast<CastleMemberBehavior *>(base->findModule("CastleMemberBehavior")))
				if (!base->findModule("CastleBehavior")) base = logic.findObjectByID(member->castleId());
		Player *owner = base ? base->getControllingPlayer() : nullptr;
		const ThingTemplate *tt = logic.things().findTemplate(param(a, 0).stringValue);
		if (!base || !owner || owner != engine.currentPlayer() || !tt) return true;
		const int cost = BuildAssistant::calcCostToBuild(*tt, owner, nullptr, -1);
		if (owner->getMoney()->countMoney() < (unsigned)cost) return true;
		Object *made = nullptr;
		if (name == "BUILD_BUILDING_ON_FOUNDATION")
		{
			for (const auto &module : base->modules())
				if (auto *foundation = module->getFoundationAIUpdate())
				{ made = foundation->createOnSite(*tt, *base->getPosition(), 0.0f, *owner, false); break; }
		}
		else if (auto *castle = dynamic_cast<CastleBehavior *>(base->findModule("CastleBehavior")))
		{
			if (!castle->hasFreeBuildSlot(tt)) return true;
			if (marker) engine.note("[S-5017] BFME1 tactical-marker construction uses first free castle pad; native proximity selection is unported");
			made = castle->buildInSlot(*tt, slot ? param(a, 1).intValue : -2);
		}
		const size_t reference = marker ? 4 : slot ? 3 : 2;
		if (made && name != "BUILD_BUILDING_ON_FOUNDATION" && !param(a, reference).stringValue.empty())
		{
			engine.setReference(param(a, reference).stringValue, *made);
		}
		return true;
	}

	if (name == "TEAM_RECRUIT_UNITS")
	{
		// DONOR BFME1 ScriptActionsTeamRecruit.cpp (0x2F5BE0): create team,
		// resolve template/object list, recruit count; empty teams use map-wide range.
		// S-5017: the helper's candidate ranking is not recovered. Restrict this
		// first slice to default-team units and stable object order; populated
		// teams keep the explicit stop instead of inventing MaxRecruitDistance.
		Team *target = ScriptConditions::team(engine, param(a, 0).stringValue, true);
		Player *owner = target ? target->getControllingPlayer() : nullptr;
		if (!target || !owner) return true;
		if (target->getMemberCount() != 0)
		{
			engine.noteUnportedAction(name);
			return true;
		}
		const auto types = ScriptConditions::typeNames(engine, param(a, 2).stringValue);
		int remaining = param(a, 1).intValue;
		for (Object *o = logic.getFirstObject(); o && remaining > 0; o = o->getNextObject())
		{
			if (o->getTeam() != owner->getDefaultTeam() || o->isEffectivelyDead() || o->isUnderConstruction()) continue;
			bool matches = false;
			for (const auto &type : types) matches = matches || ScriptConditions::equivalentTemplates(o->getTemplate(), logic.things().findTemplate(type));
			if (!matches) continue;
			engine.note("[S-5017] BFME1 empty-team recruitment uses default-team candidates in object order; native ranking is unverified");
			o->setTeam(target);
			--remaining;
		}
		target->setActive();
		return true;
	}

	if (name == "TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF")
	{
		// DONOR BFME1 ScriptActions_doTeamMoveToNearestObjectOfKindof.cpp:
		// all players, closest object to team center, ordinary script move.
		Team *team = ScriptConditions::team(engine, param(a, 0).stringValue);
		if (!team) return true;
		const auto members = ScriptConditions::members(*team);
		if (members.empty()) return true;
		Coord3D center{};
		for (Object *o : members)
		{
			center.x = SimMath::addf32(center.x, o->getPosition()->x);
			center.y = SimMath::addf32(center.y, o->getPosition()->y);
			center.z = SimMath::addf32(center.z, o->getPosition()->z);
		}
		center.x = SimMath::divf32(center.x, (float)members.size());
		center.y = SimMath::divf32(center.y, (float)members.size());
		center.z = SimMath::divf32(center.z, (float)members.size());
		const int kind = ObjectTemplateInfoBuilder::kindOfIndex(param(a, 1).stringValue.c_str());
		Object *closest = nullptr;
		float best = 0.0f;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->isDestroyed()) continue;
			if (kind < 0 || !o->isKindOf((unsigned)kind)) continue;
			const float dx = SimMath::subf32(o->getPosition()->x, center.x), dy = SimMath::subf32(o->getPosition()->y, center.y), dz = SimMath::subf32(o->getPosition()->z, center.z);
			const float distance = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), SimMath::mulf32(dz, dz));
			if (!closest || distance < best) { closest = o; best = distance; }
		}
		if (closest)
			for (Object *o : members) if (auto *ai = o->getAIUpdateInterface()) ai->aiMoveToPosition(*closest->getPosition(), CMD_FROM_SCRIPT);
		return true;
	}

	return false;
}

std::optional<bool> ScriptEditionRules::evaluate(ScriptEngine &engine, const ScriptCondition &c, const std::string &name)
{
	GameLogic &logic = engine.logic();
	if (!logic.things().bfme1Data())
	{
		return std::nullopt;
	}

	if (name == "COMPARE_NUM_PLAYERS_IN_GAME")
	{
		int count = 0;
		for (int index = 0; index < logic.players().getPlayerCount(); ++index)
		{
			const Player *player = logic.players().getNthPlayer(index);
			if (player && !player->isObserver() && (player->getPlayerType() == PLAYER_HUMAN || player->isSkirmishAI())) ++count;
		}
		return ScriptConditions::compareIntegers(param(c, 0).intValue, count, param(c, 1).intValue);
	}
	if ((name == "CAN_BUILD_OBJECTTYPE_AT_BASE") || (name == "CAN_BUILD_AT_BASE" &&
		engine.getUnitNamed(param(c, 1).stringValue) && engine.getUnitNamed(param(c, 1).stringValue)->getTemplate()->bfme1Data()))
	{
		// DONOR BFME1 ScriptConditionsCanBuildAtBase.cpp (0x323E50).
		Object *base = engine.getUnitNamed(param(c, 1).stringValue);
		// S-5017: BFME1 script base references can name the keep after unpack;
		// resolve its existing CastleMember link to the owning castle controller.
		if (base && base->getTemplate()->bfme1Data())
			if (auto *member = dynamic_cast<CastleMemberBehavior *>(base->findModule("CastleMemberBehavior")))
				if (!base->findModule("CastleBehavior") && !base->isKindOfName("BASE_FOUNDATION"))
				{
					engine.note("[S-5017] BFME1 script base references resolve the keep through its CastleMember link");
					base = logic.findObjectByID(member->castleId());
				}
		Player *owner = ScriptConditions::firstPlayer(engine, param(c, 0).stringValue);
		if (!base || !owner || base->getControllingPlayer() != owner) return false;
		const ThingTemplate *tt = name == "CAN_BUILD_OBJECTTYPE_AT_BASE" ? logic.things().findTemplate(param(c, 2).stringValue) : nullptr;
		if (name == "CAN_BUILD_OBJECTTYPE_AT_BASE" && !tt) return false;
		if (auto *castle = dynamic_cast<CastleBehavior *>(base->findModule("CastleBehavior"))) return castle->hasFreeBuildSlot(tt);
		if (!tt && base->isKindOfName("BASE_FOUNDATION"))
		{
			auto *member = dynamic_cast<CastleMemberBehavior *>(base->findModule("CastleMemberBehavior"));
			if (member && member->plotIsTaken()) return false;
			for (const auto &module : base->modules()) if (module->getFoundationAIUpdate()) return true;
		}
		return false;
	}

	return std::nullopt;
}
