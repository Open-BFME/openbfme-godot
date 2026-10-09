// OpenBFME. GPL-3.0.
//
// The skirmish AI's base builder and build request queue (lane AI-1, step 2). See AIBaseBuilder.h for the binary facts; each function cites the RW function it ports.

#include "GameLogic/SkirmishAI/AIBaseBuilder.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <algorithm>

namespace
{
const float kSecondsPerLogicFrame = 0.2f;     // RW 0xD9F61C
const float kHalfPi = 1.5707963705062866f;    // RW 0xBD89D0
const float kPhaseBoostPerDifficulty = 75.0f; // RW 0xC8EF48
const float kEnemyScanRadius = 300.0f;        // RW 0xC857A0
const std::uint32_t kRebuildDelayFrames = 150; // RW 0x97DDF4: ftol(frame + LOGICFRAMES_PER_SECOND (5) * 30.0f)
const unsigned kCheckLegalOptions = 0x85;     // RW 0x97DD2F
const unsigned kStatusUnderConstruction = 2;  // RW 0x8F0CB4 (TheObjectStatusNames: UNDER_CONSTRUCTION)
const unsigned kStatusIsLeavingFactory = 0x5A; // RW 0x9A1D7E (IS_LEAVING_FACTORY)
const unsigned kStatusPendingConstruction = 0x57; // RW 0x8F0744 (PENDING_CONSTRUCTION)

// RW 0x644FD0: into (-PI, PI] (SSE float compares and steps; constants RW 0xBDD388 / 0xBDD38C / 0xBDD390)
float normalizeAngle(float a)
{
	const float pi = 3.14159274101257324f, twoPi = 6.28318548202514648f;
	if (a > pi)
	{
		do
		{
			a = SimMath::subf32(a, twoPi);
		} while (a > pi);
	}
	if (-pi >= a)
	{
		do
		{
			a = SimMath::addf32(a, twoPi);
		} while (-pi >= a);
	}
	return a;
}

float intToFloat(int v)
{
	return (float)v; // cvtsi2ss / fild of a small integer: exact
}
} // namespace

float AIBuildRequest::bestPriority() const
{
	return effectivePriority > priority ? effectivePriority : priority;
}

Coord3D AIBuildRequest::sitePosition(const GameLogic &logic) const
{
	// RW 0x97DED8: (offset + base) in SSE, z = the ground height there
	Coord3D p;
	p.x = SimMath::addf32(basePosition.x, offset.x);
	p.y = SimMath::addf32(basePosition.y, offset.y);
	p.z = logic.getGroundHeight(p.x, p.y);
	return p;
}

float AIBuildRequest::siteAngle() const
{
	return normalizeAngle(SimMath::addf32(baseAngle, angle)); // RW 0x97DDB8: x87 fadd of two floats, stored as float
}

void AIBaseBuilder::updatePhase(GameLogic &logic, AISkirmishPlayer &ai)
{
	// RW 0x6C7677
	AIBuildState &s = ai.build;
	if (!ai.army)
	{
		return; // RW 0x6A95B3 returns the player's army; the disabled path never gets here
	}
	const std::uint32_t elapsed = logic.getFrame() - ai.creationFrame;               // unsigned difference
	const float seconds = SimMath::mulf32((float)elapsed, kSecondsPerLogicFrame);   // fild (+2^32 when negative as int) * 0.2f at 24 bits: exact below 2^24 frames
	const float rush = ai.army->phaseDurationRush, mid = ai.army->phaseDurationMidGame;
	if (s.phase == 0)
	{
		if (seconds >= rush)
		{
			s.phase = 1;
			s.phaseFraction = 0.0f;
		}
		else if (rush > 0.0f)
		{
			s.phaseFraction = SimMath::divf32(seconds, rush);
		}
	}
	else if (s.phase == 1)
	{
		const float total = SimMath::addf32(mid, rush); // RW 0x6C76D6: MidGame + Rush
		if (seconds >= total)
		{
			s.phase = 2;
			s.phaseFraction = 0.0f;
		}
		else if (total > 0.0f)
		{
			s.phaseFraction = SimMath::divf32(seconds, total);
		}
	}
	else if (s.phase == 2)
	{
		s.phaseFraction = 1.0f; // RW 0xBD1908
	}
}

float AIBaseBuilder::baseAngle(const Coord3D &pos, float extentX, float extentY)
{
	// RW 0x90D3E5 .. 0x90D4F9 (lo = (0, 0) of the active extent)
	float dx = SimMath::subf32(SimMath::mulf32(SimMath::subf32(extentX, 0.0f), 0.5f), pos.x);
	float dy = SimMath::subf32(SimMath::mulf32(SimMath::subf32(extentY, 0.0f), 0.5f), pos.y);
	const float len2 = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
	float t = 0.0f;
	if (len2 != 0.0f)
	{
		const float r = BFME2_Inverse_Sqrt(len2); // RW 0x441C56
		dx = BFME2_Mul24(dx, r);
		dy = BFME2_Mul24(dy, r);
		t = BFME2_Mul24(r, 0.0f);
	}
	float c = SimMath::addf32(SimMath::mulf32(SimMath::addf32(t, dy), 0.0f), dx);
	if (-1.0f > c)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	float a = (float)SimMath::acosDet((double)c); // MSVCR71 acos through RW 0x42F4F0 (S-167: the CRT bits are not proven equal)
	if (SimMath::subf32(SimMath::mulf32(dx, 0.0f), dy) > 0.0f)
	{
		a = SimMath::mulf32(a, -1.0f);
	}
	return normalizeAngle(SimMath::subf32(a, kHalfPi));
}

std::vector<const AIBaseTemplate *> AIBaseBuilder::candidates(const std::vector<const AIBaseTemplate *> &sideBases, const std::string &mapFileName, int startPos, bool anyAllowed)
{
	// RW 0x9BCB10 .. 0x9BCB85
	std::vector<const AIBaseTemplate *> out;
	for (const AIBaseTemplate *t : sideBases)
	{
		const bool match = t->gameMapToUseOn == mapFileName || (anyAllowed && t->gameMapToUseOn == "<ANY>");
		if (!match)
		{
			continue;
		}
		if (!t->playerPositions.empty() && std::find(t->playerPositions.begin(), t->playerPositions.end(), startPos + 1) == t->playerPositions.end())
		{
			continue;
		}
		out.push_back(t);
	}
	return out;
}

void AIBaseBuilder::init(GameLogic &logic, AISkirmishPlayer &ai, const std::string &mapFileName, const std::vector<int> &anyTypeDisabledSlots, std::vector<std::string> &errors)
{
	AIBuildState &s = ai.build;
	if (s.baseBuilderInitialised)
	{
		return;
	}
	s.baseBuilderInitialised = true; // RW 0x90D6DF
	Player *player = logic.players().getNthPlayer(ai.playerIndex);
	const TerrainLogic *terrain = logic.terrain();
	if (!player || !terrain || !ai.store)
	{
		errors.push_back("[S-413] skirmish ai base builder: player " + std::to_string(ai.playerIndex) + " has no terrain or data");
		return;
	}
	// RW 0x90CE76 (skirmish): the slot's start position waypoint
	const int startPos = player->getMultiplayerStartIndex();
	const Waypoint *wp = terrain->findWaypointByName("Player_" + std::to_string(startPos + 1) + "_Start");
	if (!wp)
	{
		errors.push_back("[S-413] skirmish ai base builder: player " + std::to_string(ai.playerIndex) + ": no Player_" + std::to_string(startPos + 1) + "_Start waypoint (no base)");
		return;
	}
	Coord3D pos = wp->location;
	pos.z = logic.getGroundHeight(pos.x, pos.y);
	s.startPosition = pos;
	// RW 0x90D3C7: the angle facing the map centre (the active extent: boundary 0)
	float ex = 0.0f, ey = 0.0f;
	terrain->getExtent(0, ex, ey);
	const float angle = baseAngle(pos, ex, ey);
	AIBaseInstance base;
	base.index = (int)s.bases.size();
	// RW 0x9BD03A -> 0x9BCA8E
	const std::vector<const AIBaseTemplate *> side = ai.store->baseTemplatesOfSide(player->getSide());
	if (side.empty())
	{
		errors.push_back("[S-413] skirmish ai base builder: side '" + player->getSide() + "' has no AIBase");
		s.bases.push_back(base);
		return;
	}
	const bool anyAllowed = std::find(anyTypeDisabledSlots.begin(), anyTypeDisabledSlots.end(), startPos + 1) == anyTypeDisabledSlots.end(); // RW 0x9BC734
	const std::vector<const AIBaseTemplate *> pool = candidates(side, mapFileName, startPos, anyAllowed);
	const AIBaseTemplate *chosen = nullptr;
	if (pool.size() > 1)
	{
		chosen = pool[(size_t)logic.random().getValue(0, (int)pool.size() - 1, "AIBase.cpp", 0xD3)]; // RW 0x9BCBAC
	}
	else if (pool.size() == 1)
	{
		chosen = pool[0];
	}
	if (!chosen)
	{
		errors.push_back("[S-413] skirmish ai base builder: no AIBase of side '" + player->getSide() + "' fits map '" + mapFileName + "'");
		s.bases.push_back(base);
		return;
	}
	base.tmpl = chosen;
	base.templateMap = chosen->map;
	base.position = pos;
	base.angle = chosen->allowsArbitraryRotation ? angle : 0.0f; // RW 0x9BD096
	// RW 0x9BCBED: the rotation, then every entry into its slot
	const float c = SimMath::cosDet(base.angle), sn = SimMath::sinDet(base.angle); // MSVCR71 cos / sin (S-167)
	const float m01 = SimMath::subf32(0.0f, sn), m10 = sn, m00 = c, m11 = c;
	for (const AIBaseStructure &st : chosen->structures)
	{
		while ((int)base.slots.size() < st.slot)
		{
			AIBaseSlot slot;
			slot.index = (int)base.slots.size(); // RW 0x9EFDC2(index, player)
			base.slots.push_back(slot);
		}
		if (st.slot < 1)
		{
			errors.push_back("[S-413] skirmish ai base builder: '" + chosen->map + "' entry " + st.templateName + " has slot " + std::to_string(st.slot) + " (RW indexes slot - 1)");
			continue;
		}
		auto req = std::make_shared<AIBuildRequest>();
		req->serial = s.nextSerial++;
		req->templateName = st.templateName;
		req->name = st.name;
		req->priority = st.priority;
		req->slot = st.slot;
		req->angle = st.angle;
		// RW 0xB26200: x' = ((m02 z) + (m01 y)) + x m00; y' = ((m12 z) + (m10 x)) + m11 y
		req->offset.x = SimMath::addf32(SimMath::addf32(SimMath::mulf32(0.0f, st.z), SimMath::mulf32(m01, st.y)), SimMath::mulf32(st.x, m00));
		req->offset.y = SimMath::addf32(SimMath::addf32(SimMath::mulf32(0.0f, st.z), SimMath::mulf32(m10, st.x)), SimMath::mulf32(m11, st.y));
		req->offset.z = logic.getGroundHeight(req->offset.x, req->offset.y); // RW 0x97D986
		req->basePosition = base.position;
		req->baseAngle = base.angle;
		base.slots[(size_t)(st.slot - 1)].entries.push_back(req);
	}
	s.bases.push_back(base);
}

