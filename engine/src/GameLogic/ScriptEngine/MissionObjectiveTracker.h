// OpenBFME. GPL-3.0.
//
// MissionObjectiveTracker (lane CAMPUI-1): RotWK's TheMissionObjectiveTracker (subsystem "TheMissionObjectiveTracker", created RW 0x63AD4F, global
// RW 0xDE8C94): the map's mission objectives, read from map.ini's MissionObjectiveList and changed by the map scripts. A 1:1 port (no BFME2 / ZH donor:
// ZH keeps its objectives as the briefing text of InGameUI, BFME2 1.06 has the same tracker).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; research notes of lane CAMPUI-1):
//   * the tracker + 0x10 holds the list (a 0x10-byte object, vtable RW 0xC538A8, ctor RW 0x8363D0) whose + 4 / + 8 are a std::vector of 8-byte records
//     (ctor RW 0x835F4F): + 0 the label (AsciiString, a game text label: SCRIPT:ANGAngmarObjectiveText_01), + 4 bonus (0), + 5 shown (**1**: every
//     objective starts shown), + 6 completed (0). There is no "failed" state.
//   * block MissionObjectiveList (RW 0x836497): only in a load of type 2 (INI_LOAD_CREATE_OVERRIDES: map.ini / solo.ini); type 5 (a reload) is ignored; any
//     other type throws "A MissionObjectiveList may only appear in a map.ini file.". A new list is made and parsed with the field table RW 0xC537E8:
//     MissionObjectiveTag (RW 0x836792: every token of the line a new record with that label), BonusMissionObjectiveTag (RW 0x8367EB: the same, then
//     bonus = 1 on the records it added). The list is installed only when the tracker has none (the first list of a map wins; a second is deleted).
//   * reset (the tracker's vslot RW 0x835E03) deletes the list (a new game starts without one).
//   * the script actions (executeAction RW 0x7CAFA5) pass their 1-based parameter minus one: SHOW_MISSION_OBJECTIVE RW 0x7BDE35 / HIDE RW 0x7BDE57 ->
//     setShown RW 0x835FAE; MARK_MISSION_OBJECTIVE_COMPLETED RW 0x7BDE79 / MARK_..._NOT_COMPLETED RW 0x7BDE9B -> setCompleted RW 0x836097. An index
//     outside the list (RW 0x835E24) is ignored. Completing does not change `shown` (a hidden objective may be completed and stays hidden).
//   * setShown plays the local player's PlayerTemplate ObjectiveAddedSound (+ 0x1A8) on a false -> true change; setCompleted plays ObjectiveCompletedSound
//     (+ 0x1AC) on a false -> true change when TheGameLogic's frame (+ 0x40) is above 3 (the setup frame's marks are silent). Sound only: here the logic
//     records a client request MISSION_OBJECTIVE_SOUND (the event name) for the client to play.
//   * ENABLE / DISABLE_OBJECTIVES_SCREEN (RW 0x7BDF1C -> RW 0x8E89BD): the objectives screen's enabled flag (default 1).
//   * the score's objectives term (RW 0x79DE90): the objectives that are shown AND bonus; it answers the completed ones, the total through its out argument.
//   * the objectives screen's rows (RW 0x8E8A8A): the n-th SHOWN objective in list order (main then bonus, as the file lists them).
//   * save (GameState RW 0x6DF73E): CHUNK_MissionObjectives = the tracker's snapshot RW 0x8363F2: skipped by a CRC xfer; version 1, bool "has a list",
//     then (a load makes a new list) the list's xfer RW 0x836835: version 1, the vector RW 0x83669A ("std::vector" label, the count, a load needs an empty
//     vector: "Vector must be empty on load"), each record RW 0x835E3F: version 1, label, bonus, shown, completed. CHUNK_ObjectivesMenu = RW 0x8227E9
//     (INFERENCE from the research's reading, the save map names it SpellStore::xfer: S-4021): version 1, the enabled flag.
// Not hashed (retail skips both blocks in its CRC xfer): the objectives never decide a game, the map scripts do.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;
class Xfer;

struct MissionObjective
{
	std::string label;      ///< + 0
	bool bonus = false;     ///< + 4
	bool shown = true;      ///< + 5 (RW 0x835F4F: 1)
	bool completed = false; ///< + 6
};

class MissionObjectiveTracker
{
public:
	// RW 0x835E03: no list
	void reset();
	bool hasList() const { return m_hasList; }
	const std::vector<MissionObjective> &objectives() const { return m_objectives; }
	// the map's list (RetailObjectWorld's map.ini parse): installed only when the tracker has none (RW 0x836497)
	void installList(const std::vector<MissionObjective> &list);

	// RW 0x835FAE / 0x836097 with the 0-based index (an index outside the list: false, nothing changes); true when the flag went from false to true (the
	// caller plays the sound)
	bool setShown(int index, bool shown);
	bool setCompleted(int index, bool completed);
	// RW 0x79DE90: the shown bonus objectives; returns the completed ones, `total` the shown ones
	int countShownBonus(int &total) const;
	// RW 0x8E8A8A: the n-th (0-based) shown objective, null when there are fewer
	const MissionObjective *nthShown(int n) const;

	// RW 0x8E89BD: the objectives screen's enabled flag (ENABLE / DISABLE_OBJECTIVES_SCREEN)
	bool screenEnabled() const { return m_screenEnabled; }
	void setScreenEnabled(bool on) { m_screenEnabled = on; }

	// CHUNK_MissionObjectives (RW 0x8363F2 -> 0x836835 -> 0x83669A -> 0x835E3F) and CHUNK_ObjectivesMenu (RW 0x8227E9)
	void xfer(Xfer &x);
	void xferMenu(Xfer &x);

	// the MissionObjectiveList block (RW 0x836497 / 0xC537E8 / 0x836792 / 0x8367EB): parses into `list` (cleared first); a load type other than 2 or 5
	// throws retail's error, a type 5 load skips the block's lines
	static void parseList(INI *ini, std::vector<MissionObjective> &list, bool &parsed);

private:
	bool m_hasList = false;
	std::vector<MissionObjective> m_objectives;
	bool m_screenEnabled = true; // RW 0x8E8811 + 4: 1
};
