#pragma once
#include <QMainWindow>
#include <QColor>
#include "AppTypes.h"

class QTabWidget;
class EditorTab;
class LayersPanel;
class ColorPanel;
class BrushPanel;
class HistoryPanel;
class QToolButton;
class QLabel;
class QComboBox;
class QSlider;
class QTimer;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // ---- shared state accessors (used by canvas & panels) ----
    ToolId currentTool() const { return m_tool; }
    const BrushSettings &brushSettings() const { return m_brush; }
    BrushSettings &brushSettingsRef() { return m_brush; }
    const WandSettings &wandSettings() const { return m_wand; }
    const FillSettings &fillSettings() const { return m_fill; }
    struct GradSettings { GradKind kind; GradTarget target; };
    GradSettings gradientSettings() const { return {m_gradKind, m_gradTarget}; }
    const ShapeSettings &shapeSettings() const { return m_shape; }
    const TextSettings &textSettings() const { return m_text; }
    QColor fgColor() const { return m_fg; }
    QColor bgColor() const { return m_bg; }
    bool fillEnabled() const { return m_shape.hasFill; }

    void setFgColor(const QColor &c);
    void setBgColor(const QColor &c);
    void swapColors();
    void resetColors();

    EditorTab *currentTab() const;
    void refreshAfterEdit(EditorTab *tab, bool structureChanged);
    void applyShortcuts();
    void openPath(const QString &path);          // public: CLI + selftest
    void setCurrentTabTo(EditorTab *tab);
    void triggerToolByName(const QString &name); // selftest helper
    void balanceDocks();                         // enforce designed dock widths

public slots:
    void showCursorPos(const QPointF &docPos);
    void showZoom(double zoom);
    void showStatus(const QString &msg);
    void applyPickedColor(const QColor &c, bool background);

signals:
    void colorsChanged();
    void toolChanged(ToolId tool);

private:
    void createUntitled();

private slots:
    // file
    void newDocument();
    void openFile();
    void openRecent();
    void saveDocument();
    void saveDocumentAs();
    void exportImage();
    void closeTab(int index);
    // edit
    void undo();
    void redo();
    void copyVisible();
    void pasteClipboard();
    void clearArea();
    void fillFg();
    void fillBg();
    void strokeSelection();
    void preferences();
    // image
    void imageSize();
    void canvasSize();
    void cropToSelection();
    void trimTransparent();
    void rotateImage90();
    void rotateImage180();
    void rotateImage270();
    void flipImageH();
    void flipImageV();
    // layer
    void newLayer();
    void newGroup();
    void duplicateLayer();
    void deleteLayer();
    void mergeDown();
    void flattenImage();
    void layerUp();
    void layerDown();
    void addMaskFromSelection();
    // select
    void selectAll();
    void deselect();
    void invertSelection();
    void featherSelection();
    // adjustments
    void adjustBrightnessContrast();
    void adjustHueSat();
    void adjustLevels();
    void adjustCurves();
    void adjustInvert();
    void adjustDesaturate();
    void adjustAutoContrast();
    // filters
    void filterBlur();
    void filterSharpen();
    void filterNoise();
    void filterPixelate();
    void filterTwirl();
    void filterWave();
    void filterEdges();
    void filterEmboss();
    void filterVignette();
    // view
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void zoom100();
    void toggleHistoryPanel(bool on);
    // window / workspace
    void saveWorkspace();
    void resetWorkspace();
    void about();
    // tools
    void setTool(QAction *act);

private:
    void buildMenus();
    void buildTopBar();
    void buildToolStrip();
    void buildDocks();
    void buildStatusBar();
    void buildBottomBar();
    void buildToolOptionsBar();
    QAction *addCmd(const QString &id, const QString &text, const QKeySequence &def,
                    const char *slot, const QString &menu = QString(), QAction::MenuRole role = QAction::NoRole);
    void updateTabTitle(EditorTab *tab);
    void addRecent(const QString &path);
    QWidget *makePanel(const QString &title, QWidget *inner);

    QTabWidget *m_tabs = nullptr;
    LayersPanel *m_layersPanel = nullptr;
    ColorPanel *m_colorPanel = nullptr;
    BrushPanel *m_brushPanel = nullptr;
    HistoryPanel *m_historyPanel = nullptr;
    QDockWidget *m_dockLayers = nullptr;
    QDockWidget *m_dockColor = nullptr;
    QDockWidget *m_dockHistory = nullptr;

    QToolBar *m_topBar = nullptr;
    QToolBar *m_toolStrip = nullptr;
    QToolBar *m_toolOptions = nullptr;
    QToolBar *m_bottomBar = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QLabel *m_statusPos = nullptr;
    QLabel *m_statusDoc = nullptr;
    QLabel *m_statusEngine = nullptr;
    QTimer *m_statusTimer = nullptr;

    QVector<QAction *> m_toolActions;
    QHash<QString, QAction *> m_cmdActions;

    ToolId m_tool = ToolId::Brush;
    BrushSettings m_brush;
    WandSettings m_wand;
    FillSettings m_fill;
    GradKind m_gradKind = GradKind::Linear;
    GradTarget m_gradTarget = GradTarget::FgToBg;
    ShapeSettings m_shape;
    TextSettings m_text;
    QColor m_fg{30, 30, 30};
    QColor m_bg{255, 255, 255};
};
