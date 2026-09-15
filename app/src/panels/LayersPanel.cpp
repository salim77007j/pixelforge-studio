#include "LayersPanel.h"
#include "../MainWindow.h"
#include "../EditorTab.h"
#include "../Icons.h"
#include "../Theme.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QToolButton>
#include <QPushButton>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMenu>
#include <QFileInfo>
#include <QTimer>
#include <functional>
#include <tuple>
#include "LayerItemDelegate.h"

LayersPanel::LayersPanel(MainWindow *mw, QWidget *parent)
    : QWidget(parent), m_mw(mw) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    m_blend = new QComboBox;
    const char *modes[] = {"Normal","Multiply","Screen","Overlay","Darken","Lighten","Color Dodge","Color Burn",
                            "Hard Light","Soft Light","Difference","Exclusion","Hue","Saturation","Color","Luminosity"};
    for (auto *mm : modes) m_blend->addItem(mm);
    m_blend->setFixedHeight(30);
    connect(m_blend, &QComboBox::currentTextChanged, this, &LayersPanel::onBlendChanged);

    auto *opRow = new QHBoxLayout;
    auto *opLabel = new QLabel(tr("Opacity"));
    opLabel->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(Theme::TextSecondary));
    m_opacityLabel = new QLabel("100%");
    m_opacityLabel->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(Theme::TextSecondary));
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacity->setValue(100);
    connect(m_opacity, &QSlider::valueChanged, this, &LayersPanel::onOpacityChanged);
    opRow->addWidget(opLabel);
    opRow->addStretch(1);
    opRow->addWidget(m_opacityLabel);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->header()->hide();
    m_tree->setMinimumHeight(140);
    m_tree->setItemDelegate(new LayerItemDelegate(m_tree));
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background:%1; border:1px solid %2; border-radius:9px; }"
        "QTreeWidget::item { height:38px; border-radius:7px; margin:1px 2px; }"
        "QTreeWidget::item:hover { background:%3; }"
        "QTreeWidget::item:selected { background:%4; }"
    ).arg(Theme::CardBg, Theme::Border, Theme::Hover, Theme::AccentChip));
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LayersPanel::onSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayersPanel::onItemChanged);
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved, this, &LayersPanel::onItemMoved);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(3);
    auto mkBtn = [this, btnRow](Icons::Tool ic, const QString &tip, const char *slot) {
        auto *b = new QToolButton;
        b->setIcon(Icons::get(ic));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(18, 18));
        connect(b, SIGNAL(clicked()), this, slot);
        btnRow->addWidget(b);
        return b;
    };
    mkBtn(Icons::NewLayer, tr("New layer"), SLOT(onAddLayer()));
    mkBtn(Icons::MaskIcon, tr("Add layer mask (from selection)"), SLOT(onAddMask()));
    mkBtn(Icons::Folder, tr("New group"), SLOT(onAddGroup()));
    mkBtn(Icons::Duplicate, tr("Duplicate layer"), SLOT(onDuplicate()));
    btnRow->addStretch(1);
    mkBtn(Icons::Up, tr("Move up"), SLOT(onMoveUp()));
    mkBtn(Icons::Down, tr("Move down"), SLOT(onMoveDown()));
    mkBtn(Icons::MergeDown, tr("Merge down"), SLOT(onMergeDown()));
    mkBtn(Icons::Trash, tr("Delete layer"), SLOT(onDelete()));

    lay->addWidget(m_blend);
    lay->addLayout(opRow);
    lay->addWidget(m_opacity);
    lay->addWidget(m_tree, 1);
    lay->addLayout(btnRow);
}

