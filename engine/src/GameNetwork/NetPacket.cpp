// OpenBFME. GPL-3.0.
// See GameNetwork/NetPacket.h.

#include "GameNetwork/NetPacket.h"

#include <cstring>

namespace
{
bool failWith(std::string *error, const std::string &why)
{
	if (error)
	{
		*error = why;
	}
	return false;
}
constexpr std::uint32_t kMaxString = 1u << 16;
} // namespace

const char *NetCommandTypeName(int type)
{
	switch (type)
	{
	case NETCOMMANDTYPE_FRAMEINFO:
		return "NETCOMMANDTYPE_FRAMEINFO";
	case NETCOMMANDTYPE_GAMECOMMAND:
		return "NETCOMMANDTYPE_GAMECOMMAND";
	case NETCOMMANDTYPE_PLAYERLEAVE:
		return "NETCOMMANDTYPE_PLAYERLEAVE";
	case NETCOMMANDTYPE_KEEPALIVE:
		return "NETCOMMANDTYPE_KEEPALIVE";
	case NETCOMMANDTYPE_LOADCOMPLETE:
		return "NETCOMMANDTYPE_LOADCOMPLETE";
	case NETCOMMANDTYPE_DESYNCREPORT:
		return "NETCOMMANDTYPE_DESYNCREPORT";
	case NETCOMMANDTYPE_DESTROYPLAYER:
		return "NETCOMMANDTYPE_DESTROYPLAYER";
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		return "NETCOMMANDTYPE_DISCONNECTKEEPALIVE";
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		return "NETCOMMANDTYPE_DISCONNECTPLAYER";
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		return "NETCOMMANDTYPE_DISCONNECTVOTE";
	case NETCOMMANDTYPE_DISCONNECTFRAME:
		return "NETCOMMANDTYPE_DISCONNECTFRAME";
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		return "NETCOMMANDTYPE_DISCONNECTSCREENOFF";
	case NETCOMMANDTYPE_FRAMERESEND:
		return "NETCOMMANDTYPE_FRAMERESEND";
	case NETCOMMANDTYPE_RUNAHEAD:
		return "NETCOMMANDTYPE_RUNAHEAD";
	case NETCOMMANDTYPE_REJOIN:
		return "NETCOMMANDTYPE_REJOIN";
	case NETCOMMANDTYPE_REJOINREQUEST:
		return "NETCOMMANDTYPE_REJOINREQUEST";
	case NETCOMMANDTYPE_PAUSE:
		return "NETCOMMANDTYPE_PAUSE";
	case NETCOMMANDTYPE_RESUME:
		return "NETCOMMANDTYPE_RESUME";
	case NETCOMMANDTYPE_VIEW:
		return "NETCOMMANDTYPE_VIEW";
	case NETCOMMANDTYPE_CHAT:
		return "NETCOMMANDTYPE_CHAT";
	case NETCOMMANDTYPE_DISCONNECTCHAT:
		return "NETCOMMANDTYPE_DISCONNECTCHAT";
	default:
		return "";
	}
}

void NetByteWriter::f32(float v)
{
	std::uint32_t bits;
	std::memcpy(&bits, &v, sizeof(bits));
	u32(bits);
}

void NetByteWriter::str(const std::string &s)
{
	u32((std::uint32_t)s.size());
	m_bytes.insert(m_bytes.end(), s.begin(), s.end());
}

void NetByteWriter::wstr(const std::u16string &s)
{
	u32((std::uint32_t)s.size());
	for (char16_t c : s)
	{
		u16((std::uint16_t)c);
	}
}

bool NetByteReader::need(size_t n)
{
	if (m_failed || m_size - m_pos < n)
	{
		m_failed = true;
		return false;
	}
	return true;
}

std::uint8_t NetByteReader::u8()
{
	if (!need(1))
	{
		return 0;
	}
	return m_data[m_pos++];
}

bool NetByteReader::flag()
{
	const std::uint8_t b = u8();
	if (b > 1)
	{
		m_failed = true;
		m_badFlag = true;
		return false;
	}
	return b != 0;
}

