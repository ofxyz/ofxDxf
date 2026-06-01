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
// DxfViewContext — optional viewport info for zoom-adaptive display tessellation.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfViewContext {
    glm::vec2 center     {0, 0}; ///< world-space view center
    float     viewportW  = 0.f;  ///< viewport size in screen pixels
    float     viewportH  = 0.f;

    bool isValid() const { return viewportW > 1.f && viewportH > 1.f; }
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
    /// Ellipse/spline curves keep `path` empty; tessellation uses metadata at draw time.
    ofPath      path;

    std::optional<DxfArcInfo>     arcInfo;
    std::optional<DxfEllipseInfo> ellipseInfo;
    std::optional<DxfSplineInfo>  splineInfo;

    /// Tessellate to polylines for display. Arcs/circles tessellate at arc center
    /// (same quality rules as draw()). `segments <= 0` uses ofGetStyle().
    std::vector<ofPolyline> getOutline(int segments = -1) const;

    /// Display path — may tessellate splines/ellipses. Circular geometry uses arcInfo
    /// when available. `segments <= 0` uses ofGetStyle().
    ofPath resolvedPath(int segments = -1) const;

    /// Export path — exact ofPath commands when possible. When arcInfo is present it
    /// is the source of truth (path is rebuilt from it). Splines/ellipses tessellate
    /// at adaptive world-space quality.
    ofPath exportPath(int segments = -1) const;

    /// Draw for preview. Uses stored arcInfo (native CIRCLE/ARC) or local tessellation
    /// for ofPath arc commands (bulge). Faceted polylines draw as-is unless you call
    /// ofxDxf::promoteCurves() first.
    void draw(float viewZoom = 1.f, int segments = -1,
              const DxfViewContext* view = nullptr) const;
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

    /// Resolved path for every entity (tessellates ellipses/splines at default quality).
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
// DxfCurvePromotionSettings — optional post-load curve recovery (never run
// automatically). Exact bulge-arc promotion is safe; faceted-circle fitting is
// heuristic and off by default so hexagons etc. are never changed unless you
// opt in with strict thresholds.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfCurvePromotionSettings {
    /// Closed loop of ofPath arc commands that span 360° (e.g. two bulge semicircles).
    bool  promoteBulgeArcCircles       = true;
    /// moveTo + one arc command, no chord lines (exact DXF bulge arc).
    bool  promoteBulgeArcs             = true;
    /// Fit closed chord polylines to CIRCLE — heuristic, off by default.
    bool  promoteFacetedCircles        = false;
    /// Fit open chord polylines to ARC — heuristic, off by default.
    bool  promoteFacetedArcs           = false;
    int   facetedCircleMinVertices     = 16;
    int   facetedArcMinVertices        = 8;
    float facetedCircleMaxRelativeError = 0.01f;
    float facetedCircleMaxEdgeVariance  = 0.05f;
};

struct DxfCurvePromotionResult {
    int circlesPromoted  = 0;
    int arcsPromoted     = 0;
    int entitiesExamined = 0;
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
    /// No heuristic curve promotion is performed — call promoteCurves() if needed.
    static DxfDocument load(const std::string& path);
    static DxfDocument loadBuffer(const ofBuffer& buffer);

    /// Try to replace polylines with native CIRCLE/ARC metadata. Never mutates
    /// entities that already have arcInfo. Returns whether this entity changed.
    static bool promoteEntityCurves(DxfEntity& entity,
                                    const DxfCurvePromotionSettings& settings = {});

    /// Run promoteEntityCurves on every entity in the document (in place).
    static DxfCurvePromotionResult promoteCurves(DxfDocument& doc,
                                                 const DxfCurvePromotionSettings& settings = {});

    /// Save as ASCII DXF (R2000). Writes native LINE / ARC / CIRCLE / ELLIPSE /
    /// SPLINE when metadata is available; otherwise falls back to LWPOLYLINE.
    static bool save(const DxfDocument& doc, const std::string& path);
};
