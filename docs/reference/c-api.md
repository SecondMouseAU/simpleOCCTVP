# C API Reference

`simpleOCCTVP` is a C++17 shared library that exposes a **pure C API** over
[OpenCASCADE](https://dev.opencascade.org/) (OCCT 8.0.0). Every exported symbol
is declared in [`src/occt_templot.h`](../../src/occt_templot.h), uses the
**cdecl** calling convention, and is safe to consume by FFI from C, C++, Pascal,
or any language that speaks cdecl.

Conventions used throughout this document:

- **Lifecycle.** Call `occt_templot_init()` once before any other function and
  `occt_templot_shutdown()` once at exit.
- **Handles.** `OTShapeRef`, `OTViewerRef`, `OTCameraRef`, and `OTDrawerRef` are
  opaque `void*` handles. You own every handle you create and must release it
  with the matching destructor (`ot_shape_free`, `ot_viewer_destroy`,
  `ot_camera_destroy`, `ot_drawer_destroy`). Destructors are NULL-safe.
- **Errors.** Functions that build shapes return `NULL` on failure; call
  `occt_templot_last_error()` for a thread-local message.
- **Returned structs** (`OTMeshData`, `OTEdgeMeshData`) own heap buffers that you
  must release with the matching `*_free` function.

---

## Lifecycle

### `occt_templot_init`

```c
void occt_templot_init(void);
```

Initialize the library. Call once before any other function.

### `occt_templot_shutdown`

```c
void occt_templot_shutdown(void);
```

Shut down the library and release global resources.

### `occt_templot_version`

```c
const char* occt_templot_version(void);
```

Return the library version string (e.g. `"0.1.0"`). The returned pointer is owned
by the library; do not free it.

### `occt_templot_last_error`

```c
const char* occt_templot_last_error(void);
```

Return the last error message for the calling thread, or `NULL` if there is no
error. Thread-local; the pointer is owned by the library.

### `ot_set_trace` / `ot_get_trace`

```c
void ot_set_trace(bool enable);
bool ot_get_trace(void);
```

Enable or query diagnostic tracing to `stderr`. Tracing can also be activated by
setting the `OCCT_TEMPLOT_TRACE=1` environment variable before the library loads.

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    occt_templot_init();
    printf("simpleOCCTVP %s\n", occt_templot_version());
    if (occt_templot_last_error())
        printf("error: %s\n", occt_templot_last_error());
    occt_templot_shutdown();
    return 0;
}
```

---

## Shape Handles

### `ot_shape_free`

```c
void ot_shape_free(OTShapeRef shape);
```

Free a shape handle. Safe to call with `NULL`. Every `OTShapeRef` returned by an
import, healing, or upgrade function must be freed exactly once.

### `ot_shape_is_valid`

```c
bool ot_shape_is_valid(OTShapeRef shape);
```

Return `true` if the handle is non-null and contains valid geometry.

### `ot_shape_type`

```c
int32_t ot_shape_type(OTShapeRef shape);
```

Return the shape type: `0`=Compound, `1`=CompSolid, `2`=Solid, `3`=Shell,
`4`=Face, `5`=Wire, `6`=Edge, `7`=Vertex, `8`=Shape, `-1`=error.

---

## Shape I/O

All importers return a new `OTShapeRef` (owned by the caller, free with
`ot_shape_free`) or `NULL` on failure. All exporters return `true` on success.
The `*_robust` importers run an automatic sew + solid + heal pipeline.

### Import

```c
OTShapeRef ot_import_stl(const char* path);
OTShapeRef ot_import_stl_robust(const char* path, double sewing_tolerance);
OTShapeRef ot_import_step(const char* path);
OTShapeRef ot_import_step_robust(const char* path);
OTShapeRef ot_import_iges(const char* path);
OTShapeRef ot_import_iges_robust(const char* path);
OTShapeRef ot_import_obj(const char* path);
```

| Function | Description |
|----------|-------------|
| `ot_import_stl(path)` | Import an STL file. |
| `ot_import_stl_robust(path, tol)` | Import STL with sew + solid + heal. Use `1e-6` for a default tolerance. |
| `ot_import_step(path)` | Import a STEP file. |
| `ot_import_step_robust(path)` | Import STEP with automatic healing and precision handling. |
| `ot_import_iges(path)` | Import an IGES file. |
| `ot_import_iges_robust(path)` | Import IGES with automatic healing. |
| `ot_import_obj(path)` | Import an OBJ file. |

### Import with diagnostics

```c
typedef struct {
    OTShapeRef shape;       /* Result shape (NULL on failure) */
    int32_t original_type;  /* TopAbs_ShapeEnum before processing */
    int32_t result_type;    /* TopAbs_ShapeEnum after processing */
    bool sewing_applied;    /* Whether sewing was needed */
    bool solid_created;     /* Whether a solid was created from a shell */
    bool healing_applied;   /* Whether healing was applied */
} OTImportResult;

