// OpenBFME. GPL-3.0.
//
// DesyncDump (lane MP-2): the file a peer writes when the CRC exchange finds a desync, so it can be debugged after the game.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; GameLogic's mismatch handler RW 0x6290C7, called once the CRCs of a frame differ):
//   * once per game (TheGameLogic + 0x1BC): the recorder notes the mismatch (RW 0x77D251), a message box (RW 0x81A375, type 4) with the game texts
//     GUI:DesyncTitle / GUI:DesyncText, then RW 0x602FFE;
//   * a text file "DESYNC-%s-%s-%s.txt" (RW 0xBFD900: "Frame%d" of the mismatch frame, the executable's name after the last '\' of GlobalData + 0xC, the
//     local player's name or else the computer name) holding "Frame #%d\n\n" (RW 0xBFD804), the logic's CRC text lines (TheGameLogic + 0x54 .. + 0x68),
//     a separator, "REPLAY FILE" (RW 0xBFD780) and the replay being recorded (RW 0x77E29A), a closing separator; with a debug flag (RW 0xDE87C9) also
//     "BIN_DESYNC-%s-%s-%s.bin".
// OpenBFME DIFFERENCES (stop S-1124): the CRC text lines are OpenBFME's per-subsystem breakdown (Network.h DesyncReport, every half that arrived, every
// object hash each half carried); the replay is not embedded: its path is named and the file is copied next to the dump ("DESYNC-...replay"); the
// file is rewritten when a later half of the report arrives; no BIN_DESYNC.

#pragma once

#include "GameNetwork/Network.h"

#include <string>

namespace DesyncDump
{
// RW 0xBFD900's name in `directory` ("" = the current directory)
std::string fileName(const std::string &directory, UnsignedInt frame, const std::string &exeName, const std::string &playerName);
// writes the dump of `report` (and copies `replayPath` when it is not empty); false + *error when the file cannot be written
bool write(const std::string &path, const DesyncReport &report, int localSlot, const std::string &replayPath, std::string *error);
} // namespace DesyncDump
