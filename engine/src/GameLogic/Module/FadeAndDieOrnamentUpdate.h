// OpenBFME. GPL-3.0.
// FadeAndDieOrnamentUpdate (both editions: RotWK 2.01 RW 0x007A772B / 0x007A7801; DONOR BFME2 1.06 0x003A439D):
// Ornament object that optionally follows its parent object (killing itself if the parent is missing or dead)
// and drives its Drawable opacity through a 5-stage ADSR envelope (OrnamentData, RW 0x004C894B / 0x0073A74B / 0x00740659).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;
class Object;
class StateHasher;

// RW 0x004C894B (init), RW 0x0073A74B (INI parse), RW 0x00740659 (update): 40-byte ADSR opacity envelope.
struct OrnamentEnvelopeData
{
	enum State : int
	{
		STATE_INITIAL_DELAY = 0,
		STATE_ATTACK = 1,
		STATE_DECAY = 2,
		STATE_SUSTAIN = 3,
		STATE_RELEASE = 4,
		STATE_DONE = 5
	};

	float initialOpacity = 1.0f;         ///< +0x00 (RW 0x004C8968: 1.0f)
	float peakOpacity = 1.0f;            ///< +0x04 (RW 0x004C896C: 1.0f)
	float sustainOpacity = 1.0f;         ///< +0x08 (RW 0x004C8971: 1.0f)
	UnsignedInt elapsedFrames = 0;       ///< +0x0C (RW 0x004C8955: 0)
	UnsignedInt initialDelay = 1;        ///< +0x10 (RW 0x004C8976: 1)
	UnsignedInt attackTime = 1;          ///< +0x14 (RW 0x004C8979: 1)
	UnsignedInt decayTime = 1;           ///< +0x18 (RW 0x004C897C: 1)
	UnsignedInt sustainTime = 0xFFFFFFFFu; ///< +0x1C (RW 0x004C8959: -1)
	UnsignedInt releaseTime = 0xFFFFFFFFu; ///< +0x20 (RW 0x004C895D: -1)
	int state = STATE_INITIAL_DELAY;     ///< +0x24 (RW 0x004C8961: 0)

	// RW 0x00740659: advances the envelope by one logic frame and writes the opacity to *out.
	bool update(float *out);
};

// RW table 0x00C08668, ctor RW 0x0065535A (envelope init at RW 0x0065537D) (DONOR BFME2 0x002555AC).
class FadeAndDieOrnamentUpdateModuleData : public ModuleData
{
public:
	std::string m_attachToTargetBone; ///< +0x08 AttachToTargetBone
	OrnamentEnvelopeData m_envelope;  ///< +0x0C Envelope
	bool m_followTarget = true;       ///< +0x34 FollowTarget (RW 0x0065539E: default true)

	static void buildFieldParse(MultiIniFieldParse &p);
};

// Primary vtable RW 0x00C316D4:
//   slot 0 scalar deleting dtor RW 0x007A777A -> ~FadeAndDieOrnamentUpdate RW 0x007A76B0
//   slot 1 loadPostProcess RW 0x008A7734
//   slot 2 getModuleName RW 0x007A76C9 ("FadeAndDieOrnamentUpdate")
//   slot 3 xfer RW 0x007A7796
//   slot 4 getModuleNameKey RW 0x007A76E6
// UpdateModuleInterface vtable RW 0x00C316C8:
//   slot 0 update RW 0x007A7801
class FadeAndDieOrnamentUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(FadeAndDieOrnamentUpdate)

	FadeAndDieOrnamentUpdate(Thing *thing, const FadeAndDieOrnamentUpdateModuleData *data); // RW 0x007A772B
	~FadeAndDieOrnamentUpdate() override = default; // RW 0x007A76B0 / RW 0x007A777A

	UpdateSleepTime update() override; // RW 0x007A7801
	void crc(StateHasher &hasher) const override;

	// RW 0x007A76CF: called by OCL FADE_AND_DIE_ORNAMENT (RW 0x005F2287) and loadPostProcess (RW 0x008A7734).
	void setParent(const Object *parent);
	// RW 0x007A77EA: resolves m_parentId via TheGameLogic->findObjectByID.
	Object *getParentObject() const;
	// RW 0x008A7734: re-validates m_parentId against TheGameLogic after load.
	void loadPostProcess();

	ObjectID parentId() const { return m_parentId; }
	float opacity() const { return m_opacity; }
	int envelopeState() const { return m_envelope.state; }

	static void registerClass(ModuleFactory &modules); // RW 0x00659D52, RW 0x007A76C9, RW 0x007A76E6

private:
	const FadeAndDieOrnamentUpdateModuleData *m_data = nullptr;
	ObjectID m_parentId = INVALID_ID; ///< +0x20
	float m_opacity = 0.0f;           ///< +0x24
	OrnamentEnvelopeData m_envelope;  ///< +0x28
};
