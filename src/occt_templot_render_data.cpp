/*
 * occt_templot_render_data.cpp — Camera, edge mesh, and drawer for occtTemplot
 *
 * Standalone rendering data API: camera (projection/view matrices),
 * edge mesh extraction, display drawer. For consumers that want
 * to render shapes themselves (e.g. Metal, Vulkan, software).
 *
 * Ported from OCCTSwift OCCTBridge.mm edge mesh extraction (lines 5785-5900).
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"

#include <Graphic3d_Camera.hxx>
#include <Graphic3d_Mat4d.hxx>
#include <Prs3d_Drawer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Polygon3D.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <vector>
#include <cstdlib>
#include <cstring>
#include <cmath>

/* ================================================================
 * Internal structs
 * ================================================================ */

struct OTCameraInternal {
    Handle(Graphic3d_Camera) camera;
    OTCameraInternal() : camera(new Graphic3d_Camera()) {
        camera->SetZeroToOneDepth(true);
    }
};

struct OTDrawerInternal {
    Handle(Prs3d_Drawer) drawer;
    OTDrawerInternal() : drawer(new Prs3d_Drawer()) {}
};

extern "C" {

/* ================================================================
 * Camera — Lifecycle
 * ================================================================ */

OT_EXPORT OTCameraRef ot_camera_create(void) {
    try {
        auto* c = new OTCameraInternal();
        c->camera->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
        c->camera->SetFOVy(45.0);
        c->camera->SetEye(gp_Pnt(0, 0, 100));
        c->camera->SetCenter(gp_Pnt(0, 0, 0));
        c->camera->SetUp(gp_Dir(0, 1, 0));
        g_last_error.clear();
        return static_cast<OTCameraRef>(c);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_camera_create: ") + e.GetMessageString();
        return nullptr;
    }
}

OT_EXPORT void ot_camera_destroy(OTCameraRef camera) {
    if (!camera) return;
    delete static_cast<OTCameraInternal*>(camera);
}

/* ================================================================
 * Camera — Set/Get
 * ================================================================ */

OT_EXPORT void ot_camera_set_eye(OTCameraRef camera, double x, double y, double z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetEye(gp_Pnt(x, y, z));
}

OT_EXPORT void ot_camera_get_eye(OTCameraRef camera, double* x, double* y, double* z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    gp_Pnt eye = c->camera->Eye();
    if (x) *x = eye.X();
    if (y) *y = eye.Y();
    if (z) *z = eye.Z();
}

OT_EXPORT void ot_camera_set_center(OTCameraRef camera, double x, double y, double z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetCenter(gp_Pnt(x, y, z));
}

OT_EXPORT void ot_camera_get_center(OTCameraRef camera, double* x, double* y, double* z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    gp_Pnt center = c->camera->Center();
    if (x) *x = center.X();
    if (y) *y = center.Y();
    if (z) *z = center.Z();
}

OT_EXPORT void ot_camera_set_up(OTCameraRef camera, double x, double y, double z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetUp(gp_Dir(x, y, z));
}

OT_EXPORT void ot_camera_get_up(OTCameraRef camera, double* x, double* y, double* z) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    gp_Dir up = c->camera->Up();
    if (x) *x = up.X();
    if (y) *y = up.Y();
    if (z) *z = up.Z();
}

OT_EXPORT void ot_camera_set_projection_type(OTCameraRef camera, OTProjectionType type) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    if (type == OT_PROJECTION_ORTHOGRAPHIC) {
        c->camera->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    } else {
        c->camera->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    }
}

OT_EXPORT OTProjectionType ot_camera_get_projection_type(OTCameraRef camera) {
    if (!camera) return OT_PROJECTION_PERSPECTIVE;
    auto* c = static_cast<OTCameraInternal*>(camera);
    if (c->camera->ProjectionType() == Graphic3d_Camera::Projection_Orthographic)
        return OT_PROJECTION_ORTHOGRAPHIC;
    return OT_PROJECTION_PERSPECTIVE;
}

OT_EXPORT void ot_camera_set_fov(OTCameraRef camera, double degrees) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetFOVy(degrees);
}

OT_EXPORT double ot_camera_get_fov(OTCameraRef camera) {
    if (!camera) return 45.0;
    auto* c = static_cast<OTCameraInternal*>(camera);
    return c->camera->FOVy();
}

OT_EXPORT void ot_camera_set_scale(OTCameraRef camera, double scale) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetScale(scale);
}

