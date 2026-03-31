# Emulation Library

PRISM replicates the real-world dynamics of various rotational objects. This library provides documentation on standard physical handles and their complementary software configurations.

## Handle Library
The physical interface of PRISM is modular. You can 3D print and swap handles to match the simulated task.
- **[Handle Design Guide & Library](CAD/Handles/README.md)**: Browse all available handles and learn how to contribute custom designs.
- **[Auto-Generated Catalog](CAD/Handles/catalog.md)**: Visual catalog with images and components.

### Available Emulations
1. **Hydrant Handwheel** (4-turn industrial valve)
2. **Quarter-turn Handle** (90° rotation constraint)
3. **Door Handle** (+/- 45° rotation with self-centering mechanics)
4. **Wrench Tightening** (fastener resistance task)

## Physics Configuration (Presets)
Emulations in PRISM are powered by configurable physics parameters (Viscous Damping, Coulomb Friction, Wall Stiffness, etc.).
- You can tune and save these parameters easily using the [Web Interface](html/web-interface-guide.md) or CLI commands (`valve_preset smooth`, `valve_preset default`).
