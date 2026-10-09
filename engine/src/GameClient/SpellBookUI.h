// OpenBFME. GPL-3.0.
//
// The spell book screens of the local player (lane SPELL-2), the engine side the HUD draws:
//   * SpellStoreModel: the rules of RotWK's AptSpellStore (SpellStore.apt, opened by AptPalantir::OnBttnSpellStore): the purchase tree. The retail movie is
//     driven by GameClient/GUI/AptScreens/AptSpellStore.h with it (review r1, S-923).
//   * InGameSpellBookModel: the spell book's cast buttons (InGameSpellBook.apt inside the Palantir, driven by AptPalantir::syncSpellBook through InGameHud)
//     and the targeting of a power that wants a position (its radius cursor).
// Both only read the logic (display reads: SpecialPowerModuleInterface::isReadyForDisplay / getPercentReadyForDisplay) and act through GameMessages.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * AptSpellStore (constructor RW 0x82379A, 0x35C bytes): callbacks OnInitialized (RW 0x82285B), OnClosed (RW 0x822A80), OnBttnClose (RW 0x82289A),
//     OnBttnReset (RW 0x823484), OnBttnSpell (RW 0x8235A5), OnRollOverBttnSpell / OnRollOutBttnSpell (RW 0x82292E / 0x822954), InputEnabled (RW 0x822974);
//     the purchase proxy at + 0x288 (constructor RW 0x82327D, vtable RW 0xC50CF0): + 4 the player's science record, + 8 .. + 0x10 the pending sciences,
//     + 0x14 their summed cost; hasScience (RW 0x822D97) = the player owns it (RW 0x6AC207) or it is pending; getSciencePurchasePoints (RW 0x822843) = the
//     player's points (+ 0x24) - the pending cost;
//   * the fill (RW 0x822A98): unless closing (+ 0x2A2), the first 20 buttons (+ 0x2A8, 8 bytes each: button, state) of the store's command set (RW 0x80C837 i);
//     each button's image goes to the clip "SpellStore/Buttons/Spell%d" (RW 0x6236DE) and the text record "APT:Spell%dCost" is the purchase cost of its
//     first science (RW 0x5FEC64);
//   * OnBttnSpell (RW 0x8235A5): not in game kind 6 (or the shell's + 0x16 set), the index (RW 0x822903) in [0, 20), not closing, a button there: its
//     first science (button + 0xA4) not owned by the proxy and RW 0x5FED5B (the root prerequisites and the cost against the proxy's points): a click sound
//     (RW 0x90BE00) and the science is pending (RW 0x82355F: appended, + 0x14 += its cost);
//   * OnBttnReset (RW 0x823484): the pending list is cleared (RW 0x8232A1 / 0x5FF9D3);
//   * the destructor (RW 0x8232ED): every pending science goes to the logic (RW 0x940435(science, 1, 0) for each, in order) when TheControlBar exists.
// INFERENCE (S-923): the store's command set is the local player's PlayerTemplate PurchaseScienceCommandSetMP in a skirmish / multiplayer game, else
// PurchaseScienceCommandSet (RW 0x822AB3 reads it through RW 0x71F933 from an object of TheControlBar, not traced); RW 0x940435 is taken as the
// MSG_PURCHASE_SCIENCE message of the player (what SPELL-1's handler executes; RW 0x940435 is ControlBar::processCommandUI of the pending button). The
// per-button state values the movie is sent are AptSpellStore's (RW 0xC50BF8, GameClient/GUI/AptScreens/AptSpellStore.h).

#pragma once

#include "Common/Science.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class CommandButton;
class CommandStore;
class GameLogic;
class Player;
class SpecialPowerTemplate;

