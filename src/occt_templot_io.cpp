/*
 * occt_templot_io.cpp — I/O module for occtTemplot
 *
 * Import/export for STL, STEP, IGES, OBJ, PLY formats.
 * Ported from OCCTSwift's OCCTBridge.mm, stripped of Objective-C dependencies.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"
#include "occt_templot_trace.h"

#include <fstream>
#include <vector>
#include <cstdio>

// OCCT I/O headers
#include <StlAPI_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>

// OBJ/PLY via XDE documents
#include <RWObj_CafReader.hxx>
#include <RWObj_CafWriter.hxx>
#include <RWPly_CafWriter.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Message_ProgressRange.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <TCollection_AsciiString.hxx>

// Healing (used by robust import)
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <ShapeFix_Shape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopExp_Explorer.hxx>

/* ================================================================
 * Lifecycle
 * ================================================================ */

static bool g_initialized = false;
thread_local std::string g_last_error;

extern "C" {

OT_EXPORT void occt_templot_init(void) {
    g_initialized = true;
    ::ot_trace::install_occt_printer();
    OT_TRACE("occt_templot_init: trace=%d", ::ot_trace::enabled() ? 1 : 0);
}

OT_EXPORT void occt_templot_shutdown(void) {
    g_initialized = false;
}

OT_EXPORT const char* occt_templot_version(void) {
    return "0.1.0";
}

OT_EXPORT const char* occt_templot_last_error(void) {
    if (g_last_error.empty()) return nullptr;
    return g_last_error.c_str();
}

/* ================================================================
 * Shape Handle
 * ================================================================ */

OT_EXPORT void ot_shape_free(OTShapeRef shape) {
    delete static_cast<OTShapeInternal*>(shape);
}

OT_EXPORT bool ot_shape_is_valid(OTShapeRef shape) {
    if (!shape) return false;
    auto* s = static_cast<OTShapeInternal*>(shape);
    return !s->shape.IsNull();
}

OT_EXPORT int32_t ot_shape_type(OTShapeRef shape) {
    if (!shape) return -1;
    auto* s = static_cast<OTShapeInternal*>(shape);
    if (s->shape.IsNull()) return -1;
    return static_cast<int32_t>(s->shape.ShapeType());
}

/* ================================================================
 * Helpers
 * ================================================================ */

// Normalize CR-only line endings to LF so OCCT's ASCII STL parser works.
// Returns empty string if no conversion was needed (file already has LF).
// Returns path to a temp file if conversion was performed.
static std::string normalize_stl_line_endings(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";

    // Read whole file
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> buf(static_cast<size_t>(size));
    in.read(buf.data(), size);
    in.close();

    // Check if file has CR-only line endings (CR without following LF)
    bool has_lone_cr = false;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == '\r') {
            if (i + 1 >= buf.size() || buf[i + 1] != '\n') {
                has_lone_cr = true;
                break;
            }
        }
    }
    if (!has_lone_cr) return "";  // no fix needed

    // Write normalized temp file (replace lone CR with LF)
    std::string tmp_path = std::string(path) + ".tmp_normalized";
    std::ofstream out(tmp_path, std::ios::binary);
    if (!out) return "";

    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == '\r') {
            if (i + 1 < buf.size() && buf[i + 1] == '\n') {
                out.put('\r');  // keep CRLF as-is
            } else {
                out.put('\n');  // convert lone CR to LF
            }
        } else {
            out.put(buf[i]);
        }
    }
    out.close();
    return tmp_path;
}

/* ================================================================
 * I/O — Import
 * ================================================================ */

