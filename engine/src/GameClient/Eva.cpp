// OpenBFME. GPL-3.0. See Eva.h.

#include "GameClient/Eva.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"

#include <algorithm>
#include <cmath>

namespace
{
double frameMs(unsigned frames)
{
	return (double)frames * 1000.0 / (double)LOGICFRAMES_PER_SECOND;
}
} // namespace

Eva::Eva(AudioIniState &ini, AudioManager &audio) : m_ini(ini), m_audio(audio)
{
	reset();
}

void Eva::reset()
{
	m_pending.clear();
	m_lastPlayed.assign(m_ini.eva.recordCount(false), -1.0);
	m_lastReported.assign(m_ini.eva.recordCount(false), -1.0);
	m_speech = 0;
	m_jumpTo.clear();
	m_jumpIndex = 0;
	m_lastJumpMs = -1.0;
	m_enabled = true;
}

void Eva::setEnabled(bool enabled)
{
	if (enabled == m_enabled)
	{
		return;
	}
	if (!enabled)
	{
		m_pending.clear(); // going dark drops what waited (BFME1 Eva.cpp setEvaEnabled invalidates the pending checks)
	}
	m_enabled = enabled;
}

bool Eva::reportEventByName(const std::string &name, const Coord3D *position, double nowMs)
{
	return reportEvent(m_ini.eva.findIndex(name, false), position, nowMs);
}

bool Eva::reportEvent(int index, const Coord3D *position, double nowMs)
{
	const EvaEventRecord *r = m_ini.eva.record(index, false);
	if (!r || !m_enabled)
	{
		return false;
	}
	++m_counters.reported;
	if (m_lastPlayed.size() < m_ini.eva.recordCount(false))
	{
		m_lastPlayed.resize(m_ini.eva.recordCount(false), -1.0);
		m_lastReported.resize(m_ini.eva.recordCount(false), -1.0);
	}
	const double previousReport = m_lastReported[(size_t)index];
	m_lastReported[(size_t)index] = nowMs;
	const double lastPlayed = m_lastPlayed[(size_t)index];
	if (lastPlayed >= 0.0 && nowMs - lastPlayed < (double)r->timeBetweenEventsMS)
	{
		++m_counters.droppedIdentical;
		return false;
	}
	// QuietTime: the event must not have been reported for QuietTimeMS before this report
	if (r->quietTimeMS > 0 && previousReport >= 0.0 && nowMs - previousReport < (double)r->quietTimeMS)
	{
		++m_counters.droppedQuiet;
		return false;
	}
	for (const Pending &p : m_pending)
	{
		if (p.index == index)
		{
			return false; // already waiting
		}
	}
	Pending p;
	p.index = index;
	p.reportedMs = nowMs;
	p.hasPosition = position != nullptr;
	p.position = position ? *position : Coord3D();
	m_pending.push_back(p);
	if (r->countAsJumpToLocation && position)
	{
		const MiscEvaData &misc = m_ini.eva.miscEvaData();
		bool merged = false;
		for (JumpTo &j : m_jumpTo)
		{
			const float dx = j.position.x - position->x, dy = j.position.y - position->y;
			if (std::sqrt(dx * dx + dy * dy) < misc.minDistanceBetweenJumpToEvents)
			{
				j.position = *position;
				j.atMs = nowMs;
				merged = true;
				break;
			}
		}
		if (!merged)
		{
			m_jumpTo.push_back({ *position, nowMs });
		}
	}
	return true;
}

void Eva::blockOthers(const EvaEventRecord &record, double nowMs)
{
	for (const int other : record.otherEventsToBlock)
	{
		if (other < 0)
		{
			continue;
		}
		const size_t before = m_pending.size();
		m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [&](const Pending &p) { return p.index == other; }), m_pending.end());
		m_counters.droppedBlocked += (unsigned)(before - m_pending.size());
		if ((size_t)other < m_lastPlayed.size())
		{
			m_lastPlayed[(size_t)other] = nowMs;
		}
	}
}