namespace
{
// RW 0x97DE12 + 0x9618BA + 0x8F0B92: count the start, state 0 unless restarted, queue it (state 0 -> pending, else in progress)
void startRequest(AIBuildState &s, const std::shared_ptr<AIBuildRequest> &req, bool keepState)
{
	++req->starts;
	if (!keepState)
	{
		req->state = 0;
	}
	if (req->state == 0)
	{
		s.pending.push_back(req);
	}
	else
	{
		s.inProgress.push_back(req);
	}
}

// RW 0x97DDC9 reset(state)
void resetRequest(GameLogic &logic, AIBuildRequest &req, int state)
{
	if (state == 3)
	{
		req.counts = false;
		req.rebuildFrame = logic.getFrame() + kRebuildDelayFrames;
	}
	req.state = state;
}

// RW 0x9EFCC4
void updateSlot(GameLogic &logic, AISkirmishPlayer &ai, AIBaseSlot &slot, int difficulty)
{
	AIBuildState &s = ai.build;
	if (slot.index > s.phase)
	{
		return;
	}
	if (!slot.started)
	{
		for (auto &e : slot.entries) // RW 0x9EFC29
		{
			if (e->state == 0)
			{
				startRequest(s, e, false);
			}
		}
		slot.started = true;
	}
	if (slot.index < s.phase && !slot.done && !slot.boosted)
	{
		for (auto &e : slot.entries)
		{
			if (e->state == 0)
			{
				// x87: (float)(difficulty * 75.0f) stored, then max(...) + it
				const float boost = SimMath::mulf32(intToFloat(difficulty), kPhaseBoostPerDifficulty);
				e->priority = SimMath::addf32(e->bestPriority(), boost);
			}
		}
		slot.boosted = true;
	}
	for (auto &e : slot.entries)
	{
		if (e->state == 3 && logic.getFrame() >= e->rebuildFrame)
		{
			resetRequest(logic, *e, 0);
			e->producer = INVALID_ID;
			startRequest(s, e, false);
		}
	}
	if (!slot.done)
	{
		bool all = true;
		for (auto &e : slot.entries)
		{
			if (e->counts && !e->finished())
			{
				all = false;
				break;
			}
		}
		if (all)
		{
			slot.done = true;
		}
	}
}

bool isEnemyOf(const Player &me, const Object &o)
{
	const Player *p = o.getControllingPlayer();
	return p && p != &me && me.getRelationship(p) == ENEMIES;
}

// RW 0x9A1D31: the nearest free dozer to `pos` (squared 2D distance; a strictly nearer one replaces)
ObjectID pickDozer(GameLogic &logic, const AIBuildState &s, const Coord3D &pos)
{
	ObjectID best = INVALID_ID;
	float bestD = 0.0f;
	for (ObjectID id : s.freeDozers)
	{
		const Object *o = logic.findObjectByID(id);
		if (!o)
		{
			continue;
		}
		// RW 0x9A1D65 .. 0x9A1D8F: the dozer interface's isAnyTaskPending (vslot 0x20, RW 0x88BDEE) skips it, and so does IS_LEAVING_FACTORY (status 0x5A) since the
		// processor passes 0 as the second argument (RW 0x8F0FFC)
		const DozerAIUpdate *dz = dynamic_cast<const DozerAIUpdate *>(o->getAIUpdateInterface());
		if ((dz && dz->taskTarget() != INVALID_ID) || o->testStatus(kStatusIsLeavingFactory))
		{
			continue;
		}
		const float dx = SimMath::subf32(pos.x, o->getPosition()->x);
		const float dy = SimMath::subf32(pos.y, o->getPosition()->y);
		const float d = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
		if (bestD > d || best == INVALID_ID)
		{
			best = id;
			bestD = d;
		}
	}
	return best;
}

// the enemy count of RW 0x97DFCD and RW 0x9A1FA4 (the same four partition filters, range 300 around `site`)
int enemiesNear(GameLogic &logic, const Player &player, const Coord3D &site)
{
	int enemies = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		// the four partition filters of RW 0x97DFCD: accept KindOf any of CAN_ATTACK | STRUCTURE | HERO | SUPPORT (RW 0xC309B4 -> 0x661359), reject
		// NEUTRALGOLLUM (RW 0xC110A4), the AI's player ENEMIES with the object (RW 0xC1676C, mask 4; RW asks the object's team, S-414), alive
		// (object + 0x458 bit 0 clear, RW 0xC10E20); range 300 from the centre in 2D (partition vslot 0x34, mode 0)
		if (o->isEffectivelyDead() || !o->isInWorld() || !isEnemyOf(player, *o) || o->isKindOfName("NEUTRALGOLLUM") ||
			!(o->isKindOfName("CAN_ATTACK") || o->isKindOfName("STRUCTURE") || o->isKindOfName("HERO") || o->isKindOfName("SUPPORT")))
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
		}
		const float dx = SimMath::subf32(o->getPosition()->x, site.x), dy = SimMath::subf32(o->getPosition()->y, site.y);
		if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) <= SimMath::mulf32(kEnemyScanRadius, kEnemyScanRadius))
		{
			++enemies;
		}
	}
	return enemies;
}

// RW 0x97DFCD (the check of a structure request)
int checkRequest(GameLogic &logic, const AIBuildRequest &req, Player &player)
{
	const Coord3D site = req.sitePosition(logic);
	if (enemiesNear(logic, player, site) > 2)
	{
		return 9;
	}
	// RW 0x97DC99
	Object *dozer = logic.findObjectByID(req.producer);
	const DozerAIUpdate *d = dozer ? dynamic_cast<const DozerAIUpdate *>(dozer->getAIUpdateInterface()) : nullptr;
	if (!dozer || !d || d->taskTarget() != INVALID_ID) // AIUpdate vslot 0x1B8 (S-414: taken as "has a task", travelling to a site included)
	{
		return 8;
	}
	const ThingTemplate *tt = logic.things().findTemplate(req.templateName);
	if (!tt)
	{
		return 10;
	}
	const CanMakeType can = BuildAssistant::canMakeUnit(*dozer, tt, -1);
	if (can != CANMAKE_OK)
	{
		return (int)can;
	}
	// RW 0x97DAFC (a pathfinder cell scan around the site): not ported, S-414
	if (BuildPlacement::isLocationLegalToBuild(logic, site, *tt->getFinalOverride(), req.siteAngle(), kCheckLegalOptions, dozer, &player) != LBC_OK)
	{
		return 10;
	}
	return 0;
}

