// OpenBFME unit tests. GPL-3.0.
// Lane CHAT-1: the in-game chat without retail data: the CHAT command's encoding and limits, the chat over an in-memory network of several peers (order,
// recipients, the local echo, mute, the flood limit), the proof that chat never touches the lockstep (the frames' commands are the same with and without
// chat), long / odd / malformed text, the chat window's rules (GameClient/InGameChat.h) and the InGameUI message lines (GameClient/InGameUIMessages.h).

#include "doctest.h"

#include "GameClient/InGameChat.h"
#include "GameClient/InGameUIMessages.h"
#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/Network.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
// an in-memory network: reliable, in order per link, delivered on the next tick (test_net_core.cpp's Hub, without the delays)
class ChatHub
{
public:
	struct Endpoint : NetTransport
	{
		ChatHub *hub = nullptr;
		int slot = 0;
		std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
		bool sendTo(int to, const std::vector<std::uint8_t> &bytes, std::string *) override
		{
			hub->m_flight.push_back({ slot, to, bytes });
			return true;
		}
		bool receive(int &from, std::vector<std::uint8_t> &bytes) override
		{
			if (inbox.empty())
			{
				return false;
			}
			from = inbox.front().first;
			bytes = inbox.front().second;
			inbox.pop_front();
			return true;
		}
	};
	explicit ChatHub(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			auto e = std::make_unique<Endpoint>();
			e->hub = this;
			e->slot = i;
			m_endpoints.push_back(std::move(e));
		}
	}
	Endpoint &endpoint(int slot) { return *m_endpoints[(size_t)slot]; }
	void tick()
	{
		for (Flight &f : m_flight)
		{
			m_endpoints[(size_t)f.to]->inbox.push_back({ f.from, f.bytes });
			NetCommandMsg c;
			if (NetPacket::decodeCommand(f.bytes, c, nullptr) && c.type == NETCOMMANDTYPE_CHAT)
			{
				chatDeliveries[f.to]++;
			}
		}
		m_flight.clear();
	}
	// a datagram straight into a slot's inbox (a hostile peer's bytes)
	void inject(int from, int to, const std::vector<std::uint8_t> &bytes) { m_endpoints[(size_t)to]->inbox.push_back({ from, bytes }); }
	std::map<int, int> chatDeliveries; ///< CHAT commands that reached each slot's machine

private:
	struct Flight
	{
		int from, to;
		std::vector<std::uint8_t> bytes;
	};
	std::vector<std::unique_ptr<Endpoint>> m_endpoints;
	std::vector<Flight> m_flight;
};

NetworkConfig chatConfig(int slot, int peers)
{
	NetworkConfig c;
	c.localSlot = slot;
	for (int i = 0; i < peers; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = 10 + i;
		c.slotNames[(size_t)i] = u"Player" + std::u16string(1, (char16_t)(u'A' + i));
	}
	c.runAhead = 2;
	c.crcInterval = 0;
	c.disconnectPath = false;
	return c;
}

GameMessage chatMove(int player, float x)
{
	GameMessage m(MSG_DO_MOVETO, player);
	m.appendLocationArgument(Coord3D{ x, 1.0f, 0.0f });
	return m;
}

