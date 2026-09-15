#include "CurvesWidget.h"
#include "../Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

CurvesWidget::CurvesWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(280, 280);
    setMouseTracking(true);
}

void CurvesWidget::setHistogram(const uint32_t *bins) {
    if (!bins) { m_hasHist = false; return; }
    memcpy(m_hist, bins, sizeof(m_hist));
    m_hasHist = true;
    update();
}

QPointF CurvesWidget::toWidget(const QPointF &p) const {
    return QPointF(16 + p.x() / 255.0 * (width() - 32),
                   height() - 16 - p.y() / 255.0 * (height() - 32));
}

QPointF CurvesWidget::toCurve(const QPointF &p) const {
    return QPointF(qBound(0.0, (p.x() - 16) / (width() - 32) * 255.0, 255.0),
                   qBound(0.0, (height() - 16 - p.y()) / (height() - 32) * 255.0, 255.0));
}

int CurvesWidget::hitPoint(const QPointF &p) const {
    for (int i = 0; i < m_points.size(); ++i) {
        if (QLineF(toWidget(m_points[i]), p).length() < 9) return i;
    }
    return -1;
}

void CurvesWidget::addPoint(const QPointF &p) {
    m_points.append(p);
    std::sort(m_points.begin(), m_points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
}

void CurvesWidget::mousePressEvent(QMouseEvent *e) {
    QPointF cp = toCurve(e->position());
    int h = hitPoint(e->position());
    if (e->button() == Qt::RightButton || e->modifiers() & Qt::AltModifier) {
        if (h > 0 && h < m_points.size() - 1 && m_points.size() > 2) {
            m_points.removeAt(h);
            emit curveChanged();
            update();
        }
        return;
    }
    if (h >= 0) {
        m_drag = h;
    } else {
        addPoint(cp);
        m_drag = m_points.indexOf(cp);
        if (m_drag < 0) m_drag = m_points.size() - 1;
        emit curveChanged();
    }
    update();
}

void CurvesWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_drag < 0) {
        setCursor(hitPoint(e->position()) >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
        return;
    }
    QPointF cp = toCurve(e->position());
    // clamp x between neighbors
    double xmin = 0, xmax = 255;
    if (m_drag > 0) xmin = m_points[m_drag - 1].x() + 2;
    if (m_drag < m_points.size() - 1) xmax = m_points[m_drag + 1].x() - 2;
    cp.setX(qBound(xmin, cp.x(), xmax));
    if (m_drag == 0) cp.setX(0);
    if (m_drag == m_points.size() - 1) cp.setX(255);
    m_points[m_drag] = cp;
    emit curveChanged();
    update();
}

void CurvesWidget::mouseReleaseEvent(QMouseEvent *) { m_drag = -1; }

void CurvesWidget::wheelEvent(QWheelEvent *) {}

void CurvesWidget::reset() {
    m_points = {{0, 0}, {255, 255}};
    emit curveChanged();
    update();
}

void CurvesWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(14, 14, -14, -14);
    // card
    p.setPen(QPen(QColor(Theme::Border), 1));
    p.setBrush(QColor(Theme::CardBg));
    p.drawRoundedRect(rect(), 10, 10);
    // grid
    p.setPen(QPen(QColor(Theme::BorderSoft), 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.right(), r.top()));
    // histogram
    if (m_hasHist) {
        uint32_t maxv = 1;
        for (uint32_t v : m_hist) maxv = qMax(maxv, v);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(74, 144, 226, 60));
        QPainterPath hp;
        hp.moveTo(r.left(), r.bottom());
        for (int i = 0; i < 256; ++i) {
            double y = r.bottom() - (double)m_hist[i] / maxv * r.height() * 0.92;
            hp.lineTo(r.left() + (double)i / 255.0 * r.width(), y);
        }
        hp.lineTo(r.right(), r.bottom());
        hp.closeSubpath();
        p.drawPath(hp);
    }
    // monotone cubic curve through points (same family as engine)
    if (m_points.size() >= 2) {
        std::sort(m_points.begin(), m_points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        int n = m_points.size();
        QVector<double> xs, ys, d, m(n), out;
        for (auto &pt : m_points) { xs << pt.x(); ys << pt.y(); }
        d.resize(n);
        for (int i = 0; i < n - 1; ++i)
            d[i] = (ys[i + 1] - ys[i]) / qMax(1e-6, xs[i + 1] - xs[i]);
        d[n - 1] = d[n - 2];
        m[0] = d[0]; m[n - 1] = d[n - 2];
        for (int i = 1; i < n - 1; ++i) {
            if (d[i - 1] * d[i] <= 0) m[i] = 0;
            else {
                double w1 = 2 * (xs[i + 1] - xs[i]) + (xs[i] - xs[i - 1]);
                double w2 = (xs[i + 1] - xs[i]) + 2 * (xs[i] - xs[i - 1]);
                m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i]);
            }
        }
        QPainterPath path;
        path.moveTo(toWidget(m_points.first()));
        for (int i = 0; i < n - 1; ++i) {
            double h = qMax(1e-6, xs[i + 1] - xs[i]);
            for (int s = 1; s <= 24; ++s) {
                double t = (double)s / 24.0;
                double t2 = t * t, t3 = t2 * t;
                double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
                double h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                double x = xs[i] + h * t;
                double y = h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1];
                path.lineTo(toWidget(QPointF(x, y)));
            }
        }
        p.setPen(QPen(QColor(Theme::Accent), 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // points
    p.setPen(QPen(QColor("#FFFFFF"), 2));
    p.setBrush(QColor(Theme::Accent));
    for (const QPointF &pt : m_points) {
        p.drawEllipse(toWidget(pt), 5, 5);
    }
}
