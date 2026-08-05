from __future__ import annotations

import contextlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock


def load_converter_module():
    fake_bpy = types.SimpleNamespace()
    previous = sys.modules.get("bpy")
    sys.modules["bpy"] = fake_bpy
    try:
        path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
        spec = importlib.util.spec_from_file_location(
            "openbfme_test_w3d_converter_phase_evidence", path
        )
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load W3D converter fixture")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            sys.modules.pop("bpy", None)
        else:
            sys.modules["bpy"] = previous
    return module


CONVERTER = load_converter_module()

FAILURE_PHASES = (
    "scene-reset",
    "model-import",
    "embedded-model-import",
    "model-file-read",
    "model-chunk-header-read",
    "model-mesh-read",
    "model-hierarchy-read",
    "model-hlod-read",
    "model-animation-read",
    "model-compressed-animation-read",
    "model-box-read",
    "model-dazzle-read",
    "model-hierarchy-dependency-validation",
    "model-scene-collection",
    "model-scene-mesh-create",
    "model-scene-rig-create",
    "model-scene-mesh-bind",
    "model-scene-animation-create",
    "model-animation-setup",
    "model-animation-channel-processing",
    "model-animation-bone-resolution",
    "model-animation-channel-decode",
    "model-animation-keyframe-write",
    "model-animation-action-finalization",
    "model-animation-frame-reset",
    "model-load-complete",
    "model-direct-load-dispatch",
    "model-direct-load-result",
    "model-operator-dispatch",
    "model-operator-result",
    "animation-output-capture-setup",
    "animation-output-capture-restore",
    "animation-output-capture-accounting",
    "model-import-validation",
    "request-validation",
    "rig-validation",
    "rig-resolution",
    "action-validation",
    "geometry-validation",
    "material-validation",
    "additive-material-discovery",
    "additive-material-graph-validation",
    "additive-material-pixel-read",
    "additive-material-alpha-derivation",
    "additive-material-image-duplication",
    "additive-material-pixel-write",
    "additive-material-round-trip",
    "additive-material-alpha-link",
    "presentation-validation",
    "skin-validation",
    "attachment-validation",
    "attachment-canonicalization",
    "mesh-object-type-validation",
    "mesh-helper-filter-validation",
    "mesh-box-ambiguity-validation",
    "mesh-equipment-classification",
    "required-equipment-validation",
    "render-proof",
    "scene-validation",
    "animation-import",
    "post-animation-validation",
    "animation-sidecar-mesh-strip",
    "attachment-restoration",
    "render-revalidation",
    "generated-image-validation",
    "shader-material-validation",
    "animation-export-preparation",
    "export",
    "glb-validation",
    "report-validation",
)
FAILURE_KINDS = (
    "assertion",
    "memory",
    "timeout",
    "os",
    "key",
    "type",
    "value",
    "runtime",
    "application",
    "control-flow",
)
FIXED_MESSAGE = "W3D conversion failed with sanitized phase evidence"
CHECKPOINT_SEQUENCE = (
    "scene-reset",
    "model-import",
    "model-operator-dispatch",
    "model-operator-result",
    "model-import-validation",
    "scene-validation",
    "request-validation",
    "rig-validation",
    "rig-resolution",
    "action-validation",
    "geometry-validation",
    "skin-validation",
    "material-validation",
    "additive-material-discovery",
    "presentation-validation",
    "skin-validation",
    "attachment-validation",
    "attachment-canonicalization",
    "required-equipment-validation",
    "mesh-object-type-validation",
    "mesh-helper-filter-validation",
    "mesh-box-ambiguity-validation",
    "mesh-equipment-classification",
    "required-equipment-validation",
    "skin-validation",
    "render-proof",
    "animation-import",
    "scene-validation",
    "post-animation-validation",
    "action-validation",
    "animation-sidecar-mesh-strip",
    "attachment-restoration",
    "render-revalidation",
    "material-validation",
    "generated-image-validation",
    "shader-material-validation",
    "animation-export-preparation",
    "export",
    "glb-validation",
    "report-validation",
)


class FixtureApplicationError(Exception):
    pass


class RenderingForbiddenError(Exception):
    def __str__(self) -> str:
        raise AssertionError("exception payload was rendered")

    def __repr__(self) -> str:
        raise AssertionError("exception payload was rendered")


class FakeAnimationOutputLedger:
    def __init__(
        self,
        *,
        suppressed: int = 0,
        replay_success_error: BaseException | None = None,
        replay_failure_error: BaseException | None = None,
    ) -> None:
        self.suppressed = suppressed
        self.replay_success_error = replay_success_error
        self.replay_failure_error = replay_failure_error
        self.success_replays = 0
        self.failure_replays = 0

    @staticmethod
    def capture(operation, *, operation_phase, phase_checkpoint=None):
        del operation_phase
        del phase_checkpoint
        return operation()

    def replay_success(self) -> int:
        self.success_replays += 1
        if self.replay_success_error is not None:
            raise self.replay_success_error
        return self.suppressed

    def replay_failure(self) -> None:
        self.failure_replays += 1
        if self.replay_failure_error is not None:
            raise self.replay_failure_error


class TrackingPhaseCheckpoint:
    def __init__(self) -> None:
        self.phases: list[str] = []

    def set(self, phase: str) -> None:
        self.phases.append(phase)


class FakePhasePixels(list):
    def foreach_set(self, values) -> None:
        self[:] = values


class FakePhaseImage:
    def __init__(self, pixels) -> None:
        self.pixels = FakePhasePixels(pixels)
        self.users = 1
        self.updated = False

    def copy(self):
        return FakePhaseImage(list(self.pixels))

    def update(self) -> None:
        self.updated = True


