/*
 * occt_templot_builder.cpp — Parametric BRep builder for Templot.
 *
 * Templot computes 3D parts (chairs, jaws, seats, nails, bolts, rails, …)
 * parametrically in Pascal then expands them to ASCII STL triangles. This
 * module replaces the STL emit path: callers register part *components*
 * once, then *place* instances of those components by (translation +
 * yaw-rotation) transforms. Component reuse via TopLoc_Location keeps the
 * resulting compound — and any STEP export of it — orders of magnitude
 * smaller than the equivalent triangle soup.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"
#include "occt_templot_trace.h"

#include <vector>
#include <string>

#include <cmath>

#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <Interface_Static.hxx>
#include <TDataStd_Name.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_TypeOfColor.hxx>
#include <TDF_Label.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

/* ================================================================
 * Internal state
 * ================================================================ */

struct OTComponentEntry {
    TopoDS_Shape shape;          // the unique component geometry
    std::string  name;           // identifier for XCAF labels (defaults to "comp_N")
    double       r{0.65}, g{0.70}, b{0.80};  // default colour, applied at XCAF export time
    bool         color_set{false};
};

// Placement = (component index, transform, optional layer/group tag).
// Tracked separately from the compound so that ot_export_step_assembly can
// rebuild the proper XCAF assembly tree at export time.
struct OTPlacementEntry {
    OTComponentRef component;
    gp_Trsf        transform;
    std::string    layer;
};

struct OTBuilderInternal {
    std::vector<OTComponentEntry>  components;
    std::vector<OTPlacementEntry>  placements;
    TopoDS_Compound                compound;   // flat compound — backwards-compat path
    BRep_Builder                   builder;

    OTBuilderInternal() {
        builder.MakeCompound(compound);
    }
};

extern "C" {

/* ================================================================
 * Lifecycle
 * ================================================================ */

OT_EXPORT OTBuilderRef ot_builder_create(void) {
    OT_TRACE("ot_builder_create: enter");
    try {
        return static_cast<OTBuilderRef>(new OTBuilderInternal());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_builder_create: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_builder_create: unknown exception";
        return nullptr;
    }
}

OT_EXPORT void ot_builder_destroy(OTBuilderRef builder) {
    if (!builder) return;
    OT_TRACE("ot_builder_destroy: enter");
    delete static_cast<OTBuilderInternal*>(builder);
}

OT_EXPORT OTShapeRef ot_builder_finalize(OTBuilderRef builder) {
    if (!builder) {
        g_last_error = "ot_builder_finalize: builder is NULL";
        return nullptr;
    }
    OT_TRACE("ot_builder_finalize: enter");
    OT_TRACE_TIMER("ot_builder_finalize");
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        // The compound has been accumulated in-place by ot_builder_place
        // calls. We hand back a copy so the caller's lifetime is independent
        // of the builder; ot_shape_free releases it.
        return static_cast<OTShapeRef>(new OTShapeInternal(b->compound));
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_builder_finalize: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_builder_finalize: unknown exception";
        return nullptr;
    }
}

/* ================================================================
 * Component constructors
 *
 * Each constructor builds a unique part definition once and returns
 * an OTComponentRef (a non-negative index into the builder's component
 * vector). Negative return = error, see occt_templot_last_error().
 * ================================================================ */

// Internal: register a freshly-built shape as a component and return its
// index. Used by every ot_component_* entry point.
static OTComponentRef register_component(OTBuilderInternal* b,
                                         const TopoDS_Shape& s) {
    OTComponentEntry entry;
    entry.shape = s;
    b->components.push_back(std::move(entry));
    return static_cast<OTComponentRef>(b->components.size() - 1);
}

