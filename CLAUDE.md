# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A C++17 shared library that exposes a pure C / cdecl API over OpenCASCADE (OCCT 8.0.0) for FFI consumption from Pascal, C, or any cdecl-capable language. Ships as `libsimpleOCCTVP.{dylib,so}` / `simpleOCCTVP.dll`. Pascal bindings live in `pascal/occt_templot.pas`. The historical name (and all symbol prefixes) is `occt_templot` / `ot_*`; only the repo and library are named `simpleOCCTVP`.

## Build

OCCT itself must be built once as a static dependency before the library can be configured:

```bash
scripts/build-occt-deps.sh        # ~15-30 min, installs into deps/occt-install/
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

Run a single test: `./build/test_io`, `./build/test_heal`, `./build/test_viewer`, or `./build/test_render_data`. Re-run via ctest with `ctest -R test_io`.

On headless Linux the viewer test needs `xvfb-run ctest …`. CI on all three platforms excludes `viewer` via `--exclude-regex viewer` (no GPU).

`Dockerfile.test` reproduces the Linux CI build locally: `docker build -f Dockerfile.test .`.

## Architecture

**Public API surface — `src/occt_templot.h`** is the single source of truth for everything callers see. All exported symbols are `extern "C"`, prefixed `ot_` or `occt_templot_`, marked `OT_EXPORT`, and use cdecl. Symbol visibility is `hidden` by default (`CXX_VISIBILITY_PRESET hidden` in CMakeLists), so anything not annotated `OT_EXPORT` stays internal — be deliberate when adding to the header. The Pascal binding in `pascal/occt_templot.pas` mirrors this header by hand and must be updated whenever a public function or struct changes.

**Opaque handles.** Callers receive `void*` handles (`OTShapeRef`, `OTViewerRef`, `OTCameraRef`, `OTDrawerRef`). The internal definitions live in `src/occt_templot_internal.h` (`OTShapeInternal` wraps a `TopoDS_Shape`) and in each module's `.cpp` (`OTViewerInternal`, `OTCameraInternal`, `OTDrawerInternal`). Never expose OCCT types across the API boundary.

**Module layout.** Each `.cpp` is one functional area, all linked into a single shared library:
- `occt_templot_io.cpp` — STL/STEP/IGES/OBJ/PLY import & export, plus library lifecycle (`occt_templot_init/shutdown`) and the thread-local `g_last_error`.
- `occt_templot_heal.cpp` — `ot_heal_shape*`, `ot_sew_shape`, `ot_make_solid`, `ot_analyze_shape`.
- `occt_templot_mesh.cpp` — triangulation (`ot_mesh_shape`) and edge polylines (`ot_edge_mesh_shape`); meshes are interleaved `[x,y,z,nx,ny,nz]` floats with separate triangle index buffer.
- `occt_templot_viewer.cpp` — V3d/OpenGL offscreen viewer; **on macOS this file is compiled as Objective-C++** (`-x objective-c++` set in CMakeLists) for the NSOpenGLContext fallback.
- `occt_templot_render_data.cpp` — standalone camera (view/proj matrices, project/unproject, fit_bbox) and display drawer (tessellation params).

**CADRays-style rendering knobs.** `occt_templot_viewer.cpp` exposes the OCCT GPU rendering pipeline that the [CADRays](https://github.com/Open-Cascade-SAS/CADRays) demo app drives: rasterization vs ray tracing (`Method`), path-traced GI (`IsGlobalIlluminationEnabled` + `SamplesPerPixel`/`RaytracingDepth`), shadows/reflections, filmic tone mapping (`ToneMappingMethod`), depth of field (`CameraApertureRadius`/`FocalPlaneDist`), PBR shading (`ShadingModel = Pbr`), packed environment cubemaps (`SetBackgroundCubeMap` + `UseEnvironmentMapBackground`), and per-shape PBR materials with auto-derived BSDF for path tracing (`Graphic3d_PBRMaterial::SetBSDF(Graphic3d_BSDF::CreateMetallicRoughness(...))`). The high-level `ot_viewer_set_render_preset(DRAFT|BALANCED|PHOTOREALISTIC)` is a one-call shortcut over these. `ot_viewer_create` applies BALANCED by default (raster + shadows + reflections + 8× MSAA + filmic tone mapping) so the out-of-box render is presentable without further configuration.

**Error reporting.** Functions return `NULL`/`false` and write a message to `thread_local std::string g_last_error`. Callers retrieve it via `occt_templot_last_error()`. All public-facing implementations should `try { … } catch (Standard_Failure& e) { g_last_error = …; return …; }` around OCCT calls — OCCT throws on bad input.

**Diagnostic tracing.** Off by default. Enable via `OCCT_TEMPLOT_TRACE=1` env var or by calling `ot_set_trace(true)` from a host language. When on, `OT_TRACE(...)` lines from `occt_templot_*.cpp` flush to stderr immediately (so a hung call is still visible), and OCCT's own `Message::DefaultMessenger()` is wired to stderr so internal `ShapeFix` / `Sewing` / `BRepCheck` output surfaces too. Helper lives in `src/occt_templot_trace.{h,cpp}`. Two macros: `OT_TRACE("fmt", args...)` for point-in-time messages, and `OT_TRACE_TIMER("label")` placed near the top of a function for a RAII timer that reports `[ot] label: <ms> ms` on every return path. When adding a new long-running function, bracket the OCCT call with `OT_TRACE("ot_foo: phase X start")` / `OT_TRACE("ot_foo: phase X done")` plus an `OT_TRACE_TIMER("ot_foo")` so total time and per-phase boundaries are both in the log.

**Memory ownership.** Anything returned from `ot_*` that isn't a primitive must be paired with a free function: `ot_shape_free`, `ot_mesh_free`, `ot_edge_mesh_free`, `ot_viewer_destroy`, `ot_camera_destroy`, `ot_drawer_destroy`. The `OTMeshData` / `OTEdgeMeshData` structs hold heap-allocated arrays — the corresponding free functions zero out the struct so double-free is safe.

## OCCT dependency

`scripts/build-occt-deps.sh` clones OCCT at tag `V8_0_0` from `Open-Cascade-SAS/OCCT` and builds it as **static** libraries with a curated module subset (DataExchange, FoundationClasses, ModelingAlgorithms, ModelingData, Visualization). FreeType, FreeImage, RapidJSON, TBB, VTK, Draco, FFmpeg, OpenVR, Tcl are all disabled to keep the dependency minimal. Linux builds add `USE_XLIB=ON`. The script tolerates executable-link failures (`|| true`) because we only need the static `.a`/`.lib` files.

**Known build quirk (rc5/beta1/final):** OCCT 8.0.0's generated Makefiles intermittently skip the `mkdir` for object subdirectories (e.g. `TKShHealing.dir/ShapeAlgo/`, `ShapeProcessAPI/`) under high `-j` parallelism, causing `error: unable to open output file ... 'No such file or directory'`. Workaround: re-run `cmake --build . --parallel N` from inside `deps/occt-build` until it converges, or pre-create the missing subdirs with `mkdir -p`. The script does not yet auto-recover from this, so a fresh clean build may need 1–2 retries.

`CMakeLists.txt` searches `${OCCT_INSTALL_DIR}` for headers under `include/opencascade`, `include`, or Windows-style `inc/`; libraries under `lib/` or `win64/vc14/lib/`. The required toolkit list is hand-maintained at `OCCT_LIBS`. **Linux linking** wraps these archives in `-Wl,--start-group … -Wl,--end-group` because OCCT's static archives have circular dependencies — adding a new `TK*` toolkit on Linux often surfaces as an unresolved-symbol error rather than a missing-library error; check the OCCT_LIBS list first. macOS/Windows do not need the group wrapping.

**Platform link extras.** macOS pulls in `Foundation`, `AppKit`, `OpenGL`, `IOKit` frameworks plus `objc`. Windows links `ws2_32 opengl32 glu32 user32 gdi32`. Linux links `OpenGL::GL X11 dl pthread`.

## Conventions when extending

- **Adding an exported function:** declare in `src/occt_templot.h` with `OT_EXPORT`, implement in the matching module `.cpp` inside `extern "C" { … }`, mirror the signature in `pascal/occt_templot.pas`. Without `OT_EXPORT` the symbol won't be visible at link time.
- **Adding an OCCT toolkit dependency:** append to `OCCT_LIBS` in `CMakeLists.txt`. If it isn't built by `build-occt-deps.sh`'s module selection, you'll need to enable the corresponding `BUILD_MODULE_*` there too.
- **Tests** are plain `add_executable` registered with `add_test` — no framework. Each test prints PASS/FAIL and returns nonzero on failure. Tests can take optional CLI args pointing at sample files; with no args they fall back to in-memory geometry round-trips.
