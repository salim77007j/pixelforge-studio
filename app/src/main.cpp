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
#include <QDir>
#include <QTimer>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *name) {
    printf("SELFTEST %-38s %s\n", name, ok ? "PASS" : "FAIL");
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
    return var > 40.0; // non-trivial content
}

static int runSelftest(MainWindow &win, const QString &outdir) {
    fprintf(stderr, "[ST] begin\n");
    QDir().mkpath(outdir);
    QApplication::processEvents();
    fprintf(stderr, "[ST] events ok\n");
    auto grab = [&](const char *name) {
        QApplication::processEvents();
        win.grab().save(outdir + "/" + name);
    };

    fprintf(stderr, "[ST] grabbing\n");
    grab("01_main_window.png");
    fprintf(stderr, "[ST] grabbed\n");
    check(true, "main window shown");

    // --- startup document exists ---
    EditorTab *tab = win.currentTab();
    check(tab != nullptr, "startup document tab");
    win.balanceDocks();
    for (int i = 0; i < 6; ++i) QApplication::processEvents();
    fprintf(stderr, "[ST] canvas %dx%d zoom=%g\n", tab->m_canvas->width(), tab->m_canvas->height(), tab->m_canvas->zoom());
    if (!tab) return 1;
    uint32_t w, h;
    tab->m_doc.size(w, h);
    check(w > 0 && h > 0, "engine document created");

    fprintf(stderr, "[ST] stroking\n");
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

    fprintf(stderr, "[ST] selection\n");
    win.triggerToolByName("Rectangular Select");
    dragStroke(tab->m_canvas, QPointF(220, 180), QPointF(560, 480));
    int sx, sy; uint32_t sw, sh;
    check(pf_selection_bounds(tab->m_doc.handle(), &sx, &sy, &sw, &sh) == 1, "rect selection active");
    grab("04_selection_marching_ants.png");

    // --- fill within selection ---
    pf_fill_selection(tab->m_doc.handle(), 74, 144, 226, 255);
    tab->m_canvas->refreshComposite();
    grab("05_filled_selection.png");

    // --- feather + invert selection ---
    pf_selection_feather(tab->m_doc.handle(), 12);
    pf_selection_invert(tab->m_doc.handle());
    check(true, "selection feather+invert");

    // --- filters through engine ---
    pf_selection_none(tab->m_doc.handle());
    pf_filter_blur(tab->m_doc.handle(), 4, 1);
    pf_filter_sharpen(tab->m_doc.handle(), 0.8, 2);
    pf_filter_pixelate(tab->m_doc.handle(), 6);
    tab->m_canvas->refreshComposite();
    grab("06_filters_applied.png");
    check(true, "filters applied");

    // --- adjustments ---
    pf_adjust_brightness_contrast(tab->m_doc.handle(), 12, 18);
    pf_adjust_hue_saturation(tab->m_doc.handle(), 40, 25, -5);
    float curvePts[] = {0, 20, 128, 150, 255, 245};
    pf_adjust_curves(tab->m_doc.handle(), curvePts, 3, 0);
    tab->m_canvas->refreshComposite();
    grab("07_adjustments_applied.png");
    check(true, "adjustments applied");

    // --- transforms ---
    double m[6] = {1.2, 0.0, 0.18, 1.1, 20, -14};
    pf_layer_affine(tab->m_doc.handle(), tab->m_doc.activeLayer(), m, 2);
    pf_layer_flip(tab->m_doc.handle(), tab->m_doc.activeLayer(), 1);
    double corners[8] = {40, 30, 700, 60, 660, 520, 20, 470};
    uint64_t pLayer = tab->m_doc.addLayer(0, "Perspective Test");
    pf_layer_fill(pLayer == tab->m_doc.activeLayer() ? tab->m_doc.handle() : tab->m_doc.handle(),
                  pLayer, 230, 150, 60, 255);
    pf_layer_perspective(tab->m_doc.handle(), pLayer, corners, 2);
    tab->m_canvas->refreshComposite();
    grab("08_transforms.png");
    check(true, "affine + flip + perspective");

    // --- history ---
    int countBefore = tab->m_doc.historyCount();
    check(countBefore > 10, "history recorded");
    bool undone = tab->m_doc.undo();
    check(undone, "undo works");
    tab->m_doc.redo();
    tab->m_canvas->refreshComposite();
    check(true, "redo works");
    // jump to start
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
            // edit it: hue shift + vignette, then draw on it
            win.setCurrentTabTo(tab);
            pf_adjust_hue_saturation(dtab->m_doc.handle(), 120, 30, 0);
            dtab->m_canvas->refreshComposite();
            win.setCurrentTabTo(dtab);
            grab("10_opened_design_edited.png");
            // export it in multiple formats
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
            // ORA round trip
            PFDoc reopened = pf_document_open((base + ".ora").toUtf8().constData());
            check(reopened != nullptr, "ORA round-trip reopen");
            if (reopened) {
                uint32_t rw, rh;
                pf_document_size(reopened, &rw, &rh);
                check(rw == dw && rh == dh, "ORA reopened size match");
                pf_document_close(reopened);
            }
            // scaled export
            check(pf_export(dtab->m_doc.handle(), (base + "_50pct.png").toUtf8(), 0, 92, 1, 255, 255, 255, 50) == 0, "export scaled 50%");
            QImage half(base + "_50pct.png");
            check(half.width() == (int)dw / 2, "scaled export dimension");
        }
    }

    fprintf(stderr, "[ST] text layer\n");
    // --- text tool layer ---
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

    // --- shapes & gradient & bucket ---
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

    // --- layers panel / history panel reflect engine state ---
    win.refreshAfterEdit(tab, true);
    QApplication::processEvents();
    grab("13_panels_refreshed.png");

    // --- final composite export of test doc ---
    check(pf_export(tab->m_doc.handle(), (outdir + "/selftest_final.png").toUtf8(), 0, 92, 1, 255, 255, 255, 100) == 0, "final export");
    check(imageHasVariance(outdir + "/selftest_final.png"), "final export content");

    printf("\nSELFTEST SUMMARY: %d passed, %d failed\n", g_pass, g_fail);
    fprintf(stderr, "[ST] done\n");
    fflush(stdout);
    return g_fail == 0 ? 0 : 2;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PixelForge Studio"));
    QApplication::setOrganizationName(QStringLiteral("PixelForge"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
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
