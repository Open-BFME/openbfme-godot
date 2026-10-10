// OpenBFME. GPL-3.0.
// See GameLogic/Module/ConstructionModules.h for the sources of every rule.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ConstructionModules.h"

#include "Common/Audio/AudioRequests.h"

#include "Common/NumericState.h"
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/ActiveBody.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
void parseOptionalObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

#define FD_OFF(member) (int)offsetof(FoundationAIUpdateModuleData, member)
const FieldParse kFoundationFieldParse[] = {
	{ "RepairHealthPercentPerSecond", INI::parsePercentToReal, nullptr, FD_OFF(m_repairHealthPercentPerSecond) },
	{ "BuildVariation", INI::parseInt, nullptr, FD_OFF(m_buildVariation) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef FD_OFF

#define GB_OFF(member) (int)offsetof(GettingBuiltBehaviorModuleData, member)
const FieldParse kGettingBuiltFieldParse[] = {
	{ "WorkerName", INI::parseAsciiString, nullptr, GB_OFF(m_workerName) },
	{ "EvilWorkerName", INI::parseAsciiString, nullptr, GB_OFF(m_evilWorkerName) },
	{ "TestFaction", INI::parseBool, nullptr, GB_OFF(m_testFaction) },
	{ "SpawnTimer", INI::parseReal, nullptr, GB_OFF(m_spawnTimer) },
	{ "RebuildWhenDead", INI::parseBool, nullptr, GB_OFF(m_rebuildWhenDead) },
	{ "HealWeapon", INI::parseAsciiString, nullptr, GB_OFF(m_healWeapon) },
	{ "RebuildTimeSeconds", INI::parseReal, nullptr, GB_OFF(m_rebuildTimeSeconds) },
	{ "SelfBuildingLoop", INI::parseAsciiString, nullptr, GB_OFF(m_selfBuildingLoop) },
	{ "SelfRepairFromDamageLoop", INI::parseAsciiString, nullptr, GB_OFF(m_selfRepairFromDamageLoop) },
	{ "SelfRepairFromRubbleLoop", INI::parseAsciiString, nullptr, GB_OFF(m_selfRepairFromRubbleLoop) },
	{ "PercentOfBuildCostToRebuildPristine", INI::parsePercentToReal, nullptr, GB_OFF(m_percentOfBuildCostToRebuildPristine) },
	{ "PercentOfBuildCostToRebuildDamaged", INI::parsePercentToReal, nullptr, GB_OFF(m_percentOfBuildCostToRebuildDamaged) },
	{ "PercentOfBuildCostToRebuildReallyDamaged", INI::parsePercentToReal, nullptr, GB_OFF(m_percentOfBuildCostToRebuildReallyDamaged) },
	{ "PercentOfBuildCostToRebuildRubble", INI::parsePercentToReal, nullptr, GB_OFF(m_percentOfBuildCostToRebuildRubble) },
	{ "DisallowRebuildFilter", parseOptionalObjectFilter, nullptr, GB_OFF(m_disallowRebuildFilter) },
	{ "DisallowRebuildRange", INI::parseReal, nullptr, GB_OFF(m_disallowRebuildRange) },
	{ "UseSpawnTimerWithoutWorker", INI::parseBool, nullptr, GB_OFF(m_useSpawnTimerWithoutWorker) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef GB_OFF

#define BB_OFF(member) (int)offsetof(BuildingBehaviorModuleData, member)
const FieldParse kBuildingFieldParse[] = {
	{ "NightWindowName", INI::parseAsciiStringVectorAppend, nullptr, BB_OFF(m_nightWindowName) },
	{ "FireWindowName", INI::parseAsciiStringVectorAppend, nullptr, BB_OFF(m_fireWindowName) },
	{ "GlowWindowName", INI::parseAsciiStringVectorAppend, nullptr, BB_OFF(m_glowWindowName) },
	{ "FireName", INI::parseAsciiStringVectorAppend, nullptr, BB_OFF(m_fireName) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BB_OFF

template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void FoundationAIUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFoundationFieldParse);
}
void GettingBuiltBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kGettingBuiltFieldParse);
}
void BuildingBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kBuildingFieldParse);
}

