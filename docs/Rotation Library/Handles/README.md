
## Table of Contents

- [Quick Reference](#quick-reference)
- [Contributing a New Handle](#contributing-a-new-handle)
  - [1. Add Entry to handles.json](#1-add-entry-to-handlesjson)
  - [2. Organize Your Files](#2-organize-your-files)
  - [3. Add Images](#3-add-images)
  - [4. Write Assembly Instructions (Optional but Recommended)](#4-write-assembly-instructions-optional-but-recommended)
  - [5. Validate](#5-validate)
- [File Format Guidelines](#file-format-guidelines)
  - [Images](#images)
  - [CAD Files](#cad-files)
  - [Mesh Files](#mesh-files)
  - [Simulation Files](#simulation-files)
- [Schema Reference](#schema-reference)
- [Updating the Markdown Reference](#updating-the-markdown-reference)


﻿# Handle Library

A curated catalog of contributed valve/handle designs for PRISM. Handle metadata is stored in [handles.json](handles.json) for easy management and extensibility.

## Quick Reference

| Image | Handle | Description | Assembly | CAD | Mesh | Sim |
|---|---|---|---|---|---|---|
| ![Hydrant](../images/handwheel_installed.jpeg) | **Hydrant Handwheel** | 4-turn industrial handwheel | ✓ | — | — | — |
| ![Quarter-turn](../images/quarter_turn.jpeg) | **Quarter-turn Handle** | 90° rotation design | — | — | — | — |
| ![Wrench](../images/8mm_wrench.jpg) | **Wrench Tightening** | Tool-based fastener task | — | — | — | — |

**Legend:** ✓ = Available, — = Not yet contributed

For complete details, see [handles.json](handles.json).

## Contributing a New Handle

### 1. Add Entry to handles.json

Edit [handles.json](handles.json) and add an object to the `handles` array:

```json
{
  "id": "my-handle",
  "name": "My Handle Name",
  "description": "Brief description of the handle type and use case",
  "images": [
    "myhandle_front.jpg",
    "myhandle_installed.jpg"
  ],
  "purchasedBom": [
    {
      "name": "Part Name",
      "link": "https://vendor.com/product"
    }
  ],
  "printedParts": [
    {
      "name": "Adapter.stl",
      "path": "MyHandle/cad/Adapter.stl"
    }
  ],
  "fabricatedParts": [
    {
      "name": "Bracket.dxf",
      "path": "MyHandle/assembly/Bracket.dxf"
    }
  ],
  "assemblyInstructions": "MyHandle/assembly/README.md",
  "cadFile": "MyHandle/cad/Model.step",
  "meshFile": "MyHandle/mesh/Model.stl",
  "simFile": "MyHandle/sim/model.urdf"
}
```

### 2. Organize Your Files

Create a folder structure under `docs/CAD%20&%20Print%20Files/Handles/`:

```
MyHandle/
├── cad/                    # CAD files (STEP, F3D, STL, etc.)
│   └── Model.step
├── mesh/                   # Mesh files (STL, OBJ, GLB, etc.)
│   └── Model.stl
├── sim/                    # Simulation files (URDF, SDF, USD, etc.)
│   └── model.urdf
└── assembly/               # Assembly instructions and diagrams
    ├── README.md           # Detailed assembly guide
    ├── BOM.md              # Extended bill of materials
    └── diagrams/           # Optional: assembly diagrams
```

### 3. Add Images

Place images in `docs/images/`:

- `myhandle_front.jpg` (or `.png`)
- `myhandle_installed.jpg` (product photo)

Keep images ≤ 1500px width and ≤ 500KB.

### 4. Write Assembly Instructions (Optional but Recommended)

Create `MyHandle/assembly/README.md` with:

- Tools required
- Hardware list (nuts, bolts, washers, etc.)
- Step-by-step photos or diagrams
- Torque specs or fitment notes
- Troubleshooting tips

### 5. Validate

Before submitting:

- ✓ All paths in handles.json are relative to `docs/CAD%20&%20Print%20Files/Handles/`
- ✓ All images referenced exist in `docs/images/`
- ✓ All files (CAD, mesh, sim) referenced exist or are marked `null`
- ✓ External links (BOM, etc.) are valid URLs
- ✓ JSON is valid (use an online validator or a linter)

## File Format Guidelines

### Images

- Format: JPG (preferred for photos) or PNG (for diagrams)
- Size: ≤ 1500px width, ≤ 500KB
- Quality: Clear, well-lit product photos or assembly diagrams

### CAD Files

- Primary: STEP (.step) or F3D (.f3d) for interchange
- Printable: STL (.stl) for 3D printing

### Mesh Files

- STL (.stl): Universal 3D printing and CAD format
- OBJ (.obj): Lightweight, easy to share
- GLB (.glb): Optimized for web and simulation

### Simulation Files

- URDF (.urdf): ROS and general robotics
- SDF (.sdf): Gazebo simulation
- USD (.usd): Pixar Universal Scene Description (modern, extensible)

## Schema Reference

Each handle object in `handles.json` contains:

- **id** (string, required): Unique identifier (lowercase, hyphens)
- **name** (string, required): Display name
- **description** (string, required): Brief description of use case
- **images** (array): Filenames from `docs/images/` (relative path)
- **purchasedBom** (array of objects):
  - **name**: Part description
  - **link**: URL to vendor/product
- **printedParts** (array of objects):
  - **name**: Part name
  - **path**: Relative path from `docs/CAD%20&%20Print%20Files/Handles/` (or `null` if TBD)
- **fabricatedParts** (array of objects): Same structure as printedParts
- **assemblyInstructions** (string or null): Relative path to README.md
- **cadFile** (string or null): Relative path to primary CAD file
- **meshFile** (string or null): Relative path to mesh file
- **simFile** (string or null): Relative path to simulation file

Use `null` for fields that do not yet have content.

## Updating the Markdown Reference

This README is manually maintained. A future enhancement could auto-generate it from handles.json using a build script. For now, please update the Quick Reference table when adding or changing handle entries.


