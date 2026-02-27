/*
 * test_heal.cpp — Healing & analysis tests for occt_templot
 *
 * Tests shape analysis, healing, sewing, and solid creation.
 *
 * Usage: ./test_heal [stl_file]
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

static void test_heal_null_safety() {
    TEST("heal null shape");
    OTShapeRef healed = ot_heal_shape(nullptr);
    if (healed == nullptr) PASS(); else FAIL("expected NULL");

    TEST("analyze null shape");
    OTShapeAnalysis analysis = ot_analyze_shape(nullptr, 1e-6);
    if (!analysis.is_valid) PASS(); else FAIL("expected invalid");

    TEST("sew null shape");
    OTShapeRef sewn = ot_sew_shape(nullptr, 1e-6);
    if (sewn == nullptr) PASS(); else FAIL("expected NULL");

    TEST("upgrade null shape");
    OTShapeRef upgraded = ot_upgrade_shape(nullptr, 1e-6);
    if (upgraded == nullptr) PASS(); else FAIL("expected NULL");

    TEST("make_solid null shape");
    OTShapeRef solid = ot_make_solid(nullptr);
    if (solid == nullptr) PASS(); else FAIL("expected NULL");

    TEST("check_valid null shape");
    bool valid = ot_shape_check_valid(nullptr);
    if (!valid) PASS(); else FAIL("expected false");
}

static void test_stl_healing(const char* path) {
    TEST("STL import for healing");
    OTShapeRef shape = ot_import_stl(path);
    if (!shape) {
        const char* err = occt_templot_last_error();
        FAIL(err ? err : "import failed");
        return;
    }
    PASS();

    // Analysis
    TEST("analyze shape");
    OTShapeAnalysis analysis = ot_analyze_shape(shape, 1e-4);
    if (analysis.is_valid) {
        printf("(small_edges=%d small_faces=%d gaps=%d free_edges=%d invalid_topo=%d) ",
            analysis.small_edge_count, analysis.small_face_count,
            analysis.gap_count, analysis.free_edge_count,
            analysis.has_invalid_topology);
        PASS();
    } else {
        FAIL("analysis failed");
    }

    // Shape info
    TEST("shape volume");
    double vol = ot_shape_volume(shape);
    printf("(%.2f) ", vol);
    PASS();

    TEST("shape surface area");
    double area = ot_shape_surface_area(shape);
    printf("(%.2f) ", area);
    PASS();

    // Heal
    TEST("heal shape");
    OTShapeRef healed = ot_heal_shape(shape);
    if (healed) {
        printf("(valid=%d) ", ot_shape_is_valid(healed));
        PASS();
    } else {
        FAIL("healing returned NULL");
    }

    // Detailed heal
    TEST("detailed heal");
    OTShapeRef detailed = ot_heal_shape_detailed(shape, 1e-4, true, true, true, true);
    if (detailed) {
        printf("(valid=%d) ", ot_shape_is_valid(detailed));
        PASS();
    } else {
        FAIL("detailed healing returned NULL");
    }

    // Sew
    TEST("sew shape");
    OTShapeRef sewn = ot_sew_shape(shape, 1e-4);
    if (sewn) {
        printf("(type=%d) ", ot_shape_type(sewn));
        PASS();
    } else {
        FAIL("sewing returned NULL");
    }

    // Upgrade pipeline
    TEST("upgrade shape (sew + solid + heal)");
    OTShapeRef upgraded = ot_upgrade_shape(shape, 1e-4);
    if (upgraded) {
        printf("(type=%d valid=%d) ", ot_shape_type(upgraded), ot_shape_is_valid(upgraded));
        PASS();
    } else {
        FAIL("upgrade returned NULL");
    }

    // Export healed shape
    TEST("export healed STL");
    bool ok = ot_export_stl(healed ? healed : shape, "/tmp/occt_templot_healed.stl", 0.1);
    if (ok) PASS(); else FAIL("export failed");

    // Robust STL import
    TEST("robust STL import");
    OTShapeRef robust = ot_import_stl_robust(path, 1e-4);
    if (robust) {
        printf("(type=%d valid=%d) ", ot_shape_type(robust), ot_shape_is_valid(robust));
        PASS();
        ot_shape_free(robust);
    } else {
        FAIL("robust import returned NULL");
    }

    // Cleanup
    if (healed) ot_shape_free(healed);
    if (detailed) ot_shape_free(detailed);
    if (sewn) ot_shape_free(sewn);
    if (upgraded) ot_shape_free(upgraded);
    ot_shape_free(shape);
}

int main(int argc, char* argv[]) {
    printf("=== occt_templot healing tests ===\n\n");

    occt_templot_init();

    test_heal_null_safety();

    if (argc >= 2) {
        printf("\n--- Healing: %s ---\n", argv[1]);
        test_stl_healing(argv[1]);
    } else {
        printf("\n  (Pass an STL file as argument for healing tests)\n");
    }

    occt_templot_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
