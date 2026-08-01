from __future__ import annotations

from dataclasses import FrozenInstanceError, replace
import hashlib
import json
import struct
import unittest
from unittest.mock import patch

from openbfme_importer.w3d_decode_plan import (
    W3DDecodePlanError,
    _W3DExternalHierarchySourceObservations,
    _build_external_hierarchy_resolver,
    _collect_external_hierarchy_observations,
    _virtual_path_lookup_sha256,
    plan_w3d_stream_decode,
)
from openbfme_importer.w3d_metadata import scan_w3d_metadata
from importer.tests.test_w3d_emitter_streams import (
    _container_payload as _emitter_container_payload,
    _emitter_file,
    _payloads as _emitter_payloads,
)


def _fixed(value: str) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\0" * (16 - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, container: bool = False) -> bytes:
    size = len(payload) | (0x80000000 if container else 0)
    return struct.pack("<II", chunk_id, size) + payload


def _mesh_header(*, vertices: int, faces: int, name: str = "Mesh") -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040000,
        0,
        _fixed(name),
        _fixed("Owner"),
        faces,
        vertices,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        *([0.0] * 10),
    )


def _mesh(*children: bytes, vertices: int = 2, faces: int = 0) -> bytes:
    payload = _chunk(0x1F, _mesh_header(vertices=vertices, faces=faces))
    payload += b"".join(children)
    return _chunk(0x00, payload, container=True)


def _pivot(name: str = "Root") -> bytes:
    return struct.pack("<16si10f", _fixed(name), -1, *([0.0] * 10))


def _hierarchy(name: str = "Rig", pivots: int = 1) -> bytes:
    header = struct.pack("<I16sI12x", 0x00040000, _fixed(name), pivots)
    records = b"".join(_pivot(f"P{index}") for index in range(pivots))
    return _chunk(
        0x100,
        _chunk(0x101, header) + _chunk(0x102, records),
        container=True,
    )


def _hierarchy_with_fixups(pivots: int = 1) -> bytes:
    header = struct.pack("<I16sI12x", 0x00040000, _fixed("Rig"), pivots)
    records = b"".join(_pivot(f"P{index}") for index in range(pivots))
    fixups = struct.pack(
        f"<{pivots * 12}f",
        *[float(index) for index in range(pivots * 12)],
    )
    return _chunk(
        0x100,
        _chunk(0x101, header) + _chunk(0x102, records) + _chunk(0x103, fixups),
        container=True,
    )


def _raw_animation(*channels: bytes, hierarchy: str = "Rig", frames: int = 2) -> bytes:
    header = struct.pack(
        "<I16s16sII",
        0x00040000,
        _fixed("Walk"),
        _fixed(hierarchy),
        frames,
        30,
    )
    return _chunk(
        0x200,
        _chunk(0x201, header) + b"".join(channels),
        container=True,
    )


def _compressed_animation(
    *channels: bytes,
    hierarchy: str = "Rig",
    frames: int = 2,
    flavor: int = 0,
) -> bytes:
    header = struct.pack(
        "<I16s16sIHH",
        0x00040000,
        _fixed("Walk"),
        _fixed(hierarchy),
        frames,
        30,
        flavor,
    )
    return _chunk(
        0x280,
        _chunk(0x281, header) + b"".join(channels),
        container=True,
    )


def _resolver(
    *sources: tuple[bytes, int],
    manifest_sha256: str = "a" * 64,
    inventory_sha256: str = "c" * 64,
):
    grouped: dict[str, tuple[bytes, int]] = {}
    for source, manifest_entry_count in sources:
        source_sha256 = hashlib.sha256(source).hexdigest()
        previous = grouped.get(source_sha256)
        if previous is None:
            grouped[source_sha256] = (source, manifest_entry_count)
        else:
            grouped[source_sha256] = (
                source,
                previous[1] + manifest_entry_count,
            )
    evidence = tuple(
        _W3DExternalHierarchySourceObservations(
            source_sha256=source_sha256,
            manifest_entry_count=entry_count,
            observations=_collect_external_hierarchy_observations(source),
        )
        for source_sha256, (source, entry_count) in sorted(grouped.items())
    )
    return _build_external_hierarchy_resolver(
        manifest_sha256=manifest_sha256,
        manifest_aggregate_sha256="b" * 64,
        inventory_sha256=inventory_sha256,
        sources=evidence,
    )


def _sibling_resolver(files: dict[str, bytes]):
    grouped: dict[str, tuple[bytes, list[str]]] = {}
    for path, source in files.items():
        source_sha256 = hashlib.sha256(source).hexdigest()
        previous = grouped.get(source_sha256)
        path_lookup = _virtual_path_lookup_sha256(path)
        if previous is None:
            grouped[source_sha256] = (source, [path_lookup])
        else:
            previous[1].append(path_lookup)
    evidence = tuple(
        _W3DExternalHierarchySourceObservations(
            source_sha256=source_sha256,
            manifest_entry_count=len(path_lookups),
            observations=_collect_external_hierarchy_observations(source),
            sibling_path_lookup_sha256s=tuple(sorted(path_lookups)),
        )
        for source_sha256, (source, path_lookups) in sorted(grouped.items())
    )
    return _build_external_hierarchy_resolver(
        manifest_sha256="a" * 64,
        manifest_aggregate_sha256="b" * 64,
        inventory_sha256="c" * 64,
        sources=evidence,
    )