OT_EXPORT OTShapeRef ot_import_stl(const char* path) {
    if (!path) {
        g_last_error = "ot_import_stl: path is NULL";
        OT_TRACE("ot_import_stl: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_stl: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_stl");
    try {
        // Normalize CR-only line endings (e.g. from Templot on macOS)
        std::string tmp_path = normalize_stl_line_endings(path);
        const char* read_path = tmp_path.empty() ? path : tmp_path.c_str();

        TopoDS_Shape shape;
        StlAPI_Reader reader;
        OT_TRACE("ot_import_stl: StlAPI_Reader::Read start");
        bool ok = reader.Read(shape, read_path);
        OT_TRACE("ot_import_stl: StlAPI_Reader::Read done ok=%d", ok ? 1 : 0);

        if (!tmp_path.empty()) std::remove(tmp_path.c_str());

        if (!ok) {
            g_last_error = "ot_import_stl: StlAPI_Reader failed to read file";
            return nullptr;
        }
        if (shape.IsNull()) {
            g_last_error = "ot_import_stl: resulting shape is null";
            return nullptr;
        }
        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_stl: ") + e.what();
        OT_TRACE("ot_import_stl: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_stl: unknown exception";
        OT_TRACE("ot_import_stl: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_stl_robust(const char* path, double sewing_tolerance) {
    if (!path) {
        g_last_error = "ot_import_stl_robust: path is NULL";
        OT_TRACE("ot_import_stl_robust: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_stl_robust: enter (%s, tol=%g)", path, sewing_tolerance);
    OT_TRACE_TIMER("ot_import_stl_robust");
    try {
        // Normalize CR-only line endings (e.g. from Templot on macOS)
        std::string tmp_path = normalize_stl_line_endings(path);
        const char* read_path = tmp_path.empty() ? path : tmp_path.c_str();

        TopoDS_Shape shape;
        StlAPI_Reader reader;
        OT_TRACE("ot_import_stl_robust: StlAPI_Reader::Read start");
        bool ok = reader.Read(shape, read_path);
        OT_TRACE("ot_import_stl_robust: StlAPI_Reader::Read done ok=%d", ok ? 1 : 0);

        if (!tmp_path.empty()) std::remove(tmp_path.c_str());

        if (!ok) {
            g_last_error = "ot_import_stl_robust: StlAPI_Reader failed to read file";
            return nullptr;
        }
        if (shape.IsNull()) {
            g_last_error = "ot_import_stl_robust: resulting shape is null";
            return nullptr;
        }

        // Sew disconnected faces
        BRepBuilderAPI_Sewing sewing(sewing_tolerance);
        sewing.Add(shape);
        OT_TRACE("ot_import_stl_robust: Sewing::Perform start");
        sewing.Perform();
        OT_TRACE("ot_import_stl_robust: Sewing::Perform done");
        TopoDS_Shape sewedShape = sewing.SewedShape();
        if (sewedShape.IsNull()) sewedShape = shape;

        // Try to create solid from shell
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

        // Apply shape healing
        OT_TRACE("ot_import_stl_robust: ShapeFix_Shape::Perform start");
        ShapeFix_Shape fixer(resultShape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();
        OT_TRACE("ot_import_stl_robust: ShapeFix_Shape::Perform done");

        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? resultShape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_stl_robust: ") + e.what();
        OT_TRACE("ot_import_stl_robust: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_stl_robust: unknown exception";
        OT_TRACE("ot_import_stl_robust: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_step(const char* path) {
    if (!path) {
        g_last_error = "ot_import_step: path is NULL";
        OT_TRACE("ot_import_step: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_step: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_step");
    try {
        STEPControl_Reader reader;
        OT_TRACE("ot_import_step: ReadFile start");
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        OT_TRACE("ot_import_step: ReadFile done status=%d", (int)status);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_step: ReadFile failed";
            return nullptr;
        }

        OT_TRACE("ot_import_step: TransferRoots start");
        reader.TransferRoots();
        OT_TRACE("ot_import_step: TransferRoots done");
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_step: resulting shape is null";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_step: ") + e.what();
        OT_TRACE("ot_import_step: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_step: unknown exception";
        OT_TRACE("ot_import_step: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_step_robust(const char* path) {
    if (!path) {
        g_last_error = "ot_import_step_robust: path is NULL";
        OT_TRACE("ot_import_step_robust: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_step_robust: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_step_robust");
    try {
        STEPControl_Reader reader;

        // Configure reader for better precision handling
        Interface_Static::SetIVal("read.precision.mode", 0);
        Interface_Static::SetRVal("read.maxprecision.val", 0.1);
        Interface_Static::SetIVal("read.surfacecurve.mode", 3);
        Interface_Static::SetIVal("read.step.product.mode", 1);

        OT_TRACE("ot_import_step_robust: ReadFile start");
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        OT_TRACE("ot_import_step_robust: ReadFile done status=%d", (int)status);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_step_robust: ReadFile failed";
            return nullptr;
        }

        OT_TRACE("ot_import_step_robust: TransferRoots start");
        Standard_Integer roots = reader.TransferRoots();
        OT_TRACE("ot_import_step_robust: TransferRoots done (%d roots)", (int)roots);
        if (roots == 0) {
            g_last_error = "ot_import_step_robust: TransferRoots returned 0";
            return nullptr;
        }

        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_step_robust: resulting shape is null";
            return nullptr;
        }

        TopAbs_ShapeEnum shapeType = shape.ShapeType();

        // If already a solid, just apply healing
        if (shapeType == TopAbs_SOLID) {
            ShapeFix_Shape fixer(shape);
            fixer.Perform();
            TopoDS_Shape fixed = fixer.Shape();
            g_last_error.clear();
            return new OTShapeInternal(fixed.IsNull() ? shape : fixed);
        }

        // Try sewing and solid creation for non-solids
        if (shapeType == TopAbs_COMPOUND || shapeType == TopAbs_SHELL ||
            shapeType == TopAbs_FACE) {

            BRepBuilderAPI_Sewing sewing(1.0e-4);
            sewing.SetNonManifoldMode(false);
            sewing.Add(shape);
            sewing.Perform();
            TopoDS_Shape sewedShape = sewing.SewedShape();
            if (sewedShape.IsNull()) sewedShape = shape;

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

            ShapeFix_Shape fixer(resultShape);
            fixer.Perform();
            TopoDS_Shape fixed = fixer.Shape();
            g_last_error.clear();
            return new OTShapeInternal(fixed.IsNull() ? resultShape : fixed);
        }

        // Fallback: just heal
        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();
        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? shape : fixed);

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_step_robust: ") + e.what();
        OT_TRACE("ot_import_step_robust: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_step_robust: unknown exception";
        OT_TRACE("ot_import_step_robust: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTImportResult ot_import_step_with_diagnostics(const char* path) {
    OTImportResult result = {nullptr, -1, -1, false, false, false};
    if (!path) {
        g_last_error = "ot_import_step_with_diagnostics: path is NULL";
        OT_TRACE("ot_import_step_with_diagnostics: NULL path");
        return result;
    }

    OT_TRACE("ot_import_step_with_diagnostics: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_step_with_diagnostics");
    try {
        STEPControl_Reader reader;

        Interface_Static::SetIVal("read.precision.mode", 0);
        Interface_Static::SetRVal("read.maxprecision.val", 0.1);
        Interface_Static::SetIVal("read.surfacecurve.mode", 3);
        Interface_Static::SetIVal("read.step.product.mode", 1);

        if (reader.ReadFile(path) != IFSelect_RetDone) {
            g_last_error = "ot_import_step_with_diagnostics: ReadFile failed";
            return result;
        }
        if (reader.TransferRoots() == 0) {
            g_last_error = "ot_import_step_with_diagnostics: TransferRoots returned 0";
            return result;
        }

        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_step_with_diagnostics: resulting shape is null";
            return result;
        }

        result.original_type = static_cast<int32_t>(shape.ShapeType());

        // Process non-solids
        if (shape.ShapeType() != TopAbs_SOLID) {
            // Try sewing
            BRepBuilderAPI_Sewing sewing(1.0e-4);
            sewing.SetNonManifoldMode(false);
            sewing.Add(shape);
            sewing.Perform();
            TopoDS_Shape sewedShape = sewing.SewedShape();
            if (!sewedShape.IsNull() && !sewedShape.IsSame(shape)) {
                shape = sewedShape;
                result.sewing_applied = true;
            }

            // Try solid creation
            if (shape.ShapeType() != TopAbs_SOLID) {
                TopExp_Explorer shellExp(shape, TopAbs_SHELL);
                if (shellExp.More()) {
                    BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shellExp.Current()));
                    if (makeSolid.IsDone()) {
                        shape = makeSolid.Solid();
                        result.solid_created = true;
                    }
                }
            }
        }

        // Apply shape healing
        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();
        if (!fixed.IsNull()) {
            shape = fixed;
            result.healing_applied = true;
        }

        result.shape = new OTShapeInternal(shape);
        result.result_type = static_cast<int32_t>(shape.ShapeType());
        g_last_error.clear();
        return result;

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_step_with_diagnostics: ") + e.what();
        OT_TRACE("ot_import_step_with_diagnostics: Standard_Failure: %s", e.what());
        return result;
    } catch (...) {
        g_last_error = "ot_import_step_with_diagnostics: unknown exception";
        OT_TRACE("ot_import_step_with_diagnostics: unknown exception");
        return result;
    }
}

OT_EXPORT OTShapeRef ot_import_iges(const char* path) {
    if (!path) {
        g_last_error = "ot_import_iges: path is NULL";
        OT_TRACE("ot_import_iges: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_iges: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_iges");
    try {
        IGESControl_Reader reader;
        OT_TRACE("ot_import_iges: ReadFile start");
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        OT_TRACE("ot_import_iges: ReadFile done status=%d", (int)status);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_iges: ReadFile failed";
            return nullptr;
        }

        OT_TRACE("ot_import_iges: TransferRoots start");
        reader.TransferRoots();
        OT_TRACE("ot_import_iges: TransferRoots done");
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_iges: resulting shape is null";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_iges: ") + e.what();
        OT_TRACE("ot_import_iges: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_iges: unknown exception";
        OT_TRACE("ot_import_iges: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_iges_robust(const char* path) {
    if (!path) {
        g_last_error = "ot_import_iges_robust: path is NULL";
        OT_TRACE("ot_import_iges_robust: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_iges_robust: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_iges_robust");
    try {
        IGESControl_Reader reader;

        Interface_Static::SetIVal("read.precision.mode", 0);
        Interface_Static::SetRVal("read.precision.val", 0.0001);

        OT_TRACE("ot_import_iges_robust: ReadFile start");
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        OT_TRACE("ot_import_iges_robust: ReadFile done status=%d", (int)status);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_iges_robust: ReadFile failed";
            return nullptr;
        }

        OT_TRACE("ot_import_iges_robust: TransferRoots start");
        Standard_Integer roots = reader.TransferRoots();
        OT_TRACE("ot_import_iges_robust: TransferRoots done (%d roots)", (int)roots);
        if (roots == 0) {
            g_last_error = "ot_import_iges_robust: TransferRoots returned 0";
            return nullptr;
        }

        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_iges_robust: resulting shape is null";
            return nullptr;
        }

        OT_TRACE("ot_import_iges_robust: ShapeFix_Shape::Perform start");
        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();
        OT_TRACE("ot_import_iges_robust: ShapeFix_Shape::Perform done");

        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? shape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_iges_robust: ") + e.what();
        OT_TRACE("ot_import_iges_robust: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_iges_robust: unknown exception";
        OT_TRACE("ot_import_iges_robust: unknown exception");
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_obj(const char* path) {
    if (!path) {
        g_last_error = "ot_import_obj: path is NULL";
        OT_TRACE("ot_import_obj: NULL path");
        return nullptr;
    }

    OT_TRACE("ot_import_obj: enter (%s)", path);
    OT_TRACE_TIMER("ot_import_obj");
    try {
        RWObj_CafReader objReader;

        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        objReader.SetDocument(doc);
        TCollection_AsciiString filePath(path);
        OT_TRACE("ot_import_obj: RWObj_CafReader::Perform start");
        bool ok = objReader.Perform(filePath, Message_ProgressRange());
        OT_TRACE("ot_import_obj: RWObj_CafReader::Perform done ok=%d", ok ? 1 : 0);
        if (!ok) {
            app->Close(doc);
            g_last_error = "ot_import_obj: RWObj_CafReader::Perform failed";
            return nullptr;
        }

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        TopoDS_Shape shape = shapeTool->GetOneShape();

        app->Close(doc);

        if (shape.IsNull()) {
            g_last_error = "ot_import_obj: resulting shape is null";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_obj: ") + e.what();
        OT_TRACE("ot_import_obj: Standard_Failure: %s", e.what());
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_obj: unknown exception";
        OT_TRACE("ot_import_obj: unknown exception");
        return nullptr;
    }
}

/* ================================================================
 * I/O — Export
 * ================================================================ */

OT_EXPORT bool ot_export_stl(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_stl: shape or path is NULL";
        OT_TRACE("ot_export_stl: NULL shape or path");
        return false;
    }

    OT_TRACE("ot_export_stl: enter (%s, deflection=%g)", path, deflection);
    OT_TRACE_TIMER("ot_export_stl");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Mesh the shape first
        OT_TRACE("ot_export_stl: BRepMesh_IncrementalMesh::Perform start");
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();
        OT_TRACE("ot_export_stl: BRepMesh_IncrementalMesh::Perform done");

        StlAPI_Writer writer;
        writer.ASCIIMode() = false; // Binary STL for smaller files
        OT_TRACE("ot_export_stl: StlAPI_Writer::Write start");
        bool result = writer.Write(s->shape, path);
        OT_TRACE("ot_export_stl: StlAPI_Writer::Write done ok=%d", result ? 1 : 0);

        if (!result) {
            g_last_error = "ot_export_stl: StlAPI_Writer::Write failed";
        } else {
            g_last_error.clear();
        }
        return result;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_stl: ") + e.what();
        OT_TRACE("ot_export_stl: Standard_Failure: %s", e.what());
        return false;
    } catch (...) {
        g_last_error = "ot_export_stl: unknown exception";
        OT_TRACE("ot_export_stl: unknown exception");
        return false;
    }
}

OT_EXPORT bool ot_export_step(OTShapeRef shape, const char* path) {
    if (!shape || !path) {
        g_last_error = "ot_export_step: shape or path is NULL";
        OT_TRACE("ot_export_step: NULL shape or path");
        return false;
    }

    OT_TRACE("ot_export_step: enter (%s)", path);
    OT_TRACE_TIMER("ot_export_step");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        bool success = false;
        {
            STEPControl_Writer writer;
            Interface_Static::SetCVal("write.step.schema", "AP214");

            OT_TRACE("ot_export_step: Transfer start");
            IFSelect_ReturnStatus status = writer.Transfer(s->shape, STEPControl_AsIs);
            OT_TRACE("ot_export_step: Transfer done status=%d", (int)status);
            if (status != IFSelect_RetDone) {
                g_last_error = "ot_export_step: Transfer failed";
                return false;
            }

            OT_TRACE("ot_export_step: Write start");
            status = writer.Write(path);
            OT_TRACE("ot_export_step: Write done status=%d", (int)status);
            success = (status == IFSelect_RetDone);
        }

        if (!success) {
            g_last_error = "ot_export_step: Write failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_step: ") + e.what();
        OT_TRACE("ot_export_step: Standard_Failure: %s", e.what());
        return false;
    } catch (...) {
        g_last_error = "ot_export_step: unknown exception";
        OT_TRACE("ot_export_step: unknown exception");
        return false;
    }
}

OT_EXPORT bool ot_export_iges(OTShapeRef shape, const char* path) {
    if (!shape || !path) {
        g_last_error = "ot_export_iges: shape or path is NULL";
        OT_TRACE("ot_export_iges: NULL shape or path");
        return false;
    }

    OT_TRACE("ot_export_iges: enter (%s)", path);
    OT_TRACE_TIMER("ot_export_iges");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        bool success = false;
        {
            IGESControl_Writer writer("MM", 0); // Millimeters, faces mode

            OT_TRACE("ot_export_iges: AddShape start");
            if (!writer.AddShape(s->shape)) {
                g_last_error = "ot_export_iges: AddShape failed";
                OT_TRACE("ot_export_iges: AddShape failed");
                return false;
            }
            OT_TRACE("ot_export_iges: AddShape done; ComputeModel start");

            writer.ComputeModel();
            OT_TRACE("ot_export_iges: ComputeModel done; Write start");
            success = writer.Write(path);
            OT_TRACE("ot_export_iges: Write done ok=%d", success ? 1 : 0);
        }

        if (!success) {
            g_last_error = "ot_export_iges: Write failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_iges: ") + e.what();
        OT_TRACE("ot_export_iges: Standard_Failure: %s", e.what());
        return false;
    } catch (...) {
        g_last_error = "ot_export_iges: unknown exception";
        OT_TRACE("ot_export_iges: unknown exception");
        return false;
    }
}

OT_EXPORT bool ot_export_obj(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_obj: shape or path is NULL";
        OT_TRACE("ot_export_obj: NULL shape or path");
        return false;
    }

    OT_TRACE("ot_export_obj: enter (%s, deflection=%g)", path, deflection);
    OT_TRACE_TIMER("ot_export_obj");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate the shape first
        OT_TRACE("ot_export_obj: BRepMesh_IncrementalMesh::Perform start");
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();
        OT_TRACE("ot_export_obj: BRepMesh_IncrementalMesh::Perform done");

        // Create an XDE document
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        shapeTool->AddShape(s->shape);

        // Write OBJ
        OT_TRACE("ot_export_obj: RWObj_CafWriter::Perform start");
        RWObj_CafWriter writer(path);
        bool success = writer.Perform(doc, NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>(), Message_ProgressRange());
        OT_TRACE("ot_export_obj: RWObj_CafWriter::Perform done ok=%d", success ? 1 : 0);

        app->Close(doc);

        if (!success) {
            g_last_error = "ot_export_obj: RWObj_CafWriter::Perform failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_obj: ") + e.what();
        OT_TRACE("ot_export_obj: Standard_Failure: %s", e.what());
        return false;
    } catch (...) {
        g_last_error = "ot_export_obj: unknown exception";
        OT_TRACE("ot_export_obj: unknown exception");
        return false;
    }
}

OT_EXPORT bool ot_export_ply(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_ply: shape or path is NULL";
        OT_TRACE("ot_export_ply: NULL shape or path");
        return false;
    }

    OT_TRACE("ot_export_ply: enter (%s, deflection=%g)", path, deflection);
    OT_TRACE_TIMER("ot_export_ply");
    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate the shape first
        OT_TRACE("ot_export_ply: BRepMesh_IncrementalMesh::Perform start");
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();
        OT_TRACE("ot_export_ply: BRepMesh_IncrementalMesh::Perform done");

        // Create an XDE document
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        shapeTool->AddShape(s->shape);

        // Write PLY
        OT_TRACE("ot_export_ply: RWPly_CafWriter::Perform start");
        RWPly_CafWriter writer(path);
        writer.SetNormals(true);
        bool success = writer.Perform(doc, NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>(), Message_ProgressRange());
        OT_TRACE("ot_export_ply: RWPly_CafWriter::Perform done ok=%d", success ? 1 : 0);

        app->Close(doc);

        if (!success) {
            g_last_error = "ot_export_ply: RWPly_CafWriter::Perform failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_ply: ") + e.what();
        OT_TRACE("ot_export_ply: Standard_Failure: %s", e.what());
        return false;
    } catch (...) {
        g_last_error = "ot_export_ply: unknown exception";
        OT_TRACE("ot_export_ply: unknown exception");
        return false;
    }
}

} // extern "C"