struct ChatGame
{
	explicit ChatGame(int n) : hub(n), peers(n), pending((size_t)n), frame((size_t)n, 0), executed((size_t)n), lines((size_t)n), issuedOn((size_t)n, -1)
	{
		for (int i = 0; i < n; ++i)
		{
			nets.push_back(std::make_unique<Network>(chatConfig(i, n), hub.endpoint(i)));
			nets.back()->setClock([this]() { return clockMs; });
		}
	}
	// one tick: every peer issues its moves, updates, runs a ready frame; then the links deliver
	void tick(UnsignedInt frames)
	{
		for (int p = 0; p < peers; ++p)
		{
			if (frame[(size_t)p] % 3 == (UnsignedInt)p && frame[(size_t)p] < frames && issuedOn[(size_t)p] != (long long)frame[(size_t)p])
			{
				issuedOn[(size_t)p] = (long long)frame[(size_t)p];
				pending[(size_t)p].append(chatMove(99, (float)(frame[(size_t)p] * 10 + p)));
			}
			nets[(size_t)p]->update(frame[(size_t)p], pending[(size_t)p]);
			if (frame[(size_t)p] < frames && nets[(size_t)p]->isFrameReady(frame[(size_t)p]))
			{
				CommandList out;
				nets[(size_t)p]->relayCommands(frame[(size_t)p], out);
				for (const GameMessage &m : out.messages())
				{
					executed[(size_t)p].push_back(std::to_string(frame[(size_t)p]) + ":" + std::to_string(m.getPlayerIndex()) + ":" + std::to_string(m.getType()));
				}
				++frame[(size_t)p];
			}
			for (Network::ChatLine &l : nets[(size_t)p]->takeChat())
			{
				lines[(size_t)p].push_back(std::to_string(l.slot) + ":" + std::string(l.text.begin(), l.text.end()));
			}
		}
		hub.tick();
		clockMs += 50;
	}
	ChatHub hub;
	int peers;
	std::vector<std::unique_ptr<Network>> nets;
	std::vector<CommandList> pending;
	std::vector<UnsignedInt> frame;
	std::vector<std::vector<std::string>> executed;
	std::vector<std::vector<std::string>> lines;
	std::vector<long long> issuedOn;
	std::uint64_t clockMs = 1000;
};

std::u16string u(const std::string &s)
{
	return std::u16string(s.begin(), s.end());
}
} // namespace

TEST_CASE("chat1: the CHAT command keeps its recipients and every UTF-16 unit; over 255 units, truncated or trailing bytes are refused")
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_CHAT;
	c.slot = 3;
	c.executionFrame = 77;
	c.playerMask = 0x8000000Bu; // 16 + slots fit (P16)
	// odd text: a NUL, control characters, a surrogate pair, a lone surrogate, right-to-left marks, '%' format characters, 255 units in all
	std::u16string odd = u"%s%n%ls\x0001";
	odd.push_back(u'\0');
	odd += u"\x001B[2J\xD83D\xDE00\xD800\x202E\x200F<Team> [x] ";
	odd += std::u16string(255 - odd.size(), u'\x4E2D');
	c.chatText = odd;
	std::string error;
	const std::vector<std::uint8_t> bytes = NetPacket::encodeCommand(c, &error);
	REQUIRE(!bytes.empty());
	NetCommandMsg d;
	REQUIRE(NetPacket::decodeCommand(bytes, d, &error));
	CHECK(d.type == NETCOMMANDTYPE_CHAT);
	CHECK(d.slot == 3);
	CHECK(d.playerMask == 0x8000000Bu);
	CHECK(d.chatText == odd);
	CHECK(d.chatText.size() == 255);
	CHECK(std::string(NetCommandTypeName(NETCOMMANDTYPE_CHAT)) == "NETCOMMANDTYPE_CHAT");

	c.chatText += u'x'; // 256 units: an error, never a cut line
	CHECK(NetPacket::encodeCommand(c, &error).empty());
	CHECK(error == "CHAT of 256 characters (at most 255)");

	// a hand-made command claiming 256 units, one claiming more units than it carries, one with a byte after the text
	NetByteWriter w;
	w.u8(NETCOMMANDTYPE_CHAT);
	w.u8(1);
	w.u32(0);
	w.u32(1);
	w.wstr(std::u16string(256, u'a'));
	CHECK_FALSE(NetPacket::decodeCommand(w.data(), d, &error));
	CHECK(error == "CHAT of 256 characters (at most 255)");
	std::vector<std::uint8_t> cut = bytes;
	cut.resize(cut.size() - 3);
	CHECK_FALSE(NetPacket::decodeCommand(cut, d, &error));
	CHECK(error == "truncated NETCOMMANDTYPE_CHAT");
	std::vector<std::uint8_t> extra = bytes;
	extra.push_back(0);
	CHECK_FALSE(NetPacket::decodeCommand(extra, d, &error));
	CHECK(error == "NETCOMMANDTYPE_CHAT with 1 trailing bytes");
}