class W3DDecodePlanGeometryTests(unittest.TestCase):
    def test_nested_ownership_preserves_primary_and_secondary_streams(self) -> None:
        primary = struct.pack("<6f", 0.0, 1.0, 2.0, 3.0, 4.0, 5.0)
        secondary = struct.pack("<6f", 10.0, 11.0, 12.0, 13.0, 14.0, 15.0)
        nested_uv = _chunk(
            0x38,
            _chunk(
                0x48,
                _chunk(0x4A, struct.pack("<4f", 0.0, 0.0, 1.0, 1.0)),
                container=True,
            ),
            container=True,
        )
        source = _mesh(
            _chunk(0x02, primary),
            _chunk(0x0C00, secondary),
            _chunk(0x03, primary),
            _chunk(0x0C01, secondary),
            nested_uv,
        )

        plan = plan_w3d_stream_decode(source, "safe/mesh.w3d")

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(len(plan.geometry_streams), 5)
        slots = [item.attestation.stream_slot for item in plan.geometry_streams]
        self.assertEqual(slots[:4], ["primary", "secondary", "primary", "secondary"])
        self.assertEqual(
            len({item.owner_container_header_offset for item in plan.geometry_streams}),
            1,
        )
        self.assertNotEqual(
            plan.geometry_streams[0].payload_sha256,
            plan.geometry_streams[1].payload_sha256,
        )

    def test_orphan_and_duplicate_owner_headers_fail_closed(self) -> None:
        payload = struct.pack("<3f", 1.0, 2.0, 3.0)
        orphan = plan_w3d_stream_decode(_chunk(0x02, payload))
        self.assertEqual(orphan.terminals[0].code, "missing-mesh-owner")
        self.assertFalse(orphan.stream_decode_complete)

        duplicate_headers = _chunk(0x1F, _mesh_header(vertices=1, faces=0, name="Two"))
        source = _mesh(duplicate_headers, _chunk(0x02, payload), vertices=1)
        plan = plan_w3d_stream_decode(source)
        codes = {item.code for item in plan.terminals}
        self.assertIn("ambiguous-mesh-owner-header", codes)
        self.assertEqual(plan.geometry_streams, ())

    def test_invalid_stream_cardinality_is_a_damaged_terminal(self) -> None:
        source = _mesh(_chunk(0x02, struct.pack("<3f", 1.0, 2.0, 3.0)), vertices=2)
        plan = plan_w3d_stream_decode(source)
        terminal = next(item for item in plan.terminals if item.chunk_id == 0x02)
        self.assertEqual(terminal.state, "damaged")
        self.assertEqual(terminal.code, "geometry-stream-payload-invalid")
        self.assertFalse(plan.stream_decode_complete)

    def test_unknown_owner_header_extension_is_not_silently_accepted(self) -> None:
        header = _chunk(
            0x1F,
            _mesh_header(vertices=1, faces=0) + b"\x00\x00\x00\x00",
        )
        source = _chunk(
            0x00,
            header + _chunk(0x02, struct.pack("<3f", 1.0, 2.0, 3.0)),
            container=True,
        )

        plan = plan_w3d_stream_decode(source)

        self.assertIn(
            "mesh-owner-header-layout-unknown",
            {item.code for item in plan.terminals},
        )
        self.assertEqual(plan.geometry_streams, ())


