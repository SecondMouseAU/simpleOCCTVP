/*
 * occt_templot_viewer.cpp — Offscreen V3d viewer for occtTemplot
 *
 * Creates an offscreen OpenGL context via OpenGl_GraphicDriver +
 * Aspect_NeutralWindow, renders shapes with full V3d pipeline
 * (lighting, materials, shading), returns RGBA pixels.
 *
 * macOS: headless CGL context + FBO via Aspect_NeutralWindow.
 * All ot_viewer_* calls for a given viewer must come from one thread.
 */

#include "occt_templot.h"
#include "occt_templot_internal.h"

#include <OpenGl_GraphicDriver.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <V3d_AmbientLight.hxx>
#include <V3d_DirectionalLight.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Image_PixMap.hxx>
#include <Image_AlienPixMap.hxx>
#include <V3d_ImageDumpOptions.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_NameOfColor.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_LineAspect.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepMesh_IncrementalMesh.hxx>

#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>

/* ================================================================
 * Internal viewer state
 * ================================================================ */

struct OTViewerInternal {
    Handle(OpenGl_GraphicDriver)   driver;
    Handle(V3d_Viewer)             viewer;
    Handle(V3d_View)               view;
    Handle(AIS_InteractiveContext)  context;
    Handle(Aspect_NeutralWindow)   window;
    std::vector<Handle(AIS_Shape)> shapes;  // indexed by display_id - 1
    int32_t nextDisplayId = 1;
    Image_PixMap pixmap;
    int32_t width, height;
    double deflection = 0.1;
};

extern "C" {

/* ================================================================
 * Lifecycle
 * ================================================================ */

OT_EXPORT OTViewerRef ot_viewer_create(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) {
        g_last_error = "ot_viewer_create: width and height must be positive";
        return nullptr;
    }

    try {
        auto* v = new OTViewerInternal();
        v->width = width;
        v->height = height;

        // Create display connection (empty for offscreen)
        Handle(Aspect_DisplayConnection) dispConn = new Aspect_DisplayConnection();

        // Create OpenGL graphic driver
        v->driver = new OpenGl_GraphicDriver(dispConn, false /* no message box */);
        v->driver->ChangeOptions().buffersNoSwap = true;
        v->driver->ChangeOptions().buffersOpaqueAlpha = true;

        // Create V3d_Viewer with 3-point lighting
        v->viewer = new V3d_Viewer(v->driver);

        // Key light: strong directional from upper-right-front
        Handle(V3d_DirectionalLight) keyLight = new V3d_DirectionalLight(
            gp_Dir(-1.0, -1.0, -2.0),
            Quantity_Color(1.0, 0.98, 0.95, Quantity_TOC_RGB));
        keyLight->SetIntensity(1.2);
        v->viewer->AddLight(keyLight);
        v->viewer->SetLightOn(keyLight);

        // Fill light: softer from left side to reduce harsh shadows
        Handle(V3d_DirectionalLight) fillLight = new V3d_DirectionalLight(
            gp_Dir(1.0, 0.5, -0.5),
            Quantity_Color(0.7, 0.75, 0.85, Quantity_TOC_RGB));
        fillLight->SetIntensity(0.6);
        v->viewer->AddLight(fillLight);
        v->viewer->SetLightOn(fillLight);

        // Rim/back light: subtle from behind for edge definition
        Handle(V3d_DirectionalLight) rimLight = new V3d_DirectionalLight(
            gp_Dir(0.0, 1.0, 0.3),
            Quantity_Color(0.9, 0.9, 1.0, Quantity_TOC_RGB));
        rimLight->SetIntensity(0.4);
        v->viewer->AddLight(rimLight);
        v->viewer->SetLightOn(rimLight);

        // Ambient light for base illumination
        Handle(V3d_AmbientLight) ambLight = new V3d_AmbientLight(
            Quantity_Color(0.25, 0.25, 0.28, Quantity_TOC_RGB));
        v->viewer->AddLight(ambLight);
        v->viewer->SetLightOn(ambLight);

        // Create AIS context
        v->context = new AIS_InteractiveContext(v->viewer);

        // Add view cube orientation indicator (upper-right corner)
        Handle(AIS_ViewCube) viewCube = new AIS_ViewCube();
        viewCube->SetSize(50);
        viewCube->SetBoxColor(Quantity_NOC_GRAY75);
        viewCube->SetTransformPersistence(
            new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers,
                Aspect_TOTP_RIGHT_UPPER, Graphic3d_Vec2i(85, 85)));
        v->context->Display(viewCube, false);

        // Create V3d_View
        v->view = v->viewer->CreateView();

        // Create neutral window (triggers headless CGL + FBO on macOS)
        v->window = new Aspect_NeutralWindow();
        v->window->SetSize(width, height);
        v->view->SetWindow(v->window);

        // Set default background
        v->view->SetBackgroundColor(Quantity_Color(0.2, 0.2, 0.3, Quantity_TOC_RGB));

        // Rendering settings: 8x MSAA for smoother edges
        v->view->ChangeRenderingParams().IsAntialiasingEnabled = true;
        v->view->ChangeRenderingParams().NbMsaaSamples = 8;
        v->view->ChangeRenderingParams().Method = Graphic3d_RM_RASTERIZATION;

        // Auto Z-fit: adjust near/far planes each frame to prevent clipping
        v->view->SetAutoZFitMode(Graphic3d_ZLayerId_Default, 1.0);
        v->view->ZFitAll();

        // Finer default tessellation for smoother curves
        v->deflection = 0.02;

        g_last_error.clear();
        return static_cast<OTViewerRef>(v);

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_create: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_viewer_create: unknown exception";
        return nullptr;
    }
}

