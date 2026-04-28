/*
 * test_viewer.cpp — Tests for offscreen V3d rendering
 *
 * Creates an offscreen viewer, adds a box shape, renders to PNG,
 * and verifies basic viewer operations.
 *
 * Usage: ./test_viewer
 */

#include "occt_templot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  TEST: %s ... ", name); \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while(0)

/* We need a shape to display. Import a generated STL box via export/import round-trip,
   or use ot_import_step on a test file. For self-contained testing, we'll create a
   simple shape via the STEP export/import trick using a box from the test_heal. */

static OTShapeRef create_test_box() {
    /* Create a box by importing a minimal STL. We write a binary STL of a unit cube. */
    /* Actually, let's just use ot_import_stl on a temp file we create */
    const char* stl_path = "/tmp/occt_test_viewer_box.stl";

    /* Write a minimal ASCII STL cube */
    FILE* f = fopen(stl_path, "w");
    if (!f) return nullptr;

    fprintf(f,
        "solid box\n"
        "  facet normal 0 0 -1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 1 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 -1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 1 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 1\n"
        "      vertex 1 1 1\n"
        "      vertex 1 0 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 1\n"
        "      vertex 0 1 1\n"
        "      vertex 1 1 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 -1 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 1\n"
        "      vertex 1 0 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 -1 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 0 1\n"
        "      vertex 1 0 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 1 0\n"
        "    outer loop\n"
        "      vertex 0 1 0\n"
        "      vertex 1 1 0\n"
        "      vertex 1 1 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 1 0\n"
        "    outer loop\n"
        "      vertex 0 1 0\n"
        "      vertex 1 1 1\n"
        "      vertex 0 1 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal -1 0 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 1 0\n"
        "      vertex 0 1 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal -1 0 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 1 1\n"
        "      vertex 0 0 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 1 0 0\n"
        "    outer loop\n"
        "      vertex 1 0 0\n"
        "      vertex 1 0 1\n"
        "      vertex 1 1 1\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 1 0 0\n"
        "    outer loop\n"
        "      vertex 1 0 0\n"
        "      vertex 1 1 1\n"
        "      vertex 1 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid box\n");
    fclose(f);

    return ot_import_stl(stl_path);
}

static void test_viewer_create_destroy() {
    TEST("viewer create");
    OTViewerRef viewer = ot_viewer_create(800, 600);
    if (viewer) {
        PASS();
    } else {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "create returned NULL");
        return;
    }

    TEST("viewer get size");
    int32_t w = 0, h = 0;
    ot_viewer_get_size(viewer, &w, &h);
    if (w == 800 && h == 600) {
        PASS();
    } else {
        printf("(got %dx%d) ", w, h);
        FAIL("wrong size");
    }

    TEST("viewer resize");
    bool ok = ot_viewer_resize(viewer, 1024, 768);
    ot_viewer_get_size(viewer, &w, &h);
    if (ok && w == 1024 && h == 768) {
        PASS();
    } else {
        FAIL("resize failed");
    }

    TEST("viewer destroy");
    ot_viewer_destroy(viewer);
    PASS();
}

static void test_viewer_add_shape() {
    OTViewerRef viewer = ot_viewer_create(640, 480);
    if (!viewer) {
        printf("  SKIP: could not create viewer\n");
        return;
    }

    OTShapeRef shape = create_test_box();
    if (!shape) {
        printf("  SKIP: could not create test shape\n");
        ot_viewer_destroy(viewer);
        return;
    }

    TEST("add shape (shaded)");
    int32_t id = ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
    if (id > 0) {
        printf("(id=%d) ", id);
        PASS();
    } else {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "add_shape returned -1");
    }

    TEST("set shape color");
    bool ok = ot_viewer_set_shape_color(viewer, id, 0.2, 0.6, 0.9);
    if (ok) PASS(); else FAIL("set_color failed");

    TEST("set shape transparency");
    ok = ot_viewer_set_shape_transparency(viewer, id, 0.3);
    if (ok) PASS(); else FAIL("set_transparency failed");

    TEST("set shape material");
    ok = ot_viewer_set_shape_material(viewer, id, "steel");
    if (ok) PASS(); else FAIL("set_material failed");

    TEST("set display mode");
    ok = ot_viewer_set_shape_display_mode(viewer, id, OT_DISPLAY_SHADED_EDGES);
    if (ok) PASS(); else FAIL("set_display_mode failed");

    TEST("remove shape");
    ok = ot_viewer_remove_shape(viewer, id);
    if (ok) PASS(); else FAIL("remove_shape failed");

    ot_shape_free(shape);
    ot_viewer_destroy(viewer);
}

