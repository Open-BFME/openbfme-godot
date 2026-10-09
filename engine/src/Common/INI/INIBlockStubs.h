// OpenBFME. GPL-3.0.
//
// The RotWK block keyword table and recording stub handlers.
//
// RotwkBlockKeywords() is the 131-entry registration list of RotWK 2.01 (spec
// ini-and-object-model.md 3.2; RW list head 0xDC51C0; SCR blocks_rotwk.txt has the parse
// function VAs). 130 are registered statically, Pathfinder lazily the first time RW 0x6F8374
// runs; the stubs register all 131 up front, which only matters if a file mentions Pathfinder
// before that function runs (not modelled).
//
// A stub is NOT a parser. Real block parsers consume exactly their block through their field
// tables (spec 3.3: nesting is decided by field parsers, there is no generic grammar). Until
// those exist (port-order steps 8-12) the stubs have two extent modes, and NEITHER certifies
// block boundaries:
//   * Strict (default): header, then lines up to and including the first End whose indentation is
//     not deeper than the header's. Correct for flat blocks, so an unknown keyword after a
//     closed block is reported ("Armor A / End / Bogus X / End" fails on Bogus). A block with
//     nested blocks closes at the first nested End, so real retail files do not traverse in
//     this mode.
//   * Lenient: skip to the next line that starts in column 0 with a registered keyword. This
//     lets TextFile, the #define pre-pass and the dispatch loop traverse the whole retail tree,
//     but it swallows anything between two keywords, including unknown blocks.
// Every record says whether the extent ended on an End line.
// ACCEPTANCE STOP: exact block boundaries need the field tables (port-order steps 8-12).

#pragma once

#include "Common/INI.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

// FX-1: the FXList and FXParticleSystem blocks are parsed for real (stores are forward declared: GameClient/FXList.h cannot share a
// translation unit with GameLogic/BitFlags.h, see FXListObjectFilter.cpp).
class FXListStore;
namespace FXParticleSystem
{
class FXParticleSystemTemplateStore;
}

struct INIBlockRecord
{
	std::string keyword;
	std::string name;   ///< first header token after the keyword, empty if none
	std::string file;
	int line = 0;       ///< source line of the header
	int extentLines = 0; ///< lines consumed after the header
	bool endsWithEnd = false;
};

class INIBlockRecorder
{
public:
	std::vector<INIBlockRecord> records;
	std::map<std::string, size_t> countByKeyword;
	size_t endedCount() const;

	// FX-1: the stores the FXList / FXParticleSystem blocks parse into when no global store is installed (the
	// retail wiring owns TheFXListStore / TheFXParticleSystemManager; tests that only walk the tree use these).
	std::shared_ptr<FXListStore> fxLists;
	std::shared_ptr<FXParticleSystem::FXParticleSystemTemplateStore> fxParticleSystems;
};

const std::vector<std::string> &RotwkBlockKeywords();

// Registers a recording stub for every RotWK keyword not in `skip`. Throws std::logic_error if
// one is already registered.
enum class StubExtent
{
	Strict,
	Lenient
};
void RegisterRecordingBlockStubs(INIBlockRegistry &registry, INIBlockRecorder &recorder, const std::vector<std::string> &skip, StubExtent mode = StubExtent::Strict);
