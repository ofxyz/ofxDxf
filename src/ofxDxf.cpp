#include "ofxDxf.h"

#include "dl_dxf.h"
#include "dl_creationadapter.h"
#include "dl_entities.h"

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float DEG2RAD = float(M_PI) / 180.f;

/// Convert a bulge value (DXF LWPOLYLINE arc notation) and two consecutive
/// vertices into an arc polyline and append it to `out`.
///
/// bulge = tan(θ/4), where θ is the included angle of the arc.
/// Positive bulge = CCW arc, negative = CW.
static void appendBulgeArc(
    const glm::vec2& p1,
    const glm::vec2& p2,
    double bulge,
    int arcSegments,
    ofPolyline& out)
{
    double theta = 4.0 * std::abs(std::atan(bulge));           // total arc angle (unsigned)
    double d      = glm::distance(p1, p2);
    if (d < 1e-10) return;

    double r      = d / (2.0 * std::sin(theta * 0.5));

    // Midpoint of chord
    glm::vec2 mid = (p1 + p2) * 0.5f;

    // Perpendicular direction (rotate chord by 90°)
    glm::vec2 chord = p2 - p1;
    glm::vec2 perp  = glm::vec2(-chord.y, chord.x);
    perp = glm::normalize(perp);

    // Distance from midpoint to centre
    double d_to_center = std::sqrt(r * r - (d * 0.5) * (d * 0.5));

    // Centre is on the left (CCW) or right (CW) side of the chord
    float sign = (bulge > 0.0 ? 1.0 : -1.0);
    glm::vec2 center = mid + perp * sign * float(d_to_center);

    float startAngle = std::atan2(p1.y - center.y, p1.x - center.x);
    float endAngle   = std::atan2(p2.y - center.y, p2.x - center.x);

    // Ensure we travel in the correct direction
    if (sign > 0) {
        // CCW: end must be > start (modulo 2π)
        if (endAngle <= startAngle) endAngle += float(TWO_PI);
    }
    else {
        // CW: end must be < start
        if (endAngle >= startAngle) endAngle -= float(TWO_PI);
    }

    int segs = std::max(4, int(std::ceil(theta / TWO_PI * arcSegments)));
    float discretAngle = (endAngle - startAngle) / float(segs);

    for (int i = 1; i <= segs; ++i) {
        float ang = startAngle + discretAngle * float(i);
        out.addVertex(glm::vec3(
            center.x + float(r) * std::cos(ang),
            center.y + float(r) * std::sin(ang),
            0));
    }
}

/// Evaluate a B-spline at parameter t using De Boor's algorithm.
/// Returns a point on the curve.
static glm::vec2 deBoor(
    int                           k,        // degree
    int                           i,        // knot span index
    const std::vector<double>&    knots,
    const std::vector<glm::vec2>& ctrl,
    double                        t)
{
    std::vector<glm::vec2> d(k + 1);
    for (int j = 0; j <= k; ++j) {
        int idx = i - k + j;
        if (idx >= 0 && idx < (int)ctrl.size()) d[j] = ctrl[idx];
    }
    for (int r = 1; r <= k; ++r) {
        for (int j = k; j >= r; --j) {
            int knotIdx = i - k + j;
            double denom = knots[knotIdx + k - r + 1] - knots[knotIdx];
            if (std::abs(denom) < 1e-12) continue;
            double alpha = (t - knots[knotIdx]) / denom;
            d[j] = glm::vec2(
                (1.0 - alpha) * d[j - 1].x + alpha * d[j].x,
                (1.0 - alpha) * d[j - 1].y + alpha * d[j].y);
        }
    }
    return d[k];
}

// ─────────────────────────────────────────────────────────────────────────────
// OfxDxfAdapter — dxflib callback receiver
// ─────────────────────────────────────────────────────────────────────────────
class OfxDxfAdapter : public DL_CreationAdapter {
public:
    explicit OfxDxfAdapter(int arcSegments)
        : m_arcSegments(arcSegments) {}

    // ── Layer tracking via group code 8 ──────────────────────────────────────
    void processCodeValuePair(unsigned int code, const std::string& value) override {
        DL_CreationAdapter::processCodeValuePair(code, value);
        if (code == 8) m_currentLayer = value;
    }

    void addLayer(const DL_LayerData& data) override {
        ensureLayer(data.name);
    }

    // ── Entities ──────────────────────────────────────────────────────────────

    void addLine(const DL_LineData& d) override {
        DxfEntity e;
        e.type  = DxfEntity::Type::Line;
        e.layer = m_currentLayer;
        e.polyline.addVertex(glm::vec3(d.x1, d.y1, 0));
        e.polyline.addVertex(glm::vec3(d.x2, d.y2, 0));
        pushEntity(std::move(e));
    }

