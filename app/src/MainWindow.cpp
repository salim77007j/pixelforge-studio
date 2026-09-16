#include "MainWindow.h"
#include "EditorTab.h"
#include "CanvasView.h"
#include "Theme.h"
#include "Icons.h"
#include "panels/LayersPanel.h"
#include "panels/ColorPanel.h"
#include "panels/BrushPanel.h"
#include "panels/HistoryPanel.h"
#include "dialogs/Dialogs.h"
#include "dialogs/CurvesWidget.h"
#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QClipboard>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFileInfo>
#include <QTimer>
#include <QWidgetAction>
#include <QActionGroup>
#include <QShortcut>
#include <QPainter>
#include <QMouseEvent>
#include <QCheckBox>
#include <QRegularExpression>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QColorDialog>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("PixelForge Studio"));
    resize(1440, 860);
    setAcceptDrops(true);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(false);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        refreshAfterEdit(currentTab(), true);
    });
    setCentralWidget(m_tabs);

    buildMenus();
    buildTopBar();
    buildToolStrip();
    buildDocks();
    buildToolOptionsBar();
    buildBottomBar();
    buildStatusBar();

    QSettings s;
    restoreGeometry(s.value("win/geometry").toByteArray());
    restoreState(s.value("win/state").toByteArray());
    // Guard against stale geometry from a different screen setup (e.g. a
    // window last closed on a 4K external display, now opened on a laptop
    // panel, or a settings file written by a headless run). If the restored
    // window doesn't reasonably fit the current virtual desktop, fall back
    // to the designed default size so the UI looks the same everywhere.
    {
        QRect desk = QGuiApplication::primaryScreen()->availableGeometry();
        for (QScreen *sc : QGuiApplication::screens())
            desk = desk.united(sc->availableGeometry());
        QRect g = geometry();
        bool usable = g.width() >= 1000 && g.height() >= 640
            && desk.intersects(g)
            && (g & desk).width() >= g.width() * 2 / 3
            && (g & desk).height() >= g.height() * 2 / 3;
        if (!usable) {
            QSize def(1440, 860);
            def = def.boundedTo(desk.size() - QSize(24, 48));
            def = def.expandedTo(QSize(960, 600));
            setGeometry(QRect(QPoint(desk.center().x() - def.width() / 2,
                                     desk.center().y() - def.height() / 2), def));
        }
    }

    // startup document (no dialog — "New…" menu opens one on demand)
    createUntitled();
    applyShortcuts();
    if (m_colorPanel) m_colorPanel->refresh(); // sync wheel/swatches to default FG
    if (qEnvironmentVariableIsSet("PF_DEBUG_KEYS")) {
        // discriminator: bare action (no menu) + plain QShortcut
        QAction *dbgPlain = new QAction("dbgplain", this);
        dbgPlain->setShortcut(QKeySequence("X"));
        connect(dbgPlain, &QAction::triggered, this, []() { fprintf(stderr, "[FIRE] bare QAction plain X\n"); });
        addAction(dbgPlain);
        QAction *bare = new QAction("bare", this);
        bare->setShortcut(QKeySequence("Ctrl+K"));
        connect(bare, &QAction::triggered, this, []() { fprintf(stderr, "[FIRE] bare QAction Ctrl+K\n"); });
        addAction(bare);
        auto *sc = new QShortcut(QKeySequence("Ctrl+Y"), this);
        connect(sc, &QShortcut::activated, this, []() { fprintf(stderr, "[FIRE] QShortcut Ctrl+Y (activated)\n"); });
        connect(sc, &QShortcut::activatedAmbiguously, this, []() { fprintf(stderr, "[FIRE] QShortcut Ctrl+Y AMBIGUOUS\n"); });
        for (auto it = m_cmdActions.begin(); it != m_cmdActions.end(); ++it) {
            QAction *act = it.value();
            fprintf(stderr, "[SC] %-22s -> '%s'%s\n", qPrintable(it.key()),
                    qPrintable(act->shortcut().toString()),
                    act->isEnabled() ? "" : "  (DISABLED)");
            connect(act, &QAction::triggered, act, [act]() {
                fprintf(stderr, "[FIRE] menu action '%s' (%s)\n",
                        qPrintable(act->objectName()), qPrintable(act->shortcut().toString()));
            });
        }
        for (QAction *a : m_toolActions)
            fprintf(stderr, "[SC] tool:%-20s -> '%s'\n", qPrintable(a->text()), qPrintable(a->shortcut().toString()));
    }

    QTimer::singleShot(0, this, [this]() {
        m_statusEngine->setText(EngineDoc::rustInfo());
    });
}

MainWindow::~MainWindow() {
    QSettings s;
    s.setValue("win/geometry", saveGeometry());
    s.setValue("win/state", saveState());
}

// ---------------------------------------------------------------- helpers

EditorTab *MainWindow::currentTab() const {
    return qobject_cast<EditorTab *>(m_tabs->currentWidget());
}

QAction *MainWindow::addCmd(const QString &id, const QString &text, const QKeySequence &def,
                            const char *slot, const QString &menu, QAction::MenuRole) {
    QMenu *target = nullptr;
    if (!menu.isEmpty()) {
        target = menuBar()->findChild<QMenu *>(menu);
        if (!target) {
            target = menuBar()->addMenu(menu);
            target->setObjectName(menu);
        }
    }
    QAction *a = new QAction(text, target ? static_cast<QWidget *>(target) : static_cast<QWidget *>(this));
    a->setObjectName("cmd_" + id);
    a->setProperty("defaultShortcut", def.toString());
    if (slot) connect(a, SIGNAL(triggered()), this, slot);
    if (target) target->addAction(a);
    // Register the shortcut at window level as well — this is what actually
    // drives global keyboard dispatch for the command, independent of menu state.
    addAction(a);
    a->setShortcut(def);
    m_cmdActions[id] = a;
    return a;
}

QWidget *MainWindow::makePanel(const QString &title, QWidget *inner) {
    auto *frame = new QWidget;
    auto *lay = new QVBoxLayout(frame);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto *header = new QLabel(title);
    header->setProperty("class", "panelHeader");
    header->setStyleSheet(QStringLiteral("color:%1;font-weight:600;font-size:13px;padding:1px 2px;").arg(Theme::Text));
    lay->addWidget(header);
    lay->addWidget(inner, 1);
    frame->setStyleSheet(QStringLiteral("QWidget#panelCard{background:%1;border:1px solid %2;border-radius:10px;}")
                             .arg(Theme::PanelBg, Theme::Border));
    frame->setObjectName("panelCard");
    return frame;
}

// ---------------------------------------------------------------- menus

