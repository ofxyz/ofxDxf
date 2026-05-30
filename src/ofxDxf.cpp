#include "ofxDxf.h"

#include "dl_dxf.h"
#include "dl_creationadapter.h"
#include "dl_entities.h"
#include "dl_attributes.h"
#include "dl_codes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace {

static constexpr float DEG2RAD = float(M_PI) / 180.f;
static constexpr float RAD2DEG = 180.f / float(M_PI);

static void initStrokePath(ofPath& path) {
    path.clear();
    path.setFilled(false);
    path.setUseShapeColor(false);
    path.setStrokeWidth(0);
    path.setMode(ofPath::COMMANDS);
    path.setPolyWindingMode(OF_POLY_WINDING_ODD);
}

static ofColor color24ToOf(int color24) {
    if (color24 < 0) return ofColor::black;
    return ofColor((color24 >> 16) & 0xFF, (color24 >> 8) & 0xFF, color24 & 0xFF);
}

static ofColor aciToOf(int aci) {
    aci = std::abs(aci);
    if (aci <= 0 || aci >= 256) return ofColor::black;
    const double* rgb = dxfColors[aci];
    return ofColor(int(rgb[0] * 255.f), int(rgb[1] * 255.f), int(rgb[2] * 255.f));
}

static ofColor resolveLayerColor(const DL_Attributes& attrs, int paletteIndex) {
    if (attrs.getColor24() >= 0) return color24ToOf(attrs.getColor24());
    const int c = attrs.getColor();
    if (c == 256 || c == 0) return ofxDxf::paletteColor(paletteIndex);
    return aciToOf(c);
}

static ofColor resolveEntityColor(const DL_Attributes& attrs, const ofColor& layerColor) {
    if (attrs.getColor24() >= 0) return color24ToOf(attrs.getColor24());
    const int c = attrs.getColor();
    if (c == 256) return layerColor;
    if (c == 0) return ofColor::black;
    return aciToOf(c);
}

static void addDxfArcToPath(ofPath& path, float cx, float cy, float radius,
                            float angle1Deg, float angle2Deg, bool ccw = true) {
    float a1 = angle1Deg;
    float a2 = angle2Deg;
    if (ccw) {
        while (a2 < a1) a2 += 360.f;
        if (a2 - a1 < 1e-4f) return;
        const float sx = cx + radius * std::cos(a1 * DEG2RAD);
        const float sy = cy + radius * std::sin(a1 * DEG2RAD);
        if (path.getCommands().empty()
            || path.getCommands().back().type == ofPath::Command::close) {
            path.moveTo(sx, sy);
        }
        path.arc(glm::vec2(cx, cy), radius, radius, a1, a2);
    } else {
        while (a2 > a1) a2 -= 360.f;
        if (a1 - a2 < 1e-4f) return;
        const float sx = cx + radius * std::cos(a1 * DEG2RAD);
        const float sy = cy + radius * std::sin(a1 * DEG2RAD);
        if (path.getCommands().empty()
            || path.getCommands().back().type == ofPath::Command::close) {
            path.moveTo(sx, sy);
        }
        path.arcNegative(glm::vec2(cx, cy), radius, radius, a1, a2);
    }
}

/// Bulge arc between p1→p2 as an exact ofPath arc command.
static void appendBulgeArcToPath(ofPath& path, const glm::vec2& p1, const glm::vec2& p2,
                                 double bulge) {
    if (std::abs(bulge) < 1e-10) return;

    const double theta = 4.0 * std::atan(bulge);
    const double d     = glm::distance(p1, p2);
    if (d < 1e-10) return;

    const double r = std::abs(d / (2.0 * std::sin(theta / 2.0)));
    const glm::vec2 mid = (p1 + p2) * 0.5f;
    glm::vec2 chord = p2 - p1;
    glm::vec2 perp  = glm::normalize(glm::vec2(-chord.y, chord.x));
    const double dToCenter = std::sqrt(r * r - (d / 2.0) * (d / 2.0));

    glm::vec2 center;
    if (bulge > 0) center = mid - glm::vec2(perp * float(dToCenter));
    else           center = mid + glm::vec2(perp * float(dToCenter));

    const float startDeg = std::atan2(p1.y - center.y, p1.x - center.x) * RAD2DEG;
    float endDeg = std::atan2(p2.y - center.y, p2.x - center.x) * RAD2DEG;

    if (bulge > 0) addDxfArcToPath(path, center.x, center.y, float(r), startDeg, endDeg, true);
    else           addDxfArcToPath(path, center.x, center.y, float(r), startDeg, endDeg, false);
}

static glm::vec2 deBoor(int k, int i, const std::vector<double>& knots,
                        const std::vector<glm::vec2>& ctrl, double t) {
    std::vector<glm::vec2> d(k + 1);
    for (int j = 0; j <= k; ++j) {
        const int idx = i - k + j;
        if (idx >= 0 && idx < (int)ctrl.size()) d[j] = ctrl[idx];
    }
    for (int r = 1; r <= k; ++r) {
        for (int j = k; j >= r; --j) {
            const int knotIdx = i - k + j;
            const double denom = knots[knotIdx + k - r + 1] - knots[knotIdx];
            if (std::abs(denom) < 1e-12) continue;
            const double alpha = (t - knots[knotIdx]) / denom;
            d[j] = glm::vec2(
                (1.0 - alpha) * d[j - 1].x + alpha * d[j].x,
                (1.0 - alpha) * d[j - 1].y + alpha * d[j].y);
        }
    }
    return d[k];
}

