// OpenBFME. GPL-3.0.
// See GameLogic/System/InvisibilityManager.h for the target facts, the call sites and what is inference.

#include "GameLogic/System/InvisibilityManager.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponState.h"

#include <variant>

namespace
{
const unsigned kLogicFramesPerSecond = 5; // RW 0xD9F608
const float kTreeRange = 50.0f;           // RW 0xBD88C4

// RW 0xDA7484 .. 0xDA74D0: one run of name pointers, NULL at RW 0xDA74D4. The three parsers start at different points of it.
const char *const kNameRun[] = { "STEALTH", "CAMOUFLAGE", "AWAY_FROM_TREES", "MOVING", "FIRING_PRIMARY", "FIRING_SECONDARY", "FIRING_TERTIARY", "FIRING_QUATERNARY",
	"FIRING_QUINARY", "FIRING_ANY", "TAKING_DAMAGE", "USING_ABILITY", "ALLOW_NEAR_TREES", "DETECTED_BY_FRIENDLIES", "DISCONTINUE_WHEN_REVEALED",
	"UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH", "NONE", "HOLD", "KILL", "SPAWN", nullptr };
const char *const *kTypeNames = kNameRun;          // RW 0xDA7484
const char *const *kForbiddenNames = kNameRun + 2; // RW 0xDA748C
const char *const *kOptionNames = kNameRun + 12;   // RW 0xDA74B4

void parseWeaponConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<WeaponConditionFlags *>(store)->data(), 4, TheWeaponConditionNames); // RW 0x6C9951
}
void parseUpgradeMask(INI *ini, void *, void *store, const void *)
{
	UpgradeCenter::parseUpgradeMask(ini, *static_cast<UpgradeMaskType *>(store), nullptr); // RW 0x66F603
}
// RW 0x73A302 (INI::parseFXList): "None" (any case) stores none; any other name must be an FXList of TheFXListStore, else INIException 3
void parseFXName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	std::string &out = *static_cast<std::string *>(store);
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		out.clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	out = name;
}

#define NG_OFF(m) (int)offsetof(InvisibilityNugget, m)
// RW 0xC507B8, in the binary's row order
const FieldParse kNuggetFields[] = {
	{ "ForbiddenConditions", INI::parseBitString32, kNameRun + 2, NG_OFF(forbiddenConditions) },
	{ "ForbiddenWeaponConditions", parseWeaponConditions, nullptr, NG_OFF(forbiddenWeaponConditions) },
	{ "DetectionRange", INI::parseReal, nullptr, NG_OFF(detectionRange) },
	{ "InvisibilityType", INI::parseIndexList, kNameRun, NG_OFF(invisibilityType) },
	{ "IgnoreTreeCheckUpgrades", parseUpgradeMask, nullptr, NG_OFF(ignoreTreeCheckUpgrades) },
	{ "Options", INI::parseBitString32, kNameRun + 12, NG_OFF(options) },
	{ "BecomeStealthedFX", parseFXName, nullptr, NG_OFF(becomeStealthedFX) },
	{ "ExitStealthFX", parseFXName, nullptr, NG_OFF(exitStealthFX) },
	{ "HintDetectableConditions", ParseObjectStatusMask, nullptr, NG_OFF(hintDetectableConditions) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef NG_OFF

struct Bits
{
	int invisibleStealth = CombatNames::modelCondition("INVISIBLE_STEALTH");       // 0x222
	int invisibleCamouflage = CombatNames::modelCondition("INVISIBLE_CAMOUFLAGE"); // 0x223
	int burningDeath = CombatNames::modelCondition("BURNINGDEATH");                 // 0x220 (RW 0x81B970)
	int invisibleDetected = CombatNames::status("INVISIBLE_DETECTED");              // 0x60
	int invisibleDetectedByFriend = CombatNames::status("INVISIBLE_DETECTED_BY_FRIEND"); // 0x5F
	int detected = CombatNames::status("DETECTED");                                 // 0x11
	int stealthed = CombatNames::status("STEALTHED");                               // 0xF
	int usingAbility = CombatNames::status("USING_ABILITY");                        // 0x18
	int hidden = CombatNames::status("HIDDEN");                                     // 0x10
	int horde = CombatNames::kindOf("HORDE");                                       // template + 0x115 bit 5
	int tree = CombatNames::kindOf("TREE");                                         // KindOf 0x5E
};
const Bits &bits()
{
	static const Bits b;
	return b;
}

bool isHorde(const Object &o)
{
	return o.isKindOf((unsigned)bits().horde);
}

// the members of a HORDE object (its contain list), in list order
template <class F>
void forEachMember(const Object &horde, F f)
{
	if (const ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			for (Object *m : *items)
			{
				if (m)
				{
					f(*m);
				}
			}
		}
	}
}

// 3D squared distance in RW's order (RW 0x81B75F / 0x67E29E: dz*dz + dy*dy, then + dx*dx)
float distanceSquared3(const Coord3D &a, const Coord3D &b)
{
	const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y), dz = SimMath::subf32(a.z, b.z);
	return SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
}

