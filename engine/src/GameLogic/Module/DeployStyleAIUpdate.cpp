// OpenBFME. GPL-3.0.
// See GameLogic/Module/DeployStyleAIUpdate.h for the sources of every rule.

#include "GameLogic/Module/DeployStyleAIUpdate.h"

#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/AsciiString.h"
#include "Common/NumericState.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace
{
const char kStopDeployStyle[] =
	"[S-4945] DeployStyleAIUpdate / ToggleDeploySpecialAbilityUpdate (lane SIEGE-1, RW 0x89251C / 0x8921E3 / 0x89209F / 0x8B1D10): the states, the deploy and "
	"undeploy requests, the unpack / pack times, DEPLOYED / UNPACKING / PACKING, the DeployedAttributeModifier, the turret enable / recentre and the kept command "
	"run. Not ported: aiDoCommand's bookkeeping for the commands the port does not route to a subclass (AIUpdateInterface::routeCommand: only 0, 0x36, the move "
	"with an angle, 0xB, 0xC, 0xE and 0xF reach it; guard, hunt, enter and the rest run without the pre-idle, the kept command and the target flags: "
	"INFERENCE), so the guard-position branch (+ 0x597, command 0x1E: INFERENCE from the ZH command order) is unreachable; the AI-source classification copy "
	"(+ 0x570) of an AI command given elsewhere (only this class's own idles and attacks keep it); the drawable's animation time and the template's \"Deploy\" / "
	"\"Undeploy\" unit sounds (RW 0x6726B9 / 0x674FFE: client); RW 0x66844A's two arguments (the port's nextMoodTarget has none); the toggle on an object "
	"whose AI is not a DeployStyleAIUpdate (retail writes into it unchecked: the port does nothing)";

std::string rawField(const ModuleData *data, const char *field)
{
	const RawModuleData *raw = dynamic_cast<const RawModuleData *>(data);
	if (!raw)
	{
		return std::string();
	}
	auto trim = [](const std::string &s) {
		const size_t b = s.find_first_not_of(" \t\r\n");
		const size_t e = s.find_last_not_of(" \t\r\n");
		return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
	};
	std::string value;
	for (const RawModuleData::Line &l : raw->lines())
	{
		std::string text = l.text;
		const size_t cut = text.find(';');
		if (cut != std::string::npos)
		{
			text.erase(cut);
		}
		const size_t eq = text.find('=');
		if (eq != std::string::npos && AsciiStringUtil::compareNoCase(trim(text.substr(0, eq)), field) == 0)
		{
			value = trim(text.substr(eq + 1)); // a later line replaces an earlier one (the field parser stores again)
		}
	}
	return value;
}

std::string firstToken(const std::string &s)
{
	const size_t e = s.find_first_of(" \t");
	return e == std::string::npos ? s : s.substr(0, e);
}

// RW 0x73A429 (parseDurationUnsignedInt): milliseconds to logic frames
unsigned parseDuration(const ModuleData *data, const char *field, unsigned def)
{
	const std::string v = rawField(data, field);
	if (v.empty())
	{
		return def;
	}
	char *end = nullptr;
	const unsigned long ms = std::strtoul(v.c_str(), &end, 10);
	if (end == v.c_str())
	{
		throw std::logic_error(std::string("DeployStyleAIUpdate: ") + field + " is not a number: " + v);
	}
	return NumericState::ceilScaled((std::uint32_t)ms, 0.005f);
}

// RW 0x42E558 (parseBool)
bool parseBoolField(const ModuleData *data, const char *field, bool def)
{
	const std::string v = firstToken(rawField(data, field));
	if (v.empty())
	{
		return def;
	}
	if (AsciiStringUtil::compareNoCase(v, "yes") == 0)
	{
		return true;
	}
	if (AsciiStringUtil::compareNoCase(v, "no") == 0)
	{
		return false;
	}
	throw std::logic_error(std::string("DeployStyleAIUpdate: ") + field + " is not Yes / No: " + v);
}

int condition(const char *name)
{
	const int c = CombatNames::modelCondition(name);
	if (c < 0)
	{
		throw std::logic_error(std::string("DeployStyleAIUpdate: the model condition ") + name + " is not in the binary's table");
	}
	return c;
}

int mcPacking()
{
	static const int c = condition("PACKING"); // 0x5E
	return c;
}
int mcUnpacking()
{
	static const int c = condition("UNPACKING"); // 0x60
	return c;
}
int mcDeployed()
{
	static const int c = condition("DEPLOYED"); // 100
	return c;
}

Object::ModelConditionBits bitsOf(std::initializer_list<int> bits)
{
	Object::ModelConditionBits b{};
	for (int bit : bits)
	{
		b[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	return b;
}

// RW 0x8DC507 through RW 0x6622E6: the turret's object target (null when it aims at nothing or at a position)
Object *turretTargetObject(TurretAI &t)
{
	Object *obj = nullptr;
	Coord3D pos{ 0.0f, 0.0f, 0.0f };
	return t.getTurretTarget(obj, pos) == 1 ? obj : nullptr;
}

constexpr int kMaxShots = 0x7FFFFFFF; // RW 0x89266C / 0x892913
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// DeployStyleAIUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
DeployStyleAIUpdate::DeployStyleAIUpdate(Thing *thing, const ModuleData *data)
	: AIUpdateInterface(thing, data)
{
	// the data fields (RW table 0xC05010); the AI's module data is raw, so they are read from its lines (the base's fields: AIUpdateInterface)
	m_unpackFrames = parseDuration(data, "UnpackTime", 0);
	m_packFrames = parseDuration(data, "PackTime", 0);
	m_resetTurretBeforePacking = parseBoolField(data, "ResetTurretBeforePacking", false);
	m_turretsOnlyWhenDeployed = parseBoolField(data, "TurretsFunctionOnlyWhenDeployed", false);
	m_turretsMustCenter = parseBoolField(data, "TurretsMustCenterBeforePacking", false);
	m_mustDeployToAttack = parseBoolField(data, "MustDeployToAttack", true); // the data constructor's default 1
	m_deployedModifier = firstToken(rawField(data, "DeployedAttributeModifier"));
	m_kept.type = -1;
	if (Object *o = thing->asObject())
	{
		o->logic().noteStop(kStopDeployStyle);
	}
}

const char *DeployStyleAIUpdate::stopLine()
{
	return kStopDeployStyle;
}

DeployStyleAIUpdate *DeployStyleAIUpdate::of(const Object &obj)
{
	return dynamic_cast<DeployStyleAIUpdate *>(const_cast<Object &>(obj).getAIUpdateInterface());
}

// RW 0x891CAF
bool DeployStyleAIUpdate::hasPendingCommand() const
{
	return m_pending && m_kept.type != -1;
}

// RW 0x891CD3
bool DeployStyleAIUpdate::isIdle() const
{
	if (hasPendingCommand())
	{
		return false;
	}
	return AIUpdateInterface::isIdle(); // RW 0x6643FC
}

// RW 0x891CEB
void DeployStyleAIUpdate::resetRequests()
{
	m_victim = INVALID_ID;
	m_attackCommand = false;
	m_updateAttacked = false;
	m_attackMoveCommand = false;
	m_attackObjectCommand = false;
	m_attackObject = INVALID_ID;
	m_attackPositionCommand = false;
	m_deployRequest = false;
	m_undeployRequest = false;
	m_attackPos = Coord3D{ 0.0f, 0.0f, 0.0f };
}

// RW 0x891D3B
void DeployStyleAIUpdate::requestDeploy()
{
	m_undeployRequest = false;
	m_deployRequest = m_state != READY_TO_ATTACK && m_state != DEPLOY;
}

// RW 0x891D62
void DeployStyleAIUpdate::requestUndeploy()
{
	m_deployRequest = false;
	m_undeployRequest = m_state != READY_TO_MOVE && m_state != UNDEPLOY;
}

// RW 0x5E821A(2): AICommandInterface::aiIdle(CMD_FROM_AI) reaches aiDoCommand (RW 0x89209F) as command 5 from the AI: its CMD_FROM_AI branch keeps the
// classification, then the base runs the idle
void DeployStyleAIUpdate::idleFromAI()
{
	m_classAtCommand = m_class;
	aiIdle(CMD_FROM_AI);
}

// RW 0x89209F for the commands AIUpdateInterface routes here (the gate RW 0x667174 has passed). true: the command waits (the base does not run it)
bool DeployStyleAIUpdate::routeCommand(const RoutedCommand &c)
{
	// RW 0x8920B8 .. 0x8920C9: any command but idle (5) and 0x35 first idles the unit (none of the routed ones is 5 or 0x35)
	idleFromAI();
	if (c.source == CMD_FROM_AI)
	{
		m_classAtCommand = m_class; // RW 0x8921C9 .. 0x8921D9, then the base (RW 0x66A7EC)
		return false;
	}
	resetRequests(); // RW 0x8920DA
	// RW 0x8920E4 .. 0x8920F7: the command kept (RW 0x75307D); a call that is not a replay clears + 0x56C
	m_kept = c;
	m_keptTarget = c.target ? c.target->getID() : INVALID_ID;
	m_kept.target = nullptr;
	if (!m_replaying)
	{
		m_ran = false;
	}
	// RW 0x892127 .. 0x8921C7: the command's flags. INFERENCE: retail sets them after the base ran the command; here the base runs after this returns
	// (nothing the base command does reads them)
	switch (c.type)
	{
	case 0xB:
	case 0xC: // and 0x39 (not routed)
		m_attackObjectCommand = true;
		m_attackObject = c.target ? c.target->getID() : INVALID_ID;
		break;
	case 0xE:
		m_attackPositionCommand = true;
		m_attackPos = c.position;
		break;
	case 0xF: // 0xF .. 0x12, 0x1F, 0x21, 0x23 (only 0xF is routed)
		m_attackCommand = true;
		break;
	default:
		break;
	}
	// RW 0x8920FE .. 0x892120: outside the (un)packing the command runs and + 0x56C is set; inside it waits (+ 0x56D)
	if (m_state != DEPLOY && m_state != UNDEPLOY)
	{
		m_ran = true;
		return false;
	}
	m_pending = true;
	return true;
}

// the routed command given again through the command interface (aiDoCommand with the replay flag)
void DeployStyleAIUpdate::dispatch(const RoutedCommand &c)
{
	switch (c.type)
	{
	case 0:
		aiMoveToPosition(c.position, c.source);
		return;
	case 0x36:
		aiMoveToPositionEvenIfSleeping(c.position, c.source);
		return;
	case kCommandUnidentifiedMove:
		aiMoveToPositionAndOrientate(c.position, c.angle, c.source);
		return;
	case 0xB:
		aiAttackObject(c.target, c.source, c.maxShots);
		return;
	case 0xC:
		aiForceAttackObject(c.target, c.source);
		return;
	case 0xE:
		aiAttackPosition(c.position, c.maxShots, c.source);
		return;
	case 0xF:
		armAttackMove(c.position, c.source);
		return;
	default:
		throw std::logic_error("DeployStyleAIUpdate: a kept command of a type the port does not route");
	}
}

// RW 0x891F9B: the kept command again (RW 0x75315B copies it), + 0x56D cleared, aiDoCommand(command, 1)
void DeployStyleAIUpdate::replayPending()
{
	RoutedCommand c = m_kept;
	c.target = m_keptTarget != INVALID_ID ? getObject()->logic().findObjectByID(m_keptTarget) : nullptr;
	m_pending = false;
	++m_replays;
	if ((c.type == 0xB || c.type == 0xC) && !c.target)
	{
		return; // the object is gone: the base's attack takes no null victim (aiAttackObject / aiForceAttackObject return at once)
	}
	const bool was = m_replaying;
	m_replaying = true;
	dispatch(c);
	m_replaying = was;
}

// RW 0x8921E3
void DeployStyleAIUpdate::setMyState(int state)
{
	Object &obj = *getObject();
	m_state = state;
	const unsigned now = obj.logic().getFrame();
	switch (state)
	{
	case READY_TO_MOVE:
		m_frame = 0;
		if (hasPendingCommand())
		{
			replayPending();
		}
		obj.clearAndSetModelConditionFlags(bitsOf({ mcPacking() }), Object::ModelConditionBits{}); // RW 0x5E3B79(0x5E)
		break;
	case DEPLOY:
		idleFromAI();
		obj.clearAndSetModelConditionFlags(bitsOf({ mcPacking() }), bitsOf({ mcUnpacking() })); // RW 0x68D607
		// RW 0x89227A .. 0x8922F6: the drawable's animation time (RW 0x6726B9) and its "Deploy" unit sound: client (S-4945)
		m_frame = m_unpackFrames + now;
		break;
	case READY_TO_ATTACK:
		m_frame = 0;
		if (!m_attackCommand && hasPendingCommand())
		{
			replayPending();
		}
		obj.clearAndSetModelConditionFlags(bitsOf({ mcUnpacking() }), bitsOf({ mcDeployed() }));
		if (m_turretsOnlyWhenDeployed)
		{
			if (TurretAI *t = turretForCurWeapon()) // RW 0x66243F
			{
				t->setTurretEnabled(true); // RW 0x662332(turret, 1)
			}
		}
		if (!m_deployedModifier.empty())
		{
			obj.addAttributeModifier(m_deployedModifier, -1); // RW 0x68F1A8(name, -1)
		}
		break;
	case UNDEPLOY:
		idleFromAI();
		obj.clearAndSetModelConditionFlags(bitsOf({ mcUnpacking(), mcDeployed() }), bitsOf({ mcPacking() }));
		// RW 0x8923E0 .. 0x89245C: the drawable's animation time and its "Undeploy" unit sound: client (S-4945)
		m_frame = m_packFrames + now;
		if (m_turretsOnlyWhenDeployed)
		{
			if (TurretAI *t = turretForCurWeapon())
			{
				t->setTurretEnabled(false); // RW 0x662332(turret, 0)
			}
		}
		if (!m_deployedModifier.empty())
		{
			obj.removeAttributeModifier(m_deployedModifier); // RW 0x68F259
		}
		break;
	case ALIGNING_TURRETS:
		m_frame = 0;
		if (TurretAI *t = turretForCurWeapon())
		{
			t->recenterTurret(); // RW 0x66234D
		}
		break;
	default:
		throw std::logic_error("DeployStyleAIUpdate: no such state");
	}
}

// RW 0x89251C .. 0x8929D3
UpdateSleepTime DeployStyleAIUpdate::update()
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	ObjectWeapons *weapons = obj.getWeapons();
	const bool hasWeapon = weapons && weapons->currentWeapon(); // RW 0x68B58C(0)
	bool inRange = false;    // [esp + 0x11]
	bool haveTarget = false; // [esp + 0x12]
	Object *victim = nullptr;
	if (hasWeapon)
	{
		if (m_attackPositionCommand)
		{
			// RW 0x892545 .. 0x892573: Weapon::isWithinAttackRange (RW 0x6CC07C) from the object's position to the attack position, extra 0, the minimum range
			inRange = weapons->isWithinAttackRange(m_attackPos);
			haveTarget = true;
		}
		else if (m_attackObjectCommand)
		{
			// RW 0x892578 .. 0x8925C0: the attack object when it lives (+ 0x458 bit 0), RW 0x6CC653
			victim = logic.findObjectByID(m_attackObject);
			if (victim && victim->isEffectivelyDead())
			{
				victim = nullptr;
			}
			if (victim)
			{
				inRange = weapons->isWithinAttackRange(*victim, 0.0f, true);
				haveTarget = true;
			}
		}
		else if (m_attackCommand)
		{
			// RW 0x8925C2 .. 0x8926E0: the turret's object target, else the machine's goal object (RW 0x8DBACE), else the remembered victim
			bool retargeted = false; // [esp + 0x13]
			if (TurretAI *t = turretForCurWeapon())
			{
				victim = turretTargetObject(*t);
			}
			else
			{
				victim = stateMachine().goalObject();
			}
			if (!victim)
			{
				victim = logic.findObjectByID(m_victim);
			}
			if (victim && victim->isEffectivelyDead())
			{
				victim = getNextMoodTarget(true, false); // RW 0x66844A(1, 0)
				retargeted = true;
			}
			if (!victim)
			{
				if (m_attackMoveCommand)
				{
					// RW 0x892631 .. 0x89268E: the guard position's mood target, attacked once it is in range
					victim = getNextMoodTarget(false, false); // RW 0x66844A(0, 0)
					if (victim)
					{
						inRange = weapons->isWithinAttackRange(*victim, 0.0f, true);
						haveTarget = true;
						if (inRange)
						{
							aiAttackObject(victim, CMD_FROM_AI, kMaxShots); // RW 0x66C536(victim, 0x7FFFFFFF, CMD_FROM_AI)
							m_updateAttacked = true;
							m_victim = victim->getID();
						}
						else
						{
							victim = nullptr;
						}
					}
				}
				else
				{
					m_victim = INVALID_ID; // RW 0x8926E0
				}
			}
			else
			{
				// RW 0x892694 .. 0x8926DE
				inRange = weapons->isWithinAttackRange(*victim, 0.0f, true);
				haveTarget = true;
				m_victim = victim->getID();
				if (m_updateAttacked && retargeted && inRange)
				{
					aiAttackObject(victim, CMD_FROM_AI, kMaxShots);
				}
			}
		}
	}
	// RW 0x8926E7 .. 0x89270E: the guard position that holds still without a victim deploys / stays deployed
	const bool waiting = mover().isWaitingForPath(); // AI + 0x3B1
	const bool guardStill = m_attackMoveCommand && !victim && !isMoving() && !waiting;
	// RW 0x892710 .. 0x892746: the classification
	if (haveTarget)
	{
		m_class = inRange ? 2 : 3;
	}
	else
	{
		m_class = (!isMoving() && !waiting) ? 0 : 1;
	}
	const unsigned now = logic.getFrame();
	switch (m_state)
	{
	case READY_TO_MOVE:
		if (m_deployRequest)
		{
			// RW 0x89292A .. 0x892958
			setMyState(DEPLOY);
			m_deployRequest = false;
			if (m_classAtCommand == 2)
			{
				m_pending = true;
			}
			else if (m_ran)
			{
				m_kept.type = -1; // RW 0x892879: the kept command dropped
				m_pending = false;
				resetRequests();
			}
		}
		else if (guardStill || (inRange && haveTarget && m_mustDeployToAttack))
		{
			// RW 0x89295D .. 0x892991
			setMyState(DEPLOY);
			if (!m_ran || m_classAtCommand != 0)
			{
				m_pending = true;
			}
		}
		break;
	case DEPLOY:
		// RW 0x8928C6 .. 0x892928
		if (m_frame != 0 && now >= m_frame)
		{
			setMyState(READY_TO_ATTACK);
			if (m_attackCommand && inRange && haveTarget && victim)
			{
				aiAttackObject(victim, CMD_FROM_AI, kMaxShots);
				m_updateAttacked = true;
			}
		}
		break;
	case READY_TO_ATTACK:
	{
		// RW 0x8927CB .. 0x8927F3: a target out of range, or no target and a path (AI + 0x140) or one asked for, packs; so does the undeploy request
		bool pack = m_undeployRequest;
		if (!guardStill)
		{
			if (!inRange && haveTarget)
			{
				pack = true;
			}
			else if (!haveTarget && (waiting || mover().path()))
			{
				pack = true;
			}
		}
		if (pack)
		{
			// RW 0x892800 .. 0x89288E
			if (turretForCurWeapon() && m_turretsMustCenter)
			{
				setMyState(ALIGNING_TURRETS);
				break;
			}
			setMyState(UNDEPLOY);
			if (!m_ran || m_classAtCommand != 0)
			{
				m_pending = true;
			}
			if (m_undeployRequest)
			{
				m_undeployRequest = false;
				if (m_mustDeployToAttack && m_ran && (m_classAtCommand == 2 || m_classAtCommand == 3))
				{
					m_kept.type = -1;
					m_pending = false;
					resetRequests();
				}
			}
		}
		else if (!victim && m_updateAttacked && hasPendingCommand())
		{
			replayPending(); // RW 0x892893 .. 0x8928BC
		}
		break;
	}
	case UNDEPLOY:
		// RW 0x8927AF .. 0x8927C9
		if (m_frame != 0 && now >= m_frame)
		{
			setMyState(READY_TO_MOVE);
		}
		break;
	case ALIGNING_TURRETS:
		// RW 0x89277E .. 0x8927AA
		if (TurretAI *t = turretForCurWeapon())
		{
			if (t->isTurretInNaturalPosition()) // RW 0x66237D
			{
				setMyState(UNDEPLOY);
			}
		}
		break;
	default:
		break;
	}
	// RW 0x892998 .. 0x8929E6: ready to move / deployed sleep forever; the (un)packing sleeps until its frame (1 at the least), aligning 1; those three idle the
	// unit (CMD_FROM_AI) every update
	unsigned sleep = (unsigned)UPDATE_SLEEP_FOREVER;
	if (m_state == DEPLOY || m_state == UNDEPLOY)
	{
		sleep = m_frame > now ? m_frame - now : 1u;
		idleFromAI();
	}
	else if (m_state == ALIGNING_TURRETS)
	{
		sleep = 1u;
		idleFromAI();
	}
	const unsigned base = (unsigned)AIUpdateInterface::update(); // RW 0x66E58F
	return (UpdateSleepTime)((int)sleep < (int)base ? sleep : base); // RW 0x8929C8: cmovl
}

void DeployStyleAIUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	AIUpdateInterface::xfer(x);
}

void DeployStyleAIUpdate::xferAIStateBody(Xfer &x)
{
	AIUpdateInterface::xferAIStateBody(x);
	SnapshotXfer::version(x, 1, 1);
	// the kept command (its target by id: m_keptTarget; RoutedCommand::target is set again where the command is given, aiDoCommand)
	x.xferInt(&m_kept.type);
	SnapshotXfer::checkRange(m_kept.type, -1, 0x7FFFFFFF, "a deploy AI's kept command");
	SnapshotXfer::coord3D(x, m_kept.position);
	SnapshotXfer::enumIn(x, m_kept.source, CMD_FROM_PLAYER, CMD_FROM_AI, "a deploy AI's kept command source");
	x.xferInt(&m_kept.waypointId);
	x.xferReal(&m_kept.angle);
	x.xferInt(&m_kept.maxShots);
	m_kept.target = nullptr;
	x.xferObjectID(&m_keptTarget);
	x.xferBool(&m_ran);
	x.xferBool(&m_pending);
	x.xferInt(&m_classAtCommand);
	SnapshotXfer::checkRange(m_classAtCommand, 0, 3, "a deploy AI's class at its command"); // update(): 0 idle, 1 moving, 2 in range, 3 out of range
	x.xferInt(&m_class);
	SnapshotXfer::checkRange(m_class, 0, 3, "a deploy AI's class");
	x.xferInt(&m_state);
	SnapshotXfer::checkRange(m_state, READY_TO_MOVE, ALIGNING_TURRETS, "a deploy AI's state");
	x.xferUnsignedInt(&m_frame);
	x.xferObjectID(&m_victim);
	x.xferObjectID(&m_attackObject);
	SnapshotXfer::coord3D(x, m_attackPos);
	x.xferBool(&m_attackCommand);
	x.xferBool(&m_attackObjectCommand);
	x.xferBool(&m_attackPositionCommand);
	x.xferBool(&m_attackMoveCommand);
	x.xferBool(&m_updateAttacked);
	x.xferBool(&m_deployRequest);
	x.xferBool(&m_undeployRequest);
	x.xferBool(&m_replaying);
	x.xferUnsignedInt(&m_replays);
}

void DeployStyleAIUpdate::crc(StateHasher &h) const
{
	AIUpdateInterface::crc(h);
	h.addI32(m_state);
	h.addU32(m_frame);
	h.addI32(m_kept.type);
	// the rest of the kept command, which dispatch() gives again (review #64: two queued orders differing only there hashed the same)
	h.addFloat(m_kept.position.x);
	h.addFloat(m_kept.position.y);
	h.addFloat(m_kept.position.z);
	h.addI32((std::int32_t)m_kept.source);
	h.addI32(m_kept.waypointId);
	h.addFloat(m_kept.angle);
	h.addI32(m_kept.maxShots);
	h.addU32(m_keptTarget);
	h.addBool(m_ran);
	h.addBool(m_pending);
	h.addI32(m_classAtCommand);
	h.addI32(m_class);
	h.addU32(m_victim);
	h.addU32(m_attackObject);
	h.addFloat(m_attackPos.x);
	h.addFloat(m_attackPos.y);
	h.addFloat(m_attackPos.z);
	h.addBool(m_attackCommand);
	h.addBool(m_attackObjectCommand);
	h.addBool(m_attackPositionCommand);
	h.addBool(m_attackMoveCommand);
	h.addBool(m_updateAttacked);
	h.addBool(m_deployRequest);
	h.addBool(m_undeployRequest);
	h.addU32(m_replays);
}

void DeployStyleAIUpdate::registerClass(ModuleFactory &modules)
{
	// after AIUpdateInterface::registerClasses: this class replaces the base AI's binding (as DozerAIUpdate does)
	modules.bindModuleProc("DeployStyleAIUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &info) -> std::unique_ptr<Module> {
		if (!thing->asObject() || !thing->asObject()->logic().aiWorld())
		{
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		}
		return std::make_unique<DeployStyleAIUpdate>(thing, data);
	});
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ToggleDeploySpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
#define TD_OFF(m) (int)offsetof(ToggleDeploySpecialAbilityUpdateModuleData, m)
// RW 0xC05AC8: the sounds through RW 0x73B217 (audio event names; the port stores the name as SpecialAbilityUpdate's sounds do, S-859)
const FieldParse kToggleDeploy[] = {
	{ "SoundDeploy", INI::parseAsciiString, nullptr, TD_OFF(m_soundDeploy) },
	{ "SoundUndeploy", INI::parseAsciiString, nullptr, TD_OFF(m_soundUndeploy) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef TD_OFF
} // namespace

void ToggleDeploySpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kToggleDeploy);
}