static ofPath pathFromSpline(const DxfSplineInfo& sp, int segs) {
    ofPath path;
    initStrokePath(path);

    if (!sp.fitPoints.empty()) {
        path.moveTo(sp.fitPoints[0].x, sp.fitPoints[0].y);
        for (size_t i = 1; i < sp.fitPoints.size(); ++i)
            path.lineTo(sp.fitPoints[i].x, sp.fitPoints[i].y);
        if (sp.closed) path.close();
        return path;
    }

    const int  k     = sp.degree;
    const int  n     = (int)sp.controlPoints.size();
    const auto& knots = sp.knots;
    if (n < 2 || (int)knots.size() < n + k + 1) {
        for (const auto& p : sp.controlPoints)
            path.lineTo(p.x, p.y);
        if (sp.closed) path.close();
        return path;
    }

    const double tMin = knots[k];
    const double tMax = knots[n];
    path.moveTo(sp.controlPoints.front().x, sp.controlPoints.front().y);
    for (int i = 1; i <= segs; ++i) {
        double t = tMin + (tMax - tMin) * double(i) / double(segs);
        t = std::min(t, tMax - 1e-10);
        int span = k;
        for (int j = k; j < n; ++j) {
            if (t >= knots[j] && t < knots[j + 1]) { span = j; break; }
        }
        const glm::vec2 pt = deBoor(k, span, knots, sp.controlPoints, t);
        path.lineTo(pt.x, pt.y);
    }
    if (sp.closed) path.close();
    return path;
}

static ofPath pathFromEllipse(const DxfEllipseInfo& ei, int segs) {
    ofPath path;
    initStrokePath(path);

    const float majorLen   = glm::length(ei.majorAxis);
    const float minorLen   = majorLen * ei.ratio;
    const float majorAngle = std::atan2(ei.majorAxis.y, ei.majorAxis.x);

    double startParam = ei.startParam;
    double endParam   = ei.endParam;
    while (endParam < startParam) endParam += TWO_PI;

    const double span = endParam - startParam;
    const bool   full = std::abs(span - TWO_PI) < 1e-4;

    auto eval = [&](double t) -> glm::vec2 {
        const float lx = majorLen * std::cos(float(t));
        const float ly = minorLen * std::sin(float(t));
        return ei.center + glm::vec2(
            lx * std::cos(majorAngle) - ly * std::sin(majorAngle),
            lx * std::sin(majorAngle) + ly * std::cos(majorAngle));
    };

    const glm::vec2 p0 = eval(startParam);
    path.moveTo(p0.x, p0.y);
    const int steps = full ? segs : std::max(4, int(float(segs) * float(span / TWO_PI)));
    for (int i = 1; i <= steps; ++i) {
        const double t = startParam + span * double(i) / double(steps);
        const glm::vec2 p = eval(t);
        path.lineTo(p.x, p.y);
    }
    if (full) path.close();
    return path;
}

static int adaptiveSegmentCount(float radiusWorld, float viewZoom, float spanDegrees = 360.f) {
    if (radiusWorld <= 0.f || viewZoom <= 0.f
        || !std::isfinite(radiusWorld) || !std::isfinite(viewZoom))
        return 64;

    const float arcLengthPx =
        (std::abs(spanDegrees) / 360.f) * TWO_PI * radiusWorld * viewZoom;
    const float kTargetPxPerSeg = 1.5f;
    const int   segs = int(std::ceil(arcLengthPx / kTargetPxPerSeg));
    return std::clamp(segs, 12, 2048);
}

static int defaultDisplayResolution() {
    const ofStyle& style = ofGetStyle();
    return std::max(style.circleResolution, style.curveResolution);
}

static int resolveSegments(int segments) {
    return segments > 0 ? segments : defaultDisplayResolution();
}

static int boundsSegmentCount() {
    return std::max(defaultDisplayResolution(), 32);
}

static float minArcRadiusInPath(const ofPath& path) {
    float minR = std::numeric_limits<float>::max();
    for (const auto& cmd : path.getCommands()) {
        if (cmd.type == ofPath::Command::arc || cmd.type == ofPath::Command::arcNegative) {
            minR = std::min(minR, std::max(cmd.radiusX, cmd.radiusY));
        }
    }
    return (minR == std::numeric_limits<float>::max()) ? 0.f : minR;
}

static void drawPathOutlines(const ofPath& path, int circleRes, int curveRes) {
    ofPath p = path;
    p.setCircleResolution(circleRes);
    p.setCurveResolution(curveRes);
    for (const auto& outline : p.getOutline()) {
        if (outline.size() < 2) continue;
        outline.draw();
    }
}

static void drawLocalCircleOutlines(const DxfArcInfo& ai, float viewZoom) {
    if (ai.radius <= 0.f) return;
    const int segs = adaptiveSegmentCount(ai.radius, viewZoom);

    ofPushMatrix();
    ofTranslate(ai.center.x, ai.center.y);

    ofPath p;
    initStrokePath(p);
    p.circle(0.f, 0.f, ai.radius);
    drawPathOutlines(p, segs, segs);

    ofPopMatrix();
}