OT_EXPORT void ot_viewer_destroy(OTViewerRef viewer) {
    if (!viewer) return;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        // Remove all displayed shapes
        if (!v->context.IsNull()) {
            v->context->RemoveAll(false);
        }
        if (!v->view.IsNull()) {
            v->view->Remove();
        }
        delete v;
    } catch (...) {
        // Suppress exceptions during cleanup
    }
}

OT_EXPORT bool ot_viewer_resize(OTViewerRef viewer, int32_t width, int32_t height) {
    if (!viewer || width <= 0 || height <= 0) {
        g_last_error = "ot_viewer_resize: invalid arguments";
        return false;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        v->width = width;
        v->height = height;
        v->window->SetSize(width, height);
        v->view->MustBeResized();
        g_last_error.clear();
        return true;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_resize: ") + e.GetMessageString();
        return false;
    }
}

OT_EXPORT void ot_viewer_get_size(OTViewerRef viewer, int32_t* out_width, int32_t* out_height) {
    if (!viewer || !out_width || !out_height) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    *out_width = v->width;
    *out_height = v->height;
}

/* ================================================================
 * Shape Display
 * ================================================================ */

OT_EXPORT int32_t ot_viewer_add_shape(OTViewerRef viewer, OTShapeRef shape, OTDisplayMode mode) {
    if (!viewer || !shape) {
        g_last_error = "ot_viewer_add_shape: NULL argument";
        return -1;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        auto* s = static_cast<OTShapeInternal*>(shape);

        // Tessellate before display
        BRepMesh_IncrementalMesh mesher(s->shape, v->deflection);
        mesher.Perform();

        Handle(AIS_Shape) aisShape = new AIS_Shape(s->shape);

        // Set display mode
        AIS_DisplayMode aisMode = AIS_Shaded;
        if (mode == OT_DISPLAY_WIREFRAME) {
            aisMode = AIS_WireFrame;
        }

        v->context->Display(aisShape, aisMode, 0, false);

        // Enable edge display for shaded+edges mode
        if (mode == OT_DISPLAY_SHADED_EDGES) {
            v->context->SetDisplayMode(aisShape, AIS_Shaded, false);
            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->SetFaceBoundaryDraw(true);
            drawer->FaceBoundaryAspect()->SetColor(Quantity_NOC_BLACK);
            v->context->Redisplay(aisShape, false);
        }

        int32_t displayId = v->nextDisplayId++;
        // Ensure vector is large enough
        if (static_cast<int32_t>(v->shapes.size()) < displayId) {
            v->shapes.resize(displayId);
        }
        v->shapes[displayId - 1] = aisShape;

        g_last_error.clear();
        return displayId;

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_add_shape: ") + e.GetMessageString();
        return -1;
    } catch (...) {
        g_last_error = "ot_viewer_add_shape: unknown exception";
        return -1;
    }
}