class W3DDecodePlanAnimationTests(unittest.TestCase):
    def test_animation_channel_uses_exact_animation_and_hierarchy_owners(self) -> None:
        channel = struct.pack("<6H2f", 0, 1, 1, 0, 0, 0, -1.0, 2.0)
        source = _hierarchy() + _raw_animation(_chunk(0x202, channel))

        plan = plan_w3d_stream_decode(source)

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(len(plan.animation_streams), 1)
        decoded = plan.animation_streams[0]
        self.assertEqual(decoded.attestation.owner_frame_count, 2)
        self.assertEqual(decoded.attestation.owner_pivot_count, 1)
        self.assertNotEqual(
            decoded.owner_container_header_offset,
            decoded.owner_header_offset,
        )

    def test_primary_skipped_pivot_is_decoded_unbound_and_not_damaged(self) -> None:
        bound = _chunk(0x282, struct.pack("<IHBBIf", 1, 1, 1, 0, 0, 1.0))
        skipped = _chunk(0x282, struct.pack("<IHBBIf", 1, 2, 1, 0, 0, 2.0))
        source = _hierarchy(pivots=2) + _compressed_animation(
            bound,
            skipped,
            frames=1,
        )

        plan = plan_w3d_stream_decode(source)

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(len(plan.animation_streams), 2)
        self.assertEqual(len(plan.primary_bound_animation_streams), 1)
        self.assertEqual(len(plan.primary_skipped_animation_streams), 1)
        self.assertEqual(
            plan.primary_skipped_animation_streams[0].primary_import_disposition,
            "skipped-invalid-pivot",
        )
        self.assertFalse(
            plan.primary_skipped_animation_streams[0].attestation.pivot_in_range
        )
        self.assertFalse(
            any(
                item.state == "damaged" and item.chunk_id == 0x282
                for item in plan.terminals
            )
        )
        neutral = plan.neutral()
        self.assertEqual(neutral["primaryBoundAnimationStreamCount"], 1)
        self.assertEqual(neutral["primarySkippedAnimationStreamCount"], 1)
        encoded = json.dumps(neutral, sort_keys=True).casefold()
        self.assertNotIn("glb", encoded)
        self.assertNotIn("conversioncomplete", encoded)

        mutated = _chunk(0x282, struct.pack("<IHBBIf", 1, 3, 1, 0, 0, 2.0))
        changed = plan_w3d_stream_decode(
            _hierarchy(pivots=2) + _compressed_animation(bound, mutated, frames=1)
        )
        self.assertEqual(len(changed.primary_skipped_animation_streams), 1)
        self.assertNotEqual(plan.plan_sha256, changed.plan_sha256)
        self.assertNotEqual(
            plan.primary_skipped_animation_streams[0].attestation_sha256,
            changed.primary_skipped_animation_streams[0].attestation_sha256,
        )

    def test_missing_and_ambiguous_hierarchy_cardinality_remain_unresolved(
        self,
    ) -> None:
        channel = _chunk(0x202, struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0))
        missing = plan_w3d_stream_decode(_raw_animation(channel, frames=1))
        self.assertIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in missing.terminals},
        )

        duplicate = _hierarchy() + _hierarchy() + _raw_animation(channel, frames=1)
        ambiguous = plan_w3d_stream_decode(duplicate)
        self.assertIn(
            "ambiguous-hierarchy-pivot-cardinality",
            {item.code for item in ambiguous.terminals},
        )
        self.assertEqual(ambiguous.animation_streams, ())

        extended_header = struct.pack(
            "<I16sI12x4x",
            0x00040000,
            _fixed("Rig"),
            1,
        )
        invalid_duplicate = _chunk(
            0x100,
            _chunk(0x101, extended_header) + _chunk(0x102, _pivot()),
            container=True,
        )
        mixed = plan_w3d_stream_decode(
            _hierarchy() + invalid_duplicate + _raw_animation(channel, frames=1)
        )
        self.assertIn(
            "ambiguous-hierarchy-pivot-cardinality",
            {item.code for item in mixed.terminals},
        )
        self.assertEqual(mixed.animation_streams, ())

    def test_unique_external_hierarchy_is_exactly_proven_without_identifier_leakage(
        self,
    ) -> None:
        hierarchy_source = _hierarchy("PrivateRig", pivots=1)
        resolver = _resolver((hierarchy_source, 1))
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation_source = _raw_animation(
            channel,
            hierarchy="PrivateRig",
            frames=1,
        )

        source_only = plan_w3d_stream_decode(animation_source)
        resolved = plan_w3d_stream_decode(
            animation_source,
            external_hierarchy_resolver=resolver,
        )

        self.assertIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in source_only.terminals},
        )
        self.assertNotIn("externalHierarchyResolverSha256", source_only.neutral())
        self.assertTrue(resolved.stream_decode_complete)
        self.assertEqual(len(resolved.animation_streams), 1)
        self.assertEqual(resolved.animation_streams[0].attestation.owner_pivot_count, 1)
        self.assertIsNotNone(
            resolved.animation_streams[0].external_hierarchy_proof_sha256
        )
        self.assertEqual(
            resolved.external_hierarchy_resolver_sha256,
            resolver.resolver_sha256,
        )
        encoded = json.dumps(
            {"plan": resolved.neutral(), "resolver": resolver.neutral()},
            sort_keys=True,
        ).casefold()
        self.assertNotIn("privaterig", encoded)
        self.assertNotIn("identifier", encoded)

    def test_external_identical_duplicates_are_safe_but_conflicts_are_ambiguous(
        self,
    ) -> None:
        hierarchy_source = _hierarchy("Rig", pivots=1)
        duplicate_resolver = _resolver((hierarchy_source, 2))
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation = _raw_animation(channel, frames=1)

        duplicate = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=duplicate_resolver,
        )
        self.assertTrue(duplicate.stream_decode_complete)
        self.assertEqual(duplicate_resolver.neutral()["states"]["resolved"], 1)

        conflicting_resolver = _resolver(
            (_hierarchy("Rig", pivots=1), 1),
            (_hierarchy("Rig", pivots=2), 1),
        )
        conflicting = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=conflicting_resolver,
        )
        self.assertIn(
            "ambiguous-external-hierarchy-pivot-cardinality",
            {item.code for item in conflicting.terminals},
        )
        self.assertEqual(conflicting.animation_streams, ())
        self.assertEqual(conflicting_resolver.neutral()["states"]["ambiguous"], 1)

    def test_internal_hierarchy_is_preferred_and_internal_damage_never_falls_back(
        self,
    ) -> None:
        resolver = _resolver(
            (_hierarchy("Rig", pivots=1), 1),
            (_hierarchy("Rig", pivots=2), 1),
        )
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        local = plan_w3d_stream_decode(
            _hierarchy("Rig", pivots=1) + _raw_animation(channel, frames=1),
            external_hierarchy_resolver=resolver,
        )
        self.assertTrue(local.stream_decode_complete)
        self.assertIsNone(local.animation_streams[0].external_hierarchy_proof_sha256)

        extended_header = struct.pack(
            "<I16sI12x4x",
            0x00040000,
            _fixed("Rig"),
            1,
        )
        damaged_local_hierarchy = _chunk(
            0x100,
            _chunk(0x101, extended_header) + _chunk(0x102, _pivot()),
            container=True,
        )
        damaged = plan_w3d_stream_decode(
            damaged_local_hierarchy + _raw_animation(channel, frames=1),
            external_hierarchy_resolver=_resolver((_hierarchy("Rig"), 1)),
        )
        self.assertIn(
            "hierarchy-owner-header-layout-unknown",
            {item.code for item in damaged.terminals},
        )
        self.assertEqual(damaged.animation_streams, ())

    def test_resolver_tamper_is_rejected_and_identity_changes_plan_hash(self) -> None:
        hierarchy_source = _hierarchy("Rig", pivots=1)
        first_resolver = _resolver((hierarchy_source, 1))
        second_resolver = _resolver(
            (hierarchy_source, 1),
            manifest_sha256="d" * 64,
            inventory_sha256="e" * 64,
        )
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation = _raw_animation(channel, frames=1)
        first = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=first_resolver,
        )
        second = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=second_resolver,
        )
        self.assertNotEqual(first.plan_sha256, second.plan_sha256)
        self.assertNotEqual(
            first.animation_streams[0].external_hierarchy_proof_sha256,
            second.animation_streams[0].external_hierarchy_proof_sha256,
        )

        occurrence = first_resolver.occurrences[0]
        injected_observation = replace(
            occurrence.observation,
            pivot_count=2,
        )
        injected = replace(
            first_resolver,
            occurrences=(replace(occurrence, observation=injected_observation),),
        )
        with self.assertRaisesRegex(W3DDecodePlanError, "integrity"):
            plan_w3d_stream_decode(
                animation,
                external_hierarchy_resolver=injected,
            )

    def test_sibling_resolution_is_directory_local_while_global_is_ambiguous(
        self,
    ) -> None:
        first_hierarchy = _hierarchy("Rig", pivots=1)
        second_hierarchy = _hierarchy("Rig", pivots=2)
        sibling_resolver = _sibling_resolver(
            {
                "private-alpha/Rig.w3d": first_hierarchy,
                "private-beta/Rig.w3d": second_hierarchy,
            }
        )
        global_resolver = _resolver(
            (first_hierarchy, 1),
            (second_hierarchy, 1),
        )
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation = _raw_animation(channel, frames=1)

        first = plan_w3d_stream_decode(
            animation,
            "private-alpha/walk.w3d",
            external_hierarchy_resolver=sibling_resolver,
        )
        second = plan_w3d_stream_decode(
            animation,
            "private-beta/walk.w3d",
            external_hierarchy_resolver=sibling_resolver,
        )
        ambiguous = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=global_resolver,
        )

        self.assertTrue(first.stream_decode_complete)
        self.assertTrue(second.stream_decode_complete)
        self.assertEqual(first.animation_streams[0].attestation.owner_pivot_count, 1)
        self.assertEqual(second.animation_streams[0].attestation.owner_pivot_count, 2)
        self.assertNotEqual(first.plan_sha256, second.plan_sha256)
        self.assertIn(
            "ambiguous-external-hierarchy-pivot-cardinality",
            {item.code for item in ambiguous.terminals},
        )
        self.assertEqual(sibling_resolver.neutral()["resolutionMode"], "sibling-path")
        self.assertEqual(
            global_resolver.neutral()["resolutionMode"],
            "global-no-context-compatibility",
        )

    def test_sibling_filename_controls_resolution_not_internal_identifier(self) -> None:
        resolver = _sibling_resolver(
            {"private-zone/Rig.w3d": _hierarchy("Unrelated", pivots=1)}
        )
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        result = plan_w3d_stream_decode(
            _raw_animation(channel, hierarchy="Rig", frames=1),
            "private-zone/walk.w3d",
            external_hierarchy_resolver=resolver,
        )

        self.assertTrue(result.stream_decode_complete)
        self.assertEqual(result.animation_streams[0].attestation.owner_pivot_count, 1)

    def test_sibling_lookup_is_case_insensitive_and_never_falls_back_global(
        self,
    ) -> None:
        hierarchy = _hierarchy("Rig", pivots=1)
        sibling_resolver = _sibling_resolver({"PRIVATE-ZONE/rIg.W3D": hierarchy})
        global_resolver = _resolver((hierarchy, 1))
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation = _raw_animation(channel, hierarchy="RIG", frames=1)

        resolved = plan_w3d_stream_decode(
            animation,
            "private-zone/walk.w3d",
            external_hierarchy_resolver=sibling_resolver,
        )
        missing = plan_w3d_stream_decode(
            animation,
            "different-zone/walk.w3d",
            external_hierarchy_resolver=sibling_resolver,
        )
        global_result = plan_w3d_stream_decode(
            animation,
            external_hierarchy_resolver=global_resolver,
        )

        self.assertTrue(resolved.stream_decode_complete)
        self.assertTrue(global_result.stream_decode_complete)
        self.assertIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in missing.terminals},
        )

    def test_multiple_and_damaged_sibling_hierarchies_are_explicit(self) -> None:
        channel = _chunk(
            0x202,
            struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0),
        )
        animation = _raw_animation(channel, frames=1)
        multiple_resolver = _sibling_resolver(
            {"multiple/Rig.w3d": _hierarchy("One") + _hierarchy("Two")}
        )
        extended_header = struct.pack(
            "<I16sI12x4x",
            0x00040000,
            _fixed("Rig"),
            1,
        )
        damaged_hierarchy = _chunk(
            0x100,
            _chunk(0x101, extended_header) + _chunk(0x102, _pivot()),
            container=True,
        )
        damaged_resolver = _sibling_resolver({"damaged/Rig.w3d": damaged_hierarchy})

        multiple = plan_w3d_stream_decode(
            animation,
            "multiple/walk.w3d",
            external_hierarchy_resolver=multiple_resolver,
        )
        damaged = plan_w3d_stream_decode(
            animation,
            "damaged/walk.w3d",
            external_hierarchy_resolver=damaged_resolver,
        )

        self.assertIn(
            "ambiguous-external-hierarchy-pivot-cardinality",
            {item.code for item in multiple.terminals},
        )
        self.assertIn(
            "damaged-external-hierarchy-cardinality-evidence",
            {item.code for item in damaged.terminals},
        )
        encoded = json.dumps(
            {
                "resolver": multiple_resolver.neutral(),
                "plan": multiple.neutral(),
            },
            sort_keys=True,
        ).casefold()
        self.assertNotIn("multiple/", encoded)
        self.assertNotIn("private-zone", encoded)
        self.assertNotIn("identifier", encoded)

    def test_identifier_prefix_resolves_cross_shard_skeleton_when_sibling_absent(
        self,
    ) -> None:
        resolver = _sibling_resolver(
            {"art/w3d/iu/iubanner_skl.w3d": _hierarchy("InternalName", pivots=3)}
        )
        channel = _chunk(0x202, struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0))
        animation = _raw_animation(channel, hierarchy="iubanner_skl", frames=1)

        resolved = plan_w3d_stream_decode(
            animation,
            "art/w3d/mu/banner_wall.w3d",
            external_hierarchy_resolver=resolver,
        )

        self.assertTrue(resolved.stream_decode_complete)
        self.assertEqual(len(resolved.animation_streams), 1)
        self.assertEqual(
            resolved.animation_streams[0].attestation.owner_pivot_count, 3
        )
        self.assertIsNotNone(
            resolved.animation_streams[0].external_hierarchy_proof_sha256
        )
        encoded = json.dumps(
            {"plan": resolved.neutral(), "resolver": resolver.neutral()},
            sort_keys=True,
        ).casefold()
        self.assertNotIn("iubanner", encoded)
        self.assertNotIn("art/w3d", encoded)
        self.assertNotIn("identifier", encoded)

    def test_sibling_candidate_precedes_identifier_prefix(self) -> None:
        resolver = _sibling_resolver(
            {
                "art/w3d/mu/rig.w3d": _hierarchy("SiblingRig", pivots=1),
                "art/w3d/ri/rig.w3d": _hierarchy("PrefixRig", pivots=2),
            }
        )
        channel = _chunk(0x202, struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0))
        animation = _raw_animation(channel, hierarchy="Rig", frames=1)

        sibling_first = plan_w3d_stream_decode(
            animation,
            "art/w3d/mu/walk.w3d",
            external_hierarchy_resolver=resolver,
        )
        prefix_only = plan_w3d_stream_decode(
            animation,
            "art/w3d/zz/walk.w3d",
            external_hierarchy_resolver=resolver,
        )

        self.assertTrue(sibling_first.stream_decode_complete)
        self.assertEqual(
            sibling_first.animation_streams[0].attestation.owner_pivot_count, 1
        )
        self.assertTrue(prefix_only.stream_decode_complete)
        self.assertEqual(
            prefix_only.animation_streams[0].attestation.owner_pivot_count, 2
        )
        self.assertNotEqual(
            sibling_first.animation_streams[0].external_hierarchy_proof_sha256,
            prefix_only.animation_streams[0].external_hierarchy_proof_sha256,
        )

    def test_reference_authored_nowhere_stays_fail_closed_over_near_names(
        self,
    ) -> None:
        # A skeleton named one character apart exists; using it would be a
        # silently wrong model.  The reference must stay an explicit
        # unresolved terminal.
        resolver = _sibling_resolver(
            {"art/w3d/cu/cusheep_skl.w3d": _hierarchy("SheepRig", pivots=2)}
        )
        channel = _chunk(0x202, struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0))
        animation = _raw_animation(channel, hierarchy="cugsheep_skl", frames=1)

        missing = plan_w3d_stream_decode(
            animation,
            "art/w3d/cu/cusheep_dwna.w3d",
            external_hierarchy_resolver=resolver,
        )

        self.assertFalse(missing.stream_decode_complete)
        self.assertEqual(missing.animation_streams, ())
        self.assertIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in missing.terminals},
        )

        short = plan_w3d_stream_decode(
            _raw_animation(channel, hierarchy="R", frames=1),
            "art/w3d/cu/short.w3d",
            external_hierarchy_resolver=resolver,
        )
        self.assertIn(
            "missing-hierarchy-pivot-cardinality",
            {item.code for item in short.terminals},
        )

    def test_ambiguous_identifier_prefix_candidate_is_explicit(self) -> None:
        resolver = _sibling_resolver(
            {"art/w3d/ri/rig.w3d": _hierarchy("One") + _hierarchy("Two")}
        )
        channel = _chunk(0x202, struct.pack("<6Hf", 0, 0, 1, 0, 0, 0, 1.0))
        ambiguous = plan_w3d_stream_decode(
            _raw_animation(channel, hierarchy="Rig", frames=1),
            "art/w3d/zz/walk.w3d",
            external_hierarchy_resolver=resolver,
        )

        self.assertIn(
            "ambiguous-external-hierarchy-pivot-cardinality",
            {item.code for item in ambiguous.terminals},
        )
        self.assertEqual(ambiguous.animation_streams, ())


