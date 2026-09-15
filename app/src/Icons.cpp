#include "Icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QApplication>
#include <functional>

namespace Icons {

static QIcon make(int size, const std::function<void(QPainter &)> &paint) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    paint(p);
    p.end();
    return QIcon(pm);
}

static QPen linePen(QPainter &p, qreal w = 2.0) {
    QPen pen(QColor("#3A4250"), w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    return pen;
}

QIcon get(Tool t) {
    auto s32 = [t](QPainter &p) {
        linePen(p);
        auto r = p.viewport();
        switch (t) {
        case Move:
            p.drawPoint(r.center());
            p.drawLine(16, 5, 16, 27); p.drawLine(5, 16, 27, 16);
            p.drawLine(16, 5, 11, 10); p.drawLine(16, 5, 21, 10);
            p.drawLine(16, 27, 11, 22); p.drawLine(16, 27, 21, 22);
            p.drawLine(5, 16, 10, 11); p.drawLine(5, 16, 10, 21);
            p.drawLine(27, 16, 22, 11); p.drawLine(27, 16, 22, 21);
            break;
        case RectSelect: {
            QPen pen = linePen(p);
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({3.0, 2.4});
            p.setPen(pen);
            p.drawRoundedRect(6, 8, 20, 16, 2, 2);
            break;
        }
        case EllipseSelect: {
            QPen pen = linePen(p);
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({3.0, 2.4});
            p.setPen(pen);
            p.drawEllipse(6, 8, 20, 16);
            break;
        }
        case Lasso: {
            QPen pen = linePen(p);
            pen.setStyle(Qt::DashLine);
            pen.setDashPattern({3.0, 2.4});
            p.setPen(pen);
            QPainterPath path;
            path.moveTo(8, 10);
            path.cubicTo(14, 4, 26, 6, 25, 13);
            path.cubicTo(24, 20, 18, 18, 15, 22);
            path.cubicTo(13, 25, 8, 26, 8, 22);
            path.closeSubpath();
            p.drawPath(path);
            p.setPen(linePen(p));
            p.drawLine(8, 24, 5, 28);
            break;
        }
        case Wand: {
            p.drawLine(17, 15, 8, 24);
            QPainterPath star;
            star.moveTo(20, 4); star.lineTo(22, 10); star.lineTo(28, 12);
            star.lineTo(22, 14); star.lineTo(20, 20); star.lineTo(18, 14);
            star.lineTo(12, 12); star.lineTo(18, 10); star.closeSubpath();
            p.drawPath(star);
            p.drawLine(24, 20, 27, 17);
            p.drawPoint(28, 8);
            break;
        }
        case Crop:
            p.drawLine(9, 4, 9, 23); p.drawLine(9, 23, 28, 23);
            p.drawLine(4, 9, 23, 9); p.drawLine(23, 9, 23, 28);
            break;
        case Eyedropper: {
            p.drawLine(19, 6, 26, 13);
            QPainterPath body;
            body.moveTo(19, 6);
            body.lineTo(23, 2);
            body.lineTo(30, 9);
            body.lineTo(26, 13);
            body.lineTo(19, 6);
            p.drawPath(body);
            QPainterPath pip;
            pip.moveTo(20, 12);
            pip.lineTo(8, 24);
            pip.lineTo(4, 28);
            pip.lineTo(6, 22);
            pip.lineTo(18, 10);
            p.drawPath(pip);
            break;
        }
        case Brush: {
            QPainterPath handle;
            handle.moveTo(27, 5);
            handle.lineTo(28, 6);
            handle.lineTo(15, 19);
            handle.lineTo(13, 17);
            handle.lineTo(26, 4);
            handle.closeSubpath();
            p.drawPath(handle);
            QPainterPath ferrule;
            ferrule.moveTo(13, 17);
            ferrule.lineTo(15, 19);
            ferrule.lineTo(11, 23);
            ferrule.lineTo(9, 21);
            p.drawPath(ferrule);
            QPainterPath bristles;
            bristles.moveTo(11, 23);
            bristles.lineTo(9, 21);
            bristles.cubicTo(4, 24, 3, 27, 6, 28);
            bristles.cubicTo(7, 26, 9, 25, 11, 23);
            p.setBrush(QColor("#3A4250"));
            p.drawPath(bristles);
            break;
        }
        case Pencil: {
            QPainterPath pp;
            pp.moveTo(26, 6);
            pp.lineTo(28, 8);
            pp.lineTo(11, 25);
            pp.lineTo(5, 27);
            pp.lineTo(7, 21);
            pp.lineTo(24, 4);
            pp.closeSubpath();
            p.drawPath(pp);
            p.drawLine(23, 7, 25, 9);
            break;
        }
        case Eraser: {
            QPainterPath e;
            e.moveTo(16, 6);
            e.lineTo(27, 17);
            e.lineTo(19, 25);
            e.lineTo(8, 14);
            e.closeSubpath();
            p.drawPath(e);
            p.drawLine(12, 10, 21, 19);
            p.drawLine(6, 27, 13, 21);
            break;
        }
        case Fill: {
            QPainterPath bucket;
            bucket.moveTo(14, 6);
            bucket.lineTo(25, 17);
            bucket.lineTo(16, 26);
            bucket.lineTo(5, 15);
            bucket.closeSubpath();
            p.drawPath(bucket);
            p.drawLine(9, 11, 17, 3);
            QPainterPath drop;
            drop.moveTo(27, 21);
            drop.cubicTo(30, 24, 30, 27, 28, 28);
            drop.cubicTo(26, 27, 25, 24, 27, 21);
            p.setBrush(QColor("#4A90E2"));
            p.setPen(Qt::NoPen);
            p.drawPath(drop);
            break;
        }
        case Gradient: {
            QPainterPath g;
            g.moveTo(16, 4); g.lineTo(28, 10); g.lineTo(16, 16); g.lineTo(4, 10);
            g.closeSubpath();
            p.drawPath(g);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 6; ++i) {
                qreal a = 0.65 - i * 0.1;
                p.setOpacity(a > 0 ? a : 0.06);
                p.setBrush(QColor("#3A4250"));
                p.drawRect(QRectF(4, 18.0 + i * 1.8, 24 - i * 4, 1.6));
            }
            p.setOpacity(1.0);
            break;
        }
        case Text:
            p.drawLine(8, 7, 24, 7);
            p.drawLine(16, 7, 16, 26);
            p.drawLine(12, 26, 20, 26);
            break;
        case Shape: {
            QPainterPath sp;
            sp.moveTo(16, 5); sp.lineTo(27, 26); sp.lineTo(5, 26); sp.closeSubpath();
            p.drawPath(sp);
            break;
        }
        case Transform: {
            p.drawRoundedRect(7, 7, 18, 18, 2, 2);
            p.setBrush(QColor("#4A90E2"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(7, 7), 3, 3);
            p.drawEllipse(QPointF(25, 7), 3, 3);
            p.drawEllipse(QPointF(7, 25), 3, 3);
            p.drawEllipse(QPointF(25, 25), 3, 3);
            break;
        }
        case Perspective: {
            QPainterPath pp;
            pp.moveTo(8, 4); pp.lineTo(27, 9); pp.lineTo(24, 27); pp.lineTo(5, 22);
            pp.closeSubpath();
            p.drawPath(pp);
            p.setBrush(QColor("#4A90E2"));
            p.setPen(Qt::NoPen);
            for (QPointF pt : {QPointF(8, 4), QPointF(27, 9), QPointF(24, 27), QPointF(5, 22)})
                p.drawEllipse(pt, 2.6, 2.6);
            break;
        }
        case Hand: {
            QPainterPath h;
            h.moveTo(11, 28);
            h.cubicTo(5, 24, 4, 18, 8, 17);
            h.cubicTo(10, 16, 11, 17, 12, 19);
            h.lineTo(12, 8);
            h.cubicTo(12, 5, 16, 5, 16, 8);
            h.lineTo(16, 6);
            h.cubicTo(16, 3, 20, 3, 20, 6);
            h.lineTo(20, 9);
            h.cubicTo(21, 7, 24, 8, 24, 11);
            h.lineTo(24, 20);
            h.cubicTo(24, 26, 20, 28, 17, 28);
            p.drawPath(h);
            break;
        }
        case Zoom:
            p.drawEllipse(6, 6, 17, 17);
            p.drawLine(20, 20, 27, 27);
            p.drawLine(13, 10, 13, 19); p.drawLine(9, 14, 18, 14);
            break;
        case Undo:
            p.drawArc(QRect(7, 8, 18, 16), 30 * 16, 260 * 16);
            {
                QPainterPath ar;
                ar.moveTo(12, 5);
                ar.lineTo(6, 11);
                ar.lineTo(13, 14);
                p.setBrush(QColor("#3A4250"));
                p.setPen(Qt::NoPen);
                p.drawPath(ar);
            }
            break;
        case Redo:
            p.drawArc(QRect(7, 8, 18, 16), 120 * 16, 260 * 16);
            {
                QPainterPath ar;
                ar.moveTo(20, 5);
                ar.lineTo(26, 11);
                ar.lineTo(19, 14);
                p.setBrush(QColor("#3A4250"));
                p.setPen(Qt::NoPen);
                p.drawPath(ar);
            }
            break;
        case FlipH: {
            p.drawLine(16, 5, 16, 27);
            QPainterPath tri1;
            tri1.moveTo(9, 10); tri1.lineTo(14, 16); tri1.lineTo(9, 22); tri1.closeSubpath();
            p.drawPath(tri1);
            QPainterPath tri2;
            tri2.moveTo(23, 10); tri2.lineTo(18, 16); tri2.lineTo(23, 22); tri2.closeSubpath();
            p.drawPath(tri2);
            break;
        }
        case FlipV: {
            p.drawLine(5, 16, 27, 16);
            QPainterPath tri1;
            tri1.moveTo(10, 9); tri1.lineTo(16, 14); tri1.lineTo(22, 9); tri1.closeSubpath();
            p.drawPath(tri1);
            QPainterPath tri2;
            tri2.moveTo(10, 23); tri2.lineTo(16, 18); tri2.lineTo(22, 23); tri2.closeSubpath();
            p.drawPath(tri2);
            break;
        }
        case Clock:
            p.drawEllipse(6, 6, 20, 20);
            p.drawLine(16, 10, 16, 16);
            p.drawLine(16, 16, 21, 19);
            break;
        case Filter:
            p.drawLine(5, 8, 27, 8);
            p.drawLine(9, 16, 23, 16);
            p.drawLine(13, 24, 19, 24);
            break;
        case Help:
            p.drawEllipse(5, 5, 22, 22);
            p.drawArc(QRectF(12, 11, 8, 7), 180 * 16, -180 * 16);
            p.drawPoint(16, 22);
            break;
        case Plus:
            p.drawLine(16, 7, 16, 25); p.drawLine(7, 16, 25, 16);
            break;
        case Duplicate: {
            p.setBrush(QColor(0xF7, 0xF8, 0xFA));
            p.setPen(QPen(QColor("#8A93A0"), 2));
            p.drawRoundedRect(5, 10, 16, 16, 2, 2);
            p.setBrush(Qt::NoBrush);
            linePen(p);
            p.drawRoundedRect(11, 4, 16, 16, 2, 2);
            break;
        }
        case Trash: {
            QPainterPath body;
            body.moveTo(9, 10); body.lineTo(10, 27); body.lineTo(22, 27); body.lineTo(23, 10);
            p.drawPath(body);
            p.drawLine(6, 10, 26, 10);
            QPainterPath lid;
            lid.moveTo(13, 6); lid.lineTo(19, 6); lid.lineTo(20, 10); lid.lineTo(12, 10); lid.closeSubpath();
            p.drawPath(lid);
            p.drawLine(14, 14, 14, 23); p.drawLine(18, 14, 18, 23);
            break;
        }
        case Folder: {
            QPainterPath f;
            f.moveTo(4, 25); f.lineTo(4, 9); f.lineTo(12, 9); f.lineTo(15, 13);
            f.lineTo(28, 13); f.lineTo(28, 25); f.closeSubpath();
            p.drawPath(f);
            break;
        }
        case MaskIcon:
            p.drawRoundedRect(5, 5, 22, 22, 3, 3);
            p.drawEllipse(11, 11, 10, 10);
            break;
        case EyeOpen: {
            QPainterPath e;
            e.moveTo(5, 16);
            e.cubicTo(10, 8, 22, 8, 27, 16);
            e.cubicTo(22, 24, 10, 24, 5, 16);
            p.drawPath(e);
            p.drawEllipse(13, 13, 6, 6);
            break;
        }
        case EyeClosed: {
            QPainterPath e;
            e.moveTo(5, 17);
            e.cubicTo(10, 25, 22, 25, 27, 17);
            p.drawPath(e);
            p.drawLine(16, 17, 16, 22);
            break;
        }
        case LockOpen:
            p.drawRoundedRect(8, 14, 16, 13, 2, 2);
            p.drawArc(QRect(11, 5, 10, 12), 0, 180 * 16);
            break;
        case LockClosed:
            p.drawRoundedRect(8, 14, 16, 13, 2, 2);
            p.drawArc(QRect(11, 5, 10, 12), 180 * 16, 180 * 16);
            break;
        case MergeDown: {
            p.drawLine(16, 5, 16, 19);
            QPainterPath head;
            head.moveTo(11, 14); head.lineTo(16, 19); head.lineTo(21, 14);
            p.drawPath(head);
            p.drawLine(7, 25, 25, 25);
            break;
        }
        case Up: {
            p.drawLine(16, 25, 16, 8);
            QPainterPath head;
            head.moveTo(9, 15); head.lineTo(16, 8); head.lineTo(23, 15);
            p.drawPath(head);
            break;
        }
        case Down: {
            p.drawLine(16, 7, 16, 24);
            QPainterPath head;
            head.moveTo(9, 17); head.lineTo(16, 24); head.lineTo(23, 17);
            p.drawPath(head);
            break;
        }
        case NewLayer: {
            p.drawRoundedRect(7, 5, 18, 8, 2, 2);
            p.drawRoundedRect(7, 19, 18, 8, 2, 2);
            p.setBrush(QColor("#4A90E2"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(16, 16), 4.5, 4.5);
            p.setPen(QPen(Qt::white, 2));
            p.drawLine(16, 14, 16, 18); p.drawLine(14, 16, 18, 16);
            break;
        }
        case SoftRound:
        case HardRound: {
            QPainterPath st;
            st.moveTo(5, 22);
            st.cubicTo(9, 12, 13, 10, 16, 10);
            st.cubicTo(20, 10, 24, 13, 27, 22);
            st.closeSubpath();
            p.setPen(QPen(QColor("#3A4250"), 2));
            if (t == SoftRound) {
                QPainterPathStroker stroker;
                stroker.setWidth(4);
                stroker.setCapStyle(Qt::RoundCap);
                QPainterPath fill = stroker.createStroke(st);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor("#3A4250"));
                p.drawPath(st.simplified());
                p.setOpacity(0.45);
                p.drawPath(stroker.createStroke(st).simplified());
                p.setOpacity(1.0);
            } else {
                p.setBrush(QColor("#3A4250"));
                p.drawPath(st);
            }
            break;
        }
        case TaperedInk: {
            QPainterPath st;
            st.moveTo(5, 25);
            st.cubicTo(12, 22, 18, 16, 27, 5);
            st.cubicTo(22, 15, 16, 21, 5, 25);
            st.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#3A4250"));
            p.drawPath(st);
            break;
        }
        case Marker: {
            QPainterPath st;
            st.moveTo(5, 12);
            st.cubicTo(12, 8, 22, 8, 27, 12);
            st.cubicTo(22, 16, 12, 16, 5, 12);
            st.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#3A4250"));
            p.drawPath(st);
            p.setPen(QPen(QColor("#3A4250"), 2));
            p.drawLine(8, 22, 24, 22);
            break;
        }
        case Airbrush: {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#3A4250"));
            p.drawEllipse(QPointF(14, 20), 6, 6);
            p.setOpacity(0.35);
            for (int i = 0; i < 10; ++i) {
                qreal a = i * 1.9;
                p.drawEllipse(QPointF(14 + 10 * cos(a), 20 + 9 * sin(a)), 2.2, 2.2);
            }
            p.setOpacity(1.0);
            p.setPen(QPen(QColor("#3A4250"), 2));
            p.drawLine(21, 14, 27, 5);
            break;
        }
        case Reset: {
            p.drawArc(QRect(7, 7, 18, 18), 100 * 16, 300 * 16);
            QPainterPath arrow;
            arrow.moveTo(20, 5); arrow.lineTo(24, 9); arrow.lineTo(19, 10); arrow.closeSubpath();
            p.setBrush(QColor("#3A4250"));
            p.setPen(Qt::NoPen);
            p.drawPath(arrow);
            break;
        }
        case Check:
            p.drawLine(8, 17, 14, 23); p.drawLine(14, 23, 25, 9);
            break;
        case Cross:
            p.drawLine(10, 10, 22, 22); p.drawLine(22, 10, 10, 22);
            break;
        case Image: {
            p.drawRoundedRect(5, 7, 22, 18, 2, 2);
            p.drawPoint(11, 12);
            QPainterPath horizon;
            horizon.moveTo(7, 21); horizon.lineTo(13, 15); horizon.lineTo(17, 18); horizon.lineTo(21, 14); horizon.lineTo(25, 18);
            p.drawPath(horizon);
            break;
        }
        case Open:
            p.drawRoundedRect(5, 7, 10, 8, 2, 2);
            p.drawRoundedRect(13, 14, 14, 11, 2, 2);
            break;
        }
    };
    QIcon base = make(32, s32);
    QIcon out;
    out.addPixmap(base.pixmap(16, 16));
    out.addPixmap(base.pixmap(20, 20));
    out.addPixmap(base.pixmap(24, 24));
    out.addPixmap(base.pixmap(32, 32));
    out.addPixmap(base.pixmap(48, 48));
    return out;
}

} // namespace Icons
