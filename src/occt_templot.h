/*
 * occt_templot.h — Public C API for occtTemplot
 *
 * Cross-platform OCCT wrapper library for Templot.
 * Provides STL/STEP/IGES/OBJ I/O, shape healing, mesh extraction,
 * shape analysis, offscreen 3D rendering, and standalone camera/edge mesh
 * via a pure C interface suitable for Pascal FFI.
 *
 * All functions use cdecl calling convention.
 * All returned OTShapeRef handles must be freed with ot_shape_free().
 */

#ifndef OCCT_TEMPLOT_H
#define OCCT_TEMPLOT_H

#include <stdint.h>
#include <stdbool.h>

/* Export/import macros */
#ifdef _WIN32
    #ifdef OCCT_TEMPLOT_BUILDING
        #define OT_EXPORT __declspec(dllexport)
    #else
        #define OT_EXPORT __declspec(dllimport)
    #endif
#else
    #define OT_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Lifecycle
 * ================================================================ */

/** Initialize the library. Call once before any other function. */
OT_EXPORT void occt_templot_init(void);

/** Shutdown the library and release global resources. */
OT_EXPORT void occt_templot_shutdown(void);

/** Return the library version string (e.g. "0.1.0"). */
OT_EXPORT const char* occt_templot_version(void);

/** Return the last error message, or NULL if no error. Thread-local. */
OT_EXPORT const char* occt_templot_last_error(void);

/* ================================================================
 * Shape Handle (opaque)
 * ================================================================ */

/** Opaque handle to a shape. */
typedef void* OTShapeRef;

/** Free a shape handle. Safe to call with NULL. */
OT_EXPORT void ot_shape_free(OTShapeRef shape);

/** Check if a shape handle is valid (non-null, contains valid geometry). */
OT_EXPORT bool ot_shape_is_valid(OTShapeRef shape);

/**
 * Get the shape type.
 * Returns: 0=Compound, 1=CompSolid, 2=Solid, 3=Shell, 4=Face,
 *          5=Wire, 6=Edge, 7=Vertex, 8=Shape, -1=error
 */
OT_EXPORT int32_t ot_shape_type(OTShapeRef shape);

/* ================================================================
 * I/O — Import
 * ================================================================ */

/** Import an STL file. Returns shape handle or NULL on failure. */
OT_EXPORT OTShapeRef ot_import_stl(const char* path);

/**
 * Import an STL file with automatic healing (sew + solid creation + heal).
 * @param path File path
 * @param sewing_tolerance Tolerance for sewing (use 1e-6 for default)
 */
OT_EXPORT OTShapeRef ot_import_stl_robust(const char* path, double sewing_tolerance);

/** Import a STEP file. Returns shape handle or NULL on failure. */
OT_EXPORT OTShapeRef ot_import_step(const char* path);

/**
 * Import a STEP file with automatic healing (sewing, solid creation, healing).
 * Configures reader for better precision handling.
 */
OT_EXPORT OTShapeRef ot_import_step_robust(const char* path);

/** Import an IGES file. Returns shape handle or NULL on failure. */
OT_EXPORT OTShapeRef ot_import_iges(const char* path);

/** Import an IGES file with automatic healing. */
OT_EXPORT OTShapeRef ot_import_iges_robust(const char* path);

/** Import an OBJ file. Returns shape handle or NULL on failure. */
OT_EXPORT OTShapeRef ot_import_obj(const char* path);

/* ================================================================
 * I/O — Import with diagnostics
 * ================================================================ */

/** Diagnostic result from STEP import. */
typedef struct {
    OTShapeRef shape;       /**< Result shape (NULL on failure) */
    int32_t original_type;  /**< TopAbs_ShapeEnum before processing */
    int32_t result_type;    /**< TopAbs_ShapeEnum after processing */
    bool sewing_applied;    /**< Whether sewing was needed */
    bool solid_created;     /**< Whether solid was created from shell */
    bool healing_applied;   /**< Whether healing was applied */
} OTImportResult;

