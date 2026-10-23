// OpenBFME. GPL-3.0.
// See GameLogic/Object/Object.h for the creation order and its sources.

#include "GameLogic/Object/Object.h"
#include "GameLogic/LegendaryHeroesGame.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameClient/MapChunks.h"

#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "Common/PlayerList.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/MpGameRules.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/NumericState.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AutoDepositUpdate.h"
#include "GameLogic/Module/ObjectHelper.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/SMCHelper.h"
#include "GameLogic/Module/WeaponStatusHelper.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Module/HeroAbilityModules.h"

#include <cstring>
#include <variant>
#include <stdexcept>

namespace
{
// RW 0x69A0A9: the DefectionHelper is skipped for these KindOfs (tt+0x108 & 0x40 / tt+0x114 & 2 / tt+0x119 & 1): the bit numbers are the
// binary's, resolved by name so a mod's own KindOf order cannot break them
bool kindOfByName(const Object &o, const char *name)
{
	return o.isKindOfName(name);
}
} // namespace

Object::Object(GameLogic &logic, const ThingTemplate *tt, const ObjectStatusMaskType &status, Team *team, ObjectID id)
	: Thing(tt ? tt->getFinalOverride() : nullptr)
	, m_logic(logic)
{
	if (!m_template)
	{
		throw std::logic_error("Object: no template");
	}
	m_status = status;
	m_creationFrame = logic.getFrame(); // RW 0x699D4B .. 0x699D65
	if (const FieldValue *v = m_template->findField("Scale"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			m_instanceScale = *f; // RENDER-2: the drawable's instance scale starts as the template's Scale (RW 0x679FD7, default 1.0, RW 0x74008C)
		}
	}
	const ObjectTemplateInfo &info = logic.templateInfo(m_template);
	m_kindOf = info.kindOf;
	m_scriptSelectable = isKindOfName("SELECTABLE"); // RW 0x69A648..0x69A652: template KindOf bit 1 initializes Object +0x454
	m_id = id != INVALID_ID ? id : logic.allocateObjectID(); // RW 0x699E4A .. 0x699E5D
	if (id != INVALID_ID)
	{
		if (logic.findObjectByID(id))
		{
			throw std::logic_error("Object: object id " + std::to_string(id) + " is already in use"); // before anything is linked
		}
		logic.noteExplicitObjectID(id);
	}
	if (!team)
	{
		Player *neutral = logic.players().getNeutralPlayer();
		team = neutral ? neutral->getDefaultTeam() : nullptr; // RW 0x699E4A: the neutral player's default team
		if (!team)
		{
			throw std::logic_error("Object: no team given and the neutral player has no default team");
		}
	}
	m_producerID = logic.takePendingProducer(); // lane PROD-1 (GameLogic::setPendingProducer)
	logic.friend_addObjectToLookup(this); // RW 0x68BC01 setID: findable by id from now on
	joinTeam(team);
	recordOriginalTeam(); // RW 0x69954A records the team's name (+ 0x320, RW 0x69959D; lane HERO-2 / BACKLOG4)
	try
	{
		buildModules();
		applyTeamAttitude(); // RW 0x69A550
		m_experienceTracker = std::make_unique<ExperienceTracker>(this); // RW 0x69A606 (lane XP-1)
		// RW 0x69A637: onObjectCreated of every module, in array order
		for (std::unique_ptr<BehaviorModule> &m : m_modules)
		{
			m->onObjectCreated();
		}
		// RW 0x69A66D: the radar (Radar::addObject RW 0x6D9042): client state; RADAR-1's radar applies its rules to the live objects at every draw (S-142)
		logic.registerObject(this); // RW 0x69A673 .. 0x69A67A
		logic.friend_objectEnteredWorld(*this); // RW 0x69A6C6: the radar / script world / partition entry of the Object (a seam, S-142)
	}
	catch (...)
	{
		// a constructor that throws is not destroyed: leave nothing behind that points at this object
		m_team->friend_removeMember(this);
		m_team = nullptr;
		logic.friend_removeObjectFromLookup(this);
		throw;
	}
	// RW 0x69A684: the construction-time stamp of RW + 0x444 (frame + tt + 0x564) is not ported (S-142)
	m_constructed = true;
}

Object::~Object()
{
	// RW 0x69A89D: the object leaves the world (the seam of S-142); ZH Object::~Object: sendObjectDestroyed (the drawable goes), then the team, then
	// the modules (deleted in array order)
	m_logic.friend_objectLeftWorld(*this);
	m_logic.emotions().objectDied(*this); // RW 0x69A808 .. 0x69A821 (lane MODULES-2)
	if (GameLogic *l = &m_logic)
	{
		if (ObjectClientHooks *hooks = l->clientHooks())
		{
			hooks->objectDestroyed(*this);
		}
	}
	if (m_team)
	{
		m_team->friend_removeMember(this);
		m_team = nullptr;
	}
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		m.reset();
	}
}

// RW 0x68D607 / 0x68B53C: the flags change, the drawable is told which bits changed (the drawable keeps bits the object never set, e.g. NIGHT)
void Object::clearAndSetModelConditionFlags(const ModelConditionBits &clear, const ModelConditionBits &set)
{
	const ModelConditionBits before = m_modelCondition;
	for (size_t w = 0; w < m_modelCondition.size(); ++w)
	{
		m_modelCondition[w] &= ~clear[w];
		m_modelCondition[w] |= set[w];
	}
	if (m_client && m_modelCondition != before)
	{
		for (size_t w = 0; w < m_modelCondition.size(); ++w)
		{
			std::uint32_t diff = before[w] ^ m_modelCondition[w];
			for (int b = 0; diff; ++b, diff >>= 1)
			{
				if (diff & 1u)
				{
					const int bit = (int)w * 32 + b;
					m_client->modelConditionChanged(*this, bit, testModelCondition(bit)); // SMOOTH-1: an ordered client event
				}
			}
		}
	}
}

void Object::flushDrawableModelConditions()
{
	if (m_client)
	{
		m_client->flushModelConditions(*this);
	}
}

void Object::setModelConditionState(int bit, bool on)
{
	if (bit < 0 || bit >= 19 * 32)
	{
		return;
	}
	ModelConditionBits change{};
	change[(size_t)bit >> 5] = 1u << (bit & 31);
	const ModelConditionBits none{};
	clearAndSetModelConditionFlags(on ? none : change, on ? change : none);
}

void Object::setSpecialModelConditionState(int bit, UnsignedInt frames)
{
	if (SMCHelper *smc = static_cast<SMCHelper *>(findModule("SMCHelper")))
	{
		smc->setSpecialModelConditionState(bit, frames);
	}
}

std::string Object::getCommandSetName() const
{
	if (!m_commandSetOverride.empty())
	{
		return m_commandSetOverride;
	}
	if (const FieldValue *v = m_template->findField("CommandSet"))
	{
		if (const std::string *name = std::get_if<std::string>(v))
		{
			return *name;
		}
	}
	return std::string();
}

bool Object::isKindOfName(const char *name) const
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && isKindOf((unsigned)bit);
}

std::unique_ptr<BehaviorModule> Object::friend_replaceModule(BehaviorModule *old, std::unique_ptr<BehaviorModule> replacement)
{
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (m.get() == old)
		{
			std::unique_ptr<BehaviorModule> out = std::move(m);
			m = std::move(replacement);
			if (m_contain == out->getContain())
			{
				m_contain = m->getContain();
			}
			rebuildCollideModules();
			return out;
		}
	}
	throw std::logic_error("Object::friend_replaceModule: the module is not this object's");
}

void Object::rebuildCollideModules()
{
	m_collideModules.clear();
	m_collideModulesKnown = true;
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (CollideModuleInterface *c = m ? m->getCollide() : nullptr)
		{
			m_collideModules.push_back(c);
			m_collideModulesKnown = m_collideModulesKnown && c->pairFactsKnown();
		}
	}
	m_attributePool = findModule("AttributeModifierPoolUpdate"); // lane UBER-3W: attributeModifierPoolModule's answer from here on
	m_moduleCachesBuilt = true;
}

void Object::buildModules()
{
	const ObjectTemplateInfo &info = m_logic.templateInfo(m_template);
	// ---- the helpers (RW 0x69A5C0 - 0x69A32F), in the retail order -------------------------------------------------------
	auto helper = [&](const char *helperName, const char *tag) {
		m_modules.push_back(std::make_unique<ObjectHelperShell>(this, helperName, tag));
		++m_helperCount;
	};
	// PROD-1: the SMCHelper acts (timed model conditions); the other helpers are LOGIC-1's shells (S-141)
	m_modules.push_back(std::make_unique<SMCHelper>(this));
	++m_helperCount;
	helper("RecoveryHelper", "ModuleTag_RecoveryHelper");
	if (m_logic.settings().enableRepulsors && kindOfByName(*this, "CAN_BE_REPULSED"))
	{
		helper("RepulsorHelper", "ModuleTag_RepulsorHelper");
	}
	if (!kindOfByName(*this, "SHRUBBERY") && !kindOfByName(*this, "ROCK") && !kindOfByName(*this, "ROCK_VENDOR"))
	{
		helper("DefectionHelper", "ModuleTag_DefectionHelper");
	}
	helper("GuardingHelper", "ModuleTag_GuardingHelper");
	if (info.canPossiblyHaveAnyWeapon)
	{
		// PROJ-2: the WeaponStatusHelper acts (the weapon model conditions, PHASE_FINAL, GameLogic/Module/WeaponStatusHelper.h)
		m_modules.push_back(std::make_unique<WeaponStatusHelper>(this));
		++m_helperCount;
		helper("FiringTrackerHelper", "ModuleTag_FiringTrackerHelper");
	}
	// ---- the behaviors, in template list order (RW 0x69A32F - 0x69A4AC) -----------------------------------------------------
	ModuleFactory &factory = m_logic.modules();
	for (const ThingTemplate::Nugget &n : m_template->behaviorModules().nuggets())
	{
		std::unique_ptr<Module> mod = factory.newModule(this, n.name, n.data.get(), MODULETYPE_BEHAVIOR);
		BehaviorModule *bm = dynamic_cast<BehaviorModule *>(mod.get());
		if (!bm)
		{
			throw std::logic_error("Object: behavior class '" + n.name + "' did not make a BehaviorModule");
		}
		mod.release();
		m_modules.push_back(std::unique_ptr<BehaviorModule>(bm));
		// RW 0x69A3B3 .. 0x69A3F0: cache the body (vslot 0), the contain (vslot 2) and the AI interface (vslot 19). Each is stored when the module has one and a LATER module
		// overwrites it (RW 0x69A3C8 / 0x69A3D9 / 0x69A3EA store without a test; ZH only asserts "Duplicate body / contain / AI"): a template that declares two bodies
		// (retail's ArnorArvedui: an ActiveBody and then a RespawnBody) runs with the LAST one as its body
		if (BodyModuleInterface *b = bm->getBody())
		{
			m_body = b;
		}
		if (ContainModuleInterface *c = bm->getContain())
		{
			m_contain = c;
		}
		if (AIUpdateInterface *a = bm->getAIUpdateInterface())
		{
			m_ai = a;
		}
	}
	rebuildCollideModules();
	// COMBAT-2: the finished object's body applies its initial damage state once (an INI InitialHealth below the thresholds shows DAMAGED / REALLYDAMAGED / RUBBLE from the start)
	if (m_body)
	{
		m_body->applyInitialDamageState();
	}
	// ZH Object::Object (Object.cpp:593, after the modules): the weapon set of an object that can have a weapon (RW 0x73C191)
	if (info.canPossiblyHaveAnyWeapon)
	{
		m_weapons = std::make_unique<ObjectWeapons>(*this);
	}
}

void Object::reactToTransformChange(const Coord3D *oldPos, float)
{
	if (m_ai)
	{
		m_ai->reactToTransformChange(); // ZH Object::reactToTransformChange: the pathfinder's position cell of the unit (MOVE-1)
	}
	if (m_contain)
	{
		m_contain->containReactToTransformChange();
	}
	if (ShroudManager *shroud = m_logic.shroud())
	{
		shroud->markDirty(*this, false); // RW 0x68B862 -> 0xB4E290 (VIS-1): the shroud record re-covers and re-looks in the next shroud update
	}
	m_logic.partition().markDirty(*this); // RW 0x68B862 / 0x68B244 -> 0xA39570 (lane MODULES-2): re-linked in the next partition update
	if (oldPos && (oldPos->x != getPosition()->x || oldPos->y != getPosition()->y || oldPos->z != getPosition()->z))
	{
		updateTriggerAreaFlags(); // RW 0x69355D: only when the position changed (RW 0x68B777)
	}
}