OT_EXPORT OTComponentRef ot_component_cone(OTBuilderRef builder,
                                           double rbot, double rtop, double h) {
    if (!builder) {
        g_last_error = "ot_component_cone: builder is NULL";
        return -1;
    }
    OT_TRACE("ot_component_cone: rbot=%g rtop=%g h=%g", rbot, rtop, h);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        BRepPrimAPI_MakeCone mk(rbot, rtop, h);
        mk.Build();
        if (!mk.IsDone()) {
            g_last_error = "ot_component_cone: MakeCone failed";
            return -1;
        }
        return register_component(b, mk.Shape());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_cone: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_cone: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_box(OTBuilderRef builder,
                                          double dx, double dy, double dz) {
    if (!builder) { g_last_error = "ot_component_box: builder is NULL"; return -1; }
    OT_TRACE("ot_component_box: dx=%g dy=%g dz=%g", dx, dy, dz);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        BRepPrimAPI_MakeBox mk(dx, dy, dz);
        mk.Build();
        if (!mk.IsDone()) {
            g_last_error = "ot_component_box: MakeBox failed";
            return -1;
        }
        return register_component(b, mk.Shape());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_box: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_box: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_cylinder(OTBuilderRef builder,
                                               double r, double h) {
    if (!builder) { g_last_error = "ot_component_cylinder: builder is NULL"; return -1; }
    OT_TRACE("ot_component_cylinder: r=%g h=%g", r, h);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        BRepPrimAPI_MakeCylinder mk(r, h);
        mk.Build();
        if (!mk.IsDone()) {
            g_last_error = "ot_component_cylinder: MakeCylinder failed";
            return -1;
        }
        return register_component(b, mk.Shape());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_cylinder: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_cylinder: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_hex_prism(OTBuilderRef builder,
                                                double across_flats, double h) {
    if (!builder) { g_last_error = "ot_component_hex_prism: builder is NULL"; return -1; }
    OT_TRACE("ot_component_hex_prism: af=%g h=%g", across_flats, h);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        // A regular hexagon's vertex-radius (centre to corner) = (across_flats / 2) / cos(30°).
        const double r = (across_flats * 0.5) / std::cos(M_PI / 6.0);
        BRepBuilderAPI_MakePolygon poly;
        for (int i = 0; i < 6; ++i) {
            double a = (M_PI / 3.0) * i;  // 60° spacing, first vertex along +X
            poly.Add(gp_Pnt(r * std::cos(a), r * std::sin(a), 0.0));
        }
        poly.Close();
        TopoDS_Wire wire = poly.Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire).Face();
        TopoDS_Shape solid = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, h)).Shape();
        return register_component(b, solid);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_hex_prism: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_hex_prism: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_extrude(OTBuilderRef builder,
                                              const double* xy, int32_t n_pts,
                                              double depth) {
    if (!builder) { g_last_error = "ot_component_extrude: builder is NULL"; return -1; }
    if (!xy || n_pts < 3) {
        g_last_error = "ot_component_extrude: need at least 3 (x,y) points";
        return -1;
    }
    OT_TRACE("ot_component_extrude: n_pts=%d depth=%g", n_pts, depth);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        BRepBuilderAPI_MakePolygon poly;
        for (int32_t i = 0; i < n_pts; ++i) {
            poly.Add(gp_Pnt(xy[2 * i], xy[2 * i + 1], 0.0));
        }
        poly.Close();
        TopoDS_Wire wire = poly.Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire).Face();
        TopoDS_Shape solid = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, depth)).Shape();
        return register_component(b, solid);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_extrude: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_extrude: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_revolve(OTBuilderRef builder,
                                              const double* rz, int32_t n_pts) {
    if (!builder) { g_last_error = "ot_component_revolve: builder is NULL"; return -1; }
    if (!rz || n_pts < 2) {
        g_last_error = "ot_component_revolve: need at least 2 (r,z) points";
        return -1;
    }
    OT_TRACE("ot_component_revolve: n_pts=%d", n_pts);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        // Build a wire in the XZ plane from the (r, z) profile, then revolve
        // about Z. The first and last points must touch the Z axis (r=0)
        // for a closed solid; we close with a back-edge along Z if needed.
        BRepBuilderAPI_MakeWire wire_mk;
        for (int32_t i = 1; i < n_pts; ++i) {
            gp_Pnt p1(rz[2 * (i - 1)], 0.0, rz[2 * (i - 1) + 1]);
            gp_Pnt p2(rz[2 * i],       0.0, rz[2 * i + 1]);
            wire_mk.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        }
        // Close back to the first point if the profile isn't already closed.
        gp_Pnt first(rz[0], 0.0, rz[1]);
        gp_Pnt last(rz[2 * (n_pts - 1)], 0.0, rz[2 * (n_pts - 1) + 1]);
        if (first.Distance(last) > 1e-9) {
            wire_mk.Add(BRepBuilderAPI_MakeEdge(last, first).Edge());
        }
        TopoDS_Wire wire = wire_mk.Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)), wire).Face();
        gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        TopoDS_Shape solid = BRepPrimAPI_MakeRevol(face, axis).Shape();
        return register_component(b, solid);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_revolve: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_revolve: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_pyramid(OTBuilderRef builder,
                                              double base_w, double base_l,
                                              double top_w, double top_l,
                                              double h) {
    if (!builder) { g_last_error = "ot_component_pyramid: builder is NULL"; return -1; }
    OT_TRACE("ot_component_pyramid: base=%gx%g top=%gx%g h=%g",
             base_w, base_l, top_w, top_l, h);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        // Base rectangle in XY, top rectangle at z=h, ThruSections lofts a solid
        // with the two flat caps. Useful for the support post / frustum shapes.
        const double bw = base_w * 0.5, bl = base_l * 0.5;
        const double tw = top_w  * 0.5, tl = top_l  * 0.5;

        BRepBuilderAPI_MakePolygon base;
        base.Add(gp_Pnt(-bw, -bl, 0));
        base.Add(gp_Pnt( bw, -bl, 0));
        base.Add(gp_Pnt( bw,  bl, 0));
        base.Add(gp_Pnt(-bw,  bl, 0));
        base.Close();

        BRepBuilderAPI_MakePolygon top;
        top.Add(gp_Pnt(-tw, -tl, h));
        top.Add(gp_Pnt( tw, -tl, h));
        top.Add(gp_Pnt( tw,  tl, h));
        top.Add(gp_Pnt(-tw,  tl, h));
        top.Close();

        BRepOffsetAPI_ThruSections loft(/*solid=*/Standard_True, /*ruled=*/Standard_True);
        loft.AddWire(base.Wire());
        loft.AddWire(top.Wire());
        loft.Build();
        if (!loft.IsDone()) {
            g_last_error = "ot_component_pyramid: ThruSections failed";
            return -1;
        }
        return register_component(b, loft.Shape());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_pyramid: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_pyramid: unknown exception";
        return -1;
    }
}