OTImportResult ot_import_step_with_diagnostics(const char* path);
```

Import a STEP file and report which repair steps ran. The `shape` field is owned
by the caller and must be freed with `ot_shape_free` when non-`NULL`.

### Export

```c
bool ot_export_stl(OTShapeRef shape, const char* path, double deflection);
bool ot_export_step(OTShapeRef shape, const char* path);
bool ot_export_iges(OTShapeRef shape, const char* path);
bool ot_export_obj(OTShapeRef shape, const char* path, double deflection);
bool ot_export_ply(OTShapeRef shape, const char* path, double deflection);
```

| Function | Description |
|----------|-------------|
| `ot_export_stl(shape, path, deflection)` | Export binary STL. `deflection` controls mesh fineness (smaller = finer, e.g. `0.1`). |
| `ot_export_step(shape, path)` | Export STEP (AP214 schema). |
| `ot_export_iges(shape, path)` | Export IGES. |
| `ot_export_obj(shape, path, deflection)` | Export OBJ (tessellated). |
| `ot_export_ply(shape, path, deflection)` | Export PLY (Stanford Polygon Format). |

### Runnable example — import, inspect, re-export

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    occt_templot_init();

    OTShapeRef shape = ot_import_step("model.step");
    if (!shape) {
        printf("import failed: %s\n", occt_templot_last_error());
        occt_templot_shutdown();
        return 1;
    }

    printf("shape type code: %d\n", ot_shape_type(shape));

    if (!ot_export_stl(shape, "model.stl", 0.1))
        printf("export failed: %s\n", occt_templot_last_error());

    ot_shape_free(shape);
    occt_templot_shutdown();
    return 0;
}
```

---

## Healing & Analysis

Healing functions return a **new** `OTShapeRef` (free it separately from the
input) or `NULL` on failure.

### `ot_analyze_shape`

```c
typedef struct {
    int32_t small_edge_count;        /* Edges smaller than tolerance */
    int32_t small_face_count;        /* Faces smaller than tolerance^2 */
    int32_t gap_count;               /* Gaps between edges/faces */
    int32_t self_intersection_count; /* Self-intersections detected */
    int32_t free_edge_count;         /* Unconnected edges */
    int32_t free_face_count;         /* Free (non-closed) faces */
    bool has_invalid_topology;       /* Topology check failed */
    bool is_valid;                   /* Analysis completed successfully */
} OTShapeAnalysis;

OTShapeAnalysis ot_analyze_shape(OTShapeRef shape, double tolerance);
```

Analyze a shape for common problems. `tolerance` controls small-feature detection
(e.g. `1e-6`). Returns counts by value; no allocation, nothing to free.

### `ot_heal_shape`

```c
OTShapeRef ot_heal_shape(OTShapeRef shape);
```

Auto-fix common issues with `ShapeFix_Shape` defaults. Returns a new shape.

### `ot_heal_shape_detailed`

```c
OTShapeRef ot_heal_shape_detailed(OTShapeRef shape, double tolerance,
    bool fix_solid, bool fix_shell, bool fix_face, bool fix_wire);
```

Heal with fine-grained control over which fixing operations run.

### `ot_sew_shape`

```c
OTShapeRef ot_sew_shape(OTShapeRef shape, double tolerance);
```

