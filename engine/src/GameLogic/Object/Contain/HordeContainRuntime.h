// OpenBFME. GPL-3.0.
//
// HordeContain at runtime (lane LOGIC-1): the module a horde object's template declares as `Behavior = HordeContain` or
// `HorseHordeContain`. It binds the typed module data (HordeContainBehaviorData, lane MAPOBJ-1) and HordeContainCore (lane HORDE-1: the slot
// table, member <-> slot maps, free list and slot world positions) to a live Object, so a horde creates its member objects and keeps them on
// their formation slots.
//
// SOURCES (RotWK game.dat, caveat S-001; DONOR Open-BFME-1 Object/Contain/HordeContain*.cpp):
//   * the slot table is built when the contain is created (HORDE-1 buildSlots RW 0x877751; with RandomOffset it draws from the logic RNG, x
//     before y, lines 1575 / 1579); a member takes the first free slot whose rank's UnitType matches its template (RW 0x873F30, 0x73D5C2);
//   * HordeContain::createPayload is a virtual of TransportContain that HordeContain overrides (B1 HordeContainCreatePayload.cpp, retail
//     0x0023C000; RotWK RW 0x871B9B): the base creates the InitialPayload members, then (no producer) (int)((100 - H) * 0.01 * count) members are
//     destroyed, H = HordeContain + 0x2A8, which only the constructor writes (100, RW 0x872972): none ever is (lane DECOMP-1);
//   * HordeContain::onDelete destroys every contained object (B1 HordeContainOnDelete.cpp: its own owned vector, then OpenContain::onDelete =
//     ZH OpenContain.cpp:843-852: destroyObject on each rider);
//   * a member's world position is the slot offset rotated by the horde's angle plus the horde's position (RW 0x875847, HordeContainCore).
//
// WHAT IS INFERENCE (stop S-149): WHEN the payload is created (here: in the CreateModule::onCreate step, after registerObject; retail's call
// site is not located: RW TransportContain::update, 0x86B86D, does not create it; B1 `createPayload` is reached through a virtual slot), that
// the members belong to the horde's own team (ZH TransportContain uses the controlling player's DEFAULT team), that a member faces its
// horde's angle and takes the HORDE_MEMBER status, and that a member that
// finds no free slot of a matching rank stands at the horde's position without a slot.
// MOVEMENT (lane MOVE-1, GameLogic/Object/Contain/HordeMemberPass.cpp): in a game with an AIWorld update() is the movement part of HordeContain::update: the
// TRANSPORT_MOVING mirror, the dirty flag, the member pass (snap, turn, walk to the slot, wait when ahead of it), the reform of the HORDE mover (greedy slot
// reassignment). In a game without an AIWorld (the LOGIC-1 fixtures) update() sleeps forever and the members keep the slots they were given.
// NOT PORTED (stop S-149 / S-222): the banner carriers, melee (M2), the flank history, the layer sync.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

class HordeContainCore;
struct HordeContainModuleData;
struct MeleeBehaviorModuleData;
class HordeContainBehaviorData;
class ModuleFactory;