TEST_CASE("chat1: four peers: lines arrive in send order, only on the recipients' machines, with the local echo; the frames' commands do not change")
{
	const UnsignedInt kFrames = 60;
	// the same game without chat
	ChatGame quiet(4);
	for (int t = 0; t < 300; ++t)
	{
		quiet.tick(kFrames);
	}
	ChatGame g(4);
	for (int t = 0; t < 300; ++t)
	{
		if (t == 5)
		{
			// slot 0 to its ally slot 1 (Allies: mask = the allies and self), three lines in one tick
			CHECK(g.nets[0]->sendChat(u"<Team> one", 0x3u));
			CHECK(g.nets[0]->sendChat(u"<Team> two", 0x3u));
			CHECK(g.nets[0]->sendChat(u"<Team> three", 0x3u));
		}
		if (t == 9)
		{
			CHECK(g.nets[2]->sendChat(u"<Global> hello all", 0xFu)); // Everyone
			CHECK(g.nets[3]->sendChat(u"<Team> to two", 0xCu));       // slot 3's allies are slots 2 and 3
		}
		g.tick(kFrames);
	}
	for (int p = 0; p < 4; ++p)
	{
		CHECK(g.frame[(size_t)p] == kFrames);
		CHECK(g.nets[(size_t)p]->errors().empty());
		CHECK(g.executed[(size_t)p] == quiet.executed[(size_t)p]); // chat never enters the frame data
		CHECK(g.nets[(size_t)p]->commandsRelayed() == quiet.nets[(size_t)p]->commandsRelayed());
	}
	CHECK(g.executed[1] == g.executed[0]);
	const std::vector<std::string> sender = { "0:<Team> one", "0:<Team> two", "0:<Team> three", "2:<Global> hello all" };
	CHECK(g.lines[0] == sender);                     // the echo of its own lines, then slot 2's
	CHECK(g.lines[1] == sender);                     // the ally: the same lines in the same order
	CHECK(g.lines[2] == std::vector<std::string>{ "2:<Global> hello all", "3:<Team> to two" });
	CHECK(g.lines[3] == std::vector<std::string>{ "3:<Team> to two", "2:<Global> hello all" }); // its own echo came first
	// the allies lines of slot 0 never reached slots 2 and 3's machines (OpenBFME: sent to the recipients only)
	CHECK(g.hub.chatDeliveries[1] == 4);
	CHECK(g.hub.chatDeliveries[2] == 1);
	CHECK(g.hub.chatDeliveries[3] == 1);
	CHECK(g.hub.chatDeliveries[0] == 1);
	for (int p = 0; p < 4; ++p)
	{
		CHECK(g.nets[(size_t)p]->chatNotAddressed() == 0);
	}
}

