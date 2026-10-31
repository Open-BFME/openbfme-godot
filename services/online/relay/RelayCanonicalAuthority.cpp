// OpenBFME. GPL-3.0.
//
// Lane NET-SYNC: the canonical game's relay-side authority (see RelayCanonicalAuthority.h for the contract).

#include "RelayCanonicalAuthority.h"

namespace
{
constexpr std::uint32_t kFirstFrame = 1; // frames publish from 1; frame 0 is the load barrier's (later lifecycle work)
constexpr std::size_t kPollDatagrams = 64; // one service round drains this many channel packets per member
constexpr std::size_t kMaxErrors = 64;
} // namespace

RelayCanonicalAuthority::RelayCanonicalAuthority(const Options &options)
	: m_options(options)
	, m_clock(options.clockOriginMs, kFirstFrame, options.frameMs)
	, m_builder(options.game, options.epoch, options.slotMask, kFirstFrame)
	, m_scheduler(options.policy, kFirstFrame)
{
	m_membership.slotMask = options.slotMask;
	m_membership.originMs = options.clockOriginMs;
	m_membership.firstFrame = kFirstFrame;
	m_membership.clockValid = true; // the clock runs from the game's start; pause arrives with its lifecycle record
	if (!m_options.append)
	{
		fail("canonical game has no game-log append");
	}
}

bool RelayCanonicalAuthority::bind(std::uint8_t slot, std::uint16_t generation, bool rejoin, std::string &error)
{
	if (!(m_options.slotMask & (1u << slot)) || m_membership.departed & (1u << slot))
	{
		error = "canonical bind of slot " + std::to_string(slot) + " outside the game's mask (or already departed)";
		return false;
	}
	// a same-generation rebinding keeps its channels; a rejoin's new incarnation starts them afresh
	if (rejoin || !m_members.count(slot))
	{
		m_members.erase(slot);
		m_members.try_emplace(slot, m_options.channel);
	}
	if (m_membership.held & (1u << slot))
	{
		m_membership.held &= (std::uint16_t)~(1u << slot);
	}
	if (!(m_membership.active & (1u << slot)))
	{
		m_membership.active |= (std::uint16_t)(1u << slot);
		m_membership.loaded |= (std::uint16_t)(1u << slot); // the load barrier is lifecycle work: bound counts as loaded for now
	}
	m_membership.ticketGenerations[slot] = generation;
	m_membershipDirty = true;
	return true;
}

void RelayCanonicalAuthority::depart(std::uint8_t slot)
{
	m_membership.active &= (std::uint16_t)~(1u << slot);
	m_membership.held &= (std::uint16_t)~(1u << slot);
	m_membership.loaded &= (std::uint16_t)~(1u << slot);
	m_membership.departed |= (std::uint16_t)(1u << slot);
	m_membershipDirty = true;
}

void RelayCanonicalAuthority::hold(std::uint8_t slot)
{
	if (!(m_membership.active & (1u << slot)) || (m_membership.departed & (1u << slot)))
	{
		return;
	}
	m_membership.held |= (std::uint16_t)(1u << slot);
	m_membershipDirty = true;
}

void RelayCanonicalAuthority::receive(std::uint8_t slot, const std::uint8_t *data, std::size_t size, std::uint64_t nowMs,
	std::vector<Emission> &out)
{
	if (m_failure.size())
	{
		return;
	}
	auto member = m_members.find(slot);
	if (member == m_members.end())
	{
		return; // the binding check is the caller's; an unbound canonical slot speaks to no one
	}
	RelayBundleChannel::Bytes ack;
	std::string error;
	if (!member->second.channel.receive(RelayBundleChannel::Bytes(data, data + size), nowMs, ack, error))
	{
		if (m_errors.size() < kMaxErrors)
		{
			m_errors.push_back("canonical slot " + std::to_string(slot) + " channel: " + error);
		}
		return;
	}
	if (!ack.empty())
	{
		out.emplace_back(slot, std::move(ack));
	}
	std::string ingressError;
	for (const RelayBundleChannel::Message &delivered : member->second.channel.takeDelivered())
	{
		RelayCanonicalInput::Message ingress;
		if (!RelayCanonicalInput::decode(delivered.bytes, ingress, ingressError))
		{
			if (m_errors.size() < kMaxErrors)
			{
				m_errors.push_back("canonical slot " + std::to_string(slot) + " ingress: " + ingressError);
			}
			continue;
		}
		const RelayFrameBundle::Builder::Admission admission =
			m_scheduler.admit(slot, ingress, nowMs, m_clock, m_builder, ingressError);
		if (admission != RelayFrameBundle::Builder::Admission::Accepted && m_errors.size() < kMaxErrors)
		{
			m_errors.push_back("canonical slot " + std::to_string(slot) + " order " + std::to_string(ingress.id) +
				" not admitted (" + std::to_string((int)admission) + "): " + ingressError);
		}
	}
	pump(nowMs, out);
}