/** Import STEP file with diagnostic information. */
OT_EXPORT OTImportResult ot_import_step_with_diagnostics(const char* path);

/* ================================================================
 * I/O — Export
 * ================================================================ */

/**
 * Export shape to STL file (binary format).
 * @param shape Shape to export
 * @param path Output file path
 * @param deflection Mesh deflection (smaller = finer mesh, e.g. 0.1)
 * @return true on success
 */
OT_EXPORT bool ot_export_stl(OTShapeRef shape, const char* path, double deflection);

/**
 * Export shape to STEP file (AP214 schema).
 * @return true on success
 */
OT_EXPORT bool ot_export_step(OTShapeRef shape, const char* path);

/**
 * Export shape to IGES file.
 * @return true on success
 */
OT_EXPORT bool ot_export_iges(OTShapeRef shape, const char* path);

/**
 * Export shape to OBJ file.
 * @param deflection Mesh deflection for tessellation
 * @return true on success
 */
OT_EXPORT bool ot_export_obj(OTShapeRef shape, const char* path, double deflection);

/**
 * Export shape to PLY file (Stanford Polygon Format).
 * @param deflection Mesh deflection for tessellation
 * @return true on success
 */
OT_EXPORT bool ot_export_ply(OTShapeRef shape, const char* path, double deflection);

/* ================================================================
 * Healing & Analysis
 * ================================================================ */

/** Shape analysis result. */
typedef struct {
    int32_t small_edge_count;        /**< Edges smaller than tolerance */
    int32_t small_face_count;        /**< Faces smaller than tolerance^2 */
    int32_t gap_count;               /**< Gaps between edges/faces */
    int32_t self_intersection_count; /**< Self-intersections detected */
    int32_t free_edge_count;         /**< Unconnected edges */
    int32_t free_face_count;         /**< Free (non-closed) faces */
    bool has_invalid_topology;       /**< Topology check failed */
    bool is_valid;                   /**< Analysis completed successfully */
} OTShapeAnalysis;

/**
 * Analyze a shape for problems.
 * @param shape Shape to analyze
 * @param tolerance Tolerance for small feature detection (e.g. 1e-6)
 */
OT_EXPORT OTShapeAnalysis ot_analyze_shape(OTShapeRef shape, double tolerance);

/**
 * Heal a shape (auto-fix common issues).
 * Uses ShapeFix_Shape with default settings.
 * @return Healed shape (new handle), or NULL on failure
 */
OT_EXPORT OTShapeRef ot_heal_shape(OTShapeRef shape);

/**
 * Heal a shape with detailed control over fixing operations.
 * @param tolerance Precision for fixing
 * @param fix_solid Fix solid orientation
 * @param fix_shell Fix shell closure
 * @param fix_face Fix face issues
 * @param fix_wire Fix wire issues
 * @return Fixed shape (new handle), or NULL on failure
 */
OT_EXPORT OTShapeRef ot_heal_shape_detailed(OTShapeRef shape, double tolerance,
    bool fix_solid, bool fix_shell, bool fix_face, bool fix_wire);

/**
 * Sew disconnected faces of a shape.
 * If the result is a closed shell, attempts to create a solid.
 * @param tolerance Sewing tolerance
 * @return Sewn shape (new handle), or NULL on failure
 */
OT_EXPORT OTShapeRef ot_sew_shape(OTShapeRef shape, double tolerance);

/**
 * Upgrade a shape: sew + make solid + heal (pipeline).
 * @param tolerance Tolerance for sewing/healing
 * @return Upgraded shape (new handle), or NULL on failure
 */
OT_EXPORT OTShapeRef ot_upgrade_shape(OTShapeRef shape, double tolerance);

/**
 * Create a solid from a shell shape.
 * @return Solid shape (new handle), or NULL if not possible
 */
OT_EXPORT OTShapeRef ot_make_solid(OTShapeRef shape);

/**
 * Check if shape is a valid closed solid.
 * @return true if shape passes BRepCheck validation
 */
OT_EXPORT bool ot_shape_check_valid(OTShapeRef shape);

