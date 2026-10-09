// OpenBFME. GPL-3.0.
//
// EVA event definitions (Eva.ini): PredefinedEvaEvent, NewEvaEvent, EvaEventForwardReference, MiscEvaData and the ScoredKillEvaAnnouncer blocks.
//
// RotWK's Eva is NOT ZH's (ZH Eva.cpp: a fixed enum of 53 messages and "EvaEvent" blocks); it is BFME2's name based system, read from the binary
// (RW addresses, caveat S-001):
//   * TheEva (RW 0xDE3670) keeps records of 0x30 bytes in two vectors (base +0x28, map.ini overlay +0x1C) and two name -> index maps (+0x48, +0x34).
//     The first 22 indices are the PREDEFINED events (names at RW 0xBF2168, index 0 is "DefaultEvaEvent"); new events are appended after them.
//   * PredefinedEvaEvent <name> (RW 0x5DE1AA): the name must be one of the 22 ("'%s' is not a predefined Eva event name"); the default event may not
//     be redefined from a map.ini ("You cannot redefine the default Eva event in a map.ini"); every predefined event except the default starts as a
//     copy of the default (RW 0x5DDE3B), then its fields are read (table RW 0xBF21F0).
//   * NewEvaEvent <name> (RW 0x5DEB41): "None" is refused ("Cannot use 'None' as a new Eva event's name"); a predefined name is refused ("'%s' is a
//     predefined Eva event name, and cannot be used as a new event name"); an event already defined in Eva.ini is refused ("Cannot redefine existing
//     Eva event '%s' in Eva.ini"); an EvaEventForwardReference (RW 0x5DEDBC, the same checks, creates the name only) may be filled in later. A new event
//     starts as a copy of the default, then its fields are read. From a map.ini (load type 2) new events go to the overlay and may redefine an overlay event.
//   * record defaults (RW 0x5DDDF5): TimeBetweenEventsMS 20000, ExpirationTimeMS 1500, QuietTimeMS 0, MillisecondsToWaitBeforePlaying 0, Priority 5,
//     AlwaysPlayFromHomeBase No, CountAsJumpToLocation Yes.
//   * Field parsers (table RW 0xBF21F0): Priority / TimeBetweenEventsMS / ExpirationTimeMS / QuietTimeMS unsigned ints (milliseconds, NOT converted to
//     frames); MillisecondsToWaitBeforePlaying through parseDurationUnsignedInt (frames); SideSound opens a nested block {Side, Sound}; OtherEvaEventsToBlock
//     is a list of event names ("None" = -1; an unknown one is "Expected a recognized Eva event name or 'None'; got '%s'", RW 0x5DE0D8).
//   * MiscEvaData (RW 0x5DC743, table 0xBF25E0): refused from a map.ini ("Cannot override MiscEvaData entries", code 8).
// INFERENCE (stop S-247): which name map an event-name list consults while a map.ini loads is decided by the INI's load type (retail tests a field of
// TheEva, RW 0x5DE0FC); the Eva RUNTIME (queueing, expiry, side sound choice, "Player_/Eva event played recently") is not ported yet.

#pragma once

#include "Common/Audio/AudioEventInfo.h"
#include "GameLogic/ObjectFilter.h"

#include <map>
#include <string>
#include <vector>

struct EvaSideSound
{
	std::string side;
	std::string sound; ///< audio event name, "" for NoSound
};

struct EvaEventRecord
{
	std::string name;
	unsigned timeBetweenEventsMS = 20000;
	unsigned expirationTimeMS = 1500;
	unsigned quietTimeMS = 0;
	unsigned millisecondsToWaitBeforePlayingFrames = 0;
	unsigned priority = 5;
	std::vector<EvaSideSound> sideSounds;
	std::vector<int> otherEventsToBlock; ///< event indices (-1 = None)
	bool alwaysPlayFromHomeBase = false;
	bool countAsJumpToLocation = true;
	bool forwardReference = false; ///< created by EvaEventForwardReference and not defined yet
	bool defined = false;
};

struct MiscEvaData
{
	float enemySightedMaxVoicePositionScanRange = 0.0f;
	unsigned enemyCampDestroyedDamageTimeoutFrames = 0;
	unsigned friendlyCampDestroyedDamageTimeoutFrames = 0;
	unsigned maxMillisecondsToKeepJumpToEventsFrames = 0;
	unsigned maxMillisecondsBeforeResettingLastJumpTo = 0;
	float minDistanceBetweenJumpToEvents = 0.0f;
};

struct ScoredKillEvaAnnouncer
{
	std::string name;
	int evaEvent = -1;
	bool countOnlyKillsByLocalPlayer = false;
	bool countOnlyKillsAgainstLocalPlayer = false;
	unsigned minimumCountForAnnouncement = 0;
	unsigned maximumTimeForAnnouncementFrames = 0;
	ObjectFilter objectFilter;
};

class EvaEventStore
{
public:
	enum { kPredefinedCount = 22 };
	static const char *const *predefinedNames(); ///< RW 0xBF2168 (22)

	EvaEventStore();

	// the block handlers; `audio` resolves the SideSound names
	void parsePredefined(INI *ini, const AudioEventInfoStore &audio);
	void parseNew(INI *ini, const AudioEventInfoStore &audio, bool forwardReference);
	void parseMiscEvaData(INI *ini);
	void parseScoredKillAnnouncer(INI *ini);

	// -1 when unknown. `overlay` consults the map.ini overlay's names.
	int findIndex(const std::string &name, bool overlay) const;
	const EvaEventRecord *record(int index, bool overlay) const;
	size_t recordCount(bool overlay) const { return (overlay ? m_overlay : m_base).size(); }
	const MiscEvaData &miscEvaData() const { return m_misc; }
	const std::vector<ScoredKillEvaAnnouncer> &announcers() const { return m_announcers; }
	// undefined forward references left over after loading (a retail data defect, reported)
	std::vector<std::string> unresolvedForwardReferences() const;

	// Eva.ini files in the order TheEva loads them (RW 0x5DE837-0x5DE868)
	static const std::vector<std::string> &loadOrder();

private:
	void parseRecord(INI *ini, EvaEventRecord &record, const AudioEventInfoStore &audio, bool overlay);
	int createRecord(const std::string &name, bool overlay);

	std::vector<EvaEventRecord> m_base;
	std::vector<EvaEventRecord> m_overlay;
	std::map<std::string, int> m_baseNames;    ///< predefined names + new events (RW +0x48), case-sensitive
	std::map<std::string, int> m_overlayNames; ///< predefined names + overlay events (RW +0x34)
	MiscEvaData m_misc;
	std::vector<ScoredKillEvaAnnouncer> m_announcers;
};
