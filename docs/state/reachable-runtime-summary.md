> Promoted from `workspace/scratch/current-reachable-runtime-summary-20260815.md` on 2026-08-17 (stage-2a triage); verbatim.

# Current selected reachable-runtime ledger

Selection SHA-256: `e3cf65197b36fc855f852f18ce7e53a698fca34ff004d29e0b25d31f0326285c`
Ledger SHA-256: computed after write as `6331ca10effa3e52efee77f07f9a0ed75226aa82f7a67b9eeab9aaf693fcf28f`

This is a new authoritative root/edge ledger. The old 203/944 file is used only for broad-baseline identity disposition; it cannot support a corrected 116/738 delta.

## Reachability

- Explicit root descriptors: 507
- Reachable descriptors: 507
- Reachable recursive edges: 1313
- Canonical unresolved reachable edges: 278 (296 including shadowed descriptors)
- Map object rows / unresolved: 3282 / 3270
- Map type identities / unresolved: 83 / 81

Runtime gap counts below are canonical last-pack-wins rows; the JSON retains every shadowed descriptor row for audit.

## Current gaps

- Gameplay module rows not executable: 736
- Presentation module rows not executable: 280
- Ability rows not implemented/passive: 34
- Additional authored presentation gaps: 4246

## Highest gameplay module clusters

- rotwk / deferred / EmotionTrackerUpdate: 133
- rotwk / deferred / HordeMemberCollide: 62
- rotwk / deferred / CastleMemberBehavior: 50
- bfme2 / deferred / EmotionTrackerUpdate: 32
- rotwk / deferred / NotifyTargetsOfImminentProbableCrushingUpdate: 27
- bfme2 / deferred / AttributeModifierUpgrade: 21
- bfme2 / deferred / GeometryUpgrade: 19
- bfme2 / deferred / SquishCollide: 19
- rotwk / deferred / WallHubBehavior: 16
- rotwk / deferred / DualWeaponBehavior: 14
- rotwk / deferred / HordeNotifyTargetsOfImminentProbableCrushingUpdate: 14
- rotwk / deferred / MonitorConditionUpdate: 14
- rotwk / deferred / FoundationAIUpdate: 13
- rotwk / deferred / PorcupineFormationBodyModule: 13
- rotwk / deferred / DoCommandUpgrade: 12
- bfme2 / deferred / HordeMemberCollide: 11
- rotwk / deferred / SlaveWatcherBehavior: 11
- rotwk / deferred / QueueProductionExitUpdate: 11
- rotwk / deferred / RefundDie: 11
- bfme2 / deferred / RefundDie: 9

## Highest ability clusters

