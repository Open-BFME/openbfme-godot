// OpenBFME. GPL-3.0.
// See GameClient/SoundSelectorClientBehavior.h for the target facts and the addresses (lane DECOMP-2, stop S-701). Client presentation: no simulation state.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameClient/SoundSelectorClientBehavior.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace
{
// RW 0xC26720: the audio table (33 voice rows parsed by RW 0x73ACEF, then 23 sound rows parsed by RW 0x73AD07)
const char *const kRowNames[SoundSelector::AUDIO_ROW_COUNT] = {
	"VoiceSelect", "VoiceSelectUnderConstruction", "VoiceSelectBattle", "VoiceMove", "VoiceMoveToHigherGround", "VoiceMoveOverWalls", "VoiceAttack",
	"VoiceAttackCharge", "VoiceFear", "VoiceCreated", "VoiceTaskComplete", "VoiceDefect", "VoiceAttackAir", "VoiceGuard", "VoiceAlert", "VoiceFullyCreated",
	"VoiceRetreatToCastle", "VoiceMoveToCamp", "VoiceAttackStructure", "VoiceAttackMachine", "VoiceMoveWhileAttacking", "VoiceCombineWithHorde",
	"VoiceEnterStateAttack", "VoiceEnterStateAttackCharge", "VoiceEnterStateAttackAir", "VoiceEnterStateAttackStructure", "VoiceEnterStateAttackMachine",
	"VoiceEnterStateMove", "VoiceEnterStateMoveToHigherGround", "VoiceEnterStateMoveOverWalls", "VoiceEnterStateRetreatToCastle", "VoiceEnterStateMoveToCamp",
	"VoiceEnterStateMoveWhileAttacking", "SoundMoveStart", "SoundMoveStartDamaged", "SoundMoveLoop", "SoundMoveLoopDamaged", "SoundAmbient", "SoundAmbientDamaged",
	"SoundAmbientReallyDamaged", "SoundAmbientRubble", "SoundAmbientBattle", "SoundStealthOn", "SoundStealthOff", "SoundCreated", "SoundOnDamaged",
	"SoundOnReallyDamaged", "SoundEnter", "SoundExit", "SoundPromotedVeteran", "SoundPromotedElite", "SoundPromotedHero", "SoundFallingFromPlane", "SoundImpact",
	"SoundImpactCyclonic", "SoundCrushing",
};
const int kFirstSoundRow = 33; // SoundMoveStart: the rows from here are parsed by RW 0x73AD07

bool startsNoCase(const std::string &s, const char *prefix)
{
	const size_t n = std::strlen(prefix);
	return s.size() >= n && AsciiStringUtil::compareNoCase(s.substr(0, n), prefix) == 0;
}

// RW 0x73AB45: one value into a record that already holds the row's earlier value
void parseValue(const std::string &token, SoundSelector::Record &r)
{
	if (AsciiStringUtil::compareNoCase(token, "NoSound") == 0) // RW 0x73AB5D: _strcmpi with RW 0xC24EC8
	{
		r = SoundSelector::Record();
		return;
	}
	if (startsNoCase(token, "EVA:")) // RW 0x73AB93: _strnicmp(token, "EVA:", 4)
	{
		const std::function<int(const std::string &)> &eva = TheContainParseHooks().evaEventIndex;
		if (!eva)
		{
			throw INIException(8, "Error: Attempting to parse special EVA: sound before EVA system initialized: %s", token.c_str()); // RW 0xC25048
		}
		const std::string event = token.substr(4);
		if (eva(event) < 0)
		{
			throw INIException(3, "Unknown EVA event in EVA:%s", event.c_str()); // RW 0xC25028
		}
		r.eva = event; // the sound half stays (RW 0x73ABEE writes only the id)
		return;
	}
	const bool plusSound = startsNoCase(token, "+SOUND:"); // RW 0x73AC14
	const std::string name = plusSound ? token.substr(7) : token;
	const std::function<bool(const std::string &)> &exists = TheContainParseHooks().audioEventExists;
	if (!exists)
	{
		throw INIException(8, "sound selector: TheAudio's event table is not installed");
	}
	if (!exists(name)) // RW 0x73AC45: TheAudio vslot 0x12C answers null
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str()); // RW 0xC2500C
	}
	r.sound = name;
	if (!plusSound)
	{
		r.eva.clear(); // RW 0x73AC7F: the id becomes -1
	}
}

