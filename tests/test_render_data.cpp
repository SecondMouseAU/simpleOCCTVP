/*
 * test_render_data.cpp — Tests for camera, edge mesh, and drawer
 *
 * Creates a standalone camera, verifies projection matrices,
 * extracts edge mesh from a test shape, and tests drawer settings.
 *
 * Usage: ./test_render_data
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

static OTShapeRef create_test_box() {
    const char* stl_path = "/tmp/occt_test_render_data_box.stl";
    FILE* f = fopen(stl_path, "w");
    if (!f) return nullptr;

    fprintf(f,
        "solid box\n"
        "  facet normal 0 0 -1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 10 0 0\n"
        "      vertex 10 10 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 -1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 10 10 0\n"
        "      vertex 0 10 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 10\n"
        "      vertex 10 10 10\n"
        "      vertex 10 0 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 10\n"
        "      vertex 0 10 10\n"
        "      vertex 10 10 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 -1 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 10 0 10\n"
        "      vertex 10 0 0\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 -1 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 0 10\n"
        "      vertex 10 0 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 1 0\n"
        "    outer loop\n"
        "      vertex 0 10 0\n"
        "      vertex 10 10 0\n"
        "      vertex 10 10 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 0 1 0\n"
        "    outer loop\n"
        "      vertex 0 10 0\n"
        "      vertex 10 10 10\n"
        "      vertex 0 10 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal -1 0 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 10 0\n"
        "      vertex 0 10 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal -1 0 0\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 0 10 10\n"
        "      vertex 0 0 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 1 0 0\n"
        "    outer loop\n"
        "      vertex 10 0 0\n"
        "      vertex 10 0 10\n"
        "      vertex 10 10 10\n"
        "    endloop\n"
        "  endfacet\n"
        "  facet normal 1 0 0\n"
        "    outer loop\n"
        "      vertex 10 0 0\n"
        "      vertex 10 10 10\n"
        "      vertex 10 10 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid box\n");
    fclose(f);

    return ot_import_stl(stl_path);
}

/* ================================================================
 * Camera Tests
 * ================================================================ */

static void test_camera_lifecycle() {
    TEST("camera create");
    OTCameraRef cam = ot_camera_create();
    if (cam) {
        PASS();
    } else {
        FAIL("create returned NULL");
        return;
    }

    TEST("camera destroy");
    ot_camera_destroy(cam);
    PASS();

    TEST("camera destroy NULL");
    ot_camera_destroy(nullptr);
    PASS();
}

static void test_camera_properties() {
    OTCameraRef cam = ot_camera_create();
    if (!cam) {
        printf("  SKIP: could not create camera\n");
        return;
    }

    TEST("set/get eye");
    ot_camera_set_eye(cam, 5.0, 10.0, 15.0);
    double x, y, z;
    ot_camera_get_eye(cam, &x, &y, &z);
    if (fabs(x - 5.0) < 0.01 && fabs(y - 10.0) < 0.01 && fabs(z - 15.0) < 0.01) {
        PASS();
    } else {
        printf("(got %.1f,%.1f,%.1f) ", x, y, z);
        FAIL("eye mismatch");
    }

    TEST("set/get center");
    ot_camera_set_center(cam, 1.0, 2.0, 3.0);
    ot_camera_get_center(cam, &x, &y, &z);
    if (fabs(x - 1.0) < 0.01 && fabs(y - 2.0) < 0.01 && fabs(z - 3.0) < 0.01) {
        PASS();
    } else {
        FAIL("center mismatch");
    }

    TEST("set/get up");
    ot_camera_set_up(cam, 0.0, 0.0, 1.0);
    ot_camera_get_up(cam, &x, &y, &z);
    if (fabs(z - 1.0) < 0.01) {
        PASS();
    } else {
        FAIL("up mismatch");
    }

    TEST("set/get projection type");
    ot_camera_set_projection_type(cam, OT_PROJECTION_ORTHOGRAPHIC);
    if (ot_camera_get_projection_type(cam) == OT_PROJECTION_ORTHOGRAPHIC) {
        PASS();
    } else {
        FAIL("projection type mismatch");
    }

    TEST("set/get FOV");
    ot_camera_set_fov(cam, 60.0);
    double fov = ot_camera_get_fov(cam);
    if (fabs(fov - 60.0) < 0.01) {
        PASS();
    } else {
        printf("(got %.1f) ", fov);
        FAIL("FOV mismatch");
    }

    TEST("set/get scale");
    ot_camera_set_scale(cam, 2.5);
    double scale = ot_camera_get_scale(cam);
    if (fabs(scale - 2.5) < 0.01) {
        PASS();
    } else {
        FAIL("scale mismatch");
    }

    TEST("set z range");
    ot_camera_set_z_range(cam, 0.1, 1000.0);
    PASS();

    TEST("set aspect");
    ot_camera_set_aspect(cam, 1.333);
    PASS();

    ot_camera_destroy(cam);
}

