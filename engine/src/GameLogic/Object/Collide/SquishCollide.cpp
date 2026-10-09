// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Module/SquishCollide.h for the target facts (RW 0x8BFBAE, 0x68D4D0, 0x695070, 0x68D524, 0x69320D).

#include "GameLogic/Module/SquishCollide.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/SimMath.h"
#include "Common/INI/HostRealText.h"

#include <cstdlib>
#include <variant>

namespace
{
// the attribute modifier types (RW 0xDA6D28 order, AttributeModifiers.cpp)
const int kCrushDecelerateModifier = 9;        // CRUSH_DECELERATE
const int kMinimumCrushVelocityModifier = 0x12; // MINIMUM_CRUSH_VELOCITY
const int kCrusherLevelModifier = 0x17;        // CRUSHER_LEVEL
const int kCrushableLevelModifier = 0x19;      // CRUSHABLE_LEVEL
const int kCrushedDecelerateModifier = 0x1A;   // CRUSHED_DECELERATE

const char *const kStop =
	"[S-580] crush: SquishCollide (RW 0x8BFBAE), the crush levels with CRUSHABLE_LEVEL / CRUSHER_LEVEL (RW 0x68D4D0 / 0x695070), canCrush (RW 0x68D524), onCrush (RW 0x69320D: the "
	"CrushKnockback fling through Object::doKnockback RW 0x692223, the deceleration with CRUSH_DECELERATE / CRUSHED_DECELERATE / MINIMUM_CRUSH_VELOCITY) and the bump (RW 0x696800: "
	"the speed hold) and RamPower's shockwave hit (lane COMBAT-4) are ported; NOT ported: the bump's contact attack (S-1600), RotWK's AI-goal exemption (AI vslot 0x188) and ZH's hijacker / TNT "
	"exemptions, SoundCrushing (client); INFERENCE: the contact pairs are MOVE-1's overlap pass of the AI units in id order (S-220, not RW's partition contacts), the shape test is "
	"planar (circles and oriented rectangles; RW 0xAD2CE0's height test is not read), the crush / revenge weapons fire through the object's ObjectWeapons";

const char *const kCombat3Stop =
	"[S-1600] shockwaves (COMBAT-3 / COMBAT-4): ported (lane COMBAT-4): the shockwave handler RW 0x6968BC (Object.cpp: RESIST_KNOCKBACK, the strength GameLogicRandomValueReal(0.85, "
	"1.15), the taper, the z mult, the clear-radius flight RW 0x792997, the cyclone, SHIP's downward z, the stand-up RW 0x792AFF of a resisting object, the kill of a flung object in AI "
	"state 0x2D), fed by RamPower's 0-damage hit (RW 0x8BFF08 .. 0x8BFF68) and MetaImpactNugget (RW 0x910025 / 0x9108EE / 0x910179 / 0x91062F / 0x910380, shouldDeliver RW "
	"0x910070); NOT ported: the bump's contact attack RW 0x6962DB (counted); the formation AttributeModifiers' other paths (interface slot 0x30 RW 0x86D056 for a joining member, slot "
	"0x74 RW 0x876481, slot 0x1F8 RW 0x8791C5; the payload's and the formation swap's are ported); the rest is stop S-1790; INFERENCE: the knockback's MSVCR71 cos / sin and RW 0x4B3D8D's "
	"acos use the deterministic SimMath pair (S-167)";

const char *const kCrewStop =
	"[S-1601] siege crew (COMBAT-3 / COMBAT-4): a rider or crew member on a bone takes its bone's BoneSpecificConditionState (RW 0x868F0B .. 0x868F4B) and its timed model conditions run "
	"while it is held (SMCHelper: RW 0x8B3313, every disabled type); the container's MOVING / TURN_* / BACKING_UP reach a WORKING_PASSENGER crew on the CLIENT (lane COMBAT-4): the "
	"container draw's DependencySharedModelFlags go to its dependent drawables (W3DModelDraw::replaceModelConditionState RW 0x4BF2D8, Drawable::applyDependencyFlags); the crew objects "
	"keep their own flags; the dependent list's writer is inference (S-1791)";

// ---- template fields (the object table keeps them in the template's field slots, S-072) --------------------------------------------
std::string firstTok(const RawTokens &raw)
{
	if (raw.tokens.empty())
	{
		return std::string();
	}
	if (!raw.macros.empty() && raw.macros[0].isMacro)
	{
		return raw.macros[0].value;
	}
	return raw.tokens[0];
}

bool readReal(const ThingTemplate &t, const char *name, float &out, std::vector<std::string> &errors)
{
	const FieldValue *v = t.findField(name);
	if (!v || std::holds_alternative<std::monostate>(*v))
	{
		return false;
	}
	if (const float *f = std::get_if<float>(v))
	{
		out = *f;
		return true;
	}
	if (const long long *n = std::get_if<long long>(v))
	{
		out = (float)*n;
		return true;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		const std::string tok = firstTok(*raw);
		char *end = nullptr;
		const float f = strtofPortable(tok.c_str(), &end);
		if (!tok.empty() && end != tok.c_str())
		{
			out = f;
			return true;
		}
	}
	errors.push_back(std::string(name) + " has an unreadable value");
	return false;
}

void readInt(const ThingTemplate &t, const char *name, int &out, std::vector<std::string> &errors)
{
	float f = 0.0f;
	if (readReal(t, name, f, errors))
	{
		out = SimMath::truncToInt32(f);
	}
}

void readBool(const ThingTemplate &t, const char *name, bool &out)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return;
	}
	if (const bool *b = std::get_if<bool>(v))
	{
		out = *b;
	}
	else if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		const std::string tok = firstTok(*raw);
		if (!tok.empty())
		{
			out = AsciiStringUtil::compareNoCase(tok, "Yes") == 0 || AsciiStringUtil::compareNoCase(tok, "True") == 0 || tok == "1";
		}
	}
}