// RW 0x73ACEF (voice rows) / RW 0x73AD07 (sound rows): getNextToken, then RW 0x73AB45; a sound row refuses an Eva half (RW 0x73ACAF)
void parseRow(INI *ini, void *, void *store, const void *userData)
{
	const int row = (int)(std::intptr_t)userData;
	SoundSelector::SoundSet *set = static_cast<SoundSelector::SoundSet *>(store);
	const std::string token = ini->getNextToken();
	SoundSelector::Record &r = set->rows[(size_t)row];
	parseValue(token, r);
	if (row >= kFirstSoundRow && !r.eva.empty())
	{
		throw INIException(3, "This is not a valid place to use the EVA: sound syntax: %s", token.c_str()); // RW 0xC250A4
	}
}

// RW 0x73F5CD, the catch-all row of table RW 0xC2728C: `Name = <value>` (the value a voice row's)
void parseUnitSpecificLine(INI *ini, void *, void *store, const void *userData)
{
	std::map<std::string, SoundSelector::Record> *map = static_cast<std::map<std::string, SoundSelector::Record> *>(store);
	const std::string key = static_cast<const char *>(userData);
	const std::string token = ini->getNextToken(); // RW 0x73F5E0
	SoundSelector::Record r;
	parseValue(token, r);
	(*map)[key] = r; // a later line of the block replaces an earlier one (UnitSpecificSound.h's reading of the template map)
}

const FieldParse kUnitSpecificTable[] = { // RW 0xC2728C
	{ nullptr, parseUnitSpecificLine, nullptr, 0 }
};

// RW 0x73F684: the map is cleared (RW 0x656FA6), then the block's lines
void parseUnitSpecificSounds(INI *ini, void *, void *store, const void *)
{
	SoundSelector::SoundSet *set = static_cast<SoundSelector::SoundSet *>(store);
	set->unitSpecific.clear();
	ini->initFromINI(&set->unitSpecific, kUnitSpecificTable);
}

// RW 0x8CEF70 / 0x8CF53A / 0x8D000E: parseInt into VoicePriority and the "set" byte
void parseVoicePriority(INI *ini, void *, void *store, const void *)
{
	SoundSelector::SoundSet *set = static_cast<SoundSelector::SoundSet *>(store);
	INI::parseInt(ini, nullptr, &set->voicePriority, nullptr);
	set->hasVoicePriority = true;
}

// every row a SoundSet answers to: UnitSpecificSounds, VoicePriority, then the audio table's 56 rows (store = the SoundSet)
const FieldParse *soundSetTable()
{
	static FieldParse table[SoundSelector::AUDIO_ROW_COUNT + 3];
	static bool built = false;
	if (!built)
	{
		table[0] = { "UnitSpecificSounds", parseUnitSpecificSounds, nullptr, 0 };
		table[1] = { "VoicePriority", parseVoicePriority, nullptr, 0 };
		for (int i = 0; i < SoundSelector::AUDIO_ROW_COUNT; ++i)
		{
			table[2 + i] = { kRowNames[i], parseRow, (const void *)(std::intptr_t)i, 0 };
		}
		table[SoundSelector::AUDIO_ROW_COUNT + 2] = { nullptr, nullptr, nullptr, 0 };
		built = true;
	}
	return table;
}

typedef ModelConditionSoundSelectorClientBehaviorModuleData MCData;
typedef UpgradeSoundSelectorClientBehaviorModuleData UpData;
typedef RandomSoundSelectorClientBehaviorModuleData RandomData;

// RW 0x8CF47D: SoundState <flags> ... End
void parseSoundState(INI *ini, void *instance, void *, const void *)
{
	MCData::Entry e;
	ParseBitFlags(ini, e.conditions.data(), e.conditions.size(), TheModelConditionNames); // RW 0x4B8B37 on the header
	MultiIniFieldParse multi;
	multi.add(soundSetTable(), (unsigned)offsetof(MCData::Entry, sounds)); // table RW 0xC75D44 and the audio rows at + 0x4C
	ini->initFromINIMulti(&e, multi);
	static_cast<MCData *>(instance)->m_entries.push_back(std::move(e)); // RW 0x8CF443
}

// RW 0x4B8C21 -> 0x4B8B37
void parseConditionWords(INI *ini, void *, void *store, const void *)
{
	SoundSelector::ConditionWords *w = static_cast<SoundSelector::ConditionWords *>(store);
	ParseBitFlags(ini, w->data(), w->size(), TheModelConditionNames);
}