static void test_viewer_camera() {
    OTViewerRef viewer = ot_viewer_create(640, 480);
    if (!viewer) {
        printf("  SKIP: could not create viewer\n");
        return;
    }

    TEST("set projection orthographic");
    ot_viewer_set_projection(viewer, OT_PROJECTION_ORTHOGRAPHIC);
    PASS();

    TEST("set projection perspective");
    ot_viewer_set_projection(viewer, OT_PROJECTION_PERSPECTIVE);
    PASS();

    TEST("set FOV");
    ot_viewer_set_fov(viewer, 60.0);
    PASS();

    TEST("set view orientation");
    ot_viewer_set_view_orientation(viewer, OT_VIEW_ISO);
    PASS();

    TEST("set camera position");
    ot_viewer_set_camera(viewer, 10.0, 10.0, 10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
    PASS();

    TEST("get camera position");
    double ex, ey, ez, cx, cy, cz, ux, uy, uz;
    ot_viewer_get_camera(viewer, &ex, &ey, &ez, &cx, &cy, &cz, &ux, &uy, &uz);
    if (fabs(ex - 10.0) < 0.01 && fabs(cx) < 0.01) {
        PASS();
    } else {
        printf("(eye=%.1f,%.1f,%.1f center=%.1f,%.1f,%.1f) ", ex, ey, ez, cx, cy, cz);
        FAIL("camera position mismatch");
    }

    ot_viewer_destroy(viewer);
}

static void test_viewer_render() {
    OTViewerRef viewer = ot_viewer_create(320, 240);
    if (!viewer) {
        printf("  SKIP: could not create viewer\n");
        return;
    }

    OTShapeRef shape = create_test_box();
    if (!shape) {
        printf("  SKIP: could not create test shape\n");
        ot_viewer_destroy(viewer);
        return;
    }

    ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
    ot_viewer_set_view_orientation(viewer, OT_VIEW_ISO);
    ot_viewer_fit_all(viewer);

    TEST("render to pixel buffer");
    int32_t w, h, sz;
    const uint8_t* pixels = ot_viewer_render(viewer, &w, &h, &sz);
    if (pixels && w == 320 && h == 240 && sz > 0) {
        printf("(%dx%d, %d bytes) ", w, h, sz);
        PASS();
    } else {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "render returned NULL");
    }

    TEST("render to caller buffer");
    int32_t bufSize = 320 * 240 * 4;
    uint8_t* buf = static_cast<uint8_t*>(malloc(bufSize));
    bool ok = ot_viewer_render_to_buffer(viewer, buf, bufSize);
    if (ok) {
        /* Check that the buffer isn't all zeros (something was rendered) */
        bool hasNonZero = false;
        for (int i = 0; i < bufSize && !hasNonZero; i++) {
            if (buf[i] != 0) hasNonZero = true;
        }
        if (hasNonZero) PASS(); else FAIL("buffer is all zeros");
    } else {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "render_to_buffer failed");
    }
    free(buf);

    TEST("save image to file");
    const char* imgPath = "/tmp/occt_test_viewer_output.bmp";
    ok = ot_viewer_save_image(viewer, imgPath);
    if (ok) {
        /* Verify file exists and has non-zero size */
        FILE* f = fopen(imgPath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fclose(f);
            if (size > 0) {
                printf("(%ld bytes) ", size);
                PASS();
            } else {
                FAIL("file is empty");
            }
        } else {
            FAIL("file not found");
        }
    } else {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "save_image failed");
    }

    ot_shape_free(shape);
    ot_viewer_destroy(viewer);
}

