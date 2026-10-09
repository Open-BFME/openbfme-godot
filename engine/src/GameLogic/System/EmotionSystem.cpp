// OpenBFME. GPL-3.0.
// See GameLogic/System/EmotionSystem.h for the target facts and the stop S-1021.

#include "GameLogic/System/EmotionSystem.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponNugget.h"

#include <algorithm>
#include <cstddef>

thread_local EmotionSystem *TheEmotionSystem = nullptr;

namespace
{
// RW 0xD9F9EC
const char *const kEmotionAIStateNames[] = { "BACK_AWAY", "AVOID_SCARER", "IDLE", "RUN_AWAY_PANIC", "FACE_OBJECT", "QUARREL", nullptr };

int lookupName(const char *const *names, const char *token)
{
	for (int i = 0; names[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(names[i], token) == 0) // _strcmpi
		{
			return i;
		}
	}
	return -1;
}

#define EN_OFF(field) (int)offsetof(EmotionNuggetTemplate, field)

// RW 0x8E0CA9
void parseCopyFrom(INI *ini, void *instance, void *, const void *)
{
	const char *name = ini->getNextTokenOrNull();
	if (!name)
	{
		throw INIException(3, "Name of emotion nugget to copy data from expected.");
	}
	const EmotionNuggetTemplate *from = TheEmotionSystem ? TheEmotionSystem->find(name) : nullptr;
	if (!from)
	{
		throw INIException(3, "Emotion nugget to copy from not found.");
	}
	static_cast<EmotionNuggetTemplate *>(instance)->copyFrom(*from);
}

// RW 0x8E09CE
void parseType(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextTokenOrNull();
	if (!t)
	{
		throw INIException(3, "Emotion type expected.");
	}
	*static_cast<int *>(store) = lookupName(TheEmotionTypeNames, t);
}

// RW 0x8E0A11
void parseAIState(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextTokenOrNull();
	if (!t)
	{
		throw INIException(3, "Emotion AI type expected.");
	}
	*static_cast<int *>(store) = lookupName(kEmotionAIStateNames, t);
}

// RW 0x8E0906: (-1, value)
void parseThreatAbove(INI *ini, void *instance, void *store, const void *)
{
	unsigned v = 0;
	INI::parseUnsignedInt(ini, instance, &v, nullptr); // RW 0x42ECB2
	int *pair = static_cast<int *>(store);
	pair[0] = -1;
	pair[1] = (int)v;
}

// RW 0x8E08DC: (1, -value)
void parseThreatBelow(INI *ini, void *instance, void *store, const void *)
{
	unsigned v = 0;
	INI::parseUnsignedInt(ini, instance, &v, nullptr);
	int *pair = static_cast<int *>(store);
	pair[0] = 1;
	pair[1] = -(int)v;
}

// RW 0x8E092B: ModelConditions / ModelConditionsClear, the flags copied 0x4C bytes further (ClearOnExit / SetOnExit)
void parseConditionsWithExit(INI *ini, void *, void *store, const void *)
{
	std::uint32_t *words = static_cast<std::uint32_t *>(store);
	ParseBitFlags(ini, words, 19, TheModelConditionNames); // RW 0x4B8C21
}

void parseConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 19, TheModelConditionNames); // RW 0x4B8C21
}

// RW 0x73A302: "None" (any case) stores none; any other name must be an FXList of TheFXListStore
void parseFX(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	*static_cast<std::string *>(store) = name;
}

// the ModelConditions rows: the copy to the exit flags happens after the row (RW 0x8E093D .. 0x8E0949)
void parseModelConditions(INI *ini, void *instance, void *, const void *)
{
	EmotionNuggetTemplate *n = static_cast<EmotionNuggetTemplate *>(instance);
	parseConditionsWithExit(ini, instance, n->modelConditions.data(), nullptr);
	n->modelConditionsClearOnExit = n->modelConditions;
}

void parseModelConditionsClear(INI *ini, void *instance, void *, const void *)
{
	EmotionNuggetTemplate *n = static_cast<EmotionNuggetTemplate *>(instance);
	parseConditionsWithExit(ini, instance, n->modelConditionsClear.data(), nullptr);
	n->modelConditionsSetOnExit = n->modelConditionsClear;
}

