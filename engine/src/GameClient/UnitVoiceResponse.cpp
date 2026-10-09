// OpenBFME. GPL-3.0. See UnitVoiceResponse.h.

#include "GameClient/UnitVoiceResponse.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/UnitSpecificSound.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

// Common/ModelState.h cannot be included next to Object.h (two ModelConditionFlags typedefs); the one function used is declared here.
namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
const char *const kRowNames[UnitVoiceResponse::VOICE_ROW_COUNT] = {
	"VoiceSelect", "VoiceSelectUnderConstruction", "VoiceSelectBattle", "VoiceMove", "VoiceMoveToHigherGround", "VoiceMoveOverWalls", "VoiceAttack",
	"VoiceAttackCharge", "VoiceFear", "VoiceCreated", "VoiceTaskComplete", "VoiceDefect", "VoiceAttackAir", "VoiceGuard", "VoiceAlert", "VoiceFullyCreated",
	"VoiceRetreatToCastle", "VoiceMoveToCamp", "VoiceAttackStructure", "VoiceAttackMachine", "VoiceMoveWhileAttacking", "VoiceCombineWithHorde",
	"VoiceEnterStateAttack", "VoiceEnterStateAttackCharge", "VoiceEnterStateAttackAir", "VoiceEnterStateAttackStructure", "VoiceEnterStateAttackMachine",
	"VoiceEnterStateMove", "VoiceEnterStateMoveToHigherGround", "VoiceEnterStateMoveOverWalls", "VoiceEnterStateRetreatToCastle", "VoiceEnterStateMoveToCamp",
	"VoiceEnterStateMoveWhileAttacking"
};

// RW 0x73AB45 applied to the value tokens of a voice line: "NoSound" -> nothing, "EVA:<e>" -> the Eva event only, "+SOUND:<s>" -> the sound (the Eva id
// is the template's carried one), else the audio event
UnitVoiceResponse::Voice parseVoiceValue(const std::string &token, const std::string &carriedEva)
{
	UnitVoiceResponse::Voice v;
	if (token.empty() || AsciiStringUtil::compareNoCase(token, "NoSound") == 0)
	{
		return v;
	}
	if (token.size() >= 4 && AsciiStringUtil::compareNoCase(token.substr(0, 4), "EVA:") == 0)
	{
		v.eva = token.substr(4);
		return v;
	}
	if (token.size() >= 7 && AsciiStringUtil::compareNoCase(token.substr(0, 7), "+SOUND:") == 0)
	{
		v.eva = carriedEva;
		v.sound = token.substr(7);
		return v;
	}
	v.sound = token;
	return v;
}

long long fieldInt(const ThingTemplate &tt, const char *name, long long fallbackWhenUnset)
{
	// an unset field is the template's default (zero-initialised RW template memory: retail's value for a field no INI line sets)
	const FieldValue *f = tt.findField(name);
	if (!f)
	{
		return fallbackWhenUnset;
	}
	if (const long long *v = std::get_if<long long>(f))
	{
		return *v;
	}
	return fallbackWhenUnset;
}

float fieldReal(const ThingTemplate &tt, const char *name)
{
	const FieldValue *f = tt.findField(name);
	if (const float *v = f ? std::get_if<float>(f) : nullptr)
	{
		return *v;
	}
	return 0.0f;
}

bool isEnterState(int msgType)
{
	// RW 0x8DD63C
	return msgType == 0x7E6 || msgType == 0x7E7 || msgType == 0x7EC || msgType == 0x7E8;
}

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

float dist2D(const Coord3D &a, const Coord3D &b)
{
	// client presentation (a voice choice), not simulation state: plain float arithmetic is allowed here (the file is a reviewed sim-audit exclusion)
	const float dx = a.x - b.x, dy = a.y - b.y;
	return dx * dx + dy * dy;
}
} // namespace

const char *UnitVoiceResponse::rowName(int row)
{
	return row >= 0 && row < VOICE_ROW_COUNT ? kRowNames[row] : "";
}

UnitVoiceResponse::UnitVoiceResponse(GameLogic &logic, Sink &sink)
	: m_logic(logic)
	, m_sink(sink)
{
}

UnitVoiceResponse::Voice UnitVoiceResponse::templateVoice(const ThingTemplate &tt, int row)
{
	if (row < 0 || row >= VOICE_ROW_COUNT)
	{
		return Voice();
	}
	// the row's state after every line of the template and the template it was copied from (ThingTemplate::voiceRow, RW 0x73AB45)
	Voice v;
	if (const ThingTemplate::VoiceRowValue *r = tt.voiceRow(kRowNames[row]))
	{
		v.eva = r->eva;
		v.sound = r->sound;
	}
	return v;
}

UnitVoiceResponse::Voice UnitVoiceResponse::unitSpecificVoice(const ThingTemplate &tt, const std::string &name)
{
	// RW 0x73EDD0: tt + 0x388, the UnitSpecificSounds map (GameLogic/UnitSpecificSound.h)
	const std::string value = UnitSpecificSound::rawValue(tt, name);
	return value.empty() ? Voice() : parseVoiceValue(value, std::string());
}

