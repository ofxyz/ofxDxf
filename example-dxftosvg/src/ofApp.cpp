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

    m_gui.setup(nullptr, true);

    m_pan = { (ofGetWidth() - SIDEBAR_W) * 0.5f, ofGetHeight() * 0.5f };

    std::string def = ofToDataPath("test.dxf");
    if (ofFile(def).exists()) loadDxf("test.dxf");
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::draw() {
    float vpW = ofGetWidth() - SIDEBAR_W;
    float vpH = float(ofGetHeight());

    ofPushView();
    ofViewport(SIDEBAR_W, 0, vpW, vpH);
    ofSetupScreenOrtho(vpW, vpH, -1, 1);

    ofBackground(28, 28, 35);
    if (m_showGrid) drawGrid();
    drawScene();

    ofPopView();

    ofSetColor(50, 50, 62);
    ofDrawRectangle(SIDEBAR_W - 1, 0, 1, ofGetHeight());

    m_gui.begin();
    drawSidebar();
    m_gui.end();
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::drawScene() {
    if (m_doc.empty()) {
        ofSetColor(80);
        ofDrawBitmapString("Drop a .dxf or .svg file here, or use Open...", 20, 30);
        return;
    }

    ofPushMatrix();
    ofTranslate(m_pan.x, m_pan.y);
    ofScale(m_zoom, m_zoom);

    for (auto& layer : m_doc.layers) {
        auto& ld = layerDisplay(layer.name);
        if (!ld.visible) continue;

        ofSetColor(
            int(ld.color[0] * 255),
            int(ld.color[1] * 255),
            int(ld.color[2] * 255));
        ofSetLineWidth(std::max(0.5f, ld.strokeWidth));

        for (auto& e : layer.entities)
            e.polyline.draw();
    }

    if (!m_doc.bounds.isEmpty()) {
        ofNoFill();
        ofSetColor(60, 60, 80, 120);
        ofSetLineWidth(0.5f);
        ofDrawRectangle(m_doc.bounds);
    }

    ofPopMatrix();

    ofSetColor(100);
    std::string info = m_loadedFilename.empty()
        ? "No file loaded"
        : m_loadedFilename + "   zoom " + ofToString(m_zoom, 2)
            + "x   scroll=zoom  LMB=pan  R=reset  F=fit";
    ofDrawBitmapString(info, 8.f, ofGetHeight() - 6.f);
}

void ofApp::drawGrid() {
    float vpW = ofGetWidth() - SIDEBAR_W;
    float vpH = float(ofGetHeight());

    ofPushMatrix();
    ofTranslate(m_pan.x, m_pan.y);
    ofScale(m_zoom, m_zoom);

    float baseStep = 10.f;
    while (baseStep * m_zoom < 12.f) baseStep *= 10.f;
    while (baseStep * m_zoom > 120.f) baseStep /= 10.f;

    float left   = (-m_pan.x) / m_zoom;
    float top    = (-m_pan.y) / m_zoom;
    float right  = left  + vpW / m_zoom;
    float bottom = top   + vpH / m_zoom;

    float startX = std::floor(left  / baseStep) * baseStep;
    float startY = std::floor(top   / baseStep) * baseStep;

    ofSetLineWidth(0.5f);
    for (float x = startX; x <= right;  x += baseStep) {
        bool isMain = (std::fmod(std::abs(x), baseStep * 10.f) < 0.1f);
        ofSetColor(isMain ? ofColor(50, 50, 65) : ofColor(38, 38, 50));
        ofDrawLine(x, top, x, bottom);
    }
    for (float y = startY; y <= bottom; y += baseStep) {
        bool isMain = (std::fmod(std::abs(y), baseStep * 10.f) < 0.1f);
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
    ImGui::SetNextWindowPos({ 0, 0 }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ SIDEBAR_W, float(ofGetHeight()) }, ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##sidebar", nullptr, flags);
    ImGui::PushItemWidth(SIDEBAR_W - 30.f);

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

    ImGui::PopItemWidth();
    ImGui::End();
}

void ofApp::drawFileSection() {
    ImGui::SeparatorText("File");

    if (ImGui::Button("Open DXF...", { (SIDEBAR_W - 34.f) * 0.5f, 0 })) {
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

        if (ImGui::Button("Fit View", { (SIDEBAR_W - 34.f) * 0.5f, 0 })) fitView();
        ImGui::SameLine();
        if (ImGui::Button(m_docFlipped ? "Flip Y (on)" : "Flip Y (off)", { -1, 0 })) {
            m_doc.flipY();
            m_docFlipped = !m_docFlipped;
        }

        ImGui::Checkbox("Show grid", &m_showGrid);
    }
}

void ofApp::drawLayerSection() {
    ImGui::SeparatorText("Layers");

    if (m_doc.empty()) {
        ImGui::TextDisabled("(no file loaded)");
        return;
    }

    if (ImGui::SmallButton("All"))  for (auto& [n,ld] : m_layerDisplay) ld.visible = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) for (auto& [n,ld] : m_layerDisplay) ld.visible = false;

    ImGui::Spacing();

    for (auto& layer : m_doc.layers) {
        auto& ld = layerDisplay(layer.name);

        ImGui::Checkbox(("##vis_" + layer.name).c_str(), &ld.visible);
        ImGui::SameLine();
        ImGui::ColorEdit3(("##col_" + layer.name).c_str(), ld.color,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoPicker);
        ImGui::SameLine();

        std::string label = layer.name + " (" + std::to_string(layer.entities.size()) + ")";
        if (!ld.visible) ImGui::TextDisabled("%s", label.c_str());
        else             ImGui::TextUnformatted(label.c_str());

        ImGui::PushItemWidth(SIDEBAR_W - 30.f);
        ImGui::SliderFloat(("##sw_" + layer.name).c_str(), &ld.strokeWidth, 0.1f, 5.f, "%.1f px");
        ImGui::PopItemWidth();
    }
}

void ofApp::drawExportSection() {
    ImGui::SeparatorText("Export");

    if (m_doc.empty()) {
        ImGui::TextDisabled("(load a file first)");
        return;
    }

    // Export type selector
    ImGui::RadioButton("SVG", &m_exportType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("DXF", &m_exportType, 1);

    ImGui::Spacing();
    ImGui::Text("Output path:");
    ImGui::InputText("##outpath", m_outPathBuf, sizeof(m_outPathBuf));
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) {
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
        ImGui::Combo("Units", &m_unitMode, units, 3);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// Loading
// ─────────────────────────────────────────────────────────────────────────────

void ofApp::loadDxf(const std::string& path) {
    m_doc = ofxDxf::load(path, 64);
    // DXF is Y-up; flip to screen/SVG Y-down immediately
    if (!m_doc.empty()) m_doc.flipY();
    m_docFlipped     = true;
    m_loadedFilename = ofFilePath::getFileName(path);
    m_exportResult   = false;

    onDocLoaded();

    ofLogNotice("ofApp") << "Loaded DXF " << m_loadedFilename
        << "  layers=" << m_doc.layers.size()
        << "  entities=" << m_doc.getAllEntities().size();
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
                    for (auto& poly : p->getPath().getOutline()) {
                        if (poly.getVertices().size() < 2) continue;
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
                        e.polyline = poly;
                        layer->entities.push_back(std::move(e));
                    }
                }
            }
        };

    walk(&svg, "0");

    if (m_doc.empty()) {
        ofLogWarning("ofApp") << "SVG loaded but no usable paths found";
        return;
    }

    // Compute bounds
    bool first = true;
    float minX, minY, maxX, maxY;
    for (auto& layer : m_doc.layers) {
        for (auto& e : layer.entities) {
            for (auto& v : e.polyline.getVertices()) {
                if (first) { minX = maxX = v.x; minY = maxY = v.y; first = false; }
                else {
                    minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
                }
            }
        }
    }
    if (!first) m_doc.bounds.set(minX, minY, maxX - minX, maxY - minY);

    m_loadedFilename = ofFilePath::getFileName(path);
    m_exportResult   = false;

    onDocLoaded();

    ofLogNotice("ofApp") << "Loaded SVG " << m_loadedFilename
        << "  layers=" << m_doc.layers.size()
        << "  entities=" << m_doc.getAllEntities().size();
}

