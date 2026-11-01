// OpenBFME. GPL-3.0.
//
// libFuzzer targets of the decoders that read peer, relay, lobby and replay bytes (OPENBFME_FUZZ=ON with Clang). OPENBFME_FUZZ_KIND selects one target
// per executable; each target only checks that a decoder refuses or accepts without undefined behaviour, a leak or unbounded work.
// Lane NETDIAG-1: ported from a contributor's change and extended to this tree's formats: the envelope target drives the real relay (RelayCore and
// RelayCanonicalAuthority, linked as openbfme_relay is: services/online/relay/CMakeLists.txt) with sixteen-slot tickets, rejoin and spectator
// tickets, canonical games, the command log, and AUTH-1's sealed version-5 envelope (valid tags, so the opened datagram is what gets fuzzed); the
// relay_log target reads version-2 / version-4 logs (sixteen slots) and a version-2 log's INPUT sidecar. Not simulation code; never built by default.

#include "Common/MiniJson.h"
#include "GameNetwork/RelayProtocol.h"
#if OPENBFME_FUZZ_KIND != 3 // the envelope target links the relay's sources only (services/online/relay/CMakeLists.txt)
#include "Common/Recorder.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/PlatformApiClient.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/Transport.h"
#endif

#if OPENBFME_FUZZ_KIND == 3
#include "Common/Crc32.h"
#include "GameNetwork/RelayAuth.h"
#include "RelayCore.h"
#include <array>
#include <filesystem>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
RelayProtocol::KeyRing fuzzKeys()
{
	RelayProtocol::KeyRing keys;
	std::vector<std::uint8_t> key(32);
	for (size_t i = 0; i < key.size(); ++i)
	{
		key[i] = (std::uint8_t)i;
	}
	keys[1] = key;
	return keys;
}

void require(bool condition)
{
	if (!condition)
	{
		std::abort();
	}
}

// a decoder that refuses must say why
void refusedWithReason(bool ok, const std::string &error)
{
	require(ok || !error.empty());
}

