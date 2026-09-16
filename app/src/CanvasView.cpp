#include "CanvasView.h"
#include "EditorTab.h"
#include "MainWindow.h"
#include "Theme.h"
#include "dialogs/Dialogs.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTabletEvent>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QToolButton>
#include <QHBoxLayout>
#include <QtMath>
#include <algorithm>

CanvasView::CanvasView(EditorTab *tab, QWidget *parent)
    : QWidget(parent), m_tab(tab) {
    setMouseTracking(true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    m_antsTimer.setInterval(110);
    connect(&m_antsTimer, &QTimer::timeout, this, &CanvasView::tickAnts);
    refreshComposite();
    // NOTE: initial fit happens on first real resize (see resizeEvent)
}

// ---------------- coordinate mapping ----------------

QPointF CanvasView::toDoc(const QPointF &vp) const { return (vp - m_origin) / m_zoom; }
QPointF CanvasView::toViewport(const QPointF &doc) const { return doc * m_zoom + m_origin; }
QRect CanvasView::docToViewportRect(const QRectF &docRect) const {
    return QRectF(toViewport(docRect.topLeft()), toViewport(docRect.bottomRight())).normalized().toAlignedRect();
}

void CanvasView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (!m_userZoomed && width() > 60 && height() > 60) {
        fitToWindow();
    }
}

void CanvasView::fitToWindow() {
    m_userZoomed = false;
    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    if (!w || !h) return;
    double m = 48; // margin
    double z = std::min((width() - m) / (double)w, (height() - m) / (double)h);
    z = std::clamp(z, 0.01, 16.0);
    double iw = w * z, ih = h * z;
    m_origin = QPointF((width() - iw) / 2.0, (height() - ih) / 2.0);
    m_zoom = z;
    update();
    emit zoomChanged(m_zoom);
}

void CanvasView::setZoom(double z, const QPointF &anchorViewport) {
    m_userZoomed = true;
    z = std::clamp(z, 0.02, 32.0);
    QPointF before = toDoc(anchorViewport);
    m_zoom = z;
    m_origin = anchorViewport - before * m_zoom;
    update();
    emit zoomChanged(m_zoom);
}

void CanvasView::setZoomImmediate(double z) {
    m_userZoomed = true;
    z = std::clamp(z, 0.02, 32.0);
    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    // center the image
    double iw = w * z, ih = h * z;
    m_origin = QPointF((width() - iw) / 2.0, (height() - ih) / 2.0);
    m_zoom = z;
    update();
    emit zoomChanged(m_zoom);
}

// ---------------- composite caching ----------------

void CanvasView::refreshComposite() {
    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    if (!w || !h) return;
    m_tab->m_doc.composite(m_composite, 0, 0, w, h);
    if (m_composite.sizeInBytes() == 0) return;
    m_tab->m_antsValid = false;
    update();
}

void CanvasView::refreshRegion(const QRect &docRect) {
    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    if (!w || !h) return;
    QRect r = docRect.intersected(QRect(0, 0, (int)w, (int)h));
    if (r.isEmpty()) return;
    QImage tmp; // composite directly into the cached image sub-rect
    QImage &img = m_composite;
    if (img.width() != (int)w || img.height() != (int)h) { refreshComposite(); return; }
    // engine writes into a temp row buffer we then blit
    QImage patch(r.width(), r.height(), QImage::Format_RGBA8888);
    pf_document_composite(m_tab->m_doc.handle(), r.x(), r.y(), r.width(), r.height(),
                          patch.bits(), (size_t)patch.sizeInBytes());
    QPainter pt(&img);
    pt.setCompositionMode(QPainter::CompositionMode_Source);
    pt.drawImage(r.topLeft(), patch);
    pt.end();
    update(docToViewportRect(QRectF(r)).adjusted(-2, -2, 2, 2));
}

void CanvasView::selectionChanged() {
    m_tab->m_antsValid = false;
    if (m_tab->selectionOutline().isEmpty()) {
        // nothing selected
    } else {
        if (!m_antsTimer.isActive()) m_antsTimer.start();
    }
    update();
}

void CanvasView::tickAnts() {
    m_antsOffset = (m_antsOffset + 1) % 8;
    update();
}

// ---------------- painting ----------------