// RW 0xC77FD0
const FieldParse kNuggetParse[] = {
	{ "CopyFrom", parseCopyFrom, nullptr, 0 },
	{ "Type", parseType, nullptr, EN_OFF(type) },
	{ "IgnoreIfUnitIdle", INI::parseBool, nullptr, EN_OFF(ignoreIfUnitIdle) },
	{ "IgnoreIfUnitBusy", INI::parseBool, nullptr, EN_OFF(ignoreIfUnitBusy) },
	{ "Duration", INI::parseDurationUnsignedInt, nullptr, EN_OFF(duration) },
	{ "InactiveDuration", INI::parseDurationUnsignedInt, nullptr, EN_OFF(inactiveDuration) },
	{ "InactiveDurationSameObject", INI::parseDurationUnsignedInt, nullptr, EN_OFF(inactiveDurationSameObject) },
	{ "InactiveDurationSameType", INI::parseDurationUnsignedInt, nullptr, EN_OFF(inactiveDurationSameType) },
	{ "OnlyIfEnemyThreatAbove", parseThreatAbove, nullptr, EN_OFF(enemyThreatScale) },
	{ "OnlyIfEnemyThreatBelow", parseThreatBelow, nullptr, EN_OFF(enemyThreatScale) },
	{ "OnlyIfFriendThreatAbove", parseThreatAbove, nullptr, EN_OFF(friendThreatScale) },
	{ "OnlyIfFriendThreatBelow", parseThreatBelow, nullptr, EN_OFF(friendThreatScale) },
	{ "IgnoreIfAI", INI::parseBool, nullptr, EN_OFF(ignoreIfAI) },
	{ "IgnoreIfHuman", INI::parseBool, nullptr, EN_OFF(ignoreIfHuman) },
	{ "StartFXList", parseFX, nullptr, EN_OFF(startFX) },
	{ "UpdateFXList", parseFX, nullptr, EN_OFF(updateFX) },
	{ "EndFXList", parseFX, nullptr, EN_OFF(endFX) },
	{ "AttributeModifier", INI::parseAsciiString, nullptr, EN_OFF(attributeModifier) },
	{ "AttributeStartDelay", INI::parseDurationUnsignedInt, nullptr, EN_OFF(attributeStartDelay) },
	{ "AttributeModifierWhileEmotionActive", INI::parseBool, nullptr, EN_OFF(attributeModifierWhileActive) },
	{ "AttributeDuration", INI::parseDurationUnsignedInt, nullptr, EN_OFF(attributeDuration) },
	{ "AIState", parseAIState, nullptr, EN_OFF(aiState) },
	{ "AILockDuration", INI::parseDurationUnsignedInt, nullptr, EN_OFF(aiLockDuration) },
	{ "ModelConditions", parseModelConditions, nullptr, 0 },
	{ "ModelConditionsClear", parseModelConditionsClear, nullptr, 0 },
	{ "ModelConditionsSetOnExit", parseConditions, nullptr, EN_OFF(modelConditionsSetOnExit) },
	{ "ModelConditionsClearOnExit", parseConditions, nullptr, EN_OFF(modelConditionsClearOnExit) },
	{ "PreventPlayerCommands", INI::parseBool, nullptr, EN_OFF(preventPlayerCommands) },
	{ "LuaEvent", INI::parseAsciiString, nullptr, EN_OFF(luaEvent) },
	{ nullptr, nullptr, nullptr, 0 }
};

const char *const kStopNuggets =
	"[S-1021] emotions: the emotion nuggets run (RW 0x8E12F5 canApply, 0x8E0ED7 start, 0x8E11A1 update, 0x8E168A stop: durations, inactivity, threat, the model "
	"conditions on the object or a horde's members, the FX calls, the attribute modifier, EMOTION_LOOK_TO_SKY, the AIState through the AI's temporary state and "
	"PreventPlayerCommands: lane MODULES-3, S-1027); not ported, counted where retail acts: the LuaEvent dispatch (RW 0x735F53); the FX object of a horde (horde "
	"interface slot 0x4C) is the horde itself; a horde's fear resistance is read on its first member (slot 0x110, as HERO-1)";
} // namespace

// ---- templates --------------------------------------------------------------------------------------------------------------------------------------------------