// ---- FoundationAIUpdate -------------------------------------------------------------------------------------------------------------
FoundationAIUpdate::FoundationAIUpdate(Thing *thing, const FoundationAIUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_foundationData(data)
{
	// RW 0x858246 -> 0x850C32: the module starts asleep (the plot has nothing to run each frame)
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
}

void FoundationAIUpdate::onCapture(Player *oldOwner, Player *newOwner)
{
	(void)oldOwner;
	Team *to = newOwner ? newOwner->getDefaultTeam() : nullptr;
	const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(getObject()->findModule("CastleMemberBehavior"));
	if (!to || !member || member->occupantId() == INVALID_ID)
	{
		return;
	}
	if (Object *occupant = getObject()->logic().findObjectByID(member->occupantId()))
	{
		occupant->setTeam(to);
	}
}

Object *FoundationAIUpdate::construct(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner, bool instant)
{
	return Construction::constructOnPlot(*getObject(), what, pos, angle, owner, instant);
}

// ---- GettingBuiltBehavior (lane BUILD-2: RotWK's runtime) --------------------------------------------------------------------------
namespace
{
// RW Object + 0x10C model condition bits by the binary's name (Construction::modelConditionIndex)
void setCondition(Object &obj, const char *name, bool on)
{
	obj.setModelConditionState(Construction::modelConditionIndex(name), on);
}

// RW 0x5E417A / 0x5E3B79 (clear a mask), 0x68D607 (clearAndSet)
void clearConditions(Object &obj, const std::vector<const char *> &names)
{
	Construction::setModelConditions(obj, names, {});
}

ActiveBody *activeBody(Object &obj)
{
	return dynamic_cast<ActiveBody *>(obj.getBodyModule());
}

// RW body vslot 0x14 (RW 0x8C1D75): health / max health left in the FPU (PC24), 0 when max health <= 0; the callers multiply by 100.0 in the FPU and store (RW 0x85807B)
float percentOfHealth(const BodyModuleInterface &body)
{
	const float maxHealth = body.getMaxHealth();
	const double fraction = maxHealth > 0.0f ? SimMath::pc24DivW((double)body.getHealth(), (double)maxHealth) : 0.0;
	return SimMath::fstpDword(SimMath::pc24MulW(fraction, 100.0));
}
} // namespace

GettingBuiltBehavior::GettingBuiltBehavior(Thing *thing, const GettingBuiltBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x857399 ends with 0x850C32(object, 1): the module runs from the next frame on (every structure with the module, built or not)
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

// RW 0x68C933(&damager, 4): the body took a damaging hit within the last 4 * LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5) frames: last + 20 >= now; never hit = 0xFFFFFFFF.
// RW asks the module at Object + 0x258 first (vslot 0x7C, then its slot 0x90): that module is not identified (stop S-650), the body's answer is used
bool GettingBuiltBehavior::recentlyDamaged() const
{
	const ActiveBody *body = activeBody(*getObject());
	if (!body)
	{
		return false;
	}
	const UnsignedInt last = body->lastDamageFrame();
	if (last == 0xFFFFFFFFu)
	{
		return false;
	}
	return getObject()->logic().getFrame() <= last + 4u * 5u;
}

// RW 0x8561C0: the object is a castle member (RW 0x797BEF) that records an occupant (+0x14) which stands (RW 0x79A09D), and the member's + 0x24 byte is 0 (the field is not
// identified: taken as 0, stop S-650)
bool GettingBuiltBehavior::castleBlocks() const
{
	const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(getObject()->findModule("CastleMemberBehavior"));
	return member && member->occupantId() != INVALID_ID && member->plotIsTaken();
}

void GettingBuiltBehavior::killBuilder(Object &builder)
{
	// RW 0x857270: the builder's model condition bit 252 (Object + 0x128 bit 28) is set first, then RW 0x698EC3: UNRESISTABLE, DEATH_FADED, amount = its max health, kill
	builder.setModelConditionState(252, true);
	DamageInfo info;
	info.m_input.m_sourceID = 0;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_FADED;
	info.m_input.m_amount = builder.getBodyModule() ? builder.getBodyModule()->getMaxHealth() : 0.0f;
	info.m_input.m_kill = true;
	builder.attemptDamage(info);
}

// RW 0x85730B (the first call of the destructor RW 0x85750D): the structure's builder (+ 0x7C, found with RW 0x449681) is killed with RW 0x698EC3(8 = UNRESISTABLE,
// 0x16 = FADED) when the structure spawned it (+ 0x3C, set by the spawn RW 0x857A19). Unlike RW 0x857270 no model condition bit is set first. Without it a
// NoSelect worker (GondorWorkerNoSelect, ...: DOZER, UNATTACKABLE) outlived its deleted structure, idle for good, and kept its player alive (S-420)
void GettingBuiltBehavior::killSpawnedWorkerOnDelete()
{
	Object *obj = getObject();
	AudioApi::stopHeldSound(AudioApi::HELD_BUILDING_LOOP, obj->getID()); // RW 0x857545 (lane AUDIO-3): the destructor RW 0x85750D removes the building loop (right after RW 0x85730B; the order touches no logic state)
	Object *builder = obj->logic().findObjectByID(obj->getBuilderID());
	if (!builder || !m_workerSpawned)
	{
		return;
	}
	DamageInfo info;
	info.m_input.m_sourceID = 0;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_FADED;
	info.m_input.m_amount = builder->getBodyModule() ? builder->getBodyModule()->getMaxHealth() : 0.0f;
	info.m_input.m_kill = true;
	builder->attemptDamage(info);
}

// RW 0x857238: true while a builder holds the structure. A builder that is not this structure's ally is killed; after completion (+0x30) an idle builder is released (a
// spawned worker fades away)
bool GettingBuiltBehavior::checkBuilder()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (!m_releaseBuilder && !castleBlocks())
	{
		Object *builder = logic.findObjectByID(obj->getBuilderID());
		if (!builder || builder->getRelationship(*obj) == ALLIES)
		{
			return false;
		}
		killBuilder(*builder);
		return true;
	}
	Object *builder = logic.findObjectByID(obj->getBuilderID());
	if (builder && obj->getBuilderID() != obj->getID())
	{
		AIUpdateInterface *ai = builder->getAIUpdateInterface();
		if (!ai || !ai->isIdle())
		{
			return true;
		}
		if (m_workerSpawned)
		{
			killBuilder(*builder);
		}
		obj->setBuilder(nullptr);
	}
	m_releaseBuilder = false;
	return true;
}

float GettingBuiltBehavior::rebuildCost(const Player &player) const
{
	// RW 0x85657E: (float)calcCostToBuild(template, owner, no producer, -1) * the percent of the body's damage state (data + 0x30 + state * 4), x87
	const Object *obj = getObject();
	const int cost = BuildAssistant::calcCostToBuild(*obj->getTemplate(), &player, nullptr, -1);
	const float percents[4] = { m_data->m_percentOfBuildCostToRebuildPristine, m_data->m_percentOfBuildCostToRebuildDamaged, m_data->m_percentOfBuildCostToRebuildReallyDamaged,
		m_data->m_percentOfBuildCostToRebuildRubble };
	const int state = obj->getBodyModule() ? (int)obj->getBodyModule()->getDamageState() : 0;
	return SimMath::fstpDword(SimMath::pc24MulW((double)cost, (double)percents[state < 0 || state > 3 ? 0 : state]));
}

namespace
{
// RW 0x8561F9 / 0x85623D: (int)ceil of the x87 price
int rebuildPrice(float cost)
{
	return (int)SimMath::ftol2Low32(SimMath::ceilD((double)cost));
}
} // namespace

bool GettingBuiltBehavior::canAffordRebuild(const Player &player) const
{
	// RW 0x8561EA: the money (player + 0x94) >= (unsigned)ceil(rebuildCost)
	const int price = rebuildPrice(rebuildCost(player));
	return (std::uint32_t)price <= player.getMoney()->countMoney();
}

void GettingBuiltBehavior::payRebuildCost(Player &player)
{
	// RW 0x856227: + 0x38 = ceil(rebuildCost); the money is withdrawn through the score keeper (RW 0x7B17EF)
	const int price = rebuildPrice(rebuildCost(player));
	m_pricePaid = price;
	player.withdrawMoney((std::uint32_t)price, true);
}

void GettingBuiltBehavior::startConstruction(bool force)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Player *player = obj->getControllingPlayer();
	if (!player)
	{
		return;
	}
	if (!force && !canAffordRebuild(*player))
	{
		return;
	}
	ActiveBody *body = activeBody(*obj);
	if (!body)
	{
		logic.reportError("GettingBuiltBehavior: '" + obj->getTemplate()->getName() + "' has no ActiveBody to build on [S-650]");
		return;
	}
	const float healthBefore = body->getHealth(); // RW 0x8566FB (body slot 0x10)
	m_constructing = true;
	if (!force)
	{
		payRebuildCost(*player);
	}
	if (m_buildFrames == 0)
	{
		// RW 0x856736: calcTimeToBuild(owner, no producer, -1)
		const int frames = BuildAssistant::calcTimeToBuild(*obj->getTemplate(), player, nullptr, -1, logic.productionSettings(), logic);
		m_buildFrames = (UnsignedInt)frames;
	}
	m_rebuilding = obj->isEffectivelyDead();
	if (m_rebuilding)
	{
		// RW 0x856762 ff: 0x79F0E1(object, 1), Object + 0x456 = 0 and the placement call RW 0x797465 are not identified (stop S-653)
		logic.noteStop("[S-653] GettingBuiltBehavior: a dead structure starts its rebuild (RW 0x8566DF): RW 0x79F0E1 / Object + 0x456 / RW 0x797465 are not ported");
	}
	if (!m_forceComplete)
	{
		if (m_rebuilding)
		{
			obj->setConstructionPercent(0.0f);
		}
		obj->setStatus(87, false); // PENDING_CONSTRUCTION
		if (m_completedOnce)
		{
			obj->setStatus(20, true); // UNDERGOING_REPAIR
		}
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	}
	else
	{
		obj->setConstructionPercent(-1.0f);
	}
	if (!m_rebuilding && !m_forceComplete)
	{
		obj->setConstructionPercent(percentOfHealth(*body));
	}
	else
	{
		obj->friend_setEffectivelyDead(false); // RW 0x68D950(0)
		m_completedOnce = false;
	}
	if (m_rebuilding || (!m_completedOnce && !m_forceComplete))
	{
		Construction::setModelConditions(*obj, { "AWAITING_CONSTRUCTION" }, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
	}
	m_healWeaponFired = false;
	setCondition(*obj, "RUBBLE", false);
	setCondition(*obj, "DYING", false);
	setCondition(*obj, "POST_COLLAPSE", false);
	obj->setBuilder(obj);
	m_workerSpawned = false;
	setWakeFrame(obj, UPDATE_SLEEP_NONE); // RW 0x850C32(object, 1)
	// RW 0x8568B3 .. 0x85694A (lane AUDIO-3): the loop kept at module + 0x24 is removed, then the data's SelfRepairFromRubbleLoop (data + 0x10) when rebuilding
	// (+0x35), else SelfRepairFromDamageLoop (+0xC) when completed once (+0x33), else SelfBuildingLoop (+0x8) is added for the object (RW 0x6DB5B3 with its id)
	// and its handle kept (field table RW 0xC56B38: the three rows, parser RW 0x73B217). Fire-and-forget: the handle lives on the audio side
	AudioApi::postHeldSound(AudioApi::HELD_BUILDING_LOOP, m_rebuilding ? m_data->m_selfRepairFromRubbleLoop : m_completedOnce ? m_data->m_selfRepairFromDamageLoop : m_data->m_selfBuildingLoop,
		obj->getID(), obj->getID());
	obj->updateUpgradeModules();         // RW 0x6936FE
	body->friend_setHealthRaw(healthBefore); // RW body slot 0xAC (RW 0x5015C6): the health stored back
	obj->friend_setEffectivelyDead(false);   // RW 0x68D950(0)
	m_checkCompletion = true;
}

void GettingBuiltBehavior::stopConstruction()
{
	Object *obj = getObject();
	if (!m_constructing)
	{
		return;
	}
	m_constructing = false;
	setCondition(*obj, "PARTIALLY_CONSTRUCTED", false);
	setCondition(*obj, "ACTIVELY_BEING_CONSTRUCTED", false);
	obj->setStatus(20, false); // UNDERGOING_REPAIR
	obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	AudioApi::stopHeldSound(AudioApi::HELD_BUILDING_LOOP, obj->getID()); // RW 0x85669B (lane AUDIO-3): the building loop is removed
}

void GettingBuiltBehavior::finishConstruction(Object *other)
{
	Object *obj = getObject();
	// RW 0x8569B1 ff: the production update's slot 0x60(0) and the module of RW 0x68C3A3 (slot 0x28) are told first: not identified (stop S-653)
	if (!m_constructing)
	{
		// RW 0x8569E9: a score / radar event (0x7379CB, 0xD) is the client's; without a WorkerName a builder `other` that this structure produced and that builds it fades away
		if (m_data->m_workerName.empty() && other && obj->getBuilderID() == other->getID() && other->getProducerID() == obj->getID() && other->getID() != obj->getID())
		{
			killBuilder(*other);
		}
		if (!m_data->m_workerName.empty())
		{
			return; // RW 0x856A14 .. 0x856A2C (lane AUDIO-3 r2): with a WorkerName the function leaves here; the loop removal and m_rebuilding = false (RW 0x856B30) are skipped
		}
		AudioApi::stopHeldSound(AudioApi::HELD_BUILDING_LOOP, obj->getID()); // RW 0x856A65 -> 0x856B11 (lane AUDIO-3)
	}
	else
	{
		m_constructing = false;
		obj->setConstructionPercent(-1.0f);
		obj->setStatus(20, false); // UNDERGOING_REPAIR
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
		setCondition(*obj, "AWAITING_CONSTRUCTION", false);
		clearConditions(*obj, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" });
		// INFERENCE (S-650): RW 0x856AEE calls onBuildComplete (RW 0x6902F5 -> 0x68D252); the player notification that counts the command points of a self-built
		// structure (RW 0x6AA72B in the dozer path) was not located here: Construction::completeConstruction's bookkeeping stands for both
		if (Player *owner = obj->getControllingPlayer())
		{
			Construction::onStructureConstructionComplete(*owner, obj, *obj, false);
		}
		obj->friend_onBuildComplete();
		// slot 2 RW 0x8574AE: the next self-build / repair lasts ftol(max(1.0, 5 * RebuildTimeSeconds)) frames (SSE product)
		const float frames = SimMath::mulf32(SimMath::sseFromInt32(5), m_data->m_rebuildTimeSeconds);
		m_buildFrames = (UnsignedInt)SimMath::ftol2Low32((double)(1.0f > frames ? 1.0f : frames));
		if (obj->getBuilderID() == obj->getID())
		{
			obj->setBuilder(nullptr);
			m_workerSpawned = false;
		}
		AudioApi::stopHeldSound(AudioApi::HELD_BUILDING_LOOP, obj->getID()); // RW 0x856B11 (lane AUDIO-3): the building loop is removed
	}
	m_rebuilding = false; // RW 0x856B30
}

void GettingBuiltBehavior::spawnWorkerOrStart(bool force)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_data->m_workerName.empty())
	{
		startConstruction(force);
		return;
	}
	Player *player = obj->getControllingPlayer();
	if (!player || !player->getPlayerTemplate() || !player->getPlayerTemplate()->m_playableSide)
	{
		return; // RW 0x857A65: the owner's template must be a PlayableSide (RW 0x6AAC66: template + 0x151)
	}
	const std::string &name = m_data->m_testFaction && player->getPlayerTemplate()->m_evil ? m_data->m_evilWorkerName : m_data->m_workerName;
	const ThingTemplate *tt = logic.things().findTemplate(name);
	if (!tt)
	{
		logic.reportError("GettingBuiltBehavior: the worker '" + name + "' of '" + obj->getTemplate()->getName() + "' is not a template (retail spawns nothing)");
		return;
	}
	if (!player->getDefaultTeam())
	{
		return;
	}
	Object *worker = logic.newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
	if (!worker)
	{
		return;
	}
	Coord3D pos = *obj->getPosition();
	worker->setPosition(&pos); // RW 0x857AE2
	worker->setProducer(obj);
	obj->setBuilder(worker);
	m_workerSpawned = true;
	if (obj->isEffectivelyDead())
	{
		// RW 0x857B0B ff: the dead structure comes back as a foundation
		obj->friend_setEffectivelyDead(false);
		obj->setConstructionPercent(0.0f);
		obj->setStatus(87, false);
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
		setCondition(*obj, "RUBBLE", false);
		setCondition(*obj, "DYING", false);
		setCondition(*obj, "POST_COLLAPSE", false);
		setCondition(*obj, "ACTIVELY_BEING_CONSTRUCTED", true);
	}
	if (DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(worker->getAIUpdateInterface()))
	{
		// RW 0x857B9B: under construction -> resume construction (RW 0x771526), else repair (RW 0x7714C1), CMD_FROM_AI
		if (obj->isUnderConstruction())
		{
			dozer->resumeConstruction(*obj);
		}
		else
		{
			dozer->repair(*obj);
		}
	}
	else if (worker->getAIUpdateInterface())
	{
		logic.reportError("GettingBuiltBehavior: the worker '" + name + "' has no dozer AI in this port [S-650]");
	}
	m_checkCompletion = true;
}

