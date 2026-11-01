// OpenBFME. GPL-3.0.
// ViewChecks (lane ANTICHEAT-1): see ViewChecks.h.

#include "GameNetwork/ViewChecks.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/OrdersIntoFog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
std::string jsonString(const std::string &s)
{
	std::string out = "\"";
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
		{
			out += '\\';
			out += (char)c;
		}
		else if (c < 0x20)
		{
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		}
		else
		{
			out += (char)c;
		}
	}
	return out + "\"";
}

std::string jsonNumber(double v)
{
	if (!std::isfinite(v))
	{
		return "null"; // lane ANTICHEAT-1 (Sol r3): a client-reported value can be anything; the report stays valid JSON
	}
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.6f", v);
	return buf;
}
} // namespace

ViewCheck::ViewCheck(GameLogic &logic, const std::vector<ReplayFile::View> &views, const std::vector<int> &slotPlayerIndex, const OrdersIntoFog &fog,
	Settings settings)
	: m_logic(logic)
	, m_views(views)
	, m_fog(fog)
	, m_settings(settings)
{
	std::stable_sort(m_views.begin(), m_views.end(), [](const ReplayFile::View &a, const ReplayFile::View &b) { return a.frame < b.frame; });
	for (size_t slot = 0; slot < slotPlayerIndex.size(); ++slot)
	{
		if (slotPlayerIndex[slot] < 0)
		{
			continue;
		}
		Tracked t;
		t.result.player = slotPlayerIndex[slot];
		t.result.slot = (int)slot;
		if (const Player *p = logic.players().getNthPlayer(t.result.player))
		{
			t.result.name = p->getPlayerName();
		}
		m_tracked.push_back(std::move(t));
	}
}

ViewCheck::ViewCheck(GameLogic &logic, const std::vector<ReplayFile::View> &views, const std::vector<int> &slotPlayerIndex, const OrdersIntoFog &fog)
	: ViewCheck(logic, views, slotPlayerIndex, fog, Settings{})
{
}

void ViewCheck::abstain(const std::string &reason)
{
	if (m_abstain.empty())
	{
		m_abstain = reason;
	}
}

ObjectID ViewCheck::hiddenEnemyUnder(int player, float x, float y, UnsignedInt frame, Event &e) const
{
	const ShroudManager *shroud = m_logic.shroud();
	const Player *viewer = m_logic.players().getNthPlayer(player);
	if (!shroud || !viewer)
	{
		return INVALID_ID;
	}
	const float r2 = m_settings.radius * m_settings.radius;
	float best = r2;
	ObjectID found = INVALID_ID;
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *owner = o->getControllingPlayer();
		if (!owner || owner == viewer || o->isEffectivelyDead() || viewer->getRelationship(owner) != ENEMIES)
		{
			continue;
		}
		const float dx = o->getPosition()->x - x, dy = o->getPosition()->y - y;
		const float d2 = dx * dx + dy * dy;
		if (d2 > best)
		{
			continue;
		}
		const ObjectShroudStatus st = shroud->clientObjectStatus(*o, player);
		const bool hidden = st == OBJECTSHROUD_SHROUDED ||
			((st == OBJECTSHROUD_CLEAR || st == OBJECTSHROUD_PARTIAL_CLEAR) && InvisibilityManager::isStealthedAndUndetected(*o, viewer));
		if (!hidden)
		{
			continue;
		}
		const long long last = m_fog.lastVisibleFrame(player, o->getID());
		if (last >= 0 && last + (long long)m_settings.recentFrames >= (long long)frame)
		{
			continue; // seen moments ago: looking where it was is ordinary
		}
		best = d2;
		found = o->getID();
		e.target = found;
		e.targetTemplate = o->getTemplate() ? o->getTemplate()->getName() : std::string();
		e.targetOwner = owner->getPlayerIndex();
	}
	return found;
}

