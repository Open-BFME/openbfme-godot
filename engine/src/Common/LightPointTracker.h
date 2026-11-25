// OpenBFME. GPL-3.0.
//
// LightPointTracker (PORT-113): a player's light points of the War of the Ring. TARGET FACTS (RotWK 2.01 game.dat):
//   * the object inside Player at + 0x338, size 0x14, vtable RW 0xC13FD4 (slot 2 RW 0x6ACE2F answers the snapshot name RW 0xC13FE4 "LightPointSystem",
//     slot 3 RW 0x830D3F is its xfer): + 4 the points, + 8 a vector of ints, the indices (into TheLightPointSystem's levels) of the levels the player
//     was given;
//   * the Player constructor makes it (RW 0x6B1BD0 -> 0x6ACE0F), Player::init empties it (RW 0x6B0355 the levels, RW 0x6B0362 the points);
//   * RW 0x8309E3 adds an amount, RW 0x8309F3 / 0x830D1E / 0x830AB9 test, add and remove a level index. In 2.01 nothing but the xfer reads the level
//     list: the special power test and the cost read the points only (RW 0x830AE3, 0x6AA91D).
#pragma once
#include <vector>

class StateHasher;
class Xfer;

class LightPointTracker
{
public:
	int getPoints() const { return m_points; }
	// RW 0x8309E3: the total moves by `amount` (a negative one too); a total below zero becomes 0 (RW 0x8309EC)
	void addPoints(int amount);
	// RW 0x6AA91D (Player): the total drops by `cost`; a total below zero becomes 0 (RW 0x6AA92B)
	void subtractPoints(int cost);
	// RW 0x6AA931 (Player): the points only, the levels stay
	void resetPoints() { m_points = 0; }
	bool hasLevel(int index) const; // RW 0x8309F3
	void addLevel(int index);       // RW 0x830D1E: appended unless the player has it
	void removeLevel(int index);    // RW 0x830AB9: the first entry equal to it goes; none: nothing
	const std::vector<int> &getLevels() const { return m_levels; }
	// Player::init (RW 0x6B0355, 0x6B0362): no levels, no points
	void reset();
	// true while it holds no points and no level (Player::init's state): such a player adds nothing to the state hash and saves the earlier Player layout
	bool isUntouched() const { return m_points == 0 && m_levels.empty(); }
	void crc(StateHasher &h) const;
	// OpenBFME's snapshot (S-4190): the points and the levels. Retail's xfer (RW 0x830D3F) saves the levels and NOT the points (RW 0x830D83 .. 0x830DB9;
	// only its light CRC branch reads them, RW 0x830D73), so a retail load resumes with the 0 points Player::init left. This snapshot has to resume the
	// game exactly (docs/SAVES.md section 6), so it holds both.
	void xfer(Xfer &x);

private:
	int m_points = 0;          // + 4
	std::vector<int> m_levels; // + 8
};
