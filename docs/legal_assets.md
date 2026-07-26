# Private retail workspace and later code-only export

> **Superseded:** `docs/RELEASE_POLICY.md` and `docs/THIRD_PARTY.md` own current
> containment, distribution, donor, and license policy.

This document is an operating containment policy, not a legal-status finding.
The current project is a private local compatibility build: complete extraction,
conversion, and use of the owner's retail install are in scope now. Legal/public
release review is deferred until the game works, at which point the deliverable
is rebuilt as code-only with every `.private` payload removed.

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

Lane B membership is a claim about origin, and artifact forensics can only *exclude*
retail derivation, never confirm which generator or which terms of service produced a
file. Every Lane B asset therefore needs a recorded owner attestation naming the tool and
its output licence before release. The outstanding ones are listed under *Open provenance
items* in `THIRD_PARTY.md`.

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
payload bytes and **retail-derived** decoded images/audio, GLBs and map binaries stay
only under the ignored `.private` tree and are stripped from any public/code-only
export. Every generated retail `pack.json` is marked `redistributable: false`.

> **Scope correction (resolves a contradiction with the tree).** An earlier revision of
> the paragraph above listed "GLBs" without qualification, which read as a blanket ban on
> committed GLB files. The repository has always tracked 27 Lane B GLBs under
> `game/data/base/assets/models`. The doc was wrong, not the tree: this containment rule
> governs **Lane A retail-derived** payloads only, and Lane B / Lane C material is
> committed by design under the repository-authored lane in `RELEASE_POLICY.md`. The
> file extension was never the test; the **origin lane** is. Per-asset provenance for
> everything tracked under `game/data/base/assets` — including those GLBs and the
> outstanding attestations they still need — lives in `THIRD_PARTY.md`.
