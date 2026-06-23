# Getting Started (C)

This guide takes you from a fresh checkout to a rendered PNG: link the shared
library, load a shape, extract a triangle mesh, and render the scene offscreen —
all through the pure C API in
[`src/occt_templot.h`](../../src/occt_templot.h).

For the full per-function reference, see [C API Reference](../reference/c-api.md).

## 1. Get the library and header

Download the shared library for your platform from the
[Releases](https://github.com/SecondMouseAU/simpleOCCTVP/releases) page, or build
it from source (see the [README](../../README.md)). You need two things:

| File | Purpose |
|------|---------|
| `src/occt_templot.h` | The C header to `#include`. |
| `libsimpleOCCTVP.dylib` / `.so` / `simpleOCCTVP.dll` | The shared library to link. |

A typical layout:

```
your-project/
  libs/    libsimpleOCCTVP.dylib   (or .so / .dll)
  include/ occt_templot.h
  src/     main.c
```

## 2. Write the program

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    /* 1. Initialize once at startup. */
    occt_templot_init();

    /* 2. Load a shape. Returns NULL on failure. */
    OTShapeRef shape = ot_import_step("model.step");
    if (!shape) {
        printf("import failed: %s\n", occt_templot_last_error());
        occt_templot_shutdown();
        return 1;
    }

    double xmin, ymin, zmin, xmax, ymax, zmax;
    ot_shape_bounding_box(shape, &xmin, &ymin, &zmin, &xmax, &ymax, &zmax);
    printf("bbox x: %.2f .. %.2f\n", xmin, xmax);

    /* 3. Extract a triangle mesh (interleaved position+normal). */
    OTMeshData mesh = ot_mesh_shape(shape, 0.1);
    printf("triangles: %d  vertices: %d\n",
           mesh.triangle_count, mesh.vertex_count);
    ot_mesh_free(&mesh);            /* free the mesh buffers */

    /* 4. Render the shape offscreen and save a PNG. */
    OTViewerRef viewer = ot_viewer_create(800, 600);
    int32_t id = ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
    ot_viewer_set_shape_material(viewer, id, "steel");
    ot_viewer_set_render_preset(viewer, OT_PRESET_BALANCED);
    ot_viewer_set_view_orientation(viewer, OT_VIEW_ISO);
    ot_viewer_fit_all(viewer);
    if (!ot_viewer_save_image(viewer, "render.png"))
        printf("render failed: %s\n", occt_templot_last_error());
    ot_viewer_destroy(viewer);     /* destroy the viewer */

    /* 5. Free the shape and shut down. */
    ot_shape_free(shape);          /* the viewer does not own the shape */
    occt_templot_shutdown();
    return 0;
}
```

<!-- 3D render TODO: example rendered PNG -->

## 3. Compile and link

```bash
# macOS
cc -o myapp src/main.c -I include -L libs -lsimpleOCCTVP \
   -Wl,-rpath,@executable_path/libs

# Linux
cc -o myapp src/main.c -I include -L libs -lsimpleOCCTVP \
   -Wl,-rpath,'$ORIGIN/libs'

# Windows (MSVC)
cl /I include src\main.c /link /LIBPATH:libs simpleOCCTVP.lib
```

The `-rpath` flags let the executable find the shared library next to it at run
time. On Windows, place `simpleOCCTVP.dll` beside the `.exe` or on `PATH`.

## 4. Run

```bash
./myapp
```

You should see the bounding box and triangle count printed, and a `render.png`
written next to the executable.

## Key rules to remember

- **Init / shutdown bracket everything.** `occt_templot_init()` first,
  `occt_templot_shutdown()` last.
- **Free every handle.** `ot_shape_free` for shapes, `ot_viewer_destroy` for
  viewers, `ot_camera_destroy` for cameras, `ot_drawer_destroy` for drawers. All
  are NULL-safe.
- **Free returned struct buffers.** `ot_mesh_free` / `ot_edge_mesh_free` release
  the heap memory inside `OTMeshData` / `OTEdgeMeshData`.
- **Check for `NULL`** after every import/heal, and read
  `occt_templot_last_error()` for the reason.
- **The viewer does not own your shape** — free the `OTShapeRef` yourself after
  destroying the viewer.

## Next steps

- Browse the grouped [C API Reference](../reference/c-api.md): shape I/O, healing
  & analysis, mesh extraction, offscreen render, standalone camera, display
  drawer.
- Using Pascal/Lazarus instead? The same calls are available through
  [`pascal/occt_templot.pas`](../../pascal/occt_templot.pas); see the Pascal
  binding section of the reference.