/* ================================================================
 * Shape Info
 * ================================================================ */

/**
 * Get volume of a shape in cubic units.
 * @return Volume, or -1.0 on error
 */
OT_EXPORT double ot_shape_volume(OTShapeRef shape);

/**
 * Get surface area of a shape in square units.
 * @return Surface area, or -1.0 on error
 */
OT_EXPORT double ot_shape_surface_area(OTShapeRef shape);

/**
 * Get axis-aligned bounding box of a shape.
 * All output pointers must be non-NULL.
 */
OT_EXPORT void ot_shape_bounding_box(OTShapeRef shape,
    double* xmin, double* ymin, double* zmin,
    double* xmax, double* ymax, double* zmax);

/* ================================================================
 * Mesh Extraction
 * ================================================================ */

/**
 * Mesh data extracted from a shape.
 * Vertices are interleaved: [x,y,z, nx,ny,nz, x,y,z, nx,ny,nz, ...]
 * (6 floats per vertex: position + normal)
 * Indices are triangle indices into the vertex array.
 */
typedef struct {
    float* vertices;        /**< Interleaved position+normal (6 floats per vertex) */
    int32_t vertex_count;   /**< Number of vertices */
    int32_t* indices;       /**< Triangle indices (3 per triangle) */
    int32_t triangle_count; /**< Number of triangles */
} OTMeshData;

/**
 * Extract mesh (triangulation) from a shape.
 * @param shape Shape to mesh
 * @param deflection Linear deflection (smaller = finer, e.g. 0.1)
 * @return Mesh data. Call ot_mesh_free() when done. Check vertex_count > 0.
 */
OT_EXPORT OTMeshData ot_mesh_shape(OTShapeRef shape, double deflection);

/**
 * Free mesh data returned by ot_mesh_shape().
 * Safe to call with a zeroed struct.
 */
OT_EXPORT void ot_mesh_free(OTMeshData* mesh);

/**
 * Separate mesh extraction: get vertices as x,y,z triples (no interleaving).
 * @param shape Shape to mesh
 * @param deflection Linear deflection
 * @param out_vertices Output: caller-allocated float array (vertex_count * 3)
 * @param out_normals Output: caller-allocated float array (vertex_count * 3)
 * @param out_indices Output: caller-allocated int32_t array (triangle_count * 3)
 * @param out_vertex_count Output: number of vertices
 * @param out_triangle_count Output: number of triangles
 * @return true on success
 */
OT_EXPORT bool ot_mesh_shape_separate(OTShapeRef shape, double deflection,
    int32_t* out_vertex_count, int32_t* out_triangle_count);

/**
 * After calling ot_mesh_shape_separate to get counts, call this to fill buffers.
 * @param shape Shape to mesh (must be same shape, already meshed)
 * @param out_vertices Caller-allocated float[vertex_count * 3]
 * @param out_normals Caller-allocated float[vertex_count * 3]
 * @param out_indices Caller-allocated int32_t[triangle_count * 3]
 * @return true on success
 */
OT_EXPORT bool ot_mesh_shape_fill(OTShapeRef shape,
    float* out_vertices, float* out_normals, int32_t* out_indices);

/* ================================================================
 * Opaque Handles — Viewer, Camera, Drawer
 * ================================================================ */

/** Opaque handle to an offscreen V3d viewer. */
typedef void* OTViewerRef;

/** Opaque handle to a standalone Graphic3d_Camera. */
typedef void* OTCameraRef;

/** Opaque handle to a Prs3d_Drawer (tessellation quality). */
typedef void* OTDrawerRef;

/* ================================================================
 * Enums
 * ================================================================ */

/** Display mode for shapes in the viewer. */
typedef enum {
    OT_DISPLAY_WIREFRAME    = 0,
    OT_DISPLAY_SHADED       = 1,
    OT_DISPLAY_SHADED_EDGES = 2
} OTDisplayMode;