// RW 0x857BDA
void GettingBuiltBehavior::checkCompletion()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	BodyModuleInterface *body = obj->getBodyModule();
	if (!body)
	{
		return;
	}
	const bool forced = m_forceComplete; // slot 11
	Object *builder = logic.findObjectByID(obj->getBuilderID());
	if (m_checkCompletion && (forced || !(body->getHealth() < body->getMaxHealth())))
	{
		const float percent = obj->getConstructionPercent();
		if (percent >= 100.0f || percent == -1.0f)
		{
			if (!builder)
			{
				m_workerSpawned = false;
			}
			else if (builder->getAIUpdateInterface() && builder != obj && builder->isKindOfName("DOZER"))
			{
				if (!m_workerSpawned)
				{
					obj->setBuilder(nullptr);
					m_workerSpawned = false;
				}
				else
				{
					builder->getAIUpdateInterface()->aiIdle(CMD_FROM_AI); // RW 0x857C9E (RW 0x66831A before it is a debug log)
					m_releaseBuilder = true;
				}
			}
			if (!forced || !obj->isKindOfName("COMMANDCENTER"))
			{
				setCondition(*obj, "DAMAGED", false);
				setCondition(*obj, "REALLYDAMAGED", false);
			}
			setCondition(*obj, "AWAITING_CONSTRUCTION", false);
			setCondition(*obj, "ACTIVELY_BEING_CONSTRUCTED", false);
			setCondition(*obj, "PARTIALLY_CONSTRUCTED", false);
			obj->setStatus(20, false);
			obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
			if (m_constructing)
			{
				finishConstruction(nullptr);
			}
			// RW 0x857D6C .. 0x857DBA: the first completion (module byte +0x33 = m_completedOnce) from logic frame 5 on sends the structure's VoiceFullyCreated
			// (voice event 0x7E2: its EVA id is the "building complete" announcement) to the client voice picker: fire-and-forget audio (lane AUDIO-2)
			if (!m_completedOnce && obj->logic().getFrame() >= 5)
			{
				AudioApi::postUnitVoice(0x7E2, obj->getID());
			}
			m_spawnTimer = m_data->m_spawnTimer;
			m_completedOnce = true;
			m_forceComplete = false;
			m_checkCompletion = false;
			return;
		}
	}
	// RW 0x857DEA: an idle builder of a structure never completed is sent back to it (RW 0x5E6810 on its locomotor, and a model condition when it is more than 1.5
	// bounding radii away): not ported (stop S-650); the port's dozer keeps its task
}

