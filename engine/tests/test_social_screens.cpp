// OpenBFME unit tests. GPL-3.0.
#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "AptRetail.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptScreens/AptMessenger.h"
#include "GameClient/GUI/AptScreens/AptOnlineShell.h"
#include "GameClient/GUI/AptScreens/AptScreenFactories.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "StartShellFx.h"
#include <algorithm>
#include <functional>

TEST_CASE("SOCIAL Messenger providers validate tabs and close without destroying a callback")
{
	apttest::MemorySource source;
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	WindowManager windows(source, services);
	Shell shell(windows, factories, services, environment);
	{
		AptMessenger messenger(windows, shell, environment);
		CHECK(windows.hasScreenRef("AptMessenger::InitGadgets"));
		std::string position;
		CHECK(windows.getExtern("MessengerX", position) == AptExternResult::Value);
		CHECK(position.empty());
		CHECK(windows.getExtern("MessengerY", position) == AptExternResult::Value);
		CHECK(position.empty());
		CHECK(windows.setExtern("MessengerActiveTab", "1"));
		CHECK(messenger.activeTab() == 1);
		CHECK(windows.setExtern("MessengerActiveTab", "2"));
		CHECK(messenger.activeTab() == 1);
		int closed = 0;
		messenger.onClose = [&]() { ++closed; };
		CHECK(windows.invokeCallback("AptMessenger::CloseScreen", ""));
		CHECK(messenger.closing());
		CHECK(windows.invokeCallback("AptMessenger::OnClosed", ""));
		CHECK(closed == 1);
	}
	CHECK_FALSE(windows.hasProvider("MessengerActiveTab"));
	CHECK_FALSE(windows.hasScreenRef("AptMessenger::InitGadgets"));
}

TEST_CASE("SOCIAL invite questions cancel only their own box and ignore late button callbacks")
{
	apttest::MemorySource source;
	for (const char *name : { "AptLevel0", "OnlineShell", "GuiFX" })
	{
		apttest::TestMovie movie;
		movie.setRootFrames({ {} });
		source.add(name, movie);
	}
	RecordingShellServices services;
	ShellEnvironment environment;
	environment.services = &services;
	AptScreenFactoryTable factories;
	registerAptScreenFactories(factories);
	WindowManager windows(source, services);
	Shell shell(windows, factories, services, environment);
	windows.setShell(&shell);
	windows.init();
	shell.push("OnlineShell.apt");
	auto *screen = dynamic_cast<AptOnlineShell *>(shell.top());
	REQUIRE(screen);
	REQUIRE(screen->askYesNo("invite-1", u"Invite", u"Join?"));
	auto *box = screen->messageBox();
	REQUIRE(box);
	const auto yes = "_level" + std::to_string(box->level()) + ".MessageBox_OnButtonYes";
	screen->cancelAsk("another-key");
	CHECK(screen->asking("invite-1"));
	screen->cancelAsk("invite-1");
	CHECK_FALSE(screen->asking("invite-1"));
	CHECK(box->type() == AptMessageBox::TYPE_NONE);
	CHECK(screen->takeAnswers().empty());
	REQUIRE(windows.invokeCallback(yes, ""));
	CHECK(screen->takeAnswers().empty());
	REQUIRE(screen->askYesNo("invite-2", u"Invite", u"Join?"));
	box->show(AptMessageBox::TYPE_OK, u"Room closed", u"The host left.");
	screen->cancelAsk("invite-2");
	CHECK(box->type() == AptMessageBox::TYPE_OK);
	CHECK(box->title() == u"Room closed");
	CHECK(screen->takeAnswers().empty());
	box->hide();
	REQUIRE(screen->askYesNo("invite-3", u"Invite", u"Join?"));
	REQUIRE(windows.invokeCallback(yes, ""));
	CHECK(screen->takeAnswers() == std::vector<std::string>{ "invite-3|yes" });
	REQUIRE(windows.invokeCallback(yes, ""));
	CHECK(screen->takeAnswers().empty());
}

