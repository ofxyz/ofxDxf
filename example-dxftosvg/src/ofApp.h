#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
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

    // ── Drawing ──────────────────────────────────────────────────────────────
    void drawScene();
    void drawGrid();

    // ── ImGui panels ─────────────────────────────────────────────────────────
    void drawSidebar();
    void drawFileSection();
    void drawLayerSection();
    void drawExportSection();

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
    static constexpr float SIDEBAR_W = 280.f;

    glm::vec2 m_pan       = { 0, 0 };
    float     m_zoom      = 1.f;
    bool      m_isPanning = false;
    glm::vec2 m_panStart;
    glm::vec2 m_panStartMouse;
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
