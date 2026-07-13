# Private retail workspace and later code-only export

## Lane A — BFME2 install (`F:\BFME2`)

- **Active private scope:** complete effective-view extraction, conversion, private
  playtests, design measurement, and user-owned caches created by the retail importer.
- **NOT OK:** committing retail or converted retail assets to public git, embedding
  them in the engine distribution, or sharing exports/zips that include them.
- **Location:** `.private\retail-work` for importer work/cache and
  `.private\content-packs` for immutable selected bundles. The entire `.private` tree
  is local-only and git-ignored. None belongs in a public engine export, source archive,
  shareable screenshot fixture, or commit.
- Public-release and branding review are deferred until the private compatibility build
  works; they do not block local extraction or conversion.

## Lane B — Original / generated

- Grok Imagine 2D
- Meshy / Blender / Blockbench 3D
- Assets already created for `middle-earth-rts` (AI/procedural)
- **Legal-safe/public fallback runtime path**

## Lane C — Third-party licensed

- Must have entry in `THIRD_PARTY.md` with license + URL
- CC0 preferred for simplicity

## Later code-only export gate

Before any shareable build:

```text
Scan export folder for known BFME2 hashes / paths / "EA Games" metadata.
Fail build if Lane A material present.
```

Also scan runtime assemblies for accidental OpenSAGE/donor dependencies and verify the
provenance manifest for every copied or modified open-source file.

Committed importer fixtures and tests use synthetic BIG archives only. Retail virtual
filenames and cryptographic hashes may be retained as provenance metadata. Private
payload bytes, decoded images/audio, GLBs, and map binaries stay only under the ignored
`.private` tree and are stripped from any public/code-only export. Every generated
retail `pack.json` is marked `redistributable: false`.