void MainWindow::buildMenus() {
    // NOTE: the 5th argument of addCmd() is the target menu's objectName.
    // Dropping it leaves the command action invisible (menu shows empty) —
    // this was the "dead top buttons" bug; every call must name its menu.
    // File
    menuBar()->addMenu(tr("&File"))->setObjectName("mFile");
    addCmd("file.new", tr("&New…"), QKeySequence::New, SLOT(newDocument()), "mFile");
    addCmd("file.open", tr("&Open…"), QKeySequence::Open, SLOT(openFile()), "mFile");
    addCmd("file.openRecent", tr("Open &Recent"), QKeySequence(), SLOT(openRecent()), "mFile");
    addCmd("file.save", tr("&Save Project (ORA)"), QKeySequence::Save, SLOT(saveDocument()), "mFile");
    addCmd("file.saveAs", tr("Save Project &As…"), QKeySequence::SaveAs, SLOT(saveDocumentAs()), "mFile");
    addCmd("file.export", tr("&Export Image…"), QKeySequence("Ctrl+E"), SLOT(exportImage()), "mFile");
    addCmd("file.importLayer", tr("Import Image as Layer…"), QKeySequence("Ctrl+Shift+O"), SLOT(openFile()), "mFile");

    // Edit
    menuBar()->addMenu(tr("&Edit"))->setObjectName("mEdit");
    addCmd("edit.undo", tr("&Undo"), QKeySequence::Undo, SLOT(undo()), "mEdit");
    addCmd("edit.redo", tr("&Redo"), QKeySequence::Redo, SLOT(redo()), "mEdit");
    addCmd("edit.copy", tr("Copy &Visible"), QKeySequence::Copy, SLOT(copyVisible()), "mEdit");
    addCmd("edit.paste", tr("&Paste as New Layer"), QKeySequence::Paste, SLOT(pasteClipboard()), "mEdit");
    addCmd("edit.clear", tr("Clear (Delete selection)"), QKeySequence("Delete"), SLOT(clearArea()), "mEdit");
    addCmd("edit.fillFg", tr("Fill with &Foreground"), QKeySequence("Alt+Backspace"), SLOT(fillFg()), "mEdit");
    addCmd("edit.fillBg", tr("Fill with Back&ground"), QKeySequence("Ctrl+Backspace"), SLOT(fillBg()), "mEdit");
    addCmd("edit.strokeSel", tr("&Stroke Selection…"), QKeySequence(), SLOT(strokeSelection()), "mEdit");
    addCmd("edit.shortcuts", tr("Keyboard &Shortcuts…"), QKeySequence("Ctrl+Alt+K"), SLOT(preferences()), "mEdit");

    // Image
    menuBar()->addMenu(tr("&Image"))->setObjectName("mImage");
    addCmd("image.size", tr("Image &Size…"), QKeySequence("Ctrl+Alt+I"), SLOT(imageSize()), "mImage");
    addCmd("image.canvas", tr("Canvas Si&ze…"), QKeySequence("Ctrl+Alt+C"), SLOT(canvasSize()), "mImage");
    addCmd("image.cropSel", tr("Crop to &Selection"), QKeySequence(), SLOT(cropToSelection()), "mImage");
    addCmd("image.trim", tr("&Trim Transparent Edges"), QKeySequence(), SLOT(trimTransparent()), "mImage");
    addCmd("image.rot90", tr("Rotate 90° Clockwise"), QKeySequence("Ctrl+R"), SLOT(rotateImage90()), "mImage");
    addCmd("image.rot180", tr("Rotate 180°"), QKeySequence(), SLOT(rotateImage180()), "mImage");
    addCmd("image.rot270", tr("Rotate 90° Counter-Clockwise"), QKeySequence("Ctrl+Shift+R"), SLOT(rotateImage270()), "mImage");
    addCmd("image.flipH", tr("Flip &Horizontally"), QKeySequence(), SLOT(flipImageH()), "mImage");
    addCmd("image.flipV", tr("Flip &Vertically"), QKeySequence(), SLOT(flipImageV()), "mImage");

    // Layer
    menuBar()->addMenu(tr("&Layer"))->setObjectName("mLayer");
    addCmd("layer.new", tr("&New Layer"), QKeySequence("Ctrl+Shift+N"), SLOT(newLayer()), "mLayer");
    addCmd("layer.newGroup", tr("New &Group"), QKeySequence("Ctrl+G"), SLOT(newGroup()), "mLayer");
    addCmd("layer.duplicate", tr("&Duplicate Layer"), QKeySequence("Ctrl+J"), SLOT(duplicateLayer()), "mLayer");
    addCmd("layer.delete", tr("Dele&te Layer"), QKeySequence(), SLOT(deleteLayer()), "mLayer");
    addCmd("layer.maskSel", tr("Add Layer Mask from Se&lection"), QKeySequence(), SLOT(addMaskFromSelection()), "mLayer");
    addCmd("layer.mergeDown", tr("&Merge Down"), QKeySequence(), SLOT(mergeDown()), "mLayer");
    addCmd("layer.flatten", tr("&Flatten Image"), QKeySequence("Ctrl+Shift+F"), SLOT(flattenImage()), "mLayer");
    addCmd("layer.up", tr("Move Layer &Up"), QKeySequence("Ctrl+]"), SLOT(layerUp()), "mLayer");
    addCmd("layer.down", tr("Move Layer &Down"), QKeySequence("Ctrl+["), SLOT(layerDown()), "mLayer");
    addCmd("layer.flipH", tr("Flip Layer Hori&zontally"), QKeySequence(), SLOT(flipLayerH()), "mLayer");
    addCmd("layer.flipV", tr("Flip Layer Verticall&y"), QKeySequence(), SLOT(flipLayerV()), "mLayer");

    // Select
    menuBar()->addMenu(tr("&Select"))->setObjectName("mSelect");
    addCmd("select.all", tr("Select &All"), QKeySequence::SelectAll, SLOT(selectAll()), "mSelect");
    addCmd("select.none", tr("&Deselect"), QKeySequence("Ctrl+D"), SLOT(deselect()), "mSelect");
    addCmd("select.invert", tr("&Inverse"), QKeySequence("Ctrl+Shift+I"), SLOT(invertSelection()), "mSelect");
    addCmd("select.feather", tr("&Feather…"), QKeySequence("Ctrl+Alt+D"), SLOT(featherSelection()), "mSelect");

    // Filter
    QMenu *filter = menuBar()->addMenu(tr("&Filter"));
    filter->setObjectName("mFilter");
    addCmd("filter.blur", tr("&Gaussian Blur…"), QKeySequence(), SLOT(filterBlur()), "mFilter");
    addCmd("filter.sharpen", tr("&Sharpen…"), QKeySequence(), SLOT(filterSharpen()), "mFilter");
    addCmd("filter.noise", tr("Add &Noise…"), QKeySequence(), SLOT(filterNoise()), "mFilter");
    addCmd("filter.pixelate", tr("&Pixelate…"), QKeySequence(), SLOT(filterPixelate()), "mFilter");
    addCmd("filter.twirl", tr("&Twirl…"), QKeySequence(), SLOT(filterTwirl()), "mFilter");
    addCmd("filter.wave", tr("&Wave…"), QKeySequence(), SLOT(filterWave()), "mFilter");
    addCmd("filter.edges", tr("Find &Edges"), QKeySequence(), SLOT(filterEdges()), "mFilter");
    addCmd("filter.emboss", tr("&Emboss…"), QKeySequence(), SLOT(filterEmboss()), "mFilter");
    addCmd("filter.vignette", tr("&Vignette…"), QKeySequence(), SLOT(filterVignette()), "mFilter");

    // Adjustments submenu inside Filter
    QMenu *adjust = filter->addMenu(tr("&Adjustments"));
    adjust->setObjectName("mAdjust");
    addCmd("adjust.bright", tr("&Brightness/Contrast…"), QKeySequence(), SLOT(adjustBrightnessContrast()), "mAdjust");
    addCmd("adjust.hue", tr("&Hue/Saturation…"), QKeySequence("Ctrl+U"), SLOT(adjustHueSat()), "mAdjust");
    addCmd("adjust.levels", tr("&Levels…"), QKeySequence("Ctrl+L"), SLOT(adjustLevels()), "mAdjust");
    addCmd("adjust.curves", tr("&Curves…"), QKeySequence("Ctrl+M"), SLOT(adjustCurves()), "mAdjust");
    addCmd("adjust.invert", tr("&Invert Colors"), QKeySequence("Ctrl+I"), SLOT(adjustInvert()), "mAdjust");
    addCmd("adjust.desat", tr("&Desaturate"), QKeySequence("Ctrl+Shift+U"), SLOT(adjustDesaturate()), "mAdjust");
    addCmd("adjust.auto", tr("&Auto Contrast"), QKeySequence(), SLOT(adjustAutoContrast()), "mAdjust");

    // View
    menuBar()->addMenu(tr("&View"))->setObjectName("mView");
    addCmd("view.zoomIn", tr("Zoom &In"), QKeySequence::ZoomIn, SLOT(zoomIn()), "mView");
    addCmd("view.zoomOut", tr("Zoom &Out"), QKeySequence::ZoomOut, SLOT(zoomOut()), "mView");
    addCmd("view.zoomFit", tr("&Fit on Screen"), QKeySequence("Ctrl+0"), SLOT(zoomFit()), "mView");
    addCmd("view.zoom100", tr("Actual &Pixels (100%)"), QKeySequence("Ctrl+1"), SLOT(zoom100()), "mView");

    // Window
    menuBar()->addMenu(tr("&Window"))->setObjectName("mWindow");
    addCmd("win.saveWorkspace", tr("&Save Workspace"), QKeySequence(), SLOT(saveWorkspace()), "mWindow");
    addCmd("win.resetWorkspace", tr("&Reset Workspace"), QKeySequence(), SLOT(resetWorkspace()), "mWindow");

    // Help
    menuBar()->addMenu(tr("&Help"))->setObjectName("mHelp");
    addCmd("help.about", tr("&About PixelForge Studio"), QKeySequence(), SLOT(about()), "mHelp");
}

