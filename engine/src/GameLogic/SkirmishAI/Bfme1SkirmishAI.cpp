// OpenBFME. GPL-3.0.
// BFME1 only. Reading aid: Open-BFME-1 GameLogic/AI/AIPlayer.cpp,
// AIPlayerSelectTeamToBuild.cpp (RVA 0x00163220), AIPlayerQueueUnits.cpp,
// AIPlayerOnUnitProduced.cpp and Common/RTS/TeamPrototypeScripts.cpp.
// The faction libraries supply the strategy, unit lists and team scripts;
// no RotWK ArmyDefinition or invented faction build order is used here.
#include "GameLogic/SkirmishAI/Bfme1SkirmishAI.h"
#include "Common/BuildAssistant.h"
#include "Common/NumericState.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/Module/StructureModules.h"
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/LivingWorld/StlportAlgorithm.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/UpgradeTypes.h"
#include <algorithm>

namespace
{
const ScriptParameter &param(const ScriptActionRec &a, size_t i) { return a.params.at(i); }

// BFME1 AIRampageState: ctor 0x001719F0, vtable 0x00C984D8.
// Registered only by the BFME1 AI variant; later editions retain their stop.
class Bfme1RampageState final : public AIState
{
	unsigned m_end = 0, m_eject = 0;
	bool m_firing = false;
public:
	explicit Bfme1RampageState(AIStateMachine &m) : AIState(m, "AIRampageState") {}
	StateReturnType onEnter() override
	{
		// 0x0017EA20 / duration accessor 0x00279750. Module times are
		// parseDurationUnsignedInt milliseconds -> ceil(ms * 0.005f).
		unsigned duration = 0, eject = 0;
		bool requiresAflame = false;
		if (const auto *raw = dynamic_cast<const RawModuleData *>(ai().getModuleData()))
			for (const auto &line : raw->lines())
			{
				std::istringstream input(line.text); std::string name, equal, value;
				if (!(input >> name >> equal >> value) || equal != "=") continue;
				if (AsciiStringUtil::compareNoCase(name, "RampageRequiresAflame") == 0)
					requiresAflame = AsciiStringUtil::compareNoCase(value, "Yes") == 0;
				else if (AsciiStringUtil::compareNoCase(name, "RampageTime") == 0)
					duration = NumericState::ceilScaled((std::uint32_t)std::strtoul(value.c_str(), nullptr, 10), 0.005f);
				else if (AsciiStringUtil::compareNoCase(name, "TimeToEjectPassengersOnRampage") == 0)
					eject = NumericState::ceilScaled((std::uint32_t)std::strtoul(value.c_str(), nullptr, 10), 0.005f);
			}
		if (requiresAflame && !owner().testStatus((unsigned)CombatNames::status("AFLAME"))) duration = 0;
		ai().chooseLocomotorSet(LOCOMOTORSET_ENRAGED);
		Coord3D position = *owner().getPosition();
		auto &world = *owner().logic().aiWorld();
		world.pathfinder().removeGoal(world.adapterFor(owner()));
		world.pathfinder().snapPosition(world.adapterFor(owner()), &position);
		ai().setPreventPlayerCommands(true);
		owner().setWeaponSetFlag(CombatNames::weaponSetBit("RAMPAGE"), true);
		owner().setStatus((unsigned)CombatNames::status("UNSELECTABLE"), true);
		owner().setStatus((unsigned)CombatNames::status("RAMPAGING"), true);
		const unsigned now = owner().logic().getFrame();
		m_end = now + duration;
		m_eject = eject ? (duration ? now + eject : now) : 0xFFFFFFFFu;
		owner().logic().noteStop("[S-5017] BFME1 rampage: selection notification, script-entry module guard and RunOffMap completion/last-damager scoring remain unverified");
		return STATE_CONTINUE;
	}
	StateReturnType update() override
	{
		// 0x0017EC10: the rampage weapon fires at the owner's own
		// position. Its first READY tick arms firing; the next fires.
		if (auto *weapons = owner().getWeapons())
			if (auto *weapon = weapons->currentWeapon())
			{
				const int status = weapons->weaponStatus(*weapon);
				if (m_firing)
				{
					if (status == WEAPON_READY_TO_FIRE)
					{
						weapons->setFiringConditionForCurrentWeapon();
						weapons->fireCurrentWeapon(nullptr, owner().getPosition());
					}
					else if (status == WEAPON_FIRING) m_firing = false;
				}
				else if (status == WEAPON_READY_TO_FIRE)
				{
					m_firing = true;
					owner().setStatus((unsigned)CombatNames::status("IS_FIRING_WEAPON"), true);
				}
			}
		// 0x00184FE0: the contain's +0xE4 virtual arms the sequential
		// falling pass. A zero eject period disables this call entirely.
		const unsigned now = owner().logic().getFrame();
		if (m_eject <= now)
		{
			if (auto *contain = dynamic_cast<HordeTransportContain *>(owner().getContain())) contain->armRidersFalling();
			m_eject = 0xFFFFFFFFu;
		}
		if (m_end <= now)
		{
			// Object completion 0x001CADC0 dispatches RunOffMapBehavior
			// when present. Never substitute the no-module kill branch.
			if (owner().findModule("RunOffMapBehavior"))
				throw std::runtime_error("[S-5017] BFME1 rampage RunOffMapBehavior completion is not implemented");
			owner().kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
			return STATE_SUCCESS;
		}
		return STATE_CONTINUE;
	}
	void onExit(StateExitType) override
	{
		if (ai().isDestroying()) return;
		// 0x0017EB90: UNSELECTABLE intentionally remains set.
		owner().setStatus((unsigned)CombatNames::status("RAMPAGING"), false);
		owner().setWeaponSetFlag(CombatNames::weaponSetBit("RAMPAGE"), false);
		ai().setPreventPlayerCommands(false);
		owner().setModelConditionState(CombatNames::modelCondition("CHARGING"), true);
		owner().kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
	}
	void crc(StateHasher &h) const override { h.addU32(m_end); h.addU32(m_eject); h.addBool(m_firing); }
	void crcPersistent(StateHasher &h) const override { h.addBool(m_firing); }
	bool hasPersistentCrc() const override { return true; }
	void xfer(Xfer &x) override
	{
		XferVersion version{1, 1}; x.xferVersion(version);
		x.xferUnsignedInt(&m_end); x.xferUnsignedInt(&m_eject); x.xferBool(&m_firing);
	}
};

class Bfme1TransportMoveState : public AIState
{
	bool m_depart;
	Coord3D m_origin{}, m_goal{};
	bool m_append = false;
	std::unique_ptr<AIMoveToState> m_move;
public:
	Bfme1TransportMoveState(AIStateMachine &m, bool depart)
		: AIState(m, depart ? "AIMoveAndDeleteState" : "AIMoveAndEvacuateState"), m_depart(depart) {}
	StateReturnType onEnter() override
	{
		// BFME1 0x0017A280 / 0x0017A430: the delivery cannot be
		// interrupted; evacuation adjusts its destination, departure does not.
		m_origin = *owner().getPosition();
		m_goal = machine().goalObject() ? *machine().goalObject()->getPosition() : machine().goalPosition();
		machine().lock();
		if (m_depart)
		{
			// Native model flags 60 / 241 (name table RVA 0x00EA6918),
			// translated to the port's model-condition indices.
			owner().setModelConditionState(CombatNames::modelCondition("MOVING"), true);
			owner().setModelConditionState(CombatNames::modelCondition("RUNNING_OFF_MAP"), true);
		}
		m_append = m_depart;
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, !m_depart);
		return m_move->onEnter();
	}
	StateReturnType update() override
	{
		// BFME1 0x00188520 / 0x0017A530: any terminal move result,
		// including failure, evacuates or destroys the live transport.
		if (owner().isEffectivelyDead()) return STATE_FAILURE;
		if (m_depart)
		{
			if (auto *loco = ai().curLocomotor()) loco->setFlags(loco->flags() | 2u); // BFME1 allow-invalid-position, native Locomotor +0x40 bit 1.
			if (m_append && !ai().mover().isWaitingForPath() && ai().mover().path())
			{
				m_goal.z = owner().logic().getGroundHeight(m_goal.x, m_goal.y);
				ai().mover().path()->appendNode(&m_goal, LAYER_GROUND);
				m_append = false;
			}
		}
		const StateReturnType result = m_move ? m_move->update() : STATE_FAILURE;
		if (result != STATE_CONTINUE)
		{
			if (m_depart) owner().logic().destroyObject(&owner());
			else
			{
				if (auto *contain = owner().getContain()) contain->orderAllPassengersToExit(CMD_FROM_AI);
				if (auto *team = owner().getTeam()) team->setActive();
			}
		}
		return result;
	}
	void onExit(StateExitType) override
	{
		machine().unlock();
		if (!m_depart)
		{
			// BFME1 0x0017A340: the exit leg goes back to the origin.
			machine().setGoalPosition(m_origin);
			if (m_move) m_move->onExit();
		}
		m_move.reset();
	}
	void crc(StateHasher &h) const override
	{
		h.addFloat(m_origin.x); h.addFloat(m_origin.y); h.addFloat(m_origin.z);
		h.addFloat(m_goal.x); h.addFloat(m_goal.y); h.addFloat(m_goal.z);
		h.addBool(m_append); h.addBool(m_move != nullptr);
		if (m_move) m_move->crc(h);
	}
	void xfer(Xfer &x) override
	{
		XferVersion version{1, 1}; x.xferVersion(version);
		AIXfer::coord(x, m_origin); AIXfer::coord(x, m_goal); x.xferBool(&m_append);
		xferMove(x, ai().mover(), m_move);
	}
};

HordeContain *hordeContain(Object &o) { return dynamic_cast<HordeContain *>(o.getContain()); }

const HordeContainModuleData *hordeData(const ThingTemplate *tt)
{
	if (tt) for (const auto &n : tt->behaviorModules().nuggets())
		if (const auto *d = dynamic_cast<const HordeContainBehaviorData *>(n.data.get())) return &d->horde;
	return nullptr;
}

bool statusNamed(const Object &o, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	return bit >= 0 && o.testStatus((unsigned)bit);
}

bool boolField(const ThingTemplate &tt, const char *name)
{
	const auto *v = tt.findField(name);
	const auto *b = v ? std::get_if<bool>(v) : nullptr;
	return b && *b;
}

const std::vector<std::string> &names(const ThingTemplate &tt, const char *field)
{
	static const std::vector<std::string> empty;
	const auto *v = tt.findField(field);
	const auto *list = v ? std::get_if<std::vector<std::string>>(v) : nullptr;
	return list ? *list : empty;
}

// BFME1 ThingTemplateIsEquivalentTo.cpp, RVA 0x0013FE10. BFME1's
// EquivalentTo lists also match by intersection; there is no RW reskin list.
bool equivalent(const ThingTemplate *a, const ThingTemplate *b)
{
	if (!a || !b) return false;
	if (a->getFinalOverride() == b->getFinalOverride()) return true;
	const auto equal = [](const std::string &x, const std::string &y) { return AsciiStringUtil::compareNoCase(x, y) == 0; };
	for (const auto &x : names(*a, "EquivalentTo"))
	{
		if (equal(x, b->getName())) return true;
		for (const auto &y : names(*b, "EquivalentTo")) if (equal(x, y)) return true;
	}
	for (const auto &x : names(*b, "EquivalentTo")) if (equal(x, a->getName())) return true;
	for (const auto &x : names(*a, "BuildVariations")) if (equal(x, b->getName())) return true;
	for (const auto &x : names(*b, "BuildVariations")) if (equal(x, a->getName())) return true;
	return false;
}

bool homePosition(GameLogic &logic, const TeamPrototype &proto, Coord3D &home)
{
	home = {0, 0, 0};
	const auto *terrain = logic.terrain();
	const auto *waypoint = terrain ? terrain->findWaypointByName(proto.getDict().getAsciiString("teamHome")) : nullptr;
	if (waypoint) home = waypoint->location;
	return waypoint != nullptr;
}

