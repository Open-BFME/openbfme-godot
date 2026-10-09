// OpenBFME. GPL-3.0.
//
// CreateAHeroHero: one Create-a-Hero, as the profile (MyHero*.cah) and the system heroes (data\systemheroes\myhero_*.cah of Data1.big) store it
// (lane HERO-2). The record and its file format only: the CreateAHeroSystem INI and the in-game use of a record are GameLogic/CreateAHeroSystem.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the file is an EA chunk stream (header reader RW 0xA200A3): u32 'ALAE' (0x45414C41), u32 '1STR' or '2STR'; a 2STR stream then has a u32 version
//     word (1 in every retail file); then a u32 block flag (non-zero: the stream is in KLBE blocks, RW 0xA20149; 0 in every retail file); anything else
//     fails the open;
//   * the record's xfer is vtable slot 0xC of RW 0xC4F390 (RW 0x80B4F5): xferVersion (1 byte, current 8), then, in order, ID (+0x4, xferObjectID RW 0x707887),
//     Name (+0x8, xferUnicodeString: a byte count, then that many UTF-16LE units), ClassIndex (+0xC), SubClassIndex (+0x10), two Dummy u32 (0 when saved),
//     PrimaryColor / SecondaryColor / TertiaryColor (+0x2C / +0x30 / +0x34), the 15 powers (+0x80, 12 bytes each, RW 0x809A31: CommandBotton xferAsciiString
//     (a byte count, then the bytes), ExpLevel u32, ButtonIndex u32), BlingCount u32 and that many (GroupName xferAsciiString, BlingIndex u32) pairs (+0x14:
//     a map by the group's name key; an empty GroupName is the fatal "Fatal error in CreateAHeroHero::DoXfer(Xfer *xfer)", RW 0xC4F2D8); version > 6: the
//     unique id (+0x4C, xferAsciiString); version > 7: IsSystemHero (+0x48, xferBool) and the checksum (+0x138, u32); on load, the flags (+0x38) := 0x2FF;
//   * the checksum (RW 0x8097F9 / 0x809816 / 0x809946 / 0x809971) is CRC-32 (RW 0xA2D770 / 0xA2D7B0, the table RW 0xDBBB70: the reflected 0xEDB88320
//     polynomial, ~ before and after) chained over every field from ID to IsSystemHero EXCEPT the version and the unique id: the 4 bytes of a u32, the
//     characters of an ascii string, the name translated to ascii (each unit's low byte, AsciiString::translate RW 0x437B90), the 1 byte of the bool. A
//     loaded record is valid (+0x71) when the stored checksum equals the computed one; a version 7 record is valid without one;
//   * on a load, a group the record already had keeps its slot (RW 0x80A73B adds a missing group with index 0), then its index is set (RW 0x80A6C8);
//     the applied index of every group (+0x20) becomes -1 (not applied yet).
// The record's map (+0x14) iterates in name key order in retail; this port keeps the file's order, which is that order at the time the file was saved
// (INFERENCE: the key values themselves depend on every name the engine keyed before; stop S-1226).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct CreateAHeroPower ///< RW 0x809A31: one of the 15 power entries (+0x80 + 12 * i)
{
	std::string commandButton;    ///< +0 "CommandBotton"
	std::uint32_t expLevel = 0;   ///< +4 "ExpLevel"
	std::uint32_t buttonIndex = 0; ///< +8 "ButtonIndex"
};

struct CreateAHeroHero
{
	enum
	{
		POWER_COUNT = 15,       ///< RW 0x80B717: mov [ebp+8], 0xF
		XFER_VERSION = 8,       ///< RW 0x80B531: mov [ebp-0xF], 8
		LOAD_FLAGS = 0x2FF,     ///< RW 0x80BAB7: + 0x38 after a load
	};
	std::uint32_t objectID = 0;          ///< +0x04 "ID": the object made from the record (0 = none)
	std::u16string name;                 ///< +0x08 "Name"
	std::uint32_t classIndex = 0;        ///< +0x0C "ClassIndex"
	std::uint32_t subClassIndex = 0;     ///< +0x10 "SubClassIndex"
	std::uint32_t primaryColor = 0;      ///< +0x2C
	std::uint32_t secondaryColor = 0;    ///< +0x30
	std::uint32_t tertiaryColor = 0;     ///< +0x34
	std::array<CreateAHeroPower, POWER_COUNT> powers{}; ///< +0x80
	std::vector<std::pair<std::string, std::uint32_t>> bling; ///< +0x14 (group name, index into the group's list of the subclass), file order
	std::string uniqueID;                ///< +0x4C (version > 6)
	bool isSystemHero = false;           ///< +0x48 (version > 7)
	std::uint32_t checksum = 0;          ///< +0x138
	bool valid = false;                  ///< +0x71: the checksum matched
	std::uint32_t flags = 0;             ///< +0x38: the dirty bits RW 0x80ACE3 consumes (1 / 2 class, 4 bling, 8 colours, 0x200 ...)
	std::uint8_t version = XFER_VERSION;

	// RW 0x80A6C8 after RW 0x80A73B: the group's index (a new group is appended)
	void setBling(const std::string &group, std::uint32_t index);
	const std::uint32_t *findBling(const std::string &group) const;

	// RW 0x80B4F5 load: the stream header (RW 0xA200A3), then the record. False + *error on a bad header, a truncated record, an empty group name or
	// trailing bytes; a checksum mismatch is NOT an error (valid = false, as retail).
	bool load(const std::vector<std::uint8_t> &bytes, std::string *error);
	// RW 0x80B4F5 save: the 2STR header (version word 1, block flag 0) and the version 8 record with its checksum.
	std::vector<std::uint8_t> save() const;
	// the checksum of the current fields (what save() writes)
	std::uint32_t computeChecksum() const;
	// every field (lane HERO-2: the game setup compares the slots' heroes)
	bool operator==(const CreateAHeroHero &o) const;
	bool operator!=(const CreateAHeroHero &o) const { return !(*this == o); }
};

namespace CreateAHeroRecord
{
// RW 0xA2D770: CRC-32 of `n` bytes chained from `crc`
std::uint32_t crc32(const void *data, size_t n, std::uint32_t crc);
}