void readName(const ThingTemplate &t, const char *name, std::string &out)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return;
	}
	if (const std::string *s = std::get_if<std::string>(v))
	{
		out = *s;
	}
	else if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		out = firstTok(*raw);
	}
	if (AsciiStringUtil::compareNoCase(out, "None") == 0)
	{
		out.clear();
	}
}

const ThingTemplate &finalTemplate(const Object &o)
{
	return *o.getTemplate()->getFinalOverride();
}

// RW 0x70B9E0: the unit direction (cos, sin) of the object's angle
void facing(const Object &o, float &fx, float &fy)
{
	const float a = o.getOrientation();
	fx = SimMath::cosDet(a);
	fy = SimMath::sinDet(a);
}

// ---- the planar shape test (RW 0xAD2CE0, planar part) -----------------------------------------------------------------------------
struct Shape2D
{
	bool box = false;
	float cx = 0.0f, cy = 0.0f;
	float major = 0.0f, minor = 0.0f; // box: half extents along the object's forward / side axes; circle: major = radius
	float ux = 1.0f, uy = 0.0f;       // box: forward axis
};

Shape2D shapeOf(const Object &o, float overrideRadius)
{
	Shape2D s;
	s.cx = o.getPosition()->x;
	s.cy = o.getPosition()->y;
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(finalTemplate(o));
	const ObjectGeometry::Shape def;
	const ObjectGeometry::Shape &g = shapes.empty() ? def : shapes[0];
	s.box = g.type == ObjectGeometry::SHAPE_BOX;
	s.major = g.majorRadius;
	s.minor = g.minorRadius;
	if (overrideRadius >= 0.0f)
	{
		s.major = overrideRadius;
		s.minor = overrideRadius;
	}
	facing(o, s.ux, s.uy);
	return s;
}

float absF(float a)
{
	return a < 0.0f ? SimMath::subf32(0.0f, a) : a;
}

// the distance from a point to a box (0 inside), squared
float pointBoxDistSq(const Shape2D &b, float px, float py)
{
	const float dx = SimMath::subf32(px, b.cx), dy = SimMath::subf32(py, b.cy);
	const float lf = SimMath::addf32(SimMath::mulf32(dx, b.ux), SimMath::mulf32(dy, b.uy));
	const float ls = SimMath::subf32(SimMath::mulf32(dy, b.ux), SimMath::mulf32(dx, b.uy));
	const float ef = SimMath::subf32(absF(lf), b.major);
	const float es = SimMath::subf32(absF(ls), b.minor);
	const float qf = ef > 0.0f ? ef : 0.0f;
	const float qs = es > 0.0f ? es : 0.0f;
	return SimMath::sumSquares2(qf, qs);
}