ToggleDeploySpecialAbilityUpdate::ToggleDeploySpecialAbilityUpdate(Thing *thing, const ToggleDeploySpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_td(data)
{
}

// RW 0x8B1C5F
bool ToggleDeploySpecialAbilityUpdate::allowsSpecialPowerUse() const
{
	const Object &obj = *getObject();
	if (obj.testModelCondition(mcPacking()) || obj.testModelCondition(mcUnpacking()))
	{
		return false;
	}
	return SpecialAbilityUpdate::allowsSpecialPowerUse(); // RW 0x851B6E
}

// RW 0x8B1D10
void ToggleDeploySpecialAbilityUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SpecialAbilityUpdate::xfer(x);
	x.xferUnsignedInt(&m_deploys);
	x.xferUnsignedInt(&m_undeploys);
}

void ToggleDeploySpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect(); // RW 0x853EDF
	if (m_td && m_triggerCount == 1)
	{
		if (getObject()->testModelCondition(mcDeployed()))
		{
			undeploy(); // vslot 0x5C
		}
		else
		{
			deploy(); // vslot 0x60
		}
	}
}

// RW 0x8B1C94
void ToggleDeploySpecialAbilityUpdate::undeploy()
{
	Object &obj = *getObject();
	AudioApi::playSoundForObject(m_td->m_soundUndeploy, obj.getID()); // RW 0x6DA95E / 0x6DA6A7, TheAudio vslot 100
	DeployStyleAIUpdate *ai = DeployStyleAIUpdate::of(obj);
	if (!ai)
	{
		obj.logic().noteStop(kStopDeployStyle); // retail calls RW 0x891D62 on the AI unchecked
		return;
	}
	++m_undeploys;
	ai->requestUndeploy();
}

