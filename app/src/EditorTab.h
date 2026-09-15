#pragma once
#include <QWidget>
#include "Engine.h"
#include "CanvasView.h"

class QVBoxLayout;
class MainWindow;

// One open document = one tab. Owns the engine doc handle and canvas view.
class EditorTab : public QWidget {
    Q_OBJECT
public:
    explicit EditorTab(MainWindow *mw, PFDoc doc, const QString &fallbackTitle, QWidget *parent = nullptr);
    ~EditorTab();

    EngineDoc &doc() { return m_doc; }
    CanvasView *canvas() const { return m_canvas; }
    QString title() const;
    bool isModified() const;
    void markSaved();

    // helpers used by canvas & panels
    const QVector<float> &selectionOutline();
    void afterEdit();            // full refresh: composite + panels + titles
    void afterStructureEdit();   // layer list changed as well
    void refreshTitle();

signals:
    void titleChanged(EditorTab *);
    void editOccurred(EditorTab *);

public:
    MainWindow *m_mw;
    EngineDoc m_doc;
    CanvasView *m_canvas = nullptr;
    QString m_fallbackTitle;
    QVector<float> m_antsCache;
    bool m_antsValid = false;
};
