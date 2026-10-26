// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical input; see RelayCanonicalClient.h for the consumption
// contract and the player-binding rule.

#include "GameNetwork/RelayCanonicalClient.h"

#include "GameNetwork/NetPacket.h"

#include <utility>

namespace RelayCanonicalClient
{
Client::Client(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask,
	std::uint32_t firstFrame, const std::array<int, 16> &slotPlayerIndex)
	: m_cursor(game, epoch, slotMask, firstFrame)
	, m_slotPlayerIndex(slotPlayerIndex)
{
}

bool Client::accept(const RelayCommit::Record &record, std::string &error)
{
	// The validator is read-only on the cursor but may capture the decoded
	// work: a frame that fails decode, bundle validation, GameMessage parsing
	// or player binding is never consumed.
	FrameInput input;
	bool isFrame = false;
	const auto validate = [&](const RelayCommit::Record &candidate,
		const RelayMembership::Snapshot &membership, std::string &validateError) -> bool {
		if (candidate.kind != RelayCommit::Kind::Frame)
			return true;   // membership and lifecycle records need no bundle check
		isFrame = true;
		RelayFrameBundle::Frame frame;
		if (!RelayFrameBundle::decode(candidate.payload, frame, validateError))
			return false;
		if (!RelayFrameBundle::validate(frame, candidate.stamp, membership, validateError))
			return false;
		input.commands.reserve(frame.orders.size());
		for (const auto &order : frame.orders)
		{
			if (order.slot >= m_slotPlayerIndex.size() || m_slotPlayerIndex[order.slot] < 0)
				return (validateError = "an order for slot " + std::to_string(order.slot)
					+ " with no network player"), false;
			NetByteReader reader(order.bytes.data(), order.bytes.size());
			auto message = NetPacket::readGameMessage(reader, &validateError);
			if (!message)
				return false;
			// The sender's identity comes from the authenticated slot, never
			// the payload; a mismatched claim rejects the whole record.
			if (message->getPlayerIndex() != m_slotPlayerIndex[order.slot])
				return (validateError = "order " + std::to_string(order.id) + " of slot "
					+ std::to_string(order.slot) + " claims player "
					+ std::to_string(message->getPlayerIndex())), false;
			FrameInput::Command command;
			command.slot = order.slot;
			command.id = order.id;
			command.message = std::move(message);
			input.commands.push_back(std::move(command));
		}
		return true;
	};
	const std::uint32_t before = m_cursor.nextFrame();
	if (!m_cursor.accept(record, validate, error))
		return false;
	if (isFrame && m_cursor.nextFrame() != before)
	{
		input.stamp = m_cursor.stamp();
		input.departed = m_cursor.departedAtFrame();
		m_pending.push_back(std::move(input));
	}
	return true;
}

bool Client::takeFrameInput(FrameInput &out)
{
	if (m_pending.empty())
		return false;
	out = std::move(m_pending.front());
	m_pending.pop_front();
	return true;
}
}
