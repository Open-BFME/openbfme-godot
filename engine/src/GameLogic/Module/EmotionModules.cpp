// OpenBFME. GPL-3.0.
// See GameLogic/Module/EmotionModules.h for the target facts and the stops S-1021 / S-1022.

#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace
{
const char *const kTrackerFile = "EmotionTrackerUpdate.cpp"; // RW 0xC6D2A0 (the path ends with this name)

const char *const kStopTracker =
	"[S-1022] EmotionTrackerUpdate: requests, the fear and hero scans, the QuarrelProbability draw, the nugget choice and run are ported (RW 0x8B5738 / 0x8B54DD); "
	"RadiateFearUpdate pulses run (RW 0x89F9A8); the taunt / point / alert scan (RW 0x8B5946 with the filters RW 0x8B50F0, 0x6612AC, 0x660D71) runs (lane MODULES-3; "
	"INFERENCE: RW 0x694CCC's garrison and disguise branches are taken as visible, RW 0x6EA857 reads the cell's connect layer, which the port's grid never sets to a "
	"wall); not ported: the scripts' forced emotion, the firing arc cone of canSee (RW 0x68FAC9, "
	"reported; no retail template sets FiringArc); inference: the scans' relationship is Object::getRelationship (RW 0x68D7AB's mine and gate cases aside), the "
	"layer is the AI's pathfinder layer (RW 0x68BBE0; ground without an AI), no shroud manager: threat 0 and the template's ShroudClearingRange (reported)";

#define ET_OFF(field) (int)offsetof(EmotionTrackerUpdateModuleData, field)
#define RF_OFF(field) (int)offsetof(RadiateFearUpdateModuleData, field)

// RW 0x8B618C
void parseAddEmotion(INI *ini, void *instance, void *, const void *)
{
	EmotionTrackerUpdateModuleData *d = static_cast<EmotionTrackerUpdateModuleData *>(instance);
	const char *token = ini->getNextTokenOrNull();
	bool isOverride = false;
	if (token && AsciiStringUtil::compareNoCase(token, "override") == 0) // _strcmpi
	{
		isOverride = true;
		token = ini->getNextTokenOrNull();
	}
	if (!token)
	{
		throw INIException(3, "Emotion name or 'OVERRIDE <Emotion name>' expected.");
	}
	const EmotionNuggetTemplate *found = TheEmotionSystem ? TheEmotionSystem->find(token) : nullptr;
	if (!found)
	{
		throw INIException(3, "Emotion not found");
	}
	auto copy = std::make_shared<EmotionNuggetTemplate>();
	copy->copyFrom(*found); // RW 0x8E0A54
	copy->name = token;
	if (isOverride)
	{
		ini->initFromINI(copy.get(), EmotionNuggetTemplate::fieldParse()); // RW 0x8E0D47: up to End
		copy->overridden = true;                                           // + 0x18D
	}
	d->m_emotions.push_back(std::move(copy)); // RW 0x90BE00
}

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0xC6D450
const FieldParse kTrackerParse[] = {
	{ "TauntAndPointDistance", INI::parseReal, nullptr, ET_OFF(m_tauntAndPointDistance) },
	{ "TauntAndPointUpdateDelay", INI::parseDurationUnsignedInt, nullptr, ET_OFF(m_tauntAndPointUpdateDelay) },
	{ "TauntAndPointExcluded", parseFilter, nullptr, ET_OFF(m_tauntAndPointExcluded) },
	{ "AfraidOf", parseFilter, nullptr, ET_OFF(m_afraidOf) },
	{ "AlwaysAfraidOf", parseFilter, nullptr, ET_OFF(m_alwaysAfraidOf) },
	{ "PointAt", parseFilter, nullptr, ET_OFF(m_pointAt) },
	{ "HeroScanDistance", INI::parseReal, nullptr, ET_OFF(m_heroScanDistance) },
	{ "FearScanDistance", INI::parseReal, nullptr, ET_OFF(m_fearScanDistance) },
	{ "QuarrelProbability", INI::parsePercentToReal, nullptr, ET_OFF(m_quarrelProbability) },
	{ "IgnoreVeterancy", INI::parseBool, nullptr, ET_OFF(m_ignoreVeterancy) },
	{ "ImmuneToFearLevel", INI::parseInt, nullptr, ET_OFF(m_immuneToFearLevel) },
	{ "AddEmotion", parseAddEmotion, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC679F0
const FieldParse kRadiateParse[] = {
	{ "InitiallyActive", INI::parseBool, nullptr, RF_OFF(m_initiallyActive) },
	{ "WhichSpecialPower", INI::parseInt, nullptr, RF_OFF(m_whichSpecialPower) },
	{ "GenerateTerror", INI::parseBool, nullptr, RF_OFF(m_generateTerror) },
	{ "GenerateFear", INI::parseBool, nullptr, RF_OFF(m_generateFear) },
	{ "GenerateUncontrollableFear", INI::parseBool, nullptr, RF_OFF(m_generateUncontrollableFear) },
	{ "EmotionPulseRadius", INI::parseReal, nullptr, RF_OFF(m_emotionPulseRadius) },
	{ "EmotionPulseInterval", INI::parseDurationUnsignedInt, nullptr, RF_OFF(m_emotionPulseInterval) },
	{ "VictimFilter", parseFilter, nullptr, RF_OFF(m_victimFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

// RW 0x68BBE0 (inference: the AI's pathfinder layer; ground when the object has none)
int layerOf(Object &obj)
{
	if (AIWorld *w = obj.logic().aiWorld())
	{
		if (ObjectPathfindAdapter *a = w->findAdapter(obj.getID()))
		{
			return (int)a->getLayer();
		}
	}
	return (int)LAYER_GROUND;
}

// RW 0x66352C -> RW 0x6634BF: the squared 2D distance between the two bounding circles' edges (0 when they overlap), the geometry ThePartitionManager reads
float edgeDistanceSquared(Object &a, Object &b)
{
	PartitionManager &pm = a.logic().partition();
	return CombatQueries::edgeDistanceSquared2D(*a.getPosition(), pm.geometryOf(a).circle, *b.getPosition(), pm.geometryOf(b).circle);
}

// RW 0x68E4E2 (the shroud manager's port), else the template's ShroudClearingRange (reported)
float visionRangeOf(Object &obj)
{
	GameLogic &logic = obj.logic();
	if (ShroudManager *sm = logic.shroud())
	{
		return sm->visionRange(obj);
	}
	logic.noteStop("[S-1022] EmotionTrackerUpdate: no shroud manager: the vision range is the template's ShroudClearingRange (RW 0x68E4E2 needs the shroud's ground reference)");
	return ShroudManager::templateVision(*static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride()).shroudClearingRange;
}

// RW 0x68FA3D Object::canSee(other, range): RW 0x8E3C6E's ellipse with both factors 1 (a circle of range + both bounding circles about the object along its
// facing), then the firing arc cone when the template's FiringArc (+ 0x54C, default 360) is below 360 (reported, not ported)
bool canSee(Object &self, Object &other, float range)
{
	PartitionManager &pm = self.logic().partition();
	const float sumR = SimMath::sseAdd(pm.geometryOf(other).circle, pm.geometryOf(self).circle); // RW 0x68FA77 addss
	// RW 0x70B9E0: the cached (cos, sin) of the object's angle (RW 0x42F4E0 / 0x42F4D0 x87: stop S-167, as the pathfinder)
	const float c = SimMath::cosDet(self.getOrientation());
	const float s = SimMath::sinDet(self.getOrientation());
	const Coord3D *o = other.getPosition();
	const Coord3D *p = self.getPosition();
	const float dx = SimMath::pc24Sub(o->x, p->x);
	const float dy = SimMath::pc24Sub(o->y, p->y);
	// RW 0x8E3C6E: f = dir.x * dx + dy * dir.y; side = (dx * dir.y - dir.x * dy) / 1; f < 0: f /= 1; side^2 + f^2 < (range + sumR)^2
	double f = SimMath::pc24AddW(SimMath::pc24MulW((double)c, (double)dx), SimMath::pc24MulW((double)dy, (double)s));
	const double side = SimMath::pc24DivW(SimMath::pc24SubW(SimMath::pc24MulW((double)dx, (double)s), SimMath::pc24MulW((double)c, (double)dy)), 1.0);
	if (f < 0.0)
	{
		f = SimMath::pc24DivW(f, 1.0);
	}
	const double lhs = SimMath::pc24AddW(SimMath::pc24MulW(side, side), SimMath::pc24MulW(f, f));
	const double r = SimMath::pc24AddW((double)range, (double)sumR);
	if (!(lhs < SimMath::pc24MulW(r, r)))
	{
		return false;
	}
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(self.getTemplate())->getFinalOverride();
	if (const FieldValue *v = tt->findField("FiringArc"))
	{
		(void)v;
		self.logic().noteStop("[S-1022] EmotionTrackerUpdate: a template with FiringArc: canSee's firing arc cone (RW 0x68FAC9) is not ported");
	}
	return true;
}

bool filterAllows(GameLogic &logic, const ObjectFilter &f, Object &o)
{
	return ObjectFilterMatch::allows(logic, f, o, nullptr); // RW 0x7640C1(object, 0)
}
} // namespace

// ---- EmotionTrackerUpdate -------------------------------------------------------------------------------------------------------------------------------------

void EmotionTrackerUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kTrackerParse);
}

std::vector<std::string> EmotionTrackerUpdate::stopLines()
{
	return { kStopTracker };
}

EmotionTrackerUpdate::EmotionTrackerUpdate(Thing *thing, const EmotionTrackerUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	Object *obj = getObject();
	// RW 0x8B6332: (object id % TauntAndPointUpdateDelay) + 1, 1 without a delay
	m_scanCountdown = data->m_tauntAndPointUpdateDelay == 0 ? 1 : (int)(obj->getID() % data->m_tauntAndPointUpdateDelay) + 1;
	for (const std::shared_ptr<EmotionNuggetTemplate> &t : data->m_emotions)
	{
		const EmotionNuggetTemplate *use = t.get();
		if (!t->overridden)
		{
			// RW 0x8B63A6: the system's nugget of that name now (none: no nugget)
			use = TheEmotionSystem ? TheEmotionSystem->find(t->name) : nullptr;
			if (!TheEmotionSystem)
			{
				obj->logic().reportError("EmotionTrackerUpdate: TheEmotionSystem is not installed: the nuggets of " + obj->getTemplate()->getName() + " are missing");
			}
		}
		if (use)
		{
			m_nuggets.push_back(std::make_unique<EmotionNugget>(*obj, *use)); // RW 0x835A93
		}
	}
}

void EmotionTrackerUpdate::stopCurrentWithAIState()
{
	// RW 0x8B4FA1: + 0x9C and its template's AIState (+ 0x4C) != -1
	if (m_current && m_current->tmpl().aiState != -1)
	{
		m_current->stop();
		m_current = nullptr;
	}
}

void EmotionTrackerUpdate::force(int type, float seconds, Object *source)
{
	if (!(0.0f < seconds))
	{
		return;
	}
	m_forcedType = type;
	m_forcedFrames = ScriptEngine::secondsToFrames(seconds); // RW 0x8B4ECC: ceil((double)(0.005f * seconds * 1000.0f)), the same as RW 0x60911C
	m_forcedSource = source ? source->getID() : INVALID_ID;
	if (m_current)
	{
		m_current->stop();
		m_current = nullptr;
	}
}

void EmotionTrackerUpdate::forceEmotion(Object &obj, int type, float seconds, Object *source)
{
	// RW 0x68F3CB: the object's tracker (+ 0x254), else its container's, up the chain
	for (Object *o = &obj; o; o = o->getContainedBy())
	{
		if (EmotionTrackerUpdate *t = of(*o))
		{
			t->force(type, seconds, source);
			return;
		}
	}
}

EmotionTrackerUpdate *EmotionTrackerUpdate::of(Object &obj)
{
	// Object + 0x254: the module found by name at construction (RW 0x69A529)
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (EmotionTrackerUpdate *t = dynamic_cast<EmotionTrackerUpdate *>(m.get()))
		{
			return t;
		}
	}
	return nullptr;
}

void EmotionTrackerUpdate::request(int type, Object *source, int delay)
{
	// RW 0x8B4E75
	if (type < 0 || type >= EMOTION_TYPE_COUNT)
	{
		return;
	}
	m_requested[(size_t)type] = true;
	m_requestEnd[(size_t)type] = getObject()->logic().getFrame() + (UnsignedInt)delay;
	m_source[(size_t)type] = source ? source->getID() : INVALID_ID;
}

void EmotionTrackerUpdate::requestEmotion(Object &obj, int type, Object *source, int delay)
{
	// RW 0x68F383: the top of the Object + 0x27C chain
	Object *top = &obj;
	while (top->getContainedBy())
	{
		top = top->getContainedBy();
	}
	if (EmotionTrackerUpdate *t = of(*top))
	{
		t->request(type, source, delay);
	}
}

void EmotionTrackerUpdate::clearEmotionRequest(Object &obj, int type)
{
	// RW 0x68F3A3: the top of the Object + 0x27C chain; RW 0x8B4EA5: + 0x24 + type = 0
	Object *top = &obj;
	while (top->getContainedBy())
	{
		top = top->getContainedBy();
	}
	EmotionTrackerUpdate *t = of(*top);
	if (t && type >= 0 && type < EMOTION_TYPE_COUNT)
	{
		t->m_requested[(size_t)type] = false;
	}
}

void EmotionTrackerUpdate::threats(int &enemyThreat, unsigned &friendThreat) const
{
	// RW 0x8B5A15 .. 0x8B5A78 (and RW 0x8B54FF in the choice): the shroud's channel 1 at the object for the players RW 0x6A8695(index, 4) (enemies) and
	// (index, 3) (itself and its allies)
	enemyThreat = 0;
	friendThreat = 0;
	Object *obj = getObject();
	const Player *p = obj->getControllingPlayer();
	if (!p)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	ShroudManager *sm = logic.shroud();
	if (!sm)
	{
		logic.noteStop("[S-1022] EmotionTrackerUpdate: no shroud manager: the threat (RW 0xB4FF50 channel 1) is 0");
		return;
	}
	std::uint32_t enemies = 0, friends = 1u << (p->getPlayerIndex() & 31);
	PlayerList &pl = logic.players();
	for (int i = 0; i < pl.getPlayerCount(); ++i)
	{
		const Player *q = pl.getNthPlayer(i);
		if (!q || q == p)
		{
			continue;
		}
		// RW 0x6A8695: the relationship of q's default team as p sees it (flag 0)
		const Relationship r = p->getRelationship(q->getDefaultTeam());
		if (r == ENEMIES)
		{
			enemies |= 1u << (q->getPlayerIndex() & 31);
		}
		else if (r == ALLIES)
		{
			friends |= 1u << (q->getPlayerIndex() & 31);
		}
	}
	const Coord3D *pos = obj->getPosition();
	enemyThreat = sm->channelSum(pos->x, pos->y, 1, enemies);
	friendThreat = (unsigned)sm->channelSum(pos->x, pos->y, 1, friends);
}

EmotionNugget *EmotionTrackerUpdate::selectNugget(int enemyThreat, unsigned friendThreat)
{
	// RW 0x8B54DD
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const int level = obj->getExperienceTracker() ? obj->getExperienceTracker()->getRank() : 0; // Object + 0x26C -> + 0x24
	static const int kProjectile = kindBit("PROJECTILE");
	static const int kAirborne = statusBit("AIRBORNE_TARGET");
	for (;;)
	{
		for (const std::unique_ptr<EmotionNugget> &np : m_nuggets)
		{
			EmotionNugget *n = np.get();
			const int type = n->type();
			if (type < 0 || type >= EMOTION_TYPE_COUNT)
			{
				continue;
			}
			// the running nugget with a duration stays while it applies
			if (n == m_current && n->tmpl().duration != 0)
			{
				if (n->canApply(enemyThreat, friendThreat, logic.findObjectByID(m_source[(size_t)type])))
				{
					return n;
				}
			}
			const bool wanted = m_forcedFrames == 0 ? m_requested[(size_t)type] : type == m_forcedType;
			if (!wanted)
			{
				continue;
			}
			// fear (4, 5, 6) is out of reach at ImmuneToFearLevel unless IgnoreVeterancy (or forced)
			if (!(m_forcedFrames != 0 || m_data->m_ignoreVeterancy || level < m_data->m_immuneToFearLevel || (type != 4 && type != 5 && type != 6)))
			{
				continue;
			}
			if (type == EMOTION_TERROR)
			{
				// RW 0x8B5627: a terror source that is not a PROJECTILE and not AIRBORNE_TARGET must share the layer
				Object *src = logic.findObjectByID(m_source[EMOTION_TERROR]);
				if (src && !(kProjectile >= 0 && src->isKindOf((unsigned)kProjectile)) && !(kAirborne >= 0 && src->testStatus((unsigned)kAirborne)) && layerOf(*obj) != layerOf(*src))
				{
					continue;
				}
			}
			if (n->canApply(enemyThreat, friendThreat, logic.findObjectByID(m_source[(size_t)type])))
			{
				return n;
			}
		}
		if (m_forcedFrames == 0)
		{
			return nullptr;
		}
		m_forcedFrames = 0;
	}
}

namespace
{
// RW 0x694CCC(player, 0): the object is visible (not invisible, not stealthed unless detected); INFERENCE (S-1022): the contain branch (RW 0x694D2B .., a garrison's
// riders) is taken as visible, and an invisible or stealthed DISGUISER (KindOf 88) as visible (RW 0x68FBD3's disguise branch is not read)
bool visibleForTaunt(const Object &o)
{
	static const int kStealthed = statusBit("STEALTHED"), kDetected = statusBit("DETECTED"), kDisguiser = kindBit("DISGUISER");
	const bool stealthed = kStealthed >= 0 && o.testStatus((unsigned)kStealthed) && !(kDetected >= 0 && o.testStatus((unsigned)kDetected));
	if (!InvisibilityManager::isInvisible(o) && !stealthed)
	{
		return true;
	}
	return kDisguiser >= 0 && o.isKindOf((unsigned)kDisguiser);
}

// RW 0x6EA857(pos, 0): the ground cell's layer field (cell + 0xC bits 4 .. 9) is 0x11 or more (a wall); INFERENCE (S-1022): the port's grid has no wall layer,
// the connect layer is read
bool onWallLayer(GameLogic &logic, const Coord3D &pos)
{
	AIWorld *w = logic.aiWorld();
	if (!w || !w->mapReady())
	{
		return false;
	}
	const PathfindCell *c = w->pathfinder().getCell(LAYER_GROUND, &pos);
	return c && (int)c->getConnectLayer() >= 0x11;
}
} // namespace

void EmotionTrackerUpdate::tauntScan(Object *&taunt, float &closest)
{
	// RW 0x8B58B1 .. 0x8B59D1
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	logic.emotions().countTauntScan();
	// the tracker filter RW 0x8B4FEA / 0x8B50F0: the object, its player, the threats, the remembered target (+ 0x1C), "an enemy was seen" (+ 0x20)
	int enemyThreat = 0;
	unsigned friendThreat = 0;
	if (obj->getControllingPlayer())
	{
		threats(enemyThreat, friendThreat);
	}
	Object *remembered = nullptr;
	bool seen = false;
	static const int kStealthed = statusBit("STEALTHED"), kDetected = statusBit("DETECTED"), kStandGround = statusBit("STAND_GROUND");
	static const int kImmobile = kindBit("IMMOBILE"), kStructure = kindBit("STRUCTURE"), kIgnoredInGui = kindBit("IGNORED_IN_GUI"),
	                 kUnattackable = kindBit("UNATTACKABLE"), kHorde = kindBit("HORDE");
	PartitionFilterFn tracker([&](Object &o) {
		if (obj->testStatus((unsigned)kStealthed) && !obj->testStatus((unsigned)kDetected))
		{
			return false;
		}
		if (remembered && remembered->getContainedBy() && remembered->getContainedBy() == o.getContainedBy())
		{
			return true; // another member of the remembered target's horde
		}
		for (int k : { kStructure, kHorde, kUnattackable, kImmobile, kIgnoredInGui })
		{
			if (k >= 0 && o.isKindOf((unsigned)k))
			{
				return false;
			}
		}
		if (obj->getRelationship(o) != ENEMIES || o.isEffectivelyDead() || !visibleForTaunt(o)) // RW 0x68D7AB, + 0x458 bit 0, RW 0x694CCC
		{
			return false;
		}
		seen = true;
		if (filterAllows(logic, m_data->m_tauntAndPointExcluded, o) || onWallLayer(logic, *o.getPosition()))
		{
			return false;
		}
		bool applies = false;
		for (const std::unique_ptr<EmotionNugget> &n : m_nuggets)
		{
			if ((n->type() == EMOTION_TAUNT || n->type() == EMOTION_POINT) && n->canApply(enemyThreat, friendThreat, &o))
			{
				applies = true;
				break;
			}
		}
		if (!applies)
		{
			return false;
		}
		AIWorld *w = logic.aiWorld();
		if (onWallLayer(logic, *obj->getPosition()) || layerOf(o) != (int)LAYER_GROUND || // RW 0x8B5228: the candidate's layer (RW 0x68BBE0)
		    (w && w->mapReady() && w->pathfinder().isGroundLineClear(*obj->getPosition(), *o.getPosition()))) // RW 0x6EE5E3
		{
			remembered = &o;
			return true;
		}
		return false;
	});
	PartitionFilterFn sees([&](Object &o) { return canSee(*obj, o, m_data->m_tauntAndPointDistance); }); // RW 0xC1D66C -> 0x68FA3D
	const Player *player = obj->getControllingPlayer();
	PartitionFilterFn visible([player](Object &o) {
		// RW 0xC0F19C -> 0x660D71: visible to the player, or a computer player and the object is firing (RW 0x68C89B)
		bool v = visibleForTaunt(o);
		if (!v && player && player->getPlayerType() == PLAYER_COMPUTER && InvisibilityManager::isFiring(o))
		{
			v = true;
		}
		return v;
	});
	Object *found = nullptr;
	if (!(kStandGround >= 0 && obj->testStatus((unsigned)kStandGround)))
	{
		found = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_tauntAndPointDistance, FROM_CENTER_2D, { &tracker, &sees, &visible }); // RW 0xA39090
	}
	if (!found)
	{
		m_requested[EMOTION_TAUNT] = false;
		m_requested[EMOTION_POINT] = false;
		m_requested[EMOTION_ALERT] = seen;
		return;
	}
	taunt = found;
	if (filterAllows(logic, m_data->m_pointAt, *found) || filterAllows(logic, m_data->m_afraidOf, *found) || filterAllows(logic, m_data->m_alwaysAfraidOf, *found))
	{
		m_requested[EMOTION_ALERT] = false;
		m_requested[EMOTION_TAUNT] = false;
		m_requested[EMOTION_POINT] = true;
		closest = edgeDistanceSquared(*obj, *found); // RW 0x66352C
	}
	else
	{
		m_requested[EMOTION_ALERT] = false;
		m_requested[EMOTION_TAUNT] = true;
		m_requested[EMOTION_POINT] = false;
	}
}

UpdateSleepTime EmotionTrackerUpdate::update()
{
	// RW 0x8B5738
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (obj->getContainedBy())
	{
		// RW 0x8B5775: a contained object (a horde member) stops its nugget
		if (m_current)
		{
			m_current->stop();
			m_current = nullptr;
		}
		return UPDATE_SLEEP_NONE;
	}
	if (m_forcedFrames > 0)
	{
		--m_forcedFrames;
	}
	const UnsignedInt now = logic.getFrame();
	Object *taunt = nullptr;                          // [ebp - 0x1C]
	float closest = std::numeric_limits<float>::max(); // [ebp - 0x24] (RW 0xBD1910)
	--m_scanCountdown;
	if (m_scanCountdown < 1 && m_data->m_tauntAndPointUpdateDelay != 0 && m_forcedFrames == 0)
	{
		m_scanCountdown = (int)m_data->m_tauntAndPointUpdateDelay;
		// RW 0x8B58B1 .. 0x8B59D1: the taunt / point / alert scan (lane MODULES-3)
		tauntScan(taunt, closest);
	}
	Object *heroSource = nullptr; // [ebp - 0x3C]
	Object *fearSource = nullptr; // [ebp - 0x40]
	if ((0.0f < m_data->m_heroScanDistance || 0.0f < m_data->m_fearScanDistance) && m_forcedFrames == 0)
	{
		int enemyThreat = 0;
		unsigned friendThreat = 0;
		threats(enemyThreat, friendThreat);
		const float vision = visionRangeOf(*obj);
		const float vision2 = SimMath::sseMul(vision, vision);
		if (vision2 < closest)
		{
			closest = vision2;
		}
		float hero2 = SimMath::sseMul(m_data->m_heroScanDistance, m_data->m_heroScanDistance);
		float fear2 = SimMath::sseMul(m_data->m_fearScanDistance, m_data->m_fearScanDistance);
		bool heroFound = false;
		static const int kScary = kindBit("SCARY"), kHero = kindBit("HERO");
		static const int kAirborne = statusBit("AIRBORNE_TARGET");
		const ThingTemplate *gollum = nullptr;
		bool gollumLooked = false;
		const std::vector<ObjectID> ids = logic.emotions().scaryOrHeroes();
		for (ObjectID id : ids)
		{
			Object *o = logic.findObjectByID(id);
			if (!o || o->isDestroyed())
			{
				continue;
			}
			if (!o->isInWorld())
			{
				continue; // RW 0x8B5AF3: + 0x474 (in the world)
			}
			const Relationship rel = obj->getRelationship(*o); // RW 0x68D7AB
			if (kScary >= 0 && o->isKindOf((unsigned)kScary))
			{
				// the two filters are pure: evaluated when first needed, each at most once (performance; the same decisions as evaluating them up front)
				int alwaysMemo = -1, afraidMemo = -1;
				auto always = [&]() {
					if (alwaysMemo < 0)
					{
						alwaysMemo = filterAllows(logic, m_data->m_alwaysAfraidOf, *o) ? 1 : 0;
					}
					return alwaysMemo == 1;
				};
				auto afraid = [&]() {
					if (afraidMemo < 0)
					{
						afraidMemo = filterAllows(logic, m_data->m_afraidOf, *o) ? 1 : 0;
					}
					return afraidMemo == 1;
				};
				if (!(rel == ALLIES && !always()))
				{
					const float d2 = edgeDistanceSquared(*obj, *o);
					bool feared = false;
					if (fear2 > d2 && (afraid() || always()) && canSee(*obj, *o, m_data->m_fearScanDistance))
					{
						if ((kAirborne >= 0 && o->testStatus((unsigned)kAirborne)) || layerOf(*obj) == layerOf(*o))
						{
							for (const std::unique_ptr<EmotionNugget> &n : m_nuggets)
							{
								if (n->type() == EMOTION_FEAR && n->canApply(enemyThreat, friendThreat, o))
								{
									fearSource = o;
									fear2 = d2;
									feared = true;
									break;
								}
							}
						}
					}
					if (feared)
					{
						continue; // RW 0x8B5BF2 -> 0x8B5D7A
					}
					// RW 0x8B5BF7: the point part
					if (closest > d2 && (afraid() || always() || filterAllows(logic, m_data->m_pointAt, *o)) &&
					    canSee(*obj, *o, vision)) // the vision range of this update (no state changes in the scan)
					{
						for (const std::unique_ptr<EmotionNugget> &n : m_nuggets)
						{
							if (n->type() == EMOTION_POINT && n->canApply(enemyThreat, friendThreat, o))
							{
								taunt = o;
								closest = d2;
								if (!(kAirborne >= 0 && o->testStatus((unsigned)kAirborne)) && layerOf(*o) != layerOf(*obj))
								{
									m_requested[EMOTION_TAUNT] = true;
									m_requested[EMOTION_POINT] = false;
								}
								else
								{
									m_requested[EMOTION_TAUNT] = false;
									m_requested[EMOTION_POINT] = true;
								}
								break;
							}
						}
					}
				}
			}
			// RW 0x8B5CDA: the hero part (an allied HERO other than the object, the template NeutralGollum excepted)
			if (o != obj && kHero >= 0 && o->isKindOf((unsigned)kHero))
			{
				if (!gollumLooked)
				{
					gollumLooked = true;
					gollum = logic.things().findTemplate("NeutralGollum"); // RW 0x6D1305
				}
				const ThingTemplate *ot = static_cast<const ThingTemplate *>(o->getTemplate());
				if (gollum && (ot == gollum || ot->getFinalOverride() == gollum->getFinalOverride())) // RW 0x73D5C2
				{
					continue;
				}
				if (rel == ALLIES)
				{
					const float d2 = edgeDistanceSquared(*obj, *o);
					if (hero2 > d2 && canSee(*obj, *o, m_data->m_heroScanDistance))
					{
						heroSource = o;
						hero2 = d2;
						heroFound = true;
					}
				}
			}
		}
		if (!heroFound)
		{
			m_heroFlag = false;
		}
		// RW 0x8B5D99: one HERO_CHEER per 5 * 60 frames (RW 0xD9F608) while a hero stays in sight
		if (m_nextHeroCheer < now && heroSource && !m_heroFlag)
		{
			m_nextHeroCheer = 5u * 60u + now;
			m_requested[EMOTION_HERO_CHEER] = true;
			m_heroFlag = true;
		}
		else
		{
			m_requested[EMOTION_HERO_CHEER] = false;
		}
		// RW 0x8B5DD8: the scan's fear replaces a request that has no source or ran out
		if (!m_requested[EMOTION_FEAR] || m_source[EMOTION_FEAR] == INVALID_ID || m_requestEnd[EMOTION_FEAR] < now)
		{
			m_requested[EMOTION_FEAR] = fearSource != nullptr;
		}
	}
	// RW 0x8B5DF3: the quarrel draw, every frame unless SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING (status 0x46)
	static const int kSpecialAbility = statusBit("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING");
	if (!(kSpecialAbility >= 0 && obj->testStatus((unsigned)kSpecialAbility)))
	{
		const float r = logic.random().getValueReal(0.0f, 1.0f, kTrackerFile, 0x25E); // RW 0x6D332C
		if (m_data->m_quarrelProbability > r && layerOf(*obj) == 1)
		{
			request(EMOTION_QUARRELSOME, nullptr, 1);
		}
	}
	int enemyThreat = 0;
	unsigned friendThreat = 0;
	threats(enemyThreat, friendThreat); // RW 0x8B54FF
	EmotionNugget *chosen = selectNugget(enemyThreat, friendThreat);
	bool switchSource = false;
	if (chosen && taunt && (chosen->type() == EMOTION_TAUNT || chosen->type() == EMOTION_POINT))
	{
		// RW 0x8B5E5D: another taunt target (horde members count as their horde) restarts the nugget
		const ObjectID prev = m_source[(size_t)chosen->type()];
		if (prev != taunt->getID())
		{
			Object *a = logic.findObjectByID(prev);
			Object *b = taunt;
			if (a && a->getContainedBy())
			{
				a = a->getContainedBy();
			}
			if (b->getContainedBy())
			{
				b = b->getContainedBy();
			}
			switchSource = a != b;
		}
	}
	if (chosen == m_current && !switchSource)
	{
		if (m_current && !m_current->update())
		{
			m_current->stop();
			m_current = nullptr;
		}
	}
	else
	{
		if (m_current)
		{
			m_current->stop();
		}
		m_current = chosen;
		if (chosen)
		{
			const int t = chosen->type();
			if (t == EMOTION_TAUNT || t == EMOTION_POINT)
			{
				if (taunt)
				{
					m_source[(size_t)t] = taunt->getID();
				}
			}
			else if (t == EMOTION_HERO_CHEER)
			{
				if (heroSource)
				{
					m_source[EMOTION_HERO_CHEER] = heroSource->getID();
				}
			}
			else if (t == EMOTION_FEAR && fearSource)
			{
				m_source[EMOTION_FEAR] = fearSource->getID();
			}
			if (m_forcedFrames > 0)
			{
				chosen->start(logic.findObjectByID(m_forcedSource), m_forcedFrames);
			}
			else
			{
				chosen->start(logic.findObjectByID(m_source[(size_t)t]), 0);
			}
		}
	}
	// RW 0x8B5F97: the requests whose end frame came
	for (int i = 0; i < EMOTION_TYPE_COUNT; ++i)
	{
		if (m_requestEnd[(size_t)i] != 0 && m_requestEnd[(size_t)i] <= now)
		{
			m_requested[(size_t)i] = false;
			m_requestEnd[(size_t)i] = 0;
		}
	}
	return UPDATE_SLEEP_NONE;
}

void EmotionTrackerUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	for (int i = 0; i < EMOTION_TYPE_COUNT; ++i)
	{
		h.addBool(m_requested[(size_t)i]);
		h.addU32(m_requestEnd[(size_t)i]);
		h.addU32(m_source[(size_t)i]);
	}
	h.addU32((std::uint32_t)m_nuggets.size());
	std::uint32_t currentIndex = 0xFFFFFFFFu;
	for (size_t i = 0; i < m_nuggets.size(); ++i)
	{
		m_nuggets[i]->crc(h);
		if (m_nuggets[i].get() == m_current)
		{
			currentIndex = (std::uint32_t)i;
		}
	}
	h.addU32(currentIndex);
	h.addI32(m_scanCountdown);
	h.addI32(m_forcedType);
	h.addI32(m_forcedFrames);
	h.addU32(m_forcedSource);
	h.addU32(m_nextHeroCheer);
	h.addBool(m_heroFlag);
}

