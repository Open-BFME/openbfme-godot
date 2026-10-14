// OpenBFME. GPL-3.0.
//
// The castle messages of the dispatcher (lane BACKLOG2, stop S-951): a player's unpack and pack of a castle, camp or economy plot it holds. Executed on the one
// dispatch path (GameLogicDispatch::registerHandler).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; the dispatcher RW 0x779A3D, tier B edited, no decomp source: read from the disassembly; the callees are BFME2
// decomp tier A, see CastleModules.h):
//   * every case takes the first selected object the player controls (RW 0x779A21; none: nothing) and its CastleBehavior (RW 0x68BDA5 with the module key
//     RW 0x797AC1; none: nothing);
//   * MSG_CASTLE_PACK (1086, RW 0x77C265): the castle is unpacked (RW 0x797B63) and its controlling player may pack it (RW 0x79A472(player, 0)): abandon
//     (RW 0x79CB47), which fades it out and packs it after FadeTime (CastleBehavior state 5);
//   * MSG_CASTLE_UNPACK (1085, RW 0x77C2C1): canUnpack(true) (RW 0x7983E3), the controller may unpack (RW 0x79A472), can afford the side's price (RW 0x79969F):
//     initiateUnpack(false, null) (RW 0x79C32E -> 0x79C17D);
//   * MSG_CASTLE_UNPACK_EXPLICIT_OBJECT (1087, RW 0x77C338): argument 0's low word is a template id (RW 0x710C9E(0), `mov ax, word`; ThingFactory RW 0x6CFE6C:
//     an unknown id is nothing); canUnpack(true), may unpack, can afford the template (RW 0x797B6D): initiateUnpack(false, template).
//   The client (ControlBar::processCommandUI RW 0x940435) appends 1085 / 1086 without arguments and 1087 with the button's template id (RW 0x940AD9 ..
//   0x940FA9); its availability (RW 0x942733 cases 33 / 34 / 50) is ControlBar's.
// A message whose arguments are not the client's layout (1085 / 1086: none; 1087: exactly one integer) is malformed: refused before anything is read, named in the
// game's report (GameLogic::reportError) and counted unhandled by the dispatcher, the world unchanged. An id no template has is refused by name too.

#pragma once

class GameLogicDispatch;

namespace CastleCommands
{
void registerHandlers(GameLogicDispatch &d);

struct Stats
{
	unsigned long long unpacks = 0, explicitUnpacks = 0, packs = 0, refused = 0, malformed = 0;
};
// process-wide counters (diagnostics, not simulation state)
const Stats &stats();
} // namespace CastleCommands