void EmotionNuggetTemplate::copyFrom(const EmotionNuggetTemplate &o)
{
	// RW 0x8E0A54: + 4 .. + 0x188, the name and + 0x18C / + 0x18D stay
	const std::string keepName = name;
	const bool keepOverridden = overridden;
	*this = o;
	name = keepName;
	overridden = keepOverridden;
}

const FieldParse *EmotionNuggetTemplate::fieldParse()
{
	return kNuggetParse;
}

void EmotionSystem::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("EmotionNugget", [](INI *ini) {
		if (!TheEmotionSystem)
		{
			throw INIException(8, "EmotionNugget block: TheEmotionSystem is not installed");
		}
		TheEmotionSystem->parseEmotionNugget(ini);
	});
}

void EmotionSystem::parseEmotionNugget(INI *ini)
{
	// RW 0x8E0D59
	const std::string name = ini->getNextToken();
	auto it = std::find_if(m_nuggets.begin(), m_nuggets.end(), [&](const std::unique_ptr<EmotionNuggetTemplate> &n) { return n->name == name; });
	if (it != m_nuggets.end() && ini->getLoadType() != INI_LOAD_RELOAD)
	{
		// the first definition stays: the fields are parsed into a temporary that is dropped
		EmotionNuggetTemplate scratch;
		scratch.name = name;
		ini->initFromINI(&scratch, kNuggetParse);
		return;
	}
	if (it != m_nuggets.end())
	{
		m_nuggets.erase(it); // RW 0x835D8E (a reload)
	}
	auto n = std::make_unique<EmotionNuggetTemplate>();
	n->name = name;
	ini->initFromINI(n.get(), kNuggetParse);
	m_nuggets.push_back(std::move(n)); // RW 0x835CFC
}

const EmotionNuggetTemplate *EmotionSystem::find(const std::string &name) const
{
	for (const std::unique_ptr<EmotionNuggetTemplate> &n : m_nuggets)
	{
		if (n->name == name)
		{
			return n.get();
		}
	}
	return nullptr;
}

// ---- the per-object nugget --------------------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> EmotionNugget::stopLines()
{
	return { kStopNuggets };
}

namespace
{
int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

int conditionBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

// the AI's current state answers "busy" for the emotion (RW AI vslot 0x16C -> the state's slot 0x38): the states whose slot is RW 0x8BD372 (true) in the RW
// machine constructor RW 0x753755; the port defines none of them yet
bool stateSlot14(unsigned id)
{
	switch (id)
	{
		case 2: case 3: case 4: case 5: case 17: case 24: case 33: case 34: case 35: case 39: case 54: case 61: case 73: case 74:
			return true;
		default:
			return false;
	}
}

EmotionWorld &emotionsOf(Object &obj)
{
	return obj.logic().emotions();
}
} // namespace

