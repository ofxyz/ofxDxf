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
#include <numeric>
#include <sstream>

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

static float polylineLength(const std::vector<glm::vec2>& pts, bool closed);

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
    segs = std::max(segs, 4);

    if (!sp.fitPoints.empty()) {
        const auto& pts = sp.fitPoints;
        if (pts.size() < 2) {
            path.moveTo(pts[0].x, pts[0].y);
            return path;
        }

        const float totalLen = polylineLength(pts, sp.closed);
        if (totalLen <= 1e-6f) {
            path.moveTo(pts[0].x, pts[0].y);
            return path;
        }

        const int edgeCount = sp.closed ? (int)pts.size() : (int)pts.size() - 1;
        path.moveTo(pts[0].x, pts[0].y);
        for (int i = 0; i < edgeCount; ++i) {
            const glm::vec2& a = pts[i];
            const glm::vec2& b = pts[(i + 1) % pts.size()];
            const float edgeLen = glm::distance(a, b);
            const int edgeSegs  = std::max(1, int(std::round(float(segs) * edgeLen / totalLen)));
            for (int j = 1; j <= edgeSegs; ++j) {
                const float t = float(j) / float(edgeSegs);
                const glm::vec2 p = glm::mix(a, b, t);
                path.lineTo(p.x, p.y);
            }
        }
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

    const bool ccw = endParam >= startParam;
    double span = endParam - startParam;
    if (ccw) {
        while (span > TWO_PI + 1e-6) {
            endParam -= TWO_PI;
            span = endParam - startParam;
        }
        while (span < 0) {
            endParam += TWO_PI;
            span = endParam - startParam;
        }
    } else {
        while (span < -TWO_PI - 1e-6) {
            endParam += TWO_PI;
            span = endParam - startParam;
        }
    }

    const bool full = ccw ? (std::abs(span - TWO_PI) < 1e-4)
                          : (std::abs(-span - TWO_PI) < 1e-4);

    auto eval = [&](double t) -> glm::vec2 {
        const float lx = majorLen * std::cos(float(t));
        const float ly = minorLen * std::sin(float(t));
        return ei.center + glm::vec2(
            lx * std::cos(majorAngle) - ly * std::sin(majorAngle),
            lx * std::sin(majorAngle) + ly * std::cos(majorAngle));
    };

    const glm::vec2 p0 = eval(startParam);
    path.moveTo(p0.x, p0.y);
    const double absSpan = ccw ? span : -span;
    const int steps = full ? segs : std::max(4, int(float(segs) * float(absSpan / TWO_PI)));
    for (int i = 1; i <= steps; ++i) {
        const double t = ccw
            ? startParam + span * double(i) / double(steps)
            : startParam - absSpan * double(i) / double(steps);
        const glm::vec2 p = eval(t);
        path.lineTo(p.x, p.y);
    }
    if (full) path.close();
    return path;
}

static float ellipseParamSpanDegrees(const DxfEllipseInfo& ei) {
    const bool ccw = ei.endParam >= ei.startParam;
    double span = ei.endParam - ei.startParam;
    if (ccw) {
        while (span > TWO_PI + 1e-6) span -= TWO_PI;
        while (span < 0) span += TWO_PI;
    } else {
        while (span < -TWO_PI - 1e-6) span += TWO_PI;
        span = -span;
    }
    return float(span / TWO_PI * 360.0);
}

static int resolveSegments(int segments);
static int adaptiveSegmentCount(float radiusWorld, float viewZoom, float spanDegrees);
static int displaySegmentsForSpline(const DxfSplineInfo& sp, int baseSegments,
                                    float viewZoom);

static int displaySegmentsForEllipse(const DxfEllipseInfo& ei, int baseSegments,
                                     float viewZoom) {
    const float majorLen = glm::length(ei.majorAxis);
    if (majorLen <= 0.f) return resolveSegments(baseSegments);
    const int res = resolveSegments(baseSegments);
    if (viewZoom <= 0.f || !std::isfinite(viewZoom)) return res;
    return std::max(res, adaptiveSegmentCount(majorLen, viewZoom, ellipseParamSpanDegrees(ei)));
}

static void drawLocalArcOutlines(const DxfArcInfo& ai, float viewZoom);
static void drawPathOutlines(const ofPath& path, int circleRes, int curveRes);

static std::vector<ofPolyline> tessellateEllipseInfoOutline(const DxfEllipseInfo& ei,
                                                            int segs) {
    DxfEllipseInfo local = ei;
    local.center = {0, 0};
    const ofPath localPath = pathFromEllipse(local, segs);

    std::vector<ofPolyline> out;
    for (auto outline : localPath.getOutline()) {
        for (auto& v : outline.getVertices()) {
            v.x += ei.center.x;
            v.y += ei.center.y;
        }
        if (outline.size() >= 2) out.push_back(std::move(outline));
    }
    return out;
}

static void drawLocalEllipseOutlines(const DxfEllipseInfo& ei, float viewZoom,
                                     int baseSegments) {
    const float majorLen = glm::length(ei.majorAxis);
    if (majorLen <= 0.f) return;

    const int segs = displaySegmentsForEllipse(ei, baseSegments, viewZoom);

    ofPushMatrix();
    ofTranslate(ei.center.x, ei.center.y);

    DxfEllipseInfo local = ei;
    local.center = {0, 0};
    drawPathOutlines(pathFromEllipse(local, segs), segs, segs);

    ofPopMatrix();
}