    void addPoint(const DL_PointData& d) override {
        DxfEntity e;
        e.type  = DxfEntity::Type::Point;
        e.layer = m_currentLayer;
        e.polyline.addVertex(glm::vec3(d.x, d.y, 0));
        pushEntity(std::move(e));
    }

    void addArc(const DL_ArcData& d) override {
        DxfEntity e;
        e.type  = DxfEntity::Type::Arc;
        e.layer = m_currentLayer;

        DxfArcInfo ai;
        ai.center     = glm::vec2(d.cx, d.cy);
        ai.radius     = float(d.radius);
        ai.startAngle = float(d.angle1);
        ai.endAngle   = float(d.angle2);
        ai.isCCW      = true;
        e.arcInfo     = ai;

        float startRad = float(d.angle1) * DEG2RAD;
        float endRad   = float(d.angle2) * DEG2RAD;
        while (endRad < startRad) endRad += float(TWO_PI);
        float span = endRad - startRad;
        int   segs = std::max(4, int(float(m_arcSegments) * std::abs(span) / float(TWO_PI)));
        for (int i = 0; i <= segs; ++i) {
            float ang = startRad + span * float(i) / float(segs);
            e.polyline.addVertex(glm::vec3(
                d.cx + d.radius * std::cos(ang),
                d.cy + d.radius * std::sin(ang), 0));
        }
        pushEntity(std::move(e));
    }

    void addCircle(const DL_CircleData& d) override {
        DxfEntity e;
        e.type  = DxfEntity::Type::Circle;
        e.layer = m_currentLayer;

        DxfArcInfo ai;
        ai.center = glm::vec2(d.cx, d.cy); ai.radius = float(d.radius);
        ai.startAngle = 0; ai.endAngle = 360; ai.isCCW = true;
        e.arcInfo = ai;

        e.polyline.setClosed(true);
        for (int i = 0; i <= m_arcSegments; ++i) {
            float ang = float(TWO_PI) * float(i) / float(m_arcSegments);
            e.polyline.addVertex(glm::vec3(
                d.cx + d.radius * std::cos(ang),
                d.cy + d.radius * std::sin(ang), 0));
        }
        pushEntity(std::move(e));
    }

    void addEllipse(const DL_EllipseData& d) override {
        DxfEntity e;
        e.type  = DxfEntity::Type::Ellipse;
        e.layer = m_currentLayer;

        float majorLen   = float(std::sqrt(d.mx * d.mx + d.my * d.my));
        float minorLen   = majorLen * float(d.ratio);
        float majorAngle = std::atan2(float(d.my), float(d.mx));
        float startParam = float(d.angle1);
        float endParam   = float(d.angle2);
        while (endParam < startParam) endParam += float(TWO_PI);

        float span = endParam - startParam;
        bool  full = std::abs(span - float(TWO_PI)) < 1e-4f;
        int   segs = full ? m_arcSegments : std::max(4, int(float(m_arcSegments) * span / float(TWO_PI)));
        if (full) e.polyline.setClosed(true);

        for (int i = 0; i <= segs; ++i) {
            float t  = startParam + span * float(i) / float(segs);
            float lx = majorLen * std::cos(t);
            float ly = minorLen * std::sin(t);
            float rx = lx * std::cos(majorAngle) - ly * std::sin(majorAngle);
            float ry = lx * std::sin(majorAngle) + ly * std::cos(majorAngle);
            e.polyline.addVertex(glm::vec3(float(d.cx) + rx, float(d.cy) + ry, 0));
        }
        pushEntity(std::move(e));
    }

    void addPolyline(const DL_PolylineData& d) override {
        m_pendingPolyline        = DxfEntity();
        m_pendingPolyline.type   = DxfEntity::Type::Polyline;
        m_pendingPolyline.layer  = m_currentLayer;
        m_polylineClosed         = (d.flags & 1) != 0;
        m_pendingPolyline.polyline.setClosed(m_polylineClosed);
        m_pendingVertices.clear();
    }

    void addVertex(const DL_VertexData& d) override {
        m_pendingVertices.push_back({ glm::vec2(d.x, d.y), d.bulge });
    }