// RW 0x8F0A53
void reorder(std::vector<std::shared_ptr<AIBuildRequest>> &q)
{
	std::vector<std::shared_ptr<AIBuildRequest>> out, rest;
	for (auto &r : q)
	{
		(r->priority < 0.0f ? out : rest).push_back(r);
	}
	const size_t total = q.size();
	float threshold = 3.40282347e+38f; // RW 0xBD1910
	while (out.size() != total)
	{
		// RW 0x8F0497: the highest bestPriority strictly below the threshold (first of equals)
		const AIBuildRequest *top = nullptr;
		for (auto &r : rest)
		{
			const float p = r->bestPriority();
			if (threshold > p && (!top || p > top->bestPriority()))
			{
				top = r.get();
			}
		}
		if (!top)
		{
			break;
		}
		threshold = top->bestPriority();
		for (auto &r : rest) // RW 0x8F09BD: every request of that priority, in queue order
		{
			if (r->bestPriority() == threshold)
			{
				out.push_back(r);
			}
		}
	}
	q = out;
}
} // namespace


namespace
{
const float kRushUnitPriority = 500.0f;   // RW 0xBDD97C (a phase 0 request's priority, RW 0x9A0F66 / 0x9A0D4F)
const float kReservedCost = 1000.0f;      // RW 0xC88214 (cost >= it: the request draws on the money reserve, + 0x21; not ported, S-413)

bool sameTemplateName(const Object &o, const std::string &name)
{
	const ThingTemplate *t = o.getTemplate();
	return t && (t->getName() == name || (t->getFinalOverride() && t->getFinalOverride()->getName() == name));
}

// RW 0x9A0705: the first factory of the unit (multimap order) that exists, is alive (object + 0x458 bit 0), has a production interface and, unless
// `ignoreBusy`, is not disabled (slot 25) and has an empty queue (slot 17); one whose id is in `exclude` is skipped
Object *findFactory(GameLogic &logic, const AISkirmishPlayer &ai, const std::string &unit, const std::vector<ObjectID> *exclude, bool ignoreBusy)
{
	const AIUnitBuilderState &u = ai.build.units;
	if (u.factories.empty())
	{
		return nullptr; // RW 0x9A070D: an empty map
	}
	const NameKeyType key = ai.store->keyOf(unit);
	auto range = u.factories.equal_range(key);
	for (auto it = range.first; it != range.second; ++it)
	{
		Object *o = logic.findObjectByID(it->second);
		if (!o || o->isEffectivelyDead())
		{
			continue;
		}
		ProductionUpdateInterface *pu = o->getProductionUpdate();
		if (!pu)
		{
			continue;
		}
		if (!ignoreBusy && (pu->isFactoryDisabled() || pu->getProductionCount() != 0))
		{
			continue;
		}
		if (exclude && std::find(exclude->begin(), exclude->end(), o->getID()) != exclude->end())
		{
			continue;
		}
		return o;
	}
	return nullptr;
}

// RW 0x9A0838 for every completed structure of the player not seen yet (S-416: RW is told by the structure's completion event)
void registerFactories(GameLogic &logic, AISkirmishPlayer &ai, const Player &player)
{
	AIUnitBuilderState &u = ai.build.units;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &player || o->isEffectivelyDead() || o->isUnderConstruction() || !o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		if (std::find(u.registeredStructures.begin(), u.registeredStructures.end(), o->getID()) != u.registeredStructures.end())
		{
			continue;
		}
		u.registeredStructures.push_back(o->getID());
		for (const ArmyMemberDefinition &m : ai.army->members)
		{
			const ThingTemplate *tt = logic.things().findTemplate(m.unit);
			if (tt && BuildAssistant::isInProducersCommandSet(*o, tt, -1)) // BuildAssistant slot 0x68
			{
				u.factories.insert(std::make_pair(ai.store->keyOf(m.unit), o->getID())); // RW 0x6B7DDE (multimap insert)
			}
		}
		u.recompute = true; // RW 0x9A08F7
	}
}

// RW 0x82FCAB: the member's percent for the current phase, blended by the phase fraction f: f * a + (1 - f) * b (x87, rounded to float on return)
float memberPercent(const ArmyMemberDefinition &m, const AIBuildState &s)
{
	float a, b;
	if (s.phase == 0)
	{
		a = m.percentageOfArmy[0];
		b = m.percentageOfArmy[1];
	}
	else if (s.phase == 1)
	{
		a = m.percentageOfArmy[1];
		b = m.percentageOfArmy[2];
	}
	else
	{
		return m.percentageOfArmy[2];
	}
	const double f = (double)s.phaseFraction;
	return (float)SimMath::addD(SimMath::mulD(f, (double)a), SimMath::mulD(SimMath::subD(1.0, f), (double)b)); // S-416: x87 extended precision as double
}

// RW 0x9A0E2B
void recomputeComposition(GameLogic &logic, AISkirmishPlayer &ai)
{
	AIUnitBuilderState &u = ai.build.units;
	float sum = 0.0f;
	for (const ArmyMemberDefinition &m : ai.army->members)
	{
		const NameKeyType key = ai.store->keyOf(m.unit);
		if (!findFactory(logic, ai, m.unit, nullptr, true))
		{
			u.composition[key] = 0.0f;
			continue;
		}
		const float pct = memberPercent(m, ai.build);
		sum = SimMath::addf32(pct, sum);
		u.composition[key] = pct;
	}
	if (sum > 0.0f)
	{
		const float scale = SimMath::divf32(100.0f, sum);
		for (auto &kv : u.composition)
		{
			kv.second = SimMath::mulf32(kv.second, scale);
		}
	}
}

int ownedCount(GameLogic &logic, const Player &player, const std::string &unit)
{
	// RW 0x8E3FD6: the player info record's count of the template (S-416: counted from the player's living objects)
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &player && !o->isEffectivelyDead() && sameTemplateName(*o, unit))
		{
			++n;
		}
	}
	return n;
}

// RW 0x9A0B26
bool hasFreeFactory(GameLogic &logic, const AISkirmishPlayer &ai, const std::string &unit)
{
	std::vector<ObjectID> claimed;
	for (const auto &r : ai.build.units.requests)
	{
		if (r->state == 0)
		{
			if (Object *f = findFactory(logic, ai, r->templateName, &claimed, false))
			{
				claimed.push_back(f->getID());
			}
		}
	}
	return findFactory(logic, ai, unit, &claimed, false) != nullptr;
}

// RW 0x9A0BC0 + RW 0x9A0F66
std::shared_ptr<AIBuildRequest> chooseUnit(GameLogic &logic, AISkirmishPlayer &ai, const Player &player)
{
	AIUnitBuilderState &u = ai.build.units;
	std::vector<std::string> pool;
	const int limit = player.commandPointLimit();
	for (const ArmyMemberDefinition &m : ai.army->members)
	{
		if (!hasFreeFactory(logic, ai, m.unit))
		{
			continue;
		}
		const ThingTemplate *tt = logic.things().findTemplate(m.unit);
		if (!tt)
		{
			continue;
		}
		int count = ownedCount(logic, player, m.unit);
		for (const auto &r : u.requests)
		{
			if (r->templateName == m.unit)
			{
				++count;
			}
		}
		// (float)(CommandPoints * count) / (float)limit * 100.0f (SSE)
		const float pct = SimMath::mulf32(SimMath::divf32((float)(BuildAssistant::commandPoints(*tt) * count), (float)limit), 100.0f);
		auto it = u.composition.find(ai.store->keyOf(m.unit));
		const float desired = it == u.composition.end() ? 0.0f : it->second; // RW 0x6B4E8F (a key the composition lacks reads an empty node: S-416)
		if (SimMath::subf32(desired, pct) > 0.0f)
		{
			pool.push_back(m.unit);
		}
	}
	if (pool.empty())
	{
		return nullptr;
	}
	const std::string &pick = pool[(size_t)logic.random().getValue(0, (int)pool.size() - 1, "AIUnitBuilder.cpp", 0x19D)];
	const ThingTemplate *tt = logic.things().findTemplate(pick);
	auto req = std::make_shared<AIBuildRequest>();
	req->serial = ai.build.nextSerial++;
	req->unit = true;
	req->templateName = pick;
	req->commandPoints = tt ? BuildAssistant::commandPoints(*tt) : 0;
	req->priority = ai.build.phase == 0 ? kRushUnitPriority : ai.army->defaultUnitPriority; // RW 0x9A0FC4 .. 0x9A0FD9
	(void)kReservedCost;
	return req;
}

// RW 0x9A1196 (AIUnitBuilder update; the living world part RW 0x9A0426 is skipped in a skirmish)
void updateUnitBuilder(GameLogic &logic, AISkirmishPlayer &ai, const Player &player)
{
	AIUnitBuilderState &u = ai.build.units;
	registerFactories(logic, ai, player);
	if (u.recompute)
	{
		recomputeComposition(logic, ai);
		u.recompute = false;
	}
	// RW 0x9A0D4F: the request list
	for (size_t i = 0; i < u.requests.size();)
	{
		AIBuildRequest &r = *u.requests[i];
		if (r.state == 0)
		{
			if (!findFactory(logic, ai, r.templateName, nullptr, true))
			{
				r.state = 3; // reset(3): RW 0x993D2E only stores the state
			}
			else if (r.priority == kRushUnitPriority && ai.build.phase != 0)
			{
				r.priority = ai.army->defaultUnitPriority;
			}
			++i;
		}
		else if (r.state == 1)
		{
			++i;
		}
		else
		{
			u.requests.erase(u.requests.begin() + (long)i); // RW 0x9618E3 removes it from the AIPlayer queues too
		}
	}
	// RW 0x9A1051: one new request per frame (the hero path RW 0x9A03A1 / 0x9A0993 is S-416)
	if (std::shared_ptr<AIBuildRequest> req = chooseUnit(logic, ai, player))
	{
		u.requests.push_back(req);
		startRequest(ai.build, req, false);
		++u.made;
	}
}

