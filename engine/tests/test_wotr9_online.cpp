// OpenBFME unit tests. GPL-3.0.
// WOTR2 milestone 9: the OpenBFME lobby's War of the Ring fields and save check remain bound to the named save.

#include "doctest.h"

#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayProtocol.h"

#include <string>
#include <vector>

namespace
{
OnlineLobbyClient::Options options()
{
	OnlineLobbyClient::Options o;
	o.profileDigest = "0123456789abcdef0123456789abcdef";
	o.playerTemplateCount = 8;
	o.colorCount = 8;
	return o;
}

std::string view(const std::string &save, const std::string &check = "", int region = 9)
{
	std::string slots;
	for (int i = 0; i < 8; ++i)
	{
		slots += std::string(i ? "," : "") + "{\"state\":\"" + (i < 2 ? "player" : "closed") + "\",\"name\":\""
			+ (i == 0 ? "Host" : "Joiner") + "\",\"faction\":" + std::to_string(i < 2 ? i : -1) + ",\"color\":-1,\"startPos\":"
			+ std::to_string(i == 1 ? region : -1) + ",\"team\":-1,\"ready\":false,\"you\":" + (i == 0 ? "true" : "false")
			+ (i == 1 ? check : std::string()) + "}";
	}
	return "{\"v\":1,\"game\":{\"id\":\"0123456789abcdef\",\"name\":\"War\",\"state\":\"open\",\"hostSlot\":0,\"private\":false,\"seq\":2,"
		   "\"setup\":{\"map\":\"maps/strategic/strategic.map\",\"gameMode\":1,\"scenario\":\"WarScenario\","
		   "\"startingCash\":-1,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null"
		+ (save.empty() ? std::string() : ",\"resumeSave\":\"" + save + "\",\"resumeHash\":123")
		+ "},\"slots\":[" + slots + "]}}";
}

std::vector<OnlineLobbyClient::Event> answer(OnlineLobbyClient &c, const std::string &body)
{
	const auto requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	c.deliver(requests[0].id, 200, body, "");
	return c.takeEvents();
}
} // namespace

TEST_CASE("wotr9 online phase: ticket binds the original seat, parent war, phase and relay game")
{
	ProfileIdentity own;
	own.digest = options().profileDigest;
	LobbyStart war;
	war.online = true;
	war.profile = own;
	war.onlineGameId = 0x0123456789abcdefull;
	war.localSlot = 1;
	war.game.game.gameMode = 1;
	war.game.game.scenario = "WarScenario";
	for (int s = 0; s < 2; ++s)
	{
		war.game.game.slots[s].state = SLOT_PLAYER;
		war.networkSlot[(size_t)s] = true;
	}
	RelayProtocol::Ticket fields;
	fields.gameId = 0xfedcba9876543210ull;
	fields.slot = 1;
	fields.slotMask = 3;
	fields.expires = 1800000000u;
	std::vector<std::uint8_t> key(32, 42);
	const auto bytes = RelayProtocol::signTicket(fields, key);
	const std::string ticket = RelayProtocol::base64UrlEncode(bytes.data(), bytes.size());
	const std::vector<std::uint8_t> sessionKey(16, 7);
	const std::string mac = RelayProtocol::base64UrlEncode(sessionKey.data(), sessionKey.size());
	auto start = [&](const std::string &changes = "") {
		return "{\"v\":2,\"kind\":\"openbfme-phase-start\",\"war\":\"0123456789abcdef\",\"phase\":1,\"phaseKind\":\"battle\","
			"\"game\":\"fedcba9876543210\",\"slot\":1,\"slots\":[0,1],\"seed\":123,\"relay\":\"127.0.0.1:7350\","
			"\"ticket\":\"" + ticket + "\",\"session_key\":\"" + mac + "\",\"netMode\":\"classic\",\"frameMs\":200,"
			"\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + own.digest + "\"" + changes + "}";
	};
	LobbyStart next;
	std::string error;
	REQUIRE_MESSAGE(OnlineStart::parsePhase(start(), war, war.onlineGameId, 1, next, &error), error);
	CHECK(next.onlineGameId == fields.gameId);
	CHECK(next.localSlot == 1);
	CHECK(next.networkSlot[0]);
	CHECK(next.networkSlot[1]);
	CHECK(next.game.game.scenario == war.game.game.scenario);
	CHECK_FALSE(OnlineStart::parsePhase(start(), war, war.onlineGameId, 2, next, &error));
	CHECK_FALSE(OnlineStart::parsePhase(start(",\"setup\":{}"), war, war.onlineGameId, 1, next, &error));
	CHECK_FALSE(OnlineStart::parsePhase(start(), war, 0, 1, next, &error));
	fields.slotMask = 2;
	const auto wrong = RelayProtocol::signTicket(fields, key);
	const std::string wrongTicket = RelayProtocol::base64UrlEncode(wrong.data(), wrong.size());
	std::string wrongStart = start();
	wrongStart.replace(wrongStart.find(ticket), ticket.size(), wrongTicket);
	CHECK_FALSE(OnlineStart::parsePhase(wrongStart, war, war.onlineGameId, 1, next, &error));
}

