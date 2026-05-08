#pragma once

#include "ofMain.h"
#include <string>
#include <vector>
#include <optional>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// DxfArcInfo
//   Raw arc parameters preserved alongside the discretized polyline.
//   Lets you generate true G2/G3 arc moves rather than line segments.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfArcInfo {
    glm::vec2 center;
    float     radius     = 0;
    float     startAngle = 0;   // degrees, CCW from +X
    float     endAngle   = 0;   // degrees, CCW from +X
    bool      isCCW      = true; // true = counter-clockwise
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfEntity
//   A single drawn shape from a DXF file.
//   polyline is always populated (discretized to line segments).
//   arcInfo is only set for ARC and CIRCLE entities.
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

    /// Always populated: the entity as a discretized polyline.
    ofPolyline  polyline;

    /// Only set for Arc and Circle — lets you preserve the exact geometry.
    std::optional<DxfArcInfo> arcInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfLayer
// ─────────────────────────────────────────────────────────────────────────────
struct DxfLayer {
    std::string              name;
    std::vector<DxfEntity>   entities;
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfDocument
//   Result of parsing a DXF file.
// ─────────────────────────────────────────────────────────────────────────────
struct DxfDocument {
    std::vector<DxfLayer>    layers;
    ofRectangle              bounds;
    std::vector<std::string> errors;

    bool empty() const { return layers.empty(); }

    /// All entities across all layers, in order.
    std::vector<const DxfEntity*> getAllEntities() const;

    /// Entities on a specific layer (returns empty vector if layer not found).
    std::vector<const DxfEntity*> getEntitiesOnLayer(const std::string& layerName) const;

    /// All entity polylines as a flat list (convenient for rendering/sorting).
    std::vector<ofPolyline> getAllPolylines() const;

    /// Polylines from one layer.
    std::vector<ofPolyline> getPolylinesOnLayer(const std::string& layerName) const;

    /// Layer names present in the document.
    std::vector<std::string> getLayerNames() const;

    /// Flip all vertices around the horizontal centre of the bounding box,
    /// converting between DXF (Y-up) and SVG/screen (Y-down) coordinate systems.
    /// Also flips arcInfo angles so arc direction remains correct.
    /// Recomputes bounds after the transform.
    void flipY();
};

// ─────────────────────────────────────────────────────────────────────────────
// ofxDxf
//   Loads a DXF file and returns a DxfDocument.
//
//   Usage:
//       DxfDocument doc = ofxDxf::load("drawing.dxf");
//       for (auto& layer : doc.layers) { ... }
//
//   arcSegments controls how many line segments are used to discretize
//   arcs, circles, ellipses, and splines.
// ─────────────────────────────────────────────────────────────────────────────
class ofxDxf {
public:
    /// Load a DXF file from disk. Path is resolved via ofToDataPath.
    static DxfDocument load(const std::string& path, int arcSegments = 64);

    /// Load from an ofBuffer (e.g. a file already read into memory).
    static DxfDocument loadBuffer(const ofBuffer& buffer, int arcSegments = 64);

    /// Save a DxfDocument to disk as an ASCII DXF (R2000 format).
    /// Each polyline is written as a LWPOLYLINE entity on its source layer.
    /// Returns true on success.
    static bool save(const DxfDocument& doc, const std::string& path);
};