// RW 0x9EDBC6 (the check of a unit request)
int checkUnitRequest(GameLogic &logic, const AIBuildRequest &req)
{
	Object *factory = logic.findObjectByID(req.producer);
	if (!factory)
	{
		return 3;
	}
	ProductionUpdateInterface *pu = factory->getProductionUpdate();
	if (!pu || pu->canQueueCreateUnit() != 0)
	{
		return 4;
	}
	const ThingTemplate *tt = logic.things().findTemplate(req.templateName);
	return tt ? (int)BuildAssistant::canMakeUnit(*factory, tt->getFinalOverride(), -1) : 3;
}

// RW 0x9EDB44
bool executeUnitRequest(GameLogic &logic, AIBuildRequest &req)
{
	Object *factory = logic.findObjectByID(req.producer);
	const ThingTemplate *tt = logic.things().findTemplate(req.templateName);
	ProductionUpdateInterface *pu = factory ? factory->getProductionUpdate() : nullptr;
	if (!pu || !tt)
	{
		return false;
	}
	const ProductionID id = pu->requestUniqueUnitID();
	if (!pu->queueCreateUnit(tt->getFinalOverride(), -1, id, -1, false, std::string(), false))
	{
		return false;
	}
	req.productionID = id;
	return true;
}

// ---- the economy builder (AIPlayer + 0xC0) ----------------------------------------------------------------------------------------------------------
const float kFarmPriorityMinFarms = 2000.0f;  // RW 0x8EE7EA (2000: fewer than EconomyBuilderMinFarmsOwned)
const float kFarmPriorityPerPhase = 100.0f;   // RW 0x8EE81B
const float kFarmSecondsAfterRush = 30.0f;    // RW 0xC79564
const float kFarmFortressDistanceSq = 810000.0f; // RW 0xC7958C (900^2)
const float kFarmPriorityCap = 2000.0f;       // RW 0xBDE8C4
const float kFarmPriorityGrowth = 10.0f;      // RW 0xBD83D8

int kindIndex(const char *name)
{
	return KindOfTokens::indexOf(name);
}

// RW 0x99E818 (the player info record's economy structures, + 0xC): not LINKED_TO_FLAG / UNATTACKABLE, STRUCTURE, ECONOMY_STRUCTURE or SUPPLY_GATHERING_CENTER
int economyStructureCount(GameLogic &logic, const Player &player)
{
	static const int linked = kindIndex("LINKED_TO_FLAG"), unattackable = kindIndex("UNATTACKABLE"), structure = kindIndex("STRUCTURE"),
					 economy = kindIndex("ECONOMY_STRUCTURE"), supply = kindIndex("SUPPLY_GATHERING_CENTER");
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &player || o->isEffectivelyDead() || o->isKindOf((unsigned)linked) || o->isKindOf((unsigned)unattackable) ||
			!o->isKindOf((unsigned)structure))
		{
			continue;
		}
		n += (o->isKindOf((unsigned)economy) || o->isKindOf((unsigned)supply)) ? 1 : 0;
	}
	return n;
}

// the unsigned frame difference as RW's x87 converts it (fild of the signed int, + 2^32 when negative)
float framesSince(std::uint32_t now, int then)
{
	return SimMath::fstpDword(SimMath::fildU32(now - (std::uint32_t)then));
}

// RW 0x8EE765: should the AI ask for a farm, and with which priority
bool wantsFarm(GameLogic &logic, const AISkirmishPlayer &ai, const Player &player, float &priority)
{
	const AIEconomyState &e = ai.build.economy;
	if (player.commandPointLimit() >= player.commandPoints().getCap()) // Player + 0x60 limit (RW 0x6A7B9F) against its cap (+ 0x70)
	{
		return false;
	}
	const std::uint32_t now = logic.getFrame();
	const int phase = ai.build.phase;
	// x87: fild LOGICFRAMES_PER_SECOND (5) * the float field, compared with the frames as float
	if (phase < 1 && e.lastFrame != -1 &&
		framesSince(now, e.lastFrame) <= SimMath::fstpDword(SimMath::pc24MulW(5.0, (double)ai.army->economyBuilderMinTimeBetweenFarmsRush)))
	{
		return false;
	}
	priority = kFarmPriorityMinFarms;
	const unsigned owned = (unsigned)(e.built + e.inFlight);
	if (owned < (unsigned)ai.army->economyBuilderMinFarmsOwned)
	{
		return true;
	}
	if (phase > 0 && e.inFlight == 0)
	{
		priority = kFarmPriorityPerPhase;
		if (owned < (unsigned)(phase * 3 + 4) &&
			SimMath::fstpDword(SimMath::pc24MulW(5.0, (double)kFarmSecondsAfterRush)) < framesSince(now, e.lastFrame))
		{
			return true;
		}
	}
	return false;
}

// RW 0x99327C: EconomyMaxFarms and EconomyUpgradeProbability of the difficulty
bool difficultyAllowsFarm(GameLogic &logic, const AISkirmishPlayer &ai, const Player &player)
{
	const int d = player.getSkirmishDifficulty() < 0 ? 0 : player.getSkirmishDifficulty();
	const AIDifficultyTuning &t = ai.store->data().difficultyTuning[d < SkirmishAI::DIFFICULTY_COUNT ? d : SkirmishAI::DIFFICULTY_COUNT - 1];
	if (!((unsigned)(ai.build.economy.inFlight + ai.build.economy.built) < (unsigned)t.economyMaxFarms))
	{
		return false;
	}
	if (1.0f <= SimMath::divf32((float)t.economyUpgrade.numerator, (float)t.economyUpgrade.denominator))
	{
		return true;
	}
	return logic.random().getValue(0, t.economyUpgrade.denominator - 1, "AIDifficulty.cpp", 0x2D) < t.economyUpgrade.numerator;
}

// RW 0x8EEC71: the nearest free farm site away from the enemy's fortress (900) and with no enemy within DefenseTreeNodeRadius
std::shared_ptr<AIBuildRequest> chooseFarmSite(GameLogic &logic, const AISkirmishPlayer &ai, const Player &player)
{
	if (!ai.farmSites)
	{
		return nullptr;
	}
	const Coord3D &base = ai.build.startPosition; // RW 0x90CD45
	std::vector<std::pair<float, std::shared_ptr<AIBuildRequest>>> byDistance; // a std::multimap: equal keys keep insertion order
	for (const auto &site : *ai.farmSites)
	{
		if (site->inUse)
		{
			continue;
		}
		if (site->failures != 0)
		{
			site->failures = 0; // skipped this time only
			continue;
		}
		if (!ai.build.baseBuilderInitialised)
		{
			continue;
		}
		const Coord3D p = site->sitePosition(logic);
		const float d = (float)SimMath::length3d(SimMath::subf32(p.x, base.x), SimMath::subf32(p.y, base.y), SimMath::subf32(p.z, base.z));
		auto at = std::upper_bound(byDistance.begin(), byDistance.end(), d, [](float v, const std::pair<float, std::shared_ptr<AIBuildRequest>> &x) { return v < x.first; });
		byDistance.insert(at, std::make_pair(d, site));
	}
	if (byDistance.empty())
	{
		return nullptr;
	}
	// the current enemy's living DOZER_FACTORY (its structures list)
	static const int dozerFactory = kindIndex("DOZER_FACTORY");
	const Object *fortress = nullptr;
	const int enemy = ai.groupFirst ? ai.groupFirst->brain.currentEnemy : -1;
	if (const Player *them = enemy >= 0 ? logic.players().getNthPlayer(enemy) : nullptr)
	{
		for (ObjectID id : AITacticalAI::listsOf(logic, *them).structures)
		{
			const Object *o = logic.findObjectByID(id);
			if (o && !o->isEffectivelyDead() && o->isKindOf((unsigned)dozerFactory))
			{
				fortress = o;
				break;
			}
		}
	}
	const float radius = ai.store->data().defenseTreeNodeRadius; // manager + 0x984
	for (const auto &c : byDistance)
	{
		const Coord3D p = c.second->sitePosition(logic);
		if (fortress)
		{
			const float dx = SimMath::subf32(p.x, fortress->getPosition()->x), dy = SimMath::subf32(p.y, fortress->getPosition()->y),
						dz = SimMath::subf32(p.z, fortress->getPosition()->z);
			if (!(kFarmFortressDistanceSq < SimMath::sumSquares3(dx, dy, dz)))
			{
				continue;
			}
		}
		bool threatened = false;
		for (Object *o = logic.getFirstObject(); o && !threatened; o = o->getNextObject())
		{
			if (o->isEffectivelyDead() || !o->isInWorld() || !isEnemyOf(player, *o) || o->isKindOfName("NEUTRALGOLLUM") ||
				!(o->isKindOfName("CAN_ATTACK") || o->isKindOfName("STRUCTURE") || o->isKindOfName("HERO") || o->isKindOfName("SUPPORT")))
			{
				continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
			}
			const float dx = SimMath::subf32(o->getPosition()->x, p.x), dy = SimMath::subf32(o->getPosition()->y, p.y);
			threatened = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) <= SimMath::mulf32(radius, radius);
		}
		if (!threatened)
		{
			return c.second;
		}
	}
	return nullptr;
}