static void drawLocalEllipticalArcCommand(const ofPath::Command& cmd, float viewZoom) {
    const float rx = cmd.radiusX;
    const float ry = cmd.radiusY;
    if (rx <= 0.f || ry <= 0.f) return;

    float a1 = cmd.angleBegin;
    float a2 = cmd.angleEnd;
    const bool ccw = (cmd.type == ofPath::Command::arc);
    if (ccw) {
        while (a2 < a1) a2 += 360.f;
    } else {
        while (a2 > a1) a2 -= 360.f;
    }
    if (std::abs(a2 - a1) < 1e-4f) return;

    const int segs = adaptiveSegmentCount(std::max(rx, ry), viewZoom, std::abs(a2 - a1));

    ofPushMatrix();
    ofTranslate(cmd.to.x, cmd.to.y);

    ofPolyline pl;
    for (int i = 0; i <= segs; ++i) {
        const float t = a1 + (a2 - a1) * float(i) / float(segs);
        const float rad = t * DEG2RAD;
        pl.addVertex(glm::vec3(rx * std::cos(rad), ry * std::sin(rad), 0.f));
    }
    pl.draw();

    ofPopMatrix();
}

static glm::vec2 ellipticalArcEndPoint(const ofPath::Command& cmd) {
    float a2 = cmd.angleEnd;
    if (cmd.type == ofPath::Command::arc) {
        while (a2 < cmd.angleBegin) a2 += 360.f;
    } else {
        while (a2 > cmd.angleBegin) a2 -= 360.f;
    }
    const float rad = a2 * DEG2RAD;
    return glm::vec2(cmd.to.x, cmd.to.y)
        + glm::vec2(cmd.radiusX * std::cos(rad), cmd.radiusY * std::sin(rad));
}

static int adaptiveSegmentCount(float radiusWorld, float viewZoom, float spanDegrees = 360.f) {
    if (radiusWorld <= 0.f || viewZoom <= 0.f
        || !std::isfinite(radiusWorld) || !std::isfinite(viewZoom))
        return 64;

    const float arcLengthPx =
        (std::abs(spanDegrees) / 360.f) * TWO_PI * radiusWorld * viewZoom;
    const float kTargetPxPerSeg = 1.5f;
    const int   segs = int(std::ceil(arcLengthPx / kTargetPxPerSeg));
    return std::clamp(segs, 12, 8192);
}

static std::pair<float, float> visibleCircleSpanDegrees(
    const DxfArcInfo& ai, const DxfViewContext& view, float viewZoom) {
    const float halfW = view.viewportW / (2.f * viewZoom);
    const float halfH = view.viewportH / (2.f * viewZoom);
    const glm::vec2 rMin(view.center.x - halfW, view.center.y - halfH);
    const glm::vec2 rMax(view.center.x + halfW, view.center.y + halfH);

    const int n = std::clamp(
        int(std::ceil(TWO_PI * ai.radius * viewZoom / 4.f)), 360, 4096);

    std::vector<bool> vis(n);
    int visCount = 0;
    for (int i = 0; i < n; ++i) {
        const float ang = float(i) / float(n) * 360.f;
        const float rad = ang * DEG2RAD;
        const glm::vec2 p(
            ai.center.x + ai.radius * std::cos(rad),
            ai.center.y + ai.radius * std::sin(rad));
        vis[i] = (p.x >= rMin.x && p.x <= rMax.x && p.y >= rMin.y && p.y <= rMax.y);
        if (vis[i]) ++visCount;
    }

    if (visCount == 0 || visCount >= n * 95 / 100)
        return {0.f, 360.f};

    int bestLen = 0;
    int bestStart = 0;
    for (int start = 0; start < n; ++start) {
        int len = 0;
        for (int j = 0; j < n; ++j) {
            if (vis[(start + j) % n]) ++len;
            else break;
        }
        if (len > bestLen) {
            bestLen = len;
            bestStart = start;
        }
    }

    const float step = 360.f / float(n);
    return {
        float(bestStart) * step - step,
        float(bestStart + bestLen) * step + step
    };
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

static bool pathHasArcCommands(const ofPath& path) {
    for (const auto& cmd : path.getCommands()) {
        if (cmd.type == ofPath::Command::arc || cmd.type == ofPath::Command::arcNegative)
            return true;
    }
    return false;
}

static std::optional<DxfEntity> tryPromoteCircleFromClosedPolyline(
    const ofPath& path, const DxfCurvePromotionSettings& settings) {
    if (!settings.promoteFacetedCircles) return std::nullopt;
    if (pathHasArcCommands(path)) return std::nullopt;

    std::vector<glm::vec2> pts;
    for (const auto& cmd : path.getCommands()) {
        if (cmd.type == ofPath::Command::moveTo || cmd.type == ofPath::Command::lineTo)
            pts.push_back(cmd.to);
    }
    if (pts.size() < 2) return std::nullopt;
    if (glm::distance(pts.front(), pts.back()) < 1e-4f)
        pts.pop_back();
    if (pts.size() < (size_t)settings.facetedCircleMinVertices) return std::nullopt;

    glm::vec2 center {0, 0};
    for (const auto& p : pts) center += p;
    center /= float(pts.size());

    float avgR = 0.f;
    for (const auto& p : pts) avgR += glm::distance(p, center);
    avgR /= float(pts.size());
    if (avgR <= 1e-4f) return std::nullopt;

    float maxErr = 0.f;
    for (const auto& p : pts)
        maxErr = std::max(maxErr, std::abs(glm::distance(p, center) - avgR));

    const float relErr = maxErr / avgR;
    if (relErr > settings.facetedCircleMaxRelativeError && maxErr > 0.1f)
        return std::nullopt;

    std::vector<float> edgeLens;
    edgeLens.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        edgeLens.push_back(glm::distance(a, b));
    }
    const float edgeMean = std::accumulate(edgeLens.begin(), edgeLens.end(), 0.f)
        / float(edgeLens.size());
    if (edgeMean <= 1e-4f) return std::nullopt;
    for (float len : edgeLens) {
        if (std::abs(len - edgeMean) / edgeMean > settings.facetedCircleMaxEdgeVariance)
            return std::nullopt;
    }

    DxfEntity e;
    e.type = DxfEntity::Type::Circle;
    initStrokePath(e.path);

    DxfArcInfo ai;
    ai.center     = center;
    ai.radius     = avgR;
    ai.startAngle = 0;
    ai.endAngle   = 360;
    ai.isCCW      = true;
    e.arcInfo     = ai;
    e.path.circle(center.x, center.y, avgR);
    return e;
}

