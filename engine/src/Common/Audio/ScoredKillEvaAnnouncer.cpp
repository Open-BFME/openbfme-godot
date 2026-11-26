// OpenBFME. GPL-3.0.
// See Common/Audio/ScoredKillEvaAnnouncer.h.

// RW 0x9616C2 (ScoredKillTracker copy constructor)
#include "Common/Audio/ScoredKillEvaAnnouncer.h"

#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/SnapshotXfer.h"

// RW 0x8251FA (ScoredKillEvaAnnouncer constructor), RW 0x82526C (~ScoredKillEvaAnnouncer)
thread_local ScoredKillEvaAnnouncerController *TheScoredKillEvaAnnouncerController = nullptr;

// RW 0x961353 (ScoredKillTracker::accepts; DONOR BFME2 ScoredKillTracker.cpp:143)
bool ScoredKillTracker::accepts(GameLogic &logic, const Object &victim) const
{
	const Player *victimPlayer = victim.getControllingPlayer();
	if (victimPlayer)
	{
		const int idx = victimPlayer->getPlayerIndex();
		if (idx < 0 || idx >= 20 || (victimPlayerMask & (1u << (unsigned)idx)) == 0)
		{
			return false;
		}
	}
	else if (victimPlayerMask != 0xFFFFFu) // RW 0x961396
	{
		return false;
	}
	const Player *owner = killerPlayerIndex >= 0 ? logic.players().getNthPlayer(killerPlayerIndex) : nullptr;
	if (!ObjectFilterMatch::isValid(&filter))
	{
		return true;
	}
	return ObjectFilterMatch::allows(logic, filter, victim, owner); // RW 0x96138D
}

void ScoredKillTracker::addTrackedKill(unsigned frame, const Coord3D &pos)
{
	killFrames.push_back(frame);
	lastKillPosition = pos;
}

// DONOR BFME2 ScoredKillTracker.cpp:209 (friend_update): pops front while front + lifetimeFrames < currentFrame
void ScoredKillTracker::pruneExpired(unsigned currentFrame)
{
	while (!killFrames.empty() && killFrames.front() + lifetimeFrames < currentFrame)
	{
		killFrames.pop_front();
	}
}

void ScoredKillTracker::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	x.xferUnsignedInt(&lifetimeFrames);
	x.xferUnsignedInt(&victimPlayerMask);
	x.xferInt(&killerPlayerIndex);
	std::uint32_t n = (std::uint32_t)killFrames.size();
	x.xferUnsignedInt(&n);
	if (x.getXferMode() == XFER_LOAD)
	{
		killFrames.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			unsigned f = 0;
			x.xferUnsignedInt(&f);
			killFrames.push_back(f);
		}
	}
	else
	{
		for (unsigned &f : killFrames)
		{
			x.xferUnsignedInt(&f);
		}
	}
	x.xferReal(&lastKillPosition.x);
	x.xferReal(&lastKillPosition.y);
	x.xferReal(&lastKillPosition.z);
}

void ScoredKillEvaAnnouncerRuntime::resetForLocalPlayer(int localIdx)
{
	trackers.clear();
	localPlayerIndex = localIdx;
	seenPlayers = 0;
}

// RW 0x8256E4 (ScoredKillEvaAnnouncer::addMoreTrackersForNewPlayers)
void ScoredKillEvaAnnouncerRuntime::addMoreTrackersForNewPlayers(int playerCount)
{
	unsigned mask = 0xFFFFFu;
	if (config.countOnlyKillsAgainstLocalPlayer) // RW 0x8256F5 (+0x19)
	{
		if (localPlayerIndex < 0 || localPlayerIndex >= 20)
		{
			return;
		}
		mask = 1u << (unsigned)localPlayerIndex; // RW 0x82570C
	}
	if (config.countOnlyKillsByLocalPlayer) // RW 0x825715 (+0x18)
	{
		if (localPlayerIndex < 0)
		{
			return;
		}
		if (trackers.empty())
		{
			ScoredKillTracker t;
			t.lifetimeFrames = config.maximumTimeForAnnouncementFrames; // +0x10
			t.filter = config.objectFilter;                             // +0x14
			t.victimPlayerMask = mask;
			t.killerPlayerIndex = localPlayerIndex;
			trackers.push_back(std::move(t));
		}
	}
	else
	{
		for (int i = seenPlayers; i < playerCount; ++i) // RW 0x825768
		{
			ScoredKillTracker t;
			t.lifetimeFrames = config.maximumTimeForAnnouncementFrames;
			t.filter = config.objectFilter;
			t.victimPlayerMask = mask;
			t.killerPlayerIndex = i;
			trackers.push_back(std::move(t));
		}
	}
	seenPlayers = playerCount;
}