OT_EXPORT bool ot_viewer_remove_shape(OTViewerRef viewer, int32_t display_id) {
    if (!viewer || display_id < 1) {
        g_last_error = "ot_viewer_remove_shape: invalid arguments";
        return false;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        if (display_id > static_cast<int32_t>(v->shapes.size()) || v->shapes[display_id - 1].IsNull()) {
            g_last_error = "ot_viewer_remove_shape: invalid display_id";
            return false;
        }
        v->context->Remove(v->shapes[display_id - 1], false);
        v->shapes[display_id - 1].Nullify();
        g_last_error.clear();
        return true;
    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_remove_shape: ") + e.GetMessageString();
        return false;
    }
}

OT_EXPORT void ot_viewer_clear(OTViewerRef viewer) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->context->RemoveAll(false);
    v->shapes.clear();
    v->nextDisplayId = 1;
}

OT_EXPORT bool ot_viewer_set_shape_color(OTViewerRef viewer, int32_t display_id,
                                          double r, double g, double b) {
    if (!viewer || display_id < 1) return false;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        if (display_id > static_cast<int32_t>(v->shapes.size()) || v->shapes[display_id - 1].IsNull())
            return false;
        Quantity_Color color(r, g, b, Quantity_TOC_RGB);
        v->context->SetColor(v->shapes[display_id - 1], color, false);
        g_last_error.clear();
        return true;
    } catch (...) { return false; }
}

OT_EXPORT bool ot_viewer_set_shape_transparency(OTViewerRef viewer, int32_t display_id, double t) {
    if (!viewer || display_id < 1) return false;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        if (display_id > static_cast<int32_t>(v->shapes.size()) || v->shapes[display_id - 1].IsNull())
            return false;
        v->context->SetTransparency(v->shapes[display_id - 1], t, false);
        g_last_error.clear();
        return true;
    } catch (...) { return false; }
}

OT_EXPORT bool ot_viewer_set_shape_display_mode(OTViewerRef viewer, int32_t display_id, OTDisplayMode mode) {
    if (!viewer || display_id < 1) return false;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        if (display_id > static_cast<int32_t>(v->shapes.size()) || v->shapes[display_id - 1].IsNull())
            return false;

        Handle(AIS_Shape)& aisShape = v->shapes[display_id - 1];
        if (mode == OT_DISPLAY_WIREFRAME) {
            v->context->SetDisplayMode(aisShape, AIS_WireFrame, false);
        } else {
            v->context->SetDisplayMode(aisShape, AIS_Shaded, false);
        }

        if (mode == OT_DISPLAY_SHADED_EDGES) {
            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->SetFaceBoundaryDraw(true);
            drawer->FaceBoundaryAspect()->SetColor(Quantity_NOC_BLACK);
        } else {
            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->SetFaceBoundaryDraw(false);
        }

        v->context->Redisplay(aisShape, false);
        g_last_error.clear();
        return true;
    } catch (...) { return false; }
}

OT_EXPORT bool ot_viewer_set_shape_material(OTViewerRef viewer, int32_t display_id, const char* name) {
    if (!viewer || display_id < 1 || !name) return false;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        if (display_id > static_cast<int32_t>(v->shapes.size()) || v->shapes[display_id - 1].IsNull())
            return false;

        Graphic3d_NameOfMaterial mat = Graphic3d_NameOfMaterial_DEFAULT;
        std::string matName(name);
        if (matName == "brass")        mat = Graphic3d_NameOfMaterial_Brass;
        else if (matName == "bronze")  mat = Graphic3d_NameOfMaterial_Bronze;
        else if (matName == "copper")  mat = Graphic3d_NameOfMaterial_Copper;
        else if (matName == "gold")    mat = Graphic3d_NameOfMaterial_Gold;
        else if (matName == "pewter")  mat = Graphic3d_NameOfMaterial_Pewter;
        else if (matName == "silver")  mat = Graphic3d_NameOfMaterial_Silver;
        else if (matName == "steel")   mat = Graphic3d_NameOfMaterial_Steel;
        else if (matName == "stone")   mat = Graphic3d_NameOfMaterial_Stone;
        else if (matName == "chrome")  mat = Graphic3d_NameOfMaterial_Chrome;
        else if (matName == "aluminium" || matName == "aluminum")
                                       mat = Graphic3d_NameOfMaterial_Aluminum;
        else if (matName == "plastic") mat = Graphic3d_NameOfMaterial_Plastified;
        else if (matName == "default") mat = Graphic3d_NameOfMaterial_DEFAULT;
        else {
            g_last_error = "ot_viewer_set_shape_material: unknown material name";
            return false;
        }

        v->context->SetMaterial(v->shapes[display_id - 1], Graphic3d_MaterialAspect(mat), false);
        g_last_error.clear();
        return true;
    } catch (...) { return false; }
}

