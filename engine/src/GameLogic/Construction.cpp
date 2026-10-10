// OpenBFME. GPL-3.0.
// See GameLogic/Construction.h for the sources of every rule.

#include "GameLogic/Construction.h"


#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <cstring>
#include <map>
#include <stdexcept>
#include <variant>

namespace
{
bool templateKindOf(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

// the template's PlacementViewAngle (RW tt + 0x4E0, parseAngleReal); 0 when the template has none
float placementViewAngle(const ThingTemplate &tt)
{
	if (const FieldValue *v = tt.findField("PlacementViewAngle"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}

Object::ModelConditionBits maskOf(const std::vector<const char *> &names)
{
	Object::ModelConditionBits bits{};
	for (const char *n : names)
	{
		const int i = Construction::modelConditionIndex(n);
		bits[(size_t)i >> 5] |= 1u << (i & 31);
	}
	return bits;
}

Object *makeStructure(GameLogic &logic, const ThingTemplate &what, Player &owner, const ObjectStatusMaskType &status)
{
	if (!owner.getDefaultTeam())
	{
		logic.reportError("Construction: player '" + owner.getPlayerName() + "' has no default team");
		return nullptr;
	}
	return logic.newObject(&what, owner.getDefaultTeam(), status);
}

// the model flags of a structure that has just been placed and starts to rise (RW 0x858931 ff)
void startRise(Object &obj)
{
	obj.setConstructionPercent(0.0f);
	obj.setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	Construction::setModelConditions(obj, { "AWAITING_CONSTRUCTION" }, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
}

bool hasGettingBuilt(Object &obj, const ThingTemplate &what)
{
	// lane BUILD-2: the structure's GettingBuiltBehavior runs on its own from its constructor (RW 0x857399): nothing to start. Without one nothing builds a plot's
	// structure (retail: it stays a foundation); reported
	if (!obj.findModule("GettingBuiltBehavior"))
	{
		obj.logic().reportError("Construction: '" + what.getName() + "' has no GettingBuiltBehavior, so nothing builds it [S-302]");
		return false;
	}
	return true;
}

// RW 0x85895D ff: a rising foundation's body starts at 1.0 health: internalChangeHealth((float)(1.0 - health)) (x87; body slots 0x10 / 0x84)
void startAtOneHealth(Object &obj)
{
	if (ActiveBody *body = dynamic_cast<ActiveBody *>(obj.getBodyModule()))
	{
		body->internalChangeHealth(SimMath::fstpDword(SimMath::pc24SubW(1.0, (double)body->getHealth())));
	}
}
} // namespace

int Construction::modelConditionIndex(const char *name)
{
	static std::map<std::string, int> cache;
	auto it = cache.find(name);
	if (it != cache.end())
	{
		return it->second;
	}
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			cache[name] = i;
			return i;
		}
	}
	throw std::logic_error(std::string("Construction: '") + name + "' is not a model condition of the binary's table");
}

void Construction::setModelConditions(Object &obj, const std::vector<const char *> &clear, const std::vector<const char *> &set)
{
	obj.clearAndSetModelConditionFlags(maskOf(clear), maskOf(set));
}

void Construction::onStructureCreated(Player &player, Object *builder, Object &structure)
{
	// RW 0x6AAF3B: the player's bookkeeping of a new structure (the counters of the score screen and the power / supply tallies of other games) is not ported; the
	// command points follow RW 0x693D51 in Object::friend_initObject and the completion (S-302)
	(void)player;
	(void)builder;
	(void)structure;
}

void Construction::onStructureConstructionComplete(Player &player, Object *builder, Object &structure, bool fromRebuild)
{
	// RW 0x6AA72B -> 0x6AA7A8 -> 0x68E0C2: the structure's command points join the player's pool (once: the object remembers)
	(void)player;
	(void)builder;
	(void)fromRebuild;
	structure.addToPlayerCommandPoints();
}

void Construction::completeConstruction(Object &structure, Object *builder)
{
	structure.setConstructionPercent(-1.0f);
	structure.setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	setModelConditions(structure, { "AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" }, { "CONSTRUCTION_COMPLETE" });
	if (Player *owner = structure.getControllingPlayer())
	{
		onStructureConstructionComplete(*owner, builder, structure, false);
	}
	// AUDIO-2: the building-complete voice (RW 0x857DBA, 0x7E2) is emitted by GettingBuiltBehavior::checkCompletion (its m_completedOnce is retail's +0x33
	// guard); not here, so a structure completes with one announcement
	structure.friend_onBuildComplete();
}

Object *Construction::constructOnPlot(Object &plot, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner, bool instant)
{
	GameLogic &logic = plot.logic();
	if (plot.isDestroyed())
	{
		return nullptr; // RW 0x85873D: status bit 0
	}
	FoundationAIUpdate *foundation = nullptr;
	CastleMemberBehavior *member = dynamic_cast<CastleMemberBehavior *>(plot.findModule("CastleMemberBehavior"));
	for (const std::unique_ptr<BehaviorModule> &m : plot.modules())
	{
		if (FoundationAIUpdate *f = m->getFoundationAIUpdate())
		{
			foundation = f;
			break;
		}
	}
	if (!foundation)
	{
		logic.reportError("Construction: object " + std::to_string(plot.getID()) + " is not a build plot (no FoundationAIUpdate)");
		return nullptr;
	}
	if (!instant && member && member->plotIsTaken())
	{
		return nullptr; // RW 0x858787: the plot already holds a structure
	}
	Object *obj = makeStructure(logic, what, owner, ObjectStatusMaskType{});
	if (!obj)
	{
		return nullptr;
	}
	obj->setProducer(&plot); // RW 0x8587E7
	const bool needsBase = templateKindOf(logic, what, "NEED_BASE_FOUNDATION"), isBase = templateKindOf(logic, what, "BASE_FOUNDATION");
	// RW 0x8587EC ff: the angle
	if (templateKindOf(logic, what, "FACE_AWAY_FROM_CASTLE_KEEP"))
	{
		const Object *castle = member && member->castleId() != INVALID_ID ? logic.findObjectByID(member->castleId()) : nullptr;
		if (castle)
		{
			const double a = SimMath::atan2d(SimMath::subf32(plot.getPosition()->y, castle->getPosition()->y), SimMath::subf32(plot.getPosition()->x, castle->getPosition()->x));
			angle = SimMath::addf32((float)a, placementViewAngle(what));
		}
	}
	else if (needsBase)
	{
		angle = SimMath::addf32(placementViewAngle(what), plot.getOrientation());
	}
	const bool rises = !instant && needsBase && !isBase;
	if (rises)
	{
		startRise(*obj); // RW 0x858924 ff
		startAtOneHealth(*obj);
	}
	switch (foundation->foundationData()->m_buildVariation)
	{
		case 1: setModelConditions(*obj, {}, { "BUILD_VARIATION_ONE" }); break;
		case 2: setModelConditions(*obj, {}, { "BUILD_VARIATION_TWO" }); break;
		default: break;
	}
	Coord3D p = pos;
	obj->setPosition(&p);
	obj->setOrientation(angle);
	if (AIWorld *ai = logic.aiWorld())
	{
		if (!templateKindOf(logic, what, "WALK_ON_TOP_OF_WALL"))
		{
			ai->addObjectToPathfindMap(*obj); // RW 0x858AAD
		}
	}
	onStructureCreated(owner, &plot, *obj);
	if (instant)
	{
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
		obj->setConstructionPercent(-1.0f);
	}
	else
	{
		const int cost = BuildAssistant::calcCostToBuild(what, &owner, &plot, -1);
		owner.withdrawMoney((std::uint32_t)cost, true);
		owner.getScoreKeeper().addMoneySpentByKind(logic, &what, cost); // RW 0x858B1E (lane END-2)
		obj->setBuildCostPaid((float)cost);
	}
	if (member)
	{
		member->setOccupantId(obj->getID());
		if (CastleMemberBehavior *theirs = dynamic_cast<CastleMemberBehavior *>(obj->findModule("CastleMemberBehavior")))
		{
			theirs->setCastleId(member->castleId()); // RW 0x858ADE
		}
	}
	// the plot's pad is drawn away once something stands on it (the pad's own CONSTRUCTION_COMPLETE model state is `Model = None`)
	setModelConditions(plot, {}, { "CONSTRUCTION_COMPLETE" });
	if (rises)
	{
		hasGettingBuilt(*obj, what);
	}
	if (instant)
	{
		obj->friend_onBuildComplete(); // RW 0x858B79
		onStructureConstructionComplete(owner, &plot, *obj, false);
	}
	return obj;
}

Object *Construction::constructByDozer(Object &dozer, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	GameLogic &logic = dozer.logic();
	ObjectStatusMaskType status{};
	const bool structure = templateKindOf(logic, what, "STRUCTURE");
	if (structure)
	{
		MaskSet(status, OBJECT_STATUS_UNDER_CONSTRUCTION, true); // RW 0x797894
	}
	Object *obj = makeStructure(logic, what, owner, status);
	if (!obj)
	{
		return nullptr;
	}
	obj->setProducer(&dozer);
	obj->setBuilder(&dozer); // RW 0x88D307
	Coord3D p = pos;
	p.z = logic.getGroundHeight(p.x, p.y);
	obj->setPosition(&p);
	obj->setOrientation(angle);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*obj);
	}
	onStructureCreated(owner, &dozer, *obj);
	const int cost = BuildAssistant::calcCostToBuild(what, &owner, &dozer, -1);
	owner.withdrawMoney((std::uint32_t)cost, true);
	owner.getScoreKeeper().addMoneySpentByKind(logic, &what, cost); // RW 0x88D34A (lane END-2)
	obj->setBuildCostPaid((float)cost);
	if (structure)
	{
		obj->setConstructionPercent(0.0f);
		setModelConditions(*obj, { "AWAITING_CONSTRUCTION" }, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
		hasGettingBuilt(*obj, what); // the dozer builds it (DozerAIUpdate::workOnBuild, RW 0x88DE43)
	}
	else
	{
		obj->friend_onBuildComplete();
	}
	return obj;
}

Object *Construction::buildObjectNow(GameLogic &logic, Object *builder, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	ObjectStatusMaskType status{};
	const bool structure = templateKindOf(logic, what, "STRUCTURE");
	if (structure)
	{
		MaskSet(status, OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	}
	Object *obj = makeStructure(logic, what, owner, status);
	if (!obj)
	{
		return nullptr;
	}
	if (builder)
	{
		obj->setProducer(builder);
	}
	Coord3D p = pos;
	p.z = logic.getGroundHeight(p.x, p.y);
	obj->setPosition(&p);
	obj->setOrientation(angle);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*obj);
	}
	if (structure)
	{
		onStructureCreated(owner, builder, *obj);
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
		onStructureConstructionComplete(owner, builder, *obj, false);
	}
	obj->friend_onBuildComplete();
	return obj;
}

