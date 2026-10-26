// OpenBFME. GPL-3.0.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RelayCommit
{
constexpr std::uint16_t kVersion = 1;

enum class Kind : std::uint8_t
{
	Frame = 1,
	Membership = 2,
	End = 3
};

struct Stamp
{
	std::uint64_t game = 0, epoch = 0, sequence = 0;
	std::uint32_t frame = 0;
	std::uint64_t membershipGeneration = 0;
};

struct Record
{
	Stamp stamp;
	Kind kind = Kind::Frame;
	std::vector<std::uint8_t> payload;
};

// One encoding for durable records and committed wire payloads; the outer transport fragments it.
// Magic "OBRC", LE u16 version, u8 kind, u8 reserved (zero), LE u64 game/epoch/sequence,
// LE u32 frame, LE u64 membership generation, LE u32 payload length, payload, SHA-256 of all preceding bytes.
constexpr size_t kRecordOverhead = 80;
bool encode(const Record &record, std::vector<std::uint8_t> &bytes, std::string &error);
bool decode(const std::vector<std::uint8_t> &bytes, size_t maxPayloadBytes, Record &record, std::string &error);
}
