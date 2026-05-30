#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ImTheme.h"
#include "ImThemeRegistry.h"
#include "ImFonts.h"
#include "ofxDxf.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void windowResized(int w, int h) override;

    void keyPressed(int key) override;
    void mouseScrolled(ofMouseEventArgs& e) override;
    void mousePressed(ofMouseEventArgs& e) override;
    void mouseReleased(ofMouseEventArgs& e) override;
    void mouseDragged(ofMouseEventArgs& e) override;
    void dragEvent(ofDragInfo info) override;

private:
    void loadFile(const std::string& path);
    void setupImGui();
    void drawUI();
    void drawScene();
    void drawEntity(const DxfEntity& entity, const ofColor& color);
    void fitView();
    void clampZoom();
    ofRectangle fitBounds() const;
    void applyViewTransform() const;
    void zoomAt(const glm::vec2& screenMouse, float factor);

    static constexpr float kMinZoom = 1e-4f;
    static constexpr float kMaxZoom = 1e6f;

    DxfDocument      m_doc;
    std::string      m_filename;
    std::string      m_loadedPath;

    float            m_zoom           = 1.f;
    glm::vec2        m_viewCenter     = { 0, 0 };
    bool             m_isPanning      = false;
    glm::vec2        m_viewCenterStart;
    glm::vec2        m_dragStartMouse;
    float            m_uiScale        = 1.f;

    std::map<std::string, bool> m_layerVisible;

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

    ofxImGui::Gui m_gui;
};