TEST_CASE("wotr9 online client: strategic setup, region choices, resume check and list fields")
{
	OnlineLobbyClient c(options());
	c.signInGuest(u"Host");
	answer(c, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"Host\",\"kind\":\"guest\"}}");
	SkirmishGameInfo setup;
	setup.mapName = "maps/strategic/strategic.map";
	setup.gameMode = 1;
	setup.scenario = "WarScenario";
	setup.startingCash = -1;
	setup.slots[0].state = SLOT_PLAYER;
	setup.slots[1].state = SLOT_OPEN;
	c.createGame(u"War", "", setup, 0);
	auto requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body.find("\"gameMode\":1,\"scenario\":\"WarScenario\"") != std::string::npos);
	CHECK(requests[0].body.find("\"resumeSave\"") == std::string::npos);
	c.deliver(requests[0].id, 200, view(""), "");
	REQUIRE(c.inGame());
	c.takeEvents();
	CHECK(c.gameInfo().warOfTheRing());
	CHECK(c.gameInfo().slots[1].startPos == 9); // region index, not the map's eighth start
	const std::string save = "0123456789abcdef0123456789abcdef";
	setup.resumeSave = save;
	setup.resumeHash = 123;
	c.setSetup(setup);
	requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body.find("\"resumeSave\":\"" + save + "\",\"resumeHash\":123") != std::string::npos);
	c.deliver(requests[0].id, 200, view(save), "");
	REQUIRE(c.gameInfo().resumed());
	c.takeEvents();
	CHECK(c.gameInfo().resumeHash == 123);
	CHECK_FALSE(c.slotViews()[1].resumeCheck.known);
	c.setResumeCheck("00000000000000000000000000000000", true, "");
	CHECK(c.takeRequests().empty());
	auto events = c.takeEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OnlineLobbyClient::Event::Refused);
	CHECK(events[0].code == "bad_field");
	c.setResumeCheck(save, false, "the copy differs");
	requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body == "{\"resumeCheck\":{\"save\":\"" + save + "\",\"ok\":false,\"why\":\"the copy differs\"}}");
	c.deliver(requests[0].id, 200, view(save, ",\"resumeCheck\":{\"save\":\"" + save + "\",\"ok\":true,\"why\":\"\"}"), "");
	REQUIRE(c.slotViews()[1].resumeCheck.known);
	c.takeEvents();
	CHECK(c.slotViews()[1].resumeCheck.save == save);
	CHECK(c.slotViews()[1].resumeCheck.ok);

	CHECK_FALSE(c.requestPhase(1, { 0, 0 }));
	REQUIRE(c.requestPhase(1, { 0, 1 }));
	CHECK(c.phasePending());
	requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].method == "POST");
	CHECK(requests[0].path == "/v1/games/0123456789abcdef/phase");
	CHECK(requests[0].body == "{\"phase\":1,\"kind\":\"battle\",\"slots\":[0,1]}");
	c.deliver(requests[0].id, 409, "{\"v\":1,\"error\":\"phase_wait\",\"message\":\"waiting for the other seats\"}", "");
	CHECK_FALSE(c.phasePending());
	CHECK(c.phaseWaiting());
	CHECK(c.phaseError().empty());
	CHECK(c.takeEvents().empty()); // another player's vote is not a refusal box
	REQUIRE(c.requestPhase(1, { 0, 1 }));
	CHECK_FALSE(c.phaseWaiting());
	requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body == "{\"phase\":1,\"kind\":\"battle\",\"slots\":[0,1]}");
	c.deliver(requests[0].id, 200, "{\"v\":1,\"war\":\"0123456789abcdef\",\"phase\":1,\"start\":{\"v\":2}}", "");
	CHECK_FALSE(c.phasePending());
	CHECK(c.phaseStartJson() == "{\"v\":2}");
	CHECK(c.phaseError().empty());
	REQUIRE(c.requestPhase(2, {}));
	requests = c.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].body == "{\"phase\":2,\"kind\":\"map\"}");
	c.deliver(requests[0].id, 409, "{\"v\":1,\"error\":\"phase_order\",\"message\":\"only phase 1\"}", "");
	CHECK_FALSE(c.phasePending());
	CHECK(c.phaseStartJson().empty());
	CHECK(c.phaseError() == "only phase 1");
	c.takeEvents();

	c.refreshList();
	events = answer(c, "{\"v\":1,\"games\":[{\"id\":\"0123456789abcdef\",\"name\":\"War\",\"host\":\"Host\","
		"\"map\":\"maps/strategic/strategic.map\",\"players\":2,\"openSlots\":6,\"state\":\"open\",\"password\":false,"
		"\"gameMode\":1,\"scenario\":\"WarScenario\",\"resume\":true}]}");
	REQUIRE(c.games().size() == 1);
	CHECK(c.games()[0].gameMode == 1);
	CHECK(c.games()[0].scenario == "WarScenario");
	CHECK(c.games()[0].resume);
	c.refreshList();
	events = answer(c, "{\"v\":1,\"games\":[{\"id\":\"0123456789abcdef\",\"name\":\"War\",\"host\":\"Host\","
		"\"map\":\"m\",\"players\":2,\"openSlots\":6,\"state\":\"open\",\"password\":false,\"gameMode\":1,\"scenario\":\"WarScenario\"}]}");
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OnlineLobbyClient::Event::Refused);
	CHECK(events[0].code == "protocol");
	CHECK(c.games()[0].resume); // a malformed answer does not replace the last good list
}

TEST_CASE("wotr9 online client: malformed strategic room and save answers are refused without replacing the last good view")
{
	OnlineLobbyClient c(options());
	c.signInGuest(u"Host");
	answer(c, "{\"v\":1,\"token\":\"t.t\",\"account\":{\"id\":\"guest:1\",\"name\":\"Host\",\"kind\":\"guest\"}}");
	c.joinGame("0123456789abcdef", "");
	const std::string save = "0123456789abcdef0123456789abcdef";
	answer(c, view(save));
	REQUIRE(c.inGame());
	for (const std::string &bad : { view(save, ",\"resumeCheck\":{\"save\":\"bad\",\"ok\":true,\"why\":\"\"}"),
			 view(save, ",\"resumeCheck\":{\"save\":\"" + save + "\",\"ok\":1,\"why\":\"\"}"), view(save, "", 64) })
	{
		c.setReady(true);
		const auto events = answer(c, bad);
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == OnlineLobbyClient::Event::Refused);
		CHECK(events[0].code == "protocol");
		CHECK(c.gameInfo().resumeSave == save);
		CHECK(c.gameInfo().slots[1].startPos == 9);
	}
}