void ViewCheck::beforeFrame(UnsignedInt frame)
{
	for (; m_next < m_views.size() && m_views[m_next].frame <= frame; ++m_next)
	{
		const ReplayFile::View &v = m_views[m_next];
		// Sol r3: a client can report anything: a view with a value that is not a finite number is counted and ignored (it never stands for the pointer)
		const bool valid = std::isfinite(v.cameraX) && std::isfinite(v.cameraY) && std::isfinite(v.cursorX) && std::isfinite(v.cursorY) &&
			std::fabs(v.cursorX) < 1.0e7f && std::fabs(v.cursorY) < 1.0e7f;
		for (Tracked &t : m_tracked)
		{
			if (t.result.slot == v.slot)
			{
				++t.result.views;
				if (valid)
				{
					t.latest = &v;
				}
				else
				{
					++t.result.invalidViews;
				}
			}
		}
	}
	for (Tracked &t : m_tracked)
	{
		Event candidate;
		const ReplayFile::View *v = t.latest;
		const bool live = v && (v->flags & 1) && v->frame + m_settings.staleFrames >= frame;
		ObjectID id = INVALID_ID;
		if (live)
		{
			candidate.x = v->cursorX;
			candidate.y = v->cursorY;
			id = hiddenEnemyUnder(t.result.player, v->cursorX, v->cursorY, frame, candidate);
		}
		if (id != INVALID_ID)
		{
			if (!t.hovering)
			{
				t.hovering = true;
				t.hoverStart = frame;
				t.hoverFrames = 0;
				t.pending = candidate;
				t.pending.frame = frame;
			}
			++t.hoverFrames;
			continue;
		}
		if (t.hovering && t.hoverFrames >= m_settings.hoverFrames)
		{
			t.pending.frames = t.hoverFrames;
			t.result.events.push_back(t.pending);
		}
		t.hovering = false;
	}
}

std::vector<ViewCheck::PlayerResult> ViewCheck::results() const
{
	std::vector<PlayerResult> out;
	for (const Tracked &t : m_tracked)
	{
		PlayerResult r = t.result;
		if (t.hovering && t.hoverFrames >= m_settings.hoverFrames)
		{
			Event e = t.pending;
			e.frames = t.hoverFrames;
			r.events.push_back(e); // still hovering when the game ended
		}
		UnsignedInt first = 0;
		for (Event &e : r.events)
		{
			if (r.encounters == 0 || e.frame > first + m_settings.encounterFrames)
			{
				first = e.frame;
				++r.encounters;
			}
			e.encounter = r.encounters;
		}
		out.push_back(std::move(r));
	}
	return out;
}

std::string ViewCheck::json(const std::string &gameId, const std::string &crcParity, UnsignedInt frames) const
{
	std::string s = "{\"v\":1,\"detector\":\"D2-view-focus\",\"detector_version\":1";
	s += ",\"game_id\":" + (gameId.empty() ? std::string("null") : jsonString(gameId));
	s += ",\"status\":" + jsonString(m_abstain.empty() ? "ok" : "abstained");
	s += ",\"abstain_reason\":" + (m_abstain.empty() ? std::string("null") : jsonString(m_abstain));
	s += ",\"frames\":" + std::to_string(frames) + ",\"crc_parity\":" + jsonString(crcParity);
	s += ",\"settings\":{\"hover_frames\":" + std::to_string(m_settings.hoverFrames) + ",\"recent_frames\":" + std::to_string(m_settings.recentFrames) +
		 ",\"radius\":" + jsonNumber(m_settings.radius) + "}";
	s += ",\"players\":[";
	const std::vector<PlayerResult> rs = results();
	for (size_t i = 0; i < rs.size(); ++i)
	{
		const PlayerResult &r = rs[i];
		const std::string band = !m_abstain.empty() ? "abstained" : r.events.empty() ? "none" : "review";
		s += (i ? "," : "") + std::string("{\"player_index\":") + std::to_string(r.player) + ",\"slot\":" + std::to_string(r.slot) +
			 ",\"name\":" + jsonString(r.name) + ",\"band\":" + jsonString(band) + ",\"score\":" + jsonNumber(r.events.empty() ? 0.0 : 0.5) +
			 ",\"encounters\":" + std::to_string(r.encounters) + ",\"views\":" + std::to_string(r.views) + ",\"invalid_views\":" + std::to_string(r.invalidViews) + ",\"events\":[";
		for (size_t k = 0; k < r.events.size(); ++k)
		{
			const Event &e = r.events[k];
			s += (k ? "," : "") + std::string("{\"frame\":") + std::to_string(e.frame) + ",\"time_s\":" + jsonNumber(e.frame / 5.0) +
				 ",\"command\":\"VIEW\",\"reason\":\"hover_hidden\",\"frames\":" + std::to_string(e.frames) + ",\"target_id\":" + std::to_string(e.target) +
				 ",\"target_template\":" + jsonString(e.targetTemplate) + ",\"target_owner_index\":" + std::to_string(e.targetOwner) +
				 ",\"x\":" + jsonNumber(e.x) + ",\"y\":" + jsonNumber(e.y) + ",\"encounter\":" + std::to_string(e.encounter) + "}";
		}
		s += "]}";
	}
	return s + "]}";
}