OT_EXPORT OTComponentRef ot_component_pipe_sweep(OTBuilderRef builder,
                                                 const double* profile_xy,
                                                 int32_t n_profile,
                                                 const double* path_xyz,
                                                 int32_t n_path) {
    if (!builder) { g_last_error = "ot_component_pipe_sweep: builder is NULL"; return -1; }
    if (!profile_xy || n_profile < 3 || !path_xyz || n_path < 2) {
        g_last_error = "ot_component_pipe_sweep: profile needs ≥3 pts, path needs ≥2";
        return -1;
    }
    OT_TRACE("ot_component_pipe_sweep: profile=%d pts, path=%d pts",
             n_profile, n_path);
    OT_TRACE_TIMER("ot_component_pipe_sweep");
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);

        // Profile wire — closed polygon in the XZ plane (Y=0). The first
        // path edge becomes the local +Y direction so the profile sweeps
        // in the path's normal plane.
        BRepBuilderAPI_MakePolygon profile_poly;
        for (int32_t i = 0; i < n_profile; ++i) {
            profile_poly.Add(gp_Pnt(profile_xy[2 * i], 0.0, profile_xy[2 * i + 1]));
        }
        profile_poly.Close();
        TopoDS_Wire  profile_wire = profile_poly.Wire();
        TopoDS_Face  profile_face = BRepBuilderAPI_MakeFace(profile_wire).Face();

        // Path wire from the polyline of (x, y, z) points.
        BRepBuilderAPI_MakeWire path_mk;
        for (int32_t i = 1; i < n_path; ++i) {
            gp_Pnt p1(path_xyz[3 * (i - 1)],
                      path_xyz[3 * (i - 1) + 1],
                      path_xyz[3 * (i - 1) + 2]);
            gp_Pnt p2(path_xyz[3 * i],
                      path_xyz[3 * i + 1],
                      path_xyz[3 * i + 2]);
            if (p1.Distance(p2) < 1e-12) continue;   // skip degenerate
            path_mk.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        }
        TopoDS_Wire path_wire = path_mk.Wire();

        BRepOffsetAPI_MakePipe pipe(path_wire, profile_face);
        pipe.Build();
        if (!pipe.IsDone()) {
            g_last_error = "ot_component_pipe_sweep: MakePipe failed";
            return -1;
        }

        return register_component(b, pipe.Shape());
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_pipe_sweep: ") + e.what();
        return -1;
    } catch (...) {
        g_last_error = "ot_component_pipe_sweep: unknown exception";
        return -1;
    }
}

