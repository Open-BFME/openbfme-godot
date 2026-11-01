#include "doctest.h"

#include "GameNetwork/OnlineLobbyClient.h"

#include <algorithm>
#include <string>

namespace
{
OnlineLobbyClient::Options socialOptions()
{
	OnlineLobbyClient::Options options;
	options.profileDigest = "0123456789abcdef0123456789abcdef";
	options.installId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	options.clientPlatform = "linux";
	options.platform = PlatformKind::Linux;
	options.clientVersion = "test";
	options.enableSocial = true;
	options.pollMs = 100;
	return options;
}

// lane JOINVER-1: the build a join body tells (socialOptions: version "test" on Linux, no build day or official flag)
const std::string kTestBuild = R"("build":{"version":"test","platform":"linux"})";
const std::string me = R"({"v":1,"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self","privacy":{"invites":"friends_or_room","challenges":"friends_or_room","requests":"anyone","presence":"friends"},"hidden":false})";
const std::string friendPlayer = R"({"pid":"2222222222222222","code":"BCDE-FGHJ-KM","nick":"Peer"})";
const std::string presence = R"({"pid":"2222222222222222","state":"online","game":null,"joinable":false,"since":0,"map":null,"mode":null})";
const std::string emptyInbox = R"({"v":1,"seq":"aaaaaaaaaaaaaaaa:1","reset":false,"presence":[],"requests":[],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":1,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25})";

OnlineLobbyClient::HttpRequest findChannel(const std::vector<OnlineLobbyClient::HttpRequest> &requests, OnlineLobbyClient::HttpRequest::Channel channel);

const std::string socialHealth = R"({"v":1,"ok":true,"games":0,"relays":[],"features":["social","build"]})"; // JOINVER-1: a lobby that takes `build` on an invite accept

// ONLINEMENU-1: a guest sign-in asks the lobby's features first (GET /v1/health); `health` answers it
void answerFeatures(OnlineLobbyClient &client, const std::string &health = socialHealth)
{
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].path == "/v1/health");
	client.deliver(requests[0].id, 200, health, "");
}

void signInSocial(OnlineLobbyClient &client)
{
	client.signInGuest(u"Self");
	answerFeatures(client);
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"token":"session","account":{"id":"guest:1","name":"Self","kind":"guest","pid":"1111111111111111"}})", "");
}

void loadInitialSocialViews(OnlineLobbyClient &client)
{
	signInSocial(client);
	auto requests = client.takeRequests();
	client.deliver(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).id, 200, me, "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, R"({"v":1,"friends":[]})", "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
}

std::string sourceGameView(const std::string &id, const std::string &mode = "normal")
{
	std::string slots = "[";
	for (int slot = 0; slot < LEGACY_MAX_SLOTS; ++slot) // an eight-start room: the lobby sends eight records (ONLINE16-FIX: sixteen only with mapStartCount 9 .. 16)
	{
		if (slot) slots += ",";
		const bool you = slot == 0;
		slots += "{\"slot\":" + std::to_string(slot) + ",\"state\":\"" + (you ? "player" : "open") +
			"\",\"name\":" + (you ? "\"Self\"" : "null") +
			",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
			(you ? "true" : "false") + "}";
	}
	slots += "]";
	return "{\"id\":" + OnlineLobbyClient::jsonQuote(id) +
		",\"mode\":" + OnlineLobbyClient::jsonQuote(mode) +
		",\"name\":\"Source room\",\"state\":\"open\",\"private\":false,\"hostSlot\":0,\"seq\":1,"
		"\"setup\":{\"map\":\"map\",\"startingCash\":1000,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},"
		"\"slots\":" + slots + "}";
}

OnlineLobbyClient::HttpRequest findChannel(const std::vector<OnlineLobbyClient::HttpRequest> &requests, OnlineLobbyClient::HttpRequest::Channel channel)
{
	auto it = std::find_if(requests.begin(), requests.end(), [&](const auto &r) { return r.channel == channel; });
	return it == requests.end() ? OnlineLobbyClient::HttpRequest{} : *it;
}
}

