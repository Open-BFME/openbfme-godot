# Veterancy pickups over the upstream crate runtime

Editions: shared BFME2/RotWK simulation; BFME1 parity remains unverified (S-5017).

This PR retains only VeterancyCrateCollide and the explicit crate context-click
adaptation. Salvage rewards, the CrateCollide base and version-2 snapshot,
AIWorld::processCrateContacts and Object collision caches remain upstream's.
Common module data is split into CrateCollideModuleData without changing salvage
field parsing, default values, reward RNG or snapshot layout.

## Evidence

RotWK 2.01 target (subject to S-001 provenance): registry create 0x650FFF calls
constructor 0x8C0F7B; data constructor 0x6558B7; field table 0xC05EB8; primary
vtable 0xC71554. Its slots 12/13 point to execute 0x8C10DB / validity 0x8C1039.
The matching BFME2 source is Open-BFME-2 VeterancyCrateCollide.cpp, execute
RVA 0x4BCFF0 / validity RVA 0x4BCEF0, and the module-data constructor.

The target validity checks the common crate rules, death/height, positive level
award, trainability, accepting XP and AffectsUpToLevel. getLevelsToGain at RW
0x8C1025 (BFME2 RVA 0x4BCEDC) returns zero for AddsOwnerVeterancy, otherwise one:
refusing AddsOwnerVeterancy therefore implements the native zero-level gate.
The shared native contact pass excludes the HORDE collision record (RW 0x68E31F); a battalion pickup therefore awards the touching individual member, not an unconditional level to the horde.
Zero EffectRange promotes the collector; a nonzero range promotes other objects
of its controlling player. The target's IsPilot path additionally checks owner,
airborne locomotion and transfers the script name after promotion.

## Explicit limits

S-5901 records ground-contact approximation of significant height, missing rank
text/animation and pilot script-name transfer. Pilot crates are explicitly
refused and reported rather than executing an incomplete transfer. Standard
experience shields work through the existing shared crate contact pass.
PickupStuffUpdate's existing collide-interface guard recognizes the new runtime;
it continues to refuse genuinely unported crate classes. S-1422 describes the
remaining temporary-machine limits rather than calling Veterancy unported.

S-5902 reports the context-click priority adaptation in HudInput::stops.
Force-attack remains on its existing path. The input adaptation is not claimed
to reproduce RotWK evaluateContextCommand RW 0x81E60B.
