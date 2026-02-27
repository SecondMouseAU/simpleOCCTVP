/*
 * occt_templot_mesh.cpp — Mesh extraction module for occtTemplot
 *
 * Extracts triangle meshes from shapes for rendering or export.
 * Ported from OCCTSwift's OCCTBridge.mm, stripped of Objective-C dependencies.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>

#include <cstdlib>
#include <cmath>
#include <algorithm>

extern "C" {

/* ================================================================
 * Mesh Extraction — Interleaved (position + normal)
 * ================================================================ */

OT_EXPORT OTMeshData ot_mesh_shape(OTShapeRef shape, double deflection) {
    OTMeshData result = {nullptr, 0, nullptr, 0};

    if (!shape) {
        g_last_error = "ot_mesh_shape: shape is NULL";
        return result;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Generate mesh
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        // First pass: count vertices and triangles
        int32_t totalVerts = 0;
        int32_t totalTris = 0;

        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            TopoDS_Face face = TopoDS::Face(faceExp.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            totalVerts += tri->NbNodes();
            totalTris += tri->NbTriangles();
        }

        if (totalVerts == 0 || totalTris == 0) {
            g_last_error = "ot_mesh_shape: no triangulation produced";
            return result;
        }

        // Allocate buffers: interleaved position + normal (6 floats per vertex)
        result.vertices = static_cast<float*>(malloc(totalVerts * 6 * sizeof(float)));
        result.indices = static_cast<int32_t*>(malloc(totalTris * 3 * sizeof(int32_t)));
        if (!result.vertices || !result.indices) {
            free(result.vertices);
            free(result.indices);
            result.vertices = nullptr;
            result.indices = nullptr;
            g_last_error = "ot_mesh_shape: memory allocation failed";
            return result;
        }

        int32_t vertexOffset = 0;
        int32_t triOffset = 0;

        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            TopoDS_Face face = TopoDS::Face(faceExp.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;

            gp_Trsf transform;
            bool hasTransform = !loc.IsIdentity();
            if (hasTransform) {
                transform = loc.Transformation();
            }

            bool reversed = (face.Orientation() == TopAbs_REVERSED);
            bool hasNormals = tri->HasNormals();

            // Write vertex positions and normals
            for (int i = 1; i <= tri->NbNodes(); i++) {
                gp_Pnt node = tri->Node(i);
                if (hasTransform) node.Transform(transform);

                float* vPtr = result.vertices + (vertexOffset + i - 1) * 6;
                vPtr[0] = static_cast<float>(node.X());
                vPtr[1] = static_cast<float>(node.Y());
                vPtr[2] = static_cast<float>(node.Z());

                if (hasNormals) {
                    gp_Dir normal = tri->Normal(i);
                    if (hasTransform) normal.Transform(transform);
                    if (reversed) normal.Reverse();
                    vPtr[3] = static_cast<float>(normal.X());
                    vPtr[4] = static_cast<float>(normal.Y());
                    vPtr[5] = static_cast<float>(normal.Z());
                } else {
                    vPtr[3] = 0.0f;
                    vPtr[4] = 0.0f;
                    vPtr[5] = 0.0f;
                }
            }

            // Compute normals from triangles if not available
            if (!hasNormals) {
                for (int i = 1; i <= tri->NbTriangles(); i++) {
                    int n1, n2, n3;
                    tri->Triangle(i).Get(n1, n2, n3);
                    if (reversed) std::swap(n2, n3);

                    gp_Pnt p1 = tri->Node(n1), p2 = tri->Node(n2), p3 = tri->Node(n3);
                    if (hasTransform) {
                        p1.Transform(transform);
                        p2.Transform(transform);
                        p3.Transform(transform);
                    }

                    gp_Vec v1(p1, p2), v2(p1, p3);
                    gp_Vec fn = v1.Crossed(v2);
                    double mag = fn.Magnitude();
                    if (mag > 1e-10) {
                        fn.Divide(mag);
                        for (int idx : {n1, n2, n3}) {
                            float* nPtr = result.vertices + (vertexOffset + idx - 1) * 6 + 3;
                            nPtr[0] += static_cast<float>(fn.X());
                            nPtr[1] += static_cast<float>(fn.Y());
                            nPtr[2] += static_cast<float>(fn.Z());
                        }
                    }
                }
                // Normalize accumulated normals
                for (int i = 0; i < tri->NbNodes(); i++) {
                    float* nPtr = result.vertices + (vertexOffset + i) * 6 + 3;
                    float len = sqrtf(nPtr[0] * nPtr[0] + nPtr[1] * nPtr[1] + nPtr[2] * nPtr[2]);
                    if (len > 1e-6f) {
                        nPtr[0] /= len;
                        nPtr[1] /= len;
                        nPtr[2] /= len;
                    }
                }
            }

            // Triangle indices
            for (int i = 1; i <= tri->NbTriangles(); i++) {
                int n1, n2, n3;
                tri->Triangle(i).Get(n1, n2, n3);
                if (reversed) std::swap(n2, n3);

                int32_t* tPtr = result.indices + triOffset * 3;
                tPtr[0] = vertexOffset + n1 - 1;
                tPtr[1] = vertexOffset + n2 - 1;
                tPtr[2] = vertexOffset + n3 - 1;
                triOffset++;
            }

            vertexOffset += tri->NbNodes();
        }

        result.vertex_count = totalVerts;
        result.triangle_count = totalTris;
        g_last_error.clear();
        return result;

    } catch (const Standard_Failure& e) {
        free(result.vertices);
        free(result.indices);
        result = {nullptr, 0, nullptr, 0};
        g_last_error = std::string("ot_mesh_shape: ") + e.GetMessageString();
        return result;
    } catch (...) {
        free(result.vertices);
        free(result.indices);
        result = {nullptr, 0, nullptr, 0};
        g_last_error = "ot_mesh_shape: unknown exception";
        return result;
    }
}

