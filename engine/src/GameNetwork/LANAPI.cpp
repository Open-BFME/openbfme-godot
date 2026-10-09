// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameNetwork/LANAPI.h.

#include "GameNetwork/LANAPI.h"

#include "GameNetwork/NetPacket.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace
{
constexpr std::uint16_t kMagic = 0x4E4C; // "LN"
constexpr std::uint8_t kVersion = 2; // 2 (lane HERO-2): the GameInfo slots' Create-a-Hero records, OPT_SLOT_CREATE_A_HERO
constexpr std::uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr std::uint32_t kLoopback = 0x7F000001u;
constexpr std::uint64_t kStartGiveUpMs = 10000; // OpenBFME: a host stops resending GAME_START after this (reported)

// GAME_OPTIONS sub-kinds (OpenBFME): the host's whole setup, a player's request for its own slot, a player's keep-alive (ZH's "HELLO")
enum : std::uint8_t
{
	OPT_FULL = 0,
	OPT_SLOT_REQUEST = 1,
	OPT_HELLO = 2,
	OPT_SLOT_CREATE_A_HERO = 3 ///< lane HERO-2: a player's Create-a-Hero for its own slot (u8 flag, then the record's .cah bytes)
};

void writeAddress(NetByteWriter &w, const NetAddress &a)
{
	w.u32(a.ip);
	w.u16(a.port);
}
NetAddress readAddress(NetByteReader &r)
{
	NetAddress a;
	a.ip = r.u32();
	a.port = r.u16();
	return a;
}

std::string toNarrow(const std::u16string &s)
{
	std::string out;
	for (char16_t c : s)
	{
		out.push_back(c < 0x80 ? (char)c : '?');
	}
	return out;
}
} // namespace

void LANAPI::applySlotRequest(SkirmishGameInfo &info, int slot, int playerTemplate, int color, int team, int startPos)
{
	// ZH LanGameOptionsMenu handle*Selection as the host applies them: a colour or start position another slot holds is refused (random -1 never is)
	SkirmishGameSlot &s = info.slots[slot];
	if (playerTemplate != kKeep)
	{
		s.playerTemplate = playerTemplate;
	}
	if (color != kKeep && (color == -1 || !info.isColorTaken(color, slot)))
	{
		s.color = color;
	}
	if (team != kKeep)
	{
		s.teamNumber = team;
	}
	if (startPos != kKeep && (startPos == -1 || !info.isStartPositionTaken(startPos, slot)))
	{
		s.startPos = startPos;
	}
}

bool LANAPI::slotRequestValid(int playerTemplate, int color, int team, int startPos, std::string *why) const
{
	auto check = [&](const char *name, int v, int count) {
		if (v == kKeep || (v >= -1 && v < count))
		{
			return true;
		}
		if (why)
		{
			*why = std::string(name) + " " + std::to_string(v) + " (-1 .. " + std::to_string(count - 1) + ")";
		}
		return false;
	};
	return check("faction", playerTemplate, m_options.playerTemplateCount) && check("colour", color, m_options.colorCount) && check("team", team, MAX_SLOTS / 2)
		&& check("start position", startPos, MAX_SLOTS);
}

bool LANAPI::gameInfoValid(const SkirmishGameInfo &info, std::string *why) const
{
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = info.slots[i];
		std::string w;
		if (!slotRequestValid(s.playerTemplate, s.color, s.teamNumber, s.startPos, &w) || s.playerTemplate == kKeep || s.color == kKeep || s.teamNumber == kKeep
			|| s.startPos == kKeep)
		{
			if (why)
			{
				*why = "slot " + std::to_string(i) + ": " + (w.empty() ? std::string("a field is the keep marker") : w);
			}
			return false;
		}
		if (s.hasCreateAHero && !createAHeroValid(s, &w))
		{
			if (why)
			{
				*why = "slot " + std::to_string(i) + ": " + w;
			}
			return false;
		}
	}
	return true;
}

bool LANAPI::createAHeroValid(const SkirmishGameSlot &s, std::string *why) const
{
	if (!s.isHuman())
	{
		if (why)
		{
			*why = "a Create-a-Hero on a slot without a human player";
		}
		return false;
	}
	if (!m_options.validateCreateAHero)
	{
		if (why)
		{
			*why = "a Create-a-Hero, but the lobby has no Create-a-Hero validator (LANAPI::Options::validateCreateAHero)";
		}
		return false;
	}
	return m_options.validateCreateAHero(s.createAHero, why);
}

int LANGame::humanSlots() const
{
	int n = 0;
	for (const SkirmishGameSlot &s : info.slots)
	{
		n += s.isHuman() ? 1 : 0;
	}
	return n;
}

LANAPI::LANAPI(const ProfileIdentity &profile, const std::u16string &name, const Options &options)
	: m_profile(profile)
	, m_name(name)
	, m_options(options)
{
	// this instance's mark (its own broadcasts come back to it); not simulation state
	m_nonce = (std::uint32_t)(NetMilliseconds() * 2654435761ull) ^ (std::uint32_t)(std::uintptr_t)this ^ 0x9E3779B9u;
}

LANAPI::~LANAPI() = default;

void LANAPI::note(const std::string &s)
{
	if (m_log.size() < 400)
	{
		m_log.push_back(s);
	}
}