// RW 0x8EEF8F
void updateEconomyBuilder(GameLogic &logic, AISkirmishPlayer &ai, const Player &player)
{
	AIEconomyState &e = ai.build.economy;
	for (size_t i = 0; i < e.requests.size();)
	{
		AIBuildRequest &r = *e.requests[i];
		if (!r.finished())
		{
			++i;
			continue;
		}
		e.inFlight -= 1;
		if (r.state == 2)
		{
			e.built += 1;
		}
		else if (r.state == 3)
		{
			e.lastFrame = -1;
			r.failures += 1;
			r.inUse = false;
		}
		e.requests.erase(e.requests.begin() + (long)i);
	}
	float priority = 0.0f;
	if (!wantsFarm(logic, ai, player, priority))
	{
		return;
	}
	if (!difficultyAllowsFarm(logic, ai, player))
	{
		++e.refusedByDifficulty;
		return;
	}
	std::shared_ptr<AIBuildRequest> site = chooseFarmSite(logic, ai, player);
	if (!site)
	{
		++e.noSite;
		return;
	}
	const std::string &tmpl = ai.army->economyTemplate; // RW 0x8EE89E: the ArmyDefinition's AIEconomyAssigment
	if (tmpl.empty())
	{
		return;
	}
	site->priority = priority;
	site->inUse = true; // RW 0x99F3E5(1)
	site->templateName = tmpl;
	site->owner = ai.playerIndex;
	site->producer = INVALID_ID;
	site->built = INVALID_ID;
	startRequest(ai.build, site, false); // vslot 0x18 RW 0x97DE12
	e.inFlight += 1;
	e.requests.push_back(site);
	e.lastFrame = (int)logic.getFrame();
	++e.made;
}

// RW 0x9EDA17 (a unit request's vslot 0x14): once the player owns 2 economy structures, a unit that is not a SHIP or a HERO gets
// max(effective, base) + (MustUseCommandPointPercentage - used / limit) * LowUnitPriorityModifier of the phase (x87 add), at least DefaultUnitPriority, at most
// (1 - used / limit) * 300 + 1800
void unitEffectivePriority(GameLogic &logic, const AISkirmishPlayer &ai, AIBuildRequest &r, const Player &player)
{
	static const int ship = kindIndex("SHIP"), hero = kindIndex("HERO");
	if (economyStructureCount(logic, player) < 2)
	{
		return;
	}
	const ThingTemplate *tt = logic.things().findTemplate(r.templateName);
	if (!tt || MaskTest(logic.templateInfo(tt->getFinalOverride()).kindOf, (unsigned)ship) || MaskTest(logic.templateInfo(tt->getFinalOverride()).kindOf, (unsigned)hero))
	{
		return;
	}
	const float usedOverLimit = SimMath::divf32((float)player.commandPoints().getUsage(), (float)player.commandPointLimit());
	const int phase = ai.build.phase < 0 ? 0 : (ai.build.phase > 2 ? 2 : ai.build.phase);
	const float term = SimMath::mulf32(SimMath::subf32(ai.army->mustUseCommandPointPercentage[phase], usedOverLimit), ai.army->lowUnitPriorityModifier[phase]);
	float eff = SimMath::pc24Add(r.bestPriority(), term); // vslot 8 (RW 0x961BB9) in st0, fadd, fstp
	if (!(eff >= ai.army->defaultUnitPriority))
	{
		eff = ai.army->defaultUnitPriority;
	}
	const float cap = SimMath::addf32(SimMath::mulf32(SimMath::subf32(1.0f, usedOverLimit), 300.0f), 1800.0f); // RW 0xBD1908, 0xBD9E90, 0xC8EBCC
	r.effectivePriority = cap > eff ? eff : cap;
}

// RW 0x99F313: a farm request's priority grows after the Rush phase
void growFarmPriority(GameLogic &logic, const AISkirmishPlayer &ai, AIBuildRequest &r, const Player &player)
{
	if (ai.build.phase <= 0)
	{
		return;
	}
	int n = economyStructureCount(logic, player);
	if (n < 1)
	{
		n = 1;
	}
	const float f = SimMath::fstpDword(SimMath::pc24DivW(SimMath::pc24MulW((double)kSecondsPerLogicFrame, (double)ai.army->economyBuilderPerSecPriorityIncreaseBase), (double)n));
	const float used = (float)player.commandPoints().getUsage(), limit = (float)player.commandPointLimit();
	const float add = SimMath::addf32(SimMath::mulf32(SimMath::mulf32(SimMath::divf32(used, limit), f), kFarmPriorityGrowth), f);
	const float grown = SimMath::addf32(r.priority, add);
	r.priority = kFarmPriorityCap > grown ? grown : kFarmPriorityCap;
}
// ---- the dozer manager (AIPlayer + 0x140; lane AI-2) -------------------------------------------------------------------------------------------------
bool isKind(const Object &o, int kind)
{
	return kind >= 0 && o.isKindOf((unsigned)kind);
}

// RW 0x9A1CE6: a dozer of the manager's player (object + 0x31C == the player's + 0x30C) joins the free list once
void addFreeDozer(AISkirmishPlayer &ai, const Object &dozer, const Player &player)
{
	if (dozer.getControllingPlayer() != &player)
	{
		return;
	}
	std::vector<ObjectID> &free = ai.build.freeDozers;
	if (std::find(free.begin(), free.end(), dozer.getID()) == free.end())
	{
		free.push_back(dozer.getID()); // RW 0x60011F: std::list push_back
	}
}

// RW 0x9A1BC0(dozer, lost): the dozer leaves the free list; `lost` and the dozer's template name equal to the manager's dozer template (+ 8, exact compare
// RW 0x4065AA of the template's name + 0x64) set "a dozer was lost" (+ 0x10)
void removeFreeDozer(AISkirmishPlayer &ai, const Object &dozer, bool lost)
{
	std::vector<ObjectID> &free = ai.build.freeDozers;
	free.erase(std::remove(free.begin(), free.end(), dozer.getID()), free.end());
	if (lost && dozer.getTemplate() && dozer.getTemplate()->getName() == ai.dozerTemplate)
	{
		ai.build.dozerManager.lostDozer = true;
	}
}

// RW 0x9A19E0(factory): queue a dozer of the manager's template at the factory, unless no dozer was lost and the AI is still in the Rush phase (AISkirmishPlayer
// + 0x16C < 1); the factory must hold nothing, or exactly one entry that is an upgrade (RW 0x729661: type 1 or 3 is a unit) or a HERO unit (template + 0x113 & 4)
void queueDozer(GameLogic &logic, AISkirmishPlayer &ai, Object &factory)
{
	AIDozerManagerState &m = ai.build.dozerManager;
	if (!m.lostDozer && ai.build.phase < 1)
	{
		return;
	}
	ProductionUpdateInterface *pu = factory.getProductionUpdate(); // RW 0x68C327(0)
	if (!pu)
	{
		return;
	}
	if (pu->getProductionCount() == 1)
	{
		static const int hero = kindIndex("HERO");
		const ProductionEntry *e = pu->firstProduction(); // vslot 0x54
		const bool unit = e && (e->type == PRODUCTION_UNIT || e->type == PRODUCTION_BUILD_INDEX);
		const bool heroUnit = unit && e->objectToProduce && hero >= 0 &&
			MaskTest(logic.templateInfo(e->objectToProduce->getFinalOverride()).kindOf, (unsigned)hero);
		if (unit && !heroUnit && pu->getProductionCount() != 0)
		{
			return;
		}
	}
	else if (pu->getProductionCount() != 0)
	{
		return;
	}
	const ProductionID id = pu->requestUniqueUnitID(); // vslot 8
	const ThingTemplate *tt = logic.things().findTemplate(ai.dozerTemplate); // RW 0x6D1305 (TheThingFactory)
	// RW 0x9A1A7D .. 0x9A1AA2: calcCostToBuild (RW 0x73C25F), capped by the player info record's money reserve (+ 0xC -> + 0x14), is drawn from that reserve
	// (RW 0x99E79A); the reserve is not ported (S-413), so it holds 0 and nothing is drawn
	if (tt && pu->queueCreateUnit(tt->getFinalOverride(), -1, id, -1, false, std::string(), false)) // vslot 0x20 (unit, -1, id, -1, 0, "", 0)
	{
		++m.queued;
	}
	else
	{
		++m.refused;
	}
}

