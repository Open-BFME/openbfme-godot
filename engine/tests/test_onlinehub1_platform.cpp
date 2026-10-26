// OpenBFME unit tests. GPL-3.0.
// Lane ONLINEHUB-1 (docs/PLATFORM_API.md): the online platform API's client (GameNetwork/PlatformApiClient.h: Quick Match tickets, tournaments,
// statistics, game results) and the local game records (GameClient/GameRecords.h: "Your stats" and the results report). The answers here have the shapes of
// the backend's own service (tools/platform_fake, source commit 192a0fca; tools/platform_fake/test_platform_contract.py pins them). The hostile-input
// rules of briefs/common.md come first: every answer is bounded, closed sets are refused by name, a refusal leaves the state as it was.

#include "doctest.h"

#include "Common/MiniJson.h"
#include "GameClient/GameRecords.h"
#include "GameNetwork/PlatformApiClient.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

namespace
{
const std::string kInstall = "0123456789abcdef0123456789abcdef";
const std::string kOther = "fedcba9876543210fedcba9876543210";
const std::string kTicket1 = "11111111111111111111111111111111";
const std::string kTicket2 = "22222222222222222222222222222222";
const std::string kMatchId = "33333333333333333333333333333333";
const std::string kTid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kTid2 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

PlatformApiClient::Options baseOptions()
{
	PlatformApiClient::Options o;
	o.install = kInstall;
	o.nick = "Elrond";
	o.clientPlatform = "linux";
	o.clientVersion = "0.4.5-test";
	o.searchTimeoutMs = 60000;
	o.resultRetryMs = 30000;
	return o;
}

PlatformApiClient makeClient()
{
	return PlatformApiClient(baseOptions());
}

// the one request out now (an empty request when none)
PlatformApiClient::HttpRequest take(PlatformApiClient &c)
{
	auto q = c.takeRequests();
	REQUIRE(q.size() <= 1); // tolerance: the client hands out one request or none (none due); either is the property
	return q.empty() ? PlatformApiClient::HttpRequest() : q.front();
}

std::vector<PlatformApiClient::Event> events(PlatformApiClient &c)
{
	return c.takeEvents();
}

std::string created(const std::string &ticket, int pollAfter = 5)
{
	return "{\"v\":1,\"ticket\":\"" + ticket + "\",\"poll_after_s\":" + std::to_string(pollAfter) + "}";
}

std::string refusal(const std::string &code, const std::string &field = std::string())
{
	return "{\"v\":1,\"error\":{\"code\":\"" + code + "\"" + (field.empty() ? "" : ",\"field\":\"" + field + "\"") + "}}";
}

std::string matched(const std::string &mode, const std::string &players, const std::string &room = "0123456789ABCDEF", const std::string &host = kInstall)
{
	return "{\"v\":1,\"state\":\"matched\",\"match\":{\"id\":\"" + kMatchId + "\",\"room_code\":\"" + room + "\",\"host_install\":\"" + host + "\",\"mode\":\"" + mode
		+ "\",\"players\":" + players + "}}";
}

const char *kTwoPlayers = "[{\"nick\":\"Elrond\",\"team\":0},{\"nick\":\"Gothmog\",\"team\":1}]";

// starts a 1v1 search and brings it to Searching (ticket kTicket1, polled every 5 s: what the service names)
void searching(PlatformApiClient &c, std::uint64_t now = 1000)
{
	c.update(now);
	std::string err;
	REQUIRE(c.qmSearch("faithful", "1v1", { "FactionMen" }, &err));
	auto q = take(c);
	REQUIRE(q.method == "POST");
	REQUIRE(q.path == "/qm/tickets");
	c.deliver(q.id, 201, created(kTicket1), "");
	REQUIRE(c.qmState() == PlatformApiClient::QmState::Searching);
	events(c);
}

std::string tournamentJson(const std::string &id, const std::string &name = "Spring Cup", const std::string &mode = "faithful", const std::string &size = "1v1",
	const std::string &state = "upcoming", int maxPlayers = 8, int registered = 3)
{
	return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"mode\":\"" + mode + "\",\"size\":\"" + size + "\",\"start\":1790000000,\"max_players\":" + std::to_string(maxPlayers)
		+ ",\"state\":\"" + state + "\",\"registered\":" + std::to_string(registered) + "}";
}

std::string listJson(const std::vector<std::string> &entries, const std::string &cursor = std::string())
{
	std::string out = "{\"v\":1,\"tournaments\":[";
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		out += (i ? "," : "") + entries[i];
	}
	return out + "],\"next_cursor\":" + (cursor.empty() ? std::string("null") : "\"" + cursor + "\"") + "}";
}

std::string mid(int n) // a match id of tournament kTid
{
	char tail[32];
	std::snprintf(tail, sizeof(tail), "%024x", n);
	return kTid + "." + tail;
}

std::string matchJson(int n, int round, const std::string &state, const std::string &p0, const std::string &p1, const std::string &winner = std::string(),
	const std::string &room = std::string())
{
	auto nick = [](const std::string &s) { return s.empty() ? std::string("null") : "\"" + s + "\""; };
	return "{\"id\":\"" + mid(n) + "\",\"round\":" + std::to_string(round) + ",\"state\":\"" + state + "\",\"players\":[" + nick(p0) + "," + nick(p1) + "],\"winner\":" + nick(winner)
		+ (room.empty() ? "" : ",\"room_code\":\"" + room + "\"") + "}";
}

// a started tournament: two rounds; round 0 has two matches, round 1 the final
std::string detailJson(const std::string &tournament, const std::vector<std::string> &matches, const std::string &rounds, const std::string &extra = std::string())
{
	std::string out = tournament.substr(0, tournament.size() - 1) + ",\"rules\":\"Best of one.\\nNo rushing.\",\"rounds\":" + rounds + ",\"matches\":[";
	for (std::size_t i = 0; i < matches.size(); ++i)
	{
		out += (i ? "," : "") + matches[i];
	}
	return "{\"v\":1," + out.substr(1) + "]" + extra + "}";
}

const std::string kRounds = "[[\"" + mid(1) + "\",\"" + mid(2) + "\"],[\"" + mid(3) + "\"]]";

std::vector<std::string> bracket()
{
	return { matchJson(1, 0, "finished", "Elrond", "Gil", "Elrond"), matchJson(2, 0, "open", "Ugluk", "Thror", "", "ABCDEF0123456789"),
		matchJson(3, 1, "waiting", "", "") };
}

GameRecord sampleRecord(const std::string &id, const std::string &kind, const std::string &faction, int winner, std::int64_t end,
	const std::string &map = "maps/map mp fords of isen")
{
	GameRecord r;
	r.id = id;
	r.kind = kind;
	r.mode = "faithful";
	r.map = map;
	r.start = end - 600;
	r.end = end;
	r.durationS = 590;
	r.players = { { 0, "Elrond", faction, 0, true }, { 1, kind == "skirmish" ? "Brutal AI" : "Gothmog", "FactionMordor", 1, kind != "skirmish" } };
	r.meSlot = 0;
	r.winnerTeam = winner;
	return r;
}

// delivers `body` (status 200) to the request out now and returns the events
std::vector<PlatformApiClient::Event> answer(PlatformApiClient &c, const std::string &body, int status = 200)
{
	auto q = take(c);
	REQUIRE(q.id != 0); // tolerance: a request id is any non-zero number; 0 is "no request"
	c.deliver(q.id, status, body, "");
	return events(c);
}
} // namespace

TEST_CASE("onlinehub1 review: every write carries the client metadata; a client without it refuses by name before anything is sent")
{
	PlatformApiClient c = makeClient();
	std::string err;
	REQUIRE(c.qmSearch("faithful", "1v1", { "FactionMen", "FactionElves" }, &err));
	auto q = take(c);
	CHECK(q.body == "{\"v\":1,\"install\":\"" + kInstall + "\",\"nick\":\"Elrond\",\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"},\"mode\":\"faithful\","
					"\"size\":\"1v1\",\"factions\":[\"FactionMen\",\"FactionElves\"]}");
	c.deliver(q.id, 201, created(kTicket1), "");
	events(c);
	c.registerTournament(kTid);
	q = take(c);
	CHECK(q.path == "/tournaments/" + kTid + "/register");
	CHECK(q.body == "{\"v\":1,\"install\":\"" + kInstall + "\",\"nick\":\"Elrond\",\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"}}");
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":true}", "");
	events(c);
	c.withdrawTournament(kTid);
	q = take(c);
	CHECK(q.method == "DELETE");
	CHECK(q.body == "{\"v\":1,\"install\":\"" + kInstall + "\",\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"}}");
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":false}", "");
	events(c);
	c.checkIn(kTid, mid(2));
	q = take(c);
	CHECK(q.method == "POST");
	CHECK(q.path == "/tournaments/" + kTid + "/matches/" + mid(2) + "/checkin");
	CHECK(q.body == "{\"v\":1,\"install\":\"" + kInstall + "\",\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"}}");

	// the version is kept exactly, up to 64 characters
	PlatformApiClient::Options odd = baseOptions();
	odd.clientVersion = std::string("v1.0-rc+Case.Preserved");
	PlatformApiClient d(odd);
	REQUIRE(d.qmSearch("faithful", "1v1", { "random" }, &err));
	CHECK(take(d).body.find("\"version\":\"v1.0-rc+Case.Preserved\"") != std::string::npos);

	// without usable metadata nothing is sent: not a platform the service knows, an empty version, a 65 character one
	for (const auto &bad : std::vector<std::pair<std::string, std::string>>{ { "", "1" }, { "mac", "1" }, { "Linux", "1" }, { "linux", "" }, { "linux", std::string(65, 'v') } })
	{
		PlatformApiClient::Options o = baseOptions();
		o.clientPlatform = bad.first;
		o.clientVersion = bad.second;
		PlatformApiClient n(o);
		CHECK_FALSE(n.qmSearch("faithful", "1v1", { "random" }, &err));
		CHECK(err.rfind("this build has no usable client metadata: ", 0) == 0);
		n.registerTournament(kTid);
		n.withdrawTournament(kTid);
		n.checkIn(kTid, mid(2));
		const auto ev = events(n);
		REQUIRE(ev.size() == 3);
		for (const auto &e : ev)
		{
			CHECK(e.kind == PlatformApiClient::Event::Refused);
			CHECK(e.code == "bad_field");
		}
		CHECK(take(n).id == 0);
		CHECK(n.qmState() == PlatformApiClient::QmState::Idle);
	}
	CHECK(PlatformApiClient::validClient("windows", "x"));
	CHECK(PlatformApiClient::validClient("macos", std::string(64, 'x')));
	CHECK_FALSE(PlatformApiClient::validClient("macos", std::string("a\nb")));
}