/** Standard view orientations. */
typedef enum {
    OT_VIEW_FRONT    = 0,
    OT_VIEW_BACK     = 1,
    OT_VIEW_TOP      = 2,
    OT_VIEW_BOTTOM   = 3,
    OT_VIEW_LEFT     = 4,
    OT_VIEW_RIGHT    = 5,
    OT_VIEW_ISO      = 6,
    OT_VIEW_ISO_LEFT = 7
} OTViewOrientation;

/** Camera projection type. */
typedef enum {
    OT_PROJECTION_PERSPECTIVE   = 0,
    OT_PROJECTION_ORTHOGRAPHIC  = 1
} OTProjectionType;

/** Rendering method (rasterization vs ray tracing). */
typedef enum {
    OT_RENDER_RASTERIZATION = 0,
    OT_RENDER_RAYTRACING    = 1
} OTRenderingMethod;

/** Tone mapping method applied to the final image. */
typedef enum {
    OT_TONEMAP_DISABLED = 0,
    OT_TONEMAP_FILMIC   = 1
} OTToneMappingMethod;

/** Surface shading model. PBR/PBR_FACET expect physically-based materials. */
typedef enum {
    OT_SHADING_DEFAULT   = 0,  /**< Phong (current OCCT default) */
    OT_SHADING_PHONG     = 1,
    OT_SHADING_PBR       = 2,  /**< Physically-based (smooth normals) */
    OT_SHADING_PBR_FACET = 3,  /**< Physically-based (faceted normals) */
    OT_SHADING_UNLIT     = 4   /**< No lighting; albedo only */
} OTShadingModel;

/**
 * High-level rendering quality preset.
 * DRAFT          — rasterization, no shadows/reflections, 4x MSAA. Fastest interactive path.
 * BALANCED       — rasterization + shadows + reflections + 8x MSAA + filmic tone mapping. Default.
 * PHOTOREALISTIC — ray traced path tracing (256 spp, 8 bounces) + filmic tone mapping. Slow.
 */
typedef enum {
    OT_PRESET_DRAFT          = 0,
    OT_PRESET_BALANCED       = 1,
    OT_PRESET_PHOTOREALISTIC = 2
} OTRenderPreset;

/* ================================================================
 * Edge Mesh Data
 * ================================================================ */

/**
 * Edge polyline data extracted from a shape.
 * vertices: [x,y,z, ...] — 3 floats per vertex
 * segment_starts: index where each edge polyline begins in the vertex array
 */
typedef struct {
    float*   vertices;       /**< [x,y,z, ...] 3 floats per vertex */
    int32_t  vertex_count;   /**< Total number of vertices */
    int32_t* segment_starts; /**< Index where each edge polyline begins */
    int32_t  segment_count;  /**< Number of edge segments */
} OTEdgeMeshData;

/* ================================================================
 * Offscreen Viewer — Lifecycle
 * ================================================================ */

/**
 * Create an offscreen V3d viewer.
 * @param width  Framebuffer width in pixels
 * @param height Framebuffer height in pixels
 * @return Viewer handle, or NULL on failure
 */
OT_EXPORT OTViewerRef ot_viewer_create(int32_t width, int32_t height);

/** Destroy a viewer and release all resources. Safe to call with NULL. */
OT_EXPORT void ot_viewer_destroy(OTViewerRef viewer);

/** Resize the viewer framebuffer. */
OT_EXPORT bool ot_viewer_resize(OTViewerRef viewer, int32_t width, int32_t height);

/** Get current viewer dimensions. */
OT_EXPORT void ot_viewer_get_size(OTViewerRef viewer, int32_t* out_width, int32_t* out_height);

/* ================================================================
 * Offscreen Viewer — Shape Display
 * ================================================================ */

/**
 * Add a shape to the viewer.
 * @param shape Shape to display
 * @param mode  Display mode (wireframe, shaded, shaded+edges)
 * @return Display ID (>0) for later reference, or -1 on failure
 */
OT_EXPORT int32_t ot_viewer_add_shape(OTViewerRef viewer, OTShapeRef shape, OTDisplayMode mode);