std::uint16_t NetByteReader::u16()
{
	if (!need(2))
	{
		return 0;
	}
	const std::uint16_t v = (std::uint16_t)(m_data[m_pos] | (m_data[m_pos + 1] << 8));
	m_pos += 2;
	return v;
}

std::uint32_t NetByteReader::u32()
{
	if (!need(4))
	{
		return 0;
	}
	const std::uint32_t v = (std::uint32_t)m_data[m_pos] | ((std::uint32_t)m_data[m_pos + 1] << 8) | ((std::uint32_t)m_data[m_pos + 2] << 16)
		| ((std::uint32_t)m_data[m_pos + 3] << 24);
	m_pos += 4;
	return v;
}

float NetByteReader::f32()
{
	const std::uint32_t bits = u32();
	float v;
	std::memcpy(&v, &bits, sizeof(v));
	return v;
}

std::string NetByteReader::str()
{
	const std::uint32_t n = u32();
	if (n > kMaxString || !need(n))
	{
		m_failed = true;
		return std::string();
	}
	std::string s((const char *)m_data + m_pos, n);
	m_pos += n;
	return s;
}

std::u16string NetByteReader::wstr()
{
	const std::uint32_t n = u32();
	if (n > kMaxString || !need((size_t)n * 2))
	{
		m_failed = true;
		return std::u16string();
	}
	std::u16string s;
	s.reserve(n);
	for (std::uint32_t i = 0; i < n; ++i)
	{
		s.push_back((char16_t)u16());
	}
	return s;
}

std::vector<std::uint8_t> NetByteReader::bytes()
{
	const std::uint32_t n = u32();
	if (!need(n))
	{
		m_failed = true;
		return {};
	}
	std::vector<std::uint8_t> b(m_data + m_pos, m_data + m_pos + n);
	m_pos += n;
	return b;
}

bool NetPacket::writeGameMessage(NetByteWriter &w, const GameMessage &m, std::string *error)
{
	if (!IsNetworkLogicMessageType(m.getType())) // lane CHEAT-1: + MSG_OPENBFME_CHEAT
	{
		return failWith(error, "GameMessage type " + std::to_string(m.getType()) + " is not a logic message (1001 .. 1147, or 32001 lane CHEAT-1)");
	}
	if (m.getPlayerIndex() < -1 || m.getPlayerIndex() > 254)
	{
		return failWith(error, "GameMessage player index " + std::to_string(m.getPlayerIndex()) + " out of range");
	}
	if (m.getArgumentCount() > NetPacket::kMaxGameMessageArguments)
	{
		return failWith(error, "GameMessage with " + std::to_string(m.getArgumentCount()) + " arguments (at most " +
			std::to_string(NetPacket::kMaxGameMessageArguments) + ")");
	}
	w.u16((std::uint16_t)m.getType());
	w.u8((std::uint8_t)(m.getPlayerIndex() < 0 ? 0xFF : m.getPlayerIndex()));
	w.u32((std::uint32_t)m.getArgumentCount());
	for (size_t i = 0; i < m.getArgumentCount(); ++i)
	{
		const GameMessageArgument &a = *m.getArgument(i);
		w.u8((std::uint8_t)a.type);
		switch (a.type)
		{
		case ARGUMENTDATATYPE_INTEGER:
			w.i32(a.integer);
			break;
		case ARGUMENTDATATYPE_REAL:
			w.f32(a.real);
			break;
		case ARGUMENTDATATYPE_BOOLEAN:
			w.u8(a.boolean ? 1 : 0);
			break;
		case ARGUMENTDATATYPE_OBJECTID:
			w.u32(a.objectID);
			break;
		case ARGUMENTDATATYPE_LOCATION:
			w.f32(a.location.x);
			w.f32(a.location.y);
			w.f32(a.location.z);
			break;
		default:
			return failWith(error, "GameMessage argument type " + std::to_string((int)a.type) + " has no encoding (GameMessage cannot hold it)");
		}
	}
	return true;
}