// the interface other modules ask a horde through (ZH names it HordeContainInterface)
class HordeContainInterface
{
public:
	virtual ~HordeContainInterface() = default;
	// the slot index a member holds, or -1
	virtual int getMemberSlot(const Object *member) const = 0;
	virtual unsigned getSlotCount() const = 0;
	// ---- lane PROD-1 (what ProductionUpdate and the exit modules ask a horde, RW HordeContainInterface at module + 0x11C, vtable 0xC5B1F8) ----
	// slot 9 (RW 0x87048F): the template name of the InitialPayload when it has exactly one entry, else ""
	virtual std::string getPayloadMemberTemplateName() const = 0;
	// slot 10 (RW 0x86C8AC): the TransportContain `Slots` capacity (module data + 0x98)
	virtual int getSlotCapacity() const = 0;
	// slot 11 (RW 0x873F30): a member made by a factory joins without being placed on its slot; false when it already is a member
	virtual bool acceptCreatedMember(Object *member) = 0;
	// slot 7 (RW 0x877D89): the world position of the member's slot for the horde's current transform
	virtual Coord3D getMemberFormationPosition(const Object *member) const = 0;
	// slot 0xC8 (RW 0x86EF13, lane MOVE-3): called by the pathfinder's updateGoal (RW 0x8E24D3) once the horde's goal changed: unless the horde melees (+ 0x184),
	// every living member's goal is reserved at its slot turned by the goal's angle around the goal's position
	virtual void reserveMemberGoals() = 0;
	// ---- MOVE-1 (the member pass, HordeMemberPass.cpp; horde spec 2.2 / 2.3 / 2.5) ----
	// the horde's own locomotor is writing its transform: the members are not teleported with it (they walk to their slots)
	virtual void setLocomoting(bool on) = 0;
	// RW contain vfuncs +0x3C / +0x40 around the HORDE mover's reform snap (RW 0x5E6D4F): the members re-form to the rotated slots
	virtual void beginReform() = 0;
	virtual void endReform() = 0;
	// RW contain vfunc +0x1C0: the members stand on their slots (the HORDE mover waits for it, x0.1 speed, while it is false)
	virtual bool isFormationReady(float angle) const = 0;
	// HordeAIUpdate's once-a-second formation refresh (B1 HordeAIUpdate_update.cpp: horde->updateFormation(1), stamped with the frame): the member pass runs again
	virtual void updateFormation() = 0;
	// the frame of the last updateFormation (B1 bfmeGetFormationRefreshValue, 0x23727)
	virtual unsigned formationRefreshFrame() const = 0;
	// ---- lane XP-1 (GameLogic/Object/Contain/HordeContainExperience.cpp) ----
	// slot 0xC0 / 4 (RW 0x873A02): a member's kill experience joins the pool of its template; the members of that template and the horde follow the pool
	virtual void addExperience(Object *member, float amount) = 0;
	// slot 0x1D8 / 4 (RW 0x870E50): a ModifierList reaches the members (not a LEVEL one) and always the horde's own pool
	virtual void addAttributeModifier(const std::string &listName, int duration) = 0;
	virtual void removeAttributeModifier(const std::string &listName) = 0; ///< slot 0x1DC (RW 0x870F75, lane INTEG-1)
	// ---- lane GARRISON-1 (a horde entering a HordeGarrisonContain and leaving it; GameLogic/Object/Contain/HordeGarrisonLink.cpp) ----
	// slot 0x18 (RW 0x86EE1B): `member` is contained by this horde or is one of its members on the way into a garrison (the map at interface + 0x54)
	virtual bool isMemberOrEntering(const Object &member) const = 0;
	// slot 0xF4 (RW 0x86D755): no member is on the way any more (the map's size + 0x58 is 0)
	virtual bool allMembersEntered() const = 0;
	// slot 0xA8 (RW 0x86EBF4): the member goes on the way (its id joins the map) and leaves this horde's contain list (contain slot 0xA4)
	virtual void releaseMemberForGarrison(Object *member) = 0;
	// slot 0x20 (RW 0x872A7F -> module slot 0x98, RW 0x8757FC): the member arrived: its id leaves the map, it rejoins this horde (contain slot 0x9C) and takes the
	// horde object's upgrades (slot 0x174)
	virtual void acceptMemberFromGarrison(Object *member) = 0;
	// slots 0x100 / 0x104 (RW 0x872AA1 / 0x872AAE): the frame the last member entered (interface + 0x1EC; 0 before the first)
	virtual void setLastMemberEnteredFrame(unsigned frame) = 0;
	virtual unsigned lastMemberEnteredFrame() const = 0;
	// slots 0x124 / 0x128 / 0x12C (RW 0x872ADF / 0x872B76 / 0x872AE7): the garrisoned byte (interface + 0x188); clearing it wakes the contain's update next frame
	virtual void setGarrisoned(bool on) = 0;
	virtual bool isGarrisoned() const = 0;
	// slot 0x80 (RW 0x875DC3): the horde enters `container` (its AI gets command 0x3D, every member is released and ordered to enter, AIGroup command RW 0x7724BE)
	virtual void enterContainer(Object *container, int source) = 0;
	// slot 0xFC (RW 0x876B25): one frame of AIHordeEnterState: the members near the container enter, the idle ones are ordered again, one late member is forced
	virtual void pushMembersIntoContainer(Object *container) = 0;
	// slot 0x7C (RW 0x875C93): every member is released (slot 0xA8) and the released members walk as a group to the container's position (RW 0x774897)
	virtual void releaseMembersToward(Object *container, int source) = 0;
	// slot 0x84 (RW 0x875EFD): the horde's AI gets command 0x3E (AIHordeExitState)
	virtual void exitContainer(Object *container, int source) = 0;
	// the members on the way, in the map's (id) order
	virtual std::vector<ObjectID> membersEntering() const = 0;
	// ---- lane GARRISON-3 (a horde leaving a garrison through GarrisonContain's exit RW 0x87CA2B; GameLogic/Object/Contain/HordeGarrisonLink.cpp) ----
	// slot 0x98 (RW 0x86F18F): the horde object moves to the member (on the way, then in the contain list on a valid movement position for `about`) nearest to
	// the members' centroid; `about` gives the start position and the locomotor of the test (the exit passes the horde object itself)
	virtual void recentreOnMembers(Object *about) = 0;
	// slot 0x10 (RW 0x8759FF): the members on the way rejoin the horde (module slot 0x98, RW 0x8757FC), the slots fill in (RW 0x873D48), the members re-form to
	// the nearest slots (slot 0x40, RW 0x877E12) and take the horde's emotion / attack model conditions (RW 0x86DC38 -> RW 0x86D061); `regroup`: the extra tail
	// of the transports' exit (slot 0xCC RW 0x86F053, the horde's destination adjustment RW 0x6FE456)
	virtual void returnToFormation(bool regroup) = 0;
	// ---- lane MODULES-3: the emotion slots (GameLogic/Object/Contain/HordeEmotion.cpp) ----
	typedef std::array<std::uint32_t, 19> ConditionFlags; ///< model condition bits (Object::ModelConditionBits)
	virtual void setMembersBusy() = 0;                    ///< slot 0x13C (RW 0x876D4E)
	virtual void recordBackUp(Object *scarer) = 0;        ///< slot 0x19C (RW 0x878905)
	virtual void setCowering(bool on) = 0;                ///< slot 0x1A0 (RW 0x872B10)
	virtual bool isCowering() const = 0;                  ///< slot 0x1A4 (RW 0x872B1D)
	virtual void markDirty() = 0;                         ///< slot 0x1D0 (RW 0x78856D: interface + 4)
	virtual void beginQuarrel(float minDistance, float maxDistance, const ConditionFlags &spectators, const ConditionFlags &fighters) = 0; ///< slot 0x1A8 (RW 0x876DA5)
	virtual void endQuarrel() = 0;                        ///< slot 0x1AC (RW 0x870B91)
	virtual void setFacePoint(const Coord3D &p) = 0;      ///< slot 0x210 (RW 0x86C1C6)
	virtual void clearFacePoint() = 0;                    ///< slot 0x214 (RW 0x86C1E5)
	virtual bool attackedWithin(unsigned frames, ObjectID &attacker) const = 0; ///< slot 0x90 (RW 0x86EE52)
	// ---- lane MOVE-2: the horde's command hand-off (HordeAIUpdate::aiDoCommand RW 0x89E169 calls it before the command runs) ----
	// slot 0x14 (RW 0x87594C): members on their way into a garrison come back (slot 0x10, RW 0x8759FF(0)), a melee ends (slot 0x138, RW 0x86C0F9), then every member
	// that is not busy and not attacking `target` (or a member of its horde: RW 0x86BDD3) gets AI command 0x31 from the AI (busy): its attack ends, its target goes
	virtual void prepareMembersForCommand(Object *target) = 0;
	// ---- lane ARCHER-1: the HordeAttackNugget's fire (the nugget's slots 5 / 6, RW 0x911A58 / 0x911AC9, call these two slots of the source's horde interface;
	// GameLogic/Object/Contain/HordeMemberPass.cpp) ----
	// slot 4 (RW 0x875221): the released ranks attack `victim`, each member its own pick among the victim horde's members (contain slot 0x48, RW 0x86FA87:
	// the nearest within the member's range after a logic random factor 0.66 .. 1.33 on every distance, which spreads the shots over the horde)
	virtual void attackTargetNow(Object *victim, bool closestMemberOnly) = 0;
	// slot 0 (RW 0x875550): the released ranks attack the ground at `pos`
	virtual void attackPositionNow(const Coord3D &pos) = 0;
};