OT_EXPORT double ot_camera_get_scale(OTCameraRef camera) {
    if (!camera) return 1.0;
    auto* c = static_cast<OTCameraInternal*>(camera);
    return c->camera->Scale();
}

OT_EXPORT void ot_camera_set_z_range(OTCameraRef camera, double z_near, double z_far) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetZRange(z_near, z_far);
}

OT_EXPORT void ot_camera_set_aspect(OTCameraRef camera, double aspect) {
    if (!camera) return;
    auto* c = static_cast<OTCameraInternal*>(camera);
    c->camera->SetAspect(aspect);
}

/* ================================================================
 * Camera — Matrices (column-major, zero-to-one depth)
 * ================================================================ */

OT_EXPORT bool ot_camera_get_projection_matrix(OTCameraRef camera, double* out16) {
    if (!camera || !out16) return false;
    try {
        auto* c = static_cast<OTCameraInternal*>(camera);
        Graphic3d_Mat4d mat = c->camera->ProjectionMatrix();
        // OCCT stores column-major (OpenGL convention)
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                out16[col * 4 + row] = mat.GetValue(row, col);
        return true;
    } catch (...) { return false; }
}

OT_EXPORT bool ot_camera_get_view_matrix(OTCameraRef camera, double* out16) {
    if (!camera || !out16) return false;
    try {
        auto* c = static_cast<OTCameraInternal*>(camera);
        Graphic3d_Mat4d mat = c->camera->OrientationMatrix();
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                out16[col * 4 + row] = mat.GetValue(row, col);
        return true;
    } catch (...) { return false; }
}

/* ================================================================
 * Camera — Project / Unproject
 * ================================================================ */

OT_EXPORT bool ot_camera_project(OTCameraRef camera,
    double world_x, double world_y, double world_z,
    double* out_x, double* out_y, double* out_z) {
    if (!camera || !out_x || !out_y || !out_z) return false;
    try {
        auto* c = static_cast<OTCameraInternal*>(camera);
        gp_Pnt result = c->camera->Project(gp_Pnt(world_x, world_y, world_z));
        *out_x = result.X();
        *out_y = result.Y();
        *out_z = result.Z();
        return true;
    } catch (...) { return false; }
}

OT_EXPORT bool ot_camera_unproject(OTCameraRef camera,
    double ndc_x, double ndc_y, double ndc_z,
    double* out_x, double* out_y, double* out_z) {
    if (!camera || !out_x || !out_y || !out_z) return false;
    try {
        auto* c = static_cast<OTCameraInternal*>(camera);
        gp_Pnt result = c->camera->UnProject(gp_Pnt(ndc_x, ndc_y, ndc_z));
        *out_x = result.X();
        *out_y = result.Y();
        *out_z = result.Z();
        return true;
    } catch (...) { return false; }
}

/* ================================================================
 * Camera — Fit to bounding box
 * ================================================================ */

OT_EXPORT bool ot_camera_fit_bbox(OTCameraRef camera,
    double xmin, double ymin, double zmin,
    double xmax, double ymax, double zmax) {
    if (!camera) return false;
    try {
        auto* c = static_cast<OTCameraInternal*>(camera);
        Bnd_Box box;
        box.Update(xmin, ymin, zmin, xmax, ymax, zmax);

        // Compute center and radius
        double cx = (xmin + xmax) * 0.5;
        double cy = (ymin + ymax) * 0.5;
        double cz = (zmin + zmax) * 0.5;
        double dx = xmax - xmin;
        double dy = ymax - ymin;
        double dz = zmax - zmin;
        double radius = sqrt(dx*dx + dy*dy + dz*dz) * 0.5;

        c->camera->SetCenter(gp_Pnt(cx, cy, cz));

        // Position eye along current direction
        gp_Dir viewDir = c->camera->Direction();
        double dist = radius * 2.5; // back off by ~2.5x radius
        c->camera->SetEye(gp_Pnt(
            cx - viewDir.X() * dist,
            cy - viewDir.Y() * dist,
            cz - viewDir.Z() * dist));

        c->camera->SetZRange(radius * 0.01, radius * 100.0);

        if (c->camera->ProjectionType() == Graphic3d_Camera::Projection_Orthographic) {
            c->camera->SetScale(radius * 2.2);
        }

        return true;
    } catch (...) { return false; }
}

/* ================================================================
 * Edge Mesh Extraction
 * ================================================================ */