TEST_CASE("onlinehub1 review: Quick Match: the ticket is 201, polls follow the interval the service named and keep it when a reply names none, a malformed one ends the "
		  "search")
{
	PlatformApiClient c = makeClient();
	c.update(1000);
	std::string err;
	REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
	auto q = take(c);
	// a ticket the service answers with 200 (not 201) is a protocol refusal, and so is one without a valid interval
	c.deliver(q.id, 200, created(kTicket1), "");
	CHECK(c.qmState() == PlatformApiClient::QmState::Failed);
	CHECK(c.qmProblem() == "Quick Match is not available: HTTP 200 is not the answer of a qm-create request");
	for (const std::string &bad : { std::string("{\"v\":1,\"ticket\":\"" + kTicket1 + "\"}"), std::string("{\"v\":1,\"ticket\":\"" + kTicket1 + "\",\"poll_after_s\":0}"),
			 std::string("{\"v\":1,\"ticket\":\"" + kTicket1 + "\",\"poll_after_s\":61}"), std::string("{\"v\":1,\"ticket\":\"" + kTicket1 + "\",\"poll_after_s\":2.5}"),
			 std::string("{\"v\":1,\"ticket\":\"" + kTicket1 + "\",\"poll_after_s\":\"5\"}"), std::string("{\"v\":1,\"ticket\":\"nothex\",\"poll_after_s\":5}") })
	{
		CAPTURE(bad);
		PlatformApiClient d = makeClient();
		REQUIRE(d.qmSearch("faithful", "1v1", { "random" }, &err));
		q = take(d);
		d.deliver(q.id, 201, bad, "");
		CHECK(d.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(d.qmProblem().rfind("Quick Match is not available", 0) == std::string::npos); // the ticket's own refusal, by name
		CHECK(d.qmProblem().rfind("the service's ticket was refused: ", 0) == 0);
		CHECK(d.qmTicket().empty());
	}

	PlatformApiClient p = makeClient();
	p.update(1000);
	REQUIRE(p.qmSearch("faithful", "1v1", { "random" }, &err));
	q = take(p);
	p.deliver(q.id, 201, created(kTicket1, 5), "");
	CHECK(p.qmPollAfterS() == 5);
	p.update(5999);
	CHECK(take(p).id == 0);
	p.update(6000);
	q = take(p);
	CHECK(q.path == "/qm/tickets/" + kTicket1);
	// the service's polls name no interval (platform v1): the 5 seconds hold
	p.deliver(q.id, 200, "{\"v\":1,\"state\":\"searching\"}", "");
	CHECK(p.qmPollAfterS() == 5);
	p.update(10999);
	CHECK(take(p).id == 0);
	p.update(11000);
	q = take(p);
	CHECK(q.path == "/qm/tickets/" + kTicket1);
	// a poll that names one changes it
	p.deliver(q.id, 200, "{\"v\":1,\"state\":\"searching\",\"poll_after_s\":8}", "");
	CHECK(p.qmPollAfterS() == 8);
	p.update(18999);
	CHECK(take(p).id == 0);
	p.update(19000);
	q = take(p);
	CHECK(q.path == "/qm/tickets/" + kTicket1);
	// a malformed one is refused, never replaced by a guess
	for (const std::string &bad : { std::string("0"), std::string("61"), std::string("1.5"), std::string("\"5\""), std::string("null"), std::string("true"), std::string("-3") })
	{
		CAPTURE(bad);
		PlatformApiClient d = makeClient();
		searching(d, 1000);
		d.update(6000);
		const auto poll = take(d);
		d.deliver(poll.id, 200, "{\"v\":1,\"state\":\"searching\",\"poll_after_s\":" + bad + "}", "");
		CHECK(d.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(d.qmProblem() == "the service's answer was refused: poll_after_s is missing or not a whole number in 1 .. 60");
	}
}

TEST_CASE("onlinehub1 review: Quick Match: the service's error envelope is read as {code, field}, anything else is a protocol refusal, statuses are exact")
{
	PlatformApiClient c = makeClient();
	std::string err;
	const std::vector<std::tuple<int, std::string, std::string>> cases = {
		{ 429, refusal("rate_limited"), "Quick Match is not available: Too many requests: wait a moment and try again." },
		{ 429, refusal("queue_full"), "Quick Match is not available: The Quick Match queue is full: try again in a moment." },
		{ 409, refusal("active_ticket_conflict"), "Quick Match is not available: You are already searching with other choices: cancel that search first." },
		{ 400, refusal("missing_field", "client"), "Quick Match is not available: The online services refused the request: client is missing." },
		{ 400, refusal("invalid_field", "client.platform"), "Quick Match is not available: The online services refused the request: client.platform is not valid." },
		{ 503, refusal("unavailable"), "Quick Match is not available: The online services are not available right now." },
		{ 418, refusal("teapot"), "Quick Match is not available: The online services refused the request (teapot)." },
		// not the envelope: the old string shape, a missing code, a bad code, a 200 carrying an error
		{ 400, "{\"v\":1,\"error\":\"bad_field\",\"message\":\"x\"}", "Quick Match is not available: HTTP 400 with an error that is not {code, field}" },
		{ 400, "{\"v\":1,\"error\":{}}", "Quick Match is not available: HTTP 400 with an error that is not {code, field}" },
		{ 400, "{\"v\":1,\"error\":{\"code\":\"has space\"}}", "Quick Match is not available: HTTP 400 with an error that is not {code, field}" },
		{ 400, "{\"v\":1,\"error\":{\"code\":\"x\",\"field\":5}}", "Quick Match is not available: HTTP 400 with an error that is not {code, field}" },
		{ 200, refusal("rate_limited"), "Quick Match is not available: HTTP 200 with an error that is not {code, field}" },
		{ 400, "{\"error\":{\"code\":\"x\"}}", "Quick Match is not available: HTTP 400 with something that is not platform API version 1" },
		{ 400, "<html>", "Quick Match is not available: HTTP 400 with something that is not platform API version 1" },
		{ 202, created(kTicket1), "Quick Match is not available: HTTP 202 is not the answer of a qm-create request" },
	};
	for (const auto &k : cases)
	{
		CAPTURE(std::get<1>(k));
		PlatformApiClient d = makeClient();
		REQUIRE(d.qmSearch("faithful", "1v1", { "random" }, &err));
		const auto q = take(d);
		d.deliver(q.id, std::get<0>(k), std::get<1>(k), "");
		CHECK(d.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(d.qmProblem() == std::get<2>(k));
	}
}

TEST_CASE("onlinehub1 review: Quick Match: a search cancelled and started again never adopts the cancelled search's late ticket")
{
	std::string err;
	SUBCASE("the first ticket is on its way when the player cancels and searches again")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		const auto first = take(c); // on the wire
		c.qmCancel();
		CHECK(c.qmState() == PlatformApiClient::QmState::Cancelled);
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		CHECK(c.qmState() == PlatformApiClient::QmState::Starting);
		// the first ticket comes back: it is not the new search's
		c.deliver(first.id, 201, created(kTicket1), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Starting);
		CHECK(c.qmTicket().empty());
		// it is deleted BEFORE the new search's request (the service hands an identical search the same ticket: deleting afterwards would delete the new
		// search's too)
		auto q = take(c);
		CHECK(q.method == "DELETE");
		CHECK(q.path == "/qm/tickets/" + kTicket1);
		c.deliver(q.id, 200, "{\"v\":1,\"state\":\"cancelled\"}", "");
		q = take(c);
		CHECK(q.method == "POST");
		CHECK(q.path == "/qm/tickets");
		c.deliver(q.id, 201, created(kTicket2), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Searching);
		CHECK(c.qmTicket() == kTicket2);
		CHECK(c.sentCount("qm-create") == 2);
		CHECK(c.sentCount("qm-cancel") == 1);
	}
	SUBCASE("a ticket not asked for yet is never asked for")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		c.qmCancel(); // nothing was sent
		REQUIRE(c.qmSearch("uber", "2v2", { "random" }, &err));
		auto q = take(c);
		CHECK(q.body.find("\"mode\":\"uber\"") != std::string::npos);
		c.deliver(q.id, 201, created(kTicket2), "");
		CHECK(c.qmTicket() == kTicket2);
		CHECK(c.sentCount("qm-create") == 1);
		CHECK(take(c).id == 0);
	}
	SUBCASE("the player left the page (reset): the late ticket is deleted and the state stays idle")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		const auto first = take(c);
		c.qmReset();
		c.deliver(first.id, 201, created(kTicket1), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Idle);
		const auto q = take(c);
		CHECK(q.method == "DELETE");
		CHECK(q.path == "/qm/tickets/" + kTicket1);
	}
	SUBCASE("a refusal of the cancelled search's ticket is nobody's: the new search goes on")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		const auto first = take(c);
		c.qmCancel();
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		c.deliver(first.id, 429, refusal("rate_limited"), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Starting);
		auto q = take(c);
		CHECK(q.method == "POST");
		c.deliver(q.id, 201, created(kTicket2), "");
		CHECK(c.qmTicket() == kTicket2);
	}
	SUBCASE("a late poll answer of the cancelled search is not taken by the next one")
	{
		PlatformApiClient c = makeClient();
		searching(c);
		c.update(6000);
		const auto poll = take(c);
		c.qmCancel();
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		c.deliver(poll.id, 200, matched("faithful", kTwoPlayers), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Starting);
		CHECK(c.match().id.empty());
	}
}