class HordeContain : public UpdateModule, public ContainModuleInterface, public CreateModuleInterface, public HordeContainInterface
{
public:
	HordeContain(Thing *thing, const HordeContainBehaviorData *data);
	~HordeContain() override;

	// Binds the runtime class for the HordeContain and HorseHordeContain classes (the typed data must already be bound,
	// MapHordeSpawn::bindHordeContainData)
	static void registerClasses(ModuleFactory &modules);

	// BehaviorModule
	ContainModuleInterface *getContain() override { return this; }
	CreateModuleInterface *getCreate() override { return this; }
	void onObjectCreated() override;
	void onDelete() override;
	void crc(StateHasher &hasher) const override;
	// UpdateModule
	UpdateSleepTime update() override;
	// lane IDLE-1 r2: HordeContain's / HorseHordeContain's / AODHordeContain's vslot 0x30 is RW 0x490AC4 (`xor eax, eax; inc eax; ret`): updates[1], run in
	// phase 5 after every AI update of updates[0], so the member pass and its hub (which clear MOVING, RW 0x8750B8 / 0x877BB8) have the last word in the frame
	SleepyUpdatePhase getUpdatePhase() const override { return PHASE_PHYSICS; }
	// ContainModuleInterface
	const ContainedItemsList *getContainedItemsList() const override { return &m_members; }
	void onDefect(Object *newOwner, bool permanent) override; // lane HERO-2: RW 0x86ED25
	void onDefectionEnded() override;                          // lane HERO-2: RW 0x86EDB0
	unsigned getContainCount() const override { return (unsigned)m_members.size(); }
	bool addToContain(Object *obj) override;
	void removeFromContain(Object *obj) override;
	void containReactToTransformChange() override;
	HordeContainInterface *getHordeContainInterface() override { return this; }
	// CreateModuleInterface
	void onCreate() override;
	void onBuildComplete() override {}
	// HordeContainInterface
	int getMemberSlot(const Object *member) const override;
	unsigned getSlotCount() const override;
	void setLocomoting(bool on) override { m_locomoting = on; }
	void beginReform() override;
	void endReform() override;
	bool isFormationReady(float angle) const override;
	void updateFormation() override;
	unsigned formationRefreshFrame() const override { return m_formationRefreshFrame; }
	// lane XP-1 (HordeContainExperience.cpp)
	void addExperience(Object *member, float amount) override;
	void addAttributeModifier(const std::string &listName, int duration) override;
	// RW 0x870F75 (slot 0x1DC, name, filter; the stances pass no filter): every member (contain slot 0x118) loses the list (Object::removeAttributeModifier), the
	// second list's members are not ported (S-486), then the horde's own AttributeModifierPoolUpdate (RW 0x8052FB) (lane INTEG-1)
	void removeAttributeModifier(const std::string &listName) override;
	// lane GARRISON-1 (HordeGarrisonLink.cpp)
	bool isMemberOrEntering(const Object &member) const override;
	bool allMembersEntered() const override { return m_garrisonEntering.empty(); }
	void releaseMemberForGarrison(Object *member) override;
	void acceptMemberFromGarrison(Object *member) override;
	void setLastMemberEnteredFrame(unsigned frame) override { m_lastMemberEnteredFrame = frame; }
	unsigned lastMemberEnteredFrame() const override { return m_lastMemberEnteredFrame; }
	void setGarrisoned(bool on) override;
	bool isGarrisoned() const override { return m_garrisoned; }
	void enterContainer(Object *container, int source) override;
	void pushMembersIntoContainer(Object *container) override;
	void exitContainer(Object *container, int source) override;
	void releaseMembersToward(Object *container, int source) override;
	std::vector<ObjectID> membersEntering() const override { return std::vector<ObjectID>(m_garrisonEntering.begin(), m_garrisonEntering.end()); }
	// lane GARRISON-3 (HordeGarrisonLink.cpp)
	void recentreOnMembers(Object *about) override;
	void returnToFormation(bool regroup) override;
	// lane GARRISON-3 counters (reported by the garrison tests)
	struct ExitStats
	{
		unsigned long long membersRejoined = 0;     // members that kept their slot and rejoined without being placed (RW 0x872D0A)
		unsigned long long returnsToFormation = 0;  // slot 0x10 calls that took a member back
		unsigned long long stuckDisabledMembers = 0; // RW 0x8759FF's 100-step guard ran out on a member disabled with type 4
		unsigned long long underConstructionUnported = 0; // the UNDER_CONSTRUCTION branch of RW 0x875AAF (the leash and the walk back, S-1620)
		unsigned long long slotsFreedOnTheWay = 0;  // a member on the way died or vanished: its kept slot is freed (S-1620 inference)
	};
	const ExitStats &exitStats() const { return m_exitStats; }
	// lane MODULES-3 (HordeEmotion.cpp)
	void setMembersBusy() override;
	void recordBackUp(Object *scarer) override;
	void setCowering(bool on) override;
	bool isCowering() const override { return m_cowering; }
	void markDirty() override { m_dirty = true; }
	void beginQuarrel(float minDistance, float maxDistance, const ConditionFlags &spectators, const ConditionFlags &fighters) override;
	void endQuarrel() override;
	void setFacePoint(const Coord3D &p) override;
	void clearFacePoint() override { m_hasFacePoint = false; }
	bool attackedWithin(unsigned frames, ObjectID &attacker) const override;
	void prepareMembersForCommand(Object *target) override; // lane MOVE-2 (HordeMemberPass.cpp)
	// lane ARCHER-1 (HordeMemberPass.cpp): the HordeAttackNugget's fire and the member choices it uses
	void attackTargetNow(Object *victim, bool closestMemberOnly) override;
	void attackPositionNow(const Coord3D &pos) override;
	// contain slot 0x48 (RW 0x86FA87): the member of this horde an attacker at `pos` aims at (see HordeMemberPass.cpp); `skipTagged`: members with the status
	// 0x3F are passed over; `attacker`: its current weapon filters the candidates (RW 0x744AAA)
	Object *pickMemberNear(bool skipTagged, const Coord3D &pos, float range, Object *attacker);
	// contain slot 0x110 (RW 0x87055D): the first member of the contain list, else the first member on its way into a garrison, else null
	Object *firstMember() const;
	// RW 0x870C29: the member (the contain list, then the members on the way) nearest to `victim` in the plane, within 1000 (1e6 squared, RW 0xBDCDC0)
	Object *closestMemberTo(const Object &victim) const;
	struct AttackStats
	{
		unsigned long long fires = 0;            // attackTargetNow calls that ran (the MELEE_HORDE guard RW 0x86C6EB passed)
		unsigned long long orders = 0;           // members ordered to attack (RW 0x66C536)
		unsigned long long outOfReach = 0;       // members whose pick was out of reach (RW 0x6FF7FA false): the horde is marked dirty
		unsigned long long meleeMembers = 0;     // members holding a melee weapon (the branch RW 0x875320)
		unsigned long long losAssumedClear = 0;  // RW 0x744AAA's line-of-sight filter (RW 0x6616AC) consulted: taken as clear (S-859)
		unsigned long long otherContainer = 0;   // victims held by a container that is not a HordeContain (its slot 0x48 is not ported: the victim stays)
		unsigned long long positionOrders = 0;   // members a position fire would order (RW 0x6961F1 aiAttackPosition is not executed, S-325)
	};
	const AttackStats &attackStats() const { return m_attackStats; }
	static const char *attackStopLine(); // S-2610
	// the emotion stop lines (S-1028)
	static std::vector<std::string> emotionStops();
	struct BackUpEntry
	{
		Coord3D position{ 0.0f, 0.0f, 0.0f }; ///< node + 0x14
		unsigned delay = 0;                   ///< node + 0x20 (frames the member waits before it backs up)
	};
	const std::map<ObjectID, BackUpEntry> &backUpRecords() const { return m_backUp; }
	ObjectID quarrelFighter(int i) const { return i == 0 ? m_quarrelA : m_quarrelB; }
	const std::map<ObjectID, int> &quarrelDistances() const { return m_quarrelDistance; }
	bool hasFacePoint() const { return m_hasFacePoint; }
	// slot 0x260 (RW 0x86C40D, lane INTEG-1): the melee behaviour is rebuilt from `data` (a stance's MeleeBehavior), else from the module data's MeleeBehavior
	// (+0x260), else Swarm; meleeBehaviorData() is what the horde AI runs (null: Swarm)
	// the horde AI's melee runtime is rebuilt for the new behaviour (HordeAIUpdate::resetMeleeRuntime), also when the data is the same kind
	void setMeleeBehavior(std::shared_ptr<MeleeBehaviorModuleData> data);
	const MeleeBehaviorModuleData *meleeBehaviorData() const;
	const std::map<unsigned short, float> &experiencePools() const { return m_experiencePools; }