TEST_CASE("chat1: mute hides a slot's lines on the muting machine only; a flood beyond the limit is dropped and counted; bad lines are refused")
{
	ChatGame g(3);
	g.nets[1]->setChatMuted(0, true);
	CHECK(g.nets[1]->chatMuted(0));
	CHECK(g.nets[1]->chatMutedMask() == 1u);
	// slot 0 floods: 20 lines in one tick (the receivers take the burst of 8, then one per second)
	for (int i = 0; i < 20; ++i)
	{
		CHECK(g.nets[0]->sendChat(u("line " + std::to_string(i)), 0x7u));
	}
	g.tick(10);
	g.tick(10);
	CHECK(g.lines[0].size() == 20);                  // the sender's own echo is never limited
	CHECK(g.lines[1].empty());                       // muted
	CHECK(g.nets[1]->chatDroppedMuted() == 8);
	CHECK(g.nets[1]->chatDroppedFlood() == 12);
	REQUIRE(g.lines[2].size() == 8);
	CHECK(g.lines[2].front() == "0:line 0");
	CHECK(g.lines[2].back() == "0:line 7");
	CHECK(g.nets[2]->chatDroppedFlood() == 12);
	// a second later one more line gets through
	for (int i = 0; i < 20; ++i)
	{
		g.tick(10); // 20 ticks of 50 ms
	}
	CHECK(g.nets[0]->sendChat(u"after", 0x7u));
	g.tick(10);
	g.tick(10);
	CHECK(g.lines[2].back() == "0:after");
	g.nets[1]->setChatMuted(0, false);
	for (int i = 0; i < 21; ++i)
	{
		g.tick(10); // slot 1 spent its refill on "after" (dropped as muted): another second
	}
	CHECK(g.nets[0]->sendChat(u"unmuted", 0x7u));
	g.tick(10);
	g.tick(10);
	CHECK(g.lines[1] == std::vector<std::string>{ "0:unmuted" });

	std::string error;
	CHECK_FALSE(g.nets[0]->sendChat(u"", 0x7u, &error));
	CHECK(error == "an empty chat line");
	CHECK_FALSE(g.nets[0]->sendChat(std::u16string(256, u'a'), 0x7u, &error));
	CHECK(error == "a chat line of 256 characters (at most 255)");
	CHECK_FALSE(g.nets[0]->sendChat(u"nobody", 0x80u, &error));
	CHECK(error == "no player of the game is a recipient (mask 128)");
	CHECK(g.nets[0]->sendChat(std::u16string(255, u'z'), 0x2u)); // the longest line

	// a malformed CHAT from slot 2 (a hostile peer): reported as an error of that slot, the game goes on
	NetByteWriter w;
	w.u8(NETCOMMANDTYPE_CHAT);
	w.u8(2);
	w.u32(0);
	w.u32(1);
	w.u32(0x7FFFFFFFu); // a length far beyond the bytes
	g.hub.inject(2, 1, w.data());
	g.tick(10);
	REQUIRE(g.nets[1]->errors().size() == 1);
	CHECK(g.nets[1]->errors()[0] == "slot 2: truncated NETCOMMANDTYPE_CHAT");
	// a CHAT claiming another sender is refused like any command
	NetCommandMsg forged;
	forged.type = NETCOMMANDTYPE_CHAT;
	forged.slot = 0;
	forged.playerMask = 0x7u;
	forged.chatText = u"<Global> I am slot 0";
	g.hub.inject(2, 1, NetPacket::encodeCommand(forged, nullptr));
	g.tick(10);
	REQUIRE(g.nets[1]->errors().size() == 2);
	CHECK(g.nets[1]->errors()[1] == "slot 2 sent a command claiming slot 0");
	for (int i = 0; i < 40; ++i)
	{
		g.tick(10);
	}
	CHECK(g.frame[0] == 10);
	CHECK(g.frame[1] == 10);
	CHECK(g.frame[2] == 10);
}

