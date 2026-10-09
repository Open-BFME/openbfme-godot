// OpenBFME. GPL-3.0. See EvaEvents.h.

#include "Common/Audio/EvaEvents.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

namespace
{
const char *const kPredefined[EvaEventStore::kPredefinedCount + 1] = { "DefaultEvaEvent", "BeaconDetected", "GeneralLevelUp", "UnitLevelUp", "UpgradeComplete", "CastleBreached", "AllyDefeated",
	"EnemyDefeated", "EnemyCampDestroyed", "CampDestroyed", "AllyCampDestroyed", "BuildQueuePausedDueToCPLimit", "CannotBuildDueToCPLimit", "BuildQueuePausedDueToFunds", "CannotBuildDueToFunds",
	"WallsBeingClimbed", "BuildingBeingStolen", "BuildingStolen", "WorldMustBattle", "WorldMustRetreat", "WorldMustChooseOwner", "WorldRegionLostUncontested", nullptr };

struct SideCtx
{
	EvaSideSound *side;
	const AudioEventInfoStore *audio;
};

// RW 0x73B217 -> 0x73AA94 (parseAudioEventRTS): "NoSound" clears, an unknown event is "Invalid Sound '%s'".
void parseSoundName(INI *ini, void *instance, void *, const void *)
{
	SideCtx *c = static_cast<SideCtx *>(instance);
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		c->side->sound.clear();
		return;
	}
	if (!c->audio->contains(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	c->side->sound = name;
}

void parseSideName(INI *ini, void *instance, void *, const void *)
{
	static_cast<SideCtx *>(instance)->side->side = ini->getNextAsciiString();
}
} // namespace

const char *const *EvaEventStore::predefinedNames()
{
	return kPredefined;
}

const std::vector<std::string> &EvaEventStore::loadOrder()
{
	static const std::vector<std::string> files = { "Data\\INI\\Default\\Eva.ini", "Data\\INI\\Eva.ini" };
	return files;
}

EvaEventStore::EvaEventStore()
{
	m_base.resize(kPredefinedCount);
	m_overlay.resize(kPredefinedCount);
	for (int i = 0; i < kPredefinedCount; ++i)
	{
		m_base[(size_t)i].name = kPredefined[i];
		m_overlay[(size_t)i].name = kPredefined[i];
		m_baseNames[kPredefined[i]] = i;
		m_overlayNames[kPredefined[i]] = i;
	}
}

int EvaEventStore::findIndex(const std::string &name, bool overlay) const
{
	const auto &names = overlay ? m_overlayNames : m_baseNames;
	const auto it = names.find(name);
	return it == names.end() ? -1 : it->second;
}

const EvaEventRecord *EvaEventStore::record(int index, bool overlay) const
{
	const auto &v = overlay ? m_overlay : m_base;
	return index >= 0 && (size_t)index < v.size() ? &v[(size_t)index] : nullptr;
}

int EvaEventStore::createRecord(const std::string &name, bool overlay)
{
	auto &records = overlay ? m_overlay : m_base;
	auto &names = overlay ? m_overlayNames : m_baseNames;
	const int id = (int)records.size();
	EvaEventRecord r;
	r.name = name;
	records.push_back(r);
	names[name] = id;
	return id;
}

// The record field table (RW 0xBF21F0) over a record: SideSound and OtherEvaEventsToBlock need the store, so the rows carry the store as userData.
void EvaEventStore::parseRecord(INI *ini, EvaEventRecord &record, const AudioEventInfoStore &audio, bool overlay)
{
	struct Ctx
	{
		EvaEventRecord *record;
		const AudioEventInfoStore *audio;
		const EvaEventStore *store;
		bool overlay;
	};
	struct Parsers
	{
		static void sideSound(INI *ini2, void *instance, void *, const void *)
		{
			Ctx *c = static_cast<Ctx *>(instance);
			EvaSideSound side;
			SideCtx sc = { &side, c->audio };
			// RW table 0xBF21C0: Side (parseAsciiString), Sound (parseAudioEventRTS); the block ends at End
			const FieldParse table[] = { { "Side", parseSideName, nullptr, 0 }, { "Sound", parseSoundName, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
			ini2->initFromINI(&sc, table);
			c->record->sideSounds.push_back(side);
		}
		// RW 0x5DE179: names until the end of the line, each through 0x5DE0D8
		static void otherEvents(INI *ini2, void *instance, void *, const void *)
		{
			Ctx *c = static_cast<Ctx *>(instance);
			for (const char *token = ini2->getNextTokenOrNull(); token; token = ini2->getNextTokenOrNull())
			{
				int id;
				if (AsciiStringUtil::compareNoCase(token, "None") == 0)
				{
					id = -1;
				}
				else
				{
					id = c->store->findIndex(token, c->overlay);
					if (id < 0)
					{
						throw INIException(3, "Expected a recognized Eva event name or 'None'; got '%s'", token);
					}
				}
				c->record->otherEventsToBlock.push_back(id);
			}
		}
		static void priority(INI *ini2, void *instance, void *, const void *) { INI::parseUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->record->priority, nullptr); }
		static void timeBetween(INI *ini2, void *instance, void *, const void *) { INI::parseUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->record->timeBetweenEventsMS, nullptr); }
		static void expiration(INI *ini2, void *instance, void *, const void *) { INI::parseUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->record->expirationTimeMS, nullptr); }
		static void quiet(INI *ini2, void *instance, void *, const void *) { INI::parseUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->record->quietTimeMS, nullptr); }
		static void wait(INI *ini2, void *instance, void *, const void *) { INI::parseDurationUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->record->millisecondsToWaitBeforePlayingFrames, nullptr); }
		static void home(INI *ini2, void *instance, void *, const void *) { INI::parseBool(ini2, nullptr, &static_cast<Ctx *>(instance)->record->alwaysPlayFromHomeBase, nullptr); }
		static void jump(INI *ini2, void *instance, void *, const void *) { INI::parseBool(ini2, nullptr, &static_cast<Ctx *>(instance)->record->countAsJumpToLocation, nullptr); }
	};
	Ctx ctx = { &record, &audio, this, overlay };
	// RW table 0xBF21F0, row order of the binary
	const FieldParse table[] = {
		{ "Priority", Parsers::priority, nullptr, 0 },
		{ "TimeBetweenEventsMS", Parsers::timeBetween, nullptr, 0 },
		{ "ExpirationTimeMS", Parsers::expiration, nullptr, 0 },
		{ "QuietTimeMS", Parsers::quiet, nullptr, 0 },
		{ "MillisecondsToWaitBeforePlaying", Parsers::wait, nullptr, 0 },
		{ "AlwaysPlayFromHomeBase", Parsers::home, nullptr, 0 },
		{ "SideSound", Parsers::sideSound, nullptr, 0 },
		{ "OtherEvaEventsToBlock", Parsers::otherEvents, nullptr, 0 },
		{ "CountAsJumpToLocation", Parsers::jump, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(&ctx, table);
	record.forwardReference = false; // RW 0x5DC73B: the flag byte is cleared after a parse
	record.defined = true;
}

void EvaEventStore::parsePredefined(INI *ini, const AudioEventInfoStore &audio)
{
	const std::string name = ini->getNextToken();
	const int index = findIndex(name, false);
	if (index < 0 || index >= kPredefinedCount)
	{
		throw INIException(3, "'%s' is not a predefined Eva event name", name.c_str());
	}
	const bool overlay = ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES;
	if (overlay && index == 0)
	{
		throw INIException(3, "You cannot redefine the default Eva event in a map.ini");
	}
	auto &records = overlay ? m_overlay : m_base;
	EvaEventRecord &dest = records[(size_t)index];
	if (index != 0)
	{
		const std::string keepName = dest.name;
		dest = m_base[0]; // RW 0x5DDE3B: the default event's values (the base default, also for a map overlay)
		dest.name = keepName;
		dest.defined = false;
	}
	parseRecord(ini, dest, audio, overlay);
}

void EvaEventStore::parseNew(INI *ini, const AudioEventInfoStore &audio, bool forwardReference)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		throw INIException(3, "Cannot use 'None' as a new Eva event's name");
	}
	const bool overlay = ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES;
	int id = findIndex(name, overlay);
	if (id >= 0 && id < kPredefinedCount)
	{
		throw INIException(3, "'%s' is a predefined Eva event name, and cannot be used as a new event name", name.c_str());
	}
	if (id < 0)
	{
		id = createRecord(name, overlay);
	}
	else if (forwardReference)
	{
		return; // RW 0x5DEEBE: a forward reference of a name that already exists changes nothing
	}
	else if (!overlay)
	{
		// RW 0x5DECCD: an event already defined in Eva.ini cannot be defined again; a forward reference can be filled in
		if (!(m_base[(size_t)id].forwardReference))
		{
			throw INIException(3, "Cannot redefine existing Eva event '%s' in Eva.ini", name.c_str());
		}
	}
	auto &records = overlay ? m_overlay : m_base;
	EvaEventRecord &dest = records[(size_t)id];
	if (forwardReference)
	{
		dest.forwardReference = true;
		return; // the name exists; the block (if any) is not read
	}
	dest = m_base[0];
	dest.name = name;
	dest.defined = false;
	parseRecord(ini, dest, audio, overlay);
}

