// OpenBFME. GPL-3.0.
// OrdersIntoFog (lane ANTICHEAT-1): see OrdersIntoFog.h.

#include "GameNetwork/OrdersIntoFog.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace
{
// the confidence of one event: never seen is the engine's own seen byte at 0 and no sample; a hidden unit grows with the time it was out of sight beyond
// the window (30 s more -> the top); a stealthed target's cells were clear (the click had a place to land) but the object was not drawn: review only
double confidenceOf(OrdersIntoFog::Reason r, long long gapFrames)
{
	switch (r)
	{
	case OrdersIntoFog::Reason::NeverSeen:
		return 0.97;
	case OrdersIntoFog::Reason::HiddenUnit:
		if (gapFrames < 0)
		{
			return 0.6; // seen before the check began: how long ago is unknown
		}
		return 0.6 + 0.35 * std::min(1.0, (double)gapFrames / 150.0);
	case OrdersIntoFog::Reason::Stealthed:
		return 0.6; // below the alert's 0.9: review only (a disguise or a detection the client showed earlier than the logic is not ruled out yet)
	}
	return 0.0;
}

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

const char *OrdersIntoFog::reasonName(Reason r)
{
	switch (r)
	{
	case Reason::NeverSeen:
		return "never_seen";
	case Reason::HiddenUnit:
		return "hidden_unit";
	case Reason::Stealthed:
		return "stealthed";
	}
	return "?";
}

int OrdersIntoFog::targetArgument(int messageType)
{
	switch (messageType)
	{
	case MSG_DO_ATTACK_OBJECT:       // { objectID victim }
	case MSG_DO_FORCE_ATTACK_OBJECT: // { objectID victim, location }
	case MSG_COMBATDROP_AT_OBJECT:   // { objectID target }
	case MSG_CAPTUREBUILDING:        // { objectID building }
		return 0;
	case MSG_DO_WEAPON_AT_OBJECT:         // { int slot, objectID target, int shots }
	case MSG_DO_SPECIAL_POWER_AT_OBJECT:  // { int power, objectID target, int options, objectID source }
		return 1;
	case MSG_DO_SPECIAL_POWER_AT_LOCATION: // { int power, location, objectID target or NONE, int options, objectID source }
		return 2;
	default:
		return -1;
	}
}

OrdersIntoFog::OrdersIntoFog(GameLogic &logic, std::vector<int> players, Settings settings)
	: m_logic(logic)
	, m_settings(settings)
{
	std::sort(players.begin(), players.end());
	players.erase(std::unique(players.begin(), players.end()), players.end());
	for (int p : players)
	{
		Tracked t;
		t.player = p;
		t.result.player = p;
		if (const Player *pl = m_logic.players().getNthPlayer(p))
		{
			t.result.name = pl->getPlayerName();
		}
		m_tracked.push_back(std::move(t));
	}
}

OrdersIntoFog::OrdersIntoFog(GameLogic &logic, std::vector<int> players)
	: OrdersIntoFog(logic, std::move(players), Settings{})
{
}

OrdersIntoFog::Tracked *OrdersIntoFog::trackedFor(int player)
{
	for (Tracked &t : m_tracked)
	{
		if (t.player == player)
		{
			return &t;
		}
	}
	return nullptr;
}

long long OrdersIntoFog::lastVisibleFrame(int player, ObjectID object) const
{
	for (const Tracked &t : m_tracked)
	{
		if (t.player == player)
		{
			const auto it = t.lastVisible.find(object);
			return it == t.lastVisible.end() ? -1 : (long long)it->second;
		}
	}
	return -1;
}

void OrdersIntoFog::setSlot(int player, int slot)
{
	if (Tracked *t = trackedFor(player))
	{
		t->result.slot = slot;
	}
}

void OrdersIntoFog::abstain(const std::string &reason)
{
	if (m_abstain.empty())
	{
		m_abstain = reason;
	}
}

void OrdersIntoFog::beforeFrame(UnsignedInt frame, const std::vector<GameMessage> &commands)
{
	// the commands of batch `frame` run on the state the logic is in now (GameLogic's commandList row comes before every update of the frame); the samples
	// are labelled with the logic's own frame counter, the events with the batch frame (the replay's frame numbers)
	const UnsignedInt state = m_logic.getFrame();
	if (m_sampledAny && (state < m_lastSample || frame < m_lastBatch))
	{
		abstain("frame order: batch " + std::to_string(frame) + " offered on state " + std::to_string(state) + " after state " + std::to_string(m_lastSample));
		return;
	}
	m_lastBatch = frame;
	if (!m_sampledAny || m_lastSample != state)
	{
		m_lastSample = state;
		sample(frame); // labelled with the batch that runs on this state
	}
	for (const GameMessage &m : commands)
	{
		judge(frame, m);
	}
}

void OrdersIntoFog::sample(UnsignedInt label)
{
	m_sampledAny = true;
	++m_samples;
	const ShroudManager *shroud = m_logic.shroud();
	if (!shroud)
	{
		abstain("the game has no shroud manager");
		return;
	}
	PlayerList &list = m_logic.players();
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *owner = o->getControllingPlayer();
		if (!owner || o->isEffectivelyDead())
		{
			continue;
		}
		for (Tracked &t : m_tracked)
		{
			const Player *viewer = list.getNthPlayer(t.player);
			if (!viewer || viewer == owner || viewer->getRelationship(owner) != ENEMIES)
			{
				continue;
			}
			const ObjectShroudStatus st = shroud->clientObjectStatus(*o, t.player);
			// no shroud record or ALWAYS_VISIBLE answer CLEAR (RW 0x68D8F7): taken as seen (the check abstains rather than flags)
			const bool cellsVisible = st == OBJECTSHROUD_CLEAR || st == OBJECTSHROUD_PARTIAL_CLEAR;
			if (cellsVisible && !InvisibilityManager::isStealthedAndUndetected(*o, viewer))
			{
				t.lastVisible[o->getID()] = label;
			}
		}
	}
}

