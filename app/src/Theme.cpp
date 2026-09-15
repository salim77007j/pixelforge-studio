#include "Theme.h"
#include <QApplication>

namespace Theme {

QString styleSheet() {
    return QStringLiteral(R"==(
* { outline: none; }
QMainWindow, QDialog { background: %1; }
QMenuBar { background: %2; border-bottom: 1px solid %5; padding: 2px 6px; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; color: %3; }
QMenuBar::item:selected { background: %4; }
QMenu { background: %2; border: 1px solid %5; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 26px 6px 14px; border-radius: 6px; color: %3; }
QMenu::item:selected { background: %4; color: %6; }
QMenu::separator { height: 1px; background: %7; margin: 5px 8px; }

QToolBar { background: %2; border: none; spacing: 3px; padding: 4px 6px; }
QToolBar::separator { width: 1px; background: %7; margin: 4px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 5px; color: %3; }
QToolButton:hover { background: %4; }
QToolButton:pressed, QToolButton:checked { background: %8; border-color: #B9D8F5; }
QToolButton:disabled { color: #B3BAC4; }

QTabWidget::pane { border: 1px solid %5; border-radius: 10px; background: %9; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: %10; padding: 7px 18px; margin: 4px 2px 0 2px;
               border: 1px solid transparent; border-bottom: none; border-top-left-radius: 9px; border-top-right-radius: 9px; }
QTabBar::tab:hover { background: rgba(74,144,226,0.08); }
QTabBar::tab:selected { background: %2; border-color: %5; color: %3; font-weight: 600; }

QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: transparent; padding: 8px 4px 4px 4px; }
QFrame.panel { background: %2; border: 1px solid %5; border-radius: 10px; }
QLabel.panelHeader { color: %3; font-weight: 600; font-size: 13px; padding: 2px; }
QLabel.subtle { color: %10; }

QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: #DFE3E9; }
QSlider::sub-page:horizontal { background: %6; border-radius: 2px; }
QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 8px;
                             background: white; border: 1px solid #C9D2DC; }
QSlider::handle:horizontal:hover { border-color: %6; }
QSlider::groove:vertical { width: 5px; border-radius: 2px; background: #DFE3E9; }
QSlider::sub-page:vertical { background: %6; border-radius: 2px; }
QSlider::handle:vertical { height: 16px; width: 16px; margin: 0 -6px; border-radius: 8px;
                           background: white; border: 1px solid #C9D2DC; }

QComboBox, QSpinBox, QDoubleSpinBox { background: %2; border: 1px solid %5; border-radius: 7px;
                                       padding: 5px 10px; color: %3; min-height: 20px; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #C4CBD4; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %6; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: %2; border: 1px solid %5; border-radius: 8px; selection-background-color: %8; selection-color: %3; outline: none; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }

QPushButton { background: %2; border: 1px solid %5; border-radius: 8px; padding: 7px 16px; color: %3; }
QPushButton:hover { background: %4; }
QPushButton:pressed { background: %11; }
QPushButton:disabled { color: #B3BAC4; background: #F1F3F6; }
QPushButton[accent="true"] { background: %6; border-color: %6; color: white; font-weight: 600; }
QPushButton[accent="true"]:hover { background: %12; }
QPushButton[accent="true"]:pressed { background: #346FB3; }

QLineEdit { background: %2; border: 1px solid %5; border-radius: 7px; padding: 6px 10px; color: %3; }
QLineEdit:focus { border-color: %6; }
QPlainTextEdit, QTextEdit { background: %2; border: 1px solid %5; border-radius: 8px; padding: 4px; color: %3; }

QTreeView, QTreeWidget, QListWidget, QListView, QTableView, QTableWidget { background: %2; border: 1px solid %5;
    border-radius: 8px; color: %3; }
QTreeWidget::item, QTreeWidget::item:selected { border: none; }
QTreeWidget::item:selected, QListWidget::item:selected, QTableWidget::item:selected { background: %8; color: %3; }
QTreeWidget::item:hover, QListWidget::item:hover { background: %4; }
QHeaderView::section { background: transparent; border: none; padding: 4px; color: %10; font-weight: 600; }

QStatusBar { background: %13; border-top: 1px solid %5; color: %10; }
QStatusBar::item { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #C9CFD8; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #B4BCC7; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #C9CFD8; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QGroupBox { border: 1px solid %5; border-radius: 8px; margin-top: 10px; padding-top: 6px; background: %2; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %10; }
QCheckBox { color: %3; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 5px; border: 1px solid #C9D2DC; background: %2; }
QCheckBox::indicator:checked { background: %6; border-color: %6; image: url(none); }
QToolTip { background: %2; color: %3; border: 1px solid %5; padding: 6px 8px; border-radius: 6px; }
)==")
        .arg(WindowBg, PanelBg, Text, Hover, Border, Accent, TextSecondary, AccentChip, CanvasBg)
        .arg(TextSecondary, Pressed, AccentDark, BottomBar);
}

} // namespace Theme
