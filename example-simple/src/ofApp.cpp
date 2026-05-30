#include "ofApp.h"

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::setup() {
    ofSetWindowTitle("ofxDxf example — drag a .dxf file onto the window");
    ofBackground(30);
    ofSetFrameRate(60);

    setupImGui();

    std::string defaultFile = "test.dxf";
    if (ofFile(ofToDataPath(defaultFile)).exists()) {
        loadFile(defaultFile);
    } else {
        ofLogNotice("ofApp") << "Drop a .dxf file onto the window to load it.";
    }
}

void ofApp::setupImGui() {
    m_gui.setup(nullptr, false);

    if (ImFont* font = ImFonts::LoadDefaultFonts(ImGui::GetIO().Fonts, 15.0f))
        m_gui.setDefaultFont(font);
    m_gui.rebuildFontsTexture();

    ImTheme::Setup(ImTheme::Theme_DarculaDarker);
    m_uiScale = ImTheme::UIScale();
}

void ofApp::update() {}

void ofApp::draw() {
    drawScene();

    m_gui.begin();
    drawUI();
    m_gui.end();
    m_gui.draw();
}

void ofApp::drawScene() {
    if (m_doc.empty())
        return;

    ofPushMatrix();
    applyViewTransform();

    ofSetLineWidth(std::max(1.f, 1.5f / m_zoom));

    int layerIdx = 0;
    for (auto& layer : m_doc.layers) {
        auto it = m_layerVisible.find(layer.name);
        bool visible = (it == m_layerVisible.end()) ? true : it->second;
        if (!visible) { ++layerIdx; continue; }

        const ofColor col = colorForLayer(layerIdx);
        for (auto& entity : layer.entities)
            drawEntity(entity, col);
        ++layerIdx;
    }

    ofPopMatrix();
}

void ofApp::drawEntity(const DxfEntity& entity, const ofColor& color) {
    ofSetColor(color);

    if (entity.type == DxfEntity::Type::Point) {
        const auto& cmds = entity.path.getCommands();
        if (cmds.empty()) return;
        const glm::vec2 p = cmds[0].to;
        ofFill();
        ofDrawCircle(p.x, p.y, 2.f / m_zoom);
        ofNoFill();
        return;
    }

    DxfViewContext view;
    view.center    = m_viewCenter;
    view.viewportW = float(ofGetWidth());
    view.viewportH = float(ofGetHeight());
    entity.draw(m_zoom, -1, &view);
}

