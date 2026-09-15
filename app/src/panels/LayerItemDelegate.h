#pragma once
#include <QStyledItemDelegate>

// Draws layer rows like the design: [eye][thumbnail][name][lock]
class LayerItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int EyeSize = 18;
    static constexpr int ThumbSize = 34;
    static constexpr int LockSize = 18;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    static QRect eyeRect(const QRect &row);
    static QRect thumbRect(const QRect &row);
    static QRect lockRect(const QRect &row);
};