void LayersPanel::refresh(EditorTab *tab) {
    m_tab = tab;
    m_rebuilding = true;
    m_tree->clear();
    if (!tab || !tab->m_doc.isValid()) {
        m_rebuilding = false;
        return;
    }
    QString json = tab->m_doc.layersJson();
    // format: <json>|<activeId>
    int sep = json.lastIndexOf('|');
    QString tree = json.left(sep);
    uint64_t active = json.mid(sep + 1).toULongLong();
    QJsonDocument doc = QJsonDocument::fromJson(tree.toUtf8());
    if (!doc.isObject()) { m_rebuilding = false; return; }
    QJsonObject root = doc.object();
    buildTree(m_tree->invisibleRootItem(), root["children"].toArray());
    m_rebuilding = false;
    // reselect active
    reselectCurrent();
    // update blend/opacity of active
    std::function<void(QTreeWidgetItem *)> find = [&](QTreeWidgetItem *it) -> void {
        for (int i = 0; i < it->childCount(); ++i) {
            QTreeWidgetItem *c = it->child(i);
            if (c->data(0, Qt::UserRole + 1).toULongLong() == active) {
                m_tree->setCurrentItem(c);
                // sync controls
                QJsonObject o = c->data(0, Qt::UserRole).toJsonObject();
                m_blend->setCurrentText(o["blend"].toString());
                m_opacity->blockSignals(true);
                m_opacity->setValue(qRound(o["opacity"].toDouble() * 100));
                m_opacityLabel->setText(QString::number(qRound(o["opacity"].toDouble() * 100)) + "%");
                m_opacity->blockSignals(false);
                return;
            }
            find(c);
        }
    };
    find(m_tree->invisibleRootItem());
}

void LayersPanel::buildTree(QTreeWidgetItem *parentItem, const QJsonArray &arr) {
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        bool isGroup = o["kind"].toString() == "group";
        auto *item = new QTreeWidgetItem(parentItem);
        item->setText(0, o["name"].toString());
        item->setData(0, Qt::UserRole, o);
        item->setData(0, Qt::UserRole + 1, o["id"].toVariant().toULongLong());
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        item->setChildIndicatorPolicy(isGroup ? QTreeWidgetItem::ShowIndicator : QTreeWidgetItem::DontShowIndicator);
        // thumbnail
        if (!isGroup) {
            QImage thumb = m_tab->m_doc.layerThumbnail(o["id"].toVariant().toULongLong(), 48);
            if (!thumb.isNull()) {
                QPixmap pm = QPixmap::fromImage(thumb).scaled(34, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(0, pm);
            }
        } else {
            item->setIcon(0, Icons::get(Icons::Folder));
        }
        item->setCheckState(0, o["visible"].toBool() ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole + 2, o["locked"].toBool());
        item->setToolTip(0, o["name"].toString() + (o["has_mask"].toBool() ? QStringLiteral("  [masked]") : QString()));
        if (isGroup)
            buildTree(item, o["children"].toArray());
    }
}

void LayersPanel::onSelectionChanged() {
    if (m_rebuilding || !m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    uint64_t id = sel.first()->data(0, Qt::UserRole + 1).toULongLong();
    m_tab->m_doc.setActiveLayer(id);
    QJsonObject o = sel.first()->data(0, Qt::UserRole).toJsonObject();
    m_blend->blockSignals(true);
    m_blend->setCurrentText(o["blend"].toString());
    m_blend->blockSignals(false);
    m_opacity->blockSignals(true);
    m_opacity->setValue(qRound(o["opacity"].toDouble() * 100));
    m_opacityLabel->setText(QString::number(qRound(o["opacity"].toDouble() * 100)) + "%");
    m_opacity->blockSignals(false);
    m_tab->m_canvas->updateCursorShape();
}

void LayersPanel::onItemChanged(QTreeWidgetItem *item, int col) {
    if (m_rebuilding || !m_tab) return;
    uint64_t id = item->data(0, Qt::UserRole + 1).toULongLong();
    if (col == 0) {
        QJsonObject o = item->data(0, Qt::UserRole).toJsonObject();
        // visibility (eye)
        bool visible = item->checkState(0) == Qt::Checked;
        m_tab->m_doc.setLayerVisible(id, visible);
        // rename
        if (item->text(0) != o["name"].toString()) {
            m_tab->m_doc.setLayerName(id, item->text(0));
        }
        // lock (padlock click writes UserRole+2)
        bool locked = item->data(0, Qt::UserRole + 2).toBool();
        if (locked != o["locked"].toBool()) {
            m_tab->m_doc.setLayerLocked(id, locked);
        }
        m_tab->afterEdit();
    }
}

void LayersPanel::onItemMoved() {
    if (m_rebuilding || !m_tab) return;
    // Record final (id, parent, index) order, then replay top-down via engine moves.
    QVector<std::tuple<uint64_t, uint64_t, int>> order;
    std::function<void(QTreeWidgetItem *, uint64_t)> walk = [&](QTreeWidgetItem *parent, uint64_t pid) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *c = parent->child(i);
            uint64_t id = c->data(0, Qt::UserRole + 1).toULongLong();
            order.append({id, pid, i});
            walk(c, id);
        }
    };
    walk(m_tree->invisibleRootItem(), 0);
    for (const auto &o : order) {
        pf_layer_move(m_tab->m_doc.handle(), std::get<0>(o), std::get<1>(o), std::get<2>(o));
    }
    m_tab->afterStructureEdit();
}