// separating axis test of two oriented rectangles
bool boxesOverlap(const Shape2D &a, const Shape2D &b)
{
	const float axes[4][2] = { { a.ux, a.uy }, { SimMath::subf32(0.0f, a.uy), a.ux }, { b.ux, b.uy }, { SimMath::subf32(0.0f, b.uy), b.ux } };
	const float dx = SimMath::subf32(b.cx, a.cx), dy = SimMath::subf32(b.cy, a.cy);
	for (const auto &ax : axes)
	{
		auto radius = [&](const Shape2D &s) {
			const float f = absF(SimMath::addf32(SimMath::mulf32(s.ux, ax[0]), SimMath::mulf32(s.uy, ax[1])));
			const float g = absF(SimMath::subf32(SimMath::mulf32(s.ux, ax[1]), SimMath::mulf32(s.uy, ax[0])));
			return SimMath::addf32(SimMath::mulf32(s.major, f), SimMath::mulf32(s.minor, g));
		};
		const float dist = absF(SimMath::addf32(SimMath::mulf32(dx, ax[0]), SimMath::mulf32(dy, ax[1])));
		if (dist > SimMath::addf32(radius(a), radius(b)))
		{
			return false;
		}
	}
	return true;
}

bool overlap(const Shape2D &a, const Shape2D &b)
{
	if (a.box && b.box)
	{
		return boxesOverlap(a, b);
	}
	if (a.box || b.box)
	{
		const Shape2D &box = a.box ? a : b;
		const Shape2D &circle = a.box ? b : a;
		return pointBoxDistSq(box, circle.cx, circle.cy) < SimMath::mulf32(circle.major, circle.major);
	}
	const float r = SimMath::addf32(a.major, b.major);
	return SimMath::sumSquares2(SimMath::subf32(a.cx, b.cx), SimMath::subf32(a.cy, b.cy)) < SimMath::mulf32(r, r);
}

struct Names
{
	int horde = CombatNames::kindOf("HORDE");
	int rampaging = CombatNames::status("RAMPAGING");
	int fleeOffMap = CombatNames::status("FLEE_OFF_MAP");
	int charging = CombatNames::modelCondition("CHARGING");
	int mounted = CombatNames::modelCondition("MOUNTED");
};
const Names &names()
{
	static const Names n;
	return n;
}

bool isHorde(const Object &o)
{
	return o.isKindOf((unsigned)names().horde);
}

// RW 0x694154: the object has the model condition, or its container has
bool hasConditionOrContainerHas(const Object &o, int bit)
{
	if (o.testModelCondition(bit))
	{
		return true;
	}
	const Object *c = o.getContainedBy();
	return c && c->testModelCondition(bit);
}
} // namespace

CrushTemplateInfo CrushTemplateInfo::of(const ThingTemplate &ttIn)
{
	const ThingTemplate &tt = *ttIn.getFinalOverride();
	CrushTemplateInfo i;
	readInt(tt, "CrusherLevel", i.crusherLevel, i.errors);
	readInt(tt, "CrushableLevel", i.crushableLevel, i.errors);
	readInt(tt, "MountedCrusherLevel", i.mountedCrusherLevel, i.errors);
	readInt(tt, "MountedCrushableLevel", i.mountedCrushableLevel, i.errors);
	readBool(tt, "UseCrushAttack", i.useCrushAttack);
	readBool(tt, "CrushOnlyWhileCharging", i.crushOnlyWhileCharging);
	readBool(tt, "CrushAllies", i.crushAllies);
	readReal(tt, "MinCrushVelocityPercent", i.minCrushVelocityPercent, i.errors);
	readReal(tt, "CrushDecelerationPercent", i.crushDecelerationPercent, i.errors);
	readReal(tt, "CrushKnockback", i.crushKnockback, i.errors);
	readReal(tt, "CrushZFactor", i.crushZFactor, i.errors);
	readReal(tt, "RamPower", i.ramPower, i.errors);
	readReal(tt, "RamZMult", i.ramZMult, i.errors);
	readName(tt, "CrushWeapon", i.crushWeapon);
	readName(tt, "CrushRevengeWeapon", i.crushRevengeWeapon);
	// the levels are bytes in the template (+0x609 .. +0x60C)
	i.crusherLevel &= 0xFF;
	i.crushableLevel &= 0xFF;
	i.mountedCrusherLevel &= 0xFF;
	i.mountedCrushableLevel &= 0xFF;
	return i;
}

const CrushTemplateInfo &CrushTemplateInfo::cached(const Object &obj)
{
	return obj.logic().crushInfo(static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride());
}

