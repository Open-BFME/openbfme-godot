// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIBurningDeathState.h for the sources and what is not ported.

#include "GameLogic/AI/AIBurningDeathState.h"

#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const std::uint32_t kAllPlayers = 0xFFFFFu;     // RW 0x751C65 push 0xFFFFF
const char *const kStatesFile = "AIStates.cpp"; // RW 0xC288A8 (the random calls' __FILE__)
const int kWanderAngleLine = 0x4578;             // RW 0x748464
const int kWanderXLine = 0x4584;                 // RW 0x748525
const int kWanderYLine = 0x4585;                 // RW 0x748542
const float kTwoPi = 6.2831854820251465f;        // RW 0xBDD38C
const float kCellsPerUnit = 0.1f;                // RW 0xBD83D4
const float kHalf = 0.5f;                        // RW 0xBD869C
const int kCellSize = 10;                        // RW 0x74853F / 0x748567: imul eax, eax, 0xA
const int kNoLocomotorCells = 3;                 // RW 0x748436 push 3

void burningDeathFinish(Object &obj)
{
	// RW 0x6992AF .. 0x6992E3: the body's last damage record scores the kill (RW 0x6955BC(obj, 1))
	if (ActiveBody *ab = dynamic_cast<ActiveBody *>(obj.getBodyModule()))
	{
		if (Object *killer = obj.logic().findObjectByID(ab->lastDamager()))
		{
			killer->scoreTheKill(obj, 1);
		}
	}
	obj.kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x69931F .. 0x699325: RW 0x698EC3(8, 0)
}
} // namespace

bool AIUpdateInterface::aiBurningDeath(CommandSourceType source)
{
	if (!acceptCommand(source, 0x53))
	{
		return false; // RW 0x66A826: aiDoCommand's gate RW 0x667174
	}
	// RW 0x66AF6B -> AI vslot 0x134, privateBurningDeath RW 0x6648B6
	if (!GarrisonRules::isMobile(*getObject()))
	{
		return false; // RW 0x6648BC: Object::isMobile RW 0x690E97
	}
	m_machine->clear();                    // machine vslot 0x14
	setLastCommandSource(source);          // AI + 0x48
	m_machine->setState(AI_BURNING_DEATH); // machine vslot 0x20 (0x4A)
	return true;
}

// RW 0x748418
Coord3D AIBurningDeathState::nextGoal()
{
	const Locomotor *loco = ai().curLocomotor(); // AI + 0x1F0
	GameLogicRandom &random = owner().logic().random();
	int cells = kNoLocomotorCells;
	if (loco)
	{
		const LocomotorTemplate &t = loco->getTemplate(); // + 4
		const float radius = t.m_burningDeathRadius;       // + 0xFC
		if (t.m_burningDeathIsCavalry)                     // + 0x100
		{
			// RW 0x74845E .. 0x7484F2: (radius, 0, 0) turned by a random angle (CRT sin / cos of the float angle), added to the death position in SSE
			const float angle = random.getValueReal(0.0f, kTwoPi, kStatesFile, kWanderAngleLine);
			const float s = SimMath::sinDet(angle); // RW 0xA3CF90
			const float c = SimMath::cosDet(angle); // RW 0xA3CF84
			Coord3D g;
			g.x = SimMath::addf32(m_deathPosition.x, SimMath::subf32(SimMath::mulf32(c, radius), SimMath::mulf32(s, 0.0f)));
			g.y = SimMath::addf32(m_deathPosition.y, SimMath::addf32(SimMath::mulf32(s, radius), SimMath::mulf32(c, 0.0f)));
			g.z = m_deathPosition.z;
			return g;
		}
		// RW 0x7484FC .. 0x748521: x87 fmul 0.1f, fadd 0.5f, CRT floor, fistp
		cells = SimMath::floorToInt(SimMath::pc24Add(SimMath::pc24Mul(radius, kCellsPerUnit), kHalf));
	}
	const int dx = random.getValue(-cells, cells, kStatesFile, kWanderXLine) * kCellSize;
	const int dy = random.getValue(-cells, cells, kStatesFile, kWanderYLine) * kCellSize;
	Coord3D g;
	g.x = SimMath::addf32(SimMath::sseFromInt32(dx), m_deathPosition.x); // cvtsi2ss; addss
	g.y = SimMath::addf32(SimMath::sseFromInt32(dy), m_deathPosition.y);
	g.z = m_goal.z; // RW: uninitialised stack, never read (the callers keep x / y)
	return g;
}

