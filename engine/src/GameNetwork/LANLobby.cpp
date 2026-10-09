// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/LANLobby.h.

#include "GameNetwork/LANLobby.h"

namespace
{
constexpr std::uint16_t kLobbyMagic = 0x4C42; // "BL"
enum : std::uint8_t
{
	LOBBY_JOIN = 1,
	LOBBY_ACCEPT = 2,
	LOBBY_REFUSE = 3,
	LOBBY_START = 4,
	LOBBY_START_ACK = 5
};
constexpr std::uint64_t kResendMs = 150;
constexpr std::uint8_t kLobbyVersion = 3; // 2: JOIN / START carry the lobby version and the profile identity; 3 (lane HERO-2): the slots' Create-a-Hero records

NetByteWriter header(std::uint8_t kind)
{
	NetByteWriter w;
	w.u16(kLobbyMagic);
	w.u8(kind);
	return w;
}
void sendTo(UDP &s, const NetAddress &to, const std::vector<std::uint8_t> &b)
{
	s.sendTo(to, b.data(), b.size());
}
} // namespace

bool LANLobby::isLobbyDatagram(const std::vector<std::uint8_t> &d)
{
	return d.size() >= 3 && (d[0] | (d[1] << 8)) == kLobbyMagic;
}

std::vector<std::uint8_t> LANLobby::encodeStart(const LobbyStart &s)
{
	NetByteWriter w = header(LOBBY_START);
	w.u8(kLobbyVersion);
	w.str(s.profile.text);
	NetPacket::writeNewGameMessage(w, s.game);
	w.i32(s.runAhead);
	w.i32(s.crcInterval);
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		w.u8(s.networkSlot[(size_t)i] ? 1 : 0);
		w.u32(s.addresses[(size_t)i].ip);
		w.u16(s.addresses[(size_t)i].port);
	}
	return w.take();
}

bool LANLobby::decodeStart(const std::vector<std::uint8_t> &d, LobbyStart &out, std::string *error)
{
	NetByteReader r(d);
	if (r.u16() != kLobbyMagic || r.u8() != LOBBY_START)
	{
		if (error)
		{
			*error = "not a lobby START";
		}
		return false;
	}
	const std::uint8_t version = r.u8();
	if (version != kLobbyVersion)
	{
		if (error)
		{
			*error = "lobby START version " + std::to_string(version) + " (this build speaks " + std::to_string(kLobbyVersion) + ")";
		}
		return false;
	}
	out.profile = ProfileIdentity::fromText(r.str());
	if (!NetPacket::readNewGameMessage(r, out.game, error))
	{
		return false;
	}
	out.runAhead = r.i32();
	out.crcInterval = r.i32();
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		out.networkSlot[(size_t)i] = r.flag(); // lane MP-2 (review): 0 / 1 only
		out.addresses[(size_t)i].ip = r.u32();
		out.addresses[(size_t)i].port = r.u16();
	}
	if (r.failed() || !r.atEnd())
	{
		if (error)
		{
			*error = r.badFlag() ? "lobby START with a network-slot flag that is not 0 / 1" : "malformed lobby START";
		}
		return false;
	}
	// lane MP-2 (review): the lockstep parameters and the players' slots must be usable
	bool anyPlayer = false;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		anyPlayer = anyPlayer || out.networkSlot[(size_t)i];
		if (out.networkSlot[(size_t)i] && !out.game.game.slots[i].isHuman())
		{
			if (error)
			{
				*error = "lobby START marks slot " + std::to_string(i) + " a network player, but its GameInfo slot is not a human";
			}
			return false;
		}
	}
	if (out.runAhead < 1 || out.runAhead > 25 || out.crcInterval < 0 || !anyPlayer)
	{
		if (error)
		{
			*error = "lobby START with run-ahead " + std::to_string(out.runAhead) + ", CRC interval " + std::to_string(out.crcInterval)
				+ (anyPlayer ? std::string() : std::string(" and no network player"));
		}
		return false;
	}
	return true;
}

std::vector<std::uint8_t> LANLobby::encodeStartAck(int slot)
{
	NetByteWriter w = header(LOBBY_START_ACK);
	w.i32(slot);
	return w.take();
}

// ---- host --------------------------------------------------------------------------------------------------------------------------
LANLobbyHost::LANLobbyHost(UDP &socket, const NewGameMessage &game, int hostSlot, int runAhead, int crcInterval, const ProfileIdentity &profile)
	: m_socket(socket)
{
	m_start.profile = profile;
	m_start.game = game;
	m_start.localSlot = hostSlot;
	m_start.runAhead = runAhead;
	m_start.crcInterval = crcInterval;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		m_start.networkSlot[(size_t)i] = game.game.slots[i].isHuman();
	}
	m_filled[(size_t)hostSlot] = true;
	m_acked[(size_t)hostSlot] = true;
	m_start.addresses[(size_t)hostSlot] = socket.localAddress();
}

