#include "ofApp.h"

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
const ofColor ofApp::PALETTE[] = {
    ofColor(255, 100, 100),
    ofColor( 80, 200,  80),
    ofColor(100, 150, 255),
    ofColor(255, 210,  60),
    ofColor(200,  80, 255),
    ofColor( 80, 220, 210),
    ofColor(255, 150,  60),
    ofColor(200, 200, 200),
    ofColor(255, 180, 180),
    ofColor(140, 255, 140),
};
const int ofApp::PALETTE_SIZE = 10;

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::setup() {
    ofSetWindowTitle("DXF ↔ SVG Converter  |  ofxDxf");
    ofBackground(28, 28, 35);
    ofSetFrameRate(60);

    setupImGui();

    std::string def = ofToDataPath("test.dxf");
    if (ofFile(def).exists()) loadDxf("test.dxf");
}

void ofApp::setupImGui() {
    m_gui.setup(nullptr, false);

    if (ImFont* font = ImFonts::LoadDefaultFonts(ImGui::GetIO().Fonts, 15.0f))
        m_gui.setDefaultFont(font);
    m_gui.rebuildFontsTexture();

    ImTheme::Setup(ImTheme::Theme_DarculaDarker);
    m_uiScale = ImTheme::UIScale();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMinSize = ImVec2(160.f * m_uiScale, 50.f * m_uiScale);
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::draw() {
    const float sw = sidebarW();
    float vpW = ofGetWidth() - sw;
    float vpH = float(ofGetHeight());

    ofPushView();
    ofViewport(sw, 0, vpW, vpH);
    ofSetupScreenOrtho(vpW, vpH, -1, 1);

    ofBackground(28, 28, 35);
    if (m_showGrid) drawGrid();
    drawScene();

    ofPopView();

    ofSetColor(50, 50, 62);
    ofDrawRectangle(sw - 1, 0, 1, ofGetHeight());

    m_gui.begin();
    drawViewportOverlay();
    drawSidebar();
    m_gui.end();
    m_gui.draw();
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::drawScene() {
    if (m_doc.empty())
        return;

    ofPushMatrix();
    applyViewTransform();

    for (auto& layer : m_doc.layers) {
        auto& ld = layerDisplay(layer.name);
        if (!ld.visible) continue;

        ofSetColor(
            int(ld.color[0] * 255),
            int(ld.color[1] * 255),
            int(ld.color[2] * 255));
        ofSetLineWidth(std::max(0.5f, ld.strokeWidth * m_zoom));

        for (auto& e : layer.entities)
            drawEntity(e);
    }

    if (!m_doc.bounds.isEmpty()) {
        ofNoFill();
        ofSetColor(60, 60, 80, 120);
        ofSetLineWidth(std::max(0.5f, 1.f / m_zoom));
        ofDrawRectangle(m_doc.bounds);
    }

    ofPopMatrix();
}

void ofApp::drawEntity(const DxfEntity& entity) {
    if (entity.type == DxfEntity::Type::Point) {
        const auto& cmds = entity.path.getCommands();
        if (cmds.empty()) return;
        const glm::vec2 p = cmds[0].to;
        ofFill();
        ofDrawCircle(p.x, p.y, 2.f / m_zoom);
        ofNoFill();
        return;
    }

    entity.draw(m_zoom);
}

void ofApp::clampZoom() {
    if (!std::isfinite(m_zoom) || m_zoom <= 0.f)
        m_zoom = 1.f;
    m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
}

void ofApp::applyViewTransform() const {
    const float vpW = viewportW();
    const float vpH = float(ofGetHeight());
    ofTranslate(vpW * 0.5f, vpH * 0.5f);
    ofScale(m_zoom, m_zoom);
    ofTranslate(-m_viewCenter.x, -m_viewCenter.y);
}

ofRectangle ofApp::fitBounds() const {
    ofRectangle bounds;
    bool first = true;

    for (auto& layer : m_doc.layers) {
        auto it = m_layerDisplay.find(layer.name);
        if (it != m_layerDisplay.end() && !it->second.visible)
            continue;

        for (auto& e : layer.entities) {
            if (e.type == DxfEntity::Type::Point) {
                const auto& cmds = e.path.getCommands();
                if (cmds.empty()) continue;
                const glm::vec2 p = cmds[0].to;
                if (first) { bounds.set(p.x, p.y, 0, 0); first = false; }
                else bounds.growToInclude(p.x, p.y);
                continue;
            }
            for (const auto& outline : e.getOutline()) {
                for (const auto& v : outline.getVertices()) {
                    if (first) { bounds.set(v.x, v.y, 0, 0); first = false; }
                    else bounds.growToInclude(v.x, v.y);
                }
            }
        }
    }
    return first ? m_doc.bounds : bounds;
}

void ofApp::zoomAt(const glm::vec2& viewMouse, float factor) {
    const float vpW = viewportW();
    const float vpH = float(ofGetHeight());
    const glm::vec2 offset = viewMouse - glm::vec2(vpW * 0.5f, vpH * 0.5f);

    const float oldZoom = m_zoom;
    const float newZoom = std::clamp(oldZoom * factor, kMinZoom, kMaxZoom);
    if (newZoom == oldZoom) return;

    const glm::vec2 worldUnderMouse = m_viewCenter + offset / oldZoom;
    m_viewCenter = worldUnderMouse - offset / newZoom;
    m_zoom = newZoom;
}

void ofApp::drawViewportOverlay() {
    const float sw = sidebarW();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont*     font = ImGui::GetFont();
    const float fs   = ImGui::GetFontSize();

    if (m_doc.empty()) {
        const char* msg = "Drop a .dxf or .svg file here, or use Open...";
        dl->AddText(font, fs, ImVec2(sw + 20.f, 30.f), IM_COL32(120, 120, 130, 255), msg);
        return;
    }

    std::string info = m_loadedFilename.empty()
        ? "No file loaded"
        : m_loadedFilename + "   zoom " + ofToString(m_zoom, 2)
            + "x   scroll=zoom  LMB=pan  R=reset  F=fit";
    const ImVec2 textSize = ImGui::CalcTextSize(info.c_str());
    dl->AddText(font, fs,
                ImVec2(sw + 8.f, ofGetHeight() - textSize.y - 8.f),
                IM_COL32(150, 150, 165, 255),
                info.c_str());
}

void ofApp::drawGrid() {
    if (m_zoom <= 0.f || !std::isfinite(m_zoom))
        return;

    const float vpW = viewportW();
    const float vpH = float(ofGetHeight());

    ofPushMatrix();
    applyViewTransform();

    float baseStep = 10.f;
    while (baseStep * m_zoom < 12.f && baseStep < 1e9f) baseStep *= 10.f;
    while (baseStep * m_zoom > 120.f && baseStep > 1e-9f) baseStep /= 10.f;
    if (baseStep <= 0.f || !std::isfinite(baseStep)) {
        ofPopMatrix();
        return;
    }

    const float left   = m_viewCenter.x - (vpW * 0.5f) / m_zoom;
    const float top    = m_viewCenter.y - (vpH * 0.5f) / m_zoom;
    const float right  = m_viewCenter.x + (vpW * 0.5f) / m_zoom;
    const float bottom = m_viewCenter.y + (vpH * 0.5f) / m_zoom;

    const float startX = std::floor(left  / baseStep) * baseStep;
    const float startY = std::floor(top   / baseStep) * baseStep;

    constexpr int kMaxGridLines = 250;
    ofSetLineWidth(std::max(0.5f, 1.f / m_zoom));

    int count = 0;
    for (float x = startX; x <= right && count < kMaxGridLines; x += baseStep, ++count) {
        const bool isMain = (std::fmod(std::abs(x), baseStep * 10.f) < 0.1f);
        ofSetColor(isMain ? ofColor(50, 50, 65) : ofColor(38, 38, 50));
        ofDrawLine(x, top, x, bottom);
    }
    count = 0;
    for (float y = startY; y <= bottom && count < kMaxGridLines; y += baseStep, ++count) {
        const bool isMain = (std::fmod(std::abs(y), baseStep * 10.f) < 0.1f);
        ofSetColor(isMain ? ofColor(50, 50, 65) : ofColor(38, 38, 50));
        ofDrawLine(left, y, right, y);
    }
    ofSetColor(70, 40, 40, 120); ofDrawLine(left, 0, right, 0);
    ofSetColor(40, 70, 40, 120); ofDrawLine(0, top, 0, bottom);

    ofPopMatrix();
}

// ─────────────────────────────────────────────────────────────────────────────
// ImGui sidebar
// ─────────────────────────────────────────────────────────────────────────────

void ofApp::drawSidebar() {
    const float sw = sidebarW();
    const float pad = ImGui::GetStyle().WindowPadding.x * 2.f;

    ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ sw, float(ofGetHeight()) }, ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##sidebar", nullptr, flags);
    ImGui::PushItemWidth(sw - pad);

    ImGui::TextDisabled("DXF \xe2\x86\x94 SVG Converter");
    ImGui::Spacing();

    drawFileSection();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawLayerSection();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawExportSection();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAppearanceSection();

    ImGui::PopItemWidth();
    ImGui::End();
}

void ofApp::drawFileSection() {
    ImGui::PushID("file");

    const float sw = sidebarW();
    const float pad = ImGui::GetStyle().WindowPadding.x * 2.f;
    const float halfBtnW = (sw - pad) * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f;

    ImGui::SeparatorText("File");

    if (ImGui::Button("Open DXF...", { halfBtnW, 0 })) {
        ofFileDialogResult res = ofSystemLoadDialog("Open DXF file", false, ofToDataPath(""));
        if (res.bSuccess) loadDxf(res.getPath());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open SVG...", { -1, 0 })) {
        ofFileDialogResult res = ofSystemLoadDialog("Open SVG file", false, ofToDataPath(""));
        if (res.bSuccess) loadSvg(res.getPath());
    }

    if (!m_loadedFilename.empty()) {
        ImGui::TextWrapped("%s", m_loadedFilename.c_str());

        if (!m_doc.empty()) {
            auto& b = m_doc.bounds;
            ImGui::TextDisabled("%.1f x %.1f  (%s)",
                b.width, b.height,
                m_docFlipped ? "Y-flipped" : "Y-up");
            ImGui::TextDisabled("%d layer%s  |  %d entities",
                (int)m_doc.layers.size(),
                m_doc.layers.size() == 1 ? "" : "s",
                (int)m_doc.getAllEntities().size());
        }

        ImGui::Spacing();

        if (ImGui::Button("Fit View", { halfBtnW, 0 })) fitView();
        ImGui::SameLine();
        if (ImGui::Button(m_docFlipped ? "Flip Y (on)" : "Flip Y (off)", { -1, 0 })) {
            m_doc.flipY();
            m_docFlipped = !m_docFlipped;
        }

        ImGui::Checkbox("Show grid", &m_showGrid);
    }

    ImGui::PopID();
}

void ofApp::drawLayerSection() {
    ImGui::PushID("layers");

    ImGui::SeparatorText("Layers");

    if (m_doc.empty()) {
        ImGui::TextDisabled("(no file loaded)");
        ImGui::PopID();
        return;
    }

    if (ImGui::SmallButton("All"))  for (auto& [n,ld] : m_layerDisplay) ld.visible = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) for (auto& [n,ld] : m_layerDisplay) ld.visible = false;

    ImGui::Spacing();

    for (int layerIdx = 0; layerIdx < (int)m_doc.layers.size(); ++layerIdx) {
        auto& layer = m_doc.layers[layerIdx];
        auto& ld = layerDisplay(layer.name);

        ImGui::PushID(layerIdx);

        ImGui::Checkbox("##vis", &ld.visible);
        ImGui::SameLine();
        ImGui::ColorEdit3("##col", ld.color,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();

        std::string label = layer.name + " (" + std::to_string(layer.entities.size()) + ")";
        if (!ld.visible) ImGui::TextDisabled("%s", label.c_str());
        else             ImGui::TextUnformatted(label.c_str());

        ImGui::SliderFloat("##sw", &ld.strokeWidth, 0.05f, 3.f, "stroke %.2f mm");

        ImGui::PopID();
    }

    ImGui::PopID();
}

void ofApp::drawExportSection() {
    ImGui::PushID("export");

    ImGui::SeparatorText("Export");

    if (m_doc.empty()) {
        ImGui::TextDisabled("(load a file first)");
        ImGui::PopID();
        return;
    }

    ImGui::RadioButton("SVG", &m_exportType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("DXF", &m_exportType, 1);

    ImGui::Spacing();
    ImGui::Text("Output path:");
    ImGui::InputText("##outpath", m_outPathBuf, sizeof(m_outPathBuf));
    ImGui::SameLine();
    if (ImGui::SmallButton("...##browse")) {
        const char* ext  = (m_exportType == 0) ? "output.svg" : "output.dxf";
        const char* desc = (m_exportType == 0) ? "Save SVG"   : "Save DXF";
        ofFileDialogResult res = ofSystemSaveDialog(ext, desc);
        if (res.bSuccess) {
            std::string p = res.getPath();
            if (p.size() < sizeof(m_outPathBuf))
                std::copy(p.begin(), p.end() + 1, m_outPathBuf);
        }
    }

    if (m_exportType == 0) {
        ImGui::Spacing();
        const char* units[] = { "mm (native)", "px @ 96 dpi", "px @ 72 dpi" };
        ImGui::Combo("##units", &m_unitMode, units, 3);
    }

    ImGui::Spacing();
    const char* btnLabel = (m_exportType == 0) ? "Export SVG" : "Export DXF";
    if (ImGui::Button(btnLabel, { -1, 32 })) {
        std::string out = std::string(m_outPathBuf);
        if (out.empty()) {
            out = ofToDataPath(m_exportType == 0 ? "output.svg" : "output.dxf", true);
        } else if (!ofFilePath::isAbsolute(out)) {
            out = ofToDataPath(out, true);
        }

        if (m_exportType == 0)
            m_exportSuccess = exportSvg(out);
        else
            m_exportSuccess = exportDxf(out);

        m_exportResult = true;
        m_exportTime   = ofGetElapsedTimef();
    }

    if (m_exportResult) {
        double age = ofGetElapsedTimef() - m_exportTime;
        if (age < 4.0) {
            if (m_exportSuccess)
                ImGui::TextColored({ 0.4f, 1.f, 0.5f, 1.f }, "Saved: %s", m_outPathBuf);
            else
                ImGui::TextColored({ 1.f, 0.4f, 0.4f, 1.f }, "Export failed — check console");
        }
    }

    ImGui::PopID();
}

void ofApp::drawAppearanceSection() {
    ImGui::PushID("appearance");

    ImGui::SeparatorText("View");

    if (!m_doc.empty())
        ImGui::TextDisabled("Zoom %.2fx", m_zoom);

    ImGui::Spacing();
    ImGui::SeparatorText("Appearance");

    if (ImGui::SliderFloat("##uiScale", &m_uiScale, 0.75f, 3.f, "UI scale %.2fx"))
        ImTheme::SetUIScale(m_uiScale);

    if (ImGui::Button("Auto (OS DPI)"))
        ImTheme::SetUIScale(m_uiScale = ImTheme::DetectOsScale());

    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────
// Loading
// ─────────────────────────────────────────────────────────────────────────────

void ofApp::loadDxf(const std::string& path) {
    m_doc = ofxDxf::load(path);
    // DXF is Y-up; flip to screen/SVG Y-down immediately
    if (!m_doc.empty()) m_doc.flipY();
    m_docFlipped     = true;
    m_loadedFilename = ofFilePath::getFileName(path);
    m_exportResult   = false;

    onDocLoaded();

    ofLogNotice("ofApp") << "Loaded DXF " << m_loadedFilename
        << "  layers=" << m_doc.layers.size()
        << "  entities=" << m_doc.getAllEntities().size();
    for (auto& layer : m_doc.layers)
        ofLogNotice("ofApp") << "  layer '" << layer.name << "'  entities="
            << layer.entities.size();
}

void ofApp::loadSvg(const std::string& path) {
    ofxSvg svg;
    if (!svg.load(path)) {
        ofLogError("ofApp") << "Failed to load SVG: " << path;
        return;
    }

    m_doc = DxfDocument();
    m_docFlipped = false;  // SVG is already Y-down

    // Walk all paths in the SVG; group by the nearest named parent <g> id (layer name).
    // ofxSvgPath is the base for PATH, RECTANGLE, ELLIPSE and CIRCLE — all have getPath().
    std::function<void(ofxSvgGroup*, const std::string&)> walk =
        [&](ofxSvgGroup* group, const std::string& layerName) {
            for (auto& child : group->getChildren()) {
                ofxSvgType t = child->getType();
                if (t == OFXSVG_TYPE_GROUP) {
                    auto* g = static_cast<ofxSvgGroup*>(child.get());
                    std::string name = g->getName().empty() ? layerName : g->getName();
                    walk(g, name);
                } else if (t == OFXSVG_TYPE_PATH      ||
                           t == OFXSVG_TYPE_RECTANGLE  ||
                           t == OFXSVG_TYPE_ELLIPSE    ||
                           t == OFXSVG_TYPE_CIRCLE) {
                    auto* p = static_cast<ofxSvgPath*>(child.get());
                    if (p->getPath().getCommands().empty()) continue;
                        DxfLayer* layer = nullptr;
                        for (auto& l : m_doc.layers)
                            if (l.name == layerName) { layer = &l; break; }
                        if (!layer) {
                            m_doc.layers.push_back({layerName, {}});
                            layer = &m_doc.layers.back();
                        }
                        DxfEntity e;
                        e.type     = DxfEntity::Type::Polyline;
                        e.layer    = layerName;
                        e.path     = p->getPath();
                        e.path.setFilled(false);
                        layer->entities.push_back(std::move(e));
                }
            }
        };

    walk(&svg, "0");

    if (m_doc.empty()) {
        ofLogWarning("ofApp") << "SVG loaded but no usable paths found";
        return;
    }

    // Compute bounds
    ofRectangle bounds;
    bool first = true;
    for (auto& layer : m_doc.layers) {
        for (auto& e : layer.entities) {
            for (const auto& outline : e.getOutline()) {
                for (const auto& v : outline.getVertices()) {
                    if (first) { bounds.set(v.x, v.y, 0, 0); first = false; }
                    else bounds.growToInclude(v.x, v.y);
                }
            }
        }
    }
    if (!first) m_doc.bounds = bounds;

    m_loadedFilename = ofFilePath::getFileName(path);
    m_exportResult   = false;

    onDocLoaded();

    ofLogNotice("ofApp") << "Loaded SVG " << m_loadedFilename
        << "  layers=" << m_doc.layers.size()
        << "  entities=" << m_doc.getAllEntities().size();
}

void ofApp::onDocLoaded() {
    m_layerDisplay.clear();
    m_paletteIdx = 0;

    for (auto& layer : m_doc.layers) {
        auto& ld = layerDisplay(layer.name);
        ld.color[0] = layer.color.r / 255.f;
        ld.color[1] = layer.color.g / 255.f;
        ld.color[2] = layer.color.b / 255.f;
        ld.strokeWidth = 0.5f;
        ld.visible = true;

        std::string lower = layer.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "defpoints")
            ld.visible = false;
    }
    fitView();
}

void ofApp::fitView() {
    if (m_doc.empty()) return;

    const ofRectangle b = fitBounds();
    if (b.width < 1e-4f || b.height < 1e-4f) return;

    const float vpW = viewportW();
    const float vpH = float(ofGetHeight());
    const float pad = 0.88f;

    m_viewCenter = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    m_zoom = std::min(vpW * pad / b.width, vpH * pad / b.height);
    clampZoom();
}

void ofApp::windowResized(int w, int h) {
    (void)w; (void)h;
    // Keep zoom/pan — do not call fitView() here.
}

// ─────────────────────────────────────────────────────────────────────────────
// SVG export  (via ofxSvg)
// ─────────────────────────────────────────────────────────────────────────────

bool ofApp::exportSvg(const std::string& outPath) {
    if (m_doc.empty()) return false;

    auto& b = m_doc.bounds;

    // DXF coordinates are in mm. Mode 0 keeps them as mm (no scaling).
    // Modes 1/2 convert to screen pixels at 96 or 72 dpi for web/print use.
    float unitScale = 1.f;
    if (m_unitMode == 1) unitScale = 96.f / 25.4f;
    if (m_unitMode == 2) unitScale = 72.f / 25.4f;
    const char* unitStr = (m_unitMode == 0) ? "mm" : "px";

    float docW = b.width  * unitScale;
    float docH = b.height * unitScale;

    ofxSvg svg;
    svg.setUnitString(unitStr);
    svg.setWidth(docW);
    svg.setHeight(docH);
    svg.setViewBox(ofRectangle(0, 0, docW, docH));
    svg.setFilled(false);

    for (auto& layer : m_doc.layers) {
        auto& ld = layerDisplay(layer.name);
        ofColor col(int(ld.color[0]*255), int(ld.color[1]*255), int(ld.color[2]*255));

        auto group = svg.addGroup(layer.name);
        svg.pushGroup(group);
        svg.setStrokeColor(col);
        svg.setHasStroke(true);
        svg.setStrokeWidth(ld.strokeWidth * unitScale);
        if (!ld.visible) group->setVisible(false);

        for (auto& entity : layer.entities) {
            if (entity.type == DxfEntity::Type::Point)
                continue;

            ofPath p = entity.exportPath();
            if (p.getCommands().empty())
                continue;

            p.setMode(ofPath::COMMANDS);
            p.translate(glm::vec2(-b.x, -b.y));
            p.scale(unitScale, unitScale);
            svg.add(p);
        }
        svg.popGroup();
    }

    bool ok = svg.save(of::filesystem::path(outPath));
    if (ok)
        ofLogNotice("ofApp") << "SVG saved: " << outPath;
    else
        ofLogError("ofApp") << "ofxSvg::save() failed for: " << outPath;
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// DXF export
// ─────────────────────────────────────────────────────────────────────────────

bool ofApp::exportDxf(const std::string& outPath) {
    if (m_doc.empty()) return false;

    // If the document is currently Y-flipped (screen space), flip it back to
    // Y-up (DXF convention) for export, working on a copy.
    DxfDocument exportDoc = m_doc;
    if (m_docFlipped) exportDoc.flipY();

    bool ok = ofxDxf::save(exportDoc, outPath);
    if (ok)
        ofLogNotice("ofApp") << "DXF saved: " << outPath;
    else
        ofLogError("ofApp") << "exportDxf: failed: " << outPath;
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

LayerDisplay& ofApp::layerDisplay(const std::string& name) {
    auto it = m_layerDisplay.find(name);
    if (it != m_layerDisplay.end()) return it->second;
    LayerDisplay ld;
    ofColor c = PALETTE[m_paletteIdx % PALETTE_SIZE];
    m_paletteIdx++;
    ld.color[0] = c.r / 255.f;
    ld.color[1] = c.g / 255.f;
    ld.color[2] = c.b / 255.f;
    m_layerDisplay[name] = ld;
    return m_layerDisplay[name];
}

// ─────────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────────

void ofApp::keyPressed(int key) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (key == 'r' || key == 'R') {
        m_zoom = 1.f;
        m_viewCenter = m_doc.empty()
            ? glm::vec2(0, 0)
            : glm::vec2(m_doc.bounds.getCenter());
    }
    if (key == 'f' || key == 'F') fitView();
    if (key == 'g' || key == 'G') m_showGrid = !m_showGrid;
}

void ofApp::mouseScrolled(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (!isInViewport(float(e.x))) return;

    const float factor = (e.scrollY > 0) ? 1.12f : (1.f / 1.12f);
    zoomAt({ float(e.x) - sidebarW(), float(e.y) }, factor);
}

void ofApp::mousePressed(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (!isInViewport(float(e.x))) return;
    if (e.button == OF_MOUSE_BUTTON_LEFT) {
        m_isPanning       = true;
        m_dragStartMouse  = { float(e.x), float(e.y) };
        m_viewCenterStart = m_viewCenter;
    }
}

void ofApp::mouseReleased(ofMouseEventArgs& e) {
    m_isPanning = false;
}

void ofApp::mouseDragged(ofMouseEventArgs& e) {
    if (!m_isPanning) return;
    const glm::vec2 delta(e.x - m_dragStartMouse.x, e.y - m_dragStartMouse.y);
    m_viewCenter = m_viewCenterStart - delta / m_zoom;
}

void ofApp::dragEvent(ofDragInfo info) {
    if (info.files.empty()) return;
    std::string path = info.files[0].string();
    std::string ext  = ofFilePath::getFileExt(path);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "svg")
        loadSvg(path);
    else
        loadDxf(path);
}
