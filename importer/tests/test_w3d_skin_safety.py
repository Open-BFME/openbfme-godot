from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import struct
import unittest

from openbfme_importer.w3d_skin_safety import (
    HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
    SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
    SKIN_SAFETY_PROOF_REJECTED,
    W3D_SKIN_SAFETY_SCHEMA,
    W3D_SKIN_SAFETY_VERSION,
    W3DSkinSafetyError,
    prove_w3d_skin_safety,
    validate_w3d_skin_safety_proof,
)


CONTAINER = 0x80000000
MESH = 0x00000000
INFLUENCES = 0x0000000E
MESH_HEADER = 0x0000001F
HIERARCHY = 0x00000100
HIERARCHY_HEADER = 0x00000101
PIVOTS = 0x00000102
PIVOT_FIXUPS = 0x00000103


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\x00" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    size = len(payload) | (CONTAINER if container else 0)
    return struct.pack("<II", kind, size) + payload


def _pivot(name: str, parent: int) -> bytes:
    return struct.pack(
        "<16si10f",
        _fixed(name, 16),
        parent,
        *([0.0] * 6),
        0.0,
        0.0,
        0.0,
        1.0,
    )


def _hierarchy(*, fixups: bool = False) -> bytes:
    pivots = b"".join(
        (
            _pivot("AUTHORED_ROOT", -1),
            _pivot("AUTHORED_ONE", 0),
            _pivot("AUTHORED_TWO", 0),
        )
    )
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed("AUTHORED_SKL", 16),
        3,
        0.0,
        0.0,
        0.0,
    )
    children = _chunk(HIERARCHY_HEADER, header) + _chunk(PIVOTS, pivots)
    if fixups:
        children += _chunk(PIVOT_FIXUPS, struct.pack("<36f", *([0.0] * 36)))
    return _chunk(HIERARCHY, children, container=True)


def _mesh_header(vertex_count: int, *, skin: bool = True) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x00020000 if skin else 0,
        _fixed("AUTHORED_MESH", 16),
        _fixed("AUTHORED_MODEL", 16),
        0,
        vertex_count,
        0,
        0,
        0,
        0,
        0,
        0x13 if skin else 0x03,
        1,
        *([0.0] * 10),
    )


def _model(
    influences: tuple[tuple[int, int, int, int], ...] = ((1, 2, 60, 40),),
    *,
    skin: bool = True,
    include_influences: bool = True,
    duplicate_influences: bool = False,
    header_vertex_count: int | None = None,
) -> bytes:
    vertex_count = (
        len(influences) if header_vertex_count is None else header_vertex_count
    )
    children = [_chunk(MESH_HEADER, _mesh_header(vertex_count, skin=skin))]
    if include_influences:
        payload = b"".join(struct.pack("<4H", *item) for item in influences)
        children.append(_chunk(INFLUENCES, payload))
        if duplicate_influences:
            children.append(_chunk(INFLUENCES, payload))
    return _chunk(MESH, b"".join(children), container=True)