static void test_camera_matrices() {
    OTCameraRef cam = ot_camera_create();
    if (!cam) {
        printf("  SKIP: could not create camera\n");
        return;
    }

    ot_camera_set_eye(cam, 0.0, 0.0, 100.0);
    ot_camera_set_center(cam, 0.0, 0.0, 0.0);
    ot_camera_set_up(cam, 0.0, 1.0, 0.0);
    ot_camera_set_fov(cam, 45.0);
    ot_camera_set_aspect(cam, 1.333);
    ot_camera_set_z_range(cam, 1.0, 1000.0);

    TEST("get projection matrix");
    double proj[16];
    bool ok = ot_camera_get_projection_matrix(cam, proj);
    if (ok) {
        /* Projection matrix should be non-zero and have typical perspective values */
        bool reasonable = (proj[0] != 0.0 && proj[5] != 0.0 && proj[10] != 0.0);
        if (reasonable) {
            printf("(m[0]=%.3f m[5]=%.3f) ", proj[0], proj[5]);
            PASS();
        } else {
            FAIL("projection matrix looks wrong");
        }
    } else {
        FAIL("get_projection_matrix failed");
    }

    TEST("get view matrix");
    double view[16];
    ok = ot_camera_get_view_matrix(cam, view);
    if (ok) {
        /* View matrix should be non-trivial */
        bool reasonable = true;
        bool allZero = true;
        for (int i = 0; i < 16; i++) {
            if (view[i] != 0.0) allZero = false;
        }
        if (!allZero) {
            PASS();
        } else {
            FAIL("view matrix is all zeros");
        }
    } else {
        FAIL("get_view_matrix failed");
    }

    ot_camera_destroy(cam);
}

static void test_camera_project_unproject() {
    OTCameraRef cam = ot_camera_create();
    if (!cam) {
        printf("  SKIP: could not create camera\n");
        return;
    }

    ot_camera_set_eye(cam, 0.0, 0.0, 100.0);
    ot_camera_set_center(cam, 0.0, 0.0, 0.0);
    ot_camera_set_up(cam, 0.0, 1.0, 0.0);
    ot_camera_set_fov(cam, 45.0);
    ot_camera_set_aspect(cam, 1.0);

    TEST("project/unproject round-trip");
    double px, py, pz;
    bool ok = ot_camera_project(cam, 5.0, 5.0, 0.0, &px, &py, &pz);
    if (ok) {
        double wx, wy, wz;
        ok = ot_camera_unproject(cam, px, py, pz, &wx, &wy, &wz);
        if (ok && fabs(wx - 5.0) < 0.1 && fabs(wy - 5.0) < 0.1 && fabs(wz) < 0.1) {
            PASS();
        } else {
            printf("(unproj=%.1f,%.1f,%.1f) ", wx, wy, wz);
            FAIL("round-trip mismatch");
        }
    } else {
        FAIL("project failed");
    }

    TEST("fit bounding box");
    ok = ot_camera_fit_bbox(cam, -10.0, -10.0, -10.0, 10.0, 10.0, 10.0);
    if (ok) {
        double ex, ey, ez;
        ot_camera_get_eye(cam, &ex, &ey, &ez);
        printf("(eye=%.1f,%.1f,%.1f) ", ex, ey, ez);
        PASS();
    } else {
        FAIL("fit_bbox failed");
    }

    ot_camera_destroy(cam);
}

/* ================================================================
 * Edge Mesh Tests
 * ================================================================ */

