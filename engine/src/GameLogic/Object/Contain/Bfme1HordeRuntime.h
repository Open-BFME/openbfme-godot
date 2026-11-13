// OpenBFME. GPL-3.0.
// BFME1 1.03 horde-system variant. BFME2/RotWK retain HordeContain.
#pragma once
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"

class Bfme1HordeData : public HordeContainBehaviorData
{
public:
	float narrowScale = 0.4f; // BFME1 data ctor 0x002472D0, +0x2DC.
	static void buildFieldParse(MultiIniFieldParse &p);
};

class Bfme1HordeContain : public HordeContain
{
public:
	Bfme1HordeContain(Thing *, const HordeContainBehaviorData *);
	const std::type_info &xferClass() const override { return typeid(Bfme1HordeContain); }
	UpdateSleepTime update() override;
	Coord3D getMemberFormationPosition(const Object *) const override;
	bool beginNativeMelee(Object &) override;
	bool endNativeMelee() override;
	bool tickNativeMelee(Object &) override;
	bool nativeTargetReady(Object &) override;
	bool usesNativeMelee() const override { return true; }
	void endReform() override;
	void removeFromContain(Object *) override;
	Object *nativeAttackRepresentative(bool longestRange) const;
	void recordBackUp(Object *) override;
	void setMembersBusy() override;
	void beginQuarrel(float,float,const ConditionFlags &,const ConditionFlags &) override;
	int emotionMemberOrder(Object &,Object *,Coord3D &,float &) override;
	void setNativeGather(bool on) override { m_gather=on; } // 0x002325E0.
	bool frozenFormation() const { return m_frozen; }
	void crc(StateHasher &) const override;
	void xfer(Xfer &) override;
protected:
	bool hasVariantMemberPositions() const override { return true; }
	bool participatesInMemberPass(const Object &) const override;
	void memberOrder(Object &, const Coord3D &, float, bool) override;
	void moveHub(Object &, const Coord3D &, float) override;
	Coord3D nativeRotatedOffset(float x, float y) const;
private:
	struct MemberState
	{
		int phase = 0;
		Coord3D position{};
		bool invalid = true;
		unsigned until = 0, last = 0;
	};
	std::vector<MemberState> m_formation;
	bool m_frozen = false;
	bool m_retry = false;
	bool m_gather = false;
	bool m_force = false; // BFME1 primary +0xE9.
	float m_narrowScale;
	MemberState *record(const Object &);
	bool findMeleeDestination(Object &, Coord3D &, Object &, const Coord3D &, unsigned &, bool);
};