/** Remove a displayed shape by its display_id. */
OT_EXPORT bool ot_viewer_remove_shape(OTViewerRef viewer, int32_t display_id);

/** Remove all displayed shapes. */
OT_EXPORT void ot_viewer_clear(OTViewerRef viewer);

/** Set the color of a displayed shape (RGB 0.0-1.0). */
OT_EXPORT bool ot_viewer_set_shape_color(OTViewerRef viewer, int32_t display_id,
    double r, double g, double b);

/** Set the transparency of a displayed shape (0.0=opaque, 1.0=transparent). */
OT_EXPORT bool ot_viewer_set_shape_transparency(OTViewerRef viewer, int32_t display_id, double t);

/** Change the display mode of a displayed shape. */
OT_EXPORT bool ot_viewer_set_shape_display_mode(OTViewerRef viewer, int32_t display_id,
    OTDisplayMode mode);

/**
 * Set the material of a displayed shape.
 * @param name Material name: "brass", "bronze", "copper", "gold", "pewter",
 *             "silver", "steel", "stone", "chrome", "aluminium", "plastic", "default"
 */
OT_EXPORT bool ot_viewer_set_shape_material(OTViewerRef viewer, int32_t display_id, const char* name);

/* ================================================================
 * Offscreen Viewer — Background
 * ================================================================ */

/** Set solid background color (RGB 0.0-1.0). */
OT_EXPORT void ot_viewer_set_background_color(OTViewerRef viewer, double r, double g, double b);

/** Set gradient background (top color, bottom color). */
OT_EXPORT void ot_viewer_set_background_gradient(OTViewerRef viewer,
    double r1, double g1, double b1, double r2, double g2, double b2);

/* ================================================================
 * Offscreen Viewer — Camera Control
 * ================================================================ */

/** Set projection type (perspective or orthographic). */
OT_EXPORT void ot_viewer_set_projection(OTViewerRef viewer, OTProjectionType type);

/** Set field of view in degrees (perspective mode). */
OT_EXPORT void ot_viewer_set_fov(OTViewerRef viewer, double degrees);

/** Set a standard view orientation. */
OT_EXPORT void ot_viewer_set_view_orientation(OTViewerRef viewer, OTViewOrientation orient);

/** Fit all displayed shapes in the view. */
OT_EXPORT void ot_viewer_fit_all(OTViewerRef viewer);

/** Rotate the view by pixel delta. */
OT_EXPORT void ot_viewer_rotate(OTViewerRef viewer, double dx, double dy);

/** Pan the view by pixel delta. */
OT_EXPORT void ot_viewer_pan(OTViewerRef viewer, double dx, double dy);

/** Zoom by a factor (>1 = zoom in, <1 = zoom out). */
OT_EXPORT void ot_viewer_zoom(OTViewerRef viewer, double factor);

/** Zoom toward/away from a specific screen point. */
OT_EXPORT void ot_viewer_zoom_at_point(OTViewerRef viewer,
    int32_t x, int32_t y, double factor);

/** Set camera position explicitly (eye, center, up). */
OT_EXPORT void ot_viewer_set_camera(OTViewerRef viewer,
    double eye_x, double eye_y, double eye_z,
    double center_x, double center_y, double center_z,
    double up_x, double up_y, double up_z);

/** Get current camera position (eye, center, up). */
OT_EXPORT void ot_viewer_get_camera(OTViewerRef viewer,
    double* eye_x, double* eye_y, double* eye_z,
    double* center_x, double* center_y, double* center_z,
    double* up_x, double* up_y, double* up_z);

/* ================================================================
 * Offscreen Viewer — Lighting
 * ================================================================ */

/** Enable/disable the default headlight. */
OT_EXPORT void ot_viewer_set_headlight(OTViewerRef viewer, bool enabled);

/** Add a directional light with specified direction and color. */
OT_EXPORT void ot_viewer_add_directional_light(OTViewerRef viewer,
    double dir_x, double dir_y, double dir_z,
    double r, double g, double b);

/** Set ambient light intensity (0.0-1.0). */
OT_EXPORT void ot_viewer_set_ambient_light(OTViewerRef viewer, double intensity);