bool UnitVoiceResponse::handles(int msgType)
{
	switch (msgType)
	{
		case MSG_CREATE_SELECTED_GROUP:
		case MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE:
		case MSG_SELECT_TEAM0: case MSG_SELECT_TEAM1: case MSG_SELECT_TEAM2: case MSG_SELECT_TEAM3: case MSG_SELECT_TEAM4:
		case MSG_SELECT_TEAM5: case MSG_SELECT_TEAM6: case MSG_SELECT_TEAM7: case MSG_SELECT_TEAM8: case MSG_SELECT_TEAM9:
		case MSG_CREATE_SELECT_ALL_GROUP:
		case MSG_DO_WEAPON_AT_LOCATION:
		case MSG_DO_WEAPON_AT_OBJECT:
		case MSG_DO_ATTACK_OBJECT:
		case MSG_DO_FORCE_ATTACK_OBJECT:
		case MSG_DO_FORCE_ATTACK_GROUND:
		case MSG_GET_REPAIRED:
		case MSG_GET_HEALED:
		case MSG_DO_MOVETO:
		case MSG_DO_ATTACKMOVETO:
		case MSG_DO_FORCEMOVETO:
		case MSG_DO_SALVAGE:
		case MSG_DO_MOVETO_FORMATION:
		case MSG_DO_GUARD_POSITION:
		case MSG_DO_GUARD_OBJECT:
		case MSG_FOUNDATION_CONSTRUCT:
		case MSG_DOZER_CONSTRUCT:
		case MSG_RESUME_CONSTRUCTION:
		case MSG_EVACUATE:
		case MSG_EVACUATE_CONTESTERS:
		case MSG_COMBATDROP_AT_LOCATION:
		case MSG_COMBATDROP_AT_OBJECT:
		case MSG_DO_REPAIR:
		case MSG_ENTER:
		case MSG_DOCK:
		case MSG_HARVEST:
		case VOICE_EVENT_CREATED:
		case VOICE_EVENT_TASK_COMPLETE:
		case VOICE_EVENT_DEFECT:
		case VOICE_EVENT_FEAR:
		case VOICE_EVENT_ALERT:
		case VOICE_EVENT_FULLY_CREATED:
		case VOICE_EVENT_ENTER_STATE_ATTACK:
		case VOICE_EVENT_ENTER_STATE_MOVE:
		case VOICE_EVENT_ENTER_STATE_ATTACKMOVE:
			return true;
		default:
			return false;
	}
}

bool UnitVoiceResponse::rankLess(const Rank &a, const Rank &b)
{
	// RW 0x8DD663
	if (a.special != b.special)
	{
		return a.special < b.special;
	}
	if (a.hero)
	{
		if (!b.hero)
		{
			return false;
		}
		if (a.level != b.level)
		{
			return a.level < b.level;
		}
		return a.ordinal > b.ordinal;
	}
	if (b.hero)
	{
		return true;
	}
	return a.priority < b.priority;
}

bool UnitVoiceResponse::fill(Choice &c, const Voice &v) const
{
	// RW 0x8DDA33: only an empty slot takes a voice
	if (!done(c))
	{
		c.voice = v;
	}
	return c.voice.any();
}

bool UnitVoiceResponse::done(const Choice &c)
{
	// RW 0x8DD876
	if (!c.voice.eva.empty())
	{
		return true;
	}
	if (c.voice.sound.empty())
	{
		return false;
	}
	return !c.crowdEntry || c.crowd.any();
}

static UnitVoiceResponse::Voice crowdVoice(const UnitVoiceResponse::CrowdThreshold *entry, const std::string &key)
{
	if (!entry)
	{
		return UnitVoiceResponse::Voice();
	}
	const auto it = entry->voices.find(key);
	return it == entry->voices.end() ? UnitVoiceResponse::Voice() : parseVoiceValue(it->second, std::string());
}

bool UnitVoiceResponse::fillRow(Choice &c, const ThingTemplate &tt, int row) const
{
	// RW 0x8DDA66 -> 0x8DDA33: slot 0 from the template row, slot 1 from the crowd entry's same row (RW 0x8DDA06: entry + row * 8)
	if (!c.voice.any())
	{
		c.voice = templateVoice(tt, row);
	}
	if (c.crowdEntry && !c.crowd.any())
	{
		c.crowd = crowdVoice(c.crowdEntry, rowName(row));
	}
	return c.voice.any();
}

bool UnitVoiceResponse::fillName(Choice &c, const ThingTemplate &tt, const std::string &name) const
{
	// RW 0x8DDB2A: an empty name answers nothing; slot 1 from the crowd entry's UnitSpecificSounds (RW 0x825D3A: entry + 0x1C0)
	if (name.empty())
	{
		return c.voice.any();
	}
	if (!c.voice.any())
	{
		c.voice = unitSpecificVoice(tt, name);
	}
	if (c.crowdEntry && !c.crowd.any())
	{
		c.crowd = crowdVoice(c.crowdEntry, name);
	}
	return c.voice.any();
}

