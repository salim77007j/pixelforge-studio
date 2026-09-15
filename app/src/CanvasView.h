#pragma once
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QVector>
#include <QPointF>
#include "AppTypes.h"

class EditorTab;
class QToolButton;

class CanvasView : public QWidget {
    Q_OBJECT
public:
    explicit CanvasView(EditorTab *tab, QWidget *parent = nullptr);

    double zoom() const { return m_zoom; }
    void setZoom(double z, const QPointF &anchorViewport);
    void fitToWindow();
    void zoomIn() { setZoom(m_zoom * 1.25, rect().center()); }
    void zoomOut() { setZoom(m_zoom / 1.25, rect().center()); }
    void setZoomImmediate(double z);

    QPointF toDoc(const QPointF &viewport) const;
    QPointF toViewport(const QPointF &doc) const;
    QRect docToViewportRect(const QRectF &docRect) const;

    void refreshComposite();                 // full re-composite from engine
    void refreshRegion(const QRect &docRect);// partial (stroke dirty rect)
    void selectionChanged();                 // re-fetch ants outline

    void enterTransformMode();               // Ctrl+T
    void enterPerspectiveMode();
    bool inTransformMode() const { return m_transformActive; }
    bool inPerspectiveMode() const { return m_perspectiveActive; }
    void commitTransform();
    void commitPerspective();
    void cancelOverlay();

    void applyCropOverlay();
    bool hasCropRect() const { return m_cropRect.isValid(); }
    void cancelCrop();

    void updateCursorShape();

signals:
    void cursorMoved(const QPointF &docPos);
    void zoomChanged(double zoom);
    void interactionFinished(); // panels should refresh
    void colorPicked(const QColor &c, bool background);
    void statusMessage(const QString &msg);

public slots:
    void tickAnts();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void tabletEvent(QTabletEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;
    void focusInEvent(QFocusEvent *) override;

private:
    struct InteractState {
        enum Kind { None, Panning, SelRect, SelEllipse, Lasso, MoveLayer,
                    CropDrag, GradientDrag, ShapeDrag, PolyCollect,
                    TransformHandle, TransformMove, PerspectiveHandle, HandDrag } kind = None;
        QPointF pressDoc;
        QPointF lastDoc;
        QPointF curDoc;
        QVector<QPointF> lassoPts;
        int handleIndex = -1;
    };

    void handlePress(const QPointF &docPos, const QPointF &vp, double pressure);
    void handleMove(const QPointF &docPos, const QPointF &vp, double pressure);
    void handleRelease(const QPointF &docPos, const QPointF &vp, double pressure);
    Qt::KeyboardModifiers e_modifiers() const;
    void beginStroke(const QPointF &docPos, double pressure);
    void moveStroke(const QPointF &docPos, double pressure);
    void endStroke();
    void pickColorAt(const QPointF &docPos, bool background);
    void floodFillAt(const QPointF &docPos);
    void wandAt(const QPointF &docPos);
    void shapeCommit(const QVector<QPointF> &pts);
    void gradientCommit(const QPointF &p0, const QPointF &p1);
    void finishTextAt(const QPointF &docPos);
    int selModeFromModifiers() const;

    // transform overlay helpers
    QRectF layerDocRect() const;         // active layer rect in doc coords
    void drawTransformOverlay(class QPainter &p);
    void drawPerspectiveOverlay(class QPainter &p);
    int hitTransformHandle(const QPointF &docPos, bool &rotate) const;
    int hitPerspectiveHandle(const QPointF &docPos) const;
    void ensureLayerPreviewImage();
    void showCropBar();
    void hideCropBar();

    EditorTab *m_tab;
    double m_zoom = 1.0;
    QPointF m_origin;          // doc(0,0) in viewport coords
    QImage m_composite;        // cached engine composite (RGBA8888)
    QTimer m_antsTimer;
    int m_antsOffset = 0;
    bool m_strokeActive = false;
    bool m_userZoomed = false; // false = keep auto-fitting to window
    InteractState m_st;
    bool m_spaceDown = false;
    bool m_midButton = false;
    QPointF m_lastMouseVp;
    bool m_mouseInside = false;
    QPointF m_lastStrokeEnd;   // for shift-line

    bool m_transformActive = false;
    bool m_perspectiveActive = false;
    class QTransform m_transform;         // accumulated preview transform
    QRectF m_transformOrigRect;
    int m_transformHandle = -1;
    bool m_transformRotating = false;
    double m_transformStartAngle = 0;
    QImage m_layerPreview;
    QPointF m_perspectiveCorners[4];      // doc coords
    bool m_perspectiveFrom = false;

    QWidget *m_cropBar = nullptr;
    QToolButton *m_cropApply = nullptr;
    QToolButton *m_cropCancel = nullptr;
    QRectF m_cropRect;
};