// BFME1 TeamMemberQueries.cpp, RVA 0x000F2420: surviving members,
// in the native prepended list order; an empty team estimates (0,0,0).
Coord3D estimateTeamPosition(const Team &team)
{
	Coord3D center{0, 0, 0};
	int count = 0;
	for (Object *o = team.getLastMember(); o; o = o->friend_teamPrev())
	{
		if (o->isEffectivelyDead()) continue;
		center.x = SimMath::addf32(center.x, o->getPosition()->x);
		center.y = SimMath::addf32(center.y, o->getPosition()->y);
		center.z = SimMath::addf32(center.z, o->getPosition()->z);
		++count;
	}
	if (count)
	{
		const float reciprocal = SimMath::divf32(1.0f, (float)count);
		center.x = SimMath::mulf32(center.x, reciprocal);
		center.y = SimMath::mulf32(center.y, reciprocal);
		center.z = SimMath::mulf32(center.z, reciprocal);
	}
	return center;
}

// BFME1 BuildAssistant::isPossibleToMakeUnit, RVA 0x000FDBF0.
// The BFME1 command set has 20 slots and one NeededUpgrade, unlike RW's
// 33 slots / upgrade list (RW 0x794F29). Keep this gate in the AI variant.
bool offersUnit(Object &builder, const ThingTemplate *tt, int index)
{
	for (const auto &module : builder.modules())
	{
		const auto *offer = module->getSpawnUnitBehaviorInterface();
		if (tt && offer && offer->getUnitName() == tt->getName()) return true;
	}
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(builder.getCommandSetName()) : nullptr;
	if (!set) return false;
	Player *owner = builder.getControllingPlayer();
	int revival = 0;
	for (int i = 0; i < 20; ++i)
	{
		const auto *button = set->getCommandButton(i);
		if (!button) continue;
		if (index != -1)
		{
			if (button->m_command == GUI_COMMAND_REVIVE && revival++ == index) return owner && owner->heroes().isAvailable(index);
			continue;
		}
		if (button->m_command != GUI_COMMAND_UNIT_BUILD && button->m_command != GUI_COMMAND_DOZER_CONSTRUCT) continue;
		const auto *offered = button->getThingTemplate();
		if (!equivalent(offered, tt)) continue;
		if (button->hasOption(COMMAND_OPTION_NEED_UPGRADE) && !button->m_neededUpgrade.empty())
		{
			const auto &upgrade = button->m_neededUpgrade.front();
			const auto *types = builder.logic().upgradeTypes();
			const int type = types ? types->typeOf(upgrade) : -1;
			if (type == UpgradeTypeTable::UPGRADE_TYPE_OBJECT && !builder.hasUpgrade(upgrade)) continue;
			if (type == UpgradeTypeTable::UPGRADE_TYPE_PLAYER && (!owner || !owner->hasUpgradeComplete(upgrade))) continue;
		}
		if (owner && BuildAssistant::playerCanBuild(*owner, offered, builder.logic())) return true;
	}
	return false;
}

// BFME1 ScriptConditionsHasCommandPointsToBuildTeam.cpp, RVA 0x003262C0.
int teamPoints(GameLogic &logic, const TeamPrototype &proto)
{
	int total = 0;
	for (int i = 1; i <= 7; ++i)
	{
		const auto n = std::to_string(i);
		const Dict &d = proto.getDict();
		const auto *tt = logic.things().findTemplate(d.getAsciiString("teamUnitType" + n));
		if (tt) total += BuildAssistant::commandPoints(*tt) * SimMath::truncToInt32(SimMath::mulf32((float)(d.getInt("teamUnitMinCount" + n) + d.getInt("teamUnitMaxCount" + n)), 0.5f));
	}
	return total;
}

// BFME1 Coord3D::GetLengthEstimate RVA 0x0015B830: the XY estimate,
// max(abs(x),abs(y)) + min(abs(x),abs(y))*0.25; then the same rule with z.
float markerDistance(const Coord3D &a, const Coord3D &b)
{
	const float x = (float)SimMath::absD(SimMath::subf32(a.x, b.x));
	const float y = (float)SimMath::absD(SimMath::subf32(a.y, b.y));
	const float z = (float)SimMath::absD(SimMath::subf32(a.z, b.z));
	const float xy = SimMath::addf32(std::max(x, y), SimMath::mulf32(std::min(x, y), 0.25f));
	return xy >= z ? SimMath::addf32(xy, SimMath::mulf32(z, 0.25f)) : SimMath::addf32(SimMath::mulf32(xy, 0.25f), z);
}
}

void Bfme1SkirmishAI::defineTransportStates(AIStateMachine &m)
{
	// BFME1 AIStateMachineConstructor.cpp / retail RVA 0x00185A20:
	// both terminal results take the same next state, including a failed move.
	m.defineState(28, std::make_unique<Bfme1TransportMoveState>(m, false), AI_IDLE, AI_IDLE);
	m.defineState(29, std::make_unique<Bfme1TransportMoveState>(m, false), 30, 30);
	m.defineState(30, std::make_unique<Bfme1TransportMoveState>(m, true), AI_IDLE, AI_IDLE);
	m.defineState(45, std::make_unique<Bfme1RampageState>(m), AI_IDLE, AI_IDLE);
}

bool Bfme1SkirmishAI::enterRampage(Object &object)
{
	if (!object.getTemplate()->bfme1Data()) return false;
	// Lua ObjectEnterRampageState 0x002E9F70 -> AI command 0x3A ->
	// private state command 0x0026FB40. Body virtual +0x94 is a no-op
	// (ActiveBody 0x0020DC10), not setIndestructible.
	if (!object.getBodyModule()) return true;
	// DelayedDeathBody overrides that same virtual: BFME1 0x00212870
	// sets its checked flag (RW 0x008C57A1), unlike ActiveBody's no-op.
	if (auto *body = dynamic_cast<DelayedDeathBody *>(object.getBodyModule())) body->setChecked(true);
	if (auto *ai = object.getAIUpdateInterface())
	{
		auto &machine = ai->stateMachine();
		if (machine.temporaryStateLocked()) machine.clearTemporaryState();
		if (machine.currentStateId() != 45)
		{
			machine.clear(); ai->setLastCommandSource(CMD_FROM_SCRIPT); machine.setState(45);
		}
	}
	return true;
}

bool Bfme1SkirmishAI::rampageBlocksScriptCommand(const Object &object)
{
	const auto *ai = object.getAIUpdateInterface();
	return object.getTemplate()->bfme1Data() && ai && ai->currentStateId() == 45;
}

int Bfme1SkirmishAI::newGame()
{
	// LiveGame has already run pending team-create actions. Their explicit
	// recruitment / auto-build overrides belong to the newly loaded players,
	// as in retail, and must survive AI startup. GameLogic::reset clears them.
	m_players.clear();
	for (int i = 0; i < m_logic.players().getPlayerCount(); ++i)
	{
		Player *p = m_logic.players().getNthPlayer(i);
		if (p && p->getPlayerType() == PLAYER_COMPUTER && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide)
		{
			// AISkirmishPlayerCtor.cpp, RVA 0x00168660.
			p->setCanBuildUnits(true);
			m_players.push_back({ i, 2, 0, false });
		}
	}
	return (int)m_players.size();
}
void Bfme1SkirmishAI::reset()
{
	m_powerReadyFrames.clear();
	m_players.clear(); m_building.clear(); m_ready.clear(); m_orders.clear(); m_conditionFrames.clear(); m_threat.clear(); m_retired.clear();
	m_teamRecruitable.clear(); m_unitRecruitable.clear(); m_autoBuild.clear(); m_teamReferences.clear();
}

bool Bfme1SkirmishAI::teamReference(const ScriptEngine &engine, const std::string &name, Team *&result) const
{
	// BFME1 ScriptEngine getTeamNamed, RVA 0x0034F160: after the
	// contextual teams, a reference wins even when its team no longer exists.
	const auto it = m_teamReferences.find(engine.qualify(name));
	if (it == m_teamReferences.end()) return false;
	result = m_logic.players().teams().findTeamByID(it->second);
	if (result && m_retired.count(result->getID()) && !result->getPrototype()->getIsSingleton()) result = nullptr;
	return true;
}

Team *Bfme1SkirmishAI::visibleTeam(const TeamPrototype &proto) const
{
	for (auto it = proto.teams().rbegin(); it != proto.teams().rend(); ++it)
		if (proto.getIsSingleton() || !m_retired.count((*it)->getID())) return *it;
	return nullptr;
}

void Bfme1SkirmishAI::retireEmptyTeams(TeamPrototype &proto)
{
	// BFME1 TeamPrototype::updateState RVA 0x000F71C0, after all
	// instances updated: delete active empty non-singletons, except the
	// player's default team. Inactive teams are still under construction.
	if (proto.getIsSingleton()) return;
	Player *owner = proto.getControllingPlayer();
	for (Team *team : proto.teams())
	{
		if (team->getFirstMember() || !team->isActive() || (owner && team == owner->getDefaultTeam())) continue;
		m_retired.insert(team->getID());
		team->scriptState().active = team->scriptState().created = team->scriptState().ready = false;
	}
}

bool Bfme1SkirmishAI::teamReady(const Team &team) const
{
	// BFME1 TeamMemberQueries.cpp RVA 0x000F4E20, gated by the
	// sticky flag at Team +0xE6 (set by 0x000EC960 when a build starts).
	// This waits for a nonempty horde, not its maximum slot capacity.
	if (!team.scriptState().wasBuilt) return true;
	for (Object *o = team.getLastMember(); o; o = o->friend_teamPrev())
	{
		if (o->isUnderConstruction()) return false;
		if (!o->isKindOfName("HORDE")) continue;
		auto *contain = o->getContain();
		auto *horde = contain ? contain->getHordeContainInterface() : nullptr;
		if (!horde) continue;
		if (contain->getContainCount() == 0) return false;
		// Native slots 39 / 38 (0x00237CD0 / 0x0023C4D0) inspect the
		// members on the way: resolved members must be idle or in state 1.
		// The moving-member flag at Object +0x114 remains a named dependency.
		for (ObjectID id : horde->membersEntering())
		{
			Object *member = m_logic.findObjectByID(id);
			if (!member) continue;
			auto *ai = member->getAIUpdateInterface();
			if (ai && ai->currentStateId() == AI_IDLE) continue;
			if (ai && ai->currentStateId() == AI_MOVE_TO)
				m_logic.scriptEngine().note("[S-5017] BFME1 team readiness: moving indexed-member flag at Object +0x114 is unported");
			return false;
		}
	}
	return true;
}

Object *Bfme1SkirmishAI::factory(Player &owner, const std::string &type, bool idle, int *buildIndex) const
{
	const ThingTemplate *tt = m_logic.things().findTemplate(type);
	// BFME1 revival list +0x684, selector RVA 0x000FA8B0: first record
	// with the exact template name, whether available or already queued.
	const int index = tt ? owner.heroes().findIndex(*tt, 0xFFFFFFFFu, 0) : -1;
	if (buildIndex) *buildIndex = index;
	if (!tt) return nullptr;
	Object *busy = nullptr;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		// BFME1 AIPlayer_findFactory.cpp: owned FS_FACTORY, alive,
		// neither UNDER_CONSTRUCTION nor SOLD, and no disabled bits.
		const int sold = ObjectTemplateInfoBuilder::objectStatusIndex("SOLD");
		if (o->getControllingPlayer() != &owner || !o->isKindOfName("FS_FACTORY") || o->isEffectivelyDead() ||
			o->isUnderConstruction() || o->getDisabledMask() != 0 || (sold >= 0 && o->testStatus((unsigned)sold))) continue;
		ProductionUpdateInterface *pu = o->getProductionUpdate();
		if (!pu || !offersUnit(*o, index == -1 ? tt : nullptr, index)) continue;
		if (pu->getProductionCount() == 0) return o;
		// Player +0x681 defaults true (PlayerInit.cpp). Auto-built units of
		// another type do not block manual AI production, even in idle-only
		// searches. AIPlayer_findFactory.cpp, RVA 0x001643B0.
		const auto autobuild = m_autoBuild.find(owner.getPlayerIndex());
		const auto *entry = pu->firstProduction();
		if ((autobuild == m_autoBuild.end() || autobuild->second) && entry &&
			(entry->type == PRODUCTION_UNIT || entry->type == PRODUCTION_BUILD_INDEX) && entry->objectToProduce &&
			boolField(*entry->objectToProduce, "IsAutoBuilt") && !equivalent(entry->objectToProduce, tt)) return o;
		if (!idle) busy = o;
	}
	return busy;
}

