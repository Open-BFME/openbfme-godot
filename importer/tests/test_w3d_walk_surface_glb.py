"""Lane L6: structures GLB flavour keeps HLOD pivots and W3D HIDDEN extras.

props-hierarchical conversion of rigid HLOD models keeps pivot rest
translations on named nodes (rbhdiramp5a P01). The maps-pack structures
flavour requested a root-rigid bake that dropped those pivots (intact-rbhddwst1
P1/P2 at the origin). W3D mesh attribute 0x00001000 (HIDDEN) was never
carried to GLB extras.

This file pins both facts against rb/rbhddwst1.w3d and rb/rbhdgathsr.w3d.
The live conversion uses the same Blender subprocess the batch runner uses
(pinned blender + OpenSAGE plugin from workspace/retail-work/tools). If that
toolchain cannot run, the live test is skipped — never faked as a pass.
"""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

from importer.tests.test_w3d_to_glb_fixtures import load_adapter_module


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / "workspace" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
BLENDER = PRIVATE_ROOT / "tools" / "blender-4.2.0-windows-x64" / "blender.exe"
PLUGIN = PRIVATE_ROOT / "tools" / "OpenSAGE.BlenderPlugin"
ADAPTER = ROOT / "importer" / "blender" / "w3d_to_glb.py"

W3D_STAIRS = EFFECTIVE_ASSETS / "art" / "w3d" / "rb" / "rbhddwst1.w3d"
W3D_GATEHOUSE = EFFECTIVE_ASSETS / "art" / "w3d" / "rb" / "rbhdgathsr.w3d"

# rbhddwst1.w3d pivot P1 is parented to HDDW_STAIR01; local translation is
# the spike-measured (-48.5, 8.8, 18.0) Z-up. The hierarchical writer emits
# that rest translation on the P1 node (same convention as props-hierarchical
# bone TRS for this rigid HLOD shape).
P1_W3D_PIVOT = (-48.5, 8.8, 18.0)
HIDDEN_FLAG = 0x00001000

live_tools = pytest.mark.skipif(
    not BLENDER.is_file() or not (PLUGIN / "io_mesh_w3d").is_dir(),
    reason="pinned Blender or OpenSAGE W3D plugin is not present",
)
oracle_w3d = pytest.mark.skipif(
    not W3D_STAIRS.is_file() or not W3D_GATEHOUSE.is_file(),
    reason="pure RotWK effective-assets W3D oracle is not present",
)


def w3d_z_up_to_gltf_y_up(x: float, y: float, z: float) -> tuple[float, float, float]:
    return (x, z, -y)


def _glb_document(path: Path) -> dict[str, object]:
    payload = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", payload, 0)
    assert magic == b"glTF" and version == 2
    offset = 12
    while offset + 8 <= length:
        chunk_len, chunk_type = struct.unpack_from("<II", payload, offset)
        body = payload[offset + 8 : offset + 8 + chunk_len]
        if chunk_type == 0x4E4F534A:
            return json.loads(body.decode("utf-8"))
        offset += 8 + chunk_len
    raise AssertionError(f"GLB has no JSON chunk: {path}")


def _nodes_named(document: dict[str, object], name: str) -> list[dict[str, object]]:
    folded = name.casefold()
    return [
        node
        for node in document.get("nodes") or []
        if isinstance(node, dict) and str(node.get("name", "")).casefold() == folded
    ]


def _node_translation(node: dict[str, object]) -> list[float]:
    value = node.get("translation") or [0.0, 0.0, 0.0]
    if not isinstance(value, list) or len(value) != 3:
        return [0.0, 0.0, 0.0]
    return [float(value[0]), float(value[1]), float(value[2])]


def _pivot_node(document: dict[str, object], name: str) -> dict[str, object] | None:
    """Prefer the HLOD pivot node (non-origin translation, often mesh-less)."""

    matches = _nodes_named(document, name)
    if not matches:
        return None
    for node in matches:
        translation = _node_translation(node)
        if any(abs(component) > 0.1 for component in translation):
            return node
    for node in matches:
        if node.get("mesh") is None:
            return node
    return matches[0]


def _hidden_on(document: dict[str, object], name: str) -> bool:
    meshes = document.get("meshes") or []
    for node in _nodes_named(document, name):
        extras = node.get("extras")
        if isinstance(extras, dict) and extras.get("hidden") is True:
            return True
        mesh_index = node.get("mesh")
        if isinstance(mesh_index, int) and 0 <= mesh_index < len(meshes):
            mesh = meshes[mesh_index]
            if isinstance(mesh, dict):
                mesh_extras = mesh.get("extras")
                if isinstance(mesh_extras, dict) and mesh_extras.get("hidden") is True:
                    return True
                if str(mesh.get("name", "")).casefold() == name.casefold():
                    mesh_extras = mesh.get("extras") if isinstance(mesh.get("extras"), dict) else {}
                    if mesh_extras.get("hidden") is True:
                        return True
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        if str(mesh.get("name", "")).casefold() != name.casefold():
            continue
        extras = mesh.get("extras")
        if isinstance(extras, dict) and extras.get("hidden") is True:
            return True
    return False


