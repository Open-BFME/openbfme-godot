// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical input (client side): the durable records the relay
// publishes become the lockstep driver's per-frame command list here. The
// RelayMembership::Cursor applies stamps and stages membership before its
// frame; for each accepted Frame record this adapter decodes the OBFB
// bundle, validates it against the staged membership, parses every order's
// serialized GameMessage and binds its player to the sender slot's player
// index — a GameMessage claiming another player rejects the whole record,
// as a sender's identity is never trusted from the payload.
//
// The caller (Network's canonical mode, or the LAN host running the same
// code in-process) feeds records in journal order and consumes each frame's
// input exactly once: first the departed slots as MSG_SELF_DESTRUCT(true)
// in ascending slot order, then the frame's commands in (slot, id) order.
// A rejected record is a protocol error: nothing is applied, the game is
// not silently continued.
#pragma once

#include "GameLogic/GameMessage.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayMembership.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace RelayCanonicalClient
{
// The command list one canonical frame runs, already decoded and rebound.
struct FrameInput
{
	RelayCommit::Stamp stamp;
	std::uint16_t departed = 0;   ///< apply as MSG_SELF_DESTRUCT(true), ascending slot order
	struct Command
	{
		std::uint8_t slot = 0;
		std::uint64_t id = 0;
		std::unique_ptr<GameMessage> message;
	};
	std::vector<Command> commands;   ///< (slot, id) order, as the bundle requires
};

class Client
{
public:
	// slotPlayerIndex: the GameInfo slot -> logic player index agreed on every
	// peer (NetworkConfig::slotPlayerIndex); -1 for a slot that is not a
	// network player.
	Client(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask,
		std::uint32_t firstFrame, const std::array<int, 16> &slotPlayerIndex);

	// Feed the next durable record. True when the cursor consumed it; false +
	// error is a validation or ordering failure (the record changes nothing).
	bool accept(const RelayCommit::Record &record, std::string &error);

	// The input of every accepted frame, exactly once, in stamp order.
	bool takeFrameInput(FrameInput &out);

	const RelayMembership::Snapshot &applied() const { return m_cursor.applied(); }
	std::uint32_t nextFrame() const { return m_cursor.nextFrame(); }
	bool ended() const { return m_cursor.ended(); }

private:
	RelayMembership::Cursor m_cursor;
	std::array<int, 16> m_slotPlayerIndex;
	std::deque<FrameInput> m_pending;
};
}