const FieldParse kUpgradeEntryTable[] = { // RW 0xC75E20 (UnitSpecificSounds and VoicePriority are the SoundSet's rows)
	{ "ExcludedUpgrades", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(UpData::Entry, excludedNames) },
	{ "RequiredModelConditions", parseConditionWords, nullptr, (int)offsetof(UpData::Entry, requiredConditions) },
	{ "ExcludedModelConditions", parseConditionWords, nullptr, (int)offsetof(UpData::Entry, excludedConditions) },
	{ nullptr, nullptr, nullptr, 0 }
};

bool anyBit(const SoundSelector::ConditionWords &w)
{
	for (std::uint32_t x : w)
	{
		if (x)
		{
			return true;
		}
	}
	return false;
}

// RW 0x8CF5BF: the names become upgrades (TheUpgradeCenter); a name that is not an Upgrade is INIException 3 (RW 0xC75DA0). RW reads null + 0x38 for an empty name
// or "None" (a crash); the port refuses those with the same exception
void resolveUpgrades(const std::vector<std::string> &names, std::vector<int> &bits)
{
	for (const std::string &n : names)
	{
		const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(n);
		if (!u)
		{
			throw INIException(3, "An upgrade module references %s, which is not an Upgrade", n.c_str());
		}
		bits.push_back(u->getMaskBit());
	}
}

// RW 0x8CFF8F -> 0x8CF9AD: SoundUpgrade <upgrade> ... End
void parseSoundUpgrade(INI *ini, void *instance, void *, const void *)
{
	UpData::Entry e;
	for (const char *tok = ini->getNextTokenOrNull(); tok; tok = ini->getNextTokenOrNull()) // RW 0x8CFA4F: every header token, no macro expansion
	{
		e.requiredNames.push_back(tok);
	}
	MultiIniFieldParse multi;
	multi.add(kUpgradeEntryTable);
	multi.add(soundSetTable(), (unsigned)offsetof(UpData::Entry, sounds)); // the audio rows at + 0x1D0
	ini->initFromINIMulti(&e, multi);
	if (!TheUpgradeCenter)
	{
		// RW 0x8CFA9E resolves later (at the first query) without TheUpgradeCenter; the port resolves at parse only and refuses a load without it
		throw INIException(8, "UpgradeSoundSelectorClientBehavior: TheUpgradeCenter is not installed");
	}
	resolveUpgrades(e.requiredNames, e.requiredBits);
	resolveUpgrades(e.excludedNames, e.excludedBits);
	e.requiredNames.clear(); // RW 0x8CF6C1: the name lists are emptied once resolved
	e.excludedNames.clear();
	e.resolved = true;
	e.hasRequiredConditions = anyBit(e.requiredConditions); // RW 0x8CFAD7 (RW 0x4B3783)
	e.hasExcludedConditions = anyBit(e.excludedConditions);
	static_cast<UpData *>(instance)->m_entries.push_back(std::move(e)); // RW 0x8CFF55
}