/* ================================================================
 * Background
 * ================================================================ */

OT_EXPORT void ot_viewer_set_background_color(OTViewerRef viewer, double r, double g, double b) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->SetBackgroundColor(Quantity_Color(r, g, b, Quantity_TOC_RGB));
}

OT_EXPORT void ot_viewer_set_background_gradient(OTViewerRef viewer,
    double r1, double g1, double b1, double r2, double g2, double b2) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->SetBgGradientColors(
        Quantity_Color(r1, g1, b1, Quantity_TOC_RGB),
        Quantity_Color(r2, g2, b2, Quantity_TOC_RGB),
        Aspect_GradientFillMethod_Vertical);
}

/* ================================================================
 * Camera Control
 * ================================================================ */

OT_EXPORT void ot_viewer_set_projection(OTViewerRef viewer, OTProjectionType type) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    if (type == OT_PROJECTION_ORTHOGRAPHIC) {
        v->view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
    } else {
        v->view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    }
}

OT_EXPORT void ot_viewer_set_fov(OTViewerRef viewer, double degrees) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->Camera()->SetFOVy(degrees);
}

OT_EXPORT void ot_viewer_set_view_orientation(OTViewerRef viewer, OTViewOrientation orient) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);

    gp_Dir dir, up;
    switch (orient) {
        case OT_VIEW_FRONT:    dir = gp_Dir( 0, -1,  0); up = gp_Dir(0, 0, 1); break;
        case OT_VIEW_BACK:     dir = gp_Dir( 0,  1,  0); up = gp_Dir(0, 0, 1); break;
        case OT_VIEW_TOP:      dir = gp_Dir( 0,  0, -1); up = gp_Dir(0, 1, 0); break;
        case OT_VIEW_BOTTOM:   dir = gp_Dir( 0,  0,  1); up = gp_Dir(0, 1, 0); break;
        case OT_VIEW_LEFT:     dir = gp_Dir( 1,  0,  0); up = gp_Dir(0, 0, 1); break;
        case OT_VIEW_RIGHT:    dir = gp_Dir(-1,  0,  0); up = gp_Dir(0, 0, 1); break;
        case OT_VIEW_ISO:      dir = gp_Dir(-1, -1, -1); up = gp_Dir(0, 0, 1); break;
        case OT_VIEW_ISO_LEFT: dir = gp_Dir( 1, -1, -1); up = gp_Dir(0, 0, 1); break;
        default: return;
    }

    gp_Pnt eye = gp_Pnt(0, 0, 0).Translated(gp_Vec(dir) * (-1000.0));
    v->view->Camera()->SetEye(eye);
    v->view->Camera()->SetCenter(gp_Pnt(0, 0, 0));
    v->view->Camera()->SetUp(up);
}

OT_EXPORT void ot_viewer_fit_all(OTViewerRef viewer) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->FitAll(0.01, false);
}

OT_EXPORT void ot_viewer_rotate(OTViewerRef viewer, double dx, double dy) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    // StartRotation sets the rotation origin; Rotation applies from there
    int cx = v->width / 2;
    int cy = v->height / 2;
    v->view->StartRotation(cx, cy);
    v->view->Rotation(cx + static_cast<int>(dx), cy + static_cast<int>(dy));
    v->view->ZFitAll();
}

OT_EXPORT void ot_viewer_pan(OTViewerRef viewer, double dx, double dy) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    // Negate dy: screen Y increases downward, OCCT Y increases upward
    v->view->Pan(static_cast<int>(dx), static_cast<int>(-dy));
    v->view->ZFitAll();
}

OT_EXPORT void ot_viewer_zoom(OTViewerRef viewer, double factor) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->SetZoom(factor);
    v->view->ZFitAll();
}