bool Bfme1SkirmishAI::eligible(TeamPrototype &proto)
{
	Player *owner = proto.getControllingPlayer();
	const Dict &d = proto.getDict();
	const std::string condition = d.getAsciiString("teamProductionCondition");
	if (!owner || condition.empty()) return false;
	unsigned &next = m_conditionFrames[proto.getID()];
	if (m_logic.getFrame() < next) return false;
	// BFME1 TeamPrototype::evaluateProductionCondition, RVA 0x000ED850.
	ScriptEngine &scripts = m_logic.scriptEngine();
	const std::string qualified = owner->getPlayerName() + "/" + condition;
	const auto *script = scripts.findScript(qualified);
	if (!script) return false;
	if (script->def->delayEvaluationSeconds > 0) next = m_logic.getFrame() + (unsigned)script->def->delayEvaluationSeconds * 5;
	if (!scripts.evaluateProductionScript(condition, *owner)) return false;
	int instances = 0;
	for (const Team *team : proto.teams())
	{
		// Retail deletes spent non-singleton teams. This engine retains their
		// IDs for snapshots, so retired empty instances do not use a build slot.
		if (!m_retired.count(team->getID())) ++instances;
	}
	if (instances >= d.getInt("teamMaxInstances")) return false;
	for (const auto &q : m_building)
	{
		const Team *t = m_logic.players().teams().findTeamByID(q.team);
		if (t && t->getPrototype() == &proto) return false;
	}
	return viable(proto, true, false);
}

bool Bfme1SkirmishAI::viable(TeamPrototype &proto, bool requireIdle, bool allowShortMoney)
{
	Player *owner = proto.getControllingPlayer();
	if (!owner) return false;
	const Dict &d = proto.getDict();
	// BFME1 RVA 0x00164750 first accepts teams whose minimum can be
	// recruited, charging only the missing minimum at an idle factory.
	bool recruitable = true, anyIdle = false;
	float missingCost = 0.0f;
	int total = 0;
	for (int i = 1; i <= 7; ++i)
	{
		const auto n = std::to_string(i);
		const auto *tt = m_logic.things().findTemplate(d.getAsciiString("teamUnitType" + n));
		if (!tt) continue;
		int available = 0;
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			Team *src = o->getTeam();
			if (o->getControllingPlayer() != owner || !src || !src->isActive() || !equivalent(o->getTemplate(), tt)) continue;
			bool allowed = src == owner->getDefaultTeam() || src->getPrototype()->getDict().getBool("teamIsAIRecruitable");
			const auto teamFlag = m_teamRecruitable.find(src->getID());
			if (teamFlag != m_teamRecruitable.end()) allowed = teamFlag->second;
			const auto unitFlag = m_unitRecruitable.find(o->getID());
			if (!allowed || (o->getAIUpdateInterface() && unitFlag != m_unitRecruitable.end() && !unitFlag->second) || (o->getDisabledMask() & 8)) continue;
			++available;
		}
		const int lo = d.getInt("teamUnitMinCount" + n), hi = d.getInt("teamUnitMaxCount" + n);
		int index = -1;
		Object *idle = factory(*owner, tt->getName(), true, &index);
		const int cost = index == -1 ? BuildAssistant::calcCostToBuild(*tt, owner, nullptr, -1) : owner->heroes().costAt(m_logic, *owner, index, nullptr);
		if (hi > 0 && available < lo)
		{
			if (!idle) recruitable = false;
			missingCost = SimMath::addf32(missingCost, (float)((lo - available) * cost));
			if ((float)owner->getMoney()->countMoney() < missingCost) recruitable = false;
		}
		// AIPlayerIsPossibleToBuildTeam.cpp, RVA 0x001645B0: every
		// existing type needs a factory; only one must be idle. Cost is
		// accumulated with a truncation after EACH average-sized row.
		if (!factory(*owner, tt->getName(), false)) total = -1;
		if (idle) anyIdle = true;
		const int freeBit = ObjectTemplateInfoBuilder::kindOfIndex("BUILD_FOR_FREE");
		if (total >= 0 && (freeBit < 0 || !MaskTest(m_logic.templateInfo(tt).kindOf, (unsigned)freeBit))) total = SimMath::truncToInt32(SimMath::addf32((float)total, SimMath::mulf32((float)cost, SimMath::mulf32((float)(lo + hi), 0.5f))));
	}
	if (recruitable) return true;
	if (total < 0) return false;
	const int threshold = SimMath::truncToInt32(SimMath::mulf32((float)total, m_logic.settings().legacyAI.resourcesToStart));
	// The direct build handler queues an otherwise feasible team even when
	// short of cash; selection requires both money and one idle producer.
	if (owner->getMoney()->countMoney() < (unsigned)threshold) return allowShortMoney;
	return anyIdle || !requireIdle;
}

void Bfme1SkirmishAI::buildTeam(TeamPrototype &proto, bool priority)
{
	// AIPlayerBuildSpecificAITeam.cpp: inactive until production finishes;
	// TeamFactory runs the production-condition actions on creation.
	if (!proto.getControllingPlayer() || !proto.getControllingPlayer()->canBuildUnits()) return;
	if (priority && proto.getIsSingleton() && proto.getFirstTeam() && proto.getFirstTeam()->getFirstMember()) return;
	if (!viable(proto, false, true)) return;
	for (const auto &q : m_building)
	{
		const Team *existing = m_logic.players().teams().findTeamByID(q.team);
		if (existing && existing->getPrototype() == &proto) return;
	}
	BuildingTeam q;
	q.started = m_logic.getFrame();
	q.priority = priority;
	const Dict &d = proto.getDict();
	// BFME1 AIPlayerBuildSpecificAITeam.cpp: optional (max-min) orders
	// are prepended first, then required min orders. There is no size draw.
	for (bool required : { true, false })
	for (int i = 7; i >= 1; --i)
	{
		const std::string n = std::to_string(i);
		const int lo = d.getInt("teamUnitMinCount" + n), hi = d.getInt("teamUnitMaxCount" + n);
		const int count = required ? lo : hi - lo;
		if (!required && count <= 0) continue;
		const auto type = d.getAsciiString("teamUnitType" + n);
		if (!m_logic.things().findTemplate(type))
		{
			if (!type.empty()) m_logic.scriptEngine().note("BFME1 BUILD_TEAM: unknown unit template '" + type + "'");
			continue;
		}
		q.members.push_back({ type, count, required });
	}
	if (q.members.empty()) return;
	Team *team = m_logic.players().teams().createTeam(&proto);
	if (!team) return;
	m_retired.erase(team->getID());
	team->scriptState().beingBuilt = true;
	team->scriptState().wasBuilt = true; // BFME1 RVA 0x000EC960; clearing beingBuilt does not clear this flag.
	q.team = team->getID();
	if (priority) m_building.insert(m_building.begin(), std::move(q));
	else m_building.push_back(std::move(q));
	if (proto.templateScripts().executeActionsOnCreate) m_logic.scriptEngine().runTeamProductionActions(*team);
}

// BFME1 TeamRva000F10F0Recruit.cpp / actual tryToRecruit at RVA
// 0x000F2A00. A default-team candidate is accepted outside the initial
// radius only on the queue-time/specific-team path, not action 397.
Object *Bfme1SkirmishAI::recruit(Team &team, const std::string &type, const Coord3D &center, float radius, bool defaultOutsideRadius) const
{
	const auto *tt = m_logic.things().findTemplate(type);
	Player *owner = team.getControllingPlayer();
	Object *best = nullptr, *a = nullptr, *b = nullptr;
	float distance = SimMath::mulf32(radius, radius), da = distance, db = distance;
	const auto *data = defaultOutsideRadius ? hordeData(tt) : nullptr;
	const auto *partA = data && data->m_splitHorde.size() > 1 ? m_logic.things().findTemplate(data->m_splitHorde[0].splitResult) : nullptr;
	const auto *partB = data && data->m_splitHorde.size() > 1 ? m_logic.things().findTemplate(data->m_splitHorde[1].splitResult) : nullptr;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		Team *src = o->getTeam();
		if (o->isEffectivelyDead() || o->getControllingPlayer() != owner || !src || src == &team) continue;
		if (!src->isActive() || src->getPrototype()->getDict().getInt("teamProductionPriority") >= team.getPrototype()->getDict().getInt("teamProductionPriority")) continue;
		bool allowed = src == owner->getDefaultTeam() || src->getPrototype()->getDict().getBool("teamIsAIRecruitable");
		const auto teamFlag = m_teamRecruitable.find(src->getID());
		if (teamFlag != m_teamRecruitable.end()) allowed = teamFlag->second;
		if (!allowed) continue;
		const auto flag = m_unitRecruitable.find(o->getID());
		if ((o->getAIUpdateInterface() && flag != m_unitRecruitable.end() && !flag->second) || (o->getDisabledMask() & 8)) continue;
		const float dx = SimMath::subf32(center.x, o->getPosition()->x), dy = SimMath::subf32(center.y, o->getPosition()->y);
		const float d = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
		auto accept = [&](const ThingTemplate *desired, Object *&candidate, float &limit, bool typeSet) {
			if (!desired && !typeSet) return false;
			if (!equivalent(o->getTemplate(), desired) && !(typeSet && ScriptConditions::inTypeSet(m_logic.scriptEngine(), type, o->getTemplate()->getName()))) return false;
			if (!(defaultOutsideRadius && !candidate && src == owner->getDefaultTeam()) && d > limit) return false;
			candidate = o; limit = d; return true;
		};
		// 0x000F2B49: one scan, direct / first component / second component.
		// A successful predicate consumes this candidate even when the two
		// split templates are identical (the Elven double horde).
		if (accept(tt, best, distance, true)) continue;
		if (partA && partB && !accept(partA, a, da, false)) accept(partB, b, db, false);
	}
	// 0x000F2BCB: a pre-existing combo wins if nearer than EITHER
	// component; ties combine. The farther component receives the call,
	// so the result takes the nearer component's transform.
	if (a && b && (!best || (distance >= da && distance >= db)))
	{
		if (da < db) std::swap(a, b);
		if (Object *made = combineHordes(*a, *b)) best = made;
	}
	if (best && defaultOutsideRadius) best->setStatus((unsigned)CombatNames::status("HUNT_WHEN_IDLE"), false);
	return best;
}

