/*
 * occt_templot_internal.h — Internal types shared across implementation files
 *
 * NOT part of the public API. Do not distribute.
 */

#ifndef OCCT_TEMPLOT_INTERNAL_H
#define OCCT_TEMPLOT_INTERNAL_H

#include <TopoDS_Shape.hxx>
#include <Standard_Failure.hxx>
#include <string>

/* Internal shape wrapper — the opaque handle points to this. */
struct OTShapeInternal {
    TopoDS_Shape shape;

    OTShapeInternal() {}
    OTShapeInternal(const TopoDS_Shape& s) : shape(s) {}
};

/* Thread-local error string (defined in occt_templot_io.cpp) */
extern thread_local std::string g_last_error;

/* Forward declarations for internal structs (defined in their respective .cpp files) */
struct OTViewerInternal;   // occt_templot_viewer.cpp
struct OTCameraInternal;   // occt_templot_render_data.cpp
struct OTDrawerInternal;   // occt_templot_render_data.cpp

#endif /* OCCT_TEMPLOT_INTERNAL_H */
