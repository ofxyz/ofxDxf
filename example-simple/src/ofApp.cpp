#include "ofApp.h"

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::setup() {
    ofSetWindowTitle("ofxDxf example — drag a .dxf file onto the window");
    ofBackground(30);
    ofSetFrameRate(60);

    // Load a default test file if it exists in bin/data
    std::string defaultFile = "test.dxf";
    if (ofFile(ofToDataPath(defaultFile)).exists()) {
        loadFile(defaultFile);
    } else {
        ofLogNotice("ofApp") << "Drop a .dxf file onto the window to load it.";
    }
}

void ofApp::update() {}

void ofApp::draw() {
    // ── Info overlay ─────────────────────────────────────────────────────────
    if (m_doc.empty()) {
        ofSetColor(180);
        ofDrawBitmapString("Drop a .dxf file onto this window", 20, 30);
        return;
    }

    // ── World transform ───────────────────────────────────────────────────────
    ofPushMatrix();
    ofTranslate(m_pan.x, m_pan.y);
    ofScale(m_zoom, m_zoom);

    // ── Draw each visible layer ───────────────────────────────────────────────
    int layerIdx = 0;
    for (auto& layer : m_doc.layers) {
        auto it = m_layerVisible.find(layer.name);
        bool visible = (it == m_layerVisible.end()) ? true : it->second;
        if (!visible) { ++layerIdx; continue; }

        ofSetColor(colorForLayer(layerIdx));
        for (auto& entity : layer.entities) {
            entity.polyline.draw();
        }
        ++layerIdx;
    }

    ofPopMatrix();

    // ── Layer list UI ─────────────────────────────────────────────────────────
    drawLayerList();

    // ── Status bar ────────────────────────────────────────────────────────────
    ofSetColor(200);
    std::string status = m_filename + "   |   "
        + ofToString(m_doc.layers.size()) + " layers   |   zoom " + ofToString(m_zoom, 2)
        + "   |   scroll to zoom, drag to pan, R to reset, F to fit";
    ofDrawBitmapString(status, 10, ofGetHeight() - 10);
}

void ofApp::drawLayerList() {
    int x = 10, y = 10;
    int layerIdx = 0;
    for (auto& layer : m_doc.layers) {
        bool visible = true;
        auto it = m_layerVisible.find(layer.name);
        if (it != m_layerVisible.end()) visible = it->second;

        ofSetColor(colorForLayer(layerIdx), visible ? 255 : 80);
        std::string label = (visible ? "[x] " : "[ ] ") + layer.name
            + " (" + ofToString(layer.entities.size()) + ")";
        ofDrawBitmapString(label, x, y + 15);
        y += 16;
        ++layerIdx;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::loadFile(const std::string& path) {
    ofLogNotice("ofApp") << "Loading: " << path;
    m_doc      = ofxDxf::load(path, 64);
    m_filename = ofFilePath::getFileName(path);

    if (m_doc.empty()) {
        ofLogWarning("ofApp") << "No geometry found in " << path;
        return;
    }

    for (auto& err : m_doc.errors)
        ofLogWarning("ofApp") << err;

    // Default all layers visible
    for (auto& layer : m_doc.layers)
        m_layerVisible.emplace(layer.name, true);

    if (m_fitOnLoad) {
        // Fit drawing to window with 10% padding
        ofRectangle b = m_doc.bounds;
        if (b.width > 0 && b.height > 0) {
            float pad  = 0.9f;
            float scaleX = ofGetWidth()  * pad / b.width;
            float scaleY = ofGetHeight() * pad / b.height;
            m_zoom = std::min(scaleX, scaleY);
            m_pan  = glm::vec2(
                ofGetWidth()  * 0.5f - (b.x + b.width  * 0.5f) * m_zoom,
                ofGetHeight() * 0.5f - (b.y + b.height * 0.5f) * m_zoom);
        }
    }

    ofLogNotice("ofApp") << "Loaded " << m_doc.layers.size() << " layers, "
        << m_doc.getAllEntities().size() << " entities. "
        << "Bounds: " << m_doc.bounds;
}

// ─────────────────────────────────────────────────────────────────────────────

void ofApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        m_zoom = 1.f;
        m_pan  = { 0, 0 };
    }
    if (key == 'f' || key == 'F') {
        m_fitOnLoad = true;
        if (!m_doc.empty()) loadFile(ofToDataPath(m_filename));
    }
    // Toggle layer visibility: 1–9 keys
    if (key >= '1' && key <= '9') {
        int idx = key - '1';
        if (idx < (int)m_doc.layers.size()) {
            auto& name = m_doc.layers[idx].name;
            m_layerVisible[name] = !m_layerVisible[name];
        }
    }
}

void ofApp::dragEvent(ofDragInfo info) {
    if (!info.files.empty()) {
        loadFile(info.files[0].string());
    }
}