TEST_CASE("chat1: the chat window's rules: who a line goes to per mode, the labels, trim and the formats")
{
	using S = InGameChat::Slot;
	// slot 0 local, 1 an ally, 2 an enemy, 3 an observer (not active), 4 empty, 5 an enemy muted
	std::vector<S> slots(8);
	slots[0] = S{ true, true, true, true, false };
	slots[1] = S{ true, false, true, true, false };
	slots[2] = S{ true, false, true, false, false };
	slots[3] = S{ true, false, false, false, false };
	slots[5] = S{ true, false, true, false, true };
	CHECK(InGameChat::recipientMask(InGameChatMode::Allies, slots) == 0x3u);
	CHECK(InGameChat::recipientMask(InGameChatMode::Everyone, slots) == 0xFu);
	CHECK(InGameChat::recipientMask(InGameChatMode::Players, slots) == 0x1u);
	CHECK(InGameChat::recipientMask(InGameChatMode::Console, slots) == 0x0u);
	CHECK(InGameChat::recipientMask(InGameChatMode::Observers, slots) == 0x8u);
	// an ally only one way is not an ally (RW 0x8ECF3E asks both relationships: the caller sets alliedBothWays from both)
	std::vector<S> none = slots;
	none[0].local = false;
	CHECK(InGameChat::recipientMask(InGameChatMode::Everyone, none) == 0x0u); // no local player: nothing (the loop's `local` test)

	CHECK(InGameChat::modeForMeta(false, true) == InGameChatMode::Allies);
	CHECK(InGameChat::modeForMeta(true, true) == InGameChatMode::Everyone);
	CHECK(InGameChat::modeForMeta(true, false) == InGameChatMode::Observers);
	CHECK(InGameChat::metaOpens(true, false, true, true));
	CHECK_FALSE(InGameChat::metaOpens(false, false, true, true)); // skirmish / campaign
	CHECK_FALSE(InGameChat::metaOpens(true, true, true, true));   // a replay
	CHECK_FALSE(InGameChat::metaOpens(true, false, false, true)); // a dead player while GlobalData + 0xA44 is set
	CHECK(InGameChat::metaOpens(true, false, false, false));
	CHECK(std::string(InGameChat::receiversLabel(InGameChatMode::Allies, true)) == "Chat:Allies");
	CHECK(std::string(InGameChat::receiversLabel(InGameChatMode::Everyone, true)) == "Chat:Everyone");
	CHECK(std::string(InGameChat::receiversLabel(InGameChatMode::Everyone, false)) == "Chat:Observers");
	CHECK(std::string(InGameChat::receiversLabel(InGameChatMode::Observers, false)) == "Chat:Observers");
	CHECK(std::string(InGameChat::formatLabel(InGameChatMode::Allies)) == "APT:TeamChat");
	CHECK(std::string(InGameChat::formatLabel(InGameChatMode::Everyone)) == "APT:GlobalChat");
	CHECK(std::string(InGameChat::formatLabel(InGameChatMode::Observers)) == "APT:ObserverChat");

	CHECK(InGameChat::trim(u"  \t hi there \r\n") == u"hi there");
	CHECK(InGameChat::trim(u" \t ") == u"");
	CHECK(InGameChat::formatLine(u"<Team> %s", u"hello") == u"<Team> hello");
	CHECK(InGameChat::formatLine(u"<Global> %ls", u"100% %s") == u"<Global> 100% %s"); // the text is never a format
	CHECK(InGameChat::formatLine(u"no argument", u"x") == u"no argument");
	CHECK(InGameChat::receivedLine(u"Jonathan", u"<Team> hello") == u"[Jonathan] <Team> hello");

	InGameChat c;
	for (int i = 0; i < 105; ++i)
	{
		c.addHistory(u("l" + std::to_string(i)), 0xFF112233u, (std::uint32_t)i);
	}
	REQUIRE(c.history().size() == InGameChat::kHistoryLines);
	CHECK(c.history().front().text == u"l5");
	CHECK(c.history().back().text == u"l104");
	c.setDraft(u"half typed");
	CHECK(c.draft() == u"half typed");
	REQUIRE(InGameChat::stopLines().size() == 1);
	CHECK(InGameChat::stopLines()[0].rfind("[S-5150] in-game chat", 0) == 0);
}

TEST_CASE("chat1: the message lines: six, newest first, colours alternate unless coloured, 20 s then the fade, retail's screen placement")
{
	InGameUIMessageSettings s;
	s.delayMS = 75000; // retail's ingameui.ini value: 75000 / 5 / 1000 = 15 frames
	InGameUIMessages m;
	m.setSettings(s);
	m.addMessageText(u"a", nullptr, 100);
	m.addMessageText(u"b", nullptr, 100);
	m.addMessageText(u"c", nullptr, 100);
	CHECK(m.lines()[2].argb == 0xFFFFFFFFu); // colour 1, then 2, then 1
	CHECK(m.lines()[1].argb == 0xFFB4B4B4u);
	CHECK(m.lines()[0].argb == 0xFFFFFFFFu);
	const std::uint32_t red = 0x00C00000u;
	m.addMessageText(u"[P] <Team> hi", &red, 101);
	CHECK(m.lines()[0].argb == 0xFFC00000u);
	for (int i = 0; i < 3; ++i)
	{
		m.addMessageText(u"x", nullptr, 102);
	}
	CHECK(m.lineCount() == 6);
	CHECK(m.lines()[5].text == u"b"); // "a" dropped (the seventh line)
	CHECK(m.lines()[4].text == u"c");
	// the expiry: nothing changes before frame + 100 (the amount truncates to 0), then 1, 2 ... per update
	m.update(150);
	CHECK((m.lines()[5].argb >> 24) == 0xFFu);
	m.update(200); // age 100 -> amount 1
	CHECK((m.lines()[5].argb >> 24) == 0xFEu);
	CHECK((m.lines()[0].argb >> 24) == 0xFFu); // age 98
	int updates = 0;
	while (m.lines()[5].used && updates < 1000)
	{
		m.update(300); // age 200: 2 per update
		++updates;
	}
	CHECK(updates == 127); // 254 / 2
	CHECK(m.lines()[4].used == false); // "c" (same age) went with it
	CHECK(m.lineCount() == 4);
	// retail's placement (RW 0x6A0BC0): X:-44 Y:10 on 1024 x 768 at 1920 x 1080
	CHECK(InGameUIMessages::screenX(1920, -44) == -82);
	CHECK(InGameUIMessages::screenY(1080, 10) == 14);
	CHECK(InGameUIMessages::screenX(1024, -44) == -44);
	CHECK(InGameUIMessages::screenY(768, 10) == 10);
}