TEST_CASE("SOCIAL client starts authenticated views and holds an independent replaceable inbox snapshot")
{
	OnlineLobbyClient client(socialOptions());
	signInSocial(client);
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 2);
	const auto inbox = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(inbox.id != 0);
	CHECK(inbox.held);
	CHECK(inbox.path == "/v1/inbox?wait=25");
	CHECK(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).path == "/v1/me");

	auto meRequest = findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary);
	client.deliver(meRequest.id, 200, me, "");
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/friends");
	client.deliver(requests[0].id, 200, "{\"v\":1,\"friends\":[{" +
		friendPlayer.substr(1, friendPlayer.size() - 2) + ",\"since\":0,\"presence\":" + presence + "}]}", "");
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/recent");
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");

	const std::string request = R"({"id":"3333333333333333","from":{"pid":"2222222222222222","code":"BCDE-FGHJ-KM","nick":"Peer"},"to":{"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self"},"at":10})";
	std::string snapshot = R"({"v":1,"seq":"aaaaaaaaaaaaaaaa:1","reset":false,"presence":[],"requests":[)" + request +
		R"(],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":1,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25})";
	client.deliver(inbox.id, 200, snapshot, "");
	REQUIRE(client.social().requests.size() == 1);
	CHECK(client.social().seq == "aaaaaaaaaaaaaaaa:1");
	CHECK(client.social().friends.size() == 1);

	client.update(100);
	requests = client.takeRequests();
	const auto replacement = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(replacement.id != 0);
	CHECK(replacement.path == "/v1/inbox?since=aaaaaaaaaaaaaaaa%3A1&wait=25");
	client.deliver(replacement.id, 200, emptyInbox, "");
	CHECK(client.social().requests.empty());
	CHECK(client.social().invites.empty());
	CHECK(client.social().presence.empty());
	CHECK(client.social().chatHeads.empty());
}

TEST_CASE("SOCIAL offline presence accepts the real lobby's undisclosed timestamp")
{
	OnlineLobbyClient client(socialOptions());
	signInSocial(client);
	auto requests = client.takeRequests();
	const auto inbox = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	client.deliver(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).id, 200, me, "");
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	const std::string offline = R"({"pid":"2222222222222222","state":"offline","game":null,"joinable":false,"since":null,"map":null,"mode":null})";
	client.deliver(requests[0].id, 200, "{\"v\":1,\"friends\":[{" +
		friendPlayer.substr(1, friendPlayer.size() - 2) + ",\"since\":123,\"presence\":" + offline + "}]}", "");
	REQUIRE(client.social().friends.size() == 1);
	REQUIRE(client.social().presence.size() == 1);
	CHECK(client.social().presence[0].state == "offline");
	CHECK(client.social().presence[0].since == 0);
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	std::string snapshot = emptyInbox;
	snapshot.replace(snapshot.find("\"presence\":[]"), std::string("\"presence\":[]").size(), "\"presence\":[" + offline + "]");
	client.deliver(inbox.id, 200, snapshot, "");
	REQUIRE(client.social().presence.size() == 1);
	CHECK(client.social().presence[0].state == "offline");
	CHECK(client.social().presence[0].since == 0);
	for (const auto &event : client.takeSocialEvents())
	{
		CHECK(event.kind != OnlineLobbyClient::SocialEvent::Error);
	}
	client.update(100);
	requests = client.takeRequests();
	const auto held = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	// tolerance: request IDs are dynamic; zero means no matching request was found.
	REQUIRE(held.id != 0);
	snapshot.replace(snapshot.find("\"offline\""), std::string("\"offline\"").size(), "\"online\"");
	client.deliver(held.id, 200, snapshot, "");
	CHECK(client.social().presence[0].state == "offline");
	const auto events = client.takeSocialEvents();
	CHECK(std::any_of(events.begin(), events.end(), [](const auto &event) {
		return event.kind == OnlineLobbyClient::SocialEvent::Error && event.code == "protocol";
	}));
}

TEST_CASE("SOCIAL client rejects malformed replacement snapshots atomically and stops on auth termination")
{
	OnlineLobbyClient client(socialOptions());
	signInSocial(client);
	auto requests = client.takeRequests();
	const auto inbox = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	client.deliver(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).id, 200, me, "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, "{\"v\":1,\"friends\":[]}", "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	const std::string request = R"({"id":"3333333333333333","from":{"pid":"2222222222222222","code":"BCDE-FGHJ-KM","nick":"Peer"},"to":{"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self"},"at":10})";
	client.deliver(inbox.id, 200, R"({"v":1,"seq":"aaaaaaaaaaaaaaaa:1","reset":false,"presence":[],"requests":[)" + request +
		R"(],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":1,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25})", "");
	const auto before = client.social().requests;
	client.update(100);
	requests = client.takeRequests();
	const auto held = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(held.id != 0);
	client.deliver(held.id, 200, R"({"v":1,"seq":"bad","reset":false,"presence":[],"requests":[],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":0,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25})", "");
	CHECK(client.social().requests.size() == before.size());
	CHECK(client.social().requests[0].id == before[0].id);
	auto events = client.takeSocialEvents();
	CHECK(std::any_of(events.begin(), events.end(), [](const auto &e) { return e.kind == OnlineLobbyClient::SocialEvent::Error && e.code == "protocol"; }));

	client.update(1000);
	requests = client.takeRequests();
	const auto next = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(next.id != 0);
	client.deliver(next.id, 401, R"({"v":1,"error":"session_revoked","message":"secret must not appear"})", "");
	CHECK_FALSE(client.social().available);
	events = client.takeSocialEvents();
	REQUIRE_FALSE(events.empty());
	CHECK(events.back().code == "session_revoked");
	CHECK(events.back().text.find("secret") == std::string::npos);
}

