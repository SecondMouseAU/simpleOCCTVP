/*
 * test_io.cpp — Basic I/O tests for occt_templot
 *
 * Tests import/export of STL, STEP, IGES formats.
 * Requires test data files in a "testdata" subdirectory
 * or creates simple shapes for round-trip testing.
 *
 * Usage: ./test_io [stl_file] [step_file]
 */

#include "occt_templot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

static void test_version() {
    TEST("version");
    const char* ver = occt_templot_version();
    if (ver && strlen(ver) > 0) {
        printf("(%s) ", ver);
        PASS();
    } else {
        FAIL("version string is null or empty");
    }
}

static void test_null_safety() {
    TEST("null safety - import");
    OTShapeRef s = ot_import_stl(nullptr);
    if (s == nullptr) PASS(); else FAIL("expected NULL");

    TEST("null safety - export");
    bool ok = ot_export_stl(nullptr, "/tmp/test.stl", 0.1);
    if (!ok) PASS(); else FAIL("expected false");

    TEST("null safety - free");
    ot_shape_free(nullptr); // Should not crash
    PASS();

    TEST("null safety - valid");
    bool valid = ot_shape_is_valid(nullptr);
    if (!valid) PASS(); else FAIL("expected false");

    TEST("null safety - volume");
    double vol = ot_shape_volume(nullptr);
    if (vol < 0) PASS(); else FAIL("expected -1.0");

    TEST("null safety - type");
    int32_t t = ot_shape_type(nullptr);
    if (t == -1) PASS(); else FAIL("expected -1");
}

static void test_nonexistent_file() {
    TEST("import nonexistent STL");
    OTShapeRef s = ot_import_stl("/nonexistent/path/file.stl");
    if (s == nullptr) {
        const char* err = occt_templot_last_error();
        if (err) printf("(error: %s) ", err);
        PASS();
    } else {
        ot_shape_free(s);
        FAIL("expected NULL for nonexistent file");
    }

    TEST("import nonexistent STEP");
    s = ot_import_step("/nonexistent/path/file.step");
    if (s == nullptr) PASS(); else { ot_shape_free(s); FAIL("expected NULL"); }

    TEST("import nonexistent IGES");
    s = ot_import_iges("/nonexistent/path/file.iges");
    if (s == nullptr) PASS(); else { ot_shape_free(s); FAIL("expected NULL"); }
}

static void test_stl_roundtrip(const char* input_path) {
    TEST("STL import");
    OTShapeRef shape = ot_import_stl(input_path);
    if (!shape) {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "import returned NULL");
        return;
    }
    PASS();

    TEST("STL shape valid");
    if (ot_shape_is_valid(shape)) PASS(); else FAIL("shape is invalid");

    TEST("STL shape type");
    int32_t type = ot_shape_type(shape);
    printf("(type=%d) ", type);
    if (type >= 0) PASS(); else FAIL("unexpected type");

    TEST("STL bounding box");
    double xmin, ymin, zmin, xmax, ymax, zmax;
    ot_shape_bounding_box(shape, &xmin, &ymin, &zmin, &xmax, &ymax, &zmax);
    printf("([%.1f,%.1f,%.1f]-[%.1f,%.1f,%.1f]) ", xmin, ymin, zmin, xmax, ymax, zmax);
    if (xmax > xmin || ymax > ymin || zmax > zmin) PASS(); else FAIL("degenerate bbox");

    // Export to temp file
    const char* tmp_path = "/tmp/occt_templot_test_output.stl";
    TEST("STL export");
    bool ok = ot_export_stl(shape, tmp_path, 0.1);
    if (ok) PASS(); else FAIL("export failed");

    // Re-import and verify
    TEST("STL re-import");
    OTShapeRef reimported = ot_import_stl(tmp_path);
    if (reimported && ot_shape_is_valid(reimported)) {
        PASS();
        ot_shape_free(reimported);
    } else {
        FAIL("re-import failed");
        if (reimported) ot_shape_free(reimported);
    }

    // Export as STEP
    const char* step_path = "/tmp/occt_templot_test_output.step";
    TEST("STL -> STEP export");
    ok = ot_export_step(shape, step_path);
    if (ok) PASS(); else FAIL("STEP export failed");

    ot_shape_free(shape);
}

static void test_step_roundtrip(const char* input_path) {
    TEST("STEP import");
    OTShapeRef shape = ot_import_step(input_path);
    if (!shape) {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "import returned NULL");
        return;
    }
    PASS();

    TEST("STEP robust import");
    OTShapeRef robust = ot_import_step_robust(input_path);
    if (robust && ot_shape_is_valid(robust)) {
        PASS();
    } else {
        FAIL("robust import failed");
    }
    if (robust) ot_shape_free(robust);

    TEST("STEP diagnostics import");
    OTImportResult diag = ot_import_step_with_diagnostics(input_path);
    if (diag.shape) {
        printf("(orig=%d result=%d sew=%d solid=%d heal=%d) ",
            diag.original_type, diag.result_type,
            diag.sewing_applied, diag.solid_created, diag.healing_applied);
        PASS();
        ot_shape_free(diag.shape);
    } else {
        FAIL("diagnostics import returned NULL shape");
    }

    // Export
    const char* tmp_path = "/tmp/occt_templot_test_step_output.step";
    TEST("STEP export");
    bool ok = ot_export_step(shape, tmp_path);
    if (ok) PASS(); else FAIL("export failed");

    // Export as STL
    const char* stl_path = "/tmp/occt_templot_test_step_to_stl.stl";
    TEST("STEP -> STL export");
    ok = ot_export_stl(shape, stl_path, 0.1);
    if (ok) PASS(); else FAIL("STL export failed");

    ot_shape_free(shape);
}

static void test_mesh_extraction(const char* input_path) {
    OTShapeRef shape = ot_import_stl(input_path);
    if (!shape) {
        printf("  SKIP mesh tests: could not import %s\n", input_path);
        return;
    }

    TEST("mesh extraction (interleaved)");
    OTMeshData mesh = ot_mesh_shape(shape, 0.1);
    if (mesh.vertex_count > 0 && mesh.triangle_count > 0) {
        printf("(verts=%d tris=%d) ", mesh.vertex_count, mesh.triangle_count);
        PASS();
    } else {
        FAIL("no mesh data");
    }
    ot_mesh_free(&mesh);

    TEST("mesh extraction (separate)");
    int32_t vcount = 0, tcount = 0;
    bool ok = ot_mesh_shape_separate(shape, 0.1, &vcount, &tcount);
    if (ok && vcount > 0 && tcount > 0) {
        printf("(verts=%d tris=%d) ", vcount, tcount);
        PASS();
    } else {
        FAIL("separate mesh count failed");
    }

    ot_shape_free(shape);
}

int main(int argc, char* argv[]) {
    printf("=== occt_templot I/O tests ===\n\n");

    occt_templot_init();

    test_version();
    test_null_safety();
    test_nonexistent_file();

    if (argc >= 2) {
        printf("\n--- STL round-trip: %s ---\n", argv[1]);
        test_stl_roundtrip(argv[1]);
        test_mesh_extraction(argv[1]);
    } else {
        printf("\n  (Pass an STL file as first argument for round-trip tests)\n");
    }

    if (argc >= 3) {
        printf("\n--- STEP round-trip: %s ---\n", argv[2]);
        test_step_roundtrip(argv[2]);
    } else {
        printf("\n  (Pass a STEP file as second argument for round-trip tests)\n");
    }

    occt_templot_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