// lane SCRIPT-2: RW 0x69264D. The exit test uses the PREVIOUS integer position (+ 0x418 is written after the exit loop): an object that leaves a
// trigger is reported exited at its next integer-cell move outside, as retail does. Not ported here: the Lua OnUnitEntered / OnUnitExited events
// (RW 0x7379CB, LUA-1's stop) (lane HUD-5: the trigger's callback, RW 0x6E4B21 / 0x6E4B3E, runs), the team's entered flag (RW 0x79FE5A: team + 0x5C and the
// script engine's + 0x1A260 frame) and the terrain decal call for IMMOBILE-less infantry / cavalry / monsters / machines (RW 0x6858BE)
void Object::updateTriggerAreaFlags()
{
	static const int projectile = ObjectTemplateInfoBuilder::kindOfIndex("PROJECTILE");
	static const int inert = ObjectTemplateInfoBuilder::kindOfIndex("INERT");
	if ((projectile >= 0 && isKindOf((unsigned)projectile)) || (inert >= 0 && isKindOf((unsigned)inert)))
	{
		return;
	}
	const std::int32_t x = SimMath::cvttss2si(getPosition()->x);
	const std::int32_t y = SimMath::cvttss2si(getPosition()->y);
	if (x == m_triggerCellX && y == m_triggerCellY)
	{
		return;
	}
	const UnsignedInt frame = m_logic.getFrame();
	if (m_enteredOrExitedFrame != 0 && m_enteredOrExitedFrame != frame)
	{
		// RW 0x68B98E: keep the entries still inside, with their entered / exited flags cleared
		int kept = 0;
		for (int i = 0; i < m_triggerCount; ++i)
		{
			if (m_triggers[i].inside)
			{
				m_triggers[kept].trigger = m_triggers[i].trigger;
				m_triggers[kept].entered = false;
				m_triggers[kept].exited = false;
				m_triggers[kept].inside = true;
				++kept;
			}
		}
		m_triggerCount = kept;
	}
	// lane HUD-5: the map's triggers and the run-time ones (AIGateUpdate) in one list; a trigger's callback runs right after its entry changed (RW 0x6928B4 / 0x6929D7)
	ScriptEngine &engine = m_logic.scriptEngine();
	const size_t areaCount = engine.triggerAreaCount();
	const auto inArea = [](const TriggerArea &t, std::int32_t px, std::int32_t py) { // RW 0x6E4DC7 -> 0x68628B: the integers as floats
		return ScriptConditions::pointInTrigger(t, SimMath::sseFromInt32(px), SimMath::sseFromInt32(py));
	};
	for (int i = 0; i < m_triggerCount; ++i)
	{
		TriggerEntry &e = m_triggers[i];
		if (e.trigger >= 0 && (size_t)e.trigger < areaCount && !inArea(engine.triggerAreaAt((size_t)e.trigger), m_triggerCellX, m_triggerCellY))
		{
			e.inside = false;
			e.exited = true;
			m_enteredOrExitedFrame = frame;
			engine.triggerCallback((size_t)e.trigger, *this, false);
		}
	}
	m_triggerCellX = x;
	m_triggerCellY = y;
	for (size_t t = 0; t < areaCount; ++t)
	{
		bool known = false;
		for (int i = 0; i < m_triggerCount; ++i)
		{
			known = known || m_triggers[i].trigger == (int)t;
		}
		if (known || !inArea(engine.triggerAreaAt(t), x, y))
		{
			continue;
		}
		if (m_triggerCount < MAX_TRIGGERS_IN)
		{
			TriggerEntry &e = m_triggers[m_triggerCount++];
			e.trigger = (int)t;
			e.inside = true;
			e.entered = true;
			e.exited = false;
			m_enteredOrExitedFrame = frame;
			engine.triggerCallback(t, *this, true);
		}
		else
		{
			engine.note("***WARNING: Too many nested triggers"); // RW 0x6929EA, once per game run (RW 0xDE478C)
		}
	}
}

// lane HUD-5: RW 0xAD3520 (see Object.h)
void Object::setGeometryActive(const std::string &name, bool on)
{
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*getTemplate());
	if (m_geometryActive.size() != shapes.size())
	{
		m_geometryActive.clear();
		for (const ObjectGeometry::Shape &sh : shapes)
		{
			m_geometryActive.push_back(sh.active ? 1 : 0);
		}
	}
	bool changed = false;
	for (size_t i = 0; i < shapes.size(); ++i)
	{
		if (shapes[i].name == name && m_geometryActive[i] != (on ? 1 : 0))
		{
			m_geometryActive[i] = on ? 1 : 0;
			changed = true;
		}
	}
	if (changed)
	{
		++m_geometryVersion;
	}
}

void Object::joinTeam(Team *team)
{
	if (team == m_team)
	{
		return;
	}
	// lane DECOMP-3: RW 0x697C9A .. 0x697CBC: between two teams whose controlling players differ (RW 0x79FD6F) the skirmish AI manager's object events run
	// around the + 0x31C store, the removal RW 0x6A99CB with the old owner and the add RW 0x6A9D9C with the new one
	const bool ownerEvent = m_team && team && m_team->getControllingPlayer() != team->getControllingPlayer();
	if (m_team)
	{
		m_team->friend_removeMember(this);
	}
	if (ownerEvent)
	{
		m_logic.skirmishAI().objectRemoved(*this);
	}
	m_team = team;
	if (ownerEvent)
	{
		m_logic.skirmishAI().objectAdded(*this);
	}
	if (m_team)
	{
		m_team->friend_addMember(this);
		applyTeamAttitude(); // RW 0x697D1A
	}
}

// lane SCRIPT-3: RW 0x69A550 (the constructor, after the modules) / RW 0x697D1A (the team change): an object with an AI takes its team prototype's
// initial attitude (prototype + 0x228: the map team's teamAggressiveness, ZH Team.cpp:715, AI_NORMAL 0 when absent)
void Object::applyTeamAttitude()
{
	AIUpdateInterface *ai = getAIUpdateInterface();
	if (!ai || !m_team || !m_team->getPrototype())
	{
		return;
	}
	ai->setAttitude(m_team->getPrototype()->getDict().getInt("teamAggressiveness"));
}

// lane BACKLOG4: RW 0x69957E .. 0x6995D2: + 0x320 = the team's prototype's owner name (+ 0x10), '/', the prototype's name (+ 0x14); cleared with no team
void Object::recordOriginalTeam()
{
	m_originalTeamProto = m_team ? m_team->getPrototype() : nullptr;
	m_originalTeamID = m_team ? m_team->getID() : 0;
}

std::string Object::getOriginalTeamName() const
{
	if (!m_originalTeamProto)
	{
		return std::string();
	}
	const Player *owner = m_originalTeamProto->getControllingPlayer();
	return (owner ? owner->getPlayerName() : std::string()) + "/" + m_originalTeamProto->getName();
}

Team *Object::getOriginalTeam() const
{
	const std::string name = getOriginalTeamName();
	return name.empty() ? nullptr : m_logic.players().teams().findTeam(name);
}

void Object::setTeam(Team *team)
{
	// lane BACKLOG4: RW 0x699554 .. 0x699570 (BFME2 decomp ObjectSetTeam.cpp Object::setTeam, tier B same-shape: "to prevent spawning useful units for a
	// player after he dies"): the team of a player who is not active (RW 0x6AAC52: an observer, Player + 0x35A, or defeated, + 0x754) is replaced by the
	// neutral player's default team (ThePlayerList + 0x18 -> + 0x30C). Retail reads the team's player without a test; a team without one is kept here
	if (team)
	{
		const Player *p = team->getControllingPlayer();
		if (p && (p->isObserver() || p->isDefeated()))
		{
			if (Player *neutral = m_logic.players().getNeutralPlayer())
			{
				team = neutral->getDefaultTeam();
			}
		}
	}
	Player *oldOwner = getControllingPlayer();
	joinTeam(team);
	recordOriginalTeam(); // RW 0x69959D (lane HERO-2 / BACKLOG4): + 0x320 = the team's name
	if (ShroudManager *shroud = m_logic.shroud())
	{
		shroud->markDirty(*this, true); // RW 0x69954A -> 0xB4E2A0 (VIS-1): a new owner looks for its own allies
	}
	m_logic.partition().markDirty(*this); // RW 0x69954A -> 0xA39570 (lane MODULES-2): the partition layer follows the owner in the next partition update
	if (m_constructed)
	{
		Player *newOwner = getControllingPlayer();
		if (oldOwner != newOwner)
		{
			onOwnerChanged(oldOwner, newOwner);
			if (m_client && oldOwner && newOwner)
			{
				m_client->observerEvent(*this, ObserverEvent::Capture);
			}
		}
		// lane FACT-B (FACT-C's bdf99d54 finding): RW 0x69954A Object::setTeam (tier B, BFME2 decomp ObjectSetTeam.cpp) ends, for a non-null team,
		// with updateUpgradeModules (RW 0x699658 -> RW 0x6936FE): the upgrade modules run against the new owner's upgrades, so a captured building takes
		// its new owner's faction command set (CommandSetUpgrade TriggeredBy Upgrade_<Side>Faction). INFERENCE: RW makes the call for any object; an
		// object still being created gets the same pass from the creation path's initObject (RW 0x693D38), so the port makes it once constructed
		if (team)
		{
			updateUpgradeModules(); // RW 0x6936FE
		}
	}
}

// lane BACKLOG4 (S-1222): RW 0x693919 (Object.h)
void Object::abortAbilitiesInUse()
{
	static const int kSummoning = ObjectTemplateInfoBuilder::objectStatusIndex("SUMMONING_REPLACEMENT"); // 0x66
	if (kSummoning >= 0 && testStatus((unsigned)kSummoning))
	{
		return;
	}
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		SpecialPowerUpdateInterface *u = dynamic_cast<SpecialPowerUpdateInterface *>(m.get());
		if (!u || !u->isActive())
		{
			continue;
		}
		SpecialAbilityUpdate *ability = dynamic_cast<SpecialAbilityUpdate *>(m.get());
		if (!ability)
		{
			// retail calls slot 0x18 / module slot 0x34 of whatever class carries the interface; only SpecialAbilityUpdate's are read
			m_logic.noteStop("[S-1222] RW 0x693919: an active special power update that is not a SpecialAbilityUpdate is not aborted (its slots 0x18 / 0x34 are not read)");
			continue;
		}
		if (ability->isPowerCurrentlyInUse())
		{
			ability->finishAbility(false, true); // RW 0x8513E4
		}
	}
	if (m_contain)
	{
		if (m_contain->getHordeContainInterface()) // RW 0x68C866
		{
			if (const ContainModuleInterface::ContainedItemsList *items = m_contain->getContainedItemsList())
			{
				const std::vector<Object *> members(items->begin(), items->end()); // RW 0x6939A7: slot 0x10C fills a list first
				for (Object *member : members)
				{
					member->abortAbilitiesInUse();
				}
			}
		}
	}
}

// ---- lane HERO-2: defection ---------------------------------------------------------------------------------------------------------------------
// RW 0x699513
void Object::setTemporaryTeam(Team *team)
{
	static const int kDefected = ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED"); // 0x3E
	if (!team || testStatus((unsigned)kDefected) || team == m_team)
	{
		return;
	}
	setStatus((unsigned)kDefected, true); // RW 0x62684D(0x3E, 1)
	// RW 0x698E6F -> 0x697C09(team, 0): the team change and the owner change (RW 0x696F0A), the original team not recorded
	Player *oldOwner = getControllingPlayer();
	joinTeam(team);
	if (ShroudManager *shroud = m_logic.shroud())
	{
		shroud->markDirty(*this, true);
	}
	m_logic.partition().markDirty(*this);
	Player *newOwner = getControllingPlayer();
	if (m_constructed && oldOwner != newOwner)
	{
		onOwnerChanged(oldOwner, newOwner);
	}
}

// RW 0x69AB22
void Object::restoreOriginalTeam()
{
	const std::string name = getOriginalTeamName();
	if (!m_team || name.empty())
	{
		return;
	}
	Team *original = m_logic.players().teams().findTeam(name); // RW 0x69AB47: RW 0x7A7483
	if (!original || original == m_team)
	{
		return;
	}
	setTeam(original); // RW 0x69954A
	// lane BACKLOG4: RW 0x69AB60 .. 0x69AB83: the object of a defeated player (RW 0x6AAC4B: Player + 0x754) is killed (RW 0x698EC3(8, 0): UNRESISTABLE,
	// NORMAL). HERO-2 read this call as a selection change
	const Player *owner = getControllingPlayer();
	if (owner && owner->isDefeated())
	{
		kill(0);
	}
}

// RW 0x69ABA7
void Object::endDefection()
{
	static const int kDefected = ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED");
	static const int kHordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");          // 0x26
	static const int kUsingAbility = ObjectTemplateInfoBuilder::objectStatusIndex("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING"); // 0x46
	static const int kStatus52 = 0x52;
	if (!testStatus((unsigned)kDefected))
	{
		return;
	}
	ContainModuleInterface *contain = getContain(); // + 0x258, read before the change
	setStatus((unsigned)kDefected, false); // RW 0x62684D(0x3E, 0)
	restoreOriginalTeam();                 // RW 0x69AB22
	if (TemporarilyDefectUpdate *t = TemporarilyDefectUpdate::of(*this))
	{
		t->friend_markEnded(); // RW 0x69ABEE: the module's + 0x2C = 1
	}
	if (contain)
	{
		contain->onDefectionEnded(); // the contain's vslot 0x10C
	}
	if (!testStatus((unsigned)kHordeMember) && getAIUpdateInterface())
	{
		if (testStatus((unsigned)kUsingAbility))
		{
			abortAbilitiesInUse(); // RW 0x69AC4E (lane BACKLOG4)
		}
		// lane BACKLOG4: RW 0x69AC55 / 0x69AC5C: RW 0x6630D7 (the temporary slot: nothing in this port, AIUpdate.h), then RW 0x663171: the machine kept
		// at the defection is the AI's machine again (HERO-2 idled the AI here as an inference)
		getAIUpdateInterface()->endDefectedStateMachine();
	}
	setStatus(kStatus52, true); // RW 0x62684D(0x52, 1)
	updateUpgradeModules();     // RW 0x6936FE
}