void ScoredKillEvaAnnouncerRuntime::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	x.xferInt(&localPlayerIndex);
	x.xferInt(&seenPlayers);
	x.xferVectorSize(trackers, 4);
	for (ScoredKillTracker &t : trackers)
	{
		t.xfer(x);
	}
}

void ScoredKillEvaAnnouncerController::loadFromStore(const EvaEventStore &store)
{
	m_announcers.clear();
	for (const ScoredKillEvaAnnouncer &cfg : store.announcers())
	{
		ScoredKillEvaAnnouncerRuntime rt;
		rt.config = cfg;
		m_announcers.push_back(std::move(rt));
	}
}

void ScoredKillEvaAnnouncerController::clear()
{
	m_announcers.clear();
	m_localPlayerIndex = -1;
}

void ScoredKillEvaAnnouncerController::onObjectKilled(GameLogic &logic, int killerPlayerIndex, const Object &victim, int localPlayerOverride)
{
	const int localIdx = localPlayerOverride >= 0 ? localPlayerOverride : m_localPlayerIndex;
	const int playerCount = logic.players().getPlayerCount();
	const unsigned frame = logic.getFrame();
	const Coord3D *posPtr = victim.getPosition();
	const Coord3D pos = posPtr ? *posPtr : Coord3D{};

	for (ScoredKillEvaAnnouncerRuntime &rt : m_announcers)
	{
		if (rt.localPlayerIndex != localIdx || rt.seenPlayers > playerCount)
		{
			rt.resetForLocalPlayer(localIdx);
		}
		if (rt.seenPlayers < playerCount)
		{
			rt.addMoreTrackersForNewPlayers(playerCount);
		}
		for (ScoredKillTracker &t : rt.trackers)
		{
			if (t.killerPlayerIndex == killerPlayerIndex && t.accepts(logic, victim))
			{
				t.addTrackedKill(frame, pos);
			}
		}
	}
}

// RW 0x825995 (ScoredKillEvaAnnouncer::update)
std::vector<ScoredKillEvaAnnouncerController::Announcement> ScoredKillEvaAnnouncerController::update(
	GameLogic &logic, int localPlayerIndex, const std::function<void(int evaEvent, const Coord3D *pos)> &reportFn)
{
	m_localPlayerIndex = localPlayerIndex;
	const int playerCount = logic.players().getPlayerCount();
	const unsigned frame = logic.getFrame();
	std::vector<Announcement> fired;

	for (ScoredKillEvaAnnouncerRuntime &rt : m_announcers)
	{
		if (rt.localPlayerIndex != localPlayerIndex || rt.seenPlayers > playerCount) // RW 0x8259A6 / 0x8259B5
		{
			rt.resetForLocalPlayer(localPlayerIndex);
			rt.addMoreTrackersForNewPlayers(playerCount);
			continue;
		}
		if (rt.seenPlayers < playerCount) // RW 0x8259BD
		{
			rt.addMoreTrackersForNewPlayers(playerCount);
		}

		unsigned total = 0;
		Coord3D point{ 0.0f, 0.0f, 0.0f };
		for (ScoredKillTracker &t : rt.trackers)
		{
			t.pruneExpired(frame);
			if (total >= rt.config.minimumCountForAnnouncement) // RW 0x8259EE (+0x0C)
			{
				break;
			}
			const unsigned kills = t.count();
			total += kills;
			if (kills > 0) // RW 0x8259F9
			{
				point = t.lastKillPosition;
			}
		}

		if (total >= rt.config.minimumCountForAnnouncement && rt.config.evaEvent >= 0) // RW 0x825A14
		{
			const bool hasPos = (point.x != 0.0f || point.y != 0.0f || point.z != 0.0f); // RW 0x825A2F
			fired.push_back({ rt.config.evaEvent, hasPos, point });
			if (reportFn)
			{
				reportFn(rt.config.evaEvent, hasPos ? &point : nullptr);
			}
		}
	}
	return fired;
}

void ScoredKillEvaAnnouncerController::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	x.xferInt(&m_localPlayerIndex);
	x.xferVectorSize(m_announcers, 4);
	for (ScoredKillEvaAnnouncerRuntime &rt : m_announcers)
	{
		rt.xfer(x);
	}
}
