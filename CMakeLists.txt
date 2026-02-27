cmake_minimum_required(VERSION 3.20)
project(simpleOCCTVP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------
# OCCT dependency
# --------------------

# Look for OCCT installed by build-occt-deps.sh
set(OCCT_INSTALL_DIR "${CMAKE_SOURCE_DIR}/deps/occt-install"
    CACHE PATH "Path to OCCT installation directory")

if(EXISTS "${OCCT_INSTALL_DIR}/include/opencascade")
    set(OCCT_INCLUDE_DIR "${OCCT_INSTALL_DIR}/include/opencascade")
elseif(EXISTS "${OCCT_INSTALL_DIR}/include")
    set(OCCT_INCLUDE_DIR "${OCCT_INSTALL_DIR}/include")
elseif(EXISTS "${OCCT_INSTALL_DIR}/inc")
    # Windows OCCT install layout uses inc/ instead of include/opencascade/
    set(OCCT_INCLUDE_DIR "${OCCT_INSTALL_DIR}/inc")
else()
    message(FATAL_ERROR
        "OCCT headers not found at ${OCCT_INSTALL_DIR}.\n"
        "Run scripts/build-occt-deps.sh first, or set OCCT_INSTALL_DIR.")
endif()

# OCCT lib directory: Unix uses lib/, Windows uses win64/vc14/lib/
if(EXISTS "${OCCT_INSTALL_DIR}/lib")
    set(OCCT_LIB_DIR "${OCCT_INSTALL_DIR}/lib")
elseif(EXISTS "${OCCT_INSTALL_DIR}/win64/vc14/lib")
    set(OCCT_LIB_DIR "${OCCT_INSTALL_DIR}/win64/vc14/lib")
else()
    message(FATAL_ERROR "OCCT libraries not found at ${OCCT_INSTALL_DIR}")
endif()

message(STATUS "OCCT include dir: ${OCCT_INCLUDE_DIR}")
message(STATUS "OCCT lib dir: ${OCCT_LIB_DIR}")

# --------------------
# Library sources
# --------------------

set(SOURCES
    src/occt_templot_io.cpp
    src/occt_templot_heal.cpp
    src/occt_templot_mesh.cpp
    src/occt_templot_viewer.cpp
    src/occt_templot_render_data.cpp
)

add_library(simpleOCCTVP SHARED ${SOURCES})

target_compile_definitions(simpleOCCTVP PRIVATE OCCT_TEMPLOT_BUILDING)

target_include_directories(simpleOCCTVP
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${OCCT_INCLUDE_DIR}
)

# --------------------
# Link OCCT static libraries
# --------------------

# Core OCCT toolkits needed for I/O + healing + meshing
set(OCCT_LIBS
    # Data Exchange — format-specific
    TKDESTL
    TKDESTEP
    TKDEIGES
    TKDEOBJ
    TKDEPLY
    TKRWMesh
    TKDE
    TKXSBase
    # XCAF (Extended Data Framework, needed by OBJ/PLY I/O)
    TKVCAF
    TKXCAF
    # Visualization — OpenGL offscreen rendering
    TKOpenGl
    TKV3d
    TKHLR
    TKService
    TKCAF
    TKLCAF
    TKCDF
    # Meshing
    TKMesh
    TKXMesh
    # Shape Healing
    TKShHealing
    # Modeling Algorithms
    TKTopAlgo
    TKGeomAlgo
    TKBO
    TKPrim
    TKFeat
    TKFillet
    TKOffset
    TKBool
    # Modeling Data
    TKBRep
    TKGeomBase
    TKG3d
    TKG2d
    # Foundation
    TKMath
    TKernel
)

foreach(LIB ${OCCT_LIBS})
    find_library(OCCT_${LIB} ${LIB} PATHS ${OCCT_LIB_DIR} NO_DEFAULT_PATH)
    if(OCCT_${LIB})
        target_link_libraries(simpleOCCTVP PRIVATE ${OCCT_${LIB}})
        message(STATUS "  Found: ${LIB}")
    else()
        message(WARNING "  OCCT library not found: ${LIB}")
    endif()
endforeach()

# --------------------
# Platform-specific settings
# --------------------

if(APPLE)
    set_target_properties(simpleOCCTVP PROPERTIES
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_RPATH TRUE
    )
    # Link system frameworks (Foundation + OpenGL/AppKit/IOKit for TKOpenGl)
    target_link_libraries(simpleOCCTVP PRIVATE
        "-framework Foundation"
        "-framework AppKit"
        "-framework OpenGL"
        "-framework IOKit"
    )
    target_link_libraries(simpleOCCTVP PRIVATE objc)
    # Compile viewer as Objective-C++ for NSOpenGLContext fallback
    set_source_files_properties(src/occt_templot_viewer.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++")
elseif(WIN32)
    set_target_properties(simpleOCCTVP PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
    target_link_libraries(simpleOCCTVP PRIVATE ws2_32 opengl32 glu32 user32 gdi32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(simpleOCCTVP PRIVATE OpenGL::GL)
    target_link_libraries(simpleOCCTVP PRIVATE X11 dl pthread)
    set_target_properties(simpleOCCTVP PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Symbol visibility: only export functions marked with OT_EXPORT
set_target_properties(simpleOCCTVP PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Output name
set_target_properties(simpleOCCTVP PROPERTIES
    OUTPUT_NAME "simpleOCCTVP"
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# --------------------
# Tests (optional)
# --------------------

option(BUILD_TESTS "Build test programs" ON)

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_io tests/test_io.cpp)
    target_include_directories(test_io PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_io PRIVATE simpleOCCTVP)

    add_executable(test_heal tests/test_heal.cpp)
    target_include_directories(test_heal PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_heal PRIVATE simpleOCCTVP)

    add_executable(test_viewer tests/test_viewer.cpp)
    target_include_directories(test_viewer PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_viewer PRIVATE simpleOCCTVP)

    add_executable(test_render_data tests/test_render_data.cpp)
    target_include_directories(test_render_data PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_render_data PRIVATE simpleOCCTVP)

    add_test(NAME test_io COMMAND test_io)
    add_test(NAME test_heal COMMAND test_heal)
    add_test(NAME test_viewer COMMAND test_viewer)
    add_test(NAME test_render_data COMMAND test_render_data)
endif()

# --------------------
# Install
# --------------------

install(TARGETS simpleOCCTVP
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES src/occt_templot.h DESTINATION include)