    void endEntity() override {
        if (m_pendingVertices.empty()) {
            flushSplineIfPending();
            return;
        }

        ofPolyline& pl = m_pendingPolyline.polyline;
        for (int i = 0; i < (int)m_pendingVertices.size(); ++i) {
            const auto& v    = m_pendingVertices[i];
            const bool  last = (i == (int)m_pendingVertices.size() - 1);

            pl.addVertex(glm::vec3(v.pos, 0));

            if (std::abs(v.bulge) > 1e-10) {
                glm::vec2 nextPos;
                if (!last) {
                    nextPos = m_pendingVertices[i + 1].pos;
                } else if (m_polylineClosed) {
                    nextPos = m_pendingVertices[0].pos;
                } else {
                    continue;
                }
                appendBulgeArc(v.pos, nextPos, v.bulge, m_arcSegments, pl);
            }
        }

        pushEntity(std::move(m_pendingPolyline));
        m_pendingVertices.clear();
    }

    void addSpline(const DL_SplineData& d) override {
        flushSplineIfPending();
        m_pendingSpline          = DxfEntity();
        m_pendingSpline.type     = DxfEntity::Type::Spline;
        m_pendingSpline.layer    = m_currentLayer;
        m_splineDegree           = d.degree;
        m_splineClosed           = (d.flags & 1) != 0;
        m_splineKnots.clear();
        m_splineControlPts.clear();
        m_splineFitPts.clear();
        m_splinePending          = true;
    }

    void addKnot(const DL_KnotData& d) override       { m_splineKnots.push_back(d.k); }
    void addControlPoint(const DL_ControlPointData& d) override { m_splineControlPts.push_back(glm::vec2(d.x, d.y)); }
    void addFitPoint(const DL_FitPointData& d) override         { m_splineFitPts.push_back(glm::vec2(d.x, d.y)); }

    void endSequence() override { flushSplineIfPending(); }

    // ── Result ─────────────────────────────────────────────────────────────

    std::vector<DxfLayer> takeLayers() {
        flushSplineIfPending();
        return std::move(m_layers);
    }

private:
    int                          m_arcSegments;
    std::string                  m_currentLayer = "0";
    std::vector<DxfLayer>        m_layers;
    std::map<std::string, size_t> m_layerIndex;

    struct PendingVertex { glm::vec2 pos; double bulge; };
    DxfEntity                    m_pendingPolyline;
    std::vector<PendingVertex>   m_pendingVertices;
    bool                         m_polylineClosed = false;

    DxfEntity                    m_pendingSpline;
    unsigned int                 m_splineDegree = 3;
    std::vector<double>          m_splineKnots;
    std::vector<glm::vec2>       m_splineControlPts;
    std::vector<glm::vec2>       m_splineFitPts;
    bool                         m_splineClosed  = false;
    bool                         m_splinePending = false;

    DxfLayer& ensureLayer(const std::string& name) {
        auto it = m_layerIndex.find(name);
        if (it != m_layerIndex.end()) return m_layers[it->second];
        m_layerIndex[name] = m_layers.size();
        m_layers.push_back({ name, {} });
        return m_layers.back();
    }

    void pushEntity(DxfEntity&& e) {
        ensureLayer(e.layer).entities.push_back(std::move(e));
    }

