/*
 * test_builder.cpp — Tests the parametric BRep builder API.
 *
 * Exercises every ot_component_* constructor + ot_builder_place +
 * ot_builder_finalize, then writes a STEP file with one of each
 * primitive plus a small assembly that re-uses a component.
 */

#include "occt_templot.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; printf("  TEST: %s ... ", name); } while (0)
#define PASS() \
    do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); } while (0)

static void check_component(const char* name, OTComponentRef c) {
    TEST(name);
    if (c >= 0) {
        printf("(ref=%d) ", c);
        PASS();
    } else {
        const char* e = occt_templot_last_error();
        FAIL(e ? e : "returned -1");
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    printf("=== occt_templot builder tests ===\n\n");

    occt_templot_init();

    TEST("ot_builder_create");
    OTBuilderRef b = ot_builder_create();
    if (b) PASS(); else { FAIL("returned NULL"); return 1; }

    // -- one of each primitive ----------------------------------------------
    OTComponentRef cone = ot_component_cone(b, 0.55, 0.35, 2.0);
    check_component("ot_component_cone (nail-shaped)", cone);

    OTComponentRef box = ot_component_box(b, 4.0, 6.0, 1.5);
    check_component("ot_component_box (timber-shaped)", box);

    OTComponentRef cyl = ot_component_cylinder(b, 0.8, 3.0);
    check_component("ot_component_cylinder (bolt boss)", cyl);

    OTComponentRef hex = ot_component_hex_prism(b, 2.4, 1.0);
    check_component("ot_component_hex_prism (M-something nut)", hex);

    // L-shaped extruded profile
    const double L_xy[] = {0, 0,  3, 0,  3, 1,  1, 1,  1, 3,  0, 3};
    OTComponentRef Lprof = ot_component_extrude(b, L_xy, 6, 1.0);
    check_component("ot_component_extrude (L-section)", Lprof);

    // Wedge profile revolved about Z (a chamfered washer)
    const double rz[] = {0, 0,  2, 0,  2, 0.5,  1, 1.0,  0, 1.0};
    OTComponentRef revol = ot_component_revolve(b, rz, 5);
    check_component("ot_component_revolve (chamfered washer)", revol);

    OTComponentRef post = ot_component_pyramid(b, 5.0, 5.0, 2.0, 2.0, 4.0);
    check_component("ot_component_pyramid (support post)", post);

    // Rail-shaped profile (very simplified: just a tee), swept along a
    // gentle curve. Validates the pipe API used for real rails in Day 6.
    const double tee_xy[] = { -3, 0,  3, 0,  3, 1,  0.5, 1,  0.5, 4,  -0.5, 4,  -0.5, 1,  -3, 1 };
    double curve[5 * 3];
    for (int i = 0; i < 5; ++i) {
        curve[3 * i + 0] = i * 10.0;
        curve[3 * i + 1] = 0.5 * i * i;   // gentle parabola
        curve[3 * i + 2] = 0.0;
    }
    OTComponentRef rail = ot_component_pipe_sweep(b, tee_xy, 8, curve, 5);
    check_component("ot_component_pipe_sweep (curved rail)", rail);

    // Drill a hole through the box
    TEST("ot_component_subtract_cylinder (drill box)");
    bool ok = ot_component_subtract_cylinder(b, box, 2.0, 3.0, 0.5, 1.5);
    if (ok) PASS(); else FAIL("subtract failed");

    TEST("ot_component_set_color (cone red)");
    ok = ot_component_set_color(b, cone, 0.9, 0.2, 0.1);
    if (ok) PASS(); else FAIL("set_color failed");

    // -- placement: one of each primitive in a row + a small bed of nails --
    TEST("ot_builder_place (one of each, X-row)");
    OTComponentRef row[] = { cone, box, cyl, hex, Lprof, revol, post };
    bool all_placed = true;
    for (size_t i = 0; i < sizeof(row) / sizeof(row[0]); ++i) {
        if (!ot_builder_place(b, row[i], i * 10.0, 0.0, 0.0, 0.0, "row")) {
            all_placed = false; break;
        }
    }
    if (all_placed) PASS(); else FAIL("a place call failed");

    TEST("ot_builder_place (12-nail circle, reused cone)");
    all_placed = true;
    for (int i = 0; i < 12; ++i) {
        double t = (2.0 * M_PI * i) / 12;
        if (!ot_builder_place(b, cone, 30.0 + 5.0 * std::cos(t), 5.0 * std::sin(t),
                              0.0, t, "nails")) {
            all_placed = false; break;
        }
    }
    if (all_placed) PASS(); else FAIL("a place call failed");

    // -- finalize and export -------------------------------------------------
    TEST("ot_builder_finalize");
    OTShapeRef shape = ot_builder_finalize(b);
    if (shape) PASS(); else { FAIL("returned NULL"); return 1; }

    TEST("ot_shape_is_valid");
    if (ot_shape_is_valid(shape)) PASS(); else FAIL("invalid shape");

    const char* step_path = "/tmp/test_builder_output.step";
    TEST("ot_export_step");
    if (ot_export_step(shape, step_path)) PASS(); else FAIL("export failed");

    // Tag a couple of components for XCAF, then export the assembly STEP
    // with hierarchy + colour preserved.
    ot_component_set_name(b, cone, "nail");
    ot_component_set_name(b, box, "timber");
    ot_component_set_color(b, box, 0.55, 0.27, 0.07);  // brown
    ot_component_set_name(b, hex, "nut");

    const char* asm_path = "/tmp/test_builder_assembly.step";
    TEST("ot_export_step_assembly (XCAF)");
    if (ot_export_step_assembly(b, asm_path)) PASS(); else FAIL("assembly export failed");

    ot_shape_free(shape);
    ot_builder_destroy(b);

    // -- failure-path checks ------------------------------------------------
    TEST("ot_builder_place out-of-range component");
    OTBuilderRef b2 = ot_builder_create();
    if (!ot_builder_place(b2, /*invalid*/ 999, 0, 0, 0, 0, "x")) PASS();
    else FAIL("expected false");
    ot_builder_destroy(b2);

    TEST("ot_component_extrude n_pts<3");
    OTBuilderRef b3 = ot_builder_create();
    double bad[] = {0, 0, 1, 1};
    if (ot_component_extrude(b3, bad, 2, 1.0) < 0) PASS();
    else FAIL("expected -1");
    ot_builder_destroy(b3);

    occt_templot_shutdown();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
