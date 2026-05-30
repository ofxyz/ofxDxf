# ofxDxf

Load, display, and save 2D DXF geometry for openFrameworks plotting and conversion workflows.

Built on [dxflib](libs/dxflib). Geometry is stored as **`ofPath`** commands where possible (lines, arcs, circles, bulge arcs stay exact). Native **CIRCLE** / **ARC** entities also store **`DxfArcInfo`** for lossless export. Splines and ellipses keep DXF metadata for re-export; display tessellation follows **`ofGetStyle()`** and view zoom.

**Load is lossless and non-destructive** — no heuristic curve fitting runs automatically. Optional **`promoteCurves()`** can recover bulge arcs or (if you opt in) faceted circles later.

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

for (auto& err : doc.errors)   // includes skip summary when present
    ofLogWarning("ofxDxf") << err;

DxfViewContext view;
view.center    = viewCenter;      // world-space pan
view.viewportW = ofGetWidth();
view.viewportH = ofGetHeight();

for (auto& layer : doc.layers) {
    for (auto& entity : layer.entities) {
        entity.draw(zoom, -1, &view);  // preview — local arc tessellation
        // entity.exportPath();        // plot / SVG — exact where possible
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
| **INSERT / BLOCK** | ✅ | Blocks collected; INSERT expanded (scale, rotation, arrays); **block entity layers preserved** |
| **Deferred INSERT** | ✅ | INSERT before block definition — resolved after load |
| **HATCH** | ✅ | **Boundary loops only** (lines, arcs, bulge polylines; no fill pattern) |
| **SOLID / TRACE / 3DFACE** | ✅ | Closed polylines (≤4 corners) |
| **Layers** | ✅ | LAYER table + entity layer attribute |
| **Colors** | ✅ | ACI palette + 24-bit `color24` |
| **Empty layers** | ✅ | Layers from table appear even with 0 entities |
| **Skip diagnostics** | ✅ | Counts ignored types; logged + appended to `doc.errors` |

### Export / save (`ofxDxf::save`)

| Entity | Write format |
|--------|----------------|
| Line, arc, circle | Native DXF entities (from `arcInfo` when present) |
| Ellipse, spline | Native DXF when metadata present |
| Hatch / solid / trace | **LWPOLYLINE** (boundaries only) |
| Other / fallback | **LWPOLYLINE** at export quality (`exportPath()`) |

INSERT/BLOCK structure is **not** recreated on save — output is flat geometry.

## Preview rendering

Circles and arcs tessellate at **arc center in local coordinates** (avoids jitter on large sheet coordinates). Rules:

| Source | Preview path |
|--------|----------------|
| Native **CIRCLE** / **ARC** (`arcInfo`) | Local tessellation from `DxfArcInfo` |
| **ELLIPSE** (`ellipseInfo`) | Local tessellation at ellipse center (zoom-adaptive) |
| **SPLINE** (`splineInfo`) | Local NURBS tessellation at spline centroid (zoom-adaptive) |
| **Bulge** polylines (`ofPath` arc commands) | Each arc drawn locally; elliptical arcs when `radiusX ≠ radiusY` |
| Chord-faceted polylines (many `lineTo`) | Drawn as stored — no silent circle fitting |

Pass **`DxfViewContext`** to `entity.draw()` so full circles only stroke the viewport-visible arc span at high zoom.

## Optional curve promotion

Heuristic fitting **never runs on load**. Call explicitly when you want polylines replaced by native **CIRCLE** / **ARC** metadata:

```cpp
DxfCurvePromotionSettings settings;
settings.promoteBulgeArcCircles = true;   // exact: arc commands spanning 360°
settings.promoteBulgeArcs       = true;   // exact: single bulge arc
settings.promoteFacetedCircles  = false;  // off by default — avoids hexagon → circle

auto result = ofxDxf::promoteCurves(doc, settings);
// result.circlesPromoted, result.arcsPromoted, result.entitiesExamined
```

| Setting | Default | Meaning |
|---------|---------|---------|
| `promoteBulgeArcCircles` | `true` | Closed loop of exact arc commands → `CIRCLE` |
| `promoteBulgeArcs` | `true` | moveTo + one arc (bulge) → `ARC` |
| `promoteFacetedCircles` | **`false`** | Chord-fit closed polyline → `CIRCLE` (heuristic) |
| `facetedCircleMinVertices` | `16` | Minimum facet count (hexagon = 6, never matches) |
| `facetedCircleMaxRelativeError` | `0.01` | Max radius fit error |
| `facetedCircleMaxEdgeVariance` | `0.05` | Rejects uneven edge lengths |

Promotion preserves **layer**, **color**, and other entity metadata. Entities that already have `arcInfo` are never changed.

### INSERT transforms

- **Uniform** block scale: `arcInfo` and `path` stay in sync after INSERT (`CIRCLE` / `ARC`).
- **Non-uniform** scale on circles/arcs: converted to **`DxfEllipseInfo`** (`Type::Ellipse`) so export writes native DXF **ELLIPSE**, not a false circle or tessellated polyline fallback.

## Not supported (counted & logged)

These are parsed by dxflib but **not** converted to geometry. Counts appear in the console and in `doc.errors` (e.g. `Skipped/unresolved: TEXT=12 DIMENSION=4`):

| Type | Common in |
|------|-----------|
| **TEXT / MTEXT** | Labels, part numbers |
| **DIMENSION** (+ variants) | AutoCAD dimensions |
| **HATCH fill pattern** | Pattern fill (boundaries **are** loaded) |
| **XLINE / RAY** | Construction geometry |
| **ATTRIB** | Block attribute text |
| **IMAGE** | Raster references |
| **LEADER** | Annotation leaders |
| **Paper space layouts** | Layout tabs (model space only) |
| **Linetypes / lineweights** | Stored in DXF; not applied on draw |
| **Unknown INSERT** | Block name not found after deferred retry |

If geometry is missing, check the console **`ofxDxf: Load summary`** line or `doc.errors`.

## Geometry API

| Method | Use |
|--------|-----|
| `entity.path` | Exact stored geometry |
| `entity.arcInfo` | Native circle/arc parameters (export source of truth) |
| `entity.exportPath()` | **Export / plot** — exact paths; `arcInfo` rebuilt into `ofPath` |
| `entity.resolvedPath()` | **Display** — uses `ofGetStyle()` segment count |
| `entity.getOutline()` | Tessellated polylines (local arc tessellation when `arcInfo` present) |
| `entity.draw(zoom, segs, &view)` | Preview with zoom-adaptive local arcs |
| `ofxDxf::promoteCurves(doc, settings)` | Optional in-place CIRCLE/ARC recovery |

Tessellation resolution is **not** stored on the document — it comes from render context (`ofGetStyle()`, zoom), except export which uses world-space quality automatically for splines/ellipses.

## Roadmap (next improvements)

| Item | Effort | Notes |
|------|--------|-------|
| **Open chord-faceted arc fitting** | medium | Optional promotion (like faceted circles) |
| **Linetype on draw** | medium | Dashed layer styles |
| **DIMENSION → lines/arcs** | ~1 day | Explode dimension primitives |
| **TEXT outline / bbox** | ~1–2 days | Optional labels for reference |
| **Paper space** | medium | Layout tab support |
| **INSERT/BLOCK on save** | medium | Preserve block structure round-trip |
| **Full HATCH fill** | high | Pattern rendering, not just boundaries |

## Dependencies

- openFrameworks (`ofPath`, `ofPolyline`, …)
- dxflib (included under `libs/dxflib`)

Optional in examples: `ofxSvg`, `ofxImGui`, `ofxImGuiStyle`.

## License

See addon and dxflib license files.