OT_EXPORT bool ot_component_subtract_cylinder(OTBuilderRef builder,
                                              OTComponentRef target,
                                              double cx, double cy,
                                              double r, double h) {
    if (!builder) { g_last_error = "ot_component_subtract_cylinder: builder is NULL"; return false; }
    OT_TRACE("ot_component_subtract_cylinder: target=%d cx=%g cy=%g r=%g h=%g",
             target, cx, cy, r, h);
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        if (target < 0 ||
            static_cast<size_t>(target) >= b->components.size()) {
            g_last_error = "ot_component_subtract_cylinder: target out of range";
            return false;
        }
        gp_Ax2 axis(gp_Pnt(cx, cy, 0.0), gp_Dir(0, 0, 1));
        BRepPrimAPI_MakeCylinder hole(axis, r, h);
        BRepAlgoAPI_Cut cut(b->components[target].shape, hole.Shape());
        cut.Build();
        if (!cut.IsDone()) {
            g_last_error = "ot_component_subtract_cylinder: Cut failed";
            return false;
        }
        b->components[target].shape = cut.Shape();
        return true;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_component_subtract_cylinder: ") + e.what();
        return false;
    } catch (...) {
        g_last_error = "ot_component_subtract_cylinder: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_component_set_color(OTBuilderRef builder,
                                      OTComponentRef component,
                                      double r, double g, double b_) {
    if (!builder) { g_last_error = "ot_component_set_color: builder is NULL"; return false; }
    auto* bi = static_cast<OTBuilderInternal*>(builder);
    if (component < 0 ||
        static_cast<size_t>(component) >= bi->components.size()) {
        g_last_error = "ot_component_set_color: component out of range";
        return false;
    }
    auto& e = bi->components[component];
    e.r = r; e.g = g; e.b = b_; e.color_set = true;
    return true;
}

OT_EXPORT bool ot_component_set_name(OTBuilderRef builder,
                                     OTComponentRef component,
                                     const char* name) {
    if (!builder) { g_last_error = "ot_component_set_name: builder is NULL"; return false; }
    auto* bi = static_cast<OTBuilderInternal*>(builder);
    if (component < 0 ||
        static_cast<size_t>(component) >= bi->components.size()) {
        g_last_error = "ot_component_set_name: component out of range";
        return false;
    }
    bi->components[component].name = name ? name : "";
    return true;
}

/* ================================================================
 * Assembly export — STEP via XCAF
 *
 * Builds an XCAF document with one part per registered component (named
 * + coloured), then references those parts at every recorded placement.
 * The resulting STEP file preserves the assembly tree and per-component
 * colours, so a downstream CAD tool (FreeCAD etc.) sees real instances
 * rather than a flattened compound.
 * ================================================================ */