const UnitVoiceResponse::CrowdThreshold *UnitVoiceResponse::pickCrowd(const std::vector<std::pair<Object *, int>> &candidates) const
{
	// RW 0x8DEF63 .. 0x8DF003: every candidate whose template has a CrowdResponseKey (tt + 0x4AC) adds the response's Weight to its total; the largest
	// total wins (a later one only with a larger total); RW 0x825C45 then takes the Threshold entry with the largest count <= the number of units
	// (INFERENCE S-700: the count is the number of candidates with that key, as CrowdResponse.ini documents)
	std::map<std::string, std::pair<int, int>> votes; // name -> weight total, units
	std::vector<std::string> order;
	for (const auto &cand : candidates)
	{
		const FieldValue *f = cand.first->getTemplate()->findField("CrowdResponseKey");
		std::string key;
		if (const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr)
		{
			key = raw->tokens.empty() ? std::string() : raw->tokens.front();
		}
		else if (const std::string *str = f ? std::get_if<std::string>(f) : nullptr)
		{
			key = *str;
		}
		if (key.empty())
		{
			continue;
		}
		const auto cr = m_crowd.find(key);
		if (cr == m_crowd.end())
		{
			++const_cast<Stats &>(m_stats).crowdResponseSkipped;
			continue;
		}
		if (!votes.count(key))
		{
			order.push_back(key);
		}
		votes[key].first += cr->second.weight;
		votes[key].second += 1;
	}
	const CrowdResponse *best = nullptr;
	int bestTotal = 0, units = 0;
	for (const std::string &k : order)
	{
		if (votes[k].first > bestTotal)
		{
			bestTotal = votes[k].first;
			best = &m_crowd.at(k);
			units = votes[k].second;
		}
	}
	if (!best)
	{
		return nullptr;
	}
	const CrowdThreshold *entry = nullptr;
	for (const CrowdThreshold &t : best->thresholds)
	{
		if (t.threshold <= units && (!entry || t.threshold > entry->threshold))
		{
			entry = &t;
		}
	}
	return entry;
}

bool UnitVoiceResponse::parseCrowdResponses(const std::string &text, std::string *error)
{
	// CrowdResponse <Name> / Threshold <Count> / <voice row> = <value> / UnitSpecificSounds ... End / End / Weight = ## / End (CrowdResponse.ini's syntax
	// comment; the binary's parser was not read: S-700)
	std::istringstream in(text);
	std::string line;
	CrowdResponse *cur = nullptr;
	CrowdThreshold *thr = nullptr;
	bool inUnitSpecific = false;
	int lineNo = 0;
	auto fail = [&](const std::string &m) {
		if (error)
		{
			*error = "CrowdResponse.ini line " + std::to_string(lineNo) + ": " + m;
		}
		return false;
	};
	while (std::getline(in, line))
	{
		++lineNo;
		line = line.substr(0, line.find(';'));
		for (char &ch : line)
		{
			if (ch == '=' || ch == '\t' || ch == '\r')
			{
				ch = ' ';
			}
		}
		std::istringstream ls(line);
		std::string a, b;
		ls >> a >> b;
		if (a.empty())
		{
			continue;
		}
		if (!cur)
		{
			if (a != "CrowdResponse" || b.empty())
			{
				return fail("expected CrowdResponse <Name>");
			}
			cur = &m_crowd[b];
			*cur = CrowdResponse();
			cur->name = b;
			continue;
		}
		if (AsciiStringUtil::compareNoCase(a, "End") == 0)
		{
			if (inUnitSpecific)
			{
				inUnitSpecific = false;
			}
			else if (thr)
			{
				thr = nullptr;
			}
			else
			{
				cur = nullptr;
			}
			continue;
		}
		if (!thr)
		{
			if (a == "Threshold")
			{
				cur->thresholds.push_back(CrowdThreshold());
				cur->thresholds.back().threshold = std::atoi(b.c_str());
				thr = &cur->thresholds.back();
			}
			else if (a == "Weight")
			{
				cur->weight = std::atoi(b.c_str());
			}
			else
			{
				return fail("unknown CrowdResponse field '" + a + "'");
			}
			continue;
		}
		if (a == "UnitSpecificSounds")
		{
			inUnitSpecific = true;
			continue;
		}
		if (b.empty())
		{
			return fail("'" + a + "' has no value");
		}
		thr->voices[a] = b;
	}
	if (cur)
	{
		return fail("missing End");
	}
	return true;
}