static void drawLocalArcOutlines(const DxfArcInfo& ai, float viewZoom) {
    if (ai.radius <= 0.f) return;

    float a1 = ai.startAngle;
    float a2 = ai.endAngle;
    if (ai.isCCW) {
        while (a2 < a1) a2 += 360.f;
    } else {
        while (a2 > a1) a2 -= 360.f;
    }
    if (std::abs(a2 - a1) < 1e-4f) return;

    const int segs = adaptiveSegmentCount(ai.radius, viewZoom, std::abs(a2 - a1));

    ofPushMatrix();
    ofTranslate(ai.center.x, ai.center.y);

    ofPath p;
    initStrokePath(p);
    addDxfArcToPath(p, 0.f, 0.f, ai.radius, a1, a2, ai.isCCW);
    drawPathOutlines(p, segs, segs);

    ofPopMatrix();
}

static int displayResolutionForEntity(const DxfEntity& e, int baseSegments, float viewZoom) {
    int res = resolveSegments(baseSegments);
    if (viewZoom <= 1.f) return res;

    if (e.arcInfo && e.arcInfo->radius > 0.f) {
        const float span = (e.type == DxfEntity::Type::Circle)
            ? 360.f
            : std::abs(e.arcInfo->endAngle - e.arcInfo->startAngle);
        res = std::max(res, adaptiveSegmentCount(e.arcInfo->radius, viewZoom, span));
    }

    const float minR = minArcRadiusInPath(e.path);
    if (minR > 0.f)
        res = std::max(res, adaptiveSegmentCount(minR, viewZoom));

    return res;
}

static float polylineLength(const std::vector<glm::vec2>& pts, bool closed) {
    if (pts.size() < 2) return 0.f;
    float len = 0.f;
    for (size_t i = 1; i < pts.size(); ++i)
        len += glm::distance(pts[i - 1], pts[i]);
    if (closed)
        len += glm::distance(pts.back(), pts.front());
    return len;
}

static int exportSegmentCountForSpline(const DxfSplineInfo& sp) {
    constexpr float kMaxSegLen = 0.025f; // ~0.025 mm per segment
    constexpr int   kMin       = 256;
    constexpr int   kMax       = 8192;

    const float len = !sp.fitPoints.empty()
        ? polylineLength(sp.fitPoints, sp.closed)
        : polylineLength(sp.controlPoints, sp.closed);
    if (len <= 0.f) return kMin;
    return std::clamp(int(std::ceil(len / kMaxSegLen)), kMin, kMax);
}

static int exportSegmentCountForEllipse(const DxfEllipseInfo& ei) {
    constexpr float kMaxSegLen = 0.025f;
    constexpr int   kMin       = 64;
    constexpr int   kMax       = 4096;

    const float a = glm::length(ei.majorAxis);
    const float b = a * ei.ratio;
    if (a <= 0.f) return kMin;

    const float h     = (a - b) * (a - b) / ((a + b) * (a + b));
    const float perim = float(M_PI) * (a + b) * (1.f + 3.f * h / (10.f + std::sqrt(4.f - 3.f * h)));

    double span = ei.endParam - ei.startParam;
    while (span < 0) span += TWO_PI;
    const float frac = float(span / TWO_PI);

    return std::clamp(int(std::ceil(perim * frac / kMaxSegLen)), kMin, kMax);
}

static ofPath entityDrawPath(const DxfEntity& e, int segments) {
    const int res = resolveSegments(segments);
    if (e.splineInfo) return pathFromSpline(*e.splineInfo, res);
    if (e.ellipseInfo && e.path.getCommands().empty())
        return pathFromEllipse(*e.ellipseInfo, res);

    ofPath p = e.path;
    p.setCircleResolution(res);
    p.setCurveResolution(res);
    return p;
}

static ofPath entityExportPath(const DxfEntity& e, int segments) {
    if (e.splineInfo) {
        const int segs = segments > 0 ? segments : exportSegmentCountForSpline(*e.splineInfo);
        return pathFromSpline(*e.splineInfo, segs);
    }
    if (e.ellipseInfo && e.path.getCommands().empty()) {
        const int segs = segments > 0 ? segments : exportSegmentCountForEllipse(*e.ellipseInfo);
        return pathFromEllipse(*e.ellipseInfo, segs);
    }

    ofPath p = e.path;
    p.setMode(ofPath::COMMANDS);
    return p;
}

struct DxfBlock {
    glm::vec2              base {0, 0};
    std::vector<DxfEntity> entities;
};

static glm::vec2 transformPoint2D(glm::vec2 p, const glm::vec2& blockBase,
                                  const glm::vec2& insert, float sx, float sy,
                                  float angleDeg) {
    p -= blockBase;
    p.x *= sx;
    p.y *= sy;
    if (std::abs(angleDeg) > 1e-6f) {
        const float rad = glm::radians(angleDeg);
        const float c   = std::cos(rad);
        const float s   = std::sin(rad);
        p = glm::vec2(c * p.x - s * p.y, s * p.x + c * p.y);
    }
    return p + insert;
}