const FieldParse kMCTable[] = { // RW 0xC75D80
	{ "SoundState", parseSoundState, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kUpgradeTable[] = { // RW 0xC75F0C
	{ "SoundUpgrade", parseSoundUpgrade, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kRandomTable[] = { // RW 0xC75F48 (UnitSpecificSounds and VoicePriority are the SoundSet's rows)
	{ "Chance", INI::parsePercentToReal, nullptr, (int)offsetof(RandomData, m_chance) },
	{ "RerollOnEveryFrame", INI::parseBool, nullptr, (int)offsetof(RandomData, m_rerollOnEveryFrame) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x4CE8CA: every flag of `mask` is set in `flags`
bool allSet(const SoundSelector::ConditionWords &mask, const SoundSelector::ConditionWords &flags)
{
	for (size_t w = 0; w < mask.size(); ++w)
	{
		if ((flags[w] & mask[w]) != mask[w])
		{
			return false;
		}
	}
	return true;
}

// RW 0x5DF56A: every required flag set and none of the excluded
bool setAndClear(const SoundSelector::ConditionWords &flags, const SoundSelector::ConditionWords &required, const SoundSelector::ConditionWords &excluded)
{
	for (size_t w = 0; w < flags.size(); ++w)
	{
		if ((flags[w] & required[w]) != required[w] || (flags[w] & excluded[w]) != 0)
		{
			return false;
		}
	}
	return true;
}

bool hasUpgradeBit(const Object &obj, int bit)
{
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgradeByMaskBit(bit) : nullptr;
	if (!u)
	{
		throw std::logic_error("UpgradeSoundSelectorClientBehavior: no Upgrade has mask bit " + std::to_string(bit));
	}
	return obj.hasUpgrade(u); // RW 0x68E040 by bit
}

// RW 0x8CF769
bool upgradeEntryApplies(const UpData::Entry &e, const SoundSelector::Query &q)
{
	if (!q.object)
	{
		return false; // RW 0x8CF84F: the drawable has no object
	}
	if (e.hasRequiredConditions || e.hasExcludedConditions)
	{
		if (!q.drawableFlags || !setAndClear(*q.drawableFlags, e.requiredConditions, e.excludedConditions))
		{
			return false; // RW 0x8CF78D: the object's drawable (RW 0x70E013), none fails
		}
	}
	for (int bit : e.requiredBits)
	{
		if (!hasUpgradeBit(*q.object, bit))
		{
			return false;
		}
	}
	for (int bit : e.excludedBits)
	{
		if (hasUpgradeBit(*q.object, bit))
		{
			return false;
		}
	}
	return true;
}

// RW 0x8D0138 and the roll < Chance test of every slot (RW 0x8D0224: comiss roll, Chance; jb)
bool randomActive(const RandomData &d, const SoundSelector::Query &q, SoundSelectors::RandomState &s, SoundSelectors::RandomRoll &rnd)
{
	if (!s.made)
	{
		s.roll = rnd.roll(); // RW 0x8D00AE (constructor), INFERENCE: drawn at the first query (S-701)
		s.frame = q.clientFrame;
		s.made = true;
	}
	else if (d.m_rerollOnEveryFrame && s.frame != q.clientFrame)
	{
		s.roll = rnd.roll(); // RW 0x8D016F
		s.frame = q.clientFrame;
	}
	return s.roll < d.m_chance;
}

enum Ask
{
	ASK_ROW,
	ASK_NAME,
	ASK_PRIORITY
};

bool ask(const ThingTemplate &tt, const SoundSelector::Query &q, Ask what, int row, const std::string &name, SoundSelectors::RandomStates &rs, SoundSelectors::RandomRoll &rnd,
	SoundSelector::Record *outRecord, int *outPriority)
{
	size_t randomIndex = 0;
	for (const ThingTemplate::Nugget &n : tt.clientBehaviorModules().nuggets())
	{
		const ModuleData *data = n.data.get();
		if (const MCData *mc = dynamic_cast<const MCData *>(data))
		{
			// RW 0x8CF01B / 0x8CF081 / 0x8CEFC1 (the drawable's flags, + 0x258)
			if (!q.drawableFlags)
			{
				continue;
			}
			for (const MCData::Entry &e : mc->m_entries)
			{
				if (!allSet(e.conditions, *q.drawableFlags))
				{
					continue;
				}
				if (what == ASK_ROW && !e.sounds.rows[(size_t)row].sound.empty())
				{
					*outRecord = e.sounds.rows[(size_t)row];
					return true;
				}
				if (what == ASK_NAME)
				{
					const auto it = e.sounds.unitSpecific.find(name);
					if (it != e.sounds.unitSpecific.end())
					{
						*outRecord = it->second; // RW 0x8CF0C1: the stored record, with or without an audio event
						return true;
					}
				}
				if (what == ASK_PRIORITY && e.sounds.hasVoicePriority)
				{
					*outPriority = e.sounds.voicePriority;
					return true;
				}
			}
		}
		else if (const UpData *up = dynamic_cast<const UpData *>(data))
		{
			// RW 0x8CF833 / 0x8CF8C5 / 0x8CF957
			for (const UpData::Entry &e : up->m_entries)
			{
				if (!upgradeEntryApplies(e, q))
				{
					continue;
				}
				if (what == ASK_ROW && !e.sounds.rows[(size_t)row].sound.empty())
				{
					*outRecord = e.sounds.rows[(size_t)row];
					return true;
				}
				if (what == ASK_NAME)
				{
					const auto it = e.sounds.unitSpecific.find(name); // RW 0x8CF731: the map's record, else an empty one
					if (it != e.sounds.unitSpecific.end() && !it->second.sound.empty())
					{
						*outRecord = it->second;
						return true;
					}
				}
				if (what == ASK_PRIORITY && e.sounds.hasVoicePriority)
				{
					*outPriority = e.sounds.voicePriority;
					return true;
				}
			}
		}
		else if (const RandomData *rd = dynamic_cast<const RandomData *>(data))
		{
			// RW 0x8D0215 / 0x8D0277 / 0x8D018A
			if (rs.size() <= randomIndex)
			{
				rs.resize(randomIndex + 1);
			}
			SoundSelectors::RandomState &s = rs[randomIndex++];
			if (!randomActive(*rd, q, s, rnd))
			{
				continue;
			}
			if (what == ASK_ROW && !rd->m_sounds.rows[(size_t)row].sound.empty())
			{
				*outRecord = rd->m_sounds.rows[(size_t)row]; // RW 0x8D01F2: data + 8 + row * 8
				return true;
			}
			if (what == ASK_NAME)
			{
				const auto it = rd->m_sounds.unitSpecific.find(name); // RW 0x8D024A
				if (it != rd->m_sounds.unitSpecific.end())
				{
					*outRecord = it->second;
					return true;
				}
			}
			if (what == ASK_PRIORITY && rd->m_sounds.hasVoicePriority)
			{
				*outPriority = rd->m_sounds.voicePriority; // RW 0x8D01AB: the value, true only when it was set
				return true;
			}
		}
	}
	return false;
}
} // namespace

const char *SoundSelector::rowName(int row)
{
	return row >= 0 && row < AUDIO_ROW_COUNT ? kRowNames[row] : "";
}

int SoundSelector::rowIndex(const std::string &name)
{
	for (int i = 0; i < AUDIO_ROW_COUNT; ++i)
	{
		if (name == kRowNames[i])
		{
			return i;
		}
	}
	return -1;
}

void ModelConditionSoundSelectorClientBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kMCTable);
}

void UpgradeSoundSelectorClientBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kUpgradeTable);
}

void RandomSoundSelectorClientBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRandomTable);
	p.add(soundSetTable(), (unsigned)offsetof(RandomData, m_sounds)); // the audio rows at data + 8 (the registry's second table, RW 0xC26720 with extra 8)
}