Sew disconnected faces. If the result is a closed shell, it attempts to create a
solid.

### `ot_upgrade_shape`

```c
OTShapeRef ot_upgrade_shape(OTShapeRef shape, double tolerance);
```

Run the full sew + make-solid + heal pipeline.

### `ot_make_solid`

```c
OTShapeRef ot_make_solid(OTShapeRef shape);
```

Create a solid from a shell. Returns `NULL` if not possible.

### `ot_shape_check_valid`

```c
bool ot_shape_check_valid(OTShapeRef shape);
```

Return `true` if the shape passes `BRepCheck` validation as a closed solid.

### Shape info

```c
double ot_shape_volume(OTShapeRef shape);        /* cubic units, or -1.0 on error */
double ot_shape_surface_area(OTShapeRef shape);  /* square units, or -1.0 on error */
void   ot_shape_bounding_box(OTShapeRef shape,
           double* xmin, double* ymin, double* zmin,
           double* xmax, double* ymax, double* zmax);
```

`ot_shape_bounding_box` writes the axis-aligned bounding box through six caller
pointers, all of which must be non-`NULL`.

### Runnable example — analyze, heal, measure

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    occt_templot_init();

    OTShapeRef raw = ot_import_step("imperfect.step");
    if (!raw) { occt_templot_shutdown(); return 1; }

    OTShapeAnalysis a = ot_analyze_shape(raw, 1e-6);
    printf("free edges: %d  gaps: %d  valid: %d\n",
           a.free_edge_count, a.gap_count, a.is_valid);

    OTShapeRef healed = ot_heal_shape(raw);
    if (healed) {
        double xmin, ymin, zmin, xmax, ymax, zmax;
        ot_shape_bounding_box(healed, &xmin, &ymin, &zmin, &xmax, &ymax, &zmax);
        printf("volume: %.3f  bbox: [%.1f..%.1f]\n",
               ot_shape_volume(healed), xmin, xmax);
        ot_shape_free(healed);
    }

    ot_shape_free(raw);
    occt_templot_shutdown();
    return 0;
}
```

---

## Mesh Extraction

### `ot_mesh_shape` / `ot_mesh_free`

```c
typedef struct {
    float* vertices;        /* Interleaved [x,y,z, nx,ny,nz, ...] (6 floats/vertex) */
    int32_t vertex_count;   /* Number of vertices */
    int32_t* indices;       /* Triangle indices (3 per triangle) */
    int32_t triangle_count; /* Number of triangles */
} OTMeshData;

OTMeshData ot_mesh_shape(OTShapeRef shape, double deflection);
void       ot_mesh_free(OTMeshData* mesh);
```

Triangulate a shape into an interleaved position+normal vertex buffer. Smaller
`deflection` produces a finer mesh (e.g. `0.1`). Check `vertex_count > 0`, then
**free the buffers with `ot_mesh_free`** (which is safe on a zeroed struct).

### Separate (caller-managed) buffers

```c
bool ot_mesh_shape_separate(OTShapeRef shape, double deflection,
        int32_t* out_vertex_count, int32_t* out_triangle_count);
bool ot_mesh_shape_fill(OTShapeRef shape,
        float* out_vertices, float* out_normals, int32_t* out_indices);
```

Two-phase API for callers that want to own the memory: first call
`ot_mesh_shape_separate` to obtain counts, allocate
`out_vertices`/`out_normals` as `float[vertex_count * 3]` and `out_indices` as
`int32_t[triangle_count * 3]`, then call `ot_mesh_shape_fill` on the same shape.

### Edge mesh

```c
typedef struct {
    float*   vertices;       /* [x,y,z, ...] 3 floats per vertex */
    int32_t  vertex_count;   /* Total number of vertices */
    int32_t* segment_starts; /* Index where each edge polyline begins */
    int32_t  segment_count;  /* Number of edge segments */
} OTEdgeMeshData;