// ThingTemplate + 0x10 CamouflageDetectionMultiplier (table RW 0xDA3DF8, parseReal; data\ini\default\object.ini gives every template its default)
float camouflageMultiplier(const Object &o, bool *known)
{
	*known = false;
	if (const ThingTemplate *t = o.getTemplate())
	{
		if (const FieldValue *v = t->getFinalOverride()->findField("CamouflageDetectionMultiplier"))
		{
			if (const float *f = std::get_if<float>(v))
			{
				*known = true;
				return *f;
			}
		}
	}
	return 0.0f;
}

float visionRange(const Object &o)
{
	if (const FieldValue *v = o.getTemplate()->getFinalOverride()->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}
} // namespace

// ---- InvisibilityNugget -------------------------------------------------------------------------------------------------------------
const char *const *InvisibilityNugget::typeNames()
{
	return kTypeNames;
}
const char *const *InvisibilityNugget::forbiddenNames()
{
	return kForbiddenNames;
}
const char *const *InvisibilityNugget::optionNames()
{
	return kOptionNames;
}

void InvisibilityNugget::parse(INI *ini, void *, void *store, const void *)
{
	ini->initFromINI(store, kNuggetFields); // RW 0x81A667
}

bool InvisibilityNugget::sameAs(const InvisibilityNugget &o) const
{
	// RW 0x81A7A3: the hint mask is compared with itself (RW 0x81A818 pushes the same address twice), so it never makes two nuggets differ
	return forbiddenConditions == o.forbiddenConditions && forbiddenWeaponConditions == o.forbiddenWeaponConditions && detectionRange == o.detectionRange &&
	       invisibilityType == o.invisibilityType && ignoreTreeCheckUpgrades == o.ignoreTreeCheckUpgrades && options == o.options &&
	       becomeStealthedFX == o.becomeStealthedFX && exitStealthFX == o.exitStealthFX;
}

void InvisibilityNugget::crc(StateHasher &h) const
{
	h.addU32(forbiddenConditions);
	for (std::uint32_t w : forbiddenWeaponConditions)
	{
		h.addU32(w);
	}
	h.addFloat(detectionRange);
	h.addI32(invisibilityType);
	for (std::uint32_t w : ignoreTreeCheckUpgrades.words)
	{
		h.addU32(w);
	}
	h.addU32(options);
	h.addString(becomeStealthedFX);
	h.addString(exitStealthFX);
	for (std::uint32_t w : hintDetectableConditions)
	{
		h.addU32(w);
	}
}

// ---- the object queries -------------------------------------------------------------------------------------------------------------
// RW 0x68FC06
int InvisibilityManager::invisibilityType(const Object &obj)
{
	if (obj.testModelCondition(bits().invisibleStealth))
	{
		return InvisibilityNugget::STEALTH;
	}
	return obj.testModelCondition(bits().invisibleCamouflage) ? InvisibilityNugget::CAMOUFLAGE : InvisibilityNugget::NONE;
}

// RW 0x68FC2F
bool InvisibilityManager::isInvisible(const Object &obj)
{
	return invisibilityType(obj) != InvisibilityNugget::NONE;
}

// RW 0x693BF2: the horde chain is walked up (RW 0x693A1A(0) until it answers null or itself), then that object's two statuses
bool InvisibilityManager::isDetected(const Object &obj)
{
	const Object *o = &obj;
	for (;;)
	{
		const Object *h = o->getHordeObject(false);
		if (!h || h == o)
		{
			break;
		}
		o = h;
	}
	return o->testStatus((unsigned)bits().invisibleDetected) || o->testStatus((unsigned)bits().invisibleDetectedByFriend);
}

// RW 0x68C89B
bool InvisibilityManager::isFiring(const Object &obj)
{
	ObjectWeapons *w = obj.getWeapons();
	if (!w || !w->currentWeapon()) // RW 0x68B58C(0)
	{
		return false;
	}
	if (w->currentStatus() != WEAPON_READY_TO_FIRE) // RW 0x6CD142(0)
	{
		return true;
	}
	const unsigned last = w->firingTracker().lastShotFrame(); // RW 0x68B645
	return last > 2u && last + 2u * kLogicFramesPerSecond > obj.logic().getFrame();
}

