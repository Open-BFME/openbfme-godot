// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// NetPacket (ZH GameNetwork/NetPacket.cpp, NetCommandMsg.cpp), lane MP-1: the byte encoding of what the lockstep network carries: a GameMessage (ZH
// NetGameCommandMsg), the frame info (ZH NetFrameCommandMsg: how many commands a player sends for one execution frame), the leave notice (NetPlayerLeave
// CommandMsg), the load-complete notice, and the game a lobby starts (the NewGameMessage with its GameInfo).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): RotWK's NetCommandType names (GetAsciiNetCommandType, RW 0x989449) number ACKBOTH 0, ACKSTAGE1 1, ACKSTAGE2 2,
// FRAMEINFO 3, GAMECOMMAND 4, REQUEST_GAMESPY_STATS_AUTHKEY 5, GAMESPY_STATS_AUTHKEY 6, REQUESTPLAYERLEAVE 7, INFORMPLAYERLEAVEFRAME 8, REQUESTFRAMEDATA 9,
// PLAYERLEAVE 10, DESTROYPLAYER 11, KEEPALIVE 12, DISCONNECTCHAT 13, CHAT 14, PROGRESS 15, LOADCOMPLETE 16, TIMEOUTSTART 17, WRAPPER 18, FILE 19, HERO 20,
// FILEANNOUNCE 21, FILEPROGRESS 22, DISCONNECTKEEPALIVE 25, DISCONNECTPLAYER 26, DISCONNECTVOTE 27, DISCONNECTFRAME 28, SAVE_GAME 30. ZH's RUNAHEADMETRICS
// and RUNAHEAD commands are NOT in the list: RotWK does not exchange run-ahead changes (see Network.h, stop S-720). The type numbers below are RotWK's.
// The BYTE LAYOUT is OpenBFME's own (enhanced profile, PLAN): retail's packet encoding is cross-play gate 1 (XPLAY-1), not reproduced here (stop S-721).
//
// Determinism: everything is little-endian with explicit widths; floats travel as their bit patterns; a reader never trusts a length it did not check.

#pragma once

#include "GameLogic/GameMessage.h"
#include "GameNetwork/GameInfo.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// RotWK NetCommandType numbering (RW 0x989449); DESYNCREPORT is OpenBFME's own (the per-subsystem hash breakdown exchanged after a CRC mismatch).
// Lane MP-2 (the disconnect path, GameNetwork/DisconnectManager.h): DESTROYPLAYER 11, DISCONNECTKEEPALIVE 25, DISCONNECTPLAYER 26, DISCONNECTVOTE 27 and
// DISCONNECTFRAME 28 are RotWK's numbers. DISCONNECTSCREENOFF 29: RW 0x989449 names no type 29, the gap between DISCONNECTFRAME 28 and SAVE_GAME 30 where ZH's
// enum (NetworkDefs.h) has DISCONNECTSCREENOFF right after DISCONNECTFRAME (INFERENCE: the number; whether RotWK still sends it was not located, stop
// S-1121). FRAMERESEND is OpenBFME's own record of ZH ConnectionManager::sendFrameDataToPlayer (RotWK RW 0x8D4761: one sender slot's commands and count of
// one frame, relayed for another slot; retail wraps the original commands, OpenBFME's encoding is S-721).
enum NetCommandType : std::uint8_t
{
	NETCOMMANDTYPE_FRAMEINFO = 3,
	NETCOMMANDTYPE_GAMECOMMAND = 4,
	NETCOMMANDTYPE_PLAYERLEAVE = 10,
	NETCOMMANDTYPE_DESTROYPLAYER = 11,
	NETCOMMANDTYPE_KEEPALIVE = 12,
	NETCOMMANDTYPE_LOADCOMPLETE = 16,
	NETCOMMANDTYPE_DISCONNECTKEEPALIVE = 25,
	NETCOMMANDTYPE_DISCONNECTPLAYER = 26,
	NETCOMMANDTYPE_DISCONNECTVOTE = 27,
	NETCOMMANDTYPE_DISCONNECTFRAME = 28,
	NETCOMMANDTYPE_DISCONNECTSCREENOFF = 29,
	NETCOMMANDTYPE_DESYNCREPORT = 0x80,
	NETCOMMANDTYPE_FRAMERESEND = 0x81,
	NETCOMMANDTYPE_RUNAHEAD = 0x82 ///< lane MP-2: ZH's NETCOMMANDTYPE_RUNAHEAD (not in RotWK's list, S-720): a frame command of the packet router, `frame` = the new run-ahead
};
const char *NetCommandTypeName(int type);

class NetByteWriter
{
public:
	void u8(std::uint8_t v) { m_bytes.push_back(v); }
	void u16(std::uint16_t v)
	{
		u8((std::uint8_t)(v & 0xFF));
		u8((std::uint8_t)(v >> 8));
	}
	void u32(std::uint32_t v)
	{
		u16((std::uint16_t)(v & 0xFFFF));
		u16((std::uint16_t)(v >> 16));
	}
	void i32(std::int32_t v) { u32((std::uint32_t)v); }
	void f32(float v);
	void str(const std::string &s);
	void wstr(const std::u16string &s);
	void bytes(const std::vector<std::uint8_t> &b)
	{
		u32((std::uint32_t)b.size());
		m_bytes.insert(m_bytes.end(), b.begin(), b.end());
	}
	const std::vector<std::uint8_t> &data() const { return m_bytes; }
	std::vector<std::uint8_t> take() { return std::move(m_bytes); }

private:
	std::vector<std::uint8_t> m_bytes;
};

