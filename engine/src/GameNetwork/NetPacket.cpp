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
	if (m.getType() <= MSG_BEGIN_NETWORK_MESSAGES || m.getType() >= MSG_END_NETWORK_MESSAGES)
	{
		return failWith(error, "GameMessage type " + std::to_string(m.getType()) + " is not a logic message (1001 .. 1147)");
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
	if (type <= MSG_BEGIN_NETWORK_MESSAGES || type >= MSG_END_NETWORK_MESSAGES)
	{
		failWith(error, "GameMessage type " + std::to_string(type) + " is not a logic message (1001 .. 1147)");
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
	for (const SkirmishGameSlot &s : g.slots)
	{
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
}

bool NetPacket::readGameInfo(NetByteReader &r, SkirmishGameInfo &g, std::string *error)
{
	for (SkirmishGameSlot &s : g.slots)
	{
		const std::int32_t state = r.i32();
		if (state < SLOT_OPEN || state > SLOT_PLAYER)
		{
			return failWith(error, "GameInfo slot state " + std::to_string(state));
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
	if (r.failed())
	{
		return failWith(error, r.badFlag() ? "GameInfo with a flag that is not 0 / 1" : "truncated GameInfo");
	}
	// lane MP-2 (review): every slot field is -1 (random / none) or a real value
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = g.slots[i];
		for (int v : { s.color, s.startPos, s.playerTemplate, s.teamNumber, s.origColor, s.origStartPos, s.origPlayerTemplate })
		{
			if (v < -1)
			{
				return failWith(error, "GameInfo slot " + std::to_string(i) + " has the field value " + std::to_string(v));
			}
		}
	}
	if (g.startingCash < 0)
	{
		return failWith(error, "GameInfo starting cash " + std::to_string(g.startingCash));
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

bool NetPacket::readNewGameMessage(NetByteReader &r, NewGameMessage &m, std::string *error)
{
	const std::uint8_t mode = r.u8();
	if (mode > 1)
	{
		return failWith(error, "NewGameMessage mode " + std::to_string(mode));
	}
	m.mode = mode ? NewGameMode::Skirmish : NewGameMode::SinglePlayer;
	m.difficulty = r.i32();
	m.rankPoints = r.i32();
	return readGameInfo(r, m.game, error);
}

std::vector<std::uint8_t> NetPacket::encodeCommand(const NetCommandMsg &c, std::string *error)
{
	NetByteWriter w;
	w.u8((std::uint8_t)c.type);
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
	case NETCOMMANDTYPE_DESTROYPLAYER:
		w.u8(c.targetSlot);
		break;
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		w.u8(c.targetSlot);
		w.u32(c.frame);
		w.u8(c.releasedMask); // lane MP-2 (review): the slots before the sender in the fallback order that the sender holds gone
		break;
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		w.u8(c.targetSlot);
		w.u32(c.frame);
		break;
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	case NETCOMMANDTYPE_RUNAHEAD:
		w.u32(c.frame);
		break;
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		w.u32(c.frame);
		w.u8(c.releasedMask);
		for (int i = 0; i < 8; ++i)
		{
			if (c.releasedMask & (1u << i))
			{
				w.u32((size_t)i < c.releasedFrames.size() ? c.releasedFrames[(size_t)i] : 0u);
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
	const int type = r.u8();
	out.slot = r.u8();
	out.executionFrame = r.u32();
	if (r.failed())
	{
		return failWith(error, "truncated net command header");
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
	case NETCOMMANDTYPE_DESTROYPLAYER:
		out.targetSlot = r.u8();
		break;
	case NETCOMMANDTYPE_DISCONNECTPLAYER:
		out.targetSlot = r.u8();
		out.frame = r.u32();
		out.releasedMask = r.u8();
		break;
	case NETCOMMANDTYPE_DISCONNECTVOTE:
		out.targetSlot = r.u8();
		out.frame = r.u32();
		break;
	case NETCOMMANDTYPE_DISCONNECTFRAME:
	case NETCOMMANDTYPE_RUNAHEAD:
		out.frame = r.u32();
		break;
	case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
		out.frame = r.u32();
		out.releasedMask = r.u8();
		out.releasedFrames.assign(8, 0u);
		for (int i = 0; i < 8; ++i)
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
		break;
	}
	case NETCOMMANDTYPE_PLAYERLEAVE:
	case NETCOMMANDTYPE_KEEPALIVE:
	case NETCOMMANDTYPE_LOADCOMPLETE:
	case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		break;
	default:
		return failWith(error, "unknown net command type " + std::to_string(type));
	}
	out.type = (NetCommandType)type;
	if ((type == NETCOMMANDTYPE_DESTROYPLAYER || type == NETCOMMANDTYPE_DISCONNECTPLAYER || type == NETCOMMANDTYPE_DISCONNECTVOTE
			|| type == NETCOMMANDTYPE_FRAMERESEND)
		&& out.targetSlot >= MAX_SLOTS && !r.failed())
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
