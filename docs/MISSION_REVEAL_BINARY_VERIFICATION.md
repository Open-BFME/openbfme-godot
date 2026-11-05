# Named mission reveals: binary verification

Static inspection on 2026-11-04 for PR #222's repeated-name and changing-alliance
review findings. No runtime observation of the original game is claimed.
Disassembly and binary-derived working files remain outside Git.

## Input identity

- BFME2 1.06: MD5 `34391b253b0ee12d6cec1178f46d3bf9`, the clean binary
  specified by PLAN. SHA-256:
  `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640`.
- Available RotWK image: MD5 `daed3668006cd90f01c34e5a7da1901f`;
  SHA-256 `5481de75b63e22483660ea8097478faef594f90b9ade7de893e092256ba68a26`.
  This is the known community-modified image (S-001), **not a verified clean
  2.01 oracle**. A second local image also contains added community sections,
  including `.bflua`; it was not accepted as clean evidence.

## Method

Read the PE's memory-mapped sections and disassemble x86 instructions with
Capstone. Start from the BFME2 decompilation's function RVAs, inspect the actual
clean-binary instructions, then locate matching instruction sequences in the
available RotWK image. Check direct call targets to distinguish the otherwise
similar reveal and undo bodies. Compare with `tools/re/relib.py`'s normalized
features, and manually inspect the relevant operand differences and branches.

All addresses in the table below are **virtual addresses**, with image base
0x400000 already added. The decompilation's address-derived filenames use RVAs.

| Role | Clean BFME2 VA | Available RotWK VA | Body size |
|---|---|---|---:|
| Lookup/create CRC-keyed record | 0x758853 | 0x75A344 | 148 |
| Permanent waypoint wrapper | 0x7BC9E0 | 0x7BD4C1 | 50 |
| Permanent trigger wrapper | 0x7BCA12 | 0x7BD4F3 | 49 |
| Apply named reveal | 0x7577DA | 0x7592E1 | 195 |
| Apply named undo | 0x75789D | 0x7593A4 | 195 |
| Undo-then-remove wrapper | 0x7BCA43 | 0x7BD524 | 26 |
| Remove named record | 0x757D52 | 0x75985D | 52 |

All seven pairs have equal normalized instruction shapes. The three action
wrappers are equal under the existing relocation/branch-target normalization.
The other four are not relocation-normalized identical: in particular, the
record-vector offsets change from BFME2 +0x1A49C/+0x1A4A0 to RotWK
+0x1A59C/+0x1A5A0. They must not be described as byte-identical functions.
No direct reference into the known added sections was found in these seven
RotWK bodies. This is not a transitive patch audit and does not lift S-001.

## Finding 1: repeating a name still applies a reveal

The clean BFME2 lookup/create helper branches directly to its return path when
it finds the CRC. It neither overwrites that existing record nor removes a
previous look. Both permanent wrappers call the helper and then call the named
reveal function unconditionally, including on the existing-name path.

The undo action calls the named undo function once, then invokes the record
removal helper. The removal helper searches by CRC and erases the matching
entry; it does not keep a repetition counter or issue additional shroud undo
calls. The examined RotWK bodies have the same relevant control flow.

Therefore, making repeated named actions idempotent would change the verified
BFME2 call-level behavior. The existing looker-count implementation is pinned
by `save4d regional reveals: repeated names...`: two applications produce two
looks, one undo leaves one, and a second undo has no named record to find.
This is an OpenBFME regression test guided by static binary facts, not a claim
that original-game runtime looker counts were captured.

## Finding 2: undo resolves the selector again

Clean BFME2 reveal at call site 0x757817 and undo at call site 0x7578DA both
pass the record's player string (+0xC) to selector resolver 0x757475. In the
available RotWK image, the corresponding calls at 0x75931E and 0x7593E1 go to
0x758F7C. The record is 0x14 bytes: name CRC, target string, radius, player
string and kind. There is no captured mask in this record.

Thus, replacing selector evaluation with a persisted original mask would change
verified BFME2 behavior. `save4d regional reveals: undo resolves...` pins both
an ally removed before undo (its look remains) and an ally added before undo
(its current look is decremented). Those awkward consequences are deliberate
compatibility tests, not recommended enhanced-game rules.

## Scope and remaining evidence

This verifies the clean BFME2 control flow for both review findings and
corroborates it in the available modified RotWK image. It does not establish
clean RotWK parity, trigger-edge rasterization, every transitive shroud callee,
or original-game runtime results. S-6102 still covers regional wrapper/edge
limits, and S-001 still blocks a clean RotWK verification claim.

Next evidence: obtain a verified clean RotWK 2.01 binary, repeat this narrow
comparison, and optionally observe repeated reveal/undo and alliance changes
in an original-game test map. No behavior difference between the editions was
found in the inspected paths; none is invented on that basis.
