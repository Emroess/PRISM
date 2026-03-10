from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import json
import xml.etree.ElementTree as ET


UNIT_SCALE = {
    "inches": 0.0254,
    "mm": 0.001,
    "m": 1.0,
}

AXIS_TO_QUAT = {
    "Y_to_Z": "0.70710678 0.70710678 0 0",
}


@dataclass(frozen=True)
class HandleDefinition:
    handle_id: str
    mesh_folder: str
    mesh_files: list[str]
    units: str
    cad_axis_to_model_axis: str
    z_offset_mm: float
    rigid_with_shaft: bool

    @property
    def mesh_scale_xyz(self) -> str:
        scale = UNIT_SCALE[self.units]
        return f"{scale} {scale} {scale}"

    @property
    def rotation_quat(self) -> str:
        return AXIS_TO_QUAT[self.cad_axis_to_model_axis]


def _root_dir() -> Path:
    return Path(__file__).resolve().parents[1]


def catalog_path() -> Path:
    return _root_dir() / "config" / "handle_catalog.json"


def _to_handle(entry: dict) -> HandleDefinition:
    required = [
        "handle_id",
        "mesh_folder",
        "mesh_files",
        "units",
        "cad_axis_to_model_axis",
        "z_offset_mm",
        "rigid_with_shaft",
    ]
    missing = [key for key in required if key not in entry]
    if missing:
        raise ValueError(f"Missing handle fields for entry: {missing}")

    units = entry["units"]
    if units not in UNIT_SCALE:
        raise ValueError(f"Unsupported units: {units}")

    axis_map = entry["cad_axis_to_model_axis"]
    if axis_map not in AXIS_TO_QUAT:
        raise ValueError(f"Unsupported axis mapping: {axis_map}")

    mesh_files = entry["mesh_files"]
    if not isinstance(mesh_files, list) or len(mesh_files) == 0:
        raise ValueError("mesh_files must be a non-empty list")

    return HandleDefinition(
        handle_id=str(entry["handle_id"]),
        mesh_folder=str(entry["mesh_folder"]),
        mesh_files=[str(v) for v in mesh_files],
        units=str(units),
        cad_axis_to_model_axis=str(axis_map),
        z_offset_mm=float(entry["z_offset_mm"]),
        rigid_with_shaft=bool(entry["rigid_with_shaft"]),
    )


def load_handle_catalog(path: Path | None = None) -> tuple[dict[str, HandleDefinition], str]:
    path = path or catalog_path()
    payload = json.loads(path.read_text(encoding="utf-8"))
    handles_raw = payload.get("handles", [])
    if not isinstance(handles_raw, list) or len(handles_raw) == 0:
        raise ValueError("Handle catalog must contain at least one handle")

    handles = {}
    for entry in handles_raw:
        handle = _to_handle(entry)
        if handle.handle_id in handles:
            raise ValueError(f"Duplicate handle_id: {handle.handle_id}")
        handles[handle.handle_id] = handle

    default_handle_id = str(payload.get("default_handle_id", ""))
    if default_handle_id not in handles:
        raise ValueError("default_handle_id missing from handle definitions")

    return handles, default_handle_id


def validate_handle_assets(handle: HandleDefinition, assets_root: Path | None = None) -> list[str]:
    assets_root = assets_root or (_root_dir() / "assets")
    errors: list[str] = []
    folder = assets_root / handle.mesh_folder
    if not folder.exists():
        return [f"missing_mesh_folder:{folder}"]

    for mesh_file in handle.mesh_files:
        mesh_path = folder / mesh_file
        if not mesh_path.exists():
            errors.append(f"missing_mesh_file:{mesh_path}")
    return errors


def _find_body_by_name(root: ET.Element, name: str) -> ET.Element | None:
    for body in root.findall(".//body"):
        if body.get("name") == name:
            return body
    return None


def _replace_handle_assets(root: ET.Element, handle: HandleDefinition) -> list[str]:
    asset = root.find("asset")
    if asset is None:
        raise ValueError("Model missing <asset> section")

    for mesh in list(asset.findall("mesh")):
        mesh_name = mesh.get("name", "")
        if mesh_name.startswith("blue_handle_") or mesh_name.startswith("handle_mesh_"):
            asset.remove(mesh)

    mesh_names: list[str] = []
    for idx, mesh_file in enumerate(handle.mesh_files):
        mesh_name = f"handle_mesh_{idx}"
        ET.SubElement(
            asset,
            "mesh",
            {
                "name": mesh_name,
                "file": f"{handle.mesh_folder}/{mesh_file}",
                "scale": handle.mesh_scale_xyz,
            },
        )
        mesh_names.append(mesh_name)
    return mesh_names


def _shaft_world_z_from_model(root: ET.Element) -> float:
    shaft_body = _find_body_by_name(root, "prism_shaft")
    if shaft_body is None:
        raise ValueError("Model missing body prism_shaft")
    pos = shaft_body.get("pos", "0 0 0").split()
    if len(pos) != 3:
        return 0.0
    return float(pos[2])


def _replace_handle_body(root: ET.Element, handle: HandleDefinition, mesh_names: list[str]) -> None:
    handle_body = _find_body_by_name(root, "blue_handle")
    if handle_body is None:
        raise ValueError("Model missing body blue_handle")

    for child in list(handle_body):
        handle_body.remove(child)

    shaft_world_z = _shaft_world_z_from_model(root)
    local_z = (handle.z_offset_mm / 1000.0) - shaft_world_z
    handle_body.set("pos", f"0 0 {local_z:.6f}")

    for idx, mesh_name in enumerate(mesh_names):
        vis_name = f"{handle.handle_id}_visual_{idx}"
        col_name = f"{handle.handle_id}_collision_{idx}"
        vis_attrs = {
            "name": vis_name,
            "class": "visual",
            "mesh": mesh_name,
            "quat": handle.rotation_quat,
        }
        if "outer" in handle.mesh_files[idx].lower():
            vis_attrs["rgba"] = "0.10 0.35 0.95 1"

        ET.SubElement(handle_body, "geom", vis_attrs)
        ET.SubElement(
            handle_body,
            "geom",
            {
                "name": col_name,
                "class": "collision",
                "mesh": mesh_name,
                "quat": handle.rotation_quat,
            },
        )


def build_model_for_handle(
    base_model_path: Path,
    output_model_path: Path,
    handle: HandleDefinition,
    prism_mount_pos: tuple[float, float, float] | None = None,
    prism_mount_quat: tuple[float, float, float, float] | None = None,
) -> Path:
    tree = ET.parse(base_model_path)
    root = tree.getroot()

    compiler = root.find("compiler")
    if compiler is not None:
        compiler.set("meshdir", str((_root_dir() / "assets").resolve()))

    mesh_names = _replace_handle_assets(root, handle)
    _replace_handle_body(root, handle, mesh_names)

    prism_mount = _find_body_by_name(root, "prism_mount")
    if prism_mount is not None:
        if prism_mount_pos is not None:
            prism_mount.set("pos", f"{prism_mount_pos[0]} {prism_mount_pos[1]} {prism_mount_pos[2]}")
        if prism_mount_quat is not None:
            prism_mount.set(
                "quat",
                f"{prism_mount_quat[0]} {prism_mount_quat[1]} {prism_mount_quat[2]} {prism_mount_quat[3]}",
            )

    output_model_path.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output_model_path, encoding="utf-8", xml_declaration=False)
    return output_model_path