TEST_CASE("SOCIAL mutation methods validate route ids and preserve caller DM idempotency keys")
{
	OnlineLobbyClient client(socialOptions());
	signInSocial(client);
	auto requests = client.takeRequests();
	auto meRequest = findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary);
	client.deliver(meRequest.id, 200, me, "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, "{\"v\":1,\"friends\":[]}", "");
	requests = client.takeRequests();
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	CHECK_FALSE(client.sendMessage("BAD", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "hello"));
	CHECK(client.sendMessage("2222222222222222", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "hello"));
	CHECK_FALSE(client.sendMessage("2222222222222222", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "\xC2\xA0"));
	CHECK_FALSE(client.sendMessage("2222222222222222", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "\xE3\x80\x80"));
	CHECK_FALSE(client.sendMessage("2222222222222222", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "hello\xE2\x80\x83"));
	CHECK_FALSE(client.blockPlayer("2222222222222222", "\xE2\x80\x83"));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].method == "POST");
	CHECK(requests[0].path == "/v1/chat/2222222222222222/messages");
	CHECK(requests[0].body.find("\"client_id\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") != std::string::npos);
	CHECK(client.removeFriend("2222222222222222"));
	CHECK(client.unblockPlayer("2222222222222222"));
}

TEST_CASE("SOCIAL writes use contract routes and typed paging responses")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	auto next = [&](const std::string &method, const std::string &path) {
		auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].method == method);
		CHECK(requests[0].path == path);
		return requests[0];
	};
	auto answer = [&](const std::string &method, const std::string &path, const std::string &body, const std::string &response) {
		const auto request = next(method, path);
		if (!body.empty()) CHECK(request.body == body);
		client.deliver(request.id, 200, response, "");
	};

	OnlineLobbyClient::Privacy privacy{"friends", "friends", "nobody", "nobody"};
	REQUIRE(client.setPrivacy(privacy));
	answer("PUT", "/v1/me/privacy",
		R"({"v":1,"privacy":{"invites":"friends","challenges":"friends","requests":"nobody","presence":"nobody"}})", me);
	REQUIRE(client.setPresence("away", true, true));
	answer("POST", "/v1/me/status", R"({"v":1,"state":"away","hidden":true})", R"({"v":1,"ok":true})");
	CHECK(client.social().me.hidden);
	REQUIRE(client.setPresence("online", true, false));
	auto presenceRequest = next("POST", "/v1/me/status");
	CHECK(presenceRequest.body == R"({"v":1,"state":"online","hidden":false})");
	client.deliver(presenceRequest.id, 429, R"({"v":1,"error":"rate_limited"})", "");
	CHECK(client.social().me.hidden);
	client.takeSocialEvents();
	REQUIRE(client.requestFriendCode("ABCD-EFGH-JK"));
	answer("POST", "/v1/friends/requests", R"({"v":1,"code":"ABCD-EFGH-JK"})",
		R"({"v":1,"request":{"id":"3333333333333333","from":{"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self"},"to":{"pid":"2222222222222222","code":"BCDE-FGHJ-KM","nick":"Peer"},"at":10}})");
	REQUIRE(client.requestFriend("2222222222222222"));
	answer("POST", "/v1/friends/requests", R"({"v":1,"pid":"2222222222222222"})",
		R"({"v":1,"request":{"id":"3333333333333333","from":{"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Self"},"to":{"pid":"2222222222222222","code":"BCDE-FGHJ-KM","nick":"Peer"},"at":10}})");
	REQUIRE(client.acceptFriend("3333333333333333"));
	auto request = next("POST", "/v1/friends/requests/3333333333333333/accept");
	CHECK(request.body == R"({"v":1})");
	client.deliver(request.id, 200, R"({"v":1,"ok":true})", "");
	answer("GET", "/v1/friends", "", R"({"v":1,"friends":[]})");
	REQUIRE(client.declineFriend("3333333333333333"));
	answer("POST", "/v1/friends/requests/3333333333333333/decline", R"({"v":1})", R"({"v":1,"ok":true})");
	REQUIRE(client.removeFriend("2222222222222222"));
	answer("DELETE", "/v1/friends/2222222222222222", "", R"({"v":1,"ok":true})");
	answer("GET", "/v1/friends", "", R"({"v":1,"friends":[]})");
	REQUIRE(client.blockPlayer("2222222222222222", "not now"));
	answer("POST", "/v1/blocks", R"({"v":1,"pid":"2222222222222222","note":"not now"})", R"({"v":1,"ok":true})");
	answer("GET", "/v1/blocks?limit=50", "", R"({"v":1,"blocks":[],"next":null})");
	REQUIRE(client.unblockPlayer("2222222222222222"));
	answer("DELETE", "/v1/blocks/2222222222222222", "", R"({"v":1,"ok":true})");
	answer("GET", "/v1/blocks?limit=50", "", R"({"v":1,"blocks":[],"next":null})");
	REQUIRE(client.fetchBlocks("2222222222222222"));
	answer("GET", "/v1/blocks?limit=50&after=2222222222222222", "", R"({"v":1,"blocks":[],"next":null})");
	REQUIRE(client.fetchThreads("2222222222222222"));
	answer("GET", "/v1/chat?limit=50&after=2222222222222222", "", R"({"v":1,"threads":[],"next":null})");
	REQUIRE(client.fetchMessages("2222222222222222", 4));
	answer("GET", "/v1/chat/2222222222222222/messages?after=4&limit=50", "",
		R"({"v":1,"messages":[],"next":null,"last_id":0,"read_id":0})");
	REQUIRE(client.markRead("2222222222222222", 0));
	answer("POST", "/v1/chat/2222222222222222/read", R"({"v":1,"through":0})", R"({"v":1,"read_id":0})");
	REQUIRE(client.acceptInvite("3333333333333333"));
	request = next("POST", "/v1/invites/3333333333333333/accept");
	CHECK(request.body == "{\"v\":1,\"profile\":\"" + socialOptions().profileDigest + "\",\"mode\":\"normal\",\"platform\":\"linux\"," + kTestBuild + "}");
	client.deliver(request.id, 400, R"({"v":1,"error":"unavailable","message":"safe refusal"})", "");
	const auto events = client.takeSocialEvents();
	REQUIRE_FALSE(events.empty());
	CHECK(events.back().code == "unavailable");
	CHECK(events.back().text == "Social request refused: unavailable.");
}