class W3DDecodePlanCompletenessTests(unittest.TestCase):
    def test_nothing_to_decode_is_vacuously_complete_and_marked(self) -> None:
        skeleton = plan_w3d_stream_decode(_hierarchy("Rig", pivots=2))
        self.assertEqual(skeleton.target_stream_chunk_count, 0)
        self.assertTrue(skeleton.stream_decode_complete)
        self.assertTrue(skeleton.stream_decode_vacuously_complete)
        self.assertIs(skeleton.neutral()["streamDecodeCompleteVacuous"], True)

        channel_less = plan_w3d_stream_decode(_raw_animation(frames=1))
        self.assertEqual(channel_less.target_stream_chunk_count, 0)
        self.assertTrue(channel_less.stream_decode_complete)
        self.assertTrue(channel_less.stream_decode_vacuously_complete)
        self.assertIs(channel_less.neutral()["streamDecodeCompleteVacuous"], True)

    def test_decoded_completeness_is_never_marked_vacuous(self) -> None:
        decoded = plan_w3d_stream_decode(
            _mesh(_chunk(0x02, struct.pack("<6f", 0.0, 1.0, 2.0, 3.0, 4.0, 5.0)))
        )
        self.assertTrue(decoded.stream_decode_complete)
        self.assertFalse(decoded.stream_decode_vacuously_complete)
        self.assertNotIn("streamDecodeCompleteVacuous", decoded.neutral())
        self.assertNotIn(
            "streamdecodecompletevacuous",
            json.dumps(decoded.neutral(), sort_keys=True).casefold(),
        )

    def test_zero_targets_with_a_blocking_terminal_stays_incomplete(self) -> None:
        blocked = plan_w3d_stream_decode(_chunk(0x7777, b"opaque"))
        self.assertEqual(blocked.target_stream_chunk_count, 0)
        self.assertFalse(blocked.stream_decode_complete)
        self.assertFalse(blocked.stream_decode_vacuously_complete)
        self.assertNotIn("streamDecodeCompleteVacuous", blocked.neutral())

    def test_vacuous_marker_is_sealed_into_the_plan_hash(self) -> None:
        plan = plan_w3d_stream_decode(_hierarchy("Rig", pivots=1))
        self.assertTrue(plan.stream_decode_vacuously_complete)
        core = {
            "schema": "openbfme.w3d-decode-plan",
            "schemaVersion": 2,
            "sourceByteLength": plan.source_byte_length,
            "sourceSha256": plan.source_sha256,
            "encounteredChunkCount": plan.encountered_chunk_count,
            "targetStreamChunkCount": plan.target_stream_chunk_count,
            "decodedStreams": [item.neutral() for item in plan.decoded_streams],
            "terminals": [item.neutral() for item in plan.terminals],
            "streamDecodeComplete": True,
            "streamDecodeCompleteVacuous": True,
        }

        def sealed(value: dict[str, object]) -> str:
            encoded = json.dumps(
                value,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
            ).encode("ascii")
            return hashlib.sha256(encoded).hexdigest()

        self.assertEqual(plan.plan_sha256, sealed(core))
        collapsed = {
            key: value
            for key, value in core.items()
            if key != "streamDecodeCompleteVacuous"
        }
        self.assertNotEqual(plan.plan_sha256, sealed(collapsed))

    def test_vacuous_and_decoded_completeness_have_distinct_plan_evidence(
        self,
    ) -> None:
        vacuous = plan_w3d_stream_decode(_hierarchy("Rig", pivots=1))
        decoded = plan_w3d_stream_decode(
            _mesh(_chunk(0x02, struct.pack("<6f", 0.0, 1.0, 2.0, 3.0, 4.0, 5.0)))
        )
        self.assertTrue(vacuous.stream_decode_complete)
        self.assertTrue(decoded.stream_decode_complete)
        self.assertNotEqual(
            vacuous.neutral().get("streamDecodeCompleteVacuous"),
            decoded.neutral().get("streamDecodeCompleteVacuous"),
        )