class SpellStoreModel
{
public:
	enum
	{
		MAX_BUTTONS = 20 ///< RW 0x822B9E: `cmp esi, 0x14`
	};
	enum State
	{
		STATE_PURCHASED, ///< the player owns the science
		STATE_PENDING,   ///< clicked in this visit: bought when the store closes
		STATE_AVAILABLE, ///< a click would make it pending (RW 0x5FED5B against the proxy)
		STATE_LOCKED     ///< prerequisites or points missing
	};
	struct Button
	{
		int index = 0;                     ///< 0-based (the clip is Spell<index + 1>)
		const CommandButton *button = nullptr;
		ScienceType science = SCIENCE_INVALID;
		std::string scienceName;
		int cost = 0;                      ///< APT:Spell%dCost
		std::string image;                 ///< the button's first ButtonImage (a MappedImage)
		std::string label;                 ///< TextLabel (a GameText label; S-520: not looked up)
		std::vector<int> parents;          ///< the store buttons whose science is in one of this science's PrerequisiteSciences groups (the tree's connectors)
		State state = STATE_LOCKED;
	};

	// RW 0x82379A + 0x822A98: the local player's store; false (with `error`) when the player has no template or the set is unknown
	bool open(const CommandStore &commands, GameLogic &logic, Player &player, std::string *error = nullptr);
	bool isOpen() const { return m_player != nullptr; }
	// RW 0x8235A5; true when the science became pending
	bool click(int index);
	// RW 0x823484
	void reset();
	// RW 0x8232ED: the purchase messages of the pending sciences, in click order; the store is closed after
	std::vector<GameMessage> close();

	const std::vector<Button> &buttons() const { return m_buttons; }
	const std::string &commandSetName() const { return m_setName; }
	// the proxy's points (RW 0x822843): the player's purchase points minus the pending cost (APT:SpellStoreSpellPoints)
	int points() const;
	const std::vector<ScienceType> &pending() const { return m_pending; }
	// recomputes every button's state (the per-frame update)
	void refresh();

private:
	struct Proxy;
	bool proxyHas(ScienceType st) const;
	GameLogic *m_logic = nullptr;
	Player *m_player = nullptr;
	std::string m_setName;
	std::vector<Button> m_buttons;
	std::vector<ScienceType> m_pending; ///< proxy + 8
	int m_pendingCost = 0;              ///< proxy + 0x14
};

class InGameSpellBookModel
{
public:
	struct Button
	{
		int index = 0;                 ///< 0-based slot of the spell book's command set (InGameSpellBookSpell<index + 1>)
		const CommandButton *button = nullptr;
		const SpecialPowerTemplate *power = nullptr;
		std::string image;             ///< InGameSpellBookSpell%dImage
		bool owned = false;            ///< one of the power's RequiredSciences is owned (an empty list counts)
		bool usable = false;           ///< SpecialPowerModules::canUseSpecialPower
		bool ready = false;            ///< isReadyForDisplay
		float percent = 0.0f;          ///< getPercentReadyForDisplay (InGameSpellBookSpell%dTimer)
		bool needsPosition = false;    ///< COMMAND_OPTION_NEED_TARGET_POS
		std::string radiusCursor;      ///< RadiusCursorType
		float radius = 0.0f;           ///< the SpecialPower's RadiusCursorRadius
	};
	// the local player's spell book object and its command set; false when the player has none
	bool refresh(const CommandStore &commands, GameLogic &logic, Player &player);
	const std::vector<Button> &buttons() const { return m_buttons; }
	ObjectID book() const { return m_book; }

	// a press of button `index` (OnAptInGameSpellBookButtonPressed): a power without a position casts at once (the message is returned); one that wants a
	// position starts the targeting (no message); a button that is not owned, usable and ready does nothing
	bool press(int index, int playerIndex, std::vector<GameMessage> &out);
	bool targeting() const { return m_targeting >= 0; }
	const Button *targetButton() const;
	// the targeting click on the ground: MSG_DO_SPECIAL_POWER_AT_LOCATION { power id, location, no object, options 0, the book } (SPELL-1's format)
	bool clickWorld(const Coord3D &where, int playerIndex, std::vector<GameMessage> &out);
	void cancel() { m_targeting = -1; }

private:
	std::vector<Button> m_buttons;
	ObjectID m_book = INVALID_ID;
	int m_targeting = -1;
};