const char *ObjectCrush::stopLine()
{
	return kStop;
}

std::vector<std::string> ObjectCrush::combat3StopLines()
{
	return { kCombat3Stop, kCrewStop };
}

// RW 0x68D4D0
int ObjectCrush::crushableLevel(const Object &obj)
{
	const CrushTemplateInfo &t = CrushTemplateInfo::cached(obj);
	// the CRUSHABLE_LEVEL (0x19) sum of the pool, truncated (cvttss2si), added as a byte (lane COMBAT-3)
	int add = 0;
	float sum = 0.0f;
	if (obj.attributeModifierSum(kCrushableLevelModifier, nullptr, sum))
	{
		add = SimMath::truncToInt32(sum);
	}
	int level = t.crushableLevel;
	if (t.mountedCrushableLevel != 0xFF && obj.testModelCondition(names().mounted))
	{
		level = t.mountedCrushableLevel;
	}
	return (int)(signed char)(unsigned char)(level + add); // a byte compared signed (RW 0x8BFC1F `jle`)
}

// RW 0x695070
int ObjectCrush::crusherLevel(const Object &obj)
{
	const CrushTemplateInfo &t = CrushTemplateInfo::cached(obj);
	if (t.crushOnlyWhileCharging && !hasConditionOrContainerHas(obj, names().charging))
	{
		return 0;
	}
	int level = t.crusherLevel;
	if (t.mountedCrusherLevel != 0xFF && obj.testModelCondition(names().mounted))
	{
		level = t.mountedCrusherLevel;
	}
	// the CRUSHER_LEVEL (0x17) sum added as a byte; a negative (signed byte) result is 0 (RW 0x6950DC .. 0x6950EB: sets / dec / and) (lane COMBAT-3)
	float sum = 0.0f;
	if (obj.attributeModifierSum(kCrusherLevelModifier, nullptr, sum))
	{
		const signed char b = (signed char)(unsigned char)(level + SimMath::truncToInt32(sum));
		return b < 0 ? 0 : (int)b;
	}
	return (int)(signed char)(unsigned char)level;
}

// RW 0x68D524
bool ObjectCrush::canCrush(const Object &objIn)
{
	const Object *obj = &objIn;
	while (obj->getContainedBy() && isHorde(*obj->getContainedBy()))
	{
		obj = obj->getContainedBy();
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return false;
	}
	const float pct = CrushTemplateInfo::cached(*obj).minCrushVelocityPercent;
	if (0.0f >= pct)
	{
		return true;
	}
	Locomotor *loco = ai->curLocomotor();
	const float speed = loco ? (loco->speed() > 0.0f ? loco->speed() : 0.0f) : 0.0f; // RW 0x68B34C: 0 without a locomotor, never negative
	const float maxSpeed = loco ? loco->getMaxSpeedForCondition(ai->locomotorHost()) : 0.0f;
	// fld maxSpeed; fmul pct (x87, PC24); fcompi with the speed: crush when max * pct <= speed
	const float need = NumericState::pc24Mul(maxSpeed, pct);
	return need <= speed;
}

