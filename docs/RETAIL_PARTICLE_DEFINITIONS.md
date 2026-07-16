# Retail particle-definition parser

`openbfme_importer.sage_particles` is the bounded lexical bridge for the two
BFME2 particle-definition families:

- `ParticleSystem`: usually a flat sequence of scalar assignments.
- `FXParticleSystem`: nested sections, including assignment-shaped section
  headers whose selector chooses the SAGE section implementation.

The parser does not interpret or convert parameters. It preserves authored
entry order, nested structure, scalar text after comment removal, and exact
source spans/hashes so later conversion work can be evidence-driven.

## API

```python
from openbfme_importer.sage_particles import (
    parse_particle_definitions,
    select_particle_definition,
)

definitions = parse_particle_definitions(source_bytes)
record = select_particle_definition(
    definitions,
    "RequestedParticleId",
    kind="ParticleSystem",
)
```

Parsing all definitions intentionally retains duplicate evidence. Selecting a
single named definition is case-insensitive and fails if it is missing or
ambiguous. Supplying `kind` disambiguates the legacy and FX families, but never
disambiguates duplicate records within one family.

`ParticleDefinition.entries` retains the authored stream of
`ParticleAssignment` and `ParticleBlock` records. `assignments(recursive=True)`
and `blocks(recursive=True)` provide deterministic depth-first traversal.

## Provenance and containment

Every definition and nested block records inclusive line numbers, half-open
byte offsets, byte length, and SHA-256 of the exact raw span. Assignments carry
the same provenance for their source line. Returned records never contain
source bytes or host filesystem paths.

Retail values are still retail data. Private proof reports may serialize only
schema, counts, hashes, and field/block-name summaries. They must not serialize
assignment values or source excerpts. Retail inputs and proof artifacts remain
under `.private`.

## Fail-closed limits

The parser rejects non-byte input, oversized documents/lines/values/counts,
NUL or control bytes, unsupported encoding, unsafe identifiers and field names,
empty assignments, malformed quotes, unknown top-level input, bad indentation,
unbalanced `End`, excessive nesting, and unterminated definitions. Comment
markers inside quoted values are preserved; comment markers outside quotes are
removed before lexical parsing.

The parser is intentionally not a general SAGE INI interpreter. Expanding its
grammar should be driven by a contained retail proof and legal-safe regression
fixture, not by permissive fallback behavior.