OT_EXPORT bool ot_export_step_assembly(OTBuilderRef builder, const char* path) {
    if (!builder || !path) {
        g_last_error = "ot_export_step_assembly: builder or path is NULL";
        return false;
    }
    OT_TRACE("ot_export_step_assembly: enter (%s)", path);
    OT_TRACE_TIMER("ot_export_step_assembly");

    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);

        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("BinXCAF", doc);

        Handle(XCAFDoc_ShapeTool) shapeTool =
            XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        Handle(XCAFDoc_ColorTool) colorTool =
            XCAFDoc_DocumentTool::ColorTool(doc->Main());

        // Register each unique component as a free-standing labelled part
        // and remember the label so we can place instances of it.
        std::vector<TDF_Label> partLabels;
        partLabels.reserve(b->components.size());

        for (size_t i = 0; i < b->components.size(); ++i) {
            const auto& e = b->components[i];
            TDF_Label part = shapeTool->AddShape(e.shape, /*makeAssembly=*/false);
            partLabels.push_back(part);

            std::string label = e.name.empty()
                ? ("comp_" + std::to_string(i)) : e.name;
            TDataStd_Name::Set(part, TCollection_ExtendedString(label.c_str()));

            if (e.color_set) {
                Quantity_Color colour(e.r, e.g, e.b, Quantity_TOC_RGB);
                colorTool->SetColor(part, colour, XCAFDoc_ColorSurf);
                colorTool->SetColor(part, colour, XCAFDoc_ColorGen);
            }
        }

        // Build a top-level assembly that references each part at the
        // recorded placement. Each AddComponent gives us a new instance
        // (a TopLoc_Location attached to the part label); STEPCAFControl
        // emits these as proper assembly references.
        TDF_Label asmLabel = shapeTool->NewShape();
        TDataStd_Name::Set(asmLabel, TCollection_ExtendedString("Templot"));

        for (const auto& p : b->placements) {
            if (p.component < 0 ||
                static_cast<size_t>(p.component) >= partLabels.size()) continue;
            shapeTool->AddComponent(asmLabel, partLabels[p.component],
                                    TopLoc_Location(p.transform));
        }

        shapeTool->UpdateAssemblies();

        Interface_Static::SetCVal("write.step.schema", "AP214");

        STEPCAFControl_Writer writer;
        writer.SetColorMode(Standard_True);
        writer.SetNameMode(Standard_True);
        writer.SetLayerMode(Standard_True);

        OT_TRACE("ot_export_step_assembly: Transfer start (%zu parts, %zu placements)",
                 b->components.size(), b->placements.size());
        if (!writer.Transfer(doc, STEPControl_AsIs)) {
            g_last_error = "ot_export_step_assembly: Transfer failed";
            return false;
        }

        IFSelect_ReturnStatus status = writer.Write(path);
        OT_TRACE("ot_export_step_assembly: Write done status=%d", (int)status);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_export_step_assembly: Write failed";
            return false;
        }
        g_last_error.clear();
        return true;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_step_assembly: ") + e.what();
        return false;
    } catch (...) {
        g_last_error = "ot_export_step_assembly: unknown exception";
        return false;
    }
}

/* ================================================================
 * Placement
 *
 * Apply (rotation around Z by yaw, then translation by (x,y,z)) to the
 * named component and add the located shape to the builder's compound.
 * Cheap — uses TopLoc_Location, no shape duplication.
 * ================================================================ */

OT_EXPORT bool ot_builder_place(OTBuilderRef builder,
                                OTComponentRef component,
                                double x, double y, double z, double yaw,
                                const char* layer) {
    if (!builder) {
        g_last_error = "ot_builder_place: builder is NULL";
        return false;
    }
    try {
        auto* b = static_cast<OTBuilderInternal*>(builder);
        if (component < 0 ||
            static_cast<size_t>(component) >= b->components.size()) {
            g_last_error = "ot_builder_place: component out of range";
            return false;
        }

        gp_Trsf rot;
        if (yaw != 0.0) {
            rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), yaw);
        }
        gp_Trsf trans;
        trans.SetTranslation(gp_Vec(x, y, z));

        gp_Trsf full = trans * rot;
        TopLoc_Location loc(full);

        TopoDS_Shape placed =
            b->components[component].shape.Located(loc);

        b->builder.Add(b->compound, placed);

        // Also record the placement explicitly so ot_export_step_assembly
        // can rebuild the proper XCAF assembly tree.
        OTPlacementEntry pe;
        pe.component = component;
        pe.transform = full;
        if (layer && *layer) pe.layer = layer;
        b->placements.push_back(std::move(pe));

        return true;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_builder_place: ") + e.what();
        return false;
    } catch (...) {
        g_last_error = "ot_builder_place: unknown exception";
        return false;
    }
}

} // extern "C"