// RW 0x699368: the object goes over to `newOwner`'s side: an AI not of a horde member drops its temporary state (RW 0x6630D7) and, for a temporary defection,
// keeps its machine (RW 0x663128); a temporary defection starts the object's TemporarilyDefectUpdate (RW 0x8D0BF3: the team through setTemporaryTeam), a
// permanent one takes the new owner's team (RW 0x698E6F); the contain's members follow (vslot 0x108); the player's upgrades apply (RW 0x6936FE); the model
// conditions ATTACKING / ATTACKING_STRUCTURE / ATTACKING_POSITION (+ 0x110 bits 5 .. 7) and the statuses IS_FIRING_WEAPON, IGNORING_STEALTH, IS_ATTACKING,
// IS_AIMING_WEAPON, IS_MELEE_ATTACKING (0x0D, 0x19, 0x16, 0x1B, 0x1C) are cleared
void Object::defect(Object *newOwner, bool permanent)
{
	static const int kHordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
	static const int kUsingAbility = ObjectTemplateInfoBuilder::objectStatusIndex("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING");
	if (!newOwner)
	{
		return;
	}
	if (!testStatus((unsigned)kHordeMember) && getAIUpdateInterface())
	{
		if (testStatus((unsigned)kUsingAbility))
		{
			abortAbilitiesInUse(); // RW 0x69939D (lane BACKLOG4)
		}
		// lane BACKLOG4: RW 0x6993A4 .. 0x6993B0: RW 0x6630D7 (the temporary slot: nothing in this port, AIUpdate.h); a temporary defection keeps the
		// machine and starts a new one (RW 0x663128); a permanent one leaves the AI as it is (HERO-2 idled the AI in both cases as an inference)
		if (!permanent)
		{
			getAIUpdateInterface()->beginDefectedStateMachine();
		}
	}
	// RW 0x6993B5 .. 0x6993D0: the selected drawable leaves the selection (client)
	// lane BACKLOG4: RW 0x6993D6 .. 0x6993E6: the object's slaved interface (RW 0x8A4ED8: the first module's behaviour slot 0x68) gets slot 2 with no
	// damage info (RW 0x8A52A7 -> 0x8A51B9): a slave leaves its master as if the master had died
	if (SlavedUpdateInterface *slaved = SlavedUpdateInterface::of(*this))
	{
		slaved->onSlaverDie(nullptr);
	}
	// RW 0x6993E9 .. 0x699401: the object's spawn interface (RW 0x68C3A3: behaviour slot 0x78) stops spawning (slot 9, RW 0x8623D6) and takes orphans for
	// its due replacements (slot 12, RW 0x86327C)
	if (SpawnBehaviorInterface *spawner = SpawnBehaviorInterface::of(*this))
	{
		spawner->stopSpawning();
		spawner->reclaimDueOrphans();
	}
	if (!permanent)
	{
		if (TemporarilyDefectUpdate *t = TemporarilyDefectUpdate::of(*this)) // RW 0x68BDA5("TemporarilyDefectUpdate")
		{
			t->startDefection(newOwner, 0); // RW 0x8D0BF3
		}
	}
	else if (newOwner->getTeam() && newOwner->getTeam() != m_team)
	{
		// RW 0x698E6F: the team change without recording it as the original
		Player *oldOwner = getControllingPlayer();
		joinTeam(newOwner->getTeam());
		if (ShroudManager *shroud = m_logic.shroud())
		{
			shroud->markDirty(*this, true);
		}
		m_logic.partition().markDirty(*this);
		Player *owner = getControllingPlayer();
		if (oldOwner != owner)
		{
			onOwnerChanged(oldOwner, owner);
		}
	}
	if (ContainModuleInterface *c = getContain())
	{
		c->onDefect(newOwner, permanent); // vslot 0x108
	}
	updateUpgradeModules(); // RW 0x6936FE
	for (int bit : { 37, 38, 39 }) // + 0x110 bits 5 .. 7
	{
		if (testModelCondition(bit))
		{
			setModelConditionState(bit, false);
		}
	}
	for (unsigned st : { 0x0Du, 0x19u, 0x16u, 0x1Bu, 0x1Cu })
	{
		setStatus(st, false); // RW 0x62684D(x, 0)
	}
}

// lane HERO-2: RW 0x6996DC
void Object::setCapturedTeam(Team *team)
{
	if (getContainedBy() || !team)
	{
		return; // + 0x27C
	}
	Player *owner = getControllingPlayer();
	if (!owner || owner->getDefaultTeam() == team)
	{
		return; // RW 0x69970C: the owner's default team (+ 0x30C) is the new one
	}
	if (testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION")) || testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD")))
	{
		return;
	}
	// RW 0x699733 .. 0x6997A7: the production module's vslot 0x40 (its queue cancelled), the score keeper's structure counts, RW 0x68B303 and the firing tracker
	// RW 0x8E3ABB are not ported (S-1225); the selection, radar and EVA messages after the change are the client's
	m_logic.noteStop("[S-1225] Object::setCapturedTeam: the production cancel, score counts and RW 0x68B303 / 0x8E3ABB of a capture are not ported");
	setTeam(team); // RW 0x69954A
	if (AIUpdateInterface *ai = getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x6997C4: RW 0x68C213, then aiIdle(2)
	}
	if (ContainModuleInterface *c = getContain())
	{
		if (c->getContainCount() != 0)
		{
			m_logic.noteStop("[S-1225] Object::setCapturedTeam: a captured container's contain vslots 0xD0 / 0xA8 (its passengers) are not read");
		}
	}
}

// RW 0x696F0A: the command points follow the owner (RW 0x6914B7: the old owner loses the object, the new one gains it, both without looking at the `counted` byte)
// unless the object is only temporarily defected; then every module learns of the capture (the module slot at + 0x24, called with the two players)
void Object::onOwnerChanged(Player *oldOwner, Player *newOwner)
{
	const int defected = ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED");
	if (!(defected >= 0 && testStatus((unsigned)defected)) && oldOwner && newOwner)
	{
		Economy &economy = m_logic.economy();
		economy.objectLost(*oldOwner, *this);
		economy.objectGained(*newOwner, *this);
	}
	// RW 0x6AE049 .. 0x6AE068 (the gaining half of the owner change): a new owner that is not the neutral player collects the AutoDepositUpdate's initial capture bonus
	if (newOwner && newOwner != m_logic.players().getNeutralPlayer())
	{
		if (AutoDepositUpdate *deposit = dynamic_cast<AutoDepositUpdate *>(findModule("AutoDepositUpdate")))
		{
			deposit->awardInitialCaptureBonus(newOwner);
		}
	}
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		m->onCapture(oldOwner, newOwner);
	}
}

void Object::addToPlayerCommandPoints()
{
	// RW 0x68E0C2
	if (m_commandPointsCounted)
	{
		return;
	}
	Economy &economy = m_logic.economy();
	if (Economy::templateInt(*m_template, "CommandPoints") <= 0 && Economy::templateInt(*m_template, "CommandPointBonus") <= 0)
	{
		return;
	}
	const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	const int pending = ObjectTemplateInfoBuilder::objectStatusIndex("PENDING_CONSTRUCTION");
	if ((pending >= 0 && testStatus((unsigned)pending)) || (underConstruction >= 0 && testStatus((unsigned)underConstruction)))
	{
		return;
	}
	Player *owner = getControllingPlayer();
	if (!owner)
	{
		return;
	}
	economy.objectGained(*owner, *this);
	m_commandPointsCounted = true;
}

void Object::removeFromPlayerCommandPoints()
{
	// RW 0x68E114 (the TEMPORARILY_DEFECTED branch looks the ORIGINAL owner up by the id kept at + 0x320: the DefectionHelper is not ported, S-256)
	if (!m_commandPointsCounted)
	{
		return;
	}
	Player *owner = getControllingPlayer();
	if (owner)
	{
		m_logic.economy().objectLost(*owner, *this);
		m_commandPointsCounted = false;
	}
}

void Object::friend_onBuildComplete()
{
	// RW 0x68D252: the create interface's slot 1 of every module (the drawable's modules follow in retail: the client lane's)
	for (size_t i = 0; i < m_modules.size(); ++i)
	{
		if (CreateModuleInterface *c = m_modules[i]->getCreate())
		{
			c->onBuildComplete();
		}
	}
}

void Object::friend_onMapPlacement(const Dict *properties)
{
	// lane WALL-2, RW 0x694692 .. 0x6946BF: the +0xC interface's slot 45 of every module
	for (size_t i = 0; i < m_modules.size(); ++i)
	{
		m_modules[i]->onMapPlacement(properties);
	}
}

void Object::friend_onDie(const DieModuleInterface::Event &event)
{
	// RW 0x698F06 (the dead flag RW + 0x458 bit 0 first: from now on the object is no target and takes no more damage)
	m_effectivelyDead = true;
	if (m_client)
	{
		m_client->observerEvent(*this, ObserverEvent::Death);
	}
	for (size_t i = 0; i < m_modules.size(); ++i)
	{
		if (DieModuleInterface *d = m_modules[i]->getDie())
		{
			d->onDie(event);
		}
	}
	removeFromPlayerCommandPoints(); // RW 0x698FD9
	m_logic.emotions().objectDied(*this); // RW 0x6990AC .. 0x6990C8 (lane MODULES-2): a SCARY or HERO object leaves TheEmotionSystem's list
	// COMBAT-1: a dead horde member leaves its horde at once (its corpse sinks where it fell; the ranks close up, the horde dies with its last member)
	if (m_containedBy)
	{
		if (ContainModuleInterface *c = m_containedBy->getContain())
		{
			if (c->getHordeContainInterface())
			{
				c->removeFromContain(this);
			}
		}
	}
	// COMBAT-1: the dead leave the pathfinder (their cell is free for the living; a corpse does not block a march) and their AI stops
	if (m_ai)
	{
		m_ai->onDie();
	}
	if (AIWorld *world = m_logic.aiWorld())
	{
		if (world->mapReady())
		{
			world->removeObjectFromPathfindMap(*this);
		}
	}
	// lane MOD-4: RW 0x699082 -> RW 0x6938BD: the producer's spawn interface hears of the death (SpawnBehavior::onSpawnDeath)
	SpawnBehaviorInterface::notifyProducerOfDeath(*this, event);
	++m_logic.combat().counters().kills;
}

// lane DECOMP-3: RW 0x691269 (BFME2 decomp Object_isAbleToAttack.cpp:Object::isAbleToAttack, tier B same-shape for RW 0x691269; RotWK's offsets read here):
//   1. NO_ATTACK (status 5) or the model condition 0x81 (RW 0x46E918 on + 0x10C): false;
//   2. the container (+ 0x27C; a HORDE container, template + 0x115 bit 5, is skipped for its own container) with a contain (+ 0x258), unless this object is
//      CONTESTING_BUILDING (status 0x25): its slot 0xB0(this) mask lacks CAN_ATTACK (status 1) -> false; with an AI (+ 0x260) whose current victim
//      (RW 0x668303) the contain's slot 0xC0(this, victim) blocks -> false;
//   3. UNDER_CONSTRUCTION (2) or SOLD (0x13): false; a PORTABLE_STRUCTURE (+ 0x10F bit 1) or SPAWNS_ARE_THE_WEAPONS (+ 0x112 bit 4) with DISABLED_EMP (+ 0x1C8
//      bit 2): false;
//   4. with an AI and no CAN_ATTACK KindOf: the weapon slots 0 .. 5 in order (RW 0x6C81B0 on + 0x34C): a weapon on no turret (RW 0x66246B: -1) ends the walk;
//      one on an enabled turret (RW 0x662364) too; when there were weapons, all on disabled turrets: false;
//   5. CAN_ATTACK KindOf (+ 0x108 bit 3) or status: true; its own contain's slot 0xB4 (isPassengerAllowedToFire) and slot 0x114(0) > 0 (the riders): true;
//      an AI with a weapon (+ 0x374: the weapon set's filled slot mask, ZH WeaponSet::hasAnyWeapon): true; its spawn interface (RW 0x68C3A3) slot 0x1C
//      canAnySlavesAttack: true; else false.
bool Object::isAbleToAttack() const
{
	static const int noAttack = CombatNames::status("NO_ATTACK"), contesting = CombatNames::status("CONTESTING_BUILDING"),
					 canAttackStatus = CombatNames::status("CAN_ATTACK"), sold = CombatNames::status("SOLD");
	static const int horde = CombatNames::kindOf("HORDE"), portable = CombatNames::kindOf("PORTABLE_STRUCTURE"), spawnsWeapons = CombatNames::kindOf("SPAWNS_ARE_THE_WEAPONS"),
					 canAttackKind = CombatNames::kindOf("CAN_ATTACK");
	if (testStatus((unsigned)noAttack) || testModelCondition(0x81))
	{
		return false;
	}
	const Object *container = m_containedBy;
	if (container && container->isKindOf((unsigned)horde))
	{
		container = container->getContainedBy();
	}
	const ContainModuleInterface *cmi = container ? container->getContain() : nullptr;
	if (cmi && !testStatus((unsigned)contesting))
	{
		const ObjectStatusMaskType *mask = cmi->getObjectStatusOfContainedFor(this);
		if (!mask || !MaskTest(*mask, (unsigned)canAttackStatus))
		{
			return false; // RW 0x6912E3 (a contain without the mask: INFERENCE, no CAN_ATTACK)
		}
		const Object *victim = m_ai ? m_ai->currentVictim() : nullptr; // RW 0x668303
		if (victim && cmi->isPassengerShotBlocked(this, victim))
		{
			return false;
		}
	}
	if (isUnderConstruction() || testStatus((unsigned)sold))
	{
		return false;
	}
	if ((isKindOf((unsigned)portable) || isKindOf((unsigned)spawnsWeapons)) && isDisabledByType(DISABLED_EMP))
	{
		return false;
	}
	if (m_ai && !isKindOf((unsigned)canAttackKind))
	{
		bool anyWeapon = false, anyEnabled = false, noTurret = false;
		const TurretAI *turret = m_ai->turret();
		for (int slot = 0; slot < WEAPONSLOT_COUNT && !noTurret && !anyEnabled; ++slot)
		{
			if (!m_weapons || !m_weapons->weaponInSlot(slot))
			{
				continue;
			}
			anyWeapon = true;
			if (!turret || !turret->isWeaponSlotOnTurret(slot))
			{
				noTurret = true; // RW 0x691388: -1 jumps to the CAN_ATTACK tests
			}
			else if (turret->isEnabled())
			{
				anyEnabled = true;
			}
		}
		if (!noTurret && anyWeapon && !anyEnabled)
		{
			return false;
		}
	}
	if (isKindOf((unsigned)canAttackKind) || testStatus((unsigned)canAttackStatus))
	{
		return true;
	}
	if (m_contain && m_contain->isPassengerAllowedToFire() && m_contain->getContainCount() > 0)
	{
		return true;
	}
	if (m_ai && m_weapons && m_weapons->hasAnyWeapon())
	{
		return true;
	}
	const SpawnBehaviorInterface *spawn = SpawnBehaviorInterface::of(const_cast<Object &>(*this));
	return spawn && spawn->canAnySlavesAttack();
}