bool SoundSelectors::row(const ThingTemplate &tt, const SoundSelector::Query &q, int row, RandomStates &rs, RandomRoll &rnd, SoundSelector::Record *out)
{
	if (row < 0 || row >= SoundSelector::AUDIO_ROW_COUNT)
	{
		return false; // RW 0x676A9B: key -1 answers the default record (no module is asked)
	}
	return ask(tt, q, ASK_ROW, row, std::string(), rs, rnd, out, nullptr);
}

bool SoundSelectors::name(const ThingTemplate &tt, const SoundSelector::Query &q, const std::string &name, RandomStates &rs, RandomRoll &rnd, SoundSelector::Record *out)
{
	return ask(tt, q, ASK_NAME, 0, name, rs, rnd, out, nullptr);
}

bool SoundSelectors::voicePriority(const ThingTemplate &tt, const SoundSelector::Query &q, RandomStates &rs, RandomRoll &rnd, int *out)
{
	return ask(tt, q, ASK_PRIORITY, 0, std::string(), rs, rnd, nullptr, out);
}

bool SoundSelectors::any(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.clientBehaviorModules().nuggets())
	{
		const ModuleData *d = n.data.get();
		if (dynamic_cast<const MCData *>(d) || dynamic_cast<const UpData *>(d) || dynamic_cast<const RandomData *>(d))
		{
			return true;
		}
	}
	return false;
}

void SoundSelectorClientBehavior::registerClasses(ModuleFactory &modules)
{
	modules.bindTypedData<MCData>("ModelConditionSoundSelectorClientBehavior", MODULETYPE_CLIENT_BEHAVIOR);
	modules.bindTypedData<UpData>("UpgradeSoundSelectorClientBehavior", MODULETYPE_CLIENT_BEHAVIOR);
	modules.bindTypedData<RandomData>("RandomSoundSelectorClientBehavior", MODULETYPE_CLIENT_BEHAVIOR);
	for (const char *name : { "ModelConditionSoundSelectorClientBehavior", "UpgradeSoundSelectorClientBehavior", "RandomSoundSelectorClientBehavior" })
	{
		modules.bindModuleProc(name, MODULETYPE_CLIENT_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			return std::make_unique<SoundSelectorClientBehavior>(thing, data);
		});
	}
}