	// ---- the member pass (MOVE-1, GameLogic/Object/Contain/HordeMemberPass.cpp) ----
	struct PassStats
	{
		unsigned long long passes = 0;       // member passes run
		unsigned long long orders = 0;       // member orders evaluated
		unsigned long long snaps = 0;        // near arm: the member snapped onto its slot
		unsigned long long walks = 0;        // far arm: the member was sent walking
		unsigned long long turns = 0;        // near arm: the member turned in place
		unsigned long long waits = 0;        // UseSlowHordeMovement: the member stopped to let the formation catch up
		unsigned long long leashCommands = 0; // the leash command (0, 2): lane AI-2 identified it as AI command 0x31 (busy) from the AI, RW 0x852E2A
		unsigned long long busyOrders = 0;    // lane AI-2: members the move hub made busy (RW 0x87471B)
		unsigned long long attackHolds = 0;   // lane MOVE-2: members the order left to their active state (RW 0x877B69)
		unsigned long long handoffBusy = 0;   // lane MOVE-2: members a horde command made busy (slot 0x14, RW 0x87594C)
	};
	const PassStats &passStats() const { return m_stats; }
	bool dirty() const { return m_dirty; }
	// diagnostics (tests, the viewer's report): the largest planar distance of a slotted member from its slot in the world; 0 without members
	float worstMemberSlotError() const;
	// the stop line S-224 (raised through AIWorld::noteStop by every horde of a game with an AIWorld)
	static const char *movementStop();
	// lane HORDE-2: the stop line of the re-forming after a melee (S-584)
	static const char *reformStopLine();
	// the member pass of one update (RW 0x875DC3-class slot, B1 HordeMemberOrder00245420 per member); true when members still have work
	bool runMemberPass();
	// the model condition mirror of TransportContain::update (BFME: the owner's MOVING becomes the passengers' TRANSPORT_MOVING)
	void mirrorMovingCondition();
	bool movementEnabled() const { return m_movementEnabled; }