void EvaEventStore::parseMiscEvaData(INI *ini)
{
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		throw INIException(8, "Cannot override MiscEvaData entries");
	}
	// RW table 0xBF25E0
	const FieldParse table[] = {
		{ "EnemySightedMaxVoicePositionScanRange", INI::parseReal, nullptr, (int)offsetof(MiscEvaData, enemySightedMaxVoicePositionScanRange) },
		{ "EnemyCampDestroyedDamageTimeoutMS", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(MiscEvaData, enemyCampDestroyedDamageTimeoutFrames) },
		{ "FriendlyCampDestroyedDamageTimeoutMS", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(MiscEvaData, friendlyCampDestroyedDamageTimeoutFrames) },
		{ "MaxMillisecondsToKeepJumpToEvents", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(MiscEvaData, maxMillisecondsToKeepJumpToEventsFrames) },
		{ "MaxMillisecondsBeforeResettingLastJumpTo", INI::parseUnsignedInt, nullptr, (int)offsetof(MiscEvaData, maxMillisecondsBeforeResettingLastJumpTo) },
		{ "MinDistanceBetweenJumpToEvents", INI::parseReal, nullptr, (int)offsetof(MiscEvaData, minDistanceBetweenJumpToEvents) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	ini->initFromINI(&m_misc, table);
}

void EvaEventStore::parseScoredKillAnnouncer(INI *ini)
{
	const std::string name = ini->getNextToken();
	ScoredKillEvaAnnouncer *a = nullptr;
	for (ScoredKillEvaAnnouncer &existing : m_announcers)
	{
		if (existing.name == name)
		{
			a = &existing;
			break;
		}
	}
	if (!a)
	{
		m_announcers.emplace_back();
		a = &m_announcers.back();
		a->name = name;
	}
	struct Ctx
	{
		ScoredKillEvaAnnouncer *a;
		const EvaEventStore *store;
	};
	struct Parsers
	{
		// RW 0x5DE588 -> 0x5DE0D8: "None" is -1; an unknown name is an error
		static void evaEvent(INI *ini2, void *instance, void *, const void *)
		{
			Ctx *c = static_cast<Ctx *>(instance);
			const char *token = ini2->getNextToken();
			if (AsciiStringUtil::compareNoCase(token, "None") == 0)
			{
				c->a->evaEvent = -1;
				return;
			}
			const int id = c->store->findIndex(token, false);
			if (id < 0)
			{
				throw INIException(3, "Expected a recognized Eva event name or 'None'; got '%s'", token);
			}
			c->a->evaEvent = id;
		}
		static void byLocal(INI *ini2, void *instance, void *, const void *) { INI::parseBool(ini2, nullptr, &static_cast<Ctx *>(instance)->a->countOnlyKillsByLocalPlayer, nullptr); }
		static void againstLocal(INI *ini2, void *instance, void *, const void *) { INI::parseBool(ini2, nullptr, &static_cast<Ctx *>(instance)->a->countOnlyKillsAgainstLocalPlayer, nullptr); }
		static void minCount(INI *ini2, void *instance, void *, const void *) { INI::parseUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->a->minimumCountForAnnouncement, nullptr); }
		static void maxTime(INI *ini2, void *instance, void *, const void *) { INI::parseDurationUnsignedInt(ini2, nullptr, &static_cast<Ctx *>(instance)->a->maximumTimeForAnnouncementFrames, nullptr); }
		static void filter(INI *ini2, void *instance, void *, const void *) { ParseObjectFilter(ini2, nullptr, &static_cast<Ctx *>(instance)->a->objectFilter, nullptr); }
	};
	Ctx ctx = { a, this };
	// RW table 0xC51540
	const FieldParse table[] = { { "EvaEvent", Parsers::evaEvent, nullptr, 0 }, { "CountOnlyKillsByLocalPlayer", Parsers::byLocal, nullptr, 0 },
		{ "CountOnlyKillsAgainstLocalPlayer", Parsers::againstLocal, nullptr, 0 }, { "MinimumCountForAnnouncement", Parsers::minCount, nullptr, 0 },
		{ "MaximumTimeForAnnouncementMS", Parsers::maxTime, nullptr, 0 }, { "ObjectFilter", Parsers::filter, nullptr, 0 }, { nullptr, nullptr, nullptr, 0 } };
	ini->initFromINI(&ctx, table);
}

std::vector<std::string> EvaEventStore::unresolvedForwardReferences() const
{
	std::vector<std::string> out;
	for (const EvaEventRecord &r : m_base)
	{
		if (r.forwardReference)
		{
			out.push_back(r.name);
		}
	}
	return out;
}
