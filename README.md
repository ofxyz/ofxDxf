# ofxDxf

Load, display, and save 2D DXF geometry for openFrameworks plotting and conversion workflows.

Built on [dxflib](libs/dxflib). Geometry is stored as **`ofPath`** commands where possible (lines, arcs, circles, bulge arcs stay exact). Splines and ellipses keep DXF metadata for lossless re-export; display tessellation follows **`ofGetStyle()`** and view zoom.

## Examples

| Example | Description |
|---------|-------------|
| `example-simple` | Minimal DXF viewer (drag & drop) |
| `example-dxftosvg` | DXF ↔ SVG converter with layer UI and export |

## Basic usage

```cpp
#include "ofxDxf.h"

DxfDocument doc = ofxDxf::load("drawing.dxf");
if (doc.empty()) return;

for (auto& layer : doc.layers) {
    for (auto& entity : layer.entities) {
        entity.draw(zoom);           // preview — zoom-adaptive tessellation
        // entity.exportPath();      // plot / SVG export — quality-first
    }
}

ofxDxf::save(doc, "out.dxf");       // R2000 ASCII DXF
doc.flipY();                         // DXF Y-up ↔ screen Y-down
```

## Supported on load

| Feature | Status | Notes |
|---------|--------|-------|
| **LINE** | ✅ | Native `ofPath` |
| **ARC** | ✅ | Exact arc commands + `DxfArcInfo` |
| **CIRCLE** | ✅ | Exact arc / circle + `DxfArcInfo` |
| **LWPOLYLINE** | ✅ | Lines + **bulge** arcs |
| **POLYLINE** | ✅ | Legacy polyline + vertices |
| **ELLIPSE** | ✅ | Metadata preserved; display via tessellation |
| **SPLINE** | ✅ | NURBS metadata preserved; display via tessellation |
| **POINT** | ✅ | Loaded; not drawn by default |
| **INSERT / BLOCK** | ✅ | Blocks collected; INSERT expands to model space (scale, rotation, arrays) |
| **Layers** | ✅ | LAYER table + entity layer attribute |
| **Colors** | ✅ | ACI palette + 24-bit `color24` |
| **Empty layers** | ✅ | Layers from table appear even with 0 entities |

### Export / save (`ofxDxf::save`)

| Entity | Write format |
|--------|----------------|
| Line, arc, circle | Native DXF entities |
| Ellipse, spline | Native DXF when metadata present |
| Other / fallback | **LWPOLYLINE** at export quality (`exportPath()`) |

INSERT/BLOCK structure is **not** recreated on save — output is flat geometry.

## Not supported (ignored)

These DXF entities are parsed by dxflib but **not** converted to `DxfEntity` geometry:

| Type | Common in |
|------|-----------|
| **TEXT / MTEXT** | Labels, part numbers |
| **DIMENSION** (+ variants) | AutoCAD dimensions |
| **HATCH** | Filled regions, sheet outlines |
| **XLINE / RAY** | Construction geometry |
| **3DFACE / SOLID / TRACE** | Filled triangles / quads |
| **ATTRIB** | Block attribute text |
| **IMAGE** | Raster references |
| **LEADER** | Annotation leaders |
| **Paper space layouts** | Layout tabs (model space only) |
| **Linetypes / lineweights** | Stored in DXF; not applied on draw |
| **Block forward references** | INSERT before block definition may fail |

If your file looks incomplete, check the console for `INSERT references unknown block` warnings.

## Geometry API

| Method | Use |
|--------|-----|
| `entity.path` | Exact stored geometry |
| `entity.exportPath()` | **Export / plot** — exact paths; adaptive tessellation for spline/ellipse only |
| `entity.resolvedPath()` | **Display** — uses `ofGetStyle()` segment count |
| `entity.getOutline()` | Tessellated polylines |
| `entity.draw(zoom)` | Preview with zoom-adaptive arcs |

Tessellation resolution is **not** stored on the document — it comes from render context (`ofGetStyle()`, zoom), except export which uses world-space quality automatically.

## Quick wins (roadmap)

Low-effort improvements that would help most laser/CNC/SVG workflows:

1. **Skip logging** — count and log ignored entity types per file (`HATCH`, `TEXT`, …) so users know why geometry is missing. *(~1 hour)*
2. **SOLID / TRACE / 3DFACE** — emit as closed polylines (≤4 vertices). Many flat parts use these for triangles. *(~2 hours)*
3. **HATCH boundaries** — use dxflib hatch loops as polylines (ignore fill pattern). Common on sheet-metal and nesting DXFs. *(~4 hours)*
4. **Block forward references** — second pass or deferred INSERT expansion when block is defined later in the file. *(~2 hours)*
5. **DIMENSION decomposition** — explode dimension primitives to lines/arcs/text (or lines only). *(~1 day)*
6. **TEXT as placeholder** — bounding box rectangle or `ofTrueTypeFont` outline for labels (optional, heavier). *(~1–2 days)*

Higher effort: paper space, linetype pattern rendering, full HATCH fill, write INSERT/BLOCK on save.

## Dependencies

- openFrameworks (`ofPath`, `ofPolyline`, …)
- dxflib (included under `libs/dxflib`)

Optional in examples: `ofxSvg`, `ofxImGui`, `ofxImGuiStyle`.

## License

See addon and dxflib license files.
