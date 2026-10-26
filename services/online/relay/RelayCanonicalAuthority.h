// OpenBFME. GPL-3.0.
//
// Lane NET-SYNC (ROUTER-1): the canonical game's relay-side authority. While a game is bound canonical
// (RelayProtocol.h: TICKET_MODE_CANONICAL in its tickets), the relay does not relay per-sender streams: every player
// DATA datagram is a RelayBundleChannel packet to this authority. The channel reassembles the client's OBCI ingress
// (RelayCanonicalInput: per-slot dense order ids and beats); the scheduler stamps each order's execution frame
// (late-order policy A EqualDelay or B NextFrame, from the game's ticket mode); sealed frames are appended to the
// game's OBFMERLG log durably and only then fanned out to every bound member as OBRC records through the same
// channels (the source branch's fsync-per-frame commit journal is dropped: the game log already records every frame
// command for rejoin and replays — ROUTER-1 makes canonical records land in it instead).
//
// The RelayCore owns one of these per canonical game and maps the emitted (slot, packet) pairs onto the slots' bound
// addresses inside DATA_FROM envelopes. Membership changes (a player bound, held, resumed or departed) are themselves
// committed records, staged ahead of the frame they take effect at. No peer forwarding, no client-side scheduling: a
// packet that is not a well-formed channel packet or OBCI message is an error line, never a silent drop.

#pragma once

#include "GameNetwork/RelayBundleChannel.h"
#include "GameNetwork/RelayCanonicalInput.h"
#include "GameNetwork/RelayCommit.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayFrameClock.h"
#include "GameNetwork/RelayFrameScheduler.h"
#include "GameNetwork/RelayMembership.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class RelayCanonicalAuthority
{
public:
	struct Options
	{
		std::uint64_t game = 0;
		std::uint64_t epoch = 1;                      ///< the fencing generation; fixed 1 until failover exists
		std::uint16_t slotMask = 0;
		RelayFrameScheduler::Policy policy = RelayFrameScheduler::Policy::NextFrame;
		std::uint64_t clockOriginMs = 0;              ///< the frame clock's origin (the game's start)
		std::uint64_t frameMs = RelayFrameClock::kFrameMs; ///< the game's own frame period (from its START; ROUTER-1)
		RelayBundleChannel::Options channel;
		// ROUTER-1: the synchronous durable append: encode and land the record in the game's OBFMERLG log
		// (RelayCore supplies it). false -> the game's log stopped; the authority fails, never publishes.
		std::function<bool(const RelayCommit::Record &record, std::string &error)> append;
	};

	// Datagrams the relay sends, addressed by bound slot.
	using Emission = std::pair<std::uint8_t, std::vector<std::uint8_t>>;

	explicit RelayCanonicalAuthority(const Options &options);

	// A player slot bound. `rejoin` is true for a new incarnation (a higher-generation ticket: its channels start
	// afresh); a same-generation rebinding keeps them. `generation` is the ticket's.
	bool bind(std::uint8_t slot, std::uint16_t generation, bool rejoin, std::string &error);
	// The slot's explicit leave (BYE): active -> departed at the next membership commit.
	void depart(std::uint8_t slot);
	// The slot went silent (the relay's timeout): active -> held (it may rejoin).
	void hold(std::uint8_t slot);

	// An NS channel packet arrived on `slot`'s binding (the RelayCore authenticated the address). Emitted packets
	// (ACKs, retries, record fanout) go to `out` in send order.
	void receive(std::uint8_t slot, const std::uint8_t *data, std::size_t size, std::uint64_t nowMs,
		std::vector<Emission> &out);
	// Retries, seal checks, log appends and channel polls. Call often (every input batch and on tick).
	void service(std::uint64_t nowMs, std::vector<Emission> &out);

	// The game is closing: drain the outbox and append the committed End record — the log tail then says the
	// game's input is complete (replays and rejoins reproduce the close). No fanout is emitted.
	bool finish(std::string &error);

	bool failed() const { return !m_failure.empty(); }
	const std::string &failure() const { return m_failure; }
	const std::vector<std::string> &errors() const { return m_errors; }
	std::uint64_t sequence() const { return m_sequence; }
	const RelayFrameBundle::Builder::Counters &counters() const { return m_builder.counters(); }
	const RelayFrameScheduler::Scheduler &scheduler() const { return m_scheduler; }
	// test/diagnostics: the slot's channel and the current membership snapshot
	const RelayMembership::Snapshot &membership() const { return m_membership; }
	const Options &options() const { return m_options; }

private:
	struct Member
	{
		bool bound = false;
		RelayBundleChannel channel;
		Member(const RelayBundleChannel::Options &o) : channel(o) {}
	};

	// Outbound OBRC records, appended to the game log in order before any byte is published.
	void queueMembership(std::vector<Emission> &out);
	void pump(std::uint64_t nowMs, std::vector<Emission> &out);
	void fanout(const RelayCommit::Record &record, std::vector<Emission> &out);
	void fail(const std::string &why);

	Options m_options;
	RelayFrameClock m_clock;
	RelayFrameBundle::Builder m_builder;
	RelayFrameScheduler::Scheduler m_scheduler;
	RelayMembership::Snapshot m_membership;
	std::map<std::uint8_t, Member> m_members;
	std::deque<RelayCommit::Record> m_outbox;   // sealed frames + membership deltas awaiting the log append
	std::uint64_t m_sequence = 0, m_membershipGeneration = 0;
	bool m_membershipDirty = false;
	std::vector<std::string> m_errors;
	std::string m_failure;
};