TEST_CASE("onlinehub1: Quick Match: the match is checked against the search; a match that does not fit is refused by name")
{
	PlatformApiClient c = makeClient();
	searching(c);
	c.update(6000);
	auto q = take(c);
	CHECK(q.method == "GET");
	CHECK(q.path == "/qm/tickets/" + kTicket1);
	// the host is the first player listed (the oldest ticket): another install hosts here, this player is the second
	c.deliver(q.id, 200, matched("faithful", "[{\"nick\":\"Gothmog\",\"team\":1},{\"nick\":\"Elrond\",\"team\":0}]", "0123456789ABCDEF", kOther), "");
	REQUIRE(c.qmState() == PlatformApiClient::QmState::Matched);
	CHECK(c.match().id == kMatchId);
	CHECK(c.match().roomCode == "0123456789ABCDEF");
	CHECK_FALSE(c.match().host);
	CHECK_FALSE(c.match().players[0].you);
	CHECK(c.match().players[1].you);
	CHECK(c.match().players[1].team == 0);
	// matched stops the polls
	c.update(600000);
	CHECK(take(c).id == 0);
	// this install is the host: the first player listed is it, whatever the others' names
	PlatformApiClient h = makeClient();
	searching(h);
	h.update(6000);
	q = take(h);
	h.deliver(q.id, 200, matched("faithful", "[{\"nick\":\"Elrond\",\"team\":0},{\"nick\":\"Elrond\",\"team\":1}]"), "");
	REQUIRE(h.qmState() == PlatformApiClient::QmState::Matched);
	CHECK(h.match().host);
	CHECK(h.match().players[0].you);
	CHECK_FALSE(h.match().players[1].you);

	struct Case
	{
		std::string body, why;
	};
	const std::string refused = "the service's answer was refused: ";
	const std::vector<Case> cases = {
		{ matched("uber", kTwoPlayers), "the match's mode \"uber\" is not the search's \"faithful\"" },
		{ matched("faithful", "[{\"nick\":\"Elrond\",\"team\":0},{\"nick\":\"A\",\"team\":1},{\"nick\":\"B\",\"team\":1}]"), "the match has 3 players, a 1v1 match has 2" },
		{ matched("faithful", "[{\"nick\":\"Gil\",\"team\":0},{\"nick\":\"Gothmog\",\"team\":1}]", "0123456789ABCDEF", kOther), "the match does not name this player" },
		{ matched("faithful", kTwoPlayers, "abc"), refused + "match.room_code is not a room code ([A-Za-z0-9_-], 6 .. 32)" },
		{ matched("faithful", kTwoPlayers, "ROOM abc123"), refused + "match.room_code is not a room code ([A-Za-z0-9_-], 6 .. 32)" },
		{ matched("faithful", kTwoPlayers, "0123456789ABCDEF", "nothex"), refused + "match.host_install is not an install id (32 lowercase hex digits)" },
		{ matched("faithful", kTwoPlayers, "0123456789ABCDEF", std::string(32, 'A')), refused + "match.host_install is not an install id (32 lowercase hex digits)" },
		{ matched("ladder", kTwoPlayers), refused + "match.mode \"ladder\" is not faithful / uber" },
		{ matched("faithful", "[{\"nick\":\"Elrond\",\"team\":8},{\"nick\":\"Gothmog\",\"team\":1}]"), refused + "match.players[0].team is missing or not a whole number in 0 .. 7" },
		{ matched("faithful", "[{\"nick\":\"Elrond\"},{\"nick\":\"Gothmog\",\"team\":1}]"), refused + "match.players[0].team is missing or not a whole number in 0 .. 7" },
		{ "{\"v\":1,\"state\":\"won\"}", refused + "state \"won\" is not searching / matched / expired / cancelled" },
		{ "{\"v\":1,\"state\":\"matched\"}", refused + "match is missing for a matched ticket" },
		{ "{\"v\":1,\"state\":\"matched\",\"match\":{\"id\":\"short\",\"room_code\":\"0123456789ABCDEF\",\"host_install\":\"" + kInstall + "\",\"mode\":\"faithful\",\"players\":[]}}",
			refused + "match.id is not a match id (32 lowercase hex digits)" },
	};
	for (const Case &k : cases)
	{
		CAPTURE(k.body);
		PlatformApiClient d = makeClient();
		searching(d);
		d.update(6000);
		q = take(d);
		d.deliver(q.id, 200, k.body, "");
		CHECK(d.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(d.qmProblem() == k.why);
		CHECK(d.match().id.empty()); // nothing of the refused match is kept
	}
	// more than 4 players: refused before any is kept
	std::string five = "[";
	for (int i = 0; i < 5; ++i)
	{
		five += std::string(i ? "," : "") + "{\"nick\":\"P" + std::to_string(i) + "\",\"team\":" + std::to_string(i % 2) + "}";
	}
	five += "]";
	PlatformApiClient big = makeClient();
	searching(big);
	big.update(6000);
	q = take(big);
	big.deliver(q.id, 200, matched("faithful", five), "");
	CHECK(big.qmProblem() == refused + "match.players has 5 entries (at most 4)");
	// a 2v2 with this player named twice among the non-hosts
	PlatformApiClient dup = makeClient();
	std::string err;
	dup.update(1000);
	REQUIRE(dup.qmSearch("faithful", "2v2", { "random" }, &err));
	q = take(dup);
	dup.deliver(q.id, 201, created(kTicket1), "");
	dup.update(6000);
	q = take(dup);
	dup.deliver(q.id, 200, matched("faithful", "[{\"nick\":\"Gil\",\"team\":0},{\"nick\":\"Elrond\",\"team\":1},{\"nick\":\"Elrond\",\"team\":1},{\"nick\":\"Thror\",\"team\":0}]",
				"0123456789ABCDEF", kOther), "");
	CHECK(dup.qmState() == PlatformApiClient::QmState::Failed);
	CHECK(dup.qmProblem() == "the match names more than one player \"Elrond\": choose a nickname no one else has");
}

TEST_CASE("onlinehub1: Quick Match: cancel, expiry, the search timeout and an unreachable service end the search with a reason, never stuck")
{
	SUBCASE("cancel while searching: DELETE, and a poll answer that comes later is not taken")
	{
		PlatformApiClient c = makeClient();
		searching(c);
		c.update(6000);
		auto poll = take(c);
		c.qmCancel();
		CHECK(c.qmState() == PlatformApiClient::QmState::Cancelled);
		c.deliver(poll.id, 200, matched("faithful", kTwoPlayers), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Cancelled);
		CHECK(c.match().id.empty());
		auto del = take(c);
		CHECK(del.method == "DELETE");
		CHECK(del.path == "/qm/tickets/" + kTicket1);
	}
	SUBCASE("the service expires or cancels the ticket; an unknown ticket (404) is an expired search")
	{
		PlatformApiClient c = makeClient();
		searching(c);
		c.update(6000);
		auto q = take(c);
		c.deliver(q.id, 200, "{\"v\":1,\"state\":\"expired\"}", "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Expired);
		CHECK(c.qmProblem() == "the search expired");
		PlatformApiClient d = makeClient();
		searching(d);
		d.update(6000);
		q = take(d);
		d.deliver(q.id, 200, "{\"v\":1,\"state\":\"cancelled\"}", "");
		CHECK(d.qmState() == PlatformApiClient::QmState::Cancelled);
		CHECK(d.qmProblem() == "the search was cancelled by the online service");
		PlatformApiClient e = makeClient();
		searching(e);
		e.update(6000);
		q = take(e);
		e.deliver(q.id, 404, refusal("ticket_not_found"), "");
		CHECK(e.qmState() == PlatformApiClient::QmState::Expired);
	}
	SUBCASE("no match within the search time: TimedOut and the ticket is deleted")
	{
		PlatformApiClient c = makeClient();
		searching(c, 1000);
		c.update(60999);
		auto poll = take(c);
		CHECK(poll.path == "/qm/tickets/" + kTicket1);
		c.deliver(poll.id, 200, "{\"v\":1,\"state\":\"searching\"}", "");
		c.update(61000);
		CHECK(c.qmState() == PlatformApiClient::QmState::TimedOut);
		CHECK(c.qmProblem() == "no opponent was found");
		auto del = take(c);
		CHECK(del.method == "DELETE");
	}
	SUBCASE("the service cannot be reached: the ticket's request, then three polls in a row (a 429 counts)")
	{
		PlatformApiClient c = makeClient();
		std::string err;
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		auto q = take(c);
		c.deliver(q.id, 0, "", "cannot reach https://api.example: timeout");
		CHECK(c.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(c.qmProblem() == "Quick Match is not available: cannot reach https://api.example: timeout");

		PlatformApiClient d = makeClient();
		searching(d, 1000);
		std::uint64_t now = 6000;
		const std::vector<std::pair<int, std::string>> failures = { { 503, "<html>busy</html>" }, { 429, refusal("rate_limited") }, { 0, "" } };
		for (std::size_t i = 0; i < 3; ++i)
		{
			d.update(now);
			q = take(d);
			REQUIRE(q.path == "/qm/tickets/" + kTicket1);
			CHECK(d.qmState() == PlatformApiClient::QmState::Searching);
			d.deliver(q.id, failures[i].first, failures[i].second, failures[i].first == 0 ? "timeout" : "");
			now += 5000;
		}
		CHECK(d.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(d.qmProblem() == "the online services stopped answering: timeout");
		// a good answer in between resets the count
		PlatformApiClient e = makeClient();
		searching(e, 1000);
		now = 6000;
		for (int i = 0; i < 6; ++i)
		{
			e.update(now);
			q = take(e);
			e.deliver(q.id, i % 3 == 2 ? 200 : 503, i % 3 == 2 ? "{\"v\":1,\"state\":\"searching\"}" : "x", "");
			now += 5000;
		}
		CHECK(e.qmState() == PlatformApiClient::QmState::Searching);
	}
	SUBCASE("a refused ticket (rate limit, bad field) is told by its message")
	{
		PlatformApiClient c = makeClient();
		std::string err;
		REQUIRE(c.qmSearch("faithful", "1v1", { "random" }, &err));
		auto q = take(c);
		c.deliver(q.id, 429, refusal("rate_limited"), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(c.qmProblem() == "Quick Match is not available: Too many requests: wait a moment and try again.");
	}
}

TEST_CASE("onlinehub1: the client refuses requests it cannot make, before anything is sent")
{
	PlatformApiClient c = makeClient();
	std::string err;
	CHECK_FALSE(c.qmSearch("ranked", "1v1", { "random" }, &err));
	CHECK(err == "the mode \"ranked\" is not faithful / uber");
	CHECK_FALSE(c.qmSearch("faithful", "3v3", { "random" }, &err));
	CHECK(err == "the size \"3v3\" is not 1v1 / 2v2");
	CHECK_FALSE(c.qmSearch("faithful", "1v1", {}, &err));
	CHECK(err == "the factions list must have 1 .. 8 entries");
	CHECK_FALSE(c.qmSearch("faithful", "1v1", std::vector<std::string>(9, "FactionMen"), &err));
	CHECK(err == "the factions list must have 1 .. 8 entries");
	CHECK_FALSE(c.qmSearch("faithful", "1v1", { "Faction\"Men" }, &err));
	CHECK(err == "the faction \"Faction\"Men\" is not a faction name");
	CHECK_FALSE(c.qmSearch("faithful", "1v1", { "random", "FactionMen" }, &err));
	CHECK(err == "\"random\" is the whole list or nothing");
	CHECK_FALSE(c.fetchGlobalStats("faithful", "1y", &err));
	CHECK(err == "the mode must be every / faithful / uber and the period 7d / 30d / all");
	PlatformApiClient::Options o = baseOptions();
	o.install.clear();
	PlatformApiClient noId(o);
	CHECK_FALSE(noId.qmSearch("faithful", "1v1", { "random" }, &err));
	CHECK(err == "this installation has no install id");
	CHECK(take(c).id == 0);
	CHECK(take(noId).id == 0);
	CHECK(c.qmState() == PlatformApiClient::QmState::Idle);
	CHECK(PlatformApiClient::installTag(kInstall) == "012345...");
	CHECK(PlatformApiClient::installTag("x") == "(no install id)");
}

TEST_CASE("onlinehub1: tournaments: the list (cancelled included, 1v1 only, pages read to the end), hostile answers are refused and the old list kept")
{
	PlatformApiClient c = makeClient();
	c.listTournaments();
	auto q = take(c);
	CHECK(q.path == "/tournaments");
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid), tournamentJson(kTid2, "Weekly 12", "uber", "1v1", "running", 4, 4), tournamentJson(std::string(32, 'c'), "Off", "faithful", "1v1", "cancelled", 2, 0) }), "");
	REQUIRE(c.tournaments().size() == 3);
	CHECK(c.tournaments()[0].name == "Spring Cup");
	CHECK(c.tournaments()[1].state == "running");
	CHECK(c.tournaments()[2].state == "cancelled");
	CHECK(c.tournamentsKnown());
	CHECK_FALSE(c.tournamentsTruncated());
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::Tournaments);

	struct Bad
	{
		std::string body, why;
	};
	const std::string nine = std::string(9, 'x');
	const std::vector<Bad> bad = {
		{ listJson({ tournamentJson(kTid, "A", "faithful", "1v1", "upcoming", 8, 9) }), "tournaments[0].registered 9 is above max_players 8" },
		{ listJson({ tournamentJson(kTid, "A", "faithful", "1v1", "open") }), "tournaments[0].state \"open\" is not upcoming / running / finished / cancelled" },
		{ listJson({ tournamentJson(kTid, "A", "faithful", "2v2") }), "tournaments[0].size \"2v2\" is not 1v1 (platform v1 has no other tournament size)" },
		{ listJson({ tournamentJson(kTid, "A", "faithful", "") }), "tournaments[0].size \"\" is not 1v1 (platform v1 has no other tournament size)" },
		{ listJson({ tournamentJson(kTid, "A", "ranked") }), "tournaments[0].mode \"ranked\" is not faithful / uber" },
		{ listJson({ tournamentJson("spring") }), "tournaments[0].id \"spring\" is not a tournament id (32 lowercase hex digits)" },
		{ listJson({ tournamentJson(std::string(32, 'A')) }), "tournaments[0].id \"AAAAAAAAAAAAAAAAAAAAAAAA...\" is not a tournament id (32 lowercase hex digits)" },
		{ listJson({ tournamentJson(kTid, "A"), tournamentJson(kTid, "B") }), "tournaments[1].id " + kTid + " appears twice" },
		{ listJson({ tournamentJson(kTid, "A\\u0007") }), "tournaments[0].name is empty, longer than 512 bytes or has control characters" },
		{ listJson({ tournamentJson(kTid, std::string(513, 'n')) }), "tournaments[0].name is empty, longer than 512 bytes or has control characters" },
		{ listJson({ tournamentJson(kTid, "A", "faithful", "1v1", "upcoming", 1, 0) }), "tournaments[0].max_players is missing or not a whole number in 2 .. 128" },
		{ listJson({ tournamentJson(kTid, "A", "faithful", "1v1", "upcoming", 129, 0) }), "tournaments[0].max_players is missing or not a whole number in 2 .. 128" },
		{ "{\"v\":1,\"tournaments\":[],\"next_cursor\":5}", "next_cursor is not a cursor (1 .. 256 characters)" },
		{ "{\"v\":1,\"tournaments\":[],\"next_cursor\":\"" + std::string(257, 'c') + "\"}", "next_cursor is not a cursor (1 .. 256 characters)" },
		{ "{\"v\":1,\"tournaments\":{}}", "tournaments is missing or not a list" },
		{ "{\"v\":2,\"tournaments\":[]}", "HTTP 200 with something that is not platform API version 1" },
	};
	for (const Bad &b : bad)
	{
		CAPTURE(b.body);
		c.listTournaments();
		q = take(c);
		c.deliver(q.id, 200, b.body, "");
		ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].kind == PlatformApiClient::Event::Refused);
		CHECK(ev[0].request == "tournaments");
		CHECK(ev[0].text == b.why);
		CHECK(c.tournaments().size() == 3);
		CHECK(c.tournaments()[0].id == kTid);
	}
	// the container caps: 101 on a page, a body over the bound
	std::vector<std::string> many;
	for (int i = 0; i < 101; ++i)
	{
		char id[40];
		std::snprintf(id, sizeof(id), "%032x", i + 1);
		many.push_back(tournamentJson(id));
	}
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, listJson(many), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].text == "tournaments has 101 entries (at most 100)");
	many.pop_back();
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, listJson(many), "");
	CHECK(c.tournaments().size() == 100); // exactly the cap is fine
	events(c);
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, std::string(PlatformApiClient::kMaxBodyBytes + 1, ' '), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].code == "protocol");
	CHECK(ev[0].text == "the answer is larger than 131072 bytes");
	CHECK(c.tournaments().size() == 100);
}