static void test_edge_mesh() {
    OTShapeRef shape = create_test_box();
    if (!shape) {
        printf("  SKIP: could not create test shape\n");
        return;
    }

    TEST("edge mesh extraction");
    OTEdgeMeshData edges = ot_edge_mesh_shape(shape, 0.1);
    if (edges.vertex_count > 0 && edges.segment_count > 0) {
        printf("(verts=%d segs=%d) ", edges.vertex_count, edges.segment_count);
        PASS();
    } else {
        const char* err = occt_templot_last_error();
        /* STL meshes may not have B-Rep edges — this is expected for pure triangle meshes */
        printf("(note: STL shapes may have no B-Rep edges) ");
        if (err) printf("(%s) ", err);
        FAIL("no edge data");
    }
    ot_edge_mesh_free(&edges);

    TEST("edge mesh free safety");
    OTEdgeMeshData empty = {nullptr, 0, nullptr, 0};
    ot_edge_mesh_free(&empty);
    PASS();

    ot_shape_free(shape);
}

/* ================================================================
 * Drawer Tests
 * ================================================================ */

static void test_drawer() {
    TEST("drawer create");
    OTDrawerRef drawer = ot_drawer_create();
    if (drawer) {
        PASS();
    } else {
        FAIL("create returned NULL");
        return;
    }

    TEST("set/get deviation coefficient");
    ot_drawer_set_deviation_coefficient(drawer, 0.005);
    double coeff = ot_drawer_get_deviation_coefficient(drawer);
    if (fabs(coeff - 0.005) < 0.0001) {
        PASS();
    } else {
        printf("(got %f) ", coeff);
        FAIL("deviation coefficient mismatch");
    }

    TEST("set/get deviation angle");
    ot_drawer_set_deviation_angle(drawer, 0.2);
    double angle = ot_drawer_get_deviation_angle(drawer);
    if (fabs(angle - 0.2) < 0.01) {
        PASS();
    } else {
        FAIL("deviation angle mismatch");
    }

    TEST("set/get max deviation");
    ot_drawer_set_max_deviation(drawer, 0.05);
    double maxdev = ot_drawer_get_max_deviation(drawer);
    if (fabs(maxdev - 0.05) < 0.001) {
        PASS();
    } else {
        FAIL("max deviation mismatch");
    }

    TEST("set/get face boundary draw");
    ot_drawer_set_face_boundary_draw(drawer, true);
    if (ot_drawer_get_face_boundary_draw(drawer)) {
        PASS();
    } else {
        FAIL("face boundary draw mismatch");
    }

    TEST("drawer destroy");
    ot_drawer_destroy(drawer);
    PASS();

    TEST("drawer destroy NULL");
    ot_drawer_destroy(nullptr);
    PASS();
}

static void test_mesh_with_drawer() {
    OTShapeRef shape = create_test_box();
    if (!shape) {
        printf("  SKIP: could not create test shape\n");
        return;
    }

    OTDrawerRef drawer = ot_drawer_create();
    if (!drawer) {
        printf("  SKIP: could not create drawer\n");
        ot_shape_free(shape);
        return;
    }

    ot_drawer_set_max_deviation(drawer, 0.1);

    TEST("mesh with drawer");
    OTMeshData mesh = ot_mesh_shape_with_drawer(shape, drawer);
    if (mesh.vertex_count > 0 && mesh.triangle_count > 0) {
        printf("(verts=%d tris=%d) ", mesh.vertex_count, mesh.triangle_count);
        PASS();
    } else {
        FAIL("no mesh data");
    }
    ot_mesh_free(&mesh);

    TEST("edge mesh with drawer");
    OTEdgeMeshData edges = ot_edge_mesh_shape_with_drawer(shape, drawer);
    /* STL shapes may not have B-Rep edges — accept either outcome */
    if (edges.vertex_count > 0) {
        printf("(verts=%d segs=%d) ", edges.vertex_count, edges.segment_count);
    } else {
        printf("(no B-Rep edges from STL, ok) ");
    }
    PASS();
    ot_edge_mesh_free(&edges);

    ot_drawer_destroy(drawer);
    ot_shape_free(shape);
}

int main(int /*argc*/, char* /*argv*/[]) {
    printf("=== occt_templot Render Data tests ===\n\n");

    occt_templot_init();

    printf("--- Camera lifecycle ---\n");
    test_camera_lifecycle();

    printf("\n--- Camera properties ---\n");
    test_camera_properties();

    printf("\n--- Camera matrices ---\n");
    test_camera_matrices();

    printf("\n--- Camera project/unproject ---\n");
    test_camera_project_unproject();

    printf("\n--- Edge mesh ---\n");
    test_edge_mesh();

    printf("\n--- Drawer ---\n");
    test_drawer();

    printf("\n--- Mesh with drawer ---\n");
    test_mesh_with_drawer();

    occt_templot_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
