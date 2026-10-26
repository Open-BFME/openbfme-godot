// OpenBFME online relay. GPL-3.0.

#include "GameNetwork/RelayCommit.h"

#include "Common/Sha256.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace RelayCommit
{
namespace
{
constexpr std::size_t kPrefixBytes = kRecordOverhead - 32;
constexpr std::size_t kChecksumBytes = 32;

bool knownKind(std::uint8_t kind)
{
	return kind == static_cast<std::uint8_t>(Kind::Frame) || kind == static_cast<std::uint8_t>(Kind::Membership)
		|| kind == static_cast<std::uint8_t>(Kind::End);
}

void put16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
	bytes.push_back(static_cast<std::uint8_t>(value));
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
	{
		bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
	}
}

void put64(std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
	for (unsigned i = 0; i < 8; ++i)
	{
		bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
	}
}

std::uint16_t get16(const std::uint8_t *bytes)
{
	return static_cast<std::uint16_t>(bytes[0]) | static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t get32(const std::uint8_t *bytes)
{
	std::uint32_t value = 0;
	for (unsigned i = 0; i < 4; ++i)
	{
		value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
	}
	return value;
}

std::uint64_t get64(const std::uint8_t *bytes)
{
	std::uint64_t value = 0;
	for (unsigned i = 0; i < 8; ++i)
	{
		value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
	}
	return value;
}

bool fail(std::string &error, const char *message)
{
	error = message;
	return false;
}
} // namespace

bool encode(const Record &record, std::vector<std::uint8_t> &bytes, std::string &error)
{
	const std::uint8_t kind = static_cast<std::uint8_t>(record.kind);
	if (!record.stamp.game || !record.stamp.epoch || !record.stamp.sequence)
	{
		return fail(error, "game, epoch, and sequence must be nonzero");
	}
	if (!knownKind(kind))
	{
		return fail(error, "unknown record kind");
	}
	if (record.payload.size() > std::numeric_limits<std::uint32_t>::max()
		|| record.payload.size() > std::numeric_limits<std::size_t>::max() - kRecordOverhead)
	{
		return fail(error, "payload is too large");
	}

	try
	{
		std::vector<std::uint8_t> encoded;
		encoded.reserve(kRecordOverhead + record.payload.size());
		encoded.insert(encoded.end(), { 'O', 'B', 'R', 'C' });
		put16(encoded, kVersion);
		encoded.push_back(kind);
		encoded.push_back(0);
		put64(encoded, record.stamp.game);
		put64(encoded, record.stamp.epoch);
		put64(encoded, record.stamp.sequence);
		put32(encoded, record.stamp.frame);
		put64(encoded, record.stamp.membershipGeneration);
		put32(encoded, static_cast<std::uint32_t>(record.payload.size()));
		encoded.insert(encoded.end(), record.payload.begin(), record.payload.end());
		const std::array<std::uint8_t, kChecksumBytes> checksum = Sha256::hash(encoded.data(), encoded.size());
		encoded.insert(encoded.end(), checksum.begin(), checksum.end());
		bytes.swap(encoded);
		error.clear();
		return true;
	}
	catch (const std::bad_alloc &)
	{
		return fail(error, "record encoding allocation failed");
	}
	catch (const std::length_error &)
	{
		return fail(error, "record is too large");
	}
}

bool decode(const std::vector<std::uint8_t> &bytes, size_t maxPayloadBytes, Record &record, std::string &error)
{
	if (bytes.size() < kRecordOverhead)
	{
		return fail(error, "record is shorter than its header and checksum");
	}
	if (bytes[0] != 'O' || bytes[1] != 'B' || bytes[2] != 'R' || bytes[3] != 'C')
	{
		return fail(error, "invalid record magic");
	}
	if (get16(bytes.data() + 4) != kVersion)
	{
		return fail(error, "unsupported record version");
	}
	if (!knownKind(bytes[6]))
	{
		return fail(error, "unknown record kind");
	}
	if (bytes[7] != 0)
	{
		return fail(error, "reserved record byte is nonzero");
	}

	const std::uint32_t payloadBytes = get32(bytes.data() + 44);
	const std::size_t availablePayloadBytes = bytes.size() - kRecordOverhead;
	if (payloadBytes > maxPayloadBytes || payloadBytes != availablePayloadBytes)
	{
		return fail(error, "record length does not match its bounded payload");
	}

	Stamp stamp;
	stamp.game = get64(bytes.data() + 8);
	stamp.epoch = get64(bytes.data() + 16);
	stamp.sequence = get64(bytes.data() + 24);
	stamp.frame = get32(bytes.data() + 32);
	stamp.membershipGeneration = get64(bytes.data() + 36);
	if (!stamp.game || !stamp.epoch || !stamp.sequence)
	{
		return fail(error, "game, epoch, and sequence must be nonzero");
	}

	const std::size_t payloadEnd = kPrefixBytes + static_cast<std::size_t>(payloadBytes);
	const std::array<std::uint8_t, kChecksumBytes> checksum = Sha256::hash(bytes.data(), payloadEnd);
	if (!ConstantTimeEqual(checksum.data(), bytes.data() + payloadEnd, checksum.size()))
	{
		return fail(error, "record checksum mismatch");
	}

	try
	{
		Record decoded;
		decoded.stamp = stamp;
		decoded.kind = static_cast<Kind>(bytes[6]);
		decoded.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kPrefixBytes),
			bytes.begin() + static_cast<std::ptrdiff_t>(payloadEnd));
		record = std::move(decoded);
		error.clear();
		return true;
	}
	catch (const std::bad_alloc &)
	{
		return fail(error, "record decoding allocation failed");
	}
	catch (const std::length_error &)
	{
		return fail(error, "record payload is too large");
	}
}
} // namespace RelayCommit
