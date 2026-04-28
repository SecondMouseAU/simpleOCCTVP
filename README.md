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
- **CADRays-style Rendering** -- Path-traced GI, PBR materials, filmic tone mapping, depth of field, environment cubemaps, and one-call quality presets

## Supported Platforms

| Platform | Library | CI |
|----------|---------|-----|
| macOS (arm64) | `libsimpleOCCTVP.dylib` | GitHub Actions |
| Windows (x64) | `simpleOCCTVP.dll` | GitHub Actions |
| Linux (x64) | `libsimpleOCCTVP.so` | GitHub Actions |

## Using in Your Project

### Option 1: Prebuilt binaries (recommended)

Download the shared library for your platform from the [Releases](https://github.com/gsdali/simpleOCCTVP/releases) page and place it where your application can find it.

**Directory layout:**

```
your-project/
  libs/
    libsimpleOCCTVP.dylib    (macOS)
    simpleOCCTVP.dll          (Windows)
    libsimpleOCCTVP.so        (Linux)
  include/
    occt_templot.h
  src/
    your_code.c
```

### Option 2: Build from source

```bash
git clone https://github.com/gsdali/simpleOCCTVP.git
cd simpleOCCTVP
scripts/build-occt-deps.sh          # ~15-30 min, builds OCCT 8.0.0
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The shared library is in `build/`.

### C / C++

Copy `src/occt_templot.h` into your include path. Link against the shared library.

```c
#include "occt_templot.h"

int main() {
    occt_templot_init();

    // Import a STEP file
    OTShapeRef shape = ot_import_step("model.step");
    if (!shape) {
        printf("Error: %s\n", occt_templot_last_error());
        return 1;
    }

    // Get bounding box
    double xmin, ymin, zmin, xmax, ymax, zmax;
    ot_shape_bounding_box(shape, &xmin, &ymin, &zmin, &xmax, &ymax, &zmax);

    // Extract triangle mesh
    OTMeshData mesh = ot_mesh_shape(shape, 0.1);
    printf("Triangles: %d, Vertices: %d\n", mesh.triangle_count, mesh.vertex_count);
    ot_mesh_free(&mesh);

    // Offscreen render to PNG
    OTViewerRef viewer = ot_viewer_create(800, 600);
    ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
    ot_viewer_fit_all(viewer);
    ot_viewer_save_image(viewer, "render.png");
    ot_viewer_destroy(viewer);

    ot_shape_free(shape);
    occt_templot_shutdown();
    return 0;
}
```

**Compile:**

```bash
# macOS
cc -o myapp myapp.c -I./include -L./libs -lsimpleOCCTVP -Wl,-rpath,@executable_path/libs

# Linux
cc -o myapp myapp.c -I./include -L./libs -lsimpleOCCTVP -Wl,-rpath,'$ORIGIN/libs'

# Windows (MSVC)
cl /I include myapp.c /link /LIBPATH:libs simpleOCCTVP.lib
```

### CMake

```cmake
# Find the prebuilt shared library
add_library(simpleOCCTVP SHARED IMPORTED)
set_target_properties(simpleOCCTVP PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libs/libsimpleOCCTVP${CMAKE_SHARED_LIBRARY_SUFFIX}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include"
)
# On Windows, also set IMPORTED_IMPLIB to the .lib file

target_link_libraries(your_target PRIVATE simpleOCCTVP)
```

Or as a subdirectory (builds from source):

```cmake
add_subdirectory(simpleOCCTVP)
target_link_libraries(your_target PRIVATE simpleOCCTVP)
```

### Pascal / Lazarus

Add `pascal/occt_templot.pas` to your project and place the shared library where your executable can find it.

```pascal
uses occt_templot;

procedure LoadAndRender;
var
  shape: OTShapeRef;
  mesh: OTMeshData;
begin
  occt_templot_init;
  try
    shape := ot_import_step('model.step');
    if shape = nil then
      raise Exception.Create('Import failed: ' + OTGetLastError);
    try
      mesh := ot_mesh_shape(shape, 0.1);
      try
        WriteLn('Triangles: ', mesh.triangle_count);
      finally
        ot_mesh_free(mesh);
      end;
    finally
      ot_shape_free(shape);
    end;
  finally
    occt_templot_shutdown;
  end;
end;
```

**Library search paths:**

| Platform | Place library in |
|----------|-----------------|
| macOS | Same directory as executable, or `/usr/local/lib` |
| Windows | Same directory as `.exe`, or on `PATH` |
| Linux | Same directory as executable (uses `$ORIGIN` rpath), or `/usr/local/lib` |

### Dynamic loading (dlopen / LoadLibrary)

If you prefer to load at runtime rather than link at compile time:

```c
// Unix (macOS / Linux)
#include <dlfcn.h>
void *lib = dlopen("libsimpleOCCTVP.dylib", RTLD_NOW);  // or .so
void (*init)(void) = dlsym(lib, "occt_templot_init");
init();
// ...
dlclose(lib);
```

```c
// Windows
#include <windows.h>
HMODULE lib = LoadLibraryA("simpleOCCTVP.dll");
void (*init)(void) = (void(*)(void))GetProcAddress(lib, "occt_templot_init");
init();
// ...
FreeLibrary(lib);
```

## Building from Source

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

**Linux** requires: `libgl-dev`, `libglu1-mesa-dev`, `libegl-dev`, `libx11-dev`, `libxi-dev`, `libxmu-dev`

```bash
sudo apt-get install -y libgl-dev libglu1-mesa-dev libegl-dev libx11-dev libxi-dev libxmu-dev
```

**macOS** uses system frameworks (AppKit, OpenGL, Foundation, IOKit).

**Windows** uses opengl32, gdi32, user32 from the Windows SDK.

## API Reference

All functions use cdecl calling convention and are prefixed with `ot_`. The full API is defined in `src/occt_templot.h`.

### Lifecycle

| Function | Description |
|----------|-------------|
| `occt_templot_init()` | Initialize the library. Call once at startup. |
| `occt_templot_shutdown()` | Release global resources. Call at exit. |
| `occt_templot_version()` | Returns version string (e.g. `"0.1.0"`). |
| `occt_templot_last_error()` | Thread-local error message, or `NULL`. |

### Shape I/O

| Function | Description |
|----------|-------------|
| `ot_import_stl(path)` | Import STL file, returns `OTShapeRef`. |
| `ot_import_step(path)` | Import STEP file. |
| `ot_import_iges(path)` | Import IGES file. |
| `ot_import_obj(path)` | Import OBJ file. |
| `ot_import_step_robust(path)` | Import STEP with automatic healing. |
| `ot_export_stl(shape, path, deflection)` | Export to STL. |
| `ot_export_step(shape, path)` | Export to STEP. |
| `ot_export_iges(shape, path)` | Export to IGES. |
| `ot_export_obj(shape, path, deflection)` | Export to OBJ. |
| `ot_export_ply(shape, path, deflection)` | Export to PLY. |
| `ot_shape_free(shape)` | Free a shape handle. |

### Healing & Analysis

| Function | Description |
|----------|-------------|
| `ot_heal_shape(shape)` | Automatic shape repair. Returns new shape. |
| `ot_heal_shape_detailed(shape, tol, ...)` | Repair with fine-grained control. |
| `ot_analyze_shape(shape, tolerance)` | Returns `OTShapeAnalysis` diagnostics. |
| `ot_sew_shape(shape, tolerance)` | Sew disconnected faces. |
| `ot_make_solid(shape)` | Convert shell to solid. |

### Shape Info

| Function | Description |
|----------|-------------|
| `ot_shape_volume(shape)` | Compute volume. |
| `ot_shape_surface_area(shape)` | Compute surface area. |
| `ot_shape_bounding_box(shape, ...)` | Get axis-aligned bounding box. |

### Mesh Extraction

| Function | Description |
|----------|-------------|
| `ot_mesh_shape(shape, deflection)` | Returns `OTMeshData` with interleaved `[x,y,z, nx,ny,nz]` vertices and triangle indices. |
| `ot_mesh_free(mesh)` | Free mesh data. |
| `ot_edge_mesh_shape(shape, deflection)` | Returns `OTEdgeMeshData` with edge polylines. |
| `ot_edge_mesh_free(mesh)` | Free edge mesh data. |

### Offscreen Viewer

| Function | Description |
|----------|-------------|
| `ot_viewer_create(w, h)` | Create offscreen viewer. Returns `OTViewerRef`. |
| `ot_viewer_destroy(viewer)` | Destroy viewer. |
| `ot_viewer_add_shape(viewer, shape, mode)` | Add shape. Mode: `0`=wireframe, `1`=shaded, `2`=shaded+edges. |
| `ot_viewer_fit_all(viewer)` | Fit camera to all shapes. |
| `ot_viewer_render(viewer, ...)` | Render to RGBA buffer. |
| `ot_viewer_save_image(viewer, path)` | Render and save to image file. |
| `ot_viewer_set_camera(viewer, ...)` | Set eye, center, up vectors. |
| `ot_viewer_set_background_color(viewer, r, g, b)` | Set solid background. |
| `ot_viewer_set_background_gradient(viewer, ...)` | Set gradient background. |

### Rendering Quality (CADRays-style)

These map onto OCCT's `Graphic3d_RenderingParams` — the same GPU rendering path the [CADRays](https://github.com/Open-Cascade-SAS/CADRays) application uses.

| Function | Description |
|----------|-------------|
| `ot_viewer_set_render_preset(viewer, preset)` | One-call quality preset: `0`=DRAFT, `1`=BALANCED (default), `2`=PHOTOREALISTIC. |
| `ot_viewer_set_rendering_method(viewer, method)` | `0`=rasterization, `1`=ray tracing. |
| `ot_viewer_set_path_tracing(viewer, enabled)` | Toggle path-traced global illumination (forces ray tracing). |
| `ot_viewer_set_samples_per_pixel(viewer, spp)` | Path-tracing samples per pixel (1=preview, 256=quality). |
| `ot_viewer_set_ray_depth(viewer, depth)` | Max ray recursion depth (3=fast, 16=glass). |
| `ot_viewer_set_shadows(viewer, on, transparent_on)` | Enable shadows; transparent shadows let light filter through glass. |
| `ot_viewer_set_reflections(viewer, on)` | Enable specular reflections. |
| `ot_viewer_set_antialiasing(viewer, on)` | Enable adaptive AA (separate from MSAA). |
| `ot_viewer_set_tone_mapping(viewer, method, exposure, white)` | `0`=disabled, `1`=filmic. |
| `ot_viewer_set_depth_of_field(viewer, aperture, focal)` | Depth-of-field (path tracing only; aperture=0 disables). |
| `ot_viewer_set_shading_model(viewer, model)` | `0`=default (Phong), `2`=PBR, `3`=PBR-facet, `4`=unlit. |
| `ot_viewer_set_environment_cubemap(viewer, image_path)` | Load a packed cubemap for IBL + background. |
| `ot_viewer_set_environment_background(viewer, on)` | Use the cubemap as background. |
| `ot_viewer_set_shape_pbr_material(viewer, id, r, g, b, metallic, roughness)` | Per-shape PBR material (also sets BSDF for ray tracing). |
| `ot_viewer_set_shape_emission(viewer, id, r, g, b, intensity)` | Make a shape emissive (acts as area light in path tracing). |

### Standalone Camera

| Function | Description |
|----------|-------------|
| `ot_camera_create()` | Create standalone camera. Returns `OTCameraRef`. |
| `ot_camera_destroy(camera)` | Destroy camera. |
| `ot_camera_get_view_matrix(camera, out16)` | Get 4x4 view matrix (column-major). |
| `ot_camera_get_projection_matrix(camera, out16)` | Get 4x4 projection matrix. |
| `ot_camera_project(camera, ...)` | World to NDC coordinates. |
| `ot_camera_unproject(camera, ...)` | NDC to world coordinates. |
| `ot_camera_fit_bbox(camera, ...)` | Fit camera to bounding box. |

### Display Drawer

| Function | Description |
|----------|-------------|
| `ot_drawer_create()` | Create display drawer. Returns `OTDrawerRef`. |
| `ot_drawer_destroy(drawer)` | Destroy drawer. |
| `ot_drawer_set_deviation_coefficient(drawer, coeff)` | Set tessellation quality. |
| `ot_mesh_shape_with_drawer(shape, drawer)` | Mesh with custom tessellation settings. |

## License

See [LICENSE](LICENSE) for details.
