#pragma once
#include <QWidget>
#include <QListWidget>

class MainWindow;
class EditorTab;

// History panel: list of engine history states, click to time-travel.
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(MainWindow *mw, QWidget *parent = nullptr);
    void refresh(EditorTab *tab);
    void setActiveTab(EditorTab *tab) { m_tab = tab; }
private slots:
    void onActivated(QListWidgetItem *item);
    void onClear();
private:
    MainWindow *m_mw;
    EditorTab *m_tab = nullptr;
    QListWidget *m_list = nullptr;
    bool m_rebuilding = false;
};
