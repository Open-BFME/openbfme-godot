"""The one definition of W3D chunks whose payload is a NUL-terminated string.

W3D chunk headers carry a 0x80000000 "has sub-chunks" bit.  For string leaves
that bit is meaningless and retail authors it inconsistently: RotWK
``art/w3d/gu/guarcher_skn.w3d`` (the GondorArcherHorde model) sets it on
VERTEX_MAPPER_ARGS0 while leaving it clear on the VERTEX_MAPPER_ARGS1 sibling
that holds the same shape of payload.  The pinned reader reads both as strings,
so a generic walk must not descend into them -- doing so reads the literal
"FPS=" as chunk id 0x3D535046 and aborts the parse with bogus oversized-chunk
and unknown-chunk diagnoses.

Every W3D chunk walker in this package consults :func:`is_w3d_string_leaf`
before honouring the flag, so the rule is stated exactly once.
"""

from __future__ import annotations


W3D_CHUNK_VERTEX_MATERIAL_NAME = 0x0000002C
W3D_CHUNK_VERTEX_MAPPER_ARGS0 = 0x0000002E
W3D_CHUNK_VERTEX_MAPPER_ARGS1 = 0x0000002F

W3D_STRING_LEAF_CHUNKS = frozenset(
    {
        W3D_CHUNK_VERTEX_MATERIAL_NAME,
        W3D_CHUNK_VERTEX_MAPPER_ARGS0,
        W3D_CHUNK_VERTEX_MAPPER_ARGS1,
    }
)


def is_w3d_string_leaf(chunk_id: int) -> bool:
    """Return True when ``chunk_id`` names a NUL-terminated string leaf.

    Callers must treat a True answer as overriding the header's sub-chunk bit:
    the payload is a string, never a chunk sequence.
    """

    return chunk_id in W3D_STRING_LEAF_CHUNKS
