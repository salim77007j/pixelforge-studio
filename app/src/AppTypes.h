#pragma once
#include <QString>
#include <QColor>
#include <QList>
#include <QMetaType>

enum class ToolId {
    Move, RectSelect, EllipticalSelect, Lasso, Wand, Crop, Eyedropper,
    Brush, Pencil, Eraser, Fill, Gradient, Text, Shape, Transform, Perspective,
    Hand, Zoom,
};

struct ToolInfo {
    ToolId id;
    const char *name;
    const char *shortcut; // default
};

inline const QList<ToolInfo> &toolList() {
    static const QList<ToolInfo> list = {
        { ToolId::Move,        "Move",              "V" },
        { ToolId::RectSelect,  "Rectangular Select","M" },
        { ToolId::EllipticalSelect, "Elliptical Select", "J" },
        { ToolId::Lasso,       "Lasso Select",      "L" },
        { ToolId::Wand,        "Magic Wand",        "W" },
        { ToolId::Crop,        "Crop",              "C" },
        { ToolId::Eyedropper,  "Eyedropper",        "I" },
        { ToolId::Brush,       "Brush",             "B" },
        { ToolId::Pencil,      "Pencil",            "N" },
        { ToolId::Eraser,      "Eraser",            "E" },
        { ToolId::Fill,        "Paint Bucket",      "G" },
        { ToolId::Gradient,    "Gradient",          "Shift+G" },
        { ToolId::Text,        "Text",              "T" },
        { ToolId::Shape,       "Shape",             "U" },
        { ToolId::Transform,   "Transform",         "Ctrl+T" },
        { ToolId::Perspective, "Perspective",       "Ctrl+Shift+T" },
        { ToolId::Hand,        "Hand",              "H" },
        { ToolId::Zoom,        "Zoom",              "Z" },
    };
    return list;
}

struct BrushSettings {
    int size = 20;
    int hardness = 50;      // %
    int opacity = 100;      // %
    int flow = 100;         // %
    int spacing = 12;       // %
    bool pressureSize = false;
    bool pressureOpacity = false;
};

struct WandSettings { int tolerance = 32; bool contiguous = true; bool sampleComposite = true; };
struct FillSettings { int tolerance = 32; bool contiguous = true; };

enum class GradKind { Linear, Radial };
enum class GradTarget { FgToBg, FgToTransparent, BgToFg };

struct ShapeSettings {
    int kind = 1;           // 0 line, 1 rect, 2 ellipse, 3 polygon
    double strokeWidth = 4;
    bool hasStroke = true;
    bool hasFill = false;
};

struct TextSettings {
    QString family;
    double size = 64;
    bool bold = false;
    bool italic = false;
};

Q_DECLARE_METATYPE(BrushSettings)