OT_EXPORT void ot_viewer_zoom_at_point(OTViewerRef viewer,
    int32_t x, int32_t y, double factor) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    // StartZoomAtPoint records the world point under (x,y)
    // ZoomAtPoint zooms toward/away from that point
    v->view->StartZoomAtPoint(x, y);
    int zoomDelta = static_cast<int>((factor - 1.0) * v->height);
    v->view->ZoomAtPoint(x, y, x, y + zoomDelta);
    v->view->ZFitAll();
}

OT_EXPORT void ot_viewer_set_camera(OTViewerRef viewer,
    double eye_x, double eye_y, double eye_z,
    double center_x, double center_y, double center_z,
    double up_x, double up_y, double up_z) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->Camera()->SetEye(gp_Pnt(eye_x, eye_y, eye_z));
    v->view->Camera()->SetCenter(gp_Pnt(center_x, center_y, center_z));
    v->view->Camera()->SetUp(gp_Dir(up_x, up_y, up_z));
}

OT_EXPORT void ot_viewer_get_camera(OTViewerRef viewer,
    double* eye_x, double* eye_y, double* eye_z,
    double* center_x, double* center_y, double* center_z,
    double* up_x, double* up_y, double* up_z) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    const Handle(Graphic3d_Camera)& cam = v->view->Camera();
    gp_Pnt eye = cam->Eye();
    gp_Pnt center = cam->Center();
    gp_Dir up = cam->Up();
    if (eye_x) *eye_x = eye.X();
    if (eye_y) *eye_y = eye.Y();
    if (eye_z) *eye_z = eye.Z();
    if (center_x) *center_x = center.X();
    if (center_y) *center_y = center.Y();
    if (center_z) *center_z = center.Z();
    if (up_x) *up_x = up.X();
    if (up_y) *up_y = up.Y();
    if (up_z) *up_z = up.Z();
}

/* ================================================================
 * Lighting
 * ================================================================ */

OT_EXPORT void ot_viewer_set_headlight(OTViewerRef viewer, bool enabled) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    if (enabled) {
        v->view->SetLightOn();
    } else {
        v->view->SetLightOff();
    }
}

OT_EXPORT void ot_viewer_add_directional_light(OTViewerRef viewer,
    double dir_x, double dir_y, double dir_z,
    double r, double g, double b) {
    if (!viewer) return;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        Handle(V3d_DirectionalLight) light = new V3d_DirectionalLight(
            gp_Dir(dir_x, dir_y, dir_z),
            Quantity_Color(r, g, b, Quantity_TOC_RGB));
        v->viewer->AddLight(light);
        v->viewer->SetLightOn(light);
    } catch (...) {}
}

OT_EXPORT void ot_viewer_set_ambient_light(OTViewerRef viewer, double intensity) {
    if (!viewer) return;
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);
        Handle(V3d_AmbientLight) light = new V3d_AmbientLight(
            Quantity_Color(intensity, intensity, intensity, Quantity_TOC_RGB));
        v->viewer->AddLight(light);
        v->viewer->SetLightOn(light);
    } catch (...) {}
}

/* ================================================================
 * Rendering
 * ================================================================ */

OT_EXPORT const uint8_t* ot_viewer_render(OTViewerRef viewer,
    int32_t* out_width, int32_t* out_height, int32_t* out_size) {
    if (!viewer) {
        g_last_error = "ot_viewer_render: NULL viewer";
        return nullptr;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);

        v->view->Redraw();

        V3d_ImageDumpOptions dumpOpts;
        dumpOpts.Width = v->width;
        dumpOpts.Height = v->height;
        dumpOpts.BufferType = Graphic3d_BT_RGBA;

        if (!v->view->ToPixMap(v->pixmap, dumpOpts)) {
            g_last_error = "ot_viewer_render: ToPixMap failed";
            return nullptr;
        }

        if (out_width)  *out_width  = v->width;
        if (out_height) *out_height = v->height;
        if (out_size)   *out_size   = static_cast<int32_t>(v->pixmap.SizeBytes());

        g_last_error.clear();
        return v->pixmap.Data();

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_render: ") + e.GetMessageString();
        return nullptr;
    } catch (...) {
        g_last_error = "ot_viewer_render: unknown exception";
        return nullptr;
    }
}

