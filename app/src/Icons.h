#pragma once
#include <QIcon>
#include <QString>

// Programmatic icon set: clean outline style matching the design reference.
namespace Icons {

enum Tool {
    Move, RectSelect, EllipseSelect, Lasso, Wand, Crop, Eyedropper,
    Brush, Pencil, Eraser, Fill, Gradient, Text, Shape, Transform, Perspective,
    Hand, Zoom,
    // toolbar / panel buttons
    Undo, Redo, FlipH, FlipV, Clock, Filter, Help, Plus, Duplicate, Trash, Folder,
    MaskIcon, EyeOpen, EyeClosed, LockOpen, LockClosed, MergeDown, Up, Down, NewLayer,
    SoftRound, HardRound, TaperedInk, Marker, Airbrush, Reset, Check, Cross, Image, Open,
};

QIcon get(Tool t);

} // namespace Icons