std::vector<std::string> Construction::stopLines()
{
	return {
		"[S-302] construction: the foundation creation follows RW 0x858701 / 0x797796 (plot and dozer builds; a plot's foundation starts at 1.0 health, RW 0x85895D); the rate is retail's "
		"(lane BUILD-2): RotWK's GettingBuiltBehavior (RW 0x857E77) builds a structure without a WorkerName by healing its body MaxHealth / calcTimeToBuild per frame (the percent is health / "
		"MaxHealth * 100, damage sets it back and pauses the build for 4 s), else spawns the worker, whose dozer AI adds 100 / calcTimeToBuild per frame of work (RW 0x88DE43), as does a Porter; "
		"what is still open is listed in S-650 .. S-656 (the build-up animation is drawn: S-461)",
		"[S-650] GettingBuiltBehavior (RW 0x857E77 and its interface RW 0xC56BF0) is ported with these gaps: the setters of module + 0x36 (slot 10) were not found (the flag stays false); "
		"the recent-damage test asks the body only (RW 0x68C933 asks the module at Object + 0x258 first); a castle member's + 0x24 byte is taken as 0 (RW 0x8561C0); an idle builder of an "
		"unfinished structure is not sent back to it (RW 0x857DEA); the player notification that counts a self-built structure's command points is inferred (BUILD-1's completion); RotWK's "
		"dozer queues its structure and places it on arrival with 1 hit point (RW 0x88D44F, lane BUILD-3: the port makes it at the order and sets the health at the arrival, S-304); "
		"BUILD-1's dozer completion sets CONSTRUCTION_COMPLETE, which RW 0x88DEE0 does not",
		"[S-652] Object::attemptHealingFromSoleBenefactor (RW 0x690584): the tail behind the template byte + 0x642 (RUBBLE cleared, a dead object revived) is not ported: the field is not identified",
		"[S-653] GettingBuiltBehavior: calls not identified and not ported: RW 0x79F0E1(object, 1) / Object + 0x456 / BuildAssistant RW 0x797465 (a rebuild's start, a wall segment crossing 20 % "
		"health), the body's slot 0x94 after a heal (RW 0x8C1D53), the production update's slot 0x60 and RW 0x68C3A3's slot 0x28 at a self build's end (RW 0x8569B1): noted at runtime where reached",
		"[S-654] RebuildWhenDead: the DisallowRebuildFilter scan (RW 0x857873) is ported over the object list (3D centre distance, any match blocks); a template without the "
		"filter row blocks nothing (RW's default filter at data + 0x40 was not read: inference); what keeps a dead structure in the world is its own die modules (KeepObjectDie)",
		"[S-655] WorkerAIUpdate (RW 0x8ADAA0, fields RW 0xC058C8) runs as the dozer AI (the workers GettingBuiltBehavior spawns build and repair); its supply-truck half (MaxBoxes, the supply "
		"center / warehouse delays, HarvestTrees and the harvest fields) is not ported",
		"[S-656] repair: MSG_DO_REPAIR sends the selected dozers to repair (DozerAIUpdate::repair, the heal of RW 0x88DC46, free); the dispatcher case of MSG_DO_REPAIR was not read, and the "
		"caller that makes the owner pay a structure's own repair (GettingBuiltBehavior slot 4 without force: ceil(calcCostToBuild * PercentOfBuildCostToRebuild<state>), RW 0x856227) was not found",
	};
}
