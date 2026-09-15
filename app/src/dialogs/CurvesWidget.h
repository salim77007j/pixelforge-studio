#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>

// Interactive tone curve editor with histogram backdrop.
class CurvesWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurvesWidget(QWidget *parent = nullptr);
    void setHistogram(const uint32_t *bins /* 256 luma */);
    QVector<QPointF> points() const { return m_points; }
    void reset();
signals:
    void curveChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    QPointF toWidget(const QPointF &p) const;
    QPointF toCurve(const QPointF &p) const;
    int hitPoint(const QPointF &p) const;
    void addPoint(const QPointF &p);
    QVector<QPointF> m_points = {{0, 0}, {255, 255}};
    int m_drag = -1;
    uint32_t m_hist[256] = {0};
    bool m_hasHist = false;
};