Player *Object::getControllingPlayer() const
{
	return m_team ? m_team->getControllingPlayer() : nullptr;
}

// RW 0x68B961 (see Object.h)
void Object::pauseAllSpecialPowers(bool pause)
{
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (SpecialPowerModuleInterface *sp = m->getSpecialPower())
		{
			sp->pauseCountdown(pause);
		}
	}
}

// RW 0x6907F1 (see Object.h)
void Object::setDisabled(unsigned type, UnsignedInt untilFrame)
{
	if (type >= DISABLED_TYPE_COUNT)
	{
		throw std::logic_error("Object::setDisabled: type out of range"); // RW returns: every caller here passes one of the 12 constants
	}
	if (untilFrame <= m_logic.getFrame())
	{
		return;
	}
	if (m_disabledExpire[type] == untilFrame)
	{
		return;
	}
	if (type != DISABLED_HELD && !isDisabledByType(type))
	{
		pauseAllSpecialPowers(true);
	}
	m_disabledExpire[type] = untilFrame;
	m_disabled |= (1u << type); // RW 0x68CC09(type, untilFrame > now): always set here
	static const int kParalyzedCondition = 529; // + 0x14C bit 17 (RW 0x6908A0 .. 0x6908B6)
	if ((type == DISABLED_USER_PARALYZED || type == DISABLED_USER_FROZEN) && !testModelCondition(kParalyzedCondition))
	{
		setModelConditionState(kParalyzedCondition, true);
	}
}

// RW 0x692443 (see Object.h)
void Object::clearDisabled(unsigned type)
{
	if (!isDisabledByType(type))
	{
		return;
	}
	if (type != DISABLED_HELD)
	{
		pauseAllSpecialPowers(false);
	}
	m_disabledExpire[type] = 0;
	m_disabled &= ~(1u << type);
	static const int kParalyzedCondition = 529;
	if ((type == DISABLED_USER_PARALYZED || type == DISABLED_USER_FROZEN) && testModelCondition(kParalyzedCondition))
	{
		setModelConditionState(kParalyzedCondition, false);
	}
	checkDisabledStatus(m_logic.getFrame()); // RW 0x69261D
}

BehaviorModule *Object::findModule(std::string_view className) const
{
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (m->getModuleClassName() == className)
		{
			return m.get();
		}
	}
	return nullptr;
}

ExitInterface *Object::getObjectExitInterface() const
{
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (ExitInterface *e = m->getExitInterface())
		{
			return e;
		}
	}
	return nullptr; // RW 0x68BB37: the contain's exit slot (0x74) is not ported
}

ProductionUpdateInterface *Object::getProductionUpdate() const
{
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (ProductionUpdateInterface *p = m->getProductionUpdateInterface())
		{
			return p;
		}
	}
	return nullptr;
}

BehaviorModule *Object::findModuleByTag(NameKeyType tagKey) const
{
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (m->getModuleTagNameKey() == tagKey)
		{
			return m.get();
		}
	}
	return nullptr;
}

void Object::friend_bindToClient(ObjectClientHooks *client)
{
	m_client = client;
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		m->onDrawableBoundToObject();
	}
}

// RW 0x6260E1
void Object::recordTransform(UnsignedInt frame)
{
	if (m_recordedValid)
	{
		m_previousPosition = m_recordedPosition;
	}
	else
	{
		m_previousPosition = *getPosition();
	}
	m_previousValid = true;
	m_recordedPosition = *getPosition();
	m_recordedAngle = getOrientation();
	std::memcpy(m_recordedBasis, getBasis(), sizeof(m_recordedBasis));
	m_recordedValid = true;
	m_recordedFrame = frame;
	// RW 0x62618F: +0x1A6 = 0, the movers' pending position (+0x198) is no longer valid (lane SMOOTH-2, S-812: the port keeps the pair in AIUpdate). A mover
	// without a path then records its pre-move position as pending (LocomotorMove RW 0x5E5B4E reads +0x38), not one of an earlier frame
	if (AIUpdateInterface *ai = getAIUpdateInterface())
	{
		ai->clearPendingPosition();
	}
}

// RW 0x674B1F(1), the drawable's forced gather (Object.h)
void Object::teleportGather()
{
	m_teleportGathered = true;
	m_teleportGatherFrame = m_logic.getFrame();
}

// RW 0x696E63 (Object.h)
void Object::teleportTo(const Coord3D *pos, bool keepAI)
{
	setPosition(pos);
	recordTransform(m_logic.getFrame());
	recordTransform(m_logic.getFrame());
	teleportGather();
	setPosition(pos);
	updateShroudNow();
	if (m_contain)
	{
		if (HordeContainInterface *horde = m_contain->getHordeContainInterface())
		{
			horde->teleportMembers(); // RW 0x696EBA .. 0x696ECB: contain slot 0x7C, then interface slot 0x1B8 (RW 0x86D881)
		}
	}
	if (keepAI)
	{
		return;
	}
	if (AIUpdateInterface *ai = getAIUpdateInterface())
	{
		// RW 0x696EE3 .. 0x696EF9: aiIdle from source 2 with the status 0x5A (IS_LEAVING_FACTORY), else 0
		static const int leavingFactory = ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY");
		ai->aiIdle(leavingFactory >= 0 && testStatus((unsigned)leavingFactory) ? CMD_FROM_AI : CMD_FROM_PLAYER);
		ai->mover().destroyPath(); // RW 0x66276B
	}
}

// RW 0x68DA67 (Object.h)
void Object::teleportTransform(const Coord3D *pos, const float basis[9])
{
	setTransform(pos, basis);
	recordTransform(m_logic.getFrame());
	recordTransform(m_logic.getFrame());
	teleportGather();
	setTransform(pos, basis);
	updateShroudNow();
	if (m_contain)
	{
		if (HordeContainInterface *horde = m_contain->getHordeContainInterface())
		{
			horde->teleportMembersTransform(basis); // RW 0x68DAB3 .. 0x68DACB: contain slot 0x7C, then interface slot 0x1B4 (RW 0x86D8FE)
		}
	}
}

// B1 Object::checkDisabledStatus (RVA 0x1C5780, matched): "tests all 11 DisabledType bits and clears each active type whose expiration
// frame at Object+0x1A8+4*type is no later than the current simulation frame"; RW 0x690A42
// RW 0x690A42: every set type whose expiry is not after the frame is cleared (RW 0x692443; the bit and PARALYZED again after it, already done)
void Object::checkDisabledStatus(UnsignedInt frame)
{
	for (unsigned type = 0; type < DISABLED_TYPE_COUNT; ++type)
	{
		if (isDisabledByType(type) && frame >= m_disabledExpire[type])
		{
			clearDisabled(type);
		}
	}
}

// B1 Object::checkIgnoreAICommandStatus (0x1CE7B0): "when the nonzero expiration at Object+0x338 is older than the current frame, clears the
// status and zeros the expiration"; RW 0x690AB9
void Object::checkIgnoreAICommandStatus(UnsignedInt frame)
{
	if (m_ignoreAIExpire != 0 && m_ignoreAIExpire < frame)
	{
		setStatus(OBJECT_STATUS_IGNORE_AI_COMMAND, false);
		m_ignoreAIExpire = 0;
	}
}

// B1 0x1CE7F0 (Object+0x33C, status bit 4); RW 0x690AE5
void Object::checkNoCollisionsStatus(UnsignedInt frame)
{
	if (m_noCollisionsExpire != 0 && m_noCollisionsExpire < frame)
	{
		setStatus(OBJECT_STATUS_NO_COLLISIONS, false);
		m_noCollisionsExpire = 0;
	}
}

// step 3 (RW 0x6D16E6 .. 0x6D1709)
void Object::friend_runCreateModules()
{
	for (size_t i = 0; i < m_modules.size(); ++i)
	{
		if (CreateModuleInterface *c = m_modules[i]->getCreate())
		{
			c->onCreate();
		}
	}
}