TEST_CASE("onlinehub1: tournaments: the pages of the list are read to the end and committed whole; a bad page keeps the old list; the page cap is stated")
{
	PlatformApiClient c = makeClient();
	c.listTournaments();
	auto q = take(c);
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid) }), "");
	REQUIRE(c.tournaments().size() == 1);
	events(c);
	// two pages: the second is asked with the first's cursor (escaped), the list is the two together and one event says so
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid2, "Second") }, "key one/2"), "");
	CHECK(events(c).empty());
	CHECK(c.tournaments().size() == 1); // not yet
	q = take(c);
	CHECK(q.path == "/tournaments?cursor=key%20one%2F2");
	c.deliver(q.id, 200, listJson({ tournamentJson(std::string(32, 'c'), "Third") }), "");
	REQUIRE(c.tournaments().size() == 2);
	CHECK(c.tournaments()[0].name == "Second");
	CHECK(c.tournaments()[1].name == "Third");
	CHECK(events(c).size() == 1);
	// a tournament on two pages: refused, the list kept
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid) }, "k"), "");
	q = take(c);
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid) }), "");
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].text == "tournament " + kTid + " appears on two pages of the list");
	CHECK(c.tournaments().size() == 2);
	// the second page fails: the list kept
	c.listTournaments();
	q = take(c);
	c.deliver(q.id, 200, listJson({ tournamentJson(kTid) }, "k"), "");
	q = take(c);
	c.deliver(q.id, 503, refusal("unavailable"), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].code == "unavailable");
	CHECK(c.tournaments().size() == 2);
	// a service that never stops sending cursors: five pages, then the list is what it is and says it is cut
	c.listTournaments();
	for (int page = 0; page < 5; ++page)
	{
		q = take(c);
		REQUIRE(q.id != 0); // tolerance: a request id is any non-zero number; 0 is "no request"
		char id[40];
		std::snprintf(id, sizeof(id), "%032x", page + 1);
		c.deliver(q.id, 200, listJson({ tournamentJson(id) }, "more"), "");
	}
	CHECK(take(c).id == 0);
	CHECK(c.tournaments().size() == 5);
	CHECK(c.tournamentsTruncated());
}