TEST_CASE("chat1: the message settings: InGameUI's Message rows over the constructor defaults, Language's MessageFont over the INI font")
{
	const std::string ini = "InGameUI\n  MaxSelectionSize = 40\n  MessageColor1 = R:255 G:255 B:255\n  MessageColor2 = R:253 G:236 B:2\n"
		"  MessagePosition = X:-44 Y:10\n  MessagePositionLW = X:-10 Y:85\n  MessageFont = \"Albertus MT\"\n  MessagePointSize = 10\n"
		"  MessageDelayMS = 75000\nEnd\n";
	InGameUIMessageSettings out;
	std::string error;
	REQUIRE_MESSAGE(InGameUIMessageSettings::scan(ini, "Language\nEnd\n", out, &error), error);
	CHECK(out.color1 == 0xFFFFFFFFu);
	CHECK(out.color2 == 0xFFFDEC02u);
	CHECK(out.positionX == -44);
	CHECK(out.positionY == 10);
	CHECK(out.positionLWX == -10);
	CHECK(out.positionLWY == 85);
	CHECK(out.font == "Albertus MT");
	CHECK(out.pointSize == 10);
	CHECK(out.bold == false);
	CHECK(out.delayMS == 75000);
	REQUIRE(InGameUIMessageSettings::scan("InGameUI\nEnd\n", "Language\n  MessageFont = \"Arial Unicode MS\" 12 Yes\nEnd\n", out, &error));
	CHECK(out.font == "Arial Unicode MS");
	CHECK(out.pointSize == 12);
	CHECK(out.bold == true);
	CHECK(out.color2 == 0xFFB4B4B4u); // the constructor default
	CHECK(out.delayMS == 5000);
	CHECK_FALSE(InGameUIMessageSettings::scan("", "Language\nEnd\n", out, &error)); // no InGameUI block: an error, not the defaults
	CHECK(error == "ingameui.ini: no InGameUI block");
}