void CanvasView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // workspace background
    p.fillRect(rect(), QColor(Theme::CanvasBg));

    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    if (!w || !h) return;

    QRect imgRect = QRect(0, 0, (int)(w * m_zoom), (int)(h * m_zoom));
    imgRect.translate(m_origin.toPoint());

    // floating-paper drop shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(35, 41, 54, 26));
    QPainterPath shadow;
    shadow.addRoundedRect(QRectF(imgRect).translated(3, 5), 6, 6);
    p.drawPath(shadow);
    p.setBrush(QColor(35, 41, 54, 14));
    p.drawRoundedRect(QRectF(imgRect).translated(0, 2), 6, 6);

    // checkerboard for transparency
    p.save();
    p.setClipRect(imgRect);
    int cs = 10;
    p.fillRect(imgRect, QColor("#FFFFFF"));
    QColor alt("#E7E9EE");
    int bx0 = imgRect.topLeft().x() - ((imgRect.x() % (2 * cs)) + 2 * cs) % (2 * cs);
    int by0 = imgRect.topLeft().y() - ((imgRect.y() % (2 * cs)) + 2 * cs) % (2 * cs);
    for (int y = by0; y < imgRect.bottom(); y += cs) {
        for (int x = bx0; x < imgRect.right(); x += cs) {
            if (((x / cs) + (y / cs)) % 2 == 0) continue;
            p.fillRect(QRect(x, y, qMin(cs, imgRect.right() - x), qMin(cs, imgRect.bottom() - y)), alt);
        }
    }
    // the composite itself
    p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 3.0);
    p.drawImage(imgRect, m_composite);
    p.restore();

    // selection marching ants
    const QVector<float> &ants = m_tab->selectionOutline();
    if (!ants.isEmpty()) {
        p.setPen(QPen(QColor(60, 66, 80, 200), 1));
        QPen dash(QPen(QColor(255, 255, 255, 235), 1));
        dash.setStyle(Qt::DashLine);
        dash.setDashPattern({3, 3});
        dash.setDashOffset(-m_antsOffset);
        for (int i = 0; i + 3 < ants.size(); i += 4) {
            QPointF a = toViewport(QPointF(ants[i], ants[i + 1]));
            QPointF b = toViewport(QPointF(ants[i + 2], ants[i + 3]));
            p.setPen(QPen(QColor(50, 56, 70, 200), 1));
            p.drawLine(a, b);
            p.setPen(dash);
            p.drawLine(a, b);
        }
    }

    // interaction overlays
    if (m_st.kind == InteractState::SelRect || m_st.kind == InteractState::SelEllipse) {
        QRectF r(m_st.pressDoc, m_st.curDoc);
        p.setPen(QPen(QColor(Theme::Accent), 1.2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        QRectF vr = QRectF(toViewport(r.topLeft()), toViewport(r.bottomRight())).normalized();
        if (m_st.kind == InteractState::SelRect) p.drawRect(vr);
        else p.drawEllipse(vr);
    } else if (m_st.kind == InteractState::Lasso || m_st.kind == InteractState::PolyCollect) {
        if (m_st.lassoPts.size() >= 2) {
            p.setPen(QPen(QColor(Theme::Accent), 1.2, Qt::DashLine));
            QPainterPath path;
            path.moveTo(toViewport(m_st.lassoPts.first()));
            for (int i = 1; i < m_st.lassoPts.size(); ++i) path.lineTo(toViewport(m_st.lassoPts[i]));
            if (m_st.kind == InteractState::Lasso) path.closeSubpath();
            p.drawPath(path);
        }
    } else if (m_st.kind == InteractState::CropDrag || m_cropRect.isValid()) {
        QRectF vr = m_st.kind == InteractState::CropDrag
                        ? QRectF(toViewport(m_st.pressDoc), toViewport(m_st.curDoc)).normalized()
                        : QRectF(toViewport(m_cropRect.topLeft()), toViewport(m_cropRect.bottomRight())).normalized();
        // darken outside
        p.fillRect(rect().adjusted(-2, -2, 2, 2), QColor(20, 24, 33, 70));
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(vr, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(QColor(Theme::Accent), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(vr);
        // rule-of-thirds guides
        p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(vr.left() + vr.width() * i / 3, vr.top()), QPointF(vr.left() + vr.width() * i / 3, vr.bottom()));
            p.drawLine(QPointF(vr.left(), vr.top() + vr.height() * i / 3), QPointF(vr.right(), vr.top() + vr.height() * i / 3));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Theme::Accent));
        for (QPointF c : {vr.topLeft(), vr.topRight(), vr.bottomLeft(), vr.bottomRight()}) {
            p.drawRect(QRectF(c - QPointF(4, 4), QSizeF(8, 8)));
        }
    } else if (m_st.kind == InteractState::GradientDrag) {
        QPointF a = toViewport(m_st.pressDoc), b = toViewport(m_st.curDoc);
        p.setPen(QPen(QColor(Theme::Accent), 1.6));
        p.drawLine(a, b);
        p.setBrush(QColor(Theme::Accent));
        p.drawEllipse(a, 4, 4);
        p.drawEllipse(b, 4, 4);
    } else if (m_st.kind == InteractState::ShapeDrag) {
        ShapeSettings ss = m_tab->m_mw->shapeSettings();
        QPen pen(QColor(m_tab->m_mw->fgColor()), ss.strokeWidth * m_zoom);
        p.setPen(pen);
        QColor fill = m_tab->m_mw->bgColor();
        fill.setAlphaF(m_tab->m_mw->fillEnabled() ? 1.0 : 0.0);
        p.setBrush(ss.hasFill ? fill : Qt::NoBrush);
        QRectF vr = QRectF(toViewport(m_st.pressDoc), toViewport(m_st.curDoc)).normalized();
        if (ss.kind == 0) {
            p.setBrush(Qt::NoBrush);
            p.drawLine(toViewport(m_st.pressDoc), toViewport(m_st.curDoc));
        } else if (ss.kind == 1) p.drawRect(vr);
        else if (ss.kind == 2) p.drawEllipse(vr);
    }

    if (m_transformActive) drawTransformOverlay(p);
    if (m_perspectiveActive) drawPerspectiveOverlay(p);

    // brush cursor outline
    ToolId tool = m_tab->m_mw->currentTool();
    if (m_mouseInside && (tool == ToolId::Brush || tool == ToolId::Pencil || tool == ToolId::Eraser)) {
        double r = m_tab->m_mw->brushSettings().size * m_zoom / 2.0;
        p.setPen(QPen(QColor(30, 34, 44, 170), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_lastMouseVp, r, r);
    }
}

// ---------------- events ----------------

bool CanvasView::event(QEvent *e) {
    // hover tracking for brush outline cursor
    if (e->type() == QEvent::ToolTip) return false;
    return QWidget::event(e);
}

void CanvasView::leaveEvent(QEvent *) {
    m_mouseInside = false;
    update();
}

void CanvasView::focusInEvent(QFocusEvent *) {
    updateCursorShape();
}

void CanvasView::updateCursorShape() {
    ToolId tool = m_tab->m_mw->currentTool();
    if (m_spaceDown) { setCursor(Qt::OpenHandCursor); return; }
    switch (tool) {
    case ToolId::Hand: setCursor(Qt::OpenHandCursor); break;
    case ToolId::Zoom: setCursor(Qt::CrossCursor); break;
    case ToolId::Text: setCursor(Qt::IBeamCursor); break;
    case ToolId::Move: setCursor(Qt::SizeAllCursor); break;
    case ToolId::Brush:
    case ToolId::Pencil:
    case ToolId::Eraser:
        setCursor(Qt::BlankCursor); break; // outline drawn in paint
    default: setCursor(Qt::CrossCursor); break;
    }
}

void CanvasView::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = true;
        updateCursorShape();
        e->accept();
        return;
    }
    if (m_transformActive) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { commitTransform(); e->accept(); return; }
        if (e->key() == Qt::Key_Escape) { cancelOverlay(); e->accept(); return; }
    }
    if (m_perspectiveActive) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { commitPerspective(); e->accept(); return; }
        if (e->key() == Qt::Key_Escape) { cancelOverlay(); e->accept(); return; }
    }
    if (m_cropRect.isValid() && m_st.kind == InteractState::None) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { applyCropOverlay(); e->accept(); return; }
        if (e->key() == Qt::Key_Escape) { cancelCrop(); e->accept(); return; }
    }
    if (m_st.kind == InteractState::PolyCollect) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            shapeCommit(m_st.lassoPts);
            m_st.kind = InteractState::None;
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) { m_st.kind = InteractState::None; update(); e->accept(); return; }
    }
    QWidget::keyPressEvent(e);
}