	// lane PROD-1: a member made by a factory joins the horde (RW 0x873F30 assigns its slot) WITHOUT being put on that slot: it stays where
	// production placed it (the factory's exit) and is not moved by containReactToTransformChange; moving it to its formation position is the
	// member pass of the horde runtime (MOVE-1 / HORDE-2, stop S-149). false when it already is a member.
	bool acceptCreatedMember(Object *member) override;
	// the world position of the member's slot for the horde's current transform (RW 0x877D89); the horde's own position when it has no slot
	Coord3D getMemberFormationPosition(const Object *member) const override;
	void reserveMemberGoals() override;
	std::string getPayloadMemberTemplateName() const override;
	int getSlotCapacity() const override;
	// horde interface slot 0x264 (RW 0x86EA09, lane MOD-4: AISpecialPowerUpdate's health ratio): each contained member's body slot 0x14 (health / max
	// health, RW 0x8C1D75), then each member on its way into a garrison (the map at interface + 0x54, by id), added in the FPU and stored as a float each time;
	// divided (SSE) by slot 0x17C (the Slots, RW 0x86C8DE) taken unsigned when it is above 0
	float averageMemberHealthRatio() const;

	const HordeContainCore &core() const { return *m_core; }
	// lane COMBAT-1: the typed data (RanksToReleaseWhenAttacking, MeleeBehavior, ...) and the melee engagement. While engaged the formation is frozen where the men stand: the member
	// pass does not run (RW 0x86D75E beginMelee / 0x86C0F9 endMelee; horde spec 2.1 step 3), the horde AI's melee behaviour moves the members instead
	const HordeContainModuleData &hordeData() const;
	bool meleeEngaged() const { return m_meleeEngaged; }
	void setMeleeEngaged(bool on);
	// the free slot indices in free-list order (HordeContainCore::freeList)
	std::vector<int> freeSlotIndices() const;
	bool payloadCreated() const { return m_payloadCreated; }
	unsigned long long membersRefusedAfterDelete() const { return m_membersRefusedAfterDelete; }
	// the data's RandomOffset is non-zero (the slots were jittered with the logic RNG)
	bool hasRandomOffset() const;
	// the payload members that found no free slot of a matching rank
	unsigned unplacedMembers() const;