Object *Bfme1SkirmishAI::combineHordes(Object &receiver, Object &other) const
{
	// BFME1 HordeContain slots 0x6C / 0x70, RVA 0x0023CAA0 /
	// 0x00245BF0. This path is separate from RW's plain team recruitment.
	auto *a = hordeContain(receiver);
	if (!a || &receiver == &other || receiver.getControllingPlayer() != other.getControllingPlayer()) return nullptr;
	if (other.isEffectivelyDead() || other.getDisabledMask() || statusNamed(other, "DEPLOYED") ||
		statusNamed(other, "TEMPORARILY_DEFECTED") || statusNamed(receiver, "TEMPORARILY_DEFECTED") ||
		other.testModelCondition(CombatNames::modelCondition("JUST_BUILT"))) return nullptr;
	for (const Object *o : { &receiver, &other })
		if (statusNamed(*o, "UNDER_CONSTRUCTION") || statusNamed(*o, "UNSELECTABLE")) return nullptr;
	const ComboHordeEntry *entry = nullptr;
	for (const auto &e : a->hordeData().m_comboHorde) if (e.target == other.getTemplate()->getName()) { entry = &e; break; }
	if (!entry) return nullptr;
	auto *b = hordeContain(other);
	if (!b && (!a->hordeData().m_machineAllowed || a->hordeData().m_machineType != other.getTemplate()->getName())) return nullptr;
	const auto *result = m_logic.things().findTemplate(entry->result);
	if (!result || !hordeData(result)) return nullptr;
	// Cancel queued production before either original horde is retired.
	for (Object *o : { &receiver, &other }) if (auto *p = o->getProductionUpdate()) p->cancelAllProduction();
	ObjectStatusMaskType status{};
	Object *made = m_logic.newObject(result, receiver.getTeam(), status);
	if (!made) return nullptr;
	made->setPosition(other.getPosition()); made->setOrientation(other.getOrientation());
	auto *fresh = hordeContain(*made);
	if (!fresh) { m_logic.destroyObject(made); return nullptr; }
	UpgradeMaskType upgrades = receiver.getUpgradeMask(); upgrades.orWith(other.getUpgradeMask());
	made->friend_orUpgradeMask(upgrades); made->updateUpgradeModules();
	if (statusNamed(receiver, "HUNT_WHEN_IDLE")) made->setStatus((unsigned)CombatNames::status("HUNT_WHEN_IDLE"), true);
	{
	// 0x0024618A..0x00246258 temporarily clears GameLogic +0x90
	// (m_isScoringEnabled, GameLogicInit.cpp). Combining existing ranks
	// must not award fresh unit-level-up scores for the replacement object.
	struct ScoringGuard
	{
		bool &slot; bool previous;
		explicit ScoringGuard(bool &value) : slot(value), previous(value) { slot = false; }
		~ScoringGuard() { slot = previous; }
	} noScores(m_logic.economy().context().scoring);
	const int levelA = receiver.getExperienceTracker() ? receiver.getExperienceTracker()->getRank() : 1;
	const int levelB = other.getExperienceTracker() ? other.getExperienceTracker()->getRank() : 1;
	// 0x00246178..0x002461AD selects MAX(rankA, rankB), not min.
	if (auto *xp = made->getExperienceTracker()) xp->gainExpForLevel(std::max(levelA, levelB) - 1, true, false);
	made->setBfme1IndicatorColorIndex(receiver.bfme1IndicatorColorIndex());
	Object *bannerA = m_logic.findObjectByID(a->bannerCarrier());
	Object *bannerB = b ? m_logic.findObjectByID(b->bannerCarrier()) : nullptr;
	Object *banner = bannerA;
	if (bannerB && (!bannerA || (bannerB->getExperienceTracker() && bannerA->getExperienceTracker() &&
		bannerB->getExperienceTracker()->getRank() > bannerA->getExperienceTracker()->getRank()))) banner = bannerB;
	auto members = a->detachBfme1CombinationMembers();
	if (b)
	{
		auto more = b->detachBfme1CombinationMembers(); members.insert(members.end(), more.begin(), more.end());
	}
	else members.push_back(&other);
	for (Object *discard : { bannerA, bannerB }) if (discard && discard != banner)
	{
		members.erase(std::remove(members.begin(), members.end(), discard), members.end()); m_logic.destroyObject(discard);
	}
	fresh->adoptBfme1CombinationMembers(members, banner ? banner->getID() : INVALID_ID);
	}
	if (b) m_logic.destroyObject(&other);
	m_logic.destroyObject(&receiver);
	return made;
}

void Bfme1SkirmishAI::recruitCombinedFromTeam(Team &destination, Team &source, const std::string &types, int count)
{
	// BFME1 0x000F5E40: type-list order, then all instances of the
	// source prototype (newest first), with first matches in member order.
	const auto *list = m_logic.scriptEngine().findObjectList(types);
	const std::vector<std::string> one{types};
	int recruited = 0;
	for (const auto &type : list ? *list : one)
	{
		const auto *tt = m_logic.things().findTemplate(type);
		const auto *data = hordeData(tt);
		const int combo = CombatNames::kindOf("COMBO_HORDE");
		if (!tt || !MaskTest(m_logic.templateInfo(tt).kindOf, (unsigned)combo) || !data || data->m_splitHorde.size() < 2) continue;
		const auto *ta = m_logic.things().findTemplate(data->m_splitHorde[0].splitResult);
		const auto *tb = m_logic.things().findTemplate(data->m_splitHorde[1].splitResult);
		if (!ta || !tb) continue;
		while (recruited < count)
		{
			Object *a = nullptr, *b = nullptr;
			for (auto it = source.getPrototype()->teams().rbegin(); it != source.getPrototype()->teams().rend() && (!a || !b); ++it)
				for (Object *o = (*it)->getLastMember(); o && (!a || !b); o = o->friend_teamPrev())
				{
					if (o->isEffectivelyDead()) continue;
					if (!a && equivalent(o->getTemplate(), ta)) a = o;
					else if (!b && equivalent(o->getTemplate(), tb)) b = o;
				}
			if (!a || !b) break;
			// 0x000F6154: x87 PC24 centre distance less the bounding
			// SPHERE radii, clamped at zero and squared before the store.
			const double dx = SimMath::pc24SubW(a->getPosition()->x, b->getPosition()->x);
			const double dy = SimMath::pc24SubW(a->getPosition()->y, b->getPosition()->y);
			const double span = SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)));
			const double edge = SimMath::pc24SubW(SimMath::pc24SubW(span, CombatQueries::boundingSphereRadius(*a)), CombatQueries::boundingSphereRadius(*b));
			const float distance = edge < 0.0 ? 0.0f : SimMath::fstpDword(SimMath::pc24MulW(edge, edge));
			// The native handler releases both to the destination owner's
			// default team before it asks the horde whether merging is legal.
			Team *home = destination.getControllingPlayer()->getDefaultTeam();
			a->setTeam(home); b->setTeam(home);
			Object *made = hordeContain(*a) ? combineHordes(*a, *b) : hordeContain(*b) ? combineHordes(*b, *a) : nullptr;
			if (!made) break;
			made->setTeam(&destination);
			if (distance > 22500.0f) made->teleportTo(made->getPosition(), false); // 0x000F6285: reset the distant combination's movement.
			++recruited;
		}
		if (recruited >= count) break;
	}
}

void Bfme1SkirmishAI::loadReinforcementTransports(Team &team, Object *carrier, const Coord3D &destination, const Dict &data)
{
	// BFME1 ScriptActions reinforcement helper 0x003006B0. Snapshot
	// the native prepended member list before loading changes containment.
	std::vector<Object *> members;
	for (Object *o = team.getLastMember(); o; o = o->friend_teamPrev()) members.push_back(o);
	if (data.getBool("teamStartsFull"))
	{
		using Item = std::pair<ObjectID, unsigned>;
		std::vector<Item> passengers, spaces;
		for (Object *o : members)
		{
			if (o == carrier) continue;
			if (o->isKindOfName("TRANSPORT"))
			{
				if (auto *contain = o->getContain()) spaces.emplace_back(o->getID(), (unsigned)contain->getContainMax());
			}
			else
			{
				const unsigned size = (unsigned)TransportContain::transportSlotCount(*o);
				passengers.emplace_back(o->getID(), size ? size : 0x7fffffu);
			}
		}
		// PartitionSolver::solve 0x00096570: descending STLport sort,
		// then first-fit in the original sorted carrier order (no re-sort).
		auto greater = [](const Item &a, const Item &b) { return a.second > b.second; };
		Stlport::sort(passengers, greater); Stlport::sort(spaces, greater);
		for (const auto &p : passengers) for (auto &space : spaces) if (p.second <= space.second)
		{
			space.second -= p.second;
			Object *rider = m_logic.findObjectByID(p.first), *transport = m_logic.findObjectByID(space.first);
			if (rider && transport && transport->getContain()) transport->getContain()->addToContain(rider);
			break;
		}
	}
	if (!carrier) { team.setActive(); return; }
	const ThingTemplate *transportType = carrier->getTemplate();
	Object *current = carrier;
	int number = 1;
	for (Object *o : members)
	{
		if (equivalent(o->getTemplate(), transportType) || o->getContainedBy()) continue;
		auto *contain = current->getContain();
		if (!contain || !contain->isValidContainerFor(*o, false, false)) continue;
		if (!contain->isValidContainerFor(*o, true, false))
		{
			Coord3D position = destination;
			position.z = m_logic.getGroundHeight(position.x, position.y);
			const float radius = current->ownPathfindGeometry(m_logic.aiWorld()->movementInfo(*transportType).geometry).boundingSphereRadius();
			// 0x00300E17: signed fild, multiply and add in the x87
			// register before storing the destination-relative X offset.
			position.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)number, (double)radius), (double)position.x));
			current = m_logic.scriptEngine().host()->createObject(*transportType, team, position, 0.0f);
			if (!current) { m_logic.scriptEngine().note("BFME1 reinforcement: transport creation failed"); break; }
			++number; current->friend_onBuildComplete(); contain = current->getContain();
		}
		if (contain) contain->addToContain(o);
	}
	// The team remains inactive until a delivery move terminates. Every
	// matching carrier receives the composite order; other unheld members move.
	for (Object *o = team.getLastMember(); o; o = o->friend_teamPrev())
	{
		auto *ai = o->getAIUpdateInterface(); if (!ai) continue;
		if (equivalent(o->getTemplate(), transportType))
		{
			auto &machine = ai->stateMachine(); machine.clear();
			machine.setGoalObject(INVALID_ID); machine.setGoalPosition(destination);
			ai->setLastCommandSource(CMD_FROM_SCRIPT);
			machine.setState(data.getBool("teamTransportsExit") ? 29 : 28);
			ai->wakeUpNow();
		}
		else if (!(o->getDisabledMask() & 8)) ai->aiMoveToPosition(destination, CMD_FROM_SCRIPT);
	}
}

