# RW 0x92FBAB — Palantir cost modifier section (Port 54)

Read with `tools/mod/rw.py dis` against RotWK 2.01 image (S-001). The BFME2
reconstruction `PalantirCostModifierUpgrade.cpp` and `Rva00528C65Getter.cpp`
suggested the callers; the addresses below are RotWK instructions.

* The CostModifierUpgrade instance factory RW 0x65009F calls constructor
  0x8B9B93; 0x8B9BAA installs vtable 0xC6EE3C. Its +0x10 slot is 0x8B9B44,
  which interns the CostModifierUpgrade name. Object::findModule 0x68BDA5
  walks modules and invokes this slot at 0x68BDB3 to match that key. The
  data factory 0x6500D4 calls 0x8B9D79; 0x8B9DB7 identifies the string,
  0x8B9E0D–0x8B9E14 initializes it to `>UNSPECIFIED<`. Field table
  0xC6EFA8 uses parser 0x42EE5E at data +0x14C. The destructor uses the
  same field at 0x8B9E70. The getter 0x92F6F2 reads module +4 (data),
  copies data +0x14C, and is called directly at 0x92FC84: not a vslot.
* The live Palantir update 0x93089B calls 0x92FBAB at 0x930970 only for a
  non-null context object, after updating portrait, buttons and rank; the
  context reset hides the cost section at 0x92F9AF–0x92F9B2.
* RW 0x92FBC0 tests template KindOf bit 7 (STRUCTURE). If absent, hide an
  already-shown section. Otherwise 0x92FC16 finds the first named module,
  0x92FC20 finds its controlling player, and 0x92FC41 tests the local-player
  visibility gate 0x6A8803. Failure of any hides an already-shown section at
  0x92FD5B–0x92FD62. 0x6A8803 rejects a missing local player, permits all
  objects for an observer/defeated local player (0x6AAC52), and otherwise
  requires relationship 2 to the object's team. It does **not** test whether
  the selected module's upgrade has executed.
* On success 0x92FC53–0x92FC6F invokes ShowCostModifierUpgradeInterface once
  and invalidates the cache. 0x92FC72 reads UpgradeDiscount, 0x92FC84 reads
  the label, and 0x92FC95 calls the amount getter. When cached, 0x92FCA2–
  0x92FCBE invalidates on a changed flag, a nonzero prior field when the flag
  is false, or a changed amount. There is no label comparison or missing-label
  check in this branch. At 0x92FCCD–0x92FD29, fetch the label from GameText,
  format it with the integer amount, then set `APT:CostModifierUpgrade`.
* Amount getter 0x92F712: UpgradeDiscount true -> 0x6AE7CB with empty name,
  summing all current upgrade discounts, then 0x92F73A–0x92F759 computes
  `floor(sum * 100 + 0.5)` followed by x87 float conversion. Otherwise
  0x92F761–0x92F76A passes the module's filter handle to 0x6AD967.
  0x6AD967 scans the player's cost entries in order, skipping a missing source
  and source with model condition 0x44; at 0x6AD9A9–0x6AD9B0 the filter ID
  must equal the module's. For each match, take its percentage at the running
  match index if present, retaining the previous value otherwise, then increment
  the index. 0x6AD9DA–0x6AD9FC returns `floor(0.5 - value * 100)` via x87.
  Entries point at the module filter handle (0x6AD85C, 0x6ACE96); filters
  are interned by 0x76341A and equality 0x762AEE. The port keeps filters by
  value, so its comparison must reproduce this identity for parsed filters.
* Activation 0x8B9CD9 adds either an upgrade-discount entry (0x6B2C67) or a
  cost-modifier entry (0x6AD845); neither copies the label. The UI reads the
  selected module's data, not a player's modifier record. An unexecuted
  module still passes the UI's module-presence gate; its amount follows the
  player records. A missing module fails the gate.

## Shared formatter boundary (Port 54, issue #164)

RW `0x92FCE1` calls `0xADF7E0` with the fetched Unicode format and one
integer; `0xADF7F7` calls `0x437120`, which invokes the import at `0xBD05BC`
(`msvcr71.dll` `vswprintf`) at `0x437147`, with a buffer of 0x2000 wide
characters. The existing `LocalizedText::format` is a shared, deliberately
restricted typed formatter: for example it leaves `%+d` literal, whereas the
MSVCRT formatter applies the sign flag to a signed integer. The synthetic
tests use `%d%%`, which is supported; they do not establish general format
parity. The owner states that all 33 Ennorath strings named by this field use
only `%d` and `%%`, and decided to keep the shared formatter unchanged.
`AptPalantir.cpp` uses it; no formatter patch or local workaround was made.
The general-format difference remains recorded in S-760.