static bool arcInfoIsFullCircle(const DxfArcInfo& ai) {
    return std::abs(std::abs(ai.endAngle - ai.startAngle) - 360.f) < 0.5f
        || (ai.startAngle == 0.f && ai.endAngle == 360.f);
}

static std::optional<DxfArcInfo> tryPromoteCircleFromArcPath(const ofPath& path) {
    bool     closed  = false;
    bool     hasArc  = false;
    glm::vec2 center {0, 0};
    float    radius  = 0.f;
    float    spanDeg = 0.f;

    for (const auto& cmd : path.getCommands()) {
        switch (cmd.type) {
        case ofPath::Command::moveTo:
            break;
        case ofPath::Command::close:
            closed = true;
            break;
        case ofPath::Command::arc:
        case ofPath::Command::arcNegative: {
            const glm::vec2 c(cmd.to.x, cmd.to.y);
            const float     r = std::max(cmd.radiusX, cmd.radiusY);
            if (r <= 1e-4f) return std::nullopt;
            if (!hasArc) {
                center = c;
                radius = r;
                hasArc = true;
            } else {
                if (glm::distance(c, center) > std::max(0.5f, radius * 0.01f))
                    return std::nullopt;
                if (std::abs(r - radius) / radius > 0.01f)
                    return std::nullopt;
            }
            float a1 = cmd.angleBegin;
            float a2 = cmd.angleEnd;
            if (cmd.type == ofPath::Command::arcNegative) {
                while (a2 > a1) a2 -= 360.f;
                spanDeg += a1 - a2;
            } else {
                while (a2 < a1) a2 += 360.f;
                spanDeg += a2 - a1;
            }
            break;
        }
        default:
            return std::nullopt;
        }
    }

    if (!hasArc || !closed || spanDeg < 359.f) return std::nullopt;

    DxfArcInfo ai;
    ai.center     = center;
    ai.radius     = radius;
    ai.startAngle = 0;
    ai.endAngle   = 360;
    ai.isCCW      = true;
    return ai;
}

static std::optional<DxfArcInfo> arcInfoFromPathCommand(const ofPath::Command& cmd) {
    if (cmd.type != ofPath::Command::arc && cmd.type != ofPath::Command::arcNegative)
        return std::nullopt;

    DxfArcInfo ai;
    ai.center     = glm::vec2(cmd.to.x, cmd.to.y);
    ai.radius     = std::max(cmd.radiusX, cmd.radiusY);
    ai.startAngle = cmd.angleBegin;
    ai.endAngle   = cmd.angleEnd;
    ai.isCCW      = (cmd.type == ofPath::Command::arc);
    return ai;
}

static glm::vec2 arcEndPoint(const DxfArcInfo& ai) {
    float a2 = ai.endAngle;
    if (ai.isCCW) {
        while (a2 < ai.startAngle) a2 += 360.f;
    } else {
        while (a2 > ai.startAngle) a2 -= 360.f;
    }
    const float rad = a2 * DEG2RAD;
    return ai.center + glm::vec2(ai.radius * std::cos(rad), ai.radius * std::sin(rad));
}

