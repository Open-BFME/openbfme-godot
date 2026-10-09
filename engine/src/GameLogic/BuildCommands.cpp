// OpenBFME. GPL-3.0.
// See GameLogic/BuildCommands.h for the sources of every rule.

#include "GameLogic/BuildCommands.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/WallSpan.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

namespace
{
bool kindOfName(const Object &obj, const char *name)
{
	return obj.isKindOfName(name);
}

struct ConstructArgs
{
	const ThingTemplate *tmpl = nullptr;
	Coord3D loc;
	float angle = 0.0f;
};

bool readConstructArgs(GameLogic &logic, const GameMessage &m, ConstructArgs &out)
{
	const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1), *a2 = m.getArgument(2);
	if (!a0 || !a1 || !a2 || a0->type != ARGUMENTDATATYPE_INTEGER || a1->type != ARGUMENTDATATYPE_LOCATION || a2->type != ARGUMENTDATATYPE_REAL)
	{
		return false;
	}
	const ThingTemplate *tt = logic.things().findByTemplateID((unsigned short)a0->integer); // RW 0x6CFE6C
	out.tmpl = tt ? tt->getFinalOverride() : nullptr;
	out.loc = a1->location;
	out.angle = a2->real;
	return true;
}
} // namespace

void BuildCommands::refuse(const GameMessage &m, const std::string &why)
{
	++m_stats.refused;
	m_refusals.push_back(std::string(GameMessageTypeName(m.getType())) + " from player " + std::to_string(m.getPlayerIndex()) + ": " + why);
}

void BuildCommands::registerHandlers(GameLogicDispatch &dispatcher)
{
	dispatcher.registerHandler(MSG_FOUNDATION_CONSTRUCT, "BUILD-1", [this](GameLogic &l, const GameMessage &m) { return foundationConstruct(l, m); });
	dispatcher.registerHandler(MSG_DOZER_CONSTRUCT, "BUILD-1", [this](GameLogic &l, const GameMessage &m) { return dozerConstruct(l, m); });
	dispatcher.registerHandler(MSG_DOZER_CANCEL_CONSTRUCT, "BUILD-1", [this](GameLogic &l, const GameMessage &m) { return dozerCancel(l, m); });
	dispatcher.registerHandler(MSG_RESUME_CONSTRUCTION, "BUILD-1", [this](GameLogic &l, const GameMessage &m) { return resumeConstruction(l, m); });
	dispatcher.registerHandler(MSG_DO_REPAIR, "BUILD-1", [this](GameLogic &l, const GameMessage &m) { return repair(l, m); });
	dispatcher.registerHandler(MSG_WALL_HUB_CONSTRUCT_SPAN, "BUILD-2", [this](GameLogic &l, const GameMessage &m) { return wallSpan(l, m); });
}

bool BuildCommands::foundationConstruct(GameLogic &logic, const GameMessage &m)
{
	ConstructArgs a;
	if (!readConstructArgs(logic, m, a))
	{
		++m_stats.rejected;
		return false;
	}
	Player *issuer = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!issuer)
	{
		++m_stats.rejected;
		return false;
	}
	// RW 0x77A945: the first object of the group that is a BASE_FOUNDATION
	Object *plot = nullptr;
	for (ObjectID id : AICommands::selection(logic, m.getPlayerIndex()))
	{
		Object *o = logic.findObjectByID(id);
		if (o && kindOfName(*o, "BASE_FOUNDATION"))
		{
			plot = o;
			break;
		}
	}
	if (!plot || !a.tmpl)
	{
		refuse(m, !plot ? "no build plot in the selection" : "unknown template id");
		return true;
	}
	Player *owner = plot->getControllingPlayer();
	if (!owner || owner != issuer)
	{
		refuse(m, "the plot is not the issuing player's");
		return true;
	}
	const CanMakeType can = BuildAssistant::canMakeUnit(*plot, a.tmpl, -1);
	if (can != CANMAKE_OK)
	{
		refuse(m, "canMakeUnit " + std::to_string((int)can));
		return true;
	}
	FoundationAIUpdate *foundation = nullptr;
	for (const std::unique_ptr<BehaviorModule> &mod : plot->modules())
	{
		if (FoundationAIUpdate *f = mod->getFoundationAIUpdate())
		{
			foundation = f;
			break;
		}
	}
	if (!foundation || !foundation->construct(*a.tmpl, a.loc, a.angle, *owner, false))
	{
		refuse(m, "the plot did not build it");
		return true;
	}
	++m_stats.foundations;
	return true;
}