std::unique_ptr<GameMessage> NetPacket::readGameMessage(NetByteReader &r, std::string *error)
{
	const int type = r.u16();
	const std::uint8_t player = r.u8();
	const std::uint32_t argc = r.u32();
	if (r.failed())
	{
		failWith(error, "truncated GameMessage header");
		return nullptr;
	}
	// every argument takes at least two bytes (its type and a boolean): a count the remaining bytes cannot hold is refused before anything is allocated
	if (argc > NetPacket::kMaxGameMessageArguments || (size_t)argc * 2 > r.remaining())
	{
		failWith(error, "GameMessage argument count " + std::to_string(argc) + " (at most " + std::to_string(NetPacket::kMaxGameMessageArguments) +
			", and the bytes left hold " + std::to_string(r.remaining() / 2) + ")");
		return nullptr;
	}
	if (!IsNetworkLogicMessageType(type)) // lane CHEAT-1: + MSG_OPENBFME_CHEAT
	{
		failWith(error, "GameMessage type " + std::to_string(type) + " is not a logic message (1001 .. 1147, or 32001 lane CHEAT-1)");
		return nullptr;
	}
	std::unique_ptr<GameMessage> m = std::make_unique<GameMessage>(type, player == 0xFF ? -1 : (int)player);
	for (std::uint32_t i = 0; i < argc; ++i)
	{
		const int at = r.u8();
		switch (at)
		{
		case ARGUMENTDATATYPE_INTEGER:
			m->appendIntegerArgument(r.i32());
			break;
		case ARGUMENTDATATYPE_REAL:
			m->appendRealArgument(r.f32());
			break;
		case ARGUMENTDATATYPE_BOOLEAN:
		{
			const std::uint8_t b = r.u8();
			if (b > 1)
			{
				failWith(error, "GameMessage boolean argument " + std::to_string(b));
				return nullptr;
			}
			m->appendBooleanArgument(b != 0);
			break;
		}
		case ARGUMENTDATATYPE_OBJECTID:
			m->appendObjectIDArgument(r.u32());
			break;
		case ARGUMENTDATATYPE_LOCATION:
		{
			Coord3D c;
			c.x = r.f32();
			c.y = r.f32();
			c.z = r.f32();
			m->appendLocationArgument(c);
			break;
		}
		default:
			failWith(error, "GameMessage argument type " + std::to_string(at) + " unknown");
			return nullptr;
		}
		if (r.failed())
		{
			failWith(error, "truncated GameMessage argument " + std::to_string(i));
			return nullptr;
		}
	}
	return m;
}

void NetPacket::writeGameInfo(NetByteWriter &w, const SkirmishGameInfo &g)
{
	bool extended = g.mapStartCount != LEGACY_MAX_SLOTS;
	for (int i = LEGACY_MAX_SLOTS; i < MAX_SLOTS; ++i)
	{
		extended = extended || g.slots[i].state != SLOT_CLOSED;
	}
	if (extended)
	{
		// An invalid legacy setup with active high slots must fail decoding, not silently lose those slots.
		w.i32(kExtendedGameInfoTag);
		w.i32(g.mapStartCount);
	}
	const int count = extended ? MAX_SLOTS : LEGACY_MAX_SLOTS;
	for (int i = 0; i < count; ++i)
	{
		const SkirmishGameSlot &s = g.slots[i];
		w.i32((std::int32_t)s.state);
		w.wstr(s.name);
		w.u8(s.accepted ? 1 : 0);
		w.u8(s.hasMap ? 1 : 0);
		w.i32(s.color);
		w.i32(s.startPos);
		w.i32(s.playerTemplate);
		w.i32(s.teamNumber);
		w.i32(s.origColor);
		w.i32(s.origStartPos);
		w.i32(s.origPlayerTemplate);
		// lane HERO-2: the slot's Create-a-Hero, the whole record in its .cah form (CreateAHeroHero::save: every field and the checksum)
		w.u8(s.hasCreateAHero ? 1 : 0);
		if (s.hasCreateAHero)
		{
			w.bytes(s.createAHero.save());
		}
	}
	w.str(g.mapName);
	w.u32(g.mapCRC);
	w.u32(g.mapSize);
	w.i32(g.mapMask);
	w.u32(g.seed);
	w.i32(g.startingCash);
	w.i32(g.superweaponRestriction);
	w.u8(g.inProgress ? 1 : 0);
	// lane RULES-1: the ten game rules (RotWK's LAN game buffer RW 0x84A976 writes them as ten u32 after the seed) and OpenBFME's Legendary Heroes factor
	for (std::int32_t v : g.rules)
	{
		w.i32(v);
	}
	w.i32(g.legendaryHeroes);
	w.i32((std::int32_t)g.simulationProfile); // lane UBER-1: the simulation profile (Common/SimulationProfile.h)
	// lane P16: the handicap entries follow the record's slot loop: 8 for a legacy game, 16 for an extended one
	for (int i = 0; i < count; ++i)
	{
		w.i32(g.slots[i].handicap); // lane OWNERFIX-2: GameSlot + 0x20 (RotWK's LAN option string carries it as "Handicap=%d", RW 0x8476E6 = BFME2 decomp BfmeAptScreenLanLobby.cpp AptLanLobby::MpOwnerSelectHandicap, tier A)
	}
	w.i32(g.cheatMode); // lane CHEAT-1: Cheat Mode (-1 off, else the cheat seat's slot)
}