/* ================================================================
 * Offscreen Viewer — Rendering
 * ================================================================ */

/**
 * Render the scene and return RGBA pixel data (bottom-to-top, OpenGL convention).
 * The returned pointer is valid until the next render call or viewer destruction.
 * @param out_width  Output: image width
 * @param out_height Output: image height
 * @param out_size   Output: total buffer size in bytes
 * @return Pointer to RGBA pixel data, or NULL on failure
 */
OT_EXPORT const uint8_t* ot_viewer_render(OTViewerRef viewer,
    int32_t* out_width, int32_t* out_height, int32_t* out_size);

/**
 * Render the scene to a caller-provided buffer (top-to-bottom for TBitmap).
 * @param buffer      Caller-allocated buffer (width * height * 4 bytes)
 * @param buffer_size Size of the buffer in bytes
 * @return true on success
 */
OT_EXPORT bool ot_viewer_render_to_buffer(OTViewerRef viewer,
    uint8_t* buffer, int32_t buffer_size);

/**
 * Render the scene and save directly to an image file.
 * Format determined by extension (.bmp always supported; .png if FreeImage available).
 */
OT_EXPORT bool ot_viewer_save_image(OTViewerRef viewer, const char* path);

/* ================================================================
 * Offscreen Viewer — Display Settings
 * ================================================================ */

/** Enable/disable face boundary (edge) display on all shapes. */
OT_EXPORT void ot_viewer_set_edge_display(OTViewerRef viewer, bool enabled);

/** Set the edge display color (RGB 0.0-1.0). */
OT_EXPORT void ot_viewer_set_edge_color(OTViewerRef viewer, double r, double g, double b);

/** Set mesh deflection for subsequently added shapes. */
OT_EXPORT void ot_viewer_set_deflection(OTViewerRef viewer, double deflection);

/** Set MSAA sample count (0, 2, 4, 8). Rasterization only. */
OT_EXPORT void ot_viewer_set_msaa(OTViewerRef viewer, int32_t samples);

/* ================================================================
 * Offscreen Viewer — Rendering Quality (CADRays-style)
 * ================================================================
 *
 * These knobs map onto OCCT's Graphic3d_RenderingParams. They configure
 * the same GPU path that the Open CASCADE CADRays application uses for
 * physically-based visualization. All are no-ops on viewers created
 * without a working OpenGL context.
 */

/**
 * Apply a rendering quality preset. This is a one-call shortcut that
 * configures method, shadows, reflections, AA, samples per pixel, and
 * tone mapping in one go. After calling, individual setters below can
 * still be used to override specific knobs.
 */
OT_EXPORT void ot_viewer_set_render_preset(OTViewerRef viewer, OTRenderPreset preset);

/** Switch between OpenGL rasterization and ray-traced rendering. */
OT_EXPORT void ot_viewer_set_rendering_method(OTViewerRef viewer, OTRenderingMethod method);

/**
 * Toggle path-traced global illumination. Requires OT_RENDER_RAYTRACING.
 * When enabled, the renderer performs progressive path tracing using
 * the configured samples-per-pixel and ray-depth.
 */
OT_EXPORT void ot_viewer_set_path_tracing(OTViewerRef viewer, bool enabled);

/** Samples per pixel for path tracing (e.g. 1=preview, 64=fast, 256=quality). */
OT_EXPORT void ot_viewer_set_samples_per_pixel(OTViewerRef viewer, int32_t spp);

/** Maximum ray recursion depth (3=fast, 8=balanced, 16=glass-heavy scenes). */
OT_EXPORT void ot_viewer_set_ray_depth(OTViewerRef viewer, int32_t depth);

/**
 * Enable shadows. transparent_shadows allows light to filter through
 * transparent materials (slower; requires ray tracing for full effect).
 */
OT_EXPORT void ot_viewer_set_shadows(OTViewerRef viewer, bool enabled, bool transparent_shadows);