- rotwk / unimplemented / no convertible effect leaf is bound to this ability: 6
- bfme2 / unimplemented / SpecialPower SpecialAbilityKingsFavor has unsupported fields: flags: 3
- bfme2 / unimplemented / no convertible effect leaf is bound to this ability: 2
- rotwk / unimplemented / ability Command_KhamulDismount needs exactly one SET_MOUNTED LocomotorSet; found 0: 2
- bfme2 / unimplemented / ability Command_SpecialAbilityTerribleFury SPECIAL_SCREECH fear push is engine-hardcoded: the bound SpecialAbilityUpdate authors only the cast envelope (EffectRange 200), no convertible effect leaf — needs the screech emotion system: 1
- bfme2 / unimplemented / SpecialPower SuperweaponSpawnOathbreakers has unsupported fields: flags: 1
- bfme2 / unimplemented / disguise needs the disguise system: 1
- rotwk / unimplemented / ability Command_KarshBlink TeleportSpecialAbilityUpdate has no resolvable MaxDistance: 1
- rotwk / unimplemented / SpecialPower SpecialAbilityRogashLeap has unsupported Flags: PATHABLE_ONLY: 1
- rotwk / unimplemented / SpecialPower SpecialAbilityGimliLeap has unsupported Flags: PATHABLE_ONLY: 1
- rotwk / unimplemented / SpecialPower SpecialAbilityGloinSmash has unsupported Flags: PATHABLE_ONLY: 1
- rotwk / unimplemented / ability Command_ArwenFlood weapon ArwenPersonalFlood has no resolvable base DamageNugget damage and no authored warhead; its damage payload uses unsupported nugget kinds: FireLogicNugget (needs fire ignition/burn-rate logic), WeaponOCLNugget (needs weapon-spawned object (OCL) payloads): 1
- rotwk / unimplemented / ability Command_ToggleHaldirWeapon weapon HaldirBow has no resolvable Damage: 1
- rotwk / unimplemented / ability Command_SpecialAbilityElfCloakThranduil ToggleHiddenSpecialAbilityUpdate has no resolvable EffectDuration: 1
- rotwk / unimplemented / ability Command_ToggleTreebeardRockThrow weapon RohanEntRockThrow has no resolvable Damage: 1
- rotwk / unimplemented / ability Command_SpecialAbilitySharkuManEater weapon IsengardSharkuManEaterGrab has no resolvable base DamageNugget damage and no authored warhead; its damage payload uses unsupported nugget kinds: GrabNugget (needs grab/passenger carry), SpecialModelConditionNugget (needs scripted model-condition status): 1
- rotwk / unimplemented / ability Command_MouthOfSauronDissent -> ModuleTag_MouthOfSauronDissentTrigger modifier MouthOfSauronDissentModifier has no runtime-supported Modifier rows: 1
- rotwk / unimplemented / ability Command_WitchKingDismount needs exactly one SET_MOUNTED LocomotorSet; found 0: 1
- rotwk / unimplemented / ability Command_SpecialAbilityScreechWitchKing SPECIAL_SCREECH needs exactly one positive EffectRange: 1
- rotwk / unimplemented / ability Command_SpecialAbilityShadeChomp weapon AngmarShadeChompWeapon has no resolvable base DamageNugget damage and no authored warhead; its damage payload uses unsupported nugget kinds: GrabNugget (needs grab/passenger carry), SpecialModelConditionNugget (needs scripted model-condition status): 1

## Highest presentation clusters

- module: rotwk / deferred / ModelConditionUpgrade: 96
- module: rotwk / deferred / W3DHordeModelDraw: 70
- module: rotwk / deferred / AudioLoopUpgrade: 20
- module: bfme2 / deferred / W3DHordeModelDraw: 16
- module: bfme2 / deferred / ModelConditionUpgrade: 14
- module: rotwk / deferred / W3DTruckDraw: 14
- module: rotwk / deferred / TransitionDamageFX: 8
- module: rotwk / deferred / EvaAnnounceClientCreate: 8
- module: bfme2 / deferred / EvaAnnounceClientCreate: 7
- module: rotwk / deferred / TerrainResourceClientBehavior: 7
- module: rotwk / deferred / ClickReactionBehavior: 4
- module: bfme2 / deferred / AudioLoopUpgrade: 3
- module: bfme2 / deferred / W3DTruckDraw: 2
- module: rotwk / deferred / AnnounceBirthAndDeathBehavior: 2
- module: bfme2 / deferred / TerrainResourceClientBehavior: 1
- authored: rotwk / packaged-unimplemented / animation-state: 3431
- authored: bfme2 / packaged-unimplemented / animation-state: 732
- authored: rotwk / unimplemented / unsupported-visual-reference: 63
- authored: bfme2 / unimplemented / unsupported-visual-reference: 18
- authored: rotwk / excluded-zero-byte-placeholder / animation-state: 2

## Old broad-baseline disposition

- rotwk / still-deferred: 657
- bfme2 / still-deferred: 172
- rotwk / no-current-descriptor: 162
- rotwk / promoted-executable: 125
- bfme2 / no-current-descriptor: 19
- bfme2 / promoted-executable: 7
- bfme2 / reachable-source-or-schema-drift: 5
