/*
 * occt_templot_io.cpp — I/O module for occtTemplot
 *
 * Import/export for STL, STEP, IGES, OBJ, PLY formats.
 * Ported from OCCTSwift's OCCTBridge.mm, stripped of Objective-C dependencies.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"

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
#include <TColStd_IndexedDataMapOfStringString.hxx>

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
        return nullptr;
    }

    try {
        // Normalize CR-only line endings (e.g. from Templot on macOS)
        std::string tmp_path = normalize_stl_line_endings(path);
        const char* read_path = tmp_path.empty() ? path : tmp_path.c_str();

        TopoDS_Shape shape;
        StlAPI_Reader reader;
        bool ok = reader.Read(shape, read_path);

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
        g_last_error = std::string("ot_import_stl: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_stl: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_stl_robust(const char* path, double sewing_tolerance) {
    if (!path) {
        g_last_error = "ot_import_stl_robust: path is NULL";
        return nullptr;
    }

    try {
        // Normalize CR-only line endings (e.g. from Templot on macOS)
        std::string tmp_path = normalize_stl_line_endings(path);
        const char* read_path = tmp_path.empty() ? path : tmp_path.c_str();

        TopoDS_Shape shape;
        StlAPI_Reader reader;
        bool ok = reader.Read(shape, read_path);

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
        sewing.Perform();
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
        ShapeFix_Shape fixer(resultShape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();

        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? resultShape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_stl_robust: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_stl_robust: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_step(const char* path) {
    if (!path) {
        g_last_error = "ot_import_step: path is NULL";
        return nullptr;
    }

    try {
        STEPControl_Reader reader;
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_step: ReadFile failed";
            return nullptr;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_step: resulting shape is null";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_step: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_step: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_step_robust(const char* path) {
    if (!path) {
        g_last_error = "ot_import_step_robust: path is NULL";
        return nullptr;
    }

    try {
        STEPControl_Reader reader;

        // Configure reader for better precision handling
        Interface_Static::SetIVal("read.precision.mode", 0);
        Interface_Static::SetRVal("read.maxprecision.val", 0.1);
        Interface_Static::SetIVal("read.surfacecurve.mode", 3);
        Interface_Static::SetIVal("read.step.product.mode", 1);

        IFSelect_ReturnStatus status = reader.ReadFile(path);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_step_robust: ReadFile failed";
            return nullptr;
        }

        if (reader.TransferRoots() == 0) {
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
            sewing.SetNonManifoldMode(Standard_False);
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
        g_last_error = std::string("ot_import_step_robust: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_step_robust: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTImportResult ot_import_step_with_diagnostics(const char* path) {
    OTImportResult result = {nullptr, -1, -1, false, false, false};
    if (!path) {
        g_last_error = "ot_import_step_with_diagnostics: path is NULL";
        return result;
    }

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
            sewing.SetNonManifoldMode(Standard_False);
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
        g_last_error = std::string("ot_import_step_with_diagnostics: ") + e.GetMessageString();
        return result;
    } catch (...) {
        g_last_error = "ot_import_step_with_diagnostics: unknown exception";
        return result;
    }
}

OT_EXPORT OTShapeRef ot_import_iges(const char* path) {
    if (!path) {
        g_last_error = "ot_import_iges: path is NULL";
        return nullptr;
    }

    try {
        IGESControl_Reader reader;
        IFSelect_ReturnStatus status = reader.ReadFile(path);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_iges: ReadFile failed";
            return nullptr;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_iges: resulting shape is null";
            return nullptr;
        }

        g_last_error.clear();
        return new OTShapeInternal(shape);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_iges: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_iges: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_iges_robust(const char* path) {
    if (!path) {
        g_last_error = "ot_import_iges_robust: path is NULL";
        return nullptr;
    }

    try {
        IGESControl_Reader reader;

        Interface_Static::SetIVal("read.precision.mode", 0);
        Interface_Static::SetRVal("read.precision.val", 0.0001);

        IFSelect_ReturnStatus status = reader.ReadFile(path);
        if (status != IFSelect_RetDone) {
            g_last_error = "ot_import_iges_robust: ReadFile failed";
            return nullptr;
        }

        if (reader.TransferRoots() == 0) {
            g_last_error = "ot_import_iges_robust: TransferRoots returned 0";
            return nullptr;
        }

        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            g_last_error = "ot_import_iges_robust: resulting shape is null";
            return nullptr;
        }

        ShapeFix_Shape fixer(shape);
        fixer.Perform();
        TopoDS_Shape fixed = fixer.Shape();

        g_last_error.clear();
        return new OTShapeInternal(fixed.IsNull() ? shape : fixed);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_import_iges_robust: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_iges_robust: unknown exception";
        return nullptr;
    }
}

OT_EXPORT OTShapeRef ot_import_obj(const char* path) {
    if (!path) {
        g_last_error = "ot_import_obj: path is NULL";
        return nullptr;
    }

    try {
        RWObj_CafReader objReader;

        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        objReader.SetDocument(doc);
        TCollection_AsciiString filePath(path);
        if (!objReader.Perform(filePath, Message_ProgressRange())) {
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
        g_last_error = std::string("ot_import_obj: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_import_obj: unknown exception";
        return nullptr;
    }
}

/* ================================================================
 * I/O — Export
 * ================================================================ */