class FakePhaseSocket:
    def __init__(self, name: str, node) -> None:
        self.name = name
        self.node = node


class FakePhaseNode:
    def __init__(self, node_type: str, *, image=None) -> None:
        self.type = node_type
        self.image = image
        self.inputs = {}
        self.outputs = {}
        if node_type == "TEX_IMAGE":
            self.outputs = {
                name: FakePhaseSocket(name, self) for name in ("Color", "Alpha")
            }
        elif node_type == "BSDF_PRINCIPLED":
            self.inputs = {
                name: FakePhaseSocket(name, self) for name in ("Base Color", "Alpha")
            }


class FakePhaseLink:
    def __init__(self, from_socket, to_socket) -> None:
        self.from_socket = from_socket
        self.to_socket = to_socket
        self.from_node = from_socket.node
        self.to_node = to_socket.node


class FakePhaseLinks(list):
    def new(self, from_socket, to_socket) -> None:
        self.append(FakePhaseLink(from_socket, to_socket))


def make_additive_material():
    image = FakePhaseImage(
        [
            0.0,
            0.0,
            0.0,
            1.0,
            0.8,
            0.4,
            0.2,
            1.0,
        ]
    )
    image_node = FakePhaseNode("TEX_IMAGE", image=image)
    principled = FakePhaseNode("BSDF_PRINCIPLED")
    links = FakePhaseLinks(
        [FakePhaseLink(image_node.outputs["Color"], principled.inputs["Base Color"])]
    )
    return types.SimpleNamespace(
        name="fixture_additive",
        use_nodes=True,
        shader=types.SimpleNamespace(src_blend=1, dest_blend=1),
        node_tree=types.SimpleNamespace(
            nodes=[image_node, principled],
            links=links,
        ),
    )


class StaticConversionHarness:
    def __init__(self) -> None:
        self.mesh = types.SimpleNamespace(type="MESH")
        self.import_model = mock.Mock(return_value={"FINISHED"})
        self.export_scene = mock.Mock(return_value={"FINISHED"})
        self.bpy = types.SimpleNamespace(
            data=types.SimpleNamespace(
                objects=[self.mesh],
                actions=[],
                materials=[],
                images=[],
            ),
            ops=types.SimpleNamespace(
                import_mesh=types.SimpleNamespace(westwood_w3d=self.import_model),
                export_scene=types.SimpleNamespace(gltf=self.export_scene),
            ),
            context=types.SimpleNamespace(
                scene=types.SimpleNamespace(render=types.SimpleNamespace(fps=30))
            ),
        )
        self.reset_scene = mock.Mock()
        self.validate_request = mock.Mock(wraps=CONVERTER.validate_asset_kind_request)
        self.find_static_rig = mock.Mock(return_value=None)
        self.find_single_rig = mock.Mock(return_value=None)
        self.find_model_rig = mock.Mock(
            side_effect=lambda asset_kind: (
                self.find_static_rig()
                if asset_kind == "static"
                else self.find_single_rig()
            )
        )
        self.assert_non_animated = mock.Mock()
        self.remove_geometry = mock.Mock(return_value=[])

        def convert_materials(_materials, *, phase_checkpoint=None):
            if phase_checkpoint is not None:
                phase_checkpoint.set("additive-material-discovery")
            return {}

        self.convert_materials = mock.Mock(side_effect=convert_materials)
        self.exclude_optional = mock.Mock(return_value=[])
        self.bake_root = mock.Mock()
        self.canonicalize_attachments = mock.Mock()

        def build_inventory(
            _mesh_objects,
            _required_equipment,
            _rig=None,
            *,
            phase_checkpoint=None,
        ):
            if phase_checkpoint is not None:
                for phase in (
                    "required-equipment-validation",
                    "mesh-object-type-validation",
                    "mesh-helper-filter-validation",
                    "mesh-box-ambiguity-validation",
                    "mesh-equipment-classification",
                    "required-equipment-validation",
                ):
                    phase_checkpoint.set(phase)
            return (
                [{"vertices": 3, "triangles": 1, "skinned": False}],
                {},
            )

        self.build_inventory = mock.Mock(side_effect=build_inventory)
        self.capture_geometry = mock.Mock(return_value=[])
        self.capture_attachments = mock.Mock(return_value=[])
        self.capture_animation = mock.Mock()
        self.detach_actions = mock.Mock()
        self.restore_attachments = mock.Mock()
        self.assert_render_geometry = mock.Mock()
        self.revalidate_inventory = mock.Mock()
        self.collect_shader_materials = mock.Mock(return_value=[])
        self.prepare_animation_export = mock.Mock(return_value=0)
        self.restore_duplicate_animations = mock.Mock()
        self.validate_split_animation = mock.Mock()

    @contextlib.contextmanager
    def patched(self):
        replacements = {
            "reset_w3d_conversion_scene": self.reset_scene,
            "validate_asset_kind_request": self.validate_request,
            "find_static_rig": self.find_static_rig,
            "find_single_rig": self.find_single_rig,
            "find_model_rig": self.find_model_rig,
            "assert_non_animated_scene_has_no_actions": self.assert_non_animated,
            "remove_non_render_geometry": self.remove_geometry,
            "convert_proven_additive_materials": self.convert_materials,
            "exclude_optional_render_meshes": self.exclude_optional,
            "bake_proven_root_rigid_hierarchy": self.bake_root,
            "canonicalize_required_rigid_attachments": (self.canonicalize_attachments),
            "build_mesh_inventory": self.build_inventory,
            "capture_render_geometry_proof": self.capture_geometry,
            "capture_render_attachment_proof": self.capture_attachments,
            "capture_w3d_animation_actions": self.capture_animation,
            "detach_actions": self.detach_actions,
            "restore_render_attachments": self.restore_attachments,
            "assert_render_geometry_unchanged": self.assert_render_geometry,
            "revalidate_restored_inventory": self.revalidate_inventory,
            "collect_shader_material_compatibility": self.collect_shader_materials,
            "prepare_w3d_animation_nla_tracks": self.prepare_animation_export,
            "restore_duplicate_logical_animations": (self.restore_duplicate_animations),
            "validate_split_animation_glb": self.validate_split_animation,
        }
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(CONVERTER, "bpy", self.bpy))
            for name, replacement in replacements.items():
                stack.enter_context(mock.patch.object(CONVERTER, name, replacement))
            yield

    @staticmethod
    def paths(root: Path) -> tuple[Path, Path]:
        model = root / "fixture.w3d"
        model.write_bytes(b"fixture")
        return model, root / "output" / "fixture.glb"