// ---------------------------------------------------------------- top bar

void MainWindow::buildTopBar() {
    m_topBar = new QToolBar;
    m_topBar->setObjectName("topBar");
    m_topBar->setMovable(false);
    m_topBar->setFloatable(false);
    addToolBar(Qt::TopToolBarArea, m_topBar);

    auto *zoomBtn = new QToolButton;
    zoomBtn->setIcon(Icons::get(Icons::Zoom));
    zoomBtn->setToolTip(tr("Zoom out (Ctrl+scroll on canvas)"));
    connect(zoomBtn, &QToolButton::clicked, this, &MainWindow::zoomOut);
    m_topBar->addWidget(zoomBtn);

    // Zoom selector: presets + Fit + free numeric entry (editable).
    m_zoomCombo = new QComboBox;
    m_zoomCombo->setObjectName("zoomCombo");
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    for (const char *z : {"Fit", "25%", "33%", "50%", "67%", "100%", "150%", "200%", "300%", "400%"})
        m_zoomCombo->addItem(QString::fromLatin1(z));
    m_zoomCombo->setCurrentText(QStringLiteral("100%"));
    m_zoomCombo->setFixedWidth(86);
    m_zoomCombo->setToolTip(tr("Zoom level — pick a preset or type a percentage (1–3200)"));
    auto applyZoomText = [this](const QString &raw) {
        EditorTab *tab = currentTab();
        if (!tab) return;
        QString t = raw.trimmed();
        if (t.compare(QLatin1String("Fit"), Qt::CaseInsensitive) == 0) {
            zoomFit();
            return;
        }
        bool ok = false;
        int pct = t.remove('%').toInt(&ok);
        if (ok && pct >= 1 && pct <= 3200)
            tab->m_canvas->setZoomImmediate(pct / 100.0);
        else
            showZoom(tab->m_canvas->zoom()); // revert to current
    };
    connect(m_zoomCombo, &QComboBox::activated, this, [this, applyZoomText](int) {
        applyZoomText(m_zoomCombo->currentText());
    });
    if (auto *le = m_zoomCombo->lineEdit())
        connect(le, &QLineEdit::editingFinished, this, [this, applyZoomText]() {
            applyZoomText(m_zoomCombo->currentText());
        });
    m_topBar->addWidget(m_zoomCombo);

    m_topBar->addSeparator();

    auto *undoBtn = new QToolButton;
    undoBtn->setIcon(Icons::get(Icons::Undo));
    undoBtn->setToolTip(tr("Undo (Ctrl+Z)"));
    connect(undoBtn, &QToolButton::clicked, this, &MainWindow::undo);
    auto *redoBtn = new QToolButton;
    redoBtn->setIcon(Icons::get(Icons::Redo));
    redoBtn->setToolTip(tr("Redo (Ctrl+Y)"));
    connect(redoBtn, &QToolButton::clicked, this, &MainWindow::redo);
    m_topBar->addWidget(undoBtn);
    m_topBar->addWidget(redoBtn);

    QWidget *stretch = new QWidget;
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_topBar->addWidget(stretch);

    // right side: Flip canvas cluster + mirror + history + panels + help
    auto *flipBtn = new QToolButton;
    flipBtn->setText(tr("Flip canvas"));
    flipBtn->setIcon(Icons::get(Icons::FlipH));
    flipBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    flipBtn->setToolTip(tr("Flip entire canvas horizontally (all layers)"));
    connect(flipBtn, &QToolButton::clicked, this, &MainWindow::flipImageH);
    m_topBar->addWidget(flipBtn);

    auto *flipVBtn = new QToolButton;
    flipVBtn->setIcon(Icons::get(Icons::FlipV));
    flipVBtn->setToolTip(tr("Flip entire canvas vertically (all layers)"));
    connect(flipVBtn, &QToolButton::clicked, this, &MainWindow::flipImageV);
    m_topBar->addWidget(flipVBtn);

    m_topBar->addSeparator();

    auto *histBtn = new QToolButton;
    histBtn->setIcon(Icons::get(Icons::Clock));
    histBtn->setToolTip(tr("Toggle history panel"));
    histBtn->setCheckable(true);
    histBtn->setChecked(true);
    connect(histBtn, &QToolButton::toggled, this, &MainWindow::toggleHistoryPanel);
    m_topBar->addWidget(histBtn);

    auto *filterBtn = new QToolButton;
    filterBtn->setIcon(Icons::get(Icons::Filter));
    filterBtn->setToolTip(tr("Filters menu"));
    connect(filterBtn, &QToolButton::clicked, this, [this, filterBtn]() {
        if (QMenu *m = menuBar()->findChild<QMenu *>("mFilter"))
            m->exec(filterBtn->mapToGlobal(QPoint(0, filterBtn->height())));
    });
    m_topBar->addWidget(filterBtn);

    auto *helpBtn = new QToolButton;
    helpBtn->setIcon(Icons::get(Icons::Help));
    helpBtn->setToolTip(tr("About"));
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::about);
    m_topBar->addWidget(helpBtn);
}

// ---------------------------------------------------------------- tool strip

