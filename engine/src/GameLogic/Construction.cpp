// OpenBFME. GPL-3.0.
// See GameLogic/Construction.h for the sources of every rule.

#include "GameLogic/Construction.h"
#include "GameLogic/FactionBuildPlots.h"
#include "GameLogic/ConstructionSite.h"


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
	// RW 0x6AAF3B (stdcall over builder, structure; BFME2 decomp PlayerRva002ABD1D.cpp:Rva002AA559, tier A): the command points follow RW 0x693D51 in
	// Object::friend_initObject and the completion (S-302). Lane PLAY-4: with a builder that is a BASE_FOUNDATION (template + 0x114 & 0x100: a plot or a castle)
	// or a DOZER (+ 0x109 & 0x40), the structure's behavior slot 0x80 interface (RW 0x68C3E6: GettingBuiltBehavior) is told. A BASE_FOUNDATION builder's
	// FoundationAIUpdate interface (RW 0x68C3C3) first records the structure (slot 2, RW 0x8582DE: the port's CastleMemberBehavior occupant, set by the callers),
	// then the structure's + 0x36 flag is set (slot 10 with 1) and it spawns its worker or starts building itself (slot 1 with force 1); a DOZER builder only
	// sets the flag
	(void)player;
	if (!builder)
	{
		return;
	}
	const bool foundation = builder->isKindOfName("BASE_FOUNDATION"), dozer = builder->isKindOfName("DOZER");
	if (!foundation && !dozer)
	{
		return;
	}
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(structure.findModule("GettingBuiltBehavior"));
	if (!gb)
	{
		return;
	}
	bool foundationInterface = false;
	for (const std::unique_ptr<BehaviorModule> &m : builder->modules())
	{
		// RW 0x68C3C3: the first module answering slot 0x98 (a plot's FoundationAIUpdate; a castle's CastleBehavior is one, vtable RW 0xC30DE8)
		foundationInterface = foundationInterface || m->getFoundationAIUpdate() != nullptr;
	}
	if (foundation && foundationInterface)
	{
		gb->setForceComplete(true);
		gb->spawnWorkerOrStart(true);
	}
	if (dozer)
	{
		gb->setForceComplete(true);
	}
}

void Construction::onStructureConstructionComplete(Player &player, Object *builder, Object &structure, bool fromRebuild)
{
	// RW 0x6AA72B -> 0x6AA7A8 -> 0x68E0C2: the structure's command points join the player's pool (once: the object remembers). Lane PLAY-4: before that the
	// structure's behavior slot 0x80 interface (RW 0x68C3E6: GettingBuiltBehavior) is told the build is done (RW 0x6AA796: slot 3, finishConstruction(builder)),
	// which enables its production again
	(void)player;
	(void)fromRebuild;
	if (GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(structure.findModule("GettingBuiltBehavior")))
	{
		gb->finishConstruction(builder);
	}
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
	const bool factionPlot = FactionBuildPlots::isPlot(plot.getTemplate()->getName());
	const bool rises = !instant && (needsBase || factionPlot) && !isBase;
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
	else if (member && (factionPlot || obj->findModule("CastleMemberBehavior")))
	{
		// lane BACKLOG2: RW 0x858AB3 .. 0x858AE8: the price is paid only when the new object and the plot both have a CastleMemberBehavior (the name key
		// RW 0xDE8E50 = "CastleMemberBehavior", RW 0xC0BE84): a structure without one built on a castle (an economy plot's explicit unpack, RW 0x79B98B) was
		// paid by the castle's unpack (RW 0x797B06). Enhanced faction plots charge ordinary builder buildings here too.
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
			// The new expansion is created before its castle link exists. Reapply
			// upgrades now so improvements bought before construction reach it too.
			theirs->setKeepId(member->keepId());
			obj->updateUpgradeModules();
		}
	}
	// the plot's pad is drawn away once something stands on it (the pad's own CONSTRUCTION_COMPLETE model state is `Model = None`)
	setModelConditions(plot, {}, { "CONSTRUCTION_COMPLETE" });
	if ((plot.isKindOfName("BASE_FOUNDATION") || plot.isKindOfName("BASE_DEFENSE_FOUNDATION") || plot.isKindOfName("EXPANSION_PAD")) && member)
	{
		plot.setDrawableHidden(true);
		const int unselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
		if (unselectable >= 0) plot.setStatus((unsigned)unselectable, true);
		foundation->wakeFromInterface(UPDATE_SLEEP_NONE);
	}
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
	// lane PLAY-4: RotWK's dozer makes the structure as a phantom with PENDING_CONSTRUCTION and PHANTOM_STRUCTURE (RW 0x88D29B .. 0x88D2C9:
	// createPhantomStructure, statuses 0x57 / 0x58) and clears both when it places it on arrival (RW 0x88D61B .. 0x88D63A). The port makes the structure
	// at the order (S-304) with PENDING_CONSTRUCTION, so its GettingBuiltBehavior disables its production when it is created (RW 0x856434), as retail's
	// phantom's does; DozerAIUpdate::placeUnplacedStructure clears it (PHANTOM_STRUCTURE is not set: S-304)
	static const int pending = ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION");
	MaskSet(status, (unsigned)pending, true);
	Object *obj = makeStructure(logic, what, owner, status);
	if (!obj)
	{
		return nullptr;
	}
	ConstructionSite::clearRemovableForConstruction(logic, what, pos, angle);
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