bool NetPacket::readGameInfo(NetByteReader &r, SkirmishGameInfo &g, std::string *error, bool profileOptionalAtEnd, bool slotHandicaps, bool cheatField)
{
	g = SkirmishGameInfo();
	const std::int32_t firstState = r.i32();
	const bool extended = firstState == kExtendedGameInfoTag;
	if (extended)
	{
		g.mapStartCount = r.i32();
		if (r.failed() || g.mapStartCount <= LEGACY_MAX_SLOTS || g.mapStartCount > MAX_SLOTS)
		{
			return failWith(error, "GameInfo extended map start count " + std::to_string(g.mapStartCount) + " (requires 9 .. 16)");
		}
	}
	const int count = g.serializedSlotCount();
	for (int i = 0; i < count; ++i)
	{
		SkirmishGameSlot &s = g.slots[i];
		const std::int32_t state = i == 0 && !extended ? firstState : r.i32();
		if (state < SLOT_OPEN || state > SLOT_PLAYER)
		{
			return failWith(error, "GameInfo slot state " + std::to_string(state));
		}
		if (i >= g.mapStartCount && state != SLOT_CLOSED)
		{
			return failWith(error, "GameInfo slot " + std::to_string(i) + " exceeds the map's start positions");
		}
		s.state = (SlotState)state;
		s.name = r.wstr();
		s.accepted = r.flag();
		s.hasMap = r.flag();
		s.color = r.i32();
		s.startPos = r.i32();
		s.playerTemplate = r.i32();
		s.teamNumber = r.i32();
		s.origColor = r.i32();
		s.origStartPos = r.i32();
		s.origPlayerTemplate = r.i32();
		s.clearCreateAHero();
		if (r.flag())
		{
			// lane HERO-2: SkirmishGameSlot::setCreateAHeroBytes (the stream, the checksum, the wire form); only a human slot has one
			const std::vector<std::uint8_t> bytes = r.bytes();
			if (r.failed())
			{
				break;
			}
			const int slot = (int)(&s - g.slots);
			std::string why;
			if (!s.setCreateAHeroBytes(bytes, &why))
			{
				return failWith(error, "GameInfo slot " + std::to_string(slot) + " " + why);
			}
			if (s.state != SLOT_PLAYER)
			{
				return failWith(error, "GameInfo slot " + std::to_string(slot) + " has a Create-a-Hero but no human player");
			}
		}
	}
	g.mapName = r.str();
	g.mapCRC = r.u32();
	g.mapSize = r.u32();
	g.mapMask = r.i32();
	g.seed = r.u32();
	g.startingCash = r.i32();
	g.superweaponRestriction = r.i32();
	g.inProgress = r.flag();
	for (std::int32_t &v : g.rules)
	{
		v = r.i32();
	}
	g.legendaryHeroes = r.i32();
	const bool profileAbsent = !extended && profileOptionalAtEnd && !r.failed() && r.atEnd();
	const std::int32_t profile = profileAbsent ? (std::int32_t)SimulationProfile::Faithful : r.i32();
	// lane OWNERFIX-2: the slots' handicaps (absent: an older replay / save format, or an announcement without the profile, UBERJOIN-1's case; the LAN
	// messages that carry them are LANAPI protocol 3 / LANLobby version 4, so an announcement with the profile and without them is truncated)
	const bool handicapsAbsent = !slotHandicaps || profileAbsent;
	// lane P16: the handicap entries follow the record's slot loop (a legacy record carries 8)
	for (int i = 0; i < count; ++i)
	{
		g.slots[i].handicap = handicapsAbsent ? 0 : r.i32();
	}
	// lane CHEAT-1: Cheat Mode (absent: an older replay / save format or an announcement without the profile: off)
	g.cheatMode = (!cheatField || handicapsAbsent) ? -1 : r.i32();
	if (r.failed())
	{
		return failWith(error, r.badFlag() ? "GameInfo with a flag that is not 0 / 1" : "truncated GameInfo");
	}
	// lane MP-2 (review): every slot field is -1 (random / none) or a real value
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = g.slots[i];
		if (extended && (s.startPos >= g.mapStartCount || s.origStartPos >= g.mapStartCount))
		{
			return failWith(error, "GameInfo slot " + std::to_string(i) + " names a start position beyond the map's capacity");
		}
		for (int v : { s.color, s.startPos, s.playerTemplate, s.teamNumber, s.origColor, s.origStartPos, s.origPlayerTemplate })
		{
			if (v < -1)
			{
				return failWith(error, "GameInfo slot " + std::to_string(i) + " has the field value " + std::to_string(v));
			}
		}
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (!SkirmishGameSlot::handicapValid(g.slots[i].handicap))
		{
			return failWith(error, "GameInfo slot " + std::to_string(i) + " has the handicap " + std::to_string(g.slots[i].handicap) + " (0, -5 .. -95)");
		}
	}
	// lane RULES-1: -1 is "not set" (RotWK GameInfo::m_54 = -1 after reset, RW 0x801AAF; the starting money then comes from the InitialResources rule)
	if (g.startingCash < -1)
	{
		return failWith(error, "GameInfo starting cash " + std::to_string(g.startingCash));
	}
	// lane RULES-1: the rules are retail ints (retail accepts any value); the Legendary Heroes factor is OpenBFME's and must be one it knows: a peer that
	// sees another refuses the game options (a refused game, never a desync)
	if (!LegendaryHeroes::valid(g.legendaryHeroes))
	{
		return failWith(error, "GameInfo Legendary Heroes factor " + std::to_string(g.legendaryHeroes));
	}
	// lane CHEAT-1: the cheat seat is a human slot (the host's); anything else is a setup no host makes: refused, never a guess
	if (g.cheatMode < -1 || g.cheatMode >= MAX_SLOTS || (g.cheatMode >= 0 && g.slots[g.cheatMode].state != SLOT_PLAYER))
	{
		return failWith(error, "GameInfo Cheat Mode seat " + std::to_string(g.cheatMode) + " is not a human slot");
	}
	// lane UBER-1: a simulation profile this build does not know is a refused setup (never a guess)
	if (!simulationProfileValid(profile))
	{
		return failWith(error, "GameInfo simulation profile " + std::to_string(profile));
	}
	g.simulationProfile = (SimulationProfile)profile;
	// lane RULES-1: AllowCustomHeroes off and a slot with a Create-a-Hero is a setup no host makes: refused
	if (!g.customHeroesAllowed())
	{
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			if (g.slots[i].hasCreateAHero)
			{
				return failWith(error, "GameInfo slot " + std::to_string(i) + " has a Create-a-Hero while AllowCustomHeroes is off");
			}
		}
	}
	return true;
}