OT_EXPORT bool ot_viewer_render_to_buffer(OTViewerRef viewer,
    uint8_t* buffer, int32_t buffer_size) {
    if (!viewer || !buffer) {
        g_last_error = "ot_viewer_render_to_buffer: NULL argument";
        return false;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);

        v->view->Redraw();

        V3d_ImageDumpOptions dumpOpts;
        dumpOpts.Width = v->width;
        dumpOpts.Height = v->height;
        dumpOpts.BufferType = Graphic3d_BT_RGBA;

        Image_PixMap pixmap;
        if (!v->view->ToPixMap(pixmap, dumpOpts)) {
            g_last_error = "ot_viewer_render_to_buffer: ToPixMap failed";
            return false;
        }

        int32_t rowBytes = v->width * 4;
        int32_t requiredSize = rowBytes * v->height;
        if (buffer_size < requiredSize) {
            g_last_error = "ot_viewer_render_to_buffer: buffer too small";
            return false;
        }

        // Flip vertically: OpenGL bottom-to-top → top-to-bottom for TBitmap
        for (int32_t y = 0; y < v->height; y++) {
            const uint8_t* srcRow = pixmap.Row(v->height - 1 - y);
            uint8_t* dstRow = buffer + y * rowBytes;
            memcpy(dstRow, srcRow, rowBytes);
        }

        g_last_error.clear();
        return true;

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_render_to_buffer: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_viewer_render_to_buffer: unknown exception";
        return false;
    }
}

OT_EXPORT bool ot_viewer_save_image(OTViewerRef viewer, const char* path) {
    if (!viewer || !path) {
        g_last_error = "ot_viewer_save_image: NULL argument";
        return false;
    }
    try {
        auto* v = static_cast<OTViewerInternal*>(viewer);

        v->view->Redraw();

        V3d_ImageDumpOptions dumpOpts;
        dumpOpts.Width = v->width;
        dumpOpts.Height = v->height;
        dumpOpts.BufferType = Graphic3d_BT_RGBA;

        Image_PixMap pixmap;
        if (!v->view->ToPixMap(pixmap, dumpOpts)) {
            g_last_error = "ot_viewer_save_image: ToPixMap failed";
            return false;
        }

        // Write raw BMP manually (OCCT Image_PixMap doesn't have Save without FreeImage)
        TCollection_AsciiString filePath(path);
        // Use Image_AlienPixMap which supports writing
        Image_AlienPixMap alienPixmap;
        if (!v->view->ToPixMap(alienPixmap, dumpOpts)) {
            g_last_error = "ot_viewer_save_image: ToPixMap failed for alien pixmap";
            return false;
        }
        if (!alienPixmap.Save(filePath)) {
            g_last_error = "ot_viewer_save_image: Save failed";
            return false;
        }

        g_last_error.clear();
        return true;

    } catch (const Standard_Failure& e) {
        g_last_error = std::string("ot_viewer_save_image: ") + e.GetMessageString();
        return false;
    } catch (...) {
        g_last_error = "ot_viewer_save_image: unknown exception";
        return false;
    }
}

/* ================================================================
 * Display Settings
 * ================================================================ */

OT_EXPORT void ot_viewer_set_edge_display(OTViewerRef viewer, bool enabled) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    for (auto& aisShape : v->shapes) {
        if (!aisShape.IsNull()) {
            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->SetFaceBoundaryDraw(enabled);
            v->context->Redisplay(aisShape, false);
        }
    }
}

OT_EXPORT void ot_viewer_set_edge_color(OTViewerRef viewer, double r, double g, double b) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    Quantity_Color color(r, g, b, Quantity_TOC_RGB);
    for (auto& aisShape : v->shapes) {
        if (!aisShape.IsNull()) {
            Handle(Prs3d_Drawer) drawer = aisShape->Attributes();
            drawer->FaceBoundaryAspect()->SetColor(color);
            v->context->Redisplay(aisShape, false);
        }
    }
}

OT_EXPORT void ot_viewer_set_deflection(OTViewerRef viewer, double deflection) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->deflection = deflection;
}

OT_EXPORT void ot_viewer_set_msaa(OTViewerRef viewer, int32_t samples) {
    if (!viewer) return;
    auto* v = static_cast<OTViewerInternal*>(viewer);
    v->view->ChangeRenderingParams().NbMsaaSamples = samples;
}

} // extern "C"