static void applyInsertTransform2D(ofPath& path, const glm::vec2& blockBase,
                                   const glm::vec2& insert, float sx, float sy,
                                   float angleDeg) {
    path.translate(glm::vec2(-blockBase.x, -blockBase.y));
    path.scale(sx, sy);
    if (std::abs(angleDeg) > 1e-6f)
        path.rotateDeg(angleDeg, glm::vec3(0, 0, 1));
    path.translate(glm::vec2(insert.x, insert.y));
}

static DxfEntity transformEntityForInsert(const DxfEntity& src, const glm::vec2& blockBase,
                                          const glm::vec2& insert, float sx, float sy,
                                          float angleDeg) {
    DxfEntity e = src;
    applyInsertTransform2D(e.path, blockBase, insert, sx, sy, angleDeg);

    if (e.arcInfo) {
        auto& ai = *e.arcInfo;
        ai.center     = transformPoint2D(ai.center, blockBase, insert, sx, sy, angleDeg);
        ai.radius    *= std::max(std::abs(sx), std::abs(sy));
        ai.startAngle += angleDeg;
        ai.endAngle   += angleDeg;
    }
    if (e.ellipseInfo) {
        auto& ei = *e.ellipseInfo;
        ei.center = transformPoint2D(ei.center, blockBase, insert, sx, sy, angleDeg);
        const float rad = glm::radians(angleDeg);
        const float c   = std::cos(rad);
        const float s   = std::sin(rad);
        const glm::vec2 ma = ei.majorAxis;
        ei.majorAxis = glm::vec2(
            ma.x * sx * c - ma.y * sy * s,
            ma.x * sx * s + ma.y * sy * c);
    }
    if (e.splineInfo) {
        for (auto& p : e.splineInfo->controlPoints)
            p = transformPoint2D(p, blockBase, insert, sx, sy, angleDeg);
        for (auto& p : e.splineInfo->fitPoints)
            p = transformPoint2D(p, blockBase, insert, sx, sy, angleDeg);
    }
    return e;
}

static void expandBounds(ofRectangle& bounds, float x, float y, bool& first) {
    if (first) {
        bounds.set(x, y, 0, 0);
        first = false;
    } else {
        bounds.growToInclude(x, y);
    }
}

static ofRectangle boundsOfEntity(const DxfEntity& e) {
    ofRectangle b;
    bool first = true;
    const ofPath drawPath = entityDrawPath(e, boundsSegmentCount());
    for (const auto& outline : drawPath.getOutline()) {
        for (const auto& v : outline.getVertices())
            expandBounds(b, v.x, v.y, first);
    }
    if (e.type == DxfEntity::Type::Point && !e.path.getCommands().empty()) {
        const glm::vec2 p = e.path.getCommands()[0].to;
        expandBounds(b, p.x, p.y, first);
    }
    return b;
}