class W3dConverterPhaseEvidenceTests(unittest.TestCase):
    @staticmethod
    def _fake_pinned_importer_modules(*, load_result={"FINISHED"}):
        def complete(*_args, **_kwargs):
            return None

        def load(*_args, **_kwargs):
            return load_result

        def reader_type(name: str):
            return type(name, (), {"read": staticmethod(complete)})

        class CompressedAnimation:
            pass

        animation_import = types.SimpleNamespace(
            create_animation=complete,
            setup_animation=complete,
            process_channels=complete,
            process_motion_channels=complete,
            get_bone=complete,
            apply_timecoded=complete,
            apply_motion_channel_time_coded=complete,
            apply_motion_channel_adaptive_delta=complete,
            apply_adaptive_delta=complete,
            apply_uncompressed=complete,
            set_translation=complete,
            set_rotation=complete,
            set_visibility=complete,
            CompressedAnimation=CompressedAnimation,
            bpy=types.SimpleNamespace(
                context=types.SimpleNamespace(
                    scene=types.SimpleNamespace(frame_set=complete)
                )
            ),
        )

        import_w3d = types.SimpleNamespace(
            load=load,
            load_file=complete,
            read_chunk_head=complete,
            create_data=complete,
            Mesh=reader_type("Mesh"),
            Hierarchy=reader_type("Hierarchy"),
            HLod=reader_type("HLod"),
            Animation=reader_type("Animation"),
            CompressedAnimation=reader_type("CompressedAnimation"),
            CollisionBox=reader_type("CollisionBox"),
            Dazzle=reader_type("Dazzle"),
        )
        import_utils = types.SimpleNamespace(
            **{
                name: complete
                for name in (
                    "get_collection",
                    "create_mesh",
                    "create_box",
                    "create_dazzle",
                    "get_or_create_skeleton",
                    "rig_mesh",
                    "rig_box",
                    "rig_object",
                )
            },
            create_animation=animation_import.create_animation,
        )
        return import_w3d, import_utils, animation_import

    @staticmethod
    def _invoke_failure(
        phase: str,
        error: BaseException,
        *,
        replay_failure_error: BaseException | None = None,
    ):
        ledger = FakeAnimationOutputLedger(replay_failure_error=replay_failure_error)

        def fail(**kwargs):
            kwargs["phase_checkpoint"].set(phase)
            raise error

        with (
            mock.patch.object(
                CONVERTER, "AnimationImportOutputLedger", return_value=ledger
            ),
            mock.patch.object(CONVERTER, "_convert_w3d_job_impl", side_effect=fail),
        ):
            with unittest.TestCase().assertRaises(
                CONVERTER.W3DConversionPhaseError
            ) as raised:
                CONVERTER.convert_w3d_job(
                    model=Path("private-model.w3d"),
                    asset_kind="animated",
                    animations=[Path("private-animation.w3d")],
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=False,
                    output=Path("private-output.glb"),
                )
        return raised.exception, ledger

    def assert_sanitized_error(
        self,
        error,
        *,
        failure_phase: str,
        failure_kind: str,
        secret: str,
    ) -> None:
        self.assertIs(type(error), CONVERTER.W3DConversionPhaseError)
        self.assertEqual(error.failure_phase, failure_phase)
        self.assertEqual(error.failure_kind, failure_kind)
        self.assertEqual(error.args, (FIXED_MESSAGE,))
        self.assertEqual(str(error), FIXED_MESSAGE)
        self.assertNotIn(secret, repr(error))
        self.assertIsNone(error.__cause__)
        self.assertIsNone(error.__context__)
        self.assertTrue(error.__suppress_context__)

    def _invoke_real_failure(
        self,
        harness: StaticConversionHarness,
        root: Path,
        *,
        failure_phase: str,
        secret: str,
        asset_kind: str = "static",
        embedded_animation: bool = False,
        proven_root_rigid_bake: bool = False,
    ) -> None:
        model, output = harness.paths(root)
        animations = [model] if embedded_animation else []
        ledger = FakeAnimationOutputLedger()
        with (
            harness.patched(),
            mock.patch.object(
                CONVERTER, "AnimationImportOutputLedger", return_value=ledger
            ),
        ):
            with self.assertRaises(CONVERTER.W3DConversionPhaseError) as raised:
                CONVERTER.convert_w3d_job(
                    model=model,
                    asset_kind=asset_kind,
                    animations=animations,
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=proven_root_rigid_bake,
                    output=output,
                )
        self.assert_sanitized_error(
            raised.exception,
            failure_phase=failure_phase,
            failure_kind="runtime",
            secret=secret,
        )
        self.assertEqual(ledger.success_replays, 0)
        self.assertEqual(ledger.failure_replays, 1)

    def test_real_static_conversion_visits_exact_operation_boundaries(self) -> None:
        harness = StaticConversionHarness()
        checkpoint = TrackingPhaseCheckpoint()
        ledger = FakeAnimationOutputLedger()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            model, output = harness.paths(root)
            with harness.patched():
                report = CONVERTER._convert_w3d_job_impl(
                    model=model,
                    asset_kind="static",
                    animations=[],
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=False,
                    output=output,
                    animation_output_ledger=ledger,
                    phase_checkpoint=checkpoint,
                )

        self.assertEqual(tuple(checkpoint.phases), CHECKPOINT_SEQUENCE)
        self.assertEqual(report["report_schema"], "openbfme.w3d-adapter-report")
        self.assertEqual(report["report_version"], 2)
        self.assertEqual(report["meshes"], 1)
        self.assertEqual(report["animations"], 0)

    def test_mesh_inventory_real_boundaries_preserve_optional_none_behavior(
        self,
    ) -> None:
        mesh_data = types.SimpleNamespace(
            name="fixture_mesh_data",
            vertices=[object(), object(), object()],
            loop_triangles=[object()],
            materials=[],
            calc_loop_triangles=mock.Mock(),
        )
        mesh = types.SimpleNamespace(name="fixture_mesh", data=mesh_data)
        checkpoint = TrackingPhaseCheckpoint()
        patches = (
            mock.patch.object(
                CONVERTER,
                "_w3d_object_type",
                return_value=CONVERTER.RENDERABLE_W3D_OBJECT_TYPE,
            ),
            mock.patch.object(CONVERTER, "_non_render_reasons", return_value=[]),
            mock.patch.object(CONVERTER, "_is_box_geometry", return_value=False),
            mock.patch.object(
                CONVERTER,
                "_equipment_classification",
                return_value=("character-mesh", "scene", []),
            ),
            mock.patch.object(CONVERTER, "_is_skinned", return_value=False),
        )
        with contextlib.ExitStack() as stack:
            for patch in patches:
                stack.enter_context(patch)
            without_checkpoint = CONVERTER.build_mesh_inventory([mesh], [])
            with_checkpoint = CONVERTER.build_mesh_inventory(
                [mesh], [], phase_checkpoint=checkpoint
            )

        self.assertEqual(with_checkpoint, without_checkpoint)
        self.assertEqual(
            tuple(checkpoint.phases),
            (
                "required-equipment-validation",
                "mesh-object-type-validation",
                "mesh-helper-filter-validation",
                "mesh-box-ambiguity-validation",
                "mesh-equipment-classification",
                "required-equipment-validation",
            ),
        )

    def test_additive_material_real_boundaries_preserve_optional_none_behavior(
        self,
    ) -> None:
        without_checkpoint = CONVERTER.convert_proven_additive_materials(
            [make_additive_material()]
        )
        checkpoint = TrackingPhaseCheckpoint()
        with_checkpoint = CONVERTER.convert_proven_additive_materials(
            [make_additive_material()], phase_checkpoint=checkpoint
        )

        self.assertEqual(with_checkpoint, without_checkpoint)
        self.assertEqual(
            tuple(checkpoint.phases),
            (
                "additive-material-discovery",
                "additive-material-graph-validation",
                "additive-material-pixel-read",
                "additive-material-alpha-derivation",
                "additive-material-image-duplication",
                "additive-material-pixel-write",
                "additive-material-round-trip",
                "additive-material-alpha-link",
            ),
        )

    def test_real_helper_failures_report_the_active_boundary(self) -> None:
        cases = (
            ("scene-reset", "reset_scene"),
            ("model-operator-dispatch", "import_model"),
            ("request-validation", "validate_request"),
            ("rig-resolution", "find_static_rig"),
            ("geometry-validation", "remove_geometry"),
            ("material-validation", "convert_materials"),
            ("presentation-validation", "exclude_optional"),
            ("render-proof", "capture_geometry"),
            ("render-revalidation", "assert_render_geometry"),
            ("shader-material-validation", "collect_shader_materials"),
            ("export", "export_scene"),
        )
        for index, (phase, operation) in enumerate(cases):
            with self.subTest(phase=phase, operation=operation):
                secret = f"PRIVATE_BOUNDARY_{index}"
                harness = StaticConversionHarness()
                getattr(harness, operation).side_effect = RuntimeError(secret)
                with tempfile.TemporaryDirectory() as raw:
                    self._invoke_real_failure(
                        harness,
                        Path(raw),
                        failure_phase=phase,
                        secret=secret,
                    )

    def test_real_validation_and_restoration_failures_report_new_phases(self) -> None:
        secret = "PRIVATE_MODEL_RESULT"
        harness = StaticConversionHarness()
        harness.import_model.return_value = {"CANCELLED"}
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="model-import-validation",
                secret=secret,
            )

        secret = "PRIVATE_EMPTY_RIG"
        harness = StaticConversionHarness()
        harness.find_static_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[])
        )
        harness.build_inventory.side_effect = lambda *_args, **_kwargs: (
            [{"vertices": 3, "triangles": 1, "skinned": True}],
            {},
        )
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="skin-validation",
                secret=secret,
            )

        # An empty carrier with no skinned meshes is a legitimate rigid shape:
        # the same harness must now convert instead of failing.
        harness = StaticConversionHarness()
        harness.find_static_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[])
        )
        checkpoint = TrackingPhaseCheckpoint()
        ledger = FakeAnimationOutputLedger()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            model, output = harness.paths(root)
            with harness.patched():
                report = CONVERTER._convert_w3d_job_impl(
                    model=model,
                    asset_kind="static",
                    animations=[],
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=False,
                    output=output,
                    animation_output_ledger=ledger,
                    phase_checkpoint=checkpoint,
                )
        self.assertEqual(report["meshes"], 1)
        self.assertEqual(report["bones"], 0)
        self.assertEqual(checkpoint.phases.count("skin-validation"), 3)

        secret = "PRIVATE_ROOT_BAKE"
        harness = StaticConversionHarness()
        harness.find_single_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[object()])
        )
        harness.bake_root.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="skin-validation",
                secret=secret,
                asset_kind="hierarchical",
                proven_root_rigid_bake=True,
            )

        secret = "PRIVATE_POST_ANIMATION"
        harness = StaticConversionHarness()
        harness.assert_non_animated.side_effect = [None, RuntimeError(secret)]
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="action-validation",
                secret=secret,
            )

        secret = "PRIVATE_ACTION_VALIDATION"
        harness = StaticConversionHarness()
        harness.assert_non_animated.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="action-validation",
                secret=secret,
            )

        secret = "PRIVATE_ATTACHMENT_CANONICALIZATION"
        harness = StaticConversionHarness()
        harness.find_static_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[object()])
        )
        harness.canonicalize_attachments.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="attachment-canonicalization",
                secret=secret,
            )

        secret = "PRIVATE_ATTACHMENT_RESTORE"
        harness = StaticConversionHarness()
        harness.find_static_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[object()])
        )
        harness.restore_attachments.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="attachment-restoration",
                secret=secret,
            )

    def test_real_animation_boundaries_fail_closed_without_retail_data(self) -> None:
        secret = "PRIVATE_EMBEDDED_MODEL_IMPORT"
        harness = StaticConversionHarness()
        harness.import_model.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="model-operator-dispatch",
                secret=secret,
                asset_kind="animated",
                embedded_animation=True,
            )

        secret = "PRIVATE_ANIMATION_IMPORT"
        harness = StaticConversionHarness()
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="animation-import",
                secret=secret,
                asset_kind="animated",
                embedded_animation=True,
            )

        action = types.SimpleNamespace(fcurves=[])
        action_shape = {
            "public": {
                "object_action_count": 1,
                "armature_action_count": 1,
            }
        }

        secret = "PRIVATE_ANIMATION_PREP"
        harness = StaticConversionHarness()
        harness.find_single_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[object()])
        )
        harness.capture_animation.return_value = ([action], action_shape)
        harness.prepare_animation_export.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="animation-export-preparation",
                secret=secret,
                asset_kind="animated",
                embedded_animation=True,
            )

        secret = "PRIVATE_GLB_VALIDATION"
        harness = StaticConversionHarness()
        harness.find_single_rig.return_value = types.SimpleNamespace(
            data=types.SimpleNamespace(bones=[object()])
        )
        harness.capture_animation.return_value = ([action], action_shape)
        harness.restore_duplicate_animations.side_effect = RuntimeError(secret)
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="glb-validation",
                secret=secret,
                asset_kind="animated",
                embedded_animation=True,
            )

    def test_real_report_construction_uses_report_validation_phase(self) -> None:
        secret = "PRIVATE_REPORT_BUILD"

        class FailingRender:
            @property
            def fps(self):
                raise RuntimeError(secret)

        harness = StaticConversionHarness()
        harness.bpy.context.scene.render = FailingRender()
        with tempfile.TemporaryDirectory() as raw:
            self._invoke_real_failure(
                harness,
                Path(raw),
                failure_phase="report-validation",
                secret=secret,
            )

    def test_pinned_importer_phase_scope_maps_calls_and_restores_identities(
        self,
    ) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()
        calls = (
            (import_w3d, "load_file", "model-file-read"),
            (import_w3d, "read_chunk_head", "model-chunk-header-read"),
            (import_w3d.Mesh, "read", "model-mesh-read"),
            (import_w3d.Hierarchy, "read", "model-hierarchy-read"),
            (import_w3d.HLod, "read", "model-hlod-read"),
            (import_w3d.Animation, "read", "model-animation-read"),
            (
                import_w3d.CompressedAnimation,
                "read",
                "model-compressed-animation-read",
            ),
            (import_w3d.CollisionBox, "read", "model-box-read"),
            (import_w3d.Dazzle, "read", "model-dazzle-read"),
            (import_w3d, "create_data", "model-scene-collection"),
            (import_utils, "get_collection", "model-scene-collection"),
            (import_utils, "create_mesh", "model-scene-mesh-create"),
            (import_utils, "create_box", "model-scene-mesh-create"),
            (import_utils, "create_dazzle", "model-scene-mesh-create"),
            (import_utils, "get_or_create_skeleton", "model-scene-rig-create"),
            (import_utils, "rig_mesh", "model-scene-mesh-bind"),
            (import_utils, "rig_box", "model-scene-mesh-bind"),
            (import_utils, "rig_object", "model-scene-mesh-bind"),
            (animation_import, "setup_animation", "model-animation-setup"),
            (
                animation_import,
                "process_channels",
                "model-animation-channel-processing",
            ),
            (
                animation_import,
                "process_motion_channels",
                "model-animation-channel-processing",
            ),
            (animation_import, "get_bone", "model-animation-bone-resolution"),
            (
                animation_import,
                "apply_timecoded",
                "model-animation-channel-decode",
            ),
            (
                animation_import,
                "apply_motion_channel_time_coded",
                "model-animation-channel-decode",
            ),
            (
                animation_import,
                "apply_motion_channel_adaptive_delta",
                "model-animation-channel-decode",
            ),
            (
                animation_import,
                "apply_adaptive_delta",
                "model-animation-channel-decode",
            ),
            (
                animation_import,
                "apply_uncompressed",
                "model-animation-channel-decode",
            ),
            (
                animation_import,
                "set_translation",
                "model-animation-keyframe-write",
            ),
            (
                animation_import,
                "set_rotation",
                "model-animation-keyframe-write",
            ),
            (
                animation_import,
                "set_visibility",
                "model-animation-keyframe-write",
            ),
        )
        originals = {
            (id(owner), name): getattr(owner, name) for owner, name, _phase in calls
        }
        original_load = import_w3d.load
        original_create_animation = import_utils.create_animation

        with CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        ):
            for owner, name, phase in calls:
                getattr(owner, name)()
                self.assertEqual(checkpoint.failure_phase, phase)
            import_utils.create_animation(None, None, None, None)
            self.assertEqual(
                checkpoint.failure_phase,
                "model-scene-animation-create",
            )
            self.assertEqual(import_w3d.load(), {"FINISHED"})
            self.assertEqual(checkpoint.failure_phase, "model-load-complete")

        for owner, name, _phase in calls:
            self.assertIs(getattr(owner, name), originals[(id(owner), name)])
        self.assertIs(import_w3d.load, original_load)
        self.assertIs(import_utils.create_animation, original_create_animation)

    def test_pinned_scope_invokes_loader_directly_with_silent_context(self) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        observed_contexts = []

        def load(context):
            observed_contexts.append(context)
            self.assertIsNone(context.info("private info"))
            self.assertIsNone(context.warning("private warning"))
            self.assertIsNone(context.error("private error"))
            return {"FINISHED"}

        import_w3d.load = load
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

        with CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        ) as scope:
            self.assertEqual(scope.invoke(Path("private-model.w3d")), {"FINISHED"})
            self.assertEqual(
                checkpoint.failure_phase,
                "model-direct-load-result",
            )

        self.assertIs(import_w3d.load, load)
        self.assertEqual(len(observed_contexts), 1)
        self.assertEqual(observed_contexts[0].file_format, "")
        self.assertEqual(observed_contexts[0].filepath, "private-model.w3d")

    def test_pinned_animation_create_runs_exact_normal_and_compressed_routes(
        self,
    ) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        events: list[tuple[str, object]] = []

        def setup_animation(animation):
            events.append(("setup", animation.header.name))

        def process_channels(_context, _hierarchy, channels, _rig, _apply):
            events.append(("channels", tuple(channels)))

        def process_motion_channels(_context, _hierarchy, channels, _rig):
            events.append(("motion", tuple(channels)))

        def frame_set(frame):
            events.append(("frame", frame))

        animation_import.setup_animation = setup_animation
        animation_import.process_channels = process_channels
        animation_import.process_motion_channels = process_motion_channels
        animation_import.bpy.context.scene.frame_set = frame_set
        normal = types.SimpleNamespace(
            channels=("normal",),
            header=types.SimpleNamespace(name="Normal"),
        )
        compressed = animation_import.CompressedAnimation()
        compressed.header = types.SimpleNamespace(name="Compressed")
        compressed.time_coded_channels = ("time",)
        compressed.adaptive_delta_channels = ("adaptive",)
        compressed.motion_channels = ("motion",)
        action = types.SimpleNamespace(name="")
        rig = types.SimpleNamespace(
            animation_data=types.SimpleNamespace(action=action),
            data=None,
        )
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

        with CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        ):
            import_utils.create_animation(None, rig, normal, None)
            self.assertEqual(action.name, "Normal")
            self.assertEqual(checkpoint.failure_phase, "model-animation-frame-reset")
            import_utils.create_animation(None, rig, compressed, None)

        self.assertEqual(action.name, "Compressed")
        self.assertEqual(
            events,
            [
                ("setup", "Normal"),
                ("channels", ("normal",)),
                ("frame", 0),
                ("setup", "Compressed"),
                ("channels", ("time",)),
                ("channels", ("adaptive",)),
                ("motion", ("motion",)),
                ("frame", 0),
            ],
        )

    def test_pinned_animation_create_captures_outer_terminal_phases(self) -> None:
        for expected_phase in (
            "model-animation-action-finalization",
            "model-animation-frame-reset",
        ):
            with self.subTest(expected_phase=expected_phase):
                import_w3d, import_utils, animation_import = (
                    self._fake_pinned_importer_modules()
                )
                secret = f"PRIVATE_{expected_phase}"
                animation = types.SimpleNamespace(
                    channels=(),
                    header=types.SimpleNamespace(name="Animation"),
                )
                rig = None
                if expected_phase == "model-animation-action-finalization":

                    class FailingRig:
                        @property
                        def animation_data(self):
                            raise RuntimeError(secret)

                    rig = FailingRig()
                else:

                    def fail_frame_set(_frame):
                        raise RuntimeError(secret)

                    animation_import.bpy.context.scene.frame_set = fail_frame_set
                original_create_animation = import_utils.create_animation
                checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

                with CONVERTER._PinnedModelImportPhaseScope(
                    checkpoint,
                    import_w3d_module=import_w3d,
                    import_utils_module=import_utils,
                    animation_import_module=animation_import,
                ) as scope:
                    with self.assertRaisesRegex(RuntimeError, secret):
                        import_utils.create_animation(None, rig, animation, None)

                self.assertIs(import_utils.create_animation, original_create_animation)
                with self.assertRaisesRegex(RuntimeError, "sanitized phase") as raised:
                    scope.raise_if_failed()
                self.assertNotIn(secret, str(raised.exception))
                self.assertEqual(checkpoint.failure_phase, expected_phase)

    def test_pinned_animation_create_preserves_deepest_failure_phase(self) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        secret = "PRIVATE_KEYFRAME_FAILURE"

        def process_channels(_context, _hierarchy, _channels, _rig, apply):
            apply()

        def apply_uncompressed():
            animation_import.set_translation()

        def fail_keyframe_write():
            raise RuntimeError(secret)

        animation_import.process_channels = process_channels
        animation_import.apply_uncompressed = apply_uncompressed
        animation_import.set_translation = fail_keyframe_write
        original_process_channels = animation_import.process_channels
        original_apply_uncompressed = animation_import.apply_uncompressed
        original_set_translation = animation_import.set_translation
        animation = types.SimpleNamespace(
            channels=("channel",),
            header=types.SimpleNamespace(name="Animation"),
        )
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

        with CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        ) as scope:
            with self.assertRaisesRegex(RuntimeError, secret):
                import_utils.create_animation(None, None, animation, None)

        self.assertIs(animation_import.process_channels, original_process_channels)
        self.assertIs(animation_import.apply_uncompressed, original_apply_uncompressed)
        self.assertIs(animation_import.set_translation, original_set_translation)
        with self.assertRaisesRegex(RuntimeError, "sanitized phase") as raised:
            scope.raise_if_failed()
        self.assertNotIn(secret, str(raised.exception))
        self.assertEqual(checkpoint.failure_phase, "model-animation-keyframe-write")

    def test_pinned_scope_exit_reasserts_failure_after_capture_cleanup(self) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        secret = "PRIVATE_IMPORT_FAILURE_BEFORE_CAPTURE_CLEANUP"

        def fail_mesh_create():
            raise RuntimeError(secret)

        import_utils.create_mesh = fail_mesh_create
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

        with self.assertRaisesRegex(RuntimeError, secret):
            with CONVERTER._PinnedModelImportPhaseScope(
                checkpoint,
                import_w3d_module=import_w3d,
                import_utils_module=import_utils,
                animation_import_module=animation_import,
            ):
                try:
                    import_utils.create_mesh()
                finally:
                    checkpoint.set("animation-output-capture-restore")

        self.assertEqual(checkpoint.failure_phase, "model-scene-mesh-create")

    def test_pinned_importer_phase_scope_is_single_use_and_all_or_none(self) -> None:
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()
        scope = CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        )
        with scope:
            pass
        with self.assertRaisesRegex(RuntimeError, "cannot be reused"):
            with scope:
                pass

        partial_checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()
        with self.assertRaisesRegex(RuntimeError, "injected together"):
            with CONVERTER._PinnedModelImportPhaseScope(
                partial_checkpoint,
                import_w3d_module=import_w3d,
            ):
                pass
        self.assertEqual(
            partial_checkpoint.failure_phase,
            "model-import-validation",
        )

    def test_pinned_importer_phase_scope_retains_swallowed_failure_without_payload(
        self,
    ) -> None:
        secret = "PRIVATE_PINNED_IMPORT_FAILURE"
        import_w3d, import_utils, animation_import = (
            self._fake_pinned_importer_modules()
        )
        original = import_utils.create_mesh

        def fail(*_args, **_kwargs):
            raise BaseException(secret)

        import_utils.create_mesh = fail
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()
        with CONVERTER._PinnedModelImportPhaseScope(
            checkpoint,
            import_w3d_module=import_w3d,
            import_utils_module=import_utils,
            animation_import_module=animation_import,
        ) as scope:
            with self.assertRaises(BaseException) as raised:
                import_utils.create_mesh()
            self.assertIn(secret, str(raised.exception))
        self.assertIs(import_utils.create_mesh, fail)
        with self.assertRaisesRegex(RuntimeError, "sanitized phase") as sanitized:
            scope.raise_if_failed()
        self.assertNotIn(secret, str(sanitized.exception))
        self.assertEqual(checkpoint.failure_phase, "model-scene-mesh-create")
        import_utils.create_mesh = original

    def test_pinned_importer_none_result_is_hierarchy_dependency_failure(self) -> None:
        import_w3d, import_utils, animation_import = self._fake_pinned_importer_modules(
            load_result=None
        )
        original_load = import_w3d.load
        checkpoint = CONVERTER._W3DConversionPhaseCheckpoint()

        with self.assertRaisesRegex(RuntimeError, "did not finish"):
            with CONVERTER._PinnedModelImportPhaseScope(
                checkpoint,
                import_w3d_module=import_w3d,
                import_utils_module=import_utils,
                animation_import_module=animation_import,
            ):
                import_w3d.load()

        self.assertEqual(
            checkpoint.failure_phase,
            "model-hierarchy-dependency-validation",
        )
        self.assertIs(import_w3d.load, original_load)

    def test_exact_phase_enum_is_sanitized_and_read_only(self) -> None:
        self.assertIs(
            getattr(CONVERTER, "W3DConversionPhaseError"),
            CONVERTER.W3DConversionPhaseError,
        )
        self.assertEqual(
            CONVERTER.W3DConversionPhaseError.__module__, CONVERTER.__name__
        )
        self.assertEqual(
            CONVERTER.W3DConversionPhaseError.__qualname__,
            "W3DConversionPhaseError",
        )
        self.assertEqual(
            CONVERTER._W3D_CONVERSION_FAILURE_PHASES, frozenset(FAILURE_PHASES)
        )
        secret = "PRIVATE_PHASE_PAYLOAD"
        for phase in FAILURE_PHASES:
            with self.subTest(phase=phase):
                error, ledger = self._invoke_failure(phase, RuntimeError(secret))
                self.assert_sanitized_error(
                    error,
                    failure_phase=phase,
                    failure_kind="runtime",
                    secret=secret,
                )
                self.assertEqual(ledger.success_replays, 0)
                self.assertEqual(ledger.failure_replays, 1)
                with self.assertRaises(AttributeError):
                    error.failure_phase = "export"
                with self.assertRaises(AttributeError):
                    error.failure_kind = "os"
                with self.assertRaises(AttributeError):
                    error._evidence = ("export", "os")

    def test_exact_kind_enum_uses_only_isinstance_categories(self) -> None:
        self.assertEqual(
            CONVERTER._W3D_CONVERSION_FAILURE_KINDS, frozenset(FAILURE_KINDS)
        )
        secret = "PRIVATE_KIND_PAYLOAD"
        cases = (
            (AssertionError(secret), "assertion"),
            (MemoryError(secret), "memory"),
            (TimeoutError(secret), "timeout"),
            (OSError(secret), "os"),
            (KeyError(secret), "key"),
            (TypeError(secret), "type"),
            (ValueError(secret), "value"),
            (RuntimeError(secret), "runtime"),
            (FixtureApplicationError(secret), "application"),
            (BaseException(secret), "control-flow"),
        )
        for source_error, kind in cases:
            with self.subTest(kind=kind):
                error, ledger = self._invoke_failure("model-import", source_error)
                self.assert_sanitized_error(
                    error,
                    failure_phase="model-import",
                    failure_kind=kind,
                    secret=secret,
                )
                self.assertEqual(ledger.failure_replays, 1)

        opaque, _ledger = self._invoke_failure(
            "scene-validation", RenderingForbiddenError()
        )
        self.assertEqual(opaque.failure_kind, "application")

    def test_replay_failure_cannot_replace_or_leak_original_evidence(self) -> None:
        source_secret = "PRIVATE_SOURCE_PAYLOAD"
        replay_secret = "PRIVATE_REPLAY_PAYLOAD"
        error, ledger = self._invoke_failure(
            "animation-import",
            KeyError(source_secret),
            replay_failure_error=RuntimeError(replay_secret),
        )

        self.assert_sanitized_error(
            error,
            failure_phase="animation-import",
            failure_kind="key",
            secret=source_secret,
        )
        self.assertNotIn(replay_secret, repr(error))
        self.assertEqual(ledger.failure_replays, 1)

    def test_success_replay_failure_is_also_sanitized(self) -> None:
        secret = "PRIVATE_SUCCESS_REPLAY_PAYLOAD"
        ledger = FakeAnimationOutputLedger(replay_success_error=MemoryError(secret))

        def succeed(**kwargs):
            kwargs["phase_checkpoint"].set("report-validation")
            return {"report_schema": "openbfme.w3d-adapter-report"}

        with (
            mock.patch.object(
                CONVERTER, "AnimationImportOutputLedger", return_value=ledger
            ),
            mock.patch.object(CONVERTER, "_convert_w3d_job_impl", side_effect=succeed),
        ):
            with self.assertRaises(CONVERTER.W3DConversionPhaseError) as raised:
                CONVERTER.convert_w3d_job(
                    model=Path("unused.w3d"),
                    asset_kind="static",
                    animations=[],
                    required_equipment=[],
                    excluded_optional_meshes=[],
                    proven_root_rigid_bake=False,
                    output=Path("unused.glb"),
                )

        self.assert_sanitized_error(
            raised.exception,
            failure_phase="report-validation",
            failure_kind="memory",
            secret=secret,
        )
        self.assertEqual(ledger.success_replays, 1)
        self.assertEqual(ledger.failure_replays, 1)

    def test_success_preserves_the_established_report_shape(self) -> None:
        ledger = FakeAnimationOutputLedger(suppressed=7)
        established_report = {
            "report_schema": "openbfme.w3d-adapter-report",
            "report_version": 2,
            "asset_kind": "static",
            "meshes": 1,
        }

        def succeed(**kwargs):
            kwargs["phase_checkpoint"].set("report-validation")
            return established_report

        with (
            mock.patch.object(
                CONVERTER, "AnimationImportOutputLedger", return_value=ledger
            ),
            mock.patch.object(CONVERTER, "_convert_w3d_job_impl", side_effect=succeed),
        ):
            report = CONVERTER.convert_w3d_job(
                model=Path("unused.w3d"),
                asset_kind="static",
                animations=[],
                required_equipment=[],
                excluded_optional_meshes=[],
                proven_root_rigid_bake=False,
                output=Path("unused.glb"),
            )

        self.assertIs(report, established_report)
        self.assertEqual(
            report,
            {
                "report_schema": "openbfme.w3d-adapter-report",
                "report_version": 2,
                "asset_kind": "static",
                "meshes": 1,
                "suppressed_redundant_keyframe_warning_count": 7,
            },
        )
        self.assertEqual(ledger.success_replays, 1)
        self.assertEqual(ledger.failure_replays, 0)

    def test_invalid_evidence_fails_closed_without_echoing_values(self) -> None:
        for phase, kind in (
            ("private-phase", "runtime"),
            ("model-import", "private-kind"),
        ):
            with self.subTest(phase=phase, kind=kind):
                with self.assertRaisesRegex(
                    ValueError, "^invalid sanitized W3D conversion failure evidence$"
                ) as raised:
                    CONVERTER.W3DConversionPhaseError(phase, kind)
                self.assertNotIn("private", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