bool LANAPI::open(std::string *error)
{
	if (m_options.playerTemplateCount <= 0 || m_options.colorCount <= 0)
	{
		if (error)
		{
			*error = "the LAN lobby needs the faction and colour counts (LANAPI::Options) to check the players' requests";
		}
		return false;
	}
	// RW 0x84C862 .. 0x84C8A3: 8086 + atoi(getenv("_EA_RTS_HEADLESS")), the next port while the bind fails, below 8094
	const char *offset = std::getenv("_EA_RTS_HEADLESS");
	int port = (int)m_options.lobbyPortBase + (offset ? std::atoi(offset) : 0);
	const int end = (int)m_options.lobbyPortBase + m_options.lobbyPorts;
	m_lobbySocket = std::make_unique<UDP>();
	std::string why;
	for (; port < end; ++port)
	{
		if (m_lobbySocket->bind(0, (std::uint16_t)port, &why))
		{
			break;
		}
	}
	if (port >= end)
	{
		if (error)
		{
			*error = "no free LAN lobby port in " + std::to_string(m_options.lobbyPortBase) + " .. " + std::to_string(end - 1);
		}
		return false;
	}
	m_lobbyPort = (std::uint16_t)port;
	m_lobbySocket->enableBroadcast();
	m_gameSocket = std::make_unique<UDP>();
	if (!m_gameSocket->bind(0, 0, error))
	{
		return false;
	}
	m_gamePort = m_gameSocket->localAddress().port;
	note("lobby port " + std::to_string(m_lobbyPort) + ", game port " + std::to_string(m_gamePort));
	return true;
}

std::vector<std::uint8_t> LANAPI::header(std::uint8_t type) const
{
	NetByteWriter w;
	w.u16(kMagic);
	w.u8(kVersion);
	w.u8(type);
	w.u32(m_nonce);
	w.wstr(m_name);
	return w.take();
}

void LANAPI::sendTo(const NetAddress &to, const std::vector<std::uint8_t> &bytes)
{
	if (m_lobbySocket && to.port != 0)
	{
		m_lobbySocket->sendTo(to, bytes.data(), bytes.size());
	}
}

void LANAPI::sendToAll(const std::vector<std::uint8_t> &bytes)
{
	// RW 0x84C2DF: every lobby port 8086 .. 8093 of the broadcast address (and, OpenBFME, of 127.0.0.1 and the extra targets)
	std::vector<std::uint32_t> targets = m_options.extraTargets;
	if (m_options.broadcast)
	{
		targets.push_back(kBroadcast);
		targets.push_back(kLoopback);
	}
	for (std::uint32_t ip : targets)
	{
		for (int p = 0; p < m_options.lobbyPorts; ++p)
		{
			const std::uint16_t port = (std::uint16_t)(m_options.lobbyPortBase + p);
			if (ip == kLoopback && port == m_lobbyPort)
			{
				continue; // ourselves
			}
			sendTo(NetAddress{ ip, port }, bytes);
		}
	}
}

void LANAPI::sendToGame(const std::vector<std::uint8_t> &bytes)
{
	if (!m_inGame)
	{
		return;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (s != m_localSlot && m_current.info.slots[s].isHuman() && m_current.lobby[(size_t)s].port != 0)
		{
			sendTo(m_current.lobby[(size_t)s], bytes);
		}
	}
}

std::vector<std::uint8_t> LANAPI::gameAnnounce() const
{
	// ZH RequestGameAnnounce: the game's name, in progress, the options
	std::vector<std::uint8_t> h = header(LANMessageType::GAME_ANNOUNCE);
	NetByteWriter w;
	w.wstr(m_current.name);
	w.u8(m_current.inProgress ? 1 : 0);
	w.str(m_profile.digest);
	NetPacket::writeGameInfo(w, m_current.info);
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	return h;
}

void LANAPI::broadcastOptions()
{
	// the host's whole setup to every player of the game (ZH RequestGameOptions(GenerateGameOptionsString()))
	if (!amIHost())
	{
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::GAME_OPTIONS);
	NetByteWriter w;
	w.u8(OPT_FULL);
	w.wstr(m_current.name);
	NetPacket::writeGameInfo(w, m_current.info);
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		writeAddress(w, m_current.lobby[(size_t)s]);
		writeAddress(w, m_current.game[(size_t)s]);
		w.u8(m_current.accepted[(size_t)s] ? 1 : 0);
	}
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendToGame(h);
	event(Event{ Event::GameOptions });
}

// ---- requests ---------------------------------------------------------------------------------------------------------------------
void LANAPI::requestLocations()
{
	sendToAll(header(LANMessageType::REQUEST_LOCATIONS));
}

void LANAPI::requestSetName(const std::u16string &name)
{
	// ZH RequestSetName: the new name, announced (in the lobby: LOBBY_ANNOUNCE)
	m_name = name;
	if (!m_inGame)
	{
		sendToAll(header(LANMessageType::LOBBY_ANNOUNCE));
	}
}