// RW 0x9A21DB / 0x9A2129 / 0x9A20C4: descending introsort with a 16-entry cutoff.
// The partition swaps equal keys; its order affects which factory can spend the remaining money.
// Do not substitute a platform's std::sort or preserve ties with std::stable_sort.
} // namespace

void AIBaseBuilder::retailSortDozerFactories(std::vector<std::pair<float, Object *>> &a)
{
	using Entry = std::pair<float, Object *>;
	const size_t count = a.size();
	if (count < 2)
	{
		return;
	}
	// RW 0x9A194B / 0x9A18CC: min-heap, choosing the right child when keys tie.
	auto adjustHeap = [&](size_t first, size_t hole, size_t length, Entry value) {
		const size_t top = hole;
		size_t child = 2 * (hole + 1);
		while (child < length)
		{
			if (a[first + child - 1].first < a[first + child].first)
			{
				--child;
			}
			a[first + hole] = a[first + child];
			hole = child;
			child = 2 * (hole + 1);
		}
		if (child == length)
		{
			a[first + hole] = a[first + child - 1];
			hole = child - 1;
		}
		while (hole > top)
		{
			const size_t parent = (hole - 1) / 2;
			if (!(value.first < a[first + parent].first))
			{
				break;
			}
			a[first + hole] = a[first + parent];
			hole = parent;
		}
		a[first + hole] = value;
	};
	// RW 0x9A210E / 0x9A1F4C / 0x9A1B4D / 0x9A1EE2: whole-range partial_sort fallback.
	auto heapSort = [&](size_t first, size_t last) {
		const size_t length = last - first;
		if (length < 2)
		{
			return;
		}
		size_t parent = (length - 2) / 2;
		for (;;)
		{
			adjustHeap(first, parent, length, a[first + parent]);
			if (parent == 0)
			{
				break;
			}
			--parent;
		}
		for (size_t end = length; end > 1; --end)
		{
			const Entry value = a[first + end - 1];
			a[first + end - 1] = a[first];
			adjustHeap(first, 0, end - 1, value);
		}
	};
	int depth = 0;
	for (size_t n = count; n > 1; n >>= 1)
	{
		++depth;
	}
	depth *= 2;
	auto sortLoop = [&](auto &&self, size_t first, size_t last, int remaining) -> void {
		while (last - first > 16)
		{
			if (remaining == 0)
			{
				heapSort(first, last);
				return;
			}
			--remaining;
			const size_t middle = first + (last - first) / 2;
			// RW 0x9A2129: median of first, middle, last-1 with the descending predicate.
			size_t pivotIndex;
			if (a[first].first > a[middle].first)
			{
				pivotIndex = a[middle].first > a[last - 1].first ? middle :
					(a[first].first > a[last - 1].first ? last - 1 : first);
			}
			else
			{
				pivotIndex = a[first].first > a[last - 1].first ? first :
					(a[middle].first > a[last - 1].first ? last - 1 : middle);
			}
			const float pivot = a[pivotIndex].first;
			size_t left = first, right = last;
			// RW 0x9A1ACC: unguarded partition; equal keys are exchanged.
			for (;;)
			{
				while (a[left].first > pivot) ++left;
				--right;
				while (a[right].first < pivot) --right;
				if (left >= right) break;
				std::swap(a[left], a[right]);
				++left;
			}
			self(self, left, last, remaining); // RW recurses right first, iterates left
			last = left;
		}
	};
	sortLoop(sortLoop, 0, count, depth);
	// RW 0x9A20C4 / 0x9A1F1C / 0x9A189B: final insertion pass (equal keys never shift).
	// The binary guards its first 16 entries and uses their maximum as a sentinel for the rest.
	for (size_t i = 1; i < count; ++i)
	{
		const Entry value = a[i];
		size_t j = i;
		while (j > 0 && a[j - 1].first < value.first)
		{
			a[j] = a[j - 1];
			--j;
		}
		a[j] = value;
	}
}

namespace
{
// RW 0x9A23A4: drop the free dozers that are gone, DESTROYED (+ 0x94 bit 0) or dead (+ 0x458 bit 0); then RW 0x9A221E: while there are fewer free dozers than
// DOZER_FACTORY structures, every living factory with at most 2 enemies within 300 (RW 0x9A1FA4: the filters of RW 0x97DFCD) is keyed by the largest squared
// 2D distance from it to a free dozer (0 with none), the list is sorted by that key in descending order (RW 0x9A21DB with the predicate of RW 0x9A1ACC: at most 16
// entries VC7.1's sort is an insertion sort, stable) and the first (factories - free dozers) of them queue a dozer (RW 0x9A19E0)
void updateDozerManager(GameLogic &logic, AISkirmishPlayer &ai, Player &player)
{
	std::vector<ObjectID> &free = ai.build.freeDozers;
	free.erase(std::remove_if(free.begin(), free.end(),
				   [&](ObjectID id) {
					   const Object *o = logic.findObjectByID(id);
					   return !o || o->isDestroyed() || o->isEffectivelyDead();
				   }),
		free.end());
	AIDozerManagerState &m = ai.build.dozerManager;
	if (free.size() >= m.factories.size())
	{
		return;
	}
	std::vector<std::pair<float, Object *>> keyed;
	for (ObjectID fid : m.factories)
	{
		Object *f = logic.findObjectByID(fid);
		if (!f || f->isEffectivelyDead() || enemiesNear(logic, player, *f->getPosition()) > 2)
		{
			continue;
		}
		float key = 0.0f;
		for (ObjectID did : free)
		{
			const Object *d = logic.findObjectByID(did);
			const float dx = SimMath::subf32(d->getPosition()->x, f->getPosition()->x);
			const float dy = SimMath::subf32(d->getPosition()->y, f->getPosition()->y);
			const float d2 = SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx));
			if (key <= d2 && d2 != key)
			{
				key = d2;
			}
		}
		keyed.emplace_back(key, f); // RW 0x97D718
	}
	if (keyed.empty())
	{
		return;
	}
	AIBaseBuilder::retailSortDozerFactories(keyed);
	size_t want = m.factories.size() - free.size();
	for (size_t i = 0; i < keyed.size() && want != 0; ++i, --want)
	{
		queueDozer(logic, ai, *keyed[i].second);
	}
}

// RW 0x90D16A (the base builder, AIPlayer + 4): a structure of the AI's bases that died is built again: every base entry (RW 0x9BC76C collects them by template
// name) whose structure (+ 0x24) is the dead object gets RW 0x9618E3, its priority multiplied by the ArmyDefinition's StructureRebuildPriorityModifier (+ 0x88,
// SSE), the money reserve flag (+ 0x21) and a new start (vslot 0x18 = RW 0x97DE12). A RebuildHoleBehavior object stands for its
// structure (RW 0x886448: its template and id); the port has no rebuild holes (the module is not ported), so the dead object is always the structure itself.
void rebuildDestroyedStructure(GameLogic &logic, AISkirmishPlayer &ai, const Object &dead)
{
	if (!ai.army || !dead.getTemplate())
	{
		return;
	}
	const std::string &name = dead.getTemplate()->getName();
	for (AIBaseInstance &b : ai.build.bases)
	{
		for (AIBaseSlot &slot : b.slots)
		{
			for (auto &e : slot.entries)
			{
				if (e->templateName != name || e->built != dead.getID())
				{
					continue;
				}
				// RW 0x9618E3: the request leaves the AI's queues (RW 0x8F0602: pending, then in progress)
				ai.build.pending.erase(std::remove(ai.build.pending.begin(), ai.build.pending.end(), e), ai.build.pending.end());
				ai.build.inProgress.erase(std::remove(ai.build.inProgress.begin(), ai.build.inProgress.end(), e), ai.build.inProgress.end());
				e->priority = SimMath::mulf32(ai.army->structureRebuildPriorityModifier, e->priority); // RW 0x90D228 .. 0x90D23D (SSE)
				e->reserveMoney = true;
				startRequest(ai.build, e, false);
				++ai.build.rebuilds;
			}
		}
	}
	(void)logic;
}