void MainWindow::buildToolStrip() {
    m_toolStrip = new QToolBar;
    m_toolStrip->setObjectName("toolStrip");
    m_toolStrip->setMovable(false);
    m_toolStrip->setFloatable(false);
    addToolBar(Qt::LeftToolBarArea, m_toolStrip);
    m_toolStrip->setOrientation(Qt::Vertical);
    m_toolStrip->setIconSize(QSize(20, 20));
    m_toolStrip->setFixedWidth(52);
    m_toolStrip->setStyleSheet(QStringLiteral(
        "QToolBar { padding: 5px 3px; spacing: 2px; background:%1; border-right:1px solid %2; }"
        "QToolButton { padding: 3px; border-radius: 8px; }"
        "QToolButton:checked { background:%3; border:1px solid #C4DDF7; }"
    ).arg(Theme::PanelBg, Theme::Border, Theme::AccentChip));

    struct TSpec { Icons::Tool icon; ToolId id; const char *name; const char *sc; };
    const TSpec tools[] = {
        { Icons::Move, ToolId::Move, "Move", "V" },
        { Icons::RectSelect, ToolId::RectSelect, "Rectangular Select", "M" },
        { Icons::EllipseSelect, ToolId::EllipticalSelect, "Elliptical Select", "J" },
        { Icons::Lasso, ToolId::Lasso, "Lasso Select", "L" },
        { Icons::Wand, ToolId::Wand, "Magic Wand", "W" },
        { Icons::Crop, ToolId::Crop, "Crop", "C" },
        { Icons::Eyedropper, ToolId::Eyedropper, "Eyedropper", "I" },
        { Icons::Brush, ToolId::Brush, "Brush", "B" },
        { Icons::Pencil, ToolId::Pencil, "Pencil", "N" },
        { Icons::Eraser, ToolId::Eraser, "Eraser", "E" },
        { Icons::Fill, ToolId::Fill, "Paint Bucket", "G" },
        { Icons::Gradient, ToolId::Gradient, "Gradient", "Shift+G" },
        { Icons::Text, ToolId::Text, "Text", "T" },
        { Icons::Shape, ToolId::Shape, "Shape", "U" },
        { Icons::Transform, ToolId::Transform, "Transform", "Ctrl+T" },
        { Icons::Perspective, ToolId::Perspective, "Perspective", "Ctrl+Shift+T" },
        { Icons::Hand, ToolId::Hand, "Hand", "H" },
        { Icons::Zoom, ToolId::Zoom, "Zoom", "Z" },
    };
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);
    for (const TSpec &t : tools) {
        QAction *a = new QAction(QIcon::fromTheme(QStringLiteral("")), QString::fromLatin1(t.name), this);
        a->setIcon(Icons::get(t.icon));
        a->setToolTip(QString::fromLatin1(t.name) + QStringLiteral(" (") + t.sc + QStringLiteral(")"));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(t.sc));
        a->setProperty("toolId", (int)t.id);
        connect(a, &QAction::triggered, this, [this, a]() { setTool(a); });
        group->addAction(a);
        m_toolStrip->addAction(a);
        m_toolActions << a;
        if (t.id == m_tool) a->setChecked(true);
    }
    m_toolStrip->addSeparator();
    // FG/BG color swatch widget at bottom of strip
    class FgBgPair : public QWidget {
    public:
        FgBgPair(MainWindow *m) : mw(m) { setFixedSize(46, 46); }
        MainWindow *mw;
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor("#C4CBD4"), 1));
            p.setBrush(mw->bgColor());
            p.drawRoundedRect(QRectF(12, 12, 30, 30), 5, 5);
            p.setBrush(mw->fgColor());
            p.drawRoundedRect(QRectF(4, 4, 30, 30), 5, 5);
        }
        void mousePressEvent(QMouseEvent *e) override {
            QRectF fgR(4, 4, 30, 30);
            QRectF bgR(12, 12, 30, 30);
            if (e->button() == Qt::LeftButton) {
                if (fgR.contains(e->position()) && !(bgR.contains(e->position()) && e->position().x() > 34)) {
                    QColor c = QColorDialog::getColor(mw->fgColor(), this, tr("Foreground color"));
                    if (c.isValid()) mw->setFgColor(c);
                } else if (bgR.contains(e->position())) {
                    QColor c = QColorDialog::getColor(mw->bgColor(), this, tr("Background color"));
                    if (c.isValid()) mw->setBgColor(c);
                }
            } else if (e->button() == Qt::RightButton) {
                mw->swapColors();
            }
        }
    };
    auto *pair = new FgBgPair(this);
    connect(this, &MainWindow::colorsChanged, pair, [pair]() { pair->update(); });
    m_toolStrip->addWidget(pair);
}

void MainWindow::setTool(QAction *act) {
    m_tool = (ToolId)act->property("toolId").toInt();
    for (QAction *a : m_toolActions) a->setChecked(a == act);
    if (EditorTab *tab = currentTab()) tab->m_canvas->updateCursorShape();
    if (m_tool == ToolId::Transform && currentTab()) currentTab()->m_canvas->enterTransformMode();
    if (m_tool == ToolId::Perspective && currentTab()) currentTab()->m_canvas->enterPerspectiveMode();
    buildToolOptionsBar(); // refresh contextual options
    emit toolChanged(m_tool);
}

// ---------------------------------------------------------------- docks

void MainWindow::buildDocks() {
    m_layersPanel = new LayersPanel(this);
    m_brushPanel = new BrushPanel(this);
    m_colorPanel = new ColorPanel(this);
    m_historyPanel = new HistoryPanel(this);

    // Column 1: Layers + Brush Settings
    auto *col1 = new QSplitter(Qt::Vertical);
    col1->addWidget(makePanel(tr("Layers"), m_layersPanel));
    col1->addWidget(makePanel(tr("Brush Settings"), m_brushPanel));
    col1->setStretchFactor(0, 3);
    col1->setStretchFactor(1, 2);
    col1->setChildrenCollapsible(false);

    // Column 2: Color Wheel + History
    auto *col2 = new QSplitter(Qt::Vertical);
    col2->addWidget(makePanel(tr("Color Wheel"), m_colorPanel));
    col2->addWidget(makePanel(tr("History"), m_historyPanel));
    col2->setStretchFactor(0, 3);
    col2->setStretchFactor(1, 2);
    col2->setChildrenCollapsible(false);

    m_dockLayers = new QDockWidget(tr("Layers & Brushes"), this);
    m_dockLayers->setObjectName("dockLayers");
    m_dockLayers->setWidget(col1);
    m_dockLayers->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_dockLayers->setTitleBarWidget(new QWidget()); // clean look, panels have their own headers

    m_dockColor = new QDockWidget(tr("Color & History"), this);
    m_dockColor->setObjectName("dockColor");
    m_dockColor->setWidget(col2);
    m_dockColor->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_dockColor->setTitleBarWidget(new QWidget());

    addDockWidget(Qt::RightDockWidgetArea, m_dockLayers);
    splitDockWidget(m_dockLayers, m_dockColor, Qt::Horizontal);

    m_dockLayers->setMinimumWidth(240);
    m_dockColor->setMinimumWidth(240);
    // resizeDocks only sticks once the layout is active — defer until shown
    QTimer::singleShot(0, this, [this]() {
        resizeDocks({m_dockLayers, m_dockColor}, {290, 290}, Qt::Horizontal);
    });
}

void MainWindow::toggleHistoryPanel(bool on) {
    // History lives in the right column's "History" card; toggle the whole
    // card (header + list) so the toggle button has a real visible effect.
    if (QWidget *card = m_historyPanel ? m_historyPanel->parentWidget() : nullptr)
        card->setVisible(on);
}

// ---------------------------------------------------------------- tool options bar

