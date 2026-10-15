// OpenBFME. GPL-3.0.
//
// BuffDraw (lane SPELL-4): the buffs of the FXList BuffNugget (non-complex): the glow model a power puts on the units it touches, e.g. Heal's
// HealingBuff (EXHealbuff) and Rallying Call's LeadershipBuff*Lvl1 (the owner: the 5-point powers show "just a circle"; the port noted the nugget and drew
// nothing). Client-only: it reads drawables and templates, never changes the game.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 twins tier A with byte-matched decomp source):
// - BuffNugget::doFXObj (RW 0x5E2838's class, BFME2 decomp BuffNuggetFXNugget.cpp): a drawable needed; BuffLifeTime <= 0 turns the type off
//   (RW 0x671EC6 -> BuffManager::TurnOffBuff, not immediate); a complex buff is the glow-outline kind (asserted unsupported, MakeGlowOutlineBuff);
//   else a HORDE gets none, the template by KindOf (CAVALRY, ORC, INFANTRY, TROLL, MUMAKIL, SHIP, MONSTER, else BuffThingTemplate) through
//   TheThingFactory (none: nothing) and Drawable::TurnOnBuff (RW 0x671E56: the drawable's BuffManager + 0x450, made on first use);
// - BuffManager::TurnOnBuff (RW 0x764483, BFME2 decomp BuffManager.cpp:147): buff types 1 .. 8 only, nine 0x44-byte instances by type; every type is
//   sub type 1 (table RW 0xDC0138 equivalent): BuffInstance::MakeNormalBuff (RW 0x7641A2, decomp Rva00362518.cpp): no template or drawable: false;
//   an instance without a TBuff makes one (BuffLogic::addBuff RW 0x707FFF -> W3DBuffBuffer::addBuff RW 0x4D3376); another template replaces it (the
//   old TBuff released); the same template while it fades out (state 2) is put back to state 1 at opacity 1; then active, the expiry frame = logic
//   frame + BuffLifeTime (+ 0x14);
// - BuffInstance::TurnOff (decomp BuffManager.cpp): immediate releases the TBuff; else an active sub type 1 instance puts its TBuff into state 2
//   (fade out) and goes inactive;
// - TBuff (constructor RW 0x4D12FA): state 0 (fade in), opacity 0, visible = the drawable's + 0x441, position = the drawable's interpolated
//   position (RW 0x676711), scale = the drawable's instance scale (+ 0x200); W3DBuffBuffer::addBuff refuses a 1001st TBuff of a type;
// - TBuff::UpdateFade (RW 0x4D1417, decomp Rva000D1C10.cpp), once per drawn frame after the TBuff is filled: state 0 adds 1/12 (0.083333336) up to
//   1 (then state 1), state 2 subtracts 1/12 down to 0 (then state 3); the fill (RW 0x4D1ACD, decomp W3DBuffBufferFill.cpp) draws the buff model's
//   meshes at the TBuff's position + the mesh's translation, the vertices times the scale, the colour emissive * vertex colour * opacity.
// INFERENCE (stop S-4642): the expiry test (an active instance turned off when the logic frame reaches its expiry) and the TBuff's position following
//   the drawable every frame are taken from the fields the code keeps (+ 0x14, the drawable pointer), the updater was not read; a TBuff in state 3 is
//   released then; the model is drawn through the live object instancer (its own materials, the opacity as the instance fade) instead of the
//   W3DBuffBuffer's additive pass; the camera-facing ALIGNED meshes are not turned to the target.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ClientBuffs
{
public:
	enum
	{
		MAX_TYPES = 9,          ///< BuffManager's instances (types 1 .. 8 are used)
		MAX_PER_TEMPLATE = 1000 ///< W3DBuffBuffer::addBuff: a type's set holds below 1000
	};
	enum FadeState
	{
		FADE_IN = 0,
		ON = 1,
		FADE_OUT = 2,
		GONE = 3
	};
	struct Buff
	{
		ObjectID object = INVALID_ID;
		int type = 0;              ///< BuffType 1 .. 8
		std::string thing;         ///< the buff ThingTemplate (BuffInfantryTemplate, ...)
		std::string model;         ///< its W3DBuffDraw ModelName
		bool active = false;       ///< BuffInstance + 0x04
		unsigned expireFrame = 0;  ///< BuffInstance + 0x14: the logic frame it is turned off at
		int state = FADE_IN;       ///< TBuff + 0x08
		float opacity = 0.0f;      ///< TBuff + 0x0C
		std::uint64_t serial = 0;  ///< a new TBuff gets a new serial (the device's instance follows it)
	};

	// Drawable::TurnOnBuff / MakeNormalBuff. False when nothing was made (type outside 1 .. 8, no template / model, the template's 1000 TBuffs)
	bool turnOn(ObjectID object, int type, const std::string &thing, const std::string &model, unsigned lifeFrames, unsigned logicFrame);
	// BuffManager::TurnOffBuff
	void turnOff(ObjectID object, int type, bool immediate);
	// the object is gone: its buffs go at once (its drawable and BuffManager are deleted)
	void removeObject(ObjectID object);
	// once per drawn (client) frame: the expiry (S-4642), then UpdateFade of every TBuff; the TBuffs that reached state 3 are released
	void update(unsigned logicFrame);
	// `seconds` of render time: update() once per 1/30 s (RotWK draws at 30 frames a second, S-456), at most 8 at once (a stall is not caught up)
	void advance(double seconds, unsigned logicFrame);
	const std::map<std::pair<ObjectID, int>, Buff> &buffs() const { return m_buffs; }
	size_t count() const { return m_buffs.size(); }

	// TBuff::UpdateFade (RW 0x4D1417)
	static void updateFade(int &state, float &opacity);

private:
	std::map<std::pair<ObjectID, int>, Buff> m_buffs; ///< by (object, type): BuffManager's instance array
	std::map<std::string, size_t> m_perTemplate;     ///< the TBuffs of each template (W3DBuffBuffer's per-type set)
	std::uint64_t m_serial = 0;
	double m_clock = 0.0; ///< render time not yet stepped
	void release(std::map<std::pair<ObjectID, int>, Buff>::iterator it);
};