// reads what NetByteWriter wrote; any read past the end sets failed() and returns zero
class NetByteReader
{
public:
	NetByteReader(const std::uint8_t *data, size_t size) : m_data(data), m_size(size) {}
	explicit NetByteReader(const std::vector<std::uint8_t> &b) : m_data(b.data()), m_size(b.size()) {}
	std::uint8_t u8();
	// lane MP-2 (review): a boolean byte: 0 or 1; any other value fails the reader (badFlag) like a truncation, so no message applies it
	bool flag();
	std::uint16_t u16();
	std::uint32_t u32();
	std::int32_t i32() { return (std::int32_t)u32(); }
	float f32();
	std::string str();
	std::u16string wstr();
	std::vector<std::uint8_t> bytes();
	bool failed() const { return m_failed; }
	bool badFlag() const { return m_badFlag; } ///< the failure was a boolean byte other than 0 / 1
	bool atEnd() const { return m_pos == m_size; }
	size_t remaining() const { return m_size - m_pos; }

private:
	bool need(size_t n);
	const std::uint8_t *m_data;
	size_t m_size;
	size_t m_pos = 0;
	bool m_failed = false;
	bool m_badFlag = false;
};

namespace NetPacket
{
// The resource limit of one GameMessage: 1 << 20 arguments (a selection of a million objects: about 5 MiB of object ids, which binds before the transport's
// 8 MiB command limit). The HUD is never capped to fit: a bigger message is an error the session reports.
constexpr std::uint32_t kMaxGameMessageArguments = 1u << 20;
// GameMessage (format 2): u16 type, u8 player index (0xFF = -1), u32 argument count, then per argument a u8 GameMessageArgumentDataType and its value
// (format 1 had a u8 count: a selection of 255 objects has 256 arguments). Only the argument types GameMessage can hold are written; false + *error for any
// other (never a silent truncation)
bool writeGameMessage(NetByteWriter &w, const GameMessage &m, std::string *error);
// null + *error on a malformed or truncated message, a type outside 1001 .. 1147 or an unknown argument type
std::unique_ptr<GameMessage> readGameMessage(NetByteReader &r, std::string *error);

void writeGameInfo(NetByteWriter &w, const SkirmishGameInfo &g);
bool readGameInfo(NetByteReader &r, SkirmishGameInfo &g, std::string *error);
void writeNewGameMessage(NetByteWriter &w, const NewGameMessage &m);
bool readNewGameMessage(NetByteReader &r, NewGameMessage &m, std::string *error);
} // namespace NetPacket

// One network command (ZH NetCommandMsg and its subclasses, reduced to the commands this lane exchanges)
struct NetCommandMsg
{
	NetCommandType type = NETCOMMANDTYPE_KEEPALIVE;
	std::uint8_t slot = 0;              ///< the sending player's slot (ZH getPlayerID)
	std::uint32_t executionFrame = 0;   ///< ZH getExecutionFrame: the logic frame the command executes on (frame info: the frame it counts)
	std::uint16_t commandCount = 0;     ///< FRAMEINFO: the number of GAMECOMMANDs the slot sends for executionFrame (ZH NetFrameCommandMsg::getCommandCount)
	std::shared_ptr<GameMessage> message; ///< GAMECOMMAND
	std::vector<std::uint8_t> payload;  ///< DESYNCREPORT: the encoded report half
	// lane MP-2 (ZH NetDestroyPlayerCommandMsg / NetDisconnectPlayerCommandMsg / NetDisconnectVoteCommandMsg / NetDisconnectFrameCommandMsg /
	// NetDisconnectScreenOffCommandMsg): DESTROYPLAYER, DISCONNECTPLAYER, DISCONNECTVOTE: the slot the command is about; FRAMERESEND: the slot whose commands
	// are relayed
	std::uint8_t targetSlot = 0;
	// DISCONNECTPLAYER: the disconnect frame; DISCONNECTVOTE: the vote frame; DISCONNECTFRAME: the frame the sender is on; DISCONNECTSCREENOFF: the new frame;
	// FRAMERESEND: the frame of the relayed commands (executionFrame stays the header field of the others)
	std::uint32_t frame = 0;
	// DISCONNECTSCREENOFF (OpenBFME addition, S-1121): the disconnect frames the sender had received when it released them (bit per slot) and those frames
	std::uint8_t releasedMask = 0;
	std::vector<std::uint32_t> releasedFrames;
	// FRAMERESEND: the relayed slot's commands of `frame` (its FRAMEINFO count is their number, plus one with a run-ahead change)
	std::vector<std::shared_ptr<GameMessage>> messages;
	std::int32_t runAheadChange = -1; ///< FRAMERESEND: the slot's RUNAHEAD of that frame (-1 none)
};

namespace NetPacket
{
std::vector<std::uint8_t> encodeCommand(const NetCommandMsg &c, std::string *error);
bool decodeCommand(const std::vector<std::uint8_t> &bytes, NetCommandMsg &out, std::string *error);
} // namespace NetPacket
