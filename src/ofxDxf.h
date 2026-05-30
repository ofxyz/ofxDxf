#pragma once

#include "ofMain.h"
#include <string>
#include <vector>
#include <optional>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// DxfArcInfo — exact circular arc / circle parameters (DXF ARC / CIRCLE).
// ─────────────────────────────────────────────────────────────────────────────
struct DxfArcInfo {
    glm::vec2 center;
    float     radius     = 0;
    float     startAngle = 0;   // degrees, CCW from +X
    float     endAngle   = 0;   // degrees, CCW from +X
    bool      isCCW      = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfEllipseInfo — exact DXF ELLIPSE parameters (may be rotated).
// ─────────────────────────────────────────────────────────────────────────────
struct DxfEllipseInfo {
    glm::vec2 center;
    glm::vec2 majorAxis;   ///< major axis vector (mx, my) from DXF
    float     ratio = 1.f; ///< minor / major
    double    startParam = 0;
    double    endParam   = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfSplineInfo — exact DXF SPLINE parameters for lossless export.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfSplineInfo {
    int                  degree = 3;
    bool                 closed = false;
    std::vector<double>  knots;
    std::vector<glm::vec2> controlPoints;
    std::vector<glm::vec2> fitPoints;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfEntity — one shape from a DXF file.
//   Primary geometry lives in `path` (ofPath commands — arcs/circles stay exact).
//   Optional metadata preserves types that need extra DXF fields on export.
//   Tessellation happens only for display (getOutline / resolvedPath) or when
//   exportPath must approximate splines/ellipses that have no exact ofPath yet.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfEntity {
    enum class Type {
        Line,
        Arc,
        Circle,
        Polyline,
        Spline,
        Ellipse,
        Point
    };

    Type        type  = Type::Line;
    std::string layer = "0";
    ofColor     color {0, 0, 0};

    /// Exact vector geometry (lines, arcs, circles, bulge arcs as arc commands).
    ofPath      path;

    std::optional<DxfArcInfo>     arcInfo;
    std::optional<DxfEllipseInfo> ellipseInfo;
    std::optional<DxfSplineInfo>  splineInfo;

    /// Tessellate to polylines for display. `segments <= 0` uses ofGetStyle().
    std::vector<ofPolyline> getOutline(int segments = -1) const;

    /// Display path — may tessellate splines/ellipses. `segments <= 0` uses ofGetStyle().
    ofPath resolvedPath(int segments = -1) const;

    /// Export path — exact ofPath commands when possible; splines/ellipses are
    /// tessellated at adaptive world-space quality (not the display resolution).
    /// Pass `segments > 0` to override the automatic count.
    ofPath exportPath(int segments = -1) const;

    /// Draw for display. Base segment count comes from ofGetStyle(); `viewZoom`
    /// adapts arc/circle density (~1.5 px/segment). Pass `segments > 0` to override.
    void draw(float viewZoom = 1.f, int segments = -1) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfLayer
// ─────────────────────────────────────────────────────────────────────────────
struct DxfLayer {
    std::string              name;
    ofColor                  color {0, 0, 0};
    std::vector<DxfEntity>   entities;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfDocument
// ─────────────────────────────────────────────────────────────────────────────
struct DxfDocument {
    std::vector<DxfLayer>    layers;
    ofRectangle              bounds;
    std::vector<std::string> errors;

    bool empty() const { return layers.empty(); }

    std::vector<const DxfEntity*> getAllEntities() const;
    std::vector<const DxfEntity*> getEntitiesOnLayer(const std::string& layerName) const;

    /// All entity paths (copies of exact ofPath storage).
    std::vector<ofPath> getAllPaths() const;

    std::vector<ofPath> getPathsOnLayer(const std::string& layerName) const;

    /// Tessellated outlines for display. `segments <= 0` uses ofGetStyle().
    std::vector<ofPolyline> getAllPolylines(int segments = -1) const;
    std::vector<ofPolyline> getPolylinesOnLayer(const std::string& layerName,
                                                int segments = -1) const;

    std::vector<std::string> getLayerNames() const;

    void flipY();
};

// ─────────────────────────────────────────────────────────────────────────────
// ofxDxf
// ─────────────────────────────────────────────────────────────────────────────
class ofxDxf {
public:
    static ofColor aciToColor(int aci);
    static ofColor paletteColor(int index);

    /// Load a DXF file. Geometry stays exact in ofPath / metadata; tessellation
    /// is chosen at draw/export time from ofGetStyle() and view zoom.
    static DxfDocument load(const std::string& path);
    static DxfDocument loadBuffer(const ofBuffer& buffer);

    /// Save as ASCII DXF (R2000). Writes native LINE / ARC / CIRCLE / ELLIPSE /
    /// SPLINE when metadata is available; otherwise falls back to LWPOLYLINE.
    static bool save(const DxfDocument& doc, const std::string& path);
};