// RW 0x69320D
void ObjectCrush::onCrush(Object &crusherIn, Object &victim)
{
	if (victim.isEffectivelyDead())
	{
		return;
	}
	// RW 0x693231 .. 0x69329C: the crusher's SoundCrushing (TheAudio, client)
	if (crusherIn.getContainedBy() && isHorde(*crusherIn.getContainedBy()))
	{
		onCrush(*crusherIn.getContainedBy(), victim); // a member's crush is its horde's
		return;
	}
	Object &crusher = crusherIn;
	GameLogic &logic = crusher.logic();
	const CrushTemplateInfo &t = CrushTemplateInfo::cached(crusher);
	if (t.crushKnockback > 0.0f)
	{
		// RW 0x6932EC .. 0x693365 (lane COMBAT-3): the victim is knocked along the crusher's facing turned 30% toward the victim (at most pi/2: RW 0xBD89D0,
		// * 0.3 RW 0xBE29D4, SSE), in degrees (* 57.29578 RW 0xBD18FC), with CrushKnockback and CrushZFactor (Object::doKnockback RW 0x692223)
		const float rel = ObjectKnockback::relativeAngle2D(crusher, *victim.getPosition());
		const float clamped = rel > 1.57079637f ? 1.57079637f : rel;
		const float angle = SimMath::addf32(SimMath::mulf32(clamped, 0.300000012f), crusher.getOrientation());
		const float degrees = SimMath::mulf32(angle, 57.2957764f);
		if (ObjectKnockback::apply(victim, degrees, t.crushKnockback, t.crushZFactor, "NONE"))
		{
			++logic.combat().counters().crushKnockbacks;
		}
	}
	AIUpdateInterface *ai = crusher.getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	// the crusher's CRUSH_DECELERATE (9) and the victim's CRUSHED_DECELERATE (0x1A) products scale the percentage (SSE) (lane COMBAT-3)
	float decel = t.crushDecelerationPercent;
	float f = 1.0f;
	if (crusher.attributeModifierProduct(kCrushDecelerateModifier, nullptr, true, f))
	{
		decel = SimMath::mulf32(f, decel);
	}
	f = 1.0f;
	if (victim.attributeModifierProduct(kCrushedDecelerateModifier, nullptr, true, f))
	{
		decel = SimMath::mulf32(f, decel);
	}
	if (0.0f >= decel)
	{
		return;
	}
	Locomotor *loco = ai->curLocomotor();
	if (!loco)
	{
		return;
	}
	const unsigned frame = logic.getFrame();
	const float current = loco->getCurrentMaxSpeed(ai->locomotorHost(), frame);              // RW 0x5E4137
	float amount = NumericState::pc24Mul(loco->getMaxSpeedForCondition(ai->locomotorHost()), decel); // fld max (RW 0x6624C9); fmul percent; fstp
	if (isHorde(crusher) && crusher.getContain())
	{
		const int n = (int)crusher.getContain()->getContainCount(); // RW vslot 0x114 of the contain
		if (n > 1)
		{
			amount = SimMath::divf32(amount, SimMath::sseFromInt32(n));
		}
	}
	// the crusher's MINIMUM_CRUSH_VELOCITY (0x12) product: with p = MinCrushVelocityPercent and m = p * product, both clamped to [0.05, 0.95] (RW 0xBDD760 /
	// 0xBDAD74), the amount is scaled by (1 - p) / (1 - m) (RW 0x69346D .. 0x693505, SSE) (lane COMBAT-3)
	f = 1.0f;
	if (crusher.attributeModifierProduct(kMinimumCrushVelocityModifier, nullptr, true, f))
	{
		auto clamp = [](float v) { return 0.05f > v ? 0.05f : (v > 0.949999988f ? 0.949999988f : v); };
		const float p = clamp(t.minCrushVelocityPercent);
		const float m = clamp(SimMath::mulf32(p, f));
		amount = SimMath::mulf32(SimMath::divf32(SimMath::subf32(1.0f, p), SimMath::subf32(1.0f, m)), amount);
	}
	float limit = SimMath::subf32(current, amount);
	if (0.0f > limit)
	{
		limit = 0.0f;
	}
	if (loco->speed() > limit)
	{
		loco->setSpeed(limit);
	}
	loco->setDesiredSpeedCap(limit, frame + (unsigned)LOGICFRAMES_PER_SECOND); // RW 0x5E39E6(limit, [0xD9F608])
	++logic.combat().counters().crushDecelerations;
}

// RW 0x696800 (lane COMBAT-3): a crusher that could not crush `victim` (too slow, canCrush RW 0x68D524) holds its speed: outside a container, against an ENEMIES
// victim, the locomotor's desired speed is capped at its current maximum for LOGICFRAMES_PER_SECOND frames (RW 0x5E39E6); a HORDE member does the same. The
// ENEMIES branch's RW 0x6962DB (a weapon in range attacks the bumped victim: the AI's contact retaliation) is not ported (stop S-1600, counted).
void ObjectCrush::onBump(Object &crusher, Object &victim)
{
	GameLogic &logic = crusher.logic();
	++logic.combat().counters().crushBumps;
	const Object *container = crusher.getContainedBy();
	if (container == nullptr)
	{
		if (crusher.getRelationship(victim) != ENEMIES)
		{
			return;
		}
		++logic.combat().counters().crushBumpAttacksNotPorted; // RW 0x696829 .. 0x69684F: RW 0x68B58C / 0x441B59 / 0x6CC653 -> RW 0x6962DB
	}
	else if (!isHorde(*container))
	{
		return;
	}
	// RW 0x69681E (COMBAT-3 round 2, Sol r1): the locomotor capped is the CONTAINING horde's (a member's bump slows its horde); outside a container the
	// crusher's own. The current maximum is read through the crusher's host (RW 0x5E4137 on the crusher)
	AIUpdateInterface *ai = crusher.getAIUpdateInterface();
	AIUpdateInterface *capAi = container ? const_cast<Object *>(container)->getAIUpdateInterface() : ai;
	Locomotor *loco = capAi ? capAi->curLocomotor() : nullptr; // RW 0x68B31D
	if (!ai || !loco)
	{
		return;
	}
	const unsigned frame = logic.getFrame();
	loco->setDesiredSpeedCap(loco->getCurrentMaxSpeed(ai->locomotorHost(), frame), frame + (unsigned)LOGICFRAMES_PER_SECOND); // RW 0x5E4137 then RW 0x5E39E6
}