void AIBurningDeathState::enterMove()
{
	const Coord3D g = nextGoal();
	m_goal.x = g.x; // + 0x20
	m_goal.y = g.y; // + 0x24
	if (!m_move)
	{
		m_move = std::make_unique<AIMoveToState>(ai().mover(), m_goal, true); // the base's + 0x48 = 1 (RW 0x740C8D)
	}
	else
	{
		Coord3D kept = m_move->goal(); // the move adjusted its goal (+ 0x20 .. + 0x28) in place
		kept.x = g.x;
		kept.y = g.y;
		m_move->setGoal(kept);
	}
}

// RW 0x751BE5
StateReturnType AIBurningDeathState::onEnter()
{
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	GameLogic &logic = obj.logic();
	m_deathFrame = logic.getFrame() + a.combatSettings().burningDeathTime; // RW 0x751C0E .. 0x751C1E (RW 0x662DAC: AI data + 0x40)
	m_deathPosition = *obj.getPosition();                                  // + 0x50
	obj.clearDisabled(DISABLED_USER_PARALYZED);                             // RW 0x692443(1)
	static const int kBurningDeath = CombatNames::modelCondition("BURNINGDEATH"); // 0x220: Object + 0x150 bit 0
	if (!obj.testModelCondition(kBurningDeath))
	{
		obj.setModelConditionState(kBurningDeath, true); // RW 0x751C3B .. 0x751C4C
	}
	// RW 0x68D440(RW 0xDE7850, 1): NO_ATTACK, NO_AUTO_ACQUIRE, UNATTACKABLE, UNSELECTABLE (the mask's bits one by one: setStatus has no per-bit side effect here)
	obj.setStatus((unsigned)CombatNames::status("NO_ATTACK"), true);       // 5
	obj.setStatus((unsigned)CombatNames::status("NO_AUTO_ACQUIRE"), true); // 0x17
	obj.setStatus((unsigned)CombatNames::status("UNATTACKABLE"), true);    // 0x3C
	obj.setStatus((unsigned)CombatNames::status("UNSELECTABLE"), true);    // 3
	logic.deselectObject(obj, kAllPlayers, true); // RW 0x625759(obj, 0xFFFFF, 1)
	a.chooseLocomotorSet(LOCOMOTORSET_BURNINGDEATH); // AI vslot 0x238(0x10)
	enterMove();
	return m_move->onEnter(); // RW 0x74DADA
}

// RW 0x751CC5
StateReturnType AIBurningDeathState::update()
{
	Object &obj = owner();
	if (m_deathFrame <= obj.logic().getFrame())
	{
		burningDeathFinish(obj); // RW 0x699282
		return STATE_SUCCESS;
	}
	if (!m_move)
	{
		return STATE_FAILURE; // a state loaded without its move (not reached: xferMove keeps it)
	}
	if (m_move->update() != STATE_CONTINUE) // RW 0x748E46; test eax, eax
	{
		enterMove();
		m_move->onEnter(); // RW 0x74DADA, its answer dropped
	}
	return STATE_CONTINUE;
}

// RW 0x748590
void AIBurningDeathState::onExit(StateExitType)
{
	if (ai().isDestroying())
	{
		return; // the object is being deleted: its modules are going away (as every state's exit in the port)
	}
	Object &obj = owner();
	DamageInfo info;                                   // RW 0x66365E
	info.m_input.m_sourceID = INVALID_ID;              // + 0x08
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;   // + 0x10 = 8
	info.m_input.m_deathType = DEATH_NORMAL;           // + 0x1C = 0
	BodyModuleInterface *body = obj.getBodyModule();
	info.m_input.m_amount = body ? body->getMaxHealth() : 0.0f; // body vslot 0x1C
	info.m_input.m_kill = true;                        // + 0x24
	info.m_input.m_delay = 1.0f;                       // + 0x28 (RW 0xBD1908)
	obj.attemptDamage(info);                           // RW 0x698E7D
	m_move.reset(); // RW keeps the base's fields; the port's move is rebuilt on the next entry
}

void AIBurningDeathState::onTemporaryStateEnded()
{
	if (m_move)
	{
		m_move->clearWaitingForPath(); // RW 0x740C97
	}
}

void AIBurningDeathState::crc(StateHasher &h) const
{
	h.addU32(m_deathFrame);
	h.addFloat(m_deathPosition.x);
	h.addFloat(m_deathPosition.y);
	h.addFloat(m_deathPosition.z);
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

// RW 0x7441AC: version (1, 1), + 0x4C, + 0x50; OpenBFME layout (S-4220): the goal and the move follow
void AIBurningDeathState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_deathFrame);
	AIXfer::coord(x, m_deathPosition);
	AIXfer::coord(x, m_goal);
	xferMove(x, ai().mover(), m_move);
}