	// ---- lane HORDE-2: flanking (HordeContain/HordeFlank.cpp; RW update 0x872F05-0x872F5D, isFlankedBy RW 0x876FC4 = HordeContainInterface slot 0x24C) ----
	// the update's orientation history: FlankedDelay entries (a vector filled to FlankedDelay, then a ring at index H+0x2D8)
	void recordFlankHistory();
	// RW 0x876FC4: this horde is flanked by the horde `attacker` (see HordeFlank.cpp)
	bool isFlankedByHorde(Object &attacker);
	// RW slot 0x254 (RW 0x872B09): the byte H+0x2E8 (1 from the constructor, 0 once this horde was flanked)
	bool canFlank() const { return m_canFlank; }
	// ---- lane HORDE-2: the formation swap (HordeFormation.cpp; MSG_HORDE_TOGGLE_FORMATION RW 0x77BE83) ----
	// RW 0x8700AC (HordeContainInterface slot 0x5C): the horde has members and an AlternateFormation template whose HordeContain is the main formation or accepts
	// the member count (MinimumHordeSize)
	bool canToggleFormation() const;
	// RW 0x86C7F6 / 0x875FB2 (slots 0x60 / 0x68): the horde object keeps its id, AI, selection and transform and takes the AlternateFormation template's HordeContain;
	// the members (and the banner carrier) move into it and walk to their new slots. Returns the new contain (this module is gone afterwards), null when nothing happened
	HordeContain *toggleFormation();
	// lane COMBAT-3: the formation's AttributeModifiers (module data + 0x22C): interface slot 0x1E4 (RW 0x86C92D) gives every list to the members and the horde's
	// own pool (slot 0x1D8 with duration -1: the list's own), slot 0x1E8 (RW 0x86C962) takes them back (slot 0x1DC). The porcupine formations' CRUSHED_DECELERATE
	// 1000% reaches the pikemen this way.
	void applyFormationModifiers();
	void removeFormationModifiers();
	// the stop line S-589
	static const char *formationStopLine();
	// ---- lane HORDE-2: the banner carrier (HordeBanner.cpp; RW 0x8719E4 / 0x870204) ----
	// RW 0x8719E4: the countdown H+0x27C runs down; with no carrier, BannerCarrierMinLevel below the horde object's ExperienceTracker rank, the horde not firing in the last
	// 4 * LOGICFRAMES_PER_SECOND frames (unless `force`) a carrier of BannerCarriersAllowed[0] is made and joins the horde
	void bannerCheck(bool force);
	ObjectID bannerCarrier() const { return m_bannerCarrier; }
	unsigned bannerCountdown() const { return m_bannerCountdown; }
	// RW HordeContainInterface slot 0x18C (RW 0x873AE3): a new member of the payload template joins at `pos` (null when the horde is full or has no single payload template)
	Object *spawnReplacementMember(const Coord3D &pos);
	// RW slot 0x90 (RW 0x86EE52, inference S-587): a member was damaged or is attacking within the last `frames` frames
	bool wasInCombatWithin(unsigned frames) const;
	const std::vector<float> &flankHistory() const { return m_flankAngles; }