bool OrdersIntoFog::isSelection(int messageType)
{
	// the client's selection of enemy objects goes over the network too (SelectionXlat: a click picks only what it draws, a drag box leaves out
	// enemies FOGGED or worse, S-288): every object such a message names is judged like an order's target
	return messageType == MSG_CREATE_SELECTED_GROUP || messageType == MSG_CREATE_SELECTED_GROUP_NO_SOUND ||
		messageType == MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE;
}

void OrdersIntoFog::judge(UnsignedInt batch, const GameMessage &m)
{
	const bool selection = isSelection(m.getType());
	const int arg = targetArgument(m.getType());
	if (arg < 0 && !selection)
	{
		return;
	}
	Tracked *t = trackedFor(m.getPlayerIndex());
	if (!t)
	{
		return;
	}
	if (selection)
	{
		// { bool createNewGroup, object ids }
		for (size_t i = 1; i < m.getArgumentCount(); ++i)
		{
			const GameMessageArgument *a = m.getArgument(i);
			if (a->type == ARGUMENTDATATYPE_OBJECTID && a->objectID != INVALID_ID)
			{
				judgeTarget(batch, m, a->objectID, *t);
			}
		}
		return;
	}
	const GameMessageArgument *a = m.getArgument((size_t)arg);
	if (!a || a->type != ARGUMENTDATATYPE_OBJECTID)
	{
		if (m.getType() != MSG_DO_SPECIAL_POWER_AT_LOCATION)
		{
			++t->result.abstained; // a malformed order (the dispatcher reports it too)
		}
		return;
	}
	if (a->objectID == INVALID_ID)
	{
		return; // a power at a location with no object
	}
	judgeTarget(batch, m, a->objectID, *t);
}

void OrdersIntoFog::judgeTarget(UnsignedInt batch, const GameMessage &m, ObjectID id, Tracked &tracked)
{
	Tracked *t = &tracked;
	PlayerResult &res = t->result;
	const Object *target = m_logic.findObjectByID(id);
	if (!target)
	{
		++res.stale;
		return;
	}
	const Player *viewer = m_logic.players().getNthPlayer(t->player);
	const Player *owner = target->getControllingPlayer();
	if (!viewer || !owner || owner == viewer || viewer->getRelationship(owner) != ENEMIES)
	{
		return;
	}
	++res.judged;
	const auto seen = t->lastVisible.find(id);
	const long long last = seen == t->lastVisible.end() ? -1 : (long long)seen->second;
	if (last >= 0 && last + (long long)m_settings.windowFrames >= (long long)batch)
	{
		return; // visible inside the window: the click had something to land on
	}
	const ShroudManager *shroud = m_logic.shroud();
	const ObjectShroudStatus st = shroud->clientObjectStatus(*target, t->player);
	if (!shroud->hasRecord(*target) || target->isKindOfName("ALWAYS_VISIBLE"))
	{
		// no record or ALWAYS_VISIBLE: RW 0x68D8F7 answers CLEAR without looking at any cell, so there is nothing to judge
		++res.abstained;
		return;
	}
	if (st == OBJECTSHROUD_FOGGED)
	{
		++res.remembered; // a seen building drawn as a ghost (Palantír D1: remembered targets are not evidence)
		return;
	}
	Event e;
	e.frame = batch;
	e.player = t->player;
	e.messageType = m.getType();
	e.target = id;
	if (const ThingTemplate *tt = target->getTemplate())
	{
		e.targetTemplate = tt->getName();
	}
	e.targetOwner = owner->getPlayerIndex();
	e.lastVisibleFrame = last;
	e.x = target->getPosition()->x;
	e.y = target->getPosition()->y;
	long long gap = -1;
	if (st == OBJECTSHROUD_CLEAR || st == OBJECTSHROUD_PARTIAL_CLEAR)
	{
		e.reason = Reason::Stealthed; // cells visible now and in the window, the object itself was not
	}
	else if (last < 0 && !shroud->hasSeen(*target, t->player))
	{
		e.reason = Reason::NeverSeen;
	}
	else
	{
		e.reason = Reason::HiddenUnit;
		if (last >= 0)
		{
			gap = (long long)batch - (long long)m_settings.windowFrames - last;
		}
	}
	e.confidence = confidenceOf(e.reason, gap);
	res.events.push_back(std::move(e));
}

