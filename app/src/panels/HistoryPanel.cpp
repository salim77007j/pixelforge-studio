#include "HistoryPanel.h"
#include "../MainWindow.h"
#include "../EditorTab.h"
#include "../Icons.h"
#include "../Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>

HistoryPanel::HistoryPanel(MainWindow *mw, QWidget *parent)
    : QWidget(parent), m_mw(mw) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    m_list = new QListWidget;
    m_list->setAlternatingRowColors(false);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:1px solid %2; border-radius:9px; }"
        "QListWidget::item { height:30px; padding:2px 8px; border-radius:6px; }"
        "QListWidget::item:hover { background:%3; }"
        "QListWidget::item:selected { background:%4; color:%5; }"
    ).arg(Theme::CardBg, Theme::Border, Theme::Hover, Theme::Accent, "#FFFFFF"));
    connect(m_list, &QListWidget::itemActivated, this, &HistoryPanel::onActivated);
    connect(m_list, &QListWidget::itemClicked, this, &HistoryPanel::onActivated);

    auto *btnRow = new QHBoxLayout;
    auto *clearBtn = new QToolButton;
    clearBtn->setText(tr("Clear History"));
    clearBtn->setIcon(Icons::get(Icons::Trash));
    clearBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    clearBtn->setStyleSheet("QToolButton{color:#6B7280;}");
    connect(clearBtn, &QToolButton::clicked, this, &HistoryPanel::onClear);
    btnRow->addStretch(1);
    btnRow->addWidget(clearBtn);

    lay->addWidget(m_list, 1);
    lay->addLayout(btnRow);
}

void HistoryPanel::refresh(EditorTab *tab) {
    m_tab = tab;
    m_rebuilding = true;
    m_list->clear();
    if (!tab || !tab->m_doc.isValid()) { m_rebuilding = false; return; }
    int count = tab->m_doc.historyCount();
    int idx = tab->m_doc.historyIndex();
    for (int i = 0; i < count; ++i) {
        QString label = tab->m_doc.historyLabel(i);
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
    if (idx >= 0 && idx < m_list->count()) {
        m_list->setCurrentRow(idx);
        m_list->scrollToItem(m_list->item(idx), QAbstractItemView::PositionAtCenter);
    }
    m_rebuilding = false;
}

void HistoryPanel::onActivated(QListWidgetItem *item) {
    if (m_rebuilding || !m_tab || !item) return;
    int i = item->data(Qt::UserRole).toInt();
    m_tab->m_doc.historyGoto(i);
    m_tab->afterStructureEdit();
}

void HistoryPanel::onClear() {
    if (!m_tab) return;
    pf_history_clear(m_tab->m_doc.handle());
    m_tab->afterStructureEdit();
}