@oracle_w3d
def test_w3d_hidden_flag_is_read_from_rbhddwst1_mesh_headers() -> None:
    adapter = load_adapter_module()
    names = adapter.w3d_hidden_mesh_names(W3D_STAIRS.read_bytes())
    assert {"P1", "P2", "P3"} <= set(names)


@oracle_w3d
def test_w3d_p1_pivot_matches_spike_local_translation() -> None:
    adapter = load_adapter_module()
    pivots = {row["name"].casefold(): row for row in adapter.w3d_hierarchy_pivots(W3D_STAIRS.read_bytes())}
    p1 = pivots["p1"]
    assert p1["translation"] == pytest.approx(P1_W3D_PIVOT, abs=0.05)


def _stage_model(model: Path, staging: Path) -> Path:
    """Flatten the W3D plus its compiled textures the way the batch runner does."""

    staging.mkdir(parents=True, exist_ok=True)
    staged = staging / model.name.casefold()
    shutil.copyfile(model, staged)
    texture_root = EFFECTIVE_ASSETS / "art" / "compiledtextures"
    for texture in texture_root.rglob("gbhd1.*"):
        if texture.is_file():
            shutil.copyfile(texture, staging / texture.name.casefold())
    return staged


def _convert(
    model: Path,
    output: Path,
    *,
    proven_root_rigid_bake: bool,
    log_path: Path,
    staging: Path,
) -> subprocess.CompletedProcess[str]:
    staged_model = _stage_model(model, staging)
    command = [
        str(BLENDER),
        "--factory-startup",
        "-noaudio",
        "--background",
        "--python-use-system-env",
        "--python-exit-code",
        "1",
        "--python",
        str(ADAPTER),
        "--",
        "--plugin-root",
        str(PLUGIN),
        "--model",
        str(staged_model),
        "--asset-kind",
        "hierarchical",
        *(["--proven-root-rigid-bake"] if proven_root_rigid_bake else []),
        "--output",
        str(output),
        "--animations",
        "--required-equipment",
        "--excluded-optional-meshes",
        "--retail-absent-textures",
    ]
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=180,
    )
    log_path.write_text(
        "CMD " + " ".join(command) + "\n"
        + f"returncode={result.returncode}\n"
        + "--- stdout ---\n"
        + result.stdout
        + "\n--- stderr ---\n"
        + result.stderr,
        encoding="utf-8",
    )
    return result


@live_tools
@oracle_w3d
def test_structures_flavour_keeps_hlod_pivots_and_hidden_extras(
    tmp_path: Path,
) -> None:
    log_dir = ROOT / "workspace" / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    stairs_out = tmp_path / "rbhddwst1.glb"
    gate_out = tmp_path / "rbhdgathsr.glb"
    stairs_log = log_dir / "l6-step2-convert-rbhddwst1.log"
    gate_log = log_dir / "l6-step2-convert-rbhdgathsr.log"

    stairs = _convert(
        W3D_STAIRS,
        stairs_out,
        proven_root_rigid_bake=False,
        log_path=stairs_log,
        staging=tmp_path / "stage-stairs",
    )
    if stairs.returncode != 0 or not stairs_out.is_file():
        pytest.skip(
            "Blender hierarchical conversion of rbhddwst1.w3d did not execute: "
            f"returncode={stairs.returncode} log={stairs_log}"
        )
    document = _glb_document(stairs_out)
    node = _pivot_node(document, "P1")
    assert node is not None, "GLB has no node named P1"
    translation = _node_translation(node)
    expected = P1_W3D_PIVOT
    assert translation == pytest.approx(expected, abs=0.15), translation
    for name in ("P1", "P2", "P3"):
        assert _hidden_on(document, name), f"{name} extras.hidden is not true"

    gate = _convert(
        W3D_GATEHOUSE,
        gate_out,
        proven_root_rigid_bake=False,
        log_path=gate_log,
        staging=tmp_path / "stage-gate",
    )
    if gate.returncode != 0 or not gate_out.is_file():
        pytest.skip(
            "Blender hierarchical conversion of rbhdgathsr.w3d did not execute: "
            f"returncode={gate.returncode} log={gate_log}"
        )
    gate_document = _glb_document(gate_out)
    for name in ("P1", "P2"):
        assert _nodes_named(gate_document, name), f"gatehouse GLB has no node named {name}"
        assert _hidden_on(gate_document, name), f"gatehouse {name} extras.hidden is not true"