	friend struct Horde2HashAccess; // the HORDE-2 hash mutation tests (test_horde2_hash.cpp)

private:
	void createPayload();
	void placeMember(Object *member);
	// the removal of removeFromContain; `keepSlot`: the release of slot 0xA8 (RW 0x86EBF4), the member keeps its slot (lane GARRISON-3)
	void removeMember(Object *obj, bool keepSlot);
	// lane GARRISON-3: a member on the way that is gone or dead gives its kept slot back (S-1620 inference)
	void forgetMemberOnTheWay(ObjectID id);
	// the member pass pieces (HordeMemberPass.cpp)
	void memberOrder(Object &member, const Coord3D &slotPos, float orientation, bool force);
	void moveHub(Object &member, const Coord3D &dest, float orientation);
	void reassignMembersToNearestSlots();
	bool slotCellClear(Object &member) const;
	bool adjustMemberDestination(Object &member, Coord3D &dest) const;
	bool isMemberAheadOfSlot(const Object &member, const Coord3D &slotPos) const;
	// lane MODULES-3: the emotion branches of the member pass (RW 0x873FE8 .. 0x874671): 0 order the member at `pos` / `angle`, 1 skip it, 2 the quarrel ended (the
	// pass stops)
	int emotionMemberOrder(Object &member, Object *scarer, Coord3D &pos, float &angle);