TEST_CASE("SOCIAL retail Messenger loads both player tabs and routes actions by public id")
{
	OPENBFME_REQUIRE_RETAIL(retail);
	AptArchiveFileSource source(retail.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(retail.fs, skins, &error), error);
	WindowManager windows(source, services);
	AptGadgetLayer layer(windows, source, skins);
	layer.registerComponents();
	Shell shell(windows, factories, services, environment);
	windows.setShell(&shell);
	windows.init();
	AptMessenger messenger(windows, shell, environment);
	messenger.setRows(0, {{"1111111111111111", u"Friend", u"Online", false}, {"2222222222222222", u"Request", u"Pending", true}});
	messenger.setRows(1, {{"3333333333333333", u"Room player", u"In a room", false}});
	std::vector<AptMessenger::Action> actions;
	messenger.onAction = [&](const AptMessenger::Action &a) { actions.push_back(a); };
	messenger.runInit();
	for (int i = 0; i < 90; ++i) windows.update(33);
	REQUIRE(windows.isAptWindowLoaded(messenger.level()));
	auto playerList = [&](int tab) -> GameWindow * {
		for (auto *window : layer.gadgets().allWindows())
			if (window->aptInstanceName() == "Messenger::PlayersTab_" + std::to_string(tab)) return window;
		return nullptr;
	};
	REQUIRE(playerList(0));
	GadgetListBoxSetSelected(playerList(0), 1);
	REQUIRE(windows.setExtern("MessengerActiveTab", "0"));
	CHECK(messenger.selectedPlayer() == "2222222222222222");
	REQUIRE(windows.invokeCallback("AptMessenger::OnBttn_1", ""));
	const auto accept = std::find_if(actions.begin(), actions.end(), [](const AptMessenger::Action &action) { return action.kind == AptMessenger::Action::AcceptFriend; });
	REQUIRE(accept != actions.end());
	CHECK(accept->player == "2222222222222222");
	messenger.setRows(0, {{"2222222222222222", u"Request renamed", u"Pending", true}});
	CHECK(messenger.selectedPlayer() == "2222222222222222");
	messenger.setRows(0, {});
	CHECK(messenger.selectedPlayer().empty());
	windows.invokeCallback("AptMessenger::OnBttn_0", "");
	CHECK(std::count_if(actions.begin(), actions.end(), [](const AptMessenger::Action &action) { return action.kind == AptMessenger::Action::AcceptFriend; }) == 1);
	GameWindow *entry = nullptr;
	for (auto *window : layer.gadgets().allWindows())
		if (window->aptInstanceName() == "Messenger::ChatEntry") entry = window;
	REQUIRE(entry);
	CHECK(GadgetTextEntryGetText(entry).empty());
	GadgetTextEntrySetText(entry, u"pending draft");
	messenger.acknowledgeMessage(u"older draft");
	CHECK(GadgetTextEntryGetText(entry) == u"pending draft");
	messenger.acknowledgeMessage(u"pending draft");
	CHECK(GadgetTextEntryGetText(entry).empty());
}

namespace
{
const std::string shellSelf = R"({"v":1,"pid":"1111111111111111","code":"ABCD-EFGH-JK","nick":"Host","privacy":{"invites":"friends_or_room","challenges":"friends_or_room","requests":"anyone","presence":"friends"},"hidden":false})";
const std::string shellInbox = R"({"v":1,"seq":"aaaaaaaaaaaaaaaa:1","reset":false,"presence":[],"requests":[],"invites":[],"notices":[],"chat":[],"stats":{"v":1,"players":1,"games_open":0,"games_running":0,"as_of":1700000000},"wait":25})";

OnlineLobbyClient::Options shellSocialOptions(int factions, int colors)
{
	OnlineLobbyClient::Options options;
	options.profileDigest = "0123456789abcdef0123456789abcdef";
	options.installId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	options.clientPlatform = "linux";
	options.clientVersion = "test";
	options.playerTemplateCount = factions;
	options.colorCount = colors;
	options.enableSocial = true;
	options.pollMs = 100;
	return options;
}

std::string shellGameView(const std::string &id, const std::string &map, bool privateRoom)
{
	std::string slots = "[";
	for (int slot = 0; slot < LEGACY_MAX_SLOTS; ++slot) // an eight-start room: eight records (ONLINE16-FIX)
	{
		if (slot) slots += ",";
		const std::string state = slot == 0 ? "player" : (privateRoom && slot > 1 ? "closed" : "open");
		const bool you = slot == 0;
		slots += "{\"slot\":" + std::to_string(slot) + ",\"state\":" + OnlineLobbyClient::jsonQuote(state) +
			",\"name\":" + (you ? "\"Host\"" : "null") +
			",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1,\"ready\":false,\"you\":" +
			(you ? "true" : "false") + "}";
	}
	slots += "]";
	return "{\"id\":" + OnlineLobbyClient::jsonQuote(id) +
		",\"name\":\"Challenge room\",\"state\":\"open\",\"private\":true,\"hostSlot\":0,\"seq\":1,"
		"\"setup\":{\"map\":" + OnlineLobbyClient::jsonQuote(map) +
		",\"startingCash\":1000,\"superweaponRestriction\":0,\"legendaryHeroes\":1,\"rules\":null},"
		"\"slots\":" + slots + "}";
}
}