void LANAPI::requestGameCreate(const SkirmishGameInfo &game)
{
	// ZH RequestGameCreate: we host; slot 0 is ours
	if (m_inGame || m_pending != ACT_NONE)
	{
		event(Event{ Event::GameCreate, RET_BUSY });
		return;
	}
	m_current = LANGame();
	m_current.name = m_name;
	m_current.info = game;
	m_current.info.slots[0].state = SLOT_PLAYER;
	m_current.info.slots[0].name = m_name;
	m_current.info.inProgress = false;
	m_current.lobby[0] = NetAddress{ 0, m_lobbyPort };
	m_current.game[0] = NetAddress{ 0, m_gamePort };
	m_inGame = true;
	m_host = true;
	m_localSlot = 0;
	m_started = false;
	note("hosting game " + toNarrow(m_name));
	sendToAll(gameAnnounce());
	event(Event{ Event::GameCreate, RET_OK });
}

void LANAPI::requestGameJoin(int index)
{
	if (index < 0 || index >= (int)m_games.size())
	{
		event(Event{ Event::GameJoin, RET_GAME_GONE });
		return;
	}
	requestGameJoinDirect(m_games[(size_t)index].host);
}

void LANAPI::requestGameJoinDirect(const NetAddress &host)
{
	// ZH RequestGameJoin: REQUEST_JOIN, then wait m_actionTimeout for the answer
	if (m_pending != ACT_NONE || m_inGame)
	{
		event(Event{ Event::GameJoin, RET_BUSY });
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::REQUEST_JOIN);
	NetByteWriter w;
	w.str(m_profile.digest);
	w.u16(m_gamePort);
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendTo(host, h);
	m_pending = ACT_JOIN;
	m_joinTarget = host;
	m_expiration = m_now + m_options.actionTimeoutMs;
	note("join request to " + host.text());
}

void LANAPI::requestGameLeave()
{
	// ZH RequestGameLeave: the host leaves at once (its game ends), a player when the host answers or after the timeout
	if (!m_inGame)
	{
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::REQUEST_GAME_LEAVE);
	NetByteWriter w;
	w.u8(0); // not a kick
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendToGame(h);
	note(m_host ? "the host leaves its game" : "leaving the game");
	leaveToLobby();
}

void LANAPI::requestLobbyLeave()
{
	sendToAll(header(LANMessageType::REQUEST_LOBBY_LEAVE));
}

void LANAPI::requestAccept(bool accepted)
{
	if (!m_inGame || m_localSlot < 0)
	{
		return;
	}
	m_current.accepted[(size_t)m_localSlot] = accepted;
	if (m_host)
	{
		broadcastOptions();
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::SET_ACCEPT);
	NetByteWriter w;
	w.u8(accepted ? 1 : 0);
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendTo(m_current.lobby[0], h);
}

void LANAPI::requestChat(const std::u16string &text, bool system)
{
	if (!m_inGame)
	{
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::CHAT);
	NetByteWriter w;
	w.wstr(text);
	w.u8(system ? 1 : 0);
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendToGame(h);
	event(Event{ Event::Chat, RET_OK, m_localSlot, system, m_name, text });
}

void LANAPI::requestSlotOptions(int playerTemplate, int color, int team, int startPos)
{
	if (!m_inGame || m_localSlot < 0)
	{
		return;
	}
	if (m_host)
	{
		SkirmishGameInfo info = m_current.info;
		applySlotRequest(info, m_localSlot, playerTemplate, color, team, startPos);
		hostSetOptions(info);
		return;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::GAME_OPTIONS);
	NetByteWriter w;
	w.u8(OPT_SLOT_REQUEST);
	w.i32(playerTemplate);
	w.i32(color);
	w.i32(team);
	w.i32(startPos);
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendTo(m_current.lobby[0], h);
}

bool LANAPI::requestSlotCreateAHero(const CreateAHeroHero *hero, std::string *error)
{
	if (!m_inGame || m_localSlot < 0)
	{
		if (error)
		{
			*error = "not in a game";
		}
		return false;
	}
	SkirmishGameSlot slot = m_current.info.slots[m_localSlot];
	slot.clearCreateAHero();
	if (hero && (!slot.setCreateAHero(*hero, error) || !createAHeroValid(slot, error)))
	{
		return false;
	}
	if (m_host)
	{
		applyCreateAHero(m_localSlot, slot);
		return true;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::GAME_OPTIONS);
	NetByteWriter w;
	w.u8(OPT_SLOT_CREATE_A_HERO);
	w.u8(slot.hasCreateAHero ? 1 : 0);
	if (slot.hasCreateAHero)
	{
		w.bytes(slot.createAHero.save());
	}
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendTo(m_current.lobby[0], h);
	return true;
}

void LANAPI::applyCreateAHero(int slot, const SkirmishGameSlot &from)
{
	SkirmishGameSlot &s = m_current.info.slots[slot];
	const bool changed = s.hasCreateAHero != from.hasCreateAHero || !(s.createAHero == from.createAHero);
	s.hasCreateAHero = from.hasCreateAHero;
	s.createAHero = from.createAHero;
	if (changed)
	{
		m_current.accepted.fill(false); // ZH: a change of the options unaccepts everybody
	}
	broadcastOptions();
}