/** Enable specular reflections. Most pronounced in ray tracing mode. */
OT_EXPORT void ot_viewer_set_reflections(OTViewerRef viewer, bool enabled);

/** Enable adaptive anti-aliasing (separate from the MSAA sample count). */
OT_EXPORT void ot_viewer_set_antialiasing(OTViewerRef viewer, bool enabled);

/**
 * Configure tone mapping for HDR-to-LDR conversion of the rendered image.
 * @param method      OT_TONEMAP_DISABLED (raw) or OT_TONEMAP_FILMIC
 * @param exposure    EV adjustment (0.0 = unchanged, +1.0 = 2x brighter)
 * @param white_point White-point luminance for filmic mapping (1.0 = default)
 */
OT_EXPORT void ot_viewer_set_tone_mapping(OTViewerRef viewer,
    OTToneMappingMethod method, double exposure, double white_point);

/**
 * Configure depth of field (path tracing only).
 * @param aperture_radius  Lens aperture in scene units (0.0 = pinhole/disabled)
 * @param focal_distance   Distance from camera to focal plane in scene units
 */
OT_EXPORT void ot_viewer_set_depth_of_field(OTViewerRef viewer,
    double aperture_radius, double focal_distance);

/** Set the surface shading model used for all subsequently rendered shapes. */
OT_EXPORT void ot_viewer_set_shading_model(OTViewerRef viewer, OTShadingModel model);

/**
 * Set an environment cubemap from a single packed image (3x2 or 4x3 cross
 * layout, auto-detected by OCCT). The cubemap provides image-based
 * lighting for PBR shading and replaces the solid/gradient background
 * when the environment background is enabled.
 */
OT_EXPORT bool ot_viewer_set_environment_cubemap(OTViewerRef viewer, const char* image_path);

/** Clear any previously set environment cubemap. */
OT_EXPORT void ot_viewer_clear_environment_cubemap(OTViewerRef viewer);

/** Whether the environment cubemap is drawn as the background. */
OT_EXPORT void ot_viewer_set_environment_background(OTViewerRef viewer, bool enabled);

/**
 * Set a physically-based material on a displayed shape. Affects both
 * rasterized PBR shading and ray-traced rendering (BSDF auto-derived).
 * @param albedo_r,g,b  Base color in linear RGB [0..1]
 * @param metallic      Metallic-ness [0=dielectric, 1=metal]
 * @param roughness     Surface roughness [0=mirror, 1=fully diffuse]
 */
OT_EXPORT bool ot_viewer_set_shape_pbr_material(OTViewerRef viewer, int32_t display_id,
    double albedo_r, double albedo_g, double albedo_b,
    double metallic, double roughness);

/**
 * Set the emissive component of a PBR shape (acts as an area light in
 * path tracing). intensity scales the color (0 = non-emissive).
 */
OT_EXPORT bool ot_viewer_set_shape_emission(OTViewerRef viewer, int32_t display_id,
    double r, double g, double b, double intensity);

/* ================================================================
 * Standalone Camera — Lifecycle
 * ================================================================ */

/** Create a standalone camera (zero-to-one depth, column-major matrices). */
OT_EXPORT OTCameraRef ot_camera_create(void);

/** Destroy a camera. Safe to call with NULL. */
OT_EXPORT void ot_camera_destroy(OTCameraRef camera);

/* ================================================================
 * Standalone Camera — Set/Get
 * ================================================================ */