OT_EXPORT OTEdgeMeshData ot_edge_mesh_shape(OTShapeRef shape, double deflection) {
    OTEdgeMeshData result = {nullptr, 0, nullptr, 0};

    if (!shape) {
        g_last_error = "ot_edge_mesh_shape: shape is NULL";
        return result;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate first
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        // Enumerate unique edges
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(s->shape, TopAbs_EDGE, edgeMap);

        if (edgeMap.Extent() == 0) {
            g_last_error = "ot_edge_mesh_shape: no edges found";
            return result;
        }

        // First pass: collect all edge polylines and count total vertices/segments
        struct EdgePolyline {
            std::vector<float> pts; // x,y,z triples
        };
        std::vector<EdgePolyline> polylines;
        int32_t totalVerts = 0;

        for (int i = 1; i <= edgeMap.Extent(); i++) {
            TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
            EdgePolyline poly;

            // Strategy 1: PolygonOnTriangulation (preferred — uses tessellation already computed)
            bool found = false;
            TopLoc_Location loc;
            for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More() && !found; faceExp.Next()) {
                TopoDS_Face face = TopoDS::Face(faceExp.Current());
                Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
                if (tri.IsNull()) continue;

                Handle(Poly_PolygonOnTriangulation) polyOnTri =
                    BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
                if (polyOnTri.IsNull()) continue;

                gp_Trsf transform;
                bool hasTransform = !loc.IsIdentity();
                if (hasTransform) transform = loc.Transformation();

                const TColStd_Array1OfInteger& nodes = polyOnTri->Nodes();
                for (int j = nodes.Lower(); j <= nodes.Upper(); j++) {
                    gp_Pnt pt = tri->Node(nodes(j));
                    if (hasTransform) pt.Transform(transform);
                    poly.pts.push_back(static_cast<float>(pt.X()));
                    poly.pts.push_back(static_cast<float>(pt.Y()));
                    poly.pts.push_back(static_cast<float>(pt.Z()));
                }
                found = true;
            }

            // Strategy 2: Polygon3D
            if (!found) {
                TopLoc_Location edgeLoc;
                Handle(Poly_Polygon3D) poly3d = BRep_Tool::Polygon3D(edge, edgeLoc);
                if (!poly3d.IsNull()) {
                    gp_Trsf transform;
                    bool hasTransform = !edgeLoc.IsIdentity();
                    if (hasTransform) transform = edgeLoc.Transformation();

                    const TColgp_Array1OfPnt& nodes = poly3d->Nodes();
                    for (int j = nodes.Lower(); j <= nodes.Upper(); j++) {
                        gp_Pnt pt = nodes(j);
                        if (hasTransform) pt.Transform(transform);
                        poly.pts.push_back(static_cast<float>(pt.X()));
                        poly.pts.push_back(static_cast<float>(pt.Y()));
                        poly.pts.push_back(static_cast<float>(pt.Z()));
                    }
                    found = true;
                }
            }

            // Strategy 3: GCPnts_TangentialDeflection (curve sampling)
            if (!found) {
                try {
                    BRepAdaptor_Curve curve(edge);
                    GCPnts_TangentialDeflection discretizer(curve, deflection, 0.1);
                    for (int j = 1; j <= discretizer.NbPoints(); j++) {
                        gp_Pnt pt = discretizer.Value(j);
                        poly.pts.push_back(static_cast<float>(pt.X()));
                        poly.pts.push_back(static_cast<float>(pt.Y()));
                        poly.pts.push_back(static_cast<float>(pt.Z()));
                    }
                    found = (discretizer.NbPoints() > 0);
                } catch (...) {
                    // Edge may be degenerate — skip
                }
            }

            if (found && poly.pts.size() >= 6) { // at least 2 points
                totalVerts += static_cast<int32_t>(poly.pts.size() / 3);
                polylines.push_back(std::move(poly));
            }
        }

        if (polylines.empty()) {
            g_last_error = "ot_edge_mesh_shape: no edge polylines generated";
            return result;
        }

        // Allocate output buffers
        result.vertices = static_cast<float*>(malloc(totalVerts * 3 * sizeof(float)));
        result.segment_starts = static_cast<int32_t*>(malloc(polylines.size() * sizeof(int32_t)));
        if (!result.vertices || !result.segment_starts) {
            free(result.vertices);
            free(result.segment_starts);
            result = {nullptr, 0, nullptr, 0};
            g_last_error = "ot_edge_mesh_shape: memory allocation failed";
            return result;
        }

        int32_t vertOffset = 0;
        for (size_t i = 0; i < polylines.size(); i++) {
            result.segment_starts[i] = vertOffset;
            int32_t npts = static_cast<int32_t>(polylines[i].pts.size());
            memcpy(result.vertices + vertOffset * 3, polylines[i].pts.data(), npts * sizeof(float));
            vertOffset += npts / 3;
        }

        result.vertex_count = totalVerts;
        result.segment_count = static_cast<int32_t>(polylines.size());
        g_last_error.clear();
        return result;

    } catch (const Standard_Failure& e) {
        free(result.vertices);
        free(result.segment_starts);
        result = {nullptr, 0, nullptr, 0};
        g_last_error = std::string("ot_edge_mesh_shape: ") + e.GetMessageString();
        return result;
    } catch (...) {
        free(result.vertices);
        free(result.segment_starts);
        result = {nullptr, 0, nullptr, 0};
        g_last_error = "ot_edge_mesh_shape: unknown exception";
        return result;
    }
}