bool ObjectCrush::shapesTouch(const Object &self, float selfRadius, const Object &other)
{
	return overlap(shapeOf(self, selfRadius), shapeOf(other, -1.0f));
}

void SquishCollide::registerClass(ModuleFactory &modules)
{
	// the registry entry: create RW 0x650D84, the shared collide data RW 0x654811 (no fields of its own)
	modules.bindModuleProc("SquishCollide", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<SquishCollide>(thing, data);
	});
}

// RW 0x8BFBAE
void SquishCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	Object *self = getObject();
	if (!other || other->isEffectivelyDead() || isHorde(*other) || self->isEffectivelyDead())
	{
		return;
	}
	if (AIUpdateInterface *sai = self->getAIUpdateInterface())
	{
		if (sai->locomotorHost().physicsMotionDisabled())
		{
			return; // self + 0x264 module byte + 0x5C
		}
	}
	if (!(ObjectCrush::crusherLevel(*other) > ObjectCrush::crushableLevel(*self)))
	{
		return;
	}
	GameLogic &logic = self->logic();
	// other must move toward self: its facing against self - other, else against self - other's recorded position
	{
		float fx = 0.0f, fy = 0.0f;
		facing(*other, fx, fy);
		const Coord3D &sp = *self->getPosition();
		const Coord3D &op = *other->getPosition();
		const float dx = SimMath::subf32(sp.x, op.x), dy = SimMath::subf32(sp.y, op.y);
		const float d1 = SimMath::addf32(SimMath::mulf32(fy, dy), SimMath::mulf32(fx, dx));
		if (0.0f > d1)
		{
			const Coord3D &rp = other->hasRecordedTransform() ? other->getRecordedPosition() : op;
			const float ex = SimMath::subf32(sp.x, rp.x), ey = SimMath::subf32(sp.y, rp.y);
			const float d2 = SimMath::addf32(SimMath::mulf32(fy, ey), SimMath::mulf32(fx, ex));
			if (0.0f > d2)
			{
				return;
			}
		}
	}
	const CrushTemplateInfo &ot = CrushTemplateInfo::cached(*other);
	if (!other->testStatus((unsigned)names().rampaging) && !other->testStatus((unsigned)names().fleeOffMap) && !other->testModelCondition(names().charging) && !ot.crushAllies)
	{
		if (other->getRelationship(*self) != ENEMIES)
		{
			return;
		}
	}
	if (!ObjectCrush::shapesTouch(*self, 5.0f, *other)) // RW 0xBDAE58 = 5.0
	{
		return;
	}
	if (!ObjectCrush::canCrush(*other))
	{
		ObjectCrush::onBump(*other, *self); // RW 0x696800
		return;
	}
	// self's body slot 0x9C(other): yes for every body of 2.01 (PorcupineFormationBodyModule's check is gated off by RW 0x8C6244)
	ObjectCrush::onCrush(*other, *self);
	if (ot.ramPower > 0.0f)
	{
		// RW 0x8BFEC6 .. 0x8BFF68 (lane COMBAT-4): a 0-damage CRUSH hit from the crusher whose shockwave half throws self: the vector is the normalised
		// self - crusher (Coord3D::normalize RW 0x403175), the amount RamPower (+0x618), the radius 10.0 (RW 0xBD83D8), the taper 1.0, the z mult RamZMult (+0x61C)
		DamageInfo info; // RW 0x66365E
		Coord3D v{ SimMath::subf32(self->getPosition()->x, other->getPosition()->x), SimMath::subf32(self->getPosition()->y, other->getPosition()->y),
			SimMath::subf32(self->getPosition()->z, other->getPosition()->z) };
		ObjectKnockback::normalize(v);
		info.m_input.m_shockWaveVector = v;
		info.m_input.m_shockWaveAmount = ot.ramPower;
		info.m_input.m_shockWaveZMult = ot.ramZMult;
		info.m_input.m_shockWaveRadius = 10.0f;
		info.m_input.m_shockWaveTaperOff = 1.0f;
		info.m_input.m_sourceID = other->getID();
		info.m_input.m_deathType = DEATH_NORMAL;
		info.m_input.m_damageType = DAMAGE_CRUSH;
		info.m_input.m_amount = 0.0f;
		self->attemptDamage(info); // RW 0x698E7D
		++logic.combat().counters().ramHits;
	}
	++logic.combat().counters().crushes;
	ObjectWeapons *ow = other->getWeapons();
	if (ow && ow->crushWeapon())
	{
		ow->fireExtraWeapon(*ow->crushWeapon(), *self);
		++logic.combat().counters().crushWeaponShots;
	}
	else
	{
		if (!ow && !ot.crushWeapon.empty())
		{
			logic.reportError("SquishCollide: " + other->getTemplate()->getName() + " names CrushWeapon " + ot.crushWeapon + " but has no weapon set to fire it from (S-580)");
		}
		DamageInfo info;
		info.m_input.m_sourceID = other->getID();
		info.m_input.m_damageType = DAMAGE_CRUSH;
		info.m_input.m_deathType = DEATH_CRUSHED;
		info.m_input.m_amount = 999999.0f; // RW 0xC70E98
		self->attemptDamage(info);
	}
	ObjectWeapons *sw = self->getWeapons();
	if (sw && sw->crushRevengeWeapon()) // RW fires it whether or not the crush killed self
	{
		// RW 0x8BFFE5: not when self is flanked by other
		if (!HordeFlank::isFlankedBy(*self, *other))
		{
			sw->fireExtraWeapon(*sw->crushRevengeWeapon(), *other);
			++logic.combat().counters().crushWeaponShots;
		}
	}
}

