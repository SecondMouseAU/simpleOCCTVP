/*
 * occt_templot_heal.cpp — Healing & analysis module for occtTemplot
 *
 * Shape validation, analysis, healing, sewing, and solid creation.
 * Ported from OCCTSwift's OCCTBridge.mm, stripped of Objective-C dependencies.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"

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
#include <TopExp_Explorer.hxx>

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
        return result;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Use BRepCheck_Analyzer for comprehensive validation
        BRepCheck_Analyzer analyzer(s->shape, true);
        result.has_invalid_topology = !analyzer.IsValid();

        int freeEdges = 0;
        int smallEdges = 0;
        int smallFaces = 0;
        int gaps = 0;

        // Analyze shells for free edges
        for (TopExp_Explorer shellExp(s->shape, TopAbs_SHELL); shellExp.More(); shellExp.Next()) {
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

        // Analyze edges for small size
        for (TopExp_Explorer edgeExp(s->shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());

            GProp_GProps props;
            BRepGProp::LinearProperties(edge, props);
            double length = props.Mass();

            if (length < tolerance) {
                smallEdges++;
            }
        }

        // Analyze faces for small size
        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            TopoDS_Face face = TopoDS::Face(faceExp.Current());

            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            double area = props.Mass();

            if (area < tolerance * tolerance) {
                smallFaces++;
            }
        }

        // Analyze wires for gaps
        for (TopExp_Explorer wireExp(s->shape, TopAbs_WIRE); wireExp.More(); wireExp.Next()) {
            TopoDS_Wire wire = TopoDS::Wire(wireExp.Current());

            // Find a face containing this wire for context
            TopoDS_Face face;
            for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
                TopoDS_Face testFace = TopoDS::Face(faceExp.Current());
                for (TopExp_Explorer innerWireExp(testFace, TopAbs_WIRE); innerWireExp.More(); innerWireExp.Next()) {
                    if (innerWireExp.Current().IsSame(wire)) {
                        face = testFace;
                        break;
                    }
                }
                if (!face.IsNull()) break;
            }

            if (!face.IsNull()) {
                ShapeAnalysis_Wire wireAnalysis(wire, face, tolerance);
                gaps += wireAnalysis.CheckGaps3d();
            }
        }

        result.small_edge_count = smallEdges;
        result.small_face_count = smallFaces;
        result.gap_count = gaps;
        result.self_intersection_count = 0; // Expensive — omitted for now
        result.free_edge_count = freeEdges;
        result.free_face_count = 0;
        result.is_valid = true;

        g_last_error.clear();
        return result;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_analyze_shape: ") + e.what();
        return result;
    } catch (...) {
        g_last_error = "ot_analyze_shape: unknown exception";
        return result;
    }
}

OT_EXPORT OTShapeRef ot_heal_shape(OTShapeRef shape) {
    if (!shape) {
        g_last_error = "ot_heal_shape: shape is NULL";
        return nullptr;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(s->shape);
        fixer->Perform();
        TopoDS_Shape fixed = fixer->Shape();

        if (fixed.IsNull()) {
            g_last_error = "ot_heal_shape: healing produced null shape";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_heal_shape: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_heal_shape: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_heal_shape_detailed(OTShapeRef shape, double tolerance,
    bool fix_solid, bool fix_shell, bool fix_face, bool fix_wire) {
    if (!shape) {
        g_last_error = "ot_heal_shape_detailed: shape is NULL";
        return nullptr;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(s->shape);
        fixer->SetPrecision(tolerance);
        fixer->FixSolidMode() = fix_solid ? 1 : 0;
        fixer->FixFreeShellMode() = fix_shell ? 1 : 0;
        fixer->FixFreeFaceMode() = fix_face ? 1 : 0;
        fixer->FixFreeWireMode() = fix_wire ? 1 : 0;

        fixer->Perform();

        TopoDS_Shape fixedShape = fixer->Shape();
        if (fixedShape.IsNull()) {
            // Return copy of original if fix failed
            g_last_error.clear();
            return new OTShapeInternal(s->shape);
        }

        g_last_error.clear();
        return new OTShapeInternal(fixedShape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_heal_shape_detailed: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_heal_shape_detailed: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_sew_shape(OTShapeRef shape, double tolerance) {
    if (!shape) {
        g_last_error = "ot_sew_shape: shape is NULL";
        return nullptr;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        BRepBuilderAPI_Sewing sewing(tolerance);
        sewing.Add(s->shape);
        sewing.Perform();
        TopoDS_Shape sewn = sewing.SewedShape();
        if (sewn.IsNull()) {
            g_last_error = "ot_sew_shape: sewing produced null shape";
            return nullptr;
        }

        // Try to make a solid if we got a closed shell
        if (sewn.ShapeType() == TopAbs_SHELL) {
            TopoDS_Shell shell = TopoDS::Shell(sewn);
            if (shell.Closed()) {
                BRepBuilderAPI_MakeSolid makeSolid(shell);
                if (makeSolid.IsDone()) {
                    g_last_error.clear();
                    return new OTShapeInternal(makeSolid.Solid());
                }
            }
        }

        g_last_error.clear();
        return new OTShapeInternal(sewn);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_sew_shape: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_sew_shape: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_upgrade_shape(OTShapeRef shape, double tolerance) {
    if (!shape) {
        g_last_error = "ot_upgrade_shape: shape is NULL";
        return nullptr;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Step 1: Sew
        BRepBuilderAPI_Sewing sewing(tolerance);
        sewing.Add(s->shape);
        sewing.Perform();
        TopoDS_Shape sewedShape = sewing.SewedShape();
        if (sewedShape.IsNull()) sewedShape = s->shape;

        // Step 2: Try to create solid from shell
        TopoDS_Shape resultShape = sewedShape;
        if (sewedShape.ShapeType() != TopAbs_SOLID) {
            TopExp_Explorer shellExp(sewedShape, TopAbs_SHELL);
            if (shellExp.More()) {
                BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shellExp.Current()));
                if (makeSolid.IsDone()) {
                    resultShape = makeSolid.Solid();
                }
            }
        }

        // Step 3: Apply shape healing
        ShapeFix_Shape fixer(resultShape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();

        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? resultShape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_upgrade_shape: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_upgrade_shape: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_make_solid(OTShapeRef shape) {
    if (!shape) {
        g_last_error = "ot_make_solid: shape is NULL";
        return nullptr;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // If already a solid, return a copy
        if (s->shape.ShapeType() == TopAbs_SOLID) {
            g_last_error.clear();
            return new OTShapeInternal(s->shape);
        }

        // Find a shell and try to make it solid
        TopExp_Explorer shellExp(s->shape, TopAbs_SHELL);
        if (shellExp.More()) {
            BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shellExp.Current()));
            if (makeSolid.IsDone()) {
                g_last_error.clear();
                return new OTShapeInternal(makeSolid.Solid());
            }
        }

        g_last_error = "ot_make_solid: could not create solid from shape";
        return nullptr;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_make_solid: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_make_solid: unknown exception";
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