void Eva::update(double nowMs)
{
	// jump-to events past their keep time are forgotten
	const double keep = frameMs(m_ini.eva.miscEvaData().maxMillisecondsToKeepJumpToEventsFrames);
	if (keep > 0.0)
	{
		m_jumpTo.erase(std::remove_if(m_jumpTo.begin(), m_jumpTo.end(), [&](const JumpTo &j) { return nowMs - j.atMs > keep; }), m_jumpTo.end());
		if (m_jumpIndex >= m_jumpTo.size())
		{
			m_jumpIndex = 0;
		}
	}
	// expiry
	for (auto it = m_pending.begin(); it != m_pending.end();)
	{
		const EvaEventRecord *r = m_ini.eva.record(it->index, false);
		const double wait = r ? frameMs(r->millisecondsToWaitBeforePlayingFrames) : 0.0;
		if (r && nowMs - it->reportedMs - wait > (double)r->expirationTimeMS)
		{
			++m_counters.droppedExpired;
			it = m_pending.erase(it);
		}
		else
		{
			++it;
		}
	}
	// Eva never overlaps itself
	if (m_speech && m_audio.isCurrentlyPlaying(m_speech))
	{
		return;
	}
	m_speech = 0;
	// the best ready event: highest priority, ties the one reported first
	auto best = m_pending.end();
	for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
	{
		const EvaEventRecord *r = m_ini.eva.record(it->index, false);
		if (!r || nowMs - it->reportedMs < frameMs(r->millisecondsToWaitBeforePlayingFrames))
		{
			continue;
		}
		if (best == m_pending.end() || r->priority > m_ini.eva.record(best->index, false)->priority)
		{
			best = it;
		}
	}
	if (best == m_pending.end())
	{
		return;
	}
	const Pending chosen = *best;
	m_pending.erase(best);
	const EvaEventRecord &rec = *m_ini.eva.record(chosen.index, false);
	m_lastPlayed[(size_t)chosen.index] = nowMs;
	// AUDIO-2: where it played (the music scripts' IS_NUM_OF_UNITS_..._NEAR_EVA_EVENT_LAST_PLAYED_LOCATION asks)
	if (m_lastPlayedPos.size() < m_lastPlayed.size())
	{
		m_lastPlayedPos.resize(m_lastPlayed.size());
	}
	m_lastPlayedPos[(size_t)chosen.index] = std::make_pair(chosen.hasPosition, chosen.position);
	blockOthers(rec, nowMs);
	const EvaSideSound *side = nullptr;
	for (const EvaSideSound &s : rec.sideSounds)
	{
		if (AsciiStringUtil::compareNoCase(s.side, m_side) == 0)
		{
			side = &s;
			break;
		}
	}
	if (!side || side->sound.empty())
	{
		++m_counters.noSideSound; // "if we can't find the side we want, don't play anything"
		return;
	}
	AudioEventRTS ev(side->sound);
	if (rec.alwaysPlayFromHomeBase && m_homeValid)
	{
		ev.setPosition(m_home);
	}
	else if (chosen.hasPosition)
	{
		ev.setPosition(chosen.position);
	}
	ev.setPlayerIndex(m_localPlayer);
	AudioLog::Scope logScope("eva");
	m_speech = m_audio.addAudioEvent(ev);
	if (m_speech >= AHSV_FirstHandle)
	{
		++m_counters.played;
	}
	else
	{
		m_speech = 0;
	}
}

bool Eva::lastPlayedPosition(int index, Coord3D *position) const
{
	if (index < 0 || (size_t)index >= m_lastPlayedPos.size() || !m_lastPlayedPos[(size_t)index].first)
	{
		return false;
	}
	*position = m_lastPlayedPos[(size_t)index].second;
	return true;
}

bool Eva::playedRecently(int index, double withinMs, double nowMs) const
{
	return index >= 0 && (size_t)index < m_lastPlayed.size() && m_lastPlayed[(size_t)index] >= 0.0 && nowMs - m_lastPlayed[(size_t)index] <= withinMs;
}

bool Eva::nextJumpToLocation(double nowMs, Coord3D *position)
{
	if (m_jumpTo.empty())
	{
		return false;
	}
	const double reset = (double)m_ini.eva.miscEvaData().maxMillisecondsBeforeResettingLastJumpTo;
	if (m_lastJumpMs >= 0.0 && reset > 0.0 && nowMs - m_lastJumpMs > reset)
	{
		m_jumpIndex = 0;
	}
	if (m_jumpIndex >= m_jumpTo.size())
	{
		m_jumpIndex = 0;
	}
	*position = m_jumpTo[m_jumpIndex].position;
	++m_jumpIndex;
	m_lastJumpMs = nowMs;
	return true;
}