bool LANAPI::handleSlotCreateAHero(NetByteReader &r, const NetAddress &from)
{
	const bool has = r.flag();
	std::vector<std::uint8_t> bytes;
	if (has)
	{
		bytes = r.bytes();
	}
	if (r.failed() || !r.atEnd())
	{
		m_errors.push_back("a malformed Create-a-Hero request from " + from.text());
		return false;
	}
	const int s = slotOf(from);
	if (!m_inGame || !m_host || s <= 0)
	{
		return true;
	}
	SkirmishGameSlot slot = m_current.info.slots[s];
	slot.clearCreateAHero();
	std::string why;
	if (has && (!slot.setCreateAHeroBytes(bytes, &why) || !createAHeroValid(slot, &why)))
	{
		m_errors.push_back("a Create-a-Hero request from " + from.text() + ": " + why + ": refused");
		return false;
	}
	applyCreateAHero(s, slot);
	return true;
}

bool LANAPI::hostSetOptions(const SkirmishGameInfo &info)
{
	if (!amIHost())
	{
		return false;
	}
	// the human slots and their addresses stay the host's (a player joins or leaves through the join / leave messages)
	SkirmishGameInfo next = info;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_current.info.slots[s].isHuman())
		{
			next.slots[s].state = SLOT_PLAYER;
			next.slots[s].name = m_current.info.slots[s].name;
			// lane HERO-2: a human slot's Create-a-Hero is its player's (requestSlotCreateAHero), like its name
			next.slots[s].hasCreateAHero = m_current.info.slots[s].hasCreateAHero;
			next.slots[s].createAHero = m_current.info.slots[s].createAHero;
		}
		else if (next.slots[s].isHuman())
		{
			next.slots[s].state = SLOT_OPEN; // a human slot is never made up by the host
		}
		if (!next.slots[s].isHuman())
		{
			next.slots[s].clearCreateAHero(); // only a human slot has one
		}
	}
	const bool changed = !(next == m_current.info);
	m_current.info = next;
	if (changed)
	{
		m_current.accepted.fill(false); // ZH: a change of the options unaccepts everybody
	}
	broadcastOptions();
	return true;
}

bool LANAPI::hostKick(int slot)
{
	if (!amIHost() || slot <= 0 || slot >= MAX_SLOTS || !m_current.info.slots[slot].isHuman())
	{
		return false;
	}
	std::vector<std::uint8_t> h = header(LANMessageType::REQUEST_GAME_LEAVE);
	NetByteWriter w;
	w.u8(1); // kicked
	std::vector<std::uint8_t> b = w.take();
	h.insert(h.end(), b.begin(), b.end());
	sendTo(m_current.lobby[(size_t)slot], h);
	const std::u16string who = m_current.info.slots[slot].name;
	m_current.info.slots[slot] = SkirmishGameSlot();
	m_current.info.slots[slot].state = SLOT_OPEN;
	m_current.lobby[(size_t)slot] = NetAddress();
	m_current.game[(size_t)slot] = NetAddress();
	m_current.accepted.fill(false);
	note("kicked slot " + std::to_string(slot));
	event(Event{ Event::PlayerLeave, RET_OK, slot, false, who });
	broadcastOptions();
	return true;
}

bool LANAPI::hostStartGame(std::string *reason)
{
	if (!amIHost())
	{
		if (reason)
		{
			*reason = "only the host starts the game";
		}
		return false;
	}
	for (int s = 1; s < MAX_SLOTS; ++s)
	{
		if (m_current.info.slots[s].isHuman() && !m_current.accepted[(size_t)s])
		{
			if (reason)
			{
				*reason = "slot " + std::to_string(s) + " has not accepted";
			}
			return false;
		}
	}
	// the start every peer begins from (GameNetwork/LANLobby.h LobbyStart)
	m_start = LobbyStart();
	m_start.game.mode = NewGameMode::Skirmish;
	m_start.game.game = m_current.info;
	m_start.game.game.inProgress = true;
	if (m_start.game.game.seed == 0)
	{
		m_start.game.game.seed = (std::uint32_t)(m_now * 2654435761u) | 1u; // the host's choice (retail: GetTickCount)
	}
	m_start.runAhead = m_options.runAhead;
	m_start.crcInterval = m_options.crcInterval;
	m_start.profile = m_profile;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		m_start.networkSlot[(size_t)s] = m_current.info.slots[s].isHuman();
		m_start.addresses[(size_t)s] = m_current.game[(size_t)s]; // the host's own ip is 0: a player takes the address it heard the host from
	}
	m_start.localSlot = 0;
	m_current.inProgress = true;
	m_starting = true;
	m_startAcked.fill(false);
	m_lastStartSend = 0;
	note("starting the game");
	return true;
}

void LANAPI::leaveToLobby()
{
	m_inGame = false;
	m_host = false;
	m_localSlot = -1;
	m_current = LANGame();
	m_pending = ACT_NONE;
	m_starting = false;
}

int LANAPI::slotOf(const NetAddress &lobbyAddress) const
{
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_current.info.slots[s].isHuman() && m_current.lobby[(size_t)s] == lobbyAddress)
		{
			return s;
		}
	}
	return -1;
}

std::vector<LANAPI::Event> LANAPI::takeEvents()
{
	std::vector<Event> e;
	e.swap(m_events);
	return e;
}