void ofApp::drawUI() {
    ImGui::SetNextWindowPos({ 10.f, 10.f }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({ 300.f * m_uiScale, 380.f * m_uiScale }, ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("ofxDxf Viewer", nullptr, flags)) {
        if (m_doc.empty()) {
            ImGui::TextWrapped("Drop a .dxf file onto this window.");
        } else {
            ImGui::TextWrapped("%s", m_filename.c_str());
            const auto& b = m_doc.bounds;
            ImGui::TextDisabled("%.0f x %.0f  |  zoom %.2fx",
                b.width, b.height, m_zoom);
            ImGui::TextDisabled("%d layer%s  |  %d entities",
                (int)m_doc.layers.size(),
                m_doc.layers.size() == 1 ? "" : "s",
                (int)m_doc.getAllEntities().size());

            ImGui::Spacing();
            if (ImGui::Button("Fit View", { -1, 0 }))
                fitView();

            ImGui::Spacing();
            ImGui::SeparatorText("Layers");

            int layerIdx = 0;
            for (auto& layer : m_doc.layers) {
                bool visible = true;
                auto it = m_layerVisible.find(layer.name);
                if (it != m_layerVisible.end()) visible = it->second;

                ImGui::PushID(layerIdx);
                if (ImGui::Checkbox("##vis", &visible))
                    m_layerVisible[layer.name] = visible;
                ImGui::SameLine();

                ImVec4 col = ImGui::ColorConvertU32ToFloat4(
                    IM_COL32(colorForLayer(layerIdx).r,
                             colorForLayer(layerIdx).g,
                             colorForLayer(layerIdx).b, 255));
                ImGui::ColorButton("##swatch", col,
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                    ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                ImGui::SameLine();

                std::string label = layer.name + " (" + std::to_string(layer.entities.size()) + ")";
                if (!visible) ImGui::TextDisabled("%s", label.c_str());
                else          ImGui::TextUnformatted(label.c_str());
                ImGui::PopID();

                ++layerIdx;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Scroll=zoom  Drag=pan  F=fit  R=reset  1-9=toggle");
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Appearance");
        ImGui::PushID("appearance");
        if (ImGui::SliderFloat("##uiScale", &m_uiScale, 0.75f, 3.f, "UI scale %.2fx"))
            ImTheme::SetUIScale(m_uiScale);
        if (ImGui::Button("Auto (OS DPI)"))
            ImTheme::SetUIScale(m_uiScale = ImTheme::DetectOsScale());
        ImGui::PopID();
    }
    ImGui::End();
}

void ofApp::fitView() {
    if (m_doc.empty()) return;

    const ofRectangle b = fitBounds();
    if (b.width < 1e-4f || b.height < 1e-4f) return;

    const float vpW = float(ofGetWidth());
    const float vpH = float(ofGetHeight());
    const float pad = 0.9f;

    m_viewCenter = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    m_zoom = std::min(vpW * pad / b.width, vpH * pad / b.height);
    clampZoom();
}

void ofApp::clampZoom() {
    if (!std::isfinite(m_zoom) || m_zoom <= 0.f)
        m_zoom = 1.f;
    m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);
}

void ofApp::applyViewTransform() const {
    const float vpW = float(ofGetWidth());
    const float vpH = float(ofGetHeight());
    ofTranslate(vpW * 0.5f, vpH * 0.5f);
    ofScale(m_zoom, -m_zoom); // DXF is Y-up; screen is Y-down
    ofTranslate(-m_viewCenter.x, -m_viewCenter.y);
}

ofRectangle ofApp::fitBounds() const {
    ofRectangle bounds;
    bool first = true;

    for (auto& layer : m_doc.layers) {
        auto it = m_layerVisible.find(layer.name);
        if (it != m_layerVisible.end() && !it->second)
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

void ofApp::zoomAt(const glm::vec2& screenMouse, float factor) {
    const float vpW = float(ofGetWidth());
    const float vpH = float(ofGetHeight());
    const glm::vec2 offset = screenMouse - glm::vec2(vpW * 0.5f, vpH * 0.5f);

    const float oldZoom = m_zoom;
    const float newZoom = std::clamp(oldZoom * factor, kMinZoom, kMaxZoom);
    if (newZoom == oldZoom) return;

    const glm::vec2 worldUnderMouse = m_viewCenter + glm::vec2(offset.x, -offset.y) / oldZoom;
    m_viewCenter = worldUnderMouse - glm::vec2(offset.x, -offset.y) / newZoom;
    m_zoom = newZoom;
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::loadFile(const std::string& path) {
    const std::string fullPath = ofFilePath::isAbsolute(path)
        ? path
        : ofToDataPath(path, true);

    ofLogNotice("ofApp") << "Loading: " << fullPath;
    m_doc        = ofxDxf::load(fullPath);
    m_loadedPath = fullPath;
    m_filename   = ofFilePath::getFileName(fullPath);

    if (m_doc.empty()) {
        ofLogWarning("ofApp") << "No geometry found in " << fullPath;
        return;
    }

    for (auto& err : m_doc.errors)
        ofLogWarning("ofApp") << err;

    m_layerVisible.clear();
    for (auto& layer : m_doc.layers) {
        std::string lower = layer.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        m_layerVisible.emplace(layer.name, lower != "defpoints");
    }

    fitView();

    ofLogNotice("ofApp") << "Loaded " << m_doc.layers.size() << " layers, "
        << m_doc.getAllEntities().size() << " entities. "
        << "Bounds: " << m_doc.bounds;
}

void ofApp::windowResized(int w, int h) {
    (void)w; (void)h;
    // Keep zoom/pan — do not call fitView() here.
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::keyPressed(int key) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (key == 'r' || key == 'R') {
        m_zoom = 1.f;
        m_viewCenter = m_doc.empty()
            ? glm::vec2(0, 0)
            : glm::vec2(m_doc.bounds.getCenter());
    }
    if (key == 'f' || key == 'F')
        fitView();
    if (key >= '1' && key <= '9') {
        int idx = key - '1';
        if (idx < (int)m_doc.layers.size()) {
            auto& name = m_doc.layers[idx].name;
            m_layerVisible[name] = !m_layerVisible[name];
        }
    }
}

void ofApp::mouseScrolled(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    const float factor = (e.scrollY > 0) ? 1.12f : (1.f / 1.12f);
    zoomAt({ float(e.x), float(e.y) }, factor);
}

void ofApp::mousePressed(ofMouseEventArgs& e) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (e.button == OF_MOUSE_BUTTON_LEFT) {
        m_isPanning       = true;
        m_dragStartMouse  = { float(e.x), float(e.y) };
        m_viewCenterStart = m_viewCenter;
    }
}

void ofApp::mouseReleased(ofMouseEventArgs& e) {
    (void)e;
    m_isPanning = false;
}

void ofApp::mouseDragged(ofMouseEventArgs& e) {
    if (!m_isPanning) return;
    const glm::vec2 delta(e.x - m_dragStartMouse.x, e.y - m_dragStartMouse.y);
    m_viewCenter = m_viewCenterStart - glm::vec2(delta.x, -delta.y) / m_zoom;
}

void ofApp::dragEvent(ofDragInfo info) {
    if (!info.files.empty())
        loadFile(info.files[0].string());
}
