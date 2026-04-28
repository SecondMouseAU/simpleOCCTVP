/*
 * render_quality_demo — A/B test the rendering quality presets
 *
 * Loads a model, renders it at DRAFT / BALANCED / PHOTOREALISTIC,
 * writes out three images plus per-mode timing so you can compare
 * visual quality vs. cost.
 *
 * Usage:
 *   render_quality_demo <input.stl|step|iges> [output_prefix]
 *
 *   Default output_prefix = /tmp/render_demo
 *   Output files:
 *     <prefix>_draft.bmp
 *     <prefix>_balanced.bmp
 *     <prefix>_photorealistic.bmp
 *
 *   Optional flags:
 *     --size WxH           Image size (default 800x600)
 *     --material NAME      Built-in material (steel/gold/...) for shape
 *     --pbr R,G,B,M,R      PBR override: albedo r,g,b in [0..1], metallic, roughness
 *     --only PRESET        Render only one preset: draft|balanced|photorealistic
 */

#include "occt_templot.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static OTShapeRef import_by_extension(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return nullptr;
    std::string e(ext + 1);
    for (auto& c : e) c = static_cast<char>(tolower(c));
    if (e == "stl") return ot_import_stl(path);
    if (e == "step" || e == "stp") return ot_import_step_robust(path);
    if (e == "iges" || e == "igs") return ot_import_iges_robust(path);
    if (e == "obj") return ot_import_obj(path);
    return nullptr;
}

struct Args {
    const char* input = nullptr;
    std::string prefix = "/tmp/render_demo";
    int width = 800;
    int height = 600;
    const char* material = nullptr;
    bool pbr_set = false;
    double pbr[5] = {0.85, 0.65, 0.20, 1.0, 0.25};
    int only = -1;  // -1 = all
};

static bool parse_args(int argc, char** argv, Args& out) {
    if (argc < 2) return false;
    out.input = argv[1];
    int i = 2;
    while (i < argc) {
        std::string a = argv[i];
        if (a == "--size" && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &out.width, &out.height);
        } else if (a == "--material" && i + 1 < argc) {
            out.material = argv[++i];
        } else if (a == "--pbr" && i + 1 < argc) {
            sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf",
                &out.pbr[0], &out.pbr[1], &out.pbr[2], &out.pbr[3], &out.pbr[4]);
            out.pbr_set = true;
        } else if (a == "--only" && i + 1 < argc) {
            std::string p = argv[++i];
            if (p == "draft")               out.only = OT_PRESET_DRAFT;
            else if (p == "balanced")       out.only = OT_PRESET_BALANCED;
            else if (p == "photorealistic") out.only = OT_PRESET_PHOTOREALISTIC;
            else { fprintf(stderr, "unknown preset: %s\n", p.c_str()); return false; }
        } else if (a[0] != '-') {
            out.prefix = a;
        } else {
            fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return false;
        }
        ++i;
    }
    return true;
}

static double render_one(OTViewerRef v, OTShapeRef shape, OTRenderPreset preset,
                         const Args& args, const char* label, int32_t shape_id) {
    ot_viewer_set_render_preset(v, preset);

    // Apply per-shape material/PBR after preset (preset doesn't touch material)
    if (args.pbr_set) {
        ot_viewer_set_shape_pbr_material(v, shape_id,
            args.pbr[0], args.pbr[1], args.pbr[2], args.pbr[3], args.pbr[4]);
    } else if (args.material) {
        ot_viewer_set_shape_material(v, shape_id, args.material);
    }

    std::string outpath = args.prefix + "_" + label + ".bmp";
    auto t0 = std::chrono::steady_clock::now();
    bool ok = ot_viewer_save_image(v, outpath.c_str());
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    if (!ok) {
        printf("  %-15s FAILED: %s\n", label, occt_templot_last_error());
    } else {
        printf("  %-15s %.2fs  ->  %s\n", label, secs, outpath.c_str());
    }
    return secs;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        fprintf(stderr,
            "Usage: %s <input.stl|step|iges> [output_prefix]\n"
            "       [--size WxH] [--material NAME] [--pbr R,G,B,M,R]\n"
            "       [--only draft|balanced|photorealistic]\n", argv[0]);
        return 1;
    }

    occt_templot_init();

    printf("Loading %s ...\n", args.input);
    OTShapeRef shape = import_by_extension(args.input);
    if (!shape) {
        fprintf(stderr, "Import failed: %s\n", occt_templot_last_error());
        occt_templot_shutdown();
        return 1;
    }

    OTViewerRef viewer = ot_viewer_create(args.width, args.height);
    if (!viewer) {
        fprintf(stderr, "Viewer create failed: %s\n", occt_templot_last_error());
        ot_shape_free(shape);
        occt_templot_shutdown();
        return 1;
    }

    int32_t id = ot_viewer_add_shape(viewer, shape, OT_DISPLAY_SHADED);
    ot_viewer_set_view_orientation(viewer, OT_VIEW_ISO);
    ot_viewer_fit_all(viewer);

    printf("Rendering at %dx%d ...\n", args.width, args.height);

    if (args.only < 0 || args.only == OT_PRESET_DRAFT)
        render_one(viewer, shape, OT_PRESET_DRAFT,          args, "draft",          id);
    if (args.only < 0 || args.only == OT_PRESET_BALANCED)
        render_one(viewer, shape, OT_PRESET_BALANCED,       args, "balanced",       id);
    if (args.only < 0 || args.only == OT_PRESET_PHOTOREALISTIC)
        render_one(viewer, shape, OT_PRESET_PHOTOREALISTIC, args, "photorealistic", id);

    ot_viewer_destroy(viewer);
    ot_shape_free(shape);
    occt_templot_shutdown();
    return 0;
}
