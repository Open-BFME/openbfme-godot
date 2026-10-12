// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Selling a structure (lane FACT-A): the MSG_SELL case of the command dispatcher, BuildAssistant::sellObject and the sell list that BuildAssistant::update
// pays out. ZH keeps the list in TheBuildAssistant (ZH BuildAssistant.cpp sellObject / update); the port's BuildAssistant is a set of free functions over the
// template, so the list lives here, owned by GameLogic and run in its phase 5 row.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp Common/System/BuildAssistant.cpp, tier B same-shape for both functions: only object offsets differ):
//   * MSG_SELL 1052, the dispatcher case RW 0x77B2C8: nothing in the Living World (RW [0xDE4950] and RW 0x441E4A); for every id of the issuing player's group
//     (RW 0x771A36, the selection minus what the player does not control) that is an object: when its template is equivalent (RW 0x73D5C2) to the template named
//     by the LOCAL player's PlayerTemplate BeaconName (RW 0x6D1305 on PlayerTemplate + 0x17C) the beacon is removed (RW 0x62BBAB, the local player's own beacon,
//     or the beacon UI); every other object goes to TheBuildAssistant vslot 0x6C = sellObject (RW 0x77B3E0).
//   * sellObject RW 0x7966BB: only a STRUCTURE (template KindOf byte + 0x108 bit 7), not NOT_SELLABLE (+ 0x11B bit 2), whose command set has a SELL button
//     (RW 0x793D57: the 33 slots of TheControlBar's set of the object's command set name, a button with Command 0x16), not UNDER_CONSTRUCTION (status 2), and
//     not already in the list; then the construction percent becomes 99.9 (RW 0xC30850), the entry { id, frame } goes to the FRONT of the list (RW 0x86DC96),
//     the statuses SOLD and UNSELECTABLE are set (RW 0x794311 / 0x68D440), the object leaves every player's selection (GameLogic::deselectObject RW 0x625759 with
//     the mask 0xFFFFF, the client too), the drawable's animation loop becomes TOTAL_FRAMES_TO_SELL_OBJECT * 0.5 frames (client: not ported), the production is
//     cancelled with its refunds (production vslot 0x3C), the AI idles (CMD_FROM_AI, RW 0x5E821A) and the contain hears of the sale (contain vslot 0x64).
//   * BuildAssistant::update RW 0x79468B (TheBuildAssistant vslot 0x28, the third subsystem of the phase 5 row before the destroy list, RW 0x62EB9B), every
//     entry in list order (front first): a missing object leaves the list; with a controlling player the refund is the template's RefundValue (u16, + 0x5EC)
//     or else ftol(paid (object + 0x33C) * GameData SellPercentage (GlobalData + 0xB94)) at PC24, times the body's health fraction (body vslot 5, RW 0x8C1D75:
//     health / max health under PC24 when the max is above 0, else 0.0; 1.0 without a body), truncated again (RW 0xA3CFA4), and deposited without a score keeper
//     with the sound (RW 0x7B18B8(value, 0, 1)); the cash text floats up (client: not ported); production is cancelled again; a STRUCTURE whose damage state
//     is above DAMAGED (body vslot 0x24) and that was hurt within 4 seconds (RW 0x68C933) credits its last attacker with the kill (Object::scoreTheKill
//     RW 0x6955BC, type 1), otherwise its owner's score keeper counts it lost (RW 0x79F486); then Object::kill(UNRESISTABLE, death type 8) (RW 0x698EC3) and the
//     entry leaves the list. The object therefore dies in the first phase 5 after the sale (the frame of the MSG_SELL).
// INFERENCE / not ported (stop S-1980): the beacon branch (no beacons in the port: a beacon is reported, not sold); the issuing player's PlayerTemplate stands in
// for the local player's (every retail faction names the same beacon); the contain's slot 0x64 (BFME2 decomp, present but not byte-matched: OpenContain
// orders its riders out with CMD_FROM_AI, GarrisonContain first removes all of them) is ported as orderAllPassengersToExit(CMD_FROM_AI); the client parts
// (animation loop, cash text, deselectDrawable) are the client's.

#pragma once

#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "GameLogic/GameMessage.h"

#include <deque>
#include <string>
#include <vector>

class GameLogic;
class GameLogicDispatch;
class Object;

class BuildAssistantSell
{
public:
	explicit BuildAssistantSell(GameLogic &logic) : m_logic(logic) {}

	// registers MSG_SELL (RW 0x77B2C8) on the dispatch
	void registerHandlers(GameLogicDispatch &dispatch);
	// RW 0x7966BB
	void sellObject(Object *obj);
	// RW 0x79468B: pays out and kills every object of the list
	void update();
	void reset() { m_list.clear(); }
	bool empty() const { return m_list.empty(); }
	void crc(StateHasher &h) const;

	struct Entry
	{
		ObjectID id = INVALID_ID;
		UnsignedInt frame = 0;
	};
	const std::deque<Entry> &list() const { return m_list; }
	struct Stats
	{
		unsigned long long messages = 0, sold = 0, refused = 0, beacons = 0, paid = 0;
	};
	const Stats &stats() const { return m_stats; }
	static std::vector<std::string> acceptanceStops();

private:
	bool onSellMessage(const GameMessage &m);

	GameLogic &m_logic;
	std::deque<Entry> m_list; ///< RW TheBuildAssistant + 0xC: the newest entry first
	Stats m_stats;
};
