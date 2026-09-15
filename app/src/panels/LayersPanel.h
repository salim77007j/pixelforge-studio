#pragma once
#include <QTreeWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QJsonObject>

class EditorTab;
class MainWindow;

// Layers panel: tree with thumbnails, eye/lock toggles, blend+opacity controls,
// footer buttons for new/dup/group/mask/up/down/merge/delete.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(MainWindow *mw, QWidget *parent = nullptr);
    void refresh(EditorTab *tab);
    void setActiveTab(EditorTab *tab) { m_tab = tab; }

private slots:
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem *item, int col);
    void onItemMoved();
    void onAddLayer();
    void onAddGroup();
    void onDuplicate();
    void onDelete();
    void onAddMask();
    void onMoveUp();
    void onMoveDown();
    void onMergeDown();
    void onBlendChanged(const QString &name);
    void onOpacityChanged(int v);

private:
    void buildTree(QTreeWidgetItem *parentItem, const QJsonArray &arr);
    void reselectCurrent();
    MainWindow *m_mw;
    EditorTab *m_tab = nullptr;
    QTreeWidget *m_tree = nullptr;
    QComboBox *m_blend = nullptr;
    QSlider *m_opacity = nullptr;
    QLabel *m_opacityLabel = nullptr;
    bool m_rebuilding = false;
};