void MainWindow::buildToolOptionsBar() {
    if (!m_toolOptions) {
        m_toolOptions = new QToolBar;
        m_toolOptions->setObjectName("toolOptions");
        m_toolOptions->setMovable(false);
        m_toolOptions->setFloatable(false);
        addToolBar(Qt::TopToolBarArea, m_toolOptions); // below the main top bar
    }
    m_toolOptions->clear();
    auto addLabel = [this](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(Theme::TextSecondary));
        m_toolOptions->addWidget(l);
    };

    switch (m_tool) {
    case ToolId::Wand: {
        addLabel(tr("Tolerance"));
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(0, 255); s->setValue(m_wand.tolerance); s->setMaximumWidth(140);
        connect(s, &QSlider::valueChanged, this, [this](int v) { m_wand.tolerance = v; });
        m_toolOptions->addWidget(s);
        auto *cb = new QCheckBox(tr("Contiguous"));
        cb->setChecked(m_wand.contiguous);
        connect(cb, &QCheckBox::toggled, this, [this](bool on) { m_wand.contiguous = on; });
        auto *cc = new QCheckBox(tr("Sample composite"));
        cc->setChecked(m_wand.sampleComposite);
        connect(cc, &QCheckBox::toggled, this, [this](bool on) { m_wand.sampleComposite = on; });
        m_toolOptions->addWidget(cb);
        m_toolOptions->addWidget(cc);
        break;
    }
    case ToolId::Fill: {
        addLabel(tr("Tolerance"));
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(0, 255); s->setValue(m_fill.tolerance); s->setMaximumWidth(140);
        connect(s, &QSlider::valueChanged, this, [this](int v) { m_fill.tolerance = v; });
        m_toolOptions->addWidget(s);
        auto *cb = new QCheckBox(tr("Contiguous"));
        cb->setChecked(m_fill.contiguous);
        connect(cb, &QCheckBox::toggled, this, [this](bool on) { m_fill.contiguous = on; });
        m_toolOptions->addWidget(cb);
        break;
    }
    case ToolId::Gradient: {
        auto *kind = new QComboBox;
        kind->addItems({tr("Linear"), tr("Radial")});
        kind->setCurrentIndex((int)m_gradKind);
        connect(kind, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_gradKind = (GradKind)i;
        });
        auto *target = new QComboBox;
        target->addItems({tr("Foreground → Background"), tr("Foreground → Transparent"), tr("Background → Foreground")});
        target->setCurrentIndex((int)m_gradTarget);
        connect(target, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_gradTarget = (GradTarget)i;
        });
        addLabel(tr("Type"));
        m_toolOptions->addWidget(kind);
        addLabel(tr("Colors"));
        m_toolOptions->addWidget(target);
        break;
    }
    case ToolId::Shape: {
        auto *kind = new QComboBox;
        kind->addItems({tr("Line"), tr("Rectangle"), tr("Ellipse"), tr("Polygon")});
        kind->setCurrentIndex(m_shape.kind);
        connect(kind, &QComboBox::currentIndexChanged, this, [this](int i) { m_shape.kind = i; });
        addLabel(tr("Shape"));
        m_toolOptions->addWidget(kind);
        auto *w = new QDoubleSpinBox;
        w->setRange(0.5, 100); w->setValue(m_shape.strokeWidth); w->setSingleStep(0.5);
        connect(w, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_shape.strokeWidth = v; });
        addLabel(tr("Stroke width"));
        m_toolOptions->addWidget(w);
        auto *stroke = new QCheckBox(tr("Stroke (FG)"));
        stroke->setChecked(m_shape.hasStroke);
        connect(stroke, &QCheckBox::toggled, this, [this](bool on) { m_shape.hasStroke = on; });
        auto *fill = new QCheckBox(tr("Fill (BG)"));
        fill->setChecked(m_shape.hasFill);
        connect(fill, &QCheckBox::toggled, this, [this](bool on) { m_shape.hasFill = on; });
        m_toolOptions->addWidget(stroke);
        m_toolOptions->addWidget(fill);
        break;
    }
    case ToolId::Brush:
    case ToolId::Pencil:
    case ToolId::Eraser: {
        addLabel(tr("Size"));
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(1, 400); s->setValue(m_brush.size); s->setMaximumWidth(160);
        auto *v = new QLabel(QString::number(m_brush.size));
        connect(s, &QSlider::valueChanged, this, [this, v](int val) {
            m_brush.size = val;
            v->setNum(val);
        });
        m_toolOptions->addWidget(s);
        m_toolOptions->addWidget(v);
        break;
    }
    case ToolId::Crop: {
        addLabel(tr("Drag a crop region, then Apply (Enter) or Cancel (Esc)."));
        break;
    }
    case ToolId::Transform: {
        addLabel(tr("Drag inside to move · corners to scale · top handle to rotate · Enter to apply · Esc to cancel"));
        break;
    }
    case ToolId::Perspective: {
        addLabel(tr("Drag the 4 corners · Enter to apply · Esc to cancel"));
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- status bar

void MainWindow::buildBottomBar() {
    m_bottomBar = new QToolBar;
    m_bottomBar->setObjectName("bottomBar");
    m_bottomBar->setMovable(false);
    m_bottomBar->setFloatable(false);
    addToolBar(Qt::BottomToolBarArea, m_bottomBar);

    auto *hint = new QLabel(tr("Brush presets"));
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:12px;padding:0 6px;").arg(Theme::TextSecondary));
    m_bottomBar->addWidget(hint);

    struct P { Icons::Tool icon; const char *name; int idx; };
    const P presets[] = {
        { Icons::SoftRound, "Soft Round", 0 },
        { Icons::HardRound, "Hard Round", 1 },
        { Icons::TaperedInk, "Tapered Ink", 2 },
        { Icons::Marker, "Marker", 3 },
        { Icons::Airbrush, "Airbrush", 4 },
    };
    for (const P &pr : presets) {
        auto *b = new QToolButton;
        b->setIcon(Icons::get(pr.icon));
        b->setToolTip(tr("Preset: %1").arg(pr.name));
        b->setAutoRaise(true);
        b->setIconSize(QSize(30, 30));
        connect(b, &QToolButton::clicked, this, [this, idx = pr.idx]() {
            m_brushPanel->applyPreset(idx);
            // switch to brush tool to use the preset
            for (QAction *a : m_toolActions)
                if (a->property("toolId").toInt() == (int)ToolId::Brush) a->trigger();
        });
        m_bottomBar->addWidget(b);
    }
    QWidget *stretch = new QWidget;
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_bottomBar->addWidget(stretch);
    m_bottomBar->setStyleSheet(QStringLiteral(
        "QToolBar { background:%1; border-top:1px solid %2; padding: 4px 8px; spacing: 4px; }"
        "QToolButton:hover { background:%3; border-radius:8px; }"
    ).arg(Theme::BottomBar, Theme::Border, Theme::Hover));
}

void MainWindow::buildStatusBar() {
    m_statusPos = new QLabel(QStringLiteral("    "));
    m_statusDoc = new QLabel(QStringLiteral("    "));
    m_statusEngine = new QLabel();
    m_statusEngine->setStyleSheet(QStringLiteral("color:%1;font-size:11px;").arg(Theme::TextSecondary));
    statusBar()->addWidget(m_statusPos);
    statusBar()->addWidget(m_statusDoc);
    statusBar()->addPermanentWidget(m_statusEngine, 1);
    m_statusTimer = new QTimer(this);
}

void MainWindow::showCursorPos(const QPointF &p) {
    m_statusPos->setText(tr("Cursor: %1, %2 px").arg((int)p.x()).arg((int)p.y()));
}

void MainWindow::showZoom(double zoom) {
    if (!m_zoomCombo) return;
    QString t = QString::number(qRound(zoom * 100)) + QStringLiteral("%");
    if (m_zoomCombo->currentText() != t) {
        m_zoomCombo->blockSignals(true);
        m_zoomCombo->setCurrentText(t);
        m_zoomCombo->blockSignals(false);
    }
}

void MainWindow::showStatus(const QString &msg) {
    statusBar()->showMessage(msg, 4000);
}

void MainWindow::applyPickedColor(const QColor &c, bool background) {
    if (background) setBgColor(c);
    else setFgColor(c);
}

// ---------------------------------------------------------------- colors

void MainWindow::setFgColor(const QColor &c) {
    if (!c.isValid()) return;
    m_fg = c;
    if (m_colorPanel) m_colorPanel->refresh();
    if (m_brushPanel) m_brushPanel->refresh();
    emit colorsChanged();
}

void MainWindow::setBgColor(const QColor &c) {
    if (!c.isValid()) return;
    m_bg = c;
    emit colorsChanged();
}

void MainWindow::swapColors() { std::swap(m_fg, m_bg); emit colorsChanged(); if (m_colorPanel) m_colorPanel->refresh(); }
void MainWindow::resetColors() { m_fg = QColor(30, 30, 30); m_bg = Qt::white; emit colorsChanged(); if (m_colorPanel) m_colorPanel->refresh(); }

// ---------------------------------------------------------------- file ops

void MainWindow::createUntitled() {
    PFDoc doc = pf_document_new(1280, 800, 0, 255, 255, 255);
    if (!doc) return;
    auto *tab = new EditorTab(this, doc, tr("Untitled"), this);
    int idx = m_tabs->addTab(tab, QStringLiteral("Untitled"));
    m_tabs->setCurrentIndex(idx);
    refreshAfterEdit(tab, true);
}

void MainWindow::newDocument() {
    NewDocDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    PFDoc doc = pf_document_new(dlg.width(), dlg.height(), dlg.fillMode(),
                                dlg.customColor().red(), dlg.customColor().green(), dlg.customColor().blue());
    if (!doc) {
        QMessageBox::warning(this, tr("PixelForge Studio"), EngineDoc::lastError());
        return;
    }
    auto *tab = new EditorTab(this, doc, tr("Untitled"), this);
    int idx = m_tabs->addTab(tab, QStringLiteral("Untitled"));
    m_tabs->setCurrentIndex(idx);
    refreshAfterEdit(tab, true);
}

void MainWindow::openFile() {
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Open Images"),
        QString(),
        tr("All Supported (%1);;PNG (*.png);;JPEG (*.jpg *.jpeg);;WebP (*.webp);;GIF (*.gif);;BMP (*.bmp);;TIFF (*.tif *.tiff);;SVG (*.svg);;OpenRaster (*.ora);;Photoshop (*.psd)")
            .arg(QStringLiteral("*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff *.svg *.ora *.psd")));
    for (const QString &f : files) openPath(f);
}