// ---- update -----------------------------------------------------------------------------------------------------------------------
void LANAPI::update(std::uint64_t now)
{
	m_now = now;
	if (!m_lobbySocket)
	{
		return;
	}
	NetAddress from;
	std::vector<std::uint8_t> bytes;
	while (m_lobbySocket->receiveFrom(from, bytes))
	{
		handle(from, bytes, now);
	}
	// ZH: the periodic announcements
	if (m_lastResend == 0 || now - m_lastResend >= m_options.resendMs)
	{
		m_lastResend = now;
		if (!m_inGame)
		{
			sendToAll(header(LANMessageType::LOBBY_ANNOUNCE));
		}
		else if (m_host && !m_current.inProgress)
		{
			sendToAll(gameAnnounce());
			broadcastOptions();
		}
		else if (!m_current.inProgress)
		{
			std::vector<std::uint8_t> h = header(LANMessageType::GAME_OPTIONS);
			h.push_back(OPT_HELLO);
			sendTo(m_current.lobby[0], h);
		}
	}
	// ZH: who has not been heard for a while
	const std::uint64_t forget = 2ull * m_options.resendMs;
	const size_t gamesBefore = m_games.size();
	m_games.erase(std::remove_if(m_games.begin(), m_games.end(), [&](const LANGame &g) { return now - g.lastHeardHost > forget; }), m_games.end());
	if (m_games.size() != gamesBefore)
	{
		event(Event{ Event::GameList });
	}
	const size_t playersBefore = m_lobbyPlayers.size();
	m_lobbyPlayers.erase(std::remove_if(m_lobbyPlayers.begin(), m_lobbyPlayers.end(), [&](const LobbyPlayer &p) { return now - p.lastHeard > forget; }),
		m_lobbyPlayers.end());
	if (m_lobbyPlayers.size() != playersBefore)
	{
		event(Event{ Event::PlayerList });
	}
	if (m_inGame && !m_current.inProgress)
	{
		if (!m_host && now - m_current.lastHeardHost > 16ull * m_options.resendMs)
		{
			note("the host is not responding");
			event(Event{ Event::Chat, RET_OK, -1, true, u"", u"LAN:HostNotResponding" });
			event(Event{ Event::HostLeave });
			leaveToLobby();
		}
		else if (m_host)
		{
			for (int s = 1; s < MAX_SLOTS; ++s)
			{
				if (m_current.info.slots[s].isHuman() && now - m_current.lastHeard[(size_t)s] > 8ull * m_options.resendMs)
				{
					const std::u16string who = m_current.info.slots[s].name;
					m_current.info.slots[s] = SkirmishGameSlot();
					m_current.info.slots[s].state = SLOT_OPEN;
					m_current.lobby[(size_t)s] = NetAddress();
					m_current.game[(size_t)s] = NetAddress();
					note("slot " + std::to_string(s) + " dropped (not heard)");
					event(Event{ Event::Chat, RET_OK, s, true, who, u"LAN:PlayerDropped" });
					event(Event{ Event::PlayerLeave, RET_OK, s, false, who });
					broadcastOptions();
				}
			}
		}
	}
	// the pending join / leave
	if (m_pending != ACT_NONE && now > m_expiration)
	{
		if (m_pending == ACT_JOIN)
		{
			event(Event{ Event::GameJoin, RET_TIMEOUT });
		}
		m_pending = ACT_NONE;
	}
	// OpenBFME: the host repeats GAME_START until every player answered, then starts too
	if (m_starting && m_host)
	{
		bool all = true;
		for (int s = 1; s < MAX_SLOTS; ++s)
		{
			all = all && (!m_current.info.slots[s].isHuman() || m_startAcked[(size_t)s]);
		}
		if (all)
		{
			m_starting = false;
			m_started = true;
			note("every player has the start");
			event(Event{ Event::GameStart });
		}
		else if (m_lastStartSend == 0 || now - m_lastStartSend >= m_options.startResendMs)
		{
			if (m_lastStartSend == 0)
			{
				m_expiration = now + kStartGiveUpMs;
			}
			else if (now > m_expiration)
			{
				m_errors.push_back("a player did not answer the game start in " + std::to_string(kStartGiveUpMs) + " ms");
				m_starting = false;
				m_current.inProgress = false;
				return;
			}
			m_lastStartSend = now;
			for (int s = 1; s < MAX_SLOTS; ++s)
			{
				if (m_current.info.slots[s].isHuman() && !m_startAcked[(size_t)s])
				{
					std::vector<std::uint8_t> h = header(LANMessageType::GAME_START);
					NetByteWriter w;
					w.u8((std::uint8_t)s); // the receiver's slot
					w.bytes(LANLobby::encodeStart(m_start));
					std::vector<std::uint8_t> b = w.take();
					h.insert(h.end(), b.begin(), b.end());
					sendTo(m_current.lobby[(size_t)s], h);
				}
			}
		}
	}
}