Object *Construction::constructWithoutBuilder(GameLogic &logic, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	// Enhanced scripting operation, composed from constructOnPlot/startRise and
	// GettingBuiltBehavior::startConstruction. No fabricated builder or instant completion.
	ObjectStatusMaskType status{};
	MaskSet(status, OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	MaskSet(status, (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION"), true);
	Object *obj = makeStructure(logic, what, owner, status);
	if (!obj) return nullptr;
	auto build = dynamic_cast<GettingBuiltBehavior *>(obj->findModule("GettingBuiltBehavior"));
	if (!build || !dynamic_cast<ActiveBody *>(obj->getBodyModule()))
	{
		logic.destroyObject(obj);
		throw std::runtime_error("building needs GettingBuiltBehavior and ActiveBody");
	}
	Coord3D p = pos;
	p.z = logic.getGroundHeight(p.x, p.y);
	ConstructionSite::clearSiteForFoundation(logic, what, p, angle);
	obj->setPosition(&p);
	obj->setOrientation(angle);
	if (auto ai = logic.aiWorld()) ai->addObjectToPathfindMap(*obj);
	onStructureCreated(owner, nullptr, *obj);
	startRise(*obj);
	startAtOneHealth(*obj);
	obj->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION"), false);
	build->setForceComplete(true);
	build->startConstruction(true);
	// Native force-start uses a repair sentinel; a new scripted foundation starts at zero.
	startRise(*obj);
	const int cost = BuildAssistant::calcCostToBuild(what, &owner, nullptr, -1);
	owner.withdrawMoney((std::uint32_t)cost, true);
	owner.getScoreKeeper().addMoneySpentByKind(logic, &what, cost);
	obj->setBuildCostPaid((float)cost);
	return obj;
}

Object *Construction::buildObjectNow(GameLogic &logic, Object *builder, const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner)
{
	// lane BACKLOG2: RW 0x7977C9 .. 0x79780C: the site is cleared and the units moved off it first (ConstructionSite::prepareSite: not for a DOZER builder, a
	// NO_COLLIDE or DO_NOT_CLASSIFY template); retail reads the builder unchecked, the port's callers without one skip it
	if (builder)
	{
		ConstructionSite::prepareSite(logic, *builder, what, pos, angle, owner);
	}
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
		"[S-650] GettingBuiltBehavior (RW 0x857E77 and its interface RW 0xC56BF0) is ported with these gaps (lane PLAY-4 found the writer of module + 0x36, RW 0x6AAF3B, and the production slot 0x60, setFactoryDisabled); "
		"the recent-damage test asks the body only (RW 0x68C933 asks the module at Object + 0x258 first); a castle member's + 0x24 byte is taken as 0 (RW 0x8561C0); an idle builder of an "
		"unfinished structure is not sent back to it (RW 0x857DEA); the player notification that counts a self-built structure's command points is inferred (BUILD-1's completion); RotWK's "
		"dozer queues its structure and places it on arrival with 1 hit point (RW 0x88D44F, lane BUILD-3: the port makes it at the order and sets the health at the arrival, S-304); "
		"BUILD-1's dozer completion sets CONSTRUCTION_COMPLETE, which RW 0x88DEE0 does not",
		"[S-652] Object::attemptHealingFromSoleBenefactor (RW 0x690584): the tail behind the template byte + 0x642 (RUBBLE cleared, a dead object revived) is not ported: the field is not identified",
		"[S-653] GettingBuiltBehavior: calls not identified and not ported: Player + 0x3DC's RW 0x79F0E1(object, 1) and Object + 0x456 at a rebuild's start, the body's slot "
		"0x94 after a heal (RW 0x8C1D53), RW 0x68C3A3's slot 0x28 at a build's end (RW 0x8569C7; the production update's slot 0x60 before it is setFactoryDisabled(false), "
		"lane PLAY-4): noted at runtime where reached. Lane BACKLOG2 ported BuildAssistant::moveObjectsForConstruction RW 0x797465 (the rebuild's start RW 0x85679D, a "
		"wall segment crossing 20 % health RW 0x85805E, buildObjectNow RW 0x797807 with clearRemovableForConstruction RW 0x796576 for the plots' builds); its "
		"inference: an object's own geometry is its template's shapes with its active flags",
		"[S-654] RebuildWhenDead: the DisallowRebuildFilter scan (RW 0x857873) is ported over the object list (3D centre distance, any match blocks); a template without the "
		"filter row blocks nothing (RW's default filter at data + 0x40 was not read: inference); what keeps a dead structure in the world is its own die modules (KeepObjectDie)",
		"[S-655] WorkerAIUpdate (RW 0x8ADAA0, fields RW 0xC058C8) runs as the dozer AI (the workers GettingBuiltBehavior spawns build and repair); its supply-truck half (MaxBoxes, the supply "
		"center / warehouse delays, HarvestTrees and the harvest fields) is not ported",
		"[S-656] repair: MSG_DO_REPAIR sends the selected dozers to repair (DozerAIUpdate::repair, the heal of RW 0x88DC46, free); the dispatcher case of MSG_DO_REPAIR was not read, and the "
		"caller that makes the owner pay a structure's own repair (GettingBuiltBehavior slot 4 without force: ceil(calcCostToBuild * PercentOfBuildCostToRebuild<state>), RW 0x856227) was not found",
	};
}
