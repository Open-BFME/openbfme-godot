// OpenBFME. GPL-3.0.
//
// BFME1 mode (community PR #36, docs/BFME1.md). Which game the mounted retail data is: BFME1 ships Data\INI\Default\SubsystemLegend.ini without
// BFME2's SubsystemLegendExpansion1.ini. Everything BFME1 does differently is gated on it, so RotWK / BFME2 parsing, simulation and state hashes are
// unchanged by BFME1 support:
//   * code that reads files asks isBfme1Data(fs) (or INI::bfme1Data() / INIEnvironment::bfme1Data(), cached per environment);
//   * BFME1's extra INI rows are registered per table (RegisterBfme1FieldRows, Common/INI.h) and found only in BFME1 data;
//   * simulation code asks the template (ThingTemplate::bfme1Data(), set by a ThingFactory built for BFME1 data);
//   * files read through a file system ask it: map chunks (MapReadOptions::bfme1), the map cache, the W3D model / texture layout
//     (W3DFileSource::FlatAssetLayout, Resolve_W3D_Texture's flatLayout; the simulation's asset manager reads launch bones through it);
//   * client code with nothing to ask (the texture decoder's signature sniffing, the Apt VM's type-8 constants, floor picking) asks bfme1Mounted(),
//     which the archive mount sets only after the archives pass isBfme1Data (RetailFileSystem::mount_bfme1_path) and clears otherwise. It never
//     decides a game rule.
#pragma once

#include "Common/ArchiveFileSystem.h"

#include <atomic>
#include <string>
#include <vector>

// lane SHELLFIX-1: BFME2 1.06's archives on their own (the BFME2 main menu style's shell source, RetailFileSystem::create_bfme2_shell) hold the legend
// without RotWK's expansion legend too; BFME2's Data\INI\AwardSystem.ini (BFME2 1.06 ini.big, RotWK 2.01 INI.big / _patch201ini.big; not in BFME1 1.03)
// tells them apart. Before, that source was read as BFME1 data: its menu models were looked up in BFME1's flat art\w3d\ (SFE_MenuFrame not found)
inline bool isBfme1Data(const ArchiveFileSystem &fs)
{
	return fs.doesFileExist("Data\\INI\\Default\\SubsystemLegend.ini") && !fs.doesFileExist("Data\\INI\\Default\\SubsystemLegendExpansion1.ini") &&
		!fs.doesFileExist("Data\\INI\\AwardSystem.ini");
}

namespace Bfme1Detail
{
inline std::atomic<bool> &mountedFlag()
{
	static std::atomic<bool> flag{ false };
	return flag;
}
} // namespace Bfme1Detail

// the mounted game is BFME1 (see above); false until a BFME1 mount
inline bool bfme1Mounted()
{
	return Bfme1Detail::mountedFlag().load();
}
inline void setBfme1Mounted(bool on)
{
	Bfme1Detail::mountedFlag().store(on);
}

// [S-5017]: what BFME1 mode infers rather than ports (docs/BFME1.md, "How the BFME1 data differs"); in the BFME1 mount's report
// (RetailFileSystem::mount_bfme1_path "stops")
inline std::vector<std::string> bfme1StopLines()
{
	return { "[S-5017] BFME1 mode (community PR #36, experimental) is not retail-exact: its simulation is RotWK's with BFME1's data and these inferences "
			 "(docs/BFME1.md): the victory rule (a STRUCTURE not IGNORE_FOR_VICTORY, a unit not a DOZER), a player's castle flag unpacking at once and free, "
			 "the pathfinder budgets without a located BFME1 constant (RotWK's), a TerrainResourceCellSize of 1.0, build plots hidden under their occupant, "
			 "AudioEvent Control RANDOM on RotWK's unused bit 6, particle priorities mapped to RotWK's tiers, MainMenu.apt's ShowMainMenu called from "
			 "OnInitialized, BloodthirstyUpdate NumToSacrifice and InitiateVoice2 are stored but their runtime semantics are unported, no select-nearest-builder cycle timeout (InGameUI has no SelectNearestBuilderCycleTimeOut: 0); Living World campaigns and saves are not available" };
}