// step 4: initObject (RW 0x693D0C); only sendObjectCreated (RW 0x628882: the creation draw, the drawable, OnCreated) is ported (S-142)
void Object::friend_initObject()
{
	m_creationSeed = m_logic.sendObjectCreated(*this);
	updateUpgradeModules(); // RW 0x693D38: the player's upgrades are applied to the new object
	// then, when it has an owner, its command points join the pool (RW 0x693D51)
	if (getControllingPlayer())
	{
		addToPlayerCommandPoints();
		// lane CAMP-1H: RW 0x693D58 .. 0x693D6F: with an owner, an object not yet receiving it gets the difficulty bonus when the script engine allows it
		// (+ 0x1A5D5, on after every reset; OBJECT_ALLOW_BONUSES). NOT PORTED: RW 0x693D74 .. 0x693D87, the owner's handicap (Player + 0xAC / 0xB0 / 0xB4,
		// RW 0x6AD3E6; see S-1712)
		if (!m_receivingDifficultyBonus && m_logic.scriptEngine().objectsReceiveDifficultyBonus())
		{
			setReceivingDifficultyBonus(true);
		}
	}
	m_logic.emotions().objectInitialized(*this); // RW 0x693E79 .. 0x693E95 (lane MODULES-2): a SCARY or HERO object joins TheEmotionSystem's list
	// lane HERO-2: RW 0x693EE8 .. 0x693F06: a CREATE_A_HERO (not while a save game loads, GameLogic + 0x6F) takes its player's Create-a-Hero record (RW 0x61B17D)
	if (isKindOfName("CREATE_A_HERO"))
	{
		m_logic.createAHeroes().onCreated(*this);
	}
	// lane XP-1: RW 0x693F1B .. 0x693F2F: the first level (no feedback), then the rank scalar's base rank is the rank reached
	if (m_experienceTracker)
	{
		m_experienceTracker->gainLevel(false);
		m_experienceTracker->resetBaseToRank();
	}
	// lane SPELL-2: RW 0x694065 .. 0x694073: TheGlobalWeatherSystem gives a new object the active weather-based modifier (RW 0x719C69)
	m_logic.weather().apply(*this, false);
	// lane OWNERFIX-2: RW 0x694078 .. 0x694130 (BFME2 decomp ObjectInitObject.cpp Object::initObject, tier B: RotWK adds the SHIP test): in a multiplayer game
	// (RW 0x625456) an object that is a STRUCTURE, an FS_POWER / FS_FACTORY / FS_BASE_DEFENSE / FS_TECHNOLOGY / FS_CASH_PRODUCER (RW 0x68DAE6), INFANTRY,
	// CAVALRY, MONSTER, MACHINE, HERO or SHIP (template + 0x11F & 0x80) takes "HandicapPercent<n>" (n = -(the controlling player's + 0x29C), only for
	// 0 < n <= 100 and n % 5 == 0) through RW 0x68F1A8 with the list's own duration; a list the data lacks is no modifier (RW 0x68F1A8 finds none)
	if (Player *owner = getControllingPlayer(); owner && m_logic.economy().isMultiplayerGame())
	{
		static const char *const kHandicapKinds[] = { "STRUCTURE", "FS_POWER", "FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_CASH_PRODUCER", "INFANTRY",
			"CAVALRY", "MONSTER", "MACHINE", "HERO", "SHIP" };
		bool kind = false;
		for (const char *k : kHandicapKinds)
		{
			kind = kind || isKindOfName(k);
		}
		const int n = -owner->getHandicap(); // neg eax (RW 0x6940E4); the handicap is one of the lobby values (no overflow)
		if (kind && n > 0 && n <= 100 && n % 5 == 0)
		{
			addAttributeModifier("HandicapPercent" + std::to_string(n), -1);
		}
	}
	// lane RULES-1, OpenBFME (not retail): the Legendary Heroes rule scales a player's hero once its record, level and modifiers are in (nothing at Off)
	LegendaryHeroesGame::applyAtCreation(*this);
	// lane END-1: RW 0x694130 .. 0x694140: the controlling player's ScoreKeeper::addObjectBuilt(this, 1) (RW 0x79F0E1), last in initObject
	if (Player *p = getControllingPlayer())
	{
		p->getScoreKeeper().addObjectBuilt(m_logic, *this, 1);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lane CAMP-1H: the difficulty bonus (TARGET FACTS, rotwk201_game.exe, caveat S-001)
// RW 0x68B907: a different value is stored (+ 0x45C) and, when the object has a controlling player (RW 0x68B678), handed to that player.
void Object::setReceivingDifficultyBonus(bool receive)
{
	if (receive == m_receivingDifficultyBonus)
	{
		return;
	}
	m_receivingDifficultyBonus = receive;
	if (Player *p = getControllingPlayer())
	{
		p->applyDifficultyBonusesForObject(*this, m_receivingDifficultyBonus);
	}
}

// RW 0x6AC32D (Player::applyDifficultyBonusesForObject; its only caller is RW 0x68B907). Nothing happens when `apply` is false (retail never takes a
// bonus back), for an object of a human player (RW 0x68B68A), one without a controlling player, or one whose player's template (Player + 0x34) is not a
// PlayableSide (+ 0x151). Then the upgrade by difficulty:
//   * a multiplayer game (RW 0x625456: LAN, skirmish, internet) and the player has an AI (Player + 0x2FC: every computer player, RW 0x6AA450): the AI's
//     difficulty (+ 0x30, RW 0x9A5E0C): a skirmish AI's level, an AIPlayer's the script engine's (RW 0x8F7FEB) - Upgrade_{Easy,Medium,Hard,Brutal}AIMultiPlayer;
//   * otherwise TheGameLogic + 0xA4 (prepareNewGame's difficulty: the campaign's choice) - Upgrade_{Easy,Medium,Hard,Brutal}AISinglePlayer;
//   * any other difficulty value: no name, no upgrade.
// The upgrade is found by name (RW 0x5487EC -> TheUpgradeCenter RW 0x66F230; an unknown name gives nothing) and given to the object (RW 0x69388B).
// The names are the binary's (RW 0xC13EEC .. 0xC13FB8), the upgrades and their AttributeModifierUpgrade modules are the data's (upgrade.ini,
// default\object.ini's DefaultThingTemplate).
void Player::applyDifficultyBonusesForObject(Object &obj, bool apply)
{
	if (!apply)
	{
		return;
	}
	const Player *owner = obj.getControllingPlayer();
	if (!owner || owner->getPlayerType() == PLAYER_HUMAN || !owner->getPlayerTemplate() || !owner->getPlayerTemplate()->m_playableSide)
	{
		return;
	}
	GameLogic &logic = obj.logic();
	static const char *const kMulti[4] = { "Upgrade_EasyAIMultiPlayer", "Upgrade_MediumAIMultiPlayer", "Upgrade_HardAIMultiPlayer", "Upgrade_BrutalAIMultiPlayer" };
	static const char *const kSingle[4] = { "Upgrade_EasyAISinglePlayer", "Upgrade_MediumAISinglePlayer", "Upgrade_HardAISinglePlayer",
		"Upgrade_BrutalAISinglePlayer" };
	const char *name = nullptr;
	if (logic.economy().isMultiplayerGame() && getPlayerType() == PLAYER_COMPUTER)
	{
		const int d = getSkirmishDifficulty() >= 0 ? getSkirmishDifficulty() : logic.scriptEngine().gameDifficulty();
		name = d >= 0 && d < 4 ? kMulti[d] : nullptr;
	}
	else
	{
		const int d = logic.getGameDifficulty();
		name = d >= 0 && d < 4 ? kSingle[d] : nullptr;
	}
	if (!name || !TheUpgradeCenter)
	{
		return;
	}
	if (const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(name))
	{
		obj.giveUpgrade(u);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lane UPGRADE-1
void Object::giveUpgradeSelf(const UpgradeTemplate *upgrade)
{
	if (!upgrade)
	{
		return;
	}
	m_upgradeMask.orWith(upgrade->grantMask()); // RW 0x693817
	updateUpgradeModules();
}

bool Object::hasUpgrade(const UpgradeTemplate *upgrade) const
{
	if (!upgrade)
	{
		return false;
	}
	// TARGET RW 0x68E040: RW 0x68C866 returns contain slot 0x7C of Object + 0x258 (null without a contain). Of the contain interface vtables only the HordeContain
	// family answers (RW 0x872A6F: the module's HordeContainInterface at module + 0x11C; HordeContain RW 0xC5B480, its RotWK-only subclass RW 0xC5C110,
	// AODHordeContain RW 0xC5D268); OpenContain and every other contain (TransportContain, HordeTransportContain, GarrisonContain, ...) return null (RW 0x851E97).
	// Its slot 0xB0 (RW 0x86EC2D, BFME2 decomp HordeContainIface11CSlots.cpp HordeContain::rva0046B95E, tier A) is true when a contained member has the upgrade
	// (this function, recursively), then when an object of the second member list (module + 0x170) has it: that list is not ported (S-486)
	if (m_contain && m_contain->getHordeContainInterface())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = m_contain->getContainedItemsList())
		{
			for (const Object *member : *items)
			{
				if (member->hasUpgrade(upgrade))
				{
					return true;
				}
			}
		}
	}
	return m_upgradeMask.test((unsigned)upgrade->getMaskBit());
}

void Object::giveUpgrade(const UpgradeTemplate *upgrade)
{
	if (!upgrade)
	{
		return;
	}
	giveUpgradeSelf(upgrade);
	ContainModuleInterface *contain = getContain();
	if (!contain || !contain->getHordeContainInterface())
	{
		return; // INFERENCE: only HordeContain's slot 0x174 (RW 0x87566B) was read; the other contain classes' slot is not ported (S-483)
	}
	// RW 0x87566B: the templates of the set bits in bit order, then every member in list order (the second list at HordeContain + 0x150 is S-486)
	std::vector<const UpgradeTemplate *> granted;
	for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
	{
		if (m_upgradeMask.test(bit) && TheUpgradeCenter)
		{
			if (const UpgradeTemplate *t = TheUpgradeCenter->findUpgradeByMaskBit((int)bit))
			{
				granted.push_back(t);
			}
		}
	}
	if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
	{
		const std::vector<Object *> members(items->begin(), items->end()); // a grant may not change the list; stay safe anyway
		for (Object *member : members)
		{
			for (const UpgradeTemplate *t : granted)
			{
				if (member->affectedByUpgrade(t))
				{
					member->giveUpgrade(t);
				}
			}
		}
	}
}

void Object::addUpgradeCostPaid(float cost)
{
	m_upgradeCostPaid = SimMath::addf32(m_upgradeCostPaid, cost);
}

bool Object::affectedByUpgrade(const UpgradeTemplate *upgrade) const
{
	const Player *player = getControllingPlayer();
	if (!upgrade || !player || upgrade->getMaskBit() < 0)
	{
		return false;
	}
	static const ObjectFilter kDefaultFilter; // the template constructor's filter (RW 0x76406F: the parser default)
	const ObjectFilter &filter = upgrade->m_requiredObjectFilter ? *upgrade->m_requiredObjectFilter : kDefaultFilter;
	// RW 0x6939DF / 0x693A1A: an attached horde member can use its horde's upgrade.
	const Object *producer = logic().findObjectByID(getProducerID());
	static const int kHordeMember = CombatNames::status("HORDE_MEMBER"); // RW 0x6939DF: status bit 0x26
	const bool attached = getProducerID() != INVALID_ID &&
		(testStatus((unsigned)kHordeMember) || (producer && producer->isKindOfName("HORDE")));
	const Object *horde = attached ? getHordeObject(false) : nullptr;
	bool requiredObject = horde && horde->hasUpgrade(upgrade);
	// RW 0x6ABD0B / 0x7A075A(filter, 1): a live object that is no MINE (template + 0x10E bit 7) and, as a STRUCTURE (+ 0x108 bit 7), has neither
	// AWAITING_CONSTRUCTION nor PARTIALLY_CONSTRUCTED (Object + 0x114 bits 3 / 4 = model conditions 67 / 68), allowed by the filter without a context; then
	// RW 0x6ABDC2 / 0x7A089B: a live object that is no STRUCTURE, PROJECTILE (+ 0x108 bit 25) or IGNORE_FOR_VICTORY (+ 0x11A bit 4), allowed with the
	// owner's context
	static const int kMine = ObjectTemplateInfoBuilder::kindOfIndex("MINE");
	static const int kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	static const int kProjectile = ObjectTemplateInfoBuilder::kindOfIndex("PROJECTILE");
	static const int kIgnoreForVictory = ObjectTemplateInfoBuilder::kindOfIndex("IGNORE_FOR_VICTORY");
	static const int kAwaiting = CombatNames::modelCondition("AWAITING_CONSTRUCTION");
	static const int kPartial = CombatNames::modelCondition("PARTIALLY_CONSTRUCTED");
	if (kMine < 0 || kStructure < 0 || kProjectile < 0 || kIgnoreForVictory < 0)
	{
		throw std::logic_error("Object::affectedByUpgrade: a KindOf name of RW 0x7A075A / 0x7A089B is not registered");
	}
	for (const Object *object = logic().getFirstObject(); object && !requiredObject; object = object->getNextObject())
	{
		if (object->getControllingPlayer() != player || object->isEffectivelyDead() || object->isDestroyed())
		{
			continue;
		}
		const KindOfMaskType &kind = object->getKindOf();
		const bool structure = MaskTest(kind, (unsigned)kStructure);
		if (!MaskTest(kind, (unsigned)kMine) && (!structure || (!object->testModelCondition(kAwaiting) && !object->testModelCondition(kPartial))) &&
			ObjectFilterMatch::allows(logic(), filter, *object, nullptr))
		{
			requiredObject = true;
		}
		else if (!structure && !MaskTest(kind, (unsigned)kProjectile) && !MaskTest(kind, (unsigned)kIgnoreForVictory) &&
		         ObjectFilterMatch::allows(logic(), filter, *object, player))
		{
			requiredObject = true;
		}
	}
	if (!requiredObject)
	{
		return false;
	}
	UpgradeMaskType mask = player->getCompletedUpgradeMask();
	mask.orWith(m_upgradeMask);
	mask.set((unsigned)upgrade->getMaskBit());
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (UpgradeMux *mux = m->getUpgrade())
		{
			if (mux->wouldUpgrade(mask)) // slot 2
			{
				return true;
			}
		}
	}
	if (ContainModuleInterface *contain = m_contain) // RW 0x6949D9: Object + 0x258, its list (slot 0x118)
	{
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			for (const Object *member : *items)
			{
				if (member->affectedByUpgrade(upgrade))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void Object::removeUpgrade(const UpgradeTemplate *upgrade)
{
	if (!upgrade || upgrade->getMaskBit() < 0)
	{
		return;
	}
	m_upgradeMask.clear((unsigned)upgrade->getMaskBit()); // RW 0x691438
	UpgradeMaskType single;
	single.set((unsigned)upgrade->getMaskBit()); // RW 0x68FD31(0, bit)
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (UpgradeMux *mux = m->getUpgrade())
		{
			if (mux->resetUpgrade(single)) // slot 3 (RW 0x8D2901)
			{
				mux->removeUpgrade(); // RW 0x8D2688
			}
		}
	}
}

void Object::updateUpgradeModules()
{
	const Player *player = getControllingPlayer(); // RW 0x68B678
	if (!player)
	{
		return;
	}
	// RW 0x693724 .. 0x69373C: the object's CastleMemberBehavior (RW 0x797BEF, BFME2 decomp CastleMemberBehaviorFind.cpp, tier A: findModule by the class name) names
	// the castle's keep at module + 0x14; that object (looked up once, before the modules) adds its own upgrades to every attempt
	const CastleMemberBehavior *castleMember = dynamic_cast<const CastleMemberBehavior *>(findModule("CastleMemberBehavior"));
	const Object *keep = castleMember && castleMember->keepId() != INVALID_ID ? logic().findObjectByID(castleMember->keepId()) : nullptr;
	for (size_t i = 0; i < m_modules.size(); ++i) // index loop: an implementation may not add modules, but stay safe against reallocation
	{
		UpgradeMux *mux = m_modules[i]->getUpgrade();
		if (!mux)
		{
			continue;
		}
		if (!mux->isAlreadyUpgraded())
		{
			UpgradeMaskType mask = player->getCompletedUpgradeMask(); // RW 0x6936FE: rebuilt for every module (an implementation may change the masks)
			mask.orWith(m_upgradeMask);
			if (keep)
			{
				mask.orWith(keep->getUpgradeMask()); // RW 0x69378C (keep + 0x28C)
			}
			mux->attemptUpgrade(mask);
		}
		mux->postUpgradeCheck();
	}
}

// ZH Object::onDestroy (Object.cpp:746-768)
void Object::friend_onDestroy()
{
	if (m_containedBy)
	{
		if (ContainModuleInterface *c = m_containedBy->getContain())
		{
			c->removeFromContain(this);
		}
		m_containedBy = nullptr;
	}
	removeFromPlayerCommandPoints(); // RW 0x69031F (the producer's AI notification before it is the AI lane's)
	for (std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		m->onDelete();
	}
}


// ---------------------------------------------------------------------------------------------------------------------------------
// lane COMBAT-1
// ---------------------------------------------------------------------------------------------------------------------------------
struct Object::PendingDamage
{
	DamageInfo info;
};

bool Object::hasAnyWeapon() const
{
	return m_weapons && m_weapons->hasAnyWeapon();
}

Relationship Object::getRelationship(const Object &other) const
{
	if (!m_team || !other.m_team)
	{
		return NEUTRAL;
	}
	return m_team->getRelationship(other.m_team);
}

void Object::setArmorSetFlag(int bit, bool on)
{
	if (bit < 0 || bit >= 32)
	{
		throw std::logic_error("Object::setArmorSetFlag: bit out of range");
	}
	if (on)
	{
		m_armorSetFlags |= (1u << bit);
	}
	else
	{
		m_armorSetFlags &= ~(1u << bit);
	}
}

void Object::setWeaponBonusCondition(int bit, bool on)
{
	if (bit < 0 || bit >= WEAPONBONUS_CONDITION_COUNT)
	{
		throw std::logic_error("Object::setWeaponBonusCondition: bit out of range");
	}
	if (on)
	{
		m_weaponBonusMask |= (1u << bit);
	}
	else
	{
		m_weaponBonusMask &= ~(1u << bit);
	}
}

// RW 0x698E7D
void Object::attemptDamage(DamageInfo &info)
{
	if (testStatus((unsigned)CombatNames::statuses().updatingAI))
	{
		info.m_input.m_delay = 1.0f; // a hit made while the AI of this object runs waits one frame
	}
	if (info.m_input.m_delay > 0.0f)
	{
		m_pendingDamage.push_back(PendingDamage{ info }); // RW 0x695031: appended
		return;
	}
	doAttemptDamage(info);
}

// RW 0x697E50 doAttemptDamage: alive (+ 0x458 bit 0 clear): the body (+ 0x25C) slot 0; then the shockwave handler RW 0x6968BC when the object is still alive, or
// when it is dead and the hit's shockwave amount (D+0x40) is above 0 (lane COMBAT-4). The drawable's refresh after it (RW 0x697E8F: + 0x84, + 0x43C) is client
void Object::doAttemptDamage(DamageInfo &info)
{
	if (!m_effectivelyDead && m_body)
	{
		m_body->attemptDamage(info);
		if (m_client && info.m_output.m_actualDamageDealt > 0.0f &&
			info.m_input.m_damageType != DAMAGE_HEALING && info.m_input.m_damageType != DAMAGE_PENALTY &&
			info.m_input.m_deathType != DEATH_FADED && info.m_input.m_sourceID != INVALID_ID && info.m_input.m_sourceID != getID())
		{
			m_client->observerEvent(*this, ObserverEvent::Damage, nullptr, {}, info.m_output.m_actualDamageDealt);
		}
	}
	if (!m_effectivelyDead || info.m_input.m_shockWaveAmount > 0.0f)
	{
		const bool notifyDrawable = ObjectKnockback::shockWave(*this, info);
		// lane RADAR-1: the tail of RW 0x6968BC hands the hit to the drawable (RW 0x696E4D -> RW 0x67B4B7), whose radar half is: not for a template both
		// UNATTACKABLE (+ 0x10E & 0x40) and MOVE_ONLY (+ 0x118 & 0x40); the damage dealt (D + 0x70) above 0.0f (RW 0xC1B594); not a PENALTY (10) or HEALING (7)
		// hit, not the death type FADED (0x16), not the sub type 2; the controlling player's bit not in the source mask; ShouldPlayUnderAttackEva (D + 0x25).
		// The local player and the radar list (Object + 0x268) are the client's tests (Radar::noteAttacks); INFERENCE (stop S-2457): the notice is reached
		// whenever the handler runs
		static const int kUnattackable = ObjectTemplateInfoBuilder::kindOfIndex("UNATTACKABLE"), kMoveOnly = ObjectTemplateInfoBuilder::kindOfIndex("MOVE_ONLY");
		const Player *owner = getControllingPlayer();
		const bool exempt = kUnattackable >= 0 && kMoveOnly >= 0 && isKindOf((unsigned)kUnattackable) && isKindOf((unsigned)kMoveOnly);
		if (!exempt && owner && info.m_output.m_actualDamageDealt > 0.0f && info.m_input.m_damageType != DAMAGE_PENALTY && info.m_input.m_damageType != DAMAGE_HEALING &&
		    info.m_input.m_deathType != DEATH_FADED && info.m_input.m_damageSubType != 2 && (info.m_input.m_sourcePlayerMask & (1u << (owner->getPlayerIndex() & 31))) == 0)
		{
			if (info.m_input.m_shouldPlayUnderAttackEva)
			{
				m_radarAttackFrame = m_logic.getFrame();
			}
			// RW 0x67B78C: the HERO effect is outside ShouldPlayUnderAttackEva and the radar-list gate.
			if (notifyDrawable && m_client && owner == m_logic.players().getLocalPlayer() && isKindOfName("HERO"))
			{
				m_client->heroAttacked(*this);
			}
		}
	}
}

// RW 0x690532: a HEALING hit whose source is the healed object itself
void Object::attemptHealing(float amount, const Object *source)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_HEALING;
	info.m_input.m_amount = amount;
	info.m_input.m_sourceID = source ? source->getID() : getID();
	if (m_body)
	{
		m_body->attemptHealing(info);
	}
}

// RW 0x68C234 / RW 0x68C7E9 (see Object.h; lane DECOMP-1)
void Object::setShroudClearingRange(float range)
{
	ShroudManager *sm = m_logic.shroud();
	const float current = m_hasShroudClearingRange ? m_shroudClearingRange : (sm ? ShroudManager::templateVision(*static_cast<const ThingTemplate *>(m_template)->getFinalOverride()).shroudClearingRange : 0.0f);
	if (range == current) // `ucomiss; jnp`: an unordered compare stores too
	{
		return;
	}
	m_hasShroudClearingRange = true;
	m_shroudClearingRange = range;
	if (sm)
	{
		sm->markDirty(*this, true);
	}
}

void Object::updateShroudNow()
{
	if (ShroudManager *sm = m_logic.shroud())
	{
		sm->updateNow(*this);
	}
}

// RW 0x690584 (see Object.h). The tail of RW (template byte + 0x642: clear the RUBBLE model condition, and for a dead object set REALLYDAMAGED and revive it,
// RW 0x6905F8 ff) is not ported: the field's name is not read (stop S-652); no retail template reaches it through this lane's callers while alive
bool Object::attemptHealingFromSoleBenefactor(float amount, const Object *source, UnsignedInt duration)
{
	if (!source)
	{
		return false;
	}
	const UnsignedInt now = m_logic.getFrame();
	const bool swarm = source->isKindOfName("SWARM_DOZER");
	if (!(m_soleHealingBenefactorExpiry < now || m_soleHealingBenefactorID == source->getID() || swarm))
	{
		return false;
	}
	if (!swarm)
	{
		m_soleHealingBenefactorID = source->getID();
		m_soleHealingBenefactorExpiry = now + duration;
	}
	if (m_body)
	{
		DamageInfo info;
		info.m_input.m_sourceID = source->getID();
		info.m_input.m_damageType = DAMAGE_HEALING;
		info.m_input.m_deathType = DEATH_NONE;
		info.m_input.m_amount = amount;
		m_body->attemptHealing(info);
	}
	return true;
}

// RW 0x697EB6: each hit's delay drops by 1.0f per frame; below 0 it applies and leaves the list (list order, the list may grow while it applies)
void Object::updatePendingDamage()
{
	for (size_t i = 0; i < m_pendingDamage.size();)
	{
		DamageInfo info = m_pendingDamage[i].info;
		float delay = info.m_input.m_delay;
		if (PendingDamageDue(delay))
		{
			m_pendingDamage.erase(m_pendingDamage.begin() + (long)i);
			info.m_input.m_delay = 0.0f;
			doAttemptDamage(info); // RW 0x697EEA
		}
		else
		{
			m_pendingDamage[i].info.m_input.m_delay = delay;
			++i;
		}
	}
}

size_t Object::pendingDamageCount() const
{
	return m_pendingDamage.size();
}

// ZH Object::kill: UNRESISTABLE damage with kill set
void Object::kill(int deathType)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = deathType;
	info.m_input.m_sourceID = getID();
	info.m_input.m_amount = 0.0f;
	info.m_input.m_kill = true;
	info.m_input.m_delay = 0.0f;
	if (!m_effectivelyDead && m_body)
	{
		m_body->attemptDamage(info);
	}
}

// RW 0x6955BC scoreTheKill (lane XP-1 extends the COMBAT-1 port):
//   1. the killer walks up its producers while it is PASS_EXPERIENCE_TO_PRODUCER (RW 0x6955C2 .. 0x6955F4: the producer id must be set, differ from its own and
//      be alive), and everything below credits that object;
//   2. a victim is scored once (RW + 0x456, 0x6955F9); a CREATE_A_HERO killer's statistics (RW 0x69560B .. 0x69564C) are not ported (S-634);
//   3. only score types 1 and 2 go on (every caller but one passes 1); a victim with KindOf IGNORED_IN_GUI is skipped (0x695661..0x69566B);
//   4. the statistics collector counts (RW 0x695684 .. 0x6956CE) are not ported (S-323); the score keeper counts are lane END-1's (the loss before the gates,
//      the kill after them); then the enemy test and the
//      other-owner test (0x6956E8..0x6956F7) gate the bounty (call 0x695743 -> Economy::awardBounty RW 0x6AC06F, killer player present) AND everything after;
//   5. the victim's player, when its template is a playable side (RW 0x69574F), gets the own-guys-die skill points (RW 0x6AB0D0);
//   6. the experience (RW 0x695763): a PASS_EXPERIENCE_TO_CONTAINED killer with a contain gives every contained object the kill (RW 0x695786: contain vslot
//      0x110 with the callback RW 0x6955A6; the list order is INFERENCE), else the PASS_EXPERIENCE_TO_CONTAINER killer's container, else the killer, takes it
//      (grantExperienceForKill(victim, false, 1.0f)).
void Object::scoreTheKill(Object &victim, int scoreType)
{
	Object *killer = this;
	while (killer->isKindOfName("PASS_EXPERIENCE_TO_PRODUCER"))
	{
		if (killer->m_producerID == INVALID_ID || killer->m_producerID == killer->m_id)
		{
			break;
		}
		Object *producer = m_logic.findObjectByID(killer->m_producerID);
		if (!producer)
		{
			break;
		}
		killer = producer;
	}
	if (victim.m_scored)
	{
		return;
	}
	victim.m_scored = true;
	if (killer->isKindOfName("CREATE_A_HERO") && m_logic.clientHooks()) m_logic.clientHooks()->createAHeroKill(*killer, victim);
	if (scoreType != 1 && scoreType != 2)
	{
		return;
	}
	if (victim.isKindOfName("IGNORED_IN_GUI"))
	{
		return;
	}
	Player *victimOwner = victim.getControllingPlayer();
	Player *owner = killer->getControllingPlayer();
	// lane END-1: RW 0x6956D1 .. 0x6956DE: the victim's player counts the loss (ScoreKeeper::addObjectLost RW 0x79F486) before the gates
	if (victimOwner)
	{
		victimOwner->getScoreKeeper().addObjectLost(m_logic, victim);
	}
	if (killer->getRelationship(victim) != ENEMIES || owner == victimOwner)
	{
		return;
	}
	// lane END-1: RW 0x69572F .. 0x695738: the killer's player counts the kill (ScoreKeeper::addObjectDestroyed RW 0x79F303); the per-object kill counters
	// (RW 0x695701 .. 0x695729, Object + 0x48C / + 0x490) are not ported (S-1061)
	if (owner)
	{
		owner->getScoreKeeper().addObjectDestroyed(m_logic, victim);
	}
	if (owner)
	{
		const int paid = m_logic.economy().awardBounty(*owner, killer, victim);
		if (paid > 0)
		{
			m_logic.combat().counters().bountyPaid += (unsigned long long)paid;
		}
	}
	if (victimOwner && victimOwner->getPlayerTemplate() && victimOwner->getPlayerTemplate()->m_evil) // RW 0x69574F: template + 0x1BC is Evil (PlayerTemplate table RW 0xBF84F8; PlayableSide is + 0x151)
	{
		m_logic.experience().awardSkillPointsForLoss(*victimOwner, killer, victim);
	}
	if (killer->isKindOfName("PASS_EXPERIENCE_TO_CONTAINED") && killer->m_contain)
	{
		const ContainModuleInterface::ContainedItemsList *items = killer->m_contain->getContainedItemsList();
		if (items)
		{
			const std::vector<Object *> list(items->begin(), items->end());
			for (Object *o : list)
			{
				o->grantExperienceForKill(victim, false, 1.0f);
			}
		}
		return;
	}
	Object *target = killer;
	if (killer->isKindOfName("PASS_EXPERIENCE_TO_CONTAINER") && killer->m_containedBy)
	{
		target = killer->m_containedBy;
	}
	target->grantExperienceForKill(victim, false, 1.0f);
}

// RW 0x695475
void Object::grantExperienceForKill(Object &victim, bool flag, float scale)
{
	ExperienceTracker *tracker = m_experienceTracker.get();
	if (!tracker || !tracker->isAcceptingExperiencePoints())
	{
		return;
	}
	if (victim.isUnderConstruction()) // RW 0x69549E
	{
		return;
	}
	const ExperienceTracker *victimTracker = victim.getExperienceTracker();
	if (!victimTracker)
	{
		return;
	}
	float value = SimMath::mulf32((float)victimTracker->getExperienceValue(*this, flag), scale); // cvtsi2ss; mulss scale
	float mult = 1.0f;
	if (attributeModifierProduct(ATTRIBUTE_EXPERIENCE, nullptr, true, mult)) // RW 0x6954D7 .. 0x6954FF
	{
		value = SimMath::mulf32(mult, value);
	}
	Object *horde = getHordeObject(false); // RW 0x694BF8
	HordeContainInterface *hordeContain = nullptr;
	if (horde && horde->m_contain)
	{
		hordeContain = horde->m_contain->getHordeContainInterface();
	}
	if (hordeContain)
	{
		float share = value;
		if (horde->m_experienceTracker) // RW 0x69551F .. 0x69553C
		{
			share = horde->m_experienceTracker->scaleForRank(value);
		}
		hordeContain->addExperience(this, share); // HordeContain vslot 0xC0 (RW 0x873A02)
	}
	else
	{
		tracker->addExperiencePoints(value, true, true, true, false); // RW 0x695566
	}
	if (value > 0.0f) // RW 0x69556B
	{
		m_experienceFlags |= tracker->hasGainedLevel() ? 1u : 0u;
		// RW 0x695589 .. 0x69559E: the object's experience listener (RW 0x88305B, lane HERO-2: ShareExperienceBehavior) shares the gain
		if (ShareExperienceBehavior *share = ShareExperienceBehavior::of(*this))
		{
			++m_logic.experience().counters().listenerCalls;
			share->share(value);
		}
	}
}

// RW 0x889141
bool Object::friend_allowUndeadKill(int damageSubType, unsigned now)
{
	if (damageSubType != 3)
	{
		return true;
	}
	if (m_undeadKillFrame + 5 >= now) // `jae`: unsigned
	{
		return false;
	}
	m_undeadKillFrame = now;
	return true;
}

// RW 0x694569 (see Object.h; lane DECOMP-1)
void Object::setStatusAcrossHorde(unsigned bit, bool set)
{
	Object *horde = getHordeObject(false);
	if (!horde)
	{
		setStatus(bit, set);
		return;
	}
	ContainModuleInterface *c = horde->getContain();
	if (!c)
	{
		return;
	}
	if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
	{
		for (Object *m : *items)
		{
			m->setStatus(bit, set);
		}
	}
	horde->setStatus(bit, set);
}

Object *Object::getHordeObject(bool alsoProducer) const
{
	// lane BIGBATTLE-1: the HORDE bit looked up once (isKindOfName searched the KindOf names on each of up to three tests; the client asks this
	// for every unit in a big battle). The name table is the engine's, fixed for the process
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const auto isHorde = [](const Object &o) { return kHorde >= 0 && o.isKindOf((unsigned)kHorde); };
	if (isHorde(*this))
	{
		return const_cast<Object *>(this);
	}
	if (m_containedBy && isHorde(*m_containedBy))
	{
		return m_containedBy;
	}
	if (alsoProducer)
	{
		Object *producer = m_logic.findObjectByID(m_producerID);
		if (producer && isHorde(*producer))
		{
			return producer;
		}
	}
	return nullptr;
}

// RW 0x68F1A8
bool Object::addAttributeModifier(const std::string &listName, int duration)
{
	if (listName.empty())
	{
		return false;
	}
	if (m_contain) // RW 0x68F1CD: contain vslot 0x7C, then HordeContain vslot 0x1D8 (RW 0x870E50)
	{
		if (HordeContainInterface *h = m_contain->getHordeContainInterface())
		{
			h->addAttributeModifier(listName, duration);
			return true;
		}
	}
	AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(attributeModifierPoolModule());
	if (!pool)
	{
		return false;
	}
	// RW 0x68F207 .. 0x68F243: an owned object with CommandPoints (template + 0x62C) > 0 leaves its player's command point count for the add and rejoins it
	// only when the add succeeded (a failed add leaves it out, like RW)
	Player *player = getControllingPlayer();
	const bool counted = player && Economy::templateInt(*static_cast<const ThingTemplate *>(getTemplate()), "CommandPoints") > 0;
	if (counted)
	{
		m_logic.economy().objectLost(*player, *this);
	}
	if (!pool->add(listName, duration))
	{
		return false;
	}
	if (counted)
	{
		m_logic.economy().objectGained(*player, *this);
	}
	return true;
}

void Object::removeAttributeModifier(const std::string &listName)
{
	if (listName.empty()) // RW 0x68F276: the empty name key
	{
		return;
	}
	if (m_contain) // RW 0x68F27A: contain vslot 0x7C, then HordeContain vslot 0x1DC (RW 0x870F75) with no filter
	{
		if (HordeContainInterface *h = m_contain->getHordeContainInterface())
		{
			h->removeAttributeModifier(listName);
			return;
		}
	}
	AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(attributeModifierPoolModule()); // RW 0x68C4A6
	if (!pool)
	{
		return;
	}
	// RW 0x68F2AE .. 0x68F2E5: an owned object with CommandPoints > 0 leaves its player's command point count around the removal (as the add)
	Player *player = getControllingPlayer();
	const bool counted = player && Economy::templateInt(*static_cast<const ThingTemplate *>(getTemplate()), "CommandPoints") > 0;
	if (counted)
	{
		m_logic.economy().objectLost(*player, *this);
	}
	pool->remove(listName); // RW 0x8052FB
	if (counted)
	{
		m_logic.economy().objectGained(*player, *this);
	}
}

bool Object::attributeModifierSum(int type, const char *name, float &out) const
{
	const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(attributeModifierPoolModule());
	if (!pool)
	{
		return false;
	}
	return pool->sum(type, name, out);
}

bool Object::attributeModifierProduct(int type, const char *name, bool innate, float &out) const
{
	const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(attributeModifierPoolModule());
	if (!pool)
	{
		return false;
	}
	return pool->product(type, name, innate, out);
}

void Object::crc(StateHasher &h) const
{
	crcOmittingModules(h, nullptr);
}

void Object::crcOmittingModules(StateHasher &h, const std::function<bool(const BehaviorModule &)> &omit) const
{
	h.addU32(m_id);
	h.addU32(m_template ? (std::uint32_t)static_cast<const ThingTemplate *>(m_template)->getTemplateID() : 0u);
	h.addU32(m_team ? m_team->getID() : 0u);
	h.addU32(m_originalTeamID); // lane HERO-2 / BACKLOG4: the team setTeam last recorded
	h.addU32(m_speechLeader);                                 // lane HERO-2
	h.addBool(m_receivingDifficultyBonus);                    // lane CAMP-1H
	if (m_legendaryFactor != 1)
	{
		h.addI32(m_legendaryFactor); // lane RULES-1: only a legendary hero adds it (every other object keeps its hash)
	}
	h.addU32(m_capturerID);                                   // lane HERO-2
	h.addU32(m_undeadKillFrame);                              // lane HERO-2
	for (std::uint32_t w : m_status)
	{
		h.addU32(w);
	}
	h.addU32(m_disabled);
	if (m_scriptStatus != 0)
	{
		h.addU32(0x5C000000u | m_scriptStatus); // lane SCRIPT-2: hashed only when set (a game without scripts keeps its hash)
	}
	if (m_scriptSelectable != isKindOfName("SELECTABLE"))
	{
		h.addU32(m_scriptSelectable ? 0x5E1Fu : 0x5E1Eu); // hash a change from the template-derived default, as for the other script fields
	}
	if (!m_geometryActive.empty()) // lane HUD-5: the object's own shape flags (a gate's open / closed geometry), hashed once changed
	{
		h.addU32(0x6E000000u | (std::uint32_t)m_geometryActive.size());
		for (std::uint8_t a : m_geometryActive)
		{
			h.addU32(a);
		}
	}
	if (m_triggerCount != 0 || m_enteredOrExitedFrame != 0) // lane SCRIPT-2: the trigger tracking, once the object met a trigger
	{
		h.addU32(0x7A000000u | (std::uint32_t)m_triggerCount);
		h.addU32(m_enteredOrExitedFrame);
		h.addU32((std::uint32_t)m_triggerCellX);
		h.addU32((std::uint32_t)m_triggerCellY);
		for (int i = 0; i < m_triggerCount; ++i)
		{
			const TriggerEntry &e = m_triggers[i];
			h.addU32((std::uint32_t)e.trigger);
			h.addU32((e.entered ? 1u : 0u) | (e.exited ? 2u : 0u) | (e.inside ? 4u : 0u));
		}
	}
	for (std::uint32_t w : m_upgradeMask.words) // UPGRADE-1
	{
		h.addU32(w);
	}
	h.addFloat(m_upgradeCostPaid);
	h.addString(m_commandSetOverride);
	for (std::uint32_t w : m_modelCondition)
	{
		h.addU32(w);
	}
	for (unsigned i = 0; i < DISABLED_TYPE_COUNT; ++i)
	{
		h.addU32(m_disabledExpire[i]);
	}
	h.addU32(m_ignoreAIExpire);
	if (m_weaponlessSetFlags != WeaponConditionFlags{}) // lane HUD-4: hashed only when set (the hash of a game that never toggles is unchanged)
	{
		h.addU32(0x38C00000u);
		for (std::uint32_t w : m_weaponlessSetFlags)
		{
			h.addU32(w);
		}
	}
	if (m_hasShroudClearingRange) // lane DECOMP-1: hashed only when set (a game whose objects keep their template's range keeps its hash)
	{
		h.addU32(0x1B400000u);
		h.addFloat(m_shroudClearingRange);
	}
	h.addU32(m_noCollisionsExpire);
	h.addU32(m_creationFrame);
	h.addU32(m_containedBy ? m_containedBy->getID() : 0u);
	h.addBool(m_inWorld);          // lane GARRISON-1: RW + 0x474
	h.addU32(m_containedFrame);    // RW + 0x284
	h.addBool(m_drawableHidden);
	h.addU32(m_producerID);
	h.addU32(m_builderID); // BUILD-2
	h.addU32(m_soleHealingBenefactorID);
	h.addU32(m_soleHealingBenefactorExpiry);
	h.addI32(m_creationSeed);
	h.addString(m_name); // the map object's name (scripts find objects by it)
	h.addFloat(m_instanceScale); // RENDER-2: the launch-bone inputs the logic owns
	for (std::uint32_t w : m_placementConditions)
	{
		h.addU32(w);
	}
	h.addBool(m_commandPointsCounted);
	h.addFloat(m_buildCostPaid);
	h.addFloat(m_constructionPercent);
	if (m_experienceTracker) // lane XP-1
	{
		m_experienceTracker->crc(h);
	}
	h.addBool(m_scored);
	h.addU32(m_experienceFlags);
	const Coord3D &p = *getPosition();
	h.addFloat(p.x);
	h.addFloat(p.y);
	h.addFloat(p.z);
	h.addFloat(getOrientation());
	for (int i = 0; i < 9; ++i)
	{
		h.addFloat(getBasis()[i]);
	}
	h.addBool(m_recordedValid);
	h.addU32(m_recordedFrame);
	h.addFloat(m_recordedPosition.x);
	h.addFloat(m_recordedPosition.y);
	h.addFloat(m_recordedPosition.z);
	h.addFloat(m_recordedAngle);
	for (int i = 0; i < 9; ++i)
	{
		h.addFloat(m_recordedBasis[i]);
	}
	h.addBool(m_previousValid);
	h.addFloat(m_previousPosition.x);
	h.addFloat(m_previousPosition.y);
	h.addFloat(m_previousPosition.z);
	h.addU32((std::uint32_t)m_modules.size());
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		if (omit && omit(*m))
		{
			continue;
		}
		h.addU32(m->getModuleClassHash());
		m->crc(h);
	}
	// COMBAT-1
	h.addBool(m_effectivelyDead);
	h.addU32(m_armorSetFlags);
	h.addU32(m_weaponBonusMask);
	h.addU32((std::uint32_t)m_pendingDamage.size());
	for (const PendingDamage &pd : m_pendingDamage)
	{
		const DamageInfoInput &in = pd.info.m_input;
		h.addU32(in.m_sourceID);
		h.addU32(in.m_sourcePlayerMask);
		h.addI32(in.m_damageType);
		h.addI32(in.m_damageFXOverride);
		h.addI32(in.m_damageSubType);
		h.addI32(in.m_deathType);
		h.addFloat(in.m_amount);
		h.addBool(in.m_kill);
		h.addBool(in.m_shouldPlayUnderAttackEva);
		h.addFloat(in.m_delay);
		h.addI32(in.m_fxTrigger);
		// lane COMBAT-4: the shockwave half, only for a hit that carries one (an ordinary hit's hash stream is unchanged)
		if (in.m_shockWaveAmount != 0.0f || in.m_shockWaveRadius != 0.0f)
		{
			h.addU32(in.m_shockWaveSourceID);
			h.addFloat(in.m_shockWaveVector.x);
			h.addFloat(in.m_shockWaveVector.y);
			h.addFloat(in.m_shockWaveVector.z);
			h.addFloat(in.m_shockWaveAmount);
			h.addFloat(in.m_shockWaveRadius);
			h.addFloat(in.m_shockWaveTaperOff);
			h.addFloat(in.m_shockWaveZMult);
			h.addBool(in.m_shockWaveClearRadius);
			h.addFloat(in.m_shockWaveClearMult);
			h.addFloat(in.m_shockWaveClearFlingHeight);
			h.addFloat(in.m_shockWaveClearCenter.x);
			h.addFloat(in.m_shockWaveClearCenter.y);
			h.addFloat(in.m_shockWaveClearCenter.z);
			h.addFloat(in.m_cyclonicFactor);
		}
	}
	h.addBool(m_weapons != nullptr);
	if (m_weapons)
	{
		m_weapons->crc(h);
	}
}