TEST_CASE("SOCIAL refusal codes whitelist server errors and retain safe names")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	client.takeSocialEvents();
	REQUIRE(client.fetchRecent());
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 400,
		R"({"v":1,"error":"raw server detail bearer-secret","message":"private response text"})", "");
	auto events = client.takeSocialEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].code == "protocol");
	CHECK(events[0].text == "Social request refused: protocol.");
	CHECK(client.lastSocialError() == "protocol");
	CHECK(events[0].text.find("bearer-secret") == std::string::npos);

	REQUIRE(client.fetchRecent());
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 409, R"({"v":1,"error":"profile_mismatch"})", "");
	events = client.takeSocialEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].code == "profile_mismatch");
	CHECK(events[0].text.find("profile_mismatch") != std::string::npos);
	CHECK(client.lastSocialError() == "profile_mismatch");
}

TEST_CASE("SOCIAL confirmed block mutations refresh the authoritative block cache")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	const std::string peer = "2222222222222222";
	const std::string first = "{" + friendPlayer.substr(1, friendPlayer.size() - 2) +
		R"(,"at":10,"note":"First private note"})";
	const std::string second = R"({"pid":"3333333333333333","code":"CDEF-GHJK-MN","nick":"Other","at":11,"note":"Keep this note"})";
	auto deliverBlocks = [&](const std::string &rows) {
		auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].method == "GET");
		CHECK(requests[0].path == "/v1/blocks?limit=50");
		client.deliver(requests[0].id, 200, "{\"v\":1,\"blocks\":[" + rows + "],\"next\":null}", "");
	};
	REQUIRE(client.fetchBlocks());
	deliverBlocks(first + "," + second);
	REQUIRE(client.social().blocks.size() == 2);
	REQUIRE(client.unblockPlayer(peer));
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 400, R"({"v":1,"error":"bad_field"})", "");
	CHECK(client.social().blocks.size() == 2);
	CHECK(client.takeRequests().empty());
	REQUIRE(client.unblockPlayer(peer));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"ok":true})", "");
	deliverBlocks(second);
	REQUIRE(client.social().blocks.size() == 1);
	CHECK(client.social().blocks[0].player.pid == "3333333333333333");
	CHECK(client.social().blocks[0].note == "Keep this note");
	REQUIRE(client.blockPlayer(peer, "First private note"));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"ok":true})", "");
	deliverBlocks(first + "," + second);
	REQUIRE(client.social().blocks.size() == 2);
	CHECK(client.social().blocks[0].player.pid == peer);
	CHECK(client.social().blocks[0].note == "First private note");
}

