// OpenBFME. GPL-3.0.
//
// LargeGroupAudio.ini: the maps of the Large Group Audio system (extra looping sounds under any big action) and the list of known unused keys.
//
// Port from the RotWK binary (RW addresses, caveat S-001): LargeGroupAudioMap RW 0x60DD01 (table RW 0xC4C5D0), its nested Sound block RW 0x7EF63F
// (table RW 0xC842C8: Sound, Key, Duck), LargeGroupAudioUnusedKnownKeys RW 0x60DADD (table 0xBFA174: Key). Semantics read from the code:
//   * a map keeps one object per name in TheLargeGroupAudio (RW 0xDE3BE8). A second block of the same name re-parses the SAME map (fields it does not
//     name keep their values). From a map.ini (load type 2) the map is replaced by a copy that is edited instead, and the original is kept to be
//     restored when the map unloads; a name that does not exist is added and removed again on unload (resetMapOverrides()).
//   * `Sound [name]` opens a nested block; within one map two Sound blocks may not share a name ("LargeGroupAudio: You cannot use the same name(%s) for
//     two Sound blocks within the same LargeGroupAudioMap(%s)") except from a map.ini (load types 2 and 5), where the later block replaces the earlier.
//     INFERENCE: unnamed blocks (the retail file has several per map) take generated names so they never clash.
//   * Key adds names (any number of lines), Sound names the audio event (NoSound clears, unknown: "Invalid Sound '%s'"),
//     Duck is `AudioMap:<name> Sound:<name> Multiplier:<percent>` (errors "Expected %s:<name> next", "Expected %s:<volume percent> next").
//   * Size real; MaximumAudioSpeed velocity (units per second * 0.2 = per logic frame, RW 0x73A4B6); StartThreshold / StopThreshold unsigned shorts;
//     HandOffModeDuration milliseconds converted to frames (RW 0x73B071, 0.005f); Required/ExcludedModelConditionFlags and Required/ExcludedObjectStatusBits
//     are the shared bit flag lists; IgnoreStealthedUnits bool (the INI documents the default as Yes).
// The RUNTIME (the per-cell counting of LargeGroupAudioUpdate modules, hand-off, ducking) needs live objects and is a later lane (stop S-248).

#pragma once

#include "Common/Audio/AudioEventInfo.h"
#include "GameLogic/BitFlags.h"

#include <string>
#include <vector>

struct LargeGroupAudioDuck
{
	std::string audioMap;
	std::string sound;
	float multiplier = 1.0f;
};

struct LargeGroupAudioSound
{
	std::string name;
	std::vector<std::string> keys;
	std::string sound; ///< audio event, "" = NoSound
	std::vector<LargeGroupAudioDuck> ducks;
};

struct LargeGroupAudioMap
{
	std::string name;
	float size = 0.0f;
	float maximumAudioSpeed = 0.0f; ///< world units per logic frame
	std::vector<LargeGroupAudioSound> sounds;
	ModelConditionMask requiredModelConditionFlags{};
	ModelConditionMask excludedModelConditionFlags{};
	ObjectStatusMaskType requiredObjectStatusBits{};
	ObjectStatusMaskType excludedObjectStatusBits{};
	unsigned short startThreshold = 0;
	unsigned short stopThreshold = 0;
	unsigned handOffModeDurationFrames = 0;
	bool ignoreStealthedUnits = true;
};

class LargeGroupAudioStore
{
public:
	void parseMap(INI *ini, const AudioEventInfoStore &audio);
	void parseUnusedKnownKeys(INI *ini);
	const LargeGroupAudioMap *find(const std::string &name) const;
	const std::vector<LargeGroupAudioMap> &maps() const { return m_maps; }
	const std::vector<std::string> &unusedKnownKeys() const { return m_unusedKnownKeys; }
	// a map.ini unloads: the overridden maps are restored, the added ones removed
	void resetMapOverrides();

	static const std::vector<std::string> &loadOrder(); ///< RW 0x60D70E
	size_t pendingOverrides() const { return m_savedOriginals.size() + m_mapAdded.size(); }

private:
	std::vector<LargeGroupAudioMap> m_maps;
	std::vector<std::string> m_unusedKnownKeys;
	std::vector<LargeGroupAudioMap> m_savedOriginals; ///< maps replaced by a map.ini edit
	std::vector<std::string> m_mapAdded;
	int m_autoName = 0;
};
