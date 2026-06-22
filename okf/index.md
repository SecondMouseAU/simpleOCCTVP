---
type: repo
title: simpleOCCTVP
resource: https://github.com/SecondMouseAU/simpleOCCTVP
tags: [occt, opencascade, c-api, ffi, cad, rendering, cpp, kernel]
description: A C++17 shared library with a pure C (cdecl) API over OpenCASCADE — shape I/O, healing, mesh extraction, and offscreen rendering.
timestamp: 2026-06-22
---

# simpleOCCTVP

A **C++17 shared library** providing a **pure C API** (cdecl) over
[OpenCASCADE](https://dev.opencascade.org/) (OCCT 8.0.0), built with CMake. It is designed for
easy FFI consumption from Pascal, C, or any language with cdecl support, and ships prebuilt for
macOS (arm64), Windows (x64), and Linux (x64).

All exported functions use the cdecl calling convention and are prefixed with `ot_` (with library
lifecycle under `occt_templot_*`). The full API is declared in `src/occt_templot.h`.

## Role in the ecosystem

- **Cluster:** kernel
- **Depends on:** nothing intra-org — it wraps OCCT 8.0.0 directly (static libs built via
  `scripts/build-occt-deps.sh`).
- **Feeds products:** none declared yet; the C-ABI entry point for non-Swift / cross-language
  consumers of the OCCT kernel (e.g. Pascal / Lazarus, plain C).

## Components

See [`components/`](components/index.md) — the `simpleOCCTVP` shared library and its C API
surface, grouped by domain (shape I/O, healing, mesh, offscreen viewer, camera, rendering).

## References

See [`references/`](references/index.md) — OpenCASCADE upstream, CADRays rendering path, and
licensing (LGPL-2.1 + OCCT exception).

## Notes

- Build from source: `scripts/build-occt-deps.sh` (builds OCCT 8.0.0 static, ~15–30 min), then
  `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
- Consumable via direct link, CMake `add_subdirectory` / imported target, or runtime
  `dlopen` / `LoadLibrary`.
- A Pascal binding (`pascal/occt_templot.pas`) ships alongside the C header.
