// OpenBFME. GPL-3.0.
//
// FXEvents (lane FX-2): the calls RotWK's logic makes into the client's effect system, as events. Retail calls the client synchronously from inside the
// logic frame (FXList::doFXObj / doFXPos through their static wrappers, Drawable::handleWeaponFireFX); the port keeps the call points and the moment, and
// hands each call to an FXEventSink the client installs (LiveFX). Nothing here is read back by the logic: the log is not part of the state hash, holds no
// floating arithmetic (positions and transforms are copied as they are) and draws no random numbers (a pick among several FX lists is the logic's draw,
// made at the call site exactly where retail makes it).
//
// TARGET FACTS (RotWK game.dat, caveat S-001) of the call sites that emit, each cited where it is ported:
//   * Weapon::fireWeaponTemplate RW 0x6CC915: when the source has a drawable (RW 0x70E013), the fire FX is FireFlankFX (+0xB4) when the victim is
//     flanked by the source (RW 0x68FB63) and the weapon has one, else FireFX (+0xA4: the REGULAR entry only, the veterancy entries are not read here);
//     none while the logic frame is below the weapon's +0x30 frame; the call is made when the source's controlling player is the local player
//     (RW 0x68B749), or the source is not stealthed for the local player (RW 0x694C0D), or the weapon has PlayFXWhenStealthed (+0x135); then
//     Drawable::handleWeaponFireFX (RW 0x671402: recoil, every draw module's slot 0x58 with slot, barrel, fx, WeaponSpeed +0x68, the victim position),
//     and when no draw module took it and there is an fx, FXList::doFXObj(fx, source, victim) through RW 0x4B1B5A. The local-player and stealth tests
//     depend on the viewing client: the event is emitted for every shot and the client applies them (WEAPON_FIRE_FX).
//   * FXListDie / SlowDeathBehavior / StructureCollapseUpdate / ActiveBody::doDamageFX / BezierProjectileBehavior ground hits: see their ports.
//
// The site name of an event is a static string ("FireFX", "FXListDie", ...); `fxList` points into template data that outlives the game.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Object;

struct FXEvent
{
	enum Kind : std::uint8_t
	{
		OBJECT_FX,      ///< FXList::doFXObj(fx, primary, secondary) (RW 0x4B1B5A -> 0x5E2282)
		POSITION_FX,    ///< FXList::doFXPos(fx, position, transform or null, 0, null) (RW 0x494615)
		WEAPON_FIRE_FX, ///< the fire FX block of RW 0x6CC915: the client applies the visibility tests, then handleWeaponFireFX / doFXObj
		OBJECT_SOUND    ///< an audio event played on the primary object (`fxList` holds the audio event name): SlowDeathBehavior's Sound (RW 0x8609A8)
	};
	Kind kind = OBJECT_FX;
	const char *site = "";          ///< the call site ("FireFX", "FXListDie", "SlowDeath INITIAL", ...)
	UnsignedInt frame = 0;          ///< the logic frame of the call
	const std::string *fxList = nullptr; ///< never null in an emitted event; the FXList name as the INI wrote it (OBJECT_SOUND: the audio event name)
	// non-null: the call picks one entry of this list with the CLIENT random (RW 0x6D32E4; SlowDeathBehavior's FX and Sound lists); the player draws the pick,
	// `fxList` is then the list's first entry. The list is template data; it is never empty in an emitted event (a "None" entry may be picked and plays nothing)
	const std::vector<std::string> *choices = nullptr;
	ObjectID primary = INVALID_ID;  ///< the object the FX plays on (OBJECT_FX, WEAPON_FIRE_FX: the shooter)
	ObjectID secondary = INVALID_ID;///< the second object of doFXObj (the victim, the attacker of a damage FX)
	Coord3D position{ 0.0f, 0.0f, 0.0f }; ///< the primary's position at the call, or the position of POSITION_FX
	bool hasTransform = false;      ///< transform holds the primary's 3x4 matrix (rows: basis row r, then the translation) at the call
	float transform[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	std::array<std::uint32_t, 19> conditions{}; ///< the primary's model condition bits at the call (Object + 0x10C)
	Coord3D secondaryPosition{ 0.0f, 0.0f, 0.0f }; ///< WEAPON_FIRE_FX: the victim position handed to handleWeaponFireFX
	int weaponSlot = 0, barrel = 0; ///< WEAPON_FIRE_FX
	float weaponSpeed = 0.0f;       ///< WEAPON_FIRE_FX: WeaponTemplate + 0x68
	bool playWhenStealthed = false; ///< WEAPON_FIRE_FX: WeaponTemplate + 0x135
	const std::string *bone = nullptr; ///< OBJECT_FX of a LevelUpFx entry with a bone (lane INTEG-1, RW 0x8211EC): doFXPos at the bone; template data
};

class FXEventSink
{
public:
	virtual ~FXEventSink() = default;
	// called at the moment of the retail call, inside the logic frame; must not change logic state or draw logic random numbers
	virtual void onFXEvent(const FXEvent &event) = 0;
};

class FXEventLog
{
public:
	// the client's player (null: events are only logged). Not owned.
	void setSink(FXEventSink *sink) { m_sink = sink; }
	FXEventSink *sink() const { return m_sink; }

	// a new logic frame: the frame's events are dropped (consumers read them through the sink, or right after the frame)
	void beginFrame() { m_frame.clear(); }

	// fills an event for `obj` (position, transform, model conditions) at `frame`
	static FXEvent objectEvent(FXEvent::Kind kind, const char *site, UnsignedInt frame, const std::string &fxList, const Object &obj);
	// records and forwards one event; an empty name or "None" (any case: RW parseFXList stores NULL for it) is not a call and is dropped
	void emit(const FXEvent &event);

	const std::vector<FXEvent> &frameEvents() const { return m_frame; }
	unsigned long long total() const { return m_total; }
	// events per site since the game began, ordered by site name
	const std::map<std::string, unsigned long long> &perSite() const { return m_perSite; }
	// true when the name stands for an FXList (not empty, not "None")
	static bool isFXName(const std::string &name);
	// the stop lines of the logic side of the effect calls (S-680, S-681, S-683, S-685), for the reports (LiveFX adds them to its own)
	static std::vector<std::string> stops();

private:
	FXEventSink *m_sink = nullptr;
	std::vector<FXEvent> m_frame;
	std::map<std::string, unsigned long long> m_perSite;
	unsigned long long m_total = 0;
};