class W3DDecodePlanAuxiliaryTests(unittest.TestCase):
    def test_all_nine_source_proven_auxiliary_layouts_are_owner_bound(self) -> None:
        tangents = struct.pack("<6f", 1, 0, 0, 0, 1, 0)
        bitangents = struct.pack("<6f", 0, 0, 1, 1, 1, 0)
        material_pass = _chunk(
            0x38,
            _chunk(0x3B, bytes((1, 2, 3, 4, 5, 6, 7, 8))),
            container=True,
        )
        aabb = _chunk(
            0x90,
            _chunk(0x91, struct.pack("<8I", 1, 2, 0, 0, 0, 0, 0, 0))
            + _chunk(0x92, struct.pack("<2I", 1, 0))
            + _chunk(
                0x93,
                struct.pack("<6f2I", 0, 0, 0, 2, 2, 2, 0x80000000, 2),
            ),
            container=True,
        )
        source = (
            _mesh(
                _chunk(0x0C, b"authored private note\0"),
                _chunk(0x22, struct.pack("<2I", 4, 9)),
                _chunk(0x60, tangents),
                _chunk(0x61, bitangents),
                material_pass,
                aabb,
                vertices=2,
                faces=2,
            )
            + _hierarchy_with_fixups()
        )

        plan = plan_w3d_stream_decode(source, "neutral.w3d")

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(plan.target_stream_chunk_count, 9)
        self.assertEqual(len(plan.auxiliary_streams), 9)
        self.assertEqual(plan.geometry_streams, ())
        self.assertEqual(plan.animation_streams, ())
        self.assertEqual(
            {item.chunk_id for item in plan.auxiliary_streams},
            {0x0C, 0x22, 0x3B, 0x60, 0x61, 0x91, 0x92, 0x93, 0x103},
        )
        self.assertEqual(plan.neutral()["auxiliaryStreamCount"], 9)
        encoded = json.dumps(plan.neutral(), sort_keys=True).casefold()
        self.assertNotIn("authored", encoded)
        self.assertNotIn("private", encoded)
        self.assertNotIn("note", encoded)

        aabb_streams = [
            item
            for item in plan.auxiliary_streams
            if item.chunk_id in {0x91, 0x92, 0x93}
        ]
        self.assertEqual(
            len({item.owner_container_header_offset for item in aabb_streams}),
            1,
        )
        self.assertEqual(
            len({item.owner_header_offset for item in aabb_streams}),
            1,
        )

    def test_auxiliary_paths_reject_wrong_direct_owners(self) -> None:
        tangent = struct.pack("<3f", 1, 0, 0)
        orphan = plan_w3d_stream_decode(_chunk(0x60, tangent))
        self.assertIn(
            "invalid-mesh-auxiliary-owner",
            {item.code for item in orphan.terminals},
        )

        diffuse_under_mesh = plan_w3d_stream_decode(
            _mesh(_chunk(0x3B, bytes((1, 2, 3, 4))), vertices=1)
        )
        self.assertIn(
            "invalid-material-pass-owner",
            {item.code for item in diffuse_under_mesh.terminals},
        )

        aabb_child_under_mesh = plan_w3d_stream_decode(
            _mesh(_chunk(0x92, struct.pack("<I", 0)), vertices=1, faces=1)
        )
        self.assertIn(
            "invalid-aabb-tree-owner",
            {item.code for item in aabb_child_under_mesh.terminals},
        )

        fixup_under_mesh = plan_w3d_stream_decode(
            _mesh(_chunk(0x103, struct.pack("<12f", *([0.0] * 12))))
        )
        self.assertIn(
            "invalid-hierarchy-auxiliary-owner",
            {item.code for item in fixup_under_mesh.terminals},
        )

    def test_mesh_and_hierarchy_cardinality_fail_closed(self) -> None:
        short_tangent = plan_w3d_stream_decode(
            _mesh(
                _chunk(0x60, struct.pack("<3f", 1, 0, 0)),
                vertices=2,
            )
        )
        terminal = next(
            item for item in short_tangent.terminals if item.chunk_id == 0x60
        )
        self.assertEqual(terminal.state, "damaged")
        self.assertEqual(terminal.code, "auxiliary-stream-payload-invalid")

        unterminated = plan_w3d_stream_decode(
            _mesh(_chunk(0x0C, b"not terminated"), vertices=0)
        )
        self.assertIn(
            "auxiliary-stream-payload-invalid",
            {item.code for item in unterminated.terminals},
        )

        hierarchy_header = struct.pack("<I16sI12x", 0x00040000, _fixed("Rig"), 1)
        duplicate = _chunk(
            0x100,
            _chunk(0x101, hierarchy_header)
            + _chunk(0x101, hierarchy_header)
            + _chunk(0x102, _pivot())
            + _chunk(0x103, struct.pack("<12f", *([0.0] * 12))),
            container=True,
        )
        ambiguous = plan_w3d_stream_decode(duplicate)
        self.assertIn(
            "ambiguous-hierarchy-owner-header",
            {item.code for item in ambiguous.terminals},
        )
        self.assertEqual(ambiguous.auxiliary_streams, ())

    def test_aabb_header_presence_uniqueness_and_counts_fail_closed(self) -> None:
        poly_indices = _chunk(0x92, struct.pack("<I", 0))
        nodes = _chunk(
            0x93,
            struct.pack("<6f2I", 0, 0, 0, 1, 1, 1, 0x80000000, 1),
        )
        missing = plan_w3d_stream_decode(
            _mesh(_chunk(0x90, poly_indices + nodes, container=True), faces=1)
        )
        self.assertIn(
            "missing-aabb-owner-header-cardinality",
            {item.code for item in missing.terminals},
        )

        header = _chunk(0x91, struct.pack("<8I", 1, 1, 0, 0, 0, 0, 0, 0))
        duplicate = plan_w3d_stream_decode(
            _mesh(
                _chunk(
                    0x90,
                    header + header + poly_indices + nodes,
                    container=True,
                ),
                faces=1,
            )
        )
        self.assertIn(
            "ambiguous-aabb-owner-header",
            {item.code for item in duplicate.terminals},
        )
        self.assertEqual(duplicate.auxiliary_streams, ())

        wrong_poly_count = _chunk(
            0x91,
            struct.pack("<8I", 1, 2, 0, 0, 0, 0, 0, 0),
        )
        mismatch = plan_w3d_stream_decode(
            _mesh(
                _chunk(
                    0x90,
                    wrong_poly_count + poly_indices + nodes,
                    container=True,
                ),
                faces=1,
            )
        )
        self.assertIn(
            "aabb-owner-header-payload-invalid",
            {item.code for item in mismatch.terminals},
        )
        self.assertEqual(mismatch.auxiliary_streams, ())