std::vector<OrdersIntoFog::PlayerResult> OrdersIntoFog::results() const
{
	std::vector<PlayerResult> out;
	for (const Tracked &t : m_tracked)
	{
		PlayerResult r = t.result;
		// encounters: one player's events within encounterFrames of an encounter's first event are that encounter; its confidence is its best event's
		std::vector<double> best;
		std::vector<UnsignedInt> firstFrame;
		for (Event &e : r.events)
		{
			if (firstFrame.empty() || e.frame > firstFrame.back() + m_settings.encounterFrames)
			{
				firstFrame.push_back(e.frame);
				best.push_back(0.0);
			}
			e.encounter = (int)firstFrame.size();
			best.back() = std::max(best.back(), e.confidence);
		}
		r.encounters = (int)best.size();
		double clean = 1.0;
		UnsignedInt strongFirst = 0, strongLast = 0;
		for (size_t i = 0; i < best.size(); ++i)
		{
			clean *= 1.0 - best[i];
			if (best[i] >= m_settings.alertConfidence)
			{
				if (r.strongEncounters == 0)
				{
					strongFirst = firstFrame[i];
				}
				strongLast = firstFrame[i];
				++r.strongEncounters;
			}
		}
		r.score = r.events.empty() ? 0.0 : 1.0 - clean;
		if (!m_abstain.empty())
		{
			r.band = "abstained";
		}
		else if (r.strongEncounters >= (int)m_settings.alertEncounters && strongLast - strongFirst >= m_settings.alertSpanFrames)
		{
			r.band = "alert";
		}
		else if (r.encounters > 0)
		{
			r.band = "review";
		}
		out.push_back(std::move(r));
	}
	return out;
}

std::string OrdersIntoFog::json(const std::string &gameId, const std::string &crcParity, UnsignedInt frames) const
{
	std::string s = "{\"v\":1,\"detector\":\"D1-orders-into-fog\",\"detector_version\":1";
	s += ",\"game_id\":" + (gameId.empty() ? std::string("null") : jsonString(gameId));
	s += ",\"status\":" + jsonString(m_abstain.empty() ? "ok" : "abstained");
	s += ",\"abstain_reason\":" + (m_abstain.empty() ? std::string("null") : jsonString(m_abstain));
	s += ",\"frames\":" + std::to_string(frames) + ",\"crc_parity\":" + jsonString(crcParity);
	s += ",\"settings\":{\"window_frames\":" + std::to_string(m_settings.windowFrames) + ",\"encounter_frames\":" + std::to_string(m_settings.encounterFrames) +
		 ",\"alert_confidence\":" + jsonNumber(m_settings.alertConfidence) + ",\"alert_encounters\":" + std::to_string(m_settings.alertEncounters) +
		 ",\"alert_span_frames\":" + std::to_string(m_settings.alertSpanFrames) + "}";
	s += ",\"players\":[";
	bool firstPlayer = true;
	for (const PlayerResult &r : results())
	{
		s += firstPlayer ? "" : ",";
		firstPlayer = false;
		s += "{\"player_index\":" + std::to_string(r.player) + ",\"slot\":" + std::to_string(r.slot) + ",\"name\":" + jsonString(r.name) + ",\"band\":" + jsonString(r.band) +
			 ",\"score\":" + jsonNumber(r.score) + ",\"encounters\":" + std::to_string(r.encounters) +
			 ",\"strong_encounters\":" + std::to_string(r.strongEncounters) + ",\"orders_judged\":" + std::to_string(r.judged) +
			 ",\"orders_on_remembered\":" + std::to_string(r.remembered) + ",\"orders_stale\":" + std::to_string(r.stale) +
			 ",\"orders_abstained\":" + std::to_string(r.abstained) + ",\"events\":[";
		bool firstEvent = true;
		for (const Event &e : r.events)
		{
			s += firstEvent ? "" : ",";
			firstEvent = false;
			const char *name = GameMessageTypeName(e.messageType);
			s += "{\"frame\":" + std::to_string(e.frame) + ",\"time_s\":" + jsonNumber(e.frame / 5.0) +
				 ",\"command\":" + jsonString(name && *name ? name : std::to_string(e.messageType)) + ",\"target_id\":" + std::to_string(e.target) +
				 ",\"target_template\":" + jsonString(e.targetTemplate) + ",\"target_owner_index\":" + std::to_string(e.targetOwner) +
				 ",\"reason\":" + jsonString(reasonName(e.reason)) +
				 ",\"last_visible_frame\":" + (e.lastVisibleFrame < 0 ? std::string("null") : std::to_string(e.lastVisibleFrame)) +
				 ",\"x\":" + jsonNumber(e.x) + ",\"y\":" + jsonNumber(e.y) + ",\"confidence\":" + jsonNumber(e.confidence) +
				 ",\"encounter\":" + std::to_string(e.encounter) + "}";
		}
		s += "]}";
	}
	s += "]}";
	return s;
}