bool UnitVoiceResponse::inBattle(const Object &obj) const
{
	// RW 0x8DD7DC: the object's model condition ENGAGED (obj + 0x11C bit 29 = bit 157 of the flags at obj + 0x10C) and an enemy the partition finds within the
	// object's vision range (RW 0x68E43B -> 0x701443 with filter 0x6E). The partition query is INFERENCE (S-702): any living object of a player the object's
	// team regards as ENEMIES within the template's VisionRange (2D).
	static const int engaged = ModelCondition::indexOf("ENGAGED");
	if (engaged < 0 || !obj.testModelCondition(engaged))
	{
		return false;
	}
	const float range = fieldReal(*obj.getTemplate(), "VisionRange");
	const float range2 = range * range;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o == &obj || o->isDestroyed() || o->getContainedBy() == &obj)
		{
			continue;
		}
		if (obj.getRelationship(*o) != ENEMIES)
		{
			continue;
		}
		if (dist2D(*o->getPosition(), *obj.getPosition()) <= range2)
		{
			return true;
		}
	}
	return false;
}

void UnitVoiceResponse::selectVoices(Object &obj, int msgType, const Info *, Choice &c, Rank &) const
{
	// RW 0x8DF8A2 .. 0x8DF92F
	const ThingTemplate &tt = *obj.getTemplate();
	if (msgType == MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE)
	{
		fillName(c, tt, "VoiceSelectIdleWorker");
	}
	if (!done(c) && inBattle(obj))
	{
		fillRow(c, tt, VOICE_SELECT_BATTLE);
	}
	if (!done(c) && obj.testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
	{
		fillRow(c, tt, VOICE_SELECT_UNDER_CONSTRUCTION);
	}
	if (!done(c))
	{
		fillRow(c, tt, VOICE_SELECT);
	}
}

void UnitVoiceResponse::moveVoices(Object &obj, int msgType, const Info *info, Choice &c, Rank &rank) const
{
	// RW 0x8DE838
	const ThingTemplate &tt = *obj.getTemplate();
	const bool enterState = isEnterState(msgType);
	const int base = enterState ? 27 : VOICE_MOVE;
	const int whileAttacking = enterState ? 32 : VOICE_MOVE_WHILE_ATTACKING;
	const int higherGround = enterState ? 28 : VOICE_MOVE_TO_HIGHER_GROUND;
	// the planning mode branch (player + 0x770 == 2: MiscAudio PlanningModeOrderGiven), VoiceCrush (GameData + 0x8B9 and a crushable target), the busy gate
	// (GameData + 0x8B0), the castle / camp rules (RW 0x8DDBC4, 0x8DD6F3 campness) and VoiceMoveOverWalls (RW 0x8DE628) are not ported: stop S-704
	if (!enterState && msgType == MSG_DO_SALVAGE)
	{
		if (fillName(c, tt, "VoiceSalvage"))
		{
			rank.special = 2;
		}
	}
	if (rank.special > 0)
	{
		return; // RW 0x8DEA60: a special voice ends the rule list
	}
	const bool hasPosition = info && info->hasPosition;
	// VoiceMoveToHigherGround: the template threshold (tt + 0x534) above 0 and the destination that much higher than the object
	if (!done(c) && hasPosition)
	{
		const float minZ = fieldReal(tt, "MinZIncreaseForVoiceMoveToHigherGround");
		if (minZ > 0.0f && minZ < info->position.z - obj.getPosition()->z)
		{
			fillRow(c, tt, higherGround);
		}
	}
	// VoiceMoveWhileAttacking: when the row exists and the object is in battle (RW 0x8DEC2A)
	if (!done(c))
	{
		if (templateVoice(tt, whileAttacking).any() && inBattle(obj))
		{
			fillRow(c, tt, whileAttacking);
		}
	}
	if (!done(c))
	{
		fillRow(c, tt, base);
	}
}

void UnitVoiceResponse::attackVoices(Object &obj, int msgType, const Info *info, Choice &c) const
{
	// RW 0x8DDF3B
	const ThingTemplate &tt = *obj.getTemplate();
	const bool enterState = isEnterState(msgType);
	const int base = enterState ? 22 : VOICE_ATTACK;
	const int charge = enterState ? 23 : VOICE_ATTACK_CHARGE;
	const int air = enterState ? 24 : VOICE_ATTACK_AIR;
	const int structure = enterState ? 25 : VOICE_ATTACK_STRUCTURE;
	const int machine = enterState ? 26 : VOICE_ATTACK_MACHINE;
	c.charge = true;
	if (msgType == MSG_DO_FORCE_ATTACK_GROUND)
	{
		fillName(c, tt, "VoiceBombard");
	}
	const Object *target = info && info->target != INVALID_ID ? m_logic.findObjectByID(info->target) : nullptr;
	if (!done(c) && target && target->getTemplate())
	{
		const ThingTemplate &ttt = *target->getTemplate();
		fillName(c, tt, std::string(enterState ? "VoiceEnterStateAttackUnit" : "VoiceAttackUnit") + ttt.getName());
		if (!done(c))
		{
			const int structureBit = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
			const int machineBit = ObjectTemplateInfoBuilder::kindOfIndex("MACHINE");
			if (target->isKindOf((unsigned)structureBit))
			{
				fillRow(c, tt, structure);
			}
			else if (target->isKindOf((unsigned)machineBit))
			{
				fillRow(c, tt, machine);
			}
		}
	}
	// the current weapon's VoiceFire (weapon template + 0xD0 / + 0xD8, RW 0x68B58C) is not ported: stop S-704
	if (!done(c))
	{
		// RW 0x671ED8: the charge voice is allowed when neither the unit nor its horde is ATTACKING (RW 0x694154(0x25): the model condition bit 37 through
		// 0x46E918, the horde resolved by 0x693A1A(0); review r1 fix 5) and obj + 0x384 (frame + VoiceAttackChargeTimeout) has passed. The retail
		// function also answers false for an object without a drawable (obj + 0xFC); that gate is not ported here (S-704)
		static const int attacking = ModelCondition::indexOf("ATTACKING");
		const Object *horde = obj.getContainedBy();
		if (horde && !(horde->getContain() && horde->getContain()->getHordeContainInterface()))
		{
			horde = nullptr; // only a horde container resolves (RW 0x693A1A)
		}
		const bool isAttacking = attacking >= 0 && (obj.testModelCondition(attacking) || (horde && horde->testModelCondition(attacking)));
		const auto it = m_memory.find(obj.getID());
		const unsigned until = it == m_memory.end() ? 0u : it->second.chargeUntil;
		if (!isAttacking && m_logic.getFrame() >= until)
		{
			fillRow(c, tt, charge);
		}
	}
	if (!done(c) && info && info->air)
	{
		fillRow(c, tt, air);
	}
	if (!done(c))
	{
		fillRow(c, tt, base);
	}
}

bool UnitVoiceResponse::recentlySaid(ObjectID id, const std::string &key) const
{
	// RW 0x6769D9: one of the three slots holds the sound and its frame + MinDelayBetweenEnterStateVoice is not before now
	const auto it = m_memory.find(id);
	if (it == m_memory.end())
	{
		return false;
	}
	for (int i = 0; i < 3; ++i)
	{
		if (it->second.used[i] && it->second.said[i] == key && it->second.frame[i] + m_minDelayEnterStateFrames >= m_logic.getFrame())
		{
			return true;
		}
	}
	return false;
}

void UnitVoiceResponse::rememberSaid(ObjectID id, const std::string &key)
{
	// RW 0x676A23: the slot already holding the sound, else the oldest (lowest frame) slot
	Memory &m = m_memory[id];
	int slot = -1;
	for (int i = 0; i < 3; ++i)
	{
		if (m.used[i] && m.said[i] == key)
		{
			slot = i;
			break;
		}
	}
	for (int i = 0; i < 3 && slot < 0; ++i)
	{
		if (!m.used[i])
		{
			slot = i;
		}
	}
	if (slot < 0)
	{
		slot = 0;
		for (int i = 1; i < 3; ++i)
		{
			if (m.frame[i] < m.frame[slot])
			{
				slot = i;
			}
		}
	}
	m.said[slot] = key;
	m.frame[slot] = m_logic.getFrame();
	m.used[slot] = true;
}

bool UnitVoiceResponse::pickAndPlay(const std::vector<ObjectID> &selection, int msgType, const Info *info)
{
	++m_stats.calls;
	m_last = Last();
	m_last.msgType = msgType;
	if (!handles(msgType))
	{
		++m_stats.unportedMessages;
		return false;
	}
	const bool enterState = isEnterState(msgType);
	const int ignoredInGui = ObjectTemplateInfoBuilder::kindOfIndex("IGNORED_IN_GUI");
	static const int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");

	// the candidates (RW 0x8DEE5B .. 0x8DEF45): (object, ordinal), in selection order and member order (retail walks a hash map keyed by the object pointer;
	// the order only decides which object of an equal sound speaks: INFERENCE, S-705)
	std::vector<std::pair<Object *, int>> candidates;
	int ordinal = 0;
	for (ObjectID id : selection)
	{
		++ordinal;
		Object *obj = m_logic.findObjectByID(id);
		if (!obj || obj->isDestroyed() || !obj->getTemplate() || (ignoredInGui >= 0 && obj->isKindOf((unsigned)ignoredInGui)))
		{
			continue;
		}
		ContainModuleInterface *contain = obj->getContain();
		if (contain && contain->getHordeContainInterface())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
			{
				for (Object *member : *items)
				{
					if (member && !member->isDestroyed() && member->getTemplate() && !(ignoredInGui >= 0 && member->isKindOf((unsigned)ignoredInGui)))
					{
						candidates.emplace_back(member, ordinal);
					}
				}
			}
			continue;
		}
		if (enterState && hordeMember >= 0 && obj->testStatus((unsigned)hordeMember))
		{
			continue;
		}
		candidates.emplace_back(obj, ordinal);
	}

	const CrowdThreshold *crowd = pickCrowd(candidates); // RW [ebp-0xCC]: one crowd entry for every choice
	Rank best;
	best.special = 0;
	best.hero = false;
	best.level = (int)0x80000000;
	best.ordinal = 0x7fffffff;
	best.priority = (int)0x80000000;
	// key: the event names, compared without case (RW 0x8DDD00 builds it from both slots' names)
	std::map<std::string, Collected> sounds;
	std::vector<std::string> keyOrder;
	for (const auto &cand : candidates)
	{
		Object &obj = *cand.first;
		const ThingTemplate &tt = *obj.getTemplate();
		Choice c;
		c.crowdEntry = crowd;
		Rank rank;
		rank.ordinal = cand.second;
		rank.priority = (int)fieldInt(tt, "VoicePriority", 0);
		const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
		rank.hero = heroBit >= 0 && obj.isKindOf((unsigned)heroBit);
		// a hero's level (RW 0x79D12A experience tracker -> 0x689024 / 0x688EEC) stays at the minimum: the experience system is lane XP-1's (S-705)
		switch (msgType)
		{
			case MSG_CREATE_SELECTED_GROUP:
			case MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE:
			case MSG_SELECT_TEAM0: case MSG_SELECT_TEAM1: case MSG_SELECT_TEAM2: case MSG_SELECT_TEAM3: case MSG_SELECT_TEAM4:
			case MSG_SELECT_TEAM5: case MSG_SELECT_TEAM6: case MSG_SELECT_TEAM7: case MSG_SELECT_TEAM8: case MSG_SELECT_TEAM9:
			case MSG_CREATE_SELECT_ALL_GROUP:
				selectVoices(obj, msgType, info, c, rank);
				break;
			case MSG_DO_WEAPON_AT_LOCATION:
			case MSG_DO_WEAPON_AT_OBJECT:
			case MSG_DO_ATTACK_OBJECT:
			case MSG_DO_FORCE_ATTACK_OBJECT:
			case MSG_DO_FORCE_ATTACK_GROUND:
			case VOICE_EVENT_ENTER_STATE_ATTACK:
				attackVoices(obj, msgType, info, c);
				break;
			case MSG_GET_REPAIRED:
			case MSG_GET_HEALED:
			case MSG_DO_MOVETO:
			case MSG_DO_SALVAGE:
			case MSG_DO_MOVETO_FORMATION:
			case VOICE_EVENT_ENTER_STATE_MOVE:
				moveVoices(obj, msgType, info, c, rank);
				break;
			case MSG_DO_FORCEMOVETO:
				// 0x431 is not in RW 0x8DEDBB's switch: a force move says nothing (INFERENCE from the case table)
				break;
			case MSG_DO_ATTACKMOVETO:
			case VOICE_EVENT_ENTER_STATE_ATTACKMOVE:
				// RW 0x8DFBE5: an enemy within the object's vision range makes it the attack voice, else the move voice (the partition query: S-702)
				if (inBattle(obj) || [&]() {
						const float range = fieldReal(tt, "VisionRange");
						for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
						{
							if (o != &obj && !o->isDestroyed() && obj.getRelationship(*o) == ENEMIES && dist2D(*o->getPosition(), *obj.getPosition()) <= range * range)
							{
								return true;
							}
						}
						return false;
					}())
				{
					attackVoices(obj, msgType, info, c);
				}
				else
				{
					moveVoices(obj, msgType, info, c, rank);
				}
				break;
			case MSG_DO_GUARD_POSITION:
			case MSG_DO_GUARD_OBJECT:
				fillRow(c, tt, VOICE_GUARD);
				break;
			case MSG_FOUNDATION_CONSTRUCT:
			case MSG_DOZER_CONSTRUCT:
			case MSG_RESUME_CONSTRUCTION:
				fillName(c, tt, "VoiceBuildResponse");
				break;
			case MSG_EVACUATE:
			case MSG_EVACUATE_CONTESTERS:
				fillName(c, tt, "VoiceUnload");
				break;
			case MSG_COMBATDROP_AT_LOCATION:
			case MSG_COMBATDROP_AT_OBJECT:
				fillName(c, tt, "VoiceCombatDrop");
				break;
			case MSG_DO_REPAIR:
				fillName(c, tt, "VoiceRepair");
				break;
			case MSG_DOCK:
			case MSG_HARVEST:
				fillName(c, tt, "VoiceSupply");
				break;
			case MSG_ENTER:
			{
				// RW 0x8DF461 .. 0x8DF716: VoiceEnterUnit<target>, else by the enter kind (heal pad / garrison / hostile / ring / slaughterhouse: the kind is
				// the AI's command classification RW [ebp-0x84], not ported: S-704), else VoiceEnter
				const Object *target = info && info->target != INVALID_ID ? m_logic.findObjectByID(info->target) : nullptr;
				if (target && target->getTemplate())
				{
					fillName(c, tt, "VoiceEnterUnit" + target->getTemplate()->getName());
				}
				if (!done(c))
				{
					fillName(c, tt, "VoiceEnter");
				}
				break;
			}
			case VOICE_EVENT_CREATED:
			case VOICE_EVENT_FULLY_CREATED:
			{
				const Object *producer = info && info->producer != INVALID_ID ? m_logic.findObjectByID(info->producer) : nullptr;
				if (producer && producer->getTemplate())
				{
					fillName(c, tt, std::string(msgType == VOICE_EVENT_CREATED ? "VoiceCreatedFrom" : "VoiceFullyCreatedFrom") + producer->getTemplate()->getName());
				}
				if (!done(c))
				{
					fillRow(c, tt, msgType == VOICE_EVENT_CREATED ? VOICE_CREATED : VOICE_FULLY_CREATED);
				}
				break;
			}
			case VOICE_EVENT_TASK_COMPLETE:
				fillRow(c, tt, VOICE_TASK_COMPLETE);
				break;
			case VOICE_EVENT_DEFECT:
				fillRow(c, tt, VOICE_DEFECT);
				break;
			case VOICE_EVENT_FEAR:
				fillRow(c, tt, VOICE_FEAR);
				break;
			case VOICE_EVENT_ALERT:
				fillRow(c, tt, VOICE_ALERT);
				break;
			default:
				break;
		}
		// RW 0x8DFEA4: a choice with a voice competes
		if (!c.voice.any())
		{
			continue;
		}
		if (rankLess(best, rank))
		{
			sounds.clear();
			keyOrder.clear();
			best = rank;
		}
		if (!rankLess(best, rank) && !rankLess(rank, best))
		{
			// RW 0x8DFF02 .. 0x8DFF2D / 0x8DE522: the bucket key is the PRIMARY audio event alone (retail: its info pointer); the same sound with different
			// Eva ids, and every choice without a primary sound (Eva only), share one bucket each (review r1 fix 4). The first choice of a bucket keeps its slots.
			const std::string key = lower(c.voice.sound);
			auto ins = sounds.emplace(key, Collected());
			if (ins.second)
			{
				ins.first->second.voice = c.voice;
				ins.first->second.crowd = c.crowd;
				ins.first->second.object = &obj;
				ins.first->second.charge = c.charge;
			}
			++ins.first->second.count;
		}
	}
	if (sounds.empty())
	{
		++m_stats.silent;
		return false;
	}
	// RW 0x8DFF8B: the most counted (strictly more replaces: the first bucket in map order wins a tie). Retail orders the buckets by the audio info
	// POINTER (unsigned), so a count tie goes to the event with the lowest info address; here the buckets are ordered by the lower-case event name, a
	// stable presentation order that is NOT the target fact (S-705)
	auto chosen = sounds.begin();
	for (auto it = std::next(sounds.begin()); it != sounds.end(); ++it)
	{
		if (it->second.count > chosen->second.count)
		{
			chosen = it;
		}
	}
	const Collected &pick = chosen->second;
	const std::string key = chosen->first;
	m_last.object = pick.object->getID();
	m_last.voice = pick.voice;
	m_last.count = pick.count;
	// RW 0x8DFF9D: an enter-state voice the object said within MinDelayBetweenEnterStateVoice plays nothing
	// the repeat memory holds primary sounds only (an Eva-only voice never enters it: review r1 fix 4)
	if (enterState && !pick.voice.sound.empty() && recentlySaid(pick.object->getID(), key))
	{
		++m_stats.repeatSuppressed;
		return false;
	}
	// RW 0x8DFFC9 .. 0x8E0029: every candidate remembers the sound (enter state) and starts its charge timeout (an attack)
	if (enterState || pick.charge)
	{
		for (const auto &cand : candidates)
		{
			if (enterState && !pick.voice.sound.empty())
			{
				rememberSaid(cand.first->getID(), key);
			}
			if (pick.charge)
			{
				const long long timeout = fieldInt(*cand.first->getTemplate(), "VoiceAttackChargeTimeout", 0);
				m_memory[cand.first->getID()].chargeUntil = m_logic.getFrame() + (unsigned)timeout;
			}
		}
	}
	bool played = false;
	const Player *owner = pick.object->getControllingPlayer();
	const int ownerIndex = owner ? owner->getPlayerIndex() : -1;
	if (!pick.voice.eva.empty())
	{
		// RW 0x8E0038: the local player's object, or an enter-state voice
		if ((owner && ownerIndex == m_localPlayer) || enterState)
		{
			m_sink.reportEva(pick.voice.eva, pick.object->getPosition());
			++m_stats.evaReports;
			played = true;
		}
	}
	if (!pick.voice.sound.empty())
	{
		if (!m_sink.soundExists(pick.voice.sound))
		{
			++m_stats.unknownSounds;
		}
		else
		{
			m_sink.playSound(pick.voice.sound, pick.object->getID(), ownerIndex);
			++m_stats.soundsPlayed;
			played = true;
		}
	}
	// RW 0x8E00A7 .. 0x8E0121: the crowd response's sound plays with the voice, attached to the same object
	if (!pick.crowd.sound.empty() && !pick.voice.sound.empty() && m_sink.soundExists(pick.crowd.sound))
	{
		m_sink.playSound(pick.crowd.sound, pick.object->getID(), ownerIndex);
		++m_stats.crowdSounds;
		played = true;
	}
	if (played)
	{
		++m_stats.played;
	}
	return played;
}