TEST_CASE("SOCIAL long history keeps its paging cursor across live tail refreshes")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	const std::string peer = "2222222222222222";
	auto deliverPage = [&](int after, int last, int end) {
		REQUIRE(client.fetchMessages(peer, after));
		auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		std::string messages;
		for (int id = after + 1; id <= end; ++id)
		{
			if (!messages.empty()) messages += ",";
			messages += "{\"id\":" + std::to_string(id) +
				",\"from\":\"1111111111111111\",\"to\":\"2222222222222222\",\"text\":\"ORD-" +
				std::to_string(id) + "\",\"at\":10,\"client_id\":\"" + std::string(28, '0') +
				std::to_string(1000 + id) + "\"}";
		}
		client.deliver(requests[0].id, 200, "{\"v\":1,\"messages\":[" + messages +
			"],\"next\":" + (end < last ? std::to_string(end) : "null") +
			",\"last_id\":" + std::to_string(last) + ",\"read_id\":0}", "");
	};
	SUBCASE("tail arrives before history")
	{
		deliverPage(75, 125, 125);
		REQUIRE(client.social().conversations.count(peer) == 1);
		CHECK(client.social().conversations.at(peer).hasNext);
		CHECK(client.social().conversations.at(peer).next == 0);
	}
	deliverPage(0, 125, 50);
	deliverPage(75, 125, 125);
	const auto &conversation = client.social().conversations.at(peer);
	CHECK(conversation.messages.size() == 100);
	CHECK(conversation.hasNext);
	CHECK(conversation.next == 50);
	deliverPage(50, 125, 100);
	CHECK(conversation.messages.size() == 125);
	CHECK(conversation.hasNext);
	CHECK(conversation.next == 100);
	deliverPage(100, 125, 125);
	CHECK(conversation.messages.size() == 125);
	CHECK_FALSE(conversation.hasNext);
	CHECK(conversation.next == 0);
	for (int id = 1; id <= 125; ++id)
	{
		CHECK(conversation.messages[id - 1].id == static_cast<std::uint64_t>(id));
		CHECK(conversation.messages[id - 1].text == "ORD-" + std::to_string(id));
	}
	deliverPage(0, 125, 50);
	CHECK_FALSE(conversation.hasNext);
	CHECK(conversation.messages.size() == 125);
}

TEST_CASE("SOCIAL malformed chat replies preserve conversation state")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	const std::string peer = "2222222222222222";
	const std::string self = "1111111111111111";
	const std::string clientId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const std::string validMessage = R"({"id":1,"from":"1111111111111111","to":"2222222222222222","text":"first","at":10,"client_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
	REQUIRE(client.fetchMessages(peer, 0));
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200,
		"{\"v\":1,\"messages\":[" + validMessage + "],\"next\":null,\"last_id\":1,\"read_id\":0}", "");
	REQUIRE(client.social().conversations.count(peer) == 1);
	CHECK(client.social().conversations.at(peer).messages.size() == 1);

	auto rejectMessages = [&](std::uint64_t after, const std::string &response) {
		REQUIRE(client.fetchMessages(peer, after));
		auto pending = client.takeRequests();
		REQUIRE(pending.size() == 1);
		client.deliver(pending[0].id, 200, response, "");
		const auto &conversation = client.social().conversations.at(peer);
		CHECK(conversation.messages.size() == 1);
		CHECK(conversation.lastId == 1);
		CHECK(conversation.messages[0].text == "first");
	};
	const std::string wrongPeer = R"({"id":2,"from":"3333333333333333","to":"2222222222222222","text":"wrong peer","at":11,"client_id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
	rejectMessages(1, "{\"v\":1,\"messages\":[" + wrongPeer + "],\"next\":null,\"last_id\":2,\"read_id\":0}");
	const std::string missingText = R"({"id":2,"from":"1111111111111111","to":"2222222222222222","at":11,"client_id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
	rejectMessages(1, "{\"v\":1,\"messages\":[" + missingText + "],\"next\":null,\"last_id\":2,\"read_id\":0}");
	const std::string nextMessage = R"({"id":2,"from":"1111111111111111","to":"2222222222222222","text":"second","at":11,"client_id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
	rejectMessages(1, "{\"v\":1,\"messages\":[" + nextMessage + "],\"next\":3,\"last_id\":2,\"read_id\":0}");
	rejectMessages(1, "{\"v\":1,\"messages\":[" + nextMessage + "],\"next\":2,\"last_id\":1,\"read_id\":0}");

	REQUIRE(client.sendMessage(peer, clientId, "pending"));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	const std::string mismatchedText = R"({"id":2,"from":"1111111111111111","to":"2222222222222222","text":"other","at":12,"client_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
	client.deliver(requests[0].id, 200, "{\"v\":1,\"message\":" + mismatchedText + "}", "");
	CHECK(client.social().conversations.at(peer).messages.size() == 1);
	CHECK(client.social().conversations.at(peer).lastId == 1);

	REQUIRE(client.sendMessage(peer, clientId, "pending"));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	const std::string mismatchedClient = R"({"id":2,"from":"1111111111111111","to":"2222222222222222","text":"pending","at":13,"client_id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
	client.deliver(requests[0].id, 200, "{\"v\":1,\"message\":" + mismatchedClient + "}", "");
	CHECK(client.social().conversations.at(peer).messages.size() == 1);
	CHECK(client.social().conversations.at(peer).lastId == 1);

	REQUIRE(client.markRead(peer, 1));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"read_id":0})", "");
	CHECK(client.social().conversations.at(peer).readId == 0);
	CHECK(client.social().conversations.at(peer).messages[0].from == self);
}