// RW 0x8F0F7E .. 0x8F1104: the request processor (its early exits jump to RW 0x8F1104, the finished-request sweep)
void processRequests(GameLogic &logic, AISkirmishPlayer &ai, Player &player)
{
	AIBuildState &s = ai.build;
	if (s.pending.empty())
	{
		return;
	}
	// RW 0x8F0F8F ..: the processor; RW 0x8F0F8F .. 0x8F0FA4 counts the dozer manager's DOZER_FACTORY list (AIPlayer + 0x144 = manager + 4; lane AI-2:
	// AI-1 read it as the in-progress list)
	const size_t dozerFactoryCount = s.dozerManager.factories.size();
	reorder(s.pending);
	for (size_t i = 0; i < s.pending.size(); ++i)
	{
		const std::shared_ptr<AIBuildRequest> req = s.pending[i];
		bool assigned = false;
		if (req->unit)
		{
			// vslot 0x2C (needs a producer): the unit builder's free factory (RW 0x9A0705 with busy checks); none: skip it this frame (RW 0x8F106D)
			Object *factory = findFactory(logic, ai, req->templateName, nullptr, false);
			if (!factory)
			{
				req->producer = INVALID_ID;
				continue;
			}
			req->producer = factory->getID();
		}
		else
		{
			// vslot 0x10 (a structure: true) and + 0x28 (needs a dozer: true): the dozer manager's pick
			req->producer = pickDozer(logic, s, req->sitePosition(logic));
		}
		assigned = true;
		const int code = req->unit ? checkUnitRequest(logic, *req) : checkRequest(logic, *req, player);
		req->lastCheck = code;
		if (code == 0 && req->unit)
		{
			if (executeUnitRequest(logic, *req)) // RW 0x96190B -> vslot 0x3C
			{
				req->state = 1; // RW 0x8F10A9 reset(1)
				s.inProgress.push_back(req);
				++s.units.executed;
			}
			else
			{
				req->state = 3; // RW 0x961901 -> the unit request's reset RW 0x993D2E (only the state)
				++s.failed;
			}
			s.pending.erase(s.pending.begin() + (long)i);
			return;
		}
		if (code == 0)
		{
			Object *dozerObj = logic.findObjectByID(req->producer);
			DozerAIUpdate *dozer = dozerObj ? dynamic_cast<DozerAIUpdate *>(dozerObj->getAIUpdateInterface()) : nullptr;
			const ThingTemplate *tt = logic.things().findTemplate(req->templateName);
			Object *built = (dozer && tt) ? dozer->construct(*tt->getFinalOverride(), req->sitePosition(logic), req->siteAngle(), player) : nullptr; // RW 0x97DC72
			if (built)
			{
				req->built = built->getID();
				resetRequest(logic, *req, 1); // RW 0x8F10A9
				s.inProgress.push_back(req);
				s.freeDozers.erase(std::remove(s.freeDozers.begin(), s.freeDozers.end(), req->producer), s.freeDozers.end()); // RW 0x9A1BC0
				++s.executed;
			}
			else
			{
				resetRequest(logic, *req, 3); // RW 0x97DC86
				++s.failed;
			}
			s.pending.erase(s.pending.begin() + (long)i);
			return;
		}
		if (code == 8 && dozerFactoryCount > 0)
		{
			++s.waitedForDozer;
			return;
		}
		if (code == 2)
		{
			++s.waitedForMoney;
			return;
		}
		if (assigned)
		{
			req->producer = INVALID_ID; // RW 0x8F103F
		}
		if (code == 10 || code == 9)
		{
			resetRequest(logic, *req, 3); // RW 0x961901
			++s.failed;
		}
	}
}

} // namespace

void AIBaseBuilder::update(GameLogic &logic, AISkirmishPlayer &ai)
{
	AIBuildState &s = ai.build;
	Player *player = logic.players().getNthPlayer(ai.playerIndex);
	if (!player)
	{
		return;
	}
	// what finishes a unit request in progress (S-416); structure requests finish through the object events
	for (auto &r : s.inProgress)
	{
		if (r->state != 1)
		{
			continue;
		}
		if (r->unit)
		{
			// S-416: a unit request is complete when its factory no longer holds the queue entry (made, or cancelled), failed when the factory is gone
			const Object *f = logic.findObjectByID(r->producer);
			const ProductionUpdateInterface *pu = f && !f->isEffectivelyDead() ? f->getProductionUpdate() : nullptr;
			bool queued = false;
			for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; e; e = pu->nextProduction(e))
			{
				queued = queued || e->productionID == r->productionID;
			}
			if (!pu)
			{
				r->state = 3;
			}
			else if (!queued)
			{
				r->state = 2;
				++s.units.queued;
			}
			continue;
		}
		// a structure request finishes through the object events (lane AI-2): RW 0x8F06FF (built, 2), RW 0x8F0660 (its dozer idle, 2), RW 0x8F0C47 (the
		// structure died under construction, 3) and RW 0x8F0BBC (its dozer died: back to the queue or 3)
	}
	// RW 0x8F0EBE: drop finished requests from the in-progress list
	s.inProgress.erase(std::remove_if(s.inProgress.begin(), s.inProgress.end(), [](const std::shared_ptr<AIBuildRequest> &r) { return r->finished(); }), s.inProgress.end());
	// RW 0x8F0EFD: the base builder (+ 4) through RW 0x90CD00 (skipped when disabled): RW 0x90CDAD -> every base RW 0x9BCA33 -> its slots
	if (!s.baseBuilderDisabled)
	{
		for (AIBaseInstance &b : s.bases)
		{
			for (AIBaseSlot &slot : b.slots)
			{
				updateSlot(logic, ai, slot, player->getSkirmishDifficulty() < 0 ? 0 : player->getSkirmishDifficulty());
			}
		}
	}
	// RW 0x8F0F10: the unit builder (+ 0x38) after the wall builder (+ 0xE4, S-412)
	if (!s.units.disabled && ai.army)
	{
		updateUnitBuilder(logic, ai, *player);
	}
	// RW 0x8F0F23: the team builder (+ 0x90) runs here in RW (AITacticalAI::updateTeamBuilder, called after this function: it draws no random value), then the
	// economy builder (+ 0xC0)
	if (!s.economy.disabled && ai.army && ai.store)
	{
		updateEconomyBuilder(logic, ai, *player);
	}
	// RW 0x8F0F47: every pending request's vslot 0x14 (structures: effective priority = priority + dependents, none here; farms: RW 0x99F313), finished ones dropped
	s.pending.erase(std::remove_if(s.pending.begin(), s.pending.end(), [](const std::shared_ptr<AIBuildRequest> &r) { return r->finished(); }), s.pending.end());
	for (auto &r : s.pending)
	{
		if (r->farm)
		{
			growFarmPriority(logic, ai, *r, *player);
		}
		else if (r->unit)
		{
			unitEffectivePriority(logic, ai, *r, *player);
		}
		else
		{
			r->effectivePriority = SimMath::addf32(0.0f, r->priority);
		}
	}
	processRequests(logic, ai, *player);
	// RW 0x8F1104 .. 0x8F1130: finished requests leave the queue; RW 0x8F1138: the dozer manager
	s.pending.erase(std::remove_if(s.pending.begin(), s.pending.end(), [](const std::shared_ptr<AIBuildRequest> &r) { return r->finished(); }), s.pending.end());
	updateDozerManager(logic, ai, *player);
	// RW 0x8F113F: the money reserve (RW 0x8F0502, every 30 s) is not ported (S-413)
}

// ---- the AIPlayer's object events (lane AI-2) -----------------------------------------------------------------------------------------------------------
void AIBaseBuilder::onObjectEntered(GameLogic &logic, AISkirmishPlayer &ai, Object &obj)
{
	// RW 0x8F069A
	static const int dozerFactory = kindIndex("DOZER_FACTORY"), dozer = kindIndex("DOZER"), selectable = kindIndex("SELECTABLE"), harvester = kindIndex("HARVESTER");
	Player *player = logic.players().getNthPlayer(ai.playerIndex);
	if (isKind(obj, dozerFactory))
	{
		ai.build.dozerManager.factories.push_back(obj.getID()); // RW 0x77F51C: manager + 4 push_back
	}
	// RW 0x8F06BF: an FS_FACTORY goes to the unit builder (RW 0x9A0838); the port registers factories when first seen complete (S-416)
	if (player && isKind(obj, dozer) && isKind(obj, selectable) && !isKind(obj, harvester))
	{
		addFreeDozer(ai, obj, *player); // RW 0x8F06F5
	}
}

