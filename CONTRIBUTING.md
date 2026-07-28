# Contributing to OpenBFME

OpenBFME welcomes careful contributions from game developers, engine programmers,
modders, technical artists, reverse engineers, testers, and documentation writers.
The project is still preparing its first public-quality release, so small
well-proven changes are much easier to review than broad rewrites.

## Before you start

Read:

1. [README.md](README.md)
2. [DIRECTION.md](DIRECTION.md)
3. [STATUS.md](STATUS.md)
4. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
5. the guide for the subsystem you want to change

Check existing issues before beginning substantial work. If no issue describes
the change, open one with the observable problem, source of truth, proposed
scope, and smallest useful acceptance check.

## The non-negotiable content boundary

Never commit or attach:

- BFME or BFME2 archives and source files;
- extracted or decoded retail content;
- converted retail models, textures, maps, audio, fonts, UI, or particles;
- private runtime packs or pack manifests that expose contained data;
- original-game screenshots used as private comparison evidence;
- personal configuration, absolute private paths, credentials, or secrets; or
- caches and external tool installations from `.private`.

Public fixtures must be project-authored and clearly identified as synthetic.
Retail filenames and hashes may appear only where needed as bounded non-payload
provenance or detection metadata.

If you suspect retail content or a credential has entered a commit, stop and
report it privately. Do not “fix” the latest commit while leaving the material in
Git history.

## Good contribution shape

A strong pull request has:

- one observable outcome;
- a named source or behavioral oracle;
- narrow files and explicit non-goals;
- the smallest focused command that can disprove the change;
- no silent fallback or invented compatibility behavior;
- no unrelated formatting or refactoring; and
- an honest list of unresolved limitations.

Avoid combining importer, runtime, simulation, documentation, and performance
work unless the change genuinely requires that boundary and has tests for it.

## Verification

Run the smallest relevant check first. Examples include:

```bat
run_importer_tests.bat
run_retail_slice.bat --test
```

Not every change needs every command. A contributor should report the exact
commands run and their results. A warning, error, leaked resource, escaped path,
or silent required-content fallback is a failure even if the process returns
success.

Final milestone gates depend on private identity-bound evidence and are run by
the integration owner. A contributor should not publish or select a canonical
private pack merely to test a code change.

## Code and design principles

- Rise of the Witch-king 2.01 is the compatibility target; BFME2 1.06 is the
  base game underneath it.
- The importer alone understands retail source formats.
- Runtime code consumes versioned OpenBFME data, not the BFME installation.
- Authoritative simulation must remain independent of Godot rendering and local
  UI state.
- Deterministic collections, identifiers, numeric rules, and random streams are
  deliberate contracts.
- Missing parity evidence fails closed.
- Optimize only against a repeatable measurement.
- Preserve user changes and never use destructive Git commands in contribution
  instructions.

## Documentation

Keep newcomer documentation short and link to the owning technical contract.
Do not duplicate current hashes, counts, benchmarks, or blockers outside
`STATUS.md`. Mark incomplete features clearly and distinguish implemented code
from approved behavior.

## Pull request description

Include:

- problem and user-visible outcome;
- source/oracle used;
- files and non-goals;
- tests run and exact results;
- retail-content and secret-scan result;
- screenshots only when they contain no retail-derived material; and
- known risks or follow-up work.

By contributing, you confirm that you have the right to submit the code or
project-authored content under the repository's license.