OT_EXPORT void ot_edge_mesh_free(OTEdgeMeshData* mesh) {
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->segment_starts);
    mesh->vertices = nullptr;
    mesh->segment_starts = nullptr;
    mesh->vertex_count = 0;
    mesh->segment_count = 0;
}

/* ================================================================
 * Display Drawer
 * ================================================================ */

OT_EXPORT OTDrawerRef ot_drawer_create(void) {
    try {
        auto* d = new OTDrawerInternal();
        g_last_error.clear();
        return static_cast<OTDrawerRef>(d);
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_drawer_create: ") + e.GetMessageString();
        return nullptr;
    }
}

OT_EXPORT void ot_drawer_destroy(OTDrawerRef drawer) {
    if (!drawer) return;
    delete static_cast<OTDrawerInternal*>(drawer);
}

OT_EXPORT void ot_drawer_set_deviation_coefficient(OTDrawerRef drawer, double coeff) {
    if (!drawer) return;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    d->drawer->SetDeviationCoefficient(coeff);
}

OT_EXPORT double ot_drawer_get_deviation_coefficient(OTDrawerRef drawer) {
    if (!drawer) return 0.001;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    return d->drawer->DeviationCoefficient();
}

OT_EXPORT void ot_drawer_set_deviation_angle(OTDrawerRef drawer, double radians) {
    if (!drawer) return;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    d->drawer->SetDeviationAngle(radians);
}

OT_EXPORT double ot_drawer_get_deviation_angle(OTDrawerRef drawer) {
    if (!drawer) return 0.4;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    return d->drawer->DeviationAngle();
}

OT_EXPORT void ot_drawer_set_max_deviation(OTDrawerRef drawer, double deviation) {
    if (!drawer) return;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    d->drawer->SetMaximalChordialDeviation(deviation);
}

OT_EXPORT double ot_drawer_get_max_deviation(OTDrawerRef drawer) {
    if (!drawer) return 0.1;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    return d->drawer->MaximalChordialDeviation();
}

OT_EXPORT void ot_drawer_set_face_boundary_draw(OTDrawerRef drawer, bool enabled) {
    if (!drawer) return;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    d->drawer->SetFaceBoundaryDraw(enabled);
}

OT_EXPORT bool ot_drawer_get_face_boundary_draw(OTDrawerRef drawer) {
    if (!drawer) return false;
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    return d->drawer->FaceBoundaryDraw();
}

/* ================================================================
 * Mesh/Edge Mesh with Drawer
 * ================================================================ */

OT_EXPORT OTMeshData ot_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer) {
    OTMeshData result = {nullptr, 0, nullptr, 0};
    if (!shape || !drawer) {
        g_last_error = "ot_mesh_shape_with_drawer: NULL argument";
        return result;
    }
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    double deflection = d->drawer->MaximalChordialDeviation();
    return ot_mesh_shape(shape, deflection);
}

OT_EXPORT OTEdgeMeshData ot_edge_mesh_shape_with_drawer(OTShapeRef shape, OTDrawerRef drawer) {
    OTEdgeMeshData result = {nullptr, 0, nullptr, 0};
    if (!shape || !drawer) {
        g_last_error = "ot_edge_mesh_shape_with_drawer: NULL argument";
        return result;
    }
    auto* d = static_cast<OTDrawerInternal*>(drawer);
    double deflection = d->drawer->MaximalChordialDeviation();
    return ot_edge_mesh_shape(shape, deflection);
}

} // extern "C"
