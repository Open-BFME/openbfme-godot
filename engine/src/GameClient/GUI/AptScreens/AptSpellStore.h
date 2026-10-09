// OpenBFME. GPL-3.0.
//
// AptSpellStore: the engine side of SpellStore.apt, RotWK's purchase tree of the spell book (lane SPELL-2). CodePrefix "AptSpellStore". The rules of the store
// (the buttons, the purchase proxy, the pending purchases) are GameClient/SpellBookUI.h's SpellStoreModel; this screen drives the retail movie with them.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the constructor (RW 0x82379A) registers AptSpellStore::OnInitialized (RW 0x82285B: the state cache reset, + 0x2A0 = 1), OnClosed (RW 0x822A80),
//     OnBttnClose (RW 0x82289A: the movie's Close is called, + 0x2A2 closing), OnBttnReset (RW 0x823484), OnBttnSpell (RW 0x8235A5), OnRollOverBttnSpell /
//     OnRollOutBttnSpell (RW 0x82292E / 0x822954: the help index + 0x354 = the button, or -1) and the extern InputEnabled (RW 0x822974), then fills the
//     buttons (RW 0x822A98); a button argument is "Spell<n>" (RW 0x822903: strncmp "Spell", atoi - 1, in [0, 20));
//   * the update (RW 0x822E43): once initialized: the layout (RW 0x822DD7: 2 in a skirmish / multiplayer game, else from the local player's side) changed:
//     SetLayout("_multiplayer" for 2 / 3, "_campaignGood" for 0, else "_campaignEvil") and nothing else this update; otherwise ShowSpellHelpText("_on" /
//     "_off") when the help index's sign changed, the help texts once per index (APT:SpellHelpText = the button's TextLabel, APT:SpellDescription = its
//     DescriptLabel, plus "\n" and TOOLTIP:ScienceDisabled when its science cannot be bought), APT:SpellStoreSpellPoints when the proxy's points changed, and
//     per button i (20) SetSpellButtonState(i + 1, state) when it changed (RW 0x8229B5, names RW 0xC50BF8: _unused (no button / science), _disabled (cannot
//     buy), _disabled_level (the player's science is disabled or hidden, RW 0x8227BB), _already_purchased (owned before the store opened, or pending except
//     right after its click), _purchased (the state that follows _active when the science became the proxy's), _active (can be bought));
//   * the button images: the clip "SpellStore/Buttons/Spell%d" gets the button's image (RW 0x822B23 -> 0x6236DE); the cost text record "APT:Spell%dCost".
// INFERENCE (S-923): the screen is loaded into a free window slot over the Palantir (RW 0x822CF7 .. 0x822D59 pushes it through the shell and pauses a single
// player game); RW 0x8227BB's disabled / hidden sciences are not kept by PlayerScience here (never _disabled_level).

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/GameMessage.h"

#include <functional>
#include <string>
#include <vector>

class CommandStore;
class GameLogic;
class GameTextSource;
class Player;

class AptSpellStore : public AptScreen
{
public:
	AptSpellStore(WindowManager &windows, Shell &shell);
	~AptSpellStore() override;

	// binds the store to the local player (RW 0x822A98); false with `error` when the player's store command set is unknown
	bool bind(const CommandStore &commands, GameLogic &logic, Player &player, const GameTextSource *gameText, std::string *error);
	// RW 0x822E43, once per HUD update
	void update();
	// the close (OnBttnClose and Escape): the pending purchases (RW 0x8232ED) to send, and the store wants to go
	bool closing() const { return m_closing; }
	std::vector<GameMessage> takePurchases();
	// RW 0x823484
	void reset();
	// a click on a button (RW 0x8235A5); index 0-based
	bool click(int index);
	// the help index (RW 0x82292E / 0x822954): the button under the pointer, -1 none
	void rollOver(int index);
	void rollOut(int index);

	bool initialized() const { return m_initialized; }
	const SpellStoreModel &model() const { return m_model; }
	int buttonState(int index) const { return index >= 0 && index < SpellStoreModel::MAX_BUTTONS ? m_state[index] : 0; }
	int layout() const { return m_layout; }
	// the image of the RenderImage clip at `clipPath` ("_levelN.SpellStore.Buttons.SpellK..."), "" none
	std::string imageForClip(const std::string &clipPath) const;
	static const char *stateName(int state);

private:
	int computeLayout() const; // RW 0x822DD7
	SpellStoreModel m_model;
	GameLogic *m_logic = nullptr;
	Player *m_player = nullptr;
	const GameTextSource *m_text = nullptr;
	bool m_initialized = false;
	bool m_closing = false;
	int m_layout = -1;
	int m_points = -1;
	int m_help = -1;
	bool m_helpShown = false;
	bool m_helpSent = false;
	int m_state[SpellStoreModel::MAX_BUTTONS];
	std::vector<GameMessage> m_out;
};
