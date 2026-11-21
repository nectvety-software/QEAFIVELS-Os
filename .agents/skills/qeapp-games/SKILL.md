# Skill: QEAPP Runtime and Game Core

## QEAPP package
Versioned manifest, entry point, resources, dependencies, declared architecture, capabilities/permissions and optional signature envelope. Validate archive paths, duplicate entries, size limits, compression ratios, manifest types and dependency graph before install.

## Runtime
- Stable ABI and lifecycle: create, start, pause, resume, stop, destroy.
- App sandbox with private data, scoped shared storage and permission broker.
- Resource loader with quotas, deterministic cleanup and cancellation.
- No direct hardware registers, privileged nodes or kernel pointers.

## Game framework
Provide:
- Fixed timestep simulation with render interpolation.
- Scene/state stack and lifecycle pause/resume.
- Input snapshots for multitouch and controllers.
- Sprite sheets, atlas, animation, tile maps and camera.
- Entity/component primitives, collision and physics extension.
- Bounded particle/audio pools and asset cache.
- Save slots, settings, localization and resource bundles.
- Frame pacing, 30/60 FPS caps, thermal/battery adaptation.

## Tests
Deterministic simulation, pause/resume, touch cancellation, collision edge cases, corrupt assets, memory quotas, app permission denial, path traversal and save recovery.