TEST_CASE("onlinehub1: tournaments: a tournament's detail has the service's flat shape; the next match is the first this install can play; hostile answers are refused")
{
	PlatformApiClient c = makeClient();
	c.fetchTournament(kTid);
	auto q = take(c);
	CHECK(q.path == "/tournaments/" + kTid + "?install=" + kInstall);
	const std::string tournament = tournamentJson(kTid, "Weekly 12", "uber", "1v1", "running", 4, 4);
	c.deliver(q.id, 200, detailJson(tournament, bracket(), kRounds), "");
	REQUIRE(c.tournamentKnown());
	const auto &t = c.tournament();
	CHECK(t.info.name == "Weekly 12");
	CHECK(t.info.mode == "uber");
	CHECK(t.rules == "Best of one.\nNo rushing.");
	REQUIRE(t.rounds.size() == 2);
	CHECK(t.rounds[0] == std::vector<std::string>{ mid(1), mid(2) });
	REQUIRE(t.matches.size() == 3);
	CHECK(t.matches[0].winner == "Elrond");
	CHECK(t.matches[2].players[0].empty());
	CHECK(t.matches[2].round == 1);
	const auto *next = t.nextMatch();
	REQUIRE(next != nullptr);
	CHECK(next->id == mid(2)); // the finished match has no room code for us and is done; the open one has
	CHECK(next->roomCode == "ABCDEF0123456789");
	CHECK(next->players[0] == "Ugluk");
	events(c);
	// before the start: no rounds, no matches
	c.fetchTournament(kTid);
	q = take(c);
	c.deliver(q.id, 200, detailJson(tournamentJson(kTid), {}, "[]"), "");
	CHECK(c.tournament().matches.empty());
	CHECK(c.tournament().nextMatch() == nullptr);
	// a cancelled one with its reason
	c.fetchTournament(kTid);
	q = take(c);
	c.deliver(q.id, 200, detailJson(tournamentJson(kTid, "Off", "faithful", "1v1", "cancelled"), {}, "[]", ",\"reason\":\"insufficient_players\""), "");
	CHECK(c.tournament().reason == "insufficient_players");
	events(c);

	// a match without a room code (not ours), or not open, or without both players is not playable
	PlatformApiClient d = makeClient();
	d.fetchTournament(kTid);
	q = take(d);
	d.deliver(q.id, 200, detailJson(tournament, { matchJson(1, 0, "open", "A", "B"), matchJson(2, 0, "finished", "C", "D", "C", "ABCDEF0123456789"), matchJson(3, 1, "ready", "E", "", "", "ABCDEF0123456789") },
				"[[\"" + mid(1) + "\",\"" + mid(2) + "\"],[\"" + mid(3) + "\"]]"), "");
	CHECK(d.tournament().nextMatch() == nullptr);

	struct Bad
	{
		std::string body, why;
	};
	auto mod = [&](const std::string &from, const std::string &to) {
		std::string body = detailJson(tournament, bracket(), kRounds);
		const std::size_t at = body.find(from);
		REQUIRE(at != std::string::npos);
		body.replace(at, from.size(), to);
		return body;
	};
	const std::vector<Bad> bad = {
		{ mod("\"winner\":\"Elrond\"", "\"winner\":\"Sauron\""), "matches[0].winner is not one of the match's players" },
		{ mod("\"room_code\":\"ABCDEF0123456789\"", "\"room_code\":\"x\""), "matches[1].room_code is not a room code" },
		{ mod("\"id\":\"" + mid(2) + "\",\"round\":0", "\"id\":\"" + mid(1) + "\",\"round\":0"), "matches[1].id " + mid(1) + " appears twice in the bracket" },
		{ mod("\"state\":\"open\"", "\"state\":\"won\""), "matches[1].state \"won\" is not waiting / open / ready / bye / finished / admin_resolved / no_show / disputed" },
		{ mod("\"id\":\"" + mid(3) + "\",\"round\":1", "\"id\":\"" + mid(3) + "\",\"round\":0"), "matches[2].id is not listed in round 0" },
		{ mod("\"id\":\"" + mid(3) + "\",\"round\":1", "\"id\":\"" + kTid2 + ".333333333333333333333333\",\"round\":1"), "matches[2].id is not a match id of this tournament" },
		{ mod("\"players\":[\"Ugluk\",\"Thror\"]", "\"players\":[\"Ugluk\"]"), "matches[1].players is not a list of 2 nicknames (or null)" },
		{ mod("\"players\":[\"Ugluk\",\"Thror\"]", "\"players\":[\"Ugluk\",\"Thror\",\"X\"]"), "matches[1].players is not a list of 2 nicknames (or null)" },
		{ mod("\"players\":[\"Ugluk\",\"Thror\"]", "\"players\":[\"Ugluk\",7]"), "matches[1].players[1] is not a nickname" },
		{ detailJson(tournament, bracket(), "[[\"" + mid(1) + "\"],[\"" + mid(3) + "\"]]"), "rounds name 2 matches, matches has 3" },
		{ detailJson(tournament, bracket(), "[[\"" + mid(1) + "\",\"" + mid(1) + "\"],[\"" + mid(3) + "\"]]"), "match " + mid(1) + " is listed twice in the rounds" },
		{ detailJson(tournament, bracket(), "[[\"nope\",\"" + mid(2) + "\"],[\"" + mid(3) + "\"]]"), "rounds[0][0] is not a match id of this tournament" },
		{ detailJson(tournament, bracket(), "[{},[]]"), "rounds[0] is not a list of at most 255 match ids" },
		{ detailJson(tournamentJson(kTid, "X", "faithful", "2v2", "running"), bracket(), kRounds), "size \"2v2\" is not 1v1 (platform v1 has no other tournament size)" },
		{ detailJson(tournament, bracket(), kRounds, ",\"reason\":\"has space\""), "reason is not a reason code" },
		{ mod("\"rules\":\"Best of one.\\nNo rushing.\"", "\"rulez\":\"\""), "rules is missing or not a string" },
	};
	for (const Bad &b : bad)
	{
		CAPTURE(b.body);
		c.fetchTournament(kTid);
		q = take(c);
		c.deliver(q.id, 200, b.body, "");
		const auto ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].kind == PlatformApiClient::Event::Refused);
		CHECK(ev[0].text == b.why);
		CHECK(c.tournament().info.state == "cancelled"); // the one kept
	}
	// an answer about another tournament than asked
	c.fetchTournament(kTid2);
	q = take(c);
	c.deliver(q.id, 200, detailJson(tournament, bracket(), kRounds), "");
	const auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].text == "the answer is about tournament " + kTid + ", not " + kTid2);
	// the caps: 9 rounds, 256 matches, 3 players in a match
	PlatformApiClient big = makeClient();
	std::string nineRounds = "[";
	for (int i = 0; i < 9; ++i)
	{
		nineRounds += std::string(i ? "," : "") + "[]";
	}
	big.fetchTournament(kTid);
	q = take(big);
	big.deliver(q.id, 200, detailJson(tournament, {}, nineRounds + "]"), "");
	CHECK(events(big)[0].text == "rounds has 9 entries (at most 8)");
	std::vector<std::string> many;
	std::string ids = "[[";
	for (int i = 1; i <= 256; ++i)
	{
		many.push_back(matchJson(i, 0, "waiting", "", ""));
		ids += std::string(i > 1 ? "," : "") + "\"" + mid(i) + "\"";
	}
	big.fetchTournament(kTid);
	q = take(big);
	big.deliver(q.id, 200, detailJson(tournament, many, ids + "]]"), "");
	CHECK(events(big)[0].text == "matches has 256 entries (at most 255)");
	many.pop_back();
	ids = "[[";
	for (int i = 1; i <= 255; ++i)
	{
		ids += std::string(i > 1 ? "," : "") + "\"" + mid(i) + "\"";
	}
	big.fetchTournament(kTid);
	q = take(big);
	big.deliver(q.id, 200, detailJson(tournament, many, ids + "]]"), "");
	CHECK(big.tournament().matches.size() == 255); // exactly the cap is fine
	CHECK(events(big)[0].kind == PlatformApiClient::Event::TournamentInfo);
	// the rules' bound
	big.fetchTournament(kTid);
	q = take(big);
	std::string longRules = detailJson(tournament, {}, "[]");
	const std::string needle = "Best of one.\\nNo rushing.";
	longRules.replace(longRules.find(needle), needle.size(), std::string(16385, 'r'));
	big.deliver(q.id, 200, longRules, "");
	CHECK(events(big)[0].text == "rules is longer than 16384 bytes or has control characters");
}

TEST_CASE("onlinehub1: tournaments: register, withdraw and check-in carry the install; the answers are checked; this session's registrations are kept")
{
	PlatformApiClient c = makeClient();
	c.fetchTournament(kTid);
	auto q = take(c);
	c.deliver(q.id, 200, detailJson(tournamentJson(kTid), {}, "[]"), "");
	events(c);
	CHECK_FALSE(c.registeredIn(kTid));
	c.registerTournament(kTid);
	q = take(c);
	CHECK(q.method == "POST");
	// the answer must say registered: true
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":false}", "");
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::Refused);
	CHECK(ev[0].text == "the answer's registered is not true");
	CHECK_FALSE(c.registeredIn(kTid));
	c.registerTournament(kTid);
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":true}", "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::Registered);
	CHECK(ev[0].flag);
	CHECK(c.registeredIn(kTid));
	q = take(c);
	CHECK(q.path == "/tournaments/" + kTid + "?install=" + kInstall); // the shown tournament is read again
	c.deliver(q.id, 0, "", "timeout");
	events(c);
	c.withdrawTournament(kTid);
	q = take(c);
	CHECK(q.method == "DELETE");
	c.deliver(q.id, 409, refusal("registration_closed"), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].request == "withdraw");
	CHECK(ev[0].code == "registration_closed");
	CHECK(ev[0].text == "Registration for this tournament is closed.");
	CHECK(c.registeredIn(kTid)); // a refused withdrawal changes nothing
	c.withdrawTournament(kTid);
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":true}", "");
	CHECK(events(c)[0].text == "the answer's registered is not false");
	CHECK(c.registeredIn(kTid));
	c.withdrawTournament(kTid);
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"registered\":false}", "");
	CHECK_FALSE(c.registeredIn(kTid));
	events(c);
	q = take(c); // the shown tournament is read again after a withdrawal too
	CHECK(q.path == "/tournaments/" + kTid + "?install=" + kInstall);
	c.deliver(q.id, 0, "", "timeout");
	events(c);
	// ids that are not the service's never reach it
	for (const std::string &id : { std::string("../admin"), std::string("spring"), std::string(31, 'a'), std::string(33, 'a'), std::string(32, 'A') })
	{
		c.registerTournament(id);
		c.withdrawTournament(id);
		c.fetchTournament(id);
		CHECK(take(c).id == 0);
		ev = events(c);
		REQUIRE(ev.size() == 3);
		CHECK(ev[0].code == "bad_field");
	}
	// check-in: the match must be this tournament's
	c.checkIn(kTid, mid(2));
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"state\":\"ready\"}", "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::CheckedIn);
	CHECK(ev[0].id == mid(2));
	CHECK(ev[0].text == "ready");
	c.checkIn(kTid2, mid(2));
	c.checkIn(kTid, "not-a-match");
	c.checkIn(kTid, kTid + ".short");
	CHECK(take(c).id == 0);
	CHECK(events(c).size() == 3);
	c.checkIn(kTid, mid(2));
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"state\":\"finished\"}", "");
	CHECK(events(c)[0].text == "the check-in's state \"finished\" is not open / ready");
	c.checkIn(kTid, mid(2));
	q = take(c);
	c.deliver(q.id, 409, refusal("checkin_closed"), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].request == "checkin");
	CHECK(ev[0].code == "checkin_closed");
	CHECK(ev[0].text == "Check-in for this match is closed.");
	c.checkIn(kTid, mid(2));
	q = take(c);
	c.deliver(q.id, 403, refusal("not_a_match_participant"), "");
	CHECK(events(c)[0].text == "You do not play in this match.");
}