void LayersPanel::reselectCurrent() {
}

void LayersPanel::onAddLayer() {
    if (!m_tab) return;
    m_tab->m_doc.addLayer(0, QStringLiteral("Layer %1").arg(m_tree->topLevelItemCount() + 1));
    m_tab->afterStructureEdit();
}

void LayersPanel::onAddGroup() {
    if (!m_tab) return;
    m_tab->m_doc.addLayer(1, QStringLiteral("Group %1").arg(m_tree->topLevelItemCount() + 1));
    m_tab->afterStructureEdit();
}

void LayersPanel::onDuplicate() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_tab->m_doc.duplicateLayer(sel.first()->data(0, Qt::UserRole + 1).toULongLong());
    m_tab->afterStructureEdit();
}

void LayersPanel::onDelete() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_tab->m_doc.removeLayer(sel.first()->data(0, Qt::UserRole + 1).toULongLong());
    m_tab->afterStructureEdit();
}

void LayersPanel::onAddMask() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_tab->m_doc.addMask(sel.first()->data(0, Qt::UserRole + 1).toULongLong(), true);
    m_tab->afterStructureEdit();
}

void LayersPanel::onMoveUp() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    QTreeWidgetItem *it = sel.first();
    QTreeWidgetItem *parent = it->parent() ? it->parent() : m_tree->invisibleRootItem();
    int idx = parent->indexOfChild(it);
    if (idx <= 0) return;
    pf_layer_move(m_tab->m_doc.handle(), it->data(0, Qt::UserRole + 1).toULongLong(),
                  parent == m_tree->invisibleRootItem() ? 0 : parent->data(0, Qt::UserRole + 1).toULongLong(),
                  idx - 1);
    m_tab->afterStructureEdit();
}

void LayersPanel::onMoveDown() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    QTreeWidgetItem *it = sel.first();
    QTreeWidgetItem *parent = it->parent() ? it->parent() : m_tree->invisibleRootItem();
    int idx = parent->indexOfChild(it);
    if (idx >= parent->childCount() - 1) return;
    pf_layer_move(m_tab->m_doc.handle(), it->data(0, Qt::UserRole + 1).toULongLong(),
                  parent == m_tree->invisibleRootItem() ? 0 : parent->data(0, Qt::UserRole + 1).toULongLong(),
                  idx + 1);
    m_tab->afterStructureEdit();
}

void LayersPanel::onMergeDown() {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_tab->m_doc.mergeDown(sel.first()->data(0, Qt::UserRole + 1).toULongLong());
    m_tab->afterStructureEdit();
}

void LayersPanel::onBlendChanged(const QString &name) {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_tab->m_doc.setLayerBlend(sel.first()->data(0, Qt::UserRole + 1).toULongLong(), name);
    m_tab->afterEdit();
}

void LayersPanel::onOpacityChanged(int v) {
    if (!m_tab) return;
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    if (sel.isEmpty()) return;
    m_opacityLabel->setText(QString::number(v) + "%");
    m_tab->m_doc.setLayerOpacity(sel.first()->data(0, Qt::UserRole + 1).toULongLong(), v / 100.0f);
    m_tab->afterEdit();
}