bool EmotionNugget::canApply(int enemyThreat, unsigned friendThreat, Object *source)
{
	// RW 0x8E12F5
	Object &obj = *m_object;
	static const int kSpecialAbility = statusBit("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING"); // RW 0x8E1304: status 0x46
	if (kSpecialAbility >= 0 && obj.testStatus((unsigned)kSpecialAbility))
	{
		return false;
	}
	const EmotionNuggetTemplate &t = *m_template;
	if (t.ignoreIfUnitIdle || t.ignoreIfUnitBusy)
	{
		AIUpdateInterface *ai = obj.getAIUpdateInterface();
		if (!ai)
		{
			return false;
		}
		// idle: AI vslot 0x1B8 (isIdle) and not vslot 0x16C (the current state's slot 0x38)
		const bool idle = ai->isIdle() && !stateSlot14(ai->currentStateId());
		if (idle ? t.ignoreIfUnitIdle : t.ignoreIfUnitBusy)
		{
			return false;
		}
	}
	if (t.ignoreIfAI || t.ignoreIfHuman)
	{
		// RW 0x68B68A: the controlling player is human
		const Player *p = obj.getControllingPlayer();
		const bool human = p && p->getPlayerType() == PLAYER_HUMAN;
		if (human ? t.ignoreIfHuman : t.ignoreIfAI)
		{
			return false;
		}
	}
	// RW 0x8E1369: (scale * threat + offset) >= 0 rejects (the friend product is unsigned arithmetic on the low word)
	if (-1 < (int)((unsigned)t.enemyThreatScale * (unsigned)enemyThreat + (unsigned)t.enemyThreatOffset))
	{
		return false;
	}
	if (-1 < (int)((unsigned)t.friendThreatScale * friendThreat + (unsigned)t.friendThreatOffset))
	{
		return false;
	}
	const UnsignedInt now = obj.logic().getFrame();
	if (now < m_inactiveUntil)
	{
		return false;
	}
	if (source)
	{
		// RW 0x8E139A .. 0x8E13FA: the per-source and per-source-template inactivity (an expired entry is erased)
		auto o = m_sameObject.find(source->getID());
		if (o != m_sameObject.end())
		{
			if (now < o->second)
			{
				return false;
			}
			m_sameObject.erase(o);
		}
		const unsigned short tid = static_cast<const ThingTemplate *>(source->getTemplate())->getTemplateID(); // + 0x5E8
		auto ty = m_sameType.find(tid);
		if (ty != m_sameType.end())
		{
			if (now < ty->second)
			{
				return false;
			}
			m_sameType.erase(ty);
		}
	}
	// RW 0x8E13FC: FEAR .. DOOM: the object (a horde: its first member) resists with the attribute 4 (TERROR: 5) at 0.5 or more, or runs the rampage state 0x2D
	if (t.type > 3 && t.type < 8)
	{
		Object *who = &obj;
		static const int kHorde = kindBit("HORDE");
		if (kHorde >= 0 && obj.isKindOf((unsigned)kHorde))
		{
			ContainModuleInterface *c = obj.getContain();
			const ContainModuleInterface::ContainedItemsList *items = c ? c->getContainedItemsList() : nullptr;
			who = items && !items->empty() ? items->front() : nullptr;
		}
		if (who)
		{
			bool resists = false;
			float value = 0.0f;
			if (who->attributeModifierSum(t.type == EMOTION_TERROR ? 5 : 4, nullptr, value))
			{
				resists = !(value < 0.5f);
			}
			if (AIUpdateInterface *ai = who->getAIUpdateInterface())
			{
				if (ai->currentStateId() == 0x2D)
				{
					resists = true;
				}
			}
			if (resists)
			{
				return false;
			}
		}
	}
	return true;
}

void EmotionNugget::applyConditions(const EmotionNuggetTemplate::Flags &clear, const EmotionNuggetTemplate::Flags &set)
{
	// RW 0x694BF8: the horde of the object (RW 0x693A1A) and its horde interface: RW 0x6944A5 changes every member in list order, then the horde; else RW 0x68D607
	Object *horde = m_object->getHordeObject(false);
	ContainModuleInterface *c = horde ? horde->getContain() : nullptr;
	if (c && c->getHordeContainInterface())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			const std::vector<Object *> members(items->begin(), items->end());
			for (Object *m : members)
			{
				m->clearAndSetModelConditionFlags(clear, set);
			}
		}
		horde->clearAndSetModelConditionFlags(clear, set);
		return;
	}
	m_object->clearAndSetModelConditionFlags(clear, set);
}

Object *EmotionNugget::fxObject() const
{
	return m_object; // INFERENCE (S-1021): a horde's interface slot 0x4C is not identified
}

void EmotionNugget::emitFX(const std::string &fx, const char *site, Object *other)
{
	if (fx.empty())
	{
		return;
	}
	GameLogic &logic = m_object->logic();
	FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, *fxObject());
	e.secondary = other ? other->getID() : INVALID_ID;
	logic.fxEvents().emit(e);
}