static void flipPathY(ofPath& path, float top) {
    for (auto& cmd : path.getCommands()) {
        switch (cmd.type) {
        case ofPath::Command::moveTo:
        case ofPath::Command::lineTo:
        case ofPath::Command::curveTo:
            cmd.to.y = top - cmd.to.y;
            break;
        case ofPath::Command::bezierTo:
        case ofPath::Command::quadBezierTo:
            cmd.cp1.y = top - cmd.cp1.y;
            cmd.cp2.y = top - cmd.cp2.y;
            cmd.to.y  = top - cmd.to.y;
            break;
        case ofPath::Command::arc:
        case ofPath::Command::arcNegative:
            cmd.to.y = top - cmd.to.y;
            std::swap(cmd.angleBegin, cmd.angleEnd);
            break;
        case ofPath::Command::close:
            break;
        }
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// DxfEntity
// ─────────────────────────────────────────────────────────────────────────────

std::vector<ofPolyline> DxfEntity::getOutline(int segments) const {
    return entityDrawPath(*this, segments).getOutline();
}

ofPath DxfEntity::resolvedPath(int segments) const {
    return entityDrawPath(*this, segments);
}

ofPath DxfEntity::exportPath(int segments) const {
    return entityExportPath(*this, segments);
}

void DxfEntity::draw(float viewZoom, int segments) const {
    if (type == Type::Point)
        return;

    // Circles/arcs: tessellate a local ofPath at the origin, then translate.
    // Same getOutline() pipeline as everything else, but avoids world-space
    // float jitter when center coordinates are large and radius is tiny.
    if (type == Type::Circle && arcInfo) {
        drawLocalCircleOutlines(*arcInfo, viewZoom);
        return;
    }
    if (type == Type::Arc && arcInfo) {
        drawLocalArcOutlines(*arcInfo, viewZoom);
        return;
    }

    const int res = displayResolutionForEntity(*this, segments, viewZoom);
    for (const auto& outline : getOutline(res)) {
        if (outline.size() < 2) continue;
        outline.draw();
    }
}

ofColor ofxDxf::aciToColor(int aci) { return aciToOf(aci); }

ofColor ofxDxf::paletteColor(int index) {
    static const ofColor kPalette[] = {
        ofColor(255,  80,  80), ofColor( 80, 200,  80), ofColor( 80, 140, 255),
        ofColor(255, 200,  60), ofColor(200,  80, 255), ofColor( 80, 220, 220),
        ofColor(255, 140,  60), ofColor(200, 200, 200),
    };
    const int n = int(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[((index % n) + n) % n];
}

// ─────────────────────────────────────────────────────────────────────────────
// OfxDxfAdapter
// ─────────────────────────────────────────────────────────────────────────────
class OfxDxfAdapter : public DL_CreationAdapter {
public:
    void addLayer(const DL_LayerData& data) override {
        const size_t idx = m_layers.size();
        ensureLayer(data.name);
        m_layerColors[data.name] = resolveLayerColor(getAttributes(), int(idx));
    }

    void addLine(const DL_LineData& d) override {
        DxfEntity e;
        e.type = DxfEntity::Type::Line;
        initStrokePath(e.path);
        e.path.moveTo(float(d.x1), float(d.y1));
        e.path.lineTo(float(d.x2), float(d.y2));
        pushEntity(std::move(e));
    }

    void addPoint(const DL_PointData& d) override {
        DxfEntity e;
        e.type = DxfEntity::Type::Point;
        initStrokePath(e.path);
        e.path.moveTo(float(d.x), float(d.y));
        pushEntity(std::move(e));
    }

    void addArc(const DL_ArcData& d) override {
        DxfEntity e;
        e.type = DxfEntity::Type::Arc;
        initStrokePath(e.path);

        DxfArcInfo ai;
        ai.center     = glm::vec2(float(d.cx), float(d.cy));
        ai.radius     = float(d.radius);
        ai.startAngle = float(d.angle1);
        ai.endAngle   = float(d.angle2);
        ai.isCCW      = true;
        e.arcInfo     = ai;

        addDxfArcToPath(e.path, ai.center.x, ai.center.y, ai.radius,
                        ai.startAngle, ai.endAngle, ai.isCCW);
        pushEntity(std::move(e));
    }

    void addCircle(const DL_CircleData& d) override {
        DxfEntity e;
        e.type = DxfEntity::Type::Circle;
        initStrokePath(e.path);

        DxfArcInfo ai;
        ai.center = glm::vec2(float(d.cx), float(d.cy));
        ai.radius = float(d.radius);
        ai.startAngle = 0;
        ai.endAngle   = 360;
        ai.isCCW      = true;
        e.arcInfo     = ai;

        e.path.circle(float(d.cx), float(d.cy), float(d.radius));
        pushEntity(std::move(e));
    }

    void addEllipse(const DL_EllipseData& d) override {
        DxfEntity e;
        e.type = DxfEntity::Type::Ellipse;
        initStrokePath(e.path);

        DxfEllipseInfo ei;
        ei.center     = glm::vec2(float(d.cx), float(d.cy));
        ei.majorAxis  = glm::vec2(float(d.mx), float(d.my));
        ei.ratio      = float(d.ratio);
        ei.startParam = d.angle1;
        ei.endParam   = d.angle2;
        e.ellipseInfo = ei;
        pushEntity(std::move(e));
    }

    void addPolyline(const DL_PolylineData& d) override {
        m_pendingPolyline      = DxfEntity();
        m_pendingPolyline.type = DxfEntity::Type::Polyline;
        initStrokePath(m_pendingPolyline.path);
        m_polylineClosed = (d.flags & 1) != 0;
        m_pendingVertices.clear();
    }

    void addVertex(const DL_VertexData& d) override {
        m_pendingVertices.push_back({ glm::vec2(float(d.x), float(d.y)), d.bulge });
    }

    void endEntity() override {
        if (m_pendingVertices.empty()) {
            flushSplineIfPending();
            return;
        }

        ofPath& path = m_pendingPolyline.path;
        if (!m_pendingVertices.empty())
            path.moveTo(m_pendingVertices[0].pos.x, m_pendingVertices[0].pos.y);

        for (int i = 0; i < (int)m_pendingVertices.size(); ++i) {
            const auto& v   = m_pendingVertices[i];
            const bool  last = (i == (int)m_pendingVertices.size() - 1);

            if (std::abs(v.bulge) > 1e-10) {
                glm::vec2 nextPos;
                if (!last) nextPos = m_pendingVertices[i + 1].pos;
                else if (m_polylineClosed) nextPos = m_pendingVertices[0].pos;
                else continue;
                appendBulgeArcToPath(path, v.pos, nextPos, v.bulge);
            } else if (!last) {
                path.lineTo(m_pendingVertices[i + 1].pos.x, m_pendingVertices[i + 1].pos.y);
            }
        }

        if (m_polylineClosed) path.close();
        pushEntity(std::move(m_pendingPolyline));
        m_pendingVertices.clear();
    }

    void addSpline(const DL_SplineData& d) override {
        flushSplineIfPending();
        m_pendingSpline      = DxfEntity();
        m_pendingSpline.type = DxfEntity::Type::Spline;
        initStrokePath(m_pendingSpline.path);
        m_splineDegree    = d.degree;
        m_splineClosed    = (d.flags & 1) != 0;
        m_splineKnots.clear();
        m_splineControlPts.clear();
        m_splineFitPts.clear();
        m_splinePending   = true;
    }

    void addKnot(const DL_KnotData& d) override {
        m_splineKnots.push_back(d.k);
    }
    void addControlPoint(const DL_ControlPointData& d) override {
        m_splineControlPts.push_back(glm::vec2(float(d.x), float(d.y)));
    }
    void addFitPoint(const DL_FitPointData& d) override {
        m_splineFitPts.push_back(glm::vec2(float(d.x), float(d.y)));
    }

    void endSequence() override { flushSplineIfPending(); }

    void addBlock(const DL_BlockData& data) override {
        if (data.name.empty() || data.name[0] == '*') {
            m_collectingBlock = false;
            m_activeBlock.clear();
            return;
        }
        m_activeBlock     = data.name;
        m_activeBlockBase = glm::vec2(float(data.bpx), float(data.bpy));
        m_blocks[m_activeBlock].base = m_activeBlockBase;
        m_collectingBlock = true;
    }

    void endBlock() override {
        m_collectingBlock = false;
        m_activeBlock.clear();
    }

    void addInsert(const DL_InsertData& d) override {
        expandInsert(d, 0);
    }

    std::vector<DxfLayer> takeLayers() {
        flushSplineIfPending();
        return std::move(m_layers);
    }

private:
    static constexpr int kMaxInsertDepth = 16;

    std::vector<DxfLayer>          m_layers;
    std::map<std::string, size_t>  m_layerIndex;
    std::map<std::string, ofColor> m_layerColors;
    std::map<std::string, DxfBlock> m_blocks;

    bool        m_collectingBlock = false;
    std::string m_activeBlock;
    glm::vec2   m_activeBlockBase {0, 0};

    struct PendingVertex { glm::vec2 pos; double bulge; };
    DxfEntity                  m_pendingPolyline;
    std::vector<PendingVertex> m_pendingVertices;
    bool                       m_polylineClosed = false;

    DxfEntity                  m_pendingSpline;
    unsigned int               m_splineDegree = 3;
    std::vector<double>        m_splineKnots;
    std::vector<glm::vec2>     m_splineControlPts;
    std::vector<glm::vec2>     m_splineFitPts;
    bool                       m_splineClosed  = false;
    bool                       m_splinePending   = false;

    DxfLayer& ensureLayer(const std::string& name) {
        auto it = m_layerIndex.find(name);
        if (it != m_layerIndex.end()) return m_layers[it->second];
        m_layerIndex[name] = m_layers.size();
        DxfLayer layer;
        layer.name = name;
        auto colorIt = m_layerColors.find(name);
        layer.color = (colorIt != m_layerColors.end())
            ? colorIt->second
            : ofxDxf::paletteColor(int(m_layers.size()));
        m_layers.push_back(std::move(layer));
        return m_layers.back();
    }

    ofColor layerColorFor(const std::string& name) const {
        auto it = m_layerColors.find(name);
        if (it != m_layerColors.end()) return it->second;
        auto idxIt = m_layerIndex.find(name);
        return ofxDxf::paletteColor(idxIt != m_layerIndex.end() ? int(idxIt->second) : 0);
    }

    void pushEntity(DxfEntity&& e) {
        std::string layerName = getAttributes().getLayer();
        if (layerName.empty()) layerName = "0";
        e.layer = layerName;
        e.color = resolveEntityColor(getAttributes(), layerColorFor(layerName));

        if (m_collectingBlock && !m_activeBlock.empty()) {
            m_blocks[m_activeBlock].entities.push_back(std::move(e));
            return;
        }

        ensureLayer(layerName).entities.push_back(std::move(e));
    }

    void expandInsert(const DL_InsertData& d, int depth) {
        if (d.name.empty() || depth > kMaxInsertDepth) return;

        const auto it = m_blocks.find(d.name);
        if (it == m_blocks.end()) {
            ofLogWarning("ofxDxf") << "INSERT references unknown block '" << d.name << "'";
            return;
        }

        const auto& block = it->second;
        const glm::vec2 insertPt(float(d.ipx), float(d.ipy));
        const float sx    = float(d.sx);
        const float sy    = float(d.sy);
        const float angle = float(d.angle);
        const int   cols  = std::max(1, d.cols);
        const int   rows  = std::max(1, d.rows);

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const glm::vec2 at(
                    insertPt.x + float(col) * float(d.colSp),
                    insertPt.y + float(row) * float(d.rowSp));

                for (const auto& src : block.entities) {
                    DxfEntity expanded =
                        transformEntityForInsert(src, block.base, at, sx, sy, angle);
                    pushEntity(std::move(expanded));
                }
            }
        }
    }

    void flushSplineIfPending() {
        if (!m_splinePending) return;
        m_splinePending = false;

        DxfSplineInfo sp;
        sp.degree        = int(m_splineDegree);
        sp.closed        = m_splineClosed;
        sp.knots         = m_splineKnots;
        sp.controlPoints = m_splineControlPts;
        sp.fitPoints     = m_splineFitPts;
        m_pendingSpline.splineInfo = sp;
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

std::vector<ofPath> DxfDocument::getAllPaths() const {
    std::vector<ofPath> out;
    for (auto& layer : layers)
        for (auto& e : layer.entities)
            out.push_back(e.path);
    return out;
}

std::vector<ofPath> DxfDocument::getPathsOnLayer(const std::string& name) const {
    std::vector<ofPath> out;
    for (auto& layer : layers)
        if (layer.name == name)
            for (auto& e : layer.entities)
                out.push_back(e.path);
    return out;
}

std::vector<ofPolyline> DxfDocument::getAllPolylines(int segments) const {
    const int res = resolveSegments(segments);
    std::vector<ofPolyline> out;
    for (auto& layer : layers)
        for (auto& e : layer.entities) {
            auto outlines = e.getOutline(res);
            out.insert(out.end(), outlines.begin(), outlines.end());
        }
    return out;
}

std::vector<ofPolyline> DxfDocument::getPolylinesOnLayer(const std::string& name,
                                                       int segments) const {
    const int res = resolveSegments(segments);
    std::vector<ofPolyline> out;
    for (auto& layer : layers)
        if (layer.name == name)
            for (auto& e : layer.entities) {
                auto outlines = e.getOutline(res);
                out.insert(out.end(), outlines.begin(), outlines.end());
            }
    return out;
}

std::vector<std::string> DxfDocument::getLayerNames() const {
    std::vector<std::string> out;
    for (auto& layer : layers) out.push_back(layer.name);
    return out;
}

void DxfDocument::flipY() {
    const float top = bounds.y + bounds.height;

    for (auto& layer : layers) {
        for (auto& e : layer.entities) {
            flipPathY(e.path, top);

            if (e.arcInfo) {
                auto& ai = e.arcInfo.value();
                ai.center.y  = top - ai.center.y;
                const float s  = std::fmod(360.f - ai.startAngle, 360.f);
                const float en = std::fmod(360.f - ai.endAngle,   360.f);
                ai.startAngle  = en;
                ai.endAngle    = s;
                ai.isCCW       = !ai.isCCW;
            }
            if (e.ellipseInfo) {
                e.ellipseInfo->center.y     = top - e.ellipseInfo->center.y;
                e.ellipseInfo->majorAxis.y  = -e.ellipseInfo->majorAxis.y;
            }
            if (e.splineInfo) {
                for (auto& p : e.splineInfo->controlPoints) p.y = top - p.y;
                for (auto& p : e.splineInfo->fitPoints)     p.y = top - p.y;
            }
        }
    }

    bool first = true;
    ofRectangle b;
    for (auto& layer : layers) {
        for (auto& e : layer.entities) {
            const ofRectangle eb = boundsOfEntity(e);
            if (eb.width <= 0 && eb.height <= 0) continue;
            if (first) { b = eb; first = false; }
            else b.growToInclude(eb);
        }
    }
    if (!first) bounds = b;
}

static bool skipLayerForBounds(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "defpoints";
}

static DxfDocument buildDocument(OfxDxfAdapter& adapter) {
    DxfDocument doc;
    doc.layers = adapter.takeLayers();

    bool first = true;
    for (auto& layer : doc.layers) {
        if (skipLayerForBounds(layer.name))
            continue;
        for (auto& e : layer.entities) {
            const ofRectangle eb = boundsOfEntity(e);
            if (eb.width <= 0 && eb.height <= 0) continue;
            if (first) { doc.bounds = eb; first = false; }
            else doc.bounds.growToInclude(eb);
        }
    }
    return doc;
}

DxfDocument ofxDxf::load(const std::string& path) {
    const std::string fullPath = ofToDataPath(path, true);
    OfxDxfAdapter adapter;
    DL_Dxf dxf;

    if (!dxf.in(fullPath, &adapter)) {
        DxfDocument doc;
        doc.errors.push_back("ofxDxf: failed to open '" + fullPath + "'");
        ofLogError("ofxDxf") << "failed to open: " << fullPath;
        return doc;
    }
    return buildDocument(adapter);
}

DxfDocument ofxDxf::loadBuffer(const ofBuffer& buffer) {
    const std::string tmpPath = ofToDataPath("_ofxdxflib_tmp.dxf", true);
    {
        ofFile tmp(tmpPath, ofFile::WriteOnly, true);
        tmp.writeFromBuffer(buffer);
    }
    DxfDocument doc = load("_ofxdxflib_tmp.dxf");
    std::remove(tmpPath.c_str());
    return doc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

static void writeEntity(DL_Dxf& dxf, DL_WriterA& dw, const DxfEntity& entity,
                        const DL_Attributes& attrib) {
    switch (entity.type) {
        case DxfEntity::Type::Line: {
            const auto& cmds = entity.path.getCommands();
            if (cmds.size() >= 2 && cmds[0].type == ofPath::Command::moveTo
                && cmds[1].type == ofPath::Command::lineTo) {
                dxf.writeLine(dw,
                    DL_LineData(cmds[0].to.x, cmds[0].to.y, 0,
                                cmds[1].to.x, cmds[1].to.y, 0),
                    attrib);
                return;
            }
            break;
        }
        case DxfEntity::Type::Arc:
            if (entity.arcInfo) {
                const auto& ai = *entity.arcInfo;
                dxf.writeArc(dw,
                    DL_ArcData(ai.center.x, ai.center.y, 0, ai.radius,
                               ai.startAngle, ai.endAngle),
                    attrib);
                return;
            }
            break;
        case DxfEntity::Type::Circle:
            if (entity.arcInfo) {
                const auto& ai = *entity.arcInfo;
                dxf.writeCircle(dw,
                    DL_CircleData(ai.center.x, ai.center.y, 0, ai.radius),
                    attrib);
                return;
            }
            break;
        case DxfEntity::Type::Ellipse:
            if (entity.ellipseInfo) {
                const auto& ei = *entity.ellipseInfo;
                dxf.writeEllipse(dw,
                    DL_EllipseData(ei.center.x, ei.center.y, 0,
                                   ei.majorAxis.x, ei.majorAxis.y, 0,
                                   ei.ratio, ei.startParam, ei.endParam),
                    attrib);
                return;
            }
            break;
        case DxfEntity::Type::Spline:
            if (entity.splineInfo) {
                const auto& sp = *entity.splineInfo;
                const int nKnots = (int)sp.knots.size();
                const int nCtrl  = (int)sp.controlPoints.size();
                const int nFit   = (int)sp.fitPoints.size();
                dxf.writeSpline(dw,
                    DL_SplineData(sp.degree, nKnots, nCtrl, nFit,
                                  sp.closed ? 1 : 0),
                    attrib);
                for (double k : sp.knots)
                    dxf.writeKnot(dw, DL_KnotData(k));
                for (const auto& p : sp.controlPoints)
                    dxf.writeControlPoint(dw, DL_ControlPointData(p.x, p.y, 0, 1.0));
                for (const auto& p : sp.fitPoints)
                    dxf.writeFitPoint(dw, DL_FitPointData(p.x, p.y, 0));
                return;
            }
            break;
        default:
            break;
    }

    // Fallback: tessellate to LWPOLYLINE at export quality.
    for (const auto& outline : entity.exportPath().getOutline()) {
        const auto& verts = outline.getVertices();
        if (verts.size() < 2) continue;
        dxf.writePolyline(dw,
            DL_PolylineData((int)verts.size(), 0, 0, outline.isClosed() ? 1 : 0),
            attrib);
        for (const auto& v : verts)
            dxf.writeVertex(dw, DL_VertexData(v.x, v.y, 0.0));
        dxf.writePolylineEnd(dw);
    }
}

bool ofxDxf::save(const DxfDocument& doc, const std::string& path) {
    DL_Dxf dxf;
    DL_WriterA* dw = dxf.out(path.c_str(), DL_VERSION_2000);
    if (!dw) {
        ofLogError("ofxDxf") << "save: could not open for writing: " << path;
        return false;
    }

    dxf.writeHeader(*dw);
    dw->sectionEnd();

    dw->sectionTables();
    dxf.writeVPort(*dw);

    dw->table("LTYPE", 3);
    dxf.writeLinetype(*dw, DL_LinetypeData("BYBLOCK",     "", 0, 0, 0.0));
    dxf.writeLinetype(*dw, DL_LinetypeData("BYLAYER",     "", 0, 0, 0.0));
    dxf.writeLinetype(*dw, DL_LinetypeData("CONTINUOUS",  "Solid line", 0, 0, 0.0));
    dw->tableEnd();

    int numLayers = 1;
    for (auto& layer : doc.layers) if (layer.name != "0") ++numLayers;
    dw->table("LAYER", numLayers);
    dxf.writeLayer(*dw, DL_LayerData("0", 0),
        DL_Attributes("", DL_Codes::black, -1, "CONTINUOUS", 1.0));
    for (auto& layer : doc.layers) {
        if (layer.name == "0") continue;
        dxf.writeLayer(*dw, DL_LayerData(layer.name, 0),
            DL_Attributes("", DL_Codes::bylayer, -1, "CONTINUOUS", 1.0));
    }
    dw->tableEnd();

    dw->table("STYLE", 1);
    dxf.writeStyle(*dw, DL_StyleData("Standard", 0, 2.5, 1.0, 0.0, 0, 2.5, "", ""));
    dw->tableEnd();

    dxf.writeView(*dw);
    dxf.writeUcs(*dw);

    dw->table("APPID", 1);
    dxf.writeAppid(*dw, "ACAD");
    dw->tableEnd();

    dxf.writeDimStyle(*dw, 2.5, 1.25, 0.625, 0.625, 2.5);
    dxf.writeBlockRecord(*dw);
    dw->tableEnd();
    dw->sectionEnd();

    dw->sectionBlocks();
    dxf.writeBlock(*dw,    DL_BlockData("*Model_Space",  0, 0.0, 0.0, 0.0));
    dxf.writeEndBlock(*dw, "*Model_Space");
    dxf.writeBlock(*dw,    DL_BlockData("*Paper_Space",  0, 0.0, 0.0, 0.0));
    dxf.writeEndBlock(*dw, "*Paper_Space");
    dw->sectionEnd();

    dw->sectionEntities();
    for (auto& layer : doc.layers) {
        DL_Attributes attrib(layer.name, DL_Codes::bylayer, -1, "BYLAYER", 1.0);
        for (auto& entity : layer.entities)
            writeEntity(dxf, *dw, entity, attrib);
    }
    dw->sectionEnd();

    dxf.writeObjects(*dw);
    dxf.writeObjectsEnd(*dw);
    dw->dxfEOF();
    dw->close();
    delete dw;
    return true;
}
