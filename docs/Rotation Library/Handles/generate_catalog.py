#!/usr/bin/env python3
"""
Generate a Markdown catalog from handles.json.

This script reads the structured handle metadata from handles.json and
generates a formatted Markdown catalog with images, BOM, parts, and links.

Usage:
    python3 generate_catalog.py                 # Write to catalog.md
    python3 generate_catalog.py --output out.md # Write to custom file
    python3 generate_catalog.py --validate      # Validate JSON only
"""

import json
import sys
from pathlib import Path
from typing import Optional


def load_handles(json_path: Path) -> dict:
    """Load handles from JSON file."""
    try:
        with open(json_path, "r") as f:
            data = json.load(f)
        return data
    except FileNotFoundError:
        print(f"Error: {json_path} not found.", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        sys.exit(1)


def validate_handles(data: dict) -> bool:
    """Validate handles.json structure."""
    if "handles" not in data:
        print("Error: handles.json missing 'handles' key.", file=sys.stderr)
        return False

    required_keys = {"id", "name", "description"}
    for i, handle in enumerate(data["handles"]):
        missing = required_keys - set(handle.keys())
        if missing:
            print(
                f"Error: Handle {i} missing required keys: {missing}",
                file=sys.stderr,
            )
            return False

    print(f"✓ Validation passed: {len(data['handles'])} handles found.")
    return True


def format_bom_list(items: list) -> str:
    """Format BOM items as bullet list or 'None'."""
    if not items:
        return "None"

    bullets = []
    for item in items:
        name = item.get("name", "")
        link = item.get("link")
        if link:
            bullets.append(f"<li><a href=\"{link}\">{name}</a></li>")
        else:
            bullets.append(f"<li>{name}</li>")

    return f"<ul>{''.join(bullets)}</ul>"


def format_parts_list(items: list, base_path: str = "") -> str:
    """Format parts items as bullet list or 'None'."""
    if not items:
        return "None"

    bullets = []
    for item in items:
        name = item.get("name", "")
        path = item.get("path")
        if path:
            bullets.append(f"<li><a href=\"{base_path}{path}\">{name}</a></li>")
        else:
            bullets.append(f"<li>{name}</li>")

    return f"<ul>{''.join(bullets)}</ul>"


def format_images(images: list, base_path: str = "../images/") -> str:
    """Format images as thumbnails."""
    if not images:
        return "(no image)"

    img_html = []
    for img in images:
        img_html.append(f'<img src="{base_path}{img}" alt="Handle image" width="120">')
    return "".join(img_html)


def format_link(path: Optional[str], display: str = "View") -> str:
    """Format a file link or return placeholder."""
    if not path:
        return "—"
    return f"[{display}]({path})"


def generate_markdown_catalog(data: dict, output_path: Path):
    """Generate Markdown catalog from handles data."""
    md = []

    # Header
    md.append("# Handle Catalog\n")
    md.append("Auto-generated from [handles.json](handles.json).\n")
    md.append("Last updated: (regenerate to update this)\n\n")

    # Table header
    md.append(
        "| Handle | Image | Purchased BOM | Printed Parts | Fabricated Parts | Assembly | CAD | Mesh | Sim |\n"
    )
    md.append("|---|---|---|---|---|---|---|---|---|\n")

    # Table rows
    for handle in data.get("handles", []):
        name = handle.get("name", "")
        images = handle.get("images", [])
        purchased_bom = handle.get("purchasedBom", [])
        printed_parts = handle.get("printedParts", [])
        fabricated_parts = handle.get("fabricatedParts", [])
        assembly = handle.get("assemblyInstructions")
        cad_file = handle.get("cadFile")
        mesh_file = handle.get("meshFile")
        sim_file = handle.get("simFile")

        # Build cells
        img_cell = format_images(images)
        bom_cell = format_bom_list(purchased_bom)
        printed_cell = format_parts_list(printed_parts)
        fabricated_cell = format_parts_list(fabricated_parts)
        assembly_cell = format_link(assembly, "Instructions")
        cad_cell = format_link(cad_file, "CAD")
        mesh_cell = format_link(mesh_file, "Mesh")
        sim_cell = format_link(sim_file, "Sim")

        row = f"| {name} | {img_cell} | {bom_cell} | {printed_cell} | {fabricated_cell} | {assembly_cell} | {cad_cell} | {mesh_cell} | {sim_cell} |"
        md.append(row + "\n")

    # Footer
    md.append("\n---\n\n")
    md.append("## Notes\n\n")
    md.append("- Images are located in `docs/CAD/images/`\n")
    md.append("- File paths are relative to `docs/CAD/Handles/`\n")
    md.append("- Regenerate this catalog by running: `python3 generate_catalog.py`\n")

    # Write to file
    output_path.write_text("".join(md))
    print(f"✓ Catalog written to {output_path}")


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate Markdown catalog from handles.json"
    )
    parser.add_argument(
        "--output",
        "-o",
        type=str,
        default="catalog.md",
        help="Output file (default: catalog.md)",
    )
    parser.add_argument(
        "--validate",
        "-v",
        action="store_true",
        help="Validate JSON only, do not generate catalog",
    )

    args = parser.parse_args()

    # Resolve paths relative to this script
    script_dir = Path(__file__).parent
    json_path = script_dir / "handles.json"
    output_path = script_dir / args.output

    # Load and validate
    data = load_handles(json_path)
    if not validate_handles(data):
        sys.exit(1)

    if args.validate:
        sys.exit(0)

    # Generate catalog
    generate_markdown_catalog(data, output_path)


if __name__ == "__main__":
    main()
