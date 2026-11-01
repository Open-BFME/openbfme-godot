// OpenBFME. GPL-3.0.
//
// NET-SYNC canonical ingress wire codec; see RelayCanonicalInput.h for the
// OBCI v1 layout and the enforcement contract.

#include "GameNetwork/RelayCanonicalInput.h"

#include <cstring>

namespace RelayCanonicalInput
{
namespace
{
constexpr std::uint8_t kMagic[4] = { 'O', 'B', 'C', 'I' };
constexpr std::uint8_t kVersion = 1;
constexpr std::uint32_t kMaxOrderBytes = 64u * 1024u;

void put64(std::uint8_t *at, std::uint64_t value)
{
	for (unsigned byte = 0; byte < 8; ++byte)
		at[byte] = (std::uint8_t)(value >> (byte * 8));
}

void put32(std::uint8_t *at, std::uint32_t value)
{
	for (unsigned byte = 0; byte < 4; ++byte)
		at[byte] = (std::uint8_t)(value >> (byte * 8));
}

std::uint64_t get64(const std::uint8_t *at)
{
	std::uint64_t value = 0;
	for (unsigned byte = 0; byte < 8; ++byte)
		value |= (std::uint64_t)at[byte] << (byte * 8);
	return value;
}

std::uint32_t get32(const std::uint8_t *at)
{
	std::uint32_t value = 0;
	for (unsigned byte = 0; byte < 4; ++byte)
		value |= (std::uint32_t)at[byte] << (byte * 8);
	return value;
}
}

bool encode(const Message &message, std::vector<std::uint8_t> &bytes, std::string &error)
{
	if (message.id == 0)
	{
		error = "an id of zero";
		return false;
	}
	if (message.kind == Kind::Beat && !message.bytes.empty())
	{
		error = "a beat carrying " + std::to_string(message.bytes.size()) + " bytes";
		return false;
	}
	if (message.kind == Kind::Order && message.bytes.size() > kMaxOrderBytes)
	{
		error = "an order of " + std::to_string(message.bytes.size())
			+ " bytes exceeds the " + std::to_string(kMaxOrderBytes) + "-byte budget";
		return false;
	}
	bytes.assign(kHeaderBytes, 0);
	std::memcpy(bytes.data(), kMagic, sizeof(kMagic));
	bytes[4] = kVersion;
	bytes[5] = (std::uint8_t)message.kind;
	put64(bytes.data() + 6, message.id);
	put32(bytes.data() + 14, (std::uint32_t)message.bytes.size());
	bytes.insert(bytes.end(), message.bytes.begin(), message.bytes.end());
	return true;
}

bool decode(const std::vector<std::uint8_t> &bytes, Message &message, std::string &error)
{
	return decode(bytes.data(), bytes.size(), message, error);
}

bool decode(const std::uint8_t *data, std::size_t size, Message &message, std::string &error)
{
	if (!data || size < kHeaderBytes)
		return (error = "a message shorter than the " + std::to_string(kHeaderBytes) + "-byte header"), false;
	if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0)
		return (error = "bad OBCI magic"), false;
	if (data[4] != kVersion)
		return (error = "OBCI version " + std::to_string(data[4])), false;
	const std::uint8_t kind = data[5];
	if (kind != (std::uint8_t)Kind::Order && kind != (std::uint8_t)Kind::Beat)
		return (error = "unknown OBCI kind " + std::to_string(kind)), false;
	message.kind = (Kind)kind;
	message.id = get64(data + 6);
	const std::uint32_t declared = get32(data + 14);
	if (message.id == 0)
		return (error = "an id of zero"), false;
	if (declared > kMaxOrderBytes)
		return (error = "a declared order of " + std::to_string(declared) + " bytes exceeds the "
			+ std::to_string(kMaxOrderBytes) + "-byte budget"), false;
	if (size != kHeaderBytes + declared)
		return (error = "a declared " + std::to_string(declared) + "-byte order in a "
			+ std::to_string(size) + "-byte message"), false;
	if (message.kind == Kind::Beat && declared != 0)
		return (error = "a beat declaring " + std::to_string(declared) + " bytes"), false;
	message.bytes.assign(data + kHeaderBytes, data + size);
	return true;
}
}