void AIBaseBuilder::onObjectLeft(GameLogic &logic, AISkirmishPlayer &ai, Object &obj)
{
	// RW 0x8F0C47
	static const int dozerFactory = kindIndex("DOZER_FACTORY"), structure = kindIndex("STRUCTURE"), dozer = kindIndex("DOZER");
	AIBuildState &s = ai.build;
	Player *player = logic.players().getNthPlayer(ai.playerIndex);
	if (isKind(obj, dozerFactory))
	{
		auto &f = s.dozerManager.factories; // RW 0x9A19B0: the first entry with the id
		auto it = std::find(f.begin(), f.end(), obj.getID());
		if (it != f.end())
		{
			f.erase(it);
		}
	}
	// RW 0x8F0C69: an FS_FACTORY leaves the unit builder (RW 0x9A051D); the port's factory map drops dead factories when it looks them up (S-416)
	const bool dead = obj.isEffectivelyDead();
	if (isKind(obj, structure) && dead)
	{
		// RW 0x8F0C9D .. 0x8F0CBF: a finished structure (construction percent below 0) without RebuildHoleExposeDie (RW 0x889938), or one under construction
		const bool underConstruction = obj.testStatus(kStatusUnderConstruction);
		const bool finished = 0.0f > obj.getConstructionPercent() && !obj.findModule("RebuildHoleExposeDie");
		if (finished || underConstruction)
		{
			if (underConstruction)
			{
				// RW 0x8F0CCE: its builder (+ 0x78), a living DOZER, is free again; the requests in progress that made it (+ 0x24) fail (vslot 0x1C(3))
				const Object *builder = logic.findObjectByID(obj.getBuilderID());
				if (builder && isKind(*builder, dozer) && !builder->isEffectivelyDead() && player)
				{
					addFreeDozer(ai, *builder, *player);
				}
				for (auto &r : s.inProgress)
				{
					if (r->built == obj.getID())
					{
						resetRequest(logic, *r, 3);
					}
				}
			}
			rebuildDestroyedStructure(logic, ai, obj); // RW 0x8F0D2D -> RW 0x90D16A
		}
	}
	if (isKind(obj, dozer) && dead)
	{
		// RW 0x8F0BBC: the dozer leaves the free list (a lost dozer); its unfinished request in progress goes back to the queue (+ 0x20 set: reset(0), producer
		// cleared, RW 0x90BE00 appends it to the pending list) or fails (3)
		removeFreeDozer(ai, obj, true);
		for (auto it = s.inProgress.begin(); it != s.inProgress.end(); ++it)
		{
			const std::shared_ptr<AIBuildRequest> r = *it;
			if (r->producer != obj.getID())
			{
				continue;
			}
			if (r->finished())
			{
				return;
			}
			if (r->requeueOnProducerDeath)
			{
				r->state = 0; // vslot 0x1C(0) (RW 0x97DDC9: only state 3 touches the rebuild fields)
				r->producer = INVALID_ID;
				s.inProgress.erase(it);
				s.pending.push_back(r);
				++s.requeuedForDozerDeath;
				return;
			}
			resetRequest(logic, *r, 3);
			return;
		}
	}
}

void AIBaseBuilder::onDozerIdle(GameLogic &logic, AISkirmishPlayer &ai, Object &dozer)
{
	// RW 0x8F0660: the dozer is free again; the first request in progress it works for (+ 8) is complete (vslot 0x1C(2))
	if (Player *player = logic.players().getNthPlayer(ai.playerIndex))
	{
		addFreeDozer(ai, dozer, *player);
	}
	for (auto &r : ai.build.inProgress)
	{
		if (r->producer == dozer.getID())
		{
			r->state = 2;
			return;
		}
	}
}

void AIBaseBuilder::onStructureBuilt(GameLogic &logic, AISkirmishPlayer &ai, const Object &builder, const Object &structure, bool built)
{
	// RW 0x8F06FF(builder, structure, built): the first request in progress (state 1) of the builder (+ 8) whose template name (+ 0xC) is the structure's is
	// complete; the created-object call (built false, Player::onStructureCreated RW 0x6AA688) skips a HORDE (template + 0x115 & 0x20) and every call skips a
	// PENDING_CONSTRUCTION structure
	static const int horde = kindIndex("HORDE");
	(void)logic;
	if (!built && isKind(structure, horde))
	{
		return;
	}
	for (auto &r : ai.build.inProgress)
	{
		if (r->state != 1 || r->producer != builder.getID())
		{
			continue;
		}
		if (structure.testStatus(kStatusPendingConstruction))
		{
			continue;
		}
		if (structure.getTemplate() && structure.getTemplate()->getName() == r->templateName)
		{
			r->state = 2;
			return;
		}
	}
}

void AIBaseBuilder::crcRequest(const AIBuildRequest &r, StateHasher &h)
{
	h.addU32(r.serial);
	h.addString(r.templateName);
	h.addString(r.name);
	h.addFloat(r.priority);
	h.addFloat(r.effectivePriority);
	h.addU32(r.producer);
	h.addI32(r.state);
	h.addU32(r.built);
	h.addFloat(r.offset.x);
	h.addFloat(r.offset.y);
	h.addFloat(r.offset.z);
	h.addFloat(r.angle);
	h.addFloat(r.basePosition.x);
	h.addFloat(r.basePosition.y);
	h.addFloat(r.basePosition.z);
	h.addFloat(r.baseAngle);
	h.addI32(r.slot);
	h.addBool(r.counts);
	h.addU32(r.rebuildFrame);
	h.addI32(r.starts);
	h.addI32(r.lastCheck);
	h.addBool(r.unit);
	h.addI32(r.commandPoints);
	h.addU32(r.productionID);
	h.addBool(r.farm);
	h.addBool(r.inUse);
	h.addI32(r.owner);
	h.addI32(r.failures);
	h.addI32(r.siteIndex);
	h.addU32((r.requeueOnProducerDeath ? 1u : 0u) | (r.reserveMoney ? 2u : 0u)); // one word: a request of 32 words would make the rotate-add hash blind to swaps
}

void AIBaseBuilder::crc(const AIBuildState &s, StateHasher &h)
{
	h.addU32(0x41494242u); // "AIBB"
	h.addBool(s.baseBuilderDisabled);
	h.addBool(s.baseBuilderInitialised);
	h.addFloat(s.startPosition.x);
	h.addFloat(s.startPosition.y);
	h.addFloat(s.startPosition.z);
	h.addI32(s.phase);
	h.addFloat(s.phaseFraction);
	h.addU32(s.nextSerial);
	// the complete state of a request; the queues below hash the stable identities (serial) in queue order
	auto request = [&h](const AIBuildRequest &r) { AIBaseBuilder::crcRequest(r, h); };
	h.addU32((std::uint32_t)s.bases.size());
	for (const AIBaseInstance &b : s.bases)
	{
		h.addString(b.templateMap);
		h.addI32(b.index);
		h.addFloat(b.position.x);
		h.addFloat(b.position.y);
		h.addFloat(b.position.z);
		h.addFloat(b.angle);
		h.addU32((std::uint32_t)b.slots.size());
		for (const AIBaseSlot &slot : b.slots)
		{
			h.addBool(slot.done);
			h.addBool(slot.started);
			h.addBool(slot.boosted);
			h.addI32(slot.index);
			h.addU32((std::uint32_t)slot.entries.size());
			for (const auto &e : slot.entries)
			{
				request(*e);
			}
		}
	}
	h.addU32((std::uint32_t)s.pending.size());
	for (const auto &r : s.pending)
	{
		h.addU32(r->serial);
	}
	h.addU32((std::uint32_t)s.inProgress.size());
	for (const auto &r : s.inProgress)
	{
		h.addU32(r->serial);
	}
	h.addU32((std::uint32_t)s.freeDozers.size());
	for (ObjectID d : s.freeDozers)
	{
		h.addU32(d);
	}
	h.addU64(s.executed);
	h.addU64(s.failed);
	h.addU64(s.waitedForMoney);
	h.addU64(s.waitedForDozer);
	h.addU32((std::uint32_t)s.dozerManager.factories.size());
	for (ObjectID f : s.dozerManager.factories)
	{
		h.addU32(f);
	}
	h.addBool(s.dozerManager.lostDozer);
	h.addU64(s.dozerManager.queued);
	h.addU64(s.dozerManager.refused);
	h.addU64(s.rebuilds);
	h.addU64(s.requeuedForDozerDeath);
	const AIUnitBuilderState &u = s.units;
	h.addBool(u.disabled);
	h.addBool(u.recompute);
	h.addU32((std::uint32_t)u.factories.size());
	for (const auto &kv : u.factories)
	{
		h.addU32(kv.first);
		h.addU32(kv.second);
	}
	h.addU32((std::uint32_t)u.registeredStructures.size());
	for (ObjectID id : u.registeredStructures)
	{
		h.addU32(id);
	}
	h.addU32((std::uint32_t)u.composition.size());
	for (const auto &kv : u.composition)
	{
		h.addU32(kv.first);
		h.addFloat(kv.second);
	}
	h.addU32((std::uint32_t)u.requests.size());
	for (const auto &r : u.requests)
	{
		request(*r);
	}
	h.addU64(u.made);
	h.addU64(u.queued);
	h.addU64(u.executed);
	const AIEconomyState &e = s.economy;
	h.addBool(e.disabled);
	h.addI32(e.built);
	h.addI32(e.inFlight);
	h.addI32(e.lastFrame);
	h.addU32((std::uint32_t)e.requests.size());
	for (const auto &r : e.requests)
	{
		h.addU32(r->serial);
	}
	h.addU64(e.made);
	h.addU64(e.refusedByDifficulty);
	h.addU64(e.noSite);
}

std::vector<std::shared_ptr<AIBuildRequest>> AIBaseBuilder::makeFarmSites(GameLogic &logic)
{
	std::vector<std::shared_ptr<AIBuildRequest>> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() != "FarmTemplate") // RW 0xDE9EE8 = "FarmTemplate" (RW 0xC79590), compared with the template name
		{
			continue;
		}
		auto r = std::make_shared<AIBuildRequest>();
		r->farm = true;
		r->siteIndex = (int)out.size();
		r->serial = 0x80000000u | (std::uint32_t)out.size();
		r->offset = *o->getPosition();
		r->offset.z = logic.getGroundHeight(r->offset.x, r->offset.y); // RW 0x97D986
		r->angle = o->getOrientation();                                 // + 0x3C = the object's angle (+ 0x44)
		out.push_back(r);
	}
	return out;
}