// ---- RadiateFearUpdate ----------------------------------------------------------------------------------------------------------------------------------------

void RadiateFearUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	UpgradeModuleData::buildBaseFieldParse(p); // RW 0x89F86E: the mux table (RW 0x8D26E0, + 0x20) first
	p.add(kRadiateParse);
}

RadiateFearUpdate::RadiateFearUpdate(Thing *thing, const RadiateFearUpdateModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x89F8DC
	if (data->m_initiallyActive)
	{
		giveSelfUpgrade(); // RW 0x89F8EC -> 0x855388
	}
}

void RadiateFearUpdate::upgradeImplementation()
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x8554D6
}

UpdateSleepTime RadiateFearUpdate::update()
{
	// RW 0x89F9A8
	Object *obj = getObject();
	if (obj->isEffectivelyDead() || !isAlreadyUpgraded())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	GameLogic &logic = obj->logic();
	const RadiateFearUpdateModuleData *d = m_data;
	if (d->m_generateTerror || d->m_generateFear || d->m_generateUncontrollableFear)
	{
		++m_pulses;
		// RW 0x89FA13: the filters RW 0xC0F374 (the same Object + 0x458 bit 3: never set) and RW 0xC1676C (the owner's ENEMIES, mask 4)
		const Player *owner = obj->getControllingPlayer();
		PartitionFilterFn enemies([&](Object &o) { return owner && owner->getRelationship(o.getTeam()) == ENEMIES; });
		const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj->getPosition(), d->m_emotionPulseRadius, FROM_CENTER_2D, { &enemies }, ITER_FASTEST);
		for (const PartitionHit &hit : hits)
		{
			Object *victim = hit.object;
			if (!ObjectFilterMatch::allows(logic, d->m_victimFilter, *victim, owner)) // RW 0x89FA8D
			{
				continue;
			}
			if (d->m_generateTerror)
			{
				EmotionTrackerUpdate::requestEmotion(*victim, EMOTION_TERROR, obj, (int)d->m_emotionPulseInterval);
				++m_requests;
			}
			if (d->m_generateFear)
			{
				EmotionTrackerUpdate::requestEmotion(*victim, EMOTION_FEAR, obj, (int)d->m_emotionPulseInterval);
				++m_requests;
			}
			if (d->m_generateUncontrollableFear)
			{
				EmotionTrackerUpdate::requestEmotion(*victim, EMOTION_UNCONTROLLABLE_FEAR, obj, (int)d->m_emotionPulseInterval);
				++m_requests;
			}
		}
	}
	return UPDATE_SLEEP((int)(obj->getID() % 5u) + (int)d->m_emotionPulseInterval); // RW 0x89FAE4
}

void RadiateFearUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	crcMux(h);
	h.addU32(m_pulses);
	h.addU64(m_requests);
}

// ---- registration ---------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
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

void EmotionModules::registerAll(ModuleFactory &modules)
{
	bind<EmotionTrackerUpdate, EmotionTrackerUpdateModuleData>(modules, "EmotionTrackerUpdate");
	bind<RadiateFearUpdate, RadiateFearUpdateModuleData>(modules, "RadiateFearUpdate");
}

bool EmotionModules::visibleToScans(const Object &o)
{
	return visibleForTaunt(o);
}

bool EmotionModules::canSeeObject(Object &self, Object &other, float range)
{
	return canSee(self, other, range);
}