TEST_CASE("SOCIAL AptOnlineShell leaves before creating a private challenge room and sending the same-session challenge")
{
	OPENBFME_REQUIRE_START(shared);
	const std::string sourceId = "4444444444444444";
	const std::string challengeId = "6666666666666666";
	const std::string targetPid = "2222222222222222";
	const std::string password = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

	auto runChallenge = [&](bool leaveSucceeds, bool falseSuccess = false) {
		OnlineLobbyClient client(shellSocialOptions(shared->world->playerTemplates().getPlayerTemplateCount(),
			(int)shared->settings.multiplayerColors.size()));
		starttest::ShellFx fx(*shared, 0x51A1);
		OnlineShellContext context;
		context.client = &client;
		fx.environment.online = &context;
		fx.environment.services = &fx.services;
		fx.shell->push("OnlineShell.apt");
		REQUIRE(fx.shell->errors().empty());
		auto *screen = dynamic_cast<AptOnlineShell *>(fx.shell->top());
		REQUIRE(screen);

		client.signInGuest(u"Host");
		auto requests = client.takeRequests();
		// ONLINEMENU-1 (BATCH-P3): the lobby's features first; this lobby has the social service
		REQUIRE(requests.size() == 1);
		REQUIRE(requests[0].path == "/v1/health");
		client.deliver(requests[0].id, 200, R"({"v":1,"ok":true,"games":0,"relays":[],"features":["social"]})", "");
		requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		client.deliver(requests[0].id, 200,
			R"({"v":1,"token":"same-session-token","account":{"id":"guest:1","name":"Host","kind":"guest","pid":"1111111111111111"}})", "");
		int loadedViews = 0;
		for (int step = 0; step < 8 && loadedViews < 3; ++step)
		{
			requests = client.takeRequests();
			REQUIRE_FALSE(requests.empty());
			for (const auto &request : requests)
			{
				if (request.channel == OnlineLobbyClient::HttpRequest::Social)
					client.deliver(request.id, 200, shellInbox, "");
				else if (request.path == "/v1/me")
				{
					client.deliver(request.id, 200, shellSelf, "");
					++loadedViews;
				}
				else if (request.path == "/v1/friends")
				{
					client.deliver(request.id, 200, R"({"v":1,"friends":[]})", "");
					++loadedViews;
				}
				else if (request.path == "/v1/recent")
				{
					client.deliver(request.id, 200, R"({"v":1,"games":[]})", "");
					++loadedViews;
				}
				else
					FAIL("unexpected initial social request " << request.path);
			}
		}
		REQUIRE(loadedViews == 3);
		for (int step = 0; step < 8; ++step)
		{
			requests = client.takeRequests();
			if (requests.empty())
			{
				break;
			}
			for (const auto &request : requests)
			{
				if (request.channel == OnlineLobbyClient::HttpRequest::Social)
					client.deliver(request.id, 200, shellInbox, "");
				else if (request.path == "/v1/friends")
					client.deliver(request.id, 200, R"({"v":1,"friends":[]})", "");
				else
					FAIL("unexpected queued social view " << request.path);
			}
		}
		client.takeEvents();
		client.takeSocialEvents();
		client.joinGame(sourceId, "source-password");
		requests = client.takeRequests();
		REQUIRE(requests.size() == 1);
		INFO("join request: " << requests[0].method << " " << requests[0].path);
		client.deliver(requests[0].id, 200,
			"{\"v\":1,\"game\":" + shellGameView(sourceId, "map", false) + "}", "");
		const auto joinEvents = client.takeEvents();
		for (const auto &event : joinEvents)
		{
			if (event.kind == OnlineLobbyClient::Event::Refused)
			{
				INFO("join refused: " << event.code << ": " << OnlineLobbyClient::utf16ToUtf8(event.text));
			}
		}
		REQUIRE(client.inGame());

		REQUIRE(screen->createChallenge(targetPid, "normal", password));
		CHECK_FALSE(client.inGame());
		std::vector<OnlineLobbyClient::HttpRequest> trace;
		std::size_t leaveAt = std::string::npos;
		std::size_t sendAt = std::string::npos;
		std::string sessionToken;
		for (int step = 0; step < 160 && sendAt == std::string::npos; ++step)
		{
			fx.tick(1);
			requests = client.takeRequests();
			for (const auto &request : requests)
			{
				const std::size_t at = trace.size();
				trace.push_back(request);
				if (request.path == "/v1/games/" + sourceId + "/leave")
				{
					leaveAt = at;
					sessionToken = request.token;
					if (leaveSucceeds)
						client.deliver(request.id, 200, R"({"v":1,"left":true})", "");
					else if (falseSuccess)
						client.deliver(request.id, 200, R"({"v":1,"left":false})", "");
					else
						client.deliver(request.id, 409, R"({"v":1,"error":"not_in_game","message":"leave refused"})", "");
				}
				else if (request.channel == OnlineLobbyClient::HttpRequest::Social)
					client.deliver(request.id, 200, shellInbox, "");
				else if (request.path == "/v1/games" && request.method == "POST")
				{
					CHECK(request.token == sessionToken);
					const std::size_t slotsAt = request.body.find("\"slots\":[");
					REQUIRE(slotsAt != std::string::npos);
					const std::size_t slotsEnd = request.body.find(']', slotsAt);
					REQUIRE(slotsEnd != std::string::npos);
					const std::string slots = request.body.substr(slotsAt, slotsEnd - slotsAt);
					CHECK(std::count(slots.begin(), slots.end(), '{') == 2);
					CHECK(slots.find("\"state\":\"player\"") != std::string::npos);
					CHECK(slots.find("\"state\":\"open\"") != std::string::npos);
					CHECK(slots.find("\"state\":\"closed\"") == std::string::npos);
					CHECK(request.body.find("\"password\":\"" + password + "\"") != std::string::npos);
					client.deliver(request.id, 200,
						"{\"v\":1,\"game\":" + shellGameView(challengeId, "map", true) + "}", "");
				}
				else if (request.path == "/v1/invites" && request.method == "POST")
				{
					sendAt = at;
					CHECK(request.token == sessionToken);
					CHECK(request.body.find("\"to\":\"" + targetPid + "\"") != std::string::npos);
					CHECK(request.body.find("\"kind\":\"challenge\"") != std::string::npos);
					CHECK(request.body.find("\"mode\":\"normal\"") != std::string::npos);
				}
				else if (request.method == "GET" && request.path.find("/v1/games") == 0)
					client.deliver(request.id, 200, R"({"v":1,"games":[]})", "");
				else if (request.method == "GET" && request.path.find("/v1/mine") == 0)
					client.deliver(request.id, 200, R"({"v":1,"games":[]})", "");
				else if (request.method == "GET" && request.path.find("/v1/") == 0)
					client.deliver(request.id, 200, R"({"v":1,"games":[]})", "");
				else
					FAIL("unexpected challenge request " << request.method << " " << request.path);
			}
			if (!leaveSucceeds && leaveAt != std::string::npos && step > 8 && !client.busy())
				break;
		}
		return trace;
	};

	const auto successful = runChallenge(true);
	const auto leave = std::find_if(successful.begin(), successful.end(), [&](const auto &request) {
		return request.path == "/v1/games/" + sourceId + "/leave";
	});
	const auto create = std::find_if(successful.begin(), successful.end(), [](const auto &request) {
		return request.path == "/v1/games" && request.method == "POST";
	});
	const auto send = std::find_if(successful.begin(), successful.end(), [](const auto &request) {
		return request.path == "/v1/invites" && request.method == "POST";
	});
	REQUIRE(leave != successful.end());
	REQUIRE(create != successful.end());
	REQUIRE(send != successful.end());
	// tolerance: strict ordering of captured requests, not a numerical-output tolerance.
	CHECK(leave < create);
	// tolerance: strict ordering of captured requests, not a numerical-output tolerance.
	CHECK(create < send);
	CHECK(leave->token == create->token);
	CHECK(create->token == send->token);
	CHECK(send->body.find("\"kind\":\"challenge\"") != std::string::npos);

	for (const auto &failedLeave : {runChallenge(false), runChallenge(false, true)})
	{
		CHECK(std::any_of(failedLeave.begin(), failedLeave.end(), [&](const auto &request) {
			return request.path == "/v1/games/" + sourceId + "/leave";
		}));
		CHECK(std::none_of(failedLeave.begin(), failedLeave.end(), [](const auto &request) {
			return (request.path == "/v1/games" && request.method == "POST") ||
				(request.path == "/v1/invites" && request.method == "POST");
		}));
	}
}