void LANAPI::handle(const NetAddress &from, const std::vector<std::uint8_t> &bytes, std::uint64_t now)
{
	NetByteReader r(bytes);
	if (r.u16() != kMagic || r.failed())
	{
		return; // not a lobby message (the game socket's datagrams never come here)
	}
	if (r.u8() != kVersion)
	{
		m_errors.push_back("a LAN lobby message of another version from " + from.text());
		return;
	}
	const std::uint8_t type = r.u8();
	const std::uint32_t nonce = r.u32();
	const std::u16string sender = r.wstr();
	if (nonce == m_nonce)
	{
		return; // our own message (a broadcast comes back on every interface)
	}
	if (r.failed())
	{
		m_errors.push_back("a malformed LAN lobby message from " + from.text());
		return;
	}
	// lane MP-2 (review): every message is read whole and checked (nothing missing, nothing after it) before it changes any lobby state
	auto complete = [&](const char *what) {
		if (r.failed() || !r.atEnd())
		{
			m_errors.push_back(std::string("a malformed ") + what + " from " + from.text() + ": "
				+ (r.badFlag() ? std::string("a flag that is not 0 / 1") : r.failed() ? std::string("truncated") : std::to_string(r.remaining()) + " trailing bytes"));
			return false;
		}
		return true;
	};
	auto heardPlayer = [&]() {
		for (LobbyPlayer &p : m_lobbyPlayers)
		{
			if (p.address == from)
			{
				p.lastHeard = now;
				if (p.name != sender)
				{
					p.name = sender;
					event(Event{ Event::NameChange, RET_OK, -1, false, sender });
				}
				return;
			}
		}
		m_lobbyPlayers.push_back(LobbyPlayer{ sender, from, now });
		event(Event{ Event::PlayerList });
	};
	auto heardInGame = [&]() {
		if (m_inGame)
		{
			const int s = slotOf(from);
			if (s >= 0)
			{
				m_current.lastHeard[(size_t)s] = now;
			}
			if (!m_host && from == m_current.lobby[0])
			{
				m_current.lastHeardHost = now;
			}
		}
	};
	switch (type)
	{
	case LANMessageType::REQUEST_LOCATIONS:
		// ZH handleRequestLocations: a lobby player answers with LOBBY_ANNOUNCE, a host with GAME_ANNOUNCE
		if (!complete("location request"))
		{
			return;
		}
		heardInGame();
		if (!m_inGame)
		{
			sendTo(from, header(LANMessageType::LOBBY_ANNOUNCE));
		}
		else if (m_host && !m_current.inProgress)
		{
			sendTo(from, gameAnnounce());
		}
		return;
	case LANMessageType::LOBBY_ANNOUNCE:
		if (complete("lobby announcement"))
		{
			heardInGame();
			heardPlayer();
		}
		return;
	case LANMessageType::REQUEST_LOBBY_LEAVE:
	{
		if (!complete("lobby leave"))
		{
			return;
		}
		heardInGame();
		const size_t before = m_lobbyPlayers.size();
		m_lobbyPlayers.erase(std::remove_if(m_lobbyPlayers.begin(), m_lobbyPlayers.end(), [&](const LobbyPlayer &p) { return p.address == from; }),
			m_lobbyPlayers.end());
		if (before != m_lobbyPlayers.size())
		{
			event(Event{ Event::PlayerList });
		}
		return;
	}
	case LANMessageType::GAME_ANNOUNCE:
		if (handleGameAnnounce(r, sender, from, now))
		{
			heardInGame();
		}
		return;
	case LANMessageType::REQUEST_JOIN:
		if (handleRequestJoin(r, sender, from, now))
		{
			heardInGame();
		}
		return;
	case LANMessageType::JOIN_ACCEPT:
	{
		const int slot = r.u8();
		if (!complete("join accept"))
		{
			return;
		}
		if (m_pending != ACT_JOIN || !(from == m_joinTarget) || slot <= 0 || slot >= MAX_SLOTS)
		{
			return;
		}
		m_pending = ACT_NONE;
		m_inGame = true;
		m_host = false;
		m_localSlot = slot;
		m_current = LANGame();
		m_current.host = from;
		m_current.lobby[0] = from;
		m_current.lastHeardHost = now;
		m_started = false;
		for (const LANGame &g : m_games)
		{
			if (g.host == from)
			{
				m_current.name = g.name;
				m_current.info = g.info;
			}
		}
		note("joined as slot " + std::to_string(slot));
		event(Event{ Event::GameJoin, RET_OK, slot });
		return;
	}
	case LANMessageType::JOIN_DENY:
	{
		const int reason = r.u8();
		if (!complete("join deny"))
		{
			return;
		}
		if (reason <= RET_OK || reason > RET_UNKNOWN)
		{
			m_errors.push_back("a join deny from " + from.text() + " with the reason " + std::to_string(reason));
			return;
		}
		if (m_pending != ACT_JOIN || !(from == m_joinTarget))
		{
			return;
		}
		m_pending = ACT_NONE;
		note("join denied (" + std::to_string(reason) + ")");
		event(Event{ Event::GameJoin, (ReturnType)reason });
		return;
	}
	case LANMessageType::REQUEST_GAME_LEAVE:
	{
		const bool kicked = r.flag();
		if (!complete("game leave"))
		{
			return;
		}
		heardInGame();
		if (!m_inGame)
		{
			return;
		}
		if (!m_host && from == m_current.lobby[0])
		{
			// the host left (ZH OnHostLeave) or removed us
			event(Event{ kicked ? Event::Kicked : Event::HostLeave });
			note(kicked ? "kicked by the host" : "the host left");
			leaveToLobby();
			return;
		}
		if (m_host)
		{
			const int s = slotOf(from);
			if (s > 0)
			{
				const std::u16string who = m_current.info.slots[s].name;
				m_current.info.slots[s] = SkirmishGameSlot();
				m_current.info.slots[s].state = SLOT_OPEN;
				m_current.lobby[(size_t)s] = NetAddress();
				m_current.game[(size_t)s] = NetAddress();
				m_current.accepted.fill(false);
				event(Event{ Event::PlayerLeave, RET_OK, s, false, who });
				broadcastOptions();
			}
		}
		return;
	}
	case LANMessageType::SET_ACCEPT:
	{
		const bool flag = r.flag();
		if (!complete("accept"))
		{
			return;
		}
		heardInGame();
		const int s = slotOf(from);
		if (m_host && s > 0)
		{
			m_current.accepted[(size_t)s] = flag;
			event(Event{ Event::Accept, RET_OK, s, flag });
			broadcastOptions();
		}
		return;
	}
	case LANMessageType::CHAT:
	{
		const std::u16string text = r.wstr();
		const bool system = r.flag();
		if (!complete("chat"))
		{
			return;
		}
		heardInGame();
		if (m_inGame && slotOf(from) >= 0)
		{
			event(Event{ Event::Chat, RET_OK, slotOf(from), system, sender, text });
		}
		return;
	}
	case LANMessageType::GAME_OPTIONS:
		if (handleGameOptions(r, from, now))
		{
			heardInGame();
		}
		return;
	case LANMessageType::GAME_START:
		if (handleGameStart(r, from))
		{
			heardInGame();
		}
		return;
	case LANMessageType::GAME_START_ACK:
	{
		if (!complete("game start answer"))
		{
			return;
		}
		heardInGame();
		const int s = slotOf(from);
		if (m_host && m_starting && s > 0)
		{
			m_startAcked[(size_t)s] = true;
		}
		return;
	}
	default:
		note("unhandled LAN message type " + std::to_string(type) + " from " + from.text());
		return;
	}
}