void MainWindow::openRecent() {
    QSettings s;
    QStringList recents = s.value("recent/files").toStringList();
    if (recents.isEmpty()) { showStatus(tr("No recent files yet.")); return; }
    QMenu m(this);
    for (const QString &f : recents) {
        QAction *a = m.addAction(QFileInfo(f).fileName());
        connect(a, &QAction::triggered, this, [this, f]() { openPath(f); });
    }
    m.exec(QCursor::pos());
}

void MainWindow::addRecent(const QString &path) {
    QSettings s;
    QStringList recents = s.value("recent/files").toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 10) recents.removeLast();
    s.setValue("recent/files", recents);
}

void MainWindow::balanceDocks() {
    resizeDocks({m_dockLayers, m_dockColor}, {290, 290}, Qt::Horizontal);
}

void MainWindow::setCurrentTabTo(EditorTab *tab) {
    int i = m_tabs->indexOf(tab);
    if (i >= 0) m_tabs->setCurrentIndex(i);
}

void MainWindow::triggerToolByName(const QString &name) {
    for (QAction *a : m_toolActions) {
        if (a->text() == name) {
            if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[MW] triggering tool '%s'\n", qPrintable(name));
            a->trigger(); return;
        }
    }
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[MW] tool '%s' NOT FOUND\n", qPrintable(name));
}

void MainWindow::openPath(const QString &path) {
    PFDoc doc = pf_document_open(path.toUtf8().constData());
    if (!doc) {
        QMessageBox::warning(this, tr("Open failed"),
            tr("Could not open\n%1\n\n%2").arg(path, EngineDoc::lastError()));
        return;
    }
    auto *tab = new EditorTab(this, doc, QFileInfo(path).fileName(), this);
    int idx = m_tabs->addTab(tab, QFileInfo(path).fileName());
    m_tabs->setCurrentIndex(idx);
    addRecent(path);
    refreshAfterEdit(tab, true);
}

void MainWindow::saveDocument() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    QString path = tab->m_doc.path();
    if (path.isEmpty() || !path.endsWith(".ora", Qt::CaseInsensitive)) {
        saveDocumentAs();
        return;
    }
    if (pf_save_ora(tab->m_doc.handle(), path.toUtf8().constData()) == 0)
        tab->markSaved();
    else
        QMessageBox::warning(this, tr("Save failed"), EngineDoc::lastError());
}

void MainWindow::saveDocumentAs() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"),
        QString(), tr("OpenRaster project (*.ora)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".ora", Qt::CaseInsensitive)) path += ".ora";
    if (pf_save_ora(tab->m_doc.handle(), path.toUtf8().constData()) == 0)
        tab->markSaved();
    else
        QMessageBox::warning(this, tr("Save failed"), EngineDoc::lastError());
}

void MainWindow::exportImage() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    ExportDialog dlg(this, w, h);
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[ST] exportImage: opening ExportDialog\n");
    if (dlg.exec() != QDialog::Accepted) return;
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[ST] exportImage: dialog accepted\n");
    QString suggested = tab->title();
    suggested = suggested.remove(QRegularExpression("\\.(ora|png|jpg|jpeg|webp|gif|bmp|tif|tiff|psd|svg)$", QRegularExpression::CaseInsensitiveOption));
    QString ext = QStringLiteral(".png");
    switch (dlg.format()) {
    case 1: ext = ".jpg"; break; case 2: ext = ".webp"; break; case 3: ext = ".bmp"; break;
    case 4: ext = ".tif"; break; case 5: ext = ".gif"; break; default: break;
    }
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[ST] exportImage: opening save dialog\n");
    QString path = QFileDialog::getSaveFileName(this, tr("Export As"), suggested + ext,
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.gif)"));
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[ST] exportImage: save path='%s'\n", qPrintable(path));
    if (path.isEmpty()) return;
    int rc = pf_export(tab->m_doc.handle(), path.toUtf8().constData(), dlg.format(), dlg.quality(),
                       dlg.lossless() ? 1 : 0, dlg.background().red(), dlg.background().green(),
                       dlg.background().blue(), dlg.scalePct());
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[ST] exportImage: pf_export rc=%d\n", rc);
    if (rc != 0) {
        QMessageBox::warning(this, tr("Export failed"), EngineDoc::lastError());
    } else {
        showStatus(tr("Exported %1").arg(path));
    }
}