std::vector<std::string> UnitVoiceResponse::acceptanceStops()
{
	return {
		"[S-700] unit voice: the crowd response (the second voice slot, RW 0x8DEF63 / 0x825C45) is ported from CrowdResponse.ini's documentation: the weighted "
		"vote of the candidates' CrowdResponseKey, the Threshold entry with the largest count <= the number of units with that key (INFERENCE: the count), "
		"the INI read with the file's documented syntax (the binary's CrowdResponse parser was not read; Stats::crowdResponseSkipped counts keys naming no "
		"loaded response)",
		"[S-701] unit voice: a draw module may override a voice row or a UnitSpecificSounds name (RW 0x676A8A / 0x674FFE ask every draw module's interface, vtable + 0x34, "
		"slot 0 / 1, before the template); which RotWK draw modules answer was not read, so the template's voice is used",
		"[S-702] unit voice: the enemy-near query of the battle voices (RW 0x701443 -> 0x7006D7 with filter 0x6E and the object's vision range RW 0x68E43B, which "
		"scales obj + 0x1B0 by a module bonus) is approximated by `a living object of an ENEMIES relationship within the template's VisionRange (2D)`",
		"[S-703] unit voice: message types of RW 0x8DEDBB not answered here: special powers (0x410 .. 0x412, 0x456), combine hordes (0x423), weapon modes (0x43A), "
		"the planned action queue (0x46A), VoiceDesperateAttack / RapidFire / CaptureBuildingComplete / StartCharging / NoBuild (0x7DB .. 0x7DD, 0x7EA .. 0x7EC), "
		"the Ring voices (0x7E3 .. 0x7E5, 0x7E9)",
		"[S-704] unit voice: rules of the move / attack / enter helpers not ported: the planning order mode (player + 0x770 == 2 -> MiscAudio PlanningModeOrderGiven), "
		"VoiceCrush (GameData + 0x8B9, crushable target), the busy gate (GameData + 0x8B0), the castle and camp rules (RW 0x8DDBC4, campness RW 0x8DD6F3 with "
		"AudioSettings VoiceMoveToCampMaxCampnessAtStartPoint / MinCampnessAtEndPoint), VoiceMoveOverWalls (RW 0x8DE628), the weapon's voice (RW 0x68B58C weapon "
		"template + 0xD0 / + 0xD8), the enter kinds (heal / garrison / hostile / ring / slaughterhouse), the charge voice's drawable gate (RW 0x671EE4: no drawable, no charge)",
		"[S-705] unit voice: the candidate order is selection then member order (retail iterates a hash map keyed by Object pointers), a count tie between "
		"two sounds goes to the lower-case name order (retail: the lower audio info POINTER, RW 0x8DE522's unsigned compare), and a hero's level is the "
		"minimum (the experience tracker RW 0x79D12A is lane XP-1's): which sound wins a tie and which object of a bucket speaks may differ from retail",
	};
}

