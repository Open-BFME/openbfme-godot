// OpenBFME. GPL-3.0.
//
// TheGlobalWeatherSystem (RotWK RW 0xDE772C, a subsystem of the init order): the weather a special power sets (ChangeWeather) and the ONE map-wide
// attribute modifier of the weather-based powers (AttributeModifierWeatherBased: Darkness, Freezing Rain, Cloud Break, Sunflare). Lane SPELL-2.
// There is no BFME1 / ZH donor (BFME2 class); everything below is read from the RotWK binary.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * fields: + 0x10 the current weather (an index into RW 0xDB0074: NONE, CLOUDY, RAINY, CLOUDYRAINY, SUNNY; 5 = MOUNTED is "no change", the
//     SpecialPowerModuleData default), + 0x18 the modifier's affect kind (0 none, 1 everyone, 2 evil players' objects, 3 good players' objects),
//     + 0x1C the ObjectFilter handle, + 0x20 the ModifierList name, + 0x24 the caster's player mask (1 << Player + 0x54), + 0x28 the weather's
//     remaining frames, + 0x2C the modifier's remaining frames, + 0x58 the AntiCategory mask (15 categories);
//   * setWeatherModifier (RW 0x71A13D, kind, filter, player, name, anti, frames): clear() first, store the arguments, then apply(obj, false) to
//     every object in the object list order (RW 0x97338F, next at Object + 0x8C);
//   * clear (RW 0x71A04F): apply(obj, true) to every object, then kind = 0, name = "", anti = 0, mask = 0, frames = 0;
//   * setWeather (RW 0x71A024, weather, burn decay, frames): only when the weather differs: + 0x10 = weather, + 0x28 = frames, RW 0xDE46A8 + 0x98
//     = burn decay (an unidentified subsystem's field, kept here), then RW 0x719DEE (the client's weather change, an FX event here: S-922);
//   * update (RW 0x71A09E, vtable slot 0x28; GameLogic::update phase 1 calls it at RW 0x62E8BE): --(+0x2C), reaching 0 from 1 clears; --(+0x28),
//     reaching 0 from 1 sets the weather NONE with burn decay 0 and 0 frames. Both count down as unsigned 32-bit values (a 0 stays 0 after the
//     wrap only through the `was != 0` test);
//   * apply (RW 0x719C69, object, remove): nothing for a null object or kind 0; kind 2 skips an object whose controlling player's template is not
//     evil (PlayerTemplate + 0x1BC), kind 3 one whose is; then the filter must allow it (RW 0x7640C1) relative to the player of the mask (RW
//     0x6A85B6; none when the mask is 0); then the list is added (RW 0x68F1A8, the list's own duration) or removed (RW 0x68F259), and with a non-empty
//     anti mask the object's AttributeModifierPool disables those categories until 999999999 (add) or 0 (remove) (RW 0x804FCC);
//   * Object::initObject (RW 0x693D0C) calls apply(obj, false) for every new object (RW 0x694073).
// INFERENCE: the weather at game start is NONE (0); the map's weather setting is not traced (S-922).

#pragma once

#include "Common/StateHash.h"

#include <memory>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
struct ObjectFilter;

class GlobalWeatherSystem
{
public:
	enum AffectKind
	{
		AFFECT_NONE = 0,
		AFFECT_ALL = 1,
		AFFECT_EVIL = 2,
		AFFECT_GOOD = 3
	};
	enum
	{
		WEATHER_NONE = 0,
		WEATHER_NO_CHANGE = 5
	};
	explicit GlobalWeatherSystem(GameLogic &logic) : m_logic(logic) {}

	void setWeatherModifier(int kind, std::shared_ptr<const ObjectFilter> filter, const Player *player, const std::string &listName, unsigned antiMask,
		unsigned frames);                                                    ///< RW 0x71A13D
	void clear();                                                            ///< RW 0x71A04F
	void setWeather(int weather, unsigned burnDecay, unsigned frames);        ///< RW 0x71A024
	void update();                                                           ///< RW 0x71A09E
	// RW 0xDE46A8 + 0x98 written directly (Darkness RW 0x8C93C4, CloudBreak RW 0x8C8D10)
	void setBurnDecay(unsigned value) { m_burnDecay = value; }
	void apply(Object &obj, bool remove);                                    ///< RW 0x719C69
	// a new game (GameLogic::reset, after every object went): the constructor's state
	void reset();

	int weather() const { return m_weather; }
	int affectKind() const { return m_kind; }
	const std::string &modifierName() const { return m_listName; }
	unsigned modifierFramesLeft() const { return m_modifierFrames; }
	unsigned weatherFramesLeft() const { return m_weatherFrames; }
	unsigned burnDecay() const { return m_burnDecay; }
	unsigned long long weatherChanges() const { return m_weatherChanges; }
	void crc(StateHasher &h) const;
	static std::vector<std::string> stopLines();

private:
	const Player *maskPlayer() const; ///< RW 0x6A85B6
	GameLogic &m_logic;
	int m_weather = WEATHER_NONE;                 ///< + 0x10
	int m_kind = AFFECT_NONE;                     ///< + 0x18
	std::shared_ptr<const ObjectFilter> m_filter; ///< + 0x1C
	std::string m_listName;                       ///< + 0x20
	unsigned m_playerMask = 0;                    ///< + 0x24
	unsigned m_weatherFrames = 0;                 ///< + 0x28
	unsigned m_modifierFrames = 0;                ///< + 0x2C
	unsigned m_antiMask = 0;                      ///< + 0x58
	unsigned m_burnDecay = 0;                     ///< RW 0xDE46A8 + 0x98
	unsigned long long m_weatherChanges = 0;      ///< RW 0x719DEE calls (the client's weather change)
};