TEST_CASE("onlinehub1: community statistics: the service's answer (overflow rows included) and its checks; the kept answer names its own filter")
{
	PlatformApiClient c = makeClient();
	std::string err;
	REQUIRE(c.fetchGlobalStats("uber", "30d", &err));
	auto q = take(c);
	CHECK(q.path == "/stats/global?mode=uber&period=30d");
	const std::string good = "{\"v\":1,\"as_of\":1790000000,\"games\":40,\"factions\":[{\"faction\":\"FactionMen\",\"picks\":30,\"wins\":18,\"win_rate\":0.6,\"pick_rate\":0.375},"
							 "{\"faction\":\"FactionMordor\",\"picks\":50,\"wins\":22,\"win_rate\":0.44,\"pick_rate\":0.625},"
							 "{\"faction\":\"other\",\"picks\":4,\"wins\":1,\"win_rate\":0.25,\"pick_rate\":0.05,\"overflow\":true}],"
							 "\"matchups\":[{\"a\":\"FactionMen\",\"b\":\"FactionMordor\",\"games\":20,\"a_wins\":12},{\"a\":\"other\",\"b\":\"other\",\"games\":3,\"a_wins\":1,\"overflow\":true}],"
							 "\"maps\":[{\"map\":\"maps/map mp fords of isen/map mp fords of isen.map\",\"games\":25},{\"map\":\"other\",\"games\":2,\"overflow\":true}],"
							 "\"avg_duration_s\":1250.5,\"dimension_policy\":{\"retained_limit\":64},\"vs_ai\":{\"games\":7,\"factions\":[],\"matchups\":[],\"maps\":[],\"avg_duration_s\":900}}";
	c.deliver(q.id, 200, good, "");
	const auto &g = c.globalStats();
	REQUIRE(g.known);
	CHECK(g.mode == "uber");
	CHECK(g.period == "30d");
	CHECK(g.asOf == 1790000000);
	CHECK(g.humans.games == 40);
	REQUIRE(g.humans.factions.size() == 3);
	CHECK_FALSE(g.humans.factions[1].overflow);
	CHECK(g.humans.factions[2].overflow);
	CHECK(g.humans.factions[2].faction == "other");
	CHECK(g.humans.matchups[1].overflow);
	CHECK(g.humans.maps[1].overflow);
	CHECK(g.humans.factions[0].winRate == 0.6);
	CHECK(g.humans.avgDurationS == 1250.5);
	CHECK(g.vsAi.games == 7);
	events(c);
	// another filter asked: until its answer comes the kept answer still names its own filter (a page never shows it under the new one)
	REQUIRE(c.fetchGlobalStats("", "7d", &err));
	CHECK(c.globalStats().mode == "uber");
	CHECK(c.globalStats().period == "30d");
	q = take(c);
	CHECK(q.path == "/stats/global?mode=&period=7d");
	c.deliver(q.id, 0, "", "timeout");
	CHECK(c.globalStats().humans.games == 40);
	events(c);
	// the not-yet-ready cache
	REQUIRE(c.fetchGlobalStats("", "all", &err));
	q = take(c);
	c.deliver(q.id, 503, refusal("stats_cache_unavailable"), "");
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].code == "stats_cache_unavailable");
	CHECK(ev[0].text == "The statistics are not ready yet: try again later.");
	const std::vector<std::pair<std::string, std::string>> bad = {
		{ "\"wins\":18,", "\"wins\":31," },
		{ "\"win_rate\":0.6", "\"win_rate\":1.5" },
		{ "\"a_wins\":12", "\"a_wins\":21" },
		{ "\"avg_duration_s\":1250.5", "\"avg_duration_s\":-1" },
		{ "\"games\":40", "\"games\":40.5" },
		{ "\"overflow\":true}],\"matchups\"", "\"overflow\":1}],\"matchups\"" },
	};
	const std::vector<std::string> why = {
		"factions[0].wins is above picks",
		"factions[0].win_rate is missing or not a fraction in 0 .. 1",
		"matchups[0].a_wins is above games",
		"avg_duration_s is missing or not a duration in 0 .. 604800 seconds",
		"games is missing or not a whole number in 0 .. 1000000000",
		"factions[2].overflow is not true / false",
	};
	for (std::size_t i = 0; i < bad.size(); ++i)
	{
		std::string body = good;
		const std::size_t at = body.find(bad[i].first);
		REQUIRE(at != std::string::npos);
		body.replace(at, bad[i].first.size(), bad[i].second);
		REQUIRE(c.fetchGlobalStats("", "all", &err));
		q = take(c);
		c.deliver(q.id, 200, body, "");
		ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].text == why[i]);
		CHECK(c.globalStats().humans.games == 40); // kept
	}
	// the container caps: 33 factions, 33 matchups, 33 maps (the service shows 20 and "other"), each refused before any is kept; exactly 32 is fine
	auto bucket = [](int factions, int matchups, int maps) {
		std::string out = "{\"v\":1,\"as_of\":1,\"games\":0,\"avg_duration_s\":0,\"vs_ai\":{\"games\":0,\"factions\":[],\"matchups\":[],\"maps\":[],\"avg_duration_s\":0},\"factions\":[";
		for (int i = 0; i < factions; ++i)
		{
			out += std::string(i ? "," : "") + "{\"faction\":\"F" + std::to_string(i) + "\",\"picks\":0,\"wins\":0,\"win_rate\":0,\"pick_rate\":0}";
		}
		out += "],\"matchups\":[";
		for (int i = 0; i < matchups; ++i)
		{
			out += std::string(i ? "," : "") + "{\"a\":\"A\",\"b\":\"B" + std::to_string(i) + "\",\"games\":0,\"a_wins\":0}";
		}
		out += "],\"maps\":[";
		for (int i = 0; i < maps; ++i)
		{
			out += std::string(i ? "," : "") + "{\"map\":\"m" + std::to_string(i) + "\",\"games\":0}";
		}
		return out + "]}";
	};
	const std::vector<std::tuple<int, int, int, std::string>> caps = {
		{ 33, 0, 0, "factions has 33 entries (at most 32)" }, { 0, 33, 0, "matchups has 33 entries (at most 32)" }, { 0, 0, 33, "maps has 33 entries (at most 32)" },
		{ 32, 32, 32, "" } };
	for (const auto &k : caps)
	{
		REQUIRE(c.fetchGlobalStats("faithful", "all", &err));
		q = take(c);
		c.deliver(q.id, 200, bucket(std::get<0>(k), std::get<1>(k), std::get<2>(k)), "");
		ev = events(c);
		REQUIRE(ev.size() == 1);
		if (std::get<3>(k).empty())
		{
			CHECK(ev[0].kind == PlatformApiClient::Event::GlobalStatsReady);
			CHECK(c.globalStats().humans.factions.size() == 32);
			CHECK(c.globalStats().mode == "faithful");
		}
		else
		{
			CHECK(ev[0].text == std::get<3>(k));
		}
	}
}

TEST_CASE("onlinehub1: results: sent once, 201 or 200, retried once after the retry time when the service failed, refusals not retried")
{
	PlatformApiClient c = makeClient();
	c.update(0);
	c.sendResult({ "g1", "{\"v\":1}" });
	c.sendResult({ "g1", "{\"v\":1}" }); // the same game again: not sent again
	auto q = take(c);
	CHECK(q.method == "POST");
	CHECK(q.path == "/results");
	c.deliver(q.id, 201, "{\"v\":1,\"accepted\":true,\"confirmed\":false}", "");
	CHECK(take(c).id == 0);
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::ResultSent);
	CHECK(ev[0].id == "g1");
	c.sendResult({ "lan-0123456789abcdef", "{\"v\":1}" });
	q = take(c);
	c.deliver(q.id, 200, "{\"v\":1,\"accepted\":true,\"confirmed\":true}", ""); // the same report again: 200
	CHECK(events(c)[0].kind == PlatformApiClient::Event::ResultSent);
	// a tournament match's id has a dot
	const std::string match = mid(7);
	c.sendResult({ match, "{\"v\":1}" });
	q = take(c);
	c.deliver(q.id, 201, "{\"v\":1,\"accepted\":true,\"confirmed\":false}", "");
	CHECK(events(c)[0].id == match);
	CHECK(c.sentCount("result") == 3);

	// the service unreachable: one retry after resultRetryMs, then dropped by name
	c.sendResult({ "g2", "{\"v\":1,\"x\":2}" });
	q = take(c);
	c.deliver(q.id, 0, "", "cannot reach the service");
	CHECK(events(c).empty());
	CHECK(c.pendingResults() == 1);
	c.update(29999);
	CHECK(take(c).id == 0);
	c.update(30000);
	q = take(c);
	CHECK(q.path == "/results");
	CHECK(q.body == "{\"v\":1,\"x\":2}");
	c.deliver(q.id, 503, refusal("unavailable"), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::ResultDropped);
	CHECK(ev[0].text == "unavailable: The online services are not available right now.");
	c.update(120000);
	CHECK(take(c).id == 0);

	// a refusal (a bad field) is not retried
	c.sendResult({ "g3", "{\"v\":1}" });
	q = take(c);
	c.deliver(q.id, 400, refusal("invalid_field", "game.players"), "");
	ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == PlatformApiClient::Event::ResultDropped);
	CHECK(ev[0].text == "invalid_field: The online services refused the request: game.players is not valid.");
	c.update(1000000);
	CHECK(take(c).id == 0);
	CHECK(c.pendingResults() == 0);
	// a result that is not a result never reaches the service
	for (const PlatformApiClient::ResultReport &bad : { PlatformApiClient::ResultReport{ "", "{\"v\":1}" }, PlatformApiClient::ResultReport{ "g 4", "{\"v\":1}" },
			 PlatformApiClient::ResultReport{ "g4", "" }, PlatformApiClient::ResultReport{ "g4", std::string(16 * 1024 + 1, ' ') },
			 PlatformApiClient::ResultReport{ std::string(129, 'g'), "{\"v\":1}" } })
	{
		c.sendResult(bad);
		CHECK(take(c).id == 0);
		ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].code == "bad_field");
	}
	c.sendResult({ std::string(128, 'g'), "{\"v\":1}" }); // exactly the longest id
	CHECK(take(c).id != 0); // tolerance: a request id is any non-zero number; 0 is "no request"
}