void EmotionNugget::start(Object *source, int lockFrames)
{
	// RW 0x8E0ED7
	Object &obj = *m_object;
	GameLogic &logic = obj.logic();
	const UnsignedInt now = logic.getFrame();
	m_startFrame = now;
	m_sourceID = source ? source->getID() : INVALID_ID;
	m_sourceTemplateID = source ? static_cast<const ThingTemplate *>(source->getTemplate())->getTemplateID() : 0;
	if (lockFrames < 1)
	{
		m_endFrame = m_template->duration == 0 ? 0 : now + m_template->duration;
	}
	else
	{
		m_endFrame = now + (UnsignedInt)lockFrames;
	}
	emitFX(m_template->startFX, "EmotionStartFX", source); // RW 0x8E0F68
	if (AIUpdateInterface *aiu = obj.getAIUpdateInterface())
	{
		const int ai = m_template->aiState;
		if (ai == 0 || (ai > 1 && ai < 6))
		{
			// lane MODULES-3: RW 0x8E0F98 -> 0x662FC8 (the AI's temporary state), then PreventPlayerCommands sets AI + 0x3C5 (RW 0x8E0FA9)
			aiu->emotionEnterAIState(ai, source);
			if (m_template->preventPlayerCommands)
			{
				aiu->setPreventPlayerCommands(true);
			}
		}
		applyConditions(m_template->modelConditionsClear, m_template->modelConditions); // RW 0x8E0FB0 .. 0x8E0FDC
		if (source)
		{
			// RW 0x70C55D: a source above the gravity height (Thing + 0x54 > GlobalData Gravity * -9, KindOf CAN_CLIMB_WALLS / SHIP excepted) sets EMOTION_LOOK_TO_SKY
			static const int kClimb = kindBit("CAN_CLIMB_WALLS"), kShip = kindBit("SHIP");
			const bool excluded = (kClimb >= 0 && source->isKindOf((unsigned)kClimb)) || (kShip >= 0 && source->isKindOf((unsigned)kShip));
			if (!excluded)
			{
				const Coord3D *p = source->getPosition();
				const float height = SimMath::pc24Add(SimMath::pc24Sub(p->z, logic.getGroundHeight(p->x, p->y)), 0.0f);
				const float threshold = SimMath::pc24Mul(logic.settings().gravity, -9.0f);
				static const int kLookToSky = conditionBit("EMOTION_LOOK_TO_SKY");
				if (height > threshold && kLookToSky >= 0 && !obj.testModelCondition(kLookToSky))
				{
					obj.setModelConditionState(kLookToSky, true);
				}
			}
		}
		if (!m_template->luaEvent.empty())
		{
			emotionsOf(obj).countUnportedLua(); // RW 0x735F53 (S-1021)
		}
	}
}

bool EmotionNugget::update()
{
	// RW 0x8E11A1
	Object &obj = *m_object;
	const UnsignedInt now = obj.logic().getFrame();
	bool done = m_endFrame != 0 && m_endFrame <= now;
	if (!m_template->attributeModifier.empty() && now == m_startFrame + m_template->attributeStartDelay)
	{
		// RW 0x68F1A8: the list for AttributeDuration, or until the stop (0 duration) while the emotion lasts
		obj.addAttributeModifier(m_template->attributeModifier, m_template->attributeModifierWhileActive ? 0 : m_template->attributeDuration);
	}
	if (m_template->aiLockDuration != 0 && m_startFrame + m_template->aiLockDuration <= now && obj.getAIUpdateInterface())
	{
		obj.getAIUpdateInterface()->emotionLockElapsed(); // RW 0x663053 (lane MODULES-3)
	}
	if (!m_template->updateFX.empty())
	{
		Object *src = obj.logic().findObjectByID(m_sourceID);
		if (src || m_sourceID == INVALID_ID)
		{
			emitFX(m_template->updateFX, "EmotionUpdateFX", src);
		}
	}
	if (m_template->type == EMOTION_POINT)
	{
		Object *src = obj.logic().findObjectByID(m_sourceID);
		if (!src || src->isEffectivelyDead())
		{
			done = true;
		}
	}
	return !done;
}