OT_EXPORT void ot_mesh_free(OTMeshData* mesh) {
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    mesh->vertices = nullptr;
    mesh->indices = nullptr;
    mesh->vertex_count = 0;
    mesh->triangle_count = 0;
}

/* ================================================================
 * Mesh Extraction — Separate arrays (for Pascal convenience)
 * ================================================================ */

OT_EXPORT bool ot_mesh_shape_separate(OTShapeRef shape, double deflection,
    int32_t* out_vertex_count, int32_t* out_triangle_count) {
    if (!shape || !out_vertex_count || !out_triangle_count) {
        g_last_error = "ot_mesh_shape_separate: NULL argument";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Generate mesh
        BRepMesh_IncrementalMesh mesher(s->shape, deflection);
        mesher.Perform();

        // Count vertices and triangles
        int32_t totalVerts = 0;
        int32_t totalTris = 0;

        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            TopoDS_Face face = TopoDS::Face(faceExp.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            totalVerts += tri->NbNodes();
            totalTris += tri->NbTriangles();
        }

        *out_vertex_count = totalVerts;
        *out_triangle_count = totalTris;
        g_last_error.clear();
        return (totalVerts > 0 && totalTris > 0);

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_mesh_shape_separate: ") + e.GetMessageString();
        *out_vertex_count = 0;
        *out_triangle_count = 0;
        return false;
    } catch (...) {
        g_last_error = "ot_mesh_shape_separate: unknown exception";
        *out_vertex_count = 0;
        *out_triangle_count = 0;
        return false;
    }
}