	const HordeContainBehaviorData *m_data;
	std::unique_ptr<HordeContainCore> m_core;
	friend struct XpTestAccess; // lane XP-1 tests: isolated state hash mutations
	ContainedItemsList m_members;
	std::map<unsigned short, float> m_experiencePools; ///< XP-1: RW interface + 0x13C, keyed by the member template's id (template + 0x5E8)
	std::vector<ObjectID> m_producedMembers; ///< members that joined through acceptCreatedMember: never repositioned by this runtime
	bool m_payloadCreated = false;
	unsigned long long m_membersRefusedAfterDelete = 0; ///< lane AI-2 r4: produced members offered after the horde was destroyed (destroyed with it)
	// MOVE-1 state (RW HordeContain: +0xD8 dirty, +0xE8 work done, +0x1AC reforming, TransportContain +0xCC propagated condition)
	bool m_movementEnabled = false; // the game has an AIWorld: the contain acts (update runs every frame); false: the LOGIC-1 static horde
	bool m_dirty = true;
	bool m_workDone = false;
	bool m_meleeEngaged = false;
	bool m_deleting = false;
	bool m_locomoting = false;
	bool m_propagatedMoving = false;
	unsigned m_formationRefreshFrame = 0;
	PassStats m_stats;
	// HORDE-2 flank state: RW H+0x2CC vector<float> of orientations, H+0x2D8 ring index, H+0x2DC map<ObjectID, expiry frame>, H+0x2E8 can-flank byte
	std::vector<float> m_flankAngles;
	unsigned m_flankIndex = 0;
	std::map<ObjectID, unsigned> m_flankers;
	bool m_canFlank = true;
	// HORDE-2 banner state: RW H+0x26C carrier id, H+0x27C countdown
	ObjectID m_bannerCarrier = 0;
	unsigned m_bannerCountdown = 0;
	// lane GARRISON-1: RW interface + 0x54 (the members on their way into a garrison, a map keyed by id), + 0x1EC (the frame the last of them entered), + 0x188 (garrisoned)
	std::set<ObjectID> m_garrisonEntering;
	unsigned m_lastMemberEnteredFrame = 0;
	bool m_garrisoned = false;
	ExitStats m_exitStats; // lane GARRISON-3 (counters, not state)
	AttackStats m_attackStats; // lane ARCHER-1 (counters, not state)
	std::shared_ptr<MeleeBehaviorModuleData> m_stanceMeleeBehavior; // lane INTEG-1: the stance's MeleeBehavior given through slot 0x260 (null: the module data's)
	bool bannerMemberSlot(const Object &member, Coord3D &pos) const;
	// lane MODULES-3: the emotion state (RW HordeContain offsets)
	std::map<ObjectID, BackUpEntry> m_backUp;          ///< + 0x1A0 (size + 0x1A4): the members backing away from a scarer
	ObjectID m_scarer = INVALID_ID;                    ///< + 0x2B0
	bool m_cowering = false;                           ///< + 0x2AC
	Coord3D m_facePoint{ 0.0f, 0.0f, 0.0f };           ///< + 0x2B8
	bool m_hasFacePoint = false;                       ///< + 0x2C4
	ObjectID m_quarrelA = INVALID_ID;                  ///< + 0x1AC (the member drawn at random)
	ObjectID m_quarrelB = INVALID_ID;                  ///< + 0x1B0 (the member nearest the centre)
	ConditionFlags m_quarrelFighterFlags{};            ///< + 0x1B4
	ConditionFlags m_quarrelSpectatorFlags{};          ///< + 0x200
	std::map<ObjectID, int> m_quarrelDistance;         ///< + 0x24C (size + 0x250: the quarrel runs while it is not empty)
};

class GameLogicDispatch;
namespace HordeCommands
{
// lane HORDE-2: MSG_HORDE_TOGGLE_FORMATION (the dispatcher case RW 0x77BE83)
void registerHandlers(GameLogicDispatch &d);
}