// RW 0x857818
void GettingBuiltBehavior::checkRestart()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	// RW 0x856E1F / 0x856E5C: the wall hub's segment list (+0x40) and its flag (+0x3E) are WallHubBehavior's (S-307)
	BodyModuleInterface *body = obj->getBodyModule();
	if (!body)
	{
		return;
	}
	if (obj->isEffectivelyDead())
	{
		if (!m_data->m_rebuildWhenDead)
		{
			return;
		}
		// RW 0x857873: the partition's objects within DisallowRebuildRange of the position (mode 1: centre distance in 3D) that the DisallowRebuildFilter allows for this
		// object's owner (wrapper RW 0xBE4CC8, flag 1); one that is alive and not this object blocks the rebuild. The port walks the object list (the answer does not depend
		// on the order). A template without the row has no filter here: nothing blocks (inference: RW's default filter at data + 0x40 was not read, S-654)
		if (m_data->m_disallowRebuildFilter)
		{
			const Coord3D &p = *obj->getPosition();
			const float range = m_data->m_disallowRebuildRange;
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o == obj || o->isEffectivelyDead() || o->isDestroyed() || !o->isInWorld())
				{
					continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
				}
				const float d = (float)SimMath::length3d(SimMath::subf32(o->getPosition()->x, p.x), SimMath::subf32(o->getPosition()->y, p.y), SimMath::subf32(o->getPosition()->z, p.z));
				if (d <= range && ObjectFilterMatch::allows(logic, *m_data->m_disallowRebuildFilter, *o, obj->getControllingPlayer()))
				{
					return;
				}
			}
		}
	}
	if (!(body->getHealth() < body->getMaxHealth()))
	{
		return;
	}
	if (castleBlocks())
	{
		return;
	}
	const bool recently = !m_rebuilding && recentlyDamaged();
	Object *builder = logic.findObjectByID(obj->getBuilderID());
	const bool builderIsHub = builder && builder->isKindOfName("WALL_HUB");
	if (m_data->m_workerName.empty() && (!builder || builder == obj || builderIsHub))
	{
		if (recently || m_constructing)
		{
			return;
		}
		if (!m_data->m_useSpawnTimerWithoutWorker)
		{
			if (!(0.0f < m_spawnTimer) && m_spawnTimer != 0.0f)
			{
				return;
			}
		}
		else
		{
			if (0.0f < m_spawnTimer)
			{
				m_spawnTimer = SimMath::subf32(m_spawnTimer, 1.0f);
			}
			if (!(m_spawnTimer < 0.0f) && m_spawnTimer != 0.0f)
			{
				return;
			}
		}
	}
	else
	{
		if (builder && !builder->isEffectivelyDead())
		{
			return;
		}
		if (0.0f < m_spawnTimer && !recently)
		{
			m_spawnTimer = SimMath::subf32(m_spawnTimer, 1.0f);
		}
		if (0.0f < m_spawnTimer)
		{
			return;
		}
	}
	spawnWorkerOrStart(true); // slot 1 (RW 0x857A04)
}