class W3DDecodePlanEmitterTests(unittest.TestCase):
    def test_source_proven_emitter_children_are_owner_bound_and_decoded(self) -> None:
        payloads = _emitter_payloads()
        payloads.pop(0x50D)
        order = (0x501, 0x502, 0x503, 0x504, 0x505, 0x509, 0x50A, 0x50B, 0x50C)
        source = _emitter_file(_emitter_container_payload(payloads, order=order))

        plan = plan_w3d_stream_decode(source, "neutral-emitter.w3d")

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(plan.target_stream_chunk_count, 9)
        self.assertEqual(len(plan.emitter_streams), 9)
        self.assertEqual(plan.neutral()["emitterStreamCount"], 9)
        self.assertEqual(
            {item.chunk_id for item in plan.emitter_streams},
            set(order),
        )
        self.assertEqual(
            len({item.owner_container_header_offset for item in plan.emitter_streams}),
            1,
        )
        self.assertEqual(
            len({item.owner_header_offset for item in plan.emitter_streams}),
            1,
        )
        encoded = json.dumps(plan.neutral(), sort_keys=True).casefold()
        self.assertNotIn("private-emitter", encoded)
        self.assertNotIn("private-texture", encoded)

    def test_source_proven_extra_info_completes_the_emitter_target_set(self) -> None:
        plan = plan_w3d_stream_decode(_emitter_file())

        self.assertTrue(plan.stream_decode_complete)
        self.assertEqual(plan.target_stream_chunk_count, 10)
        self.assertEqual(len(plan.emitter_streams), 10)
        self.assertEqual(
            [item for item in plan.terminals if item.chunk_id == 0x50D],
            [],
        )

    def test_orphan_and_cross_child_corruption_fail_closed(self) -> None:
        orphan = plan_w3d_stream_decode(_chunk(0x501, _emitter_payloads()[0x501]))
        self.assertIn(
            "invalid-emitter-stream-owner",
            {item.code for item in orphan.terminals},
        )
        self.assertEqual(orphan.emitter_streams, ())

        payloads = _emitter_payloads()
        payloads.pop(0x50D)
        payloads[0x501] = struct.pack("<I16s", 1, b"bad\0" + b"\0" * 12)
        damaged = plan_w3d_stream_decode(
            _emitter_file(
                _emitter_container_payload(
                    payloads,
                    order=(
                        0x501,
                        0x502,
                        0x503,
                        0x504,
                        0x505,
                        0x509,
                        0x50A,
                        0x50B,
                        0x50C,
                    ),
                )
            )
        )
        self.assertEqual(damaged.emitter_streams, ())
        self.assertEqual(damaged.target_stream_chunk_count, 9)
        self.assertEqual(
            {item.code for item in damaged.terminals if item.chunk_id in payloads},
            {"emitter-container-payload-invalid"},
        )