bool Bfme1SkirmishAI::reinforce(Player &owner, int priority)
{
	// AIPlayerSelectTeamToReinforce.cpp, BFME1 RVA 0x00167080.
	Team *selected = nullptr;
	std::string type;
	for (const auto &proto : m_logic.players().teams().prototypes())
	{
		if (proto->getControllingPlayer() != &owner || !proto->getDict().getBool("teamAutoReinforce") || proto->getDict().getInt("teamProductionPriority") <= priority) continue;
		bool busy = false;
		for (const auto &q : m_building)
		{
			const auto *t = m_logic.players().teams().findTeamByID(q.team);
			if (t && t->getPrototype() == proto.get()) busy = true;
		}
		if (busy) continue;
		// Native TeamInstanceList is prepended; TeamFactory stores creation
		// order. Do not re-check priority inside this prototype's loop.
		for (auto it = proto->teams().rbegin(); it != proto->teams().rend(); ++it)
		{
			Team *t = *it;
			bool hasUnits = false;
			for (Object *o = t->getFirstMember(); o; o = o->friend_teamNext())
				if (!o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE") && !o->isKindOfName("PROJECTILE") && !o->isKindOfName("MINE")) { hasUnits = true; break; }
			if (!hasUnits) continue; // BFME1 TeamMemberQueries.cpp, RVA 0x000F4C10.
			for (int i = 1; i <= 7; ++i)
			{
				const auto n = std::to_string(i);
				const auto name = proto->getDict().getAsciiString("teamUnitType" + n);
				const auto *tt = m_logic.things().findTemplate(name);
				const int max = proto->getDict().getInt("teamUnitMaxCount" + n);
				if (max < 1 || !tt) continue;
				int count = 0;
				for (Object *o = t->getFirstMember(); o; o = o->friend_teamNext()) if (equivalent(o->getTemplate(), tt)) ++count;
				if (count < max && factory(owner, name, true)) { selected = t; type = name; priority = proto->getDict().getInt("teamProductionPriority"); }
			}
		}
	}
	if (!selected) return false;
	BuildingTeam q;
	q.team = selected->getID(); q.started = m_logic.getFrame(); q.reinforcement = true;
	q.members.push_back({type, 1, true});
	Coord3D origin;
	homePosition(m_logic, *selected->getPrototype(), origin);
	if (selected->getLastMember()) origin = *selected->getLastMember()->getPosition();
	if (Object *unit = recruit(*selected, type, origin, m_logic.settings().legacyAI.maxRecruitRadius, true))
	{
		q.members.front().remaining = 0; q.reinforcementID = unit->getID();
		unit->setTeam(selected);
		if (auto *ai = unit->getAIUpdateInterface()) ai->aiIdle(CMD_FROM_AI);
	}
	else
	{
		int index = -1;
		if (Object *f = factory(owner, type, true, &index))
		{
			auto *pu = f->getProductionUpdate();
			const auto id = pu->requestUniqueUnitID();
			if (pu->queueCreateUnit(m_logic.things().findTemplate(type), index, id, -1, false, "", false))
			{
				m_orders.push_back({f->getID(), id, q.team, type, 0});
				q.members.front().remaining = 0;
			}
		}
	}
	m_building.insert(m_building.begin(), std::move(q));
	return true;
}

void Bfme1SkirmishAI::recruitTeam(TeamPrototype &proto, float radius, const Coord3D *position)
{
	// AISkirmishPlayerRecruitSpecificAITeam.cpp, BFME1 RVA 0x001699C0.
	if (radius < 1.0f) radius = 99999.0f;
	if (proto.getIsSingleton() && proto.getFirstTeam() && proto.getFirstTeam()->getFirstMember()) return;
	Team *team = m_logic.players().teams().createTeam(&proto);
	if (!team) return;
	Coord3D home;
	if (!homePosition(m_logic, proto, home)) m_logic.scriptEngine().note("BFME1 recruit team '" + proto.getName() + "' has no Home Position (or Origin)");
	int count = 0;
	for (int i = 1; i <= 7; ++i)
	{
		const auto n = std::to_string(i);
		const auto type = proto.getDict().getAsciiString("teamUnitType" + n);
		for (int k = 0; k < proto.getDict().getInt("teamUnitMaxCount" + n); ++k)
		{
			Object *o = recruit(*team, type, position ? *position : home, radius, true);
			if (!o) break;
			o->setTeam(team); ++count;
			if (auto *ai = o->getAIUpdateInterface()) ai->aiMoveToPosition(home, CMD_FROM_AI);
		}
	}
	if (count)
	{
		BuildingTeam q; q.team = team->getID(); q.started = m_logic.getFrame();
		m_ready.insert(m_ready.begin(), std::move(q)); m_retired.erase(team->getID());
	}
	else if (!proto.getIsSingleton()) m_retired.insert(team->getID());
}

void Bfme1SkirmishAI::joinTeam(Object &unit)
{
	// BFME1 AIUpdateInterface_joinTeam.cpp, RVA 0x0027D4F0. The native
	// non-idle branch deliberately reads THIS machine's state after clear,
	// not the other member's state (0x27D5EF): preserve that behavior.
	auto *ai = unit.getAIUpdateInterface();
	if (!ai || ai->isAiInDeadState() || ai->isImmobile()) return;
	// BFME1 ObjectIsMobileBody.cpp, RVA 0x001C9270. Unlike RW
	// 0x690E97 this predicate does not test the STONED model condition.
	if (unit.getDisabledMask() != 0 && unit.getDisabledMask() != (1u << 8)) return;
	const int deployed = ObjectTemplateInfoBuilder::objectStatusIndex("DEPLOYED");
	if (unit.isKindOfName("SIEGE_TOWER") && deployed >= 0 && unit.testStatus((unsigned)deployed)) return;
	ai->chooseLocomotorSet(0);
	auto &machine = ai->stateMachine();
	machine.clear(); machine.setGoalPath({});
	for (Object *o = unit.getTeam()->getLastMember(); o; o = o->friend_teamPrev())
	{
		if (o == &unit || !o->getAIUpdateInterface() || (o->getDisabledMask() & 8)) continue;
		auto *other = o->getAIUpdateInterface();
		if (other->isIdle()) ai->aiMoveToPosition(*o->getPosition(), CMD_FROM_AI);
		else
		{
			if (other->stateMachine().goalObject()) machine.setGoalObject(other->stateMachine().goalObjectID());
			else machine.setGoalPosition(other->stateMachine().goalPosition());
			ai->setLastCommandSource(CMD_FROM_AI);
			machine.setState(machine.currentStateId());
		}
		break;
	}
}

void Bfme1SkirmishAI::queueUnits(Player &owner)
{
	for (auto &q : m_building)
	{
		Team *t = m_logic.players().teams().findTeamByID(q.team);
		Player *p = t ? t->getControllingPlayer() : nullptr;
		if (p != &owner || p->isDefeated() || !p->canBuildUnits()) continue;
		const int limit = t->getPrototype()->getDict().getInt("teamInitialIdleSeconds") * 5;
		if (limit > 0 && m_logic.getFrame() > q.started + (unsigned)limit) continue;
		for (size_t index = 0; index < q.members.size(); ++index)
		{
			Member &m = q.members[index];
			bool pending = false;
			for (const auto &o : m_orders) if (o.team == q.team && o.member == (int)index) pending = true;
			if (pending) continue;
			if (m.remaining == 0) continue;
			Coord3D home;
			const bool hasHome = homePosition(m_logic, *t->getPrototype(), home);
			while (m.remaining > 0)
			{
				Object *unit = recruit(*t, m.type, home, hasHome ? m_logic.settings().legacyAI.maxRecruitRadius : 100000.0f, true);
				if (!unit) break;
				--m.remaining; unit->setTeam(t);
				if (q.reinforcement) q.reinforcementID = unit->getID();
				if (auto *ai = unit->getAIUpdateInterface())
				{
					if (hasHome) ai->aiMoveToPosition(home, CMD_FROM_AI);
					else ai->aiIdle(CMD_FROM_AI);
				}
				t->setActive();
			}
			if (m.remaining == 0) continue;
			int buildIndex = -1;
			Object *f = factory(*p, m.type, !q.priority, &buildIndex);
			if (!f) continue;
			ProductionUpdateInterface *pu = f->getProductionUpdate();
			const auto id = pu->requestUniqueUnitID();
			if (pu->queueCreateUnit(m_logic.things().findTemplate(m.type), buildIndex, id, -1, false, "", false))
			{
				m_orders.push_back({ f->getID(), id, q.team, m.type, (int)index });
				--m.remaining;
			}
		}
	}
}

void Bfme1SkirmishAI::unitProduced(Object &factoryObject, Object &unit, unsigned /*productionID*/)
{
	// BFME1 AIPlayerOnUnitProduced.cpp, RVA 0x00168320: build queue /
	// work-order order, matching factory and equivalent type, not the ID.
	// IDs remain useful for cancellation recovery, but automatic production
	// of that same type can satisfy the original work order too.
	for (auto &q : m_building)
	for (size_t index = 0; index < q.members.size(); ++index)
	{
		const auto order = std::find_if(m_orders.begin(), m_orders.end(), [&](const UnitOrder &o) {
			return o.team == q.team && o.member == (int)index && o.factory == factoryObject.getID() &&
				equivalent(unit.getTemplate(), m_logic.things().findTemplate(o.type));
		});
		if (order == m_orders.end()) continue;
		Team *t = m_logic.players().teams().findTeamByID(q.team);
		if (!t || t->getControllingPlayer() != factoryObject.getControllingPlayer()) continue;
		unit.setTeam(t);
		if (q.reinforcement) q.reinforcementID = unit.getID();
		Coord3D home;
		if (homePosition(m_logic, *t->getPrototype(), home))
			if (auto *ai = unit.getAIUpdateInterface())
			{
				// Native 0x00278830 walks non-horde containers, then tests
				// the stored moving flag, or a non-idle AI with a path.
				AIUpdateInterface *movement = ai;
				for (Object *parent = unit.getContainedBy(); parent && parent->getAIUpdateInterface() && !parent->isKindOfName("HORDE"); parent = parent->getContainedBy())
					movement = parent->getAIUpdateInterface();
				const bool preserveGoal = movement->mover().movementFlag() || (!movement->isIdle() && movement->mover().path());
				std::vector<Coord3D> path;
				if (preserveGoal) path.push_back(ai->stateMachine().goalPosition());
				path.push_back(home);
				ai->aiFollowPath(path, nullptr, CMD_FROM_AI, true);
			}
		m_orders.erase(order);
		for (auto &p : m_players) if (p.player == factoryObject.getControllingPlayer()->getPlayerIndex()) p.delay = 0;
		return;
	}
}

void Bfme1SkirmishAI::update(Player *owner)
{
	// AIPlayerQueueUnits.cpp requeues work whose factory was lost. Keep
	// the requested type with the order, rather than leaving the team waiting
	// for a completion callback that can no longer arrive.
	for (auto it = m_orders.begin(); it != m_orders.end();)
	{
		Object *f = m_logic.findObjectByID(it->factory);
		Team *team = m_logic.players().teams().findTeamByID(it->team);
		if (owner && team && team->getControllingPlayer() != owner) { ++it; continue; }
		const ProductionUpdateInterface *pu = f && !f->isEffectivelyDead() && team && f->getControllingPlayer() == team->getControllingPlayer() ? f->getProductionUpdate() : nullptr;
		bool queued = false;
		for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; e; e = pu->nextProduction(e))
			queued = queued || e->productionID == it->productionID;
		if (queued) { ++it; continue; }
		for (auto &q : m_building)
		{
			if (q.team != it->team) continue;
			if (it->member >= 0 && (size_t)it->member < q.members.size()) ++q.members[(size_t)it->member].remaining;
		}
		it = m_orders.erase(it);
	}
	// AIPlayer::update / doTeamBuilding: ready teams, queued teams, then
	// team production. The faction scripts run in ScriptEngine's own phase.
	// BFME1 AIPlayerCheckReadyTeams.cpp, RVA 0x00162560. A reinforcement
	// waits only for its new member; the existing team's attack keeps running.
	for (auto it = m_ready.begin(); it != m_ready.end();)
	{
		Team *team = m_logic.players().teams().findTeamByID(it->team);
		if (!team) { it = m_ready.erase(it); continue; }
		if (owner && team->getControllingPlayer() != owner) { ++it; continue; }
		bool allIdle = true, anyIdle = false;
		if (it->reinforcement)
		{
			Object *o = m_logic.findObjectByID(it->reinforcementID);
			if (o && o->getAIUpdateInterface()) anyIdle = allIdle = o->getAIUpdateInterface()->isIdle();
		}
		else for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
		{
			if (auto *ai = o->getAIUpdateInterface())
			{
				if (ai->isIdle()) anyIdle = true;
				else if (!o->isEffectivelyDead()) allIdle = false;
			}
		}
		const auto *proto = team->getPrototype();
		const auto *script = m_logic.scriptEngine().findScript(proto->getControllingPlayer()->getPlayerName() + "/" + proto->templateScripts().productionCondition);
		if (anyIdle && proto->templateScripts().executeActionsOnCreate && script && !script->def->actions.empty()) allIdle = true;
		if (m_logic.getFrame() > it->started + 300) allIdle = true;
		if (!allIdle) { ++it; continue; }
		team->scriptState().beingBuilt = false;
		if (it->reinforcement)
		{
			if (Object *o = m_logic.findObjectByID(it->reinforcementID)) joinTeam(*o);
		}
		else
		{
			team->setActive();
			// BFME1 ScriptEngineClearTeamFlags.cpp, RVA 0x00348E70;
			// clearFlag (0x00348DA0) clears existing name+player flags only.
			for (const char *name : { "USA Team is Building", "USA Air Team Is Building", "USA Inf Team Is Building", "China Team is Building",
				"China Air Team Is Building", "China Inf Team Is Building", "GLA Team is Building", "GLA Inf Team is Building" })
				for (int player = 0; player < 32; ++player)
					if (bool *flag = m_logic.scriptEngine().findFlag(std::string(name) + std::to_string(player))) *flag = false;
		}
		it = m_ready.erase(it);
	}
	std::vector<TeamID> productionActions;
	for (auto it = m_building.begin(); it != m_building.end();)
	{
		Team *team = m_logic.players().teams().findTeamByID(it->team);
		if (owner && team && team->getControllingPlayer() != owner) { ++it; continue; }
		const int limit = team ? team->getPrototype()->getDict().getInt("teamInitialIdleSeconds") * 5 : 0;
		const bool expired = limit > 0 && m_logic.getFrame() > it->started + (unsigned)limit;
		bool done = true, minimum = true, pending = false;
		for (const auto &m : it->members)
		{
			if (m.remaining) done = false;
			if (m.required && m.remaining) minimum = false;
		}
		for (const auto &o : m_orders) if (o.team == it->team) pending = true;
		if (expired && !minimum)
		{
			// AIPlayerCheckQueuedTeams.cpp / TeamInQueue::disband: cancel
			// unfinished work and release the units to the default team.
			for (auto order = m_orders.begin(); order != m_orders.end();)
			{
				if (order->team != it->team) { ++order; continue; }
				if (Object *factory = m_logic.findObjectByID(order->factory))
					if (auto *production = factory->getProductionUpdate()) production->cancelUnitCreate(order->productionID);
				order = m_orders.erase(order);
			}
			if (team)
			{
				while (Object *o = team->getFirstMember()) o->setTeam(team->getControllingPlayer()->getDefaultTeam());
				team->scriptState().beingBuilt = false;
				m_retired.insert(team->getID());
			}
			it = m_building.erase(it);
			continue;
		}
		if (expired && minimum) done = true;
		if (pending) done = false;
		if (done && team)
		{
			team->scriptState().beingBuilt = false;
			m_ready.insert(m_ready.begin(), std::move(*it));
			it = m_building.erase(it);
		}
		else
		{
			if (team && team->getPrototype()->templateScripts().executeActionsOnCreate)
				for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
					if (o->getAIUpdateInterface() && o->getAIUpdateInterface()->isIdle()) { productionActions.push_back(team->getID()); break; }
			++it;
		}
	}
	for (TeamID id : productionActions)
		if (Team *team = m_logic.players().teams().findTeamByID(id)) m_logic.scriptEngine().runTeamProductionActions(*team);
	for (auto &s : m_players)
	{
		Player *p = m_logic.players().getNthPlayer(s.player);
		if (owner && p != owner) continue;
		if (!p || p->isDefeated() || !p->canBuildUnits()) continue;
		if (!s.ready && --s.timer <= 0) { s.ready = true; s.delay = 0; }
		if (--s.delay >= 1) continue;
		queueUnits(*p);
		if (s.ready)
		{
			std::vector<TeamPrototype *> choices;
			int best = -99999;
			for (const auto &proto : m_logic.players().teams().prototypes())
			{
				if (proto->getControllingPlayer() != p || !eligible(*proto)) continue;
				const int priority = proto->getDict().getInt("teamProductionPriority");
				if (priority > best) { best = priority; choices.clear(); }
				if (priority == best) choices.push_back(proto.get());
			}
			if (reinforce(*p, best)) { s.delay = 0; continue; }
			if (!choices.empty())
			{
				buildTeam(*choices[(size_t)m_logic.random().getValue(0, (int)choices.size()-1, "AIPlayer.cpp", 1754)]);
				s.ready = false;
				const auto &cfg = m_logic.settings().legacyAI;
				s.timer = SimMath::truncToInt32(cfg.teamSeconds) * 5;
				const unsigned money = p->getMoney()->countMoney();
				if (money < cfg.poor) s.timer = SimMath::truncToInt32(SimMath::divf32((float)s.timer, cfg.poorRate));
				else if (money > cfg.wealthy) s.timer = SimMath::truncToInt32(SimMath::divf32((float)s.timer, cfg.wealthyRate));
			}
		}
		s.delay = 25;
	}
}