TEST_CASE("onlinehub1 review: results: turning sharing off drops the results queued or waiting for their retry and none is sent or retried until it is on again")
{
	std::string err;
	SUBCASE("queued results, a result waiting for its retry and one on the wire")
	{
		PlatformApiClient c = makeClient();
		c.update(0);
		c.sendResult({ "retry-me", "{\"v\":1}" });
		auto first = take(c);
		c.deliver(first.id, 0, "", "timeout");
		REQUIRE(c.pendingResults() == 1); // waiting for its retry
		c.sendResult({ "on-the-wire", "{\"v\":1}" });
		auto wire = take(c);
		c.sendResult({ "queued-1", "{\"v\":1}" });
		c.sendResult({ "queued-2", "{\"v\":1}" });
		events(c);
		c.setShareResults(false);
		auto ev = events(c);
		std::vector<std::string> dropped;
		for (const auto &e : ev)
		{
			CHECK(e.kind == PlatformApiClient::Event::ResultDropped);
			CHECK(e.text == "the player does not share game results");
			dropped.push_back(e.id);
		}
		std::sort(dropped.begin(), dropped.end());
		CHECK(dropped == std::vector<std::string>{ "queued-1", "queued-2", "retry-me" });
		CHECK(c.pendingResults() == 0);
		// the retry's time comes: nothing is sent
		c.update(1000000);
		CHECK(take(c).id == 0);
		// the one already on the wire fails: it is not retried either, and says why
		c.deliver(wire.id, 503, refusal("unavailable"), "");
		ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].id == "on-the-wire");
		CHECK(ev[0].text == "the player does not share game results");
		c.update(2000000);
		CHECK(take(c).id == 0);
		CHECK(c.pendingResults() == 0);
		// a result while it is off is dropped at once, nothing queued
		c.sendResult({ "while-off", "{\"v\":1}" });
		CHECK(take(c).id == 0);
		ev = events(c);
		REQUIRE(ev.size() == 1);
		CHECK(ev[0].id == "while-off");
		// on again: the next result goes (and "while-off", never sent, may still be sent: it was not recorded as sent)
		c.setShareResults(true);
		c.sendResult({ "while-off", "{\"v\":1}" });
		CHECK(take(c).path == "/results");
	}
	SUBCASE("a client made with sharing off sends none")
	{
		PlatformApiClient::Options o = baseOptions();
		o.shareResults = false;
		PlatformApiClient c(o);
		c.sendResult({ "g", "{\"v\":1}" });
		CHECK(take(c).id == 0);
		CHECK(events(c).size() == 1);
		CHECK(c.sentCount("result") == 0);
	}
	SUBCASE("other requests are not touched by it")
	{
		PlatformApiClient c = makeClient();
		searching(c);
		c.setShareResults(false);
		c.update(6000);
		CHECK(take(c).path == "/qm/tickets/" + kTicket1);
	}
}

TEST_CASE("onlinehub1 review: the client's own containers are bounded: requests waiting, results remembered, events kept")
{
	PlatformApiClient c = makeClient();
	c.update(0);
	// 64 requests may wait; the 65th is refused by name and nothing of it is kept
	for (std::size_t i = 0; i < PlatformApiClient::kMaxQueue; ++i)
	{
		c.listTournaments();
	}
	CHECK(events(c).empty());
	c.listTournaments();
	auto ev = events(c);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].code == "busy");
	CHECK(ev[0].text == "too many requests are waiting for the online services");
	std::size_t sent = 0;
	for (int guard = 0; guard < 100; ++guard)
	{
		auto q = take(c);
		if (!q.id)
		{
			break;
		}
		++sent;
		c.deliver(q.id, 200, listJson({}), "");
	}
	CHECK(sent == PlatformApiClient::kMaxQueue);
	events(c);
	// results: a game forgotten after kMaxResultsKept newer ones may be sent again; one inside the window may not
	for (std::size_t i = 0; i < PlatformApiClient::kMaxResultsKept; ++i)
	{
		c.sendResult({ "r" + std::to_string(i), "{\"v\":1}" });
		auto q = take(c);
		REQUIRE(q.id != 0); // tolerance: a request id is any non-zero number; 0 is "no request"
		c.deliver(q.id, 201, "{\"v\":1}", "");
	}
	c.sendResult({ "r1", "{\"v\":1}" });
	CHECK(take(c).id == 0); // still remembered
	c.sendResult({ "r0", "{\"v\":1}" });
	CHECK(take(c).id == 0); // the oldest of the 256 is still inside the window
	c.sendResult({ "r256", "{\"v\":1}" });
	auto q = take(c);
	REQUIRE(q.id != 0); // tolerance: a request id is any non-zero number; 0 is "no request"
	c.deliver(q.id, 201, "{\"v\":1}", "");
	c.sendResult({ "r0", "{\"v\":1}" });
	CHECK(take(c).id != 0); // tolerance: 0 is "no request"; r0 is the 257th back: forgotten, so sent again
	// events nobody reads are capped: the oldest go
	PlatformApiClient d = makeClient();
	for (std::size_t i = 0; i < PlatformApiClient::kMaxEvents + 10; ++i)
	{
		d.registerTournament("bad");
	}
	CHECK(events(d).size() == PlatformApiClient::kMaxEvents);
	CHECK(d.takeUiEvents().size() == PlatformApiClient::kMaxUiEvents);
}

TEST_CASE("onlinehub1 review 2: Quick Match with the request queue full: the search is refused by name, a poll that cannot be queued is tried again, a ticket never made fails the search")
{
	SUBCASE("Search with 64 requests waiting returns false, says why and leaves the client idle (it used to return true and stay Starting)")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		for (std::size_t i = 0; i < PlatformApiClient::kMaxQueue; ++i)
		{
			c.listTournaments();
		}
		events(c);
		std::string err;
		CHECK_FALSE(c.qmSearch("faithful", "1v1", { "FactionMen" }, &err));
		CHECK(err == "too many requests are waiting for the online services");
		CHECK(c.qmState() == PlatformApiClient::QmState::Idle);
		// room again: the same search starts and asks for its ticket
		for (std::size_t i = 0; i < PlatformApiClient::kMaxQueue; ++i)
		{
			auto q = take(c);
			c.deliver(q.id, 200, listJson({}), "");
		}
		CHECK(c.qmSearch("faithful", "1v1", { "FactionMen" }, &err));
		auto q = take(c);
		CHECK(q.method == "POST");
		CHECK(q.path == "/qm/tickets");
	}
	SUBCASE("a poll that finds the queue full is asked again once there is room (the search is never stuck waiting for a poll that was not queued)")
	{
		PlatformApiClient c = makeClient();
		searching(c);
		for (std::size_t i = 0; i < PlatformApiClient::kMaxQueue; ++i)
		{
			c.listTournaments();
		}
		c.update(6000); // the poll is due; the queue is full
		std::size_t polls = 0;
		for (std::size_t i = 0; i < PlatformApiClient::kMaxQueue; ++i)
		{
			auto q = take(c);
			polls += q.path == "/qm/tickets/" + kTicket1 ? 1 : 0;
			c.deliver(q.id, 200, listJson({}), "");
		}
		CHECK(polls == 0);
		c.update(7000);
		auto q = take(c);
		CHECK(q.method == "GET");
		CHECK(q.path == "/qm/tickets/" + kTicket1);
	}
	SUBCASE("a ticket request that is never answered fails the search after the start time; its late ticket is deleted and not adopted")
	{
		PlatformApiClient c = makeClient();
		c.update(1000);
		std::string err;
		REQUIRE(c.qmSearch("faithful", "1v1", { "FactionMen" }, &err));
		auto q = take(c);
		REQUIRE(q.path == "/qm/tickets");
		c.update(1000 + 59999);
		CHECK(c.qmState() == PlatformApiClient::QmState::Starting);
		c.update(1000 + 60000);
		CHECK(c.qmState() == PlatformApiClient::QmState::Failed);
		CHECK(c.qmProblem() == "the online services did not make the search's ticket in time");
		c.deliver(q.id, 201, created(kTicket1), "");
		CHECK(c.qmState() == PlatformApiClient::QmState::Failed);
		auto del = take(c);
		CHECK(del.method == "DELETE");
		CHECK(del.path == "/qm/tickets/" + kTicket1);
		// a new search can start at once
		CHECK(c.qmSearch("faithful", "1v1", { "FactionMen" }, &err));
	}
}

TEST_CASE("onlinehub1: an answer to a request that was not asked, or a second answer, changes nothing")
{
	PlatformApiClient c = makeClient();
	c.checkHealth();
	auto q = take(c);
	CHECK(q.path == "/health");
	c.deliver(q.id + 5, 200, "{\"v\":1}", "");
	CHECK(c.healthKnown() == -1);
	c.deliver(q.id, 200, "{\"v\":1,\"status\":\"ok\"}", "");
	CHECK(c.healthKnown() == 1);
	c.deliver(q.id, 200, "{\"v\":1}", "");
	CHECK(events(c).size() == 1);
	c.checkHealth();
	q = take(c);
	c.deliver(q.id, 0, "", "no route");
	CHECK(c.healthKnown() == 0);
}