// ZH Object::onCollide (Object.cpp): every collide module in module order; stop once the object is destroyed
void ObjectCollide::onCollide(Object &self, Object &other)
{
	for (const std::unique_ptr<BehaviorModule> &m : self.modules())
	{
		if (self.isDestroyed())
		{
			return;
		}
		if (CollideModuleInterface *c = m->getCollide())
		{
			c->onCollide(&other, nullptr, nullptr);
		}
	}
}

void HordeMemberCollide::registerClass(ModuleFactory &modules)
{
	modules.bindModuleProc("HordeMemberCollide", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<HordeMemberCollide>(thing, data);
	});
}

namespace
{
HordeAIUpdate *hordeAiOf(Object *horde)
{
	return horde ? dynamic_cast<HordeAIUpdate *>(horde->getAIUpdateInterface()) : nullptr;
}
} // namespace

// RW 0x8C0518
void HordeMemberCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	Object *self = getObject();
	if (!other || !self->getContainedBy())
	{
		return;
	}
	HordeAIUpdate *h = hordeAiOf(self->getContainedBy());
	if (!h || h->meleeTargetId() == 0)
	{
		return;
	}
	const ObjectID tid = h->meleeTargetId();
	if (other->getID() == tid)
	{
		h->refreshMeleeReadiness(*other);
		return;
	}
	Object *target = self->logic().findObjectByID(tid);
	if (h->isInCurrentMelee(target))
	{
		return;
	}
	if (other->getContainedBy() && other->getContainedBy()->getID() == tid)
	{
		h->refreshMeleeReadiness(*other);
		return;
	}
	Object *otherHorde = other;
	if (other->testStatus((unsigned)CombatNames::statuses().hordeMember) && other->getContainedBy()) // RW 0x6939DF: HORDE_MEMBER with a HORDE container
	{
		otherHorde = other->getContainedBy();
	}
	if (!isHorde(*otherHorde))
	{
		return;
	}
	if (self->getRelationship(*otherHorde) != ALLIES)
	{
		return;
	}
	HordeAIUpdate *oh = hordeAiOf(otherHorde);
	if (oh && target && oh->isInCurrentMelee(target) && oh->hasMeleeAttackingMember())
	{
		h->refreshMeleeReadiness(*target);
	}
}