bool Bfme1SkirmishAI::executeAction(ScriptEngine &engine, const ScriptActionRec &a)
{
	if (!a.enabled) return true;
	const auto *tmpl = ScriptTemplates::action(a.resolved);
	if (!tmpl) return false;
	const std::string &name = tmpl->name;
	if (name == "TEAM_MERGE_INTO_TEAM")
	{
		// BFME1 ScriptActions_doMergeTeamIntoTeam.cpp, RVA 0x002FD790.
		// In particular, an emptied source is deleted, not left running its
		// generic scripts or consuming its prototype's instance limit.
		Team *from = ScriptConditions::team(engine, param(a, 0).stringValue);
		Team *to = ScriptConditions::team(engine, param(a, 1).stringValue, true);
		if (!from || !to || from == to) return true;
		if (from->getControllingPlayer() != to->getControllingPlayer())
			engine.note("[S-5017] BFME1 TEAM_MERGE_INTO_TEAM: source team controller handoff is unported");
		for (int pass = 0; pass < 3; ++pass)
		{
			Object *cursor = from->getLastMember(), *next = cursor;
			while (cursor)
			{
				Object *o = next;
				next = cursor;
				cursor = cursor->friend_teamPrev();
				if (o->getContainedBy() && o->getContainedBy()->isKindOfName("PARACHUTABLE")) continue;
				o->setTeam(to);
				o->updateUpgradeModules();
			}
			if (next) { next->setTeam(to); next->updateUpgradeModules(); }
			if (!from->getFirstMember())
			{
				// Native deleteTeam(false), RVA 0x000F2810, deletes members
				// only (none remain here). The prototype's next updateState
				// retires this empty non-singleton; singleton teams persist.
				break;
			}
		}
		to->setActive();
		return true;
	}
	if (name == "SET_TEAM_REFERENCE" || name == "SET_TEAM_REFERENCE_TO_REFERNECE")
	{
		// BFME1 ScriptActionsReferences.cpp 0x002F5B00, assignment
		// 0x00345470 and copy 0x003455E0: the destination's raw name
		// belongs to the current side; the source is side-qualified.
		const auto key = std::make_pair(engine.currentSideName(), param(a, 0).stringValue);
		if (name == "SET_TEAM_REFERENCE_TO_REFERNECE")
		{
			const auto source = m_teamReferences.find(engine.qualify(param(a, 1).stringValue));
			if (!param(a, 1).stringValue.empty() && source != m_teamReferences.end()) m_teamReferences[key] = source->second;
		}
		else
		{
			Team *team = ScriptConditions::team(engine, param(a, 1).stringValue);
			m_teamReferences[key] = team ? team->getID() : 0;
			if (!team) engine.note("BFME1 SET_TEAM_REFERENCE: unresolved team '" + param(a, 1).stringValue + "'");
		}
		return true;
	}
	if (name == "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION" || name == "CREATE_REINFORCEMENT_TEAM")
	{
		// BFME1 action 444 at RVA 0x00304127 calls 0x003006B0 with
		// its final bool true (action 34 uses false). Plain unit-position
		// rows: 0x0030094F..0x00300A51, bounding SPHERE radius
		// (Object + 0xBC; GeometryInfo + 0x14), index * 2.25 in X,
		// radius * 2 between types in Y, ground Z. This creates the
		// faction libraries' opposition ThreatFinder probes.
		const auto q = engine.qualify(param(a, 0).stringValue);
		TeamPrototype *proto = m_logic.players().teams().findTeamPrototype(q.first, q.second);
		const bool atUnit = name == "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION";
		Object *origin = atUnit ? engine.getUnitNamed(param(a, 1).stringValue) : nullptr;
		const auto *waypoint = !atUnit && m_logic.terrain() ? m_logic.terrain()->findWaypointByName(param(a, 1).stringValue) : nullptr;
		if (!proto || (atUnit ? origin == nullptr : waypoint == nullptr)) return true;
		if (!engine.host() || !m_logic.aiWorld() || !m_logic.terrain())
		{
			engine.note("[S-1186] BFME1 reinforcement: no object creation host / AI world / terrain");
			return true;
		}
		const Dict &d = proto->getDict();
		const Coord3D destination = origin ? *origin->getPosition() : waypoint->location;
		Coord3D spawn = destination;
		if (const auto *alternate = m_logic.terrain()->findWaypointByName(d.getAsciiString("teamReinforcementOrigin"))) spawn = alternate->location;
		const bool move = spawn.x != destination.x || spawn.y != destination.y;
		Team *team = m_logic.players().teams().createTeam(proto);
		Object *carrier = nullptr;
		const std::string transport = d.getAsciiString("teamTransport");
		if (!transport.empty())
		{
			if (const auto *tt = m_logic.things().findTemplate(transport))
			{
				carrier = engine.host()->createObject(*tt, *team, spawn, 0.0f);
				if (carrier) carrier->friend_onBuildComplete();
			}
			else engine.note("BFME1 reinforcement: unknown transport template '" + transport + "'");
		}
		float rowY = spawn.y;
		for (int i = 1; i <= 7; ++i)
		{
			const auto n = std::to_string(i);
			const ThingTemplate *tt = m_logic.things().findTemplate(d.getAsciiString("teamUnitType" + n));
			if (!tt) continue;
			float lastRadius = 0.0f;
			float lastY = rowY;
			bool made = false;
			for (int k = 0; k < d.getInt("teamUnitMaxCount" + n); ++k)
			{
				Object *o = engine.host()->createObject(*tt, *team, Coord3D{0, 0, 0}, 0.0f);
				if (!o) continue;
				const float radius = o->ownPathfindGeometry(m_logic.aiWorld()->movementInfo(*tt).geometry).boundingSphereRadius();
				Coord3D pos{SimMath::addf32(SimMath::mulf32(SimMath::mulf32(radius, (float)k), 2.25f), spawn.x), rowY, spawn.z};
				if (origin && origin->isKindOfName("STRUCTURE") && !move)
					if (auto *ai = o->getAIUpdateInterface())
					{
						auto &pathfinder = m_logic.aiWorld()->pathfinder();
						pathfinder.adjustDestination(ai->pathfindObject(), ai->locomotorInfo(), &pos);
						ai->setLayer(pathfinder.getLayerAt(ai->pathfindObject(), pos));
					}
				pos.z = m_logic.terrain()->getGroundHeight(pos.x, pos.y);
				o->setPosition(&pos);
				o->setOrientation(0.0f);
				o->friend_onBuildComplete();
				lastRadius = radius;
				lastY = pos.y;
				made = true;
			}
			if (made) rowY = SimMath::addf32(lastY, SimMath::mulf32(lastRadius, 2.0f));
		}
		loadReinforcementTransports(*team, carrier, destination, d);
		if (move && !carrier)
		{
			std::vector<ObjectID> ids;
			for (Object *o : ScriptActions::teamAIGroup(*team)) ids.push_back(o->getID());
			AIGroup group(m_logic, ids);
			group.groupMoveToPosition(destination, false, CMD_FROM_SCRIPT);
		}
		return true;
	}
	if (name == "TEAM_ASSIMILATE_WITH_FIRST_WALK_ON_ARMY")
	{
		// BFME1 ScriptActions_doTeamAssimilateWithFirstWalkOnArmy.cpp:
		// there is no walk-on strategic army in an ordinary skirmish.
		return !m_logic.economy().context().livingWorld;
	}
	if (name == "TOGGLE_AUTO_BUILD")
	{
		if (Player *p = ScriptConditions::playerOfMask(engine, param(a, 0).stringValue)) m_autoBuild[p->getPlayerIndex()] = param(a, 1).intValue != 0;
		return true;
	}
	if (name == "TEAM_AVAILABLE_FOR_RECRUITMENT")
	{
		// BFME1 Team +0xE4/+0xE5: explicit availability overrides the
		// default-team / prototype flags, including an explicit false.
		if (Team *team = ScriptConditions::team(engine, param(a, 0).stringValue)) m_teamRecruitable[team->getID()] = param(a, 1).intValue != 0;
		return true;
	}
	if ((name == "UNIT_AFFECT_OBJECT_PANEL_FLAGS" || name == "TEAM_AFFECT_OBJECT_PANEL_FLAGS") && param(a, 1).stringValue == "AI_Recruitable")
	{
		// Native AIUpdate +0x32C, read by RVA 0x000F10F0. Objects
		// without an AI have no per-unit override in the original.
		const auto set = [&](Object *o) { if (o && o->getAIUpdateInterface()) m_unitRecruitable[o->getID()] = param(a, 2).intValue != 0; };
		if (name == "UNIT_AFFECT_OBJECT_PANEL_FLAGS") set(engine.getUnitNamed(param(a, 0).stringValue));
		else if (Team *team = ScriptConditions::team(engine, param(a, 0).stringValue))
			for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext()) set(o);
		return true;
	}
	if (name == "RECRUIT_TEAM" || name == "RECRUIT_TEAM_AT_TEAM")
	{
		const auto q = engine.qualify(param(a, 0).stringValue);
		auto *proto = m_logic.players().teams().findTeamPrototype(q.first, q.second);
		Coord3D at;
		const Coord3D *position = nullptr;
		if (name == "RECRUIT_TEAM_AT_TEAM")
		{
			Team *other = ScriptConditions::team(engine, param(a, 2).stringValue);
			if (!other) return true;
			at = estimateTeamPosition(*other);
			position = &at;
		}
		if (proto) recruitTeam(*proto, param(a, 1).realValue, position);
		return true;
	}
	if (name == "TEAM_RECRUIT_UNITS_FROM_TEAM" || name == "RECRUIT_COMBO_UNITS_FROM_TEAM")
	{
		Team *to = ScriptConditions::team(engine, param(a, 0).stringValue, true);
		Team *from = ScriptConditions::team(engine, param(a, 3).stringValue);
		if (to && from) recruitCombinedFromTeam(*to, *from, param(a, 2).stringValue, param(a, 1).intValue);
		return true;
	}
	if (name == "TEAM_RECRUIT_UNITS")
	{
		// BFME1 action 397, Team RVA 0x000F2FB0.
		Team *dst = ScriptConditions::team(engine, param(a, 0).stringValue, true);
		if (!dst) return true;
		const Coord3D center = estimateTeamPosition(*dst);
		const float radius = !dst->getFirstMember() ? 1000000.0f : m_logic.settings().legacyAI.maxRecruitRadius;
		for (int count = 0; count < param(a, 1).intValue; ++count)
		{
			Object *best = recruit(*dst, param(a, 2).stringValue, center, radius, false);
			if (!best) break;
			best->setTeam(dst);
		}
		return true;
	}
	if (name == "TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF")
	{
		// BFME1 ScriptActions_doTeamMoveToNearestObjectOfKindof.cpp,
		// RVA 0x002FC800: all players, closest matching object, group move.
		Team *team = ScriptConditions::team(engine, param(a, 0).stringValue);
		Coord3D center;
		const int kind = ObjectTemplateInfoBuilder::kindOfIndex(param(a, 1).stringValue);
		if (!team || kind < 0 || !ScriptConditions::teamCentreAllMembers(*team, center)) return true;
		Object *best = nullptr; float distance = 0.0f;
		for (int i = 0; i < m_logic.players().getPlayerCount(); ++i)
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() != m_logic.players().getNthPlayer(i) || !o->isKindOf((unsigned)kind) || o->isEffectivelyDead()) continue;
			const float dx = SimMath::subf32(center.x, o->getPosition()->x), dy = SimMath::subf32(center.y, o->getPosition()->y), dz = SimMath::subf32(center.z, o->getPosition()->z);
			const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), SimMath::mulf32(dz, dz));
			if (!best || d < distance) { best = o; distance = d; }
		}
		if (best)
		{
			std::vector<ObjectID> ids;
			for (Object *o : ScriptActions::teamAIGroup(*team)) ids.push_back(o->getID());
			AIGroup group(m_logic, ids);
			group.groupMoveToPosition(*best->getPosition(), false, CMD_FROM_SCRIPT);
		}
		return true;
	}
	if (name == "SET_COMMAND_POINTS_TO_BUILD_TEAM_TO_COUNTER")
	{
		const auto q = engine.qualify(param(a, 0).stringValue);
		if (const auto *proto = m_logic.players().teams().findTeamPrototype(q.first, q.second)) engine.counter(param(a, 1).stringValue).value = teamPoints(m_logic, *proto);
		return true;
	}
	if (name == "SET_COUNTER_TO_UNIT_THREAT" || name == "SET_COUNTER_TO_TEAM_THREAT")
	{
		float value = 0.0f;
		if (name == "SET_COUNTER_TO_UNIT_THREAT")
		{
			if (Object *o = engine.getUnitNamed(param(a, 1).stringValue)) value = threat(*o, *o->getPosition(), param(a, 2).realValue, { 0, o->getID() });
		}
		else
		{
			Team *t = ScriptConditions::team(engine, param(a, 1).stringValue);
			Coord3D center;
			if (t && t->getFirstMember() && ScriptConditions::teamCentreAllMembers(*t, center)) value = threat(*t->getFirstMember(), center, param(a, 2).realValue, { 1, t->getID() });
		}
		engine.counter(param(a, 0).stringValue).value = SimMath::truncToInt32(value);
		return true;
	}
	if (name == "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER")
	{
		// BFME1 RVA 0x002FE590, Rva002FE590CastleBuild.cpp. The marker
		// is the closest object of the exact template name to the base.
		Object *base = engine.getUnitNamed(param(a, 3).stringValue);
		Player *p = base ? base->getControllingPlayer() : nullptr;
		const ThingTemplate *tt = m_logic.things().findTemplate(param(a, 0).stringValue);
		CastleBehavior *castle = base ? dynamic_cast<CastleBehavior *>(base->findModule("CastleBehavior")) : nullptr;
		if (!p || !p->canBuildBase() || p != engine.currentPlayer() || !tt || !castle || !castle->canBuildAtBase(tt) ||
			BuildAssistant::calcCostToBuild(*tt, p, nullptr, -1) > (int)p->getMoney()->countMoney()) return true;
		Object *marker = nullptr;
		float distance = 99999.0f;
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() != param(a, 2).stringValue) continue;
			const float d = markerDistance(*o->getPosition(), *base->getPosition());
			if (!marker || d < distance) { marker = o; distance = d; }
		}
		Object *made = marker ? castle->buildAtBaseNear(*tt, *marker->getPosition(), param(a, 1).intValue) : castle->buildAtBase(*tt);
		if (made && !param(a, 4).stringValue.empty()) { engine.setReference(param(a, 4).stringValue, *made); engine.nameInCache(param(a, 4).stringValue, *made); }
		return true;
	}
	if (name == "BUILD_TEAM")
	{
		const auto q = engine.qualify(param(a, 0).stringValue);
		if (auto *proto = m_logic.players().teams().findTeamPrototype(q.first, q.second)) buildTeam(*proto, true);
		return true;
	}
	return false;
}