void Object::onConstructionStatusChanged()
{
	if (ShroudManager *shroud = m_logic.shroud())
	{
		shroud->markDirty(*this, true);
	}
}

// lane SCRIPT-1 (see Object.h)
void Object::setName(const std::string &name)
{
	m_name = name;
	m_logic.scriptEngine().objectNamed(*this);
}

// lane SCRIPT-2: RW 0x69317D (see Object.h). The call RW 0x68C213 before each disabled change (the partition / drawable notification of the disabled
// change) is what setDisabled / clearDisabled already do here
void Object::setScriptStatus(std::uint8_t bits, bool set)
{
	const std::uint8_t old = m_scriptStatus;
	m_scriptStatus = set ? (std::uint8_t)(old | bits) : (std::uint8_t)(old & ~bits);
	const std::uint8_t changed = (std::uint8_t)(old ^ m_scriptStatus);
	if (changed & 1u)
	{
		if (m_scriptStatus & 1u)
		{
			setDisabled(9, UPDATE_SLEEP_FOREVER);
		}
		else
		{
			clearDisabled(9);
		}
	}
	if (changed & 2u)
	{
		if (m_scriptStatus & 2u)
		{
			setDisabled(10, UPDATE_SLEEP_FOREVER);
		}
		else
		{
			clearDisabled(10);
		}
	}
}