class W3DDecodePlanTerminalTests(unittest.TestCase):
    def test_truncation_unknown_and_known_unhandled_chunks_are_explicit(self) -> None:
        truncated = struct.pack("<II", 0x02, 12) + b"\x00" * 4
        damaged = plan_w3d_stream_decode(truncated)
        self.assertIn(
            "metadata-truncated-chunk-payload",
            {item.code for item in damaged.terminals},
        )
        self.assertFalse(damaged.stream_decode_complete)

        source = _mesh(
            _chunk(0x60, struct.pack("<3f", 1.0, 2.0, 3.0)),
            _chunk(0xDEADBEEF, b"x"),
            vertices=1,
        )
        explicit = plan_w3d_stream_decode(source)
        codes = {item.code for item in explicit.terminals}
        self.assertEqual(len(explicit.auxiliary_streams), 1)
        self.assertEqual(explicit.auxiliary_streams[0].chunk_id, 0x60)
        self.assertNotIn("known-data-layout-not-decoded", codes)
        self.assertIn("unknown-chunk-layout", codes)
        self.assertFalse(explicit.stream_decode_complete)

    def test_provenance_overlap_and_bounds_fail_before_payload_decode(self) -> None:
        source = _mesh(_chunk(0x02, struct.pack("<3f", 1.0, 2.0, 3.0)), vertices=1)
        metadata = scan_w3d_metadata(source, "valid.w3d")
        chunks = list(metadata.chunks)
        stream_index = next(
            index for index, item in enumerate(chunks) if item.chunk_id == 0x02
        )
        chunks[stream_index] = replace(
            chunks[stream_index], payload_offset=len(source) + 1
        )
        bad_bounds = replace(metadata, chunks=tuple(chunks))
        with patch(
            "openbfme_importer.w3d_decode_plan.scan_w3d_metadata",
            return_value=bad_bounds,
        ):
            plan = plan_w3d_stream_decode(source)
        self.assertIn(
            "invalid-chunk-payload-offset-provenance",
            {item.code for item in plan.terminals},
        )
        self.assertEqual(plan.decoded_streams, ())

        chunks = list(metadata.chunks)
        first_child = chunks[1]
        second_child = chunks[2]
        chunks[2] = replace(
            second_child,
            header_offset=first_child.payload_offset,
            payload_offset=first_child.payload_offset + 8,
        )
        overlap = replace(metadata, chunks=tuple(chunks))
        with patch(
            "openbfme_importer.w3d_decode_plan.scan_w3d_metadata",
            return_value=overlap,
        ):
            plan = plan_w3d_stream_decode(source)
        codes = {item.code for item in plan.terminals}
        self.assertTrue(
            "overlapping-sibling-chunk-provenance" in codes
            or "chunk-id-provenance-mismatch" in codes
        )
        self.assertEqual(plan.decoded_streams, ())

    def test_plan_is_deterministic_immutable_json_ready_and_identifier_free(
        self,
    ) -> None:
        source = _mesh(
            _chunk(0x02, struct.pack("<3f", 1.0, 2.0, 3.0)),
            vertices=1,
        )
        first = plan_w3d_stream_decode(source, "secret/location/alpha.w3d")
        second = plan_w3d_stream_decode(source, "other/location/beta.w3d")

        self.assertEqual(first.plan_sha256, second.plan_sha256)
        self.assertEqual(first.neutral(), second.neutral())
        encoded = json.dumps(first.json_ready(), sort_keys=True)
        self.assertNotIn("secret", encoded.casefold())
        self.assertNotIn("location", encoded.casefold())
        self.assertNotIn("owner.mesh", encoded.casefold())
        self.assertNotIn("identifier", encoded.casefold())
        self.assertEqual(first.source_sha256, hashlib.sha256(source).hexdigest())
        with self.assertRaises(FrozenInstanceError):
            first.stream_decode_complete = False  # type: ignore[misc]


if __name__ == "__main__":
    unittest.main()