    void flushSplineIfPending() {
        if (!m_splinePending) return;
        m_splinePending = false;

        ofPolyline& pl = m_pendingSpline.polyline;

        if (!m_splineFitPts.empty()) {
            for (auto& p : m_splineFitPts)
                pl.addVertex(glm::vec3(p, 0));
            if (m_splineClosed) pl.setClosed(true);
            pushEntity(std::move(m_pendingSpline));
            return;
        }

        int  k     = int(m_splineDegree);
        int  n     = int(m_splineControlPts.size());
        auto& knots = m_splineKnots;

        if (n < 2 || (int)knots.size() < n + k + 1) {
            for (auto& p : m_splineControlPts)
                pl.addVertex(glm::vec3(p, 0));
            if (m_splineClosed) pl.setClosed(true);
            pushEntity(std::move(m_pendingSpline));
            return;
        }

        double tMin = knots[k];
        double tMax = knots[n];
        int    segs = m_arcSegments;

        for (int i = 0; i <= segs; ++i) {
            double t = tMin + (tMax - tMin) * double(i) / double(segs);
            t = std::min(t, tMax - 1e-10);
            int span = k;
            for (int j = k; j < n; ++j) {
                if (t >= knots[j] && t < knots[j + 1]) { span = j; break; }
            }
            glm::vec2 pt = deBoor(k, span, knots, m_splineControlPts, t);
            pl.addVertex(glm::vec3(pt, 0));
        }

        if (m_splineClosed) pl.setClosed(true);
        pushEntity(std::move(m_pendingSpline));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DxfDocument
// ─────────────────────────────────────────────────────────────────────────────

std::vector<const DxfEntity*> DxfDocument::getAllEntities() const {
    std::vector<const DxfEntity*> out;
    for (auto& layer : layers)
        for (auto& e : layer.entities)
            out.push_back(&e);
    return out;
}

std::vector<const DxfEntity*> DxfDocument::getEntitiesOnLayer(const std::string& name) const {
    std::vector<const DxfEntity*> out;
    for (auto& layer : layers)
        if (layer.name == name)
            for (auto& e : layer.entities)
                out.push_back(&e);
    return out;
}

std::vector<ofPolyline> DxfDocument::getAllPolylines() const {
    std::vector<ofPolyline> out;
    for (auto& layer : layers)
        for (auto& e : layer.entities)
            if (!e.polyline.getVertices().empty())
                out.push_back(e.polyline);
    return out;
}

std::vector<ofPolyline> DxfDocument::getPolylinesOnLayer(const std::string& name) const {
    std::vector<ofPolyline> out;
    for (auto& layer : layers)
        if (layer.name == name)
            for (auto& e : layer.entities)
                if (!e.polyline.getVertices().empty())
                    out.push_back(e.polyline);
    return out;
}

std::vector<std::string> DxfDocument::getLayerNames() const {
    std::vector<std::string> out;
    for (auto& layer : layers) out.push_back(layer.name);
    return out;
}

void DxfDocument::flipY() {
    // Mirror all vertices: y_new = (bounds.y + bounds.height) - (y - bounds.y)
    //                             = bounds.y + bounds.height - y + bounds.y
    //                             = 2*bounds.y + bounds.height - y
    float top = bounds.y + bounds.height;

    for (auto& layer : layers) {
        for (auto& e : layer.entities) {
            // Flip polyline vertices
            auto& verts = e.polyline.getVertices();
            for (auto& v : verts) v.y = top - v.y;

            // Flip arc metadata: angles mirror around the X-axis, and winding reverses
            if (e.arcInfo) {
                auto& ai = e.arcInfo.value();
                ai.center.y  = top - ai.center.y;
                // Mirror angles: a_new = -a (then normalise to [0,360))
                float s = std::fmod(360.f - ai.startAngle, 360.f);
                float en = std::fmod(360.f - ai.endAngle,   360.f);
                ai.startAngle = en;   // swap: after Y-flip start/end swap too
                ai.endAngle   = s;
                ai.isCCW      = !ai.isCCW;
            }
        }
    }

    // Recompute bounds from the flipped vertices
    bool first = true;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (auto& layer : layers) {
        for (auto& e : layer.entities) {
            for (auto& v : e.polyline.getVertices()) {
                if (first) { minX = maxX = v.x; minY = maxY = v.y; first = false; }
                else {
                    minX = std::min(minX, v.x); minY = std::min(minY, v.y);
                    maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y);
                }
            }
        }
    }
    if (!first) bounds.set(minX, minY, maxX - minX, maxY - minY);
}

// ─────────────────────────────────────────────────────────────────────────────
// ofxDxf
// ─────────────────────────────────────────────────────────────────────────────

static DxfDocument buildDocument(OfxDxfAdapter& adapter) {
    DxfDocument doc;
    doc.layers = adapter.takeLayers();

    // Compute bounding box
    bool first = true;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (auto& layer : doc.layers) {
        for (auto& e : layer.entities) {
            for (auto& v : e.polyline.getVertices()) {
                if (first) {
                    minX = maxX = v.x;
                    minY = maxY = v.y;
                    first = false;
                } else {
                    minX = std::min(minX, v.x);
                    minY = std::min(minY, v.y);
                    maxX = std::max(maxX, v.x);
                    maxY = std::max(maxY, v.y);
                }
            }
        }
    }
    if (!first) doc.bounds.set(minX, minY, maxX - minX, maxY - minY);

    return doc;
}

DxfDocument ofxDxf::load(const std::string& path, int arcSegments) {
    std::string fullPath = ofToDataPath(path, true);

    OfxDxfAdapter adapter(arcSegments);
    DL_Dxf        dxf;

    if (!dxf.in(fullPath, &adapter)) {
        DxfDocument doc;
        doc.errors.push_back("ofxDxf: failed to open '" + fullPath + "'");
        ofLogError("ofxDxf") << "failed to open: " << fullPath;
        return doc;
    }

    return buildDocument(adapter);
}

DxfDocument ofxDxf::loadBuffer(const ofBuffer& buffer, int arcSegments) {
    // dxflib reads from a file path; write to a temp file then parse it.
    std::string tmpPath = ofToDataPath("_ofxdxflib_tmp.dxf", true);
    {
        ofFile tmp(tmpPath, ofFile::WriteOnly, true);
        tmp.writeFromBuffer(buffer);
    }

    DxfDocument doc = load("_ofxdxflib_tmp.dxf", arcSegments);

    // Clean up
    std::remove(tmpPath.c_str());
    return doc;
}

// ─────────────────────────────────────────────────────────────────────────────
// ofxDxf::save
// ─────────────────────────────────────────────────────────────────────────────

bool ofxDxf::save(const DxfDocument& doc, const std::string& path) {
    DL_Dxf dxf;
    DL_WriterA* dw = dxf.out(path.c_str(), DL_VERSION_2000);
    if (!dw) {
        ofLogError("ofxDxf") << "save: could not open for writing: " << path;
        return false;
    }

    // ── HEADER ───────────────────────────────────────────────────────────────
    dxf.writeHeader(*dw);
    dw->sectionEnd();

    // ── TABLES ───────────────────────────────────────────────────────────────
    dw->sectionTables();

    dxf.writeVPort(*dw);

    // LTYPE table
    dw->table("LTYPE", 3);
    dxf.writeLinetype(*dw, DL_LinetypeData("BYBLOCK",     "", 0, 0, 0.0));
    dxf.writeLinetype(*dw, DL_LinetypeData("BYLAYER",     "", 0, 0, 0.0));
    dxf.writeLinetype(*dw, DL_LinetypeData("CONTINUOUS",  "Solid line", 0, 0, 0.0));
    dw->tableEnd();

    // LAYER table — "0" always written first, then any doc layers not named "0"
    int numLayers = 1;
    for (auto& layer : doc.layers) if (layer.name != "0") ++numLayers;
    dw->table("LAYER", numLayers);
    dxf.writeLayer(*dw,
        DL_LayerData("0", 0),
        DL_Attributes("", DL_Codes::black, -1, "CONTINUOUS", 1.0));
    for (auto& layer : doc.layers) {
        if (layer.name == "0") continue;
        dxf.writeLayer(*dw,
            DL_LayerData(layer.name, 0),
            DL_Attributes("", DL_Codes::bylayer, -1, "CONTINUOUS", 1.0));
    }
    dw->tableEnd();

    // STYLE table
    dw->table("STYLE", 1);
    dxf.writeStyle(*dw, DL_StyleData("Standard", 0, 2.5, 1.0, 0.0, 0, 2.5, "", ""));
    dw->tableEnd();

    dxf.writeView(*dw);
    dxf.writeUcs(*dw);

    // APPID table
    dw->table("APPID", 1);
    dxf.writeAppid(*dw, "ACAD");
    dw->tableEnd();

    dxf.writeDimStyle(*dw, 2.5, 1.25, 0.625, 0.625, 2.5);

    // BLOCK_RECORD table (R2000 requires it)
    dxf.writeBlockRecord(*dw);
    dw->tableEnd();

    dw->sectionEnd();

    // ── BLOCKS ───────────────────────────────────────────────────────────────
    dw->sectionBlocks();
    dxf.writeBlock(*dw,    DL_BlockData("*Model_Space",  0, 0.0, 0.0, 0.0));
    dxf.writeEndBlock(*dw, "*Model_Space");
    dxf.writeBlock(*dw,    DL_BlockData("*Paper_Space",  0, 0.0, 0.0, 0.0));
    dxf.writeEndBlock(*dw, "*Paper_Space");
    dw->sectionEnd();

    // ── ENTITIES ─────────────────────────────────────────────────────────────
    dw->sectionEntities();

    for (auto& layer : doc.layers) {
        DL_Attributes attrib(layer.name, DL_Codes::bylayer, -1, "BYLAYER", 1.0);

        for (auto& entity : layer.entities) {
            const auto& verts = entity.polyline.getVertices();
            if (verts.size() < 2) continue;

            int flags = entity.polyline.isClosed() ? 1 : 0;
            dxf.writePolyline(*dw,
                DL_PolylineData((int)verts.size(), 0, 0, flags),
                attrib);
            for (auto& v : verts) {
                dxf.writeVertex(*dw, DL_VertexData(v.x, v.y, 0.0));
            }
            dxf.writePolylineEnd(*dw);
        }
    }

    dw->sectionEnd();

    // ── OBJECTS (required for R2000) ──────────────────────────────────────────
    dxf.writeObjects(*dw);
    dxf.writeObjectsEnd(*dw);

    dw->dxfEOF();
    dw->close();
    delete dw;
    return true;
}
