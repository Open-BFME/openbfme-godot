// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Money (ZH Include/Common/Money.h, Source/Common/RTS/Money.cpp): a player's cash. init() clears it, deposit adds, withdraw takes
// at most what is there and returns what it took.
//
// TARGET (RotWK game.dat, caveat S-001; lane ECON-1 read the whole class): the object is {+0 unused, +4 amount (u32), +8 player index}.
//   * withdraw (RW 0x7B17EF, ret 0xC; args amount, ScoreKeeper*, playSound): the amount is capped at the cash; 0 returns 0 before anything else;
//     with playSound the withdraw sound event plays; cash -= amount; the LOCAL player's statistics (RW 0xDE8A98 + 4) count it when this is the
//     local player's money; then ScoreKeeper::addMoneySpent(amount) when the keeper is not null. Returns the amount taken.
//   * deposit (RW 0x7B18B8, ret 0xC): amount 0 does nothing at all; sound; cash += amount (u32 add, wraps like ZH); local statistics + 8; the keeper's
//     addMoneyEarned(amount) when not null.
//   * set-amount (RW 0x7B18B8's twin used by Player::init with the keeper null) is `deposit` on a cleared Money: the template's StartMoney.
// The sounds and the local player statistics belong to the audio / client lanes and are not here; the `playSound` argument is kept so callers
// match the ZH signature. Money is plain integer state: nothing here touches a float.
//
// The ScoreKeeper pointer follows the retail signature: money that is earned or spent through a player's score passes that player's keeper (the
// economy modules pass Player::getScoreKeeper()); a caller with null leaves the score alone (a refund that is not income, the start money).

#pragma once

#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"

#include <cstdint>

class Money
{
public:
	Money() = default;
	explicit Money(std::uint32_t amount)
		: m_money(amount)
	{
	}

	void init() { m_money = 0; }
	std::uint32_t countMoney() const { return m_money; }

	// returns the amount actually withdrawn (never more than the player has)
	std::uint32_t withdraw(std::uint32_t amountToWithdraw, bool playSound = true) { return withdraw(amountToWithdraw, nullptr, playSound); }
	std::uint32_t withdraw(std::uint32_t amountToWithdraw, ScoreKeeper *score, bool playSound = true)
	{
		(void)playSound;
		if (amountToWithdraw > m_money)
		{
			amountToWithdraw = m_money; // RW 0x7B1809 cmova
		}
		if (amountToWithdraw == 0)
		{
			return 0;
		}
		m_money -= amountToWithdraw;
		if (score)
		{
			score->addMoneySpent((std::int32_t)amountToWithdraw);
		}
		return amountToWithdraw;
	}
	void deposit(std::uint32_t amountToDeposit, bool playSound = true) { deposit(amountToDeposit, nullptr, playSound); }
	void deposit(std::uint32_t amountToDeposit, ScoreKeeper *score, bool playSound = true)
	{
		(void)playSound;
		if (amountToDeposit == 0)
		{
			return; // RW 0x7B18CD: test edi, edi; je end
		}
		m_money += amountToDeposit; // ZH: unsigned add (wraps; no retail game reaches 2^32)
		if (score)
		{
			score->addMoneyEarned((std::int32_t)amountToDeposit);
		}
	}
	void setPlayerIndex(int index) { m_playerIndex = index; }
	int getPlayerIndex() const { return m_playerIndex; }

	void crc(StateHasher &h) const { h.addU32(m_money); }

private:
	std::uint32_t m_money = 0;
	int m_playerIndex = 0;
};