float Bfme1SkirmishAI::threat(Object &source, const Coord3D &position, float radius, std::pair<int, unsigned> key)
{
	// BFME1 Object query RVA 0x001D2580 -> 0x001CF710 (BfmeConv891.cpp),
	// Team RVA 0x000F72D0 -> 0x000F67B0 (Rva000F67B0Refresh.cpp).
	// Unlike RotWK's AIThreatFinder this is a sum of raw ThreatLevel, with
	// no health/price/category weighting; alive enemies on the same OFF_MAP
	// layer, in the partition iterator's order. The cache expires after 25 frames.
	ThreatCache &cache = m_threat[key];
	if (cache.next >= m_logic.getFrame()) return cache.value;
	PartitionFilterFn filter([&source](Object &o) {
		return !o.isEffectivelyDead() && o.isOffMap() == source.isOffMap() && source.getRelationship(o) == ENEMIES;
	});
	cache.value = 0.0f;
	for (const auto &hit : m_logic.partition().iterateObjectsInRange(position, radius, FROM_CENTER_2D, { &filter }, ITER_FASTEST))
	{
		const FieldValue *v = hit.object->getTemplate()->findField("ThreatLevel");
		const float *f = v ? std::get_if<float>(v) : nullptr;
		// ThingTemplateConstructor.cpp: ThreatLevel starts at float 1.0.
		cache.value = SimMath::addf32(cache.value, f ? *f : 1.0f);
	}
	cache.next = m_logic.getFrame() + 25;
	return cache.value;
}

bool Bfme1SkirmishAI::evaluateCondition(ScriptEngine &engine, const ScriptCondition &c, bool &result)
{
	const auto *tmpl = ScriptTemplates::condition(c.resolved);
	if (!tmpl) return false;
	if (tmpl->name == "SKIRMISH_SPECIAL_POWER_READY")
	{
		// BFME1 condition 85, RVA 0x0032B8D0. The mutable integer belongs
		// to this condition's power parameter; successful checks leave it alone.
		result = false;
		const std::string key = engine.conditionIdentity(c);
		if (key.empty() || !TheSpecialPowerStore)
		{
			engine.note("[S-5018] BFME1 power readiness: missing condition identity or SpecialPowerStore");
			return true;
		}
		int &cached = m_powerReadyFrames.emplace(key, c.params.at(1).intValue).first->second;
		const unsigned now = m_logic.getFrame();
		if (cached == -1 || (cached > 0 && (unsigned)cached > now)) return true;
		unsigned next = now + 50;
		const auto *power = TheSpecialPowerStore->findSpecialPowerTemplate(c.params.at(1).stringValue);
		if (!power) { cached = -1; return true; }
		for (Player *p : ScriptConditions::players(engine, c.params.at(0).stringValue))
		{
			for (const auto &proto : m_logic.players().teams().prototypes())
			{
				if (proto->getControllingPlayer() != p) continue;
				for (auto it = proto->teams().rbegin(); it != proto->teams().rend(); ++it)
				{
					for (Object *o = (*it)->getLastMember(); o; o = o->friend_teamPrev())
					{
						if (o->isUnderConstruction() || o->getDisabledMask()) continue;
						auto *module = SpecialPowerModules::findModule(*o, power);
						if (!module || !SpecialPowerModules::canUseSpecialPower(*o, power)) continue;
						if (module->isReady()) { result = true; return true; }
						next = std::min(next, module->getReadyFrame());
					}
				}
			}
		}
		cached = (int)next;
		return true;
	}
	if (tmpl->name == "HAS_COMMAND_POINTS_TO_BUILD_TEAM")
	{
		const auto name = engine.qualify(c.params.at(1).stringValue);
		const auto *proto = m_logic.players().teams().findTeamPrototype(name.first, name.second);
		Player *p = ScriptConditions::playerOfMask(engine, c.params.at(0).stringValue);
		result = p && proto && teamPoints(m_logic, *proto) <= p->commandPointsAvailable();
		return true;
	}
	if (tmpl->name != "UNIT_THREAT_LEVEL" && tmpl->name != "TEAM_THREAT_LEVEL") return false;
	float value = 0.0f;
	if (tmpl->name == "UNIT_THREAT_LEVEL")
	{
		Object *o = engine.getUnitNamed(c.params.at(0).stringValue);
		if (!o) { result = false; return true; }
		value = threat(*o, *o->getPosition(), c.params.at(3).realValue, { 0, o->getID() });
	}
	else
	{
		Team *t = ScriptConditions::team(engine, c.params.at(0).stringValue);
		Coord3D center;
		if (!t || !t->getFirstMember() || !ScriptConditions::teamCentreAllMembers(*t, center)) { result = false; return true; }
		value = threat(*t->getFirstMember(), center, c.params.at(3).realValue, { 1, t->getID() });
	}
	const float threshold = c.params.at(2).realValue;
	switch (c.params.at(1).intValue)
	{
	case 0: result = value < threshold; break;
	case 1: result = value <= threshold; break;
	case 2: result = value == threshold; break;
	case 3: result = value >= threshold; break;
	case 4: result = value > threshold; break;
	case 5: result = value != threshold; break;
	default: result = false; break;
	}
	return true;
}

