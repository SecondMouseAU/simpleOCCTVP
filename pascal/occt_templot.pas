unit occt_templot;

{$MODE Delphi}
{$H+}

{  occt_templot.pas — Pascal bindings for libocct_templot

   Cross-platform OCCT wrapper library for Templot.
   Provides STL/STEP/IGES/OBJ I/O, shape healing, mesh extraction,
   shape analysis, offscreen 3D rendering, standalone camera/edge mesh,
   and display drawer via cdecl function calls.

   Usage:
     1. Call occt_templot_init at program startup
     2. Use ot_import_xxx / ot_export_xxx for file I/O
     3. Use ot_heal_shape / ot_analyze_shape for mesh repair
     4. Use ot_mesh_shape for triangle extraction
     5. Use ot_viewer_xxx for offscreen 3D rendering
     6. Use ot_camera_xxx for standalone camera matrices
     7. Use ot_edge_mesh_xxx for edge polyline extraction
     8. Free all shapes with ot_shape_free
     9. Call occt_templot_shutdown at program exit
}

interface

uses
  SysUtils, Classes, Graphics, IntfGraphics, FPimage;

const
  {$IFDEF WINDOWS}
  OCCT_LIB = 'simpleOCCTVP.dll';
  {$ENDIF}
  {$IFDEF DARWIN}
  OCCT_LIB = 'libsimpleOCCTVP.dylib';
  {$ENDIF}
  {$IFDEF LINUX}
  OCCT_LIB = 'libsimpleOCCTVP.so';
  {$ENDIF}

  { Display modes }
  OT_DISPLAY_WIREFRAME    = 0;
  OT_DISPLAY_SHADED       = 1;
  OT_DISPLAY_SHADED_EDGES = 2;

  { View orientations }
  OT_VIEW_FRONT    = 0;
  OT_VIEW_BACK     = 1;
  OT_VIEW_TOP      = 2;
  OT_VIEW_BOTTOM   = 3;
  OT_VIEW_LEFT     = 4;
  OT_VIEW_RIGHT    = 5;
  OT_VIEW_ISO      = 6;
  OT_VIEW_ISO_LEFT = 7;

  { Projection types }
  OT_PROJECTION_PERSPECTIVE  = 0;
  OT_PROJECTION_ORTHOGRAPHIC = 1;

type
  { Opaque handle to a shape. }
  OTShapeRef = Pointer;

  { Opaque handle to an offscreen V3d viewer. }
  OTViewerRef = Pointer;

  { Opaque handle to a standalone camera. }
  OTCameraRef = Pointer;

  { Opaque handle to a display drawer. }
  OTDrawerRef = Pointer;

  { Shape analysis result. }
  OTShapeAnalysis = record
    small_edge_count:        Int32;
    small_face_count:        Int32;
    gap_count:               Int32;
    self_intersection_count: Int32;
    free_edge_count:         Int32;
    free_face_count:         Int32;
    has_invalid_topology:    Boolean;
    is_valid:                Boolean;
  end;

  { Import diagnostics result. }
  OTImportResult = record
    shape:            OTShapeRef;
    original_type:    Int32;
    result_type:      Int32;
    sewing_applied:   Boolean;
    solid_created:    Boolean;
    healing_applied:  Boolean;
  end;

  { Mesh data (interleaved position+normal, 6 floats per vertex). }
  POTMeshData = ^OTMeshData;
  OTMeshData = record
    vertices:       PSingle;   { Interleaved [x,y,z, nx,ny,nz, ...] }
    vertex_count:   Int32;
    indices:        PInt32;    { Triangle indices [i0,i1,i2, ...] }
    triangle_count: Int32;
  end;

  { Edge mesh data (polyline vertices + segment start indices). }
  POTEdgeMeshData = ^OTEdgeMeshData;
  OTEdgeMeshData = record
    vertices:       PSingle;   { [x,y,z, ...] 3 floats per vertex }
    vertex_count:   Int32;
    segment_starts: PInt32;    { Index where each edge polyline begins }
    segment_count:  Int32;
  end;