void CanvasView::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        updateCursorShape();
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void CanvasView::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        double f = e->angleDelta().y() > 0 ? 1.12 : (1.0 / 1.12);
        setZoom(m_zoom * f, e->position());
    } else if (e->modifiers() & Qt::ShiftModifier) {
        m_origin.rx() -= e->angleDelta().y();
        update();
    } else {
        m_origin.ry() -= e->angleDelta().y();
        update();
    }
    e->accept();
}

void CanvasView::tabletEvent(QTabletEvent *e) {
    // Pressure-sensitive stroking. Tablet events supersede mouse events here.
    QPointF pos = e->position();
    double pressure = e->pressure() > 0.0 ? e->pressure() : 0.5;
    switch (e->type()) {
    case QEvent::TabletPress:
        if (e->button() == Qt::LeftButton) {
            m_lastMouseVp = pos;
            m_mouseInside = true;
            handlePress(toDoc(pos), pos, pressure);
            e->accept();
        }
        break;
    case QEvent::TabletMove:
        m_lastMouseVp = pos;
        handleMove(toDoc(pos), pos, pressure);
        e->accept();
        break;
    case QEvent::TabletRelease:
        handleRelease(toDoc(pos), pos, pressure);
        e->accept();
        break;
    default: break;
    }
}

void CanvasView::mousePressEvent(QMouseEvent *e) {
    m_lastMouseVp = e->position();
    m_mouseInside = true;
    QPointF docPos = toDoc(e->position());
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spaceDown)) {
        m_st.kind = InteractState::HandDrag;
        m_st.pressDoc = docPos;
        m_st.lastDoc = docPos;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) handlePress(docPos, e->position(), 1.0);
    else if (e->button() == Qt::RightButton) {
        ToolId tool = m_tab->m_mw->currentTool();
        if (tool == ToolId::Eyedropper) pickColorAt(docPos, true);
        else if (tool == ToolId::Zoom) setZoom(m_zoom / 1.4, e->position());
        else if (tool == ToolId::Brush || tool == ToolId::Pencil || tool == ToolId::Eraser) {
            pickColorAt(docPos, false); // quick sample while painting
        }
    }
    e->accept();
}