TEST_CASE("onlinemenu1 retail: the Messenger shows a friend's status in RotWK's words (Buddy:<status>, RW 0x9E63E6) and offers INVITE as RotWK does "
		  "(RW 0x9BD9A7: an online friend, not playing, while the player is in a staging room)")
{
	OPENBFME_REQUIRE_RETAIL(retail);
	AptArchiveFileSource source(retail.fs);
	RecordingShellServices services;
	ShellEnvironment environment;
	GameTextTableSource text;
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(retail.fs.readFile("data/lotr.str", bytes, &error), error);
		REQUIRE_MESSAGE(text.table.parse(bytes, &error), error);
	}
	environment.gameText = &text;
	OnlineLobbyClient::Options options = shellSocialOptions(8, 8);
	options.enableSocial = false;
	OnlineLobbyClient client(options);
	OnlineShellContext online;
	online.client = &client;
	environment.online = &online;
	AptScreenFactoryTable factories;
	GadgetSkinData skins;
	std::string error;
	REQUIRE_MESSAGE(loadGadgetSkinData(retail.fs, skins, &error), error);
	WindowManager windows(source, services);
	AptGadgetLayer layer(windows, source, skins);
	layer.registerComponents();
	Shell shell(windows, factories, services, environment);
	windows.setShell(&shell);
	windows.init();
	AptMessenger messenger(windows, shell, environment);
	// the presence states as the lobby sends them (SOCIAL-API: offline / online / in_room / matching / loading / playing / away) and a request
	messenger.setRows(0, { { "1111111111111111", u"Ann", u"in_room", false }, { "2222222222222222", u"Bea", u"offline", false },
							 { "3333333333333333", u"Cy", u"playing", false }, { "4444444444444444", u"Dee", u"online", false },
							 { "5555555555555555", u"Eve", u"", true }, { "6666666666666666", u"Fay", u"matching", false } });
	std::vector<AptMessenger::Action> actions;
	messenger.onAction = [&](const AptMessenger::Action &a) { actions.push_back(a); };
	messenger.runInit();
	for (int i = 0; i < 90; ++i) windows.update(33);
	REQUIRE(windows.isAptWindowLoaded(messenger.level()));
	GameWindow *friends = nullptr;
	for (auto *window : layer.gadgets().allWindows())
		if (window->aptInstanceName() == "Messenger::PlayersTab_0") friends = window;
	REQUIRE(friends);
	// ---- the rows: each friend's status is the retail text of its Buddy: label (read from the player's lotr.str, not from the code) ----
	auto retailText = [&](const char *label) {
		std::u16string out;
		REQUIRE_MESSAGE(text.fetch(label, out), label);
		return out;
	};
	REQUIRE(GadgetListBoxGetNumEntries(friends) == 6);
	CHECK(GadgetListBoxGetText(friends, 0, 0) == u"Ann - " + retailText("Buddy:Staging"));
	CHECK(GadgetListBoxGetText(friends, 1, 0) == u"Bea - " + retailText("Buddy:Offline"));
	CHECK(GadgetListBoxGetText(friends, 2, 0) == u"Cy - " + retailText("Buddy:Playing"));
	CHECK(GadgetListBoxGetText(friends, 3, 0) == u"Dee - " + retailText("Buddy:Online"));
	CHECK(GadgetListBoxGetText(friends, 4, 0) == u"Eve - " + retailText("Buddy:BuddyRequest"));
	CHECK(GadgetListBoxGetText(friends, 5, 0) == u"Fay - " + retailText("Buddy:Matching"));
	// ---- INVITE: not in a staging room, an online friend gets no INVITE (REMOVE FRIEND still works) ----
	auto select = [&](int row) {
		GadgetListBoxSetSelected(friends, row);
		REQUIRE(windows.setExtern("MessengerActiveTab", "0"));
		for (int i = 0; i < 3; ++i) windows.update(33);
	};
	select(0);
	CHECK(messenger.buttonEnabled(0));
	CHECK_FALSE(messenger.buttonEnabled(1));
	REQUIRE(windows.invokeCallback("AptMessenger::OnBttn_1", ""));
	CHECK(std::none_of(actions.begin(), actions.end(), [](const AptMessenger::Action &a) { return a.kind == AptMessenger::Action::Invite; }));
	// ---- the player joins a staging room: the buttons follow at once; INVITE for Ann (Staging), Dee (Online) and Fay (Matching), not Bea (Offline) or
	// Cy (Playing); a request's second button is ACCEPT FRIEND whatever the room ----
	client.signInGuest(u"Host");
	std::vector<OnlineLobbyClient::HttpRequest> rs = client.takeRequests();
	REQUIRE(rs.size() == 1);
	client.deliver(rs[0].id, 200, R"({"v":1,"token":"t.t","account":{"id":"guest:1","name":"Host","kind":"guest"}})", "");
	client.takeEvents();
	client.joinGame("0123456789abcdef", "");
	rs = client.takeRequests();
	REQUIRE(rs.size() == 1);
	client.deliver(rs[0].id, 200, "{\"v\":1,\"slot\":0,\"game\":" + shellGameView("0123456789abcdef", "maps/m/m.map", false) + "}", "");
	client.takeEvents();
	REQUIRE(client.inGame());
	for (int i = 0; i < 3; ++i) windows.update(33);
	CHECK(messenger.buttonEnabled(1));
	CHECK(messenger.inviteOffered());
	REQUIRE(windows.invokeCallback("AptMessenger::OnBttn_1", ""));
	REQUIRE(std::count_if(actions.begin(), actions.end(), [](const AptMessenger::Action &a) { return a.kind == AptMessenger::Action::Invite; }) == 1);
	CHECK(actions.back().player == "1111111111111111");
	const struct
	{
		int row;
		bool second, invite;
	} expected[] = { { 1, false, false }, { 2, false, false }, { 3, true, true }, { 4, true, false }, { 5, true, true } };
	for (const auto &e : expected)
	{
		select(e.row);
		CHECK_MESSAGE(messenger.buttonEnabled(1) == e.second, "row " << e.row);
		CHECK_MESSAGE(messenger.inviteOffered() == e.invite, "row " << e.row);
	}
	select(1); // Bea, offline: a press sends nothing
	const size_t before = actions.size();
	REQUIRE(windows.invokeCallback("AptMessenger::OnBttn_1", ""));
	CHECK(actions.size() == before);
	// ---- leaving the room takes INVITE away again ----
	select(0);
	client.leaveGame();
	REQUIRE_FALSE(client.inGame());
	for (int i = 0; i < 3; ++i) windows.update(33);
	CHECK_FALSE(messenger.buttonEnabled(1));
	// ---- lane ONLINEMENU-1 (owner, 2026-10-20: friends in this Messenger): the PLAYERS tab's ignored row says RotWK's UN-IGNORE and sends Unignore;
	// ADD FRIEND is greyed for an ignored player and for one already a friend or asked; ENTER with nobody chosen still hands the text on ----
	messenger.setRows(1, { { "7777777777777777", u"Gil", u"In this room", false, false, true }, { "8888888888888888", u"Hal", u"Ignored", false, true, false },
							 { "9999999999999999", u"Ivy", u"Recent player", false, false, false } });
	// the movie's own PLAYERS tab button (buddyGadgets.switchBttn), clicked where the pointer reaches it: its list is made when the tab plays in
	std::function<void(AptCharacterInst *, std::vector<AptButtonInst *> &)> buttons = [&](AptCharacterInst *c, std::vector<AptButtonInst *> &out) {
		if (!c) return;
		if (AptButtonInst *b = c->asButton())
		{
			if (b->globallyVisible()) out.push_back(b);
			return;
		}
		if (AptSpriteInst *sp = c->asSprite())
			for (AptCharacterInst *k : sp->children()) buttons(k, out);
	};
	bool switched = false;
	for (int attempt = 0; attempt < 30 && !switched; ++attempt)
	{
		std::vector<AptButtonInst *> all;
		buttons(windows.apt().level(messenger.level()), all);
		for (AptButtonInst *b : all)
		{
			float x0, y0, x1, y1, x, y;
			if (b->targetPath().find("buddyGadgets.switchBttn") == std::string::npos || !b->contentBounds(x0, y0, x1, y1)) continue;
			b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, x, y);
			windows.postMouseMove(x, y);
			windows.update(33);
			windows.postMouseButton(true);
			windows.update(33);
			windows.postMouseButton(false);
			for (int i = 0; i < 30; ++i) windows.update(33);
			switched = messenger.activeTab() == 1;
			break;
		}
		if (!switched) windows.update(33);
	}
	REQUIRE(switched);
	GameWindow *players = nullptr;
	for (auto *window : layer.gadgets().allWindows())
		if (window->aptInstanceName() == "Messenger::PlayersTab_1" && !window->winIsHidden()) players = window;
	REQUIRE(players);
	auto selectPlayer = [&](int row) {
		GadgetListBoxSetSelected(players, row);
		REQUIRE(windows.setExtern("MessengerActiveTab", "1"));
		for (int i = 0; i < 3; ++i) windows.update(33);
	};
	auto label = [&](int button) {
		const std::string *t = windows.aptText("APT:MessengerBttn_" + std::to_string(button));
		return t ? *t : std::string();
	};
	selectPlayer(0);
	CHECK(messenger.buttonEnabled(0)); // ADD FRIEND
	CHECK(label(1) == OnlineLobbyClient::utf16ToUtf8(retailText("APT:Ignore")));
	selectPlayer(1);
	CHECK_FALSE(messenger.buttonEnabled(0));
	CHECK(label(1) == OnlineLobbyClient::utf16ToUtf8(retailText("APT:UnIgnore")));
	REQUIRE(windows.invokeCallback("AptMessenger::OnBttn_1", ""));
	CHECK(actions.back().kind == AptMessenger::Action::Unignore);
	CHECK(actions.back().player == "8888888888888888");
	selectPlayer(2);
	CHECK_FALSE(messenger.buttonEnabled(0)); // a request either way, or a friend already
	GadgetListBoxSetSelected(players, -1);
	for (int i = 0; i < 3; ++i) windows.update(33);
	GameWindow *entry = nullptr;
	for (auto *window : layer.gadgets().allWindows())
		if (window->aptInstanceName() == "Messenger::ChatEntry") entry = window;
	REQUIRE(entry);
	GadgetTextEntrySetText(entry, u"ABCD-EFGH-JK");
	REQUIRE(windows.invokeCallback("AptMessenger::OnButtonSend", ""));
	CHECK(actions.back().kind == AptMessenger::Action::Send);
	CHECK(actions.back().player.empty());
	CHECK(actions.back().text == u"ABCD-EFGH-JK");
}