/// Pure bulge-arc entity: moveTo + one arc (optional close), no chord lines.
static std::optional<DxfArcInfo> tryExtractSingleArcFromPath(const ofPath& path) {
    std::optional<DxfArcInfo> ai;
    for (const auto& cmd : path.getCommands()) {
        switch (cmd.type) {
        case ofPath::Command::moveTo:
        case ofPath::Command::close:
            break;
        case ofPath::Command::arc:
        case ofPath::Command::arcNegative: {
            if (ai) return std::nullopt;
            ai = arcInfoFromPathCommand(cmd);
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return ai;
}

static bool isUniformScale(float sx, float sy) {
    return std::abs(std::abs(sx) - std::abs(sy))
        <= 1e-4f * std::max({std::abs(sx), std::abs(sy), 1.f});
}

static ofPath pathFromArcInfo(const DxfArcInfo& ai, DxfEntity::Type type) {
    ofPath p;
    initStrokePath(p);
    if (type == DxfEntity::Type::Circle || arcInfoIsFullCircle(ai))
        p.circle(ai.center.x, ai.center.y, ai.radius);
    else
        addDxfArcToPath(p, ai.center.x, ai.center.y, ai.radius,
                        ai.startAngle, ai.endAngle, ai.isCCW);
    return p;
}

static void applyPromotedCurve(DxfEntity& e, DxfEntity::Type type,
                               ofPath path, DxfArcInfo ai) {
    e.type    = type;
    e.path    = std::move(path);
    e.arcInfo = std::move(ai);
}

enum class DxfCurvePromotionKind { None, Circle, Arc };

static DxfCurvePromotionKind promoteEntityCurvesImpl(
    DxfEntity& e, const DxfCurvePromotionSettings& settings) {
    if (e.arcInfo) return DxfCurvePromotionKind::None;
    if (e.type != DxfEntity::Type::Polyline) return DxfCurvePromotionKind::None;

    if (settings.promoteFacetedCircles) {
        if (auto promoted = tryPromoteCircleFromClosedPolyline(e.path, settings)) {
            if (promoted->arcInfo) {
                applyPromotedCurve(e, promoted->type,
                    std::move(promoted->path), *promoted->arcInfo);
                return DxfCurvePromotionKind::Circle;
            }
        }
    }

    if (settings.promoteBulgeArcCircles) {
        if (auto ai = tryPromoteCircleFromArcPath(e.path)) {
            applyPromotedCurve(e, DxfEntity::Type::Circle,
                pathFromArcInfo(*ai, DxfEntity::Type::Circle), *ai);
            return DxfCurvePromotionKind::Circle;
        }
    }

    if (settings.promoteBulgeArcs) {
        if (auto ai = tryExtractSingleArcFromPath(e.path)) {
            applyPromotedCurve(e, DxfEntity::Type::Arc,
                pathFromArcInfo(*ai, DxfEntity::Type::Arc), *ai);
            return DxfCurvePromotionKind::Arc;
        }
    }

    return DxfCurvePromotionKind::None;
}

static std::vector<ofPolyline> tessellateArcInfoOutline(const DxfArcInfo& ai, int segs) {
    ofPath p;
    initStrokePath(p);
    if (arcInfoIsFullCircle(ai))
        p.circle(0.f, 0.f, ai.radius);
    else
        addDxfArcToPath(p, 0.f, 0.f, ai.radius, ai.startAngle, ai.endAngle, ai.isCCW);

    p.setCircleResolution(segs);
    p.setCurveResolution(segs);

    std::vector<ofPolyline> out;
    for (auto outline : p.getOutline()) {
        for (auto& v : outline.getVertices()) {
            v.x += ai.center.x;
            v.y += ai.center.y;
        }
        if (outline.size() >= 2) out.push_back(std::move(outline));
    }
    return out;
}

static std::vector<ofPolyline> tessellatePathWithLocalArcs(const ofPath& path, int segs) {
    std::vector<ofPolyline> out;
    glm::vec2 cur {0, 0};
    glm::vec2 subStart {0, 0};
    bool hasCur = false;

    auto addLine = [&](const glm::vec2& a, const glm::vec2& b) {
        ofPolyline pl;
        pl.addVertex(glm::vec3(a.x, a.y, 0.f));
        pl.addVertex(glm::vec3(b.x, b.y, 0.f));
        out.push_back(std::move(pl));
    };

    for (const auto& cmd : path.getCommands()) {
        switch (cmd.type) {
        case ofPath::Command::moveTo:
            cur      = glm::vec2(cmd.to.x, cmd.to.y);
            subStart = cur;
            hasCur   = true;
            break;
        case ofPath::Command::lineTo: {
            const glm::vec2 next(cmd.to.x, cmd.to.y);
            if (hasCur) addLine(cur, next);
            cur    = next;
            hasCur = true;
            break;
        }
        case ofPath::Command::arc:
        case ofPath::Command::arcNegative: {
            const float rx = cmd.radiusX;
            const float ry = cmd.radiusY;
            if (std::abs(rx - ry) > 1e-4f * std::max(rx, ry)) {
                ofPolyline pl;
                float a1 = cmd.angleBegin;
                float a2 = cmd.angleEnd;
                const bool ccw = (cmd.type == ofPath::Command::arc);
                if (ccw) { while (a2 < a1) a2 += 360.f; }
                else { while (a2 > a1) a2 -= 360.f; }
                const int arcSegs = std::max(4, int(float(segs) * std::abs(a2 - a1) / 360.f));
                for (int i = 0; i <= arcSegs; ++i) {
                    const float t = a1 + (a2 - a1) * float(i) / float(arcSegs);
                    const float rad = t * DEG2RAD;
                    pl.addVertex(glm::vec3(
                        cmd.to.x + rx * std::cos(rad),
                        cmd.to.y + ry * std::sin(rad), 0.f));
                }
                if (pl.size() >= 2) out.push_back(std::move(pl));
                cur    = ellipticalArcEndPoint(cmd);
                hasCur = true;
            } else if (auto ai = arcInfoFromPathCommand(cmd)) {
                auto arcOutlines = tessellateArcInfoOutline(*ai, segs);
                out.insert(out.end(), arcOutlines.begin(), arcOutlines.end());
                cur    = arcEndPoint(*ai);
                hasCur = true;
            }
            break;
        }
        case ofPath::Command::close:
            if (hasCur) addLine(cur, subStart);
            break;
        default:
            break;
        }
    }
    return out;
}

static void drawLocalPathOutlines(const ofPath& path, float viewZoom) {
    glm::vec2 cur {0, 0};
    glm::vec2 subStart {0, 0};
    bool hasCur = false;

    for (const auto& cmd : path.getCommands()) {
        switch (cmd.type) {
        case ofPath::Command::moveTo:
            cur      = glm::vec2(cmd.to.x, cmd.to.y);
            subStart = cur;
            hasCur   = true;
            break;
        case ofPath::Command::lineTo: {
            const glm::vec2 next(cmd.to.x, cmd.to.y);
            if (hasCur) ofDrawLine(cur.x, cur.y, next.x, next.y);
            cur    = next;
            hasCur = true;
            break;
        }
        case ofPath::Command::arc:
        case ofPath::Command::arcNegative: {
            const float rx = cmd.radiusX;
            const float ry = cmd.radiusY;
            if (std::abs(rx - ry) > 1e-4f * std::max(rx, ry)) {
                drawLocalEllipticalArcCommand(cmd, viewZoom);
                cur    = ellipticalArcEndPoint(cmd);
                hasCur = true;
            } else if (auto ai = arcInfoFromPathCommand(cmd)) {
                drawLocalArcOutlines(*ai, viewZoom);
                cur    = arcEndPoint(*ai);
                hasCur = true;
            }
            break;
        }
        case ofPath::Command::close:
            if (hasCur) ofDrawLine(cur.x, cur.y, subStart.x, subStart.y);
            break;
        default:
            break;
        }
    }
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

static void drawLocalCircleOutlines(const DxfArcInfo& ai, float viewZoom,
                                    const DxfViewContext* view = nullptr) {
    if (ai.radius <= 0.f) return;

    float a1 = 0.f;
    float a2 = 360.f;
    if (view && view->isValid()) {
        const auto span = visibleCircleSpanDegrees(ai, *view, viewZoom);
        a1 = span.first;
        a2 = span.second;
        if (a2 < a1) a2 += 360.f;
    }

    if (a2 - a1 < 359.9f) {
        DxfArcInfo partial = ai;
        partial.startAngle = a1;
        partial.endAngle   = a2;
        partial.isCCW      = true;
        drawLocalArcOutlines(partial, viewZoom);
        return;
    }

    const int segs = adaptiveSegmentCount(ai.radius, viewZoom, 360.f);

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
    if (viewZoom <= 0.f || !std::isfinite(viewZoom)) return res;

    if (e.arcInfo && e.arcInfo->radius > 0.f) {
        const float span = arcInfoIsFullCircle(*e.arcInfo)
            ? 360.f
            : std::abs(e.arcInfo->endAngle - e.arcInfo->startAngle);
        res = std::max(res, adaptiveSegmentCount(e.arcInfo->radius, viewZoom, span));
    }

    if (e.ellipseInfo && glm::length(e.ellipseInfo->majorAxis) > 0.f) {
        res = std::max(res, displaySegmentsForEllipse(*e.ellipseInfo, baseSegments, viewZoom));
    }

    if (e.splineInfo) {
        res = std::max(res, displaySegmentsForSpline(*e.splineInfo, baseSegments, viewZoom));
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

static float estimateSplineLength(const DxfSplineInfo& sp) {
    if (!sp.fitPoints.empty())
        return polylineLength(sp.fitPoints, sp.closed);
    if (sp.controlPoints.size() >= 2)
        return polylineLength(sp.controlPoints, sp.closed) * 1.15f;
    return 0.f;
}

static glm::vec2 splineLocalOrigin(const DxfSplineInfo& sp) {
    const auto& pts = !sp.fitPoints.empty() ? sp.fitPoints : sp.controlPoints;
    if (pts.empty()) return {0, 0};
    glm::vec2 c {0, 0};
    for (const auto& p : pts) c += p;
    return c / float(pts.size());
}

static DxfSplineInfo offsetSplineInfo(const DxfSplineInfo& sp, const glm::vec2& origin) {
    DxfSplineInfo local = sp;
    for (auto& p : local.controlPoints) p -= origin;
    for (auto& p : local.fitPoints) p -= origin;
    return local;
}

static int displaySegmentsForSpline(const DxfSplineInfo& sp, int baseSegments,
                                    float viewZoom) {
    const int res = resolveSegments(baseSegments);
    const float len = estimateSplineLength(sp);
    if (len <= 0.f || viewZoom <= 0.f || !std::isfinite(viewZoom)) return res;

    const float kTargetPxPerSeg = 1.5f;
    const int adaptive = int(std::ceil(len * viewZoom / kTargetPxPerSeg));
    return std::clamp(std::max(res, adaptive), 12, 8192);
}

static std::vector<ofPolyline> tessellateSplineInfoOutline(const DxfSplineInfo& sp,
                                                           int segs) {
    const glm::vec2 origin = splineLocalOrigin(sp);
    const ofPath localPath = pathFromSpline(offsetSplineInfo(sp, origin), segs);

    std::vector<ofPolyline> out;
    for (auto outline : localPath.getOutline()) {
        for (auto& v : outline.getVertices()) {
            v.x += origin.x;
            v.y += origin.y;
        }
        if (outline.size() >= 2) out.push_back(std::move(outline));
    }
    return out;
}

static void drawLocalSplineOutlines(const DxfSplineInfo& sp, float viewZoom,
                                    int baseSegments) {
    const glm::vec2 origin = splineLocalOrigin(sp);
    const int segs = displaySegmentsForSpline(sp, baseSegments, viewZoom);

    ofPushMatrix();
    ofTranslate(origin.x, origin.y);
    drawPathOutlines(pathFromSpline(offsetSplineInfo(sp, origin), segs), segs, segs);
    ofPopMatrix();
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
    if (e.ellipseInfo) return pathFromEllipse(*e.ellipseInfo, res);

    ofPath p = e.arcInfo ? pathFromArcInfo(*e.arcInfo, e.type) : e.path;
    p.setCircleResolution(res);
    p.setCurveResolution(res);
    return p;
}

static ofPath entityExportPath(const DxfEntity& e, int segments) {
    if (e.splineInfo) {
        const int segs = segments > 0 ? segments : exportSegmentCountForSpline(*e.splineInfo);
        return pathFromSpline(*e.splineInfo, segs);
    }
    if (e.ellipseInfo) {
        const int segs = segments > 0 ? segments : exportSegmentCountForEllipse(*e.ellipseInfo);
        return pathFromEllipse(*e.ellipseInfo, segs);
    }
    if (e.arcInfo)
        return pathFromArcInfo(*e.arcInfo, e.type);

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

static glm::vec2 pointOnCircleArc(const DxfArcInfo& ai, float angleDeg) {
    const float rad = angleDeg * DEG2RAD;
    return ai.center + ai.radius * glm::vec2(std::cos(rad), std::sin(rad));
}

/// Circle/arc + non-uniform INSERT scale → exact DXF ELLIPSE metadata.
static std::optional<DxfEllipseInfo> ellipseInfoFromArcAfterNonUniformInsert(
    const DxfArcInfo& ai, DxfEntity::Type type,
    const glm::vec2& blockBase, const glm::vec2& insert,
    float sx, float sy, float angleDeg) {
    if (ai.radius <= 1e-6f) return std::nullopt;

    const glm::vec2 center = transformPoint2D(ai.center, blockBase, insert, sx, sy, angleDeg);

    const float rot = glm::radians(angleDeg);
    const float c   = std::cos(rot);
    const float s   = std::sin(rot);
    const auto linTransform = [&](glm::vec2 v) -> glm::vec2 {
        v.x *= sx;
        v.y *= sy;
        return glm::vec2(c * v.x - s * v.y, s * v.x + c * v.y);
    };

    const glm::vec2 axisX = linTransform(glm::vec2(ai.radius, 0.f));
    const glm::vec2 axisY = linTransform(glm::vec2(0.f, ai.radius));
    const float     lenX  = glm::length(axisX);
    const float     lenY  = glm::length(axisY);
    if (lenX <= 1e-6f && lenY <= 1e-6f) return std::nullopt;

    DxfEllipseInfo ei;
    ei.center = center;
    if (lenX >= lenY) {
        ei.majorAxis = axisX;
        ei.ratio     = (lenY <= 1e-6f) ? 1.f : lenY / lenX;
    } else {
        ei.majorAxis = axisY;
        ei.ratio     = (lenX <= 1e-6f) ? 1.f : lenX / lenY;
    }

    const float majorLen = glm::length(ei.majorAxis);
    if (majorLen <= 1e-6f) return std::nullopt;
    const glm::vec2 u        = ei.majorAxis / majorLen;
    const glm::vec2 vPerp(-u.y, u.x);
    const float     minorLen = majorLen * ei.ratio;

    const auto paramFromWorldPoint = [&](glm::vec2 p) -> double {
        const glm::vec2 d  = p - center;
        const float     lx = glm::dot(d, u);
        const float     ly = glm::dot(d, vPerp);
        return std::atan2(double(ly) / double(minorLen), double(lx) / double(majorLen));
    };

    if (type == DxfEntity::Type::Circle || arcInfoIsFullCircle(ai)) {
        ei.startParam = 0.0;
        ei.endParam   = TWO_PI;
        return ei;
    }

    float a1 = ai.startAngle;
    float a2 = ai.endAngle;
    if (ai.isCCW) {
        while (a2 < a1) a2 += 360.f;
    } else {
        while (a2 > a1) a2 -= 360.f;
    }

    ei.startParam = paramFromWorldPoint(
        transformPoint2D(pointOnCircleArc(ai, a1), blockBase, insert, sx, sy, angleDeg));
    ei.endParam = paramFromWorldPoint(
        transformPoint2D(pointOnCircleArc(ai, a2), blockBase, insert, sx, sy, angleDeg));

    if (ai.isCCW) {
        while (ei.endParam < ei.startParam) ei.endParam += TWO_PI;
    } else {
        while (ei.endParam > ei.startParam) ei.endParam -= TWO_PI;
    }

    return ei;
}

static DxfEntity transformEntityForInsert(const DxfEntity& src, const glm::vec2& blockBase,
                                          const glm::vec2& insert, float sx, float sy,
                                          float angleDeg) {
    DxfEntity e = src;
    applyInsertTransform2D(e.path, blockBase, insert, sx, sy, angleDeg);

    const bool uniform = isUniformScale(sx, sy);

    if (e.arcInfo
        && (e.type == DxfEntity::Type::Circle || e.type == DxfEntity::Type::Arc)) {
        if (!uniform) {
            if (auto ei = ellipseInfoFromArcAfterNonUniformInsert(
                    *e.arcInfo, e.type, blockBase, insert, sx, sy, angleDeg)) {
                e.type = DxfEntity::Type::Ellipse;
                e.arcInfo.reset();
                e.ellipseInfo = std::move(*ei);
                initStrokePath(e.path);
            } else {
                e.arcInfo.reset();
                e.type = DxfEntity::Type::Polyline;
            }
        } else {
            auto& ai = *e.arcInfo;
            ai.center      = transformPoint2D(ai.center, blockBase, insert, sx, sy, angleDeg);
            ai.radius     *= std::max(std::abs(sx), std::abs(sy));
            ai.startAngle += angleDeg;
            ai.endAngle   += angleDeg;
            e.path = pathFromArcInfo(ai, e.type);
        }
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

static DxfEntity makePolylineEntity(ofPath path) {
    DxfEntity e;
    e.type = DxfEntity::Type::Polyline;
    e.path = std::move(path);
    return e;
}

static DxfEntity makeClosedPolylineFromPoints(const std::vector<glm::vec2>& pts) {
    ofPath path;
    initStrokePath(path);
    if (pts.size() < 2) return {};
    path.moveTo(pts[0].x, pts[0].y);
    for (size_t i = 1; i < pts.size(); ++i)
        path.lineTo(pts[i].x, pts[i].y);
    if (pts.size() >= 3) path.close();
    return makePolylineEntity(std::move(path));
}

static DxfEntity makeEntityFromTraceLike(const DL_TraceData& d) {
    std::vector<glm::vec2> pts;
    pts.reserve(4);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 p(float(d.x[i]), float(d.y[i]));
        if (pts.empty() || glm::distance(p, pts.back()) > 1e-5f)
            pts.push_back(p);
    }
    if (pts.size() >= 2 && glm::distance(pts.front(), pts.back()) < 1e-5f)
        pts.pop_back();
    return makeClosedPolylineFromPoints(pts);
}

static bool appendHatchEdgeToPath(const DL_HatchEdgeData& edge, ofPath& path) {
    if (!edge.defined && edge.type != 0) return false;

    switch (edge.type) {
    case 0: {
        if (edge.vertices.size() < 2) return false;
        if (path.getCommands().empty())
            path.moveTo(float(edge.vertices[0][0]), float(edge.vertices[0][1]));
        for (size_t i = 0; i + 1 < edge.vertices.size(); ++i) {
            const glm::vec2 p1(float(edge.vertices[i][0]), float(edge.vertices[i][1]));
            const glm::vec2 p2(float(edge.vertices[i + 1][0]), float(edge.vertices[i + 1][1]));
            const double bulge = (edge.vertices[i].size() >= 3) ? edge.vertices[i][2] : 0.0;
            if (std::abs(bulge) > 1e-10)
                appendBulgeArcToPath(path, p1, p2, bulge);
            else
                path.lineTo(p2.x, p2.y);
        }
        return true;
    }
    case 1:
        if (path.getCommands().empty())
            path.moveTo(float(edge.x1), float(edge.y1));
        path.lineTo(float(edge.x2), float(edge.y2));
        return true;
    case 2: {
        const float a1 = float(edge.angle1 * RAD2DEG);
        const float a2 = float(edge.angle2 * RAD2DEG);
        addDxfArcToPath(path, float(edge.cx), float(edge.cy), float(edge.radius),
                        a1, a2, edge.ccw);
        return true;
    }
    case 3: {
        DxfEllipseInfo ei;
        ei.center     = glm::vec2(float(edge.cx), float(edge.cy));
        ei.majorAxis  = glm::vec2(float(edge.mx), float(edge.my));
        ei.ratio      = float(edge.ratio);
        ei.startParam = edge.angle1;
        ei.endParam   = edge.angle2;
        ofPath ep = pathFromEllipse(ei, boundsSegmentCount());
        if (path.getCommands().empty()) {
            path = ep;
            return !path.getCommands().empty();
        }
        for (const auto& outline : ep.getOutline()) {
            for (const auto& v : outline.getVertices())
                path.lineTo(v.x, v.y);
        }
        return true;
    }
    case 4:
        if (edge.controlPoints.size() >= 2) {
            if (path.getCommands().empty())
                path.moveTo(float(edge.controlPoints[0][0]), float(edge.controlPoints[0][1]));
            for (size_t i = 1; i < edge.controlPoints.size(); ++i)
                path.lineTo(float(edge.controlPoints[i][0]), float(edge.controlPoints[i][1]));
            return true;
        }
        return false;
    default:
        return false;
    }
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

    auto absorbPolylines = [&](const std::vector<ofPolyline>& pls) {
        for (const auto& pl : pls)
            for (const auto& v : pl.getVertices())
                expandBounds(b, v.x, v.y, first);
    };

    if (e.arcInfo) {
        absorbPolylines(tessellateArcInfoOutline(*e.arcInfo, boundsSegmentCount()));
    } else if (e.ellipseInfo) {
        absorbPolylines(tessellateEllipseInfoOutline(
            *e.ellipseInfo, displaySegmentsForEllipse(*e.ellipseInfo, -1, 1.f)));
    } else if (e.splineInfo) {
        absorbPolylines(tessellateSplineInfoOutline(
            *e.splineInfo, displaySegmentsForSpline(*e.splineInfo, -1, 1.f)));
    } else if (pathHasArcCommands(e.path)) {
        absorbPolylines(tessellatePathWithLocalArcs(e.path, boundsSegmentCount()));
    } else {
        const ofPath drawPath = entityDrawPath(e, boundsSegmentCount());
        for (const auto& outline : drawPath.getOutline())
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
    const int res = resolveSegments(segments);
    if (arcInfo)
        return tessellateArcInfoOutline(*arcInfo, res);
    if (ellipseInfo) {
        return tessellateEllipseInfoOutline(*ellipseInfo,
            displaySegmentsForEllipse(*ellipseInfo, segments, 1.f));
    }
    if (splineInfo)
        return tessellateSplineInfoOutline(*splineInfo,
            displaySegmentsForSpline(*splineInfo, segments, 1.f));
    if (pathHasArcCommands(path))
        return tessellatePathWithLocalArcs(path, res);
    return entityDrawPath(*this, res).getOutline();
}

ofPath DxfEntity::resolvedPath(int segments) const {
    return entityDrawPath(*this, segments);
}

ofPath DxfEntity::exportPath(int segments) const {
    return entityExportPath(*this, segments);
}

void DxfEntity::draw(float viewZoom, int segments, const DxfViewContext* view) const {
    if (type == Type::Point)
        return;

    // Stored CIRCLE/ARC metadata, or native entities from the file.
    if (arcInfo) {
        if (arcInfoIsFullCircle(*arcInfo))
            drawLocalCircleOutlines(*arcInfo, viewZoom, view);
        else
            drawLocalArcOutlines(*arcInfo, viewZoom);
        return;
    }

    if (ellipseInfo) {
        drawLocalEllipseOutlines(*ellipseInfo, viewZoom, segments);
        return;
    }

    if (splineInfo) {
        drawLocalSplineOutlines(*splineInfo, viewZoom, segments);
        return;
    }

    if (pathHasArcCommands(path)) {
        drawLocalPathOutlines(path, viewZoom);
        return;
    }

    // Lines and polylines — no circular arcs to localize.
    const int res = displayResolutionForEntity(*this, segments, viewZoom);
    drawPathOutlines(entityDrawPath(*this, res), res, res);
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
        if (m_hatchActive) {
            flushHatchEntities();
            m_hatchActive = false;
            return;
        }

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

    void addTrace(const DL_TraceData& d) override {
        if (auto e = makeEntityFromTraceLike(d); !e.path.getCommands().empty())
            pushEntity(std::move(e));
    }

    void addSolid(const DL_SolidData& d) override { addTrace(d); }

    void add3dFace(const DL_3dFaceData& d) override { addTrace(d); }

    void addHatch(const DL_HatchData& d) override {
        (void)d;
        m_hatchActive = true;
        m_hatchLoopPaths.clear();
    }

    void addHatchLoop(const DL_HatchLoopData& d) override {
        (void)d;
        m_hatchLoopPaths.emplace_back();
        initStrokePath(m_hatchLoopPaths.back());
    }

    void addHatchEdge(const DL_HatchEdgeData& d) override {
        if (m_hatchLoopPaths.empty()) {
            m_hatchLoopPaths.emplace_back();
            initStrokePath(m_hatchLoopPaths.back());
        }
        if (!appendHatchEdgeToPath(d, m_hatchLoopPaths.back()))
            noteSkipped("HATCH_EDGE");
    }

    // ── Skipped entity logging ───────────────────────────────────────────────
    void addXLine(const DL_XLineData&) override { noteSkipped("XLINE"); }
    void addRay(const DL_RayData&) override { noteSkipped("RAY"); }
    void addText(const DL_TextData&) override { noteSkipped("TEXT"); }
    void addMText(const DL_MTextData&) override { noteSkipped("MTEXT"); }
    void addArcAlignedText(const DL_ArcAlignedTextData&) override { noteSkipped("ARCALIGNEDTEXT"); }
    void addAttribute(const DL_AttributeData&) override { noteSkipped("ATTRIB"); }
    void addDimAlign(const DL_DimensionData&, const DL_DimAlignedData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimLinear(const DL_DimensionData&, const DL_DimLinearData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimRadial(const DL_DimensionData&, const DL_DimRadialData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimDiametric(const DL_DimensionData&, const DL_DimDiametricData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimAngular(const DL_DimensionData&, const DL_DimAngular2LData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimAngular3P(const DL_DimensionData&, const DL_DimAngular3PData&) override {
        noteSkipped("DIMENSION");
    }
    void addDimOrdinate(const DL_DimensionData&, const DL_DimOrdinateData&) override {
        noteSkipped("DIMENSION");
    }
    void addLeader(const DL_LeaderData&) override { noteSkipped("LEADER"); }
    void addLeaderVertex(const DL_LeaderVertexData&) override {}
    void addImage(const DL_ImageData&) override { noteSkipped("IMAGE"); }
    void linkImage(const DL_ImageDefData&) override {}

    std::vector<DxfLayer> takeLayers() {
        flushSplineIfPending();
        flushPendingInserts();
        logLoadSummary();
        return std::move(m_layers);
    }

    std::string takeLoadSummary() {
        std::string s = m_loadSummary;
        m_loadSummary.clear();
        return s;
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

    bool                       m_hatchActive = false;
    std::vector<ofPath>        m_hatchLoopPaths;

    struct PendingInsert {
        DL_InsertData data;

        explicit PendingInsert(DL_InsertData insertData)
            : data(std::move(insertData)) {}
    };
    std::vector<PendingInsert>   m_pendingInserts;
    std::map<std::string, int>   m_skipped;
    int                          m_deferredInsertResolved = 0;
    int                          m_deferredInsertFailed   = 0;
    std::string                  m_loadSummary;

    void noteSkipped(const char* key) { m_skipped[key]++; }

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
        pushEntityToTarget(std::move(e));
    }

    void pushEntityToTarget(DxfEntity&& e) {
        if (m_collectingBlock && !m_activeBlock.empty()) {
            m_blocks[m_activeBlock].entities.push_back(std::move(e));
            return;
        }
        ensureLayer(e.layer).entities.push_back(std::move(e));
    }

    void flushHatchEntities() {
        for (auto& loopPath : m_hatchLoopPaths) {
            if (loopPath.getCommands().size() < 2) continue;
            loopPath.close();
            pushEntity(makePolylineEntity(std::move(loopPath)));
        }
        m_hatchLoopPaths.clear();
    }

    void expandInsert(const DL_InsertData& d, int depth) {
        if (d.name.empty() || depth > kMaxInsertDepth) return;

        const auto it = m_blocks.find(d.name);
        if (it == m_blocks.end()) {
            m_pendingInserts.emplace_back(d);
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
                    if (expanded.layer.empty()) expanded.layer = "0";
                    pushEntityToTarget(std::move(expanded));
                }
            }
        }
    }

    void flushPendingInserts() {
        if (m_pendingInserts.empty()) return;

        int passes = 0;
        bool progress = true;
        while (progress && passes < 32 && !m_pendingInserts.empty()) {
            progress = false;
            ++passes;
            auto queue = std::move(m_pendingInserts);
            m_pendingInserts.clear();

            for (auto& pending : queue) {
                if (m_blocks.count(pending.data.name)) {
                    expandInsert(pending.data, 0);
                    ++m_deferredInsertResolved;
                    progress = true;
                } else {
                    m_pendingInserts.push_back(std::move(pending));
                }
            }
        }

        m_deferredInsertFailed += (int)m_pendingInserts.size();
        for (const auto& pending : m_pendingInserts)
            ofLogWarning("ofxDxf") << "INSERT references unknown block '"
                                   << pending.data.name << "'";
        m_pendingInserts.clear();
    }

    void logLoadSummary() {
        std::stringstream ss;
        bool any = false;

        if (m_deferredInsertResolved > 0) {
            ss << "deferred INSERT resolved=" << m_deferredInsertResolved << " ";
            any = true;
        }
        if (m_deferredInsertFailed > 0) {
            ss << "INSERT failed=" << m_deferredInsertFailed << " ";
            any = true;
        }
        for (const auto& [key, count] : m_skipped) {
            if (count <= 0) continue;
            ss << key << "=" << count << " ";
            any = true;
        }

        if (!any) return;

        m_loadSummary = ss.str();
        ofLogNotice("ofxDxf") << "Load summary (skipped / unresolved): " << m_loadSummary;
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
    if (const std::string summary = adapter.takeLoadSummary(); !summary.empty())
        doc.errors.push_back("Skipped/unresolved: " + summary);

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

bool ofxDxf::promoteEntityCurves(DxfEntity& entity,
                                 const DxfCurvePromotionSettings& settings) {
    return promoteEntityCurvesImpl(entity, settings) != DxfCurvePromotionKind::None;
}

DxfCurvePromotionResult ofxDxf::promoteCurves(DxfDocument& doc,
                                              const DxfCurvePromotionSettings& settings) {
    DxfCurvePromotionResult result;
    for (auto& layer : doc.layers) {
        for (auto& entity : layer.entities) {
            ++result.entitiesExamined;
            switch (promoteEntityCurvesImpl(entity, settings)) {
            case DxfCurvePromotionKind::Circle: ++result.circlesPromoted; break;
            case DxfCurvePromotionKind::Arc:    ++result.arcsPromoted; break;
            default: break;
            }
        }
    }
    return result;
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
