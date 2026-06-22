---
type: reference
title: References index
resource: https://github.com/SecondMouseAU/simpleOCCTVP
tags: [index, references]
description: Upstream, rendering, and licensing references for simpleOCCTVP.
timestamp: 2026-06-22
---

# References

- **OpenCASCADE Technology (OCCT)** — the underlying C++ kernel (OCCT 8.0.0), wrapped by this
  library. <https://dev.opencascade.org/> / <https://github.com/Open-Cascade-SAS/OCCT>
- **CADRays** — the OCCT GPU rendering application whose path-tracing path
  (`Graphic3d_RenderingParams`) the rendering-quality API mirrors.
  <https://github.com/Open-Cascade-SAS/CADRays>
- **API header** — `src/occt_templot.h` is the canonical C API reference; `pascal/occt_templot.pas`
  is the Pascal binding.
- **Licensing** — LGPL-2.1 with the Open CASCADE Exception, mirroring OCCT; see `LICENSE`,
  `OCCT_LGPL_EXCEPTION.txt`, and `NOTICE` in the repo root.
