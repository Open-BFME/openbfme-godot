// OpenBFME. GPL-3.0.
// See GameNetwork/NetImpairment.h.

#include "GameNetwork/NetImpairment.h"

#include <cstdlib>
#include <sstream>

bool NetLinkFaults::parse(const std::string &text, NetLinkFaults &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "link faults \"" + text + "\": " + why;
		}
		return false;
	};
	NetLinkFaults f;
	std::stringstream ss(text);
	for (std::string item; std::getline(ss, item, ',');)
	{
		if (item.empty())
		{
			continue;
		}
		const size_t eq = item.find('=');
		if (eq == std::string::npos || eq + 1 >= item.size())
		{
			return fail("\"" + item + "\" is not key=value");
		}
		const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
		char *end = nullptr;
		const long v = std::strtol(value.c_str(), &end, 10);
		if (!end || *end != '\0' || v < 0 || v > 600000)
		{
			return fail("\"" + value + "\" is not a value in 0 .. 600000");
		}
		const bool perMille = key == "loss" || key == "dup" || key == "reorder";
		if (perMille && v > 1000)
		{
			return fail(key + " is per mille (0 .. 1000)");
		}
		if (key == "latency") f.latencyMs = (int)v;
		else if (key == "jitter") f.jitterMs = (int)v;
		else if (key == "loss") f.lossPerMille = (int)v;
		else if (key == "dup") f.duplicatePerMille = (int)v;
		else if (key == "reorder") f.reorderPerMille = (int)v;
		else if (key == "reorder_ms") f.reorderMs = (int)v;
		else
		{
			return fail("unknown key " + key + " (latency, jitter, loss, dup, reorder, reorder_ms)");
		}
	}
	out = f;
	return true;
}

std::string NetLinkFaults::text() const
{
	std::ostringstream o;
	o << "latency=" << latencyMs << ",jitter=" << jitterMs << ",loss=" << lossPerMille << ",dup=" << duplicatePerMille << ",reorder=" << reorderPerMille
	  << ",reorder_ms=" << (reorderMs >= 0 ? reorderMs : 2 * jitterMs + 20);
	return o.str();
}

bool NetImpairment::Config::active() const
{
	if (all.any())
	{
		return true;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (linkSet[(size_t)s] && link[(size_t)s].any())
		{
			return true;
		}
	}
	return false;
}

NetImpairment::NetImpairment(const Config &config)
	: m_config(config)
{
	for (size_t i = 0; i < m_rng.size(); ++i)
	{
		// a distinct, nonzero xorshift state per link (splitmix32 of seed and link)
		std::uint32_t z = m_config.seed + 0x9E3779B9u * (std::uint32_t)(i + 1);
		z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
		z = (z ^ (z >> 13)) * 0xC2B2AE35u;
		z ^= z >> 16;
		m_rng[i] = z ? z : 0x6D2B79F5u;
	}
}

const NetLinkFaults &NetImpairment::faults(int slot) const
{
	return slot >= 0 && slot < MAX_SLOTS && m_config.linkSet[(size_t)slot] ? m_config.link[(size_t)slot] : m_config.all;
}

std::uint32_t NetImpairment::next(int slot)
{
	std::uint32_t &x = m_rng[(size_t)(slot >= 0 && slot < MAX_SLOTS ? slot : MAX_SLOTS)];
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

int NetImpairment::uniform(int slot, int lo, int hi)
{
	return lo + (int)(next(slot) % (std::uint32_t)(hi - lo + 1));
}

void NetImpairment::blackout(std::uint64_t now, std::uint64_t durationMs)
{
	m_blackoutFrom = now;
	m_blackoutUntil = now + durationMs;
	++m_stats.blackouts;
}

void NetImpairment::schedule(int slot, std::uint64_t now, std::vector<std::uint64_t> &sendTimes)
{
	sendTimes.clear();
	++m_stats.scheduled;
	if (blackedOut(now))
	{
		++m_stats.blackoutOut;
		return;
	}
	const bool peer = slot >= 0 && slot < MAX_SLOTS;
	const NetLinkFaults &f = peer ? faults(slot) : NetLinkFaults();
	if (!f.any())
	{
		sendTimes.push_back(now);
		return;
	}
	// a fixed number of draws per datagram (loss, delay, reorder, duplicate, the duplicate's delay): the n-th datagram's fate does not depend on the
	// earlier ones' outcomes
	const int lossDraw = uniform(slot, 0, 999);
	const int delayDraw = f.jitterMs > 0 ? uniform(slot, -f.jitterMs, f.jitterMs) : 0;
	const int reorderDraw = uniform(slot, 0, 999);
	const int dupDraw = uniform(slot, 0, 999);
	const int dupDelayDraw = f.jitterMs > 0 ? uniform(slot, -f.jitterMs, f.jitterMs) : 0;
	if (lossDraw < f.lossPerMille)
	{
		++m_stats.lost;
		return;
	}
	auto delayOf = [&](int draw) {
		const int d = f.latencyMs + draw;
		return (std::uint64_t)(d > 0 ? d : 0);
	};
	std::uint64_t delay = delayOf(delayDraw);
	if (reorderDraw < f.reorderPerMille)
	{
		delay += (std::uint64_t)(f.reorderMs >= 0 ? f.reorderMs : 2 * f.jitterMs + 20);
		++m_stats.reordered;
	}
	m_stats.delayed += delay > 0 ? 1 : 0;
	sendTimes.push_back(now + delay);
	if (dupDraw < f.duplicatePerMille)
	{
		sendTimes.push_back(now + delayOf(dupDelayDraw));
		++m_stats.duplicated;
	}
}

bool NetImpairment::releaseDue(std::uint64_t now)
{
	if (blackedOut(now))
	{
		++m_stats.blackoutOut;
		return false;
	}
	return true;
}

bool NetImpairment::acceptIncoming(std::uint64_t now)
{
	if (blackedOut(now))
	{
		++m_stats.blackoutIn;
		return false;
	}
	return true;
}

std::string NetImpairment::statsText() const
{
	std::ostringstream o;
	o << "scheduled " << m_stats.scheduled << " lost " << m_stats.lost << " duplicated " << m_stats.duplicated << " reordered " << m_stats.reordered
	  << " delayed " << m_stats.delayed << " blackouts " << m_stats.blackouts << " blackout_out " << m_stats.blackoutOut << " blackout_in "
	  << m_stats.blackoutIn;
	return o.str();
}

std::vector<std::string> NetImpairment::stopLines()
{
	return {
		"[S-1890] network faults: the test harness's faults are a synthetic model (independent per-datagram uniform jitter, Bernoulli loss, duplication, "
		"held-back reordering, whole-endpoint blackouts), not recorded traces of real LAN, Wi-Fi or internet links: passing games under it do not prove every "
		"real network",
		"[S-1891] resend policy: the transport resends a record once early when an arriving ack does not cover it a smoothed round trip plus its variation "
		"after it went out, and caps Karn's backoff at twice the RFC 6298 timeout once the link has round-trip samples (lane MP-3); retail's retry policy "
		"(ZH Connection::doRetryMetrics, RotWK's not read) is not reproduced (with S-721: cross-play gate 1)",
		"[S-1892] smoothness: input-to-action latency and network stalls are measured on OpenBFME peers only (LockstepDriver::SmoothStats); no retail "
		"measurement under the same faults exists to compare against, and retail's immediate move hint (RW 0x4E03C7, SCMoveHintSml) is not drawn",
	};
}