void EmotionNugget::stop()
{
	// RW 0x8E168A
	Object &obj = *m_object;
	GameLogic &logic = obj.logic();
	const UnsignedInt now = logic.getFrame();
	if (m_template->inactiveDuration != 0)
	{
		m_inactiveUntil = m_template->inactiveDuration + now;
	}
	if (m_template->inactiveDurationSameObject != 0 && m_sourceID != INVALID_ID)
	{
		m_sameObject[m_sourceID] = m_template->inactiveDurationSameObject + now;
	}
	if (m_template->inactiveDurationSameType != 0 && m_sourceTemplateID != 0)
	{
		m_sameType[m_sourceTemplateID] = m_template->inactiveDurationSameType + now;
	}
	if (!m_template->endFX.empty())
	{
		Object *src = logic.findObjectByID(m_sourceID);
		if (src || m_sourceID == INVALID_ID)
		{
			emitFX(m_template->endFX, "EmotionEndFX", src);
		}
	}
	if (AIUpdateInterface *aiu = obj.getAIUpdateInterface())
	{
		const int ai = m_template->aiState;
		if (ai == 0 || (ai > 1 && ai < 6))
		{
			// lane MODULES-3: RW 0x8E177A -> 0x66309C (the temporary state leaves), then PreventPlayerCommands clears AI + 0x3C5 (RW 0x8E178B)
			aiu->emotionLeaveAIState();
			if (m_template->preventPlayerCommands)
			{
				aiu->setPreventPlayerCommands(false);
			}
		}
		applyConditions(m_template->modelConditionsClearOnExit, m_template->modelConditionsSetOnExit); // RW 0x8E1754 .. 0x8E1780
		static const int kLookToSky = conditionBit("EMOTION_LOOK_TO_SKY");
		if (kLookToSky >= 0 && obj.testModelCondition(kLookToSky))
		{
			obj.setModelConditionState(kLookToSky, false); // RW 0x8E178A: + 0x125 bit 0
		}
		// RW 0x8E17A6: the attribute list leaves when it was active for the emotion (while-active and its start delay reached)
		if (!m_template->attributeModifier.empty() && !(now < m_startFrame + m_template->attributeStartDelay) && m_template->attributeModifierWhileActive)
		{
			obj.addAttributeModifier(m_template->attributeModifier, m_template->attributeDuration);
		}
	}
}

void EmotionNugget::crc(StateHasher &h) const
{
	h.addString(m_template->name);
	h.addU32(m_sourceID);
	h.addU32(m_sourceTemplateID);
	h.addU32(m_inactiveUntil);
	h.addU32(m_endFrame);
	h.addU32(m_startFrame);
	h.addU32((std::uint32_t)m_sameObject.size());
	for (const auto &e : m_sameObject)
	{
		h.addU32(e.first);
		h.addU32(e.second);
	}
	h.addU32((std::uint32_t)m_sameType.size());
	for (const auto &e : m_sameType)
	{
		h.addU32(e.first);
		h.addU32(e.second);
	}
}

// ---- EmotionWorld ------------------------------------------------------------------------------------------------------------------------------------------------

void EmotionWorld::objectInitialized(Object &obj)
{
	// RW 0x693E79: KindOf SCARY (template + 0x11A bit 0) or HERO (+ 0x113 bit 2); RW 0x835D55: appended unless present
	static const int kScary = kindBit("SCARY"), kHero = kindBit("HERO");
	if (!((kScary >= 0 && obj.isKindOf((unsigned)kScary)) || (kHero >= 0 && obj.isKindOf((unsigned)kHero))))
	{
		return;
	}
	if (std::find(m_ids.begin(), m_ids.end(), obj.getID()) == m_ids.end())
	{
		m_ids.push_back(obj.getID());
	}
}

void EmotionWorld::objectDied(Object &obj)
{
	// RW 0x835B0A: the last id takes the removed one's place
	static const int kScary = kindBit("SCARY"), kHero = kindBit("HERO");
	if (!((kScary >= 0 && obj.isKindOf((unsigned)kScary)) || (kHero >= 0 && obj.isKindOf((unsigned)kHero))))
	{
		return;
	}
	auto it = std::find(m_ids.begin(), m_ids.end(), obj.getID());
	if (it != m_ids.end())
	{
		*it = m_ids.back();
		m_ids.pop_back();
	}
}

void EmotionWorld::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_ids.size());
	for (ObjectID id : m_ids)
	{
		h.addU32(id);
	}
}

std::vector<std::string> EmotionWorld::report() const
{
	std::vector<std::string> out = EmotionNugget::stopLines();
	if (m_unportedAI || m_unportedLua || m_unportedFlags || m_tauntScans)
	{
		out.push_back("[S-1021] emotions this game: Lua events not sent " + std::to_string(m_unportedLua) +
		              ", taunt / point / alert scans run " + std::to_string(m_tauntScans));
	}
	return out;
}