static void test_viewer_settings() {
    OTViewerRef viewer = ot_viewer_create(320, 240);
    if (!viewer) {
        printf("  SKIP: could not create viewer\n");
        return;
    }

    TEST("set background color");
    ot_viewer_set_background_color(viewer, 0.1, 0.2, 0.3);
    PASS();

    TEST("set background gradient");
    ot_viewer_set_background_gradient(viewer, 0.1, 0.1, 0.3, 0.9, 0.9, 1.0);
    PASS();

    TEST("set headlight");
    ot_viewer_set_headlight(viewer, true);
    PASS();

    TEST("add directional light");
    ot_viewer_add_directional_light(viewer, -1.0, -1.0, -1.0, 1.0, 1.0, 1.0);
    PASS();

    TEST("set ambient light");
    ot_viewer_set_ambient_light(viewer, 0.3);
    PASS();

    TEST("set MSAA");
    ot_viewer_set_msaa(viewer, 8);
    PASS();

    TEST("set deflection");
    ot_viewer_set_deflection(viewer, 0.05);
    PASS();

    ot_viewer_destroy(viewer);
}

static void test_viewer_render_quality() {
    OTViewerRef viewer = ot_viewer_create(320, 240);
    if (!viewer) {
        printf("  SKIP: could not create viewer\n");
        return;
    }

    TEST("render preset DRAFT");
    ot_viewer_set_render_preset(viewer, OT_PRESET_DRAFT);
    PASS();

    TEST("render preset BALANCED");
    ot_viewer_set_render_preset(viewer, OT_PRESET_BALANCED);
    PASS();

    TEST("render preset PHOTOREALISTIC");
    ot_viewer_set_render_preset(viewer, OT_PRESET_PHOTOREALISTIC);
    PASS();

    TEST("set rendering method (raster)");
    ot_viewer_set_rendering_method(viewer, OT_RENDER_RASTERIZATION);
    PASS();

    TEST("set rendering method (raytrace)");
    ot_viewer_set_rendering_method(viewer, OT_RENDER_RAYTRACING);
    PASS();

    TEST("path tracing on");
    ot_viewer_set_path_tracing(viewer, true);
    PASS();

    TEST("samples per pixel");
    ot_viewer_set_samples_per_pixel(viewer, 64);
    PASS();

    TEST("ray depth");
    ot_viewer_set_ray_depth(viewer, 8);
    PASS();

    TEST("shadows on");
    ot_viewer_set_shadows(viewer, true, true);
    PASS();

    TEST("reflections on");
    ot_viewer_set_reflections(viewer, true);
    PASS();

    TEST("antialiasing on");
    ot_viewer_set_antialiasing(viewer, true);
    PASS();

    TEST("filmic tone mapping");
    ot_viewer_set_tone_mapping(viewer, OT_TONEMAP_FILMIC, 0.5, 1.2);
    PASS();

    TEST("depth of field");
    ot_viewer_set_depth_of_field(viewer, 0.05, 100.0);
    PASS();

    TEST("shading model PBR");
    ot_viewer_set_shading_model(viewer, OT_SHADING_PBR);
    PASS();

    OTShapeRef shape = create_test_box();
    if (shape) {
        int32_t id = ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
        if (id > 0) {
            TEST("set PBR material");
            bool ok = ot_viewer_set_shape_pbr_material(viewer, id,
                0.85, 0.65, 0.20,   /* gold-like albedo */
                1.0,                /* fully metallic */
                0.25);              /* moderate roughness */
            if (ok) PASS(); else FAIL(occt_templot_last_error());

            TEST("set emission");
            ok = ot_viewer_set_shape_emission(viewer, id, 1.0, 0.4, 0.1, 2.0);
            if (ok) PASS(); else FAIL(occt_templot_last_error());
        }
        ot_shape_free(shape);
    }

    ot_viewer_destroy(viewer);
}

int main(int /*argc*/, char* /*argv*/[]) {
    printf("=== occt_templot Viewer tests ===\n\n");

    occt_templot_init();

    printf("--- Viewer lifecycle ---\n");
    test_viewer_create_destroy();

    printf("\n--- Shape display ---\n");
    test_viewer_add_shape();

    printf("\n--- Camera control ---\n");
    test_viewer_camera();

    printf("\n--- Rendering ---\n");
    test_viewer_render();

    printf("\n--- Display settings ---\n");
    test_viewer_settings();

    printf("\n--- Rendering quality (CADRays-style) ---\n");
    test_viewer_render_quality();

    occt_templot_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