void MainWindow::closeTab(int index) {
    auto *tab = qobject_cast<EditorTab *>(m_tabs->widget(index));
    if (!tab) return;
    if (tab->isModified()) {
        int ret = QMessageBox::question(this, tr("Save changes?"),
            tr("“%1” has unsaved changes. Save as OpenRaster project before closing?").arg(tab->title()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (ret == QMessageBox::Save) { saveDocumentAs(); }
        else if (ret == QMessageBox::Cancel) return;
    }
    m_tabs->removeTab(index);
    delete tab;
    refreshAfterEdit(currentTab(), true);
}

// ---------------------------------------------------------------- edit ops

void MainWindow::undo() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    if (tab->m_doc.undo()) tab->afterStructureEdit();
    else showStatus(tr("Nothing to undo"));
}

void MainWindow::redo() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    if (tab->m_doc.redo()) tab->afterStructureEdit();
    else showStatus(tr("Nothing to redo"));
}

void MainWindow::copyVisible() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    QImage img = tab->m_doc.compositeFull();
    QGuiApplication::clipboard()->setImage(img);
    showStatus(tr("Copied composite to clipboard"));
}

void MainWindow::pasteClipboard() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) { showStatus(tr("Clipboard has no image")); return; }
    tab->m_doc.addLayerFromImage(img.convertToFormat(QImage::Format_RGBA8888), tr("Pasted"), 0, 0);
    tab->afterStructureEdit();
}

void MainWindow::clearArea() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_selection_clear(tab->m_doc.handle());
    tab->afterEdit();
}

void MainWindow::fillFg() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_fill_selection(tab->m_doc.handle(), m_fg.red(), m_fg.green(), m_fg.blue(), 255);
    tab->afterEdit();
}

void MainWindow::fillBg() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_fill_selection(tab->m_doc.handle(), m_bg.red(), m_bg.green(), m_bg.blue(), 255);
    tab->afterEdit();
}

void MainWindow::strokeSelection() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    bool ok = false;
    int w = QInputDialog::getInt(this, tr("Stroke Selection"), tr("Stroke width (px):"), 4, 1, 100, 1, &ok);
    if (!ok) return;
    pf_selection_stroke(tab->m_doc.handle(), m_fg.red(), m_fg.green(), m_fg.blue(), 255, w);
    tab->afterEdit();
}

void MainWindow::preferences() {
    ShortcutsDialog dlg(this);
    dlg.exec();
}

// ---------------------------------------------------------------- image ops

void MainWindow::imageSize() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    ImageSizeDialog dlg(this, w, h);
    if (dlg.exec() != QDialog::Accepted) return;
    if (pf_image_resize(tab->m_doc.handle(), dlg.newWidth(), dlg.newHeight(), dlg.resample()) == 0) {
        tab->m_canvas->fitToWindow();
        tab->afterStructureEdit();
    } else showStatus(EngineDoc::lastError());
}

void MainWindow::canvasSize() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    CanvasSizeDialog dlg(this, w, h);
    if (dlg.exec() != QDialog::Accepted) return;
    if (pf_canvas_resize(tab->m_doc.handle(), dlg.newWidth(), dlg.newHeight(), dlg.anchor()) == 0) {
        tab->m_canvas->fitToWindow();
        tab->afterStructureEdit();
    } else showStatus(EngineDoc::lastError());
}

void MainWindow::cropToSelection() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    int x, y; uint32_t w, h;
    if (pf_selection_bounds(tab->m_doc.handle(), &x, &y, &w, &h) == 1) {
        pf_image_crop(tab->m_doc.handle(), x, y, w, h);
        tab->m_canvas->fitToWindow();
        tab->afterStructureEdit();
    } else showStatus(tr("No selection to crop to"));
}

void MainWindow::trimTransparent() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    if (pf_image_trim(tab->m_doc.handle()) == 0) {
        tab->m_canvas->fitToWindow();
        tab->afterStructureEdit();
    } else showStatus(EngineDoc::lastError());
}

void MainWindow::rotateImage90() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_image_rotate(tab->m_doc.handle(), 90);
    tab->afterStructureEdit();
}
void MainWindow::rotateImage180() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_image_rotate(tab->m_doc.handle(), 180);
    tab->afterStructureEdit();
}
void MainWindow::rotateImage270() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_image_rotate(tab->m_doc.handle(), 270);
    tab->afterStructureEdit();
}
void MainWindow::flipImageH() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_image_flip(tab->m_doc.handle(), 1);
    tab->afterStructureEdit();
}
void MainWindow::flipImageV() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_image_flip(tab->m_doc.handle(), 0);
    tab->afterStructureEdit();
}
void MainWindow::flipLayerH() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_layer_flip(tab->m_doc.handle(), tab->m_doc.activeLayer(), 1);
    tab->afterEdit();
}
void MainWindow::flipLayerV() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_layer_flip(tab->m_doc.handle(), tab->m_doc.activeLayer(), 0);
    tab->afterEdit();
}

// ---------------------------------------------------------------- layer ops

void MainWindow::newLayer() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.addLayer(0, tr("Layer"));
    tab->afterStructureEdit();
}
void MainWindow::newGroup() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.addLayer(1, tr("Group"));
    tab->afterStructureEdit();
}
void MainWindow::duplicateLayer() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.duplicateLayer(tab->m_doc.activeLayer());
    tab->afterStructureEdit();
}
void MainWindow::deleteLayer() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.removeLayer(tab->m_doc.activeLayer());
    tab->afterStructureEdit();
}
void MainWindow::mergeDown() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.mergeDown(tab->m_doc.activeLayer());
    tab->afterStructureEdit();
}
void MainWindow::flattenImage() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.flatten();
    tab->afterStructureEdit();
}
static int rootIndexOf(EditorTab *tab, uint64_t id) {
    QString json = tab->m_doc.layersJson();
    int sep = json.lastIndexOf('|');
    QJsonDocument doc = QJsonDocument::fromJson(json.left(sep).toUtf8());
    if (!doc.isObject()) return -1;
    QJsonArray arr = doc.object()["children"].toArray();
    for (int i = 0; i < arr.size(); ++i)
        if (arr[i].toObject()["id"].toVariant().toULongLong() == id) return i;
    return -1;
}

void MainWindow::layerUp() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    int idx = rootIndexOf(tab, tab->m_doc.activeLayer());
    if (idx > 0) {
        pf_layer_move(tab->m_doc.handle(), tab->m_doc.activeLayer(), 0, idx - 1);
        tab->afterStructureEdit();
    }
}
void MainWindow::layerDown() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    QString json = tab->m_doc.layersJson();
    int sep = json.lastIndexOf('|');
    QJsonDocument doc = QJsonDocument::fromJson(json.left(sep).toUtf8());
    int count = doc.isObject() ? doc.object()["children"].toArray().size() : 0;
    int idx = rootIndexOf(tab, tab->m_doc.activeLayer());
    if (idx >= 0 && idx + 1 < count) {
        pf_layer_move(tab->m_doc.handle(), tab->m_doc.activeLayer(), 0, idx + 1);
        tab->afterStructureEdit();
    }
}
void MainWindow::addMaskFromSelection() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    tab->m_doc.addMask(tab->m_doc.activeLayer(), true);
    tab->afterStructureEdit();
}

// ---------------------------------------------------------------- selection ops