// splits the input into datagrams: u16 length, then that many bytes (the rest when shorter)
std::vector<std::vector<std::uint8_t>> datagrams(const std::uint8_t *data, size_t size, size_t &pos, size_t maxCount)
{
	std::vector<std::vector<std::uint8_t>> out;
	while (pos + 2 <= size && out.size() < maxCount)
	{
		size_t n = (size_t)data[pos] | ((size_t)data[pos + 1] << 8);
		pos += 2;
		n = std::min(n, size - pos);
		out.emplace_back(data + pos, data + pos + n);
		pos += n;
	}
	return out;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size)
{
	const std::vector<std::uint8_t> bytes(data, data + size);
	std::string error;
#if OPENBFME_FUZZ_KIND == 0 // command: NetPacket::decodeCommand (frame bundles, FRAMERESEND, disconnect and rejoin commands)
	NetCommandMsg c;
	const bool ok = NetPacket::decodeCommand(bytes, c, &error);
	refusedWithReason(ok, error);
	if (ok)
	{
		std::string again;
		const std::vector<std::uint8_t> encoded = NetPacket::encodeCommand(c, &again);
		NetCommandMsg d;
		require(!encoded.empty() && NetPacket::decodeCommand(encoded, d, &again));
	}
#elif OPENBFME_FUZZ_KIND == 1 // message: GameMessage, GameInfo and NewGameMessage readers
	if (size == 0)
	{
		return 0;
	}
	NetByteReader r(data + 1, size - 1);
	switch (data[0] % 3)
	{
	case 0:
	{
		const bool ok = (bool)NetPacket::readGameMessage(r, &error);
		refusedWithReason(ok, error);
		break;
	}
	case 1:
	{
		SkirmishGameInfo g;
		const bool ok = NetPacket::readGameInfo(r, g, &error, (data[0] & 8) != 0, (data[0] & 16) != 0);
		refusedWithReason(ok, error);
		break;
	}
	default:
	{
		NewGameMessage m;
		const bool ok = NetPacket::readNewGameMessage(r, m, &error, (data[0] & 16) != 0);
		refusedWithReason(ok, error);
		break;
	}
	}
#elif OPENBFME_FUZZ_KIND == 2 // ticket: the relay's ticket check, the key ring and the base64 forms
	static const RelayProtocol::KeyRing keys = fuzzKeys();
	RelayProtocol::Ticket t;
	RelayProtocol::RefuseReason reason{};
	const std::uint32_t now = size >= 4 ? (std::uint32_t)data[0] | ((std::uint32_t)data[1] << 8) | ((std::uint32_t)data[2] << 16) | ((std::uint32_t)data[3] << 24) : 0;
	const bool ok = RelayProtocol::verifyTicket(data, size, keys, now, t, &error, &reason);
	refusedWithReason(ok, error);
	const std::string text(bytes.begin(), bytes.end());
	std::vector<std::uint8_t> decoded;
	if (RelayProtocol::base64UrlDecode(text, decoded))
	{
		require(RelayProtocol::base64UrlEncode(decoded.data(), decoded.size()) == text);
	}
	decoded.clear();
	(void)RelayProtocol::base64Decode(text, decoded);
	RelayProtocol::KeyRing ring;
	std::string ringError;
	refusedWithReason(RelayProtocol::parseKeyRing(text, ring, &ringError), ringError);
#elif OPENBFME_FUZZ_KIND == 3 // envelope: the relay's datagram handling (HELLO, DATA, LOG_REQUEST, BYE ...) with valid tickets and sealed envelopes
	// byte 0: bit 0 the command log (a per-process directory), bit 1 canonical games, bit 2 sixteen-slot masks. Then datagrams (u16 length, bytes):
	// byte 0 of each picks the address (low nibble: 16 endpoints) and the operation (bits 4..6: 0 structured extras, 1..3 raw, 4/5 HELLO, 6/7 DATA),
	// bit 7 advances the clock and ticks
	if (size == 0)
	{
		return 0;
	}
	static const RelayProtocol::KeyRing keys = fuzzKeys();
	static const std::string logDir = [] {
		const std::filesystem::path p = std::filesystem::temp_directory_path() / ("openbfme_fuzz_envelope_" + std::to_string((long long)getpid()));
		std::filesystem::create_directories(p);
		return p.string();
	}();
	const std::uint8_t setup = data[0];
	RelayCore::Options o;
	o.keys = keys;
	o.maxGames = 4;
	if (setup & 1)
	{
		o.logDir = logDir;
	}
	const bool canonical = (setup & 2) != 0, sixteen = (setup & 4) != 0;
	const std::uint64_t gameId = 0x0123456789ABCDEFull;
	const std::uint32_t unix = 1700000000u;
	struct Endpoint
	{
		std::vector<std::uint8_t> ticket; // the last ticket it said HELLO with (its AUTH-1 keys derive from it)
		RelayAuth::Keys keys;
		std::uint64_t counter = 0;
	};
	std::array<Endpoint, 16> endpoints;
	RelayCore relay(o);
	size_t pos = 1;
	std::uint64_t now = 1000;
	std::vector<RelayCore::Action> drained;
	for (std::vector<std::uint8_t> &d : datagrams(data, size, pos, 256))
	{
		if (d.empty())
		{
			continue;
		}
		const std::uint8_t sel = d[0];
		const unsigned index = sel & 0x0Fu;
		Endpoint &e = endpoints[index];
		const RelayAddress from{ 0x0A000001u, (std::uint16_t)(4000 + index) };
		const unsigned op = (sel >> 4) & 7u;
		const std::vector<std::uint8_t> rest(d.begin() + 1, d.end());
		auto at = [&](size_t i) { return i < rest.size() ? rest[i] : (std::uint8_t)0; };
		if (op == 4 || op == 5)
		{
			// a HELLO with a valid ticket: a START ticket for the slot the input names, or a rejoin / spectator ticket; op 5 seals it (version 5)
			RelayProtocol::Ticket t;
			t.keyId = 1;
			t.gameId = gameId;
			t.slot = (std::uint8_t)(at(0) & (sixteen ? 15 : 7));
			const std::uint16_t mask = sixteen ? (std::uint16_t)(at(1) | (at(2) << 8)) : at(1);
			t.slotMask = (std::uint16_t)(mask | (1u << t.slot));
			t.expires = unix + 3600;
			t.mode = canonical ? RelayProtocol::TICKET_MODE_CANONICAL : 0;
			if (at(0) & 0x80)
			{
				t.mode = 0;
				t.generation = (std::uint16_t)(at(3) & 3);
				e.ticket = RelayProtocol::signRejoinTicket(t, keys.at(1));
			}
			else if (at(0) & 0x40)
			{
				t.mode = 0;
				t.spectator = true;
				t.delaySeconds = at(3);
				e.ticket = RelayProtocol::signSpectatorTicket(t, keys.at(1));
			}
			else
			{
				e.ticket = RelayProtocol::signTicket(t, keys.at(1));
			}
			const RelayAuth::SessionKey k = RelayAuth::sessionKey(keys.at(1).data(), keys.at(1).size(), e.ticket.data(), e.ticket.size());
			e.keys = RelayAuth::packetKeys(k, gameId, t.slot, t.generation);
			e.counter = 0;
			std::vector<std::uint8_t> token;
			if (at(4) & 1)
			{
				token.assign(RelayProtocol::kSessionTokenBytes, at(5)); // a wrong token: the rebind refusal path
			}
			d = RelayProtocol::encodeHello(e.ticket, token, (std::uint8_t)((canonical ? RelayProtocol::CAP_CANONICAL : 0) | (op == 5 ? RelayProtocol::CAP_AUTH : 0)));
			if (op == 5)
			{
				RelayAuth::seal(d, e.keys.c2r, e.counter++);
			}
		}
		else if (op == 6 || op == 7)
		{
			// a DATA: target = byte 0's low six bits; bit 6 writes the transport packet's CRC (Transport.h: u32 CRC-32 of the bytes after it), so the
			// fuzzed layout gets past the CRC check. Op 6 seals it with the endpoint's c2r key (a valid tag: the relay opens it and data() reads the
			// fuzzed packet); op 7 sends it unsealed
			std::vector<std::uint8_t> packet(rest.begin() + (rest.empty() ? 0 : 1), rest.end());
			if ((at(0) & 0x40) && packet.size() >= 4)
			{
				const std::uint32_t crc = crc32Bytes(packet.data() + 4, packet.size() - 4);
				for (int i = 0; i < 4; ++i)
				{
					packet[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
				}
			}
			d = RelayProtocol::encodeData((std::uint8_t)(at(0) & 0x3F), packet);
			if (op == 6)
			{
				RelayAuth::seal(d, e.keys.c2r, e.counter++);
			}
		}
		else if (op == 0)
		{
			// what random bytes rarely reach: byte 0 picks a LOG_REQUEST (the rejoin catch-up / spectator read), a previous-release HELLO (envelope
			// version 3: a version-2 log and its INPUT sidecar), or a canonical game's NS input; bit 3 seals it with the endpoint's key
			const unsigned kind = at(0) % 3;
			if (kind == 0)
			{
				d = RelayProtocol::encodeLogRequest((std::uint32_t)at(1) | ((std::uint32_t)at(2) << 8) | ((std::uint32_t)at(3) << 16));
			}
			else if (kind == 1)
			{
				RelayProtocol::Ticket t;
				t.keyId = 1;
				t.gameId = gameId;
				t.slot = (std::uint8_t)(at(1) & 7);
				t.slotMask = (std::uint16_t)(at(2) | (1u << t.slot));
				t.expires = unix + 3600;
				e.ticket = RelayProtocol::signTicket(t, keys.at(1));
				const RelayAuth::SessionKey k = RelayAuth::sessionKey(keys.at(1).data(), keys.at(1).size(), e.ticket.data(), e.ticket.size());
				e.keys = RelayAuth::packetKeys(k, gameId, t.slot, 0);
				e.counter = 0;
				d = RelayProtocol::encodeHello(e.ticket);
				d[2] = RelayProtocol::kVersionLegacy;
			}
			else
			{
				std::vector<std::uint8_t> ns{ 'N', 'S' };
				ns.insert(ns.end(), rest.begin() + (rest.size() > 1 ? 2 : rest.size()), rest.end());
				d = RelayProtocol::encodeData((std::uint8_t)(at(1) & 15), ns);
			}
			if ((at(0) & 8) && kind != 1)
			{
				RelayAuth::seal(d, e.keys.c2r, e.counter++);
			}
		}
		else
		{
			d = rest; // raw bytes: every envelope version and kind, including a forged version 5
		}
		const RelayCore::Action a = relay.handle(from, d.data(), d.size(), now, unix);
		require(a.kind == RelayCore::Action::NONE || (a.to.ip == 0x0A000001u && a.to.port >= 4000 && a.to.port < 4016)); // only ever to an endpoint
		drained.clear();
		relay.takePending(drained);
		if (sel & 0x80)
		{
			now += 1 + at(0) * 16u;
			relay.tick(now, &drained);
		}
	}
	JsonValue health;
	std::string healthError;
	require(JsonValue::parse(relay.healthJson(now), health, &healthError)); // lane NETDIAG-1: the snapshot is always valid JSON
	relay.shutdown(now + 1);
	require(relay.stats().accountingErrors == 0);
	require(relay.heldBytes() == 0);
#elif OPENBFME_FUZZ_KIND == 4 // status: STATUS / envelope recognition
	std::vector<RelayProtocol::SlotStatus> out;
	if (RelayProtocol::decodeStatus(data, size, out))
	{
		const auto encoded = RelayProtocol::encodeStatus(out);
		std::vector<RelayProtocol::SlotStatus> again;
		require(RelayProtocol::decodeStatus(encoded.data(), encoded.size(), again));
	}
	(void)RelayProtocol::isRelayDatagram(data, size);
#elif OPENBFME_FUZZ_KIND == 5 // relay_log: the relay's command log (complete and growing), a version-2 log's INPUT sidecar, and the assembly
	// u16 length, the log's bytes; the rest is the sidecar (RelayLogFile::load reads it from <log>.input for a version-2 log)
	if (size < 2)
	{
		return 0;
	}
	const size_t logBytes = std::min(size - 2, (size_t)data[0] | ((size_t)data[1] << 8));
	const std::vector<std::uint8_t> logPart(data + 2, data + 2 + logBytes), sidecar(data + 2 + logBytes, data + size);
	for (bool partial : { false, true })
	{
		RelayLogFile log;
		error.clear();
		const bool ok = RelayLogFile::parse(logPart, log, &error, partial);
		refusedWithReason(ok, error);
		if (!ok)
		{
			continue;
		}
		require(log.slotMask != 0 && (log.version == 4 || log.slotMask <= 0xFF));
		if (log.version < 4 && !sidecar.empty())
		{
			std::string sideError;
			refusedWithReason(RelayLogFile::parseInputSidecar(sidecar, "0123456789abcdef.obfmerlog", log, &sideError), sideError);
		}
		NetworkConfig config;
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			config.slotPlayerIndex[(size_t)i] = (log.slotMask >> i) & 1 ? i : -1;
		}
		RelayLog::Assembled a;
		std::string why;
		refusedWithReason(RelayLog::assemble(log, config, a, &why, partial), why);
	}
#elif OPENBFME_FUZZ_KIND == 6 // replay: ReplayFile::parse
	ReplayFile replay;
	refusedWithReason(ReplayFile::parse(bytes, replay, &error), error);
#elif OPENBFME_FUZZ_KIND == 7 // transport: Transport's receive path over loopback (peer to peer and relay mode)
	if (size == 0)
	{
		return 0;
	}
	static UDP *local = nullptr, *remote = nullptr;
	if (!local)
	{
		local = new UDP();
		remote = new UDP();
		std::string e;
		require(local->bind(0x7F000001u, 0, &e) && remote->bind(0x7F000001u, 0, &e));
	}
	Transport::Options o;
	o.maxBufferedBytes = 1u << 16;
	o.maxCommandBytes = 1u << 15;
	o.window = 64;
	const bool relayMode = (data[0] & 1) != 0;
	if (relayMode)
	{
		o.relay.enabled = true;
		o.relay.address = remote->localAddress();
		o.relay.ticket.assign(RelayProtocol::kTicketBytes, 0);
	}
	std::uint64_t clock = 1000;
	{
		Transport t(*local, 0, o);
		t.setClock([&clock]() { return clock; });
		t.setPeer(1, remote->localAddress(), 0, data[0] & 2 ? 0xFFFFFFF0u : 0u);
		t.setPeer(2, remote->localAddress());
		size_t pos = 1;
		for (const std::vector<std::uint8_t> &d : datagrams(data, size, pos, 64))
		{
			remote->sendTo(local->localAddress(), d.data(), d.size());
			t.service();
			clock += 7;
			int slot = -1;
			std::vector<std::uint8_t> out;
			while (t.receive(slot, out))
			{
				require(slot >= 0 && slot < MAX_SLOTS);
			}
		}
	}
	NetAddress from;
	std::vector<std::uint8_t> drain;
	while (remote->receiveFrom(from, drain) || local->receiveFrom(from, drain))
	{
	}
	NetAddress parsed;
	(void)NetAddress::parse(std::string(bytes.begin(), bytes.end()), parsed);
#elif OPENBFME_FUZZ_KIND == 8 // lan_start: the LAN lobby's START
	LobbyStart s;
	refusedWithReason(LANLobby::decodeStart(bytes, s, &error), error);
#elif OPENBFME_FUZZ_KIND == 9 // online_start: the lobby service's START JSON
	if (size == 0)
	{
		return 0;
	}
	static const ProfileIdentity profile = ProfileIdentity::fromText("openbfme-profile 1\nprofile enhanced\n");
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 16;
	limits.colorCount = 16;
	const OnlineStart::Kind kinds[] = { OnlineStart::Kind::Start, OnlineStart::Kind::ReplayStart, OnlineStart::Kind::Spectate };
	LobbyStart s;
	refusedWithReason(OnlineStart::parse(std::string(bytes.begin() + 1, bytes.end()), profile, limits, s, &error, kinds[data[0] % 3]), error);
	std::u16string wide;
	(void)OnlineStart::utf8ToUtf16(std::string(bytes.begin(), bytes.end()), wide);
#elif OPENBFME_FUZZ_KIND == 10 // desync: a DESYNCREPORT's half
	DesyncHalf half;
	refusedWithReason(DesyncHalf::decode(bytes, half, &error), error);
#elif OPENBFME_FUZZ_KIND == 11 // platform_api: the platform service's JSON and all four response shapes, plus lobby response bodies
	if (size == 0)
	{
		return 0;
	}
	const std::string text(bytes.begin() + 1, bytes.end());
	JsonValue root;
	const bool json = JsonValue::parse(text, root, &error);
	refusedWithReason(json, error);
	if (json)
	{
		error.clear();
		switch (data[0] % 4)
		{
		case 0:
		{
			std::vector<PlatformApiClient::Tournament> out;
			std::string cursor;
			refusedWithReason(PlatformApiClient::parseTournaments(root, out, &cursor, &error), error);
			break;
		}
		case 1:
		{
			PlatformApiClient::TournamentDetail out;
			refusedWithReason(PlatformApiClient::parseTournament(root, out, &error), error);
			break;
		}
		case 2:
		{
			PlatformApiClient::GlobalStats out;
			refusedWithReason(PlatformApiClient::parseGlobalStats(root, out, &error), error);
			break;
		}
		default:
		{
			std::string state;
			PlatformApiClient::Match match;
			refusedWithReason(PlatformApiClient::parseTicketState(root, state, match, &error), error);
			break;
		}
		}
	}
	OnlineLobbyClient::Options o;
	o.installId = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = 16;
	o.colorCount = 16;
	OnlineLobbyClient lobby(o);
	switch (data[0] % 4)
	{
	case 0: lobby.signInGuest(u"Player"); break;
	case 1: lobby.refreshList(); break;
	case 2: lobby.fetchMotd(); break;
	default: lobby.fetchMine(); break;
	}
	for (const auto &req : lobby.takeRequests())
	{
		lobby.deliver(req.id, 200, text, "");
	}
	(void)lobby.takeEvents();
#elif OPENBFME_FUZZ_KIND == 12 // lan_control: JOIN, ACCEPT, REFUSE and START_ACK datagrams in the LAN lobby poll path
	static UDP *local = nullptr, *remote = nullptr;
	if (!local)
	{
		local = new UDP();
		remote = new UDP();
		std::string e;
		require(local->bind(0x7F000001u, 0, &e) && remote->bind(0x7F000001u, 0, &e));
	}
	const auto profile = ProfileIdentity::fromText("openbfme-profile 1\nprofile enhanced\n");
	size_t pos = 1;
	if (size && (data[0] & 1))
	{
		LANLobbyClient client(*local, remote->localAddress(), u"Player", profile);
		for (const auto &d : datagrams(data, size, pos, 64))
		{
			remote->sendTo(local->localAddress(), d.data(), d.size());
			(void)client.poll();
		}
	}
	else
	{
		LANLobbyHost host(*local, NewGameMessage{}, 0, 5, 0, profile);
		for (const auto &d : datagrams(data, size, pos, 64))
		{
			remote->sendTo(local->localAddress(), d.data(), d.size());
			(void)host.poll();
		}
	}
	NetAddress from;
	std::vector<std::uint8_t> drain;
	while (remote->receiveFrom(from, drain) || local->receiveFrom(from, drain))
	{
	}
#elif OPENBFME_FUZZ_KIND == 13 // lobby_response: authenticated social and lobby response schemas
	if (size == 0)
	{
		return 0;
	}
	OnlineLobbyClient::Options options;
	options.installId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	options.clientPlatform = "linux";
	options.clientVersion = "test";
	options.enableSocial = true;
	OnlineLobbyClient lobby(options);
	lobby.signInGuest(u"Self");
	auto queued = lobby.takeRequests();
	require(queued.size() == 1);
	lobby.deliver(queued.front().id, 200, R"({"v":1,"features":["social"]})", "");
	queued = lobby.takeRequests();
	require(queued.size() == 1);
	lobby.deliver(queued.front().id, 200,
		R"({"v":1,"token":"session","account":{"id":"guest:1","name":"Self"}})", "");
	queued = lobby.takeRequests();
	require(queued.size() == 2);
	const auto ordinary = std::find_if(queued.begin(), queued.end(), [](const auto &r) {
		return r.channel == OnlineLobbyClient::HttpRequest::Ordinary;
	});
	const auto social = std::find_if(queued.begin(), queued.end(), [](const auto &r) {
		return r.channel == OnlineLobbyClient::HttpRequest::Social;
	});
	require(ordinary != queued.end() && social != queued.end());
	const std::string text(bytes.begin() + 1, bytes.end());
	const std::uint8_t selector = data[0] % 11;
	if (selector == 3)
	{
		lobby.deliver(social->id, 200, text, "");
	}
	lobby.deliver(ordinary->id, 200,
		selector == 0 ? text :
		R"({"v":1,"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self","privacy":{"invites":"friends_or_room","challenges":"friends_or_room","requests":"anyone","presence":"friends"},"hidden":false})", "");
	if (selector > 0)
	{
		queued = lobby.takeRequests();
		require(queued.size() == 1);
		lobby.deliver(queued.front().id, 200, selector == 1 ? text : R"({"v":1,"friends":[]})", "");
	}
	if (selector > 1)
	{
		queued = lobby.takeRequests();
		require(queued.size() == 1);
		lobby.deliver(queued.front().id, 200, selector == 2 ? text : R"({"v":1,"games":[]})", "");
	}
	if (selector >= 4)
	{
		switch (selector)
		{
		case 4: lobby.fetchBlocks(); break;
		case 5: lobby.fetchThreads(); break;
		case 6: lobby.fetchMessages("2222222222222222"); break;
		case 7: lobby.generateRecovery(); break;
		case 8: lobby.requestFriend("2222222222222222"); break;
		case 9: lobby.sendInvite("2222222222222222", "game"); break;
		default: lobby.refreshSocial(); break;
		}
		queued = lobby.takeRequests();
		if (!queued.empty())
		{
			lobby.deliver(queued.front().id, 200, text, "");
		}
	}
	(void)lobby.takeSocialEvents();
	(void)lobby.takeEvents();
#else
#error "unknown OPENBFME_FUZZ_KIND"
#endif
	return 0;
}
