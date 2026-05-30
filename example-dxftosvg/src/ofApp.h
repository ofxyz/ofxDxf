#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ImTheme.h"
#include "ImThemeRegistry.h"
#include "ImFonts.h"
#include "ofxDxf.h"
#include "ofxSvg.h"
#include <string>
#include <vector>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// Per-layer display state
// ─────────────────────────────────────────────────────────────────────────────
struct LayerDisplay {
    bool    visible     = true;
    float   color[3]    = { 1.f, 1.f, 1.f };
    float   strokeWidth = 0.5f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ofApp
// ─────────────────────────────────────────────────────────────────────────────
class ofApp : public ofBaseApp {
public:
    void setup()  override;
    void draw()   override;
    void windowResized(int w, int h) override;

    void keyPressed(int key) override;
    void mouseScrolled(ofMouseEventArgs& e) override;
    void mousePressed(ofMouseEventArgs& e)  override;
    void mouseReleased(ofMouseEventArgs& e) override;
    void mouseDragged(ofMouseEventArgs& e)  override;
    void dragEvent(ofDragInfo info)         override;

private:
    // ── Loading ──────────────────────────────────────────────────────────────
    void loadDxf(const std::string& path);
    void loadSvg(const std::string& path);
    void onDocLoaded();
    void fitView();
    ofRectangle fitBounds() const;
    void applyViewTransform() const;
    void zoomAt(const glm::vec2& viewMouse, float factor);

    // ── Drawing ──────────────────────────────────────────────────────────────
    void drawScene();
    void drawGrid();
    void drawEntity(const DxfEntity& entity);
    void clampZoom();

    // ── ImGui panels ─────────────────────────────────────────────────────────
    void drawSidebar();
    void drawFileSection();
    void drawLayerSection();
    void drawExportSection();
    void drawAppearanceSection();
    void drawViewportOverlay();

    void setupImGui();
    float sidebarW() const { return kSidebarBaseW * m_uiScale; }
    float viewportW() const { return float(ofGetWidth()) - sidebarW(); }

    bool isInViewport(float screenX) const { return screenX >= sidebarW(); }

    // ── Export ───────────────────────────────────────────────────────────────
    bool exportSvg(const std::string& outPath);
    bool exportDxf(const std::string& outPath);

    // ── Helpers ──────────────────────────────────────────────────────────────
    LayerDisplay& layerDisplay(const std::string& name);

    // ── Data ─────────────────────────────────────────────────────────────────
    DxfDocument  m_doc;
    std::string  m_loadedFilename;
    bool         m_docFlipped = false;   // has flipY been applied to m_doc?

    std::map<std::string, LayerDisplay> m_layerDisplay;

    // ── View ─────────────────────────────────────────────────────────────────
    static constexpr float kSidebarBaseW = 280.f;
    static constexpr float kMinZoom      = 1e-4f;
    static constexpr float kMaxZoom      = 1e6f;

    glm::vec2 m_viewCenter = { 0, 0 };
    float     m_zoom       = 1.f;
    float     m_uiScale      = 1.f;
    bool      m_isPanning    = false;
    glm::vec2 m_viewCenterStart;
    glm::vec2 m_dragStartMouse;
    bool      m_showGrid  = true;

    // ── Export settings ──────────────────────────────────────────────────────
    char   m_outPathBuf[512] = "output.svg";
    int    m_unitMode        = 0;    // 0=mm (native DXF units), 1=px @96dpi, 2=px @72dpi
    bool   m_exportResult    = false;
    bool   m_exportSuccess   = false;
    double m_exportTime      = 0;
    int    m_exportType      = 0;    // 0=SVG, 1=DXF

    // ── ImGui ─────────────────────────────────────────────────────────────────
    ofxImGui::Gui      m_gui;

    // ── Colour palette (assigned per layer, cycling) ──────────────────────────
    static const ofColor PALETTE[];
    static const int     PALETTE_SIZE;
    int m_paletteIdx = 0;
};
