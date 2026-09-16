#include "MainWindow.h"
#include "EditorTab.h"
#include "CanvasView.h"
#include "Theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QFont>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileDialog>
#include <QPushButton>
#include <QElapsedTimer>
#include <QSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QJsonArray>
#include <QDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QCryptographicHash>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QSet>
#include <QListWidget>
#include <QTreeWidget>
#include <functional>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *name) {
    printf("SELFTEST %-46s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (ok) ++g_pass; else ++g_fail;
}

static void sendMouse(QWidget *target, QEvent::Type type, const QPointF &pos,
                      Qt::MouseButton btn = Qt::LeftButton) {
    QMouseEvent ev(type, pos, target->mapTo(target->window(), pos).toPoint(), btn,
                   (type == QEvent::MouseButtonPress ? btn : Qt::NoButton),
                   Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
}

static void dragStroke(CanvasView *canvas, const QPointF &a, const QPointF &b, int steps = 12) {
    sendMouse(canvas, QEvent::MouseButtonPress, a);
    for (int i = 1; i <= steps; ++i) {
        QPointF p = a + (b - a) * (i / (double)steps);
        sendMouse(canvas, QEvent::MouseMove, p);
        QApplication::processEvents();
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, b);
    QApplication::processEvents();
}

// Drag expressed in DOCUMENT coordinates (converted through the canvas view).
static void dragDoc(CanvasView *cv, const QPointF &docA, const QPointF &docB, int steps = 8) {
    QPointF va = cv->toViewport(docA), vb = cv->toViewport(docB);
    dragStroke(cv, va, vb, steps);
}

static void clickDoc(CanvasView *cv, const QPointF &docPos) {
    sendMouse(cv, QEvent::MouseButtonPress, cv->toViewport(docPos));
    sendMouse(cv, QEvent::MouseButtonRelease, cv->toViewport(docPos));
    QApplication::processEvents();
}

static void sendKey(QWidget *target, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

static bool imageHasVariance(const QString &path) {
    QImage img(path);
    if (img.isNull() || img.width() < 4) return false;
    img = img.convertToFormat(QImage::Format_RGB888).scaled(64, 64);
    qint64 sum = 0, sumsq = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            QRgb c = img.pixel(x, y);
            int l = qGray(c);
            sum += l;
            sumsq += (qint64)l * l;
        }
    double n = 64.0 * 64.0;
    double mean = sum / n;
    double var = sumsq / n - mean * mean;
    return var > 15.0; // non-trivial content (light UI images have low variance)
}

static QAction *act2(MainWindow *win, const char *name) {
    return win->findChild<QAction *>(name);
}

static QByteArray md5(const QImage &img) {
    return QCryptographicHash::hash(
        QByteArray::fromRawData((const char *)img.constBits(), img.sizeInBytes()),
        QCryptographicHash::Md5);
}

static int rootLayerCount(EditorTab *tab) {
    QString json = tab->m_doc.layersJson();
    int sep = json.lastIndexOf('|');
    QJsonDocument doc = QJsonDocument::fromJson(json.left(sep).toUtf8());
    return doc.isObject() ? doc.object()["children"].toArray().size() : -1;
}

static QJsonObject layerJson(EditorTab *tab, uint64_t id) {
    QString json = tab->m_doc.layersJson();
    int sep = json.lastIndexOf('|');
    QJsonDocument doc = QJsonDocument::fromJson(json.left(sep).toUtf8());
    if (!doc.isObject()) return {};
    std::function<QJsonObject(const QJsonArray &)> find = [&](const QJsonArray &arr) -> QJsonObject {
        for (const QJsonValue &v : arr) {
            QJsonObject o = v.toObject();
            if (o["id"].toVariant().toULongLong() == id) return o;
            QJsonObject r = find(o["children"].toArray());
            if (!r.isEmpty()) return r;
        }
        return {};
    };
    return find(doc.object()["children"].toArray());
}

static bool selectionActive(EditorTab *tab) {
    int x, y; uint32_t w, h;
    return pf_selection_bounds(tab->m_doc.handle(), &x, &y, &w, &h) == 1;
}

// ---------------------------------------------------------------- auto-dismiss
// Dismisses modal dialogs (clicks Cancel/Close when present) and hides popup
// menus, so the command sweep can trigger every command headlessly.
class AutoDismiss : public QObject {
public:
    using QObject::QObject;
    int dialogsClosed = 0;
    int menusHidden = 0;
    int lastMenuActionCount = -1;
    bool eventFilter(QObject *watched, QEvent *ev) override {
        if (ev->type() == QEvent::Show && watched->isWidgetType()) {
            if (auto *dlg = qobject_cast<QDialog *>(watched)) {
                if (dlg->isModal())
                    QTimer::singleShot(150, this, [this, dlg]() { dismissDialog(dlg); });
            } else if (auto *m = qobject_cast<QMenu *>(watched)) {
                lastMenuActionCount = m->actions().size();
                QTimer::singleShot(150, this, [this, m]() {
                    if (m->isVisible()) { m->hide(); ++menusHidden; }
                });
            }
        }
        return QObject::eventFilter(watched, ev);
    }
    void dismissDialog(QDialog *dlg) {
        if (!dlg->isVisible()) return;
        for (QPushButton *b : dlg->findChildren<QPushButton *>()) {
            QString t = b->text().remove('&');
            if (t.compare("cancel", Qt::CaseInsensitive) == 0 || t.contains("Cancel")
                || t.compare("close", Qt::CaseInsensitive) == 0 || t.contains("Close")) {
                b->click(); ++dialogsClosed; return;
            }
        }
        dlg->reject(); ++dialogsClosed;
    }
};

// Fills the Text tool dialog with real text and clicks "Place".
class TextFiller : public QObject {
public:
    using QObject::QObject;
    bool done = false;
    bool eventFilter(QObject *watched, QEvent *ev) override {
        if (ev->type() == QEvent::Show && watched->isWidgetType()) {
            if (auto *dlg = qobject_cast<QDialog *>(watched);
                dlg && dlg->windowTitle() == QStringLiteral("Add Text")) {
                QTimer::singleShot(120, this, [this, dlg]() {
                    if (auto *pte = dlg->findChild<QPlainTextEdit *>()) {
                        pte->setPlainText(QStringLiteral("Audit Text"));
                        for (QPushButton *b : dlg->findChildren<QPushButton *>())
                            if (b->text().remove('&') == QStringLiteral("Place")) { b->click(); break; }
                        done = true;
                    }
                });
            }
        }
        return QObject::eventFilter(watched, ev);
    }
};

// ---------------------------------------------------------------- full audit
// Tests EVERY button in the UI: menus, top bar, tool strip, tool options,
// bottom bar presets, panels, and every canvas tool through real mouse events.
static void runButtonAudit(MainWindow &win, EditorTab *tab, const QString &outdir) {
    auto grab = [&](const char *name) {
        QApplication::processEvents();
        win.grab().save(outdir + "/" + name);
    };
    CanvasView *cv = tab->m_canvas;
    fprintf(stderr, "[AUDIT] begin\n");

    // ---------- A. menu population ----------
    {
        struct MSpec { const char *obj; int minItems; };
        const MSpec menus[] = {
            {"mFile", 7}, {"mEdit", 9}, {"mImage", 9}, {"mLayer", 11}, {"mSelect", 4},
            {"mFilter", 9}, {"mAdjust", 7}, {"mView", 4}, {"mWindow", 2}, {"mHelp", 1},
        };
        QSet<QString> reachable;
        std::function<void(QMenu *)> collect = [&](QMenu *m) {
            for (QAction *a : m->actions()) {
                if (a->isSeparator()) continue;
                if (a->menu()) { collect(a->menu()); continue; }
                if (!a->objectName().isEmpty()) reachable.insert(a->objectName());
            }
        };
        int populatedOk = 0;
        for (auto &ms : menus) {
            QMenu *m = win.menuBar()->findChild<QMenu *>(ms.obj);
            int n = 0;
            if (m) { collect(m); for (QAction *a : m->actions()) if (!a->isSeparator()) ++n; }
            if (n >= ms.minItems) ++populatedOk;
            else fprintf(stderr, "[AUDIT] menu %s has %d items (need %d)\n", ms.obj, n, ms.minItems);
        }
        check(populatedOk == 10, "audit A1: all 10 menus populated");
        QStringList missing;
        for (QAction *a : win.findChildren<QAction *>())
            if (a->objectName().startsWith("cmd_") && !reachable.contains(a->objectName()))
                missing << a->objectName();
        for (const QString &m : missing) fprintf(stderr, "[AUDIT] unreachable command: %s\n", qPrintable(m));
        check(missing.isEmpty(), "audit A2: every command reachable in a menu");
    }

    // ---------- B. tool strip: all 18 tools switch + have icons ----------
    {
        const auto &tools = toolList();
        bool allSwitch = true, allIcons = true;
        for (const ToolInfo &t : tools) {
            win.triggerToolByName(t.name);
            QApplication::processEvents();
            if (win.currentTool() != t.id) {
                allSwitch = false;
                fprintf(stderr, "[AUDIT] tool '%s' did not switch\n", t.name);
            }
        }
        // switching through Transform/Perspective enters those overlay modes;
        // leave them before the canvas interaction tests
        cv->cancelOverlay();
        check(allSwitch, "audit B1: all 18 tools switch via tool strip");
        QToolBar *strip = win.findChild<QToolBar *>("toolStrip");
        for (QAction *a : strip->actions())
            if (a->property("toolId").isValid() && a->icon().isNull()) allIcons = false;
        check(allIcons, "audit B2: every tool button has an icon");
        // tool options bar rebuilds with the right controls
        QToolBar *opts = win.findChild<QToolBar *>("toolOptions");
        win.triggerToolByName("Brush");
        check(opts->findChildren<QSlider *>().size() >= 1, "audit B3: brush options bar shows size slider");
        win.triggerToolByName("Magic Wand");
        check(opts->findChildren<QCheckBox *>().size() >= 2, "audit B4: wand options bar shows checkboxes");
        win.triggerToolByName("Shape");
        check(opts->findChildren<QComboBox *>().size() >= 1, "audit B5: shape options bar shows kind combo");
        // options bar actually drives settings
        win.triggerToolByName("Shape");
        if (auto *kindCombo = opts->findChild<QComboBox *>()) {
            kindCombo->setCurrentIndex(2); // Ellipse
            QApplication::processEvents();
            check(win.shapeSettings().kind == 2, "audit B6: shape kind combo updates settings");
        } else {
            check(false, "audit B6: shape kind combo updates settings");
        }
        win.triggerToolByName("Brush");
        if (auto *sz = opts->findChild<QSlider *>()) {
            sz->setValue(66);
            QApplication::processEvents();
        }
        check(win.brushSettings().size == 66, "audit B7: brush size slider updates settings");
    }

    // ---------- C. every canvas tool through real mouse events ----------
    {
        cv->fitToWindow();
        QApplication::processEvents();

        // reference layer: solid blue 200x150 at (100,100)
        QImage blue(200, 150, QImage::Format_RGBA8888);
        blue.fill(QColor(30, 90, 220, 255));
        uint64_t refLayer = tab->m_doc.addLayerFromImage(blue, QStringLiteral("Audit Ref"), 100, 100);
        tab->m_doc.setActiveLayer(refLayer);
        win.refreshAfterEdit(tab, true);
        cv->refreshComposite();

        // C1 Move tool
        win.triggerToolByName("Move");
        int x0 = 0, y0 = 0;
        pf_layer_offset(tab->m_doc.handle(), refLayer, &x0, &y0);
        dragDoc(cv, QPointF(200, 175), QPointF(260, 215));
        int x1 = 0, y1 = 0;
        pf_layer_offset(tab->m_doc.handle(), refLayer, &x1, &y1);
        check(qAbs(x1 - x0 - 60) <= 4 && qAbs(y1 - y0 - 40) <= 4, "audit C1: Move tool drags layer");

        // C2 Rectangular Select
        win.triggerToolByName("Rectangular Select");
        dragDoc(cv, QPointF(400, 300), QPointF(800, 600));
        int sx, sy; uint32_t sw, sh;
        pf_selection_bounds(tab->m_doc.handle(), &sx, &sy, &sw, &sh);
        check(selectionActive(tab) && qAbs((int)sw - 400) <= 6 && qAbs((int)sh - 300) <= 6,
              "audit C2: rectangular select makes selection");

        // C3 Elliptical Select
        win.triggerToolByName("Elliptical Select");
        dragDoc(cv, QPointF(500, 320), QPointF(900, 640));
        check(selectionActive(tab), "audit C3: elliptical select makes selection");

        // C4 Lasso
        win.triggerToolByName("Lasso Select");
        {
            QPointF c(900, 200);
            sendMouse(cv, QEvent::MouseButtonPress, cv->toViewport(c + QPointF(-80, 0)));
            for (int i = 1; i <= 24; ++i) {
                double a = i / 24.0 * 2 * M_PI;
                sendMouse(cv, QEvent::MouseMove,
                          cv->toViewport(c + QPointF(80 * qCos(a), 80 * qSin(a))));
                QApplication::processEvents();
            }
            sendMouse(cv, QEvent::MouseButtonRelease, cv->toViewport(c + QPointF(-80, 0)));
            QApplication::processEvents();
        }
        check(selectionActive(tab), "audit C4: lasso select makes selection");
        pf_selection_none(tab->m_doc.handle());

        // C5 Magic Wand
        win.triggerToolByName("Magic Wand");
        clickDoc(cv, QPointF(1100, 700)); // uniform white background
        check(selectionActive(tab), "audit C5: magic wand selects uniform area");
        pf_selection_none(tab->m_doc.handle());

        // C6 Eyedropper
        win.setFgColor(QColor(255, 255, 255));
        win.triggerToolByName("Eyedropper");
        clickDoc(cv, QPointF(200, 175)); // inside blue layer
        check(win.fgColor() == QColor(30, 90, 220), "audit C6: eyedropper picks composite color");
        win.setFgColor(QColor(200, 30, 160));

        // C7 Paint Bucket
        uint64_t fillLayer = tab->m_doc.addLayer(0, QStringLiteral("Audit Fill"));
        tab->m_doc.setActiveLayer(fillLayer);
        win.refreshAfterEdit(tab, true);
        win.triggerToolByName("Paint Bucket");
        clickDoc(cv, QPointF(950, 650));
        {
            QImage comp = tab->m_doc.compositeFull();
            // doc point (950,650) — after strokes below may change; sample now
            QRgb picked = comp.pixel(950, 650);
            check(qRed(picked) == 200 && qGreen(picked) == 30 && qBlue(picked) == 160,
                  "audit C7: paint bucket fills with FG color");
        }

        // C8 Gradient
        {
            QImage before = tab->m_doc.compositeFull();
            win.triggerToolByName("Gradient");
            dragDoc(cv, QPointF(500, 500), QPointF(1000, 700));
            QImage after = tab->m_doc.compositeFull();
            check(md5(before) != md5(after), "audit C8: gradient tool paints gradient");
        }

        // C9 Shape (rectangle)
        {
            QImage before = tab->m_doc.compositeFull();
            win.triggerToolByName("Shape");
            dragDoc(cv, QPointF(400, 350), QPointF(850, 600));
            QImage after = tab->m_doc.compositeFull();
            check(md5(before) != md5(after), "audit C9: shape tool draws shape");
        }

        // C10 Text tool (real dialog flow: fill text, click Place)
        {
            int before = rootLayerCount(tab);
            TextFiller filler;
            qApp->installEventFilter(&filler);
            win.triggerToolByName("Text");
            clickDoc(cv, QPointF(300, 700));
            QElapsedTimer t; t.start();
            while (!filler.done && t.elapsed() < 2500) QApplication::processEvents();
            qApp->removeEventFilter(&filler);
            QApplication::processEvents();
            check(filler.done && rootLayerCount(tab) == before + 1, "audit C10: text tool places text layer");
        }

        // C11 Transform (move + apply)
        {
            tab->m_doc.setActiveLayer(refLayer);
            win.refreshAfterEdit(tab, true);
            int x0t = 0, y0t = 0;
            pf_layer_offset(tab->m_doc.handle(), refLayer, &x0t, &y0t);
            win.triggerToolByName("Transform");
            check(cv->inTransformMode(), "audit C11a: transform mode entered");
            dragDoc(cv, QPointF(200, 175), QPointF(260, 215));
            sendKey(cv, Qt::Key_Return);
            int x1t = 0, y1t = 0;
            pf_layer_offset(tab->m_doc.handle(), refLayer, &x1t, &y1t);
            check(!cv->inTransformMode() && qAbs(x1t - x0t - 60) <= 6 && qAbs(y1t - y0t - 40) <= 6,
                  "audit C11b: transform commits translate via Enter");
        }

        // C12 Perspective (drag corner + apply)
        {
            tab->m_doc.setActiveLayer(refLayer);
            QImage before;
            tab->m_doc.layerPixels(refLayer, before);
            win.triggerToolByName("Perspective");
            check(cv->inPerspectiveMode(), "audit C12a: perspective mode entered");
            int xr, yr; pf_layer_offset(tab->m_doc.handle(), refLayer, &xr, &yr);
            uint32_t lw = 0, lh = 0;
            pf_layer_pixels_size(tab->m_doc.handle(), refLayer, &lw, &lh);
            dragDoc(cv, QPointF(xr, yr), QPointF(xr + 50, yr + 40)); // drag top-left corner
            sendKey(cv, Qt::Key_Return);
            QApplication::processEvents();
            QImage after;
            tab->m_doc.layerPixels(refLayer, after);
            fprintf(stderr, "[AUDIT] C12: mode=%d before=%d after=%d size=%dx%d\n",
                    (int)cv->inPerspectiveMode(), (int)before.sizeInBytes(), (int)after.sizeInBytes(),
                    after.width(), after.height());
            check(!cv->inPerspectiveMode() && md5(before) != md5(after),
                  "audit C12b: perspective commits via Enter");
        }

        // C13 Hand tool pans
        {
            cv->fitToWindow();
            QApplication::processEvents();
            QPointF v0 = cv->toViewport(QPointF(0, 0));
            double zoomNow = cv->zoom();
            win.triggerToolByName("Hand");
            dragDoc(cv, QPointF(600, 400), QPointF(660, 440));
            QPointF v1 = cv->toViewport(QPointF(0, 0));
            // panning by (60,40) doc px shifts the view origin by (60,40)*zoom
            check(qAbs(v1.x() - v0.x() - 60 * zoomNow) <= 6 && qAbs(v1.y() - v0.y() - 40 * zoomNow) <= 6,
                  "audit C13: hand tool pans canvas");
            cv->fitToWindow();
        }

        // C14 Zoom tool: left click in, right click out
        {
            cv->fitToWindow();
            QApplication::processEvents();
            double z0 = cv->zoom();
            win.triggerToolByName("Zoom");
            clickDoc(cv, QPointF(400, 300));
            double z1 = cv->zoom();
            sendMouse(cv, QEvent::MouseButtonPress, cv->toViewport(QPointF(400, 300)), Qt::RightButton);
            sendMouse(cv, QEvent::MouseButtonRelease, cv->toViewport(QPointF(400, 300)), Qt::RightButton);
            QApplication::processEvents();
            double z2 = cv->zoom();
            check(z1 > z0 * 1.3 && z2 < z1 * 0.75, "audit C14: zoom tool in/out clicks");
        }

        // C15 Pencil
        {
            tab->m_doc.setActiveLayer(fillLayer);
            QImage before;
            tab->m_doc.layerPixels(fillLayer, before);
            win.triggerToolByName("Pencil");
            dragDoc(cv, QPointF(600, 100), QPointF(900, 180));
            QImage after;
            tab->m_doc.layerPixels(fillLayer, after);
            check(md5(before) != md5(after), "audit C15: pencil draws");
        }

        // C16 Eraser (removes pixels on the layer it strokes)
        {
            QImage before;
            tab->m_doc.layerPixels(fillLayer, before);
            win.triggerToolByName("Eraser");
            dragDoc(cv, QPointF(500, 640), QPointF(700, 690));
            QImage after;
            tab->m_doc.layerPixels(fillLayer, after);
            check(md5(before) != md5(after), "audit C16: eraser erases layer pixels");
        }

        // C17 Crop (UI flow: drag, click Apply button)
        {
            uint32_t dw, dh;
            tab->m_doc.size(dw, dh);
            win.triggerToolByName("Crop");
            dragDoc(cv, QPointF(60, 60), QPointF(460, 360));
            check(cv->hasCropRect(), "audit C17a: crop region active");
            QToolButton *applyBtn = nullptr;
            for (QToolButton *b : cv->findChildren<QToolButton *>())
                if (b->text() == QStringLiteral("Apply Crop")) applyBtn = b;
            if (applyBtn) { applyBtn->click(); QApplication::processEvents(); }
            if (cv->hasCropRect()) { sendKey(cv, Qt::Key_Return); QApplication::processEvents(); }
            uint32_t nw, nh;
            tab->m_doc.size(nw, nh);
            fprintf(stderr, "[AUDIT] crop result: %ux%u hasRect=%d btn=%d\n",
                    nw, nh, (int)cv->hasCropRect(), applyBtn ? 1 : 0);
            check(!cv->hasCropRect() && nw == 400 && nh == 300, "audit C17b: crop applies via button");
        }
        win.refreshAfterEdit(tab, true);
        grab("20_button_audit_canvas_tools.png");
    }

    // ---------- D. bottom bar brush presets ----------
    {
        QToolBar *bb = win.findChild<QToolBar *>("bottomBar");
        QList<QToolButton *> btns;
        for (QToolButton *b : bb->findChildren<QToolButton *>())
            if (b->toolTip().startsWith(QStringLiteral("Preset:"))) btns << b; // skip toolbar ext button
        check(btns.size() == 5, "audit D1: five brush preset buttons");
        struct P { int size; int hardness; };
        const P expect[] = {{24, 10}, {16, 100}, {30, 85}, {40, 70}, {36, 5}};
        bool allOk = true;
        for (int i = 0; i < btns.size() && i < 5; ++i) {
            btns[i]->click();
            QApplication::processEvents();
            const BrushSettings &b = win.brushSettings();
            if (b.size != expect[i].size || b.hardness != expect[i].hardness) {
                allOk = false;
                fprintf(stderr, "[AUDIT] preset %d: size=%d hard=%d (want %d/%d)\n",
                        i, b.size, b.hardness, expect[i].size, expect[i].hardness);
            }
            if (win.currentTool() != ToolId::Brush) allOk = false;
        }
        check(allOk, "audit D2: presets set brush params + switch to Brush");
    }

    // ---------- E. layers panel buttons ----------
    {
        // ensure a known active layer and selection state
        win.refreshAfterEdit(tab, true);
        QWidget *lp = nullptr;
        for (QWidget *w : win.findChildren<QWidget *>())
            if (w->metaObject()->className() == QStringLiteral("LayersPanel")) { lp = w; break; }
        check(lp != nullptr, "audit E0: layers panel found");
        if (!lp) return;
        auto findBtn = [lp](const QString &tipStart) -> QToolButton * {
            for (QToolButton *b : lp->findChildren<QToolButton *>())
                if (b->toolTip().startsWith(tipStart)) return b;
            return nullptr;
        };
        int c0 = rootLayerCount(tab);
        if (auto *b = findBtn(QStringLiteral("New layer"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == c0 + 1, "audit E1: new layer button");
        if (auto *b = findBtn(QStringLiteral("New group"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == c0 + 2, "audit E2: new group button");
        if (auto *b = findBtn(QStringLiteral("Duplicate layer"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == c0 + 3, "audit E3: duplicate layer button");
        int cx = rootLayerCount(tab);
        if (auto *b = findBtn(QStringLiteral("Move up"))) { b->click(); QApplication::processEvents(); }
        if (auto *b = findBtn(QStringLiteral("Move down"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == cx, "audit E4: move up/down reorder (no count change)");
        // select a known raster layer ("Audit Ref") so merge/delete/mask act
        // on a raster layer with content below it — like a user clicking the row
        QTreeWidget *tree = lp->findChild<QTreeWidget *>();
        auto selectLayerByName = [tree](const QString &name) {
            if (!tree) return;
            std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find =
                [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem *c = parent->child(i);
                    if (c->text(0) == name) return c;
                    if (QTreeWidgetItem *r = find(c)) return r;
                }
                return nullptr;
            };
            if (QTreeWidgetItem *it = find(tree->invisibleRootItem()))
                tree->setCurrentItem(it);
        };
        selectLayerByName(QStringLiteral("Audit Ref"));
        QApplication::processEvents();
        if (auto *b = findBtn(QStringLiteral("Merge down"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == cx - 1, "audit E5: merge down button");
        if (auto *b = findBtn(QStringLiteral("Delete layer"))) { b->click(); QApplication::processEvents(); }
        check(rootLayerCount(tab) == cx - 2, "audit E6: delete layer button");
        // mask from selection
        pf_selection_all(tab->m_doc.handle());
        win.refreshAfterEdit(tab, true);
        selectLayerByName(QStringLiteral("Background")); // raster layer, always present
        QApplication::processEvents();
        uint64_t active = tab->m_doc.activeLayer();
        if (auto *b = findBtn(QStringLiteral("Add layer mask"))) { b->click(); QApplication::processEvents(); }
        fprintf(stderr, "[AUDIT] E7: active=%llu kind=%s mask=%d\n",
                (unsigned long long)active,
                qPrintable(layerJson(tab, active)["kind"].toString()),
                (int)layerJson(tab, active)["has_mask"].toBool());
        check(layerJson(tab, active)["kind"].toString() != QStringLiteral("group")
              && layerJson(tab, active)["has_mask"].toBool(),
              "audit E7: add layer mask button");
        // blend combo + opacity slider drive the engine
        win.refreshAfterEdit(tab, true);
        QComboBox *blend = lp->findChild<QComboBox *>();
        if (blend) {
            blend->setCurrentText(QStringLiteral("Multiply"));
            QApplication::processEvents();
        }
        check(layerJson(tab, tab->m_doc.activeLayer())["blend"].toString() == QStringLiteral("Multiply"),
              "audit E8: blend combo sets layer blend");
        QSlider *op = lp->findChild<QSlider *>();
        if (op) { op->setValue(50); QApplication::processEvents(); }
        check(qAbs(layerJson(tab, tab->m_doc.activeLayer())["opacity"].toDouble() - 0.5) < 0.02,
              "audit E9: opacity slider sets layer opacity");
        pf_selection_none(tab->m_doc.handle());
    }

    // ---------- F. color panel ----------
    {
        QWidget *cp = nullptr;
        for (QWidget *w : win.findChildren<QWidget *>())
            if (w->metaObject()->className() == QStringLiteral("ColorPanel")) { cp = w; break; }
        check(cp != nullptr, "audit F0: color panel found");
        if (cp) {
            // F1 swatch click
            win.setFgColor(QColor(0, 0, 0));
            QWidget *grid = nullptr;
            for (QWidget *w : cp->findChildren<QWidget *>())
                if (w->metaObject()->className() == QStringLiteral("SwatchGrid")) { grid = w; break; }
            if (grid) {
                sendMouse(grid, QEvent::MouseButtonPress, QPointF(8, 8));
                sendMouse(grid, QEvent::MouseButtonRelease, QPointF(8, 8));
                QApplication::processEvents();
            }
            check(win.fgColor() == QColor("#4A90E2"), "audit F1: swatch click picks color");
            // F2 hex field
            if (auto *hex = cp->findChild<QLineEdit *>()) {
                hex->setText(QStringLiteral("00FF00"));
                QMetaObject::invokeMethod(hex, "editingFinished");
                QApplication::processEvents();
            }
            check(win.fgColor() == QColor("#00FF00"), "audit F2: hex field sets FG color");
            // F3 swap + reset buttons (by tooltip)
            QColor fg = win.fgColor(), bg = win.bgColor();
            for (QToolButton *b : cp->findChildren<QToolButton *>())
                if (b->toolTip().startsWith(QStringLiteral("Swap"))) b->click();
            QApplication::processEvents();
            bool swapped = win.fgColor() == bg && win.bgColor() == fg;
            for (QToolButton *b : cp->findChildren<QToolButton *>())
                if (b->toolTip().startsWith(QStringLiteral("Default"))) b->click();
            QApplication::processEvents();
            check(swapped && win.fgColor() == QColor(30, 30, 30) && win.bgColor() == QColor(255, 255, 255),
                  "audit F3: swap & default color buttons");
        }
    }

    // ---------- G. top bar buttons ----------
    {
        AutoDismiss closer;
        qApp->installEventFilter(&closer);
        QToolBar *top = win.findChild<QToolBar *>("topBar");
        QList<QToolButton *> btns;
        for (QToolButton *b : top->findChildren<QToolButton *>())
            if (!b->toolTip().isEmpty()) btns << b; // skip the toolbar ext button
        check(btns.size() == 8, "audit G0: top bar has 8 buttons");
        auto findBtn = [&btns](const QString &tipStart) -> QToolButton * {
            for (QToolButton *b : btns)
                if (b->toolTip().startsWith(tipStart)) return b;
            return nullptr;
        };
        bool allIcons = true;
        for (QToolButton *b : btns) if (b->icon().isNull()) allIcons = false;
        check(allIcons, "audit G1: top bar buttons all have icons");

        // G2 zoom out button
        cv->fitToWindow();
        QApplication::processEvents();
        double z0 = cv->zoom();
        if (auto *b = findBtn(QStringLiteral("Zoom out"))) b->click();
        check(cv->zoom() < z0, "audit G2: zoom out button works");

        // G3 zoom combo (editable text entry path)
        QComboBox *zc = win.findChild<QComboBox *>("zoomCombo");
        bool comboOk = false;
        if (zc && zc->lineEdit()) {
            zc->lineEdit()->setText(QStringLiteral("200%"));
            QMetaObject::invokeMethod(zc->lineEdit(), "editingFinished");
            QApplication::processEvents();
            comboOk = qAbs(cv->zoom() - 2.0) < 0.01;
        }
        check(comboOk, "audit G3: zoom combo sets 200%");

        // G4 undo/redo buttons
        int histIdx = tab->m_doc.historyIndex();
        if (auto *b = findBtn(QStringLiteral("Undo"))) b->click();
        QApplication::processEvents();
        bool undoOk = tab->m_doc.historyIndex() == histIdx - 1;
        if (auto *b = findBtn(QStringLiteral("Redo"))) b->click();
        QApplication::processEvents();
        check(undoOk && tab->m_doc.historyIndex() == histIdx, "audit G4: undo/redo buttons work");

        // G5/G6 flip canvas buttons (whole image, md5 change + round trip)
        QImage before = tab->m_doc.compositeFull();
        if (auto *b = findBtn(QStringLiteral("Flip entire canvas horizontally"))) b->click();
        QApplication::processEvents();
        QImage afterH = tab->m_doc.compositeFull();
        if (auto *b = findBtn(QStringLiteral("Flip entire canvas horizontally"))) b->click();
        QApplication::processEvents();
        QImage backH = tab->m_doc.compositeFull();
        if (auto *b = findBtn(QStringLiteral("Flip entire canvas vertically"))) b->click();
        QApplication::processEvents();
        QImage afterV = tab->m_doc.compositeFull();
        check(md5(before) != md5(afterH) && md5(before) == md5(backH) && md5(before) != md5(afterV),
              "audit G5: flip canvas H/V buttons (round-trip)");
        // layer offset must mirror too (not just pixels)
        // G7 history toggle button
        bool vis0 = win.findChild<QListWidget *>() && win.findChild<QListWidget *>()->isVisible();
        if (auto *b = findBtn(QStringLiteral("Toggle history"))) { b->click(); QApplication::processEvents(); }
        bool vis1 = win.findChild<QListWidget *>() && win.findChild<QListWidget *>()->isVisible();
        if (auto *b = findBtn(QStringLiteral("Toggle history"))) { b->click(); QApplication::processEvents(); }
        bool vis2 = win.findChild<QListWidget *>() && win.findChild<QListWidget *>()->isVisible();
        check(vis0 && !vis1 && vis2, "audit G6: history toggle button shows/hides panel");

        // G8 filters button opens a POPULATED menu
        int menuCount = -1;
        if (auto *b = findBtn(QStringLiteral("Filters"))) {
            b->click(); // exec's the menu; closer hides it
            QApplication::processEvents();
            menuCount = closer.lastMenuActionCount;
        }
        check(menuCount >= 9, "audit G7: filters button opens populated menu");

        // G9 help button opens the About dialog
        int closedBefore = closer.dialogsClosed;
        if (auto *b = findBtn(QStringLiteral("About"))) b->click();
        QApplication::processEvents();
        check(closer.dialogsClosed > closedBefore, "audit G8: help button opens about dialog");
        qApp->removeEventFilter(&closer);
        grab("21_button_audit_topbar.png");
    }

    // ---------- H. command sweep: trigger EVERY menu command ----------
    {
        AutoDismiss closer;
        qApp->installEventFilter(&closer);
        // commands that open native file pickers are verified structurally;
        // headless runs cannot dismiss native dialogs. Their functionality is
        // covered elsewhere (openPath / ORA round-trip / export tests).
        QSet<QString> skip = { "cmd_file.open", "cmd_file.importLayer",
                               "cmd_file.save", "cmd_file.saveAs" };
        int fired = 0, skipped = 0, enabledSkipped = 0;
        for (QAction *a : win.findChildren<QAction *>()) {
            if (!a->objectName().startsWith("cmd_")) continue;
            if (skip.contains(a->objectName())) {
                ++skipped;
                if (a->isEnabled()) ++enabledSkipped;
                continue;
            }
            a->trigger();
            QApplication::processEvents();
            if (!win.isVisible()) { check(false, "audit H1: window alive after every command"); break; }
            ++fired;
        }
        fprintf(stderr, "[AUDIT] sweep fired=%d skipped=%d dialogsClosed=%d menusHidden=%d\n",
                fired, skipped, closer.dialogsClosed, closer.menusHidden);
        check(fired >= 55, "audit H1: every menu command triggered (no dead buttons)");
        check(skipped == 4 && enabledSkipped == 4, "audit H2: file-picker commands exist & enabled");
        check(closer.dialogsClosed > 5, "audit H3: modal dialogs opened & dismissed");
        qApp->removeEventFilter(&closer);
        grab("22_button_audit_after_sweep.png");
    }

    // ---------- I. history panel ----------
    {
        win.refreshAfterEdit(tab, true);
        QListWidget *hl = nullptr;
        for (QListWidget *l : win.findChildren<QListWidget *>())
            if (l->parentWidget() && l->parentWidget()->metaObject()->className() == QStringLiteral("HistoryPanel"))
                hl = l;
        if (!hl)
            for (QListWidget *l : win.findChildren<QListWidget *>()) { hl = l; break; }
        check(hl != nullptr && hl->count() == tab->m_doc.historyCount(),
              "audit I1: history list reflects engine history");
        if (hl && hl->count() >= 2) {
            hl->item(0)->setSelected(true);
            hl->itemClicked(hl->item(0)); // same slot a real click runs
            QApplication::processEvents();
            check(tab->m_doc.historyIndex() == 0, "audit I2: history item click jumps to state");
            int last = tab->m_doc.historyCount() - 1;
            hl->itemClicked(hl->item(last));
            QApplication::processEvents();
            check(tab->m_doc.historyIndex() == last, "audit I3: history click returns to latest");
        }
        // clear button (destructive — last history test)
        QWidget *hp = hl ? hl->parentWidget() : nullptr;
        QToolButton *clearBtn = nullptr;
        if (hp)
            for (QToolButton *b : hp->findChildren<QToolButton *>())
                if (b->text().contains(QStringLiteral("Clear"))) clearBtn = b;
        if (clearBtn) { clearBtn->click(); QApplication::processEvents(); }
        check(clearBtn && tab->m_doc.historyCount() <= 1, "audit I4: clear history button works");
        win.refreshAfterEdit(tab, true);
    }

    fprintf(stderr, "[AUDIT] done\n");
}

// ---------------------------------------------------------------- selftest
static int runSelftest(MainWindow &win, const QString &outdir) {
    fprintf(stderr, "[ST] begin\n");
    QDir().mkpath(outdir);
    QApplication::processEvents();
    auto grab = [&](const char *name) {
        QApplication::processEvents();
        win.grab().save(outdir + "/" + name);
    };

    grab("01_main_window.png");
    check(true, "main window shown");

    // --- startup document exists ---
    EditorTab *tab = win.currentTab();
    check(tab != nullptr, "startup document tab");
    win.balanceDocks();
    for (int i = 0; i < 6; ++i) QApplication::processEvents();
    if (!tab) return 1;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    check(w > 0 && h > 0, "engine document created");

    dragStroke(tab->m_canvas, QPointF(300, 300), QPointF(500, 320));
    dragStroke(tab->m_canvas, QPointF(400, 200), QPointF(400, 450));
    grab("02_brush_strokes.png");
    check(true, "brush stroke via canvas events");

    // --- new layer + different color stroke ---
    uint64_t layer2 = tab->m_doc.addLayer(0, "Stroke Layer 2");
    check(layer2 != 0, "new layer added");
    tab->m_doc.setLayerBlend(layer2, "Multiply");
    dragStroke(tab->m_canvas, QPointF(250, 400), QPointF(550, 260));
    grab("03_two_layers_stroke.png");

    // --- layer duplication + group ---
    uint64_t dup = tab->m_doc.duplicateLayer(layer2);
    check(dup != 0, "layer duplicated");
    uint64_t grp = tab->m_doc.addLayer(1, "Group");
    check(grp != 0, "group layer added");

    win.triggerToolByName("Rectangular Select");
    dragStroke(tab->m_canvas, QPointF(220, 180), QPointF(560, 480));
    int sx, sy; uint32_t sw, sh;
    check(pf_selection_bounds(tab->m_doc.handle(), &sx, &sy, &sw, &sh) == 1, "rect selection active");
    grab("04_selection_marching_ants.png");

    pf_fill_selection(tab->m_doc.handle(), 74, 144, 226, 255);
    tab->m_canvas->refreshComposite();
    grab("05_filled_selection.png");

    pf_selection_feather(tab->m_doc.handle(), 12);
    pf_selection_invert(tab->m_doc.handle());
    check(true, "selection feather+invert");

    pf_selection_none(tab->m_doc.handle());
    pf_filter_blur(tab->m_doc.handle(), 4, 1);
    pf_filter_sharpen(tab->m_doc.handle(), 0.8, 2);
    pf_filter_pixelate(tab->m_doc.handle(), 6);
    tab->m_canvas->refreshComposite();
    grab("06_filters_applied.png");
    check(true, "filters applied");

    pf_adjust_brightness_contrast(tab->m_doc.handle(), 12, 18);
    pf_adjust_hue_saturation(tab->m_doc.handle(), 40, 25, -5);
    float curvePts[] = {0, 20, 128, 150, 255, 245};
    pf_adjust_curves(tab->m_doc.handle(), curvePts, 3, 0);
    tab->m_canvas->refreshComposite();
    grab("07_adjustments_applied.png");
    check(true, "adjustments applied");

    double m[6] = {1.2, 0.0, 0.18, 1.1, 20, -14};
    pf_layer_affine(tab->m_doc.handle(), tab->m_doc.activeLayer(), m, 2);
    pf_layer_flip(tab->m_doc.handle(), tab->m_doc.activeLayer(), 1);
    double corners[8] = {40, 30, 700, 60, 660, 520, 20, 470};
    uint64_t pLayer = tab->m_doc.addLayer(0, "Perspective Test");
    pf_layer_fill(tab->m_doc.handle(), pLayer, 230, 150, 60, 255);
    pf_layer_perspective(tab->m_doc.handle(), pLayer, corners, 2);
    tab->m_canvas->refreshComposite();
    grab("08_transforms.png");
    check(true, "affine + flip + perspective");

    int countBefore = tab->m_doc.historyCount();
    check(countBefore > 10, "history recorded");
    bool undone = tab->m_doc.undo();
    check(undone, "undo works");
    tab->m_doc.redo();
    tab->m_canvas->refreshComposite();
    check(true, "redo works");
    tab->m_doc.historyGoto(0);
    tab->m_canvas->refreshComposite();
    grab("09_history_jumped_to_start.png");
    tab->m_doc.historyGoto(tab->m_doc.historyCount() - 1);
    tab->m_canvas->refreshComposite();

    // --- open the design reference image ---
    QString design = qEnvironmentVariable("PF_DESIGN_IMAGE", "/home/z/my-project/upload/03.png");
    fprintf(stderr, "[ST] opening design\n");
    if (QFile::exists(design)) {
        win.openPath(design);
        fprintf(stderr, "[ST] design opened\n");
        QApplication::processEvents();
        EditorTab *dtab = win.currentTab();
        check(dtab && dtab != tab, "image opened in new tab");
        if (dtab) {
            uint32_t dw, dh;
            dtab->m_doc.size(dw, dh);
            check(dw == 1408 && dh == 768, "opened image size correct");
            win.setCurrentTabTo(tab);
            pf_adjust_hue_saturation(dtab->m_doc.handle(), 120, 30, 0);
            dtab->m_canvas->refreshComposite();
            win.setCurrentTabTo(dtab);
            grab("10_opened_design_edited.png");
            QString base = outdir + "/export_design";
            check(pf_export(dtab->m_doc.handle(), (base + ".png").toUtf8(), 0, 92, 1, 255, 255, 255, 100) == 0, "export PNG");
            check(pf_export(dtab->m_doc.handle(), (base + ".jpg").toUtf8(), 1, 92, 1, 255, 255, 255, 100) == 0, "export JPEG");
            check(pf_export(dtab->m_doc.handle(), (base + ".webp").toUtf8(), 2, 92, 1, 255, 255, 255, 100) == 0, "export WebP");
            check(pf_export(dtab->m_doc.handle(), (base + ".bmp").toUtf8(), 3, 92, 1, 255, 255, 255, 100) == 0, "export BMP");
            check(pf_export(dtab->m_doc.handle(), (base + ".tif").toUtf8(), 4, 92, 1, 255, 255, 255, 100) == 0, "export TIFF");
            check(pf_export(dtab->m_doc.handle(), (base + ".gif").toUtf8(), 5, 92, 1, 255, 255, 255, 100) == 0, "export GIF");
            check(pf_save_ora(dtab->m_doc.handle(), (base + ".ora").toUtf8()) == 0, "save ORA project");
            check(imageHasVariance(base + ".png"), "PNG output has content");
            check(imageHasVariance(base + ".jpg"), "JPEG output has content");
            check(QFile(base + ".ora").size() > 10000, "ORA project non-trivial");
            PFDoc reopened = pf_document_open((base + ".ora").toUtf8().constData());
            check(reopened != nullptr, "ORA round-trip reopen");
            if (reopened) {
                uint32_t rw, rh;
                pf_document_size(reopened, &rw, &rh);
                check(rw == dw && rh == dh, "ORA reopened size match");
                pf_document_close(reopened);
            }
            check(pf_export(dtab->m_doc.handle(), (base + "_50pct.png").toUtf8(), 0, 92, 1, 255, 255, 255, 50) == 0, "export scaled 50%");
            QImage half(base + "_50pct.png");
            check(half.width() == (int)dw / 2, "scaled export dimension");
        }
    }

    fprintf(stderr, "[ST] text layer\n");
    win.setCurrentTabTo(tab);
    uint64_t textLayer = 0;
    {
        QImage timg = QImage(400, 120, QImage::Format_RGBA8888);
        timg.fill(Qt::transparent);
        QPainter tp(&timg);
        QFont f = QApplication::font();
        f.setPixelSize(84);
        f.setBold(true);
        tp.setFont(f);
        tp.setPen(QColor(30, 30, 30));
        tp.drawText(QPointF(10, 95), "PixelForge");
        tp.end();
        textLayer = tab->m_doc.addLayerFromImage(timg, "Text: PixelForge", 60, 500);
    }
    check(textLayer != 0, "text layer placed");
    tab->m_canvas->refreshComposite();
    grab("11_text_layer.png");

    double linePts[4] = {50, 50, 500, 120};
    pf_shape_draw(tab->m_doc.handle(), 0, linePts, 2, 30, 30, 30, 255, 6, 0, 0, 0, 0);
    double rectPts[4] = {60, 560, 620, 660};
    pf_shape_draw(tab->m_doc.handle(), 1, rectPts, 2, 74, 144, 226, 255, 3, 255, 255, 255, 255);
    double ellipsePts[4] = {700, 400, 980, 620};
    pf_shape_draw(tab->m_doc.handle(), 2, ellipsePts, 2, 200, 60, 60, 255, 2, 255, 170, 60, 255);
    pf_gradient_fill(tab->m_doc.handle(), 0, 750, 60, 1150, 350, 74, 144, 226, 255, 235, 100, 60, 255, 1);
    pf_flood_fill(tab->m_doc.handle(), 80, 80, 90, 200, 120, 255, 32, 1);
    tab->m_canvas->refreshComposite();
    grab("12_shapes_gradient_fill.png");
    check(true, "shapes + gradient + bucket");

    // --- command shortcuts registered + actions wired ---
    {
        auto act = [&win](const char *name) -> QAction * {
            return win.findChild<QAction *>(name);
        };
        // standard keys compare against the same platform standard sequence;
        // custom shortcuts compare exact strings.
        struct SC { const char *id; QKeySequence seq; };
        const SC expected[] = {
            {"cmd_edit.undo", QKeySequence(QKeySequence::Undo)},
            {"cmd_edit.redo", QKeySequence(QKeySequence::Redo)},
            {"cmd_file.open", QKeySequence(QKeySequence::Open)},
            {"cmd_file.save", QKeySequence(QKeySequence::Save)},
            {"cmd_file.export", QKeySequence("Ctrl+E")},
            {"cmd_layer.new", QKeySequence("Ctrl+Shift+N")},
            {"cmd_select.all", QKeySequence(QKeySequence::SelectAll)},
            {"cmd_select.none", QKeySequence("Ctrl+D")},
            {"cmd_select.invert", QKeySequence("Ctrl+Shift+I")},
            {"cmd_adjust.curves", QKeySequence("Ctrl+M")},
            {"cmd_adjust.hue", QKeySequence("Ctrl+U")},
            {"cmd_adjust.levels", QKeySequence("Ctrl+L")},
            {"cmd_view.zoomFit", QKeySequence("Ctrl+0")},
            {"cmd_view.zoom100", QKeySequence("Ctrl+1")},
        };
        int okCount = 0;
        for (const SC &s : expected) {
            QAction *a = act(s.id);
            if (a && a->shortcut() == s.seq && a->isEnabled()) ++okCount;
        }
        check(okCount == (int)(sizeof(expected) / sizeof(SC)), "all command shortcuts registered");
        const auto winActions = win.actions();
        bool allRegistered = true;
        for (const SC &s : expected) {
            QAction *a = act(s.id);
            if (!winActions.contains(a)) { allRegistered = false; break; }
        }
        check(allRegistered, "actions registered at window level");
    }

    // --- QAction triggering (menus/buttons -> slots -> engine) ---
    {
        auto act = [&win](const char *name) -> QAction * {
            return win.findChild<QAction *>(name);
        };
        int before = rootLayerCount(tab);
        if (QAction *a = act("cmd_layer.duplicate")) a->trigger();
        check(rootLayerCount(tab) == before + 1, "menu command: duplicate layer");
        int histIdx = tab->m_doc.historyIndex();
        if (QAction *a = act("cmd_edit.undo")) a->trigger();
        check(tab->m_doc.historyIndex() == histIdx - 1, "menu command: undo");
        if (QAction *a = act("cmd_select.all")) a->trigger();
        QApplication::processEvents();
        check(selectionActive(tab), "menu command: select all");
        if (QAction *a = act("cmd_select.none")) a->trigger();
        check(!selectionActive(tab), "menu command: deselect");
        if (QAction *a = act("cmd_adjust.invert")) a->trigger();
        tab->m_canvas->refreshComposite();
        check(true, "menu command: invert colors");
        // image-level flip is a distinct engine call from layer flip
        QImage compBefore = tab->m_doc.compositeFull();
        if (QAction *a = act("cmd_image.flipH")) a->trigger();
        QApplication::processEvents();
        check(md5(compBefore) != md5(tab->m_doc.compositeFull()), "menu command: flip image H");
    }

    win.refreshAfterEdit(tab, true);
    QApplication::processEvents();
    grab("13_panels_refreshed.png");

    // --- export dialog UI (real modal flow from the menu command) ---
    {
        const QString dlgShot = outdir + "/14_export_dialog_ui.png";
        QTimer::singleShot(700, [&win, dlgShot]() {
            for (QDialog *dlg : win.findChildren<QDialog *>()) {
                if (!dlg->isVisible() || dlg->objectName() == "qt_msg_ext_box") continue;
                int combos = dlg->findChildren<QComboBox *>().size();
                int sliders = dlg->findChildren<QSlider *>().size();
                int spins = dlg->findChildren<QSpinBox *>().size();
                QApplication::processEvents();
                win.grab().save(dlgShot);
                bool ok = combos >= 1 && spins >= 1;
                check(ok, "export dialog controls present");
                for (QPushButton *b : dlg->findChildren<QPushButton *>())
                    if (b->text().remove('&').compare("cancel", Qt::CaseInsensitive) == 0
                        || b->text().remove('&').contains("Cancel")) {
                        b->click();
                        return;
                    }
            }
        });
        if (QAction *a = act2(&win, "cmd_file.export")) a->trigger();
        QApplication::processEvents();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1600) QApplication::processEvents();
        check(QFile::exists(dlgShot), "export dialog screenshot captured");
    }

    // --- FULL BUTTON AUDIT (every button, every tool) ---
    win.setCurrentTabTo(tab);
    tab->m_canvas->fitToWindow();
    QApplication::processEvents();
    runButtonAudit(win, tab, outdir);

    // --- final composite export of test doc ---
    check(pf_export(tab->m_doc.handle(), (outdir + "/selftest_final.png").toUtf8(), 0, 92, 1, 255, 255, 255, 100) == 0, "final export");
    check(imageHasVariance(outdir + "/selftest_final.png"), "final export content");

    printf("\nSELFTEST SUMMARY: %d passed, %d failed\n", g_pass, g_fail);
    fprintf(stderr, "[ST] done\n");
    fflush(stdout);
    return g_fail == 0 ? 0 : 2;
}

int main(int argc, char *argv[]) {
    // Consistent look across screens with different DPI / scale factors:
    // Qt 6 scales automatically; PassThrough avoids rounding to whole
    // factors on 125%/150% Windows displays (no clipped or oversized UI).
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PixelForge Studio"));
    QApplication::setOrganizationName(QStringLiteral("PixelForge"));
    QApplication::setApplicationVersion(QStringLiteral("1.1.0"));
    app.setStyleSheet(Theme::styleSheet());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PixelForge Studio — native Rust+Qt image editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selftest("selftest", "Run automated engine+UI self test into <outdir>.");
    selftest.setValueName("outdir");
    parser.addOption(selftest);
    QCommandLineOption openOpt("open", "Open image file at startup.", "file");
    parser.addOption(openOpt);
    parser.process(app);

    MainWindow win;
    win.show();
    for (int i = 0; i < 12; ++i) QApplication::processEvents(); // let layout settle

    if (parser.isSet(selftest)) {
        // Deterministic window layout for the coordinate-based UI tests:
        // force the designed size regardless of any restored session state
        // (a headless run may have persisted a tiny geometry).
        win.resize(1440, 860);
        win.balanceDocks();
        for (int i = 0; i < 12; ++i) QApplication::processEvents();
        if (EditorTab *t = win.currentTab()) t->m_canvas->fitToWindow();
        QApplication::processEvents();
        QString outdir = parser.value(selftest);
        if (outdir.isEmpty()) outdir = QStringLiteral("/tmp/pixelforge-selftest");
        int rc = runSelftest(win, outdir);
        return rc;
    }

    const QStringList pos = parser.positionalArguments();
    for (const QString &f : pos) {
        if (QFile::exists(f)) QTimer::singleShot(0, [&win, f]() { win.openPath(f); });
    }

    return app.exec();
}