OT_EXPORT void ot_camera_set_eye(OTCameraRef camera, double x, double y, double z);
OT_EXPORT void ot_camera_get_eye(OTCameraRef camera, double* x, double* y, double* z);
OT_EXPORT void ot_camera_set_center(OTCameraRef camera, double x, double y, double z);
OT_EXPORT void ot_camera_get_center(OTCameraRef camera, double* x, double* y, double* z);
OT_EXPORT void ot_camera_set_up(OTCameraRef camera, double x, double y, double z);
OT_EXPORT void ot_camera_get_up(OTCameraRef camera, double* x, double* y, double* z);
OT_EXPORT void ot_camera_set_projection_type(OTCameraRef camera, OTProjectionType type);
OT_EXPORT OTProjectionType ot_camera_get_projection_type(OTCameraRef camera);
OT_EXPORT void ot_camera_set_fov(OTCameraRef camera, double degrees);
OT_EXPORT double ot_camera_get_fov(OTCameraRef camera);
OT_EXPORT void ot_camera_set_scale(OTCameraRef camera, double scale);
OT_EXPORT double ot_camera_get_scale(OTCameraRef camera);
OT_EXPORT void ot_camera_set_z_range(OTCameraRef camera, double z_near, double z_far);
OT_EXPORT void ot_camera_set_aspect(OTCameraRef camera, double aspect);

/* ================================================================
 * Standalone Camera — Matrices (column-major, zero-to-one depth)
 * ================================================================ */

/** Get the 4x4 projection matrix (column-major, 16 doubles). */
OT_EXPORT bool ot_camera_get_projection_matrix(OTCameraRef camera, double* out16);

/** Get the 4x4 view (orientation) matrix (column-major, 16 doubles). */
OT_EXPORT bool ot_camera_get_view_matrix(OTCameraRef camera, double* out16);

/* ================================================================
 * Standalone Camera — Project / Unproject
 * ================================================================ */

/** Project a world point to NDC coordinates. */
OT_EXPORT bool ot_camera_project(OTCameraRef camera,
    double world_x, double world_y, double world_z,
    double* out_x, double* out_y, double* out_z);

/** Unproject NDC coordinates to a world point. */
OT_EXPORT bool ot_camera_unproject(OTCameraRef camera,
    double ndc_x, double ndc_y, double ndc_z,
    double* out_x, double* out_y, double* out_z);

/** Fit the camera to a bounding box. */
OT_EXPORT bool ot_camera_fit_bbox(OTCameraRef camera,
    double xmin, double ymin, double zmin,
    double xmax, double ymax, double zmax);

/* ================================================================
 * Edge Mesh Extraction
 * ================================================================ */

/**
 * Extract edge polylines from a shape.
 * @param shape Shape to extract edges from
 * @param deflection Tessellation deflection
 * @return Edge mesh data. Call ot_edge_mesh_free() when done.
 */
OT_EXPORT OTEdgeMeshData ot_edge_mesh_shape(OTShapeRef shape, double deflection);

/** Free edge mesh data. Safe to call with a zeroed struct. */
OT_EXPORT void ot_edge_mesh_free(OTEdgeMeshData* mesh);

/* ================================================================
 * Display Drawer (tessellation quality control)
 * ================================================================ */

/** Create a display drawer with default settings. */
OT_EXPORT OTDrawerRef ot_drawer_create(void);

/** Destroy a drawer. Safe to call with NULL. */
OT_EXPORT void ot_drawer_destroy(OTDrawerRef drawer);

OT_EXPORT void   ot_drawer_set_deviation_coefficient(OTDrawerRef drawer, double coeff);
OT_EXPORT double ot_drawer_get_deviation_coefficient(OTDrawerRef drawer);
OT_EXPORT void   ot_drawer_set_deviation_angle(OTDrawerRef drawer, double radians);
OT_EXPORT double ot_drawer_get_deviation_angle(OTDrawerRef drawer);
OT_EXPORT void   ot_drawer_set_max_deviation(OTDrawerRef drawer, double deviation);
OT_EXPORT double ot_drawer_get_max_deviation(OTDrawerRef drawer);
OT_EXPORT void   ot_drawer_set_face_boundary_draw(OTDrawerRef drawer, bool enabled);
OT_EXPORT bool   ot_drawer_get_face_boundary_draw(OTDrawerRef drawer);

/** Extract triangle mesh using drawer settings for tessellation. */
OT_EXPORT OTMeshData ot_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer);

/** Extract edge mesh using drawer settings for tessellation. */
OT_EXPORT OTEdgeMeshData ot_edge_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer);

#ifdef __cplusplus
}
#endif

#endif /* OCCT_TEMPLOT_H */