OTEdgeMeshData ot_edge_mesh_shape(OTShapeRef shape, double deflection);
void           ot_edge_mesh_free(OTEdgeMeshData* mesh);
```

Extract edge polylines (for wireframe rendering). Free with `ot_edge_mesh_free`.

### Runnable example — extract and free a mesh

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    occt_templot_init();

    OTShapeRef shape = ot_import_step("model.step");
    if (!shape) { occt_templot_shutdown(); return 1; }

    OTMeshData mesh = ot_mesh_shape(shape, 0.1);
    if (mesh.vertex_count > 0) {
        printf("vertices: %d  triangles: %d\n",
               mesh.vertex_count, mesh.triangle_count);
        /* mesh.vertices is [x,y,z, nx,ny,nz, ...] — 6 floats per vertex */
        float x = mesh.vertices[0], y = mesh.vertices[1], z = mesh.vertices[2];
        printf("first vertex: %.3f %.3f %.3f\n", x, y, z);
    }
    ot_mesh_free(&mesh);   /* always free; safe on a zeroed struct */

    ot_shape_free(shape);
    occt_templot_shutdown();
    return 0;
}
```

---

## Offscreen Render

The offscreen viewer renders shapes through OCCT's V3d/OpenGL path with no window.
Create it with `ot_viewer_create`, add shapes, configure the camera and quality,
then render to a buffer or save to an image file. The viewer owns its displayed
shapes' presentations but **not** the `OTShapeRef` you pass in — free that
separately. Destroy the viewer with `ot_viewer_destroy`.

### Lifecycle

```c
OTViewerRef ot_viewer_create(int32_t width, int32_t height);
void        ot_viewer_destroy(OTViewerRef viewer);
bool        ot_viewer_resize(OTViewerRef viewer, int32_t width, int32_t height);
void        ot_viewer_get_size(OTViewerRef viewer, int32_t* out_width, int32_t* out_height);
```

### Shape display

```c
typedef enum {
    OT_DISPLAY_WIREFRAME    = 0,
    OT_DISPLAY_SHADED       = 1,
    OT_DISPLAY_SHADED_EDGES = 2
} OTDisplayMode;

int32_t ot_viewer_add_shape(OTViewerRef viewer, OTShapeRef shape, OTDisplayMode mode);
bool    ot_viewer_remove_shape(OTViewerRef viewer, int32_t display_id);
void    ot_viewer_clear(OTViewerRef viewer);
bool    ot_viewer_set_shape_color(OTViewerRef viewer, int32_t display_id, double r, double g, double b);
bool    ot_viewer_set_shape_transparency(OTViewerRef viewer, int32_t display_id, double t);
bool    ot_viewer_set_shape_display_mode(OTViewerRef viewer, int32_t display_id, OTDisplayMode mode);
bool    ot_viewer_set_shape_material(OTViewerRef viewer, int32_t display_id, const char* name);
```

`ot_viewer_add_shape` returns a **display id** (`>0`) used to address the shape
later, or `-1` on failure. Material names: `brass`, `bronze`, `copper`, `gold`,
`pewter`, `silver`, `steel`, `stone`, `chrome`, `aluminium`, `plastic`,
`default`.

### Background

```c
void ot_viewer_set_background_color(OTViewerRef viewer, double r, double g, double b);
void ot_viewer_set_background_gradient(OTViewerRef viewer,
        double r1, double g1, double b1, double r2, double g2, double b2);
```

### Camera control

```c
typedef enum { OT_PROJECTION_PERSPECTIVE = 0, OT_PROJECTION_ORTHOGRAPHIC = 1 } OTProjectionType;
typedef enum {
    OT_VIEW_FRONT=0, OT_VIEW_BACK=1, OT_VIEW_TOP=2, OT_VIEW_BOTTOM=3,
    OT_VIEW_LEFT=4, OT_VIEW_RIGHT=5, OT_VIEW_ISO=6, OT_VIEW_ISO_LEFT=7
} OTViewOrientation;

void ot_viewer_set_projection(OTViewerRef viewer, OTProjectionType type);
void ot_viewer_set_fov(OTViewerRef viewer, double degrees);
void ot_viewer_set_view_orientation(OTViewerRef viewer, OTViewOrientation orient);
void ot_viewer_fit_all(OTViewerRef viewer);
void ot_viewer_rotate(OTViewerRef viewer, double dx, double dy);
void ot_viewer_pan(OTViewerRef viewer, double dx, double dy);
void ot_viewer_zoom(OTViewerRef viewer, double factor);
void ot_viewer_zoom_at_point(OTViewerRef viewer, int32_t x, int32_t y, double factor);
void ot_viewer_set_camera(OTViewerRef viewer,
        double eye_x, double eye_y, double eye_z,
        double center_x, double center_y, double center_z,
        double up_x, double up_y, double up_z);
void ot_viewer_get_camera(OTViewerRef viewer,
        double* eye_x, double* eye_y, double* eye_z,
        double* center_x, double* center_y, double* center_z,
        double* up_x, double* up_y, double* up_z);
```

