meta:
	ADDON_NAME = ofxDxf
	ADDON_DESCRIPTION = DXF file loading for openFrameworks. Parses LINE, ARC, CIRCLE, POLYLINE (with bulge arcs), SPLINE, and ELLIPSE into ofPolyline entities per layer. Wraps dxflib by RibbonSoft/QCAD.
	ADDON_AUTHOR = Bruno Herfst
	ADDON_TAGS = "dxf" "vector" "import" "cad" "plotter"
	ADDON_URL = https://github.com/qcad/qcad/tree/master/src/3rdparty/dxflib

common:
	ADDON_INCLUDES += libs/dxflib/src