bool BuildCommands::dozerConstruct(GameLogic &logic, const GameMessage &m)
{
	ConstructArgs a;
	if (!readConstructArgs(logic, m, a))
	{
		++m_stats.rejected;
		return false;
	}
	Player *issuer = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!issuer)
	{
		++m_stats.rejected;
		return false;
	}
	// RW 0x779A21: the first object of the group
	const std::vector<ObjectID> group = AICommands::selection(logic, m.getPlayerIndex());
	Object *builder = group.empty() ? nullptr : logic.findObjectByID(group.front());
	if (!builder || !a.tmpl)
	{
		refuse(m, !builder ? "no builder selected" : "unknown template id");
		return true;
	}
	if (!kindOfName(*builder, "DOZER")) // RW 0x77B067
	{
		refuse(m, "the first selected object is not a DOZER");
		return true;
	}
	Player *owner = builder->getControllingPlayer();
	if (!owner || owner != issuer)
	{
		refuse(m, "the builder is not the issuing player's");
		return true;
	}
	const CanMakeType can = BuildAssistant::canMakeUnit(*builder, a.tmpl, -1);
	if (can != CANMAKE_OK)
	{
		refuse(m, "canMakeUnit " + std::to_string((int)can));
		return true;
	}
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(builder->getAIUpdateInterface());
	if (!dozer)
	{
		refuse(m, "the builder has no DozerAIUpdate in this game (no AI world)");
		return true;
	}
	if (!dozer->siteIsLegal(*a.tmpl, a.loc, a.angle, *owner)) // RW 0x88C2A5: before the builder's state, the object or the money change
	{
		refuse(m, "the site is not legal to build on");
		return true;
	}
	dozer->aiIdle(CMD_FROM_AI); // ZH buildObjectNow: stop any current behavior
	if (!dozer->construct(*a.tmpl, a.loc, a.angle, *owner))
	{
		refuse(m, "the dozer did not start it");
		return true;
	}
	++m_stats.dozerBuilds;
	return true;
}

bool BuildCommands::dozerCancel(GameLogic &logic, const GameMessage &m)
{
	const std::vector<ObjectID> group = AICommands::selection(logic, m.getPlayerIndex());
	Object *obj = group.empty() ? nullptr : logic.findObjectByID(group.front());
	Player *issuer = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!obj || obj->isDestroyed() || !issuer || obj->getControllingPlayer() != issuer || !obj->isUnderConstruction())
	{
		return true; // RW 0x77B1AA .. 0x77B1F7: nothing to cancel
	}
	// the price paid goes back, truncated (RW 0x77B22F)
	const int refund = SimMath::truncToInt32(obj->getBuildCostPaid());
	if (refund > 0)
	{
		issuer->depositMoney((std::uint32_t)refund, true);
	}
	// the plot that held it is free again
	if (Object *plot = logic.findObjectByID(obj->getProducerID()))
	{
		if (CastleMemberBehavior *member = dynamic_cast<CastleMemberBehavior *>(plot->findModule("CastleMemberBehavior")))
		{
			if (member->occupantId() == obj->getID())
			{
				member->setOccupantId(INVALID_ID);
				Construction::setModelConditions(*plot, { "CONSTRUCTION_COMPLETE" }, {});
			}
		}
	}
	obj->removeFromPlayerCommandPoints();
	logic.destroyObject(obj);
	// a dozer working on it stops (lane BUILD-3: after the destruction, so a dozer whose structure was not placed yet does not refund it a second time)
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
		{
			if (d->taskTarget() == obj->getID())
			{
				d->cancelTask();
			}
		}
	}
	++m_stats.cancels;
	return true;
}

bool BuildCommands::resumeConstruction(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0);
	if (!a0 || a0->type != ARGUMENTDATATYPE_OBJECTID)
	{
		++m_stats.rejected;
		return false;
	}
	Object *target = logic.findObjectByID(a0->objectID);
	if (!target)
	{
		return true; // RW 0x77AEB6: the object is gone
	}
	for (ObjectID id : AICommands::selection(logic, m.getPlayerIndex()))
	{
		Object *o = logic.findObjectByID(id);
		if (o)
		{
			if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
			{
				if (d->resumeConstruction(*target))
				{
					++m_stats.resumes;
				}
			}
		}
	}
	return true;
}