### Lighting

```c
void ot_viewer_set_headlight(OTViewerRef viewer, bool enabled);
void ot_viewer_add_directional_light(OTViewerRef viewer,
        double dir_x, double dir_y, double dir_z, double r, double g, double b);
void ot_viewer_set_ambient_light(OTViewerRef viewer, double intensity);
```

### Rendering — the render entry point

```c
const uint8_t* ot_viewer_render(OTViewerRef viewer,
        int32_t* out_width, int32_t* out_height, int32_t* out_size);
bool ot_viewer_render_to_buffer(OTViewerRef viewer, uint8_t* buffer, int32_t buffer_size);
bool ot_viewer_save_image(OTViewerRef viewer, const char* path);
```

- `ot_viewer_render` renders the scene and returns RGBA pixels **bottom-to-top**
  (OpenGL convention). The pointer is owned by the viewer and is valid only until
  the next render call or `ot_viewer_destroy` — do not free it.
- `ot_viewer_render_to_buffer` renders into a caller-allocated
  `width * height * 4`-byte buffer, **top-to-bottom**.
- `ot_viewer_save_image` renders and writes a file; format is chosen by
  extension (`.bmp` always; `.png` when FreeImage is available).

### Display settings

```c
void ot_viewer_set_edge_display(OTViewerRef viewer, bool enabled);
void ot_viewer_set_edge_color(OTViewerRef viewer, double r, double g, double b);
void ot_viewer_set_deflection(OTViewerRef viewer, double deflection);
void ot_viewer_set_msaa(OTViewerRef viewer, int32_t samples);  /* 0,2,4,8 — raster only */
```

### Rendering quality (CADRays-style)