// RW 0x694C0D. Every test is a pure read, so the cheap status tests come first here (RW tests the draw modules first; the answer is the same)
bool InvisibilityManager::isStealthedAndUndetected(const Object &obj, const Player *viewer)
{
	if (!isInvisible(obj) && !obj.testStatus((unsigned)bits().stealthed))
	{
		return false;
	}
	if (isDetected(obj) || obj.testStatus((unsigned)bits().detected))
	{
		return false;
	}
	if (viewer && viewer->getPlayerType() == PLAYER_COMPUTER && isFiring(obj)) // RW 0x694C75: player + 0x5C == 1 (ZH m_playerType, INFERENCE)
	{
		return false;
	}
	// RW 0x694C11 .. 0x694C3B: every draw module of the drawable must answer slot 0xF0 (read as the model draw data's AffectedByStealth, INFERENCE S-1040)
	if (const ThingTemplate *t = obj.getTemplate())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->drawModules().nuggets())
		{
			if (const W3DModelDrawModuleData *d = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get()))
			{
				if (!d->m_affectedByStealth)
				{
					return false;
				}
			}
		}
	}
	// RW 0x694C86 .. 0x694CBF (lane STEALTH-2): a DISGUISER (template + 0x113 bit 0) disguised by its StealthUpdate (RW 0x68FBD3 + 0x3C) is seen through only by
	// a viewer whom the disguise player (RW 0x6A844E) holds as ENEMIES (RW 0x6ACEAF: the player relation, NEUTRAL without one)
	static const int kDisguiser = CombatNames::kindOf("DISGUISER");
	if (obj.isKindOf((unsigned)kDisguiser))
	{
		if (const StealthUpdate *st = StealthUpdate::of(obj))
		{
			if (st->disguiseTemplate())
			{
				if (const Player *as = obj.logic().players().getNthPlayer(st->disguisePlayerIndex()))
				{
					if (as->getRelationship(viewer) == ENEMIES)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

// RW 0x81AA03
int InvisibilityManager::clientLook(const Object &obj, const Player *local)
{
	bool friendView = true; // RW 0x81AA2B / 0x81AA39: no team: 1 (not a friend); an inactive local player: 2 (a friend)
	if (local)
	{
		const Player *owner = obj.getControllingPlayer();
		friendView = owner && (owner == local || owner->getRelationship(local->getDefaultTeam()) == ALLIES); // RW 0x7A3DAD(local team) == 2
	}
	if (isInvisible(obj))
	{
		return friendView ? 1 : 5;
	}
	const Object *root = &obj; // RW 0x693BF2's walk; the BY_FRIEND status is tested on the object RW 0x81AA67 (here: the root that carries it)
	for (const Object *h = root->getHordeObject(false); h && h != root; h = root->getHordeObject(false))
	{
		root = h;
	}
	if (isDetected(obj) && !root->testStatus((unsigned)bits().invisibleDetectedByFriend))
	{
		return friendView ? 4 : 3;
	}
	// RW 0x77661E (StealthUpdate's look, the same states): a live STEALTHED object; DisguisesAsTeam (the disguise, not ported) seen by a friend: normal; DETECTED:
	// normal with DetectedByFriendliesOnly, else 3 / 4; undetected: 1 / 5
	if (obj.isEffectivelyDead() || !obj.testStatus((unsigned)bits().stealthed))
	{
		return 0;
	}
	if (const StealthUpdate *st = StealthUpdate::of(obj))
	{
		if (st->data()->m_disguisesAsTeam)
		{
			return 0;
		}
		if (obj.testStatus((unsigned)bits().detected))
		{
			return st->data()->m_detectedByFriendliesOnly ? 0 : (friendView ? 4 : 3);
		}
	}
	else if (obj.testStatus((unsigned)bits().detected))
	{
		return friendView ? 4 : 3;
	}
	return friendView ? 1 : 5;
}

void InvisibilityManager::clientOpacityRange(const Object &obj, float *lo, float *hi, unsigned *cycleFrames) const
{
	if (!isInvisible(obj))
	{
		if (const StealthUpdate *st = StealthUpdate::of(obj)) // RW 0x777AAF .. 0x777AF7: FriendlyOpacityMin / Max, PulseFrequency * 0.2f
		{
			*lo = st->data()->m_friendlyOpacityMin;
			*hi = st->data()->m_friendlyOpacityMax;
			*cycleFrames = st->data()->m_pulseFrequency;
			return;
		}
	}
	const GameLogicSettings &s = m_logic.settings(); // RW 0x81AAC0 .. 0x81AAFF
	*lo = s.invisibilityOpacityMin;
	*hi = s.invisibilityOpacityMax;
	*cycleFrames = s.invisibilityOpacityCycleFrames;
}

// ---- the manager --------------------------------------------------------------------------------------------------------------------
InvisibilityManager::InvisibilityManager(GameLogic &logic)
	: m_logic(logic)
{
}

// RW 0x81A6A1
void InvisibilityManager::noteCreated(const Object &obj)
{
	const ObjectFilter *f = m_logic.settings().camouflageDetectorFilter.get();
	if (!f || !ObjectFilterMatch::allows(m_logic, *f, obj.getTemplate(), nullptr, nullptr))
	{
		return;
	}
	bool known = false;
	const float mult = camouflageMultiplier(obj, &known);
	if (known && mult > m_maxMultiplier) // RW 0x81A6A7: comiss x, max; jbe
	{
		m_maxMultiplier = mult;
	}
}

// RW 0x81BE40
void InvisibilityManager::reset()
{
	m_entries.clear();
	m_nextUpdate = 0;
	m_maxMultiplier = 1.0f; // RW 0x81BE4B: 1.0 (RW 0xBD1908)
	m_reveals = 0;
	m_becameInvisible = 0;
}

void InvisibilityManager::note(const std::string &what)
{
	m_logic.noteStop("[S-1040] " + what);
}

// RW 0x81C23C .. 0x81C250: a horde object, or an object no horde contains
bool InvisibilityManager::tracked(const Object &obj)
{
	return isHorde(obj) || obj.getHordeObject(false) == nullptr;
}

size_t InvisibilityManager::appliedCount(ObjectID id) const
{
	auto it = m_entries.find(id);
	return it == m_entries.end() ? 0u : it->second.applied.size();
}

// RW 0x81C217
void InvisibilityManager::applyNugget(Object *obj, unsigned frames, const InvisibilityNugget &nugget)
{
	if (!obj || !tracked(*obj) || nugget.invisibilityType >= InvisibilityNugget::NONE)
	{
		return;
	}
	Entry &e = m_entries[obj->getID()];
	for (Applied &a : e.applied)
	{
		if (a.startFrame != 0 && a.nugget->sameAs(nugget)) // RW 0x81C2C7: + 0xC8 (the start frame) non-zero, then RW 0x81A7A3
		{
			a.frames += frames; // RW 0x81C324
			return;
		}
	}
	Applied a; // RW 0x81A8F5
	a.nugget = &nugget;
	a.startFrame = m_logic.getFrame();
	a.frames = frames;
	a.active = false;
	e.applied.push_back(a); // RW 0x81B617
	evaluate(obj, e);       // RW 0x81BCBD
}

// RW 0x81C32C
bool InvisibilityManager::markDetected(Object *target, Object *detector, unsigned frames, int mode)
{
	(void)mode; // the client notification's mode (RW 0x81ACD7)
	if (!target || !tracked(*target))
	{
		return false;
	}
	Entry &e = m_entries[target->getID()];
	bool byFriend = false;
	if (detector)
	{
		const Player *dp = detector->getControllingPlayer();
		const Player *tp = target->getControllingPlayer();
		byFriend = dp && tp && dp->getRelationship(tp->getDefaultTeam()) == ALLIES; // RW 0x81C3E5: RW 0x6ADBEB(target player's default team) == 2
	}
	const unsigned until = m_logic.getFrame() + frames; // RW 0x81A751
	if (until > e.notBefore)
	{
		e.notBefore = until;
	}
	Outcome out;
	const int type = computeType(target, e, out);
	out.byFriend = byFriend;   // RW 0x81C429: written after RW 0x81BB8A
	out.detector = detector;   // RW 0x81C42C
	if (applyType(target, type, e, out, frames))
	{
		note("the reveal's client notification (RW 0x81ACD7: radar event, EVA, MESSAGE:StealthDiscovered, sounds) is the client's and is not run");
		return true;
	}
	return false;
}

// RW 0x81BE85
void InvisibilityManager::update()
{
	const unsigned now = m_logic.getFrame();
	if (now < m_nextUpdate)
	{
		return;
	}
	m_nextUpdate = now + kLogicFramesPerSecond;
	std::vector<ObjectID> gone;
	for (auto &kv : m_entries)
	{
		Object *obj = m_logic.findObjectByID(kv.first); // RW 0x449681
		if (!obj || !evaluate(obj, kv.second))
		{
			gone.push_back(kv.first);
		}
	}
	for (ObjectID id : gone) // RW 0x81BF14: erased after the walk
	{
		m_entries.erase(id);
	}
}

// RW 0x81BCBD
bool InvisibilityManager::evaluate(Object *obj, Entry &e)
{
	const GameLogicSettings &s = m_logic.settings();
	if (!s.invisibilityRulesLoaded)
	{
		m_logic.reportError("InvisibilityManager: the GameData invisibility rows (CamouflageDetectorObjectFilter, ReinvisibityDelay, InvisibilityOpacity*) were not loaded");
	}
	Outcome out;
	const int type = computeType(obj, e, out);
	if (applyType(obj, type, e, out, s.reinvisibilityDelay)) // RW 0x81BCFC with GlobalData + 0x11CC
	{
		note("the reveal's client notification (RW 0x81ACD7: radar event, EVA, MESSAGE:StealthDiscovered, sounds) is the client's and is not run");
		const unsigned until = m_logic.getFrame() + s.reinvisibilityDelay; // RW 0x81A751
		if (until > e.notBefore)
		{
			e.notBefore = until;
		}
	}
	const unsigned now = m_logic.getFrame();
	if (isDetected(*obj) && now >= e.detectedUntil) // RW 0x81BD3B .. 0x81BD4F
	{
		clearDetected(obj);
	}
	// RW 0x81AA85: the client opacity of the drawable (the client reads the model conditions and the GameData opacity rows)
	return !e.applied.empty() || now < e.notBefore || now < e.detectedUntil; // RW 0x81BD5C .. 0x81BD73
}

// RW 0x81BB8A
int InvisibilityManager::computeType(Object *obj, Entry &e, Outcome &out)
{
	const unsigned now = m_logic.getFrame();
	// RW 0x81AB8A: an inactive copy whose lifetime ended goes
	for (auto it = e.applied.begin(); it != e.applied.end();)
	{
		if (!it->active && now > it->startFrame + it->frames)
		{
			it = e.applied.erase(it);
		}
		else
		{
			++it;
		}
	}
	int result = InvisibilityNugget::NONE;
	for (Applied &a : e.applied)
	{
		const bool wasActive = a.active;
		ObjectID found = INVALID_ID;
		const bool on = now <= a.startFrame + a.frames && allowed(obj, *a.nugget, e, &found);
		a.active = on;
		if (on)
		{
			if (a.nugget->invisibilityType == InvisibilityNugget::STEALTH) // RW 0x81BC09: the first active STEALTH copy decides
			{
				if (!wasActive && !a.nugget->becomeStealthedFX.empty() && !out.becomeFX)
				{
					out.becomeFX = &a.nugget->becomeStealthedFX;
				}
				return InvisibilityNugget::STEALTH;
			}
			result = InvisibilityNugget::CAMOUFLAGE;
			continue;
		}
		if (!wasActive)
		{
			continue;
		}
		const unsigned opt = a.nugget->options; // RW 0x81BC22: the copy stopped being active
		if (opt & InvisibilityNugget::DISCONTINUE_WHEN_REVEALED)
		{
			a.startFrame = 0;
			a.frames = 0;
		}
		if (opt & InvisibilityNugget::UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH)
		{
			out.untoggleHidden = true;
		}
		if (found != INVALID_ID)
		{
			if (opt & InvisibilityNugget::DETECTED_BY_FRIENDLIES)
			{
				out.byFriend = true;
			}
			if (!out.detector)
			{
				out.detector = m_logic.findObjectByID(found);
			}
		}
		if (!a.nugget->exitStealthFX.empty() && !out.exitFX)
		{
			out.exitFX = &a.nugget->exitStealthFX;
		}
		for (size_t i = 0; i < out.hint.size(); ++i) // RW 0x68CC53
		{
			out.hint[i] |= a.nugget->hintDetectableConditions[i];
		}
	}
	return result;
}

// RW 0x81B948
bool InvisibilityManager::allowed(Object *obj, const InvisibilityNugget &n, const Entry &e, ObjectID *found)
{
	const Bits &b = bits();
	const unsigned now = m_logic.getFrame();
	if (obj->isEffectivelyDead() || isDetected(*obj) || obj->testModelCondition(b.burningDeath) || now < e.notBefore)
	{
		return false;
	}
	if (const StealthUpdate *st = StealthUpdate::of(*obj)) // RW 0x81B995: a disguised object (its StealthUpdate's + 0x3C, lane STEALTH-2) is not invisible
	{
		if (st->disguiseTemplate())
		{
			return false;
		}
	}
	bool ok = true;
	if (n.forbiddenConditions & InvisibilityNugget::MOVING) // RW 0x68B34C: the locomotor's current speed above 0
	{
		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
		if (loco && loco->speed() > 0.0f)
		{
			ok = false;
		}
	}
	if (firingForbidden(*obj, n.forbiddenConditions))
	{
		ok = false;
	}
	bool anyWeaponBit = false; // RW 0x776243
	for (std::uint32_t w : n.forbiddenWeaponConditions)
	{
		anyWeaponBit = anyWeaponBit || w != 0;
	}
	if (anyWeaponBit)
	{
		if (const ObjectWeapons *w = obj->getWeapons()) // RW 0x68BE7D -> 0x75CDC4
		{
			const WeaponSetFlags &f = w->weaponSetFlags();
			for (size_t i = 0; i < n.forbiddenWeaponConditions.size(); ++i)
			{
				if (f[i] & n.forbiddenWeaponConditions[i])
				{
					ok = false;
				}
			}
		}
	}
	if ((n.forbiddenConditions & InvisibilityNugget::USING_ABILITY) && obj->testStatus((unsigned)b.usingAbility))
	{
		ok = false;
	}
	if ((n.forbiddenConditions & InvisibilityNugget::TAKING_DAMAGE) && invisibilityType(*obj) != InvisibilityNugget::NONE)
	{
		// RW 0x81BA33 .. 0x81BA6E: the body's last damage frame (slot 0x44) at or after the frame the object became invisible, a non-zero amount, a type other
		// than 7; INFERENCE (S-1040): the engine body records damaging hits only, so the frame alone decides
		if (const ActiveBody *body = dynamic_cast<const ActiveBody *>(obj->getBodyModule()))
		{
			if (body->lastDamageFrame() != 0xFFFFFFFFu && body->lastDamageFrame() >= e.invisibleSince)
			{
				ok = false;
			}
		}
	}
	const bool nearOverride = !ok && (n.options & InvisibilityNugget::ALLOW_NEAR_TREES);
	if (nearOverride || (n.forbiddenConditions & InvisibilityNugget::AWAY_FROM_TREES))
	{
		if (nearTrees(*obj, n))
		{
			if (nearOverride)
			{
				ok = true;
			}
		}
		else if (n.forbiddenConditions & InvisibilityNugget::AWAY_FROM_TREES)
		{
			ok = false;
		}
	}
	if (!ok)
	{
		return false;
	}
	// RW 0x81BAC3: a STEALTH copy and the script's stealth switch (Object + 0x457 bit 8, RW 0x7BC7EC) -- not ported, never set (S-1040)
	if (n.invisibilityType == InvisibilityNugget::CAMOUFLAGE && camouflageDetected(*obj, n, found))
	{
		return false;
	}
	return true;
}

// RW 0x81A926
bool InvisibilityManager::firingForbidden(const Object &obj, unsigned forbidden) const
{
	bool firing = false;
	if (isHorde(obj))
	{
		forEachMember(obj, [&](const Object &m) { firing = firing || isFiring(m); }); // RW 0x81A635
	}
	else
	{
		firing = isFiring(obj);
	}
	if (!firing)
	{
		return false;
	}
	if (forbidden & InvisibilityNugget::FIRING_ANY)
	{
		return true;
	}
	const unsigned now = m_logic.getFrame();
	const ObjectWeapons *w = obj.getWeapons();
	for (int slot = 0; slot < 5; ++slot) // RW 0x81A98E: FIRING_PRIMARY (bit 2) .. FIRING_QUINARY (bit 6) -> slots 0 .. 4
	{
		if (!(forbidden & (InvisibilityNugget::FIRING_PRIMARY << slot)) || !w)
		{
			continue;
		}
		if (const Weapon *weapon = w->weaponInSlot(slot))
		{
			if (weapon->lastFireFrame() >= now - 1u) // RW 0x81A9EB: unsigned
			{
				return true;
			}
		}
	}
	return false;
}

// RW 0x81ABDF
bool InvisibilityManager::nearTrees(const Object &obj, const InvisibilityNugget &n) const
{
	if (obj.getUpgradeMask().testForAny(n.ignoreTreeCheckUpgrades)) // RW 0x8097D6
	{
		return true;
	}
	return treesNear(*obj.getPosition(), &obj);
}

bool InvisibilityManager::treesNear(const Coord3D &p, const Object *self) const
{
	(void)self; // retail has no "not the object" filter here
	const float r2 = SimMath::mulf32(kTreeRange, kTreeRange);
	// RW 0x81AC0B .. 0x81AC56: ThePartitionManager's closest object within 50.0 (RW 0xBD88C4), distance type 0, with the filters RW 0xC10E20 (alive) and the
	// KindOf filter RW 0x445139 (TREE required) (lane MODULES-2)
	const int tree = bits().tree;
	PartitionFilterFn trees([tree](Object &o) { return !o.isEffectivelyDead() && o.isKindOf((unsigned)tree); });
	if (m_logic.partition().getClosestObject(p, kTreeRange, FROM_CENTER_2D, { &trees }))
	{
		return true;
	}
	// RW 0x67F52B -> 0x67E078: a terrain tree whose 3D squared distance is below 50^2 (strict, RW 0x67E2E3)
	for (const Coord3D &t : m_trees)
	{
		if (r2 > distanceSquared3(t, p))
		{
			return true;
		}
	}
	return false;
}

// RW 0x81B64F
bool InvisibilityManager::camouflageDetected(const Object &obj, const InvisibilityNugget &n, ObjectID *found) const
{
	const GameLogicSettings &s = m_logic.settings();
	const unsigned relMask = (n.options & InvisibilityNugget::DETECTED_BY_FRIENDLIES) ? (1u << ALLIES) : (1u << ENEMIES); // RW 0x81B665 .. 0x81B684
	const Player *objPlayer = obj.getControllingPlayer();
	const ObjectFilter *f = s.camouflageDetectorFilter.get();
	// RW 0x81B6F0 .. 0x81B758: ThePartitionManager->iterateObjectsInRange(position, largest multiplier (+ 0x10) * DetectionRange, distance type 2, the filter chain
	// in RW's order (a contain's rider out of the world, RW 0x68C18F, is not in the partition): not the object (RW 0xC1D660), alive (RW 0xC10E20), has an AI
	// (RW 0xC2E65C), the same map status (RW 0xC0F374: always, no off-map objects are
	// ported), CamouflageDetectorObjectFilter with the object's player (RW 0xBE4CC8, flag 1), the candidate's relationship to the object (RW 0xC11DC0, flag 1),
	// ITER_FASTEST); then, in the result order, the first whose 3D squared distance is below (its multiplier * DetectionRange)^2
	PartitionFilterFn notSelf([&](Object &c) { return &c != &obj; });
	PartitionFilterFn alive([](Object &c) { return !c.isEffectivelyDead(); });
	PartitionFilterFn hasAI([](Object &c) { return c.getAIUpdateInterface() != nullptr; });
	PartitionFilterFn detectorFilter([&](Object &c) { return f && ObjectFilterMatch::allows(m_logic, *f, c, objPlayer); });
	PartitionFilterFn relationship([&](Object &c) { return (relMask & (1u << c.getRelationship(obj))) != 0; });
	const Coord3D &p = *obj.getPosition();
	const float radius = SimMath::mulf32(m_maxMultiplier, n.detectionRange); // RW 0x81B73B: + 0x10 * nugget + 0x14
	const PartitionHits hits = m_logic.partition().iterateObjectsInRange(p, radius, FROM_BOUNDINGSPHERE_2D, { &notSelf, &alive, &hasAI, &detectorFilter, &relationship },
		ITER_FASTEST);
	for (const PartitionHit &hit : hits)
	{
		Object *c = hit.object;
		bool known = false;
		const float mult = camouflageMultiplier(*c, &known);
		if (!known)
		{
			m_logic.reportError("InvisibilityManager: template " + c->getTemplate()->getName() + " has no CamouflageDetectionMultiplier");
			continue;
		}
		const float r = SimMath::mulf32(mult, n.detectionRange); // RW 0x81B77F
		if (SimMath::mulf32(r, r) > distanceSquared3(p, *c->getPosition())) // RW 0x81B7AD: comiss r^2, d^2; ja
		{
			if (found)
			{
				*found = c->getID();
			}
			return true;
		}
	}
	return false;
}

// RW 0x81AB0A: the object and, for a HORDE, every member (RW 0x81A657)
void InvisibilityManager::setType(Object *obj, int type)
{
	auto set = [&](Object &o) { // RW 0x691AA3
		const Bits &b = bits();
		o.setModelConditionState(b.invisibleStealth, type == InvisibilityNugget::STEALTH);
		o.setModelConditionState(b.invisibleCamouflage, type == InvisibilityNugget::CAMOUFLAGE);
	};
	set(*obj);
	if (isHorde(*obj))
	{
		forEachMember(*obj, [&](Object &m) { set(m); });
	}
}

void InvisibilityManager::playFX(const std::string *fx, const Object &obj, const char *site)
{
	if (!fx || !FXEventLog::isFXName(*fx))
	{
		return;
	}
	// RW 0x4B1B5A doFXObj(fx, obj, null)
	m_logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, m_logic.getFrame(), *fx, obj));
}

// RW 0x81B376
bool InvisibilityManager::applyType(Object *obj, int type, Entry &e, const Outcome &out, unsigned delay)
{
	const int old = invisibilityType(*obj);
	if (type == old)
	{
		return false;
	}
	setType(obj, type);
	if (out.untoggleHidden && old == InvisibilityNugget::STEALTH && type != InvisibilityNugget::STEALTH && obj->testStatus((unsigned)bits().hidden))
	{
		if (ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*obj)) // RW 0x81B3D3 .. 0x81B410 (lane STEALTH-2)
		{
			th->unhide(); // slot 0x5C
		}
	}
	if (type == InvisibilityNugget::NONE)
	{
		if (old == InvisibilityNugget::STEALTH)
		{
			playFX(out.exitFX, *obj, "ExitStealthFX");
		}
		reveal(obj, e, delay, out.byFriend); // RW 0x81AF68
		return true;
	}
	if (old == InvisibilityNugget::NONE)
	{
		if (type == InvisibilityNugget::STEALTH)
		{
			playFX(out.becomeFX, *obj, "BecomeStealthedFX");
		}
		e.invisibleSince = m_logic.getFrame(); // RW 0x81B476
		++m_becameInvisible;
	}
	return false;
}

// RW 0x81AF68
void InvisibilityManager::reveal(Object *obj, Entry &e, unsigned delay, bool byFriend)
{
	wakeEnemiesThatSee(obj); // RW 0x81AF7D
	const unsigned until = m_logic.getFrame() + delay;
	if (until > e.detectedUntil)
	{
		e.detectedUntil = until;
	}
	const Bits &b = bits();
	obj->setStatus((unsigned)b.invisibleDetected, false);
	obj->setStatus((unsigned)b.invisibleDetectedByFriend, false);
	obj->setStatus((unsigned)(byFriend ? b.invisibleDetectedByFriend : b.invisibleDetected), true);
	++m_reveals;
}

// RW 0x81A6E7: every player that is ENEMIES to the object's player wakes its idle AIs that see the object (RW 0x81A6B5 -> 0x68FA3D(obj, -1) vision test, RW 0x662DB3:
// an idle AI's next mood check becomes now); INFERENCE (S-1040): the vision test is the AI object's VisionRange, 2D centre distance
void InvisibilityManager::wakeEnemiesThatSee(Object *obj)
{
	const Player *op = obj->getControllingPlayer();
	if (!op)
	{
		return;
	}
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *p = o->getControllingPlayer();
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!p || !ai || p->getRelationship(op->getDefaultTeam()) != ENEMIES || !ai->isIdle())
		{
			continue;
		}
		const float range = visionRange(*o);
		const float dx = SimMath::subf32(obj->getPosition()->x, o->getPosition()->x), dy = SimMath::subf32(obj->getPosition()->y, o->getPosition()->y);
		if (range > 0.0f && SimMath::sumSquares2(dx, dy) <= SimMath::mulf32(range, range))
		{
			ai->wakeUpNow();
		}
	}
}