class W3DSkinSafetyTests(unittest.TestCase):
    def test_valid_nonroot_influences_are_deterministic_and_name_free(self) -> None:
        model = _model(((1, 2, 60, 40), (2, 1, 100, 0)))
        hierarchy = _hierarchy()

        first = prove_w3d_skin_safety(model, hierarchy)
        second = prove_w3d_skin_safety(model, hierarchy)

        self.assertEqual(first, second)
        self.assertEqual(first.schema, W3D_SKIN_SAFETY_SCHEMA)
        self.assertEqual(first.schema_version, W3D_SKIN_SAFETY_VERSION)
        self.assertEqual(first.skin_mesh_count, 1)
        self.assertEqual(first.influence_record_count, 2)
        self.assertEqual(first.hierarchy_pivot_count, 3)
        self.assertEqual(
            first.proof_sha256,
            hashlib.sha256(
                json.dumps(
                    first.proof_hash_basis(),
                    ensure_ascii=True,
                    separators=(",", ":"),
                    sort_keys=True,
                ).encode("ascii")
            ).hexdigest(),
        )
        evidence = json.dumps(first.neutral(), sort_keys=True)
        for forbidden in ("AUTHORED", "MESH", "MODEL", "SKL"):
            self.assertNotIn(forbidden, evidence)
        with self.assertRaises(FrozenInstanceError):
            first.skin_mesh_count = 9  # type: ignore[misc]

    def test_active_primary_or_secondary_root_is_rejected(self) -> None:
        cases = (
            (((0, 2, 60, 40),), 1, 0),
            (((1, 0, 60, 40),), 0, 1),
            (((0, 0, 60, 40),), 1, 1),
        )
        for influences, primary_count, secondary_count in cases:
            with self.subTest(influences=influences):
                with self.assertRaises(W3DSkinSafetyError) as raised:
                    prove_w3d_skin_safety(_model(influences), _hierarchy())
                self.assertEqual(
                    raised.exception.reason_code,
                    SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
                )
                self.assertEqual(raised.exception.owner, "model")
                self.assertEqual(
                    raised.exception.active_primary_root_count,
                    primary_count,
                )
                self.assertEqual(
                    raised.exception.active_secondary_root_count,
                    secondary_count,
                )

    def test_zero_weight_root_fields_are_allowed(self) -> None:
        for influences in (((0, 2, 0, 100),), ((1, 0, 100, 0),)):
            with self.subTest(influences=influences):
                proof = prove_w3d_skin_safety(_model(influences), _hierarchy())
                self.assertEqual(proof.active_primary_root_influence_count, 0)
                self.assertEqual(proof.active_secondary_root_influence_count, 0)

    def test_validator_rejects_a_resealed_noncanonical_schema(self) -> None:
        proof = prove_w3d_skin_safety(_model(), _hierarchy())
        object.__setattr__(proof, "schema", "openbfme.substituted-proof")
        object.__setattr__(
            proof,
            "proof_sha256",
            hashlib.sha256(
                json.dumps(
                    proof.proof_hash_basis(),
                    ensure_ascii=True,
                    separators=(",", ":"),
                    sort_keys=True,
                ).encode("ascii")
            ).hexdigest(),
        )

        with self.assertRaises(W3DSkinSafetyError) as raised:
            validate_w3d_skin_safety_proof(proof)
        self.assertEqual(raised.exception.reason_code, SKIN_SAFETY_PROOF_REJECTED)

    def test_any_pivot_fixup_is_rejected_on_the_hierarchy_owner(self) -> None:
        with self.assertRaises(W3DSkinSafetyError) as raised:
            prove_w3d_skin_safety(_model(), _hierarchy(fixups=True))
        self.assertEqual(
            raised.exception.reason_code,
            HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
        )
        self.assertEqual(raised.exception.owner, "hierarchy")
        self.assertEqual(raised.exception.pivot_fixup_chunk_count, 1)

    def test_malformed_or_unbound_skin_contracts_fail_closed(self) -> None:
        cases = (
            (_model(include_influences=False), _hierarchy()),
            (_model(duplicate_influences=True), _hierarchy()),
            (_model(header_vertex_count=2), _hierarchy()),
            (_model(((1, 9, 60, 40),)), _hierarchy()),
            (_model(((1, 2, 50, 40),)), _hierarchy()),
            (_model(), None),
            (b"not a chunk stream", None),
        )
        for model, hierarchy in cases:
            with self.subTest(size=len(model), hierarchy=hierarchy is not None):
                with self.assertRaises(W3DSkinSafetyError) as raised:
                    prove_w3d_skin_safety(model, hierarchy)
                self.assertEqual(
                    raised.exception.reason_code,
                    SKIN_SAFETY_PROOF_REJECTED,
                )

    def test_nonskin_static_model_needs_no_hierarchy(self) -> None:
        proof = prove_w3d_skin_safety(
            _model((), skin=False, include_influences=False),
            None,
        )
        self.assertEqual(proof.skin_mesh_count, 0)
        self.assertEqual(proof.hierarchy_pivot_count, 0)
        self.assertIsNone(proof.hierarchy_sha256)


if __name__ == "__main__":
    unittest.main()