// RW 0x8B1DB3
void ToggleDeploySpecialAbilityUpdate::deploy()
{
	Object &obj = *getObject();
	AIUpdateInterface *ai = obj.getAIUpdateInterface(); // + 0x260
	if (!allowsSpecialPowerUse() || !ai)
	{
		return;
	}
	const float speed = ai->curLocomotor() ? ai->curLocomotor()->speed() : 0.0f; // RW 0x68B34C: 0 without a locomotor or at a speed <= 0
	if (0.1f < speed || ai->isStateActive()) // RW 0x68BE97 (0.1, RW 0xBD83D4); AI vslot 0x1BC
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A(AI + 0x20, 2)
	}
	AudioApi::playSoundForObject(m_td->m_soundDeploy, obj.getID());
	DeployStyleAIUpdate *deployAi = dynamic_cast<DeployStyleAIUpdate *>(ai);
	if (!deployAi)
	{
		obj.logic().noteStop(kStopDeployStyle); // retail calls RW 0x891D3B on the AI unchecked
		return;
	}
	++m_deploys;
	deployAi->requestDeploy();
}

namespace
{
template <class Runtime, class Data>
void bindAbility(ModuleFactory &modules, const char *name)
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

void DeployStyleModules::registerAll(ModuleFactory &modules)
{
	DeployStyleAIUpdate::registerClass(modules);
	bindAbility<ToggleDeploySpecialAbilityUpdate, ToggleDeploySpecialAbilityUpdateModuleData>(modules, "ToggleDeploySpecialAbilityUpdate");
}