void Bfme1SkirmishAI::crc(StateHasher &h) const
{
	h.addString("BFME1 AIPlayer");
	const auto &cfg = m_logic.settings().legacyAI;
	h.addFloat(cfg.teamSeconds); h.addU32(cfg.wealthy); h.addU32(cfg.poor); h.addFloat(cfg.wealthyRate); h.addFloat(cfg.poorRate); h.addFloat(cfg.resourcesToStart);
	h.addFloat(cfg.maxRecruitRadius);
	h.addU32((unsigned)m_players.size());
	for (const auto &p : m_players) { h.addI32(p.player); h.addI32(p.timer); h.addI32(p.delay); h.addBool(p.ready); }
	for (const auto *queue : { &m_building, &m_ready })
	{
		h.addU32((unsigned)queue->size());
		for (const auto &q : *queue)
		{
			h.addU32(q.team); h.addU32(q.started); h.addBool(q.reinforcement); h.addBool(q.priority); h.addU32(q.reinforcementID);
			h.addU32((unsigned)q.members.size());
			for (const auto &m : q.members) { h.addString(m.type); h.addI32(m.remaining); h.addBool(m.required); }
		}
	}
	for (const auto *flags : { &m_teamRecruitable, &m_unitRecruitable })
	{
		h.addU32((unsigned)flags->size());
		for (const auto &flag : *flags) { h.addU32(flag.first); h.addBool(flag.second); }
	}
	h.addU32((unsigned)m_autoBuild.size());
	for (const auto &flag : m_autoBuild) { h.addI32(flag.first); h.addBool(flag.second); }
	h.addU32((unsigned)m_teamReferences.size());
	for (const auto &ref : m_teamReferences) { h.addString(ref.first.first); h.addString(ref.first.second); h.addU32(ref.second); }
	h.addU32((unsigned)m_orders.size());
	for (const auto &o : m_orders) { h.addU32(o.factory); h.addU32(o.productionID); h.addU32(o.team); h.addString(o.type); h.addI32(o.member); }
	h.addU32((unsigned)m_retired.size()); for (auto id : m_retired) h.addU32(id);
	h.addU32((unsigned)m_threat.size()); for (const auto &c : m_threat) { h.addI32(c.first.first); h.addU32(c.first.second); h.addU32(c.second.next); h.addFloat(c.second.value); }
	h.addU32((unsigned)m_conditionFrames.size()); for (const auto &c : m_conditionFrames) { h.addI32(c.first); h.addU32(c.second); }
	h.addU32((unsigned)m_powerReadyFrames.size()); for (const auto &c : m_powerReadyFrames) { h.addString(c.first); h.addI32(c.second); }
}
void Bfme1SkirmishAI::xfer(Xfer &x)
{
	x.xferVectorSize(m_players, 13);
	for (auto &p : m_players)
	{
		x.xferInt(&p.player); x.xferInt(&p.timer); x.xferInt(&p.delay); x.xferBool(&p.ready);
		if (x.isLoading() && !m_logic.players().getNthPlayer(p.player)) throw XferException("BFME1 AI: invalid player");
	}
	for (auto *queue : { &m_building, &m_ready })
	{
		std::set<TeamID> queued;
		x.xferVectorSize(*queue, 18);
		for (auto &q : *queue)
		{
			x.xferUnsignedInt(&q.team); x.xferUnsignedInt(&q.started);
			x.xferBool(&q.reinforcement); x.xferBool(&q.priority); x.xferObjectID(&q.reinforcementID);
			x.xferVectorSize(q.members, 6);
			for (auto &m : q.members)
			{
				x.xferAsciiString(&m.type); x.xferInt(&m.remaining); x.xferBool(&m.required);
				if (x.isLoading() && (m.remaining < 0 || !m_logic.things().findTemplate(m.type))) throw XferException("BFME1 AI: invalid team member request");
			}
			if (x.isLoading() && (!m_logic.players().teams().findTeamByID(q.team) || (queue == &m_building && !queued.insert(q.team).second))) throw XferException("BFME1 AI: invalid or duplicate queued team");
		}
	}
	for (auto *flags : { &m_teamRecruitable, &m_unitRecruitable })
	{
		unsigned size = (unsigned)flags->size(); x.xferUnsignedInt(&size);
		if (size > 1000000) throw XferException("BFME1 AI: too many recruitment overrides");
		if (x.isLoading()) for (unsigned i = 0; i < size; ++i)
		{
			unsigned id = 0; bool value = false; x.xferUnsignedInt(&id); x.xferBool(&value);
			if (!id || !flags->emplace(id, value).second) throw XferException("BFME1 AI: invalid recruitment override");
			// Unit flags can outlive destroyed objects, just as other AI ID
			// caches do; team IDs are retained by the engine for snapshots.
			if (flags == &m_teamRecruitable && !m_logic.players().teams().findTeamByID(id)) throw XferException("BFME1 AI: invalid recruitment team");
		}
		else for (const auto &flag : *flags) { unsigned id = flag.first; bool value = flag.second; x.xferUnsignedInt(&id); x.xferBool(&value); }
	}
	unsigned autoSize = (unsigned)m_autoBuild.size(); x.xferUnsignedInt(&autoSize);
	if (autoSize > 1000000) throw XferException("BFME1 AI: too many auto-build overrides");
	if (x.isLoading()) for (unsigned i = 0; i < autoSize; ++i)
	{
		int id = 0; bool value = false; x.xferInt(&id); x.xferBool(&value);
		if (!m_logic.players().getNthPlayer(id) || !m_autoBuild.emplace(id, value).second) throw XferException("BFME1 AI: invalid auto-build player");
	}
	else for (const auto &flag : m_autoBuild) { int id = flag.first; bool value = flag.second; x.xferInt(&id); x.xferBool(&value); }
	unsigned references = (unsigned)m_teamReferences.size(); x.xferUnsignedInt(&references);
	if (references > 1000000) throw XferException("BFME1 AI: too many team references");
	if (x.isLoading()) for (unsigned i = 0; i < references; ++i)
	{
		std::string side, name; unsigned id = 0;
		x.xferAsciiString(&side); x.xferAsciiString(&name); x.xferUnsignedInt(&id);
		if ((id && !m_logic.players().teams().findTeamByID(id)) || !m_teamReferences.emplace(std::make_pair(side, name), id).second) throw XferException("BFME1 AI: invalid team reference");
	}
	else for (const auto &ref : m_teamReferences)
	{
		std::string side = ref.first.first, name = ref.first.second; unsigned id = ref.second;
		x.xferAsciiString(&side); x.xferAsciiString(&name); x.xferUnsignedInt(&id);
	}
	x.xferVectorSize(m_orders, 17);
	for (auto &o : m_orders)
	{
		x.xferObjectID(&o.factory); x.xferUnsignedInt(&o.productionID); x.xferUnsignedInt(&o.team); x.xferAsciiString(&o.type); x.xferInt(&o.member);
		if (x.isLoading() && (!m_logic.players().teams().findTeamByID(o.team) || !m_logic.things().findTemplate(o.type))) throw XferException("BFME1 AI: invalid production order");
		if (x.isLoading())
		{
			const auto team = std::find_if(m_building.begin(), m_building.end(), [&](const BuildingTeam &q) { return q.team == o.team; });
			if (team == m_building.end() || o.member < 0 || (size_t)o.member >= team->members.size() || team->members[(size_t)o.member].type != o.type)
				throw XferException("BFME1 AI: production order has no matching work order");
		}
	}
	unsigned retiredSize = (unsigned)m_retired.size();
	x.xferUnsignedInt(&retiredSize);
	if (retiredSize > 1000000) throw XferException("BFME1 AI: too many retired teams");
	if (x.isLoading())
	{
		for (unsigned i = 0; i < retiredSize; ++i) { unsigned id = 0; x.xferUnsignedInt(&id); if (!m_retired.insert(id).second || !m_logic.players().teams().findTeamByID(id)) throw XferException("BFME1 AI: invalid retired team"); }
	}
	else for (unsigned id : m_retired) x.xferUnsignedInt(&id);
	unsigned threatSize = (unsigned)m_threat.size();
	x.xferUnsignedInt(&threatSize);
	if (threatSize > 1000000) throw XferException("BFME1 AI: too many threat records");
	if (x.isLoading())
	{
		for (unsigned i = 0; i < threatSize; ++i)
		{
			int kind = 0; unsigned id = 0; ThreatCache cache;
			x.xferInt(&kind); x.xferUnsignedInt(&id); x.xferUnsignedInt(&cache.next); x.xferReal(&cache.value);
			if ((kind != 0 && kind != 1) || !m_threat.emplace(std::make_pair(kind, id), cache).second) throw XferException("BFME1 AI: invalid threat record");
		}
	}
	else for (const auto &c : m_threat)
	{
		int kind = c.first.first; unsigned id = c.first.second; ThreatCache cache = c.second;
		x.xferInt(&kind); x.xferUnsignedInt(&id); x.xferUnsignedInt(&cache.next); x.xferReal(&cache.value);
	}
	unsigned size = (unsigned)m_conditionFrames.size();
	x.xferUnsignedInt(&size);
	if (size > 65536) throw XferException("BFME1 AI: too many production conditions");
	if (x.isLoading())
	{
		m_conditionFrames.clear();
		for (unsigned i = 0; i < size; ++i)
		{
			int key = 0; unsigned frame = 0;
			x.xferInt(&key); x.xferUnsignedInt(&frame);
			if (!m_conditionFrames.emplace(key, frame).second) throw XferException("BFME1 AI: duplicate production condition");
		}
	}
	else for (const auto &entry : m_conditionFrames) { int key = entry.first; unsigned frame = entry.second; x.xferInt(&key); x.xferUnsignedInt(&frame); }
	unsigned powers = (unsigned)m_powerReadyFrames.size(); x.xferUnsignedInt(&powers);
	if (powers > 1000000) throw XferException("BFME1 AI: too many power readiness conditions");
	if (x.isLoading()) for (unsigned i = 0; i < powers; ++i)
	{
		std::string key; int frame = 0; x.xferAsciiString(&key); x.xferInt(&frame);
		if (key.empty() || !m_powerReadyFrames.emplace(key, frame).second) throw XferException("BFME1 AI: invalid power readiness condition");
	}
	else for (const auto &entry : m_powerReadyFrames) { std::string key = entry.first; int frame = entry.second; x.xferAsciiString(&key); x.xferInt(&frame); }
}
std::vector<std::string> Bfme1SkirmishAI::report() const
{
	return {
		"BFME1 AIPlayer: " + std::to_string(m_players.size()) + " computer players, " + std::to_string(m_building.size()) + " teams building, " + std::to_string(m_orders.size()) + " production orders",
		"[S-5018] BFME1 skirmish AI: shared horde/transport, hero revival, combat, movement and script fidelity gaps remain S-5017 / S-1186 / S-1190"
	};
}