bool LANLobbyHost::poll()
{
	NetAddress from;
	std::vector<std::uint8_t> d;
	while (m_socket.receiveFrom(from, d))
	{
		if (!LANLobby::isLobbyDatagram(d))
		{
			continue;
		}
		NetByteReader r(d);
		r.u16();
		const std::uint8_t kind = r.u8();
		if (kind == LOBBY_JOIN)
		{
			const std::uint8_t version = r.u8();
			const std::u16string name = r.wstr();
			const ProfileIdentity theirs = ProfileIdentity::fromText(r.str());
			if (r.failed() || !r.atEnd())
			{
				continue;
			}
			std::string refusal;
			if (version != kLobbyVersion)
			{
				refusal = "lobby version " + std::to_string(version) + " (the host speaks " + std::to_string(kLobbyVersion) + ")";
			}
			else if (theirs != m_start.profile)
			{
				refusal = "another profile: " + ProfileIdentity::difference(m_start.profile, theirs);
			}
			if (!refusal.empty())
			{
				m_log.push_back("refused " + from.text() + ": " + refusal);
				NetByteWriter w = header(LOBBY_REFUSE);
				w.str(refusal);
				sendTo(m_socket, from, w.data());
				continue;
			}
			int slot = -1;
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				if (m_filled[(size_t)i] && m_start.addresses[(size_t)i] == from && i != m_start.localSlot)
				{
					slot = i; // a repeated JOIN: answer again
				}
			}
			for (int i = 0; i < MAX_SLOTS && slot < 0 && !m_started; ++i)
			{
				if (m_start.networkSlot[(size_t)i] && !m_filled[(size_t)i])
				{
					slot = i;
					m_filled[(size_t)i] = true;
					m_start.addresses[(size_t)i] = from;
					m_start.game.game.slots[i].name = name;
					m_log.push_back("slot " + std::to_string(i) + " joined from " + from.text());
				}
			}
			if (slot < 0)
			{
				NetByteWriter w = header(LOBBY_REFUSE);
				w.str(m_started ? "the game has started" : "the game is full");
				sendTo(m_socket, from, w.data());
				continue;
			}
			NetByteWriter w = header(LOBBY_ACCEPT);
			w.i32(slot);
			sendTo(m_socket, from, w.data());
		}
		else if (kind == LOBBY_START_ACK)
		{
			const int slot = r.i32();
			if (r.failed() || !r.atEnd())
			{
				m_log.push_back("a malformed START_ACK from " + from.text()); // lane MP-2 (review): read whole before any state change
				continue;
			}
			if (slot >= 0 && slot < MAX_SLOTS && m_start.addresses[(size_t)slot] == from)
			{
				m_acked[(size_t)slot] = true;
			}
		}
	}
	bool allFilled = true;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		allFilled = allFilled && (!m_start.networkSlot[(size_t)i] || m_filled[(size_t)i]);
	}
	if (!allFilled)
	{
		return false;
	}
	m_started = true;
	bool allAcked = true;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		allAcked = allAcked && (!m_start.networkSlot[(size_t)i] || m_acked[(size_t)i]);
	}
	if (allAcked)
	{
		return true;
	}
	const std::uint64_t now = NetMilliseconds();
	if (m_lastStartSent == 0 || now - m_lastStartSent >= kResendMs)
	{
		LobbyStart wire = m_start;
		wire.addresses[(size_t)m_start.localSlot] = NetAddress(); // the players use the address they joined
		const std::vector<std::uint8_t> b = LANLobby::encodeStart(wire);
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			if (m_start.networkSlot[(size_t)i] && !m_acked[(size_t)i])
			{
				sendTo(m_socket, m_start.addresses[(size_t)i], b);
			}
		}
		m_lastStartSent = now;
	}
	return false;
}

// ---- client ----------------------------------------------------------------------------------------------------------------------
LANLobbyClient::LANLobbyClient(UDP &socket, const NetAddress &host, const std::u16string &name, const ProfileIdentity &profile)
	: m_socket(socket)
	, m_host(host)
	, m_name(name)
	, m_profile(profile)
{
}

bool LANLobbyClient::poll()
{
	NetAddress from;
	std::vector<std::uint8_t> d;
	while (m_socket.receiveFrom(from, d))
	{
		if (!(from == m_host) || !LANLobby::isLobbyDatagram(d))
		{
			continue;
		}
		NetByteReader r(d);
		r.u16();
		const std::uint8_t kind = r.u8();
		if (kind == LOBBY_ACCEPT)
		{
			const int slot = r.i32();
			if (!r.failed() && r.atEnd() && slot >= 0 && slot < MAX_SLOTS) // lane MP-2 (review): nothing missing, nothing after it
			{
				m_slot = slot;
			}
		}
		else if (kind == LOBBY_REFUSE)
		{
			const std::string why = r.str();
			if (r.failed() || !r.atEnd())
			{
				continue; // lane MP-2 (review): a malformed refusal changes nothing
			}
			m_error = "the host refused: " + why;
			return false;
		}
		else if (kind == LOBBY_START && m_slot >= 0)
		{
			LobbyStart s;
			std::string error;
			if (!LANLobby::decodeStart(d, s, &error))
			{
				m_error = error;
				return false;
			}
			if (s.profile != m_profile)
			{
				m_error = "the host's START has another profile: " + ProfileIdentity::difference(m_profile, s.profile);
				return false;
			}
			s.localSlot = m_slot;
			s.addresses[(size_t)m_slot] = m_socket.localAddress();
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				if (s.networkSlot[(size_t)i] && s.addresses[(size_t)i].ip == 0 && s.addresses[(size_t)i].port == 0)
				{
					s.addresses[(size_t)i] = m_host;
				}
			}
			m_start = s;
			m_started = true;
			const std::vector<std::uint8_t> ack = LANLobby::encodeStartAck(m_slot);
			sendTo(m_socket, m_host, ack);
		}
	}
	if (m_started)
	{
		return true;
	}
	const std::uint64_t now = NetMilliseconds();
	if (m_slot < 0 && (m_lastJoinSent == 0 || now - m_lastJoinSent >= kResendMs))
	{
		NetByteWriter w = header(LOBBY_JOIN);
		w.u8(kLobbyVersion);
		w.wstr(m_name);
		w.str(m_profile.text);
		sendTo(m_socket, m_host, w.data());
		m_lastJoinSent = now;
	}
	return false;
}