TEST_CASE("chat1: the lobby moderators' mute: the START's chatMuted marks human slots; their lines are dropped everywhere and their own client cannot send")
{
	const ProfileIdentity own = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	const std::string ticket = "AQHvzauJZ0UjAQEHAJQ1dxkKDPX40Zd_QbpSaTVc2UI"; // slot 1, mask 0x07 (test_online_relay.cpp's)
	auto start = [&](const std::string &extra) {
		return "{\"v\":1,\"kind\":\"openbfme-start\",\"game\":\"0123456789abcdef\",\"slot\":1,\"relay\":\"192.0.2.10:7350\",\"ticket\":\"" + ticket
			+ "\",\"runAhead\":3,\"crcInterval\":100,\"profile\":\"" + own.digest + "\"" + extra
			+ ",\"setup\":{\"map\":\"maps/map mp evendim/map mp evendim.map\",\"seed\":4242,\"startingCash\":2000,\"superweaponRestriction\":0,\"rules\":null,"
			  "\"legendaryHeroes\":1,\"slots\":[{\"state\":\"player\",\"name\":\"Ann\",\"faction\":0,\"color\":0,\"startPos\":0,\"team\":0},"
			  "{\"state\":\"player\",\"name\":\"Bea\",\"faction\":1,\"color\":1,\"startPos\":1,\"team\":1},"
			  "{\"state\":\"player\",\"name\":\"Cy\",\"faction\":-1,\"color\":-1,\"startPos\":-1,\"team\":-1},"
			  "{\"state\":\"hard\",\"name\":\"AI\",\"faction\":2,\"color\":3,\"startPos\":3,\"team\":1}]}}";
	};
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 8;
	limits.colorCount = 8;
	LobbyStart s;
	std::string error;
	REQUIRE_MESSAGE(OnlineStart::parse(start(",\"chatMuted\":[false,false,true,true,false,false,false,false]"), own, limits, s, &error), error);
	CHECK(s.chatMutedMask == 0x4u); // slot 3 is an AI: never muted
	REQUIRE(OnlineStart::parse(start(""), own, limits, s, &error));
	CHECK(s.chatMutedMask == 0u); // an older lobby: nobody
	REQUIRE(OnlineStart::parse(start(",\"chatMuted\":\"yes\""), own, limits, s, &error));
	CHECK(s.chatMutedMask == 0u);

	ChatGame g(3);
	for (int p = 0; p < 3; ++p)
	{
		g.nets[(size_t)p]->setChatLobbyMuted(0x4u); // slot 2 muted by the moderators
	}
	CHECK_FALSE(g.nets[2]->sendChat(u"<Global> spam", 0x7u, &error));
	CHECK(error == "muted by the moderators");
	// a modified client that sends anyway: dropped by every receiver
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_CHAT;
	c.slot = 2;
	c.playerMask = 0x7u;
	c.chatText = u"<Global> spam";
	g.hub.inject(2, 0, NetPacket::encodeCommand(c, nullptr));
	g.hub.inject(2, 1, NetPacket::encodeCommand(c, nullptr));
	CHECK(g.nets[0]->sendChat(u"<Global> hi", 0x7u));
	g.tick(10);
	g.tick(10);
	CHECK(g.lines[0] == std::vector<std::string>{ "0:<Global> hi" });
	CHECK(g.lines[1] == std::vector<std::string>{ "0:<Global> hi" });
	CHECK(g.lines[2] == std::vector<std::string>{ "0:<Global> hi" }); // a muted player still reads
	CHECK(g.nets[0]->chatDroppedMuted() == 1);
	CHECK(g.nets[1]->chatDroppedMuted() == 1);
}

TEST_CASE("chat1: the disconnect screen's chat (DISCONNECTCHAT 13) goes to every connected player with the echo; long or forged lines are refused")
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_DISCONNECTCHAT;
	c.slot = 1;
	c.chatText = u"waiting for Bob";
	std::string error;
	NetCommandMsg d;
	REQUIRE(NetPacket::decodeCommand(NetPacket::encodeCommand(c, &error), d, &error));
	CHECK(d.type == NETCOMMANDTYPE_DISCONNECTCHAT);
	CHECK(d.chatText == u"waiting for Bob");
	CHECK(std::string(NetCommandTypeName(NETCOMMANDTYPE_DISCONNECTCHAT)) == "NETCOMMANDTYPE_DISCONNECTCHAT");
	c.chatText = std::u16string(256, u'a');
	CHECK(NetPacket::encodeCommand(c, &error).empty());
	CHECK(error == "DISCONNECTCHAT of 256 characters (at most 255)");

	ChatGame g(3);
	CHECK(g.nets[1]->sendDisconnectChat(u"is anyone there?"));
	g.tick(5);
	g.tick(5);
	for (int p = 0; p < 3; ++p)
	{
		const std::vector<Network::ChatLine> l = g.nets[(size_t)p]->takeDisconnectChat();
		REQUIRE(l.size() == 1);
		CHECK(l[0].slot == 1);
		CHECK(l[0].text == u"is anyone there?");
		CHECK(g.nets[(size_t)p]->takeChat().empty()); // never an in-game chat line
	}
	CHECK_FALSE(g.nets[1]->sendDisconnectChat(u"", &error));
	CHECK(error == "an empty chat line");
	g.nets[0]->setChatLobbyMuted(0x2u);
	CHECK(g.nets[1]->sendDisconnectChat(u"again"));
	g.tick(5);
	g.tick(5);
	CHECK(g.nets[0]->takeDisconnectChat().empty()); // the moderators' mute applies here too
	CHECK(g.nets[2]->takeDisconnectChat().size() == 1);
}
