# ofxDxf

DXF file loader for [openFrameworks](https://openframeworks.cc). Parses geometry into `ofPolyline` entities grouped by layer, ready for rendering, plotting, or export.

Wraps **[dxflib](https://qcad.org/en/dxflib-downloads)** by RibbonSoft / QCAD (GPLv2+), vendored under `libs/dxflib/src/`.

---

## Supported entities

| DXF entity | Output |
|---|---|
| `LINE` | 2-point polyline |
| `ARC` | Discretized polyline + raw `DxfArcInfo` (center, radius, angles) |
| `CIRCLE` | Closed polyline + raw `DxfArcInfo` |
| `LWPOLYLINE` / `POLYLINE` | Polyline with **bulge arcs** correctly converted to curve segments |
| `ELLIPSE` | Discretized polyline |
| `SPLINE` | De Boor evaluated B-spline (falls back to fit-points if present) |
| `POINT` | Single-vertex polyline |

Layers are preserved. Each entity carries its source layer name.

---

## Basic usage

```cpp
#include "ofxDxf.h"

DxfDocument doc = ofxDxf::load("drawing.dxf");

for (auto& layer : doc.layers) {
    ofSetColor(255);
    for (auto& entity : layer.entities) {
        entity.polyline.draw();
    }
}
```

### Coordinate system

DXF files use a Y-up coordinate system. Call `flipY()` before rendering or exporting to SVG/screen (Y-down):

```cpp
DxfDocument doc = ofxDxf::load("drawing.dxf");
doc.flipY();  // converts to Y-down; updates bounds and arc angles
```

### Arc metadata

For toolpath generation (G2/G3 arcs in G-code), arcs and circles expose their raw geometry:

```cpp
for (auto& entity : layer.entities) {
    if (entity.arcInfo) {
        auto& ai = *entity.arcInfo;
        // ai.center, ai.radius, ai.startAngle, ai.endAngle (degrees), ai.isCCW
    }
}
```

### Convenience accessors

```cpp
doc.getAllEntities();                    // flat list across all layers
doc.getEntitiesOnLayer("cutlines");     // entities on a named layer
doc.getAllPolylines();                   // all discretized polylines
doc.getLayerNames();                    // ["0", "cutlines", ...]
doc.bounds;                             // ofRectangle covering all geometry
```

---

## Examples

| Example | Description |
|---|---|
| `example-simple` | Drag-and-drop viewer with per-layer colour and visibility |
| `example-dxftosvg` | Load a DXF, preview with ofxImGui controls, export to SVG via ofxSvg |

---

## Dependencies

- **ofxDxf** itself has no OF addon dependencies.
- `example-dxftosvg` requires `ofxImGui` and `ofxSvg`.

---

## dxflib license

dxflib is © RibbonSoft GmbH and licensed under **GPLv2 or later**. Source files are in `libs/dxflib/src/`. See [qcad.org](https://qcad.org/en/dxflib-downloads) for the standalone release.
