// OpenBFME unit tests. GPL-3.0.
// Lane IDLE-1 r2 (community FB-0001): the per-frame probe of horde members turning where they stand and of members standing with MOVING (the run clip on the
// spot). Shared by test_exit1_motion.cpp (a computer game) and test_idle1_turns.cpp (the building attack and cavalry melee scenarios).

#pragma once

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingTemplate.h"

#include <map>
#include <sstream>
#include <string>

struct TurnProbe
{
	// a member "turns on the spot" in frame f when it stands in frames f, f + 1 and f + 2 (its position is the same at the ends of f - 1 .. f + 2) and its
	// facing changed in f; MOVING at the end of f is then a run clip drawn on the spot (a walk that starts in f + 1 or f + 2, or an order the next frame
	// replaced, moved it: not counted)
	struct Last
	{
		float x = 0, y = 0, o = 0;
		bool known = false;
		bool pendingTurn = false, pendingMoving = false, pendingAttacking = false, pendingCavalry = false; ///< frame f's turn, judged at f + 2
		int pendingAge = 0;                                                                                 ///< frames the member stood since the turn
		bool movingAfter = false;                                                                           ///< MOVING still set one frame after the turn
		int streak = 0;                                                                                     ///< frames in a row standing with MOVING
		unsigned state = 0;
	};
	std::map<ObjectID, Last> last;
	int turned = 0, turnedMoving = 0;                   ///< member-frames turning on the spot; of those with MOVING at the frame's end
	int turnedAttacking = 0, turnedAttackingMoving = 0; ///< the same for members with ATTACKING (melee, the buildings' attackers)
	int turnedCavalry = 0, turnedCavalryMoving = 0;     ///< the same for CAVALRY members (the riders' combat clips)
	int turnedMovingOutsideAttack = 0;                  ///< turned with MOVING still set the frame after, in a state other than the attack (AI_ATTACK_OBJECT:
	                                                    ///< the aim state's own turn, whose exit clears the goal before the member update clears MOVING)
	int stillMovingStreaks = 0;                         ///< members standing with MOVING for 5 frames in a row (1 s)
	std::ostringstream samples;
	int lines = 0, streakLines = 0;

	void sample(GameLogic &logic)
	{
		static const int moving = AIUpdateInterface::modelConditionBit("MOVING");
		static const int attacking = AIUpdateInterface::modelConditionBit("ATTACKING");
		for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
		{
			const Object *c = x->getContainedBy();
			if (!x->getAIUpdateInterface() || !c || !c->getContain() || !dynamic_cast<const HordeContain *>(c->getContain()) || x->isEffectivelyDead())
			{
				continue;
			}
			Last &l = last[x->getID()];
			const Coord3D p = *x->getPosition();
			const bool still = l.known && l.x == p.x && l.y == p.y;
			const bool mv = x->testModelCondition(moving);
			if (l.pendingTurn && !still)
			{
				l.pendingTurn = false; // it walked off: the MOVING was the walk's
			}
			else if (l.pendingTurn && ++l.pendingAge == 1)
			{
				l.movingAfter = l.pendingMoving && mv;
			}
			else if (l.pendingTurn && l.pendingAge == 2)
			{
				l.pendingTurn = false;
				++turned;
				turnedMoving += l.pendingMoving ? 1 : 0;
				turnedMovingOutsideAttack += l.movingAfter && l.state != (unsigned)AI_ATTACK_OBJECT ? 1 : 0;
				if (l.pendingAttacking)
				{
					++turnedAttacking;
					turnedAttackingMoving += l.pendingMoving ? 1 : 0;
				}
				if (l.pendingCavalry)
				{
					++turnedCavalry;
					turnedCavalryMoving += l.pendingMoving ? 1 : 0;
				}
				if (l.movingAfter && l.state != (unsigned)AI_ATTACK_OBJECT && lines < 6)
				{
					++lines;
					samples << "  f" << logic.getFrame() - 2 << " " << x->getTemplate()->getName() << " #" << x->getID() << " turned on the spot, MOVING still set the next frame, state " << l.state << "\n";
				}
			}
			if (!l.pendingTurn && still && l.o != x->getOrientation())
			{
				l.pendingTurn = true;
				l.pendingAge = 0;
				l.pendingMoving = mv;
				l.pendingAttacking = x->testModelCondition(attacking);
				l.pendingCavalry = x->isKindOfName("CAVALRY");
				l.state = x->getAIUpdateInterface()->currentStateId();
			}
			l.streak = still && mv ? l.streak + 1 : 0;
			if (l.streak == 5)
			{
				++stillMovingStreaks;
				if (streakLines < 10)
				{
					++streakLines;
					samples << "  f" << logic.getFrame() << " " << x->getTemplate()->getName() << " #" << x->getID() << " stands with MOVING for 5 frames, state " << x->getAIUpdateInterface()->currentStateId() << "\n";
				}
			}
			l.x = p.x;
			l.y = p.y;
			l.o = x->getOrientation();
			l.known = true;
		}
	}
};