bool AudioApiVoiceSink::soundExists(const std::string &name)
{
	return AudioApi::isValidEvent(name);
}

void AudioApiVoiceSink::playSound(const std::string &name, ObjectID object, int playerIndex)
{
	AudioLog::Scope logScope("unit voice");
	AudioApi::playSoundForObject(name, object, playerIndex);
}

void AudioApiVoiceSink::reportEva(const std::string &eventName, const Coord3D *position)
{
	AudioApi::reportEva(eventName, position);
}

void PlayUnitVoiceForMessage(UnitVoiceResponse &voice, const std::vector<ObjectID> &selection, const GameMessage &msg)
{
	const int type = msg.getType();
	if (!UnitVoiceResponse::handles(type))
	{
		return;
	}
	// ZH PickAndPlayInfo from the command: the target object and the location the command carries (the select messages carry the selected ids, not a target)
	UnitVoiceResponse::Info info;
	const bool selects = type == MSG_CREATE_SELECTED_GROUP || type == MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE || type == MSG_CREATE_SELECT_ALL_GROUP ||
		(type >= MSG_SELECT_TEAM0 && type <= MSG_SELECT_TEAM9);
	for (size_t i = 0; i < msg.getArgumentCount(); ++i)
	{
		const GameMessageArgument *a = msg.getArgument(i);
		if (!selects && a->type == ARGUMENTDATATYPE_OBJECTID && info.target == INVALID_ID)
		{
			info.target = a->objectID;
		}
		else if (a->type == ARGUMENTDATATYPE_LOCATION && !info.hasPosition)
		{
			info.hasPosition = true;
			info.position = a->location;
		}
	}
	voice.pickAndPlay(selection, type, &info);
}