TEST_CASE("SOCIAL reading older history accepts an already newer server read marker")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	const std::string peer = "2222222222222222";
	REQUIRE(client.fetchMessages(peer, 0));
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200,
		R"({"v":1,"messages":[{"id":1,"from":"1111111111111111","to":"2222222222222222","text":"old history","at":10,"client_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}],"next":1,"last_id":126,"read_id":126})", "");
	client.takeSocialEvents();
	REQUIRE(client.markRead(peer, 1));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"read_id":126})", "");
	const auto &conversation = client.social().conversations.at(peer);
	CHECK(conversation.readId == 126);
	CHECK(conversation.lastId == 126);
	CHECK(conversation.hasNext);
	CHECK(conversation.next == 1);
	for (const auto &event : client.takeSocialEvents())
	{
		CHECK(event.kind != OnlineLobbyClient::SocialEvent::Error);
	}
	REQUIRE(client.markRead(peer, 1));
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 200, R"({"v":1,"read_id":127})", "");
	CHECK(conversation.readId == 127);
	CHECK(conversation.lastId == 127);
	CHECK(conversation.next == 1);
	for (const auto &event : client.takeSocialEvents())
	{
		CHECK(event.kind != OnlineLobbyClient::SocialEvent::Error);
	}
}

TEST_CASE("SOCIAL accept after leave waits for a successful leave acknowledgement")
{
	const std::string sourceId = "4444444444444444";
	const std::string inviteId = "5555555555555555";
	auto joinSourceRoom = [&](OnlineLobbyClient &client) {
		loadInitialSocialViews(client);
	client.joinGame(sourceId, "password");
		const auto requests = client.takeRequests();
		const auto join = findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary);
		// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
		REQUIRE(join.id != 0);
		client.deliver(join.id, 200, "{\"v\":1,\"game\":" + sourceGameView(sourceId) + "}", "");
		REQUIRE(client.inGame());
	};

	OnlineLobbyClient successful(socialOptions());
	joinSourceRoom(successful);
	CHECK_FALSE(successful.acceptInvite(inviteId));
	CHECK(successful.lastSocialError() == "in_game");
	REQUIRE(successful.acceptInviteAfterLeave(inviteId));
	CHECK_FALSE(successful.inGame());
	auto requests = successful.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/games/" + sourceId + "/leave");
	CHECK(requests[0].path.find("/accept") == std::string::npos);
	successful.deliver(requests[0].id, 200, R"({"v":1,"left":true})", "");
	requests = successful.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/invites/5555555555555555/accept");
	CHECK(requests[0].body == "{\"v\":1,\"profile\":\"" + socialOptions().profileDigest + "\",\"mode\":\"normal\",\"platform\":\"linux\"," + kTestBuild + "}");

	auto rejectedLeaveDoesNotAccept = [&](int status, const std::string &body, const std::string &transportError) {
		OnlineLobbyClient client(socialOptions());
		joinSourceRoom(client);
		REQUIRE(client.acceptInviteAfterLeave(inviteId));
		auto pending = client.takeRequests();
		REQUIRE(pending.size() == 1);
		client.deliver(pending[0].id, status, body, transportError);
		CHECK(client.takeRequests().empty());
		CHECK_FALSE(client.takeSocialEvents().empty());
	};
	rejectedLeaveDoesNotAccept(409, R"({"v":1,"error":"not_in_game","message":"leave refused"})", "");
	rejectedLeaveDoesNotAccept(200, R"({"v":1,"left":"true"})", "");
	rejectedLeaveDoesNotAccept(0, "", "connection failed");
}

TEST_CASE("SOCIAL successful typed invite acceptance preserves the joined game view")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	client.takeEvents();
	client.takeSocialEvents();
	const std::string inviteId = "5555555555555555";
	const std::string gameId = "6666666666666666";
	REQUIRE(client.acceptInvite(inviteId));
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/invites/" + inviteId + "/accept");
	CHECK(requests[0].body == "{\"v\":1,\"profile\":\"" + socialOptions().profileDigest + "\",\"mode\":\"normal\",\"platform\":\"linux\"," + kTestBuild + "}");
	client.deliver(requests[0].id, 200,
		"{\"v\":1,\"id\":\"" + gameId + "\",\"slot\":0,\"game\":" + sourceGameView(gameId) + "}", "");

	REQUIRE(client.inGame());
	CHECK(client.gameId() == gameId);
	CHECK(client.localSlot() == 0);
	CHECK(client.gameInfo().slots[0].state == SLOT_PLAYER);
	CHECK(client.slotViews().size() == MAX_SLOTS);
	CHECK(client.slotViews()[0].you);
	const auto events = client.takeEvents();
	CHECK(std::any_of(events.begin(), events.end(), [](const OnlineLobbyClient::Event &event) {
		return event.kind == OnlineLobbyClient::Event::GameJoined;
	}));
}