bool LANAPI::handleGameAnnounce(NetByteReader &r, const std::u16string &sender, const NetAddress &from, std::uint64_t now)
{
	LANGame g;
	g.name = r.wstr();
	g.inProgress = r.flag();
	g.profileDigest = r.str();
	std::string error;
	if (!NetPacket::readGameInfo(r, g.info, &error) || r.failed() || !r.atEnd())
	{
		m_errors.push_back("a malformed game announcement from " + from.text() + ": " + (error.empty() ? std::string("truncated or trailing bytes") : error));
		return false;
	}
	(void)sender;
	g.host = from;
	g.lastHeardHost = now;
	for (LANGame &e : m_games)
	{
		if (e.host == from)
		{
			e = g;
			event(Event{ Event::GameList });
			return true;
		}
	}
	m_games.push_back(g);
	event(Event{ Event::GameList });
	return true;
}

bool LANAPI::handleRequestJoin(NetByteReader &r, const std::u16string &sender, const NetAddress &from, std::uint64_t now)
{
	// ZH handleRequestJoin: the host checks the request and gives the joiner the first open slot
	const std::string digest = r.str();
	const std::uint16_t gamePort = r.u16();
	if (r.failed() || !r.atEnd())
	{
		m_errors.push_back("a malformed join request from " + from.text());
		return false;
	}
	if (!amIHost())
	{
		return true;
	}
	auto deny = [&](ReturnType why) {
		std::vector<std::uint8_t> h = header(LANMessageType::JOIN_DENY);
		h.push_back((std::uint8_t)why);
		sendTo(from, h);
		note("denied a join from " + from.text() + " (" + std::to_string((int)why) + ")");
	};
	const int already = slotOf(from);
	if (already > 0)
	{
		std::vector<std::uint8_t> h = header(LANMessageType::JOIN_ACCEPT); // a repeated request: the same answer
		h.push_back((std::uint8_t)already);
		sendTo(from, h);
		return true;
	}
	if (digest != m_profile.digest)
	{
		deny(RET_CRC_MISMATCH);
		return true;
	}
	if (m_current.inProgress)
	{
		deny(RET_GAME_STARTED);
		return true;
	}
	for (const SkirmishGameSlot &s : m_current.info.slots)
	{
		if (s.isHuman() && s.name == sender)
		{
			deny(RET_DUPLICATE_NAME);
			return true;
		}
	}
	int slot = -1;
	for (int s = 1; s < MAX_SLOTS && slot < 0; ++s)
	{
		slot = m_current.info.slots[s].state == SLOT_OPEN ? s : -1;
	}
	if (slot < 0)
	{
		deny(RET_GAME_FULL);
		return true;
	}
	SkirmishGameSlot &gs = m_current.info.slots[slot];
	gs = SkirmishGameSlot();
	gs.state = SLOT_PLAYER;
	gs.name = sender;
	m_current.lobby[(size_t)slot] = from;
	m_current.game[(size_t)slot] = NetAddress{ from.ip, gamePort };
	m_current.lastHeard[(size_t)slot] = now;
	m_current.accepted.fill(false);
	std::vector<std::uint8_t> h = header(LANMessageType::JOIN_ACCEPT);
	h.push_back((std::uint8_t)slot);
	sendTo(from, h);
	note("slot " + std::to_string(slot) + " joined: " + toNarrow(sender));
	event(Event{ Event::PlayerJoin, RET_OK, slot, false, sender });
	broadcastOptions();
	return true;
}

