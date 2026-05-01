/*
 * occt_templot_heal.cpp — Healing & analysis module for occtTemplot
 *
 * Shape validation, analysis, healing, sewing, and solid creation.
 * Ported from OCCTSwift's OCCTBridge.mm, stripped of Objective-C dependencies.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"
#include "occt_templot_trace.h"

// OCCT Validation & Healing
#include <BRepCheck_Analyzer.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Solid.hxx>

// Sewing & Solid Creation
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>

// Analysis
#include <ShapeAnalysis_Shell.hxx>
#include <ShapeAnalysis_Wire.hxx>
#include <ShapeAnalysis_ShapeTolerance.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

extern "C" {

/* ================================================================
 * Healing & Analysis
 * ================================================================ */

OT_EXPORT OTShapeAnalysis ot_analyze_shape(OTShapeRef shape, double tolerance) {
    OTShapeAnalysis result = {0, 0, 0, 0, 0, 0, false, false};
    if (!shape) {
        g_last_error = "ot_analyze_shape: shape is NULL";
        OT_TRACE("ot_analyze_shape: NULL shape, returning empty");
        return result;
    }

    OT_TRACE("ot_analyze_shape: enter (tol=%g)", tolerance);

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Use BRepCheck_Analyzer for comprehensive validation
        OT_TRACE("ot_analyze_shape: BRepCheck_Analyzer start");
        BRepCheck_Analyzer analyzer(s->shape, true);
        result.has_invalid_topology = !analyzer.IsValid();
        OT_TRACE("ot_analyze_shape: BRepCheck_Analyzer done, valid=%d",
                 result.has_invalid_topology ? 0 : 1);

        int freeEdges = 0;
        int smallEdges = 0;
        int smallFaces = 0;
        int gaps = 0;

        // Analyze shells for free edges
        OT_TRACE("ot_analyze_shape: shell scan start");
        int shellCount = 0;
        for (TopExp_Explorer shellExp(s->shape, TopAbs_SHELL); shellExp.More(); shellExp.Next()) {
            shellCount++;
            TopoDS_Shell shell = TopoDS::Shell(shellExp.Current());
            ShapeAnalysis_Shell shellAnalysis;
            shellAnalysis.LoadShells(shell);

            if (shellAnalysis.HasFreeEdges()) {
                TopoDS_Compound freeEdgesCompound = shellAnalysis.FreeEdges();
                for (TopExp_Explorer edgeExp(freeEdgesCompound, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
                    freeEdges++;
                }
            }
        }
        OT_TRACE("ot_analyze_shape: shell scan done, shells=%d freeEdges=%d",
                 shellCount, freeEdges);

        // Analyze edges for small size
        OT_TRACE("ot_analyze_shape: edge scan start");
        int edgeCount = 0;
        for (TopExp_Explorer edgeExp(s->shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            edgeCount++;
            TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

            GProp_GProps props;
            BRepGProp::LinearProperties(edge, props);
            double length = props.Mass();

            if (length < tolerance) {
                smallEdges++;
            }
        }
        OT_TRACE("ot_analyze_shape: edge scan done, edges=%d smallEdges=%d",
                 edgeCount, smallEdges);

        // Analyze faces for small size
        OT_TRACE("ot_analyze_shape: face scan start");
        int faceCount = 0;
        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            faceCount++;
            TopoDS_Face face = TopoDS::Face(faceExp.Current());

            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            double area = props.Mass();

            if (area < tolerance * tolerance) {
                smallFaces++;
            }
        }
        OT_TRACE("ot_analyze_shape: face scan done, faces=%d smallFaces=%d",
                 faceCount, smallFaces);

        // Analyze wires for gaps. Build a wire->faces ancestor map up front
        // (O(F)) and iterate the unique wires, instead of the previous
        // O(W * F * W_per_F) nested explorer scan that hung on dense shapes.
        OT_TRACE("ot_analyze_shape: wire ancestor map start");
        TopTools_IndexedDataMapOfShapeListOfShape wireToFace;
        TopExp::MapShapesAndAncestors(s->shape, TopAbs_WIRE, TopAbs_FACE, wireToFace);
        OT_TRACE("ot_analyze_shape: wire ancestor map done, wires=%d",
                 wireToFace.Extent());

        OT_TRACE("ot_analyze_shape: wire gap scan start");
        for (Standard_Integer i = 1; i <= wireToFace.Extent(); i++) {
            const TopoDS_Wire& wire = TopoDS::Wire(wireToFace.FindKey(i));
            const TopTools_ListOfShape& parents = wireToFace.FindFromIndex(i);
            if (parents.IsEmpty()) continue;
            const TopoDS_Face& face = TopoDS::Face(parents.First());
            ShapeAnalysis_Wire wireAnalysis(wire, face, tolerance);
            gaps += wireAnalysis.CheckGaps3d();
        }
        OT_TRACE("ot_analyze_shape: wire gap scan done, gaps=%d", gaps);

        result.small_edge_count = smallEdges;
        result.small_face_count = smallFaces;
        result.gap_count = gaps;
        result.self_intersection_count = 0; // Expensive — omitted for now
        result.free_edge_count = freeEdges;
        result.free_face_count = 0;
        result.is_valid = true;

        g_last_error.clear();
        OT_TRACE("ot_analyze_shape: exit ok");
        return result;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_analyze_shape: ") + e.what();
        OT_TRACE("ot_analyze_shape: Standard_Failure: %s", e.what());
        return result;
    } catch (...) {
        g_last_error = "ot_analyze_shape: unknown exception";
        OT_TRACE("ot_analyze_shape: unknown exception");
        return result;
    }
}

