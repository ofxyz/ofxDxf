#pragma once

#include "ofMain.h"
#include "ofxDxf.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void dragEvent(ofDragInfo info) override;

private:
    void loadFile(const std::string& path);
    void drawLayerList();

    DxfDocument      m_doc;
    std::string      m_filename;

    // Display state
    float            m_zoom   = 1.f;
    glm::vec2        m_pan    = { 0, 0 };
    bool             m_fitOnLoad = true;

    // Per-layer visibility toggle
    std::map<std::string, bool> m_layerVisible;

    // Colours assigned per layer
    std::vector<ofColor> m_palette = {
        ofColor(255, 80,  80),
        ofColor(80,  200, 80),
        ofColor(80,  140, 255),
        ofColor(255, 200, 60),
        ofColor(200, 80,  255),
        ofColor(80,  220, 220),
        ofColor(255, 140, 60),
        ofColor(200, 200, 200),
    };
    ofColor colorForLayer(int index) const {
        return m_palette[index % m_palette.size()];
    }
};
