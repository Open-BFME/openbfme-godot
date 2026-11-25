// OpenBFME. GPL-3.0.
//
// DeflectSpecialPower (lane MODGAP-1, group 5, stop S-140 / S-5601): the shield wall of the spear carriers (Haradrim and Muhad lancers, Rohirrim spearmen, the Arnor
// lancers). Ported from the RotWK binary (caveat S-001); the BFME2 decompilation (Open-BFME-2 Code/GameEngine/Source/GameLogic/Object/SpecialPower/
// DeflectSpecialPower.cpp, byte-matched C++) names the layout and the two flags.
//
// TARGET FACTS (RotWK game.dat, each read from the disassembly):
//   * data (parse RW 0x8C98D0): the update + special power base table RW 0x9911B3 (SpecialPowerTemplate, InitiateSound, StartsPaused) and an empty own table (RW 0xC84858);
//   * module (constructor RW 0x8C98EB over the base RW 0x991660; vtables RW 0xC7470C, +0xC 0xC74650, update +0x10 0xC74644, special power update +0x20 0xC74620, special
//     power +0x24 0xC650F0, collide +0x38 0xC74608): + 0x3C "active", + 0x3D "pending", both 0;
//   * initiate (special power update slot 0, RW 0x8C99DB): nothing while active or pending; else pending and the module wakes next frame (RW 0x850C32(object, 1));
//     isActive (slot 2, RW 0x9833D3) = the active byte;
//   * update (RW 0x8C9A86): neither flag: sleep for ever. The object is MOVING (RW 0x68BE97: its locomotor's current speed, RW 0x68B34C, above 0.1 = RW 0xBD83D4):
//     not active: stay awake (the power waits until the unit stands); active: deactivate, clear model condition 158 DEFLECT_SPECIAL_POWER (word 4 bit 30 of the
//     object's condition bits, RW 0x68B53C refreshes the drawable) when set, sleep for ever. Standing: pending and not active: pending off, active on, condition 158
//     set when it was not. Sleep none;
//   * onCollide (collide interface slot 0, RW 0x8C9A3C): active, `other` given and its template's KindOf DEFLECT_BY_SPECIAL_POWER (bit 102, template byte + 0x114 bit 6):
//     every behavior module of `other` that answers the projectile update interface (behavior slot 18, +0x48) gets projectileNowJammed (slot 5, RW 0x85E54D: kill with
//     damage type 8, death type 0) with the owner as the (unused) argument;
//   * xfer (RW 0x008C99FB): SpecialPowerUpdateBase::xfer (RW 0x009912DB), then early return on XFER_CRC (0x008C9A0D .. 0x008C9A12, Xfer::isLightCRC);
//     otherwise version 1 with m_active and m_pending.
//   * lifecycle/identity: ctor RW 0x008C98EB, dtor RW 0x008C99BF -> RW 0x008C994C -> SpecialPowerUpdateBase dtor RW 0x0099113F,
//     loadPostProcess RW 0x008E38D8 (no-op RW 0x0063F3BF), getCollide RW 0x008C993F, getModuleName RW 0x008C9939, getModuleNameKey RW 0x008C997A.
// WHY IT DOES NOTHING IN VANILLA 2.01 DATA (stop S-5601, INFERENCE from the data): nothing issues SPECIAL_DEFLECT_PROJECTILES (index 44): the command button
// Command_SpecialAbilityShieldWall is commented out in commandbutton.ini and its slot in commandset.ini too, no module data names the power, and the RW callers of the
// special-power-by-type lookup (RW 0x691526) push other types; and every object with KindOf DEFLECT_BY_SPECIAL_POWER (25 definitions) also has NO_COLLIDE, which the
// collision manager does not register (RW 0x68E31F, CombatState::processProjectileContacts), so no contact of a projectile with a spear carrier reaches onCollide.
// The module is ported whole for a script or a later data set that does trigger it.

#pragma once

#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/SiegeModules.h"

class ModuleFactory;
class StateHasher;

// RW 0x008C9AFB (DeflectSpecialPowerModuleData::DeflectSpecialPowerModuleData)
class DeflectSpecialPowerModuleData : public SpecialPowerUpdateBaseModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8C98D0
};

class DeflectSpecialPower : public SpecialPowerUpdateBase, public CollideModuleInterface
{
public:
	OBFME_MODULE_XFER(DeflectSpecialPower) // RW 0x008C99FB
	DeflectSpecialPower(Thing *thing, const DeflectSpecialPowerModuleData *data); // RW 0x008C98EB
	~DeflectSpecialPower() override = default;                                    // RW 0x008C99BF -> RW 0x008C994C -> RW 0x0099113F
	CollideModuleInterface *getCollide() override { return this; }                // RW 0x008C993F
	UpdateSleepTime update() override;                                            // RW 0x008C9A86
	void crc(StateHasher &h) const override;

	// ---- the special power update interface (+ 0x20) ----
	void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra) override; // RW 0x8C99DB
	bool isActive() const override { return m_active; }                                                                                          // RW 0x9833D3
	// ---- the collide interface (+ 0x38) ----
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8C9A3C
	bool wouldLikeToCollideWith(const Object &) const override { return false; }       // RW 0x7FEAC1
	// onCollide acts only for a DEFLECT_BY_SPECIAL_POWER projectile, never for the AI units AIWorld::processCollisions pairs: ObjectCollide::pairIsInert may skip it
	bool pairFactsKnown() const override { return true; }

	bool pending() const { return m_pending; }
	static DeflectSpecialPower *of(const Object &obj);
	static const char *stopLine();
	static void registerClass(ModuleFactory &modules);

private:
	bool m_active = false;  // + 0x3C
	bool m_pending = false; // + 0x3D
};