// ---- lane SAVE-4b: the object's snapshot (see Object.h) ---------------------------------------------------------------------------------------------------
// OpenBFME native layout (S-4190): RotWK's Object::xfer (RW 0x697F28, (1, 29)) transfers RotWK's object (its drawable id, the formation, the living-world
// army, ...); this Object is the port's subset, so its own fields follow its crc's order with the state the crc leaves out (the geometry version, the radar
// frame, the trigger cell, ...). Its modules are retail's loop: a count (u16), then per module its tag name and a block with the module's xfer
// (RW 0x698775 .. 0x6987A5). OpenBFME is strict where retail skips: the snapshot's modules must be this object's, in its order (S-4191).
// Not saved, rebuilt: the KindOf mask, the helper count and the module interface caches (the template's, at construction), the client hooks, the list links
// (the Objects block's order) and the team links (the TeamFactory's post-process).
void Object::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	// a load never reads its destination: the save-side values are built from the members only when saving (SAVES.md, rules for xfer authors)
	const bool saving = !x.isLoading();
	ObjectID id = saving ? m_id : (ObjectID)INVALID_ID;
	x.xferObjectID(&id);
	std::string templateName = saving ? m_template->getName() : std::string();
	x.xferAsciiString(&templateName);
	TeamID team = saving && m_team ? m_team->getID() : TEAM_ID_INVALID;
	x.xferUnsignedInt(&team);
	if (x.isLoading() && (id != m_id || templateName != m_template->getName() || team != (m_team ? m_team->getID() : TEAM_ID_INVALID)))
	{
		throw XferException("the snapshot's object " + std::to_string(id) + " '" + templateName + "' of team " + std::to_string(team) + " is loaded into object "
			+ std::to_string(m_id) + " '" + m_template->getName() + "'");
	}
	// the transform first: setting it runs reactToTransformChange (the trigger areas, the shroud and partition marks), whose effects the fields below overwrite
	Coord3D pos = saving ? *getPosition() : Coord3D{};
	float angle = saving ? getOrientation() : 0.0f;
	float basis[9] = {};
	for (int i = 0; i < 9 && saving; ++i)
	{
		basis[i] = getBasis()[i];
	}
	SnapshotXfer::coord3D(x, pos);
	x.xferReal(&angle);
	for (float &b : basis)
	{
		x.xferReal(&b);
	}
	if (x.isLoading())
	{
		friend_setTransformExact(pos, angle, basis);
	}
	// the weapons next: loading selects the weapon set the flags select, which clears the weapons' model conditions (the conditions below are the saved ones)
	bool hasWeapons = saving && m_weapons != nullptr;
	x.xferBool(&hasWeapons);
	if (hasWeapons != (m_weapons != nullptr))
	{
		throw XferException(std::string("the snapshot's object ") + std::to_string(m_id) + (hasWeapons ? " has" : " lacks") + " weapons, this one "
			+ (m_weapons ? "has" : "lacks") + " them");
	}
	if (m_weapons)
	{
		m_weapons->xfer(x);
	}
	// lane SAVE-4e (merge with BACKLOG4): the recorded original team is its prototype (by id, -1: none) and the team id it had when recorded (hashed;
	// the team itself may be gone since, as RW 0x69959D keeps only its name)
	std::int32_t originalProto = saving && m_originalTeamProto ? m_originalTeamProto->getID() : -1;
	x.xferInt(&originalProto);
	std::uint32_t originalID = saving ? m_originalTeamID : 0;
	x.xferUnsignedInt(&originalID);
	if (x.isLoading())
	{
		const TeamPrototype *proto = nullptr;
		for (const std::unique_ptr<TeamPrototype> &p : m_logic.players().teams().prototypes())
		{
			proto = p->getID() == originalProto ? p.get() : proto;
		}
		if (originalProto != -1 && !proto)
		{
			throw XferException("the snapshot's object " + std::to_string(m_id) + " names original team prototype " + std::to_string(originalProto)
				+ ", which this game lacks");
		}
		m_originalTeamProto = proto;
		m_originalTeamID = originalID;
	}
	x.xferObjectID(&m_speechLeader);
	x.xferBool(&m_receivingDifficultyBonus);
	x.xferByte(&m_legendaryFactor);
	SnapshotXfer::checkRange(m_legendaryFactor, LegendaryHeroes::kOff, LegendaryHeroes::kMax, "an object's Legendary Heroes factor");
	x.xferObjectID(&m_capturerID);
	x.xferUnsignedInt(&m_undeadKillFrame);
	SnapshotXfer::maskWords(x, m_status, SnapshotXfer::nameCount(TheObjectStatusNames), "an object's status", "the object status names"); // lane SAVE-4e
	x.xferUnsignedInt(&m_disabled);
	SnapshotXfer::checkRange(m_disabled, 0, (1u << DISABLED_TYPE_COUNT) - 1, "an object's disabled mask");
	SnapshotXfer::words(x, m_disabledExpire);
	x.xferUnsignedInt(&m_ignoreAIExpire);
	SnapshotXfer::maskWords(x, m_weaponlessSetFlags, SnapshotXfer::nameCount(TheWeaponConditionNames), "an object's weapon set flags",
		"the weapon set conditions");
	x.xferUnsignedInt(&m_noCollisionsExpire);
	x.xferUnsignedInt(&m_creationFrame);
	x.xferInt(&m_creationSeed);
	std::string name = saving ? m_name : std::string();
	x.xferAsciiString(&name);
	if (x.isLoading() && name != m_name)
	{
		setName(name); // the script engine's name index (ScriptEngine::objectNamed) follows the name
	}
	std::uint8_t scriptStatus = saving ? m_scriptStatus : 0;
	x.xferUnsignedByte(&scriptStatus);
	SnapshotXfer::checkMask(scriptStatus, 0x1F, "an object's script status", "the five script status bits"); // ZH ObjectScriptStatusBit 1 .. 0x10
	m_scriptStatus = scriptStatus;
	x.xferBool(&m_scriptSelectable);
	x.xferInt(&m_triggerCount);
	if (m_triggerCount < 0 || m_triggerCount > MAX_TRIGGERS_IN)
	{
		throw XferException("the snapshot's object " + std::to_string(m_id) + " is inside " + std::to_string(m_triggerCount) + " triggers");
	}
	for (TriggerEntry &e : m_triggers)
	{
		x.xferInt(&e.trigger);
		SnapshotXfer::checkRange(e.trigger, -1, 0x7FFFFFFF, "an object's trigger entry"); // -1: none; else the script engine's trigger (SAVE-4d)
		x.xferBool(&e.entered);
		x.xferBool(&e.exited);
		x.xferBool(&e.inside);
	}
	x.xferUnsignedInt(&m_enteredOrExitedFrame);
	x.xferInt(&m_triggerCellX);
	x.xferInt(&m_triggerCellY);
	// the shapes' active flags: none (the template's), or one per shape of the template (setGeometryActive), each 0 or 1
	std::vector<std::uint8_t> active = saving ? m_geometryActive : std::vector<std::uint8_t>();
	x.xferVectorSize(active, 1);
	const size_t templateShapes = x.isLoading() ? ObjectGeometry::shapesOf(*getTemplate()).size() : active.size();
	if (!active.empty() && active.size() != templateShapes)
	{
		throw XferException("the snapshot's object " + std::to_string(m_id) + " has " + std::to_string(active.size()) + " shape flags, its template "
			+ std::to_string(templateShapes) + " shapes");
	}
	for (std::uint8_t &a : active)
	{
		x.xferUnsignedByte(&a);
		if (a > 1)
		{
			throw XferException("the snapshot's object " + std::to_string(m_id) + " has a shape flag of " + std::to_string(a));
		}
	}
	m_geometryActive = active;
	x.xferUnsignedInt(&m_geometryVersion);
	SnapshotXfer::maskWords(x, m_upgradeMask.words, TheUpgradeCenter ? TheUpgradeCenter->nextMaskBit() : 0, "an object's upgrades",
		"the upgrades of this game's data");
	x.xferReal(&m_upgradeCostPaid);
	x.xferAsciiString(&m_commandSetOverride);
	SnapshotXfer::maskWords(x, m_modelCondition, SnapshotXfer::nameCount(TheModelConditionNames), "an object's model conditions", "the model conditions");
	x.xferReal(&m_instanceScale);
	SnapshotXfer::maskWords(x, m_placementConditions, SnapshotXfer::nameCount(TheModelConditionNames), "an object's placement conditions", "the model conditions");
	x.xferBool(&m_hasShroudClearingRange);
	x.xferReal(&m_shroudClearingRange);
	x.xferUnsignedInt(&m_radarAttackFrame);
	SnapshotXfer::containerPointer(x, m_logic, m_containedBy, m_id);
	x.xferBool(&m_inWorld);
	x.xferUnsignedInt(&m_containedFrame);
	x.xferBool(&m_drawableHidden);
	x.xferObjectID(&m_producerID);
	x.xferObjectID(&m_builderID);
	x.xferObjectID(&m_soleHealingBenefactorID);
	x.xferUnsignedInt(&m_soleHealingBenefactorExpiry);
	x.xferBool(&m_commandPointsCounted);
	x.xferReal(&m_buildCostPaid);
	x.xferReal(&m_constructionPercent);
	x.xferBool(&m_scored);
	std::uint8_t experienceFlags = saving ? m_experienceFlags : 0;
	x.xferUnsignedByte(&experienceFlags);
	SnapshotXfer::checkMask(experienceFlags, 0x1, "an object's experience flags", "the gained-a-level flag"); // the one bit Object sets (+ 0x49C)
	m_experienceFlags = experienceFlags;
	x.xferBool(&m_effectivelyDead);
	SnapshotXfer::maskWithin(x, m_armorSetFlags, SnapshotXfer::lowBits(SnapshotXfer::nameCount(TheArmorSetNames)), "an object's armor set flags",
		"the " + std::to_string(SnapshotXfer::nameCount(TheArmorSetNames)) + " armor set conditions");
	SnapshotXfer::maskWithin(x, m_weaponBonusMask, SnapshotXfer::lowBits(WEAPONBONUS_CONDITION_COUNT), "an object's weapon bonus conditions",
		"the " + std::to_string(WEAPONBONUS_CONDITION_COUNT) + " conditions");
	x.xferBool(&m_recordedValid);
	x.xferUnsignedInt(&m_recordedFrame);
	SnapshotXfer::coord3D(x, m_recordedPosition);
	x.xferReal(&m_recordedAngle);
	for (float &b : m_recordedBasis)
	{
		x.xferReal(&b);
	}
	x.xferBool(&m_previousValid);
	SnapshotXfer::coord3D(x, m_previousPosition);
	SnapshotXfer::each(x, m_pendingDamage, SnapshotXfer::kDamageInfoBytes, [&](PendingDamage &pd) { SnapshotXfer::damageInfo(x, pd.info); });
	bool hasTracker = saving && m_experienceTracker != nullptr;
	x.xferBool(&hasTracker);
	if (hasTracker != (m_experienceTracker != nullptr))
	{
		throw XferException("the snapshot's object " + std::to_string(m_id) + " differs in its experience tracker");
	}
	if (m_experienceTracker)
	{
		m_experienceTracker->xfer(x);
	}
	// the modules (RW 0x697F28's loop): u16 count, then per module the tag and a block
	std::uint16_t count = saving ? (std::uint16_t)m_modules.size() : 0;
	x.xferUnsignedShort(&count);
	if (count != m_modules.size())
	{
		throw XferException("the snapshot's object " + std::to_string(m_id) + " '" + m_template->getName() + "' has " + std::to_string(count)
			+ " modules, this one " + std::to_string(m_modules.size()));
	}
	for (const std::unique_ptr<BehaviorModule> &m : m_modules)
	{
		std::string tag = saving ? m->getModuleTagName() : std::string();
		x.xferAsciiString(&tag);
		if (tag != m->getModuleTagName())
		{
			throw XferException("the snapshot's object " + std::to_string(m_id) + " '" + m_template->getName() + "' has the module '" + tag + "' where this one has '"
				+ m->getModuleTagName() + "'");
		}
		if (m_logic.snapshotOmitsModule(*m))
		{
			// tests only (GameLogic::setSnapshotModuleFilter): a module another lane's xfer will cover leaves an empty block
			if (x.isLoading())
			{
				x.skipBlock();
			}
			else
			{
				x.beginBlock();
				x.endBlock();
			}
			continue;
		}
		if (m->xferClass() == typeid(Module))
		{
			m->xfer(x); // Module::xfer: the named "has no xfer" refusal
		}
		if (m->xferClass() != typeid(*m))
		{
			throw XferException("the module class " + m->getModuleClassName() + " (tag '" + tag + "') of '" + m_template->getName()
				+ "' has no xfer of its own (it would inherit its base class's)");
		}
		x.beginBlock();
		m->xfer(x);
		x.endBlock();
	}
}