void NetPacket::writeNewGameMessage(NetByteWriter &w, const NewGameMessage &m)
{
	w.u8(m.mode == NewGameMode::Skirmish ? 1 : 0);
	w.i32(m.difficulty);
	w.i32(m.rankPoints);
	writeGameInfo(w, m.game);
}

bool NetPacket::readNewGameMessage(NetByteReader &r, NewGameMessage &m, std::string *error, bool slotHandicaps, bool cheatField)
{
	const std::uint8_t mode = r.u8();
	if (mode > 1)
	{
		return failWith(error, "NewGameMessage mode " + std::to_string(mode));
	}
	m.mode = mode ? NewGameMode::Skirmish : NewGameMode::SinglePlayer;
	m.difficulty = r.i32();
	m.rankPoints = r.i32();
	return readGameInfo(r, m.game, error, false, slotHandicaps, cheatField);
}

std::vector<std::uint8_t> NetPacket::encodeCommand(const NetCommandMsg &c, std::string *error)
{
	NetByteWriter w;
	std::uint8_t wireType = (std::uint8_t)c.type;
	if (c.type == NETCOMMANDTYPE_DISCONNECTPLAYER && (c.releasedMask > 0xff || c.targetSlot >= LEGACY_MAX_SLOTS)) wireType = kDisconnectPlayer16;
	if (c.type == NETCOMMANDTYPE_DISCONNECTSCREENOFF && c.releasedMask > 0xff) wireType = kDisconnectScreenOff16;
	if (c.type == NETCOMMANDTYPE_FRAMERESEND && (c.rejoinMask > 0xff || c.targetSlot >= LEGACY_MAX_SLOTS)) wireType = kFrameResend16;
	const bool wide = wireType != (std::uint8_t)c.type;
	w.u8(wireType);
	w.u8(c.slot);
	w.u32(c.executionFrame);
	switch (c.type)
	{
	case NETCOMMANDTYPE_FRAMEINFO:
		w.u16(c.commandCount);
		break;
	case NETCOMMANDTYPE_GAMECOMMAND:
		if (!c.message)
		{
			failWith(error, "GAMECOMMAND without a message");
			return {};
		}
		if (!writeGameMessage(w, *c.message, error))
		{
			return {};
		}
		break;
	case NETCOMMANDTYPE_DESYNCREPORT:
		w.bytes(c.payload);
		break;
	case NETCOMMANDTYPE_VIEW: // lane ANTICHEAT-1
		for (float v : c.view)
		{
			w.f32(v);
		}
		w.u8(c.viewFlags);
		break;
	case NETCOMMANDTYPE_DESTROYPLAYER:
	case NETCOMMANDTYPE_REJOIN: // lane ONLINE-3
		w.u8(c.targetSlot);
		break;
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		w.u8(c.targetSlot);
		w.u32(c.frame);
		// lane MP-2 (review): the slots before the sender in the fallback order that the sender holds gone (lane P16: a bit per slot)
		if (wide) w.u16(c.releasedMask);
		else w.u8((std::uint8_t)c.releasedMask);
		break;
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		w.u8(c.targetSlot);
		w.u32(c.frame);
		break;
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	case NETCOMMANDTYPE_RUNAHEAD:
	case NETCOMMANDTYPE_REJOINREQUEST: // lane ONLINE-3
	case NETCOMMANDTYPE_PAUSE:
	case NETCOMMANDTYPE_RESUME:
		w.u32(c.frame);
		break;
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		w.u32(c.frame);
		if (wide) w.u16(c.releasedMask);
		else w.u8((std::uint8_t)c.releasedMask);
		for (int i = 0; i < (wide ? MAX_SLOTS : LEGACY_MAX_SLOTS); ++i)
		{
			if (c.releasedMask & (1u << i))
			{
				if ((size_t)i >= c.releasedFrames.size())
				{
					failWith(error, "DISCONNECTSCREENOFF missing frame for slot " + std::to_string(i));
					return {};
				}
				w.u32(c.releasedFrames[(size_t)i]);
			}
		}
		break;
	case NETCOMMANDTYPE_FRAMERESEND:
		w.u8(c.targetSlot);
		w.u32(c.frame);
		if (c.messages.size() > 0xFFFF)
		{
			failWith(error, "FRAMERESEND of " + std::to_string(c.messages.size()) + " commands");
			return {};
		}
		w.u16((std::uint16_t)c.messages.size());
		for (const std::shared_ptr<GameMessage> &m : c.messages)
		{
			if (!m || !writeGameMessage(w, *m, error))
			{
				return {};
			}
		}
		w.i32(c.runAheadChange);
		// lane ONLINE-3
		if (wide) w.u16(c.rejoinMask);
		else w.u8((std::uint8_t)c.rejoinMask);
		break;
	case NETCOMMANDTYPE_CHAT: // lane CHAT-1: u32 recipients, then the text (u32 length + UTF-16LE); a longer line is an error, never cut
		if (c.chatText.size() > NetChat::kMaxTextUnits)
		{
			failWith(error, "CHAT of " + std::to_string(c.chatText.size()) + " characters (at most " + std::to_string(NetChat::kMaxTextUnits) + ")");
			return {};
		}
		w.u32(c.playerMask);
		w.wstr(c.chatText);
		break;
	case NETCOMMANDTYPE_DISCONNECTCHAT: // lane CHAT-1: the text only (RotWK readDisconnectChatMessage RW 0x99A453: no mask)
		if (c.chatText.size() > NetChat::kMaxTextUnits)
		{
			failWith(error, "DISCONNECTCHAT of " + std::to_string(c.chatText.size()) + " characters (at most " + std::to_string(NetChat::kMaxTextUnits) + ")");
			return {};
		}
		w.wstr(c.chatText);
		break;
	case NETCOMMANDTYPE_PLAYERLEAVE:
	case NETCOMMANDTYPE_KEEPALIVE:
	case NETCOMMANDTYPE_LOADCOMPLETE:
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		break;
	default:
		failWith(error, "net command type " + std::to_string((int)c.type) + " has no encoding");
		return {};
	}
	return w.take();
}