UpdateSleepTime GettingBuiltBehavior::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const UpdateSleepTime oneSecond = UPDATE_SLEEP(5); // RW 0xD9F608 = LOGICFRAMES_PER_SECOND
	const bool dead = obj->isEffectivelyDead();
	if (m_wasDead != dead && m_data->m_rebuildWhenDead)
	{
		m_wasDead = dead;
		m_spawnTimer = m_data->m_spawnTimer;
		if (dead)
		{
			m_releaseBuilder = true;
		}
	}
	const bool recently = !m_rebuilding && recentlyDamaged();
	const bool builderBusy = checkBuilder();
	const bool constructing = m_constructing;
	bool otherBuilder = false;
	if (obj->getBuilderID() != obj->getID() && !m_workerSpawned)
	{
		const Object *b = logic.findObjectByID(obj->getBuilderID());
		otherBuilder = b && !b->isKindOfName("WALL_HUB");
	}
	if (dead || (m_data->m_spawnTimer >= 0.0f && recently && !m_forceComplete))
	{
		stopConstruction(); // RW 0x8580AC (slot 5)
		checkCompletion();
		checkRestart();
		return oneSecond;
	}
	if (!constructing || otherBuilder || castleBlocks())
	{
		if (builderBusy)
		{
			return UPDATE_SLEEP_NONE;
		}
		checkCompletion();
		checkRestart();
		return oneSecond;
	}
	if (!m_forceComplete && builderBusy)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (!m_healWeaponFired && obj->getConstructionPercent() >= 75.0f)
	{
		m_healWeaponFired = true;
		if (!m_data->m_healWeapon.empty())
		{
			// RW 0x857FAA: TheWeaponStore->createAndFireTempWeapon(HealWeapon, object, object position) (RW 0x6CF530, ObjectWeapons::createAndFireTempWeapon since
			// lane DECOMP-1). No retail template has a HealWeapon. A name the store lacks is an error (PLAN rule 10).
			const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_data->m_healWeapon) : nullptr;
			if (!wt)
			{
				logic.reportError("GettingBuiltBehavior: the HealWeapon '" + m_data->m_healWeapon + "' of '" + obj->getTemplate()->getName() + "' is not a weapon");
			}
			else
			{
				const Coord3D at = *obj->getPosition();
				ObjectWeapons::createAndFireTempWeapon(wt, obj, at); // RW 0x857FAA -> RW 0x6CF530 (lane DECOMP-1: the full temporary weapon)
				++m_healWeaponShots;
			}
		}
	}
	if (BodyModuleInterface *body = obj->getBodyModule())
	{
		// RW 0x857FBD: max health / (float)(unsigned)buildFrames, x87 PC24 (one rounding: the float quotient)
		const float amount = SimMath::fstpDword(SimMath::pc24DivW((double)body->getMaxHealth(), SimMath::fildU32(m_buildFrames)));
		if (obj->isKindOfName("WALL_SEGMENT"))
		{
			// RW 0x857FD6: a wall segment re-registers its pathfinder footprint when this heal takes its health across 20 % of the max (PC24 products / sums)
			const float threshold = SimMath::fstpDword(SimMath::pc24MulW((double)body->getMaxHealth(), (double)0.2f));
			const float h = body->getHealth();
			if (threshold <= h && (double)h < SimMath::pc24AddW((double)threshold, (double)amount))
			{
				if (AIWorld *ai = logic.aiWorld())
				{
					ai->removeObjectFromPathfindMap(*obj); // RW 0x6E85FB
					ai->addObjectToPathfindMap(*obj);      // RW 0x6E85E9
				}
				// RW 0x85805E: BuildAssistant RW 0x797465(template, position, angle, owner) is not identified (stop S-653)
				logic.noteStop("[S-653] GettingBuiltBehavior: a wall segment crossed 20 % health: BuildAssistant RW 0x797465 is not ported");
			}
		}
		obj->attemptHealingFromSoleBenefactor(amount, obj, 2); // RW 0x85806F
		obj->setConstructionPercent(percentOfHealth(*body)); // RW 0x858074..0x858081
		// RW 0x85808D: unless rebuilding, the body's slot 0x94 (RW 0x8C1D53: RW 0x6903B6(object, damage state, + 0x24) then slot 0x2C) runs: not identified (stop S-653)
	}
	checkCompletion();
	return UPDATE_SLEEP_NONE;
}

