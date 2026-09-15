#include "LayerItemDelegate.h"
#include "../Icons.h"
#include "../Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTreeWidget>

QSize LayerItemDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const {
    return QSize(120, 42);
}

QRect LayerItemDelegate::eyeRect(const QRect &row) {
    return QRect(row.left() + 8, row.center().y() - EyeSize / 2 - 1, EyeSize, EyeSize);
}

QRect LayerItemDelegate::thumbRect(const QRect &row) {
    return QRect(row.left() + 34, row.center().y() - ThumbSize / 2, ThumbSize, ThumbSize);
}

QRect LayerItemDelegate::lockRect(const QRect &row) {
    return QRect(row.right() - LockSize - 10, row.center().y() - LockSize / 2 - 1, LockSize, LockSize);
}

void LayerItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const {
    QStyleOptionViewItem option = opt;
    initStyleOption(&option, index);
    bool selected = option.state & QStyle::State_Selected;
    QRect row = option.rect;

    // row background
    if (selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(Theme::AccentChip));
        QPainterPath path;
        path.addRoundedRect(QRectF(row).adjusted(2, 1.5, -2, -1.5), 7, 7);
        p->drawPath(path);
    } else if (option.state & QStyle::State_MouseOver) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(Theme::Hover));
        QPainterPath path;
        path.addRoundedRect(QRectF(row).adjusted(2, 1.5, -2, -1.5), 7, 7);
        p->drawPath(path);
    }

    bool visible = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    bool locked = index.data(Qt::UserRole + 2).toBool();
    bool isGroup = index.data(Qt::UserRole).toJsonObject()["kind"].toString() == "group";

    // eye
    QIcon eye = Icons::get(visible ? Icons::EyeOpen : Icons::EyeClosed);
    p->drawPixmap(eyeRect(row), eye.pixmap(EyeSize, EyeSize));

    // thumbnail
    QRect tr = thumbRect(row);
    if (isGroup) {
        QIcon folder = Icons::get(Icons::Folder);
        p->drawPixmap(tr, folder.pixmap(ThumbSize, ThumbSize));
    } else {
        QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        QPixmap pm = icon.pixmap(ThumbSize, ThumbSize);
        p->setPen(QPen(QColor(Theme::Border), 1));
        p->setBrush(QColor("#FFFFFF"));
        p->drawRoundedRect(QRectF(tr).adjusted(-1, -1, 1, 1), 4, 4);
        if (!pm.isNull())
            p->drawPixmap(tr, pm.scaled(tr.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // name
    QString name = index.data(Qt::DisplayRole).toString();
    p->setPen(QColor(selected ? QColor(Theme::AccentDark) : QColor(Theme::Text)));
    QFont f = option.font;
    f.setPixelSize(12);
    p->setFont(f);
    QRect nameRect = row.adjusted(thumbRect(row).right() - row.left() + 10, 0, -LockSize - 22, 0);
    p->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                option.fontMetrics.elidedText(name, Qt::ElideMiddle, nameRect.width()));

    // mask badge
    bool hasMask = index.data(Qt::UserRole).toJsonObject()["has_mask"].toBool();
    int x = nameRect.left();
    if (hasMask) {
        p->setPen(QColor(Theme::TextSecondary));
        QFont mf = f;
        mf.setPixelSize(10);
        p->setFont(mf);
        p->drawText(QRect(nameRect.left(), row.bottom() - 14, nameRect.width(), 12), Qt::AlignLeft, "◈ mask");
    }

    // lock
    if (locked) {
        QIcon lock = Icons::get(Icons::LockClosed);
        p->drawPixmap(lockRect(row), lock.pixmap(LockSize, LockSize));
    }
}

bool LayerItemDelegate::editorEvent(QEvent *ev, QAbstractItemModel *model,
                                    const QStyleOptionViewItem &option, const QModelIndex &index) {
    if (ev->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(ev);
        // note: rect in index coords uses visualRect of the item
        QTreeWidget *tree = qobject_cast<QTreeWidget *>(parent());
        if (!tree) return false;
        QRect row = tree->visualRect(index);
        if (eyeRect(row).contains(me->pos())) {
            bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
            model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            return true;
        }
        if (lockRect(row).contains(me->pos())) {
            model->setData(index, !index.data(Qt::UserRole + 2).toBool(), Qt::UserRole + 2);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(ev, model, option, index);
}