TEST_CASE("onlinehub1: game records: a record round-trips, and every field outside its set or bound is refused by name")
{
	GameRecord r = sampleRecord("abc123", "online", "FactionMen", 0, 1790000600);
	const std::string line = GameRecords::toJson(r);
	CHECK(line == "{\"v\":1,\"id\":\"abc123\",\"kind\":\"online\",\"mode\":\"faithful\",\"map\":\"maps/map mp fords of isen\",\"start\":1790000000,"
				  "\"end\":1790000600,\"duration_s\":590,\"players\":[{\"slot\":0,\"nick\":\"Elrond\",\"faction\":\"FactionMen\",\"team\":0,"
				  "\"human\":true},{\"slot\":1,\"nick\":\"Gothmog\",\"faction\":\"FactionMordor\",\"team\":1,\"human\":true}],\"me\":0,\"winner_team\":0}");
	GameRecord back;
	std::string err;
	REQUIRE(GameRecords::parse(line, back, &err));
	CHECK(GameRecords::toJson(back) == line);
	CHECK(back.won());
	CHECK_FALSE(back.vsComputerOnly());

	const std::vector<std::pair<std::pair<std::string, std::string>, std::string>> bad = {
		{ { "\"kind\":\"online\"", "\"kind\":\"campaign\"" }, "kind \"campaign\" is not skirmish / lan / online" },
		{ { "\"mode\":\"faithful\"", "\"mode\":\"fast\"" }, "mode \"fast\" is not faithful / uber" },
		{ { "\"me\":0", "\"me\":5" }, "me (slot 5) is not one of the players" },
		{ { "\"winner_team\":0", "\"winner_team\":3" }, "winner_team 3 is no player's team" },
		{ { "\"slot\":1", "\"slot\":0" }, "players[1].slot 0 appears twice" },
		{ { "\"end\":1790000600", "\"end\":1" }, "end is before start" },
		{ { "\"id\":\"abc123\"", "\"id\":\"../x\"" }, "id is not an id ([A-Za-z0-9_-], 1 .. 64)" },
		{ { "\"faction\":\"FactionMen\"", "\"faction\":\"Men Of\"" }, "players[0].faction is not a faction name" },
		{ { "\"team\":1", "\"team\":8" }, "players[1].team is missing or not a whole number in 0 .. 7" },
		{ { "\"human\":true}", "\"human\":1}" }, "players[0].human is missing or not true / false" },
		{ { "\"v\":1", "\"v\":2" }, "v is not 1" },
		{ { "\"duration_s\":590", "\"duration_s\":604801" }, "duration_s is missing or not a whole number in 0 .. 604800" },
	};
	for (const auto &b : bad)
	{
		std::string text = line;
		const std::size_t at = text.find(b.first.first);
		REQUIRE(at != std::string::npos);
		text.replace(at, b.first.first.size(), b.first.second);
		GameRecord out = back;
		CHECK_FALSE(GameRecords::parse(text, out, &err));
		CHECK(err == b.second);
		CHECK(GameRecords::toJson(out) == line); // untouched
	}
	// the local player must be a human one
	GameRecord ai = sampleRecord("x1", "skirmish", "FactionMen", 1, 10000);
	ai.meSlot = 1;
	CHECK_FALSE(GameRecords::parse(GameRecords::toJson(ai), back, &err));
	CHECK(err == "me (slot 1) is not a human player");
	CHECK_FALSE(GameRecords::parse(std::string(GameRecords::kMaxLineBytes + 1, ' '), back, &err));
	CHECK(err == "the record is longer than 4096 bytes");
	// players without a team get one of their own: the lowest team numbers 0 .. 7 nobody else has (the platform takes teams 0 .. 7 only)
	std::vector<GameRecord::Player> players = { { 0, "A", "FactionMen", -1, true }, { 3, "B", "FactionElves", -1, false }, { 4, "C", "FactionWild", 0, false } };
	GameRecords::normaliseTeams(players);
	CHECK(players[0].team == 1);
	CHECK(players[1].team == 2);
	CHECK(players[2].team == 0);
	// eight players none of whom has a team: 0 .. 7, each its own
	std::vector<GameRecord::Player> eight;
	for (int i = 0; i < 8; ++i)
	{
		eight.push_back({ i, "P" + std::to_string(i), "FactionMen", -1, i == 0 });
	}
	GameRecords::normaliseTeams(eight);
	for (int i = 0; i < 8; ++i)
	{
		CHECK(eight[(std::size_t)i].team == i);
	}
}

TEST_CASE("onlinehub1: game records: the file's bad lines and duplicates are named and skipped; Your stats counts exactly")
{
	std::string text;
	// chronological by end: men win, men win, mordor loses, elves win, men (no winner), men win, men lose; one skirmish among them
	text += GameRecords::toJson(sampleRecord("g1", "online", "FactionMen", 0, 1000, "maps/a")) + "\n";
	text += GameRecords::toJson(sampleRecord("g2", "lan", "FactionMen", 0, 2000, "maps/b")) + "\r\n";
	text += "{\"v\":1,\"id\":\"broken\"}\n";
	text += GameRecords::toJson(sampleRecord("g3", "online", "FactionMordor", 1, 3000, "maps/b")) + "\n";
	text += "\n";
	text += GameRecords::toJson(sampleRecord("g4", "skirmish", "FactionElves", 0, 4000, "maps/a")) + "\n";
	text += GameRecords::toJson(sampleRecord("g2", "lan", "FactionMen", 1, 4500, "maps/b")) + "\n"; // the same game again
	text += GameRecords::toJson(sampleRecord("g5", "online", "FactionMen", -1, 5000, "maps/c")) + "\n";
	text += GameRecords::toJson(sampleRecord("g7", "online", "FactionMen", 1, 7000, "maps/a")) + "\n";
	text += GameRecords::toJson(sampleRecord("g6", "online", "FactionMen", 0, 6000, "maps/b"));
	std::vector<std::string> rejected;
	const std::vector<GameRecord> records = GameRecords::load(text, &rejected);
	REQUIRE(rejected.size() == 2);
	CHECK(rejected[0] == "line 3: kind is missing or not a string");
	CHECK(rejected[1] == "line 7: game g2 is recorded already");
	REQUIRE(records.size() == 7);

	const PlayerStats s = GameRecords::summarise(records);
	CHECK(s.games == 7);
	CHECK(s.wins == 4);
	CHECK(s.losses == 2);
	CHECK(s.undecided == 1);
	CHECK(s.winRate() == 4.0 / 7.0);
	REQUIRE(s.factions.size() == 3);
	CHECK(s.factions[0].faction == "FactionMen");
	CHECK(s.factions[0].picks == 5);
	CHECK(s.factions[0].wins == 3);
	CHECK(s.factions[1].faction == "FactionElves"); // 1 pick 1 win before Mordor's 1 pick 0 wins
	CHECK(s.factions[2].faction == "FactionMordor");
	CHECK(s.favouriteFaction == "FactionMen");
	CHECK(s.favouriteMap == "maps/a"); // g1, g4, g7 and maps/b's g2, g3, g6: a tie goes to the first name
	CHECK(s.favouriteMapGames == 3);
	// by end: g1 W, g2 W, g3 L, g4 W, g5 -, g6 W, g7 L
	CHECK(s.longestWinStreak == 2);
	CHECK(s.currentStreak == -1);
	REQUIRE(s.last.size() == 7);
	CHECK(s.last[0].id == "g7");
	CHECK(s.last[0].result == 0);
	CHECK(s.last[1].id == "g6");
	CHECK(s.last[2].result == -1);
	CHECK(s.last[6].id == "g1");

	const PlayerStats online = GameRecords::summarise(records, "online");
	CHECK(online.games == 5);
	CHECK(online.wins == 2);
	const PlayerStats none = GameRecords::summarise({});
	CHECK(none.games == 0);
	CHECK(none.favouriteFaction.empty());
	CHECK(none.winRate() == 0.0);
	CHECK(none.last.empty());

	// the last 20 only, and the file's bound on records (the newest are kept)
	std::string big;
	for (int i = 0; i < 25; ++i)
	{
		big += GameRecords::toJson(sampleRecord("n" + std::to_string(i), "skirmish", "FactionMen", 0, 1000 + i)) + "\n";
	}
	const PlayerStats many = GameRecords::summarise(GameRecords::load(big, nullptr));
	CHECK(many.games == 25);
	CHECK(many.last.size() == 20);
	CHECK(many.last[0].id == "n24");
	CHECK(many.longestWinStreak == 25);
}

TEST_CASE("onlinehub1: the results report: LAN and online games only, the platform's body with its client metadata, the install id checked")
{
	GameRecord r = sampleRecord("abc123", "online", "FactionMen", 0, 1790000600);
	std::string err;
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "linux", "0.4.5-test", &err)
		== "{\"v\":1,\"install\":\"" + kInstall + "\",\"nick\":\"Elrond\",\"client\":{\"platform\":\"linux\",\"version\":\"0.4.5-test\"},\"game\":{\"id\":\"abc123\","
			  "\"mode\":\"faithful\",\"map\":\"maps/map mp fords of isen\",\"players\":[{\"slot\":0,\"nick\":\"Elrond\",\"faction\":\"FactionMen\",\"team\":0,"
			  "\"human\":true},{\"slot\":1,\"nick\":\"Gothmog\",\"faction\":\"FactionMordor\",\"team\":1,\"human\":true}],\"start\":1790000000,"
			  "\"end\":1790000600,\"duration_s\":590,\"winner_team\":0},\"me\":{\"slot\":0}}");
	JsonValue parsed;
	REQUIRE(JsonValue::parse(GameRecords::resultBody(r, kInstall, "Elrond", "windows", "v", &err), parsed, &err));
	r.winnerTeam = -1;
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "macos", "x", &err).find("\"winner_team\":null") != std::string::npos);
	GameRecord sk = sampleRecord("s1", "skirmish", "FactionMen", 0, 5000);
	CHECK(GameRecords::resultBody(sk, kInstall, "Elrond", "linux", "v", &err).empty());
	CHECK(err == "a skirmish game is not reported (single player)");
	CHECK(GameRecords::resultBody(r, "nope", "Elrond", "linux", "v", &err).empty());
	CHECK(err == "the install id is not 32 hex digits");
	CHECK(GameRecords::resultBody(r, kInstall, "", "linux", "v", &err).empty());
	CHECK(err == "the nickname is empty, longer than 24 bytes or has control characters");
	// the service refuses a write without client metadata, and a game with fewer than two players: neither is built
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "", "v", &err).empty());
	CHECK(err == "the client metadata is not usable: the platform is not windows / linux / macos");
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "freebsd", "v", &err).empty());
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "linux", "", &err).empty());
	CHECK(err == "the client metadata is not usable: the version tag is empty, longer than 64 bytes or has control characters");
	CHECK(GameRecords::resultBody(r, kInstall, "Elrond", "linux", std::string(65, 'v'), &err).empty());
	CHECK_FALSE(GameRecords::resultBody(r, kInstall, "Elrond", "linux", std::string(64, 'v'), &err).empty());
	GameRecord solo = r;
	solo.players.resize(1);
	CHECK(GameRecords::resultBody(solo, kInstall, "Elrond", "linux", "v", &err).empty());
	CHECK(err == "a game with fewer than two players is not reported (the service takes 2 .. 8)");
	// lane SOLO-1: an online game with a single human is never reported, AI slots or not; a LAN game's one human still reports
	GameRecord soloAI = r;
	soloAI.players[1].human = false;
	CHECK(GameRecords::resultBody(soloAI, kInstall, "Elrond", "linux", "v", &err).empty());
	CHECK(err == "an online game with a single human is not reported (a custom game may start solo)");
	soloAI.kind = "lan";
	CHECK_FALSE(GameRecords::resultBody(soloAI, kInstall, "Elrond", "linux", "v", &err).empty());
}