{ ================================================================
  Lifecycle
  ================================================================ }

procedure occt_templot_init; cdecl; external OCCT_LIB;
procedure occt_templot_shutdown; cdecl; external OCCT_LIB;
function  occt_templot_version: PAnsiChar; cdecl; external OCCT_LIB;
function  occt_templot_last_error: PAnsiChar; cdecl; external OCCT_LIB;

{ ================================================================
  Shape Handle
  ================================================================ }

procedure ot_shape_free(shape: OTShapeRef); cdecl; external OCCT_LIB;
function  ot_shape_is_valid(shape: OTShapeRef): Boolean; cdecl; external OCCT_LIB;
function  ot_shape_type(shape: OTShapeRef): Int32; cdecl; external OCCT_LIB;

{ ================================================================
  I/O — Import
  ================================================================ }

function ot_import_stl(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_stl_robust(path: PAnsiChar; sewing_tolerance: Double): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_step(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_step_robust(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_iges(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_iges_robust(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;
function ot_import_obj(path: PAnsiChar): OTShapeRef; cdecl; external OCCT_LIB;

function ot_import_step_with_diagnostics(path: PAnsiChar): OTImportResult; cdecl; external OCCT_LIB;

{ ================================================================
  I/O — Export
  ================================================================ }

function ot_export_stl(shape: OTShapeRef; path: PAnsiChar; deflection: Double): Boolean; cdecl; external OCCT_LIB;
function ot_export_step(shape: OTShapeRef; path: PAnsiChar): Boolean; cdecl; external OCCT_LIB;
function ot_export_iges(shape: OTShapeRef; path: PAnsiChar): Boolean; cdecl; external OCCT_LIB;
function ot_export_obj(shape: OTShapeRef; path: PAnsiChar; deflection: Double): Boolean; cdecl; external OCCT_LIB;
function ot_export_ply(shape: OTShapeRef; path: PAnsiChar; deflection: Double): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Healing & Analysis
  ================================================================ }

function ot_analyze_shape(shape: OTShapeRef; tolerance: Double): OTShapeAnalysis; cdecl; external OCCT_LIB;
function ot_heal_shape(shape: OTShapeRef): OTShapeRef; cdecl; external OCCT_LIB;
function ot_heal_shape_detailed(shape: OTShapeRef; tolerance: Double;
  fix_solid, fix_shell, fix_face, fix_wire: Boolean): OTShapeRef; cdecl; external OCCT_LIB;
function ot_sew_shape(shape: OTShapeRef; tolerance: Double): OTShapeRef; cdecl; external OCCT_LIB;
function ot_upgrade_shape(shape: OTShapeRef; tolerance: Double): OTShapeRef; cdecl; external OCCT_LIB;
function ot_make_solid(shape: OTShapeRef): OTShapeRef; cdecl; external OCCT_LIB;
function ot_shape_check_valid(shape: OTShapeRef): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Shape Info
  ================================================================ }

function ot_shape_volume(shape: OTShapeRef): Double; cdecl; external OCCT_LIB;
function ot_shape_surface_area(shape: OTShapeRef): Double; cdecl; external OCCT_LIB;
procedure ot_shape_bounding_box(shape: OTShapeRef;
  var xmin, ymin, zmin, xmax, ymax, zmax: Double); cdecl; external OCCT_LIB;

{ ================================================================
  Mesh Extraction
  ================================================================ }

function  ot_mesh_shape(shape: OTShapeRef; deflection: Double): OTMeshData; cdecl; external OCCT_LIB;
procedure ot_mesh_free(var mesh: OTMeshData); cdecl; external OCCT_LIB;

function ot_mesh_shape_separate(shape: OTShapeRef; deflection: Double;
  out out_vertex_count, out_triangle_count: Int32): Boolean; cdecl; external OCCT_LIB;
function ot_mesh_shape_fill(shape: OTShapeRef;
  out_vertices, out_normals: PSingle; out_indices: PInt32): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Lifecycle
  ================================================================ }

function  ot_viewer_create(width, height: Int32): OTViewerRef; cdecl; external OCCT_LIB;
procedure ot_viewer_destroy(viewer: OTViewerRef); cdecl; external OCCT_LIB;
function  ot_viewer_resize(viewer: OTViewerRef; width, height: Int32): Boolean; cdecl; external OCCT_LIB;
procedure ot_viewer_get_size(viewer: OTViewerRef; out out_width, out_height: Int32); cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Shape Display
  ================================================================ }

function  ot_viewer_add_shape(viewer: OTViewerRef; shape: OTShapeRef; mode: Int32): Int32; cdecl; external OCCT_LIB;
function  ot_viewer_remove_shape(viewer: OTViewerRef; display_id: Int32): Boolean; cdecl; external OCCT_LIB;
procedure ot_viewer_clear(viewer: OTViewerRef); cdecl; external OCCT_LIB;
function  ot_viewer_set_shape_color(viewer: OTViewerRef; display_id: Int32;
  r, g, b: Double): Boolean; cdecl; external OCCT_LIB;
function  ot_viewer_set_shape_transparency(viewer: OTViewerRef; display_id: Int32;
  t: Double): Boolean; cdecl; external OCCT_LIB;
function  ot_viewer_set_shape_display_mode(viewer: OTViewerRef; display_id: Int32;
  mode: Int32): Boolean; cdecl; external OCCT_LIB;
function  ot_viewer_set_shape_material(viewer: OTViewerRef; display_id: Int32;
  name: PAnsiChar): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Background
  ================================================================ }