// RW 0x81B054
void InvisibilityManager::clearDetected(Object *obj)
{
	const Bits &b = bits();
	obj->setStatus((unsigned)b.invisibleDetectedByFriend, false);
	obj->setStatus((unsigned)b.invisibleDetected, false);
}

void InvisibilityManager::crc(StateHasher &h) const
{
	h.addU32(m_nextUpdate);
	h.addFloat(m_maxMultiplier);
	h.addU32((std::uint32_t)m_entries.size());
	for (const auto &kv : m_entries)
	{
		h.addU32(kv.first);
		h.addU32(kv.second.notBefore);
		h.addU32(kv.second.detectedUntil);
		h.addU32(kv.second.invisibleSince);
		h.addU32((std::uint32_t)kv.second.applied.size());
		for (const Applied &a : kv.second.applied)
		{
			a.nugget->crc(h);
			h.addU32(a.startFrame);
			h.addU32(a.frames);
			h.addBool(a.active);
		}
	}
	h.addU32((std::uint32_t)m_trees.size());
	for (const Coord3D &t : m_trees)
	{
		h.addFloat(t.x);
		h.addFloat(t.y);
		h.addFloat(t.z);
	}
	h.addU32(m_reveals);
	h.addU32(m_becameInvisible);
}

std::vector<std::string> InvisibilityManager::stopLines()
{
	return { "[S-1040] invisibility (lane STEALTH-1): the InvisibilityManager (TheGameLogic + 0x178) runs as RW 0x81BE85 / 0x81C217 / 0x81C32C do; not ported: the camouflage "
		"detector (RW 0x81B758, with the +0x10 multiplier) and TREE (RW 0x81AC56) queries run on ThePartitionManager (lanes MODULES-2 / STEALTH-2) with the same-map "
		"filter always passing, the terrain tree list holds the map's TREE map objects (no toppling), the client notification of a reveal (RW 0x81ACD7) and the "
		"opacity pulse (RW 0x81AA85) are the client's, the reveal wakes idle enemy AIs by VisionRange (RW 0x68FA3D's vision test is not read), TAKING_DAMAGE reads the "
		"last damaging hit frame only, the script's stealth switch (Object + 0x457 bit 8) is absent, and RW "
		"0x694C0D's draw module slot 0xF0 is read as AffectedByStealth",
		"[S-1042] stealth look (lane STEALTH-1, client): an enemy's invisible object is not drawn or picked; a friend's pulses between GameData InvisibilityOpacityMin "
		"and Max in client time with an id-based phase (RW 0x6760F9's client random phase and per-frame step are not read); the detected look is drawn as normal" };
}

std::vector<std::string> InvisibilityManager::report() const
{
	std::vector<std::string> r = stopLines();
	for (const std::string &u : InvisibilityModules::stopLines())
	{
		r.push_back(u);
	}
	r.push_back("[S-1040] invisibility: " + std::to_string(m_entries.size()) + " objects tracked, " + std::to_string(m_trees.size()) + " terrain trees, " +
		std::to_string(m_becameInvisible) + " became invisible, " + std::to_string(m_reveals) + " reveals");
	return r;
}