bool NetPacket::decodeCommand(const std::vector<std::uint8_t> &bytes, NetCommandMsg &out, std::string *error)
{
	NetByteReader r(bytes);
	out = NetCommandMsg();
	const int wireType = r.u8();
	const bool wide = wireType == kDisconnectPlayer16 || wireType == kDisconnectScreenOff16 || wireType == kFrameResend16;
	const int type = wireType == kDisconnectPlayer16 ? NETCOMMANDTYPE_DISCONNECTPLAYER
		: wireType == kDisconnectScreenOff16 ? NETCOMMANDTYPE_DISCONNECTSCREENOFF
		: wireType == kFrameResend16 ? NETCOMMANDTYPE_FRAMERESEND : wireType;
	out.slot = r.u8();
	out.executionFrame = r.u32();
	if (r.failed())
	{
		return failWith(error, "truncated net command header");
	}
	if (out.slot >= MAX_SLOTS)
	{
		return failWith(error, "net command from unknown slot " + std::to_string(out.slot));
	}
	switch (type)
	{
	case NETCOMMANDTYPE_FRAMEINFO:
		out.commandCount = r.u16();
		break;
	case NETCOMMANDTYPE_GAMECOMMAND:
	{
		std::unique_ptr<GameMessage> m = readGameMessage(r, error);
		if (!m)
		{
			return false;
		}
		out.message = std::move(m);
		break;
	}
	case NETCOMMANDTYPE_DESYNCREPORT:
		out.payload = r.bytes();
		break;
	case NETCOMMANDTYPE_VIEW: // lane ANTICHEAT-1
		for (float &v : out.view)
		{
			v = r.f32();
		}
		out.viewFlags = r.u8();
		break;
	case NETCOMMANDTYPE_DESTROYPLAYER:
	case NETCOMMANDTYPE_REJOIN: // lane ONLINE-3
		out.targetSlot = r.u8();
		break;
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		out.targetSlot = r.u8();
		out.frame = r.u32();
		out.releasedMask = wide ? r.u16() : r.u8();
		break;
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		out.targetSlot = r.u8();
		out.frame = r.u32();
		break;
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	case NETCOMMANDTYPE_RUNAHEAD:
	case NETCOMMANDTYPE_REJOINREQUEST: // lane ONLINE-3
	case NETCOMMANDTYPE_PAUSE:
	case NETCOMMANDTYPE_RESUME:
		out.frame = r.u32();
		break;
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		out.frame = r.u32();
		out.releasedMask = wide ? r.u16() : r.u8();
		out.releasedFrames.assign(wide ? MAX_SLOTS : LEGACY_MAX_SLOTS, 0u);
		for (int i = 0; i < (wide ? MAX_SLOTS : LEGACY_MAX_SLOTS); ++i)
		{
			if (out.releasedMask & (1u << i))
			{
				out.releasedFrames[(size_t)i] = r.u32();
			}
		}
		break;
	case NETCOMMANDTYPE_FRAMERESEND:
	{
		out.targetSlot = r.u8();
		out.frame = r.u32();
		const std::uint16_t n = r.u16();
		for (std::uint16_t i = 0; i < n && !r.failed(); ++i)
		{
			std::unique_ptr<GameMessage> m = readGameMessage(r, error);
			if (!m)
			{
				return false;
			}
			out.messages.push_back(std::move(m));
		}
		out.runAheadChange = r.i32();
		// lane ONLINE-3
		out.rejoinMask = wide ? r.u16() : r.u8();
		break;
	}
	case NETCOMMANDTYPE_CHAT: // lane CHAT-1
		out.playerMask = r.u32();
		out.chatText = r.wstr();
		if (!r.failed() && out.chatText.size() > NetChat::kMaxTextUnits)
		{
			return failWith(error, "CHAT of " + std::to_string(out.chatText.size()) + " characters (at most " + std::to_string(NetChat::kMaxTextUnits) + ")");
		}
		break;
	case NETCOMMANDTYPE_DISCONNECTCHAT: // lane CHAT-1
		out.chatText = r.wstr();
		if (!r.failed() && out.chatText.size() > NetChat::kMaxTextUnits)
		{
			return failWith(error, "DISCONNECTCHAT of " + std::to_string(out.chatText.size()) + " characters (at most " + std::to_string(NetChat::kMaxTextUnits) + ")");
		}
		break;
	case NETCOMMANDTYPE_PLAYERLEAVE:
	case NETCOMMANDTYPE_KEEPALIVE:
	case NETCOMMANDTYPE_LOADCOMPLETE:
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		break;
	default:
		return failWith(error, "unknown net command type " + std::to_string(type));
	}
	out.type = (NetCommandType)type;
	const bool legacyEightSlotTarget = !wide && (type == NETCOMMANDTYPE_DISCONNECTPLAYER || type == NETCOMMANDTYPE_FRAMERESEND);
	if ((type == NETCOMMANDTYPE_DESTROYPLAYER || type == NETCOMMANDTYPE_DISCONNECTPLAYER || type == NETCOMMANDTYPE_DISCONNECTVOTE
			|| type == NETCOMMANDTYPE_FRAMERESEND || type == NETCOMMANDTYPE_REJOIN)
		&& out.targetSlot >= (legacyEightSlotTarget ? LEGACY_MAX_SLOTS : MAX_SLOTS) && !r.failed())
	{
		return failWith(error, std::string(NetCommandTypeName(type)) + " about slot " + std::to_string(out.targetSlot));
	}
	if (r.failed())
	{
		return failWith(error, std::string("truncated ") + NetCommandTypeName(type));
	}
	if (!r.atEnd())
	{
		return failWith(error, std::string(NetCommandTypeName(type)) + " with " + std::to_string(r.remaining()) + " trailing bytes");
	}
	return true;
}