These map onto OCCT's `Graphic3d_RenderingParams` — the GPU path the
[CADRays](https://github.com/Open-Cascade-SAS/CADRays) application uses. They are
no-ops on viewers created without a working OpenGL context.

```c
typedef enum { OT_PRESET_DRAFT=0, OT_PRESET_BALANCED=1, OT_PRESET_PHOTOREALISTIC=2 } OTRenderPreset;
typedef enum { OT_RENDER_RASTERIZATION=0, OT_RENDER_RAYTRACING=1 } OTRenderingMethod;
typedef enum { OT_TONEMAP_DISABLED=0, OT_TONEMAP_FILMIC=1 } OTToneMappingMethod;
typedef enum {
    OT_SHADING_DEFAULT=0, OT_SHADING_PHONG=1, OT_SHADING_PBR=2,
    OT_SHADING_PBR_FACET=3, OT_SHADING_UNLIT=4
} OTShadingModel;

void ot_viewer_set_render_preset(OTViewerRef viewer, OTRenderPreset preset);
void ot_viewer_set_rendering_method(OTViewerRef viewer, OTRenderingMethod method);
void ot_viewer_set_path_tracing(OTViewerRef viewer, bool enabled);
void ot_viewer_set_samples_per_pixel(OTViewerRef viewer, int32_t spp);
void ot_viewer_set_ray_depth(OTViewerRef viewer, int32_t depth);
void ot_viewer_set_shadows(OTViewerRef viewer, bool enabled, bool transparent_shadows);
void ot_viewer_set_reflections(OTViewerRef viewer, bool enabled);
void ot_viewer_set_antialiasing(OTViewerRef viewer, bool enabled);
void ot_viewer_set_tone_mapping(OTViewerRef viewer,
        OTToneMappingMethod method, double exposure, double white_point);
void ot_viewer_set_depth_of_field(OTViewerRef viewer,
        double aperture_radius, double focal_distance);
void ot_viewer_set_shading_model(OTViewerRef viewer, OTShadingModel model);
bool ot_viewer_set_environment_cubemap(OTViewerRef viewer, const char* image_path);
void ot_viewer_clear_environment_cubemap(OTViewerRef viewer);
void ot_viewer_set_environment_background(OTViewerRef viewer, bool enabled);
bool ot_viewer_set_shape_pbr_material(OTViewerRef viewer, int32_t display_id,
        double albedo_r, double albedo_g, double albedo_b,
        double metallic, double roughness);
bool ot_viewer_set_shape_emission(OTViewerRef viewer, int32_t display_id,
        double r, double g, double b, double intensity);
```

| Preset | Behaviour |
|--------|-----------|
| `OT_PRESET_DRAFT` | Rasterization, no shadows/reflections, 4x MSAA. Fastest. |
| `OT_PRESET_BALANCED` | Rasterization + shadows + reflections + 8x MSAA + filmic tone mapping. Default. |
| `OT_PRESET_PHOTOREALISTIC` | Ray-traced path tracing (256 spp, 8 bounces) + filmic. Slow. |

### Runnable example — offscreen render to PNG

```c
#include "occt_templot.h"
#include <stdio.h>

int main(void) {
    occt_templot_init();

    OTShapeRef shape = ot_import_step("model.step");
    if (!shape) { occt_templot_shutdown(); return 1; }

    OTViewerRef viewer = ot_viewer_create(800, 600);
    int32_t id = ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);

    ot_viewer_set_shape_material(viewer, id, "steel");
    ot_viewer_set_background_gradient(viewer, 0.2,0.2,0.3, 0.0,0.0,0.0);
    ot_viewer_set_render_preset(viewer, OT_PRESET_BALANCED);
    ot_viewer_set_view_orientation(viewer, OT_VIEW_ISO);
    ot_viewer_fit_all(viewer);

    if (!ot_viewer_save_image(viewer, "render.png"))
        printf("render failed: %s\n", occt_templot_last_error());

    ot_viewer_destroy(viewer);
    ot_shape_free(shape);
    occt_templot_shutdown();
    return 0;
}
```

<script type="module" src="https://cdn.jsdelivr.net/npm/@google/model-viewer/dist/model-viewer.min.js"></script>

<model-viewer src="../models/drilled-block.glb" camera-controls auto-rotate environment-image="neutral" exposure="1.1" shadow-intensity="1" style="width:340px;height:300px;background:#eef1f5;border-radius:6px"></model-viewer>

*Interactive 3D — representative kernel model (example rendered PNG).*

---

## Standalone Camera

A standalone `OTCameraRef` computes view/projection matrices and
project/unproject independently of any viewer. Matrices are **column-major** with
**zero-to-one depth**. Create with `ot_camera_create`, destroy with
`ot_camera_destroy`.

```c
OTCameraRef ot_camera_create(void);
void        ot_camera_destroy(OTCameraRef camera);

void ot_camera_set_eye(OTCameraRef camera, double x, double y, double z);
void ot_camera_get_eye(OTCameraRef camera, double* x, double* y, double* z);
void ot_camera_set_center(OTCameraRef camera, double x, double y, double z);
void ot_camera_get_center(OTCameraRef camera, double* x, double* y, double* z);
void ot_camera_set_up(OTCameraRef camera, double x, double y, double z);
void ot_camera_get_up(OTCameraRef camera, double* x, double* y, double* z);
void ot_camera_set_projection_type(OTCameraRef camera, OTProjectionType type);
OTProjectionType ot_camera_get_projection_type(OTCameraRef camera);
void ot_camera_set_fov(OTCameraRef camera, double degrees);
double ot_camera_get_fov(OTCameraRef camera);
void ot_camera_set_scale(OTCameraRef camera, double scale);
double ot_camera_get_scale(OTCameraRef camera);
void ot_camera_set_z_range(OTCameraRef camera, double z_near, double z_far);
void ot_camera_set_aspect(OTCameraRef camera, double aspect);

bool ot_camera_get_projection_matrix(OTCameraRef camera, double* out16);
bool ot_camera_get_view_matrix(OTCameraRef camera, double* out16);
bool ot_camera_project(OTCameraRef camera,
        double world_x, double world_y, double world_z,
        double* out_x, double* out_y, double* out_z);
bool ot_camera_unproject(OTCameraRef camera,
        double ndc_x, double ndc_y, double ndc_z,
        double* out_x, double* out_y, double* out_z);
bool ot_camera_fit_bbox(OTCameraRef camera,
        double xmin, double ymin, double zmin,
        double xmax, double ymax, double zmax);
```

`ot_camera_get_projection_matrix` / `ot_camera_get_view_matrix` write 16 doubles
into `out16` (a caller-allocated `double[16]`).

---

## Display Drawer

An `OTDrawerRef` carries tessellation-quality settings that can be reused across
mesh extractions. Create with `ot_drawer_create`, destroy with
`ot_drawer_destroy`.

```c
OTDrawerRef ot_drawer_create(void);
void        ot_drawer_destroy(OTDrawerRef drawer);

void   ot_drawer_set_deviation_coefficient(OTDrawerRef drawer, double coeff);
double ot_drawer_get_deviation_coefficient(OTDrawerRef drawer);
void   ot_drawer_set_deviation_angle(OTDrawerRef drawer, double radians);
double ot_drawer_get_deviation_angle(OTDrawerRef drawer);
void   ot_drawer_set_max_deviation(OTDrawerRef drawer, double deviation);
double ot_drawer_get_max_deviation(OTDrawerRef drawer);
void   ot_drawer_set_face_boundary_draw(OTDrawerRef drawer, bool enabled);
bool   ot_drawer_get_face_boundary_draw(OTDrawerRef drawer);

OTMeshData     ot_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer);
OTEdgeMeshData ot_edge_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer);
```

`ot_mesh_shape_with_drawer` and `ot_edge_mesh_shape_with_drawer` return
caller-owned data; free them with `ot_mesh_free` / `ot_edge_mesh_free`.

---

## Pascal binding

The repository ships a Free Pascal / Lazarus binding,
[`pascal/occt_templot.pas`](../../pascal/occt_templot.pas), that declares every
`ot_*` function `cdecl; external OCCT_LIB`. The unit selects the right library
name per platform (`simpleOCCTVP.dll`, `libsimpleOCCTVP.dylib`,
`libsimpleOCCTVP.so`) and adds a few helpers: `OTGetLastError`,
`OTShapeTypeName`, and `OTViewerRenderToBitmap`.

```pascal
uses occt_templot;

procedure LoadMeshAndRender;
var
  shape: OTShapeRef;
  mesh: OTMeshData;
  viewer: OTViewerRef;
begin
  occt_templot_init;
  try
    shape := ot_import_step('model.step');
    if shape = nil then
      raise Exception.Create('import failed: ' + OTGetLastError);
    try
      mesh := ot_mesh_shape(shape, 0.1);
      try
        WriteLn('triangles: ', mesh.triangle_count);
      finally
        ot_mesh_free(mesh);
      end;

      viewer := ot_viewer_create(800, 600);
      try
        ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
        ot_viewer_set_render_preset(viewer, OT_PRESET_BALANCED);
        ot_viewer_fit_all(viewer);
        ot_viewer_save_image(viewer, 'render.png');
      finally
        ot_viewer_destroy(viewer);
      end;
    finally
      ot_shape_free(shape);
    end;
  finally
    occt_templot_shutdown;
  end;
end;
```

<model-viewer src="../models/drilled-block.glb" camera-controls auto-rotate environment-image="neutral" exposure="1.1" shadow-intensity="1" style="width:340px;height:300px;background:#eef1f5;border-radius:6px"></model-viewer>

*Interactive 3D — representative kernel model (example rendered PNG).*