OT_EXPORT OTShapeRef ot_heal_shape(OTShapeRef shape) {
    if (!shape) {
        g_last_error = "ot_heal_shape: shape is NULL";
        OT_TRACE("ot_heal_shape: NULL shape");
        return nullptr;
    }

    OT_TRACE("ot_heal_shape: enter");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(s->shape);
        OT_TRACE("ot_heal_shape: ShapeFix_Shape::Perform start");
        fixer->Perform();
        TopoDS_Shape fixed = fixer->Shape();
        OT_TRACE("ot_heal_shape: ShapeFix_Shape::Perform done");

        if (fixed.IsNull()) {
            g_last_error = "ot_heal_shape: healing produced null shape";
            OT_TRACE("ot_heal_shape: null result");
            return nullptr;
        }

        g_last_error.clear();
        OT_TRACE("ot_heal_shape: exit ok");
        return new OTShapeInternal(fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_heal_shape: ") + e.what();
        OT_TRACE("ot_heal_shape: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_heal_shape: unknown exception";
        OT_TRACE("ot_heal_shape: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_heal_shape_detailed(OTShapeRef shape, double tolerance,
    bool fix_solid, bool fix_shell, bool fix_face, bool fix_wire) {
    if (!shape) {
        g_last_error = "ot_heal_shape_detailed: shape is NULL";
        OT_TRACE("ot_heal_shape_detailed: NULL shape");
        return nullptr;
    }

    OT_TRACE("ot_heal_shape_detailed: enter (tol=%g solid=%d shell=%d face=%d wire=%d)",
             tolerance, fix_solid, fix_shell, fix_face, fix_wire);
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(s->shape);
        fixer->SetPrecision(tolerance);
        fixer->FixSolidMode() = fix_solid ? 1 : 0;
        fixer->FixFreeShellMode() = fix_shell ? 1 : 0;
        fixer->FixFreeFaceMode() = fix_face ? 1 : 0;
        fixer->FixFreeWireMode() = fix_wire ? 1 : 0;

        OT_TRACE("ot_heal_shape_detailed: ShapeFix_Shape::Perform start");
        fixer->Perform();
        OT_TRACE("ot_heal_shape_detailed: ShapeFix_Shape::Perform done");

        TopoDS_Shape fixedShape = fixer->Shape();
        if (fixedShape.IsNull()) {
            // Return copy of original if fix failed
            g_last_error.clear();
            OT_TRACE("ot_heal_shape_detailed: null result, returning original");
            return new OTShapeInternal(s->shape);
        }

        g_last_error.clear();
        OT_TRACE("ot_heal_shape_detailed: exit ok");
        return new OTShapeInternal(fixedShape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_heal_shape_detailed: ") + e.what();
        OT_TRACE("ot_heal_shape_detailed: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_heal_shape_detailed: unknown exception";
        OT_TRACE("ot_heal_shape_detailed: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_sew_shape(OTShapeRef shape, double tolerance) {
    if (!shape) {
        g_last_error = "ot_sew_shape: shape is NULL";
        OT_TRACE("ot_sew_shape: NULL shape");
        return nullptr;
    }

    OT_TRACE("ot_sew_shape: enter (tol=%g)", tolerance);
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        BRepBuilderAPI_Sewing sewing(tolerance);
        sewing.Add(s->shape);
        OT_TRACE("ot_sew_shape: Sewing::Perform start");
        sewing.Perform();
        OT_TRACE("ot_sew_shape: Sewing::Perform done");
        TopoDS_Shape sewn = sewing.SewedShape();
        if (sewn.IsNull()) {
            g_last_error = "ot_sew_shape: sewing produced null shape";
            OT_TRACE("ot_sew_shape: null sewn shape");
            return nullptr;
        }

        // Try to make a solid if we got a closed shell
        if (sewn.ShapeType() == TopAbs_SHELL) {
            TopoDS_Shell shell = TopoDS::Shell(sewn);
            if (shell.Closed()) {
                BRepBuilderAPI_MakeSolid makeSolid(shell);
                if (makeSolid.IsDone()) {
                    g_last_error.clear();
                    OT_TRACE("ot_sew_shape: exit ok (closed shell -> solid)");
                    return new OTShapeInternal(makeSolid.Solid());
                }
            }
        }

        g_last_error.clear();
        OT_TRACE("ot_sew_shape: exit ok (type=%d)", sewn.ShapeType());
        return new OTShapeInternal(sewn);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_sew_shape: ") + e.what();
        OT_TRACE("ot_sew_shape: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_sew_shape: unknown exception";
        OT_TRACE("ot_sew_shape: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_upgrade_shape(OTShapeRef shape, double tolerance) {
    if (!shape) {
        g_last_error = "ot_upgrade_shape: shape is NULL";
        OT_TRACE("ot_upgrade_shape: NULL shape");
        return nullptr;
    }

    OT_TRACE("ot_upgrade_shape: enter (tol=%g)", tolerance);
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Step 1: Sew
        OT_TRACE("ot_upgrade_shape: sew start");
        BRepBuilderAPI_Sewing sewing(tolerance);
        sewing.Add(s->shape);
        sewing.Perform();
        TopoDS_Shape sewedShape = sewing.SewedShape();
        if (sewedShape.IsNull()) sewedShape = s->shape;
        OT_TRACE("ot_upgrade_shape: sew done");

        // Step 2: Try to create solid from shell
        TopoDS_Shape resultShape = sewedShape;
        if (sewedShape.ShapeType() != TopAbs_SOLID) {
            TopExp_Explorer shellExp(sewedShape, TopAbs_SHELL);
            if (shellExp.More()) {
                OT_TRACE("ot_upgrade_shape: make_solid from shell");
                BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shellExp.Current()));
                if (makeSolid.IsDone()) {
                    resultShape = makeSolid.Solid();
                }
            }
        }

        // Step 3: Apply shape healing
        OT_TRACE("ot_upgrade_shape: heal start");
        ShapeFix_Shape fixer(resultShape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();
        OT_TRACE("ot_upgrade_shape: heal done");

        g_last_error.clear();
        OT_TRACE("ot_upgrade_shape: exit ok");
        return new OTShapeInternal(fixed.IsNull() ? resultShape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_upgrade_shape: ") + e.what();
        OT_TRACE("ot_upgrade_shape: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_upgrade_shape: unknown exception";
        OT_TRACE("ot_upgrade_shape: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_make_solid(OTShapeRef shape) {
    if (!shape) {
        g_last_error = "ot_make_solid: shape is NULL";
        OT_TRACE("ot_make_solid: NULL shape");
        return nullptr;
    }

    OT_TRACE("ot_make_solid: enter");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // If already a solid, return a copy
        if (s->shape.ShapeType() == TopAbs_SOLID) {
            g_last_error.clear();
            OT_TRACE("ot_make_solid: already a solid, returning copy");
            return new OTShapeInternal(s->shape);
        }

        // Find a shell and try to make it solid
        TopExp_Explorer shellExp(s->shape, TopAbs_SHELL);
        if (shellExp.More()) {
            OT_TRACE("ot_make_solid: building from first shell");
            BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shellExp.Current()));
            if (makeSolid.IsDone()) {
                g_last_error.clear();
                OT_TRACE("ot_make_solid: exit ok");
                return new OTShapeInternal(makeSolid.Solid());
            }
        }

        g_last_error = "ot_make_solid: could not create solid from shape";
        OT_TRACE("ot_make_solid: no shell or builder failed");
        return nullptr;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_make_solid: ") + e.what();
        OT_TRACE("ot_make_solid: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_make_solid: unknown exception";
        OT_TRACE("ot_make_solid: unknown exception");
        return nullptr;
    }
}

OT_EXPORT bool ot_shape_check_valid(OTShapeRef shape) {
    if (!shape) return false;

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        BRepCheck_Analyzer analyzer(s->shape);
        return analyzer.IsValid();
    } catch (...) {
        return false;
    }
}

/* ================================================================
 * Shape Info
 * ================================================================ */

OT_EXPORT double ot_shape_volume(OTShapeRef shape) {
    if (!shape) return -1.0;

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        GProp_GProps props;
        BRepGProp::VolumeProperties(s->shape, props);
        return props.Mass();
    } catch (...) {
        return -1.0;
    }
}

OT_EXPORT double ot_shape_surface_area(OTShapeRef shape) {
    if (!shape) return -1.0;

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        GProp_GProps props;
        BRepGProp::SurfaceProperties(s->shape, props);
        return props.Mass();
    } catch (...) {
        return -1.0;
    }
}

OT_EXPORT void ot_shape_bounding_box(OTShapeRef shape,
    double* xmin, double* ymin, double* zmin,
    double* xmax, double* ymax, double* zmax) {
    if (!shape || !xmin || !ymin || !zmin || !xmax || !ymax || !zmax) return;

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        Bnd_Box box;
        BRepBndLib::Add(s->shape, box);
        box.Get(*xmin, *ymin, *zmin, *xmax, *ymax, *zmax);
    } catch (...) {
        *xmin = *ymin = *zmin = *xmax = *ymax = *zmax = 0;
    }
}

} // extern "C"