procedure ot_viewer_set_background_color(viewer: OTViewerRef;
  r, g, b: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_background_gradient(viewer: OTViewerRef;
  r1, g1, b1, r2, g2, b2: Double); cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Camera Control
  ================================================================ }

procedure ot_viewer_set_projection(viewer: OTViewerRef; proj_type: Int32); cdecl; external OCCT_LIB;
procedure ot_viewer_set_fov(viewer: OTViewerRef; degrees: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_view_orientation(viewer: OTViewerRef; orient: Int32); cdecl; external OCCT_LIB;
procedure ot_viewer_fit_all(viewer: OTViewerRef); cdecl; external OCCT_LIB;
procedure ot_viewer_rotate(viewer: OTViewerRef; dx, dy: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_pan(viewer: OTViewerRef; dx, dy: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_zoom(viewer: OTViewerRef; factor: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_camera(viewer: OTViewerRef;
  eye_x, eye_y, eye_z, center_x, center_y, center_z,
  up_x, up_y, up_z: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_get_camera(viewer: OTViewerRef;
  out eye_x, eye_y, eye_z, center_x, center_y, center_z,
  up_x, up_y, up_z: Double); cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Lighting
  ================================================================ }

procedure ot_viewer_set_headlight(viewer: OTViewerRef; enabled: Boolean); cdecl; external OCCT_LIB;
procedure ot_viewer_add_directional_light(viewer: OTViewerRef;
  dir_x, dir_y, dir_z, r, g, b: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_ambient_light(viewer: OTViewerRef; intensity: Double); cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Rendering
  ================================================================ }

function ot_viewer_render(viewer: OTViewerRef;
  out out_width, out_height, out_size: Int32): PByte; cdecl; external OCCT_LIB;
function ot_viewer_render_to_buffer(viewer: OTViewerRef;
  buffer: PByte; buffer_size: Int32): Boolean; cdecl; external OCCT_LIB;
function ot_viewer_save_image(viewer: OTViewerRef; path: PAnsiChar): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Offscreen Viewer — Display Settings
  ================================================================ }

procedure ot_viewer_set_edge_display(viewer: OTViewerRef; enabled: Boolean); cdecl; external OCCT_LIB;
procedure ot_viewer_set_edge_color(viewer: OTViewerRef; r, g, b: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_deflection(viewer: OTViewerRef; deflection: Double); cdecl; external OCCT_LIB;
procedure ot_viewer_set_msaa(viewer: OTViewerRef; samples: Int32); cdecl; external OCCT_LIB;

{ ================================================================
  Standalone Camera — Lifecycle
  ================================================================ }

function  ot_camera_create: OTCameraRef; cdecl; external OCCT_LIB;
procedure ot_camera_destroy(camera: OTCameraRef); cdecl; external OCCT_LIB;

{ ================================================================
  Standalone Camera — Set/Get
  ================================================================ }

procedure ot_camera_set_eye(camera: OTCameraRef; x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_get_eye(camera: OTCameraRef; out x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_set_center(camera: OTCameraRef; x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_get_center(camera: OTCameraRef; out x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_set_up(camera: OTCameraRef; x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_get_up(camera: OTCameraRef; out x, y, z: Double); cdecl; external OCCT_LIB;
procedure ot_camera_set_projection_type(camera: OTCameraRef; proj_type: Int32); cdecl; external OCCT_LIB;
function  ot_camera_get_projection_type(camera: OTCameraRef): Int32; cdecl; external OCCT_LIB;
procedure ot_camera_set_fov(camera: OTCameraRef; degrees: Double); cdecl; external OCCT_LIB;
function  ot_camera_get_fov(camera: OTCameraRef): Double; cdecl; external OCCT_LIB;
procedure ot_camera_set_scale(camera: OTCameraRef; scale: Double); cdecl; external OCCT_LIB;
function  ot_camera_get_scale(camera: OTCameraRef): Double; cdecl; external OCCT_LIB;
procedure ot_camera_set_z_range(camera: OTCameraRef; z_near, z_far: Double); cdecl; external OCCT_LIB;
procedure ot_camera_set_aspect(camera: OTCameraRef; aspect: Double); cdecl; external OCCT_LIB;

{ ================================================================
  Standalone Camera — Matrices
  ================================================================ }

function ot_camera_get_projection_matrix(camera: OTCameraRef; out16: PDouble): Boolean; cdecl; external OCCT_LIB;
function ot_camera_get_view_matrix(camera: OTCameraRef; out16: PDouble): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Standalone Camera — Project / Unproject
  ================================================================ }

function ot_camera_project(camera: OTCameraRef;
  world_x, world_y, world_z: Double;
  out out_x, out_y, out_z: Double): Boolean; cdecl; external OCCT_LIB;
function ot_camera_unproject(camera: OTCameraRef;
  ndc_x, ndc_y, ndc_z: Double;
  out out_x, out_y, out_z: Double): Boolean; cdecl; external OCCT_LIB;
function ot_camera_fit_bbox(camera: OTCameraRef;
  xmin, ymin, zmin, xmax, ymax, zmax: Double): Boolean; cdecl; external OCCT_LIB;

{ ================================================================
  Edge Mesh Extraction
  ================================================================ }

function  ot_edge_mesh_shape(shape: OTShapeRef; deflection: Double): OTEdgeMeshData; cdecl; external OCCT_LIB;
procedure ot_edge_mesh_free(var mesh: OTEdgeMeshData); cdecl; external OCCT_LIB;

{ ================================================================
  Display Drawer
  ================================================================ }

function  ot_drawer_create: OTDrawerRef; cdecl; external OCCT_LIB;
procedure ot_drawer_destroy(drawer: OTDrawerRef); cdecl; external OCCT_LIB;

procedure ot_drawer_set_deviation_coefficient(drawer: OTDrawerRef; coeff: Double); cdecl; external OCCT_LIB;
function  ot_drawer_get_deviation_coefficient(drawer: OTDrawerRef): Double; cdecl; external OCCT_LIB;
procedure ot_drawer_set_deviation_angle(drawer: OTDrawerRef; radians: Double); cdecl; external OCCT_LIB;
function  ot_drawer_get_deviation_angle(drawer: OTDrawerRef): Double; cdecl; external OCCT_LIB;
procedure ot_drawer_set_max_deviation(drawer: OTDrawerRef; deviation: Double); cdecl; external OCCT_LIB;
function  ot_drawer_get_max_deviation(drawer: OTDrawerRef): Double; cdecl; external OCCT_LIB;
procedure ot_drawer_set_face_boundary_draw(drawer: OTDrawerRef; enabled: Boolean); cdecl; external OCCT_LIB;
function  ot_drawer_get_face_boundary_draw(drawer: OTDrawerRef): Boolean; cdecl; external OCCT_LIB;

function ot_mesh_shape_with_drawer(shape: OTShapeRef; drawer: OTDrawerRef): OTMeshData; cdecl; external OCCT_LIB;
function ot_edge_mesh_shape_with_drawer(shape: OTShapeRef; drawer: OTDrawerRef): OTEdgeMeshData; cdecl; external OCCT_LIB;

{ ================================================================
  Helper functions
  ================================================================ }

{ Get the last error as a Pascal string. Returns empty string if no error. }
function OTGetLastError: string;

{ Shape type name from type code. }
function OTShapeTypeName(type_code: Int32): string;

{
  Render the viewer contents into a TBitmap (top-to-bottom RGBA).
  Sets bitmap dimensions and pixel format, then calls ot_viewer_render_to_buffer.
  Returns True on success.
}
function OTViewerRenderToBitmap(viewer: OTViewerRef; ABitmap: TBitmap): Boolean;

implementation

function OTGetLastError: string;
var
  p: PAnsiChar;
begin
  p := occt_templot_last_error;
  if p = nil then
    Result := ''
  else
    Result := string(p);
end;

function OTShapeTypeName(type_code: Int32): string;
begin
  case type_code of
    0: Result := 'Compound';
    1: Result := 'CompSolid';
    2: Result := 'Solid';
    3: Result := 'Shell';
    4: Result := 'Face';
    5: Result := 'Wire';
    6: Result := 'Edge';
    7: Result := 'Vertex';
    8: Result := 'Shape';
  else
    Result := 'Unknown';
  end;
end;

function OTViewerRenderToBitmap(viewer: OTViewerRef; ABitmap: TBitmap): Boolean;
var
  w, h: Int32;
  bufSize: Int32;
  buf: PByte;
  rawImg: TLazIntfImage;
  x, y: Integer;
  px: PByte;
  col: TFPColor;
begin
  Result := False;
  if (viewer = nil) or (ABitmap = nil) then
    Exit;

  ot_viewer_get_size(viewer, w, h);
  if (w <= 0) or (h <= 0) then
    Exit;

  bufSize := w * h * 4;
  GetMem(buf, bufSize);
  try
    if not ot_viewer_render_to_buffer(viewer, buf, bufSize) then
      Exit;

    ABitmap.SetSize(w, h);
    ABitmap.PixelFormat := pf32bit;

    rawImg := ABitmap.CreateIntfImage;
    try
      for y := 0 to h - 1 do
      begin
        for x := 0 to w - 1 do
        begin
          px := buf + (y * w + x) * 4;
          col.Red   := px[0] or (px[0] shl 8);
          col.Green := px[1] or (px[1] shl 8);
          col.Blue  := px[2] or (px[2] shl 8);
          col.Alpha := px[3] or (px[3] shl 8);
          rawImg.Colors[x, y] := col;
        end;
      end;
      ABitmap.LoadFromIntfImage(rawImg);
    finally
      rawImg.Free;
    end;

    Result := True;
  finally
    FreeMem(buf);
  end;
end;

end.