void MainWindow::selectAll() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_selection_all(tab->m_doc.handle());
    tab->m_canvas->selectionChanged();
    tab->afterEdit();
}
void MainWindow::deselect() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_selection_none(tab->m_doc.handle());
    tab->m_canvas->selectionChanged();
    tab->afterEdit();
}
void MainWindow::invertSelection() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_selection_invert(tab->m_doc.handle());
    tab->m_canvas->selectionChanged();
    tab->afterEdit();
}
void MainWindow::featherSelection() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    bool ok = false;
    double r = QInputDialog::getDouble(this, tr("Feather Selection"), tr("Feather radius (px):"), 6.0, 0.5, 200.0, 1, &ok);
    if (!ok) return;
    pf_selection_feather(tab->m_doc.handle(), r);
    tab->m_canvas->selectionChanged();
    tab->afterEdit();
}

// ---------------------------------------------------------------- adjustments

void MainWindow::adjustBrightnessContrast() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Brightness / Contrast"),
        {{"Brightness", -100, 100, 1, 0, 0, true}, {"Contrast", -100, 100, 1, 0, 0, true}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_adjust_brightness_contrast(t->m_doc.handle(), v[0], v[1]);
        });
    dlg.exec();
}

void MainWindow::adjustHueSat() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    HueSatDialog dlg(this, tab);
    dlg.exec();
}

void MainWindow::adjustLevels() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    LevelsDialog dlg(this, tab);
    dlg.exec();
}

void MainWindow::adjustCurves() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    CurvesDialog dlg(this, tab);
    dlg.exec();
}

void MainWindow::adjustInvert() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_adjust_invert(tab->m_doc.handle());
    tab->afterEdit();
}
void MainWindow::adjustDesaturate() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_adjust_desaturate(tab->m_doc.handle());
    tab->afterEdit();
}
void MainWindow::adjustAutoContrast() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    pf_adjust_auto_contrast(tab->m_doc.handle());
    tab->afterEdit();
}

// ---------------------------------------------------------------- filters

void MainWindow::filterBlur() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Gaussian Blur"),
        {{"Radius (px)", 0.5, 60, 0.5, 4, 1, false}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_blur(t->m_doc.handle(), v[0], 1);
        });
    dlg.exec();
}

void MainWindow::filterSharpen() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Sharpen"),
        {{"Amount", 0.0, 5.0, 0.1, 1.0, 1, false}, {"Radius (px)", 0.5, 20, 0.5, 2, 1, false}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_sharpen(t->m_doc.handle(), v[0], v[1]);
        });
    dlg.exec();
}

void MainWindow::filterNoise() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Add Noise"),
        {{"Amount", 0, 128, 1, 24, 0, true}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_noise(t->m_doc.handle(), (uint8_t)v[0], 0);
        });
    dlg.exec();
}

void MainWindow::filterPixelate() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Pixelate"),
        {{"Block size (px)", 2, 128, 1, 10, 0, true}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_pixelate(t->m_doc.handle(), (uint32_t)v[0]);
        });
    dlg.exec();
}

void MainWindow::filterTwirl() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    double maxdim = (double)qMax(w, h);
    AdjustDialog dlg(this, tab, tr("Twirl"),
        {{"Angle (°)", -720.0, 720.0, 5.0, 90.0, 0, true},
         {"Radius (px)", 10.0, maxdim, 10.0, maxdim * 0.7, 0, true}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_twirl(t->m_doc.handle(), v[0], v[1]);
        });
    dlg.exec();
}

void MainWindow::filterWave() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Wave"),
        {{"Amplitude", 1, 100, 1, 12, 0, true}, {"Wavelength (px)", 4, 400, 1, 80, 0, true}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_wave(t->m_doc.handle(), v[0], v[1]);
        });
    dlg.exec();
}

void MainWindow::filterEdges() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Find Edges"),
        {{"Strength", 0.1, 4.0, 0.1, 1.0, 1, false}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_edges(t->m_doc.handle(), v[0]);
        });
    dlg.exec();
}

void MainWindow::filterEmboss() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Emboss"),
        {{"Strength", 0.1, 4.0, 0.1, 1.0, 1, false}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_emboss(t->m_doc.handle(), v[0]);
        });
    dlg.exec();
}

void MainWindow::filterVignette() {
    EditorTab *tab = currentTab();
    if (!tab) return;
    AdjustDialog dlg(this, tab, tr("Vignette"),
        {{"Amount", 0.0, 1.0, 0.05, 0.5, 2, false}},
        [](EditorTab *t, const QVector<double> &v) {
            pf_filter_vignette(t->m_doc.handle(), v[0]);
        });
    dlg.exec();
}

// ---------------------------------------------------------------- view ops

void MainWindow::zoomIn() { if (currentTab()) currentTab()->m_canvas->zoomIn(); }
void MainWindow::zoomOut() { if (currentTab()) currentTab()->m_canvas->zoomOut(); }
void MainWindow::zoomFit() { if (currentTab()) currentTab()->m_canvas->fitToWindow(); }
void MainWindow::zoom100() { if (currentTab()) currentTab()->m_canvas->setZoomImmediate(1.0); if (currentTab()) currentTab()->m_canvas->update(); }

// ---------------------------------------------------------------- workspace

void MainWindow::saveWorkspace() {
    QSettings s;
    s.setValue("win/geometry", saveGeometry());
    s.setValue("win/state", saveState());
    showStatus(tr("Workspace saved"));
}

void MainWindow::resetWorkspace() {
    QSettings s;
    s.remove("win/geometry");
    s.remove("win/state");
    showStatus(tr("Workspace reset — restart to apply"));
}

void MainWindow::about() {
    AboutDialog dlg(this);
    dlg.exec();
}

// ---------------------------------------------------------------- refresh

void MainWindow::refreshAfterEdit(EditorTab *tab, bool structureChanged) {
    if (m_tabs->currentWidget() != tab && tab) {
        // still update title
        updateTabTitle(tab);
    }
    if (!tab) {
        m_layersPanel->refresh(nullptr);
        m_historyPanel->refresh(nullptr);
        updateTabTitle(nullptr);
        return;
    }
    updateTabTitle(tab);
    m_layersPanel->refresh(tab);
    m_historyPanel->refresh(tab);
    if (tab->m_canvas) tab->m_canvas->update();
    uint32_t w, h;
    tab->m_doc.size(w, h);
    m_statusDoc->setText(tr("   %1 × %2 px").arg(w).arg(h));
}

void MainWindow::updateTabTitle(EditorTab *tab) {
    if (!tab) {
        setWindowTitle(tr("PixelForge Studio"));
        return;
    }
    int idx = m_tabs->indexOf(tab);
    if (idx >= 0) {
        QString t = tab->title() + (tab->isModified() ? QStringLiteral(" ●") : QString());
        if (m_tabs->tabText(idx) != t) m_tabs->setTabText(idx, t);
    }
    QString appName = tr("PixelForge Studio");
    setWindowTitle(tab->title() + (tab->isModified() ? QStringLiteral(" ● — ") : QStringLiteral(" — ")) + appName);
}

// ---------------------------------------------------------------- shortcuts

void MainWindow::applyShortcuts() {
    QSettings s;
    for (auto it = m_cmdActions.begin(); it != m_cmdActions.end(); ++it) {
        QString def = it.value()->property("defaultShortcut").toString();
        QString ks = s.value("shortcuts/cmd_" + it.key(), def).toString();
        it.value()->setShortcut(QKeySequence(ks));
    }
    // tool shortcuts (not customizable in v1 — they belong to exclusive action group)
    for (QAction *a : m_toolActions) {
        a->setShortcut(a->toolTip().section(" (", 1).chopped(1));
    }
}
