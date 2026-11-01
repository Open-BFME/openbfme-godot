// OpenBFME. GPL-3.0.
//
// GameSorter (lane MENUWORK-1): the sort order of a lobby's game list (LanLobby.apt's list headers).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the decompile's files are byte-matched to RotWK, tier A):
//   * the constructor RW 0x9888B7 (BFME2 0x980842; GameNetwork/GameSorterDestructor.cpp) registers GameSorter::OnSortStatus, OnSortName, OnSortMap,
//     OnSortPlayers and OnSortPing and starts with the primary key 4 (players), the secondary key 8 (ping) and the status cycle 0; RotWK's
//     AptLanLobby has one as a member (decompile AptLanLobbyDestructor.cpp), as the online custom match screen does;
//   * the commands select Name 1, Map 2, Players 4, Ping 8, Status 16 through RW 0x9881DE (BFME2 0x980182, Rva00580172.cpp): the same key again
//     flips its direction bit 0x10000, another key becomes the primary and the old primary the secondary; Status also advances the cycle (+0x18);
//   * the sort (BFME2 0x98113D, RW 0x9891B2: the list's lazy introsort, Rva005810B6Sort) builds its record from the sorter and the first game's
//     GameInfo vslot 7 (the number of list icons: LANGameInfo RW 0x8311B1 returns 3); the comparator RW 0x988418 (BFME2 0x9803C0,
//     Rva005803C0Compare.cpp) reads per game: vslot 9 (RW 0x801360 GameInfo::getNumOpenOrOccupiedSlots), vslot 8 (RW 0x800ACE getNumPlayers:
//     the occupied slots), the name (vslot 22), vslot 21 (the ping: LANGameInfo RW 0x851E97 returns 0) and the map (_strcmpi of getMap(), only
//     between games of the same mode +0x5C, else equal); the status record RW 0x98824E (BFME2 0x9801F6, GameSorter.cpp) compares, for
//     each icon index i = (cycle + n) % icons, whether each game has that icon (GameInfo vslot 6: RW 0x802B04 / LANGameInfo RW 0x84A8AD: icon 0 a
//     user map (mode 0: not in the map cache or not official), icon 1 non-default rules, icon 2 (LAN) a game in progress): 1 when only the first
//     has it, -1 when only the second. For the primary, then the secondary key: Name / Map / Ping by their difference (descending when the
//     direction bit is set); Status: the first non-zero icon comparison (> 0 sorts first, no direction); Players: vslot 9's difference (with
//     the direction), then vslot 8's (ascending). Then name, vslot 9, map, ping ascending; then the games' pointer order.
// INFERENCE (stop S-4751, reported at runtime by the LAN lobby): the pointer order of the last tie is taken as the list's own order (retail compares
// the GameInfo pointers, whose order is the allocation's); the icon 0 of a War of the Ring game (mode 1: a resumed save) cannot occur (OpenBFME's
// LAN games are open play only, mode 0).
#pragma once

#include <string>
#include <vector>

class GameSorter
{
public:
	enum Key
	{
		KEY_NAME = 1,
		KEY_MAP = 2,
		KEY_PLAYERS = 4,
		KEY_PING = 8,
		KEY_STATUS = 16,
		KEY_DESCENDING = 0x10000
	};
	static const int kIcons = 3; ///< LANGameInfo vslot 7 (RW 0x8311B1)
	struct Row
	{
		std::u16string name;    ///< vslot 22
		std::string map;        ///< getMap()
		int mode = 0;           ///< + 0x5C (0 open play, 1 War of the Ring)
		int openOrOccupied = 0; ///< vslot 9 (RW 0x801360)
		int players = 0;        ///< vslot 8 (RW 0x800ACE): the occupied slots
		int ping = 0;           ///< vslot 21 (LAN: 0)
		bool icon[kIcons] = { false, false, false }; ///< vslot 6 (RW 0x84A8AD): user map, non-default rules, in progress
	};

	// RW 0x9881DE: a header pressed
	void select(int key);
	int primary() const { return m_primary; }
	int secondary() const { return m_secondary; }
	int statusCycle() const { return m_statusCycle; }
	// true once after a select (RW 0x9881CE's test-and-clear: the owner refills its list)
	bool takeChanged();
	// the rows' indices in the sorted order (RW 0x988418 over the list)
	std::vector<size_t> order(const std::vector<Row> &rows) const;
	// RW 0x988418 for two rows (true: `a` sorts before `b`); `ia` / `ib` are their places in the list (the last tie)
	bool before(const Row &a, size_t ia, const Row &b, size_t ib) const;
	// the stop line the owner reports (S-4751)
	static const char *stopLine();

private:
	int m_primary = KEY_PLAYERS; // + 0x0C
	int m_secondary = KEY_PING;  // + 0x10
	bool m_changed = false;      // + 0x14
	int m_statusCycle = 0;       // + 0x18
};