void ofApp::onDocLoaded() {
    // Pre-assign palette colours in layer order (layerDisplay does lazy init,
    // but calling it here ensures consistent ordering before first draw).
    for (auto& layer : m_doc.layers)
        layerDisplay(layer.name);
    fitView();
}

void ofApp::fitView() {
    if (m_doc.empty()) return;
    auto& b = m_doc.bounds;
    if (b.width < 1.f || b.height < 1.f) return;

    float vpW = ofGetWidth() - SIDEBAR_W;
    float vpH = float(ofGetHeight());

    float pad   = 0.88f;
    float scaleX = vpW * pad / b.width;
    float scaleY = vpH * pad / b.height;
    m_zoom = std::min(scaleX, scaleY);

    m_pan = {
        vpW * 0.5f - (b.x + b.width  * 0.5f) * m_zoom,
        vpH * 0.5f - (b.y + b.height * 0.5f) * m_zoom
    };
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
            const auto& verts = entity.polyline.getVertices();
            if (verts.size() < 2) continue;
            ofPolyline scaled;
            scaled.setClosed(entity.polyline.isClosed());
            for (auto& v : verts)
                scaled.addVertex((v.x - b.x) * unitScale, (v.y - b.y) * unitScale, 0);
            svg.add(scaled);
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
        m_pan  = { (ofGetWidth() - SIDEBAR_W) * 0.5f, ofGetHeight() * 0.5f };
    }
    if (key == 'f' || key == 'F') fitView();
    if (key == 'g' || key == 'G') m_showGrid = !m_showGrid;
}

void ofApp::mouseScrolled(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    float factor = (e.scrollY > 0) ? 1.12f : (1.f / 1.12f);
    glm::vec2 mouse(e.x - SIDEBAR_W, e.y);
    m_pan   = mouse + (m_pan - mouse) * factor;
    m_zoom *= factor;
}

void ofApp::mousePressed(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (e.button == OF_MOUSE_BUTTON_LEFT) {
        m_isPanning      = true;
        m_panStartMouse  = { float(e.x), float(e.y) };
        m_panStart       = m_pan;
    }
}

void ofApp::mouseReleased(ofMouseEventArgs& e) {
    m_isPanning = false;
}

void ofApp::mouseDragged(ofMouseEventArgs& e) {
    if (!m_isPanning) return;
    m_pan = m_panStart + glm::vec2(e.x - m_panStartMouse.x, e.y - m_panStartMouse.y);
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