OT_EXPORT bool ot_mesh_shape_fill(OTShapeRef shape,
    float* out_vertices, float* out_normals, int32_t* out_indices) {
    if (!shape || !out_vertices || !out_normals || !out_indices) {
        g_last_error = "ot_mesh_shape_fill: NULL argument";
        return false;
    }

    try {
        auto* s = static_cast<OTShapeInternal*>(shape);

        int32_t vertexOffset = 0;
        int32_t triOffset = 0;

        for (TopExp_Explorer faceExp(s->shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
            TopoDS_Face face = TopoDS::Face(faceExp.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;

            gp_Trsf transform;
            bool hasTransform = !loc.IsIdentity();
            if (hasTransform) {
                transform = loc.Transformation();
            }

            bool reversed = (face.Orientation() == TopAbs_REVERSED);
            bool hasNormals = tri->HasNormals();

            // Write vertex positions
            for (int i = 1; i <= tri->NbNodes(); i++) {
                gp_Pnt node = tri->Node(i);
                if (hasTransform) node.Transform(transform);

                int idx = (vertexOffset + i - 1) * 3;
                out_vertices[idx + 0] = static_cast<float>(node.X());
                out_vertices[idx + 1] = static_cast<float>(node.Y());
                out_vertices[idx + 2] = static_cast<float>(node.Z());

                if (hasNormals) {
                    gp_Dir normal = tri->Normal(i);
                    if (hasTransform) normal.Transform(transform);
                    if (reversed) normal.Reverse();
                    out_normals[idx + 0] = static_cast<float>(normal.X());
                    out_normals[idx + 1] = static_cast<float>(normal.Y());
                    out_normals[idx + 2] = static_cast<float>(normal.Z());
                } else {
                    out_normals[idx + 0] = 0.0f;
                    out_normals[idx + 1] = 0.0f;
                    out_normals[idx + 2] = 0.0f;
                }
            }

            // Compute normals from triangles if not available
            if (!hasNormals) {
                for (int i = 1; i <= tri->NbTriangles(); i++) {
                    int n1, n2, n3;
                    tri->Triangle(i).Get(n1, n2, n3);
                    if (reversed) std::swap(n2, n3);

                    gp_Pnt p1 = tri->Node(n1), p2 = tri->Node(n2), p3 = tri->Node(n3);
                    if (hasTransform) {
                        p1.Transform(transform);
                        p2.Transform(transform);
                        p3.Transform(transform);
                    }

                    gp_Vec v1(p1, p2), v2(p1, p3);
                    gp_Vec fn = v1.Crossed(v2);
                    double mag = fn.Magnitude();
                    if (mag > 1e-10) {
                        fn.Divide(mag);
                        for (int vidx : {n1, n2, n3}) {
                            int nidx = (vertexOffset + vidx - 1) * 3;
                            out_normals[nidx + 0] += static_cast<float>(fn.X());
                            out_normals[nidx + 1] += static_cast<float>(fn.Y());
                            out_normals[nidx + 2] += static_cast<float>(fn.Z());
                        }
                    }
                }
                // Normalize accumulated normals
                for (int i = 0; i < tri->NbNodes(); i++) {
                    int nidx = (vertexOffset + i) * 3;
                    float len = sqrtf(
                        out_normals[nidx + 0] * out_normals[nidx + 0] +
                        out_normals[nidx + 1] * out_normals[nidx + 1] +
                        out_normals[nidx + 2] * out_normals[nidx + 2]);
                    if (len > 1e-6f) {
                        out_normals[nidx + 0] /= len;
                        out_normals[nidx + 1] /= len;
                        out_normals[nidx + 2] /= len;
                    }
                }
            }

            // Triangle indices
            for (int i = 1; i <= tri->NbTriangles(); i++) {
                int n1, n2, n3;
                tri->Triangle(i).Get(n1, n2, n3);
                if (reversed) std::swap(n2, n3);

                int tidx = triOffset * 3;
                out_indices[tidx + 0] = vertexOffset + n1 - 1;
                out_indices[tidx + 1] = vertexOffset + n2 - 1;
                out_indices[tidx + 2] = vertexOffset + n3 - 1;
                triOffset++;
            }

            vertexOffset += tri->NbNodes();
        }

        g_last_error.clear();
        return true;

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_mesh_shape_fill: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_mesh_shape_fill: unknown exception";
        return false;
    }
}

} // extern "C"