bool BuildCommands::repair(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0);
	if (!a0 || a0->type != ARGUMENTDATATYPE_OBJECTID)
	{
		++m_stats.rejected;
		return false;
	}
	// lane BUILD-2: every selected dozer of the issuer repairs the target (RW 0x7714C1 aiRepair: DozerAIUpdate::repair). The dispatcher case of MSG_DO_REPAIR itself was not read
	// (stop S-656): no cost is taken (the dozer's repair path RW 0x88DB9A pays nothing); a dozer that refuses (the target is not its owner's, unfinished, dead) is counted
	Object *target = logic.findObjectByID(a0->objectID);
	if (!target)
	{
		return true;
	}
	for (ObjectID id : AICommands::selection(logic, m.getPlayerIndex()))
	{
		Object *o = logic.findObjectByID(id);
		if (o)
		{
			if (DozerAIUpdate *d = dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
			{
				if (d->repair(*target))
				{
					++m_stats.repairs;
				}
				else
				{
					++m_stats.repairsUnexecuted;
				}
			}
		}
	}
	return true;
}

// lane BUILD-2: RW 0x77C4C3 (see WallSpan.h): argument 4 the hub, 0 the template id, 1 / 2 start / end, 3 the options
bool BuildCommands::wallSpan(GameLogic &logic, const GameMessage &m)
{
	const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1), *a2 = m.getArgument(2), *a3 = m.getArgument(3), *a4 = m.getArgument(4);
	if (!a0 || !a1 || !a2 || !a3 || !a4 || a0->type != ARGUMENTDATATYPE_INTEGER || a1->type != ARGUMENTDATATYPE_LOCATION || a2->type != ARGUMENTDATATYPE_LOCATION ||
		a3->type != ARGUMENTDATATYPE_INTEGER || a4->type != ARGUMENTDATATYPE_OBJECTID)
	{
		++m_stats.rejected;
		return false;
	}
	Object *hub = logic.findObjectByID(a4->objectID);
	const ThingTemplate *tmpl = logic.things().findByTemplateID((unsigned short)(a0->integer & 0xFFFF)); // RW 0x77C4E4: the word
	Player *owner = hub ? hub->getControllingPlayer() : nullptr;
	Player *issuer = logic.players().getNthPlayer(m.getPlayerIndex());
	if (!hub || !tmpl || !owner || owner != issuer)
	{
		refuse(m, "wall span: no hub, template or owner");
		return true;
	}
	// RW 0x77C529 -> 0x6AAA2E(template, 0): the owner can afford the template (calcCostToBuild(owner, no producer) <= money + 0; BUILD-1's S-305 calls this function
	// Player::canBuild), then RW + 0x770 must be 0 (not identified: taken as 0)
	if ((std::uint32_t)BuildAssistant::calcCostToBuild(*tmpl, owner, nullptr, -1) > owner->getMoney()->countMoney())
	{
		refuse(m, "wall span: the owner cannot afford the template");
		return true;
	}
	if (WallSpan::build(logic, *hub, a1->location, a2->location, *owner, (unsigned)a3->integer, nullptr))
	{
		++m_stats.wallSpans;
	}
	else
	{
		refuse(m, "wall span: the plan was refused (an illegal tile, the price, or fewer than two tiles)");
	}
	return true;
}

std::vector<std::string> BuildCommands::acceptanceStops()
{
	return {
		"[S-305] build commands: MSG_FOUNDATION_CONSTRUCT / MSG_DOZER_CONSTRUCT / MSG_DOZER_CANCEL_CONSTRUCT / MSG_RESUME_CONSTRUCTION follow the dispatcher cases read at RW 0x77A91B / 0x77B011 / 0x77B1A0 / "
		"0x77AE9F; Player::canBuild (RW 0x6AAA2E) is stood for by BuildAssistant::canMakeUnit; the Dozer construct body's legality gate (RW 0x88C2A5, flags 0x49F) is ported (DozerAIUpdate::siteIsLegal); MSG_DO_REPAIR sends the selected dozers to repair (DozerAIUpdate::repair; the dispatcher case of MSG_DO_REPAIR was not read: S-656)",
	};
}
