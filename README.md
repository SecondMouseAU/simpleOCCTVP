# simpleOCCTVP

A C++17 shared library providing a pure C API over [OpenCASCADE](https://dev.opencascade.org/) (OCCT 8.0.0). Designed for easy FFI consumption from Pascal, C, or any language with cdecl support.

## Features

- **Shape I/O** -- Import/export STL, STEP, IGES, OBJ, PLY formats
- **Shape Healing** -- Automated repair, sewing, solid creation, validity analysis
- **Mesh Extraction** -- Triangulated mesh with interleaved position+normal data
- **Edge Mesh** -- Polyline extraction for wireframe rendering
- **Offscreen Rendering** -- V3d-based offscreen viewer with OpenGL
- **Camera Control** -- Standalone camera with projection/view matrices, project/unproject
- **Display Drawer** -- Configurable tessellation parameters

## Supported Platforms

| Platform | Library | CI |
|----------|---------|-----|
| macOS (arm64) | `libsimpleOCCTVP.dylib` | GitHub Actions |
| Windows (x64) | `simpleOCCTVP.dll` | GitHub Actions |
| Linux (x64) | `libsimpleOCCTVP.so` | GitHub Actions |

## Building

### Prerequisites

- CMake 3.20+
- C++17 compiler (Clang, GCC, or MSVC)
- ~5 GB disk space for OCCT build

### Build OCCT dependencies

```bash
scripts/build-occt-deps.sh
```

This downloads and builds OCCT 8.0.0 as static libraries into `deps/occt-install/`.

### Build the library

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

On headless Linux (no display), use xvfb for OpenGL tests:

```bash
cd build && xvfb-run ctest --output-on-failure
```

### Platform-specific notes

**Linux** requires: `libgl-dev`, `libglu1-mesa-dev`, `libx11-dev`, `libxi-dev`, `libxmu-dev`

```bash
sudo apt-get install -y libgl-dev libglu1-mesa-dev libx11-dev libxi-dev libxmu-dev
```

**macOS** uses system frameworks (AppKit, OpenGL, Foundation, IOKit).

**Windows** uses opengl32, gdi32, user32 from the Windows SDK.

## API Overview

All functions use cdecl calling convention and are prefixed with `ot_`. Include `occt_templot.h`.

```c
#include "occt_templot.h"

// Initialize / shutdown
occt_templot_init();
// ... use the API ...
occt_templot_shutdown();
```

### Lifecycle

- `occt_templot_init()` / `occt_templot_shutdown()` -- Library setup and teardown
- `occt_templot_version()` -- Returns version string
- `occt_templot_last_error()` -- Thread-local error message

### Shape I/O

- `ot_import_stl()`, `ot_import_step()`, `ot_import_iges()`, `ot_import_obj()` -- Import shapes
- `ot_export_stl()`, `ot_export_step()`, `ot_export_iges()`, `ot_export_obj()`, `ot_export_ply()` -- Export shapes
- `ot_shape_free()` -- Free shape handles

### Healing & Analysis

- `ot_heal_shape()` / `ot_heal_shape_detailed()` -- Shape repair
- `ot_analyze_shape()` -- Validity diagnostics
- `ot_sew_shape()`, `ot_upgrade_shape()`, `ot_make_solid()` -- Individual repair operations

### Mesh Extraction

- `ot_mesh_shape()` -- Triangulated mesh (interleaved position+normal, 6 floats/vertex)
- `ot_edge_mesh_shape()` -- Edge polylines for wireframe display

### Offscreen Viewer

- `ot_viewer_create()` / `ot_viewer_destroy()` -- Lifecycle
- `ot_viewer_add_shape()` -- Display shapes with wireframe/shaded/shaded+edges modes
- `ot_viewer_render()` / `ot_viewer_save_image()` -- Render to RGBA buffer or file
- Camera, lighting, and background control functions

### Pascal Bindings

Pascal/Lazarus bindings are in `pascal/occt_templot.pas`. See the unit header for usage.

## License

See [LICENSE](LICENSE) for details.