OT_EXPORT bool ot_export_stl(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_stl: shape or path is NULL";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Mesh the shape first
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        StlAPI_Writer writer;
        writer.ASCIIMode() = Standard_False; // Binary STL for smaller files
        bool result = writer.Write(s->shape, path);

        if (!result) {
            g_last_error = "ot_export_stl: StlAPI_Writer::Write failed";
        } else {
            g_last_error.clear();
        }
        return result;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_stl: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_export_stl: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_export_step(OTShapeRef shape, const char* path) {
    if (!shape || !path) {
        g_last_error = "ot_export_step: shape or path is NULL";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        bool success = false;
        {
            STEPControl_Writer writer;
            Interface_Static::SetCVal("write.step.schema", "AP214");

            IFSelect_ReturnStatus status = writer.Transfer(s->shape, STEPControl_AsIs);
            if (status != IFSelect_RetDone) {
                g_last_error = "ot_export_step: Transfer failed";
                return false;
            }

            status = writer.Write(path);
            success = (status == IFSelect_RetDone);
        }

        if (!success) {
            g_last_error = "ot_export_step: Write failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_step: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_export_step: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_export_iges(OTShapeRef shape, const char* path) {
    if (!shape || !path) {
        g_last_error = "ot_export_iges: shape or path is NULL";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);
        bool success = false;
        {
            IGESControl_Writer writer("MM", 0); // Millimeters, faces mode

            if (!writer.AddShape(s->shape)) {
                g_last_error = "ot_export_iges: AddShape failed";
                return false;
            }

            writer.ComputeModel();
            success = writer.Write(path);
        }

        if (!success) {
            g_last_error = "ot_export_iges: Write failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_iges: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_export_iges: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_export_obj(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_obj: shape or path is NULL";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate the shape first
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        // Create an XDE document
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        shapeTool->AddShape(s->shape);

        // Write OBJ
        RWObj_CafWriter writer(path);
        bool success = writer.Perform(doc, TColStd_IndexedDataMapOfStringString(), Message_ProgressRange());

        app->Close(doc);

        if (!success) {
            g_last_error = "ot_export_obj: RWObj_CafWriter::Perform failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_obj: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_export_obj: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_export_ply(OTShapeRef shape, const char* path, double deflection) {
    if (!shape || !path) {
        g_last_error = "ot_export_ply: shape or path is NULL";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate the shape first
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        // Create an XDE document
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        shapeTool->AddShape(s->shape);

        // Write PLY
        RWPly_CafWriter writer(path);
        writer.SetNormals(true);
        bool success = writer.Perform(doc, TColStd_IndexedDataMapOfStringString(), Message_ProgressRange());

        app->Close(doc);

        if (!success) {
            g_last_error = "ot_export_ply: RWPly_CafWriter::Perform failed";
        } else {
            g_last_error.clear();
        }
        return success;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_export_ply: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_export_ply: unknown exception";
        return false;
    }
}

} // extern "C"