void RelayCanonicalAuthority::service(std::uint64_t nowMs, std::vector<Emission> &out)
{
	if (m_failure.size())
	{
		return;
	}
	for (auto &m : m_members)
	{
		std::string error;
		for (RelayBundleChannel::Bytes &packet : m.second.channel.poll(nowMs, kPollDatagrams, &error))
		{
			out.emplace_back(m.first, std::move(packet));
		}
		if (!error.empty() && m_errors.size() < kMaxErrors)
		{
			m_errors.push_back("canonical slot " + std::to_string(m.first) + " channel poll: " + error);
		}
	}
	pump(nowMs, out);
}

bool RelayCanonicalAuthority::finish(std::string &error)
{
	// anything still staged or sealed lands first — the End record is the log's tail, behind every frame
	while (!m_outbox.empty())
	{
		const RelayCommit::Record &front = m_outbox.front();
		if (!m_options.append(front, error))
		{
			fail("canonical game log append record " + std::to_string(front.stamp.sequence) + ": " + error);
			return false;
		}
		m_outbox.pop_front();
	}
	RelayCommit::Record record;
	record.kind = RelayCommit::Kind::End;
	record.stamp.game = m_options.game;
	record.stamp.epoch = m_options.epoch;
	record.stamp.sequence = ++m_sequence;
	record.stamp.frame = m_builder.nextUnpublishedFrame();
	record.stamp.membershipGeneration = m_membershipGeneration;
	return m_options.append(record, error);
}

void RelayCanonicalAuthority::queueMembership(std::vector<Emission> &out)
{
	RelayCommit::Record record;
	record.kind = RelayCommit::Kind::Membership;
	record.stamp.game = m_options.game;
	record.stamp.epoch = m_options.epoch;
	record.stamp.sequence = ++m_sequence;
	record.stamp.frame = m_builder.nextUnpublishedFrame();
	record.stamp.membershipGeneration = ++m_membershipGeneration;
	m_membershipDirty = false;
	m_outbox.push_back(std::move(record)); // the OBMS payload is encoded at append time (pump)
	(void)out;
}

void RelayCanonicalAuthority::pump(std::uint64_t nowMs, std::vector<Emission> &out)
{
	if (m_failure.size())
	{
		return;
	}
	if (m_membershipDirty)
	{
		queueMembership(out);
	}
	// seal check first when the outbox holds no frame record: the next record is a frame
	bool framePending = false;
	for (const RelayCommit::Record &pending : m_outbox)
	{
		framePending = framePending || pending.kind == RelayCommit::Kind::Frame;
	}
	if (!framePending)
	{
		std::string error;
		if (m_scheduler.sealDue(nowMs, m_clock, m_membership, m_builder, error))
		{
			RelayCommit::Record record;
			record.kind = RelayCommit::Kind::Frame;
			record.stamp.game = m_options.game;
			record.stamp.epoch = m_options.epoch;
			record.stamp.sequence = m_sequence + 1;
			record.stamp.frame = m_builder.nextUnpublishedFrame();
			record.stamp.membershipGeneration = m_membershipGeneration;
			if (m_builder.prepare(record.stamp, m_membership, record, error))
			{
				++m_sequence;
				m_outbox.push_back(std::move(record));
			}
			else if (m_errors.size() < kMaxErrors)
			{
				m_errors.push_back("canonical prepare: " + error);
			}
		}
		else if (!error.empty() && m_errors.size() < kMaxErrors)
		{
			m_errors.push_back("canonical seal check: " + error);
		}
	}
	// the game-log drain (ROUTER-1: synchronous, one write per record; a completed append publishes and releases the
	// builder). The append is durable before any byte is fanned out — the relay's OBFMERLG log, not a journal.
	while (!m_outbox.empty())
	{
		RelayCommit::Record &front = m_outbox.front();
		std::string error;
		if (front.kind == RelayCommit::Kind::Membership && front.payload.empty())
		{
			if (!RelayMembership::encode(m_membership, front.payload, error))
			{
				fail("canonical membership encode: " + error);
				return;
			}
		}
		if (!m_options.append(front, error))
		{
			fail("canonical game log append record " + std::to_string(front.stamp.sequence) + ": " + error);
			return;
		}
		fanout(front, out);
		m_outbox.pop_front();
	}
}

void RelayCanonicalAuthority::fanout(const RelayCommit::Record &record, std::vector<Emission> & /*out*/)
{
	if (record.kind == RelayCommit::Kind::Frame)
	{
		std::string error;
		if (!m_builder.committed(record.stamp, error))
		{
			fail("canonical committed: " + error);
			return;
		}
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!RelayCommit::encode(record, bytes, error))
	{
		fail("canonical record encode: " + error);
		return;
	}
	for (auto &m : m_members)
	{
		if (!m.second.channel.queue(bytes, error) && m_errors.size() < kMaxErrors)
		{
			m_errors.push_back("canonical slot " + std::to_string(m.first) + " queue: " + error);
		}
	}
	// the queued records go out on the next poll round of each member's channel
}

void RelayCanonicalAuthority::fail(const std::string &why)
{
	if (m_failure.empty())
	{
		m_failure = why;
		m_builder.fail(why);
		m_errors.push_back(why);
	}
}
