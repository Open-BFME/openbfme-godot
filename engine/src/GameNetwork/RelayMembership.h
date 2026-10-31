// OpenBFME. GPL-3.0.
#pragma once

#include "GameNetwork/RelayCommit.h"

#include <array>
#include <functional>

namespace RelayMembership
{
struct Snapshot
{
	std::uint16_t slotMask = 0, active = 0, held = 0, departed = 0, loaded = 0;
	std::array<std::uint16_t, 16> ticketGenerations{};
	bool paused = false, clockValid = false;
	std::uint64_t originMs = 0;
	std::uint32_t firstFrame = 0;
};

// OBMS, LE u16 version=1, u8 flags (paused=1, clockValid=2), u8 reserved=0;
// five LE u16 masks in declaration order, sixteen LE u16 ticket generations,
// LE u64 decision-time originMs, LE u32 firstFrame. Exactly 62 bytes.
bool encode(const Snapshot &snapshot, std::vector<std::uint8_t> &bytes, std::string &error);
bool decode(const std::vector<std::uint8_t> &bytes, Snapshot &snapshot, std::string &error);

using FrameValidator = std::function<bool(const RelayCommit::Record &, const Snapshot &, std::string &)>;

// Receives only durable records from the authenticated authority (its game log).
// Membership is staged at nextFrame, before its Frame record. Frame validation is
// read-only; rejection changes nothing. Apply departedAtFrame() in ascending slot
// order as MSG_SELF_DESTRUCT(true), then the decoded frame commands, exactly once.
class Cursor
{
public:
	Cursor(std::uint64_t game, std::uint64_t epoch, std::uint16_t slotMask, std::uint32_t firstFrame);
	bool accept(const RelayCommit::Record &record, const FrameValidator &validateFrame, std::string &error);
	const Snapshot &staged() const { return m_staged; }
	const Snapshot &applied() const { return m_applied; }
	const RelayCommit::Stamp &stamp() const { return m_stamp; }
	std::uint16_t departedAtFrame() const { return m_departedAtFrame; }
	std::uint32_t nextFrame() const { return m_nextFrame; }
	bool ended() const { return m_ended; }
	bool hasAppliedFrame() const { return m_hasAppliedFrame; }

private:
	RelayCommit::Stamp m_stamp;
	Snapshot m_staged, m_applied;
	std::uint16_t m_slotMask = 0, m_departedAtFrame = 0;
	std::uint32_t m_nextFrame = 0;
	bool m_hasMembership = false, m_hasAppliedFrame = false, m_ended = false;
};
}