TEST_CASE("SOCIAL invite acceptance and ordinary join use the recipient's own mode and platform")
{
	for (const auto profile : { SimulationProfile::Faithful, SimulationProfile::Uber })
	{
		auto options = socialOptions();
		options.profile = profile;
		options.platform = PlatformKind::MacOS;
		OnlineLobbyClient client(options);
		loadInitialSocialViews(client);
		const std::string mode = profile == SimulationProfile::Uber ? "uber" : "normal";
		// lane JOINVER-1: the join identity tells the client's build too
		const std::string fields = "\"profile\":\"" + options.profileDigest + "\",\"mode\":\"" + mode + "\",\"platform\":\"macos\"," +
			"\"build\":{\"version\":\"test\",\"platform\":\"macos\"}";
		const std::string gameId = "6666666666666666";
		REQUIRE(client.acceptInvite("5555555555555555"));
		auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].body == "{\"v\":1," + fields + "}");
		client.deliver(requests[0].id, 200,
			"{\"v\":1,\"id\":\"" + gameId + "\",\"slot\":0,\"game\":" + sourceGameView(gameId, mode) + "}", "");
		REQUIRE(client.inGame());
		CHECK(client.gameInfo().simulationProfile == profile);
		client.joinGame(gameId, "");
		requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].path == "/v1/games/" + gameId + "/join");
		CHECK(requests[0].body == "{" + fields + "}");
	}
}

TEST_CASE("SOCIAL malformed presence acknowledgements preserve hidden state")
{
	for (const std::string body : { R"({"v":1})", R"({"v":1,"ok":false})", R"({"v":1,"ok":"true"})" })
	{
		OnlineLobbyClient client(socialOptions());
		loadInitialSocialViews(client);
		client.takeSocialEvents();
		REQUIRE(client.setPresence("away", true, true));
		const auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].path == "/v1/me/status");
		client.deliver(requests[0].id, 200, body, "");
		CHECK_FALSE(client.social().me.hidden);
		const auto events = client.takeSocialEvents();
		REQUIRE(events.size() == 1);
		CHECK(events[0].kind == OnlineLobbyClient::SocialEvent::Error);
		CHECK(events[0].code == "protocol");
	}
}

TEST_CASE("SOCIAL 429 retry hints pause queued requests without leaking response text")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);
	client.takeSocialEvents();
	client.takeEvents();
	REQUIRE(client.fetchRecent());
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 429,
		R"({"v":1,"error":"rate_limited","retry_after":10,"message":"session must stay private"})", "");
	const auto events = client.takeSocialEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OnlineLobbyClient::SocialEvent::Error);
	CHECK(events[0].code == "rate_limited");
	CHECK(events[0].text.find("session") == std::string::npos);
	CHECK(client.takeEvents().empty());

	REQUIRE(client.fetchRecent());
	CHECK(client.takeRequests().empty());
	client.update(9999);
	CHECK(client.takeRequests().empty());
	client.update(10000);
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/recent");
}

TEST_CASE("SOCIAL backoff lets ordinary lobby work pass and terminal errors discard old session state")
{
	OnlineLobbyClient client(socialOptions());
	loadInitialSocialViews(client);

	REQUIRE(client.fetchRecent());
	auto requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	client.deliver(requests[0].id, 429, R"({"v":1,"error":"rate_limited","retry_after":10})", "");

	REQUIRE(client.setPresence("away"));
	client.refreshList();
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].channel == OnlineLobbyClient::HttpRequest::Ordinary);
	CHECK(requests[0].path == "/v1/games");
	client.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	CHECK(client.takeRequests().empty());
	client.update(10000);
	requests = client.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/me/status");

	OnlineLobbyClient stale(socialOptions());
	signInSocial(stale);
	requests = stale.takeRequests();
	const auto inbox = findChannel(requests, OnlineLobbyClient::HttpRequest::Social);
	stale.deliver(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).id, 200, me, "");
	requests = stale.takeRequests();
	stale.deliver(requests[0].id, 200, R"({"v":1,"friends":[]})", "");
	requests = stale.takeRequests();
	stale.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	REQUIRE(stale.generateRecovery());
	requests = stale.takeRequests();
	REQUIRE(requests.size() == 1);
	// ONLINEMENU-1 (PR #8 review finding 2): the request names the install the session was issued to (the lobby's proof for a recovery code)
	CHECK(requests[0].body == R"({"v":1,"install":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})");
	stale.deliver(requests[0].id, 200, R"({"v":1,"code":"0123456789ABCDEFGH"})", "");
	stale.refreshList();
	requests = stale.takeRequests();
	REQUIRE(requests.size() == 1);
	stale.deliver(requests[0].id, 401, R"({"v":1,"error":"signed_out","message":"session detail"})", "");
	CHECK_FALSE(stale.social().available);
	CHECK(stale.social().me.pid.empty());
	CHECK(stale.takeRecoveryCode().empty());
	auto events = stale.takeSocialEvents();
	REQUIRE(events.size() == 1);
	CHECK(events[0].kind == OnlineLobbyClient::SocialEvent::Error);
	CHECK(events[0].code == "signed_out");
	stale.deliver(inbox.id, 200, emptyInbox, "");
	CHECK(stale.social().seq.empty());
	CHECK(stale.social().requests.empty());
	CHECK(stale.takeSocialEvents().empty());
	CHECK_FALSE(stale.generateRecovery());

	stale.signInGuest(u"Fresh");
	answerFeatures(stale);
	requests = stale.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/auth/guest");
	stale.deliver(requests[0].id, 200,
		R"({"v":1,"token":"new-session","account":{"id":"guest:2","name":"Fresh","kind":"guest","pid":"4444444444444444"}})", "");
	requests = stale.takeRequests();
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(findChannel(requests, OnlineLobbyClient::HttpRequest::Social).id != 0);
	CHECK(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).path == "/v1/me");

	stale.useSession("fresh.session.token");
	CHECK_FALSE(stale.social().available);
	CHECK(stale.social().me.pid.empty());
	answerFeatures(stale); // ONLINEMENU-1: a kept session asks the lobby's features first too
	requests = stale.takeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].path == "/v1/mine");
	stale.deliver(requests[0].id, 200, R"({"v":1,"games":[]})", "");
	requests = stale.takeRequests();
	// tolerance: generated request IDs are opaque; only nonzero allocation matters here.
	REQUIRE(findChannel(requests, OnlineLobbyClient::HttpRequest::Social).id != 0);
	CHECK(findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary).path == "/v1/me");
}