void CanvasView::mouseMoveEvent(QMouseEvent *e) {
    m_lastMouseVp = e->position();
    m_mouseInside = true;
    QPointF docPos = toDoc(e->position());
    emit cursorMoved(docPos);
    handleMove(docPos, e->position(), 1.0);
    e->accept();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *e) {
    QPointF docPos = toDoc(e->position());
    handleRelease(docPos, e->position(), 1.0);
    e->accept();
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent *e) {
    if (m_st.kind == InteractState::PolyCollect) {
        shapeCommit(m_st.lassoPts);
        m_st.kind = InteractState::None;
        update();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CanvasView::handlePress(const QPointF &docPos, const QPointF &vp, double pressure) {
    Q_UNUSED(vp);
    MainWindow *mw = m_tab->m_mw;
    ToolId tool = mw->currentTool();
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[CV] press tool=%d at %g,%g\n", (int)tool, docPos.x(), docPos.y());
    m_st.pressDoc = docPos;
    m_st.curDoc = docPos;
    m_st.lastDoc = docPos;

    // transform modes intercept
    if (m_transformActive) {
        bool rotate = false;
        int h = hitTransformHandle(docPos, rotate);
        if (h >= 0) {
            m_st.kind = InteractState::TransformHandle;
            m_st.handleIndex = h;
            m_transformRotating = rotate;
            if (rotate) {
                QPointF c = m_transformOrigRect.center();
                m_transformStartAngle = QLineF(c, docPos).angle();
            }
        } else if (m_transform.map(m_transformOrigRect).boundingRect().contains(docPos)) {
            m_st.kind = InteractState::TransformMove;
        }
        return;
    }
    if (m_perspectiveActive) {
        int h = hitPerspectiveHandle(docPos);
        if (h >= 0) {
            m_st.kind = InteractState::PerspectiveHandle;
            m_st.handleIndex = h;
        }
        return;
    }

    switch (tool) {
    case ToolId::Hand:
        m_st.kind = InteractState::HandDrag;
        break;
    case ToolId::Zoom:
        setZoom(m_zoom * 1.4, vp);
        break;
    case ToolId::RectSelect:
    case ToolId::EllipticalSelect:
        m_st.kind = (tool == ToolId::RectSelect) ? InteractState::SelRect : InteractState::SelEllipse;
        break;
    case ToolId::Lasso:
        m_st.kind = InteractState::Lasso;
        m_st.lassoPts.clear();
        m_st.lassoPts << docPos;
        break;
    case ToolId::Wand:
        wandAt(docPos);
        break;
    case ToolId::Crop:
        if (m_cropRect.isValid()) {
            m_cropRect = QRectF();
            hideCropBar();
        }
        m_st.kind = InteractState::CropDrag;
        break;
    case ToolId::Eyedropper:
        pickColorAt(docPos, false);
        break;
    case ToolId::Brush:
    case ToolId::Pencil:
    case ToolId::Eraser:
        if (e_modifiers() & Qt::ShiftModifier && !m_lastStrokeEnd.isNull()) {
            // straight line from previous stroke end
            beginStroke(m_lastStrokeEnd, pressure);
            moveStroke(docPos, pressure);
        } else {
            beginStroke(docPos, pressure);
        }
        break;
    case ToolId::Fill:
        floodFillAt(docPos);
        break;
    case ToolId::Gradient:
        m_st.kind = InteractState::GradientDrag;
        break;
    case ToolId::Text:
        finishTextAt(docPos);
        break;
    case ToolId::Shape: {
        ShapeSettings ss = mw->shapeSettings();
        if (ss.kind == 3) { // polygon: collect
            if (m_st.kind != InteractState::PolyCollect) {
                m_st.kind = InteractState::PolyCollect;
                m_st.lassoPts.clear();
            }
            m_st.lassoPts << docPos;
        } else {
            m_st.kind = InteractState::ShapeDrag;
        }
        break;
    }
    case ToolId::Move:
        m_st.kind = InteractState::MoveLayer;
        break;
    case ToolId::Transform:
        enterTransformMode();
        break;
    case ToolId::Perspective:
        enterPerspectiveMode();
        break;
    }
    update();
}

Qt::KeyboardModifiers CanvasView::e_modifiers() const { return QGuiApplication::keyboardModifiers(); }

int CanvasView::selModeFromModifiers() const {
    Qt::KeyboardModifiers m = e_modifiers();
    if (m & Qt::ShiftModifier && m & Qt::AltModifier) return 3;
    if (m & Qt::ShiftModifier) return 1;
    if (m & Qt::AltModifier) return 2;
    return 0;
}

void CanvasView::handleMove(const QPointF &docPos, const QPointF &vp, double pressure) {
    Q_UNUSED(vp);
    m_st.curDoc = docPos;
    switch (m_st.kind) {
    case InteractState::None:
        break;
    case InteractState::HandDrag: {
        m_userZoomed = true;
        QPointF delta = docPos - m_st.lastDoc;
        m_origin += delta * m_zoom;
        m_st.lastDoc = toDoc(vp);
        break;
    }
    case InteractState::SelRect:
    case InteractState::SelEllipse:
    case InteractState::CropDrag:
    case InteractState::GradientDrag:
    case InteractState::ShapeDrag:
        update();
        break;
    case InteractState::Lasso:
        if (QLineF(toViewport(m_st.lassoPts.last()), vp).length() > 3) m_st.lassoPts << docPos;
        update();
        break;
    case InteractState::PolyCollect:
        update();
        break;
    case InteractState::MoveLayer: {
        QPointF d = docPos - m_st.lastDoc;
        pf_layer_translate(m_tab->m_doc.handle(), m_tab->m_doc.activeLayer(),
                           qRound(d.x()), qRound(d.y()), 0);
        refreshComposite();
        break;
    }
    case InteractState::TransformHandle: {
        if (m_transformRotating) {
            QPointF c = m_transformOrigRect.center();
            double cur = QLineF(c, docPos).angle();
            double delta = cur - m_transformStartAngle;
            m_transformStartAngle = cur;
            QTransform t;
            t.translate(c.x(), c.y());
            t.rotate(-delta);
            t.translate(-c.x(), -c.y());
            m_transform = t * m_transform;
        } else {
            // scale: opposite corner stays fixed, current cursor defines the dragged corner
            QRectF r = m_transformOrigRect;
            QPointF origAnchor; // corner that stays fixed (in original space)
            if (m_st.handleIndex == 0) origAnchor = r.bottomRight();
            else if (m_st.handleIndex == 1) origAnchor = r.bottomLeft();
            else if (m_st.handleIndex == 2) origAnchor = r.topLeft();
            else origAnchor = r.topRight();
            QPointF fixed = m_transform.map(origAnchor);
            QRectF srcRect = r;
            QPointF topLeft, bottomRight;
            if (m_st.handleIndex == 0) { topLeft = docPos; bottomRight = fixed; }
            else if (m_st.handleIndex == 1) { topLeft = QPointF(fixed.x(), docPos.y()); bottomRight = QPointF(docPos.x(), fixed.y()); }
            else if (m_st.handleIndex == 2) { topLeft = fixed; bottomRight = docPos; }
            else { topLeft = QPointF(docPos.x(), fixed.y()); bottomRight = QPointF(fixed.x(), docPos.y()); }
            QRectF dst = QRectF(topLeft, bottomRight).normalized();
            double sw = dst.width() / srcRect.width();
            double sh = dst.height() / srcRect.height();
            if (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) {
                double s = qBound(0.01, qMax(qAbs(sw), qAbs(sh)), 100.0);
                sw = s * (sw < 0 ? -1 : 1);
                sh = s * (sh < 0 ? -1 : 1);
            }
            QTransform t;
            t.translate(srcRect.left(), srcRect.top());
            t.scale(qMax(0.005, qAbs(sw)), qMax(0.005, qAbs(sh)));
            t.translate(-srcRect.left(), -srcRect.top());
            t.translate(fixed.x() - t.map(origAnchor).x(), fixed.y() - t.map(origAnchor).y());
            m_transform = t;
        }
        update();
        break;
    }
    case InteractState::TransformMove: {
        QPointF d = docPos - m_st.lastDoc;
        QTransform t;
        t.translate(d.x(), d.y());
        m_transform = t * m_transform;
        update();
        break;
    }
    case InteractState::PerspectiveHandle: {
        m_perspectiveCorners[m_st.handleIndex] = docPos;
        update();
        break;
    }
    default: break;
    }
    if (m_strokeActive && m_st.kind == InteractState::None)
        moveStroke(docPos, pressure);
    m_st.lastDoc = toDoc(vp);
    update();
}

void CanvasView::handleRelease(const QPointF &docPos, const QPointF &vp, double pressure) {
    Q_UNUSED(vp); Q_UNUSED(pressure);
    if (qEnvironmentVariableIsSet("PF_DEBUG")) fprintf(stderr, "[CV] release kind=%d at %g,%g\n", (int)m_st.kind, docPos.x(), docPos.y());
    switch (m_st.kind) {
    case InteractState::SelRect: {
        QRectF r = QRectF(m_st.pressDoc, docPos).normalized();
        if (r.width() < 1 || r.height() < 1) {
            pf_selection_none(m_tab->m_doc.handle());
        } else {
            pf_selection_rect(m_tab->m_doc.handle(), r.x(), r.y(), r.width(), r.height(), selModeFromModifiers());
        }
        m_tab->afterEdit();
        selectionChanged();
        break;
    }
    case InteractState::SelEllipse: {
        QRectF r = QRectF(m_st.pressDoc, docPos).normalized();
        if (r.width() < 1 || r.height() < 1) {
            pf_selection_none(m_tab->m_doc.handle());
        } else {
            pf_selection_ellipse(m_tab->m_doc.handle(), r.center().x(), r.center().y(),
                                 r.width() / 2, r.height() / 2, selModeFromModifiers());
        }
        m_tab->afterEdit();
        selectionChanged();
        break;
    }
    case InteractState::Lasso: {
        if (m_st.lassoPts.size() >= 3) {
            QVector<double> pts;
            for (const QPointF &pt : m_st.lassoPts) { pts << pt.x() << pt.y(); }
            pf_selection_lasso(m_tab->m_doc.handle(), pts.constData(), pts.size() / 2, selModeFromModifiers());
            m_tab->afterEdit();
            selectionChanged();
        }
        break;
    }
    case InteractState::CropDrag: {
        m_cropRect = QRectF(m_st.pressDoc, docPos).normalized();
        if (m_cropRect.width() > 4 && m_cropRect.height() > 4) showCropBar();
        update();
        break;
    }
    case InteractState::GradientDrag:
        gradientCommit(m_st.pressDoc, docPos);
        m_tab->afterEdit();
        break;
    case InteractState::ShapeDrag:
        shapeCommit({m_st.pressDoc, docPos});
        m_tab->afterEdit();
        break;
    case InteractState::MoveLayer:
        pf_layer_translate(m_tab->m_doc.handle(), m_tab->m_doc.activeLayer(), 0, 0, 1);
        m_tab->afterEdit();
        break;
    case InteractState::HandDrag:
        updateCursorShape();
        break;
    case InteractState::TransformHandle:
    case InteractState::TransformMove:
        break;
    case InteractState::PerspectiveHandle:
        break;
    default: break;
    }
    if (m_st.kind != InteractState::PolyCollect) m_st.kind = InteractState::None;
    endStroke();
    update();
}

// ---------------- tool operations ----------------

void CanvasView::beginStroke(const QPointF &docPos, double pressure) {
    const BrushSettings &b = m_tab->m_mw->brushSettings();
    QColor c = m_tab->m_mw->fgColor();
    int tool = (m_tab->m_mw->currentTool() == ToolId::Pencil) ? 1
             : (m_tab->m_mw->currentTool() == ToolId::Eraser) ? 2 : 0;
    m_strokeActive = true;
    pf_stroke_begin(m_tab->m_doc.handle(), tool, b.size, b.hardness / 100.0,
                    b.opacity / 100.0, b.flow / 100.0, b.spacing / 100.0,
                    c.red(), c.green(), c.blue(), 255,
                    b.pressureSize ? 1 : 0, b.pressureOpacity ? 1 : 0,
                    docPos.x(), docPos.y(), pressure);
    update();
}

void CanvasView::moveStroke(const QPointF &docPos, double pressure) {
    int dx = 0, dy = 0; uint32_t dw = 0, dh = 0;
    if (pf_stroke_move(m_tab->m_doc.handle(), docPos.x(), docPos.y(), pressure,
                       &dx, &dy, &dw, &dh) == 0 && dw > 0) {
        refreshRegion(QRect(dx, dy, (int)dw, (int)dh));
    }
    m_lastStrokeEnd = docPos;
}

void CanvasView::endStroke() {
    if (!m_strokeActive) return;
    m_strokeActive = false;
    pf_stroke_end(m_tab->m_doc.handle());
    m_tab->afterEdit();
    emit interactionFinished();
}

void CanvasView::pickColorAt(const QPointF &docPos, bool background) {
    uint8_t r, g, b, a;
    if (pf_pick_color(m_tab->m_doc.handle(), qFloor(docPos.x()), qFloor(docPos.y()), 1, &r, &g, &b, &a) == 0) {
        QColor c(r, g, b, a);
        emit colorPicked(c, background);
        emit statusMessage(tr("Picked #%1%2%3").arg(r, 2, 16).arg(g, 2, 16).arg(b, 2, 16));
    }
}

void CanvasView::floodFillAt(const QPointF &docPos) {
    const FillSettings &fs = m_tab->m_mw->fillSettings();
    QColor c = m_tab->m_mw->fgColor();
    pf_flood_fill(m_tab->m_doc.handle(), qFloor(docPos.x()), qFloor(docPos.y()),
                  c.red(), c.green(), c.blue(), 255, fs.tolerance, fs.contiguous ? 1 : 0);
    m_tab->afterEdit();
    emit interactionFinished();
}

void CanvasView::wandAt(const QPointF &docPos) {
    const WandSettings &ws = m_tab->m_mw->wandSettings();
    pf_selection_wand(m_tab->m_doc.handle(), qFloor(docPos.x()), qFloor(docPos.y()),
                      ws.tolerance, ws.contiguous ? 1 : 0, selModeFromModifiers(),
                      ws.sampleComposite ? 1 : 0);
    m_tab->afterEdit();
    selectionChanged();
    emit interactionFinished();
}

void CanvasView::shapeCommit(const QVector<QPointF> &pts) {
    ShapeSettings ss = m_tab->m_mw->shapeSettings();
    QColor sc = m_tab->m_mw->fgColor();
    QColor fc = m_tab->m_mw->bgColor();
    if (pts.size() < 2) return;
    int npts = (ss.kind == 3) ? pts.size() : 2;
    if (ss.kind >= 1 && ss.kind <= 2 && pts.size() >= 2) npts = 2; // rect/ellipse from 2 corners
    QVector<double> flat;
    if (ss.kind == 3) {
        for (const QPointF &pt : pts) flat << pt.x() << pt.y();
    } else {
        flat << pts[0].x() << pts[0].y() << pts[1].x() << pts[1].y();
    }
    pf_shape_draw(m_tab->m_doc.handle(), ss.kind, flat.constData(), flat.size() / 2,
                  sc.red(), sc.green(), sc.blue(), ss.hasStroke ? 255 : 0, ss.strokeWidth,
                  fc.red(), fc.green(), fc.blue(), ss.hasFill ? 255 : 0);
    m_tab->afterEdit();
    emit interactionFinished();
}

void CanvasView::gradientCommit(const QPointF &p0, const QPointF &p1) {
    GradKind kind = m_tab->m_mw->gradientSettings().kind;
    GradTarget target = m_tab->m_mw->gradientSettings().target;
    QColor fg = m_tab->m_mw->fgColor();
    QColor bg = m_tab->m_mw->bgColor();
    QColor c0 = fg, c1 = bg;
    int a0 = 255, a1 = 255;
    switch (target) {
    case GradTarget::FgToBg: c0 = fg; c1 = bg; break;
    case GradTarget::FgToTransparent: c0 = fg; c1 = fg; a1 = 0; break;
    case GradTarget::BgToFg: c0 = bg; c1 = fg; break;
    }
    pf_gradient_fill(m_tab->m_doc.handle(), kind == GradKind::Radial ? 1 : 0,
                     p0.x(), p0.y(), p1.x(), p1.y(),
                     c0.red(), c0.green(), c0.blue(), a0,
                     c1.red(), c1.green(), c1.blue(), a1, 1);
}

void CanvasView::finishTextAt(const QPointF &docPos) {
    TextDialog dlg(this);
    dlg.setColor(m_tab->m_mw->fgColor());
    if (dlg.exec() != QDialog::Accepted) return;
    QImage glyph = dlg.renderedText();
    if (glyph.isNull()) return;
    m_tab->m_doc.addLayerFromImage(glyph, QStringLiteral("Text: ") + dlg.text().left(24),
                                   qRound(docPos.x()), qRound(docPos.y()));
    m_tab->afterStructureEdit();
    emit interactionFinished();
}

// ---------------- crop ----------------

void CanvasView::showCropBar() {
    if (!m_cropBar) {
        m_cropBar = new QWidget(this);
        m_cropBar->setAutoFillBackground(true);
        m_cropBar->setBackgroundRole(QPalette::Window);
        auto *lay = new QHBoxLayout(m_cropBar);
        lay->setContentsMargins(8, 5, 8, 5);
        m_cropApply = new QToolButton(m_cropBar);
        m_cropApply->setText(tr("Apply Crop"));
        m_cropApply->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_cropApply->setStyleSheet("QToolButton{background:#4A90E2;color:white;border-radius:7px;padding:6px 14px;font-weight:600;}"
                                   "QToolButton:hover{background:#3B7CC9;}");
        m_cropCancel = new QToolButton(m_cropBar);
        m_cropCancel->setText(tr("Cancel"));
        m_cropCancel->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_cropCancel->setStyleSheet("QToolButton{background:#F7F8FA;color:#1F2937;border:1px solid #E1E4E9;border-radius:7px;padding:6px 14px;}"
                                    "QToolButton:hover{background:#EDF0F4;}");
        lay->addStretch(1);
        lay->addWidget(m_cropCancel);
        lay->addWidget(m_cropApply);
        connect(m_cropApply, &QToolButton::clicked, this, &CanvasView::applyCropOverlay);
        connect(m_cropCancel, &QToolButton::clicked, this, &CanvasView::cancelCrop);
        m_cropBar->hide();
    }
    m_cropBar->setGeometry(width() / 2 - 110, height() - 52, 220, 40);
    m_cropBar->show();
    m_cropBar->raise();
}

void CanvasView::hideCropBar() {
    if (m_cropBar) m_cropBar->hide();
}

void CanvasView::applyCropOverlay() {
    if (!m_cropRect.isValid()) return;
    // toRect() rounds to nearest — toAlignedRect() would round outward and
    // make every crop 1px larger than the drawn region
    QRect r = m_cropRect.normalized().toRect();
    uint32_t w, h;
    m_tab->m_doc.size(w, h);
    r = r.intersected(QRect(0, 0, (int)w, (int)h));
    if (r.width() > 0 && r.height() > 0) {
        pf_image_crop(m_tab->m_doc.handle(), r.x(), r.y(), r.width(), r.height());
        m_cropRect = QRectF();
        hideCropBar();
        m_tab->afterStructureEdit();
        emit interactionFinished();
    }
}

void CanvasView::cancelCrop() {
    m_cropRect = QRectF();
    hideCropBar();
    update();
}

// ---------------- transform overlay ----------------

QRectF CanvasView::layerDocRect() const {
    uint64_t id = m_tab->m_doc.activeLayer();
    uint32_t w, h;
    if (pf_layer_pixels_size(m_tab->m_doc.handle(), id, &w, &h) != 0)
        return QRectF();
    int x = 0, y = 0;
    pf_layer_offset(m_tab->m_doc.handle(), id, &x, &y);
    return QRectF(x, y, w, h);
}

void CanvasView::ensureLayerPreviewImage() {
    uint64_t id = m_tab->m_doc.activeLayer();
    if (!m_tab->m_doc.layerPixels(id, m_layerPreview)) m_layerPreview = QImage();
}

void CanvasView::enterTransformMode() {
    cancelOverlay();
    m_transformOrigRect = layerDocRect();
    if (!m_transformOrigRect.isValid()) return;
    m_transform = QTransform();
    ensureLayerPreviewImage();
    m_transformActive = true;
    m_tab->m_mw->showStatus(tr("Transform: drag inside to move, corners to scale, outside top to rotate. Enter=apply, Esc=cancel."));
    update();
}

void CanvasView::enterPerspectiveMode() {
    cancelOverlay();
    m_transformOrigRect = layerDocRect();
    if (!m_transformOrigRect.isValid()) return;
    QRectF r = m_transformOrigRect;
    m_perspectiveCorners[0] = r.topLeft();
    m_perspectiveCorners[1] = r.topRight();
    m_perspectiveCorners[2] = r.bottomRight();
    m_perspectiveCorners[3] = r.bottomLeft();
    m_perspectiveActive = true;
    ensureLayerPreviewImage();
    m_tab->m_mw->showStatus(tr("Perspective: drag the 4 corner handles, Enter=apply, Esc=cancel."));
    update();
}

int CanvasView::hitTransformHandle(const QPointF &docPos, bool &rotate) const {
    rotate = false;
    QRectF r = m_transform.map(m_transformOrigRect).boundingRect();
    double tol = 10.0 / m_zoom;
    // rotation handle above top edge center
    QPointF rotHandle = QPointF(r.center().x(), r.top() - 24.0 / m_zoom);
    if (QLineF(docPos, rotHandle).length() < tol * 1.6) { rotate = true; return -2; }
    QPointF corners[4] = {m_transform.map(m_transformOrigRect.topLeft()),
                          m_transform.map(m_transformOrigRect.topRight()),
                          m_transform.map(m_transformOrigRect.bottomLeft()),
                          m_transform.map(m_transformOrigRect.bottomRight())};
    for (int i = 0; i < 4; ++i) {
        if (QLineF(docPos, corners[i]).length() < tol * 1.6) return i;
    }
    return -1;
}

int CanvasView::hitPerspectiveHandle(const QPointF &docPos) const {
    double tol = 12.0 / m_zoom;
    for (int i = 0; i < 4; ++i)
        if (QLineF(docPos, m_perspectiveCorners[i]).length() < tol) return i;
    return -1;
}

void CanvasView::drawTransformOverlay(QPainter &p) {
    QPolygonF srcPoly;
    srcPoly << m_transformOrigRect.topLeft() << m_transformOrigRect.topRight()
            << m_transformOrigRect.bottomRight() << m_transformOrigRect.bottomLeft();
    QPolygonF dstPoly = m_transform.map(srcPoly);
    // dim outside layer
    QPainterPath layerPath;
    layerPath.addPolygon(dstPoly);
    layerPath.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(74, 144, 226, 36));
    p.drawPath(layerPath);
    // preview image
    if (!m_layerPreview.isNull()) {
        QTransform viewT;
        viewT.translate(m_origin.x(), m_origin.y());
        viewT.scale(m_zoom, m_zoom);
        QTransform full = viewT * m_transform;
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 3.0);
        p.setTransform(full, false);
        p.drawImage(QPointF(m_transformOrigRect.topLeft()), m_layerPreview);
        p.resetTransform();
    }
    // border + handles
    p.setPen(QPen(QColor(Theme::Accent), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(dstPoly);
    p.setPen(QPen(QColor("#5B6472"), 1));
    p.setBrush(QColor(Theme::Accent));
    for (const QPointF &c : dstPoly) {
        p.drawEllipse(toViewport(c), 5, 5);
    }
    // rotate handle
    QPointF topC = toViewport(QLineF(dstPoly[0], dstPoly[1]).pointAt(0.5));
    QPointF rotVp = topC + QPointF(0, -26);
    p.drawLine(topC, rotVp);
    p.drawEllipse(rotVp, 5, 5);
}

void CanvasView::drawPerspectiveOverlay(QPainter &p) {
    QPolygonF poly;
    for (const QPointF &c : m_perspectiveCorners) poly << c;
    QPainterPath path;
    path.addPolygon(poly);
    path.closeSubpath();
    p.setPen(QPen(QColor(Theme::Accent), 1.4));
    p.setBrush(QColor(74, 144, 226, 30));
    p.drawPath(path);
    if (!m_layerPreview.isNull()) {
        QTransform viewT;
        viewT.scale(m_zoom, m_zoom);
        viewT.translate(m_origin.x(), m_origin.y());
        QTransform tex;
        QRectF r = m_transformOrigRect;
        tex.translate(r.left(), r.top());
        tex.scale(r.width(), r.height());
        QTransform full = viewT * tex;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(full, false);
        p.drawImage(QPoint(0, 0), m_layerPreview.scaled(512, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        p.resetTransform();
    }
    p.setPen(QPen(QColor("#5B6472"), 1));
    p.setBrush(QColor(Theme::Accent));
    for (const QPointF &c : m_perspectiveCorners) p.drawEllipse(toViewport(c), 6, 6);
}

void CanvasView::commitTransform() {
    if (!m_transformActive) return;
    // QTransform -> pf matrix [m11 m21 m12 m22 dx dy]
    double m[6] = {m_transform.m11(), m_transform.m21(), m_transform.m12(),
                   m_transform.m22(), m_transform.dx(), m_transform.dy()};
    pf_layer_affine(m_tab->m_doc.handle(), m_tab->m_doc.activeLayer(), m, 2);
    m_transformActive = false;
    m_layerPreview = QImage();
    m_tab->afterStructureEdit();
    emit interactionFinished();
}

void CanvasView::commitPerspective() {
    if (!m_perspectiveActive) return;
    double corners[8] = {
        m_perspectiveCorners[0].x(), m_perspectiveCorners[0].y(),
        m_perspectiveCorners[1].x(), m_perspectiveCorners[1].y(),
        m_perspectiveCorners[2].x(), m_perspectiveCorners[2].y(),
        m_perspectiveCorners[3].x(), m_perspectiveCorners[3].y(),
    };
    pf_layer_perspective(m_tab->m_doc.handle(), m_tab->m_doc.activeLayer(), corners, 2);
    m_perspectiveActive = false;
    m_layerPreview = QImage();
    m_tab->afterStructureEdit();
    emit interactionFinished();
}

void CanvasView::cancelOverlay() {
    m_transformActive = false;
    m_perspectiveActive = false;
    m_layerPreview = QImage();
    m_st.kind = InteractState::None;
    update();
}
