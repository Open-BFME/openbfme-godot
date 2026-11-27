// OpenBFME. GPL-3.0.
// RotWK 2.01 TheScoredKillEvaAnnouncerController (RW 0x824C08 / 0x8256E4 / 0x825995) and ScoredKillTracker (RW 0x961459 / 0x961353 / 0x9613FD).
// Editions: both (RotWK 2.01 RW 0x8256E4 / 0x825995; BFME2 1.06 ScoredKillEvaAnnouncer.cpp / ScoredKillEvaAnnouncerUpdate.cpp / ScoredKillTracker.cpp).

#pragma once

#include "Common/Audio/EvaEvents.h"
#include "Common/INIDataTypes.h"

#include <deque>
#include <functional>
#include <vector>

class GameLogic;
class Object;
class Xfer;

// RW 0x961459 (ctor), RW 0x961353 (accepts), RW 0x9613FD (hookToKeeper), RW 0x9613DB (unhook).
struct ScoredKillTracker
{
	unsigned lifetimeFrames = 0; ///< MaximumTimeForAnnouncementMS converted to frames (+0x04)
	ObjectFilter filter;         ///< ObjectFilter (+0x08)
	unsigned victimPlayerMask = 0xFFFFFu; ///< +0x0C: 1u << localIdx when CountOnlyKillsAgainstLocalPlayer, else 0xFFFFF
	int killerPlayerIndex = -1;  ///< +0x14: owning ScoreKeeper's player index
	std::deque<unsigned> killFrames; ///< +0x18: logic frame of each tracked kill
	Coord3D lastKillPosition{};  ///< +0x20: position of the most recent tracked kill

	bool accepts(GameLogic &logic, const Object &victim) const; ///< RW 0x961353
	void addTrackedKill(unsigned frame, const Coord3D &pos);    ///< friend_addTrackedKill
	void pruneExpired(unsigned currentFrame);                   ///< friend_update: pops front while front + lifetimeFrames < currentFrame
	unsigned count() const { return (unsigned)killFrames.size(); }
	void xfer(Xfer &x);
};

// Runtime instance of one ScoredKillEvaAnnouncer INI block (RW 0x8256E4 / 0x825995).
struct ScoredKillEvaAnnouncerRuntime
{
	ScoredKillEvaAnnouncer config;
	std::vector<ScoredKillTracker> trackers;
	int localPlayerIndex = -1;
	int seenPlayers = 0;

	void resetForLocalPlayer(int localIdx);
	void addMoreTrackersForNewPlayers(int playerCount); ///< RW 0x8256E4
	void xfer(Xfer &x);
};

// TheScoredKillEvaAnnouncerController: evaluates ScoredKillEvaAnnouncer blocks on kills and frame updates.
class ScoredKillEvaAnnouncerController
{
public:
	struct Announcement
	{
		int evaEvent = -1;
		bool hasPosition = false;
		Coord3D position{};
	};

	void loadFromStore(const EvaEventStore &store);
	void clear();

	// Called from ScoreKeeper::addObjectDestroyed (RW 0x79F303) when a kill is scored.
	void onObjectKilled(GameLogic &logic, int killerPlayerIndex, const Object &victim, int localPlayerIndex = -1);

	// RW 0x825995 (ScoredKillEvaAnnouncer::update): prunes expired kills, checks thresholds, returns announcements fired this tick.
	std::vector<Announcement> update(GameLogic &logic, int localPlayerIndex,
	                                 const std::function<void(int evaEvent, const Coord3D *pos)> &reportFn = {});

	void setLocalPlayerIndex(int idx) { m_localPlayerIndex = idx; }
	int localPlayerIndex() const { return m_localPlayerIndex; }

	const std::vector<ScoredKillEvaAnnouncerRuntime> &announcers() const { return m_announcers; }
	void xfer(Xfer &x);

private:
	std::vector<ScoredKillEvaAnnouncerRuntime> m_announcers;
	int m_localPlayerIndex = -1;
};

extern thread_local ScoredKillEvaAnnouncerController *TheScoredKillEvaAnnouncerController;
