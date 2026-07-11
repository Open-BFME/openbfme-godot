# Asset lanes — legal doctrine

## Lane A — BFME2 install (`F:\BFME2`)

- **OK:** private extraction, private playtests, design measurement
- **NOT OK:** committing to public git, shipping in exports, sharing zips that include these files
- **Location:** `../_bfme2_extract/` only (gitignored)

## Lane B — Original / generated

- Grok Imagine 2D
- Meshy / Blender / Blockbench 3D
- Assets already created for `middle-earth-rts` (AI/procedural)
- **Default runtime path**

## Lane C — Third-party licensed

- Must have entry in `THIRD_PARTY.md` with license + URL
- CC0 preferred for simplicity

## Export gate

Before any shareable build:

```text
Scan export folder for known BFME2 hashes / paths / "EA Games" metadata.
Fail build if Lane A material present.
```
