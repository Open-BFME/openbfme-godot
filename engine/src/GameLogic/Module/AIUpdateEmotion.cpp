// OpenBFME. GPL-3.0.
// Lane MODULES-3: the AI side of the emotion system (RotWK only, ported from the binary, caveat S-001): the nugget's AI hooks (RW 0x662FC8 / 0x663053 / 0x66309C),
// aiDoCommand's gate (RW 0x667174), the vision range (RW 0x68E43B) and the safe path host of the run-away-panic state. The states are GameLogic/AI/AIEmotionStates.cpp.

#include "GameLogic/Module/AIUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"

namespace
{
const char *const kStopEmotionAI =
	"[S-1027] emotion AI: the nugget's AIState runs the RotWK temporary states (RW 0x662FC8 / 0x74168E: BACK_AWAY 48, IDLE 42, RUN_AWAY_PANIC 20 locked; FACE_OBJECT 59, "
	"QUARREL 58 until replaced), the lock ends with AILockDuration (RW 0x663053), the stop leaves the state (RW 0x66309C); a temporary state that ends by itself calls the current state's slot 0x1C (RW 0x751D9A: the move states clear their "
	"wait flag, RW 0x740C97); clearing one stops the tracker's AIState nugget (RW 0x751DA9 -> 0x8B4FA1, retail's double stop kept); the command gate RW 0x667174, asked "
	"before any change (the group move, the attack-move's arming and continuation included), refuses dead objects, "
	"PreventPlayerCommands / model condition 206, the script lock (+ 0x3C6, never set) and locked temporary states. INFERENCE / NOT PORTED: the gate's status check RW 0x664D7E "
	"(bits 2 and 0x100, commands 0x36 / 0x53 excepted) is not ported; the vision range's Object + 0x1B0 is the template's VisionRange (its writers are not read); the "
	"safe path radius' Object + 0x1AC is 0 (its writer is not identified); AI + 0x3C4 (the short cower of state 21) is never set; the acos of relAngle (RW 0x4B3D8D) is "
	"SimMath::acosDet; state 48's VoiceDesperateAttack client event is not sent; state 21 is defined (20's success) but no RotWK caller of state 20 as a normal state is known; a move resumed after a panic tests its arrival against the "
	"panic's safe path, which is still the AI's path (RW 0x748E46), and ends there";

HordeContainInterface *hordeOf(Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}
} // namespace

std::vector<std::string> AIUpdateInterface::emotionStops()
{
	std::vector<std::string> out = { kStopEmotionAI };
	for (const std::string &s : HordeContain::emotionStops())
	{
		out.push_back(s);
	}
	return out;
}

void AIUpdateInterface::emotionEnterAIState(int aiState, Object *source)
{
	// RW 0x662FC8
	unsigned id = 0;
	int duration = -1;
	switch (aiState)
	{
		case 0: id = AI_BACK_AWAY; break;
		case 2: id = AI_BUSY; break;
		case 3: id = AI_PANIC; break;
		case 4: id = AI_FACE_OBJECT_IDLE; duration = -2; break;
		case 5: id = AI_QUARREL; duration = -2; break;
		default: return;
	}
	m_world->noteStop(kStopEmotionAI);
	if (!m_machine || m_machine->temporaryStateId() == id)
	{
		return;
	}
	Object *obj = getObject();
	if (HordeContainInterface *hc = hordeOf(*obj))
	{
		hc->setMembersBusy(); // slot 0x13C (RW 0x876D4E)
	}
	m_machine->saveGoalForTemporaryState();                         // RW 0x741675
	m_machine->setGoalObject(source ? source->getID() : INVALID_ID); // machine vslot 0x38
	m_machine->setTemporaryStateRW(id, duration);                    // RW 0x74168E
}

void AIUpdateInterface::emotionLockElapsed()
{
	// RW 0x663053: a locked temporary state is re-entered (its id, -2)
	if (m_machine && m_machine->temporaryStateLocked())
	{
		m_machine->setTemporaryStateRW(m_machine->temporaryStateId(), -2);
	}
}

void AIUpdateInterface::emotionLeaveAIState()
{
	// RW 0x66309C -> 0x751DA9
	if (m_machine)
	{
		m_machine->clearTemporaryState();
	}
}

bool AIUpdateInterface::allowedToRespondToCommand(CommandSourceType source, int command) const
{
	// RW 0x667174
	Object *obj = getObject();
	if (obj->isEffectivelyDead())
	{
		return false;
	}
	if (source == CMD_FROM_PLAYER)
	{
		static const int kDissident = modelConditionBit("EMOTION_DISSIDENT"); // condition 0xCE (RW 0x46E918)
		if (m_preventPlayerCommands || obj->testModelCondition(kDissident))
		{
			return false;
		}
	}
	// + 0x3C6 (the script lock) is never set in the port
	if (m_machine && m_machine->temporaryStateLocked())
	{
		return false;
	}
	if (source == CMD_FROM_PLAYER && m_locomotorSetSpeed == 0.0f && (command == 0 || command == 1))
	{
		return false; // RW 0x6671F6: a player's move order to an object whose locomotor set has no speed (+ 0x1F8)
	}
	return true;
}

float AIUpdateInterface::objectVisionRange() const
{
	// RW 0x68E43B
	Object *obj = getObject();
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj->getTemplate())->getFinalOverride();
	float range = 0.0f; // Object + 0x1B0 (S-1027: the template's VisionRange)
	if (const FieldValue *v = tt->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			range = *f;
		}
	}
	float sum = 0.0f;
	if (obj->attributeModifierSum(16, nullptr, sum)) // RW 0x804F39(0x10)
	{
		range = SimMath::pc24Mul(SimMath::pc24Add(sum, 1.0f), range);
	}
	ShroudManager *sm = obj->logic().shroud();
	if (sm)
	{
		const ShroudManager::TemplateVision tv = ShroudManager::templateVision(*tt);
		if (tv.bonusPerFoot > 0.0f) // template + 0x4CC
		{
			float bonus = SimMath::pc24Mul(SimMath::pc24Sub(obj->getPosition()->z, sm->groundReferenceFor(*obj, tv)), tv.bonusPerFoot);
			// RW 0x68AF8E
			if (bonus > tv.maxBonus)
			{
				bonus = tv.maxBonus;
			}
			if (tv.minBonus > bonus)
			{
				bonus = tv.minBonus;
			}
			range = SimMath::pc24Mul(SimMath::pc24Add(bonus, 1.0f), range);
		}
	}
	return range;
}

bool AIUpdateInterface::repulsorPosition(PathfindObjectID id, Coord3D &out)
{
	Object *o = id != PATHFIND_INVALID_ID ? getObject()->logic().findObjectByID((ObjectID)id) : nullptr;
	if (!o)
	{
		return false;
	}
	out = *o->getPosition();
	return true;
}

float AIUpdateInterface::safePathRadius()
{
	// RW 0x668F87 .. 0x668FAE: fld visionRange; fadd dword AIData + 0x60; fiadd (int)Object + 0x1AC (0, S-1027); fstp dword
	return SimMath::pc24Add(objectVisionRange(), getObject()->logic().settings().repulsedDistance);
}