void GettingBuiltBehavior::runNowThenSleep(UnsignedInt frames)
{
	update(); // RW 0x8574F0: the module's update interface slot 0 (its return value is not used)
	setWakeFrame(getObject(), (UpdateSleepTime)frames); // RW 0x850C32(object, frames): now + frames (0: this frame)
}

void GettingBuiltBehavior::addLinkedPiece(const Object &piece)
{
	for (const LinkedPiece &l : m_linked)
	{
		if (l.id == piece.getID())
		{
			return;
		}
	}
	m_linked.push_back(LinkedPiece{ piece.getID(), *piece.getPosition(), piece.getOrientation() });
}

void GettingBuiltBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addFloat(m_spawnTimer);
	h.addU32(m_buildFrames);
	h.addBool(m_releaseBuilder);
	h.addBool(m_wasDead);
	h.addBool(m_constructing);
	h.addBool(m_completedOnce);
	h.addBool(m_healWeaponFired);
	h.addBool(m_rebuilding);
	h.addBool(m_forceComplete);
	h.addI32(m_pricePaid);
	h.addBool(m_workerSpawned);
	h.addBool(m_checkCompletion);
	h.addU32(m_healWeaponShots);
	h.addU32((std::uint32_t)m_linked.size());
	for (const LinkedPiece &l : m_linked)
	{
		h.addU32(l.id);
		h.addFloat(l.pos.x);
		h.addFloat(l.pos.y);
		h.addFloat(l.pos.z);
		h.addFloat(l.angle);
	}
}

// ---- BuildingBehavior ---------------------------------------------------------------------------------------------------------------
BuildingBehavior::BuildingBehavior(Thing *thing, const BuildingBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
}

void ConstructionModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<FoundationAIUpdate, FoundationAIUpdateModuleData>(modules, "FoundationAIUpdate");
	bindRuntime<GettingBuiltBehavior, GettingBuiltBehaviorModuleData>(modules, "GettingBuiltBehavior");
	bindRuntime<BuildingBehavior, BuildingBehaviorModuleData>(modules, "BuildingBehavior");
}
