---
type: component
title: Components index
resource: https://github.com/SecondMouseAU/simpleOCCTVP
tags: [index, api, c-api]
description: simpleOCCTVP C API surface, grouped by OCCT domain.
timestamp: 2026-06-22
---

# Components

`simpleOCCTVP` ships **one** artifact: the shared library
(`libsimpleOCCTVP.dylib` / `simpleOCCTVP.dll` / `libsimpleOCCTVP.so`), built from `src/` via
CMake. Its public surface is the **C API** declared in `src/occt_templot.h` — all `cdecl`,
prefixed `ot_` (lifecycle: `occt_templot_*`). A Pascal binding lives in
`pascal/occt_templot.pas`.

The API is organised by OCCT domain:

- **Lifecycle** — `occt_templot_init`, `occt_templot_shutdown`, `occt_templot_version`,
  `occt_templot_last_error`
- **Shape I/O** — import/export STL, STEP, IGES, OBJ, PLY (`ot_import_step`,
  `ot_import_step_robust`, `ot_export_stl`, …), `ot_shape_free`
- **Healing & analysis** — `ot_heal_shape`, `ot_heal_shape_detailed`, `ot_analyze_shape`
  (`OTShapeAnalysis`), `ot_sew_shape`, `ot_make_solid`
- **Shape info** — `ot_shape_volume`, `ot_shape_surface_area`, `ot_shape_bounding_box`
- **Mesh extraction** — `ot_mesh_shape` (interleaved `[x,y,z, nx,ny,nz]` + indices,
  `OTMeshData`), `ot_edge_mesh_shape` (wireframe polylines, `OTEdgeMeshData`)
- **Offscreen viewer** — V3d/OpenGL offscreen render: `ot_viewer_create`, `ot_viewer_add_shape`,
  `ot_viewer_fit_all`, `ot_viewer_render`, `ot_viewer_save_image`, camera / background setters
- **Rendering quality (CADRays-style)** — maps onto OCCT `Graphic3d_RenderingParams`:
  path-traced GI, PBR materials, filmic tone mapping, depth of field, environment cubemaps,
  and one-call presets (`ot_viewer_set_render_preset`, `ot_viewer_set_path_tracing`, …)
- **Standalone camera** — `ot_camera_create`, view/projection matrices, `ot_camera_project` /
  `ot_camera_unproject`, `ot_camera_fit_bbox`
- **Display drawer** — `ot_drawer_create`, `ot_drawer_set_deviation_coefficient`,
  `ot_mesh_shape_with_drawer` (configurable tessellation)