TEST_CASE("onlinemenu1 SOCIAL a lobby without the social service (the deployed lobby before the social routes) gets no social request")
{
	// BATCH-P3 (the coordinator: the nightly plays against the live lobby): its /v1/health names no features; an unreadable or failed answer
	// counts the same; a lobby that lists the feature but answers not_found for /v1/me is stopped too, and never polled once a second
	for (const std::string health : { std::string(R"({"v":1,"ok":true,"games":1,"relays":[]})"), std::string("not json"),
			 std::string(R"({"v":1,"ok":true,"features":"social"})") })
	{
		OnlineLobbyClient client(socialOptions());
		client.signInGuest(u"Self");
		answerFeatures(client, health);
		auto requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0].path == "/v1/auth/guest");
		client.deliver(requests[0].id, 200, R"({"v":1,"token":"session","account":{"id":"guest:1","name":"Self","kind":"guest","pid":"1111111111111111"}})", "");
		CHECK(client.signedIn());
		CHECK(client.social().unsupported);
		CHECK_FALSE(client.social().available);
		for (int tick = 0; tick < 50; ++tick)
		{
			client.update(100u * (std::uint64_t)(tick + 1));
			for (const auto &r : client.takeRequests())
			{
				const bool socialPath = r.channel == OnlineLobbyClient::HttpRequest::Social || r.path.rfind("/v1/me", 0) == 0 ||
					r.path.rfind("/v1/inbox", 0) == 0 || r.path.rfind("/v1/friends", 0) == 0 || r.path.rfind("/v1/recent", 0) == 0;
				CHECK_FALSE_MESSAGE(socialPath, r.path);
				client.deliver(r.id, 200, R"({"v":1,"games":[]})", "");
			}
		}
		CHECK(client.takeSocialEvents().empty()); // no "Social request refused" line for the screens
		CHECK_FALSE(client.requestFriend("2222222222222222"));
	}
	// a kept session (a restarted game) on that lobby: signed in for its games, no social
	OnlineLobbyClient kept(socialOptions());
	kept.useSession("kept.session.token");
	answerFeatures(kept, R"({"v":1,"ok":true,"games":1,"relays":[]})");
	auto keptRequests = kept.takeRequests();
	REQUIRE(keptRequests.size() == 1);
	REQUIRE(keptRequests[0].path == "/v1/mine");
	kept.deliver(keptRequests[0].id, 200, R"({"v":1,"games":[]})", "");
	CHECK(kept.signedIn());
	CHECK(kept.social().unsupported);
	keptRequests = kept.takeRequests();
	CHECK(findChannel(keptRequests, OnlineLobbyClient::HttpRequest::Social).id == 0);
	CHECK(findChannel(keptRequests, OnlineLobbyClient::HttpRequest::Ordinary).path.empty());
	OnlineLobbyClient listed(socialOptions());
	signInSocial(listed);
	auto requests = listed.takeRequests();
	const auto meRequest = findChannel(requests, OnlineLobbyClient::HttpRequest::Ordinary);
	REQUIRE(meRequest.path == "/v1/me");
	listed.deliver(meRequest.id, 404, R"({"v":1,"error":"not_found","message":"unknown path"})", "");
	CHECK(listed.social().unsupported);
	CHECK(listed.takeSocialEvents().empty());
	int social = 0;
	for (int tick = 0; tick < 50; ++tick)
	{
		listed.update(100u * (std::uint64_t)(tick + 1));
		for (const auto &r : listed.takeRequests())
		{
			if (r.channel == OnlineLobbyClient::HttpRequest::Social || r.path.rfind("/v1/me", 0) == 0) ++social;
			listed.deliver(r.id, 200, R"({"v":1,"games":[]})", "");
		}
	}
	CHECK(social == 0);
}