bool LANAPI::handleGameOptions(NetByteReader &r, const NetAddress &from, std::uint64_t now)
{
	const std::uint8_t kind = r.u8();
	if (r.failed())
	{
		m_errors.push_back("a malformed game options message from " + from.text());
		return false;
	}
	if (kind == OPT_HELLO)
	{
		if (!r.atEnd())
		{
			m_errors.push_back("a game keep-alive from " + from.text() + " with " + std::to_string(r.remaining()) + " trailing bytes");
			return false;
		}
		return true; // the keep-alive (its arrival refreshed the slot)
	}
	if (kind == OPT_SLOT_REQUEST)
	{
		return handleSlotRequest(r, from);
	}
	if (kind == OPT_SLOT_CREATE_A_HERO)
	{
		return handleSlotCreateAHero(r, from);
	}
	if (kind != OPT_FULL)
	{
		m_errors.push_back("game options of the unknown kind " + std::to_string(kind) + " from " + from.text());
		return false;
	}
	LANGame g;
	g.name = r.wstr();
	std::string error;
	if (!NetPacket::readGameInfo(r, g.info, &error))
	{
		m_errors.push_back("malformed game options: " + error);
		return false;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		g.lobby[(size_t)s] = readAddress(r);
		g.game[(size_t)s] = readAddress(r);
		g.accepted[(size_t)s] = r.flag();
	}
	if (r.failed() || !r.atEnd())
	{
		m_errors.push_back((r.badFlag() ? "game options with a flag that is not 0 / 1 from " : r.failed() ? "truncated game options from " : "game options with trailing bytes from ") + from.text());
		return false;
	}
	std::string why;
	if (!gameInfoValid(g.info, &why))
	{
		m_errors.push_back("game options from " + from.text() + " with " + why + ": refused");
		return false;
	}
	if (!m_inGame || m_host || !(from == m_current.lobby[0]))
	{
		return true;
	}
	// the host's own lobby address is the one we hear it from
	g.lobby[0] = from;
	g.game[0].ip = g.game[0].ip == 0 ? from.ip : g.game[0].ip;
	g.host = from;
	g.lastHeardHost = now;
	g.lastHeard = m_current.lastHeard;
	if (!g.info.slots[m_localSlot].isHuman() || g.info.slots[m_localSlot].name != m_name)
	{
		// our slot is not ours any more: the host removed us (ZH: the slot list without our name)
		note("our slot was taken away");
		event(Event{ Event::Kicked });
		leaveToLobby();
		return true;
	}
	m_current = g;
	event(Event{ Event::GameOptions });
	return true;
}

bool LANAPI::handleSlotRequest(NetByteReader &r, const NetAddress &from)
{
	// a player's request for its own slot (ZH LanGameOptionsMenu -> RequestGameOptions -> the host's handleGameOptions)
	const int playerTemplate = r.i32();
	const int color = r.i32();
	const int team = r.i32();
	const int startPos = r.i32();
	if (r.failed() || !r.atEnd())
	{
		m_errors.push_back("a malformed slot request from " + from.text());
		return false;
	}
	std::string why;
	if (!slotRequestValid(playerTemplate, color, team, startPos, &why))
	{
		m_errors.push_back("a slot request from " + from.text() + " with the " + why + ": refused");
		return false;
	}
	const int s = slotOf(from);
	if (!m_inGame || !m_host || s <= 0)
	{
		return true;
	}
	SkirmishGameInfo info = m_current.info;
	applySlotRequest(info, s, playerTemplate, color, team, startPos);
	hostSetOptions(info);
	return true;
}

bool LANAPI::handleGameStart(NetByteReader &r, const NetAddress &from)
{
	const int slot = r.u8();
	const std::vector<std::uint8_t> payload = r.bytes();
	if (r.failed() || !r.atEnd())
	{
		m_errors.push_back("a malformed game start from " + from.text());
		return false;
	}
	LobbyStart s;
	std::string error;
	if (!LANLobby::decodeStart(payload, s, &error))
	{
		m_errors.push_back("a malformed game start from " + from.text() + ": " + error);
		return false;
	}
	if (!m_inGame || m_host || !(from == m_current.lobby[0]) || slot != m_localSlot)
	{
		return true;
	}
	std::vector<std::uint8_t> ack = header(LANMessageType::GAME_START_ACK);
	sendTo(from, ack);
	if (m_started)
	{
		return true; // a repeat
	}
	if (s.profile.digest != m_profile.digest)
	{
		m_errors.push_back("the game start of another profile identity: " + ProfileIdentity::difference(m_profile, s.profile));
		return true;
	}
	s.localSlot = m_localSlot;
	if (s.addresses[0].ip == 0)
	{
		s.addresses[0].ip = from.ip; // the host's game socket is where we hear the host from
	}
	m_start = s;
	m_started = true;
	m_current.inProgress = true;
	note("the game starts");
	event(Event{ Event::GameStart });
	return true;
}
